// placebox.cpp -- the building placement box: the grid the player drags around
// before committing a build order, its per-tile shading matrix, the addon and
// geyser highlighting drawn under it, and the map's starting-unit placement.
//
// The TU's name is 1.17's own, from the "placebox.cpp" tag placebox_free hands
// SMemFree. Its line numbers put placebox_create before placebox_free in 1.17's
// source, although its address is higher.

// net_cmds.h declares placebox_query_place with the WORD unit type its caller
// passes; the definition below takes an int, so the header's view is renamed away.
#define placebox_query_place placebox_query_place_word_view
#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include <stddef.h>            // offsetof -- the headers' static_asserts need it

// collide.h declares mtx_query_place_bldg with the body's int coordinates; this
// TU passes its short cells unwidened, so the header's view is renamed away.
#define mtx_query_place_bldg mtx_query_place_bldg_int_view
#include "platform.h"
#include "rect16.h"            // ours: RectIsOnMap's ECX, 4 signed shorts
#include "bmp.h"               // ours: bmp_t -- the two placebox pixel buffers
#include "img.h"               // ours: img_t -- _img_list's 0x14-byte element
#include "CUnit.h"             // ours: SelfDestruct, DisplayLandingOverlay,
                               //       RemoveAllBuildQueue, bPlayer @+0x4C
#include "CImage.h"            // ours: CImage::ClearPlaceboxes (static)
#include "Storm.h"             // ours: SMemFree -- placebox_free's deallocator
#include "placebox.h"          // ours: this TU's own free functions
#include "net_cmds.h"          // ours: placebox_validate / placebox_query_place
#include "CUnitTerran.h"       // ours: placebox_is_on / sub_47D530
#include "CUnitBuild.h"        // ours: is_placing_addon
#include "order.h"             // ours: status_get_str / map_msg_target_self
#include "statbtn.h"           // ours: TTAllowed / OrderAllowed
#include "gamesnd.h"           // ours: gamesnd_placebox / _error / _error_msg
#include "minimap.h"           // ours: map_msg_target_self / game_hold_cursor
#include "point16.h"           // ours: the placement path passes one by value
#include "dialog.h"            // ours: dlgevt_t -- placebox_gamemap_lclick's ECX
#include "CSprite.h"           // ours: CSprite -- wPosX / wPosY at +0x14 / +0x16
#include "CUnitMisc.h"         // ours: IsBuiltRefineryType
#include "collide.h"           // ours: sub_413150 / mtx_query_place_bldg
#include "SAI_Banker.h"        // ours: bldg_get_unit_cost
#include "usel.h"              // ours: console_collision
#include "CUnitSearches.h"     // ours: TTBaseUnitType
#include "grp.h"               // ours: GROUP / FRAME -- placebox_draw_bldg
#include "CUnitProtoss.h"      // ours: init_juice_matrix
#include "event_Win.h"         // ours: game_reset_handlers
#include "gluRdy.h"            // ours: bmp_create / bmp_mask_rect
#include "TrigAct.h"           // ours: CreateUnitAt
#include "TrigEnum.h"          // ours: find_foreach_unit + FindUnitCb
#include "creep.h"             // ours: unit_type_spreads_creep / spread_creep_completely
#include "Teams.h"             // ours: GetNumTeams / IsFullPlayerTeam / IsComputerTeam
#include "net_slot.h"          // ours: is_race_selectable
#include "gametype.h"          // ours: IsScenarioGame / team_get_slot_captain
#include "statcmd.h"           // ours: statcmd_set_ctrl
#include "statbtn.h"           // ours: targeting_msg
#include "CUnitZSpells.h"      // ours: FindPlaceForUnit's seat
#include "sai_PathFindLocal.h" // ours: sub_4928F0
#undef mtx_query_place_bldg
#include "../gen/globals117.h"
#include "../port/render/port_view.h"  // Hyperion: g_portView, PortViewScreenToMap
#include "../port/data/port_moddata.h"  // Hyperion: ModRaceStartUnit
#include "../h/mutators.h"  // Hyperion: the start workers

// The cells go out unwidened, as in CUnitCOP.cpp's view.
// Hyperion: the definition (collide.cpp) reads the cell as ints, and this
// TU's callers pass it as 16 bits. The view takes the definition's types, which
// the x64 build needs, and the macro narrows the arguments as 1.17's did
//.
extern "C" int __fastcall mtx_query_place_bldg(CUnit *pUnit, BYTE bPlayer, int sX, int sY, int nType, int nSlot, int nArg6, int nArg7, int nArg8);
#define mtx_query_place_bldg(a0, a1, a2, a3, a4, a5, a6, a7, a8) (mtx_query_place_bldg)((a0), (a1), (short)(a2), (short)(a3), (a4), (a5), (a6), (a7), (a8))
#undef placebox_query_place

// The TU-local globals, dual-spelled, under the symbol sheet's names where it
// has them and positional names where it does not. word_6751F2, the unit type
// being placed, is sgwPlaceUnitType[1] seen on its own.

#ifdef OC_DLL_MODE
#define sgbPlaceAddon_g      (*reinterpret_cast<BYTE *>(OC_VA(0x6751E8u)))
#define sgbPlaceboxOrder_g   (*reinterpret_cast<BYTE *>(OC_VA(0x6751E9u)))
#define sgbPlaceboxPlyr_g    (*reinterpret_cast<BYTE *>(OC_VA(0x6751EAu)))
#define word_6751F2          (*reinterpret_cast<WORD *>(OC_VA(0x6751F2u)))
#define sgbPlaceOn           (*reinterpret_cast<int *>(OC_VA(0x6751F4u)))
#define sgbParentOn          (*reinterpret_cast<int *>(OC_VA(0x6751F8u)))
#define sgwCanPlace          (*reinterpret_cast<int *>(OC_VA(0x6751FCu)))
#define sgPlaceboxBmp_g      (reinterpret_cast<bmp_t *>(OC_VA(0x675200u)))
#define gwMapPixWidth        (*reinterpret_cast<WORD *>(OC_VA(0x543230u)))
#define gwMapPixHeight       (*reinterpret_cast<WORD *>(OC_VA(0x543234u)))
#define gpStatUnit           (*reinterpret_cast<CUnit **>(OC_VA(0x6AB4C4u)))
#define g_ActiveNationID     (*reinterpret_cast<DWORD *>(OC_VA(0x508D44u)))
#define sgszPlaceboxStrs_g   (reinterpret_cast<WORD *>(OC_VA(0x508D90u)))
#define gUnitAddOnOffset     (reinterpret_cast<point16_t *>(OC_VA(0x522AB8u)))
#else
extern BYTE      sgbPlaceAddon_g;      // 0x6751E8
extern BYTE      sgbPlaceboxOrder_g;   // 0x6751E9
extern BYTE      sgbPlaceboxPlyr_g;    // 0x6751EA
extern WORD      word_6751F2;          // 0x6751F2 -- carries the unit type
extern int       sgbPlaceOn;           // 0x6751F4
extern int       sgbParentOn;          // 0x6751F8
extern int       sgwCanPlace;          // 0x6751FC
extern bmp_t     sgPlaceboxBmp_g[2] OC_UNALIGNED;   // 0x675200
extern upos_t    gwMapPixWidth;        // 0x543230
extern upos_t    gwMapPixHeight;       // 0x543234
extern CUnit    *gpStatUnit;           // 0x6AB4C4
extern DWORD     g_ActiveNationID;     // 0x508D44
extern WORD      sgszPlaceboxStrs_g[];  // 0x508D90 -- placement error code -> string id
extern point16_t gUnitAddOnOffset[96] OC_UNALIGNED;  // 0x522AB8, indexed [wType - 106]
#endif

// 0x6751EC, the tile under the placement cursor. Only this TU's three functions
// touch it, so the proof arm is file-static; the payload keeps the oracle's storage.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgPlacePos_g         (*reinterpret_cast<point16_t *>(OC_VA(0x6751ECu)))
#else
static point16_t sgPlacePos_g;         // 0x6751EC
#endif

// sgPlaceboxBmp_g holds the box's two bitmaps, as placebox_free's loop bound
// shows. The two overlay images are _img_list[3] and [4], whose bitmaps and draw
// callback placebox_init sets; one img_t.
#define placebox_img (&_img_list[3])

// The bodies.

// placebox_is_on, oracle 0x47D070 (6 B)
int placebox_is_on()
{
  return sgbPlaceOn;
}

// KillUnitProc, oracle 0x47DEF0 (12 B)
// A find_foreach_unit callback; it returns an explicit 0, so BOOL.
BOOL KillUnitProc(CUnit *pUnit, void *pCtx)
{
  if (pUnit != NULL)
    pUnit->SelfDestruct();
  return FALSE;
}

// RectIsOnMap, oracle 0x47E300 (60 B)
// Whether the box is on the map, less the console strip at the bottom.
BOOL RectIsOnMap(const mrect_t *prc)
{
  if (prc->left >= 0 && prc->right < gwMapPixWidth &&
      prc->top >= 0 && prc->bottom < gwMapPixHeight - 0x20)
    return TRUE;
  return FALSE;
}

// CUnit::GetBounds and GetBounds_xy, inline here and in CBullet.cpp. The
// oracle inlines them in ResolveUnitCollision's first two walks and calls them
// by name in the others, so this TU emits both out-of-line copies.
OC_EMIT inline void CUnit::GetBounds(mrect_t *pBounds)
{
  spos_t y = this->pSprite->wPosY;
  spos_t x = this->pSprite->wPosX;
  const rect16_t *pBox = &gUnitBoundBox[this->wType];
  pBounds->top    = y - pBox->top;
  pBounds->bottom = y + pBox->bottom;
  pBounds->left   = x - pBox->left;
  pBounds->right  = x + pBox->right;
}

OC_EMIT inline void CUnit::GetBounds_xy(mrect_t *pBounds, spos_t x, spos_t y)
{
  const rect16_t *pBox = &gUnitBoundBox[this->wType];
  pBounds->top    = y - pBox->top;
  pBounds->bottom = y + pBox->bottom;
  pBounds->left   = x - pBox->left;
  pBounds->right  = x + pBox->right;
}

// Walks the ring's four edges in 8-pixel steps for a spot where pUnit fits.
// Needs GetBounds / GetBounds_xy visible as inline bodies: the inliner takes
// them in the first two walks and RectIsOnMap in the first three. The right
// edge is stored before the top, which gives x EBX and pBounds EDI.
int ResolveUnitCollision(CUnit *pUnit, mpoint_t *pOut, spos_t x, int y,
                          short w, int h, mrect_t *pBounds, int a6)
{
  mrect_t rcArea;  // Hyperion: boxes and positions of the map
  mrect_t rc;
  mrect_t rcHit;
  CUnit *pHit;
  int nStep;
  spos_t cx;
  spos_t cy;
  short nWidth = gUnitBoundBox[pUnit->wType].left + gUnitBoundBox[pUnit->wType].right + 1;

  rcArea.bottom = y + h;
  rcArea.right = x + w;
  rcArea.top = y & ~7;
  rcArea.left = x & ~7;
  rcArea.bottom = (rcArea.bottom + 7) & ~7;
  rcArea.right = (rcArea.right + 7) & ~7;
  if (rcArea.left < pBounds->left)
    rcArea.left = pBounds->left;
  if (rcArea.right > pBounds->right)
    rcArea.right = pBounds->right;
  if (rcArea.top < pBounds->top)
    rcArea.top = pBounds->top;
  if (rcArea.bottom > pBounds->bottom)
    rcArea.bottom = pBounds->bottom;

  cx = rcArea.left;
  if (w > 2 * nWidth)
    cx = rcArea.left + ((nWidth + 7) & ~7);

  pUnit->GetBounds_xy(&rc, cx, rcArea.bottom);
  while (cx <= rcArea.right) {
    if (RectIsOnMap(&rc)) {
      pHit = find_foreach_unit(&rc, reinterpret_cast<FindUnitCb>(SharesCollisionFn), pUnit);
      if (pHit == NULL) {
        if (sai_path_can_reach_target(pUnit, cx, rcArea.bottom)
            && ((pUnit->dwFlags & 4)
                || (mtx_passable(pUnit->wType, cx, rcArea.bottom)
                    && (a6 == 0 || !sub_4928F0(pUnit->wType, cx, rcArea.bottom, a6))))) {
          pOut->x = cx;
          pOut->y = rcArea.bottom;
          return TRUE;
        }
        nStep = 8;
      } else {
        // Merged arms under the pHit test above.
        if (pHit == NULL)
          pHit->GetBounds(&rcHit);
        else
          pHit->GetBounds(&rcHit);
        spos_t nLeft = rc.left;
        nStep = rcHit.right - nLeft + 1;
        if ((cx + nStep) & 7)
          nStep += 8 - ((cx + nStep) & 7);
      }
      cx += nStep;
      rc.left += nStep;
      rc.right += nStep;
    } else {
      cx += 8;
      rc.left += 8;
      rc.right += 8;
    }
  }

  cy = rcArea.bottom;
  pUnit->GetBounds_xy(&rc, rcArea.right, cy);
  while (cy >= rcArea.top) {
    if (RectIsOnMap(&rc)) {
      pHit = find_foreach_unit(&rc, reinterpret_cast<FindUnitCb>(SharesCollisionFn), pUnit);
      if (pHit == NULL) {
        if (sai_path_can_reach_target(pUnit, rcArea.right, cy)
            && ((pUnit->dwFlags & 4)
                || (mtx_passable(pUnit->wType, rcArea.right, cy)
                    && (a6 == 0 || !sub_4928F0(pUnit->wType, rcArea.right, cy, a6))))) {
          pOut->x = rcArea.right;
          pOut->y = cy;
          return TRUE;
        }
        nStep = 8;
      } else {
        pHit->GetBounds(&rcHit);
        nStep = rc.bottom - rcHit.top + 1;
        if ((cy - nStep) & 7)
          nStep += 8 - ((cy - nStep) & 7);
      }
      cy -= nStep;
      rc.top -= nStep;
      rc.bottom -= nStep;
    } else {
      cy -= 8;
      rc.top -= 8;
      rc.bottom -= 8;
    }
  }

  cx = rcArea.right;
  pUnit->GetBounds_xy(&rc, cx, rcArea.top);
  while (cx >= rcArea.left) {
    if (RectIsOnMap(&rc)) {
      pHit = find_foreach_unit(&rc, reinterpret_cast<FindUnitCb>(SharesCollisionFn), pUnit);
      if (pHit == NULL) {
        if (sai_path_can_reach_target(pUnit, cx, rcArea.top)
            && ((pUnit->dwFlags & 4)
                || (mtx_passable(pUnit->wType, cx, rcArea.top)
                    && (a6 == 0 || !sub_4928F0(pUnit->wType, cx, rcArea.top, a6))))) {
          pOut->x = cx;
          pOut->y = rcArea.top;
          return TRUE;
        }
        nStep = 8;
      } else {
        pHit->GetBounds(&rcHit);
        nStep = rc.right - rcHit.left + 1;
        if ((cx - nStep) & 7)
          nStep += 8 - ((cx - nStep) & 7);
      }
      cx -= nStep;
      rc.left -= nStep;
      rc.right -= nStep;
    } else {
      cx -= 8;
      rc.left -= 8;
      rc.right -= 8;
    }
  }

  cy = rcArea.top;
  pUnit->GetBounds_xy(&rc, rcArea.left, cy);
  while (cy <= rcArea.bottom) {
    if (!RectIsOnMap(&rc)) {
      cy += 8;
      rc.top += 8;
      rc.bottom += 8;
    } else {
      pHit = find_foreach_unit(&rc, reinterpret_cast<FindUnitCb>(SharesCollisionFn), pUnit);
      if (pHit == NULL) {
        if (sai_path_can_reach_target(pUnit, rcArea.left, cy)
            && ((pUnit->dwFlags & 4)
                || (mtx_passable(pUnit->wType, rcArea.left, cy)
                    && (a6 == 0 || !sub_4928F0(pUnit->wType, rcArea.left, cy, a6))))) {
          pOut->x = rcArea.left;
          pOut->y = cy;
          return TRUE;
        }
        nStep = 8;
      } else {
        pHit->GetBounds(&rcHit);
        nStep = rcHit.bottom - rc.top + 1;
        if ((cy + nStep) & 7)
          nStep += 8 - ((cy + nStep) & 7);
      }
      cy += nStep;
      rc.top += nStep;
      rc.bottom += nStep;
    }
  }

  return FALSE;
}

// Can the local player give this placement order to this unit? The build
// orders go through the tech tree, the two morph-in-place orders need the unit's
// own type, and 155 also needs the game clock under 600.
int placebox_validate(order_id_t bOrderID, WORD wUnitType, CUnit *pUnit)
{
  switch (bOrderID) {
  case 25: case 30: case 31: case 32: case 36:
    if (TTAllowed(wUnitType, g_ActiveNationID, pUnit) != 1)
      return 0;
    break;
  case 155:
    if (g_dwElapsedTime > 0x258)
      return 0;
  case 46: case 71:
    if (pUnit->wType != wUnitType)
      return 0;
    break;
  default:
    return 0;
  }
  return OrderAllowed(bOrderID, g_ActiveNationID, pUnit) == 1;
}

// sub_47DB60, oracle 0x47DB60 (25 B)
// placebox_validate for the box's order and unit type and the selected unit.
int sub_47DB60()
{
  return placebox_validate(sgbPlaceboxOrder_g, word_6751F2, gpStatUnit);
}

// placebox_free, oracle 0x47CE20 (49 B)
// Frees the two placebox bitmaps. The line number 578 is 1.17's.
void placebox_free()
{
  for (int i = 0; i < 2; ++i) {
    if (sgPlaceboxBmp_g[i].pPixels != NULL) {
      SMemFree(sgPlaceboxBmp_g[i].pPixels, "placebox.cpp", 578, 0);
      sgPlaceboxBmp_g[i].pPixels = NULL;
    }
  }
}

// placebox_init, oracle 0x47D000 (100 B)
// Zeroes the box's state and wires up its two overlay images. The player is set
// to -1, since 0 is a valid player.
void placebox_init()
{
  sgbParentOn = 0;
  sgbPlaceOn = 0;
  sgbPlaceboxPlyr_g = 0xFF;

  for (int i = 0; i < 2; ++i) {
    placebox_img[i].nX = 0;
    placebox_img[i].nY = 0;
    placebox_img[i].nWidth = 0;
    placebox_img[i].nHeight = 0;
    placebox_img[i].pBmp = &sgPlaceboxBmp_g[i];
    placebox_img[i].pfnUpdate = placebox_mask_rect;
    placebox_img[i].bFlags = 0;
  }
}

// is_placing_addon, oracle 0x47CD70 (8 B)
BOOL is_placing_addon()
{
  return sgbPlaceAddon_g;
}

// sub_47D290, oracle 0x47D290 (106 B)
// Ends a placement click: an error plays its cue and returns, else the
// placement cue plays and the box comes down. The error path reads the byte at
// address 0, a 1.17 defect kept as it is (docs/open-issues.md).
void sub_47D290(int nMode, BYTE bFlag)
{
  if (static_cast<BYTE>(nMode) == 0) {
    const int nErr = sgwCanPlace;
    if (nErr != 0) {
      if (*reinterpret_cast<const BYTE *>(0) == 0x47) {
        map_msg_target_self(status_get_str(0x35F), g_ActiveNationID);
        gamesnd_placebox_error(gpStatUnit);
      } else {
        gamesnd_placebox_error_msg(gpStatUnit, sgszPlaceboxStrs_g[nErr]);
      }
      return;
    }
    gamesnd_placebox(word_6751F2);
  }

  img_invalid(4);
  img_invalid(3);
  placebox_off();
}

// The placement command path and the geyser and addon highlighting. Every
// reference 1.17 bakes to gUnitAddOnOffset is biased by 106 records, since the
// table starts at the first addon type.

// placebox_show_geyser, oracle 0x47D8E0 (59 B)
// Highlights the geyser under the placement if a refinery is being placed. The
// store is a ternary, as 1.17's cmov is.
void placebox_show_geyser(CUnit *pUnit)
{
  if (IsBuiltRefineryType(word_6751F2)) {
    int nOk = pUnit->DisplayLandingOverlay(word_6751F2);
    sgbPlaceAddon_g = nOk ? 1 : sgbPlaceAddon_g;
  }
}

// placebox_highlight_geysers, oracle 0x47D190 (98 B)
// placebox_show_geyser on each of the neutral player's vespene geysers (0xBC),
// written out, as 1.17 inlines it.
void placebox_highlight_geysers()
{
  for (CUnit *pUnit = CUnit::GetPlayerUnitList(11); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    if (pUnit->wType == 0xBC) {
      if (IsBuiltRefineryType(word_6751F2)) {
        int nOk = pUnit->DisplayLandingOverlay(word_6751F2);
        sgbPlaceAddon_g = nOk ? 1 : sgbPlaceAddon_g;
      }
    }
  }
}
// Mark every free addon of the type being placed that could attach where it
// stands. The addon offset base 1.17 bakes is biased by 106 records. The type is
// a WORD local: an int one keeps nx in EBX and spills the type instead.
void placebox_highlight_addons()
{
  CUnit *pUnit;
  CSprite *pSprite;
  CUnit *pStat;
  WORD nType;
  int nx;
  int ny;
  WORD wPlaceType;

  for (pUnit = CUnit::GetPlayerUnitList(11); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    if ((gulUnitTypeFlags[pUnit->wType] & 2) && !pUnit->IsDeadOrDying()) {
      wPlaceType = word_6751F2;
      nType = pUnit->wType;
      if (TTBaseUnitType((WORD)nType) == wPlaceType) {
        pSprite = pUnit->pSprite;
        nx = PosU(pSprite->wPosX - gUnitPlaceboxSize[nType].x / 2 -
                    gUnitAddOnOffset[nType - 106].x);
        ny = PosU(pSprite->wPosY - gUnitPlaceboxSize[nType].y / 2 -
                    gUnitAddOnOffset[nType - 106].y);
        pStat = gpStatUnit;
        if (!sub_413150(pStat, pStat->bPlayer, PosS(nx) / 32,
                        (short)ny / 32, wPlaceType)) {
          int nOk = pUnit->DisplayLandingOverlay(word_6751F2);
          sgbPlaceAddon_g = nOk ? 1 : sgbPlaceAddon_g;
        }
      }
    }
  }
}

// sub_47CC80, oracle 0x47CC80 (239 B)
// Gives the unit its placement order: 0x47 and 0x9B, and 0x2E for type 0x86,
// straight away; a drone that is already building is left alone; any other
// order only when its cost can be paid, with the build queue reset to the type.
void sub_47CC80(order_id_t nArg0, CUnit *pUnit, mpoint_t pt, int nArg3)
{

  if ((BYTE)nArg0 == 0x47) {
    pUnit->SetOrder_xy(0x47, pt.x, pt.y);
    return;
  }

  if (pUnit->wType == 0x29) {
    if (pUnit->bOrderId == 0x46 && (pUnit->dwFlags & 0x3000))
      return;
    if (pUnit->bOrderId == 0x1A)
      return;
  }

  if ((pUnit->wType == 0x86 && (BYTE)nArg0 == 0x2E) || (BYTE)nArg0 == 0x9B) {
    pUnit->SetOrder_xy(nArg0, pt.x, pt.y);
    return;
  }

  if (bldg_get_unit_cost(pUnit->bPlayer, (WORD)nArg3, TRUE, TRUE)) {
    pUnit->SetOrder_xy(nArg0, pt.x, pt.y);
    if ((gulUnitTypeFlags[pUnit->wType] & 1) && (gulUnitTypeFlags[(WORD)nArg3] & 1)) {
      pUnit->RemoveAllBuildQueue();
      *reinterpret_cast<WORD *>(&pUnit->_unmapped_C4[0]) = (WORD)nArg3;
      return;
    }
    pUnit->RemoveAllBuildQueue();
    pUnit->AddBuildQueue(nArg3);
  }
}

// placebox_query_place_addon, oracle 0x47D620 (263 B)
// Queries both the addon's placement and its parent's; the two share one error
// block, which comes last here.
void placebox_query_place_addon(order_id_t cArg0, int nArg1, point16_t ptIn)
{
  CUnit *pUnit = select_get_first();

  int rc = mtx_query_place_bldg(pUnit, pUnit->bPlayer, ptIn.x, ptIn.y,
                                nArg1, 1, 0, 0, 0);
  if (rc == 0) {
    int nCellX = ptIn.x - gUnitAddOnOffset[(WORD)nArg1 - 106].x / 32;
    int nCellY = ptIn.y - gUnitAddOnOffset[(WORD)nArg1 - 106].y / 32;

    rc = mtx_query_place_bldg(pUnit, pUnit->bPlayer, (short)nCellX, (short)nCellY,
                              pUnit->wType, 0, 0, 0, 0);
    if (rc == 0) {
      mpoint_t  pt;  // Hyperion: a map point
      pt.x = PosS(gUnitPlaceboxSize[pUnit->wType].x / 2 + nCellX * 32);
      pt.y = PosS(gUnitPlaceboxSize[pUnit->wType].y / 2 + nCellY * 32);
      sub_47CC80(cArg0, pUnit, pt, nArg1);
      return;
    }
  }

  if (gPlayback == 0)
    map_msg_target_self(status_get_str(sgszPlaceboxStrs_g[rc]), g_ActiveNationID);
}

// placebox_query_place_bldg, oracle 0x47D730 (171 B)
// placebox_query_place_addon with one query. The placebox size is indexed by
// the type argument here and by pUnit->wType there, as in 1.17.
void placebox_query_place_bldg(order_id_t bArg0, int nArg1, point16_t ptIn)
{
  CUnit *pUnit = select_get_first();

  int rc = mtx_query_place_bldg(pUnit, pUnit->bPlayer, ptIn.x, ptIn.y,
                                nArg1, 1, 0, 0, 0);
  if (rc != 0) {
    if (gPlayback == 0)
      map_msg_target_self(status_get_str(sgszPlaceboxStrs_g[rc]), g_ActiveNationID);
    return;
  }

  mpoint_t  pt;
  pt.x = PosS(gUnitPlaceboxSize[(WORD)nArg1].x / 2 + ptIn.x * 32);
  pt.y = PosS(gUnitPlaceboxSize[(WORD)nArg1].y / 2 + ptIn.y * 32);
  sub_47CC80(bArg0, pUnit, pt, nArg1);
}

// Queries the placement for the unit type and posts the result to the status bar.
// The type is an int: the oracle zero-extends a copy for the flag test and
// forwards EDX whole. C linkage makes this and net_cmds.h's WORD view one symbol.
extern "C" void __fastcall placebox_query_place(order_id_t bOrderID, int nUnitType, point16_t pos)
{
  if (gulUnitTypeFlags[(WORD)nUnitType] & 2)
    placebox_query_place_addon(bOrderID, nUnitType, pos);
  else
    placebox_query_place_bldg(bOrderID, nUnitType, pos);
  status_set_update();
}

// placebox_gamemap_lclick, oracle 0x47CE60 (90 B)
// The box's left-click handler: takes the placement unless the click is on the
// console, and holds the cursor only when the placement produced a unit type.
void placebox_gamemap_lclick(void *pArg0)
{
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(pArg0);
  int nType = 0;
  BYTE bOrder = 0;

  if (console_collision(pEvent->nX, pEvent->nY))
    return;

  placebox_update();
  set_placebox(reinterpret_cast<intptr_t>(&nType), &bOrder, 0);
  if ((WORD)nType == 0xE4)
    return;

  send_net_cmd_Placebox(bOrder, (WORD)nType,
                        sgPlacePos_g);
  game_hold_cursor(0);
}
// More globals: the box's matrices, the cursor and the .dat tables.
// sgwPlaceUnitType is one WORD[2]: [1] is the type being placed, [0] its addon
// parent.

#ifdef OC_DLL_MODE
#define word_6751EE          (*reinterpret_cast<WORD *>(OC_VA(0x6751EEu)))
#define byte_675210          (reinterpret_cast<BYTE (*)[8][6]>(OC_VA(0x675210u)))
#define byte_675188          (reinterpret_cast<BYTE (*)[8][6]>(OC_VA(0x675188u)))
#define gMapCursor           (*reinterpret_cast<WORD *>(OC_VA(0x667288u)))
#define gwScreenTileX_g      (*reinterpret_cast<WORD *>(OC_VA(0x5264B0u)))
#define gwScreenTileY_g      (*reinterpret_cast<WORD *>(OC_VA(0x5264B2u)))
#define glScrollPixelX       (*reinterpret_cast<LONG *>(OC_VA(0x543218u)))
#define glScrollPixelY       (*reinterpret_cast<LONG *>(OC_VA(0x54321Cu)))
#define gCursorPos_g         (reinterpret_cast<LONG *>(OC_VA(0x6D3154u)))
#define _bmp_current         (*reinterpret_cast<void **>(OC_VA(0x6D0C40u)))
#define gubUnitFlingy        (reinterpret_cast<BYTE *>(OC_VA(0x51FAA8u)))
#define guwFlingySprite      (reinterpret_cast<WORD *>(OC_VA(0x510AE0u)))
#define guwSpriteImage       (reinterpret_cast<WORD *>(OC_VA(0x51EAA8u)))
#define gUnitPlaceboxSize    (reinterpret_cast<point16_t *>(OC_VA(0x522728u)))
#define gubUnitCountInfo     (reinterpret_cast<BYTE *>(OC_VA(0x523C40u)))
#else
extern WORD      word_6751EE;              // 0x6751EE  positional
extern BYTE      byte_675210[][8][6];      // 0x675210  sheet's positional name
extern BYTE      byte_675188[][8][6] OC_UNALIGNED;      // 0x675188  as src117/lang/collide.cpp
extern WORD      gMapCursor;               // 0x667288  sheet, game_evt.cpp
extern WORD      gwScreenTileX_g;          // 0x5264B0  UNREGISTERED
extern WORD      gwScreenTileY_g;          // 0x5264B2  UNREGISTERED
extern LONG      glScrollPixelX;           // 0x543218  registered `long`
extern LONG      glScrollPixelY;           // 0x54321C  registered `long`
extern LONG      gCursorPos_g[2];          // 0x6D3154  registered `long[2]`
extern void     *_bmp_current;             // 0x6D0C40  registered `void *`
extern flingy_id_t gubUnitFlingy[228] OC_UNALIGNED;       // 0x51FAA8  registered
extern WORD      guwFlingySprite[209] OC_UNALIGNED;     // 0x510AE0  registered
extern WORD      guwSpriteImage[517];      // 0x51EAA8  sheet, arr/sprites.cpp
extern point16_t gUnitPlaceboxSize[228] OC_UNALIGNED;   // 0x522728  registered
extern BYTE      gubUnitCountInfo[228] OC_UNALIGNED;    // 0x523C40  registered
#endif
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgwPlaceUnitType     (reinterpret_cast<WORD *>(OC_VA(0x6751F0u)))
#else
static WORD      sgwPlaceUnitType[2];      // 0x6751F0
#endif

// The box itself: creation, the shading matrix, the mask blit and the
// per-frame update.

// sub_47D530, oracle 0x47D530 (74 B)
// The addon and geyser highlight refresh, ending in two tail jumps. The type is
// read once into a WORD local.
void __fastcall sub_47D530()
{
  if (sgbParentOn != 0)
    return;

  if (sgbPlaceAddon_g != 0) {
    CImage::ClearPlaceboxes();
    sgbPlaceAddon_g = 0;
  }

  // The call takes the global and the index the local copy, which keeps the
  // type in ESI as in 1.17; the other spellings spill it or reload it.
  WORD wType = word_6751F2;
  if (IsBuiltRefineryType(word_6751F2))
    placebox_highlight_geysers();
  else if ((gubUnitCountInfo[wType] & 2) != 0)
    placebox_highlight_addons();
}

// placebox_off, oracle 0x47D580 (110 B)
// Tears the placement box down.
void placebox_off()
{
  sgbParentOn = 0;
  sgbPlaceOn = 0;
  statcmd_set_ctrl(0xE4);

  img_invalid(4);
  placebox_img[1].bInUse = 0;
  img_invalid(3);
  placebox_img[0].bInUse = 0;

  placebox_free();
  game_reset_handlers();
  targeting_msg(NULL);
  gMapCursor = 0;
  init_juice_matrix();

  if (sgbPlaceAddon_g != 0) {
    CImage::ClearPlaceboxes();
    sgbPlaceAddon_g = 0;
  }
}
// Snaps a pixel point to the 32x32 build grid inside the 640x400 play area, stores
// the tile in sgPlacePos_g and writes its top-left pixel back; 0 when the point is
// off the play area, 1 otherwise. sgPlacePos_g is file-static and its address is
// never taken, so the stores through pPos do not force the tile to be re-read.
// Hyperion: the play area is the map as drawn, the view's width by its
// map rows (port_view.cpp).
int placebox_pix_fix(void *pArg0)
{
  point16_t *pPos = reinterpret_cast<point16_t *>(pArg0);
  short sX = pPos->x;
  short sY = pPos->y;
  if (PosU(sX) >= g_portView.nWidth)
    return 0;
  if (PosU(sY) >= g_portView.nMapRows)
    return 0;
  // Hyperion: zoomed, the tile under the cursor through the camera, and its place
  // in the game view's terms as 1.17's (port_view.cpp)
  int nMapX, nMapY;
  PortViewScreenToMap(sX, sY, &nMapX, &nMapY);
  if (nMapX != sX + glScrollPixelX || nMapY != sY + glScrollPixelY) {
    if (nMapX < 0 || nMapY < 0 || nMapX >= gwMapWidth * 32 || nMapY >= gwMapHeight * 32)
      return 0;
    sgPlacePos_g.x = PosS(nMapX >> 5);
    sgPlacePos_g.y = PosS(nMapY >> 5);
    pPos->x = (short)(sgPlacePos_g.x * 32 - glScrollPixelX);
    pPos->y = (short)(sgPlacePos_g.y * 32 - glScrollPixelY);
    return 1;
  }
  sX = PosS((sX >> 5) + gwScreenTileX_g);
  sY = PosS((sY >> 5) + gwScreenTileY_g);
  sgPlacePos_g.x = sX;
  sgPlacePos_g.y = sY;
  pPos->x = (short)(sgPlacePos_g.x * 32 - glScrollPixelX);
  pPos->y = (short)(sgPlacePos_g.y * 32 - glScrollPixelY);
  if (pPos->x < 0) {
    pPos->x += 32;
    sgPlacePos_g.x++;
  }
  if (pPos->y < 0) {
    pPos->y += 32;
    sgPlacePos_g.y++;
  }
  return 1;
}

// placebox_mask_rect, oracle 0x47D200 (133 B)
// The box's draw callback, placebox_init's pfnUpdate for both overlay images.
// It masks the bitmap over the map as drawn; the destination offsets are
// max(-x, 0).
void __fastcall placebox_mask_rect(unsigned short, unsigned short, bmp_t *pBmp, bmpsrc_t *pSrc)
{
  const point16_t *pPos = reinterpret_cast<const point16_t *>(pSrc);
  short sX = pPos->x;
  short sY = pPos->y;
  bmpsrc_t src;
  int nDx, nDy;

  src.rc.left = sX;
  src.rc.top = sY;
  if (sX < 0)
    src.rc.left = 0;
  if (sY < 0)
    src.rc.top = 0;

  // Hyperion: the map as drawn, 0x280 by 0x190 in 1.17.
  src.nWidth = PosS(sX + g_portView.nWidth);
  src.nHeight = PosS(sY + g_portView.nMapRows);
  src.rc.right = PosS(src.nWidth - 1 + src.rc.left);
  src.rc.bottom = PosS(src.nHeight - 1 + src.rc.top);

  nDy = -(int)sY;
  if (nDy < 0)
    nDy = 0;
  nDx = -(int)sX;
  if (nDx < 0)
    nDx = 0;

  bmp_mask_rect((unsigned short)nDx, (unsigned short)nDy,
                pBmp, &src);
}

// SetRect comes from winuser.h, which this TU's headers pull in.

// placebox_draw_bldg, oracle 0x47CD80 (147 B)
// Draws the building's own sprite into the current bitmap, the buffer
// placebox_create has installed, through the units, flingy, sprites and images
// tables. Frame +0x01, which grp.h leaves unnamed, is the Y offset.
void placebox_draw_bldg(WORD wArg0)
{
  BYTE bOrder = sgbPlaceboxOrder_g;
  int nColor;
  PGROUP pGrp;
  FRAME *pFrame;
  int nHeight, nOffsX, nOffsY;
  RECT rc;

  if (bOrder == 0x47 || (bOrder == 0x24 && wArg0 == 0))
    nColor = gpStatUnit->pSprite->bCreator;
  else
    nColor = gpStatUnit->bPlayer;

  sub_41D740(nColor);

  pGrp = CImage::GetImageFromGroupIndex(
      guwSpriteImage[guwFlingySprite[gubUnitFlingy[wArg0]]]);

  // The frame pointer is named first and every read goes through it, which
  // keeps the height in ECX as 1.17 does.
  pFrame = &pGrp->frames[0];
  nHeight = pFrame->bHeight;
  nOffsX = pFrame->bOffsetX;
  nOffsY = pFrame->_unmapped_01;

  SetRect(&rc, 0, 0, pFrame->bWidth, nHeight);
  rle_draw_normal(nOffsX, nOffsY, pFrame, &rc, NULL);
}
// 0x47CEC0 placebox_create (258 B).
// Allocates and paints one of the two placebox layers: a bitmap the larger of
// the building's GRP extent and its placement box, rounded up to whole 32-pixel
// tiles with the 16-bit mask 0xFFE0, with the building drawn in, the shading
// matrix reset and the overlay image live. The line number 280 is 1.17's.
void placebox_create(int nArg0, WORD nArg1)
{
  WORD wType = nArg1;
  PGROUP pGrp;
  int nBoxW, nBoxH;
  WORD wW, wH = 0;
  bmp_t *pBmp;

  sgwPlaceUnitType[nArg0] = wType;

  pGrp = CImage::GetImageFromGroupIndex(
      guwSpriteImage[guwFlingySprite[gubUnitFlingy[wType]]]);

  nBoxW = gUnitPlaceboxSize[wType].x;
  nBoxH = gUnitPlaceboxSize[wType].y;

  // Read each GRP extent inside its own max; separate locals hoist both loads.
  wW = (WORD)(pGrp->wMaxWidth > nBoxW ? pGrp->wMaxWidth : nBoxW);
  wH = (WORD)(pGrp->wMaxHeight > nBoxH ? pGrp->wMaxHeight : nBoxH);

  wW = (WORD)((wW + 0x1F) & 0xFFE0);
  wH = (WORD)((wH + 0x1F) & 0xFFE0);
  pBmp = &sgPlaceboxBmp_g[nArg0];

  pBmp->nWidth = (short)wW;
  pBmp->nHeight = (short)wH;
  bmp_create(pBmp, "placebox.cpp", 280);

  _bmp_current = pBmp;
  memset(pBmp->pPixels, 0, wH * wW);
  placebox_draw_bldg((WORD)nArg1);

  memset(byte_675210[nArg0], 0x0C, 0x30);
  _bmp_current = NULL;

  placebox_img[nArg0].nHeight = (short)wH;
  placebox_img[nArg0].nWidth = (short)wW;
  placebox_img[nArg0].bInUse = 1;
}

// sgwPlaceUnitType is file-static.
//
// Asks the collision matrix whether the placement is legal, at the cursor and,
// when an addon's parent box is on, at the parent's tile, and returns the first
// non-zero answer. The addon offset is divided by 32, signed.
int sub_47DB80()
{
  int nRet = mtx_query_place_bldg(gpStatUnit, sgbPlaceboxPlyr_g,
                                  sgPlacePos_g.x, sgPlacePos_g.y,
                                  sgwPlaceUnitType[1], 1, 1, 0, 0);

  if (sgbParentOn != 0) {
    WORD wType = sgwPlaceUnitType[1];
    int nParent = mtx_query_place_bldg(
        gpStatUnit, sgbPlaceboxPlyr_g,
        (short)(sgPlacePos_g.x - gUnitAddOnOffset[wType - 106].x / 32),
        (short)(sgPlacePos_g.y - gUnitAddOnOffset[wType - 106].y / 32),
        sgwPlaceUnitType[0], 0, 1, 0, 0);
    if (nRet == 0)
      return nParent;
  }
  return nRet;
}

// Reading the type through this accessor loads sub_4928F0's type argument
// before x, as the oracle does.
static inline int UnitTypeOf(CUnit *pUnit)
{
  return pUnit->wType;
}

// The unit's collision box around (x, y). As a helper taking values, the
// inlined RectIsOnMap loads rc.right before gwMapPixWidth, as the oracle does.
static inline void UnitRectAt(mrect_t *prc, int nType, spos_t x, spos_t y)
{
  prc->top = y - gUnitBoundBox[nType].top;
  prc->bottom = gUnitBoundBox[nType].bottom + y;
  prc->left = x - gUnitBoundBox[nType].left;
  prc->right = gUnitBoundBox[nType].right + x;
}

// Finds a free spot for pUnit near pInput, inside pBounds or +-0x80 around the
// point, growing the search box by 0x10 a side until ResolveUnitCollision fits it.
// The x step is a WORD (ESI); the y step is an int homed in a4's slot, because
// two int steps merge into one induction variable.
BOOL __fastcall FindPlaceForUnit(CUnit *pUnit, mpoint_t *pInput, mpoint_t *pOutput,
                                 mrect_t *pBounds, int a4, int a5)
{
  mrect_t rcArea;  // Hyperion: boxes and positions of the map
  if (pBounds == NULL) {
    pBounds = &rcArea;
    rcArea.top = pInput->y - 0x80;
    rcArea.bottom = pInput->y + 0x7F;
    rcArea.left = pInput->x - 0x80;
    rcArea.right = pInput->x + 0x7F;
  }
  if (pBounds->top < 0)
    pBounds->top = 0;
  if (pBounds->bottom >= gwMapPixHeight - 0x20)
    pBounds->bottom = gwMapPixHeight - 0x21;
  if (pBounds->left < 0)
    pBounds->left = 0;
  if (pBounds->right >= gwMapPixWidth)
    pBounds->right = gwMapPixWidth - 1;

  mrect_t rc;
  UnitRectAt(&rc, UnitTypeOf(pUnit), pInput->x, pInput->y);
  CUnit *pHit = find_foreach_unit(&rc, SharesCollisionFn, pUnit);
  if (pHit == NULL && RectIsOnMap(&rc)) {
    if ((pUnit->dwFlags & 4)
        || (mtx_passable(UnitTypeOf(pUnit), pInput->x, pInput->y)
            && (a5 == 0 || !sub_4928F0(UnitTypeOf(pUnit), pInput->x, pInput->y, a5)))) {
      pOutput->x = pInput->x;
      pOutput->y = pInput->y;
      return TRUE;
    }
    if (a4 == 0) {
      guwCreateUnitErr = 0x3C;
      return FALSE;
    }
  }

  WORD wStepX = 8;
  WORD wStepY = 8;
  if (pHit != NULL) {
    int nW = gUnitBoundBox[UnitTypeOf(pHit)].right + gUnitBoundBox[UnitTypeOf(pUnit)].left + 2;
    int nH = gUnitBoundBox[UnitTypeOf(pHit)].bottom + gUnitBoundBox[UnitTypeOf(pUnit)].top + 2;
    wStepX = nW > 8 ? static_cast<WORD>(nW) : 8;
    wStepY = nH > 8 ? static_cast<WORD>(nH) : 8;
  }
  int nStepY = wStepY;
  int nSpanY = static_cast<WORD>(wStepY * 2);
  for (;;) {
    upos_t wX = pInput->x;
    if (PosS(wX) - static_cast<short>(wStepX) < pBounds->left
        && static_cast<short>(wStepX) + PosS(wX) > pBounds->right
        && pInput->y - static_cast<short>(nStepY) < pBounds->top
        && static_cast<short>(nStepY) + pInput->y > pBounds->bottom) {
      guwCreateUnitErr = 0x3C;
      return FALSE;
    }
    int nFound = ResolveUnitCollision(pUnit, pOutput, wX - wStepX,
                                      PosU(pInput->y - nStepY),
                                      wStepX * 2, nSpanY, pBounds, a5);
    nSpanY += 0x20;
    wStepX += 0x10;
    nStepY += 0x10;
    if (nFound)
      break;
  }
  return TRUE;
}

// 0x47D920. Repaints one layer's shading matrix: centres the tile grid in the
// bitmap, parks the overlay at the cursor, and re-shades each cell whose cached
// code (byte_675210) differs from the wanted one (byte_675188). The divisions
// are signed; arg1 is a point16_t * passed as an integer.
void placebox_update_mtx(int nArg0, intptr_t nArg1)
{
  const point16_t *pPt = reinterpret_cast<const point16_t *>(nArg1);
  WORD wType = sgwPlaceUnitType[nArg0];
  int nCellW = gUnitPlaceboxSize[wType].x / 32;
  int nCellH = gUnitPlaceboxSize[wType].y / 32;
  // Read after the cell sizes; read first, the index stays in EAX, not EBX.
  bmp_t *pBmp = &sgPlaceboxBmp_g[nArg0];
  int nOffX = (pBmp->nWidth - nCellW * 32) / 2;
  int nOffY = (pBmp->nHeight - nCellH * 32) / 2;
  int nY;
  int r, c;

  placebox_img[nArg0].nX = (short)(pPt->x - nOffX);
  placebox_img[nArg0].nY = (short)(pPt->y - nOffY);
  placebox_img[nArg0].bInUse = 1;

  nY = nOffY;
  for (r = 0; r < nCellH; ++r) {
    // The column x stays an expression: a running local seeds above the guard.
    for (c = 0; c < nCellW; ++c) {
      BYTE bWant = byte_675188[nArg0][r][c];
      if (byte_675210[nArg0][r][c] != bWant) {
        BYTE bPen = bWant == 0 ? sgbPens_g[0x13] : sgbPens_g[0x14];
        byte_675210[nArg0][r][c] = bWant;
        shade_mtx(nOffX + c * 32, nY, &sgPlaceboxBmp_g[nArg0], bPen);
      }
    }
    nY += 32;
  }
}

// placebox_update, oracle 0x47D7E0 (243 B)
//
// The per-frame tick while the box is up: snaps the mouse to the build grid,
// asks the matrix whether the placement is legal and repaints both layers, or
// clears them, with img_force, when the point has left the play area.
void placebox_update()
{
  point16_t pt;

  if (sgbPlaceOn == 0)
    return;

  pt.x = (short)gCursorPos_g[0];
  pt.y = (short)gCursorPos_g[1];

  if (placebox_pix_fix(&pt) == 0) {
    if (placebox_img[1].bInUse != 0) {
      memset(byte_675210[1], 0x0C, 0x30);
      placebox_img[1].bInUse = 0;
      img_force(4);
    }
    if (placebox_img[0].bInUse != 0) {
      memset(byte_675210[0], 0x0C, 0x30);
      placebox_img[0].bInUse = 0;
      img_force(3);
    }
    return;
  }

  sgwCanPlace = sub_47DB80();
  img_invalid(4);
  img_invalid(3);
  placebox_update_mtx(1, reinterpret_cast<intptr_t>(&pt));

  if (sgbParentOn != 0) {
    WORD wType = sgwPlaceUnitType[1];
    pt.x -= gUnitAddOnOffset[wType - 106].x;
    pt.y -= gUnitAddOnOffset[wType - 106].y;
    placebox_update_mtx(0, reinterpret_cast<intptr_t>(&pt));
  }

  img_invalid(4);
  img_invalid(3);
}

// More globals, the registered ones with the registry's types.
#ifdef OC_DLL_MODE
#define g_LocalNationID      (*reinterpret_cast<DWORD *>(OC_VA(0x508D50u)))
#define gulUnitTypeFlags     (reinterpret_cast<DWORD *>(OC_VA(0x521660u)))
#define gUnitBoundBox        (reinterpret_cast<rect16_t *>(OC_VA(0x522C38u)))
#define gStartLoc_534A00     (reinterpret_cast<point16_t *>(OC_VA(0x534A00u)))
#define dword_526490         (*reinterpret_cast<DWORD *>(OC_VA(0x526490u)))
#define byte_526494          (reinterpret_cast<BYTE *>(OC_VA(0x526494u)))
#define byte_5264A0          (reinterpret_cast<BYTE *>(OC_VA(0x5264A0u)))
#define gPlayerData          (reinterpret_cast<PlayerData *>(OC_VA(0x6726C8u)))
#define byte_672949          (*reinterpret_cast<BYTE *>(OC_VA(0x672949u)))
#define word_667288          (*reinterpret_cast<WORD *>(OC_VA(0x667288u)))
#else
extern DWORD     g_LocalNationID;      // 0x508D50  REGISTERED  DWORD
extern DWORD     gulUnitTypeFlags[228] OC_UNALIGNED;    // 0x521660  REGISTERED
extern rect16_t  gUnitBoundBox[228] OC_UNALIGNED;       // 0x522C38  sheet-named (arr/units.cpp);
                                       //   same spelling src117/lang/collide.cpp uses
// 0x534A00, the per-nation start locations, signed 16-bit pairs under a
// positional name.
extern PlayerTable<point16_t, PlayerTagStartLocations, 8> gStartLoc_534A00;
// 0x526490 / 0x526494 / 0x5264A0, PreparePlayerSlots' snapshot of the slot
// table, under positional names. The byte arrays are zeroed 12 long, though
// only [0..7] are filled.
extern DWORD      dword_526490;
extern BYTE       byte_526494[24] OC_UNALIGNED;
extern BYTE       byte_5264A0[12];
extern PlayerTable<PlayerData, PlayerTagData> gPlayerData;      // 0x6726C8  REGISTERED  PlayerData[8]
// 0x672949 -- &gGameHeader.bScenarioFlag (0x6728D0 + 0x79, src117/h/gametype.h).
// Spelled positionally here because gGameHeader itself is declared per-TU.
extern BYTE       byte_672949;
extern WORD       word_667288;         // 0x667288  sheet: gMapCursor (game_evt.cpp)
#endif

// Starting-unit placement, the placebox on/off entry points and the collision
// predicate. The race tables are BYTEs, as every arm's `mov r8, imm8` shows.

// Snapshots each slot's type and race into the two rows of the slot table.
// Both rows are addressed off byte_526494; a second base rebases the index to 0.
void PreparePlayerSlots()
{
  dword_526490 = g_LocalNationID;
  memset(byte_526494, 0, 12);
  memset(byte_5264A0, 0, 12);

  int i = 8;
  do {
    --i;
    byte_526494[i] = gPlayerData[i].nType;
    byte_526494[i + 12] = gPlayerData[i].nRace;
  } while (i != 0);
}

// PlaceLarva, oracle 0x47E020 (94 B). Spawns one larva beside a starting
// hatchery. The spawn point is a DWORD pair on the stack.
void PlaceLarva(CUnit *pUnit)
{
  DWORD dwPt[2];
  if (pUnit->sub_43D360(dwPt) == 0)
    return;
  CUnit *pLarva = CreateUnitAt(0x23, dwPt[0], dwPt[1], pUnit->bPlayer);
  if (pLarva == NULL)
    return;
  pLarva->FinishedMaking();
  pLarva->pUnion80 = pUnit;
  pLarva->sub_43D990();
  if (pUnit->dwUnion134 != 0)
    town_move_morphed_building(pUnit, pLarva);
}

// PlaceOverlord, oracle 0x47E080 (122 B). An overlord beside the start
// location, a hatchery's size (gUnitBoundBox[131]) towards the map's middle.
void PlaceOverlord(BYTE bArg0)
{
  int nW = static_cast<short>(gUnitBoundBox[131].left + gUnitBoundBox[131].right + 1);
  int nH = static_cast<short>(gUnitBoundBox[131].top + gUnitBoundBox[131].bottom + 1);

  int nX = gStartLoc_534A00[bArg0].x;
  int nY = gStartLoc_534A00[bArg0].y;

  if (nX < gwMapPixWidth / 2)
    nX += nW;
  else
    nX -= nW;

  if (nY < gwMapPixHeight / 2)
    nY += nH;
  else
    nY -= nH;

  CUnit *pUnit = CreateUnitAt(0x2A, nX, nY, bArg0);
  if (pUnit != NULL)
    pUnit->sub_424DB0();
}

// The race -> "main building" map. A BYTE, not a WORD: every arm materialises
// `mov <r8>, imm8` and the consumer widens with `movzx`.
static __forceinline BYTE PlaceCenterType(BYTE bRace)
{
  switch (bRace) {
    case 0:  return 131;    // Zerg Hatchery
    case 1:  return 106;    // Terran Command Center
    case 2:  return 154;    // Protoss Nexus
    default: return 228;    // None
  }
}
// Places the race's main building at the player's start location, after
// killing whatever stands in its placement box.
//
// Hyperion: the body is PlaceStartBuilding's, which places any building there, a
// mod's race's start buildings too.
void PlaceCenter(char cArg0, BYTE bArg1)
{
  PlaceStartBuilding(PlaceCenterType(cArg0), bArg1);
}

void PlaceStartBuilding(int nType, BYTE bArg1)
{

  int nHalfW = gUnitPlaceboxSize[nType].x / 2;
  WORD wX = gStartLoc_534A00[bArg1].x;
  wX = ((wX - nHalfW) & 0xFFE0) + nHalfW;

  int nHalfH = gUnitPlaceboxSize[nType].y / 2;
  WORD wY = gStartLoc_534A00[bArg1].y;
  wY = ((wY - nHalfH) & 0xFFE0) + nHalfH;

  mrect_t  rc;
  rc.left   = PosS(wX - nHalfW);
  rc.top    = PosS(wY - nHalfH);
  rc.right  = PosS(wX + nHalfW - 1);
  rc.bottom = PosS(wY + nHalfH - 1);
  find_foreach_unit(&rc, KillUnitProc, NULL);

  CUnit *pUnit = CreateUnitAt(nType, wX, wY, bArg1);
  if (pUnit == NULL)
    return;

  pUnit->sub_424DB0();
  if (unit_type_spreads_creep(pUnit->wType, 1) ||
      (gulUnitTypeFlags[pUnit->wType] & 0x20000) != 0) {
    CSprite *pSpr = pUnit->pSprite;
    spread_creep_completely(pUnit->wType, PosS(pSpr->wPosX), pSpr->wPosY);
    PlaceLarva(pUnit);
    PlaceLarva(pUnit);
  }
}

// set_placebox, oracle 0x47D300 (158 B).
// Ends a placement like sub_47D290, also reporting the order and the unit type,
// 0xE4 on an error. The first argument is a WORD *. pbOut is read with no null
// check on the error path, as in 1.17.
void set_placebox(intptr_t a1, BYTE *pbOut, BYTE a3)
{
  WORD *pwOut = reinterpret_cast<WORD *>(a1);

  if (pbOut != NULL)
    *pbOut = sgbPlaceboxOrder_g;

  if (a3 == 0) {
    if (sgwCanPlace != 0) {
      if (*pbOut == 0x47) {
        map_msg_target_self(status_get_str(0x35F), g_ActiveNationID);
        gamesnd_placebox_error(gpStatUnit);
      } else {
        gamesnd_placebox_error_msg(gpStatUnit, sgszPlaceboxStrs_g[sgwCanPlace]);
      }
      if (pwOut != NULL)
        *pwOut = 0xE4;
      return;
    }
    gamesnd_placebox(word_6751F2);
  }

  if (pwOut != NULL)
    *pwOut = word_6751F2;
  img_invalid(4);
  img_invalid(3);
  placebox_off();
}

// placebox_on, oracle 0x47D3A0 (238 B).
// Puts the cursor into building-placement mode. gpStatUnit is re-read, not
// cached, as in 1.17.
void placebox_on(BYTE bCursor, WORD wUnitType)
{
  sgbPlaceOn = 1;
  statcmd_set_ctrl(0xE6);
  sgwCanPlace = 0;
  sgbPlaceboxOrder_g = bCursor;
  placebox_create(1, wUnitType);

  if ((gulUnitTypeFlags[wUnitType] & 2) != 0) {
    placebox_create(0, gpStatUnit->wType);
    sgbParentOn = 1;
  } else {
    // The call takes the global and the index takes the local, as in
    // sub_47D530; passing the local round-trips it through a frame slot.
    WORD wType = word_6751F2;
    if (IsBuiltRefineryType(word_6751F2))
      placebox_highlight_geysers();
    else if ((gubUnitCountInfo[wType] & 2) != 0)
      placebox_highlight_addons();
  }

  targeting_msg(error_get_str(0x40));
  game_reset_handlers();

  if (gpStatUnit != NULL) {
    if (gpStatUnit->GetRace() == 2)
      ShowPowerTemplates();
  }
  if (gpStatUnit != NULL)
    sgbPlaceboxPlyr_g = gpStatUnit->bPlayer;
  else
    sgbPlaceboxPlyr_g = 0xFF;
  placebox_update();
  word_667288 = 0;
}
// The three unit-type pairs that may overlap. Reading pA->wType at each test,
// rather than passing the type in, keeps a failed 0x72 group falling into the
// 0x9B test as the oracle does instead of being threaded past it.
static __forceinline BOOL SharesTypeCollision(CUnit *pA, WORD wTypeB)
{
  if (pA->wType == 0x72 && (wTypeB == 0x08 || wTypeB == 0x0B || wTypeB == 0x09 ||
                            wTypeB == 0x0C || wTypeB == 0x3A))
    return 1;
  if (pA->wType == 0x9B && (wTypeB == 0x45 || wTypeB == 0x54))
    return 1;
  if (pA->wType == 0xA7 && (wTypeB == 0x46 || wTypeB == 0x48 || wTypeB == 0x47 ||
                            wTypeB == 0x3C))
    return 1;
  return 0;
}

// Whether two units may overlap: never if A is dead or either has flag 0x200000;
// if B's type has flag 0x800, only when A has bit 2 or its type has 0x800 too;
// else for the listed type pairs, or when bit 2 of both flag words agrees.
int SharesCollisionFn(CUnit *pUnitA, void *pCtx)
{
  CUnit *pUnitB = static_cast<CUnit *>(pCtx);
  if (pUnitA->IsDead())
    return 0;

  DWORD dwFlagsA = pUnitA->dwFlags;
  if ((dwFlagsA & 0x200000) == 0) {
    DWORD dwFlagsB = pUnitB->dwFlags;
    if ((dwFlagsB & 0x200000) != 0)
      return 0;
    WORD wTypeB = pUnitB->wType;
    if ((gulUnitTypeFlags[wTypeB] & 0x800) != 0) {
      if ((dwFlagsA & 2) != 0 || (gulUnitTypeFlags[pUnitA->wType] & 0x800) != 0)
        return 1;
      return 0;
    }
    if (SharesTypeCollision(pUnitA, wTypeB))
      return 1;
    return ~((dwFlagsA ^ dwFlagsB) >> 2) & 1;
  }
  return 0;
}

// The race -> "first worker" map, a BYTE for the same reason as
// PlaceCenterType's.
static __forceinline BYTE PlaceWorkerType(BYTE bRace)
{
  switch (bRace) {
    case 0:  return 0x29;   // Zerg Drone
    case 1:  return 0x07;   // Terran SCV
    case 2:  return 0x40;   // Protoss Probe
    default: return 0xE4;   // None
  }
}

// TeamPlaceStartingUnits, oracle 0x47EB00 (280 B).
// Places each full or computer team's start units at its captain's location.
// The base slot is a BYTE product, and the else arm's `bBase + 1` is 1.17's.
void TeamPlaceStartingUnits()
{
  for (int nTeam = 1; nTeam <= GetNumTeams(); ++nTeam) {
    if (!IsFullPlayerTeam(static_cast<BYTE>(nTeam)) &&
        !IsComputerTeam(static_cast<BYTE>(nTeam)))
      continue;

    BYTE bCaptain = team_get_slot_captain(static_cast<BYTE>(nTeam));
    BYTE bBase = static_cast<BYTE>(GetMaxPlayersPerTeam() * (nTeam - 1));

    if (byte_672949 == 2) {
      PlaceCenter(byte_5264A0[bBase], bCaptain);
      if (byte_5264A0[bBase] == 0)
        PlaceOverlord(bCaptain);
    }

    // Hyperion: 1.17's four workers, or a mutator's number
    for (int i = 0; i < MutatorWorkers(); ++i) {
      int nRem = i % GetMaxPlayersPerTeam();
      BYTE bRace;
      if (bBase + nRem >= 8)
        bRace = byte_5264A0[bBase + 1];
      else
        bRace = byte_5264A0[bBase + nRem];

      // A switch on the two live game modes, which 1.17 folds into one range
      // test.
      BYTE bMode = byte_672949;
      switch (bMode) {
      case 1:
      case 2: {
        CUnit *pUnit = CreateUnitAt(PlaceWorkerType(bRace),
                                    gStartLoc_534A00[bCaptain].x,
                                    gStartLoc_534A00[bCaptain].y,
                                    bCaptain);
        if (pUnit != NULL)
          pUnit->sub_424DB0();
        break;
      }
      default:
        break;
      }
    }
  }
}

// Hyperion: a mod's race's own start units, in place of its base's
// (port_moddata.h's ModRaceStartUnit): its buildings, with bCenter, where PlaceCenter puts
// the town hall, and its other units at the start location as the workers are.
static void PlaceRaceStart(BYTE bSlot, bool bCenter)
{
  int nType, nCount;
  for (int i = 0; ModRaceStartUnit(bSlot, i, &nType, &nCount); ++i) {
    const bool bBuilding = (gulUnitTypeFlags[nType] & 1) != 0;
    for (int k = 0; k < nCount && (bCenter || !bBuilding); ++k) {
      if (bBuilding) {
        PlaceStartBuilding(nType, bSlot);
        continue;
      }
      CUnit *pUnit = CreateUnitAt(nType, gStartLoc_534A00[bSlot].x, gStartLoc_534A00[bSlot].y, bSlot);
      if (pUnit != NULL)
        pUnit->sub_424DB0();
    }
  }
}

// PlaceStartingUnits, oracle 0x47E100 (399 B).
// Places each human and computer slot's start units, walking the slots down.
void PlaceStartingUnits()
{
  // Hyperion: 8 slots in 1.17, every lobby slot on a Hyperion map
  int i = PlayerLobbySlots();
  do {
    --i;
    const PlayerData *p = &gPlayerData[i];
    if (i == PLAYER_NEUTRAL)
      continue;

    BYTE bSlot = static_cast<BYTE>(i);
    BYTE bType = p->nType;
    if (bType == 2 || bType == 1) {
      BYTE bMode = byte_672949;
      if (IsScenarioGame(NULL) && is_race_selectable(bSlot))
        bMode = 2;

      // Hyperion: a mod's race with its own start units places them
      if (ModRaceOwnStart(bSlot) && (bMode == 1 || bMode == 2)) {
        PlaceRaceStart(bSlot, bMode == 2);
        continue;
      }
      switch (bMode) {
      case 2:
        PlaceCenter(p->nRace, bSlot);
        if (p->nRace == 0)
          PlaceOverlord(bSlot);
        // Falls through to the workers, as in 1.17.
      case 1: {
        // Hyperion: 1.17's four, written out there, or a mutator's number
        for (int k = 0; k < MutatorWorkers(); ++k) {
          CUnit *pUnit = CreateUnitAt(PlaceWorkerType(p->nRace),
                                      gStartLoc_534A00[bSlot].x, gStartLoc_534A00[bSlot].y, bSlot);
          if (pUnit != NULL)
            pUnit->sub_424DB0();
        }
        break;
      }
      default:
        break;
      }
    }
  } while (i != 0);
}
