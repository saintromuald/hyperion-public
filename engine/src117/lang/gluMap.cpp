// gluMap.cpp -- the map-selection list for the create-game screens: it walks
// the maps directory, keeps the recent-maps list, and drives the map list box.
// Bodies are in oracle VA order.
//
// TU membership cuts both ways and needs re-checking after a change here: a callee
// defined in this file can be spliced into a caller in this file by /Ob2, which a
// cross-TU call never is, and a body whose size is exactly caller plus callee needs its
// callee here. __declspec(noinline) on the definition is the lever.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>
#include <typeinfo>
#include <new>

#include "../h/gluMap.h"
#include "../h/gluBNCreate.h"   // gametype_find_template, IsSimpleTemplate
#include "../h/saveload.h"      // GetSaveExtension
#include "../h/net_cmds.h"      // get_valid_str_len
#include "../h/net_slot.h"      // clear_selectable_races, CountValidSlots
#include "../h/net_cmds.h"      // voice_squelch
#include "../h/net_glue.h"      // ResetGameCreator, CreateGame_SinglePlayer
#include "../h/game.h"          // PreloadMap
#include "../h/map_load.h"      // PreloadCampaignMap
#include "../h/uiSingle.h"      // seat_local_player
#include "../h/TrigAct.h"       // get_map_string
#include "../h/netdesc.h"       // gamedesc_t
#include "../h/net_error.h"     // err_popup
#include "../h/gamedata.h"      // GameData2Txt
#include "../h/glues.h"         // IsSpawnedCopy
#include "../h/grid.h"          // sub_4634E0
#include "../h/glues.h"         // glues_get_str
#include "../h/gds_local.h"     // psnprintf
#include "../h/replay.h"        // replay_get_max_players
#include "../h/gluCreat.h"      // sub_455710
#include "../h/Storm.h"
#include "../gen/globals117.h"
#include "../port/platform/port_paths.h"  // Hyperion
#include "../port/platform/port_winmsg.h"  // Hyperion: SetTimer and KillTimer, the window's
#if !defined(_MSC_VER)
#include "../h/rtti.h"  // Hyperion: the global type_info MSVC declares itself
#endif

// SFileGetUserDataPath -- thunk 0x4D006A = `jmp dword ptr [0x4EB580]`, ordinal
// 296 out of the same descriptor. Three arguments, from the call sites: the
// push runs end `push 1h / push 104h / push eax`, and no caller cleans up.
// Declared here and not in Storm.h for the reason gds_log.cpp:17-22 records,
// and spelled character for character as saveload.cpp, replay.cpp, charactr.cpp
// and gds_log.cpp already spell it.
extern "C" {
// Hyperion: as port_stormfile.cpp defines it
DWORD  __stdcall SFileGetUserDataPath(char *pszBuf, DWORD cchBuf, DWORD dwFlags);
}  // extern "C"

// Storm ordinal 509, spelled as replay.cpp, uiSingle.cpp and gluBNCreate.cpp
// spell it; the .def supplies it at C linkage as _SStrCmpI@12.
extern "C" {
int   __stdcall SStrCmpI(const char *pszA, const char *pszB, size_t cchMax);
}

// _mbsrchr spelled as gluBNCreate.cpp and gds_log.cpp spell it, not through
// <mbstring.h>, whose _mbsicmp collides with save_dlg.h (C2733).
extern "C" {
unsigned char *__cdecl _mbsrchr(const unsigned char *pszStr, unsigned int ch);
}

// KERNEL32.dll!GetFileAttributesA, the import the oracle reaches through
// [0x4EB20C]. Spelled exactly as src117/lang/replay.cpp already spells it, so
// the two TU-local declarations of the name cannot disagree.
extern "C" __declspec(dllimport) DWORD __stdcall GetFileAttributesA(const char *pszName);

// The directory walk uses KERNEL32's FindFirstFileA / FindNextFileA /
// FindClose, which the oracle reaches through [0x4EB248], [0x4EB244] and
// [0x4EB250]; the Windows headers Storm.h pulls in already declare all three.
// WIN32_FIND_DATAA's shape is what puts cFileName at the oracle's [fd+2Ch].
static_assert(offsetof(WIN32_FIND_DATAA, cFileName) == 0x2C, "sub_45C9A0 lea ecx, [ebp-120h] off [ebp-14Ch]");
static_assert(sizeof(WIN32_FIND_DATAA) == 0x140, "the frame span between [ebp-14Ch] and [ebp-0Ch]");

// The multiplayer-mode byte that gates the saved-game arm of the extension
// test. Registered in re/globals117.json but not yet emitted by gen, so it is
// spelled here under a guard.
#ifndef gbMultiPlayerMode
#ifdef OC_DLL_MODE
#define gbMultiPlayerMode (*reinterpret_cast<BYTE *>(OC_VA(0x672664u)))
#else
extern BYTE gbMultiPlayerMode;
#endif
#endif

// ---------------------------------------------------------------------------
// Globals this TU reaches, dual-spelled. One #ifdef per name, never one over
// the block: gen emits only marked rows' globals, so the set grows mid-wave and
// a single guard would undeclare every name under it.
//
// Before coining a name, look up re/globals117.json by VA key and check
// sc-refs/sheets for an existing one. Minting a name is the master's call.

// The map list head. The name is the sheet's
// (sc-refs/sheets/data_naming_1.17.csv) and the registry's; the 12-byte extent
// is the next named global, gModemDeviceList_g @0x50820C. Static in the proof
// arm: every oracle function that reaches it is in this TU and no data word
// points at it.
#ifdef OC_DLL_MODE
#define gMapList_g (*reinterpret_cast<MAPLIST *>(OC_VA(0x508200u)))
#else
extern MAPLIST gMapList_g;
#endif

// The display name the directory walk gives the parent-directory entry. A
// char array: AddDirectory hands its address to SStrCopy and sub_45B3F0 to
// _stricmp. The 0x104 extent is the gap to the next referenced address,
// 0x66AEF4; nothing here strides it, so only the base is proven.
#ifdef OC_DLL_MODE
#define byte_66ADF0 (reinterpret_cast<char *>(OC_VA(0x66ADF0u)))
#else
extern char byte_66ADF0[0x104];
#endif

// The byte ConfigureGame requires nonzero before a competitive game may start.
// Unregistered; the name is the landed maphdr.cpp's for the same address.
#ifndef gbExpansion_g
#ifdef OC_DLL_MODE
#define gbExpansion_g (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE gbExpansion_g;
#endif
#endif

// The map tileset. Registered in re/globals117.json but not yet emitted by gen,
// so it is spelled here under the same guard maphdr.cpp uses.
#ifndef g_MapTileset
#ifdef OC_DLL_MODE
#define g_MapTileset (*reinterpret_cast<WORD *>(OC_VA(0x5264BCu)))
#else
extern WORD g_MapTileset;
#endif
#endif

// The game options block. ConfigureGame writes the chosen speed to the DWORD at
// its base, which is the only part of it any body here touches.
#ifndef OC_OWN_gOptions
#define OC_OWN_gOptions
#ifdef OC_DLL_MODE
#define gOptions (*reinterpret_cast<DWORD *>(OC_VA(0x6D3130u)))
#else
extern DWORD gOptions;
#endif
#endif

// gGameHeader @0x6728D0. Declared in the TU and deliberately not registered:
// gametype.h states why (a struct-typed registry row makes gen emit the struct
// into src117/gen/globals117.h). uiMulti.cpp and gametype.cpp spell it the same
// way; this is a third TU reaching the object, not a third object.
#ifndef OC_OWN_gGameHeader
#define OC_OWN_gGameHeader
#ifdef OC_DLL_MODE
#define gGameHeader (*reinterpret_cast<GameHeader *>(OC_VA(0x6728D0u)))
#else
extern GameHeader gGameHeader;
#endif
#endif

// The MSVC RTTI type descriptor for LISTENTRY at 0x510864: the type_info
// vftable 0x4F1EDC, a zero dword, then the raw name ".?AULISTENTRY@@". AddMap
// hands raw_name()'s result to SMemAlloc as the owner tag.
#ifndef OC_GTYPEINFO_LISTENTRY
#define OC_GTYPEINFO_LISTENTRY
#ifdef OC_DLL_MODE
#define gTypeInfo_LISTENTRY (*reinterpret_cast<type_info *>(OC_VA(0x510864u)))
#else
extern type_info gTypeInfo_LISTENTRY;
#endif
#endif

// The list entry ReadEntryInfo's timer finishes loading, and the directory the
// map browser shows. Names from sc-refs/sheets/data_naming_1.17.csv; neither is
// registered. The directory's 0x104 extent is sub_45C680's SStrCopy count.
#ifndef dword_66AEF4
#ifdef OC_DLL_MODE
#define dword_66AEF4 (*reinterpret_cast<LISTENTRY **>(OC_VA(0x66AEF4u)))
#else
extern LISTENTRY *dword_66AEF4;
#endif
#endif

// 0x66FD28 ghWnd, registered in main.cpp; spelled as music.cpp spells it.
#ifndef OC_GH_WND
#define OC_GH_WND
#ifdef OC_DLL_MODE
#define ghWnd (*reinterpret_cast<HWND *>(OC_VA(0x66FD28u)))
#else
extern HWND ghWnd;
#endif
#endif

#ifdef OC_DLL_MODE
#define byte_66AEF8 (reinterpret_cast<char *>(OC_VA(0x66AEF8u)))
#else
extern char byte_66AEF8[0x104] OC_UNALIGNED;
#endif

// ---------------------------------------------------------------------------
// SETCTRL -- the 12-byte record CreateSetControl allocates per map-setting
// control and stores in one of the ten slots below. Its `push 0Ch` into
// SMemAlloc gives the size, and its three stores give the fields.
struct SETCTRL {
  /*0x00*/ void(__stdcall *pfnSet)(int nIndex, DWORD_PTR dwData, const char *pszText);
  /*0x04*/ void(__stdcall *pfnEnable)(int nIndex, DWORD_PTR dwData, int bEnable);
  /*0x08*/ DWORD_PTR dwData;
};

static_assert(offsetof(SETCTRL, pfnSet) == 0x00, "sub_45B5E0 mov eax, [eax]");
OC_X86_LAYOUT(offsetof(SETCTRL, pfnEnable) == 0x04, "UpdateMapCtrl mov eax, dword ptr [eax+4h] then call eax");
OC_X86_LAYOUT(offsetof(SETCTRL, dwData) == 0x08, "sub_45B5E0 push [eax+8]");
OC_X86_LAYOUT(sizeof(SETCTRL) == 0x0C, "CreateSetControl push 0Ch into SMemAlloc");

// The nine map-setting controls sub_45B5E0 resets. Positional names, the
// charter's, for a run of ten consecutive slots 0x66ADC8..0x66ADEC that
// CreateSetControl fills through a switch and ClearMapList frees in order.
#ifdef OC_DLL_MODE
#define dword_66ADC8 (*reinterpret_cast<SETCTRL **>(OC_VA(0x66ADC8u)))
#define dword_66ADCC (*reinterpret_cast<SETCTRL **>(OC_VA(0x66ADCCu)))
#define dword_66ADD0 (*reinterpret_cast<SETCTRL **>(OC_VA(0x66ADD0u)))
#define dword_66ADD4 (*reinterpret_cast<SETCTRL **>(OC_VA(0x66ADD4u)))
#define dword_66ADD8 (*reinterpret_cast<SETCTRL **>(OC_VA(0x66ADD8u)))
#define dword_66ADDC (*reinterpret_cast<SETCTRL **>(OC_VA(0x66ADDCu)))
#define dword_66ADE0 (*reinterpret_cast<SETCTRL **>(OC_VA(0x66ADE0u)))
#define dword_66ADE4 (*reinterpret_cast<SETCTRL **>(OC_VA(0x66ADE4u)))
#define dword_66ADE8 (*reinterpret_cast<SETCTRL **>(OC_VA(0x66ADE8u)))
#define dword_66ADEC (*reinterpret_cast<SETCTRL **>(OC_VA(0x66ADECu)))
#else
extern SETCTRL *dword_66ADC8;
extern SETCTRL *dword_66ADCC;
extern SETCTRL *dword_66ADD0;
extern SETCTRL *dword_66ADD4;
extern SETCTRL *dword_66ADD8;
extern SETCTRL *dword_66ADDC;
extern SETCTRL *dword_66ADE0;
extern SETCTRL *dword_66ADE4;
extern SETCTRL *dword_66ADE8;
extern SETCTRL *dword_66ADEC;
#endif

// ---------------------------------------------------------------------------
// gametype.h keeps GameHeader's first 0x27 bytes unnamed, so the gamedesc_t
// record embedded at +0x1C is reached through its offset, not a member.
#define HEADER_DATA(p)   (*reinterpret_cast<gamedesc_t *>(reinterpret_cast<BYTE *>(p) + 0x1C))

// ---------------------------------------------------------------------------
// The bodies, in oracle VA order.

// 0x45B5E0. Blanks every map-setting control that exists, then drops the
// selectable-race cache. The index each control is told is its own, not its
// slot order: 0, 1, 3, 4, 6, 7, 5, 2, 8.
void sub_45B5E0(void)
{
  if (dword_66ADC8 != NULL)
    dword_66ADC8->pfnSet(0, dword_66ADC8->dwData, "");
  if (dword_66ADCC != NULL)
    dword_66ADCC->pfnSet(1, dword_66ADCC->dwData, "");
  if (dword_66ADD0 != NULL)
    dword_66ADD0->pfnSet(3, dword_66ADD0->dwData, "");
  if (dword_66ADD4 != NULL)
    dword_66ADD4->pfnSet(4, dword_66ADD4->dwData, "");
  if (dword_66ADD8 != NULL)
    dword_66ADD8->pfnSet(6, dword_66ADD8->dwData, "");
  if (dword_66ADE0 != NULL)
    dword_66ADE0->pfnSet(7, dword_66ADE0->dwData, "");
  if (dword_66ADE4 != NULL)
    dword_66ADE4->pfnSet(5, dword_66ADE4->dwData, "");
  if (dword_66ADE8 != NULL)
    dword_66ADE8->pfnSet(2, dword_66ADE8->dwData, "");
  if (dword_66ADEC != NULL)
    dword_66ADEC->pfnSet(8, dword_66ADEC->dwData, "");

  clear_selectable_races();
}

// 0x45B700. Checks the lobby settings against the game template the header's
// key selects, fixes the speed and copies the template's parameter record into
// the header. The 0x800000NN codes are the oracle's, one per rejected reason.
int ConfigureGame(GameHeader *pHdr, BYTE bSpeed)
{
  const TGAMEPARAMS *pParams = gametype_find_template(HEADER_DATA(pHdr).bGameType,
                                                      HEADER_DATA(pHdr).bGameSubType,
                                                      HEADER_DATA(pHdr).wField0E);

  if (pParams == NULL)
    return 0x80000008;

  if (IsWGTGame() && HEADER_DATA(pHdr).bApproval != 6)
    return 0x80000012;

  if (pParams->bFlag12 == 1) {
    HEADER_DATA(pHdr).bGameSpeed = 6;
    if (HEADER_DATA(pHdr).bApproval != 2)
      return 0x80000003;
  } else if (IsCompetitiveGame(pParams->bCategory, pParams->bSub)) {
    HEADER_DATA(pHdr).bGameSpeed = 6;
    if (gbExpansion_g == 0)
      return 0x80000011;
    if (HEADER_DATA(pHdr).bApproval != 5)
      return 0x8000000E;
  } else {
    HEADER_DATA(pHdr).bGameSpeed = bSpeed;
  }

  if (HEADER_DATA(pHdr).wTemplate == 2 &&
      !IsSimpleTemplate(HEADER_DATA(pHdr).bGameType, HEADER_DATA(pHdr).bGameSubType,
                        HEADER_DATA(pHdr).wField0E))
    return 0x80000002;

  if (!sub_4480A0(&HEADER_DATA(pHdr)))
    return 0x80000008;

  gOptions = bSpeed;
  *reinterpret_cast<TGAMEPARAMS *>(&pHdr->bGameType) = *pParams;
  return 0;
}

// 0x45B850. Adds the one file pszDir names to the map list and to the list box
// through pfnAdd, returning the list-box index. An entry already on the list
// returns its stored index; anything that is not a map, save or replay fails.
int sub_45B850(char *pszDir, void *pfnAdd)
{
  char szBuf[0x104];
  LISTENTRY *pEntry;
  char *pszName;
  char *pszExt;
  BYTE bKind;

  if (pszDir == NULL || pfnAdd == NULL)
    return 0x80000006;

  pEntry = sub_45CB50(pszDir);
  if (pEntry != NULL)
    return pEntry->dwListIndex;

  SStrCopy(szBuf, pszDir, 0x104);
  pszName = (char *)_mbsrchr((const unsigned char *)szBuf, '\\');
  if (pszName != NULL)
    *pszName++ = '\0';

  pszExt = strrchr(pszName, '.');
  if (pszExt++ == NULL)
    return 0x80000006;

  if (_stricmp(pszExt, GetSaveExtension() + 1) == 0)
    bKind = 1;
  else if (_stricmp(pszExt, "scx") == 0)
    bKind = 4;
  else if (_stricmp(pszExt, "scm") == 0)
    bKind = 4;
  else if (_stricmp(pszExt, "hxm") == 0)  // Hyperion: Hyperion's maps (port_hxm.cpp)
    bKind = 4;
  else if (_stricmp(pszExt, "rep") == 0)
    bKind = 0x80;
  else
    return 0x80000006;

  // The success arm must enclose the call: the early-return spelling loads
  // the flag byte through EAX where the oracle uses ECX.
  pEntry = AddMap(bKind, szBuf, pszName, NULL);
  if (pEntry != NULL) {
    pEntry->dwListIndex = ((int(__stdcall *)(LISTENTRY *, char *, BYTE))pfnAdd)(
        pEntry, pEntry->szShort, pEntry->bFlags);
    return pEntry->dwListIndex;
  }
  return 0x80000006;
}

// Returns pEntry when it is on the map list, else NULL. Inlined at every use.
static LISTENTRY *map_find_entry(LISTENTRY *pEntry)
{
  LISTENTRY *p;

  for (p = gMapList_g.link.next; (intptr_t)p > 0;
       p = (p != NULL ? p : (LISTENTRY *)&gMapList_g.link)->link.next) {
    if (p == pEntry)
      return p;
  }
  return NULL;
}

// 0x45B970. Selects a list entry: one that still needs loading is loaded by a
// 100 ms timer, or at once when the timer cannot be set; a loaded one goes
// straight into the controls.
void sub_45B970(void *pMapEntry)
{
  LISTENTRY *pEntry;

  WinMsgKillTimer(ghWnd, 0xC);
  pEntry = map_find_entry((LISTENTRY *)pMapEntry);
  if (pEntry == NULL)
    return;

  if ((pEntry->bFlags & 0x85) == 0 || pEntry->dwLoaded != 0) {
    UpdateMapCtrl(pEntry);
    return;
  }

  sub_45B5E0();
  dword_66AEF4 = pEntry;
  if (WinMsgSetTimer(ghWnd, 0xC, 100, reinterpret_cast<WinMsgTimerProc>(ReadEntryInfo)) == 0)
    ReadEntryInfo(0, 0, 0, 0);
}

// The summary CreateMapInfo copies out of a list entry. The byte count of the
// trailing copy is its `rep movsd` of 23h dwords plus one movsb.
struct MAPINFO {
  /*0x00*/ DWORD dwLoaded;
  /*0x04*/ BYTE bFlags;
  /*0x05*/ BYTE _unmapped_05[3];
  /*0x08*/ DWORD dwComputers;
  /*0x0C*/ DWORD dwHumans;
  /*0x10*/ DWORD dwPlayers;
  /*0x14*/ DWORD bKind1;
  /*0x18*/ DWORD bKind2;
  /*0x1C*/ DWORD bKind5;
  /*0x20*/ DWORD bKind6;
  /*0x24*/ BYTE abHeader[0x8D];
};

static_assert(offsetof(MAPINFO, dwLoaded) == 0x00, "CreateMapInfo mov dword ptr [edx], ecx");
static_assert(offsetof(MAPINFO, bFlags) == 0x04, "CreateMapInfo mov byte ptr [edx+4h], cl");
static_assert(offsetof(MAPINFO, dwComputers) == 0x08, "CreateMapInfo mov dword ptr [edx+8h], ecx");
static_assert(offsetof(MAPINFO, dwHumans) == 0x0C, "CreateMapInfo mov dword ptr [edx+0Ch], ecx");
static_assert(offsetof(MAPINFO, dwPlayers) == 0x10, "CreateMapInfo mov dword ptr [edx+10h], ecx");
static_assert(offsetof(MAPINFO, bKind1) == 0x14, "CreateMapInfo mov dword ptr [edx+14h], ecx");
static_assert(offsetof(MAPINFO, bKind2) == 0x18, "CreateMapInfo mov dword ptr [edx+18h], ecx");
static_assert(offsetof(MAPINFO, bKind5) == 0x1C, "CreateMapInfo mov dword ptr [edx+1Ch], ecx");
static_assert(offsetof(MAPINFO, bKind6) == 0x20, "CreateMapInfo mov dword ptr [edx+20h], ecx");
static_assert(offsetof(MAPINFO, abHeader) == 0x24, "CreateMapInfo lea edi, dword ptr [edx+24h]");
static_assert(sizeof(MAPINFO) == 0xB4, "0x24 plus the 8Dh copied bytes, padded to 4");

// 0x45BEB0. Fills pInfo from a list entry and returns 1, or returns 0 when
// either pointer is missing or the entry is not on the list. The three counts
// read 0xFF until the entry has been loaded.
int CreateMapInfo(void *pMapEntry, void *pvInfo)
{
  MAPINFO *pInfo = (MAPINFO *)pvInfo;
  LISTENTRY *pEntry = map_find_entry((LISTENTRY *)pMapEntry);

  if (pInfo == NULL || pEntry == NULL)
    return 0;

  pInfo->dwLoaded = pEntry->dwLoaded;
  pInfo->bFlags = pEntry->bFlags & 0x8F;
  pInfo->dwComputers = pEntry->dwLoaded != 0 ? pEntry->bField47A : 0xFF;
  pInfo->dwHumans = pEntry->dwLoaded != 0 ? pEntry->bField47B : 0xFF;
  pInfo->dwPlayers = pEntry->dwLoaded != 0 ? pEntry->bField479 : 0xFF;
  pInfo->bKind1 = pEntry->dwField260 == 1;
  pInfo->bKind2 = pEntry->dwField260 == 2;
  pInfo->bKind5 = pEntry->dwField260 == 5;
  pInfo->bKind6 = pEntry->dwField260 == 6;
  memcpy(pInfo->abHeader, &pEntry->abHeader[0x0C], sizeof(pInfo->abHeader));
  return 1;
}

// The map list's first entry, or NULL when the list is empty. The final
// return reads the head through this; an inline ternary becomes a cmov.
static __forceinline LISTENTRY *map_first(MAPLIST *pList)
{
  return (intptr_t)pList->link.next > 0 ? pList->link.next : NULL;
}

// 0x45BA40. Empties the map list, refills it from each directory (or from the
// recent-maps list), hands every entry to pfnAdd and returns the list index of
// the entry to select: the filter match, else the first map, else 0 or 0xFF.
int ClearLBox(BYTE bKind, const char **ppszDirs, int nDirs, const char *pszDir,
              int bAtRoot, PFN_LBOXADD pfnAdd, const char *pszFilter)
{
  LISTENTRY *p;

  if (pfnAdd == NULL)
    return 0xFF;

  if (byte_66ADF0[0] == 0) {
    strcpy(byte_66ADF0, "[");
    strcat(byte_66ADF0, glues_get_str(0x1C));
    strcat(byte_66ADF0, "]");
  }

  reinterpret_cast<TSList<LISTENTRY,TSGetLink<LISTENTRY> > *>(&gMapList_g)->Clear();

  if (bKind & 0x10) {
    FindRecentMaps();
  } else {
    MAPHASH hash;

    for (unsigned i = 0; i < (unsigned)nDirs; i++)
      sub_45C9A0(bKind, ppszDirs[i], pszDir, bAtRoot, &hash);
    hash.InternalClear(0);
  }

  for (p = gMapList_g.link.next; (intptr_t)p > 0; p = p->link.next)
    p->dwListIndex = pfnAdd((intptr_t)p, (intptr_t)p->szShort, p->bFlags);

  if (pszFilter != NULL) {
    for (p = gMapList_g.link.next; (intptr_t)p > 0;
         p = (p != NULL ? p : (LISTENTRY *)&gMapList_g.link)->link.next) {
      if (_strnicmp(pszFilter, p->szShort, strlen(p->szShort)) == 0)
        return p->dwListIndex;
    }
  }
  for (p = gMapList_g.link.next; (intptr_t)p > 0;
       p = (p != NULL ? p : (LISTENTRY *)&gMapList_g.link)->link.next) {
    char *pszExt = strrchr(p->szPath, '.');

    if (pszExt++ != NULL) {
      if (_stricmp(pszExt, "scx") == 0 || _stricmp(pszExt, "scm") == 0 ||
          _stricmp(pszExt, "hxm") == 0)  // Hyperion: Hyperion's maps
        return p->dwListIndex;
    }
  }

  p = map_first(&gMapList_g);
  return p == NULL ? 0xFF : 0;
}

// 0x45BC70. Resolves pszDir under the user data directory and the install
// directory and fills the list box from both, or from the first alone when the
// two paths are the same. Hyperion: and from each folder given with
// --maps (port_paths.cpp), when pszDir is under "maps".
int FillMapList(BYTE bKind, const char *pszDir, int bAtRoot, PFN_LBOXADD pfnAdd,
               const char *pszFilter)
{
  char szUser[0x104];
  char szInstall[0x104];
  char aszMaps[PORT_MAP_FOLDERS][0x104];
  const char *apszDirs[2 + PORT_MAP_FOLDERS];
  int nDirs;

  SFileGetUserDataPath(szUser, 0x104, 1);
  SStrPack(szUser, pszDir, 0x104);
  SFileGetInstallPath(szInstall, 0x104, 1);
  SStrPack(szInstall, pszDir, 0x104);

  nDirs = 1;
  apszDirs[0] = szUser;
  if (SStrCmpI(szUser, szInstall, 0x7FFFFFFF) != 0) {
    nDirs = 2;
    apszDirs[1] = szInstall;
  }
  for (int i = 0; i < PortMapFolderCount(); ++i)
    if (PortMapFolderPath(i, pszDir, aszMaps[i], 0x104))
      apszDirs[nDirs++] = aszMaps[i];

  return ClearLBox(bKind, apszDirs, nDirs, pszDir, bAtRoot, pfnAdd, pszFilter);
}

// 0x45BD20. Stops the map-list timer, frees the ten set-control records in
// slot order and empties the map list.
void ClearMapList()
{
  WinMsgKillTimer(ghWnd, 0xC);
  if (dword_66ADC8 != NULL) {
    SMemFree(dword_66ADC8, "gluMap.cpp", 1248, 0);
    dword_66ADC8 = NULL;
  }
  if (dword_66ADCC != NULL) {
    SMemFree(dword_66ADCC, "gluMap.cpp", 1252, 0);
    dword_66ADCC = NULL;
  }
  if (dword_66ADD0 != NULL) {
    SMemFree(dword_66ADD0, "gluMap.cpp", 1256, 0);
    dword_66ADD0 = NULL;
  }
  if (dword_66ADD4 != NULL) {
    SMemFree(dword_66ADD4, "gluMap.cpp", 1260, 0);
    dword_66ADD4 = NULL;
  }
  if (dword_66ADD8 != NULL) {
    SMemFree(dword_66ADD8, "gluMap.cpp", 1264, 0);
    dword_66ADD8 = NULL;
  }
  if (dword_66ADDC != NULL) {
    SMemFree(dword_66ADDC, "gluMap.cpp", 1268, 0);
    dword_66ADDC = NULL;
  }
  if (dword_66ADE0 != NULL) {
    SMemFree(dword_66ADE0, "gluMap.cpp", 1272, 0);
    dword_66ADE0 = NULL;
  }
  if (dword_66ADE4 != NULL) {
    SMemFree(dword_66ADE4, "gluMap.cpp", 1276, 0);
    dword_66ADE4 = NULL;
  }
  if (dword_66ADE8 != NULL) {
    SMemFree(dword_66ADE8, "gluMap.cpp", 1280, 0);
    dword_66ADE8 = NULL;
  }
  if (dword_66ADEC != NULL) {
    SMemFree(dword_66ADEC, "gluMap.cpp", 1284, 0);
    dword_66ADEC = NULL;
  }
  // The list head is the TSList instantiation's own object.
  reinterpret_cast<TSList<LISTENTRY,TSGetLink<LISTENTRY> > *>(&gMapList_g)->Clear();
}

// The path probe the bodies below run inline: build pszName's path under the
// user data directory, then under the install directory, and report whether
// either names a plain file. Not FindMapFile, which the oracle keeps out of
// line for its cross-TU callers and spells its own way.
static int map_find_file(const char *pszName, char *pszBuffer, int cchBuffer)
{
  SFileGetUserDataPath(pszBuffer, cchBuffer, 1);
  SStrPack(pszBuffer, pszName, cchBuffer);
  if ((~(GetFileAttributesA(pszBuffer) >> 4) & 1) != 0)
    return 1;

  SFileGetInstallPath(pszBuffer, cchBuffer, 1);
  SStrPack(pszBuffer, pszName, cchBuffer);
  if ((~(GetFileAttributesA(pszBuffer) >> 4) & 1) != 0)
    return 1;

  // Hyperion: then the folders given with --maps (port_paths.cpp)
  return PortMapFindInFolders(pszName, pszBuffer, cchBuffer);
}

// Unlinks the entry from whatever list holds it; see src117/h/TSList.h.
inline LISTENTRY::~LISTENTRY()
{
  LISTENTRY *pPrev = link.prev;
  if (pPrev != NULL) {
    LISTENTRY *pNext = link.next;
    TSLink<LISTENTRY> *pSlot;
    if ((intptr_t)pNext <= 0) {
      pSlot = (TSLink<LISTENTRY> *)~(intptr_t)pNext;
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pNext > 0)
        nOff = (intptr_t)&link - (intptr_t)pPrev->link.next;
      pSlot = (TSLink<LISTENTRY> *)((intptr_t)pNext + nOff);
    }
    pSlot->prev = pPrev;
    link.prev->link.next = link.next;
    link.prev = NULL;
    link.next = NULL;
  }
}

inline void LISTENTRY::operator delete(void *p)
{
  SMemFree(p, "delete", -1, 0);
}

// ListLinkNode is TSList.h's; the oracle folded this instantiation into the
// session list's copy at 0x440C10.
__declspec(noinline) void TSList<LISTENTRY,TSGetLink<LISTENTRY> >::LinkNode(
    LISTENTRY *pNode, ULONG dwWhere, LISTENTRY *pBefore)
{
  ListLinkNode(this, pNode, dwWhere, pBefore);
}

// 0x45B080. Adds a list entry for one subdirectory the walk found: ".." as the
// parent entry unless the walk is at its root, anything else as "[name]" in
// sorted order. "." is skipped, and so is a name the hash already holds.
// The length goes into the dead bAtRoot so it is the compare's SIB index, and
// the two hash registrations store in different orders, as the oracle does.
void AddDirectory(const char *pszName, const char *pszDir, int bAtRoot,
                  MAPHASH *pHash)
{
  LISTENTRY *pEntry;

  if (_stricmp(pszName, ".") == 0)
    return;
  if (pHash != NULL && pHash->Ptr(pszName) != NULL)
    return;

  pEntry = (LISTENTRY *)SMemAlloc(sizeof(LISTENTRY),
                                  gTypeInfo_LISTENTRY.raw_name(), -2, 8);
  if (pEntry != NULL) {
    pEntry->link.prev = NULL;
    pEntry->link.next = NULL;
  } else {
    pEntry = NULL;
  }
  pEntry->bFlags = 8;
  SStrCopy(pEntry->szName, pszDir, 0x104);

  if (_stricmp(pszName, "..") == 0) {
    if (bAtRoot != 0) {
      delete pEntry;
      return;
    }
    char *pszSep = strrchr(pEntry->szName, '\\');
    if (pszSep != NULL)
      *pszSep = '\0';
    SStrCopy(pEntry->szShort, byte_66ADF0, 0x41);
    reinterpret_cast<TSList<LISTENTRY,TSGetLink<LISTENTRY> > *>(&gMapList_g)
        ->LinkNode(pEntry, 1, NULL);
    if (pHash != NULL) {
      DWORD dwHash = SStrHash("..", 0, 0);
      HASHENTRY *pNew = pHash->InternalNewNode(dwHash, 0, 0);
      pNew->pszKey = "..";
      pNew->pValue = pEntry;
      pNew->dwHash = dwHash;
    }
    return;
  }

  bAtRoot = (int)strlen(pEntry->szName);
  if (bAtRoot != 0 && pEntry->szName[bAtRoot - 1] != '\\')
    SStrPack(pEntry->szName, "\\", 0x104);
  SStrPack(pEntry->szName, pszName, 0x104);
  SStrCopy(pEntry->szShort, "[", 0x41);
  SStrPack(pEntry->szShort, pszName, 0x41);
  SStrPack(pEntry->szShort, "]", 0x41);
  sub_45B3F0(pEntry);
  if (pHash != NULL) {
    DWORD dwHash = SStrHash(pszName, 0, 0);
    HASHENTRY *pNew = pHash->InternalNewNode(dwHash, 0, 0);
    pNew->dwHash = dwHash;
    pNew->pszKey = pszName;
    pNew->pValue = pEntry;
  }
}

// 0x45B240. Builds "<dir>\<name>" into the entry's szName, allocates the
// entry, registers it in pHash when there is one, and links it into the map
// list. An entry already in the hash short-circuits the whole body.
LISTENTRY *AddMap(BYTE bFlags, const char *pszDir, const char *pszName,
                  MAPHASH *pHash)
{
  char szBuf[0x104];
  LISTENTRY *pEntry;
  int nLen;

  if (pHash != NULL) {
    HASHENTRY *pFound = pHash->Ptr(pszName);
    if (pFound != NULL)
      return (LISTENTRY *)pFound->pValue;
  }

  SStrCopy(szBuf, pszDir, 0x104);
  nLen = (int)strlen(pszDir);
  if (nLen != 0 && szBuf[nLen - 1] != '\\')
    SStrPack(szBuf, "\\", 0x104);

  SStrPack(szBuf, pszName, 0x104);

  pEntry = (LISTENTRY *)SMemAlloc(sizeof(LISTENTRY),
                                  gTypeInfo_LISTENTRY.raw_name(), -2, 8);
  if (pEntry != NULL) {
    pEntry->link.prev = NULL;
    pEntry->link.next = NULL;
  } else {
    pEntry = NULL;
  }

  if (pHash != NULL) {
    DWORD dwHash = SStrHash(pszName, 0, 0);
    HASHENTRY *pNew = pHash->InternalNewNode(dwHash, 0, 0);
    pNew->dwHash = dwHash;
    pNew->pszKey = pszName;
    pNew->pValue = pEntry;
  }

  pEntry->bFlags = bFlags;
  SStrCopy(pEntry->szName, szBuf, 0x104);
  SStrCopy(pEntry->szPath, pszName, 0x104);
  SStrCopy(pEntry->szShort, pEntry->szPath, 0x41);

  if ((bFlags & 0x20) != 0)
    sub_45B3F0(pEntry);
  else
    sub_45B390(pEntry);

  return pEntry;
}

// 0x45B390. Moves pEntry to the tail of gMapList_g, unlinking it first if it is
// already on a list. A NULL argument stands for the list's own link, which is
// the sentinel every walk of this list starts from.
void sub_45B390(LISTENTRY *pEntry)
{
  LISTENTRY *pNode = pEntry != NULL ? pEntry : (LISTENTRY *)&gMapList_g.link;
  LISTENTRY *pPrev = pNode->link.prev;
  LISTENTRY *pTail;

  if (pPrev != NULL) {
    LISTENTRY *pNext = pNode->link.next;
    TSLink<LISTENTRY> *pSlot;

    // A terminator holds the complement of the slot to repair; a real node
    // needs the link offset recovered from the data. See src117/h/TSList.h.
    if ((intptr_t)pNext <= 0) {
      pSlot = (TSLink<LISTENTRY> *)~(intptr_t)pNext;
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pNext > 0)
        nOff = (intptr_t)pNode - (intptr_t)pPrev->link.next;
      pSlot = (TSLink<LISTENTRY> *)((intptr_t)pNext + nOff);
    }

    pSlot->prev = pPrev;
    pNode->link.prev->link.next = pNode->link.next;
    pNode->link.prev = NULL;
    pNode->link.next = NULL;
  }

  pTail = gMapList_g.link.prev;
  pNode->link.prev = pTail;
  pNode->link.next = pTail->link.next;
  pTail->link.next = pEntry;
  gMapList_g.link.prev = pNode;
}

// Orders two map-list display names. The parent-directory name sorts first,
// then bracketed directory names, then names whose third character is a colon.
// Inlined at both call sites in sub_45B3F0.
static int map_name_compare(const char *pszA, const char *pszB)
{
  char ch;

  if (_stricmp(pszA, byte_66ADF0) == 0)
    return -1;
  if (_stricmp(pszB, byte_66ADF0) == 0)
    return 1;

  ch = pszA[0];
  if (ch == '[' && pszB[0] != ch)
    return -1;
  if (pszB[0] == '[' && pszA[0] != '[')
    return 1;

  ch = pszA[2];
  if (ch == ':' && pszB[2] != ch)
    return 1;
  if (pszB[2] == ':' && ch != ':')
    return -1;

  return _stricmp(pszA, pszB);
}

// 0x45B3F0. Moves pEntry to its sorted position in gMapList_g. The walk keeps a
// -1 sentinel for "before the head" so the first comparison is a constant 1.
void sub_45B3F0(LISTENTRY *pEntry)
{
  LISTENTRY *pCur = (LISTENTRY *)-1;
  LISTENTRY *pAfter;
  LISTENTRY *pNode;
  LISTENTRY *pPrev;
  LISTENTRY *pAnchor;
  LISTENTRY *pNextNode;
  TSLink<LISTENTRY> *pSlot;
  intptr_t nOff;

  for (;;) {
    LISTENTRY *pNext;
    int nPrevCmp;
    int nNextCmp;

    if (pCur == (LISTENTRY *)-1) {
      pNext = gMapList_g.link.next;
      if ((intptr_t)pNext <= 0)
        pNext = NULL;
      nPrevCmp = 1;
    } else {
      pNext = pCur->link.next;
      if ((intptr_t)pNext <= 0)
        pNext = NULL;
      nPrevCmp = map_name_compare(pEntry->szShort, pCur->szShort);
    }

    nNextCmp = pNext != NULL ? map_name_compare(pEntry->szShort, pNext->szShort) : -1;

    if (nPrevCmp > 0 && nNextCmp <= 0)
      break;

    pCur = pNext;
    if (pNext == NULL)
      return;
  }

  pAfter = pCur == (LISTENTRY *)-1 ? NULL : pCur;
  pNode = pEntry != NULL ? pEntry : (LISTENTRY *)&gMapList_g.link;
  pPrev = pNode->link.prev;

  if (pPrev != NULL) {
    LISTENTRY *pNext = pNode->link.next;

    if ((intptr_t)pNext <= 0) {
      pSlot = (TSLink<LISTENTRY> *)~(intptr_t)pNext;
    } else {
      nOff = 0;
      if ((intptr_t)pNext > 0)
        nOff = (intptr_t)pNode - (intptr_t)pPrev->link.next;
      pSlot = (TSLink<LISTENTRY> *)((intptr_t)pNext + nOff);
    }

    pSlot->prev = pPrev;
    pNode->link.prev->link.next = pNode->link.next;
    pNode->link.prev = NULL;
    pNode->link.next = NULL;
  }

  pAnchor = pAfter != NULL ? pAfter : (LISTENTRY *)&gMapList_g.link;
  pNode->link.prev = pAnchor;
  pNode->link.next = pAnchor->link.next;

  pNextNode = pAnchor->link.next;
  nOff = gMapList_g.nCachedLinkOffset;
  if ((intptr_t)pNextNode <= 0) {
    pSlot = (TSLink<LISTENTRY> *)~(intptr_t)pNextNode;
  } else {
    if (nOff < 0)
      nOff = (intptr_t)pAnchor - (intptr_t)pAnchor->link.prev->link.next;
    pSlot = (TSLink<LISTENTRY> *)((intptr_t)pNextNode + nOff);
  }
  pSlot->prev = pNode;
  pAnchor->link.next = pEntry;
}

// 0x45C7D0. Re-reads the saved recent-maps blob and, for each name that still
// resolves to a file, either moves the existing list entry to the tail or adds
// one. Five names at most, and the blob is freed either way.
void FindRecentMaps(void)
{
  char szPath[0x104];
  DWORD cbData;
  int nLeft;
  int cbLeft;
  char *psz;
  void *pvData;
  LISTENTRY *pEntry;

  cbData = 0;
  nLeft = 5;
  pvData = GetRecentMaps(&cbData);
  if (pvData == NULL)
    return;

  cbLeft = cbData;
  psz = (char *)pvData;
  while (cbLeft > 0) {
    int nAdvance;

    if (*psz == '\0')
      break;
    if (nLeft-- == 0)
      break;

    nAdvance = SStrLen(psz) + 1;
    if (get_valid_str_len(psz, cbData, cbLeft, 0, 0) &&
        map_find_file(psz, szPath, 0x104)) {
      pEntry = sub_45CB50(psz);
      if (pEntry != NULL) {
        // sub_45B390's body, written out: the oracle inlines it here and a
        // call costs its whole 87 B.
        LISTENTRY *pPrev = pEntry->link.prev;
        LISTENTRY *pTail;

        if (pPrev != NULL) {
          LISTENTRY *pNext = pEntry->link.next;
          TSLink<LISTENTRY> *pSlot;

          if ((intptr_t)pNext <= 0) {
            pSlot = (TSLink<LISTENTRY> *)~(intptr_t)pNext;
          } else {
            intptr_t nOff = 0;
            if ((intptr_t)pNext > 0)
              nOff = (intptr_t)pEntry - (intptr_t)pPrev->link.next;
            pSlot = (TSLink<LISTENTRY> *)((intptr_t)pNext + nOff);
          }

          pSlot->prev = pPrev;
          pEntry->link.prev->link.next = pEntry->link.next;
          pEntry->link.prev = NULL;
          pEntry->link.next = NULL;
        }

        pTail = gMapList_g.link.prev;
        pEntry->link.prev = pTail;
        pEntry->link.next = pTail->link.next;
        pTail->link.next = pEntry;
        gMapList_g.link.prev = pEntry;
        pEntry->bFlags |= 0x10;
      } else {
        char *pszName = strrchr(psz, '\\');
        if (pszName != NULL) {
          *pszName = '\0';
          pszName++;
        } else {
          pszName = psz;
        }
        AddMap(0x14, psz, pszName, NULL);
      }
    }

    cbLeft -= nAdvance;
    psz += nAdvance;
  }

  SMemFree(pvData, "gluMap.cpp", 810, 0);
}

// ---------------------------------------------------------------------------
// MAPPROBE -- the 32-byte record PreloadMap fills in about one map file.
// CreateScenarioGame zeroes it with two movdqu and reads three of its fields.
#pragma pack(push, 1)
struct MAPPROBE {
  /*0x00*/ WORD wMapStringId;
  /*0x02*/ WORD wDescStringId;
  /*0x04*/ BYTE _underived_04[0x14];
  /*0x18*/ WORD wMaxPlayers;
  /*0x1A*/ BYTE _underived_1A[2];
  /*0x1C*/ DWORD dwField1C;
};
#pragma pack(pop)

static_assert(offsetof(MAPPROBE, wMapStringId) == 0x00, "CreateScenarioGame mov ecx, [ebp-20h] then test cx, cx");
static_assert(offsetof(MAPPROBE, wMaxPlayers) == 0x18, "CreateScenarioGame cmp ax, word ptr [ebp-8]");
static_assert(offsetof(MAPPROBE, wDescStringId) == 0x02, "ReadScenarioInfo mov ecx, dword ptr [ebp-22h] then test cx, cx");
static_assert(offsetof(MAPPROBE, dwField1C) == 0x1C, "ReadScenarioInfo mov eax, dword ptr [ebp-8]");
static_assert(sizeof(MAPPROBE) == 0x20, "the two movdqu that zero it");

// 0x4BE950, the one-byte ret /OPT:ICF folded every empty function onto. This
// call site passes nothing, so its sibling is nullary; net_cmds.h's two-argument
// spelling is a different source function at the same address (open decision 6).
void voice_squelch();

// 0x45BFC0. Starts a game from a map-list entry: a directory hands its path
// back, a map builds a fresh header, and a save or a replay reuses the header
// the entry already carries.
// The save arm calls act_none (0x4B8E70) with ECX set, so this spelling needs a gen alias.
int __fastcall act_none(DWORD dwArg);

int CreateNewGame(intptr_t nItem, intptr_t nFlags, intptr_t nArg0, int nArg1, int nArg2, char *pszDir)
{
  const char *pszName = reinterpret_cast<const char *>(nFlags);
  const char *pszPassword = reinterpret_cast<const char *>(nArg0);
  LISTENTRY *pEntry;
  GameHeader hdr;
  BYTE *pHdr = reinterpret_cast<BYTE *>(&hdr);

  WinMsgKillTimer(ghWnd, 0xC);
  if (pszName == NULL && pszDir == NULL)
    return 0x80000006;

  pEntry = map_find_entry(reinterpret_cast<LISTENTRY *>(nItem));
  if (pEntry == NULL)
    return 0x80000006;

  gPlayback = 0;
  int nResult = 0x80000006;
  switch (pEntry->bFlags & 0x8F) {
  case 8:
    SStrCopy(pszDir, pEntry->szName, 0x104);
    return 0x8000000B;

  case 4: {
    BYTE bStarted;
    int nRes;
    DWORD dwErr;

    if (pEntry->dwLoaded != 0) {
      MAPPROBE probe;
      char szPath[0x104];

      switch (pEntry->dwLoading) {
      case 1: return 0x80000007;
      case 2: return 0x80000001;
      }
      memset(&probe, 0, sizeof(probe));
      if (!FindMapFile(pEntry->szName, szPath, 0x104))
        return 0x80000007;
      if (!PreloadMap(szPath, &probe, 0))
        return probe.wMaxPlayers > 0x3B ? 0x80000001 : 0x80000007;
    } else {
      ReadScenarioInfo(pEntry);
      switch (pEntry->dwLoading) {
      case 1: return 0x80000007;
      case 2: return 0x80000001;
      }
    }

    if (pszName == NULL) {
      SStrCopy(pszDir, pEntry->szName, 0x104);
      return 0;
    }

    if (!gbMultiPlayerMode) {
      if (IsSimpleTemplate((BYTE)nArg1, (BYTE)((DWORD)nArg1 >> 8),
                           (WORD)((DWORD)nArg1 >> 16))) {
        if (pEntry->bField47B == 0)
          return 0x8000000C;
        if (pEntry->bField47A == 0)
          return 0x8000000D;
      } else if (pEntry->bField479 < 2) {
        return 0x8000000D;
      }
    }

    memset(&hdr, 0, sizeof(hdr));
    SStrCopy(reinterpret_cast<char *>(pHdr + 4), pszName, 0x18);
    SStrCopy(reinterpret_cast<char *>(pHdr + 0x4D), pEntry->szStatus, 0x20);
    // Through an int array the count is loaded with `movzx`, as in the oracle.
    int anMax[1];
    anMax[0] = pEntry->bField479;
    HEADER_DATA(&hdr).bMaxPlayers = (BYTE)anMax[0];
    HEADER_DATA(&hdr).wMapWidth = pEntry->wWidth;
    HEADER_DATA(&hdr).wMapHeight = pEntry->wHeight;
    HEADER_DATA(&hdr).wTemplate = g_MapTileset;
    HEADER_DATA(&hdr).bGameType = (BYTE)nArg1;
    HEADER_DATA(&hdr).bGameSubType = (BYTE)((DWORD)nArg1 >> 8);
    int nAppr = (BYTE)pEntry->dwField260;
    HEADER_DATA(&hdr).wField0E = (WORD)((DWORD)nArg1 >> 16);
    HEADER_DATA(&hdr).bActivePlayers = 1;
    HEADER_DATA(&hdr).bApproval = (BYTE)nAppr;

    nRes = ConfigureGame(&hdr, (BYTE)nArg2);
    if (nRes != 0)
      return nRes;

    ResetGameCreator();
    if (gbMultiPlayerMode) {
      bStarted = (BYTE)CreateGame_MultiPlayer((intptr_t)&hdr, nArg0);
    } else {
      if (!seat_local_player())
        return 0x80000007;
      bStarted = (BYTE)CreateGame_SinglePlayer((intptr_t)&hdr);
    }
    voice_squelch();
    if (bStarted) {
      RecentMapsUpdate(pEntry);
      return 0;
    }

    dwErr = SErrGetLastError();
    if (dwErr != 0x44) {
      if (dwErr == 0xB7)
        return 0x80000004;
      return dwErr != 0x85100085 ? 0x80000006 : 0x8000000F;
    }
    return 0x80000005;
  }

  case 1:
    if (pEntry->dwLoaded == 0)
      ReadSaveInfo(pEntry);
    switch (pEntry->dwLoading) {
    case 1: return 0x80000007;
    case 2: return 0x80000001;
    }
    if (pszName == NULL) {
      SStrCopy(pszDir, pEntry->szName, 0x104);
      return 0;
    }
    if (pszPassword != NULL && *pszPassword == 0)
      return 0x80000009;
    memcpy(&hdr, &pEntry->abHeader[0x0C], sizeof(hdr));
    SStrCopy(reinterpret_cast<char *>(pHdr + 4), pszName, 0x18);
    if ((BYTE)CreateGame_MultiPlayer((intptr_t)&hdr, nArg0) == 1)
      return act_none(pEntry->dwField264) ? 0 : 0x80000006;
    return SErrGetLastError() != 0xB7 ? 0x80000006 : 0x80000004;

  case 0x80:
    gPlayback = 1;
    pEntry->dwLoaded = 0;
    ReadReplayInfo(pEntry);
    switch (pEntry->dwLoading) {
    case 1: return 0x80000007;
    case 2: return 0x80000001;
    }
    if (pszName == NULL) {
      SStrCopy(pszDir, pEntry->szName, 0x104);
      return 0;
    }
    if (pszPassword != NULL && *pszPassword == 0)
      return 0x8000000A;
    memcpy(&hdr, &pEntry->abHeader[0x0C], sizeof(hdr));
    SStrCopy(reinterpret_cast<char *>(pHdr + 4), pszName, 0x18);
    if (gbMultiPlayerMode) {
      if (CreateGame_MultiPlayer((intptr_t)&hdr, nArg0))
        nResult = 0;
      else if (SErrGetLastError() == 0xB7)
        nResult = 0x80000004;
      break;
    }
    if (!seat_local_player())
      return 0x80000007;
    if (CreateGame_SinglePlayer((intptr_t)&hdr))
      nResult = 0;
    break;
  }
  return nResult;
}

// 0x45C510. Builds a single-player game header for one map and starts it. The
// 0x800000NN codes are the oracle's, one per rejected reason.
int CreateScenarioGame(const char *pszMap, char *pszName, int, int nKey, BYTE bSpeed)
{
  GameHeader hdr;
  MAPPROBE probe;
  BYTE *pHdr = reinterpret_cast<BYTE *>(&hdr);
  const char *pszMapName;
  WORD wId;
  int nRes;
  BYTE bStarted;

  if (pszMap == NULL || pszName == NULL)
    return 0x80000006;

  memset(&probe, 0, sizeof(probe));
  if (!PreloadMap(const_cast<char *>(pszMap), &probe, 0))
    return probe.wMaxPlayers > 0x3B ? 0x80000001 : 0x80000006;

  memset(&hdr, 0, sizeof(hdr));
  SStrCopy(reinterpret_cast<char *>(pHdr + 4), pszName, 0x20);
  wId = probe.wMapStringId;
  pszMapName = wId != 0 ? get_map_string(wId) : "";
  SStrCopy(reinterpret_cast<char *>(pHdr + 0x4D), pszMapName, 0x20);

  HEADER_DATA(&hdr).bActivePlayers = 1;
  HEADER_DATA(&hdr).bMaxPlayers = CountValidSlots();
  HEADER_DATA(&hdr).wMapWidth = gwMapWidth;
  HEADER_DATA(&hdr).wMapHeight = gwMapHeight;
  HEADER_DATA(&hdr).wTemplate = g_MapTileset;
  HEADER_DATA(&hdr).bGameType = (BYTE)nKey;
  HEADER_DATA(&hdr).bGameSubType = (BYTE)((DWORD)nKey >> 8);
  HEADER_DATA(&hdr).wField0E = (WORD)((DWORD)nKey >> 16);
  HEADER_DATA(&hdr).bApproval = (BYTE)probe.dwField1C;

  nRes = ConfigureGame(&hdr, bSpeed);
  if (nRes != 0)
    return nRes;

  ResetGameCreator();
  if (!seat_local_player())
    return 0x80000007;

  bStarted = (BYTE)CreateGame_SinglePlayer((intptr_t)&hdr);
  voice_squelch();
  if (bStarted)
    return 0;

  return SErrGetLastError() != 0xB7 ? 0x80000006 : 0x80000004;
}

// Hyperion: CreateScenarioGame's header for one map, made a network game, as Create
// Game's Ok makes one from the map list (CreateNewGame's map arm): a LAN room's match, made
// by the seat its launch says makes it (port_lanlobby.cpp). 0 when the game is made.
int PortCreateNetGame(const char *pszMap, char *pszName, int nKey, BYTE bSpeed)
{
  GameHeader hdr;
  MAPPROBE probe;
  BYTE *pHdr = reinterpret_cast<BYTE *>(&hdr);
  const char *pszMapName;
  WORD wId;
  int nRes;

  if (pszMap == NULL || pszName == NULL)
    return 0x80000006;

  memset(&probe, 0, sizeof(probe));
  if (!PreloadMap(const_cast<char *>(pszMap), &probe, 0))
    return probe.wMaxPlayers > 0x3B ? 0x80000001 : 0x80000006;

  memset(&hdr, 0, sizeof(hdr));
  SStrCopy(reinterpret_cast<char *>(pHdr + 4), pszName, 0x18);
  wId = probe.wMapStringId;
  pszMapName = wId != 0 ? get_map_string(wId) : "";
  SStrCopy(reinterpret_cast<char *>(pHdr + 0x4D), pszMapName, 0x20);

  HEADER_DATA(&hdr).bActivePlayers = 1;
  HEADER_DATA(&hdr).bMaxPlayers = CountValidSlots();
  HEADER_DATA(&hdr).wMapWidth = gwMapWidth;
  HEADER_DATA(&hdr).wMapHeight = gwMapHeight;
  HEADER_DATA(&hdr).wTemplate = g_MapTileset;
  HEADER_DATA(&hdr).bGameType = (BYTE)nKey;
  HEADER_DATA(&hdr).bGameSubType = (BYTE)((DWORD)nKey >> 8);
  HEADER_DATA(&hdr).wField0E = (WORD)((DWORD)nKey >> 16);
  HEADER_DATA(&hdr).bApproval = (BYTE)probe.dwField1C;

  nRes = ConfigureGame(&hdr, bSpeed);
  if (nRes != 0)
    return nRes;

  ResetGameCreator();
  const BYTE bStarted = (BYTE)CreateGame_MultiPlayer((intptr_t)&hdr, (intptr_t)"");
  voice_squelch();
  if (bStarted)
    return 0;
  return SErrGetLastError() != 0xB7 ? 0x80000006 : 0x80000004;
}

// 0x45C680. Remembers the directory the map browser is showing.
void sub_45C680(char *pszDir)
{
  SStrCopy(byte_66AEF8, pszDir, 0x104);
}

// 0x45C6A0. Allocates the record for one map-setting control and stores it in
// the slot its index names. The case order is the oracle's block order; an
// index past 9 frees the record again.
void CreateSetControl(int nIndex, void *pData, void *pfnSet, void *pfnEnable)
{
  SETCTRL *pCtrl = (SETCTRL *)SMemAlloc(sizeof(SETCTRL), "gluMap.cpp", 1179, 0);

  pCtrl->dwData = (DWORD_PTR)pData;
  pCtrl->pfnSet = (void(__stdcall *)(int, DWORD_PTR, const char *))pfnSet;
  pCtrl->pfnEnable = (void(__stdcall *)(int, DWORD_PTR, int))pfnEnable;

  switch (nIndex) {
    case 0: dword_66ADC8 = pCtrl; return;
    case 1: dword_66ADCC = pCtrl; return;
    case 2: dword_66ADE8 = pCtrl; return;
    case 3: dword_66ADD0 = pCtrl; return;
    case 4: dword_66ADD4 = pCtrl; return;
    case 5: dword_66ADE4 = pCtrl; return;
    case 6: dword_66ADD8 = pCtrl; return;
    case 9: dword_66ADDC = pCtrl; return;
    case 7: dword_66ADE0 = pCtrl; return;
    case 8: dword_66ADEC = pCtrl; return;
  }

  SMemFree(pCtrl, "gluMap.cpp", 1235, 0);
}

// 0x45C9A0. Walks pszSearchDir and files every entry: a subdirectory goes to
// AddDirectory when flag 8 is set, and a file is tagged 4 for a map, 1 for a
// saved game or 0x80 for a replay before it goes to AddMap. The flag byte each
// entry gets is the caller's masked to 0x70 plus that one bit.
// One AddMap call per arm: MSVC merges the three tails after allocation, and
// their extra pszDir uses give it EBX over the flag bytes.
void sub_45C9A0(BYTE bFlags, const char *pszSearchDir, const char *pszDir,
                int bAtRoot, MAPHASH *pHash)
{
  char szPath[0x104];
  WIN32_FIND_DATAA fd;
  HANDLE hFind;
  const char *pszSaveExt;
  BYTE bMasked;
  int nLen;

  strcpy(szPath, pszSearchDir);
  nLen = (int)strlen(szPath);
  if (nLen != 0 && szPath[nLen - 1] != '\\')
    SStrPack(szPath, "\\", 0x104);
  SStrPack(szPath, "*.*", 0x104);

  pszSaveExt = GetSaveExtension() + 1;
  bMasked = bFlags & 0x70;

  hFind = FindFirstFileA(szPath, &fd);
  if (hFind == INVALID_HANDLE_VALUE)
    return;

  do {
    if ((fd.dwFileAttributes & 0x10) != 0) {
      if ((bFlags & 8) != 0)
        AddDirectory(fd.cFileName, pszDir, bAtRoot, pHash);
    } else if ((fd.dwFileAttributes & 2) == 0) {
      char *pszExt = strrchr(fd.cFileName, '.');
      // The oracle tests the pre-increment copy: mov ecx, esi / inc esi.
      if (pszExt++ != NULL) {
        if ((gbExpansion_g != 0 && _stricmp(pszExt, "scx") == 0) ||
            _stricmp(pszExt, "scm") == 0 ||
            _stricmp(pszExt, "hxm") == 0)  // Hyperion: Hyperion's maps (port_hxm.cpp)
          AddMap(bMasked | 4, pszDir, fd.cFileName, pHash);
        else if (gbMultiPlayerMode != 0 && _stricmp(pszExt, pszSaveExt) == 0)
          AddMap(bMasked | 1, pszDir, fd.cFileName, pHash);
        else if (_stricmp(pszExt, "rep") == 0)
          AddMap(bMasked | 0x80, pszDir, fd.cFileName, pHash);
      }
    }
  } while (FindNextFileA(hFind, &fd));

  FindClose(hFind);
}

// 0x45CB50. Case-insensitive lookup of a list entry by its szName.
LISTENTRY *sub_45CB50(const char *pszName)
{
  LISTENTRY *pEntry = gMapList_g.link.next;

  while ((intptr_t)pEntry > 0) {
    if (_stricmp(pEntry->szName, pszName) == 0)
      return pEntry;
    pEntry = (pEntry != NULL ? pEntry
                             : (LISTENTRY *)&gMapList_g.link)->link.next;
  }

  return NULL;
}

// 0x45CB90. Builds pszFileName's full path under the user data directory and
// says whether a plain file is there, falling back to the install directory.
// `~(attrs >> 4) & 1` is false for a directory and for the 0xFFFFFFFF failure.
// The second probe's byte has to live in a char array: every plain char local
// folds the oracle's `movsx eax, al` away.
int FindMapFile(const char *pszFileName, char *pszBuffer, int cchBuffer)
{
  char acNotDir[1];

  // Hyperion: a full path, which a save keeps for a map outside the game's folders
  // (RemoveBasePath), is the file itself.
  if ((pszFileName[0] != 0 && pszFileName[1] == ':') || (pszFileName[0] == '\\' && pszFileName[1] == '\\')) {
    SStrCopy(pszBuffer, pszFileName, cchBuffer);
    return (~(GetFileAttributesA(pszBuffer) >> 4) & 1) != 0;
  }
  SFileGetUserDataPath(pszBuffer, cchBuffer, 1);
  SStrPack(pszBuffer, pszFileName, cchBuffer);
  if ((~(GetFileAttributesA(pszBuffer) >> 4) & 1) != 0)
    return 1;

  SFileGetInstallPath(pszBuffer, cchBuffer, 1);
  SStrPack(pszBuffer, pszFileName, cchBuffer);
  acNotDir[0] = ~(char)(GetFileAttributesA(pszBuffer) >> 4);
  // Hyperion: then the folders given with --maps (port_paths.cpp). A
  // failed search leaves the install folder's path in pszBuffer, as before.
  return (acNotDir[0] & 1) != 0 || PortMapFindInFolders(pszFileName, pszBuffer, cchBuffer);
}

// 0x45CC00. Reads the saved recent-maps blob out of the registry into a fresh
// Storm allocation, returning NULL and zeroing the size on any failure.
void *GetRecentMaps(DWORD *pcbData)
{
  void *pvData;

  if (SRegLoadData("Starcraft", "Recent Maps", 0x80, NULL, 0, pcbData) &&
      *pcbData != 0) {
    pvData = SMemAlloc(*pcbData, "gluMap.cpp", 743, 0);
    if (!SRegLoadData("Starcraft", "Recent Maps", 0x80, pvData, *pcbData,
                      NULL)) {
      SMemFree(pvData, "gluMap.cpp", 750, 0);
      pvData = NULL;
      *pcbData = 0;
    }
    return pvData;
  }

  return NULL;
}

// ---------------------------------------------------------------------------
// SAVEHDR -- the 0xB5-byte save-game header sub_499750 reads, the same blob
// saveload.h's TSAVED_GAME_NODE::abHeader carries as one array. Only the seven
// fields ReadSaveInfo reads out of it are named.
#pragma pack(push, 1)
struct SAVEHDR {
  /*0x00*/ BYTE _underived_00[0x28];
  /*0x28*/ DWORD dwField28;
  /*0x2C*/ WORD wWidth;
  /*0x2E*/ WORD wHeight;
  /*0x30*/ BYTE bPlayers;
  /*0x31*/ BYTE bField31;
  /*0x32*/ BYTE _underived_32[0x3C - 0x32];
  /*0x3C*/ WORD wTileset;
  /*0x3E*/ BYTE _underived_3E[0x59 - 0x3E];
  /*0x59*/ char szName[0x99 - 0x59];
  /*0x99*/ char szDescription[0xB5 - 0x99];
};
#pragma pack(pop)

static_assert(offsetof(SAVEHDR, dwField28) == 0x28, "ReadSaveInfo mov eax, dword ptr [ebp-94h]");
static_assert(offsetof(SAVEHDR, wWidth) == 0x2C, "ReadSaveInfo movzx eax, word ptr [ebp-90h]");
static_assert(offsetof(SAVEHDR, wHeight) == 0x2E, "ReadSaveInfo movzx eax, word ptr [ebp-8Eh]");
static_assert(offsetof(SAVEHDR, bPlayers) == 0x30, "ReadSaveInfo mov al, byte ptr [ebp-8Ch]");
static_assert(offsetof(SAVEHDR, bField31) == 0x31, "ReadSaveInfo mov al, byte ptr [ebp-8Bh]");
static_assert(offsetof(SAVEHDR, wTileset) == 0x3C, "ReadSaveInfo movzx eax, word ptr [ebp-80h]");
static_assert(offsetof(SAVEHDR, szName) == 0x59, "ReadSaveInfo lea eax, [ebp-63h] into psnprintf");
static_assert(offsetof(SAVEHDR, szDescription) == 0x99, "ReadSaveInfo lea eax, [ebp-23h] into SStrPack");
static_assert(sizeof(SAVEHDR) == 0xB5, "the rep movsd of 2Dh dwords plus the trailing movsb");

// One of the twelve 36-byte player slots replay_assign_gamedata fills; only
// the slot type is read here.
struct REPLAYSLOT {
  /*0x00*/ BYTE _underived_00[8];
  /*0x08*/ BYTE bType;
  /*0x09*/ BYTE _underived_09[0x24 - 0x09];
};

static_assert(offsetof(REPLAYSLOT, bType) == 0x08, "ReadReplayInfo lea ecx, [ebp-2D4h] off a [ebp-2DCh] base");
static_assert(sizeof(REPLAYSLOT) == 0x24, "ReadReplayInfo add ecx, 24h");

// 0x45D250. The replay twin of ReadSaveInfo: reads the replay's header and slot
// table and fills the entry's display strings, counting computer (type 1) and
// human (type 2) slots. Sets gPlayback before anything else.
void ReadReplayInfo(LISTENTRY *pEntry)
{
  char szPath[0x104];
  REPLAYSLOT aSlots[12];
  MAPPROBE probe;
  SAVEHDR *pHdr;
  DWORD dwField28;
  int bIsExpansion;
  int nPathLen;
  int nNameLen;
  int i;
  BYTE bHumans;
  BYTE bComputers;
  const char *psz;

  gPlayback = 1;
  if (pEntry->dwLoaded != 0)
    return;

  pEntry->dwLoaded = 1;
  pEntry->dwLoading = 1;
  SStrCopy(pEntry->szStatus, glues_get_str(0x3D), 0x20);
  nNameLen = SStrLen(pEntry->szPath);

  SFileGetUserDataPath(szPath, 0x104, 1);
  SStrPack(szPath, pEntry->szName, 0x104);
  if ((~(GetFileAttributesA(szPath) >> 4) & 1) == 0) {
    SFileGetInstallPath(szPath, 0x104, 1);
    SStrPack(szPath, pEntry->szName, 0x104);
    // Hyperion: then the folders given with --maps (port_paths.cpp)
    if ((~(GetFileAttributesA(szPath) >> 4) & 1) == 0 &&
        !PortMapFindInFolders(pEntry->szName, szPath, 0x104)) {
      SStrCopy(pEntry->szDescription, glues_get_str(0x42), 0x100);
      return;
    }
  }

  nPathLen = SStrLen(szPath);
  SStrCopy(pEntry->szStatus, glues_get_str(0xB2), 0x20);
  if (nNameLen >= 0x20 || nPathLen + 1 + nNameLen > 0x104) {
    SStrCopy(pEntry->szDescription, glues_get_str(0x3E), 0x100);
    return;
  }

  if (!LoadReplayData(szPath, &bIsExpansion)) {
    pEntry->dwLoading = 2;
    SStrCopy(pEntry->szDescription, glues_get_str(bIsExpansion != 0 ? 0x3C : 0x3F),
             0x100);
    return;
  }

  bHumans = 0;
  bComputers = 0;
  replay_assign_gamedata(&pEntry->abHeader[0x0C], aSlots);
  for (i = 0; i < 12; i++) {
    switch (aSlots[i].bType) {
      case 1: bComputers++; break;
      case 2: bHumans++; break;
    }
  }

  pHdr = (SAVEHDR *)pEntry->abHeader;
  pEntry->wWidth = pHdr->wWidth;
  pEntry->wHeight = pHdr->wHeight;
  pEntry->wTileset = pHdr->wTileset;
  dwField28 = pHdr->dwField28;
  pEntry->bField47A = bComputers;
  pEntry->bField47B = bHumans;
  pEntry->bField478 = 1;
  pEntry->bField479 = 8;
  pEntry->dwField264 = dwField28;

  pEntry->dwMission = GetReplayMission();
  memset(&probe, 0, sizeof(probe));
  if (!(pEntry->dwMission != 0 ? PreloadCampaignMap((WORD)pEntry->dwMission, &probe)
                               : PreloadMap(szPath, &probe, 0))) {
    if (probe.wMaxPlayers > 0x3B) {
      psz = glues_get_str(0x3C);
      pEntry->dwLoading = 2;
      SStrCopy(pEntry->szDescription, psz, 0x100);
      return;
    }
    psz = glues_get_str(0x3F);
    SStrCopy(pEntry->szDescription, psz, 0x100);
    return;
  }

  psnprintf(pEntry->szDescription, 0x100, glues_get_str(0x30), pHdr->szName);
  SStrPack(pEntry->szDescription, "\n\n", 0x100);
  SStrCopy(pEntry->szShort, pEntry->szPath, 0x41);
  psnprintf(pEntry->szPlayers, 0x23, glues_get_str(0x1E), pEntry->bField478);
  psnprintf(pEntry->szDimensions, 0x23, glues_get_str(0x20), pEntry->wWidth,
            pEntry->wHeight);
  psnprintf(pEntry->szComputers, 0x23, glues_get_str(0x23), bComputers);
  psnprintf(pEntry->szHumans, 0x23, glues_get_str(0x25), bHumans);
  SStrCopy(pEntry->szTileset, glues_get_str(0x27 + pEntry->wTileset), 0x23);
  pEntry->dwLoading = 0;
}

// 0x45D5E0. Loads one saved game's header and builds the entry's display
// strings. dwLoading marks the entry busy for the duration; every failure
// leaves the reason string in szDescription and dwLoading set.
void ReadSaveInfo(LISTENTRY *pEntry)
{
  char szPath[0x104];
  SAVEHDR hdr;
  int nPathLen;
  int nNameLen;
  BYTE bRes;

  if (pEntry->dwLoaded != 0)
    return;

  pEntry->dwLoaded = 1;
  pEntry->dwLoading = 1;
  SStrCopy(pEntry->szStatus, glues_get_str(0x40), 0x20);
  nNameLen = SStrLen(pEntry->szPath);

  SFileGetUserDataPath(szPath, 0x104, 1);
  SStrPack(szPath, pEntry->szName, 0x104);
  if ((~(GetFileAttributesA(szPath) >> 4) & 1) == 0) {
    SFileGetInstallPath(szPath, 0x104, 1);
    SStrPack(szPath, pEntry->szName, 0x104);
    // Hyperion: then the folders given with --maps (port_paths.cpp)
    if ((~(GetFileAttributesA(szPath) >> 4) & 1) == 0 &&
        !PortMapFindInFolders(pEntry->szName, szPath, 0x104)) {
      SStrCopy(pEntry->szDescription, glues_get_str(0x42), 0x100);
      return;
    }
  }

  nPathLen = SStrLen(szPath);
  SStrCopy(pEntry->szStatus, glues_get_str(0x2F), 0x20);
  if (nNameLen >= 0x20 || nPathLen + 1 + nNameLen > 0x104) {
    SStrCopy(pEntry->szDescription, glues_get_str(0x41), 0x100);
    return;
  }

  bRes = sub_499750(szPath, &hdr);
  if (bRes == 1) {
    SStrCopy(pEntry->szDescription, glues_get_str(0x42), 0x100);
    return;
  }
  if (bRes == 2) {
    SStrCopy(pEntry->szDescription, glues_get_str(0x43), 0x100);
    return;
  }

  pEntry->bField478 = hdr.bPlayers;
  pEntry->bField479 = hdr.bField31;
  memcpy(pEntry->abHeader, &hdr, sizeof(hdr));
  pEntry->wWidth = hdr.wWidth;
  pEntry->wHeight = hdr.wHeight;
  pEntry->wTileset = hdr.wTileset;
  pEntry->dwField264 = hdr.dwField28;
  psnprintf(pEntry->szDescription, 0x100, glues_get_str(0x30), hdr.szName);
  SStrPack(pEntry->szDescription, "\n\n", 0x100);
  SStrPack(pEntry->szDescription, hdr.szDescription, 0x100);
  SStrCopy(pEntry->szShort, pEntry->szPath, 0x41);
  psnprintf(pEntry->szPlayers, 0x23, glues_get_str(0x1E), pEntry->bField478);
  psnprintf(pEntry->szDimensions, 0x23, glues_get_str(0x20), pEntry->wWidth,
            pEntry->wHeight);
  SStrCopy(pEntry->szTileset, glues_get_str(0x27 + pEntry->wTileset), 0x23);
  pEntry->dwLoading = 0;
}

// 0x45D870. The map twin of ReadSaveInfo: probes the file, reads its header and
// the current slot counts, and fills the same display strings. dwLoading is
// left at 2 for a map whose player count is out of range.
void ReadScenarioInfo(LISTENTRY *pEntry)
{
  char szPath[0x104];
  MAPPROBE probe;
  BYTE bHumans;
  BYTE bValid;
  BYTE bComputers;
  int nPathLen;
  int nNameLen;
  const char *psz;

  if (pEntry->dwLoaded != 0)
    return;

  pEntry->dwLoaded = 1;
  pEntry->dwLoading = 1;
  SStrCopy(pEntry->szStatus, glues_get_str(0x3D), 0x20);
  nNameLen = SStrLen(pEntry->szPath);

  SFileGetUserDataPath(szPath, 0x104, 1);
  SStrPack(szPath, pEntry->szName, 0x104);
  if ((~(GetFileAttributesA(szPath) >> 4) & 1) == 0) {
    SFileGetInstallPath(szPath, 0x104, 1);
    SStrPack(szPath, pEntry->szName, 0x104);
    // Hyperion: then the folders given with --maps (port_paths.cpp)
    if ((~(GetFileAttributesA(szPath) >> 4) & 1) == 0 &&
        !PortMapFindInFolders(pEntry->szName, szPath, 0x104)) {
      SStrCopy(pEntry->szDescription, glues_get_str(0x42), 0x100);
      return;
    }
  }

  nPathLen = SStrLen(szPath);
  if (nNameLen >= 0x20 || nPathLen + 1 + nNameLen > 0x104) {
    SStrCopy(pEntry->szDescription, glues_get_str(0x3E), 0x100);
    return;
  }

  memset(&probe, 0, sizeof(probe));
  if (!PreloadMap(szPath, &probe, 0)) {
    if (probe.wMaxPlayers > 0x3B) {
      psz = glues_get_str(0x3C);
      pEntry->dwLoading = 2;
      SStrCopy(pEntry->szDescription, psz, 0x100);
      return;
    }
    psz = glues_get_str(0x3F);
    SStrCopy(pEntry->szDescription, psz, 0x100);
    return;
  }

  bValid = CountValidSlots();
  bComputers = CountComputerSlots();
  bHumans = CountDefaultHumanSlots();
  pEntry->bField478 = 1;
  pEntry->bField479 = bValid;
  pEntry->bField47A = bComputers;
  pEntry->bField47B = bHumans;
  pEntry->wWidth = gwMapWidth;
  pEntry->wHeight = gwMapHeight;
  pEntry->wTileset = g_MapTileset;

  psz = probe.wMapStringId != 0 ? get_map_string(probe.wMapStringId) : "";
  SStrCopy(pEntry->szStatus, psz, 0x20);
  if (pEntry->szStatus[0] == '\0')
    SStrCopy(pEntry->szStatus, pEntry->szPath, 0x20);

  psz = probe.wDescStringId != 0 ? get_map_string(probe.wDescStringId) : "";
  SStrCopy(pEntry->szDescription, psz, 0x100);

  psnprintf(pEntry->szPlayers, 0x23, glues_get_str(0x1E), bValid);
  psnprintf(pEntry->szDimensions, 0x23, glues_get_str(0x20), pEntry->wWidth,
            pEntry->wHeight);
  psnprintf(pEntry->szComputers, 0x23, glues_get_str(0x23), bComputers);
  psnprintf(pEntry->szHumans, 0x23, glues_get_str(0x25), bHumans);
  SStrCopy(pEntry->szTileset, glues_get_str(0x27 + pEntry->wTileset), 0x23);
  get_selectable_races(pEntry->abRaces, 8);
  pEntry->dwField260 = probe.dwField1C;
  pEntry->dwLoading = 0;
}

// 0x45D1E0. The timer that finishes loading the pending list entry: it kills
// itself, loads the entry by its kind and pushes it into the controls.
void __stdcall ReadEntryInfo(intptr_t hWnd, DWORD uMsg, DWORD idEvent, DWORD dwTime)
{
  if (hWnd != 0)
    WinMsgKillTimer(reinterpret_cast<void *>(hWnd), idEvent);

  gPlayback = 0;
  if ((dword_66AEF4->bFlags & 4) != 0)
    ReadScenarioInfo(dword_66AEF4);
  else if ((dword_66AEF4->bFlags & 1) != 0)
    ReadSaveInfo(dword_66AEF4);
  else if ((dword_66AEF4->bFlags & 0x80) != 0)
    ReadReplayInfo(dword_66AEF4);

  UpdateMapCtrl(dword_66AEF4);
  dword_66AEF4 = NULL;
}

// 0x45DCF0. Moves pEntry to the front of the map list, flags it recent, and
// rewrites the registry blob from the first five entries that carry both the
// 4 and 0x10 flags. The unlink is the TSList.h idiom; the relink reads the
// list's cached link offset and only recovers it from the data when negative.
void RecentMapsUpdate(LISTENTRY *pEntry)
{
  LISTENTRY *pPrev;
  LISTENTRY *pFirst;
  LISTENTRY *p;
  TSLink<LISTENTRY> *pSlot;
  int nCount;
  int nLeft;
  int cbBuf;
  int nOff;

  if (pEntry == NULL)
    return;

  FindRecentMaps();

  pPrev = pEntry->link.prev;
  if (pPrev != NULL) {
    LISTENTRY *pNext = pEntry->link.next;

    if ((intptr_t)pNext <= 0) {
      pSlot = (TSLink<LISTENTRY> *)~(intptr_t)pNext;
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pNext > 0)
        nOff = (intptr_t)pEntry - (intptr_t)pPrev->link.next;
      pSlot = (TSLink<LISTENTRY> *)((intptr_t)pNext + nOff);
    }

    pSlot->prev = pPrev;
    pEntry->link.prev->link.next = pEntry->link.next;
    pEntry->link.prev = NULL;
    pEntry->link.next = NULL;
  }

  pEntry->link.prev = (LISTENTRY *)&gMapList_g.link;
  pEntry->link.next = gMapList_g.link.next;
  pFirst = gMapList_g.link.next;
  nOff = gMapList_g.nCachedLinkOffset;
  if ((intptr_t)pFirst <= 0) {
    pSlot = (TSLink<LISTENTRY> *)~(intptr_t)pFirst;
  } else {
    if (nOff < 0)
      nOff = (int)((intptr_t)&gMapList_g.link - (intptr_t)gMapList_g.link.prev->link.next);
    pSlot = (TSLink<LISTENTRY> *)((intptr_t)pFirst + nOff);
  }
  pSlot->prev = pEntry;
  gMapList_g.link.next = pEntry;

  pEntry->bFlags |= 0x10;

  nLeft = 5;
  nCount = 0;
  for (p = gMapList_g.link.next; (intptr_t)p > 0; p = p->link.next) {
    if ((p->bFlags & 4) != 0 && (p->bFlags & 0x10) != 0)
      nCount++;
  }

  if (nCount == 0)
    return;

  cbBuf = (nCount < nLeft ? nCount : nLeft) * 0x104;

  char *pBuf = (char *)SMemAlloc(cbBuf, "gluMap.cpp", 843, 0);
  char *pCur = pBuf;
  intptr_t cchLeft = cbBuf;

  p = gMapList_g.link.next;
  while ((intptr_t)p > 0) {
    if ((p->bFlags & 4) != 0 && (p->bFlags & 0x10) != 0) {
      int n;
      if (nLeft-- == 0)
        break;
      n = (int)((uintptr_t)SStrCopy(pCur, p->szName, cchLeft) + 1);
      cchLeft -= n;
      // Stored through the index before the advance, so pCur ranks below nLeft.
      pCur[n] = '\0';
      pCur += n;
    }
    p = p->link.next;
  }

  pCur[1] = '\0';
  SRegSaveData("Starcraft", "Recent Maps", 0x80, pBuf, (DWORD)(cbBuf - cchLeft + 1));
  SMemFree(pBuf, "gluMap.cpp", 872, 0);
}

// 0x45DFC0. Pushes one entry's strings into the map-setting controls and
// refreshes the slot panel. Each control is told its own index, not its slot
// order, and the last statement tail-jmps sub_455710.
void UpdateMapCtrl(LISTENTRY *pEntry)
{
  int nPlayers;
  BYTE bCategory;

  if (pEntry->dwLoading == 1) {
    SStrCopy(pEntry->szGameType, "", 0x20);
  } else if ((pEntry->bFlags & 0x81) != 0) {
    // Through a pointer, or the category load is scheduled above the store.
    const BYTE *pHdr = pEntry->abHeader;
    pEntry->szGameType[0] = '\0';
    bCategory = pHdr[0x79];
    if (bCategory != 0)
      SStrCopy(pEntry->szGameType,
               gametype_find_template_name(bCategory, pHdr[0x7A]),
               0x20);
  } else {
    // Hyperion: the game type, category, subtype and template below were
    // the Battle.net create dialog's (sub_44DEA0, sub_44DE90, sub_44DE70 and
    // sub_44DE80, gluBNCreate.cpp). The dialog is gone, and they are what it
    // left when it never ran: empty and zero.
    SStrCopy(pEntry->szGameType, "", 0x20);
  }

  if ((pEntry->bFlags & 0x80) != 0 || IsSimpleTemplate(0, 0, 0)) {
    if (dword_66ADD8 != NULL)
      dword_66ADD8->pfnEnable(6, dword_66ADD8->dwData, 1);
    if (dword_66ADDC != NULL)
      dword_66ADDC->pfnEnable(9, dword_66ADDC->dwData, 1);
  } else {
    if (dword_66ADD8 != NULL)
      dword_66ADD8->pfnEnable(6, dword_66ADD8->dwData, 0);
    if (dword_66ADDC != NULL)
      dword_66ADDC->pfnEnable(9, dword_66ADDC->dwData, 0);
  }

  stat_strip_control_chars(pEntry->szDescription, pEntry->szDescription, 0x100);

  if (dword_66ADC8 != NULL)
    dword_66ADC8->pfnSet(0, dword_66ADC8->dwData, pEntry->szStatus);
  if (dword_66ADCC != NULL)
    dword_66ADCC->pfnSet(1, dword_66ADCC->dwData, pEntry->szDescription);
  if (dword_66ADD0 != NULL)
    dword_66ADD0->pfnSet(3, dword_66ADD0->dwData, pEntry->szGameType);
  if (dword_66ADD4 != NULL)
    dword_66ADD4->pfnSet(4, dword_66ADD4->dwData, pEntry->szPlayers);
  if (dword_66ADD8 != NULL)
    dword_66ADD8->pfnSet(6, dword_66ADD8->dwData, pEntry->szComputers);
  if (dword_66ADE0 != NULL)
    dword_66ADE0->pfnSet(7, dword_66ADE0->dwData, pEntry->szHumans);
  if (dword_66ADE4 != NULL)
    dword_66ADE4->pfnSet(5, dword_66ADE4->dwData, pEntry->szDimensions);
  if (dword_66ADE8 != NULL)
    dword_66ADE8->pfnSet(2, dword_66ADE8->dwData, pEntry->szField189);
  if (dword_66ADEC != NULL)
    dword_66ADEC->pfnSet(8, dword_66ADEC->dwData, pEntry->szTileset);

  set_selectable_races(pEntry->abRaces, 8);
  // Hyperion: here the Battle.net create dialog refilled its subtypes
  // (sub_44D7D0, sub_44D5D0); gone with it.

  if (gPlayback != 0)
    nPlayers = replay_get_max_players();
  else
    nPlayers = pEntry->bField479;
  sub_455710(nPlayers);
}

// 0x475040. Publishes a multiplayer game through Storm and, on success, takes a
// copy of the header. The oracle pushes "net_glue.cpp" here, so this body's own
// __FILE__ disagrees with the ledger's gluMap.cpp.
int CreateGame_MultiPlayer(intptr_t nArg0, intptr_t nArg1)
{
  GameHeader *pHdr = reinterpret_cast<GameHeader *>(nArg0);
  char szTxt[0x80];
  const TGAMEPARAMS *pParams;
  const TTEMPLATEMETA *pMeta;
  int nLadder;
  DWORD dw1;
  DWORD dw2;

  if (IsSpawnedCopy()) {
    err_popup(0x68, 0, 0, 0, 1);
    return 0;
  }

  pParams = gametype_find_template(HEADER_DATA(pHdr).bGameType,
                                   HEADER_DATA(pHdr).bGameSubType,
                                   HEADER_DATA(pHdr).wField0E);
  if (pParams == NULL) {
    err_popup(0x5D, (intptr_t)"CreateGame_MultiPlayer()", (intptr_t)"net_glue.cpp", 0x260, 0);
    return 0;
  }

  *reinterpret_cast<TGAMEPARAMS *>(&pHdr->bGameType) = *pParams;
  HEADER_DATA(pHdr).bActivePlayers = 1;
  if (strlen(gLocalPlayerName) >= 0x19)
    return 0;
  SStrCopy(HEADER_DATA(pHdr).szGameName, gLocalPlayerName, 0x19);

  if (pHdr->bNumTeams != 0 && HEADER_DATA(pHdr).dwGameId == 0)
    HEADER_DATA(pHdr).bMaxPlayers = 8;

  nLadder = 0;
  switch (pHdr->bLadderGame) {
    case 0: nLadder = 0; break;
    case 1: nLadder = 1; break;
    case 2: nLadder = 3; break;
    case 3: nLadder = 5; break;
  }

  if (IsWGTGame()) {
    pMeta = gametype_find_template_meta(HEADER_DATA(pHdr).bGameType,
                                        HEADER_DATA(pHdr).bGameSubType,
                                        HEADER_DATA(pHdr).wField0E);
    if (pMeta != NULL)
      nLadder = pMeta->dwValue;
  }

  if (IsOOOGame(pHdr) && HEADER_DATA(pHdr).dwGameId == 0) {
    BYTE bMax = HEADER_DATA(pHdr).bMaxPlayers;
    HEADER_DATA(pHdr).bMaxPlayers = (BYTE)(bMax < 2 ? bMax : 2);
  }

  if (IsScenarioGame(pHdr) && HEADER_DATA(pHdr).dwGameId == 0)
    HEADER_DATA(pHdr).bMaxPlayers = CountHumanSlots();

  HEADER_DATA(pHdr).dwField10 = sub_4634E0();
  GameData2Txt(&HEADER_DATA(pHdr), szTxt, 0x80);
  dw1 = sub_4476D0(&pHdr->bGameType);
  dw2 = sub_4476F0(&pHdr->bGameType);

  if (SNetCreateLadderGame(reinterpret_cast<char *>(pHdr) + 4, nArg1, szTxt,
                           dw1, nLadder, dw2, &pHdr->bGameType, 0x20,
                           HEADER_DATA(pHdr).bMaxPlayers, gLocalPlayerName, "",
                           &g_LocalStormID)) {
    gbIsGameCreator_g = 1;
    memcpy(&gGameHeader, pHdr, sizeof(GameHeader));
    return 1;
  }

  return 0;
}

// 0x4A8350. Copies pszSrc to pszDst dropping characters below 0x20 other than
// tab, newline and return, stopping after cch kept characters. A zero cch at
// entry writes nothing at all, not even the terminator.
__declspec(noinline) void stat_strip_control_chars(char *pszDst, const char *pszSrc, int cch)
{
  if (cch == 0)
    return;

  while (*pszSrc != '\0' && cch != 0) {
    unsigned char ch = (unsigned char)*pszSrc;
    if (ch >= 0x20 || ch == '\n' || ch == '\r' || ch == '\t') {
      *pszDst++ = ch;
      cch--;
    }
    pszSrc++;
  }

  *pszDst = '\0';
}

// ---------------------------------------------------------------------------
// Storm's hash-table template, instantiated for the map index. The oracle keeps
// each member as its own COMDAT; the link walks are TSList.h's idiom.

// The RTTI type descriptor for HASHENTRY at 0x51087C, the owner tag
// InternalDelete hands SMemFree.
#ifndef OC_GTYPEINFO_HASHENTRY
#define OC_GTYPEINFO_HASHENTRY
#ifdef OC_DLL_MODE
#define gTypeInfo_HASHENTRY (*reinterpret_cast<type_info *>(OC_VA(0x51087Cu)))
#else
extern type_info gTypeInfo_HASHENTRY;
#endif
#endif

// 0x45B6C0. Rounds nCount down to a power of two, at least 1; a count of 21
// or more instead pins the chunk size at 21.
ULONG TSGrowableArray<TSExplicitList<HASHENTRY,3722304989> >::CalcChunkSize(
    ULONG nCount)
{
  if (nCount < 0x15) {
    while ((nCount & (nCount - 1)) != 0)
      nCount &= nCount - 1;
    return nCount < 1 ? 1 : nCount;
  }

  nChunkSize = 0x15;
  return 0x15;
}

// 0x45C790. Empties the table and releases the bucket array.
void TSHashTable<HASHENTRY,HASHKEY_CONSTSTRI>::Destroy()
{
  InternalClear(1);
  nFullness = 0;
  dwHashMask = 0;
  slotArray.Clear();
}

// 0x45CF40. Destroys one node and returns it to Storm.
void TSHashTable<HASHENTRY,HASHKEY_CONSTSTRI>::InternalDelete(HASHENTRY *pEntry)
{
  pEntry->~HASHENTRY();
  SMemFree(pEntry, gTypeInfo_HASHENTRY.raw_name(), -2, 0);
}

// 0x45D0D0. Counts the nodes in bucket nSlot against a decaying fullness
// score and doubles the table when the score passes 13. Returns 1 when it grew.
int TSHashTable<HASHENTRY,HASHKEY_CONSTSTRI>::MonitorFullness(ULONG nSlot)
{
  TSExplicitList<HASHENTRY,3722304989> *pList;
  HASHENTRY *p;

  if (dwHashMask >= 0x7FFFFFFF)
    return 0;

  if (nFullness > 3)
    nFullness -= 3;
  else
    nFullness = 0;

  pList = &slotArray.pData[nSlot];
  for (p = pList->link.next; (intptr_t)p > 0;
       p = (p != NULL ? (TSLink<HASHENTRY> *)((intptr_t)p + pList->nLinkOffset)
                      : &pList->link)->next) {
    if (++nFullness > 0xD) {
      nFullness = 0;
      GrowListArray(dwHashMask * 2 + 2);
      return 1;
    }
  }

  return 0;
}

// 0x45D160. Returns the node whose key matches pszKey, case insensitively, or
// NULL. The bucket is re-derived from the table on every step; the first read
// goes through Head(), which is what keeps `this` in ESI across SStrHash.
HASHENTRY *TSHashTable<HASHENTRY,HASHKEY_CONSTSTRI>::Ptr(const char *pszKey)
{
  DWORD dwHash = SStrHash(pszKey, 0, 0);
  HASHENTRY *p;

  for (p = slotArray.pData[dwHash & dwHashMask].Head(); (intptr_t)p > 0;
       p = ((TSLink<HASHENTRY> *)((intptr_t)p +
            slotArray.pData[dwHash & dwHashMask].nLinkOffset))->next) {
    if (p->dwHash == dwHash &&
        (p->pszKey == pszKey || SStrCmpI(p->pszKey, pszKey, 0x7FFFFFFF) == 0))
      return p;
  }

  return NULL;
}

// 0x45D040. Allocates a node in the bucket dwHash selects, growing the table
// first when that bucket is too full, and links it at the tail of the list of
// all nodes. The list's `prev` slots hold link addresses, not node addresses.
HASHENTRY *TSHashTable<HASHENTRY,HASHKEY_CONSTSTRI>::InternalNewNode(
    ULONG dwHash, ULONG nExtra, ULONG dwFlags)
{
  ULONG dwKey = dwHash;
  ULONG nSlot = dwKey & dwHashMask;
  HASHENTRY *pEntry;
  TSLink<HASHENTRY> *pLink;
  TSLink<HASHENTRY> *pPrev;
  TSLink<HASHENTRY> *pTail;

  if (MonitorFullness(nSlot))
    nSlot = dwKey & dwHashMask;

  TSExplicitList<HASHENTRY,3722304989> *pList = &slotArray.pData[nSlot];
  pEntry = InternalNew(pList, nExtra, dwFlags);
  pLink = pEntry != NULL
              ? (TSLink<HASHENTRY> *)((intptr_t)pEntry + fullList.nLinkOffset)
              : &fullList.link;

  pPrev = (TSLink<HASHENTRY> *)pLink->prev;
  if (pPrev != NULL) {
    HASHENTRY *pNext = pLink->next;
    TSLink<HASHENTRY> *pSlot;

    if ((intptr_t)pNext <= 0) {
      pSlot = (TSLink<HASHENTRY> *)~(intptr_t)pNext;
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pNext > 0)
        nOff = (intptr_t)pLink - (intptr_t)pPrev->next;
      pSlot = (TSLink<HASHENTRY> *)((intptr_t)pNext + nOff);
    }

    pSlot->prev = (HASHENTRY *)pPrev;
    ((TSLink<HASHENTRY> *)pLink->prev)->next = pLink->next;
    pLink->prev = NULL;
    pLink->next = NULL;
  }

  pTail = (TSLink<HASHENTRY> *)fullList.link.prev;
  pLink->prev = (HASHENTRY *)pTail;
  pLink->next = pTail->next;
  pTail->next = pEntry;
  fullList.link.prev = (HASHENTRY *)pLink;
  return pEntry;
}

// 0x45CEB0. Empties every bucket, deleting each node or, with bUnlinkOnly,
// only unlinking it. A node whose link has no predecessor is re-read forever;
// the oracle does the same.
void TSHashTable<HASHENTRY,HASHKEY_CONSTSTRI>::InternalClear(int bUnlinkOnly)
{
  ULONG i;
  HASHENTRY *p;

  nFullness = 0;
  fullList.UnlinkAll();
  for (i = 0; i < slotArray.nCount; i++) {
    while ((p = slotArray.pData[i].Head()) != NULL) {
      if (bUnlinkOnly) {
        TSLink<HASHENTRY> *pLink =
            (TSLink<HASHENTRY> *)((intptr_t)p + slotArray.pData[i].nLinkOffset);
        TSLink<HASHENTRY> *pPrev = (TSLink<HASHENTRY> *)pLink->prev;

        if (pPrev != NULL) {
          HASHENTRY *pNext = pLink->next;
          TSLink<HASHENTRY> *pSlot;

          if ((intptr_t)pNext <= 0) {
            pSlot = (TSLink<HASHENTRY> *)~(intptr_t)pNext;
          } else {
            intptr_t nOff = 0;
            if ((intptr_t)pNext > 0)
              nOff = (intptr_t)pLink - (intptr_t)((TSLink<HASHENTRY> *)pLink->prev)->next;
            pSlot = (TSLink<HASHENTRY> *)((intptr_t)pNext + nOff);
          }

          pSlot->prev = pLink->prev;
          ((TSLink<HASHENTRY> *)pLink->prev)->next = pLink->next;
          pLink->prev = NULL;
          pLink->next = NULL;
        }
      } else {
        InternalDelete(p);
      }
    }
  }
}

// Unlinks one hash-node link from whichever list holds it; TSList.h's idiom
// with the predecessor held in a local.
static inline void map_hash_unlink(TSLink<HASHENTRY> *pLink)
{
  TSLink<HASHENTRY> *pPrev = (TSLink<HASHENTRY> *)pLink->prev;

  if (pPrev != NULL) {
    HASHENTRY *pNext = pLink->next;
    TSLink<HASHENTRY> *pSlot;

    if ((intptr_t)pNext <= 0) {
      pSlot = (TSLink<HASHENTRY> *)~(intptr_t)pNext;
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pNext > 0)
        nOff = (intptr_t)pLink - (intptr_t)pPrev->next;
      pSlot = (TSLink<HASHENTRY> *)((intptr_t)pNext + nOff);
    }

    pSlot->prev = (HASHENTRY *)pPrev;
    ((TSLink<HASHENTRY> *)pLink->prev)->next = pLink->next;
    pLink->prev = NULL;
    pLink->next = NULL;
  }
}

// 0x45DF70. Unlinks every node, freeing none. A node whose link has no
// predecessor is revisited forever; the oracle does the same.
void TSList<HASHENTRY,TSGetExplicitLink<HASHENTRY> >::UnlinkAll(void)
{
  HASHENTRY *p;

  while ((p = Head()) != NULL)
    map_hash_unlink((TSLink<HASHENTRY> *)((intptr_t)p + nLinkOffset));
}

// 0x45AF30. Takes the node off the all-nodes list, then off its bucket.
TSHashObject<HASHENTRY,HASHKEY_CONSTSTRI>::~TSHashObject<HASHENTRY,HASHKEY_CONSTSTRI>()
{
  map_hash_unlink(&linkFull);
  map_hash_unlink(&linkSlot);
}

// The RTTI type descriptor for the bucket class at 0x510894, the owner tag
// the bucket array is freed under.
#ifndef OC_GTYPEINFO_TSEXPLICITLIST
#define OC_GTYPEINFO_TSEXPLICITLIST
#ifdef OC_DLL_MODE
#define gTypeInfo_TSExplicitList_HASHENTRY (*reinterpret_cast<type_info *>(OC_VA(0x510894u)))
#else
extern type_info gTypeInfo_TSExplicitList_HASHENTRY;
#endif
#endif

// 0x45AEA0. Destroys every bucket in place, then frees the array.
void TSFixedArray<TSExplicitList<HASHENTRY,3722304989> >::Destructor()
{
  ULONG i;

  for (i = 0; i < nCount; i++) {
    TSExplicitList<HASHENTRY,3722304989> *pList = &pData[i];
    pList->UnlinkAll();
    map_hash_unlink(&pList->link);
  }

  if (pData != NULL)
    SMemFree(pData, gTypeInfo_TSExplicitList_HASHENTRY.raw_name(), -2, 0);
}

// Primes the bucket's terminator to point at itself and stores the explicit
// marker as its link offset.
inline TSExplicitList<HASHENTRY,3722304989>::TSExplicitList()
{
  TSLink<HASHENTRY> *pLink = &link;

  pLink->prev = NULL;
  pLink->next = NULL;
  link.prev = (HASHENTRY *)&link;
  nLinkOffset = (int)3722304989;
  link.next = (HASHENTRY *)~(intptr_t)&link;
}

// A moved bucket keeps the source's link offset but none of its nodes.
inline TSExplicitList<HASHENTRY,3722304989>::TSExplicitList(const TSExplicitList &src)
{
  TSLink<HASHENTRY> *pLink = &link;

  pLink->prev = NULL;
  pLink->next = NULL;
  nLinkOffset = src.nLinkOffset;
  link.prev = (HASHENTRY *)&link;
  link.next = (HASHENTRY *)~(intptr_t)&link;
}

// Storm ordinal 405 (thunk 0x4D01C0, slot 0x4EB394); five arguments, from the
// one call site's pushes.
extern "C" {
void * __stdcall SMemReAlloc(void *pMem, DWORD cbSize, const char *pszTag,
                             int nLine, DWORD dwFlags);
}

// 0x45DB90. Resizes the allocation to nNewMax buckets. Buckets past the new
// end are destroyed first; when Storm cannot resize in place, a fresh block is
// allocated and the surviving buckets are moved into it.
void TSFixedArray<TSExplicitList<HASHENTRY,3722304989> >::ReallocData(
    ULONG nNewMax)
{
  TSExplicitList<HASHENTRY,3722304989> *pOld = pData;
  ULONG i;

  for (i = nNewMax; i < nCount; i++) {
    TSExplicitList<HASHENTRY,3722304989> *pList = &pOld[i];
    pList->UnlinkAll();
    map_hash_unlink(&pList->link);
  }

  nMaxCount = nNewMax;
  pData = (TSExplicitList<HASHENTRY,3722304989> *)SMemReAlloc(
      pOld, nNewMax * sizeof(TSExplicitList<HASHENTRY,3722304989>),
      gTypeInfo_TSExplicitList_HASHENTRY.raw_name(), -2, 0x10);
  if (pData == NULL) {
    pData = (TSExplicitList<HASHENTRY,3722304989> *)SMemAlloc(
        nNewMax * sizeof(TSExplicitList<HASHENTRY,3722304989>),
        gTypeInfo_TSExplicitList_HASHENTRY.raw_name(), -2, 0);
    if (pOld != NULL) {
      ULONG nKeep = nNewMax < nCount ? nNewMax : nCount;

      for (i = 0; i < nKeep; i++) {
        TSExplicitList<HASHENTRY,3722304989> *pList = &pOld[i];
        new (&pData[i]) TSExplicitList<HASHENTRY,3722304989>(*pList);
        pList->UnlinkAll();
        map_hash_unlink(&pList->link);
      }

      SMemFree(pOld, gTypeInfo_TSExplicitList_HASHENTRY.raw_name(), -2, 0);
    }
  }
}

// 0x45DE70. Resizes the bucket array to nNewCount, growing the allocation by
// whole chunks and constructing or destroying the buckets in between.
void TSGrowableArray<TSExplicitList<HASHENTRY,3722304989> >::SetCount(
    ULONG nNewCount)
{
  ULONG i;

  if (nNewCount > nCount) {
    if (nNewCount > nMaxCount) {
      ULONG nChunk = nChunkSize;
      ULONG nRem;
      ULONG nAlloc;

      if (nChunk == 0)
        nChunk = CalcChunkSize(nNewCount);
      nRem = nNewCount % nChunk;
      nAlloc = nNewCount;
      if (nRem != 0)
        nAlloc = nChunk - nRem + nNewCount;
      ReallocData(nAlloc);
    }
    for (i = nCount; i < nNewCount; i++)
      new (&pData[i]) TSExplicitList<HASHENTRY,3722304989>();
  } else {
    for (i = nNewCount; i < nCount; i++) {
      TSExplicitList<HASHENTRY,3722304989> *pList = &pData[i];
      pList->UnlinkAll();
      map_hash_unlink(&pList->link);
    }
  }
  nCount = nNewCount;
}

// The terminator's own unlink runs after the nodes are released.
inline TSList<HASHENTRY,TSGetExplicitLink<HASHENTRY> >::~TSList()
{
  UnlinkAll();
  map_hash_unlink(&link);
}

inline void TSExplicitList<HASHENTRY,3722304989>::ChangeLinkOffset(int nOffset)
{
  if (nLinkOffset != nOffset) {
    UnlinkAll();
    nLinkOffset = nOffset;
    link.prev = (HASHENTRY *)&link;
    link.next = (HASHENTRY *)~(intptr_t)&link;
  }
}

// The constructor and destructor emit our vtable, whose InternalNew slot is the
// unmatched body below. The payload link repoints every vptr store at the
// oracle's table 0x4EDBE0 (abs_globals.py, from its registry entry), so objects
// keep the oracle's vtable whichever side built them, and nothing reaches our
// table or InternalNew. gen hooks the constructor and the deleting destructor
// by their decorated symbols.
inline void TSHashTable<HASHENTRY,HASHKEY_CONSTSTRI>::operator delete(void *p)
{
  SMemFree(p, "delete", -1, 0);
}

// 0x45ADE0. Starts with four buckets keyed at link offset 4 and the all-nodes
// list keyed at 0Ch.
TSHashTable<HASHENTRY,HASHKEY_CONSTSTRI>::TSHashTable()
{
  ULONG i;

  nFullness = 0;
  fullList.ChangeLinkOffset(offsetof(HASHENTRY, linkFull));  // Hyperion: the link's offset, 0x0C in 1.17
  dwHashMask = 3;
  slotArray.SetCount(4);
  for (i = 0; i <= dwHashMask; i++)
    slotArray.pData[i].ChangeLinkOffset(offsetof(HASHENTRY, linkSlot));  // Hyperion: the link's offset, 4 in 1.17
}

// Reached only through the scalar deleting destructor at 0x45AFA0.
TSHashTable<HASHENTRY,HASHKEY_CONSTSTRI>::~TSHashTable<HASHENTRY,HASHKEY_CONSTSTRI>()
{
  InternalClear(1);
}

// 0x45CF70. Allocates and constructs one node with nExtra trailing bytes and
// links it at the head of pList.
HASHENTRY *TSHashTable<HASHENTRY,HASHKEY_CONSTSTRI>::InternalNew(
    TSExplicitList<HASHENTRY,3722304989> *pList, ULONG nExtra,
    ULONG dwFlags)
{
  void *pMem = SMemAlloc(nExtra + sizeof(HASHENTRY),
                         gTypeInfo_HASHENTRY.raw_name(), -2, dwFlags | 8);
  HASHENTRY *pEntry = new (pMem) HASHENTRY;
  TSLink<HASHENTRY> *pLink =
      pEntry != NULL ? (TSLink<HASHENTRY> *)((intptr_t)pEntry + pList->nLinkOffset)
                     : &pList->link;
  TSLink<HASHENTRY> *pHead;
  HASHENTRY *pFirst;
  TSLink<HASHENTRY> *pSlot;
  intptr_t nOff;

  {
    TSLink<HASHENTRY> *pPrev = (TSLink<HASHENTRY> *)pLink->prev;

    if (pPrev != NULL) {
      HASHENTRY *pNext = pLink->next;

      if ((intptr_t)pNext <= 0) {
        pSlot = (TSLink<HASHENTRY> *)~(intptr_t)pNext;
      } else {
        nOff = 0;
        if ((intptr_t)pNext > 0)
          nOff = (intptr_t)pLink - (intptr_t)pPrev->next;
        pSlot = (TSLink<HASHENTRY> *)((intptr_t)pNext + nOff);
      }

      pSlot->prev = (HASHENTRY *)pPrev;
      ((TSLink<HASHENTRY> *)pLink->prev)->next = pLink->next;
      pLink->prev = NULL;
      pLink->next = NULL;
    }
  }

  pHead = &pList->link;
  pLink->prev = (HASHENTRY *)pHead;
  pLink->next = pHead->next;
  // Merged arms under pEntry != NULL, which the link ternary already tests; they
  // compile to one load and give pHead the oracle's EDI over pList.
  if (pEntry != NULL) { pFirst = pHead->next; } else { pFirst = pHead->next; }
  nOff = pList->nLinkOffset;
  if ((intptr_t)pFirst <= 0) {
    pSlot = (TSLink<HASHENTRY> *)~(intptr_t)pFirst;
  } else {
    if (nOff < 0)
      nOff = (intptr_t)pHead - (intptr_t)((TSLink<HASHENTRY> *)pHead->prev)->next;
    pSlot = (TSLink<HASHENTRY> *)((intptr_t)pFirst + nOff);
  }
  pSlot->prev = (HASHENTRY *)pLink;
  pHead->next = pEntry;
  return pEntry;
}

// Reads a list head through its terminator link.
static __forceinline HASHENTRY *link_head(TSLink<HASHENTRY> *pTerm)
{
  return (intptr_t)pTerm->next > 0 ? pTerm->next : NULL;
}

// 0x45CC80. Rehashes into nNewSize buckets: every node is parked on a local
// list, the bucket array is resized and re-keyed, and each node is linked at
// the tail of the bucket its hash now selects. The bucket heads are read
// through a terminator pointer; that is what gives the oracle's registers.
__declspec(noinline) void TSHashTable<HASHENTRY,HASHKEY_CONSTSTRI>::GrowListArray(ULONG nNewSize)
{
  ULONG nOldSize = dwHashMask + 1;
  TSExplicitList<HASHENTRY,3722304989> parked;
  ULONG i;
  HASHENTRY *p;

  parked.ChangeLinkOffset(offsetof(HASHENTRY, linkSlot));  // Hyperion: the link's offset, 4 in 1.17
  for (i = 0; i < nOldSize; i++) {
    while ((p = link_head(&slotArray.pData[i].link)) != NULL) {
      TSLink<HASHENTRY> *pLink = (TSLink<HASHENTRY> *)((intptr_t)p + parked.nLinkOffset);
      TSLink<HASHENTRY> *pTail;

      map_hash_unlink(pLink);
      pTail = (TSLink<HASHENTRY> *)parked.link.prev;
      pLink->prev = (HASHENTRY *)pTail;
      pLink->next = pTail->next;
      pTail->next = p;
      parked.link.prev = (HASHENTRY *)pLink;
    }
  }

  slotArray.SetCount(nNewSize);
  // Re-keyed by hand: the oracle inlines UnlinkAll here and writes the terminator first.
  for (i = 0; i < nNewSize; i++) {
    TSExplicitList<HASHENTRY,3722304989> *pList = &slotArray.pData[i];
    if (pList->nLinkOffset != offsetof(HASHENTRY, linkSlot)) {  // Hyperion: 4 in 1.17
      TSLink<HASHENTRY> *pTerm = &pList->link;
      HASHENTRY *q;

      while ((q = link_head(pTerm)) != NULL)
        map_hash_unlink((TSLink<HASHENTRY> *)((intptr_t)q + pList->nLinkOffset));
      pTerm->prev = (HASHENTRY *)pTerm;
      pList->link.next = (HASHENTRY *)~(intptr_t)pTerm;
      pList->nLinkOffset = offsetof(HASHENTRY, linkSlot);  // Hyperion: the link's offset, 4 in 1.17
    }
  }
  dwHashMask = nNewSize - 1;

  while ((p = parked.Head()) != NULL) {
    TSExplicitList<HASHENTRY,3722304989> *pDest = &slotArray.pData[p->dwHash & dwHashMask];
    TSLink<HASHENTRY> *pLink = (TSLink<HASHENTRY> *)((intptr_t)p + pDest->nLinkOffset);
    TSLink<HASHENTRY> *pTail;

    map_hash_unlink(pLink);
    TSLink<HASHENTRY> *pTerm = &pDest->link;
    pTail = (TSLink<HASHENTRY> *)pTerm->prev;
    pLink->prev = (HASHENTRY *)pTail;
    pLink->next = pTail->next;
    pTail->next = p;
    pTerm->prev = (HASHENTRY *)pLink;
  }
}

typedef TSList<LISTENTRY,TSGetLink<LISTENTRY> > LISTENTRYLIST;

static void map_list_unlink(LISTENTRY *p)
{
  LISTENTRY *pPrev = p->link.prev;

  if (pPrev != NULL) {
    LISTENTRY *pNext = p->link.next;
    TSLink<LISTENTRY> *pSlot;

    if ((intptr_t)pNext <= 0) {
      pSlot = (TSLink<LISTENTRY> *)~(intptr_t)pNext;
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pNext > 0)
        nOff = (intptr_t)&p->link - (intptr_t)pPrev->link.next;
      pSlot = (TSLink<LISTENTRY> *)((intptr_t)pNext + nOff);
    }
    pSlot->prev = pPrev;
    p->link.prev->link.next = p->link.next;
    p->link.prev = NULL;
    p->link.next = NULL;
  }
}

// 0x45B7F0. Unlinks and frees every map-list entry.
void LISTENTRYLIST::Clear(void)
{
  for (;;) {
    LISTENTRY *p = link.next;
    if ((intptr_t)p <= 0)
      p = NULL;
    if (!p)
      break;
    map_list_unlink(p);
    SMemFree(p, gTypeInfo_LISTENTRY.raw_name(), -2, 0);
  }
}
