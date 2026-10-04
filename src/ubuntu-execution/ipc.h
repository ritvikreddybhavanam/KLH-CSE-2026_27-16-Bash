#ifndef IPC_H
#define IPC_H

#include <signal.h>
#include <sys/types.h>

#define IPC_FIFO_PATH "/tmp/kernel_monitor_fifo"

int demo_pipe_anon(void);
int demo_fifo(void);
int demo_unix_socket(void);
int demo_shared_memory(void);
int demo_process_groups(void);
int demo_sessions(void);
int demo_job_control(void);
int run_signal_demo(void);
int run_ipc_demo(void);
int run_system_call_demo(void);

#endif /* IPC_H */
