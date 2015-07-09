#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ut.h>
#include <hashtable.h>
#include <pthread.h>
#include <libgen.h>

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
        ht_set(arg->table, (void *)k, strlen(k), v, strlen(v));
    }
    return NULL;
}

int check_item(hashtable_t *table, void *key, size_t klen, void *value, size_t vlen, void *user) {
    int *check_item_count = (int *)user;
    char test[25];
    char keystr[klen+1];
    memcpy(keystr, key, klen);
    keystr[klen] = 0;
    int num = atoi(keystr);
    sprintf(test, "test%d", num+1);
    if (strlen(test) == vlen && memcmp(test, value, vlen) == 0)
        (*check_item_count)++;
    static int last_count = 0;
    int count = *check_item_count/1000;
    if(count % 10 == 0 && count != last_count) { 
        ut_progress(count);
        last_count = count;
    }
    return 1;
}

static int free_count = 0;

void free_item(void *item) {
    free(item);
    free_count++;
    static int last_count = 0;
    int count = free_count/1000;
    if(count % 10 == 0 && count != last_count) { 
        ut_progress(count);
        last_count = count;
    }
}

int main(int argc, char **argv) {
    int i;

    ut_init(basename(argv[0]));
    ut_testing("Create hash table");

    hashtable_t *table = ht_create(256, 0, NULL);
    ut_result(table != NULL, "Can't create a new hash table");

    ut_testing("ht_set()");
    ht_set(table, "key1", 4, "value1", 6);
    ut_result(ht_count(table) == 1, "Count is not 1 after setting an item in the table");

    ut_testing("ht_get()");
    ut_validate_string(ht_get(table, "key1", 4, NULL), "value1");

    ut_testing("ht_get_and_set() overwrite the previous value and returns it");
    char *test_value = "blah";
    void *old_value = NULL;
    ht_get_and_set(table, "key1", 4, test_value, strlen(test_value), &old_value, NULL);
    char *new_value = ht_get(table, "key1", 4, NULL);
    if (strcmp(old_value, "value1") != 0) {
        ut_failure("Old value not returned from ht_get_and_set() (got: %s , expected: value1)", old_value);
    } else if (strcmp(new_value, test_value) != 0) {
        ut_failure("New value not stored properly (got: %s , expected: %s)", new_value, test_value);
    } else {
        ut_success();
    }

    ht_clear(table);

    ht_set(table, "test_key", 7, "test_value", 10);

    ut_testing("ht_set_if_not_exists()");
    int rc = ht_set_if_not_exists(table, "test_key", 7, "blah", 4);
    ut_validate_int(rc, 1);

    ut_testing("ht_get_or_set() doesn't overwrite the current value and returns it");
    ht_get_or_set(table, "test_key", 7, "blah", 4, &old_value, NULL);
    new_value = ht_get(table, "test_key", 7, NULL);
    ut_validate_string(new_value, "test_value");

    ut_testing("ht_unset()");
    old_value = NULL;
    ht_unset(table, "test_key", 7, &old_value, NULL);
    ut_result(ht_get(table, "test_key", 7, NULL) == NULL && ht_count(table) == 1, "ht_unset() failed");

    ut_testing("ht_unset() returns the old value");
    ut_result(strcmp(old_value, "test_value") == 0, "ht_unset() didn't return the correct old value (was: %s)", old_value);

    ut_testing("ht_delete()");
    ht_delete(table, "test_key", 7, NULL, NULL);
    ut_result(ht_get(table, "test_key", 7, NULL) == NULL && ht_count(table) == 0, "ht_delete() failed");

    int num_parallel_threads = 5;
    int num_parallel_items = 100000;
    ht_clear(table);

    ut_testing("Parallel insert (%d items, %d threads)", num_parallel_items, num_parallel_threads);

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

    ut_result(ht_count(table) == num_parallel_items,
            "Count is not %d after parallel insert (%d)",
            num_parallel_items, ht_count(table));

    ut_testing("ht_foreach_pair() iterator");
    int check_item_count = 0;
    ht_foreach_pair(table, check_item, &check_item_count);
    ut_result(check_item_count == num_parallel_items,
              "not all items were valid (%d were valid, should have been %d)",
              check_item_count,
              num_parallel_items);

    ut_testing("ht_get_all_values()");
    linked_list_t *values = ht_get_all_values(table);
    ut_result(list_count(values) == ht_count(table),
              "returned list doesn't match the table count (%u != %u)",
              list_count(values),
              ht_count(table));

    ut_testing("returned list contains all correct values");
    hashtable_t *tmptable = ht_create(0, 0, NULL);
    int failed = 0;
    for (i = 0; i < list_count(values); i++) {
        hashtable_value_t *val = list_pick_value(values, i);
        if (ht_get(tmptable, val->data, val->len, NULL)) {
            ut_failure("same value found twice!");
            failed++;
            break;
        }
        char test[25];
        char keystr[val->klen+1];
        memcpy(keystr, val->key, val->klen);
        keystr[val->klen] = 0;
        int num = atoi(keystr);
        sprintf(test, "test%d", num+1);
        if (strlen(test) != val->len || memcmp(test, val->data, val->len) != 0) {
            ut_failure("returned value for key %s doesn't match! (%.*s != %s)",
                       keystr, val->len, val->data, test);
            failed++;
            break;
        }
        ht_set(tmptable, val->data, val->len, "", 1);
    }
    if (!failed)
        ut_success();
    list_destroy(values);
    ht_destroy(tmptable);

    ht_set_free_item_callback(table, free_item);
    ut_testing("ht_clear() and free_item_callback");
    ht_clear(table);
    ut_result(free_count == num_parallel_items,
            "free_count is not equal to %d after clearing the table",
            num_parallel_items);

    ht_destroy(table);

    ut_summary();

    exit(ut_failed);
} 
