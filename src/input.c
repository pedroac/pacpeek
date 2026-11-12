#include "input.h"
#include <unistd.h>

static KeyHandler key_handler;
static void (*leave_handler)(void *data);
static void *handler_data;

/* Keys are matched by evdev code, so no keymap is needed. */
static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
                            uint32_t format, int fd, uint32_t size) {
    (void)data; (void)keyboard; (void)format; (void)size;
    close(fd);
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface, struct wl_array *keys) {
    (void)data; (void)keyboard; (void)serial; (void)surface;
    /* Keys already held when focus arrives (the shortcut that launched us) count as pressed. */
    if (!key_handler) return;
    uint32_t *key;
    wl_array_for_each(key, keys) key_handler(*key, true, handler_data);
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface) {
    (void)data; (void)keyboard; (void)serial; (void)surface;
    if (leave_handler) leave_handler(handler_data);
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t state) {
    (void)data; (void)keyboard; (void)serial; (void)time;
    if (key_handler)
        key_handler(key, state == WL_KEYBOARD_KEY_STATE_PRESSED, handler_data);
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                               uint32_t depressed, uint32_t latched, uint32_t locked,
                               uint32_t group) {
    (void)data; (void)keyboard; (void)serial;
    (void)depressed; (void)latched; (void)locked; (void)group;
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
                                 int32_t rate, int32_t delay) {
    (void)data; (void)keyboard; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

void input_set_handlers(KeyHandler on_key, void (*on_leave)(void *data), void *data) {
    key_handler = on_key;
    leave_handler = on_leave;
    handler_data = data;
}

void input_attach(struct wl_keyboard *keyboard) {
    wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
}
