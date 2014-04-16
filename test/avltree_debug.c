/*
 * build using:
 * cc -DDEBUG_AVLT -DDEBUG_AVLT_COMPACT -Isrc src/avltree.c test/avltree_debug.c -o test/avltree_debug
 */
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <libgen.h>
#include "avltree.h"

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

    char buf[256];
    int c;
    int ofx = 0;
    avlt_t *avlt = avlt_create(avlt_cmp_keys_int32, free);
    printf("\e[1;1H\e[2J");
    printf("Enter an integer number: ");
    while((c = getchar())) {
        if (c == EOF)
            break;
        if (c == '\n') {
            buf[ofx] = 0;
            int *num = malloc(sizeof(int));
            int delete = 0;
            if (buf[0] == 'D') {
                delete = 1;
                *num = strtol(&buf[1], NULL, 10);
            } else {
                *num = strtol(buf, NULL, 10);
            }
            printf("Added node: %d\n\n", *num);
            if (delete)
                avlt_remove(avlt, num, sizeof(int), NULL, NULL);
            else
                avlt_add(avlt, num, sizeof(int), num, sizeof(int));
            printf("\e[1;1H\e[2J");
            avlt_print(avlt);
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
