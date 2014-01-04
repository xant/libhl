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
 * @return a newly allocated and initialized queue
 */
queue_t *queue_create();

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
 * @param cb : an free_value_callback_t function
 */
void queue_set_free_value_callback(queue_t *q, queue_free_value_callback_t free_value_cb);

/* queue access routines */

/**
 * @brief Remove last value from the queue
 * @param q : A valid pointer to a queue_t structure
 * @return The value previous tail of the queue
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

/**
 * @brief Remove the value at the specified position
 * @param q : A valid pointer to a queue_t structure
 * @param pos : The position of the value to extract
 * @return The value stored at pos or NULL if nothing is found
 */
void *queue_pop_position(queue_t *q, uint32_t pos);

/**
 * @brief Push a new value at a specific position
 * @param q : A valid pointer to a queue_t structure
 * @param pos : The position where to insert the new item
 * @param val : The value to store in the tail of the queue
 * @return : 0 if success, -1 otherwise
 */
int queue_push_position(queue_t *q, uint32_t pos, void *value);

#ifdef __cplusplus
}
#endif

#endif
