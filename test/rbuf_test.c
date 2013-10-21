#include <rbuf.h>
#include <testing.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

static int ccnt = 0;
void *worker(void *user) {
    rbuf_t *rb = (rbuf_t *)user;
    int cnt = 0;
    int retries = 0;
    for (;;) {
        if (retries >= 1000) {
            //fprintf(stderr, "No more retries!!\n");
            break;
        }
        char *v = rb_read(rb);
        if (v) {
            //printf("0x%04x - %s \n", (int)pthread_self(), v);
            __sync_fetch_and_add(&ccnt, 1);
            retries = 0;
        } else {
            pthread_testcancel();
            retries++;
            usleep(100);
        }
    }
    //printf("Worker 0x%04x leaving\n", (int)pthread_self());
    return NULL;
}

void *filler (void *user) {
    int i;
    rbuf_t *rb = (rbuf_t *)user;
    for (i = 0; i < 5000; i++) {
        char *v = malloc(40);
        sprintf(v, "test%d", i);
        rb_write(rb, v);
        //usleep(250);
    }
    //printf("Filler 0x%04x leaving\n", (int)pthread_self());
    return NULL;
}


int main(int argc, char **argv) {
    rbuf_t *rb = rb_create(10000);
    int i;

    int num_threads = 4;
    pthread_t th[num_threads];
    
    for (i = 0; i < num_threads; i++) {
        pthread_create(&th[i], NULL, worker, rb);
    }

    int num_fillers = 2;
    pthread_t filler_th[num_fillers];
    for (i = 0 ; i < num_fillers; i++) {
        pthread_create(&filler_th[i], NULL, filler, rb);
    }

    for (i = 0 ; i < num_fillers; i++) {
        pthread_join(filler_th[i], NULL);
    }

    /*
    printf("main thread sleeping\n");
    */

    for (i = 0 ; i < num_threads; i++) {
     //   pthread_cancel(th[i]);
        pthread_join(th[i], NULL);
    }
    printf("PORKODIO %d\n", ccnt);
    sleep(1);
    printf("%d - commit:%p -- tail:%p -- commit_next:%p --- tail_prev:%p --- reader:%p - reader_next:%p -- head:%p \n", rb->writes, rb->commit, rb->tail, rb->commit->next, rb->tail->prev, rb->reader, rb->reader->next, rb->head);

}
