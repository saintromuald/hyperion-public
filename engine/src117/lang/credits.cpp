// credits.cpp -- the credits and epilog screens: the scrolling credit roll, its
// per-line tag parser and the two game states that drive it. Bodies are in
// oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>       // offsetof, for the table static_asserts
#include <stdlib.h>       // strtoul
#include <string.h>       // _strnicmp

#include "../h/credits.h"
#include "../h/dialog.h"    // ctrl_t, dlgevt_t, rect16_t, PFN_CTRL_*
#include "../h/bmp.h"       // bmp_t, bmp_destroy
#include "../h/dlg_ctrl.h"  // ctrl_find_ndx / hide / show / set_text / set_update / ctrl_dlg
#include "../h/dlg_dlg.h"   // dlg_deactivate, dlg_load, dlg_run_modal
#include "../h/dlg_draw.h"  // draw_ctrl_text
#include "../h/glues.h"     // vid_fade_in/out, alloc_load_bmp, mem_free
#include "../h/snd.h"       // load_game_data
#include "../h/net_cmds.h"  // vid_restore_palette
#include "../h/palette.h"   // set_font_palette
#include "../h/victory.h"   // psnprintf
#include "../h/Storm.h"     // SMemAlloc / SMemFree / SStrCopy (pulls <windows.h>)
#include "../h/gluRdy.h"    // stop_music, snd_play_music
#include "../h/music.h"     // music_volume_default
#include "../h/grid.h"      // IsBroodwar
#include "../gen/globals117.h"
// globals117.h comes last, so the TU-local arms below sit under every header.

// GetTickCount, imported from KERNEL32 and __declspec(dllimport) for 1.17's
// indirect call, spelled as glues.cpp and dlg_dlg.cpp spell it.
extern "C" __declspec(dllimport) ULONG __stdcall GetTickCount(void);

// The TU-local types.

// sgCreditsAlignments_g @0x4EBB70: six 8-byte entries, one per text control of
// the credits dialog, an id at +0 and its tag at +4. Only the id's low 16 bits
// are ever read, so its width is a guess.
struct credits_align_t {
  /*0x00*/ DWORD nIndex;
  /*0x04*/ const char *pszTag;
};
OC_X86_LAYOUT(sizeof(credits_align_t) == 0x08, "credits_align_t  stride: `add ecx, 8h` / `[esi*8+...]`");
static_assert(offsetof(credits_align_t, nIndex) == 0x00, "nIndex  mov dx, word ptr [esi*8+4EBB70h]");
OC_X86_LAYOUT(offsetof(credits_align_t, pszTag) == 0x04, "pszTag  push dword ptr [esi*8+4EBB74h]");

// The credit-page tables @0x4EBBA8 (campaign, 0x40 entries) and @0x4EBDA8
// (epilog, 1 entry): the same stride with the fields the other way round, and
// only wId's low word is read.
struct credits_page_t {
  /*0x00*/ const char *pszName;
  /*0x04*/ DWORD wId;
};
OC_X86_LAYOUT(sizeof(credits_page_t) == 0x08, "credits_page_t  stride: `add ecx, 8h`");
static_assert(offsetof(credits_page_t, pszName) == 0x00, "pszName  mov ecx, dword ptr [ecx]");
OC_X86_LAYOUT(offsetof(credits_page_t, wId) == 0x04, "wId      cmp word ptr [ecx+4h], dx");

// ctrl_bmp -- the bmp_t a ctrl_t embeds at +0x0C, spelled as dlg_draw.cpp
// spells it; a cast, since the slot is a union arm.
#define ctrl_bmp(p) (reinterpret_cast<bmp_t *>(&(p)->_unmapped0C[0]))

// The globals this TU reaches, dual-spelled, one #ifdef per name, all below the
// generated header. Extents are literals: no sizeof appears over them, since an
// array's dll-mode arm is a pointer.

// 0x4EBB70 sgCreditsAlignments_g.
#ifndef OC_SGCREDITSALIGNMENTS_G
#define OC_SGCREDITSALIGNMENTS_G
#ifdef OC_DLL_MODE
#define sgCreditsAlignments_g (reinterpret_cast<const credits_align_t *>(OC_VA(0x4EBB70u)))
#else
extern const credits_align_t sgCreditsAlignments_g[6];
#endif
#endif

// 0x4EBBA0 credit_justification: six font-justification bytes, one per text
// control id 1 to 6. 1.17's clamp lets id 0 read the byte before the table, a 0
// in its image; credits_fcn_draw_text spells that 0 out, since the table's
// neighbour changes layout at 64 bits.
#ifndef OC_CREDIT_JUSTIFICATION
#define OC_CREDIT_JUSTIFICATION
#ifdef OC_DLL_MODE
#define credit_justification (reinterpret_cast<const BYTE *>(OC_VA(0x4EBBA0u)))
#else
extern const BYTE credit_justification[6];
#endif
#endif

// 0x4EBBA8 campaign_credit_pages_g, 0x40 entries, ending where the epilog table
// begins.
#ifndef OC_CAMPAIGN_CREDIT_PAGES_G
#define OC_CAMPAIGN_CREDIT_PAGES_G
#ifdef OC_DLL_MODE
#define campaign_credit_pages_g (reinterpret_cast<const credits_page_t *>(OC_VA(0x4EBBA8u)))
#else
extern const credits_page_t campaign_credit_pages_g[0x40];
#endif
#endif

// 0x4EBDA8, the epilog table, one entry, under a name of our own.
#ifndef OC_EPILOG_CREDIT_PAGES
#define OC_EPILOG_CREDIT_PAGES
#ifdef OC_DLL_MODE
#define oc_EpilogCreditPages (reinterpret_cast<const credits_page_t *>(OC_VA(0x4EBDA8u)))
#else
extern const credits_page_t oc_EpilogCreditPages[1];
#endif
#endif

// 0x526524, the current campaign mission number, a WORD, under a name of our
// own.
#ifndef OC_WCAMPAIGNMISSION
#define OC_WCAMPAIGNMISSION
#ifdef OC_DLL_MODE
#define oc_wCampaignMission (*reinterpret_cast<WORD *>(OC_VA(0x526524u)))
#else
extern WORD oc_wCampaignMission;
#endif
#endif

// 0x5AEF0C dword_5AEF0C, the read cursor into the loaded .txt blob.
#ifndef OC_DWORD_5AEF0C
#define OC_DWORD_5AEF0C
#ifdef OC_DLL_MODE
#define dword_5AEF0C (*reinterpret_cast<char **>(OC_VA(0x5AEF0Cu)))
#else
extern char *dword_5AEF0C;
#endif
#endif

// 0x5AEF10 dword_5AEF10, the blob load_game_data returned, freed at the end of
// do_credits_dlg.
#ifndef OC_DWORD_5AEF10
#define OC_DWORD_5AEF10
#ifdef OC_DLL_MODE
#define dword_5AEF10 (*reinterpret_cast<void **>(OC_VA(0x5AEF10u)))
#else
extern void *dword_5AEF10;
#endif
#endif

// 0x5AEF14 rem_file_bytes_g, the bytes left in the blob. File-static in the
// plain form, which lets credits_tokenize keep it in a register across its
// stores through two out-pointers.
#ifndef OC_REM_FILE_BYTES_G
#define OC_REM_FILE_BYTES_G
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define rem_file_bytes_g (*reinterpret_cast<int *>(OC_VA(0x5AEF14u)))
#else
static int rem_file_bytes_g;
#endif
#endif

// 0x5AEF18 dword_5AEF18, the blob's size, load_game_data's out-parameter.
#ifndef OC_DWORD_5AEF18
#define OC_DWORD_5AEF18
#ifdef OC_DLL_MODE
#define dword_5AEF18 (*reinterpret_cast<DWORD *>(OC_VA(0x5AEF18u)))
#else
extern DWORD dword_5AEF18;
#endif
#endif

// 0x5AEF1C next_page_ticks_g, the GetTickCount deadline for the next page,
// signed.
#ifndef OC_NEXT_PAGE_TICKS_G
#define OC_NEXT_PAGE_TICKS_G
#ifdef OC_DLL_MODE
#define next_page_ticks_g (*reinterpret_cast<int *>(OC_VA(0x5AEF1Cu)))
#else
extern int next_page_ticks_g;
#endif
#endif

// 0x5AEF20 sgpCurrTextCtrl comes from the generated header.

// 0x5AEF24 byte_5AEF24, set by credits_edit_background and consumed by
// credits_end_page: the page changed the background, so fade it.
#ifndef OC_BYTE_5AEF24
#define OC_BYTE_5AEF24
#ifdef OC_DLL_MODE
#define byte_5AEF24 (*reinterpret_cast<BYTE *>(OC_VA(0x5AEF24u)))
#else
extern BYTE byte_5AEF24;
#endif
#endif

// 0x5AEF25 gCreditsFadespeed_g, a BYTE.
#ifndef OC_GCREDITSFADESPEED_G
#define OC_GCREDITSFADESPEED_G
#ifdef OC_DLL_MODE
#define gCreditsFadespeed_g (*reinterpret_cast<BYTE *>(OC_VA(0x5AEF25u)))
#else
extern BYTE gCreditsFadespeed_g;
#endif
#endif

// 0x5AEF28 gCreditsDisplaytime_g, milliseconds per page, 5000 by default.
#ifndef OC_GCREDITSDISPLAYTIME_G
#define OC_GCREDITSDISPLAYTIME_G
#ifdef OC_DLL_MODE
#define gCreditsDisplaytime_g (*reinterpret_cast<int *>(OC_VA(0x5AEF28u)))
#else
extern int gCreditsDisplaytime_g;
#endif
#endif

// 0x5AEF2C sgbCreditsKilled_g, set when the user aborts the roll; a DWORD
// despite the `sgb` prefix.
#ifndef OC_SGBCREDITSKILLED_G
#define OC_SGBCREDITSKILLED_G
#ifdef OC_DLL_MODE
#define sgbCreditsKilled_g (*reinterpret_cast<int *>(OC_VA(0x5AEF2Cu)))
#else
extern int sgbCreditsKilled_g;
#endif
#endif

// 0x665E60, read once to suppress the roll's auto-advance. It lies 8 bytes
// into event_Win.cpp's gKeyTbl but is spelled as its own scalar, as 1.17 bakes
// the flat address.
#ifndef OC_BYTE_665E60
#define OC_BYTE_665E60
#ifdef OC_DLL_MODE
#define oc_byte_665E60 (*reinterpret_cast<BYTE *>(OC_VA(0x665E60u)))
#else
extern BYTE oc_byte_665E60;
#endif
#endif

// 0x6D313C and 0x6D3140 are gOptions[3] and gOptions[4], spelled as
// src117/lang/music.cpp and src117/lang/gamesnd.cpp spell them.
#ifndef gOptions_music_on
#ifdef OC_DLL_MODE
#define gOptions_music_on (*reinterpret_cast<int *>(OC_VA(0x6D313Cu)))
#else
extern int gOptions_music_on;
#endif
#endif

#ifndef gOptions_sound_on
#ifdef OC_DLL_MODE
#define gOptions_sound_on (*reinterpret_cast<int *>(OC_VA(0x6D3140u)))
#else
extern int gOptions_sound_on;
#endif
#endif

// 0x536720, the expansion gate; unregistered, spelled as gametype.cpp does.
#ifndef gbIsExpansion
#ifdef OC_DLL_MODE
#define gbIsExpansion (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE gbIsExpansion;
#endif
#endif

// The bodies, in oracle VA order.

// 0x4148B0  credits_activate  (82 B)
// Re-arms the six credit text controls, walking the table backwards as 1.17
// does, and starts the first page.
BOOL credits_activate(ctrl_t *pDlg)
{
  const credits_align_t *p;
  ctrl_t *pCtrl;

  p = sgCreditsAlignments_g + 6;
  do
  {
    p--;
    pCtrl = ctrl_find_ndx(pDlg, (short)p->nIndex);
    if (pCtrl != NULL)
    {
      pCtrl->pData = NULL;
      pCtrl->pfnDraw = credits_fcn_draw_text;
    }
  } while (p != sgCreditsAlignments_g);

  gCreditsFadespeed_g = 1;
  gCreditsDisplaytime_g = 0x1388;
  return credits_forward(pDlg);
}

// 0x414910  credits_change  (127 B)
// Replaces the current text control's string. The dialog pointer is unused: the
// control it edits is sgpCurrTextCtrl, re-read after every call as in 1.17.
__declspec(noinline) void credits_change(ctrl_t *pDlg, const char *pszText, int nLen)
{
  ctrl_t *pCtrl;
  char *psz;

  (void)pDlg;
  if (sgpCurrTextCtrl == NULL)
    return;

  if (sgpCurrTextCtrl->pData != NULL)
  {
    SMemFree(sgpCurrTextCtrl->pData, "credits.cpp", 0x141, 0);
    // One load of the global feeds both the store and the call, as in 1.17.
    pCtrl = sgpCurrTextCtrl;
    pCtrl->pData = NULL;
    ctrl_hide(pCtrl);
  }

  if (nLen == 0)
    return;

  psz = (char *)SMemAlloc(nLen + 1, "credits.cpp", 0x147, 0);
  SStrCopy(psz, pszText, nLen + 1);
  ctrl_set_text(sgpCurrTextCtrl, psz);
  ctrl_show(sgpCurrTextCtrl);
}

// 0x414990  credits_deactivate  (81 B)
// The mirror of credits_activate: the same backwards walk, freeing each
// control's text, then the fade out.
void credits_deactivate(ctrl_t *pDlg)
{
  const credits_align_t *p;
  ctrl_t *pCtrl;

  p = sgCreditsAlignments_g + 6;
  do
  {
    p--;
    pCtrl = ctrl_find_ndx(pDlg, (short)p->nIndex);
    if (pCtrl != NULL && pCtrl->pData != NULL)
      SMemFree(pCtrl->pData, "credits.cpp", 0x236, 0);
  } while (p != sgCreditsAlignments_g);

  vid_fade_out(gCreditsFadespeed_g);
}

// 0x4149F0  credits_dlg_fcn  (120 B)
// The credits dialog's event function: keys, page advance, idle, and the
// activate/deactivate pair, with everything else passed on to ctrl_dlg.
// The inner dispatch is a switch (`sub eax, 0h`). Case 1 is written before
// case 0, and case 0 returns on its own. That keeps one copy of the
// `jmp ctrl_dlg` tail, placed after the deactivate arm.
int credits_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  int nRet = 1;
  WORD wKey;

  switch (pEvent->wTag)
  {
  case 14:
    switch (pEvent->dwId)
    {
    case 1:
      credits_deactivate(pCtrl);
      break;
    case 0:
      if (!credits_activate(pCtrl))
        return 0;
      break;
    }
    break;

  case 0:
    wKey = *reinterpret_cast<WORD *>(pEvent->_unmapped08);
    if (wKey == 0x20 || wKey == 0x0D)
    {
      nRet = credits_forward(pCtrl);
      break;
    }
    if (wKey != 0x1B)
      break;
    sgbCreditsKilled_g = 1;
    dlg_deactivate(pCtrl);
    return 0;

  case 4:
  case 7:
    nRet = credits_forward(pCtrl);
    break;

  case 13:
    nRet = credits_idle(pCtrl);
    break;
  }

  if (nRet == 0)
    return 0;
  return ctrl_dlg(pCtrl, pEvent);
}

// 0x414A90  credits_edit_background  (80 B)
// The `</BACKGROUND name>` tag. The name lies inside the loaded blob, so the
// closing '>' is overwritten with a NUL for the load and put back afterwards.
void credits_edit_background(ctrl_t *pCtrl, char *pszArg)
{
  char *p;

  if (ctrl_bmp(pCtrl)->pPixels != NULL)
    bmp_destroy(ctrl_bmp(pCtrl));

  p = pszArg;
  while (*p != '>')
    p++;

  *p = '\0';
  // glues.h spells alloc_load_bmp's third parameter as a DWORD; it is the
  // palette, hence the cast.
  alloc_load_bmp(pszArg, ctrl_bmp(pCtrl),
                 reinterpret_cast<DWORD_PTR>(sgGamePalette_g), "credits.cpp", 0xD5);
  *p = '>';
  byte_5AEF24 = 1;
}

// The `</DISPLAYTIME n>` tag: milliseconds per page, kept when the number is 0.
void credits_edit_displaytime(char *pszArg)
{
  char *p = pszArg;
  char *pszEnd;
  DWORD n;

  while (*p != '>')
    p++;
  *p = '\0';
  n = strtoul(pszArg, &pszEnd, 10);
  n = n ? n : gCreditsDisplaytime_g;
  *p = '>';
  gCreditsDisplaytime_g = n;
}

// The `</FADESPEED n>` tag, kept when the number is 0.
void credits_edit_fadespeed(char *pszArg)
{
  char *p = pszArg;
  char *pszEnd;
  DWORD n;

  while (*p != '>')
    p++;
  *p = '\0';
  n = strtoul(pszArg, &pszEnd, 10);
  {
    BYTE b = n ? (BYTE)n : gCreditsFadespeed_g;
    *p = '>';
    gCreditsFadespeed_g = b;
  }
}

// The `</FONTCOLOR name>` tag: loads a 0xC0-byte palette file into the font
// palette. The dialog in ECX is unused.
void credits_edit_fontcolor(ctrl_t *pCtrl, char *pszArg)
{
  BYTE abPal[0xC0];
  char *p = pszArg;

  (void)pCtrl;
  while (*p != '>')
    p++;
  *p = '\0';
  GameLoadImage((intptr_t)pszArg, 0, (intptr_t)abPal, 0xC0, 0, 0, 0);
  set_font_palette(abPal);
  *p = '>';
}

// 0x414BD0  credits_end_page  (70 B)
// The `</PAGE>` tag's fade, done only when the page changed the background: it
// consumes and clears the flag credits_edit_background sets.
void credits_end_page(ctrl_t *pCtrl)
{
  if (byte_5AEF24 == 0)
    return;

  {
    int nSpeed = gCreditsFadespeed_g;
    byte_5AEF24 = 0;
    vid_fade_out(nSpeed);
  }
  vid_restore_palette(0, 0x100, sgGamePalette_g);
  ctrl_set_update(pCtrl);
  vid_fade_in(gCreditsFadespeed_g);
}

// The draw handler credits_activate installs in each text control's +0x2E slot:
// it sets the font justification for the control's id and draws its text. The
// clamp is unsigned after a signed widening, as in 1.17, so a negative id takes
// the default.
void credits_fcn_draw_text(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  int nJust = pCtrl->nIndex;
  if ((unsigned)nJust <= 6u)
    gbFontJust = nJust == 0 ? 0 : credit_justification[nJust - 1];
  else
    gbFontJust = 0x11;
  pCtrl->dwFlags |= 0x80000000;
  draw_ctrl_text(pCtrl, 0, 0, NULL);
}

// Dispatches one `</...>` tag. Returns TRUE only for the end-of-page tag.
__declspec(noinline) BOOL credits_handle_tag(ctrl_t *pDlg, char *pszTag, int nLen)
{
  int i;

  if ((unsigned)nLen > 14 && _strnicmp(pszTag, "</BACKGROUND ", 13) == 0)
  {
    credits_edit_background(pDlg, pszTag + 13);
    return 0;
  }
  if ((unsigned)nLen > 13)
  {
    if (_strnicmp(pszTag, "</FONTCOLOR ", 12) == 0)
    {
      credits_edit_fontcolor(pDlg, pszTag + 12);
      return 0;
    }
    if (_strnicmp(pszTag, "</FADESPEED ", 12) == 0)
    {
      credits_edit_fadespeed(pszTag + 12);
      return 0;
    }
  }
  if ((unsigned)nLen > 15 && _strnicmp(pszTag, "</DISPLAYTIME ", 14) == 0)
  {
    credits_edit_displaytime(pszTag + 14);
    return 0;
  }
  if (_strnicmp(pszTag, "</PAGE>", nLen) == 0)
  {
    credits_end_page(pDlg);
    return 1;
  }

  // The `continue` keeps the loop test at the top; a nested `if` rotates it.
  i = 6;
  while (i-- != 0)
  {
    if (_strnicmp(pszTag, sgCreditsAlignments_g[i].pszTag, nLen) != 0)
      continue;
    if (sgpCurrTextCtrl != NULL)
      ctrl_hide(sgpCurrTextCtrl);
    sgpCurrTextCtrl = ctrl_find_ndx(pDlg, (short)sgCreditsAlignments_g[i].nIndex);
    return 0;
  }
  return 0;
}

// 0x414C60  credits_forward  (117 B)
// Pulls tokens out of the loaded .txt until a tag ends the page, then arms the
// next-page deadline. Returns FALSE when the file runs out, after deactivating
// the dialog; the empty-file guard goes to dlg_deactivate too.
BOOL credits_forward(ctrl_t *pDlg)
{
  char *pszToken;
  int bIsTag;
  int nLen;

  if (rem_file_bytes_g == 0)
  {
    dlg_deactivate(pDlg);
    return 0;
  }

  while ((nLen = credits_tokenize(&pszToken, &bIsTag)) != 0)
  {
    if (bIsTag == 0)
      credits_change(pDlg, pszToken, nLen);
    else if (credits_handle_tag(pDlg, pszToken, nLen))
    {
      next_page_ticks_g = (int)GetTickCount() + gCreditsDisplaytime_g;
      return 1;
    }
  }
  dlg_deactivate(pDlg);
  return 0;
}

// 0x414E20  credits_idle  (72 B)
// The per-frame tick. Game mode 2 means the game left the credits state, so the
// roll kills itself. The deadline test subtracts, which makes GetTickCount's
// wrap harmless.
BOOL credits_idle(ctrl_t *pDlg)
{
  if (gwGameMode == 2)
  {
    sgbCreditsKilled_g = 1;
    dlg_deactivate(pDlg);
    return 0;
  }

  if (oc_byte_665E60 == 0 && (int)(GetTickCount() - next_page_ticks_g) > 0)
    return credits_forward(pDlg);

  return 1;
}

// The credits game state: plays the credits track and runs the Brood War roll
// (when installed) and then the original one unless the user aborted.
void GameState_Credits()
{
  int nMusic = gOptions_music_on;

  if (nMusic == 0 && gOptions_sound_on != 0)
  {
    stop_song();
    music_volume_default();
    set_music_volume(0);
    gOptions_music_on = 0x32;
  }
  snd_play_music(5);
  sgbCreditsKilled_g = 0;
  if (IsBroodwar())
    do_credits_dlg("crdt_exp");
  if (sgbCreditsKilled_g == 0)
    do_credits_dlg("crdt_lst");
  stop_music();
  gOptions_music_on = nMusic;
}

// The epilog game state: the epilog text for the installed game, then after the
// final campaign mission (gwGameMode 9) the credits roll as well.
void GameState_Epilog()
{
  int nMusic = gOptions_music_on;

  if (nMusic == 0 && gOptions_sound_on != 0)
  {
    stop_song();
    music_volume_default();
    set_music_volume(0);
    gOptions_music_on = 0x32;
  }
  snd_play_music(9);
  if (gbIsExpansion != 0)
    do_credits_dlg("epilogX");
  else
    do_credits_dlg("epilog");
  if (gwGameMode == 9)
  {
    if (gbIsExpansion == 0)
      do_credits_dlg("crdt_lst");
    else
      do_credits_dlg("crdt_exp");
  }
  stop_music();
  gOptions_music_on = nMusic;
}

// 0x414F70  show_campaign_credits  (70 B)
// Looks the current mission up in campaign_credit_pages_g and runs its page,
// except in multiplayer and under one cheat bit.
void show_campaign_credits()
{
  // Declared in 1.17's order: the trip count, then the table pointer.
  int n = 0x40;
  const credits_page_t *p = campaign_credit_pages_g;
  WORD wMission;

  if (gbMultiPlayerMode != 0)
    return;
  if ((glCheatBits & 0x20000000) != 0)
    return;
  wMission = oc_wCampaignMission;
  if (wMission == 0)
    return;

  do
  {
    n--;
    if ((WORD)p->wId == wMission)
    {
      do_credits_dlg(p->pszName);
      return;
    }
    p++;
  } while (n != 0);
}

// 0x414FC0  show_epilog_credits  (70 B)
// show_campaign_credits over the one-entry epilog table, with the same loop.
void show_epilog_credits()
{
  // Declared in 1.17's order: the trip count, then the table pointer.
  int n = 1;
  const credits_page_t *p = oc_EpilogCreditPages;
  WORD wMission;

  if (gbMultiPlayerMode != 0)
    return;
  if ((glCheatBits & 0x20000000) != 0)
    return;
  wMission = oc_wCampaignMission;
  if (wMission == 0)
    return;

  do
  {
    n--;
    if ((WORD)p->wId == wMission)
    {
      do_credits_dlg(p->pszName);
      return;
    }
    p++;
  } while (n != 0);
}

// 0x415010  do_credits_dlg  (192 B)
// Loads `rez\<name>.txt` into the cursor globals, then runs the
// `rez\credits.bin` dialog modally with credits_dlg_fcn as its event function.
// Both path literals escape their backslash.
void do_credits_dlg(const char *pszName)
{
  char szPath[0x104];
  ctrl_t *pDlg;

  psnprintf(szPath, 0x104, "rez\\%s.txt", pszName);

  dword_5AEF10 = load_game_data(szPath, NULL, &dword_5AEF18, "credits.cpp", 0x265);
  dword_5AEF0C = (char *)dword_5AEF10;
  rem_file_bytes_g = (int)dword_5AEF18;
  sgbCreditsKilled_g = 0;

  pDlg = dlg_load("rez\\credits.bin",
                  reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                  reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                  "credits.cpp", 0x26A);
  dlg_run_modal(pDlg, 0, credits_dlg_fcn);

  mem_free(dword_5AEF10, "credits.cpp", 0x26F);
  dword_5AEF10 = NULL;
  dword_5AEF0C = NULL;
  sgpCurrTextCtrl = NULL;
}

// 0x4150D0  credits_tokenize  (189 B)
//
// Pulls the next token out of the loaded .txt and reports whether it was a
// tag; 0x1A counts as whitespace. The loop works on the globals directly. The
// two out-stores come before the local initialisers, or `*pbIsTag = 0` reuses
// a zero register and the loop head's padding changes.
__declspec(noinline) int credits_tokenize(char **ppszToken, int *pbIsTag)
{
  int nLen;
  int bBlank;
  char c;

  *pbIsTag = 0;
  *ppszToken = dword_5AEF0C;

  nLen = 0;
  bBlank = 1;

  while (rem_file_bytes_g != 0)
  {
    c = *dword_5AEF0C;
    if (c == '<')
    {
      if (nLen != 0 && bBlank == 0)
        return nLen;
      *ppszToken = dword_5AEF0C;
      nLen = 0;
      if (*pbIsTag != 0)
        return 0;
      *pbIsTag = 1;
    }
    else if (c == '>')
    {
      if (*pbIsTag == 0)
        return 0;
      rem_file_bytes_g--;
      dword_5AEF0C++;
      return nLen + 1;
    }
    else if (c != '\n' && c != '\r' && c != '\t' && c != ' ' && c != 0x1A)
    {
      bBlank = 0;
    }

    rem_file_bytes_g--;
    dword_5AEF0C++;
    nLen++;
  }

  if (*pbIsTag == 0 && bBlank == 0)
    return nLen;
  return 0;
}
