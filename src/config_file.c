#include "config_file.h"
#include "i18n.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static bool config_path(char *path, size_t size) {
    const char *home = getenv("HOME");
    if (!home || !*home) return false;
    return snprintf(path, size, "%s/.local/pac/pacpeek.conf", home) < (int)size;
}

void config_load(ConfigHandler handler, void *data) {
    char path[512];
    if (!config_path(path, sizeof(path))) return;
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[1024];
    for (int n = 1; fgets(line, sizeof(line), f); n++) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *text = trim(line);
        if (!*text) continue;

        char *eq = strchr(text, '=');
        if (!eq) {
            fprintf(stderr, _("pacpeek: %s:%d: expected \"key = value\"\n"), path, n);
            continue;
        }
        *eq = '\0';
        char *key = trim(text), *value = trim(eq + 1);
        if (!handler(key, value, data))
            fprintf(stderr, _("pacpeek: %s:%d: ignoring \"%s = %s\"\n"), path, n, key, value);
    }
    fclose(f);
}
