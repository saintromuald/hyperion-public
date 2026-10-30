// statdraw.cpp -- the stat panel's draw and query layer (oracle 0x4A3070 to
// 0x4A5990). The layer below it (find a control by index, show a run of
// siblings, drive a progress bar) is statdata.cpp; this TU decides what goes in
// those controls for the current selection, and whether the panel needs
// redrawing at all.
//
// /Ob2 matters both ways here. statdraw_draw_none needs statdraw_draw_unit_name
// inlined, so draw_unit_name stays in this file; statdraw_query_carrier needs
// statdraw_query_unit not inlined, which holds only while carrier has two
// textual `return statdraw_query_unit();` sites (one gives 131 B, not 51).
#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>
#include <string.h>
#include <intrin.h>
#include "platform.h"
#include "dialog.h"
#include "dlg_ctrl.h"
#include "CUnit.h"
#include "statdata.h"
#include "statdraw.h"
#include "order.h"      // status_get_str
#include "victory.h"    // psnprintf
#include "saveload.h"   // get_upgrade_level
#include "grp.h"        // GROUP
#include "statbtn.h"    // TechUseAllowed
#include "CUnitUpgrade.h"  // get_upgrade_time
#include "count.h"        // count_food_max
#include "replay.h"       // replay_get_progress
#include "glues.h"        // glues_get_str
#include "../h/playervision.h"  // Hyperion: UnitParasite, UnitMyopia (stage D)
#include "../h/typelimits.h"  // Hyperion: a mod's types
#include "../port/data/port_moddata.h"  // Hyperion: ModUnitArtFrame, a mod's unit's own art
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX
#include "../h/handicap.h"  // Hyperion: a player's handicap
#include "../h/mutators.h"  // Hyperion: the supply boost

// The globals this TU reaches, dual-spelled: one #ifndef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// gpStatUnit -- CUnit * @0x6AB4C4 (status.cpp), the unit the panel describes;
// separate from gpStatUnits, which starts at 0x6AB4C8.
#ifdef OC_DLL_MODE
  #ifndef gpStatUnit
  #define gpStatUnit (*reinterpret_cast<CUnit **>(OC_VA(0x6AB4C4u)))
  #endif
#else
extern CUnit *gpStatUnit;
#endif

// gpStatUnits -- CUnit *[12] @0x6AB4C8 (status.cpp), the selection.
#ifdef OC_DLL_MODE
  #ifndef gpStatUnits
  #define gpStatUnits (reinterpret_cast<CUnit **>(OC_VA(0x6AB4C8u)))
  #endif
#else
extern CUnit *gpStatUnits[SELECTION_MAX];
#endif

// gPlayback -- DWORD @0x67CC90 (replay.cpp). Non-zero in a replay, where every
// ownership test here passes: an observer sees the detail panel for any unit.
#ifdef OC_DLL_MODE
  #ifndef gPlayback
  #define gPlayback (*reinterpret_cast<DWORD *>(OC_VA(0x67CC90u)))
  #endif
#else
extern DWORD gPlayback;
#endif

// g_LocalNationID -- DWORD @0x508D50 (net_glue.cpp).
#ifdef OC_DLL_MODE
  #ifndef g_LocalNationID
  #define g_LocalNationID (*reinterpret_cast<DWORD *>(OC_VA(0x508D50u)))
  #endif
#else
extern DWORD g_LocalNationID;
#endif

// The last frame's cache: hit points, energy, types and shields.
#ifdef OC_DLL_MODE
  #ifndef sgdwSavedHP
  #define sgdwSavedHP (reinterpret_cast<DWORD *>(OC_VA(0x6AAB68u)))
  #endif
  #ifndef sgwSavedMP
  #define sgwSavedMP (*reinterpret_cast<WORD *>(OC_VA(0x6AAB98u)))
  #endif
  #ifndef sgwSavedTypes_g
  #define sgwSavedTypes_g (reinterpret_cast<WORD *>(OC_VA(0x6AAB9Cu)))
  #endif
  #ifndef sgdwSavedShields_g
  #define sgdwSavedShields_g (*reinterpret_cast<DWORD *>(OC_VA(0x6AABB4u)))
  #endif
#else
extern DWORD sgdwSavedHP[SELECTION_MAX] OC_UNALIGNED;
extern WORD sgwSavedMP;
extern WORD sgwSavedTypes_g[SELECTION_MAX] OC_UNALIGNED;
extern DWORD sgdwSavedShields_g;
#endif

// sgwSavedUpgradeLevel_g -- WORD @0x6AABCC (statdraw.cpp). statdraw_draw_unit
// seeds it to 1 and the weapon, armor and shield rows add their levels to it.
#ifdef OC_DLL_MODE
  #ifndef sgwSavedUpgradeLevel_g
  #define sgwSavedUpgradeLevel_g (*reinterpret_cast<WORD *>(OC_VA(0x6AABCCu)))
  #endif
#else
extern WORD sgwSavedUpgradeLevel_g;
#endif

// sgpszUnitStats -- char *[] @0x50D660 (statdraw.cpp), the stat rows' text
// buffers: each entry is formatted into and stored in a control's pData. The
// extent is not asserted: the storage to the next name is 6 slots, but the
// five 8-byte buffers at 0x6AAB40 (sgszUnitStat0..4) suggest 5 are used.
#if defined(OC_DLL_MODE)
  #ifndef sgpszUnitStats
  #define sgpszUnitStats (reinterpret_cast<char **>(OC_VA(0x50D660u)))
  #endif
#elif defined(OC_PAYLOAD)
// The payload binds the oracle's array as an absolute symbol.
extern char *sgpszUnitStats[6];
#else
// File-static in the proof arm: the oracle hoists its loads above stores
// through control pointers. Six slots is the storage to the next name.
static char *sgpszUnitStats[6];
#endif

// gpCmdIcons -- void * @0x6AAA74 (statcmd.cpp), one dword. Not a GROUP * here:
// its one consumer stores it into statdata_icon_t::pGrp, itself a void *.
#ifdef OC_DLL_MODE
  #ifndef gpCmdIcons
  #define gpCmdIcons (*reinterpret_cast<void **>(OC_VA(0x6AAA74u)))
  #endif
#else
extern void *gpCmdIcons;
#endif

// wpn_dmgUpg_g -- BYTE[] @0x5256C0 (arr/weapons.cpp), each weapon's damage
// upgrade, indexed by a zero-extended weapon id. Unsized: the storage to the
// next name is 136 bytes, and weapons.dat has 130 rows.
#ifdef OC_DLL_MODE
  #ifndef wpn_dmgUpg_g
  #define wpn_dmgUpg_g (reinterpret_cast<BYTE *>(OC_VA(0x5256C0u)))
  #endif
#else
extern upgrade_id_t wpn_dmgUpg_g[];
#endif

// wpn_icon_g -- WORD[] @0x5262C8 (arr/weapons.cpp), each weapon's icon;
// unsized, as above.
#ifdef OC_DLL_MODE
  #ifndef wpn_icon_g
  #define wpn_icon_g (reinterpret_cast<WORD *>(OC_VA(0x5262C8u)))
  #endif
#else
extern WORD wpn_icon_g[];
#endif


// sgwSavedTransport -- WORD[8] @0x6AABB8 (statdraw.cpp), the cargo ids last
// drawn; statdraw_save_transport's loop bound gives the 8.
// Hyperion: on 64 bits the copy is the port's, at the cargo list's width, since a
// game past 2,047 units keeps 32-bit ids there.
#if !defined(_M_IX86)
#undef sgwSavedTransport
static unit_id_t s_savedTransport[8];
#define sgwSavedTransport s_savedTransport
#endif
#ifdef OC_DLL_MODE
  #ifndef sgwSavedTransport
  #define sgwSavedTransport (reinterpret_cast<WORD *>(OC_VA(0x6AABB8u)))
  #endif
  #ifndef byte_6AAB9A
  #define byte_6AAB9A (*reinterpret_cast<BYTE *>(OC_VA(0x6AAB9Au)))
  #endif
#else
#ifndef sgwSavedTransport
extern WORD sgwSavedTransport[8];
#endif
extern BYTE byte_6AAB9A;
#endif

// CUnit +0xB0 -- the transport's eight cargo unit ids (wCargoUnitIds).
static inline unit_id_t *unit_loaded_units(CUnit *pUnit) {  // Hyperion: WORD in 1.17
  return reinterpret_cast<unit_id_t *>(reinterpret_cast<BYTE *>(pUnit) + offsetof(CUnit, wCargoUnitIds));
}


// sgwSavedStat1_g -- DWORD @0x6AABC8 (statdraw.cpp). The name says WORD, but
// 1.17 loads and stores it four bytes wide and reads one load as a byte, a word
// and a dword. sgwSavedStat2_g @0x6AABCA is its high half: two names over one
// slot, and statdraw_save_unit clears both with one dword store.
#ifdef OC_DLL_MODE
  #ifndef sgwSavedStat1_g
  #define sgwSavedStat1_g (*reinterpret_cast<DWORD *>(OC_VA(0x6AABC8u)))
  #endif
  #ifndef sgwSavedStat2_g
  #define sgwSavedStat2_g (*reinterpret_cast<WORD *>(OC_VA(0x6AABCAu)))
  #endif
  #ifndef gulUnitTypeFlags
  #define gulUnitTypeFlags (reinterpret_cast<DWORD *>(OC_VA(0x521660u)))
  #endif
  #ifndef gubUnitFoodMade
  #define gubUnitFoodMade (reinterpret_cast<BYTE *>(OC_VA(0x523D28u)))
  #endif
  #ifndef gubUnitCargoHold
  #define gubUnitCargoHold (reinterpret_cast<BYTE *>(OC_VA(0x523FE0u)))
  #endif
  #ifndef gubUnitVolume
  #define gubUnitVolume (reinterpret_cast<BYTE *>(OC_VA(0x523EF8u)))
  #endif
#else
extern DWORD sgwSavedStat1_g;
extern WORD sgwSavedStat2_g;
extern DWORD gulUnitTypeFlags[228] OC_UNALIGNED;
extern BYTE gubUnitFoodMade[232] OC_UNALIGNED;
extern BYTE gubUnitCargoHold[232] OC_UNALIGNED;
extern BYTE gubUnitVolume[232] OC_UNALIGNED;
#endif

// Free functions, so /Gr already makes them __fastcall.
int count_get_supplies_available(BYTE bRace, BYTE bPlayer);
int count_get_supplies_used(BYTE bRace, BYTE bPlayer);

// ctrl_t +0x26 and +0x24, reached by cast as in statdata.cpp, since dialog.h
// carries them as the filler `_unmapped24[6]`: the same two static inline
// accessors, in a different TU.
static inline void *&ctrl_data26(ctrl_t *pCtrl) {
  return *reinterpret_cast<void **>(&pCtrl->_unmapped24[2]);
}
static inline unsigned short *ctrl_progress_slot(ctrl_t *pCtrl) {
  return reinterpret_cast<unsigned short *>(&pCtrl->_unmapped24[0]);
}

// statdraw_query_powerup -- oracle 0x4A53F0, 21 B. The whole body is one setnz.
int __fastcall statdraw_query_powerup() {
  return sgdwSavedHP[0] != gpStatUnit->lHitPoints;
}

// statdraw_query_archon -- oracle 0x4A5160, 22 B. The tail jump to
// statdraw_query_unit sits after the ret, reached when the order test fails.
int __fastcall statdraw_query_archon() {
  if (gpStatUnit->bOrderId == 0x6A)
    return 1;
  return statdraw_query_unit();
}

// statdraw_query_egg -- oracle 0x4A52A0, 36 B. A conditional tail jump, MSVC's
// fold of `if (cond) return f();` when the fall-through is a constant.
int __fastcall statdraw_query_egg() {
  if (!gPlayback && gpStatUnit->bPlayer != g_LocalNationID)
    return statdraw_query_unit();
  return 1;
}

// statdraw_query_carrier -- oracle 0x4A51F0, 51 B. 1.17 reaches one
// `jmp statdraw_query_unit` from two places: the ownership test falls through
// into it and the hangar test jumps back to it. Only this spelling, two textual
// call sites with `return 1` as the early exit, gives that shared tail: the
// inner test inverted gives two jumps (54 B), and one call site inlines all of
// query_unit (131 B).
int __fastcall statdraw_query_carrier() {
  CUnit *u = gpStatUnit;
  if (gPlayback || u->bPlayer == g_LocalNationID) {
    if (UnitTypeQueueable(u->GetBuildUnit(0)))  // Hyperion: <= 0x69
      return 1;
    return statdraw_query_unit();
  }
  return statdraw_query_unit();
}

// statdraw_query_unit -- oracle 0x4A55E0, 81 B, the template. The shield shift
// is arithmetic (`sar`) though CUnit declares lShields a DWORD; the cast gives
// it. The energy test reads the high byte of wEnergy (+0xA3). Each compare
// keeps 1.17's operand order: the global first in the three saved-value tests,
// the unit first in the ownership test.
int __fastcall statdraw_query_unit() {
  CUnit *u = gpStatUnit;
  if (sgdwSavedHP[0] != u->lHitPoints)
    return 1;
  if (sgdwSavedShields_g != static_cast<DWORD>(static_cast<LONG>(u->lShields) >> 8))
    return 1;
  if (!gPlayback && u->bPlayer != g_LocalNationID)
    return 0;
  if (sgwSavedMP != static_cast<BYTE>(u->wEnergy >> 8))
    return 1;
  return statdraw_query_special();
}

// Hyperion: the unit the multi-selection grid shows in slot i: the page of the selection
// the console shows (selectlimits.h, SelectionPageFirst), which is its first 12 but in a game
// of Hyperion's selection (stage S).
static CUnit *PageUnit(int i) {
  const int n = SelectionPageFirst() + i;
  return n < SelectionMax() ? StatUnits()[n] : NULL;
}

// statdraw_query_multi -- oracle 0x4A52D0, 69 B. A null selection slot is
// skipped, not a change. The unit array is indexed and the two saved arrays
// walked by pointer, which gives 1.17's three induction variables; indexing all
// three merges the two 4-byte strides onto one index.
int __fastcall statdraw_query_multi() {
  const DWORD *pHP = sgdwSavedHP;
  const WORD *pType = sgwSavedTypes_g;
  for (int i = 0; i < SELECTION_PAGE; i++) {
    CUnit *u = PageUnit(i);
    if (u) {
      if (*pHP != u->lHitPoints)
        return 1;
      if (*pType != u->wType)
        return 1;
    }
    pHP++;
    pType++;
  }
  return 0;
}

// statdraw_save_stats -- oracle 0x4A5830, 73 B. 0xE4 = 228, the units.dat row
// count, stands for no unit.
void __fastcall statdraw_save_stats() {
  for (int i = 0; i < SELECTION_PAGE; i++) {
    CUnit *u = PageUnit(i);
    sgdwSavedHP[i] = u ? u->lHitPoints : 0;
    sgwSavedTypes_g[i] = u ? u->wType : 0xE4;
  }
}

// statdraw_draw_stat -- oracle 0x4A4740, 36 B. An EBP frame for its stack
// argument (`ret 4h`); ECX and EDX reach ctrl_find_ndx untouched.
void __fastcall statdraw_draw_stat(ctrl_t *pDlg, short nIndex, void *pText) {
  ctrl_t *p = ctrl_find_ndx(pDlg, nIndex);
  p->pData = pText;
  ctrl_show(p);
  ctrl_set_update(p);
}

// statdraw_draw_unit_name -- oracle 0x4A4EF0, 50 B. The TU's second template:
// find the node at a fixed index, put a pointer in ctrl_t::pData, show it, and
// tail-jump to ctrl_set_update. Ten bodies here end that way.
void __fastcall statdraw_draw_unit_name(ctrl_t *pDlg) {
  const char *psz = gpStatUnit->GetName();
  ctrl_t *p = ctrl_find_ndx(pDlg, -5);
  p->pData = const_cast<char *>(psz);
  ctrl_show(p);
  ctrl_set_update(p);
}

// statdraw_draw_none -- oracle 0x4A3F00, 64 B. statdraw_draw_unit_name is
// inlined here, so it must stay in this TU. statdata_set_type(0) takes a BYTE
// in CL, so MSVC clears CL alone.
void __fastcall statdraw_draw_none(ctrl_t *pDlg) {
  statdata_set_type(0);
  statdata_clr_all(pDlg);
  statdraw_draw_unit_name(pDlg);
}

// The ownership test, which a replay passes for every unit. It is a helper for
// statdraw_draw_overlord: written inline, MSVC inlines statdraw_draw_unit_name
// into the tail where 1.17 tail-jumps to it (133 B, not 94). The helper raises
// the caller's size enough to tip that decision, and is itself fully inlined.
static int __fastcall statdraw_owned(CUnit *u) {
  return gPlayback || u->bPlayer == g_LocalNationID;
}

// statdraw_draw_overlord -- oracle 0x4A3FD0, 94 B. ECX holds gpStatUnit once
// and survives both guards into CUnit::IsUnitInside, so `u` is a local. Three
// tail jumps (draw_transport, draw_unit_name and draw_unit): this body needs
// draw_unit_name not inlined, where draw_none, draw_powerup and draw_unit need
// it inlined.
void __fastcall statdraw_draw_overlord(ctrl_t *pDlg) {
  CUnit *u = gpStatUnit;
  if (statdraw_owned(u) && !(u->dwFlags & 0x40000000)) {
    if (u->IsUnitInside()) {
      statdraw_draw_transport(pDlg);
      return;
    }
    statdraw_draw_depot(pDlg);
    statdraw_draw_hp(pDlg);
    statdraw_draw_properties(pDlg);
    statdraw_draw_unit_name(pDlg);
    return;
  }
  statdraw_draw_unit(pDlg);
}

// statdraw_draw_powerup -- oracle 0x4A4030, 107 B. The type is a BYTE, compared
// in AL and passed in CL. The tail is statdraw_draw_unit_name inlined.
void __fastcall statdraw_draw_powerup(ctrl_t *pDlg) {
  if (statdata_get_type() != 0x0E) {
    statdata_clr_all(pDlg);
    statdata_set_type(0x0E);
  }
  sgdwSavedHP[0] = gpStatUnit->lHitPoints;
  statdraw_draw_stats(pDlg);
  statdraw_draw_hp(pDlg);
  statdraw_draw_properties(pDlg);
  statdraw_draw_unit_name(pDlg);
}

// statdraw_draw_under_construction -- oracle 0x4A4DC0, 118 B. The first call
// takes the incoming ECX, the dialog, unchanged. The string id is a 16-bit sum
// of GetRace's BYTE and 0x31F. The tail is draw_unit_name's template at index
// -0x1E, written out because 1.17 open-codes it.
void __fastcall statdraw_draw_under_construction(ctrl_t *pDlg) {
  statdata_show_progress_self(pDlg);
  if (statdata_get_type() != 6) {
    statdata_clr_stats(pDlg);
    statdata_set_type(6);
  }
  statdata_set_progress(pDlg, 0x0D, gpStatUnit->GetBuildPercentage());
  char *psz = status_get_str(gpStatUnit->GetRace() + 0x31F);
  ctrl_t *p = ctrl_find_ndx(pDlg, -0x1E);
  p->pData = psz;
  ctrl_show(p);
  ctrl_set_update(p);
}

// statdraw_draw_unit -- oracle 0x4A4E40, 162 B. The three seed stores mirror
// statdraw_query_unit's three tests: hit points as a dword, the high byte of
// wEnergy, and the shields shifted arithmetically.
void __fastcall statdraw_draw_unit(ctrl_t *pDlg) {
  CUnit *u = gpStatUnit;
  sgdwSavedHP[0] = u->lHitPoints;
  sgwSavedMP = static_cast<BYTE>(u->wEnergy >> 8);
  sgdwSavedShields_g = static_cast<DWORD>(static_cast<LONG>(u->lShields) >> 8);
  statdraw_save_unit();
  if (statdata_get_type() != 2) {
    statdata_clr_stats(pDlg);
    statdata_show_stats(pDlg);
    statdata_set_type(2);
  }
  sgwSavedUpgradeLevel_g = 1;
  statdraw_draw_stats(pDlg);
  statdraw_draw_properties(pDlg);
  statdraw_draw_kills(pDlg);
  statdraw_draw_hp(pDlg);
  statdraw_draw_unit_name(pDlg);
}

// statdraw_draw_weapon -- oracle 0x4A50A0, 188 B. The third parameter is a
// stack byte, read twice from its slot. The second is unsigned 16-bit: it is
// zero-extended to index sgpszUnitStats, and its caller walks it with an
// unsigned compare. The level is cached as a DWORD, where statdraw_draw_armor
// caches it as a BYTE.
void __fastcall statdraw_draw_weapon(ctrl_t *pDlg, WORD nIndex, weapon_id_t bWeapon) {
  BYTE bPlayer = gpStatUnit->bPlayer;
  int nLevel = get_upgrade_level(bPlayer, wpn_dmgUpg_g[bWeapon]);
  // "%d ", trailing space included, as at 0x4F007C in 1.17; without it every
  // level renders one character narrower.
  psnprintf(sgpszUnitStats[nIndex], 8, "%d ", nLevel);
  ctrl_t *p = ctrl_find_ndx(pDlg, nIndex + 9);
  ctrl_show(p);
  ctrl_gray(p);
  statdata_icon_t *pIcon = static_cast<statdata_icon_t *>(ctrl_data26(p));
  *ctrl_progress_slot(p) = 0x0C;
  pIcon->pGrp = gpCmdIcons;
  pIcon->wFrame = wpn_icon_g[bWeapon];
  pIcon->wKind = 0;
  pIcon->wParam = bWeapon;
  p->pData = sgpszUnitStats[nIndex];
  ctrl_set_update(p);
  sgwSavedUpgradeLevel_g += static_cast<WORD>(nLevel);
}

// statdraw_query_bldg -- oracle 0x4A5180, 107 B. statdraw_query_unit's four
// tests written out, then two calls; the result is byte_6AAB9A unless
// query_special reports a change.
int __fastcall statdraw_query_bldg() {
  CUnit *u = gpStatUnit;
  if (sgdwSavedHP[0] != u->lHitPoints)
    return 1;
  if (sgdwSavedShields_g != static_cast<DWORD>(static_cast<LONG>(u->lShields) >> 8))
    return 1;
  if (!gPlayback && u->bPlayer != g_LocalNationID)
    return 0;
  if (sgwSavedMP != static_cast<BYTE>(u->wEnergy >> 8))
    return 1;
  if (statdraw_query_needs_compbar(u))
    return 1;
  if (statdraw_query_special())
    return 1;
  return byte_6AAB9A;
}

// statdraw_query_overlord -- oracle 0x4A53A0, 79 B. The first two template
// tests only, then a tail jump to statdraw_query_transport when the unit
// carries anything.
int __fastcall statdraw_query_overlord() {
  CUnit *u = gpStatUnit;
  if (sgdwSavedHP[0] != u->lHitPoints)
    return 1;
  if (sgdwSavedShields_g != static_cast<DWORD>(static_cast<LONG>(u->lShields) >> 8))
    return 1;
  if (u->IsUnitInside())
    return statdraw_query_transport();
  if (gubUnitFoodMade[gpStatUnit->wType] && statdraw_query_depot())
    return 1;
  return 0;
}

// statdraw_query_depot -- oracle 0x4A5230, 105 B. gpStatUnit is re-read at
// every use, not cached. Arguments are evaluated right to left, so bPlayer is
// loaded before the GetRace call.
int __fastcall statdraw_query_depot() {
  if (!gPlayback && gpStatUnit->bPlayer != g_LocalNationID)
    return statdraw_query_unit();
  BYTE bPlayer = gpStatUnit->bPlayer;
  if (static_cast<WORD>(sgwSavedStat1_g)
      == count_get_supplies_available(gpStatUnit->GetRace(), bPlayer)) {
    bPlayer = gpStatUnit->bPlayer;
    if (sgwSavedStat2_g == count_get_supplies_used(gpStatUnit->GetRace(), bPlayer))
      return 0;
  }
  return 1;
}

// statdraw_query_needs_compbar -- oracle 0x4A5320, 127 B. pUnit is ignored and
// gpStatUnit reloaded (statdraw.h has why the argument is real). The `& 2` test
// is negated so the non-building arm falls through; both arms exit with
// `return 1`, so one `xor eax, eax` block is shared; the `& 1` test is written
// in each arm, as 1.17 duplicates it.
// noinline keeps statdraw_save_transport's call; /Ob2 splices it there otherwise.
__declspec(noinline) int __fastcall statdraw_query_needs_compbar(CUnit *pUnit) {
  if (!gPlayback && gpStatUnit->bPlayer != g_LocalNationID)
    return 0;
  if (gpStatUnit->IsATransport())
    return ~gpStatUnit->dwFlags & 1;
  if (!(gpStatUnit->dwFlags & 2)) {
    if (!(gpStatUnit->dwFlags & 1) || gpStatUnit->IsConstructing())
      return 1;
  } else {
    if (!(gpStatUnit->dwFlags & 1) || gpStatUnit->IsConstructing())
      return 1;
    if (gpStatUnit->bTechType != 0x2C || gpStatUnit->bUpgradeType != 0x3D)
      return 1;
  }
  return 0;
}

// statdraw_query_transport -- oracle 0x4A5410, 206 B. 0x6AAB6C is not a global
// but &sgdwSavedHP[1]: element 0 is the transport, 1 to 8 its cargo, walked in
// step with sgwSavedTransport[0..7].
int __fastcall statdraw_query_transport() {
  if (!gPlayback && gpStatUnit->bPlayer != g_LocalNationID)
    return statdraw_query_unit();
  for (int i = 0; i < 8; i++) {
    if (sgwSavedTransport[i] != gpStatUnit->wCargoUnitIds[i])
      return 1;
    CUnit *p = CUnit::FromIdAlive(sgwSavedTransport[i]);
    if (p && sgdwSavedHP[i + 1] != p->lHitPoints)
      return 1;
  }
  if (byte_6AAB9A)
    return 1;
  if (sgdwSavedHP[0] != gpStatUnit->lHitPoints)
    return 1;
  if (sgdwSavedShields_g
      != static_cast<DWORD>(static_cast<LONG>(gpStatUnit->lShields) >> 8))
    return 1;
  if (!gPlayback && gpStatUnit->bPlayer != g_LocalNationID)
    return 0;
  if (sgwSavedMP != static_cast<BYTE>(gpStatUnit->wEnergy >> 8))
    return 1;
  return statdraw_query_special();
}

// The saved stat read at three widths through one register.
union statdraw_saved_t { DWORD dw; WORD w; BYTE b; };

// statdraw_query_special -- oracle 0x4A54E0, 254 B. Reports whether the value
// statdraw_save_unit saved has changed. The saved dword is copied into a local
// union and read as .b, .w and whole; that keeps the one entry load in EAX.
// The type is a WORD local.
int __fastcall statdraw_query_special() {
  CUnit *u = gpStatUnit;
  statdraw_saved_t sv;
  sv.dw = sgwSavedStat1_g;
  WORD nType = u->wType;
  int nChanged = 0;
  if (nType == 0x48 || nType == 0x52)
    nChanged = sv.b != static_cast<BYTE>(u->bHangarCountBuilding + u->bHangarCount);
  else if (nType == 0x53 || nType == 0x51)
    nChanged = sv.b != u->bHangarCount;
  if (!(u->dwFlags & 0x40000000) && nType != 0x28) {
    if (gubUnitFoodMade[nType]) {
      BYTE bPlayer = u->bPlayer;
      if (static_cast<WORD>(sgwSavedStat1_g) == count_get_supplies_available(u->GetRace(), bPlayer)) {
        bPlayer = gpStatUnit->bPlayer;
        if (sgwSavedStat2_g == count_get_supplies_used(gpStatUnit->GetRace(), bPlayer))
          return 0;
      }
      return 1;
    }
    if (gulUnitTypeFlags[nType] & 0x2000)
      return sv.w != u->wResourceAmount;
    return sv.dw != 0;
  }
  if (!nChanged && sgwSavedStat2_g == u->wTimeToLive / 10)
    return 0;
  return 1;
}

// Snapshot a transport's cargo ids and hit points, the component-bar flag and
// the transport's own vitals, then the per-unit extras.
// 0x6AAB6C is &sgdwSavedHP[1]: the cargo's HP follows the transport's own.
void __fastcall statdraw_save_transport() {
  DWORD *pHP = &sgdwSavedHP[1];
  for (int i = 0; i < 8; i++) {
    unit_id_t wId = unit_loaded_units(gpStatUnit)[i];  // Hyperion: WORD in 1.17
    sgwSavedTransport[i] = wId;
    CUnit *p = CUnit::FromIdAlive(wId);
    *pHP = p ? p->lHitPoints : 0;
    pHP++;
  }
  byte_6AAB9A = static_cast<BYTE>(statdraw_query_needs_compbar(gpStatUnit));
  CUnit *u = gpStatUnit;
  sgdwSavedHP[0] = u->lHitPoints;
  sgwSavedMP = static_cast<BYTE>(u->wEnergy >> 8);
  sgdwSavedShields_g = static_cast<DWORD>(static_cast<LONG>(u->lShields) >> 8);
  statdraw_save_unit();
}

// statdraw_save_unit -- oracle 0x4A5910, 128 B. Saves the hangar count of a
// carrier or reaver, then a tenth of the +0x110 time to live for a unit with
// flag 0x40000000 or of type 0x28. Each arm stores the count itself; one shared
// store gives the unit EAX instead of the oracle's ECX. noinline because two
// callers here would otherwise inline it.
__declspec(noinline) void __fastcall statdraw_save_unit() {
  CUnit *u = gpStatUnit;
  WORD wType = u->wType;
  if (wType == 0x48 || wType == 0x52)
    *reinterpret_cast<BYTE *>(&sgwSavedStat1_g) = static_cast<BYTE>(u->bHangarCountBuilding + u->bHangarCount);
  else if (wType == 0x53 || wType == 0x51)
    *reinterpret_cast<BYTE *>(&sgwSavedStat1_g) = u->bHangarCount;
  else
    goto not_hangar;
  if (!(u->dwFlags & 0x40000000))
    return;
not_hangar:
  if (!(u->dwFlags & 0x40000000) && u->wType != 0x28) {
    sgwSavedStat1_g = 0;
    return;
  }
  sgwSavedStat2_g = static_cast<WORD>(*reinterpret_cast<WORD *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, wTimeToLive)) / 10);
}

// statdraw_draw_transport -- oracle 0x4A4AF0, 155 B. The ownership test is
// written twice, as 1.17 has it on both sides of CUnit::IsUnitInside, and the
// three failure exits share one tail. `u` is assigned twice so neither live
// range crosses a call and both stay in ECX; one range across the calls takes
// ESI and pDlg EDI, a callee-saved register more than 1.17 uses.
void __fastcall statdraw_draw_transport(ctrl_t *pDlg) {
  CUnit *u = gpStatUnit;
  if ((gPlayback || u->bPlayer == g_LocalNationID) &&
      !(u->dwFlags & 0x40000000)) {
    statdraw_save_transport();
    BOOL bInside = gpStatUnit->IsUnitInside();
    u = gpStatUnit;
    if (bInside) {
      if (gPlayback || u->bPlayer == g_LocalNationID) {
        if (statdata_get_type() != 0x0B) {
          statdata_clr_stats(pDlg);
          statdata_set_type(0x0B);
        }
        statdraw_draw_transport_slots(pDlg);
        statdraw_draw_hp(pDlg);
        statdraw_draw_unit_name(pDlg);
        return;
      }
    }
  }
  if (u->dwFlags & 2)
    statdraw_draw_bldg(pDlg);
  else
    statdraw_draw_unit(pDlg);
}

// statdraw_draw_shields -- oracle 0x4A4540, 160 B. Upgrade 0x0F is Protoss
// Plasma Shields; the level goes into the caller's row of sgpszUnitStats, and
// the row's control is the node at index nIndex + 9. The row's string is loaded
// into a local before the +4 dword store, as 1.17 orders it.
void __fastcall statdraw_draw_shields(ctrl_t *pDlg, WORD nIndex) {
  BYTE bPlayer = gpStatUnit->bPlayer;
  int nLevel = get_upgrade_level(bPlayer, 0x0F);
  // "%d ", trailing space included, as at 0x4F007C in 1.17; without it every
  // level renders one character narrower.
  psnprintf(sgpszUnitStats[nIndex], 8, "%d ", nLevel);

  ctrl_t *p = ctrl_find_ndx(pDlg, static_cast<short>(nIndex + 9));
  ctrl_show(p);
  ctrl_gray(p);
  statdata_icon_t *pIcon = static_cast<statdata_icon_t *>(ctrl_data26(p));
  *ctrl_progress_slot(p) = 0x0C;
  pIcon->pGrp = gpCmdIcons;
  pIcon->wParam = 0x0F;
  // One dword store over wFrame and wKind: no order of the three field stores
  // gets MSVC to merge that pair and keep the +8 store ahead of it.
  char *psz = sgpszUnitStats[nIndex];
  *reinterpret_cast<DWORD *>(&pIcon->wFrame) = 0x00020136;
  p->pData = psz;
  ctrl_set_update(p);
  sgwSavedUpgradeLevel_g += static_cast<WORD>(nLevel);
}

// The panel's text buffers and the tables the drawers below read.
#ifdef OC_DLL_MODE
  #ifndef sgszStatSupplyMade_g
  #define sgszStatSupplyMade_g (reinterpret_cast<char *>(OC_VA(0x6AADE0u)))
  #endif
  #ifndef sgszUnitShieldsStat
  #define sgszUnitShieldsStat (reinterpret_cast<char *>(OC_VA(0x6AB0F4u)))
  #endif
  #ifndef sgszUnitLifetimeStat
  #define sgszUnitLifetimeStat (reinterpret_cast<char *>(OC_VA(0x6AB114u)))
  #endif
  #ifndef gxUnitLife
  #define gxUnitLife (reinterpret_cast<LONG *>(OC_VA(0x520708u)))
  #endif
  #ifndef guwUnitEnergy
  #define guwUnitEnergy (reinterpret_cast<WORD *>(OC_VA(0x520540u)))
  #endif
  #ifndef gbUnitHasShields
  #define gbUnitHasShields (reinterpret_cast<BYTE *>(OC_VA(0x520458u)))
  #endif
  #ifndef techdata_timeCost_g
  #define techdata_timeCost_g (reinterpret_cast<WORD *>(OC_VA(0x51F810u)))
  #endif
  #ifndef tech_icon
  #define tech_icon (reinterpret_cast<WORD *>(OC_VA(0x51F970u)))
  #endif
  #ifndef upg_icon
  #define upg_icon (reinterpret_cast<WORD *>(OC_VA(0x524C50u)))
  #endif
  #ifndef gubUnitArmorType
  #define gubUnitArmorType (reinterpret_cast<BYTE *>(OC_VA(0x521BC0u)))
  #endif
  #ifndef sgszStatCrunchy_g
  #define sgszStatCrunchy_g (reinterpret_cast<char *>(OC_VA(0x6AABD0u)))
  #endif
  #ifndef sgszStatSupplyUsed_g
  #define sgszStatSupplyUsed_g (reinterpret_cast<char *>(OC_VA(0x6AACD8u)))
  #endif
  #ifndef gpCmdBtns
  #define gpCmdBtns (*reinterpret_cast<GROUP **>(OC_VA(0x6AAA70u)))
  #endif
  #ifndef sgszStatSupplyAvailable_g
  #define sgszStatSupplyAvailable_g (reinterpret_cast<char *>(OC_VA(0x6AAEE8u)))
  #endif
  #ifndef sgszStatSupplyMax
  #define sgszStatSupplyMax (reinterpret_cast<char *>(OC_VA(0x6AAFF0u)))
  #endif
  #ifndef sgszReplayProgress
  #define sgszReplayProgress (reinterpret_cast<char *>(OC_VA(0x6AB138u)))
  #endif
  #ifndef sgszReplayTime_g
  #define sgszReplayTime_g (reinterpret_cast<char *>(OC_VA(0x6AB1A0u)))
  #endif
  #ifndef sgszReplaySpeed_g
  #define sgszReplaySpeed_g (reinterpret_cast<char *>(OC_VA(0x6AB208u)))
  #endif
  #ifndef sgbReplayShowPaused_g
  #define sgbReplayShowPaused_g (*reinterpret_cast<int *>(OC_VA(0x67CC80u)))
  #endif
  #ifndef gubUnitWeaponAir
  #define gubUnitWeaponAir (reinterpret_cast<BYTE *>(OC_VA(0x5213A8u)))
  #endif
#else
extern GROUP *gpCmdBtns;
extern char sgszStatSupplyAvailable_g[0x108] OC_UNALIGNED;
extern char sgszStatSupplyMax[0x104];
extern weapon_id_t gubUnitWeaponAir[228] OC_UNALIGNED;
extern char sgszReplayProgress[0x68] OC_UNALIGNED;
extern char sgszReplayTime_g[0x68];
extern char sgszReplaySpeed_g[0x64] OC_UNALIGNED;
extern int sgbReplayShowPaused_g;
extern char sgszStatCrunchy_g[0x108];
extern char sgszStatSupplyUsed_g[0x108] OC_UNALIGNED;
extern WORD upg_icon[];
extern upgrade_id_t gubUnitArmorType[228];
extern WORD techdata_timeCost_g[44];
extern WORD tech_icon[44];
extern char sgszStatSupplyMade_g[0x108];
extern char sgszUnitShieldsStat[0x20] OC_UNALIGNED;
extern char sgszUnitLifetimeStat[0x20] OC_UNALIGNED;
extern LONG gxUnitLife[228] OC_UNALIGNED;
extern WORD guwUnitEnergy[228] OC_UNALIGNED;
extern BYTE gbUnitHasShields[228] OC_UNALIGNED;
#endif
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
  #ifndef sgbReplayPaused_g
  #define sgbReplayPaused_g (*reinterpret_cast<int *>(OC_VA(0x67CC7Cu)))
  #endif
#else
extern int sgbReplayPaused_g;
#endif
#ifdef OC_DLL_MODE
  #ifndef gOptions
  #define gOptions (*reinterpret_cast<DWORD *>(OC_VA(0x6D3130u)))
  #endif
#else
extern DWORD gOptions;
#endif

// Fills the multi-selection grid from the selection, one button per unit, and
// hides the buttons past the end.
void __fastcall statdraw_draw_multi(ctrl_t *pDlg) {
  if (statdata_get_type() != 1) {
    statdata_clr_all(pDlg);
    statdata_set_type(1);
  }
  statdraw_save_stats();
  ctrl_t *p = ctrl_find_ndx(pDlg, 0x21);
  if (!p)
    return;
  for (int i = 0; p; i++) {
    if (i >= SELECTION_PAGE) {
      while (p) {
        ctrl_hide(p);
        if (p->nIndex == 0x2C)
          break;
        p = p->pNext;
      }
      return;
    }
    CUnit *u = PageUnit(i);
    if (u) {
      statdata_select_t *pSel = static_cast<statdata_select_t *>(ctrl_data26(p));
      pSel->pUnit = u;
      pSel->wFrame = ModUnitArtFrame(MOD_ART_GRPWIRE, u->wType);  // Hyperion: a mod's unit's own or its looks'
      ctrl_show(p);
      ctrl_set_update(p);
      p = p->pNext;
    }
  }
}

// Shows the unit's armor upgrade level and icon in stat row nIndex and adds
// the level to the saved upgrade total. The level is cached as a BYTE.
void __fastcall statdraw_draw_armor(ctrl_t *pDlg, WORD nIndex) {
  BYTE bLevel = gpStatUnit->ArmorUpgrades();
  psnprintf(sgpszUnitStats[nIndex], 8, "%d ", bLevel);
  ctrl_t *p = ctrl_find_ndx(pDlg, nIndex + 9);
  ctrl_show(p);
  ctrl_gray(p);
  statdata_icon_t *pIcon = static_cast<statdata_icon_t *>(ctrl_data26(p));
  *ctrl_progress_slot(p) = 0x0C;
  pIcon->pGrp = gpCmdIcons;
  pIcon->wFrame = upg_icon[gubUnitArmorType[gpStatUnit->GetTypeEx()]];
  pIcon->wKind = 1;
  pIcon->wParam = gubUnitArmorType[gpStatUnit->GetTypeEx()];
  p->pData = sgpszUnitStats[nIndex];
  ctrl_set_update(p);
  sgwSavedUpgradeLevel_g += bLevel;
}

// Shows a silo's nuke count (0 or 1) in the given stat row.
void __fastcall statdraw_draw_nuke(ctrl_t *pDlg, WORD nIndex) {
  psnprintf(sgpszUnitStats[nIndex], 8, "%d ", gpStatUnit->pUnionD0 != NULL);
  ctrl_t *p = ctrl_find_ndx(pDlg, static_cast<short>(nIndex + 9));
  ctrl_show(p);
  ctrl_gray(p);
  statdata_icon_t *pIcon = static_cast<statdata_icon_t *>(ctrl_data26(p));
  *ctrl_progress_slot(p) = 0x0C;
  pIcon->pGrp = gpCmdIcons;
  pIcon->wFrame = 0x0E;
  pIcon->wKind = 3;
  pIcon->wParam = 0x0E;
  p->pData = sgpszUnitStats[nIndex];
  ctrl_set_update(p);
}

// The carrier and reaver panel: the hangar queue while it is building, else
// the plain unit panel.
void __fastcall statdraw_draw_carrier(ctrl_t *pDlg) {
  CUnit *u = gpStatUnit;
  if (!gPlayback && u->bPlayer != g_LocalNationID) {
    statdraw_draw_unit(pDlg);
    return;
  }
  BYTE bCompbar = static_cast<BYTE>(statdraw_query_needs_compbar(u));
  u = gpStatUnit;
  byte_6AAB9A = bCompbar;
  sgdwSavedHP[0] = u->lHitPoints;
  sgwSavedMP = static_cast<BYTE>(u->wEnergy >> 8);
  sgdwSavedShields_g = static_cast<DWORD>(static_cast<LONG>(u->lShields) >> 8);
  statdraw_save_unit();
  if (UnitTypeQueueable(u->GetBuildUnit(0))) {  // Hyperion: <= 0x69
    statdraw_draw_training_unit(pDlg);
    statdraw_draw_hp(pDlg);
    statdraw_draw_unit_name(pDlg);
    return;
  }
  statdraw_draw_unit(pDlg);
}

// Shows the kill count, or hides it for units that cannot attack and have no
// kills, for the two unit types 0x2F and 0x32, and for an owned hallucination.
void __fastcall statdraw_draw_kills(ctrl_t *pDlg) {
  CUnit *u = gpStatUnit;
  WORD wType = u->wType;
  if (wType == 0x2F || wType == 0x32 ||
      ((u->dwFlags & 0x40000000) && !gPlayback && u->bPlayer == g_LocalNationID)) {
    ctrl_hide(ctrl_find_ndx(pDlg, -0x15));
    return;
  }
  if (!u->CanAttack() && !gpStatUnit->bKillCount) {
    ctrl_hide(ctrl_find_ndx(pDlg, -0x15));
    return;
  }
  psnprintf(sgszStatSupplyMade_g, 0x104, "%s %d", status_get_str(0x2FC),
            gpStatUnit->bKillCount);
  ctrl_t *p = ctrl_find_ndx(pDlg, -0x15);
  p->pData = sgszStatSupplyMade_g;
  ctrl_show(p);
  ctrl_set_update(p);
}

// Shows a vulture's spider-mine count when the tech is available; returns whether
// it drew anything. The owner byte is read into a local before the call, which
// spills pDlg. The icon is three WORD stores with wKind = 3: MSVC keeps the shared 3
// in EBX for the call and still merges wFrame and wKind into one dword store.
// pIcon is __restrict (the control's own payload); without it the payload form
// moves that dword store above the string-table load.
int __fastcall statdraw_draw_landmines(ctrl_t *pDlg, WORD nIndex) {
  BYTE bPlayer = gpStatUnit->bPlayer;
  if (TechUseAllowed(3, bPlayer, gpStatUnit) != 1)
    return 0;
  psnprintf(sgpszUnitStats[nIndex], 8, "%d ", gpStatUnit->bSpiderMineCount);
  ctrl_t *p = ctrl_find_ndx(pDlg, static_cast<short>(nIndex + 9));
  ctrl_show(p);
  ctrl_gray(p);
  statdata_icon_t *__restrict pIcon = static_cast<statdata_icon_t *>(ctrl_data26(p));
  *ctrl_progress_slot(p) = 0x0C;
  pIcon->pGrp = gpCmdIcons;
  pIcon->wFrame = 0x0D;
  pIcon->wKind = 3;
  pIcon->wParam = 0x0D;
  char *psz = sgpszUnitStats[nIndex];
  p->pData = psz;
  ctrl_set_update(p);
  return 1;
}

// Shows the hangar count: interceptors inside plus building for a carrier,
// scarabs for a reaver.
void __fastcall statdraw_draw_interceptors(ctrl_t *pDlg, WORD nIndex) {
  CUnit *u = gpStatUnit;
  BYTE bCount = 0;
  WORD wType = u->wType;
  if (wType == 0x53 || wType == 0x51)
    bCount = u->bHangarCount;
  else if (wType == 0x48 || wType == 0x52)
    bCount = u->bHangarCountBuilding + u->bHangarCount;
  psnprintf(sgpszUnitStats[nIndex], 8, "%d ", bCount);
  ctrl_t *p = ctrl_find_ndx(pDlg, static_cast<short>(nIndex + 9));
  ctrl_show(p);
  ctrl_gray(p);
  statdata_icon_t *pIcon = static_cast<statdata_icon_t *>(ctrl_data26(p));
  *ctrl_progress_slot(p) = 0x0C;
  pIcon->pGrp = gpCmdIcons;
  pIcon->wFrame = gpStatUnit->GetHangarType();
  pIcon->wKind = 3;
  pIcon->wParam = gpStatUnit->GetHangarType();
  p->pData = sgpszUnitStats[nIndex];
  ctrl_set_update(p);
}

// The archon panel while the merge is in progress: the merge bar and its
// label, then hit points and name; the plain unit panel otherwise.
void __fastcall statdraw_draw_archon(ctrl_t *pDlg) {
  CUnit *u = gpStatUnit;
  if ((gPlayback || u->bPlayer == g_LocalNationID) && u->bOrderId == 0x6A) {
    statdata_show_progress_self(pDlg);
    if (statdata_get_type() != 6) {
      statdata_clr_stats(pDlg);
      statdata_set_type(6);
    }
    statdata_set_progress(pDlg, 0x0D, gpStatUnit->GetBuildPercentage());
    char *psz = status_get_str(0x316);
    ctrl_t *p = ctrl_find_ndx(pDlg, -0x1E);
    p->pData = psz;
    ctrl_show(p);
    ctrl_set_update(p);
    statdraw_draw_hp(pDlg);
    statdraw_draw_unit_name(pDlg);
    return;
  }
  statdraw_draw_unit(pDlg);
}

// The add-on construction panel: its progress bar, its icon and the race's
// "building add-on" label. Nothing for another player's building.
void __fastcall statdraw_draw_addon(ctrl_t *pDlg) {
  if (!gPlayback && gpStatUnit->bPlayer != g_LocalNationID)
    return;
  if (statdata_get_type() != 9) {
    statdata_clr_stats(pDlg);
    statdata_show_progress(pDlg);
    statdata_set_type(9);
  }
  CUnit *pAddon = gpStatUnit->GetConstructionTarget();
  statdata_set_progress(pDlg, 0x10, pAddon->GetBuildPercentage());
  WORD wType = pAddon->GetTypeEx();
  ctrl_t *p = ctrl_find_ndx(pDlg, 0x11);
  statdata_icon_t *pIcon = static_cast<statdata_icon_t *>(ctrl_data26(p));
  *ctrl_progress_slot(p) = 2;
  pIcon->pGrp = gpCmdIcons;
  pIcon->wKind = 3;
  pIcon->wFrame = ModUnitArtFrame(MOD_ART_ICON, wType);  // Hyperion: a mod's unit's own or its looks'
  pIcon->wParam = wType;
  ctrl_set_update(p);
  char *psz = status_get_str(pAddon->GetRace() + 0x313);
  p = ctrl_find_ndx(pDlg, -0x28);
  p->pData = psz;
  ctrl_show(p);
  ctrl_set_update(p);
}

// The egg panel: morph progress, the icon of what it becomes, the race's
// "morphing" label, hit points and name. The plain unit panel otherwise.
void __fastcall statdraw_draw_egg(ctrl_t *pDlg) {
  CUnit *u = gpStatUnit;
  if ((gPlayback || u->bPlayer == g_LocalNationID) && !(u->dwFlags & 0x40000000)) {
    if (statdata_get_type() != 0x0A) {
      statdata_clr_stats(pDlg);
      statdata_show_progress(pDlg);
      statdata_set_type(0x0A);
    }
    statdata_set_progress(pDlg, 0x10, gpStatUnit->GetBuildPercentage());
    ctrl_t *p = ctrl_find_ndx(pDlg, 0x11);
    statdata_icon_t *pIcon = static_cast<statdata_icon_t *>(ctrl_data26(p));
    *ctrl_progress_slot(p) = 2;
    pIcon->pGrp = gpCmdIcons;
    pIcon->wFrame = ModUnitArtFrame(MOD_ART_ICON, gpStatUnit->wBuildQueue[gpStatUnit->bBuildQueueSlot]);  // Hyperion
    pIcon->wKind = 3;
    pIcon->wParam = gpStatUnit->wBuildQueue[gpStatUnit->bBuildQueueSlot];
    ctrl_set_update(p);
    char *psz = status_get_str(gpStatUnit->GetRace() + 0x303);
    p = ctrl_find_ndx(pDlg, -0x28);
    p->pData = psz;
    ctrl_show(p);
    ctrl_set_update(p);
    statdraw_draw_hp(pDlg);
    statdraw_draw_unit_name(pDlg);
    return;
  }
  statdraw_draw_unit(pDlg);
}

// Hit points (and shields) as "cur/max", coloured by damage state; blank when
// the numbers would not fit. Then the energy line.
void __fastcall statdraw_draw_hp(ctrl_t *pDlg) {
  CUnit *u = gpStatUnit;
  WORD wType = u->wType;
  ctrl_t *p;
  // Hyperion: the maximums at the owner's handicap
  int nMax = HandicapLife(gxUnitLife[wType], u->bPlayer) >> 8;
  if (!nMax)
    nMax = (u->lHitPoints + 0xFF) >> 8;
  if ((nMax && nMax > 9999) || HandicapShields(guwUnitEnergy[wType], u->bPlayer) > 9999 || (u->dwFlags & 0x4000000)) {
    p = ctrl_find_ndx(pDlg, -7);
    p->pData = NULL;
  } else {
    BYTE bColor;
    if (u->IsBurning())
      bColor = 6;
    else
      bColor = u->IsOnFire() ? 3 : 7;
    if (!gbUnitHasShields[wType]) {
      psnprintf(sgszUnitShieldsStat, 0x20, "%c%d/%d", static_cast<BYTE>(bColor),
                (u->lHitPoints + 0xFF) >> 8, u->GetLife());
    } else {
      int nHP = (u->lHitPoints + 0xFF) >> 8;
      int nShields = static_cast<LONG>(u->lShields) >> 8;
      psnprintf(sgszUnitShieldsStat, 0x20, "%d/%d %c%d/%d",
                nShields, HandicapShields(guwUnitEnergy[u->GetTypeEx()], u->bPlayer),
                static_cast<BYTE>(bColor), nHP, u->GetLife());
    }
    p = ctrl_find_ndx(pDlg, -7);
    p->pData = sgszUnitShieldsStat;
  }
  ctrl_show(p);
  ctrl_set_update(p);
  statdraw_draw_mana(pDlg);
}

// Energy as "cur/max" for an owned spellcaster, or the remaining lifetime of a
// hallucination or a type-0x28 unit; blank otherwise.
void __fastcall statdraw_draw_mana(ctrl_t *pDlg) {
  DWORD bReplay = gPlayback;
  CUnit *u = gpStatUnit;
  DWORD nLocal = g_LocalNationID;
  WORD wType = u->wType;
  ctrl_t *p;
  if ((gulUnitTypeFlags[wType] & 0x200000) && !(u->dwFlags & 0x40000000) &&
      (bReplay || u->bPlayer == nLocal)) {
    BYTE bEnergy = static_cast<BYTE>(u->wEnergy >> 8);
    psnprintf(sgszUnitLifetimeStat, 0x20, "\x04%d/%d", bEnergy,
              u->GetMaxEnergyReal());
    p = ctrl_find_ndx(pDlg, -8);
    p->pData = sgszUnitLifetimeStat;
  } else if (((u->dwFlags & 0x40000000) || wType == 0x28) &&
             (bReplay || u->bPlayer == nLocal)) {
    WORD wLeft = u->wTimeToLive;
    psnprintf(sgszUnitLifetimeStat, 0x20, "\x04%d/%d", wLeft / 10,
              static_cast<WORD>(u->GetMaxTimeToLive()) / 10);
    p = ctrl_find_ndx(pDlg, -8);
    p->pData = sgszUnitLifetimeStat;
  } else {
    p = ctrl_find_ndx(pDlg, -8);
    p->pData = NULL;
  }
  ctrl_show(p);
  ctrl_set_update(p);
}

// The nuclear silo panel: the nuke's build progress and icon while one is
// being armed, else the stats with the two extra rows hidden.
void __fastcall statdraw_draw_silo(ctrl_t *pDlg) {
  if (UnitTypeQueueable(gpStatUnit->GetBuildUnit(0))) {  // Hyperion: <= 0x69
    ctrl_t *p = ctrl_find_ndx(pDlg, 2);
    if (statdata_get_type() != 0x0D) {
      statdata_clr_stats(pDlg);
      statdata_set_type(0x0D);
    }
    CUnit *pNuke = gpStatUnit->GetConstructionTarget();
    WORD wType;
    int nPercent;
    if (pNuke) {
      wType = pNuke->wType;
      nPercent = pNuke->GetBuildPercentage();
    } else {
      wType = 0;
      nPercent = 0;
    }
    statdata_set_progress(pDlg, 7, static_cast<short>(nPercent));
    psnprintf(sgpszUnitStats[0], 8, "%d ", 1);
    statdata_icon_t *pIcon = static_cast<statdata_icon_t *>(ctrl_data26(p));
    *ctrl_progress_slot(p) = 2;
    pIcon->pGrp = gpCmdIcons;
    pIcon->wFrame = ModUnitArtFrame(MOD_ART_ICON, wType);  // Hyperion: a mod's unit's own or its looks'
    pIcon->wKind = 3;
    pIcon->wParam = wType;
    ctrl_ungray(p);
    p->pData = sgpszUnitStats[0];
    ctrl_show(p);
    ctrl_set_update(p);
    char *psz = status_get_str(gpStatUnit->GetRace() + 0x303);
    p = ctrl_find_ndx(pDlg, -0x0F);
    p->pData = psz;
    ctrl_show(p);
    ctrl_set_update(p);
    return;
  }
  if (statdata_get_type() != 3) {
    statdata_clr_stats(pDlg);
    statdata_show_stats(pDlg);
    statdata_set_type(3);
  }
  statdraw_draw_stats(pDlg);
  ctrl_hide(ctrl_find_ndx(pDlg, -0x14));
  ctrl_hide(ctrl_find_ndx(pDlg, -0x15));
}

// The research panel: progress of the tech being researched, its icon and the
// race's "researching" label (a different one when the building is disabled).
void __fastcall statdraw_draw_researching_tech(ctrl_t *pDlg) {
  if (!gPlayback && gpStatUnit->bPlayer != g_LocalNationID)
    return;
  if (statdata_get_type() != 7) {
    statdata_clr_stats(pDlg);
    statdata_show_progress_research(pDlg);
    statdata_set_type(7);
  }
  CUnit *u = gpStatUnit;
  DWORD dwTotal = techdata_timeCost_g[u->bTechType];
  statdata_set_progress(pDlg, 0x0E,
                        static_cast<short>((dwTotal - u->wRemainingResearchTime) * 100 / dwTotal));
  ctrl_t *p = ctrl_find_ndx(pDlg, 0x0F);
  statdata_icon_t *pIcon = static_cast<statdata_icon_t *>(ctrl_data26(p));
  *ctrl_progress_slot(p) = 2;
  pIcon->pGrp = gpCmdIcons;
  pIcon->wFrame = tech_icon[gpStatUnit->bTechType];
  pIcon->wKind = 4;
  pIcon->wParam = gpStatUnit->bTechType;
  ctrl_ungray(p);
  ctrl_set_update(p);
  WORD wStr = gpStatUnit->GetRace() + 0x300;
  u = gpStatUnit;
  if ((u->dwFlags & 0x400) || u->bLockdownTimer || u->bStasisTimer || u->bParalyzeTimer)
    wStr = u->GetRace() + 0x33C;
  char *psz = status_get_str(wStr);
  p = ctrl_find_ndx(pDlg, -0x23);
  p->pData = psz;
  ctrl_show(p);
  ctrl_set_update(p);
}

// The upgrade panel: progress of the upgrade in progress, its icon and the
// race's "upgrading" label (a different one when the building is disabled).
void __fastcall statdraw_draw_upgrade(ctrl_t *pDlg) {
  if (!gPlayback && gpStatUnit->bPlayer != g_LocalNationID)
    return;
  if (statdata_get_type() != 8) {
    statdata_clr_stats(pDlg);
    statdata_show_progress_research(pDlg);
    statdata_set_type(8);
  }
  DWORD dwTotal = get_upgrade_time(gpStatUnit->bPlayer, gpStatUnit->bUpgradeType);
  statdata_set_progress(pDlg, 0x0E,
                        static_cast<short>((dwTotal - gpStatUnit->wRemainingResearchTime) * 100 / dwTotal));
  ctrl_t *p = ctrl_find_ndx(pDlg, 0x0F);
  statdata_icon_t *pIcon = static_cast<statdata_icon_t *>(ctrl_data26(p));
  *ctrl_progress_slot(p) = 2;
  pIcon->pGrp = gpCmdIcons;
  pIcon->wFrame = upg_icon[gpStatUnit->bUpgradeType];
  pIcon->wKind = 5;
  pIcon->wParam = gpStatUnit->bUpgradeType;
  ctrl_ungray(p);
  ctrl_set_update(p);
  WORD wStr = gpStatUnit->GetRace() + 0x2FD;
  CUnit *u = gpStatUnit;
  if ((u->dwFlags & 0x400) || u->bLockdownTimer || u->bStasisTimer || u->bParalyzeTimer)
    wStr = u->GetRace() + 0x33C;
  char *psz = status_get_str(wStr);
  p = ctrl_find_ndx(pDlg, -0x23);
  p->pData = psz;
  ctrl_show(p);
  ctrl_set_update(p);
}

// The stat row: armor, shields, the ground and air weapons, then hangar,
// mines and nuke counts as they apply; unused cells are hidden.
void __fastcall statdraw_draw_stats(ctrl_t *pDlg) {
  WORD n = 0;
  CUnit *u = gpStatUnit;
  if (!(u->dwFlags & 0x40000000) || (!gPlayback && u->bPlayer != g_LocalNationID)) {
    if (gubUnitArmorType[u->GetTypeEx()] != 0x3C) {
      statdraw_draw_armor(pDlg, 0);
      n = 1;
    }
    u = gpStatUnit;
    if (gbUnitHasShields[u->wType]) {
      statdraw_draw_shields(pDlg, n);
      u = gpStatUnit;
      n++;
    }
    if (u->GetWeaponGnd() != WEAPON_NONE) {
      statdraw_draw_weapon(pDlg, n, gpStatUnit->GetWeaponGnd());
      n++;
    }
    if (gpStatUnit->GetWeaponAir() != WEAPON_NONE &&
        gpStatUnit->GetWeaponAir() != gpStatUnit->GetWeaponGnd()) {
      statdraw_draw_weapon(pDlg, n, gpStatUnit->GetWeaponAir());
      n++;
    }
    WORD wType = gpStatUnit->wType;
    if (wType == 0x48 || wType == 0x52 || wType == 0x53 || wType == 0x51) {
      statdraw_draw_interceptors(pDlg, n);
      n++;
    }
    wType = gpStatUnit->wType;
    if ((wType == 2 || wType == 0x13) && statdraw_draw_landmines(pDlg, n))
      n++;
    if (gpStatUnit->GetTypeEx() == 0x6C) {
      statdraw_draw_nuke(pDlg, n);
      n++;
    }
  }
  for (ctrl_t *p = ctrl_find_ndx(pDlg, n + 9); p && n < 4; p = p->pNext, n++)
    ctrl_hide(p);
}

// A resource field's panel: the remaining minerals or gas, or "depleted".
// The saved amount is the low half of sgwSavedStat1_g.
// The amount is read in both arms of the IsMineralSrc test, so MSVC hoists one
// load between the `test` and its `jz` and the amount takes EAX.
void __fastcall statdraw_draw_resources(ctrl_t *pDlg) {
  *reinterpret_cast<WORD *>(&sgwSavedStat1_g) = gpStatUnit->wResourceAmount;
  if (statdata_get_type() != 0x0F) {
    statdata_clr_stats(pDlg);
    statdata_show_resources(pDlg);
    statdata_set_type(0x0F);
  }
  ctrl_t *p = ctrl_find_ndx(pDlg, -0x0A);
  p->pData = NULL;
  ctrl_show(p);
  ctrl_set_update(p);
  p = ctrl_find_ndx(pDlg, -0x0C);
  p->pData = NULL;
  ctrl_show(p);
  ctrl_set_update(p);
  p = ctrl_find_ndx(pDlg, -0x0D);
  p->pData = NULL;
  ctrl_show(p);
  ctrl_set_update(p);
  WORD wStr;
  int nAmount;
  if (gpStatUnit->IsMineralSrc()) {
    nAmount = gpStatUnit->wResourceAmount;
    wStr = 0x31C;
  } else {
    nAmount = gpStatUnit->wResourceAmount;
    if (static_cast<WORD>(nAmount)) {
      wStr = 0x31D;
    } else {
      char *psz = status_get_str(0x31E);
      p = ctrl_find_ndx(pDlg, -0x0B);
      p->pData = psz;
      ctrl_show(p);
      ctrl_set_update(p);
      goto shields;
    }
  }
  psnprintf(sgszStatSupplyUsed_g, 0x104, "%s %d", status_get_str(wStr), nAmount);
  p = ctrl_find_ndx(pDlg, -0x0B);
  p->pData = sgszStatSupplyUsed_g;
  ctrl_show(p);
  ctrl_set_update(p);
shields:
  if (gbUnitHasShields[gpStatUnit->wType])
    statdraw_draw_shields(pDlg, 0);
  else
    ctrl_hide(ctrl_find_ndx(pDlg, 9));
}

// The status line under the name: hallucination, a disabled building's
// "disabled", a nuke count, other status effects, or the unit's rank. Every
// arm goes through statdraw_draw_stat; the inlined copies are what MSVC
// cross-jumps into the oracle's shared `mov ecx, K / jmp` tails.
void __fastcall statdraw_draw_properties(ctrl_t *pDlg) {
  CUnit *u = gpStatUnit;
  if ((u->dwFlags & 0x40000000) && (gPlayback || u->bPlayer == g_LocalNationID)) {
    statdraw_draw_stat(pDlg, -0x14, status_get_str(0x340));
    return;
  }
  if ((gulUnitTypeFlags[u->wType] & 1) &&
      ((u->dwFlags & 0x400) || u->bLockdownTimer || u->bStasisTimer || u->bParalyzeTimer) &&
      (u->dwFlags & 1) && !u->IsConstructing() &&
      gpStatUnit->bTechType == 0x2C && gpStatUnit->bUpgradeType == 0x3D) {
    statdraw_draw_stat(pDlg, -0x14, status_get_str(gpStatUnit->GetRace() + 0x33C));
    return;
  }
  u = gpStatUnit;
  if (u->bCrunchyCount && (u->dwFlags & 1)) {
    psnprintf(sgszStatCrunchy_g, 0x104, "%s (%d)", status_get_str(0x514),
              u->bCrunchyCount);
    statdraw_draw_stat(pDlg, -0x14, sgszStatCrunchy_g);
    return;
  }
  if (UnitMyopia(u).Any() && (u->dwFlags & 1)) {  // Hyperion: and the blinders past 7 (stage D)
    statdraw_draw_stat(pDlg, -0x14, status_get_str(0x513));
    return;
  }
  if (UnitParasite(u).Any() && (u->dwFlags & 1)) {  // Hyperion: and the parasites past 7 (stage D)
    statdraw_draw_stat(pDlg, -0x14, status_get_str(0x33B));
    return;
  }
  if (u->sub_431A90()) {
    statdraw_draw_stat(pDlg, -0x14, status_get_str(0x33F));
    return;
  }
  if (!(gulUnitTypeFlags[u->wType] & 1)) {
    statdraw_draw_stat(pDlg, -0x14, u->GetRankStr());
    return;
  }
  statdraw_draw_stat(pDlg, -0x14, NULL);
}

// The training queue: progress of the unit in production and the five queue
// slots (the first highlighted), then the race's "training" label.
void __fastcall statdraw_draw_training_unit(ctrl_t *pDlg) {
  ctrl_t *p = ctrl_find_ndx(pDlg, 2);
  if (!gPlayback) {
    // The barrier makes the unit load take ECX, as the oracle's does.
    _ReadWriteBarrier();
    if (gpStatUnit->bPlayer != g_LocalNationID)
      return;
  }
  if (statdata_get_type() != 5) {
    statdata_clr_stats(pDlg);
    statdata_show_progress_queue(pDlg);
    statdata_set_type(5);
  }
  CUnit *pTarget = gpStatUnit->GetConstructionTarget();
  statdata_set_progress(pDlg, 7, pTarget ? pTarget->GetBuildPercentage() : 0);
  for (int i = 0; p && i < 5; i++) {
    WORD wType = gpStatUnit->GetBuildUnit(i);
    psnprintf(sgpszUnitStats[i], 8, i ? "\x03%d\x01 " : "%d ", i + 1);
    WORD wSlot = (i && i < 4) ? 4 : 2;
    statdata_icon_t *pIcon = static_cast<statdata_icon_t *>(ctrl_data26(p));
    *ctrl_progress_slot(p) = wSlot;
    if (wType != 0xE4) {
      pIcon->pGrp = gpCmdIcons;
      pIcon->wFrame = ModUnitArtFrame(MOD_ART_ICON, wType);  // Hyperion: a mod's unit's own or its looks'
      pIcon->wKind = 3;
      pIcon->wParam = wType;
      ctrl_ungray(p);
      p->pData = sgpszUnitStats[i];
    } else {
      pIcon->pGrp = gpCmdBtns;
      pIcon->wFrame = static_cast<WORD>(i + 6);
      pIcon->wKind = 6;
      ctrl_gray(p);
      p->pData = NULL;
    }
    ctrl_set_update(p);
    p = p->pNext;
  }
  WORD wStr;
  if ((gpStatUnit->dwFlags & 2) &&
      ((gpStatUnit->dwFlags & 0x400) || gpStatUnit->bLockdownTimer ||
       gpStatUnit->bStasisTimer || gpStatUnit->bParalyzeTimer))
    wStr = 0x33C;
  else if (gpStatUnit->GetTypeEx() == 0xA0 || gpStatUnit->GetTypeEx() == 0xA7)
    wStr = 0x303;
  else
    wStr = 0x306;
  wStr += gpStatUnit->GetRace();
  char *psz = status_get_str(wStr);
  p = ctrl_find_ndx(pDlg, -0x0F);
  p->pData = psz;
  ctrl_show(p);
  ctrl_set_update(p);
}

// Picks a cargo icon's background frame from the unit's state, offset from
// the slot size's base frame.
static inline WORD statdraw_cargo_frame(CUnit *u, WORD wBase) {
  if (u->dwFlags & 0x40000000)
    return static_cast<WORD>(wBase + 0x12);
  if (gulUnitTypeFlags[u->wType] & 0x40)
    return static_cast<WORD>(wBase + 6);
  if (UnitParasite(u).Any())  // Hyperion: and the parasites past 7 (stage D)
    return static_cast<WORD>(wBase + 0x0C);
  return wBase;
}

// Lays the transport's cargo into the slot grid, largest units first, and
// hides the slots left empty.
void __fastcall statdraw_draw_transport_slots(ctrl_t *pDlg) {
  ctrl_t *p;
  p = ctrl_find_ndx(pDlg, 0x12);
  while (p) {
    ctrl_data26(p) = NULL;
    if (p->nIndex == 0x20)
      break;
    p = p->pNext;
  }
  BYTE bSize = 4;
  BYTE bHold = gubUnitCargoHold[gpStatUnit->wType];
  WORD wLarge = bHold < 7 ? 0x14 : 0x12;
  WORD wMedium = bHold < 7 ? 0x16 : 0x15;
  WORD wSmall = bHold < 7 ? 0x1B : 0x19;
  for (; bSize; bSize >>= 1) {
    int i = 0;
    for (CUnit *pCargo = gpStatUnit->GetTransportUnit(i); i < 8;
         pCargo = gpStatUnit->GetTransportUnit(++i)) {
      if (!pCargo || gubUnitVolume[pCargo->wType] != bSize)
        continue;
      WORD wFrame;
      switch (bSize) {
      case 2:
        p = ctrl_find_ndx(pDlg, wMedium);
        wFrame = statdraw_cargo_frame(pCargo, 0x0F);
        break;
      case 4:
        p = ctrl_find_ndx(pDlg, wLarge);
        wFrame = statdraw_cargo_frame(pCargo, 0x11);
        break;
      default:
        p = ctrl_find_ndx(pDlg, wSmall);
        wFrame = statdraw_cargo_frame(pCargo, 0x0D);
        break;
      }
      *ctrl_progress_slot(p) = wFrame;
      ctrl_data26(p) = pCargo;
      ctrl_show(p);
      ctrl_set_update(p);
      wLarge += bSize >> 2;
      wMedium += bSize >> 1;
      wSmall += bSize;
    }
  }
  for (p = ctrl_find_ndx(pDlg, 0x12); p; p = p->pNext) {
    if (!ctrl_data26(p))
      ctrl_hide(p);
    if (p->nIndex == 0x20)
      break;
  }
}

// A supply depot's panel: the race's supply used, provided, available and
// maximum, then its shields or armour.
void __fastcall statdraw_draw_depot(ctrl_t *pDlg) {
  if (!gPlayback && gpStatUnit->bPlayer != g_LocalNationID) {
    statdraw_draw_unit(pDlg);
    return;
  }
  BYTE bPlayer = gpStatUnit->bPlayer;
  *reinterpret_cast<WORD *>(&sgwSavedStat1_g) =
      static_cast<WORD>(count_get_supplies_available(gpStatUnit->GetRace(), bPlayer));
  bPlayer = gpStatUnit->bPlayer;
  sgwSavedStat2_g = static_cast<WORD>(count_get_supplies_used(gpStatUnit->GetRace(), bPlayer));
  if (statdata_get_type() != 0x0C) {
    statdata_clr_stats(pDlg);
    statdata_show_resources(pDlg);
    statdata_set_type(0x0C);
  }
  BYTE bRace = gpStatUnit->GetRace();
  psnprintf(sgszStatSupplyUsed_g, 0x104, "%s %d", status_get_str(bRace + 0x335),
            (count_get_supplies_used(bRace, gpStatUnit->bPlayer) + 1) / 2);
  psnprintf(sgszStatSupplyMade_g, 0x104, "%s %d", status_get_str(bRace + 0x32F),
            (MutatorFoodMade(gubUnitFoodMade[gpStatUnit->GetTypeEx()]) + 1) / 2);  // Hyperion: boosted
  psnprintf(sgszStatSupplyAvailable_g, 0x104, "%s %d", status_get_str(bRace + 0x332),
            (count_get_supplies_available(bRace, gpStatUnit->bPlayer) + 1) / 2);
  bPlayer = gpStatUnit->bPlayer;
  psnprintf(sgszStatSupplyMax, 0x104, "%s %d", status_get_str(bRace + 0x338),
            (count_food_max(gpStatUnit->GetTypeEx(), bPlayer) + 1) / 2);
  ctrl_t *p = ctrl_find_ndx(pDlg, -0x0A);
  p->pData = sgszStatSupplyUsed_g;
  ctrl_show(p);
  ctrl_set_update(p);
  p = ctrl_find_ndx(pDlg, -0x0B);
  p->pData = sgszStatSupplyMade_g;
  ctrl_show(p);
  ctrl_set_update(p);
  p = ctrl_find_ndx(pDlg, -0x0C);
  p->pData = sgszStatSupplyAvailable_g;
  ctrl_show(p);
  ctrl_set_update(p);
  p = ctrl_find_ndx(pDlg, -0x0D);
  p->pData = sgszStatSupplyMax;
  ctrl_show(p);
  ctrl_set_update(p);
  WORD wType = gpStatUnit->wType;
  if (gbUnitHasShields[wType]) {
    statdraw_draw_shields(pDlg, 0);
    return;
  }
  if (wType != 0x2A && wType != 0x39) {
    ctrl_hide(ctrl_find_ndx(pDlg, 9));
    return;
  }
  statdraw_draw_armor(pDlg, 0);
}

// A building's panel, picked by what it is doing: resources, construction,
// silo, cargo, training, add-on, research, upgrade, supply, or its weapons.
void __fastcall statdraw_draw_bldg(ctrl_t *pDlg) {
  CUnit *u = gpStatUnit;
  sgdwSavedHP[0] = u->lHitPoints;
  sgwSavedMP = static_cast<BYTE>(u->wEnergy >> 8);
  sgdwSavedShields_g = static_cast<DWORD>(static_cast<LONG>(u->lShields) >> 8);
  byte_6AAB9A = static_cast<BYTE>(statdraw_query_needs_compbar(u));
  statdraw_save_unit();
  if (gpStatUnit->IsMineralSrc() || gpStatUnit->wType == 0xBC) {
    statdraw_draw_resources(pDlg);
  } else if (!gPlayback && gpStatUnit->bPlayer != g_LocalNationID) {
    if (statdata_get_type() != 4) {
      statdata_clr_stats(pDlg);
      statdata_set_type(4);
    }
  } else if (!(gpStatUnit->dwFlags & 1)) {
    statdraw_draw_under_construction(pDlg);
  } else if (gpStatUnit->wType == 0x6C) {
    statdraw_draw_silo(pDlg);
  } else if (gpStatUnit->IsATransport()) {
    if (statdata_get_type() != 4) {
      statdata_clr_stats(pDlg);
      statdata_set_type(4);
    }
  } else if (UnitTypeQueueable(gpStatUnit->GetBuildUnit(0))) {  // Hyperion: <= 0x69
    statdraw_draw_training_unit(pDlg);
  } else if (gpStatUnit->IsConstructingAddon() || gpStatUnit->IsBuildingProtoss()) {
    statdraw_draw_addon(pDlg);
  } else if (gpStatUnit->bTechType != 0x2C) {
    statdraw_draw_researching_tech(pDlg);
  } else if (gpStatUnit->bUpgradeType != 0x3D) {
    statdraw_draw_upgrade(pDlg);
  } else if (gubUnitFoodMade[gpStatUnit->wType]) {
    statdraw_draw_depot(pDlg);
  } else if (gpStatUnit->IsRefinery()) {
    statdraw_draw_resources(pDlg);
  } else if (gubUnitWeaponAir[gpStatUnit->GetTypeEx()] != WEAPON_NONE ||
             gpStatUnit->GetWeaponGndEx() != WEAPON_NONE ||
             gbUnitHasShields[gpStatUnit->wType]) {
    if (statdata_get_type() != 3) {
      statdata_clr_stats(pDlg);
      statdata_show_stats(pDlg);
      statdata_set_type(3);
    }
    statdraw_draw_stats(pDlg);
    ctrl_hide(ctrl_find_ndx(pDlg, -0x15));
  } else {
    if (statdata_get_type() != 4) {
      statdata_clr_stats(pDlg);
      statdata_set_type(4);
    }
  }
  statdraw_draw_hp(pDlg);
  statdraw_draw_properties(pDlg);
  statdraw_draw_unit_name(pDlg);
}

// The replay status lines: progress bar, elapsed time, and the speed or the
// paused notice.
void __fastcall statdraw_replay_progress(ctrl_t *pDlg) {
  ctrl_show(ctrl_find_ndx(pDlg, 0x0A));
  statdata_set_progress(pDlg, 0x0A, replay_get_progress());
  char szColor[2] = {3, 0};
  psnprintf(sgszReplayProgress, 0x64, "%s%s", szColor, status_get_str(0x609));
  ctrl_t *p = ctrl_find_ndx(pDlg, -0x2D);
  p->pData = sgszReplayProgress;
  ctrl_show(p);
  ctrl_set_update(p);
  unsigned nTime = replay_dlg_elapsed_seconds();
  unsigned nHours = nTime / 3600;
  unsigned nMinutes = nTime / 60 % 60;
  unsigned nSeconds = nTime % 60;
  char szTime[32];
  if (nHours)
    psnprintf(szTime, 0x20, "%d:%02d:%02d", nHours, nMinutes, nSeconds);
  else
    psnprintf(szTime, 0x20, "%02d:%02d", nMinutes, nSeconds);
  psnprintf(sgszReplayTime_g, 0x64, glues_get_str(0x10), szTime);
  p = ctrl_find_ndx(pDlg, -0x2E);
  p->pData = sgszReplayTime_g;
  ctrl_show(p);
  ctrl_set_update(p);
  if (sgbReplayPaused_g) {
    if (sgbReplayShowPaused_g)
      strcpy(sgszReplaySpeed_g, status_get_str(0x60A));
    else
      sgszReplaySpeed_g[0] = 0;
  } else {
    char szMult[8];
    szMult[0] = 0;
    int nMult = replay_dlg_get_mult();
    if (nMult > 1)
      psnprintf(szMult, 8, " %s %d", gOptions < 3 ? "/" : "x", nMult);
    psnprintf(sgszReplaySpeed_g, 0x64, "%s %s%s", glues_get_str(0x5D),
              glues_get_str(gOptions + 0x55), szMult);
  }
  p = ctrl_find_ndx(pDlg, -0x2F);
  p->pData = sgszReplaySpeed_g;
  ctrl_show(p);
  ctrl_set_update(p);
}
