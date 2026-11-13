#include "hold.h"

bool hold_key(HoldTracker *hold, uint32_t key, bool pressed) {
    int i = 0;
    while (i < hold->count && hold->keys[i] != key) i++;

    if (pressed) {
        if (i == hold->count && hold->count < HOLD_MAX_KEYS) hold->keys[hold->count++] = key;
        return false;
    }
    if (i < hold->count) {
        hold->keys[i] = hold->keys[--hold->count];
        return hold->count == 0;
    }
    return hold->count == 0;
}
