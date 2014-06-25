#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "binheap.h"
#include "pqueue.h"

typedef struct {
    void *value;
    uint64_t prio;
} pqueue_item_t;

struct __pqueue_s {
    binheap_t *heap;
    uint32_t max_size;
    pqueue_free_value_callback free_value_cb;
    pqueue_mode_t mode;
    pthread_mutex_t lock;
};

pqueue_t *
pqueue_create(pqueue_mode_t mode, uint32_t size, pqueue_free_value_callback free_value_cb)
{
    pqueue_t *pq = calloc(1, sizeof(pqueue_t));
    pq->mode = mode;
    pq->max_size = size;
    pq->heap = binheap_create(binheap_keys_callbacks_uint64_t(),
                              (mode == PQUEUE_MODE_HIGHEST) ? BINHEAP_MODE_MAX : BINHEAP_MODE_MIN);
    pq->free_value_cb = free_value_cb;
    pthread_mutex_init(&pq->lock, NULL);
    return pq;
}

static inline void
pqueue_drop_items(pqueue_t *pq, uint32_t num_items)
{
    uint32_t deleted = 0;
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
    pthread_mutex_destroy(&pq->lock);
    free(pq);
}

int
pqueue_insert(pqueue_t *pq, uint64_t prio, void *value)
{
    pqueue_item_t *item = malloc(sizeof(pqueue_item_t));
    item->value = value;
    item->prio = prio;

    pthread_mutex_lock(&pq->lock);

    int rc = binheap_insert(pq->heap, (void *)&item->prio, sizeof(item->prio), item);

    if (binheap_count(pq->heap) > pq->max_size)
        pqueue_drop_items(pq, binheap_count(pq->heap) - pq->max_size);

    pthread_mutex_unlock(&pq->lock);

    return rc;
}

int
pqueue_pull_highest(pqueue_t *pq, void **value, uint64_t *prio)
{
    void *item = NULL;

    pthread_mutex_lock(&pq->lock);

    int rc = (pq->mode == PQUEUE_MODE_HIGHEST)
           ? binheap_delete_maximum(pq->heap, &item)
           : binheap_delete_minimum(pq->heap, &item);

    pthread_mutex_unlock(&pq->lock);

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

    pthread_mutex_lock(&pq->lock);

    int rc = (pq->mode == PQUEUE_MODE_HIGHEST)
           ? binheap_delete_minimum(pq->heap, &item)
           : binheap_delete_maximum(pq->heap, &item);

    pthread_mutex_unlock(&pq->lock);

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
pqueue_walk_helper(binheap_t *bh, void *key, size_t klen, void *value, void *priv)
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
pqueue_remove_helper(pqueue_t *pq, uint64_t prio, void *value, void *priv)
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

uint32_t
pqueue_count(pqueue_t *pq)
{
    pthread_mutex_lock(&pq->lock);
    uint32_t count = binheap_count(pq->heap);
    pthread_mutex_unlock(&pq->lock);
    return count;
}
