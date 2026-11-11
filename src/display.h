#ifndef DISPLAY_H
#define DISPLAY_H

#include <wayland-client.h>
#include <cairo/cairo.h>
#include <stdbool.h>

typedef struct Display Display;

typedef enum {
    KEYBOARD_NONE,       /* never takes keyboard focus; the compositor must close us */
    KEYBOARD_ON_DEMAND,  /* the compositor may focus us, and the user can move away */
    KEYBOARD_EXCLUSIVE,  /* grabs the keyboard */
} KeyboardFocus;

typedef struct {
    Display *dpy;
    struct wl_surface *surface;
    struct wl_subsurface *subsurface;
    struct wl_buffer *buffer;
    cairo_surface_t *image;
    void *data;
    size_t size;
    int width, height;
    int x, y;
    bool busy;
} Panel;

struct Display {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_subcompositor *subcompositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct { uint32_t name, version; } output_globals[16];
    int output_global_count;
    struct wl_output *output;
    struct wl_keyboard *keyboard;
    bool wants_keyboard;
    struct wp_viewporter *viewporter;
    struct wp_single_pixel_buffer_manager_v1 *single_pixel;
    struct zwlr_layer_shell_v1 *layer_shell;
    uint32_t layer_shell_version;

    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wp_viewport *viewport;
    struct wl_buffer *background;

    /* Dedicated background panel for the radial gradient glow */
    Panel background_panel;

    uint32_t width;
    uint32_t height;
    bool configured;
    bool dirty;
    bool running;
};

/* Background color as straight (non-premultiplied) RGBA in 0..1.
 * output_name selects a monitor (e.g. "DP-1"); NULL lets the compositor choose. */
Display *display_create(const char *output_name, KeyboardFocus focus,
                        double r, double g, double b, double a);
void display_destroy(Display *dpy);
/* Returns -1 on error, 1 if extra_fd (ignored when < 0) became readable, else 0. */
int display_dispatch(Display *dpy, int timeout_ms, int extra_fd);

void panel_init(Panel *panel, Display *dpy);
void panel_finish(Panel *panel);
/* Returns NULL while the compositor still holds the previous frame. */
cairo_t *panel_begin(Panel *panel, int width, int height);
void panel_end(Panel *panel, cairo_t *cr, int x, int y);

#endif
