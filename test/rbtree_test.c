#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <libgen.h>
#include <ut.h>
#include "rbtree.h"

/*
static int
print_value(rbtree_t *rbt, void *key, size_t ksize, void *value, size_t vsize, void *priv)
{
    printf("%d\n", *((int *)value));
    return 0;
}
*/

static int
sum_value(rbtree_t *rbt, void *key, size_t ksize, void *value, size_t vsize, void *priv)
{
    int *vsum = (int *)priv;
    *vsum += *((int *)value);
    return 0;
}

static int
get_root(rbtree_t *rbt, void *key, size_t ksize, void *value, size_t vsize, void *priv)
{
    void **p = (void **)priv;
    *p = value;
    return -1;
}

int
main(int argc, char **argv)
{
    int *v;
    int i;

    srandom(time(NULL));

    ut_init(basename(argv[0]));

    ut_testing("rbtree_create(free)");
    rbtree_t *rbt = rbtree_create(rbtree_cmp_keys_int16, free);
    if (rbt)
        ut_success();
    else
        ut_failure("Can't create a new rbtree");

    ut_testing("Adding 0..18");
    int sum = 0;
    for (i = 0; i < 18; i++) {
        v = malloc(sizeof(int));
        *v = i;
        rbtree_add(rbt, v, sizeof(int), v, sizeof(int));
        sum += i;
    }
    int vsum = 0;
    rbtree_walk(rbt, sum_value, &vsum);
    ut_validate_int(vsum, sum);

    ut_testing("root is '7'");
    rbtree_walk(rbt, get_root, &v);
    ut_validate_int(*((int *)v), 7);
    
    /*
    rbtree_walk(rbt, print_value, NULL);
    printf("\n---------\n\n");
    */

    ut_testing("Removing '7'");
    i = 7;
    rbtree_remove(rbt, &i, sizeof(int));
    vsum = 0;
    rbtree_walk(rbt, sum_value, &vsum);
    ut_validate_int(vsum, sum - 7);

    ut_testing("root is '6'");
    rbtree_walk(rbt, get_root, &v);
    ut_validate_int(*((int *)v), 6);

    /*
    rbtree_walk(rbt, print_value, NULL);
    */
    
    ut_summary();

    return ut_failed;
}
