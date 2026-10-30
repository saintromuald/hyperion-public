// CUnitColor.cpp -- the player-colour tables and the lookups over them. Bodies are in
// oracle VA order.
//
// It is a TU of its own for one six-byte function, because that TU boundary is part of
// another function's bytes. In the oracle get_active_unit_color (0x41D540) is here and its
// caller rle_draw_playerside (0x40E950) is in CImage.cpp, and 1.17 is WPO-off, so the
// cross-TU call cannot be inlined. With both bodies in one TU of src117, /Ob2 spliced the
// six bytes into the caller -- call + mov bl, al (7 B) folding into
// mov bl, byte ptr [5D6E38h] (6 B) -- and both playerside bodies came out 59 B against the
// oracle's 60. A __declspec(noinline) stood in for the boundary until this file existed;
// it is gone from both the declaration and the definition now, and the boundary does the
// work instead.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../port/render/port_teamcolor.h"  // Hyperion: PortTeamRampShown
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX, SELECTION_HUMANS
#include <string.h>        // memcpy -- both permuters snapshot the tables
#include "CUnitColor.h"
#include "CImage.h"        // ours: sub_41D740's own declaration
#include "game.h"          // ours: initPlayerColorMap / permute_colors_by_slot_map
#include "glues.h"         // ours: GameLoadImage
#include "gluScore.h"      // ours: sub_461B70
#include "CUnit.h"         // ours: bPlayer, sub_422EB0, sub_4230E0, CreateTeamCircle
#include "CUnitGUI.h"      // ours: sub_422B40
#include "Teams.h"         // ours: IsTeamGame
#include "net_cmds.h"      // ours: PlayerData, which globals117.h names but only
                           // forward-declares -- it must be complete before that
                           // include or this TU will not compile
#include "TrigAct.h"       // ours: update_unit_sel_colors's own declaration
#include "../gen/globals117.h"   // sgdwCurrentDrawColor @0x5D6E38 -- the
                                 // relative path is the tree's convention; the
                                 // manifest gives this TU /I src117\h alone,
                                 // and src117/gen is deliberately not on it.

// The globals this TU reaches that the registry does not yet carry, dual-spelled: one
// #ifdef per name, never one over the block. Each name is proposed here with its evidence.

// 0x52904A -- the selection-circle colour per player.
// update_unit_sel_colors writes `[ecx+52904Ah]` for ecx over 0..0Bh, which is
// both the element width and the extent; get_selection_circle_color reads the
// same slot. Its last element is the folded absolute 0x529055 the oracle emits
// for index 11.
#ifndef OC_GBSELCIRCLECOLOR_G
#define OC_GBSELCIRCLECOLOR_G
#ifdef OC_DLL_MODE
#define gbSelCircleColor_g (reinterpret_cast<BYTE *>(OC_VA(0x52904Au)))
#else
extern PlayerSelCircleColorTable gbSelCircleColor_g;
#endif
#endif

// 0x529056 -- the team-colour pairs, 12 records of two dwords.
// initPlayerColorMap loads 60h bytes here from game\tunit.pcx and 60h is
// 12 * 8; sub_41D740 reads the halves as `[ecx*8+529056h]` and
// `[ecx*8+52905Ah]`, so the record is two dwords and the stride is 8.
#ifndef OC_GDWTEAMCOLORTBL_G
#define OC_GDWTEAMCOLORTBL_G
#ifdef OC_DLL_MODE
#define gdwTeamColorTbl_g (reinterpret_cast<DWORD (*)[2]>(OC_VA(0x529056u)))
#else
extern PlayerTeamColorTable gdwTeamColorTbl_g OC_UNALIGNED;
#endif
#endif

// 0x5290B6 -- the minimap colour per player, loaded from game\tminimap.pcx
// with an explicit length of 0Ch in the same function.
#ifndef OC_GBMINIMAPCOLOR_G
#define OC_GBMINIMAPCOLOR_G
#ifdef OC_DLL_MODE
#define gbMinimapColor_g (reinterpret_cast<BYTE *>(OC_VA(0x5290B6u)))
#else
extern PlayerMinimapColorTable gbMinimapColor_g;
#endif
#endif

// 0x5264FC -- which colour slot each of the eight players draws in.
// initPlayerColorMap fills exactly eight dwords with two 16-byte stores,
// get_slot_mapping bounds-checks the index against 8, and both colour lookups
// scan `cmp eax, 8h`.
#ifndef OC_GDWCOLORSLOTMAP_G
#define OC_GDWCOLORSLOTMAP_G
#ifdef OC_DLL_MODE
#define gdwColorSlotMap_g (reinterpret_cast<DWORD *>(OC_VA(0x5264FCu)))
#else
extern DWORD gdwColorSlotMap_g[8] OC_UNALIGNED;
#endif
#endif

// 0x671A88 -- sc-refs/sheets/data_naming_1.17.csv names this one, in
// minimap.cpp. Both colour lookups gate on it being 2.
#ifndef OC_GBMINIMAPUNITMODE_G
#define OC_GBMINIMAPUNITMODE_G
#ifdef OC_DLL_MODE
#define gbMinimapUnitMode_g (*reinterpret_cast<BYTE *>(OC_VA(0x671A88u)))
#else
extern BYTE gbMinimapUnitMode_g;
#endif
#endif

// The bodies, in oracle VA order.

// 0x41D4F0. Which colour a player's units are actually drawn in: normally the
// player's own, but in the minimap's ally-colouring mode green for yourself and
// red or blue by alliance, mapped back through the slot table.
BYTE __fastcall get_unit_color_override(BYTE bPlayer)
{
  if (gbMinimapUnitMode_g != 2)
    return bPlayer;

  BYTE bColor;
  if (g_LocalNationID == bPlayer)
    bColor = 2;
  else
    bColor = sgbAlliances_g[g_LocalNationID][bPlayer] ? 7 : 0;

  DWORD dwColor = bColor;
  for (DWORD i = 0; i < 8; i++) {
    if (gdwColorSlotMap_g[i] == dwColor) {
      dwColor = i;
      break;
    }
  }
  return (BYTE)dwColor;
}


//            A DWORD load, so the storage and the return are 32-bit; rle_draw_playerside
//            narrows to AL at its own call site.
DWORD get_active_unit_color() {
  return sgdwCurrentDrawColor;
}

// 0x41D550. The circle drawn under a selected unit, coloured by who owns it.
BYTE __fastcall get_selection_circle_color(BYTE bPlayer)
{
  return gbSelCircleColor_g[bPlayer];
}

// 0x41D560. A player's colour slot, or the player itself once past the table.
int __fastcall get_slot_mapping(int nSlot)
{
  if ((DWORD)nSlot >= 8)
    return nSlot;
  return gdwColorSlotMap_g[nSlot];
}

// 0x41D570. Read both colour tables off disk and start every player in its own slot. The
// oracle's string is game\tunit.pcx, a real directory, so the source needs \\t: one
// backslash compiles to a tab, and a literal behind a fixup is invisible to a byte
// comparison. The absolute-literal form of this body bakes six folded element addresses,
// such as 0x526500 = gdwColorSlotMap_g + 4, which the oracle never forms -- it
// materialises the base and indexes off it -- so only the plain form ships.
void initPlayerColorMap()
{
  GameLoadImage((intptr_t)"game\\tunit.pcx", 0, (intptr_t)gdwTeamColorTbl_g.a, 0x60, 0, 0, 0);
  GameLoadImage((intptr_t)"game\\tminimap.pcx", 0, (intptr_t)gbMinimapColor_g.a, 0x0C, 0, 0, 0);

  for (int i = 0; i < 8; i++)
    gdwColorSlotMap_g[i] = i;
}

// 0x41D740. Install a player's colour as the current one and patch its pair
// into the RLE remap table. get_unit_color_override is spliced in by /Ob2.
void __fastcall sub_41D740(int nColor)
{
  DWORD dwColor = get_unit_color_override((BYTE)nColor);
  sgdwCurrentDrawColor = dwColor;
  // Hyperion: the ramp Settings show the player in, its own unless they say
  // otherwise (PortTeamRampShown); the table itself is left as the game keeps it
  memcpy(&_gbRemapColorTbl[8], PortTeamRampShown(static_cast<int>(dwColor)), 8);
}

// 0x41D5D0. Permute the colour tables by a map's own slot assignment. The
// snapshot is taken first because the loop reads slots it has already written.
// The slot is read after the decrement, and sub_461B70 takes it as an int, so
// it stays in EDX for both the index and the call.
void __fastcall permute_colors_by_map(void *pData, int nSize)
{
  DWORD adwColor[12][2];
  BYTE  abMinimap[12];
  memcpy(adwColor, gdwTeamColorTbl_g.a, sizeof(adwColor));
  memcpy(abMinimap, gbMinimapColor_g.a, sizeof(abMinimap));

  const BYTE *pbSlot = reinterpret_cast<const BYTE *>(pData);
  int i = nSize;
  if (i != 0) {
    DWORD (*p)[2] = &gdwTeamColorTbl_g[i];
    do {
      i--;
      BYTE bFrom = pbSlot[i];
      p--;
      if (bFrom != i) {
        (*p)[0] = adwColor[bFrom][0];
        (*p)[1] = adwColor[bFrom][1];
        gbMinimapColor_g[i] = abMinimap[bFrom];
        sub_461B70(i, bFrom);
      }
    } while (i != 0);
  }
}

// One team-colour record, the two dwords sub_41D740 reads at +0 and +4.
struct color_pair_t {
  /*0x00*/ DWORD dw[2];
};
static_assert(sizeof(color_pair_t) == 0x08, "color_pair_t size  [edx*8] stride");
static_assert(offsetof(color_pair_t, dw) == 0x00, "color_pair_t::dw  [ebp+edx*8-50h]");

// 0x41D690. The same permutation for the eight playable slots, driven by the
// slot map rather than by a map file.
// The volatile read makes the minimap index reload the slot map as the oracle
// does; the minimap pointer local keeps the shipped form's loop exit.
void permute_colors_by_slot_map()
{
  color_pair_t adwColor[8];
  BYTE abMinimap[8];
  memcpy(adwColor, gdwTeamColorTbl_g.a, sizeof(adwColor));
  memcpy(abMinimap, gbMinimapColor_g.a, sizeof(abMinimap));

  BYTE *pbMinimap = gbMinimapColor_g.a;
  for (int i = 8; i != 0; ) {
    i--;
    reinterpret_cast<color_pair_t *>(gdwTeamColorTbl_g.a)[i] = adwColor[gdwColorSlotMap_g[i]];
    pbMinimap[i] = abMinimap[static_cast<volatile DWORD *>(gdwColorSlotMap_g)[i]];
  }
}

// 0x41D7B0. Recolour every selection circle after an alliance change: the
// per-player colour first, then each visible selection's circle.
// The counters are unsigned (the oracle's `jb`), and the alliance row pointer
// steps with the counter; both are needed for the oracle's registers.
void __fastcall update_unit_sel_colors(void)
{
  const BYTE *pbAlly = &sgbAlliances_g[g_LocalNationID][0];
  for (unsigned i = 0; i < 12; i++, pbAlly++) {
    if (gPlayback) {
      if (i == 11)
        gbSelCircleColor_g[i] = 1;
      else
        gbSelCircleColor_g[i] = 0;
    } else if (i == g_LocalNationID) {
      gbSelCircleColor_g[i] = 0;
    } else {
      gbSelCircleColor_g[i] = *pbAlly ? 1 : 2;
    }
  }
  // Hyperion: the slots past 11, allied or not
  for (int i = PLAYER_SLOTS_117; i < PlayerSlots(); i++)
    gbSelCircleColor_g[i] = gPlayback ? 0 : sgbAlliances_g[g_LocalNationID][i] ? 1 : 2;

  for (unsigned i = 0; i < SelectionMax(); i++) {
    CUnit *pUnit = NewUnits()[i];
    if (pUnit == 0)
      break;
    if (gPlayback || pUnit->bPlayer != g_LocalNationID) {
      sub_422B40(pUnit);
      pUnit->sub_422EB0(i);
    }
  }

  if (!IsTeamGame())
    return;

  for (unsigned nPlayer = 0; nPlayer < SELECTION_HUMANS; nPlayer++) {
    if (nPlayer == g_LocalHumanID)
      continue;
    if (gPlayerData[g_LocalHumanID].nTeam != gPlayerData[nPlayer].nTeam)
      continue;
    for (unsigned j = 0; j < SelectionMax(); j++) {
      CUnit *pUnit = SelectedRow(nPlayer)[j];
      if (pUnit == 0)
        break;
      if (gPlayback || pUnit->bPlayer != g_LocalNationID) {
        pUnit->sub_4230E0();
        pUnit->CreateTeamCircle();
      }
    }
  }
}
