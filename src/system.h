#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdbool.h>
#include <time.h>

typedef struct {
    unsigned long long busy;
    unsigned long long total;
} CpuSample;

typedef struct {
    bool present;
    int percent;
    bool charging;
} BatteryInfo;

/* Minutes since midnight; the window wraps past midnight, and start == end never matches. */
typedef struct {
    int start;
    int end;
} LateWindow;

struct tm system_get_time(void);
/* Parses "HH:MM-HH:MM" or "off". NULL or anything invalid gives the default, 21:00-06:00. */
LateWindow system_late_window(const char *spec);
/* True when the user should be asleep. */
bool system_is_late(const LateWindow *window, const struct tm *t);
/* Returns -1 if unknown. */
int system_ram_percent(void);
CpuSample system_cpu_sample(void);
/* Returns -1 if the samples are too close together. */
int system_cpu_percent(CpuSample before, CpuSample after);
/* Returns -1 if no CPU temperature sensor is found. */
int system_cpu_temp(void);
BatteryInfo system_battery(void);

#endif
