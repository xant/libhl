#ifndef __BINOMIAL_HEAP_H__
#define __BINOMIAL_HEAP_H__

#include <sys/types.h>
#include <stdint.h>

typedef struct __binheap_s binheap_t;

typedef int (*binheap_cmp_keys_callback)(void *k1,
                                         size_t k1size,
                                         void *k2,
                                         size_t k2size);

binheap_t *binheap_create(binheap_cmp_keys_callback cmp_keys_cb);

void binheap_destroy(binheap_t *bh);

int binheap_insert(binheap_t *bh, void *key, size_t klen, void *value, size_t vlen);

int binheap_minimum(binheap_t *bh, void **key, size_t *klen, void **value, size_t *vlen);

int binheap_maximum(binheap_t *bh, void **key, size_t *klen, void **value, size_t *vlen);

int binheap_delete_minimum(binheap_t *bh);
int binheap_delete_maximum(binheap_t *bh);

int binheap_delete(binheap_t *bh, void *key, size_t klen);

binheap_t *binheap_merge(binheap_t *bh1, binheap_t *bh2);

uint32_t binheap_count(binheap_t *bh);

#endif
