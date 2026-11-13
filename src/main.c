#define _GNU_SOURCE
#include "config.h"
#include "config_file.h"
#include "display.h"
#include "hold.h"
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
#include <math.h>
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
/* A stuck wpctl or notification daemon must not hold the query (and its refresh) forever. */
#define QUERY_TIMEOUT_MS 1500
/* The first frame waits this long for the query, so notifications and volumes are there from
 * the start instead of appearing (and shifting the layout) a moment later. */
#define FIRST_DRAW_WAIT_MS 300
/* While the overlay stays open, the status is re-read this often. */
#define REFRESH_MS 5000

typedef enum { MODE_HOLD, MODE_TOGGLE, MODE_EXTERNAL } Mode;

typedef struct {
    Mode mode;
    double timeout_s;
    const char *monitor;
    bool toggle; /* if another instance is running, send it SIGTERM and exit */
} Options;

static Options options = { .mode = MODE_HOLD };
static volatile sig_atomic_t quit_requested;

static void on_signal(int sig) {
    (void)sig;
    quit_requested = 1;
}

static HoldTracker hold;

/* Installed before the overlay exists, so keys already held when focus arrives are seen. */
static void on_key(uint32_t key, bool pressed, void *data) {
    (void)data;
    if (options.mode == MODE_HOLD && hold_key(&hold, key, pressed)) quit_requested = 1;
    if (options.mode == MODE_TOGGLE && pressed && key == KEY_ESC) quit_requested = 1;
}

/* Without keyboard focus the overlay can no longer see the key release or Esc. */
static void on_leave(void *data) {
    (void)data;
    quit_requested = 1;
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
            "  -T, --toggle           if already running, close the existing instance and exit;\n"
            "                         otherwise open normally (useful for a single keybind\n"
            "                         that both opens and closes the overlay)\n"
            "  -h, --help             show this help and exit\n"
            "\n"
            "Only one instance runs at a time; SIGTERM or SIGINT also close it.\n"), out);
}

static bool set_mode(const char *value) {
    if (strcmp(value, "hold") == 0) options.mode = MODE_HOLD;
    else if (strcmp(value, "toggle") == 0) options.mode = MODE_TOGGLE;
    else if (strcmp(value, "external") == 0) options.mode = MODE_EXTERNAL;
    else return false;
    return true;
}

static bool set_timeout(const char *value) {
    char *end;
    double seconds = strtod(value, &end);
    if (*end || !(seconds > 0 && seconds <= MAX_TIMEOUT_S)) return false;
    options.timeout_s = seconds;
    return true;
}

/* Settings from the config file. Environment variables the program reads itself are
 * only filled in when unset, so the environment beats the file and the command line
 * beats both. */
static bool on_config(const char *key, const char *value, void *data) {
    (void)data;
    static const struct { const char *key, *env; } envs[] = {
        { "late", "PACPEEK_LATE" },
        { "keyboard", "PACPEEK_KEYBOARD" },
        { "font", "PACPEEK_FONT" },
        { "font_light", "PACPEEK_FONT_LIGHT" },
        { "text_scale", "PACPEEK_TEXT_SCALE" },
        { "time_format", "PACPEEK_TIME_FORMAT" },
        { "date_format", "PACPEEK_DATE_FORMAT" },
    };
    if (strcmp(key, "mode") == 0) return set_mode(value);
    if (strcmp(key, "timeout") == 0) return set_timeout(value);
    if (strcmp(key, "monitor") == 0) {
        options.monitor = strdup(value);
        return options.monitor != NULL;
    }
    for (size_t i = 0; i < sizeof(envs) / sizeof(envs[0]); i++)
        if (strcmp(key, envs[i].key) == 0) return setenv(envs[i].env, value, 0) == 0;
    return false;
}

static bool parse_options(int argc, char **argv) {
    static const struct option longopts[] = {
        { "mode",    required_argument, NULL, 'm' },
        { "timeout", required_argument, NULL, 't' },
        { "monitor", required_argument, NULL, 'M' },
        { "toggle",  no_argument,       NULL, 'T' },
        { "help",    no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 },
    };

    int c;
    while ((c = getopt_long(argc, argv, "m:t:M:Th", longopts, NULL)) != -1) {
        switch (c) {
        case 'm':
            if (!set_mode(optarg)) {
                fprintf(stderr, _("pacpeek: invalid mode \"%s\" (use hold, toggle or external)\n"),
                        optarg);
                return false;
            }
            break;
        case 't':
            if (!set_timeout(optarg)) {
                fprintf(stderr, _("pacpeek: invalid timeout \"%s\" (seconds, 0 to %d)\n"),
                        optarg, MAX_TIMEOUT_S);
                return false;
            }
            break;
        case 'M':
            options.monitor = optarg;
            break;
        case 'T':
            options.toggle = true;
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
 * so no other user can hold it. If it can't be used, pacpeek simply runs unlocked.
 * The lock file also stores the running instance's PID so --toggle can target it. */
static bool acquire_single_instance_lock(bool toggle) {
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

    if (flock(lock_fd, LOCK_EX | LOCK_NB) == 0) {
        /* We hold the lock. Write our PID so a future --toggle invocation can find us. */
        if (ftruncate(lock_fd, 0) < 0) {
            /* Handle or suppress truncation failure if desired, but checking silences the warning */
        }
        dprintf(lock_fd, "%d\n", (int)getpid());
        return true;
    }

    /* Another instance holds the lock. */
    if (toggle) {
        /* Read the PID from the lock file and send SIGTERM to that instance.
         * If the process already exited (ESRCH), kill() fails silently — that's fine. */
        pid_t pid = 0;
        /* The holder truncates the file before writing its PID, so retry briefly if empty. */
        for (int attempt = 0; attempt < 10 && pid <= 0; attempt++) {
            FILE *pf = fopen(path, "r");
            if (pf) {
                if (fscanf(pf, "%d", (int *)&pid) != 1) pid = 0;
                fclose(pf);
            }
            if (pid <= 0) usleep(10 * 1000);
        }
        if (pid > 1) kill(pid, SIGTERM);
    }
    close(fd);
    return false;
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

/* Panels are laid out in buffer pixels but placed in logical ones. A panel's logical size is
 * rounded up and its buffer sized from that, so the compositor's scaling stays exact. */
static bool draw_panel(Display *dpy, Panel *panel, int w, int h, void (*paint)(cairo_t *),
                       int y) {
    double scale = dpy->scale;
    int logical_w = (int)ceil(w / scale - 1e-6);
    int logical_h = (int)ceil(h / scale - 1e-6);
    cairo_t *cr = panel_begin(panel, (int)lround(logical_w * scale), (int)lround(logical_h * scale));
    if (!cr) return false;
    paint(cr);
    panel_end(panel, cr, ((int)dpy->width - logical_w) / 2, y, logical_w, logical_h);
    return true;
}

/* The clock and the status block are centered on screen as one group. */
static bool draw(Display *dpy, Panel *clock, Panel *info, const SystemState *state) {
    render_update(state);

    int clock_w, clock_h, info_w, info_h;
    render_clock_size(&clock_w, &clock_h);
    render_info_size(&info_w, &info_h);
    int gap = render_group_gap();
    int screen_h = (int)lround(dpy->height * dpy->scale);
    int top = (screen_h - (clock_h + gap + info_h)) / 2;

    bool ok = draw_panel(dpy, clock, clock_w, clock_h, render_clock,
                         (int)lround(top / dpy->scale));
    ok &= draw_panel(dpy, info, info_w, info_h, render_info,
                     (int)lround((top + clock_h + gap) / dpy->scale));
    return ok;
}

/* Text is sized from the buffer, so it has to be set up again when the scale changes. */
static bool apply_scale(const Display *dpy) {
    render_finish();
    return render_init((int)lround(dpy->width * dpy->scale), (int)lround(dpy->height * dpy->scale));
}

/* Everything the main loop refreshes on a schedule. A deadline of 0 means "not pending". */
typedef struct {
    SystemState state;
    LateWindow late;
    CpuSample cpu_prev;
    long long minute;
    long long cpu_due;    /* first CPU reading, shortly after opening */
    long long close_at;   /* --timeout */
    long long query_due;  /* gives up on a stuck query */
    long long refresh_due;
    long long first_draw_due;  /* 0 once the first frame is out */
    int query_fd;
    bool dirty;           /* something changed that needs redrawing */
} Schedule;

static void start_query(Schedule *sched) {
    sched->query_fd = query_start();
    sched->query_due = sched->query_fd >= 0 ? now_ms() + QUERY_TIMEOUT_MS : 0;
    if (sched->query_fd < 0) sched->state.query.loaded = true;  /* nothing to wait for */
}

/* False while the first frame is still waiting for the query. */
static bool first_frame_ready(const Schedule *sched) {
    return !sched->first_draw_due || sched->state.query.loaded || now_ms() >= sched->first_draw_due;
}

static void finish_query(Schedule *sched) {
    sched->query_fd = -1;
    sched->query_due = 0;
    sched->dirty = true;
}

static void sample_cpu(Schedule *sched) {
    CpuSample now = system_cpu_sample();
    int percent = system_cpu_percent(sched->cpu_prev, now);
    if (percent >= 0) {
        sched->state.cpu_percent = percent;
        sched->cpu_prev = now;
    }
    sched->cpu_due = 0;
    sched->dirty = true;
}

/* How long the loop may sleep: until the next minute or the nearest pending deadline. */
static int sleep_ms(const Schedule *sched) {
    int timeout = ms_until_next_minute();
    long long deadlines[] = { sched->cpu_due, sched->close_at, sched->query_due,
                              sched->refresh_due, sched->first_draw_due };
    for (size_t i = 0; i < sizeof(deadlines) / sizeof(deadlines[0]); i++) {
        if (!deadlines[i]) continue;
        long long left = deadlines[i] - now_ms();
        if (left < timeout) timeout = left > 0 ? (int)left : 0;
    }
    return timeout;
}

/* Runs whatever became due while the loop slept. Returns false once --timeout has passed. */
static bool run_due(Schedule *sched, bool query_ready) {
    if (query_ready && query_read(&sched->state.query)) finish_query(sched);
    if (sched->query_due && now_ms() >= sched->query_due) {
        query_cancel();
        sched->state.query.loaded = true;
        finish_query(sched);
    }
    if (sched->close_at && now_ms() >= sched->close_at) return false;

    /* The clock changes every minute; the rest is refreshed every few seconds. */
    bool refresh = current_minute() != sched->minute || now_ms() >= sched->refresh_due;
    if (refresh) {
        sched->minute = current_minute();
        sched->refresh_due = now_ms() + REFRESH_MS;
        read_system(&sched->state, &sched->late);
        sched->dirty = true;
    }
    if ((sched->cpu_due && now_ms() >= sched->cpu_due) || refresh) sample_cpu(sched);
    if (refresh && sched->query_fd < 0) start_query(sched);
    return true;
}

/* Blocked except while waiting in display_dispatch(), so no signal is lost between checking
 * quit_requested and going to sleep. */
static void install_signal_handlers(void) {
    struct sigaction sa = { .sa_handler = on_signal };
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    sigprocmask(SIG_BLOCK, &blocked, NULL);
}

/* On-demand focus lets the user move to another window; PACPEEK_KEYBOARD=exclusive restores
 * the grab for compositors that don't focus on-demand surfaces by themselves. Sway hands the
 * focus back right after mapping the surface, which would end hold mode at once, so it gets
 * the grab by default. */
static KeyboardFocus keyboard_focus(void) {
    if (options.mode == MODE_EXTERNAL) return KEYBOARD_NONE;
    const char *env = getenv("PACPEEK_KEYBOARD");
    bool exclusive = env ? strcmp(env, "exclusive") == 0 : getenv("SWAYSOCK") != NULL;
    return exclusive ? KEYBOARD_EXCLUSIVE : KEYBOARD_ON_DEMAND;
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

    config_load(on_config, NULL);
    if (!parse_options(argc, argv)) return 2;
    if (!acquire_single_instance_lock(options.toggle)) return 0;
    install_signal_handlers();

    long long start = now_ms();
    Schedule sched = {
        .state = { .cpu_percent = -1, .query.notifications = -1 },
        .late = system_late_window(getenv("PACPEEK_LATE")),
        .cpu_prev = system_cpu_sample(),
        .cpu_due = start + CPU_FIRST_SAMPLE_MS,
        .refresh_due = start + REFRESH_MS,
        .first_draw_due = start + FIRST_DRAW_WAIT_MS,
        .close_at = options.timeout_s > 0 ? start + (long long)(options.timeout_s * 1000) : 0,
    };
    start_query(&sched);

    /* Read before the overlay appears, so its first frame already has content. */
    read_system(&sched.state, &sched.late);
    sched.minute = current_minute();

    input_set_handlers(on_key, on_leave, NULL);
    Display *dpy = display_create(options.monitor, keyboard_focus(), 0.04, 0.04, 0.05, 0.88);
    if (!dpy) {
        fputs(_("pacpeek: could not create the overlay (needs wlr-layer-shell, "
                "wp_viewporter and wp_single_pixel_buffer_v1)\n"), stderr);
        query_cancel();
        return 1;
    }
    double applied_scale = dpy->scale;
    if (!apply_scale(dpy)) {
        display_destroy(dpy);
        query_cancel();
        return 1;
    }

    Panel clock, info;
    panel_init(&clock, dpy);
    panel_init(&info, dpy);

    while (dpy->running && !quit_requested) {
        if (dpy->scale != applied_scale) {
            applied_scale = dpy->scale;
            if (!apply_scale(dpy)) break;
        }
        if ((dpy->dirty || sched.dirty) && first_frame_ready(&sched) &&
            draw(dpy, &clock, &info, &sched.state)) {
            dpy->dirty = false;
            sched.dirty = false;
            sched.first_draw_due = 0;
        }

        int ret = display_dispatch(dpy, sleep_ms(&sched), sched.query_fd);
        if (ret < 0 || !run_due(&sched, ret == 1)) break;
    }

    query_cancel();
    panel_finish(&clock);
    panel_finish(&info);
    display_destroy(dpy);
    render_finish();
    return 0;
}
