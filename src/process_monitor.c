/* process_monitor.c */

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <ctype.h>

int is_numeric(const char *str)
{
    while (*str)
    {
        if (!isdigit(*str))
            return 0;
        str++;
    }
    return 1;
}

void scan_processes()
{
    DIR *proc_dir;
    struct dirent *entry;

    proc_dir = opendir("/proc");

    if (proc_dir == NULL)
    {
        perror("Error opening /proc");
        return;
    }

    printf("\n=================================================\n");
    printf("PID\tPPID\tSTATE\tNAME\n");
    printf("=================================================\n");

    while ((entry = readdir(proc_dir)) != NULL)
    {
        if (is_numeric(entry->d_name))
        {
            char path[256];
            FILE *fp;

            int pid = 0;
            int ppid = 0;

            char name[100] = "";
            char state[20] = "";

            snprintf(path,
                     sizeof(path),
                     "/proc/%s/status",
                     entry->d_name);

            fp = fopen(path, "r");

            if (fp == NULL)
                continue;

            char line[256];

            while (fgets(line, sizeof(line), fp))
            {
                if (strncmp(line, "Name:", 5) == 0)
                {
                    sscanf(line, "Name:\t%99s", name);
                }
                else if (strncmp(line, "Pid:", 4) == 0)
                {
                    sscanf(line, "Pid:\t%d", &pid);
                }
                else if (strncmp(line, "PPid:", 5) == 0)
                {
                    sscanf(line, "PPid:\t%d", &ppid);
                }
                else if (strncmp(line, "State:", 6) == 0)
                {
                    sscanf(line, "State:\t%19s", state);
                }
            }

            fclose(fp);

            printf("%d\t%d\t%s\t%s\n",
                   pid,
                   ppid,
                   state,
                   name);
        }
    }

    closedir(proc_dir);
}

/* Test */
int main()
{
    scan_processes();
    return 0;
}