/**
 * @file   rbuf.h
 * @author Andrea Guzzo
 * @date   15/10/2013
 * @brief  Fast lock-free (thread-safe) ringbuffer implementation
 */
#ifndef __RBUF_H__
#define __RBUF_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Opaque structure representing the actual ringbuffer descriptor
 */
typedef struct __rbuf rbuf_t;

/**
 * @brief Callback that, if provided, will be called to release the value resources
 *        when an item is being overwritten or when releasing a not-depleted ringbuffer 
 */
typedef void (*rbuf_free_value_callback_t)(void *v);

/**
 * @brief Set the callback which must be called to release values stored in the ringbuffer
 * @param table : A valid pointer to a rbuf_t structure
 * @param cb : an rb_free_value_callback_t function
 */
void rb_set_free_value_callback(rbuf_t *rb, rbuf_free_value_callback_t cb);

/**
 * @brief The ringbuffer mode
 *
 * RBUF_MODE_BLOCKING
 *      writers will stop if the ringbuffer is full
 * RBUF_MODE_OVERWRITE
 *      writers will overwrite values when the ringbuffer is full
 */
typedef enum {
    RBUF_MODE_BLOCKING = 0,
    RBUF_MODE_OVERWRITE
} rbuf_mode_t;

/**
 * @brief Create a new ringbuffer descriptor
 * @param size : the size of the ringbuffer
 * @param mode : the mode of the ringbuffer (RBUF_MODE_BLOCKING or RBUF_MODE_OVERWRITE)
 *             default mode is BLOCKING
 * @return a newly allocated and initialized ringbuffer
 *
 */
rbuf_t *rb_create(uint32_t size, rbuf_mode_t mode);


/**
 * @brief Change the mode of an existing ringbuffer
 * @param rb : A valid pointer to a rbuf_t structure
 * @param mode : the mode of the ringbuffer (RBUF_MODE_BLOCKING or RBUF_MODE_OVERWRITE)
 *
 */
void rb_set_mode(rbuf_t *rb, rbuf_mode_t mode);

/**
 * @brief Get the current mode of an existing ringbuffer
 * @param rb : A valid pointer to a rbuf_t structure
 * @return the mode of the ringbuffer (RBUF_MODE_BLOCKING or RBUF_MODE_OVERWRITE)
 */
rbuf_mode_t rb_mode(rbuf_t *rb);

/**
 * @brief Push a new value into the ringbuffer
 * @param rb : A valid pointer to a rbuf_t structure
 * @return 0 on success, -1 on failure, -2 if the buffer is full
 *         and the mode is RBUF_MODE_BLOCKING
 */
int rb_write(rbuf_t *rb, void *value);
/**
 * @brief Read the next value in the ringbuffer
 * @param rb : A valid pointer to a rbuf_t structure
 * @return The next value in the ringbuffer
 */
void *rb_read(rbuf_t *rb);
/**
 * @brief Release all resources associated to the ringbuffer
 * @param rb : A valid pointer to a rbuf_t structure
 *
 * If a free_value_callback has been set, and the ringbuffer still
 * contains data, that will be called for each value still present
 * in the ringbuffer
 */
void rb_destroy(rbuf_t *rb);

/**
 * @brief Returns the count of 'write operations' executed so far
 * @param rb : A valid pointer to a rbuf_t structure
 * @return The actual number of write operations
 */
uint32_t rb_write_count(rbuf_t *rb);

/**
 * @brief Returns the count of 'read operations' executed so far
 * @param rb : A valid pointer to a rbuf_t structure
 * @return The actual number of read operations
 */
uint32_t rb_read_count(rbuf_t *rb);

/**
 * @brief Return a string descriptions of the ringbuffer internals
 * @param rb : A valid pointer to a rbuf_t structure
 * @return : a string containing the description of the ringbuffer structure
 *
 * This function is intended for debugging purposes.
 * Note that for being thread-safe the returned buffer is malloc'd
 * and the caller MUST release it when done
 */
char *rb_stats(rbuf_t *rb);

#ifdef __cplusplus
}
#endif

#endif
