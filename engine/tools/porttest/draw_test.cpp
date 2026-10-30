// draw_test.cpp -- checks the map's tile drawing (src117/port/render/port_draw.cpp)
// against the oracle.
//
//   draw_test.exe <path to the 1.17 StarCraft.exe> [calls]
//
// The original routines read and write game globals at fixed addresses, so the
// test runs the oracle's code where the game has it. It is laid out like
// Hyperion.exe (docs/hyperion-exe.md, tools/hyperion_pe.py): its image starts
// at 0x400000 with a blank read-write-execute section over the 1.17 image's
// range, into which it copies the oracle's code; the globals are that
// section's zeros until the test sets them. Ours reads the same globals.
// Each call runs the original into one copy of the destination and
// ours into another, from the same state, and the two are compared in full,
// with 64 KB on each side to catch a write out of bounds. The inputs are
// random, from the ranges the callers use and past them:
//   * draw_cell: any cell, plain and mirrored, at offsets across the ring and
//     past its end, aligned as cell_draw_mtx gives them and not, and rows
//     that start at the end or next to it;
//   * rle_creep: random frames of every code kind at every position, lines
//     that end exactly and lines whose last code runs over, lines that start
//     at the ring's end or next to it;
//   * cell_copy_scrollbuf: scroll positions over an 8192-pixel map, so the view
//     wraps at the ring's end in both directions, and rows that end at the
//     ring's end or next to it;
//   * cell_copy_scrollbuf_row: dirty strips as redraw_dirty_mtx cuts them,
//     strips with a row that ends at the ring's end or next to it, and any
//     row, column, width and offset;
//   * the distortion routines, rle_draw_invis_asm, rle_draw_emp_asm and their
//     mirrored twins: random frames under random clips, anywhere on a 640x480
//     target and in the corner where a distance wraps round it;
//   * img_clipped: source and destination blocks inside, across and outside
//     each edge, and sums past a short's range; both blocks and the answer
//     are compared;
//   * bmp_copy_rect_asm and bmp_mask_rect_asm: rows of any width and gaps, the
//     copy also between two places in one buffer that overlap.
// Exit code 0 means no difference was found.
#include "oracle_image.h"
#include <stddef.h>
#include "../../src117/h/bmp.h"
#include "../../src117/port/render/port_draw.h"
#include "../../src117/port/platform/port_paths.h"  // PortOptions

// port_view.cpp's step (PortViewSet) reaches the game's mouse and Hyperion's
// options, its switch in a game (ViewSwitchGame), its camera and its render
// pass the game's drawing and the display, none of which this test links or
// calls; the port's modules among them are tools/porttest/*_stub.cpp.
void sub_4CE820(int) {}
const PortOptions *PortCommandOptions(void)
{
  static PortOptions s_options;
  return &s_options;
}
int PortDisplayViewWidth(void) { return 0; }
void mou_set_pos(short, short) {}
void cur_update(void) {}
void cell_paint(void) {}
void gamemap_force_redraw(void) {}
void vid_mask_initialize(void) {}
void mask_build_grid(void) {}
void __fastcall cell_set_pos(int, int) {}
void __fastcall dlg_set_all_update(void) {}
void __fastcall minimap_force_redraw(void) {}
void __fastcall usel_cancel_drag_evt(void) {}
void __fastcall img_area_invalid(int, int, int, int) {}
void gamemap_priority_update(void) {}
unsigned char __fastcall IsAutoScroll(void) { return 0; }

#define RING_PITCH 672
#define RING_SIZE (672 * 448)
#define SCREEN_SIZE (640 * 480)
#define SLACK 0x10000

// The game globals the routines use (port_draw.cpp names them).
#define SCROLL_BUF (*reinterpret_cast<BYTE **>(0x543240u))
#define CELL_BUF (*reinterpret_cast<BYTE **>(0x543244u))
#define SCROLL_X (*reinterpret_cast<long *>(0x543218u))
#define SCROLL_Y (*reinterpret_cast<long *>(0x54321Cu))
#define DRAW_WIDTH (*reinterpret_cast<WORD *>(0x6D0B88u))
#define DRAW_BITS (*reinterpret_cast<BYTE **>(0x6D0B8Cu))
#define CURRENT_BMP (*reinterpret_cast<TestBmp **>(0x6D0C40u))
#define BRIGHTNESS (reinterpret_cast<BYTE *>(0x666460u))
#define FONT_COLORS (*reinterpret_cast<const BYTE **>(0x6D3010u))
#define CHAR_WIDTH (*reinterpret_cast<int *>(0x6D3014u))
#define CHAR_HEIGHT (*reinterpret_cast<int *>(0x6D3018u))
#define CHAR_ROW_MOD (*reinterpret_cast<int *>(0x6D301Cu))
#define BLT_WIDTH (*reinterpret_cast<int *>(0x50F2F0u))
#define BLT_HEIGHT (*reinterpret_cast<int *>(0x50F2F4u))
#define BLT_SRC_MOD (*reinterpret_cast<int *>(0x50F2F8u))
#define BLT_DST_MOD (*reinterpret_cast<int *>(0x50F2FCu))
#define BLT_SRC (*reinterpret_cast<const BYTE **>(0x50F300u))
#define BLT_DST (*reinterpret_cast<BYTE **>(0x50F304u))

// A bitmap as the game describes one (bmp_t): what sprites draw into.
struct TestBmp {
  short nWidth;
  short nHeight;
  BYTE *pPixels;
};

typedef void(__fastcall *DrawCellFn)(int, int);
typedef void(__fastcall *RleCreepFn)(const BYTE *, int);
typedef void (*CopyScrollbufFn)(void);
typedef void(__fastcall *CopyScrollbufRowFn)(int, int, int, int);
typedef void(__fastcall *RleDrawFn)(int, int, const void *, const void *, unsigned long);
typedef int(__fastcall *ImgClippedFn)(bmpsrc_t *, bmpsrc_t *);
typedef void (*BltFn)(void);
typedef void(__fastcall *DrawCharFn)(BYTE *, const BYTE *);

static uint32_t g_rng = 0x9E3779B9u;
static uint32_t Rand32()
{
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static int RandBelow(int n) { return static_cast<int>(Rand32() % static_cast<uint32_t>(n)); }

// A short from the ranges a clip sees, and one time in eight any at all.
static short RandShort(int nLo, int nHi)
{
  if ((Rand32() & 7) == 0)
    return static_cast<short>(Rand32());
  return static_cast<short>(nLo + RandBelow(nHi - nLo + 1));
}

// A coordinate one time in three at an edge of a size, 0 or nSize or next to
// either, and otherwise RandShort's.
static short RandEdge(int nSize, int nLo, int nHi)
{
  static const int s_anNear[] = {-1, 0, 1};
  if (RandBelow(3) == 0)
    return static_cast<short>((Rand32() & 1 ? nSize : 0) + s_anNear[RandBelow(3)]);
  return RandShort(nLo, nHi);
}

// An offset within 8 bytes of where one of the next nRows rows meets the ring's
// end, so that a row starts or ends at it, just before it or just past it.
static int NearRingEnd(int nRows)
{
  return RING_SIZE - RandBelow(nRows) * RING_PITCH + RandBelow(17) - 8;
}

static void FillRandom(BYTE *p, size_t cb)
{
  for (size_t i = 0; i < cb; ++i)
    p[i] = static_cast<BYTE>(Rand32() >> 24);
}

// A destination with SLACK bytes on each side: the original's copy and ours.
struct Pair {
  BYTE *pOracle;
  BYTE *pPort;
  size_t cb;   // the destination itself, without the slack
};

static bool PairAlloc(Pair *p, size_t cb)
{
  p->cb = cb;
  p->pOracle = static_cast<BYTE *>(malloc(cb + 2 * SLACK));
  p->pPort = static_cast<BYTE *>(malloc(cb + 2 * SLACK));
  if (!p->pOracle || !p->pPort)
    return false;
  FillRandom(p->pOracle, cb + 2 * SLACK);
  memcpy(p->pPort, p->pOracle, cb + 2 * SLACK);
  return true;
}

struct Routine {
  const char *name;
  uint64_t checked;
  uint64_t diffs;
};

// Compares the two copies, slack included; reports the first differences and
// brings the port's copy back in line so one difference is not counted twice.
static void Compare(Routine &r, Pair &pair, const char *args)
{
  const size_t cb = pair.cb + 2 * SLACK;
  ++r.checked;
  if (memcmp(pair.pOracle, pair.pPort, cb) == 0)
    return;
  if (r.diffs++ < 10) {
    size_t i = 0;
    while (pair.pOracle[i] == pair.pPort[i])
      ++i;
    printf("  DIFF %s(%s): byte %ld from the start is %u, oracle %u\n", r.name, args,
           static_cast<long>(i) - SLACK, pair.pPort[i], pair.pOracle[i]);
  }
  memcpy(pair.pPort, pair.pOracle, cb);
}

// True when the 1.17 image's range is this exe's placeholder: committed,
// writable and executable (copy-on-write until first written, as an image's
// writable section is).
static bool RangeIsOurs(void)
{
  MEMORY_BASIC_INFORMATION mbi;
  if (VirtualQuery(reinterpret_cast<void *>(0x401000u), &mbi, sizeof mbi) == sizeof mbi &&
      mbi.State == MEM_COMMIT &&
      (mbi.Protect == PAGE_EXECUTE_READWRITE || mbi.Protect == PAGE_EXECUTE_WRITECOPY) &&
      reinterpret_cast<uint32_t>(mbi.BaseAddress) + mbi.RegionSize >= 0x700000u)
    return true;
  printf("0x401000..0x700000 is not this exe's placeholder; build it with build_porttest.bat\n");
  return false;
}

// A creep edge frame of nLines lines nWidth wide: the line offsets, then the
// codes. The last code of a line runs over the width one time in four.
// Returns its size.
static size_t MakeCreepFrame(BYTE *pFrame, size_t cbMax, int nWidth, int *pnLines)
{
  WORD *pwLine = reinterpret_cast<WORD *>(pFrame);
  size_t cb = 2 * *pnLines;
  int nLine;

  for (nLine = 0; nLine < *pnLines; ++nLine) {
    int nLeft = nWidth;
    if (cb + nWidth * 2 + 256 > cbMax || cb > 0xFF00)
      break;
    pwLine[nLine] = static_cast<WORD>(cb);
    while (nLeft > 0) {
      const bool bOver = (Rand32() & 3) == 0;
      int nMax;
      int n;
      switch (RandBelow(3)) {
      case 0:
        nMax = bOver ? 0x7F : (nLeft < 0x7F ? nLeft : 0x7F);
        n = 1 + RandBelow(nMax);
        pFrame[cb++] = static_cast<BYTE>(0x80 | n);
        break;
      case 1:
        nMax = bOver ? 0x3F : (nLeft < 0x3F ? nLeft : 0x3F);
        n = 1 + RandBelow(nMax);
        pFrame[cb++] = static_cast<BYTE>(0x40 | n);
        pFrame[cb++] = static_cast<BYTE>(Rand32() >> 24);
        break;
      default:
        nMax = bOver ? 0x3F : (nLeft < 0x3F ? nLeft : 0x3F);
        n = 1 + RandBelow(nMax);
        pFrame[cb++] = static_cast<BYTE>(n);
        FillRandom(pFrame + cb, n);
        cb += n;
        break;
      }
      nLeft -= n;
    }
  }
  *pnLines = nLine;
  return cb;
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    printf("usage: draw_test.exe <StarCraft.exe 1.17> [calls]\n");
    return 2;
  }
  setvbuf(stdout, NULL, _IONBF, 0);  // a routine that faults leaves what went before
  if (!RangeIsOurs())
    return 2;
  const int nCalls = argc > 2 ? atoi(argv[2]) : 20000;

  BYTE *image = LoadOracle(argv[1]);
  if (!image)
    return 2;
  // The whole code section, 0x401000 to .rdata at 0x4EB000: the tile and
  // sprite routines near its start, the bitmap copies near its end.
  long off = FileOffset(image, 0x401000u);
  if (off < 0) {
    printf("no code at 0x401000\n");
    return 2;
  }
  memcpy(reinterpret_cast<void *>(0x401000u), image + off, 0x4EB000u - 0x401000u);

  const DrawCellFn oracleDrawCell = reinterpret_cast<DrawCellFn>(0x405AF0u);
  const RleCreepFn oracleRleCreep = reinterpret_cast<RleCreepFn>(0x405D40u);
  const CopyScrollbufFn oracleCopyScrollbuf = reinterpret_cast<CopyScrollbufFn>(0x405A43u);
  const CopyScrollbufRowFn oracleCopyScrollbufRow = reinterpret_cast<CopyScrollbufRowFn>(0x405AADu);

  // The tileset's cells: every index draw_cell can reach.
  CELL_BUF = static_cast<BYTE *>(malloc(0x8000 * 64));
  FillRandom(CELL_BUF, 0x8000 * 64);
  Pair ring, screen;
  static BYTE s_abFrame[0x14000];
  if (!CELL_BUF || !PairAlloc(&ring, RING_SIZE) || !PairAlloc(&screen, SCREEN_SIZE)) {
    printf("out of memory\n");
    return 2;
  }
  DRAW_WIDTH = 640;

  Routine rDrawCell = {"draw_cell"}, rRleCreep = {"rle_creep"},
          rCopy = {"cell_copy_scrollbuf"}, rCopyRow = {"cell_copy_scrollbuf_row"};
  char args[128];
  DWORD t0 = GetTickCount();

  for (int n = 0; n < nCalls; ++n) {
    // 1.17 reads 16 bits of the cell; the port all 32, for the tiles Remastered 1.22 added
    //, so the two are held to each other over 1.17's range
    int nCell = static_cast<int>(Rand32() & 0xFFFFu);
    int nDest;
    switch (Rand32() & 3) {
    case 0:
    case 1:
      nDest = RandBelow(RING_SIZE / 32) * 32 + (RandBelow(4) * 0x1500 + RandBelow(4) * 8);
      break;
    case 2:
      nDest = RandBelow(RING_SIZE + 0x3F18);
      break;
    default:
      nDest = NearRingEnd(9);
      break;
    }
    sprintf(args, "%#x, %d", nCell, nDest);
    SCROLL_BUF = ring.pOracle + SLACK;
    oracleDrawCell(nCell, nDest);
    SCROLL_BUF = ring.pPort + SLACK;
    PortDrawCell(nCell, nDest);
    Compare(rDrawCell, ring, args);
  }
  printf("draw_cell: done (%lu ms)\n", GetTickCount() - t0);

  for (int n = 0; n < nCalls; ++n) {
    // The record sits at an odd address, as creep.cpp's does (a group plus
    // 8 * edge - 2).
    BYTE abRecord[16];
    BYTE *pRecord = abRecord + 2 + (Rand32() & 1);
    int nWidth = 1 + (Rand32() & 3 ? RandBelow(64) : RandBelow(255));
    int nLines = 1 + (Rand32() & 3 ? RandBelow(64) : RandBelow(255));
    const BYTE *pFrame = s_abFrame;
    MakeCreepFrame(s_abFrame, sizeof s_abFrame, nWidth, &nLines);
    pRecord[0] = static_cast<BYTE>(Rand32() >> 24);
    pRecord[1] = static_cast<BYTE>(Rand32() >> 24);
    pRecord[2] = static_cast<BYTE>(nWidth);
    pRecord[3] = static_cast<BYTE>(nLines);
    memcpy(pRecord + 4, &pFrame, 4);
    int nDest = RandBelow(RING_SIZE);
    if ((Rand32() & 3) == 0) {
      nDest = NearRingEnd(nLines + 1) - pRecord[1] * RING_PITCH - pRecord[0];
      if (nDest < 0)
        nDest += RING_SIZE;
    }
    sprintf(args, "at %u,%u size %ux%u, %d", pRecord[0], pRecord[1], nWidth, nLines, nDest);
    SCROLL_BUF = ring.pOracle + SLACK;
    oracleRleCreep(pRecord, nDest);
    SCROLL_BUF = ring.pPort + SLACK;
    PortRleCreep(pRecord, nDest);
    Compare(rRleCreep, ring, args);
  }
  printf("rle_creep: done (%lu ms)\n", GetTickCount() - t0);

  // The copies read the ring, so both take one ring and write two screens.
  SCROLL_BUF = ring.pOracle + SLACK;
  for (int n = 0; n < nCalls; ++n) {
    switch (Rand32() & 3) {
    case 0:
    case 1:
      SCROLL_X = RandBelow(8192 - 640) & ~7;
      SCROLL_Y = RandBelow(8192 - 400) & ~7;
      break;
    case 2:
      SCROLL_X = RandBelow(8192);
      SCROLL_Y = RandBelow(8192);
      break;
    default: {
      // A row of the view ends at the ring's end.
      const int nOfs = NearRingEnd(400) - 640;
      SCROLL_X = nOfs % RING_PITCH;
      SCROLL_Y = nOfs / RING_PITCH;
      break;
    }
    }
    sprintf(args, "scroll %ld,%ld", SCROLL_X, SCROLL_Y);
    DRAW_BITS = screen.pOracle + SLACK;
    oracleCopyScrollbuf();
    DRAW_BITS = screen.pPort + SLACK;
    PortCellCopyScrollbuf();
    Compare(rCopy, screen, args);
  }
  printf("cell_copy_scrollbuf: done (%lu ms)\n", GetTickCount() - t0);

  for (int n = 0; n < nCalls; ++n) {
    int nRow, nX, nWidth, nOfs;
    const unsigned uKind = Rand32() & 3;
    if (uKind != 3) {
      const int nCol = RandBelow(40);
      nRow = RandBelow(25) * 16;
      nX = nCol * 16;
      nWidth = (1 + RandBelow(40 - nCol)) * 16;
      nOfs = (RandBelow(RING_SIZE / 16)) * 16;
      if (uKind == 2) {
        // A row of the strip ends at the ring's end.
        nOfs = NearRingEnd(16) - nWidth;
        if (nOfs < 0)
          nOfs += RING_SIZE;
      }
    } else {
      nRow = RandBelow(480 - 16 + 1);
      nX = RandBelow(640);
      nWidth = 1 + RandBelow(640 - nX);
      nOfs = RandBelow(RING_SIZE);
    }
    sprintf(args, "%d, %d, %d, %d", nRow, nX, nWidth, nOfs);
    DRAW_BITS = screen.pOracle + SLACK;
    oracleCopyScrollbufRow(nRow, nX, nWidth, nOfs);
    DRAW_BITS = screen.pPort + SLACK;
    PortCellCopyScrollbufRow(nRow, nX, nWidth, nOfs);
    Compare(rCopyRow, screen, args);
  }
  printf("cell_copy_scrollbuf_row: done (%lu ms)\n", GetTickCount() - t0);

  // The distortion routines, into a 640x480 target. The frame's pixels are
  // any byte (the EMP shockwave's distances), and byte_666460 holds 0 to 8 as
  // gray_pal_init leaves it.
  struct Distort {
    Routine r;
    RleDrawFn oracle;
    RleDrawFn port;
    bool bMirror;
  } distort[] = {
    {{"rle_draw_invis_asm"}, reinterpret_cast<RleDrawFn>(0x4063B5u), &PortRleDrawInvis, false},
    {{"rle_draw_invis_hflip_asm"}, reinterpret_cast<RleDrawFn>(0x406C09u), &PortRleDrawInvisHflip, true},
    {{"rle_draw_emp_asm"}, reinterpret_cast<RleDrawFn>(0x406533u), &PortRleDrawEmp, false},
    {{"rle_draw_emp_hflip_asm"}, reinterpret_cast<RleDrawFn>(0x406D8Eu), &PortRleDrawEmpHflip, true},
  };
  for (int i = 0; i < 256; ++i)
    BRIGHTNESS[i] = static_cast<BYTE>(RandBelow(9));
  TestBmp bmpOracle = {640, 480, screen.pOracle + SLACK};
  TestBmp bmpPort = {640, 480, screen.pPort + SLACK};
  for (int d = 0; d < 4; ++d) {
    for (int n = 0; n < nCalls; ++n) {
      BYTE abGraphic[8];
      const BYTE *pFrame = s_abFrame;
      int nFrameWidth = 1 + (Rand32() & 3 ? RandBelow(128) : RandBelow(255));
      int nFrameHeight = 1 + (Rand32() & 3 ? RandBelow(128) : RandBelow(255));
      MakeCreepFrame(s_abFrame, sizeof s_abFrame, nFrameWidth, &nFrameHeight);
      abGraphic[0] = abGraphic[1] = 0;
      abGraphic[2] = static_cast<BYTE>(nFrameWidth);
      abGraphic[3] = static_cast<BYTE>(nFrameHeight);
      memcpy(abGraphic + 4, &pFrame, 4);
      // The clip: any part of the frame, whole one time in four.
      int clip[4];
      if ((Rand32() & 3) == 0) {
        clip[0] = 0;
        clip[1] = 0;
        clip[2] = nFrameWidth;
        clip[3] = nFrameHeight;
      } else {
        clip[0] = RandBelow(nFrameWidth);
        clip[1] = RandBelow(nFrameHeight);
        clip[2] = 1 + RandBelow(nFrameWidth - clip[0]);
        clip[3] = 1 + RandBelow(nFrameHeight - clip[1]);
      }
      // Anywhere on the target, and one time in four in the corner a
      // distance wraps from: the bottom right, or the top left when mirrored.
      int nX = RandBelow(640 - clip[2] + 1);
      int nY = RandBelow(480 - clip[3] + 1);
      if ((Rand32() & 3) == 0) {
        nX = distort[d].bMirror ? 0 : 640 - clip[2];
        nY = distort[d].bMirror ? 0 : 480 - clip[3];
      }
      sprintf(args, "%d, %d, frame %dx%d, clip %d,%d %dx%d", nX, nY, nFrameWidth, nFrameHeight, clip[0], clip[1],
              clip[2], clip[3]);
      CURRENT_BMP = &bmpOracle;
      distort[d].oracle(nX, nY, abGraphic, clip, 0);
      CURRENT_BMP = &bmpPort;
      distort[d].port(nX, nY, abGraphic, clip, 0);
      Compare(distort[d].r, screen, args);
    }
    printf("%s: done (%lu ms)\n", distort[d].r.name, GetTickCount() - t0);
  }

  // img_clipped: the two blocks and the answer.
  const ImgClippedFn oracleImgClipped = reinterpret_cast<ImgClippedFn>(0x4CFE60u);
  Routine rClip = {"img_clipped"};
  for (int n = 0; n < nCalls * 5; ++n) {
    bmpsrc_t src, dst;
    src.nWidth = RandShort(1, 1024);
    src.nHeight = RandShort(1, 512);
    src.rc.left = RandEdge(src.nWidth, -300, src.nWidth + 50);
    src.rc.top = RandEdge(src.nHeight, -300, src.nHeight + 50);
    src.rc.right = RandEdge(src.nWidth, -50, src.nWidth + 300);
    src.rc.bottom = RandEdge(src.nHeight, -50, src.nHeight + 300);
    dst.nWidth = RandShort(1, 1712);
    dst.nHeight = RandShort(1, 480);
    dst.rc.left = RandEdge(dst.nWidth, -600, dst.nWidth + 50);
    dst.rc.top = RandEdge(dst.nHeight, -600, dst.nHeight + 50);
    dst.rc.right = RandShort(-1000, 1000);  // written before it is read
    dst.rc.bottom = RandShort(-1000, 1000);
    bmpsrc_t srcPort = src, dstPort = dst;
    const int nOracle = oracleImgClipped(&src, &dst);
    const int nPort = PortImgClipped(&srcPort, &dstPort);
    ++rClip.checked;
    if (nOracle != nPort || memcmp(&src, &srcPort, sizeof src) != 0 || memcmp(&dst, &dstPort, sizeof dst) != 0) {
      if (rClip.diffs++ < 10)
        printf("  DIFF img_clipped: answer %d, oracle %d; src %d,%d,%d,%d %dx%d, oracle's %d,%d,%d,%d %dx%d\n",
               nPort, nOracle, srcPort.rc.left, srcPort.rc.top, srcPort.rc.right, srcPort.rc.bottom,
               srcPort.nWidth, srcPort.nHeight, src.rc.left, src.rc.top, src.rc.right, src.rc.bottom, src.nWidth,
               src.nHeight);
    }
  }
  printf("img_clipped: done (%lu ms)\n", GetTickCount() - t0);

  // The two copies, into the screen pair from a random source with holes (the
  // mask's zeros), or between two places in the pair itself.
  struct Blt {
    Routine r;
    BltFn oracle;
    BltFn port;
  } blt[] = {
    {{"bmp_copy_rect_asm"}, reinterpret_cast<BltFn>(0x4CFF90u), &PortBmpCopyRect},
    {{"bmp_mask_rect_asm"}, reinterpret_cast<BltFn>(0x4CFFD7u), &PortBmpMaskRect},
  };
  static BYTE s_abSource[0x40000];
  FillRandom(s_abSource, sizeof s_abSource);
  for (int i = 0; i < static_cast<int>(sizeof s_abSource); i += 1 + RandBelow(8))
    s_abSource[i] = 0;
  for (int b = 0; b < 2; ++b) {
    for (int n = 0; n < nCalls; ++n) {
      const int nWidth = 1 + (Rand32() & 3 ? RandBelow(64) : RandBelow(640));
      const int nHeight = 1 + RandBelow(100);
      const int nSrcMod = RandBelow(400);
      const int nDstMod = RandBelow(400);
      const int nDstSpan = (nWidth + nDstMod) * nHeight;
      const int nSrcSpan = (nWidth + nSrcMod) * nHeight;
      const int nDst = RandBelow(SCREEN_SIZE - nDstSpan + 1);
      const bool bOverlap = b == 0 && (Rand32() & 3) == 0;
      const int nSrc = bOverlap ? nDst + RandBelow(9) - 4
                                : RandBelow(static_cast<int>(sizeof s_abSource) - nSrcSpan);
      sprintf(args, "%dx%d, gaps %d,%d%s", nWidth, nHeight, nSrcMod, nDstMod, bOverlap ? ", overlapping" : "");
      BLT_WIDTH = nWidth;
      BLT_HEIGHT = nHeight;
      BLT_SRC_MOD = nSrcMod;
      BLT_DST_MOD = nDstMod;
      BLT_SRC = bOverlap ? screen.pOracle + SLACK + nSrc : s_abSource + nSrc;
      BLT_DST = screen.pOracle + SLACK + nDst;
      blt[b].oracle();
      BLT_SRC = bOverlap ? screen.pPort + SLACK + nSrc : s_abSource + nSrc;
      BLT_DST = screen.pPort + SLACK + nDst;
      blt[b].port();
      Compare(blt[b].r, screen, args);
    }
    printf("%s: done (%lu ms)\n", blt[b].r.name, GetTickCount() - t0);
  }

  // draw_char: random glyph bits (every skip and colour, skips that carry past
  // a row's end or stop exactly at it) into a bitmap of any width, and one
  // time in four right at the screen's end.
  const DrawCharFn oracleDrawChar = reinterpret_cast<DrawCharFn>(0x4CFDF0u);
  Routine rChar = {"draw_char"};
  static BYTE s_abBits[64 * 64 + 16];
  static BYTE s_abColors[8];
  FONT_COLORS = s_abColors;
  for (int n = 0; n < nCalls * 5; ++n) {
    const int nWidth = 1 + (Rand32() & 3 ? RandBelow(16) : RandBelow(64));
    const int nHeight = 1 + (Rand32() & 3 ? RandBelow(16) : RandBelow(64));
    const int nPitch = nWidth + RandBelow(1712 - nWidth + 1);
    const int nSpan = nPitch * (nHeight - 1) + nWidth;
    FillRandom(s_abColors, sizeof s_abColors);
    FillRandom(s_abBits, sizeof s_abBits);
    if (Rand32() & 1) {
      // Mostly short skips, as a glyph's are, so that more is drawn.
      for (int i = 0; i < static_cast<int>(sizeof s_abBits); ++i)
        if (Rand32() & 1)
          s_abBits[i] &= 0x0F;
    }
    const int nDst = (Rand32() & 3) == 0 ? SCREEN_SIZE - nSpan : RandBelow(SCREEN_SIZE - nSpan + 1);
    sprintf(args, "%dx%d, pitch %d, at %d", nWidth, nHeight, nPitch, nDst);
    CHAR_WIDTH = nWidth;
    CHAR_HEIGHT = nHeight;
    CHAR_ROW_MOD = nPitch - nWidth;
    oracleDrawChar(screen.pOracle + SLACK + nDst, s_abBits);
    PortDrawChar(screen.pPort + SLACK + nDst, s_abBits);
    Compare(rChar, screen, args);
  }
  printf("draw_char: done (%lu ms)\n", GetTickCount() - t0);

  // shade_mtx: a cell anywhere in a bitmap of any width from 32, at any
  // alignment, with any pen (placebox's is a byte).
  typedef void(__fastcall *ShadeMtxFn)(int, int, const void *, unsigned long);
  const ShadeMtxFn oracleShadeMtx = reinterpret_cast<ShadeMtxFn>(0x405C12u);
  Routine rShade = {"shade_mtx"};
  for (int n = 0; n < nCalls; ++n) {
    const int nWidth = 32 + RandBelow(1712 - 32 + 1);
    const int nHeight = 32 + RandBelow(SCREEN_SIZE / nWidth - 32 + 1);
    const int nX = RandBelow(nWidth - 32 + 1);
    const int nY = RandBelow(nHeight - 32 + 1);
    const unsigned long dwPen = Rand32() & 1 ? Rand32() >> 24 : Rand32();
    TestBmp bmpO = {static_cast<short>(nWidth), static_cast<short>(nHeight), screen.pOracle + SLACK};
    TestBmp bmpP = {static_cast<short>(nWidth), static_cast<short>(nHeight), screen.pPort + SLACK};
    sprintf(args, "%d, %d, %dx%d, pen %#lx", nX, nY, nWidth, nHeight, dwPen);
    oracleShadeMtx(nX, nY, &bmpO, dwPen);
    PortShadeMtx(nX, nY, &bmpP, dwPen);
    Compare(rShade, screen, args);
  }
  printf("shade_mtx: done (%lu ms)\n", GetTickCount() - t0);

  // The other sprite routines, into the 640x480 target, with every table they
  // read filled at random: the remap, the shadow's row of shiftDark_g, the
  // brightness (any byte, for the uncloaked routines' signed test), and the
  // tables, texture and threshold the draw functions pass.
  static BYTE s_abEffect[256 * 256];
  static BYTE s_abFade[256];
  static BYTE s_abTexture[0x80000];
  FillRandom(reinterpret_cast<BYTE *>(0x501021u), 256);
  FillRandom(reinterpret_cast<BYTE *>(0x66DD20u), 32 * 256);
  FillRandom(s_abEffect, sizeof s_abEffect);
  FillRandom(s_abFade, sizeof s_abFade);
  FillRandom(s_abTexture, sizeof s_abTexture);
  enum { ARG_NONE, ARG_EFFECT, ARG_FADE, ARG_THRESHOLD, ARG_TEXTURE };
  struct Sprite {
    Routine r;
    RleDrawFn oracle;
    RleDrawFn port;
    bool bMirror;
    int nArg;
  } sprite[] = {
    {{"rle_draw_normal"}, reinterpret_cast<RleDrawFn>(0x405E1Eu), &PortRleDrawNormal, false, ARG_NONE},
    {{"rle_draw_hflip_"}, reinterpret_cast<RleDrawFn>(0x4071C0u), &PortRleDrawNormalHflip, true, ARG_NONE},
    {{"rle_draw_shadow"}, reinterpret_cast<RleDrawFn>(0x406950u), &PortRleDrawShadow, false, ARG_NONE},
    {{"rle_draw_shadow_hflip"}, reinterpret_cast<RleDrawFn>(0x40730Eu), &PortRleDrawShadowHflip, true, ARG_NONE},
    {{"rle_draw_effect"}, reinterpret_cast<RleDrawFn>(0x4067F6u), &PortRleDrawEffect, false, ARG_EFFECT},
    {{"rle_draw_effect_hflip"}, reinterpret_cast<RleDrawFn>(0x406F03u), &PortRleDrawEffectHflip, true, ARG_EFFECT},
    {{"rle_draw_fade"}, reinterpret_cast<RleDrawFn>(0x4066A1u), &PortRleDrawFade, false, ARG_FADE},
    {{"rle_draw_fade_hflip"}, reinterpret_cast<RleDrawFn>(0x407064u), &PortRleDrawFadeHflip, true, ARG_FADE},
    {{"rle_draw_uncloaked_part"}, reinterpret_cast<RleDrawFn>(0x406235u), &PortRleDrawUncloakedPart, false,
     ARG_THRESHOLD},
    {{"rle_draw_uncloaked_part_hflip"}, reinterpret_cast<RleDrawFn>(0x406A84u), &PortRleDrawUncloakedPartHflip,
     true, ARG_THRESHOLD},
    {{"rle_mask_warp_flash"}, reinterpret_cast<RleDrawFn>(0x405F64u), &PortRleMaskWarpFlash, false, ARG_TEXTURE},
    {{"rle_mask_warp_flash_hflip"}, reinterpret_cast<RleDrawFn>(0x4060C3u), &PortRleMaskWarpFlashHflip, true,
     ARG_TEXTURE},
  };
  const int nSprites = static_cast<int>(sizeof sprite / sizeof sprite[0]);
  for (int d = 0; d < nSprites; ++d) {
    for (int n = 0; n < nCalls; ++n) {
      BYTE abGraphic[8];
      const BYTE *pFrame = s_abFrame;
      // A frame is at most 255 lines (FRAME's height is a byte), so the clip's
      // top never reaches the carry into a mirrored skip; past it the skip runs
      // off the line into codes that loop the original away.
      int nFrameWidth = 1 + (Rand32() & 3 ? RandBelow(128) : RandBelow(255));
      int nFrameHeight = 1 + (Rand32() & 3 ? RandBelow(128) : RandBelow(255));
      MakeCreepFrame(s_abFrame, sizeof s_abFrame, nFrameWidth, &nFrameHeight);
      abGraphic[0] = abGraphic[1] = 0;
      abGraphic[2] = static_cast<BYTE>(nFrameWidth);
      abGraphic[3] = static_cast<BYTE>(nFrameHeight);
      memcpy(abGraphic + 4, &pFrame, 4);
      int clip[4];
      if ((Rand32() & 3) == 0) {
        clip[0] = 0;
        clip[1] = 0;
        clip[2] = nFrameWidth;
        clip[3] = nFrameHeight;
      } else {
        clip[0] = RandBelow(nFrameWidth);
        clip[1] = RandBelow(nFrameHeight);
        // Narrow clips one time in four, so that a transparent code often runs
        // past the whole of one.
        clip[2] = 1 + ((Rand32() & 3) == 0 ? RandBelow(nFrameWidth - clip[0] < 3 ? nFrameWidth - clip[0] : 3)
                                           : RandBelow(nFrameWidth - clip[0]));
        clip[3] = 1 + RandBelow(nFrameHeight - clip[1]);
      }
      const int nX = RandBelow(640 - clip[2] + 1);
      const int nY = RandBelow(480 - clip[3] + 1);
      unsigned long dwArg = 0;
      switch (sprite[d].nArg) {
      case ARG_EFFECT:
        dwArg = reinterpret_cast<unsigned long>(s_abEffect);
        break;
      case ARG_FADE:
        dwArg = reinterpret_cast<unsigned long>(s_abFade);
        break;
      case ARG_THRESHOLD:
        dwArg = Rand32();
        if (Rand32() & 1)
          FillRandom(BRIGHTNESS, 256);
        else
          for (int i = 0; i < 256; ++i)
            BRIGHTNESS[i] = static_cast<BYTE>(RandBelow(9));
        break;
      case ARG_TEXTURE:
        dwArg = reinterpret_cast<unsigned long>(s_abTexture + sizeof s_abTexture / 2);
        break;
      }
      sprintf(args, "%d, %d, frame %dx%d, clip %d,%d %dx%d, arg %#lx", nX, nY, nFrameWidth, nFrameHeight, clip[0],
              clip[1], clip[2], clip[3], dwArg);
      CURRENT_BMP = &bmpOracle;
      sprite[d].oracle(nX, nY, abGraphic, clip, dwArg);
      CURRENT_BMP = &bmpPort;
      sprite[d].port(nX, nY, abGraphic, clip, dwArg);
      Compare(sprite[d].r, screen, args);
    }
    printf("%s: done (%lu ms)\n", sprite[d].r.name, GetTickCount() - t0);
  }

  const Routine *all[] = {&rDrawCell, &rRleCreep, &rCopy, &rCopyRow,
                          &distort[0].r, &distort[1].r, &distort[2].r, &distort[3].r,
                          &rClip, &blt[0].r, &blt[1].r, &rChar, &rShade,
                          &sprite[0].r, &sprite[1].r, &sprite[2].r, &sprite[3].r, &sprite[4].r, &sprite[5].r,
                          &sprite[6].r, &sprite[7].r, &sprite[8].r, &sprite[9].r, &sprite[10].r, &sprite[11].r};
  int bad = 0;
  for (int i = 0; i < static_cast<int>(sizeof all / sizeof all[0]); ++i) {
    printf("%-24s %llu calls, %llu differences\n", all[i]->name, all[i]->checked, all[i]->diffs);
    bad += all[i]->diffs != 0;
  }
  printf(bad ? "FAIL\n" : "OK: identical to the oracle on every call\n");
  return bad ? 1 : 0;
}
