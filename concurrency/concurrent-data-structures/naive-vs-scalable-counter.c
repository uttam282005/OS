#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

#define NUMCPUS 4
#define NUMTHREADS 16
#define UPDATES 1000000
#define THRESHOLD 10000

typedef struct {
    int val;
    pthread_mutex_t lock;
} naive_counter_t;

typedef struct {
    int global;
    pthread_mutex_t glock;
    int local[NUMCPUS];
    pthread_mutex_t llock[NUMCPUS];
    int threshold;
} scalable_counter_t;

// ---------- Naive Counter ----------
void naive_init(naive_counter_t *c) {
    c->val = 0;
    pthread_mutex_init(&c->lock, NULL);
}

void naive_update(naive_counter_t *c, int amt) {
    pthread_mutex_lock(&c->lock);
    c->val += amt;
    pthread_mutex_unlock(&c->lock);
}

int naive_get(naive_counter_t *c) {
    pthread_mutex_lock(&c->lock);
    int v = c->val;
    pthread_mutex_unlock(&c->lock);
    return v;
}

// ---------- Scalable Counter ----------
void scalable_init(scalable_counter_t *c, int threshold) {
    c->threshold = threshold;
    c->global = 0;
    pthread_mutex_init(&c->glock, NULL);
    for (int i = 0; i < NUMCPUS; i++) {
        c->local[i] = 0;
        pthread_mutex_init(&c->llock[i], NULL);
    }
}

void scalable_update(scalable_counter_t *c, int threadID, int amt) {
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

int scalable_get(scalable_counter_t *c) {
    pthread_mutex_lock(&c->glock);
    int val = c->global;
    pthread_mutex_unlock(&c->glock);
    return val;
}

naive_counter_t naive_counter;
scalable_counter_t scalable_counter;

// ---------- Worker Threads ----------
void *naive_worker(void *arg) {
    for (int i = 0; i < UPDATES; i++) {
        naive_update(&naive_counter, 1);
    }
    return NULL;
}

void *scalable_worker(void *arg) {
    int threadID = *(int *)arg;
    for (int i = 0; i < UPDATES; i++) {
        scalable_update(&scalable_counter, threadID, 1);
    }
    free(arg);
    return NULL;
}

// ---------- Timer ----------
double get_time_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main() {
    pthread_t threads[NUMTHREADS];
    double start, end;

    // ---------- Naive Benchmark ----------
    naive_init(&naive_counter);
    start = get_time_sec();
    for (int i = 0; i < NUMTHREADS; i++) {
        pthread_create(&threads[i], NULL, naive_worker, NULL);
    }
    for (int i = 0; i < NUMTHREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    end = get_time_sec();
    printf("Naive Counter: Value = %d, Time = %.4f sec\n", naive_get(&naive_counter), end - start);

    // ---------- Scalable Benchmark ----------
    scalable_init(&scalable_counter, THRESHOLD);
    start = get_time_sec();
    for (int i = 0; i < NUMTHREADS; i++) {
        int *id = malloc(sizeof(int));
        *id = i;
        pthread_create(&threads[i], NULL, scalable_worker, id);
    }
    for (int i = 0; i < NUMTHREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // Final flush
    for (int i = 0; i < NUMCPUS; i++) {
        pthread_mutex_lock(&scalable_counter.llock[i]);
        pthread_mutex_lock(&scalable_counter.glock);
        scalable_counter.global += scalable_counter.local[i];
        pthread_mutex_unlock(&scalable_counter.glock);
        pthread_mutex_unlock(&scalable_counter.llock[i]);
    }

    end = get_time_sec();
    printf("Scalable Counter: Value = %d, Time = %.4f sec\n", scalable_get(&scalable_counter), end - start);

    return 0;
}
