#ifndef HL_AVLT_H
#define HL_AVLT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <sys/types.h>
#include "comparators.h"

typedef struct _avlt_s avlt_t;

/**
 * @brief Callback that, if provided, will be called to release the value
 *        resources when an item is being overwritten or when removed from
 *        the tree
 * @param v the pointer to free
 */
typedef void (*avlt_free_value_callback_t)(void *v);

/**
 * @brief Create a new AVL tree
 * @param cmp_keys_cb   The comparator callback to use when comparing
 *                      keys (defaults to memcmp())
 * @param free_value_cb The callback used to release values when a node
 *                      is removed or overwritten
 * @return              A valid and initialized AVL tree (empty)
 */
avlt_t *avlt_create(libhl_cmp_callback_t cmp_keys_cb,
                    avlt_free_value_callback_t free_value_cb);


/**
 * @brief Release all the resources used by an AVL tree
 * @param tree A valid pointer to an initialized avlt_t structure
 */
void avlt_destroy(avlt_t *tree);

/**
 * @brief Add a new value into the tree
 * @param tree  A valid pointer to an initialized avlt_t structure
 * @param key   The key of the node where to store the new value
 * @param klen  The size of the key
 * @param value The new value to store
 * @return 0 if a new node has been created successfully;
 *         1 if an existing node has been found and the value has been updated;
 *         -1 otherwise
 */
int avlt_add(avlt_t *tree, void *key, size_t klen, void *value);

/**
 * @brief Remove a node from the tree
 * @param tree  A valid pointer to an initialized avlt_t structure
 * @param key   The key of the node to remove
 * @param klen  The size of the key
 * @param value If not NULL the address of the value hold by the removed node
 *              will be stored at the memory pointed by the 'value' argument.
 *              If NULL and a free_value_callback is set, the value hold by
 *              the removed node will be released using the free_value_callback
 * @return 0 on success; -1 otherwise
 */
int avlt_remove(avlt_t *tree, void *key, size_t klen, void **value);

/**
 * @brief Callback called for each node when walking the tree
 * @param tree  A valid pointer to an initialized avlt_t structure
 * @param k     The key of the node where to store the new value
 * @param ksize The size of the key
 * @param value The new value to store
 * @param priv  The private pointer passed to either avlt_walk() or avlt_walk_sorted()
 * @return 1 If the walker can go ahead visiting the next node,
 *         0 if the walker should stop and return
 *        -1 if the current node should be removed and the walker can go ahead
 *        -2 if the current node should be removed and the walker should stop
 */
typedef int (*avlt_walk_callback_t)(avlt_t *tree,
                                    void *key,
                                    size_t klen,
                                    void *value,
                                    void *priv);

/**
 * @brief Walk the entire tree and call the callback for each visited node
 * @param tree  A valid pointer to an initialized avlt_t structure
 * @param cb   The callback to call for each visited node
 * @param priv A pointer to private data provided passed as argument to the
 *             callback when invoked.
 * @return The number of visited nodes
 */
int avlt_walk(avlt_t *tree, avlt_walk_callback_t cb, void *priv);

/**
 * @brief Walk the entire tree visiting nodes in ascending order and call the callback
 *        for each visited node
 * @param tree  A valid pointer to an initialized avlt_t structure
 * @param cb   The callback to call for each visited node
 * @param priv A pointer to private data provided passed as argument to the
 *             callback when invoked.
 * @return The number of visited nodes
 */
int avlt_walk_sorted(avlt_t *tree, avlt_walk_callback_t cb, void *priv);

#ifdef DEBUG_AVLT
/**
 * @brief Print out the whole tree on stdout (for debugging purposes only)
 */
void avlt_print(avlt_t *tree);
#endif

#ifdef __cplusplus
}
#endif

#endif

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
