// glues.cpp -- the glue-screen shell: the machinery every between-games menu screen sits
// on. Screen setup and teardown, the cursor, GRP, font and button initialisers, the
// transition start, the string table, the CD-binary loader and the panic fallback. The
// screens themselves are in gluMain.cpp, gluLogin.cpp, gluJoin.cpp, gluLoad.cpp,
// gluModem.cpp, gluRdy.cpp and gluXCmpgn.cpp. Bodies are in oracle VA order.
//
// TU membership cuts both ways and needs re-checking after a change here: a callee
// defined in this file can be spliced into a caller in this file by /Ob2, which a
// cross-TU call never is, and a body whose size is exactly caller plus callee needs its
// callee here -- glues_make_full_path is the live instance, with its two
// `static __forceinline` helpers below. No body here calls another with a real `call`;
// the intra-TU references are address-taken only.
//

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>             // memset, memmove
#include <intrin.h>              // _ReadWriteBarrier -- RunGlues
#include <new>                  // placement new for the hidden-control nodes

#include "../h/glues.h"
#include "../h/dlg_ctrl.h"      // ctrl_find_ndx, ctrl_set_text, ctrl_set_update
#include "../h/minimap.h"       // timer_create, timer_remove_node, PFN_TIMER_FN
#include "../h/TrigAct.h"       // cur_hide, cur_show
#include "../h/Storm.h"         // SMemAlloc, SStrCopy, SStrPack
#include "../h/gamesnd.h"       // sub_49E3F0
#include "../h/net_glue.h"      // NetworkMode
#include "../h/net_cmds.h"      // vid_restore_palette
#include "../h/dlg_effects.h"   // effects_init -- its only seat
#include "../h/dlg_dlgdraw.h"   // CTRLNODELIST, img_update
#include "../h/gluRdy.h"        // glues_transition_out
#include "../h/rtti.h"          // type_info::raw_name
#include "../h/dlg_flc.h"       // flc_animate
#include "../h/dlg_dlg.h"       // dlg_stats_init
#include "../h/game.h"          // CPalette::find_closest_palette_entry, init_pens
#include "../h/cur.h"           // cursor_set_image
#include "../h/palette.h"       // set_font_palette
#include "../h/save_dlg.h"      // RunEvtDispatcher
#include "../h/init.h"
#include "../h/grid.h"
#include "../h/gluMain.h"
#include "../h/uiSingle.h"
#include "../h/gluLogin.h"
#include "../h/gluCmpgn.h"
#include "../h/gluRdyT.h"
#include "../h/gluRdyZ.h"
#include "../h/gluRdyP.h"
#include "../h/gluExpCmpgn.h"
#include "../h/gluConn.h"
#include "../h/gluModem.h"
#include "../h/gluJoin.h"
#include "../h/gluCreat.h"
#include "../h/gluChat.h"
#include "../h/gluLoad.h"
#include "../h/gluBN.h"
#include "../h/snd.h"
#include "../h/gamemap.h"
#include "../port/tools/port_bench.h"  // Hyperion: PortBenchLaunch
#include "../port/gameplay/port_watchgame.h"  // Hyperion: PortWatchLaunch
#include "../port/lab/port_labworker.h"  // Hyperion: PortLabWorker, PortLabWorkerNext
#include "../port/gameplay/port_launch.h"  // Hyperion: PortLaunch
#include "../port/platform/port_paths.h"  // Hyperion: PortCdOptional
#include "../gen/globals117.h"

// External imports and callees declared in this file.

// 0x444A60 -- the loader callback glues_init_grp hands dlg_load_theme in EDX, spelled as
// src117/lang/opt_dlgs.cpp spells it.
struct GROUP;
GROUP *__fastcall ReadGroupFile(const char *pszFile, intptr_t nFlags,
                                const char *pszTag, int nLine);

// 0x444890 -- the loader callback glues_get_str hands str_load_table in EDX, spelled as
// gametype.cpp and replay.cpp spell it; only its address is taken here.
void *load_game_data(const char *pszFile, void *pBuffer,
                               DWORD *pdwSize, const char *pszTag, int nLine);

// The USER32 and KERNEL32 imports this TU reaches, read out of the oracle's own import
// directory rather than guessed from the surrounding names:
//
//     0x4EB2FC  KERNEL32.dll!GetTickCount
//     0x4EB5CC  USER32.dll!GetWindowThreadProcessId
//     0x4EB5D0  USER32.dll!EnumWindows      -- two arguments, not EnumThreadWindows
//     0x4EB5DC  USER32.dll!GetParent
//     0x4EB6A0  USER32.dll!PostMessageA     (`push 0/0/10h/esi` is WM_CLOSE)
//
// `__declspec(dllimport)` is what makes MSVC emit the oracle's six-byte call through the
// IAT slot; without it it emits a five-byte direct call to the linker's import thunk, so
// the declaration decides the bytes. The four USER32 entry points come from <windows.h>,
// which src117/h/Storm.h pulls in: a second `extern "C"` declaration with different
// parameter spellings would be a C2733. GetTickCount is declared here because winbase.h
// spells it identically.
extern "C" __declspec(dllimport) ULONG __stdcall GetTickCount(void);

// The types local to this TU, derived from the oracle's instruction stream. Field widths
// come from the instructions that read them, so each named field carries a /*0xNN*/
// comment and a static_assert, and any struct asserting offsets also asserts sizeof.

// The 12 glue-screen records at 0x503E94. Count and stride both come off two bodies that
// walk the array in the two idioms MSVC emits: glues_init_cursors counts twelve records
// of 0x518 bytes down, and glues_init_grp, glues_init and glues_init_font count up to
// 0x507BB4, which is the same twelve.
//
// szName at +0 has two readers: glues_init_cursors pushes the record base straight to
// SStrCopy as its source, and glues_get_pal_path scales a record index by 0x518 and
// pushes that, so the record base is the string either way.
//
// Hyperion: the records are gGluePalInfo's (0x503E90), read 4 bytes in, and
// gluepal_t below is the record as the image lays it. hCursor holds a pointer, so the
// x64 build's records are 0x520 bytes: gluescreen_t is packed to 4 there to keep
// hCursor at +0x104, and its tail grows by the 8 bytes the record does
//. The x86 layout is unchanged.
#if defined(_WIN64) || defined(__LP64__)
#pragma pack(push, 4)
#endif
struct gluescreen_t {
  /*0x000*/ char szName[0x104];   // the record base IS the SStrCopy source
  /*0x104*/ intptr_t  hCursor;         // `mov [esi+104h], eax` <- cur_load
  /*0x108*/ BYTE _unmapped108[0x08];
  /*0x110*/ int  dwSpawnLimited;  // `cmp dword ptr [esi+110h], 0h`
  /*0x114*/ BYTE _unmapped114[0x404 + sizeof(void *) - 4];
};
#if defined(_WIN64) || defined(__LP64__)
#pragma pack(pop)
#endif

// Hyperion: whether the screens marked for the full game are left out: in a
// spawned copy, as 1.17 does, and where the CD archives may be missing and are
// (PortCdOptional), since the one such screen, the cinematics', keeps its art there
//.
static bool GlueSpawnLimited()
{
  return IsSpawnedCopy() || (PortCdOptional() && get_cd_archive() == 0);
}

// Hyperion: one record of gGluePalInfo, the object the table is stored in:
// thirteen records, each an index at +0 and then gluescreen_t's fields. Typing the
// global with it gives the x64 build storage of its own, filled from 1.17's file
// field by field (tools/wide_globals.py).
struct gluepal_t {
  /*0x000*/ DWORD dwIndex;
  /*0x004*/ char szName[0x104];
  /*0x108*/ intptr_t hCursor;
  /*0x10C*/ BYTE _unmapped10C[0x08];
  /*0x114*/ int dwSpawnLimited;
  /*0x118*/ BYTE abPalette[0x400];
};

OC_X86_LAYOUT(sizeof(gluepal_t) == 0x518, "gluepal_t size  thirteen records in gGluePalInfo's 0x4238 bytes");
static_assert(offsetof(gluepal_t, szName) == offsetof(gluescreen_t, szName) + 4, "gluescreen_t is gluepal_t read 4 bytes in");
static_assert(offsetof(gluepal_t, hCursor) == offsetof(gluescreen_t, hCursor) + 4, "gluescreen_t is gluepal_t read 4 bytes in");
static_assert(offsetof(gluepal_t, _unmapped10C) == offsetof(gluescreen_t, _unmapped108) + 4, "gluescreen_t is gluepal_t read 4 bytes in");
static_assert(offsetof(gluepal_t, dwSpawnLimited) == offsetof(gluescreen_t, dwSpawnLimited) + 4, "gluescreen_t is gluepal_t read 4 bytes in");
static_assert(offsetof(gluepal_t, abPalette) == offsetof(gluescreen_t, _unmapped114) + 4, "gluescreen_t is gluepal_t read 4 bytes in");
static_assert(sizeof(gluepal_t) == sizeof(gluescreen_t), "the two strides agree");

OC_X86_LAYOUT(sizeof(gluescreen_t)                   == 0x518, "gluescreen_t size            glues_init_cursors: add esi, 518h");
static_assert(offsetof(gluescreen_t, szName)         == 0x000, "gluescreen_t::szName         glues_init_cursors: push esi as the SStrCopy source; glues_get_pal_path: the imul result IS the SStrCopy source");
OC_X86_LAYOUT(offsetof(gluescreen_t, hCursor)        == 0x104, "gluescreen_t::hCursor        glues_init_cursors: mov [esi+104h], eax");
OC_X86_LAYOUT(offsetof(gluescreen_t, dwSpawnLimited) == 0x110, "gluescreen_t::dwSpawnLimited glues_init_cursors: cmp dword ptr [esi+110h], 0h");

// One record of the .rdata screen table gGlueData at 0x4ED820 (27 records).
// nScreen indexes gluescreen_t[]; nId runs 0..25 in the image and no body reads it.
struct glueres_t {
  /*0x0*/ int         nId;
  /*0x4*/ int         nScreen;
  /*0x8*/ const char *pszPath;
};

OC_X86_LAYOUT(sizeof(glueres_t)            == 0x0C, "glueres_t size       lea eax,[eax+eax*2] then *4 = index * 12");
static_assert(offsetof(glueres_t, nId)     == 0x00, "glueres_t::nId       image: 0, 1, 2 .. at 0x4ED820 + 12 * i");
static_assert(offsetof(glueres_t, nScreen) == 0x04, "glueres_t::nScreen   glues_get_pal_path: imul [eax*4+4ED824h], 518h; glue_screen_activate: cmp eax, dword ptr [edx*4+4ED824h]");
static_assert(offsetof(glueres_t, pszPath) == 0x08, "glueres_t::pszPath   glues_get_path: push [eax*4+4ED828h]");

// One entry of the transition record array behind sgTransitions_g.pRecs, four bytes with
// both fields read in three bodies: +0x00 is the control's dialog index, handed straight
// to ctrl_find_ndx, whose second parameter is a `short`, and +0x02 is the edge selector,
// a signed word holding 0..3 that is sign-extended into a four-entry jump table. The
// stride is in the addressing mode, `[eax+edi*4]` and `[eax+edi*4+2]` over the same
// index, so the two words fill the entry with no room for padding.
struct transctrl_t {
  /*0x0*/ short nIndex;
  /*0x2*/ short nEdge;
};

static_assert(sizeof(transctrl_t)           == 0x04, "transctrl_t size      sub_4583C0: [eax+edi*4] / glues_transition_in_cb: [eax+edi*4+2h]");
static_assert(offsetof(transctrl_t, nIndex) == 0x00, "transctrl_t::nIndex   sub_4583C0: mov dx, word ptr [eax+edi*4] -> ctrl_find_ndx");
static_assert(offsetof(transctrl_t, nEdge)  == 0x02, "transctrl_t::nEdge    glues_transition_in_cb: movsx eax, word ptr [eax+edi*4+2h]");

// The transition state block at 0x66A288, one object of 0x90 bytes: glues_transition_in
// clears it in a single memset, and every address the four transition bodies touch
// falls inside it on a member boundary.
//
//   +0x00  pRecs    the SMemAlloc'd array, loaded before each indexing
//   +0x04  nCount   a signed word, off a `movsx` load and a signed branch
//   +0x08  bActive  dwords, scaled by 4, 20 entries up to +0x58
//   +0x58  nHome    words, scaled by 2, the same 20 a second way. Signed: the only
//                   arithmetic on it is minus ctrl_t::rc.right
//   +0x80  bOut     0 in glues_transition_in, 1 in glues_transition_out
//   +0x84  nDelay   a countdown the in-callback decrements
//   +0x88           not derived: eight bytes nothing here reads, so the 0x90 fits
//
// It has to be one struct: as two externs C++ promises bActive and pRecs cannot alias,
// so MSVC hoists the pRecs load above the prologue, where the oracle loads it after.
struct transstate_t {
  /*0x00*/ transctrl_t *pRecs;
  /*0x04*/ short        nCount;
  /*0x06*/ short        _unmapped06;
  /*0x08*/ int          bActive[20];
  /*0x58*/ short        nHome[20];
  /*0x80*/ int          bOut;
  /*0x84*/ int          nDelay;
  /*0x88*/ ctrl_t      *pHideChain;
  /*0x8C*/ CTRLNODELIST *pHidden;
};

OC_X86_LAYOUT(sizeof(transstate_t)           == 0x90, "transstate_t size       glues_transition_in: push 90h / push 0h / push 66A288h / call memset");
static_assert(offsetof(transstate_t, pRecs)  == 0x00, "transstate_t::pRecs     glues_transition_in: mov dword ptr [0x66A288], edi");
OC_X86_LAYOUT(offsetof(transstate_t, nCount) == 0x04, "transstate_t::nCount    glues_transition_in_cb: movsx eax, word ptr [0x66A28C]");
OC_X86_LAYOUT(offsetof(transstate_t, bActive)== 0x08, "transstate_t::bActive   sub_4583C0: mov dword ptr [edi*4+66A290h], 1h");
OC_X86_LAYOUT(offsetof(transstate_t, nHome)  == 0x58, "transstate_t::nHome     sub_4583C0: mov cx, word ptr [edi*2+66A2E0h]");
OC_X86_LAYOUT(offsetof(transstate_t, bOut)   == 0x80, "transstate_t::bOut      glues_transition_out: mov dword ptr [0x66A308], 1h");
OC_X86_LAYOUT(offsetof(transstate_t, nDelay) == 0x84, "transstate_t::nDelay    glues_transition_in_cb: mov eax, dword ptr [0x66A30C]");
OC_X86_LAYOUT(offsetof(transstate_t, pHideChain) == 0x88, "transstate_t::pHideChain glues_transition_out: mov esi, dword ptr [0x66A310]");
OC_X86_LAYOUT(offsetof(transstate_t, pHidden)    == 0x8C, "transstate_t::pHidden    glues_transition_out: mov esi, dword ptr [0x66A314]");

// The hidden-control list and its nodes as glues_transition_in constructs them:
// the link type's constructor zeroes both pointers, and those stores survive in
// the oracle only because a real constructor makes them. The layout is
// CTRLNODELIST's and CTRLNODE's first 12 bytes; the nodes are _HIDDENCTRL.
struct hidden_link_t {
  /*0x0*/ void *prev;
  /*0x4*/ void *next;
  hidden_link_t() : prev(NULL), next(NULL) {}
};
OC_X86_LAYOUT(sizeof(hidden_link_t)         == 0x08, "hidden_link_t size");
static_assert(offsetof(hidden_link_t, prev) == 0x00, "hidden_link_t::prev   glues_transition_in: mov dword ptr [ecx], ecx");
OC_X86_LAYOUT(offsetof(hidden_link_t, next) == 0x04, "hidden_link_t::next   glues_transition_in: mov dword ptr [ecx+4h], 0h");

// Hyperion: packed as CTRLNODELIST is (dlg_dlgdraw.h), which the list is
// read as; unpacked, the x64 build put the link at +8 where CTRLNODELIST has it
// at +4.
#pragma pack(push, 1)
struct hidden_list_t {
  /*0x0*/ DWORD         dwLinkOfs;
  /*0x4*/ hidden_link_t link;
  hidden_list_t() {
    link.prev = &link;
    dwLinkOfs = 0;
    link.next = reinterpret_cast<void *>(~reinterpret_cast<intptr_t>(&link));
  }
  static void *operator new(size_t cb) { return SMemAlloc((DWORD)cb, "new", -1, 0); }
};
#pragma pack(pop)
static_assert(offsetof(hidden_list_t, link) == offsetof(CTRLNODELIST, link), "hidden_list_t is read as CTRLNODELIST");
OC_X86_LAYOUT(sizeof(hidden_list_t)              == 0x0C, "hidden_list_t size   glues_transition_in: push 0Ch / call SMemAlloc");
static_assert(offsetof(hidden_list_t, dwLinkOfs) == 0x00, "hidden_list_t::dwLinkOfs glues_transition_in: mov dword ptr [eax], 0h");
OC_X86_LAYOUT(offsetof(hidden_list_t, link)      == 0x04, "hidden_list_t::link  glues_transition_in: lea ecx, [eax+4h]");

struct _HIDDENCTRL {
  /*0x0*/ hidden_link_t link;
  /*0x8*/ ctrl_t       *pCtrl;
};
OC_X86_LAYOUT(sizeof(_HIDDENCTRL)          == 0x0C, "_HIDDENCTRL size     glues_transition_in: push 0Ch / call SMemAlloc");
static_assert(offsetof(_HIDDENCTRL, link)  == 0x00, "_HIDDENCTRL::link    glues_transition_in: mov dword ptr [esi], 0h");
OC_X86_LAYOUT(offsetof(_HIDDENCTRL, pCtrl) == 0x08, "_HIDDENCTRL::pCtrl   glues_transition_in: mov dword ptr [esi+8h], edi");
static_assert(offsetof(_HIDDENCTRL, pCtrl) == offsetof(CTRLNODE, pCtrl), "_HIDDENCTRL is read as CTRLNODE");

// The globals this TU reaches, spelled both ways with one guard per name: a `#define`
// over OC_VA under OC_DLL_MODE, a plain `extern` otherwise. One guard per name and never
// one over the block, which would undeclare every name under it. ghWnd and gnDlgReturnVal
// are spelled as re/globals117.json spells them, and sgGamePalette_g (0x666568) comes in
// from ../gen/globals117.h rather than being re-spelled here.

// 0x503E88 -- the palette id the glue shell last uploaded, from the data sheet. A dword:
// every access is full width, and glue_screen_activate compares it against
// glueres_t::nScreen, itself a dword. Four readers agree on the width, and the store of
// -1 proves the width but not the sign. It is its own object rather than a header field
// of the array below, sitting 12 bytes under it and not adjacent.
#ifdef OC_DLL_MODE
#define sgnPrevPalId (*reinterpret_cast<int *>(OC_VA(0x503E88u)))
#else
extern int sgnPrevPalId;
#endif

// 0x503E94 -- the 12 screen records, an object rather than a pointer: the oracle reaches
// it as `add eax, 503E94h`, an immediate.
// Spelled off the registered gGluePalInfo (0x503E90, thirteen 0x518-byte
// records), whose records start 4 bytes before gluescreen_t's.
// Hyperion: typed with its record, BYTE[0x4238] in 1.17.
#ifndef OC_GGLUEPALINFO
#define OC_GGLUEPALINFO
#ifdef OC_DLL_MODE
#define gGluePalInfo (reinterpret_cast<gluepal_t *>(OC_VA(0x503E90u)))
#else
extern gluepal_t gGluePalInfo[13] OC_UNALIGNED;
#endif
#endif
#define sgGlueScreens (reinterpret_cast<gluescreen_t *>(reinterpret_cast<BYTE *>(gGluePalInfo) + 4))

// 0x4ED820 -- the .rdata screen table, an object and not a pointer. The
// registry types it BYTE[324]; keep this local spelling out of --globals.
#ifdef OC_DLL_MODE
#define gGlueData (reinterpret_cast<const glueres_t *>(OC_VA(0x4ED820u)))
#else
extern const glueres_t gGlueData[27];
#endif

// The four .rdata pack templates and the two .rdata strings, all spelled as addresses
// rather than C literals. Each carries a backslash, and a backslash in a path literal is
// a silent trap -- `"\a"` is BEL and `"dlgs\tile.grp"` is `dlgs<TAB>ile.grp`, both valid
// escapes with no diagnostic at any warning level -- so where the oracle reaches a string
// by address, the address is the safe spelling.

// 0x4EDA64 -- "\arrow.grp", glues_init_cursors' pack template.
#ifdef OC_DLL_MODE
#define sgszCursorPackFmt (reinterpret_cast<const char *>(OC_VA(0x4EDA64u)))
#else
extern const char sgszCursorPackFmt[1];
#endif

// 0x4EDA70 -- "glues.cpp", this TU's allocate and free owner tag, read out of the oracle
// image at that VA. Three readers use it: glues_get_str's str_load_table tag,
// gluestrings_exit's mem_free tag and glue_load_cd_dlg's dlg_load tag.
#ifdef OC_DLL_MODE
#define sgszGluesCpp (reinterpret_cast<const char *>(OC_VA(0x4EDA70u)))
#else
extern const char sgszGluesCpp[1];
#endif

// 0x4EDA7C -- "\tEffect.pcx", glues_init's pack template.
#ifdef OC_DLL_MODE
#define sgszEffectPackFmt (reinterpret_cast<const char *>(OC_VA(0x4EDA7Cu)))
#else
extern const char sgszEffectPackFmt[1];
#endif

// 0x4EDA8C -- "\tFont.pcx", glues_init_font's pack template.
#ifdef OC_DLL_MODE
#define sgszFontPackFmt (reinterpret_cast<const char *>(OC_VA(0x4EDA8Cu)))
#else
extern const char sgszFontPackFmt[1];
#endif

// 0x4EDA98 -- "\Dlg.grp", glues_init_grp's pack template.
#ifdef OC_DLL_MODE
#define sgszDlgGrpPackFmt (reinterpret_cast<const char *>(OC_VA(0x4EDA98u)))
#else
extern const char sgszDlgGrpPackFmt[1];
#endif

// 0x4EDAB4 -- "rez\gluAll.tbl", the glue string table's archive path.
// `mov ecx, 4EDAB4h` is 5 bytes, so the address is the value.
#ifdef OC_DLL_MODE
#define sgszGluAllTbl (reinterpret_cast<const char *>(OC_VA(0x4EDAB4u)))
#else
extern const char sgszGluAllTbl[1];
#endif

// 0x66A270 -- the latch for "the glue string table is loaded". Both readers compare the
// full dword, so it is an `int` rather than a bool.
#ifdef OC_DLL_MODE
#define sgnGlueStrsLoaded (*reinterpret_cast<int *>(OC_VA(0x66A270u)))
#else
extern int sgnGlueStrsLoaded;
#endif

// 0x66A274 -- the current glue screen index, used as a 12-byte-strided array index. A
// dword; nothing here decides the sign. It is the most-read address in this TU:
// glues_get_path, glues_get_pal_path, glue_screen_activate, CloseAllWindows,
// set_netmode_return_menu, glue_panic_fallback_fn and every do_*_dlg in the sibling TUs.
// The data sheet names it glGluesMode.
#ifdef OC_DLL_MODE
#define sgnGlueScreen (*reinterpret_cast<int *>(OC_VA(0x66A274u)))
#else
extern int sgnGlueScreen;
#endif

// 0x66A27C -- glues_init's effects image block. `push 0C0h / call SMemAlloc`
// and the loop advances 0x10 per record: 12 * 16 == 0xC0 exactly. NB the
// 16-byte element SIZE is inferred from that arithmetic, not derived from any
// field read.
#ifdef OC_DLL_MODE
#define sgpGlueEffectData (*reinterpret_cast<char **>(OC_VA(0x66A27Cu)))
#else
extern char *sgpGlueEffectData;
#endif

// 0x66A280 -- glues_init_font's font image block. `push 900h / call SMemAlloc`
// and the loop advances 0xC0 per record: 12 * 192 == 0x900 exactly. Same
// INFERRED-not-derived caveat on the element size.
#ifdef OC_DLL_MODE
#define sgpGlueFontData (*reinterpret_cast<char **>(OC_VA(0x66A280u)))
#else
extern char *sgpGlueFontData;
#endif

// 0x66A284 -- the glue dialog refcount. DWORD:
// `mov eax, dword ptr [0x66A284] ... dec eax ... mov dword ptr [...], eax`.
#ifdef OC_DLL_MODE
#define sgnGlueDlgRefs (*reinterpret_cast<int *>(OC_VA(0x66A284u)))
#else
extern int sgnGlueDlgRefs;
#endif

// 0x66A288 -- the transition state block; see transstate_t above for the
// memset-proven extent and the aliasing finding. sc-refs/sheets names
// 0x66A288 `sgTransitions_g` (glues.cpp), which is the block's first byte, so
// that name is used for the whole object; the member names are a TU-local
// derivation.
#ifdef OC_DLL_MODE
#define sgTransitions_g (*reinterpret_cast<transstate_t *>(OC_VA(0x66A288u)))
#else
extern transstate_t sgTransitions_g;
#endif

// 0x510804 -- the RTTI descriptor the hidden-control nodes are tagged with
// (.?AU_HIDDENCTRL@@).
#ifdef OC_DLL_MODE
#define gTypeInfo__HIDDENCTRL (*reinterpret_cast<type_info *>(OC_VA(0x510804u)))
#else
extern type_info gTypeInfo__HIDDENCTRL;
#endif

// 0x66A470 stru_66A470 (sheet name): the glue screen's cached backdrop, its
// bitmap descriptor and the palette that came with it. glues_clear_all memsets
// exactly 0x408 bytes from the base.
struct gluecache_t {
  /*0x000*/ bmp_t bmp;
  /*0x008*/ BYTE  aPalette[0x400];
};
OC_X86_LAYOUT(sizeof(gluecache_t) == 0x408, "glues_clear_all: push 408h / push 66A470h / call memset");
static_assert(offsetof(gluecache_t, bmp) == 0x000, "glue_init_screen_dlg copies [0x66A470] and [0x66A474] into a bmp_t");
OC_X86_LAYOUT(offsetof(gluecache_t, aPalette) == 0x008, "glue_init_screen_dlg: rep movsd from 0x66A478");

#ifdef OC_DLL_MODE
#define stru_66A470 (*reinterpret_cast<gluecache_t *>(OC_VA(0x66A470u)))
#else
extern gluecache_t stru_66A470;
#endif

// 0x66A318 -- the currently-live glue dialog. A ctrl_t *:
// glue_panic_fallback_fn loads it into ECX and calls dlg_deactivate, whose ECX
// parameter dlg_dlg.h derives as `ctrl_t *`.
#ifdef OC_DLL_MODE
#define sgpGlueDlg (*reinterpret_cast<ctrl_t **>(OC_VA(0x66A318u)))
#else
extern ctrl_t *sgpGlueDlg;
#endif

// 0x66A328 -- the 12 dialog-theme handles glues_init_grp fills. Stride 4 is in
// the walker (`add edi, 4h`), and the walk runs in lockstep with the 12-record
// screen walk, so 12 dwords.
#ifdef OC_DLL_MODE
#define sgGlueDlgThemes (reinterpret_cast<intptr_t *>(OC_VA(0x66A328u)))
#else
extern intptr_t sgGlueDlgThemes[12];
#endif

// 0x66A368 -- the install or CD root path, a NUL-terminated char buffer:
// glues_make_full_path walks it with the inline-strlen idiom and then copies that many
// bytes out of it. Its extent is not derived, nothing here bounding it, so the `[1]` is
// the minimum spelling that still decays to `char *` in both arms.
#ifdef OC_DLL_MODE
#define sgszRootPath (reinterpret_cast<char *>(OC_VA(0x66A368u)))
#else
extern char sgszRootPath[1];
#endif

// 0x66A46C -- the loaded glue string table, a dword: str_load_table's result is stored
// whole, reloaded whole for str_get_string, and handed to mem_free by gluestrings_exit.
// A separate global from the latch above, not an alias.
#ifdef OC_DLL_MODE
#define sgpGlueStrs (*reinterpret_cast<void **>(OC_VA(0x66A46Cu)))
#else
extern void *sgpGlueStrs;
#endif

// 0x66A878 through 0x66A884 -- glues_set_skip's four consecutive dword stores, in that
// address order. The first takes the literal 1 and the other three the three arguments in
// order, so the first reads as an armed flag and the rest as the payload it arms. They
// are four separate objects rather than a struct on purpose: four stores in ascending
// order are what a four-field assignment would emit too, so nothing here tells one from
// the other, and no reader of 0x66A878 is known.
#ifdef OC_DLL_MODE
#define sgnGlueSkipArmed (*reinterpret_cast<int *>(OC_VA(0x66A878u)))
#else
extern int sgnGlueSkipArmed;
#endif

#ifdef OC_DLL_MODE
#define sgnGlueSkipArg0 (*reinterpret_cast<int *>(OC_VA(0x66A87Cu)))
#else
extern int sgnGlueSkipArg0;
#endif

#ifdef OC_DLL_MODE
#define sgnGlueSkipArg1 (*reinterpret_cast<int *>(OC_VA(0x66A880u)))
#else
extern int sgnGlueSkipArg1;
#endif

#ifdef OC_DLL_MODE
#define sgnGlueSkipArg2 (*reinterpret_cast<int *>(OC_VA(0x66A884u)))
#else
extern int sgnGlueSkipArg2;
#endif

// 0x66A888 -- the tick stamp of the last button-click sound, named positionally as the
// data sheet names it. A dword, and unsigned: sub_458490, its only reader, compares the
// difference with `jb`, which is what makes GetTickCount's wrap harmless.
#ifdef OC_DLL_MODE
#define dword_66A888 (*reinterpret_cast<ULONG *>(OC_VA(0x66A888u)))
#else
extern ULONG dword_66A888;
#endif

// 0x66A88C -- the 9 button-sound ids, words: the store's `*2` scale is the element size
// and the loop closes on 9. The base is corroborated by the `mov ecx, 66A88Ch` into
// sub_49E3F0. The two bytes between its end and sgnBtnSndInit are taken for padding and
// nothing reads them.
#ifdef OC_DLL_MODE
#define sgwBtnSnd (*reinterpret_cast<short (*)[9]>(OC_VA(0x66A88Cu)))
#else
extern short sgwBtnSnd[9] OC_UNALIGNED;
#endif

// 0x66A8A0 -- the once-only guard on that fill. DWORD:
// `cmp dword ptr [0x66A8A0], 0h` / `mov dword ptr [0x66A8A0], 1h`.
#ifdef OC_DLL_MODE
#define sgnBtnSndInit (*reinterpret_cast<int *>(OC_VA(0x66A8A0u)))
#else
extern int sgnBtnSndInit;
#endif

// 0x66FD28 -- ghWnd, registered (re/globals117.json: name `ghWnd`, tu main.cpp,
// .data) but with `"type": null`. A window handle: CloseAllWindows hands the
// whole dword to GetWindowThreadProcessId's hWnd and EnumFunc compares its own
// hWnd against it. Spelled `void *` so no HWND typedef of ours can collide
// with the SDK's. gen emits only marked rows' globals, hence the local
// spelling for now (opt_dlgs.cpp:236's precedent).
#ifdef OC_DLL_MODE
#define ghWnd (*reinterpret_cast<void **>(OC_VA(0x66FD28u)))
#else
extern void *ghWnd;
#endif

// 0x6CE6DC -- gnDlgReturnVal, a `long`, spelled as dlg_dlg.cpp, gluRdy.cpp and
// opt_dlgs.cpp spell it. glue_panic_fallback_fn seeds it with -1 as dlg_run_modal seeds
// it with -3, a full 32-bit store of a negative sentinel, hence signed. The inner guard
// keeps this spelling compatible with the generated one.
#ifdef OC_DLL_MODE
  #ifndef gnDlgReturnVal
  #define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
  #endif
#else
extern LONG gnDlgReturnVal;
#endif

// glues_make_full_path's two inline helpers. Both `static __forceinline`, and both
// attributes are needed: /Ox alone refuses to inline either, each containing a call, and
// leaves a real `call` at every site, where the oracle has no such function of its own.
// There are two rather than one because branch 1 prepends a fixed two characters of the
// root and needs the counted form, while branch 2 prepends whole strings and takes its
// count from the source.

// The counted form. The oracle keeps both the source pointer and the count live across
// the memmove -- branch 1 sets them up before the strlen, which is argument setup for an
// inlined call rather than a sunk loop preheader. Straight-line source computes them
// after the call instead and MSVC then drops two pushes.
static __forceinline void prepend_n(char *pszDest, const char *pszSrc, int cch)
{
  memmove(pszDest + cch, pszDest, strlen(pszDest) + 1);
  while (cch--)
    *pszDest++ = *pszSrc++;
}

// The whole-string form is not `prepend_n(pszDest, pszSrc, strlen(pszSrc))`, and the
// difference is three bytes plus a seven-byte npad. The oracle needs the root's address
// in two registers, one the inline strlen destroys into the length and one the copy loop
// walks, and pays for it once. Reaching the source through the parameter lets
// __forceinline's constant propagation turn every use back into the address literal, so
// MSVC rematerialises it twice instead, and the resulting shift lands the copy loop on an
// alignment it fills with a npad.
//
// Copying the parameters into locals as the first statements is what stops it: `ps` then
// has one definition, live from before the strlen to the copy loop, so the strlen takes
// the register copy and the literal is materialised once.
static __forceinline void prepend_str(char *pszDest, const char *pszSrc)
{
  const char *ps = pszSrc;
  char *pd = pszDest;
  int cch = (int)strlen(ps);

  memmove(pd + cch, pd, strlen(pd) + 1);
  while (cch--)
    *pd++ = *ps++;
}

// The bodies, in oracle VA order.

// Unlinks one hidden control; the oracle splices it whole into Clear. The link
// offset is recovered at runtime and must stay a phi, as in gluBN's unlink.
static void UnlinkHiddenCtrl(_HIDDENCTRL *pNode)
{
  hidden_link_t *pl = &pNode->link;
  _HIDDENCTRL *pPrev = static_cast<_HIDDENCTRL *>(pl->prev);

  if (pPrev != NULL) {
    _HIDDENCTRL *pNext = static_cast<_HIDDENCTRL *>(pl->next);
    hidden_link_t *pnl;

    if (reinterpret_cast<intptr_t>(pNext) <= 0) {
      pnl = reinterpret_cast<hidden_link_t *>(~reinterpret_cast<intptr_t>(pNext));
    } else {
      intptr_t nOff = 0;

      if (reinterpret_cast<intptr_t>(pNext) > 0)
        nOff = reinterpret_cast<intptr_t>(pl) -
               reinterpret_cast<intptr_t>(pPrev->link.next);
      pnl = reinterpret_cast<hidden_link_t *>(reinterpret_cast<intptr_t>(pNext) +
                                              nOff);
    }
    pnl->prev = pPrev;
    static_cast<_HIDDENCTRL *>(pl->prev)->link.next = pl->next;
    pl->prev = NULL;
    pl->next = NULL;
  }
}

// 0x4573A0 (84 B). Unlinks and frees every hidden control.
void HIDDENCTRLLIST::Clear(void)
{
  for (;;) {
    _HIDDENCTRL *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkHiddenCtrl(p);
    SMemFree(p, gTypeInfo__HIDDENCTRL.raw_name(), -2, 0);
  }
}

//
// Opens pszFile out of the patch archive first and the CD archive second, sizes it,
// allocates a buffer if the caller supplied none, reads it and closes the file.
//
// It returns a value: both exits materialise EAX, one zero and one the buffer, so a
// `void` declaration cannot emit these bytes. Nothing calls it directly -- its address is
// taken once -- and for a callback the typedef decides, as it does for the arity:
// glue_load_cd_dlg hands this address to dlg_load in EDX, as a PFN_DLGLOADER, which
// returns a pointer.
//
// Each parameter's role is in the stream: ECX is the file name, EDX the caller's buffer,
// whose non-null test skips the SMemAlloc, the first stack word an optional DWORD
// out-parameter for the size, and the last two SMemAlloc's owner tag and line. The
// `ret 0Ch` on both exits confirms ECX, EDX and three stack words.
void *load_cd_binary(const char *pszFile, void *pBuffer, DWORD *pdwSize,
                     const char *pszTag, int nLine)
{
  intptr_t hFile;  // Hyperion: OpenFileEx writes a handle, `int` in 1.17

  // The `||` is short-circuit, and its polarity comes off the branch targets: a null
  // patch archive skips the first open, and a successful open skips the CD fallback.
  intptr_t hPatch = get_patch_archive();
  if ((hPatch == 0 ||
       !OpenFileEx(reinterpret_cast<intptr_t>(pszFile),
                   reinterpret_cast<intptr_t>(&hFile), 1, hPatch, 0)) &&
      !OpenFileEx(reinterpret_cast<intptr_t>(pszFile),
                  reinterpret_cast<intptr_t>(&hFile), 0, get_cd_archive(), 0)) {
    if (pdwSize != 0)
      *pdwSize = 0;
    return 0;
  }

  int cbFile = GetFileSize_gds(hFile, 0);
  if (pdwSize != 0)
    *pdwSize = cbFile;

  // The oracle pushes ESI for SMemAlloc's flags rather than an immediate 0 --
  // it knows ESI is 0 on this arm, which is exactly what the null test states.
  if (pBuffer == 0)
    pBuffer = SMemAlloc(cbFile, pszTag, nLine, 0);

  ReadFile_gds(hFile, reinterpret_cast<intptr_t>(pBuffer), cbFile, 0);
  CloseFile(hFile);
  return pBuffer;
}

//
// The WNDENUMPROC CloseAllWindows installs. The `__stdcall` is forced by the API that
// calls it: a `ret 8h` with no register input, which EnumWindows' own contract matches.
// All three branches converge on `mov eax, 1h`, so every path returns TRUE and the guard
// is one conjunction chain, and the guard's polarity skips the body when the handle has
// no parent.
//
// The first parameter's stack slot is reused as the out-buffer, which is only expressible
// as taking the address of the parameter itself -- a local would sit at a negative ebp
// offset. The window handle survives that overwrite because the source copies it into its
// own variable before any address is taken, which is what lets MSVC enregister the copy
// while the address-taken parameter stays in memory.
BOOL __stdcall EnumFunc(intptr_t nArg0, int nArg1)
{
  HWND hWnd = reinterpret_cast<HWND>(nArg0);

  if (GetParent(hWnd) != 0) {
    GetWindowThreadProcessId(hWnd, reinterpret_cast<DWORD *>(&nArg0));
    if (nArg0 == nArg1 && hWnd != reinterpret_cast<HWND>(ghWnd))
      PostMessageA(hWnd, 0x10, 0, 0);  // WM_CLOSE
  }
  return TRUE;
}

//
// A tail jmp with nothing set up in front of it, and cur_animate is declared
// nullary in glues.h on the strength of cursor_update's call site (ECX unset).
// So: one nullary call in tail position and no return value used.
void glue_cur_animate_fn()
{
  cur_animate();
}

//
// One bit, not two: the imm8 0xEF sign-extends to 0xFFFFFFEF, whose only clear bit is
// 0x10, and ~0x10 is -0x11. Clearing 0x10 and 0x01 would be -0x12. dialog.h names 0x10
// "locatable", which is the tab-stop bit in the name.
void remove_ctrl_tabstop(ctrl_t *pCtrl)
{
  pCtrl->dwFlags &= ~0x10u;
}

//
// Frees the glue string table, tagged with this TU's own file and line:
// EDX = 0x4EDA70 = "glues.cpp" and the pushed 0x2B1 = line 689.
void gluestrings_exit()
{
  if (sgnGlueStrsLoaded != 0)
    mem_free(sgpGlueStrs, sgszGluesCpp, 0x2B1);
}

// Activate a glue screen: upload its palette when it differs from the last one
// shown, then install the deferred error check. Both table reads stay reads, the
// oracle reloads the screen index after the three calls.
void glue_screen_activate(ctrl_t *pDlg)
{
  cur_show();
  if (sgnPrevPalId != gGlueData[sgnGlueScreen].nScreen) {
    vid_restore_palette(0, 0x100, sgGamePalette_g);
    ctrl_set_update(pDlg);
    vid_fade_in(3);
    sgnPrevPalId = gGlueData[sgnGlueScreen].nScreen;
  }
  timer_create(pDlg, 0x2E, 0x64,
               reinterpret_cast<PFN_TIMER_FN *>(glue_deferred_error_check));
}

//
// ECX is not read here, it is forwarded: the body sets EDX and three stack words and
// calls dlg_load, whose own first argument is ECX, so the incoming register passes
// straight through and the 26 bytes carry no prologue at all.
//
// EDX is load_cd_binary, the PFN_DLGLOADER, and the first stack word alloc_load_bmp, the
// PFN_DLGMEASURE; then "glues.cpp" and the line, which dlg_load forwards to the loader
// and the loader hands to SMemAlloc.
intptr_t glue_load_cd_dlg(intptr_t nArg0)
{
  return reinterpret_cast<intptr_t>(
      dlg_load(reinterpret_cast<void *>(nArg0),
               reinterpret_cast<PFN_DLGLOADER>(load_cd_binary),
               reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
               const_cast<char *>(sgszGluesCpp), 0x491));
}

// Unlink and free a hidden-control list: operator delete over the list's own
// link, reached through a TSLink local so the +4 stays a lea.
static __forceinline void delete_ctrl_list(CTRLNODELIST *pList)
{
  if (pList != NULL) {
    pList->UnlinkAll();
    TSLink<CTRLNODE> *pLink = &pList->link;
    CTRLNODE *pPrev = pLink->prev;
    if (pPrev != NULL) {
      CTRLNODE *pSlot = pLink->next;
      if (reinterpret_cast<intptr_t>(pSlot) <= 0) {
        pSlot = reinterpret_cast<CTRLNODE *>(~reinterpret_cast<intptr_t>(pSlot));
      } else {
        pSlot = reinterpret_cast<CTRLNODE *>(
            reinterpret_cast<char *>(pSlot) +
            (reinterpret_cast<char *>(pLink) -
             reinterpret_cast<char *>(pPrev->link.next)));
      }
      *reinterpret_cast<CTRLNODE **>(pSlot) = pPrev;
      pLink->prev->link.next = pLink->next;
      pLink->prev = NULL;
      pLink->next = NULL;
    }
    SMemFree(pList, "delete", -1, 0);
  }
}

// Start the slide-out: free the hidden-control list, park every transition
// control, arm the step timer, hide the parked chain and drop the tab stops.
// The sound guard tests the loop counter, not a second read of the count.
BYTE glues_transition_out(ctrl_t *pDlg)
{
  cur_hide();
  img_update();

  CTRLNODELIST *pList = sgTransitions_g.pHidden;
  sgTransitions_g.bOut = 1;
  delete_ctrl_list(pList);
  sgTransitions_g.pHidden = NULL;

  int i;
  for (i = 0; i < sgTransitions_g.nCount; ++i) {
    ctrl_t *pCtrl = ctrl_find_ndx(pDlg, sgTransitions_g.pRecs[i].nIndex);
    *reinterpret_cast<short *>(pCtrl->_unmapped24) = 1;
    sgTransitions_g.bActive[i] = 1;
  }
  if (i != 0)
    sub_49E740(0xAD);

  timer_create(pDlg, 7, 0x14,
               reinterpret_cast<PFN_TIMER_FN *>(glues_transition_out_cb));

  for (ctrl_t *pCtrl = sgTransitions_g.pHideChain; pCtrl != NULL;
       pCtrl = pCtrl->pNext)
    ctrl_hide(pCtrl);

  ctrl_chain(pDlg, NULL, reinterpret_cast<PFN_CTRL_EVT>(remove_ctrl_tabstop));
  return 1;
}

// Start the slide-in: record the transition table, push every control off its
// edge, keep the controls after the highest-indexed one on a hidden list, arm
// the step timer and hide them. The running maximum step is never read and the
// oracle computes it anyway; both maxima are short, which makes their compares
// 16-bit. The list allocation is written as its two arms: the plain `new`
// expression lets the shipped form merge the null store and move the case table.
void glues_transition_in(intptr_t nArg0, void *pTable, WORD nCount, int nArg3,
                         int nArg4)
{
  memset(&sgTransitions_g, 0, sizeof(sgTransitions_g));  // Hyperion: 0x90 in 1.17

  transctrl_t *pRecs = static_cast<transctrl_t *>(pTable);

  sgTransitions_g.pRecs = pRecs;
  sgTransitions_g.nCount = nCount;
  sgTransitions_g.bOut = 0;
  sgTransitions_g.nDelay = static_cast<WORD>(nArg4) / 20;

  short nMaxIndex = 0;
  short nMaxStep = 0;
  int nEnd = nCount;

  for (int i = 0; i < nEnd; ++i) {
    ctrl_t *pCtrl =
        ctrl_find_ndx(reinterpret_cast<ctrl_t *>(nArg0), pRecs[i].nIndex);
    short nStep = 0;
    pCtrl->dwFlags |= 0x20000;

    switch (pRecs[i].nEdge) {
      case 0:
        nStep = static_cast<WORD>(pCtrl->rc.right + 1);
        pCtrl->rc.left -= nStep;
        pCtrl->rc.right -= nStep;
        break;
      case 1:
        nStep = static_cast<WORD>(pCtrl->rc.bottom + 1);
        pCtrl->rc.top -= nStep;
        pCtrl->rc.bottom -= nStep;
        break;
      case 2:
        nStep = static_cast<WORD>(0x280 - pCtrl->rc.left);
        pCtrl->rc.right += nStep;
        pCtrl->rc.left += nStep;
        break;
      case 3:
        nStep = static_cast<WORD>(0x1E0 - pCtrl->rc.top);
        pCtrl->rc.bottom += nStep;
        pCtrl->rc.top += nStep;
        break;
    }

    sgTransitions_g.nHome[i] = nStep;
    *reinterpret_cast<WORD *>(pCtrl->_unmapped24) = nStep;
    if (nStep > nMaxStep)
      nMaxStep = nStep;
    if (pRecs[i].nIndex > nMaxIndex) {
      nMaxIndex = pRecs[i].nIndex;
      sgTransitions_g.pHideChain = pCtrl;
    }
    sgTransitions_g.bActive[i] = 1;
  }

  if (sgTransitions_g.pHideChain != NULL) {
    sgTransitions_g.pHideChain = sgTransitions_g.pHideChain->pNext;
    void *pMem = hidden_list_t::operator new(sizeof(hidden_list_t));
    if (pMem != NULL)
      sgTransitions_g.pHidden = reinterpret_cast<CTRLNODELIST *>(::new (pMem) hidden_list_t);
    else
      sgTransitions_g.pHidden = NULL;
  }

  timer_create(reinterpret_cast<ctrl_t *>(nArg0), 7, 0x14,
               reinterpret_cast<PFN_TIMER_FN *>(glues_transition_in_cb));

  for (ctrl_t *pCtrl = sgTransitions_g.pHideChain; pCtrl != NULL;
       pCtrl = pCtrl->pNext) {
    if ((pCtrl->dwFlags & 8) == 0)
      continue;

    CTRLNODELIST *pList = sgTransitions_g.pHidden;
    void *pMem = SMemAlloc(sizeof(_HIDDENCTRL),
                           gTypeInfo__HIDDENCTRL.raw_name(), -2, 8);
    CTRLNODE *pNode = reinterpret_cast<CTRLNODE *>(new (pMem) _HIDDENCTRL);
    TSLink<CTRLNODE> *pLink = pNode != NULL ? &pNode->link : &pList->link;

    CTRLNODE *pPrev = pLink->prev;
    if (pPrev != NULL) {
      CTRLNODE *pSlot = pLink->next;
      if (reinterpret_cast<intptr_t>(pSlot) <= 0) {
        pSlot = reinterpret_cast<CTRLNODE *>(~reinterpret_cast<intptr_t>(pSlot));
      } else {
        pSlot = reinterpret_cast<CTRLNODE *>(
            reinterpret_cast<char *>(pSlot) +
            (reinterpret_cast<char *>(pLink) -
             reinterpret_cast<char *>(pPrev->link.next)));
      }
      *reinterpret_cast<CTRLNODE **>(pSlot) = pPrev;
      pLink->prev->link.next = pLink->next;
      pLink->prev = NULL;
      pLink->next = NULL;
    }

    CTRLNODE *pTail = pList->link.prev;
    pLink->prev = pTail;
    pLink->next = pTail->link.next;
    pTail->link.next = pNode;
    pList->link.prev = reinterpret_cast<CTRLNODE *>(pLink);

    pNode->pCtrl = pCtrl;
    ctrl_hide(pCtrl);
  }
}

// Tear the glue screen down: drain and free the hidden-control list, drop the
// cached backdrop when the palette is about to change, and zero the transition
// block. img_update runs whichever way the palette test goes.
void glues_clear_all()
{
  if (sgTransitions_g.pHidden != NULL) {
    reinterpret_cast<HIDDENCTRLLIST *>(sgTransitions_g.pHidden)->Clear();
    delete_ctrl_list(sgTransitions_g.pHidden);
    sgTransitions_g.pHidden = NULL;
  }

  if (sgnPrevPalId != gGlueData[sgnGlueScreen].nScreen) {
    if (stru_66A470.bmp.pPixels != NULL)
      SMemFree(stru_66A470.bmp.pPixels, sgszGluesCpp, 0x1BB, 0);
    memset(&stru_66A470, 0, sizeof(stru_66A470));  // Hyperion: 0x408 in 1.17
    vid_fade_out(3);
    cur_hide();
  }
  img_update();

  sgnGlueDlgRefs = 0;
  memset(&sgTransitions_g, 0, sizeof(sgTransitions_g));  // Hyperion: 0x90 in 1.17
}

//
// A tail jmp with nothing set up for it -> cur_hide is nullary here.
void glue_fade_out()
{
  vid_fade_out(3);
  cur_hide();
}

//
// ECX is the destination and EDX the capacity: the push order is SStrCopy's own
// (dest, src, cchDest) reversed, so both register arguments pass straight through.
void glues_get_path(char *pszDest, int cchDest)
{
  SStrCopy(pszDest, gGlueData[sgnGlueScreen].pszPath, cchDest);
}

// Copy the current screen's palette path: the table's screen index selects a
// gluescreen_t record, whose name is the source.
void glues_get_pal_path(char *pszDest, int cchDest)
{
  SStrCopy(pszDest, sgGlueScreens[gGlueData[sgnGlueScreen].nScreen].szName,
           cchDest);
}

//
// The load-the-table-once idiom, then a tail jump into str_get_string. The latch's
// compare is hoisted above the prologue by the scheduler, not by the source order.
//
// str_load_table's four arguments come off the call: the table name, load_game_data as
// the loader callback, and the "glues.cpp" and line owner tag SMemAlloc and mem_free
// carry. glues.h declares all four parameters `int`, so each is cast here rather than
// changing the header; the real shape is `(const char *, <loader callback>, const char *,
// int)`, and str_get_string returns `int` where glues_get_str returns `char *`. The casts
// are codegen-neutral. app_add_exit takes gluestrings_exit's address rather than calling
// it, and the store between the two calls is scheduling: the source assigns, registers
// the exit, then sets the latch.
char *glues_get_str(int nStrId)
{
  if (sgnGlueStrsLoaded == 0) {
    sgpGlueStrs = reinterpret_cast<void *>(
        str_load_table(reinterpret_cast<intptr_t>(sgszGluAllTbl),
                       reinterpret_cast<intptr_t>(load_game_data),
                       reinterpret_cast<intptr_t>(sgszGluesCpp), 0x467));
    app_add_exit(gluestrings_exit);
    sgnGlueStrsLoaded = 1;
  }
  return reinterpret_cast<char *>(str_get_string(sgpGlueStrs, nStrId));
}

//
// The single 4-byte local is the process id: its address goes to
// GetWindowThreadProcessId as the out-parameter and its value is then EnumWindows'
// lParam. Two arguments at that second call, so it is EnumWindows and not the
// three-argument EnumThreadWindows, and the cast on EnumFunc is the only way to spell a
// `BOOL __stdcall (int, int)` where the API wants a `(void *, long)`.
//
// The final store is outside the guard, which only the branch target shows: the `jnz`
// lands on the store itself rather than on the epilogue. Putting it inside the `if` costs
// exactly one byte and nothing else, so that mistake shows up as a single byte delta with
// no size change.
void CloseAllWindows()
{
  DWORD dwProcessId;

  if (sgnGlueScreen == 4) {
    GetWindowThreadProcessId(reinterpret_cast<HWND>(ghWnd), &dwProcessId);
    EnumWindows(reinterpret_cast<WNDENUMPROC>(EnumFunc),
                static_cast<LPARAM>(dwProcessId));
  }
  sgnGlueScreen = 0x19;
}

//
// Absolutises a path into the caller's buffer. arg1 is a pointer, not an int: EDX is
// dereferenced as a byte and compared with a backslash. Which argument is the source and
// which the destination is a reading rather than a derivation, so the header's parameter
// names stay neutral.
//
// Three CRT primitives are inlined by /Oi, so none is a call: strcpy, strlen -- emitted
// four times and never shared, because each memmove may have moved the terminator -- and
// memmove, which is a real __cdecl call. The middle prepend_str takes a literal, where a
// count of 1 folds the copy loop away and the store constant-folds to
// `mov byte ptr [edi], 5Ch`. The backslash is doubled; `\t` would be a valid escape with
// no diagnostic.
void glues_make_full_path(void *pArg0, char *pArg1)
{
  const char *pszSrc = static_cast<const char *>(pArg0);
  char *pszDest = pArg1;

  strcpy(pszDest, pszSrc);

  if (pszDest[0] == '\\') {
    // Already root-relative: open two characters and drop the drive in.
    prepend_n(pszDest, sgszRootPath, 2);
  } else if (pszDest[1] != ':') {
    // Neither rooted nor drive-qualified: prepend a separator, then the root.
    prepend_str(pszDest, "\\");
    prepend_str(pszDest, sgszRootPath);
  }
}

//
// A `ret 4h` with ECX and EDX both stored means three arguments, the third on the stack.
// Four independent dword stores in ascending address order, so the source is four
// assignments in that order; the stack argument's load is hoisted above the constant
// store by the scheduler, which no source order expresses.
void glues_set_skip(int nArg0, int nArg1, int nArg2)
{
  sgnGlueSkipArmed = 1;
  sgnGlueSkipArg0 = nArg0;
  sgnGlueSkipArg1 = nArg1;
  sgnGlueSkipArg2 = nArg2;
}

//
// A single DWORD store of -1 and nothing else: 10 bytes of C7 05 <abs32>
// <imm32> plus the ret is the whole 11.
void glues_reset_prev_pal()
{
  sgnPrevPalId = -1;
}

//
// A binary-search switch over eight 4CC network-mode ids, and the tree shape is what
// makes it a switch rather than an if-chain: with the case values sorted ascending the
// first compare is against the middle one and the lower half pivots on its own middle.
// The compares are unsigned, so the switch expression is read unsigned although every
// case value has its top bit clear.
//
// The map, off the branch targets:
//   0x41544C4B 'ATLK'  -> 10      0x4D444D58 'MDMX'  -> 20
//   0x424E4554 'BNET'  ->  4      0x4D4F444D 'MODM'  -> 20
//   0x4950584E 'IPXN'  -> 10      0x5343424C 'SCBL'  -> 21
//   0x49505858 'IPXX'  -> 10      0x5544504E 'UDPN'  -> 10
//   default            ->  0
// Written as hex rather than multi-character literals, whose value is
// implementation-defined.
void set_netmode_return_menu()
{
  switch (static_cast<unsigned int>(NetworkMode())) {
    case 0x424E4554u:  // 'BNET'
      sgnGlueScreen = 4;
      break;
    case 0x41544C4Bu:  // 'ATLK'
    case 0x4950584Eu:  // 'IPXN'
    case 0x49505858u:  // 'IPXX'
    case 0x5544504Eu:  // 'UDPN'
      sgnGlueScreen = 10;
      break;
    case 0x5343424Cu:  // 'SCBL'
      sgnGlueScreen = 21;
      break;
    case 0x4D444D58u:  // 'MDMX'
    case 0x4D4F444Du:  // 'MODM'
      sgnGlueScreen = 20;
      break;
    default:
      sgnGlueScreen = 0;
      break;
  }
}

//
// Two tail calls with one return each: the oracle duplicates the epilogue rather than
// sharing it, so this is an if/else with a call in each arm and not a dispatch through a
// variable. +0x22 is the control's type field, and the word-wide compare fixes its width.
void glue_set_text(ctrl_t *pDlg, short nIndex, char *pszText)
{
  ctrl_t *pCtrl = ctrl_find_ndx(pDlg, nIndex);
  if (pCtrl->wType == 8)
    edit_set_text(pCtrl, pszText);
  else
    ctrl_set_text(pCtrl, pszText);
}

// Prepares a glue screen dialog. Only the first open dialog loads the screen's
// backdrop, from the cache when one is held, and sets up the palette, fonts and
// cursor; a nested one just notifies the dialog already open. The screen index
// is read above the cache test, and the effect pair shares one record pointer
// while the later reads go back to sgnGlueScreen.
void glue_init_screen_dlg(ctrl_t *pDlg, PFN_CTRL_EVT *pTable, DWORD cbTable,
                          intptr_t nFlags)
{
  char szPath[0x104];
  bmp_t *pBmp;
  int nScreen;

  flc_animate(pDlg);
  pDlg->dwFlags |= 0x20000000;
  if (pTable != NULL)
    ctrl_set_evt_fcns(pDlg, pTable, cbTable);
  if (sgnGlueDlgRefs++ != 0) {
    if (sgpGlueDlg != NULL)
      ctrl_send_msg(sgpGlueDlg, 6, 0);
    pDlg->dwFlags |= 0x8000000;
    return;
  }

  pBmp = reinterpret_cast<bmp_t *>(pDlg->_unmapped0C);
  nScreen = gGlueData[sgnGlueScreen].nScreen;
  if (stru_66A470.bmp.pPixels != NULL && pBmp != NULL) {
    *pBmp = stru_66A470.bmp;
    memcpy(sgGamePalette_g, stru_66A470.aPalette, 0x400);
  } else {
    SStrCopy(szPath, sgGlueScreens[nScreen].szName, 0x104);
    SStrPack(szPath, "\\BackGnd.pcx", 0x104);
    alloc_load_bmp(szPath, pBmp, reinterpret_cast<DWORD_PTR>(sgGamePalette_g),
                   const_cast<char *>(sgszGluesCpp), 0x41D);
    stru_66A470.bmp = *pBmp;
    memcpy(stru_66A470.aPalette, sgGamePalette_g, 0x400);
  }
  pDlg->dwFlags |= 0x20;
  init_pens(sgGamePalette_g, CPalette::find_closest_palette_entry);
  effects_init_palette(reinterpret_cast<const DWORD *>(sgGamePalette_g),
                       reinterpret_cast<effects_desc_t *>(nFlags));
  {
    const glueres_t *pRes = &gGlueData[sgnGlueScreen];
    sub_4C4D50(reinterpret_cast<const rendertype_t *>(
        &sgGlueScreens[pRes->nScreen]._unmapped108[2]));
    sub_4CA820(0x50,
               (reinterpret_cast<BYTE *>(sgpGlueEffectData) +
                sgGlueScreens[pRes->nScreen]._unmapped108[0])[pRes->nScreen * 16],
               0);
  }
  set_font_palette(reinterpret_cast<BYTE *>(sgpGlueFontData) +
                   gGlueData[sgnGlueScreen].nScreen * 0xC0);
  dlg_stats_init(reinterpret_cast<dlgtheme_t *>(
      sgGlueDlgThemes[gGlueData[sgnGlueScreen].nScreen]));
  cursor_set_image(sgGlueScreens[gGlueData[sgnGlueScreen].nScreen].hCursor);
  timer_create(pDlg, 0x18, 0x32,
               reinterpret_cast<PFN_TIMER_FN *>(glue_cur_animate_fn));
  set_dlg_target_bitmap(reinterpret_cast<bmp_t *>(pDlg->_unmapped0C));
  sgpGlueDlg = pDlg;
  RunEvtDispatcher(0);
}

//
// A post-decrement refcount teardown: the value compared is the one that was read and
// the value stored is one less. A pre-decrement compared against 0 is a different
// instruction sequence.
void glues_dlg_deactivate(ctrl_t *pDlg)
{
  flc_remove_timers(pDlg);
  if (sgnGlueDlgRefs-- == 1) {
    timer_remove_node(pDlg, 0x18);
    sgpGlueDlg = 0;
  }
}

// Starts one control's slide transition.
//
// +0x08 is ctrl_t::rc.right. +0x24 falls inside dialog.h's `_unmapped24[6]` filler and is
// reached through it: it is the live slide offset, which glues_transition_in_cb reads back
// as a word and steps toward zero. Its sign is undecided. Each arm stores it on its own.
void sub_4583C0(ctrl_t *pDlg, int nIndex, int bIn)
{
  sgTransitions_g.bActive[nIndex] = 1;

  ctrl_t *pCtrl = ctrl_find_ndx(pDlg, sgTransitions_g.pRecs[nIndex].nIndex);
  if (bIn != 0) {
    *reinterpret_cast<short *>(pCtrl->_unmapped24) =
        static_cast<short>(sgTransitions_g.nHome[nIndex] - pCtrl->rc.right);
    timer_create(pDlg, 7, 0x14,
                 reinterpret_cast<PFN_TIMER_FN *>(glues_transition_in_cb));
  } else {
    *reinterpret_cast<short *>(pCtrl->_unmapped24) = 1;
    sub_49E740(0xAD);
    timer_create(pDlg, 7, 0x14,
                 reinterpret_cast<PFN_TIMER_FN *>(glues_transition_out_cb));
  }
}

//
// The whole body is one guard on `sgnGlueScreen == 19h` -- the state
// CloseAllWindows leaves behind, which is what makes these two a pair.
//
// The last store is unconditional: the branch skips exactly the `call dlg_deactivate`,
// so the store after it is on both paths. ECX is loaded once and serves both the test and
// the call, which is `if (p) f(p);` rather than a re-read.
void glue_panic_fallback_fn()
{
  if (sgnGlueScreen == 0x19) {
    sgnGlueScreen = 0;
    gnDlgReturnVal = -1;
    deactivate_popup();
    deactivate_modal_popup();
    if (sgpGlueDlg != 0)
      dlg_deactivate(sgpGlueDlg);
    sgpGlueDlg = 0;
  }
}

// The glue shell's button-click sound hook.
//
// A switch rather than an if-chain: `sub eax, 0h` followed by `dec eax` is MSVC's
// decision tree for a small dense switch, where an `if (n == 0)` would have emitted a
// `test` and never materialised the running subtraction. Case 0 is rate-limited to 200 ms
// and case 1 is not, and the delta is compared unsigned, which is what makes
// GetTickCount's wrap harmless and why the stamp above is unsigned.
void __stdcall sub_458490(dlg_dword_t dwSound)
{
  switch (dwSound) {
    case 0: {
      ULONG dwNow = GetTickCount();
      if (dwNow - dword_66A888 >= 0xC8) {
        sub_49E740(0xA6);
        dword_66A888 = dwNow;
      }
      break;
    }
    case 1:
      sub_49E740(0xA5);
      break;
  }
}

// The shape four bodies share.
//
// The guard is the short-circuit `dwSpawnLimited == 0 || !GlueSpawnLimited()`: a spawned
// copy skips the body. The subscript has to be unsigned: with a signed index, or any
// pointer walk, MSVC biases the induction variable and addresses the record off it.
void glues_init_cursors()
{
  for (unsigned i = 0; i < 12; ++i) {
    if (sgGlueScreens[i].dwSpawnLimited == 0 || !GlueSpawnLimited()) {
      char szPath[0x104];
      SStrCopy(szPath, sgGlueScreens[i].szName, 0x104);
      SStrPack(szPath, sgszCursorPackFmt, 0x104);
      sgGlueScreens[i].hCursor = cur_load(szPath);
    }
  }
}

//
// The same guard as glues_init_cursors, but a count-up walk against the array limit plus
// a second walker that receives the theme handle. That second walker is set up first, so
// it is declared first. dlg_load_theme's `ret 8h` makes its arguments (szPath,
// ReadGroupFile, "glues.cpp", line).
//
// The loop compare is signed, so the limit test is spelled through `intptr_t`: a plain
// pointer compare emits an unsigned branch, pointer comparisons being unsigned in C++,
// and the difference is visible in the bytes. ReadGroupFile is taken as a function
// address rather than the literal 0x444A60, which the oracle relocates.
void glues_init_grp()
{
  intptr_t *pTheme = sgGlueDlgThemes;
  gluescreen_t *pScreen = sgGlueScreens;

  do {
    if (pScreen->dwSpawnLimited == 0 || !GlueSpawnLimited()) {
      char szPath[0x104];
      SStrCopy(szPath, pScreen->szName, 0x104);
      SStrPack(szPath, sgszDlgGrpPackFmt, 0x104);
      *pTheme = dlg_load_theme((intptr_t)szPath, (intptr_t)ReadGroupFile,
                               (intptr_t)sgszGluesCpp, 0x185);
    }
    ++pScreen;
    ++pTheme;
  } while ((intptr_t)pScreen < (intptr_t)(sgGlueScreens + 12));
}

//
// The block pointer is read again inside the loop, an opaque call sitting between, so it
// cannot be hoisted, and the stride is a plain byte offset rather than a second pointer.
// The owner tag names a line in the original file: 0x13B is glues.cpp:315.
void glues_init()
{
  light_cycle_init();
  effects_init(0, 0);

  sgpGlueEffectData = (char *)SMemAlloc(0xC0, sgszGluesCpp, 0x13B, 0);

  int nOff = 0;
  gluescreen_t *pScreen = sgGlueScreens;

  do {
    if (pScreen->dwSpawnLimited == 0 || !GlueSpawnLimited()) {
      char szPath[0x104];
      SStrCopy(szPath, pScreen->szName, 0x104);
      SStrPack(szPath, sgszEffectPackFmt, 0x104);
      GameLoadImage((intptr_t)szPath, 0, (intptr_t)(sgpGlueEffectData + nOff), 0x10, 0, 0, 0);
    }
    ++pScreen;
    nOff += 0x10;
  } while ((intptr_t)pScreen < (intptr_t)(sgGlueScreens + 12));
}

//
// glues_init's shape without the two init calls; 0x900 = 12 * 0xC0, and
// SMemAlloc(0x900, "glues.cpp", 0x159, 0) is glues.cpp:345.
void glues_init_font()
{
  sgpGlueFontData = (char *)SMemAlloc(0x900, sgszGluesCpp, 0x159, 0);

  int nOff = 0;
  gluescreen_t *pScreen = sgGlueScreens;

  do {
    if (pScreen->dwSpawnLimited == 0 || !GlueSpawnLimited()) {
      char szPath[0x104];
      SStrCopy(szPath, pScreen->szName, 0x104);
      SStrPack(szPath, sgszFontPackFmt, 0x104);
      GameLoadImage((intptr_t)szPath, 0, (intptr_t)(sgpGlueFontData + nOff), 0xC0, 0, 0, 0);
    }
    ++pScreen;
    nOff += 0xC0;
  } while ((intptr_t)pScreen < (intptr_t)(sgGlueScreens + 12));
}

//
// The guard only covers the fill: the branch lands on the sub_49E3F0 call, so that call
// and the snd_set_btn_click tail jump both run unconditionally. The fill itself is
// `sgwBtnSnd[i] = i + 0xA5`, computed in a full dword with the low word stored.
//
// The local `short *pw` matters under OC_DLL_MODE and nowhere else: subscripting the
// dual-spelled name directly matches the plain form, but with a constant base MSVC sees
// the whole array and strength-reduces the fill into a pointer walk, where the oracle
// keeps the scaled index. Binding the array to a local pointer once and subscripting
// through that is the only lever found, and it costs the plain form nothing.
void glues_init_btn()
{
  if (sgnBtnSndInit == 0) {
    short *pw = sgwBtnSnd;
    for (int i = 0; i < 9; ++i)
      pw[i] = (short)(i + 0xA5);
    sgnBtnSndInit = 1;
  }
  sub_49E3F0((intptr_t)sgwBtnSnd);
  snd_set_btn_click(sub_458490);
}

//
// Nothing sets ECX or EDX in front of the call and timer_remove_node takes both, so both
// registers are live-in and forwarded unchanged: two arguments, not zero, since a nullary
// spelling could not name them. Nothing is set up for the tail jump, so
// net_error_actualize is nullary.
void glue_deferred_error_check(intptr_t nArg0, int nArg1)
{
  timer_remove_node(reinterpret_cast<ctrl_t *>(nArg0), nArg1);
  net_error_actualize();
}

// Only glues_transition_in_cb reaches 0x5080C8 and no data word holds its address, so the
// proof arm is file-static, starting at the oracle's 1; the payload keeps the oracle's storage.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define dword_5080C8 (*reinterpret_cast<int *>(OC_VA(0x5080C8u)))
#else
static int dword_5080C8 = 1;  // 0x5080C8
#endif

// The node after a link, or NULL at the list's terminator.
static __forceinline _HIDDENCTRL *hidden_link_next(TSLink<_HIDDENCTRL> *pLink) {
  return (intptr_t)pLink->next > 0 ? pLink->next : NULL;
}
// The list's first node, or NULL when it is empty.
static __forceinline _HIDDENCTRL *hidden_head(HIDDENCTRLLIST *pList) {
  return hidden_link_next(&pList->link);
}

// The slide-in timer: moves every active control a quarter of what is left toward home, and once
// all are home shows the hidden controls, frees the hidden list and stops the timer.
// The step is a WORD. The emptiness test and the walk's first node both go through hidden_head,
// whose select through the link pointer keeps the walk's own zero-trip test.
void glues_transition_in_cb(ctrl_t *pDlg) {
  if (sgTransitions_g.nDelay != 0) {
    --sgTransitions_g.nDelay;
    return;
  }

  if (dword_5080C8 != 0) {
    sub_49E740(0xAC);
    dword_5080C8 = 0;
  }

  int bDone = 1;
  for (int i = 0; i < sgTransitions_g.nCount; ++i) {
    if (sgTransitions_g.bActive[i] == 0)
      continue;

    ctrl_t *pCtrl = ctrl_find_ndx(pDlg, sgTransitions_g.pRecs[i].nIndex);
    WORD nLeft = *reinterpret_cast<WORD *>(pCtrl->_unmapped24);
    if (nLeft != 0) {
      bDone = 0;

      WORD nStep;
      if (nLeft < 4)
        nStep = nLeft;
      else
        nStep = static_cast<WORD>(nLeft >> 2);
      nLeft = static_cast<WORD>(nLeft - nStep);
      *reinterpret_cast<WORD *>(pCtrl->_unmapped24) = nLeft;

      switch (sgTransitions_g.pRecs[i].nEdge) {
        case 0:
          pCtrl->rc.right += nStep;
          ctrl_set_update(pCtrl);
          pCtrl->rc.left += nStep;
          break;
        case 1:
          pCtrl->rc.bottom += nStep;
          ctrl_set_update(pCtrl);
          pCtrl->rc.top += nStep;
          break;
        case 2:
          pCtrl->rc.left -= nStep;
          ctrl_set_update(pCtrl);
          pCtrl->rc.right -= nStep;
          break;
        case 3:
          pCtrl->rc.top -= nStep;
          ctrl_set_update(pCtrl);
          pCtrl->rc.bottom -= nStep;
          break;
      }
    }
    ctrl_set_update(pCtrl);
  }

  if (bDone) {
    sub_49E740(0xAB);

    if (hidden_head(reinterpret_cast<HIDDENCTRLLIST *>(sgTransitions_g.pHidden))) {
      for (_HIDDENCTRL *p = hidden_head(reinterpret_cast<HIDDENCTRLLIST *>(sgTransitions_g.pHidden)); (intptr_t)p > 0;
           p = static_cast<_HIDDENCTRL *>(p->link.next)) {
        ctrl_send_msg(p->pCtrl, 0x405, 0);
        ctrl_show(p->pCtrl);
      }
      reinterpret_cast<HIDDENCTRLLIST *>(sgTransitions_g.pHidden)->Clear();
    }

    for (int i = 0; i < sgTransitions_g.nCount; ++i)
      sgTransitions_g.bActive[i] = 0;

    timer_remove_node(pDlg, 7);
    dword_5080C8 = 1;
    ctrl_send_msg(pDlg, 0x405, 0);
  }
}

// The slide-out timer: move every active
// control one step toward its edge, doubling the step each tick, and once all
// are off screen stop the timer and, if this was the last glue dialog going
// out, close it.
void glues_transition_out_cb(ctrl_t *pDlg)
{
  int bDone = 1;

  for (int i = 0; i < sgTransitions_g.nCount; ++i) {
    if (sgTransitions_g.bActive[i] == 0)
      continue;

    ctrl_t *pCtrl = ctrl_find_ndx(pDlg, sgTransitions_g.pRecs[i].nIndex);
    int nStep = *reinterpret_cast<WORD *>(pCtrl->_unmapped24);

    switch (sgTransitions_g.pRecs[i].nEdge) {
      case 0:
        if (pCtrl->rc.right > 0)
          bDone = 0;
        pCtrl->rc.left -= nStep;
        ctrl_set_update(pCtrl);
        pCtrl->rc.right -= nStep;
        break;
      case 1:
        if (pCtrl->rc.bottom > 0)
          bDone = 0;
        pCtrl->rc.top -= nStep;
        ctrl_set_update(pCtrl);
        pCtrl->rc.bottom -= nStep;
        break;
      case 2:
        if (pCtrl->rc.left < 0x280)
          bDone = 0;
        pCtrl->rc.right += nStep;
        ctrl_set_update(pCtrl);
        pCtrl->rc.left += nStep;
        break;
      case 3:
        if (pCtrl->rc.top < 0x1E0)
          bDone = 0;
        pCtrl->rc.bottom += nStep;
        ctrl_set_update(pCtrl);
        pCtrl->rc.top += nStep;
        break;
    }
    *reinterpret_cast<WORD *>(pCtrl->_unmapped24) *= 2;
  }

  if (bDone) {
    timer_remove_node(pDlg, 7);
    if (sgTransitions_g.bOut) {
      flc_remove_timers(pDlg);
      if (sgnGlueDlgRefs-- == 1) {
        timer_remove_node(pDlg, 0x18);
        sgpGlueDlg = NULL;
      }
      dlg_deactivate(pDlg);
    }
  }
}

#ifndef gbIsExpansion
#ifdef OC_DLL_MODE
#define gbIsExpansion (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE gbIsExpansion;
#endif
#endif
#ifndef byte_5264C3_g
#ifdef OC_DLL_MODE
#define byte_5264C3_g (*reinterpret_cast<BYTE *>(OC_VA(0x5264C3u)))
#else
extern BYTE byte_5264C3_g;
#endif
#endif
#ifndef dword_66A278
#ifdef OC_DLL_MODE
#define dword_66A278 (*reinterpret_cast<DWORD *>(OC_VA(0x66A278u)))
#else
extern DWORD dword_66A278;
#endif
#endif
#ifndef dword_66A324
#ifdef OC_DLL_MODE
#define dword_66A324 (*reinterpret_cast<DWORD *>(OC_VA(0x66A324u)))
#else
extern DWORD dword_66A324;
#endif
#endif
#ifndef gpfnModalExtraFn_g
#ifdef OC_DLL_MODE
#define gpfnModalExtraFn_g (*reinterpret_cast<PFN_MODALEXTRA *>(OC_VA(0x6CE6E4u)))
#else
extern PFN_MODALEXTRA gpfnModalExtraFn_g;
#endif
#endif

// The glue screen loop: sets up the menus, honours a pending campaign skip, then runs one screen
// per pass until the game mode leaves the menus, and tears the glue resources down.
// The two walks after glues_init_btn fill never-read arrays; MSVC keeps the walks.
// The fail paths keep their own blocks through allocation, which puts the
// fail block at the switch tree's fall-through.
void RunGlues()
{
  int   aResScreens[26];
  void *aScreenTails[12];
  int   nSpawnArg = 0;

  SFileGetInstallPath(sgszRootPath, 0x104, 0);
  light_init();
  font_init_glues();
  glues_init();
  glues_init_cursors();
  glues_init_font();
  glues_init_grp();
  glues_init_btn();

  {
    void **pp = aScreenTails;
    for (BYTE *p = sgGlueScreens[0]._unmapped114;
         reinterpret_cast<intptr_t>(p) <
             reinterpret_cast<intptr_t>(sgGlueScreens[12]._unmapped114);
         p += sizeof(gluescreen_t))  // Hyperion: 0x518 in 1.17
      *pp++ = p;
  }
  {
    int *pn = aResScreens;
    for (const BYTE *p = reinterpret_cast<const BYTE *>(&gGlueData[0].nScreen);
         reinterpret_cast<intptr_t>(p) <
             reinterpret_cast<intptr_t>(&gGlueData[26].nScreen);
         p += sizeof(glueres_t))
      *pn++ = *reinterpret_cast<const int *>(p);
  }

  if (gwGameMode == 6) {
    sgnGlueScreen = 0x11;
    gwGameMode = 4;
    nSpawnArg = 1;
  } else if (gwGameMode == 7) {
    sgnGlueScreen = 0x11;
    gwGameMode = 4;
    nSpawnArg = 0;
  } else if (sgnGlueSkipArmed != 0 && gbMultiPlayerMode == 0) {
    sgnGlueSkipArmed = 0;
    if (gLocalPlayerName[0] == 0)
      SStrCopy(gLocalPlayerName, error_get_str(0x48), 25);

    byte_5264C3_g = 0;
    gbIsExpansion = static_cast<BYTE>(sgnGlueSkipArg2 != 0);
    if (sgnGlueSkipArg2 == 0 || IsBroodwar()) {
      // Decision 16: each barrier keeps a failing path's own block, the only shape found.
      if (!HasEitherCampaign()) {
        _ReadWriteBarrier();
        goto fail;
      }
      if (!sub_4BB6C0(static_cast<WORD>(sgnGlueSkipArg0))) {
        _ReadWriteBarrier();
        goto fail;
      }
      switch (sgnGlueSkipArg1) {
        case 2:
          sgnGlueScreen = 9;
          goto armed;
        case 1:
          sgnGlueScreen = 7;
          goto armed;
        case 0:
          sgnGlueScreen = 8;
          goto armed;
      }
      _ReadWriteBarrier();
    }
  fail:
    sgnGlueScreen = 0;
    gbIsExpansion = 0;
    goto skipped;

  armed:
    dword_66A278 = 1;
    if ((glCheatBits & 0x20000000) != 0)
      gwGameMode = 1;
  } else {
    // Hyperion: a replay or a map given on the command line starts here,
    // in place of the menus (port_launch.cpp). The game mode it sets skips the
    // screen loop. A benchmark's replay plays here once its game is made, and an AI
    // lab's worker waits here for its next game, and a watched skirmish's replay
    // plays here once its game is made.
    if (!PortBenchLaunch() && !PortWatchLaunch() && !PortLabWorkerNext())
      PortLaunch();
  }

skipped:
  if (!PortLabWorker())  // Hyperion: an AI lab worker has no title to show
    Sleep(0x5DC);
  close_title_dlg();
  sgnPrevPalId = -1;
  vid_fade_out(3);
  cur_hide();
  gpfnModalExtraFn_g = reinterpret_cast<PFN_MODALEXTRA>(glue_panic_fallback_fn);
  if (stru_66A470.bmp.pPixels != NULL)
    SMemFree(stru_66A470.bmp.pPixels, sgszGluesCpp, 0x1BB, 0);
  memset(&stru_66A470, 0, sizeof(stru_66A470));  // Hyperion: 0x408 in 1.17

  while (gwGameMode == 4) {
    switch (sgnGlueScreen) {
      case 25:
        sgnGlueScreen = 0;
        gnDlgReturnVal = -1;
        deactivate_popup();
        deactivate_modal_popup();
        if (sgpGlueDlg != 0)
          dlg_deactivate(sgpGlueDlg);
        sgpGlueDlg = 0;
        break;
      case 0:
        reset_net_error();
        do_main_dlg();
        break;
      case 5:
        dword_66A324 = 0;
        do_login_dlg();
        break;
      case 6:
        do_cmpgn_dlg();
        break;
      case 7:
        do_readyt_dlg();
        break;
      case 8:
        do_readyz_dlg();
        break;
      case 9:
        do_readyp_dlg();
        break;
      case 22:
        do_xcampaign_dlg();
        break;
      case 2:
        do_conn_dlg();
        break;
      case 20:
        do_modem_dlg();
        break;
      case 10:
      case 21:
        do_join_dlg();
        break;
      case 11:
        do_create_dlg(0);
        break;
      case 12:
        do_create_dlg(1);
        break;
      case 3:
        do_gluChat_dlg();
        break;
      case 13:
        do_load_dlg();
        break;
      case 14:
      case 15:
      case 16:
      case 17:
      case 18:
      case 19:
        do_score_dlg(nSpawnArg);
        break;
      // Hyperion: 4, Battle.net's screens (do_bnet_dlg), is gone with them
      //. Only Battle.net as the provider led there,
      // and the port starts no provider but its own.
      default:
        sgnGlueScreen = 0;
        break;
    }
  }

  if (stru_66A470.bmp.pPixels != NULL)
    SMemFree(stru_66A470.bmp.pPixels, sgszGluesCpp, 0x1BB, 0);
  memset(&stru_66A470, 0, sizeof(stru_66A470));  // Hyperion: 0x408 in 1.17
  snd_stop();
  set_dlg_target_bitmap(&_img_draw_buf);
  vid_fade_out(3);
  cur_hide();
  sub_49E3F0(0);
  if (sgpGlueEffectData != NULL)
    SMemFree(sgpGlueEffectData, sgszGluesCpp, 0x152, 0);
  effects_free();
  colorcycling_free();

  for (int i = 0; i < 12; ++i) {
    if (sgGlueScreens[i].hCursor != 0 &&
        (sgGlueScreens[i].dwSpawnLimited == 0 || !GlueSpawnLimited())) {
      cur_free(sgGlueScreens[i].hCursor);
      sgGlueScreens[i].hCursor = 0;
    }
  }

  if (sgpGlueFontData != NULL)
    SMemFree(sgpGlueFontData, sgszGluesCpp, 0x172, 0);

  for (int i = 0; i < 12; ++i) {
    if (sgGlueScreens[i].dwSpawnLimited == 0 || !GlueSpawnLimited())
      dlg_grp_free(reinterpret_cast<dlgtheme_t *>(sgGlueDlgThemes[i]));
  }
}
