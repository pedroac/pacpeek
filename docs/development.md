# Development

## Source layout

- `main.c`: options, lifecycle and event loop (redraws once per minute)
- `display.c/h`: Wayland connection, layer-shell surface with a single-pixel background, and text panels (subsurfaces)
- `render.c/h`: Cairo + FreeType drawing, per-language fonts, right-to-left layout
- `shape.c/h`: HarfBuzz + FriBidi shaping for Arabic and other complex scripts (not built with `I18N=0`)
- `utf8.c/h`: the one strict UTF-8 decoder
- `i18n.h`: gettext macros (plain strings with `I18N=0`)
- `input.c/h`: `wl_keyboard` events by evdev key code (no keymap)
- `system.c/h`: time, RAM, CPU usage and temperature, battery (system batteries only)
- `query.c/h`: background query for volumes (`wpctl`) and notification count (`makoctl`/`dunstctl`)
- `network.c/h`: connection status from `/proc` and `/sys`, Wi-Fi SSID via nl80211 netlink (no D-Bus)

`protocol/` holds the `wlr-layer-shell` description (not part of `wayland-protocols`); the other protocol bindings are generated at build time. `po/` holds the translation catalogs.

## Build output

Everything generated goes to `build/` (ignored by git): protocol bindings, objects, `config.h` (records `LOCALEDIR` and `I18N`, so changing them rebuilds what depends on them) and the compiled catalogs.
