#ifndef __PQUEUE_H__
#define __PQUEUE_H__

#include <sys/types.h>
#include <stdint.h>

typedef struct __pqueue_s pqueue_t;

typedef void (*pqueue_free_value_callback)(void *value);

pqueue_t *pqueue_create(uint32_t size, pqueue_free_value_callback free_value_cb);

int pqueue_insert(pqueue_t *pq, int32_t prio, void *value);

int pqueue_pull_highest(pqueue_t *pq, void **value, int32_t *prio);
int pqueue_pull_lowest(pqueue_t *pq, void **value, int32_t *prio);

uint32_t pqueue_count(pqueue_t *pq);

void pqueue_destroy(pqueue_t *pq);

#endif
