#ifndef HL_COMPARATORS_H
#define HL_COMPARATORS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <stdint.h>

/**
 * @brief Callback that, if provided, will be used to compare node keys.
 *        If not defined memcmp() will be used in the following way :
 * @param k1     The first key to compare
 * @param k1size The size of the first key to compare
 * @param k2     The second key to compare
 * @param k2size The size of the second key to compare
 * @return The distance between the two keys.
 *         0 will be returned if the keys match (both size and value);\n
 *         "k1size - k2size" will be returned if the two sizes don't match;\n
 *         The difference between the two keys is returned if the two sizes
 *         match but the value doesn't
 * @note By default memcmp() is be used to compare the value, a custom
 *       comparator can be
 *       registered at creation time (as parameter of rbtree_create())
 * @note If integers bigger than 8 bits are going to be used as keys,
 *       an integer comparator should be used instead of the default one
 *       (either a custom comparator or one of the rbtree_cmp_keys_int16(),
 *       rbtree_cmp_keys_int32() and rbtree_cmp_keys_int64() helpers provided
 *       by the library).
 *
 */
typedef int (*libhl_cmp_callback_t)(void *k1,
                                    size_t k1size,
                                    void *k2,
                                    size_t k2size);


#define LIBHL_CMP_KEYS_TYPE(_type, _k1, _k1s, _k2, _k2s) \
{ \
    if (_k1s < sizeof(_type) || _k2s < sizeof(_type) || _k1s != _k2s) \
        return _k1s - _k2s; \
    _type _k1i = *((_type *)_k1); \
    _type _k2i = *((_type *)_k2); \
    return _k1i - _k2i; \
}

/**
 * @brief 16bit signed integers comparator
 */
static int
libhl_cmp_keys_int16(void *k1, size_t k1size, void *k2, size_t k2size)
{
    LIBHL_CMP_KEYS_TYPE(int16_t, k1, k1size, k2, k2size);
}

/**
 * @brief 32bit signed integers comparator
 */
static int libhl_cmp_keys_int32(void *k1, size_t k1size,
                                        void *k2, size_t k2size)
{
    LIBHL_CMP_KEYS_TYPE(int32_t, k1, k1size, k2, k2size);
}

/**
 * @brief 64bit signed integers comparator
 */
static int libhl_cmp_keys_int64(void *k1, size_t k1size,
                                        void *k2, size_t k2size)
{
    LIBHL_CMP_KEYS_TYPE(int64_t, k1, k1size, k2, k2size);
}

/**
 * @brief 16bit unsigned integers comparator
 */
static int
libhl_cmp_keys_uint16(void *k1, size_t k1size,
                       void *k2, size_t k2size)
{
    LIBHL_CMP_KEYS_TYPE(uint16_t, k1, k1size, k2, k2size);
}

/**
 * @brief 32bit unsigned integers comparator
 */
static int libhl_cmp_keys_uint32(void *k1, size_t k1size,
                                         void *k2, size_t k2size)
{
    LIBHL_CMP_KEYS_TYPE(uint32_t, k1, k1size, k2, k2size);
}

/**
 * @brief 64bit unsigned integers comparator
 */
static int libhl_cmp_keys_uint64(void *k1, size_t k1size,
                                         void *k2, size_t k2size)
{
    LIBHL_CMP_KEYS_TYPE(uint64_t, k1, k1size, k2, k2size);
}

/**
 * @brief float comparator
 */
static int libhl_cmp_keys_float(void *k1, size_t k1size,
                                        void *k2, size_t k2size)
{
    LIBHL_CMP_KEYS_TYPE(float, k1, k1size, k2, k2size);
}

/**
 * @brief double comparator
 */
static int libhl_cmp_keys_double(void *k1, size_t k1size,
                                         void *k2, size_t k2size)
{
    LIBHL_CMP_KEYS_TYPE(double, k1, k1size, k2, k2size);
}

#ifdef __cplusplus
}
#endif

#endif

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
