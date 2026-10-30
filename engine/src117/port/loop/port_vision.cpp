// port_vision.cpp -- vision, detection and parasite for the slots past 7
// (playervision.h). The tile sets are sized for the largest map and made on the
// first game that has such slots; the sprite and unit sets follow 1.17's tables.
#include <windows.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "../../h/playervision.h"
#include "../platform/port_log.h"  // PortLogger
#include "../../h/point16.h"
#include "../../h/playertables.h"
#include "../../h/maplimits.h"
#include "../../h/poollimits.h"
#include "CSprite.h"
#include "CUnit.h"
#include "../../h/saveload.h"  // compress, decompress
#include "../../gen/globals117.h"

extern CSprite sgSprites[POOL_SPRITES_STORE];
extern CUnit sgUnitsMem[POOL_UNITS_STORE];

#include "../replays/port_snap.h"
#include "port_workers.h"
#include "../../h/CThingy.h"  // tiles_seen_mask

namespace {

enum { SPRITES = POOL_SPRITES_STORE, UNITS = POOL_UNITS_STORE };

PlayerSet *g_pHidden;      // slot p's bit set: the tile is hidden from slot p (p from 8)
PlayerSet *g_pUnexplored;  // slot p has not explored it
PlayerSet g_spriteSeen[SPRITES];
PlayerSet g_unitParasite[UNITS];
PlayerSet g_unitMyopia[UNITS];
PlayerSet g_unitDetect[UNITS];               // the slots past 31 that detect the unit
PlayerSet g_playerVision[PLAYER_SLOTS_MAX];  // the slots past 31 a player sees for
PlayerSet g_viewHigh;                        // the replay viewer's slots past 7
// the trainer's snapshots hold the game's sets (port_snap.cpp)
PORT_SNAP_KEEP_POOL(g_spriteSeen, g_pools.sprites);
// Whether the viewer's slots past 7 see each sprite, (g_spriteSeen[i] & g_viewHigh).Any(),
// kept as the sets change (VisionHxSpriteSet) and made whole when they or the viewer are set
// (ViewSeenAll). The draw asks it of every sprite of every row on every pass, 80 passes a game
// frame drawn flat out; through the 32-byte sets that was 6.8% of the game's thread on a
// 64-player replay zoomed out.
unsigned char g_spriteViewSeen[SPRITES];
PORT_SNAP_KEEP_POOL(g_spriteViewSeen, g_pools.sprites);
PORT_SNAP_KEEP_POOL(g_unitParasite, g_pools.units);
PORT_SNAP_KEEP_POOL(g_unitMyopia, g_pools.units);
PORT_SNAP_KEEP_POOL(g_unitDetect, g_pools.units);
PORT_SNAP_KEEP(g_playerVision);

// 1.17's DWORDs hold slots 0 to 31; the port's records hold the rest.
const int DWORD_SLOTS = 32;

int Tiles() {
  return static_cast<int>(gwMapWidth) * gwMapHeight;
}

// A sprite or unit outside the game's pools has no set.
int SpriteIndex(const CSprite *pSprite) {
  const ptrdiff_t n = pSprite - sgSprites;
  return n >= 0 && n < g_pools.sprites ? static_cast<int>(n) : -1;
}

int UnitIndex(const CUnit *pUnit) {
  const ptrdiff_t n = pUnit - sgUnitsMem;
  return n >= 0 && n < g_pools.units ? static_cast<int>(n) : -1;
}

// The slots past 31 of a set, with 1.17's DWORD's.
PlayerSet Widen(ULONG dw, const PlayerSet &qHigh) {
  PlayerSet q = qHigh;
  q.w[0] = (q.w[0] & ~0xFFFFFFFFull) | dw;
  return q;
}

// The sets of the game's sprites and units, as a save holds them. A build of no more than
// 32 slots keeps a unit's parasites and blinders in 32 bits, as before stage D.
int SpriteSetBytes() {
  return g_pools.sprites * static_cast<int>(sizeof(PlayerSet));
}

#if !PLAYER_SETS_WIDE
ULONG g_unitSetDwords[UNITS];

int UnitDwords(const PlayerSet *pSets, void *pFile, bool bSave) {
  const int nBytes = g_pools.units * static_cast<int>(sizeof(ULONG));
  if (bSave) {
    for (int i = 0; i < g_pools.units; i++)
      g_unitSetDwords[i] = pSets[i].Low32();
    return compress(g_unitSetDwords, nBytes, pFile);
  }
  if (!decompress(g_unitSetDwords, nBytes, pFile))
    return 0;
  PlayerSet *p = const_cast<PlayerSet *>(pSets);
  for (int i = 0; i < g_pools.units; i++)
    p[i] = PlayerSet::FromBits(g_unitSetDwords[i]);
  return 1;
}
#endif

int UnitSets(const PlayerSet *pSets, void *pFile, bool bSave) {
#if PLAYER_SETS_WIDE
  const int nBytes = g_pools.units * static_cast<int>(sizeof(PlayerSet));
  return bSave ? compress(const_cast<PlayerSet *>(pSets), nBytes, pFile)
               : decompress(const_cast<PlayerSet *>(pSets), nBytes, pFile);
#else
  return UnitDwords(pSets, pFile, bSave);
#endif
}

}  // namespace

static void ViewSeenAll(void);

void VisionHxInit() {
  memset(g_spriteSeen, 0, sizeof g_spriteSeen);
  memset(g_spriteViewSeen, 0, sizeof g_spriteViewSeen);
  memset(g_unitParasite, 0, sizeof g_unitParasite);
  memset(g_unitMyopia, 0, sizeof g_unitMyopia);
  memset(g_unitDetect, 0, sizeof g_unitDetect);
  if (!g_hxPlayers)
    return;
  if (g_pHidden == NULL) {
    g_pHidden = new PlayerSet[MAP_TILES_MAX];
    g_pUnexplored = new PlayerSet[MAP_TILES_MAX];
    PortSnapStatic(g_pHidden, sizeof(PlayerSet) * MAP_TILES_MAX);
    PortSnapStatic(g_pUnexplored, sizeof(PlayerSet) * MAP_TILES_MAX);
  }
  const PlayerSet qHigh = PlayerSetHigh();
  for (int i = 0; i < MAP_TILES_MAX; i++)
    g_pHidden[i] = g_pUnexplored[i] = qHigh;
}

void VisionHxHideAll() {
  if (!g_hxPlayers || g_pHidden == NULL)
    return;
  const int n = Tiles();
  const PlayerSet qHigh = PlayerSetHigh();
  for (int i = 0; i < n; i++)
    g_pHidden[i] = qHigh;
}

void VisionHxReveal(bool bExplored) {
  if (!g_hxPlayers || g_pHidden == NULL)
    return;
  const int n = Tiles();
  for (int i = 0; i < n; i++) {
    g_pHidden[i] = PlayerSet::None();
    if (bExplored)
      g_pUnexplored[i] = PlayerSet::None();
  }
}

PlayerSet *VisionHxHidden() {
  return g_hxPlayers ? g_pHidden : NULL;
}

PlayerSet *VisionHxUnexplored() {
  return g_hxPlayers ? g_pUnexplored : NULL;
}

bool VisionHxTileSeen(int nTile, int nSlot) {
  if (nSlot < PLAYER_LOBBY_117)
    return (gpSquareMap[nTile] & (1u << nSlot)) == 0;
  return g_hxPlayers && g_pHidden != NULL && !g_pHidden[nTile].Has(nSlot);
}

bool VisionHxTileExplored(int nTile, int nSlot) {
  if (nSlot < PLAYER_LOBBY_117)
    return (gpSquareMap[nTile] & (1u << (nSlot + 8))) == 0;
  return g_hxPlayers && g_pUnexplored != NULL && !g_pUnexplored[nTile].Has(nSlot);
}

void VisionHxSpriteSet(const CSprite *pSprite, const PlayerSet &q) {
  const int i = SpriteIndex(pSprite);
  if (i < 0)
    return;
  const PlayerSet qOld = g_spriteSeen[i];
  g_spriteSeen[i] = q & PlayerSetHigh();
  g_spriteViewSeen[i] = g_viewHigh.Any() && (g_spriteSeen[i] & g_viewHigh).Any();
  // 1.17's byte redraws the sprite when the viewer's own slots change (SetMask); a change
  // in the viewer's slots past 7 alone, with the byte unseen, does here.
  if (g_viewHigh.Any() && !(pSprite->bVisibilityFlags & static_cast<BYTE>(gdwReplayVisionMask))) {
    const bool bWas = (qOld & g_viewHigh).Any();
    const bool bIs = (g_spriteSeen[i] & g_viewHigh).Any();
    if (bWas != bIs)
      SpriteViewRedraw(const_cast<CSprite *>(pSprite), bIs);
  }
}

PlayerSet VisionHxSprite(const CSprite *pSprite) {
  const int i = SpriteIndex(pSprite);
  return i >= 0 ? g_spriteSeen[i] : PlayerSet::None();
}

PlayerSet VisionHxRect(int nX, int nY, int nW, int nH, bool bExplored) {
  const PlayerSet *p = bExplored ? g_pUnexplored : g_pHidden;
  if (!g_hxPlayers || p == NULL)
    return PlayerSet::None();
  // the slots that see any tile: what is not hidden on every tile, so the tiles' sets are
  // ANDed and inverted once rather than each inverted and ORed
  PlayerSet qHidden = PlayerSet::All();
  for (int r = nY; r < nY + nH; r++) {
    const PlayerSet *pRow = p + r * gwMapWidth;
    for (int c = nX; c < nX + nW; c++)
      qHidden &= pRow[c];
  }
  return ~qHidden & PlayerSetHigh();
}

namespace {

// The circle walk's fog reads made ahead (playervision.h). The movement walk notes each unit
// and its rectangle of tiles, in its own order, which is the circle walk's; the pool reads
// them from the back of the list while the circle walk goes from the front, reading in place
// what is not ready, until the two meet. The pool reads only the rectangles and the fog, not
// the units and sprites the walk writes: a line of those read on another core would have to
// come back to the game's before each write, which cost more than the reads it saved.
struct AheadRect {
  short x, y, w, h;
};
struct AheadRead {
  std::atomic<DWORD> dwStamp;  // the walk it was read for, stored after the rest; 0 none
  BYTE bSeen;                  // tiles_seen_mask's byte
  PlayerSet qHigh;             // VisionHxRect's set, in a game past 8 slots
};
std::vector<const CUnit *> g_aheadUnits;  // the units noted, in the walk's order
std::vector<AheadRect> g_aheadRects;      // their rectangles
std::vector<int> g_aheadAt;               // by a unit's index, its place in them; -1 none
std::unique_ptr<AheadRead[]> g_aheadReads;
size_t g_nAheadReads;
// the last place the circle walk has told the pool it reached, on a line of its own, told
// every 32 units so the pool's threads reading it do not take the line from the game's
alignas(64) std::atomic<int> g_nWalkAt;
alignas(64) int g_nWalkTold;
const int kAheadPart = 128;
std::function<void(int)> g_aheadFn;
PortWorkersBatch *g_pAheadBatch;
DWORD g_dwAheadStamp;
bool g_bAheadOpen;       // the circle walk is under way with reads made ahead
int g_nAheadMode = -1;   // 0 off, 1 on, 2 on and checked against reads in place
size_t g_nAheadFewest;   // the fewest units for the pool to read
size_t g_nAheadSeen;     // the units the movement walk has visited since the last circle walk
bool g_bAheadNoting;     // the last walk had enough of them, so this one notes each
unsigned long long g_nAheadWalks, g_nAheadTaken, g_nAheadDiffer;

int AheadMode() {
  if (g_nAheadMode < 0) {
    const char *pszOn = getenv("HYPERION_VISION_AHEAD");
    const char *pszCheck = getenv("HYPERION_VISION_AHEAD_CHECK");
    const char *pszFewest = getenv("HYPERION_VISION_AHEAD_FEWEST");
    if ((pszOn != NULL && atoi(pszOn) == 0) || PortWorkersCount() <= 1)
      g_nAheadMode = 0;
    else
      g_nAheadMode = pszCheck != NULL && atoi(pszCheck) != 0 ? 2 : 1;
    g_nAheadFewest = g_nAheadMode == 2 ? 1 : pszFewest != NULL ? strtoul(pszFewest, NULL, 10) : 4096;
  }
  return g_nAheadMode;
}

const PortLogger AheadLog("hyperion_vision_ahead.log", PortLogger::SHARED);

}  // namespace

void VisionHxCircleNote(const CUnit *pUnit, const point16_t *pSizes) {
  if (AheadMode() == 0)
    return;
  ++g_nAheadSeen;
  if (!g_bAheadNoting)
    return;
  const int nIndex = UnitIndex(pUnit);
  const CSprite *pSprite = pUnit->pSprite;
  int x, y, w, h;
  if (nIndex < 0 || pSprite == NULL || (pSprite->flags.b & 0x20) ||
      !VisionTileRect(PosS(pSprite->wPosX), PosS(pSprite->wPosY), pSizes[pUnit->wType].x, pSizes[pUnit->wType].y,
                      gwMapWidth, gwMapHeight, &x, &y, &w, &h))
    return;
  if (g_aheadAt.size() <= static_cast<size_t>(nIndex))
    g_aheadAt.resize(g_pools.units > nIndex ? g_pools.units : nIndex + 1, -1);
  g_aheadAt[nIndex] = static_cast<int>(g_aheadUnits.size());
  g_aheadUnits.push_back(pUnit);
  const AheadRect r = {static_cast<short>(x), static_cast<short>(y), static_cast<short>(w), static_cast<short>(h)};
  g_aheadRects.push_back(r);
}

void VisionHxCirclesBegin() {
  g_bAheadOpen = false;
  // Fewer units read in place unless checked: waking the pool costs about 80 us a frame, repaid
  // only past about 4,000 units. Below that the movement walk only counts the units.
  const int nMode = AheadMode();
  const size_t n = g_aheadUnits.size();
  const size_t nSeen = g_nAheadSeen;
  g_nAheadSeen = 0;
  g_bAheadNoting = nSeen >= g_nAheadFewest;
  if (nMode == 0 || n == 0 || nSeen < g_nAheadFewest) {
    g_aheadUnits.clear();
    g_aheadRects.clear();
    return;
  }
  if (g_nAheadReads < n) {
    g_aheadReads.reset(new AheadRead[n]);
    g_nAheadReads = n;
    for (size_t i = 0; i < n; i++)
      g_aheadReads[i].dwStamp.store(0, std::memory_order_relaxed);
  }
  if (++g_dwAheadStamp == 0) {
    for (size_t i = 0; i < g_nAheadReads; i++)
      g_aheadReads[i].dwStamp.store(0, std::memory_order_relaxed);
    g_dwAheadStamp = 1;
  }
  g_nWalkAt.store(-1, std::memory_order_relaxed);
  g_nWalkTold = -1;
  const DWORD dwStamp = g_dwAheadStamp;
  const bool bHigh = g_hxPlayers && g_pHidden != NULL;
  const int nUnits = static_cast<int>(n);
  const int nParts = (nUnits + kAheadPart - 1) / kAheadPart;
  // the parts from the back, each from its own end, each read stored with its stamp last;
  // what the walk has reached is left to it
  g_aheadFn = [nUnits, nParts, dwStamp, bHigh](int k) {
    const int nFrom = (nParts - 1 - k) * kAheadPart;
    int i = nFrom + kAheadPart < nUnits ? nFrom + kAheadPart : nUnits;
    while (--i >= nFrom && i > g_nWalkAt.load(std::memory_order_relaxed)) {
      const AheadRect r = g_aheadRects[i];
      AheadRead &a = g_aheadReads[i];
      a.bSeen = static_cast<BYTE>(tiles_seen_mask(r.x, r.y, r.w, r.h));
      a.qHigh = bHigh ? VisionHxRect(r.x, r.y, r.w, r.h, false) : PlayerSet::None();
      a.dwStamp.store(dwStamp, std::memory_order_release);
    }
  };
  g_pAheadBatch = PortWorkersPost(nParts, g_aheadFn);
  g_bAheadOpen = true;
}

void VisionHxCirclesEnd() {
  if (!g_bAheadOpen)
    return;
  g_bAheadOpen = false;
  // the pool's threads stop at their next unit
  g_nWalkAt.store(INT_MAX, std::memory_order_relaxed);
  PortWorkersJoin(g_pAheadBatch);
  g_pAheadBatch = NULL;
  g_aheadUnits.clear();
  g_aheadRects.clear();
  if (g_nAheadMode == 2 && ++g_nAheadWalks % 1000 == 0) {
    char sz[160];
    snprintf(sz, sizeof sz, "walks %llu, read ahead and checked %llu, differ %llu\n", g_nAheadWalks,
             g_nAheadTaken, g_nAheadDiffer);
    AheadLog.Line(sz);
  }
}

bool VisionHxCircleRead(const void *pThingy, int nX, int nY, int nW, int nH, unsigned char *pbSeen,
                        PlayerSet *pqHigh) {
  if (!g_bAheadOpen)
    return false;
  // a thingy outside the unit table, or not noted in this walk, has none
  const int nIndex = UnitIndex(static_cast<const CUnit *>(pThingy));
  if (nIndex < 0 || static_cast<size_t>(nIndex) >= g_aheadAt.size())
    return false;
  const int nAt = g_aheadAt[nIndex];
  if (nAt < 0 || static_cast<size_t>(nAt) >= g_aheadUnits.size() || g_aheadUnits[nAt] != pThingy)
    return false;
  if (nAt >= g_nWalkTold + 32) {
    g_nWalkTold = nAt;
    g_nWalkAt.store(nAt, std::memory_order_relaxed);
  }
  const AheadRect &r = g_aheadRects[nAt];
  if (r.x != nX || r.y != nY || r.w != nW || r.h != nH)
    return false;
  const AheadRead &a = g_aheadReads[nAt];
  if (a.dwStamp.load(std::memory_order_acquire) != g_dwAheadStamp)
    return false;
  if (g_nAheadMode == 2) {
    // read in place too, and keep the reads in place
    ++g_nAheadTaken;
    const BYTE bSeen = static_cast<BYTE>(tiles_seen_mask(nX, nY, nW, nH));
    const bool bHigh = g_hxPlayers && g_pHidden != NULL;
    const PlayerSet qHigh = bHigh ? VisionHxRect(nX, nY, nW, nH, false) : PlayerSet::None();
    if (bSeen != a.bSeen || (bHigh && memcmp(&qHigh, &a.qHigh, sizeof qHigh) != 0)) {
      if (++g_nAheadDiffer <= 50) {
        char sz[160];
        snprintf(sz, sizeof sz, "differ: unit %d at tiles %d,%d %dx%d: seen %02X ahead %02X\n", nIndex, nX, nY,
                 nW, nH, bSeen, a.bSeen);
        AheadLog.Line(sz);
      }
    }
    return false;
  }
  *pbSeen = a.bSeen;
  *pqHigh = a.qHigh;
  return true;
}

static void ViewSeenAll(void) {
  for (int i = 0; i < SPRITES; i++)
    g_spriteViewSeen[i] = g_viewHigh.Any() && (g_spriteSeen[i] & g_viewHigh).Any();
}

void ViewHxReset(bool bPlayback) {
  g_viewHigh = PlayerSet::None();
  if (bPlayback && g_hxPlayers)
    for (int p = PLAYER_LOBBY_117; p < PlayerSlots(); p++)
      if (PlayerIsLobby(p))
        g_viewHigh.Add(p);
  ViewSeenAll();
}

bool ViewHxTileSeen(int nTile) {
  return g_viewHigh.Any() && g_pHidden != NULL && (~g_pHidden[nTile] & g_viewHigh).Any();
}

bool ViewHxTileExplored(int nTile) {
  return g_viewHigh.Any() && g_pUnexplored != NULL && (~g_pUnexplored[nTile] & g_viewHigh).Any();
}

bool ViewHxSpriteSeen(const CSprite *pSprite) {
  const int i = SpriteIndex(pSprite);
  const bool bSeen = i >= 0 && g_spriteViewSeen[i] != 0;
  // HYPERION_VIEW_SEEN_CHECK=1 asks the sets as well, and writes a difference to
  // hyperion_view_seen.log
  static const bool s_bCheck = getenv("HYPERION_VIEW_SEEN_CHECK") != NULL;
  if (s_bCheck) {
    static unsigned long s_nAsked, s_nDiffer;
    ++s_nAsked;
    const bool bSets = g_viewHigh.Any() && (VisionHxSprite(pSprite) & g_viewHigh).Any();
    if (bSets != bSeen || (s_nAsked & 0xFFFFF) == 0) {
      s_nDiffer += bSets != bSeen;
      static const PortLogger s_log("hyperion_view_seen.log", PortLogger::SHARED);
      s_log("asked %lu, differ %lu%s", s_nAsked, s_nDiffer, bSets != bSeen ? " (sprite differs)" : "");
    }
  }
  return bSeen;
}

bool ViewHxDetected(const CUnit *pUnit) {
  return g_viewHigh.Any() && (UnitDetection(pUnit) & g_viewHigh).Any();
}

bool ViewHxUnitSeen(const CUnit *pUnit) {
  if (!g_viewHigh.Any())
    return false;
  PlayerSet q = VisionHxSprite(pUnit->pSprite) & g_viewHigh;
  if (pUnit->dwFlags & 0x300)
    q &= UnitDetection(pUnit);
  return q.Any();
}

bool SpriteSeenBy(const CSprite *pSprite, int nSlot) {
  if (nSlot < PLAYER_LOBBY_117)
    return (pSprite->bVisibilityFlags & (1u << nSlot)) != 0;
  return g_hxPlayers && VisionHxSprite(pSprite).Has(nSlot);
}

PlayerSet VisionHxParasite(const CUnit *pUnit) {
  const int i = UnitIndex(pUnit);
  return g_hxPlayers && i >= 0 ? g_unitParasite[i] : PlayerSet::None();
}

PlayerSet UnitParasite(const CUnit *pUnit) {
  return PlayerSet::FromBits(pUnit->bParasiteFlags) | VisionHxParasite(pUnit);
}

void UnitParasiteAdd(CUnit *pUnit, int nSlot) {
  if (nSlot < PLAYER_LOBBY_117) {
    pUnit->bParasiteFlags |= static_cast<BYTE>(1 << nSlot);
    return;
  }
  const int i = UnitIndex(pUnit);
  if (g_hxPlayers && i >= 0)
    g_unitParasite[i].Add(nSlot);
}

PlayerSet UnitMyopia(const CUnit *pUnit) {
  const int i = UnitIndex(pUnit);
  PlayerSet q = PlayerSet::FromBits(pUnit->bMyopiaPlayers);
  if (g_hxPlayers && i >= 0)
    q |= g_unitMyopia[i];
  return q;
}

void UnitMyopiaAdd(CUnit *pUnit, int nSlot) {
  if (nSlot < PLAYER_LOBBY_117) {
    pUnit->bMyopiaPlayers |= static_cast<BYTE>(1 << nSlot);
    return;
  }
  const int i = UnitIndex(pUnit);
  if (g_hxPlayers && i >= 0)
    g_unitMyopia[i].Add(nSlot);
}

void UnitHxClear(CUnit *pUnit) {
  const int i = UnitIndex(pUnit);
  if (i >= 0)
    g_unitParasite[i] = g_unitMyopia[i] = PlayerSet::None();
}

void UnitHxMerge(CUnit *pUnit, const CUnit *pOther) {
  const int i = UnitIndex(pUnit);
  if (i < 0)
    return;
  g_unitParasite[i] |= VisionHxParasite(pOther);
  const int j = UnitIndex(pOther);
  if (g_hxPlayers && j >= 0)  // and the blinders, as 1.17 merges bUnion123 (stage D)
    g_unitMyopia[i] |= g_unitMyopia[j];
}

PlayerSet UnitDetection(const CUnit *pUnit) {
  const int i = UnitIndex(pUnit);
  const bool bHigh = g_hxPlayers && i >= 0 && pUnit->dwUnionE4 != 0x80000000u;
  return Widen(pUnit->dwUnionE4, bHigh ? g_unitDetect[i] : PlayerSet::None());
}

void UnitDetectionSet(CUnit *pUnit, const PlayerSet &q) {
  pUnit->dwUnionE4 = q.Low32();
  const int i = UnitIndex(pUnit);
  if (g_hxPlayers && i >= 0)
    g_unitDetect[i] = q;
}

// A unit not yet worked out (0x80000000) is detected by none of the slots past 31, as by
// none of 0 to 30 in 1.17.
bool UnitDetectedBy(const CUnit *pUnit, int nSlot) {
  if (nSlot < DWORD_SLOTS)
    return (pUnit->dwUnionE4 >> nSlot & 1) != 0;
  const int i = UnitIndex(pUnit);
  return g_hxPlayers && i >= 0 && pUnit->dwUnionE4 != 0x80000000u && nSlot < PLAYER_SLOTS_MAX &&
         g_unitDetect[i].Has(nSlot);
}

PlayerSet PlayerVision(int nSlot) {
  const bool bHigh = g_hxPlayers && nSlot >= 0 && nSlot < PLAYER_SLOTS_MAX;
  return Widen(SharedVision[nSlot], bHigh ? g_playerVision[nSlot] : PlayerSet::None());
}

void PlayerVisionAdd(int nSlot, int nOther) {
  if (nOther >= DWORD_SLOTS && nOther < PLAYER_SLOTS_MAX && nSlot >= 0 && nSlot < PLAYER_SLOTS_MAX)
    g_playerVision[nSlot].Add(nOther);
  else
    SharedVision[nSlot] |= 1ul << (nOther & 31);
}

void PlayerVisionRemove(int nSlot, int nOther) {
  if (nOther >= DWORD_SLOTS && nOther < PLAYER_SLOTS_MAX && nSlot >= 0 && nSlot < PLAYER_SLOTS_MAX)
    g_playerVision[nSlot].Remove(nOther);
  else
    SharedVision[nSlot] &= ~(1ul << (nOther & 31));
}

void PlayerVisionOnly(int nSlot) {
  SharedVision[nSlot] = 0;
  g_playerVision[nSlot] = PlayerSet::None();
  PlayerVisionAdd(nSlot, nSlot);
}

// A save of a Hyperion game (port_hxsave.cpp): the sets of the map's tiles, then each
// sprite's and unit's, in the order of the pools, whose slots the save keeps; in a build
// with slots past 31 then the units' detection and the players' shared vision past 31.
int VisionHxSave(void *pFile) {
  if (g_pHidden == NULL)
    return 0;
  const int nBytes = Tiles() * static_cast<int>(sizeof(PlayerSet));
  return compress(g_pHidden, nBytes, pFile) && compress(g_pUnexplored, nBytes, pFile)
      && compress(g_spriteSeen, SpriteSetBytes(), pFile) && UnitSets(g_unitParasite, pFile, true)
      && UnitSets(g_unitMyopia, pFile, true)
#if PLAYER_SETS_WIDE
      && compress(g_unitDetect, g_pools.units * static_cast<int>(sizeof(PlayerSet)), pFile)
      && compress(g_playerVision, sizeof g_playerVision, pFile)
#endif
      ;
}

int VisionHxLoad(void *pFile) {
  if (g_pHidden == NULL)
    return 0;
  const int nBytes = Tiles() * static_cast<int>(sizeof(PlayerSet));
  const int nOk = decompress(g_pHidden, nBytes, pFile) && decompress(g_pUnexplored, nBytes, pFile)
      && decompress(g_spriteSeen, SpriteSetBytes(), pFile) && UnitSets(g_unitParasite, pFile, false)
      && UnitSets(g_unitMyopia, pFile, false)
#if PLAYER_SETS_WIDE
      && decompress(g_unitDetect, g_pools.units * static_cast<int>(sizeof(PlayerSet)), pFile)
      && decompress(g_playerVision, sizeof g_playerVision, pFile)
#endif
      ;
  ViewSeenAll();
  return nOk;
}
