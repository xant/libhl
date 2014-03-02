#ifndef __PQUEUE_H__
#define __PQUEUE_H__

/**
 * @file pqueue.h
 *
 * @brief Priority Queue
 *
 * Priority Queue implementation holding arbitrary data
 *
 */

#include <sys/types.h>
#include <stdint.h>

/**
 * @brief Opaque structure representing the priority queue
 */
typedef struct __pqueue_s pqueue_t;

/**
 * @brief Callback that, if provided, will be called to release the value
 *        resources when an value is being removed from the queue
 */
typedef void (*pqueue_free_value_callback)(void *value);

/**
 * @brief Create a new priority queue
 * @param size   The maximum size of the queue (maximum number of elements
 *               that can be stored in the queue)
 * @param free_value_cb   the callback to use when an value has been removed
 *                        from the queue and the value can be released
 * @return a newly allocated and initialized priority queue
 */
pqueue_t *pqueue_create(uint32_t size, pqueue_free_value_callback free_value_cb);

/**
 * @brief Insert a new value into the queue with a given priority
 * @param pq A valid pointer to an initialized pqueue_t structure
 * @param prio The priority to assign to the new value
 * @param value The new value to add to the queue
 * @return 0 if the value has been successfully added to the queue;\n
 *         -1 in case of errors
 */
int pqueue_insert(pqueue_t *pq, int32_t prio, void *value);

/**
 * @brief Pull the the highest priority value out of the queue
 * @param pq A valid pointer to an initialized pqueue_t structure
 * @param value If not null it will be set to point to the value
 *              with the highest priority just pulled out of the
 *              queue
 * @param prio If not null the priority of the pulled item will
 *             be stored in the memory pointed by prio
 * @return 0 if a value has been found and successfully pulled out
 *         of the queue;\n-1 in case of errors
 */
int pqueue_pull_highest(pqueue_t *pq, void **value, int32_t *prio);

/**
 * @brief Pull the the lowest priority value out of the queue
 * @param pq A valid pointer to an initialized pqueue_t structure
 * @param value If not null it will be set to point to the value
 *              with the lowest priority just pulled out of the
 *              queue
 * @param prio If not null the priority of the pulled item will
 *             be stored in the memory pointed by prio
 * @return 0 if a value has been found and successfully pulled out
 *         of the queue;\n-1 in case of errors
 */
int pqueue_pull_lowest(pqueue_t *pq, void **value, int32_t *prio);

/**
 * @brief Return the number of values stored in the priority queue
 * @param pq A valid pointer to an initialized pqueue_t structure
 * @return The number of values actually in the queue
 */
uint32_t pqueue_count(pqueue_t *pq);

/**
 * @brief Release all the resources used by a priority queue
 * @param pq A valid pointer to an initialized pqueue_t structure
 */
void pqueue_destroy(pqueue_t *pq);

#endif
