#include <rqueue.h>
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
    rqueue_t *rb = (rqueue_t *)user;
    int retries = 0;
    for (;;) {
        if (retries >= 1000) {
            //fprintf(stderr, "No more retries!!\n");
            break;
        }
        char *v = rqueue_read(rb);
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
    rqueue_t *rb = (rqueue_t *)user;
    for (i = 0; i < 50000; i++) {
        char *v = malloc(40);
        sprintf(v, "test%d", i);
        while (rqueue_write(rb, v) != 0)
            usleep(250);
    }
    //printf("Filler 0x%04x leaving\n", (int)pthread_self());
    return NULL;
}

static void start_writers(int num_writers, pthread_t *th, rqueue_t *rb) {
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

static void start_readers(int num_readers, pthread_t *th, rqueue_t *rb) {
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

    int rqueue_size = 100000;

    t_testing("Create a new ringbuffer (size: %d)", rqueue_size);
    rqueue_t *rb = rqueue_create(rqueue_size, RQUEUE_MODE_BLOCKING);
    t_result(rb != NULL, "Can't create a new ringbuffer");


    t_testing("Multi-threaded producer/consumer (%d items, parallel reads/writes)", rqueue_size);

    int num_readers = 4;
    pthread_t reader_th[num_readers];
    start_readers(num_readers, reader_th, rb);

    int num_writers = 2;
    pthread_t writer_th[num_writers];
    start_writers(num_writers, writer_th,rb);

    wait_for_writers(num_writers, writer_th);
    wait_for_readers(num_readers, reader_th);

    t_result(rqueue_write_count(rb) == reads_count && reads_count == rqueue_size, "Number of reads and/or writes doesn't match the ringbuffer size "
             "reads: %d, writes: %d, size: %d", reads_count, rqueue_write_count(rb), rqueue_size);

    if (reads_count < rqueue_size) {
        char *stats = rqueue_stats(rb);
        printf("%s\n", stats);
        free(stats);
    }

    t_testing("Multi-threaded producer/consumer (%d items, buffer prefilled before starting readers)", rqueue_size);

    reads_count = 0;

    start_writers(num_writers, writer_th, rb);
    wait_for_writers(num_writers, writer_th);

    start_readers(num_readers, reader_th, rb);
    wait_for_readers(num_readers, reader_th);

    t_result(rqueue_write_count(rb) / 2 == reads_count && reads_count == rqueue_size, "Number of reads and/or writes doesn't match the ringbuffer size "
             "reads: %d, writes: %d, size: %d", reads_count, rqueue_write_count(rb), rqueue_size);

    if (reads_count < rqueue_size) {
        char *stats = rqueue_stats(rb);
        printf("%s\n", stats);
        free(stats);
    }

    t_testing("rqueue_set_free_value_callback()");

    do_free = 0;

    rqueue_set_free_value_callback(rb, free_item);

    filler(rb);
    rqueue_destroy(rb);

    t_result(free_count == rqueue_size/num_writers, "free_count (%d) doesn't match %d", free_count, rqueue_size/num_writers);


    t_testing("Write fails if ringbuffer is full (RQUEUE_MODE_BLOCKING)");

    do_free = 0;

    rb = rqueue_create(2, RQUEUE_MODE_BLOCKING);
    rqueue_write(rb, "1");
    rqueue_write(rb, "2");

    t_result(rqueue_write(rb, "must_fail") == -2, "Write didn't fail with return-code -2");

    t_testing("Write overwrites if ringbuffer is full (RQUEUE_MODE_OVERWRITE)");

    rqueue_set_mode(rb, RQUEUE_MODE_OVERWRITE);
    
    int rc = rqueue_write(rb, "3");
    t_result(rc == 0, "Write failed with return-code %d", rc);
    t_testing("First value is the overwritten one");
    t_validate_string(rqueue_read(rb), "3");

    rqueue_destroy(rb);

    t_summary();

    exit(t_failed);
}
