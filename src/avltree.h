#ifndef __AVLT_H__
#define __AVLT_H__

#include <stdint.h>
#include <sys/types.h>
#include <comparators.h>

typedef struct __avlt_s avlt_t;

/**
 * @brief Callback that, if provided, will be called to release the value
 *        resources when an item is being overwritten or when removed from
 *        the tree
 */
typedef void (*avlt_free_value_callback_t)(void *v);

avlt_t *avlt_create(libhl_cmp_callback_t cmp_keys_cb,
                    avlt_free_value_callback_t free_value_cb);

int avlt_add(avlt_t *tree, void *key, size_t klen, void *value, size_t vlen);
int avlt_remove(avlt_t *tree, void *key, size_t klen, void **value, size_t *vlen);

typedef int (*avlt_walk_callback_t)(avlt_t *tree,
                                    void *key,
                                    size_t klen,
                                    void *value,
                                    size_t vlen,
                                    void *priv);

int avlt_walk(avlt_t *tree, avlt_walk_callback_t cb, void *priv);
int avlt_walk_sorted(avlt_t *tree, avlt_walk_callback_t cb, void *priv);

void avlt_destroy(avlt_t *tree);


#define AVLT_CMP_KEYS_TYPE(__type, __k1, __k1s, __k2, __k2s) \
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
avlt_cmp_keys_int16(void *k1, size_t k1size, void *k2, size_t k2size)
{
    AVLT_CMP_KEYS_TYPE(int16_t, k1, k1size, k2, k2size);
}

/**
 * @brief 32bit signed integers comparator
 */
static inline int avlt_cmp_keys_int32(void *k1, size_t k1size,
                                        void *k2, size_t k2size)
{
    AVLT_CMP_KEYS_TYPE(int32_t, k1, k1size, k2, k2size);
}

/**
 * @brief 64bit signed integers comparator
 */
static inline int avlt_cmp_keys_int64(void *k1, size_t k1size,
                                        void *k2, size_t k2size)
{
    AVLT_CMP_KEYS_TYPE(int64_t, k1, k1size, k2, k2size);
}

/**
 * @brief 16bit unsigned integers comparator
 */
static inline int
avlt_cmp_keys_uint16(void *k1, size_t k1size,
                       void *k2, size_t k2size)
{
    AVLT_CMP_KEYS_TYPE(uint16_t, k1, k1size, k2, k2size);
}

/**
 * @brief 32bit unsigned integers comparator
 */
static inline int avlt_cmp_keys_uint32(void *k1, size_t k1size,
                                         void *k2, size_t k2size)
{
    AVLT_CMP_KEYS_TYPE(uint32_t, k1, k1size, k2, k2size);
}

/**
 * @brief 64bit unsigned integers comparator
 */
static inline int avlt_cmp_keys_uint64(void *k1, size_t k1size,
                                         void *k2, size_t k2size)
{
    AVLT_CMP_KEYS_TYPE(uint64_t, k1, k1size, k2, k2size);
}

/**
 * @brief float comparator
 */
static inline int avlt_cmp_keys_float(void *k1, size_t k1size,
                                        void *k2, size_t k2size)
{
    AVLT_CMP_KEYS_TYPE(float, k1, k1size, k2, k2size);
}

/**
 * @brief double comparator
 */
static inline int avlt_cmp_keys_double(void *k1, size_t k1size,
                                         void *k2, size_t k2size)
{
    AVLT_CMP_KEYS_TYPE(double, k1, k1size, k2, k2size);
}

#ifdef DEBUG_AVLT
void avlt_print(avlt_t *tree);
#endif

#endif
