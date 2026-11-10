#ifndef UTF8_H
#define UTF8_H

#include <stdint.h>
#include <stddef.h>

/* Decodes one strictly valid UTF-8 sequence (no overlongs, surrogates or code points
 * above U+10FFFF). Returns its length in bytes and stores the code point, or returns 0
 * if s does not start with a valid sequence. s must be NUL-terminated. */
int utf8_decode(const char *s, uint32_t *cp);

#endif
