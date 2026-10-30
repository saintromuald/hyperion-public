// playervision.h -- vision, detection and parasite for the slots past 7.
//
// 1.17 keeps vision 8 bits a player: a tile's DWORD has bits 0 to 7 (hidden from slot p)
// and 8 to 15 (not explored by slot p), a sprite's bVisibilityFlags one bit a slot, a
// unit's parasite byte one bit a caster. In a game with slots past 7 (g_hxPlayers) the
// bits of slots 8 and up live here, by index in 1.17's tables: a set a tile (hidden,
// unexplored), a sprite and a unit, whose low 8 bits are 1.17's own. 1.17's DWORDs of
// players, a unit's detection (CUnit +0xE4) and a player's shared vision, hold slots 0 to
// 31 as they are; the slots past 31 are kept here, and the accessors read and write both.
#ifndef PLAYERVISION_H
#define PLAYERVISION_H

#include "playerset.h"

class CSprite;
class CUnit;

// Tiles. Each is a no-op, or reads as 1.17's bits only, while g_hxPlayers is off.
void VisionHxInit();                   // the map's start: hidden and unexplored (mask_init)
void VisionHxHideAll();                // the sight refresh's reset (sub_46C180)
void VisionHxReveal(bool bExplored);   // the fog cheats (mask_disable_fog)
PlayerSet *VisionHxHidden();           // the hidden sets, a tile each, or NULL
PlayerSet *VisionHxUnexplored();
bool VisionHxTileSeen(int nTile, int nSlot);      // the tile is not hidden from the slot
bool VisionHxTileExplored(int nTile, int nSlot);

// Sprites: the bits of slots 8 and up, set where 1.17 sets bVisibilityFlags.
void VisionHxSpriteSet(const CSprite *pSprite, const PlayerSet &q);
PlayerSet VisionHxSprite(const CSprite *pSprite);
// The OR of the tiles' seen (or explored) sets over a rectangle of tiles, as 1.17's
// tiles_seen_mask and sub_418D00 make the low byte.
PlayerSet VisionHxRect(int nX, int nY, int nW, int nH, bool bExplored);
// Whether the sprite is seen by the slot: 1.17's byte for slots 0 to 7.
bool SpriteSeenBy(const CSprite *pSprite, int nSlot);

// The fog reads of a frame's circle walk, made ahead on the worker threads.
// CUnit::EntityUpdate notes each unit and its rectangle of
// tiles; between VisionHxCirclesBegin and VisionHxCirclesEnd the pool reads, from the last
// noted unit back, what CThingy::CalcVisibility will read there, and CalcVisibility takes a
// value read ahead where one is ready. The circle walk writes no fog, so the value is the
// same. Off with one thread (Settings, Processor threads), with HYPERION_VISION_AHEAD=0, and
// under HYPERION_VISION_AHEAD_FEWEST units (4,096); HYPERION_VISION_AHEAD_CHECK=1 also reads
// in place and writes any difference to hyperion_vision_ahead.log.
struct point16_t;
void VisionHxCircleNote(const CUnit *pUnit, const point16_t *pSizes);  // gUnitPlaceboxSize
void VisionHxCirclesBegin();
void VisionHxCirclesEnd();
bool VisionHxCircleRead(const void *pThingy, int nX, int nY, int nW, int nH, unsigned char *pbSeen,
                        PlayerSet *pqHigh);

// The rectangle of tiles CThingy::CalcVisibility reads: a footprint of nWidth by nHeight
// pixels centred on a sprite at (nPosX, nPosY), clipped to a map of nMapW by nMapH tiles;
// false when none of it is on the map. The read-ahead notes the same rectangle.
inline bool VisionTileRect(int nPosX, int nPosY, int nWidth, int nHeight, int nMapW, int nMapH, int *pX, int *pY,
                           int *pW, int *pH) {
  int nX = (nPosX - nWidth / 2) / 32;
  int nY = (nPosY - nHeight / 2) / 32;
  int nW = (nWidth + 31) / 32u;
  int nH = (nHeight + 31) / 32u;
  if (nX < 0) {
    nW += nX;
    if (nW <= 0)
      return false;
    nX = 0;
  }
  if (nY < 0) {
    nH += nY;
    if (nH <= 0)
      return false;
    nY = 0;
  }
  if (nX + nW > nMapW) {
    nW = nMapW - nX;
    if (nW <= 0)
      return false;
  }
  if (nY + nH > nMapH) {
    nH = nMapH - nY;
    if (nH <= 0)
      return false;
  }
  *pX = nX;
  *pY = nY;
  *pW = nW;
  *pH = nH;
  return true;
}

// 1.17's sight stamp (sub_46BBB0) for the bits past 7 (mask.cpp).
void MaskStampHx(int nPixelX, int nPixelY, int bAir, const PlayerSet &q, int nRange);

// Parasite and blindness (optical flare): the casters past 7 of each unit, by its index
// in the unit table. 1.17 keeps a byte of casters for each (bParasiteFlags,
// bMyopiaPlayers); the Unit* calls read and write both, every slot's bit at its slot.
PlayerSet VisionHxParasite(const CUnit *pUnit);
PlayerSet UnitParasite(const CUnit *pUnit);
void UnitParasiteAdd(CUnit *pUnit, int nSlot);
PlayerSet UnitMyopia(const CUnit *pUnit);
void UnitMyopiaAdd(CUnit *pUnit, int nSlot);
void UnitHxClear(CUnit *pUnit);                         // a new unit, or restoration
void UnitHxMerge(CUnit *pUnit, const CUnit *pOther);    // the parasites and blinders of a merged unit

// Detection: the players a cloaked or burrowed unit is seen by. 1.17's DWORD (+0xE4) holds
// slots 0 to 31, its value 0x80000000 meaning "not yet worked out" (CUnit::sub_4325A0);
// the slots past 31 are kept here.
PlayerSet UnitDetection(const CUnit *pUnit);
void UnitDetectionSet(CUnit *pUnit, const PlayerSet &q);
bool UnitDetectedBy(const CUnit *pUnit, int nSlot);

// Shared vision: the players a player's units see for. 1.17's table (SharedVision) holds
// slots 0 to 31 of each; the slots past 31 are kept here. A slot past the last reads and
// writes as 1.17's shift would, the count taken mod 32.
PlayerSet PlayerVision(int nSlot);
void PlayerVisionAdd(int nSlot, int nOther);
void PlayerVisionRemove(int nSlot, int nOther);
void PlayerVisionOnly(int nSlot);                       // the player sees for itself alone

// The replay viewer's slots past 7. 1.17's viewer (gdwReplayVisionMask) is a byte of
// players; a Hyperion game's playback shows what its slots from 8 on see as well, each a
// player of the game. Empty in a live game and in 1.17's replays, where every call below
// is false. Presentation only: what the screen, the minimap and the sounds show.
void ViewHxReset(bool bPlayback);             // the game's start (game.cpp)
bool ViewHxTileSeen(int nTile);               // a viewer slot past 7 sees the tile
bool ViewHxTileExplored(int nTile);           // a viewer slot past 7 has explored it
bool ViewHxSpriteSeen(const CSprite *pSprite);
bool ViewHxDetected(const CUnit *pUnit);      // a viewer slot past 7 detects the unit
bool ViewHxUnitSeen(const CUnit *pUnit);      // seen, and detected if cloaked, by the same slot
// A sprite the viewer has just come to see or stopped seeing: its images redrawn, as
// CSprite::SetMask does for 1.17's byte (CSprite.cpp).
void SpriteViewRedraw(CSprite *pSprite, bool bSeen);

// A save of a Hyperion game: every set above (port_hxsave.cpp). 0 on an error.
int VisionHxSave(void *pFile);
int VisionHxLoad(void *pFile);

#endif
