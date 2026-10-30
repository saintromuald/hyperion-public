// TrigAct.cpp -- the trigger action family (oracle 0x4B5620..0x4B8E70). A
// trigger carries an array of 64 32-byte action records, and trigger_execute
// dispatches each one on its action byte to a handler that takes
// `TrigAction *` in ECX. The bodies are in ascending oracle VA order, which
// under /Ox is also the declaration order the oracle was built from.
//
// Four kinds of body: the sweep handlers (act_give and the act_set_unit_*
// sextet), which build a rect16_t from the action's map region plus a 20-byte
// TrigUnitSpec naming a per-unit callback and hand both to find_foreach_unit;
// the per-unit setters those specs name; the kill and remove family, whose
// dispatcher is also its own enumeration callback; and the leaves.
#define trigger_create_unit trigger_create_unit_int_view
#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../port/gameplay/port_eud.h"  // Hyperion: EUD maps
#include "platform.h"          // BOOL/BYTE/WORD/DWORD + <stddef.h> for offsetof
#include <string.h>            // strlen (Trig_GetTextDisplayTime -- /Oi makes it
                               // the inline intrinsic, so no call is emitted)
                               // and memset (resetUnitProps, a real cdecl call)
#include "CSprite.h"           // set_hangar derefs pUnit->pSprite->wPosX/wPosY
#include "TrigEnum.h"          // MapRegion, rect16_t, FindUnitCb,
                               // find_foreach_unit, TriggerPlayer,
                               // clear_enum_cache, NationCountCb + the six
                               // *_enum_count (pulls CUnit.h and rect16.h)
#include "statbtn.h"           // TTAllowed -- derived and declared there
#include "minimap.h"           // do_minimap_ping -- act_ping's callee, and the
                               // only spelling of it -- a second would give one
                               // function two mangled names
#include "net_cmds.h"          // PlayerData -- act_pause / act_unpause read
                               // gPlayerData[TriggerPlayer()].nType
#include "rect32.h"            // run_aiscript's third parameter (also reached
                               // through TrigAct.h, which declares it)
#include "victory.h"           // TriggerPlayer, IsCurrentTriggerAborted, the
                               // victory-code pair, the delayed-action pair,
                               // CurrentTriggerPaused / CurrentTriggerUnpaused,
                               // victory_force_check, sub_4BE3D0, and the three
                               // abort/keep leaves TrigAct.h declares
                               // identically, so both are visible and neither
                               // is a redeclaration conflict
#include "net_rand.h"        // net_get_fixed_seed / net_set_fixed_seed
#include "scroll.h"          // scroll_start_auto / scroll_end_auto
#include "snd.h"             // play_digital
#include "gds_local.h"       // psnprintf
#include "CUnitZSpells.h"    // FindPlaceForUnit
#include "CUnitHarvest.h"    // IsTypeNeutralResource
#include "maphdr.h"          // apply_unit_properties
#include "collide.h"         // mtx_query_place
#include "tile2.h"           // sub_4B46E0, sub_4B4DD0
#include "creep.h"           // unit_type_spreads_creep, spread_creep_completely
#include "CUnitProtoss.h"    // RequestJuiceUpdate
#include "TrigAct.h"           // this TU's own interface
#include "../port/render/port_view.h"  // Hyperion: g_portView, PortViewCentreTarget
#include "TAI_GuardPost.h"   // Hyperion: TGuard, which TrigUnitAi views
#include "../h/handicap.h"  // Hyperion: a player's handicap
#undef trigger_create_unit
// CUnitInit.h declares trigger_create_unit(int). Trig_CreateUnitWithProperties
// passes the type unwidened, so this TU declares the same C symbol with a WORD.
// Hyperion: the unit type is an int, as the definition reads it, which the
// x64 build needs; a unit type is the same widened.
extern "C" CUnit *__fastcall trigger_create_unit(int nUnitType, int x, int y,
                                                 int nPlayer);

// Storm: SErrSetLastError -- oracle thunk 0x4D0076, whose whole body is
// `jmp dword ptr [0x4EB524]`, an entry of the Storm.dll import descriptor. The
// convention is read off the call site: every guarded body here pushes one
// dword and performs no `add esp,4` afterwards, so the callee retires it --
// __stdcall with one argument. It is declared here rather than in a header
// because an import is reached through an import library
// (src117/storm_imports.def).
extern "C" void __stdcall SErrSetLastError(DWORD dwErrCode);

// Storm: SStrCopy -- oracle thunk 0x4D0028, a 6-byte `jmp dword ptr [0x4EB554]`
// bound by ordinal 501 in the Storm.dll import descriptor, declared here for the
// same reason SErrSetLastError is.
//
// The convention is __stdcall, not __cdecl, and act_next_mission proves it on
// its own: the call site pushes three dwords (`push 20h` / `push eax` /
// `push 526526h`) and performs no `add esp,0Ch`, and the whole body is 34 bytes
// with nowhere for a cleanup to hide. It is spelled to match src117/h/Storm.h
// exactly (`char *` / `const char *` / `size_t`, inside `extern "C"`,
// STORMAPI == __stdcall), which is the point of restating it rather than
// including that header: a differently-typed restatement would be a second
// mangled name for one import.
extern "C" char *__stdcall SStrCopy(char *pszDest, const char *pszSrc,
                                    size_t cchDest);

// 0x57 == 87 == ERROR_INVALID_PARAMETER. The value is the oracle's `push 57h`;
// the NAME is the Win32 SDK's.
static const DWORD kErrInvalidParameter = 87;

// byte_526526's capacity, as stated by act_next_mission's own `push 20h`. A
// literal rather than sizeof(byte_526526), and that is not fussiness: under
// OC_DLL_MODE the global's spelling is a `char *`, so sizeof would silently
// become 4 and the shipped form would bake `push 4h` -- a buffer-length bug the
// proof form cannot see, because there the same expression is an array and
// sizeof is 32.
static const int kNextMissionNameMax = 32;

// TrigUnitSpec -- the 20-byte record the sweep carries, built on the stack by
// the seven handlers below and read back field by field by sub_4B7750. It lives
// here and not in src117/h/TrigAct.h because nothing that header declares takes
// one: it is a private argument-passing block between two functions of this TU
// and the one unported visitor between them.
//
// Every field is proven from both ends -- written here, read by sub_4B7750:
//   mov ecx, dword ptr [edi]        -> +0x00 DWORD, the group / player id
//   mov dx,  word ptr [edi+4h]      -> +0x04 WORD, the height mask
//   movzx esi, word ptr [edi+6h]    -> +0x06 WORD, the unit type
//   add word ptr [edi+8h], ax       -> +0x08 WORD, the remaining count
//   mov eax, dword ptr [edi+0Ch]
//   call eax                        -> +0x0C the apply function pointer
//   push dword ptr [edi+10h]        -> +0x10 DWORD, the value
// +0x0A is alignment padding between a WORD and a DWORD; nothing reads it.
struct TrigUnitSpec {
  /*0x00*/ DWORD         dwGroup;      // [edi]      -> owned_count's ECX
  /*0x04*/ WORD          wHeightMask;  // [edi+4h]   -> TrigEnum_HeightMatches
  /*0x06*/ WORD          wUnitType;    // [edi+6h]
  /*0x08*/ WORD          wCount;       // [edi+8h]   0xFFFF == unlimited
  /*0x0A*/                             // 2 B of alignment padding; unread
  /*0x0C*/ SetUnitPropCb pfnApply;     // [edi+0Ch]
  /*0x10*/ DWORD         dwValue;      // [edi+10h]
};
OC_X86_LAYOUT(sizeof(TrigUnitSpec)                == 0x14, "spec size (sub esp,1Ch minus the rect)");
static_assert(offsetof(TrigUnitSpec, dwGroup)     == 0x00, "spec::dwGroup     [edi]");
static_assert(offsetof(TrigUnitSpec, wHeightMask) == 0x04, "spec::wHeightMask [edi+4h]");
static_assert(offsetof(TrigUnitSpec, wUnitType)   == 0x06, "spec::wUnitType   [edi+6h]");
static_assert(offsetof(TrigUnitSpec, wCount)      == 0x08, "spec::wCount      [edi+8h]");
OC_X86_LAYOUT(offsetof(TrigUnitSpec, pfnApply)    == 0x0C, "spec::pfnApply    [edi+0Ch]");
OC_X86_LAYOUT(offsetof(TrigUnitSpec, dwValue)     == 0x10, "spec::dwValue     [edi+10h]");

// The globals this TU reaches, dual-spelled: an `extern` for the proof path,
// which gives the oracle's DIR32 fixup set, and a baked absolute literal for
// the payload, so the --dll-mode gate exercises the shipped branch. The extents
// are proven rather than inferred from adjacency: dword_534F20 is the 256
// switches as 8 dwords, sgUnitProps_g is BYTE[1280] by resetUnitProps' own
// `push 500h`, sgbRemoveUnit_g is four bytes wide at both ends (so the `b` in
// the sheet's name is "boolean"), and gMapRegions holds 255 records,
// 0x53632C - 0x534F40 being 255 * 20 exactly.

// Addresses that are fields rather than objects, never to be recorded as
// globals: 0x534F52 (record 0's own +0x12), 0x534F2C/30/34/38 (gMapRegions - 20
// plus a field offset, which taken at face value would cap dword_534F20 at 3
// dwords), 0x6726D0 (&gPlayerData[0].nType) and gOptions_18 @0x6D3148
// (gOptions + 0x18). guwUnitEnergy keeps the sheet's name, but the oracle uses
// that table as the shield amount and nothing else, so a rename would be a
// globals117.json decision rather than this file's.
#ifdef OC_DLL_MODE
#define g_LocalNationID   (*reinterpret_cast<DWORD *>(OC_VA(0x508D50u)))
#define dword_5349A4      (reinterpret_cast<DWORD *>(OC_VA(0x5349A4u)))
#define dword_5349E8      (*reinterpret_cast<DWORD *>(OC_VA(0x5349E8u)))
#define gbUnitHasShields  (reinterpret_cast<BYTE *>(OC_VA(0x520458u)))
#define guwUnitEnergy     (reinterpret_cast<WORD *>(OC_VA(0x520540u)))
#define gulUnitTypeFlags  (reinterpret_cast<DWORD *>(OC_VA(0x521660u)))
#define gubUnitCountInfo  (reinterpret_cast<BYTE *>(OC_VA(0x523C40u)))
#define byte_526526       (reinterpret_cast<char *>(OC_VA(0x526526u)))
#define dword_534F20      (reinterpret_cast<DWORD *>(OC_VA(0x534F20u)))
#define gMapRegions       (reinterpret_cast<MapRegion *>(OC_VA(0x534F40u)))
#define dword_53632C      (*reinterpret_cast<DWORD *>(OC_VA(0x53632Cu)))
#define gbMultiPlayerMode (*reinterpret_cast<BYTE *>(OC_VA(0x672664u)))
#define gPlayerData       (reinterpret_cast<PlayerData *>(OC_VA(0x6726C8u)))
#define sgUnitProps_g     (reinterpret_cast<BYTE *>(OC_VA(0x6C4700u)))
#define gbTriggerPaused   (*reinterpret_cast<BYTE *>(OC_VA(0x6CA740u)))
#define gszMapFileTbl      (reinterpret_cast<char **>(OC_VA(0x51ABB8u)))
#define word_526524_g      (*reinterpret_cast<WORD *>(OC_VA(0x526524u)))
#define g_dwCountdownTimer (*reinterpret_cast<DWORD *>(OC_VA(0x5349D4u)))
#define byte_5349EC        (*reinterpret_cast<BYTE *>(OC_VA(0x5349ECu)))
#define byte_5349ED        (*reinterpret_cast<BYTE *>(OC_VA(0x5349EDu)))
#define word_5349EE        (*reinterpret_cast<WORD *>(OC_VA(0x5349EEu)))
#define dword_5349F0       (*reinterpret_cast<DWORD *>(OC_VA(0x5349F0u)))
#define dword_5349F4       (*reinterpret_cast<LONG *>(OC_VA(0x5349F4u)))
#define gwMapPixWidth      (*reinterpret_cast<WORD *>(OC_VA(0x543230u)))
#define gwMapPixHeight     (*reinterpret_cast<WORD *>(OC_VA(0x543234u)))
#define gPlayback          (*reinterpret_cast<DWORD *>(OC_VA(0x67CC90u)))
#define dword_6C4C0C       (*reinterpret_cast<DWORD *>(OC_VA(0x6C4C0Cu)))
#define gPlayerMinerals    (reinterpret_cast<int *>(OC_VA(0x5263D0u)))
#define gPlayerGas         (reinterpret_cast<int *>(OC_VA(0x526400u)))
#define dword_526430       (reinterpret_cast<int *>(OC_VA(0x526430u)))
#define dword_526460       (reinterpret_cast<int *>(OC_VA(0x526460u)))
#define glGameCounts_g     (reinterpret_cast<DWORD (*)[12]>(OC_VA(0x5290C4u)))
#define glCounts_g         (reinterpret_cast<DWORD (*)[228][12]>(OC_VA(0x529604u)))
#define gubUnitCargoHold   (reinterpret_cast<BYTE *>(OC_VA(0x523FE0u)))
#define gUnitBoundBox      (reinterpret_cast<rect16_t *>(OC_VA(0x522C38u)))
#define gUnitPlaceboxSize  (reinterpret_cast<point16_t *>(OC_VA(0x522728u)))
#define sgbAlliances_g     (reinterpret_cast<BYTE (*)[12]>(OC_VA(0x534914u)))
#define gxUnitLife         (reinterpret_cast<LONG *>(OC_VA(0x520708u)))
#define dword_6C4C04       (*reinterpret_cast<int *>(OC_VA(0x6C4C04u)))
#define dword_6C4C08       (*reinterpret_cast<DWORD *>(OC_VA(0x6C4C08u)))
#else
extern DWORD      g_LocalNationID;
extern PlayerObjectivesTable dword_5349A4;  // Hyperion: and the slots past 11
extern DWORD      dword_5349E8;
extern BYTE       gbUnitHasShields[228] OC_UNALIGNED;
extern WORD       guwUnitEnergy[228] OC_UNALIGNED;
extern DWORD      gulUnitTypeFlags[228] OC_UNALIGNED;
extern BYTE       gubUnitCountInfo[228] OC_UNALIGNED;
extern char       byte_526526[32] OC_UNALIGNED;
extern DWORD      dword_534F20[8];
extern MapRegion  gMapRegions[255];
extern DWORD      dword_53632C;
extern BYTE       gbMultiPlayerMode;
extern PlayerTable<PlayerData, PlayerTagData> gPlayerData;
extern BYTE       sgUnitProps_g[1280];
// Internal linkage, and it is a codegen lever rather than a tidy-up: with
// external linkage MSVC cannot know a TrigAction * does not alias this scalar,
// so it reloads it across the handler's stores and act_kill / act_remove come
// out wrong. Every referent is inside this TU.
extern BYTE       gbTriggerPaused;
extern char      *gszMapFileTbl[65];  // Hyperion: mapdata.dat's 65 rows
extern WORD       word_526524_g;
extern DWORD      g_dwCountdownTimer;
extern BYTE       byte_5349EC;
extern BYTE       byte_5349ED;
extern WORD       word_5349EE;
extern DWORD      dword_5349F0;
extern LONG       dword_5349F4;
extern upos_t     gwMapPixWidth;
extern upos_t     gwMapPixHeight;
extern DWORD      gPlayback;
extern DWORD      dword_6C4C0C;
extern PlayerMineralsTable gPlayerMinerals;
extern PlayerGasTable gPlayerGas;
extern PlayerGasGatheredTable dword_526430;
extern PlayerMineralsGatheredTable dword_526460;
extern PlayerGameCountsGrid glGameCounts_g;
extern PlayerUnitCountsGrid glCounts_g;
extern BYTE       gubUnitCargoHold[232] OC_UNALIGNED;
extern rect16_t   gUnitBoundBox[228] OC_UNALIGNED;
extern point16_t  gUnitPlaceboxSize[228] OC_UNALIGNED;
extern LONG       gxUnitLife[228] OC_UNALIGNED;
extern PlayerAlliancesMatrix sgbAlliances_g;
extern int        dword_6C4C04;
extern DWORD      dword_6C4C08;
#endif
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgbRemoveUnit_g   (*reinterpret_cast<int *>(OC_VA(0x6C4C00u)))
#else
static int        sgbRemoveUnit_g;
#endif
#ifdef OC_DLL_MODE
#define gOptions_18       (*reinterpret_cast<DWORD *>(OC_VA(0x6D3148u)))
#else
extern DWORD      gOptions_18;
#endif

// The flag bits this TU tests. Every value is an oracle immediate; the names are
// our reading of what the guarded code does, and where a name is not derivable
// that is said rather than papered over.

// gubUnitCountInfo bits -- the three StarEdit unit classes the trigger UI
// offers, pinned by three same-shaped predicates in this file (Men 0x08,
// Buildings 0x10, Factories 0x20): three named functions, three masks, one
// table.
enum {
  UCI_Men      = 0x08,
  UCI_Building = 0x10,
  UCI_Factory  = 0x20
};

// gulUnitTypeFlags bits. 0x200000 gates set_energy (the units.dat
// "uses energy / spellcaster" bit) and 0x2000 gates set_resources_unit (the
// "resource container" bit). Values are the oracle's immediates; the readings
// follow from what each guarded body then does, and are flagged as inferred.
enum {
  UTF_UsesEnergy       = 0x00200000,
  UTF_ResourceContainer = 0x00002000
};

// CUnit::dwFlags bit 0x40000000, which disqualifies a unit from having its
// energy set (and, in set_owner at 0x4B81A0, from changing hands). The value is
// the oracle's `test dword ptr [esi+0DCh], 40000000h`; the name is not derived,
// and is kept out of src117/h/CUnit.h until a body earns it, because a name in a
// shared header reads as a fact about the field.
enum { UF_Hallucination = 0x40000000 };

// CUnit::dwFlags 0x04000000 and the units.dat flag 0x20000000 that pins it:
// clearing invulnerability is refused for types that are always invincible.
enum { UF_Invincible = 0x04000000, UTF_Invincible = 0x20000000 };

// "All", spelled the way the oracle spells it: 999999 == 0xF423F, the sentinel
// both kill/remove handlers substitute when the action's count byte is zero.
#define KILLREMOVE_ALL 999999

// The trigger group and unit-type codes. The values are the oracle's; each name
// comes from the function that case dispatches to. The group codes are
// TrigAct_KillRemoveUnits' first jump table (0x4B8DC0, 14 entries covering
// 0x0D..0x1A), the unit-type codes its second (0x4B8DF8, 4 entries covering
// 0xE5..0xE8).
//
// Group 0x0D is CurrentPlayer because its case body is exactly
// `nGroup = TriggerPlayer()`; 0x12..0x15 are the four forces because they are
// the only cases routed to group_enum_count, the one enumerator that reads
// argument 1 (`lea eax,[ecx-11h]`, group id minus 0x11 == force number).
// 0x16..0x19 fall to a bare `return 0` and are named nothing: four dead slots
// between the forces and non-allied-victory is all the binary says.
enum {
  GROUP_CurrentPlayer    = 0x0D,
  GROUP_Foes             = 0x0E,
  GROUP_Allies           = 0x0F,
  GROUP_NeutralPlayers   = 0x10,
  GROUP_AllPlayers       = 0x11,
  GROUP_Force1           = 0x12,
  GROUP_Force2           = 0x13,
  GROUP_Force3           = 0x14,
  GROUP_Force4           = 0x15,
  GROUP_Unused16         = 0x16,
  GROUP_Unused17         = 0x17,
  GROUP_Unused18         = 0x18,
  GROUP_Unused19         = 0x19,
  GROUP_NonAlliedVictory = 0x1A
};

enum {
  UNITCODE_AnyUnit   = 0xE5,   // -> TrigAct_KillRemoveAnyUnit_Cond   (0x4B8C40)
  UNITCODE_Men       = 0xE6,   // -> TrigAct_KillRemoveMen_Cond       (0x4B8E10)
  UNITCODE_Buildings = 0xE7,   // -> TrigAct_KillRemoveBuildings_Cond (0x4B8C60)
  UNITCODE_Factories = 0xE8    // -> TrigAct_KillRemoveFactories_Cond (0x4B8C90)
};

// The bodies, in ascending oracle VA order.

// The countdown ceiling act_set_timer clamps to, `mov eax, 57E3Fh`.
static const int kMaxCountdown = 0x57E3F;

// ===========================================================================
// The four argument-passing blocks, each built on one handler's stack and read
// by one unported callee. None of them is TrigAct.cpp's TrigUnitSpec.
// ===========================================================================

// act_move_unit's context, read back by trigact_moveunit_cb (0x4B7B20):
//   mov ecx, dword ptr [edi]        -> +0x00 DWORD, owned_count's group
//   mov dx,  word ptr [edi+6h]      -> +0x06 WORD, TrigEnum_HeightMatches
struct TrigMoveUnitCtx {
  /*0x00*/ DWORD dwGroup;
  /*0x04*/ WORD  wUnitType;
  /*0x06*/ WORD  wHeightMask;
  /*0x08*/ BYTE  bDestRegion;
};
static_assert(sizeof(TrigMoveUnitCtx)                == 0x0C, "ctx size (sub esp,14h minus the rect)");
static_assert(offsetof(TrigMoveUnitCtx, dwGroup)     == 0x00, "ctx::dwGroup     [ebp-14h]");
static_assert(offsetof(TrigMoveUnitCtx, wUnitType)   == 0x04, "ctx::wUnitType   [ebp-10h]");
static_assert(offsetof(TrigMoveUnitCtx, wHeightMask) == 0x06, "ctx::wHeightMask [ebp-0Eh]");
static_assert(offsetof(TrigMoveUnitCtx, bDestRegion) == 0x08, "ctx::bDestRegion [ebp-0Ch]");

// act_invincible's context, read back by trigact_set_invincibility_cb
// (0x4B7440): `movdqu xmm0, xmmword ptr [ebx]` takes all sixteen bytes,
// `cmp dword ptr [ebx+0Ch], 0h` reads +0x0C whole, `mov dx, word ptr [ebx+6h]`
// the height mask and `mov ecx, dword ptr [ebx]` the group.
struct TrigInvincibleCtx {
  /*0x00*/ DWORD dwGroup;
  /*0x04*/ WORD  wUnitType;
  /*0x06*/ WORD  wHeightMask;
  /*0x08*/ BYTE  bEnable;
  /*0x09*/                        // 3 B of alignment padding; unread
  /*0x0C*/ DWORD dwFlag;
};
static_assert(sizeof(TrigInvincibleCtx)                == 0x10, "ctx size (sub esp,18h minus the rect)");
static_assert(offsetof(TrigInvincibleCtx, dwGroup)     == 0x00, "ctx::dwGroup     [ebp-18h]");
static_assert(offsetof(TrigInvincibleCtx, wUnitType)   == 0x04, "ctx::wUnitType   [ebp-14h]");
static_assert(offsetof(TrigInvincibleCtx, wHeightMask) == 0x06, "ctx::wHeightMask [ebp-12h]");
static_assert(offsetof(TrigInvincibleCtx, bEnable)     == 0x08, "ctx::bEnable     [ebp-10h]");
static_assert(offsetof(TrigInvincibleCtx, dwFlag)      == 0x0C, "ctx::dwFlag      [ebp-0Ch]");

// act_order's context, read back by TrigOrderUnit: `mov dx, word ptr [edi+8h]`
// narrows the DWORD at +0x08 for TrigEnum_HeightMatches, `mov ecx, dword ptr
// [edi]` takes the group. Every field here is a DWORD on the write side.
struct TrigOrderCtx {
  /*0x00*/ DWORD dwGroup;
  /*0x04*/ int   nUnitType;
  /*0x08*/ int   nHeightMask;
  /*0x0C*/ int   x;
  /*0x10*/ int   y;
  /*0x14*/ int   nOrder;
};
static_assert(sizeof(TrigOrderCtx)                == 0x18, "ctx size (sub esp,20h minus the rect)");
static_assert(offsetof(TrigOrderCtx, dwGroup)     == 0x00, "ctx::dwGroup     [ebp-20h]");
static_assert(offsetof(TrigOrderCtx, nUnitType)   == 0x04, "ctx::nUnitType   [ebp-1Ch]");
static_assert(offsetof(TrigOrderCtx, nHeightMask) == 0x08, "ctx::nHeightMask [ebp-18h]");
static_assert(offsetof(TrigOrderCtx, x)           == 0x0C, "ctx::x           [ebp-14h]");
static_assert(offsetof(TrigOrderCtx, y)           == 0x10, "ctx::y           [ebp-10h]");
static_assert(offsetof(TrigOrderCtx, nOrder)      == 0x14, "ctx::nOrder      [ebp-0Ch]");

// The action's WAV string id: TrigAction's +0x08 filler, reached the way
// src117/lang/gluBrfAct.cpp reaches it, because naming the field is a header
// edit this slice may not make. `cmp dword ptr [esi+8h], 0h` guards it and
// `mov cx, word ptr [esi+8h]` narrows it at get_map_string.
#define ACT_WAV_STRING(pAct) (*reinterpret_cast<DWORD *>(&(pAct)->_unread_08[0]))


// act_portrait. "Show portrait".
//
// The local-player gate feeding a four-argument call.
int __fastcall act_portrait(TrigAction *pAction)
{
  if (TriggerPlayer() == g_LocalNationID && !IsCurrentTriggerAborted())
    show_portrait(pAction->wUnitType, -1, -1, pAction->dwTime);

  return 1;
}

// act_mute. "Mute unit speech".
//
// The local-player gate, with the abort test. act_unmute below has none, and
// the asymmetry is reproduced, not tidied.
int __fastcall act_mute(void)
{
  if (TriggerPlayer() == g_LocalNationID && !IsCurrentTriggerAborted())
    gamesnd_mute_speech();

  return 1;
}

// act_unmute. "Unmute unit speech".
//
// The same gate without act_mute's abort test: 1.17 lets an aborted trigger un-mute but
// not mute. Kept as written.
int __fastcall act_unmute(void)
{
  if (TriggerPlayer() == g_LocalNationID)
    gamesnd_unmute_speech();

  return 1;
}

// act_leaderboard_cpu. "Leaderboard: computer
// players" -- the flag controlling whether computer-owned nations appear on the
// leaderboard. bCount is the sub-opcode and all three arms write the dword dword_5349E8:
// case 4 sets 1, case 5 sets 0, case 6 toggles it. The source keeps the ascending case
// order; MSVC lays the blocks out the other way.
int __fastcall act_leaderboard_cpu(TrigAction *pAction)
{
  switch (pAction->bCount)
  {
  case 4:
    dword_5349E8 = 1;
    break;
  case 5:
    dword_5349E8 = 0;
    break;
  case 6:
    dword_5349E8 ^= 1;
    break;
  }

  return 1;
}
// act_move_loc. "Move location": recentre the
// destination location on a found unit, then clamp it back inside the map.
//
// The destination is read twice, once for the guard and once for the subscript, which
// folds the one-based index into the displacement.
int __fastcall act_move_loc(TrigAction *pAction)
{
  BYTE bRegion = pAction->bRegion;
  if (bRegion == 0)
    return 1;
  if (bRegion > 255)
    return 1;
  if (pAction->dwValue == 0)
    return 1;
  if (pAction->dwValue > 0xFF)
    return 1;

  {
    TrigPoint  pt;
    MapRegion *pDest;
    int        dx;
    int        dy;

    sub_4B7330(pAction->wUnitType, pAction->dwGroup,
               static_cast<BYTE>(bRegion - 1), &pt);

    pDest = &gMapRegions[pAction->dwValue - 1];

    dx = pt.x - (pDest->left + pDest->right) / 2;
    dy = pt.y - (pDest->top + pDest->bottom) / 2;

    pDest->right  += dx;
    pDest->left   += dx;
    pDest->top    += dy;
    pDest->bottom += dy;

    if (pDest->left < 0)
    {
      pDest->right -= pDest->left;
      pDest->left = 0;
    }
    if (pDest->top < 0)
    {
      pDest->bottom -= pDest->top;
      pDest->top = 0;
    }
    if (pDest->right >= gwMapPixWidth)
    {
      pDest->left += gwMapPixWidth - pDest->right - 1;
      pDest->right = gwMapPixWidth - 1;
    }
    if (pDest->bottom >= gwMapPixHeight)
    {
      pDest->top += gwMapPixHeight - pDest->bottom - 1;
      pDest->bottom = gwMapPixHeight - 1;
    }

    clear_enum_cache();
  }

  return 1;
}

// act_move_unit. "Move unit": sweep the
// action's region and teleport each match into the destination region.
int __fastcall act_move_unit(TrigAction *pAction)
{
  BYTE bRegion = pAction->bRegion;

  // Hyperion: and a destination whose low byte names a location: 1.17 took 256 as location 256,
  // one past the table
  if (bRegion != 0 && bRegion <= 255 && pAction->dwValue != 0 && static_cast<BYTE>(pAction->dwValue) != 0)
  {
    rect16_t        area;
    TrigMoveUnitCtx ctx;

    --bRegion;
    area.left   = static_cast<short>(gMapRegions[bRegion].left);
    area.right  = static_cast<short>(gMapRegions[bRegion].right);
    area.top    = static_cast<short>(gMapRegions[bRegion].top);
    area.bottom = static_cast<short>(gMapRegions[bRegion].bottom);

    ctx.wUnitType   = pAction->wUnitType;
    ctx.dwGroup     = pAction->dwGroup;
    ctx.bDestRegion = static_cast<BYTE>(static_cast<BYTE>(pAction->dwValue) - 1);
    ctx.wHeightMask = gMapRegions[bRegion].wFlags;

    dword_6C4C0C = pAction->bCount ? pAction->bCount : KILLREMOVE_ALL;

    find_foreach_unit(&area, trigact_moveunit_cb, &ctx);
    clear_enum_cache();
  }

  return 1;
}

// act_next_mission. "Load next mission".
//
// The guard compares the whole DWORD dwStringId, and the call narrows the same slot to a
// WORD; a WORD field would make the guard 16-bit too.
int __fastcall act_next_mission(TrigAction *pAction)
{
  if (pAction->dwStringId != 0)
    SStrCopy(byte_526526,
             get_map_string(static_cast<WORD>(pAction->dwStringId)),
             kNextMissionNameMax);

  return 1;
}

// act_doodad. "Set doodad state" -- set,
// clear or toggle the doodad (unpowered / disabled) state of the first matching
// unit in the action's map region. The jump table maps 0 and 6 to Toggle, 4 to Enable and
// 5 to Disable; 1, 2, 3 and anything above 6 fall to the shared tail. The region guard is
// the sextet's idiom, dead branch included.
int __fastcall act_doodad(TrigAction *pAction)
{
  BYTE nRegion = pAction->bRegion;

  if (nRegion != 0 && nRegion <= 255)
  {
    CUnit *pUnit;
    // The one-byte aggregate copy is what produces the oracle's EBP frame, its
    // `push ecx` local slot and its `mov byte ptr [ebp-4h], al / push dword ptr
    // [ebp-4h]` -- three of whose four pushed bytes are uninitialised, a genuine
    // 1.17 defect reproduced by design. TrigRegionId in src117/h/TrigAct.h
    // carries the derivation.
    TrigRegionId region;

    --nRegion;
    region.bRegion = nRegion;
    pUnit = TrigFindFirstUnitAt(pAction->wUnitType, pAction->dwGroup, region);

    if (pUnit != NULL)
    {
      switch (pAction->bCount)
      {
      case 4:
        pUnit->EnableDoodadState();
        break;
      case 5:
        pUnit->DisableDoodadState();
        break;
      case 0:
      case 6:
        pUnit->ToggleDoodadState();
        break;
      }
    }
  }

  return 1;
}

// act_invincible. "Set invincibility".
int __fastcall act_invincible(TrigAction *pAction)
{
  BYTE bRegion = pAction->bRegion;

  if (bRegion != 0 && bRegion <= 255)
  {
    rect16_t          area;
    TrigInvincibleCtx ctx;

    --bRegion;
    ctx.dwFlag = 1;

    area.left   = static_cast<short>(gMapRegions[bRegion].left);
    area.right  = static_cast<short>(gMapRegions[bRegion].right);
    area.top    = static_cast<short>(gMapRegions[bRegion].top);
    area.bottom = static_cast<short>(gMapRegions[bRegion].bottom);

    ctx.wUnitType   = pAction->wUnitType;
    ctx.dwGroup     = pAction->dwGroup;
    ctx.bEnable     = pAction->bCount;
    ctx.wHeightMask = gMapRegions[bRegion].wFlags;

    find_foreach_unit(&area, trigact_set_invincibility_cb, &ctx);
  }

  return 1;
}


// The sweep sextet -- one shape six times. Five of the six oracle bodies (give,
// hp, energy, shields, hangar) are identical for all 173 bytes except the one
// DIR32 slot at +0x9A that holds the apply callback; only act_set_unit_res
// diverges, and only in its unit-type filter. They are written out longhand
// rather than folded into a helper because that is what the oracle emitted: six
// separate COMDATs.

// Three decisions in the shape were each measured by removing them:
//
//  1. The guard is `region != 0 && region <= 255`, and cl 18.00 does not fold
//     the second half away even though nRegion is a BYTE, so `cmp al, 0FFh / ja`
//     is a branch that can never be taken. Dropping it gives 165 B against 173.
//  2. The null test on &gMapRegions[n] checks the address of an array element,
//     which is never null; cl 18.00 emits it anyway, and it is what makes
//     SErrSetLastError reachable in the compiler's eyes. Dropping it gives 166 B.
//  3. wHeightMask is spelled as the array subscript, not `pRegion->wFlags`: the
//     oracle re-materialises the address rather than reusing the ECX it holds,
//     the signature of a second subscript. `pRegion->wFlags` gives 169 B.

// act_give. "Give units to player".
int __fastcall act_give(TrigAction *pAction)
{
  BYTE nRegion = pAction->bRegion;

  if (nRegion != 0 && nRegion <= 255)
  {
    rect16_t     area;
    TrigUnitSpec spec;

    --nRegion;
    const MapRegion *pRegion = &gMapRegions[nRegion];
    if (pRegion == NULL)
      SErrSetLastError(kErrInvalidParameter);
    else
    {
      area.left   = (short)pRegion->left;
      area.right  = (short)pRegion->right;
      area.top    = (short)pRegion->top;
      area.bottom = (short)pRegion->bottom;
    }

    spec.dwGroup     = pAction->dwGroup;
    spec.wHeightMask = gMapRegions[nRegion].wFlags;
    spec.wUnitType   = pAction->wUnitType;
    spec.wCount      = (WORD)(pAction->bCount ? pAction->bCount : 0xFFFF);
    spec.pfnApply    = set_owner;
    spec.dwValue     = pAction->dwValue;

    find_foreach_unit(&area, sub_4B7750, &spec);
  }

  return 1;
}

// act_set_unit_hp. "Set unit hit points",
// a percentage, applied by set_health.
int __fastcall act_set_unit_hp(TrigAction *pAction)
{
  BYTE nRegion = pAction->bRegion;

  if (nRegion != 0 && nRegion <= 255)
  {
    rect16_t     area;
    TrigUnitSpec spec;

    --nRegion;
    const MapRegion *pRegion = &gMapRegions[nRegion];
    if (pRegion == NULL)
      SErrSetLastError(kErrInvalidParameter);
    else
    {
      area.left   = (short)pRegion->left;
      area.right  = (short)pRegion->right;
      area.top    = (short)pRegion->top;
      area.bottom = (short)pRegion->bottom;
    }

    spec.dwGroup     = pAction->dwGroup;
    spec.wHeightMask = gMapRegions[nRegion].wFlags;
    spec.wUnitType   = pAction->wUnitType;
    spec.wCount      = (WORD)(pAction->bCount ? pAction->bCount : 0xFFFF);
    spec.pfnApply    = set_health;
    spec.dwValue     = pAction->dwValue;

    find_foreach_unit(&area, sub_4B7750, &spec);
  }

  return 1;
}

// act_set_unit_energy.
int __fastcall act_set_unit_energy(TrigAction *pAction)
{
  BYTE nRegion = pAction->bRegion;

  if (nRegion != 0 && nRegion <= 255)
  {
    rect16_t     area;
    TrigUnitSpec spec;

    --nRegion;
    const MapRegion *pRegion = &gMapRegions[nRegion];
    if (pRegion == NULL)
      SErrSetLastError(kErrInvalidParameter);
    else
    {
      area.left   = (short)pRegion->left;
      area.right  = (short)pRegion->right;
      area.top    = (short)pRegion->top;
      area.bottom = (short)pRegion->bottom;
    }

    spec.dwGroup     = pAction->dwGroup;
    spec.wHeightMask = gMapRegions[nRegion].wFlags;
    spec.wUnitType   = pAction->wUnitType;
    spec.wCount      = (WORD)(pAction->bCount ? pAction->bCount : 0xFFFF);
    spec.pfnApply    = set_energy;
    spec.dwValue     = pAction->dwValue;

    find_foreach_unit(&area, sub_4B7750, &spec);
  }

  return 1;
}

// act_set_unit_shields.
int __fastcall act_set_unit_shields(TrigAction *pAction)
{
  BYTE nRegion = pAction->bRegion;

  if (nRegion != 0 && nRegion <= 255)
  {
    rect16_t     area;
    TrigUnitSpec spec;

    --nRegion;
    const MapRegion *pRegion = &gMapRegions[nRegion];
    if (pRegion == NULL)
      SErrSetLastError(kErrInvalidParameter);
    else
    {
      area.left   = (short)pRegion->left;
      area.right  = (short)pRegion->right;
      area.top    = (short)pRegion->top;
      area.bottom = (short)pRegion->bottom;
    }

    spec.dwGroup     = pAction->dwGroup;
    spec.wHeightMask = gMapRegions[nRegion].wFlags;
    spec.wUnitType   = pAction->wUnitType;
    spec.wCount      = (WORD)(pAction->bCount ? pAction->bCount : 0xFFFF);
    spec.pfnApply    = set_shields;
    spec.dwValue     = pAction->dwValue;

    find_foreach_unit(&area, sub_4B7750, &spec);
  }

  return 1;
}

// act_set_unit_res. "Set resource amount on units".
//
// The only member of the sextet that is not a callback swap: the unit-type filter is the
// literal 0xE5 instead of the action's own wUnitType, one byte longer. 0xE5 is the first of
// four pseudo-types sub_4B7750 dispatches, whose arm tests the "counts as men" bit; whether
// filtering a resource action by it is intended is not known.
int __fastcall act_set_unit_res(TrigAction *pAction)
{
  BYTE nRegion = pAction->bRegion;

  if (nRegion != 0 && nRegion <= 255)
  {
    rect16_t     area;
    TrigUnitSpec spec;

    --nRegion;
    const MapRegion *pRegion = &gMapRegions[nRegion];
    if (pRegion == NULL)
      SErrSetLastError(kErrInvalidParameter);
    else
    {
      area.left   = (short)pRegion->left;
      area.right  = (short)pRegion->right;
      area.top    = (short)pRegion->top;
      area.bottom = (short)pRegion->bottom;
    }

    spec.dwGroup     = pAction->dwGroup;
    spec.wHeightMask = gMapRegions[nRegion].wFlags;
    spec.wUnitType   = 0xE5;
    spec.wCount      = (WORD)(pAction->bCount ? pAction->bCount : 0xFFFF);
    spec.pfnApply    = set_resources_unit;
    spec.dwValue     = pAction->dwValue;

    find_foreach_unit(&area, sub_4B7750, &spec);
  }

  return 1;
}

// act_set_hangar. "Set hangar count"
// (interceptors / scarabs).
int __fastcall act_set_hangar(TrigAction *pAction)
{
  BYTE nRegion = pAction->bRegion;

  if (nRegion != 0 && nRegion <= 255)
  {
    rect16_t     area;
    TrigUnitSpec spec;

    --nRegion;
    const MapRegion *pRegion = &gMapRegions[nRegion];
    if (pRegion == NULL)
      SErrSetLastError(kErrInvalidParameter);
    else
    {
      area.left   = (short)pRegion->left;
      area.right  = (short)pRegion->right;
      area.top    = (short)pRegion->top;
      area.bottom = (short)pRegion->bottom;
    }

    spec.dwGroup     = pAction->dwGroup;
    spec.wHeightMask = gMapRegions[nRegion].wFlags;
    spec.wUnitType   = pAction->wUnitType;
    spec.wCount      = (WORD)(pAction->bCount ? pAction->bCount : 0xFFFF);
    spec.pfnApply    = set_hangar;
    spec.dwValue     = pAction->dwValue;

    find_foreach_unit(&area, sub_4B7750, &spec);
  }

  return 1;
}

// act_stop_timer.
//
// It sits before act_start_timer (0x4B5E80), in 1.17's order, though the names suggest the
// reverse.
int __fastcall act_stop_timer(void)
{
  dword_53632C = 1;
  return 1;
}

// act_start_timer.
//
// Stores 0 where act_stop_timer stores 1, so dword_53632C is a DWORD "paused" flag; it is
// not given a name.
int __fastcall act_start_timer(void)
{
  dword_53632C = 0;
  return 1;
}

// act_alliance. "Set alliance status".
//
// TriggerPlayer() is called twice, once to feed AllianceUpdateUnits and once for the
// local-player gate, as 1.17 does; a cached local loses the second call. The gate carries no
// abort test, like act_unmute and unlike act_mute and act_portrait.
int __fastcall act_alliance(TrigAction *pAction)
{
  Trig_SetAlliance(pAction->dwGroup, pAction->wUnitType, 0);
  AllianceUpdateUnits(TriggerPlayer());

  if (TriggerPlayer() == g_LocalNationID)
    update_unit_sel_colors();

  return 1;
}


// act_disable_escape.
int __fastcall act_disable_escape(void)
{
  CurrentTriggerDisableAbort();
  return 1;
}

// act_enable_escape.
int __fastcall act_enable_escape(void)
{
  CurrentTriggerEnableAbort();
  return 1;
}

// act_victory.
//
// No gate at all, unlike act_defeat and act_draw: victory overwrites whatever
// code the nation held. Code 3 is the victory code, which act_defeat then treats
// as terminal. The gbTriggerPaused branch chooses victory_force_check() over
// sub_4BE3D0(), and all three members of the trio spell it the same way.
int __fastcall act_victory(void)
{
  PlayerSetVictoryCode(TriggerPlayer(), 3);

  if (gbTriggerPaused)
    victory_force_check();
  else
    sub_4BE3D0();

  return 1;
}

// act_defeat.
//
// act_victory's shape behind a two-test gate, which reads the current victory code twice
// through two TriggerPlayer()/PlayerGetVictoryCode() pairs, as 1.17 does. Codes 3 (victory)
// and 4 are terminal: a defeat cannot overwrite them, and both return 0.
int __fastcall act_defeat(void)
{
  if (PlayerGetVictoryCode(TriggerPlayer()) == 3)
    return 0;

  if (PlayerGetVictoryCode(TriggerPlayer()) == 4)
    return 0;

  PlayerSetVictoryCode(TriggerPlayer(), 2);

  if (gbTriggerPaused)
    victory_force_check();
  else
    sub_4BE3D0();

  return 1;
}

// act_draw.
//
// act_defeat bar the stored code (5 rather than 2), with the same terminal-code gate. Not
// factored into a shared helper: 1.17 has two full bodies, as for act_kill_atloc and
// act_remove_atloc.
int __fastcall act_draw(void)
{
  if (PlayerGetVictoryCode(TriggerPlayer()) == 3)
    return 0;

  if (PlayerGetVictoryCode(TriggerPlayer()) == 4)
    return 0;

  PlayerSetVictoryCode(TriggerPlayer(), 5);

  if (gbTriggerPaused)
    victory_force_check();
  else
    sub_4BE3D0();

  return 1;
}


// act_preserve.
int __fastcall act_preserve(void)
{
  KeepCurrentTrigger();
  return 1;
}

// act_wait.
//
// The one handler in this file that returns 0 on a live path: 0 means "stop executing this
// trigger's action list for now", 1 "carry on". A pending delay returns 0; bFlags bit 1 set
// is cleared and returns 1, the wait over; otherwise the abort gate, then arm the timer, set
// bit 1 and return 0. The clear path reuses the byte its test loaded, hence the local.
int __fastcall act_wait(TrigAction *pAction)
{
  if (IsDelayedAction(TriggerPlayer()))
    return 0;

  BYTE bFlags = pAction->bFlags;

  if (bFlags & 1)
  {
    pAction->bFlags = static_cast<BYTE>(bFlags & ~1);
    return 1;
  }

  if (IsCurrentTriggerAborted())
    return 1;

  SetDelayedAction(TriggerPlayer(), pAction->dwTime);
  pAction->bFlags |= 1;

  return 0;
}

// act_pause. "Pause game".
//
// The four-condition guard chain, and every term is needed: single-player only,
// not already trigger-paused, the trigger's player not a named computer type,
// and the trigger not aborted. gPlayerData[TriggerPlayer()].nType is 0x6726D0
// spelled as the subscript it is -- see the globals block.
int __fastcall act_pause(void)
{
  if (!gbMultiPlayerMode && !gbTriggerPaused
      && !IsNamedComputerPlayerType(gPlayerData[TriggerPlayer()].nType)
      && !IsCurrentTriggerAborted())
  {
    accel_save();
    accel_set_sound();
    cur_hide();
    pause_game();
    CurrentTriggerPaused();
    gbTriggerPaused = 1;
  }

  return 1;
}

// act_unpause.
//
// act_pause's sibling, with the gbTriggerPaused test the other way round (the game must be
// paused) and no IsCurrentTriggerAborted gate: an aborted trigger can still un-pause, or an
// abort during a paused trigger would strand the game paused.
int __fastcall act_unpause(void)
{
  if (!gbMultiPlayerMode && gbTriggerPaused
      && !IsNamedComputerPlayerType(gPlayerData[TriggerPlayer()].nType))
  {
    CurrentTriggerUnpaused();
    gbTriggerPaused = 0;
    unpause_game(0);
    cur_show();
    mou_set_pos(g_portView.nWidth / 2, g_portView.nViewRows / 2);  // Hyperion: 320, 200
    accel_restore();
  }

  return 1;
}
// oracle 0x4B60E0 (342 B). act_transmission. "Transmission": ping and portrait
// the sender, then hold the trigger for the message's duration. The local-player
// test is an early return; nested, ESI's save moves above the bCount switch.
int __fastcall act_transmission(TrigAction *pAction)
{
  BYTE bRegion = pAction->bRegion;
  if (bRegion == 0 || bRegion > 255)
    return 1;
  if (IsNamedComputerPlayerType(gPlayerData[TriggerPlayer()].nType))
    return 1;
  if (IsDelayedAction(TriggerPlayer()))
    return 0;
  BYTE bFlags = pAction->bFlags;
  if (bFlags & 1) {
    pAction->bFlags = static_cast<BYTE>(bFlags & ~1);
    return 1;
  }
  if (IsCurrentTriggerAborted())
    return 1;

  DWORD dwTime = pAction->dwTime;
  switch (pAction->bCount) {
  case 7:
    dwTime = pAction->dwValue;
    break;
  case 8:
    dwTime += pAction->dwValue;
    break;
  case 9:
    if (pAction->dwValue > dwTime)
      dwTime = 0;
    else
      dwTime -= pAction->dwValue;
    break;
  }
  pAction->bFlags |= 1;
  SetDelayedAction(TriggerPlayer(), gbMultiPlayerMode ? dwTime : 0xFFFFFFFF);
  if (TriggerPlayer() != g_LocalNationID)
    return 0;

  act_wav(pAction);
  TrigRegionId region;
  region.bRegion = static_cast<BYTE>(pAction->bRegion - 1);
  CUnit *pUnit = TrigFindFirstUnitAt(pAction->wUnitType, GROUP_AllPlayers, region);
  if (pUnit != NULL) {
    pUnit->pSprite->bFlashCounter = 0x2D;
    do_minimap_ping(PosS(pUnit->pSprite->wPosX), pUnit->pSprite->wPosY, 0x11);
    show_portrait(pAction->wUnitType, PosS(pUnit->pSprite->wPosX),
                  pUnit->pSprite->wPosY, dwTime);
  } else {
    show_portrait(pAction->wUnitType, -1, -1, dwTime);
  }
  if ((gOptions_18 & 0x400) != 0 || (pAction->bFlags & 4) != 0) {
    const char *psz = get_map_string(static_cast<WORD>(pAction->dwStringId));
    if (dwTime <= static_cast<DWORD>(Trig_GetTextDisplayTime(psz)))
      dwTime = Trig_GetTextDisplayTime(psz);
    map_msg_default(psz, dwTime);
  }
  return 0;
}

// act_wav. "Play WAV": the briefing body
// src117/lang/gluBrfAct.cpp's brfact_wav also compiles, behind three more gates.
int __fastcall act_wav(TrigAction *pAction)
{
  char szPath[0x104];

  if (gPlayback == 0 && TriggerPlayer() == g_LocalNationID
      && ACT_WAV_STRING(pAction) != 0 && !IsCurrentTriggerAborted())
  {
    if (word_526524_g == 0)
      SStrCopy(szPath, get_map_string(static_cast<WORD>(ACT_WAV_STRING(pAction))),
               0x104);
    else
      psnprintf(szPath, 0x104, "%s\\%s", gszMapFileTbl[word_526524_g],
                get_map_string(static_cast<WORD>(ACT_WAV_STRING(pAction))));
    play_digital(szPath);
  }

  return 1;
}

// act_text. "Display text message".
//
// Four guards, the middle two a short-circuit `||`: gOptions_18's 0x400 bit bypasses the
// per-action flag test rather than adding to it. The string is computed once into a local
// and handed to both Trig_GetTextDisplayTime and map_msg_default.
int __fastcall act_text(TrigAction *pAction)
{
  if (TriggerPlayer() == g_LocalNationID
      && ((gOptions_18 & 0x400) != 0 || (pAction->bFlags & 4) != 0)
      && !IsCurrentTriggerAborted())
  {
    const char *pszText = get_map_string(static_cast<WORD>(pAction->dwStringId));
    map_msg_default(pszText, Trig_GetTextDisplayTime(pszText));
  }

  return 1;
}

// act_center_view. "Center view".
//
// Multiplayer jumps the camera; single-player smooth-scrolls and holds the
// trigger until the scroll reports back. The two arms read the same location and
// compute the same centre, and the oracle emits the arithmetic twice.
int __fastcall act_center_view(TrigAction *pAction)
{
  BYTE bRegion = pAction->bRegion;
  if (bRegion == 0)
    return 1;
  if (bRegion > 255)
    return 1;
  if (IsGamePaused())
    return 1;
  if (IsNamedComputerPlayerType(gPlayerData[TriggerPlayer()].nType))
    return 1;

  if (gbMultiPlayerMode)
  {
    if (TriggerPlayer() == g_LocalNationID)
    {
      BYTE n = static_cast<BYTE>(pAction->bRegion - 1);

      // Hyperion: the middle of the view, 0x140 and 0xC8 in 1.17.
      cell_set_pos((gMapRegions[n].left + gMapRegions[n].right) / 2 - g_portView.nWidth / 2,
                   (gMapRegions[n].top + gMapRegions[n].bottom) / 2 - g_portView.nViewRows / 2);
    }
    return 1;
  }

  if (IsDelayedAction(TriggerPlayer()))
    return 0;

  {
    BYTE bFlags = pAction->bFlags;
    if (bFlags & 1)
    {
      pAction->bFlags = static_cast<BYTE>(bFlags & ~1);
      scroll_end_auto();
      return 1;
    }
  }

  if (IsCurrentTriggerAborted())
    return 1;

  if (TriggerPlayer() == g_LocalNationID)
  {
    // Hyperion: the middle of the view, and a target inside the map at the
    // view's size, 640 by 400 in 1.17. The auto-scroll has to be able to reach
    // it, so the clamp is the view's, as cell_set_pos's is (cellscrl.cpp), and
    // a view switch during the scroll aims it again (port_view.cpp).
    BYTE n = static_cast<BYTE>(pAction->bRegion - 1);
    int  x;
    int  y;

    PortViewCentreTarget((gMapRegions[n].left + gMapRegions[n].right) / 2,
                         (gMapRegions[n].top + gMapRegions[n].bottom) / 2, &x, &y);
    scroll_start_auto(x, y, victory_notify_action_done);
  }

  pAction->bFlags |= 1;
  SetDelayedAction(TriggerPlayer(), 0xFFFFFFFF);

  return 0;
}

// act_create_unit. "Create unit".
//
// The fixed seed is saved, forced to 0x75BCD15 for the whole run of spawns and
// put back, so a trigger's units land identically on every machine.
int __fastcall act_create_unit(TrigAction *pAction)
{
  BYTE bRegion = pAction->bRegion;

  if (bRegion != 0 && bRegion <= 255)
  {
    TrigCreateProps props;
    int             nSeed = net_get_fixed_seed();
    int             nCount;
    int             i;

    net_set_fixed_seed(0x75BCD15);

    props.dwValue  = pAction->dwValue;
    props.dwRegion = static_cast<BYTE>(pAction->bRegion - 1);

    nCount = pAction->bCount ? pAction->bCount : 1;
    for (i = 0; i < nCount; ++i)
      Trig_CreateUnitWithProperties(pAction->dwGroup, pAction->wUnitType,
                                    &props);

    net_set_fixed_seed(nSeed);
  }

  return 1;
}

// act_set_deaths. "Set / add / subtract
// deaths" -- the death-counter member of the score / resource / death triple
// family, identical in shape to act_set_resource (0x4B6940) and
// act_set_score (0x4B6990) bar the three callees. bCount 7/8/9 select set / add
// / subtract, and the oracle lowers all three as a `dec`-chain rather than a
// jump table because 7..9 is only three values.
int __fastcall act_set_deaths(TrigAction *pAction)
{
  EudMaskAction(pAction);  // Hyperion: an EUD map's mask
  switch (pAction->bCount)
  {
  case 7:
    set_deaths(pAction->dwGroup, pAction->wUnitType, (void *)(DWORD_PTR)pAction->dwValue);
    break;
  case 8:
    add_deaths(pAction->dwGroup, pAction->wUnitType, (void *)(DWORD_PTR)pAction->dwValue);
    break;
  case 9:
    sub_deaths(pAction->dwGroup, pAction->wUnitType, (void *)(DWORD_PTR)pAction->dwValue);
    break;
  }
  EudMaskClear();  // Hyperion

  return 1;
}

// act_order. "Order": sweep the action's
// region and order every match toward the destination's centre.
// pDest points one past the region, so left and top are read first, at a -0x14 bias;
// `pDest->` would load right and bottom first.
int __fastcall act_order(TrigAction *pAction)
{
  BYTE bRegion = pAction->bRegion;

  // Hyperion: and a destination among the 255 locations, which 1.17 read at any dword
  if (bRegion != 0 && bRegion <= 255 && pAction->dwValue != 0 && pAction->dwValue <= 255)
  {
    rect16_t     area;
    TrigOrderCtx ctx;
    MapRegion   *pDest = &gMapRegions[pAction->dwValue];

    --bRegion;
    area.left   = static_cast<short>(gMapRegions[bRegion].left);
    area.right  = static_cast<short>(gMapRegions[bRegion].right);
    area.top    = static_cast<short>(gMapRegions[bRegion].top);
    area.bottom = static_cast<short>(gMapRegions[bRegion].bottom);

    ctx.nUnitType   = pAction->wUnitType;
    ctx.dwGroup     = pAction->dwGroup;
    ctx.x           = (pDest[-1].left + pDest[-1].right) / 2;
    ctx.y           = (pDest[-1].top + pDest[-1].bottom) / 2;
    ctx.nHeightMask = gMapRegions[bRegion].wFlags;
    ctx.nOrder      = pAction->bCount;

    find_foreach_unit(&area, TrigOrderUnit, &ctx);
  }

  return 1;
}

// act_mission_obj. "Set mission objectives".
//
// The whole dword of dwStringId is stored, as act_next_mission's guard reads it. The index
// is TriggerPlayer(), evaluated before the right-hand side.
int __fastcall act_mission_obj(TrigAction *pAction)
{
  dword_5349A4[TriggerPlayer()] = pAction->dwStringId;

  return 1;
}

// act_set_switch. "Set switch" -- a bit write
// into the 256-switch bitmap dword_534F20[8]. dwValue is the switch index, guarded below
// 0x100; the word is value >> 5 and the bit value & 0x1F. bCount 4 sets, 5 clears, 6 toggles
// and 0x0B randomises, on bit 7 of net_rand's low byte, tested as a `signed char`.

// The guard is an early `return 1`, not an `if` wrapping the body: both give the
// identical CFG, but only the early return lets MSVC fold the too-big exit into
// the set arm's epilogue, where `mov eax, 1h / pop edi / ret` is a shared suffix
// the guard can branch into. The two gotos are not tidiable either -- the
// randomise arm's set and clear paths are the same blocks as case 4 and case 5,
// which the oracle reaches by a backward `jz $-0x25` -- and keeping 0x0B out of
// the switch matters as much: as a fourth `case` the range 4..11 is dense enough
// for a jump table, +86 bytes over the oracle's `cmp al, 0Bh` peel and dec-chain.
int __fastcall act_set_switch(TrigAction *pAction)
{
  DWORD dwSwitch = pAction->dwValue;
  DWORD dwIndex;
  DWORD dwShift;
  BYTE  bOp;

  if (dwSwitch >= 256)
    return 1;

  dwIndex = dwSwitch >> 5;
  dwShift = dwSwitch & 0x1F;
  bOp     = pAction->bCount;

  if (bOp == 11)
  {
    if ((signed char)net_rand(0x39) < 0)
      goto set_switch;

    goto clear_switch;
  }

  switch (bOp)
  {
  case 4:
  set_switch:
    dword_534F20[dwIndex] |= 1 << dwShift;
    break;
  case 5:
  clear_switch:
    dword_534F20[dwIndex] &= ~(1 << dwShift);
    break;
  case 6:
    dword_534F20[dwIndex] ^= 1 << dwShift;
    break;
  }

  return 1;
}

// act_set_timer. "Set countdown timer":
// set / add / subtract, then clamp to 0 .. 0x57E3F.
int __fastcall act_set_timer(TrigAction *pAction)
{
  int nTimer = g_dwCountdownTimer;

  switch (pAction->bCount)
  {
  case 7:
    nTimer = pAction->dwTime;
    break;
  case 8:
    nTimer += pAction->dwTime;
    break;
  case 9:
    nTimer -= pAction->dwTime;
    break;
  }

  nTimer = nTimer > 0 ? nTimer : 0;
  g_dwCountdownTimer = nTimer < kMaxCountdown ? nTimer : kMaxCountdown;

  return 1;
}

// act_run_ai. "Run AI script" -- the no-location
// sibling of act_run_ai_atloc below: no region guard, and the area pointer is NULL.
int __fastcall act_run_ai(TrigAction *pAction)
{
  run_aiscript(pAction->dwValue, TriggerPlayer(), NULL);

  return 1;
}

// act_run_ai_atloc. "Run AI script at location".
//
// The two-step region guard, handing the region record itself over. The cast is there
// because run_aiscript has one spelling, `const rect32_t *` (src117/lang/gametype.cpp), and
// MapRegion's first sixteen bytes are a rect32_t field for field.
int __fastcall act_run_ai_atloc(TrigAction *pAction)
{
  BYTE nRegion = pAction->bRegion;

  if (nRegion != 0 && nRegion <= 255)
  {
    --nRegion;
    run_aiscript(pAction->dwValue, TriggerPlayer(),
                 reinterpret_cast<const rect32_t *>(&gMapRegions[nRegion]));
  }

  return 1;
}

// act_leaderboard. Publish the board's type,
// goal and label for the status display to read.
//
// A switch, not an `||` chain: 0x12 and 0x22 share the region arm and fall into
// the tail, 0x28 returns early, and every other value reaches the tail without
// the region guard.
int __fastcall act_leaderboard(TrigAction *pAction)
{
  BYTE bAction;

  sub_4BDDE0();
  bAction = pAction->bAction;

  switch (pAction->bAction)
  {
  case 0x12:
  case 0x22:
    {
      BYTE bRegion = pAction->bRegion;
      if (bRegion == 0)
        return 1;
      if (bRegion > 255)
        return 1;
      byte_5349ED = static_cast<BYTE>(bRegion - 1);
    }
    break;

  case 0x28:
    byte_5349EC  = bAction;
    dword_5349F0 = pAction->dwValue;
    word_5349EE  = 2;
    return 1;
  }

  byte_5349EC  = pAction->bAction;
  word_5349EE  = pAction->wUnitType;
  dword_5349F0 = pAction->dwValue;
  dword_5349F4 = pAction->dwStringId;

  return 1;
}

// act_kill. "Kill unit": the whole-nation
// sweep, with 0xFF standing for "no location".
// __restrict: in the payload the global is a literal address, and without it
// the store stays above these reads.
int __fastcall act_kill(TrigAction *__restrict pAction)
{
  sgbRemoveUnit_g = 0;
  TrigAct_KillRemoveUnits(pAction->dwGroup, pAction->wUnitType, 0xFF,
                          KILLREMOVE_ALL);
  clear_enum_cache();

  return 1;
}


// act_kill_atloc. "Kill unit at location".
//
// __fastcall with one pointer argument. It returns 1 on every path, and trigger_execute
// tests it (zero means "do not advance to the next action this frame"), so the return is
// int. The `bRegion > 255` test is the sextet's dead guard. The limit is a ternary and the
// one-based region index is decremented in the byte domain, as 1.17 has them.
int __fastcall act_kill_atloc(TrigAction *pAct)
{
  BYTE bRegion = pAct->bRegion;
  if (bRegion == 0)
    return 1;
  if (bRegion > 255)
    return 1;

  {
    BYTE bCount = pAct->bCount;
    int  nLimit;

    sgbRemoveUnit_g = 0;
    nLimit = bCount ? bCount : KILLREMOVE_ALL;

    --bRegion;
    TrigAct_KillRemoveUnits(pAct->dwGroup, pAct->wUnitType, bRegion, nLimit);
    clear_enum_cache();
  }
  return 1;
}

// act_remove. Byte-for-byte act_kill except
// for the immediate, exactly as act_kill_atloc and act_remove_atloc are.
// __restrict: in the payload the global is a literal address, and without it
// the store stays above these reads.
int __fastcall act_remove(TrigAction *__restrict pAction)
{
  sgbRemoveUnit_g = 1;
  TrigAct_KillRemoveUnits(pAction->dwGroup, pAction->wUnitType, 0xFF,
                          KILLREMOVE_ALL);
  clear_enum_cache();

  return 1;
}

// act_remove_atloc.
//
// act_kill_atloc except for the immediate stored into sgbRemoveUnit_g. Not factored into a
// shared helper: 1.17 has two full 65-byte bodies.
int __fastcall act_remove_atloc(TrigAction *pAct)
{
  BYTE bRegion = pAct->bRegion;
  if (bRegion == 0)
    return 1;
  if (bRegion > 255)
    return 1;

  {
    BYTE bCount = pAct->bCount;
    int  nLimit;

    sgbRemoveUnit_g = 1;
    nLimit = bCount ? bCount : KILLREMOVE_ALL;

    --bRegion;
    TrigAct_KillRemoveUnits(pAct->dwGroup, pAct->wUnitType, bRegion, nLimit);
    clear_enum_cache();
  }
  return 1;
}

// act_set_resource. "Set / add / subtract
// resources". See act_set_deaths above -- same shape, three different callees.
int __fastcall act_set_resource(TrigAction *pAction)
{
  switch (pAction->bCount)
  {
  case 7:
    set_resources(pAction->dwGroup, pAction->wUnitType, (void *)(DWORD_PTR)pAction->dwValue);
    break;
  case 8:
    add_resources(pAction->dwGroup, pAction->wUnitType, (void *)(DWORD_PTR)pAction->dwValue);
    break;
  case 9:
    sub_resources(pAction->dwGroup, pAction->wUnitType, (void *)(DWORD_PTR)pAction->dwValue);
    break;
  }

  return 1;
}

// act_set_score. "Set / add / subtract score".
// The third member of the triple family. Three bodies, one shape, no shared
// helper -- the oracle emits three full 78-byte COMDATs with no call between them.
int __fastcall act_set_score(TrigAction *pAction)
{
  switch (pAction->bCount)
  {
  case 7:
    set_score(pAction->dwGroup, pAction->wUnitType, (void *)(DWORD_PTR)pAction->dwValue);
    break;
  case 8:
    add_score(pAction->dwGroup, pAction->wUnitType, (void *)(DWORD_PTR)pAction->dwValue);
    break;
  case 9:
    sub_score(pAction->dwGroup, pAction->wUnitType, (void *)(DWORD_PTR)pAction->dwValue);
    break;
  }

  return 1;
}

// act_ping. "Minimap ping" -- flash the minimap
// at a location's centre.
//
// Three guards, all exiting to the same `mov eax, 1h` tail: the two-step region
// guard, the local-player gate and the abort gate, with `cmp al, 0FFh / ja` dead
// on a BYTE as everywhere else in this file.

// bRegion is re-read after the two calls (`mov al, byte ptr [esi]` again at
// +0x23), and that costs a byte if you get it wrong. Hoisting the byte into a
// local ahead of the guard makes MSVC park it in BL across the two calls instead
// of reloading, and then ESI has to be pushed separately for the index: two
// callee-saved registers, 102 bytes, one short of the oracle's 103.

// The centre arithmetic is signed: `cdq / sub eax, edx / sar eax, 1h` is MSVC's
// signed divide-by-2, where an unsigned field would give a bare `shr`. The
// oracle spells x as `mov right / add left` and y as `mov top / add bottom`, the
// addends in opposite order between the axes; writing both as (low + high)
// reproduces both.
int __fastcall act_ping(TrigAction *pAction)
{
  if (pAction->bRegion != 0 && pAction->bRegion <= 255
      && TriggerPlayer() == g_LocalNationID
      && !IsCurrentTriggerAborted())
  {
    BYTE nRegion = static_cast<BYTE>(pAction->bRegion - 1);

    do_minimap_ping((gMapRegions[nRegion].left + gMapRegions[nRegion].right) / 2,
                    (gMapRegions[nRegion].top + gMapRegions[nRegion].bottom) / 2,
                    0x11);
  }

  return 1;
}


// getUnitProps. Returns the base of the properties
// buffer.
BYTE *__fastcall getUnitProps(void)
{
  return sgUnitProps_g;
}

// Hands a unit to a player through set_owner.
void __fastcall sub_4B6A60(CUnit *pUnit, int nPlayer)
{
  set_owner(pUnit, nPlayer);
}

// resetUnitProps. Zero the unit-properties
// buffer.
//
// A real memset call, 1280 bytes being past /Oi's expansion threshold. It takes nothing and
// returns nothing: `void` leaves memset's EAX alone. sgUnitProps_g's BYTE[1280] extent rests
// on this body's 0x500.
void __fastcall resetUnitProps(void)
{
  memset(sgUnitProps_g, 0, 0x500);
}

// Adds to a player's death count, or to every member of a player group.
int __fastcall add_deaths(DWORD nGroup, WORD wUnitType, void *pCtx)
{
  DWORD dwValue = (DWORD)(DWORD_PTR)pCtx;

  // Hyperion: a slot an enumerator passed, not a group code; an EUD map's
  // player is its own value, bit 16 and all, and an EUD game has no slot past 11
  if ((nGroup & TRIG_SLOT_TAG) && !EudGame())
    nGroup &= ~TRIG_SLOT_TAG;
  else
  switch (nGroup) {
  case GROUP_NonAlliedVictory:
    return nava_enum(nGroup, wUnitType, pCtx, add_deaths);
  case GROUP_Foes:
    return foes_enum(nGroup, wUnitType, pCtx, add_deaths);
  case GROUP_Allies:
    return allies_enum(nGroup, wUnitType, pCtx, add_deaths);
  case GROUP_NeutralPlayers:
    return neutrals_enum(nGroup, wUnitType, pCtx, add_deaths);
  case GROUP_AllPlayers:
    return all_enum(nGroup, wUnitType, pCtx, add_deaths);
  case GROUP_Force1:
  case GROUP_Force2:
  case GROUP_Force3:
  case GROUP_Force4:
    return group_enum(nGroup, wUnitType, pCtx, add_deaths);
  case GROUP_CurrentPlayer:
    nGroup = TriggerPlayer();
    break;
  case GROUP_Unused16:
  case GROUP_Unused17:
  case GROUP_Unused18:
  case GROUP_Unused19:
    return 0;
  }

  if (EudGame())  // Hyperion: an EUD game writes through 1.16.1's addresses
    return EudSetDeaths(nGroup, wUnitType, dwValue, EUD_ADD);
  if (PlayerIsLobby(nGroup) && wUnitType < 0xE9) {  // Hyperion: nGroup < 8 in 1.17
    switch (wUnitType) {
    case UNITCODE_AnyUnit:
      break;
    case UNITCODE_Men:
      glGameCounts_g[3][nGroup] += dwValue;
      break;
    case UNITCODE_Buildings:
      glGameCounts_g[10][nGroup] += dwValue;
      break;
    case UNITCODE_Factories:
      glGameCounts_g[16][nGroup] += dwValue;
      break;
    default:
      if (wUnitType < 0xE4)
        glCounts_g[3][wUnitType][nGroup] += dwValue;
      break;
    }
  }
  return 0;
}

// Adds ore, gas or both to a player's balance and gathered total.
int __fastcall add_resources(DWORD nGroup, WORD wUnitType, void *pCtx)
{
  DWORD dwValue = (DWORD)(DWORD_PTR)pCtx;

  if (nGroup & TRIG_SLOT_TAG)  // Hyperion: a slot an enumerator passed, not a group code
    nGroup &= ~TRIG_SLOT_TAG;
  else
  switch (nGroup) {
  case GROUP_NonAlliedVictory:
    return nava_enum(nGroup, wUnitType, pCtx, add_resources);
  case GROUP_Foes:
    return foes_enum(nGroup, wUnitType, pCtx, add_resources);
  case GROUP_Allies:
    return allies_enum(nGroup, wUnitType, pCtx, add_resources);
  case GROUP_NeutralPlayers:
    return neutrals_enum(nGroup, wUnitType, pCtx, add_resources);
  case GROUP_AllPlayers:
    return all_enum(nGroup, wUnitType, pCtx, add_resources);
  case GROUP_Force1:
  case GROUP_Force2:
  case GROUP_Force3:
  case GROUP_Force4:
    return group_enum(nGroup, wUnitType, pCtx, add_resources);
  case GROUP_CurrentPlayer:
    nGroup = TriggerPlayer();
    break;
  case GROUP_Unused16:
  case GROUP_Unused17:
  case GROUP_Unused18:
  case GROUP_Unused19:
    return 0;
  }

  if (PlayerIsLobby(nGroup)) {  // Hyperion: nGroup < 8 in 1.17
    switch (wUnitType) {
    case 0:
    case 2:
      dword_526460[nGroup] += dwValue;
      gPlayerMinerals[nGroup] += dwValue;
      if (wUnitType == 0)
        break;
    case 1:
      dword_526430[nGroup] += dwValue;
      gPlayerGas[nGroup] += dwValue;
      break;
    }
  }
  return 0;
}

// Adds to one of a player's score counters, or splits it across several.
int __fastcall add_score(DWORD nGroup, WORD wUnitType, void *pCtx)
{
  DWORD dwValue = (DWORD)(DWORD_PTR)pCtx;

  if (nGroup & TRIG_SLOT_TAG)  // Hyperion: a slot an enumerator passed, not a group code
    nGroup &= ~TRIG_SLOT_TAG;
  else
  switch (nGroup) {
  case GROUP_NonAlliedVictory:
    return nava_enum(nGroup, wUnitType, pCtx, add_score);
  case GROUP_Foes:
    return foes_enum(nGroup, wUnitType, pCtx, add_score);
  case GROUP_Allies:
    return allies_enum(nGroup, wUnitType, pCtx, add_score);
  case GROUP_NeutralPlayers:
    return neutrals_enum(nGroup, wUnitType, pCtx, add_score);
  case GROUP_AllPlayers:
    return all_enum(nGroup, wUnitType, pCtx, add_score);
  case GROUP_Force1:
  case GROUP_Force2:
  case GROUP_Force3:
  case GROUP_Force4:
    return group_enum(nGroup, wUnitType, pCtx, add_score);
  case GROUP_CurrentPlayer:
    nGroup = TriggerPlayer();
    break;
  case GROUP_Unused16:
  case GROUP_Unused17:
  case GROUP_Unused18:
  case GROUP_Unused19:
    return 0;
  }

  if (PlayerIsLobby(nGroup)) {  // Hyperion: nGroup < 8 in 1.17
    switch (wUnitType) {
    case 7:
      glGameCounts_g[27][nGroup] += dwValue;
      break;
    case 1:
      glGameCounts_g[5][nGroup] += dwValue;
      break;
    case 2:
      glGameCounts_g[12][nGroup] += dwValue;
      break;
    case 4:
      glGameCounts_g[6][nGroup] += dwValue;
      break;
    case 5:
      glGameCounts_g[13][nGroup] += dwValue;
      break;
    case 3:
      glGameCounts_g[5][nGroup] += (dwValue + 1) / 2;
      glGameCounts_g[12][nGroup] += (dwValue + 1) / 2;
      break;
    case 6:
      glGameCounts_g[6][nGroup] += (dwValue + 1) / 2;
      glGameCounts_g[13][nGroup] += (dwValue + 1) / 2;
      break;
    case 0:
      glGameCounts_g[5][nGroup] += (dwValue + 3) / 4;
      glGameCounts_g[12][nGroup] += (dwValue + 3) / 4;
      glGameCounts_g[6][nGroup] += (dwValue + 3) / 4;
      glGameCounts_g[13][nGroup] += (dwValue + 3) / 4;
      break;
    }
  }
  return 0;
}

// Sets the trigger player's alliance status toward a player or group.
int __fastcall Trig_SetAlliance(DWORD nGroup, WORD wStatus, void *pCtx)
{
  if (nGroup >= 0x1B && !(nGroup & TRIG_SLOT_TAG))  // Hyperion: a tagged slot is not past the codes
    return 0;

  if (nGroup & TRIG_SLOT_TAG)  // Hyperion: a slot an enumerator passed, not a group code
    nGroup &= ~TRIG_SLOT_TAG;
  else
  switch (nGroup) {
  case GROUP_NonAlliedVictory:
    return nava_enum(nGroup, wStatus, pCtx, Trig_SetAlliance);
  case GROUP_Foes:
    return foes_enum(nGroup, wStatus, pCtx, Trig_SetAlliance);
  case GROUP_Allies:
    return allies_enum(nGroup, wStatus, pCtx, Trig_SetAlliance);
  case GROUP_NeutralPlayers:
    return neutrals_enum(nGroup, wStatus, pCtx, Trig_SetAlliance);
  case GROUP_AllPlayers:
    return all_enum(nGroup, wStatus, pCtx, Trig_SetAlliance);
  case GROUP_Force1:
  case GROUP_Force2:
  case GROUP_Force3:
  case GROUP_Force4:
    return group_enum(nGroup, wStatus, pCtx, Trig_SetAlliance);
  case GROUP_CurrentPlayer:
    return 0;
  case GROUP_Unused16:
  case GROUP_Unused17:
  case GROUP_Unused18:
  case GROUP_Unused19:
    return 0;
  }

  if (nGroup != (DWORD)TriggerPlayer()) {
    BYTE bAlliance;
    switch (wStatus) {
    case 1:
      bAlliance = 1;
      break;
    case 2:
      bAlliance = 2;
      break;
    default:
      bAlliance = 0;
      break;
    }
    sgbAlliances_g[TriggerPlayer()][nGroup] = bAlliance;
    // The oracle compares against the trigger player's own diagonal entry.
    if (bAlliance && bAlliance != sgbAlliances_g[TriggerPlayer()][TriggerPlayer()]) {
      for (int i = 0; i < PlayerSlots(); i++) {  // Hyperion: 12 in 1.17
        if (sgbAlliances_g[TriggerPlayer()][i])
          sgbAlliances_g[TriggerPlayer()][i] = bAlliance;
      }
    }
  }
  return 0;
}

// Trig_GetTextDisplayTime.
//
// The display time act_text (0x4B6300) hands to map_msg_default: 50 ms per character,
// floored at 4000. A NULL string returns 0. The max() macro evaluates strlen twice, as 1.17
// does; a function or a local computes it once. The comparison is unsigned (`4000u`).
#define TRIG_MAX(a, b) ((a) > (b) ? (a) : (b))

int __fastcall Trig_GetTextDisplayTime(const char *pszText)
{
  if (pszText == NULL)
    return 0;

  return (int)TRIG_MAX(4000u, strlen(pszText) * 50);
}



// Creates a unit at the centre of a region for a player or group, then applies
// one of the 64 stored unit-property records.
// The two early returns keep pUnit in ECX and the EBX save inside the player path.
// noinline keeps this spelling out of act_create_unit, which the oracle calls.
// Indexing gMapRegions directly keeps the oracle's top-then-bottom load order;
// a region pointer loads bottom first.
__declspec(noinline) int __fastcall
Trig_CreateUnitWithProperties(DWORD dwGroup, WORD wUnitType, void *pCtx)
{
  TrigCreateProps *pProps = (TrigCreateProps *)pCtx;

  if (dwGroup & TRIG_SLOT_TAG)  // Hyperion: a slot an enumerator passed, not a group code
    dwGroup &= ~TRIG_SLOT_TAG;
  else
  switch (dwGroup) {
  case GROUP_NonAlliedVictory:
    return nava_enum(dwGroup, wUnitType, pCtx, Trig_CreateUnitWithProperties);
  case GROUP_Foes:
    return foes_enum(dwGroup, wUnitType, pCtx, Trig_CreateUnitWithProperties);
  case GROUP_Allies:
    return allies_enum(dwGroup, wUnitType, pCtx, Trig_CreateUnitWithProperties);
  case GROUP_NeutralPlayers:
    return neutrals_enum(dwGroup, wUnitType, pCtx, Trig_CreateUnitWithProperties);
  case GROUP_AllPlayers:
    return all_enum(dwGroup, wUnitType, pCtx, Trig_CreateUnitWithProperties);
  case GROUP_Force1:
  case GROUP_Force2:
  case GROUP_Force3:
  case GROUP_Force4:
    return group_enum(dwGroup, wUnitType, pCtx, Trig_CreateUnitWithProperties);
  case GROUP_CurrentPlayer:
    dwGroup = TriggerPlayer();
    break;
  case GROUP_Unused16:
  case GROUP_Unused17:
  case GROUP_Unused18:
  case GROUP_Unused19:
    return 0;
  }

  if (!PlayerIsLobby(dwGroup))  // Hyperion: >= 8 in 1.17
    return 0;
  if (IsTypeNeutralResource(wUnitType))
    dwGroup = 11;
  CUnit *pUnit = trigger_create_unit(wUnitType,
                                     (gMapRegions[pProps->dwRegion].left + gMapRegions[pProps->dwRegion].right) / 2,
                                     (gMapRegions[pProps->dwRegion].top + gMapRegions[pProps->dwRegion].bottom) / 2,
                                     dwGroup);
  if (!pUnit)
    return 0;
  if (pProps->dwValue && pProps->dwValue <= 64)
    apply_unit_properties(pUnit, reinterpret_cast<chk_unit_props_t *>(
                                     &sgUnitProps_g[(pProps->dwValue - 1) * 20]));
  return 0;
}

// sub_4B71F0's filter, built on TrigFindFirstUnitAt's stack. Recursion into
// passengers and subunits copies it with the height test switched off.
struct TrigFindCtx {
  /*0x00*/ DWORD dwGroup;       // mov ecx, dword ptr [edi]
  /*0x04*/ WORD  wUnitType;     // movzx esi, word ptr [edi+4h]
  /*0x06*/ WORD  wHeightMask;   // mov dx, word ptr [edi+6h]
  /*0x08*/ DWORD bCheckHeight;  // cmp dword ptr [edi+8h], 0h
};
static_assert(sizeof(TrigFindCtx)                 == 0x0C, "ctx size (sub esp,14h minus the rect)");
static_assert(offsetof(TrigFindCtx, dwGroup)      == 0x00, "ctx::dwGroup      [ebp-14h]");
static_assert(offsetof(TrigFindCtx, wUnitType)    == 0x04, "ctx::wUnitType    [ebp-10h]");
static_assert(offsetof(TrigFindCtx, wHeightMask)  == 0x06, "ctx::wHeightMask  [ebp-0Eh]");
static_assert(offsetof(TrigFindCtx, bCheckHeight) == 0x08, "ctx::bCheckHeight [ebp-0Ch]");

// Returns the first unit in a map region that matches a group and unit type.
CUnit *__fastcall TrigFindFirstUnitAt(WORD wUnitType, DWORD nGroup,
                                      TrigRegionId region)
{
  rect16_t    area;
  TrigFindCtx ctx;
  BYTE        nRegion = region.bRegion;

  const MapRegion *pRegion = &gMapRegions[nRegion];
  area.left   = static_cast<short>(pRegion->left);
  area.right  = static_cast<short>(pRegion->right);
  area.top    = static_cast<short>(pRegion->top);
  area.bottom = static_cast<short>(pRegion->bottom);
  ctx.wUnitType    = wUnitType;
  ctx.dwGroup      = nGroup;
  ctx.bCheckHeight = 1;
  ctx.wHeightMask = gMapRegions[nRegion].wFlags;

  return find_foreach_unit(&area, sub_4B71F0, &ctx);
}

// TrigFindFirstUnitAt's visitor: nonzero stops the sweep on a match, searching
// a unit's subunit and passengers too.
int __fastcall sub_4B71F0(CUnit *pUnit, void *pCtx)
{
  TrigFindCtx *pFind = static_cast<TrigFindCtx *>(pCtx);

  if (pFind->bCheckHeight && TrigEnum_HeightMatches(pUnit, pFind->wHeightMask))
    return 0;

  TrigFindCtx inner = *pFind;
  inner.bCheckHeight = 0;

  if ((gulUnitTypeFlags[pUnit->wType] & 8) && pUnit->pPowerup
      && sub_4B71F0(pUnit->pPowerup, &inner))
    return 1;

  if (!owned_count(pFind->dwGroup, 0, pUnit))
    return 0;

  if (pUnit->IsATransport()) {
    DWORD nHold = gubUnitCargoHold[pUnit->wType];
    for (DWORD i = 0; i < nHold; i++) {
      if (pUnit->wCargoUnitIds[i] != 0) {
        CUnit *pCargo = pUnit->GetTransportUnit(i);
        if (pCargo && sub_4B71F0(pCargo, &inner))
          return 1;
      }
    }
  }

  BYTE bInfo = gubUnitCountInfo[pUnit->wType];
  switch (pFind->wUnitType) {
  case UNITCODE_AnyUnit:
    break;
  case UNITCODE_Men:
    if (!(bInfo & UCI_Men))
      return 0;
    break;
  case UNITCODE_Buildings:
    if (!(bInfo & UCI_Building))
      return 0;
    break;
  case UNITCODE_Factories:
    if (!(bInfo & UCI_Factory))
      return 0;
    break;
  default:
    if (pUnit->wType != pFind->wUnitType)
      return 0;
    break;
  }
  return 1;
}

// Finds the first matching unit in a region and returns its position, or the
// region's centre when there is none.
CUnit *__fastcall sub_4B7330(WORD wUnitType, DWORD dwGroup, BYTE bRegion,
                             TrigPoint *pPt)
{
  TrigRegionId region;
  region.bRegion = bRegion;
  CUnit *pUnit = TrigFindFirstUnitAt(wUnitType, dwGroup, region);

  if (pUnit != NULL) {
    pPt->x = PosS(pUnit->pSprite->wPosX);
    pPt->y = pUnit->pSprite->wPosY;
  } else {
    pPt->x = (gMapRegions[bRegion].left + gMapRegions[bRegion].right) / 2;
    pPt->y = (gMapRegions[bRegion].top + gMapRegions[bRegion].bottom) / 2;
  }
  return pUnit;
}

// Kills or removes up to nLimit of a nation's units that pass pfnCond.
void __fastcall sub_4B73A0(DWORD nNation, WORD wUnitType, KillRemoveCondCb pfnCond,
                           int nLimit)
{
  CUnit *pUnit = CUnit::GetPlayerUnitList(nNation);

  while (pUnit != NULL) {
    if (!(gulUnitTypeFlags[pUnit->wType] & 0x10) && !pUnit->IsDeadOrDying()
        && pfnCond(nNation, wUnitType, pUnit)) {
      CUnit *pVictim = pUnit;
      pUnit = pUnit->pPlayerUnitNext;
      if ((gulUnitTypeFlags[pVictim->wType] & 0x800) && pVictim->pUnionD4)
        static_cast<CUnit *>(pVictim->pUnionD4)->DestroyInventory();
      else {
        if (sgbRemoveUnit_g)
          pVictim->Hide();
        pVictim->Suicide();
      }
      if (--nLimit == 0)
        break;
    } else
      pUnit = pUnit->pPlayerUnitNext;
  }
}

// act_invincible's visitor: sets, clears or toggles invulnerability on each
// matching unit and its passengers.
int __fastcall trigact_set_invincibility_cb(CUnit *pUnit, void *pCtx)
{
  TrigInvincibleCtx *pInv = static_cast<TrigInvincibleCtx *>(pCtx);

  if (pInv->dwFlag && TrigEnum_HeightMatches(pUnit, pInv->wHeightMask))
    return 0;
  if (!owned_count(pInv->dwGroup, 0, pUnit))
    return 0;
  if (pUnit->IsATransport()) {
    DWORD nHold = gubUnitCargoHold[pUnit->wType];
    TrigInvincibleCtx inner = *pInv;
    inner.dwFlag = 0;
    for (DWORD i = 0; i < nHold; i++) {
      if (pUnit->wCargoUnitIds[i] != 0) {
        CUnit *pCargo = pUnit->GetTransportUnit(i);
        if (pCargo)
          trigact_set_invincibility_cb(pCargo, &inner);
      }
    }
  }

  WORD wType = pUnit->wType;
  BYTE bInfo = gubUnitCountInfo[wType];
  switch (pInv->wUnitType) {
  case UNITCODE_AnyUnit:
    break;
  case UNITCODE_Men:
    if (!(bInfo & UCI_Men))
      return 0;
    break;
  case UNITCODE_Buildings:
    if (!(bInfo & UCI_Building))
      return 0;
    break;
  case UNITCODE_Factories:
    if (!(bInfo & UCI_Factory))
      return 0;
    break;
  default:
    if (wType != pInv->wUnitType)
      return 0;
    break;
  }

  switch (pInv->bEnable) {
  case 4:
    pUnit->dwFlags |= UF_Invincible;
    break;
  case 5:
    if (!(gulUnitTypeFlags[wType] & UTF_Invincible))
      pUnit->dwFlags &= ~UF_Invincible;
    break;
  case 6:
    if (pUnit->dwFlags & UF_Invincible) {
      if (!(gulUnitTypeFlags[wType] & UTF_Invincible))
        pUnit->dwFlags &= ~UF_Invincible;
    } else
      pUnit->dwFlags |= UF_Invincible;
    break;
  }
  return 0;
}

// CUnit::SetOrder_xy_interrupt as TrigOrderUnit calls it: the order goes over as
// the byte's stack home pushed whole, which only a BYTE parameter allows.
typedef void (CUnit::*SetOrderXyIntW)(BYTE bOrderId, int x, int y, BOOL bInterrupt);

// What CUnit::dwUnion134 points at for TrigOrderUnit: a kind byte and a position.
// Only these three fields are read.
// Hyperion: the record is a TGuard (TAI_GuardPost.h), bKind its bType and wX
// and wY its wPosX and wPosY. The view holds TGuard's two links and its unit
// pointer so that the fields stay on TGuard's on x64 too, as asserted below
//.
struct TrigUnitAi {
  /*0x00*/ void *_links[2];
  /*0x08*/ BYTE bKind;              // cmp byte ptr [ecx+8h], 1h
  /*0x09*/ BYTE _unread_09[3];
  /*0x0C*/ void *_unread_0C;
  /*0x10*/ WORD _unread_10;
  /*0x12*/ upos_t wX;               // mov word ptr [ecx+12h], ax; TGuard's position, 4 bytes on x64 (stage E)
  /*0x14*/ upos_t wY;               // mov word ptr [ecx+14h], ax
};
OC_X86_LAYOUT(offsetof(TrigUnitAi, bKind) == 0x08, "ai::bKind [ecx+8h]");
OC_X86_LAYOUT(offsetof(TrigUnitAi, wX)    == 0x12, "ai::wX    [ecx+12h]");
OC_X86_LAYOUT(offsetof(TrigUnitAi, wY)    == 0x14, "ai::wY    [ecx+14h]");
static_assert(offsetof(TrigUnitAi, bKind) == offsetof(TGuard, bType), "TrigUnitAi is TGuard");
static_assert(offsetof(TrigUnitAi, wX) == offsetof(TGuard, wPosX), "TrigUnitAi is TGuard");
static_assert(offsetof(TrigUnitAi, wY) == offsetof(TGuard, wPosY), "TrigUnitAi is TGuard");

// act_order's visitor: issues the action's move, patrol or attack order to each
// matching unit that can take it.
int __fastcall TrigOrderUnit(CUnit *pUnit, void *pCtx)
{
  TrigOrderCtx *pOrder = static_cast<TrigOrderCtx *>(pCtx);

  if (TrigEnum_HeightMatches(pUnit, static_cast<WORD>(pOrder->nHeightMask)))
    return 0;
  if (!owned_count(pOrder->dwGroup, 0, pUnit))
    return 0;
  WORD wType = pUnit->wType;
  if ((gulUnitTypeFlags[wType] & 1)
      && (pUnit->bOrderId == 0x47 || pUnit->bOrderId == 0x48))
    return 0;
  if (!(pUnit->dwFlags & 0x20000))
    return 0;
  if (wType == 0x0E)
    return 0;
  if (pUnit->IsFighterType())
    return 0;
  if (pUnit->IsRescuable())
    return 0;

  BYTE bInfo = gubUnitCountInfo[pUnit->wType];
  switch (pOrder->nUnitType) {
  case UNITCODE_AnyUnit:
    break;
  case UNITCODE_Men:
    if (!(bInfo & UCI_Men))
      return 0;
    break;
  case UNITCODE_Buildings:
    if (!(bInfo & UCI_Building))
      return 0;
    break;
  case UNITCODE_Factories:
    if (!(bInfo & UCI_Factory))
      return 0;
    break;
  default:
    if (pUnit->wType != pOrder->nUnitType)
      return 0;
    break;
  }

  BYTE bOrderId = 0x17;
  switch (pOrder->nOrder) {
  case 0:
    bOrderId = 6;
    break;
  case 1:
    bOrderId = 0x98;
    break;
  case 2:
    bOrderId = 0x0E;
    break;
  }
  if (pUnit->dwFlags & 0x10) {
    pUnit->Deburrow();
    // Hyperion: called as it is defined, the order a byte as 1.17 gave it; a direct
    // call through a pointer of another type traps in WebAssembly
    pUnit->SetOrder_xy_interrupt(bOrderId, pOrder->x, pOrder->y, FALSE);
  } else {
    if (OrderAllowed(bOrderId, pUnit->bPlayer, pUnit) != 1)
      return 0;
    pUnit->SetOrder_xy(bOrderId, pOrder->x, pOrder->y);
  }
  TrigUnitAi *pAi = reinterpret_cast<TrigUnitAi *>(pUnit->dwUnion134);
  if (pAi && (bOrderId == 6 || bOrderId == 0x0E) && pAi->bKind == 1) {
    pAi->wX = static_cast<WORD>(pOrder->x);
    pAi->wY = static_cast<WORD>(pOrder->y);
  }
  return 0;
}

// Hyperion: one unit given act_order's order toward x, y, as TrigOrderUnit gives each unit
// the action sweeps, for the live channel and the cutscenes (port_trigrun.cpp): any unit type,
// its own player as the group, every elevation.
int PortTrigOrderOne(CUnit *pUnit, int x, int y, int nOrder)
{
  TrigOrderCtx ctx;
  ctx.dwGroup = pUnit->bPlayer;
  ctx.nUnitType = UNITCODE_AnyUnit;
  ctx.nHeightMask = 0;
  ctx.x = x;
  ctx.y = y;
  ctx.nOrder = nOrder;
  TrigOrderUnit(pUnit, &ctx);
  return 1;
}

// The sweep visitor for the sextet: applies the spec's setter to each unit that
// matches, counting successes down.
int __fastcall sub_4B7750(CUnit *pUnit, void *pCtx)
{
  TrigUnitSpec *pSpec = static_cast<TrigUnitSpec *>(pCtx);

  // Both tests as all-ones masks: `neg / sbb` twice, then one `test`.
  if (!((pUnit ? -1 : 0) & (pSpec->pfnApply ? -1 : 0))) {
    SErrSetLastError(kErrInvalidParameter);
    return 0;
  }
  if (pSpec->wCount == 0)
    return 0;
  if (TrigEnum_HeightMatches(pUnit, pSpec->wHeightMask))
    return 0;
  if (!owned_count(pSpec->dwGroup, 0, pUnit))
    return 0;

  BYTE bInfo = gubUnitCountInfo[pUnit->wType];
  switch (pSpec->wUnitType) {
  case UNITCODE_AnyUnit:
    break;
  case UNITCODE_Men:
    if (!(bInfo & UCI_Men))
      return 0;
    break;
  case UNITCODE_Buildings:
    if (!(bInfo & UCI_Building))
      return 0;
    break;
  case UNITCODE_Factories:
    if (!(bInfo & UCI_Factory))
      return 0;
    break;
  default:
    if (pUnit->wType != pSpec->wUnitType)
      return 0;
    break;
  }
  if (pSpec->pfnApply(pUnit, pSpec->dwValue))
    pSpec->wCount--;
  return 0;
}

// Moves a completed building to the placement-grid spot nearest (x, y), if the
// spot is free, carrying its addon, creep and power state along. The bounds
// sit in a rect16_t, the snap reads the halves from the table and the
// footprint test from WORD locals, and the owner is read into a local before
// the test.
void __fastcall sub_4B7810(CUnit *pUnit, DWORD x, DWORD y)
{
  if (!(pUnit->dwFlags & 1))
    return;

  int nType = pUnit->wType;
  WORD wType = static_cast<WORD>(nType);
  mrect_t  box;
  box.left = x - gUnitBoundBox[wType].left;
  box.right = gUnitBoundBox[wType].right + x;
  if (box.left < 0)
    x -= box.left;
  else if (box.right >= gwMapPixWidth)
    x += gwMapPixWidth - box.right - 1;
  box.top = y - gUnitBoundBox[wType].top;
  box.bottom = gUnitBoundBox[wType].bottom + y;
  if (box.top < 0)
    y -= box.top;
  else if (box.bottom >= gwMapPixHeight)
    y += gwMapPixHeight - box.bottom - 1;

  int nHalfW = gUnitPlaceboxSize[wType].x / 2;
  x -= nHalfW;
  if (x >= gwMapPixWidth)
    x = gwMapPixWidth - 1;
  x = (x & ~0x1F) + nHalfW;
  int nHalfH = gUnitPlaceboxSize[wType].y / 2;
  y -= nHalfH;
  if (y >= gwMapPixHeight)
    y = gwMapPixHeight - 1;
  y = (y & ~0x1F) + nHalfH;

  WORD wPlaceH = gUnitPlaceboxSize[wType].y;
  WORD wPlaceW = gUnitPlaceboxSize[wType].x;
  BYTE bPlayer = pUnit->bPlayer;
  if (mtx_query_place(bPlayer, x / 32 - (short)wPlaceW / 2 / 32,
                 y / 32 - (short)wPlaceH / 2 / 32, nType, 0))
    return;
  if (!sub_424D30(pUnit->wType, x, y))
    return;

  int nOldX = PosS(pUnit->pSprite->wPosX);
  int nOldY = pUnit->pSprite->wPosY;
  if ((pUnit->dwFlags & 2) && pUnit->GetRace() == 1) {
    if (pUnit->pAddon)
      pUnit->DetachAddon();
    if (pUnit->IsConstructingAddon())
      pUnit->CancelAddon();
    if (gulUnitTypeFlags[pUnit->wType] & 2) {
      CUnit *pParent = pUnit->sub_437290();
      if (pParent)
        pParent->DetachAddon();
    }
  }
  pUnit->sub_430390(FALSE);
  pUnit->SetUnitPosition(x, y);
  pUnit->sub_430430();
  if ((pUnit->dwFlags & 2) && pUnit->GetRace() == 1)
    pUnit->sub_437180();
  sub_4B46E0(pUnit->wType, nOldX, nOldY, 1);
  if (unit_type_spreads_creep(nType, 1)) {
    CUnit::ZergPlaceBldg(nType, PosS(pUnit->pSprite->wPosX), pUnit->pSprite->wPosY);
    sub_4B4DD0(pUnit);
    pUnit->SetBackgroundOrder(0x66);
    RequestJuiceUpdate();
    return;
  }
  if (gulUnitTypeFlags[(WORD)nType] & 0x20000) {
    spread_creep_completely(nType, PosS(pUnit->pSprite->wPosX), pUnit->pSprite->wPosY);
    sub_4B4DD0(pUnit);
  }
  RequestJuiceUpdate();
}

// Moves a unit to a point, or back where it was when no free spot is found.
void __fastcall sub_4B7A80(CUnit *pUnit, spos_t x, spos_t y)
{
  mpoint_t  ptOld;
  mpoint_t  ptNew;  // Hyperion: map points
  mpoint_t  ptFree;

  ptOld.x = pUnit->pSprite->wPosX;
  ptOld.y = pUnit->pSprite->wPosY;
  ptNew.x = x;
  ptNew.y = y;
  pUnit->sub_430390(FALSE);
  pUnit->SetUnitPosition(ptNew.x, ptNew.y);
  if (!FindPlaceForUnit(pUnit, &ptNew, &ptFree, NULL, 0, 0)) {
    pUnit->SetUnitPosition(ptOld.x, ptOld.y);
    pUnit->sub_430430();
    return;
  }
  pUnit->SetUnitPosition(ptFree.x, ptFree.y);
  pUnit->sub_430430();
  if (pUnit->dwFlags & 1)
    pUnit->EndDispatchOrder();
}

// act_move_unit's visitor: moves each matching, movable unit to the centre of
// the destination region and stops when the count runs out.
int __fastcall trigact_moveunit_cb(CUnit *pUnit, void *pCtx)
{
  TrigMoveUnitCtx *pMove = static_cast<TrigMoveUnitCtx *>(pCtx);

  if (TrigEnum_HeightMatches(pUnit, pMove->wHeightMask))
    return 0;
  if (pUnit->IsDeadOrDying())
    return 0;
  if (!owned_count(pMove->dwGroup, 0, pUnit))
    return 0;
  WORD wType = pUnit->wType;
  if ((gulUnitTypeFlags[wType] & 1)
      && (pUnit->bOrderId == 0x47 || pUnit->bOrderId == 0x48))
    return 0;

  if (wType != 0x0E && wType != 0xCA && !pUnit->IsNeutralResource()) {
    WORD wKind = pUnit->wType;
    if (!(gulUnitTypeFlags[wKind] & 0x2000) && !pUnit->IsDoodad()) {
      BYTE bInfo = gubUnitCountInfo[wKind];
      switch (pMove->wUnitType) {
      case UNITCODE_AnyUnit:
        break;
      case UNITCODE_Men:
        if (!(bInfo & UCI_Men))
          return 0;
        break;
      case UNITCODE_Buildings:
        if (!(bInfo & UCI_Building))
          return 0;
        break;
      case UNITCODE_Factories:
        if (!(bInfo & UCI_Factory))
          return 0;
        break;
      default:
        if (wKind != pMove->wUnitType)
          return 0;
        break;
      }
      // Both arms spell the centre in full; MSVC hoists the shared pushes.
      if (!(pUnit->dwFlags & 2))
        sub_4B7A80(pUnit,
                   (gMapRegions[pMove->bDestRegion].left + gMapRegions[pMove->bDestRegion].right) / 2,
                   (gMapRegions[pMove->bDestRegion].top + gMapRegions[pMove->bDestRegion].bottom) / 2);
      else
        sub_4B7810(pUnit,
                   (gMapRegions[pMove->bDestRegion].left + gMapRegions[pMove->bDestRegion].right) / 2,
                   (gMapRegions[pMove->bDestRegion].top + gMapRegions[pMove->bDestRegion].bottom) / 2);
      return --dword_6C4C0C == 0;
    }
  }
  return 0;
}

// sub_4B7D00's context, built on sub_4B7C70's stack.
struct TrigKillCtx {
  /*0x00*/ KillRemoveCondCb pfnCond;      // mov eax, dword ptr [ebx] / call eax
  /*0x04*/ DWORD            nNation;      // mov ecx, dword ptr [ebx+4h]
  /*0x08*/ WORD             wUnitType;    // mov dx, word ptr [ebx+8h]
  /*0x0A*/ WORD             wHeightMask;  // mov dx, word ptr [ebx+0Ah]
  /*0x0C*/ DWORD            bCheckHeight; // cmp dword ptr [ebx+0Ch], 0h
};
OC_X86_LAYOUT(sizeof(TrigKillCtx)                 == 0x10, "ctx size (sub esp,18h minus the rect)");
static_assert(offsetof(TrigKillCtx, pfnCond)      == 0x00, "ctx::pfnCond      [ebp-18h]");
OC_X86_LAYOUT(offsetof(TrigKillCtx, nNation)      == 0x04, "ctx::nNation      [ebp-14h]");
OC_X86_LAYOUT(offsetof(TrigKillCtx, wUnitType)    == 0x08, "ctx::wUnitType    [ebp-10h]");
OC_X86_LAYOUT(offsetof(TrigKillCtx, wHeightMask)  == 0x0A, "ctx::wHeightMask  [ebp-0Eh]");
OC_X86_LAYOUT(offsetof(TrigKillCtx, bCheckHeight) == 0x0C, "ctx::bCheckHeight [ebp-0Ch]");

// Kills or removes up to nLimit units in a region that pass pfnCond.
void __fastcall sub_4B7C70(DWORD nNation, WORD wUnitType, KillRemoveCondCb pfnCond,
                           DWORD nRegion, int nLimit)
{
  rect16_t    area;
  TrigKillCtx ctx;

  const MapRegion *pRegion = &gMapRegions[nRegion];
  area.left   = static_cast<short>(pRegion->left);
  area.right  = static_cast<short>(pRegion->right);
  area.top    = static_cast<short>(pRegion->top);
  area.bottom = static_cast<short>(pRegion->bottom);
  ctx.nNation      = nNation;
  ctx.wUnitType    = wUnitType;
  ctx.bCheckHeight = 1;
  ctx.pfnCond      = pfnCond;
  ctx.wHeightMask  = gMapRegions[nRegion].wFlags;
  dword_6C4C04 = nLimit;
  dword_6C4C08 = 0;

  find_foreach_unit(&area, sub_4B7D00, &ctx);
}

// sub_4B7C70's visitor: kills or removes a matching unit, its passengers and its
// subunit, and stops when the limit reaches zero.
int __fastcall sub_4B7D00(CUnit *pUnit, void *pCtx)
{
  TrigKillCtx *pKill = static_cast<TrigKillCtx *>(pCtx);

  if (!pKill->bCheckHeight || !TrigEnum_HeightMatches(pUnit, pKill->wHeightMask)) {
    TrigKillCtx inner = *pKill;
    inner.bCheckHeight = 0;

    if (pUnit->IsATransport()) {
      DWORD nHold = gubUnitCargoHold[pUnit->wType];
      for (DWORD i = 0; i < nHold; i++) {
        if (pUnit->wCargoUnitIds[i] != 0) {
          CUnit *pCargo = pUnit->GetTransportUnit(i);
          if (pCargo) {
            dword_6C4C08 = 1;
            sub_4B7D00(pCargo, &inner);
            dword_6C4C08 = 0;
          }
        }
      }
    }
    if (gulUnitTypeFlags[pUnit->wType] & 8) {
      CUnit *pSubunit = pUnit->pPowerup;
      if (pSubunit) {
        dword_6C4C08 = 1;
        sub_4B7D00(pSubunit, &inner);
        dword_6C4C08 = 0;
      }
    }

    if (!pUnit->IsDeadOrDying()
        && pKill->pfnCond(pKill->nNation, pKill->wUnitType, pUnit)) {
      if ((gulUnitTypeFlags[pUnit->wType] & 0x800) && pUnit->pUnionD4)
        static_cast<CUnit *>(pUnit->pUnionD4)->DestroyInventory();
      else if (pUnit->IsATransport()) {
        if (sgbRemoveUnit_g)
          pUnit->SelfDestruct();
        else
          pUnit->Suicide();
      } else {
        if (sgbRemoveUnit_g)
          pUnit->Hide();
        pUnit->Suicide();
      }
      if (!dword_6C4C08)
        dword_6C4C04--;
      return dword_6C4C04 == 0;
    }
  }
  return 0;
}

// Sets a player's death count, or every member of a player group's.
int __fastcall set_deaths(DWORD nGroup, WORD wUnitType, void *pCtx)
{
  DWORD dwValue = (DWORD)(DWORD_PTR)pCtx;

  // Hyperion: a slot an enumerator passed, not a group code; an EUD map's
  // player is its own value, bit 16 and all, and an EUD game has no slot past 11
  if ((nGroup & TRIG_SLOT_TAG) && !EudGame())
    nGroup &= ~TRIG_SLOT_TAG;
  else
  switch (nGroup) {
  case GROUP_NonAlliedVictory:
    return nava_enum(nGroup, wUnitType, pCtx, set_deaths);
  case GROUP_Foes:
    return foes_enum(nGroup, wUnitType, pCtx, set_deaths);
  case GROUP_Allies:
    return allies_enum(nGroup, wUnitType, pCtx, set_deaths);
  case GROUP_NeutralPlayers:
    return neutrals_enum(nGroup, wUnitType, pCtx, set_deaths);
  case GROUP_AllPlayers:
    return all_enum(nGroup, wUnitType, pCtx, set_deaths);
  case GROUP_Force1:
  case GROUP_Force2:
  case GROUP_Force3:
  case GROUP_Force4:
    return group_enum(nGroup, wUnitType, pCtx, set_deaths);
  case GROUP_CurrentPlayer:
    nGroup = TriggerPlayer();
    break;
  case GROUP_Unused16:
  case GROUP_Unused17:
  case GROUP_Unused18:
  case GROUP_Unused19:
    return 0;
  }

  if (EudGame())  // Hyperion: an EUD game writes through 1.16.1's addresses
    return EudSetDeaths(nGroup, wUnitType, dwValue, EUD_SET);
  if (PlayerIsLobby(nGroup) && wUnitType < 0xE9) {  // Hyperion: nGroup < 8 in 1.17
    switch (wUnitType) {
    case UNITCODE_AnyUnit:
      break;
    case UNITCODE_Men:
      glGameCounts_g[3][nGroup] = dwValue;
      break;
    case UNITCODE_Buildings:
      glGameCounts_g[10][nGroup] = dwValue;
      break;
    case UNITCODE_Factories:
      glGameCounts_g[16][nGroup] = dwValue;
      break;
    default:
      if (wUnitType < 0xE4)
        glCounts_g[3][wUnitType][nGroup] = dwValue;
      break;
    }
  }
  return 0;
}

// The per-unit setters. Four of the five bodies below are
// `int __stdcall (CUnit *, int)` and set_owner_inside is not -- see
// src117/h/TrigAct.h's two typedefs for both ABIs.

// The second parameter is `int`, but the arithmetic inside two of them is
// unsigned, and the division by 100 settles it:
// `mov eax, 51EB851Fh / mul ecx / shr edx, 5h` is the unsigned multiply and the
// unsigned shift, where the signed /100 sequence adds a three-instruction sign
// correction the oracle has none of. So the two bodies that divide carry an
// explicit `(DWORD)` at the multiply, while the parameter stays `int` as the
// shared SetUnitPropCb typedef requires.

// Only the first guard of each body calls SErrSetLastError: each body's later
// guards jump backward into the middle of the first guard's failure block, to
// the `xor eax,eax` at +0x12, past the `push 57h / call SErrSetLastError` at
// +0x0B. So a null unit sets ERROR_INVALID_PARAMETER and a wrong-type unit
// silently returns 0, and writing the natural
// `{ SErrSetLastError(...); return 0; }` for every guard is a single-byte delta.

// set_energy.
//
// 1.17's bug, kept: only the low byte of max*percent/100 survives, so any max energy above
// 255 wraps (at 400, a 100% "Set Energy" yields 144), and the clamp after it compares the
// wrapped value (docs/open-issues.md). The clamp is signed and calls GetMaxEnergyReal() a
// third time; the clamped variable is `int`, only its initialiser cast to BYTE.
int __stdcall set_energy(CUnit *pUnit, int nValue)
{
  if (pUnit == NULL) {
    SErrSetLastError(kErrInvalidParameter);
    return 0;
  }
  if (!(gulUnitTypeFlags[pUnit->wType] & UTF_UsesEnergy))
    return 0;
  if (pUnit->dwFlags & UF_Hallucination)
    return 0;

  int nEnergy = (BYTE)(pUnit->GetMaxEnergyReal() * (DWORD)nValue / 100);
  if (pUnit->GetMaxEnergyReal() < nEnergy)
    nEnergy = pUnit->GetMaxEnergyReal();
  pUnit->wEnergy = (WORD)(nEnergy << 8);
  return 1;
}

// set_hangar.
//
// Fills a carrier's or reaver's hangar by creating nValue fighters outright. The type gate
// is a `||` chain of four equality tests in 1.17's order 0x48 / 0x52 / 0x53 / 0x51 --
// Carrier, Reaver, Hero Reaver, Hero Carrier -- not a switch.

// The TTAllowed failure is a `break`, not a `continue`, and the displacement
// says so: `cmp eax,1 / jne` at +0x5A targets +0xA5, the `pop edi` that retires
// the loop's register save, while the CreateUnitAt-returned-null test targets
// +0x9F, which is `inc ebx` and really is a continue. EBX is also the return
// value, so a negative nValue returns 0 where `nValue != 0` would return 1, and
// status_set_update() is called unconditionally, at the merge point.

// The two byte locals for the owner are forced by the codegen: the TTAllowed
// site needs one, because the oracle splits the read into
// `mov al, byte ptr [esi+4Ch]` ... `movzx edi, al`, and the CreateUnitAt site
// needs a second, declared after the sprite pointer, because reusing the first
// parks it in EDI across the call. What they prove is a shape, not Blizzard's
// variable names.
int __stdcall set_hangar(CUnit *pUnit, int nValue)
{
  if (pUnit == NULL) {
    SErrSetLastError(kErrInvalidParameter);
    return 0;
  }
  if (pUnit->wType != 0x48 && pUnit->wType != 0x52 &&
      pUnit->wType != 0x53 && pUnit->wType != 0x51)
    return 0;

  int i;
  for (i = 0; i < nValue; i++) {
    BYTE bOwner = pUnit->bPlayer;
    if (TTAllowed(pUnit->GetHangarType(), bOwner, pUnit) != 1)
      break;

    CSprite *pSprite = pUnit->pSprite;
    BYTE bMaker = pUnit->bPlayer;
    upos_t x = pSprite->wPosX;
    upos_t y = pSprite->wPosY;
    CUnit *pFighter = CreateUnitAt(pUnit->GetHangarType(), PosS(x), PosS(y),
                                   bMaker);
    if (pFighter != NULL) {
      pFighter->FinishedMaking();
      pFighter->pHangarParent = pUnit;
      pUnit->HangarConstructionFinished(pFighter);
    }
  }
  status_set_update();
  return i != 0;
}

// Sets a unit's hit points to a percentage of its type's maximum, at least 1.
// The whole-hp maximum is unused. Three identical arms keep its `sar` and merge
// after allocation; two arms fold earlier, and SetLife inside the arms moves the
// tail's registers.
int __stdcall set_health(CUnit *pUnit, int nValue)
{
  if (pUnit == NULL) {
    SErrSetLastError(kErrInvalidParameter);
    return 0;
  }

  // Hyperion: a share of the maximum at the owner's handicap
  const LONG lMax = HandicapLife(gxUnitLife[pUnit->wType], pUnit->bPlayer);
  int nMaxHp = lMax >> 8;
  int nLife;
  if (nMaxHp > 0)
    nLife = lMax * (DWORD)nValue / 100;
  else if (nMaxHp == 0)
    nLife = lMax * (DWORD)nValue / 100;
  else
    nLife = lMax * (DWORD)nValue / 100;
  pUnit->SetLife(nLife ? nLife : 1);
  if ((pUnit->dwFlags & UF_Hallucination) || pUnit->wType == 0x28) {
    WORD wMax = pUnit->GetMaxTimeToLive();
    if (pUnit->wTimeToLive == 0 || wMax < pUnit->wTimeToLive)
      pUnit->wTimeToLive = wMax;
  }
  return 1;
}

// Gives a unit, its passengers, fighters, addon and nuke to a player.
// The player goes through a local copy: with the parameter itself the mask test
// takes its operands in the other order.
int __stdcall set_owner(CUnit *pUnit, int nPlayer)
{
  int nOwner = nPlayer;
  if (nOwner & TRIG_SLOT_TAG)  // Hyperion: a slot past 11, not a group code: 13 is "current player"
    nOwner &= ~TRIG_SLOT_TAG;
  else if (nOwner == GROUP_CurrentPlayer)
    nOwner = TriggerPlayer();
  if (!(((DWORD)nOwner < (DWORD)PlayerSlots() ? -1 : 0) & (pUnit ? -1 : 0))) {  // Hyperion: 12 in 1.17
    SErrSetLastError(kErrInvalidParameter);
    return 0;
  }
  if (pUnit->dwFlags & UF_Hallucination)
    return 0;
  if (pUnit->IsNeutralResource())
    return 0;
  if (pUnit->wType == 0x49 || pUnit->wType == 0x55)
    return 0;
  if (gulUnitTypeFlags[pUnit->wType] & 2)
    return 0;
  if (pUnit->bPlayer == nOwner)
    return 0;

  if (pUnit->IsRefinery()) {
    CUnit *pWorker = pUnit->sub_423F90();
    if (pWorker)
      pWorker->GiveToPlayer(nOwner);
  }
  pUnit->GiveToPlayer(nOwner);
  if (pUnit->IsATransport())
    pUnit->IterateTransport(set_owner_inside,
                            reinterpret_cast<CUnit *>((intptr_t)nOwner));
  if (pUnit->IsCarrierType())
    pUnit->FighterDoFcn(set_owner_inside,
                        reinterpret_cast<CUnit *>((intptr_t)nOwner));
  if (pUnit->dwFlags & 2) {
    if (pUnit->pAddon)
      pUnit->pAddon->GiveToPlayer(nOwner);
    if (pUnit->IsConstructingAddon()) {
      CUnit *pBuilt = pUnit->GetConstructionTarget();
      pBuilt->GiveToPlayer(nOwner);
    }
    if (pUnit->wType == 0x86 && pUnit->pUnionD0)
      pUnit->pUnionD0->GiveToPlayer(nOwner);
  }
  return 1;
}

// set_owner_inside.
//
// Not a spec callback (src117/h/TrigAct.h): the per-cargo-unit visitor set_owner (0x4B81A0)
// hands to CUnit::IterateTransport and CUnit::FighterDoFcn, __fastcall with the cargo unit
// in ECX and the caller's parameter in EDX. It returns 0, the iterators' "keep going", so
// every passenger is given away.
int __fastcall set_owner_inside(CUnit *pUnit, CUnit *pContext)
{
  pUnit->GiveToPlayer((int)reinterpret_cast<intptr_t>(pContext));
  return 0;
}

// Sets ore, gas or both in a player's balance and gathered total.
int __fastcall set_resources(DWORD nGroup, WORD wUnitType, void *pCtx)
{
  DWORD dwValue = (DWORD)(DWORD_PTR)pCtx;

  if (nGroup & TRIG_SLOT_TAG)  // Hyperion: a slot an enumerator passed, not a group code
    nGroup &= ~TRIG_SLOT_TAG;
  else
  switch (nGroup) {
  case GROUP_NonAlliedVictory:
    return nava_enum(nGroup, wUnitType, pCtx, set_resources);
  case GROUP_Foes:
    return foes_enum(nGroup, wUnitType, pCtx, set_resources);
  case GROUP_Allies:
    return allies_enum(nGroup, wUnitType, pCtx, set_resources);
  case GROUP_NeutralPlayers:
    return neutrals_enum(nGroup, wUnitType, pCtx, set_resources);
  case GROUP_AllPlayers:
    return all_enum(nGroup, wUnitType, pCtx, set_resources);
  case GROUP_Force1:
  case GROUP_Force2:
  case GROUP_Force3:
  case GROUP_Force4:
    return group_enum(nGroup, wUnitType, pCtx, set_resources);
  case GROUP_CurrentPlayer:
    nGroup = TriggerPlayer();
    break;
  case GROUP_Unused16:
  case GROUP_Unused17:
  case GROUP_Unused18:
  case GROUP_Unused19:
    return 0;
  }

  if (PlayerIsLobby(nGroup)) {  // Hyperion: nGroup < 8 in 1.17
    switch (wUnitType) {
    case 0:
    case 2:
      gPlayerMinerals[nGroup] = dwValue;
      dword_526460[nGroup] = dwValue;
      if (wUnitType == 0)
        break;
    case 1:
      gPlayerGas[nGroup] = dwValue;
      dword_526430[nGroup] = dwValue;
      break;
    }
  }
  return 0;
}

// set_resources_unit.
//
// gulUnitTypeFlags bit 0x2000, the units.dat "resource container" bit, gates it; its
// failure is a bare `return 0`. The argument is truncated to 16 bits in the source, a WORD
// load feeding the WORD store.
int __stdcall set_resources_unit(CUnit *pUnit, int nValue)
{
  if (pUnit == NULL) {
    SErrSetLastError(kErrInvalidParameter);
    return 0;
  }
  if (!(gulUnitTypeFlags[pUnit->wType] & UTF_ResourceContainer))
    return 0;

  pUnit->wResourceAmount = (WORD)nValue;
  if (pUnit->IsMineralSrc())
    pUnit->UpdateResourceAnim();
  return 1;
}

// Sets one of a player's score counters, or splits a value across several.
int __fastcall set_score(DWORD nGroup, WORD wUnitType, void *pCtx)
{
  DWORD dwValue = (DWORD)(DWORD_PTR)pCtx;

  if (nGroup & TRIG_SLOT_TAG)  // Hyperion: a slot an enumerator passed, not a group code
    nGroup &= ~TRIG_SLOT_TAG;
  else
  switch (nGroup) {
  case GROUP_NonAlliedVictory:
    return nava_enum(nGroup, wUnitType, pCtx, set_score);
  case GROUP_Foes:
    return foes_enum(nGroup, wUnitType, pCtx, set_score);
  case GROUP_Allies:
    return allies_enum(nGroup, wUnitType, pCtx, set_score);
  case GROUP_NeutralPlayers:
    return neutrals_enum(nGroup, wUnitType, pCtx, set_score);
  case GROUP_AllPlayers:
    return all_enum(nGroup, wUnitType, pCtx, set_score);
  case GROUP_Force1:
  case GROUP_Force2:
  case GROUP_Force3:
  case GROUP_Force4:
    return group_enum(nGroup, wUnitType, pCtx, set_score);
  case GROUP_CurrentPlayer:
    nGroup = TriggerPlayer();
    break;
  case GROUP_Unused16:
  case GROUP_Unused17:
  case GROUP_Unused18:
  case GROUP_Unused19:
    return 0;
  }

  if (PlayerIsLobby(nGroup)) {  // Hyperion: nGroup < 8 in 1.17
    switch (wUnitType) {
    case 7:
      glGameCounts_g[27][nGroup] = dwValue;
      break;
    case 1:
      glGameCounts_g[5][nGroup] = dwValue;
      break;
    case 2:
      glGameCounts_g[12][nGroup] = dwValue;
      break;
    case 4:
      glGameCounts_g[6][nGroup] = dwValue;
      break;
    case 5:
      glGameCounts_g[13][nGroup] = dwValue;
      break;
    case 3:
      glGameCounts_g[5][nGroup] = glGameCounts_g[12][nGroup] = (dwValue + 1) / 2;
      break;
    case 6:
      glGameCounts_g[6][nGroup] = (dwValue + 1) / 2;
      glGameCounts_g[13][nGroup] = (dwValue + 1) / 2;
      break;
    case 0:
      glGameCounts_g[6][nGroup] = (dwValue + 3) / 4;
      glGameCounts_g[13][nGroup] = (dwValue + 3) / 4;
      glGameCounts_g[12][nGroup] = (dwValue + 3) / 4;
      glGameCounts_g[5][nGroup] = (dwValue + 3) / 4;
      break;
    }
  }
  return 0;
}

// set_shields.
//
// The gate is a BYTE truth test on gbUnitHasShields, the unit type read once for both
// tables. The clamp is a signed `if (nMax < nShields)`, in that operand order. Unlike
// set_energy, it does not reject 0x40000000 units; whether that is intended is not known,
// and it is kept.
int __stdcall set_shields(CUnit *pUnit, int nValue)
{
  if (pUnit == NULL) {
    SErrSetLastError(kErrInvalidParameter);
    return 0;
  }
  if (!gbUnitHasShields[pUnit->wType])
    return 0;

  int nMax = HandicapShields(guwUnitEnergy[pUnit->wType], pUnit->bPlayer);  // Hyperion: at the owner's handicap
  int nShields = nMax * (DWORD)nValue / 100;
  if (nMax < nShields)
    nShields = nMax;
  pUnit->lShields = nShields << 8;
  return 1;
}

// Subtracts from a player's death count, flooring at zero.
int __fastcall sub_deaths(DWORD nGroup, WORD wUnitType, void *pCtx)
{
  DWORD dwValue = (DWORD)(DWORD_PTR)pCtx;

  // Hyperion: a slot an enumerator passed, not a group code; an EUD map's
  // player is its own value, bit 16 and all, and an EUD game has no slot past 11
  if ((nGroup & TRIG_SLOT_TAG) && !EudGame())
    nGroup &= ~TRIG_SLOT_TAG;
  else
  switch (nGroup) {
  case GROUP_NonAlliedVictory:
    return nava_enum(nGroup, wUnitType, pCtx, sub_deaths);
  case GROUP_Foes:
    return foes_enum(nGroup, wUnitType, pCtx, sub_deaths);
  case GROUP_Allies:
    return allies_enum(nGroup, wUnitType, pCtx, sub_deaths);
  case GROUP_NeutralPlayers:
    return neutrals_enum(nGroup, wUnitType, pCtx, sub_deaths);
  case GROUP_AllPlayers:
    return all_enum(nGroup, wUnitType, pCtx, sub_deaths);
  case GROUP_Force1:
  case GROUP_Force2:
  case GROUP_Force3:
  case GROUP_Force4:
    return group_enum(nGroup, wUnitType, pCtx, sub_deaths);
  case GROUP_CurrentPlayer:
    nGroup = TriggerPlayer();
    break;
  case GROUP_Unused16:
  case GROUP_Unused17:
  case GROUP_Unused18:
  case GROUP_Unused19:
    return 0;
  }

  if (EudGame())  // Hyperion: an EUD game writes through 1.16.1's addresses
    return EudSetDeaths(nGroup, wUnitType, dwValue, EUD_SUB);
  if (PlayerIsLobby(nGroup) && wUnitType < 0xE9) {  // Hyperion: nGroup < 8 in 1.17
    switch (wUnitType) {
    case UNITCODE_AnyUnit:
      break;
    case UNITCODE_Men:
      if (glGameCounts_g[3][nGroup] > dwValue)
        glGameCounts_g[3][nGroup] -= dwValue;
      else
        glGameCounts_g[3][nGroup] = 0;
      break;
    case UNITCODE_Buildings:
      if (glGameCounts_g[10][nGroup] > dwValue)
        glGameCounts_g[10][nGroup] -= dwValue;
      else
        glGameCounts_g[10][nGroup] = 0;
      break;
    case UNITCODE_Factories:
      if (glGameCounts_g[16][nGroup] > dwValue)
        glGameCounts_g[16][nGroup] -= dwValue;
      else
        glGameCounts_g[16][nGroup] = 0;
      break;
    default:
      if (wUnitType < 0xE4) {
        if (glCounts_g[3][wUnitType][nGroup] > dwValue)
          glCounts_g[3][wUnitType][nGroup] -= dwValue;
        else
          glCounts_g[3][wUnitType][nGroup] = 0;
      }
      break;
    }
  }
  return 0;
}

// Subtracts ore, gas or both from a player's balance, flooring at zero.
int __fastcall sub_resources(DWORD nGroup, WORD wUnitType, void *pCtx)
{
  DWORD dwValue = (DWORD)(DWORD_PTR)pCtx;

  if (nGroup & TRIG_SLOT_TAG)  // Hyperion: a slot an enumerator passed, not a group code
    nGroup &= ~TRIG_SLOT_TAG;
  else
  switch (nGroup) {
  case GROUP_NonAlliedVictory:
    return nava_enum(nGroup, wUnitType, pCtx, sub_resources);
  case GROUP_Foes:
    return foes_enum(nGroup, wUnitType, pCtx, sub_resources);
  case GROUP_Allies:
    return allies_enum(nGroup, wUnitType, pCtx, sub_resources);
  case GROUP_NeutralPlayers:
    return neutrals_enum(nGroup, wUnitType, pCtx, sub_resources);
  case GROUP_AllPlayers:
    return all_enum(nGroup, wUnitType, pCtx, sub_resources);
  case GROUP_Force1:
  case GROUP_Force2:
  case GROUP_Force3:
  case GROUP_Force4:
    return group_enum(nGroup, wUnitType, pCtx, sub_resources);
  case GROUP_CurrentPlayer:
    nGroup = TriggerPlayer();
    break;
  case GROUP_Unused16:
  case GROUP_Unused17:
  case GROUP_Unused18:
  case GROUP_Unused19:
    return 0;
  }

  if (PlayerIsLobby(nGroup)) {  // Hyperion: nGroup < 8 in 1.17
    switch (wUnitType) {
    case 0:
    case 2:
      if ((DWORD)gPlayerMinerals[nGroup] > dwValue)
        gPlayerMinerals[nGroup] -= dwValue;
      else
        gPlayerMinerals[nGroup] = 0;
      if (wUnitType == 0)
        break;
    case 1:
      if ((DWORD)gPlayerGas[nGroup] > dwValue)
        gPlayerGas[nGroup] -= dwValue;
      else
        gPlayerGas[nGroup] = 0;
      break;
    }
  }
  return 0;
}

// Subtracts from one of a player's score counters, flooring at zero.
int __fastcall sub_score(DWORD nGroup, WORD wUnitType, void *pCtx)
{
  DWORD dwValue = (DWORD)(DWORD_PTR)pCtx;

  if (nGroup & TRIG_SLOT_TAG)  // Hyperion: a slot an enumerator passed, not a group code
    nGroup &= ~TRIG_SLOT_TAG;
  else
  switch (nGroup) {
  case GROUP_NonAlliedVictory:
    return nava_enum(nGroup, wUnitType, pCtx, sub_score);
  case GROUP_Foes:
    return foes_enum(nGroup, wUnitType, pCtx, sub_score);
  case GROUP_Allies:
    return allies_enum(nGroup, wUnitType, pCtx, sub_score);
  case GROUP_NeutralPlayers:
    return neutrals_enum(nGroup, wUnitType, pCtx, sub_score);
  case GROUP_AllPlayers:
    return all_enum(nGroup, wUnitType, pCtx, sub_score);
  case GROUP_Force1:
  case GROUP_Force2:
  case GROUP_Force3:
  case GROUP_Force4:
    return group_enum(nGroup, wUnitType, pCtx, sub_score);
  case GROUP_CurrentPlayer:
    nGroup = TriggerPlayer();
    break;
  case GROUP_Unused16:
  case GROUP_Unused17:
  case GROUP_Unused18:
  case GROUP_Unused19:
    return 0;
  }

  if (PlayerIsLobby(nGroup)) {  // Hyperion: nGroup < 8 in 1.17
    switch (wUnitType) {
    case 7:
      if (dwValue > glGameCounts_g[27][nGroup])
        glGameCounts_g[27][nGroup] = 0;
      else
        glGameCounts_g[27][nGroup] -= dwValue;
      break;
    case 1:
      if (dwValue > glGameCounts_g[5][nGroup])
        glGameCounts_g[5][nGroup] = 0;
      else
        glGameCounts_g[5][nGroup] -= dwValue;
      break;
    case 2:
      if (dwValue > glGameCounts_g[12][nGroup])
        glGameCounts_g[12][nGroup] = 0;
      else
        glGameCounts_g[12][nGroup] -= dwValue;
      break;
    case 4:
      if (dwValue > glGameCounts_g[6][nGroup])
        glGameCounts_g[6][nGroup] = 0;
      else
        glGameCounts_g[6][nGroup] -= dwValue;
      break;
    case 5:
      if (dwValue > glGameCounts_g[13][nGroup])
        glGameCounts_g[13][nGroup] = 0;
      else
        glGameCounts_g[13][nGroup] -= dwValue;
      break;
    case 3:
      if ((dwValue + 1) / 2 > glGameCounts_g[5][nGroup])
        glGameCounts_g[5][nGroup] = 0;
      else
        glGameCounts_g[5][nGroup] -= (dwValue + 1) / 2;
      if ((dwValue + 1) / 2 > glGameCounts_g[12][nGroup])
        glGameCounts_g[12][nGroup] = 0;
      else
        glGameCounts_g[12][nGroup] -= (dwValue + 1) / 2;
      break;
    case 6:
      if ((dwValue + 1) / 2 > glGameCounts_g[6][nGroup])
        glGameCounts_g[6][nGroup] = 0;
      else
        glGameCounts_g[6][nGroup] -= (dwValue + 1) / 2;
      if ((dwValue + 1) / 2 > glGameCounts_g[13][nGroup])
        glGameCounts_g[13][nGroup] = 0;
      else
        glGameCounts_g[13][nGroup] -= (dwValue + 1) / 2;
      break;
    case 0:
      if ((dwValue + 3) / 4 > glGameCounts_g[5][nGroup])
        glGameCounts_g[5][nGroup] = 0;
      else
        glGameCounts_g[5][nGroup] -= (dwValue + 3) / 4;
      if ((dwValue + 3) / 4 > glGameCounts_g[12][nGroup])
        glGameCounts_g[12][nGroup] = 0;
      else
        glGameCounts_g[12][nGroup] -= (dwValue + 3) / 4;
      if ((dwValue + 3) / 4 > glGameCounts_g[6][nGroup])
        glGameCounts_g[6][nGroup] = 0;
      else
        glGameCounts_g[6][nGroup] -= (dwValue + 3) / 4;
      if ((dwValue + 3) / 4 > glGameCounts_g[13][nGroup])
        glGameCounts_g[13][nGroup] = 0;
      else
        glGameCounts_g[13][nGroup] -= (dwValue + 3) / 4;
      break;
    }
  }
  return 0;
}

// TrigAct_KillRemoveAnyUnit_Cond.
//
// The unit's BYTE bPlayer compared against the DWORD nation. wUnitType is not read -- this
// predicate accepts every unit of the nation -- but it is declared, because all five
// predicates are assigned to one variable in TrigAct_KillRemoveUnits and share one type.
int __fastcall TrigAct_KillRemoveAnyUnit_Cond(DWORD nNation, WORD wUnitType,
                                              CUnit *pUnit)
{
  (void)wUnitType;
  return pUnit->bPlayer == nNation;
}

// TrigAct_KillRemoveBuildings_Cond.
//
// The nation test and a units.dat flag test (0x10) joined with `&&`. The explicit `!= 0`
// on the flag tests of the three predicates changes no byte; it is for readability under
// /wd4800.
int __fastcall TrigAct_KillRemoveBuildings_Cond(DWORD nNation, WORD wUnitType,
                                                CUnit *pUnit)
{
  (void)wUnitType;
  return pUnit->bPlayer == nNation
      && (gubUnitCountInfo[pUnit->wType] & UCI_Building) != 0;
}

// TrigAct_KillRemoveFactories_Cond.
// Identical to Buildings but for the mask immediate (0x20).
int __fastcall TrigAct_KillRemoveFactories_Cond(DWORD nNation, WORD wUnitType,
                                                CUnit *pUnit)
{
  (void)wUnitType;
  return pUnit->bPlayer == nNation
      && (gubUnitCountInfo[pUnit->wType] & UCI_Factory) != 0;
}

// TrigAct_KillRemoveUnits. The dispatcher,
// and its own callback.
//
// __fastcall: nGroup in ECX, wUnitType in DX, nRegion and nLimit on the stack. A jump table
// covers groups 0x0D..0x1A; any other value is a nation. wUnitType is a WORD (act_kill_atloc
// sets only DX). nRegion 0xFF, compared unsigned, means "no location": sweep the whole nation.

// It passes its own address: each pseudo-group case hands
// TrigAct_KillRemoveUnits to the matching *_enum_count, which re-enters it once
// per concrete nation with that nation in ECX, and that recursion is how "kill
// all Foes' units" works. The two casts are type plumbing, TrigEnum.h spelling
// the enumerators' third parameter `void *` and their callback `NationCountCb`.

// Jump-table slots 9..12 (groups 0x16..0x19) point at the `xor eax,eax / pop
// esi / pop ebp / ret 8h` tail at 0x4B8DB9, so those four groups return 0 and do
// nothing -- but they must still be spelled as real cases, being inside the
// table's range and so unable to reach `default`, which falls through to the
// nation path. The case order is the oracle's, not numeric: 0x1A, 0x0E, 0x0F,
// 0x10, 0x11, 0x12-0x15, then 0x0D, the same order all five count_* dispatchers
// in src117/lang/TrigEnum.cpp carry.
int __fastcall TrigAct_KillRemoveUnits(DWORD nGroup, WORD wUnitType,
                                       DWORD_PTR nRegion, int nLimit)
{
  KillRemoveCondCb pfnCond;

  if (nGroup & TRIG_SLOT_TAG)  // Hyperion: a slot an enumerator passed, not a group code
    nGroup &= ~TRIG_SLOT_TAG;
  else
  switch (nGroup) {
  case GROUP_NonAlliedVictory:
    return nava_enum_count(nGroup, wUnitType, (void *)nRegion,
                           (NationCountCb)TrigAct_KillRemoveUnits, nLimit);
  case GROUP_Foes:
    return foes_enum_count(nGroup, wUnitType, (void *)nRegion,
                           (NationCountCb)TrigAct_KillRemoveUnits, nLimit);
  case GROUP_Allies:
    return allies_enum_count(nGroup, wUnitType, (void *)nRegion,
                             (NationCountCb)TrigAct_KillRemoveUnits, nLimit);
  case GROUP_NeutralPlayers:
    return neutrals_enum_count(nGroup, wUnitType, (void *)nRegion,
                               (NationCountCb)TrigAct_KillRemoveUnits, nLimit);
  case GROUP_AllPlayers:
    return all_enum_count(nGroup, wUnitType, (void *)nRegion,
                          (NationCountCb)TrigAct_KillRemoveUnits, nLimit);
  case GROUP_Force1:
  case GROUP_Force2:
  case GROUP_Force3:
  case GROUP_Force4:
    return group_enum_count(nGroup, wUnitType, (void *)nRegion,
                            (NationCountCb)TrigAct_KillRemoveUnits, nLimit);
  case GROUP_CurrentPlayer:
    nGroup = TriggerPlayer();
    break;
  case GROUP_Unused16:
  case GROUP_Unused17:
  case GROUP_Unused18:
  case GROUP_Unused19:
    return 0;
  default:
    break;
  }

  switch (wUnitType) {
  case UNITCODE_AnyUnit:   pfnCond = TrigAct_KillRemoveAnyUnit_Cond;   break;
  case UNITCODE_Men:       pfnCond = TrigAct_KillRemoveMen_Cond;       break;
  case UNITCODE_Buildings: pfnCond = TrigAct_KillRemoveBuildings_Cond; break;
  case UNITCODE_Factories: pfnCond = TrigAct_KillRemoveFactories_Cond; break;
  default:                 pfnCond = TrigAct_KillRemoveType_Cond;      break;
  }

  if (nRegion < 0xFF)
    sub_4B7C70(nGroup, wUnitType, pfnCond, (DWORD)nRegion, nLimit);
  else
    sub_4B73A0(nGroup, wUnitType, pfnCond, nLimit);
  return 0;
}

// TrigAct_KillRemoveMen_Cond. Mask 0x08.
int __fastcall TrigAct_KillRemoveMen_Cond(DWORD nNation, WORD wUnitType,
                                          CUnit *pUnit)
{
  (void)wUnitType;
  return pUnit->bPlayer == nNation
      && (gubUnitCountInfo[pUnit->wType] & UCI_Men) != 0;
}

// TrigAct_KillRemoveType_Cond.
//
// The one predicate that reads argument 2, with a 16-bit compare against CUnit::wType,
// so the family's parameter is a WORD.
int __fastcall TrigAct_KillRemoveType_Cond(DWORD nNation, WORD wUnitType,
                                           CUnit *pUnit)
{
  return pUnit->bPlayer == nNation && pUnit->wType == wUnitType;
}

// act_none. The no-op action handler: returns 1.
// It sits in this TU by its address.
int __fastcall act_none(void)
{
  return 1;
}
