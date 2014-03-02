#ifndef __BINOMIAL_HEAP_H__
#define __BINOMIAL_HEAP_H__

#include <sys/types.h>
#include <stdint.h>

typedef enum {
    BINHEAP_MODE_MAX,
    BINHEAP_MODE_MIN
} binheap_mode_t;

typedef struct __binheap_s binheap_t;

typedef int (*binheap_cmp_keys_callback)(void *k1,
                                         size_t k1size,
                                         void *k2,
                                         size_t k2size);

binheap_t *binheap_create(binheap_cmp_keys_callback cmp_keys_cb, binheap_mode_t mode);

void binheap_destroy(binheap_t *bh);

int binheap_insert(binheap_t *bh, void *key, size_t klen, void *value, size_t vlen);

int binheap_minimum(binheap_t *bh, void **key, size_t *klen, void **value, size_t *vlen);

int binheap_maximum(binheap_t *bh, void **key, size_t *klen, void **value, size_t *vlen);

int binheap_delete_minimum(binheap_t *bh);
int binheap_delete_maximum(binheap_t *bh);

int binheap_delete(binheap_t *bh, void *key, size_t klen);

binheap_t *binheap_merge(binheap_t *bh1, binheap_t *bh2);

uint32_t binheap_count(binheap_t *bh);

#define BINHEAP_CMP_KEYS_TYPE(__type, __k1, __k1s, __k2, __k2s) \
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
binheap_cmp_keys_int16(void *k1, size_t k1size, void *k2, size_t k2size)
{
    BINHEAP_CMP_KEYS_TYPE(int16_t, k1, k1size, k2, k2size);
}

/**
 * @brief 32bit signed integers comparator
 */
static inline int binheap_cmp_keys_int32(void *k1, size_t k1size,
                                        void *k2, size_t k2size)
{
    BINHEAP_CMP_KEYS_TYPE(int32_t, k1, k1size, k2, k2size);
}

/**
 * @brief 64bit signed integers comparator
 */
static inline int binheap_cmp_keys_int64(void *k1, size_t k1size,
                                        void *k2, size_t k2size)
{
    BINHEAP_CMP_KEYS_TYPE(int64_t, k1, k1size, k2, k2size);
}

/**
 * @brief 16bit unsigned integers comparator
 */
static inline int
binheap_cmp_keys_uint16(void *k1, size_t k1size,
                       void *k2, size_t k2size)
{
    BINHEAP_CMP_KEYS_TYPE(uint16_t, k1, k1size, k2, k2size);
}

/**
 * @brief 32bit unsigned integers comparator
 */
static inline int binheap_cmp_keys_uint32(void *k1, size_t k1size,
                                         void *k2, size_t k2size)
{
    BINHEAP_CMP_KEYS_TYPE(uint32_t, k1, k1size, k2, k2size);
}

/**
 * @brief 64bit unsigned integers comparator
 */
static inline int binheap_cmp_keys_uint64(void *k1, size_t k1size,
                                         void *k2, size_t k2size)
{
    BINHEAP_CMP_KEYS_TYPE(uint64_t, k1, k1size, k2, k2size);
}

/**
 * @brief float comparator
 */
static inline int binheap_cmp_keys_float(void *k1, size_t k1size,
                                        void *k2, size_t k2size)
{
    BINHEAP_CMP_KEYS_TYPE(float, k1, k1size, k2, k2size);
}

/**
 * @brief double comparator
 */
static inline int binheap_cmp_keys_double(void *k1, size_t k1size,
                                         void *k2, size_t k2size)
{
    BINHEAP_CMP_KEYS_TYPE(double, k1, k1size, k2, k2size);
}


#endif
