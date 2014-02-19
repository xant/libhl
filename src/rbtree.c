#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#include "rbtree.h"

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
    struct __rbtree_node_s *children[2]; 
    struct __rbtree_node_s *parent;
} rbtree_node_t;

struct __rbtree_s {
    rbtree_node_t *root;
    rbtree_free_value_callback free_value_cb;
};

rbtree_t *
rbtree_create(rbtree_free_value_callback free_value_cb)
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


    _rbtree_destroy_internal(node->children[0], free_value_cb);
    _rbtree_destroy_internal(node->children[1], free_value_cb);

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

    return 1 + _rbtree_walk_internal(rbt, node->children[0], cb, priv)
             + _rbtree_walk_internal(rbt, node->children[1], cb, priv);
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
    if (node->parent == gp->children[0])
        return gp->children[1];
    else
        return gp->children[0];
}


static int
_rbtree_add_internal(rbtree_t *rbt, rbtree_node_t *cur_node, rbtree_node_t *new_node)
{
    int rc;
    if (new_node->ksize != cur_node->ksize) {
        if (new_node->ksize > cur_node->ksize) {
            rc = -1;
        } else {
            rc = 1;
        }
    } else {
       rc = memcmp(cur_node->key, new_node->key, cur_node->ksize);
    }

    if (rc == 0) {
        // key matches, just set the new value
        new_node->parent = cur_node->parent;
        if (new_node->parent) {
            if (new_node->parent->children[0] == cur_node)
                new_node->parent->children[0] = new_node;
            else
                new_node->parent->children[1] = new_node;
        }
        new_node->children[0] = cur_node->children[0];
        new_node->children[1] = cur_node->children[1];

        if (new_node->children[0])
            new_node->children[0]->parent = new_node;

        if (new_node->children[1])
            new_node->children[1]->parent = new_node;

        if (new_node->value != cur_node->value && rbt->free_value_cb)
            rbt->free_value_cb(cur_node->value);

        free(cur_node->key);
        free(cur_node);
    } else if (rc > 0) {
        if (cur_node->children[0]) {
            return _rbtree_add_internal(rbt, cur_node->children[0], new_node);
        } else {
            cur_node->children[0] = new_node;
            new_node->parent = cur_node;
        }
    } else {
        if (cur_node->children[1]) {
            return _rbtree_add_internal(rbt, cur_node->children[1], new_node);
        } else {
            cur_node->children[1] = new_node;
            new_node->parent = cur_node;
        }
    }
    return 0;
}

static void 
rbtree_rotate_right(rbtree_t *rbt, rbtree_node_t *node)
{
    rbtree_node_t *p = node->children[0];
    node->children[0] = p ? p->children[1] : NULL;
    if (p)
        p->children[1] = node;

    rbtree_node_t *parent = node->parent;
    node->parent = p;
    if (p) {
        p->parent = parent;
        if (p->parent == NULL) {
            rbt->root = p;
        } else {
            if (parent->children[0] == node)
                parent->children[0] = p;
            else
                parent->children[1] = p;
        }

    } else {
        rbt->root = node;
    }
}

static void
rbtree_rotate_left(rbtree_t *rbt, rbtree_node_t *node)
{
    rbtree_node_t *p = node->children[1];
    node->children[1] = p ? p->children[0] : NULL;
    if (p) 
        p->children[0] = node;

    rbtree_node_t *parent = node->parent;
    node->parent = p;
    if (p) {
        p->parent = parent;
        if (p->parent == NULL) {
            rbt->root = p;
        } else {
            if (parent->children[0] == node)
                parent->children[0] = p;
            else
                parent->children[1] = p;
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
        node->color = RBTREE_COLOR_BLACK;
        rbt->root = node;
    } else {
        int rc = _rbtree_add_internal(rbt, rbt->root, node);
        if (rc != 0)
            return rc;
        
        if (!node->parent) { // case 1
            node->color = RBTREE_COLOR_BLACK;
            rbt->root = node;
        } else if (node->parent->color == RBTREE_COLOR_BLACK) {
            return 0;
        } else {
            rbtree_node_t *uncle = rbtree_uncle(node);
            if (uncle && uncle->color == RBTREE_COLOR_RED && node->parent->color == RBTREE_COLOR_RED) {
                node->parent->color = RBTREE_COLOR_BLACK;
                uncle->color = RBTREE_COLOR_BLACK;
                rbtree_node_t *grandparent = rbtree_grandparent(node);
                if (grandparent) {
                    rbtree_add(rbt, grandparent->key, grandparent->ksize, grandparent->value, grandparent->vsize); // XXX
                }
            } else {
                rbtree_node_t *grandparent = rbtree_grandparent(node);
                if (grandparent) {
                    if (node == node->parent->children[1] && node->parent == grandparent->children[0]) {
                        rbtree_rotate_left(rbt, node->parent);
                        node = node->children[0];
                    } else if (node == node->parent->children[0] && node->parent == grandparent->children[1]) {
                        rbtree_rotate_right(rbt, node->parent);
                        node = node->children[1];
                    }
                    node->parent->color = RBTREE_COLOR_BLACK;
                    grandparent->color = RBTREE_COLOR_RED;
                    if (node == node->parent->children[0])
                        rbtree_rotate_right(rbt, grandparent);
                    else
                        rbtree_rotate_left(rbt, grandparent);
                }
            }
        }

        
    }
    return 0;
}

typedef struct {
    void *key;
    size_t ksize;
    void **value;
    size_t *vsize;
} rbtree_find_callback_arg_t;

static int
_rbtree_find_internal(rbtree_t *rbt, rbtree_node_t *node, rbtree_find_callback_arg_t *arg)
{
    int rc;

    if (!node)
        return -1;

    if (arg->ksize < node->ksize) {
        rc = 1;
    } else if (arg->ksize > node->ksize) {
        rc = -1;
    } else {
        rc = memcmp(arg->key, node->key, node->ksize);
    }

    if (rc == 0) {
        *arg->value = node->value;
        *arg->vsize = node->vsize;
        return 0;
    }
    else if (rc > 0) {
        return _rbtree_find_internal(rbt, node->children[0], arg);
    } else {
        return _rbtree_find_internal(rbt, node->children[1], arg);
    }

    return -1;
}

int
rbtree_find(rbtree_t *rbt, void *k, size_t ksize, void **v, size_t *vsize)
{
    rbtree_find_callback_arg_t arg = {
        .key = k,
        .ksize = ksize,
        .value = v,
        .vsize = vsize
    };
    return _rbtree_find_internal(rbt, rbt->root, &arg);
}

