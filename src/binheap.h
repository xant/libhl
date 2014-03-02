#ifndef __BINOMIAL_HEAP_H__
#define __BINOMIAL_HEAP_H__

#include <sys/types.h>
#include <stdint.h>

typedef enum {
    BINHEAP_MODE_MAX,
    BINHEAP_MODE_MIN
} binheap_mode_t;

typedef struct __binheap_s binheap_t;

typedef int (*binheap_cmp_keys_callback)(void *key1,
                                         size_t key1size,
                                         void *key2,
                                         size_t key2size);

typedef void (*binheap_incr_key_callback)(void *key,
                                          size_t keysize,
                                          void **new_key,
                                          size_t *new_keysize,
                                          int increment);

typedef void (*binheap_decr_key_callback)(void *key,
                                          size_t keysize,
                                          void **new_key,
                                          size_t *new_keysize,
                                          int decrement);

typedef struct {
    binheap_cmp_keys_callback cmp;
    binheap_incr_key_callback incr;
    binheap_decr_key_callback decr;
} binheap_callbacks_t;

binheap_t *binheap_create(const binheap_callbacks_t *keys_callbacks, binheap_mode_t mode);

void binheap_destroy(binheap_t *bh);

int binheap_insert(binheap_t *bh, void *key, size_t klen, void *value, size_t vlen);

int binheap_minimum(binheap_t *bh, void **key, size_t *klen, void **value, size_t *vlen);

int binheap_maximum(binheap_t *bh, void **key, size_t *klen, void **value, size_t *vlen);

int binheap_delete_minimum(binheap_t *bh, void **value, size_t *vlen);
int binheap_delete_maximum(binheap_t *bh, void **value, size_t *vlen);

int binheap_delete(binheap_t *bh, void *key, size_t klen, void **value, size_t *vlen);

void binheap_increase_minimum(binheap_t *bh, int incr);
void binheap_increase_maximum(binheap_t *bh, int incr);
void binheap_decrease_minimum(binheap_t *bh, int decr);
void binheap_decrease_maximum(binheap_t *bh, int decr);
void binheap_increase_key(binheap_t *bh, void *key, size_t klen, int incr);
void binheap_decrease_key(binheap_t *bh, void *key, size_t klen, int decr);

void binheap_decrease(binheap_t *bh, void *key, size_t klen);

binheap_t *binheap_merge(binheap_t *bh1, binheap_t *bh2);

uint32_t binheap_count(binheap_t *bh);

extern inline const binheap_callbacks_t *binheap_keys_callbacks_int16_t();
extern inline const binheap_callbacks_t *binheap_keys_callbacks_int32_t();
extern inline const binheap_callbacks_t *binheap_keys_callbacks_int64_t();
extern inline const binheap_callbacks_t *binheap_keys_callbacks_uint16_t();
extern inline const binheap_callbacks_t *binheap_keys_callbacks_uint32_t();
extern inline const binheap_callbacks_t *binheap_keys_callbacks_uint64_t();
extern inline const binheap_callbacks_t *binheap_keys_callbacks_float();
extern inline const binheap_callbacks_t *binheap_keys_callbacks_double();

#endif
