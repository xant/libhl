/*
 * build using:
 * cc -DDEBUG_RBTREE -Isrc src/rbtree.c test/rbtree_debug.c -o test/rbtree_debug
 */
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <libgen.h>
#include "rbtree.h"

/*
static int
print_value(rbtree_t *rbt, void *key, size_t ksize, void *value, size_t vsize, void *priv)
{
    printf("%d\n", *((int *)value));
    return 0;
}
*/

int
main(int argc, char **argv)
{
    int *v;
    int i;

    srandom(time(NULL));


    rbtree_t *rbt = rbtree_create(NULL, free);
    int sum = 0;
    for (i = 0; i < 18; i++) {
        printf("\e[1;1H\e[2J");
        v = malloc(sizeof(int));
        *v = i;
        rbtree_add(rbt, v, sizeof(int), v, sizeof(int));
        sum += i;

        rbtree_print(rbt);
        sleep(2);
    }

    printf("Removing '7'\n");
    i = 7;
    rbtree_remove(rbt, &i, sizeof(int));
    printf("\e[1;1H\e[2J");
    rbtree_print(rbt);
    sleep(2);

    i = 16;
    rbtree_remove(rbt, &i, sizeof(int));
    printf("\e[1;1H\e[2J");
    rbtree_print(rbt);
    sleep(2);

    i = 17;
    rbtree_remove(rbt, &i, sizeof(int));
    printf("\e[1;1H\e[2J");
    rbtree_print(rbt);
    rbtree_destroy(rbt);

    return 0;
}
