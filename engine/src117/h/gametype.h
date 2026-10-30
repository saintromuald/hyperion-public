// gametype.h -- the game header gGameHeader (at 0x6728D0, 141 bytes) and the
// game-type predicates that read it, the template list of selectable game types,
// and the rest of src117/lang/gametype.cpp's declarations.
//
// The predicates read what look like separate byte globals (IsMeleeGame's
// 0x672945) but are fields of gGameHeader: IsOOOGame reads the same byte as
// [header+0x75] through a pointer. The struct's name is Blizzard's; the field
// names are descriptive, from what the reading functions do with each field.
#ifndef OC117_GAMETYPE_H
#define OC117_GAMETYPE_H

#include "platform.h"
#include "TSList.h"

// Packed: the WORD at +0x6F and the DWORD at +0x71 sit at odd offsets.
#pragma pack(push, 1)
struct GameHeader {
  /*0x00*/ DWORD dwSaveReserved;       // write_player_data zeroes it before the copy
  /*0x04*/ BYTE _unmapped_04[0x1C - 0x04];
  /*0x1C*/ DWORD dwSaveId;             // write_player_data's ECX; sub_4995E0 matches it
  /*0x20*/ BYTE _unmapped_20[0x24 - 0x20];
  /*0x24*/ BYTE bActivePlayers;        // write_player_data: get_num_active_players()
  /*0x25*/ BYTE bSlotCount;            // the same call again
  /*0x26*/ BYTE _unmapped_26;
  // 0 unauthenticated, 1 Blizzard, 2 ladder, 3 PGT, 4 KBK, 5 tournament, 6
  // WGT-authenticated: one value behind the seven Is*Map predicates.
  /*0x27*/ BYTE bMapAuthentication;
  /*0x28*/ BYTE _unmapped_28[0x33 - 0x28];
  /*0x33*/ BYTE bType1Players;         // write_player_data: gPlayerData[].nType == 1
  /*0x34*/ BYTE _unmapped_34[0x6D - 0x34];
  // IsTVBGame (0x0F), IsWGTGame (0x11..0x16), IsTournamentGame (0x80); a
  // different axis from bGameSubCategory.
  /*0x6D*/ BYTE bGameType;
  /*0x6E*/ BYTE bGameTypeSub;          // IsTournamentGame's second test
  /*0x6F*/ WORD wSubType;              // gametype_get_subtype
  /*0x71*/ DWORD dwSubTypeValue;       // gametype_get_subtype_value
  // 1 melee, 3 greed, 4 CTF, 5 sudden death, 6 slaughter, 7 OOO (the Is*Game
  // predicates).
  /*0x75*/ BYTE bGameSubCategory;
  // gametype_init_resources switches on it: 0 zeroes the four resource tables,
  // 1 seeds minerals from dwStartingMinerals below.
  /*0x76*/ BYTE bResourceMode;
  /*0x77*/ BYTE _unmapped_77[0x79 - 0x77];
  /*0x79*/ BYTE bScenarioFlag;         // IsScenarioGame's second test
  /*0x7A*/ BYTE _unmapped_7A;
  /*0x7B*/ BYTE bComputersAllowed;     // ComputersAllowed: 1 or 3
  /*0x7C*/ BYTE bAlliesAllowed;        // AlliesAllowed, CanChangeVision
  /*0x7D*/ BYTE bNumTeams;            // GetNumTeams returns it, IsTeamGame tests
                                      // it, GetMaxPlayersPerTeam switches on it
  /*0x7E*/ BYTE bCheatsAllowed;        // CheatsAllowed
  // A flag: IsLadderGame tests it against zero. AlliesAllowed and IsScenarioGame
  // also gate on it.
  /*0x7F*/ BYTE bLadderGame;
  /*0x80*/ BYTE _unmapped_80[0x84 - 0x80];
  /*0x84*/ DWORD dwStartingMinerals;   // gametype_init_resources, resource mode 1
  /*0x88*/ BYTE _unmapped_88[141 - 0x88];
};
#pragma pack(pop)

// 141 bytes: every copy of it is a `rep movsd` of 35 dwords and a `movsb`, and
// the 32-byte TGAMEPARAMS snet_initdata_cb copies to +0x6D ends at 141.
static_assert(sizeof(GameHeader) == 141, "rep movsd(140) + movsb(1)");
static_assert(offsetof(GameHeader, dwSaveReserved) == 0x00, "0x6728D0");
static_assert(offsetof(GameHeader, dwSaveId) == 0x1C, "0x6728EC");
static_assert(offsetof(GameHeader, bActivePlayers) == 0x24, "0x6728F4");
static_assert(offsetof(GameHeader, bSlotCount) == 0x25, "0x6728F5");
static_assert(offsetof(GameHeader, bType1Players) == 0x33, "0x672903");
static_assert(offsetof(GameHeader, bMapAuthentication) == 0x27, "0x6728F7");
static_assert(offsetof(GameHeader, bGameType) == 0x6D, "0x67293D");
static_assert(offsetof(GameHeader, bGameTypeSub) == 0x6E, "0x67293E");
static_assert(offsetof(GameHeader, wSubType) == 0x6F, "0x67293F");
static_assert(offsetof(GameHeader, dwSubTypeValue) == 0x71, "0x672941");
static_assert(offsetof(GameHeader, bGameSubCategory) == 0x75, "0x672945");
static_assert(offsetof(GameHeader, bScenarioFlag) == 0x79, "0x672949");
static_assert(offsetof(GameHeader, bComputersAllowed) == 0x7B, "0x67294B");
static_assert(offsetof(GameHeader, bAlliesAllowed) == 0x7C, "0x67294C");
static_assert(offsetof(GameHeader, bNumTeams) == 0x7D, "0x67294D");
static_assert(offsetof(GameHeader, bCheatsAllowed) == 0x7E, "0x67294E");
static_assert(offsetof(GameHeader, bLadderGame) == 0x7F, "0x67294F");
static_assert(offsetof(GameHeader, bResourceMode) == 0x76, "0x672946");
static_assert(offsetof(GameHeader, dwStartingMinerals) == 0x84, "0x672954");

// gGameHeader is declared in gametype.cpp, not in gen/globals117.h, which would
// put the struct in front of every TU.

// The gametype predicates. All read gGameHeader and return an int truth value.
int __fastcall IsUnauthenticatedMap(void);
int __fastcall IsBlizzardMap(void);
int __fastcall IsLadderMap(void);
int __fastcall IsPGTMap(void);
int __fastcall IsKBKMap(void);
int __fastcall IsTournamentMap(void);
int __fastcall IsWGTAuthenticatedMap(void);

int __fastcall IsMeleeGame(void);
int __fastcall IsGreedGame(void);
int __fastcall IsCTFGame(void);
int __fastcall IsSuddenDeathGame(void);
int __fastcall IsSlaughterGame(void);
int __fastcall IsTVBGame(void);
int __fastcall IsWGTGame(void);
// 0x447E40. An unsigned range test on bGameType - 0x11.
int __fastcall sub_447E40(void);
int __fastcall IsLadderGame(void);

int __fastcall CheatsAllowed(void);
int __fastcall AlliesAllowed(void);
int __fastcall ComputersAllowed(void);
int __fastcall IsTournamentGame(void);

int __fastcall gametype_get_subtype(void);
DWORD __fastcall gametype_get_subtype_value(void);

// A NULL pHeader means &gGameHeader; the null test is in the callee, not at the
// call sites.
int __fastcall IsOOOGame(const GameHeader *pHeader);
int __fastcall IsScenarioGame(const GameHeader *pHeader);
int __fastcall IsCompetitiveGame(BYTE bGameType, BYTE bGameTypeSub);

// A {BYTE, BYTE, WORD} record packed into one DWORD (w<<16 | b1<<8 | b0). No
// source names it, so it keeps 1.17's sub_ name.
DWORD __fastcall sub_4476D0(const BYTE *p);

// 0x4480A0. Whether the record's keyed template exists and its player cap
// (+0x10), when set, admits the record's bMaxPlayers.
struct gamedesc_t;
int sub_4480A0(const gamedesc_t *pData);

// The template list: a Storm TSList of 400-byte TTEMPLATE records, one per
// selectable game type, apart from gGameHeader. CreateTemplate allocates each
// (`push 190h`) and copies in a name (+0x08), a parameter name (+0x28), the
// 32-byte TGAMEPARAMS key (+0x48) and a 296-byte TTEMPLATEMETA (+0x68: a map
// path, a display name and a dword), which fill the 400 bytes.

#pragma pack(push, 1)

// The 296-byte record CreateTemplate receives by pointer and copies wholesale.
struct TTEMPLATEMETA {
  /*0x000*/ char  szPath[0x104];   // MAX_PATH -- the SStrCopy capacity
  /*0x104*/ char  szName[0x20];
  /*0x124*/ DWORD dwValue;
};

// The 32-byte key at TTEMPLATE+0x48, copied and compared as a unit (sub_447A70
// compares eight dwords). Only the fields a body reads are named.
struct TGAMEPARAMS {
  /*0x00*/ BYTE  bCategory;        // compared by every find and Is* predicate
  /*0x01*/ BYTE  bSub;
  /*0x02*/ WORD  wType;            // IterateMainTemplates keeps type 1
  /*0x04*/ DWORD dwSubValue;       // gametype_get_subtype_name
  /*0x08*/ BYTE  bMelee;           // IsMeleeTemplate: == 1
  // +0x09..+0x1A: UiLeagueCallback fills each from the league record at its own
  // width.
  /*0x09*/ BYTE  bOpt09;
  /*0x0A*/ BYTE  bOpt0A;
  /*0x0B*/ BYTE  bOpt0B;
  /*0x0C*/ BYTE  bFlag0C;          // IsSimpleTemplate: must be 0
  /*0x0D*/ BYTE  bOpt0D;
  /*0x0E*/ BYTE  bOpt0E;
  /*0x0F*/ BYTE  bOpt0F;
  /*0x10*/ BYTE  bOpt10;
  /*0x11*/ BYTE  bOpt11;
  /*0x12*/ BYTE  bFlag12;          // IsSimpleTemplate: must be 0
  /*0x13*/ DWORD dwOpt13;
  /*0x17*/ DWORD dwOpt17;
  /*0x1B*/ BYTE  _unmapped_1B[5];
};

struct TTEMPLATE {
  /*0x000*/ TSLink<TTEMPLATE> link;
  /*0x008*/ char szName[0x20];
  /*0x028*/ char szParam[0x20];
  /*0x048*/ TGAMEPARAMS params;
  /*0x068*/ TTEMPLATEMETA meta;
};

#pragma pack(pop)

// The typedefs let offsetof name the TSList instantiations: the comma in
// `TSList<T, TSGetLink<T> >` would split the macro's arguments. These are the
// asserts TSList.h defers to.
typedef TSLink<TTEMPLATE> TTEMPLATELINK;

// The list as an explicit specialization, which declares its Clear member.
#pragma pack(push, 1)
template <> struct TSList<TTEMPLATE,TSGetLink<TTEMPLATE> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<TTEMPLATE> link;
  // 0x447650 (84 B). Unlinks and frees every template.
  void Clear(void);
};
#pragma pack(pop)
typedef TSList<TTEMPLATE, TSGetLink<TTEMPLATE> > TTEMPLATELIST;

OC_X86_LAYOUT(sizeof(TTEMPLATELINK) == 8, "two pointers, node-embedded");
static_assert(offsetof(TTEMPLATELINK, prev) == 0x00, "Clear reads [node]");
OC_X86_LAYOUT(offsetof(TTEMPLATELINK, next) == 0x04, "every walk reads [p+4]");
OC_X86_LAYOUT(sizeof(TTEMPLATELIST) == 12, "0x5033C4 .. next name 0x5033D0");
static_assert(offsetof(TTEMPLATELIST, _unmapped_00) == 0x00, "TSList.h layout");
static_assert(offsetof(TTEMPLATELIST, link) == 4, "Clear reads [this+8]");

static_assert(sizeof(TTEMPLATEMETA) == 0x128, "0x104 + 0x20 + 4");
static_assert(offsetof(TTEMPLATEMETA, szPath) == 0x000, "find_..._meta_path -> +0x68");
static_assert(offsetof(TTEMPLATEMETA, szName) == 0x104, "SStrCopy node+0x16C");
static_assert(offsetof(TTEMPLATEMETA, dwValue) == 0x124, "mov [node+0x18C]");

static_assert(sizeof(TGAMEPARAMS) == 0x20, "two movdqu == 32 B");
static_assert(offsetof(TGAMEPARAMS, bCategory) == 0x00, "cmp [p+48h], cl");
static_assert(offsetof(TGAMEPARAMS, bSub) == 0x01, "cmp [p+49h], dl");
static_assert(offsetof(TGAMEPARAMS, wType) == 0x02, "cmp word [p+4Ah], cx");
static_assert(offsetof(TGAMEPARAMS, dwSubValue) == 0x04, "push dword ptr [ecx+4h]");
static_assert(offsetof(TGAMEPARAMS, bMelee) == 0x08, "IsMeleeTemplate [q+8]");
static_assert(offsetof(TGAMEPARAMS, bFlag0C) == 0x0C, "IsSimpleTemplate [q+0Ch]");
static_assert(offsetof(TGAMEPARAMS, bFlag12) == 0x12, "IsSimpleTemplate [q+12h]");
static_assert(offsetof(TGAMEPARAMS, bOpt09) == 0x09, "UiLeagueCallback [ebp-1Fh]");
static_assert(offsetof(TGAMEPARAMS, bOpt0A) == 0x0A, "UiLeagueCallback [ebp-1Eh]");
static_assert(offsetof(TGAMEPARAMS, bOpt0B) == 0x0B, "UiLeagueCallback [ebp-1Dh]");
static_assert(offsetof(TGAMEPARAMS, bOpt0D) == 0x0D, "UiLeagueCallback [ebp-1Bh]");
static_assert(offsetof(TGAMEPARAMS, bOpt0E) == 0x0E, "UiLeagueCallback [ebp-1Ah]");
static_assert(offsetof(TGAMEPARAMS, bOpt0F) == 0x0F, "UiLeagueCallback [ebp-19h]");
static_assert(offsetof(TGAMEPARAMS, bOpt10) == 0x10, "UiLeagueCallback [ebp-18h]");
static_assert(offsetof(TGAMEPARAMS, bOpt11) == 0x11, "UiLeagueCallback [ebp-17h]");
static_assert(offsetof(TGAMEPARAMS, dwOpt13) == 0x13, "UiLeagueCallback [ebp-15h] dword");
static_assert(offsetof(TGAMEPARAMS, dwOpt17) == 0x17, "UiLeagueCallback [ebp-11h] dword");

OC_X86_LAYOUT(sizeof(TTEMPLATE) == 0x190, "CreateTemplate's push 190h");
static_assert(offsetof(TTEMPLATE, link) == 0x000, "SMemFree(node) frees the link");
OC_X86_LAYOUT(offsetof(TTEMPLATE, szName) == 0x008, "find_template_name -> +8");
OC_X86_LAYOUT(offsetof(TTEMPLATE, szParam) == 0x028, "find_param_name -> +0x28");
OC_X86_LAYOUT(offsetof(TTEMPLATE, params) == 0x048, "find_template -> +0x48");
OC_X86_LAYOUT(offsetof(TTEMPLATE, meta) == 0x068, "find_template_meta -> +0x68");

// The callback both Iterate* functions call: __stdcall, four arguments.
// CreateTemplate has this signature and reads pMeta at +0x104 and +0x124, so
// the fourth is a pointer; the Iterate* call sites pass NULL.
typedef void(__stdcall *TEMPLATEPROC)(const char *pszName, const char *pszParam,
                                      const TGAMEPARAMS *pParams,
                                      const TTEMPLATEMETA *pMeta);

// sgTemplateList is declared in gametype.cpp, for the same reason as
// gGameHeader.

// The walks. Each reads sgTemplateList and stops on the signed test TSList.h
// explains; `p != NULL` is not the same test.
char *__fastcall gametype_find_template_name(BYTE bCategory, BYTE bSub);
char *__fastcall gametype_find_param_name(BYTE bCategory, BYTE bSub);
// 0x447C60 / 0x447C30: the first template of a category, as its map path
// (node+0x68) or its display name (node+0x16C); NULL when there is none.
char *__fastcall gametype_find_template_meta_path(BYTE bCategory);
char *__fastcall gametype_find_template_meta_name(BYTE bCategory);
void __fastcall IterateMainTemplates(TEMPLATEPROC pfn);
void __fastcall IterateLeagueTemplates(BYTE bCategory, BYTE bSub,
                                       TEMPLATEPROC pfn);

// Reads no list: gPlayback, gPlayerData[].nType and two gGameHeader bytes.
int __fastcall CanChangeVision(int nPlayerA, int nPlayerB);

// 0x447D30. Two bytes in ECX and EDX and a 16-bit stack argument (`ret 4h`).
int __fastcall IsMeleeTemplate(BYTE bCategory, BYTE bSub, WORD wType);

// The leaves.
DWORD __fastcall sub_4476F0(const BYTE *p);
int __fastcall NextListEntry(char **ppCursor, DWORD *pdwRemaining, char *pszOut,
                             DWORD dwCapacity);
int __fastcall gametype_template_allowed(const BYTE *p, int nId);
int __fastcall gametype_load_trigger_file(const char *pszFile);
void __fastcall sub_448210(int nPlayer);
void __fastcall sub_4480F0(void);
// 0x448000, `ret 4h`. NetInit registers it as Storm event 1; no direct caller.
struct SNETEVENT;
void __stdcall snet_initdata_cb(SNETEVENT *pEvent);

// The list's lifetime, and the factory CreateGametypes drives: CreateTemplate
// is a TEMPLATEPROC.
int __fastcall AllocGametypes(void);
void __stdcall CreateTemplate(const char *pszName, const char *pszParam,
                              const TGAMEPARAMS *pParams,
                              const TTEMPLATEMETA *pMeta);
// 1.17's one caller, gluConn_dlg_activate, clears CL before the call, though
// the body overwrites ECX at once; the parameter keeps that call's code.
void __fastcall gametype_exit(BYTE bArg0);
TGAMEPARAMS *__fastcall gametype_load_ums(void);
void __fastcall gametype_clear_leagues(void);
void __fastcall gametype_run_aiscript(void);

// 0x448420, nullary.
void gametype_init_resources();
// Hyperion: and a mutator's start resources over them, from game_init.
void gametype_mutator_resources();
// 0x448530, 294 B.
BOOL gametype_load_triggers();
// 0x447A70. The name of the template whose parameters equal *pParams, or
// NULL.
char *__fastcall sub_447A70(const TGAMEPARAMS *pParams);
// 0x447740 / 0x4482A0, called only from this TU: the template-list loader and
// the one-file loader it drives.
int __fastcall CreateGametypes(TEMPLATEPROC pfnCreate);
TGAMEPARAMS *__fastcall gametype_load(const char *pszTemplate, char *pszNameOut,
                                      char *pszParamOut);

// 0x447220. The SNet league callback (SNETUIDATA::pfnLeague), given the record
// buffer and its size on the stack (`ret 8h`).
BOOL __stdcall UiLeagueCallback(const BYTE *pData, DWORD dwSize);

// 0x447500, 254 B. Two bytes in ECX and EDX and one stack dword (`ret 4h`).
// Spelled without the __fastcall its neighbours carry: /Gr makes it the
// default, so both spellings are the same function.
BOOL CanChangeRace(BYTE bArg0, BYTE bArg1, int nArg2);


// 0x4B2BE0, a table read; gametype.cpp and placebox.cpp call it.
BYTE __fastcall team_get_slot_captain(BYTE bTeam);        // 0x4B2BE0  10 B

#endif  // OC117_GAMETYPE_H
