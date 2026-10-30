#pragma once
// Storm.h -- the Storm.dll entry points the game calls. Each is declared from
// Blizzard's own import table: the ordinal is the one its IAT slot is bound to, the
// name comes from the approved sheet sc-refs/sheets/storm_library.csv (storm.dll
// exports them [NONAME]), and the arity is the call sites' push count with no
// caller-side cleanup, so __stdcall. The import library is src117/storm_imports.def.
//
// It declares only what the game's C++ needs, not Storm's whole header. Global
// operator new and delete are not declared here: the CRT's <new> declares them and
// src117/lang/Storm.cpp defines them.

// NOGDI: wingdi.h declares LineTo(HDC, int, int), which with the game's own LineTo
// (0x4C4650, dlg_draw.h) makes an overload set whose address cannot be taken
// without a cast. The game uses no GDI. Names that collide with core <windows.h>
// (OpenFile, GetFileSize) cannot be suppressed and need a typed cast instead.
#define NOGDI
#include <windows.h>

// VC12's <ctype.h>, reached through <windows.h>, defines _toupper and _tolower as
// unconditional macros, and the game calls the functions. dlg_ctrl.h undefines them
// too, but it can be included before this header, so they are undefined here.
#ifdef _toupper
#undef _toupper
#endif
#ifdef _tolower
#undef _tolower
#endif
#if defined(__EMSCRIPTEN__) || defined(__APPLE__)
#define _toupper _toupper_msvc  // compiler.h: the C library has no function of the name
#endif

// Storm's exports are __stdcall with C linkage (`_SMemAlloc@16`).
#define STORMAPI __stdcall

typedef struct _HSTRANS *HSTRANS;      // Storm's transparency-mask handle, opaque

extern "C" {

// Storm.dll ordinal 401. ownerTag and ownerLine are Storm's allocation-owner tag;
// the game passes a bare .rdata string and the literal -1 rather than
// __FILE__/__LINE__. Fail-fast: no caller null-checks the result.
LPVOID STORMAPI SMemAlloc(DWORD size, LPCSTR ownerTag, int ownerLine,
                          DWORD flags);

// Storm.dll ordinal 403. Returns a 32-bit status the game's operator delete
// discards.
BOOL STORMAPI SMemFree(LPCVOID ptr, LPCSTR ownerTag, int ownerLine,
                       DWORD flags);

// Storm.dll ordinal 501. Three pushes: the destination, the source, the
// destination's capacity. Every caller discards the result; `char *` is Storm's
// convention.
char * STORMAPI SStrCopy(char *pszDest, const char *pszSrc, size_t cchDest);

// Storm.dll ordinal 503, three arguments like SStrCopy.
char * STORMAPI SStrPack(char *pszDest, const char *pszSrc, size_t cchDest);

// Storm.dll ordinal 508, three arguments. sub_451830 (gluChatSlot.cpp) tests the
// result, so it is not void; int is the comparison-result convention.
int STORMAPI SStrCmp(const char *psz1, const char *psz2, size_t cchMax);

// Storm.dll ordinal 502, thunk 0x4D01C6. sub_4634E0 pushes three dwords.
int STORMAPI SStrHash(const char *pszStr, DWORD dwFlags, DWORD dwSeed);

// Storm.dll ordinal 491, thunk 0x4D0250. recv_net_cmd_swap_slots calls it four
// times with three pushes each, the length being the 0x24-byte gPlayerData record.
// Every caller discards EAX.
void STORMAPI SMemCopy(void *pDest, const void *pSrc, size_t cbLen);

// Storm.dll ordinal 506, thunk 0x4D0040. One argument: the `push 0h` before it in
// recv_net_cmd_SaveGame is is_bad_filename's third argument, hoisted. The result
// goes whole into is_bad_filename's EDX, so it is a DWORD.
DWORD STORMAPI SStrLen(const char *pszStr);


// Storm.dll ordinal 442. No __declspec(dllimport): the game reaches it with a
// `call rel32` onto the thunk at 0x4D007C, which a dllimport spelling would turn
// into a direct `call dword ptr [IAT]`. Its caller is console_collision
// (cursor.cpp), which pushes the mask, x and y.
int STORMAPI STransIsPixelInMask(HSTRANS hTrans, int nX, int nY);

// The SNet family, for net_misc.cpp, net_glue.cpp and the network screens. Every
// call site tests EAX whole.
//
// SNetDestroy is spelled exactly as src117/lang/net_player.cpp declares it
// locally, so the two are one name rather than an overload.

// Ordinal 102, thunk 0x4D0280 -> [0x4EB414]. Nullary; NetworkShutdown calls it with
// no pushes.
BOOL __stdcall SNetDestroy();

// Ordinal 119, thunk 0x4D02AA -> [0x4EB430]. One push; app_cleanup and vappcritf
// both pass a literal 3.
int STORMAPI SNetLeaveGame(int nType);

// Ordinal 468, thunk 0x4D0376 -> [0x4EB4B8]. One push, a literal 1 at all three
// call sites (vappcritf, app_cleanup, app_exit).
BOOL STORMAPI SErrSuppressErrors(BOOL bSuppress);  // Hyperion: BOOL, as port_stormerr.cpp

// Ordinal 107, thunk 0x4D0190 -> [0x4EB374]. Four pushes at both call sites,
// sub_476EF0 and sub_477860.
int STORMAPI SNetGetGameInfo(int nInfoId, void *pvBuffer, int nSize,
                             int *pnBytesRead);

// Ordinal 115, thunk 0x4D0298 -> [0x4EB424]. One push, SendTurns' turn-count
// out-parameter.
int STORMAPI SNetGetTurnsInTransit(DWORD *pdwTurns);

// Ordinal 139, thunk 0x4D02B6 -> [0x4EB438]. Five pushes in net_end_game:
// 0, the player count 8, the eight results and two text buffers.
BOOL STORMAPI SNetReportGameResult(DWORD dwUnused, int nPlayers, int *pnResults,
                                   const char *pszHeader, const char *pszDetail);

// Ordinal 144, thunk 0x4D02B0 -> [0x4EB434]. One push, a 0x20-byte SMemAlloc
// block that net_end_game hands on as eight name pointers.
BOOL STORMAPI SNetGetPlayerNames(char **ppszNames);

// Ordinal 130, thunk 0x4D02BC -> [0x4EB43C]. Two pushes.
int STORMAPI SNetSetGameMode(DWORD dwFlags, int nUnused);

// Ordinal 106, thunk 0x4D029E -> [0x4EB428]. Two pushes; net_drop_player
// passes the player id it was given in ECX and its own stack word.
int STORMAPI SNetDropPlayer(int nPlayerId, DWORD dwFlags);

// Ordinal 109, thunk 0x4D02A4 -> [0x4EB42C]. Three pushes, all out-parameters on
// get_num_active_players' stack.
int STORMAPI SNetGetNumPlayers(int *pnFirst, int *pnLast, int *pnActive);

// Ordinal 120, thunk 0x4D028C -> [0x4EB41C]. One push, handle_upgrade's
// out-parameter.
int STORMAPI SNetPerformUpgrade(int *pnUpgradeStatus);

// Ordinal 116, thunk 0x4D01D8. Five arguments, from glumodem_init_device's one
// call: arg0 = [eax+8], arg1 = EDX, arg2 = [ebp+8], arg3 = a 0x5C-byte buffer whose
// first dword is its own size, arg4 = 0x66CCA8. glumodem_init_device returns its
// EAX.
int STORMAPI SNetInitializeDevice(DWORD dwDeviceId, DWORD dwCaps, DWORD_PTR dwArg2,
                                  void *pProgram, void *pProvider);


// The SReg pair, for gluCreat.cpp's saved dropdown settings: SRegLoadString is
// ordinal 422 (thunk 0x4D0136), SRegSaveString ordinal 425 (thunk 0x4D013C).
// SRegLoadString takes a key, a value name, flags and a buffer with its
// size, and its BOOL is tested; SRegSaveString takes a key, a value name, flags and
// the string, and its result is discarded.
BOOL __stdcall SRegLoadString(const char *pszKey, const char *pszValue,
                              DWORD dwFlags, char *pszBuffer, DWORD cchBuffer);
BOOL __stdcall SRegSaveString(const char *pszKey, const char *pszValue,
                              DWORD dwFlags, const char *pszString);

// The blob half of the same registry pair, plus the delete, added for
// src117/lang/BNetGW.cpp. Ordinals from sc-refs/sheets/storm_library.csv
// (421 SRegLoadData, 424 SRegSaveData, 428 SRegDeleteValue, 494 SMemZero).
// Arity is the oracle's: BNGatewayAccess::GetGatewayList pushes six dwords
// before SRegLoadData and reads the last as an out length, Unload pushes five
// before SRegSaveData, and Load pushes three before SRegDeleteValue.
BOOL __stdcall SRegLoadData(const char *pszKey, const char *pszValue,
                            DWORD dwFlags, void *pvBuffer, DWORD cbBuffer,
                            DWORD *pcbRead);
BOOL __stdcall SRegSaveData(const char *pszKey, const char *pszValue,
                            DWORD dwFlags, const void *pvData, DWORD cbData);
BOOL __stdcall SRegDeleteValue(const char *pszKey, const char *pszValue,
                               DWORD dwFlags);
// DWORD, not size_t: src117/lang/maphdr.cpp declares this name with DWORD, and two
// C-linkage spellings of one name do not compile.
void __stdcall SMemZero(void *pvBuffer, DWORD cbBuffer);

// Ordinal 279. BNGatewayAccess::WriteDefaultGatewayList pushes five dwords and
// reads the buffer back out of the second, so arg1 is an out pointer and arg2 the
// in/out length the caller zeroes first.
BOOL __stdcall SFileLoadFile(const char *pszFileName, void **ppvBuffer,
                             DWORD *pcbBuffer, DWORD dwFlags, void *pvUnused);


// The SVid trio, for gluBrfPort.cpp's portrait videos: ordinals 454, 457 and 458
// (thunks 0x4D017E, 0x4D0184, 0x4D018A). SVidPlayBegin takes a path, a dword, two
// rectangles, a palette, flags and the handle's slot; every SVidPlayEnd caller
// clears that slot afterwards.
// Hyperion: dwArg4 is a palette pointer to the port's SVidPlayBegin, and the
// game passes 0; pointer-wide for the x64 build.
BOOL STORMAPI SVidPlayBegin(const char *pszFilename, DWORD_PTR dwArg1, void *pArg2,
                            void *pArg3, DWORD_PTR dwArg4, DWORD dwFlags,
                            HANDLE *phVid);
BOOL STORMAPI SVidPlayContinueSingle(HANDLE hVid, DWORD dwArg1, DWORD *pdwArg2);
BOOL STORMAPI SVidPlayEnd(HANDLE hVid);
// SVidGetSize: ordinal 452, thunk 0x4D0334, one call site (flc_init_flc) with four
// pushes. The two out parameters are read back as the low word of a dword slot.
// Hyperion: dwFlags is where the port's SVidGetSize writes the depth, and the
// game passes 0; pointer-wide for the x64 build.
BOOL __stdcall SVidGetSize(HANDLE hVid, DWORD *pdwWidth, DWORD *pdwHeight,
                           DWORD_PTR dwFlags);


// SNetEnumProviders: ordinal 105, thunk 0x4D01AE, for gluConn.cpp's provider list.
// One call site, gluConn_dlg_activate, which pushes 0 and 0x4548C0, the per-provider
// callback TPROVIDER::Create. EAX is discarded.
// Hyperion: dwArg0 is a capabilities filter pointer to the port's
// SNetEnumProviders, and the game passes 0; pointer-wide for the x64 build.
BOOL STORMAPI SNetEnumProviders(DWORD_PTR dwArg0, void *pfnCallback);


// SNetEnumGamesEx: ordinal 133, thunk 0x4D01D2, for net_glue.cpp. ListGames pushes
// &local, 475380h, 0 and 0 and tests EAX. 0x475380 is that TU's per-game callback
// and the fourth argument is a 4-byte local the call fills.
BOOL STORMAPI SNetEnumGamesEx(DWORD dwArg0, DWORD dwArg1, void *pfnCallback,
                              DWORD *pdwArg3);

// SNetEnumDevices: ordinal 103, slot 0x4EB39C (thunk 0x4D01CC), for gluModem.cpp.
// One call site, enum_devices, which pushes 0x45E6B0, glumodem_create_device, the
// per-device callback; EAX is discarded.
BOOL STORMAPI SNetEnumDevices(void *pfnCallback);


// The rest of the SNet session family, for net_glue.cpp.

// Ordinal 114, slot 0x4EB4D8. One argument, a caps block whose first dword the
// caller sets to 0x24, its own size. Both sites are `lea eax, [ebp-N] /
// mov dword ptr [ebp-N], 24h / push eax / call`, then `test eax, eax`.
BOOL STORMAPI SNetGetProviderCaps(void *pCaps);

// Ordinal 117, slot 0x4EB418. Five arguments, not six: NetInit's `push esi`
// ahead of the block is the register save its `pop esi` after the call
// restores, and counting it as an argument leaks 4 bytes of stack per call.
BOOL STORMAPI SNetInitializeProvider(DWORD dwProvider, void *pProgramData,
                                     void *pPlayerData, void *pUIData,
                                     void *pArg4);

// Ordinal 118, slot 0x4EB3A8. Six arguments, both sites agreeing. The last is
// an out slot the call fills; net_glue_OnInitData passes 0x508D58 and reads it
// back. The string arguments are 0x4EC14E, which is "".
BOOL STORMAPI SNetJoinGame(DWORD dwArg0, const char *pszArg1,
                           const char *pszArg2, void *pArg3,
                           const char *pszArg4, DWORD *pdwPlayerId);

// Ordinal 101, slot 0x4EB40C. Ten arguments, one call site, so the arity is a
// lower bound. Last is the same out slot as SNetJoinGame's.
// Hyperion: dwArg4 is the game data's pointer to the port's SNetCreateGame,
// and the game passes 0; pointer-wide for the x64 build.
BOOL STORMAPI SNetCreateGame(const char *pszGameName, const char *pszPassword,
                             const char *pszGameStat, DWORD dwArg3,
                             DWORD_PTR dwArg4, DWORD dwArg5, DWORD dwArg6,
                             void *pArg7, const char *pszArg8,
                             DWORD *pdwPlayerId);

// Ordinal 138, slot 0x4EB410. Twelve arguments, one call site, so the arity is
// a lower bound. CreateGame_MultiPlayer's own `push ebx` / `push esi` saves are
// earlier in the body; the ebx and esi inside the block are values.
BOOL STORMAPI SNetCreateLadderGame(const char *pszGameName, DWORD_PTR dwArg1,
                                   void *pArg2, DWORD dwArg3, DWORD dwArg4,
                                   DWORD dwArg5, void *pArg6, DWORD dwArg7,
                                   DWORD dwArg8, void *pArg9,
                                   const char *pszArg10, DWORD *pdwPlayerId);

// Ordinal 123, slot 0x4EB420. Two arguments, four agreeing call sites in
// NetInit: an event id (1..4) and the handler address.
BOOL STORMAPI SNetRegisterEventHandler(DWORD dwEvent, void *pfnHandler);

// Ordinal 463, slot 0x4EB510. gamedata.cpp and net_cmds.cpp declare it the same
// way locally.
DWORD STORMAPI SErrGetLastError();

// Ordinal 137, slot 0x4EB440, for src117/lang/net_time.cpp. One call site,
// sub_47A630, which pushes a single flags dword (0x40000006) with no
// caller-side cleanup. No caller consumes the result.
int STORMAPI SNetDisconnectAll(DWORD dwFlags);

// Ordinal 112, slot 0x4EB380 (thunk 0x4D01A2), for src117/lang/gluChatSlot.cpp.
// One call site, sub_452A00: `push &caps / push [slot+50h] / call`, no
// caller-side cleanup, then `test eax, eax`. The caps block's first dword is
// its own size, 0x24, as with SNetGetProviderCaps.
BOOL STORMAPI SNetGetPlayerCaps(DWORD dwPlayerId, void *pCaps);

// Ordinal 141, slot 0x4EB384 (thunk 0x4D01A8), for src117/lang/gluChatSlot.cpp.
// One call site, sub_452AB0: `push 451830h / push &buf / call`, no caller-side
// cleanup. The second argument is sub_451830, a callback taking the reply.
BOOL STORMAPI SNetSendLeagueCommand(const char *pszCommand, void *pfnCallback);

// Ordinal 134, slot 0x4EB3F0 (thunk 0x4D024A). One push and no caller-side
// cleanup: sub_470150 and net_chrm.cpp's chat-room path send a line with it.
BOOL STORMAPI SNetSendServerChatCommand(const char *pszCommand);

// Ordinal 143, slot 0x4EB3AC (thunk 0x4D01E4); the sheet calls the ordinal
// SNetGetLeagueId. One call site, sub_461B80: `push &n / call`, no caller-side
// cleanup, and the result is not read.
BOOL STORMAPI SNetGetLeagueName(int *pnLeague);

// Ordinal 578, slot 0x4EB530 (thunk 0x4D005E), `SStrPrintf @578` in
// storm_imports.def. Varargs, so each caller cleans up with `add esp`.
// League.cpp and gluChatSlot.cpp still carry the same declaration locally.
int __cdecl SStrPrintf(char *pszDest, size_t cchDest, const char *pszFormat, ...);


// The two blit entries, for gluBNIcon.cpp's icon strip: SBltROP3Clipped is ordinal
// 314 (thunk 0x4D0118), SDlgBltToWindowE 221 (0x4D011E). SBltROP3Clipped takes ten
// dwords, the last always SRCCOPY (0xCC0020); SDlgBltToWindowE takes seven. EAX is
// discarded everywhere. The other arguments have no names and are spelled by width.
BOOL STORMAPI SBltROP3Clipped(DWORD_PTR dwArg0, DWORD_PTR dwArg1, DWORD_PTR dwArg2,
                              DWORD dwArg3, DWORD_PTR dwArg4, void *pArg5,
                              void *pArg6, DWORD dwArg7, DWORD dwArg8,
                              DWORD dwRop);
BOOL STORMAPI SDlgBltToWindowE(DWORD_PTR dwArg0, DWORD dwArg1, DWORD dwArg2,
                               DWORD_PTR dwArg3, void *pArg4, void *pArg5,
                               DWORD dwArg6);

// SBmpDecodeImage: ordinal 321, thunk 0x4D01B4. Nine pushes at all six call sites.
// The first argument is a small kind (err_dlg_init and err_load_img pass 2); the
// others have no names and are spelled by width. EAX is discarded everywhere.
// Hyperion: dwArg8 is where the port's SBmpDecodeImage writes the depth, and
// the game passes 0; pointer-wide for the x64 build.
BOOL STORMAPI SBmpDecodeImage(DWORD dwKind, void *pSrc, DWORD cbSrc,
                              void *pPalette, void *pDest, DWORD cbDest,
                              DWORD_PTR dwArg6, DWORD_PTR dwArg7, DWORD_PTR dwArg8);

// SFileGetInstallPath: ordinal 295, thunk 0x4D01BA. The sheet names ordinal 295
// SFileRegisterLoadNotifyProc; this name is the one init.cpp's callers use, and the
// call's shape, (buffer, MAX_PATH, 1), is a path getter's.
BOOL STORMAPI SFileGetInstallPath(char *pszBuffer, DWORD cchBuffer,
                                  DWORD dwUnused);

}  // extern "C"

// Storm's error callback: the message, a second string, the files to attach
// and their count, the source line, and whether the error is fatal. Imported
// by name, not by ordinal: the slot 0x4EB4B4 carries the decorated C++ name,
// which fixes the parameter list and the void return.
typedef void (__fastcall *SERRBLIZZERRFUNC)(const char *pszMessage, const char *pszDetail,
                                            const char *apszFiles[], unsigned int nFiles,
                                            unsigned int nLine, bool bFatal);

// thunk 0x4D0370 -> [0x4EB4B4], by name
// ?SErrSetBlizzardErrorFunction@@YIXP6IXPBD0QAPBDII_N@Z@Z. C++ linkage and
// __fastcall, which is what that decoration spells.
void __fastcall SErrSetBlizzardErrorFunction(SERRBLIZZERRFUNC pfnFunc);
