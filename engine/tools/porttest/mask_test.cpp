// mask_test.cpp -- checks the screen masks (src117/port/render/port_mask.cpp) and the
// rest of Storm's drawing the port replaces (src117/port/render/port_storm.cpp) against
// Storm's own functions.
//
//   mask_test.exe <path to the 1.17 install's storm.dll> [rounds]
//
// Loads the player's storm.dll (nothing of Blizzard's is in the repo), runs
// each operation through Storm and through ours on the same inputs, and
// compares the pixels the two masks copy. A mask is read by blitting a source
// of 1s onto 0s through it, Storm's with its own STransBltUsingMask, so the
// blit is checked along with the rest. Each round:
//   * creates a mask from a random bitmap of up to 700x520, with scattered
//     zeros or long runs (Storm splits runs past 255);
//   * inverts it, duplicates it, and subtracts a smaller random mask placed
//     anywhere around it, partly outside included (STransCombineMasks with
//     operation 3, the game's);
//   * cuts it to a random dirty array through a random dirty grid, the game's
//     640x480 in 16x16 cells one time in two;
//   * asks it about random points, outside it included;
//   * blits random pixels through it at random pitches, 0 for the source
//     included;
//   * copies random rectangles with SBltROP3 and SRCCOPY, the only operation
//     the game passes, at random pitches, empty and negative sizes included;
//   * adds random rectangles to a region, the game's 16-pixel cells on its
//     screen or small ones crowded into a corner, empty ones included, and
//     compares the pieces SRgnGetRects gives, in their order, whole and cut
//     short, then again after more are added;
//   * blits random pixels through a random 64 KB table with the two programs
//     effects_init compiles with SCodeCompile, run by SCodeExecute, at random
//     pitches and widths up to 643, 0 included. The programs are compiled for
//     at most 160 steps of 4 pixels and 3 more, so a wider row, a negative
//     width or a height of 0 or less runs Storm's away; no caller gives one.
// Exit code 0 means no difference was found.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "../../src117/h/rect32.h"
#include "../../src117/port/render/port_mask.h"
#include "../../src117/port/render/port_storm.h"

typedef struct _HSTRANS *HSTRANS;
typedef BOOL(__stdcall *CreateEFn)(const void *, int, int, int, RECT *, DWORD, HSTRANS *);
typedef BOOL(__stdcall *BltUsingMaskFn)(void *, const void *, int, int, HSTRANS);
typedef BOOL(__stdcall *DeleteFn)(HSTRANS);
typedef BOOL(__stdcall *DuplicateFn)(HSTRANS, HSTRANS *);
typedef BOOL(__stdcall *IntersectDirtyArrayFn)(HSTRANS, const BYTE *, BYTE, HSTRANS *);
typedef BOOL(__stdcall *InvertMaskFn)(HSTRANS, HSTRANS *);
typedef BOOL(__stdcall *SetDirtyArrayInfoFn)(int, int, int, int);
typedef BOOL(__stdcall *IsPixelInMaskFn)(HSTRANS, int, int);
typedef BOOL(__stdcall *CombineMasksFn)(HSTRANS, HSTRANS, int, int, DWORD, HSTRANS *);
typedef BOOL(__stdcall *BltRop3Fn)(void *, const void *, int, int, int, int, DWORD, DWORD);
typedef BOOL(__stdcall *RgnCreateFn)(void **, DWORD);
typedef BOOL(__stdcall *RgnDeleteFn)(void *);
typedef BOOL(__stdcall *RgnCombineRectFn)(void *, const rect32_t *, void *, DWORD);
typedef BOOL(__stdcall *RgnGetRectsFn)(void *, DWORD *, rect32_t *);
typedef BOOL(__stdcall *CodeCompileFn)(const char *, const char *, int, int, int, void **);
typedef BOOL(__stdcall *CodeExecuteFn)(void *, void *);

static CreateEFn STransCreateE;
static BltUsingMaskFn STransBltUsingMask;
static DeleteFn STransDelete;
static DuplicateFn STransDuplicate;
static IntersectDirtyArrayFn STransIntersectDirtyArray;
static InvertMaskFn STransInvertMask;
static SetDirtyArrayInfoFn STransSetDirtyArrayInfo;
static IsPixelInMaskFn STransIsPixelInMask;
static CombineMasksFn STransCombineMasks;
static BltRop3Fn SBltROP3;
static RgnCreateFn SRgnCreate;
static RgnDeleteFn SRgnDelete;
static RgnCombineRectFn SRgnCombineRect;
static RgnGetRectsFn SRgnGetRects;
static CodeCompileFn SCodeCompile;
static CodeExecuteFn SCodeExecute;

// SCodeExecute's block, as dlg_effects.cpp fills it (scodedata_t). Storm leaves
// its working values in aReserved24, which the game does not read.
struct CodeData {
  int nSize, nReserved4, nWidth, nHeight, nSkipDest, nSkipSrc;
  BYTE *pDest, *pSrc, *pTable;
  int aReserved24[7];
};

static uint32_t g_rng = 0x6A09E667u;
static uint32_t Rand32()
{
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static int RandBelow(int n) { return static_cast<int>(Rand32() % static_cast<uint32_t>(n)); }

// Storm's mask keeps its width and height at +0x18 and +0x1C.
static int StormWidth(HSTRANS h) { return reinterpret_cast<const int *>(h)[6]; }
static int StormHeight(HSTRANS h) { return reinterpret_cast<const int *>(h)[7]; }

struct Check {
  const char *name;
  uint64_t checked;
  uint64_t diffs;
};

static BYTE g_abOnes[1024 * 1024];
static BYTE g_abStorm[1024 * 1024];
static BYTE g_abPort[1024 * 1024];

// Compares what the two masks copy, of nWidth x nHeight.
static void CompareMasks(Check &c, HSTRANS hStorm, HSTRANS hPort, int nWidth, int nHeight, const char *args)
{
  ++c.checked;
  if ((hStorm == NULL) != (hPort == NULL)) {
    if (c.diffs++ < 10)
      printf("  DIFF %s(%s): Storm's is %s, ours %s\n", c.name, args, hStorm ? "made" : "NULL", hPort ? "made" : "NULL");
    return;
  }
  if (hStorm == NULL)
    return;
  if (StormWidth(hStorm) != nWidth || StormHeight(hStorm) != nHeight) {
    if (c.diffs++ < 10)
      printf("  DIFF %s(%s): Storm's is %dx%d, expected %dx%d\n", c.name, args, StormWidth(hStorm),
             StormHeight(hStorm), nWidth, nHeight);
    return;
  }
  const size_t cb = static_cast<size_t>(nWidth) * nHeight;
  memset(g_abStorm, 0, cb);
  memset(g_abPort, 0, cb);
  STransBltUsingMask(g_abStorm, g_abOnes, nWidth, nWidth, hStorm);
  PortMaskBlt(g_abPort, g_abOnes, nWidth, nWidth, hPort);
  if (memcmp(g_abStorm, g_abPort, cb) != 0 && c.diffs++ < 10) {
    size_t i = 0;
    while (g_abStorm[i] == g_abPort[i])
      ++i;
    printf("  DIFF %s(%s): pixel %d,%d is %s in Storm's, %s in ours\n", c.name, args, static_cast<int>(i % nWidth),
           static_cast<int>(i / nWidth), g_abStorm[i] ? "in" : "out", g_abPort[i] ? "in" : "out");
  }
}

// A bitmap of nWidth x nHeight: scattered zeros, or runs of zeros and not.
static void MakeBitmap(BYTE *p, int nWidth, int nHeight)
{
  const size_t cb = static_cast<size_t>(nWidth) * nHeight;
  if (Rand32() & 1) {
    const uint32_t uZeroIn256 = Rand32() & 0xFF;
    for (size_t i = 0; i < cb; ++i)
      p[i] = (Rand32() & 0xFF) < uZeroIn256 ? 0 : static_cast<BYTE>(1 + RandBelow(255));
  } else {
    size_t i = 0;
    while (i < cb) {
      size_t n = 1 + RandBelow(Rand32() & 1 ? 40 : 1200);
      const BYTE b = Rand32() & 1 ? 0 : static_cast<BYTE>(1 + RandBelow(255));
      if (n > cb - i)
        n = cb - i;
      memset(p + i, b, n);
      i += n;
    }
  }
}

// A rectangle for the region: one of the game's, snapped out to 16-pixel cells
// and cut to its screen as CombineRectInRgn does, or a small one in a corner,
// where they crowd and touch; now and then an empty one.
static rect32_t RandRegionRect(bool bGame)
{
  rect32_t rc;
  if (bGame) {
    rc.left = RandBelow(680) - 20;
    rc.top = RandBelow(520) - 20;
    rc.right = rc.left + RandBelow(Rand32() & 1 ? 64 : 400);
    rc.bottom = rc.top + RandBelow(Rand32() & 1 ? 64 : 300);
    rc.left &= -16;
    rc.right = (rc.right + 16) & -16;
    rc.top &= -16;
    rc.bottom = (rc.bottom + 16) & -16;
    rc.left = rc.left < 0 ? 0 : rc.left;
    rc.right = rc.right > 640 ? 640 : rc.right;
    rc.top = rc.top < 0 ? 0 : rc.top;
    rc.bottom = rc.bottom > 480 ? 480 : rc.bottom;
  } else {
    rc.left = RandBelow(24);
    rc.top = RandBelow(24);
    rc.right = rc.left + 1 + RandBelow(12);
    rc.bottom = rc.top + 1 + RandBelow(12);
  }
  if ((Rand32() & 31) == 0) {
    rc.right = rc.left - RandBelow(3);
    rc.bottom = Rand32() & 1 ? rc.top - RandBelow(3) : rc.bottom;
  }
  return rc;
}

// Asks both regions for their pieces, at most dwCap of them, and compares.
static void CompareRegions(Check &c, void *hS, void *hP, DWORD dwCap, int nAdded)
{
  static rect32_t s_arcStorm[4096], s_arcPort[4096];
  DWORD dwS = dwCap, dwP = dwCap;
  memset(s_arcStorm, 0xCD, sizeof s_arcStorm);
  memset(s_arcPort, 0xCD, sizeof s_arcPort);
  SRgnGetRects(hS, &dwS, s_arcStorm);
  PortRgnGetRects(hP, &dwP, s_arcPort);
  ++c.checked;
  if (dwS != dwP || memcmp(s_arcStorm, s_arcPort, sizeof s_arcStorm) != 0) {
    if (c.diffs++ < 10) {
      printf("  DIFF %s(%d rectangles, at most %lu): Storm gives %lu pieces, ours %lu\n", c.name, nAdded, dwCap, dwS,
             dwP);
      for (DWORD i = 0; i < dwS || i < dwP; ++i) {
        if (i < dwS && i < dwP && memcmp(&s_arcStorm[i], &s_arcPort[i], sizeof(rect32_t)) == 0)
          continue;
        printf("    piece %lu: Storm %ld,%ld,%ld,%ld, ours %ld,%ld,%ld,%ld\n", i, s_arcStorm[i].left,
               s_arcStorm[i].top, s_arcStorm[i].right, s_arcStorm[i].bottom, s_arcPort[i].left, s_arcPort[i].top,
               s_arcPort[i].right, s_arcPort[i].bottom);
        break;
      }
    }
    return;
  }
  DWORD dwSAll = 0, dwPAll = 0;
  SRgnGetRects(hS, &dwSAll, NULL);
  PortRgnGetRects(hP, &dwPAll, NULL);
  if (dwSAll != dwPAll && c.diffs++ < 10)
    printf("  DIFF %s(%d rectangles, count only): Storm %lu, ours %lu\n", c.name, nAdded, dwSAll, dwPAll);
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    printf("usage: mask_test.exe <storm.dll> [rounds]\n");
    return 2;
  }
  const int nRounds = argc > 2 ? atoi(argv[2]) : 2000;
  HMODULE hStorm = LoadLibraryA(argv[1]);
  if (hStorm == NULL) {
    printf("cannot load %s (error %lu)\n", argv[1], GetLastError());
    return 2;
  }
#define STORM_ORDINAL(fn, n) fn = reinterpret_cast<decltype(fn)>(GetProcAddress(hStorm, reinterpret_cast<LPCSTR>(n)))
  STORM_ORDINAL(STransBltUsingMask, 432);
  STORM_ORDINAL(STransDelete, 434);
  STORM_ORDINAL(STransDuplicate, 436);
  STORM_ORDINAL(STransIntersectDirtyArray, 437);
  STORM_ORDINAL(STransInvertMask, 438);
  STORM_ORDINAL(STransSetDirtyArrayInfo, 440);
  STORM_ORDINAL(STransIsPixelInMask, 442);
  STORM_ORDINAL(STransCombineMasks, 443);
  STORM_ORDINAL(STransCreateE, 445);
  STORM_ORDINAL(SBltROP3, 313);
  STORM_ORDINAL(SRgnCombineRect, 523);
  STORM_ORDINAL(SRgnCreate, 524);
  STORM_ORDINAL(SRgnDelete, 525);
  STORM_ORDINAL(SRgnGetRects, 529);
  STORM_ORDINAL(SCodeCompile, 331);
  STORM_ORDINAL(SCodeExecute, 334);
  if (!STransBltUsingMask || !STransDelete || !STransDuplicate || !STransIntersectDirtyArray || !STransInvertMask ||
      !STransSetDirtyArrayInfo || !STransIsPixelInMask || !STransCombineMasks || !STransCreateE || !SBltROP3 ||
      !SRgnCombineRect || !SRgnCreate || !SRgnDelete || !SRgnGetRects || !SCodeCompile || !SCodeExecute) {
    printf("%s lacks an ordinal the test uses\n", argv[1]);
    return 2;
  }
  setvbuf(stdout, NULL, _IONBF, 0);
  memset(g_abOnes, 1, sizeof g_abOnes);

  // effects_init's two programs, the source first in the index and then the
  // destination first.
  void *ahCode[2] = {NULL, NULL};
  if (!SCodeCompile("4 W=0 1 A=S B=D W1=A1 W2=B1 D=TW",
                    "4 W=0 A=S B=D W1=A1 W2=B1 C1=TW W1=A2 W2=B2 C2=TW W1=A3 W2=B3 C3=TW W1=A4 W2=B4 C4=TW D=C", 0,
                    0xA0, 0x40000, &ahCode[0]) ||
      !SCodeCompile("4 W=0 1 A=D B=S W1=A1 W2=B1 D=TW",
                    "4 W=0 A=D B=S W1=A1 W2=B1 C1=TW W1=A2 W2=B2 C2=TW W1=A3 W2=B3 C3=TW W1=A4 W2=B4 C4=TW D=C", 0,
                    0xA0, 0x40000, &ahCode[1])) {
    printf("SCodeCompile failed\n");
    return 2;
  }
  static BYTE s_abTable[0x10000];

  static BYTE s_abBitmap[700 * 520];
  static BYTE s_abCut[300 * 300];
  static BYTE s_abDirty[256 * 256];
  static BYTE s_abSrc[1024 * 600];
  static BYTE s_abDstStorm[1024 * 600];
  static BYTE s_abDstPort[1024 * 600];
  Check cCreate = {"create"}, cInvert = {"invert"}, cDuplicate = {"duplicate"}, cSubtract = {"subtract"},
        cIntersect = {"intersect dirty"}, cPixel = {"is pixel in mask"}, cBlt = {"blit"},
        cRop3 = {"SBltROP3"}, cRgn = {"region"}, cCode = {"SCodeExecute"};
  char args[160];
  DWORD t0 = GetTickCount();

  for (int nRound = 0; nRound < nRounds; ++nRound) {
    // Every fourth round the game's 640x480.
    const bool bGame = (nRound & 3) == 0;
    const int nWidth = bGame ? 640 : 1 + RandBelow(700);
    const int nHeight = bGame ? 480 : 1 + RandBelow(520);
    MakeBitmap(s_abBitmap, nWidth, nHeight);
    HSTRANS hS = NULL;
    STransCreateE(s_abBitmap, nWidth, nHeight, 8, NULL, 0x1000000, &hS);
    HSTRANS hP = PortMaskCreate(s_abBitmap, nWidth, nHeight);
    sprintf(args, "%dx%d", nWidth, nHeight);
    CompareMasks(cCreate, hS, hP, nWidth, nHeight, args);

    HSTRANS hSInv = NULL;
    STransInvertMask(hS, &hSInv);
    HSTRANS hPInv = PortMaskInvert(hP);
    CompareMasks(cInvert, hSInv, hPInv, nWidth, nHeight, args);

    HSTRANS hSDup = NULL;
    STransDuplicate(hSInv, &hSDup);
    HSTRANS hPDup = PortMaskDuplicate(hPInv);
    CompareMasks(cDuplicate, hSDup, hPDup, nWidth, nHeight, args);

    // A smaller mask cut out of the inverted one, anywhere around it.
    const int nCutWidth = 1 + RandBelow(300), nCutHeight = 1 + RandBelow(300);
    MakeBitmap(s_abCut, nCutWidth, nCutHeight);
    HSTRANS hSCut = NULL;
    STransCreateE(s_abCut, nCutWidth, nCutHeight, 8, NULL, 0x1000000, &hSCut);
    HSTRANS hPCut = PortMaskCreate(s_abCut, nCutWidth, nCutHeight);
    const int nX = RandBelow(nWidth + nCutWidth) - nCutWidth;
    const int nY = RandBelow(nHeight + nCutHeight) - nCutHeight;
    HSTRANS hSSub = NULL;
    STransCombineMasks(hSInv, hSCut, nX, nY, 3, &hSSub);
    HSTRANS hPSub = PortMaskSubtract(hPInv, hPCut, nX, nY);
    sprintf(args, "%dx%d less %dx%d at %d,%d", nWidth, nHeight, nCutWidth, nCutHeight, nX, nY);
    CompareMasks(cSubtract, hSSub, hPSub, nWidth, nHeight, args);

    // The dirty cut: the game's grid, or a random one that covers the mask.
    int nGridW = 640, nGridH = 480, nCellW = 16, nCellH = 16;
    if (!bGame && (Rand32() & 1)) {
      nCellW = 1 << RandBelow(6);
      nCellH = 1 << RandBelow(6);
      nGridW = nWidth + RandBelow(64);
      nGridH = nHeight + RandBelow(64);
    }
    const int nCellsW = (nGridW - 1 + nCellW) / nCellW, nCellsH = (nGridH - 1 + nCellH) / nCellH;
    if (nCellsW * nCellsH <= static_cast<int>(sizeof s_abDirty) && nWidth <= nGridW && nHeight <= nGridH) {
      STransSetDirtyArrayInfo(nGridW, nGridH, nCellW, nCellH);
      PortMaskSetDirtyGrid(nGridW, nGridH, nCellW, nCellH);
      for (int i = 0; i < nCellsW * nCellsH; ++i)
        s_abDirty[i] = static_cast<BYTE>(Rand32() & 1 ? RandBelow(4) : Rand32() >> 24);
      const BYTE bDirty = static_cast<BYTE>(Rand32() & 1 ? 3 : 1 + RandBelow(255));
      HSTRANS hSCutDirty = NULL;
      STransIntersectDirtyArray(hSSub, s_abDirty, bDirty, &hSCutDirty);
      HSTRANS hPCutDirty = PortMaskIntersectDirty(hPSub, s_abDirty, bDirty);
      sprintf(args, "%dx%d, grid %dx%d in %dx%d cells, bits %#x", nWidth, nHeight, nGridW, nGridH, nCellW, nCellH,
              bDirty);
      CompareMasks(cIntersect, hSCutDirty, hPCutDirty, nWidth, nHeight, args);
      if (hSCutDirty)
        STransDelete(hSCutDirty);
      PortMaskDelete(hPCutDirty);
    }

    for (int i = 0; i < 64; ++i) {
      const int x = RandBelow(nWidth + 4) - 2, y = RandBelow(nHeight + 4) - 2;
      const int nS = STransIsPixelInMask(hSSub, x, y) != 0, nP = PortMaskHasPixel(hPSub, x, y) != 0;
      ++cPixel.checked;
      if (nS != nP && cPixel.diffs++ < 10)
        printf("  DIFF %s(%dx%d at %d,%d): Storm %d, ours %d\n", cPixel.name, nWidth, nHeight, x, y, nS, nP);
    }

    // Random pixels through the mask at random pitches, onto random pixels.
    if (nWidth <= 1024 && nHeight <= 600) {
      const int nDstPitch = nWidth + RandBelow(1024 - nWidth + 1);
      const int nSrcPitch = Rand32() & 3 ? nWidth + RandBelow(1024 - nWidth + 1) : 0;
      const size_t cbDst = static_cast<size_t>(nDstPitch) * nHeight;
      const size_t cbSrc = static_cast<size_t>(nSrcPitch ? nSrcPitch : nWidth) * nHeight;
      for (size_t i = 0; i < cbSrc; ++i)
        s_abSrc[i] = static_cast<BYTE>(Rand32() >> 24);
      for (size_t i = 0; i < cbDst; ++i)
        s_abDstStorm[i] = static_cast<BYTE>(Rand32() >> 24);
      memcpy(s_abDstPort, s_abDstStorm, cbDst);
      STransBltUsingMask(s_abDstStorm, s_abSrc, nDstPitch, nSrcPitch, hSSub);
      PortMaskBlt(s_abDstPort, s_abSrc, nDstPitch, nSrcPitch, hPSub);
      ++cBlt.checked;
      if (memcmp(s_abDstStorm, s_abDstPort, cbDst) != 0 && cBlt.diffs++ < 10)
        printf("  DIFF %s(%dx%d, pitches %d and %d)\n", cBlt.name, nWidth, nHeight, nDstPitch, nSrcPitch);
    }

    // Rectangles copied with SBltROP3, into the middle of the destination so
    // that a write out of bounds shows.
    for (int i = 0; i < 16; ++i) {
      const int nW = Rand32() & 15 ? 1 + RandBelow(700) : RandBelow(5) - 2;
      const int nH = Rand32() & 15 ? 1 + RandBelow(300) : RandBelow(5) - 2;
      const int nDP = (nW > 0 ? nW : 0) + RandBelow(200);
      const int nSP = (nW > 0 ? nW : 0) + RandBelow(200);
      const size_t cbDst = sizeof s_abDstStorm;
      const size_t nOfs = 0x10000;
      for (size_t j = 0; j < cbDst; j += 1 + RandBelow(64))
        s_abDstStorm[j] = static_cast<BYTE>(Rand32() >> 24);
      memcpy(s_abDstPort, s_abDstStorm, cbDst);
      const BOOL bS = SBltROP3(s_abDstStorm + nOfs, s_abSrc, nW, nH, nDP, nSP, 0, SRCCOPY);
      const BOOL bP = PortBltRop3(s_abDstPort + nOfs, s_abSrc, nW, nH, nDP, nSP, 0, SRCCOPY);
      ++cRop3.checked;
      if ((memcmp(s_abDstStorm, s_abDstPort, cbDst) != 0 || (bS != 0) != (bP != 0)) && cRop3.diffs++ < 10)
        printf("  DIFF %s(%dx%d, pitches %d and %d): Storm answers %d, ours %d\n", cRop3.name, nW, nH, nDP, nSP, bS,
               bP);
    }

    // Table blits, into the middle of the destination so that a write out of
    // bounds shows.
    for (size_t j = 0; j < sizeof s_abTable; ++j)
      s_abTable[j] = static_cast<BYTE>(Rand32() >> 24);
    for (int i = 0; i < 8; ++i) {
      const int bSourceHigh = static_cast<int>(Rand32() & 1);
      const int nW = Rand32() & 15 ? 1 + RandBelow(Rand32() & 1 ? 16 : 643) : 0;
      const int nH = 1 + RandBelow(300);
      const int nDP = (nW > 0 ? nW : 0) + RandBelow(200);
      const int nSP = (nW > 0 ? nW : 0) + RandBelow(200);
      const size_t cbDst = sizeof s_abDstStorm;
      const size_t nOfs = 0x10000;
      for (size_t j = 0; j < cbDst; ++j)
        s_abDstStorm[j] = static_cast<BYTE>(Rand32() >> 24);
      memcpy(s_abDstPort, s_abDstStorm, cbDst);
      CodeData data = {0x40, 0, nW, nH, nDP - nW, nSP - nW, s_abDstStorm + nOfs, s_abSrc, s_abTable};
      const CodeData dataBefore = data;
      SCodeExecute(ahCode[bSourceHigh], &data);
      PortTableBlit(s_abDstPort + nOfs, s_abSrc, nW, nH, nDP, nSP, s_abTable, bSourceHigh);
      ++cCode.checked;
      if ((memcmp(s_abDstStorm, s_abDstPort, cbDst) != 0 || memcmp(&data, &dataBefore, offsetof(CodeData, aReserved24)) != 0) &&
          cCode.diffs++ < 10)
        printf("  DIFF %s(%s, %dx%d, pitches %d and %d)%s\n", cCode.name, bSourceHigh ? "source high" : "source low",
               nW, nH, nDP, nSP, memcmp(&data, &dataBefore, offsetof(CodeData, aReserved24)) ? ": Storm changed the block" : "");
    }

    // A region of up to 80 rectangles, asked for its pieces, more added, and
    // asked again.
    {
      const bool bGameRgn = (Rand32() & 1) != 0;
      void *hSRgn = NULL;
      SRgnCreate(&hSRgn, 0);
      void *hPRgn = PortRgnCreate();
      int nAdded = 0;
      for (int nPass = 0; nPass < 2; ++nPass) {
        const int nMore = 1 + RandBelow(Rand32() & 1 ? 8 : 40);
        for (int i = 0; i < nMore; ++i) {
          const rect32_t rc = RandRegionRect(bGameRgn);
          SRgnCombineRect(hSRgn, &rc, NULL, 2);
          PortRgnAddRect(hPRgn, &rc);
          ++nAdded;
        }
        CompareRegions(cRgn, hSRgn, hPRgn, Rand32() & 3 ? 0xFF : RandBelow(12), nAdded);
      }
      SRgnDelete(hSRgn);
      PortRgnDelete(hPRgn);
    }

    STransDelete(hS);
    STransDelete(hSInv);
    STransDelete(hSDup);
    STransDelete(hSCut);
    STransDelete(hSSub);
    PortMaskDelete(hP);
    PortMaskDelete(hPInv);
    PortMaskDelete(hPDup);
    PortMaskDelete(hPCut);
    PortMaskDelete(hPSub);
  }
  printf("%d rounds (%lu ms)\n", nRounds, GetTickCount() - t0);

  const Check *all[] = {&cCreate, &cInvert, &cDuplicate, &cSubtract, &cIntersect, &cPixel, &cBlt, &cRop3, &cRgn, &cCode};
  int bad = 0;
  for (int i = 0; i < static_cast<int>(sizeof all / sizeof all[0]); ++i) {
    printf("%-17s %llu checks, %llu differences\n", all[i]->name, all[i]->checked, all[i]->diffs);
    bad += all[i]->diffs != 0;
  }
  printf(bad ? "FAIL\n" : "OK: the same as Storm's on every check\n");
  return bad ? 1 : 0;
}
