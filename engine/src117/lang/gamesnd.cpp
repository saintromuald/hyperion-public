// gamesnd.cpp -- the game's sound cues: acknowledgements, alerts, error cues
// and positional mixing. Bodies are in oracle VA order.

// File-static in the oracle, so MSVC may hoist its load above a store
// through a pointer; the extern globals117.h adds keeps the internal linkage.
#if !defined(OC_DLL_MODE) && !defined(OC_PAYLOAD)
static int sgnHelpSndTimer;
#endif
#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/gamesnd.h"
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled, one #ifdef per name.
#ifndef gxUnitLife
  #ifdef OC_DLL_MODE
    #define gxUnitLife (reinterpret_cast<LONG *>(OC_VA(0x520708u)))
  #else
extern LONG gxUnitLife[228] OC_UNALIGNED;
  #endif
#endif

#ifndef gbSoundMinVolume
  #ifdef OC_DLL_MODE
    #define gbSoundMinVolume (reinterpret_cast<BYTE *>(OC_VA(0x51E630u)))
  #else
extern BYTE gbSoundMinVolume[1144];
  #endif
#endif

#ifndef gwSoundLengthAdjust
  #ifdef OC_DLL_MODE
    #define gwSoundLengthAdjust (reinterpret_cast<short *>(OC_VA(0x51DD40u)))
  #else
extern short gwSoundLengthAdjust[1144];
  #endif
#endif

#ifndef guwFirstAckSnd
  #ifdef OC_DLL_MODE
    #define guwFirstAckSnd (reinterpret_cast<WORD *>(OC_VA(0x522578u)))
  #else
extern WORD guwFirstAckSnd[108] OC_UNALIGNED;
  #endif
#endif

#ifndef guwLastAckSnd
  #ifdef OC_DLL_MODE
    #define guwLastAckSnd (reinterpret_cast<WORD *>(OC_VA(0x522650u)))
  #else
extern WORD guwLastAckSnd[108];
  #endif
#endif

#ifndef guwReadySnd
  #ifdef OC_DLL_MODE
    #define guwReadySnd (reinterpret_cast<WORD *>(OC_VA(0x521F60u)))
  #else
extern WORD guwReadySnd[108];
  #endif
#endif

#ifndef gwScreenTileX_g
  #ifdef OC_DLL_MODE
    #define gwScreenTileX_g (*reinterpret_cast<WORD *>(OC_VA(0x5264B0u)))
  #else
extern WORD gwScreenTileX_g;
  #endif
#endif

#ifndef guwFirstWhatSnd
  #ifdef OC_DLL_MODE
    #define guwFirstWhatSnd (reinterpret_cast<WORD *>(OC_VA(0x522038u)))
  #else
extern WORD guwFirstWhatSnd[228] OC_UNALIGNED;
  #endif
#endif

#ifndef guwLastWhatSnd
  #ifdef OC_DLL_MODE
    #define guwLastWhatSnd (reinterpret_cast<WORD *>(OC_VA(0x522200u)))
  #else
extern WORD guwLastWhatSnd[228] OC_UNALIGNED;
  #endif
#endif

// 0x5223C8 / 0x5224A0, the pissed-cue tables, 108 WORDs each.
#ifndef guwFirstPissedSnd
  #ifdef OC_DLL_MODE
    #define guwFirstPissedSnd (reinterpret_cast<WORD *>(OC_VA(0x5223C8u)))
  #else
extern WORD guwFirstPissedSnd[108] OC_UNALIGNED;
  #endif
#endif

#ifndef guwLastPissedSnd
  #ifdef OC_DLL_MODE
    #define guwLastPissedSnd (reinterpret_cast<WORD *>(OC_VA(0x5224A0u)))
  #else
extern WORD guwLastPissedSnd[108];
  #endif
#endif

// 0x66726C sgpLastSelectUnit, the unit gamesnd_select last played for.
#ifndef sgpLastSelectUnit
  #ifdef OC_DLL_MODE
    #define sgpLastSelectUnit (*reinterpret_cast<CUnit **>(OC_VA(0x66726Cu)))
  #else
extern CUnit *sgpLastSelectUnit;
  #endif
#endif

// 0x667270 sgnLastSelectCount, signed: gamesnd_select tests the sign of the
// count less 3.
#ifndef sgnLastSelectCount
  #ifdef OC_DLL_MODE
    #define sgnLastSelectCount (*reinterpret_cast<int *>(OC_VA(0x667270u)))
  #else
extern int sgnLastSelectCount;
  #endif
#endif

#ifndef sgnRndSndId_g
  #ifdef OC_DLL_MODE
    #define sgnRndSndId_g (*reinterpret_cast<int *>(OC_VA(0x66697Cu)))
  #else
extern int sgnRndSndId_g;
  #endif
#endif

#ifndef sgnSelectSndTimer
  #ifdef OC_DLL_MODE
    #define sgnSelectSndTimer (*reinterpret_cast<int *>(OC_VA(0x667274u)))
  #else
extern int sgnSelectSndTimer;
  #endif
#endif

#ifndef sgnBuildSndTimer
  #ifdef OC_DLL_MODE
    #define sgnBuildSndTimer (*reinterpret_cast<int *>(OC_VA(0x667278u)))
  #else
extern int sgnBuildSndTimer;
  #endif
#endif

#ifndef sgnHelpSndTimer
  #if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
    #define sgnHelpSndTimer (*reinterpret_cast<int *>(OC_VA(0x66727Cu)))
  #else
extern int sgnHelpSndTimer;
  #endif
#endif

#ifndef gbSpeechMuted_g
  #ifdef OC_DLL_MODE
    #define gbSpeechMuted_g (*reinterpret_cast<DWORD *>(OC_VA(0x667280u)))
  #else
extern DWORD gbSpeechMuted_g;
  #endif
#endif

#ifndef nUpdateCtr
  #ifdef OC_DLL_MODE
    #define nUpdateCtr (*reinterpret_cast<int *>(OC_VA(0x667284u)))
  #else
extern int nUpdateCtr;
  #endif
#endif

#ifndef glScrollPixelX
  #ifdef OC_DLL_MODE
    #define glScrollPixelX (*reinterpret_cast<LONG *>(OC_VA(0x543218u)))
  #else
extern LONG glScrollPixelX;
  #endif
#endif

#ifndef glScrollPixelY
  #ifdef OC_DLL_MODE
    #define glScrollPixelY (*reinterpret_cast<LONG *>(OC_VA(0x54321Cu)))
  #else
extern LONG glScrollPixelY;
  #endif
#endif

#ifndef gPlayback
  #ifdef OC_DLL_MODE
    #define gPlayback (*reinterpret_cast<DWORD *>(OC_VA(0x67CC90u)))
  #else
extern DWORD gPlayback;
  #endif
#endif

#ifndef g_LocalNationID
  #ifdef OC_DLL_MODE
    #define g_LocalNationID (*reinterpret_cast<DWORD *>(OC_VA(0x508D50u)))
  #else
extern DWORD g_LocalNationID;
  #endif
#endif

#ifndef gdwReplayVisionMask
  #ifdef OC_DLL_MODE
    #define gdwReplayVisionMask (*reinterpret_cast<DWORD *>(OC_VA(0x67CC84u)))
  #else
extern DWORD gdwReplayVisionMask;
  #endif
#endif

#ifndef gdwLocalMask
  #ifdef OC_DLL_MODE
    #define gdwLocalMask (*reinterpret_cast<DWORD *>(OC_VA(0x672668u)))
  #else
extern DWORD gdwLocalMask;
  #endif
#endif

// gOptions[4] and gOptions[6], 0x6D3140 and 0x6D3148, spelled as separate
// globals since the registry types gOptions as a single DWORD.
#ifndef gOptions_sound_on
  #ifdef OC_DLL_MODE
    #define gOptions_sound_on (*reinterpret_cast<int *>(OC_VA(0x6D3140u)))
  #else
extern int gOptions_sound_on;
  #endif
#endif

#ifndef gOptions_lift_variant
  #ifdef OC_DLL_MODE
    #define gOptions_lift_variant (*reinterpret_cast<DWORD *>(OC_VA(0x6D3148u)))
  #else
extern DWORD gOptions_lift_variant;
  #endif
#endif

#ifndef byte_5264C2
  #ifdef OC_DLL_MODE
    #define byte_5264C2 (*reinterpret_cast<BYTE *>(OC_VA(0x5264C2u)))
  #else
extern BYTE byte_5264C2;
  #endif
#endif

// gamesnd_reset's one-shot flag and preload tables; sheet names, all in this TU.
// The flag is a dword (`cmp dword ptr [666978h], 0h`). The tables are WORD
// (stride 2); their extents run to the next sheet name (0x290 and 0x340 bytes),
// and the Protoss one to the highest index gamesnd_reset writes.
#ifndef sgbGamesndInit_g
  #ifdef OC_DLL_MODE
    #define sgbGamesndInit_g (*reinterpret_cast<DWORD *>(OC_VA(0x666978u)))
  #else
extern DWORD sgbGamesndInit_g;
  #endif
#endif
#ifndef sgwZergSndPreload_g
  #ifdef OC_DLL_MODE
    #define sgwZergSndPreload_g (reinterpret_cast<WORD *>(OC_VA(0x666980u)))
  #else
extern WORD sgwZergSndPreload_g[328];
  #endif
#endif
#ifndef sgwTerranSndPreload_g
  #ifdef OC_DLL_MODE
    #define sgwTerranSndPreload_g (reinterpret_cast<WORD *>(OC_VA(0x666C10u)))
  #else
extern WORD sgwTerranSndPreload_g[416];
  #endif
#endif
#ifndef sgwProtossSndPreload_g
  #ifdef OC_DLL_MODE
    #define sgwProtossSndPreload_g (reinterpret_cast<WORD *>(OC_VA(0x666F50u)))
  #else
extern WORD sgwProtossSndPreload_g[396];
  #endif
#endif
// The per-race table pointer, a dword at stride 4 indexed by the console race.
#ifndef sgpwSndPreload_g
  #ifdef OC_DLL_MODE
    #define sgpwSndPreload_g (reinterpret_cast<WORD **>(OC_VA(0x5033B8u)))
  #else
extern WORD *sgpwSndPreload_g[3] OC_UNALIGNED;
  #endif
#endif

// The callees this TU reaches.
#include "../h/CUnit.h"
#include "../h/CSprite.h"    // bVisibilityFlags / wPosX / wPosY, all reached by value
// count_race, which count.h also declares.
// Hyperion: int, as the definition reads it, which the x64 build needs; a
// unit type is the same widened.
extern "C" BYTE count_race(int nUnitType);
#include "../h/order.h"          // status_get_str, map_msg_target_self, rect16_t
#include "../h/music.h"           // sub_46F120, music_volume_default

// The headers that declare this TU's own functions, so that each definition is
// checked against its declaration.
#include "../h/CUnitTSpells.h"   // gamesnd_play
#include "../h/CUnitTerran.h"    // gamesnd_do_liftoff
#include "../h/CUnitBuild.h"     // gamesnd_create_man
#include "../h/CUnitCombat.h"    // gamesnd_under_attack
#include "../h/CUnitHarvest.h"   // gamesnd_show_player_error
#include "../h/TrigAct.h"        // gamesnd_mute_speech / gamesnd_unmute_speech
#include "../h/CThingy.h"       // gamesnd_select

#include "../h/CImage.h"       // IsSelfOwned
#include "../port/render/port_view.h"  // Hyperion: PortViewSeenRect, g_portView
#include "../h/playervision.h"  // Hyperion: the replay viewer past slot 7 (stage D)
#include "../h/typelimits.h"  // Hyperion: a mod's types
#include "../port/hud/port_hudstate.h"  // Hyperion: the HUD's data layer
#include "../h/handicap.h"  // Hyperion: a player's handicap

// rand and srand: OcRand and OcSrand (h/platform.h)

// 0x446080  CUnit::IsOnFire  (72 B)
// Whether the unit's hit points are between 34% and 66% of its maximum, a range
// test as in 1.17. lHitPoints is read twice, as there.
int CUnit::IsOnFire() {
  int nMaxLife = HandicapLife(gxUnitLife[this->wType], this->bPlayer) >> 8;  // Hyperion: at the owner's handicap
  if (nMaxLife == 0) {
    nMaxLife = 1;
    if ((this->lHitPoints + 0xFF) >> 8)
      nMaxLife = (this->lHitPoints + 0xFF) >> 8;
  }
  int nPercent = ((this->lHitPoints + 0xFF) >> 8) * 100 / nMaxLife;
  return nPercent >= 34 && nPercent <= 66;
}

// 0x4463E0  gamesnd_screendist_volume  (100 B)
// The volume for a map point: 99 less the Manhattan distance outside the view,
// scaled, and never below 0.
// Hyperion: the view is what the camera shows above the console, 640 by
// 400 in 1.17.
int __fastcall gamesnd_screendist_volume(int nMapX, int nMapY) {
  int nLeft, nTop, nRight, nBottom;
  PortViewSeenRect(&nLeft, &nTop, &nRight, &nBottom);
  int nDist = 0;
  if (nMapX < nLeft)
    nDist = nLeft - nMapX;
  else if (nMapX > nRight)
    nDist = nMapX - nRight;

  if (nMapY < nTop)
    nDist += nTop - nMapY;
  else if (nMapY > nBottom)
    nDist += nMapY - nBottom;

  int nVolume = 99 - (nDist * 99 >> 9);
  if (nVolume < 0)
    nVolume = 0;
  return nVolume;
}

// 0x446FF0  gamesnd_update  (107 B)
// Counts the three cue timers down, and every other frame the units'
// attack-notify timers at CUnit +0x87.
void __fastcall gamesnd_update(void) {
  if (sgnSelectSndTimer)
    sgnSelectSndTimer--;
  if (sgnBuildSndTimer)
    sgnBuildSndTimer--;
  if (sgnHelpSndTimer)
    sgnHelpSndTimer--;

  if (nUpdateCtr-- == 0) {
    nUpdateCtr = 1;
    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
      if (pUnit->_unmapped_86[1])
        pUnit->_unmapped_86[1]--;
    }
  }
}

// 0x446940  gamesnd_play  (220 B)
// Plays a sound, from a unit when one is given; a unit the player cannot see is
// silent. The volume is the largest of the requested one, the sound's minimum
// and the distance volume. a3 is a flag, then reused as the pan.
int __fastcall gamesnd_play(int nSoundId, CUnit *pUnit, int a3, int nVolume) {
  if (nSoundId == 0)
    return 0;
  if (gOptions_sound_on == 0)
    return 0;
  if (a3 == 0)
    return 0;

  a3 = 0;
  int nScreenVol = 99;
  if (pUnit != NULL) {
    if (gPlayback != 0) {
      if ((pUnit->pSprite->bVisibilityFlags & (BYTE)gdwReplayVisionMask) == 0 &&
          !ViewHxSpriteSeen(pUnit->pSprite))  // Hyperion: the viewer's slots past 7 (stage D)
        return 0;
    } else if (!IsSelfOwned(pUnit)) {
      if ((pUnit->pSprite->bVisibilityFlags & (BYTE)gdwLocalMask) == 0)
        return 0;
    }
    a3 = pUnit->GetPan();
    nScreenVol = gamesnd_screendist_volume(PosS(pUnit->pSprite->wPosX), pUnit->pSprite->wPosY);
  }

  int nMinVol = gbSoundMinVolume[nSoundId];
  int nCap;
  if (nScreenVol > nMinVol)
    nCap = nScreenVol;
  else
    nCap = nMinVol;
  // The result seeded with nScreenVol before the test keeps the inlined copies
  // (pUnit NULL) loading 99 ahead of the first compare and sharing its flags.
  int nVol = nScreenVol;
  if (nVolume > nCap)
    nVol = nVolume;
  else if (nScreenVol <= nMinVol)
    nVol = nMinVol;

  if (gbSpeechMuted_g != 0)
    nVol = nVol / 5;

  if (nVol > 10)
    return sub_49E780(nSoundId, nVol, a3, pUnit);
  return 0;
}

// 0x446570  gamesnd_placebox_error  (106 B)
// The error-cue template: the race's cue, then the talking portrait for its
// length. The duration is divided unsigned, as in 1.17.
void __fastcall gamesnd_placebox_error(CUnit *pUnit) {
  if (gPlayback)
    return;
  if (pUnit->bPlayer != g_LocalNationID)
    return;

  int nSoundId = pUnit->GetRace() + 0x8D;
  if (gamesnd_play(nSoundId, pUnit, 1, 99)) {
    int nDeadline = (unsigned int)snd_get_duration(nSoundId) / 1000 * 1000;
    nDeadline += gwSoundLengthAdjust[nSoundId];
    statport_talking_portrait(pUnit, nDeadline, 1, 0x11, nSoundId);
  }
}

// 0x4466D0  gamesnd_do_liftoff  (105 B)
// The template with the fixed cue 0x82, gOptions[6] bit 2 as the variant, and
// the portrait shown whether or not the sound played.
void __fastcall gamesnd_do_liftoff(CUnit *pUnit) {
  if (gPlayback)
    return;
  if (pUnit->bPlayer != g_LocalNationID)
    return;

  gamesnd_play(0x82, pUnit, (gOptions_lift_variant >> 2) & 1, 99);
  int nDeadline = (unsigned int)snd_get_duration(0x82) / 1000 * 1000;
  nDeadline += gwSoundLengthAdjust[0x82];
  statport_talking_portrait(pUnit, nDeadline, 1, 0x11, 0x82);
}

// 0x4465E0  gamesnd_land_error  (106 B)
// The template with 0x90 for 0x8D.
void __fastcall gamesnd_land_error(CUnit *pUnit) {
  if (gPlayback)
    return;
  if (pUnit->bPlayer != g_LocalNationID)
    return;

  int nSoundId = pUnit->GetRace() + 0x90;
  if (gamesnd_play(nSoundId, pUnit, 1, 99)) {
    int nDeadline = (unsigned int)snd_get_duration(nSoundId) / 1000 * 1000;
    nDeadline += gwSoundLengthAdjust[nSoundId];
    statport_talking_portrait(pUnit, nDeadline, 1, 0x11, nSoundId);
  }
}

// 0x4464E0  gamesnd_build_error  (132 B)
// The template with 0xA2, then the status message, the last call.
void __fastcall gamesnd_build_error(CUnit *pUnit, int nStringId) {
  if (gPlayback)
    return;
  if (pUnit->bPlayer != g_LocalNationID)
    return;

  int nSoundId = pUnit->GetRace() + 0xA2;
  if (gamesnd_play(nSoundId, pUnit, 1, 99)) {
    int nDeadline = (unsigned int)snd_get_duration(nSoundId) / 1000 * 1000;
    nDeadline += gwSoundLengthAdjust[nSoundId];
    statport_talking_portrait(pUnit, nDeadline, 1, 0x11, nSoundId);
  }
  map_msg_target_self(status_get_str((WORD)nStringId), pUnit->bPlayer);
}

// 0x446450  gamesnd_placebox_error_msg  (132 B)
// gamesnd_build_error with 0x9F for 0xA2.
void __fastcall gamesnd_placebox_error_msg(CUnit *pUnit, WORD wStringId) {
  if (gPlayback)
    return;
  if (pUnit->bPlayer != g_LocalNationID)
    return;

  int nSoundId = pUnit->GetRace() + 0x9F;
  if (gamesnd_play(nSoundId, pUnit, 1, 99)) {
    int nDeadline = (unsigned int)snd_get_duration(nSoundId) / 1000 * 1000;
    nDeadline += gwSoundLengthAdjust[nSoundId];
    statport_talking_portrait(pUnit, nDeadline, 1, 0x11, nSoundId);
  }
  map_msg_target_self(status_get_str(wStringId), pUnit->bPlayer);
}

// 0x446650  gamesnd_create_man  (128 B)
// The unit's ready cue, skipped for two unit types and while speech is muted,
// with gOptions[6] bit 0 as the variant and the portrait shown regardless.
void gamesnd_create_man(CUnit *pUnit) {
  PortHudDone(pUnit, 1);  // Hyperion: unit.done, muted speech or not (port_hudstate.cpp)
  if (gPlayback)
    return;
  if (pUnit->bPlayer != g_LocalNationID)
    return;
  if (gbSpeechMuted_g)
    return;

  int nType = pUnit->wType;
  if (nType == 0x49)
    return;
  if (nType == 0x55)
    return;

  int nSoundId = guwReadySnd[nType];
  gamesnd_play(nSoundId, pUnit, (BYTE)gOptions_lift_variant & 1, 99);
  int nDeadline = (unsigned int)snd_get_duration(nSoundId) / 1000 * 1000;
  nDeadline += gwSoundLengthAdjust[nSoundId];
  statport_talking_portrait(pUnit, nDeadline, 1, 0x11, nSoundId);
}

// 0x4460D0  gamesnd_update_attacknotify_timers  (73 B)
// Sets the attack-notify timer of every unit within 0x160 of this one, through
// the area walker and gamesnd_set_attacknotify.
void __fastcall gamesnd_update_attacknotify_timers(CUnit *pUnit) {
  CSprite *pSprite = pUnit->pSprite;
  mrect_t  area;
  area.left   = pSprite->wPosX - 0x160;
  area.top    = pSprite->wPosY - 0x160;
  area.right  = pSprite->wPosX + 0x160;
  area.bottom = pSprite->wPosY + 0x160;
  sub_442B60(&area, gamesnd_set_attacknotify, NULL);
}

// 0x446360  gamesnd_screendist_pan  (117 B)
// The pan for a signed screen X: a stepped table on the megatile distance from
// the view's middle, negated to the left.
int __fastcall gamesnd_screendist_pan(int nScreenX) {
  BYTE bNegate = 0;
  int nPan = 0;

  // Hyperion: from the view's middle, 10 megatiles across in 1.17.
  int nDist = nScreenX / 32 - gwScreenTileX_g - g_portView.nWidth / 64;
  if (nDist < 0) {
    bNegate = 1;
    nDist = -nDist;
  }

  if (nDist > 80)
    nPan = 10000;
  else if (nDist > 40)
    nPan = 5000;
  else if (nDist > 20)
    nPan = 1000;
  else if (nDist > 10)
    nPan = 600;
  else if (nDist > 5)
    nPan = 300;
  else if (nDist > 2)
    nPan = 100;

  if (bNegate)
    nPan = -nPan;
  return nPan;
}

// 0x446B70  gamesnd_research_complete  (178 B)
// The race's research cue and the portrait; 1.17 inlines gamesnd_play here,
// with no unit.
void __fastcall gamesnd_research_complete(CUnit *pUnit) {
  PortHudDone(pUnit, 0);  // Hyperion: research.done (port_hudstate.cpp)
  if (gPlayback)
    return;
  if (pUnit->bPlayer != g_LocalNationID)
    return;

  int nSoundId = pUnit->GetRace() + 0x7B;
  gamesnd_play(nSoundId, NULL, 1, 0);
  int nDeadline = (unsigned int)snd_get_duration(nSoundId) / 1000 * 1000;
  nDeadline += gwSoundLengthAdjust[nSoundId];
  statport_talking_portrait(pUnit, nDeadline, 1, 0x11, nSoundId);
}

// 0x447060  gamesnd_upgrade_finished  (181 B)
// gamesnd_research_complete with 0x83 for 0x7B.
void __fastcall gamesnd_upgrade_finished(CUnit *pUnit) {
  PortHudDone(pUnit, 0);  // Hyperion: research.done (port_hudstate.cpp)
  if (gPlayback)
    return;
  if (pUnit->bPlayer != g_LocalNationID)
    return;

  int nSoundId = pUnit->GetRace() + 0x83;
  gamesnd_play(nSoundId, NULL, 1, 0);
  int nDeadline = (unsigned int)snd_get_duration(nSoundId) / 1000 * 1000;
  nDeadline += gwSoundLengthAdjust[nSoundId];
  statport_talking_portrait(pUnit, nDeadline, 1, 0x11, nSoundId);
}

// 0x447130  pick_rnd_snd_ack  (91 B)
// Picks a random acknowledgement for the unit type, never the one just played:
// a repeat moves on by one and wraps to the first.
int __fastcall pick_rnd_snd_ack(int nUnitType) {
  // Hyperion: a mod's unit has voices of its own, its looks unit's when its record gives
  // none (port_moddata.cpp)
  if ((nUnitType <= 0x69 || UnitTypeMod(static_cast<unsigned>(nUnitType))) && guwFirstAckSnd[nUnitType] != 0) {
    int nSoundId = guwFirstAckSnd[nUnitType];
    int nLast = guwLastAckSnd[nUnitType];
    if (nSoundId == nLast) {
      sgnRndSndId_g = nSoundId;
    } else {
      int nRolled = (OcRand() >> 8) % (nLast - nSoundId + 1) + nSoundId;
      if (nRolled == sgnRndSndId_g) {
        nRolled++;
        if (nRolled > nLast)
          nRolled = nSoundId;
      }
      nSoundId = nRolled;
      sgnRndSndId_g = nRolled;
    }
    return nSoundId;
  }
  return 0;
}

// 0x447190  pick_rnd_snd_select  (134 B)
// Picks the selection cue: the nCount'th pissed cue while nCount is within
// their range, else a random what cue picked as pick_rnd_snd_ack picks. Past
// the range, the select count is reset.
int __fastcall pick_rnd_snd_select(int nUnitType, int nCount) {
  // Hyperion: and a mod's unit its own
  if ((nUnitType <= 0x69 || UnitTypeMod(static_cast<unsigned>(nUnitType))) && guwFirstPissedSnd[nUnitType] != 0) {
    int nFirstPissed = guwFirstPissedSnd[nUnitType];
    if (nCount > guwLastPissedSnd[nUnitType] - nFirstPissed + 1)
      sgnLastSelectCount = 0;
    else if (nCount != 0)
      return nCount - 1 + nFirstPissed;
  }

  int nSoundId = guwFirstWhatSnd[nUnitType];
  int nLast = guwLastWhatSnd[nUnitType];
  if (nSoundId == nLast) {
    sgnRndSndId_g = nSoundId;
  } else {
    int nRolled = (OcRand() >> 8) % (nLast - nSoundId + 1) + nSoundId;
    if (nRolled == sgnRndSndId_g) {
      nRolled++;
      if (nRolled > nLast)
        nRolled = nSoundId;
    }
    nSoundId = nRolled;
    sgnRndSndId_g = nRolled;
  }
  return nSoundId;
}

// 0x446120  gamesnd_ack  (205 B)
// Plays an acknowledgement for a local unit that can act, and restores the
// previous cue id if it did not play. sgpLastSelectUnit is cleared even in
// playback, as in 1.17.
void __fastcall gamesnd_ack(CUnit *pUnit) {
  if (gbSpeechMuted_g)
    return;

  sgpLastSelectUnit = NULL;

  if (gPlayback)
    return;
  if (pUnit->bPlayer != g_LocalNationID)
    return;
  if (pUnit->dwFlags & 0x400)
    return;
  if (pUnit->bLockdownTimer)
    return;
  if (pUnit->bStasisTimer)
    return;
  if (pUnit->bParalyzeTimer)
    return;

  int nPrevSoundId = sgnRndSndId_g;
  int nSoundId = pick_rnd_snd_ack(pUnit->wType);
  if (gamesnd_play(nSoundId, pUnit, (gOptions_lift_variant >> 1) & 1, 99)) {
    int nDeadline = (unsigned int)snd_get_duration(nSoundId) / 1000 * 1000;
    nDeadline += gwSoundLengthAdjust[nSoundId];
    statport_talking_portrait(pUnit, nDeadline, 0, 0x11, nSoundId);
  } else {
    sgnRndSndId_g = nPrevSoundId;
  }
}

// 0x446280  gamesnd_build_complete  (219 B)
// The race's construction-complete cue, a switch as in 1.17, then the portrait.
void __fastcall gamesnd_build_complete(CUnit *pUnit) {
  PortHudDone(pUnit, 2);  // Hyperion: unit.done for a building (port_hudstate.cpp)
  if (gPlayback)
    return;
  if (pUnit->bPlayer != g_LocalNationID)
    return;

  switch (pUnit->GetRace()) {
    case 0:
      gamesnd_play(0x87, pUnit, (BYTE)gOptions_lift_variant & 1, 99);
      break;

    case 1: {
      int nSoundId = 0x7E;
      if (pUnit->wType == 7)
        nSoundId = 0x88;
      gamesnd_play(nSoundId, pUnit, (BYTE)gOptions_lift_variant & 1, 99);
      int nDeadline = (unsigned int)snd_get_duration(nSoundId) / 1000 * 1000;
      nDeadline += gwSoundLengthAdjust[nSoundId];
      statport_talking_portrait(pUnit, nDeadline, 1, 0x11, nSoundId);
      return;
    }
  }

  int nDeadline = (unsigned int)snd_get_duration(0x87) / 1000 * 1000;
  nDeadline += gwSoundLengthAdjust[0x87];
  statport_talking_portrait(pUnit, nDeadline, 1, 0x11, 0x87);
}

// 0x4461F0  gamesnd_show_player_error  (136 B)
// Plays an error cue and its status message for the local player; 1.17
// inlines gamesnd_play here, with no unit.
void __fastcall gamesnd_show_player_error(int nPlayer, WORD wSoundId, int nStringId) {
  PortHudError(nPlayer, nStringId);  // Hyperion: alert.minerals, gas, supply (port_hudstate.cpp)
  if (nPlayer != (int)g_LocalNationID)
    return;
  if (gPlayback)
    return;

  gamesnd_play(wSoundId, NULL, 1, 0);
  map_msg_target_self(status_get_str((WORD)nStringId), nPlayer);
}

// 0x446740  gamesnd_duration  (24 B)
// A sound's duration in whole seconds, as milliseconds; the divide is unsigned.
int __fastcall gamesnd_duration(int nSoundId) {
  return (unsigned int)snd_get_duration(nSoundId) / 1000 * 1000;
}

// 0x446760  gamesnd_error  (150 B)
// The race's error cue and an optional message for the local player;
// gamesnd_play is inlined here too.
void __fastcall gamesnd_error(CUnit *pUnit, char *pszMsg, DWORD dwNation) {
  if (dwNation != g_LocalNationID)
    return;
  if (gPlayback)
    return;

  if (pszMsg != NULL)
    map_msg_target_self(pszMsg, dwNation);

  BYTE bRace = pUnit != NULL ? pUnit->GetRace() : byte_5264C2;
  gamesnd_play(bRace + 1, NULL, 1, 0);
}

// Resets the sound timers, builds the three race preload lists once, then
// preloads the console race's list or none. Each tail loop keeps its own
// running index, and the final call takes a ternary: an if/else of two calls
// coalesces the race index into ECX where the oracle loads it into EAX.
void __fastcall gamesnd_reset(BYTE bPreload) {
  sgnSelectSndTimer = 0;
  sgnBuildSndTimer = 0;
  sgpLastSelectUnit = 0;
  sgnLastSelectCount = 0;
  sgnHelpSndTimer = 0;
  gbSpeechMuted_g = 0;

  if (!sgbGamesndInit_g) {
    int i = 0;
    for (int v = 1; v < 114; v++) {
      sgwProtossSndPreload_g[i] = (WORD)v;
      sgwTerranSndPreload_g[i] = (WORD)v;
      sgwZergSndPreload_g[i] = (WORD)v;
      i++;
    }

    int j;
    int n = i;
    for (j = 0; j < 213; j++) {
      sgwZergSndPreload_g[n] = (WORD)(j + 0x2F3);
      n++;
    }
    sgwZergSndPreload_g[n] = 0;

    n = i;
    for (j = 0; j < 299; j++) {
      sgwTerranSndPreload_g[n] = (WORD)(j + 0xAE);
      n++;
    }
    sgwTerranSndPreload_g[n] = 0;

    n = i;
    for (j = 0; j < 282; j++) {
      sgwProtossSndPreload_g[n] = (WORD)(j + 0x1D9);
      n++;
    }
    // The flag store sits between the last loop and its terminator.
    sgbGamesndInit_g = 1;
    sgwProtossSndPreload_g[n] = 0;
  }

  sub_49E3F0(bPreload ? (intptr_t)sgpwSndPreload_g[byte_5264C2] : 0);
}

// 0x446930  gamesnd_mute_speech  (15 B)
void __fastcall gamesnd_mute_speech(void) {
  gbSpeechMuted_g = 1;
  sub_46F120();
}

// Plays a sound at a map point when the point is on the map and visible, with distance volume and pan.
// A set square-map bit means fogged; in replay the vision mask is tested against the inverted tile.
// The tile index is written out in each arm, not held in a local, which gives gwMapWidth ECX.
void __fastcall gamesnd_play_mtx(int nSound, int a2, snd_pos_t pos, int a4) {
  if (nSound == 0)
    return;
  if (pos.x >= gwMapPixWidth)
    return;
  if (pos.y >= gwMapPixHeight)
    return;
  if (gOptions_sound_on == 0)
    return;
  if (a2 == 0)
    return;

  int nY = PosS(pos.y);
  int nX = PosS(pos.x);
  if (gPlayback == 0) {
    if (gpSquareMap[nY / 32 * gwMapWidth + nX / 32] & gdwLocalMask)
      return;
  } else if ((~gpSquareMap[nY / 32 * gwMapWidth + nX / 32] & (BYTE)gdwReplayVisionMask) == 0) {
    if (sgbShowAll_g == 0 && !ViewHxTileSeen(nY / 32 * gwMapWidth + nX / 32))  // Hyperion: the viewer's slots past 7 (stage D)
      return;
  }

  int nScreenVol = gamesnd_screendist_volume(nX, nY);
  int nMinVol = gbSoundMinVolume[nSound];
  int nCap = nMinVol;
  if (nScreenVol > nMinVol)
    nCap = nScreenVol;
  int nVolume = a4;
  if (nVolume <= nCap) {
    nVolume = nScreenVol;
    if (nScreenVol <= nMinVol)
      nVolume = nMinVol;
  }

  if (gbSpeechMuted_g != 0)
    nVolume = nVolume / 5;

  if (nVolume > 10)
    sub_49E780(nSound, nVolume, gamesnd_screendist_pan(nX), NULL);
}

// 0x446B30  gamesnd_rescue  (50 B)
// The sprite byte at +0x0F is the flash counter; 0x2D is the rescue flash.
void gamesnd_rescue(CUnit *pUnit) {
  if (gPlayback)
    return;
  if (pUnit->bPlayer != g_LocalNationID)
    return;

  pUnit->pSprite->bFlashCounter = 0x2D;
  gamesnd_play(byte_5264C2 + 0x20, pUnit, 1, 0);
}

// 0x446FD0  gamesnd_unmute_speech  (15 B)
void __fastcall gamesnd_unmute_speech(void) {
  gbSpeechMuted_g = 0;
  music_volume_default();
}

// 0x447120  gamesnd_set_attacknotify  (10 B)
// The callback gamesnd_update_attacknotify_timers hands to sub_442B60. The
// second parameter is the walker's context and the oracle never reads it.
int __fastcall gamesnd_set_attacknotify(CUnit *pUnit, CUnit *pContext) {
  pUnit->_unmapped_86[1] = 0x64;
  return 0;
}

// 0x446C30  gamesnd_select  (593 B)
// Plays the unit's selection cue, or its alert cue when a burning, damaged or
// disabled building is selected, and counts repeat selections of one unit.
// The type is copied into a WORD for the table tests, and the alert pick masks
// it with 0xFFFF, which sets up ECX before the zero second argument.
void __fastcall gamesnd_select(CUnit *pUnit) {
  int nSavedSndId = sgnRndSndId_g;

  if (gbSpeechMuted_g)
    return;

  if (pUnit == NULL || gPlayback != 0
      || (pUnit->bPlayer != g_LocalNationID
          && !(pUnit->bPlayer == 11 && pUnit->IsNeutralResource())
          && (pUnit->bPlayer != 11 || !pUnit->IsCritter()))) {
    sgpLastSelectUnit = NULL;
    sgnLastSelectCount = 0;
    return;
  }

  CUnit *pLastUnit = sgpLastSelectUnit;
  if (pLastUnit == pUnit && sgnSelectSndTimer != 0)
    return;

  sgnSelectSndTimer = 10;

  if (pLastUnit != pUnit) {
    sgpLastSelectUnit = pUnit;
    sgnLastSelectCount = 0;
  }

  int nUnitType = pUnit->wType;
  WORD nTest = nUnitType;
  int nSoundId;
  if ((gulUnitTypeFlags[nTest] & 1) && (pUnit->dwFlags & 1)) {
    if (pUnit->IsBurning() && (gubUnitCountInfo[nTest] & 2)) {
      nSoundId = 0x193;
    } else if (pUnit->IsBurning() && (gubUnitCountInfo[nTest] & 4)) {
      nSoundId = 0x12;
    } else if (pUnit->IsOnFire() && (gubUnitCountInfo[nTest] & 6)) {
      nSoundId = 0x12;
    } else if (pUnit->IsDisabled()) {
      nSoundId = pUnit->GetRace() + 0x72;
    } else {
      nSoundId = pick_rnd_snd_select(nUnitType & 0xFFFF, 0);
    }
  } else {
    int nRepeats = sgnLastSelectCount - 3;
    if (nRepeats < 0)
      nRepeats = 0;

    if (!pUnit->IsDisabled())
      nSoundId = pick_rnd_snd_select(pUnit->wType, nRepeats);
    else
      nSoundId = 0x0F;

    if (gbMultiPlayerMode == 0 && pUnit->IsCritter() && nRepeats >= 10) {
      pUnit->Suicide();
      CSprite *pSprite = pUnit->pSprite;
      int nPosY = pSprite->wPosY;
      int nPosX = pSprite->wPosX;
      spos_t sPosY = PosS(nPosY);
      spos_t sPosX = PosS(nPosX);
      CThingy *pThingy = CThingy::AllocThingy(0x10B, sPosX, sPosY, 0);
      if (pThingy != NULL) {
        pThingy->pSprite->bElevationLevel = 4;
        pThingy->UpdateVisibility();
      }
    }
  }

  if (nSoundId != 0) {
    DWORD dwVariant;
    if (gulUnitTypeFlags[pUnit->wType] & 1)
      dwVariant = (gOptions_lift_variant >> 2) & 1;
    else
      dwVariant = (gOptions_lift_variant >> 1) & 1;

    if (gamesnd_play(nSoundId, pUnit, dwVariant, 99)) {
      if ((gulUnitTypeFlags[pUnit->wType] & 1) == 0) {
        statport_talking_portrait(pUnit, gamesnd_duration(nSoundId) + gwSoundLengthAdjust[nSoundId],
                   0, 0x11, nSoundId);
        sgnLastSelectCount++;
      }
    } else {
      sgnRndSndId_g = nSavedSndId;
    }
  }
}

// 0x446E90  gamesnd_placebox  (98 B)
// Plays the race's placement cue, with gamesnd_play inlined.
void __fastcall gamesnd_placebox(WORD wUnitType) {
  gamesnd_play(count_race(wUnitType) + 4, NULL, 1, 0);
}

// 0x446F00. Plays the under-attack cue for a local unit that is off screen.
// __restrict: in the payload sgnHelpSndTimer is a literal address, and without
// it the timer test stays below the two unit stores.
void __fastcall gamesnd_under_attack(CUnit *__restrict pUnit) {
  PortHudAttack(pUnit);  // Hyperion: alert.attack, before the tests below (port_hudstate.cpp)
  if (gPlayback)
    return;
  if (pUnit->bPlayer != g_LocalNationID)
    return;

  BYTE bPen = sgbPens_g[0x14];
  pUnit->_unmapped_86[4] = 20;
  pUnit->_unmapped_86[5] = bPen;
  if (sgnHelpSndTimer)
    return;
  if (!pUnit->IsOffScreen())
    return;
  if (pUnit->_unmapped_86[1])
    return;

  gamesnd_update_attacknotify_timers(pUnit);

  int nSoundId = 120;
  if (pUnit->dwFlags & 2)
    nSoundId = 117;
  sgnHelpSndTimer = 100;
  nSoundId += byte_5264C2;

  gamesnd_play(nSoundId, NULL, (BYTE)gOptions_lift_variant & 1, 0);

  int nDeadline = (unsigned int)snd_get_duration(nSoundId) / 1000 * 1000;
  nDeadline += gwSoundLengthAdjust[nSoundId];
  statport_talking_portrait(pUnit, nDeadline, 1, 0x10, nSoundId);
}
