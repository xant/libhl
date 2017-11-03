#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <strings.h>
#include <sched.h>

#include "bsd_queue.h"
#include "atomic_defs.h"
#include "hashtable.h"

#ifdef USE_PACKED_STRUCTURES
#define PACK_IF_NECESSARY __attribute__((packed))
#else
#define PACK_IF_NECESSARY
#endif

#define HT_KEY_EQUALS(_k1, _kl1, _k2, _kl2) \
            (((char *)(_k1))[0] == ((char *)(_k2))[0] && \
            (_kl1) == (_kl2) && \
            memcmp((_k1), (_k2), (_kl1)) == 0)


typedef struct _ht_item {
    uint32_t hash;
    char     kbuf[32];
    void    *key;
    size_t   klen;
    void    *data;
    size_t   dlen;
    TAILQ_ENTRY(_ht_item) next;
} PACK_IF_NECESSARY ht_item_t;

typedef struct _ht_item_list {
    TAILQ_HEAD(, _ht_item) head;
#ifdef THREAD_SAFE
#ifdef __MACH__
    OSSpinLock lock;
#else
    pthread_spinlock_t lock;
#endif
#endif
    size_t index;
    TAILQ_ENTRY(_ht_item_list) iterator_next;
} PACK_IF_NECESSARY ht_items_list_t;

typedef struct {
    TAILQ_HEAD(, _ht_item_list) head;
} PACK_IF_NECESSARY ht_iterator_list_t;

// NOTE : order here matters (and also numbering)
typedef enum {
    HT_STATUS_CLEAR = 0,
    HT_STATUS_WRITE = 1,
    HT_STATUS_GROW  = 2,
    HT_STATUS_IDLE  = 3,
    HT_STATUS_READ  = 4
} PACK_IF_NECESSARY ht_status_t;

struct _hashtable_s {
    size_t size;
    size_t max_size;
    size_t count;
    ht_status_t status;
    uint32_t seed;
    ht_items_list_t **items;
    ht_free_item_callback_t free_item_cb;
    ht_iterator_list_t *iterator_list;
#ifdef THREAD_SAFE
    pthread_mutex_t iterator_lock;
#endif
} PACK_IF_NECESSARY;

typedef struct _ht_iterator_callback {
    int (*cb)();
    void *user;
    size_t count;
    hashtable_t *table;
} ht_iterator_callback_t;

typedef struct _ht_collector_arg {
    linked_list_t *output;
    size_t count;
} ht_collector_arg_t;

static inline uint32_t
ht_hash_one_at_a_time(hashtable_t *table, const unsigned char *str, const ssize_t len)
{
    const unsigned char * const end = (const unsigned char *)str + len;
    uint32_t hash = table->seed + len;
    while (str < end) {
        hash += *str++;
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    return (hash + (hash << 15));
}


hashtable_t *
ht_create(size_t initial_size, size_t max_size, ht_free_item_callback_t cb)
{
    hashtable_t *table = (hashtable_t *)calloc(1, sizeof(hashtable_t));

    if (table && ht_init(table, initial_size, max_size, cb) != 0) {
        free(table);
        return NULL;
    }

    return table;
}

int
ht_init(hashtable_t *table,
        size_t initial_size,
        size_t max_size,
        ht_free_item_callback_t cb)
{
    table->size = initial_size > HT_SIZE_MIN ? initial_size : HT_SIZE_MIN;
    table->max_size = max_size;
    table->items = (ht_items_list_t **)calloc(table->size, sizeof(ht_items_list_t *));
    if (!table->items)
        return -1;

    table->status = HT_STATUS_IDLE;

    ht_set_free_item_callback(table, cb);
#ifdef BSD
    table->seed = arc4random()%UINT32_MAX;
#else
    table->seed = random()%UINT32_MAX;
#endif
    table->iterator_list = calloc(1, sizeof(ht_iterator_list_t));
    if (!table->iterator_list) {
        free(table->items);
        return -1;
    }
    TAILQ_INIT(&table->iterator_list->head);

    MUTEX_INIT(table->iterator_lock);

    return 0;
}

void
ht_set_free_item_callback(hashtable_t *table, ht_free_item_callback_t cb)
{
    ATOMIC_SET(table->free_item_cb, cb);
}

void
ht_clear(hashtable_t *table)
{
    while(!ATOMIC_CAS(table->status, HT_STATUS_IDLE, HT_STATUS_CLEAR))
        sched_yield();

    MUTEX_LOCK(table->iterator_lock);
    ht_items_list_t *tmplist, *list = NULL;
    TAILQ_FOREACH_SAFE(list, &table->iterator_list->head, iterator_next, tmplist) {
        SPIN_LOCK(list->lock);

        ht_item_t *item = NULL;
        ht_item_t *tmp;

        TAILQ_FOREACH_SAFE(item, &list->head, next, tmp) {
            TAILQ_REMOVE(&list->head, item, next);
            if (table->free_item_cb)
                table->free_item_cb(item->data);
            if (item->key != item->kbuf)
                free(item->key);
            free(item);
            ATOMIC_DECREMENT(table->count);
        }

        table->items[list->index] = NULL;
        TAILQ_REMOVE(&table->iterator_list->head, list, iterator_next);
        SPIN_UNLOCK(list->lock);
        SPIN_DESTROY(list->lock);
        free(list);
    }
    MUTEX_UNLOCK(table->iterator_lock);

    ATOMIC_CAS(table->status, HT_STATUS_CLEAR, HT_STATUS_IDLE);
}

void
ht_destroy(hashtable_t *table)
{
    ht_clear(table);
    free(table->items);
    MUTEX_DESTROY(table->iterator_lock);
    free(table->iterator_list);
    free(table);
}

static inline void
ht_grow_table(hashtable_t *table)
{
    // if we are not able to change the status now, let's return.
    // ht_grow_table() will be called again next time a new key has been set
    if (!ATOMIC_CAS(table->status, HT_STATUS_IDLE, HT_STATUS_GROW))
        return;

    // extra check if the table has been already updated by another thread in the meanwhile
    if (table->max_size && ATOMIC_READ(table->size) >= table->max_size) {
        ATOMIC_CAS(table->status, HT_STATUS_GROW, HT_STATUS_IDLE);
        return;
    }

    ht_iterator_list_t *new_iterator_list = calloc(1, sizeof(ht_iterator_list_t));
    if (!new_iterator_list) {
        ATOMIC_CAS(table->status, HT_STATUS_GROW, HT_STATUS_IDLE);
        return;
    }

    TAILQ_INIT(&new_iterator_list->head);

    size_t new_size = ATOMIC_READ(table->size) << 1;

    if (table->max_size && new_size > table->max_size)
        new_size = table->max_size;

    ht_items_list_t **items_list = 
        (ht_items_list_t **)calloc(new_size, sizeof(ht_items_list_t *));

    if (!items_list) {
        free(new_iterator_list);
        ATOMIC_CAS(table->status, HT_STATUS_GROW, HT_STATUS_IDLE);
        return;
    }

    ht_item_t *item = NULL;

    MUTEX_LOCK(table->iterator_lock);

    ht_items_list_t **old_items = table->items;
    table->items = items_list;
    ATOMIC_SET(table->size, new_size);

    ht_items_list_t *tmp, *list = NULL;
    TAILQ_FOREACH_SAFE(list, &table->iterator_list->head, iterator_next, tmp) {
        // NOTE : list->index is safe to access outside of the lock
        ATOMIC_SET(old_items[list->index], NULL);
        // now readers can't access this list anymore
        SPIN_LOCK(list->lock);
        
        // move all the items from the old list to the new one
        while((item = TAILQ_FIRST(&list->head))) {
            ht_items_list_t *new_list = ATOMIC_READ(items_list[item->hash%new_size]);
            if (!new_list) {
                new_list = malloc(sizeof(ht_items_list_t));
                // XXX - if malloc fails here the table is irremediably corrupted
                //       so there is no point in handling the case.
                //       TODO : using an internal prealloc'd bufferpool would ensure
                //              us to always obtain a valid pointer here
                TAILQ_INIT(&new_list->head);
                SPIN_INIT(new_list->lock);
                size_t index = item->hash%new_size;
                ATOMIC_SET(items_list[index], new_list);
                new_list->index = index;
                TAILQ_INSERT_TAIL(&new_iterator_list->head, new_list, iterator_next);
            }
            TAILQ_REMOVE(&list->head, item, next);
            TAILQ_INSERT_TAIL(&new_list->head, item, next);

        }

        // we can now unregister the list from the iterator and release it
        TAILQ_REMOVE(&table->iterator_list->head, list, iterator_next);
        SPIN_UNLOCK(list->lock);
        SPIN_DESTROY(list->lock);
        free(list);
    }

    // swap the iterator list
    free(table->iterator_list);
    table->iterator_list = new_iterator_list;
    MUTEX_UNLOCK(table->iterator_lock);

    ATOMIC_CAS(table->status, HT_STATUS_GROW, HT_STATUS_IDLE);

    free(old_items);

    //fprintf(stderr, "Done growing table\n");
}

static inline ht_items_list_t *
ht_get_list(hashtable_t *table, uint32_t hash)
{
    size_t index = hash%ATOMIC_READ(table->size);

    // first try updating the status assuming we are the first reader requesting
    // access to the table
    uint32_t status;
    do {
        status = ATOMIC_CAS_RETURN(table->status, HT_STATUS_IDLE, HT_STATUS_READ);
        // NOTE : if some writer is accessing the table we need to wait,
        //        multiple readers (status greater than IDLE) are allowed.
        //        In the unlikely event that sched_yield() fails, we break the loop
        //        but we will still check if the status is valid and in case it's not
        //        (so the cas operation didn't succeed) we will synchronize again with
        //        the other threads
    } while (status < HT_STATUS_IDLE && sched_yield() == 0);

    // if some other reader is running in a background thread and has already
    // updated the status (so it's already greater than IDLE), let's take that
    // into account and try incrementing the status value by one
    while (status != HT_STATUS_IDLE &&
           !(status >= HT_STATUS_READ && ATOMIC_CAS(table->status, status, status + 1)))
    {
        // if we didn't succeed incrementing the status, maybe the other readers finished
        // their job and it was already put back to IDLE
        status = ATOMIC_CAS_RETURN(table->status, HT_STATUS_IDLE, HT_STATUS_READ);
        if (status < HT_STATUS_IDLE) // some writer is accessing the table, we need to wait
            sched_yield();
    }

    status++; // status now holds the value we have updated in table->status

    // we can now safely retrieve the list
    ht_items_list_t *list = table->items[index];

    // NOTE: it's important here to lock the list while the status
    //       has not been put back to idle yet (so writes can't happen)
    if (list)
        SPIN_LOCK(list->lock);

    // now let's update the status by decrementing it
    // NOTE: if we are the last active reader it will go down to the idle state
    do {
        if (ATOMIC_CAS(table->status, status, status -1))
            break;
        status = ATOMIC_CAS_RETURN(table->status, HT_STATUS_READ, HT_STATUS_IDLE);
    } while (status > HT_STATUS_READ);

    // NOTE: the returned list is already locked
    return list;
}

static inline ht_items_list_t *
ht_set_list(hashtable_t *table, uint32_t hash)
{
    ht_items_list_t *list = malloc(sizeof(ht_items_list_t));
    if (!list)
        return NULL;

    SPIN_INIT(list->lock);
    TAILQ_INIT(&list->head);
    SPIN_LOCK(list->lock);

    size_t index = hash%ATOMIC_READ(table->size);
    list->index = index;

    while (!ATOMIC_CAS(table->status, HT_STATUS_IDLE, HT_STATUS_WRITE))
        sched_yield();

    // NOTE: once the status has been set to WRITE no other threads can access the table

    index = hash%ATOMIC_READ(table->size);
    list->index = index;

    // NOTE: since nobody could have changed the status in the meanwhile
    if (table->items[index]) {
        // if there is a list already set at our index it means that some other
        // thread succeded in setting a new list already, completing its job before
        // we were able to update the table status.
        // So we can release our newly created list and return the existing one
        SPIN_UNLOCK(list->lock);
        SPIN_DESTROY(list->lock);
        free(list);
        list = table->items[index];
        SPIN_LOCK(list->lock);
        ATOMIC_CAS(table->status, HT_STATUS_WRITE, HT_STATUS_IDLE);
        return list;
    }

    table->items[index] = list;

    // it's safe to assume the status still WRITE,
    // so we don't need to check if the CAS operation succeeded
    ATOMIC_CAS(table->status, HT_STATUS_WRITE, HT_STATUS_IDLE);

    MUTEX_LOCK(table->iterator_lock);
    TAILQ_INSERT_TAIL(&table->iterator_list->head, list, iterator_next);
    MUTEX_UNLOCK(table->iterator_lock);

    // NOTE: the newly created list is already locked
    return list;
}

static inline int
ht_set_internal(hashtable_t *table,
                 void *key,
                 size_t klen,
                 void *data,
                 size_t dlen,
                 void **prev_data,
                 size_t *prev_len,
                 int copy,
                 int inx)
{
    void *prev = NULL;
    size_t plen = 0;

    if (!klen)
        return -1;

    uint32_t hash = ht_hash_one_at_a_time(table, key, klen);

    // let's first try checking if we fall in an existing bucket list
    ht_items_list_t *list  = ht_get_list(table, hash);

    if (!list) // if not, let's create a new bucket list
        list = ht_set_list(table, hash);

    if (!list)
        return -1;

    ht_item_t *item = NULL;
    TAILQ_FOREACH(item, &list->head, next) {
        if (/*ht_item->hash == arg->item.hash && */
            HT_KEY_EQUALS(item->key, item->klen, key, klen))
        {
            prev = item->data;
            plen = item->dlen;
            break;
        }
    }

    if (!prev) {
        ht_item_t *item = (ht_item_t *)calloc(1, sizeof(ht_item_t));
        if (!item) {
            //fprintf(stderr, "Can't create new item: %s\n", strerror(errno));
            SPIN_UNLOCK(list->lock);
            return -1;
        }
        item->hash = hash;
        item->klen = klen;

        if (klen > sizeof(item->kbuf)) {
            item->key = malloc(klen);
            if (!item->key) {
                free(item);
                SPIN_UNLOCK(list->lock);
                return -1;
            }
        } else {
            item->key = item->kbuf;
        }

        memcpy(item->key, key, klen);

        if (copy) {
            if (dlen) {
                item->data = malloc(dlen);
                if (!item->data) {
                    if (klen > sizeof(item->kbuf))
                        free(item->key);
                    free(item);
                    SPIN_UNLOCK(list->lock);
                    return -1;
                }
                memcpy(item->data, data, dlen);
            } else {
                item->data = NULL;
            }
        } else {
            item->data = data;
        }
        item->dlen = dlen;

        TAILQ_INSERT_TAIL(&list->head, item, next);
        ATOMIC_INCREMENT(table->count);
    } else {
        if (inx) {
            if (prev_data)
                *prev_data = prev;
            if (prev_len)
                *prev_len = plen;
            SPIN_UNLOCK(list->lock);
            return 1;
        }
        item->dlen = dlen;
        if (copy) {
            void *dcopy = malloc(dlen);
            if (!dcopy) {
                SPIN_UNLOCK(list->lock);
                return -1;
            }

            item->data = dcopy;
            memcpy(item->data, data, dlen);
        } else {
            item->data = data;
        }
    }

    SPIN_UNLOCK(list->lock);

    size_t current_size = ATOMIC_READ(table->size);
    if (ht_count(table) > (current_size + (current_size/3)) && 
        (!table->max_size || current_size < table->max_size))
    {
        ht_grow_table(table);
    }

    if (prev) {
        if (prev_data)
            *prev_data = prev;
        else if (table->free_item_cb)
            table->free_item_cb(prev);
    } else if (prev_data) {
        *prev_data = NULL;
    }

    if (prev_len)
        *prev_len = plen;

    return 0;
}

int
ht_set(hashtable_t *table, void *key, size_t klen, void *data, size_t dlen)
{
    return ht_set_internal(table, key, klen, data, dlen, NULL, NULL, 0, 0);
}

int
ht_set_if_not_exists(hashtable_t *table, void *key, size_t klen, void *data, size_t dlen)
{
    return ht_set_internal(table, key, klen, data, dlen, NULL, NULL, 0, 1);
}

int
ht_get_or_set(hashtable_t *table,
              void *key,
              size_t klen,
              void *data,
              size_t dlen,
              void **cur_data,
              size_t *cur_len)
{
    return ht_set_internal(table, key, klen, data, dlen, cur_data, cur_len, 0, 1);
}

int
ht_get_and_set(hashtable_t *table,
               void *key,
               size_t klen,
               void *data,
               size_t dlen,
               void **prev_data,
               size_t *prev_len)
{
    return ht_set_internal(table, key, klen, data, dlen, prev_data, prev_len, 0, 0);
}

int
ht_set_copy(hashtable_t *table,
            void *key,
            size_t klen,
            void *data,
            size_t dlen,
            void **prev_data,
            size_t *prev_len)
{
    return ht_set_internal(table, key, klen, data, dlen, prev_data, prev_len, 1, 0);
}

static inline int
ht_call_internal(hashtable_t *table,
        void *key,
        size_t klen,
        ht_pair_callback_t cb,
        void *user)
{
    int ret = -1;

    uint32_t hash = ht_hash_one_at_a_time(table, key, klen);

    ht_items_list_t *list  = ht_get_list(table, hash);
    if (!list)
        return ret;

    ht_item_t *item = NULL;
    ht_item_t *tmp;
    TAILQ_FOREACH_SAFE(item, &list->head, next, tmp) {
        if (/*ht_item->hash == arg->item.hash && */
            HT_KEY_EQUALS(item->key, item->klen, key, klen))
        {
            if (cb) {
                ret = cb(table, key, klen, &item->data, &item->dlen, user);
                if (ret == 1) {
                    TAILQ_REMOVE(&list->head, item, next);
                    if (item->key != item->kbuf)
                        free(item->key);
                    free(item);
                    ATOMIC_DECREMENT(table->count);
                    ret = 0;
                }
            } else {
                ret = 0;
            }
            break;
        }
    }

    SPIN_UNLOCK(list->lock);

    return ret;
}

int
ht_call(hashtable_t *table,
        void *key,
        size_t klen,
        ht_pair_callback_t cb,
        void *user)
{
    return ht_call_internal(table, key, klen, cb, user);
}

typedef struct {
    void *data;
    size_t dlen;
    void *match;
    size_t match_size;
    int matched;
    void **prev_data;
    size_t *prev_len;
} ht_set_if_equals_helper_arg_t;

static int
ht_set_if_equals_helper(hashtable_t *table, void *key __attribute__ ((unused)), size_t klen __attribute__ ((unused)), void **value, size_t *vlen, void *user)
{
    ht_set_if_equals_helper_arg_t *arg = (ht_set_if_equals_helper_arg_t *)user;
     
    if (arg->prev_len)
        *arg->prev_len = *vlen;

    if (arg->prev_data)
        *arg->prev_data = *value;

    if (arg->match_size == *vlen && ((char *)*value)[0] == *((char *)arg->match) &&
        memcmp(*value, arg->match, arg->match_size) == 0)
    {
        arg->matched = 1;

        if (!arg->prev_data && table->free_item_cb)
            table->free_item_cb(*value);

        *value = arg->data;
        *vlen = arg->dlen;
    }

    return 0;
}

int
ht_set_if_equals(hashtable_t *table,
                 void *key,
                 size_t klen,
                 void *data,
                 size_t dlen,
                 void *match,
                 size_t match_size,
                 void **prev_data,
                 size_t *prev_len)
{
    if (!match && match_size == 0)
        return ht_set_if_not_exists(table, key, klen, data, dlen);

    ht_set_if_equals_helper_arg_t arg = {
        .data = data,
        .dlen = dlen,
        .match = match,
        .match_size = match_size,
        .matched = 0,
        .prev_data = prev_data,
        .prev_len = prev_len
    };
    if (ht_call_internal(table, key, klen, ht_set_if_equals_helper, (void *)&arg) == 0)
    {
        return arg.matched ? 0 : 1;
    }
    return -1;
}


typedef struct
{
    int  unset;
    void **prev_data;
    size_t *prev_len;
    void *match;
    size_t match_size;
} ht_delete_helper_arg_t;

static int
ht_delete_helper(hashtable_t *table, void *key __attribute__ ((unused)), size_t klen __attribute__ ((unused)), void **value, size_t *vlen, void *user)
{
    ht_delete_helper_arg_t *arg = (ht_delete_helper_arg_t *)user;

    if (arg->match && (arg->match_size != *vlen || memcmp(arg->match, *value, *vlen) != 0))
        return -1;

    if (arg->prev_data)
        *arg->prev_data = *value;
    else if (table->free_item_cb)
        table->free_item_cb(*value);
    
    if (arg->prev_len)
        *arg->prev_len = *vlen;

    if (arg->unset) {
        *vlen = 0;
        *value = NULL;
        return 0;
    }

    return 1; // we want the item to be removed
}

int
ht_unset(hashtable_t *table,
         void *key,
         size_t klen,
         void **prev_data,
         size_t *prev_len)
{
    ht_delete_helper_arg_t arg = {
        .unset = 1,
        .prev_data = prev_data,
        .prev_len = prev_len,
        .match = NULL,
        .match_size = 0
    };

    return ht_call_internal(table, key, klen, ht_delete_helper, (void *)&arg);
}

static inline int
ht_delete_internal (hashtable_t *table,
                    void *key,
                    size_t klen,
                    void **prev_data,
                    size_t *prev_len,
                    void *match,
                    size_t match_size)
{

    ht_delete_helper_arg_t arg = {
        .unset = 0,
        .prev_data = prev_data,
        .prev_len = prev_len,
        .match = match,
        .match_size = match_size
    };

    return ht_call_internal(table, key, klen, ht_delete_helper, (void *)&arg);
}

int
ht_delete (hashtable_t *table,
           void *key,
           size_t klen,
           void **prev_data,
           size_t *prev_len)
{
    return ht_delete_internal(table, key, klen, prev_data, prev_len, NULL, 0);
}

int
ht_delete_if_equals(hashtable_t *table, void *key, size_t klen, void *match, size_t match_size)
{
    return ht_delete_internal(table, key, klen, NULL, NULL, match, match_size);
}

int
ht_exists(hashtable_t *table, void *key, size_t klen)
{
    return (ht_call_internal(table, key, klen, NULL, NULL) == 0);
}

typedef struct {
    void *data;
    size_t *dlen;
    int copy;
    ht_deep_copy_callback_t copy_cb;
    void *user;
} ht_get_helper_arg_t;

static int
ht_get_helper(hashtable_t *table __attribute__ ((unused)), void *key __attribute__ ((unused)), size_t klen __attribute__ ((unused)), void **value, size_t *vlen, void *user)
{
    ht_get_helper_arg_t *arg = (ht_get_helper_arg_t *)user;

    if (arg->copy) {
        if (arg->copy_cb) {
            arg->data = arg->copy_cb(*value, *vlen, arg->user);
        } else {
            arg->data = malloc(*vlen);
            if (!arg->data)
                return -1;
            memcpy(arg->data, *value, *vlen);
        }
    } else {
        arg->data = *value;
    }

    if (arg->dlen)
        *arg->dlen = *vlen;


    return 0;
}

static inline void *
ht_get_internal(hashtable_t *table,
                void *key,
                size_t klen,
                size_t *dlen,
                int copy,
                ht_deep_copy_callback_t copy_cb,
                void *user)
{
    ht_get_helper_arg_t arg = {
        .data = NULL,
        .dlen = dlen,
        .copy = copy,
        .copy_cb = copy_cb,
        .user = user
    };

    ht_call_internal(table, key, klen, ht_get_helper, (void *)&arg);

    return arg.data;
}

void *
ht_get(hashtable_t *table, void *key, size_t klen, size_t *dlen)
{
    return ht_get_internal(table, key, klen, dlen, 0, NULL, NULL);
}

void *
ht_get_copy(hashtable_t *table, void *key, size_t klen, size_t *dlen)
{
    return ht_get_internal(table, key, klen, dlen, 1, NULL, NULL);
}

void *
ht_get_deep_copy(hashtable_t *table, void *key, size_t klen,
        size_t *dlen, ht_deep_copy_callback_t copy_cb, void *user)
{
    return ht_get_internal(table, key, klen, dlen, 1, copy_cb, user);
}

static void
free_key(hashtable_key_t *key)
{
    free(key->data);
    free(key);
}

linked_list_t *
ht_get_all_keys(hashtable_t *table)
{
    linked_list_t *output = list_create();
    list_set_free_value_callback(output, (free_value_callback_t)free_key);

    MUTEX_LOCK(table->iterator_lock);
    ht_items_list_t *list = NULL;
    TAILQ_FOREACH(list, &table->iterator_list->head, iterator_next) {
        SPIN_LOCK(list->lock);

        ht_item_t *item = NULL;
        TAILQ_FOREACH(item, &list->head, next) {
            hashtable_key_t *key = malloc(sizeof(hashtable_key_t));
            if (!key) {
                SPIN_UNLOCK(list->lock);
                MUTEX_UNLOCK(table->iterator_lock);
                list_destroy(output);
                return NULL;
            }
            key->data = malloc(item->klen);
            if (!key->data) {
                SPIN_UNLOCK(list->lock);
                MUTEX_UNLOCK(table->iterator_lock);
                free(key);
                list_destroy(output);
                return NULL;
            }
            memcpy(key->data, item->key, item->klen);
            key->len = item->klen;
            key->vlen = item->dlen;
            list_push_value(output, key);
        }

        SPIN_UNLOCK(list->lock);
    }
    MUTEX_UNLOCK(table->iterator_lock);
    return output;
}

linked_list_t *
ht_get_all_values(hashtable_t *table)
{
    linked_list_t *output = list_create();
    list_set_free_value_callback(output, (free_value_callback_t)free);

    MUTEX_LOCK(table->iterator_lock);
    ht_items_list_t *list = NULL;
    TAILQ_FOREACH(list, &table->iterator_list->head, iterator_next) {
        SPIN_LOCK(list->lock);

        ht_item_t *item = NULL;
        TAILQ_FOREACH(item, &list->head, next) {
            hashtable_value_t *v = malloc(sizeof(hashtable_value_t));
            if (!v) {
                SPIN_UNLOCK(list->lock);
                MUTEX_UNLOCK(table->iterator_lock);
                list_destroy(output);
                return NULL;
            }
            v->data = item->data;
            v->len = item->dlen;
            v->key = item->key;
            v->klen = item->klen;
            list_push_value(output, v);
        }

        SPIN_UNLOCK(list->lock);
    }
    MUTEX_UNLOCK(table->iterator_lock);
    return output;
}

typedef struct {
    int (*cb)();
    void *user;
} ht_iterator_arg_t;

static int
ht_foreach_key_helper(hashtable_t *table, void *key, size_t klen, void *value __attribute__ ((unused)), size_t vlen __attribute__ ((unused)), void *user)
{
    ht_iterator_arg_t *arg = (ht_iterator_arg_t *)user;
    return arg->cb(table, key, klen, arg->user);
}

void
ht_foreach_key(hashtable_t *table, ht_key_iterator_callback_t cb, void *user)
{
    ht_iterator_arg_t arg = { cb, user };
    ht_foreach_pair(table, ht_foreach_key_helper, &arg);
}

static int
ht_foreach_value_helper(hashtable_t *table, void *key __attribute__ ((unused)), size_t klen __attribute__ ((unused)), void *value, size_t vlen, void *user)
{
    ht_iterator_arg_t *arg = (ht_iterator_arg_t *)user;
    return arg->cb(table, value, vlen, arg->user);
}

void
ht_foreach_value(hashtable_t *table, ht_value_iterator_callback_t cb, void *user)
{
    ht_iterator_arg_t arg = { cb, user };
    ht_foreach_pair(table, ht_foreach_value_helper, &arg);
}

void
ht_foreach_pair(hashtable_t *table, ht_pair_iterator_callback_t cb, void *user)
{
    int rc = 0;


    MUTEX_LOCK(table->iterator_lock);
    ht_items_list_t *list = NULL;
    TAILQ_FOREACH(list, &table->iterator_list->head, iterator_next) {
        SPIN_LOCK(list->lock);
        ht_item_t *item = NULL;
        TAILQ_FOREACH(item, &list->head, next) {
            rc = cb(table, item->key, item->klen, item->data, item->dlen, user);
            if (rc <= 0)
                break;
        }

        if (item) {
            if (rc == 0) {
                SPIN_UNLOCK(list->lock);
                break;
            } else if (rc < 0) {
                TAILQ_REMOVE(&list->head, item, next);
                if (table->free_item_cb)
                    table->free_item_cb(item->data);
                if (item->key != item->kbuf)
                    free(item->key);
                free(item);
                if (rc == -2) {
                    SPIN_UNLOCK(list->lock);
                    break;
                }
            }
        }
        SPIN_UNLOCK(list->lock);
    }
    MUTEX_UNLOCK(table->iterator_lock);
}

size_t
ht_count(hashtable_t *table)
{
    return ATOMIC_READ(table->count);
}

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
