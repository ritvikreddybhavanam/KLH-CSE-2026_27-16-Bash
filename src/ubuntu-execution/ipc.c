#define _POSIX_C_SOURCE 200809L
#include "ipc.h"
#include "logger.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define IPC_SOCKET_PATH "/tmp/kernel_monitor.sock"
#define IPC_SHM_NAME "/kernel_monitor_demo"

static volatile sig_atomic_t g_signal_flag = 0;

static void signal_handler(int sig) {
    if (sig == SIGUSR1 || sig == SIGUSR2 || sig == SIGINT || sig == SIGTERM) {
        g_signal_flag = 1;
    }
}

int demo_pipe_anon(void) {
    int pipefd[2];
    pid_t pid;
    char message[] = "Linux pipe demo: parent -> child -> parent";
    char buffer[256];

    if (pipe(pipefd) == -1) {
        perror("pipe");
        return -1;
    }

    pid = fork();
    if (pid == -1) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        ssize_t written = write(pipefd[1], message, strlen(message));
        if (written < 0) {
            perror("child write");
            _exit(1);
        }
        close(pipefd[1]);
        _exit(0);
    }

    close(pipefd[1]);
    ssize_t bytes = read(pipefd[0], buffer, sizeof(buffer) - 1);
    if (bytes < 0) {
        perror("parent read");
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        return -1;
    }
    buffer[bytes] = '\0';
    printf("[IPC] Anonymous pipe message: %s\n", buffer);
    close(pipefd[0]);
    waitpid(pid, NULL, 0);
    log_event("IPC_PIPE", getpid(), "monitor", buffer);
    return 0;
}

int demo_fifo(void) {
    const char *fifo_path = IPC_FIFO_PATH;
    pid_t pid;
    char message[] = "FIFO demo: writer -> reader";
    char buffer[256];

    if (mkfifo(fifo_path, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo");
        return -1;
    }

    pid = fork();
    if (pid == -1) {
        perror("fork");
        unlink(fifo_path);
        return -1;
    }

    if (pid == 0) {
        int fd = open(fifo_path, O_WRONLY);
        if (fd < 0) {
            perror("writer open");
            _exit(1);
        }
        if (write(fd, message, strlen(message)) < 0) {
            perror("writer write");
        }
        close(fd);
        _exit(0);
    }

    int fd = open(fifo_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("reader open");
        waitpid(pid, NULL, 0);
        unlink(fifo_path);
        return -1;
    }

    struct timespec ts = {0, 200000000L};
    nanosleep(&ts, NULL);
    ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes < 0) {
        perror("fifo read");
        close(fd);
        waitpid(pid, NULL, 0);
        unlink(fifo_path);
        return -1;
    }

    buffer[bytes] = '\0';
    printf("[IPC] FIFO message: %s\n", buffer);
    close(fd);
    waitpid(pid, NULL, 0);
    unlink(fifo_path);
    log_event("IPC_FIFO", getpid(), "monitor", buffer);
    return 0;
}

int demo_unix_socket(void) {
    int server_fd = -1;
    int client_fd = -1;
    int connection_fd = -1;
    struct sockaddr_un address;
    const char message[] = "AF_UNIX socket: client -> server";
    char buffer[128];
    pid_t child;

    unlink(IPC_SOCKET_PATH);
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, IPC_SOCKET_PATH, sizeof(address.sun_path) - 1);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind");
        close(server_fd);
        unlink(IPC_SOCKET_PATH);
        return -1;
    }
    if (listen(server_fd, 1) == -1) {
        perror("listen");
        close(server_fd);
        unlink(IPC_SOCKET_PATH);
        return -1;
    }

    child = fork();
    if (child == -1) {
        perror("fork");
        close(server_fd);
        unlink(IPC_SOCKET_PATH);
        return -1;
    }
    if (child == 0) {
        client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (client_fd == -1 || connect(client_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
            perror("socket/connect");
            if (client_fd != -1) close(client_fd);
            _exit(1);
        }
        if (send(client_fd, message, strlen(message), 0) == -1) {
            perror("send");
            close(client_fd);
            _exit(1);
        }
        close(client_fd);
        _exit(0);
    }

    connection_fd = accept(server_fd, NULL, NULL);
    if (connection_fd == -1) {
        perror("accept");
        close(server_fd);
        waitpid(child, NULL, 0);
        unlink(IPC_SOCKET_PATH);
        return -1;
    }
    ssize_t bytes = recv(connection_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes == -1) {
        perror("recv");
        close(connection_fd);
        close(server_fd);
        waitpid(child, NULL, 0);
        unlink(IPC_SOCKET_PATH);
        return -1;
    }
    buffer[bytes] = '\0';
    printf("[IPC] Unix domain socket message: %s\n", buffer);
    close(connection_fd);
    close(server_fd);
    waitpid(child, NULL, 0);
    unlink(IPC_SOCKET_PATH);
    log_event("IPC_UNIX_SOCKET", getpid(), "monitor", buffer);
    return 0;
}

int demo_shared_memory(void) {
    int fd = shm_open(IPC_SHM_NAME, O_CREAT | O_RDWR, 0600);
    const char message[] = "POSIX shared memory: writer -> mapped region -> reader";
    char *mapped;

    if (fd == -1) {
        perror("shm_open");
        return -1;
    }
    if (ftruncate(fd, (off_t)sizeof(message)) == -1) {
        perror("ftruncate");
        close(fd);
        shm_unlink(IPC_SHM_NAME);
        return -1;
    }
    mapped = mmap(NULL, sizeof(message), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        perror("mmap shared memory");
        close(fd);
        shm_unlink(IPC_SHM_NAME);
        return -1;
    }
    memcpy(mapped, message, sizeof(message));
    printf("[IPC] POSIX shared-memory message: %s\n", mapped);
    if (munmap(mapped, sizeof(message)) == -1) {
        perror("munmap");
        close(fd);
        shm_unlink(IPC_SHM_NAME);
        return -1;
    }
    close(fd);
    if (shm_unlink(IPC_SHM_NAME) == -1) {
        perror("shm_unlink");
        return -1;
    }
    return 0;
}

int demo_process_groups(void) {
    pid_t pid = getpid();
    pid_t ppid = getppid();
    pid_t pgrp = getpgrp();
    printf("[IPC] PID=%d PPID=%d PGID=%d\n", (int)pid, (int)ppid, (int)pgrp);

    pid_t child = fork();
    if (child == -1) {
        perror("fork");
        return -1;
    }
    if (child == 0) {
        if (setpgid(0, 0) == -1) {
            perror("setpgid");
            _exit(1);
        }
        printf("[IPC] Child PGID=%d SID=%d\n", (int)getpgrp(), (int)getsid(0));
        _exit(0);
    }

    waitpid(child, NULL, 0);
    log_event("IPC_GROUP", pid, "monitor", "Process group demonstration executed");
    return 0;
}

int demo_sessions(void) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        pid_t sid = setsid();
        if (sid == (pid_t)-1) {
            perror("setsid");
            _exit(1);
        }
        printf("[IPC] Child session ID=%d process group=%d\n", (int)sid, (int)getpgrp());
        _exit(0);
    }

    waitpid(pid, NULL, 0);
    log_event("IPC_SESSION", getpid(), "monitor", "Session concept demonstrated");
    return 0;
}

int demo_job_control(void) {
    printf("[IPC] Job control overview: a shell manages foreground and background jobs using process groups and sessions.\n");
    log_event("IPC_JOBCTL", getpid(), "monitor", "Explained process groups/session job control");
    return 0;
}

int run_signal_demo(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGUSR1, &sa, NULL) == -1 || sigaction(SIGUSR2, &sa, NULL) == -1 ||
        sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    printf("[IPC] Signal demo active. Send SIGUSR1 or SIGUSR2 to the process and observe the flag.\n");
    raise(SIGUSR1);
    if (g_signal_flag) {
        printf("[IPC] Handler recorded signal event.\n");
    }
    g_signal_flag = 0;
    return 0;
}

int run_ipc_demo(void) {
    if (demo_pipe_anon() != 0) return -1;
    if (demo_fifo() != 0) return -1;
    if (demo_unix_socket() != 0) return -1;
    if (demo_shared_memory() != 0) return -1;
    if (demo_process_groups() != 0) return -1;
    if (demo_sessions() != 0) return -1;
    if (demo_job_control() != 0) return -1;
    if (run_signal_demo() != 0) return -1;
    return 0;
}

int run_system_call_demo(void) {
    int fd = open("logs/system_call_demo.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    const char *msg = "User command -> shell -> exec -> syscall -> kernel -> service\n";
    if (write(fd, msg, strlen(msg)) < 0) {
        perror("write");
        close(fd);
        return -1;
    }

    close(fd);
    printf("[CO-1] System-call demo: user space writes to a file using open/write/close to reach kernel services.\n");
    unlink("logs/system_call_demo.txt");
    return 0;
}
