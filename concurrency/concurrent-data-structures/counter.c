#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct counter_s {
  int val;
  pthread_mutex_t lock;
} counter_t;

void init(counter_t *counter) {
  counter->val = 0;
  pthread_mutex_init(&counter->lock, NULL);
}

void* increment(void *arg) {
  counter_t *counter = (counter_t *)arg;
  pthread_mutex_lock(&counter->lock);
  counter->val++;
  pthread_mutex_unlock(&counter->lock);
  return NULL;
}

void* decrement(void *arg) {
  counter_t *counter = (counter_t *)arg;
  pthread_mutex_lock(&counter->lock);
  counter->val--;
  pthread_mutex_unlock(&counter->lock);
  return NULL;
}

void* get(void *arg) {
  counter_t *counter = (counter_t *)arg;
  pthread_mutex_lock(&counter->lock);
  printf("Counter value: %d\n", counter->val);
  pthread_mutex_unlock(&counter->lock);
  return NULL;
}

int main() {
  counter_t counter;
  init(&counter);

  pthread_t thread1, thread2;

  if (pthread_create(&thread1, NULL, increment, &counter) != 0) {
    perror("Failed to create thread1");
    exit(1);
  }

  if (pthread_create(&thread2, NULL, decrement, &counter) != 0) {
    perror("Failed to create thread2");
    exit(1);
  }

  pthread_join(thread1, NULL);
  pthread_join(thread2, NULL);

  get(&counter);

  pthread_mutex_destroy(&counter.lock);
  return 0;
}
