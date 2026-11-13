#ifndef FONTFIND_H
#define FONTFIND_H

#include <stdbool.h>
#include <stddef.h>

#define FONT_MAX_DIRS 16

/* The directories fonts are installed in: $XDG_DATA_HOME/fonts, ~/.fonts and the fonts
 * directory of every entry of $XDG_DATA_DIRS. This covers distributions that keep them
 * elsewhere (Fedora, NixOS) without listing each location. */
typedef struct {
    char storage[FONT_MAX_DIRS][256];
    const char *list[FONT_MAX_DIRS + 1];   /* NULL-terminated */
} FontDirs;

void font_dirs_init(FontDirs *dirs);

/* Searches dirs (recursively) for a file named like one of names (NULL-terminated). The
 * earliest name wins, wherever it is found. */
bool font_find(const char *const *dirs, const char *const *names, char *path, size_t size);

#endif
