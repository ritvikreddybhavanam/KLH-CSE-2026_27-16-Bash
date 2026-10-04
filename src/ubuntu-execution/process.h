#ifndef PROCESS_H
#define PROCESS_H

#include <stddef.h>
#include "system.h"

#define MAX_PROCESSES 1024
#define MAX_NAME_LEN 256

typedef struct {
    int pid;
    int ppid;
    char name[MAX_NAME_LEN];
    char state;
    unsigned long utime;
    unsigned long stime;
    unsigned long long start_time;
    double cpu_usage;
    double memory_usage_mb;
    double memory_pct;
} ProcessInfo;

typedef struct {
    ProcessInfo processes[MAX_PROCESSES];
    int count;
} ProcessList;

typedef enum {
    SORT_BY_PID,
    SORT_BY_CPU,
    SORT_BY_MEM
} SortCriterion;

/* Scans /proc directory and populates process list.
   Computes memory usage using system total RAM.
   Returns 0 on success, -1 on failure. */
int read_process_list(ProcessList *list, double total_ram_gb);

/* Calculates per-process CPU usage percentages using snapshot differentials. */
void calculate_process_list_cpu(const ProcessList *prev, ProcessList *curr, const CpuStats *prev_cpu, const CpuStats *curr_cpu);

/* Sorts process list in-place based on specified criterion. */
void sort_process_list(ProcessList *list, SortCriterion criterion);

/* Filters source process list into dest matching PID or process name substring. */
void search_process_list(const ProcessList *src, ProcessList *dest, const char *query);

/* Prints process list table summary with CPU% and MEM%. */
void print_process_list(const ProcessList *list, int max_display);

typedef struct {
    int pid;
    int ppid;
    char name[256];
    char cmdline[512];
    char state;
    int uid;
    int gid;
    int threads;
    double cpu_usage;
    double memory_usage_mb;
    double memory_pct;
} ProcessDetails;

/* Displays process tree hierarchy based on PID -> PPID relationship. */
void print_process_tree(const ProcessList *list);

/* Reads detailed process metadata from /proc/<pid>/status and /proc/<pid>/cmdline. */
int read_process_details(int pid, double total_ram_gb, ProcessDetails *details);

/* Prints formatted process details inspection report. */
void print_process_details(const ProcessDetails *details);

/* Educational process lifecycle demonstration with READY -> RUNNING -> WAITING -> READY -> TERMINATED. */
int demo_process_lifecycle(void);

#endif /* PROCESS_H */
