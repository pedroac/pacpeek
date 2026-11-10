#include "shape.h"
#include "utf8.h"
#include <fribidi.h>
#include <hb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CHARS 160
#define SUBPIXEL 64

bool shape_needed(const char *utf8) {
    while (*utf8) {
        uint32_t cp;
        int len = utf8_decode(utf8, &cp);
        /* Hebrew through Myanmar: RTL scripts, Indic scripts, Thai, Lao and friends. */
        if (len && cp >= 0x0590 && cp < 0x1100) return true;
        utf8 += len ? len : 1;
    }
    return false;
}

static hb_font_t *cached_font;
static char cached_path[256];
static int cached_index;

static hb_font_t *font_for(const char *path, int index, double px) {
    if (!cached_font || strcmp(cached_path, path) != 0 || cached_index != index) {
        if (cached_font) hb_font_destroy(cached_font);
        cached_font = NULL;

        hb_blob_t *blob = hb_blob_create_from_file(path);
        if (hb_blob_get_length(blob) == 0) {
            hb_blob_destroy(blob);
            return NULL;
        }
        hb_face_t *face = hb_face_create(blob, index);
        hb_blob_destroy(blob);
        cached_font = hb_font_create(face);
        hb_face_destroy(face);
        snprintf(cached_path, sizeof(cached_path), "%s", path);
        cached_index = index;
    }

    int scale = (int)(px * SUBPIXEL + 0.5);
    hb_font_set_scale(cached_font, scale, scale);
    return cached_font;
}

typedef struct {
    int start, end, level;
} Run;

/* Rule L2 of the Unicode bidi algorithm, applied to whole runs. */
static void reorder_runs(Run *runs, int count) {
    int highest = 0, lowest_odd = 255;
    for (int i = 0; i < count; i++) {
        if (runs[i].level > highest) highest = runs[i].level;
        if ((runs[i].level & 1) && runs[i].level < lowest_odd) lowest_odd = runs[i].level;
    }

    for (int level = highest; level >= lowest_odd && level >= 1; level--) {
        for (int i = 0; i < count;) {
            if (runs[i].level < level) {
                i++;
                continue;
            }
            int j = i;
            while (j < count && runs[j].level >= level) j++;
            for (int a = i, b = j - 1; a < b; a++, b--) {
                Run tmp = runs[a];
                runs[a] = runs[b];
                runs[b] = tmp;
            }
            i = j;
        }
    }
}

cairo_glyph_t *shape_text(const char *font_path, int font_index, double px, bool rtl,
                          const char *utf8, int *count, double *width) {
    size_t bytes = strlen(utf8);
    if (bytes == 0 || bytes >= MAX_CHARS) return NULL;

    hb_font_t *font = font_for(font_path, font_index, px);
    if (!font) return NULL;

    FriBidiChar chars[MAX_CHARS];
    FriBidiStrIndex n = fribidi_charset_to_unicode(FRIBIDI_CHAR_SET_UTF8, utf8, bytes, chars);
    if (n <= 0) return NULL;

    FriBidiCharType types[MAX_CHARS];
    FriBidiBracketType brackets[MAX_CHARS];
    FriBidiLevel levels[MAX_CHARS];
    FriBidiParType direction = rtl ? FRIBIDI_PAR_RTL : FRIBIDI_PAR_LTR;
    fribidi_get_bidi_types(chars, n, types);
    fribidi_get_bracket_types(chars, n, types, brackets);
    if (fribidi_get_par_embedding_levels_ex(types, brackets, n, &direction, levels) == 0)
        return NULL;

    Run runs[MAX_CHARS];
    int run_count = 0;
    for (int i = 0; i < n;) {
        int j = i + 1;
        while (j < n && levels[j] == levels[i]) j++;
        runs[run_count++] = (Run){ i, j, levels[i] };
        i = j;
    }
    reorder_runs(runs, run_count);

    cairo_glyph_t *glyphs = NULL;
    int total = 0, capacity = 0;
    double x = 0;

    for (int r = 0; r < run_count; r++) {
        bool run_rtl = runs[r].level & 1;
        uint32_t text[MAX_CHARS];
        int len = runs[r].end - runs[r].start;
        for (int k = 0; k < len; k++) text[k] = chars[runs[r].start + k];

        hb_buffer_t *buffer = hb_buffer_create();
        hb_buffer_add_utf32(buffer, text, len, 0, len);
        hb_buffer_set_direction(buffer, run_rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
        hb_buffer_guess_segment_properties(buffer);
        hb_shape(font, buffer, NULL, 0);

        unsigned int glyph_count;
        hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buffer, &glyph_count);
        hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buffer, &glyph_count);

        if (total + (int)glyph_count > capacity) {
            capacity = total + glyph_count + 16;
            cairo_glyph_t *grown = realloc(glyphs, capacity * sizeof(*glyphs));
            if (!grown) {
                free(glyphs);
                hb_buffer_destroy(buffer);
                return NULL;
            }
            glyphs = grown;
        }
        for (unsigned int g = 0; g < glyph_count; g++) {
            glyphs[total].index = info[g].codepoint;
            glyphs[total].x = x + pos[g].x_offset / (double)SUBPIXEL;
            glyphs[total].y = -pos[g].y_offset / (double)SUBPIXEL;
            x += pos[g].x_advance / (double)SUBPIXEL;
            total++;
        }
        hb_buffer_destroy(buffer);
    }

    *count = total;
    *width = x;
    return glyphs;
}

void shape_finish(void) {
    if (cached_font) hb_font_destroy(cached_font);
    cached_font = NULL;
}
