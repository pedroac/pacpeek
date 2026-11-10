#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

/* key is a Linux evdev key code (see linux/input-event-codes.h). */
typedef void (*KeyHandler)(uint32_t key, bool pressed, void *data);

/* on_leave runs when keyboard focus moves to another surface. */
void input_set_handlers(KeyHandler on_key, void (*on_leave)(void *data), void *data);
void input_attach(struct wl_keyboard *keyboard);

#endif
