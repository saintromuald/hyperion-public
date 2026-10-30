// gluBrfPort.cpp -- the mission briefing's portrait control: the talking-head
// SMK panel each briefing slot owns, its fidget and talking clips, and its
// frame bitmaps. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/gluBrfPort.h"   // + gluRdy.h, dialog.h
#include "../h/dlg_ctrl.h"     // ctrl_hide, ctrl_show, ctrl_set_update
#include "../h/minimap.h"      // timer_create, timer_remove_node, PFN_TIMER_FN
#include "../h/bmp.h"          // bmp_t
#include "../h/glues.h"        // alloc_load_bmp, str_load_table, str_get_string, mem_free
#include "../h/snd.h"          // load_array_data
#include "../port/data/port_moddata.h"  // Hyperion: ModPortraitChance, ModPortraitsResolved
#include "../h/victory.h"      // psnprintf
#include "../h/Storm.h"        // SMemAlloc, SMemFree, SVidPlay*
#include "../h/gds_bmpcop.h"
#include "../h/gds_bmpmas.h"
#include "../gen/globals117.h"
#include "../h/gamedata.h"  // Hyperion: array_data_t

// rand and srand: OcRand and OcSrand (h/platform.h)

// 0x444890 -- the loader callback str_load_table takes in EDX, spelled as
// glues.cpp spells it; only its address is taken here.
void *load_game_data(const char *pszFile, void *pBuffer, DWORD *pdwSize,
                     const char *pszTag, int nLine);

// The portrait object, 0x14 bytes: the frame bitmap the clip plays into, the
// SVid handle, the flags (1 talking, 2 show the control once a frame is ready),
// the portrait id, the talking clip last played, and the centring offsets
// sub_450370 draws at. +0x13 is never touched.
struct brfport_t {
  /*0x00*/ bmp_t  bmp;
  /*0x08*/ HANDLE hVid;
  /*0x0C*/ WORD   wFlags;
  /*0x0E*/ WORD   wPortrait;
  /*0x10*/ BYTE   bLastTalk;
  /*0x11*/ BYTE   bOfsX;
  /*0x12*/ BYTE   bOfsY;
  /*0x13*/ BYTE   _unmapped_13;
};
OC_X86_LAYOUT(sizeof(brfport_t) == 0x14, "brfport_t  gluBrfPort_ctrl_init: push 14h / call SMemAlloc");
static_assert(offsetof(brfport_t, bmp) == 0x00, "brfport_t::bmp        mov ecx, edi / call bmp_create");
OC_X86_LAYOUT(offsetof(brfport_t, hVid) == 0x08, "brfport_t::hVid       mov eax, dword ptr [esi+8h] / SVidPlayEnd");
OC_X86_LAYOUT(offsetof(brfport_t, wFlags) == 0x0C, "brfport_t::wFlags     or word ptr [eax+0Ch], 1h");
OC_X86_LAYOUT(offsetof(brfport_t, wPortrait) == 0x0E, "brfport_t::wPortrait  mov word ptr [esi+0Eh], bx");
OC_X86_LAYOUT(offsetof(brfport_t, bLastTalk) == 0x10, "brfport_t::bLastTalk  movzx eax, byte ptr [esi+10h]");
OC_X86_LAYOUT(offsetof(brfport_t, bOfsX) == 0x11, "brfport_t::bOfsX      mov byte ptr [edi+11h], al");
OC_X86_LAYOUT(offsetof(brfport_t, bOfsY) == 0x12, "brfport_t::bOfsY      mov byte ptr [edi+12h], al");

// load_array_data's descriptor table is gamedata.h's array_data_t.

// SVidPlayBegin's third and fourth arguments, two stack blocks. Their fields
// keep positional names: the first is filled {0, 0, nHeight-1, nWidth-1} and
// the second {nWidth, nHeight}, so neither reads as a rectangle.
struct vidblock4_t { int n00, n04, n08, n0C; };
struct vidblock2_t { int n00, n04; };
static_assert(sizeof(vidblock4_t) == 0x10, "vidblock4_t  lea eax,[ebp-1Ch] .. lea eax,[ebp-0Ch]");
static_assert(sizeof(vidblock2_t) == 0x08, "vidblock2_t  lea eax,[ebp-0Ch] .. [ebp-4h]");

// The globals this TU reaches, dual-spelled, one #ifndef per name: the
// portdata.tbl strings; the four idle-change weight tables, a BYTE per
// portrait; the normal and highlighted frames, four each; the fidget and
// talking clip names, 110 each, a string id until briefing_port_init stores the
// string's address, hence an integer; the entry briefing_port_init scans for;
// and the units.dat and portdata.dat descriptor tables.

#ifndef OC_G_BRFPORTDATA_STRINGS
#define OC_G_BRFPORTDATA_STRINGS
#ifdef OC_DLL_MODE
#define g_brfPortdata_strings (*reinterpret_cast<void **>(OC_VA(0x667BB0u)))
#else
extern void *g_brfPortdata_strings;
#endif
#endif

#ifndef OC_DWORD_667BB4
#define OC_DWORD_667BB4
#ifdef OC_DLL_MODE
#define dword_667BB4 (reinterpret_cast<BYTE **>(OC_VA(0x667BB4u)))
#else
extern BYTE *dword_667BB4[4];
#endif
#endif

#ifndef OC_NORMAL_FRAMES
#define OC_NORMAL_FRAMES
#ifdef OC_DLL_MODE
#define normal_frames (reinterpret_cast<bmp_t *>(OC_VA(0x667BC4u)))
#else
extern bmp_t normal_frames[4];
#endif
#endif

#ifndef OC_HIGHLIGHTED_FRAMES
#define OC_HIGHLIGHTED_FRAMES
#ifdef OC_DLL_MODE
#define highlighted_frames (reinterpret_cast<bmp_t *>(OC_VA(0x667BE4u)))
#else
extern bmp_t highlighted_frames[4];
#endif
#endif

// Hyperion: 0x667B4C and 0x667B6C are not arrays of their own. They are
// normal_frames and highlighted_frames less 15 elements (0x667BC4 - 0x78 and
// 0x667BE4 - 0x78): the compiler folded the portrait controls' first index, 15,
// into the base, so sub_450370 indexes the two frame tables with nIndex - 15. In
// storage of their own at 64 bits the folded bases would miss them.
static const int kFirstPortraitCtrl = 15;

#ifndef OC_GSZFIDGETSMK
#define OC_GSZFIDGETSMK
#ifdef OC_DLL_MODE
#define gszFidgetSmk (reinterpret_cast<DWORD *>(OC_VA(0x51BD40u)))
#else
extern DWORD_PTR gszFidgetSmk[110];
#endif
#endif

#ifndef OC_GSZTALKSMK
#define OC_GSZTALKSMK
#ifdef OC_DLL_MODE
#define gszTalkSmk (reinterpret_cast<DWORD *>(OC_VA(0x51BEF8u)))
#else
extern DWORD_PTR gszTalkSmk[110];
#endif
#endif

#ifndef OC_PORTDATA_IDLESMKCHANGE
#define OC_PORTDATA_IDLESMKCHANGE
#ifdef OC_DLL_MODE
#define portdata_idlesmkChange (reinterpret_cast<BYTE *>(OC_VA(0x51C0B0u)))
#else
extern BYTE portdata_idlesmkChange[110];
#endif
#endif

#ifndef OC_GUNITSARRAYDATATBL
#define OC_GUNITSARRAYDATATBL
#ifdef OC_DLL_MODE
#define gUnitsArrayDataTbl (reinterpret_cast<array_data_t*>(OC_VA(0x5014F0u)))
#else
extern array_data_t gUnitsArrayDataTbl[55] OC_UNALIGNED;
#endif
#endif

#ifndef OC_GPORTDATAARRAYDATATBL_G
#define OC_GPORTDATAARRAYDATATBL_G
#ifdef OC_DLL_MODE
#define gPORTDATAArrayDataTbl_g (reinterpret_cast<array_data_t*>(OC_VA(0x501368u)))
#else
extern array_data_t gPORTDATAArrayDataTbl_g[7] OC_UNALIGNED;
#endif
#endif

// ctrl_t +0x26 -- the portrait object, reached through dialog.h's
// `_unmapped24[6]` window, as gluRdy.cpp reaches the same slot.
// Hyperion: through the window, which holds a pointer at 64 bits.
#define ctrl_port(p) (*reinterpret_cast<brfport_t **>(&(p)->_unmapped24[2]))

// The TU's own owner tag, the .rdata string at 0x4ED650.
#define OC_BRFPORT_FILE "gluBrfPort.cpp"

// The bodies, in oracle VA order.

// 0x44FF70 (89 B). Tears the portrait object down and drops both of the
// control's timers.
void gluBrfPort_ctrl_deactivate(intptr_t nArg0) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  brfport_t *pPort = ctrl_port(pCtrl);

  if (pPort != NULL) {
    if (pPort->hVid != NULL) {
      SVidPlayEnd(pPort->hVid);
      pPort->hVid = NULL;
    }
    bmp_destroy(&pPort->bmp);
    SMemFree(pPort, OC_BRFPORT_FILE, 0x125, 0);
    ctrl_port(pCtrl) = NULL;
  }
  timer_remove_node(pCtrl, 1);
  timer_remove_node(pCtrl, 2);
}

// 0x44FFD0 (60 B). The control's event handler. Of the 0x0E-tagged events, id
// 1 deactivates and still goes on to the control type's own handler in
// gCtrlEvtFcnTbl; id 10 initialises and returns 1 without it.
BOOL ctrl_brief_portrait_fcn(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 1:
        gluBrfPort_ctrl_deactivate(reinterpret_cast<intptr_t>(pCtrl));
        break;
      case 10:
        gluBrfPort_ctrl_init(reinterpret_cast<intptr_t>(pCtrl));
        return TRUE;
    }
  }
  return reinterpret_cast<PFN_CTRL_EVT>(gCtrlEvtFcnTbl[pCtrl->wType])(pCtrl,
                                                                      pEvent);
}

// 0x450010 (104 B). The exit handler: drops the .tbl block and every loaded
// frame bitmap.
void briefing_port_free() {
  mem_free(g_brfPortdata_strings, OC_BRFPORT_FILE, 0x16F);
  g_brfPortdata_strings = NULL;

  for (int i = 0; i < 4; i++) {
    if (normal_frames[i].pPixels != NULL) {
      bmp_destroy(&normal_frames[i]);
      normal_frames[i].pPixels = NULL;
    }
    if (highlighted_frames[i].pPixels != NULL) {
      bmp_destroy(&highlighted_frames[i]);
      highlighted_frames[i].pPixels = NULL;
    }
  }
}

// 0x450080 (167 B). Builds the portrait object, hangs it off the control with
// its draw handler and fidget timer, then hides the control. hVid is zeroed
// before the two WORDs, 1.17's order.
void gluBrfPort_ctrl_init(intptr_t nArg0) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);

  brfport_t *pPort = reinterpret_cast<brfport_t *>(
      SMemAlloc(sizeof(brfport_t), OC_BRFPORT_FILE, 0xFF, 0));
  pPort->bmp.nWidth = 0x78;
  pPort->bmp.nHeight = 0x70;
  bmp_create(&pPort->bmp, OC_BRFPORT_FILE, 0x103);

  void *pSaved = _bmp_current;
  _bmp_current = &pPort->bmp;
  bmp_clear(0);
  _bmp_current = pSaved;

  pPort->hVid = NULL;
  pPort->wFlags = 0;
  pPort->wPortrait = 0;
  pPort->bOfsX = (BYTE)((pCtrl->rc.right - pCtrl->rc.left - 0x77) / 2);
  pPort->bOfsY = (BYTE)((pCtrl->rc.bottom - pCtrl->rc.top - 0x6F) / 2);

  ctrl_port(pCtrl) = pPort;
  pCtrl->pfnDraw = reinterpret_cast<PFN_CTRL_DRAW>(sub_450370);
  timer_create(pCtrl, 1, 0x32, reinterpret_cast<PFN_TIMER_FN *>(sub_4505A0));
  ctrl_hide(pCtrl);
}


// 0x450130 (190 B). Loads the two .dat tables and the portrait .tbl, turns
// every fidget and talking clip's string id into its name in place, then takes
// the four idle-change weight tables: portdata_idlesmkChange's descriptor entry
// and the three after it.
// str_get_string's id parameter is 16 bits, hence the (WORD) casts.
void briefing_port_init() {
  load_array_data("arr\\units.dat", gUnitsArrayDataTbl);
  load_array_data("arr\\portdata.dat", gPORTDATAArrayDataTbl_g);

  g_brfPortdata_strings = reinterpret_cast<void *>(
      str_load_table(reinterpret_cast<intptr_t>("arr\\portdata.tbl"),
                     reinterpret_cast<intptr_t>(load_game_data),
                     reinterpret_cast<intptr_t>(OC_BRFPORT_FILE), 0x14A));

  for (int i = 0; i < 110; i++) {
    gszFidgetSmk[i] = str_get_string(g_brfPortdata_strings,
                                     (WORD)gszFidgetSmk[i]);
    gszTalkSmk[i] = str_get_string(g_brfPortdata_strings,
                                   (WORD)gszTalkSmk[i]);
  }

  array_data_t *p = gPORTDATAArrayDataTbl_g;
  while (p->pDest != portdata_idlesmkChange)
    p++;
  dword_667BB4[0] = reinterpret_cast<BYTE *>(p[0].pDest);
  dword_667BB4[1] = reinterpret_cast<BYTE *>(p[1].pDest);
  dword_667BB4[2] = reinterpret_cast<BYTE *>(p[2].pDest);
  dword_667BB4[3] = reinterpret_cast<BYTE *>(p[3].pDest);
  ModPortraitsResolved();  // Hyperion: the names a mod's edits give portraits
}


// 0x4501F0 (82 B). Loads the four normal and four highlighted frame bitmaps
// from two parallel name arrays.
void sub_4501F0(intptr_t nArg0, intptr_t nArg1) {
  const char **ppNormal = reinterpret_cast<const char **>(nArg0);
  const char **ppHigh = reinterpret_cast<const char **>(nArg1);

  for (int i = 0; i < 4; i++) {
    alloc_load_bmp(const_cast<char *>(ppNormal[i]), &normal_frames[i], 0,
                   const_cast<char *>(OC_BRFPORT_FILE), 0x166);
    alloc_load_bmp(const_cast<char *>(ppHigh[i]), &highlighted_frames[i], 0,
                   const_cast<char *>(OC_BRFPORT_FILE), 0x167);
  }
}

// 0x450250 (42 B). Ends the clip and hides the control.
void brfport_hide_portrait(ctrl_t *pCtrl) {
  brfport_t *pPort = ctrl_port(pCtrl);

  if (pPort->hVid != NULL) {
    SVidPlayEnd(pPort->hVid);
    pPort->hVid = NULL;
  }
  pPort->wFlags = 0;
  ctrl_hide(pCtrl);
}

// 0x450280 (54 B). Ends the clip and starts the given portrait, the control
// shown once its first frame is ready.
void brfport_show_portrait(ctrl_t *pCtrl, WORD wPortrait) {
  brfport_t *pPort = ctrl_port(pCtrl);

  if (pPort->hVid != NULL) {
    SVidPlayEnd(pPort->hVid);
    pPort->hVid = NULL;
  }
  pPort->wPortrait = wPortrait;
  pPort->wFlags = 2;
  sub_4502E0(reinterpret_cast<intptr_t>(pCtrl));
}

// 0x4502C0 (25 B). Marks the portrait talking and starts the talk timer; EDX,
// the interval, goes straight to timer_create.
void brfport_talking_portrait(void *pArg0, int nArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(pArg0);
  brfport_t *pPort = ctrl_port(pCtrl);

  pPort->wFlags |= 1;
  timer_create(pCtrl, 2, nArg1, reinterpret_cast<PFN_TIMER_FN *>(sub_4505A0));
}

// 0x4502E0 (132 B). Pumps the current clip. With no clip, or when the pump
// fails, it ends the clip and starts the next, talking (flag 1) or fidget; when
// a frame finished this tick it shows the control if flag 2 asks, and repaints.
void sub_4502E0(intptr_t nArg0) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  DWORD dwFinished = 0;
  brfport_t *pPort = ctrl_port(pCtrl);

  if (pPort->hVid == NULL ||
      !SVidPlayContinueSingle(pPort->hVid, 0, &dwFinished)) {
    if (pPort->hVid != NULL) {
      SVidPlayEnd(pPort->hVid);
      pPort->hVid = NULL;
    }
    if (pPort->wFlags & 1)
      sub_4504C0(reinterpret_cast<intptr_t>(pCtrl));
    else
      sub_4503D0(reinterpret_cast<intptr_t>(pCtrl));
    return;
  }
  if (dwFinished == 0)
    return;

  if (pPort->wFlags & 2) {
    ctrl_show(pCtrl);
    pPort->wFlags &= 0xFFFD;
  }
  ctrl_set_update(pCtrl);
}

// 0x450370 (83 B). The control's draw handler, dialog.h's PFN_CTRL_DRAW with
// its `ret 8h`; neither stack argument is read. The coordinates are 16-bit
// sums, as bmp_copy and bmp_mask take them.
void sub_450370(intptr_t nArg0, int nArg1, int nArg2, int nArg3) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  brfport_t *pPort = ctrl_port(pCtrl);

  bmp_copy((unsigned short)(pPort->bOfsX + pCtrl->rc.left),
           (unsigned short)(pPort->bOfsY + pCtrl->rc.top), &pPort->bmp);

  if (pPort->wFlags & 1)
    bmp_mask((unsigned short)pCtrl->rc.left, (unsigned short)pCtrl->rc.top,
             &highlighted_frames[pCtrl->nIndex - kFirstPortraitCtrl]);
  else
    bmp_mask((unsigned short)pCtrl->rc.left, (unsigned short)pCtrl->rc.top,
             &normal_frames[pCtrl->nIndex - kFirstPortraitCtrl]);
}

// 0x4503D0 (233 B). Starts the next fidget clip, drawn from a weighted
// distribution: the four weight tables are summed for this portrait until the
// total reaches a 0..100 roll, and an index past the end falls back to 0. The
// walk is a do-while with the roll test as a break, as 1.17 has it.
void sub_4503D0(intptr_t nArg0) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  char szFile[0x104];
  vidblock4_t blk4;
  vidblock2_t blk2;

  BYTE bRoll = (BYTE)((OcRand() >> 8) % 101);
  brfport_t *pPort = ctrl_port(pCtrl);
  WORD wPortrait = pPort->wPortrait;

  char bIdx = 0;
  // Hyperion: a mod's portrait's chances are its record's (port_moddata.cpp)
  BYTE bAcc = ModPortraitChance(0, wPortrait, dword_667BB4);
  do {
    if (bRoll <= bAcc)
      break;
    bIdx++;
    bAcc += ModPortraitChance(bIdx, wPortrait, dword_667BB4);
  } while (bIdx < 4);
  if (bIdx >= 4)
    bIdx = 0;

  blk2.n00 = pPort->bmp.nWidth;
  blk2.n04 = pPort->bmp.nHeight;
  blk4.n00 = 0;
  blk4.n04 = 0;
  blk4.n0C = pPort->bmp.nWidth - 1;
  blk4.n08 = pPort->bmp.nHeight - 1;

  for (; bIdx >= 0; bIdx--) {
    if (pPort->hVid != NULL)
      break;
    psnprintf(szFile, 0x104, "%s%s%d.smk", "portrait\\",
              reinterpret_cast<const char *>(gszFidgetSmk[pPort->wPortrait]),
              (int)bIdx);
    SVidPlayBegin(szFile, reinterpret_cast<DWORD_PTR>(pPort->bmp.pPixels), &blk4,
                  &blk2, 0, 0x21100200, &pPort->hVid);
  }
  ctrl_set_update(pCtrl);
}


// 0x4504C0 (216 B). Starts the next talking clip, one of three chosen at random
// but never the one that played last ((n + 2) % 3 steps back one). The index
// that started is stored in bLastTalk.
void sub_4504C0(intptr_t nArg0) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  char szFile[0x104];
  vidblock4_t blk4;
  vidblock2_t blk2;

  char bIdx = (char)((OcRand() >> 8) % 3);
  brfport_t *pPort = ctrl_port(pCtrl);
  if (bIdx == pPort->bLastTalk)
    bIdx = (char)((bIdx + 2) % 3);

  blk2.n00 = pPort->bmp.nWidth;
  blk2.n04 = pPort->bmp.nHeight;
  blk4.n00 = 0;
  blk4.n04 = 0;
  blk4.n0C = pPort->bmp.nWidth - 1;
  blk4.n08 = pPort->bmp.nHeight - 1;

  for (; bIdx >= 0; bIdx--) {
    if (pPort->hVid != NULL)
      break;
    psnprintf(szFile, 0x104, "%s%s%d.smk", "portrait\\",
              reinterpret_cast<const char *>(gszTalkSmk[pPort->wPortrait]),
              (int)bIdx);
    SVidPlayBegin(szFile, reinterpret_cast<DWORD_PTR>(pPort->bmp.pPixels), &blk4,
                  &blk2, 0, 0x21100200, &pPort->hVid);
  }
  pPort->bLastTalk = bIdx;
  ctrl_set_update(pCtrl);
}

// 0x4505A0 (74 B). The control's timer callback; both timers land here, told
// apart by the id in EDX. The id is narrowed to 16 bits for the switch and
// passed whole to timer_remove_node. Each case calls sub_4502E0 itself, as in
// 1.17.
void sub_4505A0(intptr_t nArg0, int nArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  brfport_t *pPort = ctrl_port(pCtrl);

  if (!(pCtrl->dwFlags & 8) && !(pPort->wFlags & 2))
    return;

  switch ((WORD)nArg1) {
    case 1:
      sub_4502E0(reinterpret_cast<intptr_t>(pCtrl));
      break;
    case 2:
      pPort->wFlags &= 0xFFFE;
      timer_remove_node(pCtrl, nArg1);
      if (pPort->hVid != NULL) {
        SVidPlayEnd(pPort->hVid);
        pPort->hVid = NULL;
      }
      sub_4502E0(reinterpret_cast<intptr_t>(pCtrl));
      break;
  }
}

