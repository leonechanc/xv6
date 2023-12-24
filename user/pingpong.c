#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void) {
    int pipefd_ping[2];
    int pipefd_pong[2];
    int pid;
    char msg = 0;
    
    pipe(pipefd_ping); // create pipes
    pipe(pipefd_pong);

    pid = fork();

    if (pid < 0) {
        printf("fork err.\n");
        exit(1);
    } else if (pid == 0) {
        /* receive ping and print */
        close(pipefd_ping[1]);
        if (read(pipefd_ping[0], &msg, 1) != 1) {
            printf("%d: read err.\n", getpid());
            exit(1);
        }
        // printf("%c\n", msg);
        printf("%d: received ping\n", getpid());
        close(pipefd_ping[0]);

        /* send pong */ 
        msg = 'c';
        close(pipefd_pong[0]);
        write(pipefd_pong[1], &msg, 1);
        close(pipefd_pong[1]);

        exit(0);
    } else {
        msg = 'p';
        /* send ping */
        close(pipefd_ping[0]);
        write(pipefd_ping[1], &msg, 1);
        close(pipefd_ping[1]);

        /* receive pong */ 
        close(pipefd_pong[1]);
        if (read(pipefd_pong[0], &msg, 1) != 1) {
            printf("%d: read err.\n", getpid());
            exit(1);
        }
        // printf("%c\n", msg);
        printf("%d: received pong\n", getpid());
        close(pipefd_pong[0]);

        exit(0);
    }
}