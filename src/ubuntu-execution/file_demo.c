#define _POSIX_C_SOURCE 200809L
#include "file_demo.h"
#include "logger.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int pid;
    double cpu_usage;
    double memory_usage;
    char state;
    long timestamp;
} MonitoringRecord;

static int write_all(int fd, const void *buffer, size_t length) {
    const char *bytes = (const char *)buffer;
    size_t written = 0;
    while (written < length) {
        ssize_t result = write(fd, bytes + written, length - written);
        if (result < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (result == 0) return -1;
        written += (size_t)result;
    }
    return 0;
}

static int read_all(int fd, void *buffer, size_t length) {
    char *bytes = (char *)buffer;
    size_t read_count = 0;
    while (read_count < length) {
        ssize_t result = read(fd, bytes + read_count, length - read_count);
        if (result < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (result == 0) return -1;
        read_count += (size_t)result;
    }
    return 0;
}

int demo_file_descriptors(void) {
    int fd = open("logs/fd_demo.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    const char *msg = "FD demo via open/write/close\n";
    if (write(fd, msg, strlen(msg)) < 0) {
        perror("write");
        close(fd);
        return -1;
    }

    printf("[FS] STDIN=%d STDOUT=%d STDERR=%d\n", STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);
    printf("[FS] File descriptor demo wrote %zu bytes using fd=%d\n", strlen(msg), fd);
    close(fd);
    unlink("logs/fd_demo.txt");
    log_event("FILE_FD", getpid(), "monitor", "File descriptor demo executed");
    return 0;
}

int demo_buffered_io(void) {
    FILE *fp = fopen("logs/buffered_demo.txt", "w+");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    fprintf(fp, "buffered demo: fprintf() and fgets() operate through stdio buffers\n");
    rewind(fp);
    char line[256];
    if (fgets(line, sizeof(line), fp)) {
        printf("[FS] Buffered I/O read: %s", line);
    }
    fclose(fp);
    unlink("logs/buffered_demo.txt");
    return 0;
}

int demo_unbuffered_io(void) {
    int fd = open("logs/unbuffered_demo.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    const char *msg = "unbuffered demo\n";
    if (write(fd, msg, strlen(msg)) == -1) {
        perror("write");
        close(fd);
        return -1;
    }
    close(fd);

    fd = open("logs/unbuffered_demo.txt", O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    char buffer[128];
    ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes < 0) {
        perror("read");
        close(fd);
        return -1;
    }
    buffer[bytes] = '\0';
    printf("[FS] Unbuffered read: %s", buffer);
    close(fd);
    unlink("logs/unbuffered_demo.txt");
    return 0;
}

int demo_stat_and_dir(void) {
    struct stat st;
    if (stat("logs", &st) != 0) {
        perror("stat");
        return -1;
    }

    printf("[FS] logs inode=%ld size=%lld mode=%o\n", (long)st.st_ino,
           (long long)st.st_size, st.st_mode & 0777);

    DIR *dir = opendir("/proc");
    if (!dir) {
        perror("opendir");
        return -1;
    }

    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL && count < 5) {
        printf("[FS] /proc entry: %s\n", entry->d_name);
        count++;
    }
    closedir(dir);
    return 0;
}

int demo_mmap_io(void) {
    const char *path = "logs/mmap_demo.txt";
    FILE *fp = fopen(path, "w+");
    if (!fp) {
        perror("fopen");
        return -1;
    }
    fprintf(fp, "hello from mmap\n");
    fclose(fp);

    int fd = open(path, O_RDWR);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        perror("fstat");
        close(fd);
        return -1;
    }

    char *mapped = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return -1;
    }

    printf("[FS] mmap read text: %s", mapped);
    munmap(mapped, st.st_size);
    close(fd);
    unlink(path);
    return 0;
}

int demo_lseek(void) {
    const char *path = "logs/lseek_demo.txt";
    const char text[] = "ABC";
    char buffer[sizeof(text)] = {0};
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open lseek demo");
        return -1;
    }
    if (write_all(fd, text, sizeof(text) - 1) != 0) {
        perror("write lseek demo");
        close(fd);
        unlink(path);
        return -1;
    }
    if (lseek(fd, 0, SEEK_SET) == (off_t)-1 || read_all(fd, buffer, sizeof(text) - 1) != 0) {
        perror("lseek/read");
        close(fd);
        unlink(path);
        return -1;
    }
    printf("[FS] lseek(SEEK_SET) repositioned the file offset; read back: %s\n", buffer);
    close(fd);
    unlink(path);
    return 0;
}

int demo_structured_records(void) {
    const char *path = "logs/monitor_records.bin";
    MonitoringRecord written = {getpid(), 12.5, 3.25, 'R', time(NULL)};
    MonitoringRecord read_record;
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open structured records");
        return -1;
    }
    if (write_all(fd, &written, sizeof(written)) != 0 || lseek(fd, 0, SEEK_SET) == (off_t)-1 ||
        read_all(fd, &read_record, sizeof(read_record)) != 0) {
        perror("structured record I/O");
        close(fd);
        unlink(path);
        return -1;
    }
    printf("[FS] Structured record: PID=%d CPU=%.1f MEM=%.2f STATE=%c TIME=%ld\n",
           read_record.pid, read_record.cpu_usage, read_record.memory_usage,
           read_record.state, read_record.timestamp);
    close(fd);
    unlink(path);
    return 0;
}

int demo_vfs_mounts(void) {
    FILE *fp = fopen("/proc/self/mountinfo", "r");
    char line[512];
    struct stat root_info;
    if (!fp) {
        perror("/proc/self/mountinfo");
        return -1;
    }
    if (!fgets(line, sizeof(line), fp)) {
        perror("mountinfo read");
        fclose(fp);
        return -1;
    }
    fclose(fp);
    if (stat("/proc", &root_info) != 0) {
        perror("stat /proc");
        return -1;
    }
    printf("[FS] VFS/mount demo: /proc inode=%ld; mountinfo sample: %s", (long)root_info.st_ino, line);
    printf("[FS] VFS presents mounted filesystems through one namespace; no filesystem is modified or implemented here.\n");
    return 0;
}

int run_file_demo(void) {
    if (demo_file_descriptors() != 0) return -1;
    if (demo_buffered_io() != 0) return -1;
    if (demo_unbuffered_io() != 0) return -1;
    if (demo_stat_and_dir() != 0) return -1;
    if (demo_mmap_io() != 0) return -1;
    if (demo_lseek() != 0) return -1;
    if (demo_structured_records() != 0) return -1;
    if (demo_vfs_mounts() != 0) return -1;
    return 0;
}
