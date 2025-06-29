#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct node_s {
  int val;
  struct node_s *next;
}