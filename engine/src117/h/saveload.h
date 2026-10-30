// saveload.h -- declarations for src117/lang/saveload.cpp: the saved-game list,
// the per-player tech and upgrade tables, the file and compression helpers the
// pool codecs call, and the save and load layer itself.

#ifndef OC_SAVELOAD_H
#define OC_SAVELOAD_H

// No <windows.h> here: it brings in <ctype.h>, which defines the _toupper macro
// again after dlg_ctrl.h has undefined it, and the generated oracle_thunks.cpp
// includes both.
#include "platform.h"
#include "TSList.h"    // the intrusive list SAVEDGAMELIST instantiates

// TSAVED_GAME_NODE -- the saved-game list node, 0xC8 bytes, which sub_499250
// (the SearchDirectory callback) allocates and fills: the 0xB5-byte save header
// ReadSaveHeader decompresses, at +0x08, and the file's last write time, at
// +0xC0. Its name and struct key are Blizzard's, from the RTTI descriptor at
// 0x5109AC. The write time is two DWORDs rather than a FILETIME so that this
// header needs no <windows.h>; +0xBD..+0xBF is padding.
struct TSAVED_GAME_NODE {
  /*0x00*/ TSLink<TSAVED_GAME_NODE> link;
  /*0x08*/ BYTE  abHeader[0xB5];
  /*0xC0*/ DWORD adwLastWriteTime[2];   // a FILETIME: low dword, then high
};

static_assert(offsetof(TSAVED_GAME_NODE, link) == 0x00,
              "SMemFree(node) frees the link; Clear reads [node] as prev");
OC_X86_LAYOUT(offsetof(TSAVED_GAME_NODE, abHeader) == 0x08,
              "lea edi,[ebx+8] is rep movsd's destination; the callback in "
              "sub_4991B0 is handed lea eax,[esi+8]");
OC_X86_LAYOUT(offsetof(TSAVED_GAME_NODE, adwLastWriteTime) == 0xC0,
              "lea ecx,[ebx+0C0h] takes WIN32_FIND_DATA +0x14/+0x18");
OC_X86_LAYOUT(sizeof(TSAVED_GAME_NODE) == 0xC8,
              "push 0C8h / call SMemAlloc in sub_499250");

// The list as an explicit specialization, which declares its Clear member; the
// members mangle as the implicit instantiation's would. Layout is TSList.h's.
template <> struct TSList<TSAVED_GAME_NODE,TSGetLink<TSAVED_GAME_NODE> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<TSAVED_GAME_NODE> link;
  // 0x4989F0 (84 B). Unlink and free every node.
  void Clear(void);
};

typedef TSList<TSAVED_GAME_NODE, TSGetLink<TSAVED_GAME_NODE> > SAVEDGAMELIST;

static_assert(offsetof(SAVEDGAMELIST, _unmapped_00) == 0x00, "TSList.h layout");
static_assert(offsetof(SAVEDGAMELIST, link) == 0x04, "Clear reads [this+8] as link.next");
OC_X86_LAYOUT(sizeof(SAVEDGAMELIST) == 12, "TSList.h layout");

// The per-player tech and upgrade tables: 0x49A420 (31 B), 0x49A300 (63 B),
// 0x49A470 (38 B), 0x49A370 (67 B). The id picks one of two tables, 1.17's first
// 0x18 techs and 0x2E upgrades or the Brood War ones past them, by an unsigned
// 16-bit compare. The player is a full int, which every body scales whole; the
// getters return the byte they load, and the setters take the value as a byte in
// their one stack slot (`ret 4h`).
BYTE __fastcall get_tech_researched(int nPlayer, WORD wTechId);
void __fastcall set_tech_researched(int nPlayer, WORD wTechId, BYTE bValue);
// set_tech_available -- 0x49A2C0 (54 B), the same shape as set_tech_researched.
void __fastcall set_tech_available(int nPlayer, WORD wTechId, BYTE bValue);
BYTE __fastcall get_upgrade_level(int nPlayer, WORD wUpgradeId);
// get_max_upgrade_level -- 0x49A4A0 (38 B), the same shape as get_upgrade_level;
// both callers use the byte it returns.
// C linkage, so CUnitUpgrade.cpp can declare the same @get_max_upgrade_level@8 with a
// WORD player: CUnit::DispatchUpgrade widens BH into ECX where UpgradeAllowed passes an
// int.
extern "C" BYTE __fastcall get_max_upgrade_level(int nPlayer, WORD wUpgradeId);
BYTE __fastcall get_max_upgrade_level(int nPlayer, WORD wUpgradeId);
void __fastcall set_upgrade_level(int nPlayer, WORD wUpgradeId, BYTE bLevel);
// set_max_upgrade_level -- 0x49A3C0, the same shape as set_upgrade_level; the
// TECS and UPGR handler pairs reach it.
void __fastcall set_max_upgrade_level(int nPlayer, WORD wUpgradeId, BYTE bLevel);


// The file and compression helpers the pool codecs call, all __fastcall with two
// register arguments and one stack argument (`ret 4h`).
//
// pfread -- 0x4CC460 (28 B) / pfwrite -- 0x4CC4A0 (28 B), gds_file.cpp: one
// element of nSize bytes through fread or fwrite, so the result is whether it
// went through, not a byte count.
BOOL __fastcall pfread(void *pBuf, int nSize, void *pFile);
BOOL __fastcall pfwrite(const void *pBuf, int nSize, void *pFile);

// compress -- 0x4137D0 (354 B) / decompress -- 0x413AE0 (407 B), compress.cpp:
// the buffer, its length and the file; 0 on failure. int, not BOOL:
// CThingy::LoadThingyList passes decompress's result on as SMemFree's flags.
int __fastcall compress(const void *pSrc, int nSrcLen, void *pDstFile);
int __fastcall decompress(void *pDst, int nDstLen, void *pSrcFile);

// The save and load layer; the definitions in saveload.cpp use these spellings.

class CUnit;  // save_select / restore-selection take the selection array

// The leaves over the saveload globals at 0x6A5E34..0x6A5E48. sub_499850
// returns the whole DWORD, though both callers use its low byte.
int  __fastcall sub_499850();            // 0x499850 (6 B)
BYTE __fastcall IsALoadedGame();         // 0x499870 (6 B)  mov al,[0x6A5E38]
void __fastcall SetIsNotLoadedGame();    // 0x499880 (8 B)  [0x6A5E38] = 0
void __fastcall close_savefile();        // 0x498A50 (26 B) pfclose + null 0x6A5E34

// The tables' eighth accessor, the same shape as the others.
BYTE __fastcall get_tech_available(int nPlayer, WORD wTechId);  // 0x49A400 (31 B)

// save_select -- 0x49A340 (39 B): copies the 12-slot selection from *ECX to
// 0x6A5E48. RestoreSelection -- 0x49A240 (115 B): its inverse, which drops dead
// and unseen units and selects the rest through sub_4BCE70 and
// status_set_update.
void __fastcall save_select(CUnit **ppSelection);
void __fastcall RestoreSelection();

// GetSaveExtension -- 0x4995A0 (53 B): one of four literals, picked by
// gbIsExpansion and gbMultiPlayerMode, past their leading '*'.
const char * __fastcall GetSaveExtension();

// The save path builders. ECX is the save's name, refused at 0x19 characters or
// more; the path is built from SFileGetUserDataPath.
//   MakeSaveDir -- 0x499E80 (236 B), `ret 8h`.
//   sub_499FF0 -- 0x499FF0 (193 B), the same without bFlag, `ret 4h`.
//   sub_498B30 -- 0x498B30 (151 B): builds the path and returns pfdelete's result.
BOOL __fastcall MakeSaveDir(const char *pszName, char *pszBuf, int nBufLen, BOOL bFlag);
BOOL __fastcall sub_499FF0(const char *pszName, char *pszBuf, int nBufLen);
int  __fastcall sub_498B30(const char *pszName);

// sub_498CD0 -- 0x498CD0 (65 B): opens the path as the save file, then the
// single-player speed clamp and the game loop's re-init. BOOL: it returns 1, or
// pfopen's 0.
BOOL __fastcall sub_498CD0(const char *pszPath);

// The SearchDirectory callbacks: the full path in ECX, the WIN32_FIND_DATAA in
// EDX, and two stack dwords they never read (`ret 8h`).
//   DeleteSave_cb -- 0x498AE0 (79 B): deletes one save.
//   sub_499250 -- 0x499250 (502 B): adds a TSAVED_GAME_NODE for the save, its
//   header read through sub_499750, sorted by CompareFileTime.
BOOL __fastcall DeleteSave_cb(char *pszPath, struct _WIN32_FIND_DATAA *pFind, intptr_t nUnused0, intptr_t nUnused1);
BOOL __fastcall sub_499250(char *pszPath, struct _WIN32_FIND_DATAA *pFind, intptr_t nUnused0, intptr_t nUnused1);

// SearchDirectory -- 0x49AA20 (606 B): ECX the directory buffer, EDX the
// extension, and five stack dwords (`ret 14h`): the callback, three values it
// passes on, and an out dword.
typedef BOOL(__fastcall *SEARCHDIR_CB)(char *, struct _WIN32_FIND_DATAA *, intptr_t, intptr_t);
int __fastcall SearchDirectory(char *pszDir, const char *pszExt, SEARCHDIR_CB pfnItem,
                               int nArg1, int nArg2, intptr_t nArg3, int *pnOut);

// DeleteSave -- 0x498A70 (102 B): the save directory's path through
// MakeSaveDir, then SearchDirectory with DeleteSave_cb.
void __fastcall DeleteSave(const char *pszName);

// sub_4991B0 -- 0x4991B0 (159 B): rebuilds the list at 0x509368 through
// SearchDirectory and sub_499250, hands each node's header (+8) to pfnItem, and
// clears the list again.
void __fastcall sub_4991B0(void (__stdcall *pfnItem)(void *));

// sub_499750 -- 0x499750 (249 B): reads a save's header, past the 0x1A
// sentinel and through ReadSaveHeader, and its display name into pHdr+0x99.
// Returns 0, 1 for a bad magic, 2 for an old format.
BYTE __fastcall sub_499750(char *pszPath, void *pHdr);

// load_game -- 0x498BD0: ECX is never read; EDX is the save's name. Returns
// sub_498CD0's result when the save is found, else 0.
BOOL __fastcall load_game(DWORD dwUnused, const char *pszName);

// sub_499450 -- 0x499450: finds the file named by nArg0 under the directory
// buffer nArg1 (size nArg2), filtered by the __stdcall callback nArg3(path,
// nArg4). Returns the buffer on a hit, else 0. The int spellings are those of
// its caller, fd_user.cpp.
intptr_t __fastcall sub_499450(intptr_t nArg0, intptr_t nArg1, int nArg2, intptr_t nArg3, intptr_t nArg4);

// The SearchDirectory callbacks behind sub_499450 and sub_4995E0. nCtx is the
// caller's context block, nFound an int* set to 1 on the hit that stops the walk.
BOOL __fastcall sub_4994B0(char *pszPath, struct _WIN32_FIND_DATAA *pFind, intptr_t nCtx, intptr_t nFound);
BOOL __fastcall sub_499690(char *pszPath, struct _WIN32_FIND_DATAA *pFind, intptr_t nHdr, intptr_t nFound);

// sub_4995E0 -- 0x4995E0: copies the display name of the save whose header id
// is dwSaveId into pszOut. `ret 4h`.
BOOL __fastcall sub_4995E0(DWORD dwSaveId, char *pszOut, int nOutLen);

int  __fastcall sub_499860();            // 0x499860 (6 B) mov eax,[0x6A5E40]
void __fastcall sub_49A230();            // 0x49A230 (11 B) [0x6A5E3C] = 0

// MakeSavePath -- 0x499F70: the local player's save directory, pszName, and
// "rep" or the save extension. `ret 8h`.
BOOL __fastcall MakeSavePath(const char *pszName, char *pszBuf, int nBufLen, BOOL bReplay);

// RemoveBasePath -- 0x49A130: strips the user-data or install path from
// pszPath, else everything up to the last backslash. nLen is compared unsigned.
void __fastcall RemoveBasePath(char *pszPath, unsigned int nLen);

// sub_49A440 -- 0x49A440: turns the 96 saved unit ids in gSelected back into
// units.
void __fastcall sub_49A440();

// saveload_read_save_data -- 0x499890.
BOOL saveload_read_save_data();

// write_save_header -- 0x49A4D0: the file in ECX, its name in EDX.
BOOL __fastcall write_save_header(void *pFile, const char *pszFilename);

// write_player_data -- 0x49A8E0, `ret 4h`. EDX is never read; the caller loads
// the save's name into it.
BOOL __fastcall write_player_data(DWORD dwSaveId, const char *pszName, void *pFile);

// saveload_write_save_data -- 0x498D20, `ret 4h`: the timestamp in ECX, the
// name in EDX, one stack flag. Returns whether it succeeded.
BOOL __fastcall saveload_write_save_data(DWORD dwTimestamp, const char *pszName, int nFlags);

// ReadSaveHeader -- 0x49A0C0 (101 B): reads the 4-byte tag, requires 0x0069 in
// its high word, and decompresses the 0xB5-byte save header; header dword +4
// lands in 0x6A5E40. pHdr may be NULL, and a local buffer stands in.
BOOL __fastcall ReadSaveHeader(void *pHdr, void *pFile);

#endif  // OC_SAVELOAD_H
