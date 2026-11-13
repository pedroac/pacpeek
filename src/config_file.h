#ifndef CONFIG_FILE_H
#define CONFIG_FILE_H

#include <stdbool.h>

/* Reads $XDG_CONFIG_HOME/pacpeek/pacpeek.conf (~/.config/pacpeek/pacpeek.conf), or else the
 * older ~/.local/pac/pacpeek.conf, if there is one: "key = value" lines, '#' starts a
 * comment. Calls handler(key, value, data) for each setting; the handler returns false
 * for an unknown key or a bad value, which is reported and skipped. */
typedef bool (*ConfigHandler)(const char *key, const char *value, void *data);

void config_load(ConfigHandler handler, void *data);

#endif
