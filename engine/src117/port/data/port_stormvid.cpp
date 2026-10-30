// port_stormvid.cpp -- Storm's video player, the port's own: the SVid functions the game
// imports, with Storm's names and shapes, doing what storm.dll's do (docs/modules/data.md,
// "Video"; tools/porttest/storm_test.cpp compares them frame by frame). A video is a Smacker
// file played by port_smacker.cpp; doubling has storm.dll's SCode blitters as plain loops, each
// pixel doubled or, in the cinematics' default mode, blended through a nearest-colour table.
//
// Not kept from storm.dll: handing a new palette to smackw32's buffer too, waiting for the
// vertical blank after each palette change, seeding the blend table from the system palette
// (no classic call asks for those). Storm's blitters took rows of up to 320 pixels, too few
// for a wide view; the loops here have no limit.
//
// A video with no Smacker file may be one in full colour (port_video.h; VidOpenVideo). A
// cinematic in full colour is drawn by the GPU presenter where it is in use, fitted to the
// window (PortVidPicture): a mod's own by the cinematic's name, else Remastered's copy in HD,
// else the one the archives have; on SDL's renderer it is put in the screen's colours as any.
#include <windows.h>
#include <SDL3/SDL.h>  // SDL_getenv
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "../../h/gds_palette.h"
#include "../../h/rect32.h"
#include "../render/port_present.h"
#include "port_smacker.h"
#include "port_subtitles.h"
#include "../render/port_storm.h"
#include "port_stormapi.h"
#include "port_stormvid.h"
#include "port_moddata.h"  // ModDataVideoPath
#include "port_remastered.h"  // PortRemasteredVideoHd
#include "port_video.h"  // PortVideoExtensions

// SVidPlayBegin's flags.
static const DWORD VID_SUBMODE_1 = 0x1;           // with 2 and the doubling, blend the rows in the buffer
static const DWORD VID_BLEND = 0x2;               // blend the doubled pixels; dropped on a dropped frame
static const DWORD VID_HALF_WORK = 0x4;           // set on a dropped frame: blit this frame's and the last one's rects
static const DWORD VID_REG_SUBMODE = 0x8;         // bits 0-2 from the registry
static const DWORD VID_SINGLE = 0x100;            // at its own size
static const DWORD VID_DOUBLE = 0x200;            // at twice its size
static const DWORD VID_REG_SCALE = 0x800;         // bits 8-9 from the registry
static const DWORD VID_HANDLE = 0x10000;          // the name is an open file handle
static const DWORD VID_PRELOAD = 0x20000;         // smackw32 read it all at once
static const DWORD VID_LOOP = 0x40000;
static const DWORD VID_CENTRE_Y = 0x80000;
static const DWORD VID_NO_PALETTE = 0x100000;
static const DWORD VID_CLEAR = 0x200000;
static const DWORD VID_NO_SKIP = 0x400000;
static const DWORD VID_SPLIT_PALETTE = 0x800000;  // the blend table in two halves, and no clear per palette
static const DWORD VID_NO_SOUND = 0x1000000;
static const DWORD VID_OPEN_20 = 0x2000000;
static const DWORD VID_OPEN_40 = 0x4000000;
static const DWORD VID_SCREEN = 0x10000000;
static const DWORD VID_BITMAP = 0x20000000;

// SmackOpen's flags, as storm.dll passes them.
static const DWORD SMACK_PRELOAD = 0x200;
static const DWORD SMACK_NO_SKIP = 0x400;
static const DWORD SMACK_FILE_HANDLE = 0x1000;
static const DWORD SMACK_ALL_TRACKS = 0xFE000;

struct VidVideo {
  VidVideo *pNext;
  HANDLE hFile;              // opened here and closed at the end, or INVALID_HANDLE_VALUE
  SmkFile *pSmk;
  BYTE *pBuffer;             // the doubling's decode target, nBufferWidth wide
  DWORD nBufferWidth;
  DWORD nCentreY;            // rows down the screen
  BYTE *pDest;               // the caller's bitmap, or NULL for the screen
  rect32_t rcSource;         // the caller's rect, of which only left and top are used
  bool bHasRect;
  DWORD dwPitch, dwHeight;   // the caller's bitmap
  BYTE *pLocked;             // the screen, while it is locked
  DWORD dwFlags;
  bool bMayDouble;           // twice the size fits on the screen
  bool bBlendRows;           // blend the rows in the buffer
  int nRegion;               // which of the two regions is this frame's
  void *ahRegion[2];
  BYTE *pBlend;              // the 64 KB table of blended pairs
  DWORD nPalFirst, nPalCount;
  rect32_t *pRects;
  ULONG nRects, nRectsMax;
  char szFile[MAX_PATH];     // the name it was opened by, or empty for a handle
  // a video larger than the bitmap it is drawn in (a mod's portrait): each frame
  // decoded here at its own size, then drawn smaller into the fit, centred
  BYTE *pFull;
  DWORD nFitX, nFitY, nFitW, nFitH;
  bool bPicture;  // a cinematic drawn in full colour by the GPU presenter (VidDrawPicture)
  DWORD nShowW, nShowH;  // the shape it is shown at
  char szOwn[MAX_PATH];  // a mod's own copy of a cinematic, when it plays that
};

static bool g_bVidReady;             // SVidInitialize was called
static VidVideo *g_pVideos;
static PALETTEENTRY g_aPalette[256];  // the palette being handed to the screen
static VidVideo *g_pSubtitled;        // the video on the screen, whose subtitles show

// storm.dll's is a list whose members are the only valid handles.
static VidVideo *VidFind(HANDLE hVid)
{
  for (VidVideo *p = g_pVideos; p; p = p->pNext)
    if (p == hVid)
      return p;
  return NULL;
}

bool PortVidPlaying(void *hVid, const char **ppszFile, unsigned *pnFrame, unsigned *pnInterval)
{
  const VidVideo *p = hVid ? VidFind(static_cast<HANDLE>(hVid)) : NULL;
  if (p == NULL || p->pSmk == NULL)
    return false;
  *ppszFile = p->szFile;
  *pnFrame = SmkFrameNum(p->pSmk);
  *pnInterval = SmkInterval(p->pSmk);
  return true;
}

// ---------------------------------------------------------------------------
// storm.dll's three SCode programs for the doubling. A block is rows of nWidth
// steps; after each row the source and destination move on by the skips, and
// with bAlternate by the second pair on the rows an odd number from the end.

struct VidBlock {
  int nWidth, nHeight;
  int nSkipDest, nSkipSrc;
  BYTE *pDest;
  const BYTE *pSrc;
  const BYTE *pTable;
  int nBelow;                // the next row, for VidBlendBelow
  int nSkipDest2, nSkipSrc2;
};

static bool VidNextRow(VidBlock &b, int &nRows, bool bAlternate, BYTE *&pDest, const BYTE *&pSrc)
{
  const bool bOdd = (nRows & 1) != 0;
  if (--nRows == 0)
    return false;
  pSrc += bAlternate && bOdd ? b.nSkipSrc2 : b.nSkipSrc;
  pDest += bAlternate && bOdd ? b.nSkipDest2 : b.nSkipDest;
  return true;
}

// "1 W1=S W2=SC D=TW": each pixel blended with the one below it.
static void VidBlendBelow(VidBlock &b)
{
  BYTE *pDest = b.pDest;
  const BYTE *pSrc = b.pSrc;
  int nRows = b.nHeight;
  do {
    for (int x = 0; x < b.nWidth; ++x)
      pDest[x] = b.pTable[pSrc[x] | pSrc[x + b.nBelow] << 8];
    pDest += b.nWidth;
    pSrc += b.nWidth;
  } while (VidNextRow(b, nRows, false, pDest, pSrc));
}

// "1 W2=S W1=W2 2 D=W", then "1 W1=W2 W2=S W1=TW 2 D=W": the row's first
// pixel twice, then each pixel after the blend of it and the one before.
static void VidDoubleBlended(VidBlock &b)
{
  BYTE *pDest = b.pDest;
  const BYTE *pSrc = b.pSrc;
  int nRows = b.nHeight;
  do {
    BYTE bLast = *pSrc++;
    pDest[0] = pDest[1] = bLast;
    pDest += 2;
    for (int x = 0; x < b.nWidth; ++x) {
      const BYTE bPixel = *pSrc++;
      pDest[0] = b.pTable[bLast | bPixel << 8];
      pDest[1] = bPixel;
      pDest += 2;
      bLast = bPixel;
    }
  } while (VidNextRow(b, nRows, false, pDest, pSrc));
}

// "1 W1=S W2=W1 2 D=W", with the rows alternating between the two pairs of
// skips: each pixel twice.
static void VidDouble(VidBlock &b)
{
  BYTE *pDest = b.pDest;
  const BYTE *pSrc = b.pSrc;
  int nRows = b.nHeight;
  do {
    for (int x = 0; x < b.nWidth; ++x) {
      pDest[0] = pDest[1] = *pSrc++;
      pDest += 2;
    }
  } while (VidNextRow(b, nRows, true, pDest, pSrc));
}

// ---------------------------------------------------------------------------
// The table of blended pairs: for each pair of entries from nFirst to nLast,
// the entry nearest the colour halfway between them. The palette is sorted
// into cells of 16 levels a colour, and a pair looks in its own cell, then in
// the 26 around it until one is near enough, as storm.dll does, so that the
// same entry wins a tie.

static const signed char s_aNeighbours[26][3] = {
    {-1, 0, 0},  {0, -1, 0}, {0, 0, -1}, {1, 0, 0},    {0, 1, 0},   {0, 0, 1},    {-1, 1, 0},
    {-1, 0, 1},  {1, -1, 0}, {0, -1, 1}, {1, 0, -1},   {0, 1, -1},  {-1, -1, 0},  {-1, 0, -1},
    {0, -1, -1}, {1, 1, 0},  {1, 0, 1},  {0, 1, 1},    {-1, -1, 1}, {-1, 1, -1},  {1, -1, -1},
    {1, 1, -1},  {1, -1, 1}, {-1, 1, 1}, {-1, -1, -1}, {1, 1, 1}};

static const int VID_CELLS = 18;  // 16 a colour and one either side

static int VidCell(int r, int g, int b) { return ((r * VID_CELLS) + g) * VID_CELLS + b; }

static void VidBuildBlend(VidVideo *p, const PALETTEENTRY *pPal, int nFirst, int nLast)
{
  if (!p->pBlend)
    p->pBlend = static_cast<BYTE *>(SMemAlloc(0x10000, "port_stormvid.cpp", __LINE__, 8));
  struct Node {
    Node *pNext;
    BYTE bIndex;
  };
  static Node *s_apCell[VID_CELLS * VID_CELLS * VID_CELLS];
  static Node s_aNode[256];
  memset(s_apCell, 0, sizeof s_apCell);
  // later entries go first in their cell; an entry the same as the one before
  // it is left out
  for (int e = nFirst; e <= nLast; ++e) {
    if (e > 0 && pPal[e].peRed == pPal[e - 1].peRed && pPal[e].peGreen == pPal[e - 1].peGreen &&
        pPal[e].peBlue == pPal[e - 1].peBlue)
      continue;
    Node **ppCell = &s_apCell[VidCell((pPal[e].peRed >> 4) + 1, (pPal[e].peGreen >> 4) + 1, (pPal[e].peBlue >> 4) + 1)];
    s_aNode[e].bIndex = static_cast<BYTE>(e);
    s_aNode[e].pNext = *ppCell;
    *ppCell = &s_aNode[e];
  }
  for (int i = nFirst; i <= nLast; ++i) {
    for (int j = nFirst; j < i; ++j) {
      const int r = (pPal[i].peRed + pPal[j].peRed) >> 1;
      const int g = (pPal[i].peGreen + pPal[j].peGreen) >> 1;
      const int b = (pPal[i].peBlue + pPal[j].peBlue) >> 1;
      const int cr = (r >> 4) + 1, cg = (g >> 4) + 1, cb = (b >> 4) + 1;
      BYTE bBest = static_cast<BYTE>(i);
      DWORD dwBest = 0xFFFFFFFF;
      const Node *pNode = s_apCell[VidCell(cr, cg, cb)];
      if (pNode && !pNode->pNext) {
        bBest = pNode->bIndex;
      } else if (pNode) {
        for (; pNode; pNode = pNode->pNext) {
          const PALETTEENTRY &pe = pPal[pNode->bIndex];
          const DWORD d = (pe.peBlue - b) * (pe.peBlue - b) + (pe.peGreen - g) * (pe.peGreen - g) +
                          (pe.peRed - r) * (pe.peRed - r);
          if (d < dwBest) {
            dwBest = d;
            bBest = pNode->bIndex;
          }
        }
      } else {
        for (int k = 0; k < 26 && dwBest > 0x300; ++k) {
          pNode = s_apCell[VidCell(cr + s_aNeighbours[k][0], cg + s_aNeighbours[k][1], cb + s_aNeighbours[k][2])];
          for (; pNode; pNode = pNode->pNext) {
            const PALETTEENTRY &pe = pPal[pNode->bIndex];
            const DWORD d = (pe.peBlue - b) * (pe.peBlue - b) + (pe.peGreen - g) * (pe.peGreen - g) +
                            (pe.peRed - r) * (pe.peRed - r);
            if (d < dwBest) {
              dwBest = d;
              bBest = pNode->bIndex;
            }
          }
        }
      }
      p->pBlend[i << 8 | j] = bBest;
      p->pBlend[j << 8 | i] = bBest;
    }
    p->pBlend[i * 0x101] = static_cast<BYTE>(i);
  }
}

// Each colour to the power 0.8, brighter, for a doubled video not blending its
// rows.
static void VidBrighten(PALETTEENTRY *pPal)
{
  for (int i = 0; i < 256; ++i) {
    BYTE *pc[3] = {&pPal[i].peRed, &pPal[i].peGreen, &pPal[i].peBlue};
    for (int c = 0; c < 3; ++c) {
      const double d = pow(*pc[c] * 0.00390625, 0.8) * 256.0;
      *pc[c] = d > 255.0 ? 255 : static_cast<BYTE>(static_cast<int>(d));
    }
  }
}

// ---------------------------------------------------------------------------
// Drawing.

// Where the frame goes, and the pitch and height there. The screen is locked
// until the frame is drawn.
static bool VidResolve(VidVideo *p, BYTE **ppBase, DWORD adwSize[2])
{
  if (p->dwFlags & VID_SCREEN) {
    adwSize[0] = 640;
    adwSize[1] = 480;
    PortVidScreenSize(&adwSize[0], &adwSize[1]);
    void *pBits = NULL;
    int nPitch = static_cast<int>(adwSize[0]);
    const bool bOk = PortVidLock(&pBits, &nPitch) != 0;
    adwSize[0] = static_cast<DWORD>(nPitch);
    p->pLocked = static_cast<BYTE *>(pBits);
    *ppBase = p->pLocked + p->nCentreY * adwSize[0];
    const DWORD dwCentreW = adwSize[0] < 640 ? adwSize[0] : 640;
    if (!p->bHasRect && !p->pFull && (p->dwFlags & VID_SINGLE) && dwCentreW > SmkWidth(p->pSmk))
      *ppBase += (dwCentreW - SmkWidth(p->pSmk)) >> 1;
    return bOk;
  }
  *ppBase = p->pDest + p->rcSource.top * p->dwPitch + p->rcSource.left;
  adwSize[0] = p->dwPitch;
  adwSize[1] = p->dwHeight;
  return true;
}

static void VidUnlock(VidVideo *p)
{
  if (p->dwFlags & VID_SCREEN) {
    PortVidUnlock();
    p->pLocked = NULL;
  }
}

// At its own size, decoded straight into the destination. Whether the frame was
// dropped.
static DWORD VidDrawDirect(VidVideo *p)
{
  BYTE *pBase;
  DWORD adwSize[2];
  if (!VidResolve(p, &pBase, adwSize))
    return 1;
  if (p->pFull) {
    const DWORD dwWidth = SmkWidth(p->pSmk), dwHeight = SmkHeight(p->pSmk);
    SmkToBuffer(p->pSmk, 0, 0, dwWidth, p->pFull);
    const DWORD dwDropped = SmkDoFrame(p->pSmk);
    for (DWORD y = 0; y < p->nFitH; ++y) {
      BYTE *pRow = pBase + (p->nFitY + y) * adwSize[0] + p->nFitX;
      const BYTE *pSrc = p->pFull + (y * dwHeight / p->nFitH) * dwWidth;
      for (DWORD x = 0; x < p->nFitW; ++x)
        pRow[x] = pSrc[x * dwWidth / p->nFitW];
    }
    VidUnlock(p);
    return dwDropped;
  }
  SmkToBuffer(p->pSmk, 0, 0, adwSize[0], pBase);
  const DWORD dwDropped = SmkDoFrame(p->pSmk);
  VidUnlock(p);
  return dwDropped;
}

// In full colour, by the GPU presenter: the frame's planes handed on and shown.
static DWORD VidDrawPicture(VidVideo *p)
{
  const DWORD dwDropped = SmkDoFrame(p->pSmk);
  if (!dwDropped)
    if (const PortVideoPlanes *pPlanes = SmkVideoPlanes(p->pSmk))
      PortVidPicture(pPlanes, static_cast<int>(p->nShowW), static_cast<int>(p->nShowH));
  return dwDropped;
}

// This frame's changed rects into rgn; with VID_HALF_WORK, the region the frame
// is blitted through is a copy of it with the last frame's rects added, the
// frames taking the two regions in turn. The rects end up in p->pRects.
static void VidGetRects(VidVideo *p, void *hRgn)
{
  ULONG nRects = 0;
  PortRgnGetRects(hRgn, &nRects, NULL);
  if (nRects > p->nRectsMax) {
    if (p->pRects)
      SMemFree(p->pRects, "port_stormvid.cpp", __LINE__, 0);
    p->nRectsMax = nRects;
    p->pRects = static_cast<rect32_t *>(SMemAlloc(nRects * sizeof(rect32_t), "port_stormvid.cpp", __LINE__, 0));
  }
  p->nRects = nRects;
  PortRgnGetRects(hRgn, &p->nRects, p->pRects);
}

// Twice its size, from the buffer, rect by changed rect; storm.dll's
// arithmetic for each rect's block, step by step.
static void VidDrawDoubled(VidVideo *p)
{
  const BYTE *pBuffer = p->pBuffer;
  const int nPitch = static_cast<int>(p->nBufferWidth);
  const bool bBlendRows = p->bBlendRows;
  const bool bBlend = (p->dwFlags & VID_BLEND) != 0;
  const bool bMayDouble = p->bMayDouble;
  const bool bStretchY = bMayDouble && !(p->dwFlags & VID_SUBMODE_1);
  const bool bHalfWork = (p->dwFlags & VID_HALF_WORK) && !bStretchY;
  if (bHalfWork)
    p->nRegion = p->nRegion == 0;
  const int nRegion = p->nRegion;

  void *hRgn = p->ahRegion[nRegion];
  PortRgnClear(hRgn);
  SmkRect rcChanged;
  while (SmkToBufferRect(p->pSmk, 0, &rcChanged)) {
    const rect32_t rc = {static_cast<LONG>(rcChanged.x), static_cast<LONG>(rcChanged.y),
                         static_cast<LONG>(rcChanged.x + rcChanged.w), static_cast<LONG>(rcChanged.y + rcChanged.h)};
    PortRgnAddRect(hRgn, &rc);
  }
  void *hBoth = NULL;
  if (bHalfWork) {
    hBoth = PortRgnDuplicate(hRgn);
    VidGetRects(p, p->ahRegion[nRegion == 0]);
    for (ULONG i = 0; i < p->nRects; ++i)
      PortRgnAddRect(hBoth, &p->pRects[i]);
  }
  VidGetRects(p, hBoth ? hBoth : hRgn);
  if (hBoth)
    PortRgnDelete(hBoth);

  BYTE *pBase;
  DWORD adwSize[2];
  if (!VidResolve(p, &pBase, adwSize))
    return;
  const int nDestPitch = static_cast<int>(adwSize[0]);
  for (ULONG i = 0; i < p->nRects; ++i) {
    const int nLeft = p->pRects[i].left, nTop = p->pRects[i].top;
    const int nWidth = p->pRects[i].right - nLeft, nHeight = p->pRects[i].bottom - nTop;
    if (nWidth < 2 || nHeight < 2)
      continue;
    VidBlock b = {};
    b.pTable = p->pBlend;
    if (bBlendRows) {
      // the rows between, into the buffer's right half
      b.nWidth = nWidth;
      b.nHeight = nHeight - 1;
      b.pSrc = pBuffer + nTop * nPitch + nLeft;
      b.pDest = const_cast<BYTE *>(pBuffer) + nPitch / 2 + nTop * nPitch + nLeft;
      b.nBelow = nPitch;
      b.nSkipSrc = b.nSkipDest = nPitch - nWidth;
      if (nTop != 0) {
        b.nHeight = nHeight;
        b.pSrc -= nPitch;
        b.pDest -= nPitch;
      }
      VidBlendBelow(b);
    }
    const BYTE *pSrcStart = pBuffer + nTop * nPitch + nLeft;
    const int nDestRow = nDestPitch * nTop;
    BYTE *pDestStart = pBase + nDestRow + nLeft * 2;
    const int nSkipSrc = nPitch - nWidth;
    const int nSkipDest = nDestPitch - 2 * nWidth;
    b.nHeight = nHeight;
    b.pSrc = pSrcStart;
    b.pDest = pDestStart;
    b.nSkipSrc = nSkipSrc;
    b.nSkipDest = nSkipDest;
    b.nWidth = nWidth - (bBlend ? 1 : 0);
    if (bMayDouble) {
      b.pDest = pDestStart + nDestRow;
      b.nHeight = 2 * nHeight;
    }
    if (bBlendRows) {
      b.nSkipSrc = nSkipSrc - nPitch / 2;
      if (bHalfWork) {
        if (nRegion != 0 && nTop >= 2) {
          b.nHeight += 2;
          b.pSrc = pSrcStart - nPitch;
          b.pDest += -2 * nDestPitch;
        }
      } else if (nTop != 0) {
        b.nHeight += 1;
        b.pSrc = pSrcStart - nPitch / 2;
        b.pDest -= nDestPitch;
      }
    }
    if (bStretchY) {
      b.nHeight /= 2;
      b.nSkipDest = nSkipDest + nDestPitch;
    }
    if (bHalfWork) {
      b.nSkipDest += nDestPitch;
      b.nHeight /= 2;
      if (nRegion != 0) {
        if (bBlendRows || !bMayDouble)
          b.pSrc += nPitch >> (bBlendRows ? 1 : 0);
        b.pDest += nDestPitch;
      }
      if (bBlendRows)
        b.nSkipSrc += nPitch / 2;
      else if (!bMayDouble)
        b.nSkipSrc += nPitch;
    }
    b.nSkipSrc2 = b.nSkipSrc;
    b.nSkipDest2 = b.nSkipDest;
    if (bMayDouble && !bBlendRows && !bStretchY && !bHalfWork)
      b.nSkipSrc = -nWidth;
    if (bBlend)
      VidDoubleBlended(b);
    else
      VidDouble(b);
  }
  VidUnlock(p);
}

extern "C" {

// Ordinal 453. The DirectSound the videos' sound plays on; NULL for one of the
// player's own. storm.dll failed here when smackw32.dll could not be loaded;
// this always succeeds.
BOOL __stdcall SVidInitialize(void *pDirectSound)
{
  g_bVidReady = true;
  SmkSoundUseDirectSound(pDirectSound);
  return TRUE;
}

BOOL __stdcall SVidPlayEnd(HANDLE hVid);

// Ordinal 451. Ends every video. Always TRUE.
BOOL __stdcall SVidDestroy(void)
{
  while (g_pVideos)
    SVidPlayEnd(g_pVideos);
  g_bVidReady = false;
  return TRUE;
}

// Ordinal 452. The video's size, and 8 bits a pixel.
BOOL __stdcall SVidGetSize(HANDLE hVid, DWORD *pdwWidth, DWORD *pdwHeight, DWORD *pdwBpp)
{
  if (pdwWidth)
    *pdwWidth = 0;
  if (pdwHeight)
    *pdwHeight = 0;
  if (pdwBpp)
    *pdwBpp = 8;
  if (!hVid) {
    SErrSetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  VidVideo *p = VidFind(hVid);
  if (!p)
    return FALSE;
  if (pdwWidth)
    *pdwWidth = SmkWidth(p->pSmk);
  if (pdwHeight)
    *pdwHeight = SmkHeight(p->pSmk);
  return pdwWidth || pdwHeight || pdwBpp;
}

// The full-colour video in a Smacker file's place: pszFile with the first of the containers'
// extensions the data has for its .smk. One that sets no
// palette of its own (a portrait, drawn into the console's bitmap) takes the screen's colours,
// but for those the tilesets cycle (1 to 13) and the system's (248 on).
// 0x6D2B88, the palette the screen shows or is fading to (gds_palette.cpp).
extern palentry_t _vid_want[256] OC_UNALIGNED;

// The palette a video in the screen's colours is put in: the one the screen is fading to, or
// while that is black, the one it shows.
static void VidScreenPalette(BYTE *pRGB)
{
  bool bBlack = true;
  for (int i = 0; i < 256; ++i) {
    pRGB[3 * i] = _vid_want[i].bRed;
    pRGB[3 * i + 1] = _vid_want[i].bGreen;
    pRGB[3 * i + 2] = _vid_want[i].bBlue;
    bBlack = bBlack && !(pRGB[3 * i] | pRGB[3 * i + 1] | pRGB[3 * i + 2]);
  }
  if (bBlack)
    PortPresentPaletteRGB(pRGB);
}

// A file's bytes through Storm, in a block from malloc; NULL when it is not there.
static void *VidReadFile(const char *pszName, DWORD dwScope, DWORD *pcb)
{
  HANDLE hFile = NULL;
  if (!SFileOpenFileEx(NULL, pszName, dwScope, &hFile))
    return NULL;
  const DWORD cb = SFileGetFileSize(hFile, NULL);
  void *pData = cb && cb != 0xFFFFFFFF ? malloc(cb) : NULL;
  const bool bRead = pData && SFileReadFile(hFile, pData, cb, NULL, NULL);
  SFileCloseFile(hFile);
  if (!bRead) {
    free(pData);
    return NULL;
  }
  *pcb = cb;
  return pData;
}

// A full-colour video out of its bytes: as planes for the GPU presenter with bPlanes, else in
// the screen's colours or a palette of its own.
static SmkFile *VidOpenBytes(const void *pData, DWORD cb, DWORD dwOpen, DWORD dwFlags, bool bPlanes)
{
  if (bPlanes)
    return SmkOpenVideoPlanes(pData, cb, dwOpen);
  const bool bScreen = (dwFlags & VID_NO_PALETTE) != 0;
  return SmkOpenVideo(pData, cb, dwOpen, bScreen ? VidScreenPalette : NULL, 14, 247);
}

static SmkFile *VidOpenVideo(const char *pszFile, DWORD dwOpen, DWORD dwFlags, bool bPlanes)
{
  char szName[MAX_PATH];
  SStrCopy(szName, pszFile, MAX_PATH);
  char *pDot = strrchr(szName, '.');
  if (!pDot || _stricmp(pDot, ".smk") != 0 || pDot - szName + 6 > MAX_PATH)
    return NULL;
  void *pData = NULL;
  DWORD cb = 0;
  for (const char *const *ppszExt = PortVideoExtensions(); *ppszExt && !pData; ++ppszExt) {
    strcpy(pDot, *ppszExt);
    pData = VidReadFile(szName, 0, &cb);
  }
  if (!pData)
    return NULL;
  SmkFile *pSmk = VidOpenBytes(pData, cb, dwOpen, dwFlags, bPlanes);
  free(pData);
  return pSmk;
}

// A cinematic's own copies, before the archives': the active mod's by its name (a Smacker file
// in *phFile; its path in pszOwn), else with the GPU presenter Remastered's in HD for the
// window's height.
static SmkFile *VidOpenCinematic(const char *pszFile, DWORD dwOpen, DWORD dwFlags, bool bPlanes, HANDLE *phFile,
                                 char *pszOwn)
{
  char szPath[MAX_PATH];
  if (ModDataVideoPath(pszFile, szPath, sizeof szPath)) {
    const char *pDot = strrchr(szPath, '.');
    HANDLE hFile;
    if (pDot && _stricmp(pDot, ".smk") == 0) {
      if (SFileOpenFileEx(NULL, szPath, 5, &hFile)) {
        *phFile = hFile;
        SStrCopy(pszOwn, szPath, MAX_PATH);
      }
      return NULL;
    }
    DWORD cb = 0;
    if (void *pData = VidReadFile(szPath, 1, &cb)) {
      SmkFile *pSmk = VidOpenBytes(pData, cb, dwOpen, dwFlags, bPlanes);
      free(pData);
      if (pSmk) {
        SStrCopy(pszOwn, szPath, MAX_PATH);
        return pSmk;
      }
    }
  }
  char szName[MAX_PATH];
  SStrCopy(szName, pszFile, MAX_PATH);
  char *pDot = strrchr(szName, '.');
  if (!bPlanes || !pDot || pDot - szName + 6 > MAX_PATH)
    return NULL;
  strcpy(pDot, ".webm");
  unsigned long cb = 0;
  void *pData = PortRemasteredVideoHd(szName, PortVidWindowRows(), &cb);
  if (!pData)
    return NULL;
  SmkFile *pSmk = SmkOpenVideoPlanes(pData, cb, dwOpen);
  SDL_free(pData);
  return pSmk;
}

// Ordinal 454. pszFile is a name, or with VID_HANDLE an open file; pvDest the
// bitmap, whose pitch and height pdwSize gives and whose left and top prcSource
// does; pPal {0x0C, first, count} the entries the video's palettes may change,
// 1 to 254 without it. *phVid is NULL on every failure.
BOOL __stdcall SVidPlayBegin(const char *pszFile, void *pvDest, const rect32_t *prcSource, const DWORD *pdwSize,
                             const DWORD *pPal, DWORD dwFlags, HANDLE *phVid)
{
  if (phVid)
    *phVid = NULL;
  if (!pszFile || !phVid || (!pvDest && (dwFlags & VID_BITMAP)) || (!pdwSize && pvDest) || (pPal && pPal[0] != 0x0C)) {
    SErrSetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  // a cinematic, which names the screen (play_flic's; the menus ask their animations' size with
  // no flags), in full colour where the presenter draws one
  const bool bCinematic = (dwFlags & VID_SCREEN) && !(dwFlags & VID_BITMAP) && !(dwFlags & VID_HANDLE);
  const bool bPlanes = bCinematic && PortVidFullColour();
  DWORD dwScreenWidth = 0, dwScreenHeight = 0;
  PortVidScreenSize(&dwScreenWidth, &dwScreenHeight);
  if (dwFlags & (VID_REG_SUBMODE | VID_REG_SCALE)) {
    DWORD dwMode = 0;
    SRegLoadValue("Video Player", "Mode", 0, &dwMode);
    if (!(dwMode & VID_SINGLE))
      dwMode = VID_DOUBLE | VID_BLEND | VID_SUBMODE_1;
    if (dwFlags & VID_REG_SCALE)
      dwFlags = (dwFlags & ~(VID_SINGLE | VID_DOUBLE)) | (dwMode & (VID_SINGLE | VID_DOUBLE));
    if (dwFlags & VID_REG_SUBMODE)
      dwFlags = (dwFlags & ~7u) | (dwMode & 7);
  }
  if (!g_bVidReady)
    SVidInitialize(NULL);

  VidVideo *p = static_cast<VidVideo *>(SMemAlloc(sizeof(VidVideo), "port_stormvid.cpp", __LINE__, 8));
  p->ahRegion[0] = PortRgnCreate();
  p->ahRegion[1] = PortRgnCreate();
  VidVideo **ppLast = &g_pVideos;
  while (*ppLast)
    ppLast = &(*ppLast)->pNext;
  *ppLast = p;
  p->hFile = INVALID_HANDLE_VALUE;
  // the screen unless the bitmap
  if (dwFlags & VID_BITMAP)
    dwFlags = (dwFlags & ~(VID_SCREEN | VID_CENTRE_Y));
  else
    dwFlags |= VID_SCREEN;
  p->pDest = (dwFlags & VID_SCREEN) ? NULL : static_cast<BYTE *>(pvDest);
  if (prcSource) {
    p->rcSource = *prcSource;
    p->bHasRect = true;
  }
  if (pdwSize) {
    p->dwPitch = pdwSize[0];
    p->dwHeight = pdwSize[1];
  }
  p->nPalFirst = pPal ? pPal[1] : 1;
  p->nPalCount = pPal ? pPal[2] : 0xFE;

  DWORD dwOpen = (dwFlags & VID_PRELOAD) ? SMACK_FILE_HANDLE | SMACK_PRELOAD : SMACK_FILE_HANDLE;
  if (dwFlags & VID_NO_SKIP)
    dwOpen |= SMACK_NO_SKIP;
  if (!(dwFlags & VID_NO_SOUND))
    dwOpen |= SMACK_ALL_TRACKS;
  if (dwFlags & VID_OPEN_20)
    dwOpen |= 0x20;
  if (dwFlags & VID_OPEN_40)
    dwOpen |= 0x40;
  HANDLE hFile = INVALID_HANDLE_VALUE;
  if (dwFlags & VID_HANDLE) {
    hFile = reinterpret_cast<HANDLE>(const_cast<char *>(pszFile));
  } else {
    SStrCopy(p->szFile, pszFile, MAX_PATH);
    if (bCinematic)
      p->pSmk = VidOpenCinematic(pszFile, dwOpen, dwFlags, bPlanes, &p->hFile, p->szOwn);
    const bool bFound = p->pSmk || p->hFile != INVALID_HANDLE_VALUE;
    // scope 5: on disk or in an archive, as a file handle at the data; with
    // HYPERION_WEBM_FIRST=1 the WebM video first where there is one, to compare the two
    static const bool s_bWebmFirst = SDL_getenv("HYPERION_WEBM_FIRST") && SDL_atoi(SDL_getenv("HYPERION_WEBM_FIRST"));
    if (!bFound && s_bWebmFirst)
      p->pSmk = VidOpenVideo(pszFile, dwOpen, dwFlags, bPlanes);
    if (!bFound && !p->pSmk) {
      if (SFileOpenFileEx(NULL, pszFile, 5, &hFile)) {
        p->hFile = hFile;
      } else if ((p->pSmk = VidOpenVideo(pszFile, dwOpen, dwFlags, bPlanes)) == NULL) {
        SVidPlayEnd(p);
        return FALSE;
      }
    }
    hFile = p->hFile;
  }
  // a video opened above in full colour is the presenter's to draw; a Smacker file is not
  p->bPicture = bPlanes && p->pSmk;
  if (!p->pSmk)
    p->pSmk = SmkOpen(hFile, dwOpen);
  if (!p->pSmk) {
    SVidPlayEnd(p);
    return FALSE;
  }

  const DWORD dwWidth = SmkWidth(p->pSmk), dwHeight = SmkHeight(p->pSmk);
  // a video larger than where it is drawn (a map's or a mod's) would be drawn past it: it is
  // drawn at its own size, smaller to fit with its shape kept, in a bitmap (a portrait) or on
  // the screen; the presenter fits a picture of its own to the window
  const DWORD dwRoomW = p->pDest ? (pdwSize ? p->dwPitch - (p->bHasRect ? p->rcSource.left : 0) : dwWidth) : dwScreenWidth;
  const DWORD dwRoomH = p->pDest ? (pdwSize ? p->dwHeight - (p->bHasRect ? p->rcSource.top : 0) : dwHeight) : dwScreenHeight;
  if (p->bPicture) {
    // at the classic player's shape, doubled: twice as wide where that fits 640 columns (the
    // classic cinematics are 320 by 308, shown 640 by 308), and twice as tall where 480 rows fit
    // that too
    const bool bAcross = 2 * dwWidth <= 640, bDown = bAcross && 2 * dwHeight <= 480;
    p->nShowW = dwWidth << bAcross;
    p->nShowH = dwHeight << bDown;
    dwFlags &= ~(VID_DOUBLE | VID_CENTRE_Y);
  } else if (dwWidth > dwRoomW || dwHeight > dwRoomH) {
    if ((p->pDest && (!pdwSize || (dwFlags & VID_DOUBLE))) || dwRoomW == 0 || dwRoomH == 0) {
      SVidPlayEnd(p);
      return FALSE;
    }
    dwFlags &= ~VID_DOUBLE;
    const bool bWide = static_cast<unsigned long long>(dwWidth) * dwRoomH > static_cast<unsigned long long>(dwHeight) * dwRoomW;
    p->nFitW = bWide ? dwRoomW : static_cast<DWORD>(static_cast<unsigned long long>(dwWidth) * dwRoomH / dwHeight);
    p->nFitH = bWide ? static_cast<DWORD>(static_cast<unsigned long long>(dwHeight) * dwRoomW / dwWidth) : dwRoomH;
    p->nFitW = p->nFitW ? p->nFitW : 1;
    p->nFitH = p->nFitH ? p->nFitH : 1;
    p->nFitX = (dwRoomW - p->nFitW) / 2;
    p->nFitY = (dwRoomH - p->nFitH) / 2;
    p->pFull = static_cast<BYTE *>(SMemAlloc(dwWidth * dwHeight, "port_stormvid.cpp", __LINE__, 8));
    if (!p->pFull) {
      SVidPlayEnd(p);
      return FALSE;
    }
  }
  if (static_cast<int>(2 * dwWidth) > static_cast<int>(dwScreenWidth))
    dwFlags &= ~VID_DOUBLE;
  p->bMayDouble = (dwFlags & VID_DOUBLE) != 0;
  if (static_cast<int>(2 * dwHeight) > static_cast<int>(dwScreenHeight))
    p->bMayDouble = false;
  const DWORD dwShownH = (p->bMayDouble + 1) * dwHeight;
  p->nCentreY = (dwFlags & VID_CENTRE_Y) && dwScreenHeight > dwShownH ? (dwScreenHeight - dwShownH) >> 1 : 0;
  if (dwFlags & VID_DOUBLE) {
    p->bBlendRows = p->bMayDouble && (dwFlags & VID_SUBMODE_1) && (dwFlags & VID_BLEND);
    p->nBufferWidth = (p->bBlendRows + 1) * dwWidth;
    // as large as the SmackBuf storm.dll opened, whose rows it read at this width
    p->pBuffer =
        static_cast<BYTE *>(SMemAlloc(((p->nBufferWidth + 7) & ~7u) * dwHeight, "port_stormvid.cpp", __LINE__, 0));
    if (!p->pBuffer) {
      SVidPlayEnd(p);
      return FALSE;
    }
    SmkToBuffer(p->pSmk, 0, 0, p->nBufferWidth, p->pBuffer);
  }
  // one size or the other, and no blending at its own
  if (dwFlags & VID_DOUBLE)
    dwFlags &= ~VID_SINGLE;
  else
    dwFlags = (dwFlags | VID_SINGLE) & ~7u;
  p->dwFlags = dwFlags;
  *phVid = p;
  if (!p->pDest && !(dwFlags & VID_HANDLE)) {
    g_pSubtitled = p;
    PortSubtitlesBegin(pszFile, p->szOwn[0] ? p->szOwn : NULL);
  }
  if ((dwFlags & VID_CLEAR) || p->bPicture)
    PortVidClear();
  // the screen's first and last entries black and white, even for a bitmap
  if (!pPal && !(dwFlags & VID_NO_PALETTE)) {
    static const PALETTEENTRY s_black = {0, 0, 0, 0}, s_white = {0xFF, 0xFF, 0xFF, 0};
    PortVidPalette(&s_black, 0, 1);
    PortVidPalette(&s_white, 0xFF, 1);
  }
  return TRUE;
}

// Ordinal 457. Draws the next frame when it is time, or at once with dwNoWait;
// *pdwChanged is 1 when a frame was drawn. FALSE when the video has ended, and
// the handle is then gone, or for a handle that is not a video.
BOOL __stdcall SVidPlayContinueSingle(HANDLE hVid, DWORD dwNoWait, DWORD *pdwChanged)
{
  if (pdwChanged)
    *pdwChanged = 0;
  if (!hVid) {
    SErrSetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  VidVideo *p = VidFind(hVid);
  if (!p || !p->pSmk)
    return FALSE;
  SmkFile *pSmk = p->pSmk;
  if (!dwNoWait && SmkWait(pSmk))
    return TRUE;

  // a new palette, narrowed to the halves that are not one colour
  const PALETTEENTRY *pPal = NULL;
  DWORD nFirst = p->nPalFirst, nCount = p->nPalCount;
  if (SmkNewPalette(pSmk) && !(p->dwFlags & VID_NO_PALETTE)) {
    const BYTE *pbRGB = SmkPaletteRGB(pSmk);
    for (int i = 0; i < 256; ++i) {
      g_aPalette[i].peRed = pbRGB[3 * i];
      g_aPalette[i].peGreen = pbRGB[3 * i + 1];
      g_aPalette[i].peBlue = pbRGB[3 * i + 2];
    }
    pPal = g_aPalette;
    bool abVaried[2];
    for (int h = 0; h < 2; ++h) {
      const PALETTEENTRY &pe = g_aPalette[h * 0x80 + 1];
      abVaried[h] = false;
      for (int i = 2; i < 0x7F; ++i) {
        const PALETTEENTRY &q = g_aPalette[h * 0x80 + i];
        abVaried[h] = abVaried[h] || q.peRed != pe.peRed || q.peGreen != pe.peGreen || q.peBlue != pe.peBlue;
      }
    }
    DWORD nEnd = nFirst + nCount;
    if (!abVaried[0] && nFirst < 0x80)
      nFirst = 0x80;
    if (!abVaried[1] && nEnd > 0x80)
      nEnd = 0x80;
    nCount = nEnd > nFirst ? nEnd - nFirst : 0;
    if ((p->dwFlags & VID_CLEAR) && !(p->dwFlags & VID_SPLIT_PALETTE))
      PortVidClear();
    if ((p->dwFlags & (VID_DOUBLE | VID_SUBMODE_1)) == VID_DOUBLE)
      VidBrighten(g_aPalette);
    if (p->dwFlags & VID_BLEND) {
      g_aPalette[0].peRed = g_aPalette[0].peGreen = g_aPalette[0].peBlue = 0;
      g_aPalette[255].peRed = g_aPalette[255].peGreen = g_aPalette[255].peBlue = 0xFF;
      if (p->dwFlags & VID_SPLIT_PALETTE) {
        if (abVaried[0])
          VidBuildBlend(p, g_aPalette, 0, 0x7F);
        if (abVaried[1])
          VidBuildBlend(p, g_aPalette, 0x80, 0xFF);
      } else {
        VidBuildBlend(p, g_aPalette, 0, 0xFF);
      }
    }
  }

  DWORD dwDropped;
  if (p->dwFlags & VID_DOUBLE) {
    dwDropped = SmkDoFrame(pSmk);
    if (pPal && nCount)
      PortVidPalette(pPal + nFirst, static_cast<int>(nFirst), static_cast<int>(nCount));
  } else {
    if (pPal && nCount)
      PortVidPalette(pPal + nFirst, static_cast<int>(nFirst), static_cast<int>(nCount));
    dwDropped = p->bPicture ? VidDrawPicture(p) : VidDrawDirect(p);
  }
  if (pdwChanged && !dwDropped)
    *pdwChanged = 1;
  // a dropped frame gives up the blending, then halves the blits
  if (dwDropped) {
    if (p->dwFlags & VID_BLEND) {
      p->dwFlags &= ~VID_BLEND;
      p->bBlendRows = false;
    } else {
      p->dwFlags |= VID_HALF_WORK;
    }
  }
  if ((p->dwFlags & VID_DOUBLE) && !dwDropped)
    VidDrawDoubled(p);
  if (p == g_pSubtitled)
    PortSubtitlesAt(SmkFrameNum(pSmk) * (SmkInterval(pSmk) ? SmkInterval(pSmk) : 10000) / 100000.0);
  if (SmkFrameNum(pSmk) + 1 >= SmkFrames(pSmk) && !(p->dwFlags & VID_LOOP)) {
    SVidPlayEnd(p);
    return FALSE;
  }
  SmkNextFrame(pSmk);
  return TRUE;
}

// Ordinal 458. The screen keeps the last frame and its palette.
BOOL __stdcall SVidPlayEnd(HANDLE hVid)
{
  if (!hVid) {
    SErrSetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  VidVideo *p = VidFind(hVid);
  if (!p)
    return FALSE;
  if (p == g_pSubtitled) {
    g_pSubtitled = NULL;
    PortSubtitlesEnd();
  }
  if (p->bPicture)
    PortVidPicture(NULL, 0, 0);
  if (p->pBuffer)
    SMemFree(p->pBuffer, "port_stormvid.cpp", __LINE__, 0);
  if (p->pFull)
    SMemFree(p->pFull, "port_stormvid.cpp", __LINE__, 0);
  if (p->pSmk)
    SmkClose(p->pSmk);
  if (p->hFile != INVALID_HANDLE_VALUE)
    CloseHandle(p->hFile);
  if (p->pBlend)
    SMemFree(p->pBlend, "port_stormvid.cpp", __LINE__, 0);
  if (p->pRects)
    SMemFree(p->pRects, "port_stormvid.cpp", __LINE__, 0);
  PortRgnDelete(p->ahRegion[1]);
  PortRgnDelete(p->ahRegion[0]);
  VidVideo **pp = &g_pVideos;
  while (*pp != p)
    pp = &(*pp)->pNext;
  *pp = p->pNext;
  SMemFree(p, "port_stormvid.cpp", __LINE__, 0);
  return TRUE;
}

}  // extern "C"
