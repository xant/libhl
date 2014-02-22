/**
 * @file rbtree.h
 *
 * @brief Red/Black Tree
 *
 * Red/Black Tree implementation to store/access arbitrary data
 *
 */

#ifndef __RBTREE_H__
#define __RBTREE_H__

#include <stdint.h>

/**
 * @brief Opaque structure representing the tree
 */
typedef struct __rbtree_s rbtree_t;

/**
 * @brief Callback that, if provided, will be called to release the value resources
 *        when an item is being overwritten or when removed from the tree
 */
typedef void (*rbtree_free_value_callback)(void *v);

/**
 * @brief Callback that, if provided, will be used to compare node keys.
 *        If not defined memcmp() will be used in the following way :
 * @param k1     The first key to compare
 * @param k1size The size of the first key to compare
 * @param k2     The second key to compare
 * @param k2size The size of the second key to compare
 * @return The distance between the two keys. If the size is different
 *         0 will be returned if the keys match (both size and value);
 *         "k1size - k2size" will be returned if the two sizes don't match;
 *         The difference between the two keys is returned if the two sizes match
 *         but the value doesn't
 * @note By default memcmp() is be used to compare the value, a custom comparator can be
 *       registered at creation time (as parameter of rbtree_create())
 * @note If integers bigger than 8 bits are going to be used as keys,
 *       an integer comparator should be used instead of the default one
 *       (either a custom comparator or one of the rbtree_cmp_keys_int16(), rbtree_cmp_keys_int32()
 *       and rbtree_cmp_keys_int64() helpers provided by the library).
 *
 */
typedef int (*rbtree_cmp_keys_callback)(void *k1, size_t k1size, void *k2, size_t k2size);

/**
 * @brief Create a new red/black tree
 * @param cmp_key_cb    The comparator callback to use when comparing keys (defaults to memcmp())
 * @param free_value_cb The callback used to release values when a node is removed or overwritten
 * @return              A valid and initialized red/black tree (empty)
 */
rbtree_t *rbtree_create(rbtree_cmp_keys_callback cmp_key_cb,
                        rbtree_free_value_callback free_value_cb);

/**
 * @brief Release all the resources used by a red/black tree
 * @param rbt A valid pointer to an initialized rbtree_t structure
 */
void rbtree_destroy(rbtree_t *rbt);

/**
 * @brief Add a new value into the tree
 * @param rbt   A valid pointer to an initialized rbtree_t structure
 * @param k     The key of the node where to store the new value
 * @param ksize The size of the key
 * @param v     The new value to store
 * @param vsize The size of the value
 * @return 0 if a new node has been created successfully;
 *         1 if an existing node has been found and the value has been updated;
 *         -1 otherwise
 */
int rbtree_add(rbtree_t *rbt, void *k, size_t ksize, void *v, size_t vsize);


/**
 * @brief Remove a node from the tree
 * @param rbt   A valid pointer to an initialized rbtree_t structure
 * @param k     The key of the node to remove
 * @param ksize The size of the key
 * @return 0 on success; -1 otherwise
 */
int rbtree_remove(rbtree_t *rbt, void *k, size_t ksize);

/**
 * @brief Find the value stored in the node node matching a specific key (if any)
 * @param rbt   A valid pointer to an initialized rbtree_t structure
 * @param k     The key of the node where to store the new value
 * @param ksize The size of the key
 * @param v     A reference to the pointer which will set to point to the actual value if found
 * @param vsize A pointer to the memory where to store the size of the value
 * @return 0 on success and both *v and *vsize are set to point to the stored value and its size;
 *         -1 if not found
 */
int rbtree_find(rbtree_t *rbt, void *k, size_t ksize, void **v, size_t *vsize);

/**
 * @brief Callback called for each node when walking the tree
 * @param rbt   A valid pointer to an initialized rbtree_t structure
 * @param k     The key of the node where to store the new value
 * @param ksize The size of the key
 * @param v     The new value to store
 * @param vsize The size of the value
 * @return 0 If the walker can go ahead visiting the next node,
 *         any NON-ZERO integer if the walker should stop and return
 */
typedef int (*rbtree_walk_callback)(rbtree_t *rbt, void *key, size_t ksize, void *value, size_t vsize, void *priv);

/**
 * @brief Walk the entire tree and call the provided callback for each visited node
 * @param rbt  A valid pointer to an initialized rbtree_t structure
 * @param cb   The callback to call for each visited node
 * @param priv A pointer to private data provided passed as argument to the callback
 *             when invoked.
 * @return The number of visited nodes
 */
int rbtree_walk(rbtree_t *rbt, rbtree_walk_callback cb, void *priv);

#define RBTREE_CMP_KEYS_TYPE(__type, __k1, __k1s, __k2, __k2s) \
{ \
    if (__k1s < sizeof(__type) || __k2s < sizeof(__type) || __k1s != __k2s) \
        return __k1s - __k2s; \
    __type __k1i = *((__type *)__k1); \
    __type __k2i = *((__type *)__k2); \
    return __k1i - __k2i; \
}

/**
 * @brief 16bit signed integers comparator
 */
static inline int
rbtree_cmp_keys_int16(void *k1, size_t k1size, void *k2, size_t k2size)
{
    RBTREE_CMP_KEYS_TYPE(int16_t, k1, k1size, k2, k2size);
}

/**
 * @brief 32bit signed integers comparator
 */
static inline int rbtree_cmp_keys_int32(void *k1, size_t k1size, void *k2, size_t k2size)
{
    RBTREE_CMP_KEYS_TYPE(int32_t, k1, k1size, k2, k2size);
}

/**
 * @brief 64bit signed integers comparator
 */
static inline int rbtree_cmp_keys_int64(void *k1, size_t k1size, void *k2, size_t k2size)
{
    RBTREE_CMP_KEYS_TYPE(int64_t, k1, k1size, k2, k2size);
}

/**
 * @brief 16bit unsigned integers comparator
 */
static inline int
rbtree_cmp_keys_uint16(void *k1, size_t k1size, void *k2, size_t k2size)
{
    RBTREE_CMP_KEYS_TYPE(uint16_t, k1, k1size, k2, k2size);
}

/**
 * @brief 32bit unsigned integers comparator
 */
static inline int rbtree_cmp_keys_uint32(void *k1, size_t k1size, void *k2, size_t k2size)
{
    RBTREE_CMP_KEYS_TYPE(uint32_t, k1, k1size, k2, k2size);
}

/**
 * @brief 64bit unsigned integers comparator
 */
static inline int rbtree_cmp_keys_uint64(void *k1, size_t k1size, void *k2, size_t k2size)
{
    RBTREE_CMP_KEYS_TYPE(uint64_t, k1, k1size, k2, k2size);
}

/**
 * @brief float comparator
 */
static inline int rbtree_cmp_keys_float(void *k1, size_t k1size, void *k2, size_t k2size)
{
    RBTREE_CMP_KEYS_TYPE(float, k1, k1size, k2, k2size);
}

/**
 * @brief double comparator
 */
static inline int rbtree_cmp_keys_double(void *k1, size_t k1size, void *k2, size_t k2size)
{
    RBTREE_CMP_KEYS_TYPE(double, k1, k1size, k2, k2size);
}

#ifdef DEBUG_RBTREE
/**
 * @brief Print out the whole tree on stdout (for debugging purposes only)
 */
void rbtree_print(rbtree_t *rbt);
#endif

#endif
