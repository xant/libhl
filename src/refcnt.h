#ifndef __REFCNT_H__
#define __REFCNT_H__

#define ATOMIC_INCREMENT(__i, __cnt) __sync_fetch_and_add(&__i, __cnt);
#define ATOMIC_DECREMENT(__i, __cnt) __sync_fetch_and_sub(&__i, __cnt);
#define ATOMIC_READ(__p) __sync_fetch_and_add(&__p, 0)
#define ATOMIC_CMPXCHG(__p, __v1, __v2) __sync_bool_compare_and_swap(&__p, __v1, __v2)
#define ATOMIC_CMPXCHG_RETURN(__p, __v1, __v2) __sync_val_compare_and_swap(&__p, __v1, __v2)

#define REFCNT_MARK_ON(__addr) (refcnt_node_t *)((intptr_t)ATOMIC_READ(__addr) | 1)
#define REFCNT_MARK_OFF(__addr) (refcnt_node_t *)((intptr_t)ATOMIC_READ(__addr) & -2)
#define REFCNT_IS_MARKED(__addr) (((intptr_t)ATOMIC_READ(__addr) & 1) == 1)

typedef struct __refcnt refcnt_t;

typedef struct __refcnt_node refcnt_node_t;


/*
    TerminateNode makes sure that none of the node’s contained links have any claim on any other node. TerminateNode is called on a deleted node when there are no claims from any other node or thread to the node.2 When the argument concurrent is false BEWARE&CLEANUP guarantees that there cannot be any concurrent updates of the node, thereby allowing TerminateNode to use the cheaper StoreRef instead of CompareAndSwapRef to update the node’s links.
*/
typedef void (*refcnt_terminate_node_callback_t)(refcnt_node_t *node, int concurrent);

refcnt_t *refcnt_create(refcnt_terminate_node_callback_t terminate_node);
void refcnt_destroy(refcnt_t *refcnt);

refcnt_node_t *deref_link(refcnt_t *refcnt, refcnt_node_t **link);
refcnt_node_t *deref_link_d(refcnt_t *refcnt, refcnt_node_t **link);

void release_ref(refcnt_t *refcnt, refcnt_node_t *ref);
int compare_and_swap_ref(refcnt_t *refcnt, refcnt_node_t **link, refcnt_node_t *old, refcnt_node_t *ref);
void store_ref(refcnt_t *refcnt, refcnt_node_t **link, refcnt_node_t *ref);
refcnt_node_t *new_node(refcnt_t *refcnt, void *ptr);

void *get_node_ptr(refcnt_node_t *node);

void retain_ref(refcnt_t *refcnt, refcnt_node_t *ref);
void store_ref(refcnt_t *refcnt, refcnt_node_t **link, refcnt_node_t *ref);

#endif
