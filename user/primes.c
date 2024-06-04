#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define SIZE    35
#define PIPE_READ   0
#define PIPE_WRITE  1

void concurrent_prime_sieve(int *pParentPipe){
    int pipefd[2];
    int prime;
    int pid;
    int num;

    close(pParentPipe[PIPE_WRITE]);
    if (read(pParentPipe[PIPE_READ], &prime, sizeof(prime)) == 0) { // return zero means there are no more numbers in the pipe
        exit(0);
    }
    printf("prime %d\n", prime);

    pipe(pipefd); // create a new pipe for communication

    pid = fork();
    if (pid < 0) {
        fprintf(2, "fork err.\n");
        close(pipefd[PIPE_READ]); // in case...
        close(pipefd[PIPE_READ]);
        exit(1);
    } else if (pid == 0) {
        concurrent_prime_sieve(pipefd); // recursive
    } else {
        close(pipefd[PIPE_READ]);
        while (read(pParentPipe[PIPE_READ], &num, sizeof(num)) != 0) {
            if (num % prime == 0) {
                continue;
            }
            write(pipefd[PIPE_WRITE], &num, sizeof(num));
        }
        close(pipefd[PIPE_WRITE]);
        wait((int *)0);
        exit(0);
    }
}

int main(void) {
    int pipefd[2];
    int pid;
    int i;

    pipe(pipefd);

    pid = fork();

    if (pid < 0) {
        close(pipefd[PIPE_READ]);
        close(pipefd[PIPE_WRITE]);
        fprintf(2, "fork err.\n");
    } else if (pid == 0) {
        concurrent_prime_sieve(pipefd);
    } else {
        close(pipefd[PIPE_READ]);
        for (i = 2; i <= 35; i++) {
            write(pipefd[PIPE_WRITE], &i, sizeof(i)); // send numbers to the pipe
        }
        close(pipefd[PIPE_WRITE]);
        wait((int *)0); // wait until all the child process is done
    }

    exit(0);
}