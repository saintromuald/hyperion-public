# HUD files, version 3

The reference for a HUD file: what each field means, its default, and the maths the game has
to match. Hyperion plays the file in its GPU presenter, and `hudplayer.js`, the format's own
player, plays the same file in a browser. The samples are in `huds/`.

Version 3 extends the earlier layout file (version 2). A version 2 file still loads: see the
end.

This file is Hyperion's copy of the format's reference, which is kept alongside
`hudplayer.js` and stays the source. The sections and paragraphs marked
"Hyperion's addition" are the engine's own, read by `src117/port/hud/` and not yet by
`hudplayer.js`: Parts, Buttons, the animated images under Assets, what the engine reads of a
package, the engine's checker and live reload, and the notes on the fonts, the tooltips and the
`time` slot. `modules/hud.md` is the engine side of it, which file does what.

**2026-10-02** added, without a version change, since every addition is optional and an
earlier file means the same: the `shape` layer (panels drawn by a shader instead of from
bitmaps, with frosted glass over the map), text outlines and soft shadows, the `spring`
easing, a per-track `delay`, sound `cues` and `audio` assets, and the rule for how the game's
slot content fits a slot of another size. `hudshape.js` is the shader, and
`SCHudShape.hlsl()` writes it out as the presenter's HLSL.

**2026-10-03** split a HUD into two files, again without a version change: a
**layout** (panels, anchors, slots, timelines; this format) and a **style** (materials,
colours, shape language, effects, easings and sounds; see Styles). Any layout that names looks
can wear any style, and the game picks the style by the race in play, which the player can
override. It added a material library (21 named presets drawn by the shader), surfaces lit as
3D, the shape fields that give each race its silhouette (wobble, spines, arcs, segments, seams,
bolts, inlay, smooth unions, mirroring) and a `reveal` that peels or warps a shape open. A
file written before it still means the same: with no `surface` and none of the new fields, a
shape draws exactly as before. `huds/modern-drawn.json` is the drawn Modern layout;
`huds/styles/` holds four styles for it, `terran-glass` (the first glass look),
`terran-industrial`, `zerg-organic` and `protoss-khala`. `style.html` is the style editor, with
sweeps.

**2026-10-03** added, again optional, what a HUD built from a kit of painted parts
needs: a `run` layer that tiles a part between two ends, a panel stretched `between` two others,
a lit image surface that relights a part's own art from its normal and lights maps (`normal` and
`fx` on an asset), and `regions` that change the light, depth, material or effects in part of a
lit layer (Assets, Panels, Layers). These fields are written up from `hudplayer.js` and
`hudshape.js`, which Hyperion matches. The SC2-style console is written up in
`sc2-console.md`; the fields the generated kits draw with are in Shapes (Paths, profiles,
patterns and relief) and in Layers (Tiling).

**2026-10-05** added, again optional, what Hyperion's selection and groups need: the
selection slot's `grid` (SC2's wireframe grid in pages, its columns growing with the slot),
`places` on the groups slot for the ten groups, the `army` slot, a timeline's `group`, the
events `selection.page`, `selection.subgroup`, `group.recall` and `group.set`, and the names
for pages, subgroups and group counts (see Slots, Bindings and Events). `huds/terran-sc2wide.json`
is the sample: the generated Terran kit as one console along the whole bottom.

**2026-10-05** added two optional things, both for the user's own image of the
console, built piece by piece as `huds/terran-r8.json` (from `huds/kits/build-r8.mjs`, in the
style `huds/styles/terran-r8.json`): a layer's `inset` may leave its left (or top) as `null`
to pin a layer of fixed width (or height) to its panel's right (or bottom) edge, which is how a
stretching panel keeps a lamp tab at its right end as drawn; and `places` on the `resources`
slot, which gives minerals, gas and supply a cell each (see Slots). Nothing else changed.

**2026-10-09** added, again optional, what a HUD needs to be played in multiplayer:
1.17's four buttons as slots (`menu`, `diplomacy`, `messaging`, `terrain`), which the game
draws, handles, and shows or hides by 1.17's own rules; the bindings they follow
(`game.multiplayer`, `game.observer`, `game.replay`, `minimap.terrain`) and `army.count`; a
button slot's `align`; a press timeline's `slot`; a file's `classic`; and two checks, one
warning that a file without the menu, Diplomacy or Messaging is not fit for multiplayer, the
other holding a classic file to 1.17's slots. Every slot is now marked 1.17's or Hyperion's (see
Slots). The principle, the user's: the features belong to the engine, which offers every slot,
and a HUD file shows the ones it wants, so a HUD with only 1.17's slots stays possible.
`huds/terran-r9.json` (from `huds/kits/build-r9.mjs`, on the R8 pieces, in the `terran-r8`
style) is the sample, and `huds/remastered-terran.json`, the classic route, now has the buttons
where 1.17 has them.

**2026-10-09** made a HUD one file to share: the `.hud` package, a zip holding the
layout, the styles it wears and the author's own sounds and images, with a `package` object in
the layout, and the fonts a player without Remastered sees. Nothing in a layout or a style
changed meaning. See Packages; `hudpackage.js` reads, checks and writes them.

**2026-10-22** added, again optional, four surfaces and three fields any surface
takes (Surfaces), a style's `lights` (Styles), an asset's `pixelated`, and a file's `options`,
the choices it offers the player in Settings (Options).

**2026-10-23** added, again optional, a style's tooltip: a `tooltip` block and `tip`
looks with which a style draws the tooltip of the button under the pointer, in the console's own
materials and type, in place of 1.17's bubble (Tooltips). With it came a panel's `tip`, a text's
`weight`, the events `tip.show` and `tip.hide`, and three fonts named as Hyperion ships them,
`chakra-petch`, `saira` and `michroma`. A style without a tooltip leaves the game's own bubble.
The Mech and R9 layouts also lost their flash on a new selection: `select.change` plays nothing
in them now (the user took it out of Hyperion's copy, 2026-10-23). Where Hyperion's engine, as
built, differs from this page is listed near its end (Hyperion as built).

**2026-10-23** added, again optional, the band: a file's `band`, or an option value's,
lays the panels out across the middle of a wider screen, at most that many units wide, as
Remastered keeps its console to 1920 (The band). A panel's `outside` fills a margin past the band,
which is how a skin carries its console on to the screen's edges. The Mech layouts and R9 offer it
to the player as Width: Full screen or 16:9. And the game gained a setting of the player's that no
file holds, the map raise: the battlefield drawn up to 200 units higher, for a HUD that stands tall
at its sides (Raising the map).

**2026-10-23** added, again optional, a layout for replays: a file's `replay` and
`mode`, the replay's slots (`play`, `slower`, `faster`, `tv`, `follow`, `players`), their bindings
and events, and the players' board (Replays).

**2026-10-25** added, again optional, the players' stats a viewer calls up in a replay
or as an observer: the `stats` slot in two views, a popup (F1) and a side view (Shift+F1), with
its `view` and `backing`, the bindings `stats.popup` and `stats.side`, the events `stats.show`
and `stats.hide`, and a timeline's `view`. It also settled what the replay layout left open: a
board with more players than fit, the eye past the eighth player, the replay card's labels on a
narrow ladder, and the clock in a replay layout (The players' stats).

**2026-10-25** added, again optional, what a player needs to bring a HUD of their own
that does everything Hyperion's does: a layer's `action` (the names Hyperion's Buttons gave
them, then the replay's, the stats' and a player's vision), with `enabled` and `label`, and the
replay's position as a slider; the `each` layer, a row for every player or a cell for each kind
of unit in a player's army, drawn from templates that read that row's names (`p.*`, `t.*`,
`row.*`, `u.*`, `more.*`); every player's figures by seat (`players.N.*`) and the most of each
(`top.*`); the `icon` and `chart` layers; a layer's `ride`; a file's `start`; a style's `slots`;
the package's manifest; and the bindings `replay.live`, `replay.seeking`, `stats.page` and
`teams.count`. It also corrects the earlier reading of `game.observer`, which is not 1.17's
`IsTVBGame()` (A design of your own).

## Units and the screen

Every number is in a screen 1080 units high. A screen of W x H pixels is `LW = 1080 * W / H`
units wide (1440 at 4:3, 1920 at 16:9, 2560 at 21:9, 3840 at 32:9) and one unit is `H / 1080`
pixels. So a file is the same size at every resolution of a shape, and a wider shape only adds
room between the edges. Times are milliseconds. Angles are degrees, clockwise, 0 pointing right
(screen y points down). Colours are `#rgb`, `#rrggbb` or `#rrggbbaa`.

## The file

```json
{
  "version": 3,
  "screen": 1080,
  "name": "Modern Terran",
  "race": "terran",
  "note": "free text, shown by the mock and ignored by the game",
  "options": [ ... ],
  "assets": { ... },
  "panels": [ ... ],
  "timelines": [ ... ]
}
```

`version` must be 3 and `screen` 1080. `name` and `race` are for the player's list of HUDs;
`race` is the race the HUD was made for (a file per race, Terran first).

`classic` (optional, default `false`): `true` says the file is a 1.17 console, and
the checker then warns at every slot that is Hyperion's rather than 1.17's (Slots). It changes
nothing the game draws.

`band` (optional, default 0): the most width, in units, the panels are laid out across.
0 is the whole screen. 1920 keeps them to a 16:9 screen's width in the middle of a wider one (The
band). The Mech layouts and R9 leave it 0 and offer it as an option instead.

## Options

`options` (optional): the choices a HUD offers the player. The game lists them in
Settings under the HUD and keeps the player's picks with the profile, per HUD design. Each
value's `set` lays fields over the file's panels and layers before the style is worn, so an option
can move a panel, hide a layer, or change any other field a panel or layer has. The dark mech
layouts (`huds/mech-*.json`) offer where minerals, gas and supply hang and the plate behind them
(the user, 2026-10-22: "have in settings options to enable or disable the background you did for
them, and if it should be in same section where it is slight to the right in center, or if to be
aligned to the right side of the end"):

```json
"options": [
  {"id": "resources", "label": "Resources", "default": "centre", "help": "Right of centre is where 1.17 has them, ...",
   "values": [
     {"id": "centre", "label": "Right of centre", "set": {"resources": {"ax": "center", "x": 495, "y": 0}, "resources.resBodyEdge": {"visible": false}, ...}},
     {"id": "edge", "label": "Right edge", "set": {"resources": {"ax": "right", "x": 0, "y": 0}, "resources.resBody": {"visible": false}, ...}}]},
  {"id": "resourcesBacking", "label": "Resources backing", "default": "on", "help": "The plate behind minerals, gas and supply, ...",
   "values": [
     {"id": "on", "label": "Plate"},
     {"id": "off", "label": "None", "set": {"resources.resBody": {"visible": false}, "resources.res1": {"visible": false}, ...}}]}
]
```

| Field | Meaning | Default |
|---|---|---|
| `id` | The option's name; the player's pick is kept under it. | required |
| `label`, `help` | What the player reads: the option's name, and a line under it. | `label` required |
| `values` | Two or more, each with an `id`, a `label`, an optional `help` (a tooltip) and an optional `set`. | required |
| `default` | The value of a player who has not picked one. | the first |

A value may also carry `band`, a number in units: picked, it is the file's `band`
(The band), and the value's `set` is applied as well. Every Mech layout and R9 offer it first:

```json
{"id": "width", "label": "Width", "default": "full", "help": "16:9 keeps the console as wide as a 16:9 screen's, ...",
 "values": [
   {"id": "full", "label": "Full screen", "help": "The HUD runs the width of the screen."},
   {"id": "band", "label": "16:9", "band": 1920, "help": "The HUD keeps to the middle of a wider screen, as wide as a 16:9 screen's."}]}
```

`set` is `{ "<panel>" or "<panel>.<layer>": { field: value } }`. The game applies each option's
picked value in file order, so a later option wins a field two of them set (above, `None` hides
the plate whichever place is picked); a value with no `set` leaves the file as it is. A set never
changes `id`, `layers`, `slot`, `between`, `outside` or what a layer is (`image`, `rect`, `text`,
`shape`, `run`): the checker warns and they stay as the file has them. Since a set changes fields only,
the panels and layers keep their order, and an option changed in a game is the same file worn
again, so running timelines carry on. The file's own fields should be its defaults', so a player
that knows no options draws the default.

The checker: `options` is a list; each option has an `id` no other has and a `label`, and two or
more values with ids of their own and labels; `default` names one of them; a set naming a panel
or layer the file does not have is a warning; `ax` and `ay` in a set must be words they take; a
binding in a set is checked as any other. In the mock, `SCHudPlayer.choose(layout, picks)` is the
file with the picks laid over it (`picks` is `{ option: value }`), and a player's
`setOptions(picks)` wears it again.

## Assets

Every image is named once under `assets` and used by that name.

| Field | Meaning |
|---|---|
| `art` | The game's art: `hd:` for Remastered's, `sd:` for 1.17's, then the file's path in the install without the resolution folder or the extension. `#n` picks frame n of a multi-frame file. The game reads these from the player's install; a HUD never carries the game's art. |
| `file` | The HUD's own image, a path relative to the HUD file's folder (the samples' are in `huds/files/`; in a package, relative to its `files/`). A `data:` URL also works, so a pasted file can carry its images. |
| `crop` | `[x, y, w, h]` in the image's pixels: use that part of it. A layer can override it. |
| `slice` | `[left, top, right, bottom]` in the image's pixels: draw it nine-sliced. The corners keep their size, the edges stretch along their length, the middle stretches both ways. |
| `scale` | Units per image pixel for a nine-slice's corners and edges. Default 1. |
| `pixelated` | `true` draws the image with no smoothing, so 1.17's pixels stay square when scaled (the classic HUD). Default false. |
| `normal` | the image's normal map, a file like `file`, the same shape as the image: tangent space, its green pointing up. Read only by a `lit` layer (Layers, Lit images). Missing, the part is flat (`#8080ff`). |
| `fx` | the image's lights map, a file like `file`: red marks the lamps, green the glass. Read only by a `lit` layer. Missing, the part has neither. |
| `audio` | The HUD's own sound, a path relative to the HUD file's folder like `file`. Timelines play it through `cues`. An asset is exactly one of `art`, `file` and `audio`. |
| `placeholder` | `"none"` draws nothing when the art is missing. Otherwise a missing image draws a plain plate (`#232c36` to `#141a21` top to bottom, a 1.5-unit `#3b4755` edge) the size of its layer, as every page does without the art. |

The art names the samples use, and the files the mock maps them to through SCAssets:

| Name | Mock file under `_assets/` |
|---|---|
| `hd:game/consoles/<skin>/<part>` | `hd/ui/consoles/<skin>/<part>.png` (`console_left`, `console_center`, `console_right`, `console_TIME`, `console_APM`, ...) |
| `hd:game/consoles/<skin>/dlg#35` | frame 35 of the skin's dialog frame set |
| `hd:game/observer/<part>` | `hd/ui/consoles/observer/<part>.png` |
| `hd:game/icons#n` | the resource icons: 0 minerals, 1 to 3 gas (Zerg, Terran, Protoss), 4 to 6 supply. Each frame is 106 x 28 with the icon in its left 38. |
| `hd:unit/cmdicons/cmdicons#n` | command icon n |
| `hd:glue/<path>` | a glue (menu) image |
| `sd:game/tconsole`, `pconsole`, `zconsole` | 1.17's race consoles |
| `sd:unit/cmdbtns/cmdicons#n` | 1.17's command icon n |

StarCraft II's art never goes in a HUD.

### Animated images (Hyperion's addition)

An asset's `frames` makes a PNG a sheet of frames, and says how a video loops
(`port_gpu_hud.cpp`, `DrawImage`, `ImageFrame`).

```json
"assets": {"lamp": {"file": "lamp.png", "frames": {"cols": 8, "rows": 2, "count": 12, "fps": 15}},
           "fan": {"file": "fan.webm", "frames": {"loop": true}}}
```

| Field | Meaning | Default |
|---|---|---|
| `cols`, `rows` | The sheet's columns and rows. | 1, 1 |
| `count` | The frames to play, never more than `cols × rows`. | `cols × rows` |
| `fps` | Frames a second. | 12 |
| `loop` | `false` holds the last frame instead of starting over. | `true` |

The frame shown at the HUD's time `t`, in seconds from the design's load, is `floor(t × fps)`:
that modulo `count` while it loops, else held at `count - 1`. A sheet of one frame is a plain
picture. A cell is cut from the part the layer's `crop` names, else the asset's, else the
whole image: a cell is that part's width over `cols` by its height over `rows`, and frame `n` sits
at column `n mod cols`, row `(n / cols) mod rows`. A layer whose frame has moved is drawn again;
the others are drawn once and kept.

A `file` whose first bytes are EBML's (`1A 45 DF A3`) is a WebM video. It plays its own frames at
its own rate, and `frames` then says only whether it loops. Its frame goes to the GPU as it
changes, a picture's pixels once.

The checker refuses a `frames` that is not an object, a `cols`, `rows`, `count` or `fps` that is
not a number above 0, and a `loop` that is neither true nor false.

**What the engine reads of an asset (Hyperion's addition).** A `file` and an `audio` name a file
in the design's own `files\` folder, beside its layout, as a package's do; a `data:` URL is not
read. `sd:` art is read from the player's 1.17 files: frame n of the GRP, whether its frames
are in runs, as the units' are, or whole, as `game\icons.grp`'s, in the game's palette as it is
now, and coloured again when the colours it uses change (a tileset's palette). `hd:` art is
checked but not read yet, so a layer naming it draws the plate a missing image draws.
`pixelated` picks the nearest sampler, else a linear one.

## Panels

A panel is a box placed against the screen's edges, holding layers. Panels draw in file order,
later ones on top.

| Field | Meaning | Default |
|---|---|---|
| `id` | Unique name; timelines target it. | required |
| `ax` | `left`, `center` or `right`: which edge `x` counts from. | required |
| `ay` | `top`, `center` or `bottom`: which edge `y` counts from. | required |
| `x`, `y`, `w`, `h` | Place and size, in units. | required |
| `between` | `[a, b]`. The panel stretches from the right edge of panel `a` to the left edge of panel `b`, each a panel earlier in the file, or from the `"left"` edge or to the `"right"` edge (the band's; with no band; the screen's). It then needs only `y` and `h`; `ax`, `x` and `w` are not used. So a deck between two blocks grows with the screen. | |
| `outside` | `left` or `right`. The panel fills that margin past the band, from the screen's edge to the band's, and needs only `y` and `h`; `ax`, `x` and `w` are not used. With no band, or a screen no wider than it, the margin is 0 wide and the panel is not drawn: its timelines run, its slots are empty and it takes no pointer. Not with `between`. | |
| `overlap` | With `between` or `outside`: `[left, right]`, units the panel reaches past its ends, under the panels there. | `[0, 0]` |
| `clamp` | `true` keeps the box in the band, or on the screen with no band (version 2 always did). Not applied to a panel `outside`. | `false` |
| `visible` | `false`, or a binding, hides the panel. | `true` |
| `origin` | `[fx, fy]`, the point `scale` and `rotate` turn about, as fractions of the box. | `[0.5, 0.5]` |
| `hover` | `true` fires `panel.hover` and `panel.leave` as the pointer enters and leaves the box. It takes no clicks. | `false` |
| `opacity`, `scale`, `scaleX`, `scaleY`, `rotate`, `shake`, `flicker` | As for layers, applied to the whole panel. | 1, 1, 1, 1, 0, 0, 0 |
| `layers` | The layers, drawn in order. | `[]` |
| `z` | Stacking: a panel with a higher `z` is drawn over one with a lower, whatever the file order. A run `between` two blocks must come after them in the file, but goes under them with `z: -1`. | 0 |
| `tip` | Where the tooltip of a button in this panel goes: `auto` (by its anchor), `above`, `below`, `left`, `right`, or `none` for no tooltip (Tooltips). | `auto` |
| `note` | Ignored. | |

The box's top left corner, for a screen LW units wide, laid out in the band, BL units from the
screen's left edge and BW wide (The band; with no band, BL is 0 and BW is LW):

```
left = BL + (ax == left   ? x
           : ax == right  ? BW - x - w
           :                (BW - w) / 2 + x)   (center: x moves the box's centre)
top  = ay == top    ? y
     : ay == bottom ? 1080 - y - h
     :                (1080 - h) / 2 + y
with between [a, b] and overlap [oa, ob]:
     from = a == "left"  ? BL      : a.left + a.w
     to   = b == "right" ? BL + BW : b.left
     left = from - oa,  w = max(0, to + ob - left)
with outside and overlap [oa, ob]:
     from = outside == left ? 0  : BL + BW
     to   = outside == left ? BL : LW
     to > from:  left = from - oa,  w = to - from + oa + ob
     otherwise:  w = 0, not drawn
with clamp: left = min(max(left, BL), BL + BW - w), top = min(max(top, 0), 1080 - h)
```

## The band

The user (2026-10-23): "For the hud, the ability to have it not go full width and
instead just extend the length like remastered does to 1920px and the side has filler length on
left and right via the skin. This way for those with muscle memory that prefer to not have to
access command to the far bottom right."

A file's `band`, or the `band` of an option value the player picked, is the most width the panels
are laid out across. On a screen wider than it, the band is centred:

```
B  = the picked value's band, else the file's band, else 0
BL = B > 0 and LW > B ? (LW - B) / 2 : 0
BW = B > 0 and LW > B ? B            : LW
```

Every panel is then placed in the band as if the band were the screen (Panels, above): `ax`
counts from its edges, `between`'s `"left"` and `"right"` are its edges, and `clamp` keeps a box
inside it. `ay` and `y` are as before, since the band is the screen's full height. A panel with
`outside` is the one exception: it fills a margin past the band, from the screen's edge to the
band's, so a skin can carry its console on to the screen's edges with no slots in it. Each margin
is `(LW - B) / 2` wide: 0 at 16:9, 300 at a 21:9 screen of 2520 x 1080, 320 at 5120 x 2160 (LW
2560), 960 at 32:9. At 16:9 and narrower the two Widths are the same screen.

The band moves what the game places by the file, and nothing else. The view, the camera and
clicks on the map are the whole screen's, as they are without a band; the map shows over the
margins where no panel covers them. A button's tooltip is placed against the screen, not the
band (Tooltips).

The samples. Each margin is filled in the style's materials and light, with no slots:

- **Full width** (`mech-span`, `mech-span-min`): `fillerLeft` and `fillerRight`, the console's
  deck, at the deck's height, carried on from under the blocks (`outside`, `overlap` 24 units
  under the block, `z: -1`). Its back, lip and lamp line stretch along it, its plated straps and
  their bolts repeat (`tileEvery` with `tileFit: "center"`), and its lamp follows the info deck's
  (the same tracks, appended).
- **Left and Right column** (`mech-flank`, `mech-flank-right`): the column stands at the band's
  edge, and `filler`, a full-height plated wall, fills the margin behind it (`z: -2`, under the
  spine). The other margin is map.
- **Pods** (`mech-pods`, `mech-pods-min`): Width moves the pods in from the edges; there is no
  filler, since the pods stand on the map.
- **R9** (`terran-r9`): `fillerLeft` and `fillerRight` in the R8 pieces, the console's back,
  rails, light strip and face, with seams every 200 px, tucked 41 px (21.96 units) under the
  blocks; the strip lights as the info block's first lamp.

## Raising the map

The user (2026-10-23): "In Hud a special global setting to tweak by px count a offset
of the map view. Sometimes you can have a skin that is a bit high on the sides for example, which
the skin may cover certain units or buildings in its way. This offset will push the map view up by
x amount. Should have a reset button to go back to default, only allow moving render up. And
accordingly, the view that shows on minimap as whats seen should account. In other words if 10px of
space moves the map view up, then the player does lose top 10px of view if you get what I mean."

This is the player's setting, kept with the profile, the same for every HUD, and no file holds
it. Its value N runs 0 to 200 in the HUD's units (1080 to the screen's height, so one value suits
every resolution) and is shown to the player as "px". 0 is the default, and Reset returns it.

With N above 0 the game:

- draws the battlefield N units higher: the view is `1080 - N` units tall and stands on the
  screen's top edge, so its bottom row is N units above the screen's foot. The top N of what the
  view showed is what goes. Below the view the screen is black, and the HUD is drawn over both as
  ever;
- gives the camera that shorter view. The minimap's box is `1080 - N` units of view tall, so its
  top comes down and its bottom stays. A click or drag on the map maps through the shorter view,
  the camera's limits are the map's edges for that view, and scrolling at the screen's top edge
  works as before;
- keeps the view's bottom row where it was when N changes, so units at the bottom come out from
  under the HUD and the top is lost;
- takes no clicks on the black strip as the map: a click there is on the HUD or on nothing.

Anything that reads the screen as it shows (a `glass` shape, a mirror) reads it with the black
strip.

In Settings it is a row of the HUD tab, under the design's own options: **Raise the map**, its
value in px, a slider (arrow keys a px, Shift or Page Up and Down 10, Home and End to the ends),
Reset, and the line "Draws the battlefield higher, so units along the bottom of the screen come
out from under a HUD that stands tall at its sides. You lose as much at the top of the view, and
the minimap's box shows what is left. Counted at 1080p, so one setting suits any resolution, and
the same for every HUD design." The mock reads `?mapUp=N`.

**The raise in Hyperion (Hyperion's addition).** The map is drawn N units higher and the top N
of the view goes off the screen, as above, with two differences:

- The screen below the view is not black. The map goes on under the HUD there, as it does under
  any HUD, and a click there that no panel takes is a click on the map.
- 1.17's camera limits stay as they are. So near the map's top edge the raise gives way: it is 0
  with the view's top on the map's top edge and all of N half a view further down. The map's top
  rows still come into sight.

The minimap's box loses N at its top and keeps its foot. The game's own view, which 1.17's
commands and sounds read, does not change. Only what is drawn and where a click lands move, so
the setting changes nothing in the game. It is the `mapRaise` key in Settings' `hud` section, and
it needs the GPU renderer, as the HUD designs do.

**The columns in Hyperion (Hyperion's addition).** A layout whose panels stand the screen's
whole height from its left or right edge, as `mech-flank` and `mech-flank-right` do, is a
column. Hyperion draws the map beside a column, not under it: the view is the part of the
screen left over, its middle that part's middle, the map's edges against the column's and its
foot on the screen's foot, since no panel covers it. The raise is not used with a column. A layout says nothing new for this; the panels' places say it.

## Layers

A layer is exactly one of six kinds:

- `"image": "<asset>"`: the asset, stretched to the box (or nine-sliced if the asset has a
  `slice`);
- `"rect": true`: a fill, a stroke and a corner radius;
- `"shape": true`: a panel drawn by a shader, crisp at any size, with gradients, a bevel, glass
  over the map, glows and an energy edge (see Shapes). Art images are for accents only;
  a HUD's plates and frames are shapes;
- `"text": "<template>"`: one line, or lines wrapped at a width, in one of the game's fonts;
- `"slot": "<name>"`: the game's own content (see Slots);
- `"run": {...}`: a kit's painted part tiled along the box between two ends (Runs below).

**The box.** A layer fills its panel unless it says otherwise. `inset: [left, top, right, bottom]`
shrinks it from the panel's edges; then `x`, `y`, `w`, `h`, where given, set it outright
(measured from the panel's top left corner). A layer may reach outside its panel.

An inset's left may be `null` when the layer has a `w` and no `x`; the layer then
stands `right` units in from the panel's right edge, at its own width, however wide the panel
grows. A `null` top with an `h` and no `y` does the same from the bottom. So in a panel that
stretches, a piece left of the stretch has a plain `x`, a piece right of it a `null` left, and a
piece across it an inset on both sides (with `pathFit: "slice"` to keep its ends as drawn):

```json
{"id": "lampTab", "shape": true, "look": "steel", "inset": [null, 40.71, 52.5, 183.21], "w": 87.32, "h": 28.39, "path": "M 0 17.68 L 16.07 0 L 87.32 0 L 87.32 28.39 L 0 28.39 Z"}
```

**Every layer** (slots take only the ones marked S):

| Field | Meaning | Default | Moves |
|---|---|---|---|
| `id` | Name within the panel; timelines target `panel.layer`. | none | |
| `look` | The role this layer plays (`frame`, `screen`, `readout`, ...): the style's look of that name dresses it (Styles). A layer's own values win over its look. | none | |
| `material` | A material preset by name (Materials), under the layer's own values. Usually the look sets it. | none | |
| `visible` (S) | `false` or a binding hides it. | `true` | |
| `origin` (S) | Turn point, fractions of the box. | `[0.5, 0.5]` | |
| `opacity` (S) | 0 to 1. | 1 | yes |
| `scale`, `scaleX`, `scaleY` (S) | Multiply together about `origin`. | 1 | yes |
| `rotate` (S) | Degrees about `origin`. | 0 | yes |
| `blend` | `normal`, or `add` (the layer is light: it adds its colour, and black adds nothing). | `normal` | |
| `tint` | Multiplies the layer's colour. | `#ffffff` | yes |
| `add`, `addColor` | Adds `addColor` times `add` (0 to 1) over the layer's own pixels. | 0, `#ffffff` | yes |
| `glow`, `glowColor`, `glowRadius`, `glowMode` | A blurred copy of the layer's shape in `glowColor`, added `under` or `over` it. `glow` is its strength; above 1 it is drawn again. | 0, `#5fd0ff`, 12, `under` | glow, colour, radius |
| `wipe`, `wipeAngle`, `wipeSoft` (S) | A reveal travelling along `wipeAngle` with an edge `wipeSoft` units soft: 0 hidden, 1 shown. | 1, 0, 24 | wipe |
| `scan`, `scanMode`, `scanAngle`, `scanWidth`, `scanSpacing`, `scanColor`, `scanStrength` | A sweep across the layer's own pixels, added. `scan` is its progress, 0 to 1; absent means none. `band` is one soft band `scanWidth` wide; `lines` is lines every `scanSpacing` units that drift with `scan`. | none, `band`, 0, 60, 6, `#9fe8ff`, 0.6 | scan, strength |
| `flicker` (S) | 0 to 1: how deep random dips in opacity go. | 0 | yes |
| `shake` (S) | Units: how far a random offset reaches. | 0 | yes |
| `meter`, `meterFrom` | Show only this fraction of the layer (0 to 1), from `left`, `right`, `top` or `bottom`: bars. | 1, `left` | meter |
| `crop` | An image layer's own `[x, y, w, h]`, over the asset's. | | |
| `note` | Ignored. | | |

**Tiling.** Any layer but a slot can repeat along its panel: copies `tileEvery` units
apart along `axis` (`x` or `y`), from its own box to `tileEnd` units short of the panel's far
edge. `tileFit` says how they fill that span: `center` (default) draws as many whole copies as
fit, centred; `start` the same from the box; `round` a whole number of copies that fill the span
exactly, each lengthened or shortened by what is over, which a sliced path (Shapes, Paths) takes
up in its straight runs. So a rail of a panel `between` two blocks grows by repeating its pieces,
never by stretching them. A track on a tiled layer moves every copy.

| Field | Meaning | Default |
|---|---|---|
| `tileEvery` | Units from one copy to the next; 0 is one copy. | 0 |
| `tileEnd` | Units kept clear at the panel's far end. | 0 |
| `tileFit` | `center`, `start` or `round`. | `center` |
| `axis` | `x` or `y`. | `x` |

**Runs.** `run` names assets: `{"left": a, "mid": b, "right": c}` along `x`, or
`{"top": a, "mid": b, "bottom": c}` with `axis: "y"`. `mid` is required, the ends optional. Each
piece is scaled to the box's thickness (its height along `x`, its width along `y`), keeping its
proportions. The ends sit at the box's two ends at that size, both shrunk alike if together
they are longer than the box. `mid` repeats between them:

- `runFit: "round"` stretches it a little so a whole number of copies, at least one, fills the
  span;
- `"cut"` repeats it at its own length and cuts the last copy short, taking the same share of
  its art.

Every piece but an insert is drawn 0.4 units longer along the run, which closes hairline seams.

`inserts`, a list of assets, go along the middle span. The span is cut into
`floor(span / insertEvery)` equal parts, and one insert, taken from the list in turn, is centred
in each, at the middle piece's scale. Along `x` they sit at `insertAlign` (`center`, `top` or
`bottom`), moved down by `insertY` units; along `y` they are centred. A run may be `lit`.

| Field | Meaning | Default |
|---|---|---|
| `run` | The pieces, by asset name. | required |
| `axis` | `x` or `y`. | `x` |
| `runFit` | `round` or `cut`. | `round` |
| `inserts` | Assets laid along the middle, in turn. | none |
| `insertEvery` | Units of the middle span to each insert; 0 is none. | 0 |
| `insertAlign` | `center`, `top` or `bottom` (along `x`). | `center` |
| `insertY` | Units the inserts move down (along `x`). | 0 |

**Lit images.** An `image` or `run` layer with `lit` draws its art through a second
shader, `HudLit` (`SCHudShape.lit`; `SCHudShape.hlslLit()` writes its HLSL). The shader relights
the painted art from its asset's `normal` and `fx` maps. Without WebGL2 the layer draws as a
plain image.

The art already carries a light, the one it was painted with (`artLight`, `artElevation`). The
surface is relit by the ratio of the new light's shading to that one's, so with the light where
the art has it the picture is unchanged, and moving the light moves the shading. Then, in order:

1. a highlight;
2. grime;
3. the glass: a reflection, scanlines and the power sweep;
4. the lamps: their own colour, pulse, flicker and a glow;
5. a grade.

| Field | Meaning | Default |
|---|---|---|
| `lit` | 0 draws the art as it is, 1 relit; between, a mix. Absent, a plain image. | none |
| `light`, `lightElevation`, `lightStrength`, `ambient`, `lightColor` | The new light: its angle in degrees (as a shape's `light`), its elevation in degrees, its strength, the ambient share and its colour. | 225, 45, 1, 0.35, `#ffffff` |
| `artLight`, `artElevation` | The light the art was painted with. | 225, 45 |
| `specular`, `roughness` | The highlight's strength, and how broad it is (0 tight, 1 broad). | 0.3, 0.55 |
| `reflect` | Over the glass: a cool sky fading down it, a diagonal streak and the battlefield behind, faint, at this strength. | 0.4 |
| `glassLines`, `lineSpacing` | Scanlines on the glass, their depth (0 to 1) and their spacing in units. | 0, 4 |
| `emissive` | How strongly the lamps shine in `lampColor`. A lamp takes the colour as it shines, so a lamp painted amber can be driven red. | 0.6 |
| `lampColor`, `lampGlow`, `lampGlowStrength` | The lamps' colour, and a glow round them: its radius in units and its strength. The glow spills past the art's edge. | `#ffb040`, 6, 0.8 |
| `lampPulse`, `lampRate`, `lampFlicker` | The lamps' pulse depth (0 to 1) and rate (Hz), and a flicker's depth. | 0, 0.5, 0 |
| `grime`, `grimeScale` | Dirt in the crevices and in patches: its amount, and the patches' size in units. | 0, 40 |
| `contrast`, `saturation`, `grade`, `exposure` | The grade: contrast about mid grey, saturation, a colour multiplied in, and exposure. | 1, 1, `#ffffff`, 1 |
| `power`, `sweepColor` | 0 is off and 1 on. Between, the part comes on from the left with a band of `sweepColor` at the edge; unpowered lamps go dark and the glass dims. Region scans use `sweepColor` too. | 1, `#9fe8ff` |
| `regions` | Up to eight parts of the layer with values of their own (below). | none |

Every field but `artLight`, `artElevation`, `grimeScale`, `lineSpacing`, `sweepColor` and
`regions` moves.

**Regions.** Each region has an `id` and exactly one of:

- `path`: three or more `[x, y]`, as fractions of the layer's box;
- `rect`: `[x, y, w, h]`, as fractions of the box;
- `brush`: an image laid over the box, its alpha the weight (a path like an asset's `file`, or a
  `data:` URL);
- `pick`: `{"color": "#rrggbb", "range": r}` takes the art's pixels within `r` of that colour,
  where `r` (0 to 1, default 0.15) is a share of the colour cube's diagonal, with a soft edge in
  the last 40% of it; `{"from": "lights"}` or `{"from": "glass"}` takes the `fx` map's lamps or
  glass.

`feather` softens the edge by that many units, and `invert` swaps inside and out. A region sets
only the groups it names a field of. In each, its values replace the layer's where it is,
blended by its weight, later regions over earlier:

- light: `light`, `lightElevation`, `lightStrength`, `ambient`, `lightColor`;
- depth: `height` (a raised region catches more light, 4% a unit), `bevel` (units: its edge
  slopes, from its weight's gradient) and `parallax` (units: it slides against the rest as the
  panel crosses the screen);
- material: `specular`, `roughness`, `reflect`, `emissive`, `grime`, `grimeScale`, `contrast`,
  `saturation`, `grade`;
- effects: `lampPulse`, `lampRate`, `lampFlicker`, `glassLines`, `scan` (0 to 1: a band of
  `sweepColor` down the region, at that point) and `shimmer` (an iridescent sheen, its
  strength).

A region is a timeline target, `panel.region`, so its name must differ from every layer's and
every other region's in its panel. A track may move any of its fields but `grimeScale`.

**Rect:** `fill` (a colour, or a list of colours as a top-to-bottom gradient), `stroke` (a colour
or list), `line` (stroke width, drawn inside the box, default 1), `radius` (default 0). `fill`,
`stroke`, `line` and `radius` move.

**Text:** `text` is a template (Bindings). `font` is one of the game's: `eurostile`,
`eurostile-ext`, `blizzard` (Remastered's Eurostile, Eurostile Extended and Blizzard faces;
the mock falls back to Saira without the art), or one of the faces Hyperion
ships, by name: `chakra-petch`, `saira`, `michroma`; `weight` (100 to 900, the face's own
regular when absent) picks its weight; Hyperion has the 400, 500, 600 and 700 cuts of Chakra
Petch (Tooltips, The font). `size` in units (default 20), `color` (default
`#c9d4dd`), `align` `left`, `center` or `right`, `valign` `top`, `middle` or `bottom` (default
middle), `wrap` (`true` for the box's width, or a width in units; otherwise one line),
`lineHeight` (times the size, default 1.2), `shadow` (a colour drawn under the text, offset
by `shadowOffset`, default `[1, 1.5]`, and blurred by `shadowBlur` units, default 0, a hard
shadow), `outline` (a colour round each glyph, `outlineWidth` units wide, default 2, under the
fill), `tracking` (extra units between letters, default 0). `color`, `size`, `outline`,
`outlineWidth` and `tracking` move.

Text is drawn sharp at any size. The plan was MSDF, with the outline, glow and shadow from one
distance field. Hyperion as built (2026-10-23) instead rasterises each glyph at the size it is
drawn (stb_truetype). So `shadowBlur` is not drawn there: the shadow is one hard copy at its
offset. An outline is offset copies of the glyphs. A design should not depend on a soft shadow.
The mock draws vector text at the device's own pixels, with the shadow's blur.

**The fonts Hyperion draws (Hyperion's addition).** The engine draws every text in the fonts it
ships, as the design center draws a file without Remastered's: `eurostile`, `blizzard` and `saira`
in Saira, `eurostile-ext` and `michroma` in Michroma, and `chakra-petch` in the cut its `weight`
picks, 400 below 450, 500 from 450, 600 from 550 and 700 from 650 (`HudFaceOf` in
`port_hudplay.cpp`, `TextPrims` in `port_gpu_hud.cpp`). A `weight` of 550 or more on any other
font gives Saira's semi-bold; Michroma has one cut, so a `weight` on it changes nothing.
Remastered's own Eurostile and Blizzard faces are not read. The glyphs are rasterized by
stb_truetype at the size they are drawn, into one atlas (`port_ui.cpp`), not as a distance field:
the shadow is one offset copy of the text and `shadowBlur` is not drawn, and the outline is eight
copies of the glyphs `outlineWidth` units round them.

## Parts (Hyperion's addition)

A `parts` block holds layers and panels by name, so a design is built from pieces instead of
repeating them. The engine lays every use out before the file is checked
(`port_hudfile.cpp`, `ExpandParts`), so what the checker and the player see is the layers the
parts made.

**Where the parts come from.** The layout's own `parts`, then each parts file beside the layout
(a package's `parts\*.json`, read in name order), then the style's `parts`. A name no earlier
source has is taken from the next; the first found wins, so a design can override a shared
part by giving one of the same name in its layout. The layout's `parts` is taken out of the file
once the uses are laid out.

**A part** is an object with the layers, or the panel, it stands for, and the params it takes:

```json
"parts": {
  "cell": {
    "params": {"w": 72, "h": 40, "look": "well"},
    "layers": [
      {"id": "well", "shape": true, "look": "$look", "x": 0, "y": 0, "w": "$w", "h": "$h"},
      {"id": "pip", "shape": true, "look": "lamp", "x": "= 6 + $i * 10", "y": "= $h - 8", "w": 6, "h": 3,
       "repeat": {"count": 4, "dx": 0, "dy": 0}}
    ]
  }
}
```

| Field | Meaning |
|---|---|
| `params` | The defaults, by name. A use's `with` lays its own over them. |
| `layers` | The layers the part stands for, used among a panel's `layers`. |
| `panel` | The panel it stands for, used among the file's `panels`. |

A part needs `layers` or `panel`, and its `params` must be an object, or the file does not load.

**A use among a panel's layers** names the part and places it:

```json
{"part": "cell", "id": "res1", "x": 28.39, "y": 12.86, "with": {"w": 86}, "look": "wellLit"}
```

`part`, `with`, `id`, `x`, `y` and `note` are the use's own. Every other key of the use (`look`
above, and `visible`, `tint`, `tip` or any other field a layer takes) is set on each of the
part's layers, over what the part gives them. The use's `x` and `y` move the part's layers that
have a number for `x` or `y`; a layer placed by its `inset` is left where the part puts it.

**The values.** A value that is one `"$param"` takes that param's value as it is, of whatever kind
the param holds. A value that starts with `=` is arithmetic of numbers, `$params`, `+ - * /`,
brackets and unary minus, worked out to a number; a param's name there is letters, digits and
`_` alone, so `"= $w-2"` is `$w` less 2. A `$param` inside a longer string becomes its text
(`"text": "GROUP $n"`); a name read there runs over letters, digits, `_`, `.` and `-`, so
`$n.count` is looked for as the one name `n.count`, and a param followed by a dot or a dash needs
a name of its own. A `$name` that is no param of the part's is left as it is, for the style's
palette to fill. Arithmetic that does not work out, names a param that is not a number, divides
by zero or has anything left over is an error. A `with` naming a param the part has no default
for is a note, and the param is set all the same.

**`repeat`** on a part's layer lays it `count` times, each copy `dx` right and `dy` down from the
one before, with `$i` its index from 0. The repeat's own fields take the params first, so a count
may be `"$pips"`. A `count` that is not a whole number from 0 to 64 is an error, and 0 lays
nothing.

**The ids.** A use's id is its own `id`, else the part's name with the number of that use in the
file, from 1 (`cell-1`, `cell-2`). Each of the part's layers is then `<use's id>-<the layer's own
id, or its place in the part>`, and a repeat adds `-<i>`: the use above gives `res1-well` and
`res1-pip-0` to `res1-pip-3`. A timeline targets those names.

**A panel from a part** names a part that has a `panel`:

```json
{"part": "column", "id": "left", "ax": "left", "ay": "bottom", "x": 0, "y": 0, "w": 300, "h": 1080,
 "with": {"side": "left"}, "layers": [{"id": "badge", "shape": true, "look": "trim"}]}
```

The part's panel is taken, its params put in, then every key of the use but `part`, `with` and
`layers` is set over it, so the use gives the panel its `id`, its anchors and its box. The use's
own `layers` come after the part's.

**The limits.** A part's layers may use parts of their own, 8 deep; past that the file does not
load. A repeat's count is 64 at most. A use naming no part, a use among layers naming a part that
has only a `panel`, and a panel naming a part that has no `panel`, are errors.

## Shapes

A shape layer is a signed distance field: for each pixel, how far it is from the shape's edge,
negative inside. Everything about its look is worked out from that distance and the
direction to the edge, in one pass of one shader, so it is crisp at any size and every shape
on the HUD shares the same light. `hudshape.js` is the shader. Its body is written once in an
HLSL-shaped dialect (`float2`, `lerp`, `saturate`, `frac`) that a few `#define`s turn into
WebGL2's GLSL for the mock; `SCHudShape.hlsl()` returns the same body as an HLSL pixel shader
with a constant buffer of these fields, for the presenter.

**The outline.** The box is the layer's box.

| Field | Meaning | Default | Moves |
|---|---|---|---|
| `corners` | Corner size in units: one number for all four, or `[tl, tr, br, bl]`. | 0 | yes |
| `cornerStyle` | `round` (a quarter circle), `chamfer` (cut at 45°), `notch` (a quarter circle bitten out). One, or one per corner. | `round` | |
| `cutouts` | Up to four `[x, y, w, h, radius]` boxes, from the layer's top left, taken out of the shape. | none | |

The distance, with `p` measured from the box's centre, `b` half the box and `c` the corner's
size, in units:

- round: `length(max(abs(p) - b + c, 0)) + min(max(q.x, q.y), 0) - c` with `q = abs(p) - b + c`;
- chamfer: `max(box(p), (abs(p).x + abs(p).y - (b.x + b.y - c)) × 0.7071)`;
- notch: `max(box(p), c - length(abs(p) - b))`;
- each cut-out: `d = max(d, -roundBox(p - its centre, its half size, its radius))`.

The direction to the edge, `n`, is the distance's gradient, by central differences half a
unit apart. Coverage is `saturate(0.5 - d / px)` with `px` the units per screen pixel, which
is the anti-aliasing.

**The look**, in the order the shader builds it:

| Field | Meaning | Default | Moves |
|---|---|---|---|
| `fill` | A colour, or two to four stops spread evenly along `fillAngle`. Colours take alpha (`#rrggbbaa`). | clear | yes, a single colour |
| `fillAngle` | Degrees: 90 runs top to bottom, 0 left to right. | 90 | |
| `glass` | 0 to 1: how much of the screen behind shows through, blurred. The blurred screen is tinted by `glassTint`, the fill goes over it at the fill's own alpha, and the shape's alpha rises toward opaque by `glass`. | 0 | yes |
| `glassTint` | Colour and strength (its alpha) of the tint on the blurred screen. | `#0a1622a0` | yes |
| `glassBlur` | How many halvings the blur goes down (see below). The most any glass layer asks for is what the frame does. | 4 | |
| `glassNoise` | Grain on the glass, up to half this much brightness either way, one value per screen pixel. | 0.03 | yes |
| `glassStreak` | A diagonal reflection: a wide soft streak and a thin one beside it, this bright. | 0 | yes |
| `bevel` | Units in from the edge over which the surface slopes up. | 0 | yes |
| `bevelStrength` | How much the slope catches the light (and darkens away from it). | 0.6 | yes |
| `light` | Degrees the light comes from, shared by bevel, inner shadow and rim: 225 is the top left. Every sample uses 235. | 225 | yes |
| `highlight` | The light's colour; its alpha scales it. | `#ffffff` | yes |
| `innerShadow` | Units the shape is moved away from the light to cast its own edge inward: a sunken screen. | 0 | yes |
| `innerShadowStrength` | How dark that shadow is. | 0.5 | yes |
| `rim`, `rimWidth`, `rimColor` | A thin line of light just inside the edge, `exp(-depth / rimWidth)`, brightest facing the light (35% facing away). | 0, 3, `#ffffff` | yes |
| `innerGlow`, `innerGlowSize` | A colour added inward from the edge, `exp(-depth / size)`. | none, 12 | yes |
| `lines`, `lineSpacing` | Scanlines: a sine across the screen's rows every `lineSpacing` units, drifting down 18 units a second, darkening by up to `lines × 0.6`. | 0, 4 | lines |
| `holo` | Holographic shimmer: a slow diagonal band of shifting hue. | 0 | yes |
| `stroke`, `line` | An edge line `line` units wide, just inside the edge. | none, 1 | yes |
| `energy`, `energyColor`, `energyWidth` | An energy edge: light on both sides of the edge, `exp(-abs(d) / width)`, flowing round the shape with two travelling waves. It is what pulses on alerts. | 0, `#5fd0ff`, 6 | yes |
| `outerGlow`, `outerGlowSize` | Light (or, in black, a drop shadow) outside the edge, `exp(-d / size)`. | none, 12 | yes |

Depth is `max(-d, 0)`, units in from the edge. The energy edge's waves run along the edge, the
distance round the box clockwise from its top left corner measured on the nearest side, with a
whole number of waves to the round so it has no seam; they are
`0.55 + 0.45 sin(k1 s - 7t) sin(k2 s + 2.3t)` with `k1` near 0.09 and `k2` near 0.023 per unit.
The output is premultiplied: the shape's own colour times its coverage, plus the light outside
it where it does not cover.

A shape draws light outside its box (`outerGlow`, `energy`); the drawn area is the box plus
three times the larger of `outerGlowSize` and `energyWidth`, at most 80 units, on every side.
The other layer effects (tint, add, scan, wipe, glow) act on a shape as on any layer.

**The blur behind glass** is the presenter's dual filter (Kawase), run once a frame on the
screen under the HUD: halve it `glassBlur` times, each step averaging the centre four times and
the four diagonal half-texel neighbours once (÷ 8), then double it back up `glassBlur − 1`
times, each step taking eight taps round the point, the four edge-centres once and the four
diagonals twice (÷ 12). A glass pixel reads that half-size result at its own place on the
screen. The game's own world goes in; the HUD does not see itself.

**Cost.** One full-screen blur a frame if any glass is on screen, then one quad per shape. A
shape with glass, scanlines, shimmer, an energy edge, a moving wobble, a warp reveal under way
or a living surface (membrane, flesh, sacs, crystal, psionic) redraws every frame; others only
when a value changes. In the mock, on this machine's GPU (an RTX 5090), every sample style holds 60 frames a second at 16:9; nothing has been measured on a modest card yet.

### Paths, profiles, patterns and relief

A generated Terran kit (`huds/kits/terran-sc2kit/`) is drawn as shapes, and a box with cut
corners could not draw it: its corners step, its plates sit on plates, its edges are bevelled
twice, grooved and lipped, and it has grilles, vents, scratches and bolts under one light. These
fields are what that took. The kit's 63 parts traced with them are in
`huds/kits/terran-sc2kit/traces.json`, compared with the art on `trace.html`;
`huds/terran-sc2kit.json` is a HUD built from them.

**Paths.** `path` replaces the box as the outline: an SVG path (`M`, `L`, `H`, `V`, `Q`, `C`, `A`,
`Z`, absolute or relative), each curve cut into 2 to 24 straight edges about 6 units long. Every closed loop
counts: a loop inside another is a hole (even-odd), so a bezel is its outside and its window. The
distance is the polygon's: the nearest edge, its sign by crossings. The shader holds 96 edges a
layer; a longer path is cut at 96, with a warning when the file is checked. Bolts, the energy edge
and anything else that runs round the outline run round the path.

| Field | Meaning | Default |
|---|---|---|
| `path` | The outline, in the units of `pathSize`. | none (the box) |
| `pathSize` | `[w, h]`: the frame the path was drawn in. | the box |
| `pathFit` | How that frame meets the layer's box: `slice` (below), `stretch` (scaled to the box each way) or `fit` (scaled evenly, centred). | `slice` |
| `pathScale` | Units in the box per unit of the path, for `slice`. | box height / pathSize height |
| `pathSlice` | For `slice`: `[x, y]` lines in the path's units, or a band `[x0, y0, x1, y1]`. | the frame's centre |

`slice` keeps a piece's detail at its own size whatever the box: a point left of the slice line
keeps its distance from the box's left edge (times `pathScale`), a point right of it its
distance from the right edge, and likewise above and below. With a band, the points between
`x0` and `x1` are spread over whatever length is left between the two ends, so the piece can
shorten as well as lengthen; if the box is shorter than the two ends, the ends are scaled down
together. A trace puts its slice lines (or band) across straight runs only, so what changes
length is a plain plate or a straight band, never a bolt, a lamp or a diagonal.

**Edge profiles.** The slope inside the edge (`bevel` units wide) is one of:

| `profile` | The cross-section, going in from the edge |
|---|---|
| `slope` (default) | a ramp that eases into the top: the one bevel there was before |
| `chamfer` | one flat 45° face, and a hard crest where it meets the top |
| `step` | `profileSteps` terraces, each a riser and a flat, a dark crease at each riser's foot |
| `double` | a steep face for the outer 35%, then a shallow one |
| `lip` | a raised lip: up, then down into what it rims, the foot of the fall in shadow (a bezel round a screen) |
| `groove` | the slope, and a groove `profileWidth` wide at `profileAt` units in |

Each gives, at a depth, how steeply the surface rises (the normal leans out by it, so it takes
the light), how much ambient light a crease or channel shuts out, and where its crests are (the
lines where a face meets a flat, which catch a line of light; see `crest` below).

**Panel lines.** `panelLines` grooves (up to 4) parallel to the outline, the first
`panelLineInset` units in, each `panelLineWidth` wide, `panelLineGap` apart: the cut lines
between plates. They are profile grooves, so they shade with the light. `seamAngle` turns the
straight `seams` (Detail) from upright, in degrees.

**Patterns.** `pattern` cuts a pattern into the face, `patternInset` units in from the edge:

| `pattern` | |
|---|---|
| `honeycomb` | hexagonal holes `patternScale` across, rounded walls between them, the far side of each hole's wall lit |
| `slats` | bars across, every `patternScale` units, each rounded, the gaps dark |
| `perforated` | round holes on a square grid |

`patternWall` is the wall's (or the gap's) share of a cell, `patternDepth` how dark the holes
go, `patternAngle` turns it.

**Scratches.** `scratches` (0 to 1) scatters short thin lines about `scratchScale` units long,
mostly at `scratchAngle` degrees, each turned by up to `scratchWander` either way, bright where
they cut through to bare metal.

**Stacked shapes and contact shadows.** Layers stacked in one panel share the light, so a trim
on a plate on a base reads as one object. `castShadow` (0 to 1) lays the shape's own outline,
moved `elevation` units away from the light and softened by `shadowSoft`, as a shadow on what
the panel has already drawn under it, and on nothing else: it never falls on the battlefield.
It is a second, small pass of the shader (`HudShadow` in the HLSL), drawn just before the shape.

**Relief.** `relief` (default 1) scales how hard the light models the surface: the profile's
slopes, the pattern's ridges and the shading of creases. Above 1 a dark gunmetal still reads as
carved. `crest` (0 to 1.5) draws the profile's crests as fine lines in `rimColor`, brightest
facing the light: the pale edge highlight on every step of the kit's art.

| Field | Default | Moves |
|---|---|---|
| `profile` | `slope` | |
| `profileSteps`, `profileAt`, `profileWidth` | 2, 6, 3 | profileAt |
| `panelLines`, `panelLineInset`, `panelLineGap`, `panelLineWidth` | 0, 10, 4, 1.2 | |
| `seamAngle` | 0 | |
| `pattern`, `patternScale`, `patternDepth`, `patternInset`, `patternAngle`, `patternWall` | none, 8, 0.8, 0, 0, 0.25 | patternDepth |
| `scratches`, `scratchScale`, `scratchAngle`, `scratchWander` | 0, 30, 0, 25 | scratches |
| `castShadow`, `elevation`, `shadowSoft` | 0, 3, 2 | all three |
| `relief`, `crest` | 1, 0 | both |
| `pathScale` | | yes |

In the HLSL these are `uPath[96]` (each edge's two ends, from the box's centre) and `uPathN`,
`uProfile` (kind, steps, at, width), `uPanel` (count, inset, gap, width), `uPattern` (kind, cell,
depth, inset) and `uPattern2` (angle, wall), `uScratch` (amount, length, angle, wander), `uShadow`
(strength, height, softness), `uRelief` (relief, crest), and `uSeams.w`, the seams' angle. Round
11 adds `uWear` (amount, depth) and `uWearColor`, `uSheen` (strength) and `uSheenColor`, and
`uBolts.w`, the bolt head.

**Cost.** The path loop is the heaviest new part: up to 96 edges a pixel, and again for each of
the gradient's samples. The Terran SC2-kit HUD is about 230 shape layers; each draws once and
again only when a value changes, so the console costs in loading and in its slide, not in play.

### Surfaces

A shape with a `surface` is lit as a 3D surface instead of a flat fill: the bevel slopes it
down to the edge, the surface leans its normal (`tilt`) for brushing, plates, facets and
folds, and one light (`light`, `highlight`) gives it diffuse and specular. With
`N = normalize(n × slope + tilt, 1)`, `slope = (1 − smoothstep(0, bevel, depth)) ×
bevelStrength × 1.6` and `L = normalize(lightFrom, 0.9)`:

```
colour = albedo × (0.38 + 0.75 × saturate(N·L)) + spec × 1.4 + emit
spec   = pow(saturate(reflect(−L, N).z), 6 + 60 × gloss) × gloss, in highlight, or in the
         albedo where the surface is metal
```

The fill's alpha still applies (a surface takes `fill: "#ffffff"` unless it says otherwise),
the surface's own alpha multiplies it, and glass then works as before, the screen behind bent
by the tilt for membrane (10 units) and crystal (24).

| Field | Meaning | Default | Moves |
|---|---|---|---|
| `surface` | `none`, `brushed`, `gunmetal`, `painted`, `hazard`, `chrome`, `polished`, `carbon`, `polymer`, `chitin`, `membrane`, `flesh`, `sacs`, `crystal`, `filigree`, `psionic`, `ceramic`, `machined`, `cerakote`, `forged`, `armor`. | `none` | |
| `surfaceA`, `surfaceB`, `surfaceC` | The surface's colours: base, second, and accent or glow (each surface below says which is which). | grey, dark grey, white | yes |
| `surfaceScale` | Multiplies the pattern's size. | 1 | yes |
| `gloss` | 0 to 1: how sharp and strong the highlight is. | 0 | yes |
| `wear` | 0 to 1: paint worn to bare metal at the edges, in patches and in scratches (painted, hazard). | 0 | yes |
| `detail` | 0 to 1.5: how strong the fine pattern is (streaks, speckle, plates, veins, facets). | 1 | yes |
| `surfaceSpeed` | Multiplies the living surfaces' clock. | 1 | yes |
| `mirror` | `true`: patterns mirror about the vertical centre line. Protoss symmetry. | `false` | |
| `sheen`, `sheenColor` | a studio's reflection on the surface, so dark metal reads as metal. The view's reflection off the normal finds a soft box on the light's side (strongest where the surface leans, at bevels and crests), and a broad fall of light crosses the face from the light's corner of the box; less on a surface that is not metal. | 0, `#b8c6d4` | both |
| `edgeWear`, `edgeWearWidth`, `edgeWearColor` | bare metal where a coat has rubbed off the outline and the profile's crests, `edgeWearWidth` units deep, broken into chips by noise; more wear reaches further in. On any surface, unlike `wear`. | 0, 2, `#8f9aa6` | amount, colour |

The patterns come from three noises, all in the shader: value noise `vnoise` (0 to 1, smoothstep
interpolation of a hash per lattice point), `fbm` (four octaves, each at 2.03 times the
frequency and half the weight), and Voronoi cells (`voronoi`: the distance to the nearest and
second nearest cell point and the nearest point itself). The hash is any good 2D to 1D hash;
the engine's need not match the mock's numbers, only its look.

| Surface | What it draws, with `u = p / surfaceScale` in units |
|---|---|
| `brushed` | streaks along x (`vnoise` at 0.04 × 1.6 and 0.15 × 5 of u), B to A, a tilt across them; metal |
| `gunmetal` | `fbm(u × 0.08)` mottling B to A with a 1.5-unit speckle; metal |
| `painted`, `hazard` | paint A (hazard: diagonal stripes of A and C every 28 units) worn to bare metal B where `fbm(u × 0.06)` plus an edge term crosses `1.02 − wear × 0.6`, with scratches; the worn parts are metal and glossier |
| `chrome` | a sky A and a ground B reflected about a wavy horizon, a bright line on it; metal |
| `polished` | a soft band of reflection, B to A across the box; metal (gold and bronze trim) |
| `carbon` | a twill of fibres crossing every 6 units, each lit along its run |
| `polymer` | A, matte, a faint grain |
| `chitin` | Voronoi plates 22 units across, each domed (tilted from its centre), A with a lighter rim B and dark seams C between |
| `membrane` | two drifting `fbm` folds, B to A, very glossy, 78 to 98% opaque, bends the glass behind; living |
| `flesh` | A mottled, veins (where an `fbm` crosses its middle) in B that swell with a 2.6 s pulse and glow in C; living |
| `sacs` | Voronoi cells 30 units across, round sacs in C that glow, each on its own beat; living |
| `crystal` | Voronoi facets 26 units across, each leaning its own way, B to A, a moving caustic and lit facet edges in C, bends the glass behind; living |
| `filigree` | raised gold scrollwork A (two crossing wavy sines every 34 units) over a pale ground B |
| `psionic` | a domain-warped `fbm` flow B to A, glowing in C where it gathers, 60 to 100% opaque; living |
| `ceramic` | pale and smooth, B to A, a faint cloud in the glaze |
| `machined` | dark anodised metal cut by a tool: fine marks along x (`vnoise` at 0.05 x 2.2 of u) that change with each 40-unit pass, a slow `fbm(u x 0.035)` mottle, B to A, a tilt across the marks; metal |
| `cerakote` | a matte ceramic coat, a 0.8-unit stipple and a faint orange peel (`fbm(u x 0.11)`, tilting the normal), B to A |
| `forged` | forged carbon, Voronoi flakes 9 units across (warped by an `fbm`), each its own shade B to A and its own lean, so the light finds a few at a time; each flake's gloss varies |
| `armor` | a hull's plating. Cells 120 x 72 units, each split up to three times across its longer side at 30 to 70% (a third of the time it stops sooner), so the plates come in many sizes; each plate its own shade B to A; the joins grooves in C lit along one lip; a socket screw in each corner of a plate over 26 x 18; about one plate in fourteen louvred. Metal |

### Materials

A material is a named preset of shape fields: `"material": "steel.brushed"`. It sits under the
look and the layer, so anything it sets can be changed on top. Every material is drawn by the
shader alone: none needs an image. `SCHudShape.MATERIALS` holds them; each has a `family`,
which the editor's sweeps keep to when they swap one material for another.

| Material | Family | Surface | Set |
|---|---|---|---|
| `steel.brushed` | metal | brushed | `#a9b3bd` / `#5b646d`, gloss 0.55, bevel 8 |
| `steel.gunmetal` | metal | gunmetal | `#4d5760` / `#262c33`, gloss 0.35, detail 0.7, bevel 8 |
| `plate.painted` | metal | painted | olive `#46523f` over `#8d969e`, wear 0.45, bevel 7 |
| `paint.hazard` | metal | hazard | `#e8b923` and `#17191b` stripes over `#8d969e`, wear 0.5 |
| `metal.chrome` | metal | chrome | `#eef5fb` / `#1b2229`, gloss 1 |
| `trim.gold` | metal | polished | `#f4d273` / `#8a5a17`, gloss 0.85, bevel 4 |
| `trim.bronze` | metal | polished | `#d69c60` / `#5e3517`, gloss 0.75, bevel 4 |
| `composite.carbon` | composite | carbon | `#40464d` / `#0f1113`, gloss 0.6 |
| `composite.polymer` | composite | polymer | `#2c3238`, gloss 0.12 |
| `glass.frosted` | glass | none | glass 0.85 over a cool tint, grain, a faint streak (the glass deck) |
| `glass.visor` | glass | none | glass 0.9, amber tint, a strong streak |
| `energy.hologram` | glass | none | a faint blue fill, shimmer, scanlines, an inner glow and a thin edge |
| `energy.plasma` | glass | none | no fill: an energy edge and its glow only |
| `organic.chitin` | organic | chitin | `#4c2c3b`, rims `#b07a92`, seams `#120a10`, gloss 0.7, bevel 12 |
| `organic.membrane` | organic | membrane | `#b9607a` / `#4f1e30`, gloss 0.85, glass 0.35 |
| `organic.flesh` | organic | flesh | `#7a2e3a`, veins `#3a1030` glowing `#ff5a3a` |
| `organic.sacs` | organic | sacs | `#3a1a2a` / `#5a2a3a`, sacs `#ffb03a` |
| `crystal.khaydarin` | crystal | crystal | `#5fd8ff` / `#0a3a6a`, light `#bff4ff`, glass 0.5, gloss 0.95 |
| `crystal.filigree` | crystal | filigree | gold `#f4d273` on `#e7e2d6` |
| `crystal.psionic` | crystal | psionic | `#2f8cff` / `#06122a`, glow `#9fe8ff` |
| `crystal.ceramic` | crystal | ceramic | `#ebe8e1` / `#b3ad9f`, gloss 0.55, bevel 10 |
| `mech.gunmetal` | mech | machined | `#3b424b` / `#1b1f25`, gloss 0.5, sheen 0.5, edge wear 0.3 (as are the rows below) |
| `mech.graphite` | mech | cerakote | `#3e4044` / `#2a2c30`, gloss 0.16, sheen 0.25, edge wear 0.4 |
| `mech.blackout` | mech | machined | `#1f2226` / `#0b0c0e`, gloss 0.62, sheen 0.6, edge wear 0.15 |
| `mech.armor` | mech | armor | `#363c43` / `#262b31`, grooves `#07090b`, gloss 0.42, sheen 0.45 |
| `mech.forged` | mech | forged | `#363a40` / `#0d0e10`, gloss 0.6, sheen 0.5 |
| `mech.steel` | mech | brushed | `#9aa5b1` / `#4a535d`, gloss 0.7, sheen 0.5, bevel 1.5 (bright trim) |
| `mech.rubber` | mech | polymer | `#16181b`, gloss 0.08 (gaskets) |
| `glass.smoked` | glass | none | glass 1 over a near-black tint, a faint streak, a deep inner shadow |

### Shape language

The fields that give a race its silhouette and its surface detail. With all of them at 0 a
shape is a plain box.

| Field | Meaning | Default | Moves |
|---|---|---|---|
| `smooth` | Units over which the arc, the spines, the cut-outs and the segment gaps blend into the box (a smooth union, `smin`), instead of meeting at a crease. Zerg's "nothing straight". | 0 | yes |
| `wobble`, `wobbleScale`, `wobbleSpeed` | The whole edge pushed in and out by up to `wobble` units of `fbm(p / wobbleScale)`, drifting at `wobbleSpeed` per second. | 0, 40, 0 | wobble |
| `spines`, `spineSpacing`, `spineWidth`, `spineLean` | Spines along the top edge: one per `spineSpacing` units (none within half a spacing of the ends), each 55 to 100% of `spines` tall, `spineWidth` of its spacing wide at the base, leaning away from the middle by `spineLean` of its height. Each is an isosceles triangle joined to the box. | 0, 40, 0.5, 0 | spines |
| `bow` | An arc over the top edge `bow` units high, spanning the edge between its corners, so it meets them. Protoss crescents. | 0 | yes |
| `segments`, `segmentGap` | Gaps `segmentGap` units wide across the shape, splitting it into `segments` equal floating pieces. | 0, 0 | gap |
| `seams`, `seamWidth`, `seamStrength` | Grooves across the plate every `seams` units, inside the bevel, the lip facing the light lit. | 0, 1.5, 1 | |
| `bolts`, `boltInset`, `boltSpacing`, `boltColor` | Domed bolt heads `bolts` units in radius, `boltInset` in from the edge, every `boltSpacing` units round it, each lit as a sphere with a dark ring. | 0, 8, 100, `#9aa4ad` | |
| `boltStyle` | the heads' shape, `dome`, `hex` (a flat hexagonal head, its flats `bolts` from its centre, chamfered at its edge) or `socket` (the same head with a dark hex socket in it). In the HLSL, `uBolts.w`: 0, 1, 2. | `dome` | |
| `inlay`, `inlayDepth`, `inlayGlow`, `inlayColor` | A line of metal `inlay` units wide set into the surface `inlayDepth` units in from the edge, following it, with a faint glow. Protoss gold. | 0, 8, 0, `#f4d273` | |

The distance, in the order the shader builds it:

```
d = box with its corners (as before)
if bow:      hc = half width − larger top corner;  R = (hc² + bow²) / (2 bow)
             cap = max(|p − (0, −h + R − bow)| − R, p.y + h − 1, |p.x| − hc);  d = smin(d, cap, smooth)
if spines:   d = smin(d, the nearest spine's triangle, smooth)
cut-outs:    d = smax(d, −cut-out, smooth)
segments:    cell = width / segments; j = the nearest inner boundary;
             d = smax(d, gap / 2 − |t − j| × cell, smooth / 2)
wobble:      d += wobble × (fbm(p / wobbleScale + drift) − 0.47) × 2
smin(a, b, k) = lerp(b, a, h) − k h (1 − h), h = saturate(0.5 + 0.5 (b − a) / k); min(a, b) when k is 0
```

The drawn area grows by `spines × 1.1 + bow + wobble × 1.2` on every side, at most 72 units.

### Reveal

| Field | Meaning | Default | Moves |
|---|---|---|---|
| `reveal` | 0 to 1: how much of the shape has appeared. | 1 | yes |
| `revealStyle` | `peel`: opens from the middle outward with a ragged edge, a membrane splitting. `warp`: opens from the vertical centre line out to both sides, the edge shimmering, a psionic warp-in. | `peel` | |
| `revealColor` | The opening edge's light; its alpha is its strength. | clear | yes |

With `r` the distance measure (`peel`: `0.65 |p / half| + 0.5 (fbm(0.03 p + 3) − 0.47)`;
`warp`: `1.8 |x01 − 0.5| + 0.08 vnoise(40 y01, 6 t)`) and `th = reveal × 1.35 − 0.1`, a pixel
is shown by `1 − smoothstep(th − 0.06, th, r)`, and an edge `exp(−((r − th + 0.03) / 0.035)²)`
adds `revealColor` there. A reveal is how a style's intro opens a panel where Terran's slides it
in: the timelines move `reveal`, the layout stays.

## What the effects do

The engine must match these; the mock does exactly this per frame.

1. Draw the layer's content into a buffer the size of its box (images, rects, text), clipped
   by `meter`.
2. `tint`: multiply every pixel's colour by the tint, alpha unchanged.
3. `add`: add `addColor * add` to every pixel's colour, weighted by the pixel's alpha.
4. `scan`: build the band or lines (below), multiply it by the buffer's alpha, add it times
   `scanStrength`.
   - The band runs along direction `d = (cos a, sin a)`. Project the box's corners onto `d`:
     `s0` is the least, `s1` the greatest. The band's leading edge is at
     `pos = s0 - width + scan * (s1 - s0 + 2 * width)`, and it is `width` wide, brightest in
     its middle and clear at both sides (a linear ramp each way).
   - Lines are bars `0.4 * spacing` thick across `d`, every `spacing` units, shifted by
     `(scan * (s1 - s0)) mod spacing`.
5. `wipe`: with `s0`, `s1` as above for `wipeAngle`, the edge is at
   `e = s0 + wipe * (s1 - s0 + soft)`. A pixel at projection `s` keeps alpha 1 where
   `s <= e - soft`, 0 where `s >= e`, and a linear ramp between. 0 hides all, 1 shows all.
6. `glow`: blur the buffer's alpha with a blur of radius `glowRadius` (the mock uses a canvas
   shadow, which is a Gaussian with sigma about radius / 2), colour it `glowColor`, and add it
   under or over the layer with strength `min(glow, 1)`, again for each further whole step.
7. Place the buffer: translate to the box, add the track offsets (`x`, `y`) and the shake,
   rotate and scale about `origin`, then draw with alpha `opacity * flicker factor`, blending
   normally or by adding (`blend: add`, `ONE, ONE`).

**Shake** offsets by `(n1 * 2 - 1) * shake` and `(n2 * 2 - 1) * shake`, where `n1`, `n2` are
noise in 0 to 1 that changes every 33 ms. **Flicker** multiplies opacity by
`1 - flicker * (0.35 + 0.65 * n4)` whenever noise `n3`, changing every 45 ms, is below 0.45,
and by 1 otherwise. The noise is per panel or layer and need not match the mock's numbers,
only its rates and spread.

A panel applies its own transform, opacity, shake and flicker to everything in it, layers and
slots alike.

## Slots

A slot is a box the game draws its own content into, and the only part of a HUD that takes
clicks. These are the names:

| Slot | The game draws | From |
|---|---|---|
| `minimap` | the minimap, its camera box and its attack pings; clicking moves the camera | 1.17 |
| `selection` | the one unit's wireframe and figures, or the group's wireframes; with `grid`, SC2's selection in pages | 1.17 |
| `portrait` | the talking portrait | 1.17 |
| `card` | the command card's buttons, 1.17's commands; a key press sends at most one command | 1.17 |
| `resources` | the minerals, gas and supply line | 1.17 |
| `chat` | the chat and system lines | 1.17 |
| `alerts` | the message lines ("Not enough minerals.") | 1.17 |
| `menu` | the Game Menu button (F10) | 1.17 |
| `diplomacy` | the Diplomacy button, or View Players in a replay | 1.17 |
| `messaging` | the Messaging button | 1.17 |
| `terrain` | the Show / Hide Terrain in Minimap button (Tab) | 1.17 |
| `time`, `apm` | the game clock, the actions per minute | Hyperion (Remastered draws them too) |
| `groups` | the control groups; with `places`, each of the ten in its own place | Hyperion |
| `idle` | the idle worker button and its count | Hyperion (SC2's) |
| `army` | the army button: selects every unit that is not a worker, with its count | Hyperion (SC2's) |
| `play`, `slower`, `faster` | a replay's Play or Pause (P), Slower (D) and Faster (U) buttons (Replays) | 1.17 |
| `tv`, `follow` | a replay's Replay TV (F12) and Follow (F11) buttons | Hyperion |
| `players` | a replay's players' board: a row a player with an eye for that player's vision | Hyperion |
| `stats` | the players' stats a viewer calls up, as a side view or a popup by its `view` (The players' stats) | Hyperion |

**From** says which slots 1.17 has. A HUD meant to be 1.17's console holds only those and sets
`classic` (The file); the engine offers every slot to every HUD either way, and a HUD shows the
ones it wants. `SCHudPlayer.SLOT_ORIGIN` holds the same list.

**The `time` slot (Hyperion's addition: what the engine does).** The engine never fills it:
Hyperion's skins carry no game timer (the user, 2026-10-22), so the drawer has no case for `time`
and the slot stays an empty box the design may dress, and the hit list leaves it out with `chat`
and `alerts`, so it takes no press and the pointer goes through to the map
(`port_gpu_hud.cpp`, `SlotPrims` and the hits; `port_hudstate.cpp`, "time is the overlay's, not a
skin's"). The built-in designs leave the slot out of their layouts altogether
(`modules/hud.md`), and the game timer a player sees is Settings' own, drawn over the game by
`port_overlay.cpp`. The `time` and `time.seconds` bindings are filled as ever, so a design that
wants a clock sets one in a `text` layer.

**Size.** The game lays the selection, the portrait and the command card out at Remastered's own
slot sizes, in units: selection 553.5 × 211.5, portrait 135 × 126, card 294.75 × 256.5. In a
slot of another size that content is scaled uniformly to fit and centred, never stretched; the
rest of the slot is the HUD's to dress. The others (minimap, resources, time, apm, groups, idle,
chat, alerts) lay out to the slot's own box.

**Resources in cells.** With `places`, a list of `[x, y, w, h]` in the slot's units,
the resources slot puts minerals, gas and supply in a cell each, in that order, each with the
game's own icon for the race in play (minerals for everyone; gas and supply as the race has
them) and a placeholder when the art is missing. A cell's figure steps its type down until the
cell holds it, so 99999 minerals or 200/200 never spill. The third cell is the one a HUD lights,
since supply is the count that blocks a build. In the R8 console the clock is a `time` slot of
its own on the minimap's frame.

```json
{"id": "resources", "slot": "resources", "x": 28.39, "y": 12.86, "w": 239.46, "h": 37.5,
 "places": [[0, 0, 77.14, 37.5], [81.43, 0, 71.79, 37.5], [156.96, 0.54, 82.5, 36.43]]}
```

### 1.17's buttons

1.17 draws four buttons on its console, read from its own dialogs (`rez\minimap.bin`,
`rez\stat_f10.bin`), their handlers (`engine/src117/lang/minimap.cpp`, `statmisc.cpp`) and
their tooltips (`stat_txt.tbl`). Each is a slot: the game draws the button's face (its label, or
1.17's own button art on a classic console), its hover and pressed states and its tooltip, and
takes the press; the HUD draws whatever is behind it. Each is shown or hidden by 1.17's rules
(`minimap_btn_activate`), written here in the bindings:

| Slot | Shown when | Face, tooltip (`stat_txt.tbl`) | Key | 1.17's place on the 640 × 480 screen |
|---|---|---|---|---|
| `terrain` | `!game.multiplayer && !game.replay` | the terrain button; "Show Terrain in Minimap (Tab)" or "Hide …" (809, 810) | Tab | 64 × 24 at 3, 320: the minimap dialog (0, 315), its row at y 5 to 28 |
| `diplomacy` | `game.multiplayer \|\| game.replay` | "Diplomacy" (811); in a replay "View Players" (1539) | none | the same place as `terrain` |
| `messaging` | `game.multiplayer` | "Messaging" (812) | none | 52 × 24 at 76, 320 |
| `menu` | always | "Game Menu (F10)" (813) | F10 | 64 × 20 at 416, 388: the console's top edge over the portrait |

`terrain` and `diplomacy` are never shown together, so a HUD may give them one place, as 1.17
does. 1.17's buttons are 2.25 units a pixel at 1080 (Diplomacy 144 × 54); a HUD may make them
another size, and the face scales to the slot as the minimap's and the resources' do.

Pressing one does what 1.17 does: Terrain flips `minimap.terrain` (the minimap then draws its
units, resources and camera box on black), Diplomacy opens the alliance and shared vision dialog
(View Players, in a replay, the vision of each player), Messaging the dialog of who a chat line
goes to, and Game Menu the game menu. Diplomacy and Messaging have no key in 1.17: the buttons
are the only way to them. On a HUD whose selection has a `grid`, Tab moves the
subgroup, so there the terrain is the button's alone.

**A button the game hides leaves its slot empty**: the slot keeps its box but takes no press, so the button sharing its place gets it. A HUD that dresses a
button reads the same rule from the bindings, so its plate stays and only its light changes:

```json
{"id": "btn2", "shape": true, "look": "res.btn", "visible": "{game.multiplayer}", ...},
{"id": "cell2Br", "shape": true, "look": "res.bracket", "tint": "{(game.multiplayer) ? '#ffffff' : '#4a5864'}", ...},
{"id": "messaging", "slot": "messaging", "x": 133.07, "y": 81, "w": 83.57, "h": 42.43}
```

**`align`** on a button slot places the face's label in its box: `left`, `center` (default) or
`right`. The R9 menu is the whole of the portrait's chevron tab, so the press lands anywhere
on it, with the label at the right, past the chevrons:

```json
{"id": "menu", "slot": "menu", "x": 17.68, "y": 49.71, "w": 126.96, "h": 24.64, "align": "right"}
```

**Readouts take no clicks.** `resources`, `time` and `apm` let the pointer through to the map,
as `chat` and `alerts` do, so a plate in a screen's corner never blocks the map's edge scroll.

**A file not fit for multiplayer.** A file that places no `menu`, `diplomacy` or `messaging`
loads with a note saying so (Checking a file). What the engine then does is its call; the
proposal is that it draws the missing buttons itself, in 1.17's art at 1.17's size: Diplomacy
(or Terrain) and Messaging on the top edge of the file's `minimap` slot, at its left as 1.17 has
them, and the Game Menu on the top edge of its `portrait` slot; without those slots, in a row at
the screen's top left. The HUD's own look is never changed, only added to, and a mod maker who
wants another place sees the note.

### The selection's pages and the ten groups

Nothing a HUD draws may lie over a screen. In the file that means a slot's own panel draws
nothing after it but layers that are clear at rest (an energy edge), and no panel stacked
higher reaches into it. `terran-sc2wide.json` keeps that by building each panel as one plate
with its windows cut out of it (a path with a loop per window, even-odd), the lip and glass
under the slot, and by meeting its neighbour at a shared post instead of overlapping it. The
test reads every canvas above every slot at 16:9, 21:9 and 32:9 and fails on a covered point.

Hyperion's own games have SC2's selection: no 12-unit cap, the selection sorted into
subgroups by unit type (the strongest first, as SC2 orders them), shown in pages, Tab moving the
active subgroup, and control groups of any size. A HUD says how its slots lay that out; the game
fills them.

**`grid` on the selection slot** turns it into SC2's grid of wireframes:

```json
{"id": "selection", "slot": "selection", "inset": [39.38, 68.42, 44, 23.1],
 "grid": {"cell": [60, 54], "rows": 3, "cols": [8, 64], "tabs": "left", "tabW": 24}}
```

| Field | Meaning | Default |
|---|---|---|
| `cell` | One wireframe's pitch, `[w, h]` in the slot's units. SC2's is about 60 × 54 at 1080. | required |
| `rows` | Rows a page. | 3 |
| `cols` | `[fewest, most]` columns. The game shows as many as the slot's width holds after the tabs, between these, so a slot that widens with the screen shows more units a page. | `[8, 8]` |
| `tabs` | Where the page tabs go, `left`, `right` or `none`. A tab per page, shown when there is more than one. | `left` |
| `tabW` | The tabs' width, in units. | 24 |

A page holds `cols × rows` units. One unit selected shows its wireframe and figures instead
(name, kind, hit points, shields, kills). The active subgroup's wireframes are lit; the card and
the portrait show the active subgroup's first unit. The page shown is kept as the first unit it
shows, so two screens with different widths (or a resize) show the page holding the same units.
Clicking a wireframe selects that unit; Shift takes it out; Ctrl keeps only its type.

**The grid in Hyperion (Hyperion's addition).** The cells sit beside the tabs, from the left, or
up to the tabs when they are at the right, as StarCraft II's do; the design center's player
centres them in the slot. Hyperion's copies of the built-in layouts show at most 8 columns, 24
units a page as StarCraft II's (the user, 2026-10-23), and the flank layouts 5; each layout's
`note` says so. A cell is drawn as 1.17's panel draws a selected unit: its box by the unit's
state, the normal one in the player's selection colour and lighter for the active subgroup when
the selection holds more than one kind, and its wireframe in the colours of its hit points and
shields. A building, for which grpwire.grp has no frame, shows the console's large wireframe at
half its size. A cell's tooltip is the unit's name with 1.17's lines for a click on it. The page
shown is the HUD's own and changes nothing in the game.

The sample (`terran-sc2wide.json`) runs its window from post to post, so the grid is 17
columns at 16:9 (51 a page), 27 at 21:9 and 5120 × 2160 (81), and 49 at 32:9 (147): 150 units
are three pages at 16:9 and two at 21:9 and 32:9.

**`places` on the groups slot** gives each of the ten groups its own box, in the order 1 to 9
then 0, `[x, y, w, h]` in the slot's units, as the card's `places` do. The game draws in each
the group's number, its count and its first unit's icon, and marks the group last recalled.
Without `places` the groups lay out as a row, as before. Keys: a number recalls its group (twice
quickly centres the camera on it), Ctrl with a number sets it, Shift adds to it.

The engine needs, for these: the selection without its cap, sorted into subgroups; the page
shown and the active subgroup per player (view state, not game state: they never travel in a
replay); each group's count and its first unit's type; and the four events and the names below.

A slot takes its box, `visible`, `origin`, `opacity`, `scale`, `rotate`, the track offsets,
`wipe`, `flicker` and `shake`. Tint, add, glow and scan go on a layer over it: a `rect` with
`fill: "#000000"` and `blend: "add"` over a slot is a screen's glass, which a flash (`add`) or
a sweep (`scan`) lights without hiding what the game draws under it. A press inside a slot
also fires `panel.press` for its panel. Placing a slot twice draws it twice.

## Buttons (Hyperion's addition)

Any layer but a slot may have an `action`: a press of the left button on it then does what that
button does, through the same handler 1.17's console or its key calls (`port_hudinput.cpp`,
`PressAction`; `port_hudstate.cpp`). So a design draws its own button out of shapes, text and
images, where a slot leaves the face to the game.

```json
{"id": "idleBtn", "shape": true, "look": "btn", "action": "idle", "x": 28, "y": 12, "w": 44, "h": 32},
{"id": "idleCount", "text": "{idle.count}", "look": "figure", "x": 28, "y": 12, "w": 44, "h": 32}
```

| `action` | A press |
|---|---|
| `menu` | opens the game menu, as the `menu` slot's button does |
| `diplomacy` | opens the alliance and shared vision dialog, View Players in a replay |
| `messaging` | opens the dialog of who a chat line goes to |
| `terrain` | turns the minimap's terrain on or off |
| `idle` | selects the next idle worker and takes the view to it |
| `army` | selects every unit of the player's that fights |
| `group.0` to `group.9` | recalls the control group of that number, as its key does |

**An unknown name is a note, not a refusal**: the checker says "no action ...; the button is
greyed" and lists the names, and the design plays. The layer then draws at 40% of its opacity
and takes no press, so a design made for a later Hyperion still plays in this one.

**The action follows its slot.** A button draws nothing and takes no press when the slot its
action belongs to is left out in Settings (`<design>.hide.<slot>`), or when the game does not show
that button: `terrain`, `diplomacy` and `messaging` by 1.17's own rules for its console's
buttons, `idle` in a replay, `army` in a replay or under the classic rules (`HudActionSlot` in
`port_hudfile.cpp`, `ActionShown` in `port_gpu_hud.cpp`; `port_hudstate.cpp` reads the rules). The
`menu` action is always shown, as 1.17's button is, and a group's action follows the `groups`
slot. Settings counts a design as having a button when it places the slot or a layer with that
action, which is what its Buttons list offers to leave out.

An `action` on a slot layer is a note and is ignored, since a slot does what its slot does; an
`action` that is not a string is an error. A press on a layer with an action fires `panel.press`
for its panel with no slot, so a timeline for it leaves `slot` out. With a style that draws
tooltips, these buttons get the HUD's own tooltip (Tooltips).

## Bindings

A value written with `{...}` in it is read from the game. A whole value that is one binding
(`"{minerals}"`) gives that value as it is: a number, a true or false. Text mixed with
bindings (`"{supply.used}/{supply.max}"`) gives a string. Bindings work in any panel or layer
field except `id`, `image`, `slot` and `note`; `text` is always read this way.

The names, all read-only, all things the player can see:

| Name | Value |
|---|---|
| `minerals`, `gas` | in the bank |
| `supply.used`, `supply.max` | supply |
| `time` | the game clock as `m:ss` |
| `time.seconds` | the game clock in seconds |
| `apm` | actions per minute |
| `selection.count` | units selected |
| `unit.name`, `unit.hp`, `unit.hpMax`, `unit.shields`, `unit.shieldsMax`, `unit.energy`, `unit.energyMax`, `unit.kills` | the first selected unit; 0 or empty with none |
| `idle.count` | idle workers |
| `replay.paused` | the replay is paused (Replays) |
| `replay.speed` | the replay's speed as text: `Slowest` to `Fastest`, then `Fastest ×2` to `Fastest ×16` |
| `replay.step` | the same as a step, 0 (Slowest) to 6 (Fastest) and 7 to 10 (×2 to ×16) |
| `replay.progress` | how much of the replay has played, 0 to 1 |
| `replay.elapsed`, `replay.length` | the time played and the replay's length, as `m:ss` |
| `replay.tv`, `replay.follow` | Replay TV, or Follow, is steering the camera |
| `players.count` | the players in the game |
| `stats.popup`, `stats.side` | the stats popup is open (F1), the side view is shown (Shift+F1); always false in a game the player plays |
| `stats.page` | the popup's page: `overview`, `income`, `army` or `lost` |
| `replay.live` | the game is watched as it is played: its length is still open, so `replay.progress` is 0 and `replay.length` empty |
| `replay.seeking` | the viewer is dragging the replay's position |
| `teams.count` | the teams in the game |
| `players.N.<field>` | player N's figures, by seat from 0 to 31, the fields of A design of your own |
| `top.<figure>` | the most any player has of a figure, for a bar's scale |
| `army.count` | units that are not workers, as the `army` button counts them |
| `game.multiplayer` | a multiplayer game with two or more players in it (1.17's `gbMultiPlayerMode && get_num_active_players() > 1`) |
| `game.observer` | a game watched as it is played: a LAN room's match, a game of the AI lab watched live, or a skirmish of computers the player watches. Hyperion plays each as a replay, so `game.replay` is true with it. 1.17 has no observer seat: its `IsTVBGame()`, which this row once named, is the Top vs Bottom game type |
| `game.replay` | a replay (1.17's `gPlayback`) |
| `minimap.terrain` | the minimap shows the terrain: true until the Terrain button turns it off |
| `race` | `terran`, `protoss` or `zerg` |
| `player.colour` | the player's colour, `#rrggbb` |
| `selection.page`, `selection.pages` | the selection page shown, from 1, and how many there are |
| `selection.subgroup`, `selection.subgroups` | the active subgroup's unit name, and how many unit types are selected |
| `group.0.count` to `group.9.count` | the units in each control group |
| `group.last` | the group last recalled, 0 to 9, or -1 |

A name's parts may be numbers (`group.3.count`); every name is still checked against this list.

The language is numbers, `'strings'` (or `"strings"`), the names, `+ - * /` (`+` joins
strings), `< <= > >= == !=`, `a ? b : c`, brackets, unary minus, and three functions,
`min(...)`, `max(...)` and `round(x)`. Division by zero gives 0. Nothing else: no loops, no
assignments, no other names. Numbers print with at most two decimals; true and false print as
`yes` and `no`.

```
"{supply.used >= supply.max ? '#ff6a5a' : '#d7f3ff'}"     the supply turns red when blocked
"{round(unit.hp / max(1, unit.hpMax) * 100)}%"             hit points as a percentage
"meter": "{unit.hp / max(1, unit.hpMax)}"                  a bar
```

## Timelines

```json
{"id": "attack", "on": "alert.attack", "end": "release", "tracks": [
  {"target": "minimap.alarm", "prop": "glow", "keys": [[0, 0], [150, 1.6, "out"], [450, 0.2]]},
  {"target": "minimap", "prop": "shake", "keys": [[0, 5], [400, 0, "out"]]}
]}
```

| Field | Meaning |
|---|---|
| `id` | Unique name. |
| `on` | The event that starts it (Events). |
| `panel` | For `panel.hover`, `panel.leave` and `panel.press`: which panel's. |
| `slot` | For `panel.press`: only presses in this slot of the panel, so a panel holding the card and the Game Menu answers each its own way. Without it, a press in any of the panel's slots. |
| `group` | For `group.recall` and `group.set`: which group's, 0 to 9. Without it the timeline runs for every group. |
| `view` | For `stats.show` and `stats.hide`: `side` or `popup`, which view's. Without it the timeline runs for both. |
| `end` | `release` (default): when it ends, its values let go. `hold`: it stays on its last frame until it is fired again or the game starts. `loop`: it starts over. |
| `stops` | Ids of timelines this one ends when it starts, held ones too: `console.in` stops a held `console.out`, which would otherwise come back when `console.in` lets go. |
| `tracks` | Each moves one property of one panel (`"deck"`) or layer (`"deck.trim"`). A track's `delay` (ms, default 0) starts its keys that much later, which is how a stagger is written: the same keys on each wing, 60 ms apart. |
| `cues` | Sounds: `[[ms, "<audio asset>", volume], ...]`. Each plays once when the timeline passes its time, at `volume` 0 to 1 (default 1). A held or scrubbed frame plays nothing; a loop plays its cues each time round. |

**Keys** are `[ms, value, easing]` in time order. Before the first key a track gives the first
key's value; after the last, the last key's. Between two keys the later key's easing shapes the
way there. A timeline lasts until its latest key or cue, delays included.

**Easings** (u runs 0 to 1 between two keys):

| Easing | f(u) |
|---|---|
| `linear` (default) | u |
| `in` | u³ |
| `out` | 1 − (1 − u)³ |
| `inOut` | 4u³ below one half, else 1 − (−2u + 2)³ / 2 |
| `back` | 1 + 2.70158 (u − 1)³ + 1.70158 (u − 1)²: overshoots and settles |
| `step` | 0 until u reaches 1: holds the earlier value, then jumps |
| `spring` | (1 − e^(−5u) cos 11u) / (1 − e^(−5) cos 11): a damped spring that overshoots by about a quarter at u ≈ 0.29, swings back, and lands exactly on 1 |

**Motion, as the samples use it.** Panels arrive on `spring` so they overshoot and settle;
pieces that belong together are staggered by `delay` rather than by copying keys; and a
landing sets off something smaller after it (secondary motion): the deck's energy edge flares
as it locks, the resource icons pop in one after another once their plate has landed.

Numbers blend; colours blend channel by channel, alpha too; anything else steps at the key.

**What a track's value means.** `x` and `y` move the panel or layer from where the file puts
it, right and down, in units, whatever its anchor: `[[0, 260], [550, 0, "out"]]` on a
bottom panel rises it from below the screen. Every other property replaces the file's value
while the timeline runs.

**When two timelines move the same property**, the one fired later wins. Firing a timeline that
is running starts it again from 0. `game.start` stops every running and held timeline first,
then starts its own, so a new game is a fresh HUD. A held hover and its held leave pair up
this way: whichever fired last is what shows.

## Events

| Event | The game fires it when |
|---|---|
| `game.start` | the game starts (the mock: on load and on every HUD loaded) |
| `game.victory`, `game.defeat` | the game ends |
| `select.change` | the selection changes |
| `alert.attack` | your units or buildings are attacked, at most once in ten seconds (the minimap's own ping marks where) |
| `alert.minerals`, `alert.gas`, `alert.supply` | an order fails for want of minerals, gas or supply |
| `research.done`, `unit.done` | research or an upgrade finishes; a unit is made |
| `panel.hover`, `panel.leave` | the pointer enters or leaves a panel with `hover: true` |
| `panel.press` | a press lands in one of a panel's slots; `info.slot` is the slot's name |
| `console.in`, `console.out` | the console comes back or goes (a cinematic, a menu over the game); SC2 plays its console's Birth and Death models then |
| `selection.page` | the selection grid shows another page (a tab, or Tab moving to a subgroup on another page); `info.page` is the page, from 1 |
| `selection.subgroup` | Tab moves the active subgroup; `info.subgroup` is its unit name |
| `group.recall` | a control group is recalled; `info.group` is 0 to 9 |
| `group.set` | a control group is set or added to; `info.group` is 0 to 9 |
| `tip.show`, `tip.hide` | the style's tooltip shows (`info.panel` is the button's panel) or goes (Tooltips) |
| `replay.pause`, `replay.resume` | a replay is paused, or plays on (Replays) |
| `replay.speed` | a replay's speed changes; `info.step` and `info.speed` are the new one |
| `replay.vision` | a player's vision is shown or hidden on the board; `info.player` and `info.on` |
| `stats.show`, `stats.hide` | the stats popup or side view is shown or hidden; `info.view` is `popup` or `side` (The players' stats) |

`panel.leave` is an addition to the brief's list: without it a hover could only end by
timing out.

## Styles

A style file dresses a layout. It never places or names anything: where things are and when
they move is the layout's; what they are made of, and how they move and sound, is the style's.

```json
{
  "kind": "hud-style",
  "version": 1,
  "id": "zerg-organic",
  "name": "Zerg organic",
  "race": "zerg",
  "note": "free text",
  "palette":  { "accent": "#ff9a2a", "alert": "#ff3a2a", "titleText": "HIVE CLUSTER", ... },
  "easings":  { "arrive": "inOut" },
  "sounds":   { "boot": "peel", "alert": "chitter", "scan": null, ... },
  "assets":   { "peel": {"audio": "zerg-peel.ogg"}, "gasIcon": {"art": "hd:game/icons#1", "crop": [0, 0, 38, 28]} },
  "shape":    { "cornerStyle": "round", "cornerScale": 1.8, "wobble": 2.2, "smooth": 10, "revealStyle": "peel" },
  "looks":    { "frame": { "material": "organic.chitin", "spines": 30, "spineSpacing": 58 }, "screen": { ... }, ... },
  "timelines": { "attack": { "tracks": [ ... ], "drop": ["minimap.shake"], "cues": [ ... ] }, "breath": { "on": "game.start", "end": "loop", ... } }
}
```

| Field | Meaning |
|---|---|
| `kind`, `version` | `"hud-style"` and 1. |
| `id`, `name` | The file's id (its name in `huds/styles/`) and the player's name for it. |
| `race` | `terran`, `zerg`, `protoss` or `any`: the race it is made for. |
| `palette` | Named values the layout refers to as `"$name"`: colours mostly, and any other value, text included (`$titleText`). |
| `easings` | Named easings for keys: a key `[ms, value, "$arrive"]` takes the style's. |
| `sounds` | Named cues: a cue `[ms, "$alert", 0.8]` plays the style's audio asset of that name; `null` makes it silent. |
| `assets` | Assets added to the layout's, a style's of the same name winning: its sounds, and art it swaps in (each race's gas and supply icons). |
| `shape` | What every shape layer starts from: fields from Shapes, plus `cornerScale`, which multiplies every shape's `corners`. |
| `looks` | One set of layer fields per role. A layer with `"look": "frame"` takes the style's `frame`. |
| `timelines` | Changes to the layout's timelines, by id, and timelines of its own. |
| `lights` | Optional: the palette names that are the console's light, the lamp first. A player's light colour moves them; see below. |
| `tooltip` | Optional: the tooltip's sizes. With the `tip` looks, the style draws the tooltip itself (Tooltips). |
| `musicPlayer` | Optional: the music player addon's skin while this style is worn: its colours, the looks its frames and buttons wear, its type and its sounds (2026-10-24, The music player). |

**How a layer is dressed**, from the bottom up, each over the last:

1. the style's `shape` (shape layers only);
2. the material preset, the layer's `material` or else its look's;
3. the look;
4. the layer's own fields, which always win.

Then every `"$name"` in a field is replaced from the palette. A whole value that is one name
(`"$accent"`) takes the palette's value as it is; inside a longer string, a binding, each
`$name` is replaced by its text: `"{supply.used >= supply.max ? '$short' : '$figure'}"`.

**How timelines change.** For each of the style's timelines whose id the layout has: a track
with the same `target` and `prop` replaces the layout's; other tracks join it; `drop` lists
`"target.prop"` tracks to remove; `cues`, if given, replace the cues; `end`, if given, replaces
the end. A timeline the layout does not have is added when it says `on`, which is how Zerg's
breathing loop comes in. Key easings and cues naming `$` are then looked up in `easings` and
`sounds`. This is how one shared set of timelines plays three ways: Terran slides on servos,
Zerg peels its membranes open and beats on alerts, Protoss warps in and pulses calmly.

**Choosing the style.** The game reads the race in play (`race`) and wears the style made for
it; the player can choose another in the options, and that choice wins. The mock does both:
**By race** on `hud.html` follows the race, and the style buttons override it
(`?style=zerg-organic`). Changing style mid-game restyles the HUD in place; a running timeline
keeps running.

**The player's light colour.** A style with `lights` lets the player pick the colour
of its light in Settings, as SC2 lets a player pick a console's colour. Each palette name in
`lights` must be a `#rrggbb` or `#rrggbbaa` colour (the checker says so if not). The first, the
lamp, becomes the colour picked. Each other light keeps its place beside the lamp: lighter or
darker by the same share of the way to white or black, as much more or less saturated, the hue
the colour's, and its own alpha, so a lamp's hot core stays near white and a dim cell's tint stays
dim. What `lights` does not list (the metal, the text, and the warnings: `alert`, `warn`, `ok`,
`short`, `defeat`, hazard paint) stays as the style has it. `SCHudPlayer.relight(style, colour)`
returns the style so changed; the game does the same before wearing it, and keeps the colour
with the player's profile. The mech materials list 17 names: `lamp`, `lampHot`, `lampGlow`,
`accent`, `info`, `victory`, `lcd`, `lcdOff`, and the colours their looks once held as values,
now palette names: `bracket`, `bracketGlow`, `wellLine`, `cellLit`, `cellLitDeep`, `cellGlow`,
`cellLitLine`, `btnGlow`, `pipGlow`. Worn, they draw exactly as before. The mock keeps the
colour in the URL: `hud.html?light=amber` (a name: ice, amber, red, green, violet, white) or
`?light=3366ff`.

**A layout to wear a style** names looks and `$` values and does not draw alone: loaded with no
style, every `$` it names is an error ("it is a layout to wear a style, and needs one").
`modern-drawn.json` names these looks: `frame`, `wing`, `cap`, `plate`, `bar`, `band`,
`screen`, `trim`, `lamp`, `alarm` (shapes), `readout`, `label`, `title`, `figure`, `banner`,
`victory`, `defeat` (text) and `glint` (rects); these palette names: `accent`, `alert`,
`alertRim`, `ok`, `info`, `victory`, `victoryRim`, `victoryTrim`, `victoryGlow`, `trim`,
`trimGlow`, `rim`, `short`, `text`, `figure`, `defeatTint`, `defeatTrim`, `defeatGlow`, `white`,
`bootText`, `winText`, `loseText`, `titleText`; the easing `arrive`; and the sounds `boot`,
`deploy`, `scan`, `alert`, `denied`, `confirm`. A style for it gives all of them.

**The four styles.**

| Style | Made of | Moves and sounds |
|---|---|---|
| `terran-glass` | the first glass look: frosted glass, a cool energy edge | springs and staggers; hum, servos, a scan sweep |
| `terran-industrial` | gunmetal bolted round its edges and cut by seams, a painted-plate cap, dark sunken screens, chamfers | the same servos and power-up |
| `zerg-organic` | chitin plates with spines along the top, a cap of glowing sacs, a flesh resource plate, a membrane alert band; round corners 1.8 times larger, smooth unions, a slow wobble everywhere | membranes peel open (`reveal`, `peel`), a breathing loop, alerts beat like a heart (lub-dub on the alarm and the minimap); a membrane tearing, a heartbeat, a hive chitter |
| `protoss-khala` | white ceramic arched over the top and split into three floating segments, gold inlay, a gold cap, khaydarin lamps, a psionic alert band, mirrored patterns | a psionic warp-in (`reveal`, `warp`), a calm pulse on alerts with no shake; a warp, a calm chime, crystalline tones |

**Making new looks fast.** `style.html` is the style editor: the drawn Modern layout in the
style being edited, live; every field of every look, grouped (Material, Outline, Detail,
Light, Glass, Edge and glow), each showing whether its value is the look's own, its material's,
every shape's or the default; the palette, easings and sounds; undo; and export. Sweeps make 6
to 12 variants of the style on screen from a seed, varying colour (a hue turn and saturation),
materials (within a family, or any above an amount of 0.7), shape (corner scale, the shape
language's numbers, and borrowing another race's feature) and light; "use this" takes one,
"sweep round it" sweeps again at half the amount. `hudstyle.js` (`window.SCHudStyle`) holds the
field list, `effective()` and `sweep()`. `sc2-boards.html` sets StarCraft II's console for each
race beside the style, for colour and material only; it is local, since the pictures are SC2's.

## Tooltips

The user (2026-10-23): "a new way for the tooltip to render. That it matches the skin
more, along with a font type". A style with a `tooltip` block draws the tooltip of the button
under the pointer itself, in place of 1.17's bubble. Worn on a layout, the style adds a panel
called `tooltip`, drawn over every other (`z` 100). Its parts are layers wearing the style's
`tip` looks like any layer, so the tooltip is made of the console's own metal, glass, lamps and
type. The game fills it with what 1.17 would show and puts it beside the button. A style without
a block leaves the game's own bubble, as before. `hudplayer.js` does all of this in
`p.tip(content, button)`; `tooltips.html` shows it.

**What the game passes**, as separate fields rather than one string:

| Field | Meaning |
|---|---|
| `title` | The button's name: "Train Marine", "Game Menu", "Hide Terrain in Minimap". |
| `hotkey` | Its key as the player reads it: "M", "F10", "Tab". Without one, there is no keycap. |
| `mark` | The index in `title` of the letter 1.17 marks as the hotkey, or none. |
| `costs` | Any of `minerals`, `gas`, `supply`, `energy`, numbers or text, drawn in that order. |
| `short` | Which of those the player lacks (`{"gas": true}`); their figures take the block's `short` colour. |
| `lines` | Up to four requirement lines ("Requires: Barracks"), each wrapped. |
| the button | Its box in units, and the panel it is in. |

**The block.** Sizes are in units and every field is optional. A colour field takes a palette
name (`"$tipAccent"`).

| Field | Meaning | Default |
|---|---|---|
| `pad` | `[left, top, right, bottom]` inside the pane, or one number for all four. | `[12, 9, 12, 10]` |
| `frame` | How much of the plate shows round the pane. | 4 |
| `gap` | Between rows. | 6 |
| `minWidth`, `maxWidth` | The body's width. Requirement lines wrap to fit `maxWidth`. | 0, 360 |
| `corner`, `cornerStyle` | The plate's corners: `chamfer`, `round` or `notch`. The pane's are `corner - 0.6 * frame`, in the same style. | 6, `chamfer` |
| `tab`, `tabWidth` | The tab toward the button: its depth and its width at the base. Its sides run at 45 degrees, so a narrow tab comes to a point. 0 is none. | 6, 18 |
| `strip`, `stripSize`, `stripInset` | A lamp strip on the pane: `top` (along it, 1.5 units in), `left` (down it) or `none`; its thickness; how far it stops short of the pane's ends. | `top`, 2, 10 |
| `rule` | A hairline between the title row and the rest. | `true` |
| `keyPad`, `keyGap` | The keycap's padding `[across, down]`; the gap between the title and the keycap. | `[6, 2]`, 14 |
| `icon`, `iconGap`, `costGap` | A cost icon's size; from an icon to its figure; from one cost to the next. | 15, 5, 14 |
| `mark`, `markLine` | The marked letter's colour; the thickness of the line under it (0 is none). | `#7fdcff`, 1.5 |
| `short` | The colour of a cost the player lacks. | `#ff7a6a` |
| `icons` | `{"minerals": "<image asset>", ...}`: draw that cost with the HUD's own image instead of the built-in shape. | none |
| `offset`, `margin` | From the panel's edge to the tab's tip; how far inside the screen it stays. | 6, 8 |

**The parts.** Each is a layer of the `tooltip` panel, listed in draw order. Timelines target
them as `tooltip.<id>`.

| Layer | Kind | Look |
|---|---|---|
| `plate` | shape; its outline is a path, the body and the tab together | `tip` |
| `pane` | shape | `tip.pane` |
| `strip` | shape | `tip.strip` |
| `rule` | rect | `tip.rule` |
| `title`, `mark`, `title2` | text: the title cut round the marked letter; `mark` is in the block's `mark` colour | `tip.title` |
| `markLine` | rect, filled in the `mark` colour | none |
| `key`, `keyText` | shape, and text centred on it | `tip.key`, `tip.keyText` |
| `iconMinerals`, `costMinerals` (and `Gas`, `Supply`, `Energy`) | shape (or image, by `icons`), and text | `tip.icon.minerals` ..., `tip.cost` |
| `line1` to `line4` | text, wrapped, from the top | `tip.line` |

A look the style does not give is the player's own plain one (`SCHudPlayer.TIP_LOOKS`: a dark
plate, Chakra Petch). The built-in cost icons are paths in a 16-unit square
(`SCHudPlayer.TIP_ICONS`): a crystal, a canister, a depot and a bolt. A part with nothing to show
is hidden.

**Laying it out.** The sizes come from the dressed looks: `title.size`, `keyText.size` and so on.
`f` is `frame` and `p` is `pad`. A text's width is the face's advance with `tracking` after each
letter; wrapping breaks between words.

```
title row   h = max(title.size * title.lineHeight, keycap h)
keycap      h = keyText.size + 2 * keyPad[1];  w = max(h, width(hotkey) + 2 * keyPad[0])
costs row   h = max(icon, cost.size * 1.2);  w = sum of (icon + iconGap + width(figure)), plus costGap between
lines       each wrapped at room = maxWidth - 2f - p.left - p.right;  h = its lines * line.size * line.lineHeight
inner       = ceil(max(width(title) + keyGap + keycap w, costs w, the widest wrapped line, minWidth - 2f - p.left - p.right))
body w      = inner + p.left + p.right + 2f
body h      = 2f + p.top + title row
              + (costs or lines: gap, and with rule: 1 + gap)
              + (costs: costs row, and with lines after them: gap)
              + (lines: each line's h, round(gap / 2) between them)
              + p.bottom, rounded up
```

Everything inside starts `f + p.left` from the body's left. The title is at the left of its row
and the keycap at the row's right end, both centred on the row. The marked letter starts at the
width of the title before it, and its line sits `0.46 * title.size` below the row's centre. The
costs are centred on their row, and the lines are set from the top.

**Placing it.** The side comes from the button's panel's `tip`, or else from its anchor:

- a panel on the bottom edge has it above;
- a panel on the top edge has it below;
- a panel anchored at the middle has it beside, toward the screen's centre.

A column gives its `tip` outright (the Mech columns say `right` and `left`); `none` gives that
panel's buttons no tooltip.

Above: the body's bottom is `offset + tab` above the panel's top, centred on the button across
and kept `margin` inside the screen. Below, left and right follow the same rule. A side with no
room gives way to the opposite one. The tab sits at the button's centre along that edge, kept
`corner + tabWidth / 2 + 2` clear of the corners. `SCHudPlayer.tipLayout`, `tipPlace` and
`tipPath` are the maths, to check the engine's against.

**Showing it.** When the pointer has rested on a button (the game's own delay, as now), the game
shows the tooltip and fires `tip.show`, with `info.panel` the button's panel. Moving to another
button while it shows changes it in place and fires nothing. When the pointer leaves, the game
fires `tip.hide` and hides the panel once the longest `tip.hide` timeline has run, or at once if
there is none. Showing it again ends any `tip.hide` still running.

Reactions are light only (the user's rule: no shake or bounce). The samples fade in over 110 ms
while the strip wipes along and the keycap brightens once. They fade out over 90 ms, with
`end: hold` so the tooltip stays out until it is hidden.

**Its colour.** The lit parts take `$tipAccent`, with `tipAccentHot` for the strip's bright end and
`tipAccentGlow` for its glow. The Mech styles set these to their lamp and list them in `lights`,
so the player's light colour moves them; R9's style sets them to its cyan and has no `lights`.

When the player picks a Selection boxes colour other than Original, the game may set `tipAccent`
to it, `tipAccentHot` to 65% of the way to white and `tipAccentGlow` to that colour at alpha
`70`. The tooltip then matches the boxes. `tooltips.html`'s Accent does exactly this.

**The font.** Chakra Petch (Cadson Demak, 2018) is under the SIL Open Font License 1.1, so
Hyperion can ship it. The files are `fonts/chakra-petch/ChakraPetch-Regular.ttf`, `-Medium`,
`-SemiBold` and `-Bold` (weights 400, 500, 600 and 700), with `OFL.txt` beside them, taken from
Google Fonts' repository (`ofl/chakrapetch`).

Its cut corners are the Mech's chamfers, and it stays legible at 12 to 16 units. A text names it
with `"font": "chakra-petch"` and a `weight` (Layers, Text). `saira` and `michroma` name the faces
Hyperion already ships. `tooltips.html` also tries Oxanium and Barlow Semi Condensed, both OFL, and
Saira, today's fallback.

**The samples.**

- **Mech's three materials** (`mechStyle` in `kits/mech-parts.mjs`): the hull's metal round a
  dark cell pane, a lamp strip along the top, a lit keycap, an 8-unit chamfer and a 6-unit frame.
- **R9** (in `terran-r8`): the console's gunmetal with its bright rim, a 10-unit chamfer, a pane
  edged in its cyan, and a strip down the left.

**Checks.** The style does not load for any of these:

- a `tooltip` that is not an object;
- a size that is not a number of 0 or more;
- a `pad` or `keyPad` of the wrong length;
- a `strip` other than `top`, `left` or `none`;
- a `cornerStyle` other than the three;
- a `rule` that is not true or false;
- an `icons` entry naming no image asset.

These load with a note: a field that means nothing, an `icons` key that is not a cost, a `tip.*`
look that no part wears. A layout with a panel called `tooltip`, worn with a style that has a
tooltip, is an error.

### What Hyperion does with the tooltip (Hyperion's addition)

**Which buttons give one.** Three kinds (`modules/hud.md`):

- **the command card's buttons** (`StatCmdHudTip` in `statcmd.cpp`): the button's name from
  `stat_txt.tbl` with the letter 1.17 marks, its key (`Esc` for 0x1B), the lines 1.17 writes
  under it or, for a grey button, the reason it is grey, and the four costs from
  `sgwStatCmdTooltipData_g`, each short when the player lacks it;
- **the console's other bubbles** (`ctxhelp_show` in `CtxtHelp.cpp`): the first line of the text
  is the name, a short `(key)` at its end is the key, the rest are the lines under it;
- **the HUD's own buttons** (`OwnTip` in `port_hudinput.cpp`): the idle worker, the army, each
  control group and any layer with an `action` (Buttons). 1.17 has no bubble for these, so they
  have a tooltip only while the style draws one. Their names and lines are the engine's, and
  their keys are the player's own from Settings' Hotkeys.

`PortHudTipShow` finds the button's box on the screen, the card's own place or the least part of a
slot holding the point, and takes its corners through the slot's transform, so the tooltip sits
by the button where the design drew it, whatever the design did with the slot. It is placed
against the button's box, above it as 1.17's and Remastered's tooltips are, where the design
center's player places it against the button's panel (the user, 2026-10-23). It shows and goes
at once, as the original's does: Hyperion's copies of the styles leave out the `tip.show` and
`tip.hide` timelines, so the panel is hidden at once, as the format says when there is none.

**The cost icons.** When the style's `tooltip` names no `icons` of its own, the engine adds 1.17's:
assets `tip.art.minerals` and, for each race, `tip.art.gasZ`, `gasT`, `gasP`, the same for
`supply` and `energy`, of `sd:game/icons#n`, with a layer for each (`iconMinerals`, `iconGasT`
and so on). A cost shows the race's icon it is in: gas the console's race, supply and energy the
race whose they are.

**The player's colour.** Settings' "Selection boxes and tooltips" sets the palette before the
style is worn (`port_hudfile.cpp`, `HudLoad`): the colour picked becomes `tipAccent`,
`tipAccentHot` is 65% of the way from it to white, and `tipAccentGlow` is it at alpha `70`.
Original leaves the style's own three; Light takes the light colour the player picked for the
design.

**Turning it off.** Settings' "The design's tooltips" check (the `tooltips` key) gives back 1.17's
bubble: `PortHudTip` returns false, and the game draws the bubble as it does with no design
(`port_hud.cpp`). A style with no `tooltip` block does the same. The HUD's own buttons then have
no tooltip, since 1.17 has no bubble for them.

## The music player

Hyperion has no music player at present: it reads and checks this block, and nothing wears it
until a music add-on does (`modules/addons.md`).

2026-10-24. The music player addon (Winamp 2.91's three
windows at Winamp's geometry, Ctrl+F9 in a game) is drawn in the skin of the HUD style the player
wears. Everything but the skin stays as drawn: the windows, their sizes, the controls and what
they do. The skin is a `musicPlayer` block in the style file, beside `tooltip`, rather than a
file of its own: its colours name the same palette, so a light colour (`lights`, `relight`) moves
the player with the console; a `.hud` package carries it with the style; and there is no second
file to keep matched to the first. A style without the block, Classic's and Remastered's among
them, leaves the player in its own skin. `hud.html` shows it: the player opens with
Ctrl+F9, the Music player buttons or `?music=1` (`?music=play` also plays) and changes skin with the
HUD, its material and its light.

```json
"musicPlayer": {
  "version": 1,
  "note": "free text",
  "colors":    { "segment": "$lcd", "segmentOff": "$wellLine", "lcd": "#030405", "fill": "$accent", ... },
  "materials": { "frame": { "look": "hull", "set": { "panelLines": 0, "corners": 7, "cornerStyle": "chamfer" } },
                 "title": "bezel", "button": "btn", "lit": "cell.lit", "menu": "tip",
                 "shade": { "look": "bezel", "set": { "corners": 6 } } },
  "fonts":     { "lcd": "readout", "text": "tip.line", "title": "stencil" },
  "sounds":    { "open": "$deploy", "close": "$deploy", "shade": "$scan", "denied": "$denied" }
}
```

Every field but `version` is optional, and so is every name in each of them: what a block leaves
out is the player's own.

**`colors`.** Each value is a colour, `#rrggbb` or `#rrggbbaa`, or a palette name (`"$lcd"`),
resolved against the style as worn, so after the light. Give a colour that is the console's light
by its palette name and it follows the light; give it as hex and it stays. The right-hand column
is the token the mock's stylesheet draws it with (`musicplayer.css`).

| Name | What it colours | Token |
|---|---|---|
| `faceTop`, `faceBottom` | A window's face, top to bottom, where no material frames it. | `--mp-face-top`, `--mp-face-bot` |
| `rim`, `rimOff`, `rimGlow`, `bevel` | A window's rim with the focus and without, the glow round it, the inner bevel. `rimOff` defaults to `rim` at 55% toward black. | `--mp-rim`, `--mp-rim-off`, `--mp-rim-glow`, `--mp-bevel` |
| `titleGlow`, `titleInk`, `titleInkOff`, `titleRule` | The title bar with the focus: its glow, its name; its name without the focus; the two fine rules either side. | `--mp-title-glow`, `--mp-title-ink`, `--mp-title-ink-off`, `--mp-rule-lit` |
| `mark`, `glow` | The mark in the title bar and the logo; the glow on lit ink, a hovered button, the playing row. | `--mp-mark`, `--mp-glow` |
| `lcd`, `lcdRim`, `lcdGlow`, `lcdEdge` | The LCD's background (the time, the title, kbps and kHz), its rim, its inner glow, the light along its lower edge. | `--mp-lcd`, `--mp-lcd-rim`, `--mp-lcd-glow`, `--mp-lcd-edge` |
| `well` | The other wells: the playlist, the equalizer's graph, the playlist's time. Defaults to `lcd`. | `--mp-well` |
| `segment`, `segmentOff`, `segmentGlow` | A lit segment and the LCD's figures; an unlit segment; their glow. | `--mp-seg`, `--mp-seg-off`, `--mp-seg-glow` |
| `led`, `ledOff` | A toggle's LED on and off; the clutterbar letters and stereo when lit. | `--mp-led`, `--mp-led-off` |
| `ink`, `inkHi`, `inkDim`, `inkOff` | Text, bright text, labels and figures beside the wells, what cannot be used. | `--mp-ink`, `--mp-ink-hi`, `--mp-ink-dim`, `--mp-off` |
| `rule` | The fine lines: under the title bar, round a slider's track, above the playlist's buttons. | `--mp-rule` |
| `buttonTop`, `buttonBottom`, `buttonRim`, `buttonRimHi`, `buttonLip`, `buttonLipHi`, `buttonInk` | A button where no material frames it: its face top to bottom, its rim and hovered rim, the lip along its foot and hovered, its legend. | `--mp-btn-top`, `--mp-btn-bot`, `--mp-btn-rim`, `--mp-btn-rim-hi`, `--mp-btn-lip`, `--mp-btn-lip-hi`, `--mp-btn-ink` |
| `transportInk`, `accent` | The transport's glyphs; the source button and the volume fill's far end. | `--mp-tr-ink`, `--mp-accent` |
| `track`, `trackGlow`, `fill`, `fillGlow` | A slider's track and its glow; its fill and the fill's glow. | `--mp-track`, `--mp-track-glow`, `--mp-fill`, `--mp-fill-glow` |
| `knob`, `knobRim`, `knobHi`, `knobRimHi`, `knobGlow` | A slider's knob, its sides, both when hovered, its glow. | `--mp-knob`, `--mp-knob-rim`, `--mp-knob-hi`, `--mp-knob-rim-hi`, `--mp-knob-glow` |
| `visLow`, `visHigh`, `visPeak` | The visualizer's bars bottom to top and their peaks; `visHigh` is also the equalizer's curve. | `--mp-vis-lo`, `--mp-vis-hi`, `--mp-vis-peak` |
| `select`, `selectLine`, `current`, `hover`, `scroll`, `art` | A selected row and its edge; the playing row's text; a hovered row; the scrollbar; an app's tile under the computer's sessions. | `--mp-sel`, `--mp-sel-line`, `--mp-cur`, `--mp-hover`, `--mp-scroll`, `--mp-art` |
| `menu`, `menuRim`, `menuInk`, `note`, `noteRim` | A menu where no material frames it, its rim and text; the equalizer's note while another app plays. Tooltips take `menu`, `menuRim`, `ink` and `inkHi`. | `--mp-menu`, `--mp-menu-rim`, `--mp-menu-ink`, `--mp-note`, `--mp-note-rim` |

**`materials`.** The parts the style's looks frame, each a look's name or `{"look": name, "set":
{...}}`, where `set` holds layer fields (Shapes) laid over the look for this part alone. A part
is dressed as a shape layer is: the style's `shape`, the look's material preset, the look, then
`set`; then every `"$name"` is swapped from the palette as worn. The shader draws it at the
part's box, the margin a glow needs round it, with no time, no screen position and no cast
shadow (the windows have a drop shadow of their own). A part without one keeps the colours above;
so does every part when the shader cannot run.

| Part | What it frames |
|---|---|
| `frame` | Each window's whole face, under everything in it. |
| `title` | Each window's title bar. |
| `button` | Every button but the clutterbar's letters: the transport, the toggles when off, the title bar's three (the mark, which opens the player's menu, the shade and the close), the playlist's, the shaded bars' small ones. |
| `lit` | A toggle that is on: EQ, PL, Shuffle, Repeat, On, Auto, Follow. Defaults to `button`. |
| `menu` | Every menu and submenu, drawn over all three windows: the player's menu, a playlist row's, and the buttons' own (O, I, Source, Presets, Add, Sel, Misc, List). Each submenu is a plate of its own. |
| `shade` | A shaded window's bar, any of the three: the bar alone, in place of `frame` and `title` (a shaded bar has no title plate over it). Defaults to `title`, with every corner the title's first, so a title plate with only its top corners cut is cut all round. |

The wells (the LCD, the playlist, the graph) are never framed by a look. Whatever the metal, the
figures in them must read, so they are the colours alone.

**`fonts`.** Each role names a look that has a `font` (Layers), and takes its face; the weights
stay the player's. `lcd` is the LCD's figures, the title and the playlist's time; `text` is
everything else's; `title` is the title bars, the toggles' labels and the menus' headings. The
faces are Hyperion's names (`chakra-petch`, `eurostile-ext`, ...), so *As without Remastered*
applies as it does to the HUD.

**`sounds`.** The moments the player sounds, each a style sound (`"$deploy"`, through the style's
`sounds`), an audio asset's name, or `null` for silence. They play only when the game's sound
does. A press of an ordinary button is silent: the player is used during a game, and its own
music is what should be heard.

| Moment | When |
|---|---|
| `open`, `close` | Ctrl+F9 or the Menu calls it up or puts it away. |
| `shade` | A window goes to its window shade or back; once when the menu shades or opens all three. |
| `denied` | A button that cannot be used is pressed: Add and Rem on the game's list, a control the other app does not allow. |

**What each style gives.** The three Mech materials make theirs from the recipe
(`mechStyle` in `huds/kits/mech-parts.mjs`): the hull's metal for the frames, with its groove and
seams off and small screws in the margin, the bezel for the title bars, the console's `btn` and
`cell.lit` for the buttons, the tooltip's plate for the menus, Chakra Petch in the LCD, and every
lit colour by its palette name. R8's style (`terran-r8.json`, worn by R8 and R9) is written by
hand: its gunmetal `plate` and `trim`, its teal buttons, lit a shade deeper than the console's so
their legends read, and its LCD in the teal of the console's LCD cells.

**Checks.** The style does not load for any of these:

- a `musicPlayer` that is not an object, or a `version` other than 1;
- a colour that is neither a colour nor a palette name holding one;
- a material naming a look the style does not have, or a `set` with a field a style may not hold (`x`, `w`, ...);
- a font naming a look with no `font`, or a font Hyperion does not have;
- a sound naming neither a style sound nor an audio asset.

These load with a note: a field that means nothing, a colour name, part, role or moment the
player does not have, a field in a `set` that means nothing. `SCHudPlayer.musicPlayer(style)`
returns the block resolved (each colour a colour, each part's look whole, each font and sound),
or `null`; `musicskin.js` turns that into the player's skin. The mock's check is
`scripts/smoke-hyperion-music-skins.mjs` in the design center.

### Sizes, window shade and menus

The same day's second pass, asked for by the player: the player 25% smaller by default, sizes to
pick as Winamp's Double size, the window shade on all three windows with docked windows kept
together, and menus. The style block gains one part, `shade` (above); the rest is the front
end's and the same in every skin. `hud.html` shows each state: `?mpsize=75|100|125|150|175|200`,
`?mpdouble=1`, `?mpshade=main,eq,pl` (any of the three), and a menu open,
`?mpmenu=main|options|size|shade|playback|source|presets|row`. The page keeps the size and the
shades in its address as they change. The front end's own page has the same under Music
player, size and menus.

**Sizes.** A size is a share of Winamp's own pixels on a screen 1080 high: **75, 100 (the
default), 125, 150, 175 or 200%**, and **Double size** (D, the clutterbar's D, the Size menu)
doubles any of them, as Winamp's does. 100 is Winamp's own size at 1080, the main window 275
pixels wide there and 550 at 2160. On a taller screen the sizes scale with it, as the HUD does,
which is why the default came down: the user saw the first default, 150, at 825 pixels on a 5120
x 2160 screen ("Size wise its too big on normal, needs to be scaled down."). A new size keeps the windows docked to the main window in their places
round it (scaled about its top left corner), the others at their top left corners, and each on
the screen.

**What does not shrink with it.** Two things, so the smaller sizes read:

- *A rule or rim is never thinner than one device pixel.* Every fine line is
  `max(one device pixel, its share of the size)`, and windows are placed on whole device pixels,
  so a 1-pixel line lands on a pixel instead of smearing over two.
- *Type stops at a floor*, in the screen's pixels at 1080: labels 7.5, title bars 8, text and the
  LCD's figures 9.5, the menus' second lines 8.5. A label still too wide for its button steps down
  half a pixel at a time, to 6 (R8's Eurostile Extended needs it on EQ, PL, Repeat and On at 125
  and 150). The LCD's segments are drawn as shapes and scale cleanly.
- *Under 125 the windows' floors shrink with the windows*: each floor, and the label's least size
  of 6, is multiplied by the size as drawn over 125 (Double size counted, so 75 doubled is drawn
  at 150 and keeps the full floors). At 100 the text floor is 7.6, at 75 it is 5.7; otherwise the
  labels would outgrow their buttons. At 1080, 75 is cramped (Shuffle and Repeat touch) and every
  label stays in its box; at 2160 it reads. **The menus and tooltips keep the full floors** at
  every size, since they are read, not fitted.

| Size | Main window at 1080 | At 2160 | Text floor | Label floor | Least label |
|---|---|---|---|---|---|
| 75 | 206 | 412 | 5.7 | 4.5 | 3.6 |
| 100 (default) | 275 | 550 | 7.6 | 6 | 4.8 |
| 125 | 344 | 688 | 9.5 | 7.5 | 6 |
| 150 | 412 | 825 | 9.5 | 7.5 | 6 |
| 175, 200 | 481, 550 | 963, 1100 | 9.5 | 7.5 | 6 |

- The plates are drawn again at each size and at the screen's own pixels, so their bolts, corners
  and bevels stay the HUD's size, in screen units, as on the console.

**Window shade.** Each window shades to a bar 14 Winamp pixels high, from its shade button (the
second of the title bar's three), a double-click on its title or a free part of its bar, or the
menu's Window shade. The bars, in Winamp's pixels across the 275 (x, y, width, height):

| Bar | What it holds |
|---|---|
| Main | The time (17, 3, 31, 8); the small transport and Source (52, 2, 60, 10: six buttons 9 x 9); the position (118, 4, 80, 6); a grip (204, 5.8, 42, 2.4), the title bar's rule, where the bar is free to drag and double-click. No name: it crowds the controls at 125. |
| Equalizer | Its name; On (90, 2, 26, 10); the preset (119, 2, 48, 10), a well that opens the presets; the volume (171, 3, 40, 8) and balance (215, 3, 30, 8). On, the preset and the balance are greyed while another app plays. |
| Playlist | What plays, scrolling (17, 2, 132, 10); its time (151, 3, 28, 8); the small transport (182, 2, 49, 10: five buttons). |

Each bar keeps the title bar's three buttons where they were. The time and the scrolling title let
presses through to the bar, so they drag it and a double-click there opens it again. The time
blinks while paused, as Winamp's does, and reads `live` for a stream.

**Docked windows stay together.** Windows touching edge to edge (within 2 pixels, after the
10-pixel snap) are docked. When a window shades or opens, the windows *hanging* from it move up or
down by what it lost or gained: those whose top lies on its foot, those whose top lies on theirs,
and so on down, and any window docked beside one of those at or below the foot. Windows above it
or docked beside it higher up stay. So the stacked three shade in any order and close up, and open
again in any order and push the others back down. Moving the main window takes every window docked
to it, through each other, shaded or not; the equalizer and the playlist move alone, as Winamp's
do. A shaded window snaps by its bar's edges, so it can be docked shaded, and stays docked when it
opens again (the windows under it move down). The menu's All three shades from the top down, so
each window's group moves with it.

**Menus.** One menu, drawn over the windows and on the overlay, never inside a window, so no window
lies over it; plated with the `menu` part, each submenu its own plate. It opens:

- from the **mark**, the first of each title bar's three buttons, where Winamp has its options
  button;
- with a **right click** anywhere on a window (on a playlist row, that row's menu, with the row
  chosen first, as Winamp does);
- from the buttons that had small menus (O, I, Source, Presets, Add, Sel, Misc, List), now this
  same menu;
- with the **Menu key or Shift+F10** while the player has the focus.

The player's menu, Winamp 2's main menu for what this player has: Source ▸ (the three, Add files,
Add a folder), Equalizer (Alt+G), Playlist (Alt+E), Window shade ▸ (each window, All three, None),
Size ▸ (125, 150, 175, 200%, Double size), Options ▸ (time elapsed or remaining, Always on top in
games, Shuffle, Repeat, Snap windows together, In games ▸, Pause for cinematics and briefings,
Lower under unit speech), Playback ▸, Visualizer ▸, Equalizer presets ▸, Track and source info ▸,
Close the player (the key as bound, Ctrl+F9 by default). An item is a check (a square LED), a choice (a round LED), a
submenu (an arrow) or an action, with its key on the right; one that cannot be used is greyed,
says why in its tooltip and plays `denied`. A submenu opens on hover after 160 ms or on Right,
beside its item, and both flip to stay on the screen. Up and Down move, Right or Enter opens,
Left closes a submenu, Enter or Space picks, Escape closes one level and then the player's key
handling resumes (Escape with no menu open closes the player). A press anywhere outside closes
them all.

**What the engine keeps.** `music.json` gains `scale` (75 to 200; the first builds' `size`,
always 150, is no longer read), `double`, `shade`, `eqShade`, `plShade` and `snap`, beside
each window's place; the mock calls its kit's
`saved(state)` after every change, which is where the engine would write it.

## Replays

The user (2026-10-23): "can you design for the hud styles, a variant for Replay mode.
Currently the port uses the normal hud but its not setup for replay viewing so we need a style
for that. Can you do a variation for the 4 hud position styles". A replay is watched, not
played: there is nothing to build and no group to recall, and what the viewer needs is the
replay's own controls and every player's figures. So a HUD may carry a second layout for it.

**The file names it.** A layout's `replay` is the file name of its replay layout, beside it:

```json
{"version": 3, "screen": 1080, "name": "Dark mech, full width", "replay": "mech-span-replay.json", ...}
```

The game wears that layout while a replay plays and the HUD's own the rest of the time, both in
the style and the light the player picked. A file with no `replay` is worn in replays as it is,
as now. In a package, the replay layout is a second file beside `hud.json`.

**A replay layout says so.** `"mode": "replay"` (the default is `game`). The checker then asks
for what a replay needs instead of what multiplayer needs: a `play` slot and a `menu` slot (the
Game Menu is where a replay is quit). Its options are its own, and so is the player's choice of
them.

**1.17's replay controls.** In a replay 1.17's command card is always one card, 0xF9 while it
plays and 0xF8 while it is paused (`sub_4A18D0`, from `rez\statbtnn.bin`), with three buttons:
Play or Pause (P, `order_replay_play`), Slower (D, `order_replay_slowdown`) and Faster (U,
`order_replay_speedup`). Slower and Faster are greyed at either end of the speeds and while the
replay is paused. Its status panel draws the progress as a bar, `Replay Progress` (stat_txt
1544), the time played, and the speed, `Paused` (1545) blinking in its place while paused
(`statdraw_replay_progress`). The speeds are the seven game speeds, Slowest to Fastest (a turn
of 167, 111, 83, 67, 56, 48 and 42 ms), then Fastest ×2, ×4, ×8 and ×16: eleven steps.

The three are slots, `play`, `slower` and `faster`; the game draws their faces (a glyph each,
Play's changing to Pause while the replay plays) and greys them as 1.17 does. The HUD draws
everything round them from the bindings: `replay.speed` or `replay.paused` as text,
`replay.step` for a ladder of lights, `replay.progress` as a `meter` for the progress bar.

**Hyperion's.** `tv` is Replay TV (F12), which takes the camera where the fighting is, and
`follow` (F11) keeps the selected unit in the middle; one at a time, and `replay.tv` and
`replay.follow` say which. View Players is 1.17's, in the `diplomacy` slot as the R9 layout has it.

**The players' board.** The `players` slot is a row for every player: their colour, name and
race, minerals, gas, supply, workers and APM, and an eye that shows or hides what that player sees
(View Players' vision, one player at a time). Two fields:

- `split`: true puts the first team in the slot's left half and the rest in its right, the right
  half mirrored so each team's figures face the middle.
- `rowH`: `[least, most]`, a row's height in units. The rows share the slot's height between
  them; a row 36 units tall or more puts the name on a line over the figures. Past the players
  that fit at `least`, a side's last row is "+N more · F1", which opens the stats popup (round
  16, The players' stats).

```json
{"id": "players", "slot": "players", "x": 26, "y": 16, "w": 874, "h": 60, "split": true, "rowH": [22, 60]}
```

**What the Mech layouts do with it.** Each of the four positions (`mech-span`,
`mech-span-min`, `mech-flank`, `mech-flank-right`) names a replay layout built from the same
parts. Every one keeps the minimap, the selection and the portrait where the game layout has
them, and:

- the command card's place is the replay's own card: a lit readout of the state and the speed,
  Slower, Play or Pause and Faster in wells, and the eleven steps as a ladder of lights under
  them, labelled Slowest, Fastest and ×16;
- the console's main light line is the progress, lit as far as the replay has played: the deck's
  along its top on the full-width console, the spine's down from the top on a column. The deck's
  line is taller, has a handle at its lit end, and moves the replay (Moving through a replay);
- beside it, in one cell lit in the accent as APM's is, where the replay is over its length,
  "10:05 / 20:57", or with the option Replay position (`replayTime`) the share played, "48% of
  20:57". It is one cell the width of the time (the user: "change to one button like
  the one on the length, slightly wider with the time"), and the time is the default;
- the minimap's buttons are View Players, Replay TV and Follow;
- a column has the board in the groups' place, one player a row on two lines, over two rows of
  selection. The full-width console carries no board, since it has the side view;
- no groups, idle or army button and no resources plate: they are the player's own, and a
  viewer has every player's on the board.

In the right column the transport and the position still read left to right; the mirror moves
them across whole (`keep` boxes in `huds/kits/mech-parts.mjs`).

Timelines, in light only: `replay.pause` dims the progress line and holds it until
`replay.resume` brings it back with a scan across the card; `replay.speed` runs a light along
the ladder; `replay.vision` pulses the board's edge.

**Moving through a replay.** The user: "slightly increase the timeline bar right above
the main console, and have a handle of sort at the end of the progress bar of the reply, we'll be
giving the user the ability to rewind replays being watched since we have that built to the
engine". On the full-width replays the deck's light line is 5.5 units tall in a well of 8.5, where
a game's deck has 2.5 in 5.5. A handle (`seekKnob`, 12 × 20 in the `knob` look, with a lit line
down it) rides its lit end, and a layer over the line (`seekHit`) carries the action
`replay.seek`. A press there moves the replay to that point; a drag moves it while
`replay.seeking` is true and lands on release; Left and Right step 10 seconds, Page Up and Page
Down a minute, Home and End to the ends. A move fires `replay.seek` with `{ from, to }` in
seconds, and the `seek` timeline lights the line and the handle's glow. While the game is live,
the handle is hidden and the layer greyed (`enabled`). The engine has the rewind; the mock's world
does not go back, so on the page only the position moves. The columns' spine light has no handle,
since it runs mostly behind the modules.

**On the page.** `hud.html?game=replay` plays the world as a replay, from 6:08 of 14:31:
the transport, the keys P, U and D, TV and Follow work, and the eyes switch their state (the
mock world has no fog, so nothing else changes). The one-at-a-time view has Game and Replay
in its bar, and `?compare=mech-replay` shows the four side by side.

## The players' stats

The user (2026-10-25): "in Hud can we have the designer revise ways to turn on and off the
player stats, perhaps like how in other games there's like a
F1 command that will open the stats. Can it generate a new modal popup for that in observer mode,
along with a way for a view where its on the side of the view and transparent backing. In case
viewers want it on screen while observing still and its not in the way as much."

**Two views, two keys, each on or off by itself.**

- **F1 opens or closes the popup**: every figure of every player, in four pages, over the middle of
  the view. Tab and Shift+Tab step the pages while it is open, and Esc closes it. Held for more
  than 300 ms, F1 opens it only while the key is down, so a viewer can glance and let go.
- **Shift+F1 shows or hides the side view**: a column of the players at the edge of the view, on a
  see-through backing, to keep on screen while the game is watched.
- Both may be up at once, and neither key changes the other view. Closing the popup leaves the
  side view as it was. The popup opens again on the page it last showed.
- A "+N more · F1" row, on the board or in the side view, opens the popup too, as does a click on
  it. The popup's own close button is its × in the corner.

**Only for a viewer.** The two views exist while a game is watched (`game.replay`, which a game
watched live has too), and nowhere else. In a game the player plays, F1 is still 1.17's Help and Shift+F1
does nothing. In a replay or as an observer, F1 opens the stats instead of Help, since a viewer has
no commands to look up; Help stays in the Game Menu (F10) for them. Both views are off when a game
the player plays begins.

| Key or click | The popup | The side view |
|---|---|---|
| F1 | opens or closes | no change |
| F1 held past 300 ms | open until the key is let go | no change |
| Shift+F1 | no change | shown or hidden |
| Tab, Shift+Tab | the next or the previous page, while open | no change |
| Esc | closes | no change |
| "+N more · F1", or the popup's × | opens, or closes | no change |
| a game the player plays begins | closed | hidden |

**The slot.** `stats`, with `view`: `side` or `popup`. A layout places it at most once for each
view, and shows each panel by its binding, `"visible": "{stats.side}"` or `"{stats.popup}"`.

```json
{"id": "statsSide", "visible": "{stats.side}", "ax": "right", "x": 16, "y": 72, "w": 300, "h": 636,
 "layers": [{"slot": "stats", "x": 0, "y": 0, "w": 300, "h": 636, "view": "side", "backing": 0.38, "rowH": [24, 52]}]}
```

- **The side view.** The game draws a row for each player, grouped by team with 8 units between
  the teams. A row of 40 units or more has two lines: the name, race letter and APM over minerals,
  gas, supply and workers. A shorter row has one line: the name, supply, workers and APM. Under
  each row is a rule in the player's colour, as long as their army's value against the largest
  army. `backing`, 0 to 1 (0.38 when it is left out), is how solid the shade behind each row is.
  At 0 the type stands alone with its shadow. `rowH` is `[24, 52]` by default. Past the players
  that fit at the least height, the last row is "+N more · F1".
- **The popup.** The game draws what is inside: a header (PLAYERS, the count of players and teams,
  the four pages as tabs, the keys, the ×), the column headings, and the rows. When there is more
  than one team and a team has more than one player, each team has a heading row with its sums.
  The layout draws the frame: the popup has no backing of the game's, and a `backing` on it is a
  note. `rowH` is `[22, 34]` by default. When the rows at their most height leave at least 150
  units, a graph of the page's figure over the game fills the rest. Otherwise the rows take the
  height, down to their least, and then scroll.

The popup's pages:

| Page | Columns | The graph |
|---|---|---|
| Overview | minerals, gas, supply, workers, army supply, APM, and the vision eye | supply |
| Income | minerals a minute and gas a minute (as bars), workers, both together | income a minute |
| Army | army supply, army value (a bar), and the units, an icon and a count for each type | army value |
| Units lost | units lost, value lost (a bar), units killed, value killed (a bar) | value lost |

A unit type's icon is the game's command icon. A missing icon shows the type's initials. Army
value is the minerals and gas of every living unit that is not a worker, and income a minute is
what was mined in the last minute of game time. The graph takes a figure every two seconds of game
time, one line for each player in their colour, and ends on the figures the rows show. In a
replay it runs from the game's start: the engine passes through that part when it seeks to where
the replay is watched from, and can take the figures on the way. The mock draws that part from a
growth model.

**Bindings and events.** `stats.popup` and `stats.side` are true while each view is up (Bindings).
`stats.show` and `stats.hide` fire as each comes and goes, with `info.view`. A timeline's `view`
picks one view's, so a layout can move the side view and the popup each their own way:

```json
{"id": "statsSideIn", "on": "stats.show", "view": "side", "tracks": [
  {"target": "statsSide", "prop": "x", "keys": [[0, 40], [260, 0, "out"]]},
  {"target": "statsSide", "prop": "opacity", "keys": [[0, 0], [200, 1]]}]}
```

**More players than fit (the players' board).** Hyperion plays up to 32 players. The board and the
side view never run on below their slot. Past the players that fit at `rowH`'s least, a side's last
row is "+N more · F1", and the popup holds everyone. On a board split by team, each half has its
own "+N more" row. The popup's rows scroll when they do not fit at their least height.

**The eye stops at the eighth player.** 1.17's replay vision is a byte, a bit for each player, so
only players 1 to 8 can have their vision shown or hidden. From the ninth player on, the board
keeps the eye's place empty, so the columns still line up. The popup shows "–" there, with the
tooltip "Vision switches for the first eight players only". The eye is an almond with a pupil, not
a pill: in a 24 × 24 box, the path `M2 12s3.6-6 10-6 10 6 10 6-3.6 6-10 6S2 12 2 12z` stroked
1.8 wide, and a filled circle of radius 3 at its centre. A hidden player's eye is drawn in the
label colour at 0.55 opacity.

**The replay card's ladder.** The ladder's labels are SLOWEST at its left, FASTEST over the seventh
step and ×16 at its right. On a ladder narrower than 260 units (`mech-span-min` and the columns),
FASTEST does not fit between the other two and is left out. The wider gap after the seventh step
still marks where Fastest ends and the ×2 to ×16 steps begin.

**No clock under a panel.** A layout with `mode: "replay"` gets no clock or APM from the engine
when it places no `time` or `apm` slot: the replay's position readout stands for the clock. In any
layout, the engine never draws its own clock or APM under one of the HUD's panels. In the right
column, the engine's clock lay over the progress chip.

**Games watched live.** Corrected 2026-10-25. `game.observer` was read as
1.17's `IsTVBGame()`, but that is the Top vs Bottom game type (0x0F), and 1.17 has no observer
seat. Every game Hyperion lets a player watch plays as a replay: a finished one from its file, or
a LAN room's match or the AI lab's game as it is played. So `game.observer` now means a game
watched live, `game.replay` is true with it, and a viewer always wears the replay layout. Live,
`replay.live` is true, the length is open (`replay.progress` 0, `replay.length` empty), and the
position cannot be moved: the Mech's position reads "LIVE 6:10", and its line is lit whole.

**Where the Mech layouts put them.** Only the replay layouts carry the views, since only a viewer
can open them:

| Layout | The side view | The popup |
|---|---|---|
| `mech-span-replay` | the right edge, 300 wide, from 16 down to 16 above the right console (692 tall); shown from the start (`start`) | 1040 × 760, centred, from 16 |
| `mech-span-min-replay` | the same, 806 tall; shown from the start | the same |
| `mech-flank-replay` | the right edge, opposite the column, from 16 to 16 above the bottom | 1040 × 760, centred on the space between the column and the side view (28 right of the screen's centre) |
| `mech-flank-right-replay` | the left edge, the same | 28 left of the centre |

So the popup never covers the side view or the console. The game layouts, the pods and the other
layouts carry neither view. The full-width replays carry no board (the user: "take
down the top stats since we now have the side stats"), so the side view and the popup start at 16
there, and the side view is on when the replay starts.

**A layout without them.** When a viewer calls up a view that the layout does not place, the game
draws it in a place of its own: the side view at the right edge, 300 × 540 from 112 units down,
on its own backing; the popup in the middle, 1000 × 640 from 112 down, on a plate in the
interface colours. Classic layouts, R9, the pods and addons get these.

**The player's choice.** The Mech replay layouts offer Stats backing (`statsBacking`): None
(backing 0), Faint (0.38, the default) or Plate (0.72); and Stats at the side
(`statsDrawn`): the game's rows in the `stats` slot (the default), or the same view drawn by the
layout's own layers (A design of your own, The proof).

**On the page.** `hud.html?game=replay` or `?game=observer`, then F1 and Shift+F1. The bar has
Game, Observer and Replay, and Stats and Side buttons that work only while watching.
`?stats=popup`, `side` or `side,popup` opens them. `?statspage=income|army|lost` picks the page,
and alone opens the popup on it. A link to the stats that names no game opens as an observer.
`?players=2` to `32` adds stand-in players for the overflow and the eye's limit.

## A design of your own

The user (2026-10-25): "I want to ensure that with Replay, and all of the hud actions,
buttons, styling, etc. That its fully customizable for users to
bring in their own hud design ... Is our platform setup that way? save for the importing of the
file for now." Before this round a file could place every part of a HUD but only the game could
fill some of them: the players' board and the stats were slots, the replay's buttons pressed only
as slots, and the slots' type was the page's per style family. This round gives a layout the
means to draw all of it with its own layers, and the game keeps its slots for a layout that
wants them. Every field is optional, and a file without them is read as before.

### Actions

Any layer but a slot may have an `action`: a press of the left button on it does what that
button does. It is Hyperion's Buttons (its own copy of this page, "Buttons (Hyperion's
addition)", `port_hudinput.cpp`'s `PressAction`), the same names and rules, with the watched
game's added:

| `action` | A press |
|---|---|
| `menu`, `diplomacy`, `messaging`, `terrain` | 1.17's four buttons, as their slots do |
| `idle`, `army` | the next idle worker; every unit of the player's that fights |
| `group.0` to `group.9` | recalls that control group, or sets it when it is empty |
| `replay.play`, `replay.slower`, `replay.faster`, `replay.tv`, `replay.follow` | the replay's buttons, as their slots do |
| `replay.seek` | moves the replay: a slider over the layer's box (below) |
| `stats.popup`, `stats.side` | opens or closes the stats popup, or the side view |
| `stats.page.next`, `stats.page.prev`, `stats.page.overview`, `.income`, `.army`, `.lost` | the popup's pages |
| `vision` | in a row of an each layer over the players, switches that row's player's vision |
| `vision.0` to `vision.7` | switches the vision of the player in that seat |

```json
{"id": "playBtn", "shape": true, "look": "btn", "action": "replay.play", "label": "Play or pause", "x": 40, "y": 8, "w": 48, "h": 36},
{"id": "seekHit", "rect": true, "fill": "#000000", "opacity": 0, "inset": [24, 2, 24, 20], "action": "replay.seek", "enabled": "{replay.live ? 0 : game.replay}"}
```

- **`enabled`** is a binding; false greys the button: no press, `aria-disabled` set. Without
  it the button is always enabled where it is shown.
- **`label`** is what a screen reader says; without it, the action's own name ("Play or pause").
  A button that turns something on (`stats.side`, `replay.tv`, `vision`) also says whether it is
  on.
- **The action follows its slot**, as Hyperion's does: where the game would not show the slot an
  action belongs to, the layer draws nothing and takes no press. Terrain in a multiplayer game,
  Idle, Army and the groups in a replay, the position, vision and the stats in a game the player
  plays.
- **An unknown name is a note**, and the layer draws at 40% of its opacity and takes no press,
  so a design made for a later Hyperion still plays. `vision` outside a row has no player and is
  greyed the same way, as is a seat past the eighth (1.17's replay vision is a byte, a bit a
  player).
- **A press fires `panel.press`** for the panel with `action` in its info, and a timeline may
  answer one action alone with its own `action`: `{"on": "panel.press", "panel": "deck",
  "action": "replay.play", ...}`. That `action` must be one a layer of that panel has.
- **`replay.seek` is a slider.** The fraction along the box (from `meterFrom`'s side, left by
  default) is where the replay goes. A press moves it there, a drag moves it while
  `replay.seeking` is true, and the keys step it: Left and Right 10 seconds, Page Up and Page
  Down 60, Home and End to the ends. The game fires `replay.seek` with `{ from, to }`. The
  slider says where the replay is, "6:10 of 14:31".
- An `action` on a slot is a note and is ignored, since a slot does what its slot does.

### Each: a row for every player

An `each` layer is drawn again for every player, or for each kind of unit in a player's army,
from templates. A row is a box of its own, and the template's layers are placed in it as a
panel's layers are in the panel.

```json
{"id": "rows", "each": "players", "inset": [0, 0, 0, 138], "rowH": [24, 52], "gap": 3, "order": "team",
 "teamGap": 8, "teamH": 16,
 "layers": [
   {"id": "swatch", "rect": true, "inset": [0, 0, null, 0], "w": 4, "fill": "{p.colour}"},
   {"id": "name", "text": "{p.name}  {p.race.letter}", "x": 13, "y": 0, "w": "{row.w - 120}", "h": "{row.h}"},
   {"id": "eye", "shape": true, "path": "...", "x": "{row.w - 30}", "y": 4, "w": 22, "h": 14,
    "opacity": "{p.vision ? 1 : 0.35}", "visible": "{p.vision.can}", "action": "vision"}
 ],
 "team": [{"id": "head", "text": "TEAM {t.index + 1}    ARMY {t.army}", "inset": [6, 0, 6, 0]}],
 "more": [{"id": "more", "text": "+{more.count} MORE", "inset": [13, 0, 8, 0], "action": "stats.popup"}]}
```

| Field | What | Default |
|---|---|---|
| `each` | `players`, a row a player; or `units`, inside a players row, a cell for each kind of unit in that player's army | |
| `layers` | the row's template | |
| `team` | a head over each team's rows (players only) | none |
| `more` | a row standing for the players that do not fit, read with `more.count`; without one they are not shown, with a note | none |
| `axis` | `y`, rows down, or `x`, across | `y` for players, `x` for units |
| `rowH` | `[least, most]`: the rows share the box, each between the two | `[24, 52]`, units `[20, 40]` |
| `gap`, `teamGap`, `teamH` | the space between rows, between teams, and a head's height | 3, 8, 18 |
| `order` | `seat`, the lobby's order, or `team`, each team together | `seat`, `team` with heads or a split |
| `split` | `true`: the first team in the first half of the box and the rest in the second, the second half mirrored to face the middle; `"same"`: not mirrored. Rows down only | false |
| `splitGap` | the space between the halves | 12 |
| `max` | the most rows | all |

When every player fits at the least size, the rows grow toward the most; when they do not, the
layer shows as many as fit, the last place given to the `more` row. A units each goes one deep:
inside a players row, never inside another units each, and never at the top level. A template's
layers may have actions (a row's `vision` is that row's player's), and a timeline may move one
by `panel.each.layer`, every row's at once. A layer in a template rides nothing.

**The names a row reads.** In a template, bindings read the row's own names as well as every
other name:

| Name | Where | What |
|---|---|---|
| `p.<field>` | a players row; a units cell, for its player | the row's player |
| `t.<field>` | a players row, a team head | the row's team: `index`, `count` (its players), and the sum of each figure |
| `row.index`, `row.count`, `row.w`, `row.h`, `row.side`, `row.first`, `row.last` | every row | its place, the rows shown, its size, 1 in the second half of a split |
| `u.type`, `u.name`, `u.count` | a units cell | the kind of unit, its name, how many |
| `more.count` | the `more` row | the players not shown |

A player's fields: `name`, `race` (`terran`, `protoss`, `zerg`), `race.letter`, `colour`, `team`,
`index`, `minerals`, `gas`, `supply.used`, `supply.max`, `workers`, `apm`, `army` (army supply),
`value` (the army's worth in minerals and gas), `income.minerals`, `income.gas`, `income`,
`lost.count`, `lost.value`, `killed.count`, `killed.value`, `vision`, `vision.can` (the first eight
seats) and `units.count`. Outside a row, `players.N.<field>` reads them by seat, 0 to 31, and
`top.<figure>` is the most any player has, for a bar's scale: `"meter": "{p.army / top.army}"`.
A row's name read where it is not given is an error that says where it is read.

### Icon, chart and ride

**`icon`** draws a unit's picture: `{"icon": "{u.type}", "w": 28, "h": 28}`. The game gives it
from the install, the command card's icon for that type; without the art it draws the type's
initials in a square, in the layer's `color`. The layer's box fits it, its aspect kept.

**`chart`** draws a figure over the game, a line a player in their colour, on rules across:

| Field | What | Default |
|---|---|---|
| `chart` | `supply.used`, `income`, `value`, `lost.value`, `army` or `workers` | |
| `stroke` | the rules' colour | the text colour |
| `gridLines` | how many rules, 0 to 12 | 4 |
| `chartPad` | the space inside the box | 4 |
| `dot` | a dot's radius at each line's end, or 0 | 0 |

The game samples the figures as the game plays (in a replay, from the start up to where it is),
and a chart drawn again only when a new sample comes.

**`ride`** stands a layer at the end of another's meter: `"ride": "deckLamp"` centres it on the
lit end of the layer `deckLamp`, which must come earlier in the same panel, so a handle follows a
progress bar. A riding layer needs its own `w` and `h`; its `x` and `y` are ignored.

### Start

A file's `start` is the viewer's state when the layout is put on: `{"stats.side": true}`. Its
names are `stats.side`, `stats.popup`, `replay.tv`, `replay.follow` and `minimap.terrain`, each
true or false, and `stats.page`, one of the popup's pages. It is applied when the layout is put
on for a game: at the start of every game or replay that wears it, and when the player picks it
during one. It is not applied again while the same layout stays on, when its style, light or
options change, on a live reload, or when the intro plays again, so a viewer who closes the side
view keeps it closed until the next game or replay. The mock keys this on the layout's name and
its game or replay (`applyStart` in `hud.js`): it has no next replay, but going from the game to
the replay and back puts each layout on again, as a new game would. A link that names a state of
its own (`?stats=`) keeps it on the first load.

### The slots' type

A style's `slots` gives the type of the text the game writes in its slots, by role, in the
same tokens as the looks. It replaces Packages' provisional note: it is built, the player checks
it, and `hud.html` dresses any style that has one (`slotSheet` in `hud.js`).

```json
"slots": {
  "button": {"size": 0.4, "weight": 600, "tracking": 0.05, "case": "upper", "color": "$buttonText"},
  "clock": {"size": 0.82, "tracking": 0.04, "color": "$text"},
  "label": {"color": "$label"}
}
```

| Role | What the game writes | Its defaults |
|---|---|---|
| `button` | 1.17's and the replay's button faces, the Game Menu | eurostile, 0.4, 600, 0.05, upper, `$text` |
| `clock` | the clock, the replay's position | eurostile, 0.82, 500, 0.04, `$text` |
| `figure` | the resources' lit cells, APM's number, one unit's figures, the stats' numbers | eurostile, 0.56, 600, 0.02, `$figure` |
| `label` | APM's label, small labels, the selection's tabs, the popup's chart titles | eurostile, 0.42, 500, 0.08, upper, `$label` |
| `name` | a player's name in the stats | eurostile, 0.5, 600, 0.04, `$figure` |
| `title` | the popup's title | eurostile-ext, 0.5, 700, 0.18, upper, `$figure` |

A role takes `font` (one of the game's fonts), `size` (a share of the slot's height, above 0
and at most 1.5; only `button` and `clock` take it, since they fill a box with one line),
`weight` (100 to 900), `tracking` (ems, -0.1 to 0.5), `case` (`upper` or `none`), `color` and
`glow` (a colour or a palette `$name`). Only what a role sets is applied: what it leaves out keeps
the game's dressing, and a style with a `slots` block is dressed from its palette (`text`,
`figure`, `label`, `accent`, `lamp`, `lcd`, `lcdOff`) as the Mech's are, whoever made it. A button
keeps the game's colours when it is hovered, focused, on or greyed. An unknown role is a note; a
bad value is an error. The Mech styles carry the page's old Mech dressing written down this way,
with `buttonText` in their palette, the text colour at 78%. The faces they leave to the game,
so As without Remastered still changes them.

### The manifest

What a design offers the player is its default layout's `package` block, beside the name and
author Packages describes: its layouts, each in one size or several, its styles, the words
Settings puts over each list, and what a player starts on. Settings' HUD tab and the one-view bar
are drawn from it, Hyperion's own design included (`huds/mech-span.json`).

```json
"package": {
  "format": 1, "name": "Hyperion", "author": "Hyperion", "made": "2026-10-25",
  "labels": {"layout": "Layout", "size": "Size", "style": "Material"},
  "layouts": [
    {"id": "span", "label": "Full width", "sizes": [
      {"id": "full", "label": "Full", "line": "Everything at full size.", "file": "mech-span.json"},
      {"id": "min", "label": "Minimized", "line": "A smaller minimap, portrait and card, and two rows of selection.", "file": "mech-span-min.json"}]},
    {"id": "flank", "label": "Left column", "file": "mech-flank.json", "line": "The columns come in one size, everything at full size."}
  ],
  "styles": [{"id": "mech-gunmetal", "label": "Gunmetal", "line": "Blued, machined, armour plated", "file": "styles/mech-gunmetal.json"}],
  "default": {"layout": "span", "size": "full", "style": "mech-gunmetal"}
}
```

- A layout is in one size (`file`) or several (`sizes`, each with its own `file`). A size is a
  layout file of its own, so a minimized console can move anything, not only shrink. Each file
  names its replay in its own `replay`, so the manifest lists the game layouts only.
- A layout in one size answers to whichever size is picked: Settings greys the other sizes for it
  and shows its `line`.
- `labels` are `layout`, `size` and `style`; the defaults are Layout, Size and Style.
- `default` is where a player starts and where Restore Defaults goes back to. A default that
  names nothing listed is a note, and the first is used.
- A mistake in the manifest is a note, since the layout still loads without it.
  `SCHudPackage.manifest(block)` reads one and returns `{ layouts, styles, labels, default,
  problems }`; `hud.js`'s `readManifest` builds the bar and Settings from it.

Format 1's `.hud` carries one layout, so a package's other layouts and sizes stay in the design's
folder for now; carrying them in the file (a `layouts/` folder) comes with importing, which the
user set aside for now.

### How the engine reads a design

The order Hyperion would read a design in, so a player's own design and Hyperion's go through
one path:

1. **The manifest.** The default layout's `package` block: the layouts, sizes and styles, which
   Settings lists and the player's picks name. A design without one is its one layout and its
   styles by race, as before.
2. **The layout for this game.** The picked layout in the picked size, or its `replay` when a
   game is watched (a replay, or a game watched live). Its `options` with the player's picks
   (`choose`), then its `band` and `outside` for the screen's width.
3. **The style.** The picked style, or the one for the race played, worn on the layout (`wear`):
   its looks, materials, shape language, palette and the light colour (`relight`), its tooltip
   and music player blocks, and its `slots`.
4. **The checks.** The file's (Checking a file) and the style's; an error keeps the HUD the
   player had.
5. **The state.** The layout's `start`, once, where it is put on.
6. **Each frame.** The bindings from the game; the players and their figures (`p.*`,
   `players.N.*`, `top.*`) and the chart's samples; the slots filled by the game; each row of an
   each layer laid out; the layers drawn; a layer with an action given a press area over its box
   that follows its slot; timelines answering events.
7. **A press.** A slot's press or an action's goes through the handler 1.17's button or key
   calls; `panel.press` with the action follows, then the event the action causes
   (`replay.pause`, `replay.seek`, `stats.show` and the rest).

What the engine has to add, beside what it has: the replay's and the stats' actions
and `vision` in its `PressAction`, `replay.seek` as a slider over a layer, the each layer, the
icon and chart layers, `ride`, `start`, the style's `slots` read by its slot drawing, the manifest
read by its HUD tab, and the names. `game.observer` with `game.replay` as above.

### The proof

The Mech replay layouts carry the stats' side view twice: the game's (`statsSide`, the `stats`
slot) and one drawn by the layout alone (`statsSideDrawn`): an each layer over the players with
team heads, a "more" row that opens the popup, each row the player's colour, name, race, APM and
(at two lines) their minerals, gas, supply and workers, an eye whose `vision` action switches
theirs, and under the rows a chart of the supply over the game. The option Stats at the side
(`statsDrawn`) swaps them. Nothing in it is the game's, so a player could move, restyle or
rebuild it as they would any layer. `hud.html?game=replay&hudopt=statsDrawn:layout&players=12`
shows it.

## Checking a file

A file that fails these does not load, and the HUD on screen stays: a version or screen other
than 3 and 1080; a panel without an `id`, with a repeated one, or without numbers for `x`,
`y`, `w`, `h`; an anchor that is not one of its three; a layer that is not exactly one kind; an
image naming no asset; an unknown slot; a binding that does not parse or names something not
in the list; an unknown event; a panel event naming no panel; a track whose target does not
exist or whose property that kind of thing cannot move; keys out of order; an unknown easing;
an `inset` or `slice` that is not four numbers (an inset's left or top may be `null`, but only
with a right and a `w`, or a bottom and an `h`); a `blend` other than normal or add; a
`cornerStyle` other than round, chamfer or notch; `cutouts` that are not up to four
`[x, y, w, h, radius]`; a shape `fill` of more than four stops; an asset with none of `art`,
`file` and `audio`; a cue that is not `[ms, "<audio asset>", volume]`; a `delay` that is not a
number.

These load with a note: a field that means nothing (it is ignored), tint, add, glow or scan on
a slot (ignored), a slot placed twice, a font not among the game's.

The multiplayer slots add, with a note: a file that places no `menu`, `diplomacy` or
`messaging` slot ("Not fit for multiplayer", naming the ones missing); and, in a file with
`classic: true`, each slot
that is Hyperion's. And, as errors: an `align` other than left, center or right, or on a slot
that is not a button; a timeline's `slot` on an event other than `panel.press`, or naming a slot
its panel does not hold; a `classic` that is not true or false.

A layer's `surface` must be one of the surfaces and its `revealStyle` `peel` or `warp`, or the
file does not load.

The band adds, as errors: a `band` that is not a number 0 or above, on the file or on an option
value; an `outside` other than left or right; a panel both `outside` and `between`; an `overlap`
that is not two numbers. With a note: a `band` under 1440 (narrower than a 4:3 screen, so the
panels keep to it on every screen); panels `outside` in a file with no `band` where no option
gives one either, since they are never drawn; a set that changes `outside`.

The players' stats add, as errors: a `stats` slot without a `view` of side or popup; a
`backing` that is not 0 to 1, or on a slot other than `stats`; a timeline's `view` on an event other than
`stats.show` or `stats.hide`, or other than side or popup; a `rowH` on a slot other than
`players` or `stats`. With a note: a `view` on another slot; a `backing` on the popup; the
stats slot placed twice for the same view.

The replay layouts add, as errors: a `mode` other than game or replay; a `replay` that is not
a `.json` file beside the layout; a `split` that is not true or false, or on a slot other than
`players`;
a `rowH` that is not `[least, most]` with the least above 0. With a note: a replay layout
(`mode: "replay"`) that places no `play` or `menu` slot ("Not fit for replays"), in place of
the multiplayer note; a replay layout that names a `replay` of its own, which is not read.

Actions, each layers, icons, charts, rides and `start` add, as errors: an `action` that is not
a string; an `each` other than players or units, a units each outside a players row or deeper
than one; a `split` each across (axis x), or
`team` or `split` on a units each; a row's name (`p.*`, `t.*`, `row.*`, `u.*`, `more.*`) read
where that row does not give it; a seat past 31 or a figure `top.*` does not have; a `chart`
that is not one of its figures, `gridLines` outside 0 to 12; a `ride` naming no earlier layer of
its panel, a riding layer without `w` and `h`, or a ride inside a template; a `start` naming
something else or with the wrong kind of value; a timeline's `action` on an event other than
`panel.press`, or naming an action no layer of its panel has. With a note: an unknown action, a
`vision` outside a row, a vision seat past the eighth (each greyed), an `action` on a slot
(ignored), an each over the players without a `more` row. A replay layout counts an action as
placing its button: one with a `replay.play` layer is fit for replays.

**A style** does not load, and the HUD keeps the style it has, for: a `kind` other than
`hud-style` or a `version` other than 1; a `race` other than the three or `any`; an asset with
none of `audio`, `file` and `art`; an easing not in the list; a sound naming no audio asset of
the style's (only `null` is silence); a material or surface that does not exist; a
`cornerStyle` other than round, chamfer or notch; a look or `shape` that places or names
things (`x`, `y`, `w`, `h`, `inset`, `id`, `text`, `slot`, `image`, `rect`, `shape`, `look`);
`tracks` that are not a list; a `drop` that is not a list of `"target.prop"`. Worn on a layout,
a `$name` the style does not give is an error. With a note: a field that means nothing, a look
the style does not have (the layer draws as the layout has it), a cue naming a sound the style
does not have (silent), a style timeline for one the layout does not have and with no `on`.

### The engine's checker (Hyperion's addition)

    Hyperion.exe --hud-check <layout.json | folder | package.hud> [--style <style.json>]
                 [--pick <option>=<value>]... [--light #rrggbb] [--layout <W>x<H>] [--out <report.json>]

checks a design without a game (`port_hudcheck.cpp`, `hyperion-exe.md`). A folder is read as a
design's: its `hud.json`, its `style.json` if it has one, and its parts files. A package is
unpacked first, as a game unpacks it, and what the unpacking left out is among the notes. A
`--pick` is one option's value, as the player's would be, and `--light` the light colour.

It prints one JSON object: `plays`, true or false; `errors` and `warnings`, the notes of this
page, each a line of text; and, when it plays, `panels` and `timelines`, the counts the file
compiled to. With `--layout 2560x1080` it adds `lw`, the screen's width in units, `rects`, each
panel's box by its id, and `layers`, every layer drawn with its panel and layer numbers, its
target (`panel.layer`), its kind, its box, its margin and its transform, which is what
`tools/hud/hudgolden.mjs` compares against the design center's player. `--out` writes the JSON to
a file instead of standard output. The exit code is 0 when the design plays, 1 when it has errors,
2 when a file does not read.

### Live reload (Hyperion's addition)

A game plays the design Settings names, or the one `HYPERION_HUD=<layout.json>[;<style.json>]`
names, which is how a design is played straight from the folder it is being written in. A design
named that way plays with that style and no picks: the options, the light, the selection colour
and the buttons left out are Settings' own and belong to the design chosen there, so its sounds
and its tooltips are on and no button is left out.

The active design's files are looked at about once a second — its layout, its style, its package,
the parts files beside it and the files in its own `files\` folder — and the design is read again
when one of them was saved, or when a file came or went in those folders (`port_hud.cpp`, `Watch`
and `Changed`). A refused design is read again too, so a fix shows in the running game. Every load
writes what it found to `hyperion_hud.log`: the layout and style read, `plays` or `refused`, then
each error and note. The settings are read in the same second, so a change of option, light or
colour loads the design again with the picks over it, and a running game keeps playing throughout.

## Packages

A HUD a player backs up or hands to another player is one file, `<name>.hud`.
`hudpackage.js` is the reader, checker and writer, and `hud.html`'s file section exports and
loads them.
Hyperion as built reads packages differently in three ways (Hyperion as built).

**What it is.** A zip archive, its entries stored or deflated. Unzipped into a folder of the
same name it loads the same, so an author can work on loose files.

```
terran-r9.hud
  hud.json                the layout (version 3); required
  styles/terran-r8.json   a style it wears; optional, one per race at most
  files/alarm.ogg         the author's own sounds (.ogg) and images (.png); optional
```

**`hud.json`** is a layout as the rest of this page describes, with two more fields:

| Field | Meaning |
|---|---|
| `package` | `{ "format": 1, "name", "author", "description", "made": "2026-10-09" }`. `format` is 1; a higher one is refused as made for a newer Hyperion. The other four are text; a missing name is a note. A layout with no `package` still loads from a package, with a note. |
| `style` | The style the layout wears by default, by its file name in `styles/` without `.json`. |

**Which style.** The race in play's (the style whose `race` is that race), else the one `style`
names, else the only one. None, or a `style` naming a file the package lacks, means the
layout's own looks draw it, with a note. Two styles for one race: the first is taken, with a
note.

**References stay inside.** A `file`, `audio`, `normal` or `fx` on an asset, and a region's
`brush`, name a file in the package's `files/` as it sits there (`"audio": "alarm.ogg"` is
`files/alarm.ogg`). Refused: a URL (`https:` and every other scheme but `data:`), an absolute
path, a backslash, a `..`, and a sound that is not `.ogg` or an image that is not `.png`. A
`data:` URL is inside the file already, so a `data:image/png` or `data:audio/ogg` one is allowed.
`hd:` and `sd:` art still resolves to the player's install, which is what keeps game art out of
every package.

**No game art, no fonts.** A package holds the author's own work: generated plates, their own
sounds. Nothing cut, traced or decoded from StarCraft, Remastered or StarCraft II. The checker
cannot tell an author's image from the game's, so the rule is stated here and in the export
form, and exporting is the author's word on it. A font file is refused by name: fonts inside a
package are not allowed in format 1.

**Limits.** 16 MB packed, and 16 MB unpacked, read from the archive's directory before
anything is inflated, so a small archive cannot unpack to a huge one. Only `.json`, `.ogg` and
`.png`; anything else is refused with its name. A `.png` that is not a PNG inside, or an
`.ogg` that is not Ogg, is refused too. Encrypted entries, zip64 and any method but stored or
deflated are refused.

**Nothing runs.** A package is data the game's player draws, so a shared HUD is as safe to load
as a shared map.

**Missing pieces never stop a HUD.** A missing sound is silence and a missing image an empty
layer, each with a note naming where it was used. A style file that is not valid JSON, or not
a style, is left out with a note.

**Notes, not refusals:**
- a zip that holds the folder rather than its contents (`terran-r9/hud.json`, which is what
  zipping a folder from the file manager makes) is read from inside the folder;
- what an operating system leaves (`__MACOSX/`, `.DS_Store`, `Thumbs.db`, `desktop.ini`) is
  skipped;
- a `.json` or other allowed file outside `hud.json`, `styles/` and `files/` is left out;
- a file in `files/` that nothing names;
- a style whose `id` differs from its file name (the file name wins).

**Fonts.** `eurostile`, `eurostile-ext` and `blizzard` are Remastered's, read from the player's
Remastered install. Without one, Hyperion draws them with the fonts it ships (OFL):

| Font | With Remastered | Without |
|---|---|---|
| `eurostile` | Eurostile (`SC EUROSTILE-REG`) | Saira |
| `eurostile-ext` | Eurostile Extended (`SC EUROSTILEEXT-REG`) | Michroma |
| `blizzard` | Blizzard (`SC BLIZZARD-REGULAR`) | Saira |

A HUD should look right with either; `hud.html`'s *As without Remastered* (or
`?fonts=shipped`) draws the second column. `SCHudPlayer.remasteredFonts(false)` is the call.

**Where the game keeps them.** Hyperion keeps HUDs in `huds\` in the player data folder, beside
`settings.json`, which names the chosen one by file name, for all races or one per race
(Hyperion's Settings screen, later).

**What a package does not carry.** The game draws the slots' own content (the minimap, the
counts, the buttons' faces), so their text colours and sizes are the game's, not the HUD's. The
mock dresses them by the family of the style worn (`hud.html`'s `.hud-r8` rules for the R8
style), so a package whose style the page does not know draws its slots in the plain dressing.
The dark mech styles go one step towards the provisional `slots` block below without
changing the format: the page dresses their slots with the R8 rules in the style's own palette
tokens (`text`, `figure`, `label`, `accent`, `lamp`, `lcd`, `lcdOff`), set on each screen that
wears one (`dressSlots` in `hud.js`), so the same layout in two of them side by side reads in
two sets of colours.

**A style carries its slot dressing.** Decided 2026-10-11: a `.hud` must draw the same on a
page that has never seen its style family, so a style may hold a `slots` block (A design of
your own, The slots' type). The page's family dressing stays as
the fallback for a style with no `slots` block, so the R8 and R9 styles draw as before.

**The manifest.** The `package` block may also list what the design offers
(A design of your own, The manifest). The checker reads it and reports its mistakes as notes.

### What the engine reads of a package (Hyperion's addition)

Hyperion reads a player's design from a folder of `huds\` in the data folder, or from a `.hud`
zip beside it. A package is unpacked into `huds\.unpacked\<its name>\` and then read as a folder
is, so the two load the same way; it is unpacked again whenever it is newer than the unpacked
copy, whose `package.stamp` holds the package's last write. What an earlier unpack left is deleted
first, so a file taken out of the package is gone (`port_hud.cpp`, `Unpack`, `PackageEntry`).

The entries it takes:

| Entry | What it is |
|---|---|
| `hud.json` | the layout; a design without it is not listed |
| `style.json` | the style it wears, one for the design |
| `parts/<name>.json` | parts by name (Parts) |
| `files/<name>` | the design's own images and sounds: a PNG, a WebM or an Ogg by its first bytes |

Anything else is left out with a note naming the entry and why: a name that leads out of its own
folder or starts with a dot, a `files/` entry that is none of the three kinds, and every other
path ("a package holds hud.json, style.json, parts/ and files/"). An entry that does not unpack is
left out with a note of its own, which is what an encrypted or zip64 entry comes to; the zip
reader takes stored and deflated entries alone, and no entry over 256 MB (`port_zip.cpp`). The
limits are 512 entries and 64 MB unpacked in all, counted from the archive's own directory before
a byte is inflated; past either, the rest is left out with a note. None of these notes stops the
design loading.

**Where it differs from the design center's reader.** The style is `style.json`, one for the
design, not `styles/<name>.json` one per race, and the layout's `style` and `package` fields are
not read: a package carrying several styles loses all but the one named `style.json`. A `files/`
entry is taken by its first bytes rather than its extension, and a WebM video is allowed
(Animated images). A name with a folder before it (`my-hud/hud.json`, which is what zipping a
folder from the file manager makes) is left out rather than read from inside the folder, so a
package is zipped from its contents. The limits are the ones above, not 16 MB.

### Checking a package

`SCHudPackage.check({ entries, size })` returns `{ errors, warnings, layout, styles, files }`;
the layout and the style it wears then go through the layout's own checks (Checking a file),
and the notes of both show together. An error stops the load and nothing on screen changes; a
note does not.

## Hyperion as built

What Hyperion's engine does where it differs from this page, as it stood on 2026-10-23.
Hyperion keeps its own copy of this page, `engine/docs/hud-format.md`: this text, plus sections
marked "Hyperion's addition" (Parts,
Buttons and a layer's `action`, animated images with `frames` and WebM, the engine's checker
`Hyperion.exe --hud-check`, live reload, and the fonts).

**Packages.**

- **Styles.** Hyperion reads one style, `style.json` at the package's root. A `styles/` entry is
  left out with a note, and the layout's `style` and `package` fields are not read. This page,
  and `hudpackage.js`, which `hud.html`'s *Export .hud* uses, put the styles in
  `styles/<id>.json`, one per race. So a package exported here loads in Hyperion without its
  style. Not settled.
- **Folders.** A zip holding the package's folder (`terran-r9/hud.json`) is refused. Zip the
  folder's contents.
- **Limits and file kinds.** Up to 512 entries and 64 MB unpacked. A `files/` entry is judged by
  its first bytes, not its extension, and WebM is accepted beside `.ogg` and `.png`.

**Text.** No MSDF. Glyphs are rasterised at their drawn size, so a text's `shadowBlur` is not
drawn and an outline is offset copies (Layers, Text).

**Art.** `hd:` art and `data:` URLs are checked but not read yet; a layer using one draws the
missing-image plate. `sd:` art is read (Assets, "What the engine reads of an asset"). Mech and
R9 use none of their own; the tooltip's cost icons are 1.17's (Tooltips).

**The `time` slot.** Never filled: the user wants no game timer in the skins. Hyperion's copies
of the Mech layouts leave out `btnWell3`, `timeCell` and `time`, and R9's `clockBox` and `time`.
The design center's keep them.

**Tooltips.** Built. They show:

- for the card's buttons: the name with its key letter marked, the key, the costs with the
  lacking ones marked, and 1.17's lines under them (for a grey button, why it is grey);
- for 1.17's other console bubbles (its context help);
- for the HUD's own buttons: idle, army, the groups and a layer's `action`.

The player's selection-box colour sets `tipAccent`, `tipAccentHot` and `tipAccentGlow` as
Tooltips says. A Settings check, "The design's tooltips", turns them off and gives back 1.17's
bubble.

**The band.** Built as The band says: `band`, `outside`, a value's `band` and the
checks. The engine session reported this on 2026-10-23. Its placement agrees with
`hudplayer.js` within half a unit in 128 placements (every style, both resource picks, Width full
and 16:9, at 4:3, 16:9, 2560 x 1080 and 5120 x 1440).

**The raise.** Built, with two differences from Raising the map. Hyperion's copy of
this page calls them "The raise in Hyperion":

- **No black strip.** Under the shortened view the map goes on under the HUD, as it does under
  any HUD. A click there that no panel takes is a map click.
- **1.17's camera limits stay.** Hyperion keeps the game's own view, which commands and sounds
  read, and moves only the camera's picture. Near the map's top edge the raise gives way: 0 with
  the view's top on the map's top, rising to the full N half a view lower. So the map's top rows
  can still be seen without changing 1.17's clamp. Away from the top edge it is exactly N (60 px
  measured at N = 60). The minimap box's top comes down and its foot stays.

Its Settings row is the one drawn here, placed last in the HUD tab, after Buttons.

**The columns.** Hyperion's addition, "The columns in Hyperion", reported on
2026-10-23. The user asked for it: "much easier to simply have the map view on left or right,
depending on hud location ofcourse, to have the map contained in a more easier square". A layout
whose panels stand the screen's whole height from its left or right edge is a column, as
`mech-flank` and `mech-flank-right` are. Hyperion draws the map beside a column, not under it.
The view is the part of the screen left over: its middle is that part's middle, the map's edges
meet the column's, and the minimap box is as wide as that part. A column does not use the raise.
The HUD tab greys the row, with a note that nothing is under the map. A file needs no new field;
the panels' places are enough. `hud.html` still draws the map under the column.

**The HUD tab's preview.** Hyperion's tab draws the chosen design over Bottleneck at the screen's
shape, with the engine's own HUD renderer. It follows every pick as it is made: layout, size,
material, width, light, hidden buttons and the raise.

**One unit's icons.** The frames of the armour, weapon and upgrade icons take the selection
colour, as the boxes do. So, since v0.6.15-beta (the user: "the overlay on the units when training
to utilize the accent color like we did with the armor and attack power readings"), do a
building's queue (its boxes, the numbers of its empty places, the arrow from the unit being made),
the box of an add-on, research or upgrade being made, a passenger's box in its normal state, and
the progress bar's empty part; the bar's fill keeps its green (`PortConsoleHudSelectionBoxes`).
1.17 draws the full bar's picture over the empty one, and the full one's frame is clear, so the
frame shortened as the bar filled; under a design the empty picture's edge is drawn over the fill
too, so the bar keeps its whole frame (`PortConsoleHudBarEdge`; the user: "the outside overlay
line dissappears as progress goes on"). Classic keeps 1.17's bar.

**Replays.** Built on Hyperion's branch `hx-replayhud` (2026-10-25), as Replays says,
with these choices of the engine's:

- **Watching is a replay.** A match watched from a LAN room or the AI lab plays as a replay, so
  it wears the replay layout too.
- **Options.** A replay layout's options are its own (`<design>.replay.option.<id>` in Settings),
  but one the game layout has as well, Width, follows the game layout's pick; Settings' HUD tab
  shows only Replay position, and Replay TV and Follow among the buttons a player may leave out.
- **The players' board.** Players with a seat in the lobby, in its order. With `split`, the
  sides are the teams when the players' teams differ, else the first half and the rest. A side
  with more players than fit at `least` ends in "+N more · F1", and the eye is the
  page's almond, left out past the eighth slot: 1.17's replay vision is a mask of eight players.
  APM counts commands, so a computer player's is 0, and up to 4,096 of them a minute, past what a
  bot of SSCAIT's makes (1,024 held the count there).
- **The buttons.** Play, Slower and Faster call 1.17's own (`order_replay_*`) and are greyed by
  its own checks (`bf_replay_*`). An eye calls View Players' vision change directly, as View
  Players' checkboxes and OK would. Replay TV's and Follow's keys are the player's from Settings'
  Hotkeys (F12 and F11 unless changed).
- **The Diplomacy slot** says View Players in a replay.
- **The live channel.** `hud_press` takes `play`, `slower`, `faster`, `tv` and `follow` as
  buttons and `eye` with a slot; `hud` gives the replay's values and a `players` list.

**The players' stats.** Built on Hyperion's branch `hx-replayhud` (2026-10-25), as The
players' stats says, with these choices of the engine's:

- **Who calls them up.** A replay, which is also how a LAN room's match and a game of the AI lab
  are watched. In a game the player plays, Top vs Bottom included, F1 is 1.17's Help and Shift+F1
  nothing new. 1.17 has no observer seat; `IsTVBGame()` is Top vs Bottom, not an observer game.
- **F1 before Help.** 1.17 makes F1 Help through its accelerators, before the window sees the
  key; the stats take their keys ahead of them. Help is still on the game menu.
- **Without a design, or a layout without the view.** The engine draws the view in a place of its
  own in the interface's colours (Settings' Interface theme): the side view 300 × 540 units at the
  right edge from 112 down, on its backing; the popup at most 1000 × 640 in the middle, on a plate.
- **The figures.** Income is what was mined over the last minute of game time, from 1.17's
  gathered totals, scaled over the game so far until a minute has gone, and none for its first ten
  seconds. The army is the units made and alive that fight: not buildings, workers, Overlords,
  Interceptors, Scarabs, mines, missiles, Larvae, eggs or cocoons, nor hallucinations. A value is
  minerals and gas as `units.dat` prices them. Units lost and killed are 1.17's counts, without a
  Scarab, a mine or a missile spent.
- **The graph.** A sample of every player every 48 game frames (two seconds); past 2,400 every
  other is dropped and the step doubles. A replay taken back drops the samples past its frame.
- **The units' icons** are 1.17's command icons in the console's colours; a type without one shows
  its initials.
- **The teams.** The popup's teams are the lobby's. A player without a team stands alone, with no
  heading, and the header counts only the lobby's teams.
- **The clock.** As this page says: none from the engine in a replay layout without `time` or
  `apm`, and never under a panel.
- **The live channel.** `hud_stats {side?, popup?, page?}` turns the views and the page, as F1,
  Shift+F1 and Tab do; it says whether each view is placed by the design.

**A design of your own, in part.** Built on Hyperion's branch `hx-observe`
(2026-10-25), with the round's replay layouts imported: the full-width replays without the board,
one time cell over the deck's line, and the line a slider. What the engine does:

- **Actions.** Every name of Actions is read: the buttons', the groups', the replay's, the stats'
  and the seats' vision. A press does what the slot of that name does, through the same handlers
  (`PressAction`, `port_hudinput.cpp`); `stats.popup` and `stats.side` turn the view as F1 and
  Shift+F1 do, `stats.page.*` turns the popup's page, `vision.N` is the seat's eye. A button is
  shown as hudActionShown says, and greyed at 40% with no press when its `enabled` is false, its
  name unknown, or `vision` stands outside a row. `label` names the button in its tooltip. A
  `panel.press` timeline may name its `action`.
- **The replay's position.** A layer with `replay.seek` is the slider of Moving through a replay:
  a press takes the replay to that share of the box (from the side `meterFrom` names), a drag
  goes on as the pointer moves with `replay.seeking` true, and the release gives the
  `replay.seek` event. With the pointer over it, Left and Down go back 10 seconds and Right and Up
  on, Page Down and Page Up a minute, Home and End to the ends, ahead of the camera's arrows. A
  seconds' worth is the replay's own speed's. The replay keeps its pause, speed, vision and Show
  All through a move, and plays on from there exactly as it plays from the start: the engine keeps
  the game's state every 240 frames while a design that can move the replay is worn and puts back
  the nearest one before the frame (`port_replayseek.cpp`, the trainer's snapshots). Sound is off
  while a move plays. A game watched live cannot be moved: the slider is greyed.
- **`ride`, `start`, the live contract and `teams.count`** as this page says. `start` applies when
  a layout is put on for a game or a replay, not on a change of style; a stats view set before by
  other means stays as it was.
- **No end banner.** `game.victory` and `game.defeat` fire for the local player only, never in a
  replay. Hyperion's copies of the layouts have no `banner` panel (the user's choice,
  2026-10-25): the game menu's Victory and Defeat box ends a game, and a watched skirmish's
  "Game Over" box. The Mech styles no longer have `winText`, `loseText` or the `banner` look.
- **Not drawn yet.** `each`, `icon` and `chart` layers are read and checked but draw nothing, and
  the `p.*`, `t.*`, `players.N.*` and `top.*` names are not bound: the Mech replay layouts use them
  only in `statsSideDrawn`, which is hidden, so the side view is the engine's (`stats` slot). A
  style's `slots` and the package's manifest are read as before, without their later fields.

**The music player (2026-10-24).** Built on Hyperion's branch `hx-music` (uncommitted when its HUD
session reported it), as The music player says, with these choices of the engine's:

- **Colours.** All 59, laid over D's skin; `rimOff` and `well` take their defaults when a style
  leaves them out.
- **Fonts and sounds** as above. A press on a greyed button or a greyed menu item plays `denied`.
- **The plates are a panel.** A style with materials adds a hidden panel of kind `music`, a layer
  per plate, drawn by the HUD's shape shader; the windows then skip their own fill, rim and face.
  A layout that already has a panel called `music` is an error with that style, as `tooltip` is.
  `lit` falls back to `button`.
- **States as washes**, where `musicplayer.css` uses filters: a greyed button's plate at half
  opacity (the same); a hovered one under a white wash of about 0.09, 0.2 while pressed, with the
  glow ring (here, brightness 1.25 and 1.6); an unfocused title bar under a black wash of 28%
  (here, brightness 0.72 and saturation 0.6).
- **The plates lie under the whole overlay.** Where one window or a menu lies over another, the
  one below is drawn around it. Here each window is its own stack; since the second pass the menus
  are drawn on the overlay over every window, as Hyperion's are.
- The game clock and APM counters are no longer drawn under any overlay layer's panels.
- **Drop shadow.** A window's or menu's plate gets the mock's shadow, drawn by the overlay as soft
  fills outside the plate.
- **The key** is Ctrl+F9 unless the player changes it in Settings, Hotkeys, because F9 is the
  zoom's reset in Hyperion. The user kept it (2026-10-24), and the mock now uses it too. It sits
  under an **Addons** heading that ends the Hotkeys tab (UI_FACE_WIDE 15 in `kScInkHead`, as the
  benchmark's section heads), and the Sound tab's note points there ("Hotkeys, Addons"). The
  Settings mock draws the same heading (a setting's `section` in `front-data.js`).

Checked in a game with `mech-span` in the three materials, `terran-r9` in `terran-r8` and
`mech-flank` in gunmetal; each close to `hud.html`. Classic keeps D's skin.

**The music player's sizes, window shade and menus (2026-10-24).** Built on `hx-music` as Sizes,
window shade and menus says, with *Playing on this computer* on Windows' media sessions; the
section and the table rows above are in the engine's `engine/docs/hud-format.md` word for word.
Checked in a 1920 x 1080 run against `hud.html` (shaded, the options menu): shading in any order
and back restores every box exactly, and each size measures right (412 wide at 150, 344 at 125).
Its own choices, none of which the mock vetoes:

- **Tooltips** sit 40 pixels under the pointer at 1080, so the game's arrow does not cover them.
  The mock hangs them 10 under the control, and keeps that: its pointer is the browser's.
- **iTunes and its playlists** (the user's ask, the same day). iTunes for Windows publishes no
  media session, so Hyperion reaches it through its COM interface (`iTunes.Application`). While
  it is open it is an app row like the others, listed last, with its art, its track and all five
  capabilities lit. After the app rows, under a hairline rule, a heading, *iTunes playlists*, in
  the title face at the label size (4.6) in the dim ink, tracked 0.06; then a row per playlist at
  the player list's row height, the name at the left and the track count at the right in the dim
  ink: *Music*, the whole library, first, then the player's own playlists that have tracks. A
  click opens one: the heading gives way to a head row, "‹ " and the name in the bold face and
  `inkHi` with "40 tracks" at the right, under a rule, and its tracks follow as the player's own
  list draws them (number, artist – title, length), the one iTunes plays in the current ink.
  While they are read, a dim note says "Reading its tracks… n of m". A double click plays a track
  in iTunes; a right click gives *Open* and *Play in iTunes* on a playlist, *Play in iTunes* and
  *Back to the playlists* on a track. The footer reads "N apps · media sessions and iTunes". With
  iTunes installed and closed, one dim line: "iTunes shows here while it is open, with its
  playlists." The list scrolls with the wheel, three rows a notch, and its thumb shows where it
  is. The mock draws all of it (`musicplayer.js`, `IT`; the page's *iTunes, a playlist opened*
  and *iTunes closed*). On the mock's suggestion, since several apps put the playlists below the
  fold, an opened playlist's head row goes to the top of the list, the jump made again as its
  tracks come in; it stops once the head is at the top, every track is read or the wheel moves
  the list, and *Back* returns to where the playlists were. Hyperion took it into v0.6.11-beta
  (`port_musicview.cpp`, `ITunesGo` and `SystemList`; checked with three sessions above
  iTunes and Music's 3,710 tracks), and the mock now stops on the same three. Enter playing the
  chosen track is the mock's alone.
- **The shaded equalizer's preset well**, under that source, is a refused press (`denied`) whose
  tooltip says "The equalizer shapes what Hyperion plays". The mock now says the same; it said
  "Pick another" there.
- **Smaller, on the user's word** after trying it at 5120 x 2160: sizes 75 to 200 with 100 the
  default, the windows' floors and least label shrinking with the size under 125, the menus and
  tooltips keeping theirs, and `scale` in `music.json`. The mock follows (Sizes, above). The floor
  factor counts Double size, as the mock's does, so 75 doubled keeps the full floors (the build
  installed in `E:\Hyperion`; the engine's `addons.md` and `hud-format.md` say so).

## Version 2 files

A version 2 layout (panels by `kind` with no layers) loads as version 3. Each
visible panel keeps its place and becomes a framed slot: a `rect` (`#0a0e18e6`, a 2-unit
`#3b4755` edge, radius 4) and the slot its kind names (`minimap`, `selection`, `portrait`,
`command` to `card`, `clock` to `time` and `apm` side by side, `resources`, `chat`, `groups`,
`idle`), with `clamp: true` as version 2 had. The addon kinds (`macro`, `formula`, `apm`,
`retreat`) have no slot in version 3 and come up as a titled frame, with a note.

## Not in version 3

CSS filters, SVG filters, backdrop blur anywhere but a shape's `glass`, blend modes beyond
normal and add, 3D transforms,
anything that lays out like a web page, text that changes per key, and layers that act on the
game. The mock draws with a 2D canvas, WebGL2 for shapes and CSS, but nothing in the format
depends on that: the canvas work stands in for the presenter's draws and the CSS for its
scissor and alpha.
