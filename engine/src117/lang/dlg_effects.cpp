// dlg_effects.cpp -- the dialog layer's transparency and color-blend renderer,
// dlg/effects.cpp in the oracle, the name every SMemAlloc and SMemFree here
// tags itself with. Bodies are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle (WPO off) never does across a TU; that is what the
// __declspec(noinline) markers on some declarations are for, and why moving a
// body between TUs can change the code generated.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>              // memset / memcpy -- the oracle calls both

#include "../h/Storm.h"           // SMemAlloc / SMemFree
#include "../h/dlg_effects.h"
#include "../h/bmp.h"
#include "../gen/globals117.h"
#include "../port/render/port_storm.h"  // Hyperion: PortBltRop3, PortTableBlit

// Callees no header of ours declares, each with its calling convention written
// out.

// The four Storm entries this TU needs, by their oracle import thunks:
// SBltROP3 0x4D0316, SCodeCompile 0x4D033A, SCodeDelete 0x4D0340,
// SCodeExecute 0x4D0346, none of them in src117/h/Storm.h. The arity comes
// from the call sites, and no caller cleans up, so all four are
// callee-cleaned.
extern "C" {
int __stdcall SBltROP3(void *pDest, void *pSrc, int nWidth, int nHeight,
                       int nDestPitch, int nSrcPitch, int nUnused, DWORD dwRop);
int __stdcall SCodeCompile(const char *pszShort, const char *pszLong,
                           int nArg2, int nArg3, int nArg4, void **ppCode);
int __stdcall SCodeDelete(void *pCode);
int __stdcall SCodeExecute(void *pCode, void *pData);
}

// vid_lock_surface 0x4CB560 / vid_unlock_surface 0x4CB6D0 (gds/vidblit.cpp).
// The first wraps SDrawLockSurface and returns its BOOL, which this TU branches
// on; the second takes the same block, a flag and the rectangle to publish.
int __fastcall vid_lock_surface(vidlock_t *pLock);
int __fastcall vid_unlock_surface(vidlock_t *pLock, int nFlag, rect32_t *prc);

// font_adjust 0x4CDF20 and appcritf 0x4CC190 are declared here rather than in a
// header, as dlg_draw.h records.
const char *__fastcall font_adjust(int nX, int nY, const char *psz);
void __cdecl appcritf(const char *pszFmt, ...);

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.
// _bmp_current @0x6D0C40 is declared elsewhere and not restated here.

// dword_6CF7B0 -- effects_draw_surface stores (nMode == 5) into it and
// sub_4CA9B0 picks its slot pair on it. It also sits one dword below
// sgbTransCache, so sgbTransCache[n - 1] folds to [n*4 + 0x6CF7B0].
#ifdef OC_DLL_MODE
#define dword_6CF7B0 (*reinterpret_cast<int *>(OC_VA(0x6CF7B0u)))
#else
extern int dword_6CF7B0;
#endif

// sgbTransCache -- the three 64K blend tables. effects_init and effects_free
// both walk 0x6CF7B4 to 0x6CF7C0 in dword steps, which is the extent.
#ifdef OC_DLL_MODE
#define sgbTransCache (reinterpret_cast<BYTE **>(OC_VA(0x6CF7B4u)))
#else
extern BYTE *sgbTransCache[3] OC_UNALIGNED;
#endif

// sgEffectsPalette_g -- the cached 256-entry palette. effects_init memsets
// 0x400 bytes of it; effects_init_palette refills it with rep movsd, ecx=0x100.
#ifdef OC_DLL_MODE
#define sgEffectsPalette_g (reinterpret_cast<DWORD *>(OC_VA(0x6CF7C0u)))
#else
extern DWORD sgEffectsPalette_g[256];
#endif

// sgbTransCacheValid -- one flag per table. effects_free clears exactly three
// bytes (mov word ptr [0x6CFBC0], ax then mov byte ptr [0x6CFBC2], al) and
// effects_init_palette indexes 0..2.
#ifdef OC_DLL_MODE
#define sgbTransCacheValid (reinterpret_cast<BYTE *>(OC_VA(0x6CFBC0u)))
#else
extern BYTE sgbTransCacheValid[3];
#endif

// sgbLevel / byte_6CFD24 -- the current level of each of the two effect slots.
#ifdef OC_DLL_MODE
#define sgbLevel (*reinterpret_cast<BYTE *>(OC_VA(0x6CFBC3u)))
#else
extern BYTE sgbLevel;
#endif
#ifdef OC_DLL_MODE
#define byte_6CFD24 (*reinterpret_cast<BYTE *>(OC_VA(0x6CFD24u)))
#else
extern BYTE byte_6CFD24;
#endif

// sgbInit -- set by effects_init, cleared by effects_free. Nothing in this
// slice reads it.
#ifdef OC_DLL_MODE
#define sgbInit (*reinterpret_cast<int *>(OC_VA(0x6CFBC4u)))
#else
extern int sgbInit;
#endif

// sgnLastRenderType_g / sgnLastColor_g -- the cache key of gbEffectColorMap_g.
#ifdef OC_DLL_MODE
#define sgnLastRenderType_g (*reinterpret_cast<int *>(OC_VA(0x6CFBC8u)))
#else
extern int sgnLastRenderType_g;
#endif
#ifdef OC_DLL_MODE
#define sgnLastColor_g (*reinterpret_cast<int *>(OC_VA(0x50F0D0u)))
#else
extern int sgnLastColor_g;
#endif

// sgbTlucTbl / gpbEffectsTbl_g -- the two 64K maps, both SMemAlloc(0x10000).
#ifdef OC_DLL_MODE
#define sgbTlucTbl (*reinterpret_cast<BYTE **>(OC_VA(0x6CFBCCu)))
#else
extern BYTE *sgbTlucTbl;
#endif
#ifdef OC_DLL_MODE
#define gpbEffectsTbl_g (*reinterpret_cast<BYTE **>(OC_VA(0x6CFBD0u)))
#else
extern BYTE *gpbEffectsTbl_g;
#endif

// ghSCodeOther_g / ghSCodeTransparent_g -- SCodeCompile fills each through its
// last argument, and effects_free hands each back to SCodeDelete.
#ifdef OC_DLL_MODE
#define ghSCodeOther_g (*reinterpret_cast<void **>(OC_VA(0x6CFBD4u)))
#else
extern void *ghSCodeOther_g;
#endif
#ifdef OC_DLL_MODE
#define ghSCodeTransparent_g (*reinterpret_cast<void **>(OC_VA(0x6CFBD8u)))
#else
extern void *ghSCodeTransparent_g;
#endif

// sgbWantFlipSurface_g -- when set, effects_draw_surface draws through a locked
// surface instead of _bmp_current.
#ifdef OC_DLL_MODE
#define sgbWantFlipSurface_g (*reinterpret_cast<int *>(OC_VA(0x6CFBDCu)))
#else
extern int sgbWantFlipSurface_g;
#endif

// gSCodeData_g -- the shared SCode parameter block.
#ifdef OC_DLL_MODE
#define gSCodeData_g (*reinterpret_cast<scodedata_t *>(OC_VA(0x6CFBE0u)))
#else
extern scodedata_t gSCodeData_g;
#endif

// sgEffectCycleIndexes_g / sgdwNumEffectCycles_g -- the palette entries that
// cycle at run time and so are left out of the nearest-color buckets. The
// memcpy destination is 0x6CFC20 and the next name is 0x6CFD20, which is the
// 0x100 extent.
#ifdef OC_DLL_MODE
#define sgEffectCycleIndexes_g (reinterpret_cast<BYTE *>(OC_VA(0x6CFC20u)))
#else
extern BYTE sgEffectCycleIndexes_g[256];
#endif
#ifdef OC_DLL_MODE
#define sgdwNumEffectCycles_g (*reinterpret_cast<int *>(OC_VA(0x6CFD20u)))
#else
extern int sgdwNumEffectCycles_g;
#endif

// sgRenderTypeTbl_g -- percent to level. effects_init fills byte offsets 0 to
// 0x194, which is 101 dwords.
#ifdef OC_DLL_MODE
#define sgRenderTypeTbl_g (reinterpret_cast<int *>(OC_VA(0x6CFD28u)))
#else
extern int sgRenderTypeTbl_g[101] OC_UNALIGNED;
#endif

// sgnPowTbl_g -- squares of -255..255, so 511 dwords, and every read is
// [i*4 + 0x6D02BC] = &sgnPowTbl_g[255 + i]. 0x6CFEC0 + 511*4 = 0x6D06BC, which
// is the next name.
#ifdef OC_DLL_MODE
#define sgnPowTbl_g (reinterpret_cast<int *>(OC_VA(0x6CFEC0u)))
#else
extern int sgnPowTbl_g[511];
#endif
#ifdef OC_DLL_MODE
#define sgbPowTblInitialized_g (*reinterpret_cast<int *>(OC_VA(0x6D06BCu)))
#else
extern int sgbPowTblInitialized_g;
#endif

// dword_6D06C0 / dword_6D06C4 -- which row of sgbTlucTbl sub_4CA9B0 last
// overwrote, and at which level, so the next call can put it back. Only
// sub_4CA9B0 touches them, and dword_6D06C0 has to be static in the arm that
// defines it or its fill loop reloads it on every byte store.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define dword_6D06C0 (*reinterpret_cast<int *>(OC_VA(0x6D06C0u)))
#else
static int dword_6D06C0;
#endif
#ifdef OC_DLL_MODE
#define dword_6D06C4 (*reinterpret_cast<int *>(OC_VA(0x6D06C4u)))
#else
extern int dword_6D06C4;
#endif

// gbEffectColorMap_g -- the eight blended font colors effects_draw_text hands
// to font_adjust. The fill loop runs 0..7.
#ifdef OC_DLL_MODE
#define gbEffectColorMap_g (reinterpret_cast<BYTE *>(OC_VA(0x6D06C8u)))
#else
extern BYTE gbEffectColorMap_g[8];
#endif

// dword_50EF88 / dword_50EF90 -- the shade of each effect slot, and
// dword_50EF8C between them the flag that says one of them moved. All three
// start at 1 in .data.
#ifdef OC_DLL_MODE
#define dword_50EF88 (*reinterpret_cast<int *>(OC_VA(0x50EF88u)))
#else
extern int dword_50EF88;
#endif
#ifdef OC_DLL_MODE
#define dword_50EF8C (*reinterpret_cast<int *>(OC_VA(0x50EF8Cu)))
#else
extern int dword_50EF8C;
#endif
#ifdef OC_DLL_MODE
#define dword_50EF90 (*reinterpret_cast<int *>(OC_VA(0x50EF90u)))
#else
extern int dword_50EF90;
#endif

// stru_50EF98 -- the 26 neighbouring cells of an 18x18x18 color cube, as
// {dr, dg, db} triples. 26 is the loop bound and 0x50EF98 + 26*12 = 0x50F0D0,
// which is the next name.
#ifdef OC_DLL_MODE
#define stru_50EF98 (reinterpret_cast<int (*)[3]>(OC_VA(0x50EF98u)))
#else
extern int stru_50EF98[26][3] OC_UNALIGNED;
#endif

// gpFontColors @0x6D3010 and fontpalette_g @0x6D3050 belong to gds/font.cpp.
// effects_draw_text swaps the first around its font_adjust call and reads eight
// bytes out of two rows of the second, so the row stride is 8; the row count is
// not derivable here and the array is left unbounded.
#ifdef OC_DLL_MODE
#define gpFontColors (*reinterpret_cast<BYTE **>(OC_VA(0x6D3010u)))
#else
extern BYTE *gpFontColors;
#endif
#ifdef OC_DLL_MODE
#define fontpalette_g (reinterpret_cast<BYTE (*)[8]>(OC_VA(0x6D3050u)))
#else
extern BYTE fontpalette_g[][8];
#endif

// The bucket list effects_init_palette_table builds over the color cube. The
// node is five bytes, not eight: the oracle writes the index at +0 and the link
// at +1 (mov byte ptr [edi], bl / mov dword ptr [edx+1h], eax) and steps the
// cursor by 5.
#pragma pack(push, 1)
struct effects_node_t {
  /*0x00*/ BYTE bIndex;
  /*0x01*/ effects_node_t *pNext;
};
#pragma pack(pop)
OC_X86_LAYOUT(sizeof(effects_node_t) == 5, "effects_node_t size   effects_init_palette_table: add edx, 5h");

// A percent travels through sgRenderTypeTbl_g, which effects_init fills with
// i*7/100, so a level is 0..7: level 0 is opaque source, level 7 the special
// map at gpbEffectsTbl_g, and 1..6 index the three 64K blend tables through
// (b > 3 ? 6 - b : b - 1), so mirrored levels share a table and there are
// three and not six. The oracle recomputes that expression at every use, so it
// is spelled out at every use here.

// The bodies, in oracle VA order.

// 0x4C9C70 (1170 B). Nearest-color search over an 18x18x18 bucket grid: the
// blend of two palette entries is hashed into a cell, and only when that cell
// is empty are the 26 neighbours searched.
// The channel tables are one 2-D array so they keep the oracle's frame order,
// and only the inner entry is read through a pointer local.
int effects_init_palette_table(BYTE *pTable, const BYTE *pPalette, BYTE nLevel)
{
  effects_node_t *apBuckets[18][18][18];
  effects_node_t aNodes[256];
  int *apC[3][256];
  unsigned int aWSrc[256];
  unsigned int aWDst[256];
  effects_node_t *pNode;
  unsigned int dwBest;
  unsigned int d;
  unsigned int r, g, b;
  unsigned int r16, g16, b16;
  unsigned char k;
  BYTE bBest;
  BYTE bIdx;
  unsigned int i, j;
  int n;

  if (pTable == 0)
    pTable = (BYTE *)SMemAlloc(0x10000, "effects.cpp", 106, 0);

  if (!sgbPowTblInitialized_g) {
    sgbPowTblInitialized_g = 1;
    for (int m = -255; m <= 255; m++)
      sgnPowTbl_g[m + 255] = m * m;
  }

  for (i = 0; i < 256; i++) {
    apC[0][i] = &sgnPowTbl_g[255 + pPalette[i * 4 + 0]];
    apC[1][i] = &sgnPowTbl_g[255 + pPalette[i * 4 + 1]];
    apC[2][i] = &sgnPowTbl_g[255 + pPalette[i * 4 + 2]];
  }

  memset(apBuckets, 0, sizeof(apBuckets));  // Hyperion: 0x5B20 in 1.17, pointers

  for (unsigned int c = 0; c < 256; c++) {
    for (n = 0; n < sgdwNumEffectCycles_g; n++) {
      if (sgEffectCycleIndexes_g[n] > (int)c)
        break;
      if (sgEffectCycleIndexes_g[n] == c)
        goto next;
    }
    if (c == 0 || pPalette[c * 4 + 0] != pPalette[c * 4 - 4] ||
        pPalette[c * 4 + 1] != pPalette[c * 4 - 3] ||
        pPalette[c * 4 + 2] != pPalette[c * 4 - 2]) {
      int x = (pPalette[c * 4 + 0] >> 4) + 1;
      int y = (pPalette[c * 4 + 1] >> 4) + 1;
      int z = (pPalette[c * 4 + 2] >> 4) + 1;

      aNodes[c].bIndex = (BYTE)c;
      aNodes[c].pNext = apBuckets[x][y][z];
      apBuckets[x][y][z] = &aNodes[c];
    }
next:
    ;
  }

  for (i = 0; i < 256; i++) {
    aWDst[i] = i * (8 - nLevel);
    aWSrc[i] = i * nLevel;
  }

  for (j = 1; j < 256; j++) {
    for (i = 1; i < 256; i++) {
      const BYTE *pSrc = &pPalette[i * 4];
      bBest = (BYTE)j;
      dwBest = 0xFFFFFFFF;
      r = (aWSrc[pSrc[0]] + aWDst[pPalette[j * 4 + 0]]) >> 3;
      g = (aWSrc[pSrc[1]] + aWDst[pPalette[j * 4 + 1]]) >> 3;
      b = (aWSrc[pSrc[2]] + aWDst[pPalette[j * 4 + 2]]) >> 3;
      r16 = (r >> 4) + 1;
      g16 = (g >> 4) + 1;
      b16 = (b >> 4) + 1;
      r <<= 2;
      g <<= 2;
      b <<= 2;
      pNode = apBuckets[r16][g16][b16];
      if (pNode != 0) {
        if (pNode->pNext != 0) {
          do {
            bIdx = pNode->bIndex;
            d = *(int *)((BYTE *)apC[2][bIdx] - b) +
                *(int *)((BYTE *)apC[1][bIdx] - g) +
                *(int *)((BYTE *)apC[0][bIdx] - r);
            if (d < dwBest) {
              dwBest = d;
              bBest = bIdx;
            }
            pNode = pNode->pNext;
          } while (pNode != 0);
        } else {
          bBest = pNode->bIndex;
        }
      } else {
        for (k = 0; k < 26; k++) {
          if (dwBest <= 0x400)
            break;
          pNode = apBuckets[r16 + stru_50EF98[k][0]][g16 + stru_50EF98[k][1]]
                           [b16 + stru_50EF98[k][2]];
          while (pNode != 0) {
            bIdx = pNode->bIndex;
            d = *(int *)((BYTE *)apC[2][bIdx] - b) +
                *(int *)((BYTE *)apC[1][bIdx] - g) +
                *(int *)((BYTE *)apC[0][bIdx] - r);
            if (d < dwBest) {
              dwBest = d;
              bBest = bIdx;
            }
            pNode = pNode->pNext;
          }
        }
      }
      pTable[j * 0x100 + i] = bBest;
    }
    pTable[j * 0x101] = (BYTE)j;
  }

  for (int t = 0; t < 256; t++) {
    pTable[t] = (BYTE)t;
    pTable[t * 0x100] = (BYTE)t;
  }
  return 1;
}

// 0x4CA110 (348 B). The jump table at 0x4CA26C is this body's own tail, and
// its six entries put case 2 on the default arm.
void effects_draw_surface(unsigned char *pSrcBits, int *prcSrc, int *pUnused,
                          int nSrcPitch, int *prcDest, int nMode,
                          unsigned char *pbPercent)
{
  rect32_t *prcS = (rect32_t *)prcSrc;
  rect32_t *prcD = (rect32_t *)prcDest;
  vidlock_t lock;
  BYTE *pDestBits;
  int nDestPitch;
  int nWidth;
  int nHeight;
  int nSrcW;
  int nSrcH;

  nSrcW = prcS->right - prcS->left;
  nSrcH = prcS->bottom - prcS->top;
  nWidth = prcD->right - prcD->left;
  if (nSrcW < nWidth)
    nWidth = nSrcW;
  nHeight = prcD->bottom - prcD->top;
  if (nSrcH < nHeight)
    nHeight = nSrcH;

  if (sgbWantFlipSurface_g != 0) {
    if (!vid_lock_surface(&lock))
      return;
    pDestBits = lock.pBits;
    nDestPitch = lock.nPitch;
  } else {
    pDestBits = ((bmp_t *)_bmp_current)->pPixels;
    nDestPitch = ((bmp_t *)_bmp_current)->nWidth;
  }

  if (nWidth > 0 && nHeight > 0) {
    switch (nMode) {
    case 0:
      // Hyperion: the port's copy (port_storm.cpp), in place of Storm's SBltROP3.
      PortBltRop3(prcD->top * nDestPitch + prcD->left + pDestBits,
                  prcS->top * nSrcPitch + prcS->left + pSrcBits,
                  nWidth, nHeight, nDestPitch, nSrcPitch, 0, 0xCC0020);
      break;
    case 1:
      render_transparent(prcD->top * nDestPitch + prcD->left + pDestBits,
                         prcS->top * nSrcPitch + prcS->left + pSrcBits,
                         nWidth, nHeight, nDestPitch, nSrcPitch);
      break;
    case 3:
      sub_4CA8A0(prcD->top * nDestPitch + prcD->left + pDestBits,
                 prcS->top * nSrcPitch + prcS->left + pSrcBits,
                 nWidth, nHeight, nDestPitch, nSrcPitch, pbPercent);
      break;
    case 4:
    case 5:
      dword_6CF7B0 = (nMode == 5);
      sub_4CA9B0(prcD->top * nDestPitch + prcD->left + pDestBits,
                 prcS->top * nSrcPitch + prcS->left + pSrcBits,
                 nWidth, nHeight, nDestPitch, nSrcPitch);
      break;
    }
  }

  if (sgbWantFlipSurface_g != 0)
    vid_unlock_surface(&lock, 1, prcD);
}

// 0x4CA290 (234 B). The eight blended colors are cached on the level and the
// color together, so a repeat call with both unchanged skips the whole fill.
// The blend goes to its own local c; written back into c1 instead, the colors
// take AL and AH and the level EDX.
void effects_draw_text(const char *psz, int nX, int nY, int nColor,
                       int nPercent)
{
  BYTE *pSave;
  BYTE b;
  unsigned int u;

  b = (BYTE)sgRenderTypeTbl_g[(BYTE)nPercent];
  nPercent = b;
  if (sgnLastRenderType_g != nPercent || sgnLastColor_g != nColor) {
    const BYTE *p1 = fontpalette_g[nColor] - 1;
    const BYTE *p2 = fontpalette_g[2] - 1;
    for (u = 0; u < 8; u++) {
      BYTE c1 = *++p1;
      BYTE c2 = *++p2;
      BYTE c;

      if (b == 0)
        c = c1;
      else if (b == 7)
        c = c2;
      else if (b >= 3)
        c = sgbTransCache[b > 3 ? 6 - nPercent : nPercent - 1][c2 * 0x100 + c1];
      else
        c = sgbTransCache[nPercent - 1][c1 * 0x100 + c2];
      gbEffectColorMap_g[u] = c;
    }
    sgnLastColor_g = nColor;
    sgnLastRenderType_g = nPercent;
  }

  pSave = gpFontColors;
  gpFontColors = gbEffectColorMap_g;
  font_adjust(nX, nY, psz);
  gpFontColors = pSave;
}

// 0x4CA380 (159 B).
void effects_free()
{
  int i;

  // Hyperion: no Storm programs to delete; PortTableBlit (port_storm.cpp) needs none.
  sgbInit = 0;
  for (i = 0; i < 3; i++) {
    if (sgbTransCache[i] != 0) {
      SMemFree(sgbTransCache[i], "effects.cpp", 738, 0);
      sgbTransCache[i] = 0;
    }
  }
  memset(sgbTransCacheValid, 0, 3);
  SMemFree(sgbTlucTbl, "effects.cpp", 743, 0);
  sgbTlucTbl = 0;
  SMemFree(gpbEffectsTbl_g, "effects.cpp", 744, 0);
  gpbEffectsTbl_g = 0;
}

// 0x4CA420 (573 B). A null pDesc takes the same arm as flag bits 0 and 2 set, so
// the two pDesc tests below merge into one branch.
void effects_init(BYTE *pPalette, void *pDesc)
{
  effects_desc_t *pd = (effects_desc_t *)pDesc;
  BYTE *p;
  BYTE b;
  unsigned int u;
  int i;

  memset(sgEffectsPalette_g, 0, 0x400);
  for (u = 0; u < 101; u++)
    sgRenderTypeTbl_g[u] = (int)u * 7 / 100;

  if (pd != 0) {
    for (i = 0; i < pd->nLevelCount; i++) {
      b = (BYTE)sgRenderTypeTbl_g[pd->aPercents[i]];
      sgbTransCache[b > 3 ? 6 - b : b - 1] =
          (BYTE *)SMemAlloc(0x10000, "effects.cpp", 674, 0);
    }
  } else {
    for (i = 0; i < 3; i++)
      sgbTransCache[i] = (BYTE *)SMemAlloc(0x10000, "effects.cpp", 682, 0);
  }

  if (pd == 0 || (pd->nFlags & 4) != 0) {
    p = (BYTE *)SMemAlloc(0x10000, "effects.cpp", 688, 0);
    sgbTlucTbl = p;
    for (i = 0; i < 0x100; i++)
      p[i] = (BYTE)i;
    for (i = 1; i < 0x100; i++)
      memset(p + i * 0x100, i, 0x100);
  }

  if (pd == 0 || (pd->nFlags & 1) != 0) {
    p = (BYTE *)SMemAlloc(0x10000, "effects.cpp", 697, 0);
    gpbEffectsTbl_g = p;
    for (i = 0; i < 0x100; i++)
      p[i] = (BYTE)i;
    for (i = 1; i < 0x100; i++)
      memset(p + i * 0x100, i, 0x100);
  }

  sgbInit = 1;
  sgbWantFlipSurface_g = 0;
  if (pPalette != 0)
    effects_init_palette((const DWORD *)pPalette, pd);

  // Hyperion: 1.17 compiled two Storm programs here with SCodeCompile, a
  // lookup of each pixel in a 64 KB table by source and destination byte, the
  // source low in ghSCodeOther_g and high in ghSCodeTransparent_g.
  // PortTableBlit (port_storm.cpp) does both and needs nothing set up.
}

// 0x4CA660 (373 B). The leading compare returns early when the palette has not
// moved, which is what splits the epilogue in three.
// The compare is memcmp's inline `== 0` expansion (`sub ecx, 4 / jae`).
// The fill loop's `j = i - 1` local puts the cache walker's setup ahead of
// the valid array's base in the preheader.
void effects_init_palette(const DWORD *pPalette, effects_desc_t *pDesc)
{
  BYTE b;
  int n;
  int i;

  if (memcmp(pPalette, sgEffectsPalette_g, 0x400) == 0)
    return;

  dword_50EF8C = 1;
  sgnLastRenderType_g = 0xFF;
  memcpy(sgEffectsPalette_g, pPalette, 0x400);

  if (pDesc != 0) {
    n = pDesc->nCycleCount;
    sgdwNumEffectCycles_g = n;
    if (n != 0)
      memcpy(sgEffectCycleIndexes_g, pDesc->aCycleIndexes, n);

    memset(sgbTransCacheValid, 0, 3);
    for (i = 0; i < pDesc->nLevelCount; i++) {
      b = (BYTE)sgRenderTypeTbl_g[pDesc->aPercents[i]];
      if (b != 0 && b != 7) {
        n = b;
        if (sgbTransCacheValid[b > 3 ? 6 - n : n - 1] == 0) {
          effects_init_palette_table(sgbTransCache[b > 3 ? 6 - n : n - 1],
                                     (const BYTE *)sgEffectsPalette_g, b);
          sgbTransCacheValid[b > 3 ? 6 - n : n - 1] = 1;
        }
      }
    }
  } else {
    sgdwNumEffectCycles_g = 0;
    for (i = 1; i < 4; i++) {
      int j = i - 1;

      effects_init_palette_table(sgbTransCache[j],
                                 (const BYTE *)sgEffectsPalette_g, (BYTE)i);
      sgbTransCacheValid[j] = 1;
    }
  }
}

// 0x4CA7E0 (49 B).
void sub_4CA7E0(int nPercent, int bAlt)
{
  BYTE *pLevel;

  pLevel = bAlt ? &byte_6CFD24 : &sgbLevel;
  if (sgRenderTypeTbl_g[nPercent] != *pLevel) {
    *pLevel = (BYTE)sgRenderTypeTbl_g[nPercent];
    dword_50EF8C = 1;
  }
}

// 0x4CA820 (81 B).
void sub_4CA820(int nPercent, int nShade, int bAlt)
{
  BYTE *pLevel;
  int *pShade;

  pLevel = bAlt ? &byte_6CFD24 : &sgbLevel;
  pShade = bAlt ? &dword_50EF90 : &dword_50EF88;
  if (sgRenderTypeTbl_g[nPercent] != *pLevel || nShade != *pShade) {
    *pLevel = (BYTE)sgRenderTypeTbl_g[nPercent];
    *pShade = nShade;
    dword_50EF8C = 1;
  }
}

// 0x4CA880 (11 B).
void set_request_render(void)
{
  sgbWantFlipSurface_g = 1;
}

// 0x4CA890 (11 B).
void clear_request_render(void)
{
  sgbWantFlipSurface_g = 0;
}

// 0x4CA8A0 (176 B).
void sub_4CA8A0(BYTE *pDest, BYTE *pSrc, int nWidth, int nHeight,
                int nDestPitch, int nSrcPitch, BYTE *pbPercent)
{
  BYTE b;

  b = (BYTE)sgRenderTypeTbl_g[*pbPercent];
  if (b == 0)
    return;
  if (b == 7) {
    render_transparent(pDest, pSrc, nWidth, nHeight, nDestPitch, nSrcPitch);
    return;
  }

  // Hyperion: the port's table blit (port_storm.cpp), in place of Storm's SCodeExecute.
  PortTableBlit(pDest, pSrc, nWidth, nHeight, nDestPitch, nSrcPitch, sgbTransCache[b > 3 ? 6 - b : b - 1], b >= 3);
}

// 0x4CA950 (82 B).
void render_transparent(BYTE *pDest, BYTE *pSrc, int nWidth, int nHeight,
                        int nDestPitch, int nSrcPitch)
{
  // Hyperion: the port's table blit (port_storm.cpp), in place of Storm's SCodeExecute.
  PortTableBlit(pDest, pSrc, nWidth, nHeight, nDestPitch, nSrcPitch, gpbEffectsTbl_g, 1);
}

// 0x4CA9B0 (462 B). Only one row of sgbTlucTbl is live at a time: the previous
// one is put back before the new one is written, and dword_6D06C0 /
// dword_6D06C4 are what says which row and at which level. Both loops index
// the globals directly and the build loop keeps the level ternary, which is
// what lets MSVC hoist its table load late; each branch makes its own call.
void sub_4CA9B0(BYTE *pDest, BYTE *pSrc, int nWidth, int nHeight,
                int nDestPitch, int nSrcPitch)
{
  BYTE b;
  int nShade;
  int i;

  b = dword_6CF7B0 ? byte_6CFD24 : sgbLevel;
  nShade = dword_6CF7B0 ? dword_50EF90 : dword_50EF88;

  if (b == 7) {
    // Hyperion: the port's table blit (port_storm.cpp), in place of Storm's SCodeExecute.
    PortTableBlit(pDest, pSrc, nWidth, nHeight, nDestPitch, nSrcPitch, gpbEffectsTbl_g, 1);
  } else {
    if (dword_50EF8C != 0) {
      if (dword_6D06C0 != 0) {
        if (dword_6D06C4 >= 3) {
          memset(sgbTlucTbl + dword_6D06C0 * 0x100, dword_6D06C0, 0x100);
        } else {
          for (i = 0; i < 0x100; i++)
            sgbTlucTbl[dword_6D06C0 * 0x100 + i] = (BYTE)i;
        }
      }
      if (b == 0) {
        memset(sgbTlucTbl + nShade * 0x100, 0, 0x100);
      } else if (b >= 3) {
        memcpy(sgbTlucTbl + nShade * 0x100,
               sgbTransCache[b > 3 ? 6 - b : b - 1] + nShade * 0x100, 0x100);
      } else {
        for (i = 0; i < 0x100; i++)
          sgbTlucTbl[nShade * 0x100 + i] =
              sgbTransCache[b > 3 ? 6 - b : b - 1][i * 0x100 + nShade];
      }
      dword_6D06C0 = nShade;
      dword_6D06C4 = b;
      dword_50EF8C = 0;
    }
    // Hyperion: the port's table blit (port_storm.cpp), in place of Storm's SCodeExecute.
    PortTableBlit(pDest, pSrc, nWidth, nHeight, nDestPitch, nSrcPitch, sgbTlucTbl, 1);
  }
}
