// SAI_Scripts.cpp -- the AI script interpreter: the bytecode reader, the script list and its save/load. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller in this file by
// /Ob2, which the oracle (WPO off) never does across TUs; the lever against it
// is __declspec(noinline) on the declaration.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../port/data/port_formats.h"  // Hyperion: PortSave117Decompress, PortSave117Compress
#include "../port/data/port_moddata.h"  // Hyperion: ModRaceAiUnit
#include "../port/gameplay/port_ai.h"  // Hyperion: a computer's level
#include "../gen/save117.h"
#include <string.h>

#include "../h/SAI_Scripts.h"
#include "../h/glues.h"
#include "../h/CSprite.h"
#include "../h/CUnit.h"
#include "../h/CUnitSearches.h"
#include "../h/CUnitZSpells.h"
#include "../h/net_cmds.h"        // PlayerData -- gPlayerData's element type
#include "../h/sai.h"
#include "../h/SAI_Banker.h"
#include "../h/SAI_Build.h"
#include "../h/SAI_Main.h"
#include "../h/sai_TownBuilder.h"
#include "../h/TAI_Captain.h"
#include "../h/TrigAct.h"
#include "../h/saveload.h"      // compress / decompress
#include "../h/snd.h"           // load_game_data
#include "../h/CUnitMisc.h"      // the free IsEnemyUnit
#include "../h/CUnitInit.h"      // trigger_create_unit
#include "../h/net_rand.h"       // net_set_fixed_seed
#include "../h/TAI_GuardPost.h"  // TAI_GuardPost_aiscript_place_guard
#include "../h/gds_appcrit.h"    // appcritf
#include "../h/statbtn.h"        // TechUseAllowed
#include "../h/rect16.h"

#include "../gen/globals117.h"

// Globals this TU reaches, dual-spelled, with one #ifdef per name rather than
// one over the block: a single guard would undeclare every name under it.

// gpAIScript_g -- BYTE * @0x6A2E74 (SAI_Scripts.cpp in the naming sheet).
// sai_scripts_init loads `scripts\aiscript.bin` here.
#ifdef OC_DLL_MODE
#define gpAIScript_g (*reinterpret_cast<BYTE **>(OC_VA(0x6A2E74u)))
#else
extern BYTE *gpAIScript_g;
#endif

// gpBWScript_g -- BYTE * @0x6A2E78 (SAI_Scripts.cpp in the naming sheet).
// The expansion's `scripts\bwscript.bin`, selected by bit 0 of TScript.bFlags.
// Static in the proof form: SAI_Scripts_aiscript_progress hoists a load of it
// above a store through its TScript, which MSVC does only for a non-escaping global.
#ifdef OC_DLL_MODE
#define gpBWScript_g (*reinterpret_cast<BYTE **>(OC_VA(0x6A2E78u)))
#else
extern BYTE *gpBWScript_g;
#endif

// gScriptMgr_g -- TScriptMgr @0x6A2E6C (SAI_Scripts.cpp in the naming sheet).
// The pool pointer and the live-list head, and the eight bytes the save codec
// moves in one call.
#ifdef OC_DLL_MODE
#define gScriptMgr_g (*reinterpret_cast<TScriptMgr *>(OC_VA(0x6A2E6Cu)))
#else
extern TScriptMgr gScriptMgr_g;
#endif

// gScripts_g -- TScriptPool @0x6A1A18 (SAI_Scripts.cpp in the naming sheet).
#ifdef OC_DLL_MODE
#define gScripts_g (*reinterpret_cast<TScriptPool *>(OC_VA(0x6A1A18u)))
#else
extern TScriptPool gScripts_g;
#endif

// ret_addr -- DWORD @0x6A2E7C (SAI_Scripts.cpp in the naming sheet). The one
// saved return offset of the script call/return opcodes.
#ifdef OC_DLL_MODE
#define ret_addr (*reinterpret_cast<DWORD *>(OC_VA(0x6A2E7Cu)))
#else
extern DWORD ret_addr;
#endif

// stru_508F70 -- DWORD[] @0x508F70 (SAI_Scripts.cpp in the naming sheet).
// The zero-terminated list of script ids run_aiscript lets any slot type start.
#ifdef OC_DLL_MODE
#define stru_508F70 (reinterpret_cast<DWORD *>(OC_VA(0x508F70u)))
#else
extern DWORD stru_508F70[];
#endif

// The free list, inlined at every site the oracle inlines it.

// Returns a thread to the pool's free list, unlinking it from the live list
// first. The head store is unconditional (`cmovz`) because only the value is
// conditional. pPool is a local because a store through a TScript * may alias
// the manager, and reading it back costs a reload the oracle does not have.
static __forceinline void script_free(TScript *pScript)
{
  TScriptPool *pPool;

  if (gScriptMgr_g.pHead == pScript)
    gScriptMgr_g.pHead = pScript->pNext;
  if (pScript->pPrev != NULL)
    pScript->pPrev->pNext = pScript->pNext;
  if (pScript->pNext != NULL)
    pScript->pNext->pPrev = pScript->pPrev;
  pPool = gScriptMgr_g.pPool;
  pScript->pPrev = NULL;
  pScript->pNext = pPool->pFree;
  if (pPool->pFree != NULL)
    pPool->pFree->pPrev = pScript;
  pPool->pFree = pScript;
}

// Links a thread at the head of the live list.
static __forceinline void script_link_head(TScript *pScript)
{
  if (pScript == NULL)
    return;
  pScript->pPrev = NULL;
  pScript->pNext = gScriptMgr_g.pHead;
  if (gScriptMgr_g.pHead != NULL)
    gScriptMgr_g.pHead->pPrev = pScript;
  gScriptMgr_g.pHead = pScript;
}

// The bodies, in oracle VA order.

// 0x493240 (81 B). Unlinks a thread from the live list and pushes it on the
// pool's free list. noinline because the oracle calls it.
__declspec(noinline) void TSaveContainer<TScript,100>::Free(TScript *pNode)
{
  if (pHead == pNode)
    pHead = pNode->pNext;
  if (pNode->pPrev != NULL)
    pNode->pPrev->pNext = pNode->pNext;
  if (pNode->pNext != NULL)
    pNode->pNext->pPrev = pNode->pPrev;
  TSaveNode<TScript,100> *pSave = pPool;
  pNode->pPrev = NULL;
  pNode->pNext = pSave->pFree;
  if (pSave->pFree != NULL)
    pSave->pFree->pPrev = pNode;
  pSave->pFree = pNode;
}

// 0x4932A0 (77 B). The player's first unit of a type with at least nEnergy
// mana that is running one of three orders.
CUnit *aiscript_find_caster(int nPlayer, WORD wType, DWORD nEnergy)
{
  for (CUnit *pUnit = CUnit::GetPlayerUnitList(nPlayer); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    if (pUnit->wType == wType
        && static_cast<BYTE>(pUnit->wEnergy >> 8) >= nEnergy) {
      int nOrder = pUnit->bOrderId;
      if (nOrder == 0x9C || nOrder == 0xA0 || nOrder == 2)
        return pUnit;
    }
  }
  return NULL;
}

// 0x4932F0 (29 B). The byte load sits in both arms on purpose: with the select
// written over the base pointer instead, MSVC if-converts it to `cmove eax,
// [base]` and there is no branch. There is no 8-bit cmov, so a BYTE destination
// keeps the branch. Indexing the field directly rather than through a cursor
// local is what makes the cursor the SIB base, `[ecx+eax]` and not `[eax+ecx]`.
BYTE aiscript_get_byte(TScript *pScript)
{
  BYTE bValue;

  if (pScript->flags.bBWScript)
    bValue = gpBWScript_g[pScript->dwOffset];
  else
    bValue = gpAIScript_g[pScript->dwOffset];
  pScript->dwOffset++;
  return bValue;
}

// 0x493310 (32 B).
WORD aiscript_get_word(TScript *pScript)
{
  WORD wValue;

  if (pScript->flags.bBWScript)
    wValue = *reinterpret_cast<WORD *>(&gpBWScript_g[pScript->dwOffset]);
  else
    wValue = *reinterpret_cast<WORD *>(&gpAIScript_g[pScript->dwOffset]);
  pScript->dwOffset += 2;
  return wValue;
}

// The per-record clear Init and PreSave inline. The rectangle is cleared left,
// right, top, bottom; that is the oracle's store order.
static void script_clear(TScript *p)
{
  p->dwOffset = 0;
  p->dwWait = 0;
  p->nPlayer = 0;
  p->rcArea.left = 0;
  p->rcArea.right = 0;
  p->rcArea.top = 0;
  p->rcArea.bottom = 0;
  p->nCenterX = 0;
  p->nCenterY = 0;
  p->pTown = NULL;
  p->flags.dwAll = 0;
}

// 0x493330 (286 B). Threads all 100 records onto the free list and clears
// them. The loop body must set pPrev before pNext.
__declspec(noinline) void TSaveNode<TScript,100>::Init()
{
  const int n = g_aiPools.towns;  // Hyperion: 100 in 1.17
  items[0].pPrev = NULL;
  items[0].pNext = &items[1];
  script_clear(&items[0]);
  for (int i = 1; i < n - 1; ++i) {
    items[i].pPrev = &items[i - 1];
    items[i].pNext = &items[i + 1];
    script_clear(&items[i]);
  }
  items[n - 1].pNext = NULL;
  items[n - 1].pPrev = &items[n - 2];
  script_clear(&items[n - 1]);
  pFree = items;
}

// 0x493450 (109 B). Arms a nuclear silo: creates the missile over the silo's
// own sprite and cross-links the pair. The positions go through WORD locals
// because the oracle loads both sprite words with `movzx` and only then
// sign-extends; reading the fields straight into the call gives one `movsx`.
// The owner is read into a local before the sprite, as DispatchCTFCopInit
// does. Read inline in the call, it leaves pUnit in ESI and wX in EDX.
void aiscript_nuke_create(CUnit *pUnit)
{
  CUnit *pNuke;
  CSprite *pSprite;
  upos_t wX;
  upos_t wY;
  BYTE bOwner;

  if (pUnit->wType != 0x6C)
    return;
  if (pUnit->pUnionD4 != NULL)
    return;
  bOwner = pUnit->bPlayer;
  pSprite = pUnit->pSprite;
  wY = pSprite->wPosY;
  wX = pSprite->wPosX;
  pNuke = CreateUnitAt(0x0E, PosS(wX), PosS(wY),
                       bOwner);
  if (pNuke == NULL)
    return;
  pNuke->FinishedMaking();
  pNuke->Hide();
  pUnit->pUnionD0 = pNuke;
  pUnit->pUnionD4 = reinterpret_cast<void *>(1);
  pNuke->pUnion80 = pUnit;
  register_unit_for_ai(pUnit, pNuke);
}

// 0x4934C0 (105 B). Turns the saved town ids and 1-based record ids back into
// pointers.
__declspec(noinline) void TSaveNode<TScript,100>::PostLoad()
{
  for (int i = 0; i < g_aiPools.towns; ++i) {  // Hyperion: 100 in 1.17
    items[i].pTown = town_ptr((int)(intptr_t)items[i].pTown);
    items[i].pNext = items[i].pNext == 0 ? 0 : (TScript *)((DWORD_PTR)items[i].pNext * sizeof(TScript) - sizeof(TScript) + (DWORD_PTR)this);
    items[i].pPrev = items[i].pPrev == 0 ? 0 : (TScript *)((DWORD_PTR)items[i].pPrev * sizeof(TScript) - sizeof(TScript) + (DWORD_PTR)this);
  }
  pFree = pFree == 0 ? 0 : (TScript *)((DWORD_PTR)pFree * sizeof(TScript) - sizeof(TScript) + (DWORD_PTR)this);
}

// 0x493530 (222 B). Clears the free records, then turns every record's
// pointers into ids. The town store goes through items[i] and the links
// through p: spelled alike, MSVC hoists the pNext load above the town store.
__declspec(noinline) void TSaveNode<TScript,100>::PreSave()
{
  for (TScript *p = pFree; p != 0; p = p->pNext)
    script_clear(p);
  for (int i = 0; i < g_aiPools.towns; ++i) {  // Hyperion: 100 in 1.17
    TScript *p = &items[i];
    items[i].pTown = (TTown *)(intptr_t)town_id(items[i].pTown);
    p->pNext = p->pNext == 0 ? 0 : (TScript *)(((DWORD_PTR)p->pNext - (DWORD_PTR)this) / sizeof(TScript) + 1);
    p->pPrev = p->pPrev == 0 ? 0 : (TScript *)(((DWORD_PTR)p->pPrev - (DWORD_PTR)this) / sizeof(TScript) + 1);
  }
  pFree = pFree == 0 ? 0 : (TScript *)(((DWORD_PTR)pFree - (DWORD_PTR)this) / sizeof(TScript) + 1);
}

// 0x493610 (133 B). Reads the pool and the manager back and turns the saved
// head index into a pointer. A save older than version 1 had its scripts
// 0xB0 bytes lower, so each live cursor is moved up.
int sai_read_script_data(void *pFile, WORD wVersion)
{
  // Hyperion: 1.17's pool whole, or a grown pool's records and head
  if (!AiPoolLoad<TScript, AI_TOWNS_117>(SAVE117_ScriptPool, SAVE117_Script, gScripts_g.items, g_aiPools.towns,
                                         &gScripts_g.pFree, pFile))
    return 0;
  gScripts_g.PostLoad();
  if (!PortSave117Decompress(SAVE117_ScriptMgr, &gScriptMgr_g, 1, pFile))
    return 0;
  TScript *pHead = gScriptMgr_g.pHead;
  gScriptMgr_g.pPool = &gScripts_g;
  if (pHead != NULL)
    pHead = &gScripts_g.items[reinterpret_cast<intptr_t>(pHead) - 1];
  gScriptMgr_g.pHead = pHead;
  if (wVersion == 0) {
    for (TScript *p = pHead; p != NULL; p = p->pNext)
      p->dwOffset += 0xB0;
  }
  return 1;
}

// 0x4936A0 (157 B). Writes the pool in its save form and the manager with the
// head as a 1-based index, then restores both.
int sai_write_script_data(void *pFile)
{
  gScripts_g.PreSave();
  // Hyperion: 1.17's pool whole, or a grown pool's records and head
  int nResult = AiPoolSave<TScript, AI_TOWNS_117>(SAVE117_ScriptPool, SAVE117_Script, gScripts_g.items,
                                                  g_aiPools.towns, gScripts_g.pFree, pFile);
  gScripts_g.PostLoad();
  TScript *pHead = gScriptMgr_g.pHead;
  DWORD_PTR nHead;
  if (pHead == NULL)
    nHead = 0;
  else
    nHead = (reinterpret_cast<DWORD_PTR>(pHead) - reinterpret_cast<DWORD_PTR>(gScriptMgr_g.pPool)) / sizeof(TScript) + 1;
  gScriptMgr_g.pHead = reinterpret_cast<TScript *>(nHead);
  gScriptMgr_g.pPool = NULL;
  nResult = PortSave117Compress(SAVE117_ScriptMgr, &gScriptMgr_g, 1, pFile) ? nResult : 0;
  gScriptMgr_g.pPool = &gScripts_g;
  DWORD nIndex = (DWORD)reinterpret_cast<DWORD_PTR>(gScriptMgr_g.pHead);
  if (nIndex == 0) {
    gScriptMgr_g.pHead = reinterpret_cast<TScript *>((DWORD_PTR)nIndex);
    return nResult;
  }
  gScriptMgr_g.pHead = &gScripts_g.items[nIndex - 1];
  return nResult;
}

// 0x493740 (71 B). The line numbers are the oracle's own, from the two
// `push 60Ah` / `push 60Fh` at the call sites; the file string at 0x4EFA4C is
// what names this TU.
void sai_scripts_free()
{
  if (gpAIScript_g != NULL) {
    mem_free(gpAIScript_g, "SAI_Scripts.cpp", 0x60A);
    gpAIScript_g = NULL;
  }
  if (gpBWScript_g != NULL) {
    mem_free(gpBWScript_g, "SAI_Scripts.cpp", 0x60F);
    gpBWScript_g = NULL;
  }
}

// 0x493790 (97 B). Posts a guard at every town centre that has none yet.
void sai_scripts_guard_resources(int nArg0, int nArg1)
{
  int nTown;

  if (sai_town_iter_begin(&nTown)) {
    do {
      int nX = sai_town_x(nTown);
      int nY = sai_town_y(nTown);

      if (!guard_post_near(nArg0, nX, nY))
        guard_create_post(nArg0, nArg1, nX, nY);
    } while (sai_town_iter_next(&nTown));
  }
}

// 0x493800 (89 B). Loads both script binaries and lays the pool out empty.
void sai_scripts_init()
{
  gpAIScript_g = static_cast<BYTE *>(load_game_data("scripts\\aiscript.bin", NULL, NULL, "SAI_Scripts.cpp", 0x600));
  gpBWScript_g = static_cast<BYTE *>(load_game_data("scripts\\bwscript.bin", NULL, NULL, "SAI_Scripts.cpp", 0x601));
  gScripts_g.Init();
  gScriptMgr_g.pPool = &gScripts_g;
  gScriptMgr_g.pHead = NULL;
}

// 0x493860 (116 B). Returns every thread belonging to one town to the free
// list. sai.h seats the parameter as `int`, which is what the cast below is;
// the field it is compared against is a TTown * on town_get_base_id's evidence.
void sai_scripts_remove_town(intptr_t nArg0)
{
  TScript *pScript = gScriptMgr_g.pHead;

  while (gScriptMgr_g.pHead != NULL && pScript != NULL) {
    TScript *pNext = pScript->pNext;

    if (pScript->pTown == reinterpret_cast<TTown *>(nArg0))
      script_free(pScript);
    pScript = pNext;
  }
}

// 0x4938E0 (139 B). Ticks every live thread. The wait is post-decremented, so
// the arm runs on the tick the old value reads zero.
void sai_scripts_update()
{
  TScript *pScript = gScriptMgr_g.pHead;

  while (gScriptMgr_g.pHead != NULL && pScript != NULL) {
    TScript *pNext = pScript->pNext;

    if (pScript->dwWait-- == 0) {
      if (pScript->flags.bStop)
        script_free(pScript);
      else
        SAI_Scripts_aiscript_progress(pScript);
    }
    pScript = pNext;
  }
}

// 0x493970 (103 B). Starts a script for a player. An id in stru_508F70 runs
// whatever the slot type is; otherwise the slot must be a computer, or a
// neutral running 'Rscu'.
void __fastcall run_aiscript(DWORD dwScriptId, int nPlayer,
                             const rect32_t *prcArea)
{
  int bBWScript;
  DWORD dwOffset;
  unsigned i;

  for (i = 0; stru_508F70[i] != 0; i++) {
    if (stru_508F70[i] == dwScriptId)
      goto start;
  }
  {
    BOOL bOk;
    BYTE nType = gPlayerData[nPlayer].nType;

    if (nType == 1)
      bOk = TRUE;
    else if (nType == 7)
      bOk = (dwScriptId == 0x75637352);
    else
      bOk = FALSE;
    if (!bOk)
      return;
  }
start:
  dwOffset = get_script_offset(dwScriptId, &bBWScript);
  if (dwOffset == 0)
    return;
  aiscript_new_thread(nPlayer, dwOffset, prcArea, bBWScript);
}

// The reader inlined by hand where the oracle inlines it; aiscript_get_byte is
// noinline so every other site stays a call.
static __forceinline BYTE script_byte(TScript *pScript)
{
  BYTE bValue;

  if (pScript->flags.bBWScript)
    bValue = gpBWScript_g[pScript->dwOffset];
  else
    bValue = gpAIScript_g[pScript->dwOffset];
  pScript->dwOffset++;
  return bValue;
}

static __forceinline WORD script_word(TScript *pScript)
{
  WORD wValue;

  if (pScript->flags.bBWScript)
    wValue = *reinterpret_cast<WORD *>(&gpBWScript_g[pScript->dwOffset]);
  else
    wValue = *reinterpret_cast<WORD *>(&gpAIScript_g[pScript->dwOffset]);
  pScript->dwOffset += 2;
  return wValue;
}
// Hyperion: a unit type a script names, for the script's player: a mod's race
// names what its AI makes and counts in place of a unit it goes without (ModRaceAiUnit); the
// three races' players get the type as it is.
static WORD script_unit(TScript *pScript, WORD wType)
{
  return static_cast<WORD>(ModRaceAiUnit(pScript->nPlayer, wType));
}
// And a tech or upgrade it names, which a mod's race may research as one of its own.
static WORD script_tech(TScript *pScript, WORD wTech)
{
  return static_cast<WORD>(ModRaceAiTech(pScript->nPlayer, wTech));
}
static WORD script_upgrade(TScript *pScript, WORD wUpgrade)
{
  return static_cast<WORD>(ModRaceAiUpgrade(pScript->nPlayer, wUpgrade));
}


// 0x493A60 (4151 B). Runs one thread's bytecode until an opcode waits or ends it.
// Source case order is the oracle's block order. The rect for AIFindNukeTargetInArea goes
// through pArea in top, bottom, left, right order, which also fixes the operand
// order of the fallback centre.
void SAI_Scripts_aiscript_progress(TScript *pScript)
{
  for (;;) {
    BYTE bOp;

    if (pScript->flags.bBWScript)
      bOp = gpBWScript_g[pScript->dwOffset];
    else
      bOp = gpAIScript_g[pScript->dwOffset];
    pScript->dwOffset++;

    switch (bOp) {
    case AISOP_WAIT_SECURE:
      if (banker_plan_is_secure(pScript->nPlayer))
        continue;
      pScript->dwOffset--;
      pScript->dwWait = 30;
      return;

    case AISOP_WAIT_BUILD:
    case AISOP_WAIT_BUILDSTART: {
      BYTE bCount;
      WORD wType;

      if (pScript->flags.bBWScript)
        bCount = gpBWScript_g[pScript->dwOffset];
      else
        bCount = gpAIScript_g[pScript->dwOffset];
      pScript->dwOffset++;
      wType = script_unit(pScript, aiscript_get_word(pScript));
      if (wType == get_player_worker_type(pScript->nPlayer)) {
        intptr_t nBase = town_get_base_id(pScript->pTown);
        if (nBase != 0) {
          unsigned nMax = sai_town_worker_target(reinterpret_cast<void *>(nBase), pScript->nPlayer);
          if (bCount > nMax)
            bCount = static_cast<BYTE>(nMax);
        }
      }
      if (static_cast<unsigned>(town_unit_count(pScript->pTown, wType, bOp == 9)) >= bCount)
        continue;
      pScript->dwOffset -= 4;
      pScript->dwWait = 30;
      return;
    }

    case AISOP_WAIT_TRAIN: {
      BYTE bCount = aiscript_get_byte(pScript);
      WORD wType = script_unit(pScript, aiscript_get_word(pScript));
      if (static_cast<unsigned>(aiscript_count_units(wType, pScript->nPlayer, 1)) >= bCount)
        continue;
      pScript->dwOffset -= 4;
      pScript->dwWait = 30;
      return;
    }

    case AISOP_WAIT_FORCE:
    case AISOP_DO_MORPH:
    case AISOP_TRAIN: {
      BYTE bCount;
      WORD wType;

      bCount = script_byte(pScript);
      wType = script_unit(pScript, script_word(pScript));
      if (static_cast<unsigned>(aiscript_count_units(wType, pScript->nPlayer, bOp != 0x4C)) >= bCount)
        continue;
      banker_set_pending_type(pScript->nPlayer, wType);
      if (bOp == 0x46)
        continue;
      pScript->dwOffset -= 4;
      pScript->dwWait = 30;
      return;
    }

    case AISOP_ATTACK_DO:
      if (!AiAttackAllowed(pScript->nPlayer))  // Hyperion: not yet at its level
        continue;
      if (TAI_Captain_aiscript_attack(pScript->nPlayer, pScript->nCenterX, pScript->nCenterY, 1, 0))
        continue;
      pScript->dwOffset--;
      pScript->dwWait = 30;
      return;

    case AISOP_WAIT_FINISHATTACK:
      if (!tai_captain_any_attack_job_active(pScript->nPlayer))
        continue;
      pScript->dwOffset--;
      pScript->dwWait = 30;
      return;

    case AISOP_WAIT_UPGRADES:
      if (aiscript_has_upgrades(pScript->nPlayer))
        continue;
      pScript->dwOffset--;
      pScript->dwWait = 30;
      return;

    case AISOP_WAIT_BUNKERS:
      if (aiscript_wait_bunkers(pScript->nPlayer))
        continue;
      pScript->dwOffset--;
      pScript->dwWait = 30;
      return;

    case AISOP_WAIT_TURRETS:
      if (aiscript_wait_turrets(pScript->nPlayer))
        continue;
      pScript->dwOffset--;
      pScript->dwWait = 30;
      return;

    case AISOP_HARASS_FACTOR:
      aiscript_harass_factor(pScript->nPlayer, aiscript_get_word(pScript));
      break;

    case AISOP_GOTO:
      pScript->dwOffset = aiscript_get_word(pScript);
      break;

    case AISOP_EVAL_HARASS: {
      WORD wJump = aiscript_get_word(pScript);
      if (!aiscript_eval_harass(pScript->nPlayer, pScript->nCenterX, pScript->nCenterY))
        pScript->dwOffset = wJump;
      break;
    }

    case AISOP_GROUNDMAP_JUMP: {
      WORD wJump = aiscript_get_word(pScript);
      if (aiscript_is_groundmap(pScript->nPlayer, pScript->nCenterX, pScript->nCenterY))
        pScript->dwOffset = wJump;
      break;
    }

    case AISOP_RANDOM_JUMP: {
      BYTE bChance = aiscript_get_byte(pScript);
      WORD wJump = aiscript_get_word(pScript);
      if (static_cast<DWORD>(net_rand(0x34) & 0xFF) <= bChance)
        pScript->dwOffset = wJump;
      break;
    }

    case AISOP_TIME_JUMP: {
      DWORD dwTime = aiscript_get_byte(pScript) * 60;
      WORD wJump = aiscript_get_word(pScript);
      if (g_dwElapsedTime >= dwTime)
        pScript->dwOffset = wJump;
      break;
    }

    case AISOP_NOTOWNS_JUMP: {
      WORD wType = script_unit(pScript, aiscript_get_word(pScript));
      WORD wJump = aiscript_get_word(pScript);
      if (!count_get_existence(wType, static_cast<BYTE>(pScript->nPlayer)))
        pScript->dwOffset = wJump;
      break;
    }

    case AISOP_IF_OWNED: {
      WORD wType = script_unit(pScript, aiscript_get_word(pScript));
      WORD wJump = aiscript_get_word(pScript);
      if (count_get_existence(wType, static_cast<BYTE>(pScript->nPlayer)))
        pScript->dwOffset = wJump;
      break;
    }

    case AISOP_RACE_JUMP: {
      DWORD dwTerran = aiscript_get_word(pScript);
      DWORD dwZerg = aiscript_get_word(pScript);
      DWORD dwProtoss = aiscript_get_word(pScript);
      switch (get_town_race(pScript->pTown)) {
      case 0:
        pScript->dwOffset = dwZerg;
        break;
      case 1:
        pScript->dwOffset = dwTerran;
        break;
      case 2:
        pScript->dwOffset = dwProtoss;
        break;
      }
      break;
    }

    case AISOP_REGION_SIZE: {
      DWORD dwSize = aiscript_get_byte(pScript) << 5;
      DWORD dwJump = aiscript_get_word(pScript);
      if (static_cast<DWORD>(aiscript_get_region_size(pScript->pTown, dwSize)) < dwSize)
        pScript->dwOffset = dwJump;
      break;
    }

    case AISOP_EXPAND: {
      DWORD dwAmount = aiscript_get_byte(pScript);
      DWORD dwJump = aiscript_get_word(pScript);
      if (static_cast<DWORD>(town_count(pScript->nPlayer)) <= dwAmount)
        aiscript_expand(pScript, dwJump, dwAmount);
      break;
    }

    case AISOP_GET_OLDPEONS:
      SAI_Build_aiscript_get_old_peons(pScript->pTown, aiscript_get_byte(pScript));
      break;

    case AISOP_START_TOWN:
    case AISOP_START_AREATOWN:
      if (bOp == 3)
        pScript->pTown = town_from_point(pScript->nPlayer, pScript->nCenterX, pScript->nCenterY, pScript->flags.nTown);
      else
        pScript->pTown = town_from_area(pScript->nPlayer, &pScript->rcArea, pScript->flags.nTown);
      if (pScript->pTown == NULL)
        goto stop;
      banker_start_nation(pScript->nPlayer);
      aiscript_reset_area_captains(pScript->nPlayer);
      break;

    case AISOP_ATTACK_CLEAR:
      aiscript_attack_clear(pScript->nPlayer, 1);
      break;

    case AISOP_TRANSPORTS_OFF:
      aiscript_transports(pScript->nPlayer, 0);
      break;

    case AISOP_CHECK_TRANSPORTS:
      aiscript_transports(pScript->nPlayer, 1);
      break;

    case AISOP_FARMS_NOTIMING:
      aiscript_set_farms_timing(pScript->nPlayer, 0);
      break;

    case AISOP_FARMS_TIMING:
      aiscript_set_farms_timing(pScript->nPlayer, 1);
      break;

    case AISOP_ATTACK_ADD: {
      // Hyperion: a computer's level sizes its attacks (port_ai.h)
      BYTE bCount = AiAttackCount(pScript->nPlayer, aiscript_get_byte(pScript));
      set_harass(pScript->nPlayer, bCount, script_unit(pScript, aiscript_get_word(pScript)));
      break;
    }

    case AISOP_QUICK_ATTACK:
      if (!AiAttackAllowed(pScript->nPlayer))  // Hyperion: not yet at its level
        break;
      TAI_Captain_aiscript_attack(pScript->nPlayer, pScript->nCenterX, pScript->nCenterY, 1, 0);
      aiscript_quick_attack(pScript->nPlayer, 5);
      break;

    case AISOP_ATTACK_PREPARE:
      if (!AiAttackAllowed(pScript->nPlayer))  // Hyperion: not yet at its level
        break;
      TAI_Captain_aiscript_attack(pScript->nPlayer, pScript->nCenterX, pScript->nCenterY, 1, 0);
      break;

    case AISOP_CAPT_EXPAND:
      TAI_Captain_aiscript_capt_expand(pScript->nPlayer, 0);
      break;

    case AISOP_BUILD: {
      BYTE bCount = aiscript_get_byte(pScript);
      WORD wType = script_unit(pScript, aiscript_get_word(pScript));
      BYTE bPrio = aiscript_get_byte(pScript);
      town_need(pScript->pTown, bCount, wType, bPrio, 0);
      break;
    }

    case AISOP_PLAYER_NEED: {
      BYTE bCount = aiscript_get_byte(pScript);
      WORD wType = script_unit(pScript, aiscript_get_word(pScript));
      town_need(pScript->pTown, bCount, wType, 0x50, 1);
      break;
    }

    case AISOP_UPGRADE: {
      BYTE bLevel = aiscript_get_byte(pScript);
      WORD wUpgrade = script_upgrade(pScript, aiscript_get_word(pScript));
      BYTE bPrio = aiscript_get_byte(pScript);
      aiscript_upgrade(pScript->pTown, bLevel, wUpgrade, bPrio);
      break;
    }

    case AISOP_CREEP:
      aiscript_creep(pScript->nPlayer, aiscript_get_byte(pScript));
      break;

    case AISOP_TECH: {
      WORD wTech = script_tech(pScript, aiscript_get_word(pScript));
      aiscript_tech(pScript->pTown, wTech, aiscript_get_byte(pScript));
      break;
    }

    case AISOP_DEFENSECLEAR_GG:
      aiscript_defenseclear_gg(pScript->nPlayer);
      break;

    case AISOP_DEFENSECLEAR_AG:
      aiscript_defenseclear_ag(pScript->nPlayer);
      break;

    case AISOP_DEFENSECLEAR_GA:
      aiscript_defenseclear_ga(pScript->nPlayer);
      break;

    case AISOP_DEFENSECLEAR_AA:
      aiscript_defenseclear_aa(pScript->nPlayer);
      break;

    case AISOP_DEFENSEBUILD_GG: {
      BYTE bCount = aiscript_get_byte(pScript);
      aiscript_defensebuild_gg(pScript->nPlayer, bCount, script_unit(pScript, aiscript_get_word(pScript)));
      break;
    }

    case AISOP_DEFENSEBUILD_AG: {
      BYTE bCount = aiscript_get_byte(pScript);
      aiscript_defensebuild_ag(pScript->nPlayer, bCount, script_unit(pScript, aiscript_get_word(pScript)));
      break;
    }

    case AISOP_DEFENSEBUILD_GA: {
      BYTE bCount = aiscript_get_byte(pScript);
      aiscript_defensebuild_ga(pScript->nPlayer, bCount, script_unit(pScript, aiscript_get_word(pScript)));
      break;
    }

    case AISOP_DEFENSEBUILD_AA: {
      BYTE bCount = aiscript_get_byte(pScript);
      aiscript_defensebuild_aa(pScript->nPlayer, bCount, script_unit(pScript, aiscript_get_word(pScript)));
      break;
    }

    case AISOP_DEFENSEUSE_GG: {
      BYTE bCount = aiscript_get_byte(pScript);
      aiscript_defenseuse_gg(pScript->nPlayer, bCount, script_unit(pScript, aiscript_get_word(pScript)));
      break;
    }

    case AISOP_DEFENSEUSE_AG: {
      BYTE bCount = aiscript_get_byte(pScript);
      aiscript_defenseuse_ag(pScript->nPlayer, bCount, script_unit(pScript, aiscript_get_word(pScript)));
      break;
    }

    case AISOP_DEFENSEUSE_GA: {
      BYTE bCount = aiscript_get_byte(pScript);
      aiscript_defenseuse_ga(pScript->nPlayer, bCount, script_unit(pScript, aiscript_get_word(pScript)));
      break;
    }

    case AISOP_DEFENSEUSE_AA: {
      BYTE bCount = aiscript_get_byte(pScript);
      aiscript_defenseuse_aa(pScript->nPlayer, bCount, script_unit(pScript, aiscript_get_word(pScript)));
      break;
    }

    case AISOP_BUILD_BUNKERS:
      TAI_Captain_aiscript_build_bunkers(pScript->nPlayer);
      break;

    case AISOP_BUILD_TURRETS:
      TAI_Captain_aiscript_build_turrets(pScript->nPlayer);
      break;

    case AISOP_SEND_SUICIDE: {
      BYTE bAll = aiscript_get_byte(pScript);
      aiscript_attack_clear(pScript->nPlayer, 1);
      tai_send_everything_to_attack(pScript->nPlayer, bAll);
      aiscript_quick_attack(pScript->nPlayer, 5);
      break;
    }

    case AISOP_PLAYER_ENEMY:
      aiscript_enemy(pScript->nPlayer, reinterpret_cast<intptr_t>(&pScript->rcArea));
      break;

    case AISOP_PLAYER_ALLY:
      aiscript_ally(pScript->nPlayer, reinterpret_cast<intptr_t>(&pScript->rcArea));
      break;

    case AISOP_DEFAULT_MIN:
      aiscript_defaultmin(pScript->nPlayer, aiscript_get_byte(pScript));
      break;

    case AISOP_DEFAULTBUILD_OFF:
      aiscript_defaultbuild_off(pScript->nPlayer);
      break;

    case AISOP_DEFAULT_BUILD:
      aiscript_default_build(pScript->nPlayer);
      break;

    case AISOP_MOVE_DT:
      aiscript_move_dt(pScript->nPlayer, pScript->nCenterX, pScript->nCenterY);
      break;

    case AISOP_SWITCH_RESCUE:
      aiscript_switch_rescue(pScript->nPlayer);
      break;

    case AISOP_ENTER_BUNKER:
      aiscript_enter_bunker(pScript->nPlayer, &pScript->rcArea);
      break;

    case AISOP_SET_GENCMD:
      aiscript_set_gencmd(pScript->nPlayer, reinterpret_cast<const AI_GenCmd *>(&pScript->rcArea));
      break;

    case AISOP_MAKE_PATROL:
      aiscript_make_patrol(pScript->nPlayer, &pScript->rcArea);
      break;

    case AISOP_VALUE_AREA:
      TAI_Captain_aiscript_value_area(pScript->nPlayer, pScript->nCenterX, pScript->nCenterY);
      break;

    case AISOP_MAX_FORCE:
      banker_set_max_force(pScript->nPlayer, aiscript_get_word(pScript));
      break;

    case AISOP_NUKE_RATE:
      aiscript_nuke_rate(pScript->nPlayer, aiscript_get_byte(pScript));
      break;

    case AISOP_FAKE_NUKE:
      aiscript_fake_nuke(pScript->nPlayer);
      break;

    case AISOP_CLEAR_COMBATDATA:
      TAI_Captain_aiscript_clear_combat_data(pScript->nPlayer, &pScript->rcArea);
      break;

    case AISOP_START_CAMPAIGN:
      aiscript_start_campaign(pScript->nPlayer);
      break;

    case AISOP_PLACE_GUARD: {
      WORD wType = script_unit(pScript, aiscript_get_word(pScript));
      TAI_GuardPost_aiscript_place_guard(pScript->nPlayer, reinterpret_cast<intptr_t>(pScript->pTown), wType, aiscript_get_byte(pScript));
      break;
    }

    case AISOP_GUARD_RESOURCES:
      sai_scripts_guard_resources(pScript->nPlayer, script_unit(pScript, aiscript_get_word(pScript)));
      break;

    case AISOP_CALL: {
      WORD wJump = aiscript_get_word(pScript);
      ret_addr = pScript->dwOffset;
      pScript->dwOffset = wJump;
      break;
    }

    case AISOP_RETURN:
      pScript->dwOffset = ret_addr;
      ret_addr = 0;
      break;

    case AISOP_PANIC:
      aiscript_set_panic_block(pScript->nPlayer, aiscript_get_word(pScript));
      break;

    case AISOP_MULTIRUN: {
      WORD wStart = aiscript_get_word(pScript);
      TScript *pThread = aiscript_new_thread(pScript->nPlayer, wStart, &pScript->rcArea, pScript->flags.bBWScript);
      if (pThread != NULL)
        pThread->pTown = pScript->pTown;
      break;
    }

    case AISOP_KILLABLE:
      pScript->flags.bFlag2 = 1;
      break;

    case AISOP_KILL_THREAD: {
      for (TScript *p = gScriptMgr_g.pHead; p != NULL; p = p->pNext) {
        if (p->flags.bFlag2)
          p->flags.bStop = 1;
      }
      break;
    }

    case AISOP_RUSH: {
      BYTE bCase = aiscript_get_byte(pScript);
      WORD wJump = aiscript_get_word(pScript);
      if (!TAI_Captain_aiscript_should_rush(pScript->nPlayer, bCase, pScript->nCenterX, pScript->nCenterY))
        pScript->dwOffset = wJump;
      break;
    }

    case AISOP_SCOUT_WITH:
      aiscript_get_word(pScript);
      break;

    case AISOP_DEFINE_MAX: {
      BYTE bMax = aiscript_get_byte(pScript);
      aiscript_define_max(pScript->nPlayer, bMax, script_unit(pScript, aiscript_get_word(pScript)));
      break;
    }

    case AISOP_TARGET_EXPANSION:
      aiscript_target_expansion(pScript->nPlayer);
      break;

    case AISOP_SET_ATTACKS:
      aiscript_set_attacks(pScript->nPlayer, aiscript_get_byte(pScript));
      break;

    case AISOP_GIVE_MONEY:
      aiscript_give_money(pScript->nPlayer);
      break;

    case AISOP_DEBUG: {
      WORD wJump = aiscript_get_word(pScript);
      wrap_map_msg(&gpAIScript_g[pScript->dwOffset], pScript->nPlayer, 0);
      pScript->dwOffset = wJump;
      break;
    }

    case AISOP_RESOURCES_JUMP: {
      WORD wOre = aiscript_get_word(pScript);
      WORD wGas = aiscript_get_word(pScript);
      WORD wJump = aiscript_get_word(pScript);
      if (gPlayerMinerals[pScript->nPlayer] >= wOre && gPlayerGas[pScript->nPlayer] >= wGas)
        pScript->dwOffset = wJump;
      break;
    }

    case AISOP_PREP_DOWN: {
      BYTE bLess = aiscript_get_byte(pScript);
      BYTE bMin = aiscript_get_byte(pScript);
      int nType = script_unit(pScript, aiscript_get_word(pScript));
      int nPlayer = pScript->nPlayer;
      int nCount = count_get_existence(nType, static_cast<BYTE>(nPlayer));
      if (nType == 0x1E)
        nCount += count_get_existence(5, static_cast<BYTE>(nPlayer));
      else if (nType == 5)
        nCount += count_get_existence(0x1E, static_cast<BYTE>(nPlayer));
      nCount -= bLess;
      set_harass(pScript->nPlayer, nCount > bMin ? nCount : bMin, nType);
      break;
    }

    case AISOP_ENTER_TRANSPORT:
      aiscript_enter_transport(pScript->nPlayer, &pScript->rcArea);
      break;

    case AISOP_EXIT_TRANSPORT:
      aiscript_exit_transport(pScript->nPlayer, &pScript->rcArea);
      break;

    case AISOP_SHAREDVISION_ON: {
      BYTE bOther = aiscript_get_byte(pScript);
      aiscript_sharedvision(pScript->nPlayer, bOther, 1);
      break;
    }

    case AISOP_SHAREDVISION_OFF: {
      BYTE bOther = aiscript_get_byte(pScript);
      aiscript_sharedvision(pScript->nPlayer, bOther, 0);
      break;
    }

    case AISOP_NUKE_LOCATION: {
      CUnit *pGhost = aiscript_find_caster(pScript->nPlayer, 1, 0);
      if (pGhost == NULL)
        break;
      CUnit *pSilo = pGhost->FindArmedSilo(1, pGhost);
      if (pSilo == NULL)
        break;
      rect32_t *pArea = &pScript->rcArea;
      mrect_t  rc;
      rc.top = PosS(pArea->top);
      rc.bottom = PosS(pArea->bottom);
      rc.left = PosS(pArea->left);
      rc.right = PosS(pArea->right);
      CUnit *pTarget = AIFindNukeTargetInArea(pGhost, rc);
      if (pTarget != NULL) {
        pGhost->SetOrder_CUnit(0x7E, pTarget);
        pSilo->pUnionD0->pUnion80 = pGhost;
      } else {
        pGhost->SetOrder_xy(0x7E, (pScript->rcArea.left + pScript->rcArea.right) / 2,
                            (pScript->rcArea.bottom + pScript->rcArea.top) / 2);
        pSilo->pUnionD0->pUnion80 = pGhost;
      }
      break;
    }

    case AISOP_DISRUPTION_WEB: {
      CUnit *pUnit = aiscript_find_caster(pScript->nPlayer, 0x3C, techdata_energy_g[0x19]);
      if (pUnit != NULL)
        pUnit->SetOrder_xy(0xB5, pScript->rcArea.left + (pScript->rcArea.right - pScript->rcArea.left) / 2,
                           pScript->rcArea.top + (pScript->rcArea.bottom - pScript->rcArea.top) / 2);
      break;
    }

    case AISOP_RECALL_LOCATION: {
      CUnit *pUnit = aiscript_find_caster(pScript->nPlayer, 0x47, techdata_energy_g[0x15]);
      if (pUnit != NULL)
        pUnit->SetOrder_xy(0x89, pScript->rcArea.left + (pScript->rcArea.right - pScript->rcArea.left) / 2,
                           pScript->rcArea.top + (pScript->rcArea.bottom - pScript->rcArea.top) / 2);
      break;
    }

    case AISOP_JUNKYARD_DOG: {
      mrect_t  rc;
      rc.top = PosS(pScript->rcArea.top);
      rc.bottom = PosS(pScript->rcArea.bottom);
      rc.left = PosS(pScript->rcArea.left);
      rc.right = PosS(pScript->rcArea.right);
      aiscript_assign_all_order(pScript->nPlayer, 0xBB, rc);
      break;
    }

    case AISOP_IMPLODE:
      TAI_Captain_aiscript_implode(pScript->nPlayer);
      break;

    case AISOP_GUARD_ALL:
      for (CUnit *pUnit = CUnit::GetPlayerUnitList(pScript->nPlayer); pUnit != NULL; pUnit = pUnit->pPlayerUnitNext)
        TAI_Captain_aiscript_guard(pScript->nPlayer, pUnit);
      break;

    case AISOP_ENEMYOWNS_JUMP: {
      WORD wType = aiscript_get_word(pScript);
      WORD wJump = aiscript_get_word(pScript);
      for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
        if (pUnit->wType == wType && IsEnemyUnit(pScript->nPlayer, pUnit)) {
          pScript->dwOffset = wJump;
          break;
        }
      }
      break;
    }

    case AISOP_ENEMYRESOURCES_JUMP: {
      WORD wOre = aiscript_get_word(pScript);
      WORD wGas = aiscript_get_word(pScript);
      WORD wJump = aiscript_get_word(pScript);
      CUnit *pUnit;
      for (pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
        if (IsEnemyUnit(pScript->nPlayer, pUnit))
          break;
      }
      if (pUnit == NULL)
        break;
      if (gPlayerMinerals[pUnit->bPlayer] >= wOre && gPlayerGas[pUnit->bPlayer] >= wGas)
        pScript->dwOffset = wJump;
      break;
    }

    case AISOP_IF_DIF: {
      BYTE bMore = aiscript_get_byte(pScript);
      BYTE bValue = aiscript_get_byte(pScript);
      WORD wJump = aiscript_get_word(pScript);
      if (bMore == 0) {
        if (static_cast<unsigned>(aiscript_get_difficulty(pScript->nPlayer)) < bValue)
          pScript->dwOffset = wJump;
      } else {
        if (static_cast<unsigned>(aiscript_get_difficulty(pScript->nPlayer)) > bValue)
          pScript->dwOffset = wJump;
      }
      break;
    }

    case AISOP_EASY_ATTACK: {
      BYTE bCount = aiscript_get_byte(pScript);
      WORD wType = aiscript_get_word(pScript);
      if (aiscript_get_difficulty(pScript->nPlayer) == 0)
        set_harass(pScript->nPlayer, bCount, wType);
      break;
    }

    case AISOP_SET_RANDOMSEED: {
      DWORD dwSeed;
      if (pScript->flags.bBWScript)
        dwSeed = *reinterpret_cast<DWORD *>(&gpBWScript_g[pScript->dwOffset]);
      else
        dwSeed = *reinterpret_cast<DWORD *>(&gpAIScript_g[pScript->dwOffset]);
      pScript->dwOffset += 4;
      net_set_fixed_seed(dwSeed);
      break;
    }

    case AISOP_CREATE_NUKE:
      for (CUnit *pUnit = CUnit::GetPlayerUnitList(pScript->nPlayer); pUnit != NULL; pUnit = pUnit->pPlayerUnitNext) {
        if (pUnit->wType == 0x6C && pUnit->pUnionD4 == NULL) {
          aiscript_nuke_create(pUnit);
          break;
        }
      }
      break;

    case AISOP_CREATE_UNIT: {
      WORD wType = aiscript_get_word(pScript);
      WORD wX = aiscript_get_word(pScript);
      WORD wY = aiscript_get_word(pScript);
      CUnit *pUnit = trigger_create_unit(wType, wX, wY, pScript->nPlayer);
      if (pUnit == NULL)
        break;
      BYTE bTech = pUnit->GetCloakingTech();
      if (bTech == 0x2C)
        break;
      if (TechUseAllowed(bTech, pUnit->bPlayer, pUnit) == 1)
        pUnit->sub_41D410();
      break;
    }

    case AISOP_NUKE_POS: {
      DWORD dwX = aiscript_get_word(pScript);
      DWORD dwY = aiscript_get_word(pScript);
      CUnit *pGhost = aiscript_find_caster(pScript->nPlayer, 1, 0);
      if (pGhost == NULL)
        break;
      CUnit *pSilo = pGhost->FindArmedSilo(1, pGhost);
      if (pSilo == NULL)
        break;
      pGhost->SetOrder_xy(0x7E, dwX, dwY);
      pSilo->pUnionD0->pUnion80 = pGhost;
      break;
    }

    case AISOP_HELP_IFTROUBLE:
      aiscript_help_iftrouble(pScript->nPlayer);
      break;

    case AISOP_ALLIES_WATCH: {
      BYTE bTown = aiscript_get_byte(pScript);
      WORD wJump = aiscript_get_word(pScript);
      if (sai_townbuilder_claim(bTown))
        aiscript_start_expansion(pScript, bTown, wJump);
      break;
    }

    case AISOP_TRY_TOWNPOINT: {
      BYTE bValue = aiscript_get_byte(pScript);
      WORD wJump = aiscript_get_word(pScript);
      if (town_count(pScript->nPlayer) == bValue)
        pScript->dwOffset = wJump;
      break;
    }

    case AISOP_FATAL_ERROR:
      appcritf("Illegal AI script executed.");
      break;

    case AISOP_WAIT:
      // Hyperion: a computer's level paces its script (port_ai.h)
      pScript->dwWait = AiScriptWait(pScript->nPlayer, aiscript_get_word(pScript));
      return;

    case AISOP_STOP:
    stop:
      gScriptMgr_g.Free(pScript);
      return;

    case AISOP_HARASS_LOCATION:
      break;

    default:
      script_free(pScript);
      return;
    }
  }
}

// 0x494C60 (178 B). The panic request is a whole TScript built on the stack;
// pNext and pPrev are left alone because aiscript_start_expansion never reads them.
void aiscript_run_panic(int nPlayer, int nX, int nY, TTown *pTown)
{
  TScript sScript;
  int nBlock;
  int nBase;
  int nTown;

  nBlock = get_panic_block(nPlayer);
  if (nBlock == 0)
    return;
  sScript.dwOffset = 0;
  sScript.dwWait = 0;
  sScript.rcArea.left = 0;
  sScript.rcArea.right = 0;
  sScript.rcArea.top = 0;
  sScript.rcArea.bottom = 0;
  sScript.flags.dwAll = 0;
  sScript.nPlayer = nPlayer;
  sScript.nCenterX = nX;
  sScript.nCenterY = nY;
  sScript.pTown = pTown;
  if (gPlayerMinerals[nPlayer] >= 5000 && gPlayerGas[nPlayer] >= 3000
      && !aiscript_has_campaign(nPlayer))
    return;
  nBase = town_get_base_id(pTown);
  if (nBase == 0)
    return;
  nTown = sai_townbuilder_choose(nPlayer, nBase, 2);
  if (nTown == 0)
    return;
  aiscript_start_expansion(&sScript, nTown, nBlock);
}

// 0x494D20 (105 B). The AI table is searched first; a hit with a zero offset
// falls through to the BW table exactly as a miss does.
DWORD get_script_offset(DWORD dwScriptId, int *pbBWScript)
{
  BYTE *pBase = gpAIScript_g;
  const TScriptEntryAI *pAI;
  const TScriptEntryBW *pBW;

  *pbBWScript = 0;
  pAI = reinterpret_cast<const TScriptEntryAI *>(
      pBase + *reinterpret_cast<DWORD *>(pBase));
  while (pAI->dwId != 0) {
    if (pAI->dwId == dwScriptId) {
      if (pAI->dwOffset != 0)
        return pAI->dwOffset;
      break;
    }
    pAI++;
  }
  pBW = reinterpret_cast<const TScriptEntryBW *>(
      gpBWScript_g + *reinterpret_cast<DWORD *>(gpBWScript_g));
  while (pBW->dwId != 0) {
    if (pBW->dwId == dwScriptId) {
      DWORD dwOffset = pBW->dwOffset;

      if (dwOffset != 0) {
        *pbBWScript = 1;
        return dwOffset;
      }
      break;
    }
    pBW++;
  }
  return 0;
}

// 0x494D90 (230 B). The two null tests are the pool pop's guard and the link
// step's own, and MSVC threads both to the single `xor eax,eax` exit.
TScript *aiscript_new_thread(int nPlayer, DWORD dwOffset,
                             const rect32_t *prcArea, int bBWScript)
{
  TScript *pScript = gScriptMgr_g.pPool->pFree;

  // Guard the memory, not pScript: testing the local lets MSVC thread this
  // edge into script_link_head's null test, losing a `test esi, esi`.
  if (gScriptMgr_g.pPool->pFree != NULL) {
    TScript *pNext = pScript->pNext;

    gScriptMgr_g.pPool->pFree = pNext;
    if (pNext != NULL)
      pNext->pPrev = NULL;
  }
  script_link_head(pScript);
  if (pScript == NULL)
    return NULL;
  pScript->flags.bBWScript = (bBWScript != 0);
  pScript->dwOffset = dwOffset;
  pScript->dwWait = 0;
  pScript->nPlayer = nPlayer;
  if (prcArea != NULL) {
    pScript->rcArea = *prcArea;
    pScript->nCenterX = prcArea->left + (prcArea->right - prcArea->left) / 2;
    pScript->nCenterY = prcArea->top + (prcArea->bottom - prcArea->top) / 2;
  } else {
    memset(&pScript->rcArea, 0, sizeof(pScript->rcArea));
    pScript->nCenterX = 0;
    pScript->nCenterY = 0;
  }
  pScript->flags.nTown = 0;
  pScript->pTown = NULL;
  return pScript;
}

// 0x494E80 (159 B). The pushed 0 belongs to tai_captain_claim_defence_region, not to GetRegion_xy:
// MSVC pushes the constant argument before it calls the function that produces
// the register one.
void aiscript_start_expansion(TScript *pScript, int nTown, int nArg2)
{
  rect32_t rcArea;
  TScript *pNew;
  int nX;
  int nY;
  nX = sai_town_x(nTown);
  nY = sai_town_y(nTown);
  rcArea.top = nY - 1;
  rcArea.bottom = nY + 1;
  rcArea.left = nX - 1;
  rcArea.right = nX + 1;
  pNew = aiscript_new_thread(pScript->nPlayer, nArg2, &rcArea,
                             pScript->flags.bBWScript);
  if (pNew == NULL)
    return;
  pNew->flags.nTown = nTown;
  tai_captain_claim_defence_region(pScript->nPlayer, GetRegion_xy(nX, nY), 0);
  if (aiscript_has_campaign(pScript->nPlayer))
    return;
  tai_captain_defend_paths_to_enemies(pScript->nPlayer, nX, nY);
}

// 0x494F20 (92 B). The banked-resources trigger.
void aiscript_expand(TScript *pScript, int nArg1, int nArg2)
{
  int nBase;
  int nTown;

  if (gPlayerMinerals[pScript->nPlayer] >= 5000
      && gPlayerGas[pScript->nPlayer] >= 3000
      && !aiscript_has_campaign(pScript->nPlayer))
    return;
  nBase = town_get_base_id(pScript->pTown);
  if (nBase == 0)
    return;
  nTown = sai_townbuilder_choose(pScript->nPlayer, nBase, nArg2);
  if (nTown == 0)
    return;
  aiscript_start_expansion(pScript, nTown, nArg1);
}

// 0x494F80 (50 B). Re-runs every area captain for one computer player.
void aiscript_reset_area_captains(int nPlayer)
{
  if (gPlayerData[nPlayer].nType == 1) {
    const unsigned nAreas = NumAreas();

    for (unsigned i = 0; i < nAreas; i++)
      tai_captain_service(nPlayer, i);
  }
}
