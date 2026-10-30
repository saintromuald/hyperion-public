// snd_dlg.cpp -- the sound options dialog, oracle 0x49ED20-0x49F060: the check
// boxes over the option word, the volume scroll bars, and the dialog handler.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/snd_dlg.h"
#include "../h/dlg_btn.h"
#include "../h/dlg_ctrl.h"
#include "../h/dlg_dlg.h"
#include "../h/dlg_lbox.h"
#include "../h/menu_dlg.h"
#include "../h/netdlg.h"
#include "../h/opt_dlgs.h"
#include "../h/save_dlg.h"
#include "../h/snd.h"
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x6D3148, gOptions + 0x18: the option bit field the sound check boxes toggle
// and read back, spelled as gameloop.cpp and gamesnd.cpp spell it.
#ifdef OC_DLL_MODE
  #ifndef dword_6D3148
  #define dword_6D3148 (*reinterpret_cast<DWORD *>(OC_VA(0x6D3148u)))
  #endif
#else
extern DWORD dword_6D3148;
#endif

// 0x6A61C8 gbSndInited, a BYTE.
#ifdef OC_DLL_MODE
  #ifndef gbSndInited
  #define gbSndInited (*reinterpret_cast<BYTE *>(OC_VA(0x6A61C8u)))
  #endif
#else
extern BYTE gbSndInited;
#endif

// 0x671A81 gbDlgLevel, a BYTE.
#ifdef OC_DLL_MODE
  #ifndef gbDlgLevel
  #define gbDlgLevel (*reinterpret_cast<BYTE *>(OC_VA(0x671A81u)))
  #endif
#else
extern BYTE gbDlgLevel;
#endif

// 0x6D3130 gOptions. Only its address is used here, as snd_set_options' one
// register argument.
#ifdef OC_DLL_MODE
  #ifndef gOptions
  #define gOptions (*reinterpret_cast<DWORD *>(OC_VA(0x6D3130u)))
  #endif
#else
extern DWORD gOptions;
#endif

// 0x6D313C gnMusicVolume and 0x6D3140 gnSfxVolume, gOptions + 0x0C and + 0x10,
// signed, and separate objects as snd.cpp and music.cpp spell them.
#ifdef OC_DLL_MODE
  #ifndef gnMusicVolume
  #define gnMusicVolume (*reinterpret_cast<int *>(OC_VA(0x6D313Cu)))
  #endif
#else
extern int gnMusicVolume;
#endif

#ifdef OC_DLL_MODE
  #ifndef gnSfxVolume
  #define gnSfxVolume (*reinterpret_cast<int *>(OC_VA(0x6D3140u)))
  #endif
#else
extern int gnSfxVolume;
#endif

// 0x4EFE3C, the dialog's .bin path "rez\snd_dlg.bin", 16 bytes.
#ifdef OC_DLL_MODE
  #ifndef sgszSndDlgFile_g
  #define sgszSndDlgFile_g (*reinterpret_cast<char (*)[16]>(OC_VA(0x4EFE3Cu)))
  #endif
#else
extern char sgszSndDlgFile_g[16] OC_UNALIGNED;
#endif

// 0x5093D8 sgSndEvtFcnTbl, the dialog's six event handlers.
#ifdef OC_DLL_MODE
  #ifndef sgSndEvtFcnTbl
  #define sgSndEvtFcnTbl (*reinterpret_cast<PFN_CTRL_EVT (*)[6]>(OC_VA(0x5093D8u)))
  #endif
#else
extern PFN_CTRL_EVT sgSndEvtFcnTbl[6];
#endif

// ctrl_t +0x26, the DWORD each check box carries as the option bit it owns. It
// falls inside dialog.h's unnamed +0x24 window, the same slot filters.cpp
// names ctrl_d26.
// Hyperion: pointer-wide, since the dialog's holds option_dlg_alloc's block
//.
static __forceinline DWORD_PTR &ctrl_d26(ctrl_t *p) {
  return *reinterpret_cast<DWORD_PTR *>(&p->_unmapped24[2]);
}

// Hyperion: the dialog's +0x26 slot holds option_dlg_alloc's block, an
// optdlg_save_t (opt_dlgs.cpp) with its countdown byte, bKind, at +0x26 on either
// width, where ctrl_t's +0x24 window is not at 64 bits.
static __forceinline BYTE &optdlg_kind(void *pSave) {
  return static_cast<BYTE *>(pSave)[0x26];
}


// The same slot read as a node pointer, which is what the dialog and the scroll
// bars keep there.
static __forceinline ctrl_t *&ctrl_p26(ctrl_t *p) {
  return *reinterpret_cast<ctrl_t **>(&p->_unmapped24[2]);
}

// ctrl_t +0x36, inside dialog.h's unnamed per-type payload. A scroll bar keeps
// the callback its parent runs when the bar moves.
typedef void(__fastcall *PFN_CTRL_NOTIFY)(ctrl_t *pCtrl);

static __forceinline PFN_CTRL_NOTIFY &ctrl_p36(ctrl_t *p) {
  return *reinterpret_cast<PFN_CTRL_NOTIFY *>(&p->_unmapped36[0]);
}

// The bodies, in oracle VA order.

// Bring the sound options dialog up modeless.
void do_sound_dlg() {
  modeless_dlg_startup(sgszSndDlgFile_g, (void *)&snd_dlg_fcn);
}

// Arm the dialog's redraw countdown, unless it is already running.
void set_update_timer(ctrl_t *pDlg) {
  ctrl_t *pNode = *reinterpret_cast<ctrl_t **>(&pDlg->_unmapped24[2]);
  if (optdlg_kind(pNode) == 0)
    optdlg_kind(pNode) = 5;
}

// One sound options check box. Event 2 flips this box's bit in the option word
// and arms the dialog's update timer; event 0 seeds the box from it.
BOOL snd_checkbox_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        snd_checkbox_init(pCtrl);
        break;
      case 2:
        dword_6D3148 ^= static_cast<DWORD>(ctrl_d26(pCtrl));
        set_update_timer(pCtrl->pParent);
        break;
    }
  }
  return btn_ctrl_checkbox(pCtrl, pEvent);
}

// Publish the option bit a check box owns, from its control index, then set the
// box from that bit. Three of the four boxes are greyed until the device is up,
// and the compiler tail-merges those three identical arms into one.
void snd_checkbox_init(ctrl_t *pCtrl) {
  switch (pCtrl->nIndex) {
    case 3:
      ctrl_d26(pCtrl) = 1;
      if (gbSndInited == 0)
        ctrl_gray(pCtrl);
      break;
    case 4:
      ctrl_d26(pCtrl) = 2;
      if (gbSndInited == 0)
        ctrl_gray(pCtrl);
      break;
    case 5:
      ctrl_d26(pCtrl) = 4;
      if (gbSndInited == 0)
        ctrl_gray(pCtrl);
      break;
    case 6:
      ctrl_d26(pCtrl) = 0x400;
      break;
  }
  ctrl_set_val(pCtrl, (ctrl_d26(pCtrl) & dword_6D3148) != 0);
}

// Load the backdrop, take a private copy of the option block, and wire Enter
// and Escape to controls 3 and 5.
void snd_dlg_activate(ctrl_t *pDlg) {
  dfl_load_bmp(pDlg, NULL);
  opt_dlg_set_blend(pDlg);
  ctrl_d26(pDlg) = static_cast<DWORD_PTR>(option_dlg_alloc());
  set_size_enter(pDlg, 3, 0x20000000);
  set_size_escape(pDlg, 5, 0x20000000);
  gbDlgLevel++;
}

// Bank both volumes, commit the option block, and drop a dialog level -- back
// to the options menu when that did not reach zero.
void snd_dlg_command(ctrl_t *pDlg) {
  gnMusicVolume = scroll_get_pos(ctrl_find_ndx(pDlg, 1));
  gnSfxVolume = scroll_get_pos(ctrl_find_ndx(pDlg, 2));
  option_dlg_cmd(pDlg);
  gbDlgLevel--;
  if (gbDlgLevel != 0) {
    gbDlgLevel--;
    do_optsmenu_dlg();
  } else {
    dlg_deactivate(pDlg);
  }
}

// The sound dialog's proc. Tag 0x0D is the redraw tick; under tag 0x0E the ids
// reach a two-level jump table over 0..10.
BOOL snd_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  switch (pEvent->wTag) {
    case 0x0E:
      switch (pEvent->dwId) {
        case 10:
          ctrl_set_evt_fcns(pCtrl, sgSndEvtFcnTbl, 0x18);
          break;
        case 0:
          snd_dlg_activate(pCtrl);
          break;
        case 2:
          snd_dlg_command(pCtrl);
          return TRUE;
        case 1:
          opt_dlg_generic_close(pCtrl);
          modeless_opt_dlg(pCtrl, pEvent);
          return TRUE;
      }
      break;
    case 0x0D:
      snd_dlg_timer(pCtrl);
      break;
  }
  return modeless_opt_dlg(pCtrl, pEvent);
}

// Run the redraw countdown the scroll bars arm, and push both volumes into the
// sound system on the tick it reaches zero.
void snd_dlg_timer(ctrl_t *pDlg) {
  ctrl_t *pNode = ctrl_p26(pDlg);

  if (optdlg_kind(pNode) != 0 && --optdlg_kind(pNode) == 0) {
    gnMusicVolume = scroll_get_pos(ctrl_find_ndx(pDlg, 1));
    gnSfxVolume = scroll_get_pos(ctrl_find_ndx(pDlg, 2));
    snd_set_options(&gOptions);
  }
}

// A volume scroll bar. Event 10 marks the dialog for redraw; event 0 seeds the
// bar.
void snd_scrollbar_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        snd_scrollbar_init(pCtrl);
        break;
      case 10:
        pCtrl->dwFlags |= 0x2000000;
        break;
    }
  }
  scroll_ctrl_scroll(pCtrl, pEvent);
}

// Seed a volume scroll bar from its option slot, grey it until the device is
// up, and give it the parent and the callback the redraw tick runs through.
// The two greying arms are identical and the compiler tail-merges them.
void snd_scrollbar_init(ctrl_t *pCtrl) {
  int nVal = 0;

  switch (pCtrl->nIndex) {
    case 1:
      nVal = gnMusicVolume;
      if (gbSndInited == 0)
        ctrl_gray(pCtrl);
      break;
    case 2:
      nVal = gnSfxVolume;
      if (gbSndInited == 0)
        ctrl_gray(pCtrl);
      break;
  }
  scroll_set_range(pCtrl, 0, 100);
  scroll_set_page_size(pCtrl, 10, 1);
  ctrl_set_val(pCtrl, nVal);
  ctrl_t *pParent = pCtrl->pParent;

  ctrl_p36(pCtrl) = set_update_timer;
  ctrl_p26(pCtrl) = pParent;
}
