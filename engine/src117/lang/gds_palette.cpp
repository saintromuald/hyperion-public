// gds_palette.cpp -- the palette: the eight colour-cycle slots, the software
// fade, the desaturate pass and the pen table. Bodies are in oracle VA order.
//
// cycle_palette, extra_cycle, normal_cycle and vid_fade are called from other
// bodies here and stay calls in 1.17, so each definition is noinline.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>       // offsetof, for the layout static_asserts
#include <string.h>       // memset, memmove, memcpy

#include "../h/gds_palette.h"  // cycle_palette, extra_cycle, init_pens,
                               // normal_cycle, do_all_cycles,
                               // vid_palette_to_monitor, vid_init_cycles,
                               // vid_set_cycle, vid_start_cycle
#include "../h/gds_vidinimo.h"  // palette_to_monitor
#include "../h/net_cmds.h"      // vid_desaturate, vid_restore_palette
#include "../h/glues.h"         // vid_fade_in, vid_fade_out
#include "../gen/globals117.h"

// One colour-cycle slot, 0x10 bytes. vid_start_cycle sets bStarted. A zero
// bRate ends the list, and bCountdown, the per-frame tick, reloads from it.
// [bLo, bHi] is the run of palette indices the slot moves. With pTable set the
// slot is table-driven: bStep counts through the table and wraps at bSteps.
// Without it the run rotates by one. The pad fields are bytes nothing reads.
struct cyclerec_t {
  /*0x00*/ BYTE bStarted;
  /*0x01*/ BYTE bRate;
  /*0x02*/ BYTE bCountdown;
  /*0x03*/ BYTE bLo;
  /*0x04*/ BYTE bStep;
  /*0x05*/ BYTE bHi;
  /*0x06*/ WORD wPad06;
  /*0x08*/ const palentry_t *pTable;
  /*0x0C*/ BYTE bSteps;
  /*0x0D*/ BYTE bPad0D;
  /*0x0E*/ WORD wPad0E;
};
OC_X86_LAYOUT(sizeof(cyclerec_t) == 0x10,
              "cyclerec_t size        -- `shl ecx,4h` in four bodies; `add esi,10h` in cycle_palette");
static_assert(offsetof(cyclerec_t, bStarted) == 0x00,
              "cyclerec_t.bStarted    -- vid_start_cycle [eax+6D2F88h]");
static_assert(offsetof(cyclerec_t, bRate) == 0x01,
              "cyclerec_t.bRate       -- vid_set_cycle [ecx+1h]");
static_assert(offsetof(cyclerec_t, bCountdown) == 0x02,
              "cyclerec_t.bCountdown  -- cycle_palette `dec byte ptr [esi]`, esi = rec+2");
static_assert(offsetof(cyclerec_t, bLo) == 0x03,
              "cyclerec_t.bLo         -- extra_cycle [ecx+3h]");
static_assert(offsetof(cyclerec_t, bStep) == 0x04,
              "cyclerec_t.bStep       -- extra_cycle `inc byte ptr [ecx+4h]`");
static_assert(offsetof(cyclerec_t, bHi) == 0x05,
              "cyclerec_t.bHi         -- extra_cycle [ecx+5h]");
static_assert(offsetof(cyclerec_t, pTable) == 0x08,
              "cyclerec_t.pTable      -- extra_cycle `mov edi, dword ptr [ecx+8h]`");
OC_X86_LAYOUT(offsetof(cyclerec_t, bSteps) == 0x0C,
              "cyclerec_t.bSteps      -- extra_cycle `mov bl, byte ptr [ecx+0Ch]`");

// The globals this TU reaches, dual-spelled with one #ifndef per name, below
// ../gen/globals117.h so that a generated spelling wins.

// 0x6D2388 -- the live palette the monitor is loaded from, 256 entries.
#ifndef _vid_pal
#ifdef OC_DLL_MODE
#define _vid_pal (reinterpret_cast<palentry_t *>(OC_VA(0x6D2388u)))
#else
extern palentry_t _vid_pal[256] OC_UNALIGNED;
#endif
#endif

// 0x6D2788 -- the palette a fade starts from.
#ifndef _vid_old
#ifdef OC_DLL_MODE
#define _vid_old (reinterpret_cast<palentry_t *>(OC_VA(0x6D2788u)))
#else
extern palentry_t _vid_old[256] OC_UNALIGNED;
#endif
#endif

// 0x6D2B88 -- the palette a fade ends at.
#ifndef _vid_want
#ifdef OC_DLL_MODE
#define _vid_want (reinterpret_cast<palentry_t *>(OC_VA(0x6D2B88u)))
#else
extern palentry_t _vid_want[256] OC_UNALIGNED;
#endif
#endif

// 0x6D2F88 -- the eight colour-cycle slots.
#ifndef _vid_cycle
#ifdef OC_DLL_MODE
#define _vid_cycle (reinterpret_cast<cyclerec_t *>(OC_VA(0x6D2F88u)))
#else
extern cyclerec_t _vid_cycle[8];
#endif
#endif

// 0x50F0F0 -- a BYTE, set while the screen is faded out: vid_fade_out sets it
// and vid_fade_in clears it.
#ifndef gbFadedOut
#ifdef OC_DLL_MODE
#define gbFadedOut (*reinterpret_cast<BYTE *>(OC_VA(0x50F0F0u)))
#else
extern BYTE gbFadedOut;
#endif
#endif

// 0x50F0F8 -- the 26 colours init_pens resolves into sgbPens_g.
#ifndef colour_table_g
#ifdef OC_DLL_MODE
#define colour_table_g (reinterpret_cast<const palentry_t *>(OC_VA(0x50F0F8u)))
#else
extern const palentry_t colour_table_g[26] OC_UNALIGNED;
#endif
#endif

// 0x6D2368 sgbPens_g, BYTE[32], comes from ../gen/globals117.h; init_pens
// fills its first 26.

// GetTickCount, from KERNEL32 (IAT slot 0x4EB2FC), declared as cur.cpp,
// credits.cpp and dlg_dlg.cpp declare it.
extern "C" __declspec(dllimport) DWORD __stdcall GetTickCount(void);

// The bodies, in oracle VA order.

// 0x4CCBC0 (146 B). Walks the cycle table from record nArg1, ticking every
// started record whose low palette index is within nArg2, and hands the union
// of the ranges it moved to palette_to_monitor. Returns the index it stopped
// on, which do_all_cycles resumes from. The index and bound tests are unsigned
// and the range accumulators signed ints, as in 1.17.
__declspec(noinline)
int cycle_palette(void *pArg0, int nArg1, int nArg2)
{
  cyclerec_t *pRec = reinterpret_cast<cyclerec_t *>(pArg0);
  DWORD i = static_cast<DWORD>(nArg1);
  int nMin = 0xFF;
  int nMax = 0;

  while (pRec->bRate != 0) {
    if (i >= 8)
      break;
    if (pRec->bLo > static_cast<DWORD>(nArg2))
      break;
    if (pRec->bStarted != 0) {
      if (--pRec->bCountdown == 0) {
        pRec->bCountdown = pRec->bRate;
        if (pRec->pTable != NULL)
          extra_cycle(static_cast<int>(i));
        else
          normal_cycle(static_cast<int>(i));
        if (pRec->bLo < nMin)
          nMin = pRec->bLo;
        if (pRec->bHi > nMax)
          nMax = pRec->bHi;
      }
    }
    ++pRec;
    ++i;
  }

  if (nMax >= nMin)
    palette_to_monitor(&_vid_pal[nMin], nMin, nMax - nMin + 1);

  return static_cast<int>(i);
}

// 0x4CCC60 (76 B). The table-driven cycle: every entry in [bLo, bHi] takes its
// colour from pTable at a rotating step index, and the step wraps at bSteps.
// ECX is the slot's index, not a pointer.
__declspec(noinline)
void extra_cycle(int nIndex)
{
  cyclerec_t *pRec = &_vid_cycle[nIndex];
  BYTE bSteps = pRec->bSteps;

  if (++pRec->bStep == bSteps)
    pRec->bStep = 0;

  unsigned nLo = pRec->bLo;
  unsigned nHi = pRec->bHi;
  unsigned nStep = pRec->bStep;

  if (nLo <= nHi) {
    const palentry_t *pTable = pRec->pTable;
    unsigned nCount = bSteps;
    do {
      _vid_pal[nLo] = pTable[nStep];
      ++nStep;
      if (nStep == nCount)
        nStep = 0;
      ++nLo;
    } while (nLo <= nHi);
  }
}

// 0x4CCCB0 (57 B). Resolves each of the 26 table colours to a palette index
// through the caller's finder and caches it in sgbPens_g. The table is
// palentry_t, not DWORD, which keeps 1.17's two cursors.
void init_pens(void *pPalette, PFN_FIND_PEN pfnFind)
{
  for (int i = 0; i < 26; i++)
    sgbPens_g[i] = static_cast<BYTE>(pfnFind(pPalette, colour_table_g[i]));
}

// 0x4CCCF0 (63 B). The plain cycle: the whole [bLo, bHi] run shifts one entry
// up and the top entry wraps to the bottom.
__declspec(noinline)
void normal_cycle(int nIndex)
{
  cyclerec_t *pRec = &_vid_cycle[nIndex];
  BYTE bLo = pRec->bLo;
  BYTE bHi = pRec->bHi;
  palentry_t save = _vid_pal[bHi];

  memmove(&_vid_pal[bLo + 1], &_vid_pal[bLo], (bHi - bLo) * 4);
  _vid_pal[bLo] = save;
}

// 0x4CCD30 (46 B). Two passes over the cycle table split at palette index
// 0x80: the first stops at the first record whose bLo is above it, and the
// second resumes from exactly where the first stopped. The test against 8 is
// unsigned, hence the DWORD.
void do_all_cycles()
{
  DWORD n = static_cast<DWORD>(cycle_palette(_vid_cycle, 0, 0x80));

  if (n < 8)
    cycle_palette(&_vid_cycle[n], static_cast<int>(n), 0x100);
}

// 0x4CCD60 (232 B). The software fade: nSpeed+1 linear steps from _vid_old to
// _vid_want, each one pushed to the monitor and then paced to 10 ms with
// GetTickCount. The clamp and the loop test are unsigned and the division
// signed, so the step counters are DWORDs and the division casts to int.
__declspec(noinline)
void vid_fade(int nSpeed)
{
  DWORD dwStart;
  DWORD nSteps;
  DWORD nStep;
  int i;

  nSteps = static_cast<DWORD>(nSpeed);
  if (nSteps > 10)
    nSteps = 10;
  nStep = 0;
  dwStart = GetTickCount();
  if (nSteps == 0) {
    nStep = 1;
    nSteps = 1;
  }

  for (;;) {
    if (nSteps != 0) {
      for (i = 0; i < 256; i++) {
        BYTE b;

        b = _vid_old[i].bRed;
        _vid_pal[i].bRed = static_cast<BYTE>((_vid_want[i].bRed - b) * static_cast<int>(nStep) / static_cast<int>(nSteps) + b);
        b = _vid_old[i].bGreen;
        _vid_pal[i].bGreen = static_cast<BYTE>((_vid_want[i].bGreen - b) * static_cast<int>(nStep) / static_cast<int>(nSteps) + b);
        b = _vid_old[i].bBlue;
        _vid_pal[i].bBlue = static_cast<BYTE>((_vid_want[i].bBlue - b) * static_cast<int>(nStep) / static_cast<int>(nSteps) + b);
      }
      palette_to_monitor(_vid_pal, 0, 0x100);
    }

    if (++nStep > nSteps)
      break;

    while (GetTickCount() - dwStart < 10)
      ;
  }
}

// 0x4CCE50 (91 B). Black out the live palette, show it, then fade up from
// black to whatever _vid_want already holds.
void vid_fade_in(int nArg0)
{
  if (gbFadedOut != 0) {
    gbFadedOut = 0;
    memset(_vid_pal, 0, 0x400);
    palette_to_monitor(_vid_pal, 0, 0x100);
    img_update();
    memset(_vid_old, 0, 0x400);
    vid_fade(nArg0);
  }
}

// 0x4CCEB0 (75 B). Fade down to black: the target becomes black, the current
// palette becomes the fade source, then one fade and one repaint.
void vid_fade_out(int nArg0)
{
  if (gbFadedOut == 0) {
    gbFadedOut = 1;
    memset(_vid_want, 0, 0x400);
    memcpy(_vid_old, _vid_pal, 0x400);
    vid_fade(nArg0);
    img_update();
  }
}

// 0x4CCF00 (21 B). Clears the eight cycle slots.
void vid_init_cycles(void)
{
  memset(_vid_cycle, 0, 8 * sizeof(_vid_cycle[0]));  // Hyperion: 0x80 in 1.17
}

// 0x4CCF20 (18 B). The whole live palette to the monitor.
void vid_palette_to_monitor()
{
  palette_to_monitor(_vid_pal, 0, 0x100);
}

// 0x4CCF40 (169 B). Build a greyscale copy of the live palette in a 0x400-byte
// stack buffer, make it the fade target, and fade to it with nSpeed 0. The luma
// weights 0x4D, 0x97 and 0x1C sum to 0x100, each product a signed division by
// 256 into a BYTE. The two walks are explicit pointers, pDst assigned before
// pSrc, the loop counts down, and the chained store writes blue, green, then
// red: each is 1.17's shape.
void vid_desaturate()
{
  palentry_t gray[256];
  const palentry_t *pSrc;
  palentry_t *pDst;
  int i;

  pDst = gray;
  pSrc = _vid_pal;
  i = 256;
  do {
    BYTE b;

    b = static_cast<BYTE>(pSrc->bGreen * 0x97 / 256);
    b = static_cast<BYTE>(b + pSrc->bRed * 0x4D / 256);
    b = static_cast<BYTE>(b + pSrc->bBlue * 0x1C / 256);
    pDst->bRed = pDst->bGreen = pDst->bBlue = b;
    pDst->bFlags = pSrc->bFlags;
    ++pDst;
    ++pSrc;
  } while (--i);

  // The size goes through a local: as a constant, /Oi expands this copy
  // inline, and the oracle calls memcpy.
  DWORD cb = sizeof(gray);
  memcpy(_vid_want, gray, cb);
  vid_fade(0);
}

// 0x4CD000 (53 B). Four stack arguments (`ret 10h`). dwRate is the colour
// table extra_cycle indexes, under maphdr.cpp's name.
void vid_set_cycle(BYTE bSlot, BYTE bArg1, BYTE bLo, BYTE bHi,
                   DWORD_PTR dwRate, BYTE bArg3)
{
  cyclerec_t *pRec = &_vid_cycle[bSlot];

  pRec->bLo = bLo;
  pRec->bHi = bHi;
  pRec->pTable = reinterpret_cast<const palentry_t *>(dwRate);
  pRec->bRate = bArg1;
  pRec->bCountdown = bArg1;
  pRec->bStep = 0;
  pRec->bSteps = bArg3;
}

// 0x4CD040 (36 B). 1.17 reads the count as a WORD, hence the cast.
void vid_restore_palette(BYTE bFirst, DWORD dwCount, const void *pPalette)
{
  memcpy(&_vid_want[bFirst], pPalette, static_cast<WORD>(dwCount) * 4);
}

// 0x4CD070 (14 B).
void vid_start_cycle(BYTE bSlot)
{
  _vid_cycle[bSlot].bStarted = 1;
}
