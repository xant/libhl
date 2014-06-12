#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <libgen.h>
#include <ut.h>
#include "avltree.h"

/*
static int
print_value(avlt_t *rbt, void *key, size_t ksize, void *value, void *priv)
{
    printf("%d\n", *((int *)value));
    return 0;
}
*/

static int
sum_value(avlt_t *rbt, void *key, size_t ksize, void *value, void *priv)
{
    int *vsum = (int *)priv;
    *vsum += *((int *)value);
    return 1;
}

static int
check_sort(avlt_t *rbt, void *key, size_t ksize, void *value, void *priv)
{
    int *check = (int *)priv;
    int v = *((int *)value);

    if (v != *check)
        return 0;

    (*check)++;
    return 1;
}

static int
get_root(avlt_t *rbt, void *key, size_t ksize, void *value, void *priv)
{
    void **p = (void **)priv;
    *p = value;
    return 0;
}

int
main(int argc, char **argv)
{
    int *v;
    int i;

    srandom(time(NULL));

    ut_init(basename(argv[0]));

    ut_testing("avlt_create(free)");
    avlt_t *rbt = avlt_create(libhl_cmp_keys_int16, free);
    if (rbt)
        ut_success();
    else
        ut_failure("Can't create a new avlt");

    ut_testing("Adding 0..18");
    int sum = 0;
    for (i = 0; i < 18; i++) {
        v = malloc(sizeof(int));
        *v = i;
        avlt_add(rbt, v, sizeof(int), v);
        sum += i;
    }
    int vsum = 0;
    int rc = avlt_walk(rbt, sum_value, &vsum);
    ut_validate_int(vsum, sum);

    ut_testing("avlt_walk() return value");
    ut_validate_int(rc, 18);

    ut_testing("root is '7'");
    avlt_walk(rbt, get_root, &v);
    ut_validate_int(*((int *)v), 7);
    
    ut_testing("avlt_walk_sorted()");
    int check = 0;
    rc = avlt_walk_sorted(rbt, check_sort, &check);
    ut_validate_int(check, 18);

    ut_testing("avlt_walk_sorted() return value");
    ut_validate_int(rc, 18);

    ut_testing("Removing '7'");
    i = 7;
    avlt_remove(rbt, &i, sizeof(int), NULL);
    vsum = 0;
    avlt_walk(rbt, sum_value, &vsum);
    ut_validate_int(vsum, sum - 7);

    ut_testing("root is '8'");
    avlt_walk(rbt, get_root, &v);
    ut_validate_int(*((int *)v), 8);

    /*
    avlt_walk(rbt, print_value, NULL);
    */
    
    ut_summary();

    return ut_failed;
}
