/**
 * @file   hashtable.h
 * @author Andrea Guzzo
 * @date   22/09/2013
 * @brief  Fast thread-safe hashtable implementation
 */
#ifndef __HASHTABLE_H__
#define __HASHTABLE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <linklist.h>

/**
 * @brief Opaque structure representing the actual hash table descriptor
 */
typedef struct __hashtable hashtable_t;

/**
 * @brief Callback that, if provided, will be called to release the value resources
 *        when an item is being removed from the table
 */
typedef void (*ht_free_item_callback_t)(void *);

/**
 * @brief Create a new table descriptor
 * @param initial_size : initial size of the table
 * @param max_size     : maximum size the table can be grown up to
 * @param free_item_cb : the callback to use when an item needs to be released
 * @return a newly allocated and initialized table
 *
 * The table will be expanded if necessary
 */
hashtable_t *ht_create(uint32_t initial_size, uint32_t max_size, ht_free_item_callback_t free_item_cb);

/**
 * @brief Initialize a pre-allocated table descriptor
 *
 * This function can be used to initialize a statically defined table
 */
void ht_init(hashtable_t *table, uint32_t initial_size, uint32_t max_size, ht_free_item_callback_t free_item_cb);

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

typedef void *(*ht_deep_copy_callback_t)(void *data, size_t dlen);

/**
 * @brief Get a copy of the value stored at a specific key
 * @param table : A valid pointer to an hashtable_t structure
 * @param key   : The key to use
 * @param klen  : The length of the key
 * @param dlen  : If not NULL, the size of the returned data will be stored
 *               at the address pointed by dlen
 * @param copy_cb : The callback which will take care of deep-copying the data
 * @return The stored value if any, NULL otherwise
 * @note The returned value is eventually created by the deep_copy callback
 *       hence the caller knows if memory will need to be disposed or not and
 *       how to fully release the structured value which has been deep copied
 */
void *ht_get_deep_copy(hashtable_t *table, void *key, size_t klen, size_t *dlen, ht_deep_copy_callback_t copy_cb);

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
 * @return The previous value if any, NULL otherwise
 * @note If prev_data is not NULL, the previous data will not be released using the free_value callback
 *       so the caller will be responsible of releasing the previous data once done with it
 */
int ht_get_and_set(hashtable_t *table, void *key, size_t klen, void *data, size_t dlen, void **prev_data, size_t *prev_len);

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
 * @brief Return the count of items actually stored in the table
 * @param table : A valid pointer to an hashtable_t structure
 * @return The actual item count
 */
uint32_t ht_count(hashtable_t *table);

// use the following two functions only if the hashtable_t contains
// a small number of keys, use the iterators otherwise

typedef struct __hashtable_key_s {
    void  *data;
    size_t len;
    size_t vlen;
} hashtable_key_t;

/**
 * @brief Get all stored keys
 * @param table : A valid pointer to an hashtable_t structure
 * @return A list of hashtable_key_t pointers with all the
 *         keys present in the table
 */
linked_list_t *ht_get_all_keys(hashtable_t *table);


typedef struct __hashtable_value_s {
    void  *data;
    size_t len;
} hashtable_value_t;

/**
 * @brief Get all stored values
 * @param table : A valid pointer to an hashtable_t structure
 * @return A list of all keys present in the table
 */
linked_list_t *ht_get_all_values(hashtable_t *table);

/**
 * @brief Callback for the key iterator
 */
typedef int (*ht_key_iterator_callback_t)(hashtable_t *table, void *key, size_t klen, void *user);

/**
 * @brief Key iterator
 * @param table : A valid pointer to an hashtable_t structure
 * @param cb    : an ht_key_iterator_callback_t function
 * @param user  : A pointer which will be passed to the iterator callback at each call
 */
void ht_foreach_key(hashtable_t *table, ht_key_iterator_callback_t cb, void *user);

/**
 * @brief Callback for the value iterator
 */
typedef int (*ht_value_iterator_callback_t)(hashtable_t *table, void *value, size_t vlen, void *user);

/**
 * @brief Value iterator
 * @param table : A valid pointer to an hashtable_t structure
 * @param cb    : an ht_value_iterator_callback_t function
 * @param user  : A pointer which will be passed to the iterator callback at each call
 */
void ht_foreach_value(hashtable_t *table, ht_value_iterator_callback_t cb, void *user);

/**
 * @brief Callback for the pair iterator
 */
typedef int (*ht_pair_iterator_callback_t)(hashtable_t *table, void *key, size_t klen, void *value, size_t vlen, void *user);

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
