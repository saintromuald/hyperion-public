// SAI_Scripts.h -- the AI script interpreter: the bytecode reader, the script
// list, its save/load and the opcode numbers. Bodies are in SAI_Scripts.cpp.
//
// Class members belong in the class header; this file owns the free functions,
// and every free function the .cpp defines is declared here.
//
// Free functions are spelled without an explicit __fastcall: /Gr already makes
// it the default.

#ifndef OC117_SAI_SCRIPTS_H
#define OC117_SAI_SCRIPTS_H

#include "platform.h"
#include "rect32.h"     // TScript::rcArea, copied with one 16-byte movdqu
#include "aipools.h"    // Hyperion: AI_TOWNS_STORE

struct TTown;           // TScript::pTown; the record lives in SAI_Build.h
class CUnit;            // aiscript_find_caster's return and aiscript_nuke_create's argument

// The bits packed into TScript +0x30. A bitfield and not a mask word: the
// oracle stores bit 0 with MSVC's bitfield idiom (`setnz al` / `xor eax,
// [esi+30h]` / `and eax,1` / `xor [esi+30h], eax`) and the town index with
// `and ecx,7` / `shl edx,3` / `or`, neither of which a hand-written mask
// produces.
union TScriptFlags {
    DWORD dwAll;              // aiscript_run_panic clears the whole word at once
    struct {
        unsigned bBWScript : 1;   // bit 0  selects gpBWScript_g over gpAIScript_g
        unsigned bFlag2    : 1;   // bit 1  no body reads it
        unsigned bStop     : 1;   // bit 2  sai_scripts_update: `test byte ptr [ecx+30h], 4h`
        unsigned nTown     : 29;  // bits 3+  aiscript_start_expansion writes the town id here
    };
};
static_assert(sizeof(TScriptFlags) == 4, "TScriptFlags is one dword at TScript+0x30");

// The script object, 0x34 bytes: sai_scripts_init passes 0x6A1A18 to
// TSaveNode<TScript,100>::Init and sai_scripts_remove_town reads the list field
// at +0x1450 of that same object, so the 100-element array in front of it is
// 0x1450 bytes.
struct TScript {
    /*0x00*/ TScript *pNext;        // sai_scripts_remove_town relinks [eax] and [eax+4]
    /*0x04*/ TScript *pPrev;        // aiscript_new_thread: `mov [eax+4h], esi`
                                    // where EAX is the old head
    /*0x08*/ DWORD dwOffset;        // the reader's cursor; aiscript_get_byte
                                    // advances it by 1 and get_word by 2
    /*0x0C*/ DWORD dwWait;          // sai_scripts_update: `lea eax,[edx-1h]`
                                    // stored back to [ecx+0Ch]
    /*0x10*/ int nPlayer;           // aiscript_expand indexes gPlayerMinerals with it
    /*0x14*/ rect32_t rcArea;       // `movdqu xmmword ptr [esi+14h], xmm0`
    /*0x24*/ int nCenterX;          // left + (right-left)/2
    /*0x28*/ int nCenterY;          // top + (bottom-top)/2
    /*0x2C*/ TTown *pTown;          // aiscript_expand: `mov ecx,[esi+2Ch]` into
                                    // town_get_base_id
    union {
        /*0x30*/ TScriptFlags flags;
        /*0x30*/ BYTE bFlags;   // the reader's spelling; the same byte
    };
};

static_assert(offsetof(TScript, pNext)    == 0x00, "TScript.pNext");
OC_X86_LAYOUT(offsetof(TScript, pPrev)    == 0x04, "TScript.pPrev");
OC_X86_LAYOUT(offsetof(TScript, dwOffset) == 0x08, "TScript.dwOffset");
OC_X86_LAYOUT(offsetof(TScript, dwWait)   == 0x0C, "TScript.dwWait");
OC_X86_LAYOUT(offsetof(TScript, nPlayer)  == 0x10, "TScript.nPlayer");
OC_X86_LAYOUT(offsetof(TScript, rcArea)   == 0x14, "TScript.rcArea");
OC_X86_LAYOUT(offsetof(TScript, nCenterX) == 0x24, "TScript.nCenterX");
OC_X86_LAYOUT(offsetof(TScript, nCenterY) == 0x28, "TScript.nCenterY");
OC_X86_LAYOUT(offsetof(TScript, pTown)    == 0x2C, "TScript.pTown");
OC_X86_LAYOUT(offsetof(TScript, flags)    == 0x30, "TScript.flags");
OC_X86_LAYOUT(offsetof(TScript, bFlags)   == 0x30, "TScript.bFlags -- the same byte");
OC_X86_LAYOUT(sizeof(TScript) == 0x34, "TScript stride (TSaveNode list field at +0x1450 / 100)");

// The pool TSaveNode<TScript,100> manages, at 0x6A1A18. Its extent is stated by
// sai_read_script_data, which decompresses 0x1454 bytes into it, and the free
// head at +0x1450 is what aiscript_new_thread pops from. An explicit
// specialization, spelled as the ledger spells it.
template <class T, int N> struct TSaveNode;
template <> struct TSaveNode<TScript,100> {
    /*0x0000*/ TScript  items[AI_TOWNS_STORE];  // Hyperion: 100 in 1.17
    /*0x1450*/ TScript *pFree;
    void Init();
    void PostLoad();
    void PreSave();
};
typedef TSaveNode<TScript,100> TScriptPool;
static_assert(offsetof(TScriptPool, items) == 0x0000, "TScriptPool.items");
OC_X86_LAYOUT(offsetof(TScriptPool, pFree) == 0x1450, "TScriptPool.pFree [edi+1450h]");
OC_X86_LAYOUT(sizeof(TScriptPool) == 0x1454, "TScriptPool = TScript[100] + free head");

// The eight bytes at 0x6A2E6C the save codec round-trips in one call. An
// explicit specialization for the same reason as the pool above.
template <class T, int N> struct TSaveContainer;
template <> struct TSaveContainer<TScript,100> {
    /*0x00*/ TScriptPool *pPool;    // sai_scripts_init stores 0x6A1A18
    /*0x04*/ TScript     *pHead;    // the live list; the save form holds a
                                    // 1-based index instead
    void Free(TScript *pNode);
};
typedef TSaveContainer<TScript,100> TScriptMgr;
static_assert(offsetof(TScriptMgr, pPool) == 0x00, "TScriptMgr.pPool");
OC_X86_LAYOUT(offsetof(TScriptMgr, pHead) == 0x04, "TScriptMgr.pHead");
OC_X86_LAYOUT(sizeof(TScriptMgr) == 8, "TScriptMgr is the 8 bytes compress() moves");

// The two script directories. Each binary starts with a DWORD byte offset to
// its own table, and get_script_offset walks the table until a zero id. The
// two strides differ and that is the oracle's: 0x10 for the AI table
// (`mov eax,[esi+10h]` / `add esi,10h`) against 8 for the BW one.
struct TScriptEntryAI {
    /*0x00*/ DWORD dwId;
    /*0x04*/ DWORD dwOffset;
    /*0x08*/ DWORD _unknown08;   // nothing reads either dword; they are here
    /*0x0C*/ DWORD _unknown0C;   // to carry the 0x10 stride
};
static_assert(offsetof(TScriptEntryAI, dwId)     == 0x00, "TScriptEntryAI.dwId");
static_assert(offsetof(TScriptEntryAI, dwOffset) == 0x04, "TScriptEntryAI.dwOffset");
static_assert(offsetof(TScriptEntryAI, _unknown08) == 0x08, "TScriptEntryAI._unknown08");
static_assert(offsetof(TScriptEntryAI, _unknown0C) == 0x0C, "TScriptEntryAI._unknown0C");
static_assert(sizeof(TScriptEntryAI) == 0x10, "TScriptEntryAI stride (add esi,10h)");

struct TScriptEntryBW {
    /*0x00*/ DWORD dwId;
    /*0x04*/ DWORD dwOffset;
};
static_assert(offsetof(TScriptEntryBW, dwId)     == 0x00, "TScriptEntryBW.dwId");
static_assert(offsetof(TScriptEntryBW, dwOffset) == 0x04, "TScriptEntryBW.dwOffset");
static_assert(sizeof(TScriptEntryBW) == 8, "TScriptEntryBW stride (add ecx,8h)");

// 0x4932F0 (29 B). Reads one bytecode byte and advances the cursor. The return
// is a BYTE because all 38 call sites use AL and the oracle writes AL over the
// base pointer already in EAX rather than zero-extending.
// noinline because SAI_Scripts_aiscript_progress calls it at 35 sites.
__declspec(noinline) BYTE aiscript_get_byte(TScript *pScript);

// 0x493310 (32 B). The same reader two bytes wide; 58 of its 59 call sites use
// AX, which is the `movzx eax, word ptr` in the body.
WORD aiscript_get_word(TScript *pScript);

// 0x493740 (71 B). Releases both script buffers and clears both pointers.
void sai_scripts_free();

// 0x493610 (133 B) / 0x4936A0 (157 B). The save codec pair: the file in ECX
// and, for the reader, the save version in DX.
int sai_read_script_data(void *pFile, WORD wVersion);
int sai_write_script_data(void *pFile);

// 0x493800 (89 B). Loads both script binaries and empties the pool.
void sai_scripts_init();

// The rows below are this TU's own free functions that no other header of ours
// declares. sai_scripts_update is seated in SAI_Main.h, sai_scripts_remove_town
// in sai.h and run_aiscript in TrigAct.h, so they stay there.

// 0x4932A0 (77 B). The first unit of a type the player owns that has at least
// nEnergy mana and is casting one of three orders. `ret 4h`, so nEnergy is the
// stack slot; it is unsigned because the energy compare is `jb`.
CUnit *aiscript_find_caster(int nPlayer, WORD wType, DWORD nEnergy);

// 0x493450 (109 B). Gives a Ghost its nuke: creates one over the unit's sprite
// and cross-links the pair, then tail-calls register_unit_for_ai.
void aiscript_nuke_create(CUnit *pUnit);

// 0x493790 (97 B). Posts a guard at every town centre that has none.
void sai_scripts_guard_resources(int nArg0, int nArg1);

// 0x494C60 (178 B). Builds a TScript on the stack -- `sub esp, 34h` is one
// record, and `lea ecx, [ebp-34h]` hands it to aiscript_start_expansion -- so the town under
// attack gets a panic thread. `ret 8h`.
void aiscript_run_panic(int nPlayer, int nX, int nY, TTown *pTown);

// 0x494D20 (105 B). Looks a script id up in the AI table and then the BW table,
// returning its byte offset and setting *pbBWScript when the BW table won.
__declspec(noinline) DWORD get_script_offset(DWORD dwScriptId,
                                            int *pbBWScript);

// 0x494D90 (230 B). Pops a thread off the free list, links it at the head of
// the live list and initialises it. `ret 8h`.
__declspec(noinline) TScript *aiscript_new_thread(int nPlayer,
                                                 DWORD dwOffset,
                                                 const rect32_t *prcArea,
                                                 int bBWScript);

// 0x494E80 (159 B). Starts a thread over the 3x3 box around a town and tells
// the captain and guard layers about it. `ret 4h`.
void aiscript_start_expansion(TScript *pScript, int nTown, int nArg2);

// 0x494F20 (92 B). The banked-resources trigger: if the owner is rich enough,
// start an expansion thread on its town. `ret 4h`.
void aiscript_expand(TScript *pScript, int nArg1, int nArg2);

// 0x494F80 (50 B). Re-runs every area captain for one computer player.
void aiscript_reset_area_captains(int nPlayer);

// The callees this TU reaches that no header of ours declared.

// 0x493A60 (4151 B), the bytecode interpreter proper. One
// register argument; its single caller discards EAX.
void SAI_Scripts_aiscript_progress(TScript *pScript);

// 0x4B27A0 (73 B) / 0x4B22A0 (127 B), oracle TU TAI_GuardPost.cpp. `ret 4h`
// and `ret 8h`; guard_post_near's one caller takes the full EAX, guard_create_post's four
// discard it.
int guard_post_near(int nArg0, int nArg1, int nArg2);
void guard_create_post(int nArg0, int nArg1, int nArg2, int nArg3);

// The script opcodes, 0x00 to 0x70: SAI_Scripts_aiscript_progress switches on them. The
// names are the AI script language's usual ones, matched against each case's body;
// 0x5A does nothing in 1.17, so its name is the usual
// one and cannot be checked against what it does.
enum AiScriptOp {
  AISOP_GOTO = 0x00,
  AISOP_NOTOWNS_JUMP = 0x01,
  AISOP_WAIT = 0x02,
  AISOP_START_TOWN = 0x03,
  AISOP_START_AREATOWN = 0x04,
  AISOP_EXPAND = 0x05,
  AISOP_BUILD = 0x06,
  AISOP_UPGRADE = 0x07,
  AISOP_TECH = 0x08,
  AISOP_WAIT_BUILD = 0x09,
  AISOP_WAIT_BUILDSTART = 0x0A,
  AISOP_ATTACK_CLEAR = 0x0B,
  AISOP_ATTACK_ADD = 0x0C,
  AISOP_ATTACK_PREPARE = 0x0D,
  AISOP_ATTACK_DO = 0x0E,
  AISOP_WAIT_SECURE = 0x0F,
  AISOP_CAPT_EXPAND = 0x10,
  AISOP_BUILD_BUNKERS = 0x11,
  AISOP_WAIT_BUNKERS = 0x12,
  AISOP_DEFENSEBUILD_GG = 0x13,
  AISOP_DEFENSEBUILD_AG = 0x14,
  AISOP_DEFENSEBUILD_GA = 0x15,
  AISOP_DEFENSEBUILD_AA = 0x16,
  AISOP_DEFENSEUSE_GG = 0x17,
  AISOP_DEFENSEUSE_AG = 0x18,
  AISOP_DEFENSEUSE_GA = 0x19,
  AISOP_DEFENSEUSE_AA = 0x1A,
  AISOP_DEFENSECLEAR_GG = 0x1B,
  AISOP_DEFENSECLEAR_AG = 0x1C,
  AISOP_DEFENSECLEAR_GA = 0x1D,
  AISOP_DEFENSECLEAR_AA = 0x1E,
  AISOP_SEND_SUICIDE = 0x1F,
  AISOP_PLAYER_ENEMY = 0x20,
  AISOP_PLAYER_ALLY = 0x21,
  AISOP_DEFAULT_MIN = 0x22,
  AISOP_DEFAULTBUILD_OFF = 0x23,
  AISOP_STOP = 0x24,
  AISOP_SWITCH_RESCUE = 0x25,
  AISOP_MOVE_DT = 0x26,
  AISOP_DEBUG = 0x27,
  AISOP_FATAL_ERROR = 0x28,
  AISOP_ENTER_BUNKER = 0x29,
  AISOP_VALUE_AREA = 0x2A,
  AISOP_TRANSPORTS_OFF = 0x2B,
  AISOP_CHECK_TRANSPORTS = 0x2C,
  AISOP_NUKE_RATE = 0x2D,
  AISOP_MAX_FORCE = 0x2E,
  AISOP_CLEAR_COMBATDATA = 0x2F,
  AISOP_RANDOM_JUMP = 0x30,
  AISOP_TIME_JUMP = 0x31,
  AISOP_FARMS_NOTIMING = 0x32,
  AISOP_FARMS_TIMING = 0x33,
  AISOP_BUILD_TURRETS = 0x34,
  AISOP_WAIT_TURRETS = 0x35,
  AISOP_DEFAULT_BUILD = 0x36,
  AISOP_HARASS_FACTOR = 0x37,
  AISOP_START_CAMPAIGN = 0x38,
  AISOP_RACE_JUMP = 0x39,
  AISOP_REGION_SIZE = 0x3A,
  AISOP_GET_OLDPEONS = 0x3B,
  AISOP_GROUNDMAP_JUMP = 0x3C,
  AISOP_PLACE_GUARD = 0x3D,
  AISOP_WAIT_FORCE = 0x3E,
  AISOP_GUARD_RESOURCES = 0x3F,
  AISOP_CALL = 0x40,
  AISOP_RETURN = 0x41,
  AISOP_EVAL_HARASS = 0x42,
  AISOP_CREEP = 0x43,
  AISOP_PANIC = 0x44,
  AISOP_PLAYER_NEED = 0x45,
  AISOP_DO_MORPH = 0x46,
  AISOP_WAIT_UPGRADES = 0x47,
  AISOP_MULTIRUN = 0x48,
  AISOP_RUSH = 0x49,
  AISOP_SCOUT_WITH = 0x4A,
  AISOP_DEFINE_MAX = 0x4B,
  AISOP_TRAIN = 0x4C,
  AISOP_TARGET_EXPANSION = 0x4D,
  AISOP_WAIT_TRAIN = 0x4E,
  AISOP_SET_ATTACKS = 0x4F,
  AISOP_SET_GENCMD = 0x50,
  AISOP_MAKE_PATROL = 0x51,
  AISOP_GIVE_MONEY = 0x52,
  AISOP_PREP_DOWN = 0x53,
  AISOP_RESOURCES_JUMP = 0x54,
  AISOP_ENTER_TRANSPORT = 0x55,
  AISOP_EXIT_TRANSPORT = 0x56,
  AISOP_SHAREDVISION_ON = 0x57,
  AISOP_SHAREDVISION_OFF = 0x58,
  AISOP_NUKE_LOCATION = 0x59,
  AISOP_HARASS_LOCATION = 0x5A,
  AISOP_IMPLODE = 0x5B,
  AISOP_GUARD_ALL = 0x5C,
  AISOP_ENEMYOWNS_JUMP = 0x5D,
  AISOP_ENEMYRESOURCES_JUMP = 0x5E,
  AISOP_IF_DIF = 0x5F,
  AISOP_EASY_ATTACK = 0x60,
  AISOP_KILL_THREAD = 0x61,
  AISOP_KILLABLE = 0x62,
  AISOP_WAIT_FINISHATTACK = 0x63,
  AISOP_QUICK_ATTACK = 0x64,
  AISOP_JUNKYARD_DOG = 0x65,
  AISOP_FAKE_NUKE = 0x66,
  AISOP_DISRUPTION_WEB = 0x67,
  AISOP_RECALL_LOCATION = 0x68,
  AISOP_SET_RANDOMSEED = 0x69,
  AISOP_IF_OWNED = 0x6A,
  AISOP_CREATE_NUKE = 0x6B,
  AISOP_CREATE_UNIT = 0x6C,
  AISOP_NUKE_POS = 0x6D,
  AISOP_HELP_IFTROUBLE = 0x6E,
  AISOP_ALLIES_WATCH = 0x6F,
  AISOP_TRY_TOWNPOINT = 0x70,
};

#endif  // OC117_SAI_SCRIPTS_H
