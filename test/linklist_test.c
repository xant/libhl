#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <testing.h>
#include <linklist.h>
#include <pthread.h>


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
        set_value(arg->list, v, i);
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

int main(int argc, char **argv) {
    t_init();
    t_section("Value-based API");

    t_testing("Create list");
    linked_list_t *list = create_list();
    t_result(list != NULL, "Can't create a new list");

    t_testing("push_value() return value on success");
    t_result(push_value(list, strdup("test1")) == 0, "Can't push value");

    push_value(list, strdup("test2"));
    push_value(list, strdup("test3"));

    t_testing("list_count() after push");
    t_result(list_count(list) == 3, "Length is not 3 after 3 pushes");

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

    set_free_value_callback(list, free_value);

    t_testing("clear_list()");
    clear_list(list);
    t_result(list_count(list) == 0, "List count is not 0 after clear_list()");

    t_testing("free value callback");
    t_result(free_count == 100, "Free count is not 100 after clear_list() (%d)", free_count);

    int num_parallel_threads = 4;
    int num_parallel_items = 10000;

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
    t_testing("Parallel insert (%d items)", num_parallel_items);
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
    t_result(free_count == 10000, "Free count is not 10000 after destroy_list() (%d)", free_count);
    
    t_summary();

}
