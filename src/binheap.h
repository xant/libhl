/**
 * @file binheap.h
 *
 * @brief Binomial Heap
 *
 * Binomial Heap implementation for arbitrary data
 *
 * @todo Implement binheap_increase_key() and binheap_decrease_key()
 *
 */

#ifndef __BINOMIAL_HEAP_H__
#define __BINOMIAL_HEAP_H__

#include <sys/types.h>
#include <stdint.h>

/**
 * @brief Working modes (max-based or min-based)
 */ 
typedef enum {
    BINHEAP_MODE_MAX,
    BINHEAP_MODE_MIN
} binheap_mode_t;

/**
 * @brief Opaque structure representing the heap
 */
typedef struct __binheap_s binheap_t;

/**
 * @brief Callback used to compare two keys
 * @param key1     The first key to compare
 * @param key1size The size of the first key to compare
 * @param key2     The second key to compare
 * @param key2size The size of the second key to compare
 * @return The distance between the two keys.
 *         0 will be returned if the keys match (both size and value);\n
 *         "k1size - k2size" will be returned if the two sizes don't match;\n
 *         The difference between the two keys is returned if the two sizes
 *         match but the value doesn't
 * @note By default memcmp() is be used to compare the value, a custom
 *       comparator can be registered at creation time (part of the
 *       binheap_callbacks_t passed as parameter to binheap_create())
 * @note If integers bigger than 8 bits are going to be used as keys,
 *       an integer comparator should be used instead of the default one
 *       (either a custom comparator or one of the binheap_cmp_keys_int16(),
 *       binheap_cmp_keys_int32() and binheap_cmp_keys_int64() helpers provided
 *       by the library).
 *
 */
typedef int (*binheap_cmp_keys_callback)(void *key1,
                                         size_t key1size,
                                         void *key2,
                                         size_t key2size);

/**
 * @brief Callback used to increment a given key by an arbitrary amount
 * @param key         The key to increment
 * @param keysize     The size of the key
 * @param new_key     If NOT-NULL will be set to point to the new memory
 *                    storing the incremented key. The caller will be responsible
 *                    of releasing the memory allocated for the new key
 * @param new_keysize If NOT-NULL the memory pointed by new_keysize will be
 *                    set to the size of the memory holding the new_key
 * @param increment   How much to increment the key
 */
typedef void (*binheap_incr_key_callback)(void *key,
                                          size_t keysize,
                                          void **new_key,
                                          size_t *new_keysize,
                                          int increment);

/**
 * @brief Callback used to decrement a given key by an arbitrary amount
 * @param key         The key to decrement
 * @param keysize     The size of the key
 * @param new_key     If NOT-NULL will be set to point to the new memory
 *                    storing the decremented key. The caller will be responsible
 *                    of releasing the memory allocated for the new key
 * @param new_keysize If NOT-NULL the memory pointed by new_keysize will be
 *                    set to the size of the memory holding the new_key
 * @param decrement   How much to decrement the key
 */
typedef void (*binheap_decr_key_callback)(void *key,
                                          size_t keysize,
                                          void **new_key,
                                          size_t *new_keysize,
                                          int decrement);

typedef struct {
    binheap_cmp_keys_callback cmp; //!< compare two keys
    binheap_incr_key_callback incr; //!< increment a given key
    binheap_decr_key_callback decr; //!< decrement a given key
} binheap_callbacks_t;


/**
 * @brief Create a new binomial heap
 * @param keys_callbacks A pointer to the binheap_callbacks_t structure holding the callbacks
 *                       used for key management (compare/increment/decrement)
 * @param mode           The operational mode: BINHEAP_MODE_MAX or BINHEAP_MODE_MIN
 *                       which will determines if parents are bigger than children in
 *                       the internal binomial tree or viceversa (parents are smaller than children).
 * @note                 The difference between using BINHEAP_MODE_MAX and BINHEAP_MODE_MIN
 *                       is basically in complexity when extracting the maximum or the minimum value.\n
 *                       BINHEAP_MODE_MAX:\n
 *                         - accessing the maximum value is a O(1) operation\n
 *                         - accessing the minimum value is a O(logn) operation\n
 *                       BINHEAP_MODE_MIN:\n
 *                         - accessing the maximum value is a O(logn) operation\n
 *                         - accessing the minimum value is a O(1) operation\n
 *                         
 * @return               A valid and initialized binomial heap (empty)
 */
binheap_t *binheap_create(const binheap_callbacks_t *keys_callbacks, binheap_mode_t mode);

/**
 * @brief Release all the resources used by a binomial heap
 * @param bh A valid pointer to an initialized binheap_t structure
 */
void binheap_destroy(binheap_t *bh);

/**
 * @brief Insert a new value into the heap
 * @param bh A valid pointer to an initialized binheap_t structure
 * @param key  The key of the node where to store the new value
 * @param klen The size of the key
 * @param value The new value to store
 * @param vlen  The size of the value
 * @return 0 if a new node has been inserted successfully;
 *         -1 otherwise
 */
int binheap_insert(binheap_t *bh, void *key, size_t klen, void *value, size_t vlen);

/**
 * @brief Retrieve the minimum item in the heap
 * @param bh A valid pointer to an initialized binheap_t structure
 * @param key   If not null will be set to point to the minimum key in the heap
 * @param klen  If not null will be set to point to the size of the key
 * @param value If not null will be set to point to the value for the minimum item
 * @param vlen  If not null will be set to point to the size of the value
 * @return 0 if the minimum item has been successfully found,\n
 *         -1 in case of errors
 */
int binheap_minimum(binheap_t *bh, void **key, size_t *klen, void **value, size_t *vlen);

/**
 * @brief Retrieve the maximum item in the heap
 * @param bh A valid pointer to an initialized binheap_t structure
 * @param key   If not null will be set to point to the maximum key in the heap
 * @param klen  If not null will be set to point to the size of the key
 * @param value If not null will be set to point to the value for the maximum item
 * @param vlen  If not null will be set to point to the size of the value
 * @return 0 if the maximum item has been successfully found,\n
 *         -1 in case of errors
 */
int binheap_maximum(binheap_t *bh, void **key, size_t *klen, void **value, size_t *vlen);

/**
 * @brief Delete the minimum item in the heap (and eventually retrieve its value) 
 * @param bh A valid pointer to an initialized binheap_t structure
 * @param value If not null will be set to point to the value for the minimum item
 *              being removed
 * @param vlen  If not null will be set to point to the size of the value
 * @return 0 if the  minimum item has been found and removed successfully,\n
 *         -1 in case of errors
 */
int binheap_delete_minimum(binheap_t *bh, void **value, size_t *vlen);

/**
 * @brief Delete the maximum item in the heap (and eventually retrieve its value) 
 * @param bh A valid pointer to an initialized binheap_t structure
 * @param value If not null will be set to point to the value for the maximum item
 *              being removed
 * @param vlen  If not null will be set to point to the size of the value
 * @return 0 if the  minimum item has been found and removed successfully,\n
 *         -1 in case of errors
 */
int binheap_delete_maximum(binheap_t *bh, void **value, size_t *vlen);


/**
 * @brief Delete at most one item matching a given key in the heap
 * @param bh A valid pointer to an initialized binheap_t structure
 * @param key The key to match
 * @param klen The size of the key
 * @param value If not null will be set to point to the value for the maximum item
 *              being removed
 * @param vlen  If not null will be set to point to the size of the value
 * @return 0 if least one item has been found matching the given key
 *         and it has been removed successfully,\n -1 in case of errors
 */
int binheap_delete(binheap_t *bh, void *key, size_t klen, void **value, size_t *vlen);

/**
 * @brief Increase the minimum key by an arbitrary amount
 * @param bh A valid pointer to an initialized binheap_t structure
 * @param incr The amount to increase the minimum key by
 */
void binheap_increase_minimum(binheap_t *bh, int incr);

/**
 * @brief Increase the maximum key by an arbitrary amount
 * @param bh A valid pointer to an initialized binheap_t structure
 * @param incr The amount to increase the maximum key by
 */
void binheap_increase_maximum(binheap_t *bh, int incr);

/**
 * @brief Decrease the minimum key by an arbitrary amount
 * @param bh A valid pointer to an initialized binheap_t structure
 * @param decr The amount to decrease the minimum key by
 */
void binheap_decrease_minimum(binheap_t *bh, int decr);

/**
 * @brief Decrease the maximum key by an arbitrary amount
 * @param bh A valid pointer to an initialized binheap_t structure
 * @param decr The amount to decrease the maximum key by
 */
void binheap_decrease_maximum(binheap_t *bh, int decr);

/**
 * @brief Increase a given key by an arbitrary amount
 * @param bh A valid pointer to an initialized binheap_t structure
 * @param key The key to increase
 * @param klen The size of the key
 * @param incr The amount to increase the key by
 */
void binheap_increase_key(binheap_t *bh, void *key, size_t klen, int incr);

/**
 * @brief Decrease a given key by an arbitrary amount
 * @param key The key to increase
 * @param klen The size of the key
 * @param bh A valid pointer to an initialized binheap_t structure
 * @param decr The amount to decrease the key by
 */
void binheap_decrease_key(binheap_t *bh, void *key, size_t klen, int decr);

/**
 * @brief Merge two heaps
 * @param bh1 A valid pointer to an initialized binheap_t structure
 * @param bh2 A valid pointer to an initialized binheap_t structure
 * @return A newly created heap which will contain the union of the items
 *         stored in both the heaps (bh1 and bh2) provided as argument.
 *         The caller is responsible of disposing the new heap.
 * @note Both bh1 and bh2 will be empty once merged in the new returned heap.
 *       The caller is responsible of disposing both of them if not necessary
 *       anymore (otherwise further operations on the original heaps are still
 *       possible)
 * @note The two heaps MUST be configured to use the same operational mode
 *       for them to be merged. If the operational modes differ no merge 
 *       will be attempted and NULL will be returned
 */
binheap_t *binheap_merge(binheap_t *bh1, binheap_t *bh2);

/**
 * @brief Return the number of items in the heap
 * @param bh A valid pointer to an initialized binheap_t structure
 * @return the actual number of items in the heap
 */
uint32_t binheap_count(binheap_t *bh);

/**
 * @brief Default callbacks to handle 16bit signed integer keys
 */
extern inline const binheap_callbacks_t *binheap_keys_callbacks_int16_t();
/**
 * @brief Default callbacks to handle 32bit signed integer keys
 */
extern inline const binheap_callbacks_t *binheap_keys_callbacks_int32_t();
/**
 * @brief Default callbacks to handle 64bit signed integer keys
 */
extern inline const binheap_callbacks_t *binheap_keys_callbacks_int64_t();
/**
 * @brief Default callbacks to handle 16bit unsigned integer keys
 */
extern inline const binheap_callbacks_t *binheap_keys_callbacks_uint16_t();
/**
 * @brief Default callbacks to handle 32bit unsigned integer keys
 */
extern inline const binheap_callbacks_t *binheap_keys_callbacks_uint32_t();
/**
 * @brief Default callbacks to handle 64bit unsigned integer keys
 */
extern inline const binheap_callbacks_t *binheap_keys_callbacks_uint64_t();
/**
 * @brief Default callbacks to handle float keys
 */
extern inline const binheap_callbacks_t *binheap_keys_callbacks_float();
/**
 * @brief Default callbacks to handle double keys
 */
extern inline const binheap_callbacks_t *binheap_keys_callbacks_double();

#endif
