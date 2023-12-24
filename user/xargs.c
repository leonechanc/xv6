#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

#define MAXLEN 64

int main(int argc, char *argv[]) {
    char *cmd = argv[1];
    char *execArgs[MAXARG];
    char addiArgs[MAXARG][MAXLEN];
    char input_char;
    int pid = 0;
    int numAddiArgs;
    int i = 0;
    int j = 0;

    if (argc < 2) {
        fprintf(2, "Usage: xargs <cmd> [param] ...\n");
    }

    // /* deal with stander input */
    memset(addiArgs, 0, MAXARG * MAXLEN);
    while (read(0, &input_char, 1) > 0) {
        // if ((input_char != ' ') || (input_char != '\n')) {
        if (input_char != '\n') {
            addiArgs[i][j++] = input_char;
        } else {
            addiArgs[i++][j] = 0;
            j = 0;
        }
    }

    numAddiArgs = i;

    /* deal with the additional arguments */
    for (i = 0; i < argc - 1; i++) {
        execArgs[i] = argv[i + 1];
        // printf("execArgs[%d]: %s\n", i, execArgs[i]);
    }

    // printf("argc = %d\t numAddiArgs = %d\n", argc, numAddiArgs);
    /* execute command */
    for (i = 0; i < numAddiArgs; i++) {
        // printf("======%d======\n", i);
        // printf("addiArgs[%d]: %s\n", i, addiArgs[i]);
        pid = fork();
        execArgs[argc - 1] = addiArgs[i];
        if (pid < 0) {
            fprintf(2, "fork err.\n");
            exit(1);
        } else if (pid == 0) {
            exec(cmd, execArgs);
            fprintf(2, "exec xargs err.\n");
            exit(1);
        } else {
            wait((int *)0);
        }
    }

    exit(0);
}