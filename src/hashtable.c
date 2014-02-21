#include <hashtable.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <limits.h>
#include <strings.h>

#ifdef __MACH__
#include <libkern/OSAtomic.h>
#endif

#define PERL_HASH_FUNC_ONE_AT_A_TIME
#define PERL_STATIC_INLINE static inline
#define U8  uint8_t
#define U16 uint16_t
#define U32 uint32_t
#define I32 int32_t
#define STRLEN ssize_t
#define STMT_START
#define STMT_END
#include <hv_func.h>

#include "bsd_queue.h"

#ifdef THREAD_SAFE
#define MUTEX_INIT(__mutex) pthread_mutex_init(&(__mutex), 0)
#define MUTEX_DESTROY(__mutex) pthread_mutex_destroy(&(__mutex))
#define MUTEX_LOCK(__mutex) pthread_mutex_lock(&(__mutex))
#define MUTEX_UNLOCK(__mutex) pthread_mutex_unlock(&(__mutex))
#ifdef __MACH__
#define SPIN_INIT(__mutex) ((__mutex) = 0)
#define SPIN_DESTROY(__mutex)
#define SPIN_LOCK(__mutex) OSSpinLockLock(&(__mutex))
#define SPIN_UNLOCK(__mutex) OSSpinLockUnlock(&(__mutex))
#else
#define SPIN_INIT(__mutex) pthread_spin_init(&(__mutex), 0)
#define SPIN_DESTROY(__mutex) pthread_spin_destroy(&(__mutex))
#define SPIN_LOCK(__mutex) pthread_spin_lock(&(__mutex))
#define SPIN_UNLOCK(__mutex) pthread_spin_unlock(&(__mutex))
#endif
#else
#define MUTEX_INIT(__mutex)
#define MUTEX_DESTROY(__mutex)
#define MUTEX_LOCK(__mutex)
#define MUTEX_UNLOCK(__mutex)
#define SPIN_INIT(__mutex)
#define SPIN_DESTROY(__mutex)
#define SPIN_LOCK(__mutex)
#define SPIN_UNLOCK(__mutex)
#endif

#define ATOMIC_READ(__v) __sync_fetch_and_add(&(__v), 0)
#define ATOMIC_INCREMENT(__v) (void)__sync_add_and_fetch(&(__v), 1)
#define ATOMIC_DECREMENT(__v) (void)__sync_sub_and_fetch(&(__v), 1)

#define ATOMIC_SET(__v, __n) {\
    int __b = 0;\
    do {\
        __b = __sync_bool_compare_and_swap(&(__v), ATOMIC_READ(__v), __n);\
    } while (!__b);\
}


#define ATOMIC_GETLIST(__t, __h, __l) \
{ \
    uint32_t __i = (__h)%ATOMIC_READ((__t)->size); \
    __l = ATOMIC_READ((__t)->items[__i]); \
    if (__l) { \
        SPIN_LOCK((__l)->lock); \
    } \
    if (ATOMIC_READ((__t)->growing) > __i) { \
        if (__l) {\
            SPIN_UNLOCK((__l)->lock); \
        } \
        MUTEX_LOCK((__t)->lock); \
        __l = ATOMIC_READ((__t)->items[__i]); \
        MUTEX_UNLOCK((__t)->lock); \
    } \
}

#define ATOMIC_SETLIST(__t, __h, __l) \
{ \
    (__l) = malloc(sizeof(ht_items_list_t)); \
    SPIN_INIT((__l)->lock); \
    TAILQ_INIT(&(__l)->head); \
    SPIN_LOCK((__l)->lock); \
    MUTEX_LOCK((__t)->lock); \
    while (!__sync_bool_compare_and_swap(&(__t)->items[(__h)%ATOMIC_READ((__t)->size)], NULL, __l)) \
    { \
        ht_items_list_t *l = ATOMIC_READ((__t)->items[(__h)%ATOMIC_READ((__t)->size)]); \
        if (l) { \
            SPIN_UNLOCK(__l->lock); \
            SPIN_DESTROY(__l->lock); \
            free(__l); \
            __l = l; \
            break; \
        } \
    } \
    MUTEX_UNLOCK((__t)->lock); \
}


#define HT_GROW_THRESHOLD 16

typedef struct __ht_item {
    uint32_t hash;
    void    *key;
    size_t   klen;
    void    *data;
    size_t   dlen;
    TAILQ_ENTRY(__ht_item) next;
} ht_item_t;

typedef struct __ht_iterator_arg {
    uint32_t  index;
    bool      set;
    ht_item_t item;
} ht_iterator_arg_t;

typedef TAILQ_HEAD(__ht_items_list_head, __ht_item) ht_items_list_head_t;

typedef struct {
    TAILQ_HEAD(, __ht_item) head;
#ifdef THREAD_SAFE
#ifdef __MACH__
    OSSpinLock lock;
#else
    pthread_spinlock_t lock;
#endif
#endif
} ht_items_list_t;

struct __hashtable {
    uint32_t size;
    uint32_t max_size;
    uint32_t count;
    uint32_t growing;
    ht_items_list_t **items;
#ifdef THREAD_SAFE
    pthread_mutex_t lock;
#endif
    ht_free_item_callback_t free_item_cb;
};

typedef struct __ht_iterator_callback {
    int (*cb)();
    void *user;
    uint32_t count;
    hashtable_t *table;
} ht_iterator_callback_t;

typedef struct __ht_collector_arg {
    linked_list_t *output;
    uint32_t count;
} ht_collector_arg_t;

hashtable_t *ht_create(uint32_t initial_size, uint32_t max_size, ht_free_item_callback_t cb) {
    hashtable_t *table = (hashtable_t *)calloc(1, sizeof(hashtable_t));

    if (table)
        ht_init(table, initial_size, max_size, cb);

    return table;
}

void ht_init(hashtable_t *table, uint32_t initial_size, uint32_t max_size, ht_free_item_callback_t cb) {
    table->size = initial_size > 256 ? initial_size : 256;
    table->max_size = max_size;
    table->items = (ht_items_list_t **)calloc(table->size, sizeof(ht_items_list_t *));

    if (table->items)
        MUTEX_INIT(table->lock);

    ht_set_free_item_callback(table, cb);
}

void ht_set_free_item_callback(hashtable_t *table, ht_free_item_callback_t cb)
{
    ATOMIC_SET(table->free_item_cb, cb);
}

void ht_clear(hashtable_t *table) {
    uint32_t i;
    MUTEX_LOCK(table->lock);
    for (i = 0; i < table->size; i++)
    {
        ht_item_t *item = NULL;
        ht_item_t *tmp;

        ht_items_list_t *list = ATOMIC_READ(table->items[i]);

        if (!list)
            continue;

        TAILQ_FOREACH_SAFE(item, &list->head, next, tmp) {
            TAILQ_REMOVE(&list->head, item, next);
            if (table->free_item_cb)
                table->free_item_cb(item->data);
            free(item->key);
            free(item);
            ATOMIC_DECREMENT(table->count);
        }

        ATOMIC_SET(table->items[i], NULL);
        SPIN_DESTROY(list->lock);
        free(list);
    }
    MUTEX_UNLOCK(table->lock);
}

void ht_destroy(hashtable_t *table) {
    ht_clear(table);
    free(table->items);
    MUTEX_DESTROY(table->lock);
    free(table);
}

void ht_grow_table(hashtable_t *table) {
    // if we need to extend the table, better locking it globally
    // preventing any operation on the actual one
    MUTEX_LOCK(table->lock);

    if (table->max_size && ATOMIC_READ(table->size) >= table->max_size) {
        MUTEX_UNLOCK(table->lock);
        return;
    }

    uint32_t i;
    uint32_t new_size = ATOMIC_READ(table->size) << 1;

    if (table->max_size && new_size > table->max_size)
        new_size = table->max_size;

    //fprintf(stderr, "Growing table from %u to %u\n", __sync_fetch_and_add(&table->size, 0), new_size);

    ht_items_list_t **items_list = 
        (ht_items_list_t **)calloc(new_size, sizeof(ht_items_list_t *));

    if (!items_list) {
        //fprintf(stderr, "Can't create new items array list: %s\n", strerror(errno));
        return;
    }

    ht_item_t *item = NULL;

    for (i = 0; i < ATOMIC_READ(table->size); i++) {
        ATOMIC_INCREMENT(table->growing);

        ht_items_list_t *list = NULL;
        do {
            list = ATOMIC_READ(table->items[i]);
        } while (!__sync_bool_compare_and_swap(&table->items[i], list, NULL));

        if (!list)
            continue;

        SPIN_LOCK(list->lock);
        while((item = TAILQ_FIRST(&list->head))) {
            TAILQ_REMOVE(&list->head, item, next);
            ht_items_list_t *new_list = items_list[item->hash%new_size];
            if (!new_list) {
                new_list = malloc(sizeof(ht_items_list_t));
                TAILQ_INIT(&new_list->head);
                SPIN_INIT(new_list->lock);
                items_list[item->hash%new_size] = new_list;
            }
            TAILQ_INSERT_TAIL(&new_list->head, item, next);
        }
        SPIN_UNLOCK(list->lock);
        SPIN_DESTROY(list->lock);
        free(list);
    }

    ATOMIC_SET(table->items, items_list);

    ATOMIC_SET(table->size, new_size);

    ATOMIC_SET(table->growing, 0);

    MUTEX_UNLOCK(table->lock);
    //fprintf(stderr, "Done growing table\n");
}

int _ht_set_internal(hashtable_t *table, void *key, size_t klen,
        void *data, size_t dlen, void **prev_data, size_t *prev_len, int copy)
{
    uint32_t hash;
    void *prev = NULL;
    size_t plen = 0;

    if (!klen)
        return -1;

    PERL_HASH(hash, key, klen);

    ht_items_list_t *list;

    ATOMIC_GETLIST(table, hash, list);

    if (!list)
        ATOMIC_SETLIST(table, hash, list);

    ht_item_t *item = NULL;
    TAILQ_FOREACH(item, &list->head, next) {
        if (/*ht_item->hash == arg->item.hash && */
            ((char *)item->key)[0] == ((char *)key)[0] &&
            item->klen == klen &&
            memcmp(item->key, key, klen) == 0)
        {
            prev = item->data;
            plen = item->dlen;
            item->dlen = dlen;
            if (copy) {
                item->data = malloc(dlen);
                memcpy(item->data, data, dlen);
            } else {
                item->data = data;
            }
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
        item->key = malloc(klen);
        if (!item->key) {
            //fprintf(stderr, "Can't copy key: %s\n", strerror(errno));
            SPIN_UNLOCK(list->lock);
            free(item);
            return -1;
        }
        memcpy(item->key, key, klen);

        if (copy) {
            if (dlen) {
                item->data = malloc(dlen);
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
    }

    SPIN_UNLOCK(list->lock);

    if (ht_count(table) > ATOMIC_READ(table->size) + HT_GROW_THRESHOLD && 
        (!table->max_size || ATOMIC_READ(table->size) < table->max_size) &&
        !ATOMIC_READ(table->growing))
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

int ht_set(hashtable_t *table, void *key, size_t len, void *data, size_t dlen) {
    return _ht_set_internal(table, key, len, data, dlen, NULL, 0, 0);
}

int ht_get_and_set(hashtable_t *table, void *key, size_t len, void *data, size_t dlen, void **prev_data, size_t *prev_len) {
    return _ht_set_internal(table, key, len, data, dlen, prev_data, prev_len, 0);
}

int ht_set_copy(hashtable_t *table, void *key, size_t len, void *data, size_t dlen, void **prev_data, size_t *prev_len) {
    return _ht_set_internal(table, key, len, data, dlen, prev_data, prev_len, 1);
}

int ht_unset(hashtable_t *table, void *key, size_t klen, void **prev_data, size_t *prev_len) {
    uint32_t hash;

    PERL_HASH(hash, key, klen);


    ht_items_list_t *list;
    ATOMIC_GETLIST(table, hash, list);

    if (!list)
        return -1;

    void *prev = NULL;
    size_t plen = 0;

    ht_item_t *item = NULL;
    TAILQ_FOREACH(item, &list->head, next) {
        if (/*ht_item->hash == arg->item.hash && */
            ((char *)item->key)[0] == ((char *)key)[0] &&
            item->klen == klen &&
            memcmp(item->key, key, klen) == 0)
        {
            prev = item->data;
            plen = item->dlen;
            item->data = NULL;
            item->dlen = 0;
            break;
        }
    }

    SPIN_UNLOCK(list->lock);

    if (prev) {
        if (prev_data)
            *prev_data = prev;
        else if (table->free_item_cb)
            table->free_item_cb(prev);

        if (prev_len)
            *prev_len = plen;
    }

    return 0;
}

int ht_delete(hashtable_t *table, void *key, size_t klen, void **prev_data, size_t *prev_len) {
    uint32_t hash;
    int ret = -1;

    PERL_HASH(hash, key, klen);

    ht_items_list_t *list;
    ATOMIC_GETLIST(table, hash, list);

    if (!list)
        return ret;

    void *prev = NULL;
    size_t plen = 0;

    ht_item_t *item = NULL;
    ht_item_t *tmp;
    TAILQ_FOREACH_SAFE(item, &list->head, next, tmp) {
        if (/*ht_item->hash == arg->item.hash && */
            ((char *)item->key)[0] == ((char *)key)[0] &&
            item->klen == klen &&
            memcmp(item->key, key, klen) == 0)
        {
            prev = item->data;
            plen = item->dlen;
            TAILQ_REMOVE(&list->head, item, next);
            free(item->key);
            free(item);
            ATOMIC_DECREMENT(table->count);
            break;
        }
    }

    SPIN_UNLOCK(list->lock);

    if (prev) {
        if (prev_data)
            *prev_data = prev;
        else if (table->free_item_cb)
            table->free_item_cb(prev);

        if (prev_len)
            *prev_len = plen;

        ret = 0;
    }

    return ret;
}

int ht_exists(hashtable_t *table, void *key, size_t klen)
{
    uint32_t hash = 0;

    PERL_HASH(hash, key, klen);
    ht_items_list_t *list;
    ATOMIC_GETLIST(table, hash, list);

    if (!list)
        return 0;

    ht_item_t *item = NULL;
    TAILQ_FOREACH(item, &list->head, next) {
        if (/*ht_item->hash == arg->item.hash && */
            ((char *)item->key)[0] == ((char *)key)[0] &&
            item->klen == klen &&
            memcmp(item->key, key, klen) == 0)
        {
            SPIN_UNLOCK(list->lock);
            return 1;
        }
    }

    SPIN_UNLOCK(list->lock);
    return 0;
}

void *_ht_get_internal(hashtable_t *table, void *key, size_t klen,
        size_t *dlen, int copy, ht_deep_copy_callback_t copy_cb)
{
    uint32_t hash = 0;

    PERL_HASH(hash, key, klen);
    ht_items_list_t *list;
    ATOMIC_GETLIST(table, hash, list);

    if (!list)
        return NULL;

    void *data = NULL;

    ht_item_t *item = NULL;
    TAILQ_FOREACH(item, &list->head, next) {
        if (/*ht_item->hash == arg->item.hash && */
            ((char *)item->key)[0] == ((char *)key)[0] &&
            item->klen == klen &&
            memcmp(item->key, key, klen) == 0)
        {
            if (copy) {
                if (copy_cb) {
                    data = copy_cb(item->data, item->dlen);
                } else {
                    data = malloc(item->dlen);
                    memcpy(data, item->data, item->dlen);
                }
            } else {
                data = item->data;
            }
            if (dlen)
                *dlen = item->dlen;

            break;
        }
    }

    SPIN_UNLOCK(list->lock);

    return data;
}

void *ht_get(hashtable_t *table, void *key, size_t klen, size_t *dlen) {
    return _ht_get_internal(table, key, klen, dlen, 0, NULL);
}

void *ht_get_copy(hashtable_t *table, void *key, size_t klen, size_t *dlen) {
    return _ht_get_internal(table, key, klen, dlen, 1, NULL);
}

void *ht_get_deep_copy(hashtable_t *table, void *key, size_t klen,
        size_t *dlen, ht_deep_copy_callback_t copy_cb)
{
    return _ht_get_internal(table, key, klen, dlen, 1, copy_cb);
}

static void free_key(hashtable_key_t *key) {
    free(key->data);
    free(key);
}

linked_list_t *ht_get_all_keys(hashtable_t *table) {
    uint32_t i;

    linked_list_t *output = create_list();
    set_free_value_callback(output, (free_value_callback_t)free_key);

    for (i = 0; i < ATOMIC_READ(table->size); i++) {
        ht_items_list_t *list = ATOMIC_READ(table->items[i]);

        if (!list) {
            continue;
        }

        SPIN_LOCK(list->lock);

        ht_item_t *item = NULL;
        TAILQ_FOREACH(item, &list->head, next) {
            hashtable_key_t *key = malloc(sizeof(hashtable_key_t));
            key->data = malloc(item->klen);
            memcpy(key->data, item->key, item->klen);
            key->len = item->klen;
            key->vlen = item->dlen;
            push_value(output, key);
        }

        SPIN_UNLOCK(list->lock);
    }
    return output;
}

linked_list_t *ht_get_all_values(hashtable_t *table) {
    uint32_t i;

    linked_list_t *output = create_list();

    for (i = 0; i < ATOMIC_READ(table->size); i++) {
        ht_items_list_t *list = ATOMIC_READ(table->items[i]);

        if (!list) {
            continue;
        }

        SPIN_LOCK(list->lock);

        ht_item_t *item = NULL;
        TAILQ_FOREACH(item, &list->head, next) {
            hashtable_value_t *v = malloc(sizeof(hashtable_value_t));
            v->data = malloc(item->dlen);
            v->len = item->dlen;
            push_value(output, v);
        }

        SPIN_UNLOCK(list->lock);
    }
    return output;
}

void ht_foreach_key(hashtable_t *table, ht_key_iterator_callback_t cb, void *user) {
    uint32_t i;

    for (i = 0; i < ATOMIC_READ(table->size); i++) {
        ht_items_list_t *list = ATOMIC_READ(table->items[i]);

        if (!list) {
            continue;
        }
        
        // Note: once we acquired the linklist, we don't need to lock the whole
        // table anymore. We need to lock the list though to prevent it from
        // being destroyed while we are still accessing it (or iterating over)
        SPIN_LOCK(list->lock);

        ht_item_t *item = NULL;
        TAILQ_FOREACH(item, &list->head, next) {
            int rc = cb(table, item->key, item->klen, user);
            if (rc == 0)
                break;
        }

        SPIN_UNLOCK(list->lock);
    }
}

void ht_foreach_value(hashtable_t *table, ht_value_iterator_callback_t cb, void *user) {
    uint32_t i;

    for (i = 0; i < ATOMIC_READ(table->size); i++) {
        ht_items_list_t *list = ATOMIC_READ(table->items[i]);

        if (!list) {
            continue;
        }

        // Note: once we acquired the linklist, we don't need to lock the whole
        // table anymore. We need to lock the list though to prevent it from
        // being destroyed while we are still accessing it (or iterating over)
        SPIN_LOCK(list->lock);

        ht_item_t *item = NULL;
        TAILQ_FOREACH(item, &list->head, next) {
            int rc = cb(table, item->data, item->dlen, user);
            if (rc == 0)
                break;
        }

        SPIN_UNLOCK(list->lock);
    }
}

void ht_foreach_pair(hashtable_t *table, ht_pair_iterator_callback_t cb, void *user) {
    uint32_t i;

    for (i = 0; i < ATOMIC_READ(table->size); i++) {
        ht_items_list_t *list = ATOMIC_READ(table->items[i]);

        if (!list) {
            continue;
        }

        // Note: once we acquired the linklist, we don't need to lock the whole
        // table anymore. We need to lock the list though to prevent it from
        // being destroyed while we are still accessing it (or iterating over)
        SPIN_LOCK(list->lock);

        ht_item_t *item = NULL;
        TAILQ_FOREACH(item, &list->head, next) {
            int rc = cb(table, item->key, item->klen, item->data, item->dlen, user);
            if (rc == 0)
                break;
        }

        SPIN_UNLOCK(list->lock);
    }
}

uint32_t ht_count(hashtable_t *table) {
    return ATOMIC_READ(table->count);
}

