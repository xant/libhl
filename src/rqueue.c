#include <stdlib.h>
#include <rqueue.h>
#include <stdio.h>

#define RQUEUE_FLAG_HEAD   (0x01)
#define RQUEUE_FLAG_UPDATE (0x02)
#define RQUEUE_FLAG_ALL    (0x03)

#define RQUEUE_FLAG_ON(__addr, __flag) (rqueue_page_t *)(((intptr_t)__addr & -4) | __flag)
#define RQUEUE_FLAG_OFF(__addr, __flag) (rqueue_page_t *)((intptr_t)__addr & ~__flag)

#define RQUEUE_CHECK_FLAG(__addr, __flag) (((intptr_t)__addr & __flag) == __flag)

#define RQUEUE_MAX_RETRIES 500

#define ATOMIC_INCREMENT(__i, __cnt) __sync_fetch_and_add(&__i, __cnt);
#define ATOMIC_DECREMENT(__i, __cnt) __sync_fetch_and_sub(&__i, __cnt);
#define ATOMIC_READ(__p) __sync_fetch_and_add(&__p, 0)
#define ATOMIC_CMPXCHG(__p, __v1, __v2) __sync_bool_compare_and_swap(&__p, __v1, __v2)
#define ATOMIC_CMPXCHG_RETURN(__p, __v1, __v2) __sync_val_compare_and_swap(&__p, __v1, __v2)

#define RQUEUE_MIN_SIZE 3

#pragma pack(push, 4)
typedef struct __rqueue_page {
    void               *value;
    struct __rqueue_page *next;
    struct __rqueue_page *prev;
} rqueue_page_t;

struct __rqueue {
    rqueue_page_t             *head;
    rqueue_page_t             *tail;
    rqueue_page_t             *commit;
    rqueue_page_t             *reader;
    rqueue_free_value_callback_t free_value_cb;
    uint32_t                size;
    int                     mode;
    uint32_t                writes;
    uint32_t                reads;
};
#pragma pack(pop)

static void rqueue_destroy_page(rqueue_page_t *page, rqueue_free_value_callback_t free_value_cb) {
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

rqueue_t *rqueue_create(uint32_t size, rqueue_mode_t mode) {
    uint32_t i;
    rqueue_t *rb = calloc(1, sizeof(rqueue_t));
    rb->size = (size > RQUEUE_MIN_SIZE) ? size : RQUEUE_MIN_SIZE;
    rb->mode = mode;
    for (i = 0; i < size; i++) {
        rqueue_page_t *page = calloc(1, sizeof(rqueue_page_t));
        if (!rb->head) {
            rb->head = rb->tail = page;
        } else if (rb->tail) {
            rb->tail->next = page;
        }
        page->prev = rb->tail;
        rb->tail = page;
    }

    // close the ringbuffer
    rb->head->prev = rb->tail;
    rb->tail->next = rb->head;
    rb->tail->next = RQUEUE_FLAG_ON(rb->tail->next, RQUEUE_FLAG_HEAD);

    rb->tail = rb->head;

    rb->commit = rb->tail;

    // the reader page is out of the ringbuffer
    rb->reader = calloc(1, sizeof(rqueue_page_t));
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
    } while (page != rb->head);

    // release the reader page as well
    rqueue_destroy_page(rb->reader, rb->free_value_cb);

    // and finally release the ringbuffer descriptor
    free(rb);
}

void *rqueue_read(rqueue_t *rb) {
    int i;
    void *v = NULL;
    static int read_sync = 0;

    for (i = 0; i < RQUEUE_MAX_RETRIES; i++) {

        if (ATOMIC_CMPXCHG(read_sync, 0, 1)) {
            rqueue_page_t *head = ATOMIC_READ(rb->head);

            rqueue_page_t *commit = ATOMIC_READ(rb->commit);
            rqueue_page_t *tail = ATOMIC_READ(rb->tail);
            rqueue_page_t *next = ATOMIC_READ(head->next);
            rqueue_page_t *old_next = ATOMIC_READ(rb->reader->next);

            if (rb->reader == commit || (head == tail && commit != tail) || ATOMIC_READ(rb->writes) == 0)
            { // nothing to read
                ATOMIC_CMPXCHG(read_sync, 1, 0);
                continue;
            }

            ATOMIC_CMPXCHG(rb->reader->next, old_next, RQUEUE_FLAG_ON(next, RQUEUE_FLAG_HEAD));
            rb->reader->prev = head->prev;

            if (ATOMIC_CMPXCHG(head->prev->next, RQUEUE_FLAG_ON(head, RQUEUE_FLAG_HEAD), rb->reader)) {
                ATOMIC_CMPXCHG(rb->head, head, next);
                next->prev = rb->reader;
                rb->reader = head;
                /*
                rb->reader->next = next;
                rb->reader->prev = next->prev;
                */
                v = ATOMIC_READ(rb->reader->value); 
                ATOMIC_CMPXCHG(rb->reader->value, v, NULL);
                ATOMIC_INCREMENT(rb->reads, 1);
                ATOMIC_CMPXCHG(read_sync, 1, 0);
                break;
            } else {
                fprintf(stderr, "head swap failed\n");
            }

            ATOMIC_CMPXCHG(read_sync, 1, 0);
        }
    }
    return v;
}

int rqueue_write(rqueue_t *rb, void *value) {
    int retries = 0;
    static int num_writers = 0;
    int did_update = 0;
    int did_move_head = 0;

    rqueue_page_t *temp_page = NULL;
    rqueue_page_t *next_page = NULL;
    rqueue_page_t *tail = NULL;
    rqueue_page_t *head = NULL;
    rqueue_page_t *commit;
    ATOMIC_INCREMENT(num_writers, 1);
    do {
        temp_page = ATOMIC_READ(rb->tail);
        commit = ATOMIC_READ(rb->commit);
        next_page = RQUEUE_FLAG_OFF(ATOMIC_READ(temp_page->next), RQUEUE_FLAG_ALL);
        head = ATOMIC_READ(rb->head);
        if (rb->mode == RQUEUE_MODE_BLOCKING) {
            if (temp_page == commit && next_page == head) {
                if (ATOMIC_READ(rb->writes) - ATOMIC_READ(rb->reads) != 0) {
                    //fprintf(stderr, "No buffer space\n");
                    ATOMIC_DECREMENT(num_writers, 1);
                    return -2;
                }
            } else if (next_page == head) {
                if (ATOMIC_READ(num_writers) == 1) {
                    tail = temp_page;
                    break;
                } else {
                    ATOMIC_DECREMENT(num_writers, 1);
                    return -2;
                }
            }
        }
        tail = ATOMIC_CMPXCHG_RETURN(rb->tail, temp_page, next_page);
    } while (tail != temp_page && !(RQUEUE_CHECK_FLAG(ATOMIC_READ(tail->next), RQUEUE_FLAG_UPDATE)) && retries++ < RQUEUE_MAX_RETRIES);

    if (!tail) {
        ATOMIC_DECREMENT(num_writers, 1);
        return -1;
    } 

    rqueue_page_t *nextp = RQUEUE_FLAG_OFF(ATOMIC_READ(tail->next), RQUEUE_FLAG_ALL);

    if (ATOMIC_CMPXCHG(tail->next, RQUEUE_FLAG_ON(nextp, RQUEUE_FLAG_HEAD), RQUEUE_FLAG_ON(nextp, RQUEUE_FLAG_UPDATE))) {
        did_update = 1;
        //fprintf(stderr, "Did update head pointer\n");
        if (rb->mode == RQUEUE_MODE_OVERWRITE) {
            // we need to advance the head if in overwrite mode ...otherwise we must stop
            //fprintf(stderr, "Will advance head and overwrite old data\n");
            rqueue_page_t *nextpp = RQUEUE_FLAG_OFF(ATOMIC_READ(nextp->next), RQUEUE_FLAG_ALL);
            if (ATOMIC_CMPXCHG(nextp->next, nextpp, RQUEUE_FLAG_ON(nextpp, RQUEUE_FLAG_HEAD))) {
                if (ATOMIC_READ(rb->tail) != next_page) {
                    ATOMIC_CMPXCHG(nextp->next, RQUEUE_FLAG_ON(nextpp, RQUEUE_FLAG_HEAD), nextpp);
                } else {
                    ATOMIC_CMPXCHG(rb->head, head, nextpp);
                    did_move_head = 1;
                }
            }
        }
    }

    void *old_value = ATOMIC_READ(tail->value);
    ATOMIC_CMPXCHG(tail->value, old_value, value);
    if (old_value && rb->free_value_cb)
        rb->free_value_cb(old_value);


    if (ATOMIC_READ(num_writers) == 1) {
        while (!ATOMIC_CMPXCHG(rb->commit, ATOMIC_READ(rb->commit), tail))
            ;// do nothing
    }

    if (did_update) {
        //fprintf(stderr, "Try restoring head pointer\n");

        ATOMIC_CMPXCHG(tail->next,
                       RQUEUE_FLAG_ON(nextp, RQUEUE_FLAG_UPDATE),
                       did_move_head
                       ? RQUEUE_FLAG_OFF(nextp, RQUEUE_FLAG_ALL)
                       : RQUEUE_FLAG_ON(nextp, RQUEUE_FLAG_HEAD));

        //fprintf(stderr, "restored head pointer\n");
    }

    ATOMIC_INCREMENT(rb->writes, 1);
    ATOMIC_DECREMENT(num_writers, 1);
    return 0;
}

uint32_t rqueue_write_count(rqueue_t *rb) {
    return ATOMIC_READ(rb->writes);
}

uint32_t rqueue_read_count(rqueue_t *rb) {
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

    snprintf(buf, 1024,
           "reader:      %p \n"
           "head:        %p \n"
           "tail:        %p \n"
           "commit:      %p \n"
           "commit_next: %p \n"
           "reads:       %u \n"
           "writes:      %u \n"
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
