#include <rbuf.h>
#include <testing.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

static int reads_count = 0;

static int do_free = 0;
static int free_count = 0;
static void free_item(void *v) {
    free_count++;
    free(v);
}

void *worker(void *user) {
    rbuf_t *rb = (rbuf_t *)user;
    int retries = 0;
    for (;;) {
        if (retries >= 1000) {
            //fprintf(stderr, "No more retries!!\n");
            break;
        }
        char *v = rb_read(rb);
        if (v) {
            //printf("0x%04x - %s \n", (int)pthread_self(), v);
            __sync_fetch_and_add(&reads_count, 1);
            retries = 0;
            if (do_free)
                free(v);
        } else {
            pthread_testcancel();
            retries++;
            usleep(250);
        }
    }
    //printf("Worker 0x%04x leaving\n", (int)pthread_self());
    return NULL;
}

void *filler (void *user) {
    int i;
    rbuf_t *rb = (rbuf_t *)user;
    for (i = 0; i < 50000; i++) {
        char *v = malloc(40);
        sprintf(v, "test%d", i);
        while (rb_write(rb, v) != 0)
            usleep(250);
    }
    //printf("Filler 0x%04x leaving\n", (int)pthread_self());
    return NULL;
}

static void start_writers(int num_writers, pthread_t *th, rbuf_t *rb) {
    int i;
    for (i = 0 ; i < num_writers; i++) {
        pthread_create(&th[i], NULL, filler, rb);
    }
}

static void wait_for_writers(int num_writers, pthread_t *th) {
    int i;
    for (i = 0 ; i < num_writers; i++) {
        pthread_join(th[i], NULL);
    }
}

static void start_readers(int num_readers, pthread_t *th, rbuf_t *rb) {
    int i;
    for (i = 0; i < num_readers; i++) {
        pthread_create(&th[i], NULL, worker, rb);
    }
}

static void wait_for_readers(int num_readers, pthread_t *th) {
    int i;
    for (i = 0 ; i < num_readers; i++) {
     //   pthread_cancel(th[i]);
        pthread_join(th[i], NULL);
    }
}

int main(int argc, char **argv) {

    do_free = 1;
    t_init();

    int rbuf_size = 100000;

    t_testing("Create a new ringbuffer (size: %d)", rbuf_size);
    rbuf_t *rb = rb_create(rbuf_size, RBUF_MODE_BLOCKING);
    t_result(rb != NULL, "Can't create a new ringbuffer");


    t_testing("Multi-threaded producer/consumer (%d items, parallel reads/writes)", rbuf_size);

    int num_readers = 4;
    pthread_t reader_th[num_readers];
    start_readers(num_readers, reader_th, rb);

    int num_writers = 2;
    pthread_t writer_th[num_writers];
    start_writers(num_writers, writer_th,rb);

    wait_for_writers(num_writers, writer_th);
    wait_for_readers(num_readers, reader_th);

    t_result(rb_write_count(rb) == reads_count && reads_count == rbuf_size, "Number of reads and/or writes doesn't match the ringbuffer size "
             "reads: %d, writes: %d, size: %d", reads_count, rb_write_count(rb), rbuf_size);

    if (reads_count < rbuf_size) {
        char *stats = rb_stats(rb);
        printf("%s\n", stats);
        free(stats);
    }

    t_testing("Multi-threaded producer/consumer (%d items, buffer prefilled before starting readers)", rbuf_size);

    reads_count = 0;

    start_writers(num_writers, writer_th, rb);
    wait_for_writers(num_writers, writer_th);

    start_readers(num_readers, reader_th, rb);
    wait_for_readers(num_readers, reader_th);

    t_result(rb_write_count(rb) / 2 == reads_count && reads_count == rbuf_size, "Number of reads and/or writes doesn't match the ringbuffer size "
             "reads: %d, writes: %d, size: %d", reads_count, rb_write_count(rb), rbuf_size);

    if (reads_count < rbuf_size) {
        char *stats = rb_stats(rb);
        printf("%s\n", stats);
        free(stats);
    }

    t_testing("rb_set_free_value_callback()");

    do_free = 0;

    rb_set_free_value_callback(rb, free_item);

    filler(rb);
    rb_destroy(rb);

    t_result(free_count == rbuf_size/num_writers, "free_count (%d) doesn't match %d", free_count, rbuf_size/num_writers);


    t_testing("Write fails if ringbuffer is full (RBUF_MODE_BLOCKING)");

    do_free = 0;

    rb = rb_create(2, RBUF_MODE_BLOCKING);
    rb_write(rb, "1");
    rb_write(rb, "2");

    t_result(rb_write(rb, "must_fail") == -2, "Write didn't fail with return-code -2");

    t_testing("Write overwrites if ringbuffer is full (RBUF_MODE_OVERWRITE)");

    rb_set_mode(rb, RBUF_MODE_OVERWRITE);
    
    int rc = rb_write(rb, "3");
    t_result(rc == 0, "Write failed with return-code %d", rc);
    t_testing("First value is the overwritten one");
    t_validate_string(rb_read(rb), "3");

    rb_destroy(rb);
}
