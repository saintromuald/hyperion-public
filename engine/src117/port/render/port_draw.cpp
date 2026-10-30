// port_draw.cpp -- 1.17's hand-written assembly drawing routines, which fixed the view's size,
// in C++ at the view's size (port_view.cpp); each reports its call to the frame description
// (docs/modules/render.md; docs/renderer.md, R2a).
//
// draw_cell and rle_creep draw 8x8 cells and creep edges into the scroll ring, a buffer a little
// larger than the view that wraps at its end, and cell_copy_scrollbuf and its _row copy the view
// out of it onto _img_draw_buf; 1.17 fixed a 672x448 ring and a 640x400 view. The distortion
// routines (a cloaked unit, the EMP shockwave) wrap at the size of the bitmap they draw into,
// not at 640x480. The other sprite routines, from plain units to shadows, translucency and a
// warping-in building, share one body, DrawRle (R2b). The game reaches each body by 1.17's name
// (port_names.cpp) or a draw function table in 1.17's data (src117/gen/code_map.cpp), with
// 1.17's conventions; tools/porttest/draw_test.cpp runs 1.17's code and these on the same inputs
// and compares what they draw.
#include <windows.h>
#include <stddef.h>
#include <string.h>
#include "bmp.h"
#include "rect16.h"
#include "grp.h"
#include "port_draw.h"
#include "port_view.h"  // g_portView
#include "port_scene.h"    // the GPU scene's kinds of primitive (port_scene.cpp)
#include "oc_data.h"       // OC_VA, where 1.17's data is
#include "../platform/port_globals117.h"

// Game globals, by their 1.17 addresses (re/globals117.json).
// The ones whose layout changes at 64 bits are externs the build binds to
// their storage.
extern LONG glScrollPixelX;  // 0x543218
extern LONG glScrollPixelY;  // 0x54321C
extern bmp_t _img_draw_buf;                 // 0x6D0B88, the screen buffer
extern bmp_t *_bmp_current;                 // 0x6D0C40, what sprites draw into
#define byte_666460 (reinterpret_cast<const BYTE *>(OC_VA(0x666460u)))  // a colour's brightness, 0 to 8
extern BYTE *gpFontColors;                  // 0x6D3010, a glyph's 8 colours
extern int gbCharWdt;  // 0x6D3014, the glyph's size
extern int gbCharHgt;  // 0x6D3018
extern int gdwRemainingCharSpace_g;  // 0x6D301C, from a row's end to the next
extern int glBltWdt;  // 0x50F2F0, a blit's row, in bytes
extern int glBltHgt;  // 0x50F2F4, its rows
extern int glBltSrcMod;  // 0x50F2F8, from a row's end to the next
extern int glBltDstMod;  // 0x50F2FC
extern BYTE *gpBltSrc;                      // 0x50F300
extern BYTE *gpBltDst;                      // 0x50F304

enum {
  CELL_SIZE = 8,    // a cell of the tileset's art is 8x8
  STRIP_ROWS = 16   // a dirty strip is one row of the 16x16 dirty grid
};

// copy_chunk (0x4059F0): nRows rows of nWidth bytes out of the ring, from nOfs,
// onto pDst. A row that reaches the ring's end goes on from its start. Like the
// original's `rep movsd` it copies whole dwords and moves on from where the
// copy stopped, which only matters for a width, or a split, that is not a
// multiple of 4.
static void DrawCopyFromRing(int nOfs, BYTE *pDst, int nDstPitch, int nRows, int nWidth)
{
  const BYTE *pSrc = _gfpScrollBuf + nOfs;

  do {
    DWORD cb = static_cast<DWORD>(nWidth);

    if (nOfs + nWidth >= g_portView.nRingSize) {
      if (nOfs < g_portView.nRingSize) {
        const int nRest = nOfs + nWidth - g_portView.nRingSize;
        const DWORD cbHead = static_cast<DWORD>(nWidth - nRest) & ~3u;
        memcpy(pDst, pSrc, cbHead);
        pSrc += cbHead;
        pDst += cbHead;
        cb = static_cast<DWORD>(nRest);
      }
      pSrc -= g_portView.nRingSize;
      nOfs -= g_portView.nRingSize;
    }
    cb &= ~3u;
    memcpy(pDst, pSrc, cb);
    pSrc += cb + g_portView.nRingPitch - nWidth;
    pDst += cb + nDstPitch - nWidth;
    nOfs += g_portView.nRingPitch;
  } while (--nRows != 0);
}

// cell_copy_scrollbuf (0x405A43): the whole view, from the ring at the scroll
// position onto the top of the screen buffer.
void PortCellCopyScrollbuf(void)
{
  const int nWidth = static_cast<WORD>(_img_draw_buf.nWidth);
  int nOfs = static_cast<int>(static_cast<DWORD>(glScrollPixelY) * g_portView.nRingPitch +
                              static_cast<DWORD>(glScrollPixelX));

  while (nOfs >= g_portView.nRingSize)
    nOfs -= g_portView.nRingSize;
  DrawCopyFromRing(nOfs, _img_draw_buf.pPixels, nWidth, g_portView.nMapRows, nWidth);
}

// cell_copy_scrollbuf_row (0x405AAD): one run of dirty cells, a strip 16 rows
// high and nWidth wide, from nOfs in the ring to row nRow, column nX of the
// screen buffer. The original finds the first row with the buffer's width and
// steps to the next ones by 640, which is the same width.
void __fastcall PortCellCopyScrollbufRow(int nRow, int nX, int nWidth, int nOfs)
{
  const DWORD dwPitch = static_cast<WORD>(_img_draw_buf.nWidth);
  BYTE *pDst = _img_draw_buf.pPixels + nX + dwPitch * static_cast<DWORD>(nRow);

  DrawCopyFromRing(nOfs, pDst, static_cast<int>(dwPitch), STRIP_ROWS, nWidth);
}

// draw_cell (0x405AF0): cell nCell >> 1 of the tileset into the ring at nDest,
// mirrored left to right when bit 0 is set. Each row's offset is brought back
// into the ring once, so a cell may start past the end. The original also
// carries two vertically mirrored forms that no path reaches.
void __fastcall PortDrawCell(int nCell, int nDest)
{
  const BYTE *pSrc = gfpCellBuf + (static_cast<size_t>(nCell & ~1) << 5);
  BYTE *const pRing = _gfpScrollBuf;
  int nRow;
  int i;

  for (nRow = CELL_SIZE; nRow != 0; --nRow) {
    BYTE *pDst;

    if (nDest >= g_portView.nRingSize)
      nDest -= g_portView.nRingSize;
    pDst = pRing + nDest;
    if (nCell & 1) {
      for (i = 0; i < CELL_SIZE; ++i)
        pDst[i] = pSrc[CELL_SIZE - 1 - i];
    } else {
      memcpy(pDst, pSrc, CELL_SIZE);
    }
    pSrc += CELL_SIZE;
    nDest += g_portView.nRingPitch;
  }
}

// rle_creep (0x405D40): a creep edge frame into the ring at nDest plus the frame's offset;
// creep.cpp hands over the frame's header in the edge group, whose second byte, unread
// elsewhere, is the Y offset. The data starts with each line's offset; a line runs codes to the
// width: 0x80 and up skips its low 7 bits of pixels, 0x40 and up repeats the next byte its low 6
// bits of times, lower copies that many bytes. The offset is brought back into the ring at the
// start and after each line, not within one; a zero count (no frame has one) runs 1.17's away.
void __fastcall PortRleCreep(const BYTE *pRecord, int nDest)
{
  const FRAME *const pHeader = reinterpret_cast<const FRAME *>(pRecord);
  BYTE *const pRing = _gfpScrollBuf;
  const int nWidth = pHeader->bWidth;
  const int nRowMod = g_portView.nRingPitch - nWidth;
  int nLine = 0;
  int nPos = nDest + pHeader->_unmapped_01 * g_portView.nRingPitch + pHeader->bOffsetX;
  int nRows = pHeader->bHeight;

  if (nPos >= g_portView.nRingSize)
    nPos -= g_portView.nRingSize;
  do {
    const BYTE *pCode = GrpFrameRow(pHeader, nLine++);
    BYTE *pDst = pRing + nPos;
    int nLeft = nWidth;

    do {
      int n = *pCode++;

      if (n & 0x80) {
        n &= 0x7F;
      } else if (n & 0x40) {
        n &= 0x3F;
        memset(pDst, *pCode++, n);
      } else {
        memcpy(pDst, pCode, n);
        pCode += n;
      }
      pDst += n;
      nLeft -= n;
    } while (nLeft > 0);
    nPos = static_cast<int>(pDst - pRing) + nRowMod;
    if (nPos >= g_portView.nRingSize)
      nPos -= g_portView.nRingSize;
  } while (--nRows != 0);
}

// ---------------------------------------------------------------------------
// Distortion: sprites drawn from the screen beside them.

// The sprite clip the rle_draw routines take: the first column and row of the
// frame to draw, and how many of each.
struct DrawClip {
  int nLeft;
  int nTop;
  int nWidth;
  int nHeight;
};

// What the start of a line's clip leaves half done: part of a code to draw.
enum DrawPending {
  PENDING_NONE,
  PENDING_COPY,
  PENDING_RUN
};

// Skips nSkip pixels of a line's codes for the clip. A skip that ends inside a
// code leaves the rest of it: a transparent one moves *pnPos by the rest (to
// the left when bMirror) and takes it off *pnLeft; a copy or a run is left
// pending with *pn pixels, and *ppCode on its first byte to draw or its value.
// The original's loop, which starts only when nSkip is not 0.
static DrawPending DrawSkipCodes(const BYTE **ppCode, int nSkip, bool bMirror, int *pnPos, int *pnLeft,
                                 int *pn)
{
  const BYTE *pCode = *ppCode;
  DrawPending pending = PENDING_NONE;

  while (nSkip != 0) {
    const int c = *pCode++;
    if (c & 0x80) {
      nSkip -= c & 0x7F;
      if (nSkip < 0) {
        *pnPos += bMirror ? nSkip : -nSkip;
        *pnLeft += nSkip;
        break;
      }
    } else if (c & 0x40) {
      ++pCode;
      nSkip -= c & 0x3F;
      if (nSkip < 0) {
        --pCode;
        *pn = -nSkip;
        pending = PENDING_RUN;
        break;
      }
    } else {
      pCode += c;
      nSkip -= c;
      if (nSkip < 0) {
        pCode += nSkip;
        *pn = -nSkip;
        pending = PENDING_COPY;
        break;
      }
    }
  }
  *ppCode = pCode;
  return pending;
}

// The frame's width as a mirrored draw skips it: classic reads the width into the low byte of
// the register holding the clip's top row, so a top row past 255 keeps its high bits in it.
// No frame of classic's is that tall, so it never does; a frame of a mod's sheet past 255 a
// side has its own width.
static int MirrorWidth(const FRAME *pHeader, const DrawClip *pClip)
{
  if (pHeader->bWidth > 0xFF || pHeader->bHeight > 0xFF)
    return pHeader->bWidth;
  return (pClip->nTop & ~0xFF) | pHeader->bWidth;
}

// The distortion: each opaque pixel of the frame takes the target's pixel a distance to its
// right (left when mirrored), the distance the frame's own pixel (the EMP shockwave, draw
// function 8) or its brightness in byte_666460, 0 to 8 (a cloaked unit, function 3), wrapping at
// the target's size, which 1.17 fixes at 640x480. Each clipped line skips the clip's left
// columns, maybe inside a code, and cuts its last code short; a mirrored frame draws leftward
// from the clip's right edge, skipping the frame's width less the clip's left and width. 1.17
// reads that width into the low byte of the register holding the clip's top row, so a top row
// past 255 keeps its high bits in the skip (MirrorWidth).
static void DrawDistort(int nX, int nY, const FRAME *pHeader, const DrawClip *pClip, const BYTE *pBrightness,
                        bool bMirror)
{
  const bmp_t *const pBmp = _bmp_current;
  BYTE *const pBits = pBmp->pPixels;
  const DWORD dwPitch = static_cast<WORD>(pBmp->nWidth);
  const int nSize = static_cast<int>(dwPitch * static_cast<WORD>(pBmp->nHeight));
  int nLine = pClip->nTop;
  const int nRowMod = bMirror ? static_cast<int>(dwPitch) + pClip->nWidth : static_cast<int>(dwPitch) - pClip->nWidth;
  const int nSkip = bMirror ? MirrorWidth(pHeader, pClip) - pClip->nWidth - pClip->nLeft : pClip->nLeft;
  int nPos = static_cast<int>(nX + dwPitch * static_cast<DWORD>(nY)) + (bMirror ? pClip->nWidth : 0);
  int nRows = pClip->nHeight;

  while (--nRows >= 0) {
    const BYTE *pCode = GrpFrameRow(pHeader, nLine++);
    int nLeft = pClip->nWidth;
    int n = 0;
    DrawPending pending = DrawSkipCodes(&pCode, nSkip, bMirror, &nPos, &nLeft, &n);

    for (;;) {
      bool bRun;

      if (pending == PENDING_NONE) {
        if (nLeft <= 0)
          break;
        const int c = *pCode++;
        if (c & 0x80) {
          n = c & 0x7F;
          nLeft -= n;
          if (nLeft < 0)
            n += nLeft;
          nPos += bMirror ? -n : n;
          continue;
        }
        bRun = (c & 0x40) != 0;
        n = bRun ? c & 0x3F : c;
      } else {
        bRun = pending == PENDING_RUN;
        pending = PENDING_NONE;
      }
      nLeft -= n;
      if (nLeft < 0)
        n += nLeft;
      if (bRun) {
        // One distance for the whole run; the source steps along with it.
        const int nValue = *pCode++;
        const int nShift = pBrightness ? pBrightness[nValue] : nValue;
        if (bMirror) {
          int nSrc = nPos - nShift;
          do {
            --nSrc;
            --nPos;
            if (nSrc < 0)
              nSrc += nSize;
            pBits[nPos] = pBits[nSrc];
          } while (--n != 0);
        } else {
          int nSrc = nPos + nShift;
          do {
            if (nSrc >= nSize)
              nSrc -= nSize;
            pBits[nPos++] = pBits[nSrc++];
          } while (--n != 0);
        }
      } else {
        do {
          const int nValue = *pCode++;
          const int nShift = pBrightness ? pBrightness[nValue] : nValue;
          int nSrc;
          if (bMirror) {
            --nPos;
            nSrc = nPos - nShift;
            if (nSrc < 0)
              nSrc += nSize;
            pBits[nPos] = pBits[nSrc];
          } else {
            nSrc = nPos + nShift;
            if (nSrc >= nSize)
              nSrc -= nSize;
            pBits[nPos++] = pBits[nSrc];
          }
        } while (--n != 0);
      }
    }
    nPos += nRowMod;
  }
}

// rle_draw_invis_asm (0x4063B5) and rle_draw_invis_hflip_asm (0x406C09): a
// cloaked unit, draw function 3.
void __fastcall PortRleDrawInvis(int nX, int nY, const void *pGraphic, const void *pClip, DWORD_PTR dwUnused)
{
  (void)dwUnused;
  if (PortSceneRle(SCENE_PRIM_DISTORT, nX, nY, pGraphic, pClip, byte_666460, 256, 0))
    return;
  DrawDistort(nX, nY, static_cast<const FRAME *>(pGraphic), static_cast<const DrawClip *>(pClip), byte_666460,
              false);
}

void __fastcall PortRleDrawInvisHflip(int nX, int nY, const void *pGraphic, const void *pClip, DWORD_PTR dwUnused)
{
  (void)dwUnused;
  if (PortSceneRle(SCENE_PRIM_DISTORT | SCENE_PRIM_MIRROR, nX, nY, pGraphic, pClip, byte_666460, 256, 0))
    return;
  DrawDistort(nX, nY, static_cast<const FRAME *>(pGraphic), static_cast<const DrawClip *>(pClip), byte_666460,
              true);
}

// rle_draw_emp_asm (0x406533) and rle_draw_emp_hflip_asm (0x406D8E): the EMP shockwave, draw
// function 8.
void __fastcall PortRleDrawEmp(int nX, int nY, const void *pGraphic, const void *pClip, DWORD_PTR dwUnused)
{
  (void)dwUnused;
  if (PortSceneRle(SCENE_PRIM_DISTORT, nX, nY, pGraphic, pClip, NULL, 0, 0))
    return;
  DrawDistort(nX, nY, static_cast<const FRAME *>(pGraphic), static_cast<const DrawClip *>(pClip), NULL, false);
}

void __fastcall PortRleDrawEmpHflip(int nX, int nY, const void *pGraphic, const void *pClip, DWORD_PTR dwUnused)
{
  (void)dwUnused;
  if (PortSceneRle(SCENE_PRIM_DISTORT | SCENE_PRIM_MIRROR, nX, nY, pGraphic, pClip, NULL, 0, 0))
    return;
  DrawDistort(nX, nY, static_cast<const FRAME *>(pGraphic), static_cast<const DrawClip *>(pClip), NULL, true);
}

// ---------------------------------------------------------------------------
// Bitmap copies: bmp_copy_rect and bmp_mask_rect (C++ in src117/lang) clip with
// img_clipped, load the blit globals and call one of these.

// img_clipped (0x4CFE60): clips a copy of pSrc->rc, an inclusive rectangle of a bitmap
// pSrc->nWidth by pSrc->nHeight, to a bitmap pDst->nWidth by pDst->nHeight at pDst->rc's top
// left, rewriting both blocks in place: pSrc->rc the part to copy, pSrc's size its width and
// height, pDst->rc's top left where it goes. Answers 1, some fields changed, when nothing is
// left to copy. All in 16 bits, as 1.17: a sum past a short's range wraps.
int __fastcall PortImgClipped(bmpsrc_t *pSrc, bmpsrc_t *pDst)
{
  const short nLeft = pSrc->rc.left;
  short nOver;

  if (nLeft >= pSrc->nWidth || pSrc->rc.top >= pSrc->nHeight || pSrc->rc.right < 0 || pSrc->rc.bottom < 0)
    return 1;
  if (nLeft < 0) {
    pDst->rc.left = static_cast<short>(pDst->rc.left - nLeft);
    pSrc->rc.left = 0;
  }
  if (pSrc->rc.right >= pSrc->nWidth)
    pSrc->rc.right = static_cast<short>(pSrc->nWidth - 1);
  if (pSrc->rc.top < 0) {
    pDst->rc.top = static_cast<short>(pDst->rc.top - pSrc->rc.top);
    pSrc->rc.top = 0;
  }
  if (pSrc->rc.bottom >= pSrc->nHeight)
    pSrc->rc.bottom = static_cast<short>(pSrc->nHeight - 1);

  if (pDst->rc.left < 0) {
    pSrc->rc.left = static_cast<short>(pSrc->rc.left - pDst->rc.left);
    pDst->rc.left = 0;
  } else if (pDst->rc.left >= pDst->nWidth) {
    return 1;
  }
  if (pDst->rc.top < 0) {
    pSrc->rc.top = static_cast<short>(pSrc->rc.top - pDst->rc.top);
    pDst->rc.top = 0;
  } else if (pDst->rc.top >= pDst->nHeight) {
    return 1;
  }

  pDst->rc.right = static_cast<short>(pSrc->rc.right - pSrc->rc.left + pDst->rc.left);
  pDst->rc.bottom = static_cast<short>(pSrc->rc.bottom - pSrc->rc.top + pDst->rc.top);
  if (pDst->rc.right < 0 || pDst->rc.bottom < 0)
    return 1;

  // How far the copy runs past the destination's right and bottom edges; the
  // original leaves it in pDst->rc's right and bottom.
  nOver = static_cast<short>(pDst->rc.right - pDst->nWidth + 1);
  pDst->rc.right = nOver;
  if (nOver > 0)
    pSrc->rc.right = static_cast<short>(pSrc->rc.right - nOver);
  nOver = static_cast<short>(pDst->rc.bottom - pDst->nHeight + 1);
  pDst->rc.bottom = nOver;
  if (nOver > 0)
    pSrc->rc.bottom = static_cast<short>(pSrc->rc.bottom - nOver);

  pSrc->nHeight = static_cast<short>(pSrc->rc.bottom - pSrc->rc.top + 1);
  pSrc->nWidth = static_cast<short>(pSrc->rc.right - pSrc->rc.left + 1);
  return 0;
}

// bmp_copy_rect_asm (0x4CFF90): glBltHgt rows of glBltWdt bytes from gpBltSrc
// to gpBltDst, each row as whole dwords and then the bytes left, forward, as
// the original's `rep movsd` and `rep movsb` (which only matters where the two
// overlap). Nothing in 1.17 calls it: bmp_copy_rect copies through Storm. The
// original runs away on 0 rows; this copies none.
void PortBmpCopyRect(void)
{
  const BYTE *pSrc = gpBltSrc;
  BYTE *pDst = gpBltDst;
  const int nWidth = glBltWdt;
  int nRows = glBltHgt;

  while (nRows-- > 0) {
    int i;
    for (i = 0; i + 4 <= nWidth; i += 4) {
      DWORD dw;
      memcpy(&dw, pSrc, 4);
      memcpy(pDst, &dw, 4);
      pSrc += 4;
      pDst += 4;
    }
    for (; i < nWidth; ++i)
      *pDst++ = *pSrc++;
    pSrc += glBltSrcMod;
    pDst += glBltDstMod;
  }
}

// bmp_mask_rect_asm (0x4CFFD7): the same copy, of the pixels that are not 0
// only. The original runs away on a width or a height of 0; img_clipped never
// gives either, and this copies nothing.
void PortBmpMaskRect(void)
{
  const BYTE *pSrc = gpBltSrc;
  BYTE *pDst = gpBltDst;
  const int nWidth = glBltWdt;
  int nRows = glBltHgt;

  while (nRows-- > 0) {
    int i;
    for (i = 0; i < nWidth; ++i) {
      if (pSrc[i] != 0)
        pDst[i] = pSrc[i];
    }
    pSrc += nWidth + glBltSrcMod;
    pDst += nWidth + glBltDstMod;
  }
}

// ---------------------------------------------------------------------------
// Text.

// draw_char (0x4CFDF0): one glyph at pDest, gbCharWdt by gbCharHgt, rows
// gdwRemainingCharSpace_g apart past its width; font_puts sets the three and gpFontColors. Each
// byte is a skip in its top 5 bits and a colour in its low 3. A skip passes over that many
// pixels, on into the next rows; a colour after a skip draws only when not 0, one with no skip
// always, and a skip ending at a row's end draws its colour at the next row's start. A width or
// height of 0, which no glyph has, runs 1.17's loop away, unchecked here too.
void __fastcall PortDrawChar(BYTE *pDest, const BYTE *pBits)
{
  const BYTE *const pColors = gpFontColors;
  const int nWidth = gbCharWdt;
  const int nRowMod = gdwRemainingCharSpace_g;
  int nRows = gbCharHgt;
  int nLeft = nWidth;

  if (PortSceneGlyph(pDest, pBits, nWidth, nRows, nRowMod, pColors))
    return;

  for (;;) {
    const int c = *pBits++;
    int nSkip = c >> 3;
    const int nColor = c & 7;

    if (nSkip != 0) {
      while (nSkip >= nLeft) {
        pDest += nLeft + nRowMod;
        nSkip -= nLeft;
        nLeft = nWidth;
        if (--nRows == 0)
          return;
      }
      pDest += nSkip;
      nLeft -= nSkip;
      if (nColor == 0)
        continue;
    }
    *pDest++ = pColors[nColor];
    if (--nLeft == 0) {
      pDest += nRowMod;
      nLeft = nWidth;
      if (--nRows == 0)
        return;
    }
  }
}

// ---------------------------------------------------------------------------
// The placement grid.

// shade_mtx (0x405C12): one 32x32 cell of the building placement grid at nX,
// nY in pBmp, shaded in a checkerboard: in each row every other pixel takes
// the low byte of dwPen (placebox passes a pen colour), starting one pixel
// further right on every next row, and the pixels between keep theirs, ORed
// with dwPen's second byte (0 from placebox). Done a dword at a time with a
// mask and a fill that turn a byte each row, as the original does.
void __fastcall PortShadeMtx(int nX, int nY, const void *pBitmap, DWORD dwPen)
{
  const bmp_t *const pBmp = static_cast<const bmp_t *>(pBitmap);
  const DWORD dwWidth = static_cast<WORD>(pBmp->nWidth);
  BYTE *pDst = pBmp->pPixels + static_cast<DWORD>(nX) + dwWidth * static_cast<DWORD>(nY);
  const int nRowMod = static_cast<int>(dwWidth) - 32;
  DWORD dwKeep = 0xFF00FF00u;
  DWORD dwFill = (dwPen << 16) | (dwPen & 0xFFFFu);
  int nRow;
  int i;

  for (nRow = 0; nRow < 32; ++nRow) {
    for (i = 0; i < 8; ++i) {
      DWORD dw;
      memcpy(&dw, pDst, 4);
      dw = (dw & dwKeep) | dwFill;
      memcpy(pDst, &dw, 4);
      pDst += 4;
    }
    dwKeep = (dwKeep >> 8) | (dwKeep << 24);
    dwFill = (dwFill >> 8) | (dwFill << 24);
    pDst += nRowMod;
  }
}

// ---------------------------------------------------------------------------
// Sprites: the other draw functions of the image table at 0x5030D0.

extern BYTE _gbRemapColorTbl[256] OC_UNALIGNED;  // 0x501021, colour to colour, per image
#define shiftDark_g (reinterpret_cast<const BYTE (*)[256]>(OC_VA(0x66DD20u)))  // 32 shades of darkness

// How a routine goes on when the clip's left edge ends inside a transparent
// code, with part of it, the leftover, inside the clip. Each of the originals
// does it its own way, and the way shows in where the rest of the sprite lands.
enum DrawOverflow {
  // Moves on by the leftover; when that uses up the clip's width, moves back
  // by the excess, so the next row starts in its place (rle_draw_normal).
  OVERFLOW_BACK,
  // Moves on by the leftover and does not move back: a leftover wider than the
  // clip moves every row after it by the excess (the other routines).
  OVERFLOW_STAY,
  // The leftover is skipped as a transparent code of the drawing, cut at the
  // clip's width (rle_mask_warp_flash).
  OVERFLOW_SKIP,
  // Moves on by the leftover, then skips the whole code again as a transparent
  // code of the drawing (rle_mask_warp_flash_hflip).
  OVERFLOW_TWICE
};

// The six ways a sprite's opaque pixel is drawn. Pixel draws one at pDst from
// the frame's byte v; Skip passes over n transparent ones.
struct DrawOpNormal {  // draw function 0: the frame's colour through the remap
  void Pixel(BYTE *pDst, int v) { *pDst = _gbRemapColorTbl[v]; }
  void Skip(int) {}
};
struct DrawOpShadow {  // 10: the screen darkened
  void Pixel(BYTE *pDst, int) { *pDst = shiftDark_g[18][*pDst]; }
  void Skip(int) {}
};
struct DrawOpEffect {  // translucency: the frame's colour over the screen's
  const BYTE *pTable;  // 256 x 256
  void Pixel(BYTE *pDst, int v) { *pDst = pTable[(v << 8) | *pDst]; }
  void Skip(int) {}
};
struct DrawOpFade {  // the screen's colour through a table
  const BYTE *pTable;
  void Pixel(BYTE *pDst, int) { *pDst = pTable[*pDst]; }
  void Skip(int) {}
};
struct DrawOpUncloaked {  // a unit cloaking or decloaking: its brighter colours
  int nThreshold;         // a signed byte
  void Pixel(BYTE *pDst, int v)
  {
    const BYTE c = _gbRemapColorTbl[v];
    if (static_cast<signed char>(byte_666460[c]) > nThreshold)
      *pDst = c;
  }
  void Skip(int) {}
};
struct DrawOpWarpFlash {  // a warping-in building: the frame's shape filled from a texture
  const BYTE *pTexture;   // read on through the clip's pixels, row after row
  void Pixel(BYTE *pDst, int) { *pDst = *pTexture++; }
  void Skip(int n) { pTexture += n; }
};

// Draws the clip of a frame's codes (rle_creep's) at nX, nY in _bmp_current,
// each opaque pixel through op; mirrored left to right when bMirror, from the
// clip's right edge leftward, the skip then being the columns right of the
// clip (DrawDistort's, with the same carry of the clip's top into it). A code
// cut by the clip's left edge draws its part inside; a transparent one goes on
// as eOverflow says.
template <class Op>
static void DrawRle(int nX, int nY, const FRAME *pHeader, const DrawClip *pClip, bool bMirror,
                    DrawOverflow eOverflow, Op &op)
{
  const bmp_t *const pBmp = _bmp_current;
  const DWORD dwPitch = static_cast<WORD>(pBmp->nWidth);
  const int nStep = bMirror ? -1 : 1;
  const int nRowMod = bMirror ? static_cast<int>(dwPitch) + pClip->nWidth : static_cast<int>(dwPitch) - pClip->nWidth;
  const int nSkip = bMirror ? MirrorWidth(pHeader, pClip) - pClip->nWidth - pClip->nLeft : pClip->nLeft;
  int nLine = pClip->nTop;
  BYTE *pDst = pBmp->pPixels + static_cast<DWORD>(nX) + dwPitch * static_cast<DWORD>(nY) +
               (bMirror ? pClip->nWidth : 0);
  int nRows = pClip->nHeight;

  while (--nRows >= 0) {
    const BYTE *pCode = GrpFrameRow(pHeader, nLine++);
    int nLeft = pClip->nWidth;
    DrawPending pending = PENDING_NONE;
    int nRest = 0;       // what is left of the code the skip ended in
    int nSkipLeft = nSkip;
    bool bSkipCode = false;  // the drawing starts with a transparent code of nRest

    // The clip's left columns.
    while (nSkipLeft != 0) {
      const int c = *pCode++;
      int n;
      if (c & 0x80) {
        n = c & 0x7F;
        nSkipLeft -= n;
        if (nSkipLeft > 0)
          continue;
        if (nSkipLeft < 0) {
          const int nLeftover = -nSkipLeft;
          if (eOverflow == OVERFLOW_SKIP) {
            nRest = nLeftover;
            bSkipCode = true;
          } else {
            pDst += nStep * nLeftover;
            nLeft -= nLeftover;
            if (eOverflow == OVERFLOW_TWICE) {
              nRest = n;
              bSkipCode = true;
            }
          }
        }
        break;
      }
      if (c & 0x40) {
        n = c & 0x3F;
        nSkipLeft -= n;
        if (nSkipLeft > 0) {
          ++pCode;
          continue;
        }
        if (nSkipLeft < 0) {
          nRest = -nSkipLeft;
          pending = PENDING_RUN;
        } else {
          ++pCode;
        }
        break;
      }
      pCode += c;
      nSkipLeft -= c;
      if (nSkipLeft > 0)
        continue;
      if (nSkipLeft < 0) {
        pCode += nSkipLeft;
        nRest = -nSkipLeft;
        pending = PENDING_COPY;
      }
      break;
    }

    if (pending == PENDING_NONE && !bSkipCode && nLeft <= 0) {
      if (eOverflow == OVERFLOW_BACK)
        pDst += nStep * nLeft;
      pDst += nRowMod;
      continue;
    }

    // The clip's width.
    for (;;) {
      int n;
      if (bSkipCode) {
        n = nRest;
        bSkipCode = false;
        goto transparent;
      }
      if (pending == PENDING_COPY) {
        n = nRest;
        pending = PENDING_NONE;
        goto copy;
      }
      if (pending == PENDING_RUN) {
        n = nRest;
        pending = PENDING_NONE;
        goto run;
      }
      {
        const int c = *pCode++;
        if (c & 0x80) {
          n = c & 0x7F;
          goto transparent;
        }
        if (c & 0x40) {
          n = c & 0x3F;
          goto run;
        }
        n = c;
      }
    copy:
      nLeft -= n;
      if (nLeft < 0)
        n += nLeft;
      do {
        if (bMirror)
          op.Pixel(--pDst, *pCode++);
        else
          op.Pixel(pDst++, *pCode++);
      } while (--n != 0);
      goto next;
    run:
      nLeft -= n;
      if (nLeft < 0)
        n += nLeft;
      {
        const int v = *pCode++;
        do {
          if (bMirror)
            op.Pixel(--pDst, v);
          else
            op.Pixel(pDst++, v);
        } while (--n != 0);
      }
      goto next;
    transparent:
      nLeft -= n;
      if (nLeft < 0)
        n += nLeft;
      pDst += nStep * n;
      op.Skip(n);
    next:
      if (nLeft <= 0)
        break;
    }
    pDst += nRowMod;
  }
}

// The entry points, in the original's convention (RleDrawFn): x and y in ECX
// and EDX, the frame, the clip and a word for the draw function on the stack. The
// word is a table's address for some of them, so it is pointer-sized.
#define PORT_RLE_ARGS int nX, int nY, const void *pGraphic, const void *pClip, DWORD_PTR dwArg
#define PORT_RLE_FRAME static_cast<const FRAME *>(pGraphic), static_cast<const DrawClip *>(pClip)

// rle_draw_normal (0x405E1E) and rle_draw_hflip_ (0x4071C0).
void __fastcall PortRleDrawNormal(PORT_RLE_ARGS)
{
  DrawOpNormal op;
  (void)dwArg;
  if (PortSceneRle(SCENE_PRIM_NORMAL, nX, nY, pGraphic, pClip, _gbRemapColorTbl, 256, 0))
    return;
  DrawRle(nX, nY, PORT_RLE_FRAME, false, OVERFLOW_BACK, op);
}
void __fastcall PortRleDrawNormalHflip(PORT_RLE_ARGS)
{
  DrawOpNormal op;
  (void)dwArg;
  if (PortSceneRle(SCENE_PRIM_NORMAL | SCENE_PRIM_MIRROR, nX, nY, pGraphic, pClip, _gbRemapColorTbl, 256, 0))
    return;
  DrawRle(nX, nY, PORT_RLE_FRAME, true, OVERFLOW_STAY, op);
}

// rle_draw_shadow (0x406950) and rle_draw_shadow_hflip (0x40730E).
void __fastcall PortRleDrawShadow(PORT_RLE_ARGS)
{
  DrawOpShadow op;
  (void)dwArg;
  if (PortSceneRle(SCENE_PRIM_SHADOW, nX, nY, pGraphic, pClip, shiftDark_g[18], 256, 0))
    return;
  DrawRle(nX, nY, PORT_RLE_FRAME, false, OVERFLOW_STAY, op);
}
void __fastcall PortRleDrawShadowHflip(PORT_RLE_ARGS)
{
  DrawOpShadow op;
  (void)dwArg;
  if (PortSceneRle(SCENE_PRIM_SHADOW | SCENE_PRIM_MIRROR, nX, nY, pGraphic, pClip, shiftDark_g[18], 256, 0))
    return;
  DrawRle(nX, nY, PORT_RLE_FRAME, true, OVERFLOW_STAY, op);
}

// rle_draw_effect (0x4067F6) and rle_draw_effect_hflip (0x406F03): dwArg is
// the translucency table.
void __fastcall PortRleDrawEffect(PORT_RLE_ARGS)
{
  DrawOpEffect op = {reinterpret_cast<const BYTE *>(dwArg)};
  if (PortSceneRle(SCENE_PRIM_EFFECT, nX, nY, pGraphic, pClip, op.pTable, 65536, 0))
    return;
  DrawRle(nX, nY, PORT_RLE_FRAME, false, OVERFLOW_STAY, op);
}
void __fastcall PortRleDrawEffectHflip(PORT_RLE_ARGS)
{
  DrawOpEffect op = {reinterpret_cast<const BYTE *>(dwArg)};
  if (PortSceneRle(SCENE_PRIM_EFFECT | SCENE_PRIM_MIRROR, nX, nY, pGraphic, pClip, op.pTable, 65536, 0))
    return;
  DrawRle(nX, nY, PORT_RLE_FRAME, true, OVERFLOW_STAY, op);
}

// rle_draw_fade (0x4066A1) and rle_draw_fade_hflip (0x407064): dwArg is the
// colour table.
void __fastcall PortRleDrawFade(PORT_RLE_ARGS)
{
  DrawOpFade op = {reinterpret_cast<const BYTE *>(dwArg)};
  if (PortSceneRle(SCENE_PRIM_FADE, nX, nY, pGraphic, pClip, op.pTable, 256, 0))
    return;
  DrawRle(nX, nY, PORT_RLE_FRAME, false, OVERFLOW_STAY, op);
}
void __fastcall PortRleDrawFadeHflip(PORT_RLE_ARGS)
{
  DrawOpFade op = {reinterpret_cast<const BYTE *>(dwArg)};
  if (PortSceneRle(SCENE_PRIM_FADE | SCENE_PRIM_MIRROR, nX, nY, pGraphic, pClip, op.pTable, 256, 0))
    return;
  DrawRle(nX, nY, PORT_RLE_FRAME, true, OVERFLOW_STAY, op);
}

// rle_draw_uncloaked_part (0x406235) and rle_draw_uncloaked_part_hflip
// (0x406A84): dwArg's low byte is the brightness to draw above.
void __fastcall PortRleDrawUncloakedPart(PORT_RLE_ARGS)
{
  DrawOpUncloaked op = {static_cast<signed char>(dwArg & 0xFF)};
  if (PortSceneRleUncloaked(nX, nY, pGraphic, pClip, _gbRemapColorTbl, byte_666460, op.nThreshold, 0))
    return;
  DrawRle(nX, nY, PORT_RLE_FRAME, false, OVERFLOW_STAY, op);
}
void __fastcall PortRleDrawUncloakedPartHflip(PORT_RLE_ARGS)
{
  DrawOpUncloaked op = {static_cast<signed char>(dwArg & 0xFF)};
  if (PortSceneRleUncloaked(nX, nY, pGraphic, pClip, _gbRemapColorTbl, byte_666460, op.nThreshold, 1))
    return;
  DrawRle(nX, nY, PORT_RLE_FRAME, true, OVERFLOW_STAY, op);
}

// rle_mask_warp_flash (0x405F64) and rle_mask_warp_flash_hflip (0x4060C3):
// dwArg is the texture.
void __fastcall PortRleMaskWarpFlash(PORT_RLE_ARGS)
{
  DrawOpWarpFlash op = {reinterpret_cast<const BYTE *>(dwArg)};
  if (PortSceneRle(SCENE_PRIM_WARP, nX, nY, pGraphic, pClip, op.pTexture,
                   static_cast<const DrawClip *>(pClip)->nWidth * static_cast<const DrawClip *>(pClip)->nHeight, 0))
    return;
  DrawRle(nX, nY, PORT_RLE_FRAME, false, OVERFLOW_SKIP, op);
}
void __fastcall PortRleMaskWarpFlashHflip(PORT_RLE_ARGS)
{
  DrawOpWarpFlash op = {reinterpret_cast<const BYTE *>(dwArg)};
  if (PortSceneRle(SCENE_PRIM_WARP | SCENE_PRIM_MIRROR, nX, nY, pGraphic, pClip, op.pTexture,
                   static_cast<const DrawClip *>(pClip)->nWidth * static_cast<const DrawClip *>(pClip)->nHeight, 0))
    return;
  DrawRle(nX, nY, PORT_RLE_FRAME, true, OVERFLOW_TWICE, op);
}
