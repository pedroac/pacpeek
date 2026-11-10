#ifndef I18N_H
#define I18N_H

#include "config.h"

#ifdef NO_I18N
#define _(s) (s)
#define N_(s) (s)
#define ngettext(one, many, n) ((n) == 1 ? (one) : (many))
#else
#include <libintl.h>
#define _(s) gettext(s)
#define N_(s) (s)
#endif

#endif
