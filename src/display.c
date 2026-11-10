#define _GNU_SOURCE
#include "display.h"
#include "i18n.h"
#include "input.h"
#include "single-pixel-buffer-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    Display *dpy = data;
    bool has_keyboard = caps & WL_SEAT_CAPABILITY_KEYBOARD;

    if (has_keyboard && dpy->wants_keyboard && !dpy->keyboard) {
        dpy->keyboard = wl_seat_get_keyboard(seat);
        input_attach(dpy->keyboard);
    } else if (!has_keyboard && dpy->keyboard) {
        wl_keyboard_release(dpy->keyboard);
        dpy->keyboard = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

#define BIND(iface, ver) wl_registry_bind(registry, name, &iface##_interface, \
                                          version < (ver) ? version : (ver))

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface, uint32_t version) {
    Display *dpy = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        dpy->compositor = BIND(wl_compositor, 4);
    } else if (strcmp(interface, wl_subcompositor_interface.name) == 0) {
        dpy->subcompositor = BIND(wl_subcompositor, 1);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        dpy->shm = BIND(wl_shm, 1);
    } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        dpy->viewporter = BIND(wp_viewporter, 1);
    } else if (strcmp(interface, wp_single_pixel_buffer_manager_v1_interface.name) == 0) {
        dpy->single_pixel = BIND(wp_single_pixel_buffer_manager_v1, 1);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        dpy->layer_shell = BIND(zwlr_layer_shell_v1, 4);
        dpy->layer_shell_version = version < 4 ? version : 4;
    } else if (strcmp(interface, wl_seat_interface.name) == 0 && !dpy->seat) {
        dpy->seat = BIND(wl_seat, 5);
        wl_seat_add_listener(dpy->seat, &seat_listener, dpy);
    } else if (strcmp(interface, wl_output_interface.name) == 0 &&
               dpy->output_global_count < 16) {
        dpy->output_globals[dpy->output_global_count].name = name;
        dpy->output_globals[dpy->output_global_count].version = version;
        dpy->output_global_count++;
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t width, uint32_t height) {
    Display *dpy = data;
    zwlr_layer_surface_v1_ack_configure(surface, serial);

    if (width != dpy->width || height != dpy->height) {
        dpy->width = width;
        dpy->height = height;
        wp_viewport_set_destination(dpy->viewport, width, height);
    }
    wl_surface_attach(dpy->surface, dpy->background, 0, 0);
    wl_surface_damage_buffer(dpy->surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(dpy->surface);

    dpy->configured = true;
    dpy->dirty = true;
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface) {
    (void)surface;
    ((Display *)data)->running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static uint32_t to_u32(double v) {
    return (uint32_t)(v * UINT32_MAX);
}

typedef struct {
    struct wl_output *output;
    char name[64];
} OutputInfo;

static void output_geometry(void *data, struct wl_output *o, int32_t x, int32_t y,
                            int32_t pw, int32_t ph, int32_t subpixel, const char *make,
                            const char *model, int32_t transform) {
    (void)data; (void)o; (void)x; (void)y; (void)pw; (void)ph;
    (void)subpixel; (void)make; (void)model; (void)transform;
}

static void output_mode(void *data, struct wl_output *o, uint32_t flags,
                        int32_t w, int32_t h, int32_t refresh) {
    (void)data; (void)o; (void)flags; (void)w; (void)h; (void)refresh;
}

static void output_done(void *data, struct wl_output *o) { (void)data; (void)o; }

static void output_scale(void *data, struct wl_output *o, int32_t factor) {
    (void)data; (void)o; (void)factor;
}

static void output_name(void *data, struct wl_output *o, const char *name) {
    (void)o;
    snprintf(((OutputInfo *)data)->name, sizeof(((OutputInfo *)data)->name), "%s", name);
}

static void output_description(void *data, struct wl_output *o, const char *desc) {
    (void)data; (void)o; (void)desc;
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

static struct wl_output *find_output(Display *dpy, const char *wanted) {
    OutputInfo infos[16] = {0};
    int count = 0;

    for (int i = 0; i < dpy->output_global_count; i++) {
        if (dpy->output_globals[i].version < 4) continue;
        infos[count].output = wl_registry_bind(dpy->registry, dpy->output_globals[i].name,
                                               &wl_output_interface, 4);
        wl_output_add_listener(infos[count].output, &output_listener, &infos[count]);
        count++;
    }
    wl_display_roundtrip(dpy->display);

    struct wl_output *found = NULL;
    for (int i = 0; i < count; i++) {
        if (!found && strcmp(infos[i].name, wanted) == 0) found = infos[i].output;
        else wl_output_release(infos[i].output);
    }

    if (!found) {
        fprintf(stderr, _("pacpeek: monitor \"%s\" not found; available:"), wanted);
        for (int i = 0; i < count; i++) fprintf(stderr, " %s", infos[i].name);
        fprintf(stderr, "\n");
    }
    return found;
}

Display *display_create(const char *output_name, KeyboardFocus focus,
                        double r, double g, double b, double a) {
    Display *dpy = calloc(1, sizeof(Display));
    if (!dpy) return NULL;
    dpy->wants_keyboard = focus != KEYBOARD_NONE;

    dpy->display = wl_display_connect(NULL);
    if (!dpy->display) {
        free(dpy);
        return NULL;
    }

    dpy->registry = wl_display_get_registry(dpy->display);
    wl_registry_add_listener(dpy->registry, &registry_listener, dpy);
    wl_display_roundtrip(dpy->display);

    if (!dpy->compositor || !dpy->subcompositor || !dpy->shm || !dpy->viewporter ||
        !dpy->single_pixel || !dpy->layer_shell) {
        display_destroy(dpy);
        return NULL;
    }

    if (output_name) dpy->output = find_output(dpy, output_name);

    dpy->background = wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(
        dpy->single_pixel, to_u32(r * a), to_u32(g * a), to_u32(b * a), to_u32(a));

    dpy->surface = wl_compositor_create_surface(dpy->compositor);
    dpy->viewport = wp_viewporter_get_viewport(dpy->viewporter, dpy->surface);
    dpy->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        dpy->layer_shell, dpy->surface, dpy->output,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "pacpeek");

    /* The layer surface only needs the output while it is created. Releasing the proxy now
     * also drops its listener, whose data lives on find_output()'s stack. */
    if (dpy->output) {
        wl_output_release(dpy->output);
        dpy->output = NULL;
    }

    zwlr_layer_surface_v1_set_anchor(dpy->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(dpy->layer_surface, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(dpy->layer_surface, -1);
    /* On-demand focus only exists since layer-shell v4; older compositors get the grab. */
    if (focus == KEYBOARD_ON_DEMAND && dpy->layer_shell_version < 4) focus = KEYBOARD_EXCLUSIVE;
    zwlr_layer_surface_v1_set_keyboard_interactivity(dpy->layer_surface,
        focus == KEYBOARD_EXCLUSIVE ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
        : focus == KEYBOARD_ON_DEMAND ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND
                                      : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    zwlr_layer_surface_v1_add_listener(dpy->layer_surface, &layer_surface_listener, dpy);

    wl_surface_commit(dpy->surface);

    dpy->running = true;
    while (dpy->running && !dpy->configured) {
        if (wl_display_dispatch(dpy->display) < 0) {
            display_destroy(dpy);
            return NULL;
        }
    }
    return dpy;
}

void display_destroy(Display *dpy) {
    if (!dpy) return;

    if (dpy->layer_surface) zwlr_layer_surface_v1_destroy(dpy->layer_surface);
    if (dpy->viewport) wp_viewport_destroy(dpy->viewport);
    if (dpy->surface) wl_surface_destroy(dpy->surface);
    if (dpy->background) wl_buffer_destroy(dpy->background);
    if (dpy->keyboard) wl_keyboard_release(dpy->keyboard);
    if (dpy->seat) wl_seat_release(dpy->seat);
    if (dpy->output) wl_output_release(dpy->output);
    if (dpy->layer_shell) zwlr_layer_shell_v1_destroy(dpy->layer_shell);
    if (dpy->single_pixel) wp_single_pixel_buffer_manager_v1_destroy(dpy->single_pixel);
    if (dpy->viewporter) wp_viewporter_destroy(dpy->viewporter);
    if (dpy->shm) wl_shm_destroy(dpy->shm);
    if (dpy->subcompositor) wl_subcompositor_destroy(dpy->subcompositor);
    if (dpy->compositor) wl_compositor_destroy(dpy->compositor);
    if (dpy->registry) wl_registry_destroy(dpy->registry);
    if (dpy->display) {
        wl_display_flush(dpy->display);
        wl_display_disconnect(dpy->display);
    }
    free(dpy);
}

int display_dispatch(Display *dpy, int timeout_ms, int extra_fd) {
    struct wl_display *d = dpy->display;

    while (wl_display_prepare_read(d) != 0) {
        if (wl_display_dispatch_pending(d) < 0) return -1;
    }

    if (wl_display_flush(d) < 0 && errno != EAGAIN) {
        wl_display_cancel_read(d);
        return -1;
    }

    struct pollfd fds[2] = {
        { .fd = wl_display_get_fd(d), .events = POLLIN },
        { .fd = extra_fd, .events = POLLIN },
    };
    int ret = poll(fds, extra_fd >= 0 ? 2 : 1, timeout_ms);
    if (ret < 0) {
        wl_display_cancel_read(d);
        return errno == EINTR ? 0 : -1;
    }

    if (fds[0].revents & POLLIN) {
        if (wl_display_read_events(d) < 0) return -1;
    } else {
        wl_display_cancel_read(d);
        if (fds[0].revents & (POLLERR | POLLHUP)) return -1;
    }

    if (wl_display_dispatch_pending(d) < 0) return -1;
    return extra_fd >= 0 && (fds[1].revents & (POLLIN | POLLHUP)) ? 1 : 0;
}

static void buffer_release(void *data, struct wl_buffer *buffer) {
    (void)buffer;
    ((Panel *)data)->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

static void panel_free_buffer(Panel *panel) {
    if (panel->image) cairo_surface_destroy(panel->image);
    if (panel->buffer) wl_buffer_destroy(panel->buffer);
    if (panel->data) munmap(panel->data, panel->size);
    panel->image = NULL;
    panel->buffer = NULL;
    panel->data = NULL;
    panel->width = panel->height = 0;
}

static bool panel_alloc_buffer(Panel *panel, int width, int height) {
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    size_t size = (size_t)stride * height;

    int fd = memfd_create("pacpeek", MFD_CLOEXEC);
    if (fd < 0) return false;
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return false;
    }

    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return false;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(panel->dpy->shm, fd, size);
    panel->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
                                              WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    wl_buffer_add_listener(panel->buffer, &buffer_listener, panel);
    panel->data = data;
    panel->size = size;
    panel->width = width;
    panel->height = height;
    panel->image = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32,
                                                       width, height, stride);
    return true;
}

void panel_init(Panel *panel, Display *dpy) {
    memset(panel, 0, sizeof(*panel));
    panel->dpy = dpy;
    panel->surface = wl_compositor_create_surface(dpy->compositor);
    panel->subsurface = wl_subcompositor_get_subsurface(dpy->subcompositor,
                                                        panel->surface, dpy->surface);
    wl_subsurface_set_desync(panel->subsurface);

    struct wl_region *empty = wl_compositor_create_region(dpy->compositor);
    wl_surface_set_input_region(panel->surface, empty);
    wl_region_destroy(empty);
}

void panel_finish(Panel *panel) {
    panel_free_buffer(panel);
    if (panel->subsurface) wl_subsurface_destroy(panel->subsurface);
    if (panel->surface) wl_surface_destroy(panel->surface);
    memset(panel, 0, sizeof(*panel));
}

cairo_t *panel_begin(Panel *panel, int width, int height) {
    if (panel->busy || width <= 0 || height <= 0) return NULL;

    if (width != panel->width || height != panel->height) {
        panel_free_buffer(panel);
        if (!panel_alloc_buffer(panel, width, height)) return NULL;
    }

    cairo_t *cr = cairo_create(panel->image);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    return cr;
}

void panel_end(Panel *panel, cairo_t *cr, int x, int y) {
    cairo_destroy(cr);
    cairo_surface_flush(panel->image);

    wl_surface_attach(panel->surface, panel->buffer, 0, 0);
    wl_surface_damage_buffer(panel->surface, 0, 0, panel->width, panel->height);
    wl_surface_commit(panel->surface);
    panel->busy = true;

    if (x != panel->x || y != panel->y) {
        panel->x = x;
        panel->y = y;
        wl_subsurface_set_position(panel->subsurface, x, y);
        wl_surface_commit(panel->dpy->surface);
    }
}
