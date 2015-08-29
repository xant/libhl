#include <stdlib.h>
#include <rqueue.h>
#include <stdio.h>
#include <inttypes.h>
#include "atomic_defs.h"

#define RQUEUE_FLAG_HEAD   (0x01)
#define RQUEUE_FLAG_UPDATE (0x02)
#define RQUEUE_FLAG_ALL    (0x03)

#define RQUEUE_FLAG_ON(_addr, _flag) (rqueue_page_t *)(((intptr_t) (_addr) & -4) | (_flag))
#define RQUEUE_FLAG_OFF(_addr, _flag) (rqueue_page_t *)((intptr_t) (_addr) & ~(_flag))

#define RQUEUE_CHECK_FLAG(_addr, _flag) (((intptr_t) (_addr) & (_flag)) == (_flag))

#define RQUEUE_MAX_RETRIES 1000

#define RQUEUE_MIN_SIZE 3

#pragma pack(push, 4)
typedef struct _rqueue_page_s {
    void               *value;
    struct _rqueue_page_s *next;
    struct _rqueue_page_s *prev;
} rqueue_page_t;

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
    int num_writers;
};
#pragma pack(pop)

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
    for (i = 0; i < size; i++) {
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
            rqueue_page_t *tail = ATOMIC_READ(rb->tail);
            rqueue_page_t *next = ATOMIC_READ(head->next);
            rqueue_page_t *old_next = ATOMIC_READ(rb->reader->next);

            if (rb->reader == commit || (head == tail && commit != tail) || ATOMIC_READ(rb->writes) == 0)
            { // nothing to read
                ATOMIC_CAS(rb->read_sync, 1, 0);
                break;
            }

            if (ATOMIC_CAS(rb->reader->next, old_next, RQUEUE_FLAG_ON(next, RQUEUE_FLAG_HEAD))) {
                rb->reader->prev = head->prev;

                if (ATOMIC_CAS(head->prev->next, RQUEUE_FLAG_ON(head, RQUEUE_FLAG_HEAD), rb->reader)) {
                    ATOMIC_CAS(rb->head, head, next);
                    next->prev = rb->reader;
                    rb->reader = head;
                    /*
                    rb->reader->next = next;
                    rb->reader->prev = next->prev;
                    */
                    v = ATOMIC_READ(rb->reader->value);
                    ATOMIC_CAS(rb->reader->value, v, NULL);
                    ATOMIC_INCREMENT(rb->reads);
                    ATOMIC_CAS(rb->read_sync, 1, 0);
                    break;
                } else {
                    fprintf(stderr, "head swap failed\n");
                }
            } else {
                fprintf(stderr, "reader->next swap failed\n");
            }
            ATOMIC_CAS(rb->read_sync, 1, 0);
        }
    }
    return v;
}

int rqueue_write(rqueue_t *rb, void *value) {
    int retries = 0;
    int did_update = 0;
    int did_move_head = 0;

    rqueue_page_t *temp_page = NULL;
    rqueue_page_t *next_page = NULL;
    rqueue_page_t *tail = NULL;
    rqueue_page_t *head = NULL;
    rqueue_page_t *commit;
    ATOMIC_INCREMENT(rb->num_writers);
    do {
        temp_page = ATOMIC_READ(rb->tail);
        commit = ATOMIC_READ(rb->commit);
        next_page = RQUEUE_FLAG_OFF(ATOMIC_READ(temp_page->next), RQUEUE_FLAG_ALL);
        head = ATOMIC_READ(rb->head);
        if (rb->mode == RQUEUE_MODE_BLOCKING) {
            if (temp_page == commit && next_page == head) {
                if (ATOMIC_READ(rb->writes) - ATOMIC_READ(rb->reads) != 0) {
                    //fprintf(stderr, "No buffer space\n");
                    if (ATOMIC_READ(rb->num_writers) == 1)
                        ATOMIC_CAS(rb->commit, ATOMIC_READ(rb->commit), ATOMIC_READ(rb->tail));
                    ATOMIC_DECREMENT(rb->num_writers);
                    return -2;
                }
            } else if (next_page == head) {
                if (ATOMIC_READ(rb->num_writers) == 1) {
                    tail = temp_page;
                    break;
                } else {
                    if (ATOMIC_READ(rb->num_writers) == 1)
                        ATOMIC_CAS(rb->commit, ATOMIC_READ(rb->commit), ATOMIC_READ(rb->tail));
                    ATOMIC_DECREMENT(rb->num_writers);
                    return -2;
                }
            }
        }
        tail = ATOMIC_CAS_RETURN(rb->tail, temp_page, next_page);
    } while (tail != temp_page && !(RQUEUE_CHECK_FLAG(ATOMIC_READ(tail->next), RQUEUE_FLAG_UPDATE)) && retries++ < RQUEUE_MAX_RETRIES);

    if (!tail) {
        if (ATOMIC_READ(rb->num_writers) == 1)
            ATOMIC_CAS(rb->commit, ATOMIC_READ(rb->commit), ATOMIC_READ(rb->tail));
        ATOMIC_DECREMENT(rb->num_writers);
        return -1;
    } 

    rqueue_page_t *nextp = RQUEUE_FLAG_OFF(ATOMIC_READ(tail->next), RQUEUE_FLAG_ALL);

    if (ATOMIC_CAS(tail->next, RQUEUE_FLAG_ON(nextp, RQUEUE_FLAG_HEAD), RQUEUE_FLAG_ON(nextp, RQUEUE_FLAG_UPDATE))) {
        did_update = 1;
        //fprintf(stderr, "Did update head pointer\n");
        if (rb->mode == RQUEUE_MODE_OVERWRITE) {
            // we need to advance the head if in overwrite mode ...otherwise we must stop
            //fprintf(stderr, "Will advance head and overwrite old data\n");
            rqueue_page_t *nextpp = RQUEUE_FLAG_OFF(ATOMIC_READ(nextp->next), RQUEUE_FLAG_ALL);
            if (ATOMIC_CAS(nextp->next, nextpp, RQUEUE_FLAG_ON(nextpp, RQUEUE_FLAG_HEAD))) {
                if (ATOMIC_READ(rb->tail) != next_page) {
                    ATOMIC_CAS(nextp->next, RQUEUE_FLAG_ON(nextpp, RQUEUE_FLAG_HEAD), nextpp);
                } else {
                    ATOMIC_CAS(rb->head, head, nextpp);
                    did_move_head = 1;
                }
            }
        }
    }

    void *old_value = ATOMIC_READ(tail->value);
    ATOMIC_CAS(tail->value, old_value, value);
    if (old_value && rb->free_value_cb)
        rb->free_value_cb(old_value);



    if (did_update) {
        //fprintf(stderr, "Try restoring head pointer\n");

        ATOMIC_CAS(tail->next,
                       RQUEUE_FLAG_ON(nextp, RQUEUE_FLAG_UPDATE),
                       did_move_head
                       ? RQUEUE_FLAG_OFF(nextp, RQUEUE_FLAG_ALL)
                       : RQUEUE_FLAG_ON(nextp, RQUEUE_FLAG_HEAD));

        //fprintf(stderr, "restored head pointer\n");
    }

    ATOMIC_INCREMENT(rb->writes);
    if (ATOMIC_READ(rb->num_writers) == 1)
        ATOMIC_CAS(rb->commit, ATOMIC_READ(rb->commit), tail);
    ATOMIC_DECREMENT(rb->num_writers);
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
           "commit:      %p \n"
           "commit_next: %p \n"
           "reads:       %"PRId64" \n"
           "writes:      %"PRId64" \n"
           "mode:        %s \n",
           ATOMIC_READ(rb->reader),
           ATOMIC_READ(rb->head),
           ATOMIC_READ(rb->tail),
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
