#define _GNU_SOURCE
#include "config.h"
#include "display.h"
#include "i18n.h"
#include "input.h"
#include "network.h"
#include "query.h"
#include "render.h"
#include "system.h"
#include <fcntl.h>
#include <getopt.h>
#include <linux/input-event-codes.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CPU_FIRST_SAMPLE_MS 500
#define MAX_TIMEOUT_S (24 * 60 * 60)

typedef enum { MODE_HOLD, MODE_TOGGLE, MODE_EXTERNAL } Mode;

typedef struct {
    Mode mode;
    double timeout_s;
    const char *monitor;
} Options;

static Options options = { .mode = MODE_HOLD };
static volatile sig_atomic_t quit_requested;

static void on_signal(int sig) {
    (void)sig;
    quit_requested = 1;
}

static void on_key(uint32_t key, bool pressed, void *data) {
    Display *dpy = data;
    if (options.mode == MODE_HOLD && !pressed) dpy->running = false;
    if (options.mode == MODE_TOGGLE && pressed && key == KEY_ESC) dpy->running = false;
}

/* Without keyboard focus the overlay can no longer see the key release or Esc. */
static void on_leave(void *data) {
    ((Display *)data)->running = false;
}

static void usage(FILE *out) {
    fputs(_("Usage: pacpeek [OPTION]...\n"
            "Show the time, date and system status in a fullscreen overlay.\n"
            "\n"
            "  -m, --mode MODE        hold (default): close when a key is released\n"
            "                         toggle: close when Esc is pressed\n"
            "                         external: take no keyboard focus; close it from outside (SIGTERM)\n"
            "  -t, --timeout SECONDS  close automatically after SECONDS\n"
            "  -M, --monitor NAME     show on monitor NAME (e.g. DP-1)\n"
            "  -h, --help             show this help and exit\n"
            "\n"
            "Only one instance runs at a time; SIGTERM or SIGINT also close it.\n"), out);
}

static bool parse_options(int argc, char **argv) {
    static const struct option longopts[] = {
        { "mode", required_argument, NULL, 'm' },
        { "timeout", required_argument, NULL, 't' },
        { "monitor", required_argument, NULL, 'M' },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 },
    };

    int c;
    while ((c = getopt_long(argc, argv, "m:t:M:h", longopts, NULL)) != -1) {
        switch (c) {
        case 'm':
            if (strcmp(optarg, "hold") == 0) options.mode = MODE_HOLD;
            else if (strcmp(optarg, "toggle") == 0) options.mode = MODE_TOGGLE;
            else if (strcmp(optarg, "external") == 0) options.mode = MODE_EXTERNAL;
            else {
                fprintf(stderr, _("pacpeek: invalid mode \"%s\" (use hold, toggle or external)\n"),
                        optarg);
                return false;
            }
            break;
        case 't': {
            char *end;
            options.timeout_s = strtod(optarg, &end);
            if (*end || !(options.timeout_s > 0 && options.timeout_s <= MAX_TIMEOUT_S)) {
                fprintf(stderr, _("pacpeek: invalid timeout \"%s\" (seconds, 0 to %d)\n"),
                        optarg, MAX_TIMEOUT_S);
                return false;
            }
            break;
        }
        case 'M':
            options.monitor = optarg;
            break;
        case 'h':
            usage(stdout);
            exit(0);
        default:
            usage(stderr);
            return false;
        }
    }
    if (optind < argc) {
        fprintf(stderr, _("pacpeek: unexpected argument \"%s\"\n"), argv[optind]);
        usage(stderr);
        return false;
    }
    return true;
}

/* The lock lives in the user's private runtime directory, never a shared one like /tmp,
 * so no other user can hold it. If it can't be used, pacpeek simply runs unlocked. */
static bool acquire_single_instance_lock(void) {
    char dir[64], path[256];
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime || !*runtime) {
        snprintf(dir, sizeof(dir), "/run/user/%u", (unsigned)getuid());
        runtime = dir;
    }
    snprintf(path, sizeof(path), "%s/pacpeek.lock", runtime);

    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return true;

    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid()) {
        close(fd);
        return true;
    }
    /* Keep fd open for the process lifetime: flock(2) advisory locks are released when
     * all fds referencing the open-file-description are closed, so we must not let it
     * go out of scope. A static is enough — only one lock is ever acquired. */
    static int lock_fd = -1;
    lock_fd = fd;
    return flock(lock_fd, LOCK_EX | LOCK_NB) == 0;
}

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static long long current_minute(void) {
    return time(NULL) / 60;
}

static int ms_until_next_minute(void) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return (int)((60 - now.tv_sec % 60) * 1000 - now.tv_nsec / 1000000);
}

static void read_system(SystemState *state, const LateWindow *late) {
    state->time = system_get_time();
    state->late = system_is_late(late, &state->time);
    state->ram_percent = system_ram_percent();
    state->cpu_temp = system_cpu_temp();
    state->battery = system_battery();
    network_get_status(&state->network);
}

static bool draw_panel(Panel *panel, void (*size)(int *, int *), void (*paint)(cairo_t *),
                       int x_center, int y) {
    int w, h;
    size(&w, &h);
    cairo_t *cr = panel_begin(panel, w, h);
    if (!cr) return false;
    paint(cr);
    panel_end(panel, cr, x_center - w / 2, y);
    return true;
}

static bool draw(Display *dpy, Panel *clock, Panel *info, const SystemState *state) {
    render_update(state);

    int unused, clock_h, info_h;
    render_clock_size(&unused, &clock_h);
    render_info_size(&unused, &info_h);
    int gap = render_group_gap();
    int top = ((int)dpy->height - (clock_h + gap + info_h)) / 2;
    int cx = dpy->width / 2;

    bool ok = draw_panel(clock, render_clock_size, render_clock, cx, top);
    ok &= draw_panel(info, render_info_size, render_info, cx, top + clock_h + gap);
    return ok;
}

int main(int argc, char **argv) {
    /* Only what is displayed is localized; other categories would just cost memory. */
#ifndef NO_I18N
    setlocale(LC_TIME, "");
    setlocale(LC_MESSAGES, "");
    bindtextdomain("pacpeek", LOCALEDIR);
    bind_textdomain_codeset("pacpeek", "UTF-8");
    textdomain("pacpeek");
#endif

    if (!parse_options(argc, argv)) return 2;
    if (!acquire_single_instance_lock()) return 0;

    struct sigaction sa = { .sa_handler = on_signal };
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    CpuSample cpu_prev = system_cpu_sample();
    long long start = now_ms();
    long long cpu_due = start + CPU_FIRST_SAMPLE_MS;
    long long close_at = options.timeout_s > 0 ? start + (long long)(options.timeout_s * 1000) : 0;
    int query_fd = query_start();

    /* On-demand focus lets the user move to another window; PACPEEK_KEYBOARD=exclusive
     * restores the grab for compositors that don't focus on-demand surfaces by themselves. */
    const char *keyboard_env = getenv("PACPEEK_KEYBOARD");
    KeyboardFocus focus = options.mode == MODE_EXTERNAL ? KEYBOARD_NONE
        : keyboard_env && strcmp(keyboard_env, "exclusive") == 0 ? KEYBOARD_EXCLUSIVE
        : KEYBOARD_ON_DEMAND;
    Display *dpy = display_create(options.monitor, focus, 0.04, 0.04, 0.05, 0.88);
    if (!dpy) {
        fputs(_("pacpeek: could not create the overlay (needs wlr-layer-shell, "
                "wp_viewporter and wp_single_pixel_buffer_v1)\n"), stderr);
        query_cancel();
        return 1;
    }
    if (!render_init(dpy->width, dpy->height)) {
        display_destroy(dpy);
        query_cancel();
        return 1;
    }
    input_set_handlers(on_key, on_leave, dpy);

    Panel clock, info;
    panel_init(&clock, dpy);
    panel_init(&info, dpy);

    SystemState state = { .cpu_percent = -1, .query.notifications = -1 };
    LateWindow late = system_late_window(getenv("PACPEEK_LATE"));
    read_system(&state, &late);
    long long minute = current_minute();

    while (dpy->running && !quit_requested) {
        if (dpy->dirty && draw(dpy, &clock, &info, &state)) dpy->dirty = false;

        int timeout = ms_until_next_minute();
        long long deadlines[] = { cpu_due, close_at };
        for (int i = 0; i < 2; i++) {
            if (!deadlines[i]) continue;
            long long left = deadlines[i] - now_ms();
            if (left < timeout) timeout = left > 0 ? (int)left : 0;
        }

        int ret = display_dispatch(dpy, timeout, query_fd);
        if (ret < 0) break;
        if (ret == 1 && query_read(&state.query)) {
            query_fd = -1;
            dpy->dirty = true;
        }

        if (close_at && now_ms() >= close_at) break;

        bool new_minute = current_minute() != minute;
        if (new_minute) {
            minute = current_minute();
            read_system(&state, &late);
            dpy->dirty = true;
        }
        if ((cpu_due && now_ms() >= cpu_due) || new_minute) {
            CpuSample cpu_now = system_cpu_sample();
            int percent = system_cpu_percent(cpu_prev, cpu_now);
            if (percent >= 0) {
                state.cpu_percent = percent;
                cpu_prev = cpu_now;
            }
            cpu_due = 0;
            dpy->dirty = true;
        }
        if (new_minute && query_fd < 0) query_fd = query_start();
    }

    query_cancel();
    panel_finish(&clock);
    panel_finish(&info);
    display_destroy(dpy);
    render_finish();
    return 0;
}
