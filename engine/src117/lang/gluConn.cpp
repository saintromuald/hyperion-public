// gluConn.cpp -- the connection screen: the provider and gateway list boxes
// over Storm's network-provider enumeration, and the records behind them.
// Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>       // strcmp -- /Oi expands it inline, no CRT reference

#include "../h/gluConn.h"
#include "../h/dialog.h"
#include "../h/dlg_ctrl.h"
#include "../h/dlg_dlg.h"      // dlg_load, dlg_run_modal
#include "../h/dlg_draw.h"     // draw_bmp_effect, font_set_color_tbl
#include "../h/dlg_lbox.h"
#include "../h/bmp.h"          // bmp_t
#include "../h/glues.h"        // glues_clear_all, glues_transition_in,
                          // glue_init_screen_dlg, alloc_load_bmp
#include "../h/gametype.h"     // AllocGametypes, gametype_exit
#include "../h/gluRdy.h"       // stop_music, snd_play_music, glues_transition_out
#include "../h/net_glue.h"     // NetworkShutdown
#include "../h/save_dlg.h"     // lbox_add_item, lbox_ctrl_list
#include "../h/snd.h"          // load_game_data, snd_music_enabled
#include "../h/Storm.h"        // SMemAlloc, SMemFree, SStrCopy, SNetEnumProviders
#include "../h/TSList.h"       // TSLink -- the intrusive link and its terminator
#include "../h/rtti.h"
#include "../h/uiMulti.h"      // Begin_BNET
#include "../h/victory.h"      // psnprintf
#include "../h/gds_grp2bmpx.h"  // grp_to_bmpx
#include "../h/gds_font.h"     // string_width

// Two gds/font callees no header of ours declares, both reached only from
// gluConn_gateway_list_item_draw, with their calling convention written out:
//   string_width 0x4CDD30  `ret`, ECX + EDX; all five callers read EAX whole.
//   font_adjust  0x4CDF20  `ret 4h`, ECX + EDX + one stack word the callee
//                writes through; 21 callers, all discarding EAX.
// font_adjust returns psz, or NULL for an empty string, so `const char *`. It
// has to read exactly as src117/lang/dlg_draw.cpp declares it, parameter names
// included, so that the two spellings cannot drift apart.
const char * __fastcall font_adjust(int nX, int nY, const char *psz);

// The listbox's per-item draw callback, ctrl_t +0x52, and not a PFN_CTRL_DRAW:
// the default draw_lbox_item closes `ret 0Ch` where PFN_CTRL_DRAW closes
// `ret 8h`, and this one is `ret 0Ch` with ECX and a BYTE EDX. It is local
// because dialog.h leaves the whole +0x4E..+0x55 union arm unnamed.
typedef void(__fastcall *PFN_LBOX_ITEMDRAW)(void *pCtrl, BYTE nItem, void *prcClip,
                                            int nXOfs, int nYOfs);

// ctrl_t +0x3A on a listbox node: the array of item strings, reached through
// dialog.h's `_unmapped36[8]` window for the same reason +0x52 is, as a
// per-type union arm rather than a field of every control. The body reads it
// `mov ecx, [pBox+3Ah] / mov ecx, [ecx+esi*4]` with esi the row index.
#define LBOX_ITEMS(p)   (*reinterpret_cast<const char *const **>(&(p)->_unmapped36[CTRL_SLOT2]))


// The provider list head at 0x503D8C, spelled as its own 12-byte record. It is
// a TSList: gluConn_dlg_deactivate passes 0x503D8C as the `this` of
// TSList<TPROVIDER,0>::Clear, which reads [this+8]. The base is proven twice
// over, since static_ctor_sgProviderList stores ~0x503D90 at 0x503D94, so
// 0x503D90 is &link.prev and 0x503D94 link.next; with TSList's link at +4 that
// puts the record at 0x503D8C.
typedef TSList<TPROVIDER,TSGetLink<TPROVIDER> > providerlist_t;

OC_X86_LAYOUT(sizeof(providerlist_t) == 12, "TSList.h: an unread DWORD then the 8-byte link");
static_assert(offsetof(providerlist_t, link) == 0x04,
              "static_ctor_sgProviderList: ~0x503D90 stored at 0x503D94, and "
              "TSList<TPROVIDER,0>::Clear reads [this+8]");


#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled, one `#ifndef` per name.

// 0x53DB60 -- BNetGW.cpp's `gateways`, the object derived above.
#ifdef OC_DLL_MODE
  #ifndef gateways
  #define gateways (*reinterpret_cast<BNGatewayAccess *>(OC_VA(0x53DB60u)))
  #endif
#else
extern BNGatewayAccess gateways;
#endif


// 0x503D78 sgGluConnAnims_g -- the transition-control table
// glues_transition_in takes in EDX, five (control index, edge) word pairs. Its
// address is taken and never subscripted here, so it is spelled as a byte array
// as gluChat.cpp spells its own. The extent is proven twice: sgProviderList_g
// @0x503D8C is next, so the run is 20 bytes, and glues_transition_in's first
// stack argument is the record count, which this site pushes as 5.
#ifdef OC_DLL_MODE
  #ifndef sgGluConnAnims_g
  #define sgGluConnAnims_g (reinterpret_cast<char *>(OC_VA(0x503D78u)))
  #endif
#else
extern char sgGluConnAnims_g[20] OC_UNALIGNED;
#endif

// 0x503D8C sgProviderList_g, the record above.
#ifdef OC_DLL_MODE
  #ifndef sgProviderList_g
  #define sgProviderList_g (*reinterpret_cast<providerlist_t *>(OC_VA(0x503D8Cu)))
  #endif
#else
extern providerlist_t sgProviderList_g;
#endif

// 0x668A38 gConnDlg_g -- the live connection dialog.
// do_conn_dlg stores dlg_load's result here; every other body loads it into
// ECX for ctrl_find_ndx, so it is a ctrl_t * at each of its six readings.
#ifdef OC_DLL_MODE
  #ifndef gConnDlg_g
  #define gConnDlg_g (*reinterpret_cast<ctrl_t **>(OC_VA(0x668A38u)))
  #endif
#else
extern ctrl_t *gConnDlg_g;
#endif

// 0x6CE6DC gnDlgReturnVal -- the modal result, a signed long.
#ifdef OC_DLL_MODE
  #ifndef gnDlgReturnVal
  #define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
  #endif
#else
extern LONG gnDlgReturnVal;
#endif

// 0x668A3C -- the selected provider id, a 4-byte big-endian code.
// `cmp ecx, 424E4554h` in gluConn_init_provider and `cmp dword ptr [0x668A3C],
// 424E4554h` in do_conn_dlg; 0x424E4554 is 'B','N','E','T' read MSB-first.
#ifdef OC_DLL_MODE
  #ifndef dword_668A3C
  #define dword_668A3C (*reinterpret_cast<DWORD *>(OC_VA(0x668A3Cu)))
  #endif
#else
extern DWORD dword_668A3C;
#endif

// 0x668A40 -- the previous provider id. gluConn_provider_list_select saves
// dword_668A3C into it before overwriting that with the newly selected
// provider, then compares both against 'BNET', whole-dword at either site.
#ifdef OC_DLL_MODE
  #ifndef dword_668A40
  #define dword_668A40 (*reinterpret_cast<DWORD *>(OC_VA(0x668A40u)))
  #endif
#else
extern DWORD dword_668A40;
#endif

// 0x668A44 -- a BYTE, which gluConn_provider_list_init reads with
// `movzx edx, byte ptr [0x668A44]` straight into ctrl_set_val's dword.
#ifdef OC_DLL_MODE
  #ifndef byte_668A44
  #define byte_668A44 (*reinterpret_cast<BYTE *>(OC_VA(0x668A44u)))
  #endif
#else
extern BYTE byte_668A44;
#endif

// 0x668A48 -- the DWORD code for why the connection screen exited, stored as 4
// or 5 by gluConn_init_provider and read whole by do_conn_dlg.
#ifdef OC_DLL_MODE
  #ifndef dword_668A48
  #define dword_668A48 (*reinterpret_cast<DWORD *>(OC_VA(0x668A48u)))
  #endif
#else
extern DWORD dword_668A48;
#endif

// The bodies, in oracle VA order.

// 0x454250 do_conn_dlg (131 B). The connection screen's modal entry point.
// dlg_load's last two arguments are the .rdata string "gluConn.cpp" and the
// line number 0x286, which is a bare operand in the oracle and so is written as
// a literal rather than as __LINE__. The switch is on the residue of
// `sub eax, 9`, where an `if` or a plain case label gives `cmp eax, 9h`; the
// three `jmp glues_clear_all` are MSVC duplicating one shared tail.
void do_conn_dlg() {
  gConnDlg_g = dlg_load(const_cast<char *>("rez\\gluConn.bin"),
                        reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                        reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                        const_cast<char *>("gluConn.cpp"), 0x286);

  switch (dlg_run_modal(gConnDlg_g, 0,
                        reinterpret_cast<PFN_CTRL_EVT>(gluConn_interact_fcn)) - 9) {
  default:
    glGluesMode = 0;
    break;

  case 0:
    if (dword_668A3C == 0x424E4554) {   // 'BNET'
      stop_music();
      if (!Begin_BNET(dword_668A3C)) {
        glGluesMode = 2;
        break;
      }
    }
    glGluesMode = dword_668A48;
    break;
  }

  glues_clear_all();
}

// 0x4542E0 gluConn_gateway_list_item_draw (331 B). The gateway listbox's
// per-item draw callback, stored into ctrl_t +0x52 on the dialog's init event:
// centre the bullet bitmap in the row and draw the label to its right, clipped
// to the remaining width. `ret 0Ch`, with ECX the listbox and EDX the row. As
// the constant ternary `0x90 + 2 * (bSel != 0)` the frame id keeps the flag in
// BL, where the oracle re-tests the int at each use.
void gluConn_gateway_list_item_draw(void *pArg0, BYTE bArg1, void *pArg2,
                                    int nArg3, int nArg4) {
  ctrl_t *pBox = reinterpret_cast<ctrl_t *>(pArg0);
  BYTE nItem = bArg1;
  int bSel;
  bmp_t bmp;
  short nA, nB, nC, nD;
  rect16_t rc;
  char szText[0x104];
  short nTop;
  intptr_t nWidth;

  bSel = (nItem == pBox->_unmapped4E[0]);

  rc = *reinterpret_cast<rect16_t *>(pArg2);
  grp_to_bmpx(static_cast<GROUP *>(gpCurrDlgGrp_g), bSel ? 0x92 : 0x90, &bmp, &nA, &nB, &nC, &nD);

  nTop = rc.top;
  rc.left += static_cast<short>(nArg3);
  nTop += static_cast<short>((rc.bottom - rc.top + 1) / 2 - bmp.nHeight / 2 + nArg4);
  rc.top = nTop;
  draw_bmp_effect(&bmp, 0, 0, &rc, 0x2000);

  nArg3 += bmp.nWidth + 5;
  font_set_color_tbl(static_cast<unsigned char>(3 + (bSel != 0)));

  gbFontJust = 0x11;
  gsFontMargins = *reinterpret_cast<rect16_t *>(pArg2);
  gsFontMargins.left += static_cast<short>(nArg3);
  gsFontMargins.top += static_cast<short>(nArg4);
  gsFontMargins.right += static_cast<short>(nArg3);
  gsFontMargins.bottom += static_cast<short>(nArg4);

  // The item array is re-read at each use; a cached copy costs a callee-saved
  // register across string_width and spills bSel.
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


// 0x454430 gluConn_provider_list_select (394 B). Publishes the highlighted
// provider into the screen's four text controls and re-gates the
// Battle.net-only sub-controls. The two 0x80 buffers are one 0x100 frame, and
// the format is copied out of string 0x68 first because glues_get_str's result
// does not survive the next call. dword_668A40 takes the old provider id before
// dword_668A3C is overwritten, so the two tail comparisons read the new id and
// then the old one, over two globals rather than one test repeated.
void gluConn_provider_list_select(intptr_t nArg0) {
  ctrl_t *pBox = reinterpret_cast<ctrl_t *>(nArg0);
  TPROVIDER *p;
  ctrl_t *pCtrl;
  char *pszDesc;
  char szText[0x80];
  char szFmt[0x80];

  p = reinterpret_cast<TPROVIDER *>(gluConn_provider_list_find(nArg0));
  if (p == NULL)
    return;

  pCtrl = ctrl_find_ndx(gConnDlg_g, 9);
  pCtrl->dwFlags &= ~2u;
  ctrl_set_update(pCtrl);

  dword_668A40 = dword_668A3C;
  dword_668A3C = p->dwId;

  pCtrl = ctrl_find_ndx(gConnDlg_g, 6);
  SStrCopy(static_cast<char *>(pCtrl->pData), p->szName, 0x80);
  ctrl_set_update(pCtrl);

  // The description pointer is loaded into a callee-saved register before the
  // find. Assigned from the field after the call, MSVC reloads the field: a
  // local promises the value is unchanged across the call, a field does not.
  pszDesc = const_cast<char *>(p->pszDisplayName);
  pCtrl = ctrl_find_ndx(gConnDlg_g, 8);
  pCtrl->pData = pszDesc;
  ctrl_set_update(pCtrl);

  SStrCopy(szFmt, glues_get_str(0x68), 0x80);
  psnprintf(szText, 0x80, szFmt, p->dwMaxPlayers);

  pCtrl = ctrl_find_ndx(gConnDlg_g, 7);
  SStrCopy(static_cast<char *>(pCtrl->pData), szText, 0x80);
  ctrl_set_update(pCtrl);

  ctrl_set_update(pBox);

  if (dword_668A3C == 0x424E4554) {          // 'BNET'
    sub_4583C0(gConnDlg_g, 4, 1);
  } else if (dword_668A40 == 0x424E4554) {
    sub_4583C0(gConnDlg_g, 4, 0);
    ctrl_hide(ctrl_find_ndx(gConnDlg_g, 0x0C));
    ctrl_hide(ctrl_find_ndx(gConnDlg_g, 0x0D));
    ctrl_hide(ctrl_find_ndx(gConnDlg_g, 0x0E));
  }
}

// 0x4545C0 gluConn_dlg_activate (222 B). Allocates the three text buffers the
// screen's edit and label controls draw from, seeds the description label from
// string 0xB9, and enumerates the network providers; TPROVIDER::Create, this
// TU's own body, is the per-provider callback, so SNetEnumProviders's context
// argument is the 0. The last ctrl_find_ndx reloads gConnDlg_g where the first
// three use pDlg, the same object by a different expression, and glues_get_str
// is evaluated before that find, the argument order of one SStrCopy call.
BOOL gluConn_dlg_activate(intptr_t nArg0) {
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  ctrl_t *pCtrl;
  char *pszDesc;

  NetworkShutdown();

  pCtrl = ctrl_find_ndx(pDlg, 6);
  pCtrl->pData = SMemAlloc(0x80, "gluConn.cpp", 0x1FF, 0);

  pCtrl = ctrl_find_ndx(pDlg, 7);
  pCtrl->pData = SMemAlloc(0x80, "gluConn.cpp", 0x1FF, 0);

  pCtrl = ctrl_find_ndx(pDlg, 0x0D);
  pCtrl->pData = SMemAlloc(0xFF, "gluConn.cpp", 0x1FF, 0);

  pszDesc = glues_get_str(0xB9);
  pCtrl = ctrl_find_ndx(gConnDlg_g, 0x0D);
  SStrCopy(static_cast<char *>(pCtrl->pData), pszDesc, 0xFF);
  ctrl_set_update(pCtrl);

  SNetEnumProviders(0, reinterpret_cast<void *>(TPROVIDER::Create));

  if (!snd_music_enabled())
    snd_play_music(0x16);

  gametype_exit(0);
  return AllocGametypes();
}


// 0x454740 gluConn_interact_fcn (234 B). The screen's own dialog handler,
// installed by address in do_conn_dlg, so it is a PFN_CTRL_EVT: two paths set
// EAX explicitly, one of them widening glues_transition_out's BYTE, and five
// tail-jump ctrl_dlg. The case-2 store of 9 into gnDlgReturnVal is skipped by
// the `jne` above it, so it runs only where the value was already 9; written
// after the `if`, the branch would target the store instead. The dispatch is
// one switch over {0, 1, 2, 0x0A, 0x405}.
int gluConn_interact_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
    case 0:
      gluConn_dlg_activate(reinterpret_cast<intptr_t>(pCtrl));
      glue_screen_activate(pCtrl);
      break;
    case 1:
      gluConn_dlg_deactivate(reinterpret_cast<intptr_t>(pCtrl));
      break;
    case 2:
      if (gnDlgReturnVal == 9) {
        if (!gluConn_init_provider())
          return 1;
        gnDlgReturnVal = 9;
      }
      return glues_transition_out(pCtrl);
    case 0x0A:
      gluConn_dlg_init(reinterpret_cast<intptr_t>(pCtrl));
      break;
    case 0x405:
      ctrl_show(ctrl_find_ndx(gConnDlg_g, 0x0C));
      ctrl_show(ctrl_find_ndx(gConnDlg_g, 0x0D));
      ctrl_show(ctrl_find_ndx(gConnDlg_g, 0x0E));
      break;
    }
  }

  return ctrl_dlg(pCtrl, pEvent);
}

// 0x454830 gluConn_dlg_init (142 B). Builds the screen's per-control event
// table on the stack: `lea edx, [ebp-38h]` is what makes it a local, and the
// fourteen individual stores, zeros included and in ascending address order,
// are an aggregate initialiser rather than a memset plus assignments -- under
// /arch:SSE2 a 56-byte memset would lower to xmm stores.
void gluConn_dlg_init(intptr_t nArg0) {
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  PFN_CTRL_EVT aEvtFcn[14] = {
    NULL, NULL, NULL, NULL,
    reinterpret_cast<PFN_CTRL_EVT>(gluConn_provider_list_ctrl_fcn),
    NULL, NULL, NULL,
    ctrl_btnglow_flc,
    ctrl_btnglow_flc,
    NULL,
    reinterpret_cast<PFN_CTRL_EVT>(gluConn_gateway_list_ctrl_fcn),
    NULL, NULL,
  };

  glues_transition_in(nArg0, sgGluConnAnims_g, 5, 1, 0);
  glue_init_screen_dlg(pDlg, aEvtFcn, 0x38, 0);
}

// The provider-name table Create consults: a Storm provider id and the glue
// string that names it, six rows ending in a zero id.
struct PROVIDERSTR {
  /*0x00*/ DWORD dwId;
  /*0x04*/ int nStrId;
};
static_assert(sizeof(PROVIDERSTR) == 8, "[eax*8+503D48h] / [eax*8+503D4Ch]");
static_assert(offsetof(PROVIDERSTR, dwId) == 0x00, "cmp ecx, ebx");
static_assert(offsetof(PROVIDERSTR, nStrId) == 0x04, "cmp dword ptr [eax*8+503D4Ch], 0");
#ifdef OC_DLL_MODE
  #ifndef stru_503D48
  #define stru_503D48 (reinterpret_cast<PROVIDERSTR *>(OC_VA(0x503D48u)))
  #endif
#else
extern PROVIDERSTR stru_503D48[6] OC_UNALIGNED;
#endif

// 0x5107EC, the RTTI Type Descriptor for TPROVIDER, SMemAlloc's owner tag.
#ifdef OC_DLL_MODE
  #ifndef gTypeInfo_TPROVIDER
  #define gTypeInfo_TPROVIDER (*reinterpret_cast<type_info *>(OC_VA(0x5107ECu)))
  #endif
#else
extern type_info gTypeInfo_TPROVIDER;
#endif

// The unlink half of the list's DeleteNode, spliced into Clear. The `nOff = 0`
// pre-assignment is the shape every other list in the tree matches with.
static void UnlinkProvider(TPROVIDER *p) {
  TPROVIDER *pprev = p->link.prev;
  if (pprev != 0) {
    TPROVIDER *pnext = p->link.next;
    TSLink<TPROVIDER> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<TPROVIDER> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0)
        nOff = (intptr_t)&p->link - (intptr_t)pprev->link.next;
      pnl = reinterpret_cast<TSLink<TPROVIDER> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    p->link.prev->link.next = p->link.next;
    p->link.prev = 0;
    p->link.next = 0;
  }
}

// 0x4541F0 (84 B). Unlinks and frees every provider. Normalising the head to
// null before the test keeps the loop unrotated.
void providerlist_t::Clear(void) {
  for (;;) {
    TPROVIDER *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkProvider(p);
    SMemFree(p, gTypeInfo_TPROVIDER.raw_name(), -2, 0);
  }
}

// 0x4546A0 (155 B). Frees the three buffers gluConn_dlg_activate allocated,
// empties the provider list and drops the dialog pointer. The first call is the
// nullary spelling of the folded empty body at 0x4BE950.
void __fastcall voice_squelch();
void gluConn_dlg_deactivate(intptr_t nArg0) {
  ctrl_t *const pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  ctrl_t *pCtrl;

  voice_squelch();
  pCtrl = ctrl_find_ndx(pDlg, 6);
  SMemFree(pCtrl->pData, "gluConn.cpp", 0x207, 0);
  pCtrl->pData = NULL;
  pCtrl = ctrl_find_ndx(pDlg, 7);
  SMemFree(pCtrl->pData, "gluConn.cpp", 0x207, 0);
  pCtrl->pData = NULL;
  pCtrl = ctrl_find_ndx(pDlg, 0x0D);
  SMemFree(pCtrl->pData, "gluConn.cpp", 0x207, 0);
  pCtrl->pData = NULL;
  sgProviderList_g.Clear();
  gConnDlg_g = NULL;
}

// TSList::LinkNode for the provider list, inlined into Create.
static __forceinline void LinkProvider(providerlist_t *self, TPROVIDER *pNode,
                                       ULONG dwWhere, TPROVIDER *pBefore) {
  TSLink<TPROVIDER> *pLink = pNode ? &pNode->link : &self->link;
  TPROVIDER *pPrev = pLink->prev;
  TSLink<TPROVIDER> *pWhere;

  if (pPrev != NULL) {
    TPROVIDER *pSlot = pLink->next;
    if (reinterpret_cast<intptr_t>(pSlot) <= 0) {
      pSlot = reinterpret_cast<TPROVIDER *>(~reinterpret_cast<intptr_t>(pSlot));
    } else {
      pSlot = reinterpret_cast<TPROVIDER *>(
          reinterpret_cast<char *>(pSlot) +
          (reinterpret_cast<char *>(pLink) -
           reinterpret_cast<char *>(pPrev->link.next)));
    }
    *reinterpret_cast<TPROVIDER **>(pSlot) = pPrev;
    pLink->prev->link.next = pLink->next;
    pLink->prev = NULL;
    pLink->next = NULL;
  }
  pWhere = pBefore ? &pBefore->link : &self->link;
  switch (dwWhere) {
  case 1: {
    TPROVIDER *pNext;
    intptr_t nOff;

    pLink->prev = reinterpret_cast<TPROVIDER *>(pWhere);
    pLink->next = pWhere->next;
    pNext = pWhere->next;
    nOff = self->_unmapped_00;
    if (reinterpret_cast<intptr_t>(pNext) <= 0) {
      pNext = reinterpret_cast<TPROVIDER *>(~reinterpret_cast<intptr_t>(pNext));
    } else {
      if (nOff < 0)
        nOff = reinterpret_cast<char *>(pWhere) -
               reinterpret_cast<char *>(pWhere->prev->link.next);
      pNext = reinterpret_cast<TPROVIDER *>(reinterpret_cast<char *>(pNext) + nOff);
    }
    *reinterpret_cast<TSLink<TPROVIDER> **>(pNext) = pLink;
    pWhere->next = pNode;
    break;
  }
  default: {
    TPROVIDER *pIns = pWhere->prev;

    pLink->prev = pIns;
    pLink->next = pIns->link.next;
    pIns->link.next = pNode;
    pWhere->prev = reinterpret_cast<TPROVIDER *>(pLink);
    break;
  }
  }
}

// TSList::NewNode for the provider list: placement new (TPROVIDER's
// constructor is its link's), then LinkNode when dwWhere is set.
static __forceinline TPROVIDER *InitProvider(TPROVIDER *q) {
  q->link.prev = NULL;
  q->link.next = NULL;
  return q;
}
static __forceinline TPROVIDER *NewProvider(providerlist_t *self,
                                            ULONG dwWhere, int nExtra,
                                            DWORD dwFlags) {
  void *pv = SMemAlloc(nExtra + sizeof(TPROVIDER), gTypeInfo_TPROVIDER.raw_name(), -2,  // Hyperion: 0x118 in 1.17
                       dwFlags | 8);
  TPROVIDER *p = pv ? InitProvider(reinterpret_cast<TPROVIDER *>(pv)) : NULL;
  if (dwWhere != 0)
    LinkProvider(self, p, dwWhere, static_cast<TPROVIDER *>(NULL));
  return p;
}


// 0x4548C0 TPROVIDER::Create (318 B). Storm's per-provider callback: add a
// node, copy the caps and names, and name the provider from the string table
// when its id is listed.
BOOL __stdcall TPROVIDER::Create(DWORD dwId, const char *pszName,
                                 const char *pszDesc, void *pCaps) {
  const DWORD *pdwCaps = static_cast<const DWORD *>(pCaps);
  TPROVIDER *p = NewProvider(&sgProviderList_g, 1, 0, 0);
  int i;

  p->dwFlags = pdwCaps[1] & 2;
  p->dwId = dwId;
  p->dwMaxPlayers = pdwCaps[4] > 8 ? 8 : pdwCaps[4];
  SStrCopy(p->szDescription, pszDesc, 0x80);
  p->pszDisplayName = p->szDescription;
  SStrCopy(p->szName, pszName, 0x80);

  for (i = 0; i < 6; ++i) {
    if (stru_503D48[i].dwId == dwId || stru_503D48[i].dwId == 0)
      break;
  }
  if (stru_503D48[i].nStrId != 0)
    p->pszDisplayName = glues_get_str((short)stru_503D48[i].nStrId);
  return TRUE;
}

// 0x454A00 gluConn_provider_list_ctrl_fcn (115 B). The provider listbox's event
// handler. The dispatch is a switch, and the redundant `sub eax, 0h` is MSVC's
// compare chain subtracting a first case label of 0 anyway. Cases 0, 0x0A and
// 0x0B; everything else, and every event whose wTag is not 0x0E, falls through
// to the shared lbox_ctrl_list tail, which the oracle reaches by jumping into
// case 0's epilogue.
BOOL gluConn_provider_list_ctrl_fcn(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
    case 0: {
      ctrl_t *pOther = ctrl_find_ndx(gConnDlg_g, 9);
      pOther->dwFlags |= 2;
      gluConn_provider_list_init(nArg0);
      ctrl_set_update(pOther);
      break;
    }
    case 0x0A:
      pCtrl->dwFlags |= 0x20000;
      break;
    case 0x0B:
      lbox_ctrl_list(pCtrl, pEvent);
      gluConn_provider_list_select(nArg0);
      return 1;
    }
  }

  return lbox_ctrl_list(pCtrl, pEvent);
}

// 0x454A80 gluConn_gateway_list_ctrl_fcn (122 B). The gateway listbox's event
// handler, dispatched through a two-level jump table in this body's own tail:
// the byte index array at 0x454B10 maps event ids 0..0x0B onto five arms and
// the dword table at 0x454AFC holds their addresses. Arm 4 is the default and
// arm 2, id 0x0A, falls through into it, so the flag store has no tail of its
// own. +0x52 is the listbox's per-item draw callback, reached through dialog.h's
// `_unmapped4E` window because its signature is not ctrl_t::pfnDraw's.
BOOL gluConn_gateway_list_ctrl_fcn(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
    case 0x0A:
      pCtrl->dwFlags |= 0x30000;
      break;
    case 0:
      *reinterpret_cast<PFN_LBOX_ITEMDRAW *>(&pCtrl->_unmapped4E[0x52 - 0x4E]) =
          gluConn_gateway_list_item_draw;
      gluConn_gateway_list_init(nArg0);
      break;
    case 1:
      gateways.SetCurGateway(lbox_get_selection(reinterpret_cast<lbox_t *>(pCtrl)) + 1);
      gateways.Unload();
      // The fall-through matters: as its own `lbox_ctrl_list(...); return 1;`
      // the two arms tail-merge and case 0x0B's entry moves four bytes later,
      // onto the `call`, since on the direct path ECX and EDX are still ours.
    case 0x0B:
      lbox_ctrl_list(pCtrl, pEvent);
      return 1;
    }
  }

  return lbox_ctrl_list(pCtrl, pEvent);
}

// 0x454B20 gluConn_init_provider (56 B). Whether the screen can go straight
// through on the provider already chosen: it sets the exit code at 0x668A48 and
// reports whether the caller should proceed. The failing arm returns
// Begin_BNET's own EAX untouched rather than a fresh 0, which would cost an
// `xor eax, eax`; ECX is still live at that call, since the provider id loaded
// at the top is Begin_BNET's own argument.
int gluConn_init_provider() {
  int nOk;

  if (dword_668A3C == 0x424E4554) {   // 'BNET'
    dword_668A48 = 4;
    return 1;
  }

  nOk = Begin_BNET(dword_668A3C);
  if (!nOk)
    return nOk;

  dword_668A48 = 5;
  return 1;
}

// 0x454C40 gluConn_gateway_list_init (91 B). Fills the gateway listbox from
// BNetGW.cpp's `gateways` table and selects the current one. The `push 10h`
// before BNGatewayAccess::Name is lbox_add_item's flags argument, evaluated
// early, not an argument to Name, which is __thiscall with `ret 4` and so
// consumes only the `push esi`. The selection narrows before the decrement
// (`mov dl, byte ptr [0x53DB6C] / dec dl`), because lbox_set_selection's
// parameter is an unsigned char where nCurGateway is a dword field.
void gluConn_gateway_list_init(intptr_t nArg0) {
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);
  int i;

  lbox_set_recalc(pBox, 0);

  gateways.Load();
  for (i = 1; i <= gateways.nCount; ++i)
    lbox_add_item(reinterpret_cast<ctrl_t *>(pBox), gateways.Name(i), 0x10);

  lbox_set_recalc(pBox, 1);
  lbox_set_selection(pBox, (unsigned char)(gateways.nCurGateway - 1));
}

// 0x454B60 gluConn_provider_list_find (115 B). Finds the TPROVIDER whose name
// is the listbox's current item, or NULL. The comparison is /Oi's inline strcmp
// expansion rather than a helper call, so there is no CRT reference here. The
// list head is hoisted above lbox_get_selection, as in
// gluConn_provider_list_init: the `(int)p > 0 ? p : NULL` terminator
// (src117/h/TSList.h) depends on neither call, so MSVC schedules it into the
// prologue.
intptr_t gluConn_provider_list_find(intptr_t nArg0) {
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);
  TPROVIDER *p;
  unsigned char nSel;
  const char *pszItem;

  p = sgProviderList_g.link.next;
  if ((intptr_t)p <= 0)
    p = NULL;

  nSel = lbox_get_selection(pBox);
  if (nSel == 0xFF)
    return 0;

  pszItem = reinterpret_cast<const char *>(lbox_get_item(pBox, nSel));

  while (p != NULL) {
    if (strcmp(pszItem, p->szName) == 0)
      break;
    p = p->link.next;
    if ((intptr_t)p <= 0)
      p = NULL;
  }

  return reinterpret_cast<intptr_t>(p);
}

// 0x454BE0 gluConn_provider_list_init (85 B). Fills the provider listbox from
// sgProviderList_g and re-selects. The walk cannot be written with `p != NULL`
// (src117/h/TSList.h): the last node's `next` holds
// ~(&sgProviderList_g.link.prev), a large negative number, so the terminator
// test is the signed `(int)p > 0`, and the oracle's
// `xor eax,eax / test esi,esi / cmovle esi,eax` is that ternary, hoisted above
// the first lbox_set_recalc because the head load does not depend on it.
void gluConn_provider_list_init(intptr_t nArg0) {
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);
  TPROVIDER *p;

  p = sgProviderList_g.link.next;
  if ((intptr_t)p <= 0)
    p = NULL;

  lbox_set_recalc(pBox, 0);

  while (p != NULL) {
    lbox_add_item(reinterpret_cast<ctrl_t *>(pBox), p->szName, 0);
    p = p->link.next;
    if ((intptr_t)p <= 0)
      p = NULL;
  }

  lbox_set_recalc(pBox, 1);
  ctrl_set_val(reinterpret_cast<ctrl_t *>(pBox), byte_668A44);
  gluConn_provider_list_select(nArg0);
}
