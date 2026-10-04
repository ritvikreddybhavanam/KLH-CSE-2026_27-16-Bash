#ifndef LOGGER_H
#define LOGGER_H

#define DEFAULT_LOG_FILE "logs/monitor.log"

/* Initializes logger file path. Returns 0 on success, -1 on warning/failure. */
int logger_init(const char *filepath);

/* Appends timestamped event log line. Safe if file cannot be opened. */
int log_event(const char *event_type, int pid, const char *process_name, const char *extra_msg);

/* Closes logger resources. */
void logger_close(void);

#endif /* LOGGER_H */
