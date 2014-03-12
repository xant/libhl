#include <stdlib.h>

#include "binheap.h"
#include "pqueue.h"

typedef struct {
    void *value;
    size_t len;
    int32_t prio;
} pqueue_item_t;

struct __pqueue_s {
    binheap_t *heap;
    uint32_t max_size;
    pqueue_free_value_callback free_value_cb;
    pqueue_mode_t mode;
};

pqueue_t *
pqueue_create(pqueue_mode_t mode, uint32_t size, pqueue_free_value_callback free_value_cb)
{
    pqueue_t *pq = calloc(1, sizeof(pqueue_t));
    pq->mode = mode;
    pq->max_size = size;
    pq->heap = binheap_create(binheap_keys_callbacks_int32_t(),
                              (mode == PQUEUE_MODE_HIGHEST) ? BINHEAP_MODE_MAX : BINHEAP_MODE_MIN);
    pq->free_value_cb = free_value_cb;
    return pq;
}

static void
pqueue_drop_items(pqueue_t *pq, uint32_t num_items)
{
    uint32_t deleted = 0;
    while(binheap_count(pq->heap) && deleted < num_items) {
        pqueue_item_t *deleted_item = NULL;
        if (pq->mode == PQUEUE_MODE_HIGHEST)
            binheap_delete_minimum(pq->heap, (void **)&deleted_item, NULL);
        else
            binheap_delete_maximum(pq->heap, (void **)&deleted_item, NULL);
        if (deleted_item) {
            if (pq->free_value_cb)
                pq->free_value_cb(deleted_item->value);
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
    free(pq);
}

int
pqueue_insert(pqueue_t *pq, int32_t prio, void *value, size_t len)
{
    pqueue_item_t *item = malloc(sizeof(pqueue_item_t));
    item->value = value;
    item->len = len;
    item->prio = prio;
    int rc = binheap_insert(pq->heap, (void *)&item->prio, sizeof(item->prio), item, sizeof(pqueue_item_t));

    if (binheap_count(pq->heap) > pq->max_size)
        pqueue_drop_items(pq, binheap_count(pq->heap) - pq->max_size);

    return rc;
}

int
pqueue_pull_highest(pqueue_t *pq, void **value, size_t *len, int32_t *prio)
{
    pqueue_item_t *item = NULL;

    int rc = (pq->mode == PQUEUE_MODE_HIGHEST)
           ? binheap_delete_maximum(pq->heap, (void **)&item, NULL)
           : binheap_delete_minimum(pq->heap, (void **)&item, NULL);

    if (rc == 0) {

        if (value) {
            *value = item->value;
        } else if (pq->free_value_cb) {
            pq->free_value_cb(item->value);
        }

        if (len)
            *len = item->len;

        if (prio)
            *prio = item->prio;

        free(item);
    }
    return rc;
}

int
pqueue_pull_lowest(pqueue_t *pq, void **value, size_t *len, int32_t *prio)
{
    pqueue_item_t *item = NULL;

    int rc = (pq->mode == PQUEUE_MODE_HIGHEST)
           ? binheap_delete_minimum(pq->heap, (void **)&item, NULL)
           : binheap_delete_maximum(pq->heap, (void **)&item, NULL);

    if (rc == 0) {

        if (value) {
            *value = item->value;
        } else if (pq->free_value_cb) {
            pq->free_value_cb(item->value);
        }

        if (len)
            *len = item->len;

        if (prio)
            *prio = item->prio;

        free(item);
    }
    return rc;
}

uint32_t
pqueue_count(pqueue_t *pq)
{
    return binheap_count(pq->heap);
}
