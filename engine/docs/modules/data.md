# data: archives, formats, video, sound, maps and mods

## Overview

Hyperion reads every byte of game content itself. The player's `storm.dll` is never loaded:
the SFile, SBmp, SVid, SBig, SMem, SStr, SReg and SErr functions the game calls are the
port's own code in `src117/port/`, keeping Storm's names, ordinals' shapes and `__stdcall`,
so `src117/lang/` calls them unchanged. Under them sit StormLib for the MPQs, CascLib for a
StarCraft: Remastered install, StormLib's pklib for PKWARE's implode and explode, a Smacker
decoder of the port's own in place of RAD's `smackw32.dll`, libvpx, libwebm and dav1d for
videos in full colour (Remastered's and a mod's), with the system's own decoders for H.264,
H.265 and AAC, and DirectSound on SDL audio.

On top of that the port adds what 1.17 had no notion of: its own map format (`.hxm`),
maps saved by Remastered (STRx, CRGB), and mods that bring new unit, weapon, image and
tech rows with their image scripts and art.

## Status

Done and in use. Storm was replaced family by family (2026-09-28
to 2026-09-29) and each family was compared against the player's `storm.dll` by
`tools/porttest/storm_test.cpp` before it landed; the Smacker player was compared against
`smackw32.dll` by `tools/porttest/smk_test.cpp`. `Hyperion.exe` imports nothing from
`storm.dll` and loads no `smackw32.dll`. The replay suite is the gate for anything here
that a game can see (`../replay-test.md`).

Storm's network functions, `port_stormnet.cpp` and `port_stormlan.cpp`, are the LAN area
and have their own doc; this one only notes that they exist.

## Key files

| File | What it holds | Key functions |
|---|---|---|
| `port_stormfile.cpp` | Storm's `SFile*`: archive list, file lookup, reads, the music streams | `SFileOpenArchive`, `SFileOpenFileEx`, `SFileReadFile`, `SFileLoadFileEx`, `SFileDdaBeginEx`, `FileDdaService` |
| `port_stormlib.cpp` | the one file that sees StormLib's header; MPQ open, read, layout, write | `StormLibOpenArchive`, `StormLibOpenFile`, `StormLibReadFile`, `StormLibFileLayout`, `StormLibWriteArchiveFiles` |
| `port_stormlib_names.h` | StormLib's public names given an `Sl` prefix, so none meets Storm's | (macros) |
| `port_stormbase.cpp` | Storm's memory, string and registry functions; a block keeps malloc's 16-byte alignment, which GCC and Clang take `operator new`'s blocks to have | `SMemAlloc`, `SMemReAlloc`, `SStrHash`, `SRegLoadValue`, `SRegSaveString`, `PortMemSize` |
| `port_stormerr.cpp` | Storm's last error, apart from Win32's, and its error texts | `SErrSetLastError`, `SErrGetLastError`, `SErrGetErrorStr` |
| `port_stormbmp.cpp` | PCX decoding, the only image format 1.17 gives Storm | `SBmpDecodeImage`, `SBmpLoadImage`, `SBmpAllocLoadImage`, `SBmpGetPitchForAlignment` |
| `port_stormbig.cpp` | Storm's big numbers, for the map signature check in `signature.cpp` | `SBigFromBinary`, `SBigPowMod`, `SBigToBinaryBuffer` |
| `port_stormvid.cpp` | Storm's video player: a Smacker file into a bitmap or onto the screen | `SVidPlayBegin`, `SVidPlayContinueSingle`, `SVidPlayEnd`, `SVidGetSize` |
| `port_smacker.cpp` | the Smacker decoder and its DirectSound back-end; a video in full colour played as a Smacker file | `SmkOpen`, `SmkOpenVideo`, `SmkDoFrame`, `SmkToBuffer`, `SmkToBufferRect`, `SmkWait`, `SmkSoundUseDirectSound` |
| `port_video.cpp` | videos in full colour: libvpx's VP8 and VP9 decoders, dav1d's AV1, stb_vorbis and libopus for the sound, the pictures' colours to RGBA | `PortVideoOpen`, `PortVideoFrame`, `PortVideoSound`, `PortVideoKeyFrame`, `PortVideoExtensions` |
| `port_webm.cpp` | WebM and Matroska files, indexed with libwebm's parser for `port_video.cpp` | `VideoIndexMatroska` |
| `port_mp4.cpp` | MP4 and QuickTime files, indexed from their sample tables for `port_video.cpp` | `VideoIndexMp4` |
| `port_sysvideo_win.cpp`, `port_sysvideo_mac.cpp`, `port_sysvideo_ffmpeg.cpp` | H.264, H.265 and AAC through the system's decoders: Media Foundation, VideoToolbox and AudioToolbox, the player's FFmpeg on Linux (`port_sysvideo.h`) | `SysVideoOpen`, `SysVideoSend`, `SysVideoPicture`, `SysSoundAac`, `SysVideoNeeds` |
| `port_chatfilter.cpp` | Settings' Mature language filter: other players' chat shown with Remastered's list of profanity starred out | `PortChatFilterSet`, `PortChatClean` |
| `port_subtitles.cpp` | Settings' Cinematic subtitles: Remastered's subtitle files for the cinematics, drawn over them | `PortSubtitlesBegin`, `PortSubtitlesAt`, `PortSubtitlesEnd`, `PortSubtitlesSet` |
| `port_casc.cpp` | files out of a Remastered install through CascLib; its text and speech languages | `PortCascAvailable`, `PortCascRead`, `PortCascCheck`, `PortCascUse`, `PortCascFolder`, `PortCascTextLocale`, `PortCascSpeechLocale`, `PortCascHidden` |
| `port_remastered.cpp`, `port_remastered.h` | a Remastered install as one of Storm's archives: the files 1.17 asks for, found in CASC or made from what it has, in the game's language; which source a run reads | `PortGameSource`, `PortRemasteredLoad`, `PortRemasteredLocalDll`, `PortRemasteredStatTxtEnglish`, `PORT_REMASTERED_ARCHIVE` |
| `port_language.cpp`, `port_language.h` | the language chosen, the game's text and its speech (The install's language) | `PortLanguageSetting`, `PortLanguageChosen`, `PortLanguageGame`, `PortLanguageSpeech`, `PortLanguageNative` |
| `port_local.cpp`, `port_local.h`, `gen/local117.cpp` | what stands in for a missing `Local.dll` and the game types; the generated part written by `tools/local117.py` | `PortLocalUseOwn`, `PortLocalResource`, `PortLocalAccelerators`, `PortLocalDialog`, `PortLocalTemplate` |
| `port_install.cpp`, `port_install.h` | which kind of install the game's files come from, as the player chose it or the command line names it, and the first start's choice, both installs linked and the base, in Windows' own window | `PortInstallChosen`, `PortInstallCommandLine`, `PortInstallChoose`, `PortInstallFound`, `PortInstallCheckFolder`, `PortInstallStartFolder`, `PortInstallBaseFor`, `PortInstallKeepBoth`, `PortInstallFirstStart` |
| `port_install_window.cpp` | the first start's window as Hyperion draws it, with SDL and stb_truetype: on Linux and a Mac | `PortInstallWindowRun` |
| `port_install_dlg.h` | the Windows window's resource and controls, shared with `hyperion.rc` | `IDD_PORT_INSTALL` |
| `port_remastered_keys.h` | 1.17's `gluAll.tbl`, `network.tbl`, `tips.tbl`, `gluHist.tbl` and `help_txt.tbl` in 1.17's order as the keys of Remastered's locale XML, written by `tools/remastered_keys.py` | `kRemasteredGluAll`, `kRemasteredNetwork`, `kRemasteredTips`, `kRemasteredGluHist`, `kRemasteredHelpTxt` |
| `port_pklib.cpp` | PKWARE implode, explode and CRC-32 under the game's names | `implode`, `explode`, `crc32_pklib` |
| `port_formats.cpp` | the file formats that hold pointers once loaded, laid out for 64 bits | `PortGroupNative`, `PortDialogNative`, `PortStarsNative` |
| `port_dsound.cpp` | DirectSound on one SDL audio stream | `PortDirectSoundCreate`, `PortDirectSoundCreateAny`, `PortSoundForeground`, `PortSoundMaster`, `PortSoundBackground` |
| `port_hxm.cpp` | the `.hxm` package: zip reading, compile to CHK and MPQ, the cache, the checker | `PortHxmIsMap`, `PortHxmCompiled`, `PortHxmCompileCommand`, `PortHxmCheckCommand` |
| `port_hxm_names.h` | 1.17's units, orders, conditions and the rest by their `.hxm` names | `kHxmUnits` and the other tables |
| `port_map.cpp` | what Remastered's maps add to 1.17's: the STRx string table, CRGB colours | `PortMapStringWide`, `PortMapStringNarrow`, `PortMapColorsSet`, `PortMapColorsApply` |
| `port_mods.cpp`, `port_mods.h` | the installed mods and which one is active | `PortMods`, `PortModActive`, `PortModSetActive`, `PortModsFolder` |
| `port_moddata.cpp`, `port_moddata.h` | a mod's new type rows, edits of rows already there, requirement trees, cards, races, art, sounds, portraits, icons and wireframes, pushes; the data of the mods it depends on first | `ModDataStartUp`, `ModDataForGame`, `ModDataPush`, `ModTypeByName`, `ModUnitTree`, `ModTreeEdit` |
| `port_modcheck.cpp`, `port_modcheck.h` | `--mod-check`: a mod's load and every file it names decoded, reported with no game | `PortModCheckRun`, `ModDataCheckResult` |
| `port_modsheet.cpp`, `port_modsheet.h` | a mod's art sheet as classic frames: colours, team pixels, the canvas, the encoding; a classic GRP file or the engine's own group | `ModSheetCheck`, `ModSheetScaledDown`, `ModSheetEncode`, `ModSheetFile`, `ModSheetGroupNative` |
| `port_imagefile.cpp`, `port_imagefile.h` | PNG and TGA through stb_image, WebP through libwebp's decoder, into RGBA; or only their size | `PortImageDecode`, `PortImageInfo`, `PortImageFileName` |
| `port_typenames.cpp`, `port_typenames.h` | the original's rows by name, for every table a mod names rows of (`port_typenames117.h` and `port_hxm_names.h`, generated by `tools/hxmap/gen_cpp.py`) | `TypeNameFind` |
| `port_iscript.cpp`, `port_iscript.h` | a mod's image scripts compiled from text | `IscriptCompile` |

## How it works

### Where the data comes from

Four places, and they do not overlap:

- **Hyperion's own copy of 1.17's starting values.** `src117/gen/data117.cpp`
  (`platform.md`), with the two resources the game took from 1.17's exe; no exe is read.
  `Local.dll` is loaded as a data file for its strings, dialogs and accelerators
  (`PortLoadResources`); with none, Hyperion stands in for it (The install's language, below).
- **The MPQs beside it.** `open_archives` (`lang/init.cpp`) opens `Stardat.mpq` at priority
  0x7D0, `patch_rt.mpq` at 0x1B58 and, outside a spawned copy, `Broodat.mpq` at 0x9C4, and
  `FindCDArchive` adds the CD's archive. 1.17's own files in that folder are left untouched.
- **The data folder.** Everything Hyperion writes goes in `Data\` beside the exe, or in
  `--data <folder>`: profiles, replays, saves, maps, screenshots, `settings.json`, `mods\`,
  `cache\hxm\` and the `hyperion_*.log` files. 1.17's calls to Storm's registry functions
  land in `settings.json`, so vanilla's registry key is neither read nor written. Extra
  read-only map folders come from `--maps <folder>`, up to eight, and with StarCraft from
  Battle.net's files its `Maps` folder is one more (`platform.md`).
- **A Remastered install, for HD art and the front end's typeface.** CASC storage through
  CascLib; nothing requires it, and every caller falls back without it.
- **Or a Remastered install in the MPQs' place** (milestone B). `PortGameSource`
  decides once: `HYPERION_DATA_SOURCE`, else the `"data"` setting, else auto, which is the
  MPQs when `Stardat.mpq` is in the game's folder and Remastered when it is not and an
  install is found. With Remastered, `open_archives` opens `PORT_REMASTERED_ARCHIVE` at
  `Stardat.mpq`'s priority, and `port_stormfile.cpp` serves that archive's files from
  `PortRemasteredLoad`, each read whole into memory; the game is never a spawned copy
  (`init_cdkey`), the CD archive may be missing (`PortCdOptional`), and `Local.dll` comes from
  the install's `locales\enUS\`, or its text language's folder, when the game's folder has none. `both`, for development,
  opens Remastered's archive at 0x1F40, above every MPQ, which serve what it lacks.

### Which install, as the player chose it

Two kinds of install give the game's files, and the player chooses
(`port_install.cpp`): StarCraft from Battle.net, the free StarCraft, whose files are
Remastered's storage, or a classic StarCraft folder with its MPQs (1.16.1's and earlier, or
1.17's). `settings.json` keeps the kind as `"data"` (`remastered` or `mpq`) and each one's
folder, `"remastered"` and `"classic"`; Settings' Data and mods tab sets them (Game files,
Classic StarCraft folder, StarCraft from Battle.net), and so does the first start. The game's
folder (`GameFolder`, `hyperion.cpp`) is the `"classic"` folder when one is kept and
StarCraft from Battle.net is not the kind chosen, so the MPQs need not be beside
`Hyperion.exe`; the data folder stays beside it. `HYPERION_GAME`, `HYPERION_DATA_SOURCE` and
`HYPERION_REMASTERED` come before them, and with no kind kept `PortGameSource` finds one as
before. Either way the art is the classic game's (below).

A script names the install for one run on the command line, before everything else and
without keeping it: `--install classic` (or `mpq`) or `--install remastered` (or
`battlenet`), and `--install-folder <folder>`, the classic StarCraft folder (the game's folder
for the run) or StarCraft from Battle.net's (or a folder inside it). The folder alone is
enough: its files say which kind it is (`PortInstallCommandLine`). A run given either never
shows the first start; a word `--install` does not know, or files that are not there, end it
with a message that names what was asked.

A start a player made with no kind kept, or whose kept kind's files have gone (an install
moved), asks before `PortGameSource` decides, even when an install is found by itself. On
Windows it is a window of Hyperion's own (`hyperion.rc`'s `IDD_PORT_INSTALL`), with a part for
StarCraft: Remastered from Battle.net and one for Classic (1.16.1 and prior). Each has the
folder it wants (the StarCraft folder itself, which holds `StarCraft Launcher.exe`, `x86_64`
and `Data`; or the one with `StarDat.mpq` and `BrooDat.mpq`), a folder field with Browse
(Windows' folder dialog), and under it, checked as the folder changes, "Files located" in dark
green or "Files not located. Are you sure you chose the correct folder?" in dark red, with a
word when the folder is the other kind's, or "Not linked" in grey while it is empty. The fields
start with what settings.json keeps, else what is found (`PortInstallStartFolder`): StarCraft
from Battle.net where Windows' installed programs say the Battle.net app put it (the entry
`PortCascAvailable` reads) or its usual folder, and a classic folder beside `Hyperion.exe`.
Either may stay empty. Under them, "Use as the game's files:" chooses the base, the one the
game reads: only an install whose files are there can be chosen, and when only one is, it is
(`PortInstallBaseFor`). The check is the nearest folder at or above the one given whose
`.build.info` names StarCraft (`PortCascCheck`) for the one, `StarDat.mpq` in it for the other.
Start is there only when the base's files are. It keeps the other install's folder when its
files are there, linked to switch to in Settings, then the base and its folder, opening the
storage for StarCraft from Battle.net (saying so if it will not open; `PortInstallKeepBoth`),
and the start goes on; Quit ends it. On Linux and a Mac the same window is drawn by Hyperion
(`port_install_window.cpp`): an SDL window and renderer of its own, the text in Saira from the
program's resources, the system's light or dark theme (green and red lighter on dark), a folder
typed in the field clicked or reached with Tab, pasted (Ctrl+V, or Cmd+V on a Mac) or chosen
with SDL's folder dialog; on Linux it offers Lutris's and Wine's usual prefixes when StarCraft
is there. `HYPERION_INSTALL_WINDOW=drawn` shows it on Windows. With no window to show (no
display), the start says what is missing, as below. A start for a replay, a map, a saved game, the canvas, a benchmark, a lab
worker or a LAN room's seat, one with a hidden or background window, and one given
`HYPERION_GAME` or `HYPERION_DATA_SOURCE` never asks (`PortInstallMayAsk`): it ends with the
message naming the files, as before. Neither does the browser build, whose page gives the
files.

### What Hyperion does not read: Remastered's paid content

The free StarCraft is the classic game, but its install holds Remastered's paid content too:
the same HD files as an owner's install (measured 2026-10-21 against another install of
1.23.10, every HD folder the same; the sizes apart only the other languages' recordings).
Blizzard's game unlocks it after the purchase, and nothing in the files tells an owner from a
free player. By the user's decision of 2026-10-21 (PRD) Hyperion reads the classic game's files
alone, and HD is held. Every read from the install goes through `PortCascRead`
(`port_casc.cpp`), which refuses the paid files unless `PortRemasteredOwned`, false while HD is
held: by folder, `anim\` and `HD2\` (the HD art at both scales), `carbot\` (the Cartooned
skin), `effect\`, `parallax\` and `portrait\` (the HD effects, backdrop and portraits) and
`sound\` (the recordings Remastered made again); by kind anywhere, `.anim`, `.dds.grp`,
`.dds.vr4` and `.tmsk`, a console's `.dds` under `game\consoles\` and the HD cinematics'
`.webm` under `smk\` (their subtitles are not refused); the same within a
locale's `Assets\`. `SD\` is never refused. The classic game's own files share some of those
folders' neighbours (`tileset\`'s `.wpe`, `.cv5` and `.vf4`, `unit\`'s `.grp`, `glue\`'s
`.pcx`), and none of them is refused. With HD held, `PortHdAvailable` answers as it does
without an install (`port_hd.cpp`), Remastered's voices play the classic sounds
(`PortSoundVoicesSet`), and Settings greys HD art, HD lighting and Unit voices with the reason;
F5 and a stored `art` turn nothing on either (`GpuHdAllowed`, `render.md`).
`HYPERION_FILE_LOG=1` also lists each name asked of the install, read, missing or refused, in
`hyperion_casc.log`: the quick suite on a Remastered install alone read 3,224 names and refused
none. The replay harness runs every Remastered run with it and fails a run that reads a file
from an MPQ, or that is refused a name with no SD file read in its place
(`../replay-test.md`). A selected unit's portrait is the one refusal on the way: `Find` tries
`portrait\<unit>\<name>.smk` at the install's top, Remastered's HD portraits, after `SD\`, and
then the `.webm` stands in from `SD\portrait\`.

### What a Remastered install gives

`PortRemasteredLoad` looks for a name under `SD\` first, where Remastered keeps 1.17's own
files (its root has HD sounds and fonts of the same names), then as it is, then under
`locales\enUS\Assets\` and the install's text language's folder (the campaign, the game
templates; The install's language, below). Moved files are found by `kMoved`: the command
icons, `ticon.pcx`, the races' command buttons and dialog frames under `game\consoles\`, and
`parallax\star.spk`, which is `SD\TileSet\platform.spk`. Made from what it has: a tileset's
`.vx4` from its `.vx4ex` (32-bit entries, the first ones 1.17's numbers), for a reader that
asks for the `.vx4` (the engine reads the `.vx4ex`, below), `stat_txt.tbl` with the replay
screen's 8 strings Remastered dropped, `gluAll.tbl`, `network.tbl`, `tips.tbl`,
`gluHist.tbl` (the campaigns' mission titles) and `help_txt.tbl` (Keystroke Help, since
2026-10-25: without it the help closed the game, as `load_game_data` takes a missing file for a
broken install) from `SD\rez\*.xml` in 1.17's order (`port_remastered_keys.h`; the one tip
Remastered dropped, which told the player to buy StarCraft, is Hyperion's own, as is the help's
F1 line, which Remastered gives as F11; six mission titles are Remastered's corrected spelling,
"New Gettysburg" and a plain apostrophe for 1.17's 0x92), and `rez\DataVersion.txt`. The fonts
are 1.17's unencoded; with no key files `LoadFontFile`'s `DecodeFile` has no key and leaves
them be, so with `both` they come from the MPQs.

Checked on 1.23.10.13515 (2026-10-20): with `both`, the quick suite is identical while
Remastered serves 3,185 of the files; of the 1,062 files one replay took from it
(`HYPERION_REMASTERED_DUMP`), 1,052 are 1.17's bytes, `stat_txt.tbl` is 1.17's byte for
byte (since 2026-10-21: until then the table was rebuilt reading each string up to its first
NUL, which cut 274 strings, a plain command's tooltip among them, since a button's string is
its key, 0 when the tooltip has no cost line, then the text), `gluAll` differs in 2 strings and `network` in 3 (Remastered's wording), and each
tileset's `.vf4`, `.vr4` and `.vx4` is 1.17's with more after it. Remastered's `.cv5` of
Twilight, Desert and Platform differ only in groups that are empty in 1.17's (227, 254 and 91
from group 770 on, its newer doodads), which no vanilla map places; a map that places an
empty group on purpose would walk differently.

The newer doodads, the tiles Remastered 1.22 added, name cells past what 1.17's 16-bit
references hold: Desert's `.vr4` has 85,278 cells, and a `.vx4` reference is the cell times
two and a mirror bit. So the engine holds a tileset's megatiles as 32-bit references
(`PortMegatilesLoad`, `port_formats.cpp`): the `.vx4ex` as it is when the source has one,
otherwise the `.vx4` widened. `cell_draw_mtx`, the minimap, the GPU scene (`scene.hlsl`'s
tileset buffer) and the lobby's map preview read them so. With a Remastered install as the
source, recent ladder maps that use those tiles now play (before, every one tried was
refused), their terrain and the walkability its `.vf4` gives coming from the install; checked
on Butter 1.4 (Desert): every whole tile of two views, 135 of them naming cells past 16 bits,
drawn exactly as the tileset gives it, through the GPU scene and through the SDL renderer, and
99% of those tiles' minimap pixels (0.6% before). With 1.17's MPQs alone such a map is
refused as before, since 1.17's tilesets do not have the groups.

With a Remastered install alone, more is made or stood in for:

- the console's pictures, `game\<r>console.pcx`, `<r>conover.pcx`, `<r>pbrempt.pcx` and
  `<r>pbrfull.pcx`, from `SD\game\`'s DDS (32 bits a pixel, DXT1 or DXT5) put in the 188
  palette entries the eight tilesets share, clear under half alpha. Remastered's console is a
  strip of 640 x 180 to 192 that goes at the foot of 1.17's 640 x 480; so placed, every
  console comes back as 1.17's pixel for pixel but the eight that 1.17 draws in entry 200,
  which changes with the tileset and which Remastered paints (35, 35, 255): those get 200
  back. Checked: the made `nconsole.pcx` is 1.17's. The portrait's frame and the bars are DXT
  and come near 1.17's (the empty bars exactly); the full bar, which Remastered drew 12 rows
  high, is scaled to 1.17's 9;
- the glue screens' pictures (`glue\<folder>\<name>.pcx`: backgrounds, buttons, popups),
  from `SD\glue\`'s DDS, which hold 1.17's pictures in every pixel (`GluePicture`). Each is put
  in the palette of the screen that draws it, which Remastered keeps at the end of each
  screen's `glue\Pal<xx>\tFont.pcx` (the same 256 entries as 1.17's `BackGnd.pcx`):
  `Pal<xx>` is itself, `Score<xx>` takes `Pal<xx>`'s, `Ready<r>` `PalR<r>`'s, `campaign`
  `PalCs`'s, `mainmenu` `PalMm`'s, `title` its own, and the rest `PalNl`'s. A clear pixel is
  entry 0. Checked against 1.17's 235 such pictures: the same colour in every pixel but those
  of the two main menu buttons Hyperion's front end does not show and one of `PalCs`'s
  `arrhot`; the made `PalMm\BackGnd.pcx` is 1.17's index for index;
- each glue screen's dialog pieces, tiles and cursor (`glue\Pal<xx>\Dlg.grp`, `tile.grp`,
  `arrow.grp`), which Remastered has only redrawn (`GlueGrp`): `PalNl`'s `Dlg.grp`, which it
  keeps as 1.17's, and the game's `dlgs\tile.grp` and `cursor\arrow.grp`, each entry put to
  the nearest of the screen's colours (77 to 84% of `Dlg.grp`'s pixels exactly 1.17's).
  Should either not be made, the game's own of the same kind stand in (`kStandIn`);
- the chat room's icons (`glue\chatroom\iChat.grp`), which Remastered does not have at all:
  1.17's eleven frames at their sizes, every pixel clear (`ChatIcons`). A room's match runs
  1.17's chat room unseen, which reads the file as it opens and stopped the game without it,
  so a host on Remastered's files left each match as another player joined (2026-10-26).
  1.17's own error dialogs (`ErrorDlg`, `FileErrorDlg`) now also write their words to
  `hyperion_crash.log` (`PortCrashFatal`), since a dialog under the match's cover is never seen;
- the start-up splash (`rez\titledlg.bin`, `glue\title\title.pcx`) is left out: the screen
  stays black while the game loads its graphics, about two seconds. 1.17 loads them faded
  out then, and `img_update_real` copies its black frame only when the screen was written
  since (`PortScreenWrites`): copied on each of the load's 8,000 updates (999 images, their
  GRP and seven overlay files each), each a frame presented, the black lasted 40 seconds at
  120 Hz and over a minute at 60 (2026-10-21, seen on another PC as a hang after the intro).

The dialogs' layouts (`rez\*.bin`) are Hyperion's source (PRD 2026-10-20): `tools/layouts117.py`
wrote 1.17's 70 into `src117/gen/layouts117.cpp` and `port_layouts.cpp` makes each file back
from the table, byte for byte, whenever the source is not the MPQs alone. Remastered's
`SD\rez\*.ui.json` could not serve: compared layout by layout, about 40 of the 60 1.17 has
differ in their rects, ids, controls or scale, and their types do not map one to one. With
that, a Remastered install alone plays the full replay suite identical to 1.17
(`rt_runs/20261020_064055`), and Hyperion's menus run on it. Since 2026-10-22 every gate
plays its replays on the install alone as well as on the MPQs (`build.md`, the gates). The videos are there: each `.smk` 1.17 asks for
that the archives do not have is played from Remastered's `.webm` of the same name (Video,
below).

### The install's language

Battle.net's app installs the files of the one text language chosen in its settings, and of
one speech language. Every language's install has English's menu text
(`locales\enUS\Assets\rez\`), `stat_txt.tbl` and the error pop-up's pictures
(`SD\glue\error\`), so the game runs in English from any of them. Only English's has the game
types (`locales\enUS\Assets\Templates\*.got`), and only English's, German's, French's, Spanish's
and Italian's have a `Local.dll`, on Windows alone.

- `PortCascTextLocale` and `PortCascSpeechLocale` read the languages from `.build.info`: on the
  active line, the tags' text and speech groups end with them (`enUS speech?`, `enUS text?`).
  The start-up log, the system part of a report and the first start's check name them.
- `port_language.cpp` decides the game's language once: Settings' Language (`"hyperion"` /
  `"language"`, or `HYPERION_LANGUAGE`), `install` by default, which is the install's text
  language. `PortLanguageGame` is that language when the game reads the install and the install
  has its text (`locales\<lang>\Assets\rez\stat_txt.json`), else English. `PortLanguageSpeech`
  is the install's speech language when it is German, Spanish, French or Italian, the four
  with the classic speech, else English. The start-up log's "language:" line names all three.
- In another language, `PortRemasteredLoad` builds the game's text from the language's JSON
  files (`locales\<lang>\Assets\rez\*.json`, a byte-order mark and an array of `Key` and
  `Value`, each value escaped twice):
  - `stat_txt.tbl` from `stat_txt.json`, entry for entry, as its order is the classic table's.
    A button keeps English's hotkey and kind when the translation's key is not an ASCII letter
    or digit, or its form is broken; an empty entry keeps English's. The text is UTF-8, which
    the game draws (Fonts, `render.md`). A table past 64 KB in UTF-8 (Russian's) is written in
    a wide form, a 0xFFFF word, then a 32-bit count and offsets, which `str_get_string` reads.
  - the keyed tables (`gluAll`, `network`, `tips`, `gluHist`, `help_txt`) from their JSON by
    key over English's; the help keys and a few network ones write `-` where the XML has the
    last `_`.
  - the classic dialogs' text from `rc3.json`, matched by English's text within the dialog,
    else in any dialog; the keys' numbers do not follow the controls' order.
  - the game types' names and parameter labels from `gametype.json`, over the classic ones.
  - the credit and epilogue pages from the language's `SD\rez\`, else its `rez\`.
- A sound or `.wav` or `.ogg` file is looked for in the speech language's `SD\` first, the rest
  in the game language's `SD\`, then English's: German's, Spanish's, French's and Italian's
  installs have the classic campaign maps and speech translated there. A cinematic is played
  dubbed from the speech language's `SD\Smk\` when it has one.
- An EUD map reads English's `stat_txt.tbl` in every language, so that its offsets hold
  (`PortRemasteredStatTxtEnglish`, `port_eud.cpp`); the game then shows English's text.
- `SStrCopy` leaves a UTF-8 character out whole when its byte limit falls inside one: a
  translated name in the classic's 25 bytes.
- The game types are looked for in English's folder alone, then the classic ones stand in
  (`PortLocalTemplate`), so they agree with `templates.lst`, the same in every language.
- With no `Local.dll` in the game's folder or the install's, `InitLocal` goes on with Hyperion's
  stand-in (`port_local.cpp`) and the start-up log says so:
  - `Local.dll`'s three key tables (0x65, 0x66, 0x67) and the pop-up's two layouts, written
    from the classic `Local.dll` by `tools/local117.py` into `src117/gen/local117.cpp`, with the
    30 classic game types;
  - the pop-up's two pictures from the install's `SD\glue\error\`, the same bytes as
    `Local.dll`'s, or a plain frame Hyperion makes when there is no install;
  - the dialogs the game reaches (the CD, an old processor, little memory or virtual memory,
    little disk space) as Hyperion's own message box, and the file error none, since the
    start-up's window has said it;
  - the locale 0x409, as before.
- Subtitles: the game language's when the text face (Saira) has their letters, else English's:
  it has the Latin scripts', none of Cyrillic's or the East Asian ones'.
- `HYPERION_TEST_TEXT_LOCALE=<locale>` makes the install found one in that language, text and
  speech, for a test: the storage's tags (CascLib's `TagBitMask`) name the files that have a
  language's tag but not that one's, and `PortCascRead` refuses them as such an install would
  not have them. On an English install `plPL` leaves out 30,761 files, the 30 game types among
  those a game reads.
- `HYPERION_TEST_LOCALE_FILES=<folder>` serves the `locales\` files a test's language needs
  from a folder, under their names in the storage, as the English install does not have them.
  `tools/assets/fetch_locale.py <locale>... [--speech]` fills one from Battle.net's servers
  (CascLib's online storage) into the gitignored `engine/testrun/locales_cdn/files/`.

### A file request, end to end

`SFileOpenFileEx` (`port_stormfile.cpp`) does what `storm.dll` did, read off its code:

1. With scope bit 2, the name's last part under the exe's folder; then with bit 1 the name
   itself when it is absolute, else under the exe's folder. A hit there is a plain Win32
   handle wrapped in an `SFileFile`.
2. Otherwise the archives, either the one handle given or every open archive in search
   order: descending priority, and among equal priorities the one opened last first. A name
   is looked up in the locale `SFileSetLocale` set, falling back to a neutral entry, which is
   what StormLib does too.
3. Scope bit 4 hands back a raw Win32 handle positioned at the file's data inside the
   archive file, which only works for data stored as it is; `StormLibFileLayout` adds
   StormLib's byte offset to the archive's header offset, because a CD archive starts some
   way into its file.

`SFileReadFile` serves a read with Storm's request block (its own `OVERLAPPED`: the position
at +8, an event at +0x10) at once and signals the event, where Storm queued it on a worker
thread; the game waits on the event either way. A read that comes up short returns FALSE
with `ERROR_HANDLE_EOF`. A read that fails goes through the I/O error mode
`SFileSetIoErrorMode` set: 0 gives up, 1 asks the callback, 2 asks the player with a message
box, more retries.

`StormLibOpenArchive` opens every archive read-only, as MPQ version 1 whatever its header
says, with no list file and no attributes. StormLib sets Win32's last error as it goes, where
`storm.dll` left it alone, so every function in `port_stormlib.cpp` restores it with
`StormLibKeepError` and hands the error back itself.

### Formats that hold pointers

`port_formats.cpp` exists because three of 1.17's formats keep a file offset that the
original code turns into a pointer in place, which no longer fits at 64 bits:

- **GRP.** `PortGroupNative` builds one new block with the header, a frame table in the
  engine's `FRAME` layout and the file after it, each frame's `pData` pointing into that
  copy. One block, so the `SMemFree` the game calls frees all of it. At 64 bits a frame's
  place and size are WORDs and it has `pLines`: NULL for a file's frames, whose rows start at
  the WORD offsets at `pData`, or a DWORD table for a frame made from a mod's sheet, which may
  be past 255 a side and 64 KiB. Every reader of a frame's rows goes through `GrpFrameRow`
  (`h/grp.h`).
- **Dialogs (`.bin`).** `PortDialogNative`, which `dlg_load` calls first, lays the 0x56-byte
  `ctrl_t` records and FLC `flc_t` elements out in the engine's layout, still linked by
  offsets, so `dlg_load_ctrl`'s relocation and everything after it run as they did.
- **`parallax\star.spk`.** `PortStarsNative`, the same idea for `starent_t`.

The same file also holds the saved game's 1.17-layout record helpers (`PortSave117Put`,
`PortSaveChunk` and the rest); those belong to saves, not to asset loading.

PCX is the only image format `port_stormbmp.cpp` decodes, because PCX is all 1.17 ever gives
Storm; BMP and Storm's third format are refused. One `storm.dll` quirk is deliberately not
kept: a run that crosses the end of the output is cut there, where Storm's fill ran on past
the buffer by up to a row.

### Video

`port_stormvid.cpp` is Storm's `SVid*` over `port_smacker.cpp`, through the same calls
`storm.dll` made on `smackw32.dll`. A video plays into the caller's bitmap (portraits, the
menus' animations) or onto the screen (the cinematics), at its own size or twice it. Doubling
goes through a buffer of the video's size, with the pixels between either repeated or blended
through a 64 KB table of the palette's nearest colours, built the way `storm.dll` built it so
ties go the same way. Flags 0x8 and 0x800 take the scale and sub-mode from the
`"Video Player"` / `"Mode"` setting. The screen is the port's, reached through
`PortVidScreenSize`, `PortVidLock`, `PortVidPalette` and `PortVidClear` in
`port_present.cpp`, where `storm.dll` went through SDraw.

`port_smacker.cpp` was written from the public description of the format and smackw32's
disassembly. A frame is a palette change, a piece of each sound track and 4x4 blocks in runs
of four kinds; the four Huffman trees are packed bit by bit, least significant first, and the
three 16-bit ones keep their last three values on leaves of their own. A frame is painted
straight onto the caller's buffer, and the rects of what changed come from smackw32's map of
changed blocks, walked its way, so `SmkToBufferRect` reports what smackw32 reported. The
sound plays on smackw32's DirectSound back-end: the video waits until the sound has played
the frames decoded, and drops a frame when the sound is half a second past it. A video larger
than its room, in a bitmap (a mod's portrait) or on the screen (a mod's cinematic), is decoded
into a buffer of its own size and sampled into the room, its shape kept and centred
(`VidVideo`'s `pFull` and fit box); one for a bitmap of no size, or doubled in a bitmap, is
refused as before. Left out,
because 1.17 never reaches it: SMK4 files, `SmackToScreen`, 16-bit destinations, and the
Miles, DiamondWare and `waveOut` back-ends.

Settings' Cinematic subtitles (`cinematicSubtitles`, on unless set off) shows Remastered's
subtitles over the cinematics that speak. When a video starts on the screen, `SVidPlayBegin`
names it to `port_subtitles.cpp`, which reads `locales\enUS\Assets\Smk\<name>.srt` from the
Remastered install, or the install's text language's when it has no English ones and the text
face has their letters (a file that is not UTF-8 is read as Windows-1252). Each frame drawn gives the time into the video by its own frame count and frame
length (`SmkInterval`), so the lines keep to the picture when frames are dropped, and a layer
of the overlay draws the line due in the lower letterbox (`render.md`, the overlay's layers).

#### Videos in full colour

A Remastered install has no Smacker file: its cinematics (`SD\smk\*.webm`, VP9 with Vorbis
sound), the units' portraits (`SD\portrait\...\*.webm`, VP9 in 4:4:4 with the sRGB colour
space, so a picture's planes are green, blue and red) and the menus' animations
(`SD\glue\mainmenu\*.webm`, VP8 with an alpha picture beside each frame) are WebM. A mod's
portraits may be WebM, Matroska (`.mkv`), MP4 (`.mp4`, `.m4v`) or QuickTime (`.mov`). When
`SVidPlayBegin` cannot open a `.smk`, `VidOpenVideo` reads the first file of the same name
through Storm with one of the containers' extensions (`PortVideoExtensions`), and
`SmkOpenVideo` plays it as a Smacker file, so the rest of the player is the same for all.

A cinematic (a video `play_flic` puts on the screen: its flags name the screen and neither a
bitmap nor a handle; the menus ask their animations' size with no flags) looks for copies of
its own first. The active mod's comes first, by the cinematic's path in its data folder with
any of the extensions or `.smk`, its chain newest first (`ModDataVideoPath`); then, with the
GPU presenter, Remastered's in HD (`PortRemasteredVideoHd`): `smk\<name>.webm` at 1920 by 1080,
or `HD2\smk\` at 960 by 540 for a window of 540 rows or fewer. Those are paid content and are
refused while HD is held (above), so the `SD\smk\` copies, 320 by 308, play. A file the
system cannot decode is passed over for the next. With the GPU presenter a video that is not
Smacker is opened for its planes (`SmkOpenVideoPlanes`, `PortVideoPicture`) and drawn in full
colour at the window's size (`render.md`, A cinematic in full colour), at the classic player's
shape: twice as wide where twice fits 640 columns, and twice as tall where 480 rows fit that
too, so the classic cinematics' 320 by 308 shows as 640 by 308. A mod's own cinematic shows
the `.srt` beside it in place of Remastered's subtitles when it has one (an empty one shows
none).

`PortVideoOpen` indexes the file at open, by its first bytes: `port_webm.cpp` reads Matroska
with libwebm's `mkvparser`, and reads each block group's additions itself, which `mkvparser`
skips, for a WebM alpha picture; `port_mp4.cpp` walks an MP4's boxes to its sample tables (a
fragmented MP4 is not read). Both fill a `VideoIndex` (`port_videoindex.h`): each frame's
place, time and key flag, each sound packet's, and the codecs' setup. The pictures decode in
`port_video.cpp`: VP8 and VP9 by libvpx, with a second decoder for the alpha picture, and AV1
by dav1d, given frames ahead on its own threads past 1280 by 720. libvpx runs on one thread:
its threads, as configured here, do not give the same pictures as one.

H.264 and H.265 go to the system's decoder (`port_sysvideo.h`); Hyperion ships no code for
them. `port_video.cpp` reads the parameter sets from the avcC or hvcC (or, for avc3 and hev1,
from the first key frame) and gives the frames in decoding order, each with its rank in
showing order, from the frames' times; a picture comes back with its rank, and going back
starts again at the key frame before the one wanted. On Windows, Media Foundation's
synchronous decoders (`MFTEnumEx`; `mfplat.dll` loaded at run time, as Windows' N editions
lack it) take the frames in Annex B's form, their parameter sets before each key frame, and
give NV12 or P010. H.265 needs Microsoft's HEVC Video Extensions; a computer whose only
H.265 decoder is its graphics card's asynchronous one does not play it. On a Mac,
VideoToolbox takes the frames as the container keeps them and gives each picture as it is
decoded, so they are held until those shown before them have come. On Linux the player's
FFmpeg is loaded at run time, the newest `libavcodec` of versions 58 to 63 (FFmpeg 4 to 9):
only calls those all have are made, AVFrame's and AVPacket's fields are read at their places,
and each picture's time is checked against those given, so a library that has moved them
plays nothing rather than wrong pictures. Where no decoder is found the video is refused with
a line in `hyperion_video.log` naming what to install (`SysVideoNeeds`).

The sound is decoded whole at open, Vorbis by stb_vorbis (the packets put in Ogg pages for
it), Opus by libopus (from its own header, past its pre-skip, at 48 kHz) and AAC by the
system's decoder (Media Foundation's from the AudioSpecificConfig, AudioToolbox's from an ES
descriptor made of it, FFmpeg's with an ADTS header before each packet), past the encoder's
lead-in that the MP4's edit list or Matroska's CodecDelay gives; more than two channels are
mixed to two. It is handed to the Smacker sound track a frame's share at a time.

A frame's planes become RGBA by the matrix the stream names (BT.601, BT.709, BT.2020 or
RGB), in limited or full range, at 8 to 16 bits, the chroma in two planes or in one (NV12,
P010); an unnamed matrix is BT.709 above 576 rows for AV1, H.264 and H.265 and BT.601
otherwise. Frames of 1920 by 1080 and more are converted in bands on up to 8 threads. Against
ffmpeg's decode, AV1, H.264 and H.265 frames come to 80 dB (rounding apart; 57 dB for 10-bit
H.265, whose top 8 bits are kept), and an RGB AV1 stream is identical. On a 16-core Ryzen 9
7950X3D, 1080p AV1 decodes at about 210 frames a second and 4K in 10 bits at about 48, and
1080p H.264 through Media Foundation at about 220.

Otherwise a video's frame is RGBA and the game draws in 256 colours, in one of two ways:

- a video drawn in the screen's colours (`SVidPlayBegin`'s flag 0x100000: the portraits and
  the menus' animations) is put in entries 14 to 247 of the palette the screen shows or is
  fading to (`_vid_want`), through a 32K table of the nearest entry for each colour of 5 bits a
  channel, made again when that palette changes: the menus open their animations while the
  screen is still black. Where the alpha is under half the pixel is entry 0, as in 1.17's files,
  and over it the colour is put on black;
- a video with a palette of its own (the cinematics) gets one by median cut over the frame's
  colours, in entries 1 to 254 with 0 black and 255 white, at each key frame and wherever the
  frame's error has grown past twice what the palette had on the frame it was made from (a
  cinematic's groups of pictures run for seconds, through fades and cuts). Against the
  decoded colours, `terran1`'s frames come to 34 dB or better.

Checked against ffmpeg: the VP9 portraits' and cinematics' frames are bit-exact, the Vorbis
sound is within 1 of ffmpeg's, the VP8 menus within 3. `HYPERION_WEBM_FIRST=1` tries the
video in full colour before the `.smk`, to see Remastered's videos with the MPQs present, and
`HYPERION_CINEMATIC=<n>` plays the cinematic numbered n at start-up. The
browser build has none of these videos: libvpx's way out of a bad frame, `setjmp` and `longjmp`, breaks its link's
rewriting for asynchronous calls, and its page holds 1.17's files alone.

### Sound

`port_dsound.cpp` is DirectSound, as much of it as the engine uses, on SDL audio. Three
callers create buffers on it: the game's effects (`lang/snd.cpp`, which calls
`PortDirectSoundCreateAny` where 1.17 called `DirectSoundCreate`), Storm's music and long
sounds (`SFileDda*` in `port_stormfile.cpp`), and the cinematics' tracks
(`SmkSoundUseDirectSound`). The mixer is one SDL callback, float stereo at 44.1 kHz: for each
playing buffer it reads from the play position at the buffer's own rate, interpolating between
samples, scales by volume and pan as DirectSound does, and adds into the output. A buffer
made without `DSBCAPS_GLOBALFOCUS` goes silent while the window is in the background and
keeps its place, as under DirectSound, unless Settings' Sound in the background is on
(`PortSoundBackground`, `backgroundSound`). Settings' Master volume scales the whole mix by its
level squared after a listener's tap has taken it, so a streamed seat's sound does not follow
it (`PortSoundMaster`, `masterVolume`). The interfaces are COM's, each method in the slot
DirectSound's own vtable gives it, so a pointer made here also serves `lang/snd.h`'s
declarations. `port_dsound.h` stands in for `<dsound.h>`: a TU includes one or the other.

Beside the buffers the mixer takes one source of Hyperion's own (`PortSoundSource`, 2026-10-24):
a function the mixer's thread calls each time it mixes, which adds its frames, float stereo at
the mix's rate, before a listener's tap and Master volume. Its `bHeard` is false while the
buffers made without `DSBCAPS_GLOBALFOCUS` are silent. It is there for an add-on's sound
(`addons.md`); nothing fills it at present.

Settings' Unit voices (`unitVoices`, Classic unless set) takes the game's sounds from the
Remastered install: `open_wav_file` (`lang/snd.cpp`) asks `PortSoundRemastered` first, which
reads a path under `sound\` from CASC when the install has it, as Remastered recorded it again
at 44.1 kHz, and copies it into Storm's memory for the caller to free; a sound it lacks loads
from the MPQs as before. A sound's length only times the console's talking portrait, so the
longer files change nothing in a game. It takes the next game, since loaded sounds stay.

A mod's sound (`../../../docs/mod-data.md`, "Sounds and portraits") is a file of its data folder,
named to the game as `hxmod\<id>\<path>` and found on disk by `ModDataFilePath` before any
archive. `open_wav_file` loads every sound through `PortSoundLoad` (`port_dsound.cpp`), which
hands a file that is not a plain WAV of 8 or 16 bits and one or two channels to
`port_audiodecode.cpp`'s decoders (Ogg Vorbis, Opus, MP3, FLAC, and WAV of other depths and
channels) and makes the result a WAV of 16-bit PCM in Storm's memory, more than two channels
mixed into two. Opus is libopus with a small Ogg page reader, and is not in the browser
build.

The `SFileDda*` streams are `storm.dll`'s: a WAV file of an archive into a looping
DirectSound buffer, 16 KB at a time ahead of the play cursor, silence after a sound that
ends, positions reported in bytes of its data. `storm.dll` read the pieces through its
request queue; here a thread of its own (`DdaThread`, `FileDdaService`) does, started with
the first stream and never stopped. Beginning a stream on an archive's file moves that
archive to read kind 1, so its reads by request answer the way `storm.dll`'s did.

### Chat

Settings' Mature language filter (`chatFilter`, on unless set off) stars out the words of
Remastered's list in what players say (in 1.17's lobby, the others). The list is `rez\badnames.json` in the Remastered
install, 46,000 entries of regular expressions by locale, most of them names Battle.net
reserves; the English profanity ones, 339 once repeats are dropped, are compiled with
`std::regex`, case aside, on a thread `PortPresentInit` starts (not for a test's window, and
only with an install), which takes a few hundred milliseconds; until then chat shows as sent.
`PortChatClean` is asked by `wrap_map_msg` (`lang/mapmsg.cpp`) for a player's line in a game
or a replay, after `net_record_cmd` has recorded it as sent, by `on_recv_chat_from`
(`lang/gluChat.cpp`) for 1.17's lobby, and by the room's lobby (`port_front_lan.cpp`), which
draws its lines every frame, so cleaned lines are kept.

### The .hxm map format

A `.hxm` is a zip (or an unpacked folder) holding `map.json`, the per-tile layers and the
map's other files (`../../../docs/hxm-format.md`). The game reads maps through Storm's archive
calls, so a `.hxm` goes the same way: `SFileOpenArchive` asks `PortHxmIsMap`, and
`PortHxmCompiled` compiles it into a `scenario.chk` and an MPQ holding that and its sounds,
cached in the data folder's `cache\hxm\` under the map's name plus an FNV hash of its full
path, and made again when the `.hxm` is newer. The archive keeps the compiled path as its
name, because a raw read (scope 4) reopens it. Everything after that, the map list, both load
passes, the briefing and the replay's copy of the map, is the game's own path.

The compile is `tools/hxmap/convert.py`'s `to_chk` written again in C++ and has to produce
the same bytes; `Hyperion.exe --hxm-compile <in.hxm> <out.chk>` writes what it makes and
`tools/hxmap/check.py` compares. `--hxm-check <in.hxm> [report]` runs the map checker
(`MapCheck`), which reports errors and warnings without playing. The zip is inflated with
StormLib's bundled zlib; the output MPQ is written by `StormLibWriteArchiveFiles` as a
version 1 archive with each file imploded, which Storm, StormLib and the editors all read.

A map whose players from list entry 8 on take part is a Hyperion map, and carries its extra
slots in HXPL, their colours in HXRG and their triggers in HXTE (`../maps.md`).

`port_map.cpp` is the other end of the map story: maps saved by Remastered and ScmDraft 2
may carry a STRx string table (u32 count and offsets) in place of STR (u16), so the strings
can pass 64 KB. `PortMapStringsWide` remembers which kind the loaded map has, and
`PortMapStringWide` reads it with `str_get_string`'s rules. `PortMapColorsSet` and
`PortMapColorsApply` take CRGB's per-player colours, picking the nearest palette ramp for
the 8-bit drawing.

### Mods and mod data

An installed mod is a folder under `mods\` in the data folder with a `mod.json`
(`port_mods.cpp`). StarCraft and Brood War are built in. One mod is active at a time: the one
`--mod <id>` names, else the `"hyperion"` / `"mod"` setting, which outranks `--mod` once the
front end's Mods screen picks one. A folder whose `mod.json` is missing or does not read is
skipped and `hyperion_mods.log` says why.

A mod with a `"data"` folder brings new rows (`port_moddata.cpp`, `../../../docs/mod-data.md`).
`ModDataStartUp` runs at the end of `LoadGraphics` and reads `types.json`, `iscript.txt` and
the GRPs those name. Each record names the row it starts from (`"base"`: one of the original's
rows by number or by name, or another of the mod's records) and gives only the columns
it changes; records get the next free id of their table in the order of their names, so a
version of a mod always gets the same ids. A column that names another table's row takes a
name or a number, and a number must be one of 1.17's rows, the table's none, or one of the
mod's (`RowExists`). The original's rows have names in every table (`port_typenames.cpp`,
`TypeNameFind`, from the generated `port_typenames117.h` and `port_hxm_names.h`).

A record with `"edit"` changes a row already there instead (`Edit`), and is built before the
new records, so one based on that row starts from the change. An edit of a record of a mod
it depends on changes that record, which is stored again; an edit of one of the original's
rows goes in the table's `edits`, which `ModDataForGame` writes for a game that has the mod
(`EditsStore`, before the map's settings) and puts back from the file for any other
(`EditsPutBack`; only images.dat needs it, the other tables being read again for each game).
An edit's `requires` and `use_requires` become trees of their own (`ModTreeEdit`), read by
`TTAllowed`, `TTBaseUnitType` and the three checks of `Allowed.cpp` in place of 1.17's tree
alone; its unit `name` reaches `get_unit_name_from_type` under the map's own.

A mod's `"depends"` (`port_mods.cpp`, `PortModChain`) lists the mods to load first; each loads
as a submission of its own under one fingerprint, and their ids and versions go in the rule
record (`ModDataDepends`, `replays.md`). Rows are built in 1.17's layout and stored through
`PortDataStore` and `PortDataRow`, which put a column's rows past 1.17's into the engine's
own storage. `techdata` and `upgrades` are filled per game by `ModDataForGame`; a mod's
techs and upgrades, from 45 and 62, keep each slot's state in `PlayerModTech` and
`PlayerModUpgrade` (`port_players.cpp`), which the accessors of `saveload.cpp` and
`statbtn.cpp` reach for those ids, saved in the save's HXMT section. A mod's image with a `grp` gets a block of its own through
`PortGroupNative`, or, when the `grp` is a picture sheet, from `ModSheetGroupNative`
(`port_modsheet.cpp`): its smallest level decoded part by part, scaled to game pixels and
made classic frames, each sheet file in the fingerprint. The same sheet is given to
`PortModArtSet` (`render/port_modart.cpp`), which the GPU draws it from at its own
resolution (`render.md`, "A mod's sheets"). One whose file is missing keeps its
base's frames and script, and the log says so. A sheet of 32 directions sets
`g_hxModImage32`, by which `CImage.cpp` shows its own frame for each direction instead of
mirroring (`ImageFold`, `ImageFrame`), and turns a direction copied between a 17 and a 32
(`ImageTakeDirection`). By Hyperion's rules a sprite's size past 255 is kept for fog, sites
and clicks (`SpriteGrpWidth`, revision 22). A mod that does not load is put back whole (`Keep`, `PutBack`) and leaves no
fingerprint, so the run, and a LAN room, see a Hyperion without it. `port_iscript.cpp` compiles `iscript.txt` into a block appended after 1.17's scripts
by `ImageScriptsAppend` (`lang/CImage.cpp`), with ids from 1000. The
block goes after `iscript.bin`'s last byte, its id table too, which keeps the file's bytes for
an EUD map; an image's offsets into the scripts are `iscript_off_t`, 32 bits on 64
(`h/typelimits.h`), and a mod's code uses wide forms of the jumps, `call` and `useweapon`
(`RunScript`'s 0x45 to 0x4D: a DWORD target, a WORD weapon) and headers of DWORD animations
(`ImageScriptAnim`), so its block has no size limit. A mod's ids are found in
`g_scriptHeaders` (`ModScriptHeader`, from `CImage::ForceScript`), 1.17's in its table.
Path prediction's cursor (`CUnitPathUtil.cpp`, `sgPathScript_g`) is a variable of its own on
64 bits, wider than 1.17's 8 bytes.

Two more tables, `sounds` (sfxdata.dat, from 1144) and `portraits`
(portdata.dat, from 110), which units' sound and portrait columns may name, and three unit
fields of the mod's own: `icon`, `wireframe` and `transport_wireframe`. `UnitArtOf` keeps them
per unit (`g_unitArt`); as each game with the mod begins, `ArtBuild` reads 1.17's `cmdicons`,
`wirefram`, `tranwire` and `grpwire` GRPs, makes each named PNG or GRP a frame of the console's
values (an icon's ticon shades, a wireframe's outline and four damage parts), places it on the
unit's own frame below 1.17's count or appends it, and builds the four files again in memory.
`ModConsoleGroup` hands each one to the console as `statcmd_init`, `statwire_init` and
`statdata_init` load theirs, through `PortGroupNative`, and `ModUnitArtFrame` gives the frame
for a unit where `UnitTypeLooks` gave it (`statdraw.cpp`, `statwire.cpp`, `statdata.cpp`,
`port_console.cpp`). A unit that gives another unit's name borrows that unit's frame. A game
without the mod builds nothing and the console has the classic files. An icon's and a wireframe's
picture at the HD scale is made beside its frame and given to `PortModUiSet`, which the GPU
draws them from over either console (`render.md`, the console's contents in HD;
`ModArtOwnFrame` tells a mod's own frames). A mod's portrait videos are read from its folder by
the same path names, the HD window's through `PortReadFile` when `ModDataFilePath` has the name
with one of the containers' extensions, and fitted to the box with their shape kept.
None of these files are in the fingerprint.

`ModDataPush` is the live-channel `mod_push`: in a Debug Tools canvas game a mod's data can be
sent again as a new submission, whose records take ids after every earlier one's; the game's
slots keep their races by id. A push never reaches another game, a replay or a save: its data
is put away as its game ends (`ModDataAfterGame`, from `main.cpp`), and every game starts from
the data loaded at start-up. A game has the mod's data when one loaded and the game is no
campaign's mission (`RulesForGame`, `port_rules.cpp`: the mission's word at 0x526524 is 0); a
mission plays as the campaign was made. `ModDataName` gives the mod's name for the skirmish
set-up. Names and strings handed to the game are held for the run
(`Lasting`), since 1.17 keeps its string pointers; a
replay or save records the mod's id and a SHA-1 fingerprint of its data and is refused by a
run without it.

`--mod-check <id>` (`port_modcheck.cpp`) runs from `RunGame` once `init_program` has opened the
game's files: it calls `LoadGraphics`, so the mod loads as at start-up, and asks
`ModDataCheckResult` for the load's error, the lines its `Note` calls kept (each a `Log` too),
the tables' counts and the files the records name, each data folder's own (`g_subFolders`). It
decodes those as the game would (`PortSoundLoad`, `PortVideoProbe` and `PortVideoOpen` with the
first frame, `PortImageDecode`, a GRP's frame table), looks through each mod's `smk/` against
`sgszFlics`, writes the report and ends with `_Exit`, so no settings are written. While it runs
`PortLogFile` puts the logs in `mod-check\` and `PortCheckForOtherInstances` lets it start beside
a running Hyperion; `PortModsSkipped` gives the folders `port_mods.cpp` left out.

## Settings and environment variables

| Name | Kind | What it does |
|---|---|---|
| `HYPERION_GAME` | environment | the game's folder, instead of the `"classic"` setting's or `Hyperion.exe`'s; it also keeps the first start from asking; a file named there stands for its folder; `Hyperion.exe` sets it to the folder |
| `HYPERION_REMASTERED` | environment | the Remastered install CascLib opens, before the setting and before Battle.net's uninstall entry (on a Mac, `/Applications/StarCraft`) |
| `HYPERION_TEST_TEXT_LOCALE` | environment | a test: the install is taken as one in that text and speech language (`plPL`, `ruRU`), and the files such an install would lack are refused (The install's language) |
| `HYPERION_TEST_LOCALE_FILES` | environment | a test: a folder that serves the `locales\` files of the test's language, as `tools/assets/fetch_locale.py` writes it |
| `HYPERION_LANGUAGE` | environment | the language, before the setting: `install` or a locale (`deDE`, `ruRU`) |
| `"hyperion"` / `"language"` | `settings.json` | Settings' Language: `install` (the default), the install's text language, or a locale |
| `HYPERION_DATA_SOURCE` | environment | where the game's files come from: `mpq`, `remastered`, `both` (Remastered's over the MPQs) or `auto` (the default); before the setting |
| `"hyperion"` / `"data"` | `settings.json` | the same choice, when the environment gives none; Settings' Game files and the first start write `remastered` or `mpq` |
| `--install classic\|remastered` | command line | this run's kind of install, before the environment and `settings.json`, and not kept; `mpq` and `battlenet` too |
| `--install-folder <folder>` | command line | that install's folder for this run; alone, its files give the kind |
| `HYPERION_INSTALL_WINDOW` | environment | `drawn`: the first start's window Linux and a Mac have, on Windows |
| `"hyperion"` / `"classic"` | `settings.json` | the classic StarCraft folder Settings or the first start was given: the game's folder when `HYPERION_GAME` names none and StarCraft from Battle.net is not the kind chosen |
| `HYPERION_FILE_LOG` | environment | `1`: each file the game asks Storm for, once, with where it was found, in the data folder's `hyperion_files.log`; and each name asked of a Remastered install, read, missing or refused, in `hyperion_casc.log` |
| `HYPERION_MOD_SHEETS` | environment | a folder: each mod image made from a sheet written there as `<image>.grp`, when its frames fit a classic GRP |
| `HYPERION_REMASTERED_DUMP` | environment | a folder: each file `PortRemasteredLoad` serves written there, its separators as `#`, to compare with 1.17's |
| `HYPERION_WEBM_FIRST` | environment | `1`: a video in full colour before its `.smk` (Videos in full colour, under Video) |
| `"hyperion"` / `"remastered"` | `settings.json` | the StarCraft from Battle.net install Settings was given with Browse, or the first start (`PortCascUse`) |
| `"hyperion"` / `"chatFilter"` | `settings.json` | 0: other players' chat as sent (`port_chatfilter.cpp`) |
| `"hyperion"` / `"cinematicSubtitles"` | `settings.json` | 0: no subtitles over the cinematics (`port_subtitles.cpp`) |
| `"hyperion"` / `"unitVoices"` | `settings.json` | 1: the game's sounds from the Remastered install where it has them (`PortSoundRemastered`) |
| `"hyperion"` / `"mod"` | `settings.json` | the active mod's id |
| `"Video Player"` / `"Mode"` | `settings.json` | the scale and sub-mode `SVidPlayBegin` takes for flags 0x8 and 0x800 |
| `--data <folder>` | command line | the data folder, in place of `Data\` beside the exe |
| `--maps <folder>` | command line | another read-only `maps` folder to list from; up to eight |
| `--mod <id>` | command line | the mod to load for this run |
| `--hxm-compile <in> <out>` | command line | write a `.hxm`'s compiled CHK, for comparing with `tools/hxmap` |
| `--hxm-check <in> [report]` | command line | run the map checker over a `.hxm` |

Logs: `hyperion_hxm.log` (what was compiled and why a map was refused), `hyperion_mods.log`
(mods read and mod data loaded), `hyperion_init.log`, `hyperion_canvas.log`.

## Integration points

- `src117/h/Storm.h` declares the Storm functions the game calls; the definitions in
  `src117/port/render/port_storm*.cpp` are plain `extern "C"` symbols with the same names, so the
  link binds them with no table in between. The port's own files declare them from
  `port_stormapi.h`, spelled as the definitions are and as `Storm.h` is where both have a
  name; `SFileGetInstallPath` answers the length written, typed `BOOL` as `Storm.h` has it.
- `lang/init.cpp` (`open_archives`, `FindCDArchive`), `lang/maphdr.cpp` and `lang/League.cpp`
  are the main `SFileOpenArchive` callers. `LoadGroups` is what waits on Storm's request
  events.
- `lang/compress.cpp` calls `implode`, `explode` and `crc32_pklib`, which `port_pklib.cpp`
  forwards to pklib's `PkImplode`, `PkExplode` and `PkCrc32`.
- `lang/snd.cpp` reaches `PortDirectSoundCreateAny` where 1.17 reached `DirectSoundCreate`.
- `port_present.cpp` provides the `PortVid*` screen that `port_stormvid.cpp` draws on.
- `port_hd.cpp` reads Remastered's HD sprites through `PortCascRead`, and Settings
  (`port_front_settings.cpp`) picks and checks the install (`PortCascUse`, `PortCascFolder`).
- `port_canvas.cpp` writes the canvas map with `StormLibWriteArchive`.
- `tools/porttest/storm_test.cpp`, `err_test.cpp`, `big_test.cpp`, `smk_test.cpp`,
  `pklib_test.cpp` and `mask_test.cpp` run the player's `storm.dll`, `smackw32.dll` and
  PKWARE library beside the port's code on the same inputs; 32-bit, as the original is
  (`tools/build_porttest.bat`). What their port files call on the side (the scene, snapshots,
  `.hxm`, sound, the LAN provider) comes from `tools/porttest/*_stub.cpp`. storm.dll counts a
  whole 0x20000-byte piece on a short read that ends inside one, where the port counts what it
  read; 1.17 never reads that count, so `storm_test` leaves it out.

## Gotchas

- `port_storm.cpp` is not file work at all, despite the name: it is the drawing Storm did
  (`SBltROP3`, the dirty-rectangle region, the `SCodeExecute` table blits). It belongs with
  the renderer; it is listed here only because it is part of Storm's replacement.
- `port_stormlib.cpp` is the only file allowed to include StormLib's header. Everything else
  goes through its `StormLib*` wrappers (`port_stormlib.h`), and StormLib's own public names
  carry an `Sl` prefix (`port_stormlib_names.h`) so they never collide with Storm's.
- `port_pklib.cpp` and pklib are compiled `/Gd`, not the engine's `/Gr`, because pklib's
  header spells its callback types with no calling convention and the game's are `__cdecl`.
- Storm keeps one last error for the process apart from Win32's. `SetLastError` does not
  reach it; use `SErrSetLastError`. Several of the port's functions deliberately return TRUE
  unconditionally (`SFileCloseArchive`, `SFileCloseFile`, `SFileSetIoErrorMode`,
  `SVidDestroy`) because `storm.dll`'s did.
- A Dda stream's `dwEnd` is the data chunk's size, compared against positions in the file, so
  a stream ends that many bytes before the data does. `SFileDdaGetPos` gives the game the
  difference, and changing either without the other breaks the music's position.
- `PortGroupNative` and `PortDialogNative` are no-ops on x86; only the tools that compile the
  engine at 1.17's widths build that arm.
- The `.hxm` cache key is the map's leaf name plus a hash of its full path, and staleness is
  decided by file time alone. Editing a `.hxm` without its mtime changing leaves the cached
  `.scx` in place.
- `SFileGetUserDataPath` keeps `storm.dll`'s odd arithmetic around the trailing backslash on
  purpose; `storm_test` covers it.

## Open items

- CascLib is not mentioned in `engine/README.md`'s build instructions or tool table, although
  `tools/fetch_casclib.py` exists and `build_engine_x64.py` needs it (`--casclib` rebuilds
  it, and the first build makes it).
- With a Remastered install alone, the campaign's route goes through: the episode's text,
  the briefing (its pictures as 1.17's; its videos, re-encoded by Remastered, a few levels
  of colour off), the mission and the score screen after it (checked against 1.17's MPQs
  after Boot Camp won: the same pixels but the elapsed time's and the cursor's).
- The HD portraits are drawn in the HD console (`render.md`, HD art); the briefing's
  portraits stay SD until the briefing is Hyperion's own.
- A mod's own art stays on the player's machine: an `art/` folder in a mod is left out of git
  and out of `tools/package.py`'s package.
- `--data` and `--maps` are command-line options waiting for settings to take their place.
- The first start's window is Windows' own controls there and drawn by Hyperion elsewhere; a
  screen in Hyperion's look, from a design session's mockup, could take the place of both.
- On a Mac, StarCraft from Battle.net has no `Local.dll` (the storage tags it `enUS` and
  `Windows` only); Hyperion's stand-in should start it there, not yet tried.
- An install in another language has its campaign maps in that language. Their text has not
  been seen (this machine's install is English's, whose storage lists the other languages'
  files without having them); letters the classic 8-bit fonts lack, Polish's or Cyrillic, would
  need converting to ones they have.

## Further reading

- [`../hyperion-exe.md`](../hyperion-exe.md): the exe check, the data folder, `--maps`,
  `--mod`, `--hxm-compile`, `--hxm-check`.
- [`../maps.md`](../maps.md): the map sizes and player counts Hyperion plays.
- [`../../../docs/hxm-format.md`](../../../docs/hxm-format.md): the `.hxm` document, layer by
  layer and field by field.
- [`../../../docs/mod-data.md`](../../../docs/mod-data.md): every `types.json` field, cards,
  races and pushes.
- [`../replay-test.md`](../replay-test.md): the gate any change here must pass.
- [`render.md`](render.md): the screen `port_stormvid.cpp` draws on, and the HD art
  out of CASC.
- `../../third_party/StormLib-9.40/` and `../../third_party/CascLib-3.0/`: both MIT, both
  gitignored, fetched by `tools/fetch_stormlib.py` and `tools/fetch_casclib.py`.
