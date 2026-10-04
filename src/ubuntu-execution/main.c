#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system.h"
#include "process.h"
#include "monitor.h"
#include "ipc.h"
#include "memory_demo.h"
#include "file_demo.h"
#include "thread_monitor.h"
#include "web_export.h"

static void print_menu(void) {
    printf("\n============================================================\n");
    printf("                 LINUX KERNEL MONITOR                       \n");
    printf("============================================================\n");
    printf("1. Live System Monitoring\n");
    printf("2. Show All Processes\n");
    printf("3. Sort Processes by CPU%%\n");
    printf("4. Sort Processes by Memory%%\n");
    printf("5. Search Process (by PID or Name)\n");
    printf("6. Inspect Process Details\n");
    printf("7. Process Hierarchy Tree\n");
    printf("8. Process Lifecycle Demo\n");
    printf("9. IPC Demonstrations\n");
    printf("10. Signal Demonstration\n");
    printf("11. Memory Management Demonstrations\n");
    printf("12. File I/O Demonstrations\n");
    printf("13. Thread & Synchronization Demonstrations\n");
    printf("14. System Call / OS Architecture Demo\n");
    printf("15. View Logs\n");
    printf("16. Unix Domain Socket Demo\n");
    printf("17. POSIX Shared Memory Demo\n");
    printf("18. Starvation and Fairness Demo\n");
    printf("19. Exit\n");
    printf("============================================================\n");
    printf("Select an option [1-19]: ");
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    MonitorState state;
    int single_run = 0;
    int show_tree = 0;
    int inspect_pid = -1;
    SortCriterion sort_crit = SORT_BY_PID;
    char search_query[128] = "";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--web-export") == 0 && i + 1 < argc) {
            return run_web_export(argv[i + 1]) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--once") == 0 || strcmp(argv[i], "-1") == 0) {
            single_run = 1;
        } else if (strcmp(argv[i], "--tree") == 0 || strcmp(argv[i], "-t") == 0) {
            show_tree = 1;
        } else if (strcmp(argv[i], "--sort=cpu") == 0) {
            sort_crit = SORT_BY_CPU;
        } else if (strcmp(argv[i], "--sort=mem") == 0) {
            sort_crit = SORT_BY_MEM;
        } else if (strcmp(argv[i], "--sort=pid") == 0) {
            sort_crit = SORT_BY_PID;
        } else if (strncmp(argv[i], "--search=", 9) == 0) {
            strncpy(search_query, argv[i] + 9, sizeof(search_query) - 1);
        } else if (strncmp(argv[i], "--pid=", 6) == 0) {
            inspect_pid = atoi(argv[i] + 6);
        }
    }

    monitor_init(&state, 2);

    if (inspect_pid > 0) {
        monitor_step(&state);
        ProcessDetails details;
        if (read_process_details(inspect_pid, state.current_sys.total_ram_gb, &details) == 0) {
            print_process_details(&details);
        } else {
            fprintf(stderr, "Error: Process with PID %d not found or terminated.\n", inspect_pid);
        }
        return EXIT_SUCCESS;
    }

    if (single_run || show_tree || strlen(search_query) > 0 || sort_crit != SORT_BY_PID) {
        monitor_step(&state);

        ProcessList display_list = state.current_procs;
        if (strlen(search_query) > 0) {
            ProcessList filtered;
            search_process_list(&display_list, &filtered, search_query);
            display_list = filtered;
            printf("\n[Search Query: '%s']\n", search_query);
        }

        sort_process_list(&display_list, sort_crit);

        print_system_info(&state.current_sys);
        if (show_tree) {
            print_process_tree(&display_list);
        } else {
            print_process_list(&display_list, 15);
        }
        return EXIT_SUCCESS;
    }

    // Interactive Menu Mode
    int choice = 0;
    while (choice != 19) {
        print_menu();
        if (scanf("%d", &choice) != 1) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            printf("Invalid input. Please enter a number between 1 and 19.\n");
            continue;
        }

        switch (choice) {
            case 1:
                printf("\nStarting Live Dashboard...\n");
                monitor_run_live(&state);
                break;
            case 2:
                monitor_step(&state);
                print_system_info(&state.current_sys);
                print_process_list(&state.current_procs, 0);
                break;
            case 3: {
                monitor_step(&state);
                ProcessList sorted = state.current_procs;
                sort_process_list(&sorted, SORT_BY_CPU);
                print_system_info(&state.current_sys);
                print_process_list(&sorted, 15);
                break;
            }
            case 4: {
                monitor_step(&state);
                ProcessList sorted = state.current_procs;
                sort_process_list(&sorted, SORT_BY_MEM);
                print_system_info(&state.current_sys);
                print_process_list(&sorted, 15);
                break;
            }
            case 5: {
                char query[128];
                printf("Enter PID or process name to search: ");
                scanf("%127s", query);
                monitor_step(&state);
                ProcessList filtered;
                search_process_list(&state.current_procs, &filtered, query);
                print_system_info(&state.current_sys);
                print_process_list(&filtered, 0);
                break;
            }
            case 6: {
                int target_pid = 0;
                printf("Enter Process PID to inspect: ");
                if (scanf("%d", &target_pid) == 1 && target_pid > 0) {
                    monitor_step(&state);
                    ProcessDetails details;
                    if (read_process_details(target_pid, state.current_sys.total_ram_gb, &details) == 0) {
                        print_process_details(&details);
                    } else {
                        printf("Process PID %d not found.\n", target_pid);
                    }
                }
                break;
            }
            case 7:
                monitor_step(&state);
                print_system_info(&state.current_sys);
                print_process_tree(&state.current_procs);
                break;
            case 8:
                demo_process_lifecycle();
                break;
            case 9:
                run_ipc_demo();
                break;
            case 10:
                run_signal_demo();
                break;
            case 11:
                run_memory_demo();
                break;
            case 12:
                run_file_demo();
                break;
            case 13:
                run_thread_demo();
                break;
            case 14:
                run_system_call_demo();
                break;
            case 15:
                monitor_step(&state);
                print_recent_events(&state, 20);
                break;
            case 16:
                demo_unix_socket();
                break;
            case 17:
                demo_shared_memory();
                break;
            case 18:
                run_starvation_demo();
                break;
            case 19:
                printf("\nExiting Linux Kernel Monitor. Goodbye!\n");
                break;
            default:
                printf("Invalid option. Choose between 1 and 19.\n");
                break;
        }
    }

    return EXIT_SUCCESS;
}
