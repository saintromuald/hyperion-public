// gluCreat.cpp -- the create-game screen: the map listbox, the game-type, sub-type, race
// and computer-slot dropdowns, and the speed scroller. Bodies are in oracle VA order.
//
// TU membership cuts both ways and needs re-checking after a change here: a callee
// defined in this file can be spliced into a caller in this file by /Ob2, which a
// cross-TU call never is, and a body whose size is exactly caller plus callee needs its
// callee here. __declspec(noinline) on the declaration is the lever.
//
// The VA order itself matters: it puts gluCreate_get_config_game_speed (0x454CA0) and
// gluCreate_speedScroll_ctrl_deactivate (0x454DB0) above their single call site at
// 0x4565D0, which is the condition /Ob2 inlines on -- MSVC cannot inline a definition it
// has not seen, so a reorder changes what gets spliced.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>       // offsetof, for the table static_asserts
#include <string.h>       // strrchr -- see the note below

#include "../h/gluCreat.h"
#include "../h/gluMap.h"     // ClearMapList
#include "../h/dialog.h"    // ctrl_t, dlgevt_t
#include "../h/dlg_ctrl.h"  // ctrl_showhide, ctrl_find_ndx
#include "../h/dlg_lbox.h"  // lbox_*, scroll_*
#include "../h/glues.h"     // glues_get_str
#include "../h/save_dlg.h"
#include "../h/Storm.h"     // SRegLoadString / SRegSaveString / SStrCopy
#include "../h/net_cmds.h"
#include "../h/victory.h"
#include "../h/opt_dlgs.h"
#include "../h/gametype.h"     // TGAMEPARAMS, IterateMainTemplates
#include "../h/gluBNCreate.h"  // gametype_get_subtype_name, CreateMapInfo
#include "../h/minimap.h"      // timer_remove_node
#include "../h/gluPopup.h"     // do_pok_dlg
#include "../h/net_slot.h"     // slot_set_race
#include "../h/gluRdy.h"       // glues_transition_out
#include "../port/front/port_front.h"  // Hyperion: PortFrontReplays, PortFrontSkirmish
#include "../h/gds_grp2bmpx.h" // grp_to_bmpx, bmp_t, GROUP
#include "../h/dlg_draw.h"     // draw_bmp_effect, draw_ctrl_text
#include "../h/dlg_dlg.h"      // dlg_set_font
#include "../h/gds_font.h"     // string_width
#include "../h/gamedata.h"     // ReadGroupFile
#include "../h/replay.h"       // map_free
#include "../h/net_glue.h"     // NetworkMode
#include "../h/gds_log.h"      // CreateDirectoriesRecursive
#include "../gen/globals117.h"

// Storm ordinal 509; Storm.h does not declare it yet.  C linkage, as in
// gluBNCreate.cpp and cheat.cpp: the .def supplies _SStrCmpI@12.
extern "C" {
int  __stdcall SStrCmpI(const char *pszA, const char *pszB, size_t cchMax);
// Storm ordinal 296, spelled as charactr.cpp spells it.
// Hyperion: as port_stormfile.cpp defines it
DWORD  __stdcall SFileGetUserDataPath(char *pszBuf, DWORD cchBuf, DWORD dwFlags);
}

// Same spelling as dlg_draw.cpp, gluConn.cpp and dlg_edit.cpp; no header seats it.
const char * __fastcall font_adjust(int nX, int nY, const char *psz);

// The listbox's per-item draw callback at ctrl_t +0x52 and its item-string
// array at +0x3A, spelled as gluConn.cpp spells them: dialog.h leaves the
// listbox arm of the payload unnamed.
typedef void(__fastcall *PFN_LBOX_ITEMDRAW)(void *pCtrl, BYTE nItem, void *prcClip,
                                            int nXOfs, int nYOfs);
#define LBOX_ITEMS(p)   (*reinterpret_cast<const char *const **>(&(p)->_unmapped36[CTRL_SLOT2]))

// strrchr comes from <string.h> on purpose: sub_4560D0 calls it __cdecl with two
// arguments, caller-cleaned, and VC12 declares it as a C++ overload pair, against which a
// local single prototype is C2556.

// The three .data and .rdata tables this screen walks. Each entry layout comes off the
// loop that walks it -- the base, the stride, the end the `cmp` stops at, and the
// displacement and width of every field access. All three are local to this TU.

// 0x503DA4, walked by gluCreate_raceDropDown_ctrl_activate:
//   `mov esi, 503DA4h` ... `add esi, 2h` ... `cmp esi, 503DACh` -> stride 2,
//   8 bytes total, 4 entries.  `movzx ecx, byte ptr [esi]` (+0, fed to
//   lbox_set_data as the item data) and `movzx ecx, byte ptr [esi+1h]`
//   (+1, fed to error_get_str) -> two BYTES.
struct gluCreate_race_ent_t {
  /*0x00*/ BYTE nValue;
  /*0x01*/ BYTE nStrId;
};
static_assert(offsetof(gluCreate_race_ent_t, nValue) == 0x00, "race +0: movzx ecx, byte ptr [esi]");
static_assert(offsetof(gluCreate_race_ent_t, nStrId) == 0x01, "race +1: movzx ecx, byte ptr [esi+1h]");
static_assert(sizeof(gluCreate_race_ent_t) == 2, "race stride: add esi, 2h");

// 0x503D9C, walked by gluCreate_computerSlotDropDown_ctrl_activate:
//   `mov esi, 503D9Ch` ... `add esi, 4h` ... `cmp esi, 503DA4h` -> stride 4,
//   8 bytes total, 2 entries.  `movzx ecx, word ptr [esi]` / `cmp word ptr
//   [esi], 1h` (+0) and `movzx ecx, word ptr [esi+2h]` (+2) -> two words.
struct gluCreate_cslot_ent_t {
  /*0x00*/ WORD nValue;
  /*0x02*/ WORD nStrId;
};
static_assert(offsetof(gluCreate_cslot_ent_t, nValue) == 0x00, "cslot +0: cmp word ptr [esi], 1h");
static_assert(offsetof(gluCreate_cslot_ent_t, nStrId) == 0x02, "cslot +2: movzx ecx, word ptr [esi+2h]");
static_assert(sizeof(gluCreate_cslot_ent_t) == 4, "cslot stride: add esi, 4h");

// 0x4ED73C (.rdata), indexed by the speed scroller's position in three functions:
// `movsx ecx, word ptr [ebx*4+4ED73Ch]`, a scale of 4 with a signed 16-bit read at +0.
// Nothing here reads the +2 half, so it keeps a placeholder name; the image holds seven
// entries whose +2 words are 0..6, the entry's own ordinal, against string ids 85..91.
struct gluCreate_speed_ent_t {
  /*0x00*/ short nStrId;
  /*0x02*/ short _unmapped02;
};
static_assert(offsetof(gluCreate_speed_ent_t, nStrId) == 0x00, "speed +0: movsx ecx, word ptr [ebx*4+4ED73Ch]");
static_assert(offsetof(gluCreate_speed_ent_t, _unmapped02) == 0x02, "speed +2: unread here; image holds the entry ordinal 0..6");
static_assert(sizeof(gluCreate_speed_ent_t) == 4, "speed stride: the ebx*4 scale");

// The globals this TU reaches, spelled both ways with one guard per name: a `#define`
// over OC_VA under OC_DLL_MODE, a plain `extern` otherwise. The block sits below every
// include, so a name ../gen/globals117.h starts emitting wins the spelling rather than
// colliding with an arm above it. The names are the data sheet's, positional ones
// included; the types are derived here, each with its evidence beside it.

// 0x668A54 -- the map's player count: UpdateMapCtrl hands sub_455710 a
// `movzx` of a map byte or replay_get_max_players(), which it stores here.
// Signed int: gluCreate_subTypeDropDown_add_item tests it with `cmp eax, 2h / jl`.
#ifndef OC117_GLUCREAT_dword_668A54
#define OC117_GLUCREAT_dword_668A54
#ifdef OC_DLL_MODE
#define dword_668A54 (*reinterpret_cast<int *>(OC_VA(0x668A54u)))
#else
extern int dword_668A54;
#endif
#endif

// 0x668A58 -- `mov ecx, dword ptr [0x668A58]` in sub_4560D0 feeds
// glue_set_text's ECX, which is a dialog node.
#ifndef OC117_GLUCREAT_dword_668A58
#define OC117_GLUCREAT_dword_668A58
#ifdef OC_DLL_MODE
#define gCreateDlg_g (*reinterpret_cast<ctrl_t **>(OC_VA(0x668A58u)))
#else
extern ctrl_t *gCreateDlg_g;
#endif
#endif

// 0x668A5C -- `cmp dword ptr [0x668A5C], 0h` in sub_455710.  DWORD from the
// compare width; the only reading is a truth test, so `int`.
#ifndef OC117_GLUCREAT_dword_668A5C
#define OC117_GLUCREAT_dword_668A5C
#ifdef OC_DLL_MODE
#define sgbInCreateDlg_g (*reinterpret_cast<int *>(OC_VA(0x668A5Cu)))
#else
extern int sgbInCreateDlg_g;
#endif
#endif

// 0x668A60 -- sgGluCreateMapListCtrl_g, the map listbox control. A `ctrl_t *`: in
// sub_455810 it feeds lbox_add_item's and lbox_set_data's ECX, and both take a control.
// The spelling below is token-identical to the generated one, so the two agree if
// ../gen/globals117.h starts declaring it too.
#ifndef OC_SGGLUCREATEMAPLISTCTRL_G
#define OC_SGGLUCREATEMAPLISTCTRL_G
#ifdef OC_DLL_MODE
#define sgGluCreateMapListCtrl_g (*reinterpret_cast<ctrl_t **>(OC_VA(0x668A60u)))
#else
extern ctrl_t *sgGluCreateMapListCtrl_g;
#endif
#endif

// 0x668B6C -- the game-type dropdown; create_dlg_init stores ctrl_find_ndx(dlg, 0x11)
// here and sub_456990 hands it to lbox_add_item's ECX.
#ifndef OC117_GLUCREAT_dword_668B6C
#define OC117_GLUCREAT_dword_668B6C
#ifdef OC_DLL_MODE
#define sgCreateGametypeCtrl_g (*reinterpret_cast<ctrl_t **>(OC_VA(0x668B6Cu)))
#else
extern ctrl_t *sgCreateGametypeCtrl_g;
#endif
#endif

// 0x668F94 -- create_dlg_init stores ctrl_find_ndx(dlg, 0x13) here.
#ifndef OC117_GLUCREAT_dword_668F94
#define OC117_GLUCREAT_dword_668F94
#ifdef OC_DLL_MODE
#define sgCreateSlotsContainerCtrl_g (*reinterpret_cast<ctrl_t **>(OC_VA(0x668F94u)))
#else
extern ctrl_t *sgCreateSlotsContainerCtrl_g;
#endif
#endif

// 0x503DB0 -- the screen's event-function table.  create_dlg_init passes
// `push 98h` as glue_init_screen_dlg's cbTable, i.e. 0x98 / 4 = 38 entries.
#ifndef OC117_GLUCREAT_sCreateEvtFcnTbl_g
#define OC117_GLUCREAT_sCreateEvtFcnTbl_g
#ifdef OC_DLL_MODE
#define sCreateEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x503DB0u)))
#else
extern PFN_CTRL_EVT sCreateEvtFcnTbl_g[38] OC_UNALIGNED;
#endif
#endif

// 0x668A68 / 0x668B70 / 0x668C78 -- three MAX_PATH character buffers, each
// 0x104 below the next sheet name.  sub_455F50 compares the first two with
// SStrCmpI and hands 0x668C78 to FillMapList when its first byte is nonzero.
#ifndef OC117_GLUCREAT_sgMapCurrentDir_g
#define OC117_GLUCREAT_sgMapCurrentDir_g
#ifdef OC_DLL_MODE
#define sgMapCurrentDir_g (reinterpret_cast<char *>(OC_VA(0x668A68u)))
#else
extern char sgMapCurrentDir_g[0x104] OC_UNALIGNED;
#endif
#endif
#ifndef OC117_GLUCREAT_sgMapBaseDir_g
#define OC117_GLUCREAT_sgMapBaseDir_g
#ifdef OC_DLL_MODE
#define sgMapBaseDir_g (reinterpret_cast<char *>(OC_VA(0x668B70u)))
#else
extern char sgMapBaseDir_g[0x104];
#endif
#endif
#ifndef OC117_GLUCREAT_sgRestoreSelectEntry_g
#define OC117_GLUCREAT_sgRestoreSelectEntry_g
#ifdef OC_DLL_MODE
#define sgRestoreSelectEntry_g (reinterpret_cast<char *>(OC_VA(0x668C78u)))
#else
extern char sgRestoreSelectEntry_g[0x104] OC_UNALIGNED;
#endif
#endif

// 0x668E84 -- the selected sub-type's item data; the sub-type dropdown stores
// lbox_get_data's return here, or 1 on hide.  DWORD from the store width;
// InitSinglePlayerCustom and sub_455740 read its low WORD as the template type.
#ifndef OC117_GLUCREAT_sgCreateGameSubtypeId_g
#define OC117_GLUCREAT_sgCreateGameSubtypeId_g
#ifdef OC_DLL_MODE
#define sgCreateGameSubtypeId_g (*reinterpret_cast<DWORD *>(OC_VA(0x668E84u)))
#else
extern DWORD sgCreateGameSubtypeId_g;
#endif
#endif

// 0x668F90 -- `mov dword ptr [0x668F90], 1h` in gluCreate_listbox_ctrl_fcn.
#ifndef OC117_GLUCREAT_dword_668F90
#define OC117_GLUCREAT_dword_668F90
#ifdef OC_DLL_MODE
#define dword_668F90 (*reinterpret_cast<int *>(OC_VA(0x668F90u)))
#else
extern int dword_668F90;
#endif
#endif

// 0x503E48 -- the screen's transition record, glues_transition_in's EDX; the
// sheet names it.  Extent not derived beyond the address.
#ifndef OC117_GLUCREAT_sCreateAnims_g
#define OC117_GLUCREAT_sCreateAnims_g
#ifdef OC_DLL_MODE
#define sCreateAnims_g (reinterpret_cast<void *>(OC_VA(0x503E48u)))
#else
extern BYTE sCreateAnims_g[8];
#endif
#endif

// 0x6CE6DC -- registered as gnDlgReturnVal (`long`); gen emits it only for a
// marked row that lists it.
#ifndef OC_GNDLGRETURNVAL
#define OC_GNDLGRETURNVAL
#ifndef gnDlgReturnVal
#ifdef OC_DLL_MODE
#define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
#else
extern LONG gnDlgReturnVal;
#endif
#endif
#endif

// 0x6728D0 -- gGameHeader, dual-spelled as gametype.cpp does; not registered
// because it is a struct.
#ifndef gGameHeader
#ifdef OC_DLL_MODE
#define gGameHeader (*reinterpret_cast<GameHeader *>(OC_VA(0x6728D0u)))
#else
extern GameHeader gGameHeader;
#endif
#endif

// 0x669730 -- the game-type label buffer.  0x20 is the capacity SStrCopy and
// SStrPack are told; the extent beyond it is not derived.
#ifndef OC117_GLUCREAT_game_subtype_label_buf_g
#define OC117_GLUCREAT_game_subtype_label_buf_g
#ifdef OC_DLL_MODE
#define game_subtype_label_buf_g (reinterpret_cast<char *>(OC_VA(0x669730u)))
#else
extern char game_subtype_label_buf_g[0x20];
#endif
#endif

// 0x668F8C -- registered as gpCreateIcons_g with no type; it is grp_to_bmpx's
// ECX, a GROUP pointer.
#ifndef OC117_GLUCREAT_gpCreateIcons_g
#define OC117_GLUCREAT_gpCreateIcons_g
#ifdef OC_DLL_MODE
#define gpCreateIcons_g (*reinterpret_cast<GROUP **>(OC_VA(0x668F8Cu)))
#else
extern GROUP *gpCreateIcons_g;
#endif
#endif

// 0x668F98 -- do_create_dlg stores its own ECX here; the sheet names it.
#ifndef OC117_GLUCREAT_gbCreateReplay_g
#define OC117_GLUCREAT_gbCreateReplay_g
#ifdef OC_DLL_MODE
#define gbCreateReplay_g (*reinterpret_cast<int *>(OC_VA(0x668F98u)))
#else
extern int gbCreateReplay_g;
#endif
#endif

// 0x536720 -- the expansion gate, spelled as gametype.cpp and CUnitUpgrade.cpp
// spell it; unregistered.
#ifndef gbIsExpansion
#ifdef OC_DLL_MODE
#define gbIsExpansion (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE gbIsExpansion;
#endif
#endif

// 0x6690A8 .. 0x6692A8 -- five 0x80-byte label buffers, one per map-info
// kind in sub_455D10; 0x80 is the capacity psnprintf is told and the stride
// between the sheet's five names.
#ifndef OC117_GLUCREAT_byte_6690A8
#define OC117_GLUCREAT_byte_6690A8
#ifdef OC_DLL_MODE
#define byte_6690A8 (reinterpret_cast<char *>(OC_VA(0x6690A8u)))
#else
extern char byte_6690A8[0x80] OC_UNALIGNED;
#endif
#endif
#ifndef OC117_GLUCREAT_byte_669128
#define OC117_GLUCREAT_byte_669128
#ifdef OC_DLL_MODE
#define byte_669128 (reinterpret_cast<char *>(OC_VA(0x669128u)))
#else
extern char byte_669128[0x80] OC_UNALIGNED;
#endif
#endif
#ifndef OC117_GLUCREAT_byte_6691A8
#define OC117_GLUCREAT_byte_6691A8
#ifdef OC_DLL_MODE
#define byte_6691A8 (reinterpret_cast<char *>(OC_VA(0x6691A8u)))
#else
extern char byte_6691A8[0x80] OC_UNALIGNED;
#endif
#endif
#ifndef OC117_GLUCREAT_byte_669228
#define OC117_GLUCREAT_byte_669228
#ifdef OC_DLL_MODE
#define byte_669228 (reinterpret_cast<char *>(OC_VA(0x669228u)))
#else
extern char byte_669228[0x80] OC_UNALIGNED;
#endif
#endif
#ifndef OC117_GLUCREAT_byte_6692A8
#define OC117_GLUCREAT_byte_6692A8
#ifdef OC_DLL_MODE
#define byte_6692A8 (reinterpret_cast<char *>(OC_VA(0x6692A8u)))
#else
extern char byte_6692A8[0x80] OC_UNALIGNED;
#endif
#endif

// 0x668D80 / 0x668E88 / 0x669328 -- the list entry and directory remembered
// between visits, and the first-visit flag; sheet names.  0x104 is the
// capacity SStrCopy is told.
#ifndef OC117_GLUCREAT_sgCurrentListEntryStr_g
#define OC117_GLUCREAT_sgCurrentListEntryStr_g
#ifdef OC_DLL_MODE
#define sgCurrentListEntryStr_g (reinterpret_cast<char *>(OC_VA(0x668D80u)))
#else
extern char sgCurrentListEntryStr_g[0x104];
#endif
#endif
#ifndef OC117_GLUCREAT_sgRestoreDirectory_g
#define OC117_GLUCREAT_sgRestoreDirectory_g
#ifdef OC_DLL_MODE
#define sgRestoreDirectory_g (reinterpret_cast<char *>(OC_VA(0x668E88u)))
#else
extern char sgRestoreDirectory_g[0x104] OC_UNALIGNED;
#endif
#endif
#ifndef OC117_GLUCREAT_sgAlreadyOpenedOnce
#define OC117_GLUCREAT_sgAlreadyOpenedOnce
#ifdef OC_DLL_MODE
#define sgAlreadyOpenedOnce (*reinterpret_cast<int *>(OC_VA(0x669328u)))
#else
extern int sgAlreadyOpenedOnce;
#endif
#endif

// 0x5264C3, 0x526546 and 0x526547 -- game.cpp's spelling for the same three
// bytes.  InitSinglePlayerCustom stores 7 into eight bytes from 0x526547 as two dwords and
// then fills them one per shown race dropdown.
#ifdef OC_DLL_MODE
  #ifndef byte_5264C3_g
  #define byte_5264C3_g (*reinterpret_cast<BYTE *>(OC_VA(0x5264C3u)))
  #endif
  #ifndef byte_526546_g
  #define byte_526546_g (*reinterpret_cast<BYTE *>(OC_VA(0x526546u)))
  #endif
  #ifndef byte_526547_g
  #define byte_526547_g (reinterpret_cast<BYTE *>(OC_VA(0x526547u)))
  #endif
#else
extern BYTE  byte_5264C3_g;
extern BYTE  byte_526546_g;
extern BYTE  byte_526547_g[8];
#endif

// 0x668C74 -- the selected game type's item data: `mov ecx, dword ptr [0x668C74]`
// then `shr edx, 8h` feeds IsMeleeTemplate its category (CL) and sub (DL), so
// the slot is a DWORD; sub_455710's `cmp byte ptr [0x668C74], 0Fh` reads its
// low byte.
#ifndef OC117_GLUCREAT_byte_668C74
#define OC117_GLUCREAT_byte_668C74
#ifdef OC_DLL_MODE
#define sgCreateGameTypeId_g (*reinterpret_cast<DWORD *>(OC_VA(0x668C74u)))
#else
extern DWORD sgCreateGameTypeId_g;
#endif
#endif

// 0x668D7C -- `mov ecx, dword ptr [0x668D7C]` in sub_455710 feeds
// gluCreate_subTypeDropDown_init_items, whose ECX is a control.
#ifndef OC117_GLUCREAT_dword_668D7C
#define OC117_GLUCREAT_dword_668D7C
#ifdef OC_DLL_MODE
#define sgCreateSubtypeCtrl_g (*reinterpret_cast<ctrl_t **>(OC_VA(0x668D7Cu)))
#else
extern ctrl_t *sgCreateSubtypeCtrl_g;
#endif
#endif

// 0x668FA0 -- a character buffer: sub_4560D0 hands it to SStrCopy with a capacity of
// 0x104 and then to strrchr. Nothing bounds it beyond that 0x104, so never `sizeof`
// this.
#ifndef OC117_GLUCREAT_szMapPath_668FA0
#define OC117_GLUCREAT_szMapPath_668FA0
#ifdef OC_DLL_MODE
#define byte_668FA0 (reinterpret_cast<char *>(OC_VA(0x668FA0u)))
#else
extern char byte_668FA0[0x104];
#endif
#endif

// 0x503D9C -- two entries: gluCreate_computerSlotDropDown_ctrl_activate walks
// [0x503D9C, 0x503DA4), 8 bytes at a stride of 4.
#ifndef OC117_GLUCREAT_g_503D9C
#define OC117_GLUCREAT_g_503D9C
#ifdef OC_DLL_MODE
#define stru_503D9C (reinterpret_cast<gluCreate_cslot_ent_t *>(OC_VA(0x503D9Cu)))
#else
extern gluCreate_cslot_ent_t stru_503D9C[2];
#endif
#endif

// 0x503DA4 -- four entries: gluCreate_raceDropDown_ctrl_activate walks
// [0x503DA4, 0x503DAC), 8 bytes at a stride of 2. The two arrays are adjacent and each
// one's end is the other's base, which corroborates both extents at once.
#ifndef OC117_GLUCREAT_g_503DA4
#define OC117_GLUCREAT_g_503DA4
#ifdef OC_DLL_MODE
#define stru_503DA4 (reinterpret_cast<gluCreate_race_ent_t *>(OC_VA(0x503DA4u)))
#else
extern gluCreate_race_ent_t stru_503DA4[4];
#endif
#endif

// 0x4ED73C -- seven entries, from the image rather than the instruction stream: every
// loop over this table is bounded by word_503D98 at run time, whose initialised .data
// word is 7, and the entries 0..6 are a clean ascending run ending where ASCII string
// data starts at 0x4ED758.
// Flagged as image-derived rather than stream-derived: if word_503D98 is ever written
// at runtime the loops honour the variable, and this extent is only the initial one.
#ifndef OC117_GLUCREAT_g_4ED73C
#define OC117_GLUCREAT_g_4ED73C
#ifdef OC_DLL_MODE
#define sgSpeedStrMap_g (reinterpret_cast<const gluCreate_speed_ent_t *>(OC_VA(0x4ED73Cu)))
#else
extern const gluCreate_speed_ent_t sgSpeedStrMap_g[7] OC_UNALIGNED;
#endif
#endif

// 0x503D98 -- the number of game-speed entries (7).  Signed 16-BIT: every
// reader is `movsx eax, word ptr [0x503D98]` or `mov cx, word ptr [0x503D98]`
// followed by `movsx eax, cx`.
#ifndef OC117_GLUCREAT_word_503D98
#define OC117_GLUCREAT_word_503D98
#ifdef OC_DLL_MODE
#define word_503D98 (*reinterpret_cast<short *>(OC_VA(0x503D98u)))
#else
extern short word_503D98;
#endif
#endif

// 0x668A64 -- `mov byte ptr [0x668A64], bl` in gluCreate_speedScroll_updateText
// publishes the scroller position.  BYTE from the store width.
#ifndef OC117_GLUCREAT_byte_668A64
#define OC117_GLUCREAT_byte_668A64
#ifdef OC_DLL_MODE
#define sgbCreateGameSpeed_g (*reinterpret_cast<BYTE *>(OC_VA(0x668A64u)))
#else
extern BYTE sgbCreateGameSpeed_g;
#endif
#endif

// 0x669330 -- the speed label formatting buffer.  0x400 is the capacity
// psnprintf is told (`push 400h / push 669330h`) and the same 0x400 the
// preceding length check compares against.  Never `sizeof` this.
#ifndef OC117_GLUCREAT_szSpeedText_669330
#define OC117_GLUCREAT_szSpeedText_669330
#ifdef OC_DLL_MODE
#define game_speed_label_buf (reinterpret_cast<char *>(OC_VA(0x669330u)))
#else
extern char game_speed_label_buf[0x400];
#endif
#endif

// The bodies, in oracle VA order.

// ---------------------------------------------------------------------------
// oracle 0x454CA0 (146 B).
//
// `dec eax / cdq / sub eax, edx / sar eax, 1h` is signed division by two of
// (count - 1) -- the midpoint speed, used both when the registry has no value
// and when the stored name matches nothing.
BYTE gluCreate_get_config_game_speed()
{
  char szSpeed[0x80];

  // Two textual returns rather than one shared tail: the oracle computes the midpoint
  // twice, and one shared `return` after the `if` is 16 bytes short.
  if (!SRegLoadString("Starcraft", "Game Speed", 0, szSpeed, 0x80))
    return static_cast<BYTE>((word_503D98 - 1) / 2);

  for (int i = 0; i < word_503D98; i++)
  {
    // The call is its own statement: written inline as _stricmp's first argument,
    // right-to-left evaluation pushes the buffer address before the call, where the
    // oracle calls glues_get_str first.
    char *pszName = glues_get_str(sgSpeedStrMap_g[i].nStrId);
    if (_stricmp(pszName, szSpeed) == 0)
      return static_cast<BYTE>(i);
  }

  return static_cast<BYTE>((word_503D98 - 1) / 2);
}

// ---------------------------------------------------------------------------
// oracle 0x454D40 (101 B).  Restores the saved game type into the dropdown;
// 1 when the registry name was found in the list.
int gluCreate_get_game_type_setting(intptr_t nArg0)
{
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);
  char szType[0x20];

  if (lbox_get_count(pBox) == 0)
    return 0;

  if (SRegLoadString("Starcraft", gbMultiPlayerMode ? "Game Type" : "Custom Type",
                     0, szType, 0x20) &&
      lbox_set_match(pBox, szType) != 0xFF)
    return 1;

  lbox_set_selection(pBox, 0);
  return 0;
}

// ---------------------------------------------------------------------------
// oracle 0x454DB0 (88 B).
//
// The four-instruction loop between glues_get_str and SRegSaveString
// (`mov cl,[eax] / lea eax,[eax+1] / mov [edx+eax-1],cl / test cl,cl / jnz`)
// is the inlined strcpy MSVC emits under /Oi, not a hand-written loop.
void gluCreate_speedScroll_ctrl_deactivate(intptr_t nArg0)
{
  char szSpeed[0x80];
  int nPos = scroll_get_pos(reinterpret_cast<ctrl_t *>(nArg0));

  if (nPos >= 0 && nPos < word_503D98)
  {
    strcpy(szSpeed, glues_get_str(sgSpeedStrMap_g[nPos].nStrId));
    SRegSaveString("Starcraft", "Game Speed", 0, szSpeed);
  }
}

// oracle 0x454E10 (63 B). The `cmovz eax, ecx` is a select between two .rdata string
// pointers rather than a branch -- a ternary over two literals, taking "Custom Type" when
// gbMultiPlayerMode is 0 and "Game Type" otherwise. The `movzx eax, al / cmp eax, 0FFh`
// is a byte-returning callee's result kept in an `int`.
void gluCreate_gameTypeDropDown_ctrl_deactivate(intptr_t nArg0)
{
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);
  int nSel = lbox_get_selection(pBox);

  if (nSel != 0xFF)
  {
    SRegSaveString("Starcraft",
                   gbMultiPlayerMode ? "Game Type" : "Custom Type",
                   0,
                   reinterpret_cast<const char *>(
                       lbox_get_item(pBox, static_cast<unsigned char>(nSel))));
  }
}

// ---------------------------------------------------------------------------
// oracle 0x454E50 (455 B).  Locks the game-setting controls to a saved map's
// own settings, or releases them for an ordinary map.
void sub_454E50()
{
  BYTE mapInfo[0xB4];

  if (!CreateMapInfo(lbox_get_data(reinterpret_cast<lbox_t *>(sgGluCreateMapListCtrl_g),
                                   lbox_get_selection(reinterpret_cast<lbox_t *>(
                                       sgGluCreateMapListCtrl_g))),
                     mapInfo))
    return;

  ctrl_t *pType = ctrl_find_ndx(sgGluCreateMapListCtrl_g, 0x11);
  ctrl_t *pSubtype = ctrl_find_ndx(sgGluCreateMapListCtrl_g, 0x12);
  ctrl_t *pSpeed = ctrl_find_ndx(sgGluCreateMapListCtrl_g, 0x10);
  ctrl_t *pLabel = ctrl_find_ndx(sgGluCreateMapListCtrl_g, 0x26);

  if (*reinterpret_cast<DWORD *>(&mapInfo[0]) != 0 && (mapInfo[4] & 0x80))
  {
    if (pType != NULL)
    {
      if (gbMultiPlayerMode)
      {
        // The item data is re-read from mapInfo each pass: CreateMapInfo took its
        // address, so no local copy survives the calls.
        int nFound = -1;
        for (int i = 0; i < lbox_get_count(reinterpret_cast<lbox_t *>(pType)); i++)
        {
          if (reinterpret_cast<DWORD_PTR>(lbox_get_data(reinterpret_cast<lbox_t *>(pType),
                                                    static_cast<unsigned char>(i))) ==
              mapInfo[0x91])
          {
            nFound = i;
            break;
          }
        }
        if (nFound != -1 && nFound != lbox_get_selection(reinterpret_cast<lbox_t *>(pType)))
          ctrl_set_val(pType, nFound);
        ctrl_gray(pType);
      }
      else
      {
        ctrl_hide(pType);
        if (pLabel != NULL)
          ctrl_hide(pLabel);
      }
    }
    if (pSubtype != NULL)
    {
      int nFound = -1;
      for (int i = 0; i < lbox_get_count(reinterpret_cast<lbox_t *>(pSubtype)); i++)
      {
        if (reinterpret_cast<DWORD_PTR>(lbox_get_data(reinterpret_cast<lbox_t *>(pSubtype),
                                                  static_cast<unsigned char>(i))) ==
            *reinterpret_cast<WORD *>(&mapInfo[0x93]))
        {
          nFound = i;
          break;
        }
      }
      if (nFound != -1 && nFound != lbox_get_selection(reinterpret_cast<lbox_t *>(pSubtype)))
        ctrl_set_val(pSubtype, nFound);
      ctrl_gray(pSubtype);
    }
    if (pSpeed != NULL)
    {
      ctrl_set_val(pSpeed, mapInfo[0x4A]);
      ctrl_gray(pSpeed);
    }
    return;
  }

  if (pType != NULL)
  {
    ctrl_show(pType);
    ctrl_ungray(pType);
  }
  if (pLabel != NULL)
    ctrl_show(pLabel);
  if (pSubtype != NULL)
    ctrl_ungray(pSubtype);
  if (pSpeed != NULL)
    ctrl_ungray(pSpeed);
}

// ---------------------------------------------------------------------------
// oracle 0x455020 (189 B).  Commits the race dropdowns: the local slot's race
// for a normal type, or every shown dropdown's race for a melee type.
void InitSinglePlayerCustom(intptr_t nArg0)
{
  ctrl_t *pCtrl = ctrl_find_ndx(reinterpret_cast<ctrl_t *>(nArg0), 0x1C);

  if (!IsMeleeTemplate(static_cast<BYTE>(sgCreateGameTypeId_g),
                       static_cast<BYTE>(sgCreateGameTypeId_g >> 8),
                       static_cast<WORD>(sgCreateGameSubtypeId_g)))
  {
    if (pCtrl->dwFlags & 8)
    {
      unsigned char nSel = lbox_get_selection(reinterpret_cast<lbox_t *>(pCtrl));
      if (nSel != 0xFF)
        slot_set_race(static_cast<BYTE>(reinterpret_cast<uintptr_t>(
            lbox_get_data(reinterpret_cast<lbox_t *>(pCtrl), nSel))));
      else
        slot_set_race(6);
    }
    return;
  }

  byte_5264C3_g = 1;
  memset(byte_526547_g, 7, 8);
  unsigned char nSel = lbox_get_selection(reinterpret_cast<lbox_t *>(pCtrl));
  if (nSel != 0xFF)
    byte_526546_g = static_cast<BYTE>(reinterpret_cast<uintptr_t>(
        lbox_get_data(reinterpret_cast<lbox_t *>(pCtrl), nSel)));

  ctrl_t *p = pCtrl->pNext;
  if (p == NULL)
    return;

  BYTE *pRace = byte_526547_g;
  do
  {
    if (p->dwFlags & 8)
    {
      unsigned char nItem = lbox_get_selection(reinterpret_cast<lbox_t *>(p));
      if (nItem != 0xFF)
        *pRace++ = static_cast<BYTE>(reinterpret_cast<uintptr_t>(
            lbox_get_data(reinterpret_cast<lbox_t *>(p), nItem)));
    }
    p = p->pNext;
  } while (p != NULL);
}

// ---------------------------------------------------------------------------
// oracle 0x4550E0 (227 B). The create-game screen's dialog handler.
int create_interact_fcn(ctrl_t *pDlg, dlgevt_t *pEvent)
{
  switch (pEvent->wTag)
  {
  case 0x0E:
    switch (pEvent->dwId)
    {
    case 2:
      if (gnDlgReturnVal == 0x0C && !sub_455240())
        return 1;
      timer_remove_node(gCreateDlg_g, 5);
      return glues_transition_out(pDlg);
    case 0:
      glues_transition_in(reinterpret_cast<intptr_t>(pDlg), sCreateAnims_g,
                          5, 2, 0);
      ctrl_find_ndx(pDlg, 6)->pfnDraw = draw_glucreate_directory_label;
      glue_screen_activate(pDlg);
      break;
    case 0x0A:
      create_dlg_init(reinterpret_cast<intptr_t>(pDlg));
      break;
    case 0x405:
      sub_455F50();
      break;
    }
    break;
  case 0x11:
    ctrl_send_msg(*reinterpret_cast<ctrl_t **>(sgGluCreateMapListCtrl_g->_unmapped36), 0x0F, 0);
    return 1;
  case 0x12:
    ctrl_send_msg(*reinterpret_cast<ctrl_t **>(sgGluCreateMapListCtrl_g->_unmapped36), 0x10, 0);
    return 1;
  }

  return ctrl_dlg(pDlg, pEvent);
}

// ---------------------------------------------------------------------------
// oracle 0x4551D0 (103 B).  Binds the screen's event table and caches three
// child controls; single player takes the game speed from gOptions.
void create_dlg_init(intptr_t nArg0)
{
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);

  glue_init_screen_dlg(pDlg, sCreateEvtFcnTbl_g, 0x98, 0);
  sgCreateSubtypeCtrl_g = ctrl_find_ndx(pDlg, 0x12);
  sgCreateGametypeCtrl_g = ctrl_find_ndx(pDlg, 0x11);
  sgCreateSlotsContainerCtrl_g = ctrl_find_ndx(pDlg, 0x13);
  sgbCreateGameSpeed_g = !gbMultiPlayerMode ? static_cast<BYTE>(gOptions)
                                            : sgbCreateGameSpeed_g;
}

// ---------------------------------------------------------------------------
// oracle 0x455240 (542 B).  Loads the selected map for the game about to be
// created; reports each load error, and on success remembers the directory
// and entry for the next visit.
int sub_455240()
{
  char szPath[0x104];

  unsigned char nSel = lbox_get_selection(reinterpret_cast<lbox_t *>(sgGluCreateMapListCtrl_g));
  if (nSel == 0xFF)
    return 0;

  void *pEntry = lbox_get_data(reinterpret_cast<lbox_t *>(sgGluCreateMapListCtrl_g), nSel);
  DWORD nType = sgCreateGameTypeId_g;
  DWORD nPacked = ((static_cast<BYTE>(nType >> 8) |
                    (static_cast<DWORD>(static_cast<WORD>(sgCreateGameSubtypeId_g)) << 8)) << 8) |
                  static_cast<BYTE>(nType);
  int nResult = CreateNewGame(reinterpret_cast<intptr_t>(pEntry), reinterpret_cast<intptr_t>(gLocalPlayerName), 0,
                           nPacked, sgbCreateGameSpeed_g, sgMapCurrentDir_g);
  switch (nResult)
  {
  case static_cast<int>(0x80000007):
    do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x33)));
    return 0;
  case static_cast<int>(0x80000001):
    do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x3C)));
    return 0;
  case static_cast<int>(0x80000008):
    do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x36)));
    return 0;
  case static_cast<int>(0x80000005):
    do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x37)));
    return 0;
  case static_cast<int>(0x8000000B):
    sub_4560D0(reinterpret_cast<intptr_t>(sgMapCurrentDir_g));
    sgRestoreSelectEntry_g[0] = 0;
    sub_455F50();
    return 0;
  case static_cast<int>(0x8000000C):
    do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x3A)));
    return 0;
  case static_cast<int>(0x8000000D):
    do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x39)));
    return 0;
  case static_cast<int>(0x80000002):
    do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x3B)));
    return 0;
  case 0:
    if (!gbCreateReplay_g)
    {
      glues_make_full_path(const_cast<char *>(""), szPath);
      strcpy(sgRestoreDirectory_g, sgMapCurrentDir_g);
      strcpy(sgCurrentListEntryStr_g, static_cast<const char *>(lbox_get_item(
                                          reinterpret_cast<lbox_t *>(sgGluCreateMapListCtrl_g),
                                          nSel)));
    }
    if (!gbMultiPlayerMode)
      return sub_4554A0();
    return 1;
  default:
    if (SErrGetLastError() != 0x4C6)
    {
      do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x34)));
      return 0;
    }
    if (static_cast<DWORD>(NetworkMode()) == 0x4D4F444D)
      do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x4A)));
    else
      do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x38)));
    return 0;
  }
}

// ---------------------------------------------------------------------------
// oracle 0x4554A0 (176 B).  Validates the selection before the game is
// created; 0 after telling the player the map needs an opponent.
BOOL sub_4554A0()
{
  BYTE mapInfo[0xB4];

  timer_remove_node(gCreateDlg_g, 5);
  unsigned char nSel = lbox_get_selection(reinterpret_cast<lbox_t *>(sgGluCreateMapListCtrl_g));
  if (nSel == 0xFF)
    return 1;
  // Assigned twice so the second test reads a materialised bool (setnz).
  int bCheck = CreateMapInfo(lbox_get_data(reinterpret_cast<lbox_t *>(sgGluCreateMapListCtrl_g),
                                           nSel),
                             mapInfo);
  if (bCheck)
    bCheck = mapInfo[4] != 8;
  if (!bCheck)
    return 1;

  dword_668F90 = 0;
  gluCreate_computerSlots_update(reinterpret_cast<intptr_t>(gCreateDlg_g), 5);
  if (!sub_455740(reinterpret_cast<intptr_t>(gCreateDlg_g)) && !gPlayback)
  {
    do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x39)));
    return 0;
  }

  InitSinglePlayerCustom(reinterpret_cast<intptr_t>(gCreateDlg_g));
  return 1;
}

// ---------------------------------------------------------------------------
// oracle 0x455550 (433 B). Runs the create-game dialog and picks the next glue screen
// from how it closed.
void do_create_dlg(int nArg0)
{
  gbCreateReplay_g = nArg0;
  // Hyperion: a single-player game is set up on Hyperion's Skirmish screen when the
  // front end is on (port_front_skirmish.cpp), and a replay chosen on its Replays screen
  // (port_front_replays.cpp). Each starts the game as Play Custom's or the replay list's Ok
  // does and gives 0x0C, or 0 for its way home; -1 when 1.17's screen runs.
  const int nFront = gbMultiPlayerMode ? -1
                     : nArg0          ? PortFrontReplays()
                                      : PortFrontSkirmish(static_cast<BYTE>(gOptions));
  int nResult = nFront;
  if (nFront == 0) {
    gPlayback = 0;
    map_free();
  } else if (nFront < 0) {
    // Two calls, one per arm: a single call over a ternary if-converts to cmov.
    if (!gbMultiPlayerMode)
      gCreateDlg_g = reinterpret_cast<ctrl_t *>(
          glue_load_cd_dlg(reinterpret_cast<intptr_t>("rez\\gluCustm.bin")));
    else
      gCreateDlg_g = reinterpret_cast<ctrl_t *>(
          glue_load_cd_dlg(reinterpret_cast<intptr_t>("rez\\gluCreat.bin")));
    gpCreateIcons_g = ReadGroupFile("glue\\create\\iCreate.grp", 0, "gluCreat.cpp", 0x593);
    sgbInCreateDlg_g = 1;
    nResult = dlg_run_modal(gCreateDlg_g, 0, create_interact_fcn);
  }

  switch (nResult)
  {
  case -3:
  case 0x0D:
    gPlayback = 0;
    map_free();
    if (!gbMultiPlayerMode)
    {
      glGluesMode = gbIsExpansion ? 0x16 : 6;
    }
    else
    {
      switch (static_cast<DWORD>(NetworkMode()))
      {
      case 0x4D444D58:
      case 0x4D4F444D:
        glGluesMode = 0x14;
        break;
      case 0x424E4554:
        glGluesMode = 4;
        break;
      case 0x5343424C:
        glGluesMode = 0x15;
        break;
      default:
        glGluesMode = 0x0A;
        break;
      }
    }
    break;
  case 0x0C:
    if (gbMultiPlayerMode)
    {
      glGluesMode = 3;
    }
    else if (IsScenarioGame(0) && !gPlayback)
    {
      switch (gPlayerData[g_LocalNationID].nRace)
      {
      case 0:
        glGluesMode = 8;
        break;
      case 1:
        glGluesMode = 7;
        break;
      case 2:
        glGluesMode = 9;
        break;
      }
    }
    else
    {
      gwGameMode = 1;
    }
    break;
  default:
    glGluesMode = 0;
    break;
  }

  glues_clear_all();
  if (nFront >= 0)
    return;
  mem_free(gpCreateIcons_g, "gluCreat.cpp", 0x5CC);
  sgbInCreateDlg_g = 0;
}

// ---------------------------------------------------------------------------
// oracle 0x455710 (41 B). The `cmp byte ptr [0x668C74], 0Fh` is scheduled above the
// `mov [0x668A54], ecx` store, which it does not depend on, so MSVC hoists it into the
// load slot. Both live inside the outer guard.
void sub_455710(int nArg0)
{
  if (sgbInCreateDlg_g != 0)
  {
    // The store is duplicated into both arms on purpose: written once above the guard
    // it matches in the plain form, but under OC_DLL_MODE the global is a
    // constant-address rvalue, the hoist does not happen and the pair comes out in
    // source order. Duplicated, MSVC cross-jumps the arms back into one store.
    if (static_cast<BYTE>(sgCreateGameTypeId_g) == 0x0F)
    {
      dword_668A54 = nArg0;
      gluCreate_subTypeDropDown_init_items(reinterpret_cast<intptr_t>(sgCreateSubtypeCtrl_g));
    }
    else
    {
      dword_668A54 = nArg0;
    }
    sub_454E50();
  }
}

// ---------------------------------------------------------------------------
// oracle 0x455740 (199 B).  Whether the chosen setup has an opponent: the
// map's own count for a simple type, else any shown slot set to computer.
BOOL sub_455740(intptr_t nArg0)
{
  BYTE mapInfo[0xB4];

  if (IsSimpleTemplate(static_cast<BYTE>(sgCreateGameTypeId_g),
                       static_cast<BYTE>(sgCreateGameTypeId_g >> 8),
                       static_cast<WORD>(sgCreateGameSubtypeId_g)))
  {
    unsigned char nSel = lbox_get_selection(reinterpret_cast<lbox_t *>(sgGluCreateMapListCtrl_g));
    if (nSel == 0xFF)
      return 0;
    if (!CreateMapInfo(lbox_get_data(reinterpret_cast<lbox_t *>(sgGluCreateMapListCtrl_g), nSel),
                       mapInfo))
      return 0;
    // Through an int so the select stays 32-bit (a cmovz, not a branch).
    int nPlayers = mapInfo[8];
    BYTE nCount = static_cast<BYTE>(mapInfo[4] == 4 ? nPlayers : 0xFF);
    if (nCount == 0 || nCount == 0xFF)
      return 0;
    return 1;
  }

  for (ctrl_t *p = ctrl_find_ndx(reinterpret_cast<ctrl_t *>(nArg0), 0x15);
       p != NULL && p->nIndex <= 0x1B; p = p->pNext)
  {
    if (p->dwFlags & 8)
    {
      unsigned char nItem = lbox_get_selection(reinterpret_cast<lbox_t *>(p));
      if (nItem != 0xFF &&
          lbox_get_data(reinterpret_cast<lbox_t *>(p), nItem) == reinterpret_cast<void *>(1))
        return 1;
    }
  }
  return 0;
}

// ---------------------------------------------------------------------------
// oracle 0x455810 (54 B). It returns `int`, not `void`: the instruction before the
// epilogue is `mov eax, ebx`, an explicit return of the zero-extended item index, and EBX
// is saved only because that index has to survive the lbox_set_data call. Compiled `void`
// the body is 49 bytes against the oracle's 54.
int __stdcall sub_455810(intptr_t nArg0, intptr_t nArg1, int nArg2)
{
  (void)nArg2;
  // A byte-wide callee result widened into an int: lbox_add_item really returns a byte,
  // and save_dlg.h declares it `int`, so the cast stays local rather than changing that
  // declaration.
  int nItem = static_cast<unsigned char>(
      lbox_add_item(sgGluCreateMapListCtrl_g,
                    reinterpret_cast<const char *>(nArg1), 3));

  if (nItem != 0xFF)
    lbox_set_data(reinterpret_cast<lbox_t *>(sgGluCreateMapListCtrl_g),
                  static_cast<unsigned char>(nItem),
                  reinterpret_cast<void *>(nArg0));

  return nItem;
}

// ---------------------------------------------------------------------------
// oracle 0x455850 (373 B).  The map listbox's per-item draw callback: a
// folder/map icon by map kind and hover state, then the clipped item text.
void sub_455850(void *pArg0, BYTE bArg1, void *pArg2, int nArg3, int nArg4)
{
  ctrl_t *pBox = reinterpret_cast<ctrl_t *>(pArg0);
  BYTE nItem = bArg1;
  char szText[0x104];
  BYTE mapInfo[0xB4];
  bmp_t bmp;
  short nA, nB, nC, nD;
  rect16_t rc;
  int bHot;
  int nFrame;
  intptr_t nWidth;

  bHot = (nItem == pBox->_unmapped4E[0]);
  nFrame = 0;
  if (CreateMapInfo(lbox_get_data(reinterpret_cast<lbox_t *>(pBox), nItem), mapInfo))
  {
    if (mapInfo[4] == 8)
      nFrame = (bHot != 0);
    else
      nFrame = (bHot != 0) + 3;
  }

  rc = *reinterpret_cast<rect16_t *>(pArg2);
  grp_to_bmpx(gpCreateIcons_g, static_cast<unsigned char>(nFrame), &bmp, &nA, &nB, &nC, &nD);
  rc.left += static_cast<short>(nArg3);
  rc.top += static_cast<short>((rc.bottom - rc.top + 1) / 2 - bmp.nHeight / 2 + nArg4);
  draw_bmp_effect(&bmp, 0, 0, &rc, 0x2000);

  nArg3 += bmp.nWidth + 2;
  font_set_color_tbl(static_cast<unsigned char>((bHot != 0) + 3));

  gbFontJust = 0x11;
  gsFontMargins = *reinterpret_cast<rect16_t *>(pArg2);
  gsFontMargins.left += static_cast<short>(nArg3);
  gsFontMargins.top += static_cast<short>(nArg4);
  gsFontMargins.right += static_cast<short>(nArg3);
  gsFontMargins.bottom += static_cast<short>(nArg4);

  nWidth = string_width(
      LBOX_ITEMS(pBox)[nItem],
      reinterpret_cast<rect16_t *>(pArg2)->right
          - reinterpret_cast<rect16_t *>(pArg2)->left - nArg3 + 1) + 1;
  if (static_cast<unsigned int>(nWidth) > 0x104u)
    nWidth = 0x104;
  SStrCopy(szText, LBOX_ITEMS(pBox)[nItem], nWidth);

  font_adjust(reinterpret_cast<rect16_t *>(pArg2)->left + nArg3,
              reinterpret_cast<rect16_t *>(pArg2)->top + nArg4, szText);
}

// ---------------------------------------------------------------------------
// oracle 0x4559D0 (170 B).  The map listbox's event handler.
BOOL gluCreate_listbox_ctrl_fcn(intptr_t nArg0, intptr_t nArg1)
{
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E)
  {
    switch (pEvent->dwId)
    {
    case 0x0A:
      pCtrl->dwFlags |= 0x30000;
      sgGluCreateMapListCtrl_g = pCtrl;
      break;
    case 0:
      gluCreate_listbox_ctrl_activate(nArg0);
      break;
    case 1:
      lbox_remove_all(reinterpret_cast<lbox_t *>(sgGluCreateMapListCtrl_g));
      ClearMapList();
      break;
    case 0x0B:
    {
      lbox_ctrl_list(pCtrl, pEvent);
      int nSel = lbox_get_selection(reinterpret_cast<lbox_t *>(pCtrl));
      if (nSel != 0xFF)
        sub_45B970(lbox_get_data(reinterpret_cast<lbox_t *>(pCtrl),
                                 static_cast<unsigned char>(nSel)));
      ctrl_t *pDlg = pCtrl->pParent;
      dword_668F90 = 1;
      gluCreate_computerSlots_update(reinterpret_cast<intptr_t>(pDlg), 5);
      return 1;
    }
    }
  }

  return lbox_ctrl_list(pCtrl, pEvent);
}

// ---------------------------------------------------------------------------
// oracle 0x455AA0 (614 B).  The map listbox's activate handler: picks the
// maps or replays root, restores the last directory, and wires the seven
// map-setting labels.
void gluCreate_listbox_ctrl_activate(intptr_t nArg0)
{
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  char szPath[0x104];

  // The store is written in both arms so the compare can be scheduled first.
  if (gbCreateReplay_g)
  {
    *reinterpret_cast<PFN_LBOX_ITEMDRAW *>(&pCtrl->_unmapped4E[0x52 - 0x4E]) =
        reinterpret_cast<PFN_LBOX_ITEMDRAW>(sub_455850);
    SStrCopy(sgMapBaseDir_g, "maps\\replays", 0x104);
    SStrCopy(sgMapCurrentDir_g, sgMapBaseDir_g, 0x104);
    sub_45C680(sgMapCurrentDir_g);
  }
  else
  {
    *reinterpret_cast<PFN_LBOX_ITEMDRAW *>(&pCtrl->_unmapped4E[0x52 - 0x4E]) =
        reinterpret_cast<PFN_LBOX_ITEMDRAW>(sub_455850);
    SStrCopy(sgMapBaseDir_g, "maps", 0x104);
    SStrCopy(sgMapCurrentDir_g, sgMapBaseDir_g, 0x104);
    sub_45C680(sgMapCurrentDir_g);
    if (!sgAlreadyOpenedOnce)
    {
      sgAlreadyOpenedOnce = 1;
      if (gbIsExpansion)
        strcat(sgMapCurrentDir_g, "\\BroodWar");
      SStrCopy(sgRestoreDirectory_g, sgMapCurrentDir_g, 0x104);
    }
    else
    {
      SStrCopy(sgMapCurrentDir_g, sgRestoreDirectory_g, 0x104);
      SStrCopy(sgRestoreSelectEntry_g, sgCurrentListEntryStr_g, 0x104);
    }
  }

  SFileGetUserDataPath(szPath, 0x104, 1);
  SStrPack(szPath, "maps\\replays", 0x104);
  CreateDirectoriesRecursive(szPath);
  sub_4560D0(reinterpret_cast<intptr_t>(sgMapCurrentDir_g));

  CreateSetControl(0, ctrl_find_ndx(pCtrl, 7), reinterpret_cast<void *>(sub_455D10),
                   reinterpret_cast<void *>(sub_455F30));
  CreateSetControl(1, ctrl_find_ndx(pCtrl, 8), reinterpret_cast<void *>(sub_455D10),
                   reinterpret_cast<void *>(sub_455F30));
  CreateSetControl(4, ctrl_find_ndx(pCtrl, 9), reinterpret_cast<void *>(sub_455D10),
                   reinterpret_cast<void *>(sub_455F30));
  CreateSetControl(5, ctrl_find_ndx(pCtrl, 0x0A), reinterpret_cast<void *>(sub_455D10),
                   reinterpret_cast<void *>(sub_455F30));
  CreateSetControl(6, ctrl_find_ndx(pCtrl, 0x24), reinterpret_cast<void *>(sub_455D10),
                   reinterpret_cast<void *>(sub_455F30));
  CreateSetControl(7, ctrl_find_ndx(pCtrl, 0x25), reinterpret_cast<void *>(sub_455D10),
                   reinterpret_cast<void *>(sub_455F30));
  CreateSetControl(8, ctrl_find_ndx(pCtrl, 0x0B), reinterpret_cast<void *>(sub_455D10),
                   reinterpret_cast<void *>(sub_455F30));
}

// ---------------------------------------------------------------------------
// oracle 0x455D10 (523 B).  Map-info label callback: prefixes the value with
// the label for its kind, and shows or hides the kinds that only apply to some
// game types.  A value too long for its buffer is dropped; any other kind is
// shown as-is.
void __stdcall sub_455D10(int nArg0, void *pArg1, intptr_t nArg2)
{
  ctrl_t *pCtrl = static_cast<ctrl_t *>(pArg1);
  const char *pszValue = reinterpret_cast<const char *>(nArg2);
  char *pszLabel;

  switch (nArg0)
  {
  case 5:
    pszLabel = glues_get_str(0x21);
    if (SStrLen(pszLabel) + SStrLen(pszValue) < 0x80)
    {
      psnprintf(byte_6690A8, 0x80, pszLabel, 0x12, pszValue);
      ctrl_set_text(pCtrl, byte_6690A8);
    }
    return;
  case 4:
    pszLabel = glues_get_str(0x1F);
    if (SStrLen(pszLabel) + SStrLen(pszValue) < 0x80)
    {
      psnprintf(byte_669128, 0x80, pszLabel, 0x12, pszValue);
      ctrl_set_text(pCtrl, byte_669128);
      ctrl_showhide(pCtrl, !IsSimpleTemplate(static_cast<BYTE>(sgCreateGameTypeId_g),
                                             static_cast<BYTE>(sgCreateGameTypeId_g >> 8),
                                             static_cast<WORD>(sgCreateGameSubtypeId_g)));
    }
    return;
  case 6:
    pszLabel = glues_get_str(0x24);
    if (SStrLen(pszLabel) + SStrLen(pszValue) < 0x80)
    {
      psnprintf(byte_6691A8, 0x80, pszLabel, 0x12, pszValue);
      ctrl_set_text(pCtrl, byte_6691A8);
      ctrl_showhide(pCtrl, IsSimpleTemplate(static_cast<BYTE>(sgCreateGameTypeId_g),
                                            static_cast<BYTE>(sgCreateGameTypeId_g >> 8),
                                            static_cast<WORD>(sgCreateGameSubtypeId_g)));
    }
    return;
  case 7:
    pszLabel = glues_get_str(0x26);
    if (SStrLen(pszLabel) + SStrLen(pszValue) < 0x80)
    {
      psnprintf(byte_669228, 0x80, pszLabel, 0x12, pszValue);
      ctrl_set_text(pCtrl, byte_669228);
      ctrl_showhide(pCtrl, IsSimpleTemplate(static_cast<BYTE>(sgCreateGameTypeId_g),
                                            static_cast<BYTE>(sgCreateGameTypeId_g >> 8),
                                            static_cast<WORD>(sgCreateGameSubtypeId_g)));
    }
    return;
  case 8:
    pszLabel = glues_get_str(0x22);
    if (SStrLen(pszLabel) + SStrLen(pszValue) < 0x80)
    {
      psnprintf(byte_6692A8, 0x80, pszLabel, 0x12, pszValue);
      ctrl_set_text(pCtrl, byte_6692A8);
    }
    return;
  default:
    pCtrl->pData = const_cast<char *>(pszValue);
    ctrl_set_update(pCtrl);
    return;
  }
}

// ---------------------------------------------------------------------------
//
// The first argument is read by nobody, and the `ret 0Ch` is why it exists: the oracle
// takes [ebp+0Ch] and [ebp+10h] into ECX and EDX, never touches [ebp+08h], and still
// retires twelve bytes. Dropping the parameter gives `ret 8h`.
void __stdcall sub_455F30(int nArg0, intptr_t nArg1, int nArg2)
{
  (void)nArg0;
  ctrl_showhide(reinterpret_cast<ctrl_t *>(nArg1), nArg2);
}

// ---------------------------------------------------------------------------
// oracle 0x455F50 (144 B).  Refills the map listbox from the current
// directory and reselects the restored entry.
void sub_455F50()
{
  lbox_set_recalc(reinterpret_cast<lbox_t *>(sgGluCreateMapListCtrl_g), 0);
  lbox_remove_all(reinterpret_cast<lbox_t *>(sgGluCreateMapListCtrl_g));

  const char *pszRestore = sgRestoreSelectEntry_g[0] == 0 ? NULL : sgRestoreSelectEntry_g;
  int bAtRoot = SStrCmpI(sgMapBaseDir_g, sgMapCurrentDir_g, 0x7FFFFFFF) == 0;
  int nSel = FillMapList(0x28, sgMapCurrentDir_g, bAtRoot, sub_455810, pszRestore);
  lbox_set_recalc(reinterpret_cast<lbox_t *>(sgGluCreateMapListCtrl_g), 1);

  if (nSel != 0xFF)
    sub_45B970(lbox_get_data(reinterpret_cast<lbox_t *>(sgGluCreateMapListCtrl_g),
                             static_cast<unsigned char>(nSel)));
  lbox_set_selection(reinterpret_cast<lbox_t *>(sgGluCreateMapListCtrl_g),
                     static_cast<unsigned char>(nSel));
}

// ---------------------------------------------------------------------------
// oracle 0x455FE0 (234 B).  Draws the directory icon vertically centred in
// the control, then the directory text clipped to the space right of it.
void draw_glucreate_directory_label(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  char szText[0x104];
  rect16_t rc;
  bmp_t bmp;
  short nX, nY, nWidth, nHeight;

  (void)nXOfs;
  (void)nYOfs;
  (void)prcClip;
  rc = pCtrl->rc;
  grp_to_bmpx(gpCreateIcons_g, 2, &bmp, &nX, &nY, &nWidth, &nHeight);
  rc.top += (rc.bottom - rc.top + 1) / 2 - bmp.nHeight / 2;
  draw_bmp_effect(&bmp, 0, 0, &rc, 0x2000);
  rc.left += bmp.nWidth + 2;

  if (pCtrl->pData != NULL)
  {
    dlg_set_font(pCtrl);
    uintptr_t cchText =
        string_width(static_cast<const char *>(pCtrl->pData), rc.right - rc.left) + 1;
    if (cchText > 0x104)
      cchText = 0x104;
    SStrCopy(szText, static_cast<const char *>(pCtrl->pData), cchText);
    gbFontJust = 0x11;
    void *pSaved = pCtrl->pData;
    pCtrl->pData = szText;
    draw_ctrl_text(pCtrl, bmp.nWidth + 2, 0, NULL);
    pCtrl->pData = pSaved;
  }
}

// ---------------------------------------------------------------------------
// oracle 0x4560D0 (65 B).
//
// `mov ecx, [0x668A58]` and `mov edx, 6h` are hoisted above the branch -- they
// are common to both arms -- while the third argument and the call itself are
// duplicated into each arm.  That is an if/else, not a ternary: a ternary over
// the pointer would fold the two calls into one.
void sub_4560D0(intptr_t nArg0)
{
  SStrCopy(byte_668FA0, reinterpret_cast<const char *>(nArg0), 0x104);

  // The const cast picks the `const char *` overload on purpose: binding to VC12's
  // other declaration, `char *strrchr(char *, int)`, emits a second COMDAT named
  // strrchr into this object.
  const char *pszLeaf =
      strrchr(const_cast<const char *>(byte_668FA0), '\\');

  if (pszLeaf != NULL)
    glue_set_text(gCreateDlg_g, 6, const_cast<char *>(pszLeaf + 1));
  else
    glue_set_text(gCreateDlg_g, 6, byte_668FA0);
}

// ---------------------------------------------------------------------------
// oracle 0x456120 (79 B).  Same *_ctrl_fcn shape as 0x4561D0.
BOOL gluCreate_selfSlotDropDown_ctrl_fcn(intptr_t nArg0, intptr_t nArg1)
{
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E)
  {
    switch (pEvent->dwId)
    {
    case 0:
      lbox_set_recalc(pBox, 0);
      lbox_add_item(reinterpret_cast<ctrl_t *>(nArg0), gLocalPlayerName, 0);
      lbox_set_recalc(pBox, 1);
      break;
    case 0x0A:
      reinterpret_cast<ctrl_t *>(nArg0)->dwFlags |= 0x20000;
      break;
    }
  }

  return lbox_ctrl_combo(pBox, pEvent);
}

// ---------------------------------------------------------------------------
// oracle 0x456170 (84 B).
void gluCreate_raceDropDown_ctrl_activate(intptr_t nArg0)
{
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);

  lbox_set_recalc(pBox, 0);

  for (int i = 0; i < 4; i++)
  {
    unsigned char nItem = static_cast<unsigned char>(
        lbox_add_item(reinterpret_cast<ctrl_t *>(nArg0),
                      error_get_str(stru_503DA4[i].nStrId), 0));
    if (nItem == 0xFF)
      break;

    lbox_set_data(pBox, nItem,
                  reinterpret_cast<void *>(
                      static_cast<uintptr_t>(stru_503DA4[i].nValue)));
  }

  lbox_set_recalc(pBox, 1);
  lbox_set_selection(pBox, 3);
}

// ---------------------------------------------------------------------------
// oracle 0x4561D0 (54 B).
//
// The dropdown *_ctrl_fcn shape: guard on the event tag, switch on the event id, then
// tail-jump lbox_ctrl_combo with the caller's own ECX and EDX. The 0x0A arm gets its own
// copy of the tail because nothing in it clobbers the two registers, while the case-0 arm
// makes a call and reloads them; both come out of one `return`.
BOOL gluCreate_raceDropDown_ctrl_fcn(intptr_t nArg0, intptr_t nArg1)
{
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E)
  {
    switch (pEvent->dwId)
    {
    case 0:
      gluCreate_raceDropDown_ctrl_activate(nArg0);
      break;
    case 0x0A:
      reinterpret_cast<ctrl_t *>(nArg0)->dwFlags |= 0x20000;
      break;
    }
  }

  return lbox_ctrl_combo(pBox, pEvent);
}

// ---------------------------------------------------------------------------
// oracle 0x456210 (122 B).
//
// `cmovz ebx, eax` remembers the item index of the entry whose nValue is 1 and
// hands it to lbox_set_selection; the `movzx ebx, bl` right before it is the
// running value being widened for the move.
void gluCreate_computerSlotDropDown_ctrl_activate(intptr_t nArg0)
{
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);

  lbox_set_recalc(pBox, 0);

  // `xor bl, bl` sits after the call in the oracle, so the initialisation is its own
  // statement here rather than part of the declaration above.
  unsigned char nSelect = 0;

  for (int i = 0; i < 2; i++)
  {
    unsigned char nItem = static_cast<unsigned char>(
        lbox_add_item(reinterpret_cast<ctrl_t *>(nArg0),
                      glues_get_str(stru_503D9C[i].nStrId), 0));
    if (nItem == 0xFF)
      break;

    lbox_set_data(pBox, nItem,
                  reinterpret_cast<void *>(
                      static_cast<uintptr_t>(stru_503D9C[i].nValue)));

    if (stru_503D9C[i].nValue == 1)
      nSelect = nItem;
  }

  lbox_set_recalc(pBox, 1);
  lbox_set_selection(pBox, nSelect);
}

// ---------------------------------------------------------------------------
// oracle 0x456290 (75 B). Three cases, so MSVC lowers the dispatch as a subtract chain
// instead of the two-case compare chain the race and self-slot handlers get.
BOOL gluCreate_computerSlotDropDown_ctrl_fcn(intptr_t nArg0, intptr_t nArg1)
{
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E)
  {
    switch (pEvent->dwId)
    {
    case 0:
      gluCreate_computerSlotDropDown_ctrl_activate(nArg0);
      break;
    case 0x0A:
      reinterpret_cast<ctrl_t *>(nArg0)->dwFlags |= 0x20000;
      break;
    case 0x0B:
      gluCreate_computerSlotDropDown_ctrl_select(nArg0, nArg1);
      return 1;
    }
  }

  return lbox_ctrl_combo(pBox, pEvent);
}

// ---------------------------------------------------------------------------
// oracle 0x4562E0 (99 B).
//
// The first lbox_get_selection result is dead: the oracle calls it, zeroes EDX for
// ctrl_set_gray and then calls it again. It stays as written, since MSVC may not elide an
// external call.
void gluCreate_computerSlotDropDown_ctrl_select(intptr_t nArg0, intptr_t nArg1)
{
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);

  lbox_ctrl_list(pCtrl, reinterpret_cast<dlgevt_t *>(nArg1));

  // A byte load, so the source names the low byte: `(dwFlags >> 3) & 1` over the whole
  // dword loads it as a dword and is one byte longer.
  int bShow = (static_cast<BYTE>(pCtrl->dwFlags) >> 3) & 1;

  lbox_get_selection(pBox);
  ctrl_set_gray(pCtrl, 0);

  unsigned char nSel = lbox_get_selection(pBox);
  if (nSel != 0xFF)
  {
    // The call is hoisted out of the `&&`: left inside, the short circuit puts the
    // test in front of it, where the oracle calls lbox_get_data unconditionally.
    void *pData = lbox_get_data(pBox, nSel);
    bShow = (bShow != 0 && pData != NULL);
  }

  ctrl_showhide(ctrl_find_ndx(pCtrl->pParent,
                              static_cast<short>(pCtrl->nIndex + 8)),
                bShow);
}

// ---------------------------------------------------------------------------
// oracle 0x456350 (199 B).
//
// The second half from `cmp al, -1h` on is the same block
// gluCreate_computerSlotDropDown_ctrl_select ends with.
void gluCreate_computerSlotDropDown_update(intptr_t nArg0, int nArg1)
{
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);

  int bShow = (nArg1 != 0 && (pCtrl->dwFlags & 8) != 0);

  unsigned char nSel = lbox_get_selection(pBox);

  if (nArg1 == 0 && nSel != 0xFF &&
      lbox_get_data(pBox, nSel) != reinterpret_cast<void *>(1))
  {
    unsigned char nCount = lbox_get_count(pBox);
    unsigned char i;

    for (i = 0; i < nCount; i++)
    {
      if (lbox_get_data(pBox, i) == reinterpret_cast<void *>(1))
        break;
    }

    if (i < nCount)
      lbox_set_selection(pBox, i);
  }

  ctrl_set_gray(pCtrl, nArg1 == 0);

  unsigned char nSel2 = lbox_get_selection(pBox);
  if (nSel2 != 0xFF)
  {
    void *pData2 = lbox_get_data(pBox, nSel2);
    bShow = (bShow != 0 && pData2 != NULL);
  }

  ctrl_showhide(ctrl_find_ndx(pCtrl->pParent,
                              static_cast<short>(pCtrl->nIndex + 8)),
                bShow);
}

// ---------------------------------------------------------------------------
// oracle 0x456420 (420 B).  Shows or hides the computer-slot rows for the
// chosen map and game type; re-arms itself as a timer while the map is still
// unresolved.  Single player only.  The human count is a one-field struct so
// it keeps the oracle's [ebp-1] stack home instead of BH, and the shown count
// is assigned then overridden, which gives the cmove.
void gluCreate_computerSlots_update(intptr_t nArg0, int nArg1)
{
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  BYTE mapInfo[0xB4];

  if (gbMultiPlayerMode)
    return;

  timer_remove_node(pDlg, nArg1);
  BYTE nPlayers = 0;
  int bMelee = 0;
  struct { BYTE n; } hum;
  hum.n = nPlayers;
  unsigned char nSel = lbox_get_selection(reinterpret_cast<lbox_t *>(sgGluCreateMapListCtrl_g));
  if (nSel != 0xFF &&
      CreateMapInfo(lbox_get_data(reinterpret_cast<lbox_t *>(sgGluCreateMapListCtrl_g), nSel),
                    mapInfo))
  {
    if (mapInfo[4] & 0x80)
    {
      hum.n = nPlayers;
    }
    else
    {
      hum.n = mapInfo[8];
      nPlayers = mapInfo[0x10];
    }
  }

  if (dword_668F90 != 0 && nPlayers == 0xFF)
  {
    timer_create(pDlg, static_cast<dlg_word_t>(nArg1), 0x64,
                 reinterpret_cast<PFN_TIMER_FN *>(gluCreate_computerSlots_update));
    return;
  }

  ctrl_t *pSlot = ctrl_find_ndx(pDlg, 0x14);
  ctrl_t *pRace = ctrl_find_ndx(pDlg, 0x1C);
  if (pSlot == NULL || pRace == NULL)
    return;

  unsigned char nType = lbox_get_selection(reinterpret_cast<lbox_t *>(sgCreateGametypeCtrl_g));
  if (nPlayers != 0 && nType != 0xFF)
  {
    uintptr_t nData = reinterpret_cast<uintptr_t>(
        lbox_get_data(reinterpret_cast<lbox_t *>(sgCreateGametypeCtrl_g), nType));
    bMelee = IsMeleeTemplate(static_cast<BYTE>(nData), static_cast<BYTE>(nData >> 8), 1);
    ctrl_show(pSlot);

    int bShow;
    if (dword_668F90 != 0)
      bShow = bMelee || IsLocalSeatRaceSelectable();
    else
      bShow = bMelee || is_race_selectable(static_cast<BYTE>(g_LocalNationID));
    ctrl_showhide(pRace, bShow);

    BYTE nShown = nPlayers - 1;
    if (!bMelee) nShown = hum.n;
    pSlot = pSlot->pNext;
    for (; nShown != 0; nShown--)
    {
      if (pSlot == NULL || pSlot->nIndex >= 0x1C)
        break;
      ctrl_show(pSlot);
      gluCreate_computerSlotDropDown_update(reinterpret_cast<intptr_t>(pSlot), bMelee);
      pSlot = pSlot->pNext;
    }
  }

  for (; pSlot != NULL && pSlot->nIndex < 0x1C; pSlot = pSlot->pNext)
  {
    ctrl_hide(pSlot);
    gluCreate_computerSlotDropDown_update(reinterpret_cast<intptr_t>(pSlot), bMelee);
  }
}

// ---------------------------------------------------------------------------
// oracle 0x4565D0 (141 B). The scroll variant of the same shape, and the only one dense
// enough for a real jump table: a 12-byte case map at 0x456674 into the five-entry
// address table at 0x456660 -- cases 0, 1, 0x0A, 0x0B and a shared default. Both tables
// are COMDAT-tail data rather than globals.
BOOL gluCreate_speedScroll_ctrl_fcn(intptr_t nArg0, intptr_t nArg1)
{
  ctrl_t *pScroll = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E)
  {
    switch (pEvent->dwId)
    {
    case 0x0A:
      pScroll->dwFlags |= 0x2000000;
      break;
    case 0:
      scroll_set_range(pScroll, 0, 6);
      scroll_set_page_size(pScroll, 1, 1);
      ctrl_set_val(pScroll, gluCreate_get_config_game_speed());
      gluCreate_speedScroll_updateText(nArg0);
      break;
    case 0x0B:
      scroll_ctrl_scroll(pScroll, pEvent);
      gluCreate_speedScroll_updateText(nArg0);
      return 1;
    case 1:
      gluCreate_speedScroll_ctrl_deactivate(nArg0);
      break;
    }
  }

  return scroll_ctrl_scroll(pScroll, pEvent);
}

// ---------------------------------------------------------------------------
// oracle 0x456680 (158 B).
void gluCreate_speedScroll_updateText(intptr_t nArg0)
{
  ctrl_t *pScroll = reinterpret_cast<ctrl_t *>(nArg0);
  int nPos = scroll_get_pos(pScroll);

  if (nPos >= 0 && nPos < word_503D98)
  {
    sgbCreateGameSpeed_g = static_cast<BYTE>(nPos);

    ctrl_t *pLabel = ctrl_find_ndx(pScroll, 0x0F);
    char *pszSpeed = glues_get_str(sgSpeedStrMap_g[nPos].nStrId);
    char *pszLabel = glues_get_str(0x5D);

    if (SStrLen(pszLabel) + SStrLen(pszSpeed) + 1 < 0x400)
    {
      psnprintf(game_speed_label_buf, 0x400, "%s%c%s", pszLabel, 0x12, pszSpeed);
      ctrl_set_text(pLabel, game_speed_label_buf);
    }
  }
}

// ---------------------------------------------------------------------------
// oracle 0x456720 (92 B).  Adds one sub-type entry; for category 0x0F the
// entry must fit under the current map's player count.
void __stdcall gluCreate_subTypeDropDown_add_item(const char *pszName, const char *pszParam,
                                                  const TGAMEPARAMS *pParams,
                                                  const TTEMPLATEMETA *pMeta)
{
  char szName[0x10];

  (void)pszName;
  (void)pszParam;
  (void)pMeta;
  if (pParams->dwSubValue == 0)
    return;
  int nMax = dword_668A54;
  if (pParams->bCategory == 0x0F &&
      (nMax < 2 || pParams->dwSubValue >= static_cast<DWORD>(nMax)))
    return;

  gametype_get_subtype_name(szName, 0x10, pParams, nMax);
  unsigned char nItem = static_cast<unsigned char>(
      lbox_add_item(sgCreateSubtypeCtrl_g, szName, 0x10));
  lbox_set_data(reinterpret_cast<lbox_t *>(sgCreateSubtypeCtrl_g), nItem,
                reinterpret_cast<void *>(static_cast<uintptr_t>(pParams->wType)));
}

// ---------------------------------------------------------------------------
// oracle 0x456780 (165 B).  The sub-type dropdown's event handler.
BOOL gluCreate_subTypeDropDown_ctrl_fcn(intptr_t nArg0, intptr_t nArg1)
{
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E)
  {
    switch (pEvent->dwId)
    {
    case 1:
    {
      int nSel = lbox_get_selection(pBox);
      if (nSel != 0xFF)
        SRegSaveString("Starcraft", "Game Subtype", 0,
                       reinterpret_cast<const char *>(
                           lbox_get_item(pBox, static_cast<unsigned char>(nSel))));
      break;
    }
    case 0x0A:
      reinterpret_cast<ctrl_t *>(nArg0)->dwFlags |= 0x20000;
      ctrl_hide(reinterpret_cast<ctrl_t *>(nArg0));
      sgCreateGameSubtypeId_g = 1;
      break;
    case 0x0B:
      lbox_ctrl_combo(pBox, pEvent);
      if (lbox_get_count(pBox) != 0)
      {
        unsigned char nSel = lbox_get_selection(pBox);
        if (nSel != 0xFF)
          sgCreateGameSubtypeId_g = (DWORD)reinterpret_cast<DWORD_PTR>(lbox_get_data(pBox, nSel));
      }
      return 1;
    }
  }

  return lbox_ctrl_combo(pBox, pEvent);
}

// ---------------------------------------------------------------------------
// oracle 0x456830 (221 B).  Refills the sub-type dropdown for the chosen game
// type and restores the saved sub-type; hidden when the type has none.
void gluCreate_subTypeDropDown_init_items(intptr_t nArg0)
{
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);
  char szSubtype[0x20];

  lbox_set_recalc(pBox, 0);
  lbox_remove_all(pBox);
  IterateLeagueTemplates(static_cast<BYTE>(sgCreateGameTypeId_g),
                         static_cast<BYTE>(sgCreateGameTypeId_g >> 8),
                         gluCreate_subTypeDropDown_add_item);
  lbox_set_recalc(pBox, 1);

  if (lbox_get_count(pBox) != 0)
  {
    if (!SRegLoadString("Starcraft", "Game Subtype", 0, szSubtype, 0x20) ||
        lbox_set_match(pBox, szSubtype) == 0xFF)
      lbox_set_selection(pBox, 0);
  }

  int nCount = lbox_get_count(pBox);
  if (nCount != 0)
  {
    if (static_cast<BYTE>(sgCreateGameTypeId_g) == 0x0F)
      lbox_set_selection(pBox, static_cast<unsigned char>(nCount / 2));
    ctrl_show(reinterpret_cast<ctrl_t *>(nArg0));

    if (lbox_get_count(pBox) != 0)
    {
      unsigned char nSel = lbox_get_selection(pBox);
      if (nSel != 0xFF)
        sgCreateGameSubtypeId_g = (DWORD)reinterpret_cast<DWORD_PTR>(lbox_get_data(pBox, nSel));
    }
    return;
  }

  ctrl_hide(reinterpret_cast<ctrl_t *>(nArg0));
  sgCreateGameSubtypeId_g = 1;
}

// ---------------------------------------------------------------------------
// oracle 0x456910 (126 B).  Fills the game-type dropdown; with no saved type
// in single player it selects the last melee entry.
void gluCreate_gameTypeDropDown_add_gametypes(intptr_t nArg0)
{
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);

  IterateMainTemplates(sub_456990);
  int bRestored = gluCreate_get_game_type_setting(nArg0);
  unsigned char i = lbox_get_count(pBox);
  if (i == 0)
  {
    ctrl_gray(reinterpret_cast<ctrl_t *>(nArg0));
    return;
  }

  if (!gbMultiPlayerMode && !bRestored)
  {
    do
    {
      i--;
      uintptr_t nData = reinterpret_cast<uintptr_t>(lbox_get_data(pBox, i));
      if (IsMeleeTemplate(static_cast<BYTE>(nData), static_cast<BYTE>(nData >> 8), 1))
      {
        lbox_set_selection(pBox, i);
        break;
      }
    } while (i != 0);
  }

  gluCreate_gameTypeDropDown_ctrl_select(nArg0);
}

// ---------------------------------------------------------------------------
// oracle 0x456990 (58 B).  Adds one main template to the game-type dropdown,
// keyed by its category byte.
void __stdcall sub_456990(const char *pszName, const char *pszParam,
                          const TGAMEPARAMS *pParams, const TTEMPLATEMETA *pMeta)
{
  (void)pszParam;
  (void)pMeta;
  if (gametype_template_allowed(reinterpret_cast<const BYTE *>(pParams), 0))
  {
    unsigned char nItem = static_cast<unsigned char>(
        lbox_add_item(sgCreateGametypeCtrl_g, pszName, 0));
    lbox_set_data(reinterpret_cast<lbox_t *>(sgCreateGametypeCtrl_g), nItem,
                  reinterpret_cast<void *>(static_cast<uintptr_t>(pParams->bCategory)));
  }
}

// ---------------------------------------------------------------------------
// oracle 0x4569D0 (103 B).  Four cases with 0x405 far outside the others, so
// the chain splits on `cmp eax, 0Bh / jnbe` first and tests 0x405 on the far
// side.
BOOL gluCreate_gameTypeDropDown_ctrl_fcn(intptr_t nArg0, intptr_t nArg1)
{
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E)
  {
    switch (pEvent->dwId)
    {
    case 1:
      gluCreate_gameTypeDropDown_ctrl_deactivate(nArg0);
      break;
    case 0x0A:
      reinterpret_cast<ctrl_t *>(nArg0)->dwFlags |= 0x20000;
      break;
    case 0x0B:
      lbox_ctrl_combo(pBox, pEvent);
      gluCreate_gameTypeDropDown_ctrl_select(nArg0);
      return 1;
    case 0x405:
      gluCreate_gameTypeDropDown_add_gametypes(nArg0);
      break;
    }
  }

  return lbox_ctrl_combo(pBox, pEvent);
}

// ---------------------------------------------------------------------------
// oracle 0x456A40 (263 B).  Adopts the selected game type: its label, its
// sub-types, and its template record in gGameHeader.
void gluCreate_gameTypeDropDown_ctrl_select(intptr_t nArg0)
{
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);

  if (lbox_get_count(pBox) == 0)
    return;
  unsigned char nSel = lbox_get_selection(pBox);
  if (nSel == 0xFF)
    return;

  DWORD_PTR nType = reinterpret_cast<DWORD_PTR>(lbox_get_data(pBox, nSel));
  sgCreateGameTypeId_g = (DWORD)nType;
  SStrCopy(game_subtype_label_buf_g,
           gametype_find_param_name(static_cast<BYTE>(nType), static_cast<BYTE>(nType >> 8)),
           0x20);
  if (game_subtype_label_buf_g[0])
    SStrPack(game_subtype_label_buf_g, ":", 0x20);
  ctrl_set_text(ctrl_find_ndx(pCtrl, 0x0E), game_subtype_label_buf_g);
  gluCreate_subTypeDropDown_init_items(reinterpret_cast<intptr_t>(sgCreateSubtypeCtrl_g));

  *reinterpret_cast<TGAMEPARAMS *>(&gGameHeader.bGameType) =
      *gametype_find_template(static_cast<BYTE>(sgCreateGameTypeId_g),
                              static_cast<BYTE>(sgCreateGameTypeId_g >> 8),
                              static_cast<WORD>(sgCreateGameSubtypeId_g));
  if (!gbMultiPlayerMode)
  {
    ctrl_t *pDlg = pCtrl->pParent;
    dword_668F90 = 1;
    gluCreate_computerSlots_update(reinterpret_cast<intptr_t>(pDlg), 5);
  }

  lbox_t *pMapList = reinterpret_cast<lbox_t *>(ctrl_find_ndx(pCtrl->pParent, 5));
  int nMap = lbox_get_selection(pMapList);
  if (nMap != 0xFF)
    sub_45B970(lbox_get_data(pMapList, static_cast<unsigned char>(nMap)));
}
