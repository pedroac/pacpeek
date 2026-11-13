#ifndef SYSFS_H
#define SYSFS_H

#include <stdbool.h>
#include <stddef.h>

/* Read the first line of a file, without its newline. */
bool sysfs_read_line(const char *path, char *buf, size_t size);
bool sysfs_read_int(const char *path, long *value);

#endif
