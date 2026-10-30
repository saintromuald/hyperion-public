// dlg_edit.cpp -- the single-line edit control: its event handler, the text
// buffer, caret movement, and the scroll that keeps the caret visible. Bodies
// are in oracle VA order.
//
// Every caret move runs through sub_4C7730, which reports whether a byte
// position is the lead or the trail half of a Korean double-byte character;
// the control never lets the caret settle on a trail byte.
//
// A callee defined in this file can be spliced into a caller in this file by
// /Ob2, so TU membership needs re-checking after any change here.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>        // memmove and the inline strlen the oracle emits

#include "../h/dlg_edit.h"
#include "../h/dialog.h"
#include "../h/dlg_ctrl.h"
#include "../h/dlg_dlg.h"
#include "../h/dlg_draw.h"
#include "../h/minimap.h"
#include "../h/save_dlg.h"
#include "../h/TrigAct.h"
#include "../h/Storm.h"
#include "../gen/globals117.h"
#include "../port/render/port_maptext.h"  // Hyperion: MapTextChar, MapTextCharStart
#include "../port/render/port_present.h"  // Hyperion: PortPresentTextArea

// Callees no header of ours declares, spelled with their calling convention
// written out.

// font_adjust -- oracle 0x4CDF20, spelled as src117/lang/dlg_draw.cpp spells
// it, parameter names included.
const char * __fastcall font_adjust(int nX, int nY, const char *psz);

// font_char_width -- oracle 0x4CDC30. ECX is a byte (`mov cl, byte ptr [edi]`).
// The return is int, not a short: six of the ten call sites use the full EAX
// with no widening, so the four `movzx eax, ax` sites are casts in the source.
int __fastcall font_char_width(unsigned char bChar);

// font_clear_ext_char -- oracle 0x4CE520. Nullary; the result is discarded at
// all four call sites.
void __fastcall font_clear_ext_char(void);

// font_isprint -- oracle 0x4CDE90. ECX is a byte; its one caller keeps the
// full EAX and returns it.
intptr_t __fastcall font_isprint(unsigned char bChar);

// font_set_caret -- oracle 0x4CE240, 7 bytes. ECX is a byte (`mov cl, -5Ah`) and
// nothing reads the result. edit_init is its only caller.
void __fastcall font_set_caret(unsigned char bChar);

// _mbslen -- oracle 0x4D5489, the CRT's multibyte string length. Declared here
// rather than by including <mbstring.h>, so this TU pulls in no CRT header.
// The oracle emits `push esi / call __mbslen / add esp, 4h`, so __cdecl with
// one pointer.
extern "C" {
size_t __cdecl _mbslen(const unsigned char *psz);
}

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// 0x4F11A8 -- "edit.cpp", this TU's allocator owner tag, read out of the oracle
// image at that VA. edit_init's SMemAlloc and edit_deactivate's SMemFree both
// pass it.
#ifdef OC_DLL_MODE
#define sgszEditCpp (reinterpret_cast<const char *>(OC_VA(0x4F11A8u)))
#else
extern const char sgszEditCpp[1];
#endif

// 0x4F11A4 -- the three bytes 04 A6 01, the truncation marker edit_draw_text
// overwrites the tail of the visible text with. Not a printable string: 0x04
// and 0x01 are font control codes, so it is four bytes with its NUL rather
// than a printable string.
#ifndef sgszEditMore
#ifdef OC_DLL_MODE
#define sgszEditMore (reinterpret_cast<char *>(OC_VA(0x4F11A4u)))
#else
extern char sgszEditMore[4];
#endif
#endif

// sgdwCurrentFontSet_g @0x66CCC0 (init.cpp) -- 1 selects the wide-font layout
// edit_recalc measures with.
#ifdef OC_DLL_MODE
#define sgdwCurrentFontSet_g (*reinterpret_cast<dlg_dword_t *>(OC_VA(0x66CCC0u)))
#else
extern dlg_dword_t sgdwCurrentFontSet_g;
#endif

// gbFontAllowWrap @0x50F160 (gds/font.cpp) -- cleared around the font_adjust
// call so the visible line is never wrapped.
#ifdef OC_DLL_MODE
#define gbFontAllowWrap (*reinterpret_cast<BYTE *>(OC_VA(0x50F160u)))
#else
extern BYTE gbFontAllowWrap;
#endif

// The edit control's own view of the per-type payload dialog.h leaves unnamed,
// reached through the _unmapped36 window rather than by naming a field there.
// +0x3E is outside that window and is dialog.h's pFocus, which is a dialog-only
// field; for an edit control the same byte is the caret index.
#define edit_state(p) (*reinterpret_cast<dlg_dword_t *>(&(p)->_unmapped36[0]))
#define edit_flags(p) ((p)->_unmapped36[0])
#define edit_first(p) ((p)->_unmapped36[1])
#define edit_xofs(p) ((p)->_unmapped36[2])
#define edit_yofs(p) ((p)->_unmapped36[3])
#define edit_width(p) (*reinterpret_cast<dlg_word_t *>(&(p)->_unmapped36[4]))
#define edit_hidcur(p) ((p)->_unmapped36[6])
#define edit_caret(p) (*reinterpret_cast<dlg_byte_t *>(&(p)->pFocus))

// edit_text is the 0x100-byte buffer edit_init allocates into ctrl_t::pData.
#define edit_text(p) (static_cast<char *>((p)->pData))

// dlgevt_t's +0x08 window is per-event-id too; for a key event it is the
// virtual key and its modifier mask, the same split statcmd.cpp reads.
#define evt_key(p) (*reinterpret_cast<dlg_word_t *>(&(p)->_unmapped08[0]))
#define evt_mods(p) (*reinterpret_cast<dlg_word_t *>(&(p)->_unmapped08[2]))

// The caret's blink bit, shared with the button family's pressed bit.
#define EDIT_CARET_ON 0x40000000u

// The bodies, in oracle VA order.

// 0x4C7730 (146 B). Outside the Korean locale every position is a plain byte,
// so the whole walk is skipped. The string length is recomputed on each pass
// because IsDBCSLeadByte is opaque to the compiler.
int sub_4C7730(const char *psz, int nIndex)
{
  unsigned int i;

  if (GetUserDefaultLangID() != 0x412)
    return 0;
  for (i = 0; i < strlen(psz); i++) {
    if (IsDBCSLeadByte(static_cast<BYTE>(psz[i])) != 0) {
      if (nIndex == i)
        return 1;
      i++;
      if (nIndex == i)
        return 2;
    } else if (nIndex == i) {
      return 0;
    }
  }
  return 0;
}

// 0x4C77D0 (108 B). Appending writes the terminator one past the new end;
// inserting only shifts the tail when the insert bit at +0x36 is set, so with
// it clear a mid-string keystroke overwrites. The terminator store spells the
// field, not pText, so the text pointer is the SIB base.
void edit_add_char(ctrl_t *pCtrl, BYTE bChar)
{
  char *pText = edit_text(pCtrl);
  int nLen = (int)(strlen(pText) + 1);

  if (nLen < 0x100) {
    unsigned int nCaret = edit_caret(pCtrl);

    if (nCaret == nLen - 1)
      edit_text(pCtrl)[nLen] = 0;
    else if ((edit_flags(pCtrl) & 0x10) != 0)
      memmove(pText + nCaret + 1, pText + nCaret, nLen - nCaret);
    edit_text(pCtrl)[nCaret] = bChar;
    edit_caret(pCtrl)++;
    edit_flags(pCtrl) |= 1;
    ctrl_set_update(pCtrl);
  }
}

// 0x4C7840 (59 B). Half the blink cycle is 500 ms and the other 250, chosen by
// the bit this body flips, so the caret is visible for the longer half.
void edit_ctrl_timer_fn(ctrl_t *pCtrl, int nId)
{
  int nInterval = (pCtrl->dwFlags & EDIT_CARET_ON) != 0 ? 250 : 500;

  pCtrl->dwFlags ^= EDIT_CARET_ON;
  timer_create(pCtrl, nId, nInterval,
               reinterpret_cast<PFN_TIMER_FN *>(edit_ctrl_timer_fn));
  ctrl_set_update(pCtrl);
}

// 0x4C7C00 (57 B). The text buffer is freed unconditionally; the cursor is
// only restored if this control was the one that hid it.
void edit_deactivate(ctrl_t *pCtrl)
{
  timer_remove_node(pCtrl, 2);
  SMemFree(pCtrl->pData, sgszEditCpp, 0x192, 0);
  if (edit_hidcur(pCtrl) != 0) {
    cur_show();
    dlg_unhook_all(pCtrl);
    edit_hidcur(pCtrl) = 0;
  }
}

// 0x4C7880 (781 B). The control's whole behaviour, dispatched through three
// jump tables: the event tag, then the dialog message id, then the virtual key.
// Every key that moves the caret re-asks sub_4C7730 whether it landed on a
// trail byte and steps one further.
int edit_ctrl_edit(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  switch (pEvent->wTag) {
  case 0x0E:
    switch (pEvent->dwId) {
    case 10:
      edit_init(pCtrl);
      break;
    case 1:
      edit_deactivate(pCtrl);
      break;
    case 5:
      pCtrl->dwFlags |= EDIT_CARET_ON;
      timer_create(pCtrl, 2, 0x1F4,
                   reinterpret_cast<PFN_TIMER_FN *>(edit_ctrl_timer_fn));
      ctrl_set_update(pCtrl);
      break;
    case 6:
      if (edit_hidcur(pCtrl) != 0) {
        cur_show();
        dlg_unhook_all(pCtrl);
        edit_hidcur(pCtrl) = 0;
      }
      pCtrl->dwFlags &= ~EDIT_CARET_ON;
      timer_remove_node(pCtrl, 2);
      ctrl_set_update(pCtrl);
      break;
    }
    return ctrl_evn_dlg(pCtrl, pEvent);

  case 0x0F: {
    intptr_t nHandled;

    pCtrl->dwFlags |= EDIT_CARET_ON;
    timer_create(pCtrl, 2, 0x1F4,
                 reinterpret_cast<PFN_TIMER_FN *>(edit_ctrl_timer_fn));
    ctrl_set_update(pCtrl);
    if (evt_key(pEvent) == 8 && edit_caret(pCtrl) != 0) {
      dlg_byte_t nCaret;

      edit_obscure_cursor(pCtrl, 1);
      nCaret = edit_caret(pCtrl);
      // Hyperion: the character before the caret goes whole, its UTF-8 bytes or a
      // Korean code page's pair
      const char *pText = edit_text(pCtrl);
      int nBytes = nCaret - static_cast<int>(MapTextCharStart(pText, pText + nCaret - 1) - pText);
      if (nBytes == 1 && nCaret >= 2 && sub_4C7730(pText, nCaret - 2) == 1)
        nBytes = 2;
      while (nBytes-- > 0)
        edit_remove_char(pCtrl, 0);
      return 1;
    }
    dlg_set_font(pCtrl);
    // Hyperion: a byte past ASCII may be part of a character typed as UTF-8, which the
    // font draws from TrueType when it has no glyph of its own (FontCharGlyph)
    if (evt_key(pEvent) >= 0x80 && evt_key(pEvent) <= 0xF4) {
      edit_obscure_cursor(pCtrl, 1);
      edit_add_char(pCtrl, static_cast<BYTE>(evt_key(pEvent)));
      font_set(0);
      return 1;
    }
    nHandled = font_isprint(static_cast<BYTE>(evt_key(pEvent)));
    if (nHandled != 0) {
      edit_obscure_cursor(pCtrl, 1);
      edit_add_char(pCtrl, static_cast<BYTE>(evt_key(pEvent)));
    }
    font_set(0);
#if defined(_M_IX86)
    return nHandled;
#else
    // the answer is font_isprint's glyph pointer, whose low half alone can be 0
    return nHandled != 0;
#endif
  }

  case 0:
    if (evt_key(pEvent) == 0x2D && (evt_mods(pEvent) & 0x0F) == 0) {
      edit_flags(pCtrl) ^= 0x10;
      return 1;
    }
    // fall through -- the insert key is the only one this tag handles itself
  case 1:
    switch (evt_key(pEvent)) {
    case 0x25: {
      dlg_byte_t nCaret = edit_caret(pCtrl);

      if (nCaret != 0) {
        nCaret--;
        edit_caret(pCtrl) = nCaret;
        if (sub_4C7730(edit_text(pCtrl), nCaret) == 2)
          edit_caret(pCtrl)--;
        // Hyperion: to the start of a character of several UTF-8 bytes
        const char *pText = edit_text(pCtrl);
        edit_caret(pCtrl) =
            static_cast<dlg_byte_t>(MapTextCharStart(pText, pText + edit_caret(pCtrl)) - pText);
        ctrl_set_update(pCtrl);
      }
      return 1;
    }

    case 0x27: {
      char *pText = edit_text(pCtrl);
      unsigned int nLen = (unsigned int)strlen(pText);
      dlg_byte_t nCaret = edit_caret(pCtrl);

      if (nCaret < nLen) {
        // Hyperion: over a character of several UTF-8 bytes whole
        unsigned cp;
        const int nBytes = MapTextChar(pText + nCaret, pText + nLen, &cp);
        nCaret = static_cast<dlg_byte_t>(nCaret + (nBytes > 1 ? nBytes : 1));
        edit_caret(pCtrl) = nCaret;
        if (sub_4C7730(pText, nCaret) == 2)
          edit_caret(pCtrl)++;
        ctrl_set_update(pCtrl);
      }
      return 1;
    }

    case 0x23:
      edit_caret(pCtrl) = static_cast<dlg_byte_t>(strlen(edit_text(pCtrl)));
      ctrl_set_update(pCtrl);
      return 1;

    case 0x24:
      edit_caret(pCtrl) = 0;
      ctrl_set_update(pCtrl);
      return 1;

    case 0x2E: {
      char *pText = edit_text(pCtrl);

      if (edit_caret(pCtrl) < strlen(pText)
          && sub_4C7730(pText, edit_caret(pCtrl)) == 1)
        edit_remove_char(pCtrl, 1);
      // Hyperion: a character of several UTF-8 bytes goes whole
      unsigned cp;
      const size_t nLen = strlen(pText);
      int nBytes = edit_caret(pCtrl) < nLen ? MapTextChar(pText + edit_caret(pCtrl), pText + nLen, &cp) : 0;
      for (nBytes = nBytes > 1 ? nBytes - 1 : 0; nBytes > 0; --nBytes)
        edit_remove_char(pCtrl, 1);
      if (edit_caret(pCtrl) < strlen(edit_text(pCtrl)))
        edit_remove_char(pCtrl, 1);
      return 1;
    }
    }
    return 0;

  case 4:
  case 6:
    pCtrl->dwFlags |= EDIT_CARET_ON;
    timer_create(pCtrl, 2, 0x1F4,
                 reinterpret_cast<PFN_TIMER_FN *>(edit_ctrl_timer_fn));
    ctrl_set_update(pCtrl);
    if (edit_hidcur(pCtrl) != 0) {
      cur_show();
      dlg_unhook_all(pCtrl);
      edit_hidcur(pCtrl) = 0;
    }
    edit_line_clicked(pCtrl, pEvent);
    return 1;

  case 3:
    if (edit_hidcur(pCtrl) != 0) {
      cur_show();
      dlg_unhook_all(pCtrl);
      edit_hidcur(pCtrl) = 0;
    }
    break;
  }
  return 0;
}

// 0x4C7C40 (260 B). Copies the visible slice into a stack buffer, splices the
// three-byte caret marker in at the caret, and draws that. The pen comes from
// the high nibble at +0x36 and falls back to 2 when it is zero.
void edit_draw_text(ctrl_t *pCtrl)
{
  char szBuf[0x100];
  dlg_byte_t nRel;
  dlg_byte_t nColor;

  dlg_set_font(pCtrl);
  ctrl_set_hot_flag(pCtrl);
  if ((pCtrl->dwFlags & 0x20000) == 0)
    draw_frame_hot(pCtrl, &pCtrl->rc, sgbPens_g[0]);
  if ((edit_flags(pCtrl) & 1) != 0)
    edit_recalc(pCtrl);

  nRel = static_cast<dlg_byte_t>(edit_caret(pCtrl) - edit_first(pCtrl));
  SStrCopy(szBuf, edit_text(pCtrl) + edit_first(pCtrl), 0x100);
  if ((pCtrl->dwFlags & EDIT_CARET_ON) != 0) {
    unsigned int n = nRel;
    // Hyperion: the box with the caret is the one being typed in; an input method's
    // window goes to it, at the caret
    const char cAtCaret = szBuf[n];
    szBuf[n] = '\0';
    PortPresentTextArea(pCtrl->rc.left, pCtrl->rc.top, pCtrl->rc.right, pCtrl->rc.bottom,
                        edit_xofs(pCtrl) + font_strlen(szBuf));
    szBuf[n] = cAtCaret;

    memmove(&szBuf[n + 3], &szBuf[n], 0xFD - n);
    memcpy(&szBuf[n], sgszEditMore, 3);
  }

  nColor = static_cast<dlg_byte_t>(edit_flags(pCtrl) >> 4);
  if (nColor != 0)
    font_set_color_tbl(nColor);
  else
    font_set_color_tbl(2);
  gsFontMargins = pCtrl->rc;
  gbFontAllowWrap = 0;
  gbFontJust = 0x11;
  font_adjust(edit_xofs(pCtrl) + pCtrl->rc.left,
              edit_yofs(pCtrl) + pCtrl->rc.top, szBuf);
  gbFontAllowWrap = 1;
  font_set(0);
}

// 0x4C7D50 (15 B). The 0x7FFFFFFF is the oracle's own literal: the caller's
// buffer size is not passed, so this trusts it.
void edit_get_text(ctrl_t *pCtrl, char *pszBuf)
{
  SStrCopy(pszBuf, edit_text(pCtrl), 0x7FFFFFFF);
}

// 0x4C7D60 (60 B). The 0xA6 handed to font_set_caret is the same byte edit_recalc
// measures the caret with.
void edit_init(ctrl_t *pCtrl)
{
  char *pBuf;

  dlg_set_font(pCtrl);
  font_set_caret(0xA6);
  pBuf = static_cast<char *>(SMemAlloc(0x100, sgszEditCpp, 0x182, 0));
  *pBuf = 0;
  pCtrl->pData = pBuf;
  edit_hidcur(pCtrl) = 0;
  edit_caret(pCtrl) = 0;
  edit_state(pCtrl) = 0x10;
}

// 0x4C7DA0 (161 B). The caret lands on the character the click is proportionally
// nearest, measured against the whole string's pixel width, and steps back off
// a trail byte.
void edit_line_clicked(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  point16_t pt;

  pt.x = pEvent->nX;
  pt.y = pEvent->nY;
  global_evt_2_local_point(pCtrl->pParent, pEvent, &pt);
  if (pt_in_rect(&pCtrl->rc, &pt) != 0) {
    int nX = pt.x - pCtrl->rc.left;

    dlg_set_font(pCtrl);
    if (nX <= 0) {
      edit_caret(pCtrl) = 0;
    } else {
      int nWidth = font_strlen(edit_text(pCtrl));

      if (nX >= nWidth)
        edit_caret(pCtrl) = static_cast<dlg_byte_t>(strlen(edit_text(pCtrl)));
      else
        edit_caret(pCtrl) = static_cast<dlg_byte_t>(
            strlen(edit_text(pCtrl)) * nX / nWidth);
    }
    if (sub_4C7730(edit_text(pCtrl), edit_caret(pCtrl)) == 2)
      edit_caret(pCtrl)--;
  }
}

// 0x4C7E50 (87 B). Turning the flag on twice is a no-op, but turning it off
// stores the zero whether or not the cursor was actually hidden.
void edit_obscure_cursor(ctrl_t *pCtrl, BYTE bHide)
{
  if (bHide != 0) {
    if (edit_hidcur(pCtrl) != 0)
      return;
    cur_hide();
    dlg_hook(pCtrl, 3);
    dlg_hook(pCtrl, 4);
    dlg_hook(pCtrl, 7);
  } else {
    if (edit_hidcur(pCtrl) == 0)
      return;
    cur_show();
    dlg_unhook_all(pCtrl);
  }
  edit_hidcur(pCtrl) = bHide;
}

// Hyperion: the width of the bytes from p to pEnd, a character of several UTF-8 bytes
// measured once (font_strlen), where the classic summed each byte's width
static int EditWidth(const char *p, const char *pEnd)
{
  char sz[0x100];
  const size_t n = pEnd - p < 0xFF ? static_cast<size_t>(pEnd - p) : 0xFF;
  memcpy(sz, p, n);
  sz[n] = '\0';
  return font_strlen(sz);
}

// 0x4C7EB0 (467 B). Picks the first visible character so the caret still fits
// in the control. Font set 1 scrolls forward from the current offset, font
// set 2 starts from the end of the string; any other set leaves the offset
// alone. Both walks step two bytes at a time over a Korean pair.
// Hyperion: and over a UTF-8 character's bytes together, so that the
// first one shown is never part of one.
void edit_recalc(ctrl_t *pCtrl)
{
  int nAvail;

  font_clear_ext_char();
  nAvail = static_cast<dlg_word_t>(pCtrl->rc.right - edit_xofs(pCtrl) * 2
                                   - pCtrl->rc.left + 1);
  nAvail -= font_char_width(0xA6);
  edit_width(pCtrl) = static_cast<dlg_word_t>(nAvail);

  if (sgdwCurrentFontSet_g == 1) {
    char *pCur;
    int nSum;
    dlg_byte_t nCaret;

    if (edit_caret(pCtrl) < edit_first(pCtrl)) {
      edit_first(pCtrl) = edit_caret(pCtrl);
      return;
    }
    font_clear_ext_char();
    nSum = EditWidth(edit_text(pCtrl) + edit_first(pCtrl), edit_text(pCtrl) + edit_caret(pCtrl));
    if (static_cast<short>(nSum) < static_cast<short>(nAvail))
      return;

    font_clear_ext_char();
    nCaret = edit_caret(pCtrl);
    pCur = edit_text(pCtrl) + nCaret;
    edit_first(pCtrl) = nCaret;
    while (pCur > edit_text(pCtrl)) {
      if (sub_4C7730(edit_text(pCtrl), (int)(pCur - edit_text(pCtrl) - 1)) == 2) {
        int nPair = static_cast<unsigned short>(font_char_width(pCur[-2]));

        nPair += font_char_width(pCur[-1]);
        pCur -= 2;
        if (static_cast<short>(nPair) >= static_cast<short>(nAvail))
          return;
        nAvail -= nPair;
        edit_first(pCtrl) = static_cast<dlg_byte_t>(edit_first(pCtrl) - 2);
      } else {
        const char *pStart = MapTextCharStart(edit_text(pCtrl), pCur - 1);
        int nW = EditWidth(pStart, pCur);

        if (static_cast<short>(nW) >= static_cast<short>(nAvail))
          return;
        nAvail -= nW;
        edit_first(pCtrl) = static_cast<dlg_byte_t>(edit_first(pCtrl) - (pCur - pStart));
        pCur -= pCur - pStart;
      }
    }
    return;
  }

  if (sgdwCurrentFontSet_g == 2) {
    char *pText;
    char *pCur;
    dlg_byte_t nFirst;

    font_clear_ext_char();
    pText = edit_text(pCtrl);
    edit_first(pCtrl) = static_cast<dlg_byte_t>(strlen(pText));
    pCur = pText + edit_first(pCtrl);
    while (pCur > edit_text(pCtrl)) {
      if (sub_4C7730(edit_text(pCtrl), (int)(pCur - edit_text(pCtrl) - 1)) == 2) {
        int nPair = static_cast<unsigned short>(font_char_width(pCur[-2]));

        nPair += font_char_width(pCur[-1]);
        pCur -= 2;
        if (static_cast<short>(nPair) >= static_cast<short>(nAvail))
          break;
        nAvail -= nPair;
        edit_first(pCtrl) = static_cast<dlg_byte_t>(edit_first(pCtrl) - 2);
      } else {
        const char *pStart = MapTextCharStart(edit_text(pCtrl), pCur - 1);
        int nW = EditWidth(pStart, pCur);

        if (static_cast<short>(nW) >= static_cast<short>(nAvail))
          break;
        nAvail -= nW;
        edit_first(pCtrl) = static_cast<dlg_byte_t>(edit_first(pCtrl) - (pCur - pStart));
        pCur -= pCur - pStart;
      }
    }
    nFirst = edit_first(pCtrl);
    edit_first(pCtrl) =
        nFirst < edit_caret(pCtrl) ? nFirst : edit_caret(pCtrl);
  }
}

// 0x4C8090 (100 B). Backspace moves the caret and deletes what it left;
// forward delete keeps the caret and deletes what is under it.
void edit_remove_char(ctrl_t *pCtrl, BYTE bForward)
{
  char *pText = edit_text(pCtrl);
  int nLen = (int)strlen(pText);
  dlg_byte_t nPos;
  unsigned int n;

  if (nLen == 0)
    return;
  nPos = edit_caret(pCtrl);
  if (bForward != 0)
    nPos++;
  else
    edit_caret(pCtrl) = static_cast<dlg_byte_t>(nPos - 1);
  n = nPos;
  memmove(pText + n - 1, pText + n, nLen - n);
  edit_text(pCtrl)[nLen - 1] = 0;
  if (edit_first(pCtrl) != 0)
    edit_flags(pCtrl) |= 1;
  ctrl_set_update(pCtrl);
}

// 0x4C8100 (7 B). The argument lands in the high nibble, which is the pen the
// text is drawn with; the low nibble's recalc bit is cleared as a side effect.
void edit_set_recalc(ctrl_t *pEdit, BYTE bRecalc)
{
  edit_flags(pEdit) = static_cast<dlg_byte_t>(bRecalc << 4);
}

// 0x4C8110 (54 B). The caret goes to the end of the new text, counted in
// characters rather than bytes.
void edit_set_text(ctrl_t *pCtrl, const char *pszText)
{
  dlg_byte_t nLen;

  SStrCopy(edit_text(pCtrl), pszText, 0x100);
  nLen = static_cast<dlg_byte_t>(
      _mbslen(reinterpret_cast<const unsigned char *>(pszText)));
  edit_flags(pCtrl) |= 1;
  edit_caret(pCtrl) = nLen;
  if ((pCtrl->dwFlags & 8) != 0)
    ctrl_set_update(pCtrl);
}
