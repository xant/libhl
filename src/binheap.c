#include "binheap.h"
#include "linklist.h"

typedef struct __binomial_tree_node_s {
    void *key;
    size_t klen;
    void *value;
    size_t vlen;
    struct __binomial_tree_node_s *parent;
    struct __binomial_tree_node_s **children;
    int num_children;
    binheap_t *bh;
} binomial_tree_node_t;

/*
typedef struct {
    binomial_tree_node_t *root;
} binomial_tree_t;
*/

struct __binheap_s {
    linked_list_t *trees;
    binheap_cmp_keys_callback cmp_keys_cb;
    int count;
};

static int __cmp_keys_default(void *k1, size_t kl1, void *k2, size_t kl2)
{
    if (kl1 != kl2)
        return kl1 - kl2;
    return memcmp(k1, k2, kl1);
}

int
binomial_tree_node_add(binomial_tree_node_t *node,
                       binomial_tree_node_t *child)
{
    node->children = realloc(node->children, node->num_children + 1);
    node->children[node->num_children++] = child;
    child->parent = node;
    return 0;
}

int
binomial_tree_node_find_min_child(binomial_tree_node_t *node)
{
    if (!node->num_children)
        return -1;

    int min_child_index = 0;
    int i;
    for (i = 0; i < node->num_children; i++) {
        binomial_tree_node_t *cur = node->children[i];
        binomial_tree_node_t *min = node->children[min_child_index];
        if (node->bh->cmp_keys_cb(cur->key, cur->klen, min->key, min->klen) <= 0)
            min_child_index = i;
    }
    return min_child_index;
}

int
binomial_tree_node_find_max_child(binomial_tree_node_t *node)
{
    if (!node->num_children)
        return -1;

    int max_child_index = 0;
    int i;
    for (i = 0; i < node->num_children; i++) {
        binomial_tree_node_t *cur = node->children[i];
        binomial_tree_node_t *max = node->children[max_child_index];
        if (node->bh->cmp_keys_cb(cur->key, cur->klen, max->key, max->klen) >= 0)
            max_child_index = i;
    }
    return max_child_index;
}

void
binomial_tree_node_destroy(binomial_tree_node_t *node)
{
    int i;
    binomial_tree_node_t *new_parent = NULL;

    if (node->parent) {
        new_parent = node->parent;
    } else if (node->num_children) {
        int max_child_index = binomial_tree_node_find_max_child(node);
        if (max_child_index >= 0) {
            new_parent = node->children[max_child_index];
            if (max_child_index < node->num_children - 1) {
                memcpy(&node->children[max_child_index],
                       &node->children[max_child_index + 1],
                       sizeof(binomial_tree_node_t *) * (node->num_children - max_child_index + 1));
                       
            }
            node->num_children--;
        }
        new_parent->parent = NULL;
    }

    for (i = 0; i < node->num_children; i++) {
        if (new_parent)
            binomial_tree_node_add(new_parent, node->children[i]);
        else
            node->children[i]->parent = NULL;
    }

    free(node->key);
    free(node);
    node->bh->count--;
}

binheap_t *
binheap_create(binheap_cmp_keys_callback cmp_keys_cb)
{
    binheap_t *bh = calloc(1, sizeof(binheap_t));
    bh->trees = create_list();
    bh->cmp_keys_cb = cmp_keys_cb ? cmp_keys_cb : __cmp_keys_default;
    set_free_value_callback(bh->trees, (free_value_callback_t)binomial_tree_node_destroy);
    return bh;
}

void
binheap_destroy(binheap_t *bh)
{
    destroy_list(bh->trees);
    free(bh);
}

int binomial_tree_merge(binomial_tree_node_t *node1, binomial_tree_node_t *node2)
{
    node1->children = realloc(node1->children, node1->num_children + 1);
    node1->children[node1->num_children++] = node2;
    node2->parent = node1;
    return 0;
}

int
binheap_insert(binheap_t *bh, void *key, size_t klen, void *value, size_t vlen)
{
    binomial_tree_node_t *node = calloc(1, sizeof(binomial_tree_node_t));
    node->bh = bh;
    node->key = malloc(klen);
    memcpy(node->key, key, klen);
    node->klen = klen;
    node->value = value;
    node->vlen = vlen;
    int order = 0;
    binomial_tree_node_t *tree = shift_value(bh->trees);
    while (tree && tree->num_children == order) {
        if (bh->cmp_keys_cb(node->key, node->klen, tree->key, tree->klen) >= 0)
        {
            binomial_tree_merge(node, tree);
        } else {
            binomial_tree_merge(tree, node);
        }
        node = tree;
        order++;
        tree = shift_value(bh->trees);
    }
    if (tree)
        unshift_value(bh->trees, tree);
    unshift_value(bh->trees, node);

    bh->count++;
    return 0;
}

int
binheap_delete(binheap_t *bh, void *key, size_t klen)
{
    binomial_tree_node_t *tree = NULL;
    int i;
    for (i = 0; i < list_count(bh->trees); i++) {
        binomial_tree_node_t *cur_tree = pick_value(bh->trees, i);
        if (bh->cmp_keys_cb(cur_tree->key, cur_tree->klen, key, klen) >= 0)
        {
            if (tree) {
                if (bh->cmp_keys_cb(tree->key, tree->klen, cur_tree->key, cur_tree->klen) >= 0)
                {
                    tree = cur_tree;
                }
            } else {
                tree = cur_tree;
            }
        }
    }

    binomial_tree_node_t *to_delete = tree;
    while(to_delete && bh->cmp_keys_cb(to_delete->key, to_delete->klen, key, klen) != 0)
    {
        binomial_tree_node_t *next_tree = NULL;
        for (i = 0; i < to_delete->num_children; i++) {
            binomial_tree_node_t *child = to_delete->children[i];
            
            if (bh->cmp_keys_cb(child->key, child->klen, key, klen) >= 0)
            {
                if (next_tree) {
                    if (bh->cmp_keys_cb(next_tree->key, next_tree->klen, child->key, child->klen) >= 0)
                    {
                        next_tree = child;
                    }
                } else {
                    next_tree = child;
                }
            }
        }
        if (next_tree) {
            to_delete = next_tree;
        } else {
            to_delete = NULL;
        }
    }

    if (to_delete) {
        binomial_tree_node_destroy(to_delete);
        return 0;
    }
    return -1;
}

static binomial_tree_node_t *
__binheap_maxmin(binheap_t *bh, int maxmin)
{
    int i;
    binomial_tree_node_t *node = NULL;
    for (i = 0; i < list_count(bh->trees); i ++) {
        binomial_tree_node_t *curtree = pick_value(bh->trees, i);
        if (!node) {
            node = curtree;
            continue;
        }
        int is_bigger = (bh->cmp_keys_cb(curtree->key, curtree->klen, node->key, node->klen) >= 0);
        if ((maxmin == 0 && is_bigger) || (maxmin != 0 && !is_bigger))
        {
            node = curtree;
        }
    }

    return node;
}

int
binheap_maximum(binheap_t *bh, void **key, size_t *klen, void **value, size_t *vlen)
{
    binomial_tree_node_t *maxroot = __binheap_maxmin(bh, 0);
    if (!maxroot)
        return -1;
    if (key)
        *key = maxroot->key;
    if (klen)
        *klen = maxroot->klen;
    if (value)
        *value = maxroot->value;
    if (vlen)
        *vlen = maxroot->vlen;
    return 0;
}

int
binheap_minimum(binheap_t *bh, void **key, size_t *klen, void **value, size_t *vlen)
{
    binomial_tree_node_t *minroot = __binheap_maxmin(bh, 1);
    if (minroot) { 
        while (minroot->num_children) {
            int min_child_index = binomial_tree_node_find_min_child(minroot);
            minroot = minroot->children[min_child_index];
        }
        if (key)
            *key = minroot->key;
        if (klen)
            *klen = minroot->klen;
        if (value)
            *value = minroot->value;
        if (vlen)
            *vlen = minroot->vlen;
        return 0;
    }
    return -1;
}

int
binheap_delete_minimum(binheap_t *bh)
{
    binomial_tree_node_t *minroot = __binheap_maxmin(bh, 1);
    if (minroot) { 
        while (minroot->num_children) {
            int min_child_index = binomial_tree_node_find_min_child(minroot);
            minroot = minroot->children[min_child_index];
        }
        binomial_tree_node_destroy(minroot);
        return 0;
    } 
    return -1;
}

int
binheap_delete_maximum(binheap_t *bh)
{
    binomial_tree_node_t *maxroot = __binheap_maxmin(bh, 0);

    if (!maxroot)
        return -1;

    binomial_tree_node_destroy(maxroot);

    return 0;
}


uint32_t
binheap_count(binheap_t *bh)
{
    return bh->count;
}
