# Translating Hyperion

Hyperion shows two kinds of text. The game's own (units, buttons, the console, the classic
menus and briefings) comes from the player's StarCraft: Remastered install, in the language it
was installed in, and is Blizzard's translation; nothing here changes it. Hyperion's own text
(its menus, Settings, the HUD's panels, the LAN room) is translated here, in
`engine/locale/`.

## The files

- `engine/locale/hyperion.pot` lists every text to translate, with where the code uses it.
  It is written by `python engine/tools/locale_catalog.py extract`; do not edit it by hand.
- `engine/locale/<locale>.po` holds one language's translations, in gettext's format, which
  Poedit, Lokalize, Weblate and any text editor read. The locales are Remastered's: `deDE`,
  `esES`, `esMX`, `frFR`, `itIT`, `jaJP`, `koKR`, `plPL`, `ptBR`, `ruRU`, `zhCN`, `zhTW`.

The first translations were drafted for Hyperion, not by native speakers. Corrections are
welcome, as a pull request that changes the `.po` file or as an issue that quotes the line.

## An entry

```
#: src117/port/front/port_front_settings.cpp:929
msgid "Language"
msgstr "Sprache"
```

- `msgid` is the English, which is the key; leave it as it is.
- An empty `msgstr` shows the English.
- `{0}`, `{1}` are filled in by the code (a name, a number). Keep every one, in the order the
  language needs: `"{0} of {1}"` may become `"{1} 中的 {0}"`.
- A text with `|` is a list of choices (`"Classic|Exact"`). Keep the same number of `|`.
- `msgctxt` tells two uses of the same English apart.
- An entry with `msgid_plural` has one `msgstr[n]` for each of the language's plural forms,
  as the file's `Plural-Forms` line counts them: two for German, three for Polish and
  Russian, one for Japanese, Korean and Chinese.
- The game's own words (Minerals, Vespene Gas, the races and units) are taken from the
  language's StarCraft: Remastered text, so that the menus and the game agree.

## Trying a translation

Build Hyperion (`python engine/tools/build_engine_x64.py`, `engine/README.md`): each build
compiles the `.po` files in. In Settings, Interface, set Language and start Hyperion again. A
text with a broken slot or choice list is left out of the build and shows the
English; `python engine/tools/locale_catalog.py check` names each one, and
`python engine/tools/locale_catalog.py report` counts what each language lacks.

## In the code

Hyperion's text goes through `PortText` (`engine/src117/port/front/port_text.h`):

- `PortText("Back")` for a text on its own;
- `PortTextF("{0} of {1}", {a, b})` for a text with values in it, never English joined with `+`;
- `PortTextN("{0} unit", "{0} units", n)` (or `PortTextNF` to fill it) for a count;
- `PortTextC("verb", "Close")` when the same English means two things;
- `PT_("...")` marks a table's text, which the code passes through `PortText` where it is
  shown.

Run `locale_catalog.py extract` after adding text, and commit `hyperion.pot` and the `.po`
files with the code.
