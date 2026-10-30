// port_formats.cpp -- 1.17's file formats that hold pointers once loaded, laid out for the
// engine where a pointer does not fit the file's, and the saved game's 1.17-layout records
// (docs/modules/data.md; "Formats that hold pointers").
//
// PortGroupNative makes a GRP one block of the header, a frame table in FRAME's 64-bit
// layout (src117/h/grp.h; the file's +4 dword is the data's offset, bit 31 adding 256 to the
// width) and the file, so the game's one SMemFree frees all of it. PortDialogNative lays a
// .bin's 0x56-byte ctrl_t and 0x1E-byte flc_t records out as the engine's ctrl_t and
// dlgitem_t (src117/h/dialog.h, dlg_flc.h) in the order dlg_load_ctrl walks them: the root,
// the elements, then the file's bytes. Links and text offsets stay offsets into the new block,
// so dlg_load_ctrl's relocation runs as it did; a per-type dword that can be a pointer at run
// time is widened, zero-extended. The game frees the block through the root, as it did the file.
#include <windows.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include "grp.h"
#include "dialog.h"
#include "dlg_flc.h"
#include "Storm.h"
#include "snd.h"  // load_game_data
#include "port_stormapi.h"  // SFileOpenFileEx, SFileCloseFile
#include "port_formats.h"
#include "port_stormbase.h"  // PortMemSize

GROUP *__cdecl PortGroupNative(GROUP *pFile, bool bLines)
{
#if !defined(_WIN64) && !defined(__LP64__)
  // Only the tools that compile the engine at 1.17's widths build this for x86
  // (tools/wide_globals.py, x64census/); there FRAME is the file's.
  (void)bLines;
  return pFile;
#else
  if (!pFile)
    return NULL;
  const size_t size = PortMemSize(pFile);
  const size_t header = offsetof(GROUP, frames);
  const size_t fileFrame = 8;  // the file's frame: four bytes and the dword
  size_t n = *reinterpret_cast<const WORD *>(pFile) & 0x7FFF;
  if (size < header)
    n = 0;
  else if (n > (size - header) / fileFrame)
    n = (size - header) / fileFrame;
  const size_t table = header + n * sizeof(FRAME);
  // after the file, codes that end any line (each skips 127 pixels): a line's codes the file
  // cuts short end in them, not in the memory past the block
  const size_t slack = 512;
  BYTE *block = static_cast<BYTE *>(SMemAlloc(static_cast<DWORD>(table + size + slack),
                                              "port_formats.cpp", __LINE__, 0));
  BYTE *file = block + table;
  memcpy(file, pFile, size);
  memset(file + size, 0xFF, slack);
  memcpy(block, file, size < header ? size : header);
  GROUP *pGroup = reinterpret_cast<GROUP *>(block);
  for (size_t i = 0; i < n; i++) {
    const BYTE *from = file + header + i * fileFrame;
    FRAME *pFrame = &pGroup->frames[i];
    pFrame->bOffsetX = from[0];
    pFrame->_unmapped_01 = from[1];
    pFrame->bWidth = from[2];
    pFrame->bHeight = from[3];
    memcpy(&pFrame->dwData, from + 4, sizeof(DWORD));
    // a mod's frame whose line table or lines are past the file is drawn as nothing; a
    // classic file's frames may be raw pixels, with no line table
    const size_t at = pFrame->dwData & 0x7FFFFFFF;
    bool bInside = !bLines || (at <= size && 2u * pFrame->bHeight <= size - at);
    for (unsigned y = 0; bLines && bInside && y < pFrame->bHeight; y++)
      bInside = at + (file[at + 2 * y] | file[at + 2 * y + 1] << 8) < size;
    if (!bInside) {
      pFrame->bWidth = pFrame->bHeight = 0;
      pFrame->dwData &= 0x80000000;
    }
    pFrame->pData = file + (bInside ? at : 0);
    pFrame->pLines = NULL;
  }
  SMemFree(pFile, "port_formats.cpp", __LINE__, 0);
  return pGroup;
#endif
}

// The parallax stars (parallax\star.spk): a WORD layer count, a WORD star count a layer,
// then 8-byte records, a position and the file offset of the image drawn there, which
// scroll_init_game relocates in place. PortStarsNative lays them out as the engine's
// starent_t (scroll.cpp, two WORDs and a pointer) with the file after them, each offset moved
// to its image in that copy, so the relocation runs as it did.
struct PortStar {
  WORD wX;
  WORD wY;
  BYTE *pbImage;
};

unsigned short *__cdecl PortStarsNative(unsigned short *pFile)
{
  if (!pFile)
    return NULL;
  const size_t size = PortMemSize(pFile);
  const size_t fileStar = 8;  // the file's star: two WORDs and the dword
  const size_t layers = size >= 2 ? pFile[0] : 0;
  const size_t header = (layers + 1) * sizeof(WORD);
  if (header > size)
    return pFile;
  size_t n = 0;
  for (size_t i = 0; i < layers; i++)
    n += pFile[1 + i];
  if (n > (size - header) / fileStar)
    n = (size - header) / fileStar;
  const size_t stars = header + n * sizeof(PortStar);
  BYTE *block = static_cast<BYTE *>(SMemAlloc(static_cast<DWORD>(stars + size),
                                              "port_formats.cpp", __LINE__, 0));
  BYTE *file = block + stars;
  memcpy(file, pFile, size);
  memcpy(block, file, header);
  for (size_t i = 0; i < n; i++) {
    const BYTE *from = file + header + i * fileStar;
    PortStar star;
    DWORD off;
    memcpy(&star.wX, from, sizeof(WORD));
    memcpy(&star.wY, from + 2, sizeof(WORD));
    memcpy(&off, from + 4, sizeof(DWORD));
    star.pbImage = reinterpret_cast<BYTE *>(static_cast<UINT_PTR>(stars + off));
    memcpy(block + header + i * sizeof(PortStar), &star, sizeof(PortStar));
  }
  SMemFree(pFile, "port_formats.cpp", __LINE__, 0);
  return reinterpret_cast<WORD *>(block);
}

ULONG *__cdecl PortMegatilesLoad(const char *pszVx4)
{
  char szEx[MAX_PATH];
  wsprintfA(szEx, "%sex", pszVx4);
  HANDLE hFile;
  if (SFileOpenFileEx(NULL, szEx, 0, &hFile)) {
    SFileCloseFile(hFile);
    return static_cast<ULONG *>(load_game_data(szEx, NULL, NULL, "port_formats.cpp", __LINE__));
  }
  DWORD cb = 0;
  WORD *pFile = static_cast<WORD *>(load_game_data(pszVx4, NULL, &cb, "port_formats.cpp", __LINE__));
  if (pFile == NULL)
    return NULL;
  const size_t n = cb / sizeof(WORD);
  DWORD *pRefs = static_cast<DWORD *>(SMemAlloc(static_cast<DWORD>(n ? n * sizeof(DWORD) : sizeof(DWORD)),
                                                "port_formats.cpp", __LINE__, 0));
  for (size_t i = 0; i < n; i++)
    pRefs[i] = pFile[i];
  SMemFree(pFile, "port_formats.cpp", __LINE__, 0);
  return reinterpret_cast<ULONG *>(pRefs);
}

// The dialog file's 32-bit records.
static const DWORD kFileCtrl = 0x56;

static void DialogFail(const char *what)
{
  char msg[256];
  wsprintfA(msg, "Hyperion cannot read a dialog file: %s.", what);
  MessageBoxA(NULL, msg, "Hyperion", MB_OK | MB_ICONERROR);
  ExitProcess(1);
}

namespace {
struct DialogFile {
  const BYTE *p;
  size_t size;
  // a dword or a word of the file, 0 past its end
  DWORD D(DWORD off) const { DWORD v = 0; if (Has(off, sizeof v)) memcpy(&v, p + off, sizeof v); return v; }
  WORD W(DWORD off) const { WORD v = 0; if (Has(off, sizeof v)) memcpy(&v, p + off, sizeof v); return v; }
  bool Has(DWORD off, DWORD n) const { return off <= size && n <= size - off; }
};
}  // namespace

// Where the file's offset off lands in the new block, as an offset from it: the
// record of offs found there, records being stride apart from at, or else the same
// byte of the file's copy at copy. 0 stays 0.
static UINT_PTR DialogLink(const std::vector<DWORD> &offs, size_t stride, size_t at,
                           size_t copy, DWORD off)
{
  if (!off)
    return 0;
  std::vector<DWORD>::const_iterator it = std::find(offs.begin(), offs.end(), off);
  return it != offs.end() ? at + (it - offs.begin()) * stride : copy + off;
}

ctrl_t *__cdecl PortDialogNative(ctrl_t *pFile)
{
  if (!pFile)
    return NULL;
  const DialogFile f = {reinterpret_cast<const BYTE *>(pFile), PortMemSize(pFile)};
  if (!f.size)
    DialogFail("it is not in a block of the engine's");

  // dlg_load_ctrl's walk: from the root, into a dialog's children, along any other
  // control's next link; an FLC control's elements along theirs.
  std::vector<DWORD> nodes, items;
  for (DWORD off = 0;;) {
    if (!f.Has(off, kFileCtrl) || nodes.size() > f.size / kFileCtrl)
      DialogFail("a control is outside the file");
    nodes.push_back(off);
    const WORD wType = f.W(off + 0x22);
    if (wType == 0x0E) {
      for (DWORD it = f.D(off + 0x42); it; it = f.D(it)) {
        if (!f.Has(it, offsetof(dlgitem_t, pText) + 4) || items.size() > f.size / (offsetof(dlgitem_t, pText) + 4))
          DialogFail("an animation element is outside the file");
        items.push_back(it);
      }
    }
    off = f.D(off + (wType == 0 ? 0x42 : 0x00));
    if (!off)
      break;
  }

  const size_t nodeBytes = nodes.size() * sizeof(ctrl_t);
  const size_t copy = nodeBytes + items.size() * sizeof(flc_t);
  BYTE *block = static_cast<BYTE *>(SMemAlloc(static_cast<DWORD>(copy + f.size),
                                              "port_formats.cpp", __LINE__, 0));
  memset(block, 0, copy);
  memcpy(block + copy, f.p, f.size);

  for (size_t i = 0; i < nodes.size(); i++) {
    const DWORD r = nodes[i];
    ctrl_t *n = reinterpret_cast<ctrl_t *>(block + i * sizeof(ctrl_t));
    const WORD wType = f.W(r + 0x22);
    n->pNext = reinterpret_cast<ctrl_t *>(DialogLink(nodes, sizeof(ctrl_t), 0, copy, f.D(r)));
    memcpy(&n->rc, f.p + r + 0x04, 8);
    memcpy(n->_unmapped0C, f.p + r + 0x0C, 4);
    n->nResIndex = static_cast<LONG>(f.D(r + 0x10));
    n->pData = f.D(r + 0x14) ? reinterpret_cast<void *>(copy + f.D(r + 0x14)) : NULL;
    n->dwFlags = f.D(r + 0x18);
    memcpy(n->_unmapped1C, f.p + r + 0x1C, 4);
    n->nIndex = static_cast<short>(f.W(r + 0x20));
    n->wType = wType;
    memcpy(n->_unmapped24, f.p + r + 0x24, 6);
    n->pfnEvt = reinterpret_cast<PFN_CTRL_EVT>(static_cast<UINT_PTR>(f.D(r + 0x2A)));
    n->pfnDraw = reinterpret_cast<PFN_CTRL_DRAW>(static_cast<UINT_PTR>(f.D(r + 0x2E)));
    n->pParent = reinterpret_cast<ctrl_t *>(static_cast<UINT_PTR>(f.D(r + 0x32)));
    memcpy(n->_unmapped36, f.p + r + 0x36, 8);
    n->pFocus = reinterpret_cast<ctrl_t *>(static_cast<UINT_PTR>(f.D(r + 0x3E)));
    const DWORD children = f.D(r + 0x42);
    n->pChildren = reinterpret_cast<ctrl_t *>(
        wType == 0 ? DialogLink(nodes, sizeof(ctrl_t), 0, copy, children)
        : wType == 0x0E ? DialogLink(items, sizeof(flc_t), nodeBytes, copy, children)
                        : children);
    if (wType == 0x0E) {
      memcpy(&n->rcItems, f.p + r + 0x46, 8);
    } else {
      n->pMouseIn = reinterpret_cast<ctrl_t *>(static_cast<UINT_PTR>(f.D(r + 0x46)));
      n->pfnDlgProc = reinterpret_cast<void *>(static_cast<UINT_PTR>(f.D(r + 0x4A)));
    }
    memcpy(n->_unmapped4E, f.p + r + 0x4E, 4);
    const UINT_PTR tail = f.D(r + 0x52);
    memcpy(n->_unmapped4E + 4, &tail, sizeof tail);
    if (n->nResIndex < 0) {
      // dlg_load_ctrl reads the resource index at nResIndex + 0x10 from the file's start,
      // which the new block does not keep: it is read here, with the flag dlg_load_ctrl sets.
      const DWORD at = f.D(r + 0x10) + 0x10;
      n->nResIndex = f.Has(at, 4) ? static_cast<LONG>(f.D(at)) : 0;
      n->dwFlags |= 0x20;
    }
  }
  for (size_t j = 0; j < items.size(); j++) {
    const DWORD r = items[j];
    flc_t *it = reinterpret_cast<flc_t *>(block + nodeBytes + j * sizeof(flc_t));
    it->pNext = reinterpret_cast<flc_t *>(DialogLink(items, sizeof(flc_t), nodeBytes, copy, f.D(r)));
    it->dwFlags = f.D(r + 0x04);
    it->wLevel = f.W(r + 0x08);
    it->pszFile = f.D(r + 0x0A) ? reinterpret_cast<const char *>(copy + f.D(r + 0x0A)) : NULL;
    it->hVid = reinterpret_cast<void *>(static_cast<UINT_PTR>(f.D(r + 0x0E)));
    it->wXOfs = f.W(r + 0x12);
    it->wYOfs = f.W(r + 0x14);
    it->bmp.nWidth = static_cast<short>(f.W(r + 0x16));
    it->bmp.nHeight = static_cast<short>(f.W(r + 0x18));
    it->bmp.pPixels = reinterpret_cast<unsigned char *>(static_cast<UINT_PTR>(f.D(r + 0x1A)));
  }
  SMemFree(pFile, "port_formats.cpp", __LINE__, 0);
  return reinterpret_cast<ctrl_t *>(block);
}

// ---------------------------------------------------------------------------
// The saved game (docs/modules/replays.md; "Hyperion's save").
//
// PortSave117Put and the rest translate each field between the engine's layout and 1.17's
// by src117/gen/save117.h (tools/save_layouts.py): a pointer keeps its low half, an id or -1
// once the codec's PreSave has run, and comes back sign-extended. A unit id kept in 32 bits
// where 1.17 has 16 (the cargo list) keeps its low half; a Hyperion save writes the whole
// list besides (port_hxsave.cpp). A section is an id, a version, the count and 1.17 size of
// its first run of records and the length, then what 1.17 writes for that part; a reader
// skips what it does not read. The marker's second dword is where 1.17 has the player
// table's block count, 1, so the two formats cannot be confused.
#include <stdio.h>
#define SAVE117_TABLES
#include "../../gen/save117.h"
#include "CUnit.h"
#include "../../gen/globals117.h"
#include "../../h/typelimits.h"  // Hyperion: a mod's types
#include "../../h/saveload.h"  // compress, decompress, pfread, pfwrite

// The arm a union of a pointer and a point uses (stage E): CUnit's +0xF8 is a pylon's psi
// link and any other unit's rally point, its +0xD0 a units.dat 0x800 unit's (a power-up's)
// pick-up position and a pointer or the resources of any other (CUnit::SaveEncode). The
// unit's type comes before both, so a record being read has it already.
bool PortSaveUsesPoint(unsigned int layout, unsigned int off86, const void *pRec)
{
  const CUnit *pUnit = static_cast<const CUnit *>(pRec);
  if (layout == SAVE117_CUnit && off86 == 0xF8)
    return pUnit->wType != 0x9C;
  if (layout == SAVE117_CUnit && off86 == 0xD0)
    return UnitTypeValid(pUnit->wType) && (gulUnitTypeFlags[pUnit->wType] & 0x800) != 0;
  char msg[128];
  wsprintfA(msg, "Hyperion has no arm for the union at 0x%X of saved layout %u.", off86, layout);
  MessageBoxA(NULL, msg, "Hyperion", MB_OK | MB_ICONERROR);
  ExitProcess(1);
}

// CUnit's +0xC8 holds a worker's saved order target and a fighter's hangar link, the pointers
// CUnit::SaveEncode makes ids of, by the same tests; any other unit's are bytes: a building's
// tech and upgrade, a carrier's hangar counts.
bool PortSaveUsesPointer(unsigned int layout, unsigned int off86, const void *pRec)
{
  const CUnit *pUnit = static_cast<const CUnit *>(pRec);
  if (layout == SAVE117_CUnit && off86 == 0xC8)
    return (UnitTypeValid(pUnit->wType) && (gulUnitTypeFlags[pUnit->wType] & 8) != 0) || pUnit->wType == 0x49 ||
           pUnit->wType == 0x55;
  char msg[128];
  wsprintfA(msg, "Hyperion has no arm for the union at 0x%X of saved layout %u.", off86, layout);
  MessageBoxA(NULL, msg, "Hyperion", MB_OK | MB_ICONERROR);
  ExitProcess(1);
}

namespace {

void To117(unsigned int layout, const BYTE *ours, BYTE *p117)
{
  const Save117Layout &l = kSave117Layouts[layout];
  for (unsigned int i = 0; i < l.opCount; i++) {
    const Save117Op &op = kSave117Ops[l.firstOp + i];
    if (op.kind == SAVE117_COPY) {
      memcpy(p117 + op.off86, ours + op.off, op.n);
    } else if (op.kind == SAVE117_ARRAY) {
      const Save117Layout &e = kSave117Layouts[op.sub];
      for (unsigned int k = 0; k < op.n; k++)
        To117(op.sub, ours + op.off + k * e.size, p117 + op.off86 + k * e.size86);
    } else if (op.kind == SAVE117_UNSIGNED16 || op.kind == SAVE117_POS16S || op.kind == SAVE117_POS16U ||
               op.kind == SAVE117_SIGNED16 || op.kind == SAVE117_OFFSET16) {
      DWORD d;
      memcpy(&d, ours + op.off, 4);
      const WORD w = static_cast<WORD>(d);
      memcpy(p117 + op.off86, &w, 2);
    } else if (op.kind == SAVE117_UNSIGNED8) {
      // an id of h/typelimits.h's: its low byte; a mod's past it are the wide ids' section's
      // (port_hxsave.cpp)
      p117[op.off86] = ours[op.off];
    } else if (op.kind == SAVE117_PTRBYTES && !PortSaveUsesPointer(layout, op.off86, ours)) {
      To117(op.sub, ours + op.off, p117 + op.off86);  // the bytes' fields
    } else if (op.kind == SAVE117_PTRPOINT && PortSaveUsesPoint(layout, op.off86, ours)) {
      // the point: each position's low half, x then y, as 1.17's two WORDs
      memcpy(p117 + op.off86, ours + op.off, 2);
      memcpy(p117 + op.off86 + 2, ours + op.off + 4, 2);
    } else {
      UINT_PTR v;
      memcpy(&v, ours + op.off, sizeof v);
      const DWORD d = static_cast<DWORD>(v);
      memcpy(p117 + op.off86, &d, 4);
    }
  }
}

void From117(unsigned int layout, const BYTE *p117, BYTE *ours)
{
  const Save117Layout &l = kSave117Layouts[layout];
  for (unsigned int i = 0; i < l.opCount; i++) {
    const Save117Op &op = kSave117Ops[l.firstOp + i];
    if (op.kind == SAVE117_COPY) {
      memcpy(ours + op.off, p117 + op.off86, op.n);
    } else if (op.kind == SAVE117_ARRAY) {
      const Save117Layout &e = kSave117Layouts[op.sub];
      for (unsigned int k = 0; k < op.n; k++)
        From117(op.sub, p117 + op.off86 + k * e.size86, ours + op.off + k * e.size);
    } else if (op.kind == SAVE117_UNSIGNED16 || op.kind == SAVE117_OFFSET16) {
      WORD w;
      memcpy(&w, p117 + op.off86, 2);
      const DWORD d = w;
      memcpy(ours + op.off, &d, 4);
    } else if (op.kind == SAVE117_UNSIGNED8) {
      const WORD w = p117[op.off86];
      memcpy(ours + op.off, &w, 2);
    } else if (op.kind == SAVE117_POS16S || op.kind == SAVE117_POS16U || op.kind == SAVE117_SIGNED16) {
      // a map position, in the form h/pos.h stores it: sign-extended, whichever its sign;
      // or a short the x64 build keeps in an int (contour_count_t)
      short n;
      memcpy(&n, p117 + op.off86, 2);
      const int v = n;
      memcpy(ours + op.off, &v, 4);
    } else if (op.kind == SAVE117_PTRBYTES && !PortSaveUsesPointer(layout, op.off86, ours)) {
      memset(ours + op.off, 0, kSave117Layouts[op.sub].size);
      From117(op.sub, p117 + op.off86, ours + op.off);
    } else if (op.kind == SAVE117_PTRPOINT && PortSaveUsesPoint(layout, op.off86, ours)) {
      short xy[2];
      memcpy(xy, p117 + op.off86, 4);
      const int v[2] = {xy[0], xy[1]};
      memcpy(ours + op.off, v, 8);
    } else {
      DWORD d;
      memcpy(&d, p117 + op.off86, 4);
      const UINT_PTR v = op.kind == SAVE117_UNSIGNED
          ? static_cast<UINT_PTR>(d)
          : static_cast<UINT_PTR>(static_cast<INT_PTR>(static_cast<LONG>(d)));
      memcpy(ours + op.off, &v, sizeof v);
    }
  }
}

// A record's SAVE117_UNSIGNED8 fields' high bytes and its SAVE117_OFFSET16 fields' high halves
// (an image's script offsets), in the layout's order, into *ppOut or from
// *ppIn, each cursor moved past them.
void HighBytes(unsigned int layout, BYTE *ours, BYTE **ppOut, const BYTE **ppIn)
{
  const Save117Layout &l = kSave117Layouts[layout];
  for (unsigned int i = 0; i < l.opCount; i++) {
    const Save117Op &op = kSave117Ops[l.firstOp + i];
    if (op.kind == SAVE117_ARRAY && PortSave117HighCount(op.sub) != 0) {
      const Save117Layout &e = kSave117Layouts[op.sub];
      for (unsigned int k = 0; k < op.n; k++)
        HighBytes(op.sub, ours + op.off + k * e.size, ppOut, ppIn);
    } else if (op.kind == SAVE117_PTRBYTES && !PortSaveUsesPointer(layout, op.off86, ours)) {
      HighBytes(op.sub, ours + op.off, ppOut, ppIn);
    } else if (op.kind == SAVE117_PTRBYTES) {
      // the pointer's arm: none, a place each kept
      const unsigned int n = PortSave117HighCount(op.sub);
      if (ppOut != NULL)
        *ppOut = static_cast<BYTE *>(memset(*ppOut, 0, n)) + n;
      else
        *ppIn += n;
    } else if (op.kind == SAVE117_UNSIGNED8) {
      if (ppOut != NULL)
        *(*ppOut)++ = ours[op.off + 1];
      else
        ours[op.off + 1] = *(*ppIn)++;
    } else if (op.kind == SAVE117_OFFSET16) {
      if (ppOut != NULL) {
        memcpy(*ppOut, ours + op.off + 2, 2);
        *ppOut += 2;
      } else {
        memcpy(ours + op.off + 2, *ppIn, 2);
        *ppIn += 2;
      }
    }
  }
}

// The section being written or read.
struct SaveSection {
  DWORD dwId;
  WORD wVersion;
  WORD wFlags;
  DWORD dwCount;        // records in the section's first run
  DWORD dwRecordSize;   // their size in 1.17
  DWORD dwBytes;        // what follows the header
};
static_assert(sizeof(SaveSection) == 20, "the section header is 20 bytes on both widths");

const char kMarker[8] = {'H', 'Y', 'P', 'E', 'R', 'I', 'O', 'N'};
// 2: the Hyperion sections of a game past 1.17's limits (port_hxsave.cpp)
const DWORD kFormatVersion = 2;

SaveSection s_section;
LONG s_sectionStart = -1;   // the header's place in the file, -1 for none open
LONG s_sectionEnd;          // on load, where the section ends
bool s_hyperion;            // on load, whether the save is Hyperion's

void IdsTo117(const void *pSlots, unsigned int count, std::vector<DWORD> &out)
{
  out.resize(count);
  for (unsigned int i = 0; i < count; i++)
    out[i] = static_cast<DWORD>(static_cast<const UINT_PTR *>(pSlots)[i]);
}

void IdsFrom117(const std::vector<DWORD> &in, void *pSlots)
{
  for (size_t i = 0; i < in.size(); i++)
    static_cast<UINT_PTR *>(pSlots)[i] = static_cast<UINT_PTR>(static_cast<INT_PTR>(static_cast<LONG>(in[i])));
}

}  // namespace

unsigned int __cdecl PortSave117Size(unsigned int layout)
{
  return kSave117Layouts[layout].size86;
}

unsigned int PortSave117SizeHere(unsigned int layout)
{
  return kSave117Layouts[layout].size;
}

int PortSave117Offset86(unsigned int layout, unsigned int off)
{
  const Save117Layout &l = kSave117Layouts[layout];
  for (unsigned int i = l.firstOp; i < l.firstOp + l.opCount; i++) {
    const Save117Op &op = kSave117Ops[i];
    if (op.kind == SAVE117_ARRAY) {
      const Save117Layout &e = kSave117Layouts[op.sub];
      if (off >= op.off && off < op.off + op.n * e.size) {
        const int nIn = PortSave117Offset86(op.sub, (off - op.off) % e.size);
        return nIn < 0 ? -1 : static_cast<int>(op.off86 + (off - op.off) / e.size * e.size86) + nIn;
      }
    } else if (off == op.off) {
      return static_cast<int>(op.off86);
    } else if (op.kind == SAVE117_COPY && off > op.off && off < op.off + op.n) {
      return static_cast<int>(op.off86 + (off - op.off));
    }
  }
  return -1;
}

unsigned char *__cdecl PortSave117Put(unsigned int layout, const void *pRec, unsigned char *pOut)
{
  To117(layout, static_cast<const BYTE *>(pRec), pOut);
  return pOut + kSave117Layouts[layout].size86;
}

const unsigned char *__cdecl PortSave117Get(unsigned int layout, const unsigned char *pIn, void *pRec)
{
  From117(layout, pIn, static_cast<BYTE *>(pRec));
  return pIn + kSave117Layouts[layout].size86;
}

int __cdecl PortSaveChunk(const void *pBuf, int nBytes, unsigned int count, void *pFile)
{
  if (s_sectionStart >= 0 && s_section.dwRecordSize == 0 && count) {
    s_section.dwCount = count;
    s_section.dwRecordSize = static_cast<DWORD>(nBytes) / count;
  }
  return compress(pBuf, nBytes, pFile);
}

int __cdecl PortSave117Compress(unsigned int layout, const void *pRecs, unsigned int count, void *pFile)
{
  const Save117Layout &l = kSave117Layouts[layout];
  std::vector<BYTE> buf(static_cast<size_t>(count) * l.size86);
  for (unsigned int i = 0; i < count; i++)
    To117(layout, static_cast<const BYTE *>(pRecs) + static_cast<size_t>(i) * l.size,
          &buf[static_cast<size_t>(i) * l.size86]);
  return PortSaveChunk(buf.data(), static_cast<int>(buf.size()), count, pFile);
}

int __cdecl PortSave117Decompress(unsigned int layout, void *pRecs, unsigned int count, void *pFile)
{
  const Save117Layout &l = kSave117Layouts[layout];
  std::vector<BYTE> buf(static_cast<size_t>(count) * l.size86);
  if (!decompress(buf.data(), static_cast<int>(buf.size()), pFile))
    return 0;
  for (unsigned int i = 0; i < count; i++)
    From117(layout, &buf[static_cast<size_t>(i) * l.size86],
            static_cast<BYTE *>(pRecs) + static_cast<size_t>(i) * l.size);
  return 1;
}

unsigned int PortSave117HighCount(unsigned int layout)
{
  const Save117Layout &l = kSave117Layouts[layout];
  unsigned int n = 0;
  for (unsigned int i = 0; i < l.opCount; i++) {
    const Save117Op &op = kSave117Ops[l.firstOp + i];
    if (op.kind == SAVE117_ARRAY)
      n += op.n * PortSave117HighCount(op.sub);
    else if (op.kind == SAVE117_PTRBYTES)
      n += PortSave117HighCount(op.sub);
    else if (op.kind == SAVE117_UNSIGNED8)
      n++;
    else if (op.kind == SAVE117_OFFSET16)
      n += 2;
  }
  return n;
}

void PortSave117HighGet(unsigned int layout, const void *pRecs, unsigned int count, unsigned char *pOut)
{
  const Save117Layout &l = kSave117Layouts[layout];
  if (PortSave117HighCount(layout) == 0)
    return;
  for (unsigned int i = 0; i < count; i++)
    HighBytes(layout, const_cast<BYTE *>(static_cast<const BYTE *>(pRecs)) + static_cast<size_t>(i) * l.size,
              &pOut, NULL);
}

void PortSave117HighSet(unsigned int layout, void *pRecs, unsigned int count, const unsigned char *pIn)
{
  const Save117Layout &l = kSave117Layouts[layout];
  if (PortSave117HighCount(layout) == 0)
    return;
  for (unsigned int i = 0; i < count; i++)
    HighBytes(layout, static_cast<BYTE *>(pRecs) + static_cast<size_t>(i) * l.size, NULL, &pIn);
}

int __cdecl PortSaveWriteIds(void *pFile, const void *pSlots, unsigned int count)
{
  std::vector<DWORD> ids;
  IdsTo117(pSlots, count, ids);
  return pfwrite(ids.data(), static_cast<int>(count * 4), pFile);
}

int __cdecl PortSaveReadIds(void *pFile, void *pSlots, unsigned int count)
{
  std::vector<DWORD> ids(count);
  if (!pfread(ids.data(), static_cast<int>(count * 4), pFile))
    return 0;
  IdsFrom117(ids, pSlots);
  return 1;
}

int __cdecl PortSaveCompressIds(const void *pSlots, unsigned int count, void *pFile)
{
  std::vector<DWORD> ids;
  IdsTo117(pSlots, count, ids);
  return compress(ids.data(), static_cast<int>(count * 4), pFile);
}

int __cdecl PortSaveDecompressIds(void *pSlots, unsigned int count, void *pFile)
{
  std::vector<DWORD> ids(count);
  if (!decompress(ids.data(), static_cast<int>(count * 4), pFile))
    return 0;
  IdsFrom117(ids, pSlots);
  return 1;
}

int __cdecl PortSaveBegin(void *pFile)
{
  s_sectionStart = -1;
  if (!pFile)
    return 0;
  return pfwrite(kMarker, sizeof kMarker, pFile) && pfwrite(&kFormatVersion, 4, pFile);
}

int __cdecl PortSaveEnd(void *pFile)
{
  if (s_sectionStart < 0)
    return 1;
  FILE *f = static_cast<FILE *>(pFile);
  const long end = ftell(f);
  const LONG start = s_sectionStart;
  s_sectionStart = -1;
  if (end < 0)
    return 0;
  s_section.dwBytes = static_cast<DWORD>(end - start - static_cast<LONG>(sizeof s_section));
  return fseek(f, start, SEEK_SET) == 0 && fwrite(&s_section, sizeof s_section, 1, f) == 1
      && fseek(f, end, SEEK_SET) == 0;
}

int __cdecl PortSaveSection(void *pFile, ULONG id)
{
  if (!pFile || !PortSaveEnd(pFile))
    return 0;
  memset(&s_section, 0, sizeof s_section);
  s_section.dwId = id;
  s_section.wVersion = 1;
  s_sectionStart = ftell(static_cast<FILE *>(pFile));
  return s_sectionStart >= 0 && pfwrite(&s_section, sizeof s_section, pFile);
}

int __cdecl PortLoadBegin(void *pFile)
{
  FILE *f = static_cast<FILE *>(pFile);
  char marker[sizeof kMarker];
  DWORD dwVersion;
  s_hyperion = false;
  s_sectionStart = -1;
  if (fread(marker, sizeof marker, 1, f) == 1 && memcmp(marker, kMarker, sizeof marker) == 0) {
    if (fread(&dwVersion, 4, 1, f) != 1 || dwVersion > kFormatVersion)
      return 0;
    s_hyperion = true;
    return 1;
  }
  return fseek(f, -static_cast<long>(sizeof marker), SEEK_CUR) == 0;
}

int __cdecl PortLoadEnd(void *pFile)
{
  if (!s_hyperion || s_sectionStart < 0)
    return 1;
  s_sectionStart = -1;
  FILE *f = static_cast<FILE *>(pFile);
  const long at = ftell(f);
  if (at < 0 || at > s_sectionEnd)
    return 0;
  return at == s_sectionEnd || fseek(f, s_sectionEnd, SEEK_SET) == 0;
}

int __cdecl PortLoadSectionIf(void *pFile, ULONG id)
{
  if (!s_hyperion)
    return 0;
  if (!PortLoadEnd(pFile))
    return -1;
  FILE *f = static_cast<FILE *>(pFile);
  const long at = ftell(f);
  SaveSection next;
  if (at < 0 || fread(&next, sizeof next, 1, f) != 1 || next.dwId != id)
    return at >= 0 && fseek(f, at, SEEK_SET) == 0 ? 0 : -1;
  return fseek(f, at, SEEK_SET) == 0 && PortLoadSection(pFile, id) ? 1 : -1;
}

int __cdecl PortLoadSectionBytes(void)
{
  return s_hyperion && s_sectionStart >= 0 ? static_cast<int>(s_section.dwBytes) : 0;
}

int __cdecl PortLoadSection(void *pFile, ULONG id)
{
  if (!s_hyperion)
    return 1;
  if (!PortLoadEnd(pFile))
    return 0;
  FILE *f = static_cast<FILE *>(pFile);
  if (fread(&s_section, sizeof s_section, 1, f) != 1 || s_section.dwId != id)
    return 0;
  s_sectionStart = ftell(f) - static_cast<long>(sizeof s_section);
  s_sectionEnd = s_sectionStart + static_cast<LONG>(sizeof s_section) + static_cast<LONG>(s_section.dwBytes);
  return 1;
}
