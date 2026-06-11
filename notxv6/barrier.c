#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int nthread = 1;

struct barrier {
    pthread_mutex_t barrier_mutex;
    pthread_cond_t barrier_cond;
    int nthread;
    int round;
} bstate;

static void barrier_init(void) {
    assert(pthread_mutex_init(&bstate.barrier_mutex, NULL) == 0);
    assert(pthread_cond_init(&bstate.barrier_cond, NULL) == 0);
    bstate.nthread = 0;
    bstate.round = 0;
}

static void barrier(void) {
    int my_round;

    pthread_mutex_lock(&bstate.barrier_mutex);
    my_round = bstate.round;
    bstate.nthread++;

    if (bstate.nthread == nthread) {
        bstate.nthread = 0;
        bstate.round++;
        pthread_cond_broadcast(&bstate.barrier_cond);
    } else {
        while (my_round == bstate.round) {
            pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
        }
    }

    pthread_mutex_unlock(&bstate.barrier_mutex);
}

static void *thread(void *xa) {
    for (int i = 0; i < 20000; i++) {
        int t = bstate.round;
        assert(i == t);
        barrier();
        usleep(random() % 100);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    pthread_t *tha;
    void *value;

    if (argc < 2) {
        fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
        exit(-1);
    }
    nthread = atoi(argv[1]);
    tha = malloc(sizeof(pthread_t) * nthread);
    srandom(0);

    barrier_init();

    for (long i = 0; i < nthread; i++) {
        assert(pthread_create(&tha[i], NULL, thread, (void *)i) == 0);
    }
    for (long i = 0; i < nthread; i++) {
        assert(pthread_join(tha[i], &value) == 0);
    }
    printf("OK; passed\n");
}
