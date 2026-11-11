# Usage

```
pacpeek [--mode hold|toggle|external] [--timeout SECONDS] [--monitor NAME] [--toggle] [--help]
```

## Options

- `--mode hold` (default): Closes on the next key release. Launched from a held shortcut, letting go closes it (a peek). If the keys were already released, it stays open until you press and release any key.
- `--mode toggle`: Stays open until the Esc key is pressed (matched by a physical key, so a key remapped to Escape does not count).
- `--mode external`: Never takes keyboard focus, so the window you were using keeps it. Nothing inside pacpeek can close it, meaning the compositor must handle it: see [Release-to-close](#release-to-close-external-mode).
- `--timeout SECONDS`: Closes automatically after the specified duration in any mode.
- `--monitor NAME`: Displays on a specific monitor (e.g., `DP-1`, see `hyprctl monitors`). An unknown name falls back to the compositor's choice.
- `--toggle` (`-T`): If another instance is already running, sends it `SIGTERM` and exits; otherwise opens normally. Combine with any `--mode` to create a single keybind that both opens and closes the overlay.

`SIGTERM` and `SIGINT` also close the application. Only one instance runs at a time.

## Keyboard focus

The overlay requests keyboard focus on demand rather than grabbing the keyboard exclusively, allowing you to switch to another window. It closes as soon as it loses focus because it can no longer monitor key releases or the Esc key. On compositors that do not focus on-demand overlays automatically, set `PACPEEK_KEYBOARD=exclusive`.

## Hyprland binds

Hold the shortcut to peek, or use `--toggle` for a bind that both opens and closes:

```lua
hl.bind("SUPER + Control_L", hl.dsp.exec_cmd("pacpeek --toggle --mode hold"))
hl.bind("SUPER + SHIFT + Control_L", hl.dsp.exec_cmd("pacpeek --toggle --mode toggle"))
```

## Release-to-close (external mode)

In `external` mode a Hyprland key-release bind closes the overlay:

```lua
hl.bind("SUPER + Control_L", hl.dsp.exec_cmd("pacpeek --mode external --timeout 10"))
hl.bind("SUPER + Control_L", hl.dsp.exec_cmd("pkill -x pacpeek"), { release = true })
```

Advantage: This guarantees the layer is not focused and does not interfere with the windows behind it.

Notes:

- Hyprland matches a release bind against the modifiers still held at that moment, so letting go of `SUPER` before `Z` may not fire it. If that happens, also bind the release of `SUPER_L` (or use `ignore_mods = true` together with `non_consuming = true`).
- Keep a `--timeout` as a safety net: a very short tap can release the key before pacpeek has appeared, and an overlay with no keyboard focus can't be closed by Esc. Pressing and releasing the shortcut again also closes it, because a second launch exits and the release bind then runs `pkill`.

## Known limits

See [platform.md](platform.md#known-limits).
