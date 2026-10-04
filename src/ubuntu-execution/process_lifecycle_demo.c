#define _POSIX_C_SOURCE 200809L
#include "process.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int demo_process_lifecycle(void) {
    printf("[CO-2] Process lifecycle demonstration: READY -> RUNNING -> WAITING/SLEEPING -> READY -> TERMINATED\n");
    printf("[CO-2] A child process is created, enters RUNNING, waits briefly, then exits.\n");

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        printf("[CO-2] Child process: READY -> RUNNING (PID=%d PPID=%d)\n", getpid(), getppid());
        sleep(1);
        printf("[CO-2] Child process: WAITING/SLEEPING -> READY -> TERMINATED\n");
        _exit(0);
    }

    printf("[CO-2] Parent process waiting for child termination.\n");
    waitpid(pid, NULL, 0);
    printf("[CO-2] Parent observed child termination; lifecycle demo complete.\n");
    return 0;
}
