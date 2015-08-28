/**
 * @file   rqueue.h
 * @author Andrea Guzzo
 * @date   15/10/2013
 * @brief  Fast lock-free (thread-safe) ringbuffer implementation
 */
#ifndef HL_RQUEUE_H
#define HL_RQUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>

/**
 * @brief Opaque structure representing the actual ringbuffer descriptor
 */
typedef struct _rqueue_s rqueue_t;

/**
 * @brief Callback that, if provided, will be called to release the value resources
 *        when an item is being overwritten or when releasing a not-depleted ringbuffer 
 */
typedef void (*rqueue_free_value_callback_t)(void *v);

/**
 * @brief Set the callback which must be called to release values stored in the ringbuffer
 * @param rb : A valid pointer to a rqueue_t structure
 * @param cb : an rqueue_free_value_callback_t function
 */
void rqueue_set_free_value_callback(rqueue_t *rb, rqueue_free_value_callback_t cb);

/**
 * @brief The ringbuffer mode
 *
 * RQUEUE_MODE_BLOCKING
 *      writers will stop if the ringbuffer is full
 * RQUEUE_MODE_OVERWRITE
 *      writers will overwrite values when the ringbuffer is full
 */
typedef enum {
    RQUEUE_MODE_BLOCKING = 0,
    RQUEUE_MODE_OVERWRITE
} rqueue_mode_t;

/**
 * @brief Create a new ringbuffer descriptor
 * @param size : the size of the ringbuffer
 *               (the maximum number of pointers that can fit in the ringbuffer)
 * @param mode : the mode of the ringbuffer
 *               (RQUEUE_MODE_BLOCKING or RQUEUE_MODE_OVERWRITE)
 *               default mode is BLOCKING
 * @return a newly allocated and initialized ringbuffer
 *
 */
rqueue_t *rqueue_create(size_t size, rqueue_mode_t mode);


/**
 * @brief Change the mode of an existing ringbuffer
 * @param rb : A valid pointer to a rqueue_t structure
 * @param mode : the mode of the ringbuffer (RQUEUE_MODE_BLOCKING or RQUEUE_MODE_OVERWRITE)
 *
 */
void rqueue_set_mode(rqueue_t *rb, rqueue_mode_t mode);

/**
 * @brief Get the current mode of an existing ringbuffer
 * @param rb : A valid pointer to a rqueue_t structure
 * @return the mode of the ringbuffer (RQUEUE_MODE_BLOCKING or RQUEUE_MODE_OVERWRITE)
 */
rqueue_mode_t rqueue_mode(rqueue_t *rb);

/**
 * @brief Push a new value into the ringbuffer
 * @param rb : A valid pointer to a rqueue_t structure
 * @param value : The pointer to store in the ringbuffer
 * @return 0 on success, -1 on failure, -2 if the buffer is full
 *         and the mode is RQUEUE_MODE_BLOCKING
 */
int rqueue_write(rqueue_t *rb, void *value);

/**
 * @brief Read the next value in the ringbuffer
 * @param rb : A valid pointer to a rqueue_t structure
 * @return The next value in the ringbuffer
 */
void *rqueue_read(rqueue_t *rb);

/**
 * @brief Release all resources associated to the ringbuffer
 * @param rb : A valid pointer to a rqueue_t structure
 *
 * If a free_value_callback has been set, and the ringbuffer still
 * contains data, that will be called for each value still present
 * in the ringbuffer
 */
void rqueue_destroy(rqueue_t *rb);

/**
 * @brief Returns the count of 'write operations' executed so far
 * @param rb : A valid pointer to a rqueue_t structure
 * @return The actual number of write operations
 */
uint64_t rqueue_write_count(rqueue_t *rb);

/**
 * @brief Returns the count of 'read operations' executed so far
 * @param rb : A valid pointer to a rqueue_t structure
 * @return The actual number of read operations
 */
uint64_t rqueue_read_count(rqueue_t *rb);

/**
 * @brief Return a string descriptions of the ringbuffer internals
 * @param rb : A valid pointer to a rqueue_t structure
 * @return : a string containing the description of the ringbuffer structure
 *
 * This function is intended for debugging purposes.
 * Note that for being thread-safe the returned buffer is malloc'd
 * and the caller MUST release it when done
 */
char *rqueue_stats(rqueue_t *rb);

size_t rqueue_size(rqueue_t *rb);

int rqueue_isempty(rqueue_t *tb);

#ifdef __cplusplus
}
#endif

#endif

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
