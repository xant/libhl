#ifndef __RBTREE_H__
#define __RBTREE_H__

/**
 * @brief Opaque structure representing the tree
 */
typedef struct __rbtree_s rbtree_t;

/*
 * @brief Callback that, if provided, will be called to release the value resources
 *        when an item is being overwritten or when removed from the tree
 */
typedef void (*rbtree_free_value_callback)(void *v);

/*
 * @brief Callback that, if provided, will be used to compare node keys.
 *        If not defined memcmp() will be used in the following way :
 *
 *        @note If integers bigger than 8 bits are being used as keys
 *              an integer comparator should be used (either a custom comparator or
 *              one of the rbtree_cmp_keys_int16() rbtree_cmp_keys_int32() rbtree_cmp_keys_int64()
 *              should be used) since if on little endian architecture memcmp() is not going to do
 *              the right thing but values need to be compared as integers and not comparing the
 *              memory directly)
 */
typedef int (*rbtree_cmp_keys_callback)(void *k1, size_t k1size, void *k2, size_t k2size);

rbtree_t *rbtree_create(rbtree_cmp_keys_callback cmp_key_cb,
                        rbtree_free_value_callback free_value_cb);

void rbtree_destroy(rbtree_t *rbt);

int rbtree_add(rbtree_t *rbt, void *k, size_t ksize, void *v, size_t vsize);
int rbtree_remove(rbtree_t *rbt, void *k, size_t ksize);
int rbtree_find(rbtree_t *rbt, void *k, size_t ksize, void **v, size_t *vsize);

typedef int (*rbtree_walk_callback)(rbtree_t *rbt, void *key, size_t ksize, void *value, size_t vsize, void *priv);

int rbtree_walk(rbtree_t *rbt, rbtree_walk_callback cb, void *priv);

#ifdef DEBUG_RBTREE
void rbtree_print(rbtree_t *rbt);
#endif

#define RBTREE_CMP_KEYS_TYPE(__type, __k1, __k1s, __k2, __k2s) \
{ \
    if (__k1s != sizeof(__type) || __k2s != sizeof(__type)) \
        return 0; \
    __type __k1i = *((__type *)__k1); \
    __type __k2i = *((__type *)__k2); \
    return __k1i - __k2i; \
}

static inline int
rbtree_cmp_keys_int16(void *k1, size_t k1size, void *k2, size_t k2size)
{
    RBTREE_CMP_KEYS_TYPE(int16_t, k1, k1size, k2, k2size);
}

static inline int rbtree_cmp_keys_int32(void *k1, size_t k1size, void *k2, size_t k2size)
{
    RBTREE_CMP_KEYS_TYPE(int32_t, k1, k1size, k2, k2size);
}

static inline int rbtree_cmp_keys_int64(void *k1, size_t k1size, void *k2, size_t k2size)
{
    RBTREE_CMP_KEYS_TYPE(int64_t, k1, k1size, k2, k2size);
}

static inline int
rbtree_cmp_keys_uint16(void *k1, size_t k1size, void *k2, size_t k2size)
{
    RBTREE_CMP_KEYS_TYPE(uint16_t, k1, k1size, k2, k2size);
}

static inline int rbtree_cmp_keys_uint32(void *k1, size_t k1size, void *k2, size_t k2size)
{
    RBTREE_CMP_KEYS_TYPE(uint32_t, k1, k1size, k2, k2size);
}

static inline int rbtree_cmp_keys_uint64(void *k1, size_t k1size, void *k2, size_t k2size)
{
    RBTREE_CMP_KEYS_TYPE(uint64_t, k1, k1size, k2, k2size);
}

#endif
