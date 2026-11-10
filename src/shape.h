#ifndef SHAPE_H
#define SHAPE_H

#include "config.h"
#include <cairo/cairo.h>
#include <stdbool.h>

#ifdef NO_I18N

static inline bool shape_needed(const char *utf8) { (void)utf8; return false; }
static inline cairo_glyph_t *shape_text(const char *font_path, int font_index, double px,
                                        bool rtl, const char *utf8, int *count, double *width) {
    (void)font_path; (void)font_index; (void)px; (void)rtl; (void)utf8; (void)count; (void)width;
    return NULL;
}
static inline void shape_finish(void) {}

#else

/* True if the text has characters that need joining or reordering (Arabic, Hebrew,
 * Indic, ...). Everything else is drawn directly by Cairo, which costs less. */
bool shape_needed(const char *utf8);

/* Shapes and reorders (bidi) utf8 with the font file at path. rtl is the base direction.
 * Returns a malloc'd glyph array positioned relative to the text's left edge on the
 * baseline, or NULL on failure; the caller frees it. */
cairo_glyph_t *shape_text(const char *font_path, int font_index, double px, bool rtl,
                          const char *utf8, int *count, double *width);

void shape_finish(void);

#endif

#endif
