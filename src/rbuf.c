#include <stdlib.h>
#include <rbuf.h>
#include <stdio.h>

#define RBUF_FLAG_HEAD   (0x01)
#define RBUF_FLAG_UPDATE (0x02)
#define RBUF_FLAG_ALL    (0x03)

#define RBUF_FLAG_ON(__addr, __flag) (rbuf_page_t *)(((intptr_t)__addr & -4) | __flag)
#define RBUF_FLAG_OFF(__addr, __flag) (rbuf_page_t *)((intptr_t)__addr & ~__flag)

#define RBUF_CHECK_FLAG(__addr, __flag) (((intptr_t)__addr & __flag) == __flag)

#define RBUF_MAX_RETRIES 500

#define ATOMIC_INCREMENT(__i, __cnt) __sync_fetch_and_add(&__i, __cnt);
#define ATOMIC_DECREMENT(__i, __cnt) __sync_fetch_and_sub(&__i, __cnt);
#define ATOMIC_READ(__p) __sync_fetch_and_add(&__p, 0)
#define ATOMIC_CMPXCHG(__p, __v1, __v2) __sync_bool_compare_and_swap(&__p, __v1, __v2)
#define ATOMIC_CMPXCHG_RETURN(__p, __v1, __v2) __sync_val_compare_and_swap(&__p, __v1, __v2)

#define RBUF_MIN_SIZE 3

#pragma pack(push, 4)
typedef struct __rbuf_page {
    void               *value;
    struct __rbuf_page *next;
    struct __rbuf_page *prev;
} rbuf_page_t;

struct __rbuf {
    rbuf_page_t             *head;
    rbuf_page_t             *tail;
    rbuf_page_t             *commit;
    rbuf_page_t             *reader;
    rbuf_free_value_callback_t free_value_cb;
    uint32_t                size;
    int                     mode;
    uint32_t                writes;
    uint32_t                reads;
};
#pragma pack(pop)

static void rb_destroy_page(rbuf_page_t *page, rbuf_free_value_callback_t free_value_cb) {
    if (page->value && free_value_cb)
        free_value_cb(page->value);
    free(page);
}

/*
static rbuf_page_t *rbuf_create_page() {
    rbuf_page_t *page = calloc(1, sizeof(rbuf_page_t));
    return page;
}

static void *rb_page_value(rbuf_page_t *page) {
    return page->value;
}
*/

rbuf_t *rb_create(uint32_t size, rbuf_mode_t mode) {
    uint32_t i;
    rbuf_t *rb = calloc(1, sizeof(rbuf_t));
    rb->size = (size > RBUF_MIN_SIZE) ? size : RBUF_MIN_SIZE;
    rb->mode = mode;
    for (i = 0; i < size; i++) {
        rbuf_page_t *page = calloc(1, sizeof(rbuf_page_t));
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
    rb->tail->next = RBUF_FLAG_ON(rb->tail->next, RBUF_FLAG_HEAD);

    rb->tail = rb->head;

    rb->commit = rb->tail;

    // the reader page is out of the ringbuffer
    rb->reader = calloc(1, sizeof(rbuf_page_t));
    rb->reader->prev = rb->head->prev;
    rb->reader->next = rb->head;
    return rb;
}

void rb_set_free_value_callback(rbuf_t *rb, rbuf_free_value_callback_t cb) {
    rb->free_value_cb = cb;
}

void rb_destroy(rbuf_t *rb) {
    rbuf_page_t *page = rb->head;
    do {
        rbuf_page_t *p = page;
        page = RBUF_FLAG_OFF(p->next, RBUF_FLAG_ALL);
        rb_destroy_page(p, rb->free_value_cb);
    } while (page != rb->head);

    // release the reader page as well
    rb_destroy_page(rb->reader, rb->free_value_cb);

    // and finally release the ringbuffer descriptor
    free(rb);
}

void *rb_read(rbuf_t *rb) {
    int i;
    void *v = NULL;
    static int read_sync = 0;

    for (i = 0; i < RBUF_MAX_RETRIES; i++) {

        if (ATOMIC_CMPXCHG(read_sync, 0, 1)) {
            rbuf_page_t *head = ATOMIC_READ(rb->head);

            rbuf_page_t *commit = ATOMIC_READ(rb->commit);
            rbuf_page_t *tail = ATOMIC_READ(rb->tail);
            rbuf_page_t *next = ATOMIC_READ(head->next);
            rbuf_page_t *old_next = ATOMIC_READ(rb->reader->next);

            if (rb->reader == commit || (head == tail && commit != tail) || ATOMIC_READ(rb->writes) == 0)
            { // nothing to read
                ATOMIC_CMPXCHG(read_sync, 1, 0);
                continue;
            }

            ATOMIC_CMPXCHG(rb->reader->next, old_next, RBUF_FLAG_ON(next, RBUF_FLAG_HEAD));
            rb->reader->prev = head->prev;

            if (ATOMIC_CMPXCHG(head->prev->next, RBUF_FLAG_ON(head, RBUF_FLAG_HEAD), rb->reader)) {
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

int rb_write(rbuf_t *rb, void *value) {
    int retries = 0;
    static int num_writers = 0;
    int did_update = 0;
    int did_move_head = 0;

    rbuf_page_t *temp_page = NULL;
    rbuf_page_t *next_page = NULL;
    rbuf_page_t *tail = NULL;
    rbuf_page_t *head = NULL;
    rbuf_page_t *commit;
    ATOMIC_INCREMENT(num_writers, 1);
    do {
        temp_page = ATOMIC_READ(rb->tail);
        commit = ATOMIC_READ(rb->commit);
        next_page = RBUF_FLAG_OFF(ATOMIC_READ(temp_page->next), RBUF_FLAG_ALL);
        head = ATOMIC_READ(rb->head);
        if (rb->mode == RBUF_MODE_BLOCKING) {
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
    } while (tail != temp_page && !(RBUF_CHECK_FLAG(ATOMIC_READ(tail->next), RBUF_FLAG_UPDATE)) && retries++ < RBUF_MAX_RETRIES);

    if (!tail) {
        ATOMIC_DECREMENT(num_writers, 1);
        return -1;
    } 

    rbuf_page_t *nextp = RBUF_FLAG_OFF(ATOMIC_READ(tail->next), RBUF_FLAG_ALL);

    if (ATOMIC_CMPXCHG(tail->next, RBUF_FLAG_ON(nextp, RBUF_FLAG_HEAD), RBUF_FLAG_ON(nextp, RBUF_FLAG_UPDATE))) {
        did_update = 1;
        //fprintf(stderr, "Did update head pointer\n");
        if (rb->mode == RBUF_MODE_OVERWRITE) {
            // we need to advance the head if in overwrite mode ...otherwise we must stop
            //fprintf(stderr, "Will advance head and overwrite old data\n");
            rbuf_page_t *nextpp = RBUF_FLAG_OFF(ATOMIC_READ(nextp->next), RBUF_FLAG_ALL);
            if (ATOMIC_CMPXCHG(nextp->next, nextpp, RBUF_FLAG_ON(nextpp, RBUF_FLAG_HEAD))) {
                if (ATOMIC_READ(rb->tail) != next_page) {
                    ATOMIC_CMPXCHG(nextp->next, RBUF_FLAG_ON(nextpp, RBUF_FLAG_HEAD), nextpp);
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
                       RBUF_FLAG_ON(nextp, RBUF_FLAG_UPDATE),
                       did_move_head
                       ? RBUF_FLAG_OFF(nextp, RBUF_FLAG_ALL)
                       : RBUF_FLAG_ON(nextp, RBUF_FLAG_HEAD));

        //fprintf(stderr, "restored head pointer\n");
    }

    ATOMIC_INCREMENT(rb->writes, 1);
    ATOMIC_DECREMENT(num_writers, 1);
    return 0;
}

uint32_t rb_write_count(rbuf_t *rb) {
    return ATOMIC_READ(rb->writes);
}

uint32_t rb_read_count(rbuf_t *rb) {
    return ATOMIC_READ(rb->reads);
}

void rb_set_mode(rbuf_t *rb, rbuf_mode_t mode) {
    rb->mode = mode;
}

rbuf_mode_t rb_mode(rbuf_t *rb) {
    return rb->mode;
}

char *rb_stats(rbuf_t *rb) {
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
           rb->mode == RBUF_MODE_BLOCKING ? "blocking" : "overwrite");

    return buf;
}
