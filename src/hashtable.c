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

#include "bsd_queue.h"
#include "atomic_defs.h"

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

#define HT_SIZE_MIN 128

#pragma pack(push, 1)
typedef struct __ht_item {
    uint32_t hash;
    char     kbuf[32];
    void    *key;
    size_t   klen;
    void    *data;
    size_t   dlen;
    TAILQ_ENTRY(__ht_item) next;
} ht_item_t;

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
    uint32_t seed;
    ht_items_list_t **items;
#ifdef THREAD_SAFE
    pthread_mutex_t lock;
#endif
    ht_free_item_callback_t free_item_cb;
};
#pragma pack(pop)

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
ht_create(uint32_t initial_size, uint32_t max_size, ht_free_item_callback_t cb)
{
    hashtable_t *table = (hashtable_t *)calloc(1, sizeof(hashtable_t));

    if (table)
        ht_init(table, initial_size, max_size, cb);

    return table;
}

void
ht_init(hashtable_t *table,
        uint32_t initial_size,
        uint32_t max_size,
        ht_free_item_callback_t cb)
{
    table->size = initial_size > HT_SIZE_MIN ? initial_size : HT_SIZE_MIN;
    table->max_size = max_size;
    table->items = (ht_items_list_t **)calloc(table->size, sizeof(ht_items_list_t *));

    if (table->items)
        MUTEX_INIT(table->lock);

    ht_set_free_item_callback(table, cb);
#ifdef BSD
    table->seed = arc4random()%UINT32_MAX;
#else
    table->seed = random()%UINT32_MAX;
#endif
}

void
ht_set_free_item_callback(hashtable_t *table, ht_free_item_callback_t cb)
{
    ATOMIC_SET(table->free_item_cb, cb);
}

static inline ht_items_list_t *
ht_get_list_at_index(hashtable_t *table, uint32_t index)
{
    ht_items_list_t *list = NULL;
    if (ATOMIC_READ(table->growing)) {
        MUTEX_LOCK(table->lock);
        list = ATOMIC_READ(table->items[index]);
        MUTEX_UNLOCK(table->lock);
    } else {
        list = ATOMIC_READ(table->items[index]);
    }
    return list;
}

void
ht_clear(hashtable_t *table)
{
    uint32_t i;
    MUTEX_LOCK(table->lock);
    for (i = 0; i < table->size; i++)
    {
        ht_item_t *item = NULL;
        ht_item_t *tmp;

        ht_items_list_t *list = ht_get_list_at_index(table, i);

        if (!list)
            continue;

        TAILQ_FOREACH_SAFE(item, &list->head, next, tmp) {
            TAILQ_REMOVE(&list->head, item, next);
            if (table->free_item_cb)
                table->free_item_cb(item->data);
            if (item->key != item->kbuf)
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

void
ht_destroy(hashtable_t *table)
{
    ht_clear(table);
    free(table->items);
    MUTEX_DESTROY(table->lock);
    free(table);
}

static inline void
ht_grow_table(hashtable_t *table)
{
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

    //fprintf(stderr, "Growing table from %u to %u\n",
    //        __sync_fetch_and_add(&table->size, 0), new_size);

    ht_items_list_t **items_list = 
        (ht_items_list_t **)calloc(new_size, sizeof(ht_items_list_t *));

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

    ht_items_list_t **old_items = ATOMIC_READ(table->items);
    ATOMIC_SET(table->items, items_list);
    free(old_items);

    ATOMIC_SET(table->size, new_size);

    ATOMIC_SET(table->growing, 0);

    MUTEX_UNLOCK(table->lock);
    //fprintf(stderr, "Done growing table\n");
}

static inline ht_items_list_t *
ht_get_list(hashtable_t *table, uint32_t hash)
{
    uint32_t idx = hash%ATOMIC_READ(table->size);
    ht_items_list_t *list = ht_get_list_at_index(table, idx);
    if (list)
        SPIN_LOCK(list->lock);
    return list;
}


static inline ht_items_list_t *
ht_set_list(hashtable_t *table, uint32_t hash)
{ 
    ht_items_list_t *list = malloc(sizeof(ht_items_list_t));
    SPIN_INIT(list->lock);
    TAILQ_INIT(&list->head);
    SPIN_LOCK(list->lock);
    MUTEX_LOCK(table->lock);
    while (!__sync_bool_compare_and_swap(&table->items[hash%ATOMIC_READ(table->size)], NULL, list))
    {
        ht_items_list_t *other = ATOMIC_READ(table->items[hash%ATOMIC_READ(table->size)]);
        if (other) {
            SPIN_LOCK(other->lock);
            SPIN_UNLOCK(list->lock);
            SPIN_DESTROY(list->lock);
            free(list);
            list = other;
            break;
        }
    }
    MUTEX_UNLOCK(table->lock);
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

    ht_items_list_t *list  = ht_get_list(table, hash);

    if (!list)
        list = ht_set_list(table, hash);

    ht_item_t *item = NULL;
    TAILQ_FOREACH(item, &list->head, next) {
        if (/*ht_item->hash == arg->item.hash && */
            ((char *)item->key)[0] == ((char *)key)[0] &&
            item->klen == klen &&
            memcmp(item->key, key, klen) == 0)
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

        if (klen > sizeof(item->kbuf))
            item->key = malloc(klen);
        else
            item->key = item->kbuf;

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
            item->data = malloc(dlen);
            memcpy(item->data, data, dlen);
        } else {
            item->data = data;
        }
    }

    SPIN_UNLOCK(list->lock);

    uint32_t current_size = ATOMIC_READ(table->size);
    if (ht_count(table) > (current_size + (current_size/3)) && 
        (!table->max_size || current_size < table->max_size) &&
        ATOMIC_CAS(table->growing, 0, 1))
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

int
ht_unset(hashtable_t *table,
         void *key,
         size_t klen,
         void **prev_data,
         size_t *prev_len)
{
    uint32_t hash = ht_hash_one_at_a_time(table, key, klen);


    ht_items_list_t *list  = ht_get_list(table, hash);
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

static inline int
ht_delete_internal (hashtable_t *table,
                    void *key,
                    size_t klen,
                    void **prev_data,
                    size_t *prev_len,
                    void *match,
                    size_t match_size)
{
    int ret = -1;

    uint32_t hash = ht_hash_one_at_a_time(table, key, klen);

    ht_items_list_t *list  = ht_get_list(table, hash);
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

            if (match && (match_size != item->dlen || memcmp(match, item->data, match_size) != 0)) {
                SPIN_UNLOCK(list->lock);
                return ret;
            }

            prev = item->data;
            plen = item->dlen;
            TAILQ_REMOVE(&list->head, item, next);
            if (item->key != item->kbuf)
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
    uint32_t hash = ht_hash_one_at_a_time(table, key, klen);

    ht_items_list_t *list  = ht_get_list(table, hash);
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

static inline void *
ht_get_internal(hashtable_t *table,
                void *key,
                size_t klen,
                size_t *dlen,
                int copy,
                ht_deep_copy_callback_t copy_cb,
                void *user)
{
    uint32_t hash = ht_hash_one_at_a_time(table, key, klen);

    ht_items_list_t *list  = ht_get_list(table, hash);
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
                    data = copy_cb(item->data, item->dlen, user);
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
    uint32_t i;

    linked_list_t *output = list_create();
    list_set_free_value_callback(output, (free_value_callback_t)free_key);

    for (i = 0; i < ATOMIC_READ(table->size); i++) {
        ht_items_list_t *list = ht_get_list_at_index(table, i);

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
            list_push_value(output, key);
        }

        SPIN_UNLOCK(list->lock);
    }
    return output;
}

linked_list_t *
ht_get_all_values(hashtable_t *table)
{
    uint32_t i;

    linked_list_t *output = list_create();

    for (i = 0; i < ATOMIC_READ(table->size); i++) {
        ht_items_list_t *list = ht_get_list_at_index(table, i);

        if (!list) {
            continue;
        }

        SPIN_LOCK(list->lock);

        ht_item_t *item = NULL;
        TAILQ_FOREACH(item, &list->head, next) {
            hashtable_value_t *v = malloc(sizeof(hashtable_value_t));
            v->data = malloc(item->dlen);
            v->len = item->dlen;
            list_push_value(output, v);
        }

        SPIN_UNLOCK(list->lock);
    }
    return output;
}

typedef struct {
    int (*cb)();
    void *user;
} ht_iterator_arg_t;

static int
ht_foreach_key_helper(hashtable_t *table, void *key, size_t klen, void *value, size_t vlen, void *user)
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
ht_foreach_value_helper(hashtable_t *table, void *key, size_t klen, void *value, size_t vlen, void *user)
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
    uint32_t i;
    uint32_t count = 0;
    int rc = 0;

    for (i = 0; i < ATOMIC_READ(table->size) && count < ATOMIC_READ(table->count); i++)
    {
        ht_items_list_t *list  = ht_get_list(table, i);
        if (!list)
            continue;

        ht_item_t *item = NULL;
        TAILQ_FOREACH(item, &list->head, next) {
            count++;
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
                ATOMIC_DECREMENT(table->count);
                if (rc == -2) {
                    SPIN_UNLOCK(list->lock);
                    break;
                }
                count--;
            }
        }
        SPIN_UNLOCK(list->lock);
    }
}

uint32_t
ht_count(hashtable_t *table)
{
    return ATOMIC_READ(table->count);
}

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
