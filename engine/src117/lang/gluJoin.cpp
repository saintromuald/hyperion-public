// gluJoin.cpp -- the join-game screen: the game list box, its per-row
// formatting and selection, and the screen's dialog handler. Bodies are in
// oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>            // strcmp and memset, inlined by /Oi

#include "../h/gluJoin.h"
#include "../h/Storm.h"
#include "../h/TSList.h"
#include "../h/_GAMENODE.h"
#include "../h/dialog.h"
#include "../h/dlg_ctrl.h"
#include "../h/dlg_dlg.h"
#include "../h/dlg_lbox.h"
#include "../h/gametype.h"    // gametype_find_template_name / _param_name
#include "../h/gds_local.h"   // psnprintf
#include "../h/gluBNCreate.h"  // gametype_find_template / _get_subtype_name
#include "../h/glues.h"
#include "../h/gluPopup.h"
#include "../h/gluRdy.h"  // glues_transition_out
#include "../h/net_cmds.h"  // error_get_str
#include "../h/minimap.h"   // timer_create / timer_remove_node
#include "../h/net_glue.h"  // NetworkMode / ListGames / LookupGameHdr / sub_4755F0
#include "../h/replay.h"    // map_free
#include "../h/rtti.h"      // type_info::raw_name -- OURS, not <typeinfo>
#include "../h/save_dlg.h"  // lbox_ctrl_list / lbox_add_item
#include "../h/snd.h"       // load_game_data
#include "../h/uiMulti.h"   // sub_4BAF80
#include "../gen/globals117.h"

// The advertised-game record LookupGameHdr hands back, 141 bytes, and the copy
// of the current selection kept at 0x66A8B8. Only the fields this TU reads are
// named.
#pragma pack(push, 1)
struct JOINGAMEINFO {
  /*0x00*/ DWORD dwGameId;
  /*0x04*/ char szGameName[0x1C];
  /*0x20*/ WORD wMapWidth;
  /*0x22*/ WORD wMapHeight;
  /*0x24*/ BYTE bCurPlayers;
  /*0x25*/ BYTE bMaxPlayers;
  /*0x26*/ BYTE bGameSpeed;
  /*0x27*/ BYTE _unmapped_27;
  /*0x28*/ BYTE bCategory;
  /*0x29*/ BYTE bSub;
  /*0x2A*/ WORD wType;
  /*0x2C*/ BYTE _unmapped_2C[0x32 - 0x2C];
  /*0x32*/ BYTE bSavedGame;
  /*0x33*/ BYTE _unmapped_33[0x4D - 0x33];
  /*0x4D*/ char szMapName[141 - 0x4D];
};
#pragma pack(pop)
static_assert(sizeof(JOINGAMEINFO) == 141, "rep movsd(23h) + movsb");
static_assert(offsetof(JOINGAMEINFO, szGameName) == 0x04, "lea eax, [esi+4]");
static_assert(offsetof(JOINGAMEINFO, wMapWidth) == 0x20, "movzx ecx, word [esi+20h]");
static_assert(offsetof(JOINGAMEINFO, wMapHeight) == 0x22, "movzx ecx, word [esi+22h]");
static_assert(offsetof(JOINGAMEINFO, bCurPlayers) == 0x24, "mov al, [edi+24h]");
static_assert(offsetof(JOINGAMEINFO, bMaxPlayers) == 0x25, "cmp [edi+25h], al");
static_assert(offsetof(JOINGAMEINFO, bGameSpeed) == 0x26, "movzx edx, byte [esi+26h]");
static_assert(offsetof(JOINGAMEINFO, bCategory) == 0x28, "mov cl, [esi+28h]");
static_assert(offsetof(JOINGAMEINFO, bSub) == 0x29, "mov dl, [esi+29h]");
static_assert(offsetof(JOINGAMEINFO, wType) == 0x2A, "movzx eax, word [esi+2Ah]");
static_assert(offsetof(JOINGAMEINFO, bSavedGame) == 0x32, "cmp byte [esi+32h], 0");
static_assert(offsetof(JOINGAMEINFO, szMapName) == 0x4D, "lea eax, [esi+4Dh]");

// The game-speed table at 0x4EDAEC, seven 4-byte rows, both halves signed.
struct GAMESPEEDSTR {
  /*0x00*/ short nStrId;
  /*0x02*/ short nSpeed;
};
static_assert(sizeof(GAMESPEEDSTR) == 4, "movsx word [ecx*4+4EDAECh] and +2");
static_assert(offsetof(GAMESPEEDSTR, nSpeed) == 0x02, "movsx word [ecx*4+4EDAEEh]");

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x508114 -- the advertised-game list, _GAMENODE.h's.
#ifdef OC_DLL_MODE
  #ifndef gGameNodeList_g
  #define gGameNodeList_g (*reinterpret_cast<GAMENODELIST *>(OC_VA(0x508114u)))
  #endif
  #ifndef gnDlgReturnVal
  #define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
  #endif
  #ifndef sgpJoinDlg
  #define sgpJoinDlg (*reinterpret_cast<ctrl_t **>(OC_VA(0x66A8B0u)))
  #endif
  #ifndef dword_66A8B4
  #define dword_66A8B4 (*reinterpret_cast<DWORD *>(OC_VA(0x66A8B4u)))
  #endif
  #ifndef dword_67CC90
  #define dword_67CC90 (*reinterpret_cast<DWORD *>(OC_VA(0x67CC90u)))
  #endif
  #ifndef sJoinAnims_g
  #define sJoinAnims_g (reinterpret_cast<void *>(OC_VA(0x508100u)))
  #endif
  #ifndef sJoinEvtFcnTbl_g
  #define sJoinEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x508120u)))
  #endif
  #ifndef word_508110
  #define word_508110 (*reinterpret_cast<short *>(OC_VA(0x508110u)))
  #endif
  #ifndef stru_66A8B8
  #define stru_66A8B8 (*reinterpret_cast<JOINGAMEINFO *>(OC_VA(0x66A8B8u)))
  #endif
  #ifndef byte_66A948
  #define byte_66A948 (reinterpret_cast<char *>(OC_VA(0x66A948u)))
  #endif
  #ifndef byte_66A96C
  #define byte_66A96C (reinterpret_cast<char *>(OC_VA(0x66A96Cu)))
  #endif
  #ifndef byte_66A990
  #define byte_66A990 (reinterpret_cast<char *>(OC_VA(0x66A990u)))
  #endif
#else
extern GAMENODELIST gGameNodeList_g;
extern LONG gnDlgReturnVal;
extern ctrl_t *sgpJoinDlg;
extern DWORD dword_66A8B4;
extern DWORD dword_67CC90;
extern BYTE sJoinAnims_g[16];
extern PFN_CTRL_EVT sJoinEvtFcnTbl_g[15] OC_UNALIGNED;
extern short word_508110;
extern JOINGAMEINFO stru_66A8B8;
extern char byte_66A948[0x24] OC_UNALIGNED;
extern char byte_66A96C[0x24] OC_UNALIGNED;
extern char byte_66A990[0x400];
#endif
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
  #ifndef sgGameSpeedStrs_g
  #define sgGameSpeedStrs_g (reinterpret_cast<GAMESPEEDSTR *>(OC_VA(0x4EDAECu)))
  #endif
#else
extern GAMESPEEDSTR sgGameSpeedStrs_g[7];
#endif

// 0x510820 -- 1.17's RTTI type descriptor for `struct _GAMENODE`, whose raw
// name Create hands to SMemAlloc as the owner tag.
#ifdef OC_DLL_MODE
  #ifndef gTypeInfo_GAMENODE
  #define gTypeInfo_GAMENODE (*reinterpret_cast<type_info *>(OC_VA(0x510820u)))
  #endif
#else
extern type_info gTypeInfo_GAMENODE;
#endif

// TSList's unlink, spelled as gametype.cpp spells it: the link offset is
// computed in its own branch, or MSVC reassociates the sum. A terminator's
// complement is the list's own link address.
static void UnlinkGameNode(_GAMENODE *node) {
  TSLink<_GAMENODE> *pl = &node->link;
  _GAMENODE *pprev = pl->prev;

  if (pprev != NULL) {
    _GAMENODE *pnext = pl->next;
    TSLink<_GAMENODE> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<_GAMENODE> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0)
        nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
      pnl = reinterpret_cast<TSLink<_GAMENODE> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    pl->prev->link.next = pl->next;
    pl->prev = NULL;
    pl->next = NULL;
  }
}

// The bodies, in oracle VA order.

// Unlinks and frees every advertised game. Normalising the head to null before
// the test keeps MSVC from rotating the loop.
void GAMENODELIST::Clear(void) {
  for (;;) {
    _GAMENODE *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkGameNode(p);
    SMemFree(p, gTypeInfo_GAMENODE.raw_name(), -2, 0);
  }
}

// Unlinks and frees one node and returns its successor, or null at the end.
_GAMENODE *GAMENODELIST::DeleteNode(_GAMENODE *pNode) {
  TSLink<_GAMENODE> *pl = (pNode != NULL) ? &pNode->link : &link;
  _GAMENODE *pnext = pl->next;
  if ((intptr_t)pnext <= 0)
    pnext = 0;
  UnlinkGameNode(pNode);
  SMemFree(pNode, gTypeInfo_GAMENODE.raw_name(), -2, 0);
  return pnext;
}

// Finds the game whose list text matches and returns its id. The head and each
// next link are normalised to NULL before the test, as TSList::Head() does.
int sub_4590D0(intptr_t nArg0) {
  const char *pszText = reinterpret_cast<const char *>(nArg0);

  _GAMENODE *p = gGameNodeList_g.link.next;
  if ((intptr_t)p <= 0)
    p = NULL;
  while (p != NULL) {
    if (strcmp(p->szText, pszText) == 0)
      return p->dwGameId;
    p = p->link.next;
    if ((intptr_t)p <= 0)
      p = NULL;
  }
  return 0;
}

// Runs the join screen modally and sets the next glue mode.
void do_join_dlg() {
  dword_67CC90 = 0;
  map_free();

  sgpJoinDlg = dlg_load("rez\\gluJoin.bin",
                        reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                        reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                        "gluJoin.cpp", 0x1DA);

  switch (dlg_run_modal(sgpJoinDlg, 0,
                        reinterpret_cast<PFN_CTRL_EVT>(gluJoin_dlg_fcn))) {
    case 13:
      glGluesMode = 3;
      break;
    case 14:
      glGluesMode = NetworkMode() == 0x5343424C ? 2 : 5;
      break;
    case 15:
      glGluesMode = 0x0B;
      break;
    default:
      glGluesMode = 0;
      break;
  }
  glues_clear_all();
}

// Adds or refreshes one advertised game. ListGames calls it once per
// advertisement, so the list is matched on the game id first and only a new id
// allocates. A failed allocation still stores through the null node, as in
// 1.17.
void __stdcall _GAMENODE::Create(intptr_t nArg0, intptr_t nArg1) {
  JOINGAMEINFO *pInfo = reinterpret_cast<JOINGAMEINFO *>(nArg0);
  _GAMENODE *node;
  _GAMENODE *ptail;
  TSLink<_GAMENODE> *nl;
  void *pv;
  BYTE nItem;

  node = gGameNodeList_g.link.next;
  if ((intptr_t)node > 0) {
    do {
      if (node->dwGameId == pInfo->dwGameId)
        goto found;
      node = node->link.next;
    } while ((intptr_t)node > 0 && node);
  }

  pv = SMemAlloc(sizeof(_GAMENODE), gTypeInfo_GAMENODE.raw_name(), -2, 8);
  if (pv != NULL) {
    node = reinterpret_cast<_GAMENODE *>(pv);
    node->link.prev = NULL;
    node->link.next = NULL;
    nl = &node->link;
  } else {
    node = NULL;
    nl = &gGameNodeList_g.link;
  }
  UnlinkGameNode(reinterpret_cast<_GAMENODE *>(nl));
  ptail = gGameNodeList_g.link.prev;
  nl->prev = ptail;
  nl->next = ptail->link.next;
  ptail->link.next = node;
  gGameNodeList_g.link.prev = reinterpret_cast<_GAMENODE *>(nl);
  node->dwGameId = pInfo->dwGameId;

found:
  if (pInfo->bCurPlayers == 0 && pInfo->bMaxPlayers == 0) {
    psnprintf(node->szText, 0x41, "%s", pInfo->szGameName);
  } else {
    psnprintf(node->szText, 0x41, "%s%c%u/%u", pInfo->szGameName, 0x12,
              pInfo->bCurPlayers,
              pInfo->bSavedGame != 0 ? 8 : pInfo->bMaxPlayers);
  }

  nItem = lbox_add_item(reinterpret_cast<ctrl_t *>(nArg1), node->szText, 0);
  if ((BYTE)nItem == 0xFF) {
    gGameNodeList_g.DeleteNode(node);
    return;
  }
  ++dword_66A8B4;
  node->bListIndex = (BYTE)nItem;
  node->dwSeen = 1;
}

// Puts the selected game's details in the screen's text controls and keeps a
// copy of the record at 0x66A8B8. Nothing selected blanks all seven and clears
// the copy.
void gluJoin_gamelist_select(intptr_t nArg0) {
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);
  JOINGAMEINFO *pInfo;
  unsigned char nSel;
  char *pszTemplate;
  char *pszParam;
  const TGAMEPARAMS *pParams;
  int i;
  int nSpeeds;

  nSel = lbox_get_selection(pBox);
  if (nSel != 0xFF) {
    pInfo = reinterpret_cast<JOINGAMEINFO *>(LookupGameHdr(
        sub_4590D0(reinterpret_cast<intptr_t>(lbox_get_item(pBox, nSel)))));
    if (pInfo != 0) {
      psnprintf(byte_66A948, 0x23, "%ux%u", pInfo->wMapWidth, pInfo->wMapHeight);
      pszTemplate = gametype_find_template_name(pInfo->bCategory, pInfo->bSub);
      pszParam = gametype_find_param_name(pInfo->bCategory, pInfo->bSub);
      if (pszParam != 0 && *pszParam != 0) {
        SStrCopy(byte_66A96C, pszParam, 0x20);
        SStrPack(byte_66A96C, ":", 0x20);
        pParams = gametype_find_template(pInfo->bCategory, pInfo->bSub,
                                         pInfo->wType);
        if (pParams != 0) {
          gametype_get_subtype_name(byte_66A990, 0x400, pParams,
                                    pInfo->bMaxPlayers);
        } else {
          byte_66A990[0] = 0;
        }
      } else {
        byte_66A96C[0] = 0;
        byte_66A990[0] = 0;
      }

      if (pInfo->bSavedGame != 0) {
        byte_66A96C[0] = 0;
        SStrCopy(byte_66A990, glues_get_str(0xB2), 0x400);
      }

      glue_set_text(reinterpret_cast<ctrl_t *>(nArg0), 7, pInfo->szGameName);
      glue_set_text(reinterpret_cast<ctrl_t *>(nArg0), 10, pInfo->szMapName);
      glue_set_text(reinterpret_cast<ctrl_t *>(nArg0), 8, pszTemplate);
      glue_set_text(reinterpret_cast<ctrl_t *>(nArg0), 6, byte_66A96C);
      glue_set_text(reinterpret_cast<ctrl_t *>(nArg0), 9, byte_66A990);
      glue_set_text(reinterpret_cast<ctrl_t *>(nArg0), 11, byte_66A948);

      nSpeeds = word_508110;
      i = 0;
      if (nSpeeds > 0) {
        do {
          if (sgGameSpeedStrs_g[i].nSpeed == pInfo->bGameSpeed) {
            glue_set_text(reinterpret_cast<ctrl_t *>(nArg0), 12,
                          glues_get_str(sgGameSpeedStrs_g[i].nStrId));
            break;
          }
          ++i;
        } while (i < nSpeeds);
      }

      stru_66A8B8 = *pInfo;
      return;
    }
  }

  glue_set_text(reinterpret_cast<ctrl_t *>(nArg0), 7, "");
  glue_set_text(reinterpret_cast<ctrl_t *>(nArg0), 10, "");
  glue_set_text(reinterpret_cast<ctrl_t *>(nArg0), 8, "");
  glue_set_text(reinterpret_cast<ctrl_t *>(nArg0), 6, "");
  glue_set_text(reinterpret_cast<ctrl_t *>(nArg0), 9, "");
  glue_set_text(reinterpret_cast<ctrl_t *>(nArg0), 11, "");
  glue_set_text(reinterpret_cast<ctrl_t *>(nArg0), 12, "");
  memset(&stru_66A8B8, 0, 0x8D);
}

// The game list's own event handler.
BOOL gluJoin_gamelist_ctrl_fcn(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 10:
        pCtrl->dwFlags |= 0x20000;
        break;
      case 0:
        gluJoin_gamelist_activate(nArg0);
        break;
      case 1:
        timer_remove_node(sgpJoinDlg, 0x2D);
        break;
      case 8:
        if (pEvent->dwParam != 0x2D)
          break;
        gluJoin_gamelist_setval(nArg0);
        return TRUE;
      case 11:
        lbox_ctrl_list(pCtrl, pEvent);
        gluJoin_gamelist_select(nArg0);
        return TRUE;
    }
  }

  return lbox_ctrl_list(pCtrl, pEvent);
}

// Refreshes the list once and arms the one-second re-query timer.
void gluJoin_gamelist_activate(intptr_t nArg0) {
  dword_66A8B4 = 0;
  gluJoin_gamelist_setval(nArg0);
  lbox_set_recalc(reinterpret_cast<lbox_t *>(nArg0), 1);
  timer_create(sgpJoinDlg, 0x2D, 0x3E8, 0);
}

// Drops every node the last refresh did not touch. The unlink runs twice per
// node; the second is dead, since the first zeroes the link, and 1.17 has it
// anyway.
void sub_459640() {
  _GAMENODE *p = gGameNodeList_g.link.next;

  if ((intptr_t)p > 0) {
    do {
      _GAMENODE *pnext = p->link.next;
      _GAMENODE *psafe = pnext;
      if ((intptr_t)pnext <= 0)
        psafe = NULL;
      if (p->dwSeen == 0) {
        UnlinkGameNode(p);
        UnlinkGameNode(p);
        SMemFree(p, "delete", -1, 0);
      }
      p = psafe;
    } while (p != NULL);
  }
}

// Rebuilds the list from the current advertisements, keeping the selection on
// the same game id where it still exists.
void gluJoin_gamelist_setval(intptr_t nArg0) {
  lbox_t *pBox = reinterpret_cast<lbox_t *>(nArg0);
  DWORD dwSelId;
  unsigned char nSel;
  intptr_t nDesc;

  nSel = lbox_get_selection(pBox);
  if (nSel == 0xFF) {
    dwSelId = 0;
  } else {
    nDesc = LookupGameHdr(
        sub_4590D0(reinterpret_cast<intptr_t>(lbox_get_item(pBox, nSel))));
    dwSelId = 0;
    if (nDesc != 0)
      dwSelId = *reinterpret_cast<DWORD *>(nDesc);
  }

  lbox_set_recalc(pBox, 0);
  lbox_remove_all(pBox);

  dword_66A8B4 = 0;
  _GAMENODE *p = gGameNodeList_g.link.next;
  if ((intptr_t)p > 0) {
    do {
      p->dwSeen = 0;
      p = p->link.next;
    } while ((intptr_t)p > 0 && p);
  }

  ListGames(&_GAMENODE::Create, nArg0);
  sub_459640();
  lbox_set_recalc(pBox, 1);

  ctrl_t *pJoinBtn = ctrl_find_ndx(sgpJoinDlg, 13);
  if (dword_66A8B4 != 0)
    ctrl_ungray(pJoinBtn);
  else
    ctrl_gray(pJoinBtn);

  if (dwSelId != 0) {
    _GAMENODE *q = gGameNodeList_g.link.next;
    if ((intptr_t)q > 0) {
      do {
        if (q->dwGameId == dwSelId) {
          lbox_set_selection(pBox, q->bListIndex);
          gluJoin_gamelist_select(nArg0);
          return;
        }
        q = q->link.next;
      } while ((intptr_t)q > 0 && q);
    }
    if (lbox_get_count(pBox) != 0)
      lbox_set_selection(pBox, 0);
  } else {
    lbox_set_selection(pBox, dword_66A8B4 != 0 ? 0 : 0xFF);
  }

  gluJoin_gamelist_select(nArg0);
}

// The screen's event handler. Command 2 runs a second dispatch over
// gnDlgReturnVal.
int gluJoin_dlg_fcn(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);
  ctrl_t *pJoinBtn;

  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 10:
        glues_transition_in(nArg0, sJoinAnims_g, 4, 1,
                            0);
        glue_init_screen_dlg(pDlg, sJoinEvtFcnTbl_g, 0x3C, 0);
        break;
      case 0:
        sub_4755F0();
        ctrl_gray(ctrl_find_ndx(pDlg, 13));
        glue_screen_activate(pDlg);
        return ctrl_dlg(pDlg, pEvent);
      case 2:
        if (gnDlgReturnVal == 13) {
          pJoinBtn = ctrl_find_ndx(pDlg, 13);
          ctrl_gray(pJoinBtn);
          if (!sub_4BAF80(reinterpret_cast<intptr_t>(&stru_66A8B8)))
            return 1;
          ctrl_ungray(pJoinBtn);
        } else if (gnDlgReturnVal == 15) {
          if (IsSpawnedCopy()) {
            do_pok_dlg(reinterpret_cast<intptr_t>(error_get_str(0x68)));
            return 1;
          }
        }
        return glues_transition_out(pDlg);
      case 1:
        ClearGameHeaders();
        gGameNodeList_g.Clear();
        break;
    }
  }

  return ctrl_dlg(pDlg, pEvent);
}
