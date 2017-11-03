/* linked queue management library - by xant */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sched.h>
#include "queue.h"
#include "refcnt.h"
#include "rqueue.h"
#include "atomic_defs.h"

#ifdef USE_PACKED_STRUCTURES
#define PACK_IF_NECESSARY __attribute__((packed))
#else
#define PACK_IF_NECESSARY
#endif

// size is 32 bytes on 32bit systems and 64 bytes on 64bit ones
typedef struct _queue_entry_s {
    refcnt_node_t *node;
    refcnt_node_t *prev;
    refcnt_node_t *next;
    void *value;
    queue_t *queue;
} PACK_IF_NECESSARY queue_entry_t;

// size is 32 bytes on 32bit systems and 64 bytes on 64bit ones
struct _queue_s {
    refcnt_t *refcnt;
    queue_entry_t *head;
    queue_entry_t *tail;
    size_t length;
    int free;
    queue_free_value_callback_t free_value_cb;
    rqueue_t *bpool;
    size_t bpool_size;
} PACK_IF_NECESSARY;

/*
 * Create a new queue_t. Allocates resources and returns
 * a queue_t opaque structure for later use
 */
queue_t *
queue_create()
{
    queue_t *q = (queue_t *)malloc(sizeof(queue_t));
    if(q) {
        queue_init(q);
        q->free = 1;
    } else {
        return NULL;
    }
    return q;
}

size_t
queue_set_bpool_size(queue_t *q, size_t size)
{
    rqueue_t *new_queue = rqueue_create(size, RQUEUE_MODE_BLOCKING);
    rqueue_set_free_value_callback(new_queue, free);

    size_t old_size = ATOMIC_READ(q->bpool_size);

    if (old_size == size) {
        rqueue_destroy(new_queue);
        return old_size;
    }

    rqueue_t *old_queue = ATOMIC_READ(q->bpool);
    if (ATOMIC_CAS(q->bpool, old_queue, new_queue)) {
        ATOMIC_CAS(q->bpool_size, old_size, size);
        if (old_queue)
            rqueue_destroy(old_queue);
    } else {
        rqueue_destroy(new_queue);
    }
    return old_size;
}

/*
static void
terminate_node_callback(refcnt_node_t *node, void *priv)
{
}
*/

/*
 * Create a new queue_entry_t structure. Allocates resources and returns
 * a pointer to the just created queue_entry_t opaque structure
 */
static inline queue_entry_t *
create_entry(queue_t *q)
{
    queue_entry_t *new_entry = (queue_entry_t *)calloc(1, sizeof(queue_entry_t));
    if (!new_entry)
        return NULL;

    new_entry->node = new_node(q->refcnt, new_entry, NULL);
    new_entry->queue = q;
    return new_entry;
}

queue_entry_t *dequeue_reusable_entry(queue_t *q)
{
    rqueue_t *pool = ATOMIC_READ(q->bpool);
    queue_entry_t *entry = pool ? rqueue_read(pool) : NULL;
    if (entry)
        entry->node = new_node(q->refcnt, entry, NULL);
    else
        entry = create_entry(q);

    return entry;
}

void queue_reusable_entry(queue_entry_t *entry)
{
    entry->node = NULL;
    rqueue_t *pool = ATOMIC_READ(entry->queue->bpool);
    if (!pool || rqueue_write(pool, entry) != 0)
        free(entry);
}


/*
 * Initialize a preallocated queue_t pointed by q
 * useful when using static queue handlers
 */
void
queue_init(queue_t *q)
{
    memset(q,  0, sizeof(queue_t));
    if (!q->refcnt)
        q->refcnt = refcnt_create(1<<10, NULL, (refcnt_free_node_ptr_callback_t)queue_reusable_entry);

    if (!q->head)
        q->head = create_entry(q);

    if (!q->tail)
        q->tail = create_entry(q);

    store_ref(q->refcnt, &q->head->next, q->tail->node);
    store_ref(q->refcnt, &q->tail->prev, q->head->node);
}

/*
 * Free resources allocated for a queue_entry_t structure.
 * If the entry is linked in a queue this routine will also unlink correctly
 * the entry from the queue.
 */
static inline void
destroy_entry(refcnt_t *refcnt, queue_entry_t *entry)
{
    if(entry)
        release_ref(refcnt, ATOMIC_READ(entry->node)); // this will also release the entry itself
}

/*
 * Destroy a queue_t. Free resources allocated for q
 */
void
queue_destroy(queue_t *q)
{
    if(q)
    {
        if (q->bpool) {
            rqueue_destroy(q->bpool);
            q->bpool = NULL;
        }
        queue_clear(q);
        store_ref(q->refcnt, &q->head->next, NULL);
        destroy_entry(q->refcnt, q->head);
        store_ref(q->refcnt, &q->tail->prev, NULL);
        destroy_entry(q->refcnt, q->tail);
        if (q->bpool)
            rqueue_destroy(q->bpool);
        refcnt_destroy(q->refcnt);
        if(q->free)
            free(q);
    }
}

/*
 * Clear a queue_t. Removes all entries in q
 * if values are associated to entries, resources for those will not be freed.
 * queue_clear() can be used safely with entry-based and tagged-based api,
 * otherwise you must really know what you are doing
 */
void
queue_clear(queue_t *q)
{
    void *v;
    /* Destroy all entries still in q */
    while((v = queue_pop_left(q)) != NULL)
    {
        /* if there is a tagged_value_t associated to the entry,
        * let's free memory also for it */
        if (q->free_value_cb)
            q->free_value_cb(v);
    }
}

/* Returns actual lenght of queue_t pointed by l */
size_t
queue_count(queue_t *q)
{
    size_t len;
    len = ATOMIC_READ(q->length);
    return len;
}

void
queue_set_free_value_callback(queue_t *q, queue_free_value_callback_t free_value_cb)
{
    q->free_value_cb = free_value_cb;
}

static inline void mark_prev(queue_entry_t *entry) {
    do {
        refcnt_node_t *link = ATOMIC_READ(entry->prev);
        if (link == REFCNT_MARK_ON(link))
            break;
        if (ATOMIC_CAS(entry->prev, link, REFCNT_MARK_ON(link)))
            break;
    } while (1);
}

/*
 * Insert a queue_entry_t at the beginning of a queue (or at the top if the stack)
 */
int
queue_push_left(queue_t *q, void *value)
{
    queue_entry_t *entry = create_entry(q);

    entry->value = value;

    queue_entry_t *prev = ATOMIC_READ(q->head);

    while (1) {
        queue_entry_t *next = get_node_ptr(deref_link(q->refcnt, &prev->next));
        if (!next) {
            continue;
        }

        store_ref(q->refcnt, &entry->prev, ATOMIC_READ(prev->node));
        store_ref(q->refcnt, &entry->next, ATOMIC_READ(next->node));

        if (ATOMIC_CAS(next->prev, REFCNT_MARK_OFF(entry->prev), entry->node)) {
            release_ref(q->refcnt, ATOMIC_READ(prev->node));
            retain_ref(q->refcnt, ATOMIC_READ(entry->node));
            while (!ATOMIC_CAS(prev->next, ATOMIC_READ(next->node), ATOMIC_READ(entry->node))) {
                release_ref(q->refcnt, next->node);
                sched_yield();
                queue_entry_t *next2 = get_node_ptr(deref_link(q->refcnt, &prev->next));
                next = next2;
                store_ref(q->refcnt, &entry->next, ATOMIC_READ(next->node));
            }
            release_ref(q->refcnt, ATOMIC_READ(next->node));
            retain_ref(q->refcnt, ATOMIC_READ(entry->node));
            release_ref(q->refcnt, ATOMIC_READ(next->node));
            break;
        }

        if (next)
            release_ref(q->refcnt, ATOMIC_READ(next->node));

        store_ref(q->refcnt, &entry->next, NULL);
        store_ref(q->refcnt, &entry->prev, NULL);
    }

    ATOMIC_INCREMENT(q->length);
    return 0;
}

/*
 * Pushs a queue_entry_t at the end of a queue
 */
int
queue_push_right(queue_t *q, void *value)
{
    queue_entry_t *entry = create_entry(q);

    entry->value = value;

    queue_entry_t *next = ATOMIC_READ(q->tail);

    while (1) {
        queue_entry_t *prev = get_node_ptr(deref_link(q->refcnt, &next->prev));
        if (!prev) {
            continue;
        }

        store_ref(q->refcnt, &entry->next, ATOMIC_READ(next->node));
        store_ref(q->refcnt, &entry->prev, ATOMIC_READ(prev->node));

        if (ATOMIC_CAS(prev->next, REFCNT_MARK_OFF(entry->next), entry->node)) {
            release_ref(q->refcnt, ATOMIC_READ(next->node));
            retain_ref(q->refcnt, ATOMIC_READ(entry->node));
            while (!ATOMIC_CAS(next->prev, ATOMIC_READ(prev->node), ATOMIC_READ(entry->node))) {
                release_ref(q->refcnt, prev->node);
                sched_yield();
                queue_entry_t *prev2 = get_node_ptr(deref_link(q->refcnt, &next->prev));
                prev = prev2;
                store_ref(q->refcnt, &entry->prev, ATOMIC_READ(prev->node));
            }
            release_ref(q->refcnt, ATOMIC_READ(prev->node));
            retain_ref(q->refcnt, ATOMIC_READ(entry->node));
            release_ref(q->refcnt, ATOMIC_READ(prev->node));
            break;
        }

        if (prev)
            release_ref(q->refcnt, ATOMIC_READ(prev->node));

        store_ref(q->refcnt, &entry->next, NULL);
        store_ref(q->refcnt, &entry->prev, NULL);
    }

    ATOMIC_INCREMENT(q->length);
    return 0;
}

/*
 * Retreive a queue_entry_t from the beginning of a queue (or top of the stack
 * if you are using the queue as a stack)
 */

void *
queue_pop_left(queue_t *q)
{
    void *v = NULL;
    queue_entry_t *entry = NULL;

    queue_entry_t *prev = ATOMIC_READ(q->head);
    if (!prev)
        return NULL;

    while(1) {
        refcnt_node_t *entry_node = deref_link(q->refcnt, &prev->next);
        entry = get_node_ptr(entry_node);

        if (!entry || entry == ATOMIC_READ(q->tail)) {
            if (entry_node)
                release_ref(q->refcnt, entry_node);
            return NULL;
        }

        if (ATOMIC_READ(entry->prev) != ATOMIC_READ(prev->node)) {
            release_ref(q->refcnt, ATOMIC_READ(entry->node));
            continue;
        }

        refcnt_node_t *link1 = ATOMIC_READ(entry->next);
        if (!REFCNT_MARK_OFF(link1)) {
            release_ref(q->refcnt, ATOMIC_READ(entry->node));
            continue;
        }

        if (ATOMIC_CAS(entry->next, REFCNT_MARK_OFF(link1), REFCNT_MARK_ON(link1))) {
            refcnt_node_t *link2;
            do {
                link2 = ATOMIC_READ(entry->prev);
            } while (!link2 || !ATOMIC_CAS(entry->prev, link2, REFCNT_MARK_ON(link2)));

            queue_entry_t *next = NULL;
            do {
                if (next)
                    release_ref(q->refcnt, ATOMIC_READ(next->node));
                next = get_node_ptr(deref_link_d(q->refcnt, &entry->next));
            } while(!next || !ATOMIC_CAS(next->prev, ATOMIC_READ(entry->node), ATOMIC_READ(prev->node)));

            release_ref(q->refcnt, ATOMIC_READ(entry->node));
            retain_ref(q->refcnt, ATOMIC_READ(prev->node));
            
            if (ATOMIC_CAS(prev->next, ATOMIC_READ(entry->node), ATOMIC_READ(next->node))) {
                release_ref(q->refcnt, ATOMIC_READ(entry->node));
                retain_ref(q->refcnt, ATOMIC_READ(next->node));
            }
    
            release_ref(q->refcnt, ATOMIC_READ(next->node));

            v = entry->value;
            release_ref(q->refcnt, ATOMIC_READ(entry->node));
            break;
        }
        release_ref(q->refcnt, ATOMIC_READ(entry->node));
    }
    if (entry) {
        ATOMIC_DECREMENT(q->length);
        store_ref(q->refcnt, &entry->next, NULL);
        store_ref(q->refcnt, &entry->prev, NULL);
        sched_yield();
        destroy_entry(q->refcnt, entry);
    }
    return v;
}

/*
 * Pops a queue_entry_t from the end of the queue (or bottom of the stack
 * if you are using the queue as a stack)
 */
void *
queue_pop_right(queue_t *q)
{
    void *v = NULL;
    queue_entry_t *entry = NULL;

    queue_entry_t *next = ATOMIC_READ(q->tail);
    if (!next)
        return NULL;

    while(1) {
        refcnt_node_t *entry_node = deref_link(q->refcnt, &next->prev);
        entry = get_node_ptr(entry_node);

        if (!entry || entry == ATOMIC_READ(q->head)) {
            if (entry_node)
                release_ref(q->refcnt, entry_node);
            return NULL;
        }

        if (ATOMIC_READ(entry->next) != ATOMIC_READ(next->node)) {
            release_ref(q->refcnt, ATOMIC_READ(entry->node));
            continue;
        }

        refcnt_node_t *link1 = ATOMIC_READ(entry->prev);
        if (!REFCNT_MARK_OFF(link1)) {
            release_ref(q->refcnt, ATOMIC_READ(entry->node));
            continue;
        }

        if (ATOMIC_CAS(entry->prev, REFCNT_MARK_OFF(link1), REFCNT_MARK_ON(link1))) {
            refcnt_node_t *link2;
            do {
                link2 = ATOMIC_READ(entry->next);
            } while (!ATOMIC_CAS(entry->next, link2, REFCNT_MARK_ON(link2)));

            queue_entry_t *prev = NULL;
            do {
                if (prev)
                    release_ref(q->refcnt, ATOMIC_READ(prev->node));
                prev = get_node_ptr(deref_link_d(q->refcnt, &entry->prev));
            } while(!prev || !ATOMIC_CAS(prev->next, ATOMIC_READ(entry->node), ATOMIC_READ(next->node)));


            release_ref(q->refcnt, ATOMIC_READ(entry->node));
            retain_ref(q->refcnt, ATOMIC_READ(next->node));

            if (ATOMIC_CAS(next->prev, ATOMIC_READ(entry->node), ATOMIC_READ(prev->node))) {
                release_ref(q->refcnt, ATOMIC_READ(entry->node));
                retain_ref(q->refcnt, ATOMIC_READ(prev->node));
            } 

            release_ref(q->refcnt, ATOMIC_READ(prev->node));
            
            v = entry->value;
            release_ref(q->refcnt, ATOMIC_READ(entry->node));
            break;
        }
        release_ref(q->refcnt, ATOMIC_READ(entry->node));
    }
    if (entry) {
        ATOMIC_DECREMENT(q->length);
        store_ref(q->refcnt, &entry->next, NULL);
        store_ref(q->refcnt, &entry->prev, NULL);
        sched_yield();
        destroy_entry(q->refcnt, entry);
    }
    return v;
}

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
