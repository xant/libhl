#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <libgen.h>
#include <ut.h>
#include "pqueue.h"

static int free_count = 0;

void free_item(void *item)
{
    free_count++;
    free(item);
}

static int
test_walk(pqueue_t *pq, uint64_t prio, void *value, void *priv)
{
    int *count = (int *)priv;
    (*count)++;
    return 1;
}

int
main(int argc, char **argv)
{
    ut_init(basename(argv[0]));

    ut_testing("pqueue_create(PQUEUE_MODE_HIGHEST, 100, free)");
    pqueue_t *pq = pqueue_create(PQUEUE_MODE_HIGHEST, 100, (pqueue_free_value_callback)free_item);
    if (pq)
        ut_success();
    else
        ut_failure("Can't create a new priority queue");

    ut_testing("pqueue_insert(0..99)");
    int i;
    for (i = 0; i < 100; i++) { 
        int *v = malloc(sizeof(int));
        *v = i;
        pqueue_insert(pq, i, (void *)v);
    }
    ut_validate_int(pqueue_count(pq), 100);

    ut_testing("pqueue_pull_highest() == 99");
    int *max = NULL;
    uint64_t maxprio;
    pqueue_pull_highest(pq, (void **)&max, &maxprio);
    ut_validate_int(maxprio, 99);
    ut_testing("pqueue_pull_highest() returned the correct value");
    ut_validate_int(*max, 99);
    free(max);

    ut_testing("pqueue_pull_highest() removed the item from the queue");
    ut_validate_int(pqueue_count(pq), 99);

    ut_testing("pqueue_pull_lowest() == 0");
    int *min = NULL;
    uint64_t minprio;
    pqueue_pull_lowest(pq, (void **)&min, &minprio);
    ut_validate_int(minprio, 0);

    ut_testing("pqueue_pull_lowest() returned the correct value");
    ut_validate_int(*min, 0);
    free(min);

    ut_testing("pqueue_pull_lowest() removed the item from the queue");
    ut_validate_int(pqueue_count(pq), 98);

    ut_testing("Adding 3 elements will drop one because max_size is 100");

    for (i = 0; i < 3; i++) {
        int *v = malloc(sizeof(int));
        *v = 100 + i + 1;
        pqueue_insert(pq, *v, (void *)v);
    }

    ut_validate_int(pqueue_count(pq), 100);

    ut_testing("pqueue_pull_lowest() == 2");
    pqueue_pull_lowest(pq, (void **)&min, &minprio);
    ut_validate_int(*min, 2);
    free(min);

    ut_testing("pqueue_count(pq) == 99");
    ut_validate_int(pqueue_count(pq), 99);

    int cnt = 0;

    ut_testing("pqueue_walk(pq, test_walk, &count)");
    int visited = pqueue_walk(pq, test_walk, &cnt);
    // pqueue_walk should report we have visited 99 nodes
    // and count should have also been summed up to 99
    ut_validate_int(cnt+visited, 99*2);

    ut_testing("pqueue_destroy() and the free_value_callback");
    pqueue_destroy(pq);
    // 100 items need to have been freed so far
    // 1 was automatically removed when overflowing the queue by adding extra items
    // 99 are left in the queue when we called pqueue_destroy()
    ut_validate_int(free_count, 100);

    ut_summary();

    return ut_failed;
}
