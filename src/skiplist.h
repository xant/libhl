/**
 * @file skiplist.h
 *
 * @brief Skip list implementation for arbitrary data
 */

#ifndef HL_SKIPLIST_H
#define HL_SKIPLIST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "comparators.h"

/**
 * @brief Opaque structure representing the skip list
 */
typedef struct _skiplist_s skiplist_t;

/**
 * @brief Callback that, if provided, will be called to release the value
 *        resources when an item is being overwritten or when removed from
 *        the tree
 * @param v the pointer to free
 */
typedef void (*skiplist_free_value_callback_t)(void *value);

/**
 * @brief Create a new skip list
 * @param num_layers    The number of layers used by the skip list
 * @param probability   The chance an item has to be inserted in an upper layer
 * @param cmp_keys_cb   The comparator callback to use when comparing
 *                      keys (defaults to memcmp())
 * @param free_value_cb The callback used to release values when a node
 *                      is removed or overwritten
 * @return              A valid and initialized skip list
 */
skiplist_t *skiplist_create(int num_layers,
                            int probability,
                            libhl_cmp_callback_t cmp_keys_cb,
                            skiplist_free_value_callback_t free_value_cb);
/**
 * @brief Insert a new value into the skip list
 * @param skl   A valid pointer to an initialized skiplist_t structure
 * @param key   The key of the item in the list where to store the new value
 * @param klen  The size of the key
 * @param value The new value to store
 * @return 0 if a new node has been created successfully;
 *         1 if an existing node has been found and the value has been updated;
 *         -1 otherwise
 */
int skiplist_insert(skiplist_t *skl, void *key, size_t klen, void *value);

/**
 * @brief Remove an item from the skip list
 * @param skl  A valid pointer to an initialized skiplist_t structure
 * @param key   The key of the item in the list where to store the new value
 * @param klen  The size of the key
 * @param value If not NULL the address of the value hold by the removed item
 *              will be stored at the memory pointed by the 'value' argument.
 *              If NULL and a free_value_callback is set, the value hold by
 *              the removed node will be released using the free_value_callback
 * @return 0 if the item has been found and correctly removed; -1 otherwise
 */
int skiplist_remove(skiplist_t *skl, void *key, size_t klen, void **value);

/**
 * @brief Search an item into the skip list
 * @param skl  A valid pointer to an initialized skiplist_t structure
 * @param key   The key of the item in the list where to store the new value
 * @param klen  The size of the key
 * @return The value stored in the found item if any; NULL otherwise
 */
void *skiplist_search(skiplist_t *skl, void *key, size_t klen);

/**
 * @brief Release all the resources allocated for the skip list
 * @param skl  A valid pointer to an initialized skiplist_t structure
 */
void skiplist_destroy(skiplist_t *skl);

/**
 * @brief Return the number of elements in the skip list
 * @param skl  A valid pointer to an initialized skiplist_t structure
 * @return The number of elements in the skip list
 */
size_t skiplist_count(skiplist_t *skl);


#ifdef __cplusplus
}
#endif

#endif

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
