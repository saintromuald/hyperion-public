// gamemap.cpp -- the playfield surface: the tile map the game draws into, the
// screen's position over it, and the click routing that turns a screen point
// back into a map target. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/gamemap.h"
#include "../h/repulse.h"   // repulse_init, repulse_free
#include "../h/maplimits.h"  // Hyperion: maps past 256 tiles
#include "../h/CThingy.h"
#include "../h/CUnitBuild.h"
#include "../h/cursor.h"
#include "../h/creep.h"
#include "../h/mapmsg.h"
#include "../h/gds_local.h"
#include "../h/gamedata.h"
#include "../h/snd.h"
#include "../h/net_cmds.h"
#include "../h/usel.h"
#include "../h/CSprite.h"
#include "../h/img.h"
#include "../h/dlg_dlgdraw.h"
#include "../h/storm.h"
#include "../h/mask.h"
#include "../h/gds_vidblit.h"
#include "../h/minimap.h"
#include "../h/gameloop.h"
#include "../h/TrigAct.h"
#include "../h/glues.h"
#include "../h/statbtn.h"
#include "../h/statcmd.h"   // statcmd_set_ctrl
#include "../h/game_evt.h"
#include "../h/placebox.h"
#include "../h/rclick.h"      // show_target_x
#include "../h/target.h"      // target_off
#include "../gen/globals117.h"
#include "../port/data/port_map.h"  // Hyperion: PortMapStringsAreWide, PortMapStringWide, PortMapStringNarrow
#include "../port/render/port_scene.h"  // Hyperion
#include "../port/render/port_view.h"  // Hyperion
#include "../port/data/port_formats.h"  // Hyperion: PortMegatilesLoad

// Globals this TU reaches, dual-spelled, one #ifdef per name. gwScreenTileY_g
// @0x5264B2 is the screen's top-left Y in tiles, spelled as maphdr.cpp,
// gamesnd.cpp and CUnitGUI.cpp spell it.

#ifdef OC_DLL_MODE
  #ifndef gwScreenTileY_g
  #define gwScreenTileY_g (*reinterpret_cast<WORD *>(OC_VA(0x5264B2u)))
  #endif
  #ifndef sgdwYScanBegin_g
  #define sgdwYScanBegin_g (*reinterpret_cast<DWORD *>(OC_VA(0x666970u)))
  #endif
  #ifndef sgdwYScanEnd_g
  #define sgdwYScanEnd_g (*reinterpret_cast<DWORD *>(OC_VA(0x666974u)))
  #endif
  #ifndef gpMapStrs
  #define gpMapStrs (*reinterpret_cast<void **>(OC_VA(0x666440u)))
  #endif
  #ifndef gdwMapStrSize
  #define gdwMapStrSize (*reinterpret_cast<DWORD *>(OC_VA(0x666444u)))
  #endif
  #ifndef byte_666460
  #define byte_666460 (*reinterpret_cast<BYTE (*)[256]>(OC_VA(0x666460u)))
  #endif
  #ifndef sgpCreepMIMap
  #define sgpCreepMIMap (*reinterpret_cast<WORD **>(OC_VA(0x66696Cu)))
  #endif
  #ifndef gfpMIMap
  #define gfpMIMap (*reinterpret_cast<WORD **>(OC_VA(0x666458u)))
  #endif
  #ifndef gpMtxInfo
  #define gpMtxInfo (*reinterpret_cast<WORD **>(OC_VA(0x6CE650u)))
  #endif
  #ifndef sg_vf4_map_g
  #define sg_vf4_map_g (*reinterpret_cast<WORD **>(OC_VA(0x66644Cu)))
  #endif
  #ifndef gpSquareMap
  #define gpSquareMap (*reinterpret_cast<DWORD **>(OC_VA(0x666454u)))
  #endif
  #ifndef gfpCellMap
  #define gfpCellMap (*reinterpret_cast<WORD **>(OC_VA(0x54323Cu)))
  #endif
  #ifndef gpMtxFlagsBuf
  #define gpMtxFlagsBuf (*reinterpret_cast<DWORD **>(OC_VA(0x666450u)))
  #endif
  #ifndef gdwMIEntries
  #define gdwMIEntries (*reinterpret_cast<DWORD *>(OC_VA(0x666968u)))
  #endif
  #ifndef gfpMtxSet
  #define gfpMtxSet (*reinterpret_cast<BYTE **>(OC_VA(0x543238u)))
  #endif
  #ifndef _gfpScrollBuf
  #define _gfpScrollBuf (*reinterpret_cast<BYTE **>(OC_VA(0x543240u)))
  #endif
  #ifndef gfpCellBuf
  #define gfpCellBuf (*reinterpret_cast<BYTE **>(OC_VA(0x543244u)))
  #endif
  #ifndef dword_66645C
  #define dword_66645C (*reinterpret_cast<void **>(OC_VA(0x66645Cu)))
  #endif
  #ifndef sgbIsMinimapPreview_g
  #define sgbIsMinimapPreview_g (*reinterpret_cast<DWORD *>(OC_VA(0x666560u)))
  #endif
  #ifndef gMapCursor
  #define gMapCursor (*reinterpret_cast<WORD *>(OC_VA(0x667288u)))
  #endif
  #ifndef gbSelectDragging
  #define gbSelectDragging (*reinterpret_cast<BYTE *>(OC_VA(0x6CA720u)))
  #endif
  #ifndef gpszTilesetNames_g
  #define gpszTilesetNames_g (reinterpret_cast<const char **>(OC_VA(0x503398u)))
  #endif
  #ifndef g_MapTileset
  #define g_MapTileset (*reinterpret_cast<WORD *>(OC_VA(0x5264BCu)))
  #endif
  #ifndef gwCellFlagsEntries
  #define gwCellFlagsEntries (*reinterpret_cast<WORD *>(OC_VA(0x666448u)))
  #endif
  #ifndef gwScreenTileX_g
  #define gwScreenTileX_g (*reinterpret_cast<WORD *>(OC_VA(0x5264B0u)))
  #endif
  #ifndef word_526550
  #define word_526550 (reinterpret_cast<WORD *>(OC_VA(0x526550u)))
  #endif
  #ifndef gGameMapRect
  #define gGameMapRect (*reinterpret_cast<RECT *>(OC_VA(0x666430u)))
  #endif
#else
extern WORD  gwScreenTileY_g;    // 0x5264B2
extern DWORD sgdwYScanBegin_g;   // 0x666970
extern DWORD sgdwYScanEnd_g;     // 0x666974
extern void *gpMapStrs;          // 0x666440
extern DWORD gdwMapStrSize;      // 0x666444  Hyperion: for get_map_string's STRx
extern BYTE  byte_666460[256];   // 0x666460
extern WORD *sgpCreepMIMap;      // 0x66696C
extern WORD *gfpMIMap;           // 0x666458
extern WORD *gpMtxInfo;          // 0x6CE650, creep.cpp's spelling
extern WORD *sg_vf4_map_g;       // 0x66644C
extern DWORD *gpSquareMap;       // 0x666454
extern WORD  *gfpCellMap;        // 0x54323C
extern DWORD *gpMtxFlagsBuf;     // 0x666450
extern DWORD gdwMIEntries;       // 0x666968
extern BYTE *gfpMtxSet;          // 0x543238
extern BYTE *_gfpScrollBuf;      // 0x543240
extern BYTE *gfpCellBuf;         // 0x543244
extern void *dword_66645C;       // 0x66645C
extern DWORD sgbIsMinimapPreview_g;  // 0x666560
extern WORD  gMapCursor;         // 0x667288, game_evt.cpp's spelling
extern BYTE  gbSelectDragging;   // 0x6CA720, usel.cpp's spelling
extern const char *gpszTilesetNames_g[8] OC_UNALIGNED;  // 0x503398
extern WORD  g_MapTileset;       // 0x5264BC
extern WORD  gwCellFlagsEntries; // 0x666448
extern WORD  gwScreenTileX_g;    // 0x5264B0
extern WORD  word_526550[6];     // 0x526550
extern RECT  gGameMapRect;       // 0x666430
#endif

// The bodies, in oracle VA order.

// Look a string up in the map's own string table. The table pointer is null
// before a map is loaded, and the caller gets a null back rather than a fault.
const char *get_map_string(WORD wStringId)
{
    void *pStrs = gpMapStrs;
    if (pStrs == NULL)
        return NULL;
    // Hyperion: a STRx table, from a map saved by StarCraft: Remastered,
    // has u32 counts and offsets (port_map.cpp). A STR table is read
    // inside its bounds, as a protected map's may be too short for its reads.
    if (PortMapStringsAreWide())
        return PortMapStringWide(pStrs, gdwMapStrSize, wStringId);
    return PortMapStringNarrow(pStrs, gdwMapStrSize, wStringId);
}

// Derive one flags dword per megatile from its 16 sub-tile vf4 entries: how
// much of it is walkable, and how much is buildable, high ground or ramp. The
// counter is bumped before the store, as 1.17 does it.
void gamemap_gen_default()
{
    DWORD dwFlags;
    int i;
    DWORD *pOut;
    WORD *pVf4;

    pOut = reinterpret_cast<DWORD *>(
        SMemAlloc(gwCellFlagsEntries * 4, "Gamemap.cpp", 0xC3, 8));
    gpMtxFlagsBuf = pOut;
    for (i = 0; i < gwCellFlagsEntries;) {
        pVf4 = &sg_vf4_map_g[i * 16];
        dwFlags = 0;
        unsigned nWalk = 0;
        for (int k = 0; k < 16; k++) {
            if ((pVf4[k] & 1) != 0)
                nWalk++;
        }
        if (nWalk > 12)
            dwFlags = 0x10000;
        if (nWalk >= 1 && nWalk <= 15)
            dwFlags |= 0x20000000;

        int nMid = 0;
        int nHigh = 0;
        int nRamp = 0;
        for (unsigned k = 0; k < 16; k++) {
            WORD wCell = pVf4[k];
            if ((wCell & 4) != 0)
                nHigh++;
            if ((wCell & 2) != 0)
                nMid++;
            if ((wCell & 8) != 0)
                nRamp++;
        }
        if (nHigh >= 12)
            dwFlags |= 0x4000000;
        else if (nHigh + nMid >= 12)
            dwFlags |= 0x2000000;
        if (nRamp != 0)
            dwFlags |= 0x1000000;
        if ((dwFlags & 0x10000) == 0)
            dwFlags |= 0x40000;
        i++;
        *pOut = dwFlags;
        pOut++;
    }
}

// Classify a map pixel: 2 where the tile is high ground, else its walkable bit.
// The creep overlay wins over the base tile map wherever it has an entry.
int get_map_height(int x, int y)
{
    WORD wMapWidth = gwMapWidth;
    // The tile coordinates as one struct keep x / 32 ahead of y / 32.
    struct { int x; int y; } t = { x / 32, y / 32 };
    int nTile = t.x + wMapWidth * t.y;
    unsigned nMtx = sgpCreepMIMap[nTile];
    if (nMtx == 0)
        nMtx = gfpMIMap[nTile];

    unsigned nGroup = (nMtx >> 4) & 0x7FF;
    unsigned nSub = nMtx & 0xF;
    WORD wVx4 = gpMtxInfo[nGroup * 0x1A + nSub + 10];
    WORD wFlags = sg_vf4_map_g[(wVx4 * 4 + ((y >> 3) & 3)) * 4 + ((x >> 3) & 3)];
    if ((wFlags & 4) != 0)
        return 2;
    return ((BYTE)wFlags >> 1) & 1;
}

// Expand the loaded megatile map into the two per-cell buffers the renderer
// walks. Returns 0 the moment a tile names a group the tileset does not have.
// pCell is defined before the count, and the count is width * height, 1.17's
// order.
int __fastcall convert_cellmap()
{
    WORD *pMI = gfpMIMap;
    WORD *pCell = gfpCellMap;
    int nCount = gwMapWidth * gwMapHeight;
    DWORD *pSquare = gpSquareMap;
    while (nCount != 0) {
        WORD wMtx = *pMI;
        --nCount;
        unsigned nGroup = (wMtx >> 4) & 0x7FF;
        if (nGroup >= gdwMIEntries)
            return 0;
        *pCell = gpMtxInfo[nGroup * 26 + (wMtx & 0xF) + 10];
        *(reinterpret_cast<WORD *>(pSquare) + 1) = 0;
        *pSquare |= gpMtxFlagsBuf[*pCell & 0x7FFF]
                    | ((gpMtxInfo[nGroup * 26 + 1] & ~0x2705) << 16);
        if ((*pMI & 0x8000) != 0) {
            *pCell |= 0x8000;
            *pSquare |= 0x400000;
        }
        pMI++;
        pCell++;
        pSquare++;
    }
    return 1;
}

// Centre the view on a tile. The two arguments arrive in ECX and EDX, and the
// biases are half the 24 x 17 tile viewport, so the named tile ends up in the
// middle rather than at the top-left corner. Hyperion: across, the bias
// is half the view's width, 10 tiles in 1.17, of what shows beside a HUD's
// side columns (PortViewShownWidth); down, it stays 1.17's 6.
void gamemap_center_on_mtx(int nTileX, int nTileY)
{
    cell_set_pos(nTileX * 32 - PortViewShownWidth() / 2, (nTileY - 6) * 32);
}

// Put the view where the map header asked, or at the origin when it named no
// start tile, then seed the three scroll-history points with where it landed.
void gamemap_choose_center()
{
    if (gwScreenTileX_g != 0xFFFF && gwScreenTileY_g != 0xFFFF)
        cell_set_pos(gwScreenTileX_g << 5, gwScreenTileY_g << 5);
    else
        cell_set_pos(0, 0);

    word_526550[0] = (WORD)glScrollPixelX;
    word_526550[1] = (WORD)glScrollPixelY;
    word_526550[2] = (WORD)glScrollPixelX;
    word_526550[3] = (WORD)glScrollPixelY;
    word_526550[4] = (WORD)glScrollPixelX;
    word_526550[5] = (WORD)glScrollPixelY;
}

// Invalidate the playfield layer so the next paint redraws all of it. The test
// is a suppression flag: while it is set the caller is already inside a redraw.
void gamemap_force_redraw()
{
    if ((byte_6D0BF5 & 1) == 0)
        img_force(5);
}

// Release every buffer the loaded map owns and null the pointers, then hand the
// rest of the teardown on. The four mem_free calls are unconditional; the six
// Storm ones are not.
void gamemap_free()
{
    repulse_free();
    mem_free(gfpCellBuf, "Gamemap.cpp", 0x1DC);
    mem_free(gfpMtxSet, "Gamemap.cpp", 0x1DD);
    mem_free(gpMtxInfo, "Gamemap.cpp", 0x1DE);
    mem_free(sg_vf4_map_g, "Gamemap.cpp", 0x1DF);
    if (gpSquareMap != NULL)
        SMemFree(gpSquareMap, "Gamemap.cpp", 0x1E1, 0);
    if (dword_66645C != NULL)
        SMemFree(dword_66645C, "Gamemap.cpp", 0x1E2, 0);
    if (_gfpScrollBuf != NULL)
        SMemFree(_gfpScrollBuf, "Gamemap.cpp", 0x1E3, 0);
    if (gfpCellMap != NULL)
        SMemFree(gfpCellMap, "Gamemap.cpp", 0x1E4, 0);
    if (gfpMIMap != NULL)
        SMemFree(gfpMIMap, "Gamemap.cpp", 0x1E5, 0);
    if (gpMtxFlagsBuf != NULL)
        SMemFree(gpMtxFlagsBuf, "Gamemap.cpp", 0x1E6, 0);
    gfpCellBuf = NULL;
    gfpMtxSet = NULL;
    gpMtxInfo = NULL;
    sg_vf4_map_g = NULL;
    gpMtxFlagsBuf = NULL;
    gpSquareMap = NULL;
    dword_66645C = NULL;
    _gfpScrollBuf = NULL;
    gfpCellMap = NULL;
    gfpMIMap = NULL;
    sgpCreepMIMap = NULL;
    mask_free();
    scroll_free();
    vid_free();
    if (sgbIsMinimapPreview_g == 0)
        colorcycling_free();
    light_free();
}

// Load every per-tileset file a freshly started map needs, derive the cell
// tables from them, and hand the rest of the setup on. The two preview guards
// skip the palette work and the mask when the map is only being previewed.
void gamemap_init()
{
    char szPath[0x104];

    gMapCursor = 0;
    gbSelectDragging = 0;
    // Hyperion: the three tile maps hold the largest map; 1.17's were
    // 0x20000, 0x20000 and 0x40000 bytes
    gfpMIMap = reinterpret_cast<WORD *>(
        SMemAlloc(MAP_TILES_MAX * 2, "Gamemap.cpp", 0x261, 0));
    gfpCellMap = reinterpret_cast<WORD *>(
        SMemAlloc(MAP_TILES_MAX * 2, "Gamemap.cpp", 0x262, 0));
    // Hyperion: the ring for the widest view, so the view can change
    // size without it (port_view.cpp); 1.17's was 0x49800 bytes.
    _gfpScrollBuf = reinterpret_cast<BYTE *>(
        SMemAlloc(PORT_VIEW_RING_MAX, "Gamemap.cpp", 0x263, 0));
    // Black where nothing is drawn: past the right edge of a map narrower than
    // the view, which 1.17's cell_draw_row leaves alone.
    memset(_gfpScrollBuf, 0, PORT_VIEW_RING_MAX);
    gpSquareMap = reinterpret_cast<DWORD *>(
        SMemAlloc(MAP_TILES_MAX * 4, "Gamemap.cpp", 0x264, 0));
    memset(gpSquareMap, 0, MAP_TILES_MAX * 4);
    repulse_init();

    psnprintf(szPath, 0x104, "%s%s%s", "Tileset\\",
              gpszTilesetNames_g[g_MapTileset], ".wpe");
    load_game_data(szPath, sgGamePalette_g, NULL, "Gamemap.cpp", 0x27B);

    psnprintf(szPath, 0x104, "%s%s%s", "Tileset\\",
              gpszTilesetNames_g[g_MapTileset], ".vf4");
    DWORD dwVf4Size;
    sg_vf4_map_g = reinterpret_cast<WORD *>(
        load_game_data(szPath, NULL, &dwVf4Size, "Gamemap.cpp", 0x27F));
    gwCellFlagsEntries = (WORD)(dwVf4Size >> 5);
    gamemap_gen_default();

    psnprintf(szPath, 0x104, "%s%s%s", "Tileset\\",
              gpszTilesetNames_g[g_MapTileset], ".cv5");
    DWORD dwCv5Size;
    gpMtxInfo = reinterpret_cast<WORD *>(
        load_game_data(szPath, NULL, &dwCv5Size, "Gamemap.cpp", 0x289));
    gdwMIEntries = dwCv5Size / 52;

    psnprintf(szPath, 0x104, "%s%s%s", "Tileset\\",
              gpszTilesetNames_g[g_MapTileset], ".grp");
    creep_hooks_t hooks;
    hooks.pfnRedrawCell = sub_4B4040;
    hooks.pfnIsCovered = tile_covered_by_creep;
    hooks.pfnCellChanged = tile_is_fogged;
    hooks.pfnCellRecord = NULL;
    creep_init(gfpMIMap, &hooks, gwMapWidth, gwMapHeight, szPath);
    int nCreepSize;
    sgpCreepMIMap = getCreepMap(&nCreepSize);

    psnprintf(szPath, 0x104, "%s%s%s", "Tileset\\",
              gpszTilesetNames_g[g_MapTileset], ".vx4");
    // Hyperion: a DWORD a cell reference, so Remastered's newer doodads, whose
    // cells are past 1.17's 16 bits, draw (port_formats.cpp)
    gfpMtxSet = reinterpret_cast<BYTE *>(PortMegatilesLoad(szPath));

    psnprintf(szPath, 0x104, "%s%s%s", "Tileset\\",
              gpszTilesetNames_g[g_MapTileset], ".vr4");
    gfpCellBuf = reinterpret_cast<BYTE *>(
        load_game_data(szPath, NULL, NULL, "Gamemap.cpp", 0x2A1));
    PortSceneMapLoaded();  // Hyperion: the GPU scene's tileset (port_scene.cpp)

    if (sgbIsMinimapPreview_g == 0) {
        vid_restore_palette(0, 0x100, sgGamePalette_g);
        gray_pal_init(sgGamePalette_g);
        light_cycle_init();
    }
    sub_466810(sgGamePalette_g, gpszTilesetNames_g[g_MapTileset]);
    sub_4668F0(sgGamePalette_g, gpszTilesetNames_g[g_MapTileset]);
    if (sgbIsMinimapPreview_g == 0) {
        _img_list[5].bInUse = 1;
        mask_init();
        gamemap_reinit();
    }
    scroll_reset();
    scroll_init_game();
    sub_4B44A0();
}

// Left click on the playfield. The console swallows the click where it covers
// the map; otherwise the map point becomes a move or attack order, and the
// target marker's own unit type picks the command card the console shows.
void gamemap_lclick_target(dlgevt_t *pEvent)
{
    if (console_collision(pEvent->nX, pEvent->nY) != 0)
        return;

    // Hyperion: through the camera (port_view.cpp)
    int nMapX, nMapY;
    PortViewScreenToMap(pEvent->nX, pEvent->nY, &nMapX, &nMapY);
    CUnit *pTarget = get_map_target(nMapX, nMapY);
    CThingy *pMarker = show_target_x(nMapX, nMapY, pTarget);
    int nCard;
    if (pMarker != NULL)
        nCard = pMarker->wUnitType;
    else
        nCard = 0xE4;
    usel_click_target(nMapX, nMapY, reinterpret_cast<intptr_t>(pTarget), nCard);
    game_hold_cursor(0);
}

// Rebuild the sprite priority rows the next draw walks. The range is the
// on-screen band clamped to the map; the low end's clamp is a conditional
// expression, not a branch.
void gamemap_priority_update()
{
    if (!IsGameRunning())
        return;

    int nTop = gwScreenTileY_g - 4;
    int nBottom = gwScreenTileY_g + 0x194;
    sgdwYScanEnd_g = nBottom;
    if (nTop < 0)
        nTop = 0;
    int nMapHeight = gwMapHeight;
    sgdwYScanBegin_g = nTop;
    if (nBottom >= nMapHeight) {
        nBottom = nMapHeight - 1;
        sgdwYScanEnd_g = nBottom;
    }
    CSprite::PriorityAddRange(nTop, nBottom);
}

// Reset the playfield layer for a new map: the full 640x400 surface, no
// scroll offset, this file's own paint callback, then repaint layers 3..5.
// Hyperion: the surface is the view's width by its map rows.
void gamemap_reinit()
{
    SetRect(&gGameMapRect, 0, 0, g_portView.nWidth - 1, g_portView.nMapRows - 1);
    _img_list[5].nX = 0;
    _img_list[5].nY = 0;
    _img_list[5].nWidth = static_cast<short>(g_portView.nWidth);
    _img_list[5].nHeight = static_cast<short>(g_portView.nMapRows);
    _img_list[5].pBmp = 0;
    _img_list[5].pfnUpdate = gamemap_update;
    byte_6D0BF5 = 0;
    img_area_invalid(0, 0, g_portView.nWidth - 1, g_portView.nHeight - 1);
    for (int nLayer = 3; nLayer <= 5; nLayer++)
        img_force((BYTE)nLayer);
}

// The playfield's paint callback, reached through _img_list[5]. The suppression
// bit picks between a full scroll-buffer copy and a dirty-cell repaint, and it
// is also what mask_draw and the parallax pair read afterwards.
void __fastcall gamemap_update(unsigned short, unsigned short, bmp_t *, bmpsrc_t *)
{
    int nFull = byte_6D0BF5 & 1;
    // Hyperion: the GPU scene describes the pass as it is drawn, and takes
    // the full path each time so the description is of the whole view
    // (port_scene.cpp, docs/renderer.md, R4b). The whole view is then marked
    // dirty, as a scroll marks it: the full path does not mark where the sprites
    // moved, which the other path does (CSprite::InvalidateSprites), and the copy
    // onto the screen takes only what is dirty. With the camera zoomed out the map is
    // drawn over a larger view than the game's, up to its text (port_view.cpp).
    PortViewRenderBegin();
    // the software's render view (SDL's renderer, zoomed) is drawn whole each pass too
    const bool bScene = PortSceneMapBegin() != 0 || PortViewRenderSoft() != 0;
    if (bScene) {
        nFull = 1;
        img_area_invalid(0, 0, g_portView.nWidth - 1, g_portView.nMapRows - 1);
    }
    if (nFull != 0) {
        mask_build_grid();
        // Hyperion: the scene's pass updates the rows it draws, not the whole map's
        //; a full redraw for 1.17's own reasons walks the map as before
        if (bScene)
            CalcDrawRectsInRows(sgdwYScanBegin_g, sgdwYScanEnd_g);
        else
            CalcAllDrawRects();
        mask_update(1);
        // Hyperion: not while nothing it reads has changed (PortSceneCellsKept)
        const bool bKept = PortSceneGpuOnly() != 0 && PortSceneCellsKept() != 0;
        if (!bKept || PortScenePassKeepCheck()) {
          const unsigned int nReports = PortSceneCellReports();
          cell_refresh();
          if (bKept)
            PortScenePassKeepDiffer("cell_refresh", PortSceneCellReports() - nReports);
        }
        // Hyperion: the ring is not drawn while the GPU alone draws the map.
        if (!PortSceneSkipDraw())
          cell_copy_scrollbuf();
        PortSceneTerrainDone();
    } else {
        mask_invalidate();
        CSprite::InvalidateSprites(sgdwYScanBegin_g, sgdwYScanEnd_g);
        mask_update(1);
        cell_refresh();
        redraw_dirty_mtx();
    }
    CSprite::DrawAll();
    PortSceneSpritesDone();  // Hyperion: the GPU scene (port_scene.cpp)
    if (g_MapTileset == 1) {
        if (nFull != 0)
            DrawStars();
        else
            DrawStarsDirty();
    }
    mask_draw(nFull);
    // Hyperion: the black X is a sprite of the map's, drawn before the map's text so
    // it is in the render view's part of the pass; 1.17 drew it after the drag box.
    CThingy::DrawBlackX();
    // Hyperion: zoomed, the placement box with the map
    PortViewRenderLayers();
    PortViewRenderEnd();
    map_msg_draw();
    usel_draw_rect();
    PortSceneMapEnd();
}

// Build the 256-entry grey index table from a four-byte-per-entry palette:
// a 77/151/28 luminance, rounded, reduced to three bits.
// The sum is unsigned: a signed one would shift arithmetically.
void gray_pal_init(BYTE *pPal)
{
    int i = 256;
    do {
        --i;
        unsigned nLum = pPal[i*4+0] * 0x4Du + pPal[i*4+1] * 0x97u
                        + pPal[i*4+2] * 7u * 4u + 0x1000u;
        byte_666460[i] = (BYTE)(nLum >> 13);
    } while (i != 0);
}

// Repaint every dirty 16x16 cell of the playfield from the scroll buffer,
// coalescing each row's consecutive dirty cells into one copy. The offset wraps
// modulo the scroll buffer, so it is tested again at the top of the cell loop,
// after each copy and after each row. nRun * 16 is held in a local across the call.
// Hyperion: the ring, the grid and the map rows are the view's
// (port_view.cpp): 1.17's ring was 0x2A0 by 0x1C0 bytes, its grid 0x28 cells
// across, its map 0x190 rows, and 0x2780 took a row of cells to the next.
void redraw_dirty_mtx()
{
    const int nRingSize = g_portView.nRingSize;
    const int nCells = g_portView.nGridWidth;
    int nOfs = (DWORD)(glScrollPixelY * g_portView.nRingPitch + glScrollPixelX) % nRingSize;
    BYTE *pInval = g_portView.pbInvalMap;
    int nY = 0;
    do {
        int nCol = 0;
        do {
            if (nOfs >= nRingSize)
                nOfs -= nRingSize;
            if (*pInval == 1) {
                int nEnd = nCol + 1;
                int nRun = 1;
                while (nEnd < nCells && *pInval != 0) {
                    nEnd++;
                    pInval++;
                    nRun++;
                }
                int nWidth = nRun << 4;
                cell_copy_scrollbuf_row(nY, nCol << 4, nWidth, nOfs);
                nOfs += nWidth - 0x10;
                nCol += nRun - 1;
                if (nOfs >= nRingSize)
                    nOfs -= nRingSize;
            }
            nCol++;
            nOfs += 0x10;
            pInval++;
        } while (nCol < nCells);
        nOfs += 16 * g_portView.nRingPitch - nCells * 16;
        if (nOfs >= nRingSize)
            nOfs -= nRingSize;
        nY += 0x10;
    } while (nY < g_portView.nMapRows);
}

// Right-click on the playfield. Targeting and placement modes each swallow the
// click; otherwise the command card goes back to its default page.
void screen_rclick(dlgevt_t *pEvent)
{
    if (target_is_on()) {
        target_off();
        return;
    }
    if (placebox_is_on()) {
        set_placebox(0, NULL, 1);
        return;
    }
    statcmd_set_ctrl(0xE4);
}
