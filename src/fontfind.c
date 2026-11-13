#define _GNU_SOURCE
#include "fontfind.h"
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void add_dir(FontDirs *dirs, int *count, const char *base, const char *suffix) {
    if (*count >= FONT_MAX_DIRS) return;
    if (snprintf(dirs->storage[*count], sizeof(dirs->storage[0]), "%s%s", base, suffix) >=
        (int)sizeof(dirs->storage[0]))
        return;
    dirs->list[*count] = dirs->storage[*count];
    (*count)++;
}

void font_dirs_init(FontDirs *dirs) {
    memset(dirs, 0, sizeof(*dirs));
    int count = 0;

    const char *home = getenv("HOME");
    const char *data_home = getenv("XDG_DATA_HOME");
    if (data_home && *data_home) add_dir(dirs, &count, data_home, "/fonts");
    else if (home && *home) add_dir(dirs, &count, home, "/.local/share/fonts");
    if (home && *home) add_dir(dirs, &count, home, "/.fonts");

    const char *data_dirs = getenv("XDG_DATA_DIRS");
    if (!data_dirs || !*data_dirs) data_dirs = "/usr/local/share:/usr/share";
    while (*data_dirs) {
        size_t len = strcspn(data_dirs, ":");
        if (len > 0 && len < 200) {
            char base[200];
            memcpy(base, data_dirs, len);
            base[len] = '\0';
            add_dir(dirs, &count, base, "/fonts");
        }
        data_dirs += len + (data_dirs[len] ? 1 : 0);
    }
}

/* nftw() takes no user data, so the search state is static. */
static const char *const *search_names;
static int best_rank;
static char best_path[256];

static int visit(const char *path, const struct stat *sb, int type, struct FTW *ftw) {
    (void)sb;
    /* Fonts are often symlinks (package stores), so links count as files. */
    if (type != FTW_F && type != FTW_SL) return 0;

    const char *base = path + ftw->base;
    for (int rank = 0; search_names[rank] && rank < best_rank; rank++) {
        if (strcmp(base, search_names[rank]) != 0 || access(path, R_OK) != 0) continue;
        if (snprintf(best_path, sizeof(best_path), "%s", path) >= (int)sizeof(best_path)) continue;
        best_rank = rank;
        return rank == 0;   /* nothing can beat the first choice: stop searching */
    }
    return 0;
}

bool font_find(const char *const *dirs, const char *const *names, char *path, size_t size) {
    int count = 0;
    while (names[count]) count++;

    search_names = names;
    best_rank = count;
    best_path[0] = '\0';
    for (int i = 0; dirs[i] && best_rank > 0; i++) nftw(dirs[i], visit, 16, FTW_PHYS);

    if (best_rank == count || size == 0) return false;
    snprintf(path, size, "%s", best_path);
    return true;
}
