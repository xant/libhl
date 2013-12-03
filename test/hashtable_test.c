#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <testing.h>
#include <hashtable.h>
#include <pthread.h>

typedef struct {
    int start;
    int end;
    hashtable_t *table;
} parallel_insert_arg;

static void *parallel_insert(void *user) {
    parallel_insert_arg *arg = (parallel_insert_arg *)user;
    int i;
    for (i = arg->start; i <= arg->end; i++) {
        char k[21];
        sprintf(k, "%d", i);
        char *v = malloc(100);
        sprintf(v, "test%d", i+1);
        ht_set(arg->table, (void *)k, strlen(k), v, strlen(v), NULL, NULL);
    }
    return NULL;
}

int check_item(hashtable_t *table, void *key, size_t klen, void *value, size_t vlen, void *user) {
    int *check_item_count = (int *)user;
    char test[25];
    int num = atoi((char *)key);
    sprintf(test, "test%d", num+1);
    if (strcmp(test, value) == 0)
        (*check_item_count)++;
    return 1;
}

static int free_count = 0;

void free_item(void *item) {
    free(item);
    free_count++;
}

int main(int argc, char **argv) {
    int i;

    t_init();
    t_testing("Create hash table");

    hashtable_t *table = ht_create(256, 0, NULL);
    t_result(table != NULL, "Can't create a new hash table");

    t_testing("ht_set()");
    ht_set(table, "key1", 4, "value1", 6, NULL, NULL);
    t_result(ht_count(table) == 1, "Count is not 1 after setting an item in the table");

    t_testing("ht_get()");
    t_validate_string(ht_get(table, "key1", 4, NULL), "value1");

    t_testing("ht_set() overrides previous value (and returns it)");
    char *test_value = "blah";
    void *old_value = NULL;
    ht_set(table, "key1", 4, test_value, strlen(test_value), &old_value, NULL);
    char *new_value = ht_get(table, "key1", 4, NULL);
    if (strcmp(old_value, "value1") != 0) {
        t_failure("Old value not returned from ht_set() (got: %s , expected: value1)", old_value);
    } else if (strcmp(new_value, test_value) != 0) {
        t_failure("New value not stored properly (got: %s , expected: %s)", new_value, test_value);
    } else {
        t_success();
    }

    ht_clear(table);

    ht_set(table, "test_key", 7, "test_value", 10, NULL, NULL);

    t_testing("ht_unset()");
    old_value = NULL;
    ht_unset(table, "test_key", 7, &old_value, NULL);
    t_result(ht_get(table, "test_key", 7, NULL) == NULL && ht_count(table) == 1, "ht_unset() failed");

    t_testing("ht_unset() returns old value");
    t_result(strcmp(old_value, "test_value") == 0, "ht_unset() didn't return the correct old value (was: %s)", old_value);

    t_testing("ht_delete()");
    ht_delete(table, "test_key", 7, NULL, NULL);
    t_result(ht_get(table, "test_key", 7, NULL) == NULL && ht_count(table) == 0, "ht_delete() failed");

    int num_parallel_threads = 5;
    int num_parallel_items = 100000;

    parallel_insert_arg args[num_parallel_threads];
    pthread_t threads[num_parallel_threads];
    for (i = 0; i < num_parallel_threads; i++) {
        args[i].start = 0 + (i * (num_parallel_items / num_parallel_threads)); 
        args[i].end = args[i].start + (num_parallel_items / num_parallel_threads) -1;
        args[i].table = table;
        pthread_create(&threads[i], NULL, parallel_insert, &args[i]);
    }

    for (i = 0; i < num_parallel_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    t_testing("Parallel insert (%d items, %d threads)", num_parallel_items, num_parallel_threads);
    t_result(ht_count(table) == num_parallel_items,
            "Count is not %d after parallel insert (%d)",
            num_parallel_items, ht_count(table));

    t_testing("ht_foreach_pair() iterator");
    int check_item_count = 0;
    ht_foreach_pair(table, check_item, &check_item_count);
    t_result(check_item_count == num_parallel_items,
            "not all items were valid (%d were valid, should have been %d)",
            check_item_count,
            num_parallel_items);

    t_testing("ht_get_all_values()");
    linked_list_t *values = ht_get_all_values(table);
    t_result(list_count(values) == ht_count(table),
            "returned list doesn't match the table count (%u != %u)",
            list_count(values),
            ht_count(table));
    destroy_list(values);

    ht_set_free_item_callback(table, free_item);
    t_testing("ht_clear() and free_item_callback");
    ht_clear(table);
    t_result(free_count == num_parallel_items,
            "free_count is not equal to %d after clearing the table",
            num_parallel_items);

    ht_destroy(table);

    t_summary();

    exit(0);
} 
