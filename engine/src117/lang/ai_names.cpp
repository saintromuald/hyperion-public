// ai_names.cpp -- computer player name assignment. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller in this file by
// /Ob2, which the oracle (WPO off) never does across TUs; the lever against it
// is __declspec(noinline) on the declaration.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/ai_names.h"
#include "../h/game.h"
#include "../h/net_cmds.h"     // PlayerData, error_get_str
#include "../h/CUnitColor.h"   // get_slot_mapping
#include "../h/TrigAct.h"      // IsNamedComputerPlayerType
#include "../h/Storm.h"
#include "../gen/globals117.h"

// Globals this TU reaches, dual-spelled, with one #ifdef per name rather than
// one over the block: a single guard would undeclare every name under it.

// 0x536720, the expansion gate; unregistered, spelled as gametype.cpp does.
#ifndef gbIsExpansion
#ifdef OC_DLL_MODE
#define gbIsExpansion (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE gbIsExpansion;
#endif
#endif

// 0x526524, the campaign mission; unregistered, spelled as gluBrfAct.cpp does.
#ifndef word_526524_g
#ifdef OC_DLL_MODE
#define word_526524_g (*reinterpret_cast<WORD *>(OC_VA(0x526524u)))
#else
extern WORD word_526524_g;
#endif
#endif

// The bodies, in oracle VA order.

// Gives every named computer slot its race's stat_txt name, then blanks slot
// 2's name on two campaign missions.
// The empty Terran arm must stay a statement, or the switch loses its case 1
// test. `short` and an int copy of the name byte keep the shipped form's
// mission tail if-converted as in the oracle.
void AssignAINames()
{
  int nBase = gbIsExpansion ? 0x91 : 0x1D;
  for (int i = 0; i < 8; i++) {
    BYTE bStr = (BYTE)nBase;
    if (!IsNamedComputerPlayerType(gPlayerData[i].nType))
      continue;
    switch (gPlayerData[i].nRace) {
    case 0:
      bStr = (BYTE)nBase + 8;
      break;
    case 1:
      bStr += 0;
      break;
    case 2:
      bStr = (BYTE)nBase + 0x10;
      break;
    }
    bStr += (BYTE)get_slot_mapping(i);
    const char *psz = error_get_str(bStr);
    if (psz == NULL)
      psz = error_get_str(0xB);
    SStrCopy(gPlayerData[i].szName, psz, 0x19);
  }
  // Hyperion: the computers past 7 take the eight names again with a numeral,
  // slot 8 the first name's second, and so on past neutral's 11
  for (int i = PLAYER_LOBBY_117; i < PlayerLobbySlots(); i++) {
    if (i == PLAYER_NEUTRAL || !IsNamedComputerPlayerType(gPlayerData[i].nType))
      continue;
    int nStr = nBase + (gPlayerData[i].nRace == 0 ? 8 : gPlayerData[i].nRace == 2 ? 0x10 : 0) + i % 8;
    const char *psz = error_get_str((BYTE)nStr);
    if (psz == NULL)
      psz = error_get_str(0xB);
    static const char *const kNumerals[] = {"", " II", " III", " IV", " V", " VI", " VII", " VIII", " IX"};
    char szName[0x19];
    SStrCopy(szName, psz, 0x19);
    SStrPack(szName, kNumerals[(i / 8) % (sizeof kNumerals / sizeof kNumerals[0])], 0x19);
    SStrCopy(gPlayerData[i].szName, szName, 0x19);
  }
  if (gbMultiPlayerMode)
    return;
  short wMission = word_526524_g;
  if (wMission == 0)
    return;
  if (wMission == 0xF) {
    gPlayerData[2].szName[0] = 0;
  } else {
    int c = (BYTE)gPlayerData[2].szName[0];
    if (wMission == 0x11)
      c = 0;
    gPlayerData[2].szName[0] = c;
  }
}
