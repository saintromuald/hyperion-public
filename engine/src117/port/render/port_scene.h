// port_scene.h -- the frame description (port_scene.cpp) as the GPU scene renderer
// reads it (port_gpu.cpp; docs/renderer.md, R4b). Plain data: neither the game's
// headers nor SDL's.
//
// Each frame of a game, the map layer is described as well as drawn: which megatile,
// and which creep edge, the scroll ring holds for each tile of the view, then every
// sprite and line in the order the game draws it, with the colour tables it drew
// through. The art the description points at is decoded once into an atlas of
// 16-bit pixels, a colour index with bit 8 set where the pixel is opaque, and the
// tables go into a store of their own, each once. The GPU draws the description into
// an 8-bit map of its own, which the check compares with the one the game drew.
#pragma once

// How far the GPU draws, and where the game's own map is taken for the check.
enum SceneStage {
  SCENE_OFF = 0,
  SCENE_TERRAIN = 1,  // the terrain alone, before the sprites
  SCENE_SPRITES = 2,  // and the sprites (CSprite::DrawAll)
  SCENE_MAP = 3,      // the whole map layer: the stars, the fog, the text over it
  SCENE_SCREEN = 4    // and the layers drawn over it into the same buffer (the building
                      // placement, the help bubble, the cursor); the frame shown takes
                      // the GPU's map where the map reaches the screen
};

// What the ring holds for one tile of the view: the megatile id as the game's cell
// map has it (bit 15 is creep), the tile it was drawn for, and the atlas entry of
// the creep edge drawn over it, or SCENE_NONE; and that edge as the HD terrain takes it
//: its frame in the tileset's edge GRP in the low byte, its x and y
// offset in the tile in the next two, bit 31 set; 0 with no edge.
struct SceneSlot {
  unsigned int nMtx;
  unsigned int nTile;  // x in the low 16 bits, y in the high
  unsigned int nEdge;
  unsigned int nEdgeFrame;
};

// A frame of art in the atlas: where its pixels start (in pixels), its size and the
// offset it is drawn at.
struct SceneEntry {
  unsigned int nOffset;
  unsigned int nSize;    // width in the low 16 bits, height in the high
  unsigned int nOrigin;  // x offset in the low 16 bits, y offset in the high
  unsigned int nMax;     // its highest colour index
};

// What a primitive does with each opaque pixel of its frame, v, and the pixel under
// it, d (port_draw.cpp's DrawOp structs), through the table at nTable in the store.
enum ScenePrimKind {
  SCENE_PRIM_NORMAL = 1,     // table[v]: the remap, player colours in it
  SCENE_PRIM_SHADOW = 2,     // table[d]: a shade of darkness
  SCENE_PRIM_EFFECT = 3,     // table[v * 256 + d]: translucency, up to 64 KB, of
                             // which only the rows up to the frame's highest v are read
  SCENE_PRIM_FADE = 4,       // table[d]
  SCENE_PRIM_UNCLOAKED = 5,  // c = table[v], drawn where brightness[c], signed, is over the threshold
  SCENE_PRIM_WARP = 6,       // the warp texture, the clip's size, read in the order drawn
  SCENE_PRIM_SPAN = 7,       // a line in the pen's colour: nX the first pixel, nY how many,
                             // nClip0 the step between them, nTable the colour
  SCENE_PRIM_DISTORT = 8,    // the pixel a distance away, the distance v or brightness[v]
  SCENE_PRIM_STAR = 9,       // a star of the space platform's parallax, nClip1 its size, its
                             // pixels at nTable: drawn where the map is still 0
  SCENE_PRIM_FOG = 10,       // the fog over the whole map: the light map is in the frame's
                             // bytes, its first block at nX, nY bytes to a row; nTable is
                             // the 32 shades of darkness
  SCENE_PRIM_GLYPH = 11,     // a character of text: the frame's pixels are colour numbers,
                             // 0 to 7, of the 8 colours at nTable
  SCENE_PRIM_BLIT = 12,      // a bitmap copied in, nClip1 its size, its pixels in the frame's
                             // bytes at nTable; nParam 0 copies, 1 copies those not 0, 2 and 3
                             // take the 64 KB table at nEntry, the source's byte high (2) or
                             // the map's (3)
  SCENE_PRIM_LOOK = 13,      // a look's light on the ground or its plume (port_looks.h), drawn
                             // in full colour only, over nClip1's box at nX, nY: nClip0 its
                             // centre (x low, y high, signed), nEntry its numbers, nParam the
                             // look, PORT_LOOK_LIGHT or _PLUME at bits 8 and a seed at 16
  SCENE_PRIM_MIRROR = 0x100  // or'ed in: the frame drawn mirrored left to right
};

// One thing drawn, in the order drawn: its frame's entry, drawn at nX, nY, the part
// of the frame the clip keeps (the rle_draw routines' DrawClip), and its table.
struct ScenePrim {
  unsigned int nKind;
  int nX;
  int nY;
  unsigned int nEntry;
  unsigned int nClip0;  // the clip's left in the low 16 bits, its top in the high
  unsigned int nClip1;  // its width in the low 16 bits, its height in the high
  unsigned int nTable;  // byte offset in the table store, or SCENE_NONE
  unsigned int nParam;  // SCENE_PRIM_UNCLOAKED: the threshold in the low byte, the
                        // brightness table's offset above it
};

enum { SCENE_NONE = 0xFFFFFFFFu, SCENE_BIN = 32 };

struct SceneFrame {
  int nStage;          // SceneStage
  int nWidth;          // the map layer: the view's width, a multiple of 16
  int nRows;           // and its rows (400 in 1.17)
  int nHeight;         // the screen buffer's rows, where a distortion wraps
  int nScrollX;        // the map pixel at its top left
  int nScrollY;
  int nRingTilesX;     // the ring's size in megatiles (port_view.cpp)
  int nRingTilesY;
  const SceneSlot *pSlots;      // nRingTilesX * nRingTilesY, by tile x and y modulo the ring's size
  // Which ring they are, 0 the game's and 1 the render view's, and the slots of it written
  // since the last frame of that ring taken, nSlotsFirst up to nSlotsEnd.
  int nSlotsRing;
  unsigned int nSlotsFirst, nSlotsEnd;

  // The tileset: each megatile's 16 cell references (bit 0 mirrors the cell), and
  // the cells' 8x8 pixels. nTileset changes when they do.
  unsigned int nTileset;
  int nEra;                     // which tileset: the map's ERA, 0 badlands to 7 twilight
  // The space platform's parallax: each of the five layers' shift in game pixels, x then
  // y, 1.17's accumulator i (cell_draw_background) at i and 5 + i. StarsDraw gives the
  // file's first layer accumulator 4, its second 3, and so on.
  int anStarOfs[10];
  const unsigned int *pMegatiles;  // a DWORD a cell reference
  unsigned int nMegatiles;
  const unsigned char *pCells;
  unsigned int nCells;

  // The atlas and the table store, all of them; what is past the counts the GPU has
  // is new. nAtlasSerial changes when they start again, at each map's load.
  unsigned int nAtlasSerial;
  const SceneEntry *pEntries;
  unsigned int nEntries;
  const unsigned short *pPixels;
  unsigned int nPixels;         // a multiple of 2
  const unsigned char *pTables;
  unsigned int nTables;         // bytes, a multiple of 16

  // The primitives, and for each SCENE_BIN-pixel square of the map the ones that
  // reach it, in order: square i's are pBinList[pBinStart[i]] up to pBinStart[i + 1].
  // A distortion is in no square: it reads the map around it, so the GPU draws it in
  // a pass of its own.
  const ScenePrim *pPrims;
  unsigned int nPrims;
  // For each primitive, the image of images.dat and the frame of its GRP it drew (the
  // image in the low 16 bits, the frame in the high), or SCENE_NONE when it is not an
  // image's: what the HD art is chosen by (port_hd.h).
  const unsigned int *pPrimArt;
  // And the player colour it drew with, the slot whose ramp sub_41D740 put in the remap
  // (sgdwCurrentDrawColor): what the full colour's team shades are chosen by (stage G).
  const unsigned char *pPrimColor;
  // And the look of the image it drew, as PortLookNow gives it (port_looks.h), and
  // the looks' data the GPU reads, nLooksBytes of it.
  const unsigned short *pPrimLook;
  const unsigned char *pLooks;
  unsigned int nLooksBytes;
  // And its GRP's largest frame, the canvas the image is centred in (width low, height
  // high), or 0: half of it back from the frame's offset is the image's anchor.
  const unsigned int *pPrimGrp;
  int nBinsX;
  int nBinsY;
  const unsigned int *pBinStart;  // nBinsX * nBinsY + 1
  const unsigned int *pBinList;
  unsigned int nBinList;

  // Bytes that are this frame's alone: the fog's light map, the bitmaps copied in.
  const unsigned char *pBytes;
  unsigned int nBytes;
  // The fog's light map when the GPU makes it (PortSceneFog): nFogLightBytes of it at
  // nFogLightAt in the frame's bytes, past nBytes and not uploaded, made by CSFogLight
  // (fog.hlsl) from the smoothed mask grid at nFogGridAt (nFogGrid bytes to a row) through
  // 1.17's ramp at nFogRampAt, over a ring nFogRingX by nFogRingY megatiles, nFogPitch bytes
  // to a row of light. nFogLightBytes 0: the light map is in the bytes as the software made it.
  unsigned int nFogLightAt, nFogLightBytes;
  unsigned int nFogGridAt, nFogGrid, nFogRampAt, nFogRingX, nFogRingY, nFogPitch;

  // The game's own map at the stage, nWidth x nRows, for the check; at SCENE_SCREEN
  // the whole buffer, nWidth x nHeight, and pCopied the pixels of it the screen takes
  // this frame (1 each), which are the ones compared.
  const unsigned char *pGame;
  const unsigned char *pCopied;

  // With the camera zoomed out (port_view.cpp's render view), bZoom 1: the map layer
  // above is the render view's, nWidth x nRows at nScrollX, nScrollY, and only the primitives
  // before nMapPrims are in its squares; those after were drawn over the map at the screen's
  // size, nScreenW by nScreenRows (dialogs, the map's text, the drag box, the cursor), in squares
  // of their own. The screen is nScreenW x nScreenH, pCopied and pGame its; the camera the map
  // pixel at its top left and the map's pixels a screen pixel. Unzoomed, nMapPrims is nPrims.
  int bZoom;
  int nScreenW;
  int nScreenH;
  int nScreenRows;
  unsigned int nMapPrims;
  int nHudBinsX;
  int nHudBinsY;
  const unsigned int *pHudBinStart;
  const unsigned int *pHudBinList;
  unsigned int nHudBinList;
  double fCamX, fCamY, fZoom;
  int nGameScrollX, nGameScrollY;  // the game's own view's top left

  // The GPU's map layer: nStep of the game's pixels to one of its pixels, across and
  // down, each the game's pixel at the middle of its square; nLayerW (a multiple of 4) by
  // nLayerRows of them. The squares above are SCENE_BIN of its pixels. nStep is 1, and the
  // layer nWidth by nRows, but with the camera far out (PortGpuSceneStep).
  int nStep;
  int nLayerW, nLayerRows;
};

// The frame the game has just described, or NULL when there is none (no game, the
// scene off, or the map layer not drawn this frame). Taken once: a second call before
// the next frame gives NULL.
const SceneFrame *PortSceneTake(void);
// The stage HYPERION_GPU_SCENE asks for (SceneStage), and whether the GPU alone draws
// it ("gpu": the software's drawing off, the screen buffer's map not the frame's).
int PortSceneStage(void);
int PortSceneGpuOnly(void);
// The render view's part of a pass (port_view.cpp): between the two the ring's
// reports go into slots of the render view's own, and at its end the primitives so far are
// the map's.
void PortSceneRenderBegin(void);
void PortSceneRenderEnd(void);
// The render view's ring at the start of its pass, from the map's cells as the player has
// seen them (gfpCellMap) and the creep's edges (sg_pEdgeMap): what cell_paint reports of
// each megatile, written only where the slot holds something else. Zero when the
// GPU does not draw the map alone, where cell_paint must draw the ring.
int PortSceneRingFill(void);
// The passes recorded since the last call and the time they took, in milliseconds
// (HYPERION_PERF, port_present.cpp).
void PortSceneTimes(double *pdMs, unsigned int *pnPasses);
// The last pass's time alone, for the frame log (port_framelog.cpp).
double PortSceneLastPassMs(void);
// The box, in the game's pixels of the layer nWidth x nRows and clipped to it, that the primitives nFirst up to nLast of
// pPrims can draw in, as the squares take them (SceneBin); false when none reaches the layer
// (the GPU's passes after a distortion, port_gpu_scene.cpp).
bool PortSceneRunBox(const ScenePrim *pPrims, unsigned int nFirst, unsigned int nLast, int nWidth, int nRows,
                     int *pnLeft, int *pnTop, int *pnRight, int *pnBottom);

// The image CSprite::Draw is about to draw, its images.dat place, its GRP frame and the
// GRP's largest frame, for the frame description's HD art (port_scene.cpp);
// nImage -1 when it is done.
void PortSceneImage(int nImage, int nFrame, int nGrpWidth, int nGrpHeight);

// The frame description the GPU scene renderer draws from (port_scene.cpp,
// docs/renderer.md, R4b). gamemap_init calls PortSceneMapLoaded once the tileset is
// in. gamemap_update calls PortSceneMapBegin first, which answers non-zero when the
// scene is on and the pass must draw the whole view, PortSceneTerrainDone once the
// view's terrain is in the screen buffer, and PortSceneMapEnd last. cell_draw_mtx
// and cell_draw_creep report what they draw into the scroll ring.
void PortSceneMapLoaded(void);
int PortSceneMapBegin(void);
// A count that moves when the game's state is put back other than by its frames: a map's
// load, and a snapshot's restore (port_snap.cpp, PortSceneStateRestored). What a pass keeps
// from the last while no game frame has run (the render view's ring, the fog's grid) is made
// again after either.
unsigned int PortSceneStateSerial(void);
void PortSceneStateRestored(void);
// HYPERION_PASS_KEEP_CHECK=1 makes what a pass keeps again each time as well and writes each
// difference from what was kept to hyperion_pass_keep.log: PortScenePassKeepCheck says
// whether it is on, PortScenePassKeepDiffer writes one (what, and how many bytes or slots, 0
// for none: a line every 4,096 checks says how many were made).
bool PortScenePassKeepCheck(void);
void PortScenePassKeepDiffer(const char *pszWhat, unsigned int nCount);
// Whether what cell_refresh reads is as it was at the last call: the game's frame
// (the fog dwords, the tiles' kinds and the cell map change in one), the state serial, the
// view's tiles and ring, the map, and the masks the player's vision is read with. Then it
// changes nothing, and gamemap_update's full path skips it: zoomed out it walked every tile
// of the render view a pass, most passes between the game's frames.
int PortSceneCellsKept(void);
// The megatiles reported to the scene so far (PortSceneCell), for HYPERION_PASS_KEEP_CHECK.
unsigned int PortSceneCellReports(void);
void PortSceneTerrainDone(void);
void PortSceneMapEnd(void);
int PortSceneCell(unsigned int wMtx, int nX, int nY);
int PortSceneCreep(int nX, int nY, const void *pFrame, int nFrame);
// After CSprite::DrawAll. The sprite routines (port_draw.cpp) and the pen's lines
// (bmp_hline, bmp_vline) report each call that draws into the screen buffer during
// the pass: its kind (port_scene.h, ScenePrimKind), the frame and clip, and the table
// it reads as it is at the call.
void PortSceneSpritesDone(void);
int PortSceneRle(unsigned int nKind, int nX, int nY, const void *pFrame, const void *pClip,
                 const void *pTable, unsigned int nTableBytes, unsigned int nParam);
int PortSceneRleUncloaked(int nX, int nY, const void *pFrame, const void *pClip, const void *pRemap,
                          const void *pBrightness, int nThreshold, int bMirror);
int PortSceneSpan(const void *pFirst, unsigned int nCount, unsigned int nStride, unsigned int nColor);
// The rest of the pass: the stars (StarBlit), the fog (mask_draw) and the text over
// the map (draw_char).
int PortSceneStar(const void *pPixels, int nX, int nY, int nW, int nH);
// A look's light on the ground or its plume (SCENE_PRIM_LOOK, port_looks.cpp).
void PortSceneLook(int nX, int nY, int nW, int nH, unsigned int nCentre, unsigned int nNumbers, unsigned int nParam);
// The fog: the light map (pLight, nBytes of it, nPitch to a row, the view's first block at
// nFirst), the 32 shades, and the smoothed grid it is made from with 1.17's ramp
// (dword_670A80), for the GPU to make it from.
int PortSceneFog(const void *pLight, int nPitch, int nBytes, int nFirst, const void *pShades, const void *pGrid,
                 int nGrid, int nRingX, int nRingY, const void *pRamp);
// mask_update: whether the GPU makes the fog's light map this pass, so the software need not
// (Fog). Set by the presenter (PortSceneFogGpu) when CSFogLight is there.
int PortSceneFogOnGpu(void);
void PortSceneFogGpu(bool bCan);
int PortSceneGlyph(const void *pDest, const void *pBits, int nWidth, int nHeight, int nRowMod,
                   const void *pColors);
// The layers over the map: bitmaps copied into the buffer (PortBltRop3, bmp_mask_rect,
// the dialogs' PortTableBlit; nMode a ScenePrim BLIT's nParam, pTable 64 KB or NULL),
// and img_update_real's end of it, with the pixels the screen takes (PortMaskBits).
int PortSceneBlit(const void *pDst, int nDstPitch, const void *pSrc, int nSrcPitch, int nWidth, int nHeight,
                   const void *pTable, int nMode);
void PortSceneScreenDone(const struct _HSTRANS *pMask);
// Each report above answers non-zero when the GPU alone draws what it reported
// (HYPERION_GPU_SCENE=gpu), and the caller then skips its own drawing;
// PortSceneSkipDraw says the same for the pass (the ring's copy into the buffer), and
// PortSceneGpuOnly whether the mode is on at all.
int PortSceneSkipDraw(void);
// A benchmark run in 1.17's 256 colours (port_bench.cpp): the software drawing between games
// while true, the GPU scene again after (unless HYPERION_GPU_SCENE chose a stage).
void PortSceneSoftware(bool bSoftware);
// The GPU presenter starts the GPU scene as "gpu" does when HYPERION_GPU_SCENE gives
// none (port_present.cpp).
void PortSceneDefault(void);
