# Building and installing

## Dependencies

`wayland-client`, `wayland-protocols`, `cairo`, `freetype2`, and `harfbuzz` and `fribidi` (not needed with `I18N=0`, see [below](#minimal-build)). Build tools: GNU make, a C compiler, `pkg-config` and `wayland-scanner`.

The compositor must support `wlr-layer-shell`, `wp_viewporter` and `wp_single_pixel_buffer_v1`.

The translation catalogs are built with `msgfmt` from gettext. Without it the build still succeeds, just without translations (or use `I18N=0`). `xgettext` and `msgmerge` are only needed to refresh the catalogs.

## Build and install

```bash
make
sudo make install            # installs to /usr/local/bin/pacpeek
sudo make uninstall
```

`PREFIX` (default `/usr/local`), `BINDIR`, `LOCALEDIR` and `DESTDIR` are honored, e.g. `make install PREFIX=$HOME/.local`. `CFLAGS`, `LDFLAGS` and `LDLIBS` can be overridden too; the hardening flags (PIE, full RELRO, stack protector, fortified calls, and control-flow protection when the compiler supports it) are always added.

## Minimal build

```bash
make clean && make I18N=0
```

An English-only binary: no gettext, HarfBuzz or FriBidi, no per-language fonts, no catalogs to install, and the system locale is ignored. It saves roughly 0.1 MB of memory and two dependencies. Switch back with `make clean && make`.

## Runtime tools

Volumes come from `wpctl` (PipeWire/WirePlumber) and the notification count from `makoctl` or `dunstctl`. They run in the background so the overlay isn't delayed, and whatever is unavailable is left out. CPU usage appears about 0.5 s after opening, since it needs two samples.
