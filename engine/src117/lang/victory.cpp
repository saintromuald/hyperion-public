// victory.cpp -- trigger execution state and the victory and defeat
// bookkeeping around it. See src117/h/victory.h for the trigger layout.
// Functions are in ascending oracle VA order, this TU's definition order.

// Include order matters here and is not alphabetical: src117/gen/globals117.h
// is generated with no includes of its own and relies on its includer for
// BYTE/DWORD and the struct types its rows use, so it comes last, after
// victory.h has pulled the platform scaffold and net_cmds.h has defined
// PlayerData.
#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/victory.h"
#include "../port/gameplay/port_eud.h"  // Hyperion: EUD maps

#include <string.h>  // memset

#include "../h/Storm.h"
#include "../h/TrigEnum.h"
#include "../h/gametype.h"
#include "../h/Teams.h"          // GetTeamName, IsTeamGame
#include "../h/net_cmds.h"
#include "../h/rtti.h"
#include "../h/saveload.h"  // pfread, pfwrite, compress, decompress
#include "../h/statres.h"    // HideCountdownTimer
#include "../h/TrigLB.h"     // get_lb_values, lb_update
#include "../h/chunk.h"      // ReadChunk, for HXTE
#include <vector>
#include "../h/VictDlg.h"    // victdlg_init

#include "../gen/globals117.h"

// The action handler, read off trigger_execute's dispatch: `lea ecx,[edx+148h]`
// puts &action in ECX alone, nothing cleans the stack, and the result is
// consumed by `test eax,eax`. Shape sibling of TRIGCONDPROC in victory.h.
typedef int(__fastcall *TRIGACTPROC)(TrigAction *pAction);

// The dual spelling. A plain `extern` here would stay a symbol in both
// compiles, which makes the shipped-form check vacuous. None of these is
// recorded in re/globals117.json: sgpCurrTrigger is struct-typed and the rest
// are `static` in the original TU, as the sheet's `sg` prefix says.
//
// Extents come from initialiser width: victory_init zeroes each one with a
// single SSE store, and the store's width is the extent (`movq`, 8 B = BYTE[8];
// two `movdqu`, 16 B each = DWORD[8]). The four 16-bit slots at 0x6CA748, 4C
// and 50 are four separate WORD scalars, by the store width in the body that
// writes each; 0x6CA744 is the fourth and is undeclared: nothing reads it.
//
// Three names are ours: 0x5349E0, 0x5349D4 and 0x526524 carry none in any
// approved source. gbVictoryState @0x5349E0 is BYTE[8], the committed victory
// code latched from the pending sgbVictoryCodes (0x6CA758) -- victory_chk tests
// it for zero and only then stores, so 0 means unset.
#ifdef OC_DLL_MODE
#define gdwCountdownTimer (*reinterpret_cast<DWORD *>(OC_VA(0x5349D4u)))
#define gbVictoryState (reinterpret_cast<BYTE *>(OC_VA(0x5349E0u)))
#define sgConditionFcns (reinterpret_cast<TRIGCONDPROC *>(OC_VA(0x50E058u)))
#define sgTriggers (reinterpret_cast<TRIGGERLIST *>(OC_VA(0x50E0D8u)))
#define gTypeInfo_TRIGGERNODE (*reinterpret_cast<type_info *>(OC_VA(0x510940u)))
#define gwMissionID (*reinterpret_cast<WORD *>(OC_VA(0x526524u)))
#define sgwCheckDelay (*reinterpret_cast<WORD *>(OC_VA(0x6CA748u)))
#define sgwVerifyDelay (*reinterpret_cast<WORD *>(OC_VA(0x6CA74Cu)))
#define sgwLBDelay (*reinterpret_cast<WORD *>(OC_VA(0x6CA750u)))
#define sgdwTriggerPlayer (*reinterpret_cast<DWORD *>(OC_VA(0x6CA754u)))
#define sgbVictoryCodes (reinterpret_cast<BYTE *>(OC_VA(0x6CA758u)))
#define sgbDelayedActions (reinterpret_cast<BYTE *>(OC_VA(0x6CA760u)))
#define sgdwDelayedTimers (reinterpret_cast<DWORD *>(OC_VA(0x6CA768u)))
#define sgpCurrTrigger (*reinterpret_cast<TRIGGERNODE **>(OC_VA(0x6CA790u)))
#define sgActionFcns (reinterpret_cast<TRIGACTPROC *>(OC_VA(0x50DF68u)))
#define dword_5349EC (*reinterpret_cast<DWORD *>(OC_VA(0x5349ECu)))
#define dword_5349F0 (*reinterpret_cast<DWORD *>(OC_VA(0x5349F0u)))
#define sgwTickDelay (*reinterpret_cast<WORD *>(OC_VA(0x6CA744u)))
#define sgpParticipants (reinterpret_cast<BYTE *>(OC_VA(0x6CA788u)))
#else
extern DWORD gdwCountdownTimer;
// Hyperion: the per-nation tables are player tables, for the slots past 7
extern PlayerVictoryStateTable gbVictoryState;
extern TRIGCONDPROC sgConditionFcns[24] OC_UNALIGNED;
extern PlayerTable<TRIGGERLIST, PlayerTagTriggers, 8> sgTriggers;
extern type_info gTypeInfo_TRIGGERNODE;
extern WORD gwMissionID;
extern WORD sgwCheckDelay;
extern WORD sgwVerifyDelay;
extern WORD sgwLBDelay;
extern DWORD sgdwTriggerPlayer;
extern PlayerTable<BYTE, PlayerTagVictoryCodes, 8> sgbVictoryCodes;
extern PlayerTable<BYTE, PlayerTagDelayedActions, 8> sgbDelayedActions;
extern PlayerTable<DWORD, PlayerTagDelayedTimers, 8> sgdwDelayedTimers;
extern TRIGGERNODE *sgpCurrTrigger;
extern TRIGACTPROC sgActionFcns[60] OC_UNALIGNED;  // sgConditionFcns follows at +0xF0
extern DWORD dword_5349EC;
extern DWORD dword_5349F0;
extern WORD sgwTickDelay;
extern PlayerTable<BYTE, PlayerTagParticipants, 8> sgpParticipants;
#endif

// gdwLocalBlackMask is registered in re/globals117.json but no marked row uses
// it yet, so gen emits nothing for the name; the #ifndef is per name, so it
// survives the row being marked.
#ifdef OC_DLL_MODE
#ifndef gdwLocalBlackMask
#define gdwLocalBlackMask (*reinterpret_cast<DWORD *>(OC_VA(0x67266Cu)))
#endif
#else
extern DWORD gdwLocalBlackMask;
#endif

// EliminatedMsg's callees are declared here rather than in victory.h: a callee
// earns a header slot only when the generated thunk needs one.

extern "C" size_t __cdecl strlen(const char *);

// error_get_str is seated in net_cmds.h, included above.


// GetTeamName and IsTeamGame are seated in Teams.h, included above.

// 0x46AC90, 57 B (mapmsg.cpp). Text in ECX, duration in EDX (`mov eax,1B58h /
// cmovz esi,eax` defaults it to 7000 ms), both consumed as 32-bit. The spelling
// has to match src117/h/net_cmds.h, which declares the same name: two spellings
// mangle differently and fail at the payload link.
void __fastcall map_msg_chat_msg(const char *pszText, int nTime);

// MSVC merges the three psnprintf calls into one call site, and the trailing guard
// is an inlined strlen rather than a `szBuf[0] != 0` test.
void __fastcall EliminatedMsg(int nPlayer) {
  char szBuf[256];

  if (IsScenarioGame(NULL)) {
    return;
  }
  szBuf[0] = '\0';

  if (IsTeamGame()) {
    psnprintf(szBuf, sizeof(szBuf), error_get_str(0x35),
              GetTeamName(gPlayerData[nPlayer].nTeam));
  } else if (gPlayerData[nPlayer].nType == 2 || gPlayback) {
    psnprintf(szBuf, sizeof(szBuf), error_get_str(0x35),
              gPlayerData[nPlayer].szName);
  } else if (gPlayerData[nPlayer].nType == 1) {
    psnprintf(szBuf, sizeof(szBuf), error_get_str(0x36), nPlayer + 1);
  }

  if (strlen(szBuf) != 0) {
    map_msg_chat_msg(szBuf, 0);
  }
}


void __fastcall CurrentTriggerDisableAbort(void) {
  sgpCurrTrigger->dwFlags |= 0x40;
}

// Clears bit 6 of the current trigger's
//            flags, a read-modify-write of the whole dword. That the bit means
//            "abort disabled" is the sheet name's claim.
void __fastcall CurrentTriggerEnableAbort(void) {
  sgpCurrTrigger->dwFlags &= ~0x40;
}

void __fastcall CurrentTriggerPaused(void) {
  sgpCurrTrigger->dwFlags |= 0x20;
}

void __fastcall CurrentTriggerUnpaused(void) {
  sgpCurrTrigger->dwFlags &= ~0x20;
}

// Returns the region table's address. The type
//            and the 20-byte stride come from the subscripting call sites; the
//            [255] extent in globals117.h is not proven by either.
MapRegion *__fastcall ExtMapRegions(void) {
  return gMapRegions;
}

// Reads the flags DWORD whole and returns
//            bit 4.
int __fastcall IsCurrentTriggerAborted(void) {
  return (sgpCurrTrigger->dwFlags >> 4) & 1;
}

// One byte of the delayed-action table,
//            returned as a BYTE. The parameter's signedness and width are not
//            known.
BYTE __fastcall IsDelayedAction(int nPlayer) {
  return sgbDelayedActions[nPlayer];
}

// Both pairs test 2 before 1, as
//            PlayerSetVictoryCode does.
int __fastcall IsOutOfGame(BYTE nPlayer) {
  if (!PlayerIsLobby(nPlayer)) {  // Hyperion: nPlayer >= 8 in 1.17
    return 0;
  }

  BYTE nType = gPlayerData[nPlayer].nType;
  if (nType == 0x0A || nType == 0x0B) {
    return 1;
  }
  if (nType == 2 || nType == 1) {
    BYTE bCode = gbVictoryState[nPlayer];
    if (bCode == 2 || bCode == 1) {
      return 1;
    }
  }
  return 0;
}

void __fastcall KeepCurrentTrigger(void) {
  sgpCurrTrigger->dwFlags |= 4;
}

// IsDelayedAction's shape over
//            sgbVictoryCodes, the array PlayerSetVictoryCode writes.
BYTE __fastcall PlayerGetVictoryCode(int nPlayer) {
  return sgbVictoryCodes[nPlayer];
}

// Reads gPlayerData[i].nType (stride 36;
//            0x6726D0 is 0x6726C8+8, not a global of its own). 2 is tested
//            before 1.
void __fastcall PlayerSetVictoryCode(int nPlayer, BYTE bCode) {
  if (gPlayerData[nPlayer].nType == 2 || gPlayerData[nPlayer].nType == 1) {
    sgbVictoryCodes[nPlayer] = bCode;
  }
}

void __fastcall SetDelayedAction(int nPlayer, DWORD dwTimer) {
  sgbDelayedActions[nPlayer] = 1;
  sgdwDelayedTimers[nPlayer] = dwTimer;
}

// Returns the 32-bit global; `int` follows the
//            sheet's use of the value as a player index.
int __fastcall TriggerPlayer(void) {
  return sgdwTriggerPlayer;
}

// Walk the local player's trigger list and
//            mark every resuming trigger aborted.
//
//            The flags are read once into a local, and the WORD-global store
//            precedes the field store, as in 1.17.
void __fastcall TryAbortCurrentTrigger(void) {
  if (gbMultiPlayerMode) {
    return;
  }
  if (gwMissionID >= 0x20) {
    return;
  }

  for (TRIGGERNODE *p = sgTriggers[g_LocalNationID].link.next; (intptr_t)p > 0;
       p = p->link.next) {
    DWORD dwFlags = p->dwFlags;
    if ((dwFlags & 1) && !(dwFlags & 0x40)) {
      sgwCheckDelay = 1;
      p->dwFlags = dwFlags | 0x10;
      sgbDelayedActions[g_LocalNationID] = 0;
      sgdwDelayedTimers[g_LocalNationID] = 0;
      snd_end_streamed();
      statport_trigger_reset();
      map_msg_trigger_abort();
    }
  }
}

// The 16-bit constant store again; see
//            victory_force_check below for why it is not a return value.
void __fastcall sub_4BDDE0(void) {
  sgwLBDelay = 1;
}

// Clears flags 0x1|0x8 and the action index
//            on every trigger of all eight lists.
//
//            The outer loop carries both a pointer and a count, the one shape
//            difference from victory_free below, which walks the same eight
//            lists.
__declspec(noinline)
void __fastcall sub_4BDDF0(void) {
  // Hyperion: over every nation's list
  for (int i = 0; i < PlayerLobbySlots(); i++) {
    TRIGGERLIST *pList = &sgTriggers[i];
    for (TRIGGERNODE *p = pList->link.next; (intptr_t)p > 0; p = p->link.next) {
      p->dwFlags &= ~9;
      p->bCurrentAction = 0;
    }
  }
}

//            Every division is unsigned -- `mul` + `shr`, never `imul` or
//            `sar` -- which fixes the timer's type as DWORD even though the
//            format strings say %d; the two magics are the compiler's.
//            Both divisions are hoisted above the branch because both arms need
//            them, and the H:MM:SS arm then re-divides (dwTime % 3600) by 60
//            rather than reusing the hoisted quotient, while its seconds term
//            still comes from the hoisted dwTime/60: the two moduli are
//            asymmetric in the emitted code on purpose. The buffer is 32 bytes,
//            stated twice, by `sub esp,20h` and by the `push 20h` psnprintf gets.
void __fastcall UpdateCountdownTimerString(void) {
  char szBuf[32];
  DWORD dwTime = gdwCountdownTimer;

  if (dwTime / 3600 != 0) {
    psnprintf(szBuf, sizeof(szBuf), "%d:%02d:%02d", dwTime / 3600,
              (dwTime % 3600) / 60, dwTime % 60);
  } else {
    psnprintf(szBuf, sizeof(szBuf), "%d:%02d", dwTime / 60, dwTime % 60);
  }
  SetCountdownText(szBuf);
}

// `__declspec(noinline)` is how the oracle's real `call` is reproduced:
// trigger_parse is trigger_cond_parse's only caller in the whole binary, so at
// /Ox MSVC splices the 16-entry condition loop into it and trigger_parse comes
// out 113 B against the oracle's 70. Definition order is not a lever -- MSVC
// inlines a function defined later in the same TU just as happily. The
// attribute does not alter trigger_cond_parse's own 60 bytes, and that
// Blizzard's source carried one is not claimed; what the oracle shows is that
// no inlining happened. It is repeated on the declaration in victory.h, where
// the line break after it matters: the generator's declaration pattern is a
// single-line regex that wants the line to start with the return type.
// 16 conditions of 20 bytes, dispatched on
//            cond->bCondition through sgConditionFcns with &cond in ECX.
//
//            The two exits differ: a condition byte of 0 ends the scan
//            successfully -- a `break`, not a `return 0` -- while a failing
//            handler returns 0. bCondition is loaded inside the branch.
__declspec(noinline)
int __fastcall trigger_cond_parse(TRIGGERNODE *pTrig) {
  for (int i = 0; i < 16; i++) {
    TrigCondition *pCond = &pTrig->conditions[i];
    if (!(pCond->bFlags & 2)) {
      BYTE bCond = pCond->bCondition;
      if (bCond == 0) {
        break;
      }
      if (!sgConditionFcns[bCond](pCond)) {
        return 0;
      }
    }
  }
  return 1;
}

// The TSList walk: the terminator is
//            ~(&list.link.prev), a large negative number, so the walk ends on a
//            signed test and `p != NULL` would dereference the terminator.
//
//            The action-index reset belongs inside the `!(flags & 1)` block;
//            hoisted out, it would reset the index on a path 1.17 does not.
void __fastcall trigger_parse(TRIGGERLIST *pList) {
  for (TRIGGERNODE *p = pList->link.next; (intptr_t)p > 0; p = p->link.next) {
    if (EudHalted())  // Hyperion: an EUD map reached memory not served
      return;
    DWORD dwFlags = p->dwFlags;
    if (dwFlags & 8) {
      continue;
    }
    sgpCurrTrigger = p;
    if (!(dwFlags & 1)) {
      if (!trigger_cond_parse(p)) {
        continue;
      }
      p->bCurrentAction = 0;
    }
    trigger_execute(p);
  }
}

// Hyperion: trigger_parse over an EUD game's list, which is 1.16.1's and walked
// by its addresses (port_eud.h). A node runs in place, so a trigger that rewrites another, or
// the next link of one, is seen as 1.16.1 would see it; the next is read once a node has run.
// An address no item serves ends the walk.
static void trigger_parse_eud(DWORD nList) {
  for (DWORD dwAddr = EudTriggersFirst(nList); (int)dwAddr > 0; dwAddr = EudTriggerNext(dwAddr)) {
    if (EudHalted())
      return;
    TRIGGERNODE *p = EudTriggerAt(dwAddr);
    if (p == NULL)
      return;
    DWORD dwFlags = p->dwFlags;
    if (dwFlags & 8) {
      continue;
    }
    sgpCurrTrigger = p;
    if (!(dwFlags & 1)) {
      if (!trigger_cond_parse(p)) {
        continue;
      }
      p->bCurrentAction = 0;
    }
    trigger_execute(p);
  }
}

// Stores the 16-bit constant 1 through a
//            register. It is not a return value: victory_init ends with this
//            exact shape and EAX dead.
void __fastcall victory_force_check(void) {
  sgwVerifyDelay = 1;
}

// The TSList unlink, instantiated for TRIGGERNODE. Two things matter: the link
// offset is recovered at runtime (`mov eax,esi / sub eax,[edx+4h] / add ecx,eax`)
// rather than folded to a compile-time zero, and nOff needs more than one
// reaching definition, or MSVC reassociates the three-term sum into two
// instructions where the oracle emits three -- which is what the
// redundant-looking inner `if` buys, as TSList.h records for the gametype.cpp
// instantiation. `static`: it is inlined into victory_free, and the standalone
// copy MSVC also emits is unreferenced and dropped by the linker.
static void UnlinkTrigger(TRIGGERNODE *node);

// Hyperion: the trigger lists of the slots past 7 start empty as 1.17's do, the
// link's prev at the link and its next the link's complement. Once.
static void TriggerListsHxInit() {
  for (int k = 0; k < sgTriggers.kExtra; k++) {
    TRIGGERLIST &l = sgTriggers.Extra()[k];
    if (l.link.prev == NULL) {
      l.link.prev = reinterpret_cast<TRIGGERNODE *>(&l.link);
      l.link.next = reinterpret_cast<TRIGGERNODE *>(~reinterpret_cast<intptr_t>(&l.link));
    }
  }
}

// Hyperion: the triggers on a nation's list, for the control channel
int TriggersCount(int nSlot) {
  int n = 0;
  if (PlayerIsLobby(nSlot))
    for (TRIGGERNODE *p = sgTriggers[nSlot].link.next; (intptr_t)p > 0; p = p->link.next)
      n++;
  return n;
}

static void UnlinkTrigger(TRIGGERNODE *node) {
  TSLink<TRIGGERNODE> *pl = &node->link;
  TRIGGERNODE *pprev = pl->prev;
  if (pprev != NULL) {
    TRIGGERNODE *pnext = pl->next;
    TSLink<TRIGGERNODE> *pnl;
    if ((intptr_t)pnext <= 0) {
      // the terminator: the complement IS the address of the list's own link
      pnl = reinterpret_cast<TSLink<TRIGGERNODE> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0) {
        nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
      }
      pnl = reinterpret_cast<TSLink<TRIGGERNODE> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    pl->prev->link.next = pl->next;
    pl->prev = NULL;
    pl->next = NULL;
  }
}

// TSList<TRIGGERNODE,...>::Clear over all
//            eight lists, fully inlined. The head is re-read every iteration
//            because UnlinkTrigger has just patched it. The allocation tag is
//            RTTI, from type_info::raw_name.
//
//            It is one loop, not two: the two loops share a header, and
//            `++i >= 8` is a signed compare.
void __fastcall victory_free(void) {
  int i = 0;
  for (;;) {
    TRIGGERNODE *p = sgTriggers.a[i].link.next;
    if ((intptr_t)p > 0) {
      UnlinkTrigger(p);
      SMemFree(p, gTypeInfo_TRIGGERNODE.raw_name(), -2, 0);
      continue;
    }
    if (++i >= 8) {
      break;
    }
  }
  // Hyperion: the lists of the slots past 7, whatever the game was
  TriggerListsHxInit();
  for (int k = 0; k < sgTriggers.kExtra; k++) {
    TRIGGERNODE *p;
    while ((intptr_t)(p = sgTriggers.Extra()[k].link.next) > 0) {
      UnlinkTrigger(p);
      SMemFree(p, gTypeInfo_TRIGGERNODE.raw_name(), -2, 0);
    }
  }
}

// The countdown is a post-decrement tested
// on the old value; the dialog mapping is 2 -> loss, 3 and 5 -> victory.
// noinline keeps it a call from triggers_loop, as in 1.17.
__declspec(noinline)
void __fastcall victory_update(void) {
  if (gPlayback != 0) {
    return;
  }
  if (sgwVerifyDelay-- != 0) {
    return;
  }
  sgwVerifyDelay = 45;
  switch (gbVictoryState[g_LocalNationID]) {
    case 2:
      do_loss_dlg();
      break;
    case 3:
    case 5:
      do_victory_dlg();
      break;
  }
}

// g_LocalNationID is loaded once and used as
//            both the byte index and the dword index; spelled as two subscripts
//            it may be reloaded after the intervening BYTE store.
void __fastcall victory_notify_action_done(void) {
  int nPlayer = g_LocalNationID;

  sgbDelayedActions[nPlayer] = 0;
  sgdwDelayedTimers[nPlayer] = 0;
  sgwCheckDelay = 1;
}

// sub_4BDDE0's sibling over sgwVerifyDelay
//            with 0x2D; victory_force_check writes 1 to the same WORD.
void __fastcall sub_4BE3D0(void) {
  sgwVerifyDelay = 45;
}

// The trigger table: load, save, distribute, run, and the victory bookkeeping
// the game loop drives every tick. Definitions below are in ascending oracle VA
// order, which is what lets each one call the previous without a forward
// declaration.

// TSList<TRIGGERNODE,...>::NewNode with LinkNode folded in: allocate a node,
// prime its link and append it to pList.
// The list's own link.prev holds the tail and link.next the head, so the append
// writes [pList+4]; that is what separates it from a head insert.
// pl and pNew stay separate values because the allocation-failure arm makes
// them differ: the oracle stores EDI (the node) and ECX (the link).
static __forceinline TRIGGERNODE *NewTrigger(TRIGGERLIST *pList) {
  TRIGGERNODE *pNew = static_cast<TRIGGERNODE *>(
      SMemAlloc(sizeof(TRIGGERNODE), gTypeInfo_TRIGGERNODE.raw_name(), -2, 8));
  TSLink<TRIGGERNODE> *pl;
  if (pNew != NULL) {
    pNew->link.prev = NULL;
    pNew->link.next = NULL;
    pl = &pNew->link;
  } else {
    pNew = NULL;
    pl = &pList->link;
  }

  // The same unlink UnlinkTrigger above carries, over a link rather than a node.
  TRIGGERNODE *pprev = pl->prev;
  if (pprev != NULL) {
    TRIGGERNODE *pnext = pl->next;
    TSLink<TRIGGERNODE> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<TRIGGERNODE> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0) {
        nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
      }
      pnl = reinterpret_cast<TSLink<TRIGGERNODE> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    pl->prev->link.next = pl->next;
    pl->prev = NULL;
    pl->next = NULL;
  }

  TRIGGERNODE *pTail = pList->link.prev;
  pl->prev = pTail;
  pl->next = pTail->link.next;
  pTail->link.next = pNew;
  pList->link.prev = reinterpret_cast<TRIGGERNODE *>(pl);
  return pNew;
}

// Hyperion: in a save of a Hyperion game, after 1.17's eight lists and
// the unit properties, the lists of the slots from 8 on: the count of their triggers, then
// one block of each slot's participant byte and count and every trigger's 0x960 bytes.
enum { TRIG_HX_FIRST = 8, TRIG_HX_SLOTS = PLAYER_SLOTS_MAX - TRIG_HX_FIRST };

static int TriggersHxSave(void *pFile) {
  WORD aCounts[TRIG_HX_SLOTS] = {0};
  DWORD nTotal = 0;
  for (int i = 0; i < TRIG_HX_SLOTS; i++)
    for (TRIGGERNODE *p = sgTriggers[TRIG_HX_FIRST + i].link.next; (intptr_t)p > 0; p = p->link.next) {
      aCounts[i]++;
      nTotal++;
    }
  const DWORD nSize = nTotal * 0x960 + TRIG_HX_SLOTS * 3;
  BYTE *pBuf = static_cast<BYTE *>(SMemAlloc(nSize, "victory.cpp", 0, 0));
  BYTE *pDst = pBuf;
  for (int i = 0; i < TRIG_HX_SLOTS; i++) {
    *pDst++ = sgpParticipants[TRIG_HX_FIRST + i];
    memcpy(pDst, &aCounts[i], 2);
    pDst += 2;
  }
  for (int i = 0; i < TRIG_HX_SLOTS; i++)
    for (TRIGGERNODE *p = sgTriggers[TRIG_HX_FIRST + i].link.next; (intptr_t)p > 0; p = p->link.next) {
      memcpy(pDst, reinterpret_cast<BYTE *>(p) + offsetof(TRIGGERNODE, conditions), 0x960);
      pDst += 0x960;
    }
  const int nOk = pfwrite(&nTotal, 4, pFile) && compress(pBuf, nSize, pFile);
  SMemFree(pBuf, "victory.cpp", 0, 0);
  return nOk;
}

static int TriggersHxLoad(void *pFile) {
  DWORD nTotal = 0;
  if (!pfread(&nTotal, 4, pFile) || nTotal > 0x10000)
    return 0;
  const DWORD nSize = nTotal * 0x960 + TRIG_HX_SLOTS * 3;
  BYTE *pBuf = static_cast<BYTE *>(SMemAlloc(nSize, "victory.cpp", 0, 0));
  int nOk = decompress(pBuf, nSize, pFile);
  if (nOk) {
    WORD aCounts[TRIG_HX_SLOTS];
    const BYTE *pSrc = pBuf;
    DWORD nSum = 0;
    for (int i = 0; i < TRIG_HX_SLOTS; i++) {
      sgpParticipants[TRIG_HX_FIRST + i] = *pSrc++;
      memcpy(&aCounts[i], pSrc, 2);
      pSrc += 2;
      nSum += aCounts[i];
    }
    nOk = nSum == nTotal;
    for (int i = 0; nOk && i < TRIG_HX_SLOTS; i++) {
      sgTriggers[TRIG_HX_FIRST + i].Clear();
      for (int j = 0; j < aCounts[i]; j++) {
        TRIGGERNODE *pNew = NewTrigger(&sgTriggers[TRIG_HX_FIRST + i]);
        memcpy(reinterpret_cast<BYTE *>(pNew) + offsetof(TRIGGERNODE, conditions), pSrc, 0x960);
        pSrc += 0x960;
      }
    }
  }
  SMemFree(pBuf, "victory.cpp", 0, 0);
  return nOk;
}

// Read the trigger count, decompress the
//            table into a scratch buffer and rebuild all eight lists from it.
//            The 0x18-byte header is 8 bytes of sgpParticipants followed by the
//            eight WORD per-player counts; each node's payload is the 0x960
//            bytes that follow TRIGGERNODE's own link. pSrc passes the header
//            in two steps (+8, then +0x10), as in 1.17.
int __fastcall LoadTriggers(void *pFile) {
  WORD aCounts[8];
  DWORD nCount = 0;

  if (!pfread(&nCount, 2, pFile)) {
    return 0;
  }

  DWORD nSize = (WORD)nCount * 0x960 + 0x18;
  BYTE *pBuf = static_cast<BYTE *>(SMemAlloc(nSize, "victory.cpp", 1066, 0));
  memset(pBuf, 0, nSize);

  if (decompress(pBuf, nSize, pFile)) {
    *reinterpret_cast<DWORD *>(sgpParticipants.a) = *reinterpret_cast<DWORD *>(pBuf);
    *reinterpret_cast<DWORD *>(sgpParticipants.a + 4) =
        *reinterpret_cast<DWORD *>(pBuf + 4);
    BYTE *pSrc = pBuf + 8;
    memcpy(aCounts, pSrc, 16);
    pSrc += 16;
    // Hyperion: the players' counts within the triggers the buffer holds; a save whose counts
    // add up past it is broken
    DWORD nAll = 0;
    for (int i = 0; i < 8; i++)
      nAll += aCounts[i];
    if (nAll > (WORD)nCount) {
      SMemFree(pBuf, "victory.cpp", 1117, 0);
      return 0;
    }

    for (int i = 0; i < 8; i++) {
      sgTriggers[i].Clear();
      int n = aCounts[i];
      for (int j = 0; j < n; j++) {
        TRIGGERNODE *pNew = NewTrigger(&sgTriggers[i]);
        memcpy(reinterpret_cast<BYTE *>(pNew) + offsetof(TRIGGERNODE, conditions), pSrc, 0x960);
        pSrc += 0x960;
      }
    }

    if (decompress(getUnitProps(), 0x500, pFile)) {
      getUnitProps();
      SMemFree(pBuf, "victory.cpp", 1113, 0);
      return !g_hxPlayers || TriggersHxLoad(pFile);  // Hyperion
    }
  }

  SMemFree(pBuf, "victory.cpp", 1117, 0);
  return 0;
}

// Retire a defeated player: convert its
//            slot type, then hand its units over or destroy them. The player
//            loop counts down as a do/while, as in 1.17. The team arm reads the
//            slot type once, the other arm twice.
void __fastcall MarkPlayerAsDefeated(BYTE nPlayer) {
  if (IsTeamGame()) {
    int j = PlayerLobbySlots();  // Hyperion: 8 in 1.17
    do {
      --j;
      if (gPlayerData[j].nTeam == gPlayerData[nPlayer].nTeam) {
        BYTE nType = gPlayerData[j].nType;
        if (nType == 1) {
          gPlayerData[j].nType = 0x0B;
        } else if (nType == 2) {
          gPlayerData[j].nType = 0x0A;
        }
      }
    } while (j != 0);
  } else {
    if (gPlayerData[nPlayer].nType == 1) {
      gPlayerData[nPlayer].nType = 0x0B;
    }
    if (gPlayerData[nPlayer].nType == 2) {
      gPlayerData[nPlayer].nType = 0x0A;
    }
  }

  if (!IsCTFGame() && !IsSuddenDeathGame()) {
    CUnit *p = CUnit::GetPlayerUnitList(nPlayer);
    while (p != NULL) {
      CUnit *pNext = p->pPlayerUnitNext;
      p->GiveToNeutralSlot();
      p = pNext;
    }
    status_set_update();
  } else {
    CUnit *p = CUnit::GetPlayerUnitList(nPlayer);
    while (p != NULL) {
      if ((gulUnitTypeFlags[p->wType] & 0x10) || p->IsDeadOrDying()) {
        p = p->pPlayerUnitNext;
        continue;
      }
      CUnit *pCur = p;
      p = p->pPlayerUnitNext;
      if ((gulUnitTypeFlags[pCur->wType] & 0x800) && pCur->pUnionD4 != NULL) {
        static_cast<CUnit *>(pCur->pUnionD4)->DestroyInventory();
      } else {
        pCur->Suicide();
      }
    }
  }

  if (gPlayback != 0 && nPlayer < 8) {  // Hyperion: 1.17's masks
    gdwReplayVisionMask &= ~(1 << nPlayer);
    gdwLocalMask &= ~(1 << nPlayer);
    gdwLocalBlackMask &= ~(1 << (nPlayer + 8));
  }
}

// Count the triggers, pack them behind the
//            0x18-byte header LoadTriggers reads, and compress it to the file.
//            nTotal is declared before aCounts, which keeps its store ahead of
//            the zero-init.
int __fastcall SaveTriggers(void *pFile) {
  DWORD nTotal = 0;
  WORD aCounts[8] = {0};

  for (int i = 0; i < 8; i++) {
    TRIGGERNODE **ppNext = &sgTriggers[i].link.next;
    TRIGGERNODE *p = *ppNext;
    while ((intptr_t)p > 0) {
      nTotal++;
      aCounts[i]++;
      TSLink<TRIGGERNODE> *pl = (p != NULL)
          ? &p->link
          : reinterpret_cast<TSLink<TRIGGERNODE> *>(
                reinterpret_cast<BYTE *>(ppNext) - 4);
      p = pl->next;
    }
  }

  DWORD nSize = (WORD)nTotal * 0x960 + 0x18;
  BYTE *pBuf = static_cast<BYTE *>(SMemAlloc(nSize, "victory.cpp", 1009, 0));

  *reinterpret_cast<DWORD *>(pBuf) = *reinterpret_cast<DWORD *>(sgpParticipants.a);
  *reinterpret_cast<DWORD *>(pBuf + 4) =
      *reinterpret_cast<DWORD *>(sgpParticipants.a + 4);
  BYTE *pDst = pBuf + 8;
  memcpy(pDst, aCounts, 16);
  pDst += 16;

  for (int i = 0; i < 8; i++) {
    for (TRIGGERNODE *p = sgTriggers[i].link.next; (intptr_t)p > 0; p = p->link.next) {
      memcpy(pDst, reinterpret_cast<BYTE *>(p) + offsetof(TRIGGERNODE, conditions), 0x960);
      pDst += 0x960;
    }
  }

  int nOk = pfwrite(&nTotal, 2, pFile);
  if (nOk) {
    nOk = compress(pBuf, nSize, pFile);
  }
  getUnitProps();
  if (nOk) {
    nOk = compress(getUnitProps(), 0x500, pFile);
  }
  getUnitProps();
  SMemFree(pBuf, "victory.cpp", 1041, 0);
  if (nOk && g_hxPlayers)
    nOk = TriggersHxSave(pFile);  // Hyperion
  return nOk;
}

#if !defined(OC_PAYLOAD) || defined(OC_WITH_HELD)
// oracle 0x4BDB00 (696 B). Copy pTrigger into the per-player lists its 27-byte
// players[] selects. The switch value is the players[] index: 0..12 name a
// player, 17 is all players, 18..21 are forces 1..4, and the rest do nothing.
// The record is 0x960 bytes and goes in at node+8, past TRIGGERNODE's link.
int __fastcall TryAddTrigger(void *pTrigger, int nFlags) {
  BYTE *pPlayers = static_cast<BYTE *>(pTrigger) + 0x944;

  for (DWORD i = 0; i < 27; i++, pPlayers++) {
    if (*pPlayers == 0) {
      continue;
    }
    switch (i) {
      case 18:
      case 19:
      case 20:
      case 21: {
        int nCursor;
        for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots();
             n = nation_find_next(&nCursor)) {
          if (GetPlayerForce(n) == i - 17) {
            TRIGGERNODE *pNew = NewTrigger(&sgTriggers[n]);
            memcpy(reinterpret_cast<BYTE *>(pNew) + offsetof(TRIGGERNODE, conditions), pTrigger, 0x960);
          }
        }
        break;
      }
      case 17: {
        int nCursor;
        for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots();
             n = nation_find_next(&nCursor)) {
          TRIGGERNODE *pNew = NewTrigger(&sgTriggers[n]);
          memcpy(reinterpret_cast<BYTE *>(pNew) + offsetof(TRIGGERNODE, conditions), pTrigger, 0x960);
        }
        break;
      }
      case 13:
      case 14:
      case 15:
      case 16:
      case 22:
      case 23:
      case 24:
      case 25:
      case 26:
        break;
      default: {
        int nCursor;
        for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots();
             n = nation_find_next(&nCursor)) {
          if (i == n) {
            TRIGGERNODE *pNew = NewTrigger(&sgTriggers[n]);
            memcpy(reinterpret_cast<BYTE *>(pNew) + offsetof(TRIGGERNODE, conditions), pTrigger, 0x960);
            break;
          }
        }
        break;
      }
    }
  }
  return 1;
}
#endif  // OC_PAYLOAD

// Hyperion: HXTE, version 1: a DWORD version, a DWORD count of TRIG's
// triggers, then for each a QWORD of the slots past 11 it runs for, bit n slot n (bits 0
// to 11 are not read: those slots are in the trigger's own players). A map without it
// runs no trigger for those slots but through the groups.
static std::vector<unsigned long long> g_trigHxSlots;

void TriggersHxMapBegin() {
  g_trigHxSlots.clear();
}

int __stdcall TriggersMapHxte(void *pFile, int nSize, void *) {
  std::vector<BYTE> ab(nSize > 0 ? nSize : 1);
  if (nSize < 8 || !ReadChunk(pFile, &ab[0]))
    return FALSE;
  DWORD dwVersion, dwCount;
  memcpy(&dwVersion, &ab[0], 4);
  memcpy(&dwCount, &ab[4], 4);
  if (dwVersion != 1 || static_cast<unsigned long long>(nSize) != 8 + 8ull * dwCount)
    return FALSE;
  g_trigHxSlots.resize(dwCount);
  if (dwCount)
    memcpy(&g_trigHxSlots[0], &ab[8], 8 * dwCount);
  return TRUE;
}

// A code TRIG_HX_CODE + slot as the slot, tagged for the callbacks.
static DWORD TrigHxCode(DWORD dwCode) {
  if (dwCode < TRIG_HX_CODE || dwCode >= TRIG_HX_CODE + PLAYER_SLOTS_MAX)
    return dwCode;
  return TrigSlotTag(dwCode - TRIG_HX_CODE);
}

// In a Hyperion game, the codes in a trigger record's condition and action players, and in
// Give Units' new owner, become tagged slots, so that slot 13 is not read as the group 13,
// the current player. A 1.17 game reads no Hyperion map, so it never sees a code.
void TriggerHxCodes(void *pTrigger) {
  if (!g_hxPlayers)
    return;
  TrigCondition *pCond = static_cast<TrigCondition *>(pTrigger);
  for (int i = 0; i < 16; i++)
    pCond[i].dwGroup = TrigHxCode(pCond[i].dwGroup);
  TrigAction *pAct = reinterpret_cast<TrigAction *>(static_cast<BYTE *>(pTrigger) + 16 * sizeof(TrigCondition));
  for (int i = 0; i < 64; i++) {
    pAct[i].dwGroup = TrigHxCode(pAct[i].dwGroup);
    if (pAct[i].bAction == 48)  // Give Units
      pAct[i].dwValue = TrigHxCode(pAct[i].dwValue);
  }
}

// TRIG's trigger nIndex onto the lists of the slots past 11 HXTE gives it, in the order the
// map has them, as TryAddTrigger does for a player.
void TryAddTriggerHx(void *pTrigger, int nIndex) {
  if (!g_hxPlayers || nIndex < 0 || static_cast<size_t>(nIndex) >= g_trigHxSlots.size())
    return;
  const unsigned long long qwSlots = g_trigHxSlots[nIndex];
  int nCursor;
  for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots(); n = nation_find_next(&nCursor)) {
    if (n < PLAYER_SLOTS_117 || n >= 64 || !((qwSlots >> n) & 1))
      continue;
    TRIGGERNODE *pNew = NewTrigger(&sgTriggers[n]);
    memcpy(reinterpret_cast<BYTE *>(pNew) + offsetof(TRIGGERNODE, conditions), pTrigger, 0x960);
  }
}

// The noinline is what trigger_cond_parse above needs for the same reason:
// without it MSVC splices this body into trigger_parse, which calls it.
__declspec(noinline)
// oracle 0x4BDF10 (184 B). Run a trigger's actions from where it left off.
// The loop reads bCurrentAction from memory every iteration, and the post-loop
// >= 64 test is a second read -- the handler-returned-0 exit lands there too
// and leaves the index below 64, which is what the second test discriminates.
void __fastcall trigger_execute(TRIGGERNODE *pTrig) {
  pTrig->dwFlags |= 1;
  if (pTrig->dwFlags & 2) {
    sgbVictoryCodes[sgdwTriggerPlayer] = 4;
  }

  for (;;) {
    BYTE bAct = pTrig->bCurrentAction;
    // Clamped, then tested for equality: that pair is what keeps MSVC from
    // rotating the loop, and it folds back into one `jnb`.
    if (bAct >= 64)
      bAct = 64;
    if (bAct == 64) {
      break;
    }
    TrigAction *pAct = &pTrig->actions[bAct];
    if (pAct->bFlags & 2) {
      pTrig->bCurrentAction = (BYTE)(bAct + 1);
      continue;
    }
    if (pAct->bAction == 0) {
      pTrig->bCurrentAction = 64;
      break;
    }
    if (!sgActionFcns[pAct->bAction](pAct)) {
      break;
    }
    if (EudHalted())  // Hyperion: the trigger left where it stopped, as Remastered leaves it
      return;
    pTrig->bCurrentAction++;
  }

  if (pTrig->bCurrentAction >= 64) {
    if (pTrig->dwFlags & 0x20) {
      sgActionFcns[6](NULL);
    }
    if (pTrig->dwFlags & 4) {
      pTrig->bCurrentAction = 0;
      pTrig->dwFlags &= ~0x51;
    } else {
      pTrig->dwFlags |= 8;
    }
  }
}

// Settle this tick's victory codes and
//            apply them. The two participant counters are BYTEs and the three
//            index loops are unsigned. The counting loop is a do/while, which
//            MSVC does not unroll; the sibling loops below keep the for.
void victory_chk(void) {
  if (!gbMultiPlayerMode) {
    DWORD dwCheats = glCheatBits;
    int nLocal = g_LocalNationID;
    if (dwCheats & 0x80) {
      sgbVictoryCodes[nLocal] = 3;
    } else if (dwCheats & 0x40) {
      sgbVictoryCodes[nLocal] = 2;
    }
    if ((dwCheats & 0x100) && sgbVictoryCodes[nLocal] >= 2) {
      sgbVictoryCodes[nLocal] = 0;
    }
  }

  int nCursor;
  for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots();
       n = nation_find_next(&nCursor)) {
    if (sgbVictoryCodes[n] >= 3) {
      int nInner;
      for (DWORD m = nation_find_first(&nInner); m < (DWORD)PlayerLobbySlots();
           m = nation_find_next(&nInner)) {
        if (sgbVictoryCodes[m] == 0) {
          sgbVictoryCodes[m] = 2;
          if (m == g_LocalNationID) {
            sgwVerifyDelay = 45;
          }
        }
      }

      BYTE nPlaying = 0;
      BYTE nWinning = 0;
      DWORD i = 0;
      do {
        if (sgpParticipants[i] != 0) {
          nPlaying++;
          if (sgbVictoryCodes[i] >= 3) {
            nWinning++;
          }
        }
      } while (++i < (DWORD)PlayerLobbySlots());  // Hyperion: 8 in 1.17
      if (nPlaying == nWinning) {
        for (DWORD i = 0; i < (DWORD)PlayerLobbySlots(); i++) {
          if (sgpParticipants[i] != 0) {
            sgbVictoryCodes[i] = 5;
          }
        }
      }
      break;
    }
  }

  for (DWORD i = 0; i < (DWORD)PlayerLobbySlots(); i++) {  // Hyperion: 8
    BYTE bCode = sgbVictoryCodes[i];
    if (bCode != 0 && gbVictoryState[i] == 0) {
      gbVictoryState[i] = bCode;
      if (bCode == 2) {
        EliminatedMsg(i);
        MarkPlayerAsDefeated((BYTE)i);
      }
    }
  }

  if (gbMultiPlayerMode) {
    for (DWORD i = 0; i < (DWORD)PlayerLobbySlots(); i++) {  // Hyperion: 8
      BYTE bCode = sgbVictoryCodes[i];
      if (bCode != 0 && bCode != 4) {
        sgTriggers[i].Clear();
      }
    }
  }
}

// Drop a dropped player's triggers and latch
//            the disconnect code. MarkPlayerAsDefeated takes a BYTE.
void __fastcall victory_disconnect(int nPlayer) {
  sgTriggers[nPlayer].Clear();
  if (EudGame())  // Hyperion: and 1.16.1's list of an EUD game
    EudTriggersClear(nPlayer);
  if (gbVictoryState[nPlayer] == 0) {
    gbVictoryState[nPlayer] = 1;
    MarkPlayerAsDefeated((BYTE)nPlayer);
  }
}

// Reset the trigger machinery for a new
//            game, skipping the map-side state when a save is being loaded.
//            Every zeroing is a memset with a literal length, never a sizeof,
//            since a registered array can be a 4-byte pointer.
void victory_init(void) {
  victdlg_init();

  if (gpLoadGameFile == NULL) {
    g_dwElapsedTime = 0;
    gdwCountdownTimer = 0;
    memset(dword_534F20, 0, 32);
    dword_53632C = 0;
    dword_5349E8 = 1;
    dword_5349EC = 0;
    dword_5349F0 = 0;
    PlayerClear(gbVictoryState);  // Hyperion: and the slots past 7
    PlayerClear(sgpParticipants);

    int nCursor;
    for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots();
         n = nation_find_next(&nCursor)) {
      sgpParticipants[n] = 1;
    }
  }

  gbTriggerPaused = 0;
  PlayerClear(sgbDelayedActions);  // Hyperion: and the slots past 7
  PlayerClear(sgdwDelayedTimers);
  TriggerListsHxInit();
  memset(get_lb_values(), 0xFF, lb_values_bytes());  // Hyperion: 0x40 in 1.17
  sgwTickDelay = 15;
  sgwLBDelay = 15;
  sub_4BDDF0();
  sgwCheckDelay = 1;
}

// Shape twin of victory_disconnect with the
//            quit code; the only byte between them is the latched value.
void __fastcall victory_quit(int nPlayer) {
  sgTriggers[nPlayer].Clear();
  if (EudGame())  // Hyperion: and 1.16.1's list of an EUD game
    EudTriggersClear(nPlayer);
  if (gbVictoryState[nPlayer] == 0) {
    gbVictoryState[nPlayer] = 2;
    MarkPlayerAsDefeated((BYTE)nPlayer);
  }
}

// Age the eight delayed-action timers, then
//            the leaderboard and countdown clocks. 0xFFFFFFFF means no deadline
//            and the comparison is unsigned, so the timer is a DWORD. Both clock
//            ticks are post-decrements tested on the old value, as in
//            victory_update; as pre-decrements they would fire a tick early.
void timers_loop(int nTicks) {
  for (int i = 0; i < PlayerLobbySlots(); i++) {  // Hyperion: 8 in 1.17
    if (sgbDelayedActions[i] != 0) {
      DWORD dwLeft = sgdwDelayedTimers[i];
      if (dwLeft != 0xFFFFFFFF) {
        if (dwLeft <= (DWORD)nTicks) {
          sgbDelayedActions[i] = 0;
          sgdwDelayedTimers[i] = 0;
          sgwCheckDelay = 1;
        } else {
          sgdwDelayedTimers[i] = dwLeft - nTicks;
        }
      }
    }
  }

  if (IsGamePaused()) {
    return;
  }

  if (sgwLBDelay-- == 0) {
    sgwLBDelay = 15;
    lb_update();
  }

  if (sgwTickDelay-- == 0) {
    g_dwElapsedTime++;
    if (gdwCountdownTimer != 0) {
      if (dword_53632C == 0) {
        if (--gdwCountdownTimer == 0) {
          sgwCheckDelay = 1;
        }
      }
      UpdateCountdownTimerString();
    } else {
      HideCountdownTimer();
    }
    sgwTickDelay = 15;
  }
}

// Hyperion: an EUD game's lists as 1.16.1's, built once from the game's own as
// its triggers first run: every node of the eight lists, in each list's order.
static void TriggersEudBuild() {
  if (EudTriggersBuilt())
    return;
  DWORD nNodes = 0;
  for (int i = 0; i < 8; i++)
    for (TRIGGERNODE *p = sgTriggers[i].link.next; (intptr_t)p > 0; p = p->link.next)
      nNodes++;
  EudTriggersBegin(nNodes);
  for (int i = 0; i < 8; i++)
    for (TRIGGERNODE *p = sgTriggers[i].link.next; (intptr_t)p > 0; p = p->link.next)
      EudTriggerAdd(i, reinterpret_cast<BYTE *>(p) + offsetof(TRIGGERNODE, conditions));
  EudTriggersEnd();
}

// One tick of the trigger machinery: age
//            the clocks, then walk every active nation's trigger list and
//            settle the victory codes if any of them ran. The head is read into
//            a local and null-tested before the signed test, as in 1.17.
void triggers_loop(int nTicks) {
  if (IsGamePaused() && gbTriggerPaused == 0) {
    return;
  }

  victory_update();
  timers_loop(nTicks);

  if (sgwCheckDelay-- != 0) {
    return;
  }
  sgwCheckDelay = 30;
  PlayerClear(sgbVictoryCodes);  // Hyperion: and the slots past 7
  clear_enum_cache();

  int nCursor;
  int bAny = 0;
  if (EudGame())  // Hyperion: 1.16.1's lists
    TriggersEudBuild();
  for (sgdwTriggerPlayer = nation_find_first(&nCursor); sgdwTriggerPlayer < (DWORD)PlayerLobbySlots();
       sgdwTriggerPlayer = nation_find_next(&nCursor)) {
    if (EudGame()) {  // Hyperion: walked by 1.16.1's addresses
      const DWORD nList = sgdwTriggerPlayer;
      if ((int)EudTriggersFirst(nList) > 0) {
        trigger_parse_eud(nList);
        bAny = 1;
      }
      continue;
    }
    TRIGGERNODE *pHead = sgTriggers[sgdwTriggerPlayer].link.next;
    if (pHead != NULL && (intptr_t)pHead > 0) {
      trigger_parse(&sgTriggers[sgdwTriggerPlayer]);
      bAny = 1;
    }
  }
  if (EudGame())  // Hyperion: what the map wrote that applies once its triggers have run
    EudTriggersDone();
  if (bAny) {
    victory_chk();
  }
}
