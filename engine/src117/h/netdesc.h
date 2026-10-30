// netdesc.h -- declarations for src117/lang/netdesc.cpp: the SNet program and
// player descriptors, and the text form of a game description that goes into
// the game list.
//
// Free functions are __fastcall by /Gr's default, so their declarations do not
// spell it.

#ifndef OC117_NETDESC_H
#define OC117_NETDESC_H

#include "platform.h"

// The descriptor SNetInitializeProvider is given. Size 0x3C from the two
// memsets, and every field below is a displacement InitSNetProgramData writes
// with a `mov dword ptr`.
struct SNETPROGRAMDATA {
  /*0x00*/ DWORD dwSize;              // `mov dword ptr [esi], 3Ch`
  /*0x04*/ const char *pszName;       // "Starcraft" / "Brood War"
  /*0x08*/ const char *pszVersion;    // 0x508620, "internal version unknown"
  /*0x0C*/ DWORD dwProgramId;         // 'STAR' / 'SEXP'
  /*0x10*/ DWORD dwVersionId;         // 0xD5
  /*0x14*/ DWORD dwUnwritten14;
  /*0x18*/ DWORD dwMaxPlayers;        // 8
  /*0x1C*/ DWORD dwUnwritten1C[3];
  /*0x28*/ DWORD dwField28;           // 0xFF
  /*0x2C*/ DWORD_PTR dwKeyLo;         // RetrieveKey's first out-parameter
  /*0x30*/ DWORD_PTR dwKeyHi;         // its second
  /*0x34*/ DWORD dwIsSpawn;           // IsSpawnedCopy's result, stored 32-bit
  /*0x38*/ DWORD dwLangId;            // GetLocale's result, stored 32-bit
};

static_assert(offsetof(SNETPROGRAMDATA, dwSize)      == 0x00, "SNETPROGRAMDATA::dwSize       InitSNetProgramData: mov dword ptr [esi], 3Ch");
OC_X86_LAYOUT(offsetof(SNETPROGRAMDATA, pszName)     == 0x04, "SNETPROGRAMDATA::pszName      InitSNetProgramData: mov dword ptr [esi+4h], 4ECD60h");
OC_X86_LAYOUT(offsetof(SNETPROGRAMDATA, pszVersion)  == 0x08, "SNETPROGRAMDATA::pszVersion   InitSNetProgramData: mov dword ptr [esi+8h], 508620h");
OC_X86_LAYOUT(offsetof(SNETPROGRAMDATA, dwProgramId) == 0x0C, "SNETPROGRAMDATA::dwProgramId  InitSNetProgramData: mov dword ptr [esi+0Ch], 53544152h");
OC_X86_LAYOUT(offsetof(SNETPROGRAMDATA, dwVersionId) == 0x10, "SNETPROGRAMDATA::dwVersionId  InitSNetProgramData: mov dword ptr [esi+10h], 0D5h");
OC_X86_LAYOUT(offsetof(SNETPROGRAMDATA, dwUnwritten14) == 0x14, "SNETPROGRAMDATA::dwUnwritten14 never written; the memset clears it. Pinned so a later type change cannot shift dwMaxPlayers");
OC_X86_LAYOUT(offsetof(SNETPROGRAMDATA, dwMaxPlayers) == 0x18, "SNETPROGRAMDATA::dwMaxPlayers InitSNetProgramData: mov dword ptr [esi+18h], 8h");
OC_X86_LAYOUT(offsetof(SNETPROGRAMDATA, dwUnwritten1C) == 0x1C, "SNETPROGRAMDATA::dwUnwritten1C never written; the memset clears it. Its extent is what puts dwField28 at 28h");
OC_X86_LAYOUT(offsetof(SNETPROGRAMDATA, dwField28)   == 0x28, "SNETPROGRAMDATA::dwField28    InitSNetProgramData: mov dword ptr [esi+28h], 0FFh");
OC_X86_LAYOUT(offsetof(SNETPROGRAMDATA, dwKeyLo)     == 0x2C, "SNETPROGRAMDATA::dwKeyLo      DestroySNetProgramData: mov ecx, dword ptr [esi+2Ch]");
OC_X86_LAYOUT(offsetof(SNETPROGRAMDATA, dwKeyHi)     == 0x30, "SNETPROGRAMDATA::dwKeyHi      DestroySNetProgramData: mov edx, dword ptr [esi+30h]");
OC_X86_LAYOUT(offsetof(SNETPROGRAMDATA, dwIsSpawn)   == 0x34, "SNETPROGRAMDATA::dwIsSpawn    InitSNetProgramData: mov dword ptr [esi+34h], eax");
OC_X86_LAYOUT(offsetof(SNETPROGRAMDATA, dwLangId)    == 0x38, "SNETPROGRAMDATA::dwLangId     InitSNetProgramData: mov dword ptr [esi+38h], eax");
OC_X86_LAYOUT(sizeof(SNETPROGRAMDATA)                == 0x3C, "SNETPROGRAMDATA               memset length 3Ch in both bodies");

// The player descriptor. Size 0x10 from the single `movdqu` that zeroes it.
struct SNETPLAYERDATA {
  /*0x00*/ DWORD dwSize;              // `mov dword ptr [ecx], 10h`
  /*0x04*/ char *pszName;             // gLocalPlayerName
  /*0x08*/ const char *pszDescription; // 0x4EC14E, the empty string
  /*0x0C*/ DWORD dwUnwritten0C;
};

static_assert(offsetof(SNETPLAYERDATA, dwSize)        == 0x00, "SNETPLAYERDATA::dwSize        InitSNetPlayerData: mov dword ptr [ecx], 10h");
OC_X86_LAYOUT(offsetof(SNETPLAYERDATA, pszName)       == 0x04, "SNETPLAYERDATA::pszName       InitSNetPlayerData: mov dword ptr [ecx+4h], 672690h");
OC_X86_LAYOUT(offsetof(SNETPLAYERDATA, pszDescription) == 0x08, "SNETPLAYERDATA::pszDescription InitSNetPlayerData: mov dword ptr [ecx+8h], 4EC14Eh");
OC_X86_LAYOUT(offsetof(SNETPLAYERDATA, dwUnwritten0C) == 0x0C, "SNETPLAYERDATA::dwUnwritten0C never written; the movdqu clears it. Pinned so the 10h size stays honest");
OC_X86_LAYOUT(sizeof(SNETPLAYERDATA)                  == 0x10, "SNETPLAYERDATA                movdqu xmmword ptr [ecx] zeroes 16 bytes");

// The game description that travels as the comma-separated hex text in a game
// list entry. GameData2Txt writes it, Txt2GameData reads it back and sub_46F8B0
// range-checks the result; between them they name every field. The two
// dimensions are stored as (nibble * 32).
struct gamedesc_t {
  /*0x00*/ DWORD dwGameId;
  /*0x04*/ WORD wMapWidth;        // <= 0x100
  /*0x06*/ WORD wMapHeight;       // <= 0x100
  /*0x08*/ BYTE bActivePlayers;   // <= bMaxPlayers, packed with it as one nibble pair
  /*0x09*/ BYTE bMaxPlayers;      // <= 8
  /*0x0A*/ BYTE bGameSpeed;       // <= 6
  /*0x0B*/ BYTE bApproval;        // < 7
  /*0x0C*/ BYTE bGameType;
  /*0x0D*/ BYTE bGameSubType;
  /*0x0E*/ WORD wField0E;
  /*0x10*/ DWORD dwField10;
  /*0x14*/ WORD wTemplate;        // < 8
  /*0x16*/ BYTE bField16;
  /*0x17*/ BYTE bUnwritten17;
  /*0x18*/ char szGameName[0x19]; // String2Txt's 19h
  /*0x31*/ char szMapName[0x20];  // String2Txt's 20h
};

static_assert(offsetof(gamedesc_t, dwGameId)       == 0x00, "gamedesc_t::dwGameId       GameData2Txt: mov esi, dword ptr [eax]");
static_assert(offsetof(gamedesc_t, wMapWidth)      == 0x04, "gamedesc_t::wMapWidth      sub_46F8B0: cmp word ptr [esi+4h], ax");
static_assert(offsetof(gamedesc_t, wMapHeight)     == 0x06, "gamedesc_t::wMapHeight     sub_46F8B0: cmp word ptr [esi+6h], ax");
static_assert(offsetof(gamedesc_t, bActivePlayers) == 0x08, "gamedesc_t::bActivePlayers sub_46F8B0: cmp byte ptr [esi+8h], al");
static_assert(offsetof(gamedesc_t, bMaxPlayers)    == 0x09, "gamedesc_t::bMaxPlayers    sub_46F8B0: mov al, byte ptr [esi+9h]");
static_assert(offsetof(gamedesc_t, bGameSpeed)     == 0x0A, "gamedesc_t::bGameSpeed     sub_46F8B0: cmp byte ptr [esi+0Ah], 6h");
static_assert(offsetof(gamedesc_t, bApproval)      == 0x0B, "gamedesc_t::bApproval      sub_46F8B0: cmp byte ptr [esi+0Bh], 7h");
static_assert(offsetof(gamedesc_t, bGameType)      == 0x0C, "gamedesc_t::bGameType      GameData2Txt: movzx ecx, byte ptr [eax+0Ch]");
static_assert(offsetof(gamedesc_t, bGameSubType)   == 0x0D, "gamedesc_t::bGameSubType   GameData2Txt: movzx ecx, byte ptr [eax+0Dh]");
static_assert(offsetof(gamedesc_t, wField0E)       == 0x0E, "gamedesc_t::wField0E       GameData2Txt: movzx ecx, word ptr [eax+0Eh]");
static_assert(offsetof(gamedesc_t, dwField10)      == 0x10, "gamedesc_t::dwField10      GameData2Txt: mov eax, dword ptr [eax+10h]");
static_assert(offsetof(gamedesc_t, wTemplate)      == 0x14, "gamedesc_t::wTemplate      sub_46F8B0: cmp word ptr [esi+14h], 8h");
static_assert(offsetof(gamedesc_t, bField16)       == 0x16, "gamedesc_t::bField16       GameData2Txt: movzx eax, byte ptr [eax+16h]");
static_assert(offsetof(gamedesc_t, bUnwritten17)   == 0x17, "gamedesc_t::bUnwritten17   neither GameData2Txt nor Txt2GameData touches it. Pinned so szGameName stays at 18h");
static_assert(offsetof(gamedesc_t, szGameName)     == 0x18, "gamedesc_t::szGameName     GameData2Txt: lea ecx, dword ptr [ebx+18h] with 19h");
static_assert(offsetof(gamedesc_t, szMapName)      == 0x31, "gamedesc_t::szMapName      GameData2Txt: lea ecx, dword ptr [ebx+31h] with 20h");
// 0x51 is the last byte the oracle names; 0x54 is that rounded up to the
// struct's own 4-byte alignment, and nothing in the image allocates one, so the
// three trailing bytes are unobserved.
static_assert(sizeof(gamedesc_t)                   == 0x54, "gamedesc_t                 0x31 + 0x20 rounded to alignment 4");

// One ECX argument. Releases the CD key pair and then zeroes the whole record;
// EAX at the `ret` is memset's leftover.
void DestroySNetProgramData(SNETPROGRAMDATA *pData);

// One ECX argument, dereferenced. EAX is never written on a path to the `ret`.
void InitSNetPlayerData(SNETPLAYERDATA *pData);

// One ECX argument. EAX at the `ret` is GetLocale's leftover.
void InitSNetProgramData(SNETPROGRAMDATA *pData);

// Nullary; the network heartbeat. The one consuming caller uses the full EAX,
// and every path materialises 0 or a `setz` result.
int sub_46F3C0();

// Nullary; restamps the heartbeat clock and tail-jmps SendTurns, which is void.
void sub_46F460();

// ECX the description, EDX the output buffer, one stack dword: the buffer's
// capacity, which the body decrements to a remaining count before handing it to
// String2Txt. `ret 4h`. EAX is String2Txt's leftover.
void GameData2Txt(gamedesc_t *pData, char *pszBuf, int cchBuf);

// ECX the source, EDX the destination, then the running remaining count and a
// capacity the body never reads. `ret 8h`; EAX is the destination pointer one
// past the terminator. The __declspec(noinline) belongs on this declaration:
// without it /Ob2 splices both calls into GameData2Txt.
__declspec(noinline) char *String2Txt(const char *pszSrc, char *pszDest,
                                      int *pcchLeft, int cchMax);

// ECX the source text, which goes straight to get_valid_str_len; EDX the record
// that every field store indexes. All five callers use EAX 32 bits wide.
BOOL Txt2GameData(const char *pszText, gamedesc_t *pData);

// One ECX argument. Range-checks a parsed description; the `sbb`/`neg` tail
// makes every path 0 or 1.
BOOL sub_46F8B0(gamedesc_t *pData);

// ECX the out-parameter, EDX the text, one stack dword: the value to store when
// the field is absent or empty. `ret 4h`; ten consuming callers use the full
// EAX, which is the text pointer past the comma, or NULL.
char *Txt2Number(int *pnOut, char *pszTxt, int nDefault);

// 0x463820 cleanup_key (91 B) and 0x4635D0 RetrieveKey (306 B), both key.cpp
// rows no header of ours declares; ECX and EDX are real inputs on each and
// neither writes EAX on a path to its `ret`. RetrieveKey's two arguments are
// the out-parameters DestroySNetProgramData later hands cleanup_key.
void cleanup_key(DWORD_PTR dwKeyLo, DWORD_PTR dwKeyHi);
void RetrieveKey(DWORD_PTR *pdwKeyLo, DWORD_PTR *pdwKeyHi);

// 0x4CB750 GetLocale (89 B). InitSNetProgramData stores the result with a full
// `mov dword ptr [esi+38h], eax`, so it is int here, though one of the three
// callers elsewhere narrows it to 16 bits.
int GetLocale();

#endif  // OC117_NETDESC_H
