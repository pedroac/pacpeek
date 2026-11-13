# Development

## Source layout

- `main.c`: options, lifecycle and event loop (redraws when the clock or status changes)
- `display.c/h`: Wayland connection, layer-shell surface with a single-pixel background, and text panels (subsurfaces)
- `render.c/h`: Cairo + FreeType drawing, per-language fonts, right-to-left layout
- `fontfind.c/h`: finds font files by name in the system font directories
- `shape.c/h`: HarfBuzz + FriBidi shaping for Arabic and other complex scripts (not built with `I18N=0`)
- `utf8.c/h`: the one strict UTF-8 decoder
- `i18n.h`: gettext macros (plain strings with `I18N=0`)
- `hold.c/h`: which keys are held, so `hold` mode knows when all are released
- `input.c/h`: `wl_keyboard` events by evdev key code (no keymap)
- `sysfs.c/h`: reading single-line `/proc` and `/sys` files, shared by `system.c` and `network.c`
- `system.c/h`: time, RAM, CPU usage and temperature, battery (system batteries only)
- `query.c/h`: background query for volumes (`wpctl`) and notification count (`pacreact`/`makoctl`/`dunstctl`)
- `network.c/h`: connection status from `/proc` and `/sys`, Wi-Fi SSID via nl80211 netlink (no D-Bus)

Listeners in `display.c` and `input.c` have empty functions for events pacpeek ignores: libwayland aborts if an event reaches a NULL listener entry.

`protocol/` holds the `wlr-layer-shell` description (not part of `wayland-protocols`); the other protocol bindings are generated at build time. `po/` holds the translation catalogs.

## Tests

`make test` builds and runs the unit tests in `tests/test.c`: UTF-8 decoding, font lookup, the late-night window, CPU percentage, route parsing and choice, SSID sanitizing, hold-mode key tracking and config parsing. They need no Wayland session. The test includes `network.c` directly to reach its static functions.

## Build output

Everything generated goes to `build/` (ignored by git): protocol bindings, objects, `config.h` (records `LOCALEDIR` and `I18N`, so changing them rebuilds what depends on them) and the compiled catalogs.
