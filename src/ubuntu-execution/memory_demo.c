#define _POSIX_C_SOURCE 200809L
#include "memory_demo.h"
#include "logger.h"
#include "system.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

static void print_meminfo_summary(void) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        perror("/proc/meminfo");
        return;
    }

    char line[256];
    printf("[MEM] /proc/meminfo:\n");
    for (int i = 0; i < 6 && fgets(line, sizeof(line), fp); i++) {
        printf("      %s", line);
    }
    fclose(fp);
}

int demo_memory_overview(void) {
    SystemInfo info;
    if (read_system_info(&info) != 0) {
        fprintf(stderr, "Failed to read system memory info.\n");
        return -1;
    }

    printf("[MEM] Total RAM: %.2f GB | Used: %.2f GB | Available: %.2f GB\n",
           info.total_ram_gb, info.used_ram_gb, info.available_ram_gb);
    print_meminfo_summary();
    log_event("MEMORY_INFO", getpid(), "monitor", "System memory snapshot displayed");
    return 0;
}

int demo_copy_on_write(void) {
    int *shared = malloc(sizeof(int) * 4);
    if (!shared) {
        perror("malloc");
        return -1;
    }

    for (int i = 0; i < 4; i++) shared[i] = 10 + i;

    pid_t child = fork();
    if (child == -1) {
        perror("fork");
        free(shared);
        return -1;
    }

    if (child == 0) {
        shared[0] = 999;
        printf("[MEM] Child wrote to shared buffer: %d %d %d %d\n", shared[0], shared[1], shared[2], shared[3]);
        free(shared);
        _exit(0);
    }

    waitpid(child, NULL, 0);
    printf("[MEM] Parent still sees original buffer: %d %d %d %d\n", shared[0], shared[1], shared[2], shared[3]);
    free(shared);
    log_event("MEM_COW", getpid(), "monitor", "Copy-on-write demo executed");
    return 0;
}

int demo_malloc_cycle(void) {
    void *ptr = malloc(1024 * 1024);
    if (!ptr) {
        perror("malloc");
        return -1;
    }

    memset(ptr, 0x5A, 1024 * 1024);
    printf("[MEM] malloc() allocated 1 MB and initialized successfully.\n");

    void *grown = realloc(ptr, 4 * 1024 * 1024);
    if (!grown) {
        perror("realloc");
        free(ptr);
        return -1;
    }
    ptr = grown;
    memset(ptr, 0xA5, 4 * 1024 * 1024);
    printf("[MEM] realloc() expanded memory to 4 MB.\n");

    free(ptr);
    log_event("MEM_ALLOC", getpid(), "monitor", "Dynamic allocation demo executed");
    return 0;
}

int demo_memory_error_isolation(void) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        volatile int *bad = NULL;
        *bad = 42;
        _exit(1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    printf("[MEM] Isolated crash child exited with status %d after invalid write.\n", status);
    return 0;
}

int demo_address_translation(void) {
    long page_size = sysconf(_SC_PAGESIZE);
    uintptr_t virtual_address;
    unsigned long page_number;
    unsigned long offset;
    unsigned long frame;
    int value = 42;

    if (page_size <= 0) {
        fprintf(stderr, "Unable to determine page size.\n");
        return -1;
    }
    virtual_address = (uintptr_t)&value;
    page_number = (unsigned long)(virtual_address / (uintptr_t)page_size);
    offset = (unsigned long)(virtual_address % (uintptr_t)page_size);
    frame = page_number % 16UL;
    printf("[MEM] Address translation model: virtual=%p -> page=%lu offset=%lu -> page-table frame=%lu -> physical offset=%lu\n",
           (void *)&value, page_number, offset, frame, offset);
    printf("[MEM] The frame is a controlled teaching model; Linux page tables perform the real translation.\n");
    return 0;
}

int demo_tlb_locality(void) {
    static volatile unsigned char pages[64 * 4096];
    unsigned long sum = 0;

    for (size_t i = 0; i < sizeof(pages); i++) {
        pages[i] = (unsigned char)i;
    }
    for (int repeat = 0; repeat < 1000; repeat++) {
        for (size_t i = 0; i < sizeof(pages); i += 64) {
            sum += pages[i];
        }
    }
    printf("[MEM] TLB locality demo touched %zu bytes with repeated nearby accesses (checksum=%lu).\n",
           sizeof(pages), sum);
    printf("[MEM] User space cannot read hardware TLB entries directly; this demonstrates locality that makes TLB caching useful.\n");
    return 0;
}

int demo_page_faults(void) {
    struct rusage before;
    struct rusage after;
    const size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
    const size_t page_count = 32;
    volatile unsigned char *memory = mmap(NULL, page_count * page_size,
                                          PROT_READ | PROT_WRITE,
                                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (memory == MAP_FAILED || page_size == 0) {
        perror("mmap/page size");
        return -1;
    }
    if (getrusage(RUSAGE_SELF, &before) == -1) {
        perror("getrusage before");
        munmap((void *)memory, page_count * page_size);
        return -1;
    }
    for (size_t page = 0; page < page_count; page++) {
        memory[page * page_size] = (unsigned char)page;
    }
    if (getrusage(RUSAGE_SELF, &after) == -1) {
        perror("getrusage after");
        munmap((void *)memory, page_count * page_size);
        return -1;
    }
    printf("[MEM] Page-fault statistics: minor +%ld, major +%ld after touching %zu pages.\n",
           after.ru_minflt - before.ru_minflt, after.ru_majflt - before.ru_majflt, page_count);
    munmap((void *)memory, page_count * page_size);
    return 0;
}

int demo_demand_paging(void) {
    const size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
    const size_t page_count = 16;
    unsigned char *memory = malloc(page_count * page_size);

    if (!memory || page_size == 0) {
        perror("malloc/page size");
        free(memory);
        return -1;
    }
    printf("[MEM] Demand-paging demo reserved %zu pages; physical pages are faulted in as each page is first accessed.\n",
           page_count);
    for (size_t page = 0; page < page_count; page++) {
        memory[page * page_size] = 1;
        printf("[MEM] First access to page %zu completed.\n", page);
    }
    free(memory);
    return 0;
}

int run_memory_demo(void) {
    if (demo_memory_overview() != 0) return -1;
    if (demo_malloc_cycle() != 0) return -1;
    if (demo_address_translation() != 0) return -1;
    if (demo_tlb_locality() != 0) return -1;
    if (demo_page_faults() != 0) return -1;
    if (demo_demand_paging() != 0) return -1;
    if (demo_copy_on_write() != 0) return -1;
    if (demo_memory_error_isolation() != 0) return -1;
    return 0;
}
