#include "system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int read_cpu_stats(CpuStats *stats) {
    if (!stats) return -1;
    memset(stats, 0, sizeof(CpuStats));

    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) {
        perror("Error opening /proc/stat");
        return -1;
    }

    char line[512];
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    if (strncmp(line, "cpu ", 4) != 0) {
        return -1;
    }

    int ret = sscanf(line + 4, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                     &stats->user, &stats->nice, &stats->system, &stats->idle,
                     &stats->iowait, &stats->irq, &stats->softirq, &stats->steal,
                     &stats->guest, &stats->guest_nice);

    if (ret < 4) return -1; // Minimum requirement: user, nice, system, idle
    return 0;
}

double calculate_cpu_usage_pct(const CpuStats *prev, const CpuStats *curr) {
    if (!curr) return 0.0;

    unsigned long long idle1 = prev ? (prev->idle + prev->iowait) : 0;
    unsigned long long idle2 = curr->idle + curr->iowait;

    unsigned long long non_idle1 = prev ? (prev->user + prev->nice + prev->system +
                                           prev->irq + prev->softirq + prev->steal) : 0;
    unsigned long long non_idle2 = curr->user + curr->nice + curr->system +
                                   curr->irq + curr->softirq + curr->steal;

    unsigned long long total1 = idle1 + non_idle1;
    unsigned long long total2 = idle2 + non_idle2;

    if (prev == NULL || total2 <= total1) {
        // Fallback calculation since system boot if no previous sample
        if (total2 == 0) return 0.0;
        return ((double)(total2 - idle2) / (double)total2) * 100.0;
    }

    unsigned long long totald = total2 - total1;
    unsigned long long idled = idle2 - idle1;

    if (totald == 0) return 0.0;
    double cpu_pct = ((double)(totald - idled) / (double)totald) * 100.0;
    if (cpu_pct < 0.0) cpu_pct = 0.0;
    if (cpu_pct > 100.0) cpu_pct = 100.0;

    return cpu_pct;
}

static int read_meminfo(SystemInfo *info) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        perror("Error opening /proc/meminfo");
        return -1;
    }

    char line[256];
    long mem_total_kb = 0;
    long mem_free_kb = 0;
    long mem_available_kb = 0;
    long cached_kb = 0;
    long reclaimable_kb = 0;
    long swap_total_kb = 0;
    long swap_free_kb = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line + 9, "%ld", &mem_total_kb);
        } else if (strncmp(line, "MemFree:", 8) == 0) {
            sscanf(line + 8, "%ld", &mem_free_kb);
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line + 13, "%ld", &mem_available_kb);
        } else if (strncmp(line, "Cached:", 7) == 0) {
            sscanf(line + 7, "%ld", &cached_kb);
        } else if (strncmp(line, "SReclaimable:", 13) == 0) {
            sscanf(line + 13, "%ld", &reclaimable_kb);
        } else if (strncmp(line, "SwapTotal:", 10) == 0) {
            sscanf(line + 10, "%ld", &swap_total_kb);
        } else if (strncmp(line, "SwapFree:", 9) == 0) {
            sscanf(line + 9, "%ld", &swap_free_kb);
        }
    }
    fclose(fp);

    if (mem_total_kb <= 0) return -1;

    if (mem_available_kb == 0) {
        mem_available_kb = mem_free_kb; // Fallback for older Linux kernels
    }

    long used_kb = mem_total_kb - mem_available_kb;
    if (used_kb < 0) used_kb = 0;

    info->total_ram_gb = mem_total_kb / (1024.0 * 1024.0);
    info->free_ram_gb = mem_free_kb / (1024.0 * 1024.0);
    info->available_ram_gb = mem_available_kb / (1024.0 * 1024.0);
    info->cached_ram_gb = (cached_kb + reclaimable_kb) / (1024.0 * 1024.0);
    info->used_ram_gb = used_kb / (1024.0 * 1024.0);
    info->memory_usage_pct = ((double)used_kb / (double)mem_total_kb) * 100.0;
    info->swap_total_gb = swap_total_kb / (1024.0 * 1024.0);
    info->swap_free_gb = swap_free_kb / (1024.0 * 1024.0);
    info->swap_used_gb = (swap_total_kb - swap_free_kb) / (1024.0 * 1024.0);

    return 0;
}

static int read_uptime(SystemInfo *info) {
    FILE *fp = fopen("/proc/uptime", "r");
    if (!fp) {
        perror("Error opening /proc/uptime");
        return -1;
    }

    double uptime_sec = 0.0;
    if (fscanf(fp, "%lf", &uptime_sec) != 1) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    info->uptime_seconds = (long)uptime_sec;
    info->uptime_hours = (int)(info->uptime_seconds / 3600);
    info->uptime_minutes = (int)((info->uptime_seconds % 3600) / 60);
    info->uptime_secs = (int)(info->uptime_seconds % 60);

    return 0;
}

static int read_loadavg(SystemInfo *info) {
    FILE *fp = fopen("/proc/loadavg", "r");
    if (!fp) {
        perror("Error opening /proc/loadavg");
        return -1;
    }

    char line[128];
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    int running = 0, total = 0;
    if (sscanf(line, "%lf %lf %lf %d/%d", 
               &info->load_1m, &info->load_5m, &info->load_15m, 
               &running, &total) >= 3) {
        info->running_processes = running;
        info->total_processes = total;
    }

    return 0;
}

int read_system_info(SystemInfo *info) {
    if (!info) return -1;

    // Preserve cpu_stats from last read for differential calculation
    CpuStats prev_cpu = info->cpu_stats;
    int has_prev = (info->cpu_stats.user | info->cpu_stats.idle) > 0;

    memset(info, 0, sizeof(SystemInfo));

    if (read_cpu_stats(&info->cpu_stats) == 0) {
        info->cpu_usage_pct = calculate_cpu_usage_pct(has_prev ? &prev_cpu : NULL, &info->cpu_stats);
    }

    if (read_meminfo(info) != 0) return -1;
    if (read_uptime(info) != 0) return -1;
    if (read_loadavg(info) != 0) return -1;

    return 0;
}

void print_system_info(const SystemInfo *info) {
    if (!info) return;

    printf("============================================================\n");
    printf("                 LINUX KERNEL MONITOR                       \n");
    printf("============================================================\n\n");
    printf("SYSTEM INFORMATION\n");
    printf("------------------------------------------------------------\n");
    printf("CPU Usage       : %.1f%%\n", info->cpu_usage_pct);
    printf("Memory Usage    : %.1f%% (Used: %.2f GB / Total: %.2f GB)\n",
           info->memory_usage_pct, info->used_ram_gb, info->total_ram_gb);
    printf("Available RAM   : %.2f GB (Free: %.2f GB)\n",
           info->available_ram_gb, info->free_ram_gb);
    printf("Uptime          : %02d:%02d:%02d (%ld seconds)\n",
           info->uptime_hours, info->uptime_minutes, info->uptime_secs, info->uptime_seconds);
    printf("Load Average    : %.2f  %.2f  %.2f\n",
           info->load_1m, info->load_5m, info->load_15m);
    printf("Processes       : Total: %d | Running: %d\n",
           info->total_processes, info->running_processes);
    printf("------------------------------------------------------------\n");
}
