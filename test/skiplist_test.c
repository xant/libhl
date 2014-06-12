#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <libgen.h>
#include <ut.h>
#include "skiplist.h"

int
main(int argc, char **argv)
{
    ut_init(basename(argv[0]));

    ut_testing("skiplist_create(6, 50, libhl_cmp_keys_int32, free)");
    skiplist_t *skl = skiplist_create(6, 50, libhl_cmp_keys_int32, free);
    if (skl)
        ut_success();
    else
        ut_failure("Can't create a new binomial heap");

    ut_testing("skiplist_insert(0..99)");
    int i;
    for (i = 0; i < 100; i++) { 
        char *val = malloc(4);
        snprintf(val, 4, "%d", i);
        skiplist_insert(skl, &i, sizeof(i), val);
    }
    ut_validate_int(skiplist_count(skl), 100);

    int test_key = 50;
    ut_testing("skiplist_search(50) = \"50\"");
    char *val = skiplist_search(skl, &test_key, sizeof(int));
    ut_validate_string(val, "50");

    
    ut_testing("skiplist_remove(50, &old_value)");
    val = NULL;
    int rc = skiplist_remove(skl, &test_key, sizeof(int), (void **)&val);
    ut_validate_int(rc, 0);

    ut_testing("old_value is \"50\"");
    ut_validate_string(val, "50");
    free(val);

    ut_testing("skiplist_search(50) = NULL");

    val = skiplist_search(skl, &test_key, sizeof(int));
    ut_validate_string(val, NULL);


    skiplist_destroy(skl);

    ut_summary();

    return ut_failed;
}

