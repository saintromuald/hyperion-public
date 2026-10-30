// gluPopup.cpp -- the popup dialogs the glue screens put over themselves: the message
// and edit popups, and the history popup. Bodies are in oracle VA order.
//
// TU membership cuts both ways and needs re-checking after a change here: a callee
// defined in this file can be spliced into a caller in this file by /Ob2, which a
// cross-TU call never is, and a body whose size is exactly caller plus callee needs its
// callee here. __declspec(noinline) on the declaration is the lever.
//
// One such pair outlives any one change: deactivate_popup_dlg's 26 bytes are spliced into
// arm 1 of both gluPopup_dlg_fcn and glupedit_dlg_fcn, so all three stay in this TU
// together and removing the callee breaks both callers.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>
#include <string.h>

#include "../h/gluPopup.h"
#include "../h/dialog.h"
#include "../h/dlg_ctrl.h"
#include "../h/dlg_dlg.h"
#include "../h/dlg_draw.h"
#include "../h/dlg_lbox.h"
#include "../h/glues.h"
#include "../h/menu_dlg.h"
#include "../h/save_dlg.h"
#include "../h/event_Win.h"   // ours: init_lang_imm
#include "../h/snd.h"         // ours: load_game_data (dlg_load's PFN_DLGLOADER)
#include "../h/Storm.h"       // ours: SStrCopy
#include "../gen/globals117.h"

// 0x6D3037 gbFontMaxHgt, the current font's tallest glyph; spelled as
// dlg_lbox.cpp spells it. Registered with no type, so gen does not emit it.
#ifdef OC_DLL_MODE
#ifndef gbFontMaxHgt
#define gbFontMaxHgt (*reinterpret_cast<dlg_byte_t *>(OC_VA(0x6D3037u)))
#endif
#else
extern dlg_byte_t gbFontMaxHgt;
#endif

// The 8-byte pending-position pair at 0x66B3F8. Tu-local, two dword members:
// gluPopup_dlg_init reads them separately (two dword loads into the dialog's
// +0x0C and +0x10) and clears them together with a single
// `movq qword ptr [0x66B3F8], xmm0`. Two independent `long`s give two dword
// stores of zero instead, so the 8-byte object is what the bytes say.
struct popup_pos_t {
  /*0x00*/ LONG nA;
  /*0x04*/ LONG nB;
};
static_assert(sizeof(popup_pos_t) == 8, "popup_pos_t: one movq clears the pair");
static_assert(offsetof(popup_pos_t, nA) == 0x00, "popup_pos_t::nA");
static_assert(offsetof(popup_pos_t, nB) == 0x04, "popup_pos_t::nB");

// hist_ent_t -- the 8-byte history table entry. gluHist_dlg_cmd's `lea eax,[ecx+eax*8]`
// gives the stride, and two readers pin two members: +0x00 is the string id
// gluHist_dlg_init hands to str_get_string, and +0x02 is a word compared unsigned against
// the dword at 0x66B0EC and also the loop's sentinel. +0x04..+0x07 are never touched
// here, so they stay unnamed, and sgpHistTable keeps its `char (*)[8]` spelling with both
// readers casting locally.
struct hist_ent_t {
  /*0x00*/ unsigned short wId;
  /*0x02*/ unsigned short wGroup;
  /*0x04*/ unsigned char _unmapped04[4];
};
static_assert(sizeof(hist_ent_t) == 8, "hist_ent_t: the lea pins the stride at 8");
static_assert(offsetof(hist_ent_t, wId) == 0x00, "hist_ent_t::wId");
static_assert(offsetof(hist_ent_t, wGroup) == 0x02, "hist_ent_t::wGroup");

// The globals this TU reaches, spelled both ways with one guard per name: a `#define`
// over OC_VA under OC_DLL_MODE, a plain `extern` otherwise. One guard per name and never
// one over the block, which would undeclare every name under it.

// 0x6CE6DC gnDlgReturnVal, a LONG. The inner guard keeps this spelling compatible with
// the generated one, which five TUs also declare locally.
#ifdef OC_DLL_MODE
  #ifndef gnDlgReturnVal
  #define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
  #endif
#else
extern LONG gnDlgReturnVal;
#endif

// 0x66B0D8 -- the live popup dialog. get_popup_dlg returns it whole;
// gluPopup_dlg_fcn and deactivate_popup_dlg clear it; deactivate_popup tests
// it and tail-jumps to dlg_deactivate, whose ECX is a ctrl_t *.
#ifdef OC_DLL_MODE
#define sgpPopupDlg (*reinterpret_cast<ctrl_t **>(OC_VA(0x66B0D8u)))
#else
extern ctrl_t *sgpPopupDlg;
#endif

// 0x66B0DC -- the popup's per-control event-function table, handed to
// glue_init_screen_dlg in EDX with its byte size in the first stack word.
// Read as a whole dword and never dereferenced here; the element type comes
// from replay.cpp's derivation of the callee, now seated in glues.h.
#ifdef OC_DLL_MODE
#define sgpPopupDlgTable (*reinterpret_cast<PFN_CTRL_EVT **>(OC_VA(0x66B0DCu)))
#else
extern PFN_CTRL_EVT *sgpPopupDlgTable;
#endif

// 0x66B0E0 -- the gluHist string table. do_hist_popup stores str_load_table's
// result here, hands it to mem_free at the end and clears it; gluHist_dlg_init
// loads it into ECX for str_get_string. Only ever moved as a whole dword and
// never dereferenced in this TU, so `void *` is the honest type.
#ifdef OC_DLL_MODE
#define sgpHistStrTable (*reinterpret_cast<void **>(OC_VA(0x66B0E0u)))
#else
extern void *sgpHistStrTable;
#endif

// 0x66B0E4 -- the selected history entry. gluHist_dlg_cmd stores
// `pBase + nItem * 8` here, so it is a pointer into the 8-byte-stride table
// based at 0x66B0E8.
#ifdef OC_DLL_MODE
#define sgpHistSelected (*reinterpret_cast<void **>(OC_VA(0x66B0E4u)))
#else
extern void *sgpHistSelected;
#endif

// 0x66B0E8 -- the base of that 8-byte-stride table. Read as a dword and used as the base
// of a `lea eax, [ecx+eax*8]`, which pins the stride at 8 and says nothing about the
// element's shape; `char (*)[8]` produces exactly that lea. hist_ent_t above names the
// two words two readers do pin.
#ifdef OC_DLL_MODE
#define sgpHistTable (*reinterpret_cast<char (**)[8]>(OC_VA(0x66B0E8u)))
#else
extern char (*sgpHistTable)[8];
#endif

// 0x66B0EC -- the history filter ceiling. do_hist_popup stores its second
// argument here as a whole dword; gluHist_dlg_init compares a zero-extended
// WORD against it with `jnbe`, i.e. Unsigned, which is what makes it a DWORD
// rather than a long.
#ifdef OC_DLL_MODE
#define sgnHistFilter (*reinterpret_cast<DWORD *>(OC_VA(0x66B0ECu)))
#else
extern DWORD sgnHistFilter;
#endif

// 0x66B0F0 -- the popup's pending modal result. deactivate_popup selects it
// over the literal 3 with a `cmovnz` into gnDlgReturnVal, so it is the same
// width as that registered `long` at 0x6CE6DC.
#ifdef OC_DLL_MODE
#define sgnPopupResult (*reinterpret_cast<LONG *>(OC_VA(0x66B0F0u)))
#else
extern LONG sgnPopupResult;
#endif

// 0x66B0F4 -- the BYTE SIZE of that table, the first stack word
// glue_init_screen_dlg is called with (pushed, not a register). A whole dword
// at its only site.
#ifdef OC_DLL_MODE
#define sgnPopupDlgSize (*reinterpret_cast<DWORD *>(OC_VA(0x66B0F4u)))
#else
extern DWORD sgnPopupDlgSize;
#endif

// 0x66B3F8 -- the 8-byte pending-position pair described at popup_pos_t above.
// Hyperion: it is the popup's background, a bmp_t, which each do_*_dlg has
// alloc_load_bmp fill and gluPopup_dlg_init hands to the dialog's bitmap at +0x0C.
// At 32 bits a bmp_t is the two dwords popup_pos_t reads; at 64 bits its pixel
// pointer is 8 bytes at +8, so the pair left the dialog without its background
// (the game-type chooser drew as a grey box) and the load wrote past the global
// (../docs/upstream-notes.md; 2026-09-30).
#ifdef OC_DLL_MODE
#define sgPopupPendingPos (*reinterpret_cast<bmp_t *>(OC_VA(0x66B3F8u)))
#else
extern bmp_t sgPopupPendingPos;
#endif

// 0x66B0F8 and 0x66B1F8 -- the popup's two message-text buffers, 0x100 apart.
// gluPopup_dlg_activate points a control's pData at each and tests the second
// one's first BYTE, so both are char arrays; the 0x100 stride is what bounds
// the first.
#ifdef OC_DLL_MODE
#define sgszPopupText (reinterpret_cast<char *>(OC_VA(0x66B0F8u)))
#define sgszPopupText2 (reinterpret_cast<char *>(OC_VA(0x66B1F8u)))
#else
extern char sgszPopupText[0x100] OC_UNALIGNED;
extern char sgszPopupText2[0x100] OC_UNALIGNED;
#endif

// 0x66B2F8 -- the popup-EDIT dialog's text buffer, 0x100 bytes (the next name
// is sgPopupPendingPos at 0x66B3F8). glupedit_dlg_activate hands it to
// edit_set_text and tests its first byte; glupedit_dlg_fcn and do_pedit_dlg
// SStrCopy into it with a 0xFF cap; sub_45FB00 clears its first byte.
#ifdef OC_DLL_MODE
#define sgszEditText (reinterpret_cast<char *>(OC_VA(0x66B2F8u)))
#else
extern char sgszEditText[0x100] OC_UNALIGNED;
#endif

// 0x66B400 -- do_pedit_dlg's fourth argument, parked for sub_45FB00, which
// NULL-checks it and then walks it BYTE BY BYTE to its NUL exactly as it walks
// the constant reject list at 0x508278. A `char *`.
#ifdef OC_DLL_MODE
#define sgpszEditReject (*reinterpret_cast<char **>(OC_VA(0x66B400u)))
#else
extern char *sgpszEditReject;
#endif

// 0x508278 -- the constant reject list, `*\/:><|&+^?-` (12 chars + NUL, and
// the next name begins at 0x508288). In .data, not .rdata, so it is a
// non-const initialised array rather than a string literal. Address-only: this
// TU only walks it, so it is declared and never defined here.
#ifdef OC_DLL_MODE
#define sgszEditRejectConst (reinterpret_cast<char *>(OC_VA(0x508278u)))
#else
extern char sgszEditRejectConst[13];
#endif

// The six per-dialog event-function tables in .data, each the value a do_*_dlg stores
// into sgpPopupDlgTable together with its byte size in sgnPopupDlgSize, so each table's
// element count is that size over 4 and the six spans tile 0x508288..0x5082F8 with no
// gap:
//   0x508288  0x1C -> 7  do_select_gamemode_dlg
//   0x5082A4  0x0C -> 3  do_pok_cancel_dlg
//   0x5082B0  0x08 -> 2  do_pok_dlg
//   0x5082B8  0x10 -> 4  do_pedit_dlg      (slot 0 is glupedit_edit_fcn @0x45FCC0)
//   0x5082C8  0x14 -> 5  do_pok_split_dlg
//   0x5082DC  0x1C -> 7  do_hist_popup
// They are reached by address only: every use is `sgpPopupDlgTable = <table>`, and
// defining them would name handler addresses this TU does not own.
#ifdef OC_DLL_MODE
#define sgGameModeDlgFcns (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x508288u)))
#define sgPokCancelDlgFcns (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x5082A4u)))
#define sgPokDlgFcns (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x5082B0u)))
#define sgPeditDlgFcns (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x5082B8u)))
#define sgPokSplitDlgFcns (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x5082C8u)))
#define sgHistDlgFcns (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x5082DCu)))
#else
extern PFN_CTRL_EVT sgGameModeDlgFcns[7] OC_UNALIGNED;
extern PFN_CTRL_EVT sgPokCancelDlgFcns[3];
extern PFN_CTRL_EVT sgPokDlgFcns[2] OC_UNALIGNED;
extern PFN_CTRL_EVT sgPeditDlgFcns[4] OC_UNALIGNED;
extern PFN_CTRL_EVT sgPokSplitDlgFcns[5] OC_UNALIGNED;
extern PFN_CTRL_EVT sgHistDlgFcns[7];
#endif

// 0x665F58 / 0x665F5C -- event_Win.cpp's pair in the sheet, both IDA
// placeholders, so the names here are `oc_` tu-local spellings exactly as
// replay.cpp:1793 spells the same two addresses. DWORD from
// `cmp dword ptr [0x665F58], 1h` and `mov dword ptr [0x665F5C], 1h`.
// Not ours to name.
#ifdef OC_DLL_MODE
#define oc_dwEventWin665F58 (*reinterpret_cast<int *>(OC_VA(0x665F58u)))
#else
extern int oc_dwEventWin665F58;
#endif

#ifdef OC_DLL_MODE
#define oc_dwEventWin665F5C (*reinterpret_cast<int *>(OC_VA(0x665F5Cu)))
#else
extern int oc_dwEventWin665F5C;
#endif

// The bodies, in oracle VA order.

// 0x45FA70, 140 B. The popup-edit dialog's activate.
//
// Four ctrl_find_ndx calls and not one null check: index 4 is looked up twice in a row
// and the second result dereferenced at once, the oracle trusting the template.
// Reproduced, not repaired.
//
// ctrl_t +0x24 is a word and is not in dialog.h: the oracle writes it as a word here and
// sub_45FB00 reads and clears the same slot as one. It goes through `_unmapped24` until
// the header owns it.
void glupedit_dlg_activate(ctrl_t *pDlg) {
  ctrl_t *pText = ctrl_find_ndx(pDlg, 2);
  pText->pData = sgszPopupText;
  ctrl_set_update(pText);

  ctrl_t *pEdit = ctrl_find_ndx(pDlg, 4);
  pEdit->dwFlags |= 0x20000;
  edit_set_text(pEdit, sgszEditText);
  if (sgszEditText[0] != 0)
    *reinterpret_cast<short *>(pEdit->_unmapped24) = 1;

  ctrl_t *pEdit2 = ctrl_find_ndx(pDlg, 4);
  BYTE bEmpty = static_cast<BYTE>(*static_cast<const char *>(pEdit2->pData) == 0);
  ctrl_t *pBtn = ctrl_find_ndx(pEdit2, 1);
  ctrl_set_gray(pBtn, bEmpty);

  ctrl_set_tabstop(pEdit);
  ctrl_set_update(pEdit);
}

// 0x45FB00, 249 B. The popup-edit dialog's key filter, called from
// glupedit_edit_fcn's tag>1 arm.
//
// Two identical reject scans: the first walks the constant list at 0x508278, the second
// the caller-supplied one at 0x66B400, null-checked with its first byte tested before the
// loop. Both hoist the key load out of the loop and compare 16-bit, so the key is read as
// a word and the list byte widened to one.
//
// ctrl_t +0x26 is a dword and is not in dialog.h either: the edit's capacity, read here
// against the inline strlen of pData and written by do_pedit_dlg. The compare is
// unsigned, which is what an inline `strlen` against a DWORD gives.
#define OC_EVT_KEY(e) (*reinterpret_cast<const unsigned short *>((e)->_unmapped08))

void sub_45FB00(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  const char *p;

  for (p = sgszEditRejectConst; *p != 0; ++p) {
    if (OC_EVT_KEY(pEvent) == static_cast<unsigned char>(*p))
      return;
  }

  p = sgpszEditReject;
  if (p != NULL) {
    for (; *p != 0; ++p) {
      if (OC_EVT_KEY(pEvent) == static_cast<unsigned char>(*p))
        return;
    }
  }

  if (*reinterpret_cast<short *>(pCtrl->_unmapped24) != 0) {
    edit_set_text(pCtrl, "");
    *reinterpret_cast<short *>(pCtrl->_unmapped24) = 0;
  }

  const char *pszText = static_cast<const char *>(pCtrl->pData);
  if (strlen(pszText) < *reinterpret_cast<DWORD *>(pCtrl->_unmapped24 + 2) - 1 ||
      OC_EVT_KEY(pEvent) == 8) {
    if (OC_EVT_KEY(pEvent) != 0x20 || *pszText != 0)
      edit_ctrl_edit(pCtrl, pEvent);
  } else if (oc_dwEventWin665F58 == 1) {
    oc_dwEventWin665F5C = 1;
  }

  ctrl_t *pEdit = ctrl_find_ndx(pCtrl, 4);
  BYTE bEmpty = static_cast<BYTE>(*static_cast<const char *>(pEdit->pData) == 0);
  ctrl_t *pBtn = ctrl_find_ndx(pEdit, 1);
  ctrl_set_gray(pBtn, bEmpty);
}

// 0x45FC00, 148 B. The popup-EDIT dialog's event handler.
//
// The two-level jump table is the compiler's, not the source's: an 11-byte selector array
// at 0x45FCA8 feeding a five-entry dword table at 0x45FC94, which is what MSVC builds for
// a sparse switch over 0..10 with four arms and a default.
//
// The arm order matters and is not ascending: the bodies are laid out 10, 0, 7, 1, and
// MSVC lays case bodies out in source order. Both handlers in this family share the same
// selector array and the same layout. Arm 1 inlines deactivate_popup_dlg from one TU
// away, needing a `mov ecx, esi` first only because ctrl_find_ndx has clobbered ECX.
int glupedit_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 10:
        gluPopup_dlg_init(pCtrl);
        break;
      case 0:
        glupedit_dlg_activate(pCtrl);
        break;
      case 7:
        title_position(pCtrl);
        break;
      case 1: {
        ctrl_t *pEdit = ctrl_find_ndx(pCtrl, 4);
        SStrCopy(sgszEditText, static_cast<const char *>(pEdit->pData), 0xFF);
        deactivate_popup_dlg(pCtrl);
        break;
      }
    }
  }
  return ctrl_dlg(pCtrl, pEvent);
}

// 0x45FCC0, 96 B. The popup-edit dialog's edit control handler.
//
// Three arms off one event tag, and the comparison order has to stay: the sign test
// first, then `<= 1` for the middle arm, then the test for 15. The sign test is dead --
// the load is a `movzx`, so the value cannot be negative -- and is reproduced rather than
// repaired.
//
// The empty-string flag feeding ctrl_set_gray is set narrow and widened after the
// intervening call, not twice. Which register it lands in is decided by the statement
// split: with the find nested as an argument MSVC widens the flag early into a register
// pCtrl has just freed, where lifting the find out leaves the flag narrow and live across
// the call, the one condition under which MSVC spends a callee-saved byte register.
BOOL glupedit_edit_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  int nTag = pEvent->wTag;
  if (nTag >= 0 && (nTag <= 1 || nTag == 0x0F)) {
    if (nTag > 1) {
      sub_45FB00(pCtrl, pEvent);
      return TRUE;
    }
    edit_ctrl_edit(pCtrl, pEvent);
    ctrl_t *pEdit = ctrl_find_ndx(pCtrl, 4);
    BYTE bEmpty = static_cast<BYTE>(*static_cast<const char *>(pEdit->pData) == 0);
    ctrl_t *pBtn = ctrl_find_ndx(pEdit, 1);
    ctrl_set_gray(pBtn, bEmpty);
    return TRUE;
  }
  return edit_ctrl_edit(pCtrl, pEvent);
}

// 0x45FD20, 37 B. Tear the popup down with a result.
//
// `mov edx, 3h / test eax, eax / cmovnz edx, eax` is the ternary
// `result ? result : 3` -- a conditional move, not a branch, which is what
// pins the ternary rather than an if/else.
void deactivate_popup() {
  ctrl_t *pDlg = sgpPopupDlg;
  if (pDlg) {
    gnDlgReturnVal = sgnPopupResult ? sgnPopupResult : 3;
    dlg_deactivate(pDlg);
  }
}

// 0x45FD50, 393 B. Run the popup-EDIT dialog modally and hand the edited text
// back through the caller's buffer.
//
// `ret 8h` -> ECX + EDX + two stack words. nArg0/nArg1 are the prompt and the
// initial text, nArg2 is the caller's buffer SIZE and nArg3 the extra reject
// list parked at 0x66B400. nArg1 is IN AND OUT: it is SStrCopy'd into
// sgszEditText on the way in and SStrCopy'd back out at the end.
//
// The tail is a fused walk and fill: an inline strlen that decrements the caller's size
// as it goes, then a signed guard over an inline memset, so the remainder of the caller's
// buffer is zeroed before the copy. The walk and the countdown sharing one loop is what
// makes a separate strlen wrong here.
BYTE do_pedit_dlg(intptr_t nArg0, intptr_t nArg1, int nArg2, intptr_t nArg3) {
  char szPath[0x104];

  deactivate_popup();
  SStrCopy(sgszPopupText, reinterpret_cast<const char *>(nArg0), 0xFF);
  SStrCopy(sgszEditText, reinterpret_cast<const char *>(nArg1), 0xFF);
  sgpszEditReject = reinterpret_cast<char *>(nArg3);
  sgpPopupDlgTable = sgPeditDlgFcns;
  sgnPopupDlgSize = 0x10;
  glues_get_pal_path(szPath, 0x104);
  strcat(szPath, "\\pEPopup.pcx");
  alloc_load_bmp(szPath, &sgPopupPendingPos,
                 reinterpret_cast<DWORD_PTR>(sgGamePalette_g), "gluPopup.cpp", 0x1AE);
  sgpPopupDlg = dlg_load("rez\\gluPEdit.bin",
                         reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                         reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                         "gluPopup.cpp", 0x1B0);
  ctrl_t *pEdit = ctrl_find_ndx(sgpPopupDlg, 4);
  *reinterpret_cast<LONG *>(pEdit->_unmapped24 + 2) = nArg2;
  init_lang_imm(1);
  int nResult = dlg_run_modal(sgpPopupDlg, 0, glupedit_dlg_fcn);
  init_lang_imm(0);
  if (nResult != 1)
    return 0;

  char *pszEnd = sgszEditText;
  int nLeft = nArg2;
  while (*pszEnd != 0) {
    ++pszEnd;
    --nLeft;
  }
  if (nLeft > 0) {
    int i;
    for (i = 0; i < nLeft; ++i)
      pszEnd[i] = 0;
  }
  SStrCopy(reinterpret_cast<char *>(nArg1), sgszEditText, nArg2);
  sgszEditText[0] = 0;
  return 1;
}

// 0x45FEE0, 6 B. The live popup dialog.
ctrl_t *get_popup_dlg() {
  return sgpPopupDlg;
}

// 0x45FEF0, 255 B. Run the campaign history popup over a caller-supplied
// 8-byte-stride table, returning the chosen entry.
//
// The pre-pass decides whether the dialog is worth showing: it counts entries whose
// wGroup is at most the filter and whose wId is non-zero, and if at most one qualifies it
// returns the table base without loading anything. The counting loop is rotated, reading
// the next iteration's sentinel as entry[1].wGroup before the stride is added, and the
// two nested jumps over the wId test are what make the predicate a short-circuiting `&&`.
intptr_t do_hist_popup(intptr_t nArg0, int nArg1) {
  const hist_ent_t *pTable = reinterpret_cast<const hist_ent_t *>(nArg0);
  DWORD dwFilter = static_cast<DWORD>(nArg1);
  DWORD dwCount = 0;

  for (const hist_ent_t *p = pTable; p->wGroup != 0; ++p) {
    if (p->wGroup <= dwFilter && p->wId != 0)
      ++dwCount;
  }
  if (dwCount <= 1)
    return nArg0;

  sgnHistFilter = dwFilter;
  sgpHistSelected = NULL;
  sgpHistTable = reinterpret_cast<char (*)[8]>(nArg0);
  sgpPopupDlgTable = sgHistDlgFcns;
  sgnPopupDlgSize = 0x1C;
  sgpHistStrTable = reinterpret_cast<void *>(
      str_load_table(reinterpret_cast<intptr_t>("rez\\gluHist.tbl"),
                     reinterpret_cast<intptr_t>(load_game_data),
                     reinterpret_cast<intptr_t>("gluPopup.cpp"), 0x259));
  alloc_load_bmp("glue\\campaign\\pHist.pcx", &sgPopupPendingPos,
                 reinterpret_cast<DWORD_PTR>(sgGamePalette_g), "gluPopup.cpp", 0x25A);
  sgpPopupDlg = dlg_load("rez\\gluHist.bin",
                         reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                         reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                         "gluPopup.cpp", 0x25C);
  dlg_run_modal(sgpPopupDlg, 0, gluHist_dlg_fcn);
  mem_free(sgpHistStrTable, "gluPopup.cpp", 0x261);
  sgpHistStrTable = NULL;
  return reinterpret_cast<intptr_t>(sgpHistSelected);
}

// 0x45FFF0, 268 B. The game-mode chooser popup.
//
// The only member of the do_* family that takes NO argument and whose message
// text comes from the string table (`glues_get_str(0xAA)`) rather than the
// caller, and the only one that returns dlg_run_modal's result untouched --
// there is no `mov al, 1h` and no `setz`, EAX simply survives the epilogue.
BYTE do_select_gamemode_dlg() {
  char szPath[0x104];

  deactivate_popup();
  sgpPopupDlgTable = sgGameModeDlgFcns;
  sgnPopupDlgSize = 0x1C;
  SStrCopy(sgszPopupText, glues_get_str(0xAA), 0xFF);
  sgszPopupText2[0] = 0;
  glues_get_pal_path(szPath, 0x104);
  strcat(szPath, "\\retail_ex.pcx");
  alloc_load_bmp(szPath, &sgPopupPendingPos,
                 reinterpret_cast<DWORD_PTR>(sgGamePalette_g), "gluPopup.cpp", 0x14C);
  sgpPopupDlg = dlg_load("rez\\gluGameMode.bin",
                         reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                         reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                         "gluPopup.cpp", 0x14E);
  return static_cast<BYTE>(dlg_run_modal(sgpPopupDlg, 0, gluPopup_dlg_fcn));
}

// 0x460100, 294 B. The two-line OK popup.
//
// It falls back to do_pok_dlg when the second line is absent, both a null pointer and an
// empty string landing on that call and returning its result untouched. Both message
// buffers are filled with MSVC's biased-pointer inline strcpy, so the spelling is a plain
// `strcpy` rather than SStrCopy.
BYTE do_pok_split_dlg(intptr_t nArg0, intptr_t nArg1) {
  char szPath[0x104];
  const char *pszLine2 = reinterpret_cast<const char *>(nArg1);

  deactivate_popup();
  if (pszLine2 == NULL || *pszLine2 == 0)
    return do_pok_dlg(nArg0);

  sgpPopupDlgTable = sgPokSplitDlgFcns;
  sgnPopupDlgSize = 0x14;
  strcpy(sgszPopupText, reinterpret_cast<const char *>(nArg0));
  strcpy(sgszPopupText2, pszLine2);
  glues_get_pal_path(szPath, 0x104);
  strcat(szPath, "\\pOPopup.pcx");
  alloc_load_bmp(szPath, &sgPopupPendingPos,
                 reinterpret_cast<DWORD_PTR>(sgGamePalette_g), "gluPopup.cpp", 0x1D9);
  sgpPopupDlg = dlg_load("rez\\gluPOkSplit.bin",
                         reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                         reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                         "gluPopup.cpp", 0x1DB);
  dlg_run_modal(sgpPopupDlg, 0, gluPopup_dlg_fcn);
  return 1;
}

// 0x460230, 255 B. The one-line OK popup -- the family's template.
//
// `mov byte ptr [0x66B1F8], al` reuses the AL the inline strcpy loop left at
// zero, which is what tells you the second buffer is cleared with a literal 0
// rather than copied. The return is a hard `mov al, 1h`.
BYTE do_pok_dlg(intptr_t nArg0) {
  char szPath[0x104];

  deactivate_popup();
  sgpPopupDlgTable = sgPokDlgFcns;
  sgnPopupDlgSize = 0x08;
  strcpy(sgszPopupText, reinterpret_cast<const char *>(nArg0));
  sgszPopupText2[0] = 0;
  glues_get_pal_path(szPath, 0x104);
  strcat(szPath, "\\pOPopup.pcx");
  alloc_load_bmp(szPath, &sgPopupPendingPos,
                 reinterpret_cast<DWORD_PTR>(sgGamePalette_g), "gluPopup.cpp", 0x18A);
  sgpPopupDlg = dlg_load("rez\\gluPOk.bin",
                         reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                         reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                         "gluPopup.cpp", 0x18C);
  dlg_run_modal(sgpPopupDlg, 0, gluPopup_dlg_fcn);
  return 1;
}

// 0x460330, 259 B. The OK/Cancel popup -- do_pok_dlg with a different table,
// a different bitmap and a result: `cmp eax, 1h / setz al`, so it reports
// whether the modal closed on OK rather than a constant 1.
BYTE do_pok_cancel_dlg(intptr_t nArg0) {
  char szPath[0x104];

  deactivate_popup();
  sgpPopupDlgTable = sgPokCancelDlgFcns;
  sgnPopupDlgSize = 0x0C;
  strcpy(sgszPopupText, reinterpret_cast<const char *>(nArg0));
  sgszPopupText2[0] = 0;
  glues_get_pal_path(szPath, 0x104);
  strcat(szPath, "\\pDPopup.pcx");
  alloc_load_bmp(szPath, &sgPopupPendingPos,
                 reinterpret_cast<DWORD_PTR>(sgGamePalette_g), "gluPopup.cpp", 0x16F);
  sgpPopupDlg = dlg_load("rez\\gluPOkCancel.bin",
                         reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                         reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                         "gluPopup.cpp", 0x171);
  return static_cast<BYTE>(dlg_run_modal(sgpPopupDlg, 0, gluPopup_dlg_fcn) == 1);
}

// 0x460440, 13 B. Install the popup dialog and its pending result together.
void set_popup_dlg(ctrl_t *pDlg, LONG nResult) {
  sgpPopupDlg = pDlg;
  sgnPopupResult = nResult;
}

// gluHist_dlg_init
// Fill the history dialog's listbox. The walk shares do_hist_popup's predicate (wGroup at
// most the filter, unsigned, and wId non-zero) but keeps a running index over every entry,
// qualifying or not, for lbox_set_data: the payload is the entry's position in the source
// table. The loop is a do/while whose guard reads `pTable[0].wGroup` and whose body and
// latch read `p->wGroup`; `p->wId` is read twice, in the test and the argument.
void gluHist_dlg_init(ctrl_t *pDlg) {
  lbox_t *pBox = reinterpret_cast<lbox_t *>(ctrl_find_ndx(pDlg, 6));
  reinterpret_cast<ctrl_t *>(pBox)->dwFlags |= 0x20000;
  lbox_set_recalc(pBox, 0);

  const hist_ent_t *pTable = reinterpret_cast<const hist_ent_t *>(sgpHistTable);
  const hist_ent_t *p = pTable;
  intptr_t nIndex = 0;
  if (pTable[0].wGroup != 0) {
    do {
      if (p->wGroup <= sgnHistFilter && p->wId != 0) {
        BYTE nItem = lbox_add_item(reinterpret_cast<ctrl_t *>(pBox),
                                  reinterpret_cast<const char *>(
                                      str_get_string(sgpHistStrTable, p->wId)),
                                  0);
        lbox_set_data(pBox, static_cast<unsigned char>(nItem),
                      reinterpret_cast<void *>(nIndex));
      }
      ++p;
      ++nIndex;
    } while (p->wGroup != 0);
  }

  lbox_set_recalc(pBox, 1);
  lbox_set_selection(pBox, 0);
}

// 0x4604E0, 62 B. The history dialog's item-chosen command.
//
// The guard is gnDlgReturnVal == 1, the modal having closed on OK. The listbox selection
// is a byte with -1 for nothing selected, and lbox_get_data's dword then indexes the
// table base at a stride of 8.
void gluHist_dlg_cmd(ctrl_t *pDlg) {
  if (gnDlgReturnVal == 1) {
    ctrl_t *pBox = ctrl_find_ndx(pDlg, 6);
    if (pBox) {
      unsigned char nSel = lbox_get_selection(reinterpret_cast<lbox_t *>(pBox));
      if (nSel != 0xFF) {
        DWORD_PTR dwItem = reinterpret_cast<DWORD_PTR>(
            lbox_get_data(reinterpret_cast<lbox_t *>(pBox), nSel));
        sgpHistSelected = sgpHistTable + dwItem;
      }
    }
  }
}

// 0x460520, 57 B. The history dialog's event handler, layered over gluPopup_dlg_fcn, and
// the shape the rest of the family follows: both arguments spill to callee-saved
// registers and are restored into ECX and EDX for the tail jump, which is what a handler
// that calls a helper and then forwards the same event looks like. The dispatch is
// chained subtracts, a switch's lowering, where an if-chain gives two compares.
int gluHist_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        gluHist_dlg_init(pCtrl);
        break;
      case 2:
        gluHist_dlg_cmd(pCtrl);
        break;
    }
  }
  return gluPopup_dlg_fcn(pCtrl, pEvent);
}

// 0x460560, 26 B. Close the popup and forget it.
//
// No prologue at all: ECX is forwarded to glues_dlg_deactivate untouched and
// the two literal stores follow the call, so nothing needs saving. This body
// is inlined into arm 1 of both *_dlg_fcn handlers in this TU.
void deactivate_popup_dlg(ctrl_t *pDlg) {
  glues_dlg_deactivate(pDlg);
  sgpPopupDlg = NULL;
  sgnPopupResult = 0;
}

// 0x460580, 64 B. Build the popup's screen dialog.
//
// The two-dword copy and the zeroing are one 8-byte object: MSVC emits an `xorps` up
// front, two dword load and store pairs for the copy, and one `movq` to clear.
//
// The clear is a `memset` and nothing else reaches it. Two field stores of 0 give 73
// bytes, a value-initialised temporary 87 or 109, an integer store through a cast lands
// on 64 bytes with a `movlpd` where the oracle has `movq`. Only `memset(&g, 0, sizeof g)`
// emits the oracle's pair, because MSVC expands an 8-byte memset inline as a float-domain
// zero plus an integer-domain 8-byte store, which no typed assignment produces. It is
// expanded rather than called, so there is no CRT reference here.
//
// The two destinations are ctrl_t +0x0C, four bytes with no derived shape, and +0x10
// (nResIndex). The dword goes through the byte array because nothing here says what
// those four bytes mean, only that the oracle stores a dword into them.
void gluPopup_dlg_init(ctrl_t *pDlg) {
  glue_init_screen_dlg(pDlg, sgpPopupDlgTable, sgnPopupDlgSize, 0);
  pDlg->dwFlags |= 0x40000;
  // Hyperion: the whole bmp_t (above); at 32 bits the same two dwords.
  *reinterpret_cast<bmp_t *>(pDlg->_unmapped0C) = sgPopupPendingPos;
  memset(&sgPopupPendingPos, 0, sizeof(sgPopupPendingPos));
  ctrl_set_update(pDlg);
}

// 0x4605C0, 27 B. The popup message text's PFN_CTRL_DRAW.
//
// `ret 8h` with two register arguments and two stack words IS the
// PFN_CTRL_DRAW contract. The `mov byte ptr [0x6D300E], 18h` before the tail
// call is the draw_?text family's set-then-call idiom -- draw_ctrl_text saves
// and restores gbFontJust itself, which is what makes it safe.
void popup_txt_update_fcn(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip) {
  if (pCtrl->pData) {
    gbFontJust = 0x18;
    draw_ctrl_text(pCtrl, 0, 0, NULL);
  }
}

// 0x4605E0, 234 B. Step the message control down through its font sizes until
// the text, laid out ten units to a character, fits the control's height.
// Neither size flag set, or only 0x400, ends at the small font.
void gluPopup_scale_message_size(ctrl_t *pCtrl) {
  if (pCtrl->pData == NULL)
    return;
  dlg_set_font(pCtrl);
  int nWidth = pCtrl->rc.right - pCtrl->rc.left + 1;
  int nHeight = pCtrl->rc.bottom - pCtrl->rc.top + 1;
  if (nWidth == 0)
    nWidth = 1;
  int nArea = nHeight * 10;
  while (font_strlen(static_cast<const char *>(pCtrl->pData)) * 10 / nWidth + 10 >=
         nArea / (gbFontMaxHgt + 1)) {
    DWORD dwSize = pCtrl->dwFlags & 0x4C00;
    pCtrl->dwFlags &= ~dwSize;
    if (dwSize == 0x4000) {
      pCtrl->dwFlags |= 0x800;
    } else if (dwSize != 0x800) {
      if (dwSize == 0 || dwSize == 0x400) {
        pCtrl->dwFlags |= 0x400;
        break;
      }
    }
    dlg_set_font(pCtrl);
  }
  font_set(NULL);
}

// 0x4606D0, 101 B. The popup dialog's activate.
//
// Find a control by index, point its pData at a fixed text buffer, scale the dialog to
// it, install the draw callback and request a redraw. The second control is conditional
// on its own buffer's first byte, and that arm tail-jumps into ctrl_set_update where the
// other returns normally, so the two cannot be merged.
void gluPopup_dlg_activate(ctrl_t *pDlg) {
  ctrl_t *pText = ctrl_find_ndx(pDlg, 2);
  if (pText) {
    pText->pData = sgszPopupText;
    gluPopup_scale_message_size(pText);
    pText->pfnDraw = popup_txt_update_fcn;
    ctrl_set_update(pText);
  }
  if (sgszPopupText2[0] != 0) {
    ctrl_t *pText2 = ctrl_find_ndx(pDlg, 5);
    if (pText2) {
      pText2->pData = sgszPopupText2;
      gluPopup_scale_message_size(pText2);
      ctrl_set_update(pText2);
    }
  }
}

// 0x460740, 118 B. The popup dialog's event handler -- the base of the
// family, tail-jumped to by both gluHist_dlg_fcn and (as a table entry) every
// do_* modal in this TU.
//
// The same two-level jump table as glupedit_dlg_fcn and the same selector array, at
// 0x4607CC over a five-entry dword table at 0x4607B8, so the source arm order is again
// 10, 0, 7, 1. The default arm is the shared tail, textually last, which is what a
// `return ctrl_dlg(...)` after a `default`-less switch gives. Arm 1 inlines
// deactivate_popup_dlg with no `mov ecx, esi` in front, nothing here having clobbered ECX.
int gluPopup_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 10:
        gluPopup_dlg_init(pCtrl);
        break;
      case 0:
        gluPopup_dlg_activate(pCtrl);
        break;
      case 7:
        title_position(pCtrl);
        break;
      case 1:
        deactivate_popup_dlg(pCtrl);
        break;
    }
  }
  return ctrl_dlg(pCtrl, pEvent);
}
