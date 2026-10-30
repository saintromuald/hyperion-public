// saveload.cpp -- the save and load layer: the save file globals, the
// per-player tech and upgrade tables, the save path builders, the save
// directory's enumeration callbacks and the save-header codec (0x4989F0 to
// 0x49AA20).
//
// The path literals carry doubled backslashes; read each against the oracle's
// .rdata before changing it.

#include "oc_data.h"  // Hyperion: OC_VA
#include <windows.h>             // WIN32_FIND_DATAA
#include <string.h>              // memset, memcpy, strlen (inlined by /Oi)

#include "../h/platform.h"
#include "../h/saveload.h"
#include "../h/gds_log.h"
#include "../h/Storm.h"          // SStrCopy, SMemFree
#include "../h/rtti.h"           // type_info::raw_name -- Clear's alloc tag
#include "../h/CUnit.h"          // CUnit::IsDead (RestoreSelection)
#include "../h/CSprite.h"        // bVisibilityFlags (+0x0C) in the same body
#include "../h/usel.h"           // sub_4BCE70 (commit-selection)
#include "../h/net_cmds.h"       // gameloop_init (sub_498CD0)
#include "../h/gametype.h"       // GameHeader (write_player_data)
#include "../h/minimap.h"        // get_num_active_players
#include "../h/net_rand.h"       // net_randsequence
#include "../h/net_glue.h"       // IsGameCreator
#include "../h/grid.h"           // IsBroodwar
#include "../h/gluMap.h"         // FindMapFile
#include "../h/gds_file.h"       // pfwrite
#include "../h/gds_local.h"      // psnprintf
#include "../h/map_load.h"       // GetCurrentMap
#include "../h/CImage.h"         // CImage::LoadImages
#include "../h/CThingy.h"        // CThingy::LoadThingies
#include "../h/CFlingy.h"        // CFlingy::LoadFlingies
#include "../h/CUnitInit.h"      // read_save_units
#include "../h/CBullet.h"        // CBullet::LoadBullets
#include "../h/COrder.h"         // COrder::LoadOrders
#include "../h/creep.h"          // getCreepMap, getEdgeMap
#include "../h/tile2.h"          // tile2_read_from_save
#include "../h/victory.h"        // LoadTriggers
#include "../h/sai_LoadSave.h"   // read_save_paths
#include "../h/SAI_Main.h"       // read_save_ai_data
#include "../h/CUnitSave.h"      // sub_4313C0
#include "../h/uiSingle.h"       // sub_4BBB40
#include "../h/CUnitProtoss.h"   // DestroyPsiLinks, RestorePsiLinks
#include "../h/okcancel.h"       // do_wait_dlg, sub_47B090
#include "../h/net_time.h"       // net_elapsed_gametime
#include "../h/net_error.h"      // err_popup
#include "../h/net_record.h"     // net_record_write
#include "../gen/globals117.h"
#include "../port/data/port_formats.h"  // Hyperion
#include "../port/data/port_map.h"  // Hyperion: PortMapStringsWide, PortMapStringsAreWide
#include "../port/platform/port_paths.h"  // Hyperion: PortMapRelativePath
#include "../h/maplimits.h"        // Hyperion: maps past 256 tiles
#include "../h/port_hxsave.h"      // Hyperion: Hyperion's sections
#include "../h/port_hxreplay.h"    // Hyperion: ReplayHxSaved
#include "../h/hxrules.h"          // Hyperion: HxRules
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX, SELECTION_HUMANS

// Hyperion: the tiles a save's MAPS section holds: 1.17's 0x10000, or the map's
// when it has more.
static DWORD SaveMapTiles() {
  DWORD n = (DWORD)gwMapWidth * gwMapHeight;
  return n > MAP_TILES_117 ? n : MAP_TILES_117;
}

// Storm imports Storm.h does not carry, spelled as replay.cpp spells them.
// extern "C" gives the undecorated names (_SStrPack@12) that Storm's NONAME
// exports link against. SStrChrR is Storm ordinal 572 (the thunk at 0x4D00A0),
// a two-argument __stdcall returning a pointer into the caller's buffer;
// storm_imports.def supplies it.
extern "C" {
char * __stdcall SStrPack(char *pszDest, const char *pszSrc, size_t cchDest);
// Hyperion: as port_stormfile.cpp defines it
DWORD  __stdcall SFileGetUserDataPath(char *pszBuf, DWORD cchBuf, DWORD dwFlags);
char * __stdcall SStrChrR(const char *pszStr, char chFind);
}

// The gds_file.cpp helpers, spelled as replay.cpp's. pfgetc (0x4CC3B0) returns
// fgetc's int, the character or -1.
int    __fastcall pfclose(void *pFile);
BOOL   __fastcall pfdelete(char *pszPath, BOOL bAlsoReadOnly);
void * __fastcall pfopen(const char *pszName, short sMode);
int    __fastcall pfgetc(void *pFile);

// Spelled as CUnitTerran.h / CUnitTSpells.h spell it.
void __fastcall status_set_update();

// Globals this TU reaches, dual-spelled, one #ifndef per name so that the
// spelling in globals117.h wins where it has one (gbMultiPlayerMode comes from
// there). gbIsExpansion and gbLocalMask are spelled as CUnitUpgrade.cpp and
// CUnitUpdate2.cpp spell them.
#ifdef OC_DLL_MODE
#ifndef gpLoadGameFile
#define gpLoadGameFile    (*reinterpret_cast<void **>(OC_VA(0x6A5E34u)))
#endif
#ifndef gbIsLoadedGame
#define gbIsLoadedGame    (*reinterpret_cast<BYTE *>(OC_VA(0x6A5E38u)))
#endif
#ifndef gdwSaveHdrDword4
#define gdwSaveHdrDword4  (*reinterpret_cast<DWORD *>(OC_VA(0x6A5E40u)))
#endif
#ifndef gdwSaveTag
#define gdwSaveTag        (*reinterpret_cast<DWORD *>(OC_VA(0x6A5E44u)))
#endif
#ifndef sgSavedSelected_g
#define sgSavedSelected_g (reinterpret_cast<CUnit **>(OC_VA(0x6A5E48u)))
#endif
#ifndef gbTechAvailSC
#define gbTechAvailSC     (reinterpret_cast<BYTE *>(OC_VA(0x534104u)))
#endif
#ifndef gbTechAvailBW
#define gbTechAvailBW     (reinterpret_cast<BYTE *>(OC_VA(0x536318u)))
#endif
// The BW tables, gbTechAvailBW among them, have their base biased down by the
// first BW id (0x18 techs, 0x2E upgrades), because the id arrives un-rebased.
#ifndef gbTechResearchedSC
#define gbTechResearchedSC  (reinterpret_cast<BYTE *>(OC_VA(0x534224u)))
#endif
#ifndef gbTechResearchedBW
#define gbTechResearchedBW  (reinterpret_cast<BYTE *>(OC_VA(0x536408u)))
#endif
#ifndef gbUpgradeLevelSC
#define gbUpgradeLevelSC    (reinterpret_cast<BYTE *>(OC_VA(0x534590u)))
#endif
#ifndef gbUpgradeLevelBW
#define gbUpgradeLevelBW    (reinterpret_cast<BYTE *>(OC_VA(0x5365DEu)))
#endif
#ifndef gbMaxUpgradeLevelSC
#define gbMaxUpgradeLevelSC (reinterpret_cast<BYTE *>(OC_VA(0x534368u)))
#endif
// dword_6A5E3C, the save id sub_499C50 sets and sub_49A230 clears.
#ifndef dword_6A5E3C
#define dword_6A5E3C      (*reinterpret_cast<DWORD *>(OC_VA(0x6A5E3Cu)))
#endif
#ifndef gbLocalMask
#define gbLocalMask       (*reinterpret_cast<BYTE *>(OC_VA(0x672668u)))
#endif
// gLocalPlayerName @0x672690, a char buffer: SetLocalPlayerName copies at most
// 0x19 bytes into it, the bound every path builder applies to a name.
#ifndef gLocalPlayerName
#define gLocalPlayerName  (reinterpret_cast<char *>(OC_VA(0x672690u)))
#endif
// gwGameMode @0x6662B8, a WORD (sub_498CD0 reads and stores it as one).
#ifndef gwGameMode
#define gwGameMode        (*reinterpret_cast<WORD *>(OC_VA(0x6662B8u)))
#endif
// sgSavedGameList @0x509368, a TSList: +0 the unread dword, +4 the link, so
// 0x50936C is .link.prev and 0x509370 is .link.next.
#ifndef sgSavedGameList
#define sgSavedGameList   (*reinterpret_cast<SAVEDGAMELIST *>(OC_VA(0x509368u)))
#endif
// gTypeInfo_TSAVED_GAME_NODE @0x5109AC, the MSVC RTTI type descriptor for
// TSAVED_GAME_NODE in the oracle's .rdata, a global rather than typeid(T) so
// the dll-mode build uses the oracle's own.
#ifndef gTypeInfo_TSAVED_GAME_NODE
#define gTypeInfo_TSAVED_GAME_NODE (*reinterpret_cast<type_info *>(OC_VA(0x5109ACu)))
#endif
#else
extern void  *gpLoadGameFile;
extern BYTE   gbIsLoadedGame;
extern DWORD  gdwSaveHdrDword4;
extern DWORD  gdwSaveTag;
extern CUnit *sgSavedSelected_g[SELECTION_MAX];
extern BYTE   gbTechAvailSC[];
extern BYTE   gbTechAvailBW[];
extern BYTE   gbTechResearchedSC[];
extern BYTE   gbTechResearchedBW[];
extern BYTE   gbUpgradeLevelSC[];
extern BYTE   gbUpgradeLevelBW[];
extern BYTE   gbMaxUpgradeLevelSC[];
extern DWORD  dword_6A5E3C;
extern WORD   gwGameMode;
extern BYTE   gbLocalMask;
extern SAVEDGAMELIST sgSavedGameList;
extern type_info gTypeInfo_TSAVED_GAME_NODE;
// No extent: 0x19 is the copy's bound, not a known size.
extern char   gLocalPlayerName[];
#endif
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#ifndef gbMaxUpgradeLevelBW
#define gbMaxUpgradeLevelBW (reinterpret_cast<BYTE *>(OC_VA(0x53652Au)))
#endif
#else
extern BYTE   gbMaxUpgradeLevelBW[];
#endif
#ifdef OC_DLL_MODE
#ifndef gbIsExpansion
#define gbIsExpansion     (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#endif
#else
extern BYTE   gbIsExpansion;
#endif

// The tables sub_499C50 upgrades an old save from, and two path globals.
// sgSaveVersion_g is a DWORD read whole; sub_499C50 stores only its low word.
#ifdef OC_DLL_MODE
#ifndef sgSaveVersion_g
#define sgSaveVersion_g   (*reinterpret_cast<DWORD *>(OC_VA(0x6A5E78u)))
#endif
#ifndef gOldTechInProgress
#define gOldTechInProgress (reinterpret_cast<BYTE *>(OC_VA(0x534344u)))
#endif
#ifndef gOldUpgradeInProgress
#define gOldUpgradeInProgress (reinterpret_cast<BYTE *>(OC_VA(0x5347B8u)))
#endif
#ifndef word_526524
#define word_526524       (*reinterpret_cast<WORD *>(OC_VA(0x526524u)))
#endif
#ifndef gszSavePath
#define gszSavePath       (reinterpret_cast<char *>(OC_VA(0x52701Cu)))
#endif
#else
extern DWORD sgSaveVersion_g;
extern BYTE gOldTechInProgress[0x24] OC_UNALIGNED;
extern BYTE gOldUpgradeInProgress[0x48] OC_UNALIGNED;
extern WORD word_526524;
extern char gszSavePath[0x104] OC_UNALIGNED;
#endif
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#ifndef dword_5349DC
#define dword_5349DC      (*reinterpret_cast<DWORD *>(OC_VA(0x5349DCu)))
#endif
#ifndef gOldMapRegions
#define gOldMapRegions    (reinterpret_cast<BYTE *>(OC_VA(0x534A20u)))
#endif
#else
extern DWORD dword_5349DC;
extern BYTE gOldMapRegions[0x500];
#endif

// gGameHeader @0x6728D0, spelled as gluMap.cpp spells it.
#ifndef OC_OWN_gGameHeader
#define OC_OWN_gGameHeader
#ifdef OC_DLL_MODE
#define gGameHeader (*reinterpret_cast<GameHeader *>(OC_VA(0x6728D0u)))
#else
extern GameHeader gGameHeader;
#endif
#endif

// The context sub_499450 (inlined in load_game) hands SearchDirectory for the
// sub_4994B0 callback: 0x1C bytes, of which the first five fields are used.
struct SEARCHFILE_CTX {
  /*0x00*/ const char *pszName;
  /*0x04*/ BOOL (__stdcall *pfnFilter)(const char *, void *);
  /*0x08*/ void *pFilterArg;
  /*0x0C*/ char *pszOut;
  /*0x10*/ unsigned int nOutLen;
  /*0x14*/ DWORD adwUnused[2];
};
static_assert(offsetof(SEARCHFILE_CTX, pszName) == 0x00, "stricmp(cFileName, [esi])");
OC_X86_LAYOUT(offsetof(SEARCHFILE_CTX, pfnFilter) == 0x04, "mov eax, [esi+4h] / call eax");
OC_X86_LAYOUT(offsetof(SEARCHFILE_CTX, pFilterArg) == 0x08, "push [esi+8h]");
OC_X86_LAYOUT(offsetof(SEARCHFILE_CTX, pszOut) == 0x0C, "push [esi+0Ch] / SStrCopy");
OC_X86_LAYOUT(offsetof(SEARCHFILE_CTX, nOutLen) == 0x10, "cmp eax, [esi+10h] / jnb");
OC_X86_LAYOUT(sizeof(SEARCHFILE_CTX) == 0x1C, "sub esp, 1Ch in sub_499450");

// The 0xB5-byte save header write_player_data compresses. sub_499750 reads
// the magic back as a WORD at +0 and ReadSaveHeader publishes +4.
#pragma pack(push, 1)
struct SAVEGAME_HEADER {
  // sub_499C50 reads the version half alone (`mov ax, word ptr [ebp-0BAh]`).
  union {
    /*0x00*/ DWORD dwTag;
    struct {
      /*0x00*/ WORD wMagic;
      /*0x02*/ WORD wVersion;
    };
  };
  /*0x04*/ DWORD dwSeed;
  /*0x08*/ DWORD _unmapped_08;
  /*0x0C*/ GameHeader game;
  /*0x99*/ char szName[0x1C];
};
#pragma pack(pop)
static_assert(offsetof(SAVEGAME_HEADER, dwTag) == 0x00, "mov [ebp-0BCh], 30069h");
static_assert(offsetof(SAVEGAME_HEADER, wMagic) == 0x00, "the 0x69 half ReadSaveHeader validates");
static_assert(offsetof(SAVEGAME_HEADER, wVersion) == 0x02, "mov ax, word ptr [ebp-0BAh]");
static_assert(offsetof(SAVEGAME_HEADER, dwSeed) == 0x04, "mov [ebp-0B8h], eax");
static_assert(offsetof(SAVEGAME_HEADER, _unmapped_08) == 0x08, "never written");
static_assert(offsetof(SAVEGAME_HEADER, game) == 0x0C, "lea edi, [ebp-0B0h]");
static_assert(offsetof(SAVEGAME_HEADER, szName) == 0x99, "SStrCopy to hdr+99h");
static_assert(sizeof(SAVEGAME_HEADER) == 0xB5, "push 0B5h / call memset");

// The TSList unlink for TSAVED_GAME_NODE, the shape of victory.cpp's
// UnlinkTrigger and gametype.cpp's UnlinkTemplate: the link offset is recovered
// at run time, and the redundant inner `if` gives nOff the second definition
// 1.17's code has. Static, inlined into Clear.
static void UnlinkSavedGame(TSAVED_GAME_NODE *node) {
  TSLink<TSAVED_GAME_NODE> *pl = &node->link;
  TSAVED_GAME_NODE *pprev = pl->prev;
  if (pprev != NULL) {
    TSAVED_GAME_NODE *pnext = pl->next;
    TSLink<TSAVED_GAME_NODE> *pnl;
    if ((intptr_t)pnext <= 0) {
      // The terminator: its complement is the list's own link.
      pnl = reinterpret_cast<TSLink<TSAVED_GAME_NODE> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0) {
        nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
      }
      pnl = reinterpret_cast<TSLink<TSAVED_GAME_NODE> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    pl->prev->link.next = pl->next;
    pl->prev = NULL;
    pl->next = NULL;
  }
}

// TSList<TSAVED_GAME_NODE,...>::Clear -- oracle 0x4989F0 (84 B). The head is
// re-read every pass because the unlink has just patched it, and normalised to
// NULL before the test, as 1.17 has it.
void SAVEDGAMELIST::Clear(void) {
  for (;;) {
    TSAVED_GAME_NODE *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkSavedGame(p);
    SMemFree(p, gTypeInfo_TSAVED_GAME_NODE.raw_name(), -2, 0);
  }
}

// close_savefile -- oracle 0x498A50 (26 B). Close the open save file, if any,
// and clear the global.
void __fastcall close_savefile()
{
  if (gpLoadGameFile) {
    pfclose(gpLoadGameFile);
    gpLoadGameFile = 0;
  }
}

// DeleteSave -- oracle 0x498A70 (102 B). Build the save directory for the name
// in ECX and delete every save in it through DeleteSave_cb. These globs keep
// the leading '*' that GetSaveExtension skips.
void __fastcall DeleteSave(const char *pszName)
{
  char szDir[0x104];
  int nFound;

  if (MakeSaveDir(pszName, szDir, 0x104, 0)) {
    const char *pszExt;

    if (gbIsExpansion)
      pszExt = gbMultiPlayerMode ? "*.mlx" : "*.snx";
    else
      pszExt = gbMultiPlayerMode ? "*.mlt" : "*.sng";
    SearchDirectory(szDir, pszExt, DeleteSave_cb, 0, 0, 0, &nFound);
  }
}

// DeleteSave_cb -- oracle 0x498AE0 (79 B). SearchDirectory callback, ECX the
// directory and EDX the find data; the two stack words go unread. Delete the
// found file and return 1 to go on.
BOOL __fastcall DeleteSave_cb(char *pszPath, struct _WIN32_FIND_DATAA *pFind,
                           intptr_t nUnused0, intptr_t nUnused1)
{
  char szPath[0x104];

  SStrCopy(szPath, pszPath, 0x104);
  SStrPack(szPath, pFind->cFileName, 0x104);
  pfdelete(szPath, 1);
  return 1;
}

// sub_498B30 -- oracle 0x498B30 (151 B). Delete the local player's save named
// pszName (with the current extension) and return pfdelete's result. The
// directory comes from gLocalPlayerName, not the parameter. The three failures
// share one exit after the success path, so the ifs nest, as 1.17 has them.
int __fastcall sub_498B30(const char *pszName)
{
  char szPath[0x104];

  if (MakeSaveDir(gLocalPlayerName, szPath, 0x104, 0)) {
    if (strlen(szPath) + 0x22 < 0x104) {
      if (CreateDirectoriesRecursive(szPath)) {
        SStrPack(szPath, pszName, 0x104);
        SStrPack(szPath, GetSaveExtension(), 0x104);
        return pfdelete(szPath, 1);
      }
    }
  }
  return 0;
}

// load_game -- oracle 0x498BD0 (251 B). Find the named save in the local
// player's save directory and open it; on any miss close whatever save is
// open. sub_499450 is inlined here.
BOOL __fastcall load_game(DWORD dwUnused, const char *pszName)
{
  char szDir[0x104];
  char szFile[0x20];

  if (pszName && sub_499FF0(gLocalPlayerName, szDir, 0x104)) {
    SStrCopy(szFile, pszName, 0x20);
    SStrPack(szFile, GetSaveExtension(), 0x20);
    if (sub_499450(reinterpret_cast<intptr_t>(szFile), reinterpret_cast<intptr_t>(szDir), 0x104, 0, 0))
      return sub_498CD0(szDir);
  }
  close_savefile();
  return 0;
}

// sub_498CD0 -- oracle 0x498CD0 (65 B). Open the save and, in single player,
// set the game mode: 4 becomes 1, otherwise gameloop_init(1). BOOL: both
// success paths return 1 and the failure returns pfopen's zero.
BOOL __fastcall sub_498CD0(const char *pszPath)
{
  gpLoadGameFile = pfopen(pszPath, 1);
  if (!gpLoadGameFile)
    return 0;
  if (!gbMultiPlayerMode) {
    if (gwGameMode == 4)
      gwGameMode = 1;
    else
      gameloop_init(1);
  }
  return 1;
}

// GetSaveExtension -- oracle 0x4995A0 (53 B). The current save extension, one
// of the four globs (0x4EFAA4 "*.mlx", 0x4EFA9C "*.snx", 0x4EFA94 "*.mlt",
// 0x4EFA8C "*.sng") past its '*'. Each arm has its own `+ 1`, as 1.17 has it.
const char * __fastcall GetSaveExtension()
{
  if (gbIsExpansion)
    return (gbMultiPlayerMode ? "*.mlx" : "*.snx") + 1;
  return (gbMultiPlayerMode ? "*.mlt" : "*.sng") + 1;
}

// sub_499850 -- oracle 0x499850 (6 B). The save tag, full width. IsALoadedGame
// (0x499870, 6 B) and SetIsNotLoadedGame (0x499880, 8 B) follow.
int __fastcall sub_499850()
{
  return gdwSaveTag;
}

// sub_499860 -- oracle 0x499860 (6 B). The save header's dword +4, full width.
int __fastcall sub_499860()
{
  return gdwSaveHdrDword4;
}

BYTE __fastcall IsALoadedGame()
{
  return gbIsLoadedGame;
}

void __fastcall SetIsNotLoadedGame()
{
  gbIsLoadedGame = 0;
}

// get_tech_available -- oracle 0x49A400 (31 B). An id below 0x18 reads the
// original table (0x18 per player), any other the BW one (0x14 per player).
BYTE __fastcall get_tech_available(int nPlayer, WORD wTechId)
{
  // Hyperion: a mod's, past 1.17's rows (playertables.h)
  if (wTechId >= TECH_TYPE_MOD_FIRST)
    return PlayerModTech(nPlayer, wTechId, PLAYER_MOD_AVAILABLE);
  if (nPlayer >= PLAYER_SLOTS_117 && g_hxPlayers)  // Hyperion: Hyperion's slots
    return PlayerTech(nPlayer)->abTechAvailable[wTechId];
  if (wTechId < 0x18)
    return gbTechAvailSC[nPlayer * 0x18 + wTechId];
  return gbTechAvailBW[nPlayer * 0x14 + wTechId];
}

// set_tech_available -- oracle 0x49A2C0 (54 B). get_tech_available's store twin.
void __fastcall set_tech_available(int nPlayer, WORD wTechId, BYTE bValue)
{
  // Hyperion: a mod's, past 1.17's rows (playertables.h)
  if (wTechId >= TECH_TYPE_MOD_FIRST) {
    PlayerModTech(nPlayer, wTechId, PLAYER_MOD_AVAILABLE) = bValue;
    return;
  }
  if (nPlayer >= PLAYER_SLOTS_117 && g_hxPlayers) {  // Hyperion: Hyperion's slots
    PlayerTech(nPlayer)->abTechAvailable[wTechId] = bValue;
    return;
  }
  if (wTechId < 0x18)
    gbTechAvailSC[nPlayer * 0x18 + wTechId] = bValue;
  else
    gbTechAvailBW[nPlayer * 0x14 + wTechId] = bValue;
}

// set_tech_researched -- oracle 0x49A300 (63 B). A BW id is stored only when
// the expansion is loaded.
void __fastcall set_tech_researched(int nPlayer, WORD wTechId, BYTE bValue)
{
  // Hyperion: a mod's, past 1.17's rows (playertables.h)
  if (wTechId >= TECH_TYPE_MOD_FIRST) {
    PlayerModTech(nPlayer, wTechId, PLAYER_MOD_RESEARCHED) = bValue;
    return;
  }
  if (nPlayer >= PLAYER_SLOTS_117 && g_hxPlayers) {  // Hyperion: Hyperion's slots
    if (wTechId < 0x18 || gbIsExpansion)
      PlayerTech(nPlayer)->abTechResearched[wTechId] = bValue;
    return;
  }
  if (wTechId < 0x18)
    gbTechResearchedSC[nPlayer * 0x18 + wTechId] = bValue;
  else if (gbIsExpansion)
    gbTechResearchedBW[nPlayer * 0x14 + wTechId] = bValue;
}

// set_upgrade_level -- oracle 0x49A370 (67 B). The same over 0x2E and 0xF rows.
void __fastcall set_upgrade_level(int nPlayer, WORD wUpgradeId, BYTE bLevel)
{
  // Hyperion: a mod's, past 1.17's rows (playertables.h)
  if (wUpgradeId >= UPGRADE_TYPE_MOD_FIRST) {
    PlayerModUpgrade(nPlayer, wUpgradeId, PLAYER_MOD_LEVEL) = bLevel;
    return;
  }
  if (nPlayer >= PLAYER_SLOTS_117 && g_hxPlayers) {  // Hyperion: Hyperion's slots
    if (wUpgradeId < 0x2E || gbIsExpansion)
      PlayerTech(nPlayer)->abUpgradeLevel[wUpgradeId] = bLevel;
    return;
  }
  if (wUpgradeId < 0x2E)
    gbUpgradeLevelSC[nPlayer * 0x2E + wUpgradeId] = bLevel;
  else if (gbIsExpansion)
    gbUpgradeLevelBW[nPlayer * 0xF + wUpgradeId] = bLevel;
}

// set_max_upgrade_level -- oracle 0x49A3C0 (58 B). No expansion test on the BW arm.
void __fastcall set_max_upgrade_level(int nPlayer, WORD wUpgradeId, BYTE bLevel)
{
  // Hyperion: a mod's, past 1.17's rows (playertables.h)
  if (wUpgradeId >= UPGRADE_TYPE_MOD_FIRST) {
    PlayerModUpgrade(nPlayer, wUpgradeId, PLAYER_MOD_MAX_LEVEL) = bLevel;
    return;
  }
  if (nPlayer >= PLAYER_SLOTS_117 && g_hxPlayers) {  // Hyperion: Hyperion's slots
    PlayerTech(nPlayer)->abMaxUpgradeLevel[wUpgradeId] = bLevel;
    return;
  }
  if (wUpgradeId < 0x2E)
    gbMaxUpgradeLevelSC[nPlayer * 0x2E + wUpgradeId] = bLevel;
  else
    gbMaxUpgradeLevelBW[nPlayer * 0xF + wUpgradeId] = bLevel;
}

// get_tech_researched -- oracle 0x49A420 (31 B).
BYTE __fastcall get_tech_researched(int nPlayer, WORD wTechId)
{
  // Hyperion: a mod's, past 1.17's rows (playertables.h)
  if (wTechId >= TECH_TYPE_MOD_FIRST)
    return PlayerModTech(nPlayer, wTechId, PLAYER_MOD_RESEARCHED);
  if (nPlayer >= PLAYER_SLOTS_117 && g_hxPlayers)  // Hyperion: Hyperion's slots
    return PlayerTech(nPlayer)->abTechResearched[wTechId];
  if (wTechId < 0x18)
    return gbTechResearchedSC[nPlayer * 0x18 + wTechId];
  return gbTechResearchedBW[nPlayer * 0x14 + wTechId];
}

// sub_49A440 -- oracle 0x49A440 (34 B). Turn the saved unit ids in every
// selection slot back into units, with a pointer and a down-counter as 1.17 has.
void __fastcall sub_49A440()
{
  CUnit **ppSlot = gSelected;
  int nLeft = SELECTION_HUMANS * SELECTION_MAX;
  do {
    *ppSlot = CUnit::FromIdAlive((DWORD)reinterpret_cast<DWORD_PTR>(*ppSlot));
    ppSlot++;
  } while (--nLeft);
}

// get_upgrade_level -- oracle 0x49A470 (38 B).
BYTE __fastcall get_upgrade_level(int nPlayer, WORD wUpgradeId)
{
  // Hyperion: a mod's, past 1.17's rows (playertables.h)
  if (wUpgradeId >= UPGRADE_TYPE_MOD_FIRST)
    return PlayerModUpgrade(nPlayer, wUpgradeId, PLAYER_MOD_LEVEL);
  if (nPlayer >= PLAYER_SLOTS_117 && g_hxPlayers)  // Hyperion: Hyperion's slots
    return PlayerTech(nPlayer)->abUpgradeLevel[wUpgradeId];
  if (wUpgradeId < 0x2E)
    return gbUpgradeLevelSC[nPlayer * 0x2E + wUpgradeId];
  return gbUpgradeLevelBW[nPlayer * 0xF + wUpgradeId];
}

// get_max_upgrade_level -- oracle 0x49A4A0 (38 B).
BYTE __fastcall get_max_upgrade_level(int nPlayer, WORD wUpgradeId)
{
  // Hyperion: a mod's, past 1.17's rows (playertables.h)
  if (wUpgradeId >= UPGRADE_TYPE_MOD_FIRST)
    return PlayerModUpgrade(nPlayer, wUpgradeId, PLAYER_MOD_MAX_LEVEL);
  if (nPlayer >= PLAYER_SLOTS_117 && g_hxPlayers)  // Hyperion: Hyperion's slots
    return PlayerTech(nPlayer)->abMaxUpgradeLevel[wUpgradeId];
  if (wUpgradeId < 0x2E)
    return gbMaxUpgradeLevelSC[nPlayer * 0x2E + wUpgradeId];
  return gbMaxUpgradeLevelBW[nPlayer * 0xF + wUpgradeId];
}

// gpMapStrs @0x666440 and gdwMapStrSize @0x666444, the map string table and its
// size; gfpMIMap @0x666458 and gfpCellMap @0x54323C, the two 0x20000-byte map
// tables, spelled as tile2.cpp spells them.
#ifdef OC_DLL_MODE
  #ifndef gpMapStrs
  #define gpMapStrs (*reinterpret_cast<void **>(OC_VA(0x666440u)))
  #endif
  #ifndef gdwMapStrSize
  #define gdwMapStrSize (*reinterpret_cast<DWORD *>(OC_VA(0x666444u)))
  #endif
  #ifndef gfpMIMap
  #define gfpMIMap (*reinterpret_cast<WORD **>(OC_VA(0x666458u)))
  #endif
  #ifndef gfpCellMap
  #define gfpCellMap (*reinterpret_cast<WORD **>(OC_VA(0x54323Cu)))
  #endif
#else
extern void  *gpMapStrs;
extern DWORD  gdwMapStrSize;
extern WORD  *gfpMIMap;
extern WORD  *gfpCellMap;
#endif

// saveload_read_save_data -- oracle 0x499890 (945 B). Read the body of an open
// save, one subsystem at a time, then close it and restore the view. Any
// failure frees the map strings, closes the file and answers 0.
BOOL saveload_read_save_data()
{
  int nSize;
  int nExpected;
  int nPathSize;
  BYTE *pPaths = NULL;

  // Hyperion: each part behind its section in a Hyperion save.
  if (!PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('I', 'M', 'G', 'S'))
      || !CImage::LoadImages(gpLoadGameFile, sgSaveVersion_g))
    goto fail;
  if (!PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('S', 'P', 'R', 'T'))
      || !CSprite::LoadSprites(gpLoadGameFile))
    goto fail;
  if (!PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('T', 'H', 'N', 'G'))
      || !CThingy::LoadThingies(gpLoadGameFile))
    goto fail;
  if (!PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('F', 'L', 'N', 'G'))
      || !CFlingy::LoadFlingies(gpLoadGameFile))
    goto fail;
  if (!PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('U', 'N', 'I', 'T'))
      || !read_save_units(gpLoadGameFile, sgSaveVersion_g))
    goto fail;
  if (!PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('B', 'U', 'L', 'T'))
      || !CBullet::LoadBullets(gpLoadGameFile, sgSaveVersion_g))
    goto fail;
  if (!PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('O', 'R', 'D', 'R'))
      || !COrder::LoadOrders(gpLoadGameFile))
    goto fail;
  if (!PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('C', 'R', 'E', 'P'))
      || !pfread(&nSize, 4, gpLoadGameFile))
    goto fail;
  {
    WORD *pCreep = getCreepMap(&nExpected);
    if (nSize != nExpected || !decompress(pCreep, nSize, gpLoadGameFile))
      goto fail;
  }
  if (!pfread(&nSize, 4, gpLoadGameFile))
    goto fail;
  {
    BYTE *pEdge = getEdgeMap(&nExpected);
    if (nSize != nExpected || !decompress(pEdge, nSize, gpLoadGameFile))
      goto fail;
  }
  if (!PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('D', 'E', 'C', 'Y'))
      || !tile2_read_from_save(gpLoadGameFile))
    goto fail;
  // Hyperion: 1.17's 0x10000 tiles, or the map's past that
  if (!PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('M', 'A', 'P', 'S'))
      || !decompress(gfpMIMap, SaveMapTiles() * 2, gpLoadGameFile))
    goto fail;
  if (!decompress(gfpCellMap, SaveMapTiles() * 2, gpLoadGameFile))
    goto fail;
  if (!decompress(gpSquareMap, SaveMapTiles() * 4, gpLoadGameFile))
    goto fail;
  if (!PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('T', 'R', 'I', 'G'))
      || !LoadTriggers(gpLoadGameFile))
    goto fail;
  if (!PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('S', 'T', 'R', 'S'))
      || !pfread(&gdwMapStrSize, 4, gpLoadGameFile) || !gdwMapStrSize)
    goto fail;
  // Hyperion: the size's top bit marks a STRx table, and the
  // table gets a NUL after it, as maphdr_STRx gives it; a 1.17 save sets neither.
  PortMapStringsWide((gdwMapStrSize & 0x80000000u) != 0);
  gdwMapStrSize &= 0x7FFFFFFFu;
  if (gpMapStrs)
    SMemFree(gpMapStrs, "saveload.cpp", 0x78E, 0);
  gpMapStrs = SMemAlloc(gdwMapStrSize + 1, "saveload.cpp", 0x78F, 0);
  if (!gpMapStrs)
    goto fail;
  static_cast<BYTE *>(gpMapStrs)[gdwMapStrSize] = 0;
  if (!decompress(gpMapStrs, gdwMapStrSize, gpLoadGameFile))
    goto fail;
  if (!PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('S', 'E', 'L', 'S'))
      || !PortSaveDecompressIds(gSelected, SELECTION_HUMANS * SELECTION_MAX, gpLoadGameFile))  // Hyperion: 0x180 bytes
    goto fail;
  sub_49A440();
  if (!PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('P', 'A', 'T', 'H'))
      || !pfread(&nPathSize, 4, gpLoadGameFile))
    goto fail;
  pPaths = static_cast<BYTE *>(SMemAlloc(nPathSize, "saveload.cpp", 0x7AA, 0));
  if (!decompress(pPaths, nPathSize, gpLoadGameFile) || !read_save_paths(pPaths, nPathSize))
    goto fail;
  SMemFree(pPaths, "saveload.cpp", 0x7AE, 0);
  pPaths = NULL;
  if (!PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('A', 'I', ' ', ' '))
      || !read_save_ai_data(reinterpret_cast<intptr_t>(gpLoadGameFile), sgSaveVersion_g))
    goto fail;
  if (!PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('D', 'A', 'T', 'S'))
      || !read_save_mapdata(gpLoadGameFile, sgSaveVersion_g))
    goto fail;
  if (!PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('V', 'I', 'E', 'W'))
      || !pfread(&glScrollPixelX, 4, gpLoadGameFile))
    goto fail;
  if (!pfread(&glScrollPixelY, 4, gpLoadGameFile) || !HxLoadState(gpLoadGameFile)  // Hyperion
      || !PortLoadEnd(gpLoadGameFile))
    goto fail;
  close_savefile();
  if (!sub_4313C0())
    goto fail;
  sub_4BBB40();
  cell_set_pos(glScrollPixelX, glScrollPixelY);
  gbIsLoadedGame = 1;
  return TRUE;

fail:
  if (pPaths)
    SMemFree(pPaths, "saveload.cpp", 0x7D4, 0);
  if (gpMapStrs) {
    SMemFree(gpMapStrs, "saveload.cpp", 0x7D5, 0);
    gpMapStrs = NULL;
  }
  if (gpLoadGameFile) {
    pfclose(gpLoadGameFile);
    gpLoadGameFile = NULL;
  }
  gbIsLoadedGame = 0;
  return FALSE;
}

// sub_499C50 -- oracle 0x499C50 (558 B). Read the open save's body: skip to the
// 0x1A sentinel, read the header, the player table, the resource block and the
// tag, then upgrade the tables an older save version lacks. Any failure closes
// the file and answers 0.
BOOL __fastcall sub_499C50()
{
  char szPath[0x104];
  SAVEGAME_HEADER hdr;
  BYTE bExpansion = gbIsExpansion;
  void *pFile = gpLoadGameFile;
  BYTE abPlayers[0x1B0];

  for (;;) {
    int nChar = pfgetc(pFile);
    if (nChar == -1)
      goto fail;
    if (nChar == 0x1A)
      break;
  }
  if (!ReadSaveHeader(&hdr, gpLoadGameFile))
    goto fail;
  reinterpret_cast<WORD &>(sgSaveVersion_g) = hdr.wVersion;
  DWORD dwSaveId;  // Hyperion: set apart from its declaration, which a goto jumps past
  dwSaveId = hdr.game.dwSaveId;
  gGameHeader = hdr.game;
  memset(gPlayerMinerals.a, 0, 0x17700);
  // Hyperion: a Hyperion save's marker and sections, or a 1.17 save as it is
  // (port_formats.cpp).
  if (!PortLoadBegin(gpLoadGameFile) || !PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('P', 'L', 'Y', 'R')))
    goto fail;
  if (!decompress((!gbMultiPlayerMode || IsGameCreator(NULL, 0)) ? static_cast<void *>(gPlayerData.a) : abPlayers,
                  0x1B0, gpLoadGameFile))
    goto fail;
  if (!PortLoadSection(gpLoadGameFile, PORT_SAVE_ID('G', 'A', 'M', 'E')))
    goto fail;
  if (!decompress(gPlayerMinerals.a, 0x17700, gpLoadGameFile))
    goto fail;
  if (!pfread(&gdwSaveTag, 4, gpLoadGameFile))
    goto fail;
  // Hyperion: a Hyperion game's set-up, before the game starts; a 1.17 save's turns
  // the player tables' extra entries off
  if (!HxLoadSetUp(gpLoadGameFile))
    goto fail;
  // The entry byte is compared whether or not the game is an expansion one.
  if (gbIsExpansion && !IsBroodwar())
    goto fail;
  if (gbIsExpansion != bExpansion)
    goto fail;
  if (hdr.wVersion < 2) {
    dword_534F20[0] = dword_5349DC;
    memcpy(gMapRegions, gOldMapRegions, 0x500);
  }
  if (hdr.wVersion < 3) {
    memcpy(gTechInProgress.a, gOldTechInProgress, 0x24);
    memcpy(gUpgradeInProgress.a, gOldUpgradeInProgress, 0x48);
  }
  if (word_526524 == 0) {
    SStrCopy(szPath, gszSavePath, 0x104);
    FindMapFile(szPath, gszSavePath, 0x104);
  }
  dword_6A5E3C = dwSaveId;
  return 1;

fail:
  close_savefile();
  return 0;
}

// MakeSaveDir -- oracle 0x499E80 (236 B). The save path builder: 0 for a name
// of 0x19 chars or more, or a buffer without room for the user path + 0x24.
//   bFlag       -> userpath + "maps\\replays\\"
//   multiplayer -> userpath + "maps\\" + "save\\"
//   else        -> userpath + "save\\" + name + "\\"
BOOL __fastcall MakeSaveDir(const char *pszName, char *pszBuf, int nBufLen, BOOL bFlag)
{
  char szUserPath[0x104];

  if (strlen(pszName) >= 0x19)
    return 0;
  SFileGetUserDataPath(szUserPath, 0x104, 1);
  if (strlen(szUserPath) + 0x24 >= (unsigned int)nBufLen)
    return 0;
  SStrCopy(pszBuf, szUserPath, nBufLen);
  if (bFlag) {
    SStrPack(pszBuf, "maps\\replays\\", nBufLen);
  } else if (gbMultiPlayerMode) {
    SStrPack(pszBuf, "maps\\", nBufLen);
    SStrPack(pszBuf, "save\\", nBufLen);
  } else {
    SStrPack(pszBuf, "save\\", nBufLen);
    SStrPack(pszBuf, pszName, nBufLen);
    SStrPack(pszBuf, "\\", nBufLen);
  }
  return 1;
}

// MakeSavePath -- oracle 0x499F70 (127 B). Build the local player's save or
// replay path for pszName, creating the directory. The replay suffix is "rep"
// with no dot, as the oracle has it.
BOOL __fastcall MakeSavePath(const char *pszName, char *pszBuf, int nBufLen, BOOL bReplay)
{
  if (!MakeSaveDir(gLocalPlayerName, pszBuf, nBufLen, bReplay))
    return 0;
  if (strlen(pszBuf) + 0x22 >= 0x104)
    return 0;
  if (!CreateDirectoriesRecursive(pszBuf))
    return 0;
  SStrPack(pszBuf, pszName, nBufLen);
  SStrPack(pszBuf, bReplay ? "rep" : GetSaveExtension(), nBufLen);
  return 1;
}

// dword_526520 @0x526520, the elapsed game time stamped into a save.
#ifdef OC_DLL_MODE
  #ifndef dword_526520
  #define dword_526520 (*reinterpret_cast<DWORD *>(OC_VA(0x526520u)))
  #endif
#else
extern DWORD dword_526520;
#endif

// saveload_write_save_data -- oracle 0x498D20 (1156 B). Write a save game:
// resolve the path, show the wait dialog under the local player's ids, then
// write the banner, the header and every subsystem. Every write runs even after
// one fails; a failure deletes the file and reports it.
BOOL __fastcall saveload_write_save_data(DWORD dwTimestamp, const char *pszName, int nFlags)
{
  char szSaveDir[0x104];
  char szPath[0x104];
  char szMsg[0x104];
  void *pFile;
  int nSize;

  if (!nFlags && IsLadderGame())
    return FALSE;
  nSize = MakeSavePath(pszName, szPath, 0x104, nFlags);
  // Hyperion: a replay making its game's save again writes it to a scratch file
  if (const char *pszTarget = HxSaveTarget()) {
    SStrCopy(szPath, pszTarget, 0x104);
    nSize = 1;
  }
  if (is_bad_filename(szPath, 0x104, 1))
    return FALSE;
  SStrCopy(szMsg, error_get_str(0x6D), 0x104);
  SStrPack(szMsg, " \"", 0x104);
  SStrPack(szMsg, pszName, 0x104);
  SStrPack(szMsg, "\"", 0x104);
  DWORD dwNation = g_ActiveNationID;
  DWORD dwHuman = g_ActiveHumanID;
  g_ActiveNationID = g_LocalNationID;
  g_ActiveHumanID = g_LocalHumanID;
  if (!HxSaveTarget())  // Hyperion: no dialog for a replay's save
    do_wait_dlg(szMsg);
  g_ActiveNationID = dwNation;
  g_ActiveHumanID = dwHuman;
  BOOL bOk = nSize;
  if (!bOk) {
    pFile = NULL;
  } else {
    pFile = pfopen(szPath, 0x17);
    if (!pFile)
      bOk = FALSE;
  }
  bOk = write_save_header(pFile, szPath) ? bOk : FALSE;
  bOk = write_player_data(dwTimestamp, pszName, pFile) ? bOk : FALSE;
  // Hyperion: Hyperion's container, each part in a section of its own
  // (port_formats.cpp).
  bOk = PortSaveBegin(pFile) ? bOk : FALSE;
  dword_526520 = net_elapsed_gametime();
  SStrCopy(szSaveDir, gszSavePath, 0x104);
  if (!word_526524)
    RemoveBasePath(gszSavePath, 0x104);
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('P', 'L', 'Y', 'R')) ? bOk : FALSE;
  bOk = compress(gPlayerData.a, 0x1B0, pFile) ? bOk : FALSE;
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('G', 'A', 'M', 'E')) ? bOk : FALSE;
  bOk = compress(gPlayerMinerals.a, 0x17700, pFile) ? bOk : FALSE;
  bOk = pfwrite(&g_LocalHumanID, 4, pFile) ? bOk : FALSE;
  if (!word_526524)
    SStrCopy(gszSavePath, szSaveDir, 0x104);
  if (HxSaveNeeded())
    ReplayHxSaved();  // Hyperion: what follows changes the game, so its replay notes it
  DestroyPsiLinks();
  CSprite::RemoveSelectsForSave();
  // Hyperion: a Hyperion game's set-up, after GAME, with the free lists as they are now
  // that the power fields are down
  bOk = HxSaveSetUp(pFile) ? bOk : FALSE;
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('I', 'M', 'G', 'S')) ? bOk : FALSE;
  bOk = CImage::SaveImages(pFile) ? bOk : FALSE;
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('S', 'P', 'R', 'T')) ? bOk : FALSE;
  bOk = CSprite::SaveSprites(pFile) ? bOk : FALSE;
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('T', 'H', 'N', 'G')) ? bOk : FALSE;
  bOk = CThingy::SaveThingies(pFile) ? bOk : FALSE;
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('F', 'L', 'N', 'G')) ? bOk : FALSE;
  bOk = CFlingy::SaveFlingies(pFile) ? bOk : FALSE;
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('U', 'N', 'I', 'T')) ? bOk : FALSE;
  bOk = CUnit::WriteSave(pFile) ? bOk : FALSE;
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('B', 'U', 'L', 'T')) ? bOk : FALSE;
  bOk = CBullet::SaveBullets(pFile) ? bOk : FALSE;
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('O', 'R', 'D', 'R')) ? bOk : FALSE;
  bOk = COrder::SaveOrders(pFile) ? bOk : FALSE;
  CSprite::RestoreSelectsFromSave();
  RestorePsiLinks();
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('C', 'R', 'E', 'P')) ? bOk : FALSE;
  WORD *pCreep = getCreepMap(&nSize);
  bOk = pfwrite(&nSize, 4, pFile) ? bOk : FALSE;
  bOk = compress(pCreep, nSize, pFile) ? bOk : FALSE;
  BYTE *pEdge = getEdgeMap(&nSize);
  bOk = pfwrite(&nSize, 4, pFile) ? bOk : FALSE;
  bOk = compress(pEdge, nSize, pFile) ? bOk : FALSE;
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('D', 'E', 'C', 'Y')) ? bOk : FALSE;
  bOk = sub_4B4B80(pFile) ? bOk : FALSE;
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('M', 'A', 'P', 'S')) ? bOk : FALSE;
  bOk = compress(gfpMIMap, SaveMapTiles() * 2, pFile) ? bOk : FALSE;  // Hyperion
  bOk = compress(gfpCellMap, SaveMapTiles() * 2, pFile) ? bOk : FALSE;
  bOk = compress(gpSquareMap, SaveMapTiles() * 4, pFile) ? bOk : FALSE;
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('T', 'R', 'I', 'G')) ? bOk : FALSE;
  bOk = SaveTriggers(pFile) ? bOk : FALSE;
  // Hyperion: a STRx table is marked in the size's top bit, which a 1.17
  // save never sets (port_map.cpp). The saved size is dwStrSize.
  DWORD dwStrSize = gdwMapStrSize | (PortMapStringsAreWide() ? 0x80000000u : 0);
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('S', 'T', 'R', 'S')) ? bOk : FALSE;
  bOk = pfwrite(&dwStrSize, 4, pFile) ? bOk : FALSE;
  bOk = compress(gpMapStrs, gdwMapStrSize, pFile) ? bOk : FALSE;
  CUnit **ppSel = gSelected;
  for (int n = SELECTION_HUMANS * SELECTION_MAX; n; n--, ppSel++)
    *ppSel = reinterpret_cast<CUnit *>((intptr_t)(*ppSel)->GetUnitID());
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('S', 'E', 'L', 'S')) ? bOk : FALSE;
  bOk = PortSaveCompressIds(gSelected, SELECTION_HUMANS * SELECTION_MAX, pFile) ? bOk : FALSE;  // Hyperion: 0x180 bytes
  ppSel = gSelected;
  for (int n = SELECTION_HUMANS * SELECTION_MAX; n; n--, ppSel++)
    *ppSel = CUnit::FromIdAlive((DWORD)reinterpret_cast<DWORD_PTR>(*ppSel));
  nSize = sai_get_contour_bytes();
  void *pPaths = SMemAlloc(nSize, "saveload.cpp", 0x674, 0);
  bOk = sai_PathingGlobWriteSave(pPaths, nSize) ? bOk : FALSE;
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('P', 'A', 'T', 'H')) ? bOk : FALSE;
  bOk = pfwrite(&nSize, 4, pFile) ? bOk : FALSE;
  bOk = compress(pPaths, nSize, pFile) ? bOk : FALSE;
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('A', 'I', ' ', ' ')) ? bOk : FALSE;
  bOk = sai_write_save_data(reinterpret_cast<intptr_t>(pFile)) ? bOk : FALSE;
  SMemFree(pPaths, "saveload.cpp", 0x67B, 0);
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('D', 'A', 'T', 'S')) ? bOk : FALSE;
  bOk = write_save_mapdata(pFile) ? bOk : FALSE;
  bOk = PortSaveSection(pFile, PORT_SAVE_ID('V', 'I', 'E', 'W')) ? bOk : FALSE;
  bOk = pfwrite(&glScrollPixelX, 4, pFile) ? bOk : FALSE;
  bOk = pfwrite(&glScrollPixelY, 4, pFile) ? bOk : FALSE;
  bOk = HxSaveState(pFile) ? bOk : FALSE;  // Hyperion: a Hyperion game's state past 1.17's
  if (nFlags) {
    bOk = PortSaveSection(pFile, PORT_SAVE_ID('R', 'C', 'M', 'D')) ? bOk : FALSE;
    bOk = net_record_write(reinterpret_cast<intptr_t>(pFile));
  }
  bOk = PortSaveEnd(pFile) ? bOk : FALSE;
  if (pFile && pfclose(pFile))
    bOk = FALSE;
  sub_47B090();
  if (!bOk) {
    pfdelete(szPath, TRUE);
    err_popup(0x65, 0, 0, 0, 0);
  }
  return bOk;
}

// sub_4991B0 -- oracle 0x4991B0 (159 B). Rebuild the saved-game list, hand
// every node's header to the callback, then drop the list again. The glob is
// picked inline rather than through GetSaveExtension, as 1.17 has it.
void __fastcall sub_4991B0(void (__stdcall *pfnItem)(void *))
{
  char szDir[0x104];
  int nFound;

  sgSavedGameList.Clear();
  if (MakeSaveDir(gLocalPlayerName, szDir, 0x104, 0)) {
    const char *pszExt;
    if (gbIsExpansion) {
      pszExt = gbMultiPlayerMode ? "*.mlx" : "*.snx";
    } else {
      pszExt = gbMultiPlayerMode ? "*.mlt" : "*.sng";
    }
    SearchDirectory(szDir, pszExt, sub_499250, 0, 0, 0, &nFound);
    // Each successor is clamped to NULL, as 1.17's loop does.
    TSAVED_GAME_NODE *p = sgSavedGameList.link.next;
    if ((intptr_t)p <= 0) {
      p = NULL;
    }
    while (p != NULL) {
      pfnItem(p->abHeader);
      p = p->link.next;
      if ((intptr_t)p <= 0) {
        p = NULL;
      }
    }
    sgSavedGameList.Clear();
  }
}

// sub_499250 -- oracle 0x499250 (502 B). SearchDirectory callback: read this
// file's save header into a new list node, then move the node so the list stays
// sorted newest first. Each TSList step is spelled out as TSList inlines it.
BOOL __fastcall sub_499250(char *pszPath, struct _WIN32_FIND_DATAA *pFind, intptr_t nUnused0, intptr_t nUnused1)
{
  char szPath[0x104];
  BYTE abHeader[0xB5];

  if (strlen(pFind->cFileName) >= 0x20)
    return 1;
  SStrCopy(szPath, pszPath, 0x104);
  SStrPack(szPath, pFind->cFileName, 0x104);
  if (sub_499750(szPath, abHeader))
    return 1;

  void *pv = SMemAlloc(sizeof(TSAVED_GAME_NODE), gTypeInfo_TSAVED_GAME_NODE.raw_name(), -2, 8);
  TSAVED_GAME_NODE *pNew;
  TSLink<TSAVED_GAME_NODE> *nl;
  if (pv != NULL) {
    pNew = reinterpret_cast<TSAVED_GAME_NODE *>(pv);
    pNew->link.prev = NULL;
    pNew->link.next = NULL;
    nl = &pNew->link;
  } else {
    pNew = NULL;
    nl = &sgSavedGameList.link;
  }
  TSAVED_GAME_NODE *pPrev = nl->prev;
  if (pPrev != NULL) {
    TSAVED_GAME_NODE *pSlot = nl->next;
    if (reinterpret_cast<intptr_t>(pSlot) <= 0) {
      pSlot = reinterpret_cast<TSAVED_GAME_NODE *>(~reinterpret_cast<intptr_t>(pSlot));
    } else {
      pSlot = reinterpret_cast<TSAVED_GAME_NODE *>(
          reinterpret_cast<char *>(pSlot) +
          (reinterpret_cast<char *>(nl) -
           reinterpret_cast<char *>(pPrev->link.next)));
    }
    *reinterpret_cast<TSAVED_GAME_NODE **>(pSlot) = pPrev;
    nl->prev->link.next = nl->next;
    nl->prev = NULL;
    nl->next = NULL;
  }
  nl->prev = reinterpret_cast<TSAVED_GAME_NODE *>(&sgSavedGameList.link);
  nl->next = sgSavedGameList.link.next;
  TSAVED_GAME_NODE *pHead = sgSavedGameList.link.next;
  int nLinkOff = static_cast<int>(sgSavedGameList._unmapped_00);
  if (reinterpret_cast<intptr_t>(pHead) <= 0) {
    pHead = reinterpret_cast<TSAVED_GAME_NODE *>(~reinterpret_cast<intptr_t>(pHead));
  } else {
    if (nLinkOff < 0) {
      nLinkOff = static_cast<int>(
          reinterpret_cast<char *>(&sgSavedGameList.link) -
          reinterpret_cast<char *>(sgSavedGameList.link.prev->link.next));
    }
    pHead = reinterpret_cast<TSAVED_GAME_NODE *>(
        reinterpret_cast<char *>(pHead) + nLinkOff);
  }
  *reinterpret_cast<TSAVED_GAME_NODE **>(pHead) = reinterpret_cast<TSAVED_GAME_NODE *>(nl);
  sgSavedGameList.link.next = pNew;

  memcpy(pNew->abHeader, abHeader, 0xB5);
  *reinterpret_cast<FILETIME *>(pNew->adwLastWriteTime) = pFind->ftLastWriteTime;

  TSLink<TSAVED_GAME_NODE> *l = &pNew->link;
  pPrev = l->prev;
  if (pPrev != NULL) {
    TSAVED_GAME_NODE *pSlot = l->next;
    if (reinterpret_cast<intptr_t>(pSlot) <= 0) {
      pSlot = reinterpret_cast<TSAVED_GAME_NODE *>(~reinterpret_cast<intptr_t>(pSlot));
    } else {
      pSlot = reinterpret_cast<TSAVED_GAME_NODE *>(
          reinterpret_cast<char *>(pSlot) +
          (reinterpret_cast<char *>(l) -
           reinterpret_cast<char *>(pPrev->link.next)));
    }
    *reinterpret_cast<TSAVED_GAME_NODE **>(pSlot) = pPrev;
    l->prev->link.next = l->next;
    l->prev = NULL;
    l->next = NULL;
  }

  TSAVED_GAME_NODE *p = sgSavedGameList.link.next;
  if (reinterpret_cast<intptr_t>(p) <= 0)
    p = NULL;
  while (p) {
    if (CompareFileTime(reinterpret_cast<FILETIME *>(pNew->adwLastWriteTime),
                        reinterpret_cast<FILETIME *>(p->adwLastWriteTime)) != 1)
      break;
    p = p->link.next;
    if (reinterpret_cast<intptr_t>(p) <= 0)
      p = NULL;
  }

  pPrev = pNew->link.prev;
  if (pPrev != NULL) {
    TSAVED_GAME_NODE *pSlot = pNew->link.next;
    if (reinterpret_cast<intptr_t>(pSlot) <= 0) {
      pSlot = reinterpret_cast<TSAVED_GAME_NODE *>(~reinterpret_cast<intptr_t>(pSlot));
    } else {
      pSlot = reinterpret_cast<TSAVED_GAME_NODE *>(
          reinterpret_cast<char *>(pSlot) +
          (reinterpret_cast<char *>(pNew) -
           reinterpret_cast<char *>(pPrev->link.next)));
    }
    *reinterpret_cast<TSAVED_GAME_NODE **>(pSlot) = pPrev;
    pNew->link.prev->link.next = pNew->link.next;
    pNew->link.prev = NULL;
    pNew->link.next = NULL;
  }
  TSLink<TSAVED_GAME_NODE> *pAt = p ? &p->link : &sgSavedGameList.link;
  TSAVED_GAME_NODE *pBefore = pAt->prev;
  pNew->link.prev = pBefore;
  pNew->link.next = pBefore->link.next;
  pBefore->link.next = pNew;
  pAt->prev = pNew;
  return 1;
}

// sub_499450 -- oracle 0x499450 (87 B). Search the directory in nArg1 for the
// file named nArg0 and leave its full path in nArg1. Two returns rather than a
// ternary, for load_game, which inlines it.
intptr_t __fastcall sub_499450(intptr_t nArg0, intptr_t nArg1, int nArg2, intptr_t nArg3, intptr_t nArg4)
{
  SEARCHFILE_CTX ctx;
  int nFound;

  ctx.pszName = reinterpret_cast<const char *>(nArg0);
  ctx.pfnFilter = reinterpret_cast<BOOL (__stdcall *)(const char *, void *)>(nArg3);
  ctx.pFilterArg = reinterpret_cast<void *>(nArg4);
  ctx.pszOut = reinterpret_cast<char *>(nArg1);
  ctx.nOutLen = nArg2;
  nFound = 0;
  SearchDirectory(reinterpret_cast<char *>(nArg1), "*.*", sub_4994B0, 0, 1,
                  reinterpret_cast<intptr_t>(&ctx), &nFound);
  if (!nFound)
    return 0;
  return reinterpret_cast<intptr_t>(ctx.pszOut);
}

// sub_4994B0 -- oracle 0x4994B0 (238 B). sub_499450's callback: on a name match
// that fits the output buffer and passes the filter, copy the full path out,
// set the found flag and return 0 to stop the walk.
BOOL __fastcall sub_4994B0(char *pszPath, struct _WIN32_FIND_DATAA *pFind, intptr_t nCtx, intptr_t nFound)
{
  char szPath[0x104];
  SEARCHFILE_CTX *pCtx = reinterpret_cast<SEARCHFILE_CTX *>(nCtx);
  char *pszSep;

  if (_stricmp(pFind->cFileName, pCtx->pszName))
    return 1;
  if (strlen(pszPath) + 1 + strlen(pFind->cFileName) >= pCtx->nOutLen)
    return 1;
  SStrCopy(szPath, pszPath, 0x104);
  pszSep = SStrChrR(szPath, '\\');
  if (!pszSep || pszSep[1])
    SStrPack(szPath, "\\", 0x104);
  SStrPack(szPath, pFind->cFileName, 0x104);
  if (pCtx->pfnFilter && !pCtx->pfnFilter(szPath, pCtx->pFilterArg))
    return 1;
  SStrCopy(pCtx->pszOut, szPath, pCtx->nOutLen);
  *reinterpret_cast<int *>(nFound) = 1;
  return 0;
}

// sub_4995E0 -- oracle 0x4995E0 (167 B). Find the save whose header id is
// dwSaveId and copy its display name out.
BOOL __fastcall sub_4995E0(DWORD dwSaveId, char *pszOut, int nOutLen)
{
  char szDir[0x104];
  BYTE abHeader[0xB5];
  int nFound;
  const char *pszExt;

  if (!sub_499FF0(gLocalPlayerName, szDir, 0x104))
    return 0;
  *reinterpret_cast<DWORD *>(abHeader + 0x28) = dwSaveId;
  nFound = 0;
  if (gbIsExpansion)
    pszExt = gbMultiPlayerMode ? "*.mlx" : "*.snx";
  else
    pszExt = gbMultiPlayerMode ? "*.mlt" : "*.sng";
  SearchDirectory(szDir, pszExt, sub_499690, 0, 1,
                  reinterpret_cast<intptr_t>(abHeader), &nFound);
  if (!nFound)
    return 0;
  SStrCopy(pszOut, reinterpret_cast<char *>(abHeader) + 0x99, nOutLen);
  return 1;
}

// sub_499690 -- oracle 0x499690 (191 B). sub_4995E0's callback: read this
// file's save header and, if its id (+0x28) matches the caller's, copy the
// whole header back, set the found flag and stop.
BOOL __fastcall sub_499690(char *pszPath, struct _WIN32_FIND_DATAA *pFind, intptr_t nHdr, intptr_t nFound)
{
  char szPath[0x104];
  BYTE abHeader[0xB5];
  char *pszSep;

  SStrCopy(szPath, pszPath, 0x104);
  pszSep = SStrChrR(szPath, '\\');
  if (!pszSep || pszSep[1])
    SStrPack(szPath, "\\", 0x104);
  SStrPack(szPath, pFind->cFileName, 0x104);
  if (sub_499750(szPath, abHeader))
    return 1;
  if (*reinterpret_cast<DWORD *>(abHeader + 0x28) !=
      *reinterpret_cast<DWORD *>(nHdr + 0x28))
    return 1;
  memcpy(reinterpret_cast<void *>(nHdr), abHeader, 0xB5);
  *reinterpret_cast<int *>(nFound) = 1;
  return 0;
}

// sub_499750 -- oracle 0x499750 (249 B). Open a save, skip its text preamble to
// the 0x1A sentinel, read the header through ReadSaveHeader, then write the
// display name, the file name without its extension, at +0x99. Returns 0 when
// read, 2 for an old format (the leading WORD is not 0x69), 1 for anything
// else. The shared `return 1` comes last and the flag is set through a goto, as
// 1.17 has them.
BYTE __fastcall sub_499750(char *pszPath, void *pHdr)
{
  char szName[0x20];
  void *pFile;
  int nOk;
  int nChar;
  char *psz;
  char *pszFound;

  pFile = pfopen(pszPath, 1);
  if (pFile != NULL) {
    for (;;) {
      nChar = pfgetc(pFile);
      if (nChar == -1) {
        break;
      }
      if (nChar == 0x1A) {
        if (ReadSaveHeader(pHdr, pFile)) {
          nOk = 1;
          goto closefile;
        }
        break;
      }
    }
    nOk = 0;
  closefile:
    if (!pfclose(pFile)) {
      if (nOk) {
        if (*reinterpret_cast<WORD *>(pHdr) != 0x69) {
          return 2;
        }
        pszFound = strrchr(pszPath, '\\');
        if (pszFound == NULL) {
          pszFound = pszPath;
        } else {
          pszFound = pszFound + 1;
        }
        SStrCopy(szName, pszFound, 0x20);
        psz = szName;
        pszFound = SStrChrR(psz, '\\');
        if (pszFound != NULL) {
          psz = pszFound;
        }
        pszFound = SStrChrR(psz, '.');
        if (pszFound != NULL) {
          *pszFound = 0;
        }
        if (strlen(szName) < 0x1C) {
          SStrCopy(reinterpret_cast<char *>(pHdr) + 0x99, szName, 0x1C);
          return 0;
        }
      }
    }
  }
  return 1;
}

// sub_499FF0 -- oracle 0x499FF0 (193 B). MakeSaveDir without the bFlag arm,
// hence `ret 4h`.
//   multiplayer -> userpath + "maps\\"
//   else        -> userpath + "save\\" + name + "\\"
// Its multiplayer arm packs only "maps\\", where MakeSaveDir's packs "maps\\"
// and "save\\", kept as 1.17 has it.
BOOL __fastcall sub_499FF0(const char *pszName, char *pszBuf, int nBufLen)
{
  char szUserPath[0x104];

  if (strlen(pszName) >= 0x19)
    return 0;
  SFileGetUserDataPath(szUserPath, 0x104, 1);
  if (strlen(szUserPath) + 0x24 >= (unsigned int)nBufLen)
    return 0;
  SStrCopy(pszBuf, szUserPath, nBufLen);
  if (gbMultiPlayerMode) {
    SStrPack(pszBuf, "maps\\", nBufLen);
  } else {
    SStrPack(pszBuf, "save\\", nBufLen);
    SStrPack(pszBuf, pszName, nBufLen);
    SStrPack(pszBuf, "\\", nBufLen);
  }
  return 1;
}

// ReadSaveHeader -- oracle 0x49A0C0 (101 B). Read the 4-byte tag, check 0x0069
// in its high word, decompress the 0xB5-byte save header and publish its dword
// +4. A NULL pHdr decodes into a local scratch buffer.
BOOL __fastcall ReadSaveHeader(void *pHdr, void *pFile)
{
  BYTE abScratch[0xB5];
  DWORD dwTag;

  if (!pHdr)
    pHdr = abScratch;
  if (!pfread(&dwTag, 4, pFile))
    return 0;
  if ((dwTag & 0xFFFF0000) != 0x690000)
    return 0;
  if (!decompress(pHdr, 0xB5, pFile))
    return 0;
  gdwSaveHdrDword4 = *((DWORD *)pHdr + 1);
  return 1;
}

// 0x49A130 RemoveBasePath (253 B). Make pszPath relative: drop a leading user-data
// path, else a leading install path, else everything up to the last
// backslash. Hyperion: a path in a folder given with --maps becomes its
// name under "maps" before that (port_paths.cpp), so the saved game finds its
// map there again. The `const char *` strrchr keeps the inline C++ overload out of
// the object.
void __fastcall RemoveBasePath(char *pszPath, unsigned int nLen)
{
  char szBase[0x104];
  size_t nCmp;
  const char *pszSep;

  SFileGetUserDataPath(szBase, 0x104, 1);
  nCmp = strlen(szBase);
  if (nLen < nCmp)
    nCmp = nLen;
  if (!_strnicmp(pszPath, szBase, nCmp)) {
    SStrCopy(pszPath, pszPath + strlen(szBase), nLen);
    return;
  }
  SFileGetInstallPath(szBase, 0x104, 1);
  nCmp = strlen(szBase);
  if (nLen < nCmp)
    nCmp = nLen;
  if (!_strnicmp(pszPath, szBase, nCmp)) {
    SStrCopy(pszPath, pszPath + strlen(szBase), nLen);
    return;
  }
  if (PortMapRelativePath(pszPath, (int)nLen))
    return;
  // Hyperion: a map outside those folders, as --map plays one, keeps its full path,
  // so that a Use Map Settings game saved on it finds the map again (FindMapFile);
  // 1.17 kept its name alone, which it then looked for in the game's folder.
  if ((pszPath[0] != 0 && pszPath[1] == ':') || (pszPath[0] == '\\' && pszPath[1] == '\\'))
    return;
  pszSep = strrchr(static_cast<const char *>(pszPath), '\\');
  if (pszSep)
    SStrCopy(pszPath, pszSep + 1, nLen);
}

// sub_49A230 -- oracle 0x49A230 (11 B).
void __fastcall sub_49A230()
{
  dword_6A5E3C = 0;
}

// save_select -- oracle 0x49A340 (39 B). Copy the selection into the saved
// selection block.
void __fastcall save_select(CUnit **ppSelection)
{
  memcpy(SavedSelected(), ppSelection, SelectionMax() * sizeof(CUnit *));  // Hyperion: 48 in 1.17
}

// RestoreSelection -- oracle 0x49A240 (115 B). Restore the saved selection:
// keep the units up to the first empty slot that are alive and visible to the
// local player, commit them through sub_4BCE70, then status_set_update.
void __fastcall RestoreSelection()
{
  CUnit *apKeep[SELECTION_WIDE];  // Hyperion: 12 in 1.17, 255 in Hyperion's selection (stage S)
  CUnit **ppSlot;
  int nLeft;
  int nCount;

  nCount = 0;
  ppSlot = SavedSelected();
  memset(apKeep, 0, sizeof(apKeep));  // Hyperion: 0x30 in 1.17
  nLeft = SelectionMax();
  do {
    CUnit *pUnit = *ppSlot;
    nLeft--;
    if (pUnit == NULL)
      break;
    if (!pUnit->IsDead()) {
      if (pUnit->pSprite->bVisibilityFlags & gbLocalMask) {
        apKeep[nCount] = pUnit;
        nCount++;
      }
    }
    ppSlot++;
  } while (nLeft != 0);
  // Hyperion: a Hyperion game shows the selection without sending it, its players'
  // selections being the save's already (sub_443900); 1.17's command seated the units in
  // the reverse order, so a game loaded went on otherwise than the game saved (stage D)
  if (nCount)
    sub_4BCE70(nCount, apKeep, 0, HxRules() ? 0 : 1);
  status_set_update();
}

// gszVersionNumber @0x508620, the version string the save banner prints,
// spelled as gluMain.cpp spells it.
#ifdef OC_DLL_MODE
  #ifndef gszVersionNumber
  #define gszVersionNumber (*reinterpret_cast<char (*)[0x104]>(OC_VA(0x508620u)))
  #endif
#else
extern char gszVersionNumber[0x104];
#endif

// write_save_header -- oracle 0x49A4D0 (1032 B). Write the plain-text banner at
// the top of a save file: format, file name, human players, game type, map
// title and path, then a Ctrl-Z. Fails if any write fails, or if the human
// count is wrong for the game mode.
BOOL __fastcall write_save_header(void *pFile, const char *pszFilename)
{
  char szLine[0x100];

  SStrCopy(szLine, "****************************************************\r\n", 0x100);
  if (!pfwrite(szLine, (int)strlen(szLine), pFile))
    return FALSE;
  SStrCopy(szLine, "** ", 0x100);
  SStrPack(szLine, "Starcraft", 0x100);
  SStrPack(szLine, " ", 0x100);
  SStrPack(szLine, gszVersionNumber, 0x100);
  SStrPack(szLine, "\r\n", 0x100);
  if (!pfwrite(szLine, (int)strlen(szLine), pFile))
    return FALSE;
  if (gbMultiPlayerMode)
    psnprintf(szLine, 0x100, "** Multi Player Save Format ver %d.%d \r\n", 0x69, 3);
  else
    psnprintf(szLine, 0x100, "** Single Player Save Format ver %d.%d \r\n", 0x69, 3);
  if (!pfwrite(szLine, (int)strlen(szLine), pFile))
    return FALSE;
  SStrCopy(szLine, "****************************************************\r\n", 0x100);
  if (!pfwrite(szLine, (int)strlen(szLine), pFile))
    return FALSE;
  SStrCopy(szLine, "\r\n  Filename:  ", 0x100);
  SStrPack(szLine, pszFilename, 0x100);
  SStrPack(szLine, "\r\n", 0x100);
  if (!pfwrite(szLine, (int)strlen(szLine), pFile))
    return FALSE;
  int nHumans = 0;
  for (int i = 0; i < 8; i++) {
    if (gPlayerData[i].nType != 2)
      continue;
    if (!nHumans++)
      SStrCopy(szLine, "   Players:  ", 0x100);
    else
      SStrCopy(szLine, "             ", 0x100);
    SStrPack(szLine, gPlayerData[i].szName, 0x100);
    SStrPack(szLine, "\r\n", 0x100);
    if (!pfwrite(szLine, (int)strlen(szLine), pFile))
      return FALSE;
  }
  if (gbMultiPlayerMode) {
    if (nHumans < 1)
      return FALSE;
  } else if (nHumans != 1) {
    return FALSE;
  }
  const char *pszType = sub_447A70(reinterpret_cast<const TGAMEPARAMS *>(&gGameHeader.bGameType));
  if (!pszType)
    pszType = "Custom";
  SStrCopy(szLine, " Game Type:  ", 0x100);
  SStrPack(szLine, pszType, 0x100);
  SStrPack(szLine, "\r\n", 0x100);
  if (!pfwrite(szLine, (int)strlen(szLine), pFile))
    return FALSE;
  // +0x4D is the map title, inside GameHeader's unmapped run.
  SStrCopy(szLine, " SCM Title:  ", 0x100);
  SStrPack(szLine, reinterpret_cast<const char *>(&gGameHeader._unmapped_34[0x4D - 0x34]), 0x100);
  SStrPack(szLine, "\r\n", 0x100);
  if (!pfwrite(szLine, (int)strlen(szLine), pFile))
    return FALSE;
  SStrCopy(szLine, "  SCM Path:  ", 0x100);
  SStrPack(szLine, static_cast<const char *>(GetCurrentMap(1)), 0x100);
  SStrPack(szLine, "\r\n\r\n", 0x100);
  if (!pfwrite(szLine, (int)strlen(szLine), pFile))
    return FALSE;
  szLine[0] = 0x1A;
  return pfwrite(szLine, 1, pFile) != 0;
}

// write_player_data -- oracle 0x49A8E0 (306 B). Stamp the game header with the
// save id and player counts, then write the 4-byte tag and the compressed
// 0xB5-byte save header. The counter is stored every pass, as 1.17 does.
BOOL __fastcall write_player_data(DWORD dwSaveId, const char *pszName, void *pFile)
{
  SAVEGAME_HEADER hdr;
  DWORD dwTag;

  memset(&hdr, 0, sizeof(hdr));
  hdr.dwTag = 0x30069;
  gGameHeader.dwSaveReserved = 0;
  gGameHeader.dwSaveId = dwSaveId;
  gGameHeader.bActivePlayers = get_num_active_players();
  gGameHeader.bSlotCount = get_num_active_players();
  gGameHeader.bType1Players = 0;
  for (int i = 0; i < 8; i++) {
    if (gPlayerData[i].nType == 1)
      gGameHeader.bType1Players++;
  }
  hdr.game = gGameHeader;
  hdr.dwSeed = net_randsequence();
  dwTag = 0x690003;
  if (!pfwrite(&dwTag, 4, pFile))
    return 0;
  return compress(&hdr, 0xB5, pFile) != 0;
}

// SearchDirectory -- oracle 0x49AA20 (606 B). Call pfnItem for every non-hidden
// match of pszExt in pszDir (directories only when bDirs), then, when bRecurse,
// recurse into every subdirectory. A zero from the callback stops the whole
// walk and returns 0. The callback gets (dir, find data, nArg, pnOut).
int __fastcall SearchDirectory(char *pszDir, const char *pszExt, SEARCHDIR_CB pfnItem,
                               int bDirs, int bRecurse, intptr_t nArg, int *pnOut)
{
  char szPath[0x104];
  WIN32_FIND_DATAA fd;
  char szSub[0x104];
  HANDLE hFind;
  char *pszSep;

  SStrCopy(szPath, pszDir, 0x104);
  pszSep = SStrChrR(szPath, '\\');
  if (!pszSep || pszSep[1])
    SStrPack(szPath, "\\", 0x104);
  SStrPack(szPath, pszExt, 0x104);
  hFind = FindFirstFileA(szPath, &fd);
  if (hFind != INVALID_HANDLE_VALUE) {
    do {
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
        continue;
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (fd.cFileName[0] == '.' &&
            (!fd.cFileName[1] || (fd.cFileName[1] == '.' && !fd.cFileName[2])))
          continue;
        if (!bDirs)
          continue;
      }
      if (!pfnItem(pszDir, &fd, nArg, reinterpret_cast<intptr_t>(pnOut)))
        goto fail;
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
  }
  if (!bRecurse)
    return 1;
  SStrCopy(szPath, pszDir, 0x104);
  pszSep = SStrChrR(szPath, '\\');
  if (!pszSep || pszSep[1])
    SStrPack(szPath, "\\", 0x104);
  SStrPack(szPath, "*.*", 0x104);
  hFind = FindFirstFileA(szPath, &fd);
  if (hFind == INVALID_HANDLE_VALUE)
    return 1;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
      continue;
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
      continue;
    if (fd.cFileName[0] == '.' &&
        (!fd.cFileName[1] || (fd.cFileName[1] == '.' && !fd.cFileName[2])))
      continue;
    SStrCopy(szSub, pszDir, 0x104);
    pszSep = SStrChrR(szSub, '\\');
    if (!pszSep || pszSep[1])
      SStrPack(szSub, "\\", 0x104);
    SStrPack(szSub, fd.cFileName, 0x104);
    if (!SearchDirectory(szSub, pszExt, pfnItem, bDirs, bRecurse, nArg, pnOut))
      goto fail;
  } while (FindNextFileA(hFind, &fd));
  FindClose(hFind);
  return 1;
fail:
  FindClose(hFind);
  return 0;
}
