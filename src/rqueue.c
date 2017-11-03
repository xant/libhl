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

#define RQUEUE_MIN_SIZE 3

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
    rqueue_page_t             *head;
    rqueue_page_t             *tail;
    rqueue_page_t             *commit;
    rqueue_page_t             *reader;
    rqueue_free_value_callback_t free_value_cb;
    size_t                     size;
    int                     mode;
    uint64_t                writes;
    uint64_t                reads;
    int read_sync;
    int write_sync;
    int num_writers;
} PACK_IF_NECESSARY;

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
    for (i = 0; i <= size; i++) { // NOTE : we create one page more than the requested size
        rqueue_page_t *page = calloc(1, sizeof(rqueue_page_t));
        if (!page) {
            free(rb);
            // TODO - free the pages allocated so far
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

    // close the ringbuffer
    rb->head->prev = rb->tail;
    rb->tail->next = rb->head;
    rb->tail->next = RQUEUE_FLAG_ON(rb->tail->next, RQUEUE_FLAG_HEAD);

    rb->tail = rb->head;

    rb->commit = rb->tail;

    // the reader page is out of the ringbuffer
    rb->reader = calloc(1, sizeof(rqueue_page_t));
    if (!rb->reader) {
        free(rb);
        return NULL;
    }
    rb->reader->prev = rb->head->prev;
    rb->reader->next = rb->head;
    return rb;
}

void rqueue_set_free_value_callback(rqueue_t *rb, rqueue_free_value_callback_t cb) {
    rb->free_value_cb = cb;
}

void rqueue_destroy(rqueue_t *rb) {
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
    int i;
    void *v = NULL;

    for (i = 0; i < RQUEUE_MAX_RETRIES; i++) {

        if (__builtin_expect(ATOMIC_CAS(rb->read_sync, 0, 1), 1)) {
            rqueue_page_t *head = ATOMIC_READ(rb->head);

            rqueue_page_t *commit = ATOMIC_READ(rb->commit);
            //rqueue_page_t *tail = ATOMIC_READ(rb->tail);
            rqueue_page_t *next = ATOMIC_READ(head->next);
            rqueue_page_t *old_next = ATOMIC_READ(rb->reader->next);

            if (rb->reader == commit || rqueue_isempty(rb))
            { // nothing to read
                ATOMIC_CAS(rb->read_sync, 1, 0);
                continue;
            }

            // first connect the reader page to the head->next page
            if (ATOMIC_CAS(rb->reader->next, old_next, RQUEUE_FLAG_ON(next, RQUEUE_FLAG_HEAD))) {
                rb->reader->prev = head->prev;

                // now connect head->prev->next to the reader page, replacing the head with the reader
                // in the ringbuffer
                if (ATOMIC_CAS(head->prev->next, RQUEUE_FLAG_ON(head, RQUEUE_FLAG_HEAD), rb->reader)) {
                    // now the head is out of the ringbuffer and we can update the pointer
                    ATOMIC_CAS(rb->head, head, next);

                    next->prev = rb->reader;
                    rb->reader = head; // the head is the new reader
                    v = ATOMIC_READ(rb->reader->value);
                    if (v == NULL) { 
                        ATOMIC_CAS(rb->read_sync, 1, 0);
                        continue;
                    }
                    ATOMIC_CAS(rb->reader->value, v, NULL);
                    ATOMIC_INCREMENT(rb->reads);
                    ATOMIC_CAS(rb->read_sync, 1, 0);
                    break;
                } else {
                    fprintf(stderr, "head swap failed\n");
                    ATOMIC_SET(rb->reader->next, old_next);
                    sched_yield();
                }
            } else {
                fprintf(stderr, "reader->next swap failed\n");
            }
            ATOMIC_CAS(rb->read_sync, 1, 0);
        }
    }
    return v;
}

static inline void
rqueue_update_value(rqueue_t *rb, rqueue_page_t *page, void *value) {
    void *old_value = ATOMIC_READ(page->value);
    ATOMIC_CAS(page->value, old_value, value);
    if (old_value && rb->free_value_cb)
        rb->free_value_cb(old_value);
}

int
rqueue_write(rqueue_t *rb, void *value) {
    int retries = 0;

    rqueue_page_t *temp_page = NULL;
    rqueue_page_t *next_page = NULL;
    rqueue_page_t *tail = NULL;
    rqueue_page_t *head = NULL;
    rqueue_page_t *commit;
    int count = 0;
    
    // TODO - get rid of this barrier
    while (!__builtin_expect(ATOMIC_CAS(rb->write_sync, 0, 1), 1))
        sched_yield();

    ATOMIC_INCREMENT(rb->num_writers);
    do {
        count++;
        temp_page = ATOMIC_READ(rb->tail);
        commit = ATOMIC_READ(rb->commit);
        next_page = RQUEUE_FLAG_OFF(ATOMIC_READ(temp_page->next), RQUEUE_FLAG_ALL);
        if (!next_page) {
            ATOMIC_DECREMENT(rb->num_writers);
            ATOMIC_CAS(rb->write_sync, 1, 0); 
            return -1;
        }
        head = ATOMIC_READ(rb->head);

        if (rb->mode == RQUEUE_MODE_BLOCKING && commit == temp_page && temp_page != head && next_page != head) {
            if (retries++ < RQUEUE_MAX_RETRIES) {
                ATOMIC_CAS(rb->tail, temp_page, next_page);
                ATOMIC_DECREMENT(rb->num_writers);
                ATOMIC_CAS(rb->write_sync, 1, 0);
                sched_yield();

                while (!__builtin_expect(ATOMIC_CAS(rb->write_sync, 0, 1), 1))
                    sched_yield();

                ATOMIC_INCREMENT(rb->num_writers);
                continue;
            } else {
                ATOMIC_DECREMENT(rb->num_writers);
                ATOMIC_CAS(rb->write_sync, 1, 0);
                //fprintf(stderr, "queue full: %s \n", rqueue_stats(rb));
                return -2;
            }
        }

       if (RQUEUE_CHECK_FLAG(ATOMIC_READ(temp_page->next), RQUEUE_FLAG_HEAD) || (next_page == head && !rqueue_isempty(rb))) {
           /* TODO - FIXME 
            if (rb->mode == RQUEUE_MODE_BLOCKING) {
               if (ATOMIC_CAS(rb->read_sync, 0, 1)) { // TODO - it shouldn't be necessary to synchronize with the reader
                   temp_page = ATOMIC_READ(rb->tail);
                   head = ATOMIC_READ(rb->head);
                   if (commit->next == temp_page && RQUEUE_FLAG_OFF(temp_page->next, RQUEUE_FLAG_ALL) == head) {
                       rqueue_update_value(rb, temp_page, value);
                       ATOMIC_SET(rb->commit, temp_page);
                       ATOMIC_INCREMENT(rb->writes);
                       ATOMIC_DECREMENT(rb->num_writers);
                       ATOMIC_CAS(rb->read_sync, 1, 0);
                       ATOMIC_CAS(rb->write_sync, 1, 0);
                       return 0;
                   } else {
                       ATOMIC_DECREMENT(rb->num_writers);
                       ATOMIC_CAS(rb->read_sync, 1, 0);
                       ATOMIC_CAS(rb->write_sync, 1, 0);
                       return -2;
                   }
                   ATOMIC_CAS(rb->read_sync, 1, 0);
               }
            } else if (rb->mode == RQUEUE_MODE_OVERWRITE && commit == temp_page) { */
            if (rb->mode == RQUEUE_MODE_OVERWRITE && ATOMIC_READ(commit->next) == temp_page) {  // DELETE ME WHEN FIXED
               if (ATOMIC_CAS(rb->read_sync, 0, 1)) { // TODO - it shouldn't be necessary to synchronize with the reader
                    // we need to advance the head if in overwrite mode ...otherwise we must stop
                    //fprintf(stderr, "Will advance head and overwrite old data\n");
                    rqueue_page_t *nextpp = RQUEUE_FLAG_OFF(ATOMIC_READ(head->next), RQUEUE_FLAG_ALL);
                    if (ATOMIC_CAS(head->next, nextpp, RQUEUE_FLAG_ON(nextpp, RQUEUE_FLAG_HEAD))) {
                        ATOMIC_CAS(temp_page->next, RQUEUE_FLAG_ON(head, RQUEUE_FLAG_HEAD), head);
                        ATOMIC_SET(rb->head, nextpp);
                    }
                   ATOMIC_CAS(rb->read_sync, 1, 0);
                   continue;
               }
            }
            if (retries++ < RQUEUE_MAX_RETRIES) {
                ATOMIC_DECREMENT(rb->num_writers);
                ATOMIC_CAS(rb->write_sync, 1, 0);
                sched_yield();

                while (!__builtin_expect(ATOMIC_CAS(rb->write_sync, 0, 1), 1))
                    sched_yield();

                ATOMIC_INCREMENT(rb->num_writers);
                continue;
            } else {
                ATOMIC_DECREMENT(rb->num_writers);
                ATOMIC_CAS(rb->write_sync, 1, 0);
                //fprintf(stderr, "queue full: %s \n", rqueue_stats(rb));
                return -2;
            }

        } 
       
        if (!RQUEUE_CHECK_FLAG(ATOMIC_READ(temp_page->next), RQUEUE_FLAG_HEAD)) {
            tail = ATOMIC_CAS_RETURN(rb->tail, temp_page, next_page);
        }
    } while (tail != temp_page && retries++ < RQUEUE_MAX_RETRIES);

    if (!tail || tail != temp_page) {
        ATOMIC_DECREMENT(rb->num_writers);
        ATOMIC_CAS(rb->write_sync, 1, 0);
        return -1;
    } 


    void *old_value = ATOMIC_READ(tail->value);
    ATOMIC_CAS(tail->value, old_value, value);
    if (old_value && rb->free_value_cb)
        rb->free_value_cb(old_value);


    ATOMIC_INCREMENT(rb->writes);

    if (ATOMIC_READ(rb->num_writers) == 1)
        ATOMIC_CAS(rb->commit, ATOMIC_READ(rb->commit), tail);
    ATOMIC_DECREMENT(rb->num_writers);
    ATOMIC_CAS(rb->write_sync, 1, 0);

    return 0;
}

uint64_t rqueue_write_count(rqueue_t *rb) {
    return ATOMIC_READ(rb->writes);
}

uint64_t rqueue_read_count(rqueue_t *rb) {
    return ATOMIC_READ(rb->reads);
}

void rqueue_set_mode(rqueue_t *rb, rqueue_mode_t mode) {
    rb->mode = mode;
}

rqueue_mode_t rqueue_mode(rqueue_t *rb) {
    return rb->mode;
}

char *rqueue_stats(rqueue_t *rb) {
    char *buf = malloc(1024);
    if (!buf)
        return NULL;

    snprintf(buf, 1024,
           "reader:      %p \n"
           "head:        %p \n"
           "tail:        %p \n"
           "tail_next:   %p \n"
           "commit:      %p \n"
           "commit_next: %p \n"
           "reads:       %"PRId64" \n"
           "writes:      %"PRId64" \n"
           "mode:        %s \n",
           ATOMIC_READ(rb->reader),
           ATOMIC_READ(rb->head),
           ATOMIC_READ(rb->tail),
           ATOMIC_READ(ATOMIC_READ(rb->tail)->next),
           ATOMIC_READ(rb->commit),
           ATOMIC_READ(ATOMIC_READ(rb->commit)->next),
           ATOMIC_READ(rb->reads),
           ATOMIC_READ(rb->writes),
           rb->mode == RQUEUE_MODE_BLOCKING ? "blocking" : "overwrite");

    return buf;
}

int rqueue_isempty(rqueue_t *rb)
{
    rqueue_page_t *head = ATOMIC_READ(rb->head);
    rqueue_page_t *commit = ATOMIC_READ(rb->commit);
    rqueue_page_t *tail = ATOMIC_READ(rb->tail);
    return ((rb->reader == commit || (head == tail && commit != tail) || ATOMIC_READ(rb->writes) == 0));
}

size_t rqueue_size(rqueue_t *rb)
{
    return rb->size;
}

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
