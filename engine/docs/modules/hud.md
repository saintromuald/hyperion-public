# hud: the HUD designs, their files, drawing and input, and Hyperion's selection

## Overview

A HUD design replaces 1.17's console during a game with a HUD of Hyperion's own:
panels drawn by the GPU presenter at the window's
resolution, made of procedural shapes, text and images, lit and animated by timelines. The
player chooses one in Settings' HUD tab: Classic, which is 1.17's console by race, Mech, or a
design of their own from the data folder's `huds\`. R9 is held back from players while it is
worked on (`PortHudDesignHeld`, the user's decision of 2026-10-27): the tab shows it greyed,
"Not available yet", and a game whose saved design it is shows the classic console;
`HYPERION_HUD` still names its files.

A design is a pair of files in the HUD format, version 3: a layout (panels, layers, slots,
bindings, timelines, options) and a style (materials, palette, looks, lights, sounds, since
round 12 a tooltip, and since 2026-10-24 the music player's skin). The format is `../hud-format.md`, Hyperion's copy of the design center's,
with Hyperion's additions marked. The engine reads and plays the files as the design center's
player (`hudplayer.js`) does, so a file checked there means the same here.

1.17's console does not go away under a design. Its dialogs live on and draw into their own
bitmaps. The design's slots show those pixels (the minimap, the selection, the portrait, the
card) and pass a press on a slot to the dialog under it, so every press ends in 1.17's own
handler and sends the command a click on 1.17's console sends. The HUD is presentation only: a
replay plays the same with any design or none.

Hyperion's selection (stage S) came with it and is a simulation change: in a game by Hyperion's
rules a player selects up to 255 units, own buildings together, with SC2's subgroups. It is
described at the end of this page and in `replays.md` (the option) and `gameplay.md` (the keys).

## State

On master since 2026-10-23 (the merge of `hx-hud`). Built: the data layer (H0), the format in C++
(H1), the drawing (H2), input (H3), Hyperion's selection (S1 to S5), timelines, sounds and
animated images (H4), the HUD tab (H5), player-made designs with parts, packages, the checker
and live reload (H6), round 12's tooltips with Chakra Petch, round 13's band and raise, and
round 14's columns beside the map and the tab's live picture. Since v0.6.11-beta
(2026-10-24): the music player's skin in each style (below). On `hx-observe` (2026-10-25), part
of round 17: actions, the replay's slider, `ride`, `start` and the live contract (Replays,
Moving through a replay). The designs: Mech (full width,
its minimized size, a left column, a right column; gunmetal, graphite, blackout), R9
(`terran-r9` in `terran-r8`'s style) and Classic. Remastered's design is left out while its HD
art is held, and the detached pods are not taken.

hxsuite's `h2_wide_select` records a game of Hyperion's selection on the wide selection test
map (`tools/hxmap/widemap.py`; `../replay-test.md`): selections of up to 255 in chunks, buildings
training together, a control group of the selection; its replay and its save play as the live
game did on both kinds of files. The SDL renderer and the browser build draw no HUD and keep Classic (`PortHudPlays` needs the
GPU presenter); there the tab offers Classic alone and says the designs need the GPU renderer.

## Key files

| File | What it holds | Main functions |
|---|---|---|
| `port/hud/port_hud.cpp` | the design a game shows: Settings' `hud` section or `HYPERION_HUD`, its files found and read, played, its events and clock, its sounds, live reload, the player's designs listed; the music player's skin and plates | `PortHudPlays`, `PortHudFrameBegin`, `PortHudDesigns`, `PortHudRead`, `PortHudTip`, `PortHudReload`, `PortHudMusicSkin`, `PortHudMusicPlates`, `PortHudMusicSound` |
| `port/hud/port_hudfile.cpp` | a layout and style read, checked and compiled: validateStyle, relight, choose, wear, validate, as `hudplayer.js`; parts expanded; tooltips worn; the music player's block resolved and its plates' panel added | `HudLoad`, `HudParseColour` |
| `port/hud/port_hudbind.cpp` | the binding language in `{...}`, compiled once and evaluated each frame | |
| `port/hud/port_hudplay.cpp` | a design played: layout at a screen's shape, each layer's state this frame, timelines, events, the tooltip's layout, the music player's plates | `HudPlayer::Layout`, `Frame`, `Fire`, `Advance`, `Tip`, `MusicPlates` |
| `port/hud/port_hudshape.cpp` | the shape layers' tables and path outlines, as `hudshape.js` | `HudFlatten` |
| `port/hud/port_hudstate.cpp` | the data layer: what bindings and events read from the game, and the presses its buttons make through 1.17's handlers; a replay's state, its events and the players' board | `PortHudGet`, `PortHudFrame`, `PortHudPress`, `PortHudPressButton`, `PortHudPressGroup`, `PortHudPressEye`, `PortHudReplayWatch` |
| `port/hud/port_hudinput.cpp` | the mouse over the HUD: hits, a slot's press given to 1.17's dialog, drags, hover, the tooltips | `PortHudMouse`, `PortHudOver`, `PortHudTipShow`, `PortHudTipHide` |
| `port/hud/port_hudstats.cpp` | the players' stats a viewer calls up: the keys, the figures and the graph's samples, both views drawn in a stats slot or in a place of their own, a layer over the game | `PortHudStatsSet`, `PortHudStatsFrame`, `PortHudStatsFigures`, `HudStatsDraw`, `HudStatsEye` |
| `port/hud/port_hudcheck.cpp` | `Hyperion.exe --hud-check` | |
| `port/render/port_gpu_hud.cpp` | the drawing: each layer into a buffer, its effects, glow and shadow, runs laid on the screen; the slots' console pixels; the HUD tab's picture | `GpuHudDraw`, `GpuHudPreview` |
| `port/render/shaders/hud.hlsl`, `hudshape.hlsl` | the HUD's passes and the shape shader, made from `hudshape.js` by `tools/hud/export_hud_shader.mjs` | |
| `port/render/port_console.cpp` | the console under a design: no art, dialogs held, their pixels and parts; the selection grid's boxes and wireframes, and the buildings' | `PortConsoleHudOn`, `PortConsoleHudSource`, `PortConsoleHudPart`, `PortConsoleHudBox`, `PortConsoleHudWire`, `PortConsoleSmallWire` |
| `port/front/port_front_hud.cpp` | Settings' HUD tab and its live picture | `HudTab` |
| `port/data/port_zip.cpp` | zips read in memory, for `.hud` packages and `.hxm` maps | `PortZipDirectory`, `PortZipRead` |
| `port/gameplay/port_selwide.cpp`, `h/selectlimits.h` | Hyperion's selection: the rows of 255, subgroups | `SelectionWide`, `SelectionMax`, `SelectionSubgroupStep` |
| `engine/huds/` | the built-in designs' files, packaged to `Data\huds\` | |
| `tools/hud/` | the shader export, the layout golden (`hudgolden.mjs`) | |

## How it plugs in

As a game's console loads, `PortHudPlays` reads the wanted design. When it plays,
`PortConsoleHudOn` is true for that game: 1.17's console art is not drawn, the map has the
view's whole height, and the console's dialogs are held, drawing into their bitmaps but not put
on the screen and taking no mouse of their own.

A design chosen or left in Settings during a game builds the console again between frames
(`PortConsoleUpdate`, `port_console.cpp`), as the trainer does: `status_free` and `status_init`
with `PortConsoleRebuilding` true, so `status_init` keeps the HUD's record of the game and
`statport_init` puts back `portdata.dat`'s string ids, which its first run turned into pointers.
The selection is picked again from the game's. `PortHudFrameBegin` draws no design over the
classic console, so the two never show together.

Each frame the presenter calls `GpuHudDraw` (`port_gpu.cpp`) after the game's frame.
`PortHudFrameBegin` returns nothing until the game starts (`PortHudGameStarted`). A skirmish
from the menus starts it earlier, as the map comes out of black: `port_intro.cpp`'s arrival calls
`PortHudArrive` at 1.0 s, as 1.17's console would rise, so the design plays its intro before the
countdown, not after it (the user, 2026-10-23). It fires the data layer's events
into the player, advances the timelines on the window's clock (`SDL_GetTicks`, a stall taken as
a tenth of a second at most), plays the cues and fills the bindings' values. The drawer draws
again only the layers whose content changed.

A mouse event reaches `PortHudMouse` from `event_Win.cpp` before the game's own handling. Over a
slot it is given to the held dialog at the place 1.17 has that part (`PortConsoleHudPart`), and a
drag begun there stays with it until its button is up. Over the HUD's own buttons (idle, army,
groups, a layer's `action`) it presses through `port_hudstate.cpp`, which calls the handler
1.17's console or its keys call. A group's button recalls the group as its key does; with Ctrl,
or pressed with the right button, it makes the group of the selection, and with Shift it adds
the selection to it, as Ctrl and Shift with the key do (`PortHudPressGroup`'s `nHow`,
`send_net_cmd_GroupUnits`). `console_collision` (`cursor.cpp`) asks `PortHudOver` wherever the
pointer is, before 1.17's test of the console's band, so the map takes no press on a panel and
the cursor is the console's there, a column's upper part and the resources at the top included.
1.17's two other places of its console go through the slot the pointer is over
(`PortHudConsoleCursor`): the minimap's rectangle, where `DetermineCursor` shows the minimap's
cursor (the targeting one while a target is chosen), and the card's, where
`game_rmouse_down_hook` takes a right click as the console's. So beside a column, or above a
lower HUD, the map there has the magnifier over a unit and takes a right click as an order, as it
did not before 0.6.10.

## Designs and their files

The built-in files are in `engine/huds/`: `mech-span.json`, `mech-span-min.json`,
`mech-flank.json`, `mech-flank-right.json`, `terran-r9.json`, and `styles/mech-gunmetal.json`,
`mech-graphite.json`, `mech-blackout.json`, `terran-r8.json`. They are copies of the design
center's round 13 files, built there from its parts (`huds/kits/build-mech.mjs`,
`build-r9.mjs`). `tools/hud/import_builtins.py` copies them and makes Hyperion's changes, each
file's `note` saying which:

- no game timer in the skin (the user, 2026-10-22): the `time` slot and its cell are left out;
- no flash on a new selection: the `select.change` timeline is left out;
- the selection's grid at most 8 columns (`GRID_MOST`);
- the styles' tooltip shows and goes at once: their `tip.show` and `tip.hide` timelines are left
  out.

Take a new round by running it again. It stops when a layer it changes is gone from the design
center's files. The files hold no Blizzard art: the mech and R9 designs are procedural. The
builds put them beside the program, and `package.py` puts them in `Data\huds\`.

A player's design is a folder of `huds\` in the data folder holding `hud.json` (the layout) and
`style.json` if it has one, or a `.hud` package, a zip of the same. Parts files and a package's
`parts\*.json` give parts by name (`../hud-format.md`, "Parts"). The tab lists them by their
`name` after the built-ins.

`HYPERION_HUD=<layout.json>[;<style.json>]` plays a design straight from its files, for its
maker. The active design's files are looked at once a second and read again when one was saved,
a refused one too, so a fix shows in the running game. `Hyperion.exe --hud-check` checks a
file, a folder or a package without a game and prints `plays`, `errors` and `warnings` as JSON
(`../hyperion-exe.md`).

## The band, the columns and the map's raise

Round 13 (the user, 2026-10-23: the HUD "not go full width and instead just extend the length
like remastered does to 1920px", and "a special global setting to tweak by px count a offset of
the map view").

**The band.** Each built-in layout's first option, Width, is Full screen or 16:9. A picked value's
`band` is the file's band (`Choose`, `port_hudfile.cpp`): `HudPlayer::Band` centres it on a
screen wider than it, and `PlaceAll` places every panel in it as `hudplayer.js`'s `placePanel`
does. A panel with `outside` fills the margin from the screen's edge to the band's: the filler
deck of the span layouts and R9, the plated wall behind a column. With no margin it is not drawn
and takes no pointer, though its timelines run. The tooltip panel is placed against the screen.
Only the panels move; the view, the camera and map clicks are the whole screen's.

**The raise.** Settings' `mapRaise`, 0 to 200 units of a 1080-high screen, the same for every
design (`PortHudMapRaise`). While a design's console is on and the GPU draws the map's render
view, `CameraUpdate` (`port_view.cpp`) puts the camera's sight that many screen rows lower in the
map. The map is drawn higher, the view's top goes off the screen, and units along the screen's
foot come out from under a HUD tall at its sides. Every reader of the camera follows:

- the render view and the presents (`PortViewCamera`);
- clicks and drags (`PortViewScreenToMap`);
- the minimap's box (`PortViewSeenRect`), whose top comes down while its foot stays;
- Ctrl+click and a double click, which take the units of a type in what shows (`UselScreenRect`);
- the wheel's anchor.

1.17's view and its limits stay as they are. So the raise gives way near the map's top: it is 0
with the camera's sight at the top edge and full half a view down, and the map's top rows still
come into sight. Presentation only: the game's own view, which its commands and sounds read,
does not move. The tab's row is a slider (the arrows a pixel, Shift or Page Up and Page Down 10,
Home and End its ends, while the pointer is on it) and Reset. A column layout has no raise: the
tab greys the row and says why.

**The columns.** Round 14 (the user, 2026-10-23: for the left and right column "much easier to
simply have the map view on left or right, depending on hud location ofcourse, to have the map
contained in a more easier square"). A column does not cover the map; the battlefield is drawn
beside it. `HudPlayer::Sides` finds the panels standing the screen's whole height from its left
and from its right edge, one against the next with a unit of give, and `PortHudSides` gives the
playing design's widths. While a design plays and the GPU draws the render view,
`CameraUpdate` (`port_view.cpp`, `CameraSides`) takes them in screen pixels, and the camera
places the map in the part that shows (`ViewShown`): the part's middle is the view's middle,
and the map's left and right edges come to the column's edge, not under it. 1.17's scroll limit
across, `glMapPixelRgt`, is widened by the columns' width rounded up to 8 (`ViewScrollRight`, at
the map's start and in each pass while there are columns), so the view reaches the map's right
edge beside a right column. Down, nothing covers the screen's foot beside a column, where 1.17's
view of 400 rows would leave the map's last rows black past its foot: the camera puts the game
view's middle at the screen's middle and holds the screen's foot on the map's
(`PortViewCameraPlace`, `ViewShownRows`). 1.17's limit down is not changed, so a centre-view
trigger's scroll still ends where 1.17's does; the price is a few frames at the map's top and
foot where the game's view scrolls and the picture does not. The readers follow: clicks and
drags, the minimap's box, the part shown (`PortViewSeenRect`), and the centring of a minimap
click, a portrait click, a group's second press and `gamemap_center_on_mtx`, which take half the
shown width (`PortViewShownWidth`). The game's screen stays the window's, so the HUD's own input
is as before. The trigger action that centres the view is left as 1.17 has it.

## Tooltips

Round 12 (the user: "a new way for the tooltip to render. That it matches the skin more, along
with a font type"). A style with a `tooltip` block and `tip.*` looks draws the
tooltip of the button under the pointer in the console's own materials, in Chakra Petch, in
place of 1.17's bubble. `HudLoad` adds a `tooltip` panel at z 100; `HudPlayer::Tip` lays it out
as `hudplayer.js`'s `tipLayout`, `tipPlace` and `tipPath` do, and fires `tip.show` and
`tip.hide` for its timelines. Two changes of Hyperion's (the user, 2026-10-23): the tooltip is
placed against the button itself, above it as 1.17's and Remastered's are, where the design
center places it against the button's panel; and it shows and goes at once, as the original's
does, so Hyperion's styles leave out the `tip.show` and `tip.hide` timelines (each style's
`note` says so).

A cost is drawn with the game's own icon when the style names none of its own: `HudLoad` adds
assets `tip.art.*` of `sd:game/icons#<n>`, minerals, and the gas, supply and energy of each
race, and the tooltip shows the race's that the button's costs are in (`HudTipContent`'s
`anRace`: the console's race for gas, the race whose supply or energy the button costs). Its
art is read from the player's files (`ArtOf`, `port_gpu_hud.cpp`), as a later round will
replace it with upscaled art.

Three kinds of button give one:

- **The card's.** `DrawTooltipText` (`statcmd.cpp`) asks `StatCmdHudTip` first: the button's
  name from `stat_txt.tbl` with its key's letter marked, the key (Esc for 0x1B), the lines
  1.17 writes under it or, for a grey button, the reason, and the costs from
  `sgwStatCmdTooltipData_g`. A cost the player lacks is drawn short. A string's first byte is
  its key and its second the kind of its costs, 0 for a button with none (Stop, Set Rally
  Point, Cancel), so its text is found from the third.
- **The console's other bubbles.** `ctxhelp_show` (`CtxtHelp.cpp`) gives its text: the first
  line the name, a short "(key)" at its end the key. `ctxhelp_hide` hides the tooltip too.
- **The HUD's own buttons.** Idle worker, army, the groups and a layer's action have no bubble
  of 1.17's; `OwnTip` in `port_hudinput.cpp` names them and their keys from `port_keys`.

`PortHudTipShow` finds the control's box on the screen, through the card's place or the least
part of a slot holding the point, and its corners through the slot's transform. When no design
plays, its style has no tooltip, or the player turned "The design's tooltips" off, it returns
false and 1.17's bubble is drawn.

The fonts: the format's `eurostile` and `blizzard` are drawn in Saira, `eurostile-ext` in
Michroma, and `chakra-petch` in its 400, 500, 600 or 700 cut by the text's `weight`
(`HudFaceOf`). The four Chakra Petch files are OFL, fetched by `tools/fetch_fonts.py` and built
into the exe as `HYPERION_FONT_PETCH*` (`build.md`). The selection-box colour the player picks
lights the tooltip as well: `tipAccent`, `tipAccentHot` (65% of the way to white) and
`tipAccentGlow` (alpha 70) in the palette.

## The music player's skin

See also `addons.md`. A style's `musicPlayer` block (`../hud-format.md`, "The
music player") skins a music add-on's windows while the design plays; Hyperion has none at
present, so the block is read and checked and nothing wears it. `ValidateStyle` checks
the block in the design center's words. `MusicSkinOf` resolves it into `HudDesign::music`
(`HudMusicSkin`), in `HudLoad` after the palette is worn. The parts it resolves:

- **colours**: each one swapped from the palette and checked as hex;
- **materials**: each part's look whole, as `hudplayer.js`'s `musicPlayerOf` makes it: the
  style's `shape` without `cornerScale`, the look's material preset, the look and the part's
  `set`, swapped. `lit` takes `button`'s when it has none; `shade`, a shaded window's bar, takes
  `title`'s with every corner the title's first;
- **fonts**: each role's font and weight;
- **sounds**: each moment's audio asset, through the style's `sounds`.

`PortHudMusicSkin` gives the skin to a music add-on's windows. It is NULL until the game's first
frame, while no design plays, and when the style has no block. `PortHudMusicSound` plays a moment
through the design's cues (`PlayCue`).

The materials are drawn by the HUD, not by the windows. `Wear` adds a panel of kind `music`
(`MusicPanel`, z 99) when the block has materials, as it adds the tooltip's. The panel holds one
hidden shape layer per plate:

- for each of the three windows, lowest first, one `frame`, one `shade`, one `title`, 14 `button`
  and 6 `lit` layers, named `w0-frame-0`, `w0-button-13` and so on (a shaded window shows its
  `shade` plate in place of `frame` and `title`);
- the menus' four, one a level, `menu-0` to `menu-3`.

Each layer wears the part's look and `set`. A window's layers are in one stretch, so a window
higher up covers all of one below. A layout that has a panel called `music` is an error with
such a style.

Each frame the windows give `PortHudMusicPlates` their boxes in the HUD's units over the game.
`HudPlayer::MusicPlates` puts the panel round all of them, since a panel's layers are drawn into
one run the size of its box (`port_gpu_hud.cpp`), and each plate in it. `MusicLayer` shows a
part's first layers at those boxes and hides the rest. `PlaceAll` keeps the panel on the screen
rather than the band, `Sides` leaves it out, and the hits leave it out, since the windows take
their own mouse. The overlay draws after the HUD, so the plates follow the windows a frame late.

## Replays

Round 15 (the user, 2026-10-25: "currently our replay and observer styl uses the normal game
variant of them"). A layout may name a second layout, its `replay`, that the game wears while a
replay plays (`../hud-format.md`, Replays). Each of the four Mech layouts names one,
`mech-*-replay.json`, which `tools/hud/import_builtins.py` copies from the design center with the
game layouts. A match watched from a LAN room or the AI lab plays as a replay, so it wears it
too; an observer has no other way in.

- **Swapping.** `ForReplay` (`port_hud.cpp`) puts the replay layout in the game layout's place
  while `gPlayback` is set, for a design from Settings and one from `HYPERION_HUD` alike. Its file
  is checked (`ReplayLayout`: a `.json` beside the layout) and read again when it is saved, as
  the game layout is. Its options are read under `<design>.replay.option.<id>`, falling back to
  the game layout's pick for an option both have (Width).
- **The transport.** The `play`, `slower` and `faster` slots are buttons (`PortHudPressButton`)
  that call 1.17's `order_replay_play`, `_slowdown` and `_speedup`, and are greyed by its
  `bf_replay_*` checks: at either end of the speeds and while paused. The engine draws their
  glyphs (`Glyph` in `port_gpu_hud.cpp`), Play turning to Pause while the replay plays. `tv` and
  `follow` are Replay TV and Follow (`port_tv.cpp`), lit while on; Follow says "no unit selected"
  when nothing is.
- **The values.** `replay.speed` is 1.17's speed name (`glues_get_str`, the speed's string)
  with ×2 to ×16 after it; `replay.step` 0 to 10; `replay.elapsed` the replay's clock
  (`replay_dlg_elapsed_seconds`); `replay.length` and `replay.progress` from the replay's frames at its first
  speed's turn. `PortHudReplayWatch` fires `replay.pause`, `replay.resume`, `replay.speed` and
  `replay.vision` when they change.
- **The players' board.** Round 17 took it off the full-width replay layouts (the user: "you
  still have the top bar in observer and replay ... which I think should go away now"); the
  columns' replay layouts keep it. `PortHudState::aPlayers` holds a row for each player with a seat in
  the lobby: colour, name (UTF-8), race, minerals, gas, supply by race, workers, APM, and the
  eye. `HudBoardOf` (`port_hud.cpp`) lays the rows out, as the page's `drawBoard`, for the
  drawing (`BoardPrims`) and the mouse (`EyeAt`, `port_hudinput.cpp`) both. APM counts the
  commands each player's turns carry, where the replay recorder takes them (`net_mgr.cpp`), so
  a computer player's is 0. An eye calls `PortReplayVisionToggle` (`filters.cpp`), View Players'
  vision change without its dialog: it does not go through `sub_441AE0`, which would reseed
  OcRand. 1.17's replay vision is a mask of eight, so slots past the eighth have no eye. The
  eye is the page's almond (`HudStatsEye`). A side with more players than fit at the least row
  height ends in "+N more · F1" (`HudBoardRow::nMore`), which opens the stats popup.
- **Tooltips.** Play or Pause (P), Slower (D), Faster (U), Replay TV and Follow with the
  player's keys, and "Show" or "Hide <name>'s vision" over an eye (`ReplayTip`).
- **Settings.** The HUD tab reads the replay layout too, shows its own options (Replay
  position) after the game layout's, and offers Replay TV and Follow among the buttons to leave
  out.

### The players' stats

Round 16 (the user, 2026-10-25: "perhaps like how in other games there's like a F1 command that
will open the stats ... along with a way for a view where its on the side of the view and
transparent backing"). Two views of every player's figures, each on or off by itself, in a
replay, which is also how a LAN room's match and the AI lab's games are watched
(`PortHudStatsAllowed`); in a game the player plays, F1 stays 1.17's Help. `game.observer` is a
game watched as it is played (`PortLanWatchLive`, `PortLabWatchLive`, `PortWatchLive`): 1.17
has no observer seat, and its `IsTVBGame()` is the Top vs Bottom game type. Such a game has no
length yet, so `replay.live` is true with it, `replay.progress` reads 0 and `replay.length` is
empty (round 17's contract); the Mech's time cell then reads "LIVE 6:10". A computer's level follows its race in both views when the game chose levels or is
watched (`PortHudPlayer::szLevel`). `../hud-format.md`, The players' stats, is the design.

- **The keys.** F1 opens or closes the popup; held past 300 ms it shows only while down. Shift+F1
  shows or hides the side view. While the popup is open Tab and Shift+Tab turn its four pages
  (Overview, Income, Army, Units lost) and Escape closes it. 1.17 makes F1 Help with its
  accelerators before the window sees the key, so the stats' layer is flagged
  `PORT_OVERLAY_BEFORE_ACCEL` and `RunEvtDispatcher` (`event_Win.cpp`) gives it the keys first
  (`PortOverlayKeyFirst`). A menu over the game (the game menu) keeps its keys.
- **Where they are drawn.** A layout places each view in a `stats` slot with its `view`; the
  Mech layouts and their replay layouts have both, in panels shown by `{stats.side}` and
  `{stats.popup}`. `port_gpu_hud.cpp` draws the slot through `HudStatsDraw` in the style's
  colours. A view the worn layout does not place, or both with no design, is drawn by the stats'
  own layer (place 750, `render.md`) in the interface theme's colours: the side view at the right
  edge, the popup on a plate in the middle (`PortHudStatsPlaced` says which).
- **The figures.** `PortHudStatsFigures`, from `PortHudGet` while a view is on: the army (units
  made and alive that fight, by `IsArmyUnit`), its supply, value and units by type; income over
  the last minute of game time from 1.17's gathered totals (`Income`, a ring of the totals two
  seconds apart); units lost and killed and their value from 1.17's counts (`glCounts_g`).
  `PortHudStatsFrame` takes the graph's sample every 48 game frames while a viewer may look,
  and drops what lies past the frame after a replay is taken back.
- **The icons.** The Army page's units are 1.17's command icons (`cmdicons.grp`, a frame a unit
  type) in the console's colours (`sgBmpTIcon_g`), cut into a sheet in the picture atlas's slot
  40 as they are first shown.
- **The mouse.** In a design, `HitAt` (`port_hudinput.cpp`) lets the side view's rows go to the
  game but its "+N more" row; the popup's tabs, close and eyes are presses (`HudStatsPress`) with
  tooltips (`HudStatsTip`). In a place of its own the layer's Mouse does the same. The wheel
  scrolls the popup's rows when they do not fit.
- **The counters.** A replay layout without `time` or `apm` slots gets no clock from the engine
  (`PortHudCountersOff`), and the engine's clock is never drawn under a HUD panel
  (`PortHudCovers`).

### Moving through a replay

Round 17 (the user, 2026-10-25: "a single timer spot above the timeline bar so it makes more sense,
and the time bar on center piece to have a slider element to be able to rewind the replay as
well"). The round's Mech replay layouts carry one time cell over the deck's line, `posTime`
("2:48 / 5:36", or "LIVE 0:41"), a handle that rides the line (`seekKnob`, `ride`), and over the
line a `seekHit` rect whose action is `replay.seek`.

- **The press and the drag.** `PortHudMouse` (`port_hudinput.cpp`) takes a press on such a layer
  as the start of a drag: the replay goes to the point's share of the box (`SeekAt`, from the side
  its `meterFrom` names), again as the pointer moves, and the release ends it
  (`PortHudSeekDrag`, then `PortHudSeekEvent`, which gives `replay.seek`). `replay.seeking` is
  true during the drag. The move itself is `PortReplaySeekTo` (`replays.md`, Moving through a
  replay).
- **The keys.** With the pointer over the slider or a drag on, the `hudseek` overlay layer (740,
  `PORT_OVERLAY_BEFORE_ACCEL`) takes Left and Down (10 seconds back), Right and Up (10 on), Page
  Down and Page Up (a minute), Home and End, ahead of the camera's arrows and 1.17's accelerators,
  and not while a message is typed. A second is the replay's recorded speed's.
- **What follows a move.** `PortHudRewound` drops the APM commands past the frame, the stats'
  samples go back with it (`PortHudStatsFrame`), and while a move plays `replay.elapsed` and
  `replay.progress` read its target, so the handle and the cell move at once.
- **Shown and greyed.** As hudActionShown: the slider is in a replay only, and its `enabled`
  (`{replay.live ? 0 : game.replay}`) greys it while a game is watched live, at 40%, with no hit.
- **The tooltip.** "Replay position" (the layer's `label`), the time over the length, and the keys.

### Round 17's other parts

- **Actions** (`HudAction`, `port_hudfile.h`): a layer names what it does, and a press does what
  the slot of that name does (`PressAction`): the console's buttons, the groups, the replay's
  buttons, `stats.popup`, `stats.side`, `stats.page.next|prev|<page>`, `vision.0` to `vision.7`.
  `ActionShown` and `ActionGreyed` (`port_gpu_hud.cpp`) follow the design center's rules; a press
  fires `panel.press` with the action, which a timeline's `action` may pick out.
- **`ride`** places a layer at the lit end of a meter layer of the same panel (`HudPlayer::Frame`,
  from that layer's box this frame).
- **`start`** (`ApplyStart`, `port_hud.cpp`): the stats' views and page, Replay TV, Follow and the
  minimap's terrain, set when the layout is put on for a game or a replay; a stats view set at the
  game's start by other means (`PortHudStatsTouched`) is left alone.
- **New names**: `stats.page`, `teams.count` (a player without a team counts as a team) and
  `replay.seeking`; `replay.length` is empty while live.
- **Read, not drawn yet**: `each`, `icon` and `chart` layers, the `p.*`, `t.*`, `players.N.*` and
  `top.*` names, the styles' `slots` and the package's manifest. The Mech replay layouts use them
  only in a hidden panel (`statsSideDrawn`), so they draw as designed meanwhile.

### The end of a game

`game.victory` and `game.defeat` follow the local player's `gbVictoryState`, and not in a
replay or a watched skirmish, where the recorded seat's result is not the viewer's
(`PortHudFrame`, `port_hudstate.cpp`). The layouts' MISSION COMPLETE and MISSION FAILED banner
(`banner.win`, `banner.lose`) was taken out of every layout on 2026-10-25 (the user: "Can we
remove that wording as a whole, I prefer the old method of the modal popup, but just have it in
our own gui ui style"): the game menu's Victory and Defeat box says it. The victory and defeat
timelines keep their lights. The design session took it out of the design center's builders the
same day, with the Mech styles' `winText`, `loseText` and `banner` look, and
`tools/hud/import_builtins.py` brought those in; round 8's console and the archived layouts keep
theirs.

Open: whether an observer has a console of their own, as a replay does (the design session's
question).

## Settings

The HUD tab (`port_front_hud.cpp`), Settings' `hud` section (`port_hud.h` lists the keys):

| Key | Values |
|---|---|
| `design` | `classic`, `mech`, `r9`, or `user:<folder or .hud>` |
| `layout`, `size`, `material` | Mech's: `span`, `flank`, `flank-right`; `full`, `min`; `gunmetal`, `graphite`, `blackout` |
| `<design>.light` | `#rrggbb`, empty for the style's own |
| `<design>.selection` | the selection boxes, the frames of one unit's slots (its armour, weapons and upgrades, what it is making, its queue with the empty places' numbers and the arrow, its cargo), the progress bar's frame, kept whole over its fill (`PortConsoleHudBarEdge`) and, where the full picture repeats the frame (Remastered's, since 0.6.21), coloured within the fill too, the fill itself keeping its green (0.6.23), and the tooltip's light: empty for 1.17's blue, `light`, or `#rrggbb` |
| `<design>.option.<id>` | the file's options |
| `<design>.replay.option.<id>` | the replay layout's own options; one the game layout has too follows `<design>.option.<id>` |
| `<design>.hide.<slot>` | `1` leaves a button out; never menu, diplomacy or messaging |
| `sound` | `on` plays the designs' sounds; off unless set (2026-10-27) |
| `tooltips` | `off` gives 1.17's bubbles under a design |
| `mapRaise` | 0 to 200: the map raised over every design, in units of a 1080-high screen; empty for 0 |

**The live picture.** Round 14 (the user: "A way to preview how it would look on a small map
would be idea"). Over the pane, the chosen design is drawn as it shows on the player's screen:
at the screen's shape, over Bottleneck's terrain (`MapTerrainRgba`, `port_frontlobby.cpp`, read
from the player's files; Lost Civilization when Bottleneck is not there), with sample readouts.
It is the HUD's own renderer: the tab hands `PortHudPreviewSet` its box, a player of its own
laid out at the box's size, and the picks not yet accepted (layout, size, material, width,
light, the buttons left out), and `PortGpuUiEnd` draws it with `GpuHudPreview` between the
pane's prims, so every change shows at once. A column's picture starts the map beside it, and
the raise moves the map in it. Its box is as wide as the pane and a third of the screen's
height at most.

With Classic chosen, the pane shows Settings' own rows for 1.17's console (Console layout,
Console skin, its rows on the `hud` tab in `port_front_settings.cpp`). The hotkeys of Hyperion's selection are rows of the
Hotkeys tab (`gameplay.md`).

## Hyperion's selection

`HX_OPT_SELECTION` (0x40, `hxrules.h`), recorded by every new game by Hyperion's rules but one on
an EUD map, whose triggers read 1.17's arrays. A game over the network plays the classic rules
and so selects 12. A replay or save without the option selects 12 as recorded.

- **Storage.** `SelectionWide()` is true for such a game, and `SelectionMax()` is 255. The
  selections, groups and the console's units are rows of the port's own (`port_selwide.cpp`),
  reached through `selectlimits.h`'s accessors; 1.17's arrays stay as they are and empty, so
  1.17's saves, the EUD twins and every other game keep 1.17's layout. A save keeps the rows in
  `HXST` (`port_hxsave.cpp`).
- **Commands.** A selection is sent as 0x8C (`net_cmds.h`): one of 1.17's three operations
  (Select, SelectDeltaAdd, SelectDeltaDel) in chunks of up to 60 unit ids, so a chunk fits a
  replay's block, the flags `SELECT_CHUNK_NEXT` and `SELECT_CHUNK_MORE` joining them. A group's
  recall stays 1.17's 0x13.
- **Buildings together.** Own buildings select together. A command from the card goes to one of
  them, picked by the receivers in `net_cmds.cpp`: Train to the building of the type with the
  shortest queue, Research and Upgrade to the first free one that can, the cancels to the first
  busy one, Build (a placement) to the nearest worker that may build there; Lift-off applies to
  each at its own place. `SelectOnly` makes the selection's walk give that one unit.
- **Subgroups.** The console's units are sorted by kind, highest `gubUnitBaseRank` first
  (`SelectionSubgroupsSort`). The card is the active subgroup's. Every unit has its box, and
  when the selection holds more than one kind the active subgroup's boxes are lighter than the
  rest (`PortConsoleLighten`, `PortConsoleLightRemap` for 1.17's panel; the user, 2026-10-23).
  The subgroup key steps through them, with Shift back; the page follows the subgroup. A
  targeted spell is cast by the nearest unit able to cast it with the energy (`SmartCaster`,
  `select.cpp`). With other kinds selected (`SelectionMixed`), a card's button that is one kind's
  own form of a shared command gives the shared one, as 1.17's card for a mixed selection does:
  the medic's Heal, the carrier's and the reaver's Attack, Move and Stop (`order.cpp`), and each
  unit takes its own form of it (a medic heal-moves). The attack key on a card with no Attack
  (the High Templar's, the Defiler's, the Dark Archon's) attack-moves the selection when any of
  it can attack (`StatcmdSharedAttackKey`, `statcmd.cpp`; 0.6.10).
- **The HUD's grid.** A layout's selection slot with a `grid` (`../hud-format.md`) shows two or
  more units as StarCraft II does, in place of 1.17's panel of twelve: pages of 8 by 3 in the
  built-in layouts (the user, 2026-10-23; the flank layouts 5 by 3), the page tabs at the left,
  the cells beside them. Each cell is drawn as 1.17's panel draws a unit
  (`statdata_select_btn_draw`): the box by the unit's state (`StatDataBoxFrame`), in the
  player's selection colour when it is the normal one, and the wireframe in its hit points'
  colours (`PortConsoleHudBox`, `PortConsoleHudWire`). A cell's press selects as a click on
  1.17's wireframe does (`StatDataSelectAmong`: alone, Shift leaves it out, Ctrl takes its
  type), its tooltip names the unit with 1.17's lines (status string 0x329), and a tab shows its
  page (`PortHudGridTurn`). The page shown is the one holding the active subgroup's first unit
  until the player turns it (`PortHudGridPage`). Presentation only: the page is the HUD's.
- **Buildings' wireframes.** grpwire.grp, the panel's small wireframes, has 1.17's 131 units'
  and no building's. A building's is the console's large wireframe (wirefram.grp) at half its
  size, made into a frame as 1.17's are (`PortConsoleSmallWire`), so 1.17's panel and the grid
  draw it in its hit points' colours; before, the panel read past grpwire.grp's frames.
- **Keys.** The subgroup key (Tab), the minimap's terrain (Ctrl+Tab, Tab in a classic game), the
  idle worker (Shift+F11) and the army (Shift+F12) are `port_keys` hotkeys and rows of
  Settings' Hotkeys tab. Under Hyperion's selection, 1.17's own Tab toggle is off.

## Live channel

`hud` (the data layer's values under the format's names, the card, the buttons and the hits,
and the cursor: its id, 6 to 8 the magnifier over a unit, and its place in the game's pixels),
`hud_events {since?}`, `hud_press {place | button | group | eye}`, `hud_stats {side?, popup?, page?}`,
which turns the players' stats as F1, Shift+F1 and Tab do, and `hud_tip {place}`, which shows
the card button's tooltip at a place as a resting pointer does, -1 to hide it
(`../hyperion-exe.md`).

## Gotchas

- `GpuHudDraw` draws after the game's frame and the effects, so the game's own snapshot
  (`snapshot` on the live channel) has no HUD. Capture the window from the screen to see it.
- 1.17's Tab terrain toggle is case 15 of the minimap's key handler; under Hyperion's selection
  it returns 0 there, and the terrain key goes through `PortMinimapTerrainKey`.
- A path's outline is cached by its text (`HudFlatten`); each tooltip makes a new one, so the
  cache is cleared at 1,024 entries.
- `MapTextUtf8` drops the bytes under 32, so the card's marked letter is found before the name
  is converted, and its index is the UTF-8 length of what comes before it.

## Further reading

`../hud-format.md` (the format),
`replays.md` (the rules and the save), `gameplay.md` (hotkeys), `front-end.md` (Settings).
