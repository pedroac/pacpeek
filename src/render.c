#include "render.h"
#include "fontfind.h"
#include "i18n.h"
#include "shape.h"
#include "utf8.h"
#include <cairo/cairo-ft.h>
#include <ft2build.h>
#include <locale.h>
#include <math.h>
#include FT_FREETYPE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Sizes are fractions of the screen's shorter side. */
#define CLOCK_SIZE 0.22
#define DATE_SIZE 0.035
#define INFO_SIZE 0.018
#define CLOCK_DATE_GAP 0.01
#define GROUP_GAP 0.05
/* Line height as a multiple of the font height, and the extra space between groups of
 * lines as a multiple of a line. */
#define INFO_LINE_SPACING 1.55
#define INFO_GROUP_SPACING 0.7

typedef struct { double r, g, b; } Color;
static const Color CLOCK_COLOR = { 0.95, 0.95, 0.95 };
static const Color DATE_COLOR = { 0.65, 0.66, 0.70 };
static const Color LEVEL_COLORS[] = {
    { 0.52, 0.53, 0.57 },  /* normal */
    { 0.85, 0.74, 0.42 },  /* warning */
    { 0.87, 0.47, 0.44 },  /* critical */
};

/* Font files are found by name in the system's font directories (see fontfind.h); the
 * earlier names are preferred. */
static const char *const latin_fonts[] = {
    "NotoSans-Regular.ttf", "DejaVuSans.ttf", "LiberationSans-Regular.ttf", NULL,
};

static const char *const light_fonts[] = {
    "NotoSans-Light.ttf", "DejaVuSans-ExtraLight.ttf", NULL,
};

typedef struct {
    const char *prefix;          /* start of the locale name, longest match first */
    const char *const *fonts;
    int face_index;              /* face inside a .ttc: Noto CJK is JP, KR, SC, TC, HK */
    bool rtl;
} Script;

#ifndef NO_I18N

/* DejaVu Sans covers Arabic and Hebrew together with Latin, so a Wi-Fi name in Latin
 * letters still renders inside right-to-left text (Noto's Arabic fonts lack Latin). */
static const char *const rtl_fonts[] = { "DejaVuSans.ttf", NULL };

/* One .ttc holds all the CJK faces: JP, KR, SC, TC, HK (see Script.face_index). */
static const char *const cjk_fonts[] = { "NotoSansCJK-Regular.ttc", NULL };

static const Script scripts[] = {
    { "ja", cjk_fonts, 0, false },
    { "ko", cjk_fonts, 1, false },
    { "zh_TW", cjk_fonts, 3, false },
    { "zh_HK", cjk_fonts, 4, false },
    { "zh", cjk_fonts, 2, false },
    { "ar", rtl_fonts, 0, true },
    { "fa", rtl_fonts, 0, true },
    { "he", rtl_fonts, 0, true },
    { "ur", rtl_fonts, 0, true },
};

#endif

typedef struct {
    char path[256];
    int index;
} FontFile;

/* Text is either drawn by Cairo directly or, when it needs joining/reordering, as
 * glyphs positioned by shape.c. */
typedef struct {
    char utf8[128];
    cairo_glyph_t *glyphs;
    int glyph_count;
    double width;
} Text;

typedef struct {
    cairo_scaled_font_t *font;
    double px;
    Text text;
    double ascent, descent;
    double ink_top; /* height of the drawn glyphs above the baseline */
} Line;

typedef enum { SEG_TEXT, SEG_DOTS, SEG_BELL, SEG_ALERT } SegmentKind;
typedef enum { LEVEL_NORMAL, LEVEL_WARN, LEVEL_CRIT } Level;

typedef struct {
    SegmentKind kind;
    Level level;
    Text text;
    int dots;
    double width;
} Segment;

#define MAX_SEGMENTS 12
#define MAX_INFO_LINES 4

typedef struct {
    Segment segments[MAX_SEGMENTS];
    int count;
    int group;
    double width;
    double top;
} InfoLine;

static FT_Library ft;
static Line time_line, date_line;
static InfoLine info_lines[MAX_INFO_LINES];
static int info_count;
static cairo_scaled_font_t *info_font;
static double info_px, info_ascent, info_cap_height, info_line_height, info_height;
static int base_px, gap_px;
static bool ready;
static bool ui_rtl;
static bool clock_late;
static FontFile regular_file;

static const cairo_user_data_key_t ft_face_key;

static void done_face(void *face) {
    FT_Done_Face(face);
}

/* PACPEEK_FONT (or the like) names a file; otherwise the font is looked up by file name. */
static cairo_font_face_t *load_face(const char *env, const char *const *names, int index,
                                    FontFile *out) {
    const char *path = getenv(env);
    char found[256];
    int face_index = 0;
    if (!path || access(path, R_OK) != 0) {
        FontDirs dirs;
        font_dirs_init(&dirs);
        path = font_find(dirs.list, names, found, sizeof(found)) ? found : NULL;
        face_index = index;
    }
    if (!path) return NULL;

    FT_Face face;
    if (FT_New_Face(ft, path, face_index, &face) != 0) return NULL;

    if (out) {
        snprintf(out->path, sizeof(out->path), "%s", path);
        out->index = face_index;
    }
    cairo_font_face_t *cface = cairo_ft_font_face_create_for_ft_face(face, 0);
    cairo_font_face_set_user_data(cface, &ft_face_key, face, done_face);
    return cface;
}

static cairo_scaled_font_t *scaled(cairo_font_face_t *face, double px) {
    cairo_matrix_t size, identity;
    cairo_matrix_init_scale(&size, px, px);
    cairo_matrix_init_identity(&identity);

    cairo_font_options_t *options = cairo_font_options_create();
    cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_SLIGHT);

    cairo_scaled_font_t *font = cairo_scaled_font_create(face, &size, &identity, options);
    cairo_font_options_destroy(options);
    return font;
}

#ifdef NO_I18N

static const Script *script_for_locale(void) { return NULL; }

#else

static const Script *script_for_locale(void) {
    const char *locale = setlocale(LC_MESSAGES, NULL);
    bool messages_c = !locale || strcmp(locale, "C") == 0 || strcmp(locale, "POSIX") == 0;
    if (messages_c) locale = setlocale(LC_TIME, NULL);

    /* gettext prefers the first LANGUAGE entry unless the locale is plain "C". */
    const char *language = getenv("LANGUAGE");
    if (!messages_c && language && *language && *language != ':') locale = language;
    if (!locale) return NULL;

    for (size_t i = 0; i < sizeof(scripts) / sizeof(scripts[0]); i++)
        if (strncmp(locale, scripts[i].prefix, strlen(scripts[i].prefix)) == 0) return &scripts[i];
    return NULL;
}

#endif

/* PACPEEK_TEXT_SCALE makes all text larger or smaller; anything unusable counts as 1. */
static double text_scale_from_env(void) {
    const char *value = getenv("PACPEEK_TEXT_SCALE");
    if (!value) return 1;
    char *end;
    double scale = strtod(value, &end);
    if (*end || !(scale >= 0.5 && scale <= 2.5)) {
        fprintf(stderr, _("pacpeek: invalid text scale \"%s\" (use 0.5 to 2.5)\n"), value);
        return 1;
    }
    return scale;
}

/* Sizes are in buffer pixels, so a HiDPI output gets proportionally larger text. */
bool render_init(int screen_width, int screen_height) {
    /* FreeType stays initialized: Cairo may keep scaled fonts (and their faces) cached after
     * render_finish(), so the library has to outlive them. */
    if (!ft && FT_Init_FreeType(&ft) != 0) return false;

    const Script *script = script_for_locale();
    ui_rtl = script && script->rtl;

    cairo_font_face_t *regular = NULL;
    if (script) regular = load_face("PACPEEK_FONT", script->fonts, script->face_index, &regular_file);
    if (!regular) regular = load_face("PACPEEK_FONT", latin_fonts, 0, &regular_file);
    if (!regular) {
        fputs(_("pacpeek: no font found; set PACPEEK_FONT to a .ttf/.otf file\n"), stderr);
        return false;
    }
    /* The clock is digits only, so it keeps the Latin light face in every language. */
    cairo_font_face_t *light = load_face("PACPEEK_FONT_LIGHT", light_fonts, 0, NULL);
    if (!light) light = cairo_font_face_reference(regular);

    base_px = screen_height < screen_width ? screen_height : screen_width;
    double text_scale = text_scale_from_env();
    gap_px = base_px * CLOCK_DATE_GAP;
    time_line.px = base_px * CLOCK_SIZE * text_scale;
    time_line.font = scaled(light, time_line.px);
    date_line.px = base_px * DATE_SIZE * text_scale;
    date_line.font = scaled(regular, date_line.px);

    info_px = base_px * INFO_SIZE * text_scale;
    info_font = scaled(regular, info_px);
    cairo_font_extents_t fe;
    cairo_scaled_font_extents(info_font, &fe);
    info_ascent = fe.ascent;
    info_line_height = (fe.ascent + fe.descent) * INFO_LINE_SPACING;
    cairo_text_extents_t cap;
    cairo_scaled_font_text_extents(info_font, "H", &cap);
    info_cap_height = -cap.y_bearing;

    cairo_font_face_destroy(light);
    cairo_font_face_destroy(regular);
    ready = true;
    return true;
}

static void text_clear(Text *t) {
    free(t->glyphs);
    t->glyphs = NULL;
    t->glyph_count = 0;
}

static void free_info(void) {
    for (int i = 0; i < info_count; i++)
        for (int j = 0; j < info_lines[i].count; j++) text_clear(&info_lines[i].segments[j].text);
    info_count = 0;
}

void render_finish(void) {
    if (!ready) return;
    free_info();
    text_clear(&time_line.text);
    text_clear(&date_line.text);
    shape_finish();
    cairo_scaled_font_destroy(time_line.font);
    cairo_scaled_font_destroy(date_line.font);
    cairo_scaled_font_destroy(info_font);
    ready = false;
}

/* Copies whole characters only: Cairo rejects broken UTF-8. */
static void copy_utf8(char *dst, size_t size, const char *src) {
    size_t len = 0;
    while (src[len]) {
        uint32_t cp;
        int n = utf8_decode(src + len, &cp);
        if (n == 0 || len + n >= size) break;
        len += n;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void text_set(Text *t, cairo_scaled_font_t *font, double px, bool allow_shaping,
                     const char *src) {
    text_clear(t);
    copy_utf8(t->utf8, sizeof(t->utf8), src);

    if (allow_shaping && shape_needed(t->utf8)) {
        t->glyphs = shape_text(regular_file.path, regular_file.index, px, ui_rtl, t->utf8,
                               &t->glyph_count, &t->width);
        if (t->glyphs) return;
    }
    cairo_text_extents_t te;
    cairo_scaled_font_text_extents(font, t->utf8, &te);
    t->width = te.x_advance;
}

static void text_draw(cairo_t *cr, const Text *t, double x, double baseline) {
    if (t->glyphs) {
        cairo_save(cr);
        cairo_translate(cr, x, baseline);
        cairo_show_glyphs(cr, t->glyphs, t->glyph_count);
        cairo_restore(cr);
    } else {
        cairo_move_to(cr, x, baseline);
        cairo_show_text(cr, t->utf8);
    }
}

static void set_line(Line *line, bool allow_shaping, const char *src) {
    text_set(&line->text, line->font, line->px, allow_shaping, src);
    cairo_font_extents_t fe;
    cairo_scaled_font_extents(line->font, &fe);
    cairo_text_extents_t te;
    cairo_scaled_font_text_extents(line->font, line->text.utf8, &te);
    line->ascent = fe.ascent;
    line->descent = fe.descent;
    line->ink_top = -te.y_bearing;
}

/* Signal dots and the bell are drawn shapes, since the fonts may lack those glyphs. */
static double dot_diameter(void) { return info_px * 0.42; }
static double dot_step(void) { return dot_diameter() * 1.45; }
static double bell_width(void) { return info_px * 0.72; }
static double alert_height(void) { return info_cap_height * 1.05; }
static double alert_width(void) { return alert_height() * 1.15; }
/* The drawn icons sit slightly high next to mixed-case text, so they are nudged down. */
static double icon_drop(void) { return fmax(1.0, round(info_px * 0.04)); }

static void set_color(cairo_t *cr, Color c) {
    cairo_set_source_rgb(cr, c.r, c.g, c.b);
}

static void level_color(cairo_t *cr, Level level) {
    set_color(cr, LEVEL_COLORS[level]);
}

static Level level_above(int value, int warn, int crit) {
    return value >= crit ? LEVEL_CRIT : value >= warn ? LEVEL_WARN : LEVEL_NORMAL;
}

static void add_segment(InfoLine *line, SegmentKind kind, const char *text, int dots,
                        Level level) {
    if (line->count == MAX_SEGMENTS) return;
    Segment *seg = &line->segments[line->count++];
    seg->kind = kind;
    seg->dots = dots;
    seg->level = level;

    switch (kind) {
    case SEG_TEXT:
        text_set(&seg->text, info_font, info_px, true, text);
        seg->width = seg->text.width;
        break;
    case SEG_DOTS: seg->width = info_px * 0.5 + 4 * dot_step() + dot_diameter(); break;
    case SEG_BELL: seg->width = bell_width() + info_px * 0.4; break;
    case SEG_ALERT: seg->width = alert_width() + info_px * 0.35; break;
    }
    line->width += seg->width;
}

static void add_text(InfoLine *line, const char *text, Level level) {
    if (line->count > 0) add_segment(line, SEG_TEXT, "   \u00B7   ", 0, LEVEL_NORMAL);
    add_segment(line, SEG_TEXT, text, 0, level);
    /* Colour alone is not enough for everyone: attention and critical also get a triangle. */
    if (level != LEVEL_NORMAL) add_segment(line, SEG_ALERT, NULL, 0, level);
}

enum { GROUP_ALERTS, GROUP_STATUS, GROUP_CONTROLS };

/* Past the last slot, lines are built in a scratch slot that commit_line() discards. */
static InfoLine *next_line(int group) {
    static InfoLine overflow;
    InfoLine *line = info_count < MAX_INFO_LINES ? &info_lines[info_count] : &overflow;
    memset(line, 0, sizeof(*line));
    line->group = group;
    return line;
}

static void commit_line(InfoLine *line) {
    if (line->count == 0 || info_count >= MAX_INFO_LINES) return;
    if (info_count > 0) {
        const InfoLine *prev = &info_lines[info_count - 1];
        line->top = prev->top + info_line_height;
        if (prev->group != line->group) line->top += info_line_height * INFO_GROUP_SPACING;
    }
    info_height = line->top + info_line_height;
    info_count++;
}

/* A muted or silent output is flagged so missing sound is explained; a muted mic is
 * usually deliberate, so it stays neutral. */
static void format_volume(InfoLine *line, const char *percent_fmt, const char *muted_text,
                          const Volume *vol, bool flag_silence) {
    if (!vol->valid) return;
    char buf[128];
    if (vol->muted) snprintf(buf, sizeof(buf), "%s", muted_text);
    else snprintf(buf, sizeof(buf), percent_fmt, vol->percent);
    bool silent = vol->muted || vol->percent == 0;
    add_text(line, buf, flag_silence && silent ? LEVEL_WARN : LEVEL_NORMAL);
}

/* These rows depend on the background query. Until it answers, a blank row holds the space;
 * and a row that was ever shown keeps it, so the layout never shifts when its content
 * arrives late, or goes away. */
static bool alerts_shown, controls_shown;

static void reserve_row(InfoLine *line, bool *shown, bool loaded) {
    if (line->count == 0 && (!loaded || *shown)) add_segment(line, SEG_TEXT, " ", 0, LEVEL_NORMAL);
    if (line->count > 0) *shown = true;
}

static void build_alerts(const SystemState *s) {
    char buf[128];
    InfoLine *line = next_line(GROUP_ALERTS);
    if (s->query.notifications >= 0) {
        int n = s->query.notifications;
        add_segment(line, SEG_BELL, NULL, 0, n > 0 ? LEVEL_WARN : LEVEL_NORMAL);
        if (n == 0) snprintf(buf, sizeof(buf), "%s", _("No notifications"));
        else snprintf(buf, sizeof(buf), ngettext("%d notification", "%d notifications", n), n);
        add_segment(line, SEG_TEXT, buf, 0, LEVEL_NORMAL);
    }
    reserve_row(line, &alerts_shown, s->query.loaded);
    commit_line(line);
}

static void build_network(const NetworkStatus *net) {
    char buf[128];
    InfoLine *line = next_line(GROUP_STATUS);
    switch (net->kind) {
    case NET_OFFLINE:  add_text(line, _("Offline"), LEVEL_WARN); break;
    case NET_ETHERNET: add_text(line, _("Ethernet  Connected"), LEVEL_NORMAL); break;
    case NET_OTHER:
        /* TRANSLATORS: %s is a network interface name, e.g. wg0. */
        snprintf(buf, sizeof(buf), _("%s  Connected"), net->iface);
        add_text(line, buf, LEVEL_NORMAL);
        break;
    case NET_WIFI: {
        Level level = net->signal_bars >= 0 && net->signal_bars <= 1 ? LEVEL_WARN : LEVEL_NORMAL;
        /* TRANSLATORS: %s is the Wi-Fi network name (SSID). */
        if (net->ssid[0]) snprintf(buf, sizeof(buf), _("Wi-Fi (%s)"), net->ssid);
        else snprintf(buf, sizeof(buf), "%s", _("Wi-Fi"));
        add_text(line, buf, LEVEL_NORMAL);
        if (net->signal_bars >= 0) add_segment(line, SEG_DOTS, NULL, net->signal_bars, level);
        if (level != LEVEL_NORMAL) add_segment(line, SEG_ALERT, NULL, 0, level);
        break;
    }
    }
    if (net->vpn) add_text(line, _("VPN"), LEVEL_NORMAL);
    commit_line(line);
}

static void build_stats(const SystemState *s) {
    char buf[128];
    InfoLine *line = next_line(GROUP_STATUS);
    if (s->ram_percent >= 0) {
        snprintf(buf, sizeof(buf), _("RAM %d%%"), s->ram_percent);
        add_text(line, buf, level_above(s->ram_percent, 80, 90));
    }
    /* The first sample takes a moment; a placeholder keeps the line from shifting. */
    if (s->cpu_percent >= 0) {
        snprintf(buf, sizeof(buf), _("CPU %d%%"), s->cpu_percent);
        add_text(line, buf, level_above(s->cpu_percent, 85, 95));
    } else {
        add_text(line, _("CPU --%"), LEVEL_NORMAL);
    }
    if (s->cpu_temp >= 0) {
        snprintf(buf, sizeof(buf), _("Temp %d\u00B0C"), s->cpu_temp);
        add_text(line, buf, level_above(s->cpu_temp, 80, 90));
    }
    if (s->battery.present) {
        snprintf(buf, sizeof(buf),
                 s->battery.charging ? _("Battery %d%% charging") : _("Battery %d%%"),
                 s->battery.percent);
        Level level = LEVEL_NORMAL;
        if (!s->battery.charging)
            level = s->battery.percent <= 10 ? LEVEL_CRIT
                  : s->battery.percent <= 20 ? LEVEL_WARN : LEVEL_NORMAL;
        add_text(line, buf, level);
    }
    commit_line(line);
}

static void build_controls(const SystemState *s) {
    InfoLine *line = next_line(GROUP_CONTROLS);
    format_volume(line, _("Volume %d%%"), _("Volume muted"), &s->query.output, true);
    format_volume(line, _("Mic %d%%"), _("Mic muted"), &s->query.input, false);
    reserve_row(line, &controls_shown, s->query.loaded);
    commit_line(line);
}

static void build_info(const SystemState *s) {
    free_info();
    info_height = 0;
    build_alerts(s);
    build_network(&s->network);
    build_stats(s);
    build_controls(s);
}

/* The user's strftime format (from the environment variable) wins over the translated one;
 * one that gives nothing falls back to it. */
static void format_time(char *buf, size_t size, const char *env, const char *fallback,
                        const struct tm *t) {
    const char *custom = getenv(env);
    if (custom && *custom && strftime(buf, size, custom, t)) return;
    if (!strftime(buf, size, fallback, t)) buf[0] = '\0';
}

void render_update(const SystemState *s) {
    char buf[128];

    /* TRANSLATORS: strftime(3) format for the clock, e.g. "%I:%M %p" for 12-hour time. */
    format_time(buf, sizeof(buf), "PACPEEK_TIME_FORMAT", _("%H:%M"), &s->time);
    set_line(&time_line, false, buf);

    /* TRANSLATORS: strftime(3) date format; reorder freely, e.g. "%A, %-d de %B de %Y". */
    format_time(buf, sizeof(buf), "PACPEEK_DATE_FORMAT", _("%A, %-d %B %Y"), &s->time);
    set_line(&date_line, true, buf);

    clock_late = s->late;
    build_info(s);
}

static void show(cairo_t *cr, const Line *line, double box_width, double top) {
    cairo_set_scaled_font(cr, line->font);
    text_draw(cr, &line->text, (box_width - line->text.width) / 2, top + line->ascent);
}

/* The clock's buffer starts at the top of the digits rather than the font's ascent,
 * which would add a band of empty pixels as tall as a third of the digits. */
static double clock_baseline(void) { return ceil(time_line.ink_top) + 1; }
static double date_top(void) { return clock_baseline() + time_line.descent + gap_px; }

void render_clock_size(int *width, int *height) {
    double w = time_line.text.width > date_line.text.width ? time_line.text.width
                                                            : date_line.text.width;
    *width = (int)w + 1;
    *height = (int)(date_top() + date_line.ascent + date_line.descent) + 1;
}

void render_clock(cairo_t *cr) {
    int w, h;
    render_clock_size(&w, &h);
    if (clock_late) level_color(cr, LEVEL_WARN);
    else set_color(cr, CLOCK_COLOR);
    cairo_set_scaled_font(cr, time_line.font);
    text_draw(cr, &time_line.text, (w - time_line.text.width) / 2, clock_baseline());

    set_color(cr, DATE_COLOR);
    show(cr, &date_line, w, date_top());
}

/* Mirrored for right-to-left layouts: the strongest bar is on the right. */
static void draw_dots(cairo_t *cr, double x, double center_y, int filled) {
    double r = dot_diameter() / 2;
    cairo_set_line_width(cr, r * 0.25);
    for (int i = 0; i < 5; i++) {
        bool on = (ui_rtl ? 4 - i : i) < filled;
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + r + i * dot_step(), center_y, on ? r : r * 0.88, 0, 2 * M_PI);
        if (on) cairo_fill(cr);
        else cairo_stroke(cr);
    }
}

static void draw_bell(cairo_t *cr, double x, double baseline) {
    double w = bell_width();
    double cx = x + w / 2;
    double r = w * 0.31;
    double clapper = info_cap_height * 0.11;
    double top = baseline - info_cap_height;
    double dome = top + r;
    double bottom = baseline - clapper * 2.2;

    cairo_new_path(cr);
    cairo_arc(cr, cx, dome, r, M_PI, 2 * M_PI);
    cairo_curve_to(cr, cx + r, bottom - (bottom - dome) * 0.3, cx + w / 2, bottom - clapper * 0.6,
                   cx + w / 2, bottom);
    cairo_line_to(cr, cx - w / 2, bottom);
    cairo_curve_to(cr, cx - w / 2, bottom - clapper * 0.6, cx - r, bottom - (bottom - dome) * 0.3,
                   cx - r, dome);
    cairo_close_path(cr);
    cairo_fill(cr);

    cairo_arc(cr, cx, baseline - clapper, clapper, 0, 2 * M_PI);
    cairo_fill(cr);
}

/* A warning triangle: outlined when something needs attention, solid when it is critical. */
static void draw_alert(cairo_t *cr, double x, double baseline, Level level) {
    double h = alert_height(), w = alert_width();
    double top = baseline - h;
    double stem_x = x + w / 2;

    cairo_save(cr);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_new_path(cr);
    cairo_move_to(cr, stem_x, top);
    cairo_line_to(cr, x + w, baseline);
    cairo_line_to(cr, x, baseline);
    cairo_close_path(cr);
    if (level == LEVEL_CRIT) {
        cairo_fill(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);   /* the mark is cut out of the solid shape */
    } else {
        cairo_set_line_width(cr, h * 0.1);
        cairo_stroke(cr);
    }
    cairo_set_line_width(cr, h * 0.11);
    cairo_move_to(cr, stem_x, top + h * 0.42);
    cairo_line_to(cr, stem_x, top + h * 0.68);
    cairo_stroke(cr);
    cairo_arc(cr, stem_x, baseline - h * 0.14, h * 0.07, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_restore(cr);
}

void render_info_size(int *width, int *height) {
    double w = 0;
    for (int i = 0; i < info_count; i++)
        if (info_lines[i].width > w) w = info_lines[i].width;
    *width = (int)w + 1;
    *height = (int)info_height + 1;
}

void render_info(cairo_t *cr) {
    int box_width, box_height;
    render_info_size(&box_width, &box_height);
    cairo_set_scaled_font(cr, info_font);

    for (int i = 0; i < info_count; i++) {
        const InfoLine *line = &info_lines[i];
        double baseline = line->top + info_ascent;
        double x = (box_width - line->width) / 2;

        for (int j = 0; j < line->count; j++) {
            /* Segments are stored in reading order, so right-to-left lays them out reversed. */
            const Segment *seg = &line->segments[ui_rtl ? line->count - 1 - j : j];
            level_color(cr, seg->level);
            switch (seg->kind) {
            case SEG_TEXT:
                text_draw(cr, &seg->text, x, baseline);
                break;
            case SEG_DOTS:
                draw_dots(cr, ui_rtl ? x : x + info_px * 0.5, baseline - info_px * 0.36 + icon_drop(),
                          seg->dots);
                break;
            case SEG_ALERT:
                draw_alert(cr, ui_rtl ? x : x + info_px * 0.35, baseline + icon_drop(), seg->level);
                break;
            case SEG_BELL:
                draw_bell(cr, ui_rtl ? x + info_px * 0.4 : x, baseline + icon_drop() + 1);
                break;
            }
            x += seg->width;
        }
    }
}

int render_group_gap(void) {
    return base_px * GROUP_GAP;
}
