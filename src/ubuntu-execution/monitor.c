#define _DEFAULT_SOURCE
#include "monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static volatile int g_keep_running = 1;

static void handle_sigint(int sig) {
    (void)sig;
    g_keep_running = 0;
}

void clear_screen(void) {
    // Clear screen and reset cursor position to top-left
    printf("\033[2J\033[H");
    fflush(stdout);
}

#include <time.h>

static void get_current_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (tm_info) {
        strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        snprintf(buffer, size, "UNKNOWN");
    }
}

#include "logger.h"

static void add_event(MonitorState *state, EventType type, int pid, const char *name) {
    if (!state) return;

    ProcessEvent ev;
    ev.type = type;
    ev.pid = pid;
    strncpy(ev.name, name ? name : "unknown", sizeof(ev.name) - 1);
    ev.name[sizeof(ev.name) - 1] = '\0';
    get_current_timestamp(ev.timestamp, sizeof(ev.timestamp));

    if (state->event_count < MAX_EVENTS) {
        state->events[state->event_count++] = ev;
    } else {
        // Shift left to drop oldest event
        memmove(&state->events[0], &state->events[1], sizeof(ProcessEvent) * (MAX_EVENTS - 1));
        state->events[MAX_EVENTS - 1] = ev;
    }

    // Log event to file
    if (type == EVENT_PROCESS_CREATED) {
        log_event("PROCESS_CREATED", pid, ev.name, NULL);
    } else {
        log_event("PROCESS_TERMINATED", pid, ev.name, NULL);
    }
}

void detect_process_events(MonitorState *state) {
    if (!state || !state->has_previous_snapshot) return;

    // Detect Created Processes (In current but not in previous)
    for (int i = 0; i < state->current_procs.count; i++) {
        const ProcessInfo *curr_p = &state->current_procs.processes[i];
        int found = 0;
        for (int j = 0; j < state->prev_procs.count; j++) {
            if (state->prev_procs.processes[j].pid == curr_p->pid) {
                found = 1;
                break;
            }
        }
        if (!found) {
            add_event(state, EVENT_PROCESS_CREATED, curr_p->pid, curr_p->name);
        }
    }

    // Detect Terminated Processes (In previous but not in current)
    for (int i = 0; i < state->prev_procs.count; i++) {
        const ProcessInfo *prev_p = &state->prev_procs.processes[i];
        int found = 0;
        for (int j = 0; j < state->current_procs.count; j++) {
            if (state->current_procs.processes[j].pid == prev_p->pid) {
                found = 1;
                break;
            }
        }
        if (!found) {
            add_event(state, EVENT_PROCESS_TERMINATED, prev_p->pid, prev_p->name);
        }
    }
}

void print_recent_events(const MonitorState *state, int max_display) {
    if (!state) return;

    printf("\nEVENTS LOG (Recent %d events)\n", state->event_count);
    printf("----------------------------------------------------------------------\n");
    if (state->event_count == 0) {
        printf("No process creation or termination events detected yet.\n");
    } else {
        int start = (max_display > 0 && state->event_count > max_display) ? (state->event_count - max_display) : 0;
        for (int i = start; i < state->event_count; i++) {
            const ProcessEvent *ev = &state->events[i];
            if (ev->type == EVENT_PROCESS_CREATED) {
                printf("[%s] [+] Process Created:    PID %-6d (Name: %s)\n",
                       ev->timestamp, ev->pid, ev->name);
            } else {
                printf("[%s] [-] Process Terminated: PID %-6d (Name: %s)\n",
                       ev->timestamp, ev->pid, ev->name);
            }
        }
    }
    printf("----------------------------------------------------------------------\n");
}

void monitor_init(MonitorState *state, int refresh_interval_sec) {
    if (!state) return;
    memset(state, 0, sizeof(MonitorState));
    state->refresh_interval_sec = (refresh_interval_sec > 0) ? refresh_interval_sec : 2;
    state->is_running = 1;
    state->has_previous_snapshot = 0;
    logger_init(NULL);
    log_event("MONITOR_STARTED", -1, NULL, "Linux Kernel Monitor service initialized");
}

void monitor_step(MonitorState *state) {
    if (!state) return;

    // Shift current to previous
    if (state->has_previous_snapshot) {
        state->prev_sys = state->current_sys;
        state->prev_procs = state->current_procs;
    }

    // Capture system info
    if (read_system_info(&state->current_sys) != 0) {
        fprintf(stderr, "Warning: Failed reading system info snapshot\n");
    }

    // Capture process list using current total RAM
    if (read_process_list(&state->current_procs, state->current_sys.total_ram_gb) != 0) {
        fprintf(stderr, "Warning: Failed reading process list snapshot\n");
    }

    // Compute per-process CPU percentages if previous snapshot exists
    if (state->has_previous_snapshot) {
        calculate_process_list_cpu(&state->prev_procs, &state->current_procs,
                                   &state->prev_sys.cpu_stats, &state->current_sys.cpu_stats);
        detect_process_events(state);
    } else {
        calculate_process_list_cpu(NULL, &state->current_procs, NULL, &state->current_sys.cpu_stats);
        state->has_previous_snapshot = 1;
    }
}

void monitor_run_live(MonitorState *state) {
    if (!state) return;

    g_keep_running = 1;
    signal(SIGINT, handle_sigint);

    // Initial snapshot baseline
    monitor_step(state);

    while (g_keep_running && state->is_running) {
        // Sleep for refresh interval
        sleep(state->refresh_interval_sec);
        if (!g_keep_running) break;

        // Take update step
        monitor_step(state);

        // Render dashboard
        clear_screen();
        print_system_info(&state->current_sys);
        print_process_list(&state->current_procs, 15);
        print_recent_events(state, 5);

        printf("\nRefreshing every %d seconds... (Press Ctrl+C to stop)\n",
               state->refresh_interval_sec);
        fflush(stdout);
    }

    printf("\n[!] Live Monitor stopped.\n");
}
