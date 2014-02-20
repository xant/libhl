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
#ifdef __MACH__
#define SPIN_LOCK(__mutex) OSSpinLockLock(__mutex)
#define SPIN_UNLOCK(__mutex) OSSpinLockUnlock(__mutex)
#else
#define SPIN_LOCK(__mutex) pthread_spin_lock(__mutex)
#define SPIN_UNLOCK(__mutex) pthread_spin_unlock(__mutex)
#endif
#else
#define SPIN_LOCK(__mutex)
#define SPIN_UNLOCK(__mutex)
#endif

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
    int growing;
    ht_items_list_t **items;
#ifdef THREAD_SAFE
#ifdef __MACH__
    OSSpinLock lock;
#else
    pthread_spinlock_t lock;
#endif
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

    if (table->items) {
#ifdef THREAD_SAFE
#ifdef __MACH__
        table->lock = 0;
#else
        pthread_spin_init(&table->lock, 0);
#endif
#endif
    }
    ht_set_free_item_callback(table, cb);
}

void ht_set_free_item_callback(hashtable_t *table, ht_free_item_callback_t cb)
{
    ht_free_item_callback_t ocb = NULL;
    do {
         ocb = __sync_fetch_and_add(&table->free_item_cb, 0);
    } while(!__sync_bool_compare_and_swap(&table->free_item_cb, ocb, cb));
}

void ht_clear(hashtable_t *table) {
    uint32_t i;
    SPIN_LOCK(&table->lock);
    for (i = 0; i < table->size; i++)
    {
        ht_item_t *item = NULL;
        ht_item_t *tmp;

        ht_items_list_t *list = __sync_fetch_and_add(&table->items[i], 0);

        if (!list)
            continue;

        TAILQ_FOREACH_SAFE(item, &list->head, next, tmp) {
            TAILQ_REMOVE(&list->head, item, next);
            if (table->free_item_cb)
                table->free_item_cb(item->data);
            free(item->key);
            free(item);
            __sync_sub_and_fetch(&table->count, 1);
        }

        __sync_bool_compare_and_swap(&table->items[i], list, NULL);
#ifdef THREAD_SAFE
#ifndef __MACH__
        pthread_spin_destroy(&list->lock);
#endif
#endif
        free(list);
    }
    SPIN_UNLOCK(&table->lock);
}

void ht_destroy(hashtable_t *table) {
    ht_clear(table);
    free(table->items);
#ifdef THREAD_SAFE
#ifndef __MACH__
    pthread_spin_destroy(&table->lock);
#endif
#endif
    free(table);
}

void ht_grow_table(hashtable_t *table) {
    // if we need to extend the table, better locking it globally
    // preventing any operation on the actual one
    SPIN_LOCK(&table->lock);
    __sync_add_and_fetch(&table->growing, 1);

    if (table->max_size && __sync_fetch_and_add(&table->size, 0) >= table->max_size) {
        SPIN_UNLOCK(&table->lock);
        __sync_sub_and_fetch(&table->growing, 1);
        return;
    }

    uint32_t i;
    uint32_t newSize = __sync_fetch_and_add(&table->size, 0) << 1;

    if (table->max_size && newSize > table->max_size)
        newSize = table->max_size;

    //fprintf(stderr, "Growing table from %u to %u\n", __sync_fetch_and_add(&table->size, 0), newSize);

    ht_items_list_t **items_list = 
        (ht_items_list_t **)calloc(newSize, sizeof(ht_items_list_t *));

    if (!items_list) {
        //fprintf(stderr, "Can't create new items array list: %s\n", strerror(errno));
        __sync_sub_and_fetch(&table->growing, 1);
        return;
    }

    ht_item_t *item = NULL;

    for (i = 0; i < __sync_fetch_and_add(&table->size, 0); i++) {
        ht_items_list_t *list = NULL;
        do {
            list = __sync_fetch_and_add(&table->items[i], 0);
        } while (!__sync_bool_compare_and_swap(&table->items[i], list, NULL));

        if (!list)
            continue;

        SPIN_LOCK(&list->lock);
        while((item = TAILQ_FIRST(&list->head))) {
            TAILQ_REMOVE(&list->head, item, next);
            ht_items_list_t *new_list = items_list[item->hash%newSize];
            if (!new_list) {
                new_list = malloc(sizeof(ht_items_list_t));
                TAILQ_INIT(&new_list->head);
#ifdef THREAD_SAFE
#ifdef __MACH__
                new_list->lock = 0;
#else
                pthread_spin_init(&new_list->lock, 0);
#endif
#endif
                items_list[item->hash%newSize] = new_list;
            }
            TAILQ_INSERT_TAIL(&new_list->head, item, next);
        }
        SPIN_UNLOCK(&list->lock);
#ifndef __MACH__
        pthread_spin_destroy(&list->lock);
#endif
        free(list);
    }
    ht_items_list_t **old_items = __sync_fetch_and_add(&table->items, 0);
    while (!__sync_bool_compare_and_swap(&table->items, old_items, items_list))
        old_items = __sync_fetch_and_add(&table->items, 0);

    uint32_t old_size = __sync_fetch_and_add(&table->size, 0);
    while (!__sync_bool_compare_and_swap(&table->size, old_size, newSize))
        old_size = __sync_fetch_and_add(&table->size, 0);

    __sync_sub_and_fetch(&table->growing, 1);
    SPIN_UNLOCK(&table->lock);
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

    ht_items_list_t *list = __sync_fetch_and_add(&table->items[hash%__sync_fetch_and_add(&table->size, 0)], 0);

    if (__sync_fetch_and_add(&table->growing, 0) || !list) {
        SPIN_LOCK(&table->lock);
        list = __sync_fetch_and_add(&table->items[hash%__sync_fetch_and_add(&table->size, 0)], 0);
        if (!list) {
            list = malloc(sizeof(ht_items_list_t));

#ifdef THREAD_SAFE
#ifdef __MACH__
            list->lock = 0;
#else
            pthread_spin_init(&list->lock, 0);
#endif
#endif
            TAILQ_INIT(&list->head);

            while (!__sync_bool_compare_and_swap(&table->items[hash%__sync_fetch_and_add(&table->size, 0)], NULL, list)) {
                ht_items_list_t *l = __sync_fetch_and_add(&table->items[hash%__sync_fetch_and_add(&table->size, 0)], 0);
                if (l) {
#ifdef THREAD_SAFE
#ifndef __MACH__
                    pthread_spin_destroy(&list->lock);
#endif
#endif
                    free(list);
                    list = l;
                    break;
                }
            }
        }
        SPIN_LOCK(&list->lock);
        SPIN_UNLOCK(&table->lock);
    } else {
        SPIN_LOCK(&list->lock);
    }

    // we can anyway unlock the table to allow operations which 
    // don't involve the actual linklist

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
            SPIN_UNLOCK(&list->lock);
            return -1;
        }
        item->hash = hash;
        item->klen = klen;
        item->key = malloc(klen);
        if (!item->key) {
            //fprintf(stderr, "Can't copy key: %s\n", strerror(errno));
            SPIN_UNLOCK(&list->lock);
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
        __sync_add_and_fetch(&table->count, 1);
    }

    SPIN_UNLOCK(&list->lock);

    if (ht_count(table) > __sync_fetch_and_add(&table->size, 0) + HT_GROW_THRESHOLD && 
        (!table->max_size || __sync_fetch_and_add(&table->size, 0) < table->max_size))
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


    ht_items_list_t *list = __sync_fetch_and_add(&table->items[hash%__sync_fetch_and_add(&table->size, 0)], 0);

    if (!list)
        return -1;

    // TODO : maybe this lock is not necessary
    SPIN_LOCK(&list->lock);



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

    SPIN_UNLOCK(&list->lock);

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

    ht_items_list_t *list = __sync_fetch_and_add(&table->items[hash%__sync_fetch_and_add(&table->size, 0)], 0);

    if (!list) {
        return ret;
    }

    SPIN_LOCK(&list->lock);

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
            __sync_sub_and_fetch(&table->count, 1);
            break;
        }
    }

    SPIN_UNLOCK(&list->lock);

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
    ht_items_list_t *list = __sync_fetch_and_add(&table->items[hash%__sync_fetch_and_add(&table->size, 0)], 0);

    if (!list)
        return 0;

    SPIN_LOCK(&list->lock);

    ht_item_t *item = NULL;
    TAILQ_FOREACH(item, &list->head, next) {
        if (/*ht_item->hash == arg->item.hash && */
            ((char *)item->key)[0] == ((char *)key)[0] &&
            item->klen == klen &&
            memcmp(item->key, key, klen) == 0)
        {
            SPIN_UNLOCK(&list->lock);
            return 1;
        }
    }

    SPIN_UNLOCK(&list->lock);
    return 0;
}

void *_ht_get_internal(hashtable_t *table, void *key, size_t klen,
        size_t *dlen, int copy, ht_deep_copy_callback_t copy_cb)
{
    uint32_t hash = 0;

    PERL_HASH(hash, key, klen);
    ht_items_list_t *list = __sync_fetch_and_add(&table->items[hash%__sync_fetch_and_add(&table->size, 0)], 0);

    if (!list)
        return NULL;

    SPIN_LOCK(&list->lock);

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

    SPIN_UNLOCK(&list->lock);

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

    for (i = 0; i < __sync_fetch_and_add(&table->size, 0); i++) {
        ht_items_list_t *list = __sync_fetch_and_add(&table->items[i], 0);

        if (!list) {
            continue;
        }

        SPIN_LOCK(&list->lock);

        ht_item_t *item = NULL;
        TAILQ_FOREACH(item, &list->head, next) {
            hashtable_key_t *key = malloc(sizeof(hashtable_key_t));
            key->data = malloc(item->klen);
            memcpy(key->data, item->key, item->klen);
            key->len = item->klen;
            key->vlen = item->dlen;
            push_value(output, key);
        }

        SPIN_UNLOCK(&list->lock);
    }
    return output;
}

linked_list_t *ht_get_all_values(hashtable_t *table) {
    uint32_t i;

    linked_list_t *output = create_list();

    for (i = 0; i < __sync_fetch_and_add(&table->size, 0); i++) {
        ht_items_list_t *list = __sync_fetch_and_add(&table->items[i], 0);

        if (!list) {
            continue;
        }

        SPIN_LOCK(&list->lock);

        ht_item_t *item = NULL;
        TAILQ_FOREACH(item, &list->head, next) {
            hashtable_value_t *v = malloc(sizeof(hashtable_value_t));
            v->data = malloc(item->dlen);
            v->len = item->dlen;
            push_value(output, v);
        }

        SPIN_UNLOCK(&list->lock);
    }
    return output;
}

void ht_foreach_key(hashtable_t *table, ht_key_iterator_callback_t cb, void *user) {
    uint32_t i;

    for (i = 0; i < __sync_fetch_and_add(&table->size, 0); i++) {
        ht_items_list_t *list = __sync_fetch_and_add(&table->items[i], 0);

        if (!list) {
            continue;
        }
        
        // Note: once we acquired the linklist, we don't need to lock the whole
        // table anymore. We need to lock the list though to prevent it from
        // being destroyed while we are still accessing it (or iterating over)
        SPIN_LOCK(&list->lock);

        ht_item_t *item = NULL;
        TAILQ_FOREACH(item, &list->head, next) {
            int rc = cb(table, item->key, item->klen, user);
            if (rc == 0)
                break;
        }

        SPIN_UNLOCK(&list->lock);
    }
}

void ht_foreach_value(hashtable_t *table, ht_value_iterator_callback_t cb, void *user) {
    uint32_t i;

    for (i = 0; i < __sync_fetch_and_add(&table->size, 0); i++) {
        ht_items_list_t *list = __sync_fetch_and_add(&table->items[i], 0);

        if (!list) {
            continue;
        }

        // Note: once we acquired the linklist, we don't need to lock the whole
        // table anymore. We need to lock the list though to prevent it from
        // being destroyed while we are still accessing it (or iterating over)
        SPIN_LOCK(&list->lock);

        ht_item_t *item = NULL;
        TAILQ_FOREACH(item, &list->head, next) {
            int rc = cb(table, item->data, item->dlen, user);
            if (rc == 0)
                break;
        }

        SPIN_UNLOCK(&list->lock);
    }
}

void ht_foreach_pair(hashtable_t *table, ht_pair_iterator_callback_t cb, void *user) {
    uint32_t i;

    for (i = 0; i < __sync_fetch_and_add(&table->size, 0); i++) {
        ht_items_list_t *list = __sync_fetch_and_add(&table->items[i], 0);

        if (!list) {
            continue;
        }

        // Note: once we acquired the linklist, we don't need to lock the whole
        // table anymore. We need to lock the list though to prevent it from
        // being destroyed while we are still accessing it (or iterating over)
        SPIN_LOCK(&list->lock);

        ht_item_t *item = NULL;
        TAILQ_FOREACH(item, &list->head, next) {
            int rc = cb(table, item->key, item->klen, item->data, item->dlen, user);
            if (rc == 0)
                break;
        }

        SPIN_UNLOCK(&list->lock);
    }
}

uint32_t ht_count(hashtable_t *table) {
    return __sync_add_and_fetch(&table->count, 0);
}

