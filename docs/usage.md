# Usage

```
pacpeek [--mode hold|toggle|external] [--timeout SECONDS] [--monitor NAME] [--help]
```

## Options

- `--mode hold` (default): closes on the next key release. Launched from a held shortcut, letting go closes it (a peek). If the keys were already released, it stays open until you press and release any key.
- `--mode toggle`: stays open until the Esc key is pressed (matched by physical key, so a key remapped to Escape doesn't count).
- `--mode external`: never takes keyboard focus (`KEYBOARD_INTERACTIVITY_NONE`), so the window you were using keeps it. Nothing inside pacpeek can close it any more, so the compositor must: see [Release-to-close](#release-to-close-external-mode).
- `--timeout SECONDS`: closes automatically after that long, in any mode.
- `--monitor NAME`: shows on a specific monitor (e.g. `DP-1`, see `hyprctl monitors`). An unknown name falls back to the compositor's choice.

`SIGTERM`/`SIGINT` also close it, and only one instance runs at a time (the lock is `pacpeek.lock` in `$XDG_RUNTIME_DIR`, or `/run/user/UID`).

## Keyboard focus

The overlay asks for keyboard focus on demand rather than grabbing the keyboard exclusively, so you can still move to another window. It closes as soon as it loses focus, since it could no longer see the key release or Esc. On a compositor that doesn't focus on-demand overlays by itself, set `PACPEEK_KEYBOARD=exclusive`.

## Hyprland binds

Hold the shortcut to peek, or use toggle mode (press again, or Esc, to close):

```lua
hl.bind("SUPER + Z", hl.dsp.exec_cmd("pacpeek"))
hl.bind("SUPER + SHIFT + Z", hl.dsp.exec_cmd("pkill -x pacpeek || pacpeek --mode toggle"))
```

A bind made only of modifiers, such as `"SUPER + CTRL"`, does nothing: the bind needs a real key at the end.

## Release-to-close (external mode)

In `external` mode a Hyprland key-release bind closes the overlay:

```lua
hl.bind("SUPER + Z", hl.dsp.exec_cmd("pacpeek --mode external --timeout 10"))
hl.bind("SUPER + Z", hl.dsp.exec_cmd("pkill -x pacpeek"), { release = true })
```

Notes:

- Hyprland matches a release bind against the modifiers still held at that moment, so letting go of `SUPER` before `Z` may not fire it. If that happens, also bind the release of `SUPER_L` (or use `ignore_mods = true` together with `non_consuming = true`).
- Keep a `--timeout` as a safety net: a very short tap can release the key before pacpeek has appeared, and an overlay with no keyboard focus can't be closed by Esc. Pressing and releasing the shortcut again also closes it, because a second launch exits and the release bind then runs `pkill`.

## Known limits

See [platform.md](platform.md#known-limits).
