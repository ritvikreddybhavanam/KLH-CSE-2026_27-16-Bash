#include <stdio.h>
#include <stdlib.h>

void read_cpu_stats()
{
    FILE *fp;
    char cpu[10];
    unsigned long user, nice, system, idle;

    fp = fopen("/proc/stat", "r");

    if (fp == NULL)
    {
        perror("Error opening /proc/stat");
        return;
    }

    fscanf(fp, "%s %lu %lu %lu %lu",
           cpu,
           &user,
           &nice,
           &system,
           &idle);

    printf("\n===== CPU INFO =====\n");
    printf("User Time   : %lu\n", user);
    printf("Nice Time   : %lu\n", nice);
    printf("System Time : %lu\n", system);
    printf("Idle Time   : %lu\n", idle);

    fclose(fp);
}

void read_memory_info()
{
    FILE *fp;
    char label[50];
    unsigned long value;
    char unit[20];

    fp = fopen("/proc/meminfo", "r");

    if (fp == NULL)
    {
        perror("Error opening /proc/meminfo");
        return;
    }

    printf("\n===== MEMORY INFO =====\n");

    for (int i = 0; i < 3; i++)
    {
        fscanf(fp, "%s %lu %s",
               label,
               &value,
               unit);

        printf("%s %lu %s\n",
               label,
               value,
               unit);
    }

    fclose(fp);
}

void read_uptime()
{
    FILE *fp;
    double uptime;

    fp = fopen("/proc/uptime", "r");

    if (fp == NULL)
    {
        perror("Error opening /proc/uptime");
        return;
    }

    fscanf(fp, "%lf", &uptime);

    printf("\n===== UPTIME =====\n");
    printf("System Uptime: %.2f seconds\n", uptime);

    fclose(fp);
}

void read_loadavg()
{
    FILE *fp;
    float load1, load5, load15;

    fp = fopen("/proc/loadavg", "r");

    if (fp == NULL)
    {
        perror("Error opening /proc/loadavg");
        return;
    }

    fscanf(fp,
           "%f %f %f",
           &load1,
           &load5,
           &load15);

    printf("\n===== LOAD AVERAGE =====\n");
    printf("1 Minute  : %.2f\n", load1);
    printf("5 Minutes : %.2f\n", load5);
    printf("15 Minutes: %.2f\n", load15);

    fclose(fp);
}

int main()
{
    read_cpu_stats();
    read_memory_info();
    read_uptime();
    read_loadavg();

    return 0;
}