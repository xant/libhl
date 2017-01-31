/**
 * @file   hashtable.h
 * @author Andrea Guzzo
 * @date   22/09/2013
 * @brief  Fast thread-safe hashtable implementation
 * @note   In case of failures reported from the pthread interface
 *         abort() will be called. Callers can catch SIGABRT if more
 *         actions need to be taken.
 */
#ifndef HL_HASHTABLE_H
#define HL_HASHTABLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "linklist.h"

/**
 * @brief Opaque structure representing the actual hash table descriptor
 */
typedef struct _hashtable_s hashtable_t;

/**
 * @brief Callback that, if provided, will be called to release the value resources
 *        when an item is being removed from the table
 */
typedef void (*ht_free_item_callback_t)(void *);

#define HT_SIZE_MIN 128

/**
 * @brief Create a new table descriptor
 * @param initial_size : initial size of the table; if 0 HT_SIZE_MIN will be used as initial size
 * @param max_size     : maximum size the table can be grown up to
 * @param free_item_cb : the callback to use when an item needs to be released
 * @return a newly allocated and initialized table
 *
 * The table will be expanded if necessary
 */
hashtable_t *ht_create(size_t initial_size, size_t max_size, ht_free_item_callback_t free_item_cb);

/**
 * @brief Initialize a pre-allocated table descriptor
 *
 * This function can be used to initialize a statically defined table
 * @return 0 on success; -1 otherwise
 */
int ht_init(hashtable_t *table, size_t initial_size, size_t max_size, ht_free_item_callback_t free_item_cb);

/**
 * @brief Set the callback which must be called to release values stored in the table
 * @param table : A valid pointer to an hashtable_t structure
 * @param cb : an ht_free_item_callback_t function
 */
void ht_set_free_item_callback(hashtable_t *table, ht_free_item_callback_t cb);

/**
 * @brief Clear the table by removing all the stored items
 * @param table : A valid pointer to an hashtable_t structure
 *
 * If a free_item_callback has been set, that will be called for each item removed from the table
 */
void ht_clear(hashtable_t *table);

/**
 * @brief Destroy the table by releasing all its resources
 * @param table : A valid pointer to an hashtable_t structure
 *
 * If a free_item_callback has been set, that will be called for each item removed from the table
 */
void ht_destroy(hashtable_t *table);

/**
 * @brief Get the value stored at a specific key
 * @param table : A valid pointer to an hashtable_t structure
 * @param key   : The key to use
 * @param klen  : The length of the key
 * @param dlen  : If not NULL, the size of the returned data will be stored
 *                at the address pointed by dlen
 * @return The stored value if any, NULL otherwise
 */
void *ht_get(hashtable_t *table, void *key, size_t klen, size_t *dlen);

/**
 * @brief Check if a key exists in the hashtable
 * @param table : A valid pointer to an hashtable_t structure
 * @param key   : The key to use
 * @param klen  : The length of the key
 * @return 1 If the key exists, 0 if it doesn't exist and -1 in case of error
 */
int ht_exists(hashtable_t *table, void *key, size_t klen);

/**
 * @brief Get a copy of the value stored at a specific key
 * @param table : A valid pointer to an hashtable_t structure
 * @param key   : The key to use
 * @param klen  : The length of the key
 * @param dlen  : If not NULL, the size of the returned data will be stored
 *                at the address pointed by dlen
 * @return The stored value if any, NULL otherwise
 * @note The returned value is a copy (memcpy) of the stored value and the 
 *       caller MUST release it using free() once done
 *
 * @note The copy is a simple copy done using memcpy() if the stored value
 *       is structured and requires a deep copy, then ht_get_deep_copy()
 *       should be used instead of this function
 */
void *ht_get_copy(hashtable_t *table, void *key, size_t klen, size_t *dlen);

typedef void *(*ht_deep_copy_callback_t)(void *data, size_t dlen, void *user);

/**
 * @brief Get a copy of the value stored at a specific key
 * @param table : A valid pointer to an hashtable_t structure
 * @param key   : The key to use
 * @param klen  : The length of the key
 * @param dlen  : If not NULL, the size of the returned data will be stored
 *               at the address pointed by dlen
 * @param copy_cb : The callback which will take care of deep-copying the data
 * @param user    : A private pointer which will be passed back to the copy_cb
 * @return The stored value if any, NULL otherwise
 * @note The returned value is eventually created by the deep_copy callback
 *       hence the caller knows if memory will need to be disposed or not and
 *       how to fully release the structured value which has been deep copied
 */
void *ht_get_deep_copy(hashtable_t *table, void *key, size_t klen, size_t *dlen, ht_deep_copy_callback_t copy_cb, void *user);

/**
 * @brief Set the value for a specific key
 * @param table : A valid pointer to an hashtable_t structure
 * @param key   : The key to use
 * @param klen  : The length of the key
 * @param data  : A pointer to the data to store
 * @param dlen  : The size of the data
 * @return 0 on success, -1 otherwise
 */
int ht_set(hashtable_t *table, void *key, size_t klen, void *data, size_t dlen);

/**
 * @brief Set the value for a specific key and returns the previous value if any
 * @param table : A valid pointer to an hashtable_t structure
 * @param key   : The key to use
 * @param klen  : The length of the key
 * @param data  : A pointer to the data to store
 * @param dlen  : The size of the data
 * @param prev_data : If not NULL, the referenced pointer will be set to point to the previous data
 * @param prev_len  : If not NULL, the size of the previous data will be stored in the memory
 *                    pointed by prev_len
 * @return 0 on success, -1 otherwise
 * @note If prev_data is not NULL, the previous data will not be released using the free_value callback
 *       so the caller will be responsible of releasing the previous data once done with it
 */
int ht_get_and_set(hashtable_t *table, void *key, size_t klen, void *data, size_t dlen, void **prev_data, size_t *prev_len);

/**
 * @brief Get the value for a specific key or set a new value if none has been found
 * @param table    : A valid pointer to an hashtable_t structure
 * @param key      : The key to use
 * @param klen     : The length of the key
 * @param data     : A pointer to the new data to store if none is found
 * @param dlen     : The size of the data to store
 * @param cur_data : If not NULL, the referenced pointer will be set to point to the current data
 * @param cur_len  : If not NULL, the size of the current data will be stored in the memory
 *                    pointed by cur_len
 * @return 0 the value new value has been set successfully;\n
 *         1 if a value was already set;\n
 *         -1 in case of errors
 */
int ht_get_or_set(hashtable_t *table, void *key, size_t klen, void *data, size_t dlen, void **cur_data, size_t *cur_len);

/**
 * @brief Set the value for a specific key and returns the previous value if any.
 *
 *        The new value will be copied before being stored
 *
 * @param table : A valid pointer to an hashtable_t structure
 * @param key   : The key to use
 * @param klen  : The length of the key
 * @param data  : A pointer to the data to store
 * @param dlen  : The size of the data
 * @param prev_data : If not NULL, the referenced pointer will be set to point to the previous data
 * @param prev_len  : If not NULL, the size of the previous data will be stored in the memory
 *                    pointed by prev_len
 * @return The previous value if any, NULL otherwise
 * @note If prev_data is not NULL, the previous data will not be released using the free_value callback
 *       so the caller will be responsible of releasing the previous data once done with it
 */
int ht_set_copy(hashtable_t *table, void *key, size_t klen, void *data, size_t dlen, void **prev_data, size_t *prev_len);


/**
 * @brief Set the value for a specific key if there is no value already stored
 * @param table : A valid pointer to an hashtable_t structure
 * @param key   : The key to use
 * @param klen  : The length of the key
 * @param data  : A pointer to the data to store
 * @param dlen  : The size of the data
 * @return 0 on success;\n
 *         1 if a value was already set;\n
 *         -1 in case of errors
 */
int ht_set_if_not_exists(hashtable_t *table, void *key, size_t klen, void *data, size_t dlen);

/**
 * @brief Set a new value stored at a specific key only if the actual one matches some provided data
 * @param table : A valid pointer to an hashtable_t structure
 * @param key : The key to use
 * @param klen : The length of the key
 * @param data  : A pointer to the data to store
 * @param dlen  : The size of the data
 * @param match : A valid pointer to the data we need to match in order to delete the value
 * @param match_size : The value of the data to match
 * @param prev_data : If not NULL the pointer will be set to point to the previous data
 * @param prev_len : If not NULL the integer pointer will be set to the size of the previous data
 * @node If the prev_data pointer is provided, the caller will be responsible of relasing
 *       the resources pointed after the call. If not provided (NULL) the free_value callback
 *       will be eventually used (if defined)
 * @return 0 on success;\n
 *         1 if the value didn't match (a the new value was not set),
 *         -1 in case of errors
 */
int ht_set_if_equals(hashtable_t *table, void *key, size_t klen, void *data, size_t dlen, void *match, size_t match_size, void **prev_data, size_t *prev_len);

/**
 * @brief Unset the value stored at a specific key
 * @param table : A valid pointer to an hashtable_t structure
 * @param key : The key to use
 * @param klen : The length of the key
 * @param prev_data : If not NULL, the referenced pointer will be set to point to the previous data
 * @param prev_len  : If not NULL, the size of the previous data will be stored in the memory
 *                    pointed by prev_len
 * @return The previous value if any, NULL otherwise
 * @note If prev_data is not NULL, the previous data will not be released using the free_value callback
 *       so the caller will be responsible of releasing the previous data once done with it
 */
int ht_unset(hashtable_t *table, void *key, size_t klen, void **prev_data, size_t *prev_len);

/**
 * @brief Delete the value stored at a specific key
 * @param table : A valid pointer to an hashtable_t structure
 * @param key : The key to use
 * @param klen : The length of the key
 * @param prev_data : If not NULL, the referenced pointer will be set to point to the previous data
 * @param prev_len  : If not NULL, the size of the previous data will be stored in the memory
 *                    pointed by prev_len
 * @return 0 on success, -1 otherwise
 * @note If prev_data is not NULL, the previous data will not be released using the free_value callback
 *       so the caller will be responsible of releasing the previous data once done with it
 */
int ht_delete(hashtable_t *table, void *key, size_t klen, void **prev_data, size_t *prev_len);

/**
 * @brief Delete the value stored at a specific key only if it matches some provided data
 * @param table : A valid pointer to an hashtable_t structure
 * @param key : The key to use
 * @param klen : The length of the key
 * @param match : A valid pointer to the data we need to match in order to delete the value
 * @param match_size : The value of the data to match
 * @return 0 on success, -1 otherwise
 */
int ht_delete_if_equals(hashtable_t *table, void *key, size_t klen, void *match, size_t match_size);

/**
 * @brief Callback called if an item for a given key is found
 * @param table : A valid pointer to an hashtable_t structure
 * @param key : The key to use
 * @param klen : The length of the key
 * @return 0 on success
 *         1 on success and the item must be removed from the hashtable
 *        -1 on error
 */
typedef int (*ht_pair_callback_t)(hashtable_t *table, void *key, size_t klen, void **value, size_t *vlen, void *user);

/**
 * @brief call the provided callback passing the item stored at the specified key (if any)
 * @param table : A valid pointer to an hashtable_t structure
 * @param key : The key to use
 * @param klen : The length of the key
 * @param cb : The callback
 * @param user : A private pointer which will be passed to the callback when invoked
 * @note the callback is called while the bucket-level mutex is being retained
 */
int ht_call(hashtable_t *table, void *key, size_t klen, ht_pair_callback_t cb, void *user);

/**
 * @brief Return the count of items actually stored in the table
 * @param table : A valid pointer to an hashtable_t structure
 * @return The actual item count
 */
size_t ht_count(hashtable_t *table);

// use the following two functions only if the hashtable_t contains
// a small number of keys, use the iterators otherwise

typedef struct _hashtable_key_s {
    void  *data;
    size_t len;
    size_t vlen;
} hashtable_key_t;

/**
 * @brief Get all stored keys
 * @param table : A valid pointer to an hashtable_t structure
 * @return A list of hashtable_key_t pointers with all the
 *         keys present in the table
 * @note The returned list should be released calling list_destroy()
 */
linked_list_t *ht_get_all_keys(hashtable_t *table);

typedef struct _hashtable_value_s {
    void *key;
    size_t klen;
    void  *data;
    size_t len;
} hashtable_value_t;

/**
 * @brief Get all stored values
 * @param table : A valid pointer to an hashtable_t structure
 * @return A list containing a pointer to all the values stored in the table
 * @note The returned list will contain pointers to the actual stored values
 *       and not copies
 * @note The returned list should be released calling list_destroy()
 */
linked_list_t *ht_get_all_values(hashtable_t *table);

typedef enum {
    HT_ITERATOR_STOP = 0,
    HT_ITERATOR_CONTINUE = 1,
    HT_ITERATOR_REMOVE = -1,
    HT_ITERATOR_REMOVE_AND_STOP = -2
} ht_iterator_status_t;

/**
 * @brief Callback for the key iterator
 * @param table : A valid pointer to an hashtable_t structure
 * @param key : The key
 * @param klen : The length of the key
 * @param user : The user pointer passed as argument to the ht_foreach_pair() function
 * @return HT_ITERATOR_CONTINUE to go ahead with the iteration,
 *         HT_ITERATOR_STOP to stop the iteration,
 *         HT_ITERATOR_REMOVE to remove the current item from the table and go ahead with the iteration
 *         HT_ITERATOR_REMOVE_AND_STOP to remove the current item from the table and stop the iteration
 */
typedef ht_iterator_status_t (*ht_key_iterator_callback_t)(hashtable_t *table, void *key, size_t klen, void *user);

/**
 * @brief Key iterator
 * @param table : A valid pointer to an hashtable_t structure
 * @param cb    : an ht_key_iterator_callback_t function
 * @param user  : A pointer which will be passed to the iterator callback at each call
 */
void ht_foreach_key(hashtable_t *table, ht_key_iterator_callback_t cb, void *user);

/**
 * @brief Callback for the value iterator
 * @param table : A valid pointer to an hashtable_t structure
 * @param value : The value
 * @param vlen : The length of the value
 * @param user : The user pointer passed as argument to the ht_foreach_pair() function
 * @return HT_ITERATOR_CONTINUE to go ahead with the iteration,
 *         HT_ITERATOR_STOP to stop the iteration,
 *         HT_ITERATOR_REMOVE to remove the current item from the table and go ahead with the iteration
 *         HT_ITERATOR_REMOVE_AND_STOP to remove the current item from the table and stop the iteration
 */
typedef ht_iterator_status_t (*ht_value_iterator_callback_t)(hashtable_t *table, void *value, size_t vlen, void *user);

/**
 * @brief Value iterator
 * @param table : A valid pointer to an hashtable_t structure
 * @param cb    : an ht_value_iterator_callback_t function
 * @param user  : A pointer which will be passed to the iterator callback at each call
 */
void ht_foreach_value(hashtable_t *table, ht_value_iterator_callback_t cb, void *user);

/**
 * @brief Callback for the pair iterator
 * @param table : A valid pointer to an hashtable_t structure
 * @param key : The key
 * @param klen : The length of the key
 * @param value : The value
 * @param vlen : The length of the value
 * @param user : The user pointer passed as argument to the ht_foreach_pair() function
 * @return HT_ITERATOR_CONTINUE to go ahead with the iteration,
 *         HT_ITERATOR_STOP to stop the iteration,
 *         HT_ITERATOR_REMOVE to remove the current item from the table and go ahead with the iteration
 *         HT_ITERATOR_REMOVE_AND_STOP to remove the current item from the table and stop the iteration
 */
typedef ht_iterator_status_t (*ht_pair_iterator_callback_t)(hashtable_t *table, void *key, size_t klen, void *value, size_t vlen, void *user);

/**
 * @brief Pair iterator
 * @param table : A valid pointer to an hashtable_t structure
 * @param cb    : an ht_pair_iterator_callback_t function
 * @param user  : A pointer which will be passed to the iterator callback at each call
 */
void ht_foreach_pair(hashtable_t *table, ht_pair_iterator_callback_t cb, void *user);

#ifdef __cplusplus
}
#endif

#endif

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
