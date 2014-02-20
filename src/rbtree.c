#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#include "rbtree.h"

#define IS_BLACK(__n) (!(__n) || (__n)->color == RBTREE_COLOR_BLACK)
#define IS_RED(__n) ((__n) && (__n)->color == RBTREE_COLOR_RED)

#define PAINT_BLACK(__n) { if (__n) (__n)->color = RBTREE_COLOR_BLACK; }
#define PAINT_RED(__n) { if (__n) (__n)->color = RBTREE_COLOR_RED; }

typedef enum {
    RBTREE_COLOR_RED = 0,
    RBTREE_COLOR_BLACK,
} rbtree_color_t;

typedef struct __rbtree_node_s {
    rbtree_color_t color;
    void *key;
    size_t ksize;
    void *value;
    size_t vsize;
    struct __rbtree_node_s *left; 
    struct __rbtree_node_s *right; 
    struct __rbtree_node_s *parent;
} rbtree_node_t;

struct __rbtree_s {
    rbtree_node_t *root;
    rbtree_cmp_key_callback cmp_key_cb;
    rbtree_free_value_callback free_value_cb;
};

rbtree_t *
rbtree_create(rbtree_cmp_key_callback cmp_key_cb,
              rbtree_free_value_callback free_value_cb)
{
    rbtree_t *rbt = calloc(1, sizeof(rbtree_t));
    rbt->free_value_cb = free_value_cb;
    return rbt;
}

void
_rbtree_destroy_internal(rbtree_node_t *node, rbtree_free_value_callback free_value_cb)
{
    if (!node)
        return;


    _rbtree_destroy_internal(node->left, free_value_cb);
    _rbtree_destroy_internal(node->right, free_value_cb);

    free(node->key);
    if (free_value_cb)
        free_value_cb(node->value);

    free(node);
}


void
rbtree_destroy(rbtree_t *rbt)
{
    _rbtree_destroy_internal(rbt->root, rbt->free_value_cb);
    free(rbt);
}

int
_rbtree_walk_internal(rbtree_t *rbt, rbtree_node_t *node, rbtree_walk_callback cb, void *priv)
{
    if (!node)
        return 0;

    int rc = cb(rbt, node->key, node->ksize, node->value, node->vsize, priv);
    if (rc != 0)
        return 0;

    return 1 + _rbtree_walk_internal(rbt, node->left, cb, priv)
             + _rbtree_walk_internal(rbt, node->right, cb, priv);
}

int
rbtree_walk(rbtree_t *rbt, rbtree_walk_callback cb, void *priv)
{
    if (rbt->root)
        return _rbtree_walk_internal(rbt, rbt->root, cb, priv);

    return 0;
}

static rbtree_node_t *
rbtree_grandparent(rbtree_node_t *node)
{
    if (node && node->parent)
        return node->parent->parent;
    return NULL;
}

static rbtree_node_t *
rbtree_uncle(rbtree_node_t *node)
{
    rbtree_node_t *gp = rbtree_grandparent(node);
    if (!gp)
        return NULL;
    if (node->parent == gp->left)
        return gp->right;
    else
        return gp->left;
}


static int
rbtree_compare_keys(rbtree_t *rbt, void *k1, size_t k1size, void *k2, size_t k2size)
{
    int rc;
    if (rbt->cmp_key_cb) {
        rc = rbt->cmp_key_cb(k1, k1size, k2, k2size);
    } else {
        if (k1size != k2size) {
            if (k2size > k1size) {
                rc = memcmp(k1, k2, k1size) - (k2size - k1size);
            } else {
                rc = memcmp(k1, k2, k2size) + (k1size - k2size);
            }
        } else {
            rc = memcmp(k1, k2, k1size);
        }
    }
    return rc;
}

static int
_rbtree_add_internal(rbtree_t *rbt, rbtree_node_t *cur_node, rbtree_node_t *new_node)
{
    int rc = rbtree_compare_keys(rbt, cur_node->key, cur_node->ksize, new_node->key, new_node->ksize);

    if (rc == 0) {
        // key matches, just set the new value
        new_node->parent = cur_node->parent;
        if (new_node->parent) {
            if (new_node->parent->left == cur_node)
                new_node->parent->left = new_node;
            else
                new_node->parent->right = new_node;
        }
        new_node->left = cur_node->left;
        new_node->right = cur_node->right;

        if (new_node->left)
            new_node->left->parent = new_node;

        if (new_node->right)
            new_node->right->parent = new_node;

        if (new_node->value != cur_node->value && rbt->free_value_cb)
            rbt->free_value_cb(cur_node->value);

        free(cur_node->key);
        free(cur_node);
    } else if (rc > 0) {
        if (cur_node->left) {
            return _rbtree_add_internal(rbt, cur_node->left, new_node);
        } else {
            cur_node->left = new_node;
            new_node->parent = cur_node;
        }
    } else {
        if (cur_node->right) {
            return _rbtree_add_internal(rbt, cur_node->right, new_node);
        } else {
            cur_node->right = new_node;
            new_node->parent = cur_node;
        }
    }
    return 0;
}

static void 
rbtree_rotate_right(rbtree_t *rbt, rbtree_node_t *node)
{
    rbtree_node_t *p = node->left;
    node->left = p ? p->right : NULL;
    if (p)
        p->right = node;

    if (node->left)
        node->left->parent = node;

    rbtree_node_t *parent = node->parent;
    node->parent = p;
    if (p) {
        p->parent = parent;
        if (p->parent == NULL) {
            rbt->root = p;
        } else {
            if (parent->left == node)
                parent->left = p;
            else
                parent->right = p;
        }

    } else {
        rbt->root = node;
    }
}

static void
rbtree_rotate_left(rbtree_t *rbt, rbtree_node_t *node)
{
    rbtree_node_t *p = node->right;
    node->right = p ? p->left : NULL;
    if (p) 
        p->left = node;

    if (node->right)
        node->right->parent = node;

    rbtree_node_t *parent = node->parent;
    node->parent = p;
    if (p) {
        p->parent = parent;
        if (p->parent == NULL) {
            rbt->root = p;
        } else {
            if (parent->left == node)
                parent->left = p;
            else
                parent->right = p;
        }
    } else {
        rbt->root = node;
    }
}

int
rbtree_add(rbtree_t *rbt, void *k, size_t ksize, void *v, size_t vsize)
{
    rbtree_node_t *node = calloc(1, sizeof(rbtree_node_t));
    node->key = malloc(ksize);
    memcpy(node->key, k, ksize);
    node->ksize = ksize;
    node->value = v;
    node->vsize = vsize;
    if (!rbt->root) {
        PAINT_BLACK(node);
        rbt->root = node;
    } else {
        int rc = _rbtree_add_internal(rbt, rbt->root, node);
        if (rc != 0)
            return rc;
        
        if (!node->parent) { // case 1
            PAINT_BLACK(node);
            rbt->root = node;
        } else if (IS_BLACK(node->parent)) {
            return 0;
        } else {
            rbtree_node_t *uncle = rbtree_uncle(node);
            if (IS_RED(uncle) && IS_RED(node->parent)) {
                PAINT_BLACK(node->parent);
                PAINT_BLACK(uncle);
                rbtree_node_t *grandparent = rbtree_grandparent(node);
                if (grandparent) {
                    rbtree_add(rbt, grandparent->key, grandparent->ksize, grandparent->value, grandparent->vsize);
                }
            } else {
                rbtree_node_t *grandparent = rbtree_grandparent(node);
                if (grandparent) {
                    if (node == node->parent->right && node->parent == grandparent->left) {
                        rbtree_rotate_left(rbt, node->parent);
                        node = node->left;
                    } else if (node == node->parent->left && node->parent == grandparent->right) {
                        rbtree_rotate_right(rbt, node->parent);
                        node = node->right;
                    }
                    if (node->parent) {
                        PAINT_BLACK(node->parent);
                        PAINT_RED(grandparent);
                        if (node == node->parent->left)
                            rbtree_rotate_right(rbt, grandparent);
                        else
                            rbtree_rotate_left(rbt, grandparent);
                    }
                }
            }
        }

        
    }
    return 0;
}

static rbtree_node_t *
_rbtree_find_internal(rbtree_t *rbt, rbtree_node_t *node, void *key, size_t ksize)
{
    if (!node)
        return NULL;

    int rc = rbtree_compare_keys(rbt, node->key, node->ksize, key, ksize);

    if (rc == 0) {
        return node;
    }
    else if (rc > 0) {
        return _rbtree_find_internal(rbt, node->left, key, ksize);
    } else {
        return _rbtree_find_internal(rbt, node->right, key, ksize);
    }

    return NULL;
}

int
rbtree_find(rbtree_t *rbt, void *k, size_t ksize, void **v, size_t *vsize)
{
    rbtree_node_t *node = _rbtree_find_internal(rbt, rbt->root, k, ksize);
    if (!node)
        return -1;

    *v = node->value;
    *vsize = node->vsize;
    return 0;
}

static rbtree_node_t *
rbtree_sibling(rbtree_node_t *node)
{
    return (node == node->parent->left)
           ? node->parent->right
           : node->parent->left;
}

static int
is_leaf(rbtree_node_t *node)
{
    return (!node->left && !node->right);
}


static rbtree_node_t *
rbtree_find_next(rbtree_t *rbt, rbtree_node_t *node)
{
    if (!node->right)
        return NULL;

    rbtree_node_t *next = node->right;

    while (next->left)
        next = next->left;

    return next;
}

static rbtree_node_t *
rbtree_find_prev(rbtree_t *rbt, rbtree_node_t *node)
{
    if (!node->left)
        return NULL;

    rbtree_node_t *prev = node->left;

    while (prev->right)
        prev = prev->right;

    return prev;
}

void
rbtree_repaint_onremove(rbtree_t *rbt, rbtree_node_t *node)
{
    if (!node)
        return;

    // delete case 1
    if (node->parent != NULL) {
        // delete case 2
        rbtree_node_t *sibling = rbtree_sibling(node);
        if (IS_RED(sibling)) {
            PAINT_RED(node->parent);
            PAINT_BLACK(sibling);
            if (node == node->parent->left) {
                rbtree_rotate_left(rbt, node->parent);
            } else {
                rbtree_rotate_right(rbt, node->parent);
            }
        }

        // delete case 3
        if (IS_BLACK(node->parent) &&
            sibling &&
            IS_BLACK(sibling) &&
            IS_BLACK(sibling->left) &&
            IS_BLACK(sibling->right))
        {
            PAINT_RED(sibling);
            rbtree_repaint_onremove(rbt, node->parent);
        } else {
            // delete case 4
            if (IS_RED(node->parent) &&
                sibling &&
                IS_BLACK(sibling) &&
                IS_BLACK(sibling->left) &&
                IS_BLACK(sibling->right))
            {
                PAINT_RED(sibling);
                PAINT_BLACK(node->parent);
            } else {
                // delete case 5
                if (IS_BLACK(sibling)) {
                    if (node == node->parent->left &&
                        sibling &&
                        IS_BLACK(sibling->right) &&
                        IS_RED(sibling->left))
                    {
                        PAINT_RED(sibling);
                        PAINT_BLACK(sibling->left);
                        rbtree_rotate_right(rbt, sibling);
                    } else if (node == node->parent->right &&
                               sibling &&
                               IS_BLACK(sibling->left) &&
                               IS_RED(sibling->right))
                    {
                        PAINT_RED(sibling);
                        PAINT_BLACK(sibling->right);
                        rbtree_rotate_left(rbt, sibling);

                    }
                }
                // delete case 6
                if (sibling)
                    sibling->color = node->parent->color;
                PAINT_BLACK(node->parent);
                if (node == node->parent->left) {
                    if (sibling)
                        PAINT_BLACK(sibling->right);
                    rbtree_rotate_left(rbt, node->parent);
                } else {
                    if (sibling)
                        PAINT_BLACK(sibling->left);
                    rbtree_rotate_right(rbt, node->parent);
                }
            }
        }
    }
}

int
rbtree_remove(rbtree_t *rbt, void *k, size_t ksize)
{
    rbtree_node_t *node = _rbtree_find_internal(rbt, rbt->root, k, ksize);
    if (!node)
        return -1;

    if (!is_leaf(node)) {
        if (node->left && node->right) {
            // two children case
            rbtree_node_t *n = NULL;
            static int prevnext = 0;
            int isprev = (prevnext++%2 == 0);
            if (isprev)
                n = rbtree_find_prev(rbt, node);
            else
                n = rbtree_find_next(rbt, node);
            node->key = realloc(node->key, n->ksize);
            memcpy(node->key, n->key, n->ksize);
            void *prev_value = node->value;
            node->value = n->value;
            if (isprev) {
                if (n == node->left) {
                    node->left = n->left;
                } else {
                    n->parent->right = n->left;
                }
                if (n->left) {
                    n->left->parent = node;
                }
            } else {
                if (n == node->right) {
                    node->right = n->right;
                } else {
                    n->parent->left = n->right;
                }
                if (n->right) {
                    n->right->parent = node;
                }
            }
            free(n->key);
            if (rbt->free_value_cb)
                rbt->free_value_cb(prev_value);
            free(n);
            return 0;
        } else {
            // one child case
            rbtree_node_t *child = node->right ? node->right : node->left;
            // replace node with child
            child->parent = node->parent;
            if (child->parent) {
                if (node == node->parent->left)
                    node->parent->left = child;
                else
                    node->parent->right = child;
            }
            if (IS_BLACK(node)) {
                if (IS_RED(child)) {
                    PAINT_BLACK(child);
                } else {
                    rbtree_repaint_onremove(rbt, child);
                }
            }
            if (rbt->free_value_cb)
                rbt->free_value_cb(node->value);
            free(node->key);
            free(node);
            return 0;
        }
    }

    // if it's not the root node we need to update the parent
    if (node->parent) {
        if (node == node->parent->left)
            node->parent->left = NULL;
        else
            node->parent->right = NULL;
    }
    if (rbt->free_value_cb && node->value)
        rbt->free_value_cb(node->value);
    free(node->key);
    free(node);
    return 0;
}

