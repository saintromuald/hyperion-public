// net_cmds.cpp -- the network-command block: the emitters a UI action calls to
// queue a game command onto the network turn, the receivers that execute one
// when it comes back, and the block's own helpers. Bodies are in oracle VA order.
//
// Each emitter fills a packed local command and calls
// netmgr_queue_cmd(&cmd, sizeof(cmd)); the layouts and the callees' signatures
// are in src117/h/net_cmds.h. The members are assigned in 1.17's order, payload
// then opcode, except in send_net_cmd_RightClick and send_net_cmd_Action (the
// opcode between wX and wY) and send_net_cmd_GroupUnits (the opcode first).
//
// Globals come from gen/globals117.h, included after the game headers so that
// each global's type is complete.

typedef int                 BOOL;
typedef unsigned char       BYTE;
typedef unsigned short      WORD;
#include "platform.h"  // DWORD

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>        // offsetof -- the layout static_asserts in CUnit.h/CSprite.h
#include <string.h>        // strlen, memset
#include <time.h>          // _time64 -- see send_net_cmd_SaveGame

#include "CSprite.h"       // CUnit::pSprite -> wPosX/wPosY (send_net_cmd_Liftoff)
#include "CUnit.h"
#include "Storm.h"         // SStrCopy (ord 501), SStrLen (ord 506)
#include "net_rand.h"     // set_rand_enabled
#include "TrigAct.h"       // unpause_game, AllianceUpdateUnits
#include "minimap.h"       // do_minimap_ping
#include "glues.h"        // CloseAllWindows
#include "filters.h"     // update_message_filter
#include "net_cmds.h"      // packet layouts, unported callees, this TU's declarations
#include "net_misc.h"        // sub_477DA0
#include "net_error.h"     // net_error_show_error
#include "net_player.h"    // leave_game and the per-player record accessors
#include "pause.h"         // RemainingPauses, SubtractPauseCount, player_can_pause
#include "replay.h"        // replay_set_speed
#include "saveload.h"      // load_game
#include "statbtn.h"       // {Research,Upgrade,TechUse}Allowed, TTAllowed
#include "victory.h"       // psnprintf
#include "gds_palette.h"
#include "../gen/globals117.h"
#include "../h/playervision.h"  // Hyperion: the replay viewer past slot 7 (stage D)
#include "../h/typelimits.h"  // Hyperion: a mod's types
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX

// The predicates and helpers other headers declare are included, never restated
// here: a second spelling of one oracle function mangles to a second symbol.

// Picks the candidate in ppUnits[0..nCount-1] nearest to pUnit and removes it
// from the array, writing the previous best into the slot the pick came from, so
// the archon-merge receivers can call it down a shrinking list. The fourth
// parameter, the unit type both callers push, is unused. The best distance is
// compared unsigned. The PosS casts, 1.17's `(short)`, stay at the point of use:
// CBullet::DispatchBounce reads the same sprite field unsigned.
CUnit * __fastcall FindClosestMergeTarget(CUnit *pUnit, CUnit **ppUnits,
                                          int nCount, int nUnitType) {
  CUnit *pBest = 0;
  DWORD dwBest = 0x7FFFFFFF;
  while (nCount != 0) {
    CUnit *pCur = *ppUnits;
    nCount--;
    if (pCur != 0) {
      int dx = PosS(pCur->pSprite->wPosX) - PosS(pUnit->pSprite->wPosX);
      int dy = PosS(pCur->pSprite->wPosY) - PosS(pUnit->pSprite->wPosY);
      DWORD dwDist = (DWORD)(dx * dx + dy * dy);
      if (dwDist < dwBest) {
        *ppUnits = pBest;
        pBest = pCur;
        dwBest = dwDist;
      }
    }
    ppUnits++;
  }
  return pBest;
}

// GetCommandByteSize, oracle 0x4720C0 (33 B), below its port helper: a command's
// size from 1.17's table cmd_size_bytes_g, except the three select commands,
// opcodes 9 to 11, whose size is 2 + 2 * count. The table is read before the
// range test, as in 1.17.
// Hyperion: the sizes of the commands of wide unit ids (h/net_cmds.h), past
// 1.17's table; 0 for an opcode not theirs.
int NetCmdWideSize(const BYTE *pCmd) {
  switch (pCmd[0]) {
  case 0x89: case 0x8A: case 0x8B:
    return pCmd[1] * 4 + 2;
  case NET_CMD_SELECT_CHUNK:  // Hyperion's selection (stage S)
    return pCmd[1] * 4 + 3;
  case 0x94:
    return sizeof(net_cmd_rclick_w_t);
  case 0x95:
    return sizeof(net_cmd_action_w_t);
  case 0x96:  // an order past 1.17's byte
    return sizeof(net_cmd_action_o_t);
  case 0x8D:
    return sizeof(net_cmd_placebox_o_t);
  case 0x97: case 0x98:  // a tech or an upgrade past 1.17's byte
    return sizeof(net_cmd_word_t);
  case 0xA9:
    return sizeof(net_cmd_dword_t);
  }
  return 0;
}

int __fastcall GetCommandByteSize(const net_cmd_select_t *pCmd) {
  if (int nWide = NetCmdWideSize(&pCmd->bOpcode))  // Hyperion: wide commands
    return nWide;
  // nOpcode is an int so that one value serves as the table index and the biased
  // range test; held as a BYTE, the test compiles to another shape.
  int nOpcode = pCmd->bOpcode;
  // Hyperion: an opcode past the table is no command, size 0; 1.17 read past the table's end
  int nBytes = nOpcode < static_cast<int>(sizeof cmd_size_bytes_g / sizeof cmd_size_bytes_g[0])
                   ? cmd_size_bytes_g[nOpcode] : 0;
  if (nOpcode >= 9 && nOpcode <= 11)
    return pCmd->bCount * 2 + 2;
  return nBytes;
}

// 0x4720F0 (16 B). The owner of the first net-selected unit, or 0xFF. The return
// is 8-bit: the failure path is `or al, -1h`.
BYTE __fastcall GetSelectedOwner() {
  CUnit *pUnit = NetSelected()[0];  // Hyperion: selectlimits.h's (stage S)
  if (pUnit)
    return pUnit->bPlayer;
  return 0xFF;
}

// Removes one unit from the twelve-slot net
// selection array, keeping it dense: the removed slot receives the last live
// entry and the last slot is cleared. The 0x672618 in 1.17's code is
// sgpNetSelected_g - 1, the folded [count - 1] subscript, not a global.
// Hyperion: 255 slots in a game of Hyperion's selection (stage S).
void __fastcall sub_472100(CUnit *pUnit) {
  CUnit **ppNet = NetSelected();
  int nCount = 0;
  while (nCount < SelectionMax() && ppNet[nCount] != 0)
    nCount++;
  for (int i = 0; i < nCount; i++) {
    if (ppNet[i] == pUnit) {
      ppNet[i] = ppNet[nCount - 1];
      ppNet[nCount - 1] = 0;
      return;
    }
  }
}

// 0x472190 (22 B). Clears only the first three of the twelve net-selection slots
// (12 bytes in 1.17, not 48) and leaves the rest stale: 1.17's slip, kept.
void __fastcall net_cmds_reset_select() {
  memset(NetSelected(), 0, 3 * sizeof(CUnit *));  // Hyperion: 12 in 1.17
}

// Stores -1 into sgbLastUnitGroup_g @0x67264C, the "no group" value
// send_net_cmd_GroupUnits compares the recalled group against.
void __fastcall net_cmds_init() {
  sgbLastUnitGroup_g = 0xFF;
}

// Walks one player's unit list and drops any attack order aimed at a unit that
// is now allied; the three guards are early-outs inside the loop. Declared in
// src117/h/TrigAct.h.
void __fastcall AllianceUpdateUnits(int nPlayer) {
  for (CUnit *pUnit = CUnit::GetPlayerUnitList(nPlayer); pUnit != 0;
       pUnit = pUnit->pPlayerUnitNext) {
    CUnit *pTarget = pUnit->pOrderTarget;
    if (pTarget == 0)
      continue;
    if (!gubOrderAttack[pUnit->bOrderId])
      continue;
    if (!sgbAlliances_g[nPlayer][pTarget->bPlayer])
      continue;
    pUnit->ClearTargetIfAllied();
  }
}

// 0x472210 (47 B, then a 37-byte two-level switch table). Cases 2 and 23 and
// cases 1, 4, 8 and 25 call two declarations of one folded `ret` stub
// (src117/h/net_cmds.h); any other error does nothing. The {2, 23} group comes
// first in the source, as 1.17 emits it. No header declares AllowedErr.
void __fastcall AllowedErr(int nErr);

void __fastcall AllowedErr(int nErr) {
  switch (nErr) {
    case 2:
    case 23:
      nullsub_folded_with_voice_squelch(g_ActiveHumanID, 0);
      break;
    case 1:
    case 4:
    case 8:
    case 25:
      voice_squelch(g_ActiveHumanID, 0);
      break;
  }
}

// Prototypes no header declares.
int  __fastcall select_insert(CUnit *pUnit, DWORD dwHumanID, int nCount);
void __fastcall sub_49C7D0(DWORD dwHumanID);

// CenterOnSelectionGroup (0x463D60), send_net_cmd_GroupUnits' double-tap
// action. Its parameter is a BYTE: the caller writes CL only.
void __fastcall CenterOnSelectionGroup(BYTE nGroup);

// recv_net_cmd_RightClick with the order id at +9, which moves the queued flag
// to +0x0A, and one more argument to the six-parameter select_target. The
// packet's position is unsigned, as the bounds test reads it, but reaches
// select_target through PosS, 1.17's `(short)`: 1.17 sign-extends it there.
void __fastcall recv_net_cmd_Action(const net_cmd_action_t *pCmd) {
  if (pCmd->wX >= gwMapPixWidth)
    return;
  if (pCmd->wY >= gwMapPixHeight)
    return;
  select_target(PosS(pCmd->wX), PosS(pCmd->wY),
                CUnit::FromIdAlive(pCmd->wTargetID), pCmd->wTargetType,
                pCmd->bOrderID, pCmd->bQueued);
}

// Hyperion: Action with a wide unit id, 0x95.
void recv_net_cmd_ActionWide(const net_cmd_action_w_t *pCmd) {
  if (pCmd->wX >= gwMapPixWidth)
    return;
  if (pCmd->wY >= gwMapPixHeight)
    return;
  select_target(PosS(pCmd->wX), PosS(pCmd->wY),
                CUnit::FromIdAlive(pCmd->dwTargetID), pCmd->wTargetType,
                pCmd->bOrderID, pCmd->bQueued);
}

// Hyperion: Action with an order past 1.17's byte, 0x96. An order the game
// does not have is dropped, as a position off the map is.
void recv_net_cmd_ActionOrder(const net_cmd_action_o_t *pCmd) {
  if (pCmd->wX >= gwMapPixWidth || pCmd->wY >= gwMapPixHeight || !OrderTypeValid(pCmd->wOrderID))
    return;
  select_target(PosS(pCmd->wX), PosS(pCmd->wY),
                CUnit::FromIdAlive(pCmd->dwTargetID), pCmd->wTargetType,
                pCmd->wOrderID, pCmd->bQueued);
}


// Hyperion: Hyperion's selection. A command 1.17 gives only to a
// selection of one unit goes, in a game of Hyperion's selection where several are selected, to
// the one unit the command's receiver picks (a building to train with the shortest queue, the
// first one free to research, the worker nearest the place to build), or to the first unit
// when none is picked, so that its refusal is heard: the receiver runs again with the walk over
// the selection giving that unit alone (SelectOnlySet).
namespace {

struct SelectOnly {
  explicit SelectOnly(CUnit *pUnit) { SelectOnlySet(pUnit); }
  ~SelectOnly() { SelectOnlySet(NULL); }
};

// The active player's selected units, in order, when there are several in a game of Hyperion's
// selection and no unit is picked yet; else 0.
int SeveralSelected(CUnit **ppOut) {
  if (!SelectionWide() || SelectOnlyActive())
    return 0;
  int n = 0;
  for (CUnit *p = select_get_first(); p != 0; p = select_get_next())
    ppOut[n++] = p;
  return n > 1 ? n : 0;
}

// The units in a building's queue.
int QueuedCount(const CUnit *pUnit) {
  int n = 0;
  for (int i = 0; i < 5; i++)
    n += pUnit->wBuildQueue[i] != 0xE4;
  return n;
}

// Free to research or upgrade: neither under way.
bool ResearchFree(const CUnit *pUnit) {
  return pUnit->bTechType == 0x2C && pUnit->bUpgradeType == 0x3D;
}

}  // namespace

// The single-selection guard, TTAllowed on the packet's unit type, then the
// build queue. The type goes to TTAllowed as a bare 16-bit argument, not through
// a local. A failed AddBuildQueue still refreshes the button state, unlike in
// recv_net_cmd_UpgradeBldg; only the type-range reject returns without it.
void __fastcall recv_net_cmd_Build(const net_cmd_word_t *pCmd) {
  // Hyperion: of several, the one allowed to train it with the shortest queue
  CUnit *apSel[SELECTION_WIDE];
  if (const int nSel = SeveralSelected(apSel)) {
    CUnit *pBest = NULL;
    int nBest = 5;
    for (int i = 0; i < nSel; i++) {
      const int nQueued = QueuedCount(apSel[i]);
      if (nQueued < nBest && TTAllowed(pCmd->wArg, g_ActiveNationID, apSel[i]) == 1) {
        pBest = apSel[i];
        nBest = nQueued;
      }
    }
    SelectOnly only(pBest != NULL ? pBest : apSel[0]);
    recv_net_cmd_Build(pCmd);
    return;
  }
  CUnit *pUnit = select_get_first();
  if (pUnit == 0 || select_get_next() != 0)
    return;
  if (TTAllowed(pCmd->wArg, g_ActiveNationID, pUnit) != 1) {
    AllowedErr(get_allowed_err());
    return;
  }
  unsigned int nUnitType = pCmd->wArg;
  if (!UnitTypeQueueable(nUnitType))  // Hyperion: > 0x69 in 1.17
    return;
  if (pUnit->AddBuildQueue(nUnitType))
    pUnit->SetBackgroundOrder(0x26);
  status_set_update();
}

// 0x472330 (179 B). Queues a hangar unit in every selected Carrier or Reaver
// type and sets the build order, with AllowedErr inlined. The hangar type is a
// WORD, widened once for both TTAllowed and AddBuildQueue.
void __fastcall recv_net_cmd_BuildFighter() {
  for (CUnit *pUnit = select_get_first(); pUnit != 0;
       pUnit = select_get_next()) {
    WORD wType = pUnit->wType;
    if (wType != 0x48 && wType != 0x52 && wType != 0x53 && wType != 0x51)
      continue;
    WORD wHangar = pUnit->GetHangarType();
    if (TTAllowed(wHangar, g_ActiveNationID, pUnit) != 1) {
      AllowedErr(get_allowed_err());
      continue;
    }
    if (!pUnit->AddBuildQueue(wHangar))
      continue;
    if (pUnit->sub_41C260())
      continue;
    pUnit->ReplaceBackgroundOrder(0x3F);
  }
  status_set_update();
}

// 0x472410 (179 B). Burrows every selected unit whose units.dat flags allow it
// (bit 20, tested against the whole DWORD). The flag test comes before
// TechUseAllowed, so a unit that cannot burrow raises no error, and the
// already-burrowing tests come after it; the order of the four guards is 1.17's.
void __fastcall recv_net_cmd_Burrow(const net_cmd_byte_t *pCmd) {
  for (CUnit *pUnit = select_get_first(); pUnit != 0;
       pUnit = select_get_next()) {
    if (!(gulUnitTypeFlags[pUnit->wType] & 0x100000))
      continue;
    if (TechUseAllowed(0x0B, g_ActiveNationID, pUnit) != 1) {
      AllowedErr(get_allowed_err());
      continue;
    }
    if (pUnit->dwFlags & 0x10)
      continue;
    if (pUnit->bOrderId == 0x74)
      continue;
    pUnit->sub_42A880(pCmd->bArg != 0, 0x74, 0);
  }
}

// Cancels the nuke of every selected unit of the five ghost types, one wType
// read feeding the five compares.
void __fastcall recv_net_cmd_CancelNuke() {
  for (CUnit *pUnit = select_get_first(); pUnit != 0;
       pUnit = select_get_next()) {
    WORD wType = pUnit->wType;
    if (wType == 1 || wType == 0x10 || wType == 0x64 || wType == 0x63 ||
        wType == 0x68)
      pUnit->CancelNuke();
  }
}

// 0x472530 (38 B). Applies the cheat bits when cheats are allowed.
void __fastcall recv_net_cmd_Cheat(const net_cmd_dword_t *pCmd) {
  if (CheatsAllowed())
    cheat_set_bits(pCmd->dwArg, gPlayerData[g_ActiveHumanID].szName);
}

// 0x472560 (118 B). Cloaks every selected unit allowed its cloaking tech. MSVC
// inlines AllowedErr here, from the same call that stays a call in
// recv_net_cmd_SiegeMode. GetCloakingTech returns a BYTE.
void __fastcall recv_net_cmd_Cloak(const net_cmd_t *pCmd) {
  for (CUnit *pUnit = select_get_first(); pUnit != 0;
       pUnit = select_get_next()) {
    if (TechUseAllowed(pUnit->GetCloakingTech(), g_ActiveNationID, pUnit) != 1) {
      AllowedErr(get_allowed_err());
      continue;
    }
    pUnit->Cloak();
  }
}

// 0x472600 (130 B). Unburrows every selected unit that can burrow (units.dat
// bit 20) and may use tech 0x0B, Burrowing.
void __fastcall recv_net_cmd_Deburrow(const net_cmd_t *pCmd) {
  for (CUnit *pUnit = select_get_first(); pUnit != 0;
       pUnit = select_get_next()) {
    if (!(gulUnitTypeFlags[pUnit->wType] & 0x100000))
      continue;
    if (TechUseAllowed(0x0B, g_ActiveNationID, pUnit) != 1) {
      AllowedErr(get_allowed_err());
      continue;
    }
    pUnit->Deburrow();
  }
}

// 0x4726B0 (120 B). recv_net_cmd_Cloak with SetBackgroundOrder(0x6E) in place
// of Cloak().
void __fastcall recv_net_cmd_Decloak(const net_cmd_t *pCmd) {
  for (CUnit *pUnit = select_get_first(); pUnit != 0;
       pUnit = select_get_next()) {
    if (TechUseAllowed(pUnit->GetCloakingTech(), g_ActiveNationID, pUnit) != 1) {
      AllowedErr(get_allowed_err());
      continue;
    }
    pUnit->SetBackgroundOrder(0x6E);
  }
}

// Four guards and a tail call into CUnit::ExitTransport. The two middle guards
// test one fact twice, as CUnit::EnterTransport sets both: pUnion80 points at
// the transport and dwFlags has UF_InTransport (0x40).
// Hyperion: the body over a unit id of either width.
static void RecvExitTransport(DWORD dwUnitId) {
  CUnit *pUnit = CUnit::FromIdAlive(dwUnitId);
  if (pUnit == 0)
    return;
  if (pUnit->pUnion80 == 0)
    return;
  if (!(pUnit->dwFlags & CUnit::UF_InTransport))
    return;
  if (pUnit->bPlayer != g_ActiveNationID)
    return;
  pUnit->ExitTransport();
}

void __fastcall recv_net_cmd_ExitTransport(const net_cmd_word_t *pCmd) {
  RecvExitTransport(pCmd->wArg);
}

// Hyperion: ExitTransport with a wide unit id, 0xA9.
void recv_net_cmd_ExitTransportWide(const net_cmd_dword_t *pCmd) {
  RecvExitTransport(pCmd->dwArg);
}

// 0x472790 (48 B). Applies a control-group action to groups 0..18: group_modify
// for 0 and 2, group_recall for 1. The action test is a switch, as 1.17
// compiles it.
void __fastcall recv_net_cmd_GroupUnits(const net_cmd_group_t *pCmd) {
  BYTE nGroup = pCmd->bGroup;
  if (nGroup > 18)
    return;
  switch (pCmd->bAction) {
    case 0:
      group_modify(nGroup, 1);
      break;
    case 1:
      group_recall(nGroup);
      break;
    case 2:
      group_modify(nGroup, 0);
      break;
  }
}

// 0x4727C0 (142 B). Gives every selected unit its hold order where it is
// allowed. The order id is a BYTE through CUnit::sub_42A880, sub_42A850 and
// sub_42A8B0, each forwarding it to the next, so those widths change together
// or not at all; 1.17 pushes the whole 4-byte slot with only its low byte set.
void __fastcall recv_net_cmd_HoldPosition(const net_cmd_byte_t *pCmd) {
  for (CUnit *pUnit = select_get_first(); pUnit != 0;
       pUnit = select_get_next()) {
    BYTE bOrder = pUnit->HoldOrder();
    if (OrderAllowed(bOrder, g_ActiveNationID, pUnit) == 1)
      pUnit->sub_42A880(pCmd->bArg != 0, bOrder, 0);
    else
      AllowedErr(get_allowed_err());
  }
}

// 0x472880 (43 B). Cancels the build of the selected unit when exactly one unit,
// one of the active player's, is selected.
void __fastcall recv_net_cmd_Kill() {
  CUnit *pUnit = select_get_first();
  if (pUnit != 0 && select_get_next() == 0 &&
      pUnit->bPlayer == g_ActiveNationID)
    pUnit->CancelBuild();
}

// 0x4728B0 (45 B). recv_net_cmd_Kill's test applied to every selected unit.
void __fastcall recv_net_cmd_KillEgg() {
  for (CUnit *pUnit = select_get_first(); pUnit != 0;
       pUnit = select_get_next()) {
    if (pUnit->bPlayer == g_ActiveNationID)
      pUnit->CancelBuild();
  }
}

// recv_net_cmd_Liftoff
// The single-selection guard, eight rejections, then the order. The zero passed
// to GetBuildUnit is a literal; 1.17 pushes it as `push eax`. 44 and 61 are the
// "none" values of the tech and upgrade fields, so those two tests read "not
// researching" and "not upgrading". The coordinates are read signed, so the
// bounds tests are signed compares.
void __fastcall recv_net_cmd_Liftoff(const net_cmd_point_t *pCmd) {
  // Hyperion: of several, each lifts off where it stands
  CUnit *apSel[SELECTION_WIDE];
  if (const int nSel = SeveralSelected(apSel)) {
    for (int i = 0; i < nSel; i++) {
      net_cmd_point_t cmd = *pCmd;
      cmd.wX = apSel[i]->pSprite->wPosX;  // as send_net_cmd_Liftoff gives the point
      cmd.wY = apSel[i]->pSprite->wPosY;
      SelectOnly only(apSel[i]);
      recv_net_cmd_Liftoff(&cmd);
    }
    return;
  }
  CUnit *pUnit = select_get_first();
  if (pUnit == 0 || select_get_next() != 0)
    return;
  if (pUnit->GetBuildUnit(0) != 228)
    return;
  if (pUnit->IsConstructingAddon())
    return;
  if (pUnit->IsBuildingProtoss())
    return;
  if (pUnit->bTechType != 44)
    return;
  if (pUnit->bUpgradeType != 61)
    return;
  if (!(pUnit->dwFlags & 2))
    return;
  if (!(gulUnitTypeFlags[pUnit->wType] & 0x20))
    return;
  if (OrderAllowed(0x48, g_ActiveNationID, pUnit) != 1) {
    AllowedErr(get_allowed_err());
    return;
  }
  if (PosS(pCmd->wX) >= gwMapPixWidth)
    return;
  if (PosS(pCmd->wY) >= gwMapPixHeight)
    return;
  pUnit->SetOrder_xy(0x48, PosS(pCmd->wX), PosS(pCmd->wY));
  pUnit->SetOrder_xy_interrupt(gubUnitOrderGuard[pUnit->wType],
                               PosS(pCmd->wX), PosS(pCmd->wY) - 42, 0);
}

// gbMsgFilterMode @0x529040, a BYTE: the message-filter mode, 8 or 9 (8 when
// allies are not allowed). The name is a placeholder; 1.17's is not known.
#ifdef OC_DLL_MODE
  #ifndef gbMsgFilterMode
  #define gbMsgFilterMode (*reinterpret_cast<BYTE *>(OC_VA(0x529040u)))
  #endif
#else
extern BYTE gbMsgFilterMode;
#endif

// 0x4729E0 (21 B). Loads the saved game the packet names, outside multiplayer.
void __fastcall recv_net_cmd_LoadGame(const net_cmd_loadgame_t *pCmd) {
  if (!gbMultiPlayerMode)
    load_game(pCmd->dwArg, pCmd->szName);
}

// Two unsigned bounds tests against the map's pixel extent and an alliance test,
// then the ping. The colour, 0x11 or 0x12 for another player's ping, is byte
// arithmetic, since do_minimap_ping's third parameter is a BYTE; the coordinates
// go to it signed.
void __fastcall recv_net_cmd_MinimapPing(const net_cmd_point_t *pCmd) {
  WORD wX = pCmd->wX;
  if (wX >= gwMapPixWidth)
    return;
  WORD wY = pCmd->wY;
  if (wY >= gwMapPixHeight)
    return;
  int nActive = g_ActiveNationID;
  int nLocal = g_LocalNationID;
  if (sgbAlliances_g[nActive][nLocal] == 0)
    return;
  do_minimap_ping(PosS(wX), PosS(wY), (BYTE)(0x11 + (nActive != nLocal)));
}

// 0x472A50 (191 B). Morphs every selected unit allowed to: a Hydralisk into a
// Lurker under tech 0x20, written `wType - 6` as in 1.17, a Larva or Mutalisk
// under TTAllowed on the requested type; any other type takes the "not allowed"
// path. The second type test is a switch, which keeps MSVC from threading the
// branch past it, as 1.17's compile does not. The packet field is read at each
// use, as in 1.17.
void __fastcall recv_net_cmd_Morph(const net_cmd_word_t *pCmd) {
  for (CUnit *pUnit = select_get_first(); pUnit != 0;
       pUnit = select_get_next()) {
    WORD wType = pUnit->wType;
    int nAllowed;
    if (wType == 0x26 && pCmd->wArg == 0x67) {
      nAllowed = TechUseAllowed(wType - 6, g_ActiveNationID, pUnit);
    } else {
      switch (wType) {
        case 0x23:
        case 0x2B:
          nAllowed = TTAllowed(pCmd->wArg, g_ActiveNationID, pUnit);
          break;
        default:
          nAllowed = 0;
          break;
      }
    }
    if (nAllowed != 1) {
      AllowedErr(get_allowed_err());
      continue;
    }
    if (pUnit->bOrderId == 0x2A)
      continue;
    if (pUnit->AddBuildQueue(pCmd->wArg))
      pUnit->SetOrder(0x2A);
  }
  status_set_update();
}

// Gives every selected unit order 0x17 where it is allowed. MSVC inlines
// AllowedErr here, from the same call that stays a call in
// recv_net_cmd_SiegeMode.
void __fastcall recv_net_cmd_Nothing() {
  for (CUnit *pUnit = select_get_first(); pUnit != 0;
       pUnit = select_get_next()) {
    if (OrderAllowed(0x17, g_ActiveNationID, pUnit) == 1)
      pUnit->SetOrder(0x17);
    else
      AllowedErr(get_allowed_err());
  }
}

// Unpauses, restores the game palette and fades back in, then announces who did
// it, naming the player from gPlayerSlots_g in a replay and from gPlayerData
// live. The psnprintf call is written once per arm, which lets MSVC load its
// string id before the branch, as 1.17 does.
void __fastcall recv_net_cmd_PauseOff() {
  char szBuf[0x80];
  if (!IsGamePaused())
    return;
  sgbPauseChanged_g = 1;
  unpause_game(1);
  vid_restore_palette(0, 0x100, sgGamePalette_g);
  vid_fade(0);
  if (gPlayback)
    psnprintf(szBuf, 0x80, error_get_str(0x3A),
              gPlayerSlots_g[g_ActiveHumanID].szName);
  else
    psnprintf(szBuf, 0x80, error_get_str(0x3A),
              gPlayerData[g_ActiveHumanID].szName);
  map_msg_chat_msg(szBuf, 0);
}

// 0x472C70 (177 B). recv_net_cmd_PauseOff's mirror: pauses, desaturates, spends
// one of the player's pauses and announces it with the count left (string
// 0x39). The arms are written out twice for the same reason as PauseOff's.
void __fastcall recv_net_cmd_PauseOn() {
  char szBuf[0x80];
  if (IsGamePaused())
    return;
  if (!player_can_pause(g_ActiveHumanID))
    return;
  sgbPauseChanged_g = 1;
  pause_game();
  vid_desaturate();
  SubtractPauseCount(g_ActiveHumanID);
  if (gPlayback)
    psnprintf(szBuf, 0x80, error_get_str(0x39),
              gPlayerSlots_g[g_ActiveHumanID].szName,
              RemainingPauses(g_ActiveHumanID));
  else
    psnprintf(szBuf, 0x80, error_get_str(0x39),
              gPlayerData[g_ActiveHumanID].szName,
              RemainingPauses(g_ActiveHumanID));
  map_msg_chat_msg(szBuf, 0);
}

// The single-selection guard, a bounds test in tiles (gwMapWidth and
// gwMapHeight, not the pixel extent), then placebox_validate with the AllowedErr
// tail. The position is one point16_t, read by halves and passed by value.
// Hyperion: the command's fields as arguments, the order in an order_id_t, which the
// command of an order past 1.17's byte shares.
static void PlaceboxRecv(order_id_t nOrderID, WORD wUnitType, point16_t pos) {
  const net_cmd_placebox_o_t cmd = {0x8D, nOrderID, pos, wUnitType};
  const net_cmd_placebox_o_t *pCmd = &cmd;
  // Hyperion: of several, the one able to nearest the place
  CUnit *apSel[SELECTION_WIDE];
  if (const int nSel = SeveralSelected(apSel)) {
    const WORD *pTile = reinterpret_cast<const WORD *>(&pCmd->pos);
    const int nX = pTile[0] * 32 + 16, nY = pTile[1] * 32 + 16;
    CUnit *pBest = NULL;
    long long qBest = 0;
    for (int i = 0; i < nSel; i++) {
      if (!placebox_validate(pCmd->wOrderID, pCmd->wUnitType, apSel[i]))
        continue;
      const long long dx = PosU(apSel[i]->pSprite->wPosX) - nX, dy = PosU(apSel[i]->pSprite->wPosY) - nY;
      if (pBest == NULL || dx * dx + dy * dy < qBest) {
        pBest = apSel[i];
        qBest = dx * dx + dy * dy;
      }
    }
    SelectOnly only(pBest != NULL ? pBest : apSel[0]);
    PlaceboxRecv(nOrderID, wUnitType, pos);
    return;
  }
  CUnit *pUnit = select_get_first();
  if (pUnit == 0 || select_get_next() != 0)
    return;
  const WORD *pPos = reinterpret_cast<const WORD *>(&pCmd->pos);
  if (pPos[0] >= gwMapWidth)
    return;
  if (pPos[1] >= gwMapHeight)
    return;
  if (!placebox_validate(pCmd->wOrderID, pCmd->wUnitType, pUnit)) {
    AllowedErr(get_allowed_err());
    return;
  }
  placebox_query_place(pCmd->wOrderID, pCmd->wUnitType, pCmd->pos);
}

void __fastcall recv_net_cmd_Placebox(const net_cmd_placebox_t *pCmd) {
  PlaceboxRecv(pCmd->bOrderID, pCmd->wUnitType, pCmd->pos);
}

// Hyperion: Placebox with an order past 1.17's byte, 0x8D.
void recv_net_cmd_PlaceboxOrder(const net_cmd_placebox_o_t *pCmd) {
  if (OrderTypeValid(pCmd->wOrderID))
    PlaceboxRecv(pCmd->wOrderID, pCmd->wUnitType, pCmd->pos);
}

// 0x472DA0 (18 B). Runs only under playback: in a live game the network layer
// handles a leaver, so only a replay replays the recorded quit.
void __fastcall recv_net_cmd_Quit(const net_cmd_byte_t *pCmd) {
  if (gPlayback)
    sub_477E00(pCmd->bArg);
}

// The single-selection guard, ResearchAllowed and the AllowedErr tail call. The
// payload byte is read again after the predicate, as in 1.17. The test is
// `!= 1`: the predicate's success value is 1, and 1.17 compares against it.
// Hyperion: the tech as an argument, a tech_id_t, which the command of a tech past
// 1.17's byte shares.
static void ResearchRecv(tech_id_t wTech) {
  // Hyperion: of several, the first free one allowed to
  CUnit *apSel[SELECTION_WIDE];
  if (const int nSel = SeveralSelected(apSel)) {
    CUnit *pPick = apSel[0];
    for (int i = 0; i < nSel; i++) {
      if (ResearchFree(apSel[i]) && ResearchAllowed(wTech, g_ActiveNationID, apSel[i]) == 1) {
        pPick = apSel[i];
        break;
      }
    }
    SelectOnly only(pPick);
    ResearchRecv(wTech);
    return;
  }
  CUnit *pUnit = select_get_first();
  if (pUnit == 0 || select_get_next() != 0)
    return;
  if (ResearchAllowed(wTech, g_ActiveNationID, pUnit) != 1) {
    AllowedErr(get_allowed_err());
    return;
  }
  if (pUnit->StartResearch(wTech)) {
    pUnit->SetOrder(0x4B);
    status_set_update();
  }
}

void __fastcall recv_net_cmd_Research(const net_cmd_byte_t *pCmd) {
  ResearchRecv(pCmd->bArg);
}

// Hyperion: Research with a tech past 1.17's byte, 0x97.
void recv_net_cmd_ResearchWord(const net_cmd_word_t *pCmd) {
  if (TechTypeValid(pCmd->wArg))
    ResearchRecv(pCmd->wArg);
}

// 0x472E20 (20 B). Takes no packet: 1.17 never reads ECX.
void __fastcall recv_net_cmd_RestartGame() {
  if (!gbMultiPlayerMode)
    gameloop_init(5);
}

// 0x472E40 (158 B). recv_net_cmd_Stop's tail with the order chosen by the cargo:
// bCarryResource bit 2 (minerals) gives order 0x5A, bit 1 (gas) 0x54, and a unit
// carrying nothing is skipped. The byte is read once into a local and tested
// twice; the order id is an int.
void __fastcall recv_net_cmd_Return(const net_cmd_byte_t *pCmd) {
  for (CUnit *pUnit = select_get_first(); pUnit != 0;
       pUnit = select_get_next()) {
    BYTE bFlags = pUnit->bCarryResource;
    int nOrder;
    if (bFlags & 2)
      nOrder = 0x5A;
    else if (bFlags & 1)
      nOrder = 0x54;
    else
      continue;
    if (OrderAllowed(nOrder, g_ActiveNationID, pUnit) != 1) {
      AllowedErr(get_allowed_err());
      continue;
    }
    pUnit->sub_42A880(pCmd->bArg != 0, nOrder, 0);
  }
}

// The map-bounds test is an unsigned 16-bit compare, which holds only while both
// net_cmd_rclick_t's fields and gwMapPixWidth and gwMapPixHeight are WORDs. The
// arguments are evaluated right to left, so the queued flag is pushed before the
// target is looked up.
void __fastcall recv_net_cmd_RightClick(const net_cmd_rclick_t *pCmd) {
  if (pCmd->wX >= gwMapPixWidth)
    return;
  if (pCmd->wY >= gwMapPixHeight)
    return;
  sub_47F680(pCmd->wX, pCmd->wY, CUnit::FromIdAlive(pCmd->wTargetID),
             pCmd->wTargetType, pCmd->bQueued);
}

// Hyperion: RightClick with a wide unit id, 0x94.
void recv_net_cmd_RightClickWide(const net_cmd_rclick_w_t *pCmd) {
  if (pCmd->wX >= gwMapPixWidth)
    return;
  if (pCmd->wY >= gwMapPixHeight)
    return;
  sub_47F680(pCmd->wX, pCmd->wY, CUnit::FromIdAlive(pCmd->dwTargetID),
             pCmd->wTargetType, pCmd->bQueued);
}

// The three zero arguments are literals; 1.17 spells them `push eax` and
// `lea edx, [eax+1Ch]` because EAX is known to be 0 after IsLadderGame's test.
void __fastcall recv_net_cmd_SaveGame(const net_cmd_gamefile_t *pCmd) {
  if (gbMultiPlayerMode && sub_4771E0() != g_ActiveStormID)
    return;
  if (IsLadderGame())
    return;
  if (!get_valid_str_len(pCmd->szName, 0x1C, 0x1C, 0, 0))
    return;
  if (!is_bad_filename(pCmd->szName, SStrLen(pCmd->szName), 0))
    return;
  saveload_write_save_data(pCmd->dwTimestamp, pCmd->szName, 0);
}

// recv_net_cmd_Select
// Replaces the active player's selection: clear it, then add each unit id in the
// packet that is alive, not yet in the row and not a type-14 unit. The ids are
// walked by pointer, the row search returns a pointer, and both counters are
// unsigned, as in 1.17.
// Hyperion: the body over unit ids of either width.
template <class Id>
static void RecvSelect(BYTE bCount, const Id *pw) {
  if (bCount > SELECTION_MAX)
    return;

  sub_49C7D0(g_ActiveHumanID);

  int nAdded = 0;
  for (DWORD i = 0; i < bCount; i++) {
    CUnit *pUnit = CUnit::FromIdAlive(*pw);
    if (pUnit != 0) {
      CUnit **ppRow = SelectedRow(g_ActiveHumanID);  // Hyperion: selectlimits.h's (stage S)
      CUnit **ppFound = 0;
      for (DWORD j = 0; j < (DWORD)SelectionMax(); j++) {
        if (ppRow[j] == 0)
          break;
        if (ppRow[j] == pUnit) {
          ppFound = &ppRow[j];
          break;
        }
      }
      if (ppFound == 0 && pUnit->wType != 14) {
        if (select_insert(pUnit, g_ActiveHumanID, nAdded))
          nAdded++;
      }
    }
    pw++;
  }

  if (nAdded > 1)
    sub_464360();
}

void __fastcall recv_net_cmd_Select(const net_cmd_select_t *pCmd) {
  RecvSelect(pCmd->bCount, (const WORD *)(pCmd + 1));
}

// Hyperion: Select with wide unit ids, 0x89.
void recv_net_cmd_SelectWide(const net_cmd_selectdelta_w_t *pCmd) {
  RecvSelect(pCmd->bCount, pCmd->dwUnits);
}

// recv_net_cmd_SelectDeltaAdd
// recv_net_cmd_Select's add loop behind a prefix: count what is already
// selected, refuse the packet if the union would pass 12, and start the running
// total at that count; one variable serves as both. No wType == 14 rejection
// here, unlike recv_net_cmd_Select.
// Hyperion: the body over unit ids of either width.
template <class Id>
static void RecvSelectDeltaAdd(BYTE bCount, const Id *pw) {
  if (bCount > SELECTION_MAX)
    return;

  // Hyperion: the row and its size selectlimits.h's (stage S)
  CUnit **ppSel = SelectedRow(g_ActiveHumanID);
  int nCount;
  for (nCount = 0; nCount < SelectionMax(); nCount++) {
    if (ppSel[nCount] == 0)
      break;
  }

  if (bCount + nCount > SelectionMax())
    return;

  for (DWORD i = 0; i < bCount; i++) {
    CUnit *pUnit = CUnit::FromIdAlive(*pw);
    if (pUnit != 0) {
      CUnit **ppFound = 0;
      for (DWORD j = 0; j < (DWORD)SelectionMax(); j++) {
        if (ppSel[j] == 0)
          break;
        if (ppSel[j] == pUnit) {
          ppFound = &ppSel[j];
          break;
        }
      }
      if (ppFound == 0) {
        if (select_insert(pUnit, g_ActiveHumanID, nCount))
          nCount++;
      }
    }
    pw++;
  }

  if (nCount > 1)
    sub_464360();
}

void __fastcall recv_net_cmd_SelectDeltaAdd(const net_cmd_selectdelta_t *pCmd) {
  RecvSelectDeltaAdd(pCmd->bCount, pCmd->wUnits);
}

// Hyperion: SelectDeltaAdd with wide unit ids, 0x8A.
void recv_net_cmd_SelectDeltaAddWide(const net_cmd_selectdelta_w_t *pCmd) {
  RecvSelectDeltaAdd(pCmd->bCount, pCmd->dwUnits);
}

// sgbHasNetError_g @0x6728B8 (net_error.cpp), a BYTE: set once a network error
// has been reported, so the four transport bodies below do not stack dialogs.
#ifdef OC_DLL_MODE
  #ifndef sgbHasNetError_g
  #define sgbHasNetError_g (*reinterpret_cast<BYTE *>(OC_VA(0x6728B8u)))
  #endif
#else
extern BYTE sgbHasNetError_g;
#endif

// Storm imports of the transport block. They must be extern "C" __stdcall:
// otherwise the names mangle and the payload build fails to link.
extern "C" {
BOOL  __stdcall SNetSendTurn(char *pData, unsigned int nSize);
BOOL  __stdcall SNetReceiveMessage(int *pnPlayerId, char **ppData,
                                   unsigned int *pnSize);
BOOL  __stdcall SNetReceiveTurns(int nFirstPlayer, int nPlayers, char **ppData,
                                 unsigned int *pnSize, DWORD *pdwPlayerBits);
DWORD __stdcall SErrGetLastError();
BOOL  __stdcall SErrGetErrorStr(DWORD dwErrCode, char *pszBuffer,
                                unsigned int nBufferSize);
}  // extern "C"

// recv_net_cmd_SelectDeltaDel removes each live unit in the packet from the
// active player's selection. 1.17's exit on a zero count skips the final test,
// since the result is known to be 0 there; it is not an uninitialised read.
// Hyperion: the body over unit ids of either width.
template <class Id>
static void RecvSelectDeltaDel(BYTE bCount, const Id *pIds) {
  if (bCount > SELECTION_MAX)
    return;
  int nResult = 0;
  for (DWORD i = 0; i < bCount; i++) {
    CUnit *pUnit = CUnit::FromIdAlive(pIds[i]);
    if (pUnit != 0)
      nResult = sub_49C870(pUnit, g_ActiveHumanID);
  }
  if (nResult > 1)
    sub_464360();
}

void __fastcall recv_net_cmd_SelectDeltaDel(const net_cmd_selectdelta_t *pCmd) {
  RecvSelectDeltaDel(pCmd->bCount, pCmd->wUnits);
}

// Hyperion: SelectDeltaDel with wide unit ids, 0x8B.
void recv_net_cmd_SelectDeltaDelWide(const net_cmd_selectdelta_w_t *pCmd) {
  RecvSelectDeltaDel(pCmd->bCount, pCmd->dwUnits);
}

// Hyperion: a chunk of a selection command of Hyperion's selection, 0x8C (stage S): the
// three bodies above over up to 60 units at a time. A Select's first chunk clears the row and
// the later ones add to it; an add refuses a chunk that would pass 255. Only the operation's last
// chunk keeps the selection as a recent group (sub_464360), when it holds more than one unit, so
// one selection is one recent group however many chunks it takes.
void recv_net_cmd_SelectChunk(const net_cmd_selectchunk_t *pCmd) {
  if (!SelectionWide() || pCmd->bCount > SELECT_CHUNK_UNITS)
    return;
  const int nOp = pCmd->bOp & 0x0F;
  const bool bFirst = (pCmd->bOp & SELECT_CHUNK_NEXT) == 0;
  CUnit **ppRow = SelectedRow(g_ActiveHumanID);
  if (nOp == 0x09 && bFirst)
    sub_49C7D0(g_ActiveHumanID);
  int nCount = 0;
  while (nCount < SelectionMax() && ppRow[nCount] != 0)
    nCount++;
  if (nOp == 0x09 || nOp == 0x0A) {
    if (nOp == 0x0A && pCmd->bCount + nCount > SelectionMax())
      return;
    for (int i = 0; i < pCmd->bCount; i++) {
      CUnit *pUnit = CUnit::FromIdAlive(pCmd->dwUnits[i]);
      if (pUnit == 0 || (nOp == 0x09 && pUnit->wType == 14))
        continue;
      bool bIn = false;
      for (int j = 0; j < nCount && !bIn; j++)
        bIn = ppRow[j] == pUnit;
      if (!bIn && select_insert(pUnit, g_ActiveHumanID, nCount))
        nCount++;
    }
  } else if (nOp == 0x0B) {
    for (int i = 0; i < pCmd->bCount; i++) {
      CUnit *pUnit = CUnit::FromIdAlive(pCmd->dwUnits[i]);
      if (pUnit != 0)
        nCount = sub_49C870(pUnit, g_ActiveHumanID);
    }
  } else {
    return;
  }
  if ((pCmd->bOp & SELECT_CHUNK_MORE) == 0 && nCount > 1)
    sub_464360();
}

// recv_net_cmd_SetAllies
// When alliances are allowed, unpacks the 24-bit alliance word two bits a slot
// into the active player's row of sgbAlliances_g, refreshes the selection
// colours and clears every attack target that is now allied. nPlayer holds the
// active player across CUnit::ClearTargetIfAllied; the final test reads the
// global again.
void __fastcall recv_net_cmd_SetAllies(const net_cmd_dword_t *pCmd) {
  if (!AlliesAllowed())
    return;

  DWORD dwFlags = pCmd->dwArg;
  BYTE *pb = sgbAlliances_g.a[g_ActiveNationID];
  for (int i = 12; i != 0; i--) {
    *pb++ = (BYTE)(dwFlags & 3);
    dwFlags >>= 2;
  }

  update_unit_sel_colors();

  int nPlayer = g_ActiveNationID;
  for (CUnit *pUnit = CUnit::GetPlayerUnitList(nPlayer); pUnit != 0;
       pUnit = pUnit->pPlayerUnitNext) {
    CUnit *pTarget = pUnit->pOrderTarget;
    if (pTarget == 0)
      continue;
    if (gubOrderAttack[pUnit->bOrderId] == 0)
      continue;
    if (sgbAlliances_g[nPlayer][pTarget->bPlayer] == 0)
      continue;
    pUnit->ClearTargetIfAllied();
  }

  if (g_ActiveNationID == g_LocalNationID && gbMsgFilterMode == 9)
    update_message_filter();
}

// recv_net_cmd_SetFog sets the active player's shared vision: it returns if
// CanChangeVision refuses any player the packet names, then stores the packet's
// low byte, keeps the rest and forces the player's own bit on. The loop is
// `for (i = 8; i != 0;) { --i; ... }` with the mask first, `(1 << i) & wArg`;
// other spellings compile to other code.
void __fastcall recv_net_cmd_SetFog(const net_cmd_word_t *pCmd) {
  for (int i = 8; i != 0;) {
    --i;
    if ((1 << i) & pCmd->wArg) {
      if (!CanChangeVision(g_ActiveNationID, i))
        return;
    }
  }
  // Hyperion: the bits past 15 kept too, the slots past 15 (stage D)
  SharedVision[g_ActiveNationID] = (pCmd->wArg & 0xFF) |
                                   (SharedVision[g_ActiveNationID] & (g_hxPlayers ? ~0xFFul : 0xFF00ul)) |
                                   (1 << g_ActiveNationID);
}

// 0x4732F0 (26 B). Applies a replay speed during playback only. The call is not
// a tail jump, as replay_set_speed pops a stack argument.
void __fastcall recv_net_cmd_SetReplaySpeed(const net_cmd_replayspeed_t *pCmd) {
  if (gPlayback)
    replay_set_speed(pCmd->dwSpeed, pCmd->dwMult, pCmd->bPaused);
}

// 0x473310 (25 B). Sets the game speed outside multiplayer; the speed is a BYTE
// below 7, the seven game speeds.
void __fastcall recv_net_cmd_SetSpeed(const net_cmd_byte_t *pCmd) {
  BYTE nSpeed = pCmd->bArg;
  if (nSpeed >= 7)
    return;
  if (gbMultiPlayerMode)
    return;
  gOptions = nSpeed;
}

// Sieges every selected tank (types 5 and 0x17) not already sieging, where tech
// 5 allows it. AllowedErr stays a call here, inside the loop, where the
// single-selection receivers tail-jump to it. One wType read feeds both
// compares.
void __fastcall recv_net_cmd_SiegeMode(const net_cmd_byte_t *pCmd) {
  for (CUnit *pUnit = select_get_first(); pUnit != 0;
       pUnit = select_get_next()) {
    WORD wType = pUnit->wType;
    if (wType != 5 && wType != 0x17)
      continue;
    if (pUnit->bOrderId == 0x62)
      continue;
    if (TechUseAllowed(5, g_ActiveNationID, pUnit) != 1) {
      AllowedErr(get_allowed_err());
      continue;
    }
    pUnit->sub_42A880(pCmd->bArg != 0, 0x62, 0);
  }
}

// 0x4733A0 (110 B). Stims every selected unit allowed tech 0, Stim Packs, with
// AllowedErr inlined.
void __fastcall recv_net_cmd_Stim() {
  for (CUnit *pUnit = select_get_first(); pUnit != 0;
       pUnit = select_get_next()) {
    if (TechUseAllowed(0, g_ActiveNationID, pUnit) != 1) {
      AllowedErr(get_allowed_err());
      continue;
    }
    pUnit->UnitApplyStim();
  }
}

// Gives every selected unit the stop order (1) where it is allowed, queued when
// the packet's flag is set: recv_net_cmd_SiegeMode's tail with AllowedErr
// inlined, as in recv_net_cmd_Nothing.
void __fastcall recv_net_cmd_Stop(const net_cmd_byte_t *pCmd) {
  for (CUnit *pUnit = select_get_first(); pUnit != 0;
       pUnit = select_get_next()) {
    if (OrderAllowed(1, g_ActiveNationID, pUnit) != 1) {
      AllowedErr(get_allowed_err());
      continue;
    }
    pUnit->sub_42A880(pCmd->bArg != 0, 1, 0);
  }
}

// Cancels the add-on of the one selected building. The guard order is 1.17's:
// IsConstructingAddon is called before the dwFlags test.
void __fastcall recv_net_cmd_StopAddon(const net_cmd_t *pCmd) {
  // Hyperion: of several, the first building an addon
  CUnit *apSel[SELECTION_WIDE];
  if (const int nSel = SeveralSelected(apSel)) {
    CUnit *pPick = apSel[0];
    for (int i = 0; i < nSel; i++) {
      if (apSel[i]->IsConstructingAddon()) {
        pPick = apSel[i];
        break;
      }
    }
    SelectOnly only(pPick);
    recv_net_cmd_StopAddon(pCmd);
    return;
  }
  CUnit *pUnit = select_get_first();
  if (pUnit == 0 || select_get_next() != 0)
    return;
  if (!pUnit->IsConstructingAddon())
    return;
  if (!(pUnit->dwFlags & 1))
    return;
  if (pUnit->bPlayer != g_ActiveNationID)
    return;
  pUnit->CancelAddon();
  status_set_update();
}

// Cancels one entry of the one selected unit's build queue, unless it is one of
// seven morphing types. The slot is a switch: 0xFE calls sub_41B6F0, 0xFF does
// nothing, any other value removes that queue entry. The seven compares come out
// in two encodings, 32-bit for constants below 0x80 and 16-bit above, from one
// WORD local.
void __fastcall recv_net_cmd_StopBuild(const net_cmd_word_t *pCmd) {
  // Hyperion: of several, the one with the longest queue, the last of those
  CUnit *apSel[SELECTION_WIDE];
  if (const int nSel = SeveralSelected(apSel)) {
    CUnit *pPick = apSel[0];
    int nMost = 0;
    for (int i = 0; i < nSel; i++) {
      const int nQueued = QueuedCount(apSel[i]);
      if (nQueued != 0 && nQueued >= nMost) {
        pPick = apSel[i];
        nMost = nQueued;
      }
    }
    SelectOnly only(pPick);
    recv_net_cmd_StopBuild(pCmd);
    return;
  }
  CUnit *pUnit = select_get_first();
  if (pUnit == 0 || select_get_next() != 0)
    return;
  WORD wType = pUnit->wType;
  if (wType == 0x23 || wType == 0x2B || wType == 0x26 || wType == 0x83 ||
      wType == 0x84 || wType == 0x8D || wType == 0x8F)
    return;
  if (!(pUnit->dwFlags & 1))
    return;
  if (pUnit->bPlayer != g_ActiveNationID)
    return;
  int nSlot = pCmd->wArg;
  switch (nSlot) {
    case 0xFE:
      pUnit->sub_41B6F0();
      break;
    case 0xFF:
      break;
    default:
      pUnit->RemoveBuildQueue(nSlot);
      break;
  }
  status_set_update();
}

// recv_net_cmd_StopUpgrade plus a units.dat flag test on the unit's type, the
// mask written against the DWORD element.
void __fastcall recv_net_cmd_StopResearch(const net_cmd_t *pCmd) {
  // Hyperion: of several, the first researching
  CUnit *apSel[SELECTION_WIDE];
  if (const int nSel = SeveralSelected(apSel)) {
    CUnit *pPick = apSel[0];
    for (int i = 0; i < nSel; i++) {
      if (apSel[i]->bTechType != 0x2C) {
        pPick = apSel[i];
        break;
      }
    }
    SelectOnly only(pPick);
    recv_net_cmd_StopResearch(pCmd);
    return;
  }
  CUnit *pUnit = select_get_first();
  if (pUnit == 0 || select_get_next() != 0)
    return;
  if (!(gulUnitTypeFlags[pUnit->wType] & 1))
    return;
  if (!(pUnit->dwFlags & 1))
    return;
  if (pUnit->bPlayer != g_ActiveNationID)
    return;
  pUnit->CancelResearch();
  status_set_update();
}

// Cancels the upgrade of the one selected unit when it is the active player's
// and completed (dwFlags bit 0). status_set_update is inside the guard, which
// gives 1.17's two epilogues.
void __fastcall recv_net_cmd_StopUpgrade(const net_cmd_t *pCmd) {
  // Hyperion: of several, the first upgrading
  CUnit *apSel[SELECTION_WIDE];
  if (const int nSel = SeveralSelected(apSel)) {
    CUnit *pPick = apSel[0];
    for (int i = 0; i < nSel; i++) {
      if (apSel[i]->bUpgradeType != 0x3D) {
        pPick = apSel[i];
        break;
      }
    }
    SelectOnly only(pPick);
    recv_net_cmd_StopUpgrade(pCmd);
    return;
  }
  CUnit *pUnit = select_get_first();
  if (pUnit == 0 || select_get_next() != 0)
    return;
  if (!(pUnit->dwFlags & 1))
    return;
  if (pUnit->bPlayer != g_ActiveNationID)
    return;
  pUnit->CancelUpgrade();
  status_set_update();
}

// 0x473670 (115 B). recv_net_cmd_Nothing with order 0x34. The test is `== 1`,
// the action in the then arm and AllowedErr in the else, as 1.17 has it; the
// SiegeMode side of the family writes `!= 1` and continues.
void __fastcall recv_net_cmd_Stop_Carrier() {
  for (CUnit *pUnit = select_get_first(); pUnit != 0;
       pUnit = select_get_next()) {
    if (OrderAllowed(0x34, g_ActiveNationID, pUnit) == 1)
      pUnit->SetOrder(0x34);
    else
      AllowedErr(get_allowed_err());
  }
}

// 0x473710 (115 B). recv_net_cmd_Stop_Carrier with order 7.
void __fastcall recv_net_cmd_Stop_Reaver() {
  for (CUnit *pUnit = select_get_first(); pUnit != 0;
       pUnit = select_get_next()) {
    if (OrderAllowed(7, g_ActiveNationID, pUnit) == 1)
      pUnit->SetOrder(7);
    else
      AllowedErr(get_allowed_err());
  }
}

// 0x4737B0 (185 B). Pairs the selected High Templar (0x43) into archon merges.
// Tech 0x17 is tested once, on the first selected unit. The array is zeroed
// though the collect loop fills it densely, and the merge loop's null test is
// 1.17's. The merge loop is a plain `for` over i, whose `nCount - 1 - i` is
// also FindClosestMergeTarget's count.
void __fastcall recv_net_cmd_SummonArchon() {
  CUnit *apUnits[SELECTION_WIDE];  // Hyperion: 12 in 1.17, 255 in Hyperion's selection (stage S)
  CUnit *pUnit = select_get_first();
  if (pUnit == 0)
    return;
  if (TechUseAllowed(0x17, g_ActiveNationID, pUnit) != 1) {
    AllowedErr(get_allowed_err());
    return;
  }
  memset(apUnits, 0, sizeof(apUnits));
  int nCount = 0;
  do {
    if (pUnit->wType == 0x43)
      apUnits[nCount++] = pUnit;
    pUnit = select_get_next();
  } while (pUnit != 0);
  for (int i = 0; i < nCount - 1; i++) {
    CUnit *pMerger = apUnits[i];
    if (pMerger == 0)
      continue;
    CUnit *pTarget = FindClosestMergeTarget(pMerger, &apUnits[i + 1],
                                            nCount - 1 - i, 0x43);
    if (pTarget == 0)
      continue;
    pMerger->SetOrder_CUnit(0x69, pTarget);
    pTarget->SetOrder_CUnit(0x69, pMerger);
  }
  status_set_update();
}

// 0x473870 (191 B). recv_net_cmd_SummonArchon with tech 0x1C, unit type 0x3D
// (Dark Templar) and order 0xB7.
void __fastcall recv_net_cmd_SummonDArchon() {
  CUnit *apUnits[SELECTION_WIDE];  // Hyperion: 12 in 1.17, 255 in Hyperion's selection (stage S)
  CUnit *pUnit = select_get_first();
  if (pUnit == 0)
    return;
  if (TechUseAllowed(0x1C, g_ActiveNationID, pUnit) != 1) {
    AllowedErr(get_allowed_err());
    return;
  }
  memset(apUnits, 0, sizeof(apUnits));
  int nCount = 0;
  do {
    if (pUnit->wType == 0x3D)
      apUnits[nCount++] = pUnit;
    pUnit = select_get_next();
  } while (pUnit != 0);
  for (int i = 0; i < nCount - 1; i++) {
    CUnit *pMerger = apUnits[i];
    if (pMerger == 0)
      continue;
    CUnit *pTarget = FindClosestMergeTarget(pMerger, &apUnits[i + 1],
                                            nCount - 1 - i, 0x3D);
    if (pTarget == 0)
      continue;
    pMerger->SetOrder_CUnit(0xB7, pTarget);
    pTarget->SetOrder_CUnit(0xB7, pMerger);
  }
  status_set_update();
}

// recv_net_cmd_SiegeMode with unit types 0x1E and 0x19 and order 0x63. The tech
// is 5 in both, as in 1.17.
void __fastcall recv_net_cmd_SwitchTankMode(const net_cmd_byte_t *pCmd) {
  for (CUnit *pUnit = select_get_first(); pUnit != 0;
       pUnit = select_get_next()) {
    WORD wType = pUnit->wType;
    if (wType != 0x1E && wType != 0x19)
      continue;
    if (pUnit->bOrderId == 0x63)
      continue;
    if (TechUseAllowed(5, g_ActiveNationID, pUnit) != 1) {
      AllowedErr(get_allowed_err());
      continue;
    }
    pUnit->sub_42A880(pCmd->bArg != 0, 0x63, 0);
  }
}

// 0x4739A0 (138 B). Unloads every selected transport that is allowed to and has
// cargo, queued when the packet's flag is set. OrderAllowed runs before
// CUnit::IsUnitInside, as in 1.17, so an empty transport still takes the
// permission check.
void __fastcall recv_net_cmd_UnloadAll(const net_cmd_byte_t *pCmd) {
  for (CUnit *pUnit = select_get_first(); pUnit != 0;
       pUnit = select_get_next()) {
    if (OrderAllowed(0x6F, g_ActiveNationID, pUnit) != 1) {
      AllowedErr(get_allowed_err());
      continue;
    }
    if (!pUnit->IsUnitInside())
      continue;
    pUnit->sub_42A880(pCmd->bArg != 0, 0x6F, 0);
  }
}

// recv_net_cmd_Research with UpgradeAllowed, CUnit::StartUpgrade and order
// 0x4C. The test is `!= 1`, as in Research. Hyperion: the upgrade as an argument, as
// ResearchRecv's tech.
static void UpgradeRecv(upgrade_id_t wUpgrade) {
  // Hyperion: of several, the first free one allowed to
  CUnit *apSel[SELECTION_WIDE];
  if (const int nSel = SeveralSelected(apSel)) {
    CUnit *pPick = apSel[0];
    for (int i = 0; i < nSel; i++) {
      if (ResearchFree(apSel[i]) && UpgradeAllowed(wUpgrade, g_ActiveNationID, apSel[i]) == 1) {
        pPick = apSel[i];
        break;
      }
    }
    SelectOnly only(pPick);
    UpgradeRecv(wUpgrade);
    return;
  }
  CUnit *pUnit = select_get_first();
  if (pUnit == 0 || select_get_next() != 0)
    return;
  if (UpgradeAllowed(wUpgrade, g_ActiveNationID, pUnit) != 1) {
    AllowedErr(get_allowed_err());
    return;
  }
  if (pUnit->StartUpgrade(wUpgrade)) {
    pUnit->SetOrder(0x4C);
    status_set_update();
  }
}

void __fastcall recv_net_cmd_Upgrade(const net_cmd_byte_t *pCmd) {
  UpgradeRecv(pCmd->bArg);
}

// Hyperion: Upgrade with an upgrade past 1.17's byte, 0x98.
void recv_net_cmd_UpgradeWord(const net_cmd_word_t *pCmd) {
  if (UpgradeTypeValid(pCmd->wArg))
    UpgradeRecv(pCmd->wArg);
}

// Morphs the one selected building of a type in 0x82..0x98 into the packet's
// type in the same range. The unit's type is a WORD local and the packet's is
// read from the packet at each use, which keeps the two range tests compiled
// differently, as in 1.17. Every range failure shares one voice_squelch call. A
// failed AddBuildQueue and an order already 0x2B return without
// status_set_update, unlike in recv_net_cmd_Build.
void __fastcall recv_net_cmd_UpgradeBldg(const net_cmd_word_t *pCmd) {
  // Hyperion: of several, the first free one allowed to
  CUnit *apSel[SELECTION_WIDE];
  if (const int nSel = SeveralSelected(apSel)) {
    CUnit *pPick = apSel[0];
    for (int i = 0; i < nSel; i++) {
      if (QueuedCount(apSel[i]) == 0 && apSel[i]->bOrderId != 0x2B &&
          TTAllowed(pCmd->wArg, g_ActiveNationID, apSel[i]) == 1) {
        pPick = apSel[i];
        break;
      }
    }
    SelectOnly only(pPick);
    recv_net_cmd_UpgradeBldg(pCmd);
    return;
  }
  CUnit *pUnit = select_get_first();
  if (pUnit == 0 || select_get_next() != 0)
    return;
  WORD wType = pUnit->wType;
  if (wType >= 0x82 && wType <= 0x98) {
    if (pCmd->wArg >= 0x82 && pCmd->wArg <= 0x98) {
      if (TTAllowed(pCmd->wArg, g_ActiveNationID, pUnit) != 1) {
        AllowedErr(get_allowed_err());
        return;
      }
      if (!pUnit->AddBuildQueue(pCmd->wArg))
        return;
      if (pUnit->bOrderId == 0x2B)
        return;
      pUnit->SetOrder(0x2B);
      status_set_update();
      return;
    }
  }
  voice_squelch(g_ActiveHumanID, 0);
}

// 0x473B60 (95 B). Toggles bit 1 of the active player's network flags, unless
// the active player is the local one. The opcode carries the sense (0x39
// clears). The flags local is a WORD, as net_player_set_flags reads 16 bits.
// The call is written once after the if/else; MSVC copies it into both arms, as
// 1.17 shows.
void __fastcall recv_net_cmd_VoiceToggle(const net_cmd_t *pCmd) {
  WORD wFlags;
  if (g_ActiveHumanID == g_LocalHumanID)
    return;
  if (!net_player_get_flags((BYTE)g_ActiveStormID, &wFlags))
    return;
  if (pCmd->bOpcode == 0x39)
    wFlags &= 0xFFFD;
  else
    wFlags |= 2;
  net_player_set_flags((BYTE)g_ActiveStormID, wFlags);
}

// 0x473BC0 (32 B). The opcode carries the sense (0x3A squelches), and
// voice_squelch's second argument is 32-bit: 1.17 zeroes all of EDX before
// `setz dl`.
void __fastcall recv_net_cmd_VoiceSquelch(const net_cmd_byte_t *pCmd) {
  if (pCmd->bArg == g_LocalStormID)
    voice_squelch(g_ActiveStormID, pCmd->bOpcode == 0x3A);
}

// Sends a chat line to every player the message filter admits, unless
// sub_46FD90 handled it as a slash command. The buffer is 0x52 bytes with a
// two-byte header, and only its first byte is set: the second goes out
// uninitialised, as in 1.17. strlen is inlined by /Oi.
void __fastcall net_send_string(const char *pszText) {
  char szMsg[0x52];
  if (sub_46FD90(pszText))
    return;
  int nLen = (int)strlen(pszText);
  if (gwMsgFilter_g == 0)
    return;
  if (nLen == 0)
    return;
  szMsg[0] = 0;
  SStrCopy(szMsg + 2, pszText, 0x50);
  nLen += 3;
  for (int i = 0; i < 8; i++) {
    if ((gwMsgFilter_g & (1 << i)) && IsValidPlayer((BYTE)i))
      net_send_message(i, szMsg, nLen);
  }
}

// oracle 0x473C70, 68 B -- opcode 0x15. Six parameters (`ret 10h`); the last
// two are separate BYTEs at +9 and +10, which sets this packet apart from
// RightClick's. bOrderID is a BYTE here and in net_cmds.h, and the two change
// together: apart, they are two overloads.
void __fastcall send_net_cmd_Action(WORD wX, WORD wY, CUnit *pTarget,
                                    int nTargetType, order_id_t bOrderID,
                                    int nQueued) {
  if (bOrderID > 0xFF) {  // Hyperion: 0x96, an order past 1.17's byte
    net_cmd_action_o_t cmdO;
    cmdO.bOpcode = 0x96;
    cmdO.wX = wX;
    cmdO.wY = wY;
    cmdO.dwTargetID = pTarget->GetUnitID();
    cmdO.wTargetType = (WORD)nTargetType;
    cmdO.wOrderID = bOrderID;
    cmdO.bQueued = (BYTE)nQueued;
    netmgr_queue_cmd(&cmdO, sizeof(cmdO));
    return;
  }
  if (PoolsWideIds()) {  // Hyperion: 0x95, a wide unit id
    net_cmd_action_w_t cmdW;
    cmdW.bOpcode = 0x15 | NET_CMD_WIDE_IDS;
    cmdW.wX = wX;
    cmdW.wY = wY;
    cmdW.dwTargetID = pTarget->GetUnitID();
    cmdW.wTargetType = (WORD)nTargetType;
    cmdW.bOrderID = static_cast<BYTE>(bOrderID);  // under 256 here
    cmdW.bQueued = (BYTE)nQueued;
    netmgr_queue_cmd(&cmdW, sizeof(cmdW));
    return;
  }
  net_cmd_action_t cmd;
  cmd.wX = wX;
  cmd.bOpcode = 0x15;
  cmd.wY = wY;
  cmd.bOrderID = static_cast<BYTE>(bOrderID);
  cmd.wTargetID = pTarget->GetUnitID();
  cmd.wTargetType = (WORD)nTargetType;
  cmd.bQueued = (BYTE)nQueued;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x473CC0, 29 B -- opcode 0x1F. The unit type to place, a WORD, as
// order.cpp's order_build forwards it.
void __fastcall send_net_cmd_Build(WORD wUnitType) {
  net_cmd_word_t cmd;
  cmd.wArg = wUnitType;
  cmd.bOpcode = 0x1F;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x473CE0, 25 B -- opcode 0x27. 1.17's `push ecx` is frame space for
// the 1-byte packet, not an argument.
void __fastcall send_net_cmd_BuildFighter() {
  net_cmd_t cmd;
  cmd.bOpcode = 0x27;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x473D00, 28 B -- opcode 0x2C.
void __fastcall send_net_cmd_Burrow(BYTE bQueued) {
  net_cmd_byte_t cmd;
  cmd.bArg = bQueued;
  cmd.bOpcode = 0x2C;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x473D20, 25 B -- opcode 0x2E.
void __fastcall send_net_cmd_CancelNuke() {
  net_cmd_t cmd;
  cmd.bOpcode = 0x2E;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x473D40, 30 B -- opcode 0x12. The payload is a 4-byte cheat bitfield
// from textbox_mode_switch, which compares it as a full 32-bit value.
void __fastcall send_net_cmd_Cheat(DWORD dwFlags) {
  net_cmd_dword_t cmd;
  cmd.dwArg = dwFlags;
  cmd.bOpcode = 0x12;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x473D60, 28 B -- opcode 0x21.
void __fastcall send_net_cmd_Cloak(BYTE bQueued) {
  net_cmd_byte_t cmd;
  cmd.bArg = bQueued;
  cmd.bOpcode = 0x21;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x473D80, 28 B -- opcode 0x2D.
void __fastcall send_net_cmd_Deburrow(BYTE bQueued) {
  net_cmd_byte_t cmd;
  cmd.bArg = bQueued;
  cmd.bOpcode = 0x2D;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x473DA0, 28 B -- opcode 0x22.
void __fastcall send_net_cmd_Decloak(BYTE bQueued) {
  net_cmd_byte_t cmd;
  cmd.bArg = bQueued;
  cmd.bOpcode = 0x22;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x473DC0, 34 B -- opcode 0x29. The opcode is stored before the unit
// id's call, in 1.17's order.
void __fastcall send_net_cmd_ExitTransport(CUnit *pUnit) {
  if (PoolsWideIds()) {  // Hyperion: 0xA9, a wide unit id
    net_cmd_dword_t cmdW;
    cmdW.bOpcode = 0x29 | NET_CMD_WIDE_IDS;
    cmdW.dwArg = pUnit->GetUnitID();
    netmgr_queue_cmd(&cmdW, sizeof(cmdW));
    return;
  }
  net_cmd_word_t cmd;
  cmd.bOpcode = 0x29;
  cmd.wArg = pUnit->GetUnitID();
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x473E90, 28 B -- opcode 0x2B.
void __fastcall send_net_cmd_HoldPosition(BYTE bQueued) {
  net_cmd_byte_t cmd;
  cmd.bArg = bQueued;
  cmd.bOpcode = 0x2B;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x473EB0, 25 B -- opcode 0x18.
void __fastcall send_net_cmd_Kill() {
  net_cmd_t cmd;
  cmd.bOpcode = 0x18;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x473ED0, 25 B -- opcode 0x19.
void __fastcall send_net_cmd_KillEgg() {
  net_cmd_t cmd;
  cmd.bOpcode = 0x19;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x473EF0, 55 B -- opcode 0x2F. Nullary: the unit is gpStatUnit, the
// status pane's, and the command-button action table is its only caller. The
// global is read once into a local and the sprite pointer at each use.
void __fastcall send_net_cmd_Liftoff() {
  net_cmd_point_t cmd;
  CUnit *pUnit = gpStatUnit;
  cmd.bOpcode = 0x2F;
  cmd.wX = pUnit->pSprite->wPosX;
  cmd.wY = pUnit->pSprite->wPosY;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x473F30, 56 B -- opcode 0x07. The length is not sizeof(cmd) but
// 1 + 4 + strlen + 1: the packet stops at the name's terminator. strlen is
// inlined by /Oi.
void __fastcall send_net_cmd_LoadGame(DWORD dwTimestamp, const char *pszName) {
  net_cmd_gamefile_t cmd;
  cmd.bOpcode = 0x07;
  cmd.dwTimestamp = dwTimestamp;
  SStrCopy(cmd.szName, pszName, sizeof(cmd.szName));
  netmgr_queue_cmd(&cmd, (DWORD)(strlen(cmd.szName) + 6));
}

// oracle 0x473F70, 35 B -- opcode 0x58. A two-WORD payload, stored before the
// opcode.
void __fastcall send_net_cmd_MinimapPing(int nX, int nY) {
  net_cmd_point_t cmd;
  cmd.wX = PosU(nX);
  cmd.wY = PosU(nY);
  cmd.bOpcode = 0x58;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x473FA0, 29 B -- opcode 0x23.
void __fastcall send_net_cmd_Morph(WORD wUnitType) {
  net_cmd_word_t cmd;
  cmd.wArg = wUnitType;
  cmd.bOpcode = 0x23;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x473FC0, 34 B. Sent only while the game is paused.
void __fastcall send_net_cmd_PauseOff() {
  if (IsGamePaused()) {
    net_cmd_t cmd;
    cmd.bOpcode = 0x11;
    netmgr_queue_cmd(&cmd, sizeof(cmd));
  }
}

// oracle 0x473FF0, 32 B. Sent only while the game is not paused. 1.17's
// `lea edx, [eax+1h]` is sizeof(cmd), made from an EAX known to be 0.
void __fastcall send_net_cmd_PauseOn() {
  if (!IsGamePaused()) {
    net_cmd_t cmd;
    cmd.bOpcode = 0x10;
    netmgr_queue_cmd(&cmd, sizeof(cmd));
  }
}

// oracle 0x474010, 42 B -- opcode 0x0C. Three parameters (`ret 4`): the order id
// a BYTE in ECX, the position pushed whole.
void __fastcall send_net_cmd_Placebox(order_id_t bOrderID, WORD wUnitType,
                                      point16_t pos) {
  if (bOrderID > 0xFF) {  // Hyperion: 0x8D, an order past 1.17's byte
    net_cmd_placebox_o_t cmdO;
    cmdO.bOpcode = 0x8D;
    cmdO.wOrderID = bOrderID;
    cmdO.pos = pos;
    cmdO.wUnitType = wUnitType;
    netmgr_queue_cmd(&cmdO, sizeof(cmdO));
    return;
  }
  net_cmd_placebox_t cmd;
  cmd.bOrderID = static_cast<BYTE>(bOrderID);  // under 256 here
  cmd.wUnitType = wUnitType;
  cmd.bOpcode = 0x0C;
  cmd.pos = pos;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x474040, 28 B -- opcode 0x30. A BYTE tech id; order_research narrows
// its WORD at the call. Hyperion: a tech_id_t, and a tech past 1.17's byte goes in
// 0x97.
void __fastcall send_net_cmd_Research(tech_id_t bTechId) {
  if (bTechId > 0xFF) {
    net_cmd_word_t cmdW;
    cmdW.bOpcode = 0x97;
    cmdW.wArg = bTechId;
    netmgr_queue_cmd(&cmdW, sizeof(cmdW));
    return;
  }
  net_cmd_byte_t cmd;
  cmd.bArg = static_cast<BYTE>(bTechId);  // under 256 here
  cmd.bOpcode = 0x30;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x474060, 25 B -- opcode 0x08.
void __fastcall send_net_cmd_RestartGame() {
  net_cmd_t cmd;
  cmd.bOpcode = 0x08;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x474080, 28 B -- opcode 0x1E.
void __fastcall send_net_cmd_Return(BYTE bQueued) {
  net_cmd_byte_t cmd;
  cmd.bArg = bQueued;
  cmd.bOpcode = 0x1E;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x4740A0, 62 B -- opcode 0x14. Five parameters (`ret 0Ch`); the opcode
// is stored between wX and wY, as in Action.
void __fastcall send_net_cmd_RightClick(int nX, int nY, CUnit *pTarget,
                                        int nTargetType, int nQueued) {
  if (PoolsWideIds()) {  // Hyperion: 0x94, a wide unit id
    net_cmd_rclick_w_t cmdW;
    cmdW.bOpcode = 0x14 | NET_CMD_WIDE_IDS;
    cmdW.wX = PosU(nX);
    cmdW.wY = PosU(nY);
    cmdW.dwTargetID = pTarget->GetUnitID();
    cmdW.wTargetType = (WORD)nTargetType;
    cmdW.bQueued = (BYTE)nQueued;
    netmgr_queue_cmd(&cmdW, sizeof(cmdW));
    return;
  }
  net_cmd_rclick_t cmd;
  cmd.wX = PosU(nX);
  cmd.bOpcode = 0x14;
  cmd.wY = PosU(nY);
  cmd.wTargetID = pTarget->GetUnitID();
  cmd.wTargetType = (WORD)nTargetType;
  cmd.bQueued = (BYTE)nQueued;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x4740E0, 78 B -- opcode 0x06. Sent by the host only. The packet keeps
// the low dword of _time64's 64-bit result, as 1.17 does, and its length stops
// at the name's terminator. _time64 is written rather than time, its inline
// wrapper in VS2013.
void __fastcall send_net_cmd_SaveGame(const char *pszName) {
  if (IsGameHost()) {
    net_cmd_gamefile_t cmd;
    cmd.bOpcode = 0x06;
    cmd.dwTimestamp = (DWORD)_time64(0);
    SStrCopy(cmd.szName, pszName, sizeof(cmd.szName));
    netmgr_queue_cmd(&cmd, (DWORD)(strlen(cmd.szName) + 6));
  }
}

// oracle 0x474310, 30 B -- opcode 0x0E. The payload is the 24-bit alliance word,
// twelve slots of 2 bits that teamfilter_dlg_cmd packs, so it is a DWORD.
void __fastcall send_net_cmd_SetAllies(DWORD dwAllies) {
  net_cmd_dword_t cmd;
  cmd.dwArg = dwAllies;
  cmd.bOpcode = 0x0E;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x474330, 29 B -- opcode 0x0D. A WORD payload, the shared-vision mask
// teamfilter_dlg_cmd sends after SetAllies. The caller does not fix the
// parameter's width; WORD matches the 3-byte packet.
void __fastcall send_net_cmd_SetFog(WORD wVisionFlags) {
  net_cmd_word_t cmd;
  cmd.wArg = wVisionFlags;
  cmd.bOpcode = 0x0D;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x474350, 41 B -- opcode 0x56. Three parameters (`ret 4`); every caller
// passes 0 for the third, a flag stored in one byte.
void __fastcall send_net_cmd_SetReplaySpeed(DWORD dwSpeed, DWORD dwTurnRate,
                                            BYTE bPaused) {
  net_cmd_replay_t cmd;
  cmd.dwSpeed = dwSpeed;
  cmd.dwTurnRate = dwTurnRate;
  cmd.bOpcode = 0x56;
  cmd.bPaused = bPaused;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x474380, 28 B -- opcode 0x0F, the game speed; in 1.17 it belongs to
// game_evt.cpp. The packet field is a BYTE but the parameter is 32-bit: its
// caller speed_dlg_cmd sign-extends the scroll position into all of ECX, which
// MSVC does only for a 32-bit parameter. It is an int, as the value is one of
// the seven game speeds.
void __fastcall send_net_cmd_SetSpeed(int nSpeed) {
  net_cmd_byte_t cmd;
  cmd.bArg = (BYTE)nSpeed;
  cmd.bOpcode = 0x0F;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x4743A0, 28 B -- opcode 0x26.
void __fastcall send_net_cmd_SiegeMode(BYTE bQueued) {
  net_cmd_byte_t cmd;
  cmd.bArg = bQueued;
  cmd.bOpcode = 0x26;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x4743C0, 25 B -- opcode 0x36.
void __fastcall send_net_cmd_Stim() {
  net_cmd_t cmd;
  cmd.bOpcode = 0x36;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x4743E0, 28 B -- opcode 0x1A.
void __fastcall send_net_cmd_Stop(BYTE bQueued) {
  net_cmd_byte_t cmd;
  cmd.bArg = bQueued;
  cmd.bOpcode = 0x1A;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x474400, 25 B -- opcode 0x34.
void __fastcall send_net_cmd_StopAddon() {
  net_cmd_t cmd;
  cmd.bOpcode = 0x34;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x474420, 29 B -- opcode 0x20. The WORD is the production-queue slot
// order_cancel_train forwards.
void __fastcall send_net_cmd_StopBuild(WORD wSlot) {
  net_cmd_word_t cmd;
  cmd.wArg = wSlot;
  cmd.bOpcode = 0x20;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x474440, 25 B -- opcode 0x31.
void __fastcall send_net_cmd_StopResearch() {
  net_cmd_t cmd;
  cmd.bOpcode = 0x31;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x474460, 25 B -- opcode 0x33.
void __fastcall send_net_cmd_StopUpgrade() {
  net_cmd_t cmd;
  cmd.bOpcode = 0x33;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x474480, 25 B -- opcode 0x1B. No queue byte, unlike Stop (0x1A): the
// carrier and reaver stops are 1-byte packets.
void __fastcall send_net_cmd_Stop_Carrier() {
  net_cmd_t cmd;
  cmd.bOpcode = 0x1B;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x4744A0, 25 B -- opcode 0x1C.
void __fastcall send_net_cmd_Stop_Reaver() {
  net_cmd_t cmd;
  cmd.bOpcode = 0x1C;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x4744C0, 25 B -- opcode 0x2A.
void __fastcall send_net_cmd_SummonArchon() {
  net_cmd_t cmd;
  cmd.bOpcode = 0x2A;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x4744E0, 25 B -- opcode 0x5A, outside the block's 0x08..0x36 range.
void __fastcall send_net_cmd_SummonDArchon() {
  net_cmd_t cmd;
  cmd.bOpcode = 0x5A;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x474500, 28 B -- opcode 0x25.
void __fastcall send_net_cmd_SwitchTankMode(BYTE bQueued) {
  net_cmd_byte_t cmd;
  cmd.bArg = bQueued;
  cmd.bOpcode = 0x25;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x474520, 28 B -- opcode 0x28.
void __fastcall send_net_cmd_UnloadAll(BYTE bQueued) {
  net_cmd_byte_t cmd;
  cmd.bArg = bQueued;
  cmd.bOpcode = 0x28;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x474540, 28 B -- opcode 0x32. A BYTE upgrade id; order_upgrade
// narrows its WORD at the call. Hyperion: an upgrade_id_t, and an upgrade past
// 1.17's byte goes in 0x98.
void __fastcall send_net_cmd_Upgrade(upgrade_id_t bUpgradeId) {
  if (bUpgradeId > 0xFF) {
    net_cmd_word_t cmdW;
    cmdW.bOpcode = 0x98;
    cmdW.wArg = bUpgradeId;
    netmgr_queue_cmd(&cmdW, sizeof(cmdW));
    return;
  }
  net_cmd_byte_t cmd;
  cmd.bArg = static_cast<BYTE>(bUpgradeId);  // under 256 here
  cmd.bOpcode = 0x32;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// oracle 0x474560, 29 B -- opcode 0x35.
void __fastcall send_net_cmd_UpgradeBldg(WORD wUnitType) {
  net_cmd_word_t cmd;
  cmd.wArg = wUnitType;
  cmd.bOpcode = 0x35;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}

// send_net_cmd_GroupUnits
// Queues opcode 0x13 and, on the recall action (1), copies the selection into
// sgpNetSelected_g and checks for a double tap: the same group again within
// 500 ms, an unsigned tick difference, centres the view on it. The stores go
// opcode, bGroup, bAction, as in 1.17, and the count is an int narrowed here.
void __fastcall send_net_cmd_GroupUnits(BYTE bAction, BYTE bGroup,
                                        CUnit **ppUnits, int nCount) {
  net_cmd_group_t cmd;
  cmd.bOpcode = 0x13;
  cmd.bGroup = bGroup;
  cmd.bAction = bAction;
  netmgr_queue_cmd(&cmd, 3);

  if (bAction == 1) {
    memset(NetSelected(), 0, SelectionMax() * sizeof(CUnit *));  // Hyperion: 0x30 in 1.17
    memcpy(NetSelected(), ppUnits, (BYTE)nCount * sizeof(CUnit *));  // Hyperion: 4 in 1.17

    DWORD dwNow = GetTickCount();
    if (bGroup != sgbLastUnitGroup_g ||
        dwNow - sgdwLastUnitGroupTime_g >= 0x1F4) {
      sgbLastUnitGroup_g = bGroup;
      sgdwLastUnitGroupTime_g = dwNow;
      return;
    }
    CenterOnSelectionGroup(bGroup);
  }

  sgbLastUnitGroup_g = 0xFF;
}

// send_net_cmd_Select
// Diffs the selection against sgpNetSelected_g and sends a full Select (9), or a
// SelectDeltaDel (0x0B) and SelectDeltaAdd (0x0A) when smaller. As in 1.17, a
// null entry is dereferenced and the full Select lists the units in reverse.
// Hyperion: a selection command as 1.17 sends it, a WORD a unit, or in a game of wide
// unit ids with the opcode's top bit set and a DWORD a unit. The ids are
// gathered at 32 bits either way.
// In a game of Hyperion's selection, chunks of 0x8C, at least one (stage S).
struct SelectIds {
  DWORD dwUnits[SELECTION_WIDE];
};

static void QueueSelect(const SelectIds &ids, BYTE bOpcode, int nCount) {
  if (SelectionWide()) {
    int nAt = 0;
    do {
      net_cmd_selectchunk_t cmd;
      const int n = nCount - nAt < SELECT_CHUNK_UNITS ? nCount - nAt : SELECT_CHUNK_UNITS;
      cmd.bOpcode = NET_CMD_SELECT_CHUNK;
      cmd.bCount = (BYTE)n;
      cmd.bOp = (BYTE)(bOpcode | (nAt != 0 ? SELECT_CHUNK_NEXT : 0) | (nAt + n < nCount ? SELECT_CHUNK_MORE : 0));
      memcpy(cmd.dwUnits, &ids.dwUnits[nAt], n * sizeof(DWORD));
      netmgr_queue_cmd(&cmd, n * 4 + 3);
      nAt += n;
    } while (nAt < nCount);
    return;
  }
  if (PoolsWideIds()) {
    net_cmd_selectdelta_w_t cmd;
    memcpy(cmd.dwUnits, ids.dwUnits, nCount * sizeof(DWORD));
    cmd.bOpcode = bOpcode | NET_CMD_WIDE_IDS;
    cmd.bCount = (BYTE)nCount;
    netmgr_queue_cmd(&cmd, nCount * 4 + 2);
    return;
  }
  net_cmd_selectdelta_t cmd = {0};
  cmd.bOpcode = bOpcode;
  cmd.bCount = (BYTE)nCount;
  for (int i = 0; i < nCount; i++)
    cmd.wUnits[i] = (WORD)ids.dwUnits[i];
  netmgr_queue_cmd(&cmd, nCount * 2 + 2);
}

void __fastcall send_net_cmd_Select(int nCount, CUnit **ppUnits) {
  SelectIds cmdSel = {};  // Hyperion: the ids at 32 bits, QueueSelect
  SelectIds cmdAdd = {};
  SelectIds cmdDel = {};
  CUnit **ppNet = NetSelected();  // Hyperion: selectlimits.h's, 255 slots in Hyperion's selection

  sgbLastUnitGroup_g = 0xFF;

  CUnit **pp = ppUnits;
  int nAdd = 0;
  int i = nCount;
  while (i != 0) {
    CUnit *pUnit = *pp;
    i--;
    CUnit **ppFound = 0;
    if (pUnit != 0) {
      for (DWORD j = 0; j < (DWORD)SelectionMax(); j++) {
        if (ppNet[j] == 0)
          break;
        if (ppNet[j] == pUnit) {
          ppFound = &ppNet[j];
          break;
        }
      }
    }
    if (ppFound == 0) {
      BYTE bMask = gPlayback ? (BYTE)gdwReplayVisionMask : (BYTE)gdwLocalMask;
      if ((pUnit->pSprite->bVisibilityFlags & bMask) ||
          ViewHxSpriteSeen(pUnit->pSprite))  // Hyperion: the viewer's slots past 7 (stage D)
        cmdAdd.dwUnits[nAdd++] = pUnit->GetUnitID();
    }
    pp++;
  }

  int nDel = 0;
  CUnit **ps = ppNet;
  int k = SelectionMax();
  do {
    CUnit *pUnit = *ps;
    k--;
    if (pUnit == 0)
      break;
    CUnit **ppFound = 0;
    for (DWORD j = 0; j < (DWORD)nCount; j++) {
      if (ppUnits[j] == 0)
        break;
      if (ppUnits[j] == pUnit) {
        ppFound = &ppUnits[j];
        break;
      }
    }
    if (ppFound == 0)
      cmdDel.dwUnits[nDel++] = pUnit->GetUnitID();
    ps++;
  } while (k != 0);

  memset(ppNet, 0, SelectionMax() * sizeof(CUnit *));  // Hyperion: 0x30 in 1.17
  memcpy(ppNet, ppUnits, nCount * sizeof(CUnit *));  // Hyperion: 4 in 1.17

  if (nAdd + nDel >= nCount) {
    CUnit **pq = ppUnits;
    for (int j = nCount; j != 0; ) {
      j--;
      cmdSel.dwUnits[j] = (*pq)->GetUnitID();
      pq++;
    }
    QueueSelect(cmdSel, 9, nCount);
    return;
  }

  if (nDel != 0)
    QueueSelect(cmdDel, 0x0B, nDel);
  if (nAdd != 0)
    QueueSelect(cmdAdd, 0x0A, nAdd);
}

// sgdwLastUnitGroupTime_g @0x672650, a DWORD: GetTickCount at the last group
// recall, the other half of send_net_cmd_GroupUnits' double-tap test.
#ifdef OC_DLL_MODE
  #ifndef sgdwLastUnitGroupTime_g
  #define sgdwLastUnitGroupTime_g (*reinterpret_cast<DWORD *>(OC_VA(0x672650u)))
  #endif
#else
extern DWORD sgdwLastUnitGroupTime_g;
#endif

// KERNEL32!GetTickCount through the IAT slot 0x4EB2FC, declared as
// src117/lang/dlg_dlg.cpp declares it.
extern "C" __declspec(dllimport) ULONG __stdcall GetTickCount(void);

// SNetSendMessage -- ordinal import at 0x4D0268, the send twin of
// SNetReceiveMessage. extern "C" + __stdcall as above.
extern "C" BOOL __stdcall SNetSendMessage(int nPlayerId, const void *pData,
                                          unsigned int nSize);

// net_init_ids -- oracle 0x474580, 91 B. Sets the six per-game id slots and both
// Storm id maps to 8, "nobody", one past the last player slot. The loop is a
// do-while countdown over an unsigned counter: a `for` over 8 is fully unrolled
// and an int counter compiles to other code.
void __fastcall net_init_ids() {
  g_ActiveNationID = 8;
  g_ActiveHumanID  = 8;
  g_ActiveStormID  = 8;
  g_LocalNationID  = 8;
  g_LocalHumanID   = 8;
  g_LocalStormID   = 8;
  unsigned i = 8;
  do {
    --i;
    g_StormToNationID[i] = 8;
    g_StormToHumanID[i]  = 8;
  } while (i != 0);
}

// net_receive_message
// net_send_turn's error tail behind SNetReceiveMessage, with one more rejection,
// error 0x8510006B, before the latch test, and string 0x51 for 0x5B.
// SErrGetLastError is called twice, before the latch test and after it, as in
// 1.17.
BOOL __fastcall net_receive_message(int *pnPlayerId, char **ppData,
                                    unsigned int *pnSize) {
  char szErr[256];

  if (SNetReceiveMessage(pnPlayerId, ppData, pnSize))
    return 1;
  if (SErrGetLastError() == 0x8510006B)
    return 0;
  if (sgbHasNetError_g)
    return 0;

  DWORD dwErr = SErrGetLastError();
  if (sgbHasNetError_g)
    return 0;
  if (dwErr == 0x8510006A)
    return 0;
  if (dwErr == 0x120)
    return 0;
  if (dwErr == 0x4C7)
    return 0;

  SErrGetErrorStr(dwErr, szErr, 256);
  if (sgbHasNetError_g)
    return 0;

  leave_game(3);
  sgbHasNetError_g = 1;
  net_error_show_error(0x51, szErr, 0, 0);
  if (gwGameMode == 3)
    gameloop_init(4);
  CloseAllWindows();
  return 0;
}

// net_receive_turns
// SNetReceiveTurns with net_receive_message's error tail. On success the RNG is
// switched on around sub_477DA0 and then restored to its previous setting,
// which set_rand_enabled returns.
BOOL __fastcall net_receive_turns(int nFirstPlayer, int nPlayers,
                                  char **ppData, unsigned int *pnSize,
                                  DWORD *pdwPlayerBits) {
  char szErr[256];

  if (SNetReceiveTurns(nFirstPlayer, nPlayers, ppData, pnSize, pdwPlayerBits)) {
    int bWasEnabled = set_rand_enabled(1);
    sub_477DA0();
    set_rand_enabled(bWasEnabled);
    return 1;
  }

  if (SErrGetLastError() == 0x8510006B)
    return 0;
  if (sgbHasNetError_g)
    return 0;

  DWORD dwErr = SErrGetLastError();
  if (sgbHasNetError_g)
    return 0;
  if (dwErr == 0x8510006A)
    return 0;
  if (dwErr == 0x120)
    return 0;
  if (dwErr == 0x4C7)
    return 0;

  SErrGetErrorStr(dwErr, szErr, 256);
  if (sgbHasNetError_g)
    return 0;

  leave_game(3);
  sgbHasNetError_g = 1;
  net_error_show_error(0x51, szErr, 0, 0);
  if (gwGameMode == 3)
    gameloop_init(4);
  CloseAllWindows();
  return 0;
}

// net_send_message
// net_send_turn with SNetSendMessage for SNetSendTurn and one more stack
// argument (`ret 4h`); the error tail, string 0x5B included, is the same.
void __fastcall net_send_message(int nPlayer, const void *pData, int nLen) {
  char szErr[256];

  if (SNetSendMessage(nPlayer, pData, nLen))
    return;
  if (sgbHasNetError_g)
    return;

  DWORD dwErr = SErrGetLastError();
  if (sgbHasNetError_g)
    return;
  if (dwErr == 0x8510006A)
    return;
  if (dwErr == 0x120)
    return;
  if (dwErr == 0x4C7)
    return;

  SErrGetErrorStr(dwErr, szErr, 256);
  if (sgbHasNetError_g)
    return;

  leave_game(3);
  sgbHasNetError_g = 1;
  net_error_show_error(0x5B, szErr, 0, 0);
  if (gwGameMode == 3)
    gameloop_init(4);
  CloseAllWindows();
}

// net_send_turn
// SNetSendTurn and the block's shared error tail: on a real error, leave the
// game, set the latch, show string 0x5B and close the windows. The latch is
// tested again after every call, since a callee can set it.
void __fastcall net_send_turn(char *pData, unsigned int nSize) {
  char szErr[256];

  if (SNetSendTurn(pData, nSize))
    return;
  if (sgbHasNetError_g)
    return;

  DWORD dwErr = SErrGetLastError();
  if (sgbHasNetError_g)
    return;
  if (dwErr == 0x8510006A)
    return;
  if (dwErr == 0x120)
    return;
  if (dwErr == 0x4C7)
    return;

  SErrGetErrorStr(dwErr, szErr, 256);
  if (sgbHasNetError_g)
    return;

  leave_game(3);
  sgbHasNetError_g = 1;
  net_error_show_error(0x5B, szErr, 0, 0);
  if (gwGameMode == 3)
    gameloop_init(4);
  CloseAllWindows();
}
