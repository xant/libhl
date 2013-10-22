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


int main(int argc, char **argv) {
    int i;

    do_free = 1;
    t_init();

    int rbuf_size = 100000;

    t_testing("Create a new ringbuffer (size: %d)", rbuf_size);
    rbuf_t *rb = rb_create(rbuf_size, RBUF_MODE_BLOCKING);
    t_result(rb != NULL, "Can't create a new ringbuffer");


    t_testing("Multi-threaded producer/consumer (%d items, parallel reads/writes)", rbuf_size);
    int num_threads = 4;
    pthread_t th[num_threads];
    
    for (i = 0; i < num_threads; i++) {
        pthread_create(&th[i], NULL, worker, rb);
    }

    int num_fillers = 2;
    pthread_t filler_th[num_fillers];
    for (i = 0 ; i < num_fillers; i++) {
        pthread_create(&filler_th[i], NULL, filler, rb);
    }


    for (i = 0 ; i < num_fillers; i++) {
        pthread_join(filler_th[i], NULL);
    }

    for (i = 0 ; i < num_threads; i++) {
     //   pthread_cancel(th[i]);
        pthread_join(th[i], NULL);
    }

    t_result(rb_write_count(rb) == reads_count && reads_count == rbuf_size, "Number of reads and/or writes doesn't match the ringbuffer size "
             "reads: %d, writes: %d, size: %d", reads_count, rb_write_count(rb), rbuf_size);

    if (reads_count < rbuf_size) {
        char *stats = rb_stats(rb);
        printf("%s\n", stats);
        free(stats);
    }

    reads_count = 0;
    do_free = 0;

    for (i = 0 ; i < num_fillers; i++) {
        pthread_create(&filler_th[i], NULL, filler, rb);
    }

    for (i = 0 ; i < num_fillers; i++) {
        pthread_join(filler_th[i], NULL);
    }

    for (i = 0; i < num_threads; i++) {
        pthread_create(&th[i], NULL, worker, rb);
    }

     for (i = 0 ; i < num_threads; i++) {
     //   pthread_cancel(th[i]);
        pthread_join(th[i], NULL);
    }

    t_testing("Multi-threaded producer/consumer (%d items, buffer prefilled before starting readers)", rbuf_size);
    t_result(rb_write_count(rb) / 2 == reads_count && reads_count == rbuf_size, "Number of reads and/or writes doesn't match the ringbuffer size "
             "reads: %d, writes: %d, size: %d", reads_count, rb_write_count(rb), rbuf_size);

    if (reads_count < rbuf_size) {
        char *stats = rb_stats(rb);
        printf("%s\n", stats);
        free(stats);
    }

    rb_set_free_value_callback(rb, free_item);

    t_testing("free_value_callback()");

    filler(rb);
    rb_destroy(rb);
    t_result(free_count == rbuf_size/num_fillers, "free_count (%d) doesn't match %d", free_count, rbuf_size/num_fillers);

    do_free = 0;

    rb = rb_create(2, RB_MODE_OVERWRITE);
    rb_write(rb, "1");
    rb_write(rb, "2");
    t_testing("Write fails if ringbuffer is full (RB_MODE_BLOCKING)");
    t_result(rb_write(rb, "must_fail") == -2, "Write didn't fail with return-code -2");

    t_testing("Write overwrites if ringbuffer is full (RB_MODE_OVERWRITE)");
    
    int rc = rb_write(rb, "3");
    t_result(rc == 0, "Write failed with return-code %d", rc);
    t_testing("First value is the overridden one");
    t_validate_string(rb_read(rb), "3");

    rb_destroy(rb);
}
