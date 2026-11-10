# Configuration

pacpeek has no config file. Behaviour is set with [command-line options](usage.md) and these environment variables.

| Variable | Effect |
|---|---|
| `PACPEEK_LATE` | Late-night window for the clock tint: `HH:MM-HH:MM`, or `off`. Default `21:00-06:00`. |
| `PACPEEK_KEYBOARD` | `exclusive` grabs the keyboard instead of asking for focus on demand. |
| `PACPEEK_FONT` | Font file for the date and info text. |
| `PACPEEK_FONT_LIGHT` | Font file for the clock (falls back to `PACPEEK_FONT`). |
| `LANG`, `LC_TIME`, `LC_MESSAGES`, `LANGUAGE` | Language, date format and text direction. See [translations](translations.md). |

## Late-night clock

During the hours when you should be asleep the clock turns soft yellow, like anything else that needs attention. The window is `21:00-06:00` by default. Change it with `PACPEEK_LATE`, as `HH:MM-HH:MM` (it may wrap past midnight), or turn it off:

```bash
PACPEEK_LATE=23:30-06:00 pacpeek
PACPEEK_LATE=off pacpeek
```

An invalid value falls back to the default. The tint updates as the minute changes.

## Colors

The info turns soft yellow when it needs attention and soft red when critical:

| Item | Yellow | Red |
|---|---|---|
| RAM | ≥ 80% | ≥ 90% |
| CPU | ≥ 85% | ≥ 95% |
| Temperature | ≥ 80 °C | ≥ 90 °C |
| Battery (not charging) | ≤ 20% | ≤ 10% |

Offline, one bar of Wi-Fi, and muted or 0% output volume are yellow. A muted mic stays neutral, since that's usually deliberate. The bell is yellow only when there are notifications.

## Fonts

pacpeek loads font files directly, without fontconfig, to keep memory low. By default it looks for Noto Sans, then DejaVu Sans, then Liberation Sans. To use other fonts, set `PACPEEK_FONT` and `PACPEEK_FONT_LIGHT`. Which font is used for each language is described in [translations](translations.md#fonts).
