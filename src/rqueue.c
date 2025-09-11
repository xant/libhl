#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <sched.h>
#include "atomic_defs.h"
#include "rqueue.h"

#define RQUEUE_FLAG_HEAD   (0x01)
#define RQUEUE_FLAG_UPDATE (0x02)
#define RQUEUE_FLAG_ALL    (0x03)

#define RQUEUE_FLAG_ON(_addr, _flag) (rqueue_page_t *)(((intptr_t) (_addr) & -4) | (_flag))
#define RQUEUE_FLAG_OFF(_addr, _flag) (rqueue_page_t *)((intptr_t) (_addr) & ~(_flag))

#define RQUEUE_CHECK_FLAG(_addr, _flag) (((intptr_t) (_addr) & (_flag)) == (_flag))

#define RQUEUE_MAX_RETRIES 1000

#define RQUEUE_MIN_SIZE 2

#ifdef USE_PACKED_STRUCTURES
#define PACK_IF_NECESSARY __attribute__((packed))
#else
#define PACK_IF_NECESSARY
#endif

typedef struct _rqueue_page_s {
    void               *value;
    struct _rqueue_page_s *next;
    struct _rqueue_page_s *prev;
} PACK_IF_NECESSARY rqueue_page_t;

struct _rqueue_s {
    rqueue_page_t                *head;
    rqueue_page_t                *tail;
    rqueue_page_t                *commit;
    rqueue_page_t                *reader;
    rqueue_free_value_callback_t free_value_cb;
    size_t                       size;
    int                          mode;
    uint64_t                     writes;
    uint64_t                     reads;
    int                          read_sync;
    int                          write_sync;
    int                          num_writers;
    int                          head_swap_failed;
    int                          reader_next_swap_failed;
    int                          queue_full_counter;
    int                          overwrite_counter;
    int                          is_empty;
} PACK_IF_NECESSARY;

// Ensure pointer alignment for flag embedding
_Static_assert(sizeof(void*) >= 4, "Pointer size must be at least 4 bytes for flag embedding");
_Static_assert(_Alignof(rqueue_page_t) >= 4, "rqueue_page_t must be 4-byte aligned for flag embedding");

static inline void rqueue_destroy_page(rqueue_page_t *page, rqueue_free_value_callback_t free_value_cb) {
    if (page->value && free_value_cb)
        free_value_cb(page->value);
    free(page);
}

/*
static rqueue_page_t *rqueue_create_page() {
    rqueue_page_t *page = calloc(1, sizeof(rqueue_page_t));
    return page;
}

static void *rqueue_page_value(rqueue_page_t *page) {
    return page->value;
}
*/

rqueue_t *rqueue_create(size_t size, rqueue_mode_t mode) {
    size_t i;
    rqueue_t *rb = calloc(1, sizeof(rqueue_t));
    if (!rb)
        return NULL;
    rb->size = (size > RQUEUE_MIN_SIZE) ? size : RQUEUE_MIN_SIZE;
    rb->mode = mode;
    rb->is_empty = 1;
    for (i = 0; i <= rb->size; i++) { // NOTE : we create one page more than the requested size
        rqueue_page_t *page = calloc(1, sizeof(rqueue_page_t));
        if (!page) {
            // Free all previously allocated pages (ring not yet closed, so simple traversal)
            rqueue_page_t *current = rb->head;
            while (current) {
                rqueue_page_t *next = current->next;
                free(current);
                current = next;
            }
            free(rb);
            return NULL;
        }
        if (!rb->head) {
            rb->head = rb->tail = page;
        } else if (rb->tail) {
            rb->tail->next = page;
        }
        page->prev = rb->tail;
        rb->tail = page;
    }

    if (!rb->head || !rb->tail) {
        free(rb);
        return NULL;
    }

    // close the ringbuffer - safe to dereference since we just verified non-NULL above
    rb->head->prev = rb->tail;
    rb->tail->next = rb->head;
    ATOMIC_STORE_RELEASE(rb->tail->next, RQUEUE_FLAG_ON(rb->head, RQUEUE_FLAG_HEAD));

    rb->tail = rb->head;

    rb->commit = rb->tail;

    // the reader page is out of the ringbuffer
    rb->reader = calloc(1, sizeof(rqueue_page_t));
    if (!rb->reader) {
        // Free all ring buffer pages
        rqueue_page_t *page = rb->head;
        do {
            rqueue_page_t *next = RQUEUE_FLAG_OFF(page->next, RQUEUE_FLAG_ALL);
            free(page);
            page = next;
        } while (page != rb->head);
        free(rb);
        return NULL;
    }
    rb->reader->prev = rb->head->prev;
    rb->reader->next = rb->head;
    return rb;
}

void rqueue_set_free_value_callback(rqueue_t *rb, rqueue_free_value_callback_t cb) {
    if (rb == NULL) {
        // do nothing
        return;
    }
    rb->free_value_cb = cb;
}

void rqueue_destroy(rqueue_t *rb) {
    if (rb == NULL) {
        // do nothing
        return;
    }
    rqueue_page_t *page = rb->head;
    do {
        rqueue_page_t *p = page;
        page = RQUEUE_FLAG_OFF(p->next, RQUEUE_FLAG_ALL);
        rqueue_destroy_page(p, rb->free_value_cb);
    } while (__builtin_expect(page != rb->head, 1));

    // release the reader page as well
    rqueue_destroy_page(rb->reader, rb->free_value_cb);

    // and finally release the ringbuffer descriptor
    free(rb);
}

void *rqueue_read(rqueue_t *rb) {
    if (rb == NULL) {
        // do nothing
        return NULL;
    }

    int i;
    void *v = NULL;

    for (i = 0; i < RQUEUE_MAX_RETRIES; i++) {

        if (__builtin_expect(ATOMIC_CAS(rb->read_sync, 0, 1), 1)) {
            rqueue_page_t *head = ATOMIC_READ_ACQUIRE(rb->head);

            rqueue_page_t *commit = ATOMIC_READ_ACQUIRE(rb->commit);
            rqueue_page_t *next = ATOMIC_READ_ACQUIRE(head->next);
            rqueue_page_t *old_next = ATOMIC_READ_ACQUIRE(rb->reader->next);

            if (rb->reader == commit || ATOMIC_READ_RELAXED(rb->is_empty))
            { // nothing to read
                ATOMIC_CAS(rb->read_sync, 1, 0);
                continue;
            }

            // first connect the reader page to the head->next page
            if (ATOMIC_CAS(rb->reader->next, old_next, RQUEUE_FLAG_ON(next, RQUEUE_FLAG_HEAD))) {
                ATOMIC_STORE_RELEASE(rb->reader->prev, head->prev);

                // now connect head->prev->next to the reader page, replacing the head with the reader
                // in the ringbuffer
                if (ATOMIC_CAS(head->prev->next, RQUEUE_FLAG_ON(head, RQUEUE_FLAG_HEAD), rb->reader)) {
                    // now the head is out of the ringbuffer and we can update the pointer
                    ATOMIC_CAS(rb->head, head, next);

                    ATOMIC_STORE_RELEASE(next->prev, rb->reader);
                    ATOMIC_STORE_RELEASE(rb->reader, head); // the head is the new reader
                    v = ATOMIC_READ_ACQUIRE(rb->reader->value);
                    if (v == NULL) { 
                        ATOMIC_CAS(rb->read_sync, 1, 0);
                        continue;
                    }
                    ATOMIC_CAS(rb->reader->value, v, NULL);
                    ATOMIC_INCREMENT(rb->reads);
                    if (ATOMIC_READ_ACQUIRE(rb->head) == ATOMIC_READ_ACQUIRE(rb->tail)) {
                        ATOMIC_STORE_RELEASE(rb->is_empty, 1);
                    }
                    ATOMIC_CAS(rb->read_sync, 1, 0);
                    break;
                } else {
                    ATOMIC_INCREMENT(rb->head_swap_failed);
                    // Safe to use ATOMIC_SET since we hold read_sync lock - no other reader can modify rb->reader->next
                    ATOMIC_STORE_RELAXED(rb->reader->next, old_next);
                    sched_yield();
                }
            } else {
                ATOMIC_INCREMENT(rb->reader_next_swap_failed);
            }
            ATOMIC_CAS(rb->read_sync, 1, 0);
        }
    }
    return v;
}

static inline void
rqueue_update_value(rqueue_t *rb, rqueue_page_t *page, void *value) {
    // Only free the old value if we successfully replace it
    void *old_value;
    do {
        old_value = ATOMIC_READ_ACQUIRE(page->value);
    } while (!ATOMIC_CAS(page->value, old_value, value));
    
    if (old_value && rb->free_value_cb)
        rb->free_value_cb(old_value);
}

int
rqueue_write(rqueue_t *rb, void *value) {
    if (rb == NULL) {
        // do nothing
        return -1; // Invalid queue pointer
    }

    int retries = 0;

    rqueue_page_t *temp_page = NULL;
    rqueue_page_t *next_page = NULL;
    rqueue_page_t *tail = NULL;
    rqueue_page_t *head = NULL;
    rqueue_page_t *commit;
    
    // TODO - get rid of this barrier
    while (!__builtin_expect(ATOMIC_CAS(rb->write_sync, 0, 1), 1))
        sched_yield();

    ATOMIC_INCREMENT(rb->num_writers);  // Entry: increment once per thread
    do {
        temp_page = ATOMIC_READ_ACQUIRE(rb->tail);
        commit = ATOMIC_READ_ACQUIRE(rb->commit);
        next_page = RQUEUE_FLAG_OFF(ATOMIC_READ_ACQUIRE(temp_page->next), RQUEUE_FLAG_ALL);
        if (!next_page) {
            ATOMIC_DECREMENT(rb->num_writers);  // Exit path: decrement once
            ATOMIC_CAS(rb->write_sync, 1, 0); 
            return -1;
        }
        head = ATOMIC_READ_ACQUIRE(rb->head);

        if (rb->mode == RQUEUE_MODE_BLOCKING && commit == temp_page && temp_page != head && next_page != head) {
            if (retries++ < RQUEUE_MAX_RETRIES) {
                ATOMIC_CAS(rb->tail, temp_page, next_page);
                ATOMIC_DECREMENT(rb->num_writers);  // Retry: decrement before yield
                ATOMIC_CAS(rb->write_sync, 1, 0);
                sched_yield();

                while (!__builtin_expect(ATOMIC_CAS(rb->write_sync, 0, 1), 1))
                    sched_yield();

                ATOMIC_INCREMENT(rb->num_writers);  // Retry: increment after reacquire
                continue;
            } else {
                ATOMIC_DECREMENT(rb->num_writers);  // Exit path: decrement once
                ATOMIC_CAS(rb->write_sync, 1, 0);
                ATOMIC_INCREMENT(rb->queue_full_counter);
                return -2;
            }
        }

       if (RQUEUE_CHECK_FLAG(ATOMIC_READ_ACQUIRE(temp_page->next), RQUEUE_FLAG_HEAD) || (next_page == head && !ATOMIC_READ_RELAXED(rb->is_empty))) {
            if (rb->mode == RQUEUE_MODE_OVERWRITE && ATOMIC_READ_ACQUIRE(commit->next) == temp_page) {  // DELETE ME WHEN FIXED
               if (ATOMIC_CAS(rb->read_sync, 0, 1)) { // TODO - it shouldn't be necessary to synchronize with the reader
                    // we need to advance the head if in overwrite mode ...otherwise we must stop
                    ATOMIC_INCREMENT(rb->overwrite_counter);
                    rqueue_page_t *nextpp = RQUEUE_FLAG_OFF(ATOMIC_READ_ACQUIRE(head->next), RQUEUE_FLAG_ALL);
                    if (ATOMIC_CAS(head->next, nextpp, RQUEUE_FLAG_ON(nextpp, RQUEUE_FLAG_HEAD))) {
                        ATOMIC_CAS(temp_page->next, RQUEUE_FLAG_ON(head, RQUEUE_FLAG_HEAD), head);
                        // Safe to use ATOMIC_SET since we hold read_sync lock - no concurrent head modifications possible
                        ATOMIC_STORE_RELEASE(rb->head, nextpp);
                    }
                   ATOMIC_CAS(rb->read_sync, 1, 0);
                   continue;
               }
            }
            if (retries++ < RQUEUE_MAX_RETRIES) {
                ATOMIC_DECREMENT(rb->num_writers);  // Retry: decrement before yield
                ATOMIC_CAS(rb->write_sync, 1, 0);
                sched_yield();

                while (!__builtin_expect(ATOMIC_CAS(rb->write_sync, 0, 1), 1))
                    sched_yield();

                ATOMIC_INCREMENT(rb->num_writers);  // Retry: increment after reacquire
                continue;
            } else {
                ATOMIC_DECREMENT(rb->num_writers);  // Exit path: decrement once
                ATOMIC_CAS(rb->write_sync, 1, 0);
                ATOMIC_INCREMENT(rb->queue_full_counter);
                return -2;
            }

        } 
       
        if (!RQUEUE_CHECK_FLAG(ATOMIC_READ_ACQUIRE(temp_page->next), RQUEUE_FLAG_HEAD)) {
            tail = ATOMIC_CAS_RETURN(rb->tail, temp_page, next_page);
        }
    } while (tail != temp_page && retries++ < RQUEUE_MAX_RETRIES);

    if (!tail || tail != temp_page) {
        ATOMIC_DECREMENT(rb->num_writers);  // Exit path: decrement once
        ATOMIC_CAS(rb->write_sync, 1, 0);
        return -1;
    } 


    // Only free the old value if we successfully replace it
    void *old_value;
    do {
        old_value = ATOMIC_READ_ACQUIRE(tail->value);
    } while (!ATOMIC_CAS(tail->value, old_value, value));
    
    if (old_value && rb->free_value_cb)
        rb->free_value_cb(old_value);


    ATOMIC_INCREMENT(rb->writes);
    ATOMIC_STORE_RELEASE(rb->is_empty, 0);

    if (ATOMIC_READ_RELAXED(rb->num_writers) == 1)
        ATOMIC_CAS(rb->commit, ATOMIC_READ_ACQUIRE(rb->commit), tail);
    ATOMIC_DECREMENT(rb->num_writers);  // Success path: decrement once
    ATOMIC_CAS(rb->write_sync, 1, 0);

    return 0;
}

uint64_t rqueue_write_count(rqueue_t *rb) {
    if (rb == NULL) {
        return 0;
    }
    return ATOMIC_READ_RELAXED(rb->writes);
}

uint64_t rqueue_read_count(rqueue_t *rb) {
    if (rb == NULL) {
        return 0;
    }
    return ATOMIC_READ_RELAXED(rb->reads);
}

void rqueue_set_mode(rqueue_t *rb, rqueue_mode_t mode) {
    if (rb == NULL) {
        // do nothing
        return;
    }
    rb->mode = mode;
}

rqueue_mode_t rqueue_mode(rqueue_t *rb) {
    if (rb == NULL) {
        // do nothing
        return RQUEUE_MODE_INVALID;
    }
    return rb->mode;
}

// Only for debugging purposes
char *rqueue_stats(rqueue_t *rb) {
    if (!rb) {
        return "Invalid pointer";
    }

    // Take snapshot of key pointers
    rqueue_page_t *reader = ATOMIC_READ_RELAXED(rb->reader);
    rqueue_page_t *head = ATOMIC_READ_RELAXED(rb->head);
    rqueue_page_t *tail = ATOMIC_READ_RELAXED(rb->tail);
    rqueue_page_t *commit = ATOMIC_READ_RELAXED(rb->commit);
    rqueue_page_t *tail_next = tail ? ATOMIC_READ_RELAXED(tail->next) : NULL;
    rqueue_page_t *commit_next = commit ? ATOMIC_READ_RELAXED(commit->next) : NULL;

    const char *format = 
           "reader:                    %p \n"
           "head:                      %p \n"
           "tail:                      %p \n"
           "tail_next:                 %p \n"
           "commit:                    %p \n"
           "commit_next:               %p \n"
           "reads:                     %"PRId64" \n"
           "writes:                    %"PRId64" \n"
           "mode:                      %s \n"
           "is_empty:                  %s \n"
           "head_swap_failed:          %d \n"
           "reader_next_swap_failed:   %d \n"
           "queue_full_counter:        %d \n"
           "overwrite_counter:         %d \n";

#define STATS_ARGS \
           reader, \
           head, \
           tail, \
           tail_next, \
           commit, \
           commit_next, \
           ATOMIC_READ_RELAXED(rb->reads), \
           ATOMIC_READ_RELAXED(rb->writes), \
           rb->mode == RQUEUE_MODE_BLOCKING ? "blocking" : "overwrite", \
           ATOMIC_READ_RELAXED(rb->is_empty) ? "true" : "false", \
           ATOMIC_READ_RELAXED(rb->head_swap_failed), \
           ATOMIC_READ_RELAXED(rb->reader_next_swap_failed), \
           ATOMIC_READ_RELAXED(rb->queue_full_counter), \
           ATOMIC_READ_RELAXED(rb->overwrite_counter) \

    // First pass: calculate exact size needed
    int needed = snprintf(NULL, 0, format, STATS_ARGS);
    
    if (needed < 0)
        return NULL;
        
    char *buf = malloc(needed + 1);
    if (!buf)
        return NULL;

    snprintf(buf, needed + 1, format, STATS_ARGS);

#undef STATS_ARGS
    return buf;
}

size_t rqueue_size(rqueue_t *rb)
{
    if (!rb) {
        return 0;
    }
    return rb->size;
}

int rqueue_isempty(rqueue_t *rb) {
    if (!rb) {
        return -1;
    }
    return ATOMIC_READ_RELAXED(rb->is_empty);
}
// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
