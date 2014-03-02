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

struct __binheap_s {
    linked_list_t *trees;
    const binheap_callbacks_t *cbs;
    int count;
    int mode;
};

#define HAS_PRECEDENCE(__bh, __k1, __kl1, __k2, __kl2) \
((__bh->mode == BINHEAP_MODE_MAX  && __bh->cbs->cmp(__k1, __kl1, __k2, __kl2) >= 0) || \
 (__bh->mode == BINHEAP_MODE_MIN  && __bh->cbs->cmp(__k1, __kl1, __k2, __kl2) <= 0))

static inline int __cmp_keys_default(void *k1, size_t kl1, void *k2, size_t kl2)
{
    if (kl1 != kl2)
        return kl1 - kl2;
    return memcmp(k1, k2, kl1);
}

static inline void __incr_key_default(void *k1, size_t kl1, void **out, size_t *olen, int incr)
{
    size_t size = kl1;
    size_t off = kl1 - 1;
    unsigned char *key = (unsigned char *)k1;
    unsigned char b = key[off];
    unsigned char *nk = NULL;

    if (out) {
        nk = malloc(kl1);
        memcpy(nk, key, kl1);
    }

    b += incr;
    while (off > 0 && b < key[off]) {
        if (nk)
            nk[off--] = b;
        b = key[off] + 1;
    }

    if (nk && off == 0 && b < nk[0]) {
        unsigned char *copy = malloc(++size);
        copy[0] = 0x01;
        memcpy(copy + 1, nk, kl1);
        free(nk);
        nk = copy;
    }

    if (out)
        *out = nk;

    if (olen)
        *olen = size;


}

static inline void __decr_key_default(void *k1, size_t kl1, void **out, size_t *olen, int decr)
{
    size_t size = kl1;
    size_t off = kl1 - 1;
    unsigned char *key = (unsigned char *)k1;
    unsigned char b = key[off];
    unsigned char *nk = NULL;

    if (out) {
        nk = malloc(kl1);
        memcpy(nk, key, kl1);
    }

    b -= decr;
    while (off > 0 && b > key[off]) {
        if (nk)
            nk[off--] = b;
        b = key[off] - 1;
    }

    if (nk && off == 0 && b > nk[0]) {
        unsigned char *copy = malloc(++size);
        copy[0] = 0xFF;
        memcpy(copy + 1, nk, kl1);
        free(nk);
        nk = copy;
    }

    if (out)
        *out = nk;

    if (olen)
        *olen = size;

}


static binheap_callbacks_t __keys_callbacks_default = {
    .cmp = __cmp_keys_default,
    .incr = __incr_key_default,
    .decr = __decr_key_default
};

static int
binomial_tree_node_add(binomial_tree_node_t *node,
                       binomial_tree_node_t *child)
{
    node->children = realloc(node->children, sizeof(binomial_tree_node_t *) * (node->num_children + 1));
    node->children[node->num_children++] = child;
    if (child->parent) {
        // TODO - remove the node
    }
    child->parent = node;
    return 0;
}

static int
binomial_tree_node_find_min_child(binomial_tree_node_t *node)
{
    if (!node->num_children)
        return -1;

    int min_child_index = 0;
    int i;
    for (i = 0; i < node->num_children; i++) {
        binomial_tree_node_t *cur = node->children[i];
        binomial_tree_node_t *min = node->children[min_child_index];
        if (node->bh->cbs->cmp(cur->key, cur->klen, min->key, min->klen) <= 0)
            min_child_index = i;
    }
    return min_child_index;
}

static int
binomial_tree_node_find_max_child(binomial_tree_node_t *node)
{
    if (!node->num_children)
        return -1;

    int max_child_index = 0;
    int i;
    for (i = 0; i < node->num_children; i++) {
        binomial_tree_node_t *cur = node->children[i];
        binomial_tree_node_t *max = node->children[max_child_index];
        if (node->bh->cbs->cmp(cur->key, cur->klen, max->key, max->klen) >= 0)
            max_child_index = i;
    }
    return max_child_index;
}

static void
binomial_tree_node_increase_key(binomial_tree_node_t *node, int incr)
{
    void *okey = node->key;
    size_t oklen = node->klen;
    void *nkey = NULL;
    size_t nklen = 0;

    if (incr == 0)
        return;
    else if (incr > 0)
        node->bh->cbs->incr(okey, oklen, &nkey, &nklen, incr);
    else 
        node->bh->cbs->decr(okey, oklen, &nkey, &nklen, -incr);

    binomial_tree_node_t *parent = node->parent;

    int swapped = 0;
    while (parent && HAS_PRECEDENCE(node->bh, nkey, nklen, parent->key, parent->klen))
    {
        binomial_tree_node_t tmp;
        tmp.key = parent->key;
        tmp.klen = parent->klen;
        tmp.value = parent->value;
        tmp.vlen = parent->vlen;

        parent->key = nkey;
        parent->klen = nklen;
        parent->value = node->value;
        parent->vlen = node->vlen;

        free(node->key);
        node->key = tmp.key;
        node->klen = tmp.klen;
        node->value = tmp.value;
        node->vlen = tmp.vlen;

        binomial_tree_node_t *op = parent;
        parent = parent->parent; 
        node = op;
        swapped++;
    }

    if (!swapped) {
        free(node->key);
        node->key = nkey;
        node->klen = nklen;
    }
}

static void
binomial_tree_node_destroy(binomial_tree_node_t *node)
{
    int i;
    binomial_tree_node_t *new_parent = NULL;

    if (node->parent) {
        new_parent = node->parent;
        int node_index = -1;
        for (i = 0; i < new_parent->num_children; i++) {
            if (new_parent->children[i] == node) {
                node_index = i;
                break;
            }
        }
        if (new_parent->num_children && node_index >= 0) {
            int to_copy = new_parent->num_children - (node_index + 1);
            if (to_copy) {
                memcpy(&new_parent->children[node_index],
                       &new_parent->children[node_index+1],
                       sizeof(binomial_tree_node_t *) * to_copy);
            } else {
                // TODO - Error messages
                // (something is badly corrupted if we are here)
            }
            new_parent->num_children--;
        }
    } else if (node->num_children) {
        int child_index = node->bh->mode == BINHEAP_MODE_MAX
                        ? binomial_tree_node_find_max_child(node)
                        : binomial_tree_node_find_min_child(node);

        if (child_index >= 0) {
            new_parent = node->children[child_index];
            if (child_index < node->num_children - 1) {
                memcpy(&node->children[child_index],
                       &node->children[child_index + 1],
                       sizeof(binomial_tree_node_t *) * (node->num_children - child_index + 1));
                       
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
binheap_create(const binheap_callbacks_t *keys_callbacks, binheap_mode_t mode)
{
    binheap_t *bh = calloc(1, sizeof(binheap_t));
    bh->trees = create_list();
    bh->cbs = keys_callbacks ? keys_callbacks : &__keys_callbacks_default;
    bh-> mode = mode;
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
    node1->children = realloc(node1->children, sizeof(binomial_tree_node_t *) * (node1->num_children + 1));
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
        if (HAS_PRECEDENCE(bh, node->key, node->klen, tree->key, tree->klen)) {
            binomial_tree_merge(node, tree);
        } else {
            binomial_tree_merge(tree, node);
            node = tree;
        }
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
binheap_delete(binheap_t *bh, void *key, size_t klen, void **value, size_t *vlen)
{
    binomial_tree_node_t *tree = NULL;
    int i;
    for (i = 0; i < list_count(bh->trees); i++) {
        binomial_tree_node_t *cur_tree = pick_value(bh->trees, i);
        if (HAS_PRECEDENCE(bh, cur_tree->key, cur_tree->klen, key, klen)) {
            if (tree) {
                if (HAS_PRECEDENCE(bh, tree->key, tree->klen, cur_tree->key, cur_tree->klen)) {
                    tree = cur_tree;
                }
            } else {
                tree = cur_tree;
            }
        }
    }

    binomial_tree_node_t *to_delete = tree;
    while(to_delete && bh->cbs->cmp(to_delete->key, to_delete->klen, key, klen) != 0)
    {
        binomial_tree_node_t *next_tree = NULL;
        for (i = 0; i < to_delete->num_children; i++) {
            binomial_tree_node_t *child = to_delete->children[i];
            
            if (HAS_PRECEDENCE(bh, child->key, child->klen, key, klen)) {
                if (next_tree) {
                    if (HAS_PRECEDENCE(bh, next_tree->key, next_tree->klen, child->key, child->klen)) {
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
        if (value)
            *value = to_delete->value;
        if (vlen)
            *vlen = to_delete->vlen;
        binomial_tree_node_destroy(to_delete);
        return 0;
    }
    return -1;
}

static binomial_tree_node_t *
__binheap_maxmin(binheap_t *bh, uint32_t *index, int maxmin)
{
    int i;
    binomial_tree_node_t *node = NULL;
    for (i = 0; i < list_count(bh->trees); i ++) {
        binomial_tree_node_t *curtree = pick_value(bh->trees, i);
        if (!node) {
            node = curtree;
            if (index)
                *index = i;
            continue;
        }
        int is_bigger = (bh->cbs->cmp(curtree->key, curtree->klen, node->key, node->klen) >= 0);
        if ((maxmin == 0 && is_bigger) || (maxmin != 0 && !is_bigger))
        {
            node = curtree;
            if (index)
                *index = i;
        }
    }

    return node;
}

int
binheap_maximum(binheap_t *bh, void **key, size_t *klen, void **value, size_t *vlen)
{
    binomial_tree_node_t *maxroot = __binheap_maxmin(bh, NULL, 0);
    if (!maxroot)
        return -1;
    if (bh->mode == BINHEAP_MODE_MAX) {
        if (key)
            *key = maxroot->key;
        if (klen)
            *klen = maxroot->klen;
        if (value)
            *value = maxroot->value;
        if (vlen)
            *vlen = maxroot->vlen;
        return 0;
    } else {
        while (maxroot->num_children) {
            int max_child_index = binomial_tree_node_find_max_child(maxroot);
            maxroot = maxroot->children[max_child_index];
        }
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
}

int
binheap_minimum(binheap_t *bh, void **key, size_t *klen, void **value, size_t *vlen)
{
    binomial_tree_node_t *minroot = __binheap_maxmin(bh, NULL, 1);
    if (!minroot)
        return -1;
    if (bh->mode == BINHEAP_MODE_MAX) {
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
    } else {
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

binomial_tree_node_t *
binheap_get_minimum(binheap_t *bh, uint32_t *minidx)
{
    binomial_tree_node_t *minroot = __binheap_maxmin(bh, minidx, 1);

    if (!minroot)
        return NULL;

    if (bh->mode != BINHEAP_MODE_MIN && minroot->num_children) {
        while (minroot->num_children) {
            int min_child_index = binomial_tree_node_find_min_child(minroot);
            minroot = minroot->children[min_child_index];
        }
    }
    return minroot;
}

binomial_tree_node_t *
binheap_get_maximum(binheap_t *bh, uint32_t *maxidx)
{
    binomial_tree_node_t *maxroot = __binheap_maxmin(bh, maxidx, 0);

    if (!maxroot)
        return NULL;

    if (bh->mode != BINHEAP_MODE_MAX && maxroot->num_children) {
        while (maxroot->num_children) {
            int max_child_index = binomial_tree_node_find_max_child(maxroot);
            maxroot = maxroot->children[max_child_index];
        }
    }
    return maxroot; 
}

int
binheap_delete_minimum(binheap_t *bh, void **value, size_t *vlen)
{
    uint32_t minidx = 0;
    binomial_tree_node_t *minroot = binheap_get_minimum(bh, &minidx);

    if (!minroot)
        return -1;

    if (bh->mode == BINHEAP_MODE_MIN) {
        (void)fetch_value(bh->trees, minidx);
        if (minroot->num_children) {
            int child_index = binomial_tree_node_find_min_child(minroot);
            insert_value(bh->trees, minroot->children[child_index], minidx);
        }
    } else if (!minroot->parent) {
        (void)fetch_value(bh->trees, minidx);
    }

    if (value)
        *value = minroot->value;
    if (vlen)
        *vlen = minroot->vlen;

    binomial_tree_node_destroy(minroot);

    return 0;
}

int
binheap_delete_maximum(binheap_t *bh, void **value, size_t *vlen)
{
    uint32_t maxidx = 0;
    binomial_tree_node_t *maxroot = binheap_get_maximum(bh, &maxidx);

    if (!maxroot)
        return -1;

    if (bh->mode == BINHEAP_MODE_MAX) {
        (void)fetch_value(bh->trees, maxidx);
        if (maxroot->num_children) {
            int child_index = binomial_tree_node_find_max_child(maxroot);
            insert_value(bh->trees, maxroot->children[child_index], maxidx);
        }
    } else if (!maxroot->parent) {
        (void)fetch_value(bh->trees, maxidx);
    }

    if (value)
        *value = maxroot->value;
    if (vlen)
        *vlen = maxroot->vlen;
    binomial_tree_node_destroy(maxroot);

    return 0;
}


uint32_t
binheap_count(binheap_t *bh)
{
    return bh->count;
}

// merge two heaps in a single iteration
binheap_t *binheap_merge(binheap_t *bh1, binheap_t *bh2)
{
    if (bh1->mode != bh2->mode) {
        // refuse to merge binomial heaps initialized with different operational modes
        // TODO - error message
        return NULL;
    }

    linked_list_t *new_list = create_list();
    set_free_value_callback(new_list, (free_value_callback_t)binomial_tree_node_destroy);

    binomial_tree_node_t *node1 = shift_value(bh1->trees);
    binomial_tree_node_t *node2 = shift_value(bh2->trees);
    binomial_tree_node_t *carry = NULL;

    while (node1 || node2 || carry) {

        if (carry) {
            // if we have a carry (merged tree from previous iteration)
            // lets check if either node1 or node2 is of the same order and
            // in case let's merge it before comparing node1 with node2 so 
            // we get rid of the carry as soon as possible
            binomial_tree_node_t *node = NULL;
            if (node1 && node1->num_children == carry->num_children) {
                node = node1;
            } else if (node2 && node2->num_children  == carry->num_children) {
                node = node2;
            } else {
                if (!node1 && !node2) {
                    // if we have the carry but there is neither node1 nor node2
                    // we can just add the carry to the list and forget about it
                    push_value(new_list, carry);
                    carry = NULL;
                    continue;
                }

                // if either node1 or node2 is of an higher order than the carry,
                // let's swap it so that we will always compare the lower order trees
                // among the three (node1, node2 and carry)
                if (node1 && node1->num_children > carry->num_children) {
                    binomial_tree_node_t *tmp = node1;
                    node1 = carry;
                    carry = tmp;
                } else if (node2 && node2->num_children > carry->num_children) {
                    binomial_tree_node_t *tmp = node2;
                    node2 = carry;
                    carry = tmp;
                }
            }

            if (node) {
                if (HAS_PRECEDENCE(bh1, node->key, node->klen, carry->key, carry->klen)) {
                    binomial_tree_merge(node, carry);
                } else {
                    binomial_tree_merge(carry, node);
                    if (node == node1)
                        node1 = carry;
                    else
                        node2 = carry;
                }
                carry = NULL;
            }
        }

        // we have already taken care of the carry here
        // so now if either node1 or node2 is missing 
        // we can just add the other to the list and go ahead
        if (node1 && !node2) {
            push_value(new_list, node1);
            node1 = shift_value(bh1->trees);
            continue;
        } else if (node2 && !node1) {
            push_value(new_list, node2);
            node2 = shift_value(bh2->trees);
            continue;
        } else if (carry && !node1 && !node2) {
            // XXX - this case should have already been handled earlier
            //       (we have a carry but neither node1 nor node2)
            push_value(new_list, carry);
            carry = NULL;
            continue;
        }

        
        int order1 = node1->num_children;
        int order2 = node2->num_children;

        // compare node1 and node2 and if they are of different orders
        // let's add the lower one to the list and go ahead
        if (order1 < order2) {
            push_value(new_list, node1);
            node1 = shift_value(bh1->trees);
            continue;
        } else if (order1 > order2) {
            push_value(new_list, node2);
            node2 = shift_value(bh2->trees);
            continue;
        }

        // if we are here node1 and node2 have the same order so they
        // need to be merged
        if (HAS_PRECEDENCE(bh1, node1->key, node1->klen, node2->key, node2->klen)) {
            binomial_tree_merge(node1, node2);
            if (carry) {
                if (bh1->cbs->cmp(node1->key, node1->klen, carry->key, carry->klen) >= 0) {
                    binomial_tree_merge(node1, carry);
                    carry = node1;
                } else {
                    binomial_tree_merge(carry, node1);
                }
            } else {
                carry = node1;
            }
        } else {
            binomial_tree_merge(node2, node1);
            if (carry) {
                if (HAS_PRECEDENCE(bh1, node2->key, node2->klen, carry->key, carry->klen)) {
                    binomial_tree_merge(node2, carry);
                    carry = node2;
                } else {
                    binomial_tree_merge(carry, node2);
                }
            } else {
                carry = node2;
            }
        }

        // the two trees (node1 and node2) have been merged and put into carry,
        // so let's get the next two nodes (if any) and go ahead
        node1 = shift_value(bh1->trees);
        node2 = shift_value(bh2->trees);
    }

    binheap_t *merged_heap = calloc(1, sizeof(binheap_t));
    merged_heap->mode = bh1->mode;
    merged_heap->trees = new_list;
    merged_heap->count = bh1->count + bh2->count;
    merged_heap->cbs = bh1->cbs;

    return merged_heap;
}

void
binheap_increase_maximum(binheap_t *bh, int incr)
{
    binomial_tree_node_t *maxitem = binheap_get_maximum(bh, NULL);

    if (!maxitem)
        return;

    binomial_tree_node_increase_key(maxitem, incr);
}

void
binheap_decrease_maximum(binheap_t *bh, int decr)
{
    binomial_tree_node_t *maxitem = binheap_get_maximum(bh, NULL);

    if (!maxitem)
        return;

    binomial_tree_node_increase_key(maxitem, -decr);
}


void
binheap_increase_minimum(binheap_t *bh, int incr)
{
    binomial_tree_node_t *minitem = binheap_get_minimum(bh, NULL);

    if (!minitem)
        return;

    binomial_tree_node_increase_key(minitem, incr);
}

void
binheap_decrease_minimum(binheap_t *bh, int decr)
{
    binomial_tree_node_t *minitem = binheap_get_minimum(bh, NULL);

    if (!minitem)
        return;

    binomial_tree_node_increase_key(minitem, -decr);
}


#define BINHEAP_CMP_KEYS_TYPE(__type, __k1, __k1s, __k2, __k2s) \
{ \
    if (__k1s < sizeof(__type) || __k2s < sizeof(__type) || __k1s != __k2s) \
        return __k1s - __k2s; \
    __type __k1i = *((__type *)__k1); \
    __type __k2i = *((__type *)__k2); \
    return __k1i - __k2i; \
}

#define BINHEAP_INCR_KEY_TYPE(__type, __k, __ks, __nk, __nks, __incr) \
{ \
    __type __ki = *((__type *)__k); \
    __ki += __incr; \
    if (__nks) \
        *(__nks) = __ks; \
    if (__nk) { \
        *__nk = malloc(__ks); \
        __type *__nkp = *__nk; \
        *__nkp = __ki; \
    } \
}

#define BINHEAP_DECR_KEY_TYPE(__type, __k, __ks, __nk, __nks, __decr) \
{ \
    __type __ki = *((__type *)__k); \
    __ki -= __decr; \
    if (__nks) \
        *(__nks) = __ks; \
    if (__nk) { \
        *__nk = malloc(__ks); \
        __type *__nkp = *__nk; \
        *__nkp = __ki; \
    } \
}

#define BINHEAP_KEY_CALLBACKS_TYPE(__type) \
static inline int \
binheap_cmp_keys_##__type(void *k1, size_t k1size, void *k2, size_t k2size) \
{ \
    BINHEAP_CMP_KEYS_TYPE(__type, k1, k1size, k2, k2size); \
} \
\
static inline void \
binheap_incr_key_##__type(void *k, size_t ksize, void **nk, size_t *nksize, int incr) \
{ \
    BINHEAP_INCR_KEY_TYPE(int16_t, k, ksize, nk, nksize, incr); \
} \
\
static inline void \
binheap_decr_key_##__type(void *k, size_t ksize, void **nk, size_t *nksize, int decr) \
{ \
    BINHEAP_DECR_KEY_TYPE(int16_t, k, ksize, nk, nksize, decr); \
} \
\
inline const binheap_callbacks_t * \
binheap_keys_callbacks_##__type() \
{ \
    static const binheap_callbacks_t __type##_cbs \
    = { \
          .cmp = binheap_cmp_keys_##__type \
        , .incr = binheap_incr_key_##__type \
        , .decr = binheap_decr_key_##__type \
    };\
    return &(__type##_cbs); \
}

BINHEAP_KEY_CALLBACKS_TYPE(int16_t)
BINHEAP_KEY_CALLBACKS_TYPE(int32_t)
BINHEAP_KEY_CALLBACKS_TYPE(int64_t)
BINHEAP_KEY_CALLBACKS_TYPE(uint16_t)
BINHEAP_KEY_CALLBACKS_TYPE(uint32_t)
BINHEAP_KEY_CALLBACKS_TYPE(uint64_t)
BINHEAP_KEY_CALLBACKS_TYPE(float)
BINHEAP_KEY_CALLBACKS_TYPE(double)

