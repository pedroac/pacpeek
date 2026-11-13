# Configuration

Everything works without configuration. To change the defaults, pacpeek reads an optional config file, then environment variables, then [command-line options](usage.md); each overrides the one before.

## Config file

`$XDG_CONFIG_HOME/pacpeek/pacpeek.conf` (usually `~/.config/pacpeek/pacpeek.conf`), or else the older `~/.local/pac/pacpeek.conf`, one `key = value` per line. `#` starts a comment; blank lines are ignored. Bad lines are reported on stderr and skipped.

```ini
# ~/.config/pacpeek/pacpeek.conf
mode = toggle
timeout = 10
monitor = DP-1
late = 23:30-06:00
keyboard = exclusive
font = /usr/share/fonts/TTF/MyFont.ttf
font_light = /usr/share/fonts/TTF/MyFont-Light.ttf
text_scale = 1.25
time_format = %-I:%M %p
```

| Key | Same as |
|---|---|
| `mode`, `timeout`, `monitor` | `--mode`, `--timeout`, `--monitor` |
| `late` | `PACPEEK_LATE` |
| `keyboard` | `PACPEEK_KEYBOARD` |
| `font`, `font_light` | `PACPEEK_FONT`, `PACPEEK_FONT_LIGHT` |
| `text_scale` | `PACPEEK_TEXT_SCALE` |
| `time_format`, `date_format` | `PACPEEK_TIME_FORMAT`, `PACPEEK_DATE_FORMAT` |

## Environment variables


| Variable | Effect |
|---|---|
| `PACPEEK_LATE` | Late-night window for the clock tint: `HH:MM-HH:MM`, or `off`. Default `21:00-06:00`. |
| `PACPEEK_KEYBOARD` | `exclusive` grabs the keyboard instead of asking for focus on demand. |
| `PACPEEK_FONT` | Font file for the date and info text. |
| `PACPEEK_FONT_LIGHT` | Font file for the clock (falls back to `PACPEEK_FONT`). |
| `PACPEEK_TEXT_SCALE` | Makes all text larger or smaller, from `0.5` to `2.5`. Default `1`. |
| `PACPEEK_TIME_FORMAT` | [`strftime`](https://man7.org/linux/man-pages/man3/strftime.3.html) format for the clock, e.g. `%-I:%M %p` for 12-hour time. Default: from the language (`%H:%M`). |
| `PACPEEK_DATE_FORMAT` | `strftime` format for the date, e.g. `%a %-d %b`. Default: from the language. |
| `LANG`, `LC_TIME`, `LC_MESSAGES`, `LANGUAGE` | Language, date format and text direction. See [translations](translations.md). |

## Late-night clock

During the hours when you should be asleep the clock turns soft yellow, like anything else that needs attention. The window is `21:00-06:00` by default. Change it with `PACPEEK_LATE`, as `HH:MM-HH:MM` (it may wrap past midnight), or turn it off:

```bash
PACPEEK_LATE=23:30-06:00 pacpeek
PACPEEK_LATE=off pacpeek
```

An invalid value falls back to the default. The tint updates as the minute changes.

## Updates

The clock updates every minute. The status (RAM, CPU, temperature, battery, network, volume and notifications) is read when the overlay opens and again every 5 seconds while it stays open. The first frame waits briefly (up to 0.3 s) for the volume and notification query, so nothing shifts when it arrives.

## Colors

The info turns soft yellow when it needs attention and soft red when critical:

| Item | Yellow | Red |
|---|---|---|
| RAM | ≥ 80% | ≥ 90% |
| CPU | ≥ 85% | ≥ 95% |
| Temperature | ≥ 80 °C | ≥ 90 °C |
| Battery (not charging) | ≤ 20% | ≤ 10% |

Anything yellow or red also gets a small warning triangle (outlined for yellow, solid for red), so the state does not depend on colour alone.

Offline, one bar of Wi-Fi, and muted or 0% output volume are yellow. A muted mic stays neutral, since that's usually deliberate. The bell is yellow only when there are notifications.

## Network

The line shows the connection the default route uses: Wi-Fi (with the network name and signal strength), Ethernet, or another interface. When a VPN carries the traffic, the line shows the Wi-Fi or Ethernet link underneath and adds `VPN`.

## Fonts

pacpeek loads font files directly, without fontconfig, to keep memory low. By default it searches the system font directories (`$XDG_DATA_HOME/fonts`, `~/.fonts` and the `fonts` directory of every `$XDG_DATA_DIRS` entry, so `/usr/share/fonts` and the like) for a file named `NotoSans-Regular.ttf`, then `DejaVuSans.ttf`, then `LiberationSans-Regular.ttf`. To use other fonts, set `PACPEEK_FONT` and `PACPEEK_FONT_LIGHT`. Which font is used for each language is described in [translations](translations.md#fonts).
