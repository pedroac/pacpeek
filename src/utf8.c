#include "utf8.h"

int utf8_decode(const char *s, uint32_t *cp) {
    const unsigned char *p = (const unsigned char *)s;
    int len;
    uint32_t c, min;

    if (p[0] < 0x80) { *cp = p[0]; return 1; }
    else if ((p[0] & 0xE0) == 0xC0) { len = 2; c = p[0] & 0x1F; min = 0x80; }
    else if ((p[0] & 0xF0) == 0xE0) { len = 3; c = p[0] & 0x0F; min = 0x800; }
    else if ((p[0] & 0xF8) == 0xF0) { len = 4; c = p[0] & 0x07; min = 0x10000; }
    else return 0;

    for (int i = 1; i < len; i++) {
        if ((p[i] & 0xC0) != 0x80) return 0;
        c = (c << 6) | (p[i] & 0x3F);
    }
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return 0;
    *cp = c;
    return len;
}
