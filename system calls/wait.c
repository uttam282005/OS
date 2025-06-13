#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  printf("Parent process (pid:%d)\n", (int)getpid());
  int rc = fork();
  if (rc < 0)
    fprintf(stderr, "Process creation failed...\n");
  else if (rc == 0) {
    printf("Child process (pid:%d)\n", (int)getpid());
  }
  else {
    int status;
    int rc_wait = wait(&status); // returns after child is finished
    printf("waiting for %d, (pid:%d)\nstatus of child: %d\n", rc_wait,
           (int)getpid(), status);
  }
  return 0;
}
