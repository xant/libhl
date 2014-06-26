#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ut.h>
#include <queue.h>
#include <pthread.h>
#include <unistd.h>
#include <libgen.h>


typedef struct {
    int start;
    int end;
    queue_t *queue;
} parallel_insert_arg;

static void *parallel_insert(void *user) {
    parallel_insert_arg *arg = (parallel_insert_arg *)user;
    int i;
    for (i = arg->start; i <= arg->end; i++) {
        char *v = malloc(100);
        sprintf(v, "test%d", i+1);
        if (queue_push_right(arg->queue, v) != 0)
            printf("\nERROR\n\n");
    }
    return NULL;
}

static int free_count = 0;
static void free_value(void *val) {
    free(val);
    free_count++;
}

typedef struct {
    queue_t *queue;
    int count;
    int leave;
    int direction;
} queue_worker_arg;

void *queue_worker(void *user) {
    void *v;
    queue_worker_arg *arg = (queue_worker_arg *)user;
    while (!__sync_fetch_and_add(&arg->leave, 0)) {
        v = arg->direction ? queue_pop_right(arg->queue) : queue_pop_left(arg->queue);
        if (v) {
            __sync_add_and_fetch(&arg->count, 1);
            free(v);
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    int i;

    ut_init(basename(argv[0]));

    ut_testing("Create queue");
    queue_t *q = queue_create();
    ut_result(q != NULL, "Can't create a new queue");

    ut_testing("queue_push_right() return value on success");
    ut_result(queue_push_right(q, strdup("test1")) == 0, "Can't push value");

    queue_push_right(q, strdup("test2"));
    queue_push_right(q, strdup("test3"));

    ut_testing("queue_count() after push");
    ut_result(queue_count(q) == 3, "Length is not 3 (but %d) after 3 pushes", queue_count(q));

    queue_set_free_value_callback(q, free_value);
    queue_clear(q);
    //queue_destroy(q);

   // exit(0);
    int num_parallel_threads = 5;
    int num_parallel_items = 10000;

    parallel_insert_arg args[num_parallel_threads];
    pthread_t threads[num_parallel_threads];
    for (i = 0; i < num_parallel_threads; i++) {
        args[i].start = 0 + (i * (num_parallel_items / num_parallel_threads)); 
        args[i].end = args[i].start + (num_parallel_items / num_parallel_threads) -1;
        args[i].queue = q;
        pthread_create(&threads[i], NULL, parallel_insert, &args[i]);
    }

    for (i = 0; i < num_parallel_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    ut_testing("Parallel insert (%d items)", num_parallel_items);
    ut_result(queue_count(q) == num_parallel_items, "Count is not %d after parallel insert (%d)", num_parallel_items, queue_count(q));

    /*
    ut_testing("Order after parallel insertion");
    failed = 0;
    foreach_list_value(list, iterator_callback, &failed);
    if (!failed)
        ut_success();
    else
        ut_failure("Order is wrong");
    */

    queue_set_free_value_callback(q, free_value);
    free_count = 0;
    ut_testing("destroy_list()");
    queue_destroy(q);
    ut_result(free_count == num_parallel_items, "Free count is not %d after destroy_list() (%d)", num_parallel_items, free_count);

    queue_worker_arg arg = {
        .queue = queue_create(),
        .count = 0,
        .leave = 0,
        .direction = 0
    };

    int num_queued_items = 10000;
    ut_testing("Threaded queue (%d pull-workers, %d items pushed to the queue from the main thread)",
              num_parallel_threads, num_queued_items);

    for (i = 0; i < num_parallel_threads; i++) {
        pthread_create(&threads[i], NULL, queue_worker, &arg);
    }

    for (i = 0; i < num_queued_items; i++) {
        char *val = malloc(21);
        sprintf(val, "%d", i);
        queue_push_right(arg.queue, val);
    }

    while(queue_count(arg.queue))
        usleep(500);

    __sync_add_and_fetch(&arg.leave, 1);
    for (i = 0; i < num_parallel_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    ut_result(arg.count == num_queued_items, "Handled items should have been %d (was %d)", num_queued_items, arg.count);

    queue_destroy(arg.queue);

    // now try the same but pushing to the left and pulling from the right
    arg.queue = queue_create();
    arg.count = 0;
    arg.leave = 0;
    arg.direction = 1;

    ut_testing("Threaded queue reverse (%d pull-workers, %d items pushed to the queue from the main thread)",
              num_parallel_threads, num_queued_items);

    for (i = 0; i < num_parallel_threads; i++) {
        pthread_create(&threads[i], NULL, queue_worker, &arg);
    }

    for (i = 0; i < num_queued_items; i++) {
        char *val = malloc(21);
        sprintf(val, "%d", i);
        queue_push_left(arg.queue, val);
    }

    while(queue_count(arg.queue))
        usleep(500);

    __sync_add_and_fetch(&arg.leave, 1);
    for (i = 0; i < num_parallel_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    ut_result(arg.count == num_queued_items, "Handled items should have been %d (was %d)", num_queued_items, arg.count);

    queue_destroy(arg.queue);

    ut_summary();

    exit(ut_failed);
}
