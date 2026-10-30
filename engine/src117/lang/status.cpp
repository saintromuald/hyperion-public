// status.cpp -- the status console shell: load the console art, own the
// selection, drive the per-frame update. Bodies are in oracle VA order.
//
// /Ob2 can splice a callee defined in this file into a caller here, where 1.17,
// built without WPO, keeps every cross-TU call; noinline on the declaration is
// the lever when such a callee has to live here.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>
#include <string.h>     // memcpy inlines to three movdqu pairs, memset calls

#include "../h/platform.h"
#include "../h/status.h"

#include "../h/bmp.h"           // bmp_t, bmpsrc_t, bmp_copy_rect
#include "../h/CUnit.h"         // CUnit
#include "../h/cursor.h"        // cursor_set_console_offset
#include "../h/dialog.h"        // ctrl_t
#include "../h/glues.h"         // mem_free, alloc_load_bmp, the str_ pair
#include "../h/gluRdy.h"        // bmp_create
#include "../h/minimap.h"       // ctxhelp_hide, status_get_console_art,
                                // minimap_init, minimap_deactivate
#include "../h/order.h"         // status_get_str
#include "../h/CUnitTSpells.h"  // status_set_update
#include "../h/snd.h"           // load_game_data
#include "../h/statcmd.h"       // the statcmd family, sub_4A18D0
#include "../h/statfluf.h"     // statfluf_init, statfluf_deactivate
#include "../h/statdata.h"      // request_statdata_update
#include "../h/statwire.h"      // statwire_init, statwire_deactivate
#include "../h/textbox.h"       // textbox_init, textbox_free
#include "../h/Storm.h"         // SMemFree
#include "../h/victory.h"       // psnprintf
#include "../gen/globals117.h"
#include "../port/render/port_console.h"  // Hyperion: PortConsoleFreed, PortConsoleArtX, PortConsoleArtLoaded, PortConsoleModArt, PortConsoleHudBlank
#include "../port/render/port_view.h"  // Hyperion: g_portView
#include "../port/data/port_moddata.h"  // Hyperion: ModString
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX
#include "../port/hud/port_hudstate.h"  // Hyperion: the HUD's data layer

// This TU's functions, declared here as no status.h exists; the other three
// are declared in minimap.h (status_get_console_art), order.h (status_get_str)
// and CUnitTSpells.h (status_set_update).

// 0x4A7BC0 (125 B). Tears the console down. Nullary; free_game, the one
// caller, ignores EAX.
void status_free();

// 0x4A7CD0 (156 B). Re-reads the selection and picks the unit the console
// shows. Nullary; the tail jump to sub_4A18D0 makes it void.
void status_get_new_selection();

// 0x4A7D80 (146 B). Bring every console subsystem up. `ret`, nullary; tail jmp
// to request_statdata_update.
void status_init();

// 0x4A7E20 (140 B). Load the race's console bitmap and build its cursor mask.
// `ret`, nullary; EAX at the ret is create_mask's leftover, nobody reads it.
void status_load_console();

// 0x4A7EB0 (31 B). Load rez\stat_txt.tbl. `ret`, nullary; EAX at the ret is
// str_load_table's and no caller consumes it.
void status_load_strings();

// 0x4A7EE0 (36 B). One frame of console upkeep. `ret`, nullary; tail jmp to
// statdata_update.
void status_update();

// External callees declared here and not in a header, each with an explicit
// __fastcall.

// The other console subsystems' constructors and destructors, all nullary and
// void.
void __fastcall hp_float_init();
void __fastcall statF10_init();
void __fastcall statf10_deactivate();
void __fastcall statlb_deactivate();
void __fastcall statlb_init();
void __fastcall statport_deactivate();
void __fastcall statport_init();
void __fastcall statport_update();
void __fastcall statres_deactivate();
void __fastcall statres_init();
void __fastcall request_statport_update();

// vid_erase_mask -- oracle 0x4CB460. One register argument, the mask handle
// create_mask returned; `ret` with a tail jmp into vid_mask_initialize.
void __fastcall vid_erase_mask(intptr_t nMask);

// create_mask -- oracle 0x4CB2B0, `ret 4h`. ECX is the bitmap; one caller uses
// the full EAX, which is the handle vid_erase_mask takes back.
intptr_t __fastcall create_mask(bmp_t *pBmp, int nArg1, int nArg2);

// sub_49C140 -- oracle 0x49C140. Both arguments are CUnit *: the callee reads
// ECX and EDX at the same four offsets inside CUnit, and returns 1 when ECX is
// null. Callers use the full EAX.
int __fastcall sub_49C140(CUnit *pCurrent, CUnit *pCandidate);

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it. The
// names are the naming sheet's. gpStatUnit and gpStatUnits come from
// globals117.h; this TU writes them.

// The loaded stat_txt.tbl. Reached as a void * by mem_free and str_get_string.
#ifdef OC_DLL_MODE
#define sgpStatStrs (*reinterpret_cast<void **>(OC_VA(0x6AB4F8u)))
#else
extern void *sgpStatStrs;
#endif

// The dirty flag status_update drains. BYTE: `cmp byte ptr [0x6AB4FC], 0h`.
#ifdef OC_DLL_MODE
#define sgbStatusChange (*reinterpret_cast<BYTE *>(OC_VA(0x6AB4FCu)))
#else
extern BYTE sgbStatusChange;
#endif

// The console art's cursor mask. create_mask's full 32-bit return, handed back
// to vid_erase_mask; four bytes, because sgConsoleArt begins at 0x6AB504.
#ifdef OC_DLL_MODE
#define sgConsoleArtMask_g (*reinterpret_cast<intptr_t *>(OC_VA(0x6AB500u)))
#else
extern intptr_t sgConsoleArtMask_g;
#endif

// The console bitmap itself, an object and not a pointer: alloc_load_bmp,
// cursor_set_console_offset, create_mask and bmp_copy_rect take its address,
// and status_init reads +0x04 of it as the pixel base. Eight bytes, the gap to
// sgbTWire_g at 0x6AB50C.
#ifdef OC_DLL_MODE
#define sgConsoleArt (*reinterpret_cast<bmp_t *>(OC_VA(0x6AB504u)))
#else
extern bmp_t sgConsoleArt;
#endif

// byte_6AB4C0, how many units the new selection holds (a BYTE), comes from
// globals117.h; a local spelling would redefine the macro. The naming sheet
// puts it in statres.cpp, though this TU writes it.

// The local player's race index. No sheet name; spelled as src117/lang/
// banker.cpp:96 already spells it, so the two cannot disagree.
#ifdef OC_DLL_MODE
#define byte_5264C2 (*reinterpret_cast<BYTE *>(OC_VA(0x5264C2u)))
#else
extern BYTE byte_5264C2;
#endif

// The per-race console-file letters, "ztp". gamedata.cpp owns it; this TU only
// indexes it, and the read is `movsx`, which is what makes the element signed.
#ifdef OC_DLL_MODE
#define gszRaceChars_g (reinterpret_cast<char *>(OC_VA(0x503358u)))
#else
extern char gszRaceChars_g[4];
#endif

// The bmp_t a ctrl_t carries at +0x0C, spelled as src117/lang/dlg_draw.cpp:123
// spells it. A macro rather than a static inline, because as a function MSVC
// inlines it and still emits an out-of-line COMDAT.
#define ctrl_bmp(p) (reinterpret_cast<bmp_t *>(&(p)->_unmapped0C[0]))

// The console dialog's art size, in the +0x36 per-type payload dialog.h leaves
// unmapped. Both are read `movzx`, so both are unsigned.
#define ctrl_art_width(p) (*reinterpret_cast<dlg_word_t *>(&(p)->_unmapped36[0]))
#define ctrl_art_height(p) (*reinterpret_cast<dlg_word_t *>(&(p)->_unmapped36[2]))

// The TU's own owner tag: the .rdata string at 0x4F0188 that four allocation
// sites push.
#define OC_STATUS_FILE "status.cpp"

// 0x4A7BC0 status_free (125 B). Tear the console down, in the reverse of
// nothing in particular: the order below is the oracle's. Line 236 is the
// literal it pushes.
//
// The selection array is cleared with a literal 0x30 and not sizeof, because
// the shipped form spells the global as a pointer and sizeof collapses to 4.
void status_free() {
  minimap_deactivate();
  textbox_free();
  statf10_deactivate();
  statwire_deactivate();
  statdata_deactivate();
  statres_deactivate();
  statlb_deactivate();
  statcmd_deactivate();
  statport_deactivate();
  statfluf_deactivate();
  ctxhelp_hide();

  mem_free(sgpStatStrs, OC_STATUS_FILE, 0xEC);
  sgpStatStrs = NULL;
  vid_erase_mask(sgConsoleArtMask_g);
  PortConsoleFreed();  // Hyperion: 1.17's fluff panels back (port_console.cpp)
  gpStatUnit = NULL;
  memset(StatUnits(), 0, SelectionMax() * sizeof(StatUnits()[0]));  // Hyperion: 0x30 in 1.17
}

// 0x4A7C40 status_get_console_art (142 B). Copy the part of the console bitmap
// this dialog covers into the dialog's own bitmap. Line 181 is the literal the
// oracle pushes.
// _bmp_current is saved and restored around the copy. The bitmap accessor is
// respelled at each use rather than cached: the oracle reaches the height off
// the control as `[esi+0Eh]` and only the width through the cached pointer.
void __fastcall status_get_console_art(ctrl_t *pDlg) {
  void *pSaved = _bmp_current;
  bmpsrc_t src;

  ctrl_bmp(pDlg)->nWidth = ctrl_art_width(pDlg);
  ctrl_bmp(pDlg)->nHeight = ctrl_art_height(pDlg);
  bmp_create(ctrl_bmp(pDlg), OC_STATUS_FILE, 0xB5);
  // Hyperion: the HUD's console has no art under its dialogs (port_console.cpp)
  if (PortConsoleHudBlank(ctrl_bmp(pDlg)))
    return;

  // Hyperion: the art is the console's, which starts nConsoleX from the
  // screen's left (port_view.cpp), or the screen-wide one at 0 (port_console.cpp).
  src.rc.left = pDlg->rc.left - PortConsoleArtX();
  src.rc.top = pDlg->rc.top;
  src.nWidth = ctrl_bmp(pDlg)->nWidth;
  src.nHeight = ctrl_bmp(pDlg)->nHeight;
  src.rc.right = src.rc.left + src.nWidth - 1;
  src.rc.bottom = src.rc.top + src.nHeight - 1;

  _bmp_current = ctrl_bmp(pDlg);
  bmp_copy_rect(0, 0, &sgConsoleArt, &src);
  _bmp_current = pSaved;
}

// 0x4A7CD0 status_get_new_selection (156 B). Takes the selection the input
// layer staged, counts it, and picks the one unit the console reports on; a
// one-unit selection moves into slot 0. The loop is indexed, not a pointer
// walk: MSVC turns `i < 12` into 1.17's signed `cmp esi, 6AB4F8h`, where a
// pointer bound costs seven bytes.
void status_get_new_selection() {
  int i;

  memcpy(StatUnits(), NewUnits(), SelectionMax() * sizeof(StatUnits()[0]));  // Hyperion: 0x30 in 1.17
  byte_6AB4C0 = 0;
  gpStatUnit = NULL;

  for (i = 0; i < SelectionMax(); i++) {
    if (StatUnits()[i] != NULL) {
      if (sub_49C140(gpStatUnit, StatUnits()[i]))
        gpStatUnit = StatUnits()[i];
      byte_6AB4C0++;
    }
  }

  if (byte_6AB4C0 == 1) {
    StatUnits()[0] = gpStatUnit;
    memset(&StatUnits()[1], 0, (SelectionMax() - 1) * sizeof(StatUnits()[0]));  // Hyperion: 0x2C in 1.17
  }
  SelectionSubgroupsSort();  // Hyperion: the subgroups of Hyperion's selection
  sub_4A18D0();
}

// 0x4A7D70 status_get_str (13 B). One string out of this console's table. A
// shape-insensitive leaf.
char *__fastcall status_get_str(WORD wStringId) {
  // Hyperion: a mod's strings, past stat_txt's
  if (const char *pszMod = ModString(wStringId))
    return const_cast<char *>(pszMod);
  return reinterpret_cast<char *>(str_get_string(sgpStatStrs, wStringId));
}

// 0x4A7D80 status_init (146 B). Bring every console subsystem up, then drop the
// loaded console bitmap and start from an empty selection. Line 217 is the
// literal the oracle pushes.
// The three update requests are spelled out rather than routed through
// status_set_update: that body is defined later in this file, so calling it
// would emit a call the oracle does not have.
void status_init() {
  // Hyperion: a new game for the HUD's data layer (port_hudstate.cpp), unless the
  // console is only built again for another design (port_console.cpp)
  if (!PortConsoleRebuilding())
    PortHudGameReset();
  status_load_console();
  hp_float_init();
  statfluf_init();
  statport_init();
  statlb_init();
  statres_init();
  statdata_init();
  statwire_init();
  statcmd_init();
  statF10_init();
  textbox_init();
  minimap_init();

  if (sgConsoleArt.pPixels != NULL) {
    SMemFree(sgConsoleArt.pPixels, OC_STATUS_FILE, 0xD9, 0);
    sgConsoleArt.pPixels = NULL;
  }

  memset(StatUnits(), 0, SelectionMax() * sizeof(StatUnits()[0]));  // Hyperion: 0x30 in 1.17
  byte_6AB4C0 = 0;
  gpStatUnit = NULL;

  request_statcmd_update();
  request_statport_update();
  request_statdata_update();
}

// 0x4A7E20 status_load_console (140 B). Load game\<race>console.pcx and build
// the cursor mask over it. Line 207 is the literal the oracle pushes.
//
// A replay uses 'n' directly rather than a fourth entry of gszRaceChars_g,
// which only holds "ztp".
void status_load_console() {
  char szFile[0x104];

  // Hyperion: Settings' Console skin, taken for this game's console (port_console.cpp);
  // PortConsoleRace is byte_5264C2 unless a race was chosen, here and in the console's parts
  PortConsoleSkinTake();
  psnprintf(szFile, 0x104, "game\\%c%s",
            gPlayback != 0 ? 'n' : gszRaceChars_g[PortConsoleRace()], "console.pcx");
  sgConsoleArt.pPixels = NULL;
  // Hyperion: a mod's own console image, when it gives one (port_console.cpp)
  if (!PortConsoleModArt(gPlayback != 0 ? 3 : PortConsoleRace(), &sgConsoleArt))
    alloc_load_bmp(szFile, &sgConsoleArt, 0, const_cast<char *>(OC_STATUS_FILE),
                   0xCF);
  cursor_set_console_offset(&sgConsoleArt);
  // Hyperion: the HD frame's console and its layout (port_console.cpp, R4f)
  PortConsoleArtLoaded(sgConsoleArt.pPixels, sgConsoleArt.nWidth, sgConsoleArt.nHeight,
                       gPlayback != 0 ? 3 : PortConsoleRace());
  sgConsoleArtMask_g = create_mask(&sgConsoleArt, g_portView.nConsoleX, 0);  // Hyperion: 0, 0
}

// 0x4A7EB0 status_load_strings (31 B). Load this console's string table. Line
// 230 is the literal the oracle pushes; load_game_data is passed by address as
// the per-entry loader, so nothing here looks like a call to it.
void status_load_strings() {
  sgpStatStrs = reinterpret_cast<void *>(
      str_load_table(reinterpret_cast<intptr_t>("rez\\stat_txt.tbl"),
                     reinterpret_cast<intptr_t>(load_game_data),
                     reinterpret_cast<intptr_t>(OC_STATUS_FILE), 0xE6));
}

// 0x4A7ED0 status_set_update (15 B). Ask the three panels that depend on the
// selection to redraw. A shape-insensitive leaf.
void __fastcall status_set_update() {
  request_statcmd_update();
  request_statport_update();
  request_statdata_update();
}

// 0x4A7EE0 status_update (36 B). One frame of console upkeep: re-pick the
// selection if it changed, then drive the three panels.
void status_update() {
  if (sgbStatusChange != 0) {
    status_get_new_selection();
    sgbStatusChange = 0;
  }
  statport_update();
  statcmd_update();
  statdata_update();
}
