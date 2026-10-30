// TrigLB.cpp -- the trigger leaderboard, oracle 0x4BA880..0x4BAD01: two aggregators that walk
// the nations through one of TrigEnum's counting callbacks, the dispatcher that
// rebuilds the eight {nation, value} records and sorts them, and the two qsort
// comparators. Bodies are in ascending oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../h/platform.h"
#include <stdlib.h>            // qsort
#include "../h/net_cmds.h"          // PlayerData -- gPlayerData[n].nType
#include "../h/TrigEnum.h"          // NationEnumCb, nation_find_first/next,
                               // TrigEnum_PrepCounts, owned_count_atloc and the
                               // four counting callbacks
#include "../h/TrigLB.h"       // this TU's own interface

// Globals, one #ifdef per name. dword_5349E8 and gPlayerData are registered and
// keep the registry's types; 0x6726D0 is the phantom &gPlayerData[0].nType and
// is spelled as that subscript.
#ifdef OC_DLL_MODE
#define dword_5349E8 (*reinterpret_cast<DWORD *>(OC_VA(0x5349E8u)))
#define byte_5349EC  (*reinterpret_cast<BYTE *>(OC_VA(0x5349ECu)))
#define byte_5349ED  (*reinterpret_cast<BYTE *>(OC_VA(0x5349EDu)))
#define word_5349EE  (*reinterpret_cast<WORD *>(OC_VA(0x5349EEu)))
#define dword_5349F0 (*reinterpret_cast<DWORD *>(OC_VA(0x5349F0u)))
#define gPlayerData  (reinterpret_cast<PlayerData *>(OC_VA(0x6726C8u)))
#define glbVals_g (reinterpret_cast<LONG *>(OC_VA(0x6CA6D8u)))
#else
extern DWORD      dword_5349E8;
extern BYTE       byte_5349EC;
extern BYTE       byte_5349ED;
extern WORD       word_5349EE;
extern DWORD      dword_5349F0;
extern PlayerTable<PlayerData, PlayerTagData> gPlayerData;
extern LONG       glbVals_g[16] OC_UNALIGNED;
#endif

// gPlayerData[n].nType == 2 is the one player type the leaderboard always
// counts; every other type is counted only while dword_5349E8 is set. The 2 is
// the oracle's `cmp byte ptr [eax*4+6726D0h], 2h`; what that type means is not
// derived here.
static const BYTE kPlayerTypeCounted = 2;

// One leaderboard record as lb_update clears it: the nation, then its value.
struct LbRec { LONG nNation; LONG nValue; };

// Hyperion: 1.17's board keeps eight records, one a lobby slot, in
// glbVals_g. A Hyperion game's nation walk reaches every lobby slot, so its board keeps a
// record for each slot in storage of the port's own, sorted as 1.17's are; the board's
// dialog shows the first eight, the leaders. A 1.17 game uses glbVals_g as before.
static LONG g_alLbHx[PLAYER_SLOTS_MAX * 2];

static int LbRecords() { return g_hxPlayers ? PLAYER_SLOTS_MAX : 8; }

int lb_values_bytes() { return g_hxPlayers ? static_cast<int>(sizeof g_alLbHx) : 0x40; }

// get_lb_values.
LONG *get_lb_values()
{
  return g_hxPlayers ? g_alLbHx : glbVals_g;  // Hyperion
}

// agg_lb_values_togoal. The nation walk that
// accumulates the shortfall against the goal rather than the raw count, so the
// board counts down toward a target.
void agg_lb_values_togoal(NationEnumCb pfnCount, WORD nParam)
{
  int   nCursor;
  DWORD nNation;

  for (nNation = nation_find_first(&nCursor); nNation < (DWORD)PlayerLobbySlots();
       nNation = nation_find_next(&nCursor))
  {
    int nCount;

    if (gPlayerData[nNation].nType != kPlayerTypeCounted && dword_5349E8 == 0)
      continue;

    nCount = pfnCount(TrigSlotTag(nNation), nParam, reinterpret_cast<void *>(1));  // Hyperion
    get_lb_values()[nNation * 2 + 1] +=
        nCount >= dword_5349F0 ? 0 : dword_5349F0 - nCount;
    get_lb_values()[nNation * 2] = nNation;
  }
}

// lb_update. Clear the records, then dispatch on
// the board type: 0x11..0x15 count and sort ascending, 0x21..0x28 count toward
// the goal and sort descending. Each record is cleared as one
// {nation, value} pair, which keeps the stores scalar. Every arm picks a
// comparator and one qsort follows the switch; a type outside both families
// returns without sorting.
void lb_update()
{
  int (__cdecl *pfnCmp)(const void *, const void *);

  for (int i = 0; i < LbRecords(); i++) {  // Hyperion: 8 in 1.17
    LbRec r = { -1, 0 };
    reinterpret_cast<LbRec *>(get_lb_values())[i] = r;
  }

  switch (byte_5349EC)
  {
  case 0x11:
    TrigEnum_PrepCounts();
    agg_lb_values(units_count);
    pfnCmp = lb_sort_ascending_cb;
    break;
  case 0x12:
    owned_count_atloc(byte_5349ED);
    agg_lb_values(units_count);
    pfnCmp = lb_sort_ascending_cb;
    break;
  case 0x13:
    agg_lb_values(resource_count);
    pfnCmp = lb_sort_ascending_cb;
    break;
  case 0x14:
    agg_lb_values(kill_count);
    pfnCmp = lb_sort_ascending_cb;
    break;
  case 0x15:
    agg_lb_values(score_count);
    pfnCmp = lb_sort_ascending_cb;
    break;
  case 0x21:
    TrigEnum_PrepCounts();
    agg_lb_values_togoal(units_count, word_5349EE);
    pfnCmp = lb_sort_descending_cb;
    break;
  case 0x22:
    owned_count_atloc(byte_5349ED);
    agg_lb_values_togoal(units_count, word_5349EE);
    pfnCmp = lb_sort_descending_cb;
    break;
  case 0x23:
    if (word_5349EE != 2)
    {
      agg_lb_values_togoal(resource_count, word_5349EE);
    }
    else
    {
      agg_lb_values_togoal(resource_count, 0);
      agg_lb_values_togoal(resource_count, 1);
    }
    pfnCmp = lb_sort_descending_cb;
    break;
  case 0x24:
    agg_lb_values_togoal(kill_count, word_5349EE);
    pfnCmp = lb_sort_descending_cb;
    break;
  case 0x25:
    if (word_5349EE == 0 || word_5349EE == 3 || word_5349EE == 6)
      sub_4BABF0();
    else
      agg_lb_values_togoal(score_count, word_5349EE);
    pfnCmp = lb_sort_descending_cb;
    break;
  case 0x28:
    agg_lb_values_togoal(resource_count, 0);
    agg_lb_values_togoal(resource_count, 1);
    pfnCmp = lb_sort_descending_cb;
    break;
  default:
    return;
  }
  qsort(get_lb_values(), LbRecords(), 8, pfnCmp);  // Hyperion: 8 in 1.17
}

// agg_lb_values. The same nation walk, with
// the callback's raw result stored and the board's sub-type handed to it.
void agg_lb_values(NationEnumCb pfnCount)
{
  int   nCursor;
  DWORD nNation;

  for (nNation = nation_find_first(&nCursor); nNation < (DWORD)PlayerLobbySlots();
       nNation = nation_find_next(&nCursor))
  {
    if (gPlayerData[nNation].nType != kPlayerTypeCounted && dword_5349E8 == 0)
      continue;

    get_lb_values()[nNation * 2] = nNation;  // Hyperion
    get_lb_values()[nNation * 2 + 1] =
        pfnCount(TrigSlotTag(nNation), word_5349EE, reinterpret_cast<void *>(1));
  }
}

// sub_4BABF0. The composite score boards:
// each one sums several score_count sub-types into the same records.
void sub_4BABF0()
{
  switch (word_5349EE)
  {
  case 0:
    agg_lb_values_togoal(score_count, 7);
    agg_lb_values_togoal(score_count, 1);
    agg_lb_values_togoal(score_count, 2);
    agg_lb_values_togoal(score_count, 4);
    agg_lb_values_togoal(score_count, 5);
    break;

  case 3:
    agg_lb_values_togoal(score_count, 1);
    agg_lb_values_togoal(score_count, 2);
    break;

  case 6:
    agg_lb_values_togoal(score_count, 4);
    agg_lb_values_togoal(score_count, 5);
    break;
  }
}

// lb_sort_ascending_cb. The qsort comparator
// for the ascending boards: records with no nation sort last, then by value.
// The two volatile reads are the only shape that matches: the first loads pA
// ahead of the right value so the right value takes EAX, the second keeps the
// left value a memory operand of the shared compare.
int __cdecl lb_sort_ascending_cb(const void *pA, const void *pB)
{
  const DWORD *pRight = static_cast<const DWORD *>(pB);
  DWORD nRightId = pRight[0];
  // Volatile so pA is loaded here, before the right value.
  const DWORD *pLeft = static_cast<const DWORD *>(*(const void *volatile *)&pA);
  DWORD nRightVal = pRight[1];

  if (pLeft[0] == 0xFFFFFFFF)
    return nRightId != 0xFFFFFFFF;
  if (nRightId == 0xFFFFFFFF)
    return -1;
  // Volatile so the value is not cached in a register for the second compare.
  if (*(volatile const DWORD *)&pLeft[1] > nRightVal)
    return -1;
  return pLeft[1] < nRightVal;
}

// lb_sort_descending_cb. The mirror: the two
// value comparisons read their operands the other way round.
int __cdecl lb_sort_descending_cb(const void *pA, const void *pB)
{
  const DWORD *pRight = static_cast<const DWORD *>(pB);
  DWORD nRightId = pRight[0];
  // Volatile so pA is loaded here, before the right value.
  const DWORD *pLeft = static_cast<const DWORD *>(*(const void *volatile *)&pA);
  DWORD nRightVal = pRight[1];

  if (pLeft[0] == 0xFFFFFFFF)
    return nRightId != 0xFFFFFFFF;
  if (nRightId == 0xFFFFFFFF)
    return -1;
  // Volatile so the value is not cached in a register for the second compare.
  if (nRightVal > *(volatile const DWORD *)&pLeft[1])
    return -1;
  return nRightVal < pLeft[1];
}
