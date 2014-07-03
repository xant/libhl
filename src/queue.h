/** 
 * @file queue.h
 * @author Andrea Guzzo
 * @date 22/10/2013
 * @brief lock-free queue implementation
 */
#ifndef __QUEUE_H__
#define __QUEUE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#ifdef WIN32
#ifdef THREAD_SAFE
#include <w32_pthread.h>
#endif
#endif
#include <string.h> // for memset

/**
 * @brief Callback that, if provided, will be called to release the value resources
 *        when an item is being removed from the queue
 */
typedef void (*queue_free_value_callback_t)(void *v);

/**
 * @brief Opaque structure representing the actual linked queue descriptor
 */
typedef struct __queue queue_t;


/* Queue creation and destruction routines */

/**
 * @brief Create a new queue
 * @param expected_size : If non zero specifies how big the queue is expected to
 *        grow. While this doesn't impose any limit, this size hints the queue
 *        on how many spare wrapper structures to keep ready for use, with the
 *        intent of mitigate frequent allocs/frees when the queue is being used
 *        at an high rate
 * @note  If expected size is 0 no spare items will be kept for reuse
 * @return a newly allocated and initialized queue
 */
queue_t *queue_create();

/**
 * @brief Set the size of the internal buffer pool
 * @param q : A valid pointer to a queue_t structure
 * @param size : The size of the buffer pool
 * @return The old size of the buffer pool (if any, 0 otherwise)
 * @note If a buffer pool is used (size > 0) queue_entry_t structures,
 * used internally by the queue implementation to encapsulate the
 * actual values, will be reused when possible by maintaining an internal
 * pool of at most 'size' spare entries
 */
uint32_t queue_set_bpool_size(queue_t *q, uint32_t size);

/**
 * @brief Initialize a pre-allocated queue
 *
 * This function can be used to initialize a statically defined queue
 */
void queue_init(queue_t *q);

/**
 * @brief Release all resources related to the queue
 * @param q : A valid pointer to a queue_t structure
 */
void queue_destroy(queue_t *q);

/**
 * @brief remove all items from the queue
 * @param q : A valid pointer to a queue_t structure
 */
void queue_clear(queue_t *q);

/**
 * @brief Return the total count of items in the queue
 * @param q : A valid pointer to a queue_t structure
 * @return the actual number of items stored in the queue
 */
uint32_t queue_count(queue_t *q);

/**
 * @brief Set the callback which must be called to release values stored in the queue
 * @param q : A valid pointer to a queue_t structure
 * @param free_value_cb : an free_value_callback_t function
 */
void queue_set_free_value_callback(queue_t *q, queue_free_value_callback_t free_value_cb);

/* queue access routines */

/**
 * @brief Remove last value from the queue
 * @param q : A valid pointer to a queue_t structure
 * @return The value previously stored in the tail of the queue
 */
void *queue_pop_right(queue_t *q);

/**
 * @brief Append a new value to the queue (tail)
 * @param q : A valid pointer to a queue_t structure
 * @param val : The value to store in the tail of the queue
 * @return : 0 if success, -1 otherwise
 */
int queue_push_right(queue_t *q, void *val);

/**
 * @brief Insert a new value at the beginning of the least (head)
 * @param q : A valid pointer to a queue_t structure
 * @param val : The value to store in the head of the queue
 * @return : 0 if success, -1 otherwise
 */
int queue_push_left(queue_t *q, void *val);

/**
 * @brief Remove the first value from the queue
 * @param q : A valid pointer to a queue_t structure
 * @return The previous value stored in the tail of the queue
 */
void *queue_pop_left(queue_t *q);

#ifdef __cplusplus
}
#endif

#endif
