// gluLogin.cpp -- the login screen: a listbox of saved character ids, backed by
// gIDList_g, a Storm TSList whose terminator is a complemented address rather than NULL.
// Read src117/h/TSList.h before touching any walk here: a loop written `p != NULL`
// compiles to the wrong branch. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/gluLogin.h"
#include "../h/charactr.h"  // EnumCharacterFiles, PFN_ENUMCHARACTER,
                            // CreateNewCharacterFile

#include "../h/TSList.h"     // ours: TSLink / TSGetLink / TSList, and
                        // the complemented terminator convention
#include "../h/Storm.h"      // ours: SMemAlloc
#include "../h/rtti.h"       // ours: type_info::raw_name -- NOT <typeinfo>
#include "../h/dlg_dlg.h"    // ours: dlg_load, dlg_run_modal
#include "../h/dlg_ctrl.h"   // ours: ctrl_dlg, ctrl_find_ndx, ctrl_gray/ungray, ctrl_send_msg
#include "../h/dlg_lbox.h"   // ours: lbox_get_count, lbox_remove_all/current, lbox_set_*
#include "../h/save_dlg.h"   // ours: lbox_add_item, lbox_ctrl_list, _mbsicmp
#include "../h/glues.h"      // ours: glues_get_str, glues_clear_all, glue_*, glues_transition_in
#include "../h/gluRdy.h"     // ours: glues_transition_out
#include "../h/gluPopup.h"   // ours: do_pok_dlg, do_pok_cancel_dlg, do_pedit_dlg
#include "../h/net_cmds.h"   // ours: error_get_str, is_bad_filename, voice_squelch
#include "../h/net_glue.h"   // ours: SetLocalPlayerName, NetworkMode
#include "../h/net_player.h" // ours: net_exit
#include "../h/uiMulti.h"    // ours: remove_character
#include "../h/minimap.h"    // ours: timer_create, timer_remove_node
#include "../h/snd.h"      // ours: load_game_data -- dlg_load's loader callback
#include "../gen/globals117.h"
#include "../port/front/port_front.h"  // Hyperion: PortFrontProfileChosen, PortFrontPassProfile, PortFrontAfterProfile


// The globals this TU reaches, spelled both ways with one guard per name: a `#define`
// over OC_VA under OC_DLL_MODE, a plain `extern` otherwise. The names are the data
// sheet's, filed there under gluLogin.cpp.

// 0x5081A0 -- the ID list, a TSList: gluLogin_dlg_fcn passes it as `this` to Clear and
// gluLogin_delete_id to DeleteNode, and every walk in the TU reads the head absolutely at
// 0x5081A8, which is `link.next` under src117/h/TSList.h's 12-byte layout.
#ifdef OC_DLL_MODE
#define gIDList_g (*reinterpret_cast<TSList<_ID, TSGetLink<_ID> > *>(OC_VA(0x5081A0u)))
#else
extern TSList<_ID, TSGetLink<_ID> > gIDList_g;
#endif

// 0x66ADB8 -- the login dialog. `mov dword ptr [0x66ADB8], eax` straight from
// dlg_load's result in do_login_dlg, and read back as ctrl_find_ndx's ECX in
// four bodies. A ctrl_t *.
#ifdef OC_DLL_MODE
#define sgpGluLoginDlg_g (*reinterpret_cast<ctrl_t **>(OC_VA(0x66ADB8u)))
#else
extern ctrl_t *sgpGluLoginDlg_g;
#endif

// 0x66ADBC -- the ID listbox control, a `ctrl_t *`. gluLogin_listbox_ctrl_fcn stores its
// own ECX there and gluLogin_dlg_fcn reads it back as the ECX of gluLogin_new_id,
// gluLogin_delete_id and gluLogin_OK, and dereferences it at +0x36, which dlg_lbox.cpp
// shows is the companion scrollbar.
#ifdef OC_DLL_MODE
#define sgpGluLoginListbox_g (*reinterpret_cast<ctrl_t **>(OC_VA(0x66ADBCu)))
#else
extern ctrl_t *sgpGluLoginListbox_g;
#endif

// 0x66ADC0 -- the number of IDs enumerated so far. `mov dword ptr [0x66ADC0],
// 0h` in gluLogin_dlg_activate, `inc dword ptr [0x66ADC0]` in
// gluLogin_retrieve_character, and compared `cmp dword ptr [0x66ADC0], 0FAh` /
// `jnb` -- an unsigned 4-byte compare against 250.
#ifdef OC_DLL_MODE
#define sgnNumLogins_g (*reinterpret_cast<DWORD *>(OC_VA(0x66ADC0u)))
#else
extern DWORD sgnNumLogins_g;
#endif

// 0x508194 -- the screen's animation and transition descriptor table, handed to
// glues_transition_in in EDX by gluLogin_dlg_fcn. Nothing here dereferences it, so only
// the address is derived, and the 12-byte extent is the span to the next named global at
// 0x5081A0 rather than anything an instruction shows.
#ifdef OC_DLL_MODE
#define sLoginAnims_g (reinterpret_cast<BYTE *>(OC_VA(0x508194u)))
#else
extern BYTE sLoginAnims_g[12];
#endif

// 0x5081AC -- the screen's per-control event-function table, passed to
// glue_init_screen_dlg with a size of 0x24, so nine entries. The contents agree: the
// table reads 0, 0, 0, four copies of 0x4C83B0, 0x45A590 and 0x4C1860, and 0x45A590 is
// this TU's gluLogin_listbox_ctrl_fcn. An array rather than a pointer, since the oracle
// materialises its address and never loads from it. It sits 12 bytes past gIDList_g and
// is a global in its own right, not that list's end pointer.
#ifdef OC_DLL_MODE
#define sLoginEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x5081ACu)))
#else
extern PFN_CTRL_EVT sLoginEvtFcnTbl_g[9] OC_UNALIGNED;
#endif

// 0x6CE6DC gnDlgReturnVal, a long. The inner guard keeps this spelling compatible with
// the generated one, which five other TUs also declare locally. gluLogin_dlg_fcn reads
// it as the modal result of the popup it just ran, a whole-dword load.
#ifdef OC_DLL_MODE
  #ifndef gnDlgReturnVal
  #define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
  #endif
#else
extern LONG gnDlgReturnVal;
#endif

// 0x510850 -- the MSVC RTTI Type Descriptor for `struct _ID`, reached exactly
// as gametype.cpp reaches the TTEMPLATE one: `mov ecx, 510850h` /
// `call type_info::raw_name`, feeding SMemAlloc's owner tag. `typeid(_ID)`
// cannot be used -- the compiler would emit its own descriptor at an address we
// do not control, and the shipped form could never bake 0x510850. Spelled with
// the same gTypeInfo_ prefix the tree already uses for 0x5107BC.
#ifdef OC_DLL_MODE
#define gTypeInfo__ID (*reinterpret_cast<type_info *>(OC_VA(0x510850u)))
#else
extern type_info gTypeInfo__ID;
#endif

// lbox_insert_item is declared in src117/h/dlg_lbox.h and gluLogin_dlg_fcn in
// src117/h/gluLogin.h, so neither is repeated here.

// 0x4BE950, the ICF-folded one-byte `ret`. gluLogin_dlg_activate and gluLogin_dlg_fcn
// call it with nothing set up in ECX, EDX or on the stack, so the name at these sites
// took no arguments.
void nullsub_1();

// The list accessors. src117/h/TSList.h sets the terminator convention: the last node's
// `next` holds ~(&list.link.prev), a large negative number, so a walk ends on a signed
// `<= 0` and never on `== NULL`. The walks here normalise that sentinel to NULL before
// using it, which the oracle shows as either two branches off one flag set (a `jle` out
// for the sentinel and a `jne` back for the loop test) or a `cmovle`; which one MSVC
// picks is its own scheduling, and the source is this accessor both times.
static _ID *IdHead(void) {
  _ID *p = gIDList_g.link.next;
  if ((intptr_t)p <= 0) {
    p = 0;
  }
  return p;
}

static _ID *IdNext(_ID *p) {
  _ID *q = p->link.next;
  if ((intptr_t)q <= 0) {
    q = 0;
  }
  return q;
}

// The bodies, in oracle VA order.

// 0x45A240 gluLogin_OK (59 B) -- the listbox's selected row names the character to log in
// as: walk that many nodes down gIDList_g and publish the name.
//
// The control's +0x48 is the listbox selection index, a byte with 0xFF for nothing
// selected, as dlg_lbox.cpp's own lbox_t asserts. That struct is local to that TU, so the
// field is reached by offset here rather than by duplicating a 0x56-byte layout.
BOOL gluLogin_OK(void *pArg0) {
  int i = CTRL_LBOX_SEL(static_cast<ctrl_t *>(pArg0));  // Hyperion: was [0x48]
  _ID *p;

  if (i == 0xFF) {
    return FALSE;
  }
  p = gIDList_g.link.next;
  if ((intptr_t)p <= 0) {
    return FALSE;
  }
  while (p != 0) {
    if (i-- == 0) {
      break;
    }
    p = p->link.next;
    if ((intptr_t)p <= 0) {
      return FALSE;
    }
  }
  if (p == 0) {
    return FALSE;
  }
  SetLocalPlayerName(p->rec.szName);
  PortFrontProfileChosen(p->rec.szName);  // Hyperion: remembered (port_front.cpp)
  return TRUE;
}

// 0x45A280 gluLogin_listbox_ctrl_select (148 B) -- the listbox selection changed: enable
// or disable the two buttons that act on the selected id, indices 7 and 4 of the login
// dialog, then walk that many nodes down the list. The walk's result goes unused in the
// oracle too; it is reproduced rather than repaired.
//
// This walk uses the CMOV spelling of the terminator normalisation that
// src117/h/TSList.h names as the second form -- `xor ecx, ecx / test eax, eax /
// cmovle eax, ecx` -- where gluLogin_OK's uses branches. Same source shape,
// different scheduling decision by MSVC.
void gluLogin_listbox_ctrl_select(void *pArg0) {
  int nSel = CTRL_LBOX_SEL(static_cast<ctrl_t *>(pArg0));  // Hyperion: was [0x48]
  _ID *p;
  int i;

  if (nSel == 0xFF || IdHead() == 0) {
    ctrl_gray(ctrl_find_ndx(sgpGluLoginDlg_g, 7));
    ctrl_gray(ctrl_find_ndx(sgpGluLoginDlg_g, 4));
    return;
  }
  ctrl_ungray(ctrl_find_ndx(sgpGluLoginDlg_g, 7));
  ctrl_ungray(ctrl_find_ndx(sgpGluLoginDlg_g, 4));

  p = IdHead();
  for (i = nSel; i != 0; i--) {
    p = IdNext(p);
  }
}

// 0x45A620 gluLogin_listbox_ctrl_activate (110 B) -- the listbox is activated again: grey
// both action buttons, suspend layout, refill the box from gIDList_g, resume layout and
// select row 0. The walk is gluLogin_OK's, two branches off one flag set.
void gluLogin_listbox_ctrl_activate(intptr_t nArg0) {
  lbox_t *const pBox = reinterpret_cast<lbox_t *>(nArg0);
  _ID *p;

  ctrl_gray(ctrl_find_ndx(sgpGluLoginDlg_g, 7));
  ctrl_gray(ctrl_find_ndx(sgpGluLoginDlg_g, 4));
  lbox_set_recalc(pBox, 0);

  p = gIDList_g.link.next;
  if ((intptr_t)p > 0) {
    while (p != 0) {
      lbox_add_item(reinterpret_cast<ctrl_t *>(pBox), p->rec.szName, 0);
      p = p->link.next;
      if ((intptr_t)p <= 0) {
        break;
      }
    }
  }

  lbox_set_recalc(pBox, 1);
  lbox_set_selection(pBox, 0);
}

// 0x45A1D0 gluLogin_retrieve_character (107 B) -- the per-file callback
// EnumCharacterFiles drives; gluLogin_dlg_activate hands it over by address, so the call
// site inside EnumCharacterFiles decides the arity, and it pushes exactly one record.
//
// Allocate a node, tag the allocation with `struct _ID`'s RTTI name the way
// every Storm allocation in this tree does, copy the whole 0x70-byte record in
// and link it. The `test eax,eax / jz / mov [eax],0 / mov [eax+4],0 / jmp /
// xor eax,eax` shape is the same allocate-then-zero-the-link phi that
// src117/lang/gametype.cpp's CreateTemplate matches.
void __stdcall gluLogin_retrieve_character(void *pArg0) {
  const ID_REC *const pRec = reinterpret_cast<const ID_REC *>(pArg0);
  void *pv;
  _ID *pNode;

  if (sgnNumLogins_g >= 0xFA) {
    return;
  }
  if (pRec->bMultiPlayer != (BYTE)(gbMultiPlayerMode != 0)) {
    return;
  }

  pv = SMemAlloc(sizeof(_ID), gTypeInfo__ID.raw_name(), -2, 8);
  if (pv != 0) {
    pNode = reinterpret_cast<_ID *>(pv);
    pNode->link.prev = 0;
    pNode->link.next = 0;
  } else {
    pNode = 0;
  }
  pNode->rec = *pRec;
  gluLogin_insert_chara(pNode);
  ++sgnNumLogins_g;
}

// 0x45A590 gluLogin_listbox_ctrl_fcn (110 B) -- the id listbox's event handler: four
// events are intercepted and everything else goes to the stock list handler. The dispatch
// is two-level, a byte index table at 0x45A614 over dwId 0..0x0B selecting from five
// targets at 0x45A600 -- case 0 activate, case 1 remove-all, case 0x0A adopt, case 0x0B
// forward then reselect, 2..9 the default. Both tables live in this COMDAT's tail.
BOOL gluLogin_listbox_ctrl_fcn(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *const pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *const pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0x0A:
        pCtrl->dwFlags |= 0x20000;
        sgpGluLoginListbox_g = pCtrl;
        break;
      case 0:
        gluLogin_listbox_ctrl_activate(nArg0);
        break;
      case 0x0B:
        lbox_ctrl_list(pCtrl, pEvent);
        gluLogin_listbox_ctrl_select(pCtrl);
        return TRUE;
      case 1:
        lbox_remove_all(reinterpret_cast<lbox_t *>(pCtrl));
        break;
      default:
        break;
    }
  }
  return lbox_ctrl_list(pCtrl, pEvent);
}

// 0x536720 -- the expansion (Brood War) gate, unregistered and deliberately
// TU-local. Its only access shape in .text is `cmp byte ptr [0x536720], 0h`.
// Three of our own sources disagree on the NAME (src117/lang/maphdr.cpp:234
// records the conflict); the
// spelling used here is the tree's majority one -- src117/lang/CUnitUpgrade.cpp
// and src117/lang/gametype.cpp both already say gbIsExpansion -- so nothing is
// coined and no registry row is implied.
#ifdef OC_DLL_MODE
#define gbIsExpansion (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE gbIsExpansion;
#endif

// 0x459F10 do_login_dlg (207 B) -- the login screen's modal loop, in do_gluChat_dlg's
// shape: load, run modal, translate the modal result into glGluesMode, tail-jump
// glues_clear_all.
//
// The `xor cl, cl` before `call net_exit` is an argument the callee ignores. net_exit
// @0x477F60 reads no register, and a function that ignores a register parameter is
// byte-for-byte the same as one without it, so this call site is the only authority and
// it states one byte-wide argument. src117/h/net_player.h declares it nullary, right for
// the callee, so the real symbol is reached through a cast here rather than redeclared.
void do_login_dlg() {
  int nResult;

  // Hyperion: Hyperion's front end passes the screen with the profile chosen last,
  // after a choice on its home screen (port_front.cpp); its OK's result follows.
  if (gbMultiPlayerMode == 0 && PortFrontPassProfile()) {
    nResult = 4;
  } else {
    sgpGluLoginDlg_g = dlg_load("rez\\gluLogin.bin",
                                reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                                reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                                "gluLogin.cpp", 0x1C0);

    nResult = dlg_run_modal(sgpGluLoginDlg_g, 0,
                            reinterpret_cast<PFN_CTRL_EVT>(gluLogin_dlg_fcn));
  }

  switch (nResult) {
    case 4:
      if (gbMultiPlayerMode == 0) {
        glGluesMode = (gbIsExpansion != 0) ? 0x16 : 6;
        // Hyperion: Skirmish and Replays on Hyperion's home screen go on past the
        // campaign screen to the custom game screen (0x0B) and the replay list (0x0C)
        // (port_front.cpp).
        if (const char *pszNext = PortFrontAfterProfile())
          glGluesMode = pszNext[0] == 's' ? 0x0B : 0x0C;
      } else {
        switch (NetworkMode()) {
          case 'MDMX':
          case 'MODM':
            glGluesMode = 0x14;
            break;
          case 'SCBL':
            glGluesMode = 0x15;
            break;
          default:
            glGluesMode = 0x0A;
            break;
        }
      }
      break;
    case 5:
      reinterpret_cast<void(__fastcall *)(BYTE)>(net_exit)(0);
      glGluesMode = (gbMultiPlayerMode != 0) ? 2 : 0;
      break;
    default:
      glGluesMode = 0;
      break;
  }

  glues_clear_all();
}

// 0x45A320 gluLogin_new_id (391 B) -- the create-id button: prompt for a name, reject a
// bad filename and a duplicate, create the character file, wrap it in a node, link it and
// put it in the listbox at the position the link returned.
//
// The `lea ecx, dword ptr [eax+1Bh]` is MSVC using a known-zero EAX rather than an
// address computation -- that block is only reached when CreateNewCharacterFile returned
// 0 -- so the source writes glues_get_str(0x1B). The duplicate-name walk is
// gluLogin_OK's.
void gluLogin_new_id(intptr_t nArg0) {
  ctrl_t *const pBox = reinterpret_cast<ctrl_t *>(nArg0);
  _ID *pNode;
  char szName[0x19];
  ID_REC rec;
  void *pv;
  _ID *p;
  int nIndex;

  if (lbox_get_count(reinterpret_cast<lbox_t *>(pBox)) >= 0xFA) {
    do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x1A)));
    return;
  }

  szName[0] = 0;
  if (!do_pedit_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x17)),
                    reinterpret_cast<intptr_t>(szName), 0x19,
                    reinterpret_cast<intptr_t>(error_get_str(0x11)))) {
    return;
  }

  if (is_bad_filename(szName, 0x19, 0)) {
    do_pok_dlg(reinterpret_cast<intptr_t>(error_get_str(6)));
    return;
  }

  p = gIDList_g.link.next;
  if ((intptr_t)p > 0) {
    while (p != 0) {
      if (_mbsicmp(p->rec.szName, szName) == 0) {
        do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x19)));
        return;
      }
      p = p->link.next;
      if ((intptr_t)p <= 0) {
        break;
      }
    }
  }

  if (!CreateNewCharacterFile(szName, &rec)) {
    do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x1B)));
    return;
  }

  pv = SMemAlloc(sizeof(_ID), gTypeInfo__ID.raw_name(), -2, 8);
  if (pv != 0) {
    pNode = reinterpret_cast<_ID *>(pv);
    pNode->link.prev = 0;
    pNode->link.next = 0;
  } else {
    pNode = 0;
  }
  pNode->rec = rec;

  nIndex = gluLogin_insert_chara(pNode);
  lbox_insert_item(pBox, (BYTE)nIndex, pNode->rec.szName, 0);
  lbox_set_selection(reinterpret_cast<lbox_t *>(pBox), (BYTE)nIndex);
  ctrl_ungray(ctrl_find_ndx(sgpGluLoginDlg_g, 7));
  ctrl_ungray(ctrl_find_ndx(sgpGluLoginDlg_g, 4));
}



// 0x45A180 gluLogin_insert_chara (71 B) -- insert one node into gIDList_g in ascending
// sort-key order and return the ordinal it landed at, which is the listbox row index both
// callers then use.
//
// The loop's `jb` is unsigned, which is what makes ID_REC::dwSortKey a DWORD rather than a
// LONG. MSVC hoists the new node's key out of the loop by itself, the back edge targeting
// the compare, so the source reads the field in place rather than caching it.
int gluLogin_insert_chara(void *pArg0) {
  _ID *const pNew = reinterpret_cast<_ID *>(pArg0);
  int nIndex = 0;
  _ID *pBefore = 0;
  _ID *p = gIDList_g.link.next;

  while ((intptr_t)p > 0) {
    if (pNew->rec.dwSortKey < p->rec.dwSortKey) {
      pBefore = p;
      break;
    }
    p = p->link.next;
    ++nIndex;
  }

  gIDList_g.LinkNode(pNew, 2, pBefore);
  return nIndex;
}


// 0x45A4B0 gluLogin_delete_id (224 B) -- the delete-id button: walk to the selected node,
// confirm, delete the file, unlink the node and repair the selection. The walk is
// gluLogin_OK's, with one more `test/jz` because the loop can also fall out on p == 0.
//
// The control's +0x48 selection byte is read twice by the oracle, once for the counter
// and once after remove_character for the selection to restore, so the source reads it
// twice. The two `lbox_set_selection` tails are one source tail the compiler duplicated
// across the if and the else.
void gluLogin_delete_id(intptr_t nArg0) {
  ctrl_t *const pBox = reinterpret_cast<ctrl_t *>(nArg0);
  int i = CTRL_LBOX_SEL(pBox);  // Hyperion: was [0x48]
  BYTE bSel;
  BYTE nCount;
  _ID *p;

  if (i == 0xFF) {
    return;
  }
  p = gIDList_g.link.next;
  if ((intptr_t)p <= 0) {
    return;
  }
  while (p != 0) {
    if (i-- == 0) {
      break;
    }
    p = p->link.next;
    if ((intptr_t)p <= 0) {
      return;
    }
  }
  if (p == 0) {
    return;
  }

  if (do_pok_cancel_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x18))) == 0) {
    return;
  }
  if (remove_character(reinterpret_cast<intptr_t>(p->rec.szName)) == 0) {
    return;
  }

  bSel = CTRL_LBOX_SEL(pBox);  // Hyperion: was [0x48]
  lbox_remove_current(reinterpret_cast<lbox_t *>(pBox));
  gIDList_g.DeleteNode(p);
  nCount = lbox_get_count(reinterpret_cast<lbox_t *>(pBox));

  if (nCount == 0) {
    bSel = 0xFF;
    ctrl_gray(ctrl_find_ndx(sgpGluLoginDlg_g, 7));
    ctrl_gray(ctrl_find_ndx(sgpGluLoginDlg_g, 4));
  } else if (bSel >= nCount - 1) {
    bSel = (BYTE)(nCount - 1);
  }

  lbox_set_selection(reinterpret_cast<lbox_t *>(pBox), bSel);
  gluLogin_listbox_ctrl_select(pBox);
}



// 0x459EA0 TSList<_ID,TSGetLink<_ID> >::DeleteNode (103 B). The successor is read with
// the cmov spelling of the terminator normalisation, where IdHead and IdNext above use
// the branch spelling; both appear in this one TU and are not interchangeable.
static void UnlinkId(_ID *node) {
  TSLink<_ID> *pl = &node->link;
  _ID *pprev = pl->prev;
  if (pprev != NULL) {
    _ID *pnext = pl->next;
    TSLink<_ID> *pnl;
    if ((intptr_t)pnext <= 0) {
      // the terminator: the complement IS the address of the list's own link
      pnl = reinterpret_cast<TSLink<_ID> *>(~(intptr_t)pnext);
    } else {
      // The redundant guard has to stay, and is gametype.cpp's spelling: without it
      // MSVC reassociates `pnext + (pl - prevnext)` into `(pnext - prevnext) + pl`,
      // one instruction shorter than the oracle.
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0) {
        nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
      }
      pnl = reinterpret_cast<TSLink<_ID> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    pl->prev->link.next = pl->next;
    pl->prev = NULL;
    pl->next = NULL;
  }
}

// 0x459E40 (84 B). Unlinks and frees every id node. Normalising the head to
// null before the test keeps the loop unrotated, as in gametype's TTEMPLATE list.
void IDLIST::Clear(void) {
  for (;;) {
    _ID *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkId(p);
    SMemFree(p, gTypeInfo__ID.raw_name(), -2, 0);
  }
}

// The ledger spelled this row `TSList<_ID,0>::DeleteNode(_ID *)` until the
// 2026-09-23 rename, which is what let the gate and gen resolve it.
_ID *IDLIST::DeleteNode(_ID *pNode) {
  TSLink<_ID> *pl = (pNode != NULL) ? &pNode->link : &link;
  _ID *pnext = pl->next;
  if ((intptr_t)pnext <= 0) {
    pnext = 0;
  }
  UnlinkId(pNode);
  SMemFree(pNode, gTypeInfo__ID.raw_name(), -2, 0);
  return pnext;
}

// The id list's instantiation of 0x440C10, which has no ledger row of its own.
// The oracle's copy is the session list's, folded by the linker, so the call in
// gluLogin_insert_chara stays a call: noinline, or MSVC inlines this definition.
// ListLinkNode is TSList.h's.
__declspec(noinline) void IDLIST::LinkNode(_ID *pNode, ULONG dwWhere, _ID *pBefore) {
  ListLinkNode(this, pNode, dwWhere, pBefore);
}


// 0x45A000 gluLogin_dlg_activate (57 B) -- screen activation: reset the counter,
// re-enumerate the character files into the list, and, only if the list came back empty,
// arm timer 0x57, which gluLogin_dlg_fcn later turns into an automatic "create your first
// ID" prompt. The sentinel test is IdHead()'s branch form, as in gluLogin_OK.
void gluLogin_dlg_activate(intptr_t nArg0) {
  ctrl_t *const pDlg = reinterpret_cast<ctrl_t *>(nArg0);

  nullsub_1();
  sgnNumLogins_g = 0;
  EnumCharacterFiles(gluLogin_retrieve_character);

  if (IdHead() == 0) {
    timer_create(pDlg, 0x57, 0x32, 0);
  }
}


// 0x45A040 gluLogin_dlg_fcn (282 B) -- the screen's dialog procedure: an outer switch on
// dlgevt_t::wTag and, for wTag 0x0E, a two-level jump table over dwId 0..10, a byte index
// at 0x45A174 selecting one of six targets at 0x45A15C.
//
// It returns BOOL, not `void`: the body materialises a value on every path and a `void`
// body does not emit a dead immediate. It is a PFN_CTRL_EVT -- do_login_dlg hands its
// address to dlg_run_modal, and dialog.h types that slot
// `int(__fastcall *)(ctrl_t *, dlgevt_t *)` -- so the caller side cannot show it, the
// call being indirect. The argument spelling stays the header's `int, int`; ECX is the
// dialog and EDX the event, and both casts are free.
BOOL gluLogin_dlg_fcn(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *const pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *const pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  switch (pEvent->wTag) {
    // 0x11 and 0x12 scroll the id listbox, both forwarding to its companion control at
    // ctrl_t +0x36, the per-type payload slot, reached by offset rather than named for
    // every ctrl_t.
    case 0x11:
      ctrl_send_msg(*reinterpret_cast<ctrl_t **>(sgpGluLoginListbox_g->_unmapped36),
                    0x0F, 0);  // Hyperion: was + 0x36
      return TRUE;

    case 0x12:
      ctrl_send_msg(*reinterpret_cast<ctrl_t **>(sgpGluLoginListbox_g->_unmapped36),
                    0x10, 0);  // Hyperion: was + 0x36
      return TRUE;

    case 0x0E:
      switch (pEvent->dwId) {
        case 10:
          glue_init_screen_dlg(pDlg, sLoginEvtFcnTbl_g, 0x24, 0);
          glues_transition_in(reinterpret_cast<intptr_t>(pDlg), sLoginAnims_g,
                              3, 1, 0);
          break;

        case 0:
          gluLogin_dlg_activate(reinterpret_cast<intptr_t>(pDlg));
          glue_screen_activate(pDlg);
          break;

        case 2:
          switch (gnDlgReturnVal) {
            case 7:
              gluLogin_delete_id(reinterpret_cast<intptr_t>(sgpGluLoginListbox_g));
              return TRUE;
            case 4:
              if (gluLogin_OK(sgpGluLoginListbox_g) == 0) {
                return TRUE;
              }
              break;
            case 6:
              gluLogin_new_id(reinterpret_cast<intptr_t>(sgpGluLoginListbox_g));
              return TRUE;
          }
          return glues_transition_out(pDlg);

        case 8:
          if (pEvent->dwParam != 0x57) {
            break;
          }
          timer_remove_node(pDlg, 0x57);
          gluLogin_new_id(reinterpret_cast<intptr_t>(sgpGluLoginListbox_g));
          return TRUE;

        case 1:
          nullsub_1();
          gIDList_g.Clear();
          break;

        default:
          break;
      }
      break;

    default:
      break;
  }

  return ctrl_dlg(pDlg, pEvent);
}

