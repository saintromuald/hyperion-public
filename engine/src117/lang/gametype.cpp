// gametype.cpp -- the game-type and map-authentication predicates over
// gGameHeader, and the template list of selectable game types. Functions are in
// ascending oracle VA order.
//
// The byte "globals" the predicates read are fields of one object, gGameHeader
// @0x6728D0 (src117/h/gametype.h). The template list is a Storm TSList whose
// terminator is a complemented address, not NULL: read src117/h/TSList.h before
// touching a walk here, since a loop written `p != NULL` runs off the end.
#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../port/gameplay/port_ai.h"     // Hyperion: AiMeleeScript
#include "../h/mutators.h"  // Hyperion: the start resources
#include "gametype.h"
#include "gluBNCreate.h"     // gametype_find_template, IsSimpleTemplate
#include "net_misc.h"        // IsBNetGame
#include "net_bnet_msg.h"    // SNETEVENT
#include "Teams.h"           // IsTeamGame, GetNumTeams, IsFullPlayerTeam, IsComputerTeam
#include "net_slot.h"        // is_race_selectable
#include "gds_local.h"       // psnprintf
#include "glues.h"           // glues_get_str
#include "SAI_Banker.h"      // aiscript_start_campaign
#include "gds_appcrit.h"     // appcritf
#include <string.h>          // strrchr
#include "sai_TownBuilder.h"  // ai_get_build_location_near -- its oracle TU is sai_TownBuilder.cpp
#include "net_cmds.h"           // PlayerData -- gPlayerData indexes it
#include "CUnit.h"              // CUnit, GetPlayerUnitList, FinishedMaking
#include "TrigAct.h"            // CreateUnitAt
#include "dialog.h"             // point16_t
#include "rect32.h"             // run_aiscript's 16-byte area argument
#include "netdesc.h"             // gamedesc_t
#include "Storm.h"              // SMemAlloc / SMemFree / SStrCopy
#include "rtti.h"               // type_info::raw_name, this tree's, not <typeinfo>
#include "../gen/globals117.h"  // gPlayerData, gPlayback, gbMultiPlayerMode,
                                // gulUnitTypeFlags

// gGameHeader and the template list, dual-spelled. Both are structs, so they are
// declared here rather than in gen/globals117.h.
#ifdef OC_DLL_MODE
#define sgTemplateList (*reinterpret_cast<TTEMPLATELIST *>(OC_VA(0x5033C4u)))
#else
extern TTEMPLATELIST sgTemplateList;
#endif
#ifdef OC_DLL_MODE
#define gGameHeader (*reinterpret_cast<GameHeader *>(OC_VA(0x6728D0u)))
#else
extern GameHeader gGameHeader;
#endif

// 0x534A00: the per-player map start locations, eight point16_t (maphdr_UNIT
// initialises all 32 bytes). Four bodies read x at +0 and y at +2, signed;
// sub_448210 pushes a whole point by value, so ai_get_build_location_near's
// third parameter is a point16_t. No source names
// it; `g` because eight TUs read it. A struct, so declared here like gGameHeader.
#ifdef OC_DLL_MODE
#define gStartLocations (reinterpret_cast<point16_t *>(OC_VA(0x534A00u)))
#else
extern PlayerTable<point16_t, PlayerTagStartLocations, 8> gStartLocations;
#endif

// The RTTI type descriptor for `struct TTEMPLATE` at 0x5107BC
// (".?AUTTEMPLATE@@"), used in place of typeid(TTEMPLATE), whose descriptor
// would be the compiler's own.
#ifdef OC_DLL_MODE
#define gTypeInfo_TTEMPLATE (*reinterpret_cast<type_info *>(OC_VA(0x5107BCu)))
#else
extern type_info gTypeInfo_TTEMPLATE;
#endif

// gbIsExpansion: GetSaveExtension, HasEitherCampaign, and gametype_run_aiscript's
// choice of the 'x' (expansion) or 'u' (original) AI scripts test it.
#ifdef OC_DLL_MODE
#define gbIsExpansion (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE gbIsExpansion;
#endif


// Callees this TU reaches that no header declares.

// victory_free -- 0x4BE1D0 (victory.cpp): frees the TSList of trigger blocks.
void __fastcall victory_free(void);

// load_game_data -- 0x444890 (gamedata.cpp): reads pszFile whole into pBuffer,
// or into a fresh allocation when pBuffer is NULL, and stores its size through
// pdwSize when that is not NULL. pszTag and nLine are accepted and ignored.
void *load_game_data(const char *pszFile, void *pBuffer,
                                DWORD *pdwSize, const char *pszTag, int nLine);

// mem_free -- 0x444CA0 (gamedata.cpp): a null-guarded SMemFree with the caller's
// tag and line.
void mem_free(void *pv, const char *pszTag, int nLine);

// TryAddTrigger -- 0x4BDB00 (victory.cpp). Every caller passes 0 in EDX, which
// the body never reads; the parameter keeps that register set up.
int __fastcall TryAddTrigger(void *pTrigger, int nFlags);


// CreateGametypes (0x447740) and gametype_load (0x4482A0) are declared in
// gametype.h.

// run_aiscript -- 0x493970: the four-character script id in ECX, the player in
// EDX, and the area on the stack, which it passes on to aiscript_new_thread.
void __fastcall run_aiscript(DWORD dwScriptId, int nPlayer,
                             const rect32_t *prcArea);


// IsTeamGame and GetNumTeams are declared in Teams.h, included above.

// IsFullPlayerTeam and IsComputerTeam are declared in Teams.h,
// team_get_slot_captain in src117/h/gametype.h.


// The TSList unlink, written as 1.17's other instantiations of the template do
// it (TSList<CTRLNODE,...>::DeleteNode at 0x4C46F0 and the like). `prev` points
// at the predecessor's link and `next` at the successor object; the link is at
// offset 0 of TTEMPLATE, so both are spelled T*. The link's offset is still
// computed at run time, as 1.17 does, through the guarded nOff below, which
// keeps it a value of its own rather than folded into one sum.
static void UnlinkTemplate(TTEMPLATE *node) {
  TSLink<TTEMPLATE> *pl = &node->link;
  TTEMPLATE *pprev = pl->prev;
  if (pprev != NULL) {
    TTEMPLATE *pnext = pl->next;
    TSLink<TTEMPLATE> *pnl;
    if ((intptr_t)pnext <= 0) {
      // the terminator: the complement is the address of the list's own link
      pnl = reinterpret_cast<TSLink<TTEMPLATE> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0) {
        nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
      }
      pnl = reinterpret_cast<TSLink<TTEMPLATE> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    pl->prev->link.next = pl->next;
    pl->prev = NULL;
    pl->next = NULL;
  }
}

// Two exits rather than a setcc, since the two tests read separate bytes.
int __fastcall AlliesAllowed(void) {
  if (gGameHeader.bAlliesAllowed == 1 && gGameHeader.bLadderGame == 0) {
    return 1;
  }
  return 0;
}

// A tail jump into CreateGametypes.
int __fastcall AllocGametypes(void) {
  return CreateGametypes(CreateTemplate);
}

// Five exits, in 1.17's order: gPlayback -> 1; A observing -> 0; B observing
// -> 1; then AlliesAllowed's own two tests. [i*36 + 0x6726D0] is
// &gPlayerData[i].nType, and 0x67294C / 0x67294F are gGameHeader fields.
int __fastcall CanChangeVision(int nPlayerA, int nPlayerB) {
  if (gPlayback) {
    return 1;
  }
  if (gPlayerData[nPlayerA].nType == 9) {
    return 0;
  }
  if (gPlayerData[nPlayerB].nType == 9) {
    return 1;
  }
  if (gGameHeader.bAlliesAllowed == 1 && gGameHeader.bLadderGame == 0) {
    return 1;
  }
  return 0;
}

int __fastcall CheatsAllowed(void) {
  return gGameHeader.bCheatsAllowed == 1;
}

// Unlinks and frees every template. The head is normalised to null before the
// test, which keeps the loop in 1.17's shape.
void TTEMPLATELIST::Clear(void) {
  for (;;) {
    TTEMPLATE *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkTemplate(p);
    SMemFree(p, gTypeInfo_TTEMPLATE.raw_name(), -2, 0);
  }
}

// The field is loaded once into a local and compared twice.
int __fastcall ComputersAllowed(void) {
  BYTE b = gGameHeader.bComputersAllowed;
  if (b == 1 || b == 3) {
    return 1;
  }
  return 0;
}

// The first load is a WORD: the record is {BYTE, BYTE, WORD}, and the result
// w<<16 | b1<<8 | b0 is assembled from the three fields.
DWORD __fastcall sub_4476D0(const BYTE *p) {
  return (DWORD)(((*reinterpret_cast<const WORD *>(p + 2) << 8) | p[1]) << 8)
         | p[0];
}

// Bit assembly over the same {BYTE, BYTE, WORD} record sub_4476D0 reads, as an
// if / else-if chain.
DWORD __fastcall sub_4476F0(const BYTE *p) {
  DWORD dwResult = 0;
  if (p[0] == 9 && *reinterpret_cast<const WORD *>(p + 2) == 2) {
    dwResult = 0x10;
  } else if (p[0] == 0x80 && p[1] != 0) {
    dwResult = ((DWORD)p[1] << 24) | 0x800020;
    if (*reinterpret_cast<const WORD *>(p + 2) == 2) {
      dwResult |= 0x10;
    }
  }
  if (gPlayback) {
    dwResult |= 0x80;
  }
  return dwResult;
}

// Not a list walk: a quote-delimited string tokenizer over a text buffer,
// __fastcall with four arguments (`ret 8h`). *pdwRemaining is read through the
// pointer after the first store into the output, which may alias it, so each of
// those reads stays a dereference. j is declared at the top and assigned after
// the first loop, and the tail is `if (ok) { ...; return 1; } return 0;`, both
// 1.17's shape. noinline keeps it a call in CreateGametypes.
__declspec(noinline) int __fastcall NextListEntry(char **ppCursor, DWORD *pdwRemaining,
                             char *pszOut, DWORD dwCapacity) {
  DWORD i = 0;
  DWORD j;
  char *pc = *ppCursor;
  while (i < *pdwRemaining && *pc != '"') {
    i++;
    pc++;
  }
  if (i >= *pdwRemaining) {
    return 0;
  }
  i++;
  pc++;
  j = 0;
  while (i < *pdwRemaining && j < dwCapacity && *pc != '"') {
    pszOut[j] = *pc;
    i++;
    pc++;
    j++;
  }
  if (i < *pdwRemaining && j < dwCapacity) {
    pszOut[j] = 0;
    pc++;
    *ppCursor = pc;
    *pdwRemaining -= i + 1;
    return 1;
  }
  return 0;
}

// IsBNetGame is called twice, as in 1.17: it is opaque to the compiler.
int __fastcall gametype_template_allowed(const BYTE *p, int nId) {
  if (!IsBNetGame() && p[0x12] != 0) {
    return 0;
  }
  if (p[0] == 0x80 && p[1] != 0) {
    if (!IsBNetGame()) {
      return 0;
    }
    if (nId != p[1]) {
      return 0;
    }
  }
  if (gbMultiPlayerMode == 0) {
    if (p[0x0E] == 0) {
      return 0;
    }
    if (p[0x0E] == 1) {
      return 0;
    }
  }
  return 1;
}

void __stdcall CreateTemplate(const char *pszName, const char *pszParam,
                              const TGAMEPARAMS *pParams,
                              const TTEMPLATEMETA *pMeta) {
  void *pv = SMemAlloc(sizeof(TTEMPLATE), gTypeInfo_TTEMPLATE.raw_name(), -2, 8);
  TTEMPLATE *node;
  TSLink<TTEMPLATE> *nl;
  if (pv != NULL) {
    node = reinterpret_cast<TTEMPLATE *>(pv);
    node->link.prev = NULL;
    node->link.next = NULL;
    nl = &node->link;
  } else {
    node = NULL;
    nl = &sgTemplateList.link;
  }
  UnlinkTemplate(reinterpret_cast<TTEMPLATE *>(nl));
  // append: insert before the list's own link, i.e. after the current tail.
  TTEMPLATE *ptail = sgTemplateList.link.prev;
  nl->prev = ptail;
  nl->next = ptail->link.next;
  ptail->link.next = node;
  sgTemplateList.link.prev = reinterpret_cast<TTEMPLATE *>(nl);
  if (node != NULL) {
    SStrCopy(node->szName, pszName, sizeof(node->szName));
    SStrCopy(node->szParam, pszParam, sizeof(node->szParam));
    node->params = *pParams;
    if (pMeta != NULL) {
      SStrCopy(node->meta.szPath, pMeta->szPath, sizeof(node->meta.szPath));
      SStrCopy(node->meta.szName, pMeta->szName, sizeof(node->meta.szName));
      node->meta.dwValue = pMeta->dwValue;
    }
  }
}

// The bare two-key walk, and the template
// the rest of the finder family is built on.
// The test is signed, `(int)p > 0` and never `p != NULL`: the last node's next
// holds the complemented address of the list's link, a negative value
// (src117/h/TSList.h). The three-key siblings need the do/while spelling.
char *__fastcall gametype_find_template_name(BYTE bCategory, BYTE bSub) {
  for (TTEMPLATE *p = sgTemplateList.link.next; (intptr_t)p > 0;
       p = p->link.next) {
    if (p->params.bCategory == bCategory && p->params.bSub == bSub) {
      return p->szName;
    }
  }
  return 0;
}

// The same walk as 0x447A40, instruction for
// instruction, except that it returns &p->szParam rather than &p->szName.
char *__fastcall gametype_find_param_name(BYTE bCategory, BYTE bSub) {
  for (TTEMPLATE *p = sgTemplateList.link.next; (intptr_t)p > 0;
       p = p->link.next) {
    if (p->params.bCategory == bCategory && p->params.bSub == bSub) {
      return p->szParam;
    }
  }
  return 0;
}

// A tail jump into Clear.
void __fastcall gametype_exit(BYTE bArg0) {
  sgTemplateList.Clear();
}

// Both out-buffers are the same 32-byte
// stack buffer.
TGAMEPARAMS *__fastcall gametype_load_ums(void) {
  char szBuf[0x20];
  return gametype_load("Use Map Settings(1)", szBuf, szBuf);
}

// Formats a subtype's name: a string-table entry for category 9, the team split
// for 0x0F, else the value. Case 9 reads the category again in merged arms, as
// 1.17 does.
void gametype_get_subtype_name(char *pszDest, int cchDest,
                               const TGAMEPARAMS *pParams, int nMax) {
  switch (pParams->bCategory) {
  case 9:
    if (pParams->bCategory)
      SStrCopy(pszDest, glues_get_str(0xA4 + pParams->dwSubValue), cchDest);
    else
      SStrCopy(pszDest, glues_get_str(0xA4 + pParams->dwSubValue), cchDest);
    break;
  case 0x0F:
    psnprintf(pszDest, cchDest, glues_get_str(0xAC), pParams->dwSubValue,
              nMax - pParams->dwSubValue);
    break;
  default:
    psnprintf(pszDest, cchDest, "%u", pParams->dwSubValue);
    break;
  }
}

DWORD __fastcall gametype_get_subtype_value(void) {
  return gGameHeader.dwSubTypeValue;
}

// int, not WORD: 1.17 zero-extends the WORD
// field into the whole of EAX.
int __fastcall gametype_get_subtype(void) {
  return gGameHeader.wSubType;
}

// Tests gGameHeader + 0x27.
int __fastcall IsBlizzardMap(void) {
  return gGameHeader.bMapAuthentication == 1;
}

int __fastcall IsCTFGame(void) {
  return gGameHeader.bGameSubCategory == 4;
}

// Both operands arrive in registers; the same two-exit shape as AlliesAllowed.
int __fastcall IsCompetitiveGame(BYTE bGameType, BYTE bGameTypeSub) {
  if (bGameType == 0x80 && bGameTypeSub != 0) {
    return 1;
  }
  return 0;
}

int __fastcall IsGreedGame(void) {
  return gGameHeader.bGameSubCategory == 3;
}

int __fastcall IsKBKMap(void) {
  return gGameHeader.bMapAuthentication == 4;
}

int __fastcall IsLadderGame(void) {
  return gGameHeader.bLadderGame != 0;
}

int __fastcall IsLadderMap(void) {
  return gGameHeader.bMapAuthentication == 2;
}

int __fastcall IsMeleeGame(void) {
  return gGameHeader.bGameSubCategory == 1;
}

// The null check is in the callee: the pointer defaults to &gGameHeader.
// Written as a ternary, as 1.17's cmov is.
int __fastcall IsOOOGame(const GameHeader *pHeader) {
  const GameHeader *p = pHeader ? pHeader : &gGameHeader;
  return p->bGameSubCategory == 7;
}

int __fastcall IsPGTMap(void) {
  return gGameHeader.bMapAuthentication == 3;
}

// The IsOOOGame default-pointer shape with a
// three-field AND on it, and the same two-exit tail.
int __fastcall IsScenarioGame(const GameHeader *pHeader) {
  const GameHeader *p = pHeader ? pHeader : &gGameHeader;
  if (p->bGameSubCategory == 0 && p->bScenarioFlag == 0 && p->bLadderGame == 0) {
    return 1;
  }
  return 0;
}

int __fastcall IsSlaughterGame(void) {
  return gGameHeader.bGameSubCategory == 6;
}

int __fastcall IsSuddenDeathGame(void) {
  return gGameHeader.bGameSubCategory == 5;
}

// Tests bGameType, not bGameSubCategory --
// a different axis with the same predicate shape.
int __fastcall IsTVBGame(void) {
  return gGameHeader.bGameType == 0x0F;
}

// AlliesAllowed's shape over the
// bGameType and bGameTypeSub pair; IsCompetitiveGame is this function with the
// two fields as arguments.
int __fastcall IsTournamentGame(void) {
  if (gGameHeader.bGameType == 0x80 && gGameHeader.bGameTypeSub != 0) {
    return 1;
  }
  return 0;
}

int __fastcall IsTournamentMap(void) {
  return gGameHeader.bMapAuthentication == 5;
}

// The == 0 twin of IsLadderGame.
int __fastcall IsUnauthenticatedMap(void) {
  return gGameHeader.bMapAuthentication == 0;
}

int __fastcall IsWGTAuthenticatedMap(void) {
  return gGameHeader.bMapAuthentication == 6;
}

// An unsigned window test, which the compiler makes branchless.
int __fastcall IsWGTGame(void) {
  return (BYTE)(gGameHeader.bGameType - 0x11) <= 5;
}

// The two-key callback walk.
// Both byte keys share EBX, the byte-addressable register the callback leaves
// alone. The same do/while shape as IterateMainTemplates below.
void __fastcall IterateLeagueTemplates(BYTE bCategory, BYTE bSub,
                                       TEMPLATEPROC pfn) {
  TTEMPLATE *p = sgTemplateList.link.next;
  if ((intptr_t)p > 0) {
    do {
      if (p->params.bCategory == bCategory && p->params.bSub == bSub) {
        pfn(p->szName, p->szParam, &p->params, 0);
      }
      p = p->link.next;
    } while ((intptr_t)p > 0 && p);
  }
}

// The callback walk.
// A do/while and not a for: 1.17 tests the next pointer twice at the bottom of
// the loop, which the do/while reproduces.
void __fastcall IterateMainTemplates(TEMPLATEPROC pfn) {
  TTEMPLATE *p = sgTemplateList.link.next;
  if ((intptr_t)p > 0) {
    do {
      if (p->params.wType == 1) {
        pfn(p->szName, p->szParam, &p->params, 0);
      }
      p = p->link.next;
    } while ((intptr_t)p > 0 && p);
  }
}

// The trigger file is a whole number of 2400-byte (0x960) records. The division
// is unsigned and the tests after it signed: a DWORD size into an int count.
// The two line numbers are 1.17's __LINE__ values, written as literals, and the
// string is 1.17's own "gametype.cpp" at 0x4EC4D8.
int __fastcall gametype_load_trigger_file(const char *pszFile) {
  DWORD dwSize;
  victory_free();
  void *pData = load_game_data(pszFile, 0, &dwSize, "gametype.cpp", 900);
  if (!pData) {
    return 0;
  }
  BYTE *pTrigger = (BYTE *)pData;
  int nCount = dwSize / 0x960;
  for (int i = 0; i < nCount; i++) {
    if (!TryAddTrigger(pTrigger, 0)) {
      break;
    }
    pTrigger += 0x960;
  }
  mem_free(pData, "gametype.cpp", 914);
  return 1;
}

// Storm's init-data event: adopt the
// host's template parameters when a local template has the same key and all 32
// bytes agree, else mark the game type unknown. The key reads sit inside the
// `if`, the word first.
void __stdcall snet_initdata_cb(SNETEVENT *pEvent) {
  const DWORD *pData = reinterpret_cast<const DWORD *>(pEvent->pszData);
  const TGAMEPARAMS *pFound;
  TTEMPLATE *p = sgTemplateList.link.next;
  if ((intptr_t)p > 0) {
    const WORD wType = *reinterpret_cast<const WORD *>(&gGameHeader._unmapped_28[2]);
    const BYTE bSub = gGameHeader._unmapped_28[1];
    const BYTE bCat = gGameHeader._unmapped_28[0];
    do {
      if (p->params.bCategory == bCat && p->params.bSub == bSub &&
          p->params.wType == wType)
        goto found;
      p = p->link.next;
    } while ((intptr_t)p > 0 && p);
  }
  pFound = 0;
  goto have;
found:
  pFound = &p->params;
have:
  if (pEvent->cbData == sizeof(TGAMEPARAMS) && pFound &&
      memcmp(pFound, pData, sizeof(TGAMEPARAMS)) == 0) {
    // Copied as two halves, as 1.17 copies it.
    struct half_t { DWORD d[4]; };
    reinterpret_cast<half_t *>(&gGameHeader.bGameType)[0] = reinterpret_cast<const half_t *>(pFound)[0];
    reinterpret_cast<half_t *>(&gGameHeader.bGameType)[1] = reinterpret_cast<const half_t *>(pFound)[1];
  } else {
    gGameHeader.bGameType = 0xFF;
  }
}

// The unit type is a WORD: 1.17 adds and zero-extends it as 16 bits. 0x6726D1
// is &gPlayerData[0].nRace, folded with the 36-byte stride.
void __fastcall sub_448210(int nPlayer) {
  point16_t pt;
  WORD wUnitType = (WORD)(gPlayerData[nPlayer].nRace + 0xC5);
  CUnit *pUnit = CUnit::GetPlayerUnitList(nPlayer);
  while (pUnit) {
    if (gulUnitTypeFlags[pUnit->wType] & 8) {
      if (ai_get_build_location_near(pUnit, wUnitType, gStartLocations[nPlayer], &pt, 0, 0x40)) {
        CUnit *pNew = CreateUnitAt(wUnitType, pt.x, pt.y, nPlayer);
        if (pNew) {
          pNew->FinishedMaking();
        }
      }
      return;
    }
    pUnit = pUnit->pPlayerUnitNext;
  }
}

// sub_448210's body is inlined in the team branch and called in the player
// branch, so sub_448210 is defined above this one. The test is
// `nType == 2 || nType == 1`.
void __fastcall sub_4480F0(void) {
  if (IsTeamGame()) {
    int i = GetNumTeams();
    while (i != 0) {
      i--;
      BYTE bTeam = (BYTE)(i + 1);
      if (IsFullPlayerTeam(bTeam) || IsComputerTeam(bTeam)) {
        sub_448210(team_get_slot_captain(bTeam));
      }
    }
  } else {
    int j = 8;
    do {
      j--;
      BYTE bType = gPlayerData[j].nType;
      if (bType == 2 || bType == 1) {
        sub_448210(j);
      }
    } while (j != 0);
  }
}

// The unlink again, in the simpler
// terminator spelling, `(int)next > 0 ? next : NULL`, taken before the filter
// test because the node is freed inside the loop.
void __fastcall gametype_clear_leagues(void) {
  // The successor is the loop-carried variable, copied into the current node at
  // the top of the loop, as 1.17 orders it.
  TTEMPLATE *pnext = sgTemplateList.link.next;
  if ((intptr_t)pnext <= 0) {
    pnext = NULL;
  }
  while (pnext != NULL) {
    TTEMPLATE *p = pnext;
    pnext = p->link.next;
    if ((intptr_t)pnext <= 0) {
      pnext = NULL;
    }
    if (p->params.bCategory == 0x80) {
      UnlinkTemplate(p);
      SMemFree(p, gTypeInfo_TTEMPLATE.raw_name(), -2, 0);
    }
  }
}

// The script ids are 4-byte little-endian
// ASCII: 0x78434D5A is "ZMCx", 0x78434D54 "TMCx", 0x78434D50 "PMCx", and the
// 'u' twins are the original game's.
void __fastcall gametype_run_aiscript(void) {
  // Unsigned, as 1.17's counter is.
  // Hyperion: 8 slots in 1.17, every lobby slot on a Hyperion map; neutral's is
  // never a computer
  for (unsigned i = 0; i < static_cast<unsigned>(PlayerLobbySlots()); i++) {
    if (gPlayerData[i].nType != 1) {
      continue;
    }
    DWORD dwScript;
    if (gbIsExpansion != 0) {
      switch (gPlayerData[i].nRace) {
        case 0:  dwScript = 0x78434D5Au; break;   // "ZMCx"
        case 1:  dwScript = 0x78434D54u; break;   // "TMCx"
        case 2:  dwScript = 0x78434D50u; break;   // "PMCx"
        default: continue;
      }
    } else {
      switch (gPlayerData[i].nRace) {
        case 0:  dwScript = 0x75434D5Au; break;   // "ZMCu"
        case 1:  dwScript = 0x75434D54u; break;   // "TMCu"
        case 2:  dwScript = 0x75434D50u; break;   // "PMCu"
        default: continue;
      }
    }
    // Hyperion: a computer's profile may start another script, or none
    dwScript = AiMeleeScript(static_cast<int>(i), dwScript);
    if (dwScript == 0)
      continue;
    // The y pair (top, bottom) is stored first and the x pair second, 1.17's
    // order.
    rect32_t rcArea;
    rcArea.top = gStartLocations[i].y - 1;
    rcArea.bottom = gStartLocations[i].y + 1;
    rcArea.left = gStartLocations[i].x - 1;
    rcArea.right = gStartLocations[i].x + 1;
    run_aiscript(dwScript, i, &rcArea);
  }
}

// 0x447E40. The IsWGTGame window with a wider bound.
int __fastcall sub_447E40(void) {
  return (BYTE)(gGameHeader.bGameType - 0x11) <= 0x6E;
}

// The template list's head and successor, with the terminator folded to NULL.
// Written as a branch, not a ternary, as 1.17 has it.
static __forceinline TTEMPLATE *tmpl_head() {
  TTEMPLATE *p = sgTemplateList.link.next;
  if ((intptr_t)p <= 0)
    return 0;
  return p;
}

static __forceinline TTEMPLATE *tmpl_next(TTEMPLATE *p) {
  TTEMPLATE *pNext = p->link.next;
  if ((intptr_t)pNext <= 0)
    return 0;
  return pNext;
}

// 0x447B10. The metadata of the template with this three-part key.
// The key is a WORD: `int` loads the slot
// as a dword.
const TTEMPLATEMETA *gametype_find_template_meta(BYTE bCategory, BYTE bSub,
                                                 WORD wType) {
  for (TTEMPLATE *p = tmpl_head(); p; p = tmpl_next(p)) {
    if (p->params.bCategory == bCategory && p->params.bSub == bSub &&
        p->params.wType == wType)
      return &p->meta;
  }
  return 0;
}

// The parameters of the template with this
// three-part key.
const TGAMEPARAMS *gametype_find_template(BYTE bCategory, BYTE bSub,
                                          WORD wType) {
  for (TTEMPLATE *p = tmpl_head(); p; p = tmpl_next(p)) {
    if (p->params.bCategory == bCategory && p->params.bSub == bSub &&
        p->params.wType == wType)
      return &p->params;
  }
  return 0;
}

// Whether the keyed template is a melee
// type. gametype_find_template is inlined, so it must be defined above.
int __fastcall IsMeleeTemplate(BYTE bCategory, BYTE bSub, WORD wType) {
  const TGAMEPARAMS *pParams = gametype_find_template(bCategory, bSub, wType);
  if (!pParams)
    return 0;
  return pParams->bMelee == 1;
}

// Whether the keyed template sets none of
// its three option bytes.
int IsSimpleTemplate(BYTE bCategory, BYTE bSub, WORD wType) {
  const TGAMEPARAMS *pParams = gametype_find_template(bCategory, bSub, wType);
  if (!pParams || pParams->bMelee || pParams->bFlag0C || pParams->bFlag12)
    return 0;
  return 1;
}

// gametype_find_template inlined on the
// record's three keys, then the template's player cap against bMaxPlayers.
int sub_4480A0(const gamedesc_t *pData) {
  const TGAMEPARAMS *pParams = gametype_find_template(
      pData->bGameType, pData->bGameSubType, pData->wField0E);
  if (!pParams)
    return 0;
  if (pParams->bOpt10 != 0 && pParams->bOpt10 > pData->bMaxPlayers)
    return 0;
  return 1;
}

// 0x447C30. The display name of the first template in a category.
char *__fastcall gametype_find_template_meta_name(BYTE bCategory) {
  TTEMPLATE *p = sgTemplateList.link.next;
  if ((intptr_t)p > 0) {
    do {
      if (bCategory == p->params.bCategory) {
        return p->meta.szName;
      }
      p = p->link.next;
    } while ((intptr_t)p > 0 && p);
  }
  return 0;
}

// 0x447C60. The map path of the first template in a category.
char *__fastcall gametype_find_template_meta_path(BYTE bCategory) {
  TTEMPLATE *p = sgTemplateList.link.next;
  if ((intptr_t)p > 0) {
    do {
      if (bCategory == p->params.bCategory) {
        return p->meta.szPath;
      }
      p = p->link.next;
    } while ((intptr_t)p > 0 && p);
  }
  return 0;
}

// 0x447A70. The name of the template whose parameters equal
// all 32 bytes of pParams; the inline memcmp counts 0x1C down to 0 by 4.
char *__fastcall sub_447A70(const TGAMEPARAMS *pParams) {
  TTEMPLATE *p = sgTemplateList.link.next;
  if ((intptr_t)p > 0) {
    do {
      if (memcmp(&p->params, pParams, sizeof(TGAMEPARAMS)) == 0) {
        return p->szName;
      }
      p = p->link.next;
    } while ((intptr_t)p > 0 && p);
  }
  return 0;
}

// 0x447500. Whether player nPlayer may set slot bSlot to bRace. A player
// without a Storm id may also recolor computer teams and open slots; the two
// halves are an if/else with one shared `return TRUE`.
BOOL CanChangeRace(BYTE bSlot, BYTE bRace, int nPlayer) {
  if ((unsigned)nPlayer >= 8)
    return FALSE;
  if (bSlot >= 8)
    return FALSE;
  if (bRace != 0 && bRace != 1 && bRace != 2 && bRace != 6)
    return FALSE;
  if (gGameHeader.bGameSubCategory == 0 && gGameHeader.bScenarioFlag == 0 &&
      gGameHeader.bLadderGame == 0 && !is_race_selectable(bSlot))
    return FALSE;
  if (gpLoadGameFile != 0)
    return FALSE;
  if (gPlayerData[nPlayer].dwStormID == 0) {
    if (IsTeamGame()) {
      BYTE bTeam = gPlayerData[bSlot].nTeam;
      if (gPlayerData[nPlayer].nTeam != bTeam && !IsComputerTeam(bTeam))
        return FALSE;
    }
    if (!IsTeamGame() && bSlot != nPlayer && gPlayerData[bSlot].nType != 5)
      return FALSE;
  } else {
    if (IsTeamGame() &&
        gPlayerData[nPlayer].nTeam != gPlayerData[bSlot].nTeam)
      return FALSE;
    if (!IsTeamGame() && bSlot != nPlayer)
      return FALSE;
  }
  return TRUE;
}

// The .got template file: a version byte, the two display strings, then the
// parameter record, 97 bytes in all.
#pragma pack(push, 1)
struct GOTFILE {
  /*0x00*/ BYTE bVersion;
  /*0x01*/ char szName[0x20];
  /*0x21*/ char szParam[0x20];
  /*0x41*/ TGAMEPARAMS params;
};
#pragma pack(pop)
static_assert(sizeof(GOTFILE) == 0x61, "cmp dword ptr [ebp-4h], 61h");
static_assert(offsetof(GOTFILE, bVersion) == 0x00, "cmp byte ptr [edi], 3h");
static_assert(offsetof(GOTFILE, szName) == 0x01, "movdqu xmm0, [edi+1h]");
static_assert(offsetof(GOTFILE, szParam) == 0x21, "movdqu xmm0, [edi+21h]");
static_assert(offsetof(GOTFILE, params) == 0x41, "movdqu xmm0, [edi+41h]");

// 0x4482A0. Loads a template's .got file: copies its two strings out and returns
// its parameters in a fresh allocation, or NULL for a bad or foreign file.
// noinline keeps it a call in CreateGametypes, as in 1.17. The three copies walk
// one pointer through the file, set before the null test.
__declspec(noinline) TGAMEPARAMS *__fastcall gametype_load(const char *pszTemplate, char *pszNameOut,
                                      char *pszParamOut) {
  char szPath[0x104];
  DWORD dwSize;
  psnprintf(szPath, sizeof(szPath), "%s%s%s", "Templates\\", pszTemplate,
            ".got");
  GOTFILE *pFile = static_cast<GOTFILE *>(
      load_game_data(szPath, 0, &dwSize, "gametype.cpp", 58));
  const char *p = pFile->szName;
  if (pFile == 0)
    return 0;
  if (dwSize == sizeof(GOTFILE) && pFile->bVersion == 3) {
    memcpy(pszNameOut, p, sizeof(pFile->szName));
    p += sizeof(pFile->szName);
    memcpy(pszParamOut, p, sizeof(pFile->szParam));
    p += sizeof(pFile->szParam);
    TGAMEPARAMS *pParams = static_cast<TGAMEPARAMS *>(
        SMemAlloc(sizeof(TGAMEPARAMS), "gametype.cpp", 74, 0));
    memcpy(pParams, p, sizeof(TGAMEPARAMS));
    mem_free(pFile, "gametype.cpp", 78);
    if (pParams->bCategory < 0x81 && pParams->wType < 8)
      return pParams;
    SMemFree(pParams, "gametype.cpp", 98, 0);
    return 0;
  }
  mem_free(pFile, "gametype.cpp", 97);
  return 0;
}

// 0x447740. Reads the templates.lst index and hands every template it names
// to pfnCreate, then frees each record.
int __fastcall CreateGametypes(TEMPLATEPROC pfnCreate) {
  char szPath[0x104];
  char szEntry[0x104];
  char szName[0x20];
  char szParam[0x20];
  char *pCursor;
  DWORD dwSize;
  psnprintf(szPath, sizeof(szPath), "%s%s", "Templates\\", "templates.lst");
  char *pData = static_cast<char *>(
      load_game_data(szPath, 0, &dwSize, "gametype.cpp", 214));
  pCursor = pData;
  if (pData == 0)
    appcritf("Unable to read game templates.");
  while (NextListEntry(&pCursor, &dwSize, szEntry, sizeof(szEntry))) {
    // The const overload; the char * one is an inline wrapper.
    const char *pExt = strrchr(static_cast<const char *>(szEntry), '.');
    if (pExt != 0)
      *const_cast<char *>(pExt) = 0;
    TGAMEPARAMS *pParams = gametype_load(szEntry, szName, szParam);
    if (pParams != 0) {
      pfnCreate(szName, szParam, pParams, 0);
      SMemFree(pParams, "gametype.cpp", 231, 0);
    }
  }
  mem_free(pData, "gametype.cpp", 233);
  return 1;
}

// The two cumulative-resource tables above gPlayerGas. Resource mode 1 seeds
// 0x526460 together with the minerals, so it is the mineral total.
#ifdef OC_DLL_MODE
  #ifndef dword_526430
  #define dword_526430 (reinterpret_cast<int *>(OC_VA(0x526430u)))
  #endif
  #ifndef dword_526460
  #define dword_526460 (reinterpret_cast<int *>(OC_VA(0x526460u)))
  #endif
#else
extern PlayerGasGatheredTable dword_526430;
extern PlayerMineralsGatheredTable dword_526460;
#endif

// 0x448420. Sets every player's starting resources from the game header. The
// default arm repeats case 0 in full, as 1.17 does.
void gametype_init_resources() {
  switch (gGameHeader.bResourceMode) {
  case 0:
    memset(gPlayerMinerals.a, 0, sizeof(int) * 12);
    memset(gPlayerGas.a, 0, sizeof(int) * 12);
    memset(dword_526460.a, 0, sizeof(int) * 12);
    memset(dword_526430.a, 0, sizeof(int) * 12);
    break;
  case 1:
    {
      int n = gGameHeader.dwStartingMinerals;
      for (int i = 0; i < 12; i++)
        gPlayerMinerals[i] = n;
      for (int i = 0; i < 12; i++)
        dword_526460[i] = n;
    }
    for (int i = 0; i < 12; i++)
      gPlayerGas[i] = 0;
    for (int i = 0; i < 12; i++)
      dword_526430[i] = 0;
    break;
  default:
    memset(gPlayerMinerals.a, 0, sizeof(int) * 12);
    memset(gPlayerGas.a, 0, sizeof(int) * 12);
    memset(dword_526460.a, 0, sizeof(int) * 12);
    memset(dword_526430.a, 0, sizeof(int) * 12);
    break;
  }
  // Hyperion: Hyperion's slots past 11 start as the others do
  if (g_hxPlayers) {
    int n = gGameHeader.bResourceMode == 1 ? (int)gGameHeader.dwStartingMinerals : 0;
    for (int i = PLAYER_EXTRA_FIRST; i < PLAYER_SLOTS_MAX; i++) {
      gPlayerMinerals[i] = n;
      dword_526460[i] = n;
      gPlayerGas[i] = 0;
      dword_526430[i] = 0;
    }
  }
}

// Hyperion: a mutator's start resources, in any game type, counted as gathered as mode 1
// counts its minerals. game_init calls it once the game's mutators are known, which is
// after game_ui_init has run gametype_init_resources.
void gametype_mutator_resources() {
  int nMinerals, nGas;
  if (!MutatorStartResources(&nMinerals, &nGas))
    return;
  for (int i = 0; i < (g_hxPlayers ? PLAYER_SLOTS_MAX : 12); i++) {
    gPlayerMinerals[i] = nMinerals;
    dword_526460[i] = nMinerals;
    gPlayerGas[i] = nGas;
    dword_526430[i] = nGas;
  }
}

// 0x448530. Starts the computer players' AI, then loads the victory triggers
// that go with the game type. FALSE for a greed or slaughter setting that has
// no trigger file.
BOOL gametype_load_triggers() {
  if (ComputersAllowed() && !IsScenarioGame(0)) {
    gametype_run_aiscript();
  } else {
    for (unsigned i = 0; i < 8; i++)
      aiscript_start_campaign(i);
  }
  if (IsMeleeGame())
    return gametype_load_trigger_file("triggers\\Melee.trg");
  if (IsSuddenDeathGame())
    return gametype_load_trigger_file("triggers\\SuddenDeath.trg");
  if (IsOOOGame(0))
    return gametype_load_trigger_file("triggers\\Melee.trg");
  if (IsGreedGame()) {
    switch (gametype_get_subtype_value()) {
    case 2500:
      return gametype_load_trigger_file("triggers\\Greed2500.trg");
    case 5000:
      return gametype_load_trigger_file("triggers\\Greed5000.trg");
    case 7500:
      return gametype_load_trigger_file("triggers\\Greed7500.trg");
    case 10000:
      return gametype_load_trigger_file("triggers\\Greed10000.trg");
    default:
      return FALSE;
    }
  }
  if (IsSlaughterGame()) {
    switch (gametype_get_subtype_value()) {
    case 15:
      return gametype_load_trigger_file("triggers\\Slaughter15.trg");
    case 30:
      return gametype_load_trigger_file("triggers\\Slaughter30.trg");
    case 45:
      return gametype_load_trigger_file("triggers\\Slaughter45.trg");
    case 60:
      return gametype_load_trigger_file("triggers\\Slaughter60.trg");
    default:
      return FALSE;
    }
  }
  if (IsCTFGame()) {
    sub_4480F0();
    return gametype_load_trigger_file("triggers\\CTF.trg");
  }
  return TRUE;
}

// 0x66FD3C; maphdr.cpp reads it too.
#ifdef OC_DLL_MODE
  #ifndef sgbLeagueId
  #define sgbLeagueId (*reinterpret_cast<BYTE *>(OC_VA(0x66FD3Cu)))
  #endif
#else
extern BYTE sgbLeagueId;
#endif

// Copies len bytes out of the league record, or fails if fewer remain.
#define LEAGUE_READ(dst, len)            \
  if (dwSize < (len))                    \
    break;                               \
  memcpy(&(dst), p, (len));              \
  p += (len);                            \
  dwSize -= (len);

// 0x447220. Battle.net's league list: replaces every league template with the
// records in pData. Each record is a version byte, four strings (name, param,
// map path, map name), a dword, and the parameter fields one by one.
// dwSize itself counts the bytes left, decremented after the count byte is
// read.
// wType is read through this copy, as 1.17 reads it.
static __forceinline void LeagueCopy(void *pDst, const BYTE *pSrc, DWORD nLen) {
  memcpy(pDst, pSrc, nLen);
}

BOOL __stdcall UiLeagueCallback(const BYTE *pData, DWORD dwSize) {
  BOOL bOk = TRUE;
  gametype_clear_leagues();
  const BYTE *p = pData;
  BYTE bVersion;
  BYTE bCat;
  BYTE bSub;
  WORD wType;
  int nCount = *p++;
  dwSize--;
  for (int i = 0; i < nCount; i++) {
    TGAMEPARAMS params;
    memset(&params, 0, sizeof(params));
    LEAGUE_READ(bVersion, 1);
    DWORD len = (DWORD)(strlen((const char *)p) + 1);
    if (dwSize < len)
      break;
    const char *pszName = (const char *)p;
    p += len;
    dwSize -= len;
    len = (DWORD)(strlen((const char *)p) + 1);
    if (dwSize < len)
      break;
    const char *pszParam = (const char *)p;
    p += len;
    dwSize -= len;
    len = (DWORD)(strlen((const char *)p) + 1);
    if (dwSize < len)
      break;
    const char *pszPath = (const char *)p;
    p += len;
    dwSize -= len;
    len = (DWORD)(strlen((const char *)p) + 1);
    if (dwSize < len)
      break;
    const char *pszMapName = (const char *)p;
    dwSize -= len;
    p += len;
    DWORD dwValue;
    LEAGUE_READ(dwValue, 4);
    LEAGUE_READ(bCat, 1);
    params.bCategory = bCat;
    LEAGUE_READ(bSub, 1);
    params.bSub = bSub;
    if (dwSize < 2)
      break;
    LeagueCopy(&wType, p, 2);
    p += 2;
    dwSize -= 2;
    params.wType = wType;
    LEAGUE_READ(params.dwSubValue, 4);
    LEAGUE_READ(params.bMelee, 1);
    LEAGUE_READ(params.bOpt09, 1);
    LEAGUE_READ(params.bOpt0A, 1);
    LEAGUE_READ(params.bOpt0B, 1);
    LEAGUE_READ(params.bFlag0C, 1);
    LEAGUE_READ(params.bOpt0D, 1);
    LEAGUE_READ(params.bOpt0E, 1);
    LEAGUE_READ(params.bOpt0F, 1);
    LEAGUE_READ(params.bOpt10, 1);
    LEAGUE_READ(params.bOpt11, 1);
    LEAGUE_READ(params.bFlag12, 1);
    LEAGUE_READ(params.dwOpt13, 4);
    LEAGUE_READ(params.dwOpt17, 4);
    TTEMPLATEMETA meta;
    SStrCopy(meta.szPath, pszPath, sizeof(meta.szPath));
    SStrCopy(meta.szName, pszMapName, sizeof(meta.szName));
    meta.dwValue = dwValue;
    if (bOk && bVersion == 3 && bCat < 0x81 && wType < 8) {
      CreateTemplate(pszName, pszParam, &params, &meta);
      bOk = TRUE;
    } else {
      bOk = FALSE;
    }
    if (params.bSub != 0)
      sgbLeagueId = params.bSub;
  }
  return bOk;
}
#undef LEAGUE_READ
