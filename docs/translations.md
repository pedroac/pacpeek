# Translations

pacpeek follows `LANG`/`LC_TIME`/`LC_MESSAGES` (and `LANGUAGE`, which also selects the font and text direction). Day and month names come from the system locale (`strftime`), and the date layout and labels come from gettext catalogs in `po/`.

Included: Arabic, Chinese (Simplified), Dutch, French, German, Italian, Japanese, Korean, Polish, Portuguese (`pt_PT`, `pt_BR`), Russian, Spanish and Turkish. These were machine-written and should be reviewed by native speakers.

## Fonts

Fonts are chosen by language: Noto Sans (or DejaVu/Liberation) by default, DejaVu Sans for right-to-left languages, and Noto Sans CJK for Japanese, Chinese and Korean (install `noto-fonts-cjk`). Arabic and other complex scripts are shaped with HarfBuzz and FriBidi, only when such text is shown, so they cost nothing otherwise.

## Adding a language

```bash
make pot                                    # refresh po/pacpeek.pot
msginit -i po/pacpeek.pot -o po/de.po -l de_DE.UTF-8
# translate po/de.po, then:
make && sudo make install
```

`make update-po` merges new strings into existing catalogs. The date format is a translatable `strftime` string, so each language can reorder it (e.g. `%A, %-d de %B de %Y`).

## Without translations

`make I18N=0` builds an English-only binary: see [building](building.md#minimal-build).
