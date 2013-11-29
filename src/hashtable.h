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
 * @arg size : initial size of the table
 * @return a newly allocated and initialized table
 *
 * The table will be expanded if necessary
 */
hashtable_t *ht_create(uint32_t size, ht_free_item_callback_t free_item_cb);

/**
 * @brief Initialize a pre-allocated table descriptor
 *
 * This function can be used to initialize a statically defined table
 */
void ht_init(hashtable_t *table, uint32_t size, ht_free_item_callback_t free_item_cb);

/**
 * @brief Set the callback which must be called to release values stored in the table
 * @arg table : A valid pointer to an hashtable_t structure
 * @arg cb : an ht_free_item_callback_t function
 */
void ht_set_free_item_callback(hashtable_t *table, ht_free_item_callback_t cb);

/**
 * @brief Clear the table by removing all the stored items
 * @arg table : A valid pointer to an hashtable_t structure
 *
 * If a free_item_callback has been set, that will be called for each item removed from the table
 */
void ht_clear(hashtable_t *table);

/**
 * @brief Destroy the table by releasing all its resources
 * @arg table : A valid pointer to an hashtable_t structure
 *
 * If a free_item_callback has been set, that will be called for each item removed from the table
 */
void ht_destroy(hashtable_t *table);

/**
 * @brief Get the value stored at a specific key
 * @arg table : A valid pointer to an hashtable_t structure
 * @arg key : The key to use
 * @arg len : The length of the key
 * @return The stored value if any, NULL otherwise
 */
void *ht_get(hashtable_t *table, void *key, size_t klen, size_t *dlen);

void *ht_get_copy(hashtable_t *table, void *key, size_t klen, size_t *dlen);

typedef void *(*ht_deep_copy_callback_t)(void *data, size_t dlen);

void *ht_get_deep_copy(hashtable_t *table, void *key, size_t klen, size_t *dlen, ht_deep_copy_callback_t copy_cb);

/**
 * @brief Set the value for a specific key
 * @arg table : A valid pointer to an hashtable_t structure
 * @arg key: The key to use
 * @arg len : The length of the key
 * @return The previous value if any, NULL otherwise
 */
int ht_set(hashtable_t *table, void *key, size_t klen, void *data, size_t dlen, void **prev_data, size_t *prev_len);

int ht_set_copy(hashtable_t *table, void *key, size_t klen, void *data, size_t dlen, void **prev_data, size_t *prev_len);

/**
 * @brief Unset the value stored at a specific key
 * @arg table : A valid pointer to an hashtable_t structure
 * @arg key : The key to use
 * @arg len : The length of the key
 * @return The previous value if any, NULL otherwise
 */
int ht_unset(hashtable_t *table, void *key, size_t klen, void **prev_data, size_t *prev_len);

/**
 * @brief Delete the value stored at a specific key
 * @arg table : A valid pointer to an hashtable_t structure
 * @arg key : The key to use
 * @arg len : The length of the key
 * (equivalent to an ht_delete() call with the difference that
 * the previous value will be returned and the free callback won't be called)
 */
int ht_delete(hashtable_t *table, void *key, size_t klen, void **prev_data, size_t *prev_len);

/**
 * @brief Return the count of items actually stored in the table
 * @arg table : A valid pointer to an hashtable_t structure
 * @return The actual item count
 */
uint32_t ht_count(hashtable_t *table);

// use the following two functions only if the hashtable_t contains
// a small number of keys, use the iterators otherwise

/**
 * @brief Get all stored keys
 * @arg table : A valid pointer to an hashtable_t structure
 * @return A list of all keys present in the table
 */
linked_list_t *ht_get_all_keys(hashtable_t *table);

/**
 * @brief Get all stored values
 * @arg table : A valid pointer to an hashtable_t structure
 * @return A list of all keys present in the table
 */
linked_list_t *ht_get_all_values(hashtable_t *table);

/**
 * @brief Callback for the key iterator
 */
typedef void (*ht_key_iterator_callback_t)(hashtable_t *table, void *key, size_t klen, void *user);

/**
 * @brief Key iterator
 * @arg table : A valid pointer to an hashtable_t structure
 * @arg cb : an ht_key_iterator_callback_t function
 * @arg user : A pointer which will be passed to the iterator callback at each call
 */
void ht_foreach_key(hashtable_t *table, ht_key_iterator_callback_t cb, void *user);

/**
 * @brief Callback for the value iterator
 */
typedef void (*ht_value_iterator_callback_t)(hashtable_t *table, void *value, size_t vlen, void *user);

/**
 * @brief Value iterator
 * @arg table : A valid pointer to an hashtable_t structure
 * @arg cb : an ht_value_iterator_callback_t function
 * @arg user : A pointer which will be passed to the iterator callback at each call
 */
void ht_foreach_value(hashtable_t *table, ht_value_iterator_callback_t cb, void *user);

/**
 * @brief Callback for the pair iterator
 */
typedef void (*ht_pair_iterator_callback_t)(hashtable_t *table, void *key, size_t klen, void *value, size_t vlen, void *user);

/**
 * @brief Pair iterator
 * @arg table : A valid pointer to an hashtable_t structure
 * @arg cb : an ht_pair_iterator_callback_t function
 * @arg user : A pointer which will be passed to the iterator callback at each call
 */
void ht_foreach_pair(hashtable_t *table, ht_pair_iterator_callback_t cb, void *user);

#ifdef __cplusplus
}
#endif

#endif
