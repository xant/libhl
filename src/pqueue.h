#ifndef HL_PQUEUE_H
#define HL_PQUEUE_H

/**
 * @file pqueue.h
 *
 * @brief  Priority Queue implementation holding arbitrary data
 * @note   In case of failures reported from the pthread interface
 *         abort() will be called. Callers can catch SIGABRT if more
 *         actions need to be taken.
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <stdint.h>

typedef enum {
    PQUEUE_MODE_HIGHEST,
    PQUEUE_MODE_LOWEST
} pqueue_mode_t;

/**
 * @brief Opaque structure representing the priority queue
 */
typedef struct _pqueue_s pqueue_t;

/**
 * @brief Callback that, if provided, will be called to release the value
 *        resources when an value is being removed from the queue
 */
typedef void (*pqueue_free_value_callback)(void *value);

/**
 * @brief Create a new priority queue
 * @param mode   The operational mode of the priority queue \n
 *                - PQUEUE_MODE_HIGHEST : highest key have highest priority
 *                - PQUEUE_MODE_LOWEST  : lowest key have highest priority
 * @param size   The maximum size of the queue (maximum number of elements
 *               that can be stored in the queue)
 * @param free_value_cb   the callback to use when an value has been removed
 *                        from the queue and the value can be released
 * @return a newly allocated and initialized priority queue
 */
pqueue_t *pqueue_create(pqueue_mode_t mode, size_t size, pqueue_free_value_callback free_value_cb);

/**
 * @brief Insert a new value into the queue with a given priority
 * @param pq A valid pointer to an initialized pqueue_t structure
 * @param prio The priority to assign to the new value
 * @param value The new value to add to the queue
 * @return 0 if the value has been successfully added to the queue;\n
 *         -1 in case of errors
 * @note If the number of items after the insertion will be bigger than the
 *       'size' specified at pqueue_create(), the lowest priority items
 *       will be dropped until the size is again less or equal to the
 *       configured maximum size
 */
int pqueue_insert(pqueue_t *pq, uint64_t prio, void *value);

/**
 * @brief Pull the the highest priority value out of the queue
 * @param pq A valid pointer to an initialized pqueue_t structure
 * @param value If not NULL it will be set to point to the value
 *              with the highest priority just pulled out of the
 *              queue
 * @param prio If not NULL the priority of the pulled item will
 *             be stored in the memory pointed by prio
 * @return 0 if a value has been found and successfully pulled out
 *         of the queue;\n-1 in case of errors
 */
int pqueue_pull_highest(pqueue_t *pq, void **value, uint64_t *prio);

/**
 * @brief Pull the the lowest priority value out of the queue
 * @param pq A valid pointer to an initialized pqueue_t structure
 * @param value If not NULL it will be set to point to the value
 *              with the lowest priority just pulled out of the
 *              queue
 * @param prio If not NULL the priority of the pulled item will
 *             be stored in the memory pointed by prio
 * @return 0 if a value has been found and successfully pulled out
 *         of the queue;\n-1 in case of errors
 */
int pqueue_pull_lowest(pqueue_t *pq, void **value, uint64_t *prio);

/**
 * @brief Callback called for each node when walking the priority queue
 * @param pq A valid pointer to an initialized pqueue_t structure
 * @param prio The priority of the current node
 * @param value The value stored in the current node
 * @param priv The private pointer passed to pqueue_walk()
 * @return 1 If the walker can go ahead visiting the next node,
 *         0 if the walker should stop and return
 *        -1 if the current node should be removed and the walker can go ahead
 *        -2 if the current node should be removed and the walker should stop
 */
typedef int (*pqueue_walk_callback_t)(pqueue_t *pq, uint64_t prio, void *value, void *priv);

/**
 * @brief Walk the entire priority queue and call the provided
 *        callback for each visited node
 * @note The callback can both stop the walker and/or remove the currently
 *       visited node using its return value (check pqueue_walk_callback_t)
 * @param pq A valid pointer to an initialized pqueue_t structure
 * @param cb The callback to call for each visited node
 * @param priv A private pointer which will be passed to the callback at each
 *             call
 * @return The number of visited nodes
 *
 */
int pqueue_walk(pqueue_t *pq, pqueue_walk_callback_t cb, void *priv);

/**
 * @brief Remove the first node matching the given value
 * @note If more than one node have the same value, only the first one will be
 *       removed from the priority queue
 * @param pq A valid pointer to an initialized pqueue_t structure
 * @param value The value to mach
 * @return 0 if a matching node has been found and successfully removed;\n
 *        -1 If no matching node was found or an error occurred
 */
int pqueue_remove(pqueue_t *pq, void *value);

/**
 * @brief Return the number of values stored in the priority queue
 * @param pq A valid pointer to an initialized pqueue_t structure
 * @return The number of values actually in the queue
 */
size_t pqueue_count(pqueue_t *pq);

/**
 * @brief Release all the resources used by a priority queue
 * @param pq A valid pointer to an initialized pqueue_t structure
 */
void pqueue_destroy(pqueue_t *pq);

#ifdef __cplusplus
}
#endif

#endif

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
