#include "sysfs.h"
#include <stdio.h>
#include <string.h>

bool sysfs_read_line(const char *path, char *buf, size_t size) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    bool ok = fgets(buf, size, f) != NULL;
    fclose(f);
    if (ok) buf[strcspn(buf, "\n")] = '\0';
    return ok;
}

bool sysfs_read_int(const char *path, long *value) {
    char buf[32];
    return sysfs_read_line(path, buf, sizeof(buf)) && sscanf(buf, "%ld", value) == 1;
}
