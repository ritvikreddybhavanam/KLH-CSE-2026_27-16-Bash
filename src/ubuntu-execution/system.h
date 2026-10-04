#ifndef SYSTEM_H
#define SYSTEM_H

typedef struct {
    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
    unsigned long long steal;
    unsigned long long guest;
    unsigned long long guest_nice;
} CpuStats;

typedef struct {
    CpuStats cpu_stats;
    double cpu_usage_pct;

    double total_ram_gb;
    double used_ram_gb;
    double free_ram_gb;
    double available_ram_gb;
    double cached_ram_gb;
    double memory_usage_pct;
    double swap_total_gb;
    double swap_free_gb;
    double swap_used_gb;

    long uptime_seconds;
    int uptime_hours;
    int uptime_minutes;
    int uptime_secs;

    double load_1m;
    double load_5m;
    double load_15m;

    int total_processes;
    int running_processes;
} SystemInfo;

/* Reads raw CPU time counters from /proc/stat. Returns 0 on success, -1 on error. */
int read_cpu_stats(CpuStats *stats);

/* Calculates CPU usage percentage between two snapshots (or single snapshot if prev is NULL). */
double calculate_cpu_usage_pct(const CpuStats *prev, const CpuStats *curr);

/* Reads system-wide metrics from /proc/stat, /proc/meminfo, /proc/uptime, and /proc/loadavg.
   Returns 0 on success, -1 on failure. */
int read_system_info(SystemInfo *info);

/* Prints formatted system information dashboard header. */
void print_system_info(const SystemInfo *info);

#endif /* SYSTEM_H */
