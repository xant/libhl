#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "refcnt.h"
#include "atomic_defs.h"
#include "rqueue.h"

#define RQUEUE_MIN_SIZE 1<<8

#ifdef USE_PACKED_STRUCTURES
#define PACK_IF_NECESSARY __attribute__((packed))
#else
#define PACK_IF_NECESSARY
#endif

struct _refcnt_node_s {
    void *ptr;
    void *priv;
    uint32_t count;
    uint8_t updating;
} PACK_IF_NECESSARY;

struct _refcnt_s {
    refcnt_terminate_node_callback_t terminate_node_cb;
    refcnt_free_node_ptr_callback_t free_node_ptr_cb;
    rqueue_t *free_list;
    rqueue_t *node_pool;
    uint32_t gc_threshold;
};


/* Global variables */


refcnt_t *
refcnt_create(uint32_t gc_threshold,
              refcnt_terminate_node_callback_t terminate_node_cb,
              refcnt_free_node_ptr_callback_t free_node_ptr_cb)
{
    refcnt_t *refcnt = calloc(1, sizeof(refcnt_t));
    if (!refcnt)
        return NULL;
    refcnt->terminate_node_cb = terminate_node_cb;
    refcnt->free_node_ptr_cb = free_node_ptr_cb;
    refcnt->gc_threshold = gc_threshold;
    int rqueue_size = gc_threshold + gc_threshold/2;
    if (rqueue_size < RQUEUE_MIN_SIZE)
        rqueue_size = RQUEUE_MIN_SIZE;
    refcnt->free_list = rqueue_create(rqueue_size, RQUEUE_MODE_BLOCKING);
    // XXX - make the pool size configurable
    refcnt->node_pool = rqueue_create(1<<16, RQUEUE_MODE_BLOCKING);
    rqueue_set_free_value_callback(refcnt->node_pool, free);

    return refcnt;
}

static void
gc(refcnt_t *refcnt, int force)
{

    uint64_t limit = force ? 0 : refcnt->gc_threshold/2;

    while (rqueue_write_count(refcnt->free_list) - rqueue_read_count(refcnt->free_list) > limit)
    {
        refcnt_node_t *ref = rqueue_read(refcnt->free_list);
        if (!ref)
            break;

        if (refcnt->free_node_ptr_cb) {
            refcnt->free_node_ptr_cb(ATOMIC_READ(ref->ptr));
        }
        if (rqueue_write(refcnt->node_pool, ref) != 0)
            free(ref);
    }
}

void
refcnt_destroy(refcnt_t *refcnt)
{
    gc(refcnt, 1);
    rqueue_destroy(refcnt->free_list);
    rqueue_destroy(refcnt->node_pool);
    free(refcnt);
}

refcnt_node_t *
deref_link_internal(refcnt_t *refcnt __attribute__ ((unused)), refcnt_node_t **link, int skip_deleted)
{
    while (1) {
        refcnt_node_t *node = __sync_fetch_and_add(link, 0);
        if (node == REFCNT_MARK_ON(node)) {
            if (skip_deleted)
                return NULL;
            else
                node = REFCNT_MARK_OFF(node);
        }
        if (node && !(ATOMIC_READ(node->updating) == 1 &&
                      ATOMIC_READ(node->count) == 0))
        {
            ATOMIC_INCREMENT(node->count);
            return node;
        } else {
            // XXX - should never happen
            return NULL;
        }
    }
}



refcnt_node_t *
deref_link_d(refcnt_t *refcnt, refcnt_node_t **link)
{
    return deref_link_internal(refcnt, link, 0);
}

refcnt_node_t *
deref_link(refcnt_t *refcnt, refcnt_node_t **link)
{
    return deref_link_internal(refcnt, link, 1);
}

refcnt_node_t *
release_ref(refcnt_t *refcnt, refcnt_node_t *ref)
{
    int terminated = 0;

    if (!refcnt)
        return NULL;

    if (ATOMIC_READ(ref->count) > 0)
        ATOMIC_DECREMENT(ref->count);

    if (ATOMIC_CAS(ref->updating, 0, 1)) {
        if (ATOMIC_READ(ref->count) == 0) {
            if (refcnt->terminate_node_cb)
                refcnt->terminate_node_cb(ref, ref->priv);
            rqueue_write(refcnt->free_list, ref);
            terminated = 1;
        } else {
            ATOMIC_CAS(ref->updating, 1, 0);
        }
    }
    if (rqueue_write_count(refcnt->free_list) - rqueue_read_count(refcnt->free_list) > refcnt->gc_threshold)
        gc(refcnt, 0);

    return terminated ? NULL : ref;
}

int
compare_and_swap_ref(refcnt_t *refcnt __attribute__ ((unused)), refcnt_node_t **link, refcnt_node_t *old, refcnt_node_t *ref)
{
    if (__sync_bool_compare_and_swap(link, old, ref)) {
        if (ref != NULL) {
            ATOMIC_INCREMENT(ref->count);
        }
        if (old != NULL) {
            ATOMIC_DECREMENT(old->count);
        }
        return 1;
    }
    return 0;
}

void
store_ref(refcnt_t *refcnt, refcnt_node_t **link, refcnt_node_t *ref)
{
    refcnt_node_t *old = NULL;
    do {
        old = __sync_fetch_and_add(link, 0);
    } while (!__sync_bool_compare_and_swap(link, old, ref));

    if (ref != NULL)
        retain_ref(refcnt, ref);

    if (old != NULL)
        release_ref(refcnt, REFCNT_MARK_OFF(old));
}

refcnt_node_t *
retain_ref(refcnt_t *refcnt, refcnt_node_t *ref)
{
    ref = deref_link(refcnt, &ref);
    return ref;
}


refcnt_node_t *
new_node(refcnt_t *refcnt, void *ptr, void *priv)
{
    refcnt_node_t *node = rqueue_read(refcnt->node_pool);
    if (!node)
        node = calloc(1, sizeof(refcnt_node_t));

    if (!node) // calloc failed
        return NULL;

    node->ptr = ptr;
    node->priv = priv;
    node->count = 1;
    node->updating = 0;
    return node;
}

void *
get_node_ptr(refcnt_node_t *node)
{
    if (node)
        return ATOMIC_READ(node->ptr);
    return NULL;
}

int
get_node_refcount(refcnt_node_t *node)
{
    return ATOMIC_READ(node->count);
}

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
