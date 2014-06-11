#include <stdlib.h>
#include <string.h>
#include "skiplist.h"
#include "bsd_queue.h"


typedef struct __skl_item_wrapper_s skl_item_wrapper_t;

typedef struct {
    void *key;
    size_t klen;
    void *value;
    int *layer_check;
    skl_item_wrapper_t *wrappers;
} skl_item_t;

struct __skl_item_wrapper_s {
    skl_item_t *data;
    TAILQ_ENTRY(__skl_item_wrapper_s) next;
};

struct __skiplist_s {
    int num_layers;
    int probability;
    libhl_cmp_callback_t cmp_keys_cb;
    skiplist_free_value_callback_t free_value_cb;
    TAILQ_HEAD(layer_list, __skl_item_wrapper_s) *layers;
};


skiplist_t *
skiplist_create(int num_layers,
                int probability,
                libhl_cmp_callback_t cmp_keys_cb,
                skiplist_free_value_callback_t free_value_cb)
{
    skiplist_t *skl = calloc(1, sizeof(skiplist_t));
    skl->layers = calloc(num_layers, sizeof(struct layer_list));
    int i;
    for (i = 0; i < num_layers; i++)
        TAILQ_INIT(&skl->layers[i]);
    skl->num_layers = num_layers;
    skl->probability = probability;
    skl->cmp_keys_cb = cmp_keys_cb;
    skl->free_value_cb = free_value_cb;
    return skl;
}

static inline skl_item_t *
skiplist_search_internal(skiplist_t *skl, void *key, size_t klen)
{
    int i = skl->num_layers-1;
    skl_item_wrapper_t *prev_item = NULL;
    while (i >= 0) {
        skl_item_wrapper_t *item;
        if (prev_item)
            item = TAILQ_NEXT(&prev_item->data->wrappers[i], next);
        else
            item = TAILQ_FIRST(&skl->layers[i]);

        while (item) {
            if (skl->cmp_keys_cb(item->data->key, item->data->klen, key, klen) > 0)
                break;
            prev_item = item;
            item = TAILQ_NEXT(item, next);
        }
    }
    return prev_item->data;
}

void *
skiplist_search(skiplist_t *skl, void *key, size_t klen)
{
    skl_item_t *prev_item = skiplist_search_internal(skl, key, klen);
    if (prev_item && skl->cmp_keys_cb(prev_item->key, prev_item->klen, key, klen) == 0)
        return prev_item->value;
    return NULL;
}

int
skiplist_insert(skiplist_t *skl, void *key, size_t klen, void *value)
{
    skl_item_t *prev_item = skiplist_search_internal(skl, key, klen);
    if (prev_item && skl->cmp_keys_cb(prev_item->key, prev_item->klen, key, klen) == 0) {
        // we have found an item with the same key, let's just update the value
        if (skl->free_value_cb)
            skl->free_value_cb(prev_item->value);
        prev_item->value = value;
        return 1;
    }

    // create a new item
    skl_item_t *new_item = calloc(1, sizeof(skl_item_t));
    new_item->key = calloc(1, klen);
    memcpy(new_item->key, key, klen);
    new_item->value = value;
    new_item->layer_check = calloc(skl->num_layers, sizeof(char));
    new_item->wrappers = calloc(skl->num_layers, sizeof(skl_item_wrapper_t));
    // initialize the list wrappers
    int i;
    for (i = 0; i < skl->num_layers; i++)
        new_item->wrappers[i].data = new_item;

    // always insert the new item to the tail list
    i = 0;
    TAILQ_INSERT_AFTER(&skl->layers[i], &prev_item->wrappers[i], &new_item->wrappers[i], next);
    while (++i < skl->num_layers) {
        if (random()%100 > skl->probability)
            break;
        // then insert it to the upper layers as well if we got the chance
        TAILQ_INSERT_AFTER(&skl->layers[i], &prev_item->wrappers[i], &new_item->wrappers[i], next);
        new_item->layer_check[i] = 1;
    }
    return 0;
}

static inline void
skiplist_remove_internal(skiplist_t *skl, skl_item_t *item, void **value)
{
    if (value)
        *value = item->value;
    else if (skl->free_value_cb)
        skl->free_value_cb(item->value);

    int i;
    for (i = 0; i < skl->num_layers; i++) {
        if (item->layer_check[i]) 
            TAILQ_REMOVE(&skl->layers[i], &item->wrappers[i], next);
    }
    free(item->wrappers);
    free(item->layer_check);
    free(item->key);
    free(item);
}

int
skiplist_remove(skiplist_t *skl, void *key, size_t klen, void **value)
{
    skl_item_t *prev_item = skiplist_search_internal(skl, key, klen);
    if (prev_item && skl->cmp_keys_cb(prev_item->key, prev_item->klen, key, klen) == 0) {
        skiplist_remove_internal(skl, prev_item, value);
        return 0;
    }
    return -1;
}

void
skiplist_destroy(skiplist_t *skl)
{
    skl_item_wrapper_t *item = TAILQ_FIRST(&skl->layers[0]);
    while (item) {
        skl_item_wrapper_t *next = TAILQ_NEXT(item, next);
        skiplist_remove_internal(skl, item->data, NULL);
        item = next;
    }
    free(skl->layers);
    free(skl);
}

