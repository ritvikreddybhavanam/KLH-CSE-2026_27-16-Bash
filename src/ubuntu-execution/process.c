#define _DEFAULT_SOURCE
#include "process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>

static int is_numeric(const char *str) {
    if (!str || *str == '\0') return 0;
    while (*str) {
        if (!isdigit((unsigned char)*str)) return 0;
        str++;
    }
    return 1;
}

static int parse_process_stat(int pid, ProcessInfo *proc) {
    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        // Process disappeared before reading
        return -1;
    }

    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    // /proc/<pid>/stat format:
    // pid (comm) state ppid ...
    char *open_paren = strchr(buffer, '(');
    char *close_paren = strrchr(buffer, ')');

    if (!open_paren || !close_paren || close_paren < open_paren) {
        return -1;
    }

    proc->pid = pid;

    // Extract process name inside (comm)
    size_t name_len = close_paren - open_paren - 1;
    if (name_len >= MAX_NAME_LEN) {
        name_len = MAX_NAME_LEN - 1;
    }
    strncpy(proc->name, open_paren + 1, name_len);
    proc->name[name_len] = '\0';

    // Parse values after closing parenthesis:
    // state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt utime stime ...
    char state;
    int ppid;
    unsigned long utime = 0, stime = 0;
    unsigned long long start_time = 0;

    int fields_read = sscanf(close_paren + 2,
        "%c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d %*d %*d %*d %*d %llu",
        &state, &ppid, &utime, &stime, &start_time);

    if (fields_read < 2) {
        return -1;
    }

    proc->state = state;
    proc->ppid = ppid;
    proc->utime = utime;
    proc->stime = stime;
    proc->start_time = start_time;

    return 0;
}

#include <unistd.h>

static int read_process_memory(int pid, double total_ram_gb, double *mem_mb, double *mem_pct) {
    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/statm", pid);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }

    long total_pages = 0, resident_pages = 0;
    if (fscanf(fp, "%ld %ld", &total_pages, &resident_pages) != 2) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    long page_size_bytes = sysconf(_SC_PAGESIZE);
    if (page_size_bytes <= 0) page_size_bytes = 4096;

    double bytes = (double)resident_pages * (double)page_size_bytes;
    *mem_mb = bytes / (1024.0 * 1024.0);

    if (total_ram_gb > 0.0) {
        double total_ram_bytes = total_ram_gb * 1024.0 * 1024.0 * 1024.0;
        *mem_pct = (bytes / total_ram_bytes) * 100.0;
    } else {
        *mem_pct = 0.0;
    }

    return 0;
}

int read_process_list(ProcessList *list, double total_ram_gb) {
    if (!list) return -1;
    list->count = 0;

    DIR *dir = opendir("/proc");
    if (!dir) {
        perror("Error opening /proc directory");
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && list->count < MAX_PROCESSES) {
        if (entry->d_type == DT_DIR || entry->d_type == DT_UNKNOWN) {
            if (is_numeric(entry->d_name)) {
                int pid = atoi(entry->d_name);
                if (pid > 0) {
                    ProcessInfo proc;
                    memset(&proc, 0, sizeof(ProcessInfo));
                    if (parse_process_stat(pid, &proc) == 0) {
                        read_process_memory(pid, total_ram_gb, &proc.memory_usage_mb, &proc.memory_pct);
                        list->processes[list->count++] = proc;
                    }
                }
            }
        }
    }
    closedir(dir);
    return 0;
}

void calculate_process_list_cpu(const ProcessList *prev, ProcessList *curr,
                                const CpuStats *prev_cpu, const CpuStats *curr_cpu) {
    if (!curr || !curr_cpu) return;

    if (!prev || !prev_cpu) {
        // Single snapshot: set default 0.0% until 2nd snapshot
        for (int i = 0; i < curr->count; i++) {
            curr->processes[i].cpu_usage = 0.0;
        }
        return;
    }

    unsigned long long sys1 = prev_cpu->user + prev_cpu->nice + prev_cpu->system +
                              prev_cpu->idle + prev_cpu->iowait + prev_cpu->irq +
                              prev_cpu->softirq + prev_cpu->steal;
    unsigned long long sys2 = curr_cpu->user + curr_cpu->nice + curr_cpu->system +
                              curr_cpu->idle + curr_cpu->iowait + curr_cpu->irq +
                              curr_cpu->softirq + curr_cpu->steal;

    if (sys2 <= sys1) return;
    double sys_diff = (double)(sys2 - sys1);

    for (int i = 0; i < curr->count; i++) {
        ProcessInfo *p_curr = &curr->processes[i];
        p_curr->cpu_usage = 0.0;

        // Search for matching PID in prev
        for (int j = 0; j < prev->count; j++) {
            const ProcessInfo *p_prev = &prev->processes[j];
            if (p_prev->pid == p_curr->pid) {
                unsigned long proc1 = p_prev->utime + p_prev->stime;
                unsigned long proc2 = p_curr->utime + p_curr->stime;
                if (proc2 >= proc1) {
                    double proc_diff = (double)(proc2 - proc1);
                    p_curr->cpu_usage = (proc_diff / sys_diff) * 100.0;
                }
                break;
            }
        }
    }
}

static int compare_by_pid(const void *a, const void *b) {
    const ProcessInfo *p1 = (const ProcessInfo *)a;
    const ProcessInfo *p2 = (const ProcessInfo *)b;
    return p1->pid - p2->pid;
}

static int compare_by_cpu(const void *a, const void *b) {
    const ProcessInfo *p1 = (const ProcessInfo *)a;
    const ProcessInfo *p2 = (const ProcessInfo *)b;
    if (p2->cpu_usage > p1->cpu_usage) return 1;
    if (p2->cpu_usage < p1->cpu_usage) return -1;
    return p1->pid - p2->pid;
}

static int compare_by_mem(const void *a, const void *b) {
    const ProcessInfo *p1 = (const ProcessInfo *)a;
    const ProcessInfo *p2 = (const ProcessInfo *)b;
    if (p2->memory_pct > p1->memory_pct) return 1;
    if (p2->memory_pct < p1->memory_pct) return -1;
    return p1->pid - p2->pid;
}

void sort_process_list(ProcessList *list, SortCriterion criterion) {
    if (!list || list->count <= 1) return;

    switch (criterion) {
        case SORT_BY_CPU:
            qsort(list->processes, list->count, sizeof(ProcessInfo), compare_by_cpu);
            break;
        case SORT_BY_MEM:
            qsort(list->processes, list->count, sizeof(ProcessInfo), compare_by_mem);
            break;
        case SORT_BY_PID:
        default:
            qsort(list->processes, list->count, sizeof(ProcessInfo), compare_by_pid);
            break;
    }
}

static int str_case_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    if (*needle == '\0') return 1;

    for (; *haystack != '\0'; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h != '\0' && *n != '\0' && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++;
            n++;
        }
        if (*n == '\0') return 1;
    }
    return 0;
}

void search_process_list(const ProcessList *src, ProcessList *dest, const char *query) {
    if (!src || !dest) return;
    dest->count = 0;
    if (!query || strlen(query) == 0) {
        *dest = *src;
        return;
    }

    int search_pid = atoi(query);

    for (int i = 0; i < src->count && dest->count < MAX_PROCESSES; i++) {
        const ProcessInfo *p = &src->processes[i];
        if ((search_pid > 0 && p->pid == search_pid) || str_case_contains(p->name, query)) {
            dest->processes[dest->count++] = *p;
        }
    }
}

void print_process_list(const ProcessList *list, int max_display) {
    if (!list) return;

    printf("\nPROCESS MONITOR (Displaying %d of %d processes)\n",
           max_display < list->count ? max_display : list->count, list->count);
    printf("----------------------------------------------------------------------\n");
    printf("%-8s %-8s %-6s %-8s %-8s %-20s\n", "PID", "PPID", "STATE", "CPU%", "MEM%", "NAME");
    printf("----------------------------------------------------------------------\n");

    int limit = (max_display > 0 && max_display < list->count) ? max_display : list->count;
    for (int i = 0; i < limit; i++) {
        const ProcessInfo *p = &list->processes[i];
        printf("%-8d %-8d %-6c %-8.1f %-8.1f %-20s\n",
               p->pid, p->ppid, p->state, p->cpu_usage, p->memory_pct, p->name);
    }
    printf("----------------------------------------------------------------------\n");
}
static void print_tree_node(const ProcessList *list, int pid, const char *prefix, int is_last, int *visited, int depth) {
    if (depth > 20 || !list || !visited) return;

    int idx = -1;
    for (int i = 0; i < list->count; i++) {
        if (list->processes[i].pid == pid) {
            idx = i;
            break;
        }
    }
    if (idx < 0 || visited[idx]) return;
    visited[idx] = 1;

    const ProcessInfo *p = &list->processes[idx];

    if (depth == 0) {
        printf("%s (PID %d)\n", p->name, p->pid);
    } else {
        printf("%s%s%s (PID %d)\n", prefix, is_last ? "└── " : "├── ", p->name, p->pid);
    }

    // Collect child processes
    int child_count = 0;
    int children[MAX_PROCESSES];
    for (int i = 0; i < list->count; i++) {
        if (list->processes[i].ppid == pid && !visited[i]) {
            children[child_count++] = list->processes[i].pid;
        }
    }

    char next_prefix[512];
    for (int i = 0; i < child_count; i++) {
        int last_child = (i == child_count - 1);
        if (depth == 0) {
            snprintf(next_prefix, sizeof(next_prefix), "%s", "");
        } else {
            snprintf(next_prefix, sizeof(next_prefix), "%s%s", prefix, is_last ? "    " : "│   ");
        }
        print_tree_node(list, children[i], next_prefix, last_child, visited, depth + 1);
    }
}

void print_process_tree(const ProcessList *list) {
    if (!list || list->count == 0) return;

    printf("\nPROCESS HIERARCHY TREE\n");
    printf("----------------------------------------------------------------------\n");

    int visited[MAX_PROCESSES] = {0};

    // First find root processes (PPID 0 or parent PPID not present in process list)
    for (int i = 0; i < list->count; i++) {
        int ppid = list->processes[i].ppid;
        int parent_exists = 0;
        if (ppid > 0) {
            for (int j = 0; j < list->count; j++) {
                if (list->processes[j].pid == ppid) {
                    parent_exists = 1;
                    break;
                }
            }
        }
        if (ppid == 0 || !parent_exists) {
            print_tree_node(list, list->processes[i].pid, "", 1, visited, 0);
        }
    }

    // Print any remaining unvisited processes
    for (int i = 0; i < list->count; i++) {
        if (!visited[i]) {
            print_tree_node(list, list->processes[i].pid, "", 1, visited, 0);
        }
    }

    printf("----------------------------------------------------------------------\n");
}

int read_process_details(int pid, double total_ram_gb, ProcessDetails *details) {
    if (!details || pid <= 0) return -1;
    memset(details, 0, sizeof(ProcessDetails));

    ProcessInfo basic_info;
    if (parse_process_stat(pid, &basic_info) != 0) {
        return -1;
    }

    details->pid = basic_info.pid;
    details->ppid = basic_info.ppid;
    details->state = basic_info.state;
    snprintf(details->name, sizeof(details->name), "%s", basic_info.name);
    read_process_memory(pid, total_ram_gb, &details->memory_usage_mb, &details->memory_pct);

    // Read cmdline
    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    FILE *fp = fopen(path, "rb");
    if (fp) {
        size_t bytes_read = fread(details->cmdline, 1, sizeof(details->cmdline) - 1, fp);
        fclose(fp);
        if (bytes_read > 0) {
            details->cmdline[bytes_read] = '\0';
            // Replace internal null characters with spaces
            for (size_t i = 0; i < bytes_read - 1; i++) {
                if (details->cmdline[i] == '\0') {
                    details->cmdline[i] = ' ';
                }
            }
        } else {
            strncpy(details->cmdline, details->name, sizeof(details->cmdline) - 1);
        }
    } else {
        strncpy(details->cmdline, details->name, sizeof(details->cmdline) - 1);
    }

    // Read status file for Uid, Gid, Threads
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    fp = fopen(path, "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "Uid:", 4) == 0) {
                sscanf(line + 4, "%d", &details->uid);
            } else if (strncmp(line, "Gid:", 4) == 0) {
                sscanf(line + 4, "%d", &details->gid);
            } else if (strncmp(line, "Threads:", 8) == 0) {
                sscanf(line + 8, "%d", &details->threads);
            }
        }
        fclose(fp);
    }

    return 0;
}

void print_process_details(const ProcessDetails *details) {
    if (!details) return;

    printf("\nPROCESS DETAILS INSPECTION (PID: %d)\n", details->pid);
    printf("----------------------------------------------------------------------\n");
    printf("Process Name    : %s\n", details->name);
    printf("PID             : %d\n", details->pid);
    printf("Parent PID (PPID): %d\n", details->ppid);
    printf("State           : %c (%s)\n", details->state,
           details->state == 'R' ? "Running" :
           details->state == 'S' ? "Sleeping (Interruptible)" :
           details->state == 'D' ? "Disk Sleep (Uninterruptible)" :
           details->state == 'Z' ? "Zombie" :
           details->state == 'T' ? "Stopped" : "Other");
    printf("User ID (UID)   : %d\n", details->uid);
    printf("Group ID (GID)  : %d\n", details->gid);
    printf("Threads         : %d\n", details->threads);
    printf("Memory Usage    : %.2f MB (%.2f%% of Total RAM)\n",
           details->memory_usage_mb, details->memory_pct);
    printf("Command Line    : %s\n", details->cmdline);
    printf("----------------------------------------------------------------------\n");
}
