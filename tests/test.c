/* Unit tests for the parts that need no Wayland connection. network.c is included rather
 * than linked so its static parsers can be called directly. */
#define _GNU_SOURCE
#include "../src/network.c"
#include "../src/config_file.h"
#include "../src/fontfind.h"
#include "../src/hold.h"
#include "../src/system.h"
#include "../src/utf8.h"
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks, failures;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_STR(actual, expected) do { \
    checks++; \
    if (strcmp((actual), (expected)) != 0) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (actual), (expected)); \
    } \
} while (0)

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(2); }
    fputs(text, f);
    fclose(f);
}

static void test_utf8(void) {
    uint32_t cp;
    CHECK(utf8_decode("A", &cp) == 1 && cp == 'A');
    CHECK(utf8_decode("\xC3\xA9", &cp) == 2 && cp == 0xE9);
    CHECK(utf8_decode("\xE2\x82\xAC", &cp) == 3 && cp == 0x20AC);
    CHECK(utf8_decode("\xF0\x9F\x98\x80", &cp) == 4 && cp == 0x1F600);
    CHECK(utf8_decode("\xC0\x80", &cp) == 0);          /* overlong NUL */
    CHECK(utf8_decode("\xED\xA0\x80", &cp) == 0);      /* surrogate */
    CHECK(utf8_decode("\xE2\x82", &cp) == 0);          /* truncated */
    CHECK(utf8_decode("\xF4\x90\x80\x80", &cp) == 0);  /* above U+10FFFF */
    CHECK(utf8_decode("\x80", &cp) == 0);              /* stray continuation byte */
}

static void test_late_window(void) {
    LateWindow w = system_late_window(NULL);
    CHECK(w.start == 21 * 60 && w.end == 6 * 60);

    w = system_late_window("22:30-05:15");
    CHECK(w.start == 22 * 60 + 30 && w.end == 5 * 60 + 15);

    w = system_late_window("off");
    CHECK(w.start == 0 && w.end == 0);

    w = system_late_window("25:00-06:00");  /* invalid: default */
    CHECK(w.start == 21 * 60 && w.end == 6 * 60);
    w = system_late_window("22:00-06:00 junk");
    CHECK(w.start == 21 * 60 && w.end == 6 * 60);

    struct tm t = { .tm_hour = 23, .tm_min = 0 };
    LateWindow night = { 21 * 60, 6 * 60 };      /* wraps past midnight */
    CHECK(system_is_late(&night, &t));
    t.tm_hour = 3;
    CHECK(system_is_late(&night, &t));
    t.tm_hour = 6;
    CHECK(!system_is_late(&night, &t));          /* the end is exclusive */
    t.tm_hour = 12;
    CHECK(!system_is_late(&night, &t));

    LateWindow day = { 9 * 60, 17 * 60 };
    t.tm_hour = 12;
    CHECK(system_is_late(&day, &t));
    t.tm_hour = 17;
    CHECK(!system_is_late(&day, &t));

    LateWindow off = { 0, 0 };
    CHECK(!system_is_late(&off, &t));
}

static void test_cpu_percent(void) {
    CpuSample a = { 100, 200 };
    CHECK(system_cpu_percent(a, (CpuSample){ 150, 300 }) == 50);
    CHECK(system_cpu_percent(a, (CpuSample){ 300, 300 }) == 100);
    CHECK(system_cpu_percent(a, (CpuSample){ 150, 205 }) == -1);   /* window too short */
    CHECK(system_cpu_percent(a, (CpuSample){ 90, 300 }) == -1);    /* busy went backwards */
    CHECK(system_cpu_percent((CpuSample){ 0, 0 }, (CpuSample){ 50, 100 }) == -1);  /* no baseline */
}

static void test_routes(void) {
    char iface[IF_NAMESIZE];
    unsigned metric;

    const char *v4_default = "wlan0\t00000000\t0100A8C0\t0003\t0\t0\t600\t00000000\t0\t0\t0\n";
    CHECK(parse_route_v4(v4_default, iface, &metric));
    CHECK_STR(iface, "wlan0");
    CHECK(metric == 600);

    CHECK(!parse_route_v4("wlan0\t0000A8C0\t00000000\t0001\t0\t0\t600\t00FFFFFF\t0\t0\t0\n",
                          iface, &metric));                    /* a subnet, not the default */
    CHECK(!parse_route_v4("wlan0\t00000000\t0100A8C0\t0002\t0\t0\t600\t00000000\t0\t0\t0\n",
                          iface, &metric));                    /* route is not up */
    CHECK(!parse_route_v4("Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\n",
                          iface, &metric));                    /* header */

    const char *zeros = "00000000000000000000000000000000";
    char v6[256];
    snprintf(v6, sizeof(v6), "%s 00 %s 00 fe800000000000000000000000000001 00000400 00000001 "
             "00000000 00000003 eth0\n", zeros, zeros);
    CHECK(parse_route_v6(v6, iface, &metric));
    CHECK_STR(iface, "eth0");
    CHECK(metric == 0x400);

    snprintf(v6, sizeof(v6), "%s 00 %s 00 %s 00000400 00000001 00000000 00000003 lo\n",
             zeros, zeros, zeros);
    CHECK(!parse_route_v6(v6, iface, &metric));                /* loopback never counts */
    snprintf(v6, sizeof(v6), "fe800000000000000000000000000000 40 %s 00 %s 00000100 00000001 "
             "00000000 00000001 eth0\n", zeros, zeros);
    CHECK(!parse_route_v6(v6, iface, &metric));                /* a prefix, not the default */
}

static void test_route_choice(void) {
    Route best = {0};
    consider(&best, "wlan0", 600);
    CHECK(best.found && strcmp(best.iface, "wlan0") == 0 && best.metric == 600);
    consider(&best, "eth0", 700);                  /* a worse metric changes nothing */
    CHECK_STR(best.iface, "wlan0");
    consider(&best, "eth0", 100);
    CHECK_STR(best.iface, "eth0");                 /* a better one wins */
    consider(&best, "wg0", 100);
    CHECK_STR(best.iface, "eth0");                 /* a tie keeps the first */
}

static void test_sanitize(void) {
    char s[64];

    snprintf(s, sizeof(s), "%s", "plain");
    sanitize_utf8(s);
    CHECK_STR(s, "plain");

    snprintf(s, sizeof(s), "%s", "ab\x01" "c");
    sanitize_utf8(s);
    CHECK_STR(s, "ab?c");                                      /* control character */

    snprintf(s, sizeof(s), "%s", "a\xFF" "b");
    sanitize_utf8(s);
    CHECK_STR(s, "a?b");                                       /* invalid UTF-8 */

    snprintf(s, sizeof(s), "%s", "a\xE2\x80\xAE" "b");
    sanitize_utf8(s);
    CHECK_STR(s, "a?b");                                       /* right-to-left override */

    snprintf(s, sizeof(s), "%s", "caf\xC3\xA9");
    sanitize_utf8(s);
    CHECK_STR(s, "caf\xC3\xA9");                               /* valid text is kept */
}

static void test_fonts(void) {
    char root[] = "/tmp/pacpeek-fonts-XXXXXX";
    if (!mkdtemp(root)) { perror("mkdtemp"); exit(2); }
    char nested[300], deeper[340], file[400], other[400], link[400];
    snprintf(nested, sizeof(nested), "%s/truetype", root);
    snprintf(deeper, sizeof(deeper), "%s/noto", nested);
    mkdir(nested, 0700);
    mkdir(deeper, 0700);
    snprintf(other, sizeof(other), "%s/DejaVuSans.ttf", nested);
    write_file(other, "x");

    const char *dirs[] = { root, NULL };
    const char *names[] = { "NotoSans-Regular.ttf", "DejaVuSans.ttf", NULL };
    char found[300];

    CHECK(font_find(dirs, names, found, sizeof(found)));
    CHECK_STR(found, other);                       /* only the second choice exists */

    snprintf(file, sizeof(file), "%s/NotoSans-Regular.ttf", deeper);
    write_file(file, "x");
    CHECK(font_find(dirs, names, found, sizeof(found)));
    CHECK_STR(found, file);                        /* the first choice wins, however deep */

    remove(file);
    snprintf(link, sizeof(link), "%s/NotoSans-Regular.ttf", deeper);
    CHECK(symlink(other, link) == 0);
    CHECK(font_find(dirs, names, found, sizeof(found)));
    CHECK_STR(found, link);                        /* symlinked fonts (package stores) count */

    const char *missing_names[] = { "Nope.ttf", NULL };
    CHECK(!font_find(dirs, missing_names, found, sizeof(found)));
    const char *no_dirs[] = { "/nonexistent-pacpeek-dir", NULL };
    CHECK(!font_find(no_dirs, names, found, sizeof(found)));

    /* The search directories come from the XDG variables. */
    setenv("HOME", "/home/user", 1);
    setenv("XDG_DATA_HOME", "/data", 1);
    setenv("XDG_DATA_DIRS", "/a:/b", 1);
    FontDirs fd;
    font_dirs_init(&fd);
    CHECK_STR(fd.list[0], "/data/fonts");
    CHECK_STR(fd.list[1], "/home/user/.fonts");
    CHECK_STR(fd.list[2], "/a/fonts");
    CHECK_STR(fd.list[3], "/b/fonts");
    CHECK(fd.list[4] == NULL);
    unsetenv("XDG_DATA_HOME");
    unsetenv("XDG_DATA_DIRS");
    font_dirs_init(&fd);
    CHECK_STR(fd.list[0], "/home/user/.local/share/fonts");
    CHECK_STR(fd.list[2], "/usr/local/share/fonts");

    remove(link);
    remove(other);
    rmdir(deeper);
    rmdir(nested);
    rmdir(root);
}

static void test_hold(void) {
    HoldTracker h = {0};
    CHECK(!hold_key(&h, 1, true));
    CHECK(!hold_key(&h, 2, true));
    CHECK(!hold_key(&h, 1, false));    /* one shortcut key still held */
    CHECK(hold_key(&h, 2, false));     /* last one released: close */

    HoldTracker unseen = {0};
    CHECK(hold_key(&unseen, 5, false));    /* nothing held, any release closes */

    HoldTracker mixed = {0};
    hold_key(&mixed, 1, true);
    CHECK(!hold_key(&mixed, 9, false));    /* release of a key never seen: ignored while held */
    CHECK(hold_key(&mixed, 1, false));

    HoldTracker full = {0};
    for (uint32_t k = 0; k < HOLD_MAX_KEYS + 4; k++) hold_key(&full, k, true);
    CHECK(full.count == HOLD_MAX_KEYS);    /* extra keys are dropped, not overflowed */
}

typedef struct {
    int count;
    char keys[8][32], values[8][64];
} Seen;

static bool collect(const char *key, const char *value, void *data) {
    Seen *seen = data;
    if (strcmp(key, "bad") == 0) return false;
    if (seen->count < 8) {
        snprintf(seen->keys[seen->count], 32, "%s", key);
        snprintf(seen->values[seen->count], 64, "%s", value);
        seen->count++;
    }
    return true;
}

static void test_config(void) {
    char dir[] = "/tmp/pacpeek-test-XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); exit(2); }
    char path[256];
    snprintf(path, sizeof(path), "%s/.local", dir);
    mkdir(path, 0700);
    snprintf(path, sizeof(path), "%s/.local/pac", dir);
    mkdir(path, 0700);
    snprintf(path, sizeof(path), "%s/.local/pac/pacpeek.conf", dir);

    char too_long[1200];
    memset(too_long, 'x', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    char text[1600];
    snprintf(text, sizeof(text),
             "# a comment\n\n  mode =  toggle  \ntimeout=10 # trailing comment\nno equals here\n"
             "bad = 1\nfont = %s\nlast = yes\n", too_long);
    write_file(path, text);

    setenv("HOME", dir, 1);
    unsetenv("XDG_CONFIG_HOME");
    Seen seen = {0};
    FILE *quiet = freopen("/dev/null", "w", stderr);   /* the bad lines are reported there */
    config_load(collect, &seen);
    if (quiet) fflush(stderr);

    CHECK(seen.count == 3);
    CHECK_STR(seen.keys[0], "mode");
    CHECK_STR(seen.values[0], "toggle");
    CHECK_STR(seen.keys[1], "timeout");
    CHECK_STR(seen.values[1], "10");
    CHECK_STR(seen.keys[2], "last");               /* the over-long line was skipped whole */

    /* The XDG location wins over the legacy one. */
    char xdg_dir[256], xdg_path[300];
    snprintf(xdg_dir, sizeof(xdg_dir), "%s/xdg", dir);
    mkdir(xdg_dir, 0700);
    snprintf(xdg_dir, sizeof(xdg_dir), "%s/xdg/pacpeek", dir);
    mkdir(xdg_dir, 0700);
    snprintf(xdg_path, sizeof(xdg_path), "%s/pacpeek.conf", xdg_dir);
    write_file(xdg_path, "mode = hold\n");
    char xdg_home[256];
    snprintf(xdg_home, sizeof(xdg_home), "%s/xdg", dir);
    setenv("XDG_CONFIG_HOME", xdg_home, 1);
    Seen xdg_seen = {0};
    config_load(collect, &xdg_seen);
    CHECK(xdg_seen.count == 1 && strcmp(xdg_seen.values[0], "hold") == 0);
    remove(xdg_path);
    rmdir(xdg_dir);
    rmdir(xdg_home);
    unsetenv("XDG_CONFIG_HOME");

    remove(path);
    snprintf(path, sizeof(path), "%s/.local/pac", dir);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/.local", dir);
    rmdir(path);
    rmdir(dir);
}

int main(void) {
    test_utf8();
    test_late_window();
    test_cpu_percent();
    test_routes();
    test_route_choice();
    test_sanitize();
    test_fonts();
    test_hold();
    test_config();

    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
