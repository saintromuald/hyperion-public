// tips.cpp -- the gameplay-tips dialog: its startup checkbox, its next-tip
// button, and the tip string table. Bodies are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle (WPO off) never does across a TU; that is what the
// __declspec(noinline) markers on some declarations are for, and why moving a
// body between TUs can change the code generated.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/tips.h"
#include "../h/dlg_btn.h"       // btn_ctrl_checkbox
#include "../h/dlg_ctrl.h"      // ctrl_set_val, ctrl_d26
#include "../h/dlg_draw.h"      // font_set, font_set_color_tbl, font_strlen
#include "../h/dlg_dlg.h"       // dlg_deactivate
#include "../h/glues.h"         // str_load_table, str_get_string, mem_free
#include "../h/menu_dlg.h"      // set_size_enter, set_btn_size
#include "../h/minimap.h"       // btn_ctrl_pushbutton
#include "../h/opt_dlgs.h"      // do_tips_dlg, dfl_load_bmp, option_dlg_cmd
#include "../h/save_dlg.h"      // modeless_dlg_startup, modeless_opt_dlg
#include "../gen/globals117.h"
#include "../port/front/port_gamemenu.h"  // Hyperion: PortTipsOpen and the rest

// 0x444890 -- the loader callback str_load_table takes in EDX, spelled as
// src117/lang/help.cpp and src117/lang/glues.cpp spell it; only its address is
// taken here.
void *load_game_data(const char *pszFile, void *pBuffer, DWORD *pdwSize,
                     const char *pszTag, int nLine);

// font_adjust -- oracle 0x4CDF20, spelled as src117/lang/dlg_draw.cpp and ten
// other TUs declare it, parameter names included, so that the spellings cannot
// drift apart.
const char * __fastcall font_adjust(int nX, int nY, const char *psz);

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// The options word, spelled as snd_dlg.cpp and spd_dlg.cpp spell it. Bit 8 is
// the show-tips-on-startup flag.
#ifdef OC_DLL_MODE
  #ifndef dword_6D3148
  #define dword_6D3148 (*reinterpret_cast<DWORD *>(OC_VA(0x6D3148u)))
  #endif
#else
extern DWORD dword_6D3148;
#endif

// 0x6C46E0 sgbStartupTip_g -- do_tips_dlg stores its incoming argument here
// before tail-jumping into the dialog.
#ifdef OC_DLL_MODE
  #ifndef sgbStartupTip_g
  #define sgbStartupTip_g (*reinterpret_cast<DWORD *>(OC_VA(0x6C46E0u)))
  #endif
#else
extern DWORD sgbStartupTip_g;
#endif

// 0x6C46DC sgpTipStrs -- the tip string table: str_load_table's result goes
// here and mem_free takes it back.
#ifndef OC_SGPTIPSTRS
#define OC_SGPTIPSTRS
#ifdef OC_DLL_MODE
#define sgpTipStrs (*reinterpret_cast<void **>(OC_VA(0x6C46DCu)))
#else
extern void *sgpTipStrs;
#endif
#endif

// 0x6D3144 -- the current tip number, which has no name of its own.
// A DWORD: every access is a full 32-bit load or store, and the two range
// checks use `jb`, so the value is unsigned.
#ifndef OC_DWORD_6D3144
#define OC_DWORD_6D3144
#ifdef OC_DLL_MODE
#define dword_6D3144 (*reinterpret_cast<DWORD *>(OC_VA(0x6D3144u)))
#else
extern DWORD dword_6D3144;
#endif
#endif

// 0x50DED8 sgnTipStrsCmpgn_g -- the first tip of each campaign mission,
// indexed by the mission number. WORD entries from
// `movzx eax, word ptr [eax*2+50DED8h]`; the extent is the gap to the next
// object at 0x50DF5C less its alignment padding, so 65 is derived, not proven.
#ifndef OC_SGNTIPSTRSCMPGN_G
#define OC_SGNTIPSTRSCMPGN_G
#ifdef OC_DLL_MODE
#define sgnTipStrsCmpgn_g (reinterpret_cast<WORD *>(OC_VA(0x50DED8u)))
#else
extern WORD sgnTipStrsCmpgn_g[65] OC_UNALIGNED;
#endif
#endif

// 0x50DF5C sgTipsEvtFcnTbl -- the dialog's event table. `push 0Ch` is the
// size in bytes and ctrl_set_evt_fcns indexes it by dword, so the extent is
// exactly 3.
#ifndef OC_SGTIPSEVTFCNTBL
#define OC_SGTIPSEVTFCNTBL
#ifdef OC_DLL_MODE
#define sgTipsEvtFcnTbl (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x50DF5Cu)))
#else
extern PFN_CTRL_EVT sgTipsEvtFcnTbl[3];
#endif
#endif

// 0x536720 -- the expansion (Brood War) gate. Its only access shape in .text
// is `cmp byte ptr [0x536720], 0h`.
#ifndef OC_GBISEXPANSION
#define OC_GBISEXPANSION
#ifdef OC_DLL_MODE
#define gbIsExpansion (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE gbIsExpansion;
#endif
#endif

// 0x526524 -- the current campaign mission number, read here as a WORD
// (`mov ax, word ptr [0x526524]`), spelled as gluBrfAct.cpp spells it.
#ifndef OC_WORD_526524_G
#define OC_WORD_526524_G
#ifdef OC_DLL_MODE
#define word_526524_g (*reinterpret_cast<WORD *>(OC_VA(0x526524u)))
#else
extern WORD word_526524_g;
#endif
#endif

// 0x671A81 gbDlgLevel -- the nesting level of the dialog stack, a BYTE: every
// access here is `inc byte ptr` or `mov al, byte ptr`.
#ifndef OC_GBDLGLEVEL
#define OC_GBDLGLEVEL
#ifdef OC_DLL_MODE
#define gbDlgLevel (*reinterpret_cast<BYTE *>(OC_VA(0x671A81u)))
#else
extern BYTE gbDlgLevel;
#endif
#endif

// 0x6D3034 gbFontLineExtra -- the extra leading between text lines, a BYTE;
// the write here is `mov byte ptr [0x6D3034], 0h`.
#ifndef OC_GBFONTLINEEXTRA
#define OC_GBFONTLINEEXTRA
#ifdef OC_DLL_MODE
#define gbFontLineExtra (*reinterpret_cast<BYTE *>(OC_VA(0x6D3034u)))
#else
extern BYTE gbFontLineExtra;
#endif
#endif

// 0x6D3037 gbFontMaxHgt -- the tallest glyph in the current font, a BYTE;
// set_tip_text reads it `movzx ecx, byte ptr [0x6D3037]`.
#ifndef OC_GBFONTMAXHGT
#define OC_GBFONTMAXHGT
#ifdef OC_DLL_MODE
#define gbFontMaxHgt (*reinterpret_cast<BYTE *>(OC_VA(0x6D3037u)))
#else
extern BYTE gbFontMaxHgt;
#endif
#endif

// ctrl_t +0x26, the DWORD each check box carries as the option bit it owns. It
// falls inside dialog.h's unnamed +0x24 window, the same slot filters.cpp names
// ctrl_d26 and snd_dlg.cpp reaches the same way.
// Hyperion: pointer-wide, since the dialog's holds option_dlg_alloc's block
//.
static __forceinline DWORD_PTR &ctrl_d26(ctrl_t *p) {
  return *reinterpret_cast<DWORD_PTR *>(&p->_unmapped24[2]);
}

// Hyperion: tips_dlg_init's choice of the tip it opens on, giving the step set_tip_text then
// takes, and set_tip_text's step through the product's tips, which Hyperion's game menu makes too
//.
static int PortTipFirst(void)
{
  DWORD dwTip;
  int nStep;

  nStep = 1;
  if (!gbMultiPlayerMode && word_526524_g != 0 && sgbStartupTip_g != 0) {
    nStep = 0;
    dword_6D3144 = sgnTipStrsCmpgn_g[word_526524_g];
  }
  dwTip = dword_6D3144;
  if (gbIsExpansion) {
    if (dwTip == 0 || dwTip >= 0x68)
      dword_6D3144 = 1;
  } else {
    if (dwTip == 0 || dwTip >= 0x51)
      dword_6D3144 = 1;
  }
  return nStep;
}

static void PortTipAdvance(int nTipIndex)
{
  DWORD dwTip;

  dwTip = dword_6D3144 + nTipIndex;
  dword_6D3144 = dwTip;
  if (gbIsExpansion) {
    if (dwTip >= 0x68)
      dword_6D3144 = 1;
    else if (dwTip == 0)
      dword_6D3144 = 0x67;
  } else {
    if (dwTip >= 0x51) {
      dword_6D3144 = 1;
    } else {
      if (dwTip == 0)
        dwTip = 0x50;
      dword_6D3144 = dwTip;
    }
  }
}

// The tips of Hyperion's game menu: rez\tips.tbl, held while the menu's page shows, and Show
// Tips at Startup, the options' bit tips_checkbox_fcn flips.
static void *s_pPortTips;

bool PortTipsStartup(void)
{
  return sgbStartupTip_g != 0;
}

void PortTipsOpen(bool bStartup)
{
  sgbStartupTip_g = bStartup;
  if (s_pPortTips == NULL)
    s_pPortTips = (void *)str_load_table(reinterpret_cast<intptr_t>("rez\\tips.tbl"),
                                         reinterpret_cast<intptr_t>(load_game_data),
                                         reinterpret_cast<intptr_t>("tips.cpp"), 0xEA);
  PortTipAdvance(PortTipFirst());
}

const char *PortTipsText(void)
{
  if (s_pPortTips == NULL)
    return "";
  return reinterpret_cast<const char *>(str_get_string(s_pPortTips, static_cast<WORD>(dword_6D3144)));
}

void PortTipsStep(int nDelta)
{
  PortTipAdvance(nDelta);
}

bool PortTipsAtStart(void)
{
  return (dword_6D3148 & 0x100) != 0;
}

void PortTipsAtStartToggle(void)
{
  dword_6D3148 ^= 0x100;
}

void PortTipsClose(void)
{
  if (s_pPortTips != NULL) {
    mem_free(s_pPortTips, "tips.cpp", 0x12A);
    s_pPortTips = NULL;
  }
}

// The bodies, in oracle VA order.

// Bring the tips dialog up modeless, remembering whether this is the startup
// showing. The tail jmp is what pins the return type.
void do_tips_dlg(int nStartup) {
  sgbStartupTip_g = nStartup;
  modeless_dlg_startup((void *)"rez\\tips_dlg.bin", (void *)&tips_dlg_fcn);
}

// The textbox's draw handler. Both offsets and the clip rect are ignored: the
// text is laid out at the control's own top-left with the control's rectangle
// as the font margin.
void tips_draw_textbox(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip) {
  if (pCtrl->pData) {
    // The call goes in every arm. Assigning to one variable and calling at the
    // join is 3 bytes shorter: MSVC if-converts the last two arms into a cmov.
    switch (pCtrl->dwFlags & 0x4C00) {
      case 0x400:  font_set(sgpFonts_g[0]); break;
      case 0x800:  font_set(sgpFonts_g[2]); break;
      case 0x4000: font_set(sgpFonts_g[3]); break;
      default:     font_set(sgpFonts_g[1]); break;
    }
    gbFontJust = 0x11;
    gbFontLineExtra = 0;
    gsFontMargins = pCtrl->rc;
    font_set_color_tbl(2);
    font_adjust(pCtrl->rc.left, pCtrl->rc.top, (const char *)pCtrl->pData);
    font_set(0);
  }
}

// Step the tip number by nDelta, wrap it inside the range the installed
// product allows, then load that tip into the dialog's textbox. The text size
// is picked by whether the string fits the box at the medium font.
void set_tip_text(ctrl_t *pCtrl, int nTipIndex) {
  ctrl_t *pText;
  DWORD dwTip;
  int nLen;
  int nFits;

  PortTipAdvance(nTipIndex);  // Hyperion: shared with Hyperion's game menu

  pText = ctrl_find_ndx(pCtrl, -10);
  dwTip = dword_6D3144;
  pText->pData = (void *)str_get_string(sgpTipStrs, (WORD)dwTip);
  font_set(sgpFonts_g[2]);
  nLen = (WORD)font_strlen((const char *)pText->pData);
  nFits = ((pText->rc.bottom - pText->rc.top) / gbFontMaxHgt - 1) *
          (pText->rc.right - pText->rc.left);
  if (nLen < nFits)
    ctrl_set_big_text(pText);
  else
    ctrl_set_med_text(pText);
  font_set(0);
  ctrl_set_update(pText);
}

// The control-event shape snd_dlg.cpp repeats: guard on the event tag, switch
// on the id, then tail-call the stock checkbox handler on every path, including
// the ones the switch does not take. The case-2 arm reaches that tail without
// reloading ECX/EDX because nothing between them clobbers the pair; the case-0
// arm calls ctrl_set_val and has to put them back.
int tips_checkbox_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        ctrl_d26(pCtrl) = 0x100;
        ctrl_set_val(pCtrl, (dword_6D3148 >> 8) & 1);
        break;
      case 2:
        dword_6D3148 ^= static_cast<DWORD>(ctrl_d26(pCtrl));
        break;
    }
  }
  return btn_ctrl_checkbox(pCtrl, pEvent);
}

// The tips dialog's proc, the same teardown shape help.cpp's help_dlg_fcn has:
// closing drops the dialog level by one, and by a second one when that did not
// reach zero, which sends the user back to the help menu.
int tips_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        tips_dlg_init(pCtrl);
        break;
      case 1:
        opt_dlg_generic_close(pCtrl);
        modeless_opt_dlg(pCtrl, pEvent);
        mem_free(sgpTipStrs, "tips.cpp", 0x12A);
        return 1;
      case 2:
        option_dlg_cmd(pCtrl);
        gbDlgLevel--;
        if (gbDlgLevel != 0) {
          gbDlgLevel--;
          do_helpmenu_dlg();
        } else {
          dlg_deactivate(pCtrl);
        }
        return 1;
    }
  }
  return modeless_opt_dlg(pCtrl, pEvent);
}

// Build the dialog: read the tip table, pick the opening tip, install the
// control handlers and the textbox's own draw routine. A single-player mission
// that was reached from the startup showing opens on that mission's first tip
// and does not step; everything else steps forward by one.
void tips_dlg_init(ctrl_t *pDlg) {
  ctrl_t *pText;
  int nStep;

  sgpTipStrs = (void *)str_load_table(reinterpret_cast<intptr_t>("rez\\tips.tbl"),
                                      reinterpret_cast<intptr_t>(load_game_data),
                                      reinterpret_cast<intptr_t>("tips.cpp"), 0xEA);
  nStep = PortTipFirst();  // Hyperion: shared with Hyperion's game menu

  dfl_load_bmp(pDlg, NULL);
  opt_dlg_set_blend(pDlg);
  ctrl_set_evt_fcns(pDlg, sgTipsEvtFcnTbl, 0x0C);
  ctrl_d26(pDlg) = static_cast<DWORD_PTR>(option_dlg_alloc());
  set_tip_text(pDlg, nStep);
  pText = ctrl_find_ndx(pDlg, -10);
  pText->pfnDraw = &tips_draw_textbox;
  set_size_enter(pDlg, 3, 0x20000000);
  gbDlgLevel++;
}

// The next-tip button. Event 0 sizes the button by which of the two it is;
// event 2 steps the tip forward on the right-hand button and back on the other.
int tips_next_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        // A select between two int constants keeps the frame index in a full
        // register. Spelling it as an addition narrows it to `add dx, 4`.
        set_btn_size(pCtrl, pCtrl->nIndex == 3 ? 4 : 5, 0x20000000);
        break;
      case 2:
        set_tip_text(pCtrl->pParent, (pCtrl->nIndex != 3) * 2 - 1);
        break;
    }
  }
  return btn_ctrl_pushbutton(pCtrl, pEvent);
}
