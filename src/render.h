#ifndef RENDER_H
#define RENDER_H

#include "network.h"
#include "query.h"
#include "system.h"
#include <cairo/cairo.h>
#include <stdbool.h>

typedef struct {
    struct tm time;
    bool late;            /* the user should be asleep: tint the clock */
    int ram_percent;      /* -1 if unknown */
    int cpu_percent;      /* -1 if unknown */
    int cpu_temp;         /* -1 if unknown */
    BatteryInfo battery;
    QueryState query;
    NetworkStatus network;
} SystemState;

bool render_init(int screen_width, int screen_height);
void render_update(const SystemState *state);
void render_finish(void);

void render_clock_size(int *width, int *height);
void render_clock(cairo_t *cr);

void render_info_size(int *width, int *height);
void render_info(cairo_t *cr);

/* Vertical space between the date and the info block. */
int render_group_gap(void);

#endif
