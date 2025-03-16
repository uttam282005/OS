#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  printf("Parent process (pid:%d)\n", (int)getpid());
  int rc = fork();
  if (rc < 0)
    fprintf(stderr, "Process creation failed...\n");
  else if (rc == 0)
    printf("Child process (pid:%d)\n", (int)getpid());
  else
    printf("Parent of %d, (pid:%d)\n", rc, (int)getpid());
  return 0;
}
