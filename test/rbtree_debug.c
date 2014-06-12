/*
 * build using:
 * cc -DDEBUG_RBTREE -DDEBUG_RBTREE_COMPACT -Isrc src/rbtree.c test/rbtree_debug.c -o test/rbtree_debug
 */
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <libgen.h>
#include "rbtree.h"

/*
static int
print_value(rbtree_t *rbt, void *key, size_t ksize, void *value, void *priv)
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

    char buf[256];
    int c;
    int ofx = 0;
    rbtree_t *rbt = rbtree_create(rbtree_cmp_keys_int32, free);
    printf("\e[1;1H\e[2J");
    printf("Enter an integer number: ");
    while((c = getchar())) {
        if (c == EOF)
            break;
        if (c == '\n') {
            buf[ofx] = 0;
            int *num = malloc(sizeof(int));
            *num = strtol(buf, NULL, 10);
            printf("Added node: %d\n\n", *num);
            rbtree_add(rbt, num, sizeof(int), num, sizeof(int));
            printf("\e[1;1H\e[2J");
            rbtree_print(rbt);
            ofx = 0;
            printf("Enter an integer number: ");
        } else {
            buf[ofx++] = c;
        }
        if (ofx == 255) {
            fprintf(stderr, "input too long, discarding\n");
            ofx = 0;
        }
    }
    printf("\n");
}
