// port_standalone.cpp -- 1.17's data as the engine's own, with none of 1.17's code in the
// process (docs/modules/platform.md; "1.17's data").
//
// The engine keeps 1.17's .rdata and .data as one block, g_data117, in 1.17's layout
// (src117/h/oc_data.h), every global it names at its place there (tools/wave117/abs_globals.py).
// PortGameDataInit fills it as Hyperion.exe starts from 1.17's starting values as Hyperion's
// own source (src117/gen/data117.cpp): each piece at its place, the rest zero; each pointer among
// them made the engine's global or function (src117/gen/code_map.cpp), or
// PortOriginalCodeReached where the engine has none; and each global with a pointer in it
// translated field by field into its own storage in g_wide117 (src117/gen/wide117.h), the
// block's copy cleared so a read past the storage finds zeros. A file read into such a global
// later, a DAT column of string ids, takes the same translation (PortDataStore). PortStartUp
// does what the original's start-up did for the game.
#include <windows.h>
#include <intrin.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include "oc_data.h"
#include "port_standalone.h"
#include "../replays/port_snap.h"  // PORT_SNAP_KEEP
#include "../../gen/wide117.h"
#include "main.h"  // WinMain

#pragma intrinsic(_ReturnAddress)

extern "C" int __isa_available;  // this runtime's own processor level

static const DWORD ORIGINAL_CODE_REACHED = 0xE0520002;

// The static constructors of 1.17's start-up, read from its code at 0x4021A0..0x4025C0.
// Each list head's second word is the inverted address of the head, which marks the
// empty list's end (TSList).
static const DWORD kListHeads[] = {
    0x503034,  // gAreaFixups
    0x5032A4,
    0x5032B4,  // gFDSessions
    0x5032C0,  // gFDUsers
    0x5033C8,  // sgTemplateList
    0x5033E4,  // sgDrawTextList
    0x503D90,  // sgProviderList
    0x508118,  // gGameNodeList
    0x508170,  // gSaveGameList
    0x5081A4,  // gIDList
    0x508204,  // gMapList
    0x508210,  // gModemDeviceList
    0x50821C,  // gPhonebook
    0x5082FC,  // gBriefTriggers
    0x508D30,  // gBanList
    0x508D3C,  // gMessageList
    0x508D60,  // gGameHeaders
    0x50936C,  // gSaveGameNodes
    0x509378,  // gSaveGames
    0x5093CC,  // gStreams
    0x50E0DC, 0x50E0E8, 0x50E0F4, 0x50E100,  // sgSlotTriggerNodes, eight lists
    0x50E10C, 0x50E118, 0x50E124, 0x50E130,
    0x50EF3C,  // gDlgGroups
    0x50EF68,  // gControlNodes
    0x50F0D8,  // gBlitMasks
};
static const DWORD kCriticalSections[] = {
    0x6662C8,  // gSFileCrit_g
    0x6AA970,  // gSoundCrit
    0x6D168C,  // sgLogCrit_l
};
static const DWORD kMainThread = 0x6D44F4;      // s_mainThread
static const DWORD kLeagueEvent = 0x672968;     // ghLeagueEvt_g
static const DWORD kLeagueEventName = 0x4EF240; // its name, in .rdata
static const DWORD kIsaAvailable = 0x6D3198;    // __isa_available, the original runtime's

// The original's two big numbers for the map signature (static_ctor_sigNum1 and 2)
// are left out: signature.cpp keeps its own since step 4.

// The global at 1.17's address va, in the block.
template <class T> static T *Block(DWORD va)
{
  return reinterpret_cast<T *>(OC_VA(va));
}

static void Fail(const char *what);

extern "C" unsigned char g_wide117[];  // abs_globals.py's second section

static const size_t kWideCount = sizeof kWideGlobals / sizeof kWideGlobals[0];

// The row of the wide global that holds 1.17's address va, or NULL.
static const WideGlobal *WideRow(DWORD va)
{
  size_t lo = 0, hi = kWideCount;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (kWideGlobals[mid].va <= va)
      lo = mid + 1;
    else
      hi = mid;
  }
  if (lo == 0)
    return NULL;
  const WideGlobal &g = kWideGlobals[lo - 1];
  return va - g.va < g.size86 ? &g : NULL;
}

// Where the byte at off86 of 1.17's element falls in the engine's: the field that starts
// there, or the byte in a copied run; -1 for padding. The first field in the table's
// order that has the byte answers.
static LONG WideFieldScan(const WideGlobal &g, DWORD off86)
{
  for (unsigned i = g.firstOp; i < g.firstOp + g.opCount; i++) {
    const WideOp &op = kWideOps[i];
    if (off86 == op.off86)
      return static_cast<LONG>(op.off);
    if (op.kind == WIDE_COPY && off86 - op.off86 < op.len)
      return static_cast<LONG>(op.off + (off86 - op.off86));
  }
  return -1;
}

// The reverse: the byte of 1.17's element at off in the engine's, or -1.
static LONG WideField86Scan(const WideGlobal &g, DWORD off)
{
  for (unsigned i = g.firstOp; i < g.firstOp + g.opCount; i++) {
    const WideOp &op = kWideOps[i];
    if (off == op.off)
      return static_cast<LONG>(op.off86);
    if (op.kind == WIDE_COPY && off - op.off < op.len)
      return static_cast<LONG>(op.off86 + (off - op.off));
  }
  return -1;
}

// The same answers sooner: the state check names every pointer into the AI's pools by its 1.17
// address, and those are globals of 1,000 to 11,000 fields. Most globals have their fields in
// offset order in both layouts, no copied run reaching the next field, and there a field is
// found by halves; the few whose unions lay runs over runs (the AI's workers and scripts among
// them) keep, past a few fields, a table of each byte's answer by the scans' rule; the rest are
// scanned.
struct WideFast {
  std::vector<char> ordered;            // by the global's row
  std::vector<std::vector<LONG> > by86;  // a byte of 1.17's element: the engine's, or -1
  std::vector<std::vector<LONG> > byOff; // a byte of the engine's: 1.17's, or -1
};

static const WideFast &Fast(void)
{
  static const WideFast *s_pFast = [] {
    WideFast *f = new WideFast;
    f->ordered.assign(kWideCount, 0);
    f->by86.resize(kWideCount);
    f->byOff.resize(kWideCount);
    for (size_t k = 0; k < kWideCount; k++) {
      const WideGlobal &g = kWideGlobals[k];
      bool bOrdered = true;
      for (unsigned i = g.firstOp + 1; i < g.firstOp + g.opCount && bOrdered; i++) {
        const WideOp &p = kWideOps[i - 1], &q = kWideOps[i];
        bOrdered = q.off86 > p.off86 && q.off > p.off &&
                   !(p.kind == WIDE_COPY && (p.off86 + p.len > q.off86 || p.off + p.len > q.off));
      }
      f->ordered[k] = bOrdered;
      if (bOrdered || g.opCount <= 64 || g.stride86 > (1u << 18) || g.stride > (1u << 18))
        continue;
      std::vector<LONG> &t86 = f->by86[k], &tOff = f->byOff[k];
      t86.assign(g.stride86, -1);
      tOff.assign(g.stride, -1);
      std::vector<char> set86(g.stride86, 0), setOff(g.stride, 0);
      for (unsigned i = g.firstOp; i < g.firstOp + g.opCount; i++) {
        const WideOp &op = kWideOps[i];
        // the bytes the scans match this field on: its first, and a run's every one
        const unsigned n = op.kind == WIDE_COPY && op.len > 1 ? op.len : 1;
        for (unsigned d = 0; d < n; d++) {
          if (op.off86 + d < g.stride86 && !set86[op.off86 + d]) {
            set86[op.off86 + d] = 1;
            t86[op.off86 + d] = static_cast<LONG>(op.off + d);
          }
          if (op.off + d < g.stride && !setOff[op.off + d]) {
            setOff[op.off + d] = 1;
            tOff[op.off + d] = static_cast<LONG>(op.off86 + d);
          }
        }
      }
    }
    return f;
  }();
  return *s_pFast;
}

// In an ordered global, the field holding byte at of 1.17's element (b86) or the engine's.
static LONG WideFind(const WideGlobal &g, DWORD at, bool b86)
{
  unsigned lo = g.firstOp, hi = g.firstOp + g.opCount;
  while (lo < hi) {
    const unsigned mid = (lo + hi) / 2;
    if ((b86 ? kWideOps[mid].off86 : kWideOps[mid].off) <= at)
      lo = mid + 1;
    else
      hi = mid;
  }
  if (lo == g.firstOp)
    return -1;
  const WideOp &op = kWideOps[lo - 1];
  const DWORD from = b86 ? op.off86 : op.off, to = b86 ? op.off : op.off86;
  if (at == from)
    return static_cast<LONG>(to);
  if (op.kind == WIDE_COPY && at - from < op.len)
    return static_cast<LONG>(to + (at - from));
  return -1;
}

static LONG WideField(const WideGlobal &g, DWORD off86)
{
  const WideFast &f = Fast();
  const size_t k = static_cast<size_t>(&g - kWideGlobals);
  if (f.ordered[k])
    return WideFind(g, off86, true);
  if (!f.by86[k].empty())
    return off86 < f.by86[k].size() ? f.by86[k][off86] : WideFieldScan(g, off86);
  return WideFieldScan(g, off86);
}

static LONG WideField86(const WideGlobal &g, DWORD off)
{
  const WideFast &f = Fast();
  const size_t k = static_cast<size_t>(&g - kWideGlobals);
  if (f.ordered[k])
    return WideFind(g, off, false);
  if (!f.byOff[k].empty())
    return off < f.byOff[k].size() ? f.byOff[k][off] : WideField86Scan(g, off);
  return WideField86Scan(g, off);
}

// The row of the wide global whose storage holds p, or NULL.
static const WideGlobal *WideRowAt(UINT_PTR p)
{
  const UINT_PTR wide = reinterpret_cast<UINT_PTR>(g_wide117);
  if (p - wide >= OC_WIDE_SIZE)
    return NULL;
  const DWORD o = static_cast<DWORD>(p - wide);
  size_t lo = 0, hi = kWideCount;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (kWideGlobals[mid].off <= o)
      lo = mid + 1;
    else
      hi = mid;
  }
  if (lo == 0)
    return NULL;
  const WideGlobal &g = kWideGlobals[lo - 1];
  return o - g.off < g.size ? &g : NULL;
}

// One element of g in 1.17's layout, from, into the engine's, to. A pointer field takes the
// value slots gives its 1.17 address va, when slots is given and has it, and is
// otherwise widened as the integer it holds: sign-extended from the exe's data, where
// such a value is a sentinel, and zero-extended from a file's, where it is an id.
static void WideElement(const WideGlobal &g, const BYTE *from, BYTE *to,
                        const std::vector<std::pair<DWORD, UINT_PTR> > *slots, DWORD va,
                        bool pointerSigned);

UINT_PTR __cdecl PortDataAddress(DWORD va)
{
  if (const WideGlobal *g = WideRow(va)) {
    DWORD off = va - g->va;
    LONG f = WideField(*g, off % g->stride86);
    if (f < 0)
      Fail("an address inside a global that no field of it covers");
    return reinterpret_cast<UINT_PTR>(g_wide117) + g->off + off / g->stride86 * g->stride + f;
  }
  return OC_VA(va);
}

DWORD __cdecl PortOriginalAddress(UINT_PTR p)
{
  const UINT_PTR wide = reinterpret_cast<UINT_PTR>(g_wide117);
  if (p - wide < OC_WIDE_SIZE) {
    const WideGlobal *g = WideRowAt(p);
    if (!g)
      return 0;
    const DWORD o = static_cast<DWORD>(p - wide) - g->off;
    LONG f = WideField86(*g, o % g->stride);
    return f < 0 ? 0 : g->va + o / g->stride * g->stride86 + static_cast<DWORD>(f);
  }
  const UINT_PTR block = OC_VA(OC_DATA_VA);
  if (p - block < OC_DATA_END - OC_DATA_VA)
    return OC_DATA_VA + static_cast<DWORD>(p - block);
  return 0;
}

bool __cdecl PortDataField(DWORD va, PortField *pf)
{
  const WideGlobal *g = WideRow(va);
  if (!g) {
    pf->va = va;
    pf->p = OC_VA(va);
    pf->kind = WIDE_COPY;
    pf->len = 1;
    return true;
  }
  const DWORD off = va - g->va;
  const DWORD in = off % g->stride86;
  const UINT_PTR elem = reinterpret_cast<UINT_PTR>(g_wide117) + g->off + off / g->stride86 * g->stride;
  for (unsigned i = g->firstOp; i < g->firstOp + g->opCount; i++) {
    const WideOp &op = kWideOps[i];
    if (in - op.off86 >= op.len)
      continue;
    pf->va = va - (in - op.off86);
    pf->p = elem + op.off;
    pf->kind = op.kind;
    pf->len = op.len;
    return true;
  }
  return false;
}

bool __cdecl PortDataPlain(DWORD va, DWORD size)
{
  for (size_t k = 0; k < kWideCount; k++)
    if (kWideGlobals[k].va < va + size && va < kWideGlobals[k].va + kWideGlobals[k].size86)
      return false;
  return true;
}

// The two spans PortOriginalAddress names an address in: the grown globals' storage and 1.17's
// data block, each as its first address and one past its last.
void __cdecl PortOriginalSpans(UINT_PTR aSpans[2][2])
{
  aSpans[0][0] = reinterpret_cast<UINT_PTR>(g_wide117);
  aSpans[0][1] = aSpans[0][0] + OC_WIDE_SIZE;
  aSpans[1][0] = OC_VA(OC_DATA_VA);
  aSpans[1][1] = aSpans[1][0] + (OC_DATA_END - OC_DATA_VA);
}

bool __cdecl PortDataGrown(const void *p)
{
  return WideRowAt(reinterpret_cast<UINT_PTR>(p)) != NULL;
}

void *__cdecl PortDataRow(void *p, DWORD rowBytes, DWORD row)
{
  // row is unsigned, so a row below a column's first (a mod's unit below those units.dat's
  // add-on positions start at) arrives huge, and an offset past 32 bits names no row of anything
  if (rowBytes != 0 && row > (0xFFFFFFFFu - rowBytes) / rowBytes)
    return NULL;
  const DWORD at = row * rowBytes;
  if (const WideGlobal *g = WideRowAt(reinterpret_cast<UINT_PTR>(p))) {
    const DWORD o = static_cast<DWORD>(static_cast<BYTE *>(p) - g_wide117) - g->off;
    const DWORD first = o / g->stride;
    // the storage's elements, which for a grown column are past the count the file's
    // translation fills (tools/wide_globals.py, GROWN)
    if (o % g->stride || rowBytes % g->stride86 || at % g->stride86 ||
        first + (at + rowBytes) / g->stride86 > g->size / g->stride)
      return NULL;
    return g_wide117 + g->off + (first + at / g->stride86) * g->stride;
  }
  // a column of 1.17's own data, where every row of it is inside the block
  const UINT_PTR pRow = reinterpret_cast<UINT_PTR>(static_cast<BYTE *>(p) + at);
  const UINT_PTR block = OC_VA(OC_DATA_VA);
  const DWORD cbBlock = OC_DATA_END - OC_DATA_VA;
  if (pRow < block || rowBytes > cbBlock || pRow - block > cbBlock - rowBytes)
    return NULL;
  return reinterpret_cast<void *>(pRow);
}

DWORD __cdecl PortDataBytes(const void *p, DWORD bytes86)
{
  const WideGlobal *g = WideRowAt(reinterpret_cast<UINT_PTR>(p));
  return g ? bytes86 / g->stride86 * g->stride : bytes86;
}

void __cdecl PortDataStore(void *p, const void *from, DWORD bytes)
{
  if (const WideGlobal *g = WideRowAt(reinterpret_cast<UINT_PTR>(p))) {
    const DWORD o = static_cast<DWORD>(static_cast<BYTE *>(p) - g_wide117) - g->off;
    const DWORD first = o / g->stride;
    if (o % g->stride || bytes % g->stride86 || first + bytes / g->stride86 > g->size / g->stride)
      Fail("a file's data that is not whole elements of the global it is stored in");
    for (DWORD e = 0; e < bytes / g->stride86; e++)
      WideElement(*g, static_cast<const BYTE *>(from) + e * g->stride86,
                  static_cast<BYTE *>(p) + e * g->stride, NULL, 0, false);
    return;
  }
  memcpy(p, from, bytes);
}

// 1.17's C runtime rand and srand, as the game's own OcRand and OcSrand (h/platform.h): the
// state times 214013 plus 2531011, bits 16 to 30. The runtime keeps the state per thread; here
// it is one global, which a save of a Hyperion game keeps (port_hxsave.cpp), since the creep's
// edge tiles and the sounds' variations come from it. Its callers are the game's own code, on
// the game's thread; named apart from rand, no library's call to rand reaches the game's state.
static unsigned int g_crtRandState = 1;
PORT_SNAP_KEEP(g_crtRandState);  // the trainer's snapshots hold it (port_snap.cpp)

extern "C" void __cdecl OcSrand(unsigned int nSeed)
{
    g_crtRandState = nSeed;
}

extern "C" int __cdecl OcRand(void)
{
    g_crtRandState = g_crtRandState * 214013u + 2531011u;
    return static_cast<int>((g_crtRandState >> 16) & 0x7FFF);
}

unsigned int *__cdecl PortCrtRandState(void)
{
    return &g_crtRandState;
}

extern "C" void __cdecl PortOriginalCodeReached(void)
{
  ULONG_PTR caller = reinterpret_cast<ULONG_PTR>(_ReturnAddress());
  RaiseException(ORIGINAL_CODE_REACHED, EXCEPTION_NONCONTINUABLE, 1, &caller);
}

static void Fail(const char *what)
{
  // wsprintfA writes up to 1024 characters whatever the buffer is
  char msg[1024];
  wsprintfA(msg, "Hyperion stopped: %s.", what);
  MessageBoxA(NULL, msg, "Hyperion", MB_OK | MB_ICONERROR);
  ExitProcess(1);
}

// The engine's function for each address of 1.17's code that its data holds
// (src117/gen/code_map.cpp), sorted by address.
typedef std::vector<std::pair<DWORD, UINT_PTR> > FunctionMap;

static UINT_PTR Lookup(const FunctionMap &map, DWORD va)
{
  FunctionMap::const_iterator it = std::lower_bound(
      map.begin(), map.end(), std::make_pair(va, static_cast<UINT_PTR>(0)),
      [](const std::pair<DWORD, UINT_PTR> &a, const std::pair<DWORD, UINT_PTR> &b) {
        return a.first < b.first;
      });
  return it != map.end() && it->first == va ? it->second : 0;
}

// A pointer slot of the data, with what it becomes.
typedef std::vector<std::pair<DWORD, UINT_PTR> > SlotMap;

static bool FindSlot(const SlotMap &slots, DWORD va, UINT_PTR *pValue)
{
  SlotMap::const_iterator it = std::lower_bound(
      slots.begin(), slots.end(), std::make_pair(va, static_cast<UINT_PTR>(0)),
      [](const std::pair<DWORD, UINT_PTR> &a, const std::pair<DWORD, UINT_PTR> &b) {
        return a.first < b.first;
      });
  if (it == slots.end() || it->first != va)
    return false;
  *pValue = it->second;
  return true;
}

static void WideElement(const WideGlobal &g, const BYTE *from, BYTE *to, const SlotMap *slots,
                        DWORD va, bool pointerSigned)
{
  for (unsigned i = g.firstOp; i < g.firstOp + g.opCount; i++) {
    const WideOp &op = kWideOps[i];
    // the four bytes the pointer and widening ops read; a copied run or a 16-bit field can
    // end the element, where reading four would read past what the caller gave
    const bool bWord32 = op.kind == WIDE_POINTER || op.kind == WIDE_SIGNED || op.kind == WIDE_UNSIGNED;
    const DWORD v = bWord32 ? *reinterpret_cast<const DWORD *>(from + op.off86) : 0;
    UINT_PTR value;
    switch (op.kind) {
    case WIDE_COPY:
      memcpy(to + op.off, from + op.off86, op.len);
      break;
    case WIDE_POINTER:
      if (!slots || !FindSlot(*slots, va + op.off86, &value))
        value = pointerSigned ? static_cast<UINT_PTR>(static_cast<INT_PTR>(static_cast<LONG>(v)))
                              : static_cast<UINT_PTR>(v);
      *reinterpret_cast<UINT_PTR *>(to + op.off) = value;
      break;
    case WIDE_SIGNED:
      *reinterpret_cast<INT_PTR *>(to + op.off) = static_cast<LONG>(v);
      break;
    case WIDE_UNSIGNED:
      *reinterpret_cast<UINT_PTR *>(to + op.off) = v;
      break;
    case WIDE_UNSIGNED16:  // a unit id, or an unsigned map position, kept in 32 bits
      *reinterpret_cast<DWORD *>(to + op.off) = *reinterpret_cast<const WORD *>(from + op.off86);
      break;
    case WIDE_SIGNED16:  // a signed map position kept in 32 bits (h/pos.h, stage E)
      *reinterpret_cast<int *>(to + op.off) = *reinterpret_cast<const short *>(from + op.off86);
      break;
    case WIDE_UNSIGNED8:  // an id of h/typelimits.h's kept in 16 bits
      *reinterpret_cast<WORD *>(to + op.off) = from[op.off86];
      break;
    }
  }
}

// Step 3: each wide global from the block into its storage, then the block's copy
// cleared.
static void TranslateWide(const SlotMap &slots)
{
  for (size_t k = 0; k < kWideCount; k++) {
    const WideGlobal &g = kWideGlobals[k];
    for (unsigned e = 0; e < g.count; e++) {
      const DWORD va = g.va + e * g.stride86;
      WideElement(g, Block<BYTE>(va), g_wide117 + g.off + e * g.stride, &slots, va, true);
    }
    memset(Block<BYTE>(g.va), 0, g.size86);
  }
}

// The places of the block that 1.17's file held and data117.cpp leaves out (the original's C
// runtime, its imports, its crash reporter, PKWARE's tables), filled with 0xA5 for a check that
// nothing of Hyperion's reads them (HYPERION_DATA_POISON=1); the block left as it is
// otherwise, zero there.
static void Poison(void)
{
  char sz[8];
  DWORD n = GetEnvironmentVariableA("HYPERION_DATA_POISON", sz, sizeof sz);
  if (n == 0 || n >= sizeof sz || sz[0] == '0')
    return;
  ULONG nFile = 0, nKept = 0;
  const PortDataSpan *file = PortData117File(&nFile);
  const PortDataSpan *kept = PortData117Kept(&nKept);
  for (ULONG i = 0; i < nFile; i++)
    for (DWORD va = file[i].va; va < file[i].end; va++) {
      bool bKept = false;
      for (ULONG k = 0; k < nKept && !bKept; k++)
        bKept = kept[k].va <= va && va < kept[k].end;
      if (!bKept)
        *Block<BYTE>(va) = 0xA5;
    }
}

bool __cdecl PortGameDataInit(DWORD *pOurs, DWORD *pTrapped)
{
  *pOurs = *pTrapped = 0;

  // 1. The data, at its places in the block, which starts zeroed.
  ULONG n = 0;
  const PortDataPiece *pieces = PortData117(&n);
  for (ULONG i = 0; i < n; i++)
    memcpy(Block<BYTE>(pieces[i].va), pieces[i].bytes, pieces[i].len);
  Poison();

  // 2. The pointers: an address in 1.17's data is the engine's global, one in its code the
  // engine's function of that address.
  ULONG nRows = 0;
  const PortCodeRow *rows = PortCodeMap(&nRows);
  FunctionMap map;
  for (ULONG i = 0; i < nRows; i++)
    map.push_back(std::make_pair(static_cast<DWORD>(rows[i].va), reinterpret_cast<UINT_PTR>(rows[i].fn)));
  std::sort(map.begin(), map.end());
  const UINT_PTR trap = reinterpret_cast<UINT_PTR>(&PortOriginalCodeReached);
  const PortDataSpan code = PortData117Code();
  const ULONG *pointers = PortData117Pointers(&n);
  DWORD ours = 0, trapped = 0;
  SlotMap slots;
  for (ULONG i = 0; i < n; i++) {
    const DWORD slot = pointers[i];
    const DWORD v = *Block<DWORD>(slot);
    if (v >= OC_DATA_VA && v < OC_DATA_END) {
      slots.push_back(std::make_pair(slot, PortDataAddress(v)));
      continue;
    }
    if (v < code.va || v >= code.end)
      continue;
    const UINT_PTR fn = Lookup(map, v);
    if (fn) {
      slots.push_back(std::make_pair(slot, fn));
      ours++;
    } else {
      slots.push_back(std::make_pair(slot, trap));
      trapped++;
    }
  }
  // The block keeps 1.17's pointers: nothing reads one there (step 3 gives every global that
  // holds one storage of its own).
  std::sort(slots.begin(), slots.end());

  // 3. The globals that hold pointers, into storage of their own.
  TranslateWide(slots);

  *pOurs = ours;
  *pTrapped = trapped;
  return true;
}

// For a check: the block as the game starts, written to HYPERION_DUMP_DATA (step 6
// compared the standalone mode's with the hosted mode's this way).
static void DataCheckpoint(void)
{
  char path[MAX_PATH];
  DWORD n = GetEnvironmentVariableA("HYPERION_DUMP_DATA", path, sizeof path);
  if (n == 0 || n >= sizeof path)
    return;
  HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
  if (h == INVALID_HANDLE_VALUE)
    return;
  DWORD wrote;
  WriteFile(h, Block<BYTE>(OC_DATA_VA), OC_DATA_END - OC_DATA_VA, &wrote, NULL);
  CloseHandle(h);
}

// The global at 1.17's address va, wherever the engine keeps it.
template <class T> static T *Data(DWORD va)
{
  return reinterpret_cast<T *>(PortDataAddress(va));
}

// What 1.17's start-up did for the game before WinMain; WinMain calls it first.
void __cdecl PortStartUp(void)
{
  // the link's second word, at 1.17's +4
  for (size_t i = 0; i < sizeof kListHeads / sizeof kListHeads[0]; i++)
    *Data<UINT_PTR>(kListHeads[i] + 4) = ~PortDataAddress(kListHeads[i]);
  for (size_t i = 0; i < sizeof kCriticalSections / sizeof kCriticalSections[0]; i++)
    InitializeCriticalSection(Data<CRITICAL_SECTION>(kCriticalSections[i]));
  *Data<DWORD>(kMainThread) = GetCurrentThreadId();
  *Data<HANDLE>(kLeagueEvent) = CreateEventA(NULL, TRUE, FALSE, Data<const char>(kLeagueEventName));
  *Data<int>(kIsaAvailable) = __isa_available;
  DataCheckpoint();
}

// The command line after the program's name, as the C runtime hands it to WinMain.
static LPSTR CommandTail(void)
{
  char *p = GetCommandLineA();
  if (*p == '"') {
    for (p++; *p && *p != '"'; p++)
      ;
    if (*p == '"')
      p++;
  } else {
    // unsigned, so a byte of an accented or Korean name in the program's path is part of
    // the word and not whitespace
    while (static_cast<unsigned char>(*p) > ' ')
      p++;
  }
  while (*p && static_cast<unsigned char>(*p) <= ' ')
    p++;
  return p;
}

// The game, as 1.17's start-up called it: Hyperion.exe calls this once 1.17's data is
// loaded and the development DLLs are in.
int __cdecl PortRunGame(void)
{
  STARTUPINFOA si;
  si.cb = sizeof si;
  GetStartupInfoA(&si);
  int show = si.dwFlags & STARTF_USESHOWWINDOW ? si.wShowWindow : SW_SHOWDEFAULT;
  return WinMain(GetModuleHandleA(NULL), NULL, CommandTail(), show);
}

// Local.dll, where the game loads its strings, dialogs and accelerators from
// (gds_local.cpp). It exports nothing, so the game reaches only its resources; it is
// loaded as a data file, so none of its code runs and the 64-bit process can read the
// 32-bit DLL.
HMODULE __cdecl PortLoadResources(const char *pszPath)
{
  return LoadLibraryExA(pszPath, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
}

const void *PortModuleResource(void *hModule, int nResId, int nType, DWORD *pcbSize)
{
  HMODULE h = static_cast<HMODULE>(hModule);
  HRSRC hInfo = h ? FindResourceA(h, MAKEINTRESOURCEA(nResId), MAKEINTRESOURCEA(nType)) : NULL;
  HGLOBAL hRes = hInfo ? LoadResource(h, hInfo) : NULL;
  if (hRes == NULL)
    return NULL;
  if (pcbSize)
    *pcbSize = SizeofResource(h, hInfo);
  return LockResource(hRes);
}
