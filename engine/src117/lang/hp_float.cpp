// hp_float.cpp -- the floating hit-point, shield and energy bars drawn over a selected
// unit. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/hp_float.h"
#include "../h/bmp.h"         // bmp_hline, bmp_vline
#include "../h/CSprite.h"     // CSprite
#include "../h/CUnit.h"       // CUnit
#include "../h/glues.h"       // GameLoadImage
#include "../gen/globals117.h"
#include "../h/handicap.h"  // Hyperion: a player's handicap

// The globals this TU reaches, each spelled for both builds, one #ifdef per name.

// 0x66CB5C gbHpBarBytes_g -- the bar's colour bytes. hp_float_init loads 0x13 of them and
// the highest element read is [18], so the extent is exact.
#ifndef OC_GBHPBARBYTES_G
#define OC_GBHPBARBYTES_G
#ifdef OC_DLL_MODE
#define gbHpBarBytes_g (reinterpret_cast<BYTE *>(OC_VA(0x66CB5Cu)))
#else
extern BYTE gbHpBarBytes_g[0x13] OC_UNALIGNED;
#endif
#endif

// 0x5085D0 and 0x5085F4 -- the colour rows the five-row and seven-row bars pick from. The
// search steps one row at a time, so the stride is the row size: 6 for the first (a
// threshold plus five colours) and 8 for the second. Three rows each, the last with
// threshold 0, which is what ends the search.
#ifndef OC_BYTE_5085D0
#define OC_BYTE_5085D0
#ifdef OC_DLL_MODE
#define byte_5085D0 (reinterpret_cast<BYTE *>(OC_VA(0x5085D0u)))
#else
extern BYTE byte_5085D0[18];
#endif
#endif

#ifndef OC_BYTE_5085F4
#define OC_BYTE_5085F4
#ifdef OC_DLL_MODE
#define byte_5085F4 (reinterpret_cast<BYTE *>(OC_VA(0x5085F4u)))
#else
extern BYTE byte_5085F4[24] OC_UNALIGNED;
#endif
#endif

// 0x5085E4 -- the energy bar's one colour row, the same 6-byte shape.
#ifndef OC_BYTE_5085E4
#define OC_BYTE_5085E4
#ifdef OC_DLL_MODE
#define byte_5085E4 (reinterpret_cast<BYTE *>(OC_VA(0x5085E4u)))
#else
extern BYTE byte_5085E4[6];
#endif
#endif

// 0x5085EC and 0x50860C -- the colours of the unfilled part, one per row. The extents come
// from the loop bounds, 5 and 7.
#ifndef OC_BYTE_5085EC
#define OC_BYTE_5085EC
#ifdef OC_DLL_MODE
#define byte_5085EC (reinterpret_cast<BYTE *>(OC_VA(0x5085ECu)))
#else
extern BYTE byte_5085EC[5];
#endif
#endif

#ifndef OC_BYTE_50860C
#define OC_BYTE_50860C
#ifdef OC_DLL_MODE
#define byte_50860C (reinterpret_cast<BYTE *>(OC_VA(0x50860Cu)))
#else
extern BYTE byte_50860C[7];
#endif
#endif

// 0x508614 and 0x508618 -- the four-row bar's filled and unfilled colours. The extent is
// the loop bound, 4.
#ifndef OC_BYTE_508614
#define OC_BYTE_508614
#ifdef OC_DLL_MODE
#define byte_508614 (reinterpret_cast<BYTE *>(OC_VA(0x508614u)))
#else
extern BYTE byte_508614[4];
#endif
#endif

#ifndef OC_BYTE_508618
#define OC_BYTE_508618
#ifdef OC_DLL_MODE
#define byte_508618 (reinterpret_cast<BYTE *>(OC_VA(0x508618u)))
#else
extern BYTE byte_508618[4];
#endif
#endif

// 0x51EEB8 sprite_healthbarSize -- sprites.dat's health bar column, one byte per sprite
// from id 130, so the reads index it by the sprite id less 130.
#ifndef OC_SPRITE_HEALTHBARSIZE
#define OC_SPRITE_HEALTHBARSIZE
#ifdef OC_DLL_MODE
#define sprite_healthbarSize (reinterpret_cast<BYTE *>(OC_VA(0x51EEB8u)))
#else
extern BYTE sprite_healthbarSize[387];
#endif
#endif

// Scale nCur out of nMax into a percentage, then into a pixel run that is a multiple of
// three and at least three wide. The rounding is to nearest: a remainder of one rounds
// down, of two rounds up.
int sub_4648A0(int nMax, int nCur, int nWidth, int *pnLen)
{
  int nPct;
  int nLen;
  int nRem;
  int nSpan = nWidth - 1;

  if (nMax != 0)
    nPct = nCur * 100 / nMax;
  else
    nPct = 100;
  nLen = nPct * nSpan / 100;
  if (nLen < 3)
    nLen = 3;
  nRem = nLen % 3;
  if (nRem != 0) {
    if (nRem > 1)
      nLen += 3 - nRem;
    else
      nLen -= nRem;
  }
  *pnLen = nLen;
  return nPct;
}

// sub_4648A0 over the full width.
int sub_464920(int nMax, int nCur, int nWidth, int *pnLen)
{
  int nPct;
  int nLen;
  int nRem;

  if (nMax != 0)
    nPct = nCur * 100 / nMax;
  else
    nPct = 100;
  nLen = nPct * nWidth / 100;
  if (nLen < 3)
    nLen = 3;
  nRem = nLen % 3;
  if (nRem != 0) {
    if (nRem > 1)
      nLen += 3 - nRem;
    else
      nLen -= nRem;
  }
  *pnLen = nLen;
  return nPct;
}

// Draw the five-row bar: the filled run in the row's own colour, the rest in the shared
// ramp, then the ticks every three pixels from the first multiple of three at or after the
// bar's start value.
void sub_4649A0(int nX, int nY, hpbar_t *pBar, int nValue, const BYTE *pbColors)
{
  int nRest;
  int nXRest;
  int nRow;
  int y;
  int nTick;
  int nHeight;

  if (pBar->lHeight != 5)
    return;

  nRest = pBar->lWidth;
  nValue -= pBar->lStart;
  if (nValue > nRest)
    nValue = nRest;
  else if (nValue < 0)
    nValue = 0;
  nXRest = nX + nValue;
  nRest -= nValue;
  y = nY;
  for (nRow = 0; nRow < pBar->lHeight; nRow++) {
    if (nValue > 0) {
      _bmp_pen = gbHpBarBytes_g[pbColors[nRow + 1]];
      bmp_hline(nX, y, nValue);
    }
    if (nRest > 0) {
      _bmp_pen = gbHpBarBytes_g[byte_5085EC[nRow]];
      bmp_hline(nXRest, y, nRest);
    }
    y++;
  }
  nTick = nX;
  _bmp_pen = gbHpBarBytes_g[18];
  nHeight = pBar->lHeight;
  if (pBar->lStart != 0 && pBar->lStart % 3 != 0)
    nTick += 3 - pBar->lStart % 3;
  for (; nTick < nX + pBar->lWidth; nTick += 3)
    bmp_vline(nTick, nY, nHeight);
}

// sub_4649A0 for the seven-row bar, over its own unfilled ramp.
void sub_464AB0(int nX, int nY, hpbar_t *pBar, int nValue, const BYTE *pbColors)
{
  int nRest;
  int nXRest;
  int nRow;
  int y;
  int nTick;
  int nHeight;

  if (pBar->lHeight != 7)
    return;

  nRest = pBar->lWidth;
  nValue -= pBar->lStart;
  if (nValue > nRest)
    nValue = nRest;
  else if (nValue < 0)
    nValue = 0;
  nXRest = nX + nValue;
  nRest -= nValue;
  y = nY;
  for (nRow = 0; nRow < pBar->lHeight; nRow++) {
    if (nValue > 0) {
      _bmp_pen = gbHpBarBytes_g[pbColors[nRow + 1]];
      bmp_hline(nX, y, nValue);
    }
    if (nRest > 0) {
      _bmp_pen = gbHpBarBytes_g[byte_50860C[nRow]];
      bmp_hline(nXRest, y, nRest);
    }
    y++;
  }
  nTick = nX;
  _bmp_pen = gbHpBarBytes_g[18];
  nHeight = pBar->lHeight;
  if (pBar->lStart != 0 && pBar->lStart % 3 != 0)
    nTick += 3 - pBar->lStart % 3;
  for (; nTick < nX + pBar->lWidth; nTick += 3)
    bmp_vline(nTick, nY, nHeight);
}

// Draw the four-row energy bar. Its left end is tapered when the bar starts at zero: each
// row loses one or two pixels and then gets a two-pixel cap in the tick colour, which is
// what rounds the corner.
void sub_464BC0(int nX, int nY, hpbar_t *pBar, int nValue)
{
  int nRest;
  int nXRest;
  int nRow;
  int nStep;
  int x;
  int nTick;
  int nHeight;

  if (pBar->lHeight != 7)
    return;

  nValue -= pBar->lStart;
  nRest = pBar->lWidth;
  if (nValue > nRest)
    nValue = nRest;
  else if (nValue < 0)
    nValue = 0;
  x = nX;
  nXRest = nX + nValue;
  nRest = pBar->lWidth - nValue;
  for (nRow = 0; nRow < 4; nRow++) {
    if (nValue > 0) {
      _bmp_pen = gbHpBarBytes_g[byte_508614[nRow]];
      bmp_hline(x, nY + nRow, nValue);
    }
    if (nRest > 0) {
      _bmp_pen = gbHpBarBytes_g[byte_508618[nRow]];
      bmp_hline(nXRest, nY + nRow, nRest);
    }
    if (pBar->lStart == 0) {
      if (nValue != 0) {
        nStep = (nRow != 1) + 1;
        nValue -= nStep;
        x += nStep;
      } else if (nRest != 0) {
        nStep = (nRow != 1) + 1;
        nRest -= nStep;
        nXRest += nStep;
      }
    }
  }
  nTick = nX;
  _bmp_pen = gbHpBarBytes_g[18];
  nHeight = pBar->lHeight;
  if (nHeight > 4)
    nHeight = 4;
  if (pBar->lStart != 0 && pBar->lStart % 3 != 0)
    nTick += 3 - pBar->lStart % 3;
  nTick += 3;
  for (; nTick < nX + pBar->lWidth; nTick += 3)
    bmp_vline(nTick, nY, nHeight);
  if (pBar->lStart == 0) {
    for (nRow = 0; nRow < 4; nRow++) {
      bmp_hline(nX, nY + nRow, 2);
      nX += (nRow != 1) ? 2 : 1;
    }
  }
}

// Draw one unit's floating bar stack: hit points, shields when the type has them, and an
// energy or time-to-live bar below for a spellcaster, hallucination or scarab the local
// player owns.
void rle_draw_hpfloat(int nX, int nY, int nUnused, hpbar_t *pBar, CUnit *pUnit)
{
  hpbar_t bar;
  const BYTE *pbRow;
  int nOut;
  int nShieldOut;
  int nMax;
  int nCur;
  int nPct;
  int nSpell;
  int nHalluc;
  WORD wType;
  BYTE bThick;

  bar.lStart = pBar->lStart;
  bar.lWidth = pBar->lWidth;
  bar.lTop = pBar->lTop;
  if (gbUnitHasShields[pUnit->wType] == 0) {
    bar.lHeight = pBar->lHeight;
    if (bar.lHeight > 5)
      bar.lHeight = 5;
    nMax = HandicapLife(gxUnitLife[pUnit->wType], pUnit->bPlayer) >> 8;  // Hyperion: at the owner's handicap
    nCur = (pUnit->lHitPoints + 255) >> 8;
    bThick = sprite_healthbarSize[pUnit->pSprite->wSpriteID - 130];
    if (nMax == 0)
      nMax = (nCur != 0) ? nCur : 1;
    nPct = sub_4648A0(nMax, nCur, bThick, &nOut);
    pbRow = byte_5085D0;
    while (nPct < pbRow[0])
      pbRow += 6;
    sub_4649A0(nX, nY, &bar, nOut, pbRow);
    bThick = 6;
  } else {
    bar.lHeight = pBar->lHeight;
    if (bar.lHeight > 7)
      bar.lHeight = 7;
    nMax = HandicapLife(gxUnitLife[pUnit->wType], pUnit->bPlayer) >> 8;  // Hyperion: at the owner's handicap
    nCur = (pUnit->lHitPoints + 255) >> 8;
    bThick = sprite_healthbarSize[pUnit->pSprite->wSpriteID - 130];
    if (nMax == 0)
      nMax = (nCur != 0) ? nCur : 1;
    nPct = sub_4648A0(nMax, nCur, bThick, &nOut);
    // The thickness is read from the table again, not from bThick.
    sub_464920(HandicapShields(guwUnitEnergy[pUnit->wType], pUnit->bPlayer),  // Hyperion:
               static_cast<LONG>(pUnit->lShields) >> 8,
               sprite_healthbarSize[pUnit->pSprite->wSpriteID - 130], &nShieldOut);
    pbRow = byte_5085F4;
    while (nPct < pbRow[0])
      pbRow += 8;
    sub_464AB0(nX, nY, &bar, nOut, pbRow);
    sub_464BC0(nX, nY, &bar, nShieldOut);
    bThick = 8;
  }
  wType = pUnit->wType;
  nSpell = gulUnitTypeFlags[wType] & 0x200000;
  nHalluc = (pUnit->dwFlags >> 30) & 1;
  if ((nSpell != 0 && (nHalluc = (pUnit->dwFlags >> 30) & 1) == 0) ||
      (nHalluc = (pUnit->dwFlags >> 30) & 1) != 0 || wType == 0x28) {
    if (pUnit->bPlayer != g_LocalNationID)
      return;
    if (nSpell != 0 && nHalluc == 0)
      nMax = pUnit->GetMaxEnergyReal();
    else
      nMax = static_cast<WORD>(pUnit->GetMaxTimeToLive());
    if ((gulUnitTypeFlags[pUnit->wType] & 0x200000) != 0 &&
        (pUnit->dwFlags & 0x40000000) == 0)
      nCur = pUnit->wEnergy >> 8;
    else
      nCur = pUnit->wTimeToLive;
    bar.lTop = pBar->lTop + bThick;
    bar.lHeight = pBar->lHeight - bThick;
    if (bar.lHeight <= 0)
      return;
    nPct = sub_464920(nMax, nCur,
                      sprite_healthbarSize[pUnit->pSprite->wSpriteID - 130], &nOut);
    pbRow = byte_5085E4;
    while (nPct < pbRow[0])
      pbRow += 6;
    sub_4649A0(nX, nY + bThick, &bar, nOut, pbRow);
  }
}

// Load the bar's nineteen colour bytes.
void __fastcall hp_float_init()
{
  GameLoadImage(reinterpret_cast<intptr_t>("game\\thpbar.pcx"), 0,
                reinterpret_cast<intptr_t>(gbHpBarBytes_g), 0x13, 0, 0, 0);
}
