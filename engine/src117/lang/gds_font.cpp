// gds_font.cpp -- the bitmap fonts, 1.17's font.cpp: loading a .fnt file,
// measuring and wrapping text, drawing it glyph by glyph into the current
// bitmap with the colour codes, and Korean double-byte characters through GDI.

#include "oc_data.h"  // Hyperion: OC_VA
#include <intrin.h>
#include "../h/Storm.h"
#include "../h/gds_local.h"
#include "../h/rect16.h"
#include "../h/bmp.h"
#include "../h/dlg_edit.h"
#include "../h/gds_font.h"
#include "../h/dlg_draw.h"
#include "../port/render/port_maptext.h"  // Hyperion: map text

// Hyperion: the width of the character at p and its byte count, a character of several
// bytes measured as one, below font_char_width
static int FontWidthAt(const char *p, int *pnBytes);

// One glyph record: font_puts reads each of the four header bytes, and the
// pixel data starts at +4. The abBits[1] is a declaration convenience: the run
// of bits is variable length, so sizeof covers the header plus one byte.
struct FontGlyph
{
  /*0x00*/ BYTE bWidth;
  /*0x01*/ BYTE bHeight;
  /*0x02*/ BYTE bXOffset;
  /*0x03*/ BYTE bYOffset;
  /*0x04*/ BYTE abBits[1];
};
static_assert(offsetof(FontGlyph, bWidth) == 0x00, "font_puts: movzx ecx, byte ptr [ecx]");
static_assert(offsetof(FontGlyph, bHeight) == 0x01, "font_puts: movzx eax, byte ptr [ecx+1h]");
static_assert(offsetof(FontGlyph, bXOffset) == 0x02, "font_puts: movzx ecx, byte ptr [ecx+2h]");
static_assert(offsetof(FontGlyph, bYOffset) == 0x03, "font_puts: movzx esi, byte ptr [ecx+3h]");
static_assert(offsetof(FontGlyph, abBits) == 0x04, "font_puts: lea edx, [ecx+4h]");
static_assert(sizeof(FontGlyph) == 5, "FontGlyph size");

// One slot of the .fnt glyph table: a 4-byte offset from the start of the
// file. On x86 font_load turns each one into a pointer in place. On x64 a
// pointer does not fit the slot, so the table keeps its offsets and FONT_GLYPH
// adds the block's base when it reads one.
#if defined(_M_IX86)
typedef FontGlyph *font_glyph_slot_t;
#else
typedef DWORD font_glyph_slot_t;
#endif

// The loaded .fnt block. apGlyphs is the variable-length table font_load
// relocates; the [1] is a declaration convenience, not an extent claim.
struct FontHeader
{
  /*0x00*/ DWORD dwUnk0;
  /*0x04*/ BYTE bFirstChar;
  /*0x05*/ BYTE bLastChar;
  /*0x06*/ BYTE bMaxWidth;
  /*0x07*/ BYTE bMaxHeight;
  /*0x08*/ font_glyph_slot_t apGlyphs[1];
};
static_assert(offsetof(FontHeader, bFirstChar) == 0x04, "FontHeader::bFirstChar");
static_assert(offsetof(FontHeader, bLastChar) == 0x05, "FontHeader::bLastChar");
static_assert(offsetof(FontHeader, bMaxWidth) == 0x06, "FontHeader::bMaxWidth");
static_assert(offsetof(FontHeader, bMaxHeight) == 0x07, "FontHeader::bMaxHeight");
static_assert(offsetof(FontHeader, dwUnk0) == 0x00, "FontHeader::dwUnk0");
static_assert(offsetof(FontHeader, apGlyphs) == 0x08, "FontHeader::apGlyphs");
OC_X86_LAYOUT(sizeof(FontHeader) == 12, "FontHeader size");

// Glyph nIndex of the table, counted from bFirstChar. A 0 offset is not
// special: the oracle rebases it like any other, onto the header.
#if defined(_M_IX86)
#define FONT_GLYPH(pFont, nIndex) ((pFont)->apGlyphs[nIndex])
#else
#define FONT_GLYPH(pFont, nIndex) \
  reinterpret_cast<FontGlyph *>(reinterpret_cast<DWORD_PTR>(pFont) + (pFont)->apGlyphs[nIndex])
#endif

// Storm.h pulls <windows.h> in under NOGDI, so BITMAPINFO is not available and
// the two DIB types are restated here. sizeof(fontbmi_t) is the 0x428 bytes
// font_init_bitmapinfo allocates, a 256-entry palette.
struct fontquad_t
{
  /*0x00*/ BYTE bBlue;
  /*0x01*/ BYTE bGreen;
  /*0x02*/ BYTE bRed;
  /*0x03*/ BYTE bReserved;
};
static_assert(offsetof(fontquad_t, bBlue) == 0x00, "fontquad_t::bBlue");
static_assert(offsetof(fontquad_t, bGreen) == 0x01, "fontquad_t::bGreen");
static_assert(offsetof(fontquad_t, bRed) == 0x02, "fontquad_t::bRed");
static_assert(offsetof(fontquad_t, bReserved) == 0x03, "fontquad_t::bReserved");
static_assert(sizeof(fontquad_t) == 4, "fontquad_t size");

struct fontbmi_t
{
  /*0x00*/ DWORD dwSize;
  /*0x04*/ int nWidth;
  /*0x08*/ int nHeight;
  /*0x0C*/ WORD wPlanes;
  /*0x0E*/ WORD wBitCount;
  /*0x10*/ DWORD dwCompression;
  /*0x14*/ DWORD dwSizeImage;
  /*0x18*/ int nXPelsPerMeter;
  /*0x1C*/ int nYPelsPerMeter;
  /*0x20*/ DWORD dwClrUsed;
  /*0x24*/ DWORD dwClrImportant;
  /*0x28*/ fontquad_t aColors[256];
};
static_assert(offsetof(fontbmi_t, dwSize) == 0x00, "fontbmi_t::dwSize");
static_assert(offsetof(fontbmi_t, nWidth) == 0x04, "fontbmi_t::nWidth");
static_assert(offsetof(fontbmi_t, nHeight) == 0x08, "fontbmi_t::nHeight");
static_assert(offsetof(fontbmi_t, wPlanes) == 0x0C, "fontbmi_t::wPlanes");
static_assert(offsetof(fontbmi_t, dwCompression) == 0x10, "fontbmi_t::dwCompression");
static_assert(offsetof(fontbmi_t, dwSizeImage) == 0x14, "fontbmi_t::dwSizeImage");
static_assert(offsetof(fontbmi_t, nYPelsPerMeter) == 0x1C, "fontbmi_t::nYPelsPerMeter");
static_assert(offsetof(fontbmi_t, dwClrUsed) == 0x20, "fontbmi_t::dwClrUsed");
static_assert(offsetof(fontbmi_t, dwClrImportant) == 0x24, "fontbmi_t::dwClrImportant");
static_assert(offsetof(fontbmi_t, wBitCount) == 0x0E, "fontbmi_t::wBitCount");
static_assert(offsetof(fontbmi_t, nXPelsPerMeter) == 0x18, "fontbmi_t::nXPelsPerMeter");
static_assert(offsetof(fontbmi_t, aColors) == 0x28, "fontbmi_t::aColors");
static_assert(sizeof(fontbmi_t) == 0x428, "fontbmi_t size");

// LOGFONTA. font_putc_wide writes lfHeight at +0x00, lfWeight at +0x10,
// lfCharSet at +0x17 and lfFaceName at +0x1C, and memsets 0x3C bytes. The
// fields in between are Microsoft's layout.
struct font_logfont_t {
  /*0x00*/ LONG lfHeight;
  /*0x04*/ LONG lfWidth;
  /*0x08*/ LONG lfEscapement;
  /*0x0C*/ LONG lfOrientation;
  /*0x10*/ LONG lfWeight;
  /*0x14*/ BYTE lfItalic;
  /*0x15*/ BYTE lfUnderline;
  /*0x16*/ BYTE lfStrikeOut;
  /*0x17*/ BYTE lfCharSet;
  /*0x18*/ BYTE lfOutPrecision;
  /*0x19*/ BYTE lfClipPrecision;
  /*0x1A*/ BYTE lfQuality;
  /*0x1B*/ BYTE lfPitchAndFamily;
  /*0x1C*/ char lfFaceName[32];
};

static_assert(sizeof(font_logfont_t) == 0x3C, "font_putc_wide: push 3Ch into memset");
static_assert(offsetof(font_logfont_t, lfHeight) == 0x00, "mov dword ptr [ebp-74h], eax");
static_assert(offsetof(font_logfont_t, lfWeight) == 0x10, "mov dword ptr [ebp-64h], 2BCh");
static_assert(offsetof(font_logfont_t, lfCharSet) == 0x17, "mov byte ptr [ebp-5Dh], 81h");
static_assert(offsetof(font_logfont_t, lfFaceName) == 0x1C, "mov dword ptr [ebp-58h], ecx");
static_assert(offsetof(font_logfont_t, lfWidth) == 0x04, "Microsoft's layout");
static_assert(offsetof(font_logfont_t, lfEscapement) == 0x08, "Microsoft's layout");
static_assert(offsetof(font_logfont_t, lfOrientation) == 0x0C, "Microsoft's layout");
static_assert(offsetof(font_logfont_t, lfItalic) == 0x14, "Microsoft's layout");
static_assert(offsetof(font_logfont_t, lfUnderline) == 0x15, "Microsoft's layout");
static_assert(offsetof(font_logfont_t, lfStrikeOut) == 0x16, "Microsoft's layout");
static_assert(offsetof(font_logfont_t, lfOutPrecision) == 0x18, "Microsoft's layout");
static_assert(offsetof(font_logfont_t, lfClipPrecision) == 0x19, "Microsoft's layout");
static_assert(offsetof(font_logfont_t, lfQuality) == 0x1A, "Microsoft's layout");
static_assert(offsetof(font_logfont_t, lfPitchAndFamily) == 0x1B, "Microsoft's layout");

// BITMAP. font_putc_wide reads bmWidth, bmHeight and bmWidthBytes and keeps
// its shadow copy of the bits in bmBits.
struct font_bitmap_t {
  /*0x00*/ LONG  bmType;
  /*0x04*/ LONG  bmWidth;
  /*0x08*/ LONG  bmHeight;
  /*0x0C*/ LONG  bmWidthBytes;
  /*0x10*/ WORD  bmPlanes;
  /*0x12*/ WORD  bmBitsPixel;
  /*0x14*/ void *bmBits;
};

OC_X86_LAYOUT(sizeof(font_bitmap_t) == 0x18, "font_putc_wide: push 18h into GetObjectA");
static_assert(offsetof(font_bitmap_t, bmWidth) == 0x04, "mov esi, dword ptr [ebp-34h]");
static_assert(offsetof(font_bitmap_t, bmHeight) == 0x08, "mov ecx, dword ptr [ebp-30h]");
static_assert(offsetof(font_bitmap_t, bmWidthBytes) == 0x0C, "imul eax, dword ptr [ebp-2Ch]");
OC_X86_LAYOUT(offsetof(font_bitmap_t, bmBits) == 0x14, "mov dword ptr [ebp-24h], eax");
static_assert(offsetof(font_bitmap_t, bmType) == 0x00, "Microsoft's layout");
static_assert(offsetof(font_bitmap_t, bmPlanes) == 0x10, "Microsoft's layout");
static_assert(offsetof(font_bitmap_t, bmBitsPixel) == 0x12, "Microsoft's layout");

// The block font_init_bitmapinfo (0x4CD080) allocates: a BITMAPINFOHEADER and
// a 256-entry palette. Only the two fields font_putc_wide writes are named.

extern "C" {
__declspec(dllimport) int __stdcall MulDiv(int nNumber, int nNumerator, int nDenominator);
__declspec(dllimport) int __stdcall GetDeviceCaps(void *hdc, int nIndex);
__declspec(dllimport) void *__stdcall CreateFontIndirectA(const font_logfont_t *plf);
__declspec(dllimport) void *__stdcall SelectObject(void *hdc, void *hObject);
__declspec(dllimport) void *__stdcall CreateCompatibleDC(void *hdc);
__declspec(dllimport) int __stdcall SetBkMode(void *hdc, int nMode);
__declspec(dllimport) DWORD __stdcall SetTextColor(void *hdc, DWORD crColor);
__declspec(dllimport) DWORD __stdcall SetBkColor(void *hdc, DWORD crColor);
__declspec(dllimport) void *__stdcall GetStockObject(int i);
__declspec(dllimport) void *__stdcall CreateDIBSection(void *hdc, const fontbmi_t *pbmi,
                                                       unsigned int iUsage, void **ppvBits,
                                                       void *hSection, DWORD dwOffset);
__declspec(dllimport) BOOL __stdcall Rectangle(void *hdc, int nLeft, int nTop,
                                               int nRight, int nBottom);
__declspec(dllimport) int __stdcall GetObjectA(void *hObject, int cb, void *pv);
__declspec(dllimport) LONG __stdcall GetBitmapBits(void *hbm, LONG cb, void *pvBits);
__declspec(dllimport) BOOL __stdcall DeleteObject(void *hObject);
}

#include "../gen/globals117.h"

// The globals of 1.17's font.cpp this file reaches, one #ifdef per name.
#ifdef OC_DLL_MODE
#define byte_6D3114 (*reinterpret_cast<BYTE *>(OC_VA(0x6D3114u)))
#else
extern BYTE byte_6D3114;
#endif
#ifdef OC_DLL_MODE
#define sgdwDefaultCharWidth_g (*reinterpret_cast<DWORD *>(OC_VA(0x6D3008u)))
#else
extern DWORD sgdwDefaultCharWidth_g;
#endif
#ifdef OC_DLL_MODE
#define sgcPrevExtChar_g (*reinterpret_cast<BYTE *>(OC_VA(0x6D300Cu)))
#else
extern BYTE sgcPrevExtChar_g;
#endif
#ifdef OC_DLL_MODE
#define gbFontTabSize (*reinterpret_cast<BYTE *>(OC_VA(0x6D300Fu)))
#else
extern BYTE gbFontTabSize;
#endif
#ifdef OC_DLL_MODE
#define sgpFont (*reinterpret_cast<FontHeader **>(OC_VA(0x6D3020u)))
#else
extern FontHeader *sgpFont;
#endif
#ifdef OC_DLL_MODE
#define gnFontCurrY (*reinterpret_cast<int *>(OC_VA(0x6D304Cu)))
#else
extern int gnFontCurrY;
#endif

#ifdef OC_DLL_MODE
#define gpFontColors (*reinterpret_cast<BYTE **>(OC_VA(0x6D3010u)))
#else
extern BYTE *gpFontColors;
#endif
#ifdef OC_DLL_MODE
#define gbFontLineExtra (*reinterpret_cast<BYTE *>(OC_VA(0x6D3034u)))
#else
extern BYTE gbFontLineExtra;
#endif
#ifdef OC_DLL_MODE
#define gbFontMaxHgt (*reinterpret_cast<BYTE *>(OC_VA(0x6D3037u)))
#else
extern BYTE gbFontMaxHgt;
#endif
#ifdef OC_DLL_MODE
#define gnFontCurrX (*reinterpret_cast<int *>(OC_VA(0x6D3048u)))
#else
extern int gnFontCurrX;
#endif
#ifdef OC_DLL_MODE
#define sghWideFonts_g (reinterpret_cast<void **>(OC_VA(0x6D3118u)))
#else
extern void *sghWideFonts_g[3] OC_UNALIGNED;
#endif
#ifdef OC_DLL_MODE
#define sghDCCompat_g (*reinterpret_cast<void **>(OC_VA(0x6D3124u)))
#else
extern void *sghDCCompat_g;
#endif
#ifdef OC_DLL_MODE
#define sghWndDC_g (*reinterpret_cast<void **>(OC_VA(0x6D3128u)))
#else
extern void *sghWndDC_g;
#endif
#ifdef OC_DLL_MODE
#define sgpWideFontBitmapInfo_g (*reinterpret_cast<fontbmi_t **>(OC_VA(0x6D312Cu)))
#else
extern fontbmi_t *sgpWideFontBitmapInfo_g;
#endif
#ifdef OC_DLL_MODE
#define sgdwCurrentFontSet_g (*reinterpret_cast<DWORD *>(OC_VA(0x66CCC0u)))
#else
extern DWORD sgdwCurrentFontSet_g;
#endif
#ifdef OC_DLL_MODE
#define ghWnd (*reinterpret_cast<void **>(OC_VA(0x66FD28u)))
#else
extern void *ghWnd;
#endif

#ifdef OC_DLL_MODE
#define gbFontAllowWrap        (*reinterpret_cast<BYTE *>(OC_VA(0x50F160u)))
#else
extern BYTE gbFontAllowWrap;
#endif
#ifdef OC_DLL_MODE
#define gbFontMaxWdt           (*reinterpret_cast<BYTE *>(OC_VA(0x6D3036u)))
#else
extern BYTE gbFontMaxWdt;
#endif
#ifdef OC_DLL_MODE
#define sgpPrevColors          (*reinterpret_cast<BYTE **>(OC_VA(0x6D3110u)))
#else
extern BYTE *sgpPrevColors;
#endif
#ifdef OC_DLL_MODE
#define fontpalette_g          (reinterpret_cast<BYTE (*)[8]>(OC_VA(0x6D3050u)))
#else
extern BYTE fontpalette_g[24][8];
#endif
#ifdef OC_DLL_MODE
#define gbFontColorInhibit     (*reinterpret_cast<BYTE *>(OC_VA(0x6D300Du)))
#else
extern BYTE gbFontColorInhibit;
#endif
#ifdef OC_DLL_MODE
#define gbCharWdt              (*reinterpret_cast<int *>(OC_VA(0x6D3014u)))
#else
extern int gbCharWdt;
#endif
#ifdef OC_DLL_MODE
#define gbCharHgt              (*reinterpret_cast<int *>(OC_VA(0x6D3018u)))
#else
extern int gbCharHgt;
#endif
#ifdef OC_DLL_MODE
#define gdwRemainingCharSpace_g (*reinterpret_cast<int *>(OC_VA(0x6D301Cu)))
#else
extern int gdwRemainingCharSpace_g;
#endif

#define OC_FONT_CPP "font.cpp"

// font_load's EDX. init.cpp declares it `void *`, so the typedef stays inside
// this file and out of every prototype.
typedef void *(__fastcall *PFN_FONTLOADFILE)(const char *pszPath, int nArg1,
                                             DWORD *pdwSize, int nArg3,
                                             int nArg4, const char *pszFile,
                                             int nLine);

// oracle 0x4CD080 (124 B). Build the 8bpp DIB description the wide-font blitter
// draws through, with a greyscale ramp for its palette.
void *__fastcall font_init_bitmapinfo()
{
  fontbmi_t *pbmi;
  int i;

  pbmi = reinterpret_cast<fontbmi_t *>(SMemAlloc(0x428, 0, 0, 0));
  pbmi->dwSize = 0x28;
  pbmi->nWidth = 0;
  pbmi->nHeight = 0;
  pbmi->wPlanes = 1;
  pbmi->wBitCount = 8;
  pbmi->dwCompression = 0;
  pbmi->dwSizeImage = 0;
  pbmi->nXPelsPerMeter = 0x375D;
  pbmi->nYPelsPerMeter = 0x375D;
  pbmi->dwClrUsed = 0;
  pbmi->dwClrImportant = 0;
  for (i = 0; i < 256; i++)
  {
    pbmi->aColors[i].bRed = static_cast<BYTE>(i);
    pbmi->aColors[i].bGreen = static_cast<BYTE>(i);
    pbmi->aColors[i].bBlue = static_cast<BYTE>(i);
    pbmi->aColors[i].bReserved = 0;
  }
  return pbmi;
}

// oracle 0x4CD100 (484 B). Measure psz from the current pen X and return where the
// line breaks: at a newline, at the right margin, or, with wrapping on, at the
// start of the word that overflows. The barrier in the wrap-off half keeps the
// shared newline return between the two halves, where 1.17 has it.
const char *check_wrap(const char *psz)
{
  const char *pStart = psz;
  char ch;
  int x;

  sgcPrevExtChar_g = 0;
  ch = *psz;
  if (!gbFontAllowWrap) {
    if (ch != '\0') {
      x = gnFontCurrX;
      do {
        if (ch == ' ') {
          x += sgpFont->bMaxWidth >> 1;
          gnFontCurrX = x;
        } else if (ch == '\t') {
          x += gbFontTabSize - (x - gsFontMargins.left) % gbFontTabSize;
          gnFontCurrX = x;
        } else if (ch == '\n' || ch == '\f') {
          _ReadWriteBarrier();
          goto newline;
        } else if (ch != 0x1A && ch != 0x12 && ch != 0x13
                   && !(ch == static_cast<char>(byte_6D3114)
                        && psz != pStart && psz[-1] == 4)) {
          int nBytes;
          x += FontWidthAt(psz, &nBytes);  // Hyperion
          gnFontCurrX = x;
          if (x > gsFontMargins.right)
            break;
          psz += nBytes - 1;
        }
        ch = *++psz;
      } while (ch != '\0');
    }
    if (sub_4C7730(pStart, static_cast<int>(psz - pStart)) == 2)
      --psz;
    psz = MapTextCharStart(pStart, psz);  // Hyperion: not inside a character
    return psz;
  newline:
    return psz + 1;
  }

  if (ch == '\0')
    return psz;
  x = gnFontCurrX;
  for (;;) {
    do {
      switch (ch) {
      case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06:
      case 0x07: case 0x08: case 0x0E: case 0x0F: case 0x10: case 0x11:
      case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1B:
      case 0x1C: case 0x1D: case 0x1E: case 0x1F:
        break;
      default:
        if (ch == ' ') {
          x += sgpFont->bMaxWidth >> 1;
          gnFontCurrX = x;
          break;
        }
        if (ch == '\t') {
          x += gbFontTabSize - (x - gsFontMargins.left) % gbFontTabSize;
          gnFontCurrX = x;
          break;
        }
        if (ch == '\n' || ch == '\f')
          goto newline;
        if (ch == 0x1A || ch == '\r' || ch == 0x12 || ch == 0x13)
          break;
        goto measure_word;
      }
      ch = *++psz;
    } while (ch != '\0');
  measure_word:
    {
      int nRight = gsFontMargins.right;
      int xWord;
      const char *pWord;

      if (x > nRight)
        return psz;
      ch = *psz;
      xWord = x;
      pWord = psz;
      while (ch != '\0') {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\f'
            || ch == 0x1A || ch == 0x12 || ch == 0x13)
          break;
        int nBytes;
        x += FontWidthAt(psz, &nBytes);  // Hyperion
        psz += nBytes;
        gnFontCurrX = x;
        ch = *psz;
      }
      if (x > nRight) {
        gnFontCurrX = xWord;
        return pWord;
      }
      ch = *psz;
      if (ch == '\0')
        return psz;
    }
  }
}

// oracle 0x4CDBF0 (61 B). Slide the caller's Y so the drawn block sits between
// it and the bottom margin; bit 0x20 of the justification mode halves both
// gaps, which is what centres it.
void font_adjust_vertical(int *pnY)
{
  int nY;
  int nToMargin;
  int nToCursor;

  nToMargin = gsFontMargins.bottom;
  nToCursor = gnFontCurrY;
  nY = *pnY;
  nToCursor -= nY;
  nToMargin -= nY;
  if ((gbFontJust & 0x20) != 0)
  {
    nToMargin /= 2;
    nToCursor /= 2;
  }
  *pnY = nY - nToCursor + nToMargin;
}

// oracle 0x4CDC30 (161 B). Advance width of one character. The switch operand
// is signed, so the range test starts at 1. font_strlen and string_width inline
// this body; it reads the globals in place, not through locals, as 1.17 does.
// Hyperion: the edit box (dlg_edit.cpp) measures typed bytes with it; this file's
// measuring goes through FontWidthAt, below, which takes a character of several bytes first
//.
int font_char_width(unsigned char bChar)
{
  FontGlyph *pGlyph;
  BYTE bPrev;

  switch (static_cast<char>(bChar))
  {
  case ' ':
    return sgpFont->bMaxWidth >> 1;
  case '\t':
    return gbFontTabSize;
  case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
  case 10: case 12: case 13: case 14: case 15: case 16: case 17:
  case 18: case 19: case 20: case 21: case 22: case 23: case 24:
  case 25: case 26: case 27: case 28: case 29: case 30: case 31:
    return 0;
  }

  if (sgdwDefaultCharWidth_g != 0)
  {
    if (bChar >= 0xA0)
    {
      bPrev = sgcPrevExtChar_g;
      if (bPrev >= 0xB0 || bPrev == 0xA4)
      {
        sgcPrevExtChar_g = 0;
        return sgdwDefaultCharWidth_g;
      }
      if (bChar >= 0xB0 || bChar == 0xA4)
      {
        sgcPrevExtChar_g = bChar;
        return sgdwDefaultCharWidth_g;
      }
    }
    sgcPrevExtChar_g = 0;
  }

  if (bChar < sgpFont->bFirstChar || bChar > sgpFont->bLastChar)
    return 0;
  pGlyph = FONT_GLYPH(sgpFont, bChar - sgpFont->bFirstChar);
  if (pGlyph == 0)
    return 0;
  return pGlyph->bXOffset + pGlyph->bWidth + gbFontCharExtra;
}

// Hyperion: a map's text as StarCraft: Remastered reads it (port_maptext.h).
// The glyph for a character of several bytes: the font's own where Windows-1252, the font's
// set, has the character, else one made from a TrueType face; the font's '?' when no face has
// it.
static FontGlyph *FontCharGlyph(unsigned cp)
{
  const int b = MapTextByte1252(cp);
  if (b >= sgpFont->bFirstChar && b <= sgpFont->bLastChar)
    return FONT_GLYPH(sgpFont, b - sgpFont->bFirstChar);
  int nBaseline = sgpFont->bMaxHeight - 2;
  if ('A' >= sgpFont->bFirstChar && 'A' <= sgpFont->bLastChar) {
    const FontGlyph *pA = FONT_GLYPH(sgpFont, 'A' - sgpFont->bFirstChar);
    nBaseline = pA->bYOffset + pA->bHeight;
  }
  const void *p = MapTextGlyph(sgpFont->bMaxHeight, nBaseline, cp);
  if (p != NULL)
    return static_cast<FontGlyph *>(const_cast<void *>(p));
  return '?' >= sgpFont->bFirstChar && '?' <= sgpFont->bLastChar ? FONT_GLYPH(sgpFont, '?' - sgpFont->bFirstChar)
                                                                  : NULL;
}

static int FontWidthAt(const char *p, int *pnBytes)
{
  unsigned cp;
  const int n = MapTextChar(p, NULL, &cp);
  if (n > 1) {
    *pnBytes = n;
    const FontGlyph *pGlyph = FontCharGlyph(cp);
    return pGlyph != NULL ? pGlyph->bXOffset + pGlyph->bWidth + gbFontCharExtra : 0;
  }
  *pnBytes = 1;
  return font_char_width(static_cast<unsigned char>(*p));
}

// oracle 0x4CDD10 (22 B). Hand the font block back to Storm and clear the
// caller's handle. The clear happens through the pointer, not to it.
void font_free(void *pFont)
{
  *reinterpret_cast<DWORD *>(pFont) = 0;
  SMemFree(pFont, OC_FONT_CPP, 0x7C, 0);
}

// oracle 0x4CDD30 (292 B). How many bytes of pszText fit in nMaxWidth pixels,
// never splitting a double-byte character.
int string_width(const char *pszText, int nMaxWidth)
{
  const char *p = pszText;
  int nWidth;

  sgcPrevExtChar_g = 0;
  nWidth = 0;
  for (; *p != '\0'; p++)
  {
    if (*p == '\n')
    {
      nWidth = 0;
      continue;
    }
    int nBytes;
    nWidth += FontWidthAt(p, &nBytes);  // Hyperion
    if (nWidth > nMaxWidth)
      break;
    p += nBytes - 1;
  }
  if (sub_4C7730(pszText, (int)(p - pszText)) == 2)
    p--;
  p = MapTextCharStart(pszText, p);  // Hyperion: not inside a character
  return (int)(p - pszText);
}

// oracle 0x4CDE90 (39 B). The glyph pointer doubles as the predicate, so the
// caller's `int` is the pointer value and not a flag.
intptr_t font_isprint(unsigned char bChar)
{
  FontHeader *pFont;
  BYTE bFirst;

  pFont = sgpFont;
  bFirst = pFont->bFirstChar;
  if (bChar < bFirst || bChar > pFont->bLastChar)
    return 0;
  return reinterpret_cast<intptr_t>(FONT_GLYPH(pFont, bChar - bFirst));
}

// oracle 0x4CDEC0 (95 B). Load font\<name>.fnt through the caller's loader and
// turn the glyph table's file offsets into pointers.
void *__fastcall font_load(const char *pszName, void *pfnLoadFile,
                           const char *pszFile, int nLine)
{
  char szPath[0x104];
  DWORD dwSize;
  FontHeader *pFont;
#if defined(_M_IX86)
  DWORD *pdwEntry;
  int nCount;
#endif

  psnprintf(szPath, 0x104, "font\\%s.fnt", pszName);
  pFont = reinterpret_cast<FontHeader *>(
      reinterpret_cast<PFN_FONTLOADFILE>(pfnLoadFile)(
          szPath, 0, &dwSize, 0, 0, OC_FONT_CPP, 0x62));
#if defined(_M_IX86)
  nCount = pFont->bLastChar - pFont->bFirstChar + 1;
  pdwEntry = reinterpret_cast<DWORD *>(pFont->apGlyphs);
  while (nCount != 0)
  {
    *pdwEntry += reinterpret_cast<DWORD_PTR>(pFont);
    pdwEntry++;
    nCount--;
  }
#endif
  return pFont;
}

// oracle 0x4CE240 (7 B).
void font_set_caret(BYTE bValue)
{
  byte_6D3114 = bValue;
}

// oracle 0x4CE520 (8 B). Forget the half of an extended character that a
// previous call was holding on to.
void font_clear_ext_char()
{
  sgcPrevExtChar_g = 0;
}

// oracle 0x4CD310 (853 B). Render one double-byte character through GDI into a
// DIB section and stamp it into the destination bitmap twice, offset by a pixel
// so the second colour reads as a shadow. Returns the width DrawTextA measured.
// The GDI objects are created once and cached in globals.
int __fastcall font_putc_wide(BYTE *pDest, const char *pszText)
{
  font_logfont_t lf;
  font_bitmap_t bm;
  RECT rc;
  void *pvBits;
  fontbmi_t *pbmi;
  void *hOldBitmap;
  void *hBitmap;
  char szText[3];
  int x;
  int y;

  *reinterpret_cast<WORD *>(szText) = *reinterpret_cast<const WORD *>(pszText);
  szText[2] = '\0';

  if (sghWideFonts_g[0] == NULL)
  {
    void *hdc = GetDC(NULL);

    memset(&lf, 0, sizeof(lf));
    strcpy(lf.lfFaceName, "\xB1\xBC\xB8\xB2");
    lf.lfCharSet = 0x81;
    lf.lfHeight = -MulDiv(9, GetDeviceCaps(hdc, 0x5A), 72);
    sghWideFonts_g[0] = CreateFontIndirectA(&lf);
    lf.lfWeight = 700;
    lf.lfHeight = -MulDiv(10, GetDeviceCaps(hdc, 0x5A), 72);
    sghWideFonts_g[1] = CreateFontIndirectA(&lf);
    lf.lfWeight = 700;
    lf.lfHeight = -MulDiv(11, GetDeviceCaps(hdc, 0x5A), 72);
    sghWideFonts_g[2] = CreateFontIndirectA(&lf);
    ReleaseDC(NULL, static_cast<HDC>(hdc));
  }
  if (sghWndDC_g == NULL)
  {
    sghWndDC_g = GetDC(reinterpret_cast<HWND>(ghWnd));
    sghDCCompat_g = CreateCompatibleDC(sghWndDC_g);
    SetBkMode(sghDCCompat_g, 2);
    SetTextColor(sghDCCompat_g, 0xFFFFFF);
    SetBkColor(sghDCCompat_g, 0);
    SelectObject(sghDCCompat_g, GetStockObject(4));
    sgpWideFontBitmapInfo_g = static_cast<fontbmi_t *>(font_init_bitmapinfo());
  }
  if (sgdwCurrentFontSet_g == 1)
  {
    if (sgpFont == sgpFonts_g[0])
      SelectObject(sghDCCompat_g, sghWideFonts_g[0]);
    else if (sgpFont == sgpFonts_g[1])
      SelectObject(sghDCCompat_g, sghWideFonts_g[1]);
    else if (sgpFont == sgpFonts_g[2] || sgpFont == sgpFonts_g[3])
      SelectObject(sghDCCompat_g, sghWideFonts_g[2]);
  }
  else if (sgdwCurrentFontSet_g == 2)
  {
    if (sgpFont == sgpFonts_g[0])
      SelectObject(sghDCCompat_g, sghWideFonts_g[0]);
    else if (sgpFont == sgpFonts_g[1])
      SelectObject(sghDCCompat_g, sghWideFonts_g[0]);
    else if (sgpFont == sgpFonts_g[2] || sgpFont == sgpFonts_g[3])
      SelectObject(sghDCCompat_g, sghWideFonts_g[2]);
  }

  rc.left = 0;
  rc.top = 0;
  rc.right = 0;
  rc.bottom = 0;
  DrawTextA(static_cast<HDC>(sghDCCompat_g), szText, (int)strlen(szText), &rc, 0x400);
  pbmi = sgpWideFontBitmapInfo_g;
  pbmi->nWidth = (rc.right + 3) & ~3;
  pbmi->nHeight = (rc.bottom + 3) & ~3;
  hBitmap = CreateDIBSection(NULL, pbmi, 0, &pvBits, NULL, 0);
  hOldBitmap = SelectObject(sghDCCompat_g, hBitmap);
  Rectangle(sghDCCompat_g, rc.left - 1, rc.top - 1, rc.right + 1, rc.bottom + 1);
  DrawTextA(static_cast<HDC>(sghDCCompat_g), szText, (int)strlen(szText), &rc, 0);
  GetObjectA(hBitmap, sizeof(bm), &bm);
  bm.bmBits = SMemAlloc(bm.bmWidthBytes * bm.bmHeight, NULL, 0, 0);
  GetBitmapBits(hBitmap, bm.bmWidthBytes * bm.bmHeight, bm.bmBits);
  for (y = 0; y < bm.bmHeight; y++)
  {
    for (x = 0; x < bm.bmWidth; x++)
    {
      if (static_cast<BYTE *>(bm.bmBits)[y * bm.bmWidthBytes + x] != 0)
      {
        pDest[(y + 3) * reinterpret_cast<bmp_t *>(_bmp_current)->nWidth + x + 1] = gpFontColors[0];
        pDest[(y + 2) * reinterpret_cast<bmp_t *>(_bmp_current)->nWidth + x] = gpFontColors[1];
      }
    }
  }
  SMemFree(bm.bmBits, NULL, 0, 0);
  SelectObject(sghDCCompat_g, hOldBitmap);
  DeleteObject(hBitmap);
  return rc.right;
}

// oracle 0x4CD670 (855 B). Draw one already-wrapped run character by
// character, advancing gnFontCurrX and growing gFontRect.bottom. A glyph the
// font does not have ends the run; the colour codes are a switch and the
// control characters an if chain after it.
const char *font_puts(const char *psz, const char *pszEnd)
{
  const char *pszStart;
  BYTE *pDest;
  BYTE *pPendingDest;
  int nPendingX;
  int nX;
  int nAdvance;
  FontGlyph *pGlyph;

  nPendingX = 0;
  pPendingDest = NULL;
  nX = gnFontCurrX;
  pDest = reinterpret_cast<bmp_t *>(_bmp_current)->nWidth * gnFontCurrY
          + reinterpret_cast<bmp_t *>(_bmp_current)->pPixels + nX;
  pszStart = psz;
  for (; psz < pszEnd; psz++)
  {
    char c = *psz;

    if (c == '\0')
      break;
    if (c == '\x0C')
      break;
    switch (c)
    {
    case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
    case 14: case 15: case 16: case 17:
    case 21: case 22: case 23: case 24: case 25:
    case 27: case 28: case 29: case 30: case 31:
      if (gbFontColorInhibit == 0 && gpFontColors != fontpalette_g[3])
        font_set_color_tbl(c);
      continue;
    }
    if (c == '\n')
      continue;
    if (c == '\t')
    {
      pDest -= nX;
      nX += gbFontTabSize - (nX - gsFontMargins.left) % gbFontTabSize;
      pDest += nX;
      gnFontCurrX = nX;
      continue;
    }
    if (c == '\r')
      continue;
    if (c == '\x1A')
      continue;
    if (c == ' ')
    {
      nX += sgpFont->bMaxWidth / 2;
      pDest += sgpFont->bMaxWidth / 2;
      gnFontCurrX = nX;
      continue;
    }
    if (c == '\x12')
    {
      check_wrap(psz);
      nX = gnFontCurrX;
      pDest += gsFontMargins.right - nX;
      continue;
    }
    if (c == '\x13')
    {
      check_wrap(psz);
      nX = gnFontCurrX;
      pDest += (gsFontMargins.right - nX) >> 1;
      continue;
    }
    {
      // Hyperion: a character of several bytes, UTF-8 or a Korean map's CP949, one
      // glyph (port_maptext.h)
      unsigned cp;
      const int nChar = MapTextChar(psz, pszEnd, &cp);
      if (nChar > 1) {
        pGlyph = FontCharGlyph(cp);
        if (pGlyph == NULL)
          break;
        if (gbFontColorInhibit == 0) {
          gbCharHgt = pGlyph->bHeight;
          gbCharWdt = pGlyph->bWidth;
          gdwRemainingCharSpace_g = reinterpret_cast<bmp_t *>(_bmp_current)->nWidth - gbCharWdt;
          draw_char(pDest + pGlyph->bYOffset * reinterpret_cast<bmp_t *>(_bmp_current)->nWidth
                          + pGlyph->bXOffset,
                    pGlyph->abBits);
          nX = gnFontCurrX;
        }
        if (gFontRect.bottom <= pGlyph->bYOffset + gnFontCurrY + pGlyph->bHeight - 1)
          gFontRect.bottom = static_cast<short>(pGlyph->bYOffset + gnFontCurrY + pGlyph->bHeight - 1);
        nAdvance = pGlyph->bXOffset + pGlyph->bWidth + gbFontCharExtra;
        psz += nChar - 1;
        goto advance;
      }
    }
    if (static_cast<BYTE>(c) < sgpFont->bFirstChar
        || static_cast<BYTE>(c) > sgpFont->bLastChar)
      break;
    pGlyph = FONT_GLYPH(sgpFont, static_cast<BYTE>(c) - sgpFont->bFirstChar);
    if (pGlyph == NULL)
      break;
    if (static_cast<BYTE>(c) == byte_6D3114 && psz != pszStart && psz[-1] == 4)
    {
      pPendingDest = pDest;
      nPendingX = nX;
    }
    else if (gbFontColorInhibit == 0)
    {
      gbCharHgt = pGlyph->bHeight;
      gbCharWdt = pGlyph->bWidth;
      gdwRemainingCharSpace_g =
          reinterpret_cast<bmp_t *>(_bmp_current)->nWidth - gbCharWdt;
      if (GetUserDefaultLangID() == 0x412
          && IsDBCSLeadByte(static_cast<BYTE>(*psz)) == 1
          && static_cast<BYTE>(*psz) != 0xA9
          && static_cast<BYTE>(*psz) != 0x99)
      {
        nAdvance = font_putc_wide(pDest, psz);
        psz++;
        if (gFontRect.bottom <= gnFontCurrY - 1 + nAdvance)
          gFontRect.bottom = static_cast<short>(gnFontCurrY - 1 + nAdvance);
        nX = gnFontCurrX;
        goto advance;
      }
      draw_char(pDest + pGlyph->bYOffset * reinterpret_cast<bmp_t *>(_bmp_current)->nWidth
                      + pGlyph->bXOffset,
                pGlyph->abBits);
      nX = gnFontCurrX;
    }
    if (gFontRect.bottom <= pGlyph->bYOffset + gnFontCurrY + pGlyph->bHeight - 1)
      gFontRect.bottom = static_cast<short>(pGlyph->bYOffset + gnFontCurrY
                                            + pGlyph->bHeight - 1);
    if (static_cast<BYTE>(*psz) == byte_6D3114 && psz != pszStart && psz[-1] == 4)
      nAdvance = 0;
    else
      nAdvance = pGlyph->bXOffset + pGlyph->bWidth + gbFontCharExtra;
advance:
    nX += nAdvance;
    pDest += nAdvance;
    gnFontCurrX = nX;
  }
  if (pPendingDest != NULL && gbFontColorInhibit == 0)
  {
    pGlyph = FONT_GLYPH(sgpFont, byte_6D3114 - sgpFont->bFirstChar);
    gnFontCurrX = nPendingX;
    gbCharHgt = pGlyph->bHeight;
    gbCharWdt = pGlyph->bWidth;
    gdwRemainingCharSpace_g =
        reinterpret_cast<bmp_t *>(_bmp_current)->nWidth - gbCharWdt;
    draw_char(pPendingDest + pGlyph->bYOffset * reinterpret_cast<bmp_t *>(_bmp_current)->nWidth
                           + pGlyph->bXOffset,
              pGlyph->abBits);
  }
  return psz;
}

// oracle 0x4CD9F0 (505 B). Lay one paragraph out line by line, tracking the
// ink rectangle in gFontRect and the line count through EDX. Returns where it
// stopped, one past a form feed.
const char *__fastcall font_adjust_horizontal(const char *pszText, unsigned int *pnLines)
{
  gFontRect.left = 0x7FFF;
  gFontRect.right = 0;
  gFontRect.top = static_cast<short>(gnFontCurrY);
  gFontRect.bottom = static_cast<short>(gnFontCurrY);
  *pnLines = 1;

  while (*pszText != '\0')
  {
    int nStartX = gnFontCurrX;
    const char *pszEnd = check_wrap(pszText);
    int nWidth = gnFontCurrX - nStartX;

    if ((gbFontJust & 2) != 0)
    {
      gnFontCurrX = (gsFontMargins.right - nStartX) / 2 - nWidth / 2 + nStartX;
      if (gFontRect.right <= gnFontCurrX + nWidth)
        gFontRect.right = static_cast<short>(gnFontCurrX + nWidth);
      if (gFontRect.left >= gnFontCurrX)
        gFontRect.left = static_cast<short>(gnFontCurrX);
    }
    else if ((gbFontJust & 4) != 0)
    {
      gFontRect.right = gsFontMargins.right;
      gnFontCurrX = gsFontMargins.left + gsFontMargins.right - nWidth - nStartX;
      if (gFontRect.left >= gnFontCurrX)
        gFontRect.left = static_cast<short>(gnFontCurrX);
    }
    else
    {
      if (gFontRect.right <= gnFontCurrX)
        gFontRect.right = static_cast<short>(gnFontCurrX);
      gnFontCurrX = nStartX;
      if (gFontRect.left >= gnFontCurrX)
        gFontRect.left = static_cast<short>(gnFontCurrX);
    }

    pszText = font_puts(pszText, pszEnd);
    gnFontCurrX = gsFontMargins.left;
    gnFontCurrY += gbFontLineExtra + sgpFont->bMaxHeight;
    gFontRect.bottom += static_cast<short>(gbFontLineExtra - 1);
    ++*pnLines;
    if (*pszText == '\x0C')
      return pszText + 1;
    if (gbFontMaxHgt + gnFontCurrY > gsFontMargins.bottom)
      break;
  }

  gFontRect.bottom += static_cast<short>(1 - gbFontLineExtra);
  gnFontCurrY -= gbFontLineExtra;
  --*pnLines;
  return pszText;
}

// oracle 0x4CDF20 (567 B). Clamp the margins to the current bitmap, lay the
// string out from (nX, nY) when it starts inside them, and grow gFontRect by
// two on each side within the margins. Each edge is adjusted in place, which
// keeps its reads 16-bit so the values carry across the bounds test's join.
const char *__fastcall font_adjust(int nX, int nY, const char *psz)
{
  if (gsFontMargins.left < 0) {
    gsFontMargins.left = 0;
  }
  if (gsFontMargins.top < 0) {
    gsFontMargins.top = 0;
  }
  if (gsFontMargins.right >= reinterpret_cast<bmp_t *>(_bmp_current)->nWidth) {
    gsFontMargins.right = static_cast<short>(reinterpret_cast<bmp_t *>(_bmp_current)->nWidth - 1);
  }
  if (gsFontMargins.bottom >= reinterpret_cast<bmp_t *>(_bmp_current)->nHeight) {
    gsFontMargins.bottom = static_cast<short>(reinterpret_cast<bmp_t *>(_bmp_current)->nHeight - 1);
  }
  gFontRect.left = static_cast<short>(nX);
  gFontRect.right = static_cast<short>(nX);
  gFontRect.top = static_cast<short>(nY);
  gFontRect.bottom = static_cast<short>(nY);
  if (nX >= gsFontMargins.left && nY >= gsFontMargins.top
      && nX <= gsFontMargins.right
      && gbFontMaxHgt + nY <= gsFontMargins.bottom) {
    unsigned int nLines;
    BYTE bJust;
    BYTE bSaveJust;
    bJust = gbFontJust;
    gnFontCurrX = nX;
    gnFontCurrY = nY;
    nLines = 0;
    if ((bJust & 0x68) != 0) {
      gbFontColorInhibit = 1;
      font_adjust_horizontal(psz, &nLines);
      bJust = gbFontJust;
      gbFontColorInhibit = 0;
      if ((bJust & 0x10) == 0) {
        font_adjust_vertical(&nY);
      }
    }
    bSaveJust = bJust;
    if ((bJust & 8) != 0) {
      if (nLines > 1) {
        bJust ^= 9;
      } else {
        bJust ^= 0x0A;
      }
      gbFontJust = bJust;
    }
    gnFontCurrX = nX;
    gnFontCurrY = nY;
    psz = font_adjust_horizontal(psz, &nLines);
    gbFontJust = bSaveJust;
  }
  gFontRect.left -= 2;
  gFontRect.left = gsFontMargins.left > gFontRect.left ? gsFontMargins.left : gFontRect.left;
  gFontRect.top -= 2;
  gFontRect.top = gsFontMargins.top > gFontRect.top ? gsFontMargins.top : gFontRect.top;
  gFontRect.right += 2;
  gFontRect.right = gsFontMargins.right < gFontRect.right ? gsFontMargins.right : gFontRect.right;
  gFontRect.bottom += 2;
  gFontRect.bottom = gsFontMargins.bottom < gFontRect.bottom ? gsFontMargins.bottom : gFontRect.bottom;
  return *psz == 0 ? NULL : psz;
}

// oracle 0x4CE160 (223 B). Install a font and reset every drawing mode that
// depends on it. The Korean language check picks the default DBCS advance.
// Set 2 tests fonts 0 and 1 as two arms with the same store, as 1.17 does.
void __fastcall font_set(void *pFont)
{
  FontHeader *pF = static_cast<FontHeader *>(pFont);

  sgpFont = pF;
  if (pF == 0)
    return;

  gpFontColors = fontpalette_g[0];
  gbFontMaxWdt = pF->bMaxWidth;
  gbFontMaxHgt = pF->bMaxHeight;
  gbFontTabSize = static_cast<BYTE>(pF->bMaxWidth * 2);
  sgpPrevColors = fontpalette_g[0];
  gbFontLineExtra = 3;
  gbFontJust = 0x11;
  gbFontCharExtra = 1;
  gsFontMargins.left = 0;
  gsFontMargins.top = 0;
  gsFontMargins.right = 0x7FFF;
  gsFontMargins.bottom = 0x7FFF;

  if (GetUserDefaultLangID() != 0x412)
    return;
  if (sgdwCurrentFontSet_g == 1) {
    if (sgpFont == sgpFonts_g[0])
      sgdwDefaultCharWidth_g = 6;
    else if (sgpFont == sgpFonts_g[1])
      sgdwDefaultCharWidth_g = 7;
    else if (sgpFont == sgpFonts_g[2] || sgpFont == sgpFonts_g[3])
      sgdwDefaultCharWidth_g = 8;
  } else if (sgdwCurrentFontSet_g == 2) {
    if (sgpFont == sgpFonts_g[0])
      sgdwDefaultCharWidth_g = 6;
    else if (sgpFont == sgpFonts_g[1])
      sgdwDefaultCharWidth_g = 6;
    else if (sgpFont == sgpFonts_g[2] || sgpFont == sgpFonts_g[3])
      sgdwDefaultCharWidth_g = 8;
  }
}

// oracle 0x4CE250 (279 B). Point the renderer at one of the palette's colour
// tables; code 1 restores whatever was in force before the last change. Case 5
// precedes case 4, 1.17's order.
void __fastcall font_set_color_tbl(unsigned char bColor)
{
  if (bColor == 1) {
    gpFontColors = sgpPrevColors;
    return;
  }
  sgpPrevColors = gpFontColors;
  switch (bColor) {
  case 0x02: gpFontColors = fontpalette_g[0];  break;
  case 0x03: gpFontColors = fontpalette_g[1];  break;
  case 0x05: gpFontColors = fontpalette_g[3];  break;
  case 0x04: gpFontColors = fontpalette_g[2];  break;
  case 0x06: gpFontColors = fontpalette_g[4];  break;
  case 0x07: gpFontColors = fontpalette_g[5];  break;
  case 0x08: gpFontColors = fontpalette_g[8];  break;
  case 0x0E: gpFontColors = fontpalette_g[9];  break;
  case 0x0F: gpFontColors = fontpalette_g[10]; break;
  case 0x10: gpFontColors = fontpalette_g[11]; break;
  case 0x11: gpFontColors = fontpalette_g[12]; break;
  case 0x15: gpFontColors = fontpalette_g[13]; break;
  case 0x16: gpFontColors = fontpalette_g[14]; break;
  case 0x17: gpFontColors = fontpalette_g[15]; break;
  case 0x18: gpFontColors = fontpalette_g[16]; break;
  case 0x19: gpFontColors = fontpalette_g[17]; break;
  case 0x1B: gpFontColors = fontpalette_g[18]; break;
  case 0x1C: gpFontColors = fontpalette_g[19]; break;
  case 0x1D: gpFontColors = fontpalette_g[20]; break;
  case 0x1E: gpFontColors = fontpalette_g[21]; break;
  case 0x1F: gpFontColors = fontpalette_g[23]; break;
  }
}

// oracle 0x4CE3E0 (263 B). Width in pixels of the widest line of psz.
int font_strlen(const char *psz)
{
  int nMax;
  int nCur;
  sgcPrevExtChar_g = 0;
  nMax = 0;
  nCur = 0;
  for (; *psz != '\0'; psz++)
  {
    if (*psz == '\n')
    {
      if (nMax < nCur)
        nMax = nCur;
      nCur = 0;
    }
    else
    {
      int nBytes;
      nCur += FontWidthAt(psz, &nBytes);  // Hyperion
      psz += nBytes - 1;
    }
  }
  if (nMax < nCur)
    nMax = nCur;
  return nMax;
}
