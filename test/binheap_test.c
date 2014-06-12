#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <libgen.h>
#include <ut.h>
#include "binheap.h"

int
main(int argc, char **argv)
{
    ut_init(basename(argv[0]));

    ut_testing("binheap_create(binheap_keys_callbacks_int32(), BINHEAP_MODE_MAX)");
    binheap_t *bh = binheap_create(binheap_keys_callbacks_int32_t(), BINHEAP_MODE_MAX);
    if (bh)
        ut_success();
    else
        ut_failure("Can't create a new binomial heap");

    ut_testing("binheap_insert(0..99)");
    int i;
    for (i = 0; i < 100; i++) { 
        binheap_insert(bh, &i, sizeof(i), NULL);
    }
    ut_validate_int(binheap_count(bh), 100);

    ut_testing("binheap_maximum() == 99");
    int *max = NULL;
    binheap_maximum(bh, (void **)&max, NULL, NULL);
    ut_validate_int(*max, 99);

    ut_testing("binheap_minimum() == 0");
    int *min = NULL;
    binheap_minimum(bh, (void **)&min, NULL, NULL);
    ut_validate_int(*min, 0);

    ut_testing("binheap_increase_minimum(bh, 1)");
    binheap_increase_minimum(bh, 1);
    binheap_minimum(bh, (void **)&min, NULL, NULL);
    ut_validate_int(*min, 1);

    ut_testing("binheap_increase_maximum(bh, 1)");
    binheap_increase_maximum(bh, 1);
    binheap_maximum(bh, (void **)&max, NULL, NULL);
    ut_validate_int(*max, 100);

    binheap_destroy(bh);

    ut_summary();

    return ut_failed;
}
