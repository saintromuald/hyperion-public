// gluRdy.cpp -- the mission briefing (the "ready room") screen: its dialog
// init, command and deactivate handlers, the speaking-portrait controls, the
// objectives text, the briefing-trigger walk, the scrolling briefing-text
// bitmap and the wait and advance timers. Bodies are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle (WPO off) never does across a TU; that is what the
// __declspec(noinline) markers on some declarations are for, and why moving a
// body between TUs can change the code generated. It cuts both ways: a body
// whose size is exactly caller plus callee needs that callee in this TU.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/gluRdy.h"
#include "../gen/globals117.h"
#include "../h/dlg_lbox.h"
#include "../h/dlg_ctrl.h"
#include "../h/minimap.h"   // timer_create / timer_remove_node live here
#include "../h/maphdr.h"    // close_map, sub_46A640
#include "../h/bmp.h"       // bmp_t / bmpsrc_t / bmp_clear / bmp_destroy
#include "../h/TrigAct.h"   // get_map_string
#include "../h/victory.h"   // TRIGGERNODE / TRIGGERLIST / snd_end_streamed
#include "../h/rtti.h"      // type_info::raw_name
#include "../h/TrigEnum.h"
#include "../h/Storm.h"     // SMemFree

// font_set and font_adjust are declared here rather than in a header, as
// src117/lang/dlg_draw.cpp declares them, and have to read exactly as that
// file spells them: font_adjust returns `const char *`, and a `void` spelling
// is a different mangled name. This TU discards the result, so the return
// type does not show in its own bytes.
void __fastcall font_set(void *pFont);                                  // 0x4CE160
const char * __fastcall font_adjust(int nX, int nY, const char *psz);   // 0x4CDF20

// The globals this TU reaches, dual-spelled, one guard per name, never one over
// the block, so adding a name cannot undeclare the ones below it.


// 0x6CE6DC gnDlgReturnVal -- the modal result, a signed long.
#ifdef OC_DLL_MODE
#define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
#else
extern LONG gnDlgReturnVal;
#endif

// 0x66B40C -- the briefing dialog, which has no name of its own.
#ifdef OC_DLL_MODE
#define sgpBriefingDlg (*reinterpret_cast<ctrl_t **>(OC_VA(0x66B40Cu)))
#else
extern ctrl_t *sgpBriefingDlg;
#endif



// 0x66B404 sgBriefBmp -- the briefing text's bitmap, read two ways by the
// oracle and both ways here: sub_460A70 stores a word width at +0 and a word
// height at +2 and hands the address to bmp_create and bmp_destroy, while
// glurdy_draw_text reads the first field as a dword.
#ifdef OC_DLL_MODE
#define sgBriefBmp (*reinterpret_cast<bmp_t *>(OC_VA(0x66B404u)))
#else
extern bmp_t sgBriefBmp;
#endif

// 0x66B408 -- the "bitmap is live" test, which is sgBriefBmp.pPixels tested
// before the bmp_destroy and cleared after it. Spelled as the field, since
// bmp_t's layout changes at 64 bits.
#define sgdwBriefingBmpLoaded (sgBriefBmp.pPixels)

// 0x5082F8 -- the briefing trigger list, a TRIGGERLIST (victory.h): the ECX
// Clear is called with is the list object itself.
#ifdef OC_DLL_MODE
#define sgBriefingTriggers (*reinterpret_cast<TRIGGERLIST *>(OC_VA(0x5082F8u)))
#else
extern TRIGGERLIST sgBriefingTriggers;
#endif

// 0x508300 -- sgBriefingTriggers.link.next, the list's first node, or Storm's
// negative terminator when the list is empty, so glurdy_reset_briefing clamps
// it with a signed <= 0 test. Read through the list rather than as its own
// dword, since the list's layout changes at 64 bits.
#define dword_508300 (reinterpret_cast<intptr_t>(sgBriefingTriggers.link.next))

// 0x6D0C40 `_bmp_current` (gds/image.cpp) -- the current destination bitmap,
// saved and restored around the text render.
#ifdef OC_DLL_MODE
#define _bmp_current (*reinterpret_cast<void **>(OC_VA(0x6D0C40u)))
#else
extern void *_bmp_current;
#endif


// 0x6D3028 -- sgpFonts_g[1], one of the four font handles dlg_dlg.cpp derives
// at 0x6D3024, loaded straight into font_set's ECX. Spelled as the element,
// since the table's layout changes at 64 bits.
#define dword_6D3028 (sgpFonts_g[1])

// 0x6D3046 -- a signed 16-bit value with no name of its own: the laid-out text
// height font_adjust leaves behind.
#ifdef OC_DLL_MODE
#define word_6D3046 (*reinterpret_cast<short *>(OC_VA(0x6D3046u)))
#else
extern short word_6D3046;
#endif

// 0x4EDDD4 -- the .rdata literal "gluRdy.cpp" this TU hands the allocator. An
// object, not a pointer: the address is the value, so `mov edx, 4EDDD4h` is
// five bytes where a pointer variable would cost a six-byte load.
#ifdef OC_DLL_MODE
#define aGluRdyCpp (*reinterpret_cast<char(*)[1]>(OC_VA(0x4EDDD4u)))
#else
extern char aGluRdyCpp[1];
#endif

// ctrl_t +0x24 is a WORD and +0x26 a DWORD inside dialog.h's _unmapped24[6]
// window, both widths read straight off this TU: glurdy_text_display and
// glurdy_text_scroll touch +0x24 as a word, and sub_460A20 stores a dword at
// +0x26. Reached through the filler rather than named.
// Hyperion: pointer-wide, since the dialog's holds the briefing's pointer
//.
static __forceinline DWORD_PTR &ctrl_dw26(ctrl_t *p) {
  return *reinterpret_cast<DWORD_PTR *>(&p->_unmapped24[2]);
}
static __forceinline dlg_word_t &ctrl_w24(ctrl_t *p) {
  return *reinterpret_cast<dlg_word_t *>(&p->_unmapped24[0]);
}

// The bodies, in oracle VA order.

// 0x510940, the RTTI Type Descriptor for TRIGGERNODE; Clear hands raw_name()
// to SMemFree as the tag.
#ifdef OC_DLL_MODE
#define gTypeInfo_TRIGGERNODE (*reinterpret_cast<type_info *>(OC_VA(0x510940u)))
#else
extern type_info gTypeInfo_TRIGGERNODE;
#endif

// TSList's unlink for a trigger node. The link offset needs two reaching
// definitions or MSVC folds the three-term sum; static so it inlines.
static void UnlinkBriefingTrigger(TRIGGERNODE *node) {
  TSLink<TRIGGERNODE> *pl = &node->link;
  TRIGGERNODE *pprev = pl->prev;
  if (pprev != NULL) {
    TRIGGERNODE *pnext = pl->next;
    TSLink<TRIGGERNODE> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<TRIGGERNODE> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0)
        nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
      pnl = reinterpret_cast<TSLink<TRIGGERNODE> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    pl->prev->link.next = pl->next;
    pl->prev = NULL;
    pl->next = NULL;
  }
}

// TSList::LinkNode for the briefing list with dwWhere 2 and no anchor: unlink
// the node if it is linked, then append it at the tail.
static __forceinline void LinkBriefingTrigger(TRIGGERLIST *self, TRIGGERNODE *pNode) {
  TSLink<TRIGGERNODE> *pLink = pNode ? &pNode->link : &self->link;
  TRIGGERNODE *pPrev = pLink->prev;
  TSLink<TRIGGERNODE> *pWhere;
  TRIGGERNODE *pIns;

  if (pPrev != NULL) {
    TRIGGERNODE *pSlot = pLink->next;
    if (reinterpret_cast<intptr_t>(pSlot) <= 0) {
      pSlot = reinterpret_cast<TRIGGERNODE *>(~reinterpret_cast<intptr_t>(pSlot));
    } else {
      pSlot = reinterpret_cast<TRIGGERNODE *>(
          reinterpret_cast<char *>(pSlot) +
          (reinterpret_cast<char *>(pLink) -
           reinterpret_cast<char *>(pPrev->link.next)));
    }
    *reinterpret_cast<TRIGGERNODE **>(pSlot) = pPrev;
    pLink->prev->link.next = pLink->next;
    pLink->prev = NULL;
    pLink->next = NULL;
  }
  pWhere = &self->link;
  pIns = pWhere->prev;
  pLink->prev = pIns;
  pLink->next = pIns->link.next;
  pIns->link.next = pNode;
  pWhere->prev = reinterpret_cast<TRIGGERNODE *>(pLink);
}

// TSList::NewNode for the briefing list: placement new, then the tail link.
static __forceinline TRIGGERNODE *InitBriefingTrigger(TRIGGERNODE *q) {
  q->link.prev = NULL;
  q->link.next = NULL;
  return q;
}
static __forceinline TRIGGERNODE *NewBriefingTrigger(TRIGGERLIST *self) {
  void *pv = SMemAlloc(sizeof(TRIGGERNODE), gTypeInfo_TRIGGERNODE.raw_name(), -2, 8);
  TRIGGERNODE *p = pv ? InitBriefingTrigger(reinterpret_cast<TRIGGERNODE *>(pv)) : NULL;
  LinkBriefingTrigger(self, p);
  return p;
}

// 0x4607E0. Copies pTrigger (a 0x960-byte trigger record, a TRIGGERNODE
// without its link) into the briefing list once for every executing player
// slot that means the local nation: its own slot, all players (17), or a force
// (18..21) whose member walk reaches it. nFlags is unused; the caller sets EDX.
// The force index is tested unsigned before the +1, as the oracle does.
int addBriefingTrigger(void *pTrigger, int nFlags) {
  const BYTE *pPlayers = static_cast<const BYTE *>(pTrigger) + 0x944;
  DWORD i;

  for (i = 0; i < 0x1B; i++) {
    if (pPlayers[i] == 0)
      continue;
    if (i == g_LocalNationID || i == 0x11) {
      TRIGGERNODE *p = NewBriefingTrigger(&sgBriefingTriggers);
      memcpy(&p->conditions, pTrigger, 0x960);
      continue;
    }
    DWORD nForce = i - 0x12;
    if (nForce > 3)
      continue;
    nForce++;
    int nCursor;
    for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots(); n = nation_find_next(&nCursor)) {
      if (GetPlayerForce(n) == nForce && n == g_LocalNationID) {
        TRIGGERNODE *p = NewBriefingTrigger(&sgBriefingTriggers);
        memcpy(&p->conditions, pTrigger, 0x960);
      }
    }
  }
  return 1;
}

// 0x4609C0. Unlinks and frees every trigger node.
void TRIGGERLIST::Clear(void) {
  for (;;) {
    TRIGGERNODE *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkBriefingTrigger(p);
    SMemFree(p, gTypeInfo_TRIGGERNODE.raw_name(), -2, 0);
  }
}

// 0x460A20 sub_460A20, 79 B. The "briefing is running" state reset: gray the
// start button, reveal the multiplayer-only control, tell the net layer, and
// drop the dialog callback. Its definition has to precede glurdy_dlg_command,
// which inlines it twice, once with 0 and once with 1.
void sub_460A20(ctrl_t *pDlg, int nArg1) {
  ctrl_dw26(pDlg) = 0;
  ctrl_gray(ctrl_find_ndx(pDlg, 0x0D));
  if (gbMultiPlayerMode) {
    ctrl_show(ctrl_find_ndx(pDlg, -0x0C));
  }
  packet_briefing_start(nArg1);
  if (nArg1) {
    pDlg->pfnDlgProc = NULL;
  }
}

// 0x460A70 sub_460A70, 247 B. The briefing dialog's init: publish the dialog,
// load the portrait art, seat the text control's draw handler, size and
// allocate the tall scroll bitmap, start the 100 ms advance timer, reset the
// trigger state, and either reveal the start button or gray the tutorial-skip
// one.
void sub_460A70(ctrl_t *pDlg) {
  ctrl_t *pText;

  sgpBriefingDlg = pDlg;
  sub_46A640();
  briefing_port_init();
  pText = ctrl_find_ndx(pDlg, -0x0A);
  pText->pfnDraw = reinterpret_cast<PFN_CTRL_DRAW>(&glurdy_draw_text);
  sgBriefBmp.nWidth = static_cast<short>(pText->rc.right - pText->rc.left + 1);
  sgBriefBmp.nHeight =
      static_cast<short>((pText->rc.bottom - pText->rc.top + 1) * 20);
  bmp_create(&sgBriefBmp, aGluRdyCpp, 0xE4);
  timer_create(pDlg, 0x0A, 100, reinterpret_cast<PFN_TIMER_FN *>(&glurdy_run_trigger));
  glurdy_reset_briefing(pDlg);
  if (ctrl_dw26(pDlg) == 0) {
    ctrl_show(ctrl_find_ndx(pDlg, -0x0E));
    if (gbMultiPlayerMode) {
      sub_460A20(pDlg, 0);
    } else {
      ctrl_gray(ctrl_find_ndx(pDlg, 0x14));
    }
  }
}

// 0x460B70 glurdy_dlg_command, 187 B. The briefing dialog's command handler. It
// dispatches on gnDlgReturnVal with a three-value compare chain -- 0x0D, 0x0E,
// 0x14 -- which is how MSVC lowers a sparse switch, and the 0x0E arm falls
// through into the shared default tail, which is what puts that tail right
// after the arm's last store. It returns a value: the oracle sets EAX on all
// three exits, one of them widening glues_transition_out's BYTE result.
BOOL glurdy_dlg_command(ctrl_t *pDlg) {
  switch (gnDlgReturnVal) {
    case 0x0D:
      sub_460A20(pDlg, 0);
      return 1;
    case 0x0E:
      sub_460A20(pDlg, 1);
      break;
    case 0x14:
      glurdy_reset_briefing(pDlg);
      return 1;
  }
  glurdy_stop_briefing(pDlg);
  return glues_transition_out(pDlg);
}

// 0x460C30 glurdy_dlg_deactivate, 69 B. Drops the dialog, destroys the briefing
// bitmap if it is live, frees the portraits, closes the map, empties the
// trigger list and tail-jumps brfact_clear_waiting.
void glurdy_dlg_deactivate(ctrl_t *pDlg) {
  sgpBriefingDlg = 0;
  glurdy_stop_briefing(pDlg);
  if (sgdwBriefingBmpLoaded) {
    bmp_destroy(&sgBriefBmp);
    sgdwBriefingBmpLoaded = 0;
  }
  briefing_port_free();
  close_map();
  sgBriefingTriggers.Clear();
  brfact_clear_waiting();
}

// 0x460C80 glurdy_dlg_init, 62 B. Hides the two multiplayer-only controls in a
// single-player game, then -- outside that guard, which the jz's target is what
// shows -- calls sub_475D10 and installs glurdy_wait_players as the
// dialog-level callback at +0x4A, dialog.h's pfnDlgProc.
void glurdy_dlg_init(ctrl_t *pDlg) {
  if (gbMultiPlayerMode) {
    ctrl_hide(ctrl_find_ndx(pDlg, 5));
    ctrl_hide(ctrl_find_ndx(pDlg, 0x14));
  }
  sub_475D10();
  pDlg->pfnDlgProc = (void *)&glurdy_wait_players;
}

// 0x460CC0 glurdy_draw_text, 95 B. The briefing text control's draw handler,
// which sub_460A70 seats into pText->pfnDraw: it blits one control-height
// window out of the tall scroll bitmap, the window's top being the +0x24
// scroll counter glurdy_text_scroll increments.
void glurdy_draw_text(ctrl_t *pCtrl, int nArg1, int nArg2, int nArg3) {
  bmpsrc_t src;
  // The width is read as a dword on purpose, not as `sgBriefBmp.nWidth`: only
  // the low half is ever used, and this is the oracle's own pun on the field.
  LONG w = *reinterpret_cast<const LONG *>(&sgBriefBmp);

  src.rc.left = 0;
  src.nWidth = w;
  src.rc.top = static_cast<short>(ctrl_w24(pCtrl));
  src.nHeight = static_cast<short>(pCtrl->rc.bottom - pCtrl->rc.top + 1);
  src.rc.right = static_cast<short>(w - 1);
  src.rc.bottom = static_cast<short>(src.rc.top + src.nHeight - 1);
  bmp_mask_rect(static_cast<unsigned short>(pCtrl->rc.left),
                static_cast<unsigned short>(pCtrl->rc.top), &sgBriefBmp, &src);
}

// 0x460D20 glurdy_update_objectives, 56 B. Puts the mission's objectives string
// into control -0x0B and shows it. The string id is a dword table element read
// 16-bit, truncated to get_map_string's word parameter.
void glurdy_update_objectives(ctrl_t *pDlg) {
  ctrl_t *pCtrl = ctrl_find_ndx(sgpBriefingDlg, -0x0B);
  pCtrl->pData = (void *)get_map_string((WORD)dword_5349A4[g_LocalNationID]);
  ctrl_show(pCtrl);
  ctrl_set_update(pCtrl);
}

// 0x460D60 glurdy_hide_portrait, 25 B. Hides the portrait control whose index
// is the slot biased by 0x0F. The `add dx, 0Fh` is a 16-bit add on a
// zero-extended byte, so the source adds in a word-typed expression; an
// int-typed `bSlot + 15` would leave the add 32-bit.
void glurdy_hide_portrait(BYTE bSlot) {
  brfport_hide_portrait(ctrl_find_ndx(sgpBriefingDlg, (short)(WORD)(bSlot + 0x0F)));
}

// 0x460D80 glurdy_show_portrait, 42 B. The same index bias, plus the unit's own
// portrait: ECX is forwarded untouched into get_unit_portrait, which reads it
// as a word, hence the word parameter.
void glurdy_show_portrait(WORD wUnitId, BYTE bSlot) {
  ctrl_t *pCtrl = ctrl_find_ndx(sgpBriefingDlg, (short)(WORD)(bSlot + 0x0F));
  brfport_show_portrait(pCtrl, (WORD)get_unit_portrait(wUnitId));
}

// 0x460DB0 glurdy_talking_portrait, 31 B. The same index bias again.
void glurdy_talking_portrait(BYTE bSlot, int nArg1) {
  brfport_talking_portrait(ctrl_find_ndx(sgpBriefingDlg, (short)(WORD)(bSlot + 0x0F)),
                           nArg1);
}

// 0x460DD0 glurdy_reset_briefing, 199 B. Tears the briefing text and portrait
// state down: stop the stream, hide the four portraits, blank and hide the text
// control, drop its three timers and the dialog's advance timer, clear the
// current trigger's speaking bit, and re-seat the trigger cursor from
// 0x508300.
void glurdy_reset_briefing(ctrl_t *pDlg) {
  int i;
  ctrl_t *pText;
  BYTE *pBrief;
  intptr_t nNext;

  snd_end_streamed();
  for (i = 0x0F; i <= 0x12; ++i) {
    brfport_hide_portrait(ctrl_find_ndx(pDlg, (short)i));
  }
  pText = ctrl_find_ndx(pDlg, -0x0A);
  pText->pData = NULL;
  ctrl_hide(pText);
  timer_remove_node(pText, 1);
  timer_remove_node(pText, 2);
  timer_remove_node(pText, 3);
  ctrl_hide(ctrl_find_ndx(sgpBriefingDlg, -0x0B));
  brfact_clear_waiting();
  timer_remove_node(pDlg, 0x0B);

  // ctrl_t +0x26 carries a pointer here and a clamped signed count three
  // instructions later, the oracle's own pun. The pointee is a briefing-trigger
  // record: a BYTE cursor at +0x967, a 32-byte-strided flag array based at
  // +0x164, and a DWORD flag word at +0x948.
  pBrief = reinterpret_cast<BYTE *>(ctrl_dw26(pDlg));
  if (pBrief) {
    pBrief[0x164 + (pBrief[0x967] << 5)] &= 0xFE;
    *reinterpret_cast<dlg_dword_t *>(pBrief + 0x948) &= 0xFFFFFFFEu;
    pBrief[0x967] = 0;
  }
  nNext = dword_508300;
  if (nNext <= 0) {
    nNext = 0;
  }
  // Hyperion: the whole pointer; dlg_dword_t is 32 bits and cut the node's address on
  // x64, which brfact_RunAction then read (every campaign briefing crashed)
  ctrl_dw26(pDlg) = static_cast<DWORD_PTR>(nNext);
}

// 0x460EA0 glurdy_run_trigger, 63 B. Runs the current briefing trigger's
// action, advances to the next trigger, and when the walk ends in multiplayer
// resets the state through a tail jump to sub_460A20. The next link is tested
// signed, because Storm's list terminator is negative. The oracle inlines
// sub_460A20 in glurdy_dlg_command and sub_460A70 but not here, so inlining is
// off for this body rather than noinline on the callee.
#pragma inline_depth(0)
void glurdy_run_trigger(ctrl_t *pDlg) {
  TRIGGERNODE *pNode = *reinterpret_cast<TRIGGERNODE **>(&pDlg->_unmapped24[2]);
  if (pNode && brfact_RunAction(&pNode->conditions[0])) {
    TRIGGERNODE *pNext = pNode->link.next;
    if (reinterpret_cast<intptr_t>(pNext) > 0) {
      *reinterpret_cast<TRIGGERNODE **>(&pDlg->_unmapped24[2]) = pNext;
      return;
    }
    *reinterpret_cast<TRIGGERNODE **>(&pDlg->_unmapped24[2]) = 0;
    if (gbMultiPlayerMode)
      sub_460A20(pDlg, 0);
  }
}
#pragma inline_depth()

// 0x460EE0 glurdy_set_menu_music, 15 B. Stops the music and starts track 0x16.
void glurdy_set_menu_music() {
  stop_music();
  snd_play_music(0x16);
}

// 0x460EF0 glurdy_set_music, 16 B. The same body with the track id a parameter,
// spilled to ESI across stop_music and restored for the tail jmp.
void glurdy_set_music(int nTrack) {
  stop_music();
  snd_play_music(nTrack);
}

// 0x460F00 glurdy_stop_briefing, 153 B. The lighter teardown: drop the text
// control's three timers plus the dialog's two, stop the stream, hide the text
// and objectives controls, and hide the four portraits.
void glurdy_stop_briefing(ctrl_t *pDlg) {
  ctrl_t *pText;
  int i;

  pText = ctrl_find_ndx(pDlg, -0x0A);
  timer_remove_node(pText, 1);
  timer_remove_node(pText, 2);
  timer_remove_node(pText, 3);
  timer_remove_node(pDlg, 0x0A);
  timer_remove_node(pDlg, 0x0B);
  snd_end_streamed();
  ctrl_hide(ctrl_find_ndx(pDlg, -0x0A));
  ctrl_hide(ctrl_find_ndx(pDlg, -0x0B));
  for (i = 0x0F; i <= 0x12; ++i) {
    brfport_hide_portrait(ctrl_find_ndx(pDlg, (short)i));
  }
}

// 0x460FA0 glurdy_text_display, 259 B. Renders one briefing line into the tall
// scroll bitmap and starts the scroll timers.
void glurdy_text_display(const char *pszText, unsigned int dwDuration) {
  void *pSave;
  ctrl_t *pDlg;
  ctrl_t *pText;
  int nCtrlH, nTextH;
  DWORD dwHalf, dwDen, dwStep;

  if (!pszText) {
    return;
  }
  pSave = _bmp_current;
  _bmp_current = &sgBriefBmp;
  bmp_clear(0);
  font_set(dword_6D3028);
  gbFontJust = 0x11;
  font_adjust(0, 0, pszText);
  font_set(NULL);
  // sgpBriefingDlg is read into a local first, and the find must not be folded
  // back into the call: written inline, the load and the store below it swap
  // places. Writing the find before the restore is a different body.
  pDlg = sgpBriefingDlg;
  _bmp_current = pSave;
  pText = ctrl_find_ndx(pDlg, -0x0A);
  ctrl_w24(pText) = 0;
  ctrl_show(pText);
  ctrl_set_update(pText);
  timer_create(pText, 1, dwDuration,
               reinterpret_cast<PFN_TIMER_FN *>(&glurdy_text_timeout));
  timer_remove_node(pText, 2);

  nCtrlH = pText->rc.bottom - pText->rc.top;
  nTextH = word_6D3046;
  if (nTextH > nCtrlH) {
    dwHalf = static_cast<DWORD>((nCtrlH + 1) / 2);
    dwDen = static_cast<DWORD>(nTextH) - dwHalf;
    if (dwHalf > dwDen) {
      dwDen = dwHalf;
    }
    if (dwDuration > 0xFA0) {
      ctrl_dw26(pText) = (dwDuration - 0xFA0) / dwDen;
      timer_create(pText, 3, 0xFA0,
                   reinterpret_cast<PFN_TIMER_FN *>(&glurdy_text_scroll_start));
    } else {
      dwStep = dwDuration / dwDen;
      ctrl_dw26(pText) = dwStep;
      timer_create(pText, 2, dwStep,
                   reinterpret_cast<PFN_TIMER_FN *>(&glurdy_text_scroll));
    }
  }
}

// 0x4610B0 glurdy_text_scroll, 9 B. Two instructions: bump the +0x24 scroll
// counter and tail-jump ctrl_set_update. The oracle touches +0x24 as a word and
// nothing else in the corpus reads it, so it is reached through dialog.h's
// filler rather than given a speculative name.
void glurdy_text_scroll(ctrl_t *pCtrl) {
  ++ctrl_w24(pCtrl);
  ctrl_set_update(pCtrl);
}

// 0x4610C0 glurdy_text_scroll_start, 35 B. Arms the scroll timer, id 2, with
// the interval the control carries at +0x26, then removes itself.
void glurdy_text_scroll_start(ctrl_t *pDlg, int nId) {
  timer_create(pDlg, 2, ctrl_dw26(pDlg),
               reinterpret_cast<PFN_TIMER_FN *>(&glurdy_text_scroll));
  timer_remove_node(pDlg, nId);
}

// 0x4610F0 glurdy_text_timeout, 46 B. Hides the text control and removes its
// three timers.
void glurdy_text_timeout(ctrl_t *pDlg, int nId) {
  ctrl_hide(pDlg);
  timer_remove_node(pDlg, nId);
  timer_remove_node(pDlg, 2);
  timer_remove_node(pDlg, 3);
}

// 0x461120 glurdy_wait_players, 73 B. The dialog-level callback glurdy_dlg_init
// installs at +0x4A, and it clears that slot on its way out. It publishes 100
// as the modal result when sub_476B20 fails and 101 when it succeeds with a
// non-zero count, returning early on zero, then sends the dialog event 2. The
// source predicate is negated -- `if (!sub_476B20(&n))` -- because the `jnz`
// takes the far arm and falls through to the 100 store.
void glurdy_wait_players(ctrl_t *pDlg) {
  int nResult;
  if (!sub_476B20(&nResult)) {
    gnDlgReturnVal = 100;
  } else {
    if (!nResult)
      return;
    gnDlgReturnVal = 101;
  }
  ctrl_send_msg(pDlg, 2, 0);
  pDlg->pfnDlgProc = 0;
}

// 0x461170 glurdy_show_skip_tutorial, 46 B. Shows controls 0x13 and -0x0D. The
// second index is negative, so ctrl_find_ndx's index has to be a short.
void glurdy_show_skip_tutorial() {
  ctrl_show(ctrl_find_ndx(sgpBriefingDlg, 0x13));
  ctrl_show(ctrl_find_ndx(sgpBriefingDlg, -0x0D));
}

// 0x4611A0 glurdy_start_wait_timer, 23 B. Arms glurdy_wait_timer_finished_cb as
// timer 0x0B on the briefing dialog. ECX is the interval -- it is pushed, not
// used as the dialog, which comes from the global.
void glurdy_start_wait_timer(int nInterval) {
  timer_create(sgpBriefingDlg, 0x0B, nInterval,
               reinterpret_cast<PFN_TIMER_FN *>(&glurdy_wait_timer_finished_cb));
}

// 0x4611C0 glurdy_wait_timer_finished_cb, 22 B. Clears the waiting state and
// removes itself. Both arguments are spilled to callee-saved registers across
// the inner call and restored for the tail jmp, which is the plain source
// shape.
void glurdy_wait_timer_finished_cb(ctrl_t *pDlg, int nId) {
  brfact_clear_waiting();
  timer_remove_node(pDlg, nId);
}
