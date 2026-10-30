// repulse.cpp -- the air-unit repulsion field: flyer counts per 48-pixel chunk
// and the drift that pushes stacked flyers apart.
//
// Bodies are in ascending oracle address, 1.17's own order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/repulse.h"
#include "../port/replays/port_snap.h"  // Hyperion: the trainer's snapshots hold it
#include <stdlib.h>
#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/CFlingy.h"          // AngleBetween
#include "../h/TrigAct.h"          // net_rand
#include "../h/rect16.h"
#include "../h/Storm.h"            // SMemAlloc, SMemFree
#include "../gen/globals117.h"
#include "../h/maplimits.h"  // Hyperion: maps past 256 tiles

// The globals this TU reaches, each spelled once for the standalone build and
// once for the payload.

// Per-slot drift steps, indexed by the high nibble of the repulse flags.
#ifdef OC_DLL_MODE
#define sgRepulseDirs_g (reinterpret_cast<int (*)[2]>(OC_VA(0x508E68u)))
#else
extern int sgRepulseDirs_g[16][2] OC_UNALIGNED;
#endif
// Flyer counts, one byte per 48-pixel chunk, 171 chunks square.
#ifdef OC_DLL_MODE
#define sgpRpMap (*reinterpret_cast<BYTE **>(OC_VA(0x67CF60u)))
#else
extern BYTE *sgpRpMap;
#endif
// Eight direction-remap rows of 32; the lookup runs over the whole block.
#ifdef OC_DLL_MODE
#define sgbRepulseDirections_g (reinterpret_cast<BYTE (*)[32]>(OC_VA(0x67CF68u)))
#else
extern BYTE sgbRepulseDirections_g[8][32] OC_UNALIGNED;
#endif
// The staged flingy position in 1/256 pixel.
#ifdef OC_DLL_MODE
#define sgnStagedDX_g (*reinterpret_cast<LONG *>(OC_VA(0x543290u)))
#else
extern LONG sgnStagedDX_g;
#endif
#ifdef OC_DLL_MODE
#define sgnStagedDY_g (*reinterpret_cast<LONG *>(OC_VA(0x543294u)))
#else
extern LONG sgnStagedDY_g;
#endif

// The unit's repulse bytes at +0x14C..+0x14F, inside CUnit::_unmapped_138.
#define REPULSE_FLAGS(u)   ((u)->_unmapped_138[0x14])
#define REPULSE_DIR(u)     ((u)->_unmapped_138[0x15])
#define REPULSE_CHUNK_X(u) ((u)->_unmapped_138[0x16])
#define REPULSE_CHUNK_Y(u) ((u)->_unmapped_138[0x17])

#define RP_MAP_WIDTH 171

// Hyperion: on a map past 256 tiles a side (8,192 pixels) a chunk's coordinates
// pass a byte, so the grid's row is the largest map's and each unit's chunk is kept here
// too, by the unit's index in the unit table. 1.17's maps read the bytes and the row of
// 171 as 1.17 did.
#define RP_MAP_WIDTH_MAX (MAP_SIDE_MAX * 32 / 48 + 1)
extern CUnit sgUnitsMem[POOL_UNITS_STORE];
static WORD g_repulseChunk[POOL_UNITS_STORE][2];
PORT_SNAP_KEEP_POOL(g_repulseChunk, g_pools.units);  // Hyperion: the trainer's snapshots hold it

static __forceinline bool RepulseWide() {
  return gwMapPixWidth > 8192 || gwMapPixHeight > 8192;
}

static __forceinline int RepulseChunkX(CUnit *pUnit) {
  return RepulseWide() ? g_repulseChunk[pUnit - sgUnitsMem][0] : REPULSE_CHUNK_X(pUnit);
}

static __forceinline int RepulseChunkY(CUnit *pUnit) {
  return RepulseWide() ? g_repulseChunk[pUnit - sgUnitsMem][1] : REPULSE_CHUNK_Y(pUnit);
}

static __forceinline BYTE *RepulseCell(CUnit *pUnit) {
  if (RepulseWide())
    return &sgpRpMap[RepulseChunkY(pUnit) * RP_MAP_WIDTH_MAX + RepulseChunkX(pUnit)];
  return &sgpRpMap[REPULSE_CHUNK_Y(pUnit) * RP_MAP_WIDTH + REPULSE_CHUNK_X(pUnit)];
}

// Turns the drift direction toward the move target and reflects it off the
// map edges. The direction is an int masked to a byte, which keeps the 32-bit
// xor/inc negation.
void CUnit::UpdateRepulseDirection() {
  int nX = PosS(pSprite->wPosX);
  int nY = pSprite->wPosY;
  int nDir = REPULSE_DIR(this);
  int nTargetX = PosS(wMoveTargetX);
  int nTargetY = PosS(wMoveTargetY);
  if (nX != nTargetX || nY != nTargetY) {
    int nAngle = AngleBetween(nX, nY, nTargetX, nTargetY);
    nDir = sgbRepulseDirections_g[0][(nDir - nAngle) & 0xFF] + nAngle;
  }
  nDir &= 0xFF;
  rect16_t *pBox = &gUnitBoundBox[wType];
  if (nX < pBox->left + 32) {
    if ((char)nDir < 0)
      nDir = (nDir ^ 0xFF) + 1;
    REPULSE_FLAGS(this) &= 0x0F;
  } else if (nX > gwMapPixWidth - pBox->right - 33) {
    if ((char)nDir >= 0)
      nDir = (nDir ^ 0xFF) + 1;
    REPULSE_FLAGS(this) &= 0x0F;
  }
  if (nY < pBox->top + 32) {
    nDir += 0x40;
    if ((char)nDir >= 0)
      nDir = (nDir ^ 0xFF) + 1;
    nDir -= 0x40;
    REPULSE_FLAGS(this) &= 0x0F;
  } else if (nY > gwMapPixHeight - pBox->bottom - 33) {
    nDir += 0x40;
    if ((char)nDir < 0)
      nDir = (nDir ^ 0xFF) + 1;
    nDir -= 0x40;
    REPULSE_FLAGS(this) &= 0x0F;
  }
  REPULSE_DIR(this) = nDir;
}

// Moves the unit to its current chunk and, when bSnap is set, snaps a near
// move target onto the staged position.
void CUnit::FinishRepulse(int bSnap) {
  if (!(dwFlags & 0x20000) || (gulUnitTypeFlags[wType] & 1) || wType == 0x49)
    return;
  CSprite *pSpr = pSprite;
  // The chunk test is held in a bool; tested in place, the staged Y compare
  // below loads the global before the member.
  bool bMoved = RepulseChunkX(this) != PosS(pSpr->wPosX) / 48 ||  // Hyperion
                RepulseChunkY(this) != pSpr->wPosY / 48;
  if (bMoved) {
    RemoveFromRepulseChunk();
    AddToRepulseChunk();
  }
  if (bSnap && abs(PosS(wMoveTargetX) - PosS(sgwStagedX_g)) < 24 &&
      abs(PosS(wMoveTargetY) - PosS(sgwStagedY_g)) < 24) {
    wMoveTargetX = sgwStagedX_g;
    wNextWaypointX = sgwStagedX_g;
    wMoveTargetY = sgwStagedY_g;
    wNextWaypointY = sgwStagedY_g;
  }
}

// Frees the chunk counts.
void repulse_free() {
  if (sgpRpMap) {
    SMemFree(sgpRpMap, "repulse.cpp", 0x13B, 0);
    sgpRpMap = NULL;
  }
}

// Fills the direction-remap rows.
void repulse_init_directions() {
  for (unsigned int i = 0; i < 32; i++) {
    sgbRepulseDirections_g[0][i] = i + 0x20;
    sgbRepulseDirections_g[1][i] = i + 0x20;
    sgbRepulseDirections_g[2][i] = i + 0x20;
    sgbRepulseDirections_g[3][i] = i + 0x20;
    sgbRepulseDirections_g[4][i] = i - 0x40;
    sgbRepulseDirections_g[5][i] = i - 0x40;
    sgbRepulseDirections_g[6][i] = i - 0x40;
    sgbRepulseDirections_g[7][i] = i - 0x40;
  }
}

// Allocates the chunk counts.
void repulse_init() {
  sgpRpMap = static_cast<BYTE *>(
      SMemAlloc(RP_MAP_WIDTH_MAX * RP_MAP_WIDTH_MAX, "repulse.cpp", 0x143, 8));  // Hyperion: 171 x 171 in 1.17
}

// Counts a flyer into its chunk, giving it a random drift slot the first time.
void CUnit::AddToRepulseChunk() {
  if (!(dwFlags & 0x20000) || (gulUnitTypeFlags[wType] & 1) || wType == 0x49)
    return;
  if (!(REPULSE_FLAGS(this) & 0xF0)) {
    unsigned int nRand = net_rand(0x25);
    REPULSE_DIR(this) = nRand;
    int nHi = (nRand >> 8) & 0xF0;
    if (!nHi)
      nHi = 0xF0;
    REPULSE_FLAGS(this) |= nHi;
  }
  REPULSE_FLAGS(this) &= 0xF8;
  CSprite *pSpr = pSprite;
  REPULSE_CHUNK_X(this) = PosS(pSpr->wPosX) / 48;
  REPULSE_CHUNK_Y(this) = pSpr->wPosY / 48;
  g_repulseChunk[this - sgUnitsMem][0] = PosU(PosS(pSpr->wPosX) / 48);  // Hyperion
  g_repulseChunk[this - sgUnitsMem][1] = PosU(pSpr->wPosY / 48);
  BYTE *pCell = RepulseCell(this);
  if (*pCell < 0xFF)
    (*pCell)++;
}

// Counts a flyer out of its chunk.
void CUnit::RemoveFromRepulseChunk() {
  if (!(dwFlags & 0x20000) || (gulUnitTypeFlags[wType] & 1) || wType == 0x49)
    return;
  BYTE *pCell = RepulseCell(this);  // Hyperion
  if (*pCell)
    (*pCell)--;
}

// Adds this frame's drift to the staged position when the unit shares its
// chunk; returns 1 when it did.
int CUnit::ProgressRepulse() {
  DWORD dwUnitFlags = dwFlags;
  if (!(dwUnitFlags & 0x20000) || (gulUnitTypeFlags[wType] & 1) || wType == 0x49 ||
      (dwUnitFlags & 0x3000))
    return 0;
  BYTE bCount = *RepulseCell(this);  // Hyperion
  if (bCount <= 1) {
    REPULSE_FLAGS(this) &= 0xF8;
    return 0;
  }
  BYTE bFlags = REPULSE_FLAGS(this);
  if (bFlags & 7) {
    REPULSE_FLAGS(this) = bFlags - 1;
  } else {
    REPULSE_FLAGS(this) = bFlags | 7;
    UpdateRepulseDirection();
  }
  BYTE bDir = REPULSE_DIR(this);
  int nDX = DIRECTIONS[bDir * 2] / 2;
  int nDY = DIRECTIONS[bDir * 2 + 1] / 2;
  if (AtMoveTarget()) {
    int nScale = (BYTE)((bCount >> 2) + 1);
    nDX = nDX * nScale / 2;
    nDY = nDY * nScale / 2;
  }
  int *pStep = sgRepulseDirs_g[REPULSE_FLAGS(this) >> 4];
  int nStepX = pStep[0];
  int nStepY = pStep[1];
  if (nDX > nStepX * 4)
    nDX -= nStepX;
  else if (nDX > 0)
    nDX += nStepX;
  else if (nDX < -nStepX * 4)
    nDX += nStepX;
  else if (nDX < 0)
    nDX -= nStepX;
  if (nDY > nStepY * 4)
    nDY -= nStepY;
  else if (nDY > 0)
    nDY += nStepY;
  else if (nDY < -nStepY * 4)
    nDY += nStepY;
  else if (nDY < 0)
    nDY -= nStepY;
  sgnStagedDX_g += nDX;
  sgnStagedDY_g += nDY;
  sgwStagedX_g = (WORD)(sgnStagedDX_g >> 8);
  sgwStagedY_g = (WORD)(sgnStagedDY_g >> 8);
  return 1;
}
