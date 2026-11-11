#include "system.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct tm system_get_time(void) {
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    return t;
}

LateWindow system_late_window(const char *spec) {
    LateWindow window = { 21 * 60, 6 * 60 };
    if (!spec) return window;
    if (strcmp(spec, "off") == 0) return (LateWindow){ 0, 0 };

    int h1, m1, h2, m2;
    char extra;
    if (sscanf(spec, "%d:%d-%d:%d%c", &h1, &m1, &h2, &m2, &extra) == 4 &&
        h1 >= 0 && h1 < 24 && h2 >= 0 && h2 < 24 &&
        m1 >= 0 && m1 < 60 && m2 >= 0 && m2 < 60)
        window = (LateWindow){ h1 * 60 + m1, h2 * 60 + m2 };
    return window;
}

bool system_is_late(const LateWindow *window, const struct tm *t) {
    int now = t->tm_hour * 60 + t->tm_min;
    if (window->start < window->end) return now >= window->start && now < window->end;
    if (window->start > window->end) return now >= window->start || now < window->end;
    return false;
}

int system_ram_percent(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;

    char line[128];
    unsigned long total = 0, available = 0;
    while (fgets(line, sizeof(line), f) && (!total || !available)) {
        sscanf(line, "MemTotal: %lu kB", &total);
        sscanf(line, "MemAvailable: %lu kB", &available);
    }
    fclose(f);

    if (!total) return -1;
    return (int)((total - available) * 100.0 / total + 0.5);
}

CpuSample system_cpu_sample(void) {
    CpuSample s = {0};
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return s;

    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) == 8) {
        s.busy = user + nice + system + irq + softirq + steal;
        s.total = s.busy + idle + iowait;
    }
    fclose(f);
    return s;
}

int system_cpu_percent(CpuSample before, CpuSample after) {
    /* iowait can make the counters non-monotonic; treat that as "unknown".
     * Require at least 10 jiffies of elapsed total time so a very short sample
     * window (< ~100 ms on a HZ=100 kernel) doesn't produce a meaningless result. */
    if (!before.total || after.total < before.total + 10 || after.busy < before.busy) return -1;
    unsigned long long total = after.total - before.total;
    int percent = (int)((after.busy - before.busy) * 100.0 / total + 0.5);
    return percent > 100 ? 100 : percent;
}

static bool read_line(const char *path, char *buf, size_t size) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    bool ok = fgets(buf, size, f) != NULL;
    fclose(f);
    if (ok) buf[strcspn(buf, "\n")] = '\0';
    return ok;
}

static bool read_int(const char *path, long *value) {
    char buf[32];
    return read_line(path, buf, sizeof(buf)) && sscanf(buf, "%ld", value) == 1;
}

static const char *cpu_sensors[] = { "coretemp", "k10temp", "zenpower", "cpu_thermal", NULL };

int system_cpu_temp(void) {
    static char sensor_path[64];

    if (!sensor_path[0]) {
        DIR *dir = opendir("/sys/class/hwmon");
        if (!dir) return -1;

        int best_rank = -1;
        struct dirent *e;
        while ((e = readdir(dir))) {
            if (e->d_name[0] == '.') continue;
            char path[64], name[32];
            snprintf(path, sizeof(path), "/sys/class/hwmon/%.20s/name", e->d_name);
            if (!read_line(path, name, sizeof(name))) continue;

            for (int rank = 0; cpu_sensors[rank]; rank++) {
                if (strcmp(name, cpu_sensors[rank]) != 0) continue;
                if (best_rank < 0 || rank < best_rank) {
                    best_rank = rank;
                    snprintf(sensor_path, sizeof(sensor_path),
                             "/sys/class/hwmon/%.20s/temp1_input", e->d_name);
                }
            }
        }
        closedir(dir);
        if (best_rank < 0) snprintf(sensor_path, sizeof(sensor_path),
                                    "/sys/class/thermal/thermal_zone0/temp");
    }

    long millidegrees;
    if (!read_int(sensor_path, &millidegrees)) return -1;
    return (int)((millidegrees + 500) / 1000);
}

BatteryInfo system_battery(void) {
    BatteryInfo info = {0};
    DIR *dir = opendir("/sys/class/power_supply");
    if (!dir) return info;

    struct dirent *e;
    while ((e = readdir(dir))) {
        if (e->d_name[0] == '.') continue;
        char base[96], path[128], value[32];
        snprintf(base, sizeof(base), "/sys/class/power_supply/%.40s", e->d_name);

        snprintf(path, sizeof(path), "%s/type", base);
        if (!read_line(path, value, sizeof(value)) || strcmp(value, "Battery") != 0) continue;

        snprintf(path, sizeof(path), "%s/scope", base);
        if (read_line(path, value, sizeof(value)) && strcmp(value, "Device") == 0) continue;

        long capacity;
        snprintf(path, sizeof(path), "%s/capacity", base);
        if (!read_int(path, &capacity)) continue;

        snprintf(path, sizeof(path), "%s/status", base);
        info.present = true;
        info.percent = (int)capacity;
        info.charging = read_line(path, value, sizeof(value)) && strcmp(value, "Charging") == 0;
        break;
    }
    closedir(dir);
    return info;
}
