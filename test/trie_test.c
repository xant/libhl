#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <libgen.h>
#include <ut.h>
#include "trie.h"


int main(int argc, char **argv)
{

    ut_init(basename(argv[0]));

    ut_testing("trie_create(NULL)");

    trie_t *trie = trie_create(NULL);
    if (trie)
        ut_success();
    else
        ut_failure("Can't create a new trie");

    int value = 1;

    ut_testing("trie_insert(trie, 'TEST', &value, sizeof(value)) creates 4 nodes");
    int nodes_inserted = trie_insert(trie, "TEST", &value, sizeof(value), 0);
    ut_validate_int(nodes_inserted, 4);

    size_t psize = 0;
    ut_testing("trie_find(trie, 'TEST', &psize) finds and returns the stored value");
    void *p = trie_find(trie, "TEST", &psize);
    if (p)
        ut_validate_int(*((int *)p), value);
    else
        ut_failure("no value found");

    ut_testing("trie_insert(trie, 'TECH', &value, sizeof(value)) creates 2 nodes");
    nodes_inserted = trie_insert(trie, "TECH", &value, sizeof(value), 0);
    ut_validate_int(nodes_inserted, 2);

    ut_testing("trie_remove(trie, 'TEST', &p, &psize) removes 2 nodes and returns the old value");
    int nodes_removed = trie_remove(trie, "TEST", &p, &psize);
    ut_validate_int(nodes_removed, 2);

    ut_testing("trie_find(trie, 'TECH', &psize) finds and returns the stored value");
    p = trie_find(trie, "TECH", &psize);
    if (p)
        ut_validate_int(*((int *)p), value);
    else
        ut_failure("no value found");

    int value2 = 2;
    void *p2 = NULL;
    size_t p2size = 0;
    ut_testing("trie_find_or_insert finds and returns the stored value without updating");
    int update_count = trie_find_or_insert(trie, "TECH", &value2, sizeof(value2), &p2, &p2size, 0);
    ut_validate_int(update_count, 0);

    ut_testing("trie_find_or_insert doesn't find a stored value and inserts a new one");
    update_count = trie_find_or_insert(trie, "TEST", &value2, sizeof(value2), &p2, &p2size, 0);
    ut_validate_int(update_count, 2);

    ut_testing("trie_find_and_insert finds a stored value and doesn't create any new node");
    update_count = trie_find_and_insert(trie, "TEST", &value, sizeof(value), &p2, &p2size, 0);
    ut_validate_int(update_count, 0);

    ut_testing("prev value was correctly returned");
    if (p2)
        ut_validate_int(*((int *)p2), value2);
    else
        ut_failure("no prev value returned");

    ut_testing("the value has been correctly updated");
    p = trie_find(trie, "TEST", &psize);
    if (p)
        ut_validate_int(*((int *)p), value);
    else
        ut_failure("no value found");

    trie_destroy(trie);

    ut_summary();

    return ut_failed;

}
