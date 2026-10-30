// map_load.cpp -- the saved-map payload's read and write, and the current map's
// file name (0x46B0B0 to 0x46B910). Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../h/map_load.h"
#include "../h/maphdr.h"      // map_load_1 / LoadMapPass1 / LoadMapPass2
#include "../h/game.h"        // LoadMapData / PreloadMap
#include "../h/fd_user.h"     // set_map_filename
#include "../h/net_slot.h"    // SyncSlots / set_selectable_race
#include "../h/forces.h"      // LoadForceSettings / ApplyPlayerForces
#include "../h/TrigAct.h"     // get_map_string
#include "../h/Storm.h"       // SStrCopy
#include "../h/saveload.h"    // compress / decompress
#include "../h/signature.h"   // sig_validate_certificate
#include <stddef.h>           // offsetof
#include <string.h>           // strrchr, memset

#ifndef NULL
#define NULL 0
#endif

// 0x57 == ERROR_INVALID_PARAMETER.
#define kErrInvalidParameter 0x57

// Storm imports, declared here rather than in src117/h/Storm.h, where a second
// spelling of a name would conflict. SStrChrR is spelled as gamedata.cpp and
// uiSingle.cpp spell it. The extern "C" block gives the undecorated
// _SStrChrR@8 and _SFileLoadFileEx@28 symbols storm_imports.def supplies.
extern "C" {
void __stdcall SErrSetLastError(DWORD dwErrCode);
char *__stdcall SStrChrR(const char *pszStr, char chFind);
// Hyperion: dwArg6 is an OVERLAPPED pointer to Storm, and the game passes 0;
// pointer-wide for the x64 build.
BOOL __stdcall SFileLoadFileEx(HANDLE hMpq, const char *pszFileName,
                               void **ppvBuffer, DWORD *pdwBytes, DWORD dwArg4,
                               DWORD dwArg5, DWORD_PTR dwArg6);
BOOL __stdcall SFileUnloadFile(void *pvBuffer);
}  // extern "C"

// The 12 game slots at 0x673D48. maphdr.cpp views the same array from +8 as
// oc_player_t, so bOwner and bRace land at +0x08 and +0x09 here.
struct mapslot_t {
  /*0x00*/ DWORD dwIndex;
  /*0x04*/ DWORD dwStormId;
  /*0x08*/ BYTE bOwner;      // zeroed with bRace as one word store
  /*0x09*/ BYTE bRace;
  /*0x0A*/ BYTE bTeam;
  /*0x0B*/ BYTE _pad0B[0x19];
};
static_assert(offsetof(mapslot_t, dwIndex) == 0x00, "mapslot_t::dwIndex");
static_assert(offsetof(mapslot_t, dwStormId) == 0x04, "mapslot_t::dwStormId");
static_assert(offsetof(mapslot_t, bOwner) == 0x08, "mapslot_t::bOwner");
static_assert(offsetof(mapslot_t, bRace) == 0x09, "mapslot_t::bRace");
static_assert(offsetof(mapslot_t, bTeam) == 0x0A, "mapslot_t::bTeam");
static_assert(sizeof(mapslot_t) == 0x24, "the walk steps `sub esi, 24h` and memsets 1B0h");

// Globals this TU reaches, dual-spelled, one #ifdef per name.
#ifdef OC_DLL_MODE
  #ifndef techdata_oreCost_g
  #define techdata_oreCost_g (reinterpret_cast<WORD *>(OC_VA(0x51F760u)))
  #endif
  #ifndef techdata_gasCost_g
  #define techdata_gasCost_g (reinterpret_cast<WORD *>(OC_VA(0x51F7B8u)))
  #endif
  #ifndef techdata_timeCost_g
  #define techdata_timeCost_g (reinterpret_cast<WORD *>(OC_VA(0x51F810u)))
  #endif
  #ifndef techdata_energy_g
  #define techdata_energy_g (reinterpret_cast<WORD *>(OC_VA(0x51F868u)))
  #endif
  #ifndef guwUnitEnergy
  #define guwUnitEnergy (reinterpret_cast<WORD *>(OC_VA(0x520540u)))
  #endif
  #ifndef gxUnitLife
  #define gxUnitLife (reinterpret_cast<LONG *>(OC_VA(0x520708u)))
  #endif
  #ifndef gubUnitArmor_g
  #define gubUnitArmor_g (reinterpret_cast<BYTE *>(OC_VA(0x521D90u)))
  #endif
  #ifndef guwUnitMineralsCost
  #define guwUnitMineralsCost (reinterpret_cast<WORD *>(OC_VA(0x523520u)))
  #endif
  #ifndef guwUnitGasesCost
  #define guwUnitGasesCost (reinterpret_cast<WORD *>(OC_VA(0x5236E8u)))
  #endif
  #ifndef guwUnitTimeToBuild
  #define guwUnitTimeToBuild (reinterpret_cast<WORD *>(OC_VA(0x5238B0u)))
  #endif
  #ifndef guwUnitCustomName_g
  #define guwUnitCustomName_g (reinterpret_cast<WORD *>(OC_VA(0x524458u)))
  #endif
  #ifndef upg_oreCostBase_g
  #define upg_oreCostBase_g (reinterpret_cast<WORD *>(OC_VA(0x5248D0u)))
  #endif
  #ifndef upg_oreCostFactor_g
  #define upg_oreCostFactor_g (reinterpret_cast<WORD *>(OC_VA(0x524950u)))
  #endif
  #ifndef upg_gasCostBase_g
  #define upg_gasCostBase_g (reinterpret_cast<WORD *>(OC_VA(0x5249D0u)))
  #endif
  #ifndef upg_gasCostFactor_g
  #define upg_gasCostFactor_g (reinterpret_cast<WORD *>(OC_VA(0x524A50u)))
  #endif
  #ifndef upg_timeCostBase_g
  #define upg_timeCostBase_g (reinterpret_cast<WORD *>(OC_VA(0x524AD0u)))
  #endif
  #ifndef upg_timeCostFactor_g
  #define upg_timeCostFactor_g (reinterpret_cast<WORD *>(OC_VA(0x524B50u)))
  #endif
  #ifndef wpn_dmgAmt_g
  #define wpn_dmgAmt_g (reinterpret_cast<WORD *>(OC_VA(0x525C80u)))
  #endif
  #ifndef wpn_dmgBonus_g
  #define wpn_dmgBonus_g (reinterpret_cast<WORD *>(OC_VA(0x525D88u)))
  #endif
  #ifndef sig_pubkey_g
  #define sig_pubkey_g (reinterpret_cast<const BYTE *>(OC_VA(0x4EEC90u)))
  #endif
  #ifndef sig_exponent_g
  #define sig_exponent_g (reinterpret_cast<const BYTE *>(OC_VA(0x4EED90u)))
  #endif
  #ifndef gszMapFileTbl
  #define gszMapFileTbl (reinterpret_cast<char **>(OC_VA(0x51ABB8u)))
  #endif
  #ifndef word_526524_g
  #define word_526524_g (*reinterpret_cast<WORD *>(OC_VA(0x526524u)))
  #endif
  #ifndef MapFileName
  #define MapFileName (reinterpret_cast<char *>(OC_VA(0x52701Cu)))
  #endif
  #ifndef gszMapTitle_g
  #define gszMapTitle_g (reinterpret_cast<char *>(OC_VA(0x527120u)))
  #endif
  #ifndef gMapSlots_g
  #define gMapSlots_g (reinterpret_cast<mapslot_t *>(OC_VA(0x673D48u)))
  #endif
  #ifndef sgbIsMapFilenameSet
  #define sgbIsMapFilenameSet (*reinterpret_cast<DWORD *>(OC_VA(0x666564u)))
  #endif
  #ifndef sglBytes
  #define sglBytes (*reinterpret_cast<DWORD *>(OC_VA(0x67CC74u)))
  #endif
  #ifndef sgpMap
  #define sgpMap (*reinterpret_cast<void **>(OC_VA(0x67CC78u)))
  #endif
  #ifndef gPlayback
  #define gPlayback (*reinterpret_cast<DWORD *>(OC_VA(0x67CC90u)))
  #endif
#else
extern WORD      techdata_oreCost_g[44];  // 0x51F760
extern WORD      techdata_gasCost_g[44] OC_UNALIGNED;  // 0x51F7B8
extern WORD      techdata_timeCost_g[44]; // 0x51F810
extern WORD      techdata_energy_g[44] OC_UNALIGNED;   // 0x51F868
extern WORD      guwUnitEnergy[228] OC_UNALIGNED;      // 0x520540
extern LONG      gxUnitLife[228] OC_UNALIGNED;         // 0x520708
extern BYTE      gubUnitArmor_g[228] OC_UNALIGNED;     // 0x521D90
extern WORD      guwUnitMineralsCost[228] OC_UNALIGNED; // 0x523520
extern WORD      guwUnitGasesCost[228] OC_UNALIGNED;   // 0x5236E8
extern WORD      guwUnitTimeToBuild[228] OC_UNALIGNED; // 0x5238B0
extern WORD      guwUnitCustomName_g[228] OC_UNALIGNED; // 0x524458
extern WORD      upg_oreCostBase_g[61];   // 0x5248D0
extern WORD      upg_oreCostFactor_g[61]; // 0x524950
extern WORD      upg_gasCostBase_g[61];   // 0x5249D0
extern WORD      upg_gasCostFactor_g[61]; // 0x524A50
extern WORD      upg_timeCostBase_g[61];  // 0x524AD0
extern WORD      upg_timeCostFactor_g[61]; // 0x524B50
extern WORD      wpn_dmgAmt_g[130] OC_UNALIGNED;       // 0x525C80
extern WORD      wpn_dmgBonus_g[130] OC_UNALIGNED;     // 0x525D88
extern const BYTE sig_pubkey_g[0x100];   // 0x4EEC90 (.rdata)
extern const BYTE sig_exponent_g[4];     // 0x4EED90 (.rdata)
extern char      *gszMapFileTbl[65];  // Hyperion: mapdata.dat's 65 rows       // 0x51ABB8  UNREGISTERED
extern WORD       word_526524_g;         // 0x526524
extern char       MapFileName[0x104] OC_UNALIGNED;    // 0x52701C
extern char       gszMapTitle_g[0x20];   // 0x527120
extern DWORD      sgbIsMapFilenameSet;   // 0x666564
extern PlayerTable<mapslot_t, PlayerTagNetData> gMapSlots_g;       // 0x673D48
extern DWORD      sglBytes;              // 0x67CC74
extern void      *sgpMap;                // 0x67CC78
extern DWORD      gPlayback;             // 0x67CC90
#endif

#include "../gen/globals117.h"

// The bodies, in oracle VA order.

// GetCurrentMap -- the whole path when the argument is set, the file name alone
// when it is not.
void *__fastcall GetCurrentMap(int nWhich) {
  if (nWhich)
    return MapFileName;
  {
    char *pszTail = strrchr(MapFileName, '\\');
    if (!pszTail)
      return MapFileName;
    return pszTail + 1;
  }
}

// map_validate_signature -- load the map file and run its Blizzard certificate.
// The out-parameter carries the certificate result; the return says only
// whether the file was readable.
BOOL map_validate_signature(const char *pszName, int *pnStatus) {
  char szLeaf[0x104];
  void *pvData;
  DWORD dwSize;
  int nLoaded;
  int nSigned;
  if (!pnStatus) {
    SErrSetLastError(kErrInvalidParameter);
    return FALSE;
  }
  nLoaded = 0;
  nSigned = 0;
  if (SFileLoadFileEx(NULL, pszName, &pvData, &dwSize, 0, 1, 0)) {
    // The larger of the two separators, compared unsigned, so a NULL loses to
    // any hit; NULL on both arms leaves the whole name.
    char *pszBack;
    char *pszFwd;
    const char *pszLeaf;
    nLoaded = 1;
    szLeaf[0] = 0;
    pszBack = SStrChrR(pszName, '\\');
    pszFwd = SStrChrR(pszName, '/');
    pszLeaf = pszBack > pszFwd ? pszBack : pszFwd;
    // Two calls, not a ternary argument, as 1.17 has them.
    if (pszLeaf)
      SStrCopy(szLeaf, pszLeaf + 1, 0x104);
    else
      SStrCopy(szLeaf, pszName, 0x104);
    nSigned = sig_validate_certificate(szLeaf, pvData, dwSize, sig_pubkey_g,
                                       0x100, sig_exponent_g, 4);
    SFileUnloadFile(pvData);
  }
  *pnStatus = nSigned != 0;
  return nLoaded;
}

// read_save_mapdata -- restore the units.dat, weapons.dat, upgrades.dat and
// techdata.dat tables a saved game carries. Saves at version 2 or below hold
// the original extents, so those tables are cleared to their full length and
// only a prefix is read back.
int read_save_mapdata(void *pFile, WORD wVersion) {
  if (!decompress(gxUnitLife, 0x390, pFile))
    return 0;
  if (!decompress(guwUnitEnergy, 0x1C8, pFile))
    return 0;
  if (!decompress(gubUnitArmor_g, 0xE4, pFile))
    return 0;
  if (!decompress(guwUnitTimeToBuild, 0x1C8, pFile))
    return 0;
  if (!decompress(guwUnitMineralsCost, 0x1C8, pFile))
    return 0;
  if (!decompress(guwUnitGasesCost, 0x1C8, pFile))
    return 0;
  if (!decompress(guwUnitCustomName_g, 0x1C8, pFile))
    return 0;
  if (wVersion <= 2) {
    memset(wpn_dmgAmt_g, 0, 0x104);
    if (!decompress(wpn_dmgAmt_g, 0xC8, pFile))
      return 0;
    memset(wpn_dmgBonus_g, 0, 0x104);
    if (!decompress(wpn_dmgBonus_g, 0xC8, pFile))
      return 0;
    memset(upg_oreCostBase_g, 0, 0x7A);
    if (!decompress(upg_oreCostBase_g, 0x5C, pFile))
      return 0;
    memset(upg_oreCostFactor_g, 0, 0x7A);
    if (!decompress(upg_oreCostFactor_g, 0x5C, pFile))
      return 0;
    memset(upg_gasCostBase_g, 0, 0x7A);
    if (!decompress(upg_gasCostBase_g, 0x5C, pFile))
      return 0;
    memset(upg_gasCostFactor_g, 0, 0x7A);
    if (!decompress(upg_gasCostFactor_g, 0x5C, pFile))
      return 0;
    memset(upg_timeCostBase_g, 0, 0x7A);
    if (!decompress(upg_timeCostBase_g, 0x5C, pFile))
      return 0;
    memset(upg_timeCostFactor_g, 0, 0x7A);
    if (!decompress(upg_timeCostFactor_g, 0x5C, pFile))
      return 0;
    memset(techdata_oreCost_g, 0, 0x58);
    if (!decompress(techdata_oreCost_g, 0x30, pFile))
      return 0;
    memset(techdata_gasCost_g, 0, 0x58);
    if (!decompress(techdata_gasCost_g, 0x30, pFile))
      return 0;
    memset(techdata_timeCost_g, 0, 0x58);
    if (!decompress(techdata_timeCost_g, 0x30, pFile))
      return 0;
    memset(techdata_energy_g, 0, 0x58);
    if (!decompress(techdata_energy_g, 0x30, pFile))
      return 0;
  } else {
    if (!decompress(wpn_dmgAmt_g, 0x104, pFile))
      return 0;
    if (!decompress(wpn_dmgBonus_g, 0x104, pFile))
      return 0;
    if (!decompress(upg_oreCostBase_g, 0x7A, pFile))
      return 0;
    if (!decompress(upg_oreCostFactor_g, 0x7A, pFile))
      return 0;
    if (!decompress(upg_gasCostBase_g, 0x7A, pFile))
      return 0;
    if (!decompress(upg_gasCostFactor_g, 0x7A, pFile))
      return 0;
    if (!decompress(upg_timeCostBase_g, 0x7A, pFile))
      return 0;
    if (!decompress(upg_timeCostFactor_g, 0x7A, pFile))
      return 0;
    if (!decompress(techdata_oreCost_g, 0x58, pFile))
      return 0;
    if (!decompress(techdata_gasCost_g, 0x58, pFile))
      return 0;
    if (!decompress(techdata_timeCost_g, 0x58, pFile))
      return 0;
    if (!decompress(techdata_energy_g, 0x58, pFile))
      return 0;
  }
  return 1;
}

// LoadMapData -- pass 2 over either the replay's in-memory image or the current
// map file.
BOOL LoadMapData() {
  if (gPlayback)
    return map_load_1(sgpMap, sglBytes, 2, NULL);
  if (!MapFileName[0])
    return FALSE;
  return LoadMapPass2(MapFileName) != 0;
}

// PreloadCampaignMap -- run PreloadMap over the campaign map the mission index names.
// The first register argument is never read here; EDX is forwarded.
BOOL PreloadCampaignMap(WORD wMission, void *pOut) {
  return PreloadMap(gszMapFileTbl[word_526524_g], pOut, 1) != 0;
}

// PreloadMap -- reset the slot table, run pass 1 over the named map (or the
// replay image), then publish the map's file name and title.
BOOL PreloadMap(char *pszMap, void *pSlots, int nFlag) {
  mapslot_t *p;
  int i;
  const char *pszTitle;
  MapFileName[0] = 0;
  if (!nFlag)
    word_526524_g = 0;
  PlayerClear(gMapSlots_g);  // Hyperion: and Hyperion's slots
  clear_selectable_races();
  memset(pSlots, 0, 0x20);
  if (gPlayback) {
    if (!map_load_1(sgpMap, sglBytes, 1, (mapheader_t *)pSlots))
      return FALSE;
  } else {
    if (!pszMap[0])
      return FALSE;
    if (!LoadMapPass1(pszMap, (mapheader_t *)pSlots))
      return FALSE;
  }
  i = 12;
  p = &gMapSlots_g.a[12];  // Hyperion: the end of 1.17's 12, not slot 12
  do {
    --p;
    --i;
    p->dwStormId = -1;
    p->dwIndex = i;
    if (p->bRace == 5) {
      p->bRace = 6;
      set_selectable_race((BYTE)i);
    }
    if (i >= 8) {
      p->bOwner = 0;
      p->bRace = 0;
      p->bTeam = 0;
    }
  } while (&p->bRace != &gMapSlots_g[0].bRace);
  // Hyperion: on a Hyperion map the slots past 7 are the lobby's too, as its HXPL
  // section sets them
  for (i = PLAYER_LOBBY_117; i < PlayerLobbySlots(); i++) {
    if (i == PLAYER_NEUTRAL)
      continue;
    const PlayerMapSlot *pMap = PlayersMapSlot(i);
    gMapSlots_g[i].dwStormId = -1;
    gMapSlots_g[i].dwIndex = i;
    gMapSlots_g[i].bOwner = pMap->bOwner;
    gMapSlots_g[i].bRace = pMap->bRace == 5 ? 6 : pMap->bRace;
    gMapSlots_g[i].bTeam = pMap->bForce;
  }
  SyncSlots();
  LoadForceSettings((BYTE *)pSlots + 4);
  ApplyPlayerForces();
  strrchr(pszMap, '\\');
  SStrCopy(MapFileName, pszMap, 0x104);
  if (!nFlag)
    word_526524_g = 0;
  sgbIsMapFilenameSet = 1;
  pszTitle = *(WORD *)pSlots != 0 ? get_map_string(*(WORD *)pSlots) : "";
  SStrCopy(gszMapTitle_g, pszTitle, 0x20);
  return TRUE;
}

// write_save_mapdata -- read_save_mapdata's mirror, always at the Brood War
// extents. Every failure shares one exit.
int write_save_mapdata(void *pFile) {
  if (!compress(gxUnitLife, 0x390, pFile))
    return 0;
  if (!compress(guwUnitEnergy, 0x1C8, pFile))
    return 0;
  if (!compress(gubUnitArmor_g, 0xE4, pFile))
    return 0;
  if (!compress(guwUnitTimeToBuild, 0x1C8, pFile))
    return 0;
  if (!compress(guwUnitMineralsCost, 0x1C8, pFile))
    return 0;
  if (!compress(guwUnitGasesCost, 0x1C8, pFile))
    return 0;
  if (!compress(guwUnitCustomName_g, 0x1C8, pFile))
    return 0;
  if (!compress(wpn_dmgAmt_g, 0x104, pFile))
    return 0;
  if (!compress(wpn_dmgBonus_g, 0x104, pFile))
    return 0;
  if (!compress(upg_oreCostBase_g, 0x7A, pFile))
    return 0;
  if (!compress(upg_oreCostFactor_g, 0x7A, pFile))
    return 0;
  if (!compress(upg_gasCostBase_g, 0x7A, pFile))
    return 0;
  if (!compress(upg_gasCostFactor_g, 0x7A, pFile))
    return 0;
  if (!compress(upg_timeCostBase_g, 0x7A, pFile))
    return 0;
  if (!compress(upg_timeCostFactor_g, 0x7A, pFile))
    return 0;
  if (!compress(techdata_oreCost_g, 0x58, pFile))
    return 0;
  if (!compress(techdata_gasCost_g, 0x58, pFile))
    return 0;
  if (!compress(techdata_timeCost_g, 0x58, pFile))
    return 0;
  return compress(techdata_energy_g, 0x58, pFile) != 0;
}

// set_map_filename -- publish a map file name. The strrchr result is discarded.
void set_map_filename(intptr_t nArg0, int nArg1) {
  strrchr((const char *)nArg0, '\\');
  SStrCopy(MapFileName, (const char *)nArg0, 0x104);
  sgbIsMapFilenameSet = 1;
  if (!nArg1)
    word_526524_g = 0;
}
