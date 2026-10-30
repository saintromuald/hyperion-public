// gluLoad.cpp -- the load-game screen: the saved-game list box, its selection
// and activation handlers, and the screen's dialog handler. Bodies are in
// oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>

#include "../h/platform.h"
#include "../h/dialog.h"
#include "../h/gluLoad.h"
#include "../h/TSList.h"
#include "../h/rtti.h"
#include "../h/dlg_ctrl.h"
#include "../h/dlg_dlg.h"
#include "../h/dlg_lbox.h"
#include "../h/glues.h"
#include "../h/gluPopup.h"    // do_pok_cancel_dlg
#include "../h/gluRdy.h"      // glues_transition_out
#include "../h/net_cmds.h"    // load_game
#include "../h/saveload.h"    // sub_498B30, sub_4991B0
#include "../h/gds_local.h"   // psnprintf
#include "../h/Storm.h"
#include "../gen/globals117.h"

// The saved-game record the screen lists. Every offset below is written by
// sub_459BC0 and read back by the two walks; the gaps are not derived.
struct _SAVEGAME {
  /*0x000*/ TSLink<_SAVEGAME> link;
  /*0x008*/ char szListText[0x41];
  /*0x049*/ BYTE _unmapped049[0x133];
  /*0x17C*/ DWORD dwStamp;
  /*0x180*/ char szFile[0x1C];
  /*0x19C*/ char szTitle[0x18];
  /*0x1B4*/ char szMap[0x20];
  /*0x1D4*/ BYTE bKind;
  /*0x1D5*/ BYTE _unmapped1D5;
  /*0x1D6*/ WORD wA;
  /*0x1D8*/ WORD wB;
  /*0x1DA*/ BYTE _unmapped1DA[0xC8];
  /*0x2A2*/ BYTE aRest[0x8D];
  /*0x32F*/ BYTE _unmapped32F[1];
};

OC_X86_LAYOUT(sizeof(_SAVEGAME) == 0x330, "SMemAlloc takes 330h in sub_459BC0");
static_assert(offsetof(_SAVEGAME, link) == 0x000, "TSList intrusive link");
OC_X86_LAYOUT(offsetof(_SAVEGAME, szListText) == 0x008, "lea esi, [ebx+8h] into psnprintf and lbox_insert_item");
OC_X86_LAYOUT(offsetof(_SAVEGAME, dwStamp) == 0x17C, "mov eax, [esi+17Ch] into dword_66AD98");
OC_X86_LAYOUT(offsetof(_SAVEGAME, szFile) == 0x180, "add esi, 180h into SStrCopy(.., 1Ch)");
OC_X86_LAYOUT(offsetof(_SAVEGAME, szTitle) == 0x19C, "lea edx, [ebx+19Ch] strcpy destination");
OC_X86_LAYOUT(offsetof(_SAVEGAME, szMap) == 0x1B4, "lea edx, [ebx+1B4h] strcpy destination");
OC_X86_LAYOUT(offsetof(_SAVEGAME, bKind) == 0x1D4, "mov byte ptr [ebx+1D4h], al");
OC_X86_LAYOUT(offsetof(_SAVEGAME, wA) == 0x1D6, "mov word ptr [ebx+1D6h], ax");
OC_X86_LAYOUT(offsetof(_SAVEGAME, wB) == 0x1D8, "mov word ptr [ebx+1D8h], ax");
OC_X86_LAYOUT(offsetof(_SAVEGAME, aRest) == 0x2A2, "lea edi, [ebx+2A2h] / rep movsd");

typedef TSList<_SAVEGAME, TSGetLink<_SAVEGAME> > SAVEGAMELIST;

// The on-disk saved-game header sub_4991B0 hands its callback. Only the fields
// sub_459BC0 reads are named; the 141-byte block it copies wholesale starts at
// +0x0C and overlaps them, so it is spelled as an offset rather than a member.
struct _SAVEGAMEFILE {
  /*0x00*/ BYTE _unmapped000[0x10];
  /*0x10*/ char szTitle[0x18];
  /*0x28*/ DWORD dwStamp;
  /*0x2C*/ WORD wA;
  /*0x2E*/ WORD wB;
  /*0x30*/ BYTE bKind;
  /*0x31*/ BYTE _unmapped031[0x28];
  /*0x59*/ char szMap[0x40];
  /*0x99*/ char szFile[0x1C];
};

static_assert(sizeof(_SAVEGAMEFILE) == 0xB8, "NOT DERIVED -- the record is an input, so this is only the padded extent of the fields sub_459BC0 reads");
static_assert(offsetof(_SAVEGAMEFILE, szTitle) == 0x10, "lea ecx, [esi+10h] strcpy source");
static_assert(offsetof(_SAVEGAMEFILE, dwStamp) == 0x28, "mov eax, [esi+28h] into node+17Ch");
static_assert(offsetof(_SAVEGAMEFILE, wA) == 0x2C, "movzx eax, word ptr [esi+2Ch]");
static_assert(offsetof(_SAVEGAMEFILE, wB) == 0x2E, "movzx eax, word ptr [esi+2Eh]");
static_assert(offsetof(_SAVEGAMEFILE, bKind) == 0x30, "mov al, byte ptr [esi+30h]");
static_assert(offsetof(_SAVEGAMEFILE, szMap) == 0x59, "lea ecx, [esi+59h] strcpy source");
static_assert(offsetof(_SAVEGAMEFILE, szFile) == 0x99, "lea ecx, [esi+99h] strcpy source");

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x66AD90 / 0x66AD94 -- the load dialog and its saved-game listbox, both
// dlg/ctrl pointers: the first is dlg_load's result and ctrl_find_ndx's ECX,
// the second is the listbox's own ECX, stored by gluLoad_listbox_ctrl_activate.
#ifdef OC_DLL_MODE
#define dword_66AD90 (*reinterpret_cast<ctrl_t **>(OC_VA(0x66AD90u)))
#define dword_66AD94 (*reinterpret_cast<lbox_t **>(OC_VA(0x66AD94u)))
#else
extern ctrl_t *dword_66AD90;
extern lbox_t *dword_66AD94;
#endif

// 0x66AD98 / 0x66AD9C -- the selected record's stamp and file name, the pair
// load_game is handed. 0x1C is SStrCopy's own size argument.
#ifdef OC_DLL_MODE
#define dword_66AD98 (*reinterpret_cast<DWORD *>(OC_VA(0x66AD98u)))
#define byte_66AD9C (reinterpret_cast<char *>(OC_VA(0x66AD9Cu)))
#else
extern DWORD dword_66AD98;
extern char byte_66AD9C[0x1C] OC_UNALIGNED;
#endif

// 0x508160 -- the screen's transition-animation descriptor, handed to
// glues_transition_in as its EDX. Its extent is not proven: 12 bytes is the
// span to the next named global.
#ifdef OC_DLL_MODE
#define sgGluLoadAnims_g (reinterpret_cast<BYTE *>(OC_VA(0x508160u)))
#else
extern BYTE sgGluLoadAnims_g[12];
#endif

// 0x50816C -- the saved games, a Storm TSList, declared in the TU because it is
// a struct. 0x508174 is its link.next under src117/h/TSList.h's 12-byte
// layout.
#ifdef OC_DLL_MODE
#define gSaveGameList_g (*reinterpret_cast<TSList<_SAVEGAME, TSGetLink<_SAVEGAME> > *>(OC_VA(0x50816Cu)))
#else
extern TSList<_SAVEGAME, TSGetLink<_SAVEGAME> > gSaveGameList_g;
#endif

// 0x508178 -- the screen's per-control event-function table, passed to
// glue_init_screen_dlg with a size of 0x1C, i.e. seven entries.
#ifdef OC_DLL_MODE
#define sLoadEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x508178u)))
#else
extern PFN_CTRL_EVT sLoadEvtFcnTbl_g[7] OC_UNALIGNED;
#endif

// 0x510838 -- the MSVC RTTI type descriptor for `struct _SAVEGAME`, reached as
// `mov ecx, 510838h` / `call type_info::raw_name` for SMemAlloc's owner tag.
// `typeid` would emit our own descriptor at an address we do not control.
#ifdef OC_DLL_MODE
#define gTypeInfo__SAVEGAME (*reinterpret_cast<type_info *>(OC_VA(0x510838u)))
#else
extern type_info gTypeInfo__SAVEGAME;
#endif

// 0x4FDB60 -- the "%s" psnprintf uses to build a row's display text.
#ifdef OC_DLL_MODE
#define sgszFmtStr_g (reinterpret_cast<char *>(OC_VA(0x4FDB60u)))
#else
extern char sgszFmtStr_g[3];
#endif

// 0x4EDB38 -- the screen's dialog resource path, an address handed on rather
// than a string this TU reads.
#ifdef OC_DLL_MODE
#define sgszGluLoadBin_g (reinterpret_cast<char *>(OC_VA(0x4EDB38u)))
#else
extern char sgszGluLoadBin_g[1];
#endif

// 0x6CE6DC gnDlgReturnVal -- the modal result, a signed long.
#ifdef OC_DLL_MODE
  #ifndef gnDlgReturnVal
  #define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
  #endif
#else
extern LONG gnDlgReturnVal;
#endif

// 0x536720 -- the expansion gate, which has neither a name nor a type of its
// own, so it is spelled by address. Its only access shape in .text is
// `cmp byte ptr [536720h], 0h`.
#ifdef OC_DLL_MODE
  #ifndef byte_536720
  #define byte_536720 (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
  #endif
#else
extern BYTE byte_536720;
#endif

// src117/h/TSList.h states the terminator convention: the last node's next
// holds ~(&list.link.prev), so every walk below ends on a signed <= 0 and never
// on == NULL. The sentinel arm and the `p != 0` loop test are two branches off
// one flag set, which is what keeps `jle` and `jne` adjacent.

// The bodies, in oracle VA order.

// The load screen's modal loop. The modal result picks between loading the
// selected game, returning to the campaign menu and returning to the main menu.
void do_load_dlg() {
  ctrl_t *pDlg;

  pDlg = reinterpret_cast<ctrl_t *>(
      glue_load_cd_dlg(reinterpret_cast<intptr_t>(sgszGluLoadBin_g)));
  dword_66AD90 = pDlg;
  switch (dlg_run_modal(pDlg, 0,
                        reinterpret_cast<PFN_CTRL_EVT>(gluload_dlg_fcn))) {
    default:
      glGluesMode = 0;
      break;
    case 5:
      glGluesMode = byte_536720 != 0 ? 0x16 : 6;
      break;
    case 4:
      load_game(dword_66AD98, byte_66AD9C);
      break;
  }
  glues_clear_all();
}

// The selected save record, or 0 when the list is empty, nothing is selected or
// no record carries the selected text. Every failure returns 0 on its own, so
// the walk ends in EAX and the caller forms `&p->szListText` after the join.
static __forceinline _SAVEGAME *FindSelectedSave(lbox_t *pBox) {
  _SAVEGAME *p = gSaveGameList_g.link.next;
  if (p == 0 || reinterpret_cast<intptr_t>(p) <= 0)
    return 0;
  BYTE nSel = lbox_get_selection(pBox);
  if (nSel == 0xFF)
    return 0;
  void *pItem = lbox_get_item(pBox, nSel);
  p = gSaveGameList_g.link.next;
  if (reinterpret_cast<intptr_t>(p) > 0) {
    while (p != 0) {
      if (reinterpret_cast<void *>(p->szListText) == pItem)
        return p;
      p = p->link.next;
      if (reinterpret_cast<intptr_t>(p) <= 0)
        break;
    }
  }
  return 0;
}

// The delete button: confirm, drop the selected record's file, then rebuild the
// list from disk and reselect. With nothing selected the oracle passes 8, the
// name field of a null record.
void sub_4599E0() {
  if (!do_pok_cancel_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x16))))
    return;

  sub_498B30(FindSelectedSave(dword_66AD94)->szListText);

  lbox_t *pBox = dword_66AD94;
  lbox_set_recalc(pBox, 0);
  lbox_remove_all(pBox);
  sub_4991B0(sub_459BC0);
  lbox_set_recalc(pBox, 1);
  gluLoad_listbox_ctrl_select(reinterpret_cast<intptr_t>(pBox));
}

// The load screen's dialog handler: build the screen on 0x0A, take the modal
// exit on 2 and re-activate on 0.
int gluload_dlg_fcn(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *const pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *const pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0x0A:
        glues_transition_in(nArg0, sgGluLoadAnims_g,
                            3, 1, 0);
        glue_init_screen_dlg(pCtrl, sLoadEvtFcnTbl_g, 0x1C, 0);
        break;
      case 2:
        if (gnDlgReturnVal == 7) {
          sub_4599E0();
          return TRUE;
        }
        return glues_transition_out(pCtrl);
      case 0:
        glue_screen_activate(pCtrl);
        break;
      default:
        break;
    }
  }
  return ctrl_dlg(pCtrl, pEvent);
}

// The listbox selection changed: enable or disable the load and delete buttons
// and publish the selected record.
void gluLoad_listbox_ctrl_select(intptr_t nArg0) {
  lbox_t *const pBox = reinterpret_cast<lbox_t *>(nArg0);
  _SAVEGAME *p;
  void *pItem;
  BYTE nSel;

  p = gSaveGameList_g.link.next;
  if (p != 0 && reinterpret_cast<intptr_t>(p) > 0) {
    nSel = lbox_get_selection(pBox);
    if (nSel != 0xFF) {
      pItem = lbox_get_item(pBox, nSel);
      p = gSaveGameList_g.link.next;
      if (reinterpret_cast<intptr_t>(p) > 0) {
        while (p != 0) {
          if (reinterpret_cast<void *>(p->szListText) == pItem)
            goto found;
          p = p->link.next;
          if (reinterpret_cast<intptr_t>(p) <= 0)
            break;
        }
      }
    }
  }
disable:
  ctrl_gray(ctrl_find_ndx(dword_66AD90, 4));
  ctrl_gray(ctrl_find_ndx(dword_66AD90, 7));
  return;

found:
  if (p == 0)
    goto disable;
  ctrl_ungray(ctrl_find_ndx(dword_66AD90, 4));
  ctrl_ungray(ctrl_find_ndx(dword_66AD90, 7));
  dword_66AD98 = p->dwStamp;
  SStrCopy(byte_66AD9C, p->szFile, 0x1C);
}

// Unlink the node from wherever it is, then splice it in: dwWhere 1 puts it at
// the head, anything else before pBefore. The list's +0 word caches the link
// offset and a negative value means recover it from the data instead.
__forceinline void SAVEGAMELIST::LinkNode(_SAVEGAME *pNode, ULONG dwWhere,
                            _SAVEGAME *pBefore) {
  TSLink<_SAVEGAME> *pLink = pNode ? &pNode->link : &link;
  _SAVEGAME *pPrev = pLink->prev;
  TSLink<_SAVEGAME> *pWhere;

  if (pPrev != NULL) {
    _SAVEGAME *pSlot = pLink->next;
    if (reinterpret_cast<intptr_t>(pSlot) <= 0) {
      pSlot = reinterpret_cast<_SAVEGAME *>(~reinterpret_cast<intptr_t>(pSlot));
    } else {
      pSlot = reinterpret_cast<_SAVEGAME *>(
          reinterpret_cast<char *>(pSlot) +
          (reinterpret_cast<char *>(pLink) -
           reinterpret_cast<char *>(pPrev->link.next)));
    }
    *reinterpret_cast<_SAVEGAME **>(pSlot) = pPrev;
    pLink->prev->link.next = pLink->next;
    pLink->prev = NULL;
    pLink->next = NULL;
  }
  pWhere = pBefore ? &pBefore->link : &link;
  if (--dwWhere != 0) {
    _SAVEGAME *pIns = pWhere->prev;

    pLink->prev = pIns;
    pLink->next = pIns->link.next;
    pIns->link.next = pNode;
    pWhere->prev = reinterpret_cast<_SAVEGAME *>(pLink);
  } else {
    _SAVEGAME *pNext;
    intptr_t nOff;

    pLink->prev = reinterpret_cast<_SAVEGAME *>(pWhere);
    pLink->next = pWhere->next;
    pNext = pWhere->next;
    nOff = _unmapped_00;
    if (reinterpret_cast<intptr_t>(pNext) <= 0) {
      pNext = reinterpret_cast<_SAVEGAME *>(~reinterpret_cast<intptr_t>(pNext));
    } else {
      if (nOff < 0)
        nOff = reinterpret_cast<char *>(pWhere) -
               reinterpret_cast<char *>(pWhere->prev->link.next);
      pNext = reinterpret_cast<_SAVEGAME *>(reinterpret_cast<char *>(pNext) + nOff);
    }
    *reinterpret_cast<TSLink<_SAVEGAME> **>(pNext) = pLink;
    pWhere->next = pNode;
  }
}

// Allocate a record plus the caller's tail, zero its link and splice it in.
// The allocation tag is the type's own RTTI raw name.
__forceinline _SAVEGAME *SAVEGAMELIST::NewNode(ULONG dwWhere, int nExtra,
                                 DWORD dwFlags) {
  _SAVEGAME *p = reinterpret_cast<_SAVEGAME *>(
      SMemAlloc(nExtra + sizeof(_SAVEGAME), gTypeInfo__SAVEGAME.raw_name(), -2,  // Hyperion: 0x330 in 1.17
                dwFlags | 8));

  if (p != NULL) {
    p->link.prev = NULL;
    p->link.next = NULL;
  } else {
    p = NULL;
  }
  if (dwWhere != 0)
    LinkNode(p, dwWhere, NULL);
  return p;
}

// sub_4991B0's per-file callback: turn one on-disk header into a list record
// and give the listbox a row for it. Each copy's destination is the previous
// copy's result plus the field size, so every strcpy address has a second use;
// written from pNode, MSVC folds the offset into the loop and loses its padding.
void __stdcall sub_459BC0(void *pArg0) {
  const _SAVEGAMEFILE *const pFile = static_cast<const _SAVEGAMEFILE *>(pArg0);
  _SAVEGAME *pNode;

  pNode = gSaveGameList_g.NewNode(1, 0, 0);
  strcpy(pNode->szFile, pFile->szFile);
  char *pT = strcpy(pNode->szTitle, pFile->szTitle);
  char *pM = strcpy(pT + sizeof(pNode->szTitle), pFile->szMap);
  pNode->dwStamp = pFile->dwStamp;
  pM[sizeof(pNode->szMap)] = pFile->bKind;
  pNode->wA = pFile->wA;
  pNode->wB = pFile->wB;
  memcpy(pNode->aRest, reinterpret_cast<const BYTE *>(pFile) + 0x0C,
         sizeof(pNode->aRest));
  psnprintf(pNode->szListText, 0x41, sgszFmtStr_g, pNode->szFile);
  lbox_insert_item(reinterpret_cast<ctrl_t *>(dword_66AD94), 0,
                   pNode->szListText, 0);
}

// The unlink half of the list's DeleteNode, spliced into Clear. The `nOff = 0`
// pre-assignment is the shape every other list in the tree matches with.
static void UnlinkSaveGame(_SAVEGAME *p) {
  _SAVEGAME *pprev = p->link.prev;
  if (pprev != 0) {
    _SAVEGAME *pnext = p->link.next;
    TSLink<_SAVEGAME> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<_SAVEGAME> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0)
        nOff = (intptr_t)&p->link - (intptr_t)pprev->link.next;
      pnl = reinterpret_cast<TSLink<_SAVEGAME> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    p->link.prev->link.next = p->link.next;
    p->link.prev = 0;
    p->link.next = 0;
  }
}

// 0x459910 (84 B). Unlinks and frees every saved-game record. Normalising the
// head to null before the test keeps the loop unrotated.
void SAVEGAMELIST::Clear(void) {
  for (;;) {
    _SAVEGAME *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkSaveGame(p);
    SMemFree(p, gTypeInfo__SAVEGAME.raw_name(), -2, 0);
  }
}

// The saved-game listbox's event handler. Four ids are intercepted and
// everything else forwards to the stock list handler. The byte index table at
// 0x459DC4 over dwId 0..0x0B selects from the five targets at 0x459DB0, both
// inside this COMDAT's tail.
BOOL gluLoad_listbox_ctrl_fcn(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *const pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *const pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0x0A:
        pCtrl->dwFlags |= 0x20000;
        break;
      case 0:
        gluLoad_listbox_ctrl_activate(nArg0);
        break;
      case 0x0B:
        lbox_ctrl_list(pCtrl, pEvent);
        gluLoad_listbox_ctrl_select(nArg0);
        return TRUE;
      case 1:
        gSaveGameList_g.Clear();
        break;
      default:
        break;
    }
  }
  return lbox_ctrl_list(pCtrl, pEvent);
}

// The listbox re-activates: grey both buttons, refill from disk and reselect.
void gluLoad_listbox_ctrl_activate(intptr_t nArg0) {
  lbox_t *const pBox = reinterpret_cast<lbox_t *>(nArg0);

  ctrl_t *pCtrl;

  dword_66AD94 = pBox;
  pCtrl = ctrl_find_ndx(dword_66AD90, 4);
  pCtrl->dwFlags |= 2;
  ctrl_set_update(pCtrl);
  pCtrl = ctrl_find_ndx(dword_66AD90, 7);
  pCtrl->dwFlags |= 2;
  ctrl_set_update(pCtrl);
  lbox_set_recalc(pBox, 0);
  lbox_remove_all(pBox);
  sub_4991B0(sub_459BC0);
  lbox_set_recalc(pBox, 1);
  gluLoad_listbox_ctrl_select(nArg0);
}
