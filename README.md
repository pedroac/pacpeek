# pacpeek
A quick hotkey-powered panel to peek at system essentials on demand, without needing a permanent status bar.

`pacpeek` was built for tiling window managers like Hyprland to optimize how system status is delivered:

* **No configuration overhead:** Skip the upfront setup and complex styling layers (CSS, JavaScript, heavy configs). It works cleanly and readably right out of the box.
* **Reliability & clean presentation:** Built to offer a polished layout that doesn't suffer from stability issues or layout clutter.
* **Resource efficiency:** Designed specifically for low-RAM environments, eliminating background overhead for stats you only need to check occasionally.
* **Maximized screen space:** Since most tasks and window operations are handled naturally via the keyboard, status info only needs to be a quick "peek" rather than a permanent bar taking up display space.

If you already rely on keybindings, system status works best on demand.

`pacpeek` pops up for an instant look, disappears when you release the hotkey, and leaves anything that needs your attention to your desktop notifications.

## Quick start

```bash
make && sudo make install
```

Bind it in Hyprland (`hyprland.lua`), then hold the shortcut to peek and let go to close:

```lua
hl.bind("SUPER + Z", hl.dsp.exec_cmd("pacpeek"))
```

## Usage

```
pacpeek [--mode hold|toggle|external] [--timeout SECONDS] [--monitor NAME]
```

- `hold` (default): closes when a key is released. `toggle`: closes on Esc. `external`: takes no keyboard focus; the compositor closes it.
- `--timeout` closes it automatically; `--monitor` picks the screen.

## Platform

Linux only, tested on **Arch Linux with Hyprland**. It will probably not work elsewhere as it is: fork it and adapt it ([details](docs/platform.md)).

## Documentation

- [Usage](docs/usage.md): modes, options, keyboard focus, Hyprland binds, release-to-close
- [Configuration](docs/configuration.md): environment variables, late-night clock, colors, fonts
- [Translations](docs/translations.md): languages, fonts per language, adding a language
- [Building and installing](docs/building.md): dependencies, install options, minimal build
- [Platform support and known limits](docs/platform.md)
- [Development](docs/development.md): source layout

## AI Assistance Disclosure

AI tools were used during the development of pacpeek for:

- Architecture & Ideation: Discussing design trade-offs (e.g., C-first philosophy, avoiding heavy dependencies, and leveraging wlr-layer-shell).
- Quality & Security Auditing: Reviewing code for correctness, safety, and reliability.
- Code Refinement: Structuring low-level Wayland and Cairo calls, and handling focus/input behavior.
- Documentation & Localization: Drafting documentation and initial translation catalogs (po/).

All code was reviewed, tested, and curated by the human author to ensure performance, security, and minimal overhead.

## License

MIT, see [LICENSE](LICENSE). The bundled wlroots protocol file keeps its own notice.
