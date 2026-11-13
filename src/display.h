#ifndef DISPLAY_H
#define DISPLAY_H

#include <wayland-client.h>
#include <cairo/cairo.h>
#include <stdbool.h>

#define MAX_OUTPUTS 32

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
    struct wp_viewport *viewport;
    struct wl_buffer *buffer;
    cairo_surface_t *image;
    void *data;
    size_t size;
    int width, height;      /* buffer size in pixels */
    int dest_w, dest_h;     /* size on screen in logical pixels */
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
    struct { uint32_t name, version; } output_globals[MAX_OUTPUTS];
    int output_global_count;
    struct wl_output *output;
    struct wl_keyboard *keyboard;
    bool wants_keyboard;
    struct wp_viewporter *viewporter;
    struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
    struct wp_fractional_scale_v1 *fractional_scale;
    struct wp_single_pixel_buffer_manager_v1 *single_pixel;
    struct zwlr_layer_shell_v1 *layer_shell;
    uint32_t layer_shell_version;

    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wp_viewport *viewport;
    struct wl_buffer *background;

    /* Dedicated background panel for the radial gradient glow */
    Panel background_panel;

    uint32_t width;   /* logical size of the overlay */
    uint32_t height;
    /* Buffer pixels per logical pixel. Panels are drawn at this scale and shown through a
     * viewport, so text stays sharp on HiDPI outputs. */
    double scale;
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
/* width and height are buffer pixels. Returns NULL while the compositor still holds the
 * previous frame. */
cairo_t *panel_begin(Panel *panel, int width, int height);
/* x, y and dest_w, dest_h are logical: the buffer is stretched to dest_w x dest_h. */
void panel_end(Panel *panel, cairo_t *cr, int x, int y, int dest_w, int dest_h);

#endif
