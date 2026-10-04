#ifndef MONITOR_H
#define MONITOR_H

#include "system.h"
#include "process.h"

#define MAX_EVENTS 50

typedef enum {
    EVENT_PROCESS_CREATED,
    EVENT_PROCESS_TERMINATED
} EventType;

typedef struct {
    EventType type;
    int pid;
    char name[256];
    char timestamp[64];
} ProcessEvent;

typedef struct {
    SystemInfo current_sys;
    SystemInfo prev_sys;
    ProcessList current_procs;
    ProcessList prev_procs;
    ProcessEvent events[MAX_EVENTS];
    int event_count;
    int refresh_interval_sec;
    int is_running;
    int has_previous_snapshot;
} MonitorState;

/* Initializes monitoring state with refresh interval in seconds. */
void monitor_init(MonitorState *state, int refresh_interval_sec);

/* Performs a single snapshot capture, metrics update, and event detection. */
void monitor_step(MonitorState *state);

/* Detects process creation and termination events between snapshots. */
void detect_process_events(MonitorState *state);

/* Prints recent process events on the dashboard. */
void print_recent_events(const MonitorState *state, int max_display);

/* Runs continuous monitoring loop until interrupted by signal or user exit. */
void monitor_run_live(MonitorState *state);

/* Clears the terminal screen cleanly using ANSI escape sequences. */
void clear_screen(void);

#endif /* MONITOR_H */
