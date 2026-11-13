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

/* The XDG location first, then the older ~/.local/pac one. */
static bool config_path(int which, char *path, size_t size) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    int len;
    if (which == 0 && xdg && *xdg) len = snprintf(path, size, "%s/pacpeek/pacpeek.conf", xdg);
    else if (which == 0 && home && *home) len = snprintf(path, size, "%s/.config/pacpeek/pacpeek.conf", home);
    else if (which == 1 && home && *home) len = snprintf(path, size, "%s/.local/pac/pacpeek.conf", home);
    else return false;
    return len < (int)size;
}

void config_load(ConfigHandler handler, void *data) {
    char path[512];
    FILE *f = NULL;
    for (int which = 0; which < 2 && !f; which++)
        if (config_path(which, path, sizeof(path))) f = fopen(path, "r");
    if (!f) return;

    char line[1024];
    for (int n = 1; fgets(line, sizeof(line), f); n++) {
        /* A line that doesn't fit would otherwise have its tail read as a new setting. */
        if (!strchr(line, '\n') && !feof(f)) {
            fprintf(stderr, _("pacpeek: %s:%d: line too long, ignored\n"), path, n);
            for (int c; (c = fgetc(f)) != EOF && c != '\n';) {}
            continue;
        }
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
