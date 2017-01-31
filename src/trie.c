#include <stdlib.h>
#include <string.h>

#include "trie.h"

typedef struct _trie_node_s {
    void *value;
    size_t vsize;
    int is_copy;
    int num_children;
    struct _trie_node_s *child[256];
    struct _trie_node_s *parent;
    char pidx;
} trie_node_t;

struct _trie_s {
    int count;
    int node_count;
    trie_node_t *root;
    trie_free_value_callback_t free_value_cb;
};

trie_t *
trie_create(trie_free_value_callback_t free_value_cb)
{
    trie_t *trie = calloc(1, sizeof(trie_t));
    trie->root = calloc(1, sizeof(trie_node_t));
    trie->free_value_cb = free_value_cb;
    return trie;
}


static inline void
trie_node_destroy(trie_t *trie, trie_node_t *node, trie_free_value_callback_t free_value_cb)
{
    trie_node_t *parent = node->parent;
    int pidx = node->pidx;

    while (parent && parent != trie->root) {
        if (parent->value || parent->num_children > 1)
            break;
        trie_node_t *empty_node = parent;
        pidx = parent->pidx;
        parent = parent->parent;
        free(empty_node);
        trie->node_count--;
    }

    if (parent) {
        parent->child[pidx] = NULL;
        parent->num_children--;
    }

    int i;
    for (i = 0; i < 256; i++)
        if (node->child[i]) {
            node->child[i]->parent = NULL;
            trie_node_destroy(trie, node->child[i], free_value_cb);
            node->child[i] = NULL;
            node->num_children--;
        }

    if (node->value) {
        trie->count--;
        if (node->is_copy)
            free(node->value);
        else if (free_value_cb)
            free_value_cb(node->value);
    }

    trie->node_count--;
    free(node);
}

void
trie_destroy(trie_t *trie)
{
    trie_node_destroy(trie, trie->root, trie->free_value_cb);
    free(trie);
}

static inline void
trie_node_set_value(trie_t *trie, trie_node_t *node, void *value, size_t vsize, int copy)
{
    if (node->value) {
        if (node->is_copy)
            free(node->value);
        else if (trie->free_value_cb)
            trie->free_value_cb(node->value);
    }

    if (copy) {
        node->value = malloc(vsize);
        memcpy(node->value, value, vsize);
        node->vsize = vsize;
        node->is_copy = 1;
    } else {
        node->value = value;
        node->vsize = vsize;
        node->is_copy = 0;
    }
}

int
trie_insert(trie_t *trie, char *key, void *value, size_t vsize, int copy)
{
    trie_node_t *node = trie->root, *tmp;
    int new_nodes = 0;
    while (*key && (tmp = node->child[(int)*key])) {
            node = tmp;
            ++key;
    }

    while (*key) {
        new_nodes++;
        node->num_children++;
        tmp = node->child[(int)*key] = calloc(1, sizeof(trie_node_t));
        tmp->parent = node;
        tmp->pidx = *key;
        node = tmp;
        ++key;
    }

    if (new_nodes) {
        trie->count++;
        trie->node_count += new_nodes;
    }

    if (node)
        trie_node_set_value(trie, node, value, vsize, copy);

    return new_nodes;
}

static inline trie_node_t *
trie_find_internal(trie_t *trie, char *key)
{
    trie_node_t *node;
    for (node = trie->root; *key && node; ++key) {
        node = node->child[(int)*key];
    }
    return node;
}

void *
trie_find(trie_t *trie, char *key, size_t *vsize)
{
    trie_node_t *node = trie_find_internal(trie, key);
    if (!node)
        return NULL;

    if (vsize)
        *vsize = node->vsize;

    return node->value;
}

int
trie_remove(trie_t *trie, char *key, void **value, size_t *vsize)
{
    trie_node_t *node = trie_find_internal(trie, key);
    if (!node)
        return 0;

    int num_nodes = trie->node_count;


    if (vsize)
        *vsize = node->vsize;

    if (value) {
        *value = node->value;
        trie_node_destroy(trie, node, NULL);
    } else {
        trie_node_destroy(trie, node, trie->free_value_cb);
    }

    return num_nodes - trie->node_count;
}

int
trie_find_or_insert(trie_t *trie, char *key, void *value, size_t vsize, void **prev_value, size_t *prev_vsize, int copy)
{
    trie_node_t *node = trie_find_internal(trie, key);
    if (!node)
        return trie_insert(trie, key, value, vsize, copy);

    if (prev_value)
        *prev_value = node->value;
    if (prev_vsize)
        *prev_vsize = node->vsize;

    return 0;
}

int
trie_find_and_insert(trie_t *trie, char *key, void *value, size_t vsize, void **prev_value, size_t *prev_vsize, int copy)
{

    trie_node_t *node = trie_find_internal(trie, key);
    if (node) {
        if (prev_value)
            *prev_value = node->value;
        if (prev_vsize)
            *prev_vsize = node->vsize;
        trie_node_set_value(trie, node, value, vsize, copy);
        return 0;
    }

    return trie_insert(trie, key, value, vsize, copy);
}


