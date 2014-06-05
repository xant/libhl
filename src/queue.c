/* linked queue management library - by xant */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "queue.h"
#include "refcnt.h"

#define ATOMIC_READ REFCNT_ATOMIC_READ
#define ATOMIC_INCREMENT REFCNT_ATOMIC_INCREMENT
#define ATOMIC_DECREMENT REFCNT_ATOMIC_DECREMENT
#define ATOMIC_CMPXCHG REFCNT_ATOMIC_CMPXCHG

typedef struct __queue_entry {
    refcnt_t *refcnt;
    refcnt_node_t *node;
    queue_t *queue;
    refcnt_node_t *prev;
    refcnt_node_t *next;
    void *value;
    queue_free_value_callback_t free_value_cb;
} queue_entry_t;

struct __queue {
    refcnt_t *refcnt;
    queue_entry_t *head;
    queue_entry_t *tail;
    uint32_t length;
    int free;
    queue_free_value_callback_t free_value_cb;
};

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

static void
terminate_node_callback(refcnt_node_t *node)
{
    queue_entry_t *node_entry = get_node_ptr(node);
    store_ref(node_entry->refcnt, &node_entry->next, NULL);
    store_ref(node_entry->refcnt, &node_entry->prev, NULL);
}

/*
 * Create a new queue_entry_t structure. Allocates resources and returns
 * a pointer to the just created queue_entry_t opaque structure
 */
static inline queue_entry_t *
create_entry(refcnt_t *refcnt)
{
    queue_entry_t *new_entry = (queue_entry_t *)calloc(1, sizeof(queue_entry_t));
    new_entry->refcnt = refcnt;
    new_entry->node = new_node(refcnt, new_entry);
    return new_entry;
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
        q->refcnt = refcnt_create(1<<10, terminate_node_callback, free);
    if (!q->head) {
        q->head = create_entry(q->refcnt);
    }
    if (!q->tail) {
        q->tail = create_entry(q->refcnt);
    }

    store_ref(q->refcnt, &q->head->next, q->tail->node);
    store_ref(q->refcnt, &q->tail->prev, q->head->node);
}

/*
 * Free resources allocated for a queue_entry_t structure.
 * If the entry is linked in a queue this routine will also unlink correctly
 * the entry from the queue.
 */
static inline void
destroy_entry(queue_entry_t *entry)
{
    if(entry)
        release_ref(entry->refcnt, ATOMIC_READ(entry->node)); // this will also release the entry itself
}

/*
 * Destroy a queue_t. Free resources allocated for q
 */
void
queue_destroy(queue_t *q)
{
    if(q)
    {
        queue_clear(q);
        q->head->next = NULL;
        release_ref(q->refcnt, q->head->node);
        destroy_entry(q->head);
        q->tail->prev = NULL;
        release_ref(q->refcnt, q->tail->node);
        destroy_entry(q->tail);
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
uint32_t
queue_count(queue_t *q)
{
    uint32_t len;
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
        if (ATOMIC_CMPXCHG(entry->prev, link, REFCNT_MARK_ON(link)))
            break;
    } while (1);
}

static inline
queue_entry_t *help_insert(queue_entry_t *prev, queue_entry_t *entry)
{
    queue_entry_t *last = NULL;
    while (prev) {
        queue_entry_t *prev2 = get_node_ptr(deref_link(prev->refcnt, &prev->next));
        if (prev2 == NULL) {
            if (last != NULL) {
                mark_prev(prev);
                queue_entry_t *next2 = get_node_ptr(deref_link_d(prev->refcnt, &prev->next));
                if (next2) {
                    if (ATOMIC_CMPXCHG(last->next, ATOMIC_READ(prev->node), ATOMIC_READ(next2->node))) {
                        release_ref(prev->refcnt, ATOMIC_READ(prev->node));
                    } else {
                        release_ref(next2->refcnt, ATOMIC_READ(next2->node));
                    }
                }
                release_ref(prev->refcnt, ATOMIC_READ(prev->node));
                prev = last;
                last = NULL;
            } else {
                prev2 = get_node_ptr(deref_link_d(prev->refcnt, &prev->prev));
                release_ref(prev->refcnt, ATOMIC_READ(prev->node));
                prev = prev2;
            }
            continue;
        }
        refcnt_node_t *link1 = ATOMIC_READ(entry->prev);
        if (REFCNT_IS_MARKED(link1)) {
            release_ref(prev2->refcnt, ATOMIC_READ(prev2->node));
            prev = prev2;
            break;
        }
        if (prev2 != entry) {
            if (last != NULL)
                release_ref(last->refcnt, ATOMIC_READ(last->node));
            last = prev;
            prev = prev2;
            continue;
        }
        release_ref(prev2->refcnt, ATOMIC_READ(prev2->node));
        if (get_node_ptr(link1) == prev)
            break;

        if (ATOMIC_READ(prev->next) == ATOMIC_READ(entry->node) &&
            ATOMIC_CMPXCHG(entry->prev, link1, ATOMIC_READ(prev->node)))
        {
            retain_ref(prev->refcnt, ATOMIC_READ(prev->node));
            release_ref(entry->refcnt, link1);
            if (REFCNT_IS_MARKED(prev->prev))
                break;
        }

    }
    if (last != NULL)
        release_ref(last->refcnt, ATOMIC_READ(last->node));
    return prev;
}

static inline queue_entry_t *
help_delete(queue_entry_t *entry)
{
    mark_prev(entry);
    queue_entry_t *last = NULL;
    queue_entry_t *next = get_node_ptr(deref_link_d(entry->refcnt, &entry->next));
    queue_entry_t *prev = get_node_ptr(deref_link_d(entry->refcnt, &entry->prev));
    while (1) {
        if (prev == next)
            break;
        if (next && REFCNT_IS_MARKED(next->prev)) {
            mark_prev(next);
            queue_entry_t *next2 = get_node_ptr(deref_link_d(next->refcnt, &next->next));
            release_ref(next->refcnt, ATOMIC_READ(next->node));
            next = next2;
            continue;
        }
        queue_entry_t *prev2 = get_node_ptr(deref_link(prev->refcnt, &prev->next));
        if (prev2 == NULL) {
            if (last != NULL) {
                mark_prev(prev);
                queue_entry_t *next2 = get_node_ptr(deref_link_d(prev->refcnt, &prev->next));
                if (next2) {
                    if (ATOMIC_CMPXCHG(last->next, ATOMIC_READ(prev->node), ATOMIC_READ(next2->node))) {
                        release_ref(prev->refcnt, ATOMIC_READ(prev->node));
                    } else {
                        release_ref(next2->refcnt, ATOMIC_READ(next2->node));
                    }
                }
                release_ref(prev->refcnt, ATOMIC_READ(prev->node));
                prev = last;
                last = NULL;
            } else {
                release_ref(prev->refcnt, ATOMIC_READ(prev->node));
                prev = get_node_ptr(deref_link_d(prev->refcnt, &prev->prev));
            }
            continue;
        }
        if (prev2 != entry) {
            if (last != NULL)
                release_ref(last->refcnt, ATOMIC_READ(last->node));
            last = prev;
            prev = prev2;
            continue;
        }
        if (ATOMIC_CMPXCHG(prev->next, ATOMIC_READ(entry->node), ATOMIC_READ(next->node))) {
            refcnt_node_t *next_prev = ATOMIC_READ(next->prev);
            if (!ATOMIC_CMPXCHG(next->prev, next_prev, ATOMIC_READ(prev->node))) {
                next_prev = ATOMIC_READ(next->prev); 
            }
            if (REFCNT_MARK_OFF(next_prev) != ATOMIC_READ(prev2->node))
                release_ref(next->refcnt, next_prev);
            release_ref(prev2->refcnt, ATOMIC_READ(prev2->node));

            retain_ref(next->refcnt, ATOMIC_READ(next->node));
            retain_ref(prev->refcnt, prev->node);

            release_ref(entry->refcnt, ATOMIC_READ(entry->node));
            break;
        }
        release_ref(prev2->refcnt, ATOMIC_READ(prev2->node));
    }
    if (last != NULL)
        release_ref(last->refcnt, ATOMIC_READ(last->node));

    if (next)
        release_ref(next->refcnt, ATOMIC_READ(next->node));

    return prev;
}


/*
 * Insert a queue_entry_t at the beginning of a queue (or at the top if the stack)
 */
int
queue_push_left(queue_t *q, void *value)
{
    queue_entry_t *entry = create_entry(q->refcnt);
    if (!entry)
        return -1;

    entry->value = value;

    queue_entry_t *prev = ATOMIC_READ(q->head);

    retain_ref(q->refcnt, ATOMIC_READ(prev->node));

    while (1) {
        queue_entry_t *next = get_node_ptr(deref_link(prev->refcnt, &prev->next));
        if (!next)
            continue;

        store_ref(entry->refcnt, &entry->prev, ATOMIC_READ(prev->node));
        store_ref(entry->refcnt, &entry->next, ATOMIC_READ(next->node));

        if (ATOMIC_CMPXCHG(next->prev, REFCNT_MARK_OFF(entry->prev), entry->node)) {
            release_ref(prev->refcnt, ATOMIC_READ(prev->node));
            retain_ref(entry->refcnt, ATOMIC_READ(entry->node));
            while (!ATOMIC_CMPXCHG(prev->next, ATOMIC_READ(next->node), ATOMIC_READ(entry->node))) {
                queue_entry_t *next2 = get_node_ptr(deref_link(prev->refcnt, &prev->next));
                release_ref(next->refcnt, next->node);
                next = next2;
                store_ref(entry->refcnt, &entry->next, ATOMIC_READ(next->node));
            }
            release_ref(next->refcnt, ATOMIC_READ(next->node));
            retain_ref(entry->refcnt, ATOMIC_READ(entry->node));
            release_ref(next->refcnt, ATOMIC_READ(next->node));
            break;
        }

        if (next)
            release_ref(next->refcnt, ATOMIC_READ(next->node));
    }

    ATOMIC_INCREMENT(q->length, 1);
    release_ref(prev->refcnt, ATOMIC_READ(prev->node));
    return 0;
}

/*
 * Pushs a queue_entry_t at the end of a queue
 */
int
queue_push_right(queue_t *q, void *value)
{
    queue_entry_t *entry = create_entry(q->refcnt);
    if(!entry)
        return -1;

    entry->value = value;

    queue_entry_t *next = ATOMIC_READ(q->tail);

    retain_ref(next->refcnt, ATOMIC_READ(next->node));

    while (1) {
        queue_entry_t *prev = get_node_ptr(deref_link(next->refcnt, &next->prev));
        if (!prev)
            continue;

        store_ref(entry->refcnt, &entry->next, ATOMIC_READ(next->node));
        store_ref(entry->refcnt, &entry->prev, ATOMIC_READ(prev->node));

        if (ATOMIC_CMPXCHG(prev->next, REFCNT_MARK_OFF(entry->next), entry->node)) {
            release_ref(next->refcnt, ATOMIC_READ(next->node));
            retain_ref(entry->refcnt, ATOMIC_READ(entry->node));
            while (!ATOMIC_CMPXCHG(next->prev, ATOMIC_READ(prev->node), ATOMIC_READ(entry->node))) {
                queue_entry_t *prev2 = get_node_ptr(deref_link(next->refcnt, &next->prev));
                release_ref(prev->refcnt, prev->node);
                prev = prev2;
                store_ref(entry->refcnt, &entry->prev, ATOMIC_READ(prev->node));
            }
            release_ref(prev->refcnt, prev->node);
            retain_ref(entry->refcnt, ATOMIC_READ(entry->node));
            release_ref(prev->refcnt, ATOMIC_READ(prev->node));
            break;
        }

        if (prev)
            release_ref(prev->refcnt, ATOMIC_READ(prev->node));
    }
    ATOMIC_INCREMENT(q->length, 1);
    release_ref(next->refcnt, ATOMIC_READ(next->node));
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

    retain_ref(prev->refcnt, ATOMIC_READ(prev->node));
    while(1) {
        entry = get_node_ptr(deref_link(prev->refcnt, &prev->next));

        if (!entry || entry == ATOMIC_READ(q->tail)) {
            if (entry)
                release_ref(q->refcnt, ATOMIC_READ(entry->node));
            release_ref(q->refcnt, ATOMIC_READ(prev->node));
            return NULL;
        }

        if (ATOMIC_READ(entry->prev) != ATOMIC_READ(prev->node)) {
            release_ref(entry->refcnt, ATOMIC_READ(entry->node));
            continue;
        }

        refcnt_node_t *link1 = ATOMIC_READ(entry->next);

        if (ATOMIC_CMPXCHG(entry->next, REFCNT_MARK_OFF(link1), REFCNT_MARK_ON(link1))) {
            refcnt_node_t *link2;
            do {
                link2 = ATOMIC_READ(entry->prev);
            } while (!ATOMIC_CMPXCHG(entry->prev, link2, REFCNT_MARK_ON(link2)));

            queue_entry_t *next = NULL;
            do {
                if (next)
                    release_ref(next->refcnt, ATOMIC_READ(next->node));
                next = get_node_ptr(deref_link_d(entry->refcnt, &entry->next));
            } while(!ATOMIC_CMPXCHG(next->prev, ATOMIC_READ(entry->node), ATOMIC_READ(prev->node)));

            release_ref(entry->refcnt, entry->node);
            retain_ref(prev->refcnt, ATOMIC_READ(prev->node));
            
            if (ATOMIC_CMPXCHG(prev->next, ATOMIC_READ(entry->node), ATOMIC_READ(next->node))) {
                release_ref(entry->refcnt, ATOMIC_READ(entry->node));
                retain_ref(next->refcnt, ATOMIC_READ(next->node));
            }

            if (next)
                release_ref(next->refcnt, ATOMIC_READ(next->node));

            v = entry->value;
            break;
        }
        release_ref(entry->refcnt, ATOMIC_READ(entry->node));
    }
    release_ref(prev->refcnt, ATOMIC_READ(prev->node));
    if (entry) {
        ATOMIC_DECREMENT(q->length, 1);
        release_ref(entry->refcnt, ATOMIC_READ(entry->node));
        destroy_entry(entry);
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

    retain_ref(next->refcnt, ATOMIC_READ(next->node));
    while(1) {
        entry = get_node_ptr(deref_link(next->refcnt, &next->prev));

        if (!entry || entry == ATOMIC_READ(q->head)) {
            if (entry)
                release_ref(q->refcnt, ATOMIC_READ(entry->node));
            release_ref(q->refcnt, ATOMIC_READ(next->node));
            return NULL;
        }

        if (ATOMIC_READ(entry->next) != ATOMIC_READ(next->node)) {
            release_ref(entry->refcnt, ATOMIC_READ(entry->node));
            continue;
        }

        refcnt_node_t *link1 = ATOMIC_READ(entry->prev);

        if (ATOMIC_CMPXCHG(entry->prev, REFCNT_MARK_OFF(link1), REFCNT_MARK_ON(link1))) {
            refcnt_node_t *link2;
            do {
                link2 = ATOMIC_READ(entry->next);
            } while (!ATOMIC_CMPXCHG(entry->next, link2, REFCNT_MARK_ON(link2)));

            queue_entry_t *prev = NULL;
            do {
                if (prev)
                    release_ref(prev->refcnt, ATOMIC_READ(prev->node));
                prev = get_node_ptr(deref_link_d(entry->refcnt, &entry->prev));
            } while(!ATOMIC_CMPXCHG(prev->next, ATOMIC_READ(entry->node), ATOMIC_READ(next->node)));
            release_ref(entry->refcnt, entry->node);
            retain_ref(next->refcnt, ATOMIC_READ(next->node));

            if (ATOMIC_CMPXCHG(next->prev, ATOMIC_READ(entry->node), ATOMIC_READ(prev->node))) {
                release_ref(entry->refcnt, ATOMIC_READ(entry->node));
                retain_ref(prev->refcnt, ATOMIC_READ(prev->node));
            }

            if (prev)
                release_ref(prev->refcnt, ATOMIC_READ(prev->node));
            
            v = entry->value;
            break;
        }
        release_ref(entry->refcnt, ATOMIC_READ(entry->node));
    }
    release_ref(next->refcnt, ATOMIC_READ(next->node));
    if (entry) {
        ATOMIC_DECREMENT(q->length, 1);
        release_ref(entry->refcnt, ATOMIC_READ(entry->node));
        destroy_entry(entry);
    }
    return v;
}


