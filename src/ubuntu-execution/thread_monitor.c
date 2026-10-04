#define _POSIX_C_SOURCE 200809L
#include "thread_monitor.h"
#include "logger.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct CounterState {
    int counter;
    pthread_mutex_t mutex;
};

static void *increment_no_lock(void *arg) {
    int *counter = (int *)arg;
    for (int i = 0; i < 50000; i++) {
        (*counter)++;
    }
    return NULL;
}

static void *increment_with_lock(void *arg) {
    struct CounterState *state = (struct CounterState *)arg;
    for (int i = 0; i < 50000; i++) {
        pthread_mutex_lock(&state->mutex);
        state->counter++;
        pthread_mutex_unlock(&state->mutex);
    }
    return NULL;
}

static void *monitor_thread(void *arg) {
    ThreadJob *job = (ThreadJob *)arg;
    pthread_mutex_lock(job->mutex);
    job->shared->process_count = job->thread_id + 100;
    job->shared->last_update = job->thread_id;
    job->shared->status[0] = 'd';
    job->shared->status[1] = 'a';
    job->shared->status[2] = 't';
    job->shared->status[3] = 'a';
    job->shared->status[4] = '\0';
    pthread_cond_signal(job->cond);
    pthread_mutex_unlock(job->mutex);
    return NULL;
}

static void *logger_thread(void *arg) {
    ThreadJob *job = (ThreadJob *)arg;
    pthread_mutex_lock(job->mutex);
    while (job->shared->status[0] != 'd') {
        pthread_cond_wait(job->cond, job->mutex);
    }
    printf("[THREAD] logger observed update: process_count=%d last_update=%d\n",
           job->shared->process_count, job->shared->last_update);
    pthread_mutex_unlock(job->mutex);
    return NULL;
}

static void *semaphore_worker(void *arg) {
    ThreadJob *job = (ThreadJob *)arg;
    sem_wait(job->semaphore);
    printf("[THREAD] worker %d acquired resource\n", job->thread_id);
    struct timespec ts = {0, 100000000L};
    nanosleep(&ts, NULL);
    sem_post(job->semaphore);
    return NULL;
}

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int rounds;
    int fair;
    int served[2];
} StarvationState;

typedef struct {
    StarvationState *state;
    int worker_id;
} StarvationJob;

static void *starvation_worker(void *arg) {
    StarvationJob *job = (StarvationJob *)arg;
    StarvationState *state = job->state;
    int id = job->worker_id;

    for (;;) {
        pthread_mutex_lock(&state->mutex);
        while (state->rounds < 20 && ((state->fair && state->rounds % 2 != id) ||
                                      (!state->fair && id != 0))) {
            pthread_cond_wait(&state->condition, &state->mutex);
        }
        if (state->rounds >= 20) {
            pthread_cond_broadcast(&state->condition);
            pthread_mutex_unlock(&state->mutex);
            return NULL;
        }
        state->served[id]++;
        state->rounds++;
        pthread_cond_broadcast(&state->condition);
        pthread_mutex_unlock(&state->mutex);
    }
}

int run_race_condition_demo(void) {
    int no_lock_counter = 0;
    struct CounterState state = {0, PTHREAD_MUTEX_INITIALIZER};
    pthread_t t1, t2, t3, t4;

    if (pthread_create(&t1, NULL, increment_no_lock, &no_lock_counter) != 0 ||
        pthread_create(&t2, NULL, increment_no_lock, &no_lock_counter) != 0) {
        perror("pthread_create");
        return -1;
    }
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    printf("[THREAD] Without mutex: final value=%d (may be lower than expected 100000)\n", no_lock_counter);

    if (pthread_create(&t3, NULL, increment_with_lock, &state) != 0 ||
        pthread_create(&t4, NULL, increment_with_lock, &state) != 0) {
        perror("pthread_create");
        return -1;
    }
    pthread_join(t3, NULL);
    pthread_join(t4, NULL);
    printf("[THREAD] With mutex: final value=%d (expected 100000)\n", state.counter);
    pthread_mutex_destroy(&state.mutex);
    return 0;
}

int run_deadlock_demo(void) {
    pthread_mutex_t a = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t b = PTHREAD_MUTEX_INITIALIZER;

    printf("[THREAD] Deadlock prevention pattern: lock A then B in the same order across all threads.\n");
    pthread_mutex_lock(&a);
    pthread_mutex_lock(&b);
    pthread_mutex_unlock(&b);
    pthread_mutex_unlock(&a);
    pthread_mutex_destroy(&a);
    pthread_mutex_destroy(&b);
    return 0;
}

int run_starvation_demo(void) {
    for (int fair = 0; fair <= 1; fair++) {
        StarvationState state = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, fair, {0, 0}};
        StarvationJob jobs[2] = {{&state, 0}, {&state, 1}};
        pthread_t threads[2];
        if (pthread_create(&threads[0], NULL, starvation_worker, &jobs[0]) != 0 ||
            pthread_create(&threads[1], NULL, starvation_worker, &jobs[1]) != 0) {
            perror("pthread_create");
            return -1;
        }
        pthread_join(threads[0], NULL);
        pthread_join(threads[1], NULL);
        if (!fair) {
            printf("[THREAD] Starvation example: unfair priority gave worker 0 all %d turns and worker 1 %d turns.\n",
                   state.served[0], state.served[1]);
            printf("[THREAD] Starvation occurs when scheduling repeatedly favors one runnable worker.\n");
        } else {
            printf("[THREAD] Fairness prevention: round-robin scheduling gave worker 0 %d turns and worker 1 %d turns.\n",
                   state.served[0], state.served[1]);
        }
        pthread_mutex_destroy(&state.mutex);
        pthread_cond_destroy(&state.condition);
    }
    return 0;
}

int build_multithreaded_monitor_demo(void) {
    SharedMonitorState shared = {0};
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    sem_t semaphore;
    pthread_t threads[3];
    ThreadJob jobs[3];

    snprintf(shared.status, sizeof(shared.status), "idle");
    shared.active = 1;
    if (sem_init(&semaphore, 0, MAX_THREAD_JOBS) != 0) {
        perror("sem_init");
        return -1;
    }

    for (int i = 0; i < 3; i++) {
        jobs[i].shared = &shared;
        jobs[i].mutex = &mutex;
        jobs[i].cond = &cond;
        jobs[i].semaphore = &semaphore;
        jobs[i].thread_id = i + 1;

        void *(*func)(void *) = NULL;
        if (i == 0) func = (void *(*)(void *))monitor_thread;
        else if (i == 1) func = (void *(*)(void *))logger_thread;
        else func = (void *(*)(void *))semaphore_worker;

        if (pthread_create(&threads[i], NULL, func, &jobs[i]) != 0) {
            perror("pthread_create");
            sem_destroy(&semaphore);
            return -1;
        }
    }

    struct timespec ts = {0, 200000000L};
    nanosleep(&ts, NULL);
    shared.active = 0;
    pthread_cond_broadcast(&cond);
    for (int i = 0; i < 3; i++) pthread_join(threads[i], NULL);
    sem_destroy(&semaphore);
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
    log_event("THREAD_DEMO", getpid(), "monitor", "Thread synchronization demonstration completed");
    return 0;
}

int run_thread_demo(void) {
    if (run_race_condition_demo() != 0) return -1;
    if (build_multithreaded_monitor_demo() != 0) return -1;
    if (run_deadlock_demo() != 0) return -1;
    if (run_starvation_demo() != 0) return -1;
    return 0;
}
