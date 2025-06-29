#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define NUMCPUS 4
#define NUMTHREADS 8
#define UPDATES 1000000
#define THRESHOLD 100

typedef struct __counter_t {
    int global;                         // Global count
    pthread_mutex_t glock;             // Global lock
    int local[NUMCPUS];                // Local counters
    pthread_mutex_t llock[NUMCPUS];    // Local locks
    int threshold;                     // Threshold for flush
} counter_t;

// Initialize counter
void init(counter_t *c, int threshold) {
    c->threshold = threshold;
    c->global = 0;
    pthread_mutex_init(&c->glock, NULL);
    for (int i = 0; i < NUMCPUS; i++) {
        c->local[i] = 0;
        pthread_mutex_init(&c->llock[i], NULL);
    }
}

// Update counter
void update(counter_t *c, int threadID, int amt) {
    int cpu = threadID % NUMCPUS;
    pthread_mutex_lock(&c->llock[cpu]);
    c->local[cpu] += amt;
    if (c->local[cpu] >= c->threshold) {
        pthread_mutex_lock(&c->glock);
        c->global += c->local[cpu];
        pthread_mutex_unlock(&c->glock);
        c->local[cpu] = 0;
    }
    pthread_mutex_unlock(&c->llock[cpu]);
}

// Get global count (approximate)
int get(counter_t *c) {
    pthread_mutex_lock(&c->glock);
    int val = c->global;
    pthread_mutex_unlock(&c->glock);
    return val;
}

// Shared counter instance
counter_t counter;

// Thread routine
void *worker(void *arg) {
    int threadID = *(int *)arg;
    for (int i = 0; i < UPDATES; i++) {
        update(&counter, threadID, 1);
    }
    free(arg); // prevent memory leak
    return NULL;
}

int main() {
    pthread_t threads[NUMTHREADS];
    init(&counter, THRESHOLD);

    // Spawn threads
    for (int i = 0; i < NUMTHREADS; i++) {
        int *id = malloc(sizeof(int));
        *id = i;
        pthread_create(&threads[i], NULL, worker, id);
    }

    // Wait for threads
    for (int i = 0; i < NUMTHREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // Final flush: sum any remaining local counts to global
    for (int i = 0; i < NUMCPUS; i++) {
        pthread_mutex_lock(&counter.llock[i]);
        pthread_mutex_lock(&counter.glock);
        counter.global += counter.local[i];
        pthread_mutex_unlock(&counter.glock);
        pthread_mutex_unlock(&counter.llock[i]);
    }

    printf("Final count = %d (expected = %d)\n", get(&counter), NUMTHREADS * UPDATES);
    return 0;
}
