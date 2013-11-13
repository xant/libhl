#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <testing.h>
#include <linklist.h>
#include <pthread.h>
#include <unistd.h>


typedef struct {
    int start;
    int end;
    linked_list_t *list;
} parallel_insert_arg;

static void *parallel_insert(void *user) {
    parallel_insert_arg *arg = (parallel_insert_arg *)user;
    int i;
    for (i = arg->start; i <= arg->end; i++) {
        char *v = malloc(100);
        sprintf(v, "test%d", i+1);
        set_value(arg->list, i, v);
    }
    return NULL;
}

static int free_count = 0;
static void free_value(void *val) {
    free(val);
    free_count++;
}

int iterator_callback(void *item, uint32_t idx, void *user) {
    int *failed = (int *)user;
    char *val = (char *)item;
    char test[100];
    sprintf(test, "test%u", idx+1);
    if (strcmp(test, val) != 0) {
        t_failure("Value at index %d doesn't match %s  (%s)", idx, test, val);
        *failed = 1;
        return 0;
    }
    return 1;
}

typedef struct {
    linked_list_t *list;
    int count;
} queue_worker_arg;

void *queue_worker(void *user) {
    void *v;
    queue_worker_arg *arg = (queue_worker_arg *)user;
    for (;;) {
        v = shift_value(arg->list);
        if (v) {
            __sync_add_and_fetch(&arg->count, 1);
            free(v);
        }
        pthread_testcancel();
    }
    return NULL;
}

int main(int argc, char **argv) {
    t_init();

    t_testing("Create list");
    linked_list_t *list = create_list();
    t_result(list != NULL, "Can't create a new list");

    t_testing("push_value() return value on success");
    t_result(push_value(list, strdup("test1")) == 0, "Can't push value");

    push_value(list, strdup("test2"));
    push_value(list, strdup("test3"));

    t_testing("list_count() after push");
    t_result(list_count(list) == 3, "Length is not 3 (but %d) after 3 pushes", list_count(list));

    t_testing("pick_value()");
    t_validate_string(pick_value(list, 1), "test2");

    t_testing("shift_value()");
    char *v = shift_value(list);
    t_validate_string(v, "test1"); 

    t_testing("list_count() after shift");
    t_result(list_count(list) == 2, "Length is not 2 after shifting one value");

    t_testing("unshift_value()");
    unshift_value(list, v);
    t_validate_string(pick_value(list, 0), "test1"); 

    t_testing("push_value() accepts NULL");
    t_result(push_value(list, NULL) == 0, "push_value didn't accept a NULL value");

    t_testing("length updated after pushing NULL");
    t_result(list_count(list) == 4, "Length is not 4 after pushing the NULL value");

    t_testing("pop_value()");
    v = pop_value(list);
    t_result(list_count(list) == 3, "Length is not 3 after popping a value from the list");
    t_testing("pop_value() returned last element");
    t_result(v == NULL, "Value is not NULL");

    t_testing("still pop_value() return value");
    v = pop_value(list);
    t_validate_string(v, "test3");

    push_value(list, v);
    t_testing("list_count() consistent");
    t_result(list_count(list) == 3, "Length is not 3 after pushing back the 'test3' value");

    t_testing("pushing 100 values to the list");
    int i;
    for (i = 4; i <= 100; i++) {
        char *val = malloc(100);
        sprintf(val, "test%d", i);
        push_value(list, val);
    }
    t_result(list_count(list) == 100, "Length is not 100"); 

    t_testing("Order is preserved");
    int failed = 0;
    for (i = 0; i < 100; i++) {
        char test[100];
        sprintf(test, "test%d", i+1);
        char *val = pick_value(list, i);
        if (strcmp(val, test) != 0) {
            t_failure("Value at index %d doesn't match %s  (%s)", i, test, val);
            failed = 1;
            break;
        }
    }
    if (!failed)
        t_success();

    t_testing("Value iterator");
    failed = 0;
    foreach_list_value(list, iterator_callback, &failed);
    if (!failed)
        t_success();
    else
        t_failure("Order is wrong");

    t_testing("set_value() overrides previous value (and returns it)");
    // Note: we need to copy the string because the value will be released from our free callback
    char *test_value = strdup("blah");
    char *old_value = set_value(list, 5, test_value);
    char *new_value = pick_value(list, 5);
    if (strcmp(old_value, "test6") != 0) {
        t_failure("Old value is wrong ('%s' should have been 'test6', old_value)", old_value);
    } else if (strcmp(new_value, test_value) != 0) {
        t_failure("New value is wrong ('%s' should have been '%s', old_value)", new_value, test_value);
    } else {
        t_success();
    }
    free(old_value);

    t_testing("swap_values()");
    swap_values(list, 9, 19);
    char *v1 = pick_value(list, 9);
    char *v2 = pick_value(list, 19);
    if (strcmp(v1, "test20") != 0) {
        t_failure("Value at position 9 (%s) should have been equal to 'test20'", v1);
    } else if (strcmp(v2, "test10") != 0) {
        t_failure("Value at position 19 (%s) should have been equal to 'test10'", v2);
    } else {
        t_success();
    }

    t_testing("move_value()");
    old_value = pick_value(list, 45);
    move_value(list, 45, 67);
    t_validate_string(pick_value(list, 67), old_value);

    set_free_value_callback(list, free_value);

    t_testing("clear_list()");
    clear_list(list);
    t_result(list_count(list) == 0, "List count is not 0 after clear_list()");

    t_testing("free value callback");
    t_result(free_count == 100, "Free count is not 100 after clear_list() (%d)", free_count);

    int num_parallel_threads = 5;
    int num_parallel_items = 100000;

    t_testing("Parallel insert (%d items)", num_parallel_items);

    parallel_insert_arg args[num_parallel_threads];
    pthread_t threads[num_parallel_threads];
    for (i = 0; i < num_parallel_threads; i++) {
        args[i].start = 0 + (i * (num_parallel_items / num_parallel_threads)); 
        args[i].end = args[i].start + (num_parallel_items / num_parallel_threads) -1;
        args[i].list = list;
        pthread_create(&threads[i], NULL, parallel_insert, &args[i]);
    }

    for (i = 0; i < num_parallel_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    t_result(list_count(list) == num_parallel_items, "Count is not %d after parallel insert (%d)", num_parallel_items, list_count(list));

    t_testing("Order after parallel insertion");
    failed = 0;
    foreach_list_value(list, iterator_callback, &failed);
    if (!failed)
        t_success();
    else
        t_failure("Order is wrong");

    free_count = 0;
    t_testing("destroy_list()");
    destroy_list(list);
    t_result(free_count == num_parallel_items, "Free count is not %d after destroy_list() (%d)", num_parallel_items, free_count);

    queue_worker_arg arg = {
        .list = create_list(),
        .count = 0
    };

    int num_queued_items = 100000;
    t_testing("Threaded queue (%d pull-workers, %d items pushed to the queue from the main thread)",
              num_parallel_threads, num_queued_items);

    for (i = 0; i < num_parallel_threads; i++) {
        pthread_create(&threads[i], NULL, queue_worker, &arg);
    }

    for (i = 0; i < num_queued_items; i++) {
        char *val = malloc(21);
        sprintf(val, "%d", i);
        push_value(arg.list, val);
    }

    while(list_count(arg.list))
        usleep(500);

    for (i = 0; i < num_parallel_threads; i++) {
        pthread_cancel(threads[i]);
        pthread_join(threads[i], NULL);
    }

    t_result(arg.count == num_queued_items, "Handled items should have been %d (was %d)", num_queued_items, arg.count);

    destroy_list(arg.list);

    linked_list_t *tagged_list = create_list();
    for (i = 0; i < 100; i++) {
        char key[21];
        char val[21];
        sprintf(key, "key%d", i);
        sprintf(val, "value%d", i);
        tagged_value_t *tv1 = create_tagged_value(key, val, strlen(val));
        push_tagged_value(tagged_list, tv1);
    }

    t_testing("get_tagged_value()");
    tagged_value_t *test_tagged_value = get_tagged_value(tagged_list, "key10");
    t_validate_string(test_tagged_value->value, "value10");

    destroy_list(tagged_list);

    t_summary();

    exit(0);
}
