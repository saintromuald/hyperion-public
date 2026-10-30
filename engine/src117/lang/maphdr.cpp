// maphdr.cpp -- the CHK map-header chunk handlers and the map file's lifecycle:
// loading, versioning and authenticating a map and placing what it holds.
// Functions are in ascending oracle VA order. The line numbers passed to Storm
// are 1.17's __LINE__ values, written as constants.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../h/poollimits.h"    // Hyperion: PoolsMapUnit, the map's units
#include "../h/typelimits.h"    // Hyperion: UnitTypeValid
#include "../port/gameplay/port_eud.h"  // Hyperion: EUD maps
#include "../port/render/port_maptext.h"  // Hyperion: map text
#include "CUnitSearches.h"   // TTBaseUnitType
#include "Storm.h"
#include "maphdr.h"
#include "header.h"       // sub_464560
#include "map_load.h"     // GetCurrentMap / map_validate_signature
#include "gds_appaddex.h"  // app_add_exit
#include "gds_palette.h"   // vid_init_cycles / vid_set_cycle / vid_start_cycle
#include "CUnit.h"       // apply_liftoff / SetHangarContents / unit_create
#include "CSprite.h"     // CSprite::SetShadowOffset / SetAnimation / wPosX
#include "CUnitMisc.h"   // get_unit_name_from_type (unit_create)
#include "CThingy.h"     // CThingy::AllocThingy (maphdr_THG2)
#include "TrigAct.h"     // getUnitProps, CreateUnitAt
#include "victory.h"     // ExtMapRegions, psnprintf
#include "gametype.h"    // IsScenarioGame
#include "statbtn.h"     // OrderAllowed
#include "saveload.h"    // set_upgrade_level / set_max_upgrade_level /
                         // set_tech_available / set_tech_researched
#include "net_cmds.h"    // error_get_str (unit_create)
#include "net_slot.h"    // is_race_selectable (maphdr_UNIT)
#include "snd.h"         // load_array_data (maphdr_data_init)
#include "CUnitProtoss.h"  // RequestJuiceUpdate (maphdr_UNIT)
#include "sai.h"        // sai_map_init (maphdr_MTXM)
#include "collide.h"    // mtx_unplace_bldg / mtx_place_bldg (maphdr_MTXM)
#include "gamemap.h"     // gamemap_choose_center
#include "placebox.h"    // PlaceStartingUnits / TeamPlaceStartingUnits
#include "creep.h"      // unit_type_spreads_creep / spread_creep_completely
                        // (unit_create)
#include "point16.h"     // point16_t (setup_startloc)
#include "Teams.h"       // IsTeamGame
#include "gluRdy.h"      // addBriefingTrigger
#include "../port/data/port_map.h"  // Hyperion: PortMapStringsWide, PortMapColorsSet, PortMapColorsBegin
#include "../port/platform/port_standalone.h"  // Hyperion: PortGameVcod
#include "../port/render/port_view.h"  // Hyperion: g_portView
#include <stddef.h>      // offsetof (the layout static_asserts)
#include <string.h>      // memset
#include "../h/gamedata.h"  // Hyperion: array_data_t
#include "../h/maplimits.h"  // Hyperion: maps past 256 tiles
#include "../port/data/port_moddata.h"  // Hyperion: ModRacesWithout, ModTechsForGame
#include "../h/handicap.h"  // Hyperion: a player's handicap
#include "../h/mutators.h"  // Hyperion: what a resource holds

#ifndef NULL
#define NULL 0
#endif

// 0x57 == ERROR_INVALID_PARAMETER, as src117/lang/TrigAct.cpp uses it.
#define kErrInvalidParameter 0x57

// Storm imports, declared here as src117/lang/TrigAct.cpp declares its own;
// src117/storm_imports.def supplies them by ordinal (SMemZero 494, SStrPrintf
// 578, SErrSetLastError 465, SFileCloseFile 253, SFileOpenFileEx 268,
// SFileCloseArchive 252, SFileOpenArchive 266).
extern "C" void __stdcall SErrSetLastError(DWORD dwErrCode);
extern "C" void __stdcall SMemZero(void *pDest, DWORD dwSize);
extern "C" BOOL __stdcall SFileOpenArchive(const char *pszName, DWORD dwPriority,
                                           DWORD dwFlags, HANDLE *phArchive);
extern "C" BOOL __stdcall SFileCloseArchive(HANDLE hArchive);
extern "C" BOOL __stdcall SFileOpenFileEx(HANDLE hMpq, const char *pszFileName,
                                          DWORD dwSearchScope, HANDLE *phFile);
extern "C" BOOL __stdcall SFileCloseFile(HANDLE hFile);
extern "C" int __cdecl SStrPrintf(char *pszDest, size_t cchDest,
                                  const char *pszFmt, ...);

// Game callees no other header declares, spelled as the other files that
// declare them do.

// permute_colors_by_map -- 0x41D5D0 (CUnitColor.cpp); maphdr_COLR, its only
// caller, passes the buffer and 8.
void __fastcall permute_colors_by_map(void *pData, int nSize);
// TryAddTrigger, spelled as src117/lang/gametype.cpp spells it.
int __fastcall TryAddTrigger(void *pTrigger, int nFlags);
// MemFileLoad -- 0x444990 (gamedata.cpp), spelled as src117/h/gamedata.h spells
// it: ECX the file name, then the size out-slot, a quiet flag, Storm's search
// scope and the owner tag pair. Every caller passes 0 in EDX.
void *__fastcall MemFileLoad(const char *pszFile, DWORD_PTR dwArg2, DWORD *pdwSize,
                             int nFlags, int nScope, const char *pszOwnerFile,
                             int nOwnerLine);
// map_msg_error -- 0x46B070: unit_create sets only DL, so the second parameter
// is a BYTE.
void __fastcall map_msg_error(const char *pszMsg, BYTE bType);
// mem_free -- 0x444CA0 (gamedata.cpp), spelled as src117/lang/gametype.cpp
// spells it.
void mem_free(void *pv, const char *pszTag, int nLine);
// The per-player upgrade-level rows maphdr_data_init resets. 46 is upgrades.dat's
// original row count and 15 the Brood War extension, and upg_repeats carries
// both, so one 0x3D-byte record feeds two arrays.
struct upgmax_t { BYTE b[0x2E]; };
struct upgmaxbw_t { BYTE b[0x0F]; };
struct upgblock_t {
  /*0x0000*/ upgmax_t a[12];
  /*0x0228*/ BYTE gap[0x536558 - 0x534368 - 12 * 0x2E];
  /*0x21F0*/ upgmaxbw_t b[12];
};
static_assert(offsetof(upgblock_t, a) == 0x0000, "upgblock_t::a");
static_assert(offsetof(upgblock_t, gap) == 0x0228, "upgblock_t::gap");
static_assert(offsetof(upgblock_t, b) == 0x21F0, "upgblock_t::b");
static_assert(sizeof(upgblock_t) == 0x22A4, "upgblock_t size");

// Game globals, one #ifndef per name inside the DLL branch.
#ifdef OC_DLL_MODE
  #ifndef sgCycleTbl
  #define sgCycleTbl (reinterpret_cast<BYTE **>(OC_VA(0x5089B0u)))
  #endif
  #ifndef g_LocalNationID
  #define g_LocalNationID (*reinterpret_cast<DWORD *>(OC_VA(0x508D50u)))
  #endif
  #ifndef gulUnitTypeFlags
  #define gulUnitTypeFlags (reinterpret_cast<DWORD *>(OC_VA(0x521660u)))
  #endif
  #ifndef upg_repeats
  #define upg_repeats (reinterpret_cast<BYTE *>(OC_VA(0x524D8Cu)))
  #endif
  #ifndef byte_526526
  #define byte_526526 (reinterpret_cast<char *>(OC_VA(0x526526u)))
  #endif
  #ifndef gPORTDATAArrayDataTbl_g
  #define gPORTDATAArrayDataTbl_g (reinterpret_cast<array_data_t*>(OC_VA(0x501368u)))
  #endif
  #ifndef gTECHDATAArrayDataTbl_g
  #define gTECHDATAArrayDataTbl_g (reinterpret_cast<array_data_t*>(OC_VA(0x501460u)))
  #endif
  #ifndef gUPGRADESArrayDataTbl
  #define gUPGRADESArrayDataTbl (reinterpret_cast<array_data_t*>(OC_VA(0x501788u)))
  #endif
  #ifndef gWEAPONSArrayDataTbl_g
  #define gWEAPONSArrayDataTbl_g (reinterpret_cast<array_data_t*>(OC_VA(0x501828u)))
  #endif
  #ifndef gbTechAvailSC
  #define gbTechAvailSC (reinterpret_cast<BYTE *>(OC_VA(0x534104u)))
  #endif
  #ifndef gbTechResearchedSC
  #define gbTechResearchedSC (reinterpret_cast<BYTE *>(OC_VA(0x534224u)))
  #endif
  #ifndef byte_534368_g
  #define byte_534368_g (reinterpret_cast<upgmax_t *>(OC_VA(0x534368u)))
  #endif
  #ifndef gbUpgradeLevelSC
  #define gbUpgradeLevelSC (reinterpret_cast<BYTE *>(OC_VA(0x534590u)))
  #endif
  #ifndef gbTechAvailBW
  #define gbTechAvailBW (reinterpret_cast<BYTE *>(OC_VA(0x536318u)))
  #endif
  #ifndef gbTechResearchedBW
  #define gbTechResearchedBW (reinterpret_cast<BYTE *>(OC_VA(0x536408u)))
  #endif
  #ifndef gTechInProgress
  #define gTechInProgress (reinterpret_cast<BYTE *>(OC_VA(0x536510u)))
  #endif
  #ifndef byte_536558_g
  #define byte_536558_g (reinterpret_cast<upgmaxbw_t *>(OC_VA(0x536558u)))
  #endif
  #ifndef gbUpgradeLevelBW
  #define gbUpgradeLevelBW (reinterpret_cast<BYTE *>(OC_VA(0x5365DEu)))
  #endif
  #ifndef gUpgradeInProgress
  #define gUpgradeInProgress (reinterpret_cast<BYTE *>(OC_VA(0x5366C0u)))
  #endif
  #ifndef gbSlotRemap_g
  #define gbSlotRemap_g (reinterpret_cast<BYTE *>(OC_VA(0x536722u)))
  #endif
  #ifndef guwAttachedUnit
  #define guwAttachedUnit (reinterpret_cast<WORD *>(OC_VA(0x51FB90u)))
  #endif
  #ifndef guwUnitEnergy
  #define guwUnitEnergy (reinterpret_cast<WORD *>(OC_VA(0x520540u)))
  #endif
  #ifndef gxUnitLife
  #define gxUnitLife (reinterpret_cast<LONG *>(OC_VA(0x520708u)))
  #endif
  #ifndef gubUnitWeaponGnd
  #define gubUnitWeaponGnd (reinterpret_cast<BYTE *>(OC_VA(0x5211D8u)))
  #endif
  #ifndef gubUnitWeaponAir
  #define gubUnitWeaponAir (reinterpret_cast<BYTE *>(OC_VA(0x5213A8u)))
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
  #ifndef guwUnitCustomName
  #define guwUnitCustomName (reinterpret_cast<WORD *>(OC_VA(0x524458u)))
  #endif
  #ifndef wpn_dmgAmt_g
  #define wpn_dmgAmt_g (reinterpret_cast<WORD *>(OC_VA(0x525C80u)))
  #endif
  #ifndef wpn_dmgBonus_g
  #define wpn_dmgBonus_g (reinterpret_cast<WORD *>(OC_VA(0x525D88u)))
  #endif
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
  #ifndef gfEditFlags
  #define gfEditFlags (reinterpret_cast<WORD *>(OC_VA(0x524708u)))
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
  #ifndef gubUnitCountInfo
  #define gubUnitCountInfo (reinterpret_cast<BYTE *>(OC_VA(0x523C40u)))
  #endif
  #ifndef gPlayerData
  #define gPlayerData (reinterpret_cast<PlayerData *>(OC_VA(0x6726C8u)))
  #endif
  #ifndef gStartLocations
  #define gStartLocations (reinterpret_cast<point16_t *>(OC_VA(0x534A00u)))
  #endif
  #ifndef gwScreenTileX_g
  #define gwScreenTileX_g (*reinterpret_cast<WORD *>(OC_VA(0x5264B0u)))
  #endif
  #ifndef gwScreenTileY_g
  #define gwScreenTileY_g (*reinterpret_cast<WORD *>(OC_VA(0x5264B2u)))
  #endif
  #ifndef gwMapWidth
  #define gwMapWidth (*reinterpret_cast<WORD *>(OC_VA(0x5264B4u)))
  #endif
  #ifndef gwMapHeight
  #define gwMapHeight (*reinterpret_cast<WORD *>(OC_VA(0x5264B6u)))
  #endif
  #ifndef g_MapTileset
  #define g_MapTileset (*reinterpret_cast<WORD *>(OC_VA(0x5264BCu)))
  #endif
  #ifndef word_526524_g
  #define word_526524_g (*reinterpret_cast<WORD *>(OC_VA(0x526524u)))
  #endif
  #ifndef PlayerUnitsAvailable
  #define PlayerUnitsAvailable (reinterpret_cast<BYTE(*)[228]>(OC_VA(0x52655Cu)))
  #endif
  #ifndef gpMapStrs_g
  #define gpMapStrs_g (*reinterpret_cast<void **>(OC_VA(0x666440u)))
  #endif
  #ifndef gdwMapStrSize_g
  #define gdwMapStrSize_g (*reinterpret_cast<DWORD *>(OC_VA(0x666444u)))
  #endif
  #ifndef gpSquareMap
  #define gpSquareMap (*reinterpret_cast<DWORD **>(OC_VA(0x666454u)))
  #endif
  #ifndef gfpMIMap
  #define gfpMIMap (*reinterpret_cast<WORD **>(OC_VA(0x666458u)))
  #endif
  #ifndef gbValidateUnits_g
  #define gbValidateUnits_g (*reinterpret_cast<BYTE *>(OC_VA(0x672949u)))
  #endif
  #ifndef sgbLeagueId
  #define sgbLeagueId (*reinterpret_cast<BYTE *>(OC_VA(0x66FD3Cu)))
  #endif
  #ifndef gNetPlayerData_g
  #define gNetPlayerData_g (reinterpret_cast<PlayerData *>(OC_VA(0x673D48u)))
  #endif
  #ifndef ghScenArchive
  #define ghScenArchive (*reinterpret_cast<HANDLE *>(OC_VA(0x66FD4Cu)))
  #endif
  #ifndef gPlayback
  #define gPlayback (*reinterpret_cast<DWORD *>(OC_VA(0x67CC90u)))
  #endif
  #ifndef gpLoadGameFile
  #define gpLoadGameFile (*reinterpret_cast<void **>(OC_VA(0x6A5E34u)))
  #endif
#else
extern BYTE   *sgCycleTbl[8] OC_UNALIGNED;             // 0x5089B0  dword[tileset] -> cycle table; Hyperion: eight, a string follows
extern DWORD   g_LocalNationID;           // 0x508D50
extern DWORD   gulUnitTypeFlags[228] OC_UNALIGNED;     // 0x521660
extern BYTE    upg_repeats[64] OC_UNALIGNED;            // 0x524D8C
extern char    byte_526526[32] OC_UNALIGNED;           // 0x526526
extern array_data_t   gPORTDATAArrayDataTbl_g[7] OC_UNALIGNED; // 0x501368
extern array_data_t   gTECHDATAArrayDataTbl_g[12] OC_UNALIGNED; // 0x501460
extern array_data_t   gUPGRADESArrayDataTbl[13] OC_UNALIGNED;   // 0x501788
extern array_data_t   gWEAPONSArrayDataTbl_g[25] OC_UNALIGNED;  // 0x501828
extern BYTE    gbTechAvailSC[];           // 0x534104, 12 x 24 techs
extern BYTE    gbTechResearchedSC[];      // 0x534224
extern BYTE    gbMaxUpgradeLevelSC[];     // 0x534368
// Both upgrade-row arrays are spelled as one object, so that the six unrolled
// row copies are stored in 1.17's order.
#define gUpgBlock (*reinterpret_cast<upgblock_t *>(gbMaxUpgradeLevelSC))
#define byte_534368_g (gUpgBlock.a)
extern BYTE    gbUpgradeLevelSC[];        // 0x534590, 12 x 0x2E upgrades
// The Brood War bases are biased down by the first BW id (0x18 techs, 0x2E
// upgrades), so a table's first row is at base + that id.
extern BYTE    gbTechAvailBW[];           // 0x536318
extern BYTE    gbTechResearchedBW[];      // 0x536408
extern PlayerTechInProgressTable gTechInProgress;       // 0x536510
#define byte_536558_g (gUpgBlock.b)     // 0x536558
extern BYTE    gbUpgradeLevelBW[];        // 0x5365DE
extern PlayerUpgradeInProgressTable gUpgradeInProgress;    // 0x5366C0
extern BYTE    gbSlotRemap_g[12];         // 0x536722
extern WORD    guwAttachedUnit[228] OC_UNALIGNED;       // 0x51FB90
extern WORD    guwUnitEnergy[228] OC_UNALIGNED;         // 0x520540
extern LONG    gxUnitLife[228] OC_UNALIGNED;            // 0x520708
extern weapon_id_t gubUnitWeaponGnd[228] OC_UNALIGNED;      // 0x5211D8
extern weapon_id_t gubUnitWeaponAir[228] OC_UNALIGNED;      // 0x5213A8
extern BYTE    gubUnitArmor_g[228] OC_UNALIGNED;        // 0x521D90
extern WORD    guwUnitMineralsCost[228] OC_UNALIGNED;   // 0x523520
extern WORD    guwUnitGasesCost[228] OC_UNALIGNED;      // 0x5236E8
extern WORD    guwUnitTimeToBuild[228] OC_UNALIGNED;    // 0x5238B0
extern WORD    guwUnitCustomName[228] OC_UNALIGNED;     // 0x524458
extern WORD    wpn_dmgAmt_g[130] OC_UNALIGNED;          // 0x525C80
extern WORD    wpn_dmgBonus_g[130] OC_UNALIGNED;        // 0x525D88
extern WORD    techdata_oreCost_g[44];    // 0x51F760
extern WORD    techdata_gasCost_g[44] OC_UNALIGNED;    // 0x51F7B8
extern WORD    techdata_timeCost_g[44];   // 0x51F810
extern WORD    techdata_energy_g[44] OC_UNALIGNED;     // 0x51F868
extern WORD    gfEditFlags[228] OC_UNALIGNED;          // 0x524708
extern WORD    upg_oreCostBase_g[61];     // 0x5248D0
extern WORD    upg_oreCostFactor_g[61];   // 0x524950
extern WORD    upg_gasCostBase_g[61];     // 0x5249D0
extern WORD    upg_gasCostFactor_g[61];   // 0x524A50
extern WORD    upg_timeCostBase_g[61];    // 0x524AD0
extern WORD    upg_timeCostFactor_g[61];  // 0x524B50
extern BYTE    gubUnitCountInfo[228] OC_UNALIGNED;      // 0x523C40
extern PlayerTable<PlayerData, PlayerTagData> gPlayerData;          // 0x6726C8
extern PlayerTable<point16_t, PlayerTagStartLocations, 8> gStartLocations;       // 0x534A00
extern WORD    gwScreenTileX_g;           // 0x5264B0
extern WORD    gwScreenTileY_g;           // 0x5264B2
extern WORD    gwMapWidth;                // 0x5264B4
extern WORD    gwMapHeight;               // 0x5264B6
extern WORD    g_MapTileset;              // 0x5264BC
extern WORD    word_526524_g;             // 0x526524
extern PlayerUnitsAvailableTable PlayerUnitsAvailable;  // 0x52655C
extern void   *gpMapStrs_g;               // 0x666440
extern DWORD   gdwMapStrSize_g;           // 0x666444
extern DWORD  *gpSquareMap;               // 0x666454
extern WORD   *gfpMIMap;                  // 0x666458
extern BYTE    gbValidateUnits_g;         // 0x672949
extern BYTE    sgbLeagueId;               // 0x66FD3C
extern PlayerTable<PlayerData, PlayerTagNetData> gNetPlayerData_g;   // 0x673D48
extern HANDLE  ghScenArchive;             // 0x66FD4C
extern DWORD   gPlayback;                 // 0x67CC90
extern void   *gpLoadGameFile;            // 0x6A5E34
#endif
#ifdef OC_DLL_MODE
  #ifndef gbExpansion_g
  #define gbExpansion_g (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
  #endif
#else
extern BYTE    gbExpansion_g;             // 0x536720
#endif

// The scenario-only guard the handlers of TECS and UPGR, of UPGS and PTEC, and
// their Brood War twins open with. IsScenarioGame is called twice and the second result is unused, as
// 1.17 does; both operands are full-width masks, -(scenario != 0) and
// -(nSize == K), and the guard is their bitwise AND, not a short-circuit `&&`.
// The handlers spell them `IsScenarioGame(NULL) ? -1 : 0` and `(nSize != K) - 1`.

// maphdr_VCOD -- the map verification code. The chunk is checked against the
// copy of itself the exe carries as the KERNEL32 resource (type "VCOD", name
// #202): the same length, and the same sub_464560 digest over the net player
// table.
BOOL __stdcall maphdr_VCOD(void *pFile, int nSize, int a3) {
  // Three early exits goto one shared tail between the last guard and the success
  // body, and the last guard keeps a plain `return FALSE;`, as 1.17 lays it out.
  ULONG dwResSize;
  const void *pRes;
  // FindResourceA with name 202 and type "VCOD", 1.17's string at 0x4EEC1C.
  // Hyperion: the resource is Hyperion's own copy (src117/gen/data117.cpp),
  // where it was the exe file's when 1.17's image was not mapped.
  pRes = PortGameVcod(&dwResSize);
  if (!pRes)
    goto fail;
  if ((DWORD)nSize != dwResSize)
    return FALSE;
  {
  BYTE *pChunk = (BYTE *)SMemAlloc(nSize, "maphdr.cpp", 1172, 0);
  if (!pChunk)
    return FALSE;
  if (!ReadChunk(pFile, pChunk)) {
    SMemFree(pChunk, "maphdr.cpp", 1175, 0);
    return FALSE;
  }
  // sub_464560 digests the chunk seeded with the 432-byte net player table, the
  // chunk first and the resource second; the result is res == chunk.
  int nChunkSum = sub_464560(pChunk, nSize, gNetPlayerData_g.a, 0x1B0);
  int nResSum = sub_464560(const_cast<void *>(pRes), nSize, gNetPlayerData_g.a, 0x1B0);
  SMemFree(pChunk, "maphdr.cpp", 1183, 0);
  return nResSum == nChunkSum;
  }
fail:
  return FALSE;
}

// The map's "UNIS" section, Brood War extent: 0x1048 B. Every table is 228 rows
// except the last two, which are indexed by weapon and carry Brood War's 130.
struct chk_unis_bw_t {
  /*0x0000*/ BYTE  bUsesDefault[228];
  /*0x00E4*/ LONG  lHitPoints[228];
  /*0x0474*/ WORD  wShieldPoints[228];
  /*0x063C*/ BYTE  bArmor[228];
  /*0x0720*/ WORD  wBuildTime[228];
  /*0x08E8*/ WORD  wMineralCost[228];
  /*0x0AB0*/ WORD  wGasCost[228];
  /*0x0C78*/ WORD  wCustomName[228];
  /*0x0E40*/ WORD  wWeaponDamage[130];
  /*0x0F44*/ WORD  wWeaponBonus[130];
};
static_assert(offsetof(chk_unis_bw_t, bUsesDefault) == 0x0000, "chk_unis_bw_t::bUsesDefault");
static_assert(offsetof(chk_unis_bw_t, lHitPoints) == 0x00E4, "chk_unis_bw_t::lHitPoints");
static_assert(offsetof(chk_unis_bw_t, wShieldPoints) == 0x0474, "chk_unis_bw_t::wShieldPoints");
static_assert(offsetof(chk_unis_bw_t, bArmor) == 0x063C, "chk_unis_bw_t::bArmor");
static_assert(offsetof(chk_unis_bw_t, wBuildTime) == 0x0720, "chk_unis_bw_t::wBuildTime");
static_assert(offsetof(chk_unis_bw_t, wMineralCost) == 0x08E8, "chk_unis_bw_t::wMineralCost");
static_assert(offsetof(chk_unis_bw_t, wGasCost) == 0x0AB0, "chk_unis_bw_t::wGasCost");
static_assert(offsetof(chk_unis_bw_t, wCustomName) == 0x0C78, "chk_unis_bw_t::wCustomName");
static_assert(offsetof(chk_unis_bw_t, wWeaponDamage) == 0x0E40, "chk_unis_bw_t::wWeaponDamage");
static_assert(offsetof(chk_unis_bw_t, wWeaponBonus) == 0x0F44, "chk_unis_bw_t::wWeaponBonus");
static_assert(sizeof(chk_unis_bw_t) == 0x1048, "UNIS_Bwar: the guard and the SMemZero both say 1048h");

// 228 is units.dat's row count and doubles as its "no unit" sentinel; 130 is
// weapons.dat's row count and its "no weapon" sentinel.
#define kUnitNone 228
#define kWeaponNone 130

BOOL __stdcall maphdr_UNIS_Bwar(void *pFile, int nSize, int a3) {
  chk_unis_bw_t unis;
  BOOL bScenMask = IsScenarioGame(NULL) ? -1 : 0;
  IsScenarioGame(NULL);
  if (!(bScenMask & ((nSize == 0x1048) ? -1 : 0))) {
    SErrSetLastError(kErrInvalidParameter);
    return FALSE;
  }
  SMemZero(&unis, 0x1048);
  if (!ReadChunk(pFile, &unis))
    return FALSE;
  int i = 228;
  do {
    --i;
    if (!unis.bUsesDefault[i]) {
      // The weapon rows follow the subunit where the type has one.
      int nWeaponOwner = i;
      weapon_id_t bWeapon;
      gxUnitLife[i] = unis.lHitPoints[i];
      guwUnitEnergy[i] = unis.wShieldPoints[i];
      gubUnitArmor_g[i] = unis.bArmor[i];
      guwUnitTimeToBuild[i] = unis.wBuildTime[i];
      guwUnitMineralsCost[i] = unis.wMineralCost[i];
      guwUnitGasesCost[i] = unis.wGasCost[i];
      guwUnitCustomName[i] = unis.wCustomName[i];
      if (guwAttachedUnit[i] != kUnitNone)
        nWeaponOwner = guwAttachedUnit[i];
      bWeapon = gubUnitWeaponGnd[nWeaponOwner];
      if (bWeapon < kWeaponNone) {  // Hyperion: != in 1.17; a mod's weapon is past the map's
        wpn_dmgAmt_g[bWeapon] = unis.wWeaponDamage[bWeapon];
        wpn_dmgBonus_g[bWeapon] = unis.wWeaponBonus[bWeapon];
      }
      bWeapon = gubUnitWeaponAir[nWeaponOwner];
      if (bWeapon < kWeaponNone) {  // Hyperion: != in 1.17; a mod's weapon is past the map's
        wpn_dmgAmt_g[bWeapon] = unis.wWeaponDamage[bWeapon];
        wpn_dmgBonus_g[bWeapon] = unis.wWeaponBonus[bWeapon];
      }
    }
  } while (i != 0);
  return TRUE;
}

// The original StarCraft extent of "UNIS": 0xFD0 B, the Brood War record with
// 100-row weapon tables instead of 130, the whole 0x78-byte difference.
struct chk_unis_t {
  /*0x0000*/ BYTE  bUsesDefault[228];
  /*0x00E4*/ LONG  lHitPoints[228];
  /*0x0474*/ WORD  wShieldPoints[228];
  /*0x063C*/ BYTE  bArmor[228];
  /*0x0720*/ WORD  wBuildTime[228];
  /*0x08E8*/ WORD  wMineralCost[228];
  /*0x0AB0*/ WORD  wGasCost[228];
  /*0x0C78*/ WORD  wCustomName[228];
  /*0x0E40*/ WORD  wWeaponDamage[100];
  /*0x0F08*/ WORD  wWeaponBonus[100];
};
static_assert(offsetof(chk_unis_t, bUsesDefault) == 0x0000, "chk_unis_t::bUsesDefault");
static_assert(offsetof(chk_unis_t, lHitPoints) == 0x00E4, "chk_unis_t::lHitPoints");
static_assert(offsetof(chk_unis_t, wShieldPoints) == 0x0474, "chk_unis_t::wShieldPoints");
static_assert(offsetof(chk_unis_t, bArmor) == 0x063C, "chk_unis_t::bArmor");
static_assert(offsetof(chk_unis_t, wBuildTime) == 0x0720, "chk_unis_t::wBuildTime");
static_assert(offsetof(chk_unis_t, wMineralCost) == 0x08E8, "chk_unis_t::wMineralCost");
static_assert(offsetof(chk_unis_t, wGasCost) == 0x0AB0, "chk_unis_t::wGasCost");
static_assert(offsetof(chk_unis_t, wCustomName) == 0x0C78, "chk_unis_t::wCustomName");
static_assert(offsetof(chk_unis_t, wWeaponDamage) == 0x0E40, "chk_unis_t::wWeaponDamage");
static_assert(offsetof(chk_unis_t, wWeaponBonus) == 0x0F08, "chk_unis_t::wWeaponBonus");
static_assert(sizeof(chk_unis_t) == 0xFD0, "UNIS: the guard and the SMemZero both say 0FD0h");

BOOL __stdcall maphdr_UNIS(void *pFile, int nSize, int a3) {
  chk_unis_t unis;
  BOOL bScenMask = IsScenarioGame(NULL) ? -1 : 0;
  IsScenarioGame(NULL);
  if (!(bScenMask & ((nSize == 0xFD0) ? -1 : 0))) {
    SErrSetLastError(kErrInvalidParameter);
    return FALSE;
  }
  SMemZero(&unis, 0xFD0);
  if (!ReadChunk(pFile, &unis))
    return FALSE;
  int i = 228;
  do {
    --i;
    if (!unis.bUsesDefault[i]) {
      int nWeaponOwner = i;
      weapon_id_t bWeapon;
      gxUnitLife[i] = unis.lHitPoints[i];
      guwUnitEnergy[i] = unis.wShieldPoints[i];
      gubUnitArmor_g[i] = unis.bArmor[i];
      guwUnitTimeToBuild[i] = unis.wBuildTime[i];
      guwUnitMineralsCost[i] = unis.wMineralCost[i];
      guwUnitGasesCost[i] = unis.wGasCost[i];
      guwUnitCustomName[i] = unis.wCustomName[i];
      if (guwAttachedUnit[i] != kUnitNone)
        nWeaponOwner = guwAttachedUnit[i];
      bWeapon = gubUnitWeaponGnd[nWeaponOwner];
      if (bWeapon < kWeaponNone) {  // Hyperion: != in 1.17; a mod's weapon is past the map's
        wpn_dmgAmt_g[bWeapon] = unis.wWeaponDamage[bWeapon];
        wpn_dmgBonus_g[bWeapon] = unis.wWeaponBonus[bWeapon];
      }
      bWeapon = gubUnitWeaponAir[nWeaponOwner];
      if (bWeapon < kWeaponNone) {  // Hyperion: != in 1.17; a mod's weapon is past the map's
        wpn_dmgAmt_g[bWeapon] = unis.wWeaponDamage[bWeapon];
        wpn_dmgBonus_g[bWeapon] = unis.wWeaponBonus[bWeapon];
      }
    }
  } while (i != 0);
  return TRUE;
}

// The Brood War extent of the map's "UPGS" section: 61 rows, upgrades.dat's full
// count. 61 is odd, so one pad byte follows the byte array before the first
// WORD table, spelled out here.
struct chk_upgs_bw_t {
  /*0x000*/ BYTE bUsesDefault[61];
  /*0x03D*/ BYTE _pad3D;
  /*0x03E*/ WORD wOreCost[61];
  /*0x0B8*/ WORD wOreFactor[61];
  /*0x132*/ WORD wGasCost[61];
  /*0x1AC*/ WORD wGasFactor[61];
  /*0x226*/ WORD wTimeCost[61];
  /*0x2A0*/ WORD wTimeFactor[61];
};
static_assert(offsetof(chk_upgs_bw_t, bUsesDefault) == 0x000, "chk_upgs_bw_t::bUsesDefault");
static_assert(offsetof(chk_upgs_bw_t, wOreCost) == 0x03E, "chk_upgs_bw_t::wOreCost");
static_assert(offsetof(chk_upgs_bw_t, wOreFactor) == 0x0B8, "chk_upgs_bw_t::wOreFactor");
static_assert(offsetof(chk_upgs_bw_t, wGasCost) == 0x132, "chk_upgs_bw_t::wGasCost");
static_assert(offsetof(chk_upgs_bw_t, wGasFactor) == 0x1AC, "chk_upgs_bw_t::wGasFactor");
static_assert(offsetof(chk_upgs_bw_t, wTimeCost) == 0x226, "chk_upgs_bw_t::wTimeCost");
static_assert(offsetof(chk_upgs_bw_t, wTimeFactor) == 0x2A0, "chk_upgs_bw_t::wTimeFactor");
static_assert(sizeof(chk_upgs_bw_t) == 0x31A, "UPGS_Bwar: the guard and the SMemZero both say 31Ah");

BOOL __stdcall maphdr_UPGS_Bwar(void *pFile, int nSize, int a3) {
  chk_upgs_bw_t upgs;
  BOOL bScenMask = IsScenarioGame(NULL) ? -1 : 0;
  IsScenarioGame(NULL);
  if (!(bScenMask & ((nSize == 0x31A) ? -1 : 0))) {
    SErrSetLastError(kErrInvalidParameter);
    return FALSE;
  }
  SMemZero(&upgs, 0x31A);
  if (!ReadChunk(pFile, &upgs))
    return FALSE;
  int i = 61;
  do {
    --i;
    if (!upgs.bUsesDefault[i]) {
      upg_oreCostBase_g[i]    = upgs.wOreCost[i];
      upg_oreCostFactor_g[i]  = upgs.wOreFactor[i];
      upg_gasCostBase_g[i]    = upgs.wGasCost[i];
      upg_gasCostFactor_g[i]  = upgs.wGasFactor[i];
      upg_timeCostBase_g[i]   = upgs.wTimeCost[i];
      upg_timeCostFactor_g[i] = upgs.wTimeFactor[i];
    }
  } while (i != 0);
  return TRUE;
}

// The map's "UPGS" section (upgrade cost settings), 0x256 B on disk:
// upgrades.dat's original 46 rows, six WORD tables 92 B apart.
struct chk_upgs_t {
  /*0x000*/ BYTE bUsesDefault[46];
  /*0x02E*/ WORD wOreCost[46];
  /*0x08A*/ WORD wOreFactor[46];
  /*0x0E6*/ WORD wGasCost[46];
  /*0x142*/ WORD wGasFactor[46];
  /*0x19E*/ WORD wTimeCost[46];
  /*0x1FA*/ WORD wTimeFactor[46];
};
static_assert(offsetof(chk_upgs_t, bUsesDefault) == 0x000, "chk_upgs_t::bUsesDefault");
static_assert(offsetof(chk_upgs_t, wOreCost) == 0x02E, "chk_upgs_t::wOreCost");
static_assert(offsetof(chk_upgs_t, wOreFactor) == 0x08A, "chk_upgs_t::wOreFactor");
static_assert(offsetof(chk_upgs_t, wGasCost) == 0x0E6, "chk_upgs_t::wGasCost");
static_assert(offsetof(chk_upgs_t, wGasFactor) == 0x142, "chk_upgs_t::wGasFactor");
static_assert(offsetof(chk_upgs_t, wTimeCost) == 0x19E, "chk_upgs_t::wTimeCost");
static_assert(offsetof(chk_upgs_t, wTimeFactor) == 0x1FA, "chk_upgs_t::wTimeFactor");
static_assert(sizeof(chk_upgs_t) == 0x256, "UPGS: the guard and the SMemZero both say 256h");

// maphdr_UPGS (0x468340, 231 B).
BOOL __stdcall maphdr_UPGS(void *pFile, int nSize, int a3) {
  chk_upgs_t upgs;
  BOOL bScenMask = IsScenarioGame(NULL) ? -1 : 0;
  IsScenarioGame(NULL);
  if (!(bScenMask & ((nSize == 0x256) ? -1 : 0))) {
    SErrSetLastError(kErrInvalidParameter);
    return FALSE;
  }
  SMemZero(&upgs, 0x256);
  if (!ReadChunk(pFile, &upgs))
    return FALSE;
  int i = 46;
  do {
    --i;
    // The copy happens when the flag is zero: the map overrides the defaults
    // only where it says it is not using them.
    if (!upgs.bUsesDefault[i]) {
      upg_oreCostBase_g[i]    = upgs.wOreCost[i];
      upg_oreCostFactor_g[i]  = upgs.wOreFactor[i];
      upg_gasCostBase_g[i]    = upgs.wGasCost[i];
      upg_gasCostFactor_g[i]  = upgs.wGasFactor[i];
      upg_timeCostBase_g[i]   = upgs.wTimeCost[i];
      upg_timeCostFactor_g[i] = upgs.wTimeFactor[i];
    }
  } while (i != 0);
  return TRUE;
}

// The Brood War TECS extent: 44 techs instead of 24, the same five arrays.
struct chk_tecs_bw_t {
  /*0x000*/ BYTE bUseDefault[44];
  /*0x02C*/ WORD wOreCost[44];
  /*0x084*/ WORD wGasCost[44];
  /*0x0DC*/ WORD wTimeCost[44];
  /*0x134*/ WORD wEnergyCost[44];
};
static_assert(offsetof(chk_tecs_bw_t, bUseDefault) == 0x000, "chk_tecs_bw_t::bUseDefault");
static_assert(offsetof(chk_tecs_bw_t, wOreCost) == 0x02C, "chk_tecs_bw_t::wOreCost");
static_assert(offsetof(chk_tecs_bw_t, wGasCost) == 0x084, "chk_tecs_bw_t::wGasCost");
static_assert(offsetof(chk_tecs_bw_t, wTimeCost) == 0x0DC, "chk_tecs_bw_t::wTimeCost");
static_assert(offsetof(chk_tecs_bw_t, wEnergyCost) == 0x134, "chk_tecs_bw_t::wEnergyCost");
static_assert(sizeof(chk_tecs_bw_t) == 0x18C, "TECS_Bwar: `push 18Ch` sizes SMemZero exactly");

// maphdr_TECS_Bwar -- maphdr_TECS at the expansion extent, with the same four
// `WORD *` locals.
BOOL __stdcall maphdr_TECS_Bwar(void *pFile, int nSize, int a3) {
  chk_tecs_bw_t tecs;
  BOOL bScen = IsScenarioGame(NULL) ? -1 : 0;
  IsScenarioGame(NULL);
  if (!(bScen & ((nSize != sizeof(tecs)) - 1))) {
    SErrSetLastError(0x57);
    return FALSE;
  }
  SMemZero(&tecs, sizeof(tecs));
  if (!ReadChunk(pFile, &tecs))
    return FALSE;
  WORD *pOre = techdata_oreCost_g;
  WORD *pGas = techdata_gasCost_g;
  WORD *pTime = techdata_timeCost_g;
  WORD *pEnergy = techdata_energy_g;
  int i = 44;
  do {
    --i;
    if (!tecs.bUseDefault[i]) {
      pOre[i] = tecs.wOreCost[i];
      pGas[i] = tecs.wGasCost[i];
      pTime[i] = tecs.wTimeCost[i];
      pEnergy[i] = tecs.wEnergyCost[i];
    }
  } while (i != 0);
  return TRUE;
}

// The map's "TECS" (tech costs) section, 0xD8 B on disk for the 24 original
// techs: a BYTE[24] and four WORD[24], which go to techdata_oreCost_g,
// _gasCost_g, _timeCost_g and _energy_g in that order.
struct chk_tecs_t {
  /*0x00*/ BYTE bUseDefault[24];
  /*0x18*/ WORD wOreCost[24];
  /*0x48*/ WORD wGasCost[24];
  /*0x78*/ WORD wTimeCost[24];
  /*0xA8*/ WORD wEnergyCost[24];
};
static_assert(offsetof(chk_tecs_t, bUseDefault) == 0x00, "chk_tecs_t::bUseDefault");
static_assert(offsetof(chk_tecs_t, wOreCost) == 0x18, "chk_tecs_t::wOreCost");
static_assert(offsetof(chk_tecs_t, wGasCost) == 0x48, "chk_tecs_t::wGasCost");
static_assert(offsetof(chk_tecs_t, wTimeCost) == 0x78, "chk_tecs_t::wTimeCost");
static_assert(offsetof(chk_tecs_t, wEnergyCost) == 0xA8, "chk_tecs_t::wEnergyCost");
static_assert(sizeof(chk_tecs_t) == 0xD8, "TECS: `push 0D8h` sizes SMemZero exactly");

// maphdr_TECS. The four `WORD *` locals bind the tables' bases first: the loop
// covers 24 of each table's 44 entries, and indexing the globals directly lets
// the compiler fold the four onto one pointer that starts inside the first
// table, an address 1.17 never forms.
BOOL __stdcall maphdr_TECS(void *pFile, int nSize, int a3) {
  chk_tecs_t tecs;
  BOOL bScen = IsScenarioGame(NULL) ? -1 : 0;
  IsScenarioGame(NULL);
  if (!(bScen & ((nSize != sizeof(tecs)) - 1))) {
    SErrSetLastError(0x57);
    return FALSE;
  }
  SMemZero(&tecs, sizeof(tecs));
  if (!ReadChunk(pFile, &tecs))
    return FALSE;
  WORD *pOre = techdata_oreCost_g;
  WORD *pGas = techdata_gasCost_g;
  WORD *pTime = techdata_timeCost_g;
  WORD *pEnergy = techdata_energy_g;
  // A decrement-first do-while: the loop ends on zero, as 1.17's does, not on
  // the sign a `for (i = N-1; i >= 0; i--)` tests.
  int i = 24;
  do {
    --i;
    if (!tecs.bUseDefault[i]) {
      pOre[i] = tecs.wOreCost[i];
      pGas[i] = tecs.wGasCost[i];
      pTime[i] = tecs.wTimeCost[i];
      pEnergy[i] = tecs.wEnergyCost[i];
    }
  } while (i != 0);
  return TRUE;
}

// The map's "PUNI" (player unit restrictions) section, 0x1644 B on disk: rows of
// 228 (units.dat's count) for each player's availability, the defaults, and
// each player's use-the-default flags.
struct chk_puni_t {
  /*0x0000*/ BYTE bAvailable[12][228];
  /*0x0AB0*/ BYTE bDefault[228];          // row 12
  /*0x0B94*/ BYTE bUsesDefault[12][228];
};
static_assert(offsetof(chk_puni_t, bAvailable) == 0x0000, "chk_puni_t::bAvailable");
static_assert(offsetof(chk_puni_t, bDefault) == 0x0AB0, "chk_puni_t::bDefault");
static_assert(offsetof(chk_puni_t, bUsesDefault) == 0x0B94, "chk_puni_t::bUsesDefault");
static_assert(sizeof(chk_puni_t) == 0x1644,
              "PUNI: `mov eax,1644h / call _chkstk` sizes the local exactly");

// maphdr_PUNI (0x4685D0, 137 B).
BOOL __stdcall maphdr_PUNI(void *pFile, int nSize, int a3) {
  chk_puni_t puni;
  if (!IsScenarioGame(NULL))
    return FALSE;
  if (nSize != 0x1644)
    return FALSE;
  if (!ReadChunk(pFile, &puni))
    return FALSE;
  int p = 12;
  do {
    --p;
    int i = 228;
    do {
      --i;
      PlayerUnitsAvailable[p][i] =
          puni.bUsesDefault[p][i] ? puni.bDefault[i] : puni.bAvailable[p][i];
    } while (i != 0);
  } while (p != 0);
  // Hyperion: Hyperion's slots take the map's defaults
  for (p = PLAYER_SLOTS_117; p < PlayerSlots(); p++)
    memcpy(PlayerUnitsAvailable[p], puni.bDefault, sizeof puni.bDefault);
  return TRUE;
}

// The Brood War UPGR extent: 61 upgrades, 12*61 == 0x2DC per player-indexed
// array; 0x90E bytes, the size SMemZero and the size check take.
struct chk_upgr_bw_t {
  /*0x000*/ BYTE bPlayerMaxLevel[12][61];
  /*0x2DC*/ BYTE bPlayerStartLevel[12][61];
  /*0x5B8*/ BYTE bDefaultMaxLevel[61];
  /*0x5F5*/ BYTE bDefaultStartLevel[61];
  /*0x632*/ BYTE bUseDefault[12][61];
};
static_assert(offsetof(chk_upgr_bw_t, bPlayerMaxLevel) == 0x000, "chk_upgr_bw_t::bPlayerMaxLevel");
static_assert(offsetof(chk_upgr_bw_t, bPlayerStartLevel) == 0x2DC, "chk_upgr_bw_t::bPlayerStartLevel");
static_assert(offsetof(chk_upgr_bw_t, bDefaultMaxLevel) == 0x5B8, "chk_upgr_bw_t::bDefaultMaxLevel");
static_assert(offsetof(chk_upgr_bw_t, bDefaultStartLevel) == 0x5F5, "chk_upgr_bw_t::bDefaultStartLevel");
static_assert(offsetof(chk_upgr_bw_t, bUseDefault) == 0x632, "chk_upgr_bw_t::bUseDefault");
static_assert(sizeof(chk_upgr_bw_t) == 0x90E, "UPGR_Bwar: `push 90Eh` sizes SMemZero exactly");

// maphdr_UPGR_Bwar -- maphdr_UPGR at the expansion extent.
BOOL __stdcall maphdr_UPGR_Bwar(void *pFile, int nSize, int a3) {
  chk_upgr_bw_t upgr;
  BOOL bScen = IsScenarioGame(NULL) ? -1 : 0;
  IsScenarioGame(NULL);
  if (!(bScen & ((nSize != sizeof(upgr)) - 1))) {
    SErrSetLastError(0x57);
    return FALSE;
  }
  SMemZero(&upgr, sizeof(upgr));
  if (!ReadChunk(pFile, &upgr))
    return FALSE;
  int p = 12;
  do {
    --p;
    int i = 61;
    do {
      --i;
      if (upgr.bUseDefault[p][i]) {
        set_upgrade_level(p, (WORD)i, upgr.bDefaultStartLevel[i]);
        set_max_upgrade_level(p, (WORD)i, upgr.bDefaultMaxLevel[i]);
      } else {
        set_upgrade_level(p, (WORD)i, upgr.bPlayerStartLevel[p][i]);
        set_max_upgrade_level(p, (WORD)i, upgr.bPlayerMaxLevel[p][i]);
      }
    } while (i != 0);
  } while (p != 0);
  // Hyperion: Hyperion's slots take the map's defaults
  for (p = PLAYER_SLOTS_117; p < PlayerSlots(); p++) {
    for (int i = 0; i < 61; i++) {
      set_upgrade_level(p, (WORD)i, upgr.bDefaultStartLevel[i]);
      set_max_upgrade_level(p, (WORD)i, upgr.bDefaultMaxLevel[i]);
    }
  }
  return TRUE;
}

// The map's "UPGR" (upgrade levels) section, 0x6D4 B for the 46 original
// upgrades: each player's max and start levels, the default max and start
// levels, and each player's use-the-default flags. The max levels go to
// set_max_upgrade_level and the start levels to set_upgrade_level.
struct chk_upgr_t {
  /*0x000*/ BYTE bPlayerMaxLevel[12][46];
  /*0x228*/ BYTE bPlayerStartLevel[12][46];
  /*0x450*/ BYTE bDefaultMaxLevel[46];
  /*0x47E*/ BYTE bDefaultStartLevel[46];
  /*0x4AC*/ BYTE bUseDefault[12][46];
};
static_assert(offsetof(chk_upgr_t, bPlayerMaxLevel) == 0x000, "chk_upgr_t::bPlayerMaxLevel");
static_assert(offsetof(chk_upgr_t, bPlayerStartLevel) == 0x228, "chk_upgr_t::bPlayerStartLevel");
static_assert(offsetof(chk_upgr_t, bDefaultMaxLevel) == 0x450, "chk_upgr_t::bDefaultMaxLevel");
static_assert(offsetof(chk_upgr_t, bDefaultStartLevel) == 0x47E, "chk_upgr_t::bDefaultStartLevel");
static_assert(offsetof(chk_upgr_t, bUseDefault) == 0x4AC, "chk_upgr_t::bUseDefault");
static_assert(sizeof(chk_upgr_t) == 0x6D4, "UPGR: `push 6D4h` sizes SMemZero exactly");

// maphdr_UPGR. Two nested decrement-first do-whiles that end on zero; the WORD
// upgrade id is computed once and passed to both callees in EDX.
BOOL __stdcall maphdr_UPGR(void *pFile, int nSize, int a3) {
  chk_upgr_t upgr;
  BOOL bScen = IsScenarioGame(NULL) ? -1 : 0;
  IsScenarioGame(NULL);
  if (!(bScen & ((nSize != sizeof(upgr)) - 1))) {
    SErrSetLastError(0x57);
    return FALSE;
  }
  SMemZero(&upgr, sizeof(upgr));
  if (!ReadChunk(pFile, &upgr))
    return FALSE;
  int p = 12;
  do {
    --p;
    int i = 46;
    do {
      --i;
      if (upgr.bUseDefault[p][i]) {
        set_upgrade_level(p, (WORD)i, upgr.bDefaultStartLevel[i]);
        set_max_upgrade_level(p, (WORD)i, upgr.bDefaultMaxLevel[i]);
      } else {
        set_upgrade_level(p, (WORD)i, upgr.bPlayerStartLevel[p][i]);
        set_max_upgrade_level(p, (WORD)i, upgr.bPlayerMaxLevel[p][i]);
      }
    } while (i != 0);
  } while (p != 0);
  // Hyperion: Hyperion's slots take the map's defaults
  for (p = PLAYER_SLOTS_117; p < PlayerSlots(); p++) {
    for (int i = 0; i < 46; i++) {
      set_upgrade_level(p, (WORD)i, upgr.bDefaultStartLevel[i]);
      set_max_upgrade_level(p, (WORD)i, upgr.bDefaultMaxLevel[i]);
    }
  }
  return TRUE;
}

// The Brood War extent of the map's "PTEC" section: 44 rows instead of 24,
// everything else the same.
struct chk_ptec_bw_t {
  /*0x000*/ BYTE bAvailable[12][44];
  /*0x210*/ BYTE bResearched[12][44];
  /*0x420*/ BYTE bDefaultAvailable[44];
  /*0x44C*/ BYTE bDefaultResearched[44];
  /*0x478*/ BYTE bUsesDefault[12][44];
};
static_assert(offsetof(chk_ptec_bw_t, bAvailable) == 0x000, "chk_ptec_bw_t::bAvailable");
static_assert(offsetof(chk_ptec_bw_t, bResearched) == 0x210, "chk_ptec_bw_t::bResearched");
static_assert(offsetof(chk_ptec_bw_t, bDefaultAvailable) == 0x420, "chk_ptec_bw_t::bDefaultAvailable");
static_assert(offsetof(chk_ptec_bw_t, bDefaultResearched) == 0x44C, "chk_ptec_bw_t::bDefaultResearched");
static_assert(offsetof(chk_ptec_bw_t, bUsesDefault) == 0x478, "chk_ptec_bw_t::bUsesDefault");
static_assert(sizeof(chk_ptec_bw_t) == 0x688, "PTEC_Bwar: `sub esp,688h` sizes the local exactly");

// maphdr_PTEC_Bwar -- the same handler at the Brood War chunk extent.
BOOL __stdcall maphdr_PTEC_Bwar(void *pFile, int nSize, int a3) {
  chk_ptec_bw_t ptec;
  BOOL bScenMask = IsScenarioGame(NULL) ? -1 : 0;
  IsScenarioGame(NULL);
  if (!(bScenMask & ((nSize == 0x688) ? -1 : 0))) {
    SErrSetLastError(kErrInvalidParameter);
    return FALSE;
  }
  SMemZero(&ptec, 0x688);
  if (!ReadChunk(pFile, &ptec))
    return FALSE;
  int p = 12;
  do {
    --p;
    int t = 44;
    do {
      --t;
      int nRes;
      if (ptec.bUsesDefault[p][t]) {
        set_tech_available(p, (WORD)t, ptec.bDefaultAvailable[t]);
        nRes = ptec.bDefaultResearched[t];
      } else {
        set_tech_available(p, (WORD)t, ptec.bAvailable[p][t]);
        nRes = ptec.bResearched[p][t];
      }
      set_tech_researched(p, (WORD)t, (BYTE)nRes);
    } while (t != 0);
  } while (p != 0);
  // Hyperion: Hyperion's slots take the map's defaults
  for (p = PLAYER_SLOTS_117; p < PlayerSlots(); p++) {
    for (int t = 0; t < 44; t++) {
      set_tech_available(p, (WORD)t, ptec.bDefaultAvailable[t]);
      set_tech_researched(p, (WORD)t, ptec.bDefaultResearched[t]);
    }
  }
  return TRUE;
}

// The map's "PTEC" section (player tech restrictions), 0x390 B on disk, in rows
// of 24, techdata.dat's original count.
struct chk_ptec_t {
  /*0x000*/ BYTE bAvailable[12][24];
  /*0x120*/ BYTE bResearched[12][24];
  /*0x240*/ BYTE bDefaultAvailable[24];
  /*0x258*/ BYTE bDefaultResearched[24];
  /*0x270*/ BYTE bUsesDefault[12][24];
};
static_assert(offsetof(chk_ptec_t, bAvailable) == 0x000, "chk_ptec_t::bAvailable");
static_assert(offsetof(chk_ptec_t, bResearched) == 0x120, "chk_ptec_t::bResearched");
static_assert(offsetof(chk_ptec_t, bDefaultAvailable) == 0x240, "chk_ptec_t::bDefaultAvailable");
static_assert(offsetof(chk_ptec_t, bDefaultResearched) == 0x258, "chk_ptec_t::bDefaultResearched");
static_assert(offsetof(chk_ptec_t, bUsesDefault) == 0x270, "chk_ptec_t::bUsesDefault");
static_assert(sizeof(chk_ptec_t) == 0x390, "PTEC: `sub esp,390h` sizes the local exactly");

BOOL __stdcall maphdr_PTEC(void *pFile, int nSize, int a3) {
  chk_ptec_t ptec;
  BOOL bScenMask = IsScenarioGame(NULL) ? -1 : 0;
  IsScenarioGame(NULL);
  if (!(bScenMask & ((nSize == 0x390) ? -1 : 0))) {
    SErrSetLastError(kErrInvalidParameter);
    return FALSE;
  }
  SMemZero(&ptec, 0x390);
  if (!ReadChunk(pFile, &ptec))
    return FALSE;
  // Both loops are decrement-first do-whiles that end on zero, as 1.17's do; a
  // `for (i = N-1; i >= 0; i--)` tests the sign instead.
  int p = 12;
  do {
    --p;
    int t = 24;
    do {
      --t;
      // nRes is an int, which keeps the zero-extended byte in EAX across the
      // branch join, as 1.17 does.
      int nRes;
      if (ptec.bUsesDefault[p][t]) {
        set_tech_available(p, (WORD)t, ptec.bDefaultAvailable[t]);
        nRes = ptec.bDefaultResearched[t];
      } else {
        set_tech_available(p, (WORD)t, ptec.bAvailable[p][t]);
        nRes = ptec.bResearched[p][t];
      }
      set_tech_researched(p, (WORD)t, (BYTE)nRes);
    } while (t != 0);
  } while (p != 0);
  // Hyperion: Hyperion's slots take the map's defaults
  for (p = PLAYER_SLOTS_117; p < PlayerSlots(); p++) {
    for (int t = 0; t < 24; t++) {
      set_tech_available(p, (WORD)t, ptec.bDefaultAvailable[t]);
      set_tech_researched(p, (WORD)t, ptec.bDefaultResearched[t]);
    }
  }
  return TRUE;
}

// maphdr_TYPE -- the chunk is accepted without being read.
BOOL __stdcall maphdr_TYPE(void *pFile, int nSize, int a3) {
  return TRUE;
}

// maphdr_VER -- the family's canonical shape: size guard, ReadChunk into an
// a3-relative destination, boolean-normalised return.
BOOL __stdcall maphdr_VER(void *pFile, int nSize, void *pDest) {
  if (nSize != 2)
    return FALSE;
  return ReadChunk(pFile, (BYTE *)pDest + 0x18) != 0;
}

struct oc_player_t {           // 0x24 stride, 12 entries at 0x673D50
  /*0x00*/ BYTE bOwner;        // maphdr_OWNR scatter target
  /*0x01*/ BYTE bSide;         // maphdr_SIDE scatter target (0x673D51)
  /*0x02*/ BYTE _pad02[0x22];
};
static_assert(sizeof(oc_player_t) == 0x24, "player stride: OWNR loop sub ecx,24h");
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
  #ifndef gPlayers_g
  #define gPlayers_g ((oc_player_t *)OC_VA(0x673D50))
  #endif
#else
extern oc_player_t gPlayers_g[];
#endif

// maphdr_SIDE (0x468A60, 79 B) -- byte-twin of OWNR at +1 in the player struct.
BOOL __stdcall maphdr_SIDE(void *pFile, int nSize, int a3) {
  BYTE buf[12];
  if (nSize != 12)
    return FALSE;
  if (!ReadChunk(pFile, buf))
    return FALSE;
  int i = 12;
  do {
    --i;
    gPlayers_g[i].bSide = buf[i];
  } while (i != 0);
  return TRUE;
}

// maphdr_OWNR (0x468AB0, 79 B). Reads 12 bytes and scatters them into
// player[i].bOwner, through a struct-typed destination indexed by i.
BOOL __stdcall maphdr_OWNR(void *pFile, int nSize, int a3) {
  BYTE buf[12];
  if (nSize != 12)
    return FALSE;
  if (!ReadChunk(pFile, buf))
    return FALSE;
  int i = 12;
  do {
    --i;
    gPlayers_g[i].bOwner = buf[i];
  } while (i != 0);
  return TRUE;
}

// maphdr_ERA (0x468B00, 85 B) -- reads the tileset, refuses one above 4 unless
// the expansion is loaded, and keeps it modulo 8. The WORD is read into the
// size parameter's slot once the size check is done, as 1.17 does it.
BOOL __stdcall maphdr_ERA(void *pFile, int nSize, int a3) {
  if (nSize != 2)
    return FALSE;
  WORD wEra;
  if (!ReadChunk(pFile, &wEra))
    return FALSE;
  g_MapTileset = wEra;
  if (g_MapTileset > 4 && gbExpansion_g == 0)
    return FALSE;
  g_MapTileset %= 8;
  return TRUE;
}

// maphdr_COLR -- an 8-byte stack buffer handed to a consumer.
BOOL __stdcall maphdr_COLR(void *pFile, int nSize, int a3) {
  BYTE buf[8];
  if (nSize != 8)
    return FALSE;
  if (!ReadChunk(pFile, buf))
    return FALSE;
  permute_colors_by_map(buf, 8);
  return TRUE;
}

// The map's "THG2" (map doodads and sprites) entry, 10 B on disk: the type, x, y
// and player, and a flags word tested for 0x1000 and 0x8000. The same four
// fields go to CThingy::AllocThingy or, in the other arm, to unit_create.
struct chk_thg2_t {
  /*0x00*/ WORD wType;
  /*0x02*/ WORD wX;
  /*0x04*/ WORD wY;
  /*0x06*/ BYTE bPlayer;
  /*0x07*/ BYTE _pad07;
  /*0x08*/ WORD wFlags;
};
static_assert(offsetof(chk_thg2_t, wType) == 0x00, "chk_thg2_t::wType");
static_assert(offsetof(chk_thg2_t, wX) == 0x02, "chk_thg2_t::wX");
static_assert(offsetof(chk_thg2_t, wY) == 0x04, "chk_thg2_t::wY");
static_assert(offsetof(chk_thg2_t, bPlayer) == 0x06, "chk_thg2_t::bPlayer");
static_assert(offsetof(chk_thg2_t, wFlags) == 0x08, "chk_thg2_t::wFlags");
static_assert(sizeof(chk_thg2_t) == 10, "THG2 stride: `add esi, 0Ah`");

// maphdr_THG2.
BOOL __stdcall maphdr_THG2(void *pFile, int nSize, int a3) {
  // Unsigned division by 10, as 1.17 divides.
  int nCount = (DWORD)nSize / 10;
  if ((DWORD)nSize % 10 != 0)
    return FALSE;
  chk_thg2_t *pThingies = (chk_thg2_t *)SMemAlloc(nSize, "maphdr.cpp", 322, 0);
  if (!ReadChunk(pFile, pThingies)) {
    SMemFree(pThingies, "maphdr.cpp", 323, 0);
    return FALSE;
  }
  chk_thg2_t *p = pThingies;
  while (nCount != 0) {
    --nCount;
    if (p->wFlags & 0x1000) {
      CThingy::AllocThingy(p->wType, p->wX, p->wY, p->bPlayer);
    } else {
      WORD wType = p->wType;
      if (wType == 205 || wType == 206 || wType == 207 || wType == 208)
        p->bPlayer = 11;
      if (IsScenarioGame(NULL) || p->bPlayer == 11) {
        CUnit *pUnit = unit_create(p->wType, p->wX, p->wY, p->bPlayer);
        if (pUnit && (p->wFlags & 0x8000))
          pUnit->InitDisableDoodad();
      }
    }
    ++p;
  }
  SMemFree(pThingies, "maphdr.cpp", 348, 0);
  return TRUE;
}

// maphdr_STR (0x468CE0, 107 B) -- keeps the map's string table and registers
// free_map_strs to free it.
BOOL __stdcall maphdr_STR(void *pFile, int nSize, int a3) {
  if (gpMapStrs_g)
    SMemFree(gpMapStrs_g, "maphdr.cpp", 356, 0);
  gpMapStrs_g = SMemAlloc(nSize, "maphdr.cpp", 357, 0);
  if (!gpMapStrs_g)
    return FALSE;
  gdwMapStrSize_g = nSize;
  PortMapStringsWide(FALSE);  // Hyperion: a STR table (port_map.cpp)
  app_add_exit(free_map_strs);
  // Hyperion: whether the map's strings are Korean in CP949 (port_maptext.h)
  if (ReadChunk(pFile, gpMapStrs_g) == 0)
    return FALSE;
  MapTextStrings(gpMapStrs_g, nSize, false);
  return TRUE;
}

// Hyperion: maphdr_STRx, the string table that maps saved by StarCraft:
// Remastered and ScmDraft 2 may carry in place of STR: a u32 count
// and u32 offsets from the section's start. Kept as STR's is, with a NUL after
// it as Remastered keeps it, and marked for get_map_string (port_map.cpp). A
// section too short to hold its count is passed over, as in Remastered. Only
// the versions Remastered saves list it (MapVersions, below).
BOOL __stdcall maphdr_STRx(void *pFile, int nSize, void *pDest) {
  (void)pDest;
  if (nSize < 4)
    return TRUE;
  if (gpMapStrs_g)
    SMemFree(gpMapStrs_g, "maphdr.cpp", __LINE__, 0);
  gpMapStrs_g = SMemAlloc(nSize + 1, "maphdr.cpp", __LINE__, 0);
  if (!gpMapStrs_g)
    return FALSE;
  static_cast<BYTE *>(gpMapStrs_g)[nSize] = 0;
  gdwMapStrSize_g = nSize;
  PortMapStringsWide(TRUE);
  app_add_exit(free_map_strs);
  if (ReadChunk(pFile, gpMapStrs_g) == 0)
    return FALSE;
  MapTextStrings(gpMapStrs_g, nSize, true);  // Hyperion
  return TRUE;
}

// Hyperion: maphdr_CRGB, the players' colours a map saved by StarCraft:
// Remastered may set: eight RGB triples, then how each player's colour
// is chosen. Kept for PortMapColorsApply, which the game start runs once each
// slot has its colour (port_map.cpp). Remastered reads it for every version and
// refuses a map whose section is not 32 bytes; here such a section is passed
// over and the colours stay 1.17's, since they change nothing but the picture.
BOOL __stdcall maphdr_CRGB(void *pFile, int nSize, void *pDest) {
  BYTE abCrgb[32];
  (void)pDest;
  if (nSize != sizeof abCrgb)
    return TRUE;
  if (!ReadChunk(pFile, abCrgb))
    return FALSE;
  PortMapColorsSet(abCrgb);
  return TRUE;
}

// maphdr_MRGN -- destination comes from a CALLEE, a3 unread.
BOOL __stdcall maphdr_MRGN(void *pFile, int nSize, int a3) {
  if (nSize != 0x500)
    return FALSE;
  return ReadChunk(pFile, ExtMapRegions()) != 0;
}

// maphdr_MRGN_Ext -- MRGN with the expansion chunk size.
BOOL __stdcall maphdr_MRGN_Ext(void *pFile, int nSize, int a3) {
  if (nSize != 0x13EC)
    return FALSE;
  return ReadChunk(pFile, ExtMapRegions()) != 0;
}

// maphdr_UPRP -- MRGN with getUnitProps as the destination.
BOOL __stdcall maphdr_UPRP(void *pFile, int nSize, int a3) {
  if (nSize != 0x500)
    return FALSE;
  return ReadChunk(pFile, getUnitProps()) != 0;
}

// maphdr_TRIG -- N trigger records of 0x960 bytes each.
int __stdcall maphdr_TRIG(void *pFile, int nSize, int a3) {
  // Unsigned division by 0x960, as 1.17 divides.
  int nCount = (DWORD)nSize / 0x960;
  if ((DWORD)nSize % 0x960 != 0)
    return FALSE;
  BYTE *pTriggers = (BYTE *)SMemAlloc(nSize, "maphdr.cpp", 456, 0);
  if (!ReadChunk(pFile, pTriggers)) {
    SMemFree(pTriggers, "maphdr.cpp", 458, 0);
    return FALSE;
  }
  BYTE *p = pTriggers;
  for (int i = 0; i < nCount; i++) {
    EudMapTrigger(p);  // Hyperion: does it reach memory, as Remastered tests
    TriggerHxCodes(p);  // Hyperion: a Hyperion map's slots past 11
    if (!TryAddTrigger(p, 0))
      break;
    TryAddTriggerHx(p, i);
    p += 0x960;
  }
  EudMapTriggersRead();  // Hyperion: a new game on an EUD map is an EUD game
  SMemFree(pTriggers, "maphdr.cpp", 473, 0);
  return TRUE;
}

// maphdr_MBRF -- maphdr_TRIG's twin: N briefing-trigger records of 0x960 bytes
// each, counted down.
int __stdcall maphdr_MBRF(void *pFile, int nSize, int a3) {
  // Unsigned division by 0x960.
  int nCount = (DWORD)nSize / 0x960;
  if ((DWORD)nSize % 0x960 != 0)
    return FALSE;
  BYTE *pBriefings = (BYTE *)SMemAlloc(nSize, "maphdr.cpp", 482, 0);
  if (!ReadChunk(pFile, pBriefings)) {
    SMemFree(pBriefings, "maphdr.cpp", 486, 0);
    return FALSE;
  }
  BYTE *p = pBriefings;
  // A pre-test, the decrement before the call and a zero test at the bottom.
  while (nCount != 0) {
    --nCount;
    if (!addBriefingTrigger(p, 0))
      break;
    p += 0x960;
  }
  SMemFree(pBriefings, "maphdr.cpp", 498, 0);
  return TRUE;
}

// The map's "UNIT" record, 36 bytes. The property tail at +0x0C is a block of its
// own, since maphdr_UNIT passes its address to apply_unit_properties. dwProps is
// one dword, tested as a dword.
struct chk_unit_props_t {
  /*0x00*/ DWORD dwProps;
  /*0x04*/ BYTE bOwner;
  /*0x05*/ BYTE bHitPointsPct;
  /*0x06*/ BYTE bShieldsPct;
  /*0x07*/ BYTE bEnergyPct;
  /*0x08*/ WORD wResources;
  /*0x0A*/ BYTE _pad0A[2];
  /*0x0C*/ BYTE bHangarCount;
  /*0x0D*/ BYTE _pad0D;
  /*0x0E*/ WORD wStateFlags;
  /*0x10*/ BYTE _pad10[4];
};
static_assert(offsetof(chk_unit_props_t, dwProps) == 0x00, "chk_unit_props_t::dwProps");
static_assert(offsetof(chk_unit_props_t, bOwner) == 0x04, "chk_unit_props_t::bOwner");
static_assert(offsetof(chk_unit_props_t, bHitPointsPct) == 0x05, "chk_unit_props_t::bHitPointsPct");
static_assert(offsetof(chk_unit_props_t, bShieldsPct) == 0x06, "chk_unit_props_t::bShieldsPct");
static_assert(offsetof(chk_unit_props_t, bEnergyPct) == 0x07, "chk_unit_props_t::bEnergyPct");
static_assert(offsetof(chk_unit_props_t, wResources) == 0x08, "chk_unit_props_t::wResources");
static_assert(offsetof(chk_unit_props_t, bHangarCount) == 0x0C, "chk_unit_props_t::bHangarCount");
static_assert(offsetof(chk_unit_props_t, wStateFlags) == 0x0E, "chk_unit_props_t::wStateFlags");
static_assert(sizeof(chk_unit_props_t) == 0x14, "the block runs chk_unit_t +0x0C to +0x20");

struct chk_unit_t {
  /*0x00*/ DWORD dwSerial; // connect_nydus's nId
  /*0x04*/ WORD wX;
  /*0x06*/ WORD wY;
  /*0x08*/ WORD wType;
  /*0x0A*/ WORD wRelation;
  /*0x0C*/ chk_unit_props_t props;
  /*0x20*/ DWORD dwLink;   // connect_nydus's nLink
};
static_assert(offsetof(chk_unit_t, dwSerial) == 0x00, "chk_unit_t::dwSerial");
static_assert(offsetof(chk_unit_t, wX) == 0x04, "chk_unit_t::wX");
static_assert(offsetof(chk_unit_t, wY) == 0x06, "chk_unit_t::wY");
static_assert(offsetof(chk_unit_t, wType) == 0x08, "chk_unit_t::wType");
static_assert(offsetof(chk_unit_t, props) == 0x0C, "chk_unit_t::props");
static_assert(offsetof(chk_unit_t, props.bOwner) == 0x10, "chk_unit_t props.bOwner: cmp byte ptr [ecx+10h], 0Bh");
static_assert(offsetof(chk_unit_t, dwLink) == 0x20, "chk_unit_t::dwLink");
static_assert(sizeof(chk_unit_t) == 36, "CHK UNIT entries are 36 bytes");

// maphdr_UNIT -- place every preplaced unit, then wire the nydus and addon
// links the placement pass collected. The two SMemFree failure exits differ only
// in their line number.
BOOL __stdcall maphdr_UNIT(void *pFile, int nSize, int a3) {
  maplink_t *pNydus = NULL;
  maplink_t *pAddon = NULL;
  chk_unit_t *pChunk;
  chk_unit_t *pEntry;
  point16_t *pStarts;
  CUnit *pUnit;
  // Unsigned division by 36.
  DWORD dwCount = (DWORD)nSize / 36;
  if ((DWORD)nSize % 36 != 0)
    return FALSE;
  pChunk = (chk_unit_t *)SMemAlloc(nSize, "maphdr.cpp", 913, 0);
  if (!pChunk)
    return FALSE;
  if (!ReadChunk(pFile, pChunk)) {
    SMemFree(pChunk, "maphdr.cpp", 916, 0);
    return FALSE;
  }
  // The start-location block is allocated zeroed (flag 8), unlike the chunk
  // buffer above it. Hyperion: 32 bytes in 1.17, which an owner past 7
  // overran; a point for every slot.
  pStarts = (point16_t *)SMemAlloc(sizeof(point16_t) * PLAYER_SLOTS_MAX, "maphdr.cpp", 921, 8);
  if (!pStarts) {
    SMemFree(pChunk, "maphdr.cpp", 923, 0);
    return FALSE;
  }
  pEntry = pChunk;
  while (dwCount--) {
    // Hyperion: a type the game lacks indexes past its tables; the editors write none
    if (!UnitTypeValid(pEntry->wType))
      goto next;
    if (!setup_startloc(pEntry, pStarts)) {
      BYTE bOwner = pEntry->props.bOwner;
      if (PlayerIsLobby(bOwner)) {  // Hyperion: < 8 in 1.17
        BYTE bSlotType = gPlayerData[bOwner].nType;
        if (bSlotType == 0)
          goto next;
        if (bSlotType > 4 && bSlotType != 7)
          goto next;
      }
      if (!is_invalid_preplaced_unit(pEntry) &&
          !(IsScenarioGame(NULL) && is_race_selectable(pEntry->props.bOwner) &&
            (gubUnitCountInfo[pEntry->wType] & 0x80) == 0)) {
        pUnit = unit_create(pEntry->wType, pEntry->wX, pEntry->wY,
                            pEntry->props.bOwner);
        if (pUnit) {
          apply_unit_properties(pUnit, &pEntry->props);
          pNydus = connect_nydus(pNydus, pUnit, pEntry->dwSerial, pEntry->dwLink);
          pAddon = connect_addon(pAddon, pUnit, pEntry->dwSerial, pEntry->dwLink);
        }
      }
    }
  next:
    ++pEntry;
  }
  memcpy(gStartLocations.a, pStarts, 32);
  // Hyperion: and on a Hyperion map the starts of the slots past 7
  for (int i = PLAYER_LOBBY_117; i < PlayerLobbySlots(); i++)
    gStartLocations[i] = pStarts[i];
  while (pNydus != NULL)
    pNydus = sub_4698E0(pNydus);
  while (pAddon != NULL)
    pAddon = sub_469810(pAddon);
  SMemFree(pChunk, "maphdr.cpp", 1003, 0);
  SMemFree(pStarts, "maphdr.cpp", 1004, 0);
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext)
    p->CircleUpdate();
  RequestJuiceUpdate();
  return TRUE;
}

// maphdr_DIM -- reads the map's width and height into the size parameter's
// slot once the size check is done, as 1.17 does.
BOOL __stdcall maphdr_DIM(void *pFile, int nSize, int a3) {
  if (nSize != 4)
    return FALSE;
  point16_t dim;
  if (!ReadChunk(pFile, &dim))
    return FALSE;
  // Hyperion: Hyperion plays up to MAP_SIDE_MAX a side, and refuses an empty
  // map, whose arrays nothing sizes
  if (dim.x == 0 || dim.y == 0 || (WORD)dim.x > MAP_SIDE_MAX || (WORD)dim.y > MAP_SIDE_MAX)
    return FALSE;
  gwMapWidth = dim.x;
  gwMapHeight = dim.y;
  return TRUE;
}

// maphdr_MTXM -- the tile matrix. After the read the bottom map border is
// stamped unbuildable: two 5x2 corners at y = height-2 and the whole 1-tile
// bottom row. It returns `expr != 0`.
BOOL __stdcall maphdr_MTXM(void *pFile, int nSize, int a3) {
  // Hyperion: 1.17's 0x20000 bytes, or the map's tiles past that
  DWORD nTiles = (DWORD)gwMapWidth * gwMapHeight;
  if ((DWORD)nSize > (nTiles > MAP_TILES_117 ? nTiles * 2 : 0x20000))
    return FALSE;
  if (!ReadChunk(pFile, gfpMIMap))
    return FALSE;
  if (!convert_cellmap())
    return FALSE;
  mtx_unplace_bldg(0, gwMapHeight - 2, 5, 2, 0x20410000);
  mtx_place_bldg(0, gwMapHeight - 2, 5, 2, 0x800000);
  mtx_unplace_bldg(gwMapWidth - 5, gwMapHeight - 2, 5, 2, 0x20410000);
  mtx_place_bldg(gwMapWidth - 5, gwMapHeight - 2, 5, 2, 0x800000);
  mtx_unplace_bldg(0, gwMapHeight - 1, gwMapWidth, 1, 0x20410000);
  mtx_place_bldg(0, gwMapHeight - 1, gwMapWidth, 1, 0x800000);
  return sai_map_init() != 0;
}

// maphdr_MASK -- one fog byte per tile, broadcast into both low bytes of the
// square-map entry.
BOOL __stdcall maphdr_MASK(void *pFile, int nSize, int a3) {
  BYTE *pBuf = (BYTE *)SMemAlloc(nSize, "maphdr.cpp", 1074, 0);
  if (!ReadChunk(pFile, pBuf)) {
    SMemFree(pBuf, "maphdr.cpp", 1084, 0);
    return FALSE;
  }
  DWORD *pDst = gpSquareMap;
  BYTE *p = pBuf;
  int n = nSize;
  if (n > MAP_TILES_MAX)  // Hyperion: no bound in 1.17, which overran the square map
    n = MAP_TILES_MAX;
  while (n--) {
    *(WORD *)pDst = 0;
    *pDst |= (*p << 8) | *p;
    ++p;
    ++pDst;
  }
  SMemFree(pBuf, "maphdr.cpp", 1102, 0);
  return TRUE;
}

// maphdr_SPRP -- VER with the destination unoffset.
BOOL __stdcall maphdr_SPRP(void *pFile, int nSize, void *pDest) {
  if (nSize != 4)
    return FALSE;
  return ReadChunk(pFile, pDest) != 0;
}

// maphdr_FORC -- the only handler with a relational guard, and it is unsigned.
// 20 bytes of the destination are zeroed first.
BOOL __stdcall maphdr_FORC(void *pFile, int nSize, void *pDest) {
  if ((DWORD)nSize > 0x14)
    return FALSE;
  BYTE *p = (BYTE *)pDest + 4;
  memset(p, 0, 20);
  return ReadChunk(pFile, p) != 0;
}

// The map-load fixup list, 0x14 bytes, allocated by connect_nydus and
// connect_addon and released by sub_46A320.
struct maplink_t {
  /*0x00*/ int nId;
  /*0x04*/ int nLink;
  /*0x08*/ CUnit *pUnit;
  /*0x0C*/ maplink_t *pPrev;        // the old head
  /*0x10*/ maplink_t *pNext;
};
static_assert(offsetof(maplink_t, nId) == 0x00, "maplink_t::nId");
static_assert(offsetof(maplink_t, nLink) == 0x04, "maplink_t::nLink");
static_assert(offsetof(maplink_t, pUnit) == 0x08, "maplink_t::pUnit");
OC_X86_LAYOUT(offsetof(maplink_t, pPrev) == 0x0C, "maplink_t::pPrev");
OC_X86_LAYOUT(offsetof(maplink_t, pNext) == 0x10, "maplink_t::pNext");
OC_X86_LAYOUT(sizeof(maplink_t) == 0x14, "connect_nydus allocates `push 14h`");

// connect_addon -- connect_nydus's twin: pushes a Terran addon link onto the
// map-load fixup list. Both allocate at line 761, one shared source line in
// 1.17.
maplink_t *connect_addon(maplink_t *pHead, CUnit *pUnit, int nId, int nLink) {
  if (pUnit->GetRace() == 1) {
    if (pUnit->dwFlags & 2) {
      if (nLink == 0)
        return pHead;
      maplink_t *p = (maplink_t *)SMemAlloc(sizeof(maplink_t), "maphdr.cpp", 761, 0);
      p->nId = nId;
      p->nLink = nLink;
      p->pUnit = pUnit;
      p->pPrev = pHead;
      p->pNext = NULL;
      if (pHead)
        pHead->pNext = p;
      return p;
    }
  }
  return pHead;
}

// connect_nydus -- pushes a Nydus Canal link onto the fixup list.
maplink_t *connect_nydus(maplink_t *pHead, CUnit *pUnit, int nId, int nLink) {
  // The outer test is a positive `if` block with one trailing `return pHead;`,
  // the inner one an early return: 1.17's shape.
  if (pUnit->wType == 0x86) {
    if (nLink == 0)
      return pHead;
    maplink_t *p = (maplink_t *)SMemAlloc(sizeof(maplink_t), "maphdr.cpp", 761, 0);
    p->nId = nId;
    p->nLink = nLink;
    p->pUnit = pUnit;
    p->pPrev = pHead;
    p->pNext = NULL;
    if (pHead)
      pHead->pNext = p;
    return p;
  }
  return pHead;
}

// close_map.
BOOL close_map(void) {
  if (!ghScenArchive)
    return TRUE;
  BOOL bOk = SFileCloseArchive(ghScenArchive);
  ghScenArchive = NULL;
  return bOk;
}

// unit_create -- place one preplaced/triggered unit, spread its creep, and roll
// it back with an on-screen error if it will not fit.
CUnit *unit_create(WORD wType, int x, int y, int nPlayer) {
  char szBuf[0x100];
  if (!UnitTypeValid(wType))  // Hyperion: a map's THG2 names any type
    return NULL;
  if ((gfEditFlags[wType] & 0x200) && gbExpansion_g == 0)
    return NULL;
  CUnit *pUnit = CreateUnitAt(wType, x, y, nPlayer);
  if (pUnit == NULL) {
    CUnit::ShowCreateError((BYTE)nPlayer);
    return NULL;
  }
  if (unit_type_spreads_creep(pUnit->wType, 1) ||
      (gulUnitTypeFlags[pUnit->wType] & 0x20000))
    spread_creep_completely(pUnit->wType, PosS(pUnit->pSprite->wPosX),
                            pUnit->pSprite->wPosY);
  if (!pUnit->sub_424DB0()) {
    // A named local for the unit type, declared before the two coordinate
    // locals, as 1.17 orders them.
    WORD wUnitType = pUnit->wType;
    upos_t wY = pUnit->pSprite->wPosY;
    upos_t wX = pUnit->pSprite->wPosX;
    psnprintf(szBuf, 0x100, error_get_str(18),
              get_unit_name_from_type(wUnitType), PosS(wX), PosS(wY));
    map_msg_error(szBuf, 3);
    pUnit->SelfDestruct();
    return NULL;
  }
  pUnit->MakeGuardPost();
  return pUnit;
}

// The map-authentication context handed to sub_4C1350's callback:
// authenticate_map fills +0 with the open archive and zeroes +4, and
// sub_469650 opens through +0 and writes the matched entry's id to +4.
struct mapauth_t {
  /*0x00*/ HANDLE hArchive;
  /*0x04*/ int nMapId;
};
static_assert(offsetof(mapauth_t, hArchive) == 0x00, "mapauth_t::hArchive");
OC_X86_LAYOUT(offsetof(mapauth_t, nMapId) == 0x04, "mapauth_t::nMapId");
OC_X86_LAYOUT(sizeof(mapauth_t) == 8,
              "authenticate_map's `lea eax,[ebp-10h]` covers exactly [ebp-10h] "
              "and [ebp-0Ch]");

// authenticate_map -- open a map archive, ask the chunk enumerator which
// product it belongs to, and confirm the digital signature.
BOOL __fastcall authenticate_map(const char *pszName, int *pnMapId,
                                 BOOL bKeepOpen, HANDLE *phArchive) {
  HANDLE hArchive;
  int nSigStatus;
  mapauth_t ctx;

  if (!SFileOpenArchive(pszName, 0, 0, &hArchive))
    return FALSE;
  ctx.hArchive = hArchive;
  ctx.nMapId = 0;
  sub_4C1350(reinterpret_cast<chunkenum_fn>(sub_469650), &ctx);
  if (ctx.nMapId != 0) {
    if (!map_validate_signature(pszName, &nSigStatus) || nSigStatus != 1)
      ctx.nMapId = 0;
  }
  if (!bKeepOpen)
    SFileCloseArchive(hArchive);
  else
    *phArchive = hArchive;
  *pnMapId = ctx.nMapId;
  return TRUE;
}

// is_invalid_preplaced_unit (0x469600, 79 B). Only neutral (player 11) entries
// of the start-location and resource types are valid preplacements once the
// gate byte is set.
BOOL __fastcall is_invalid_preplaced_unit(chk_unit_t *pUnit) {
  if (gbValidateUnits_g == 0)
    return FALSE;
  if (pUnit->props.bOwner != 11)
    return TRUE;
  DWORD t = pUnit->wType;   // the range test is unsigned
  if ((t >= 0xB0 && t <= 0xB2) || t == 0xBC || t == 0x59 || t == 0x5A ||
      t == 0x5F || t == 0x5D || t == 0x5E || t == 0x60)
    return FALSE;
  return TRUE;
}

// The 8-byte context sub_4C1350 hands to its callback, two adjacent stack
// dwords at both callers (authenticate_map, OpenScenarioArchive): the archive
// handle, and an out field the callers zero and read back as the result.
struct mapver_ctx_t {
  /*0x00*/ HANDLE hArchive;   // SFileOpenFileEx's archive
  /*0x04*/ int nVersion;
};
static_assert(offsetof(mapver_ctx_t, hArchive) == 0x00, "mapver_ctx_t::hArchive");
OC_X86_LAYOUT(offsetof(mapver_ctx_t, nVersion) == 0x04, "mapver_ctx_t::nVersion");
OC_X86_LAYOUT(sizeof(mapver_ctx_t) == 8,
              "both callers build it as two adjacent stack dwords");

// sub_469650 -- the map-version probe callback. sub_4C1350 walks the
// seven-entry version table at 0x4F1034..0x4F110C and calls it with the
// candidate file name, that entry's version and the context; a zero return
// stops the walk, so the found path returns 0 and the others 1.
int __stdcall sub_469650(const char *pszName, int nVersion, mapver_ctx_t *pCtx) {
  char szLeague[32];
  HANDLE hFile;
  // The guard is branchless, both pointers made 0/-1 masks and ANDed, with the
  // error block inline; a `||` or a 0/1 test compiles differently.
  if (!((pszName ? -1 : 0) & (pCtx ? -1 : 0))) {
    SErrSetLastError(kErrInvalidParameter);
    return 0;
  }
  if (!SFileOpenFileEx(pCtx->hArchive, pszName, 0, &hFile))
    return 1;
  SFileCloseFile(hFile);
  if (nVersion == 5) {
    SStrPrintf(szLeague, 32, "League_%d.cnd", sgbLeagueId);
    if (!SFileOpenFileEx(pCtx->hArchive, szLeague, 0, &hFile))
      return 1;
    SFileCloseFile(hFile);
  }
  pCtx->nVersion = nVersion;
  return 0;
}

// The 32-byte header the TYPE and VER chunks are read into. maphdr_header_tbl
// (0x4EE7F0) holds their two 12-byte records, "TYPE" -> maphdr_TYPE and
// "VER " -> maphdr_VER, and maphdr_VER writes the version word at +0x18, which
// GetMapVersionIndex reads back.
struct mapheader_t {
  /*0x00*/ BYTE _pad00[0x18];
  /*0x18*/ WORD wVersion;   // maphdr_VER's destination
  /*0x1A*/ BYTE _pad1A[2];
  /*0x1C*/ int nMapId;      // OpenScenarioArchive's map id
};
static_assert(offsetof(mapheader_t, wVersion) == 0x18, "mapheader_t::wVersion");
static_assert(offsetof(mapheader_t, nMapId) == 0x1C, "mapheader_t::nMapId");
static_assert(sizeof(mapheader_t) == 0x20, "SMemZero(&hdr, 20h)");

// The .rdata version table at 0x4EEB80, three 40-byte rows. Each holds a
// version, four {table, count} chunk-handler sets and an expansion-only flag;
// the handler tables (0x4EE808..0x4EEB7C, 12-byte {tag, handler, flags}) are
// where every maphdr_* handler is reached from. The sets:
//   sets[0] -- map_load_1 pass 1
//   sets[1] -- sub_46A640
//   sets[2] -- map_load_1 pass 2, not a scenario game
//   sets[3] -- map_load_1 pass 2, a scenario game
struct chunkset_t {
  /*0x00*/ const chunkhandler_t *pTbl;
  /*0x04*/ int nCount;
};
OC_X86_LAYOUT(sizeof(chunkset_t) == 8, "chunkset_t: [eax] / [eax+4h]");

struct mapver_t {
  /*0x00*/ DWORD dwVersion;      // compared with hdr->wVersion
  /*0x04*/ chunkset_t sets[4];   // the four handler sets map_load_1 selects
  /*0x24*/ DWORD bExpansionOnly;
};
static_assert(offsetof(mapver_t, dwVersion) == 0x00, "mapver_t::dwVersion");
OC_X86_LAYOUT(offsetof(mapver_t, sets) == 0x04, "mapver_t::sets");
OC_X86_LAYOUT(offsetof(mapver_t, bExpansionOnly) == 0x24, "mapver_t::bExpansionOnly");
OC_X86_LAYOUT(sizeof(mapver_t) == 0x28, "stride: `sub ecx, 28h`");

// The version table and the two-entry chunk handler table for TYPE and VER, declared
// here, where mapver_t is complete.
#ifdef OC_DLL_MODE
  #ifndef maphdr_versions_g
  #define maphdr_versions_g ((const mapver_t *)OC_VA(0x4EEB80))
  #endif
  #ifndef maphdr_header_tbl_g
  #define maphdr_header_tbl_g ((const chunkhandler_t *)OC_VA(0x4EE7F0))
  #endif
#else
extern const mapver_t maphdr_versions_g[3] OC_UNALIGNED;        // 0x4EEB80 (.rdata)
extern const chunkhandler_t maphdr_header_tbl_g[]; // 0x4EE7F0 (.rdata)
#endif

// Hyperion: the map versions, with the two that StarCraft: Remastered
// saves. Read from Remastered's loader: 64 is 63 and 206 is 205 with
// one change in each of their four handler sets, STR made optional and STRx,
// optional, added right after it. So a map may carry either, and STRx wins when
// it has both, the sets running in table order. 64 plays without Brood War, as
// 63 does; 206 needs it, as 205 does. Remastered lists the rows in this order
// and walks them from the end, as GetMapVersionIndex does.
//
// Every version also takes CRGB, optional, in the two pass-2 sets, where the
// played map is read (Remastered reads it in pass 1, which the map list runs for
// every map it shows). Every set takes HXPL, optional and first, so a Hyperion
// map's players past 7 are known before the sections that read them,
// HXRG, optional, after it (their RGB colours, stage G) and HXTE, optional,
// before TRIG (the slots past 11 its triggers run for). The rows are
// 1.17's with those additions, built on first use.
//
// Hyperion's two versions, for a map past what a classic map holds (maplimits.h), are
// read as 64 and 206 are.
static const int kMapVersions = 7;
static const int kMapSetMax = 24;  // 63's scenario set, 19, with HXPL, HXRG, HXTE, STRx and CRGB
                                   // (a set without UNIT, far smaller, adds it)
static mapver_t g_mapVersions[kMapVersions];
static chunkhandler_t g_mapDerived[kMapVersions][4][kMapSetMax];

static void MapAddHandler(chunkhandler_t *p, const char *pszId, chunkhandler_fn pfn) {
  memset(p, 0, sizeof *p);
  memcpy(&p->dwId, pszId, 4);
  p->pfnHandler = pfn;
}

// A copy of pFrom's handlers after HXPL: with bStrx, STR made optional and STRx
// added after it; with bCrgb, CRGB added at the end.
static void MapDeriveSet(const chunkset_t *pFrom, bool bStrx, bool bCrgb, chunkhandler_t *pTbl,
                         chunkset_t *pTo) {
  int n = 0;
  MapAddHandler(&pTbl[n++], "HXPL", PlayersMapHxpl);
  MapAddHandler(&pTbl[n++], "HXRG", PlayersMapHxrg);  // Hyperion: RGB colours (stage G)
  MapAddHandler(&pTbl[n++], "HXTE", TriggersMapHxte);  // Hyperion: triggers past 11
  bool bUnits = false;
  for (int i = 0; i < pFrom->nCount; i++) {
    pTbl[n++] = pFrom->pTbl[i];
    bUnits = bUnits || memcmp(&pTbl[n - 1].dwId, "UNIT", 4) == 0;
    if (!bStrx || memcmp(&pTbl[n - 1].dwId, "STR ", 4) != 0)
      continue;
    pTbl[n - 1].bRequired = 0;
    MapAddHandler(&pTbl[n++], "STRx", maphdr_STRx);
  }
  if (bCrgb)
    MapAddHandler(&pTbl[n++], "CRGB", maphdr_CRGB);
  // Hyperion: a set that does not place the units counts them, for the unit table
  // (port_pools.cpp)
  if (!bUnits)
    MapAddHandler(&pTbl[n++], "UNIT", PoolsMapUnit);
  pTo->pTbl = pTbl;
  pTo->nCount = n;
}

static const mapver_t *MapVersions(void) {
  if (g_mapVersions[0].dwVersion == 0) {
    // 64 is derived from 63, 206 from 205, and Hyperion's two the same way.
    static const DWORD kDerived[kMapVersions - 3] = {64, 206, MAP_VERSION_HX_ORIGINAL, MAP_VERSION_HX};
    for (int i = 0; i < kMapVersions; i++) {
      bool bDerived = i >= 3;
      const mapver_t &from = maphdr_versions_g[bDerived ? 1 + (i - 3) % 2 : i];
      mapver_t &to = g_mapVersions[i];
      to = from;
      if (bDerived)
        to.dwVersion = kDerived[i - 3];
      for (int s = 0; s < 4; s++)
        MapDeriveSet(&from.sets[s], bDerived, s >= 2, g_mapDerived[i][s], &to.sets[s]);
    }
  }
  return g_mapVersions;
}

// GetMapVersionIndex (0x469700, 186 B) -- reads the TYPE and VER chunks out of
// an already-loaded map image and maps the version word onto a
// maphdr_versions_g row.
BOOL __fastcall GetMapVersionIndex(void *pBuffer, DWORD dwSize, int *pnIndex,
                                   mapheader_t *pHdr) {
  mapheader_t hdr;

  // The mask is assigned again in the else arm, as 1.17 does.
  int nValid = pBuffer ? -1 : 0;
  if (pnIndex)
    *pnIndex = 0;
  else
    nValid = 0;
  if (!nValid)
    goto err;

  {
    SMemZero(&hdr, sizeof(hdr));
    // The caller's buffer when it supplies one, else the 32-byte local.
    mapheader_t *p = pHdr ? pHdr : &hdr;
    if (!ReadBufferChunks(pBuffer, dwSize, maphdr_header_tbl_g, 2, p))
      return FALSE;
    // `i-- == 0` in a do/while, as 1.17's loop is.
    // Hyperion: MapVersions, 1.17's three rows, Remastered's two and Hyperion's two.
    const mapver_t *pVersions = MapVersions();
    int i = kMapVersions;
    do {
      if (i-- == 0)
        return FALSE;
    } while (pVersions[i].dwVersion != p->wVersion);
    if (pVersions[i].bExpansionOnly != 0 && gbExpansion_g == 0)
      return FALSE;
    *pnIndex = i;
    return TRUE;
  }

err:
  SErrSetLastError(kErrInvalidParameter);
  return FALSE;
}

// is_color_cycled (0x4697C0, 65 B). Walks the tileset's colour-cycle ranges:
// 8-byte entries from tbl+0x0A, {lo at p[-1], hi at p[0]}, continuing while
// p[-2], the next entry's flag, is non-zero. tbl+8 gates the whole walk.
BOOL __fastcall is_color_cycled(int nColor) {
  BYTE *tbl = sgCycleTbl[g_MapTileset];
  if (tbl[8] == 0)
    return FALSE;
  BYTE *p = tbl + 10;
  do {
    if (nColor >= p[-1] && nColor <= p[0])
      return TRUE;
    p += 8;
  } while (p[-2] != 0);
  return FALSE;
}

// The shared partner search, a file-static helper the compiler inlines at each
// call site: the inline boundary keeps a redundant null test 1.17 has, which a
// loop written out loses.
static maplink_t *find_partner(maplink_t *pNode) {
  maplink_t *p = pNode->pPrev;
  while (p) {
    if (p->nLink == pNode->nId)
      break;
    p = p->pPrev;
  }
  return p;
}

// sub_469810 -- the addon half of the map-load fixup pass, the twin of
// sub_4698E0 (the nydus half) below: the same walk, cleanup and retire-both
// tail, but it calls sub_46A320 where the nydus half tail-jumps it. The
// free-and-unlink tail (line 803) is written out in both.
maplink_t *sub_469810(maplink_t *pNode) {
  if (!pNode)
    return NULL;
  maplink_t *pHead = pNode->pPrev;
  // find_partner reads pNode->pPrev itself rather than taking the pHead local.
  maplink_t *p = find_partner(pNode);
  if (p) {
    CUnit *pAddon = pNode->pUnit;
    CUnit *pBase;
    if (gulUnitTypeFlags[pAddon->wType] & 2) {
      pBase = p->pUnit;
    } else {
      pBase = pAddon;
      pAddon = p->pUnit;
    }
    WORD wAddonType = pAddon->wType;
    WORD wBaseType = pBase->wType;
    if (TTBaseUnitType(wAddonType) == wBaseType)
      pBase->sub_4359C0(pAddon);
    pHead = pNode->pPrev;
    if (pHead)
      pHead->pNext = NULL;
    SMemFree(pNode, "maphdr.cpp", 803, 0);
    return sub_46A320(pHead, p);
  }
  if (pHead)
    pHead->pNext = NULL;
  SMemFree(pNode, "maphdr.cpp", 803, 0);
  return pHead;
}

// sub_4698E0 -- pair one Nydus link with its partner (the node whose nLink
// names this node's nId), cross-connect the two units, then retire both list
// nodes.
maplink_t *sub_4698E0(maplink_t *pNode) {
  if (!pNode)
    return NULL;
  maplink_t *pHead = pNode->pPrev;
  // find_partner reads pNode->pPrev itself, and the search is a `while` with a
  // `break`; in other spellings the compiler inlines sub_46A320 here.
  maplink_t *p = find_partner(pNode);
  if (p) {
    pNode->pUnit->pUnionD0 = p->pUnit;
    p->pUnit->pUnionD0 = pNode->pUnit;
    pHead = pNode->pPrev;
    if (pHead)
      pHead->pNext = NULL;
    SMemFree(pNode, "maphdr.cpp", 803, 0);
    return sub_46A320(pHead, p);
  }
  if (pHead)
    pHead->pNext = NULL;
  SMemFree(pNode, "maphdr.cpp", 803, 0);
  return pHead;
}

// map_load_1 -- run one pass of the chunk reader over an already-loaded map
// image, choosing the handler table by map version and pass.
BOOL __fastcall map_load_1(void *pBuffer, DWORD dwSize, int nPass,
                           mapheader_t *pHdr) {
  int nIdx = 0;
  if (!GetMapVersionIndex(pBuffer, dwSize, &nIdx, pHdr))
    return FALSE;
  BOOL bResult = FALSE;
  // Hyperion: the rows are MapVersions', which has Remastered's too.
  PlayersMapBegin();  // Hyperion: HXPL
  if (nPass == 1)
    PoolsMapBegin();  // Hyperion: and the map's units
  if (nPass == 1)
    bResult = ReadBufferChunks(pBuffer, dwSize, MapVersions()[nIdx].sets[0].pTbl,
                            MapVersions()[nIdx].sets[0].nCount, pHdr);
  else if (nPass == 2) {
    const chunkset_t *pSet = IsScenarioGame(NULL)
                                 ? &MapVersions()[nIdx].sets[3]
                                 : &MapVersions()[nIdx].sets[2];
    PortMapColorsBegin();  // Hyperion: CRGB (port_map.cpp)
    bResult = ReadBufferChunks(pBuffer, dwSize, pSet->pTbl, pSet->nCount, NULL);
  }
  return bResult;
}

// LoadMapPass1 -- open the named scenario, load staredit\scenario.chk out of it
// and run the pass-1 handler set. The close sequence at the MemFileLoad
// failure is close_map written out; the two later exits call it.
BOOL LoadMapPass1(const char *pszName, mapheader_t *pHdr) {
  char szDir[0x104];
  char szPath[0x104];
  DWORD dwSize;
  int nIdx;
  void *pChk;
  BOOL bResult;
  // The three guards are full-width masks and SStrLen is evaluated twice, the
  // second result unused: the maphdr family's guard shape, not a `&&`.
  int nMask = (SStrLen(pszName) ? -1 : 0) & (pszName ? -1 : 0) & (pHdr ? -1 : 0);
  SStrLen(pszName);
  if (!nMask) {
    SErrSetLastError(kErrInvalidParameter);
    return FALSE;
  }
  if (!OpenScenarioArchive(pszName, szDir, 0x104, &pHdr->nMapId))
    return FALSE;
  dwSize = 0;
  if (szDir[0] != 0)
    psnprintf(szPath, 0x104, "%s\\%s", szDir, "staredit\\scenario.chk");
  else
    SStrCopy(szPath, "staredit\\scenario.chk", 0x104);
  pChk = MemFileLoad(szPath, 0, &dwSize, 1, 0, "maphdr.cpp", 2061);
  if (!pChk) {
    if (ghScenArchive) {
      SFileCloseArchive(ghScenArchive);
      ghScenArchive = NULL;
    }
    return FALSE;
  }
  nIdx = 0;
  if (!GetMapVersionIndex(pChk, dwSize, &nIdx, pHdr)) {
    mem_free(pChk, "maphdr.cpp", 2069);
    close_map();
    return FALSE;
  }
  // Hyperion: the rows are MapVersions', which has Remastered's too.
  PlayersMapBegin();  // Hyperion: HXPL
  PoolsMapBegin();    // Hyperion: and the map's units
  bResult = ReadBufferChunks(pChk, dwSize, MapVersions()[nIdx].sets[0].pTbl,
                             MapVersions()[nIdx].sets[0].nCount, pHdr);
  mem_free(pChk, "maphdr.cpp", 2078);
  close_map();
  return bResult;
}

// LoadMapPass2 -- LoadMapPass1's twin over the pass-2 handler sets, which the
// scenario flag picks between.
BOOL LoadMapPass2(const char *pszName) {
  char szDir[0x104];
  char szPath[0x104];
  DWORD dwSize;
  int nIdx;
  void *pChk;
  BOOL bResult;
  if (!OpenScenarioArchive(pszName, szDir, 0x104, NULL))
    return FALSE;
  if (szDir[0] != 0)
    psnprintf(szPath, 0x104, "%s\\%s", szDir, "staredit\\scenario.chk");
  else
    SStrCopy(szPath, "staredit\\scenario.chk", 0x104);
  pChk = MemFileLoad(szPath, 0, &dwSize, 1, 0, "maphdr.cpp", 2142);
  if (!pChk) {
    if (ghScenArchive) {
      SFileCloseArchive(ghScenArchive);
      ghScenArchive = NULL;
    }
    return FALSE;
  }
  nIdx = 0;
  if (!GetMapVersionIndex(pChk, dwSize, &nIdx, NULL)) {
    mem_free(pChk, "maphdr.cpp", 2150);
    if (ghScenArchive) {
      SFileCloseArchive(ghScenArchive);
      ghScenArchive = NULL;
    }
    return FALSE;
  }
  {
    // Hyperion: the rows are MapVersions', which has Remastered's too.
    const chunkset_t *pSet = IsScenarioGame(NULL)
                                 ? &MapVersions()[nIdx].sets[3]
                                 : &MapVersions()[nIdx].sets[2];
    PortMapColorsBegin();  // Hyperion: CRGB (port_map.cpp)
    PlayersMapBegin();     // Hyperion: HXPL
    bResult = ReadBufferChunks(pChk, dwSize, pSet->pTbl, pSet->nCount, NULL);
  }
  mem_free(pChk, "maphdr.cpp", 2160);
  if (ghScenArchive) {
    SFileCloseArchive(ghScenArchive);
    ghScenArchive = NULL;
  }
  return bResult;
}

// get_map_chk -- open the scenario archive, load staredit\scenario.chk out of
// it, close the archive again and hand the caller the buffer. The close
// sequence is written out twice, as 1.17 has it; neither path calls close_map.
void *__fastcall get_map_chk(const char *pszMapName, DWORD *pdwSize) {
  char szPath[0x104];
  char szName[0x104];
  DWORD dwSize;

  *pdwSize = 0;
  if (!OpenScenarioArchive(pszMapName, szName, 0x104, NULL))
    return NULL;

  // 0x4ED630 is "%s\\%s" and 0x4EEBF8 is "staredit\\scenario.chk".
  if (szName[0] != 0)
    psnprintf(szPath, 0x104, "%s\\%s", szName, "staredit\\scenario.chk");
  else
    SStrCopy(szPath, "staredit\\scenario.chk", 0x104);

  void *pChk = MemFileLoad(szPath, 0, &dwSize, 1, 0, "maphdr.cpp", 2188);
  if (!pChk) {
    if (ghScenArchive) {
      SFileCloseArchive(ghScenArchive);
      ghScenArchive = NULL;
    }
    return NULL;
  }
  if (ghScenArchive) {
    SFileCloseArchive(ghScenArchive);
    ghScenArchive = NULL;
  }
  *pdwSize = dwSize;
  return pChk;
}

// sub_469DB0 -- open the current map's scenario archive. The casts are to
// OpenScenarioArchive's declaration in src117/h/maphdr.h.
BOOL sub_469DB0(intptr_t a1, int a2) {
  void *pMap = GetCurrentMap(1);
  if (pMap == NULL)
    return FALSE;
  return OpenScenarioArchive((const char *)pMap, (char *)a1, a2, (int *)0);
}

// OpenScenarioArchive -- authenticate_map's sibling, but it PUBLISHES the handle
// in ghScenArchive rather than handing it back, and it has a pass-through mode
// selected by the WORD at 0x526524.
BOOL __fastcall OpenScenarioArchive(const char *pszName, char *pszDest,
                                    int nDestMax, int *pnMapId) {
  if (word_526524_g != 0) {
    if (pszDest)
      SStrCopy(pszDest, pszName, nDestMax);
    return TRUE;
  }
  if (pszDest)
    *pszDest = 0;

  HANDLE hArchive;
  int nSigStatus;
  mapauth_t ctx;

  // A positive guard, not an early return, which keeps 1.17's three separate
  // tails.
  if (SFileOpenArchive(pszName, 0, 0, &hArchive)) {
    ctx.hArchive = hArchive;
    ctx.nMapId = 0;
    sub_4C1350(reinterpret_cast<chunkenum_fn>(sub_469650), &ctx);
    // A named local seeded from the field, not an `&&` chain in a ternary, as
    // 1.17 holds the value.
    int nMapId = ctx.nMapId;
    if (nMapId != 0)
      nMapId = (map_validate_signature(pszName, &nSigStatus) && nSigStatus == 1)
                   ? ctx.nMapId
                   : 0;
    ghScenArchive = hArchive;
    if (pnMapId)
      *pnMapId = nMapId;
    return TRUE;
  }
  return FALSE;
}

// PlaceGameStart -- place the starting units, unless a saved game is being
// restored. Both arms end in gamemap_choose_center, not joined.
void PlaceGameStart(void) {
  if (gpLoadGameFile != NULL)
    return;
  ModRacesWithout();  // Hyperion: a mod's races
  ModTechsForGame();  // Hyperion: and its techs to research
  if (IsTeamGame())
    TeamPlaceStartingUnits();
  else
    PlaceStartingUnits();
  gamemap_choose_center();
}

// maphdr_data_init -- oracle 0x469ED0. Loads the four DAT tables the map
// header needs, then resets every per-player tech and upgrade table for a new
// game.
void maphdr_data_init(void) {
  int i;
  load_array_data("arr\\upgrades.dat", gUPGRADESArrayDataTbl);
  load_array_data("arr\\weapons.dat", gWEAPONSArrayDataTbl_g);
  load_array_data("arr\\techdata.dat", gTECHDATAArrayDataTbl_g);
  load_array_data("arr\\portdata.dat", gPORTDATAArrayDataTbl_g);
  if (gpLoadGameFile)
    return;
  PlayerFill(PlayerUnitsAvailable, 1);  // Hyperion: and Hyperion's slots
  memset(gbTechAvailSC, 1, 0x120);
  memset(gbTechAvailBW + 0x18, 1, 0xF0);
  memset(gbTechResearchedSC, 0, 0x120);
  memset(gbTechResearchedBW + 0x18, 0, 0xF0);
  memset(gTechInProgress.a, 0, 0x48);
  // memcpy over the `.b` arrays; a struct assignment copies differently.
  for (i = 0; i < 12; i++) {
    memcpy(byte_534368_g[i].b, upg_repeats, sizeof(upgmax_t));
    memcpy(byte_536558_g[i].b, &upg_repeats[0x2E], sizeof(upgmaxbw_t));
  }
  memset(gbUpgradeLevelSC, 0, 0x228);
  memset(gbUpgradeLevelBW + 0x2E, 0, 0xB4);
  memset(gUpgradeInProgress.a, 0, 0x60);
  PlayerTechInit(upg_repeats);  // Hyperion: Hyperion's slots
  memset(gbSlotRemap_g, 0, 8);
  resetUnitProps();
  memset(byte_526526, 0, 32);
}

// SetHangarContents -- fill a Carrier/Reaver's hangar with as many fighters as
// the map asked for, capped at the unit's capacity.
void SetHangarContents(CUnit *pUnit, DWORD dwProps, BYTE bCount) {
  if (!(dwProps & 0x200000))
    return;
  WORD wType = pUnit->GetHangarType();
  if (wType == 0)
    return;
  BYTE bCap = pUnit->GetHangarCapacity();
  // The int capacity local keeps the compare signed and wide, as 1.17 has it;
  // bCount is read twice, and bPlayer gets its own local.
  int nCap = bCap;
  BYTE n = bCount;
  if (bCount > nCap)
    n = (BYTE)nCap;
  while (n) {
    --n;
    CSprite *pSprite = pUnit->pSprite;
    BYTE bPlayer = pUnit->bPlayer;
    // WORD locals, then an explicit (short) at the use, as 1.17 sign-extends
    // them.
    upos_t wY = pSprite->wPosY;
    upos_t wX = pSprite->wPosX;
    CUnit *pNew = CreateUnitAt(wType, PosS(wX), PosS(wY), bPlayer);
    if (pNew) {
      pNew->FinishedMaking();
      pNew->pHangarParent = pUnit;
      pUnit->HangarConstructionFinished(pNew);
    }
  }
}

// apply_liftoff -- one of apply_unit_properties' CHK-property appliers: put a
// liftable building in the air if the map asked for it.
void apply_liftoff(CUnit *pUnit, DWORD dwProps, int nArg2) {
  if (!(dwProps & 4))
    return;
  if (nArg2 == 0)
    return;
  if (OrderAllowed(0x48, pUnit->bPlayer, pUnit) != 1)
    return;
  pUnit->pSprite->SetShadowOffset(0x2A);
  pUnit->pSprite->SetAnimation(0x0D, 1);
  pUnit->UntrackCollision();
  pUnit->MakeLifted();
  pUnit->pSprite->bElevationLevel = 0x0C;
  pUnit->dwFlags |= 4;
  pUnit->ResetMovementState();
  pUnit->TrackCollision();
}

// setup_startloc -- record one start location, and if it is the local
// player's (or, in a replay, the first one seen) centre the view on it.
int setup_startloc(chk_unit_t *pUnit, point16_t *pStarts) {
  if (pUnit->wType != 0xD6)
    return FALSE;
  pStarts[pUnit->props.bOwner].x = pUnit->wX;
  pStarts[pUnit->props.bOwner].y = pUnit->wY;
  // A goto to one shared exit, as 1.17 has one; two `return TRUE;` duplicate
  // it.
  if (gPlayback) {
    if (gwScreenTileX_g != 0xFFFF)
      goto out;
  } else if (pUnit->props.bOwner != g_LocalNationID) {
    goto out;
  }
  {
    // The WORD locals feed the clamp directly, `< 0 ? 0 : x`, as 1.17 computes
    // it.
    WORD wTx = pUnit->wX;
    wTx >>= 5;
    WORD wTy = pUnit->wY;
    wTy >>= 5;
    // Hyperion: half the view's width in megatiles, 10 in 1.17.
    gwScreenTileX_g = (WORD)(wTx - g_portView.nWidth / 64 < 0 ? 0 : wTx - g_portView.nWidth / 64);
    gwScreenTileY_g = (WORD)(wTy - 6 < 0 ? 0 : wTy - 6);
  }
out:;
  return TRUE;
}

// sub_46A320 -- unlink one node and free it; returns the head, which may have
// advanced.
maplink_t *sub_46A320(maplink_t *pHead, maplink_t *pNode) {
  if (pNode->pNext)
    pNode->pNext->pPrev = pNode->pPrev;
  if (pNode->pPrev)
    pNode->pPrev->pNext = pNode->pNext;
  if (pNode == pHead) {
    pHead = pHead->pPrev;
    if (pHead)
      pHead->pNext = NULL;
  }
  SMemFree(pNode, "maphdr.cpp", 819, 0);
  return pHead;
}

// sub_46A370 -- rebuild every live unit's selection circle.
void sub_46A370(void) {
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext)
    p->CircleUpdate();
}

// The tileset colour-cycle descriptor, an 8-byte stride; is_color_cycled walks
// the same entries from +6.
struct colorcycle_t {
  /*0x00*/ DWORD dwRate;   // vid_set_cycle's second argument
  /*0x04*/ BYTE bSteps;    // the loop's continue flag and vid_set_cycle's EDX
  /*0x05*/ BYTE bLo;       // is_color_cycled's low bound
  /*0x06*/ BYTE bHi;       // is_color_cycled's high bound
  /*0x07*/ BYTE bFlags;    // vid_set_cycle's last argument
};
static_assert(offsetof(colorcycle_t, dwRate) == 0x00, "colorcycle_t::dwRate");
static_assert(offsetof(colorcycle_t, bSteps) == 0x04, "colorcycle_t::bSteps");
static_assert(offsetof(colorcycle_t, bLo) == 0x05, "colorcycle_t::bLo");
static_assert(offsetof(colorcycle_t, bHi) == 0x06, "colorcycle_t::bHi");
static_assert(offsetof(colorcycle_t, bFlags) == 0x07, "colorcycle_t::bFlags");
static_assert(sizeof(colorcycle_t) == 8, "init_cycles advances `add esi, 8h`");

// init_color_cycles -- a tail-jmp thunk into init_cycles.
void init_color_cycles(void) {
  init_cycles((colorcycle_t *)(sgCycleTbl[g_MapTileset] + 4));
}

// init_cycles -- program every cycle slot of one tileset.
void init_cycles(colorcycle_t *pCycles) {
  DWORD dwMask = 1;
  vid_init_cycles();
  BYTE bSlot = 0;
  if (pCycles->bSteps == 0)
    return;
  do {
    vid_set_cycle(bSlot, pCycles->bSteps, pCycles->bLo, pCycles->bHi,
                  pCycles->dwRate, pCycles->bFlags);
    if (dwMask & *(WORD *)sgCycleTbl[g_MapTileset])
      vid_start_cycle(bSlot);
    ++pCycles;
    ++bSlot;
    dwMask += dwMask;
  } while (pCycles->bSteps != 0);
}

// apply_unit_properties -- stamp one CHK record's property block onto a unit it
// has just created. Each member is gated twice: once by the record's own valid
// mask and, for the state flags, once by the flags word at +0x0E.
void apply_unit_properties(CUnit *pUnit, chk_unit_props_t *pProps) {
  if (pProps->dwProps & 0x20000) {
    // The cast makes the /100 unsigned, as in 1.17; the narrower tables below
    // are never negative.
    DWORD dwLife = (DWORD)HandicapLife(gxUnitLife[pUnit->wType], pUnit->bPlayer) * pProps->bHitPointsPct / 100;  // Hyperion: at the owner's handicap
    pUnit->SetLife(dwLife == 0 ? 1 : dwLife);
  }
  {
    int nMax = HandicapShields(guwUnitEnergy[pUnit->wType], pUnit->bPlayer);  // Hyperion: at the owner's handicap
    int nShields = (DWORD)(nMax * pProps->bShieldsPct) / 100;
    if (pProps->dwProps & 0x40000)
      pUnit->lShields = (nMax < nShields ? nMax : nShields) << 8;
  }
  {
    // The percentage is read before the call, as 1.17 orders it. The min is a
    // macro.
    BYTE bPct = pProps->bEnergyPct;
    DWORD dwFlags = pProps->dwProps;
    BYTE bEnergy = (BYTE)((DWORD)(bPct * pUnit->GetMaxEnergyReal()) / 100);
    if (dwFlags & 0x80000)
      pUnit->wEnergy = (WORD)((pUnit->GetMaxEnergyReal() < bEnergy
                                   ? pUnit->GetMaxEnergyReal()
                                   : bEnergy)
                              << 8);
  }
  if (pProps->dwProps & 0x100000) {
    if (gulUnitTypeFlags[pUnit->wType] & 0x2000) {
      // Hyperion: the map's amount at a mutator's share
      pUnit->wResourceAmount = MutatorResource(pProps->wResources, !pUnit->IsMineralSrc());
      if (pUnit->IsMineralSrc())
        pUnit->UpdateResourceAnim();
    }
  }
  SetHangarContents(pUnit, pProps->dwProps, pProps->bHangarCount);
  if ((pProps->dwProps & 1) && (pProps->wStateFlags & 1)) {
    if (TechUseAllowed(pUnit->GetCloakingTech(), pUnit->bPlayer, pUnit) == 1)
      pUnit->sub_41D410();
  }
  if ((pProps->dwProps & 2) && (pProps->wStateFlags & 2)) {
    if (TechUseAllowed(11, pUnit->bPlayer, pUnit) == 1) {
      pUnit->pSprite->SetAnimation(0x0E, 1);
      pUnit->ReplaceWithBurrowedSprite();
      pUnit->sub_41D410();
      pUnit->dwFlags |= 0x200000;
      pUnit->SetOrder(0x75);
    }
  }
  apply_liftoff(pUnit, pProps->dwProps, pProps->wStateFlags & 4);
  if ((pProps->dwProps & 8) && (pProps->wStateFlags & 8))
    pUnit->sub_430A00();
  if ((pProps->dwProps & 0x10) && (pProps->wStateFlags & 0x10))
    pUnit->dwFlags |= 0x4000000;
  if (pUnit->wType == 0x28) {
    WORD wTtl = (WORD)pUnit->GetMaxTimeToLive();
    if (pUnit->wTimeToLive == 0 || wTtl < pUnit->wTimeToLive)
      pUnit->wTimeToLive = wTtl;
  }
}

// free_map_strs (0x46A610, 38 B) -- the app-exit callback maphdr_STR registers.
void __fastcall free_map_strs(void) {
  // The NULL store is unconditional.
  if (gpMapStrs_g)
    SMemFree(gpMapStrs_g, "maphdr.cpp", 267, 0);
  gpMapStrs_g = NULL;
  PortMapStringsWide(FALSE);  // Hyperion: no table (port_map.cpp)
}

// sub_46A640 -- load the current map's scenario.chk out of its archive and run
// the sub_46A640 handler set (maphdr_versions_g[i].sets[1]) over it. The two
// path literals are spelled as get_map_chk's, with doubled backslashes.
BOOL sub_46A640(void) {
  char szPath[0x104];
  char szDir[0x104];
  int nIdx;
  DWORD dwSize;

  void *pMap = GetCurrentMap(1);
  if (pMap) {
    if (OpenScenarioArchive((const char *)pMap, szDir, 0x104, NULL)) {
      dwSize = 0;
      if (szDir[0] != 0)
        psnprintf(szPath, 0x104, "%s\\%s", szDir, "staredit\\scenario.chk");
      else
        SStrCopy(szPath, "staredit\\scenario.chk", 0x104);
      void *pFile = MemFileLoad(szPath, 0, &dwSize, 1, 0, "maphdr.cpp", 2096);
      if (!pFile)
        return FALSE;
      nIdx = 0;
      if (!GetMapVersionIndex(pFile, dwSize, &nIdx, NULL)) {
        mem_free(pFile, "maphdr.cpp", 2105);
        return FALSE;
      }
      // Hyperion: the rows are MapVersions', which has Remastered's too.
      BOOL bResult = ReadBufferChunks(pFile, dwSize,
                                      MapVersions()[nIdx].sets[1].pTbl,
                                      MapVersions()[nIdx].sets[1].nCount, NULL);
      mem_free(pFile, "maphdr.cpp", 2114);
      return bResult;
    }
  }
  return FALSE;
}
