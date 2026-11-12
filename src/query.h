#ifndef QUERY_H
#define QUERY_H

#include <stdbool.h>

typedef struct {
    bool valid;
    int percent;
    bool muted;
} Volume;

typedef struct {
    Volume output;
    Volume input;
    int notifications; /* -1 if no supported notification daemon answered */
    bool loaded;       /* the first query has finished */
} QueryState;

/* Runs wpctl/makoctl/dunstctl in the background; returns a pollable fd, or -1. */
int query_start(void);
/* Call when the fd is readable; returns true once the query has finished. */
bool query_read(QueryState *state);
void query_cancel(void);

#endif
