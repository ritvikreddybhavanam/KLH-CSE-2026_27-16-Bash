#ifndef THREAD_MONITOR_H
#define THREAD_MONITOR_H

#include <pthread.h>
#include <semaphore.h>

#define MAX_THREAD_JOBS 4

typedef struct {
    int cpu_usage_samples;
    int memory_usage_samples;
    int process_count;
    int active;
    int last_update;
    char status[64];
} SharedMonitorState;

typedef struct {
    SharedMonitorState *shared;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
    sem_t *semaphore;
    int thread_id;
} ThreadJob;

int run_thread_demo(void);
int run_race_condition_demo(void);
int run_deadlock_demo(void);
int run_starvation_demo(void);
int build_multithreaded_monitor_demo(void);

#endif /* THREAD_MONITOR_H */
