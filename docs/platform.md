# Platform support

pacpeek is Linux only, and it has only been tested on **Arch Linux with Hyprland** (0.56, with the Lua config), PipeWire/WirePlumber and mako. It will probably not work elsewhere as it is; anyone who needs another system or compositor should fork it and adapt it. These are the places that assume Linux or that setup:

- **Compositor:** it needs the `wlr-layer-shell`, `wp_viewporter` and `wp_single_pixel_buffer_v1` protocols (wlroots-based compositors and Hyprland). Compositors without `wlr-layer-shell`, such as GNOME's Mutter, can't run it. Keyboard handling and the Esc/key-release behaviour are only verified on Hyprland (`display.c`, `input.c`).
- **System stats:** RAM, CPU, temperature, battery and network state come straight from `/proc` and `/sys` (`system.c`, `network.c`).
- **Wi-Fi name:** read through the Linux `nl80211` generic netlink interface (`network.c`).
- **Linux APIs:** `memfd_create` for shared memory (`display.c`) and Linux evdev key codes (`input.c`, `main.c`).
- **Distribution layout:** font files are looked up under `/usr/share/fonts` (`render.c`), and the lock file lives in `$XDG_RUNTIME_DIR` or `/run/user/UID` (`main.c`).
- **External tools:** `wpctl` for volumes and `pachud count`, `makoctl` or `dunstctl` for notifications, run through `/bin/sh` (`query.c`).
- **Build:** GNU make, a C compiler, `pkg-config` and `wayland-scanner`. The translated date formats use the GNU `strftime` `%-d` extension.

## Known limits

- **No keyboard focus, no close key.** In `hold` and `toggle` mode, if the compositor never gives the overlay keyboard focus (Hyprland does), nothing inside pacpeek can close it. If you can't close it, use `--timeout`, a signal (`pkill -x pacpeek`), `PACPEEK_KEYBOARD=exclusive`, or `external` mode with a release bind.
- **One buffer per text panel.** pacpeek is only verified on Hyprland, which releases shared-memory buffers right after drawing. A compositor that keeps the buffer until it is replaced would stop the clock and status from updating after the first frame, because a panel is never drawn into while the compositor still holds its buffer.
