/**
 * @file refcnt.h
 *
 * @brief lock-free refcount framework
 *
 * Allows to manage refcounting on pointers accessed by multiple threads.
 * Facilities to atomically reference/dereference pointers are available
 * and are implemented using only the atomic builtins and without any
 * mutex or lock
 *
 */


#ifndef HL_REFCNT_H
#define HL_REFCNT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define REFCNT_MARK_ON(_addr) (refcnt_node_t *)((intptr_t)ATOMIC_READ(_addr) | 1)
#define REFCNT_MARK_OFF(_addr) (refcnt_node_t *)((intptr_t)ATOMIC_READ(_addr) & -2)
#define REFCNT_IS_MARKED(_addr) (((intptr_t)ATOMIC_READ(_addr) & 1) == 1)

#define REFCNT_RETAIN(_refcnt, _node, _type) ((_type) *)get_node_ptr(retain_ref((_refcnt), (_node)))
#define REFCNT_RELEASE(_refcnt, _node, _type) ((_type) *)get_node_ptr(release_ref((_refcnt), (_node)))

typedef struct _refcnt_s refcnt_t;

typedef struct _refcnt_node_s refcnt_node_t;

/**
 * @brief Callback called when a node is going to be terminated, which means
 *        that its refcount reached 0 and nobody is referencing it at the moment.
 *  @note The terminate_node callback makes sure that none of the node’s contained
 *         links have any claim on any other node.
 *         The callback is called on a deleted node when there are no claims
 *         from any other node or thread to the node.
*/
typedef void (*refcnt_terminate_node_callback_t)(refcnt_node_t *node, void *priv);

/**
 * @brief Callback called when a node is going to be released and the
 *        underlying ptr can be released as well
 * @note  If this callback is being called, there is no possibility for the
 *        node to be referenced anymore so it's now safe to release the
 *        underlying pointer.
 */
typedef void (*refcnt_free_node_ptr_callback_t)(void *ptr);

/**
 * @brief Create a new refcounted context
 * @param gc_threshold  :  The garbage-collector threshold, basically how many
 *                         unreferenced pointers can be accumulated before the
 *                         garbage collector triggers and starts releasing the
 *                         older ones
 * @param terminate_node_cb  : The terminate node callback
 * @param free_node_ptr_cb : The free node callback
 */
refcnt_t *refcnt_create(uint32_t gc_threshold,
                        refcnt_terminate_node_callback_t terminate_node_cb,
                        refcnt_free_node_ptr_callback_t free_node_ptr_cb);

/**
 * @brief Release all resources hold by the refcounted context pointed by refcnt
 * @param refcnt : A pointer to a valid refcounted context
 */
void refcnt_destroy(refcnt_t *refcnt);

/**
 * @brief Atomically creates a reference to a refcounted object
 * @param refcnt : A pointer to a valid refcounted context
 * @param link   : The address where the pointer to the node
 *                 to be referenced is stored
 * @return       : A pointer to the node which has been referenced.
 *                 Its refcount has been increased by 1
 * @note         : Objects marked for deletion (whose refcount is 0) are ignored
 *                 and their refcount untouched
 */
refcnt_node_t *deref_link(refcnt_t *refcnt, refcnt_node_t **link);

/**
 * @brief Atomically creates a reference to a refcounted object.
 *        If the object was marked for deletion, its deletion will
 *        be canceled and its refcount increased anyway
 * @param refcnt : A pointer to a valid refcounted context
 * @param link   : The address where the pointer to the node
 *                 to be referenced is stored
 * @return       : A pointer to the node which has been referenced.
 *                 Its refcount has been increased by 1
 */
refcnt_node_t *deref_link_d(refcnt_t *refcnt, refcnt_node_t **link);

/**
 * @brief Release a reference by decreasing its refcount
 * @param refcnt : A pointer to a valid refcounted context
 * @param ref    : The reference to release
 * @note         : The refcount of the provided reference is decreased by 1
 */
refcnt_node_t *release_ref(refcnt_t *refcnt, refcnt_node_t *ref);

/**
 * @brief Atomically compare a reference to a refcounted object with an existing pointer
 *        and swap it with a new pointer in case it matches
 */
int compare_and_swap_ref(refcnt_t *refcnt, refcnt_node_t **link, refcnt_node_t *old, refcnt_node_t *ref);


/**
 * @brief Create a new refcounted node which encapsulates a given pointer
 * @param refcnt : A pointer to a valid refcounted context
 * @param ptr    : The pointer to encapsulate in the refcounted object
 * @param priv   : An optional pointer to some private data which will be passed
 *                 as argument to both the terminate_node callback
 * @return       : An initialized refcounted object holding the given 'ptr'
 */
refcnt_node_t *new_node(refcnt_t *refcnt, void *ptr, void *priv);

/**
 * @brief Retrieve the pointer stored in the refcounted object
 * @param node : A pointer to a valid refcounted object
 * @return     : The pointer stored in the refcounted object
 */
void *get_node_ptr(refcnt_node_t *node);

/**
 * @brief Retain a refcounted object by increasing its refcount
 *
 * @param refcnt : A pointer to a valid refcounted context
 * @param ref    : The reference to retain
 * @return       : The pointer passed as ref argument (whose refcount
 *                 has been increased by 1) if success;\n NULL in case of error
 *                 (and the refcount has not been increased)
 * @note         : The refcount of the provided reference is increased by 1
 */
refcnt_node_t *retain_ref(refcnt_t *refcnt, refcnt_node_t *ref);

/**
 * @brief Atomically store a reference to a refcounted object in the memory
 *         pointed by the provided address, releasing a previous reference
 *         if any
 * @param refcnt : A pointer to a valid refcounted context
 * @param link   : The address where to store the pointer to the reference
 * @param ref    : The reference to store into *link
 * @note         : The refcount of the provided reference is increased by 1
 *               : If link points to an existing reference, its refcount will be
 *                 decreased by 1 before storing the new reference
 */
void store_ref(refcnt_t *refcnt, refcnt_node_t **link, refcnt_node_t *ref);

/**
 * @brief Returns the actual refcount for a given node
 * @param node   : A pointer to a valid refcounted object
 * @return       : The actual refcount of the node passed as argument
 * @note         : The returned is the actual value at the moment the call
 *                 is done, in a multithreaded environment where other threads
 *                 can retain/release the same objects this value can be
 *                 very volatile and the caller shouldn't rely on it unless
 *                 really knowing what he is doing.\n
 *                 This function has been introduced solely for debugging purposes
 */
int get_node_refcount(refcnt_node_t *node);

#ifdef __cplusplus
}
#endif

#endif

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
