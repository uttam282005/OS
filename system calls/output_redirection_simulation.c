#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

// exec transforsm the currenlty running process into a different one (wc in
// this case)
int main(int argc, char *argv[]) {
  printf("Parent process (pid:%d)\n", (int)getpid());
  int rc = fork();
  if (rc < 0)
    fprintf(stderr, "Process creation failed...\n");
  else if (rc == 0) {
    printf("Child process (pid:%d)\n", (int)getpid());
    // close the STDOUT file
    close(STDOUT_FILENO);
    open("./output.txt", 1);
    char *argv[3];
    argv[0] = "wc";
    argv[1] = "fork.c";
    argv[2] = NULL;
    execvp(argv[0], argv);
    printf("This line has been overridden by the static data of the wc "
           "program.\n");
  } else {
    int status;
    int rc_wait = wait(&status); // returns after child is finished
    printf("waiting for %d, (pid:%d)\nstatus of child: %d\n", rc_wait,
           (int)getpid(), status);
  }
  return 0;
}
