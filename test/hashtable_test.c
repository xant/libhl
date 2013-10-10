#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <testing.h>
#include <hashtable.h>

int main(int argc, char **argv) {
    t_init();
    t_testing("Create hash table");
    hashtable_t *table = ht_create(256);
    t_result(table != NULL, "Can't create a new hash table");

    t_testing("ht_set()");
    ht_set(table, "key1", "value1");
    t_result(ht_count(table) == 1, "Count is not 1 after setting an item in the table");

    t_testing("ht_get()");
    t_validate_string(ht_get(table, "key1"), "value1");

    t_summary();
} 
