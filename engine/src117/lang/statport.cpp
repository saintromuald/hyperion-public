// statport.cpp -- the status console unit portrait: the talking-head video,
// its dialog and its controls. Bodies are in oracle VA order.
//
// /Ob2 can splice a callee defined in this file into a caller here, where 1.17,
// built without WPO, keeps every cross-TU call; noinline on the declaration is
// the lever when such a callee has to live here.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>

#include "../h/platform.h"

#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/bmp.h"
#include "../h/dialog.h"
#include "../h/dlg_ctrl.h"
#include "../h/dlg_dlg.h"
#include "../h/gamesnd.h"
#include "../h/glues.h"
#include "../h/gluRdy.h"
#include "../h/minimap.h"
#include "../h/rect32.h"
#include "../h/statport.h"
#include "../h/victory.h"
#include "../h/Storm.h"
#include "../h/gds_bmpcop.h"
#include "../h/gds_bmpmas.h"

// One row of gPORTDATAArrayDataTbl_g. statport_init reads only the leading
// pointer and steps by 0x0C, so the other eight bytes stay unmapped.

#include "../gen/globals117.h"
#include "../h/gamedata.h"  // Hyperion: array_data_t
#include "../port/render/port_view.h"  // Hyperion: g_portView
#include "../port/render/port_console.h"  // Hyperion: PortConsoleRace, PortConsoleRebuilding
#include "../port/data/port_moddata.h"  // Hyperion: ModPortraitChance, a mod's portraits

// Callees no header of ours declares yet, each spelled exactly as the TU that
// owns it spells it: another spelling would be a different mangled name.

// 0x444890 -- the loader callback str_load_table and dlg_load take, from
// src117/lang/glues.cpp.
void *load_game_data(const char *pszFile, void *pBuffer, DWORD *pdwSize,
                     const char *pszTag, int nLine);

// 0x41D560 (15 B, ai_names.cpp). One dword in ECX, bounds-checked against 8,
// returns a dword out of the table at 0x5264FC.
int __fastcall get_slot_mapping(int nSlot);

// 0x446740 (24 B), from src117/lang/gamesnd.cpp. The result is read unsigned at
// the one call site here: the divide is `mul` plus `shr` and the caller
// branches with `jbe`, so statport_talk_timer casts.
int __fastcall gamesnd_duration(int nSoundId);

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// The per-type control event table ctrl_set_evt_fcns installs, 4 bytes = one
// entry: the dword at 0x4F0104 is 0x4A6D30, statport_portrait_ctrl_fcn.
#ifdef OC_DLL_MODE
#define sStatportEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x4F0104u)))
#else
extern PFN_CTRL_EVT sStatportEvtFcnTbl_g[1];
#endif

#ifdef OC_DLL_MODE
#define gPORTDATAArrayDataTbl_g (reinterpret_cast<array_data_t*>(OC_VA(0x501368u)))
#else
extern array_data_t gPORTDATAArrayDataTbl_g[7] OC_UNALIGNED;
#endif

// gamedata.cpp's race initials, "ztp".
#ifdef OC_DLL_MODE
#define gszRaceChars_g (reinterpret_cast<char *>(OC_VA(0x503358u)))
#else
extern char gszRaceChars_g[4];
#endif

// The per-race default portrait id, indexed by byte_5264C2. WORD table:
// statport_portrait_ctrl_fcn does `mov ax, word ptr [eax*2+50D7C0h]`. The
// extent is bounded by the next name, not proven.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define gwRacePort_g (reinterpret_cast<WORD *>(OC_VA(0x50D7C0u)))
#else
// Static: only this TU reads it, which lets a store to a control pass the load.
static WORD gwRacePort_g[4] = {0x26, 0x11, 0x3C, 0};
#endif

// The portrait playback option: 0 off, 1 still, 2 animated. set_playback_option
// clamps to 2 and stores it as a dword.
#ifdef OC_DLL_MODE
#define gPortPlayback_g (*reinterpret_cast<DWORD *>(OC_VA(0x50D7C8u)))
#else
extern DWORD gPortPlayback_g;
#endif

// The three-word generator state sub_4A6E00 steps. Each is a dword: every
// access is a full-width load or store.
#ifdef OC_DLL_MODE
#define dword_50D7CC (*reinterpret_cast<DWORD *>(OC_VA(0x50D7CCu)))
#else
extern DWORD dword_50D7CC;
#endif
#ifdef OC_DLL_MODE
#define dword_50D7D0 (*reinterpret_cast<DWORD *>(OC_VA(0x50D7D0u)))
#else
extern DWORD dword_50D7D0;
#endif
#ifdef OC_DLL_MODE
#define dword_50D7D4 (*reinterpret_cast<DWORD *>(OC_VA(0x50D7D4u)))
#else
extern DWORD dword_50D7D4;
#endif

// arr/portdata.cpp's two string columns, 110 entries each: statport_init
// rewrites a table index in place over a byte offset running 0 to 0x1B8 in
// steps of 4, and 0x1B8/4 is 110.
#ifdef OC_DLL_MODE
#define gszFidgetSmk (reinterpret_cast<char **>(OC_VA(0x51BD40u)))
#else
extern char *gszFidgetSmk[110];
#endif
#ifdef OC_DLL_MODE
#define gszTalkSmk (reinterpret_cast<char **>(OC_VA(0x51BEF8u)))
#else
extern char *gszTalkSmk[110];
#endif

#ifdef OC_DLL_MODE
#define portdata_idlesmkChange (reinterpret_cast<BYTE *>(OC_VA(0x51C0B0u)))
#else
extern BYTE portdata_idlesmkChange[110];
#endif

// arr/units.cpp's portrait id per unit type. WORD[228]: every reader is
// `movzx edx, word ptr [eax*2+523358h]` and 0x523358 + 228*2 is 0x523520, the
// next name in the sheet. Indices 78 and 152 are interior offsets of it
// (0x5233F4 and 0x523488), not globals of their own.
#ifdef OC_DLL_MODE
#define guwUnitPortrait (reinterpret_cast<WORD *>(OC_VA(0x523358u)))
#else
extern WORD guwUnitPortrait[228] OC_UNALIGNED;
#endif

// The campaign mission number, spelled as gluScore.cpp spells it. WORD: every
// reader is `cmp word ptr [0x526524], N`.
#ifdef OC_DLL_MODE
#define word_526524 (*reinterpret_cast<WORD *>(OC_VA(0x526524u)))
#else
extern WORD word_526524;
#endif

// The local player's race, spelled as banker.cpp spells it.
#ifdef OC_DLL_MODE
#define byte_5264C2 (*reinterpret_cast<BYTE *>(OC_VA(0x5264C2u)))
#else
extern BYTE byte_5264C2;
#endif

// The screen position a portrait event asks the map to jump to, seeded by
// statport_init and consumed by statport_dlg_char's cell_set_pos. Unnamed in
// every source, so spelled by address.
#ifdef OC_DLL_MODE
#define dword_527014 (*reinterpret_cast<int *>(OC_VA(0x527014u)))
#else
extern int dword_527014;
#endif
#ifdef OC_DLL_MODE
#define dword_527018 (*reinterpret_cast<int *>(OC_VA(0x527018u)))
#else
extern int dword_527018;
#endif

// A BYTE per player, spelled as gluScore.cpp spells it. It sits two bytes above
// the expansion flag at 0x536720, so it is not addressed from zero.
#ifdef OC_DLL_MODE
#define gbSlotRemap_g (reinterpret_cast<BYTE *>(OC_VA(0x536722u)))
#else
extern BYTE gbSlotRemap_g[8];
#endif

// arr\portdata.tbl, loaded by statport_init and freed by statport_deactivate.
#ifdef OC_DLL_MODE
#define g_portdata_strings (*reinterpret_cast<void **>(OC_VA(0x6AB388u)))
#else
extern void *g_portdata_strings;
#endif

// Set when a new portrait was claimed, cleared once the video has produced a
// frame. `cmp byte ptr [0x6AB38C], 0h` at every reader.
#ifdef OC_DLL_MODE
#define byte_6AB38C (*reinterpret_cast<BYTE *>(OC_VA(0x6AB38Cu)))
#else
extern BYTE byte_6AB38C;
#endif

// The Storm SMK handle. SVidPlayBegin takes its address, SVidPlayEnd its value.
#ifdef OC_DLL_MODE
#define sgpPortSmk_g (*reinterpret_cast<HANDLE *>(OC_VA(0x6AB390u)))
#else
extern HANDLE sgpPortSmk_g;
#endif

// A frame budget sub_4A6700 decrements; sub_4A6B40 and sub_4A6C50 reset it to 1
// per clip.
#ifdef OC_DLL_MODE
#define dword_6AB394 (*reinterpret_cast<DWORD *>(OC_VA(0x6AB394u)))
#else
extern DWORD dword_6AB394;
#endif

#ifdef OC_DLL_MODE
#define sgpPortDlg (*reinterpret_cast<ctrl_t **>(OC_VA(0x6AB398u)))
#else
extern ctrl_t *sgpPortDlg;
#endif

// The portrait control, cached when it claims itself on event id 0.
#ifdef OC_DLL_MODE
#define sgpStatPortCtrl (*reinterpret_cast<ctrl_t **>(OC_VA(0x6AB39Cu)))
#else
extern ctrl_t *sgpStatPortCtrl;
#endif

// statport_update's own dirty flag, and the still-portrait fade level next to
// it. Both BYTE: `cmp byte ptr [..], 0h` and `mov al, byte ptr [..]`.
#ifdef OC_DLL_MODE
#define sgbStatPortUpdate (*reinterpret_cast<BYTE *>(OC_VA(0x6AB3A0u)))
#else
extern BYTE sgbStatPortUpdate;
#endif
#ifdef OC_DLL_MODE
#define byte_6AB3A1 (*reinterpret_cast<BYTE *>(OC_VA(0x6AB3A1u)))
#else
extern BYTE byte_6AB3A1;
#endif

// The 8-byte bitmap the portrait video decodes into. Both halves are proven:
// statport_dlg_activate writes WORDs at +0 and +2 and hands 0x6AB3A4 to
// bmp_create, and sub_4A6C50 pushes [0x6AB3A8] as SVidPlayBegin's pixel base,
// which is bmp_t::pPixels at +4.
#ifdef OC_DLL_MODE
#define sgStatportSurface_g (*reinterpret_cast<bmp_t *>(OC_VA(0x6AB3A4u)))
#else
extern bmp_t sgStatportSurface_g;
#endif

// game\conover.pcx, the overlay masked on top of the portrait.
#ifdef OC_DLL_MODE
#define sgBmpConover_g (*reinterpret_cast<bmp_t *>(OC_VA(0x6AB3ACu)))
#else
extern bmp_t sgBmpConover_g;
#endif

// SVidPlayBegin's extent and rectangle arguments. statport_dlg_activate writes
// the four members of the second as left 0, top 0, right width-1, bottom
// height-1, which fixes it as a rect32_t; the first is left flat.
#ifdef OC_DLL_MODE
#define sgStatportSurfaceSize_g (reinterpret_cast<DWORD *>(OC_VA(0x6AB3B4u)))
#else
extern DWORD sgStatportSurfaceSize_g[2];
#endif
#ifdef OC_DLL_MODE
#define sgStatportSurfaceRect_g (*reinterpret_cast<rect32_t *>(OC_VA(0x6AB3BCu)))
#else
extern rect32_t sgStatportSurfaceRect_g;
#endif

// Non-zero while a talking-head clip owns the panel, so the per-frame update
// refreshes it from the control instead of from the selection.
#ifdef OC_DLL_MODE
#define dword_6AB3CC (*reinterpret_cast<DWORD *>(OC_VA(0x6AB3CCu)))
#else
extern DWORD dword_6AB3CC;
#endif

// Non-zero while a trigger owns the portrait; statport_trigger_reset clears it.
#ifdef OC_DLL_MODE
#define dword_6AB3D0 (*reinterpret_cast<DWORD *>(OC_VA(0x6AB3D0u)))
#else
extern DWORD dword_6AB3D0;
#endif

// Four consecutive portdata columns, collected by statport_init and indexed
// 0..3 by sub_4A6B40. The sheet names 0x6AB3D4 and then 0x6AB3E4, so the extent
// is exactly four pointers.
#ifdef OC_DLL_MODE
#define dword_6AB3D4 (reinterpret_cast<BYTE **>(OC_VA(0x6AB3D4u)))
#else
extern BYTE *dword_6AB3D4[4];
#endif

// The hotkey string, taken off the index -10 control's +0x14 text pointer.
#ifdef OC_DLL_MODE
#define sgpszStatportHotkeys_g (*reinterpret_cast<char **>(OC_VA(0x6AB3E4u)))
#else
extern char *sgpszStatportHotkeys_g;
#endif

// The sound id statport_talk_timer asks gamesnd_duration for a duration in milliseconds.
#ifdef OC_DLL_MODE
#define dword_6AB3E8 (*reinterpret_cast<DWORD *>(OC_VA(0x6AB3E8u)))
#else
extern DWORD dword_6AB3E8;
#endif

// The last talking-head variant played. Signed: sub_4A6C50 keeps it in BL, ends
// the loop on `test bl,bl / js`, and widens it with `movsx eax, bl`.
#ifdef OC_DLL_MODE
#define byte_6AB3EC (*reinterpret_cast<char *>(OC_VA(0x6AB3ECu)))
#else
extern char byte_6AB3EC;
#endif

// The TU's own owner tag: the .rdata string at 0x4F0108 that every allocation
// site pushes.
#define OC_STATPORT_FILE "statport.cpp"

// dialog.h leaves dlgevt_t +0x08 unmapped because it is a per-event payload.
// The character event reads it as a WORD virtual key.
#define dlgevt_key(p) (*reinterpret_cast<WORD *>(&(p)->_unmapped08[0]))

// The control's +0x24 user slot holds the portrait id and the unit being shown.
// dialog.h leaves 0x24..0x29 unmapped because no two subsystems agree on it, so
// each TU names its own view the way gluBrfPort.cpp does.
#define ctrl_port_id(p) (*reinterpret_cast<WORD *>(&(p)->_unmapped24[0]))
#define ctrl_port_unit(p) (*reinterpret_cast<CUnit **>(&(p)->_unmapped24[2]))

// CUnit +0x8A/+0x8B, inside CUnit.h's _unmapped_86[9]. sub_46E490 counts the
// first one down and reads the second as the blink colour, so this pair is the
// minimap blink the ping arms. The role is inferred from that one reader.
#define unit_blink_timer(p) ((p)->_unmapped_86[4])
#define unit_blink_player(p) ((p)->_unmapped_86[5])

// 0x4A65D0 get_unit_portrait (59 B). Map a unit type to its portrait, with two
// mission-gated substitutions. Declared in src117/h/gluRdy.h:149; that spelling
// is authoritative here. 0x5233F4 and 0x523488 are guwUnitPortrait[78] and
// guwUnitPortrait[152], not globals of their own. Keep the cast on the switch
// selector: it is the only spelling that keeps the oracle's `mov ecx, eax`, and
// sixteen other shapes coalesce it away and come out two bytes short.
int get_unit_portrait(WORD wUnitId) {
  int nPortrait = guwUnitPortrait[wUnitId];

  switch (static_cast<WORD>(nPortrait)) {
    case 0x26:
      if (word_526524 > 0x1F)
        nPortrait = guwUnitPortrait[152];
      break;
    case 0x3C:
      if (word_526524 >= 0x1C)
        nPortrait = guwUnitPortrait[78];
      break;
  }
  return nPortrait;
}

// 0x4A6610 sub_4A6610 (239 B). Sprinkles static over the current bitmap's
// portrait rectangle: each pixel draws 0..0x64 and, at or above the threshold,
// takes one of sixteen pens. The RNG state is in locals (the pixel store may
// alias, so the globals cost a 17-byte reload), the draws are unsigned (`jae`,
// not `jge`), and the row decrement sits above the column load for two
// registers.
void sub_4A6610(BYTE bThreshold, void *pArg1) {
  rect16_t *prc = reinterpret_cast<rect16_t *>(pArg1);
  bmp_t *pBmp = reinterpret_cast<bmp_t *>(_bmp_current);
  int nWidth = prc->right - prc->left + 1;
  int nRowAdvance = pBmp->nWidth - nWidth;
  BYTE *pDst = pBmp->pPixels + prc->top * pBmp->nWidth + prc->left;
  int nRows = prc->bottom - prc->top + 1;

  if (nRows != 0) {
    DWORD s0 = dword_50D7CC;
    DWORD s1 = dword_50D7D0;
    DWORD s2 = dword_50D7D4;

    do {
      int nCols;
      nRows--;
      nCols = nWidth;
      while (nCols != 0) {
        DWORD r;
        nCols--;
        do {
          s0 += s1;
          s1 += 0x71823402;
          s2 += s0;
          s0 >>= 1;
          s1 ^= s2;
          r = (s1 ^ s0) & 0x7F;
        } while (r >= 0x65);
        dword_50D7D0 = s1;
        dword_50D7CC = s0;
        dword_50D7D4 = s2;
        if (r >= bThreshold) {
          DWORD nPen;
          do {
            s0 += s1;
            s1 += 0x71823402;
            s2 += s0;
            s0 >>= 1;
            s1 ^= s2;
            nPen = (s1 ^ s0) & 0x0F;
          } while (nPen >= 0x10);
          dword_50D7D0 = s1;
          dword_50D7CC = s0;
          dword_50D7D4 = s2;
          *pDst = sgbPens_g[nPen];
        }
        pDst++;
      }
      pDst += nRowAdvance;
    } while (nRows != 0);
  }
}

// 0x4A6700 sub_4A6700 (94 B). Advance the portrait video by one frame. All
// three arguments are forwarded to Storm unchanged; nothing here reads a global
// handle.
int sub_4A6700(intptr_t nArg0, int nArg1, intptr_t nArg2) {
  HANDLE hVid = reinterpret_cast<HANDLE>(nArg0);
  DWORD dwArg1 = static_cast<DWORD>(nArg1);
  DWORD *pdwArg2 = reinterpret_cast<DWORD *>(nArg2);

  if (gPortPlayback_g == 0)
    return 0;
  if (gPortPlayback_g != 2 && dword_6AB394 == 0)
    return 1;
  if (!SVidPlayContinueSingle(hVid, dwArg1, pdwArg2))
    return 0;
  if (dword_6AB394 != 0 && pdwArg2 != NULL && *pdwArg2 != 0)
    dword_6AB394--;
  return 1;
}

// 0x4A6760 statport_dlg_char (57 B). Recentre the view when the key is one of
// the portrait's hotkeys. Reading the hotkey string before the key keeps the
// character in AL; the other order gives it EBX and costs the push and two
// pops.
BOOL statport_dlg_char(intptr_t nArg0, void *pArg1) {
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(pArg1);
  const char *psz = sgpszStatportHotkeys_g;
  WORD wKey = dlgevt_key(pEvent);

  while (*psz != '\0') {
    if (*psz == wKey) {
      cell_set_pos(dword_527014, dword_527018);
      return 1;
    }
    psz++;
  }
  return 0;
}

// 0x4A67A0 statport_dlg_activate (311 B). Builds the portrait surface from the
// control's rectangle, clears it, installs the per-type handlers, loads the
// overlay and caches the hotkey string; 635 and 651 are the line numbers 1.17
// pushes. The bottom is stored before the right so the height decrement is an
// in-place `dec eax`, and bPen is read between the two _bmp_current statements,
// where 1.17 loads CL.
void statport_dlg_activate(intptr_t nArg0) {
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  char szPath[0x104];
  ctrl_t *pCtrl;
  void *pSavedBmp;
  BYTE bPen;
  char cRace;
  int nWidth;
  int nHeight;

  pCtrl = ctrl_find_ndx(pDlg, 1);
  sgStatportSurface_g.nWidth = pCtrl->rc.right - pCtrl->rc.left + 1;
  nWidth = sgStatportSurface_g.nWidth;
  sgStatportSurfaceSize_g[0] = nWidth;
  sgStatportSurface_g.nHeight = pCtrl->rc.bottom - pCtrl->rc.top + 1;
  nHeight = sgStatportSurface_g.nHeight;
  sgStatportSurfaceSize_g[1] = nHeight;
  sgStatportSurfaceRect_g.left = 0;
  sgStatportSurfaceRect_g.top = 0;
  sgStatportSurfaceRect_g.bottom = nHeight - 1;
  sgStatportSurfaceRect_g.right = nWidth - 1;
  bmp_create(&sgStatportSurface_g, OC_STATPORT_FILE, 0x27B);

  pSavedBmp = _bmp_current;
  bPen = sgbPens_g[0x0F];
  _bmp_current = &sgStatportSurface_g;
  bmp_clear(bPen);
  _bmp_current = pSavedBmp;

  ctrl_set_evt_fcns(pDlg, sStatportEvtFcnTbl_g, 4);
  status_get_console_art(pDlg);

  if (gPlayback != 0)
    cRace = 'n';
  else
    cRace = gszRaceChars_g[PortConsoleRace()];  // Hyperion: Console skin's race
  psnprintf(szPath, 0x104, "%s%c%s", "game\\", cRace, "conover.pcx");

  sgBmpConover_g.pPixels = NULL;
  alloc_load_bmp(szPath, &sgBmpConover_g, 0, OC_STATPORT_FILE, 0x28B);

  sgpszStatportHotkeys_g =
      reinterpret_cast<char *>(ctrl_find_ndx(pDlg, -10)->pData);
}

// 0x4A68E0 statport_dlg_fcn (111 B). The dialog's event handler. The 0x0F case
// is written before 0x0E because the oracle lays its block out first, and the
// jump table records that order.
BOOL statport_dlg_fcn(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  switch (pEvent->wTag) {
    case 0:
    case 1:
    case 3:
      return 0;
    case 0x0F:
      return statport_dlg_char(nArg0, pEvent);
    case 0x0E:
      switch (pEvent->dwId) {
        case 0:
          statport_dlg_activate(nArg0);
          break;
        case 1:
          bmp_destroy(&sgBmpConover_g);
          bmp_destroy(&sgStatportSurface_g);
          break;
        case 4:
          return 1;
      }
      break;
  }
  return ctrl_dlg(pCtrl, pEvent);
}

// 0x4A6970 statport_portrait_ctrl_draw (73 B). Blit the decoded portrait, add
// static while the fade counter is low, then mask the console overlay over it.
// PFN_CTRL_DRAW's four-argument shape.
void statport_portrait_ctrl_draw(intptr_t nArg0, int nArg1, int nArg2, int nArg3) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);

  if (gPortPlayback_g != 0) {
    bmp_copy(static_cast<unsigned short>(pCtrl->rc.left),
             static_cast<unsigned short>(pCtrl->rc.top),
             &sgStatportSurface_g);
    if (byte_6AB3A1 < 0x50)
      sub_4A6610(byte_6AB3A1, &pCtrl->rc);
  }
  bmp_mask(static_cast<unsigned short>(pCtrl->rc.left),
           static_cast<unsigned short>(pCtrl->rc.top), &sgBmpConover_g);
}

// 0x4A69C0 request_statport_update (8 B). Mark the portrait for a refresh on
// the next frame.
void request_statport_update() {
  sgbStatPortUpdate = 1;
}

// 0x4A69D0 statport_deactivate (82 B). Stop the video, drop the dialog and free
// the portdata strings. Line 882 is the literal the oracle pushes. Reading the
// string block into a local first is what keeps the shipped form scheduled like
// the oracle.
void statport_deactivate() {
  if (sgpPortSmk_g != NULL) {
    SVidPlayEnd(sgpPortSmk_g);
    sgpPortSmk_g = NULL;
  }
  if (sgpPortDlg != NULL)
    dlg_deactivate(sgpPortDlg);
  void *pStrings = g_portdata_strings;
  sgpPortDlg = NULL;
  mem_free(pStrings, OC_STATPORT_FILE, 0x372);
  g_portdata_strings = NULL;
}

// 0x4A6A30 get_playback_option (6 B). The playback mode. A two-instruction
// leaf, so a match here is not evidence.
int get_playback_option() {
  return gPortPlayback_g;
}

// 0x4A6A40 statport_init (255 B). Resolve the portdata strings, find the four
// idle-weight columns and bring the dialog up. Lines 835 and 865 are the
// literals the oracle pushes. The scan stops on the row whose pointer is
// portdata_idlesmkChange and then takes that row and the next three, which is
// what fixes dword_6AB3D4 at four entries.
// Hyperion: portdata.dat's string ids, as the game loaded them, for a console built again
// in a game (port_console.cpp): the loop below turns them into pointers in place
static DWORD_PTR s_anFidgetIds[110], s_anTalkIds[110];

void statport_init() {
  array_data_t *pEnt;
  int i;

  // Hyperion: the ids back for a console built again, else kept
  for (i = 0; i < 110; i++) {
    if (PortConsoleRebuilding()) {
      gszFidgetSmk[i] = reinterpret_cast<char *>(s_anFidgetIds[i]);
      gszTalkSmk[i] = reinterpret_cast<char *>(s_anTalkIds[i]);
    } else {
      s_anFidgetIds[i] = reinterpret_cast<DWORD_PTR>(gszFidgetSmk[i]);
      s_anTalkIds[i] = reinterpret_cast<DWORD_PTR>(gszTalkSmk[i]);
    }
  }

  g_portdata_strings = reinterpret_cast<void *>(
      str_load_table(reinterpret_cast<intptr_t>("arr\\portdata.tbl"),
                     reinterpret_cast<intptr_t>(load_game_data),
                     reinterpret_cast<intptr_t>(OC_STATPORT_FILE), 0x343));
  for (i = 0; i < 110; i++) {
    gszFidgetSmk[i] = reinterpret_cast<char *>(str_get_string(
        g_portdata_strings,
        static_cast<WORD>(reinterpret_cast<DWORD_PTR>(gszFidgetSmk[i]))));
    gszTalkSmk[i] = reinterpret_cast<char *>(str_get_string(
        g_portdata_strings,
        static_cast<WORD>(reinterpret_cast<DWORD_PTR>(gszTalkSmk[i]))));
  }

  pEnt = gPORTDATAArrayDataTbl_g;
  while (pEnt->pDest != portdata_idlesmkChange)
    pEnt++;
  dword_6AB3D4[0] = reinterpret_cast<BYTE *>(pEnt[0].pDest);
  dword_6AB3D4[1] = reinterpret_cast<BYTE *>(pEnt[1].pDest);
  dword_6AB3D4[2] = reinterpret_cast<BYTE *>(pEnt[2].pDest);
  dword_6AB3D4[3] = reinterpret_cast<BYTE *>(pEnt[3].pDest);
  ModPortraitsResolved();  // Hyperion: the names a mod's edits give portraits

  dword_527014 = glScrollPixelX;
  dword_527018 = glScrollPixelY;
  dword_6AB3CC = 0;
  dword_6AB3D0 = 0;
  sgbStatPortUpdate = 1;

  sgpPortDlg = dlg_load("rez\\statport.bin",
                        reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                        reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                        OC_STATPORT_FILE, 0x361);
  dlg_activate(sgpPortDlg, 0, reinterpret_cast<PFN_CTRL_EVT>(statport_dlg_fcn));
}

// 0x4A6B40 sub_4A6B40 (257 B). Start an idle animation for the portrait: pick a
// weighted variant, then walk down the variants until one of the .smk files
// opens. The picker's loop condition is the variant bound, not the weight test.
// That leaves one `cmp dl, cl` and lets MSVC duplicate the post-loop clamp into
// the break edge; the weight test as the condition rotates the loop instead and
// loses nine bytes.
void sub_4A6B40(WORD wPortrait) {
  char szPath[0x104];
  signed char iVariant = 0;

  if (gPortPlayback_g == 2) {
    BYTE bDraw = sub_4A6E00(0, 100, 0x80);
    // Hyperion: a mod's portrait's chances are its record's (port_moddata.cpp)
    BYTE bAccum = ModPortraitChance(0, wPortrait, dword_6AB3D4);

    while (iVariant < 4) {
      if (bDraw <= bAccum)
        break;
      iVariant++;
      bAccum = static_cast<BYTE>(bAccum + ModPortraitChance(iVariant, wPortrait, dword_6AB3D4));
    }
    if (iVariant >= 4)
      iVariant = 0;
  }

  for (; iVariant >= 0; iVariant--) {
    if (sgpPortSmk_g != NULL)
      break;
    psnprintf(szPath, 0x104, "%s%s%d.smk", "portrait\\", gszFidgetSmk[wPortrait],
              iVariant);
    sgpPortSmk_g = NULL;
    if (gPortPlayback_g != 0) {
      dword_6AB394 = 1;
      SVidPlayBegin(szPath,
                    reinterpret_cast<DWORD_PTR>(sgStatportSurface_g.pPixels),
                    &sgStatportSurfaceRect_g, sgStatportSurfaceSize_g, 0,
                    0x21100000, &sgpPortSmk_g);
    }
  }
  ctrl_set_update(sgpStatPortCtrl);
}

// 0x4A6C50 sub_4A6C50 (219 B). Start the next talking-head clip. On the
// animated setting a variant is drawn at random and rerolled once if it repeats
// the last one; the loop then walks variants downwards until one of them opens.
//
// The counter is a signed char: the oracle keeps it in BL, guards the loop with
// `test bl,bl / js` and widens it with `movsx` for psnprintf.
void sub_4A6C50(WORD wPortrait) {
  char szPath[0x104];
  char i = 0;

  if (gPortPlayback_g == 2) {
    i = static_cast<char>(sub_4A6E00(0, 2, 4));
    if (i == byte_6AB3EC)
      i = static_cast<char>((i + 2) % 3);
  }

  for (; i >= 0; i--) {
    if (sgpPortSmk_g != NULL)
      break;
    psnprintf(szPath, 0x104, "%s%s%d.smk", "portrait\\", gszTalkSmk[wPortrait],
              i);
    sgpPortSmk_g = NULL;
    if (gPortPlayback_g != 0) {
      dword_6AB394 = 1;
      SVidPlayBegin(szPath,
                    reinterpret_cast<DWORD_PTR>(sgStatportSurface_g.pPixels),
                    &sgStatportSurfaceRect_g, sgStatportSurfaceSize_g, 0,
                    0x21100000, &sgpPortSmk_g);
    }
  }

  byte_6AB3EC = i;
  ctrl_set_update(sgpStatPortCtrl);
}

// 0x4A6D30 statport_portrait_ctrl_fcn (198 B). The portrait control's event
// handler: id 0 claims the control, id 1 releases it, id 2 centres the view on
// the shown unit, and anything else tail-jumps into the per-type handler. The
// race index is an int local read after the +0x26 store, the draw store
// between them; gwRacePort_g and sgpStatPortCtrl are static in the proof
// arm, or the draw store lands above timer_create's pushes.
BOOL statport_portrait_ctrl_fcn(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0: {
        ctrl_port_unit(pCtrl) = NULL;
        int nRace = byte_5264C2;
        pCtrl->pfnDraw =
            reinterpret_cast<PFN_CTRL_DRAW>(statport_portrait_ctrl_draw);
        ctrl_port_id(pCtrl) = gwRacePort_g[nRace];
        sgpStatPortCtrl = pCtrl;
        timer_create(pCtrl, 1, 0x32,
                     reinterpret_cast<PFN_TIMER_FN *>(statport_portrait_timer));
        timer_create(pCtrl, 2, 0x1E,
                     reinterpret_cast<PFN_TIMER_FN *>(statport_portrait_timer));
        return TRUE;
      }
      case 1:
        timer_remove_node(pCtrl, 1);
        timer_remove_node(pCtrl, 2);
        timer_remove_node(pCtrl, 3);
        timer_remove_node(pCtrl, 4);
        sgpStatPortCtrl = NULL;
        break;
      case 2:
        if (ctrl_port_unit(pCtrl) != NULL)
          ctrl_port_unit(pCtrl)->CenterView();
        break;
    }
  }
  return reinterpret_cast<PFN_CTRL_EVT>(gCtrlEvtFcnTbl[pCtrl->wType])(pCtrl,
                                                                      pEvent);
}

// 0x4A6E00 sub_4A6E00 (92 B). Draw a number in [nLow, nHigh] from the TU's own
// three-word generator by masking with nBucket-1 and rerolling until the value
// is inside the span. nMask declared between the 0x50D7CC and 0x50D7D4 loads
// gives the oracle's registers; the other declaration orders swap ESI and EDX.
BYTE sub_4A6E00(int nLow, int nHigh, int nBucket) {
  DWORD nSpan = nHigh - nLow + 1;
  DWORD a = dword_50D7CC;
  DWORD nMask = nBucket - 1;
  DWORD c = dword_50D7D4;
  DWORD b = dword_50D7D0;
  DWORD v;

  do {
    a += b;
    b += 0x71823402;
    c += a;
    a >>= 1;
    b ^= c;
    v = (b ^ a) & nMask;
  } while (v >= nSpan);

  dword_50D7D0 = b;
  dword_50D7CC = a;
  dword_50D7D4 = c;
  return static_cast<BYTE>(nLow + v);
}

// 0x4A6E60 statport_portrait_hide (39 B). Drop the panel when the unit it is
// showing goes away.
void statport_portrait_hide(intptr_t nArg0) {
  ctrl_t *pCtrl = sgpStatPortCtrl;

  if (pCtrl != NULL &&
      reinterpret_cast<CUnit *>(nArg0) == ctrl_port_unit(pCtrl)) {
    ctrl_port_unit(pCtrl) = NULL;
    ctrl_port_id(pCtrl) = 0xFFFF;
    ctrl_hide(pCtrl);
  }
}

// 0x4A6E90 set_playback_option (125 B). Publish the video option and repaint
// the current selection's portrait under it. The two portrait ids that get
// remapped are the campaign's own.
void set_playback_option(int nArg0) {
  DWORD dwOption = nArg0;

  if (dwOption > 2)
    dwOption = 2;
  gPortPlayback_g = dwOption;

  if (sgpStatPortCtrl == NULL || gpStatUnit == NULL)
    return;

  WORD w = guwUnitPortrait[gpStatUnit->GetTypeEx()];
  switch (w) {
    case 0x26:
      if (word_526524 > 0x1F)
        w = guwUnitPortrait[152];
      break;
    case 0x3C:
      if (word_526524 >= 0x1C)
        w = guwUnitPortrait[78];
      break;
  }
  statport_portrait_show(gpStatUnit, w, 1);
}

// 0x4A6F10 statport_portrait_show (284 B). Put a portrait on the panel: stop
// whatever is playing, resolve id 0x52 to the unit's own slot colour, and start
// the still or the talking head.
void statport_portrait_show(CUnit *pUnit, WORD wPortrait, int nMode) {
  HANDLE hVid = sgpPortSmk_g;

  if (hVid != NULL) {
    SVidPlayEnd(hVid);
    sgpPortSmk_g = NULL;
  }

  if (wPortrait == 0xFFFF) {
    ctrl_t *pHide = sgpStatPortCtrl;
    ctrl_port_unit(pHide) = NULL;
    ctrl_port_id(pHide) = 0xFFFF;
    ctrl_hide(pHide);
    return;
  }

  if (wPortrait == 0x52 && pUnit != NULL) {
    BYTE bOwner = pUnit->pSprite->bCreator;
    BYTE bSlot = static_cast<BYTE>(get_slot_mapping(bOwner));
    if (bSlot >= 8)
      wPortrait = bSlot + 0x62;
    else if (gbSlotRemap_g[bSlot] != 0)
      wPortrait = gbSlotRemap_g[bSlot] + 0x62;
    else
      wPortrait = bSlot + 0x52;
  }

  ctrl_t *pCtrl = sgpStatPortCtrl;
  if (ctrl_port_unit(pCtrl) != pUnit || ctrl_port_id(pCtrl) != wPortrait) {
    BYTE bPen = sgbPens_g[15];
    void *pSaved = _bmp_current;
    _bmp_current = &sgStatportSurface_g;
    bmp_clear(bPen);
    ctrl_t *pShow = sgpStatPortCtrl;
    _bmp_current = pSaved;
    ctrl_show(pShow);
    ctrl_t *pClaim = sgpStatPortCtrl;
    byte_6AB3A1 = 0;
    byte_6AB38C = 1;
    ctrl_port_unit(pClaim) = pUnit;
    ctrl_port_id(pClaim) = wPortrait;
  }

  switch (static_cast<WORD>(nMode)) {
    case 2:
      sub_4A6C50(wPortrait);
      dword_6AB3CC = 1;
      break;
    default:
      sub_4A6B40(wPortrait);
      dword_6AB3CC = 0;
      break;
  }
}

// 0x4A7030 statport_portrait_timer (344 B). The panel's timer callback. Id 1
// steps the video, id 2 fades the still, id 3 drops back to the selection's own
// portrait.
// The id arrives as a dword and is switched on as a WORD, as in gluBrfPort.cpp,
// which lets the pass-through to timer_remove_node forward EDX unchanged.
// pass-through to timer_remove_node forward EDX unchanged.
void statport_portrait_timer(intptr_t nArg0, int nArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  int nDrawn = 0;

  switch (static_cast<WORD>(nArg1)) {
    case 1:
      if ((pCtrl->dwFlags & 8) == 0)
        return;
      if (sgpPortSmk_g == NULL)
        return;
      if (sub_4A6700(reinterpret_cast<intptr_t>(sgpPortSmk_g), 0,
                     reinterpret_cast<intptr_t>(&nDrawn)) == 0) {
        if (dword_6AB3CC == 0)
          statport_portrait_show(gpStatUnit, static_cast<WORD>(get_unit_portrait(
                                     gpStatUnit->GetTypeEx())),
                     1);
        else
          statport_portrait_show(ctrl_port_unit(pCtrl), ctrl_port_id(pCtrl), 2);
      }
      if (byte_6AB38C != 0 && nDrawn != 0) {
        byte_6AB38C = 0;
        byte_6AB3A1 = 0x32;
      }
      break;

    case 2:
      if ((pCtrl->dwFlags & 8) == 0)
        return;
      if (sgpPortSmk_g == NULL)
        return;
      if (byte_6AB38C == 0) {
        if (byte_6AB3A1 >= 0x50)
          return;
        byte_6AB3A1 = byte_6AB3A1 + 0x0A;
      }
      nDrawn = 1;
      break;

    case 3:
      dword_6AB3CC = 0;
      timer_remove_node(pCtrl, nArg1);
      if (sgpPortSmk_g != NULL) {
        SVidPlayEnd(sgpPortSmk_g);
        sgpPortSmk_g = NULL;
      }
      if (gpStatUnit != NULL) {
        statport_portrait_show(gpStatUnit,
                   static_cast<WORD>(get_unit_portrait(gpStatUnit->GetTypeEx())),
                   1);
      } else {
        ctrl_t *pHide = sgpStatPortCtrl;
        ctrl_port_unit(pHide) = NULL;
        ctrl_port_id(pHide) = 0xFFFF;
        ctrl_hide(pHide);
      }
      break;

    default:
      return;
  }

  if (nDrawn != 0)
    ctrl_set_update(pCtrl);
}

// 0x4A7190 statport_talk_timer (110 B). The transmission timer: hold the
// portrait for as long as the sound runs, then hand the panel back to the
// selection.
void statport_talk_timer(intptr_t nArg0, WORD wArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);

  if (wArg1 != 3)
    return;

  DWORD dwMs = gamesnd_duration(dword_6AB3E8);
  if (dwMs > 1000) {
    timer_create(sgpStatPortCtrl, 3, dwMs - 1000,
                 reinterpret_cast<PFN_TIMER_FN *>(statport_portrait_timer));
    return;
  }

  dword_6AB3CC = 0;
  timer_remove_node(pCtrl, 3);
  if (gpStatUnit != NULL)
    return;

  ctrl_t *pHide = sgpStatPortCtrl;
  ctrl_port_unit(pHide) = NULL;
  ctrl_port_id(pHide) = 0xFFFF;
  ctrl_hide(pHide);
}

// 0x4A7200 statport_talking_portrait (241 B). Show a unit's portrait for a
// sound, arm the release timer, and ping the minimap. The declaration in
// src117/h/gamesnd.h:178 is the one this satisfies. The x clamp repeats the
// subtraction in the test so it compiles to `add eax, -140h` after a hoisted
// `xor`; y is read raw first so its clamp is a lea into EAX. pSprite is reread
// before the blink stores.
void __fastcall statport_talking_portrait(CUnit *pUnit, int nDeadlineMs, int a3, int a4,
                           int nSoundId) {
  if (dword_6AB3D0 != 0)
    return;

  WORD w = guwUnitPortrait[pUnit->GetTypeEx()];
  switch (w) {
    case 0x26:
      if (word_526524 > 0x1F)
        w = guwUnitPortrait[152];
      break;
    case 0x3C:
      if (word_526524 >= 0x1C)
        w = guwUnitPortrait[78];
      break;
  }
  statport_portrait_show(pUnit, w, 2);

  if (nDeadlineMs == 0 && static_cast<DWORD>(nSoundId) < 0x478) {
    dword_6AB3E8 = nSoundId;
    timer_create(sgpStatPortCtrl, 3, 1000,
                 reinterpret_cast<PFN_TIMER_FN *>(statport_talk_timer));
  } else {
    timer_create(sgpStatPortCtrl, 3, nDeadlineMs,
                 reinterpret_cast<PFN_TIMER_FN *>(statport_portrait_timer));
  }

  if (pUnit == NULL || a3 == 0)
    return;

  int nY = pUnit->pSprite->wPosY;
  int nX = static_cast<short>(pUnit->pSprite->wPosX);
  // Hyperion: the middle of the view, 0x140 and 0xC8 in 1.17; of what shows beside a
  // HUD's side columns (PortViewShownWidth).
  dword_527014 = nX - PortViewShownWidth() / 2 < 0 ? 0 : nX - PortViewShownWidth() / 2;
  int nY2 = nY - g_portView.nViewRows / 2;
  dword_527018 = nY2 < 0 ? 0 : nY2;

  CSprite *pSpr = pUnit->pSprite;
  unit_blink_timer(pUnit) = 8;
  unit_blink_player(pUnit) = static_cast<BYTE>(a4);
  do_minimap_ping(static_cast<short>(pSpr->wPosX), pSpr->wPosY,
                  static_cast<BYTE>(a4));
}

// 0x4A7300 statport_trigger_done (29 B). The trigger portrait's own expiry
// timer: release the claim, and in a single player game let the trigger engine
// know the action finished.
void statport_trigger_done(intptr_t nArg0, int nArg1) {
  timer_remove_node(reinterpret_cast<ctrl_t *>(nArg0), nArg1);
  dword_6AB3D0 = 0;
  if (gbMultiPlayerMode == 0)
    victory_notify_action_done();
}

// 0x4A7320 show_portrait (186 B). A trigger's portrait action: claim the panel
// for dwTime, remap the campaign ids, and remember where the view should jump
// to. The two clamps are ternaries; as `if` statements nX lands in EBX instead
// of EDI.
BOOL __fastcall show_portrait(WORD wUnitType, int nX, int nY, DWORD dwTime) {
  dword_6AB3D0 = 1;
  timer_create(sgpStatPortCtrl, 4, dwTime,
               reinterpret_cast<PFN_TIMER_FN *>(statport_trigger_done));

  WORD w = guwUnitPortrait[wUnitType];
  switch (w) {
    case 0x26:
      if (word_526524 > 0x1F)
        w = guwUnitPortrait[152];
      break;
    case 0x3C:
      if (word_526524 >= 0x1C)
        w = guwUnitPortrait[78];
      break;
  }
  statport_portrait_show(NULL, w, 2);

  timer_create(sgpStatPortCtrl, 3, dwTime,
               reinterpret_cast<PFN_TIMER_FN *>(statport_portrait_timer));

  if (nX != -1) {
    int x = nX - PortViewShownWidth() / 2;  // Hyperion: 0x140 and 0xC8 in 1.17
    dword_527014 = x < 0 ? 0 : x;
    int y = nY - g_portView.nViewRows / 2;
    dword_527018 = y < 0 ? 0 : y;
  }
  return TRUE;
}

// 0x4A73E0 statport_trigger_reset (52 B). Drop a trigger's claim on the panel
// and put the selection's portrait back.
void statport_trigger_reset(void) {
  if (dword_6AB3D0 == 0)
    return;

  ctrl_t *pCtrl = sgpStatPortCtrl;
  dword_6AB3D0 = 0;
  timer_remove_node(pCtrl, 4);
  statport_portrait_timer(reinterpret_cast<intptr_t>(sgpStatPortCtrl), 3);
}

// 0x4A7420 statport_update (145 B). The per-frame refresh: repaint only when
// the selection changed identity or type, and clear the dirty flag either way.
void statport_update(void) {
  if (sgbStatPortUpdate == 0)
    return;

  if (dword_6AB3CC == 0) {
    ctrl_t *pCtrl = sgpStatPortCtrl;
    CUnit *pUnit = gpStatUnit;
    CUnit *pShown = ctrl_port_unit(pCtrl);

    if (pUnit == NULL) {
      ctrl_port_unit(pCtrl) = pUnit;
      ctrl_port_id(pCtrl) = 0xFFFF;
      ctrl_hide(pCtrl);
    } else if (pShown != pUnit) {
      statport_portrait_show(gpStatUnit,
                 static_cast<WORD>(get_unit_portrait(gpStatUnit->GetTypeEx())),
                 1);
    } else if (pShown != NULL && pShown->GetTypeEx() != gpStatUnit->GetTypeEx()) {
      statport_portrait_show(gpStatUnit,
                 static_cast<WORD>(get_unit_portrait(gpStatUnit->GetTypeEx())),
                 1);
    }
  }
  sgbStatPortUpdate = 0;
}
