#include "refcnt.h"
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <rbuf.h>
#include <stdio.h>

#define RBUF_MIN_SIZE 1<<8

#pragma pack(push, 4)
struct __refcnt_node {
    void *ptr;
    uint32_t count;
    int delete;
};

struct __refcnt {
    refcnt_terminate_node_callback_t terminate_node_cb;
    refcnt_free_node_ptr_callback_t free_node_ptr_cb;
    rbuf_t *free_list;
    uint32_t gc_threshold;
};
#pragma pack(pop)


/* Global variables */


refcnt_t *refcnt_create(uint32_t gc_threshold,
                        refcnt_terminate_node_callback_t terminate_node_cb,
                        refcnt_free_node_ptr_callback_t free_node_ptr_cb)
{
    refcnt_t *refcnt = calloc(1, sizeof(refcnt_t));
    refcnt->terminate_node_cb = terminate_node_cb;
    refcnt->free_node_ptr_cb = free_node_ptr_cb;
    refcnt->gc_threshold = gc_threshold;
    int rbuf_size = gc_threshold + gc_threshold/2;
    if (rbuf_size < RBUF_MIN_SIZE)
        rbuf_size = RBUF_MIN_SIZE;
    refcnt->free_list = rb_create(rbuf_size, RBUF_MODE_BLOCKING);
    return refcnt;
}

static void gc(refcnt_t *refcnt, int force) {

    int limit = force ? 0 : refcnt->gc_threshold/3;
    do {
        refcnt_node_t *ref = rb_read(refcnt->free_list);
        if (!ref)
            break;

        if (refcnt->free_node_ptr_cb) {
            refcnt->free_node_ptr_cb(ATOMIC_READ(ref->ptr));
        }
        free(ref);
    } while (rb_write_count(refcnt->free_list) - rb_read_count(refcnt->free_list) > limit);
}

void refcnt_destroy(refcnt_t *refcnt) {
    gc(refcnt, 1);
    rb_destroy(refcnt->free_list);
    free(refcnt);
}

refcnt_node_t *deref_link_internal(refcnt_t *refcnt, refcnt_node_t **link, int skip_deleted) {
    while (1) {
        refcnt_node_t *node = ATOMIC_READ(*link);
        if (node == REFCNT_MARK_ON(node)) {
            if (skip_deleted)
                return NULL;
            else
                node = REFCNT_MARK_OFF(node);
        }
        if (node) {
            ATOMIC_INCREMENT(node->count, 1);
            return node;
        }
    }
}



refcnt_node_t *deref_link_d(refcnt_t *refcnt, refcnt_node_t **link) {
    return deref_link_internal(refcnt, link, 0);
}

refcnt_node_t *deref_link(refcnt_t *refcnt, refcnt_node_t **link) {
    return deref_link_internal(refcnt, link, 1);
}

void release_ref(refcnt_t *refcnt, refcnt_node_t *ref) {
    if (ATOMIC_READ(ref->count) > 0)
        ATOMIC_DECREMENT(ref->count, 1);
    if (ATOMIC_CMPXCHG(ref->delete, 0, 1)) {
        if (ATOMIC_READ(ref->count) == 0) {
            if (refcnt->terminate_node_cb)
                refcnt->terminate_node_cb(ref, 0);
            rb_write(refcnt->free_list, ref);
        } else {
            ATOMIC_CMPXCHG(ref->delete, 1, 0);
        }
    }
    if (rb_write_count(refcnt->free_list) - rb_read_count(refcnt->free_list) > refcnt->gc_threshold)
        gc(refcnt, 0);
}

int compare_and_swap_ref(refcnt_t *refcnt, refcnt_node_t **link, refcnt_node_t *old, refcnt_node_t *ref) {
    if (refcnt) { } // suppress warnings

    if (__sync_bool_compare_and_swap(link, old, ref)) {
        if (ref != NULL) {
            ATOMIC_INCREMENT(ref->count, 1);
        }
        if (old != NULL) {
            ATOMIC_DECREMENT(old->count, 1);
        }
        return 1;
    }
    return 0;
}

void store_ref(refcnt_t *refcnt, refcnt_node_t **link, refcnt_node_t *ref) {
    if (refcnt) { } // suppress warnings

    refcnt_node_t *old = REFCNT_MARK_OFF(*link);
    ATOMIC_CMPXCHG(*link, old, ref);
    if (ref != NULL) {
        retain_ref(refcnt, ref);
    }
    if (old != NULL) {
        release_ref(refcnt, old);
    }
}

void retain_ref(refcnt_t *refcnt, refcnt_node_t *ref) {
    ref = deref_link(refcnt, &ref);
    if (ref) { } // suppress warnings
}

refcnt_node_t *new_node(refcnt_t *refcnt, void *ptr) {
    refcnt_node_t *node = calloc(1, sizeof(refcnt_node_t));
    node->ptr = ptr;
    ATOMIC_INCREMENT(node->count, 1);
    return node;
}

void *get_node_ptr(refcnt_node_t *node) {
    if (node)
        return ATOMIC_READ(node->ptr);
    return NULL;
}

