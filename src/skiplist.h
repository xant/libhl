#ifndef __SKIPLIST_H__
#define __SKIPLIST_H__

#include <comparators.h>

typedef struct __skiplist_s skiplist_t;

typedef void (*skiplist_free_value_callback_t)(void *value);

skiplist_t *skiplist_create(int num_layers,
                            int probability,
                            libhl_cmp_callback_t compare_cb,
                            skiplist_free_value_callback_t free_value_cb);

int skiplist_insert(skiplist_t *skl, void *key, size_t klen, void *value);
int skiplist_remove(skiplist_t *skl, void *key, size_t klen, void **value);
void *skiplist_search(skiplist_t *skl, void *key, size_t klen);

void skiplist_destroy(skiplist_t *skl);


#endif
