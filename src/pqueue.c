#include <stdlib.h>
#include <string.h>

#include "binheap.h"
#include "pqueue.h"
#include "atomic_defs.h"

typedef struct {
    void *value;
    uint64_t prio;
} pqueue_item_t;

struct _pqueue_s {
    binheap_t *heap;
    size_t max_size;
    pqueue_free_value_callback free_value_cb;
    pqueue_mode_t mode;
    pthread_mutex_t lock;
};

pqueue_t *
pqueue_create(pqueue_mode_t mode, size_t size, pqueue_free_value_callback free_value_cb)
{
    pqueue_t *pq = calloc(1, sizeof(pqueue_t));
    if (!pq)
        return NULL;

    MUTEX_INIT(pq->lock);

    pq->mode = mode;
    pq->max_size = size;
    pq->heap = binheap_create(binheap_keys_callbacks_uint64_t(),
                              (mode == PQUEUE_MODE_HIGHEST) ? BINHEAP_MODE_MAX : BINHEAP_MODE_MIN);
    pq->free_value_cb = free_value_cb;
    return pq;
}

static inline void
pqueue_drop_items(pqueue_t *pq, size_t num_items)
{
    size_t deleted = 0;
    while(binheap_count(pq->heap) && deleted < num_items) {
        void *deleted_item = NULL;
        if (pq->mode == PQUEUE_MODE_HIGHEST)
            binheap_delete_minimum(pq->heap, &deleted_item);
        else
            binheap_delete_maximum(pq->heap, &deleted_item);
        if (deleted_item) {
            if (pq->free_value_cb)
                pq->free_value_cb(((pqueue_item_t *)deleted_item)->value);
            free(deleted_item);
        }
        deleted++;
    }
}

void
pqueue_destroy(pqueue_t *pq)
{
    pqueue_drop_items(pq, binheap_count(pq->heap));
    binheap_destroy(pq->heap);
    MUTEX_DESTROY(pq->lock);
    free(pq);
}

int
pqueue_insert(pqueue_t *pq, uint64_t prio, void *value)
{
    pqueue_item_t *item = malloc(sizeof(pqueue_item_t));
    if (!item)
        return -1;

    item->value = value;
    item->prio = prio;

    MUTEX_LOCK(pq->lock);

    int rc = binheap_insert(pq->heap, (void *)&item->prio, sizeof(item->prio), item);

    if (binheap_count(pq->heap) > pq->max_size)
        pqueue_drop_items(pq, binheap_count(pq->heap) - pq->max_size);

    MUTEX_UNLOCK(pq->lock);

    return rc;
}

int
pqueue_pull_highest(pqueue_t *pq, void **value, uint64_t *prio)
{
    void *item = NULL;

    MUTEX_LOCK(pq->lock);

    int rc = (pq->mode == PQUEUE_MODE_HIGHEST)
           ? binheap_delete_maximum(pq->heap, &item)
           : binheap_delete_minimum(pq->heap, &item);

    MUTEX_UNLOCK(pq->lock);

    if (rc == 0) {

        if (value) {
            *value = ((pqueue_item_t *)item)->value;
        } else if (pq->free_value_cb) {
            pq->free_value_cb(((pqueue_item_t *)item)->value);
        }

        if (prio)
            *prio = ((pqueue_item_t *)item)->prio;

        free(item);
    }
    return rc;
}

int
pqueue_pull_lowest(pqueue_t *pq, void **value, uint64_t *prio)
{
    void *item = NULL;

    MUTEX_LOCK(pq->lock);

    int rc = (pq->mode == PQUEUE_MODE_HIGHEST)
           ? binheap_delete_minimum(pq->heap, &item)
           : binheap_delete_maximum(pq->heap, &item);

    MUTEX_UNLOCK(pq->lock);

    if (rc == 0) {

        if (value) {
            *value = ((pqueue_item_t *)item)->value;
        } else if (pq->free_value_cb) {
            pq->free_value_cb(((pqueue_item_t *)item)->value);
        }

        if (prio)
            *prio = ((pqueue_item_t *)item)->prio;

        free(item);
    }
    return rc;
}

typedef struct {
    pqueue_t *pq;
    pqueue_walk_callback_t cb;
    void *priv;
} pqueue_walk_helper_arg_t;

static int 
pqueue_walk_helper(binheap_t *bh __attribute__ ((unused)), void *key, size_t klen __attribute__ ((unused)), void *value, void *priv)
{
    pqueue_walk_helper_arg_t *arg = (pqueue_walk_helper_arg_t *)priv;
    uint64_t *prio = (uint64_t *)key;
    return arg->cb(arg->pq, *prio, value, arg->priv);
}

int
pqueue_walk(pqueue_t *pq, pqueue_walk_callback_t cb, void *priv)
{
    pqueue_walk_helper_arg_t arg = {
        .pq = pq,
        .cb = cb,
        .priv = priv
    };
    return binheap_walk(pq->heap, pqueue_walk_helper, &arg);
}


typedef struct {
    void *value;
    int found;
} pqueue_remove_helper_arg_t;

static inline int
pqueue_remove_helper(pqueue_t *pq __attribute__ ((unused)), uint64_t prio __attribute__ ((unused)), void *value, void *priv)
{
    pqueue_remove_helper_arg_t *arg = (pqueue_remove_helper_arg_t *)priv;
    if (value == arg->value)
    {
        return -2;
    }
    return 1;
}

int
pqueue_remove(pqueue_t *pq, void *value)
{
    pqueue_remove_helper_arg_t arg = {
        .value = value,
        .found = 0
    };
    pqueue_walk(pq, pqueue_remove_helper, &arg);
    return arg.found ? 0 : -1;
}

size_t
pqueue_count(pqueue_t *pq)
{
    MUTEX_LOCK(pq->lock);
    size_t count = binheap_count(pq->heap);
    MUTEX_UNLOCK(pq->lock);
    return count;
}

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
