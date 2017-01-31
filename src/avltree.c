// AVL tree implementation

#include "avltree.h"
#include "bsd_queue.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define	MIN(a,b) (((a)<(b))?(a):(b))
#define	MAX(a,b) (((a)>(b))?(a):(b))

typedef struct _avlt_node_s {
    void *key;
    size_t klen;
    void *value;
    struct _avlt_node_s *left;
    struct _avlt_node_s *right;
    struct _avlt_node_s *parent;
    int hl;
    int hr;
    TAILQ_ENTRY(_avlt_node_s) list; // used by the walker
} avlt_node_t;

struct _avlt_s {
    int num_nodes;
    struct _avlt_node_s *root;
    libhl_cmp_callback_t cmp_keys_cb;
    avlt_free_value_callback_t free_value_cb;
};

static int
cmp_keys_default_cb(void *k1, size_t k1l, void *k2, size_t k2l)
{
    if (k1l != k2l) {
        if (k2l > k1l) {
            return memcmp(k1, k2, k1l) - (k2l - k1l);
        } else {
            return memcmp(k1, k2, k2l) + (k1l - k2l);
        }
    } else {
        return memcmp(k1, k2, k1l);
    }
}

avlt_t *
avlt_create(libhl_cmp_callback_t cmp_keys_cb,
            avlt_free_value_callback_t free_value_cb)
{
    avlt_t *tree = calloc(1, sizeof(avlt_t));
    tree->cmp_keys_cb = cmp_keys_cb;
    tree->free_value_cb = free_value_cb;
    return tree;
}

static inline void
avlt_rotate_left(avlt_node_t *node)
{
    avlt_node_t *right = node->right;
    if (!right)
        return;
    avlt_node_t *rl = right->left;
    avlt_node_t *parent = node->parent;

    right->left = node;
    node->parent = right;

    right->parent = parent;
    node->right = rl;
    node->hr = 0;
    if (rl) {
        rl->parent = node;
        node->hr = MAX(rl->hl, rl->hr) + 1;
    } 
    right->hl = MAX(node->hl, node->hr) + 1;
    if (parent) {
        if (parent->left == node) {
            parent->left = right;
            parent->hl = MAX(right->hl, right->hr) + 1;
        } else {
            parent->right = right;
            parent->hr = MAX(right->hl, right->hr) + 1;
        }
    }
}

static inline void
avlt_rotate_right(avlt_node_t *node)
{
    avlt_node_t *left = node->left;
    if (!left)
        return;
    avlt_node_t *lr = left->right;
    avlt_node_t *parent = node->parent;

    left->right = node;
    node->parent = left;

    left->parent = parent;
    node->left = lr;
    node->hl = 0;
    if (lr) {
        lr->parent = node;
        node->hl = MAX(lr->hl, lr->hr) + 1;
    }
    left->hr = MAX(node->hl, node->hr) + 1;
    if (parent) {
        if (parent->right == node) {
            parent->right = left;
            parent->hr = MAX(left->hl, left->hr) + 1;
        } else {
            parent->left = left;
            parent->hl = MAX(left->hl, left->hr) + 1;
        }
    }

}

static inline void
avlt_node_destroy(avlt_node_t *node, avlt_free_value_callback_t free_value_cb)
{
    free(node->key);
    if (node->value && free_value_cb)
        free_value_cb(node->value);
    free(node);
}

static inline avlt_node_t *
avlt_node_create(void *key, size_t klen, void *value)
{
    avlt_node_t *node = calloc(1, sizeof(avlt_node_t));
    node->key = malloc(klen);
    memcpy(node->key, key, klen);
    node->klen = klen;
    node->value = value;
    return node;
}

static inline void
avlt_balance(avlt_t *tree, avlt_node_t *node)
{
    while(node) {
        node->hl = node->left ? MAX(node->left->hl, node->left->hr) + 1 : 0;
        node->hr = node->right ? MAX(node->right->hl, node->right->hr) + 1 : 0;
        int balance_factor = node->hl - node->hr;
        if (balance_factor <= -2) {
            avlt_node_t *right = node->right;
            // check for right-left case
            if (right && (right->hl - right->hr) == 1)
                avlt_rotate_right(right);
            avlt_rotate_left(node);
            if (tree->root == node)
                tree->root = node->parent;
        } else if (balance_factor >= 2) {
            avlt_node_t *left = node->left;
            // check for left-right case
            if (left && (left->hl - left->hr) == -1)
                avlt_rotate_left(left);
            avlt_rotate_right(node);
            if (tree->root == node)
                tree->root = node->parent;
        } else {
            node = node->parent;
            continue;
        }

        balance_factor = node->hl - node->hr;
        if (balance_factor >= -1 && balance_factor <= 1)
            node = node->parent;
    }
}

int
avlt_add(avlt_t *tree, void *key, size_t klen, void *value)
{
    if (!tree->root) {
        tree->root = calloc(1, sizeof(avlt_node_t));
        tree->root->key = malloc(klen);
        memcpy(tree->root->key, key, klen);
        tree->root->klen = klen;
        tree->root->value = value;
        return 0;
    }
    avlt_node_t *cur = tree->root; 
    libhl_cmp_callback_t cmp = tree->cmp_keys_cb;

    for(;;) {
        if (cmp(key, klen, cur->key, cur->klen) < 0) {
            if (cur->left) {
                if (cmp(cur->left->key, cur->left->klen, key, klen) > 0) {
                    cur = cur->left;
                    continue;
                } else if (cmp(cur->left->key, cur->left->klen, key, klen) < 0 &&
                          (!cur->left->right || cmp(cur->left->right->key, cur->left->right->klen, key, klen) != 0))
                {
                    // swap
                    avlt_node_t *new = avlt_node_create(key, klen, value);
                    avlt_node_t *left = cur->left;
                    cur->left = new;
                    new->parent = left->parent;
                    new->left = left;
                    new->hl = left ? MAX(left->hl, left->hr) + 1 : 0;
                    left->parent = new;
                    if (left->right && cmp(left->right->key, left->right->klen, new->key, new->klen) > 0)
                    {
                        new->right = left->right;
                        new->right->parent = new;
                        left->right = NULL;
                        left->hr = 0;
                        new->hr = MAX(new->right->hl, new->right->hr) + 1;
                    }
                    if (new->left)
                        cur = new->left;
                    else if (new->right)
                        cur = new->right;
                    else
                        cur = new;
                } else {
                    return -1;
                }
            } else {
                // leaf
                cur->left = avlt_node_create(key, klen, value);
                cur->left->parent = cur;
                cur->hl++;
            }
            break;
        } else if (cmp(key, klen, cur->key, cur->klen) > 0) {
            if (cur->right) {
                if (cmp(cur->right->key, cur->right->klen, key, klen) < 0) {
                    cur = cur->right;
                    continue;
                } else if (cmp(cur->right->key, cur->right->klen, key, klen) > 0 &&
                          (!cur->right->left || cmp(cur->right->left->key, cur->right->left->klen, key, klen) != 0))
                {
                    // swap
                    avlt_node_t *new = avlt_node_create(key, klen, value);
                    avlt_node_t *right = cur->right;
                    cur->right = new;
                    new->parent = right->parent;
                    new->right = right;
                    new->hr = right ? MAX(right->hl, right->hr) + 1 : 0;
                    right->parent = new;
                    if (right->left && cmp(right->left->key, right->left->klen, new->key, new->klen) < 0)
                    {
                        new->left = right->left;
                        new->left->parent = new;
                        right->left = NULL;
                        right->hl = 0;
                        new->hl = MAX(new->left->hl, new->left->hr) + 1;
                    }
                    if (new->right)
                        cur = new->right;
                    else if (new->left)
                        cur = new->left;
                    else
                        cur = new;
                } else {
                    return -1;
                }
            } else {
                // leaf
                cur->right = avlt_node_create(key, klen, value);
                cur->right->parent = cur;
                cur->hr++;
            }
            break;
        } else {
            return -1;
        }
    }

    avlt_balance(tree, cur);
    return 0;
}

static inline avlt_node_t *
avlt_find_next(avlt_node_t *node)
{
    if (!node->right)
        return NULL;

    avlt_node_t *next = node->right;

    while (next->left)
        next = next->left;

    return next;
}

static inline avlt_node_t *
avlt_find_prev(avlt_node_t *node)
{
    if (!node->left)
        return NULL;

    avlt_node_t *prev = node->left;

    while (prev->right)
        prev = prev->right;

    return prev;
}

int
avlt_remove(avlt_t *tree, void *key, size_t klen, void **value)
{
    avlt_node_t *cur = tree->root;
    while(cur) {
        if (tree->cmp_keys_cb(cur->key, cur->klen, key, klen) < 0) {
            cur = cur->right;
            continue;
        } else if (tree->cmp_keys_cb(cur->key, cur->klen, key, klen) > 0)
        {
            cur = cur->left;
            continue;
        }

        if (cur->left && cur->right) {
            // 2 children case
            avlt_node_t *p = NULL;
            if (cur->hl > cur->hr)
                p = avlt_find_prev(cur);
            else
                p = avlt_find_next(cur);
            if (p) {
                void *ktmp = cur->key;
                size_t kltmp = cur->klen;
                void *vtmp = cur->value;

                cur->key = p->key;
                cur->klen = p->klen;
                cur->value = p->value;
                cur = p;

                p->key = ktmp;
                p->klen = kltmp;
                p->value = vtmp;
            } else {
                // TODO - Error messages
                return -1;
            }
        }

        avlt_node_t *to_free = cur;
        avlt_node_t *parent = cur->parent;
        if (!cur->left || !cur->right) {
            // none or at most 1 child case
            if (parent) {
                if (parent->left == cur)
                    parent->left = cur->left ? cur->left : cur->right;
                else
                    parent->right = (cur->right) ? cur->right : cur->left;

                if (cur->left) {
                    cur->left->parent = parent;
                } else if (cur->right) {
                    cur->right->parent = parent;
                }

                avlt_balance(tree, parent);
            } else {
                if (cur->left) {
                    cur->left->parent = NULL;
                    tree->root = cur->left;
                } else if (cur->right) {
                    cur->right->parent = NULL;
                    tree->root = cur->right;
                } else {
                    tree->root = NULL;
                }
            }
        }
        avlt_free_value_callback_t cb = NULL;

        if (value)
            *value = to_free->value;
        else if (to_free->value)
            cb = tree->free_value_cb;

        avlt_node_destroy(to_free, cb);
        break;
    }
    return 0;
}

int
avlt_walk(avlt_t *tree, avlt_walk_callback_t cb, void *priv)
{
    // iterative walker implementation
    int cnt = 0;

    if (!tree->root)
        return 0;

    TAILQ_HEAD(, _avlt_node_s) head;
    TAILQ_INIT(&head);

    avlt_node_t *cur = tree->root;
    TAILQ_INSERT_TAIL(&head, cur, list);

    while ((cur = TAILQ_FIRST(&head))) {
        TAILQ_REMOVE(&head, cur, list);
        int ret = cb(tree, cur->key, cur->klen, cur->value, priv);
        void *v = NULL;
        switch(ret) {
            case 0:
                return cnt;
            case -1:
                avlt_remove(tree, cur->key, cur->klen, &v);
                break;
            case -2:
                avlt_remove(tree, cur->key, cur->klen, &v);
                return cnt;
            case 1:
            default:
                break;
        }
        cnt++;
        if (cur->left)
            TAILQ_INSERT_TAIL(&head, cur->left, list);
        if (cur->right)
            TAILQ_INSERT_TAIL(&head, cur->right, list);
    }
    return cnt;
}

int
avt_walk_sorted_internal(avlt_t *tree,
                         avlt_node_t *node,
                         avlt_walk_callback_t cb,
                         void *priv)
{
    int cnt = 1;
    int ret = 0;

    if (node->left) {
        ret = avt_walk_sorted_internal(tree, node->left, cb, priv);
        if (ret == -1)
            return cnt + 1;
        else
            cnt += ret;
    }

    ret = cb(tree, node->key, node->klen, node->value, priv);
    void *v = NULL;
    switch(ret) {
        case 0:
            return -1;
        case -1:
            avlt_remove(tree, node->key, node->klen, &v);
            break;
        case -2:
            avlt_remove(tree, node->key, node->klen, &v);
            return -1;
        case 1:
        default:
            break;
    }

    if (node->right) {
        ret = avt_walk_sorted_internal(tree, node->right, cb, priv);
        if (ret == -1)
            return cnt + 1;
        else
            cnt += ret;
    }

    return cnt;
}

int
avlt_walk_sorted(avlt_t *tree, avlt_walk_callback_t cb, void *priv)
{
    int cnt = 0;
    avlt_node_t *root = tree->root;
    if (root)
        cnt = avt_walk_sorted_internal(tree, root, cb, priv);
    return cnt;
}

void
avlt_destroy(avlt_t *tree)
{
    while (tree->root) {
        avlt_remove(tree, tree->root->key, tree->root->klen, NULL);
    }
    free(tree);
}

#ifdef DEBUG_AVLT
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
static int
avltree_print_internal(avlt_node_t *node, int is_left, int offset, int depth, char s[20][255])
{
    char b[20];
    memset(b, 0, sizeof(b));

    if (!node) return 0;

    sprintf(b, "(%d [%d])", *((int *)node->value), node->hl - node->hr);
    int width = strlen(b);

    int left  = avltree_print_internal(node->left,  1, offset,                depth + 1, s);
    int right = avltree_print_internal(node->right, 0, offset + left + width, depth + 1, s);

    int i;

#ifdef DEBUG_AVLT_COMPACT
    for (i = 0; i < width; i++)
        s[depth][offset + left + i] = b[i];

    if (depth && is_left) {

        for (i = 0; i < width + right; i++)
            s[depth - 1][offset + left + width/2 + i] = '-';

        s[depth - 1][offset + left + width/2] = '.';

    } else if (depth && !is_left) {

        for (i = 0; i < left + width; i++)
            s[depth - 1][offset - width/2 + i] = '-';

        s[depth - 1][offset + left + width/2] = '.';
    }
#else
    for (i = 0; i < width; i++)
        s[2 * depth][offset + left + i] = b[i];

    if (depth && is_left) {

        for (i = 0; i < width + right; i++)
            s[2 * depth - 1][offset + left + width/2 + i] = '-';

        s[2 * depth - 1][offset + left + width/2] = '+';
        s[2 * depth - 1][offset + left + width + right + width/2] = '+';

    } else if (depth && !is_left) {

        for (i = 0; i < left + width; i++)
            s[2 * depth - 1][offset - width/2 + i] = '-';

        s[2 * depth - 1][offset + left + width/2] = '+';
        s[2 * depth - 1][offset - width/2 - 1] = '+';
    }
#endif

    return left + width + right;
}

void avlt_print(avlt_t *avlt)
{
    int i;
    char s[20][255];
    memset(s, 0, sizeof(s));

    struct winsize w;
    ioctl(0, TIOCGWINSZ, &w);

    char format[16];
    snprintf(format, sizeof(format), "%%%ds", w.ws_col);
    for (i = 0; i < 20; i++)
        sprintf(s[i], format, " ");

    avltree_print_internal(avlt->root, 0, 0, 0, s);

    for (i = 0; i < 20; i++)
        printf("%s\n", s[i]);
}
#endif

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
