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
        ht_set(arg->table, k, v);
    }
    return NULL;
}

static int free_count = 0;

void free_item(void *item) {
    free(item);
    free_count++;
}

int main(int argc, char **argv) {
    /*
    int i;

    t_init();
    t_testing("Create hash table");
    hashtable_t *table = ht_create(256);
    t_result(table != NULL, "Can't create a new hash table");

    t_testing("ht_set()");
    ht_set(table, "key1", "value1");
    t_result(ht_count(table) == 1, "Count is not 1 after setting an item in the table");

    t_testing("ht_get()");
    t_validate_string(ht_get(table, "key1"), "value1");

    ht_clear(table);

    int num_parallel_threads = 4;
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
    t_testing("Parallel insert (%d items)", num_parallel_items);
    t_result(ht_count(table) == num_parallel_items, "Count is not %d after parallel insert (%d)", num_parallel_items, ht_count(table));


    t_summary();
    */
    int i;
    struct timeval t1, t2; 
#define NUM_ITEMS 10000000
    char **keys = malloc(sizeof(char *) * NUM_ITEMS);

    hashtable_t *table = ht_create(NUM_ITEMS);
    ht_set_free_item_callback(table, free);
    for (i = 0; i < NUM_ITEMS; i++) {
        char *key = malloc(11);
        char *data = malloc(14);
        sprintf(key, "k_%d", i); 
        keys[i] = key;
        sprintf(data, "data_%d", i); 
        ht_set(table, key, data);
    }   

    for (i = 0; i < NUM_ITEMS; i++) {
        char *v = ht_get(table, keys[i]);
        //printf("%s\n", v);
    }   
    ht_destroy(table);
} 
