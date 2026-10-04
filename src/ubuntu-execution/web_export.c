#define _DEFAULT_SOURCE
#include "web_export.h"
#include "monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define JSON_TEMP_SUFFIX ".tmp"
#define COMMAND_OUTPUT_SIZE 32768

typedef struct {
    const char *name;
    const char *command;
    const char *output;
} CommandEvidence;

static void write_json_string(FILE *fp, const char *value) {
    const unsigned char *cursor = (const unsigned char *)(value ? value : "");
    fputc('"', fp);
    while (*cursor) {
        switch (*cursor) {
            case '"': fputs("\\\"", fp); break;
            case '\\': fputs("\\\\", fp); break;
            case '\b': fputs("\\b", fp); break;
            case '\f': fputs("\\f", fp); break;
            case '\n': fputs("\\n", fp); break;
            case '\r': fputs("\\r", fp); break;
            case '\t': fputs("\\t", fp); break;
            default:
                if (*cursor < 0x20) fprintf(fp, "\\u%04x", *cursor);
                else fputc(*cursor, fp);
        }
        cursor++;
    }
    fputc('"', fp);
}

static void capture_command(const char *command, char *output, size_t output_size) {
    FILE *pipe = popen(command, "r");
    size_t used = 0;
    if (!pipe) {
        output[0] = '\0';
        return;
    }
    while (used + 1 < output_size) {
        size_t count = fread(output + used, 1, output_size - used - 1, pipe);
        used += count;
        if (count == 0) break;
    }
    output[used] = '\0';
    pclose(pipe);
}

static double ticks_delta(unsigned long long current, unsigned long long previous,
                          unsigned long long total_delta) {
    if (current < previous || total_delta == 0) return 0.0;
    return (double)(current - previous) * 100.0 / (double)total_delta;
}

static void write_cpu_breakdown(FILE *fp, const MonitorState *state) {
    const CpuStats *current = &state->current_sys.cpu_stats;
    const CpuStats *previous = &state->prev_sys.cpu_stats;
    unsigned long long total_current = current->user + current->nice + current->system +
        current->idle + current->iowait + current->irq + current->softirq + current->steal;
    unsigned long long total_previous = previous->user + previous->nice + previous->system +
        previous->idle + previous->iowait + previous->irq + previous->softirq + previous->steal;
    unsigned long long total_delta = total_current > total_previous ? total_current - total_previous : 0;
    unsigned long long user_current = current->user + current->nice;
    unsigned long long user_previous = previous->user + previous->nice;
    unsigned long long system_current = current->system + current->irq + current->softirq;
    unsigned long long system_previous = previous->system + previous->irq + previous->softirq;

    fprintf(fp, "\"cpu_user_pct\":%.2f,\"cpu_system_pct\":%.2f,"
                "\"cpu_idle_pct\":%.2f,\"cpu_iowait_pct\":%.2f,",
            ticks_delta(user_current, user_previous, total_delta),
            ticks_delta(system_current, system_previous, total_delta),
            ticks_delta(current->idle, previous->idle, total_delta),
            ticks_delta(current->iowait, previous->iowait, total_delta));
}

static int write_snapshot(const char *path, const MonitorState *state) {
    char temp_path[1024];
    char timestamp[32];
    char ps_output[COMMAND_OUTPUT_SIZE];
    char top_output[COMMAND_OUTPUT_SIZE];
    char free_output[4096];
    char uptime_output[2048];
    char proc_stat_output[8192];
    char meminfo_output[8192];
    char tree_output[8192];
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);
    FILE *fp;
    int state_counts[256] = {0};
    int i;

    if (snprintf(temp_path, sizeof(temp_path), "%s%s", path, JSON_TEMP_SUFFIX) >= (int)sizeof(temp_path)) {
        return -1;
    }
    if (utc) strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", utc);
    else snprintf(timestamp, sizeof(timestamp), "unknown");

    capture_command("ps -eo pid,ppid,stat,comm,%cpu,%mem --sort=-%cpu | head -n 16", ps_output, sizeof(ps_output));
    capture_command("top -b -n 1", top_output, sizeof(top_output));
    capture_command("free -h", free_output, sizeof(free_output));
    capture_command("uptime", uptime_output, sizeof(uptime_output));
    capture_command("cat /proc/stat", proc_stat_output, sizeof(proc_stat_output));
    capture_command("cat /proc/meminfo", meminfo_output, sizeof(meminfo_output));
    capture_command("pstree -p", tree_output, sizeof(tree_output));

    for (i = 0; i < state->current_procs.count; i++) {
        unsigned char code = (unsigned char)state->current_procs.processes[i].state;
        state_counts[code]++;
    }

    fp = fopen(temp_path, "w");
    if (!fp) return -1;

    fprintf(fp, "{\"mode\":\"LIVE\",\"timestamp\":");
    write_json_string(fp, timestamp);
    fprintf(fp, ",\"system\":{\"cpu_usage_pct\":%.2f,",
            state->current_sys.cpu_usage_pct);
    write_cpu_breakdown(fp, state);
    fprintf(fp, "\"memory_total_mb\":%.2f,\"memory_used_mb\":%.2f,"
                "\"memory_free_mb\":%.2f,\"memory_available_mb\":%.2f,"
                "\"memory_cached_mb\":%.2f,\"swap_total_mb\":%.2f,"
                "\"swap_used_mb\":%.2f,\"swap_free_mb\":%.2f,"
                "\"memory_usage_pct\":%.2f,\"uptime_seconds\":%ld,"
                "\"load_1m\":%.2f,\"load_5m\":%.2f,\"load_15m\":%.2f,"
                "\"process_count\":%d,\"running_processes\":%d},",
            state->current_sys.total_ram_gb * 1024.0,
            state->current_sys.used_ram_gb * 1024.0,
            state->current_sys.free_ram_gb * 1024.0,
            state->current_sys.available_ram_gb * 1024.0,
            state->current_sys.cached_ram_gb * 1024.0,
            state->current_sys.swap_total_gb * 1024.0,
            state->current_sys.swap_used_gb * 1024.0,
            state->current_sys.swap_free_gb * 1024.0,
            state->current_sys.memory_usage_pct,
            state->current_sys.uptime_seconds,
            state->current_sys.load_1m,
            state->current_sys.load_5m,
            state->current_sys.load_15m,
            state->current_procs.count,
            state->current_sys.running_processes);

    fputs("\"process_states\":{", fp);
    {
        const char *states = "RSDTtZXI";
        int first = 1;
        while (*states) {
            char key[2] = {*states, '\0'};
            if (!first) fputc(',', fp);
            write_json_string(fp, key);
            fprintf(fp, ":%d", state_counts[(unsigned char)*states]);
            first = 0;
            states++;
        }
    }
    fputs("},\"processes\":[", fp);
    for (i = 0; i < state->current_procs.count; i++) {
        const ProcessInfo *proc = &state->current_procs.processes[i];
        ProcessDetails details;
        long ticks_per_second = sysconf(_SC_CLK_TCK);
        long start_epoch = 0;
        if (ticks_per_second > 0 && state->current_sys.uptime_seconds >= 0) {
            start_epoch = now - state->current_sys.uptime_seconds +
                (long)(proc->start_time / (unsigned long long)ticks_per_second);
        }
        if (i) fputc(',', fp);
        fprintf(fp, "{\"pid\":%d,\"ppid\":%d,\"state\":", proc->pid, proc->ppid);
        {
            char process_state[2] = {proc->state, '\0'};
            write_json_string(fp, process_state);
        }
        fputs(",\"name\":", fp);
        write_json_string(fp, proc->name);
        fprintf(fp, ",\"cpu_percent\":%.2f,\"memory_percent\":%.2f,"
                    "\"memory_mb\":%.2f,\"start_time_epoch\":%ld,",
                proc->cpu_usage, proc->memory_pct, proc->memory_usage_mb, start_epoch);
        if (read_process_details(proc->pid, state->current_sys.total_ram_gb, &details) == 0) {
            fprintf(fp, "\"threads\":%d,\"command_line\":", details.threads);
            write_json_string(fp, details.cmdline);
        } else {
            fputs("\"threads\":0,\"command_line\":", fp);
            write_json_string(fp, proc->name);
        }
                fprintf(fp, ",\"sources\":{\"identity_state_parent_cpu_start\":\"/proc/%d/stat\","
                                        "\"resident_memory\":\"/proc/%d/statm\","
                                        "\"threads_uid_gid\":\"/proc/%d/status\","
                                        "\"command_line\":\"/proc/%d/cmdline\"}}",
                                proc->pid, proc->pid, proc->pid, proc->pid);
    }
        fputs("],\"sources\":{\"cpu\":\"/proc/stat\",\"memory\":\"/proc/meminfo\","
                    "\"uptime\":\"/proc/uptime\",\"load\":\"/proc/loadavg\","
                    "\"process_directory\":\"/proc/[numeric PID]/\","
                    "\"process_identity_state_parent_cpu_start\":\"/proc/[PID]/stat\","
                    "\"process_resident_memory\":\"/proc/[PID]/statm\","
                    "\"process_threads_uid_gid\":\"/proc/[PID]/status\","
                    "\"process_command_line\":\"/proc/[PID]/cmdline\"},"
                    "\"source_map\":["
                    "{\"metric\":\"cpu_usage_pct and cpu breakdown\",\"source\":\"/proc/stat\",\"calculation\":\"two-snapshot CPU tick deltas\"},"
                    "{\"metric\":\"memory and swap\",\"source\":\"/proc/meminfo\",\"calculation\":\"MemTotal, MemAvailable, MemFree, Cached, SReclaimable, SwapTotal, SwapFree\"},"
                    "{\"metric\":\"uptime_seconds\",\"source\":\"/proc/uptime\",\"calculation\":\"first uptime value in seconds\"},"
                    "{\"metric\":\"load averages and running/total process counts\",\"source\":\"/proc/loadavg\",\"calculation\":\"first three load values and running/total field\"},"
                    "{\"metric\":\"process PID, name, state, PPID, CPU ticks, start time\",\"source\":\"/proc/[PID]/stat\",\"calculation\":\"CPU percent from process tick delta divided by aggregate /proc/stat tick delta\"},"
                    "{\"metric\":\"process resident memory\",\"source\":\"/proc/[PID]/statm\",\"calculation\":\"resident pages multiplied by sysconf(_SC_PAGESIZE)\"},"
                    "{\"metric\":\"process thread count, UID and GID\",\"source\":\"/proc/[PID]/status\",\"calculation\":\"parse Threads, Uid and Gid fields\"},"
                    "{\"metric\":\"process command line\",\"source\":\"/proc/[PID]/cmdline\",\"calculation\":\"NUL-separated arguments converted to spaces\"}],"
                    "\"process_list_map\":["
                    "{\"metric\":\"process_count\",\"source\":\"numeric directories under /proc\",\"calculation\":\"count successfully parsed process entries\"},"
                    "{\"metric\":\"process state counts\",\"source\":\"/proc/[PID]/stat state field\",\"calculation\":\"count each process state from the collected process list\"},"
                    "{\"metric\":\"process hierarchy\",\"source\":\"/proc/[PID]/stat PID and PPID fields\",\"calculation\":\"connect each process to the process whose PID equals its PPID\"}],"
          "\"commands\":[", fp);

    {
        CommandEvidence commands[] = {
            {"Process list", "ps -eo pid,ppid,stat,comm,%cpu,%mem --sort=-%cpu | head -n 16", ps_output},
            {"CPU summary", "top -b -n 1", top_output},
            {"Memory", "free -h", free_output},
            {"Uptime and load", "uptime", uptime_output},
            {"CPU counters", "cat /proc/stat", proc_stat_output},
            {"Memory counters", "cat /proc/meminfo", meminfo_output},
            {"Process tree", "pstree -p", tree_output}
        };
        size_t command_count = sizeof(commands) / sizeof(commands[0]);
        size_t command_index;
        for (command_index = 0; command_index < command_count; command_index++) {
            if (command_index) fputc(',', fp);
            fputs("{\"name\":", fp);
            write_json_string(fp, commands[command_index].name);
            fputs(",\"command\":", fp);
            write_json_string(fp, commands[command_index].command);
            fputs(",\"output\":", fp);
            write_json_string(fp, commands[command_index].output);
            fputc('}', fp);
        }
    }
    fputs("]}\n", fp);

    if (fclose(fp) != 0 || rename(temp_path, path) != 0) {
        remove(temp_path);
        return -1;
    }
    return 0;
}

int run_web_export(const char *output_path) {
    MonitorState state;
    if (!output_path || !*output_path) return -1;
    monitor_init(&state, 2);
    monitor_step(&state);
    fprintf(stderr, "Writing live Linux data to %s every 2 seconds (Ctrl+C to stop).\n", output_path);
    while (1) {
        sleep(2);
        monitor_step(&state);
        if (write_snapshot(output_path, &state) != 0) {
            perror("Unable to write dashboard data");
            return -1;
        }
    }
}