// port_scene.cpp -- the frame description: each pass of the map layer recorded as it is drawn,
// for the GPU to draw again (port_gpu_scene.cpp, shaders/scene.hlsl; docs/modules/render.md,
// "The frame description and the GPU scene"; docs/renderer.md, R4b). Presentation only.
//
// While the scene is on, the map layer takes its full path every pass, so the description is
// whole. The terrain is the scroll ring's: for each ring tile the megatile and creep edge
// cell_draw_mtx last drew there and the tile they were for; a view tile whose entry is for
// another shows the ring's leftover, which the GPU draws black and counts. Over it, each call
// that draws into the screen buffer during the pass (port_draw.cpp's routines, the pen's lines,
// StarBlit, mask_draw's light map, draw_char, bmp_mask_rect, bmp_copy_rect, the cursor) is a
// primitive in the order made, with its frame and its table as the table stood at the call.
// Frames go into the atlas once and tables into the store once, found again by their bytes;
// both start again at a map load. A cursor's frames are freed and others loaded in their place,
// so a frame's atlas entry is found by its address and a fingerprint of its codes.
// HYPERION_GPU_SCENE names the stage the GPU draws to and the check compares at (SceneStage).
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>
#include <vector>
#include "bmp.h"
#include "grp.h"
#include "port_mask.h"  // PortMaskBits
#include "port_present.h"  // PortPresentGpu, PortScreenCoverCopied, PortScreenCoverClear
#include "port_view.h"
#include "port_scene.h"
#include "port_looks.h"  // PortLookNow, PortLooksGpu
#include "../data/port_stormbase.h"  // PortMemSize
#include "../platform/port_globals117.h"

// Game globals (re/globals117.json), bound to their storage by the build
//.
extern bmp_t _img_draw_buf;   // 0x6D0B88, the screen buffer the map is drawn in
extern WORD g_MapTileset;     // 0x5264BC, the map's ERA
extern bmp_t *_bmp_current;   // 0x6D0C40, what the drawing routines draw into
extern LONG glScrollPixelX;   // 0x543218, the map pixel at the view's top left
extern LONG glScrollPixelY;   // 0x54321C
extern LONG dword_543258[5] OC_UNALIGNED;  // 0x543258, the parallax layers' shifts across, x256
extern LONG dword_54326C[5] OC_UNALIGNED;  // 0x54326C, and down
extern BYTE *gfpMtxSet;       // 0x543238, the tileset's megatiles, a DWORD a cell (PortMegatilesLoad)
extern WORD *gpMtxInfo;       // 0x6CE650, its tile groups (.cv5)
extern DWORD gdwMIEntries;    // 0x666968, how many groups
extern WORD *gfpCellMap;      // 0x54323C, the map's megatiles as the player has seen them
extern WORD gwMapWidth;       // 0x5264B4
extern WORD gwMapHeight;      // 0x5264B6
extern BYTE *sg_pEdgeMap;     // 0x6CA7A8, the creep's edge over each tile, its frame + 1, or 0
extern int gdwCreepMtxWdt_g;  // 0x6CA7AC, the edge map's tiles across
extern BYTE *sg_pEdgeGroup_g; // 0x6CA7B4, the edges' frames (cell_draw_creep)
extern unsigned int oc_dwReplayFrame;  // 0x52651C, the game's frames so far
extern WORD gwScreenTileX;    // 0x5264B0, the view's top left tile
extern WORD gwScreenTileY;    // 0x5264B2
extern DWORD *gpSquareMap;    // 0x666454, each tile's fog and creep bits
extern WORD *gfpMIMap;        // 0x666458, each tile's kind
extern DWORD gPlayback;       // 0x67CC90, a replay
extern DWORD sgbShowAll_g;    // 0x67CC88, a replay's whole map shown
extern DWORD gdwReplayVisionMask;  // 0x67CC84, the players a replay is seen as
extern DWORD gdwLocalMask;    // 0x672668, the player's sight in each tile's bits
extern DWORD sgdwCurrentDrawColor;  // 0x5D6E38, the colour slot sub_41D740 put in the remap

enum {
  CV5_GROUP_WORDS = 26,   // a group of the .cv5 is 52 bytes
  CV5_FIRST_MTX = 10,     // and its 16 megatile ids start at its 11th word
  MTX_CELLS = 16,         // a megatile is 4x4 cells
  SLOTS_FIRST = PORT_VIEW_RING_TILES_X_MAX * PORT_VIEW_RING_TILES_Y_MAX
};

static int g_nStage = -1;           // read from the environment on first use
static bool g_bRecording;            // from gamemap_update to the copy onto the screen
static bool g_bOnly;                 // "gpu": the software draws none of what is recorded
static LARGE_INTEGER g_passStart;    // when the pass began, for HYPERION_PERF
static double g_dPassMs;             // the passes recorded since PortSceneTimes, and their time
static unsigned int g_nPasses;
static bool g_bReady;                // g_frame describes a pass not yet taken
static SceneFrame g_frame;
static const SceneSlot g_slotEmpty = {0, static_cast<unsigned int>(SCENE_NONE), static_cast<unsigned int>(SCENE_NONE), 0};
// The ring's slots, as many as the widest view's ring to start with and more when a view
// zoomed out over the map needs a larger ring.
static std::vector<SceneSlot> g_slots(SLOTS_FIRST, g_slotEmpty);
// The render view's, filled at each of its passes, and its pass's place.
static std::vector<SceneSlot> g_renderSlots;
static bool g_bRenderSlots;    // PortViewRenderBegin to PortViewRenderEnd
// The slots written since each ring's last frame was taken, the game's and the render view's
//, so the GPU takes only those: none when nFirst is past nEnd, all from 0 to SIZE_MAX.
struct SlotsDirty {
  size_t nFirst, nEnd;
};
static SlotsDirty g_aSlotsDirty[2] = {{0, SIZE_MAX}, {0, SIZE_MAX}};
static unsigned int g_nRingFillSerial;  // the atlas the render view's slots' edges are in
// What the last fill read from: the game's frame, the ring's top left tile and size. The cells
// and the edges change in a game frame, or in a pass where cell_refresh redraws a tile and
// its neighbours' edges (g_bRingTouched), so with none of these moved the slots are as filled.
static unsigned int g_nRingFillFrame, g_nRingFillState;
static int g_anRingFillAt[4] = {-1, -1, -1, -1};
static bool g_bRingTouched;
static bool g_bZoomPass;       // the pass drew its map over the render view
static unsigned int g_nMapPrims;
static int g_nRenderRingX, g_nRenderRingY, g_nRenderHeight;
static std::vector<unsigned int> g_hudBinStart;
static std::vector<unsigned int> g_hudBinList;
static std::vector<unsigned char> g_game;    // the game's map at the stage
static std::vector<unsigned char> g_copied;  // SCENE_SCREEN: the pixels the screen takes

// The tileset.
static unsigned int g_nTileset;
static unsigned int g_nMegatiles;
static unsigned int g_nCells;

// The atlas and the table store.
static unsigned int g_nAtlasSerial;
static unsigned int g_nStateSerial;  // PortSceneStateSerial
static std::vector<SceneEntry> g_entries;
static std::vector<unsigned short> g_pixels;
struct SceneFrameKey {
  unsigned long long nPrint;  // the frame's size and first codes
  unsigned int nEntry;
};
static std::unordered_map<const FRAME *, SceneFrameKey> g_frameEntry;
static std::unordered_map<const unsigned char *, unsigned int> g_glyphEntry;
static std::vector<unsigned char> g_tables;
static std::unordered_multimap<unsigned long long, unsigned int> g_tableAt;  // by hash

// The pass's primitives and their squares.
static std::vector<ScenePrim> g_prims;
static std::vector<unsigned int> g_primArt;  // beside g_prims: the image and frame, or SCENE_NONE
static unsigned int g_nArt = SCENE_NONE;     // what CSprite::Draw is drawing now
static std::vector<unsigned int> g_primGrp;  // beside g_prims: its GRP's largest frame, or 0
static unsigned int g_nArtGrp;
static std::vector<unsigned char> g_primColor;  // beside g_prims: the colour slot it drew with
static std::vector<unsigned short> g_primLook;  // and the look of the image it drew
static bool g_bPassLooked;                      // a look's light or plume is in the pass
static std::vector<unsigned int> g_binStart;
static std::vector<unsigned int> g_binList;
static std::vector<unsigned int> g_binCount;
static std::vector<unsigned char> g_bytes;

int PortSceneStage(void)
{
  if (g_nStage < 0) {
    char szStage[32];
    const DWORD n = GetEnvironmentVariableA("HYPERION_GPU_SCENE", szStage, sizeof szStage);
    g_nStage = SCENE_OFF;
    if (n != 0 && n < sizeof szStage) {
      if (lstrcmpiA(szStage, "terrain") == 0)
        g_nStage = SCENE_TERRAIN;
      else if (lstrcmpiA(szStage, "sprites") == 0)
        g_nStage = SCENE_SPRITES;
      else if (lstrcmpiA(szStage, "map") == 0)
        g_nStage = SCENE_MAP;
      else if (lstrcmpiA(szStage, "screen") == 0)
        g_nStage = SCENE_SCREEN;
      else if (lstrcmpiA(szStage, "gpu") == 0) {
        g_nStage = SCENE_SCREEN;
        g_bOnly = true;
      }
    }
  }
  return g_nStage;
}

// With HYPERION_GPU_SCENE=gpu and the GPU presenter in use, the GPU alone draws what
// the description holds: each routine that reports a call skips its own drawing when
// told so, the scroll ring is not drawn, and the screen buffer's map is not the
// frame's.
static bool SceneGpuOnly(void)
{
  return g_bOnly && PortPresentGpu();
}

// Without HYPERION_GPU_SCENE the GPU presenter starts the GPU scene as "gpu" does, the
// GPU alone drawing the map, which HD art and the lights need; HYPERION_GPU_SCENE=off
// (or any name not a stage) keeps the software drawing it.
void PortSceneDefault(void)
{
  if (GetEnvironmentVariableA("HYPERION_GPU_SCENE", NULL, 0) != 0)
    return;
  g_nStage = SCENE_SCREEN;
  g_bOnly = true;
}

void PortSceneSoftware(bool bSoftware)
{
  if (GetEnvironmentVariableA("HYPERION_GPU_SCENE", NULL, 0) != 0)
    return;
  g_nStage = bSoftware ? SCENE_OFF : SCENE_SCREEN;
  g_bOnly = !bSoftware;
}

int PortSceneGpuOnly(void)
{
  PortSceneStage();
  return SceneGpuOnly() ? 1 : 0;
}

int PortSceneSkipDraw(void)
{
  return g_bRecording && SceneGpuOnly() ? 1 : 0;
}

// A frame's pixels into the atlas, once; its entry. The codes are rle_creep's
// (port_draw.cpp): each line a run of codes until the width is used up, 0x80 and up
// skipping, 0x40 and up repeating the next byte, anything lower copying.
static unsigned int SceneFrameEntry(const FRAME *pFrame)
{
  const int nWidth = pFrame->bWidth;
  const int nHeight = pFrame->bHeight;
  const BYTE *const pFirst = pFrame->pData;
  unsigned long long nPrint = (reinterpret_cast<unsigned long long>(pFirst) * 0x9E3779B97F4A7C15ull) ^
                              (static_cast<unsigned long long>(nWidth) << 8) ^ static_cast<unsigned long long>(nHeight);
  // The codes of the first line, which follow the table of each line's offset.
  if (nHeight > 0) {
    const BYTE *pCode = GrpFrameRow(pFrame, 0);
    for (int x = 0; x < nWidth;) {
      const int c = *pCode++;
      nPrint = (nPrint ^ c) * 0x100000001B3ull;
      if (c & 0x80) {
        x += c & 0x7F;
      } else if (c & 0x40) {
        nPrint = (nPrint ^ *pCode++) * 0x100000001B3ull;
        x += c & 0x3F;
      } else {
        for (int i = 0; i < c; ++i)
          nPrint = (nPrint ^ *pCode++) * 0x100000001B3ull;
        x += c;
      }
      if (c == 0)
        break;
    }
  }
  std::unordered_map<const FRAME *, SceneFrameKey>::const_iterator it = g_frameEntry.find(pFrame);
  if (it != g_frameEntry.end() && it->second.nPrint == nPrint)
    return it->second.nEntry;

  SceneEntry entry;
  entry.nOffset = static_cast<unsigned int>(g_pixels.size());
  entry.nSize = static_cast<unsigned int>(nWidth) | (static_cast<unsigned int>(nHeight) << 16);
  entry.nOrigin = static_cast<unsigned int>(pFrame->bOffsetX) |
                  (static_cast<unsigned int>(pFrame->_unmapped_01) << 16);
  entry.nMax = 0;
  // Kept even, so the atlas stays whole words.
  g_pixels.resize(g_pixels.size() + ((nWidth * nHeight + 1) & ~1), 0);

  unsigned short *pOut = g_pixels.data() + entry.nOffset;
  for (int y = 0; y < nHeight; ++y) {
    const BYTE *pCode = GrpFrameRow(pFrame, y);
    unsigned short *pRow = pOut + y * nWidth;
    int x = 0;
    while (x < nWidth) {
      int n = *pCode++;
      if (n & 0x80) {
        x += n & 0x7F;
        continue;
      }
      if (n & 0x40) {
        const unsigned short v = static_cast<unsigned short>(0x100 | *pCode++);
        n &= 0x3F;
        for (; n > 0 && x < nWidth; --n)
          pRow[x++] = v;
      } else {
        for (; n > 0; --n, ++pCode)
          if (x < nWidth)
            pRow[x++] = static_cast<unsigned short>(0x100 | *pCode);
      }
    }
  }

  for (int i = 0; i < nWidth * nHeight; ++i)
    if ((pOut[i] & 0x100) != 0 && (pOut[i] & 0xFF) > entry.nMax)
      entry.nMax = pOut[i] & 0xFF;
  const unsigned int nIndex = static_cast<unsigned int>(g_entries.size());
  g_entries.push_back(entry);
  SceneFrameKey key = {nPrint, nIndex};
  g_frameEntry[pFrame] = key;
  return nIndex;
}

// The tables a pass has put in the store, by where the game keeps them and the colour
// slot drawn with: a large game's pass draws thousands of frames through a few
// hundred tables, and hashing each table at each frame cost more than the rest of its
// primitive. The player's remap is one table the game fills again for each colour, so the
// slot is part of the key. A table found here is compared with the store's copy before it
// is taken, so one the game changed is not.
static unsigned int SceneTableStore(const unsigned char *pBytes, unsigned int nBytes);
struct SceneTableSeen {
  const void *pTable;
  unsigned int nBytes, nColor, nAt, nPass;
};
static SceneTableSeen g_tableSeen[1024];
static unsigned int g_nTablePass = 1;  // PortSceneMapBegin's count

// A table's bytes into the store, once; where they are. Found again by where the pass
// last saw them, or by a hash of the bytes, and then the bytes themselves.
static unsigned int SceneTable(const void *pTable, unsigned int nBytes)
{
  const unsigned char *pBytes = static_cast<const unsigned char *>(pTable);
  const unsigned int nColor = static_cast<unsigned int>(sgdwCurrentDrawColor);
  SceneTableSeen &seen =
      g_tableSeen[(reinterpret_cast<uintptr_t>(pTable) >> 4 ^ nBytes ^ nColor * 0x9E3779B1u >> 22) & 1023];
  if (seen.pTable == pTable && seen.nBytes == nBytes && seen.nColor == nColor && seen.nPass == g_nTablePass &&
      seen.nAt + nBytes <= g_tables.size() && memcmp(&g_tables[seen.nAt], pBytes, nBytes) == 0)
    return seen.nAt;
  seen.pTable = pTable;
  seen.nBytes = nBytes;
  seen.nColor = nColor;
  seen.nPass = g_nTablePass;
  seen.nAt = SceneTableStore(pBytes, nBytes);
  return seen.nAt;
}

static unsigned int SceneTableStore(const unsigned char *pBytes, unsigned int nBytes)
{
  unsigned long long h = 0x9E3779B97F4A7C15ull ^ nBytes;
  unsigned int i;

  for (i = 0; i + 8 <= nBytes; i += 8) {
    unsigned long long w;
    memcpy(&w, pBytes + i, 8);
    h = (h ^ w) * 0xFF51AFD7ED558CCDull;
    h ^= h >> 29;
  }
  for (; i < nBytes; ++i)
    h = (h ^ pBytes[i]) * 0x100000001B3ull;

  typedef std::unordered_multimap<unsigned long long, unsigned int>::const_iterator It;
  std::pair<It, It> range = g_tableAt.equal_range(h);
  for (It it = range.first; it != range.second; ++it) {
    if (it->second + nBytes <= g_tables.size() && memcmp(&g_tables[it->second], pBytes, nBytes) == 0)
      return it->second;
  }
  const unsigned int nAt = static_cast<unsigned int>(g_tables.size());
  g_tables.insert(g_tables.end(), pBytes, pBytes + nBytes);
  g_tables.resize((g_tables.size() + 15) & ~static_cast<size_t>(15), 0);
  g_tableAt.insert(std::make_pair(h, nAt));
  return nAt;
}

// gamemap_init, once the tileset is in: how many megatiles and cells it has, which
// the game never keeps, from the highest ids its tile groups and megatiles name.
void PortSceneMapLoaded(void)
{
  unsigned int nMaxMtx = 0;
  unsigned int nMaxCell = 0;
  unsigned int i;

  if (PortSceneStage() == SCENE_OFF)
    return;
  for (i = 0; i < gdwMIEntries * CV5_GROUP_WORDS; ++i) {
    if (i % CV5_GROUP_WORDS >= CV5_FIRST_MTX && gpMtxInfo[i] > nMaxMtx)
      nMaxMtx = gpMtxInfo[i];
  }
  g_nMegatiles = nMaxMtx + 1;
  // a tileset's groups can name a megatile past the .vx4's end (a mod's, a damaged archive)
  const unsigned int nMtxHeld = static_cast<unsigned int>(PortMemSize(gfpMtxSet) / (MTX_CELLS * sizeof(DWORD)));
  if (g_nMegatiles > nMtxHeld)
    g_nMegatiles = nMtxHeld;
  const DWORD *pMtx = reinterpret_cast<const DWORD *>(gfpMtxSet);
  for (i = 0; i < g_nMegatiles * MTX_CELLS; ++i) {
    if ((pMtx[i] >> 1) > nMaxCell)
      nMaxCell = pMtx[i] >> 1;
  }
  g_nCells = nMaxCell + 1;
  const unsigned int nCellsHeld = static_cast<unsigned int>(PortMemSize(gfpCellBuf) / 64);  // 8x8 a cell
  if (g_nCells > nCellsHeld)
    g_nCells = nCellsHeld;
  ++g_nTileset;

  ++g_nAtlasSerial;
  ++g_nStateSerial;
  g_entries.clear();
  g_pixels.clear();
  g_frameEntry.clear();
  g_glyphEntry.clear();
  g_tables.clear();
  g_tableAt.clear();
  for (SceneSlot &slot : g_slots) {
    slot.nMtx = 0;
    slot.nTile = SCENE_NONE;
    slot.nEdge = SCENE_NONE;
    slot.nEdgeFrame = 0;
  }
  g_aSlotsDirty[0] = {0, SIZE_MAX};
  g_bReady = false;
}

// A tile's place in the ring across and down, x % the ring's tiles and y % them, looked up
// rather than divided: a pass zoomed out over the map fills tens of thousands of
// slots. Made again when the ring's size changes, across or down: a row's place is its row
// times the ring's width, so a ring as tall but narrower or wider needs it too.
static std::vector<int> g_ringColumn, g_ringRow;
static int g_nRingColumnsOf, g_nRingRowsOf;

static void RingTables(int nRingX, int nRingY)
{
  if (nRingX == g_nRingColumnsOf && nRingY == g_nRingRowsOf)
    return;
  g_nRingColumnsOf = nRingX;
  g_nRingRowsOf = nRingY;
  g_ringColumn.resize(4096);  // past it, divided
  g_ringRow.resize(4096);
  for (size_t i = 0; i < g_ringColumn.size(); ++i)
    g_ringColumn[i] = static_cast<int>(i % nRingX);
  for (size_t i = 0; i < g_ringRow.size(); ++i)
    g_ringRow[i] = static_cast<int>(i % nRingY) * nRingX;
}

unsigned int PortSceneStateSerial(void)
{
  return g_nStateSerial;
}

void PortSceneStateRestored(void)
{
  ++g_nStateSerial;
}

struct SceneCellsKey {
  unsigned int nFrame, nState;
  int anView[7];
  DWORD adwMasks[4];
  const void *apMaps[3];
};
static SceneCellsKey g_cellsKey;
static unsigned int g_nCellReports;

int PortSceneCellsKept(void)
{
  SceneCellsKey key;
  memset(&key, 0, sizeof key);
  key.nFrame = oc_dwReplayFrame;
  key.nState = g_nStateSerial;
  key.anView[0] = gwScreenTileX;
  key.anView[1] = gwScreenTileY;
  key.anView[2] = g_portView.nRingTilesX;
  key.anView[3] = g_portView.nRingTilesY;
  key.anView[4] = g_portView.nRingPitch;
  key.anView[5] = gwMapWidth;
  key.anView[6] = gwMapHeight;
  key.adwMasks[0] = gPlayback;
  key.adwMasks[1] = sgbShowAll_g;
  key.adwMasks[2] = gdwReplayVisionMask;
  key.adwMasks[3] = gdwLocalMask;
  key.apMaps[0] = gpSquareMap;
  key.apMaps[1] = gfpMIMap;
  key.apMaps[2] = gfpCellMap;
  if (memcmp(&key, &g_cellsKey, sizeof key) == 0)
    return 1;
  memcpy(&g_cellsKey, &key, sizeof key);
  return 0;
}

unsigned int PortSceneCellReports(void)
{
  return g_nCellReports;
}

bool PortScenePassKeepCheck(void)
{
  static const bool s_bOn = getenv("HYPERION_PASS_KEEP_CHECK") != NULL;
  return s_bOn;
}

void PortScenePassKeepDiffer(const char *pszWhat, unsigned int nCount)
{
  static unsigned long s_nChecked, s_nDiffer;
  if (nCount == 0) {
    if ((++s_nChecked & 4095) != 0)
      return;
  } else if (++s_nDiffer > 1000) {
    return;
  }
  if (FILE *f = fopen("hyperion_pass_keep.log", "a")) {
    if (nCount == 0)
      fprintf(f, "frame %u: %lu kept and checked, %lu differ\n", oc_dwReplayFrame, s_nChecked, s_nDiffer);
    else
      fprintf(f, "frame %u: %s differs, %u\n", oc_dwReplayFrame, pszWhat, nCount);
    fclose(f);
  }
}

static SceneSlot *SceneSlotFor(int nX, int nY)
{
  const int nRingX = g_portView.nRingTilesX;
  const int nRingY = g_portView.nRingTilesY;
  if (nX < 0 || nY < 0)
    return NULL;
  std::vector<SceneSlot> &slots = g_bRenderSlots ? g_renderSlots : g_slots;
  const size_t nNeed = static_cast<size_t>(nRingX) * nRingY;
  if (slots.size() < nNeed) {
    slots.resize(nNeed, g_slotEmpty);
    g_aSlotsDirty[g_bRenderSlots ? 1 : 0] = {0, SIZE_MAX};
  }
  RingTables(nRingX, nRingY);
  const int nColumn = static_cast<size_t>(nX) < g_ringColumn.size() ? g_ringColumn[nX] : nX % nRingX;
  const int nRow = static_cast<size_t>(nY) < g_ringRow.size() ? g_ringRow[nY] : nY % nRingY * nRingX;
  // written by the caller: the GPU takes it with the frame
  SlotsDirty &dirty = g_aSlotsDirty[g_bRenderSlots ? 1 : 0];
  const size_t i = static_cast<size_t>(nRow + nColumn);
  if (i < dirty.nFirst)
    dirty.nFirst = i;
  if (i + 1 > dirty.nEnd)
    dirty.nEnd = i + 1;
  return &slots[i];
}

// cell_draw_mtx: megatile wMtx drawn into the ring for tile nX, nY. Non-zero when the
// ring need not be drawn.
int PortSceneCell(unsigned int wMtx, int nX, int nY)
{
  ++g_nCellReports;
  if (g_nStage <= SCENE_OFF)
    return 0;
  if (SceneSlot *pSlot = SceneSlotFor(nX, nY)) {
    pSlot->nMtx = wMtx;
    pSlot->nTile = (static_cast<unsigned int>(nX) & 0xFFFF) | (static_cast<unsigned int>(nY) << 16);
    pSlot->nEdge = SCENE_NONE;
    pSlot->nEdgeFrame = 0;
    g_bRingTouched = g_bRingTouched || g_bRenderSlots;
    return SceneGpuOnly();
  }
  return 0;
}

// cell_draw_creep: the creep edge frame drawn over it, frame nFrame of the edge GRP.
int PortSceneCreep(int nX, int nY, const void *pFrame, int nFrame)
{
  if (g_nStage <= SCENE_OFF)
    return 0;
  if (SceneSlot *pSlot = SceneSlotFor(nX, nY)) {
    const FRAME *pEdge = static_cast<const FRAME *>(pFrame);
    pSlot->nEdge = SceneFrameEntry(pEdge);
    pSlot->nEdgeFrame = (static_cast<unsigned int>(nFrame) & 0xFF) | (static_cast<unsigned int>(pEdge->bOffsetX) << 8) |
                        (static_cast<unsigned int>(pEdge->_unmapped_01 & 0xFF) << 16) | 0x80000000u;
    return SceneGpuOnly();
  }
  return 0;
}

// Whether a drawing routine's call is part of the pass: during it, and into the
// screen buffer.
static bool SceneRecording(void)
{
  return g_bRecording && _bmp_current == &_img_draw_buf;
}

// The sprite routines (port_draw.cpp): a frame drawn at nX, nY with its clip, through
// the table they read, as it is now (nTableBytes of it; none for a distortion's
// brightness when it has none).
int PortSceneRle(unsigned int nKind, int nX, int nY, const void *pFrame, const void *pClip,
                  const void *pTable, unsigned int nTableBytes, unsigned int nParam)
{
  if (!SceneRecording())
    return 0;
  const int *pnClip = static_cast<const int *>(pClip);  // left, top, width, height
  ScenePrim prim;
  prim.nKind = nKind;
  prim.nX = nX;
  prim.nY = nY;
  prim.nEntry = SceneFrameEntry(static_cast<const FRAME *>(pFrame));
  // A translucency table is read only in the rows of the frame's colours, and some
  // hold no more rows than that.
  if ((nKind & 0xFF) == SCENE_PRIM_EFFECT)
    nTableBytes = (g_entries[prim.nEntry].nMax + 1) * 256;
  prim.nClip0 = (static_cast<unsigned int>(pnClip[0]) & 0xFFFF) | (static_cast<unsigned int>(pnClip[1]) << 16);
  prim.nClip1 = (static_cast<unsigned int>(pnClip[2]) & 0xFFFF) | (static_cast<unsigned int>(pnClip[3]) << 16);
  prim.nTable = pTable != NULL ? SceneTable(pTable, nTableBytes) : SCENE_NONE;
  prim.nParam = nParam;
  g_prims.push_back(prim);
  // the image it is part of, beside it; the primitives of no image take none
  g_primArt.resize(g_prims.size() - 1, SCENE_NONE);
  g_primArt.push_back(g_nArt);
  g_primGrp.resize(g_prims.size() - 1, 0);
  g_primGrp.push_back(g_nArt != SCENE_NONE ? g_nArtGrp : 0);
  g_primColor.resize(g_prims.size() - 1, 0);
  g_primColor.push_back(static_cast<unsigned char>(sgdwCurrentDrawColor));
  const unsigned int nLook = PortLookNow();
  if (nLook != 0 || !g_primLook.empty()) {
    g_primLook.resize(g_prims.size() - 1, 0);
    g_primLook.push_back(static_cast<unsigned short>(nLook));
  }
  // a crystal's halo past its edge: a look primitive round the image, reading its frame
  const int nReach = (nKind & 0xFF) == SCENE_PRIM_NORMAL && nLook != 0 ? PortLookHalo(nLook) : 0;
  if (nReach > 0) {
    ScenePrim halo = prim;
    halo.nKind = SCENE_PRIM_LOOK | (nKind & SCENE_PRIM_MIRROR);
    halo.nX -= nReach;
    halo.nY -= nReach;
    halo.nClip1 = (static_cast<unsigned int>(pnClip[2] + 2 * nReach) & 0xFFFF) |
                  (static_cast<unsigned int>(pnClip[3] + 2 * nReach) << 16);
    halo.nTable = SCENE_NONE;
    halo.nParam = (nLook & 0xFF) | (PORT_LOOK_HALO << 8) | (static_cast<unsigned int>(nReach) << 16);
    g_prims.push_back(halo);
    g_bPassLooked = true;
  }
  return SceneGpuOnly();
}

void PortSceneImage(int nImage, int nFrame, int nGrpWidth, int nGrpHeight)
{
  g_nArt = nImage < 0 ? SCENE_NONE
                      : (static_cast<unsigned int>(nImage) & 0xFFFF) | (static_cast<unsigned int>(nFrame) << 16);
  g_nArtGrp = (static_cast<unsigned int>(nGrpWidth) & 0xFFFF) | (static_cast<unsigned int>(nGrpHeight) << 16);
}

// The uncloaked part of a cloaking unit: the remap and the brightness table both.
int PortSceneRleUncloaked(int nX, int nY, const void *pFrame, const void *pClip, const void *pRemap,
                          const void *pBrightness, int nThreshold, int bMirror)
{
  if (!SceneRecording())
    return 0;
  const unsigned int nBright = SceneTable(pBrightness, 256);
  return PortSceneRle(SCENE_PRIM_UNCLOAKED | (bMirror ? SCENE_PRIM_MIRROR : 0), nX, nY, pFrame, pClip, pRemap,
                      256, (static_cast<unsigned int>(nThreshold) & 0xFF) | (nBright << 8));
}

// bmp_hline and bmp_vline: nCount pixels of the pen's colour from pFirst, nStride
// apart.
int PortSceneSpan(const void *pFirst, unsigned int nCount, unsigned int nStride, unsigned int nColor)
{
  if (!SceneRecording() || nCount == 0)
    return 0;
  ScenePrim prim;
  prim.nKind = SCENE_PRIM_SPAN;
  prim.nX = static_cast<int>(static_cast<const BYTE *>(pFirst) - _img_draw_buf.pPixels);
  prim.nY = static_cast<int>(nCount);
  prim.nEntry = SCENE_NONE;
  prim.nClip0 = nStride;
  prim.nClip1 = 0;
  prim.nTable = nColor & 0xFF;
  prim.nParam = 0;
  g_prims.push_back(prim);
  return SceneGpuOnly();
}

// StarBlit: a star's nW x nH pixels at nX, nY, drawn where the map is 0.
int PortSceneStar(const void *pPixels, int nX, int nY, int nW, int nH)
{
  if (!SceneRecording())
    return 0;
  ScenePrim prim;
  prim.nKind = SCENE_PRIM_STAR;
  prim.nX = nX;
  prim.nY = nY;
  prim.nEntry = SCENE_NONE;
  prim.nClip0 = 0;
  prim.nClip1 = (static_cast<unsigned int>(nW) & 0xFFFF) | (static_cast<unsigned int>(nH) << 16);
  prim.nTable = SceneTable(pPixels, static_cast<unsigned int>(nW * nH));
  prim.nParam = 0;
  g_prims.push_back(prim);
  return SceneGpuOnly();
}

void PortSceneLook(int nX, int nY, int nW, int nH, unsigned int nCentre, unsigned int nNumbers, unsigned int nParam)
{
  if (!SceneRecording() || nW <= 0 || nH <= 0)
    return;
  ScenePrim prim;
  prim.nKind = SCENE_PRIM_LOOK;
  prim.nX = nX;
  prim.nY = nY;
  prim.nEntry = nNumbers;
  prim.nClip0 = nCentre;
  prim.nClip1 = (static_cast<unsigned int>(nW) & 0xFFFF) | (static_cast<unsigned int>(nH) << 16);
  prim.nTable = SCENE_NONE;
  prim.nParam = nParam;
  g_prims.push_back(prim);
  g_bPassLooked = true;
}

// The GPU makes the fog's light map: CSFogLight there, and the pass's fog
// primitive and grid, placed when the pass ends (SceneFogLightPlace).
static bool g_bFogGpu;
struct SceneFogLight {
  bool bOn;
  unsigned int nPrim, nFirst, nBytes, nGridAt, nGrid, nRampAt, nRingX, nRingY, nPitch;
};
static SceneFogLight g_fogLight;

void PortSceneFogGpu(bool bCan)
{
  g_bFogGpu = bCan;
}

int PortSceneFogOnGpu(void)
{
  return g_bRecording && g_bFogGpu && SceneGpuOnly() ? 1 : 0;
}

// The light map's place: past every byte the pass put in, where nothing is uploaded and the
// GPU writes it, and the fog primitive pointed at it.
static void SceneFogLightPlace(void)
{
  g_frame.nFogLightAt = 0;
  g_frame.nFogLightBytes = 0;
  if (!g_fogLight.bOn || g_fogLight.nPrim >= g_prims.size())
    return;
  const unsigned int nAt = (static_cast<unsigned int>(g_bytes.size()) + 15u) & ~15u;
  g_prims[g_fogLight.nPrim].nX = static_cast<int>(nAt + g_fogLight.nFirst);
  g_frame.nFogLightAt = nAt;
  g_frame.nFogLightBytes = (g_fogLight.nBytes + 15u) & ~15u;
  g_frame.nFogGridAt = g_fogLight.nGridAt;
  g_frame.nFogGrid = g_fogLight.nGrid;
  g_frame.nFogRampAt = g_fogLight.nRampAt;
  g_frame.nFogRingX = g_fogLight.nRingX;
  g_frame.nFogRingY = g_fogLight.nRingY;
  g_frame.nFogPitch = g_fogLight.nPitch;
}

// mask_draw, over the whole view: the light map as it is (nBytes of it, nPitch to a
// row, the view's first block at nFirst) and the 32 shades of darkness. When the GPU makes
// the light map, the grid and the ramp go in its place, a fifteenth of its bytes.
int PortSceneFog(const void *pLight, int nPitch, int nBytes, int nFirst, const void *pShades, const void *pGrid,
                 int nGrid, int nRingX, int nRingY, const void *pRamp)
{
  if (!SceneRecording())
    return 0;
  ScenePrim prim;
  const size_t nAt = g_bytes.size();
  g_fogLight.bOn = g_bFogGpu && pGrid != NULL && pRamp != NULL;
  if (g_fogLight.bOn) {
    const size_t nGridBytes = static_cast<size_t>(nGrid) * (nRingY + 3);
    g_fogLight.nPrim = static_cast<unsigned int>(g_prims.size());
    g_fogLight.nFirst = static_cast<unsigned int>(nFirst);
    g_fogLight.nBytes = static_cast<unsigned int>(nBytes);
    g_fogLight.nGrid = static_cast<unsigned int>(nGrid);
    g_fogLight.nRingX = static_cast<unsigned int>(nRingX);
    g_fogLight.nRingY = static_cast<unsigned int>(nRingY);
    g_fogLight.nPitch = static_cast<unsigned int>(nPitch);
    g_fogLight.nRampAt = static_cast<unsigned int>(nAt);
    g_bytes.insert(g_bytes.end(), static_cast<const unsigned char *>(pRamp),
                   static_cast<const unsigned char *>(pRamp) + 32 * 32 * 4);
    g_fogLight.nGridAt = static_cast<unsigned int>(g_bytes.size());
    g_bytes.insert(g_bytes.end(), static_cast<const unsigned char *>(pGrid),
                   static_cast<const unsigned char *>(pGrid) + nGridBytes);
  } else {
    g_bytes.insert(g_bytes.end(), static_cast<const unsigned char *>(pLight),
                   static_cast<const unsigned char *>(pLight) + nBytes);
  }
  g_bytes.resize((g_bytes.size() + 15) & ~static_cast<size_t>(15), 0);
  prim.nKind = SCENE_PRIM_FOG;
  prim.nX = static_cast<int>(nAt) + nFirst;  // the GPU's: placed at the pass's end
  prim.nY = nPitch;
  prim.nEntry = SCENE_NONE;
  prim.nClip0 = 0;
  prim.nClip1 = 0;
  prim.nTable = SceneTable(pShades, 32 * 256);
  prim.nParam = 0;
  g_prims.push_back(prim);
  return SceneGpuOnly();
}

// A glyph's colour numbers into the atlas, once. draw_char's codes (port_draw.cpp):
// each byte a skip in its top 5 bits, on into the next rows, and a colour in its low
// 3, drawn when it is not 0 after a skip and always without one.
static unsigned int SceneGlyphEntry(const BYTE *pBits, int nWidth, int nHeight)
{
  const BYTE *const pGlyph = pBits;  // the key: the loop below moves pBits past the glyph
  std::unordered_map<const unsigned char *, unsigned int>::const_iterator it = g_glyphEntry.find(pGlyph);
  if (it != g_glyphEntry.end())
    return it->second;

  SceneEntry entry;
  entry.nOffset = static_cast<unsigned int>(g_pixels.size());
  entry.nSize = static_cast<unsigned int>(nWidth) | (static_cast<unsigned int>(nHeight) << 16);
  entry.nOrigin = 0;
  entry.nMax = 7;
  g_pixels.resize(g_pixels.size() + ((nWidth * nHeight + 1) & ~1), 0);
  unsigned short *pOut = &g_pixels[entry.nOffset];
  int nRow = 0;
  int nCol = 0;
  int nLeft = nWidth;
  for (;;) {
    const int c = *pBits++;
    int nSkip = c >> 3;
    const int nColor = c & 7;
    if (nSkip != 0) {
      while (nSkip >= nLeft) {
        nSkip -= nLeft;
        nLeft = nWidth;
        nCol = 0;
        if (++nRow == nHeight)
          goto done;
      }
      nCol += nSkip;
      nLeft -= nSkip;
      if (nColor == 0)
        continue;
    }
    pOut[nRow * nWidth + nCol] = static_cast<unsigned short>(0x100 | nColor);
    ++nCol;
    if (--nLeft == 0) {
      nLeft = nWidth;
      nCol = 0;
      if (++nRow == nHeight)
        break;
    }
  }
done:
  const unsigned int nIndex = static_cast<unsigned int>(g_entries.size());
  g_entries.push_back(entry);
  g_glyphEntry[pGlyph] = nIndex;
  return nIndex;
}

// draw_char: a glyph nWidth x nHeight at pDest, its rows nRowMod apart past its
// width, in the 8 colours at pColors.
int PortSceneGlyph(const void *pDest, const void *pBits, int nWidth, int nHeight, int nRowMod,
                    const void *pColors)
{
  if (!SceneRecording() || nWidth <= 0 || nHeight <= 0)
    return 0;
  const int nPitch = static_cast<WORD>(_img_draw_buf.nWidth);
  const long long nAt = static_cast<const BYTE *>(pDest) - _img_draw_buf.pPixels;
  if (nWidth + nRowMod != nPitch || nAt < 0)
    return 0;
  ScenePrim prim;
  prim.nKind = SCENE_PRIM_GLYPH;
  prim.nX = static_cast<int>(nAt % nPitch);
  prim.nY = static_cast<int>(nAt / nPitch);
  prim.nEntry = SceneGlyphEntry(static_cast<const BYTE *>(pBits), nWidth, nHeight);
  prim.nClip0 = 0;
  prim.nClip1 = (static_cast<unsigned int>(nWidth) & 0xFFFF) | (static_cast<unsigned int>(nHeight) << 16);
  prim.nTable = SceneTable(pColors, 8);
  prim.nParam = 0;
  g_prims.push_back(prim);
  return SceneGpuOnly();
}

// A bitmap into the screen buffer, clipped: nWidth x nHeight pixels from pSrc, rows
// nSrcPitch apart, to pDst, rows nDstPitch apart, as nMode says (SCENE_PRIM_BLIT).
// PortBltRop3 copies (bmp_copy_rect, the dialogs), bmp_mask_rect skips 0s, and the
// dialogs' translucency and shading go through a table (PortTableBlit).
int PortSceneBlit(const void *pDst, int nDstPitch, const void *pSrc, int nSrcPitch, int nWidth, int nHeight,
                   const void *pTable, int nMode)
{
  if (!SceneRecording() || nWidth <= 0 || nHeight <= 0)
    return 0;
  const int nPitch = static_cast<WORD>(_img_draw_buf.nWidth);
  const long long nSize = static_cast<long long>(nPitch) * static_cast<WORD>(_img_draw_buf.nHeight);
  const long long nAt = static_cast<const BYTE *>(pDst) - _img_draw_buf.pPixels;
  if (nAt < 0 || nAt >= nSize || nDstPitch != nPitch || nAt % nPitch + nWidth > nPitch)
    return 0;
  ScenePrim prim;
  const size_t nBytesAt = g_bytes.size();
  for (int y = 0; y < nHeight; ++y) {
    const BYTE *pRow = static_cast<const BYTE *>(pSrc) + static_cast<size_t>(y) * nSrcPitch;
    g_bytes.insert(g_bytes.end(), pRow, pRow + nWidth);
  }
  g_bytes.resize((g_bytes.size() + 15) & ~static_cast<size_t>(15), 0);
  prim.nKind = SCENE_PRIM_BLIT;
  prim.nX = static_cast<int>(nAt % nPitch);
  prim.nY = static_cast<int>(nAt / nPitch);
  prim.nEntry = pTable != NULL ? SceneTable(pTable, 0x10000) : SCENE_NONE;
  prim.nClip0 = 0;
  prim.nClip1 = (static_cast<unsigned int>(nWidth) & 0xFFFF) | (static_cast<unsigned int>(nHeight) << 16);
  prim.nTable = static_cast<unsigned int>(nBytesAt);
  prim.nParam = static_cast<unsigned int>(nMode);
  g_prims.push_back(prim);
  return SceneGpuOnly();
}

int PortSceneMapBegin(void)
{
  if (PortSceneStage() == SCENE_OFF || !PortPresentGpu() || g_nMegatiles == 0)
    return 0;
  // A pass described and never taken does not reach the GPU: what it copied is the
  // screen's own.
  if (g_bReady && g_frame.pCopied != NULL)
    PortScreenCoverCopied(g_frame.pCopied, 0, 0);
  QueryPerformanceCounter(&g_passStart);
  g_bRecording = true;
  g_bReady = false;
  g_bZoomPass = false;
  ++g_nTablePass;
  g_fogLight.bOn = false;
  g_prims.clear();
  g_primArt.clear();
  g_primGrp.clear();
  g_primColor.clear();
  g_primLook.clear();
  g_bPassLooked = false;
  g_bytes.clear();
  return 1;
}

// The squares of the map a primitive reaches, as a rectangle of pixels, inclusive;
// false when it reaches none, or is a distortion.
static bool ScenePrimBox(const ScenePrim &prim, int nWidth, int *pnLeft, int *pnTop, int *pnRight, int *pnBottom)
{
  const unsigned int nKind = prim.nKind & 0xFF;
  if (nKind == SCENE_PRIM_DISTORT)
    return false;
  if (nKind == SCENE_PRIM_FOG) {
    *pnLeft = 0;
    *pnTop = 0;
    *pnRight = nWidth - 1;
    *pnBottom = 0x7FFFFFFF;
    return true;
  }
  if (nKind == SCENE_PRIM_SPAN) {
    const long long nFirst = prim.nX;
    const long long nLast = nFirst + static_cast<long long>(prim.nY - 1) * prim.nClip0;
    if (nLast < 0)
      return false;
    const long long nFrom = nFirst < 0 ? 0 : nFirst;
    *pnTop = static_cast<int>(nFrom / nWidth);
    *pnBottom = static_cast<int>(nLast / nWidth);
    if (*pnTop == *pnBottom) {
      *pnLeft = static_cast<int>(nFrom % nWidth);
      *pnRight = static_cast<int>(nLast % nWidth);
    } else if (prim.nClip0 == static_cast<unsigned int>(nWidth)) {
      *pnLeft = *pnRight = static_cast<int>(nFrom % nWidth);
    } else {
      *pnLeft = 0;
      *pnRight = nWidth - 1;
    }
    return true;
  }
  *pnLeft = prim.nX;
  *pnTop = prim.nY;
  *pnRight = prim.nX + static_cast<int>(prim.nClip1 & 0xFFFF) - 1;
  *pnBottom = prim.nY + static_cast<int>(prim.nClip1 >> 16) - 1;
  return *pnRight >= *pnLeft && *pnBottom >= *pnTop;
}

bool PortSceneRunBox(const ScenePrim *pPrims, unsigned int nFirst, unsigned int nLast, int nWidth, int nRows,
                     int *pnLeft, int *pnTop, int *pnRight, int *pnBottom)
{
  int nL = nWidth, nT = nRows, nR = -1, nB = -1;
  for (unsigned int i = nFirst; i < nLast; ++i) {
    int nLeft, nTop, nRight, nBottom;
    if (!ScenePrimBox(pPrims[i], nWidth, &nLeft, &nTop, &nRight, &nBottom))
      continue;
    if (nRight < 0 || nBottom < 0 || nLeft >= nWidth || nTop >= nRows)
      continue;
    nL = nLeft < nL ? (nLeft < 0 ? 0 : nLeft) : nL;
    nT = nTop < nT ? (nTop < 0 ? 0 : nTop) : nT;
    nR = nRight > nR ? (nRight >= nWidth ? nWidth - 1 : nRight) : nR;
    nB = nBottom > nB ? (nBottom >= nRows ? nRows - 1 : nBottom) : nB;
  }
  if (nR < nL || nB < nT)
    return false;
  *pnLeft = nL;
  *pnTop = nT;
  *pnRight = nR;
  *pnBottom = nB;
  return true;
}

// Each square's primitives, in order: those from nFirst up to nLast, over nWidth x nRows; the
// squares SCENE_BIN pixels of a layer nLayerW by nLayerRows with nStep of nWidth's to a pixel.
static void SceneBin(unsigned int nFirst, unsigned int nLast, int nWidth, int nRows, int nStep, int nLayerW,
                     int nLayerRows, std::vector<unsigned int> &binStart, std::vector<unsigned int> &binList,
                     int *pnBinsX, int *pnBinsY)
{
  const int nBinsX = (nLayerW + SCENE_BIN - 1) / SCENE_BIN;
  const int nBinsY = (nLayerRows + SCENE_BIN - 1) / SCENE_BIN;
  const int nBin = SCENE_BIN * nStep;
  const size_t nBins = static_cast<size_t>(nBinsX) * nBinsY;
  unsigned int i;

  g_binCount.assign(nBins, 0);
  for (int pass = 0; pass < 2; ++pass) {
    if (pass == 1) {
      binStart.resize(nBins + 1);
      unsigned int nAt = 0;
      for (size_t b = 0; b < nBins; ++b) {
        binStart[b] = nAt;
        nAt += g_binCount[b];
        g_binCount[b] = binStart[b];
      }
      binStart[nBins] = nAt;
      binList.resize(nAt ? nAt : 1);
    }
    for (i = nFirst; i < nLast; ++i) {
      int nLeft, nTop, nRight, nBottom;
      if (!ScenePrimBox(g_prims[i], nWidth, &nLeft, &nTop, &nRight, &nBottom))
        continue;
      if (nRight < 0 || nBottom < 0 || nLeft >= nWidth || nTop >= nRows)
        continue;
      const int bx0 = (nLeft < 0 ? 0 : nLeft) / nBin;
      const int by0 = (nTop < 0 ? 0 : nTop) / nBin;
      const int bx1 = (nRight >= nWidth ? nWidth - 1 : nRight) / nBin;
      const int by1 = (nBottom >= nRows ? nRows - 1 : nBottom) / nBin;
      for (int by = by0; by <= by1; ++by)
        for (int bx = bx0; bx <= bx1; ++bx) {
          const size_t b = static_cast<size_t>(by) * nBinsX + bx;
          if (pass == 0)
            ++g_binCount[b];
          else
            binList[g_binCount[b]++] = i;
        }
    }
  }
  *pnBinsX = nBinsX;
  *pnBinsY = nBinsY;
}

// The game's map at the stage the scene draws to, and the description's frame; at
// SCENE_SCREEN, pCopied the pixels the screen takes.
static void SceneCapture(const unsigned char *pCopied)
{
  // the screen's, and with the render view its map layer's
  const bool bZoom = g_bZoomPass && g_nStage == SCENE_SCREEN;
  const int nScreenW = g_portView.nWidth;
  const int nScreenRows = g_portView.nMapRows;
  const int nScreenH = static_cast<WORD>(_img_draw_buf.nHeight);
  const int nWidth = bZoom ? g_portRender.nWidth : nScreenW;
  const int nRows = bZoom ? g_portRender.nRows : nScreenRows;
  const int nHeight = bZoom ? g_nRenderHeight : nScreenH;
  const int nGameRows = g_nStage == SCENE_SCREEN ? nScreenH : nRows;
  const int nGameW = bZoom ? nScreenW : nWidth;
  const unsigned int nPrims = g_nStage >= SCENE_SPRITES ? static_cast<unsigned int>(g_prims.size()) : 0;
  const unsigned int nMapPrims = bZoom && g_nMapPrims < nPrims ? g_nMapPrims : nPrims;

  // With the GPU alone drawing, the buffer holds nothing to compare with.
  if (!SceneGpuOnly()) {
    g_game.resize(static_cast<size_t>(nGameW) * nGameRows);
    for (int y = 0; y < nGameRows; ++y)
      memcpy(&g_game[static_cast<size_t>(y) * nGameW], _img_draw_buf.pPixels + static_cast<size_t>(y) * nGameW,
             nGameW);
  }
  g_frame.pCopied = NULL;
  if (pCopied != NULL) {
    g_copied.assign(pCopied, pCopied + static_cast<size_t>(nScreenW) * nScreenH);
    g_frame.pCopied = &g_copied[0];
  }

  g_frame.nStage = g_nStage;
  g_frame.nWidth = nWidth;
  g_frame.nRows = nRows;
  g_frame.nHeight = nHeight;
  g_frame.nScrollX = bZoom ? g_portRender.nX : glScrollPixelX;
  g_frame.nScrollY = bZoom ? g_portRender.nY : glScrollPixelY;
  g_frame.nRingTilesX = bZoom ? g_nRenderRingX : g_portView.nRingTilesX;
  g_frame.nRingTilesY = bZoom ? g_nRenderRingY : g_portView.nRingTilesY;
  std::vector<SceneSlot> &slots = bZoom ? g_renderSlots : g_slots;
  const size_t nSlots = static_cast<size_t>(g_frame.nRingTilesX) * g_frame.nRingTilesY;
  g_frame.nSlotsRing = bZoom ? 1 : 0;
  SlotsDirty &dirty = g_aSlotsDirty[g_frame.nSlotsRing];
  if (slots.size() < nSlots) {
    slots.resize(nSlots, g_slotEmpty);
    dirty = {0, SIZE_MAX};
  }
  g_frame.pSlots = &slots[0];
  g_frame.nSlotsFirst = static_cast<unsigned int>(dirty.nFirst < nSlots ? dirty.nFirst : nSlots);
  g_frame.nSlotsEnd = static_cast<unsigned int>(dirty.nEnd < nSlots ? dirty.nEnd : nSlots);
  g_frame.bZoom = bZoom ? 1 : 0;
  g_frame.nScreenW = nScreenW;
  g_frame.nScreenH = nScreenH;
  g_frame.nScreenRows = nScreenRows;
  g_frame.nMapPrims = nMapPrims;
  g_frame.fCamX = bZoom ? g_portRender.fCamX : glScrollPixelX;
  g_frame.fCamY = bZoom ? g_portRender.fCamY : glScrollPixelY;
  g_frame.fZoom = bZoom ? g_portRender.fZoom : 1.0;
  g_frame.nGameScrollX = glScrollPixelX;
  g_frame.nGameScrollY = glScrollPixelY;
  g_frame.nTileset = g_nTileset;
  g_frame.nEra = g_MapTileset & 7;
  for (int i = 0; i < 5; ++i) {
    g_frame.anStarOfs[i] = dword_543258[i] >> 8;
    g_frame.anStarOfs[5 + i] = dword_54326C[i] >> 8;
  }
  g_frame.pMegatiles = reinterpret_cast<const unsigned int *>(gfpMtxSet);
  g_frame.nMegatiles = g_nMegatiles;
  g_frame.pCells = gfpCellBuf;
  g_frame.nCells = g_nCells;
  g_frame.nAtlasSerial = g_nAtlasSerial;
  g_frame.pEntries = g_entries.empty() ? NULL : &g_entries[0];
  g_frame.nEntries = static_cast<unsigned int>(g_entries.size());
  g_frame.pPixels = g_pixels.empty() ? NULL : &g_pixels[0];
  g_frame.nPixels = static_cast<unsigned int>(g_pixels.size());
  g_frame.pTables = g_tables.empty() ? NULL : &g_tables[0];
  g_frame.nTables = static_cast<unsigned int>(g_tables.size());
  g_frame.pPrims = g_prims.empty() ? NULL : &g_prims[0];
  g_frame.nPrims = nPrims;
  g_primArt.resize(g_prims.size(), SCENE_NONE);
  g_frame.pPrimArt = g_primArt.empty() ? NULL : &g_primArt[0];
  g_primGrp.resize(g_prims.size(), 0);
  g_frame.pPrimGrp = g_primGrp.empty() ? NULL : &g_primGrp[0];
  g_primColor.resize(g_prims.size(), 0);
  g_frame.pPrimColor = g_primColor.empty() ? NULL : &g_primColor[0];
  // the looks only where a primitive has one, and their data as the pass left them
  if (!g_primLook.empty())
    g_primLook.resize(g_prims.size(), 0);
  g_frame.pPrimLook = g_primLook.empty() ? NULL : &g_primLook[0];
  g_frame.pLooks = g_primLook.empty() && !g_bPassLooked ? NULL : PortLooksGpu(&g_frame.nLooksBytes);
  if (g_frame.pLooks == NULL)
    g_frame.nLooksBytes = 0;
  // the GPU's layer, coarser with the camera far out; never under the check, which
  // compares it with the software's
  const int nStep = bZoom && SceneGpuOnly() ? PortGpuSceneStep(nWidth, nRows, g_portRender.fZoom) : 1;
  g_frame.nStep = nStep;
  g_frame.nLayerW = ((nWidth + nStep - 1) / nStep + 3) & ~3;
  g_frame.nLayerRows = (nRows + nStep - 1) / nStep;
  SceneBin(0, nMapPrims, nWidth, nRows, nStep, g_frame.nLayerW, g_frame.nLayerRows, g_binStart, g_binList,
           &g_frame.nBinsX, &g_frame.nBinsY);
  g_frame.pBinStart = &g_binStart[0];
  g_frame.pBinList = &g_binList[0];
  g_frame.nBinList = g_binStart[static_cast<size_t>(g_frame.nBinsX) * g_frame.nBinsY];
  if (bZoom) {
    SceneBin(nMapPrims, nPrims, nScreenW, nScreenRows, 1, nScreenW, nScreenRows, g_hudBinStart, g_hudBinList,
             &g_frame.nHudBinsX, &g_frame.nHudBinsY);
    g_frame.pHudBinStart = &g_hudBinStart[0];
    g_frame.pHudBinList = &g_hudBinList[0];
    g_frame.nHudBinList = g_hudBinStart[static_cast<size_t>(g_frame.nHudBinsX) * g_frame.nHudBinsY];
  } else {
    g_frame.nHudBinsX = 0;
    g_frame.nHudBinsY = 0;
    g_frame.pHudBinStart = NULL;
    g_frame.pHudBinList = NULL;
    g_frame.nHudBinList = 0;
  }
  g_frame.pBytes = g_bytes.empty() ? NULL : &g_bytes[0];
  g_frame.nBytes = static_cast<unsigned int>(g_bytes.size());
  SceneFogLightPlace();
  g_frame.pGame = SceneGpuOnly() ? NULL : &g_game[0];
  g_bReady = true;
}

// A pass's time, from gamemap_update to the end of its recording: the game's drawing
// and the description both, or the description alone when the GPU alone draws.
static double g_dLastPassMs;  // the last pass's alone, for the frame log
static void SceneTimePass(void)
{
  LARGE_INTEGER now, freq;
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&freq);
  g_dLastPassMs = 1000.0 * static_cast<double>(now.QuadPart - g_passStart.QuadPart) / static_cast<double>(freq.QuadPart);
  g_dPassMs += g_dLastPassMs;
  ++g_nPasses;
}

double PortSceneLastPassMs(void)
{
  return g_dLastPassMs;
}

void PortSceneTimes(double *pdMs, unsigned int *pnPasses)
{
  *pdMs = g_dPassMs;
  *pnPasses = g_nPasses;
  g_dPassMs = 0.0;
  g_nPasses = 0;
}

// After cell_copy_scrollbuf: the view's terrain is in the screen buffer.
void PortSceneTerrainDone(void)
{
  if (g_bRecording && g_nStage == SCENE_TERRAIN)
    SceneCapture(NULL);
}

// After CSprite::DrawAll: the sprites are.
void PortSceneSpritesDone(void)
{
  if (g_bRecording && g_nStage == SCENE_SPRITES)
    SceneCapture(NULL);
}

// At the end of gamemap_update: the whole map layer is.
void PortSceneMapEnd(void)
{
  if (!g_bRecording || g_nStage > SCENE_MAP)
    return;
  if (g_nStage == SCENE_MAP)
    SceneCapture(NULL);
  SceneTimePass();
  g_bRecording = false;
}

// img_update_real, before the buffer goes onto the screen: the layers over the map are
// in it, and pMask (NULL when nothing is copied) says what goes. The screen's cover
// (port_present.cpp) follows: what is copied shows the GPU's map when the scene has
// this frame, the screen's own bytes when not.
void PortSceneScreenDone(const struct _HSTRANS *pMask)
{
  int nWidth = 0;
  int nHeight = 0;
  const unsigned char *pCopied = PortMaskBits(pMask, &nWidth, &nHeight);

  if (pCopied != NULL && (nWidth != g_portView.nWidth || nHeight != g_portView.nHeight)) {
    PortScreenCoverClear();
    pCopied = NULL;
  }
  const bool bCapture = g_bRecording && g_nStage == SCENE_SCREEN && pCopied != NULL;
  if (bCapture)
    SceneCapture(pCopied);
  if (g_bRecording)
    SceneTimePass();
  g_bRecording = false;
  if (pCopied != NULL)
    PortScreenCoverCopied(pCopied, g_portView.nMapRows, bCapture ? 1 : 0);
  PortViewSoftCover(pCopied);  // the software's render view, zoomed under SDL's renderer
}

void PortSceneRenderBegin(void)
{
  g_bRenderSlots = true;
}

// A slot as cell_draw_mtx and cell_draw_creep leave it for megatile wMtx at tile nX, nY.
static void SceneSlotFill(SceneSlot *pSlot, unsigned int wMtx, int nX, int nY, unsigned int nEdge)
{
  pSlot->nMtx = wMtx;
  pSlot->nTile = (static_cast<unsigned int>(nX) & 0xFFFF) | (static_cast<unsigned int>(nY) << 16);
  pSlot->nEdge = SCENE_NONE;
  pSlot->nEdgeFrame = 0;
  if (nEdge != 0) {
    // cell_draw_creep's frame of the edges' group, as PortSceneCreep takes it
    const FRAME *pEdge = &reinterpret_cast<const GROUP *>(sg_pEdgeGroup_g)->frames[nEdge - 1];
    pSlot->nEdge = SceneFrameEntry(pEdge);
    pSlot->nEdgeFrame = ((nEdge - 1) & 0xFF) | (static_cast<unsigned int>(pEdge->bOffsetX) << 8) |
                        (static_cast<unsigned int>(pEdge->_unmapped_01 & 0xFF) << 16) | 0x80000000u;
  }
}

int PortSceneRingFill(void)
{
  if (g_nStage <= SCENE_OFF || !SceneGpuOnly() || !g_bRenderSlots || gfpCellMap == NULL)
    return 0;
  // cell_paint's tiles: the ring's rows from the view's top left tile, each row's tiles up to
  // the map's right edge, and megatile 0 in the rows past its foot
  const int nX0 = static_cast<int>(glScrollPixelX >> 5);
  const int nY0 = static_cast<int>(glScrollPixelY >> 5);
  const int nRingX = g_portView.nRingTilesX;
  const int nRingY = g_portView.nRingTilesY;
  const int nMapW = gwMapWidth;
  const int nMapH = gwMapHeight;
  // a map's load starts the atlas again, and the edges' entries with it
  const bool bAll = g_nRingFillSerial != g_nAtlasSerial;
  g_nRingFillSerial = g_nAtlasSerial;
  const int anAt[4] = {nX0, nY0, nRingX, nRingY};
  const bool bKept = !bAll && !g_bRingTouched && g_nRingFillFrame == oc_dwReplayFrame &&
                     g_nRingFillState == g_nStateSerial && memcmp(anAt, g_anRingFillAt, sizeof anAt) == 0;
  if (bKept && !PortScenePassKeepCheck())
    return 1;
  unsigned int nWritten = 0;
  g_nRingFillFrame = oc_dwReplayFrame;
  g_nRingFillState = g_nStateSerial;
  memcpy(g_anRingFillAt, anAt, sizeof anAt);
  g_bRingTouched = false;
  // the slots are read where this ring puts each tile: with the tables of a ring of another
  // width, a tile's slot of that ring held it still and its slot of this one was left as it was
  RingTables(nRingX, nRingY);
  for (int nRow = 0; nRow < nRingY; ++nRow) {
    const int y = nY0 + nRow;
    const bool bPast = static_cast<unsigned int>(y) >= static_cast<unsigned int>(nMapH);
    int nTiles = nRingX;
    if (!bPast && nMapW - nX0 < nTiles)
      nTiles = nMapW - nX0 > 0 ? nMapW - nX0 : 0;
    const WORD *pwCell = bPast ? NULL : gfpCellMap + static_cast<size_t>(y) * nMapW + nX0;
    const BYTE *pbEdge = bPast || sg_pEdgeMap == NULL ? NULL : sg_pEdgeMap + static_cast<size_t>(gdwCreepMtxWdt_g) * y + nX0;
    for (int k = 0; k < nTiles; ++k) {
      const int x = nX0 + k;
      const unsigned int wMtx = bPast ? 0u : pwCell[k];
      const unsigned int nEdge = (wMtx & 0x8000) != 0 && pbEdge != NULL ? pbEdge[k] : 0u;
      const unsigned int nTile = (static_cast<unsigned int>(x) & 0xFFFF) | (static_cast<unsigned int>(y) << 16);
      // the slot as it is, read without marking it written
      const int nColumn = static_cast<size_t>(x) < g_ringColumn.size() ? g_ringColumn[x] : x % nRingX;
      const int nRingRow = static_cast<size_t>(y) < g_ringRow.size() ? g_ringRow[y] : y % nRingY * nRingX;
      const size_t i = static_cast<size_t>(nRingRow + nColumn);
      if (!bAll && i < g_renderSlots.size()) {
        const SceneSlot &held = g_renderSlots[i];
        if (held.nTile == nTile && held.nMtx == wMtx &&
            (nEdge != 0 ? (held.nEdgeFrame & 0x800000FFu) == (0x80000000u | ((nEdge - 1) & 0xFF))
                        : held.nEdge == SCENE_NONE))
          continue;
      }
      if (SceneSlot *pSlot = SceneSlotFor(x, y)) {
        SceneSlotFill(pSlot, wMtx, x, y, nEdge);
        ++nWritten;
      }
    }
  }
  if (bKept)
    PortScenePassKeepDiffer("the render view's ring", nWritten);
  return 1;
}

void PortSceneRenderEnd(void)
{
  g_bRenderSlots = false;
  if (!g_bRecording)
    return;
  g_bZoomPass = true;
  g_nMapPrims = static_cast<unsigned int>(g_prims.size());
  g_nRenderRingX = g_portView.nRingTilesX;
  g_nRenderRingY = g_portView.nRingTilesY;
  g_nRenderHeight = g_portView.nHeight;
}

const SceneFrame *PortSceneTake(void)
{
  if (!g_bReady)
    return NULL;
  g_bReady = false;
  // the GPU has the slots written so far
  g_aSlotsDirty[g_frame.nSlotsRing] = {SIZE_MAX, 0};
  return &g_frame;
}
