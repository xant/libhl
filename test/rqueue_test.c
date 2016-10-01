#include <rqueue.h>
#include <ut.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <libgen.h>

#define SIZE_OF_BUFFER 512
#define NUM_OF_WRITER 5
#define NUM 1000

static rqueue_t *ring;
static int end;
static int write_count = 0;
static int read_count = 0;

void *write_thread(void *ptr) {
    int i;
    for(i = 0; i < NUM; i++) {
        void *p = malloc(1);
        if(p) {
            if(rqueue_write(ring, p) == 0) {
                __sync_fetch_and_add(&write_count, 1);
            }
        }
    }
    return NULL;
}

void *read_thread(void *ptr) {
    while(1) {
        if(__sync_fetch_and_add(&end, 0) && rqueue_isempty(ring))
            break;

        void *val = rqueue_read(ring);
        if(val) __sync_fetch_and_add(&read_count, 1);
        free(val);
    }
    return NULL;
}

void test_multiple_writers_one_reader() {
    ut_testing("Test multiple writers and one reader");
    ring = rqueue_create(SIZE_OF_BUFFER, RQUEUE_MODE_BLOCKING);
    pthread_t writer[NUM_OF_WRITER];
    pthread_t reader;
    end = 0;
    pthread_create(&reader, NULL, read_thread, NULL);
    int i;
    for(i = 0; i < NUM_OF_WRITER; i++) {
            pthread_create(writer + i, NULL, write_thread, NULL);
    }
    for(i = 0; i < NUM_OF_WRITER; i++) {
            pthread_join(writer[i], NULL);
    }
    __sync_fetch_and_add(&end, 1);
    pthread_join(reader, NULL);
    rqueue_destroy(ring);
    ut_result(read_count == write_count, "the read count (%d) doesn't match the write count (%d)");
}

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
    ut_init(basename(argv[0]));

    int rqueue_size = 100000;

    ut_testing("Create a new ringbuffer (size: %d)", rqueue_size);
    rqueue_t *rb = rqueue_create(rqueue_size, RQUEUE_MODE_BLOCKING);
    ut_result(rb != NULL, "Can't create a new ringbuffer");


    ut_testing("Multi-threaded producer/consumer (%d items, parallel reads/writes)", rqueue_size);

    int num_readers = 4;
    pthread_t reader_th[num_readers];
    start_readers(num_readers, reader_th, rb);

    int num_writers = 2;
    pthread_t writer_th[num_writers];
    start_writers(num_writers, writer_th,rb);

    wait_for_writers(num_writers, writer_th);
    wait_for_readers(num_readers, reader_th);

    ut_result(rqueue_write_count(rb) == reads_count && reads_count == rqueue_size, "Number of reads and/or writes doesn't match the ringbuffer size "
             "reads: %d, writes: %d, size: %d", reads_count, rqueue_write_count(rb), rqueue_size);

    if (reads_count < rqueue_size) {
        char *stats = rqueue_stats(rb);
        printf("%s\n", stats);
        free(stats);
    }

    ut_testing("Multi-threaded producer/consumer (%d items, buffer prefilled before starting readers)", rqueue_size);

    reads_count = 0;

    start_writers(num_writers, writer_th, rb);
    wait_for_writers(num_writers, writer_th);

    start_readers(num_readers, reader_th, rb);
    wait_for_readers(num_readers, reader_th);

    ut_result(rqueue_write_count(rb) / 2 == reads_count && reads_count == rqueue_size, "Number of reads and/or writes doesn't match the ringbuffer size "
             "reads: %d, writes: %d, size: %d", reads_count, rqueue_write_count(rb), rqueue_size);

    if (reads_count < rqueue_size) {
        char *stats = rqueue_stats(rb);
        printf("%s\n", stats);
        free(stats);
    }

    ut_testing("rqueue_set_free_value_callback()");

    do_free = 0;

    rqueue_set_free_value_callback(rb, free_item);

    filler(rb);
    rqueue_destroy(rb);

    ut_result(free_count == rqueue_size/num_writers, "free_count (%d) doesn't match %d", free_count, rqueue_size/num_writers);


    ut_testing("Write fails if ringbuffer is full (RQUEUE_MODE_BLOCKING)");

    do_free = 0;

    rb = rqueue_create(2, RQUEUE_MODE_BLOCKING);
    rqueue_write(rb, "1");
    rqueue_write(rb, "2");

    ut_result(rqueue_write(rb, "must_fail") == -2, "Write didn't fail with return-code -2");

    ut_testing("Write overwrites if ringbuffer is full (RQUEUE_MODE_OVERWRITE)");

    rqueue_set_mode(rb, RQUEUE_MODE_OVERWRITE);
    
    int rc = rqueue_write(rb, "3");
    ut_result(rc == 0, "Write failed with return-code %d", rc);
    ut_testing("First value is the overwritten one (XXX - this test MUST fail until the algorithm is fixed to not require size+1 pages to work) ");
    ut_validate_string(rqueue_read(rb), "3");

    rqueue_destroy(rb);

    test_multiple_writers_one_reader();

    ut_summary();

    exit(ut_failed);
}


