#include "refcnt.h"
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <rbuf.h>
#include <stdio.h>

#define REFCNT_GC_THRESHOLD 32767

#pragma pack(push, 4)
struct __refcnt_node {
    void *ptr;
    uint32_t count;
    int delete;
};

struct __refcnt {
    refcnt_terminate_node_callback_t terminate_node_cb;
    rbuf_t *free_list;
};
#pragma pack(pop)


/* Global variables */


refcnt_t *refcnt_create(refcnt_terminate_node_callback_t terminate_node) {
    refcnt_t *refcnt = calloc(1, sizeof(refcnt_t));
    refcnt->terminate_node_cb = terminate_node;
    refcnt->free_list = rb_create(REFCNT_GC_THRESHOLD + REFCNT_GC_THRESHOLD/3, RBUF_MODE_BLOCKING);
    return refcnt;
}

static void gc(refcnt_t *refcnt) {
    //printf("GARBAGE COLLECTING\n");
    do {
        refcnt_node_t *ref = rb_read(refcnt->free_list);
        if (!ref)
            break;

        free(ATOMIC_READ(ref->ptr));
        free(ref);
    } while (1);
}

void refcnt_destroy(refcnt_t *refcnt) {
    gc(refcnt);
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
    ATOMIC_DECREMENT(ref->count, 1);
    if (ATOMIC_CMPXCHG(ref->delete, 0, 1)) {
        if (ATOMIC_READ(ref->count) == 0) {
            refcnt->terminate_node_cb(ref, 0);
            rb_write(refcnt->free_list, ref);
        } else {
            ATOMIC_CMPXCHG(ref->delete, 1, 0);
        }
    }
    if (rb_write_count(refcnt->free_list) - rb_read_count(refcnt->free_list) > REFCNT_GC_THRESHOLD)
        gc(refcnt);
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

