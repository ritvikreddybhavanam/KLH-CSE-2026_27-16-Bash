#define _DEFAULT_SOURCE
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

static char g_log_filepath[256] = DEFAULT_LOG_FILE;
static int g_log_warned = 0;

static void ensure_logs_directory(void) {
    // Attempt creating logs directory if missing
#ifdef _WIN32
    mkdir("logs");
#else
    mkdir("logs", 0755);
#endif
}

int logger_init(const char *filepath) {
    ensure_logs_directory();
    if (filepath && strlen(filepath) > 0) {
        strncpy(g_log_filepath, filepath, sizeof(g_log_filepath) - 1);
        g_log_filepath[sizeof(g_log_filepath) - 1] = '\0';
    }

    FILE *fp = fopen(g_log_filepath, "a");
    if (!fp) {
        if (!g_log_warned) {
            fprintf(stderr, "Warning: Unable to open log file '%s'\n", g_log_filepath);
            g_log_warned = 1;
        }
        return -1;
    }
    fclose(fp);
    return 0;
}

int log_event(const char *event_type, int pid, const char *process_name, const char *extra_msg) {
    ensure_logs_directory();
    FILE *fp = fopen(g_log_filepath, "a");
    if (!fp) {
        if (!g_log_warned) {
            fprintf(stderr, "Warning: Unable to open log file '%s'\n", g_log_filepath);
            g_log_warned = 1;
        }
        return -1;
    }

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[64];
    if (tm_info) {
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        snprintf(time_str, sizeof(time_str), "UNKNOWN_TIME");
    }

    if (pid > 0 && process_name) {
        fprintf(fp, "[%s] %s PID=%d NAME=%s%s%s\n",
                time_str,
                event_type ? event_type : "EVENT",
                pid,
                process_name,
                extra_msg ? " INFO=" : "",
                extra_msg ? extra_msg : "");
    } else {
        fprintf(fp, "[%s] %s %s\n",
                time_str,
                event_type ? event_type : "EVENT",
                extra_msg ? extra_msg : "");
    }

    fclose(fp);
    return 0;
}

void logger_close(void) {
    // Cleanup if needed
}
