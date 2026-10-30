# How a frame reaches the screen

## Overview

1.17's own code still draws the picture: the game's C++ writes 8-bit palette indices into
`_img_draw_buf`, the layer walk copies that buffer onto a screen buffer through the
console's mask, and dialogs blit straight onto the screen. The port owns everything from
the screen buffer onward. `port_present.cpp` holds the screen (`g_screen`, one byte a
pixel, the view's width to a row) in place of Storm's SDraw surface and hands it, with the
palette, to one of two presenters: SDL's renderer, whose INDEX8 texture does the palette
lookup, or Hyperion's own on SDL_GPU, which is the default. Beside that, and only with the
GPU presenter, the map layer is described as it is drawn (`port_scene.cpp`) and the GPU
draws the same map again from the description, in full colour and, with a Remastered
install, from HD art, with 2D lights and the bloom and grade over it. The parts of the
drawing that were hand-written assembly in 1.17 and fixed the view at 640 are C++ here
(`port_draw.cpp`, `port_mask.cpp`), so the view's size can change (`port_view.cpp`) and the
camera can zoom out over a larger render view (`port_camera.cpp`). All of it is
presentation only: the simulation never reads any of it, and the replay suite is the gate.

## Status

On master since v0.3.6-beta (2026-10-18). The GPU presenter draws through Vulkan by
default since 2026-10-18, with Direct3D 12 the fallback where Vulkan does not start, and
SDL's renderer the fallback where no GPU driver starts. With the GPU presenter the GPU
alone draws the map (the scene's last stage), full colour is on by default, and HD art,
HD terrain, the HD console and the HD console contents are in and switched by Settings'
"Art" or F5. The whole replay suite is identical to the original frame for frame, and the
suite has been run through Vulkan with the GPU check with no frame differing.
Remastered's HD is held (the PRD's decisions of 2026-10-21, `data.md`, "What Hyperion does not read"): until the files are given otherwise, neither Settings, F5 nor a stored `art` turns it on (`GpuHdAllowed`, `port_gpu_hd.cpp`), and the game is drawn in the classic art.

## Key files

Each file has its header beside it. `port_gpu.h` and `port_gpu_internal.h` carry SDL's types
and are for the presenter's own files; the others stay plain, because the game's C++ includes
them (`PortGpuZoomCan`, which the view asks, is declared in `port_present.h` for that reason).
`PortGpuInit` makes the device and the present's pipelines, then each part makes its own
(`GpuSceneInit`, `GpuFxInit`, `GpuHdInit`), and `PortGpuShutdown` has each release them.
Each part's state, and its own types and uniform blocks, are file-local at the top of its
file; `port_gpu_internal.h` declares only what another part reads, and says which part
defines it.

| File | What it holds | Key functions |
|---|---|---|
| `src117/port/render/port_present.cpp` | the 8-bit screen, the window's display kind, the screen's cover, the frame rate, the mouse mapping, an input method's place, screenshots | `PortPresentInit`, `PortPresentTextArea`, `PortPresentFrame`, `PortScreenLock`, `PortPresentPalette`, `PortScreenCoverCopied`, `PortFrameDue`, `PortDisplayShow`, `PortScreenshot`, `oc_screen` |
| `port_gpu.cpp`, `port_gpu.h` | the SDL_GPU device, the present itself, frames ahead, vsync, the check's compare | `PortGpuInit`, `PortGpuFrame`, `PortGpuFrameEnd`, `GpuFrameCmd`, `GpuSubmit`, `PortGpuAheadSet`, `PortGpuVsyncSet`, `PortGpuLetterbox`, `GpuMapMoved`, `GpuWriteLog` |
| `port_gpu_internal.h` | what the parts share: the state another part reads, the types and uniform blocks two parts use, the storage-buffer slots, the functions one part calls in another | `PostParams`, `GpuStore`, `SCENE_STORE_*`, `GpuMapMove`, `GpuZoomShown` |
| `port_gpu_scene.cpp` | the description's uploads and dispatches, the full colour, the scene's check | `GpuSceneInit`, `GpuSceneRelease`, `PortGpuScene`, `GpuSceneCompare`, `GpuFullPrepare`, `PortGpuReadShown` |
| `port_gpu_hd.cpp` | the frame drawn again from Remastered's art, its terrain, console and console contents | `GpuHdInit`, `GpuHdScene`, `GpuHdDrawFrame`, `GpuHdFit`, `GpuHdFitMatrix`, `GpuHdFitEffect`, `PortGpuHdToggle` |
| `port_gpu_fx.cpp` | the bloom, the grade and vignette, the 2D light pass, the live values | `GpuFxInit`, `GpuFxFitSize`, `GpuBloom`, `GpuLights`, `GpuPostParams`, `GpuFxValues` |
| `port_gpu_ui.cpp` | the front end's own frame, and the overlay drawn into the swapchain after a game's frame | `PortGpuUiBegin`, `PortGpuUiEnd`, `GpuUiOverlay` |
| `port_gpu_video.cpp` | a cinematic in full colour: the decoder's planes at their own depth, put in colour and fitted to the window | `PortGpuVideoSet`, `GpuVideoUp`, `GpuVideoDraw` |
| `port_scene.cpp`, `port_scene.h` | the frame description, recorded as the game draws the map layer | `PortSceneMapBegin`, `PortSceneRle`, `PortSceneFog`, `PortSceneGlyph`, `PortSceneScreenDone`, `PortSceneTake`, `PortSceneStage` |
| `port_draw.cpp` | 1.17's assembly drawing routines as C++ at the view's size; each reports its call to the description | `PortCellCopyScrollbuf`, `PortDrawCell`, `PortRleCreep`, `PortRleDrawNormal` and the rest of the `PortRle*` family, `PortDrawChar`, `PortShadeMtx`, `PortImgClipped` |
| `port_maptext.cpp`, `port_maptext.h`, `../../src117/gen/cp949.inc` | a map's text as StarCraft: Remastered reads it: UTF-8 first, a Korean map's CP949 as the fallback, and the characters 1.17's fonts lack made from a TrueType face into 1.17's glyph form; `gds_font.cpp` measures and draws through it | `MapTextStrings`, `MapTextChar`, `MapTextCharStart`, `MapTextByte1252`, `MapTextGlyph`, `MapTextSqueeze`, `MapTextUtf8`, `MapTextFace`, `MapTextGlyphBox`, `MapTextGlyphDraw` |
| `port_mask.cpp` | the screen's masks, in place of Storm's STrans functions, at any size | `PortMaskCreate`, `PortMaskIntersectDirty`, `PortMaskBlt`, `PortMaskBits`, `PortMaskSetDirtyGrid` |
| `port_hd.cpp`, `port_hd.h` | Remastered's art read from the player's install on a loader thread, kept as BC blocks | `PortHdImage`, `PortHdTileset`, `PortHdConsoleArt`, `PortHdUiSet`, `PortHdScale`, `PortHdCounts` |
| `port_modart.cpp`, `port_modart.h` | a mod's art sheets made HD art at the HD scale on a thread of their own: cells cut to their pixels and packed, the team colour's part taken out; and its own icons and wireframes packed for the console | `PortModArtSet`, `PortModArtGame`, `PortModArtWanted`, `PortModArtImage`, `PortModArtGeneration`, `PortModUiSet`, `PortModUiArt`, `PortModUiHas` |
| `port_lights.cpp`, `port_lights.h` | the 2D light table and the lights of the last map pass | `PortLightsBegin`, `PortLightImage`, `PortLightsEnd`, `PortLightsFrame`, `PortLightSet` |
| `port_looks.cpp`, `port_looks.h` | the looks: which units have one, each look's remap of the palette made per tileset, its GPU data, its light, halo and plume recorded into the description, `looks.json` and the channel's looks commands | `PortLooksPassBegin`, `PortLookOfSprite`, `PortLookImageBegin`, `PortLookHalo`, `PortLookSpriteDrawn`, `PortLooksGpu`, `PortLookUnitSet`, `PortLooksProvider`, `PortLooksGameBegin` |
| `port_teamcolor.cpp` | each player slot's colour in RGB, its ramp and its eight full-colour shades | `PortTeamColorSet`, `PortTeamColorsApply`, `PortTeamShades`, `PortTeamRamp` |
| `port_view.cpp` | the view's size and everything derived from it; the render view the camera draws over | `PortViewSet`, `PortViewApply`, `PortViewRenderBegin`/`End`, `PortViewCamera`, `PortViewCameraPlace`, `PortViewScreenToMap` |
| `port_camera.cpp` | the zoom itself: where it eases to, and when it is held at 1 | `PortCameraZoom`, `PortCameraZoomTo`, `PortCameraWheel`, `PortCameraStep`, `PortCameraForced` |
| `port_console.cpp` | the console laid out as Remastered's HD one, and what its controls show in HD: the command card, the wireframe and the portrait | `PortConsoleUpdate`, `PortConsoleArtLoaded`, `PortConsoleHdItems`, `PortWireframeCell`, `PortHdPortraitFrame` |
| `port_minimap.cpp` | the minimap's looks: a dot's colour, 1.17's where none gives one; the active mod's `hud.json` gives one | `PortMinimapAdd`, `PortMinimapDotColour`, `PortMinimapHud` |
| `port_selection.cpp` | the selection's looks: whether the circles, a teammate's circles and the health bars are drawn, 1.17's where none says; the active mod's `hud.json` gives one | `PortSelectionAdd`, `PortSelectionImageShown`, `PortSelectionHud` |
| `port_overlay.cpp` | the game timer, actions per minute and frame rate drawn over a game, and the list of layers over the game that the other modules add | `PortOverlayBuild`, `PortOverlayAction`, `PortOverlayFrameShown`, `PortOverlayAdd`, `PortOverlayGameArea`, `PortOverlayHold`, `PortOverlayMouse`, `PortOverlayKey`, `PortOverlayOutside`, `PortOverlayHas` |
| `port_window.cpp` | the window's icon and its square corners | `PortWindowCreated` |
| `port_hwcursor.cpp`, `port_hwcursor.h` | Settings' Hardware cursor: the game's cursor frame as a system cursor, scaled as the game is, in the screen's colours, kept until the scale or a colour it uses changes; `cur_display_update` (`cur.cpp`) draws nothing while it shows | `PortHwCursorNow`, `PortHwCursorShowing`, `PortHwCursorSet` |
| `port_motion.cpp`, `port_motion.h` | the game picture's motion: the map's scale, the console's shift, black and cinema bars, eased over time, for the skirmish start, cutscenes and map loads; the presenter and the camera read it | `PortMotionTick`, `PortMotionCover`, `PortMotionRead` |
| `port_storm.cpp` | Storm's blit and region functions: `SBltROP3` and the dirty-rectangle regions the game redraws by | `PortBltRop3`, `PortRgnCreate` |
| `src117/port/render/shaders/` | `present.hlsl` (`PSResolve`, `PSCompose`, `PSComposeFull`, `PSScale`, `PSGround`), `scene.hlsl` (`CSScene`, `CSDistort`, `CSShow`, their full-colour twins, `CSHud`), `hd.hlsl`, `effects.hlsl`, `ui.hlsl`, `video.hlsl` (`PSVideoColour`, `PSVideoScale`) | built to DXIL and SPIR-V by `tools/build_engine_x64.py` with `dxc`, included as byte arrays; the SPIR-V then to MSL by `tools/shader_msl.py` with SPIRV-Cross, which only a Mac's build includes (`GPU_CODE`, `GPU_MSL` in `port_gpu_internal.h`). SDL's Metal path numbers a storage buffer after every uniform buffer the pipeline declares, which the SPIR-V cannot show, so the MSL names those indices by counts (`HX_UNIFORMS` and the rest) that `GpuMslSource` defines ahead of the source from the counts given to SDL |

## How it works

### The two presenters

`PresentCreate` (`port_present.cpp`) picks one at start-up. Without `HYPERION_RENDERER` it
tries the GPU presenter on `vulkan`, then on whatever driver SDL_GPU picks (Direct3D 12 on
Windows), and falls back to `SDL_CreateRenderer` with vsync off. `HYPERION_RENDERER` names
one: `vulkan`, `direct3d12`, `gpu` for SDL_GPU's own choice, or `sdl`. Without it, Settings'
Renderer on Windows (`"renderer"`: `vulkan`, the default, or `direct3d12`) names the first
driver tried, with the same fallbacks after it; it takes a restart. SDL's renderer is not
offered there: Hyperion's menus, Settings among them, need the GPU presenter.
`hyperion_render.log` names the presenter, the device and the driver version in use. When
the GPU presenter starts it also starts the GPU scene at its last stage with the software's
drawing off (`PortSceneDefault`), unless `HYPERION_GPU_SCENE` says otherwise.

SDL's renderer path is the short one: `SDL_UpdateTexture` of a streaming INDEX8 texture
with the palette attached, `SDL_LOGICAL_PRESENTATION_LETTERBOX` for the scaling, clear,
draw, present. It has no scene, no HD art, no effects and no overlay. With the camera zoomed
out it shows the software's render view instead (`PresentSoftZoom`, below).

### One frame through the GPU presenter

1. The game draws as it always has. `gamemap_update` draws the map layer, the rest of
   `img_update_real`'s layers go over it, and the copy onto the screen goes through the
   console mask cut to the dirty grid (`PortMaskIntersectDirty`, `vid_draw_mask`). The
   sites that locked Storm's surface lock `g_screen` instead (`PortScreenLock`), and
   `flip_palette` hands its entries to `PortPresentPalette`.
2. `img_update` ends in `PortPresentFrame`. It polls the live channel and the LAN pumps,
   feeds a streamed seat its frame, and returns at once when the frame is held
   (`PortPresentHold`, for the colour cycle at a chosen frame rate) or nothing is dirty.
3. `PortSceneTake` gives the frame description recorded during the pass, if there is one,
   and `PortGpuScene` draws it (below). At the last stage the screen's cover is handed over
   too, and uploaded only when it changed since the GPU last took it.
4. `PortGpuFrame` runs the frame's passes, one function each. `GpuFrameUpload` uploads the
   screen into an R8 texture, the palette when it changed, and the cover when it is stale.
   `GpuFrameResolve` resolves the frame at the game's size into an RGBA8 texture:
   `PSResolve` from the screen alone, `PSCompose` from the GPU's copy of the screen where
   the cover is 1, or `PSComposeFull` from the full colour's copy.
5. With HD art in use `GpuFrameHd` draws the frame again at the art's scale
   (`GpuHdDrawFrame`), and mid-blend the whole SD frame too, laid over it. Where the map
   moves under the rest of the frame (a skirmish's start, or the camera zoomed out)
   `GpuFrameGround` resolves the map layer into a layer of its own with `PSGround`.
6. `GpuFrameEffects` runs the bloom and the light pass from the resolved frame (`GpuBloom`,
   `GpuLights`), and the downloads a snapshot or the check asks for.
7. `GpuFrameWindow` acquires the swapchain image without waiting; the pass clears it to
   black and draws the frame into its letterbox with `PSPost` (effects on) or `PSScale`
   (off), using the same pixel-art filter maths as SDL's renderer. Settings' Classic art
   filtering (`PortArtFilterSet`, `artFilter`) widens the filter's box from one window pixel
   toward a whole texel, a third a step (`gArtSoft`, `gPostArtSoft`), so Filtered blends every
   texel into the next; the HD frame keeps the pixel-art filter, and SDL's renderer has
   Sharp (`SDL_SCALEMODE_PIXELART`) and linear for the rest. A cinematic in full colour is
   drawn over the frame (`GpuVideoDraw`, below), and `GpuUiOverlay` draws the overlay over it.
8. The frame's commands go in one command buffer, acquired at the frame's first use
   (`GpuFrameCmd`) and submitted at its end: `GpuSubmit` waits on the fence from the submit
   `framesAhead` submits ago, so a slow card slows the frames rather than making SDL cycle
   a new copy of every texture. A `FrameEnd` guard in `PortPresentFrame` calls
   `PortGpuFrameEnd`, so work recorded by a frame that was never presented is still
   submitted.

When the swapchain has no image to give, `PortGpuFrame` returns false, the caller sets the
frame dirty again and the frame is shown with the next present. A hidden window claims no
swapchain at all, and only draws when the check or a snapshot asks for a frame.

### The frame description and the GPU scene

`port_scene.cpp` records a map pass as it is drawn. The terrain is the scroll ring's: for
each tile of the ring it keeps the megatile and creep edge last drawn there and the tile
they were drawn for. Over it, every call that draws into the screen buffer during the pass
is a primitive in the order made, with the frame it drew and the colour table as that table
stood at the call: the sprite routines of `port_draw.cpp`, the pen's lines, the space
platform's stars, the fog's light map, the text's glyphs, and the bitmaps copied in, up to
the copy onto the screen. Frames of art and glyphs are decoded once into an atlas of 16-bit
pixels (the index, with bit 8 set where opaque) and tables into a store, both starting
again at each map load. Primitives are binned into 32-pixel squares; a distortion is in no
square, since it reads the map around it.

While the scene is on, `gamemap_update` takes its full path every pass and marks the whole
view dirty, so the description is of the whole view and all of it reaches the screen.

`PortGpuScene` uploads whatever the GPU does not already have in one transfer buffer, then
dispatches `CSScene` over the view, four pixels a thread: the terrain from the megatiles
and cells, then each square's primitives in order, each as its routine draws it. A
distortion is drawn by `CSDistort` over a copy of the map as it is before it. They go in
batches (`GpuDistortMeets`): a distortion joins the batch before it while it reads
nothing the batch draws before it, neither an earlier distortion's box nor a primitive
between them by the squares' lists. A batch is one copy of the rows its distortions read, one
pass with a dispatch for each, and one dispatch of the primitives after them over their own
box (`PortSceneRunBox`, `gOrigin` in `scene.hlsl`). The frame is the one each on its own
gives. With the map layer at a step of 4 or more (below) only the words the distortions write
are kept apart: there a primitive between two of a batch is drawn over the later one where
1.17 draws it under, a pixel or two of the layer's, and the batches went from one distortion
each to a few a frame. `CSShow` then puts
the pixels the screen's copy took into the GPU's copy of the screen, which is what the
cover points at. `PortSceneTake` hands out a frame once: a second call before the next pass
gives nothing.

`PortGpuScene` runs its stages as functions of their own: `GpuSceneLayoutOf` (where the
description goes in the stores), `GpuSceneStoresFit`, `GpuScenePieces` (what this frame
uploads), `GpuSceneUploadFill` and `GpuSceneUploadCopy`, `GpuSceneRun` (the dispatches,
with `GpuSceneDistort` and the camera's `GpuSceneHud`), mid-blend `GpuSceneKeepSd` and a
second run, `GpuSceneDrawn` (what the present reads next) and the check's
`GpuSceneCheck`. `GpuHdScene`, which takes the HD draws out of the primitives, is likewise
`GpuHdSceneView`, `GpuHdTerrainUp`, `GpuHdConsoleUp`, `GpuHdFogOf`, `GpuHdDrawOf` for each
primitive and `GpuHdOverTablesOf`; its uploads share one copy pass, begun at the first
(`GpuHdCopy`).

Zoomed out, each pixel of the GPU's map layer is the game's at the middle of a square of 2,
4, 8 or 16 of them (`SceneFrame`'s `nStep`, `PortGpuSceneStep`): the most that
leaves two of the layer's pixels to a pixel of the window, which the present averages, and
more where the layer would be past 8,192 a side. `HYPERION_SCENE_STEP` fixes it. The
squares, the distortions, `CSHud`, `PSGround`'s layer and the HD base's lookups go by it. In
the 32-player game at zoom 16 the layer went from 67 to 1.2 megapixels. The render view can
then be up to 32,752 pixels a side where the GPU draws it (`RENDER_MAX_GPU`), so a map of
1,024 tiles is seen across; the software's render view keeps 8,192 (`RENDER_MAX`).

Every drawn frame takes the map's full path with the GPU scene, most of them between the
game's frames, so a pass keeps what nothing it is made from has changed. The fog's
grid (`mask_build_grid`) is made again when the game's frame, the view's tiles, the vision
masks or the state serial (`PortSceneStateSerial`: a map's load, a snapshot's restore) move.
`PortSceneRingFill` fills the render view's slots from `gfpCellMap` and the creep's edges,
writing only those that differ, in place of `cell_paint`, and is left out on the same terms
unless `cell_refresh` redrew a tile; `cell_refresh` itself is left out on them too
(`PortSceneCellsKept`). The GPU takes only the slots written since the ring's last frame.
`HYPERION_PASS_KEEP_CHECK=1` makes each of them again anyway and writes any difference to
`hyperion_pass_keep.log`, with a line every 4,096 checks.

Recording a pass costs the game's thread little in a large game: `SceneTable` finds a colour
table it has stored by where the table is, its size and the draw colour, checking its bytes,
before hashing it again, and `SceneSlotFor` looks the ring's column and row up in a table.
`PortSceneRingFill` reads the slots through the same tables, made again for the ring it fills
(`RingTables`): a row's place in them is its row times the ring's width, so a zoom that changes
the width alone (44 by 20 tiles to 43 by 20) must remake them. Until 2026-10-21 it read the old
ones then, found a tile where the wider ring kept it, wrote nothing, and left the new slot
showing another tile: the fog and ground that came and went in a scene's slow zoom.
Where the GPU alone draws the map (`PortSceneFogOnGpu`), `mask_update` leaves 1.17's `Fog`
to the card: `PortSceneFog` puts the smoothed mask grid and the ramp (`dword_670A80`) in the
frame's bytes, a few kilobytes, and `fog.hlsl`'s `CSFogLight` makes the light map from them
in the frame's store before `CSScene` reads it, by `Fog`'s own integer steps. Zoomed out the
light map is half a megabyte a pass; `HYPERION_FOG_CPU=1` makes it on the processor as before
.

### The scene's stages

`HYPERION_GPU_SCENE` names how far the GPU draws and where the check compares:
`terrain`, `sprites`, `map`, `screen`, or `gpu`, which is `screen` with the software's own
drawing of the map off. Only `screen` and `gpu` change the frame shown; `off` leaves the
software drawing the map. With the GPU presenter and no variable the stage is `gpu`, which
is what HD art, the lights and the camera need. The replay suite asks for `sdl` and `off`
unless told otherwise, so its runs are as they were.

### Full colour

At the last stage, with the check off, `CSSceneFull` draws the same bytes as `CSScene` and
beside each pixel its colour: a sprite's team pixels take the eight shades of the slot it
drew with (`port_teamcolor.cpp` fits them in OKLab for a slot with an RGB colour), shadows
and fades scale the colour under them, a translucent frame carries its table's change onto
the colour under it, and the fog applies colour matrices fitted from its 32 tables.
`CSShowFull` keeps those colours beside the indices and `PSComposeFull` shows them where
the cover says. The live value `colour.full` (the `fullColour` setting, Settings' Player
colours, `PortGpuFullColourSet`) turns it off, which draws the map as 1.17 does to the pixel.

### HD art

`port_hd.cpp` reads an image's Remastered art the first time it is drawn, on a loader
thread, out of the install's storage, keeping the DDS blocks as they are; without an
install nothing is read and the frame stays SD. `GpuHdScene` takes the primitives whose
image has art on the GPU out of the scene's map and records them as HD draws instead;
`GpuHdDrawFrame` then draws the resolved frame scaled up (zoomed out, the map's part from the
scene's map layer, which `GpuGroundDraw` resolves as for the present and `PSHdBase` filters as
`PSScale` does, since the resolved frame has the map at the screen's size) and each of those
frames over it in the game's order, with the player's colour where the team layer says, the fog's shade
read from the description's light map, and Remastered's lighting layers as `hd.lighting`
asks. The terrain, the console and the console's contents have their own HD paths. Its
passes are one function each, in one render pass: `GpuHdLightsUp` (the lights, before
it), `GpuHdBaseSetUp` and `GpuHdDrawBase` (the SD frame scaled up with the HD terrain,
and again after the sprites for the pixels drawn over the map), `GpuHdDrawSprites` (which
ends and begins the pass again before a distortion, which reads the frame so far),
`GpuHdDrawConsole` with `GpuHdUiItems`, and `GpuHdDrawBlend`. Art goes up a few images a
frame; F5 switches `hd` and the frame blends between SD and HD over
`hd.fade` seconds, during which the scene is drawn twice.

A large game changes art on nearly every sprite, and a draw cost the processor more than the
card's work on it. `GpuHdSpritesUp`, before the render pass, cuts the frame's
draws into runs: plain, shadow and translucent frames of up to 8 arts in a row go in one
instanced draw (`hd.hlsl`, `VSHdBatch` and `PSHdBatch`), each sprite's place, team colour,
fog and remap in a 128-byte record (`HdSpriteRec`) the frame uploads, its art one of the
batch's 16 samplers. A distortion, a lit sprite or a sprite of a ninth art starts a new run.
The remap, translucency and fog fits (`GpuHdSpriteFits`) are kept from frame to frame while
the palette and the table store stay. In the 32-player game zoomed to 16, about 2,140 draws
became about 200 and the present went from 3.3 to 0.66 ms. `HYPERION_HD_NO_BATCH=1` draws
them one by one; the two differ only where two runs one by one do.

### A mod's sheets

A mod image whose `grp` is a picture sheet is drawn by the HD frame from the sheet itself,
whether or not Remastered's art is (`GpuHdWanted`: the GPU presenter, and `hd` on with
Remastered's art there or `hd.mods` on in a game with a sheet, or an icon or wireframe of the
mod's own). Without Remastered's art the frame keeps the classic terrain and console and draws
only the sheets in HD, and the mod's own icons, wireframe and portrait over the console. `port_modart.cpp`
takes the sheet's smallest level at `PortHdScale` or above, else its largest, scales each cell
to that scale (nearest up, an average down), cuts it to its pixels on the classic canvas the
frames were made on (`ModSheetFrames`' `dx`, `dy`), and packs the cells into one RGBA picture
with a byte-a-pixel team layer beside it (`PORT_HD_RGBA8`, `PORT_HD_R8`), on a thread of its
own. A team pixel has the team colour's part taken out and grey of its brightness put in, which
the player's colour multiplies as it does Remastered's. The hue is the average of the pixels
wholly the team's, and its share is told by that hue's brightest channel over its dullest.
Mod art has a serial number, so art made again for a new game goes up again (`GpuHdTex`'s
`nSerial`), and a new game lets the last one's textures go (`PortModArtGeneration`). Images
before 999 keep Remastered's art; a mod image has only its sheet's.

The HD frames are drawn after the scene, over it, so the scene keeps the game's order for
them, or an HD ground unit would cover the air unit over it: `GpuHdScene` writes into each primitive left in the map how many HD draws came before
it (the kind's top 16 bits), and `CSScene` and `CSSceneFull` keep, two bytes a pixel after the
dialogs' planes in `gTouched`, the order of the last primitive that drew the pixel solidly
(`RankSet`; a shadow, a translucency and the fog leave it). An HD frame drawn before that
leaves the pixel to the map (`RankOver`, in `PSHdSprite` and `PSHdBatch`, at the map layer's
pixel and step, so zoomed too).

A distortion in HD reads the frame so far along its rows (`PSHdSprite`'s shift), so before
one the pass ends and the target is copied to `g_hdPrev`. Zoomed out that was about 300
copies of the whole target a frame in the 64-player game. `GpuHdDrawn` stamps squares of 32
of the target's pixels with each draw since the copy, and a distortion whose quad, widened by
its reach, meets none of them reads `g_hdPrev` as it is: the copy is left out and the frame
is the same.

The console's contents in HD are the command card's buttons and icons, the selection's
wireframe, the portrait, and the selection panel's slots and progress bar, with the numbers
1.17 writes over the slots kept as its pixels; over
the map, the resource counter's icons. The portrait is Remastered's HD video of the one 1.17 plays
(`HD2\portrait\` at 2x, `portrait\` at 4x): `PortHdPortraitFrame` asks Storm's
player which video plays in the console and on which frame (`PortVidPlaying`), opens the HD
video of the same name (`port_webm.cpp`) and decodes the frame at the same time into the
video's own rate, on the render thread when the frame changes; `GpuHdRgba` sends it up as
it changes and `PSHdUi` draws it over the portrait's control, scaled to cover the box and cut
to it. The box is 1.17's 60 x 56 and 6 rows more, the HD console's window for it.

A mod's own command icons, wireframes and portrait video are drawn the same way with or
without Remastered's console (`PortConsoleHdItems`' `bConsole`). `ArtBuild`
(`port_moddata.cpp`) makes each icon and wireframe picture at the HD scale as it makes the
classic frame, and `PortModUiSet` (`port_modart.cpp`) packs them into one RGBA picture: red
the brightness, green a wireframe pixel's part times 32 (1 to 4 the parts, 5 and 6 the outline,
as deep as the scale), alpha its own. Each is an item of `PORT_HD_UI_MOD`, a wireframe six of
them, a part each in the colour the classic remap gives it, which `PSHdUi` draws from texture 7.
Without Remastered's console `GpuHdDrawModUi` draws the items alone over the classic console,
and the console leaves those frames out: `PortConsoleUpdate` follows `PortGpuModUiShown`,
redrawing every dialog when it changes, and `statcmd_btn_ctrl_draw_fn`,
`statdata_slot_icon_draw` and `statwire_draw_dlg` skip a frame `PortConsoleModUi` names. An
icon there takes its remap's shade 1, the brightest the icons use; over Remastered's console
the remap's brightest at full strength, as Remastered's icons. A mod's portrait
(`PortHdPortrait`'s `bMod`) needs no Remastered art; over the classic console it fills the
classic box.

### Lights and effects

`CSprite::Draw` names each image it draws (`PortLightImage`); the images in the light table
become lights where they are drawn, and `PortLightsFrame` gives them in the frame's pixels,
moved through the camera when the picture is zoomed. `GpuLights` draws them into a light
buffer at the bloom's first level's size, and `PSPost` applies the light, the bloom, the
grade (exposure, contrast, saturation, temperature, tint) and the vignette as it draws the
frame into the swapchain. Every value is live in the channel (`GpuFxValues`); `effects`
turns the lot on or off and starts from the `effects` setting.

### Looks

A look draws a unit's images past their own colours: rich bases' gold mineral fields and violet geysers first, and any look a mod or the
creation suite makes. It is presentation only. `port_looks.cpp` holds the looks; a look has:

- a **rule**, the palette entries it takes: `blues` (a crystal's, and its darkest outline),
  `greens` (vespene's) or `all`; never the transparent entry or the team colours (8 to 15);
- a **ramp** of six colours, dark to light, onto which each entry it takes goes by its tone
  (OKLab lightness, from the darkest it takes to the lightest);
- the **images** it covers, places in images.dat; with none, all of a sprite's;
- a **material** for full colour, `crystal` or `gas`, and its clarity, shine, glow and sparkle;
- a **light** on the ground (colour, radius, strength, its centre from the unit's place);
- a **plume**: its height, density and rise, the vents it rises from (an image, a place in that
  image's frame, a strength, a width, and the frame it is for if only one: a vent naming the
  frame drawn stands in for the image's vents that name none), and the images it replaces. The
  geyser's frame is the tileset's, and 1.17 draws it 6 pixels to the right on space platform
  and installation and apart on ash world, so `rich-violet` has vents for frames 1, 2 and 3.

A unit's look is given by hand (`PortLookUnitSet`, the channel's `look_unit`), else by the
provider (`PortLooksProvider`; rich bases' `RichLookOf`), else by its type (a look's `units`).
Each map pass (`PortLooksPassBegin`, from `CSprite::DrawAll` after `PortLightsBegin`) gathers the
looked units' sprites, and a fogged copy of a looked unit where it stood, sorted by sprite.
`CSprite::Draw` asks `PortLookOfSprite`, and draws each image the look covers between
`PortLookImageBegin` and `PortLookImageEnd`: the remap table (`_gbRemapColorTbl`) goes through
the look's remap, each entry it takes to the palette entry nearest the ramp at its tone, made
again when the palette changes (`Make`). So the software picture, SDL's renderer, exact 1.17
colour and the GPU's index map all draw the recolour, and the check still matches. The
minimap's dot of a looked resource takes the look's pen, the entry nearest the ramp's fourth.

In full colour the GPU draws the rest (`scene.hlsl`, `LookColour` and `LookPrim`):

- `PortSceneRle` records each primitive's look (`g_primLook`, the look of the image drawn now,
  `PortLookNow`), and `GpuFullPrepare` puts it in the primitive's extras (word 1).
  `PortLooksGpu` gives each look's 416 bytes (the tone of each entry, the ramp, the material,
  the plume's and the light's numbers), uploaded after the extras; `SceneParams` carries where
  (`gLooksAt`) and the time (`gTimeMs`).
- A looked pixel takes the ramp at its entry's tone. Its relief is lit from 1.17's art light
  (from the top left): a crystal's faces turn out from the frame's middle and toward the light
  by the band of tone they were drawn in, gas by the slope of its brightness. A crystal shows
  the ground under it (deeper amber where it is thick, as 24 samples round the pixel measure
  it), reflects a warm sky by Schlick's rule, catches the sun and a sweep of light, and some
  of its bright pixels flash as stars. Each look glows from within, most at its heart, with a
  slow pulse. The pixels round a look that it does not take (the rock, a refinery's metal)
  take the glow beside them.
- `SCENE_PRIM_LOOK` (13) is a look's light, halo or plume, which `Apply` leaves alone. The light is
  recorded as the pass begins, before the sprites, so it lights only pixels no sprite has drawn
  yet: an ellipse half as tall as wide, flickering a little, `c * (1 + light * 1.3) + light² *
  0.12`, so the faint edge adds little to dark ground. The plume is recorded after the sprite's
  images (`PortLookSpriteDrawn`), from each vent of its main image measured from the frame's
  canvas: a column that widens, drifts and sways as it rises, broken by noise that rises with
  it, from the ramp's middle to its light. The images it replaces (1.17's puffs for vespene)
  draw nothing in full colour; instead the puff playing near a vent swells its plume, higher,
  wider and denser as the puff starts and less as it plays out (`PuffSwell`: the puff's frame
  against its group's frame count), so the plume keeps the puffs' own timing, part of the
  simulation. The swell rides in the primitive's top byte of `nParam`.
- A crystal's halo (the design's `gathered`): after each looked image of a crystal look whose
  glow is on, `PortSceneRle` records a look primitive over the image's box widened by
  `PortLookHalo`'s reach (6 pixels), with the image's own frame, clip and mirroring, so the
  shader reads the frame round each pixel. Past the frame's edge, the share of crystal pixels
  within 4.5 pixels (weighted by 1 / (1 + d²)) lights what is behind in the ramp's glow colour,
  in six steps, and the arms of the crystal's stars reach past its edge.
- The light, the halo and the plume are drawn in steps with a 4×4 Bayer dither on the map's
  pixels, as 1.17 draws its smoke (`looks.steps`).

The live values are `looks` (all of the GPU's part, on by default), `looks.steps`, and
`looks.clarity`, `looks.shine`, `looks.glow`, `looks.sparkle`, `looks.plume` and `looks.light`,
each 0 to 2 times the look's own.

`looks.json` in the active mod's folder (its data folder, else its own, as `hud.json`), or with
no mod in the data folder, is read as each game begins (`PortLooksGameBegin`, from `game.cpp`):
each look in it changes the built-in look of its name or is added. It is presentation only and
outside the mod's loader, so it is no part of the mod's fingerprint. A field left out keeps the
built-in look's value, or a new look's: the gold one's ramp and values, taking every entry
(`all`), as a crystal, with no images (so all of a sprite's) and no units.
`hyperion_mods.log` names what does not read.

```json
{"looks": [
  {"name": "frost", "rule": "all", "material": "crystal",
   "ramp": ["#04122c", "#0c3a6e", "#1a74b8", "#40a8ea", "#9ad6ff", "#eef8ff"],
   "images": [], "units": [64],
   "clarity": 0.38, "shine": 0.55, "glow": 0.3, "sparkle": 0.55,
   "plume": {"height": 54, "density": 0.99, "rise": 20, "replaces": [],
             "vents": [{"image": 344, "x": 73, "y": 19, "strength": 1, "width": 4.5}]},
   "light": {"color": "#40a8ea", "radius": 30, "strength": 0.6, "x": 0, "y": 8}}
]}
```

The channel's `looks` lists them, `look {name, ...}` changes one or adds it (the same fields),
`look_unit {unit, look}` gives a unit one by hand (`"none"` for none, `""` back to the game's),
and `looks_save` writes them all to that `looks.json`; the MCP tools of the same names call
them. A change by the channel lasts until the next game unless it is saved.

### A cinematic in full colour

With the GPU presenter a cinematic that is not a Smacker file keeps its colours
(`data.md`, Video). The player hands each picture's planes on at their own depth, 8 to 16
bits, through `PortVidPicture` to `PortGpuVideoSet`, which copies the rows into a transfer
buffer at once, so the decoder may reuse its planes before the frame is drawn. `GpuVideoUp`
uploads them into R8 or R16 textures (R8G8 or R16G16 for chroma in one plane), and
`PSVideoColour` puts them in colour at the video's size into an RGBA16F target, by the
matrix and range the file names, the chroma sampled between its samples. `GpuVideoDraw`
fits the picture to the whole window after the frame's pass, at the shape the player gives:
`PSVideoScale` resamples across and then down with Catmull-Rom's cubic, widened as far as the
picture shrinks (up to 8 times), and the last pass adds a triangular noise of one 8-bit step,
none at black and white, so the finer levels show as grain in the 8-bit swapchain rather than
bands. The subtitles are an overlay layer over it. `SVidPlayEnd` hands NULL, which releases
the textures. On SDL's renderer the cinematic is in the screen's 256 colours, as before.

### The console and the overlay

`PortConsoleUpdate` runs from the game loop and splits 1.17's 640-wide console into three
regions that follow Remastered's three HD pieces toward the screen's edges in a wide view,
and puts it back when the art goes to SD, the view changes size or Settings' Console layout
is set to One block (`PortConsoleEdgesSet`, `consoleLayout`). Whether the regions
move, an image of its own for a race (shown in place of the HD console, whose pieces are cut
for 1.17's images) and the map's full height at 640 are the active mod's when its `hud.json`
gives them (`PortConsoleModArt`, `docs/mod-data.md`). Settings' Console skin (`consoleSkin`)
picks the race whose art the console's parts load in a game played: `status_load_console`
takes it (`PortConsoleSkinTake`), and the console, its fluff panels, command buttons and card,
progress bars, portrait frame and chat box load by `PortConsoleRace` in place of
`byte_5264C2`, whose other uses, the advisor's voice and the supply icon, stay the race played.
A replay keeps its own console. A
benchmark's run hides the console altogether and gives the map the view's whole height. `port_overlay.cpp` builds
the timer, actions per minute and frame rate as UI primitives, which `GpuUiOverlay` draws
into the swapchain after the game's frame; SDL's renderer shows none of them.

### The layers over the game

What else draws over a game is a layer (`PortOverlayLayer`, `port_overlay.h`): a name, a place
in the stack, flags, and hooks a layer leaves NULL when it has no use for them. Each module adds
its own at file scope with `PORT_OVERLAY_LAYER`, and `port_overlay.cpp` keeps them in one list
by place, so nothing else names them:

| Place | Layer | Module | Hooks | Flags |
|---|---|---|---|---|
| 100 | the picture's black and cinema bars | `port_motion.cpp` | Draw | |
| 200 | a cutscene's words, and the player's input held | `port_cutscene.cpp` | Draw, Hold | |
| 250 | a cinematic's subtitles | `port_subtitles.cpp` | Draw | out of a game |
| 300 | Debug Tools' overlay | `port_debugview.cpp` | Area, Draw, Mouse, Outside | free cursor, over the cover |
| 400 | the trainer's strip and Tape | `port_trainerview.cpp` | Area, Draw, Mouse, Key, Outside | |
| 450 | a game of the AI lab's farm watched, its panel (`lab.md`) | `port_labwatch.cpp` | Draw, Key | its own counters |
| 500 | a watched LAN match's Live bug and L | `port_lanfeed.cpp` | Draw, Key | |
| 600 | a benchmark run's overlay | `port_benchview.cpp` | Area, Draw, Mouse, Key, Outside | free cursor, hides the console, its own counters |
| 650 | a LAN room's match starting: the map and players on black, and the player's input held | `port_lanlobby.cpp` | Draw, Hold | over the cover |
| 700 | the countdown of a skirmish or a room's match, and the player's input held | `port_intro.cpp` | Draw, Hold | |
| 750 | the players' stats where no HUD design places them, and their keys (`hud.md`, "The players' stats") | `port_hudstats.cpp` | Draw, Mouse, Key, Outside, Wheel | keys before 1.17's accelerators |
| 850 | the game menu (`front-end.md`, "The game menu") | `port_front_gamemenu.cpp` | Draw, Mouse, Key, Outside, Wheel | free cursor, Alt's keys, hides the game's cursor |
| 900 | the game's cursor over a HUD design or a layer that is the console's | `port_hwcursor.cpp` | Draw | |

They are drawn the lowest first, after the counters. The counters are left out while a shown
layer's panels cover them (its Outside), since a window drawn on the HUD's plates, as a music
add-on's would be, leaves its face to them, and the plates are under all of the overlay. Out of a game only the layers flagged
`PORT_OVERLAY_OUT_OF_GAME` are drawn, over 1.17's screens and the cinematics. The window's code asks the list, the top
layer first: `PortGpuGameBox` takes the area of the top shown layer with one
(`PortOverlayGameArea`), `WndProc` offers every message to the Hold hooks before anything else
sees it, then a mouse message and, in a game, a key or its character (`PortOverlayMouse`,
`PortOverlayKey`); the cursor's code asks whether a point is over a layer's panels
(`PortOverlayOutside`) and whether a shown layer frees the cursor; the console hides while one
says so (`PortOverlayHas`).

A layer's panels are either off the game or the console's to it. Off the game (Debug Tools',
the trainer's, a benchmark run's) Windows' arrow shows and the game keeps its pointer where it
left it, a few pixels in from the edges, so the map does not scroll under them. A layer flagged
`PORT_OVERLAY_CONSOLE` (an add-on's window; none at present) is the console's instead, as a HUD design's panels
are: `PortOverlayOutside` leaves it out, so the game keeps the pointer and draws its own cursor
over it (the cursor layer, 900, while one shows), and the map scrolls at the screen's edges under
it. `console_collision` and `DetermineCursor` answer from `PortCursorOverLayer`, so the cursor is
the arrow there and a click reaches no unit under the panels. The layer's Mouse takes the presses
and passes the moves on; `SendMouseMovedEvt` sends a move over the panels no further, unless a
selection box is being dragged. While the layer drags a window of its own, `PortCursorHoldScroll`
holds the pointer in from the edges, so the map does not scroll under the drag. A layer flagged
`PORT_OVERLAY_SYSKEYS` (likewise) also gets Alt's keys and F10, which come as
WM_SYSKEYDOWN and WM_SYSCHAR; the others never see them. While a layer flagged
`PORT_OVERLAY_HIDES_CURSOR` (the game menu's) shows, the game draws no cursor, neither into its
frame nor as the cursor layer, and Windows' arrow is the pointer over the whole window. A layer with a Wheel gets the wheel
while it shows, the pointer where its last mouse message put it, before the map's zoom: the music
player's takes it over its windows. Every hook but Shown is called whether the layer shows or not and
answers for itself, so Debug Tools' Overlay button draws while the overlay is off. The
benchmark's, the trainer's and a watched match's never show together. A new layer, a mod's
among them, takes a place between these and needs no change anywhere else.

### Map text

1.17 draws a string a byte a glyph from its fonts, which hold Windows-1252; only a Korean
Windows took a pair of bytes as one character, through GDI. StarCraft: Remastered's maps write
UTF-8, and Korean maps' programs CP949, so `gds_font.cpp` asks `port_maptext.cpp` at each byte
of 0x80 and over whether a character of several bytes starts there: valid UTF-8 first, then, on
a map whose string table has a Korean string (`MapTextStrings`, as `maphdr_STR` and
`maphdr_STRx` read it), a CP949 pair (`gen/cp949.inc`, from `tools/gen_cp949.py`). Anything
else is a byte of its own, as in 1.17. `font_puts` draws such a character as one glyph:
the font's own when Windows-1252 has the character (an é in UTF-8), else one `MapTextGlyph`
makes with `stb_truetype` from StarCraft: Remastered's `font\Kostar.ttf` (Hangul, arrows,
shapes), then its `font\bl.ttf` (every script), read from the install, then Hyperion's own
Noto faces (Latin, Greek and Cyrillic, then Japanese, Korean and Chinese, in the program's
resources, `build.md`), then the system's. The menus' language puts its own Noto face before
the other East Asian ones. A game in Japanese or Chinese looks in that language's face first
(`font\UDTypos58B_P_H.ttf`, `font\arudjingxiheiu20_b.ttf`), whose ideographs are drawn as the
language writes them. The glyph is 1.17's form: the face's coverage as the font's colours 1 to 3 and a
shadow in colour 7 a pixel right and down, sized to the font's height and set on the baseline
of its 'A', so it takes the text's colour codes, goes through `draw_char` and the GPU scene's
glyph cache like the font's own, and is kept for the process's life (the scene knows a glyph by
its address; the cache by height, baseline and character). `check_wrap`, `string_width` and
`font_strlen` measure with look-ahead (`FontWidthAt`), so a line never breaks inside a
character. The edit box takes typed text as UTF-8 (`TextEvent`, `port_winmsg.cpp`: a character
Windows-1252 lacks comes as its bytes, one `WM_CHAR` each), scrolls, deletes and moves its caret
by whole characters, and places an input method's window at its caret
(`PortPresentTextArea`). In another language a line of one-line text too wide for its control,
which would wrap out of sight (a console's "label: number"), takes the font set's next smaller
font, else the smallest with its TrueType letters narrowed down to 60% until it fits
(`FitText`, `dlg_draw.cpp`; `MapTextSqueeze`). Hyperion's skirmish screen shows a
map's title and description through `MapTextUtf8`, which reads each string on its own (UTF-8,
else a CP949 pair that makes a Hangul syllable, else Windows-1252), and the UI kit's glyph atlas
takes a character its face lacks from the same faces (`MapTextFace`, `GlyphOf` in
`port_ui.cpp`), as do the windows SDL draws (`GlyphFont` in `port_drawn.cpp`). All three size and
draw a glyph from its outline (`MapTextGlyphBox`, `MapTextGlyphDraw`), not the box its header
records: Kostar's composite Hangul record one 200 units of 1,000 too high, which cut the bottom
of every syllable. Presentation only: the replay suite is identical with it.

### The view and the camera

`g_portView` holds the view's size and everything 1.17 derived from 640x480: the map's
rows, the console's left edge, the scroll ring's megatiles, the dirty grid. `PortViewSet`
changes it between frames and `PortViewApply` switches a running game over. The camera
(`port_camera.cpp`) is a zoom out, 1 to 32: `PortViewRenderBegin` draws the map's part of a
pass over a larger render view around the game's own, which the game never sees, and the
presenter shows that at the camera's scale. `CSHud` draws what was drawn over the map at
the screen's size into the screen's copy. The zoom is held at 1 unless the GPU alone draws
the map and the zoom's own pipelines are there (`PortGpuZoomCan`), or SDL's renderer is the
presenter.

Zoomed out, the camera keeps its own offset from the game's view (a wheel's zoom at the cursor,
a followed unit) and takes the view's moves as they come. A centre-view trigger's scroll is the
exception: while it runs the camera aims at the place the trigger names (`PortViewCentreTarget`
keeps its middle), so the place is in the picture's middle and not to one side. At zoom 1 the
camera is the game's view, as in 1.17.

Under SDL's renderer (a browser's tab, or a PC where no GPU driver starts) there is no GPU
scene, so the software draws the render view itself (`g_bSoftPass`): into the render view's
own buffer and a scroll ring of its own size, swapped in for the pass, whole each pass
(`PortViewRenderSoft` makes `gamemap_update` take the full path). `PortViewRenderEnd` then
fills the game's buffer's map rows with the render view sampled at the camera, one pixel each,
and keeps that copy; the layers over the map (its text, the drag box, the cursor) draw over it
as before, and at the copy onto the screen `PortViewSoftCover` marks the screen's cover 1 where
the pixel is still the sample's and 0 where a layer drew. `PresentSoftZoom` draws the render
view in colour, scaled to the camera with linear filtering and clipped to the map's rows, and
the screen over it, clear where the cover is 1. A screenshot and a streamed seat take the
screen, the sampled map. The camera goes out no further than 4 there
(`PortViewZoomSoftMost`), a replay's whole map included, as the software draws the whole view
each pass. `HYPERION_ZOOM_FORCE`'s check keeps the software's own path at the game's view.

### Frames ahead, vsync and the frame rate

Settings' "Frames ahead" (`framesAhead`, 1 to 3, 2 by default) is both the fence ring in
`GpuSubmit` and SDL's allowed frames in flight. "Vertical sync" (`vsync`, on) sets the
present mode: vsync, or immediate, or mailbox where there is no immediate. Settings' "Frame
rate" (`frameRate`) decides when the loop draws at all: 0 is 1.17's way, a frame only when
something changed; 1 is 60 a second, 2 the display's refresh (the default), 3 is 144, 4 is
240, 5 no limit. With vsync on, `FrameRateHz` caps whatever was asked at the display's
rate, since a frame past the refresh is never seen and handing it over queues the next one
a refresh behind. The display's rate is read once a second, not once a pass.

A vsync or frames-ahead change asked for while the menus' frame is open (between
`PortGpuUiBegin` and `PortGpuUiEnd`, as Settings' Accept is) waits: changing the swapchain
there took away the image the frame blits into, and the blit crashed. `GpuSwapchainWaiting`
makes the change at the start of the next frame, the menus' or a game's, before it takes an
image.

## Settings and environment variables

| Name | Effect |
|---|---|
| `HYPERION_RENDERER=vulkan/direct3d12/gpu/sdl` | the presenter; without it Vulkan, then SDL_GPU's choice, then SDL's renderer |
| `HYPERION_GPU_SCENE=terrain/sprites/map/screen/gpu/off` | how far the GPU draws the map and where the check compares; `gpu` by default with the GPU presenter |
| `HYPERION_GPU_CHECK=1` | reads every frame back and compares it with the screen, and the GPU's map with the game's |
| `HYPERION_GPU_SCENE_DUMP=<frame>` | writes the first differing scene frames, and the one named, as raw maps |
| `HYPERION_GPU_DEBUG=1` | SDL_GPU's debug mode, and SDL's log to `hyperion_gpu_debug.log` |
| `HYPERION_WINDOW=fullscreen/windowed/background/hidden` | how the window is shown: `windowed` is the largest whole multiple of the game's frame that fits, `background` the frame's size and taking neither the focus nor the mouse, `hidden` never shown or presented; without it, as Settings saved it |
| `HYPERION_DISPLAY=left/right/<n>` | which screen a background window opens on |
| `HYPERION_GAME_SIZE=WxH`, `HYPERION_FRONT_SIZE=WxH` | a background or hidden window's size, for checks of what is drawn around the game |
| `HYPERION_FRAMES_AHEAD=<n>` | before the `framesAhead` setting |
| `HYPERION_HD_SCALE=4` | the 4x HD art instead of 2x |
| `HYPERION_ZOOM=<z>`, `HYPERION_ZOOM_FORCE=1` | the zoom a run starts at; the render view's path even at zoom 1 |
| `HYPERION_PERF=1` | `hyperion_perf.log` once a second: frames and passes a second, the pass, scene and present milliseconds |
| `HYPERION_FRAME_LOG=<folder>` | a row for every game frame and frame shown (`tools.md`) |
| `HYPERION_HD_NO_BATCH=1` | every HD sprite drawn by itself, not in batches |
| `HYPERION_FOG_CPU=1` | the fog's light map made on the processor though the GPU alone draws the map |
| `HYPERION_SCENE_STEP=<1/2/4/8/16>` | the map layer's step zoomed out, in place of `PortGpuSceneStep`'s |
| `HYPERION_PASS_KEEP_CHECK=1` | what a pass keeps made again as well, any difference to `hyperion_pass_keep.log` |
| `HYPERION_TEST_MOUSE=1` | a background window takes the mouse a harness posts (`drag.py`) |
| `HYPERION_CINEMATIC=<n>` | plays the cinematic numbered n at start-up (0 the logo, 1 the opening, 6 the first Terran mission's), to see one without its campaign |
| Settings "Display", "Monitor and resolution" (`display`, `monitor`, `monitorIndex`, `width`, `height`) | the window kind and where it sits; borderless full screen until the player chooses. Full screen stays up when the focus goes to another monitor (`SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS` off, `PortPresentInit`), as the borderless one does; SDL minimized it |
| Settings "Vertical sync" (`vsync`) | the present mode, and the cap on the frame rate |
| Settings "Frames ahead" (`framesAhead`), "Frame rate" (`frameRate`) | frames prepared before the card has drawn them; when the loop draws one |
| Settings "Art" (`art`), "HD lighting" (`hdLighting`), F5 | the HD art, and how much of Remastered's lighting it takes |
| `effects` setting, live `effects`, `bloom.*`, `grade.*`, `vignette.*`, `lights*` | the effects after the scene and their values |
| `fullColour` setting, live `colour.full` | the map in full colour, or 1.17's own pixels |
| live `looks`, `looks.steps`, `looks.clarity`, `looks.shine`, `looks.glow`, `looks.sparkle`, `looks.plume`, `looks.light` | the looks' full-colour part and its values ("Looks") |
| live `hd`, `hd.mods`, `hd.fog`, `hd.terrain`, `hd.console`, `hd.water`, `hd.kinds`, `hd.fade`, `hd.debug` | the HD paths and how they draw |
| Settings `hardwareCursor` | the system draws the game's cursor in a game shown in the window (`PresentHwCursor`, port_present.cpp), not for a streamed seat; Windows' arrow off the game's picture and the game's cursor share one display count (`PresentSystemCursor`) |
| Settings `confineMouse`, `dragScroll`, `dragScrollSpeed`, `mouseSensitivity`, `mouseSensitivitySpeed` | the cursor cage, the drag scroll, and the cursor's speed while the cage holds it (`PortMouseScale`: each move puts the pointer where the mouse's movement times the scale takes it, read from the pointer so queued moves are not counted twice) |
| Settings `gameTimer`, `apm`, `fps` | what the overlay draws over a game |
| Settings "View shape" (`view`), `smoothCamera` (live `camera.smooth`, for the run), `zoomMax`, `minimapSmoothDrag` | the view's width and how the camera moves |

## Integration points

- **In from the game.** `PortScreenLock`/`Unlock` and `PortPresentPalette` from the four
  places that reached Storm's surface; `PortPresentFrame` from `img_update`
  (`gds_image.cpp`), `dlg_dlgdraw.cpp`'s modal loop and `gds_vidinimo_PC.cpp`;
  `PortSceneMapBegin`, `PortSceneTerrainDone`, `PortSceneSpritesDone`, `PortSceneMapEnd` and
  `PortSceneScreenDone` from `gamemap_update` and `img_update_real`; the `PortRle*`,
  `PortDrawCell`, `PortDrawChar` and `PortMask*` bodies through `port_names.cpp` and the
  draw function table in `src117/gen/code_map.cpp`; `PortLightImage` and `PortSceneImage`
  from `CSprite::Draw`; `PortConsoleUpdate` and `PortViewApply` from `RunGameLoop`.
- **Out to the platform.** SDL3 for the window, the GPU device and the saving of images.
  The window itself is SDL's, made by `InitWindow` through `port_winmsg.cpp`;
  `port_window.cpp` gives it its icon and square corners.
- **Out to other modules.** `port_front.cpp` draws its own frames through
  `PortGpuUiBegin`/`UiEnd` (`front-end.md`); `port_control.cpp` asks for snapshots
  (`PortGpuSnapAsk`) and holds the live values; the layers over the game
  (`port_overlay.h`) take the area around the game's frame (`PortGpuGameBox`), and
  `port_bench.cpp` and `port_debugview.cpp` read the frame times; `port_lanstream.cpp` takes the frame as shown; `port_clock.cpp` and
  `PortFrameDue` decide when the loop draws (`loop.md`); `tools/replaytest/rt_observer.cpp`
  reads the frame through the exported `oc_screen` and `oc_screen_size`.

## Checks

- **The presenter's check.** `HYPERION_GPU_CHECK=1` (`replaytest.py --gpu-check`) reads the
  resolved frame back every frame and compares it with the screen through the palette on
  the CPU, hidden windows included. With the GPU alone drawing the map, only the pixels
  outside the cover are compared, since the screen holds nothing else to compare with.
- **The scene's check.** The same variable, with `HYPERION_GPU_SCENE` at a stage, compares
  the GPU's map with the game's buffer at that stage, and at the last stage the pixels the
  screen took. `GpuSceneCompare` counts the pixels, the stale ring tiles and the primitives
  of each kind it covered.
- **Where the counts go.** `hyperion_render.log`, rewritten as it goes, since a replay run
  ends the process without a shutdown: the presenter, the device, the frames checked and
  mismatched, the first difference, and the kinds covered. The replay observer logs the
  presenter and the counts at the end of a run, and the report puts them beside each run's
  verdict.
- **How to run it.** `python tools/replaytest/replaytest.py suite --renderer vulkan --scene screen
  --gpu-check` for the suite (a frame that differs fails the run); `python tools/play.py --renderer vulkan --scene gpu` to play
  a build; `--perf` adds `hyperion_perf.log`. Snapshots against the oracle's whole screen
  (`--snap-screen`) are what catch a frame that is self-consistent but wrong.
- **What a pass keeps.** `HYPERION_PASS_KEEP_CHECK=1` (`hxrender.py sweep --env`) makes
  the fog's grid, the render view's ring and `cell_refresh` again where they were kept and
  counts the differences.
- **The gate.** Nothing here may change the simulation, so the replay suite must stay
  identical on every frame (`../replay-test.md`).

## Gotchas

- A drawing thread is not possible as things stand: SDL hands out the swapchain image only
  on the thread that made the window, which is the game's, so the present and its
  submission cannot move off it.
- `PortSceneTake` hands the frame out once. `PresentShown` calls it too, so a caller that
  reads the frame as shown before `PortPresentFrame` takes the description out of its
  hands; both call `PortGpuScene` for that reason.
- The cover is uploaded only when it changed. Anything that writes to the screen outside
  the map's copy must clear its part (`PortScreenCoverRect`) or clear the lot
  (`PortScreenCoverClear`), or the frame keeps showing the GPU's older pixels there.
- A uniform block past 4 KiB reads as zeros on SDL's Vulkan. `GPU_UNIFORM_BLOCK_MAX`
  (`port_gpu_internal.h`), the static asserts in `port_gpu_scene.cpp` and `port_gpu_hd.cpp`,
  and `tools/shader_layout.py`, which the build runs over the shaders, hold the line; adding
  fields to a block near the limit means splitting it, not growing it. The HD base pass keeps
  its palette and its translucency shares in blocks of their own (`BasePalette`, `BaseShares`,
  `BaseSharesMore`) and the console its palette (`ConsolePalette`): before 2026-10-19 they
  were one block each, and under Vulkan the HD frame lost what was drawn over the map, so its
  sprites showed over the dialogs, the menus and the cursor.
- A three-component vector after a scalar in a uniform block sits beside it in Direct3D's
  layout and 16 bytes on in SPIR-V's, moving every field after it; `tools/shader_layout.py`
  fails the build on any field whose offsets differ.
- SDL_GPU binds at most 8 storage buffers of each kind, which is why the description's
  parts share buffers at offsets (`SCENE_STORE_*`) rather than having one each.
- The check and a snapshot submit the frame's command buffer themselves and wait on a
  fence, so they step outside the frames-ahead ring; perf numbers taken with the check on
  are not a player's numbers.
- The full colour is off under the check (`bFull` requires no check and no
  `HYPERION_GPU_CHECK`), because the check compares the frame with 1.17's own pixels.
- With HD art on, the primitives drawn in HD are left out of the GPU's map, so the map
  layer is not whole: `g_bGroundOk` goes false and the camera's zoom holds at 1.
- `g_screen` is sized for the widest view, but its rows are the *current* view's width
  apart. Index it with `g_portView.nWidth`, never with `PORT_VIEW_MAX_WIDTH`.
- Mouse positions reach the game in game pixels, and the two presenters letterbox
  differently (`SDL_RenderCoordinatesFromWindow` against `PortGpuGameBox`). A new overlay
  that takes room around the game's frame has to be added to `PortGpuGameBox` or clicks
  land in the wrong place.

## Open items

- Remastered's fonts and cursors, the last of the HD UI.
- A thread for the scene's recording and uploads is possible where the present is not, and
  is not built.
- The check compares at zoom 1, where the map layer's step is 1. The steps past it and the
  distortions' batches at a step of 4 or more are seen in `hxrender.py shots`, not compared
  pixel by pixel; nor is the HD frame.
- HD art with the camera zoomed out: the HD frame is drawn through the camera
  (`g_hdZoom`) over the scene's map layer, but `PSGround`'s layer under a moving console
  is made only in the SD path, so the two together are not fully checked.
- Lighting beyond the 2D lights, and anything HDR, wait on the user's word after the
  renderer settles.

## Further reading

- `../hyperion-exe.md`: every environment variable of the program, and how to run it.
- `front-end.md`: Hyperion's menus, which draw through `port_gpu_ui.cpp`.
- `../replay-test.md`: the gate every change here is held to.
- `loop.md`: when a frame is drawn, the worker pool and the rest between frames.
- `../hud-format.md`: the HUD designs this module draws.
