#ifndef __RBTREE_H__
#define __RBTREE_H__

typedef struct __rbtree_s rbtree_t;

typedef void (*rbtree_free_value_callback)(void *v);

rbtree_t *rbtree_create(rbtree_free_value_callback free_value_cb);

void rbtree_destroy(rbtree_t *rbt);

int rbtree_add(rbtree_t *rbt, void *k, size_t ksize, void *v, size_t vsize);
int rbtree_remove(rbtree_t *rbt, void *k, size_t ksize);
int rbtree_find(rbtree_t *rbt, void *k, size_t ksize, void **v, size_t *vsize);

typedef int (*rbtree_walk_callback)(rbtree_t *rbt, void *key, size_t ksize, void *value, size_t vsize, void *priv);

int rbtree_walk(rbtree_t *rbt, rbtree_walk_callback cb, void *priv);


#endif
