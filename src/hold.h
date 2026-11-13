#ifndef HOLD_H
#define HOLD_H

#include <stdbool.h>
#include <stdint.h>

#define HOLD_MAX_KEYS 32

/* Tracks the keys held while the overlay is open, to close it once they are all released. */
typedef struct {
    uint32_t keys[HOLD_MAX_KEYS];
    int count;
} HoldTracker;

/* Feeds one key event. Returns true when the overlay should close.
 *
 * A release of a key never seen pressed (compositors differ in what they forward) is ignored
 * while other keys are held; with nothing held, any release closes, as there is no shortcut
 * left to wait for. */
bool hold_key(HoldTracker *hold, uint32_t key, bool pressed);

#endif
