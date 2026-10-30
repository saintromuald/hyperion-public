# The front end: Hyperion's own menus

## Overview

Everything outside a game is Hyperion's own code, drawn by the port at the window's own
resolution: the home screen, the campaign screen, the skirmish set-up, the replays list,
Settings, the Mods library, the player card, Patch Notes, and the benchmark, trainer and AI
lab screens the mods add. None of it is 1.17's dialogs. It sits in 1.17's glue state machine by
standing in for the screens that machine would run: a port function is called where a glue
screen would load its `rez\*.bin` dialog, runs its own loop until the player chooses, and
gives back the result 1.17's dialog would have given, so everything downstream is the game's
own code.

Three layers:

- `port_ui.cpp`, the drawing layer: primitives for the graphics card (`UiPrim`), text out of
  one glyph atlas, pictures out of a picture atlas, Clay for layout, and per-element motion.
- `port_frontkit.cpp` and `port_frontlobby.cpp`, the kit: the themed colours, StarCraft II's
  controls drawn by hand, the frame's input, and the pieces the lobby-style screens share.
- `port_front.cpp` and the `port_front_*.cpp` screens: the loop that runs them, the home
  screen, Settings and Mods, and the screens themselves.

Presentation only. Nothing here reaches the simulation: a game the front end sets up is made
by the same game functions 1.17's own screens call, so the replay suite is unaffected.

## Status

Built: the UI layer, the
home screen (the "Orbit hub" design), Settings and the theme, single player
(campaign, skirmish, replays, Load Game), and the motion. The front end is the default; the
setting "Hyperion's menus" (`hyperion`/`menus`) or `HYPERION_FRONT` turns it off, and it needs
the GPU presenter, without which 1.17's menus run instead (a browser build has none).

1.17's own screens still show for the title screen at start, the mission briefings, the
multiplayer ID screen, and 1.17's own multiplayer screens, reached from Multiplayer's The
Original's Screens. The score screen after a game is Hyperion's since 2026-10-21, and
the game menu over a game since v0.6.12-beta: its End Mission, Help, Tips and Mission
Objectives, Settings in Options' place, Save Game and Load Game, Victory and Defeat, and
Alliances and Messaging.


## Screens

| Screen | File | How it is reached |
|---|---|---|
| Home (nav bar, hero, world plate, notifications, taskbar) | `port_front.cpp` (`Home`), `port_front_home.cpp` (`NavBar`, `Hero`, `Plate`, `News`, `Taskbar`) | `do_main_dlg` calls `PortFrontMainMenu` |
| The Menu (F10) | `port_front_home.cpp` (`MenuPanel`, `kDrop`) | F10, the taskbar's Menu button |
| Settings | `port_front_settings.cpp` (`SettingsDialog`, `kSettings`, `kTabs`) | the Menu's Settings (O) |
| Settings' colour theme and its picker | `port_front_theme.cpp` (`ThemeGroup`) | Settings, the Interface tab |
| Settings' HUD tab: the designs, the chosen one's settings and its live picture | `port_front_hud.cpp` (`HudTab`); see the HUD's doc | Settings, the HUD tab (U) |
| Mods | `port_front_mods.cpp` (`ModsDialog`) | the nav bar's Mods (D) |
| Patch Notes | `port_front_notes.cpp` (`NotesDialog`) | the Menu's Patch Notes (P), or a notification |
| Player card, Create a Profile, the profiles dialog, Import StarCraft IDs | `port_front_profile.cpp` (`ProfileScreen`, `ProfilesOpen`) | the avatar, the card tab or Y; `ProfileGate` before Campaign, Skirmish or Replays |
| Campaign: the episodes, then an episode's missions | `port_front_campaign.cpp` (`CampaignScreen`) | `do_cmpgn_dlg` and `do_xcampaign_dlg` call `PortFrontCampaign` |
| Load Game | `port_front_load.cpp` (`LoadGameDialog`) | L or Load Game on the campaign screen; over a game, the game menu's Load Game and Save Game (`GameSavesOpen`) |
| Skirmish: Choose a Map, then the set-up | `port_front_skirmish.cpp` (`SkirmishScreen`) | `do_create_dlg` calls `PortFrontSkirmish` in Play Custom's place |
| Replays | `port_front_replays.cpp` (`ReplaysScreen`) | `do_create_dlg`'s replay mode calls `PortFrontReplays` |
| Multiplayer: the rooms, the host's console, a room, a match's lobby | `port_front_lan*.cpp` | the nav bar's Multiplayer (M); see the LAN doc |
| The benchmark's set-up and runs | `port_front_bench.cpp` (`BenchScreen`) | the home screen's button while the benchmark mod is active |
| The trainer's entry and sessions | `port_front_trainer.cpp` (`TrainerScreen`) | the home screen's button while the trainer mod is active |
| The AI lab: its farm and its games | `lab/port_front_lab.cpp` (`LabScreen`); see the lab's doc | the home screen's button while the AI lab mod is active |
| The score screen | `port_front_score.cpp` (`ScoreScreen`) | after a game, `do_score_dlg` calls `PortFrontScore` |
| The game menu over a game: End Mission and its confirms, Help, Keystroke Help, Tips, Mission Objectives; Settings, Save Game and Load Game; Victory and Defeat; Alliances and Messaging | `port_front_gamemenu.cpp` (`GameMenuPanel` and the other panels; Settings' and Load Game's own code for those three); a layer over the game (`render.md`) | F10, Alt+M, the console's or a HUD's Menu button, Alt+Q, Alt+X, Alt+H, F1, Alt+O, Alt+S, Alt+L, the tips at a game's start, the game's end, and the buttons beside the minimap: every way 1.17 opens those dialogs (`modeless_dlg_startup`) |

Home, campaign, skirmish, replays, multiplayer, bench, trainer, lab and score are screens: one
of them is up at a time (`g_nScreen`), under the same nav bar. Settings, Mods, Patch Notes, the player
card, the profiles dialog and Load Game are panels and dialogs over the screen that is up.
The nav bar and the Menu work on every screen but the score screen, which has only the Menu's
Exit Game (`Enabled`; the user, 2026-10-26): Settings and Patch Notes open over the screen
that is up, and a section's tab goes to that section. Skirmish, replays and the campaign run
in loops of their own inside 1.17's screens, so `Choose` ends that loop as its Home would and
names the section in `g_pszSwitch`; `PortFrontMainMenu` chooses it before it draws a frame of
home, so the bar goes from one section to the other with no home between. Bench, trainer,
Multiplayer and the lab are inside home's loop and just set the screen. The bar's underlined
letters work on home alone; past it those keys are the screen's own.
A switch between sections is one motion (the user, 2026-10-26: it felt "like a webpage changing
a page"). The bar's choice (`NavPick`) fades what the screen shows out over 0.12 s before
`Choose` takes it; 1.17's screens on the way from one loop to the next stay unseen behind the
last frame, since `FrontHandBack(true)` has the presenter keep it (`PortPresentFrontKeep`, until
the next `PortPresentFrontBegin`, 1.5 s at most), where they showed a frame or two of black; and
the screen that comes up fades in and rises 10 units over 0.25 s (`SwitchIn`). The nav bar, the
profile's card, the taskbar and the world stay as they are throughout. The fades step by at most a
30th of a second a frame, so a screen's slow first frame (83 ms for the campaign's art, the first
time) does not take them in one step. The log says how long 1.17's screens took ("back from
1.17's screens in N ms", 1 to 6 ms) and the first two frames' times.
The Menu holds Remastered's main-menu entries in Remastered's order, and the home screen has
the world's name bottom left and the notifications bottom right. The game menu is not one of
the screens: it is drawn over a game, outside the screens' loop ("The game menu" below).

## Key files

| File | What it holds | Key functions |
|---|---|---|
| `src117/port/front/port_front.cpp` | the screen loop, the nav's dispatch, the keys, the entrance and leaving motions, the menus' planet quality, the front end's log; the window's input and a frame's start and end, which the game menu shares | `PortFrontMainMenu`, `PortFrontSkirmish`, `PortFrontReplays`, `PortFrontCampaign`, `PortFrontStartMode`, `PortFrontWndMsg`, `FrontOn`, `FrontInputMsg`, `FrameBegin`, `FrameEnd`, `RunScreen`, `Home`, `Keys`, `Choose`, `ScDialog` |
| `src117/port/front/port_front_home.cpp` | the home screen's pieces and their custom draws, and the panel the Menu and the game menu share | `NavBar`, `Profile`, `Taskbar`, `MenuPanel`, `ScPanel`, `ScMenu`, `Hero`, `Plate`, `News` |
| `src117/port/front/port_front_gamemenu.cpp`, `port_gamemenu.h` | the game menu over a game: its panels, its layer, and what 1.17's code calls; the header is what `lang/` includes | `PortGameMenuFor`, `PortGameMenuDrop`, `PortGameMenuStep`; in `lang/`: `PortGameMenuRead`, `PortGameMenuDo`, `PortObjectivesText` (`opt_dlgs.cpp`), `PortHelpOpen`/`Line`/`Close` (`help.cpp`), `PortTips*` (`tips.cpp`), `PortGameEnd*` (`VictDlg.cpp`), `PortAllies*` and `PortMessages*` (`filters.cpp`) |
| `src117/port/front/port_front_settings.cpp` | Settings: the rows, what each reads and writes, the dialog and its controls, its keys, the live channel's settings; over a game, what it greys and its Accept out of the frame | `kSettings`, `ReadSetting`, `WriteSetting`, `SettingsOpen`/`Accept`, `SettingsDialog`, `SettingsKeys`, `Locked`, `SettingsGameStep`, `PortSettingsLiveJson`, `PortSettingLiveSet` |
| `src117/port/front/port_front_load.cpp` | Load Game over the campaign screen, and over a game the game menu's Load Game and Save Game; `lang/save_dlg.cpp` sends what they choose | `LoadGameOpen`, `LoadGameDialog`, `LoadGameKeys`, `GameSavesOpen`, `GameSavesStep`; in `lang/`: `PortGameSave`, `PortGameLoad` |
| `src117/port/front/port_front_mods.cpp` | the Mods screen | `ModsOpen`, `ModsDialog`, `ModsKeys` |
| `src117/port/front/port_front_internal.h` | what those four share, in `namespace Front`: `SCREEN_*`, the nav's and Settings' tabs, the screens' state, the functions one calls in another | |
| `src117/port/front/port_ui.cpp`, `port_ui.h` | primitives, the glyph and picture atlases, Clay's draw, the per-element motion, the eases | `UiBegin`, `UiLayoutBegin`/`End`, `UiFill`/`Gradient`/`Ring`/`Line`/`Blur`, `UiText`, `UiPictureSet`, `UiPicture`, `UiMotionSet`, `UiEase`, `UiSpan`, `UiPrims` |
| `src117/port/front/port_clay.cpp` | Clay's implementation, compiled once as C++20 | (`CLAY_IMPLEMENTATION` only) |
| `src117/port/front/port_text.cpp`, `port_text.h` | Hyperion's text in the player's language: the catalog for the language chosen, the plural rules, the slots | `PortText`, `PortTextC`, `PortTextN`, `PortTextF`, `PortTextNF`, `PortTextFill`, `PT_`, `PTC_`, `PortTextLocale`, `PortTextLanguages` |
| `src117/port/front/port_frontkit.cpp`, `port_frontkit.h` | the themed colours, Clay without its macros, SC2's controls, the frame's input, the theme solver | `Open`, `Close`, `Text`, `TextGlow`, `Over`, `Clicked`, `DoubleClicked`, `Key`, `Custom`, `FileDate`, `Thousands`, `Elide`, `GameTypeName`, `kDot`, `Sc`, `Fs`, `Box`, `Spacer`, `DialogTitle`, `WheelRows`, `WheelPixels`, `ScButton`, `ScCheck`, `ScMini`, `ScMiniTab`, `ScEdit`, `ThemeApply`, `ThemeShow`, `LayoutCheckBegin`/`End` |
| `src117/port/front/port_frontlobby.cpp`, `port_frontlobby.h` | what the lobby-style screens share: boxes, pulldowns, segments, tips, dialogs, the slot colours, a map's picture; the set-up screens' panels, option rows and players' rows, their Team and Quick layout choices, and a map's team layouts | `InnerBox`, `At`, `Well`, `BoxTitle`, `Pulldown`, `PulldownCloseOutside`, `PulldownKeys`, `Segments`, `TipOn`, `DrawTip`, `DialogOpen`, `MapPictureRead`, `MapPreview`, `kSlotColours`, `InBox`, `OptionRow`, `OptionName`, `SlotGround`, `SlotNumber`, `SlotShade`, `TeamChoices`, `QuickLayoutChoices`, `MapLayouts`, `LevelNames` |
| `src117/port/front/port_frontscreens.h` | what each screen offers the loop, the `FRONT_*` results, `SkirmishGame` and `SkirmishHost` | (declarations) |
| `src117/port/front/port_front_skirmish.cpp` | the map browser and the set-up: slots, colours, teams, game options, rules | `SkirmishOpen`, `SkirmishScreen`, `SkirmishKeys`, `SkirmishResult`, `SkirmishGameNow`, `StartGame` |
| `src117/port/front/port_front_colours.cpp`, `port_front_colours.h` | the colours a row may take: the game's 65, how the game draws a colour, the clash rule, the Any colour field | `ColourName`, `ColourRgb`, `ColourOrder`, `ColourLookOf`, `ColourGap`, `ColourCloseOn`, `ColourAnyField`, `ColourAnyValues`, `ColourAnyTyping` |
| `src117/port/front/port_front_mutators.cpp`, `port_front_mutators.h` | the set-up's Mutators tab, its tabs over the right column and the options' word on the mutators | `MutatorsViewTabs`, `MutatorsTab`, `MutatorsSummary`, `MutatorsTabKeys` |
| `src117/port/front/port_front_campaign.cpp` | the episode cards with 1.17's Smacker figures, an episode's missions and cinematics | `CampaignOpen`, `CampaignScreen`, `CampaignGluesMode`, `CampaignWatchStart`, `CampaignDone`, `CampaignReread` |
| `src117/port/front/port_front_replays.cpp` | the replays list | `ReplaysOpen`, `ReplaysScreen`, `ReplaysResult`, `ReplaysKeys` |
| `src117/port/front/port_front_score.cpp` | the score screen after a game: the head, the tabs, the rows and their densities, the team modes, the count-up and its sounds, the sort and the mark, Save Replay; the numbers are `port_score.cpp`'s (`gameplay.md`, "The score") | `ScoreOpen`, `ScoreScreen`, `ScoreKeys`, `ScoreResult`, `ScoreClose`, `PortFrontScore` (in `port_front.cpp`) |
| `src117/port/front/port_front_load.cpp` | Load Game, over the campaign screen | `LoadGameOpen`, `LoadGameDialog`, `LoadGameStarted`, `LoadGameKeys` |
| `src117/port/front/port_front_profile.cpp` | profiles: the card, the forms, the profiles dialog, importing 1.17 IDs | `ProfileCurrent`, `ProfileGate`, `ProfileToggle`, `ProfileScreen`, `ProfileKeys`, `ProfilesOpen`, `ProfileCountGame`, `ProfileSetLastRace` |
| `src117/port/front/port_front_notes.cpp` | the build's release notes: the home screen's toasts and the Patch Notes window | `NotesToasts`, `NotesToast`, `NotesOpen`, `NotesDialog`, `NotesKeys`, `NotesVersionLine` |
| `src117/port/front/port_front_theme.cpp` | Settings' theme presets and the custom colour picker | `ThemeGroup`, `ThemePickerReset`, `ThemePickerTyping` |
| `src117/port/front/port_front_bench.cpp` | the benchmark mod's screen | `BenchOpen`, `BenchScreen`, `BenchResult`, `BenchTake`, `BenchCameBack` |
| `src117/port/front/port_front_trainer.cpp` | the trainer mod's screen, hosting the skirmish set-up | `TrainerOpen`, `TrainerScreen`, `TrainerResult`, `TrainerTake`, `TrainerCameBack` |
| `src117/port/lab/port_front_lab.cpp` | the AI lab mod's screen, in the lab's folder with the rest of the lab (`lab.md`) | `LabOpen`, `LabScreen`, `LabKeys`, `LabResult`, `LabCameBack`, `LabPanelId` |
| `src117/port/front/port_world.cpp`, `port_world.h` | the planet behind every screen and the camera's views | `WorldSetPlanet`, `WorldSetView`, `WorldStep`, `WorldParams`, `WorldIntroBegin` |
| `src117/port/front/port_intro.cpp` | the countdown of a skirmish or a LAN room's match, drawn over the map before its first frame with 1.17's countdown beep at each digit; the game loop does not run a frame while it holds | `PortIntroGameStarts`, `PortIntroHolds`, `PortIntroArmed`; a layer over the game (`render.md`) |

`port_motion.cpp` and `port_motion.h` are not the front end's: they are the game picture's
motion (the skirmish start, the cutscenes, a map load), read by the presenter and the camera.
The front end's own motion is `UiMotionSet` in `port_ui.cpp`.

## How it works

### Standing in for 1.17's menus

`do_main_dlg` (`src117/lang/gluMain.cpp`) calls `PortFrontMainMenu` first. With the front end
off, or without the GPU presenter, that returns -1 and `do_main_dlg` loads `rez\gluMain.bin`
as before. Otherwise it runs the home screen's loop and returns the result 1.17's main-menu
dialog would have returned (3 single player, 4 multiplayer, 2 exit, 8 the intro, 9 the
credits, `0xFFF0` when the window closed), plus two of the port's own: `PORT_FRONT_MODE`, the
active mod's own mode, which `PortFrontStartMode` starts, and `PORT_FRONT_LAN`, a LAN match,
left with the same fade to black as a skirmish's start, after which 1.17's chat room comes
(unseen with the GPU presenter, `lan.md`). `do_main_dlg`'s switch is unchanged.

The other screens stand in the same way, each returning the glue mode to go on to or -1:
`PortFrontSkirmish` in `do_create_dlg`'s Play Custom, `PortFrontReplays` in its replay list,
`PortFrontCampaign` in `do_cmpgn_dlg` and `do_xcampaign_dlg`, and `PortFrontPassProfile` in
`do_login_dlg`, which passes 1.17's ID screen when the chosen profile still has its character
file.

### A frame

Every screen runs in `RunScreen`. One pass: pump the window's messages
(`WinMsgPeek`/`WinMsgDispatch`, `port_winmsg.cpp`), the live channel (`PortControlPoll`) and
the LAN client (`LanFrontFrame`); step the planet and camera (`WorldStep`) and the entrance,
return and leaving clocks; `PortGpuUiBegin` for the size to lay out at, `UiBegin` for the
stage scale, `UiPlanet` behind everything; `Keys` for the frame's key presses; then
`UiLayoutBegin` and `Home`, which opens the root element, draws the nav bar, the avatar and
card tab and the taskbar, and then either the home screen's content or the screen that is up,
with the dialogs last so they lay out over it; the screen's result (`SkirmishResult` and the
rest) into `g_nChoice`; `UiLayoutEnd`, which lays Clay out and turns its commands into
primitives; `PortGpuUiEnd`, which draws the planet and the primitives in one pass and
presents; and a sleep to the display's pace (`SDL_DelayPrecise`).

The loop ends when `g_nChoice` is not 0. `FrontHandBack` then gives the window back to 1.17's
screens and forgets the last glue palette, so a 1.17 screen shown next fades in properly.

### Layout and drawing

The design's stage is 1080 units high and at least 1920 wide: `UiBegin` takes the scale from
the window's height, or from its width when the window is narrower than 16:9, so the nav bar
always fits. `UiPx` turns stage units into pixels. The design measures in StarCraft II's units,
0.9 of a stage pixel: `Fs` (FrontKit) turns them into the stage units Pad, Gap, Border and Text
take, and `Sc` into pixels.

A list the wheel scrolls adds `WheelRows(Over(id))` to its first row (three rows a notch, one
for tall rows) or `WheelPixels` to its offset, then clamps. A screen with pulldowns ends its
draw with `PulldownCloseOutside` and starts its keys with `PulldownKeys`, which gives Esc to
an open pulldown.

Layout is Clay, called without its macros: a screen fills a `Decl` (a
`Clay_ElementDeclaration`) and calls `Open`, then its children, then `Close`. `Id("name", n)`
names an element, `Over`, `Clicked` and `DoubleClicked` ask about the pointer (a list's double
click is the same row clicked again within 450 ms), and `Floating` attaches a panel
to an element or a corner. Text goes through `Text` or `TextGlow`, which measure with
stb_truetype and rasterize each glyph once at the size it is drawn.

Anything SC2 draws with art is drawn by hand instead: `Custom` hangs a draw function
(`DrawScFrame`, `DrawScRaised`, `DrawScCheck`, `DrawScSlider`, `DrawScQuery`, `DrawScBox`) and
an `ScLook` on an element, and `ClayDraw` calls it in place. Images that are not geometry are
pictures: `UiPictureSet` puts RGBA into one of 32 slots of the picture atlas and `UiPicture`
draws a slot. `port_ui.h` says who owns which slot.

Motion is per element: `UiMotionSet` gives a Clay id an opacity, an offset and a scale for the
frame, and each draw takes the smallest element with a motion whose box holds its centre. The
timelines are `Enter` (the first home screen coming in), `Return` (coming back from a game),
`SwitchIn` (a section coming in from another, and the bar's choice fading out first) and `Leave`
(going to a game, with `LeaveBlack` over it); `UiEase`, `UiEaseEven` and `UiSpan` are the
design's curves.

### Input

While the loop is up, `PortFrontActive` is true and 1.17's window procedure
(`src117/lang/event_Win.cpp`) hands the window's mouse and keyboard messages to
`PortFrontWndMsg`, which fills `g_mouseX`, `g_mouseY`, `g_bDown`, `g_bPressed`, `g_bReleased`,
`g_keys` (one entry a press, Backspace repeating), `g_typed` (printable `WM_CHAR`) and
`g_nWheel`, and says the message was taken; Alt+Enter stays the window's. Clay gets the
pointer in `UiLayoutBegin`. The keys go through `Keys`, which gives them to whatever is
innermost: a profile form, the Menu while it is open (`MenuKeys`, on every screen), the screen
that is up, Patch Notes, Settings, Mods, then the home screen's own. `Key(vk)` reports a press
without taking it; an edit box with the keyboard (`ScEdit`) takes the frame's typing and
Backspace, so only one box gets them.

### Themes

The kit's colours are variables, not constants. `ThemeApply` writes them from the saved
setting when the front end starts and when Settings closes; `ThemeShow` writes them from a
theme not saved yet, so the Settings dialog shows a theme while it is open. A theme is three
colours (accent, second accent, panels) from one of five presets or Custom. The solver walks
the standard palette in OKLCh: greys keep their own hue, blues follow the accent, golds the
second accent, and the colours that say what a thing is keep theirs; a custom theme with its
own lightness remaps each family's lightness, then lightens any colour that would stop
reading on the new panels. `ThemeReportOf` reports what the result draws, for the picker.

### Text in the player's language

Every text Hyperion shows goes through `port_text.h`, with its English as the key:
`PortText("Back")`; `PortTextF("{0} of {1}", {a, b})` for a text with values in it, whose slots a
language may put in its own order; `PortTextN`/`PortTextNF` for a count, which picks the
language's plural form; `PortTextC("context", "Open")` when one English means two things. A
table's text is marked `PT_("...")` (or `PTC_`), which does nothing at run time, and is passed
through `PortText` where it is shown. A missing translation shows the English. `PortText`'s
pointer lives as long as the process, so `Text` takes it without `Keep`.

The catalogs are `engine/locale/<locale>.po`, gettext's format, one per Remastered language;
`docs/translating.md` (the repository's) is the translators' guide. `tools/locale_catalog.py`
finds the marked text (`extract` writes `hyperion.pot` and merges each `.po`), checks that a
translation keeps its slots and its `|` choices (`check`), counts what each language lacks
(`report`), and writes `locale_catalogs.h` into the build folder (`header`), which every build
does and `port_text.cpp` compiles in; an entry that fails the check is left out. The
language is `PortLanguageChosen()` (`data.md`, "The install's language"), decided once at the
start; the Language row of Settings' Interface tab sets it, and a restart applies it. In any
language but English the row's label adds the English word ("Язык · Language") and each choice
is in its own language, so a player who picked one they cannot read finds the way back.

What keeps English: the live channel's names and the settings file's keys (a Settings row's
label is translated where it is drawn, its English stays the key the channel uses), log lines,
error details a player copies for us, file names, and the game's own words that come from the
game's tables, which the install has already translated. A function whose result is compared
or saved stays English and its display sites translate: `MutatorsText(m)` is the key the
benchmark saves, `MutatorsText(m, true)` the line a player reads.

The kit is ready for any script: `Upper` (`UiUpper`) upper-cases Latin, Greek and Cyrillic;
`KeyAt` finds a label's key letter by character; `ScEdit` and `g_typed` take UTF-8 and Backspace
removes a whole character (a save's name keeps ASCII, as file names must); `FileDate` and
`Thousands` take the month names and the thousands separator from the catalog. A button of
fixed width (`ScButton`, `ScMini`) draws a label longer than its room smaller, down to 60% of
its size (`FitSize`). Clay wraps
text only at spaces, so the build patches its measuring (`fetch_clay.py`, `build.md`) to make
each CJK character a word of its own. A glyph none of the menus' faces has is drawn from the
game's fallback faces (`MapTextFace`, `render.md`): Remastered's, then Hyperion's Noto faces,
which draw every language's menus with no Remastered install, then the system's. The glyph
atlas starts over when it fills, the next frame drawing every glyph again.

### The Settings table

`kSettings` in `port_front_settings.cpp` is the whole list, one row a setting: its tab (`kTabs`), its
label, its control (`CTL_TOGGLE`, `CTL_SLIDER`, `CTL_STEPS`, `CTL_CHOICE`, `CTL_KEYS`,
`CTL_PATH`, `CTL_BUTTON`, `CTL_THEME`, or `CTL_HEAD`, a section's heading whose rows follow it,
as Hotkeys' Addons), its choices as a `|`-separated list, its default,
where it comes from (Classic, SC:R or Hyperion), what it waits on, when it applies, its
`Bind`, its read and write, its settings-file key, and the note its `?` shows. The label,
the choices, what it waits on and the note are marked `PT_` and translated where they are
drawn; the English label stays the row's name on the live channel.

A row's read and write are the whole of its plumbing, in the functions above the table.
`ReadSetting` calls the row's read for its `SetValue` when the dialog opens, and
`WriteSetting` calls its write: 1.17's own settings go into 1.17's globals (`gOptions`, the
scroll rates, the option bits, `set_gamma`, `set_playback_option`), and Hyperion's go into
the settings file's `hyperion` section through `PortSettingSetNumber`/`SetString` and are
applied at once by their owner's setter (`PortGpuHdSet`, `PortGpuVsyncSet`,
`PortFrameRateSet`, `PortCoresSet`, `PortConfineSet`, `PortHotkeySet` and the rest). A write
returns true when its change waits for a restart. Rows of one kind share a pair: the on-or-off
settings `ReadFlag` with a write such as `WriteFlag` or `WriteConfine`, under the row's
`pszKey`; the two volumes `ReadVolume`. A row with no read and no write is one the engine does
not have yet: `Editable` returns false, it draws off and greyed, and `Tip` adds "Not available
yet." to its `?`. `NotYet` names those that wait on something, and Remastered's paid content
while it is held: a tab lists them last, under a "Not available yet" heading (the table's last
row, a `CTL_HEAD` no tab owns), so what can be changed comes first, and `ReadSetting` shows
them at their default, a toggle off, whatever the settings file keeps (the user, 2026-10-26).
The HUD tab draws its own way and is not regrouped. `Bind` names only the rows the dialog draws, applies or checks its own way
(the display, the paths, the hotkeys, the theme, the HD art); the rest are `BIND_NONE`.

Nothing applies until Accept. `SettingsOpen` reads every row twice, into `g_set` and
`g_setOpened`; `SettingsAccept` writes only the rows that differ, inside one
`PortSettingsHold`/`Release` so the file is written once, then calls 1.17's own savers
(`snd_set_options`, `ini_file_close`, `exit_video_opts`). Cancel and Escape just close, and
`ThemeApply` puts the saved theme back. A row that needs a restart raises that confirmation,
with Restart now and Later: Restart now starts a new copy through `PortInstanceRestart`
(`port_instance.cpp`), with this copy's command line and `--after` naming it, and leaves as
the menu's Exit does; the new copy waits at the single-instance check for this one to end;
Display and Monitor are shown at once and kept only if the player says so, with a 15-second
revert. `SettingsRestoreTab` resets the open tab to the table's defaults after asking.

### A player's colour

A row's colour tile opens the picker (the design's `teams.js` picker), with two tabs. The
game's colours are the 65 the game draws, 1.17's twelve, Remastered's and Hyperion's
(`PlayersColourRamp`, `gameplay.md`), round the hue wheel from red with the greys last. A
colour another row has shows that player's number, and one that reads as another's swaps
with it; one close to another's has a warning rim, and the line under the grid names the
colour pointed at and why. The key under it, to the numbers and to the rims, shows each line
only while a cell has one (the user, 2026-10-26: the rim's line stayed with no rim to explain).
The tile is 150 wide (`kSwatchW`, and a team's shade, `SlotShade`, with it) and its name on one
line, a size smaller down to 12 before it is cut short (`TextFitted`); a tile close to another
player's has its rim in warning and a "!" on its corner, floating so the name keeps the width,
and a tip naming the player and how to part them. The names carry no mark of whose colour they
are: Hyperion's own of a name 1.17 has are numbered as Remastered's are ("Yellow 7", not
"Yellow (Hyperion)"; `kNames`, `tools/hxmap/names.py` keeps its `_hx` keys). Any colour is the theme picker's field and hue strip, for one
colour: a saturation and brightness field, the hue beside it, Hex, R, G and B to type in, and
what the game draws from it, the units' eight shades and the minimap's dot, fitted as
`port_teamcolor.cpp` fits them. A colour that reads as another player's cannot be used there,
and Use This Colour (U) takes it. `port_front_colours.cpp` keeps the model: the 65's names
(`tools/hxmap/names.py`'s), their swatches, shades and dots read from a tileset's palette
(the team colour entries are the same in every tileset), the clash rule (`ColourGap`: the
nearer of the dots and the shades in OKLab; under 0.04 the same, under 0.08 close), and the
field (`ColourAnyField`, `ColourAnyValues`; the picture atlas's slots 25 and 43). At the set-up's foot, beside Back, the rules' mark, HYPERION RULES (or MOD RULES), shows
only when a choice requires Hyperion's rules (`ForcedWhy`: a mod, more than 8 players, a map
past 256, teams, a computer level, a handicap, mutators), with the reason in its tip; the rules
chosen in Game Options, Classic or the default Hyperion, leave the place empty (the user,
2026-10-26). A row's own
colour goes to the game as an RGB seat (`SkirmishSeat::rgb`, `PlayersChooseColourRgb`), as a
team's shade does; Distinct Colours and a newly seated row still take from 1.17's eleven
first, Distinct Colours the 65 once none of the eleven is clear of those taken. Every row
chooses, the rows past 8 too (the user, 2026-10-26: they had none to choose): a row past 8
starts with the colour Hyperion gives a slot past 1.17's (`RowColour`, `PlayersDefaultColour`),
and `PortLaunchSkirmishChoices` hands each computer's to the game by the order they are seated,
as teams and levels go (`PlayersSeatNote`).

### The Mutators tab

The skirmish set-up's right column is two tabs, the design's take A2 (2026-10-26, the design
center's `11-front-end`, `lobby.js` `viewTabs`, `mutBrief` and `mutSummary`, `mutators.js`):
Game Options (O) with the map, and Mutators (U), which takes the column
(`port_front_mutators.cpp`). A host's own box, a LAN room's or the trainer's, stays under the
options and the map, which are then 462 high; with none they take the column, and the options say
at their foot which mutators are on, with a button to the tab (`MutatorsSummary`). The strip
over the column counts the mutators on and names the set: "No mutators · as Classic", or the
preset's name or "Custom set", with Hyperion rules.

The tab is the presets (the engine's five, Custom lit for any other set and never a choice),
the list of the ten by group, and the briefing of one: what it does, a picture of Classic
beside this game, its choices as chips, and what the choice does in a sentence. Pointing at a
row briefs it until the pointer leaves the list; pointing at a chip shows that choice in the
picture and the sentence, dashed, before it is taken. Nothing a chip pointed at shows moves the
chips: the sentence's box keeps the height of the longest of the choices' sentences (and the
distance's and the chance's, for rich resources), wrapped as Clay wraps it (`EffectHeight`, `WrappedLines`), and never
shrinks while the same mutator is briefed. Without it a choice whose sentence wrapped to fewer
lines moved the chips from under the pointer, which ended the peek, which moved them back. A
choice harder than Classic (less in a
field or a geyser, fewer workers, a cap under 200, upgrades or research taken away;
`MutatorHard`) is marked in a dark red where any other is the theme's gold, and Classic's chip
has a blue dot. Rich resources (Rich bases before v0.6.23) has its choices as pulldowns, not
chips, each under its name, so its picture keeps its room in a narrow window (the user,
2026-10-26: the chips had left the picture a sliver): Where, then side by side the distance (32
to 80 tiles, or a quarter, a third or half of the map), greyed but for Outside a radius, and
the chance, greyed for Off and Every base, which roll nothing, neither marked as Classic's,
since Classic has no rich resources. An item pointed at in an open list shows in the picture as
a chip does (`skmutwhere_item` and the others, `ChipPointed`). The picture's bases are the map's
read (`RichBasesPreview`), made on a thread of its own, one at a time, while the picture says
"Reading this map..." (in turn in the browser's build, which has no threads); the read keeps the
map's bases and walks for the next choice, so only a choice's sets are made again. The effect
box's height, the longest line of any choice, takes a choice not read yet as 99 bases rather
than reading it (the user, 2026-10-26: on Garden of Aiur 2x, 384 tiles and 16 starts, pointing
at the row read the map eighteen times, half a second each, and the set-up hung). Then a check, Announce at the
start (`MUTATOR_RICH_QUIET` off), greyed for Off. Its NEW tag in the list shows until it is
chosen, where its choice takes the room. Its picture
is the map with the bases the choice may turn rich, read from the map file as the game will read
it (`RichBasesPreview` over `MapWalkable`'s minitiles, the fields' amounts from the map's units
for its walls), kept a map and a choice at a time. Up
and Down move through the list, Left and Right through the briefed one's choices, Esc goes back
to Game Options. A guest sees the host's set and chooses nothing.

The pictures are drawn in a space 240 high and at least 340 wide, in the console's wireframes
(`wirefram.grp`), lit in Classic's blue, the option blue, or this game's gold, with the command
card's icons (`cmdicons.grp` through `ticon.pcx`) and the console's resource icons
(`game\icons.grp`): the player's own art, read as the tab is first shown into the picture
atlas's slot 41; the fog's two masks are slot 42.

### Starting a game, and coming back

A choice that starts a game is not acted on at once: `Launches` says whether the result starts
one, and if so the screen plays the leaving motion for 1.1 seconds and goes to black before
`g_nChoice` is handed back (the screens keep giving their result each frame until then).

- Skirmish's `StartGame` makes the game where Play Custom's Ok did: `CreateNewGame`, the race
  bytes `InitSinglePlayerCustom` sets, and the set-up's choices (teams, levels, starts,
  colours, mod races, computers past 8, the rules) through `PortLaunchSkirmishChoices` into the
  session (`gameplay.md`, "The session"). `PortFrontSkirmish` returns 0x0C and
  `do_create_dlg` goes on as before. With the player's own seat a computer's ("Computer" in its
  list, Skirmish's own set-up only), the game is made the same way and then watched as its
  replay, at up to Fastest ×16 (`gameplay.md`, "Watching a skirmish of computers"); the
  skirmish flag stays set through the replay, so leaving it comes back to the set-up.
- A replay or a save is made ready on the screen and started once it is black
  (`PortLaunchStart`, after `PortLaunchReplayFile` or `PortLaunchLoadFile`).
- A mission goes 1.17's own way: the campaign screen names the glue mode
  (`CampaignGluesMode`), so the briefing and the mission are 1.17's.
- An episode lists its cinematics between its missions, where they play: the history list's
  rows with a flic (`wFlic`), named from `gluHist.tbl` without "Cinematic: ". One is seen once
  the mission after it is reached, since its row carries that mission's number; the counts,
  pips and next mission leave them out. Watch Again, once the screen is black
  (`CampaignWatchStart`), sets the row up as the history list does (`sgSavedFlic`, game mode
  10), and the main loop's mode 10 goes back to the glue screens rather than on to the next
  mission when `PortFrontCinematicWatched` says the campaign screen asked; `CampaignOpen` then
  selects the cinematic's row again. Once a choice is made the screen takes no keys while it
  fades out.
- After any game, a mission's too, 1.17's `do_score_dlg` works out the results and plays the
  music, and `PortFrontScore` runs the score screen where it ran its dialog: the players and
  numbers `ScoreRead` gives (`gameplay.md`, "The score"), with a skirmish's map, game type and
  team names as its set-up named them (`SkirmishPlayed`). OK returns 7, 1.17's OK, and 1.17
  goes on to its campaign screen, which `AfterGame` turns into the way back below; Exit Game
  in its Menu exits after it. After a skirmish or a replay, OK hands straight over to its
  screen (`g_bFromScore`, `PortFrontScoreToScreen`): the screen comes up as it is, with no
  `Return` motion, and `do_score_dlg` fades the score's music into the menus' track 0x16 on the
  window's timer (`PortMusicChange`, `music.cpp`) where 1.17 stopped it with `stop_music`,
  whose fade waits about a second and held the screen still (the user, 2026-10-26). Leave Game in a skirmish the menus set up goes to the score
  screen as a defeat (`PortFrontScoreAfterLeave`, in `quit_to_menu_respond`), where 1.17 goes
  back to its menus. While the score screen is up `gwGameMode` is 6 or 7, not 4, so
  `RunScreen` takes the window as closed when the mode changes from the one it started in.
- A mod's mode returns `PORT_FRONT_MODE`, and `PortFrontStartMode` starts the run (`BenchTake`
  into `PortBenchFromMenus`, `TrainerTake` into `PortTrainerFromMenus`, or
  `PortLaunchCanvas`); a trainer session's game is remade from a `SkirmishGame` record by
  `PortLaunchSkirmishGame`. A game the lab watches (`FRONT_LAB_WATCH`) goes the same way, its
  replay started by `PortLabWatchLaunch`.

After a game 1.17 goes to its campaign screen however the game ended. `AfterGame`, in
`PortFrontCampaign`, turns that one visit into a return to the screen the game came from: 0x0B
for a skirmish, 0x0C for a replay, and home for a trainer session, a watched LAN match or a
game watched from the lab, where the lab opens again on the view it was left from. The
screen comes back as it was left, and plays the `Return` motion, unless the score screen was
up before it, which it takes over from as it is.

### The Mods screen

The library (`port_front_mods.cpp`): a card per mod, its page (what it changes, what it is built
on, its folder), Activate and Open mods folder. Activate saves the `"hyperion"/"mod"` setting.
A mod's units, weapons and rules load only as Hyperion starts (`ModDataStartUp`), so activating
a mod whose chain has data, or leaving one, asks for a restart, as Dawn of War's mod manager
does: Confirm's `CONFIRM_MOD_RESTART` with `ModsRestartText`'s words, and Restart now starts
Hyperion again without the `--mod` it may have started with (`PortInstanceRestart`). A mod that
depends on others lists them under BUILT ON, each "or later"; when one is missing or too old
the page says which and Activate is greyed (`ModsMissing`, the A key too). A mod whose
`mod.json` asks for the live channel says so under WHAT IT OPENS, and activating it asks first
(`CONFIRM_MOD_CHANNEL`, `ModsChannelText`): Allow stores the mod's folder in
`modChannel.<id>` (`PortModAllowChannel`), so a mod later put in that folder under the same
id asks again.

The skirmish set-up names the mod while a mod's data is loaded (`ModDataName`): its line reads
"<mod> on Brood War" and its Game button "Mod: <mod>". A campaign's mission never has a mod's
data (`RulesForGame`, `port_rules.cpp`); it keeps Hyperion's own improvements, such as the wide
selection.

### The game menu

The menu a game opens over itself is the main menu's Menu panel (`ScMenu`, over the same
blackout) and its confirm (`ScDialog`), in the theme chosen in Settings' Interface tab,
which `ThemeApply` reads again as it opens, so a game started with `--map` has it too. It
shows in a single-player game, a LAN room's match and a replay, on the GPU presenter with
Hyperion's menus on (`Available`); a streamed seat, which sends the game's 8-bit frame only, the
SDL renderer and the browser build keep 1.17's dialogs, as does `HYPERION_FRONT=0`.

**One way in.** `modeless_dlg_startup` (`lang/modeless.cpp`) is where every one of 1.17's
in-game dialogs is loaded, so it asks `PortGameMenuFor` with the dialog's file first. For the
files in `kTemplates` (`gamemenu`, `abrtmenu`, `restart`, `quit2mnu`, `quitrepl`, `quit`,
`helpmenu`, `help`, `tips_dlg`, `objctdlg`, `options`, `spd_dlg`, `snd_dlg`, `video`, `netdlg`,
`savegame`, `loadgame`, `wmission`, `lmission`, `allyfltr`, `teamfltr`, `msgfltr`) the menu opens
on that panel instead, and the dialog is never loaded.
The five option dialogs are all Settings, each on its tab (Speed and Network on Game, Sound on
Sound, Video on Graphics). So F10, Alt+M, the console's and a HUD's Menu button, Alt+Q, Alt+X,
Alt+H, Alt+O, Alt+S, Alt+L and F1, the tips at a game's start, the game's end
(`do_victory_dlg`, `do_loss_dlg`) and the Alliance and Messaging buttons beside the minimap, all
land here with no change to their callers. A panel opened by its own key is alone, so its Previous closes the menu, as
1.17's does at level 0.

**1.17's state.** Opening the menu runs `modeless_dlg_enter`, the first half of
`modeless_dlg_startup`: the single-player pause, the cursor kept, the accelerators saved and set
to the sound's alone. Closing it is `modeless_dlg_shutdown(0)`, as 1.17's dialogs close. 1.17
has one slot for the saved accelerators, so another dialog opening over the menu (victory,
defeat, the fatal box) or the game's end (`free_game`) must
put the menu away first: `modeless_dlg_enter` and `modeless_dlg_shutdown` call
`PortGameMenuDrop` where they would have closed 1.17's dialog, and give the accelerators and
the cursor back the same way.

**What it shows and does.** `PortGameMenuRead` (`lang/opt_dlgs.cpp`) gives the entries by
`gamemenu_activate`'s rules, read every pass so they change as the game does: Save Game (in
single player, and for the host of a multiplayer game that is not a ladder game; greyed while
`CanSaveGame` says no, which 1.17 never did in single player), Load Game (single player),
Pause Game or Resume Game (multiplayer, while the player has pauses left), and End Mission's
Restart Mission (single player), Exit Program (not on Battle.net) and its quit, captioned as
`abortmenu_update_timer` captions it (Quit Mission, Surrender, Victory, Quit (Draw) or Exit
Replay). `PortGameMenuDo` makes each choice with the calls 1.17's handler makes: the pause and
restart commands, `PortQuitMission` (`victdlg_quit`, then the score screen as a defeat or the
way back to the menus, `quit_to_menu_respond`'s body), `PortLeaveForMenu` for a replay,
`PortExitProgram` (`quit_respond`'s). Mission Objectives is `objective_activate`'s text,
Keystroke Help is `help_txt.tbl`'s lines in two columns, and Tips is `tips.tbl`'s with 1.17's
Previous, Next and Show Tips at Startup, which steps and keeps them as `set_tip_text` and the
tips dialog do.

**Options is Settings.** The panel draws the main menu's `SettingsDialog`, `SettingsKeys` and
`Confirm` as they are; `SettingsOpen(true, tab)` marks it as over a game (`SettingsInGame`),
and closing it, by Accept, Cancel or Escape, goes back to the menu. Over a game `Locked` greys
what a game cannot take: every row that applies on a restart, Display, Monitor and resolution,
the Remastered install, Game frame timing, and the game speed in a multiplayer game or a
replay, which shows the replay's own speed (`replay_get_initial_game_speed`). A greyed row's ?
says to change it in the main menu. The game speed changes in single player by
`send_net_cmd_SetSpeed`, as 1.17's Speed dialog sends it, so the replay keeps it; the scroll
speeds read and write the multiplayer copies in a multiplayer game, as 1.17's Speed dialog does;
Network latency is live in a multiplayer game (`net_get_latency`, `netmgr_set_latency`, which
changes it for every player) and greyed elsewhere. Accept waits for `SettingsGameStep`, from
the menu's step, so no setter runs inside the GPU frame; after it a restart's word has OK alone,
as there is nothing to restart into mid-game. The HUD tab's live picture is the menus' alone
(`PortGpuUiEnd` draws it and shares the console's caches), so over a game the tab says the
console takes the design on Accept, and `HudTabAccept`'s `PortHudReload` shows it.

**Save Game and Load Game** are the campaign screen's Load Game dialog (`GameSavesOpen`): the
saves of the kind the game makes (`GetSaveExtension`: `.snx` in single player, `.mlx` in
multiplayer), Delete and its confirm, and Cancel back to the menu. Save adds a name box of 27
characters (1.17's), which drops what a file name cannot hold and takes no letter keys; a row
picked gives its name, Enter or a double click saves, a name that is there asks Replace first,
and the name saved under comes back next time. What they choose waits for `GameSavesStep`,
which calls `PortGameSave` or `PortGameLoad` (`lang/save_dlg.cpp`): 1.17's checks (the 128 saves,
`is_bad_filename`, `sub_498B30` for a replaced save), then `send_net_cmd_SaveGame` or
`send_net_cmd_LoadGame`, and the menu closes as 1.17's does after a save or a load. The layer
types the characters `WM_CHAR` gives into the kit's `g_typed`, and redraws a paused game at
each blink of the caret.

**Victory and Defeat** are the confirm's box. `PortGameEndKind` (`lang/VictDlg.cpp`) gives
the text `victory_activate` and `loss_activate` choose (Victory, Victory in a multiplayer game,
Draw, Disconnected, the end of a replay, Defeat) and whether Continue Playing shows (a
multiplayer victory, not in a replay). `PortGameEndSound` plays the sound those two play as
they open, `sound\Misc\youwin.wav` or `youlose.wav` (sfxdata 0x15 and 0x16), which the
panel left out until 2026-10-27. A watched skirmish's end is Hyperion's own kind,
`GM_END_WATCHED`: "Game Over", who won and when (`PortWatchResult`), Leave Game and Keep
Watching (`gameplay.md`, "Watching a skirmish of computers"). The choice goes to `PortGameEndChoose`, which sets
`gnDlgReturnVal` as the buttons do and runs `victory_dlg_cmd` or `loss_dlg_cmd`: the score screen,
or the game going on. A defeat left 20 seconds ends the game as `loss_dlg_fcn`'s timer does
(`PortGameEndTimeout`). F10 and Escape do not close them; Enter is OK.

**Alliances and Messaging** read and stage what 1.17's dialogs do (`lang/filters.cpp`).
`PortAlliesOpen` copies the alliance row and the vision mask as `do_ally_filter_dlg` does and
gives the rows by `sub_442320`'s and `sub_442490`'s rules: the other people of a melee game, or
the teams with their members in a team game, each with Allied and Shared Vision, greyed where
1.17 greys them (a computer, a game without alliances, `CanChangeVision`), and Allied Victory.
The toggles change the staged copy as the dialog's check boxes do, and Accept is
`teamfilter_dlg_cmd`'s body, `AlliesCommit`, which sends the alliance and vision commands. In a
replay the panel is Replay Players: Vision for every player and Reveal Entire Map (control 0x19's
box), committed to the replay's own vision mask. Messaging lists the people (or teams), Allies
and Everyone as `sub_4429A0` fills the list, and its Accept is `msgfilter_dlg_cmd`'s body,
`MessagesCommit`. The colour swatch beside a name is the score screen's (`ScoreColour`).

**A pass.** The panels are a layer over the game at 850 (`render.md`, "The layers over the
game"), over the other layers and under nothing but the cursor. Its Draw is a frame of
the kit's (`FrameBegin`, `UiLayoutBegin`, the top panel, `UiLayoutEnd`, `FrameEnd`); its Mouse,
Key and Wheel feed the kit's input (`FrontInputMsg`) and take every message while the menu is
up, as 1.17's modeless dialog does, but for the modifiers, the sound's Ctrl+M and Ctrl+S and
Alt+F4. What a panel chose waits for `PortGameMenuStep`, in `RunGameLoop` after
`PortConsoleUpdate`, so no game call runs inside the GPU frame. The layer frees the cursor and
hides the game's: Windows' arrow is the pointer, as in the menus.

## Settings and environment variables

The settings file is `settings.json` in the player data folder (`port_settings.cpp`); the
front end's own settings are its `hyperion` section. Those the front end owns:

| Name | Effect |
|---|---|
| `menus` | "Hyperion's menus": off, 1.17's dialogs run from the next start |
| `colourTheme` | the theme, 0 to 4 a preset, 5 Custom |
| `themeCustomAccent`, `themeCustomAccent2`, `themeCustomPanel`, `themeCustomLight` | Custom's three colours as `#rrggbb`, and whether its lightness is the player's |
| `menuQuality` | the planet's resolution behind the menus: 0 Auto, 1 High, 2 Low |
| `profile` | the profile chosen |
| `notesDismissed` | the release-note ids whose notifications were closed |
| `language` | the menus' and the game's language: `install` (the default, the install's own) or a locale such as `deDE`; read at the start (`port_language.cpp`), so it waits for a restart |

Every other row of `kSettings` belongs to the module whose setter its write calls; the key is
in the row's `pszKey` or in its read and write.

| Variable | Effect |
|---|---|
| `HYPERION_FRONT` | 1 or 0 overrides the "Hyperion's menus" setting |
| `HYPERION_FRONT_SIZE=WxH` | sizes a hidden or background window for a check (`port_present.cpp`) |
| `HYPERION_FRONT_SNAP=<file.bmp>` | saves the frame drawn after the snapshot time |
| `HYPERION_FRONT_SNAP_AT=<seconds>` | when to save it (2 by default) |
| `HYPERION_FRONT_SNAP_SCREEN=<screen>` | which screen the snapshot is taken on (`home` by default) |
| `HYPERION_FRONT_EXIT=1` | leaves through Exit once the snapshot is saved |
| `HYPERION_FRONT_OPEN=<screen>` | goes on from home to `skirmish`, `campaign`, `replays`, `multiplayer`, `lanhost` (`lanhost-match` goes on to a new match's lobby), `bench`, `bench-runs`, `trainer`, `trainer-sessions`, `lab` or `lab-games`, once; `score-<preset>` opens the score screen with stand-in players (`1v1`, `ffa8`, `4v4`, `ffa16`, `4x8`, `ffa32`; `ScoreStandIn`), and its OK goes back home |
| `HYPERION_FRONT_MAP=<folder>\<file>` | then opens the set-up of that map, under `maps` (`port_front_skirmish.cpp`) |
| `HYPERION_FRONT_WATCH=<speed>[:<level>,...]` | with `HYPERION_FRONT_MAP`: the player's seat made a computer, the speed 0 to 10 (7 to 10 Fastest ×2 to ×16), the seats' levels in order (`port_ai.h`, 0 Classic to 7 Elite), and Start Game, to watch the game (`gameplay.md`, "Watching a skirmish of computers") |
| `HYPERION_FRONT_MUTATORS=<n>[:<list>]` | with `HYPERION_FRONT_MAP`: the Mutators tab with the nth in its list briefed (0 Mineral fields to 9 Tech and upgrades), or Game Options for `-`, and `--mutators`' list as the set |
| `HYPERION_FRONT_SETTINGS=<tab>` | opens Settings on that tab (`kTabs`) |
| `HYPERION_FRONT_WORLD=<slug>` | shows that world behind the screens (`port_world_table.h`) |
| `HYPERION_FRONT_ENTER` | 1 plays the entrance when a check would leave it out, 0 never |
| `HYPERION_GAME_MENU_OPEN=<panel>` | opens the game menu on a panel three seconds into a game, once, for a check: `menu`, `end`, `restart`, `quit`, `exit`, `help`, `keys`, `tips`, `objectives`, `options`, `speed`, `sound`, `video`, `network`, `save`, `load`, `victory`, `defeat`, `allies` or `messages` (the last four for their look: what they choose ends or changes the game) |
| `HYPERION_FONT`, `HYPERION_FONT_WIDE`, `HYPERION_FONT_BOLD`, `HYPERION_FONT_MONO` | font files in place of the ones in the exe (`HYPERION_FONT_MONO` the score screen's numbers) |
| `HYPERION_FONT_PETCH`, `HYPERION_FONT_PETCH_MEDIUM`, `HYPERION_FONT_PETCH_SEMI`, `HYPERION_FONT_PETCH_BOLD` | Chakra Petch's four cuts in place of the exe's, the HUD tooltips' face (`hud.md`) |

`hyperion_front.log` in the data folder records the window and refresh, which font each face
came from, the screen's state as it changes, the layout's complaints, and at the end the
frames shown, where a frame's time went and the planet's share; every line also goes to the
crash report's notes (`FrontLog`, `Log`).

## Adding a screen or a setting

A setting: add a row to `kSettings` under its tab, naming its read and write. An on-or-off
setting in the settings file takes `ReadFlag` and `WriteFlag` (or a write of its own that calls
`StoreFlag` and then its owner's setter) with its key in `pszKey`; anything else gets a read and
write pair above the table, the write calling its owner's setter so Accept applies it live.
Say in the row when it applies ("live", "next game", "restart"). A row with no engine support
has no read and no write and fills `pszNeeds` with what it waits on; it needs a `Bind` only
when the dialog must treat it its own way. Write the label and the note as a player reads them: no take
numbers and no internal names, and a greyed row says "Not available yet", which `Tip` adds
from `pszNeeds`. Mark the row's text `PT_` and run `python tools/locale_catalog.py extract`.

A screen: put it in its own `port_front_<name>.cpp`, declare its `Open`, `Screen`, `Keys` and
`Result` in `port_frontscreens.h`, and give its result a `FRONT_*` value there. Add a
`SCREEN_*` (`port_front_internal.h`), name it in `ScreenId` (`port_front.cpp`), give it its branch in
`Home`, `Keys` and `Choose` (home is the step back from it), and read its `Result` in
`RunScreen`. Lay it out with the kit, and with `port_frontlobby.h` for anything lobby-shaped.
If it starts a game, say so in `Launches` so it fades to black first, and give its panel an id
`Return` can fade in. If the planet should move for it, call `WorldSetView`. Its text goes
through `PortText` and its kin, never English joined with `+`; then run `locale_catalog.py
extract` and commit `hyperion.pot` and the `.po` files with the code.

The skirmish, the benchmark and the trainer build their set-ups from the same pieces in
`port_frontlobby.h`, each with its own sizes: the option rows are 54 high in the skirmish,
46 in the benchmark and 48 in the trainer. Each screen keeps its own rule for when teams
exist, its colours (the skirmish's are the game's 65 and any colour, below; the benchmark's its
32), and its computer levels' tip, whose wording differs. `MapLayouts` keeps one map's
layouts at a time, which is enough while one set-up shows at once.

## Integration points

- `src117/lang/gluMain.cpp`, `gluCreat.cpp`, `gluCmpgn.cpp`, `gluExpCmpgn.cpp`,
  `gluLogin.cpp`: the five places a port function stands in for a glue screen.
- `src117/lang/gluScore.cpp`, `do_score_dlg`: `PortFrontScore` in the score dialog's run;
  `opt_dlgs.cpp`, `quit_to_menu_respond`: `PortFrontScoreAfterLeave`.
- `src117/lang/event_Win.cpp`: `PortFrontWndMsg` in the window procedure.
- The game menu: `modeless.cpp` (`PortGameMenuFor` in `modeless_dlg_startup`, which is split
  into `modeless_dlg_enter` and the load), `dlgFatal.cpp` (`PortGameMenuDrop` in
  `modeless_dlg_shutdown`), `gameloop.cpp` (`PortGameMenuStep`), `opt_dlgs.cpp` (what it shows
  and does, in bodies 1.17's handlers now share), `help.cpp` and `tips.cpp` (their tables),
  `VictDlg.cpp` (Victory's and Defeat's text and choice), `filters.cpp` (Alliances and Messaging,
  whose commands are `AlliesCommit` and `MessagesCommit`, shared with 1.17's handlers).
- `port_gpu.cpp`: `PortGpuUiBegin`/`PortGpuUiEnd` draw the frame; `shaders/ui.hlsl` is the one
  pass (`render.md`). `port_present.cpp`: `PortPresentFrontBegin` and
  `PortPresentFrontEnd` hand the window between the game and the menus, and `PortDisplay*` are
  the display settings.
- `port_settings.cpp`: the settings file, which also holds 1.17's own preferences.
- `port_launch.cpp`: `PortLaunchStart`, `PortLaunchReplayFile`, `PortLaunchLoadFile`,
  `PortLaunchMapFile`, `PortLaunchCanvas`, `PortLaunchSkirmishGame`.
- `port_mods.cpp`: the active mod gives the home screen its name, world, main button and mode.
- `port_control.cpp`: the live channel reads and sets settings through `PortSettingsLiveJson`,
  `PortSettingLiveWord` and `PortSettingLiveSet`, asks for snapshots, and can push a map or a
  canvas into `PortFrontLaunchNext`.
- `port_casc.cpp`, `port_smacker.cpp`: the Remastered install Settings points to, and 1.17's
  campaign clips the episode cards play (with a Remastered install alone, its `.webm` of the
  same name, whose clear ground is keyed out as 1.17's black is: `ClipOpenWebm`).
- `port_front_lan*.cpp`: the LAN screens run inside this loop (`LanOpen`, `LanScreen`,
  `LanResult`); they are the LAN doc's.

## Gotchas

- Every `Open` needs its `Close`. A `Close` with nothing open would close Clay's root and
  corrupt the layout from there on, so `LayoutCheckEnd` refuses it and logs the caller once;
  a frame that leaves elements open says so in the log.
- Clay keeps scroll offsets for only ten clipping elements and releases those not laid out in
  `Clay_UpdateScrollContainers`, which `UiLayoutBegin` calls every frame. Cut text to fit
  (`Elide`) rather than clipping every cell, or a panel that does need to scroll gets no
  record.
- `g_strings`, `g_draws` and `g_customs` are deques because Clay holds pointers into them
  until the layout is drawn: keep a string alive with `Keep`, never a pointer into a local.
- A dialog takes keys only from the frame after it shows (`g_nFrameNo`), so the key that
  opened it does not also answer it.
- Settings' values are not live: `g_set` is the dialog's copy, and only `SettingsAccept`
  writes. A row with a read and no write draws as a setting that cannot be changed.
- Accept runs inside the menus' frame, which holds a swapchain image until it is shown, so a
  setter it calls must not change the swapchain there: `PortGpuVsyncSet` and `PortGpuAheadSet`
  hold their change for the next frame (`GpuSwapchainWaiting`, `render.md`). Over a game it
  waits for `SettingsGameStep` instead.
- `HYPERION_FRONT_SNAP`, `_OPEN` and `_SETTINGS` suppress the entrance, so a check sees the
  screen settled; `HYPERION_FRONT_ENTER=1` brings it back.
- If `PortGpuUiBegin` fails ten times on the first frame the front end gives up, logs why and
  lets 1.17's menu run for the rest of the session.
- `AfterGame` consumes the one visit to the campaign screen per game, so an early return out
  of `PortFrontCampaign` must not skip it.
- A game in a background window stops while it has no focus unless the live channel is open
  or Settings' Run in the background is on (`hyperion-exe.md`), so a check of the game menu passes `--control`, or the menu asked for by
  `HYPERION_GAME_MENU_OPEN` never opens. A window capture that waits for a new frame gets none
  while the menu holds a single-player game; `PrintWindow` takes the last one.
- A table or picture 1.17 loads with `load_game_data` must be there: a missing file is 1.17's
  fatal file error, which ends the game with no crash report. Check a Remastered install's
  files too, which serve 1.17's tables only where `port_remastered.cpp` makes them (Keystroke
  Help's `help_txt.tbl` was missing until 2026-10-25); `PortHelpOpen` looks for its table first
  and the panel says so when it is missing.
- A real victory or defeat comes a few seconds after the last building goes (`victory_update`'s
  check), not at once. A check that removes a player's units through the live channel (`clear`,
  single player only) waits for the panel before it answers it, or its key reaches the game.
- 1.17's own dialogs answer a letter by its character (`WM_CHAR`); the game menu, like the
  menus, by the key going down. A script driving both posts both. A posted click reaches a
  background window only with `HYPERION_TEST_MOUSE=1` and `PORT_TEST_MOUSE` (0x1000) in its
  `wParam`; a posted Alt key does not reach 1.17's accelerators, which read the keys' state.

## Open items

- Collections and Hotkeys in the Menu are greyed (`Enabled`'s `kNotYet`).
- The mission briefings, 1.17's title screen and the multiplayer ID screen are still 1.17's.
- Over a game, Settings has no CPU throttle row (1.17's Speed dialog's): Performance's "Rest
  between frames" covers it, and Remastered has none.
- Victory's Continue Playing, in a multiplayer game, has not been tried: the live channel's
  `clear`, which a check ends a game with, is single player only.
- The score screen has the mockup's StarCraft II look; its Remastered look is to come. A LAN
  room's game ends on 1.17's score dialog's way home, which has not been tried with the native
  screen up.
- Four Settings rows have no read or write yet, each with its `pszNeeds`: smooth unit turning, the language, text size and the hotkey editor. Unit voices, Cinematic
  subtitles and the Mature language filter are the data layer's (`data.md`, "Sound" and
  "Video"), Classic, on and on unless set, and need the Remastered install as the HD art does.
  Art's Remastered HD, HD lighting and Unit voices' Remastered are Remastered's paid content,
  held by the user's decision of 2026-10-21: greyed, with the reason (`PaidSetting`,
  `PortRemasteredOwned`, `data.md`). Mouse sensitivity and Hardware cursor are the present
  layer's (`render.md`, the settings it reads), both off unless set: the cursor from half
  Windows' speed to twice it while it is held in the window, and the game's cursor drawn by the
  system with the original's art. Master volume and
  Sound in the background are the mixer's (`data.md`, "Sound"); both start as the original
  plays, at full and silent behind another window. Classic art filtering is the presenter's
  (`render.md`, the frame's steps), Sharp unless set; Console skin and Console layout are the
  console's (`render.md`, "The console and the overlay"), by race and to the edges unless set;
  Player colours is full colour's (`render.md`, "Full colour"), Exact unless set.
- The skirmish set-up greys computer personalities and locked teams, the game chooser and team
  columns past 24 players. Its observers' frame has one observer's slot in Skirmish's own set-up,
  the player or empty (`ObserverRow`, `gameplay.md`, "Watching a skirmish of computers");
  observers of a multiplayer game wait for multiplayer.
- Online, Leaderboards and Replay TV on the replays screen wait for online play.
- Hyperion's menus do not draw in a browser build.

## Further reading

- `../hyperion-exe.md`: the program, the data folder and the rest of the environment.
- `render.md`: the GPU presenter and the one UI pass.
- `loop.md`: the game loop and the settings the Performance tab binds to.
- `../../../docs/versioning.md`: the release notes the home screen's notifications come from.
- `../../src117/port/front/port_frontscreens.h`: the contract every screen implements.
