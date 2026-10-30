// gluMap.h -- declarations for src117/lang/gluMap.cpp: the map-selection list
// for the create-game screens -- the maps directory walk, the recent-maps
// list, and the map list box
//
// Class members belong in the class header; this file owns the free functions. They are
// spelled without an explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_GLUMAP_H
#define OC117_GLUMAP_H

#include "platform.h"
#include "TSList.h"
#include "gametype.h"   // GameHeader, TGAMEPARAMS

// The map list's node, one per map file or directory the browser found.
// 0x544 bytes, from AddMap's `push 544h` into SMemAlloc; the link at +0 is
// what makes `&pEntry->link` and pEntry the same address in every walk.
struct LISTENTRY {
  /*0x000*/ TSLink<LISTENTRY> link;
  /*0x008*/ char szShort[0x41];
  // Each string's capacity is the SStrCopy or psnprintf argument at that
  // offset; the gaps below are the spans no body here reads.
  /*0x049*/ char szStatus[0x20];
  /*0x069*/ char szDescription[0x100];
  /*0x169*/ char szGameType[0x20];
  /*0x189*/ char szField189[0x1A5 - 0x189];
  /*0x1A5*/ char szPlayers[0x23];
  /*0x1C8*/ char szComputers[0x23];
  /*0x1EB*/ char szHumans[0x23];
  /*0x20E*/ char szDimensions[0x23];
  /*0x231*/ char szTileset[0x23];
  // The list-box index sub_45B850 stores and returns.
  /*0x254*/ DWORD dwListIndex;
  /*0x258*/ DWORD dwLoaded;
  /*0x25C*/ DWORD dwLoading;
  /*0x260*/ DWORD dwField260;
  /*0x264*/ DWORD dwField264;
  /*0x268*/ BYTE bFlags;
  /*0x269*/ char szName[0x104];
  /*0x36D*/ char szPath[0x104];
  /*0x471*/ BYTE _unmapped_471[0x472 - 0x471];
  /*0x472*/ WORD wWidth;
  /*0x474*/ WORD wHeight;
  /*0x476*/ WORD wTileset;
  /*0x478*/ BYTE bField478;
  /*0x479*/ BYTE bField479;
  /*0x47A*/ BYTE bField47A;
  /*0x47B*/ BYTE bField47B;
  /*0x47C*/ BYTE _unmapped_47C[0x480 - 0x47C];
  // The 0xB5-byte save-game header blob, the same one saveload.h's
  // TSAVED_GAME_NODE::abHeader carries; ReadSaveInfo copies it whole.
  /*0x480*/ BYTE abHeader[0xB5];
  // The eight selectable-race bytes get_selectable_races fills; its EDX is 8.
  /*0x535*/ BYTE abRaces[8];
  /*0x53D*/ BYTE _unmapped_53D[0x540 - 0x53D];
  // The replay's campaign mission, or 0; ReadReplayInfo widens GetReplayMission's WORD.
  /*0x540*/ DWORD dwMission;

  // Defined in gluMap.cpp, whose AddDirectory is the only delete of an entry:
  // the destructor unlinks and the class delete frees under the "delete" tag.
  ~LISTENTRY();
  void operator delete(void *p);
};

static_assert(offsetof(LISTENTRY, link) == 0x000, "sub_45B390 reads [p] and [p+4]");
OC_X86_LAYOUT(offsetof(LISTENTRY, szShort) == 0x008, "AddMap SStrCopy(edi+8, .., 41h)");
OC_X86_LAYOUT(offsetof(LISTENTRY, szStatus) == 0x049, "ReadSaveInfo SStrCopy(ebx+49h, .., 20h)");
OC_X86_LAYOUT(offsetof(LISTENTRY, szDescription) == 0x069, "ReadSaveInfo psnprintf(ebx+69h, 100h, ..)");
OC_X86_LAYOUT(offsetof(LISTENTRY, szGameType) == 0x169, "UpdateMapCtrl SStrCopy(esi+169h, .., 20h)");
OC_X86_LAYOUT(offsetof(LISTENTRY, szField189) == 0x189, "UpdateMapCtrl lea eax, [esi+189h] into control 2");
OC_X86_LAYOUT(offsetof(LISTENTRY, szPlayers) == 0x1A5, "ReadSaveInfo psnprintf(ebx+1A5h, 23h, ..)");
OC_X86_LAYOUT(offsetof(LISTENTRY, szComputers) == 0x1C8, "ReadScenarioInfo psnprintf(esi+1C8h, 23h, ..)");
OC_X86_LAYOUT(offsetof(LISTENTRY, szHumans) == 0x1EB, "ReadScenarioInfo psnprintf(esi+1EBh, 23h, ..)");
OC_X86_LAYOUT(offsetof(LISTENTRY, szDimensions) == 0x20E, "ReadSaveInfo psnprintf(ebx+20Eh, 23h, ..)");
OC_X86_LAYOUT(offsetof(LISTENTRY, szTileset) == 0x231, "ReadSaveInfo SStrCopy(ebx+231h, .., 23h)");
OC_X86_LAYOUT(offsetof(LISTENTRY, dwListIndex) == 0x254, "sub_45B850 mov dword ptr [esi+254h], eax");
OC_X86_LAYOUT(offsetof(LISTENTRY, dwLoaded) == 0x258, "ReadSaveInfo cmp dword ptr [ebx+258h], 0");
OC_X86_LAYOUT(offsetof(LISTENTRY, dwLoading) == 0x25C, "ReadSaveInfo mov dword ptr [ebx+25Ch], 1");
OC_X86_LAYOUT(offsetof(LISTENTRY, dwField260) == 0x260, "ReadScenarioInfo mov [esi+260h], eax");
OC_X86_LAYOUT(offsetof(LISTENTRY, dwField264) == 0x264, "ReadSaveInfo mov [ebx+264h], eax");
OC_X86_LAYOUT(offsetof(LISTENTRY, bFlags) == 0x268, "AddMap mov [edi+268h], bl");
OC_X86_LAYOUT(offsetof(LISTENTRY, szName) == 0x269, "sub_45CB50 lea eax, [esi+269h]");
OC_X86_LAYOUT(offsetof(LISTENTRY, szPath) == 0x36D, "AddMap lea esi, [edi+36Dh]");
OC_X86_LAYOUT(offsetof(LISTENTRY, wWidth) == 0x472, "ReadSaveInfo mov word ptr [ebx+472h], ax");
OC_X86_LAYOUT(offsetof(LISTENTRY, wHeight) == 0x474, "ReadSaveInfo mov word ptr [ebx+474h], ax");
OC_X86_LAYOUT(offsetof(LISTENTRY, wTileset) == 0x476, "ReadSaveInfo movzx ecx, word ptr [ebx+476h]");
OC_X86_LAYOUT(offsetof(LISTENTRY, bField478) == 0x478, "ReadSaveInfo movzx eax, byte ptr [ebx+478h]");
OC_X86_LAYOUT(offsetof(LISTENTRY, bField479) == 0x479, "ReadSaveInfo mov byte ptr [ebx+479h], al");
OC_X86_LAYOUT(offsetof(LISTENTRY, bField47A) == 0x47A, "ReadScenarioInfo mov byte ptr [esi+47Ah], bh");
OC_X86_LAYOUT(offsetof(LISTENTRY, bField47B) == 0x47B, "ReadScenarioInfo mov byte ptr [esi+47Bh], al");
OC_X86_LAYOUT(offsetof(LISTENTRY, abRaces) == 0x535, "ReadScenarioInfo lea ecx, [esi+535h] with edx = 8");
OC_X86_LAYOUT(offsetof(LISTENTRY, abHeader) == 0x480, "ReadSaveInfo lea edi, [ebx+480h] then rep movsd");
OC_X86_LAYOUT(offsetof(LISTENTRY, dwMission) == 0x540, "ReadReplayInfo mov dword ptr [esi+540h], eax");
OC_X86_LAYOUT(sizeof(LISTENTRY) == 0x544, "AddMap push 544h into SMemAlloc");

// The map list's class, spelled as the ledger spells its Clear row so gen can
// seat it. gMapList_g itself is spelled MAPLIST, which carries the cached link
// offset the walks read.
#pragma pack(push, 1)
template <> struct TSList<LISTENTRY,TSGetLink<LISTENTRY> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<LISTENTRY> link;
  // 0x45B7F0 (84 B). Unlinks and frees every map-list entry.
  void Clear(void);
  // The oracle's one LinkNode body at 0x440C10 serves this list too.
  void LinkNode(LISTENTRY *pNode, ULONG dwWhere, LISTENTRY *pBefore);
};
#pragma pack(pop)

// The map index is Storm's hash-table template, instantiated once. The template arguments
// are spelled as the oracle's decorated row 0x45CF70 mangles them -- `UHASHENTRY@@`,
// `VHASHKEY_CONSTSTRI@@` and `VTSExplicitList` -- and each instantiation is an explicit
// specialization with the ledger's own argument list.
struct HASHENTRY;
class HASHKEY_CONSTSTRI;

// The link policy the hash table's lists name; the link offset lives in the
// list head instead.
template <class T>
struct TSGetExplicitLink;

// The list of all nodes. Its link offset is stored in the head, where the
// primary template in TSList.h keeps an unnamed route DWORD.
template <>
struct TSList<HASHENTRY,TSGetExplicitLink<HASHENTRY> > {
  /*0x00*/ int nLinkOffset;
  /*0x04*/ TSLink<HASHENTRY> link;

  // Inline, defined in gluMap.cpp: unlinks every node, then the terminator.
  ~TSList();

  HASHENTRY *Head() {
    return (intptr_t)link.next > 0 ? link.next : NULL;
  }

  void UnlinkAll(void);
};

// A bucket: a list head whose link offset is stored rather than fixed. The
// 0xDDDDDDDD argument is the "explicit" marker the constructor also stores.
template <class T, ULONG N>
class TSExplicitList;

template <>
class TSExplicitList<HASHENTRY,3722304989>
    : public TSList<HASHENTRY,TSGetExplicitLink<HASHENTRY> > {
public:
  // Inline, defined in gluMap.cpp: only that TU constructs a bucket.
  TSExplicitList();
  TSExplicitList(const TSExplicitList &src);

  // Re-keys an empty-able list to a new link offset, dropping its nodes.
  void ChangeLinkOffset(int nOffset);

};

// The bucket array. Destructor is a named member, not a C++ destructor: it is
// called explicitly and keeps no vtable.
template <class T>
class TSFixedArray;

template <>
class TSFixedArray<TSExplicitList<HASHENTRY,3722304989> > {
public:
  /*0x00*/ DWORD nMaxCount;
  /*0x04*/ DWORD nCount;
  /*0x08*/ TSExplicitList<HASHENTRY,3722304989> *pData;

  TSFixedArray() {
    Constructor();
  }

  ~TSFixedArray() {
    Destructor();
  }

  void Destructor();
  void ReallocData(ULONG nCount);

  void Constructor() {
    nMaxCount = 0;
    nCount = 0;
    pData = NULL;
  }

  void Clear() {
    Destructor();
    Constructor();
  }
};

template <class T>
class TSGrowableArray;

template <>
class TSGrowableArray<TSExplicitList<HASHENTRY,3722304989> >
    : public TSFixedArray<TSExplicitList<HASHENTRY,3722304989> > {
public:
  /*0x0C*/ DWORD nChunkSize;

  TSGrowableArray() {
    nChunkSize = 0;
  }

  ULONG CalcChunkSize(ULONG nCount);
  void SetCount(ULONG nCount);
};

// The part of the hash node the table owns: the hash, the bucket link at +4,
// the all-nodes link at +0Ch and the key. The constructor seeds the two list
// heads with link offsets 4 and 0Ch.
template <class T, class TKey>
struct TSHashObject;

template <>
struct TSHashObject<HASHENTRY,HASHKEY_CONSTSTRI> {
  /*0x00*/ DWORD dwHash;
  /*0x04*/ TSLink<HASHENTRY> linkSlot;
  /*0x0C*/ TSLink<HASHENTRY> linkFull;
  /*0x14*/ const char *pszKey;

  // The two links and the key start empty; the hash is left as allocated.
  TSHashObject<HASHENTRY,HASHKEY_CONSTSTRI>() {
    linkSlot.prev = NULL;
    linkSlot.next = NULL;
    linkFull.prev = NULL;
    linkFull.next = NULL;
    pszKey = NULL;
  }

  ~TSHashObject<HASHENTRY,HASHKEY_CONSTSTRI>();
};

// The hash node the map list is indexed by. 0x1C bytes, from InternalNew's
// `add eax, 1Ch` before SMemAlloc.
struct HASHENTRY : public TSHashObject<HASHENTRY,HASHKEY_CONSTSTRI> {
  /*0x18*/ void *pValue;
};

static_assert(offsetof(HASHENTRY, dwHash) == 0x00, "AddMap mov [eax], esi");
static_assert(offsetof(HASHENTRY, linkSlot) == 0x04, "the constructor stores 4 as each bucket's link offset");
OC_X86_LAYOUT(offsetof(HASHENTRY, linkFull) == 0x0C, "the constructor stores 0Ch as the full list's link offset");
OC_X86_LAYOUT(offsetof(HASHENTRY, pszKey) == 0x14, "Ptr mov eax, [esi+14h]");
OC_X86_LAYOUT(offsetof(HASHENTRY, pValue) == 0x18, "AddMap mov eax, [eax+18h]");
OC_X86_LAYOUT(sizeof(HASHENTRY) == 0x1C, "InternalNew allocates the argument + 1Ch");

template <class TEntry, class TKey>
class TSHashTable;

template <>
class TSHashTable<HASHENTRY,HASHKEY_CONSTSTRI> {
public:
  // Vtable order at 0x4EDBE0: InternalDelete, InternalNew, the destructor,
  // Destroy.
  virtual void InternalDelete(HASHENTRY *pEntry);
  virtual HASHENTRY *InternalNew(TSExplicitList<HASHENTRY,3722304989> *pList,
                                 ULONG nExtra, ULONG dwFlags);
  virtual ~TSHashTable<HASHENTRY,HASHKEY_CONSTSTRI>();
  virtual void Destroy();

  TSHashTable<HASHENTRY,HASHKEY_CONSTSTRI>();

  // Storm's inlined operator delete, under its "delete" tag.
  void operator delete(void *p);

  HASHENTRY *Ptr(const char *pszKey);
  HASHENTRY *InternalNewNode(ULONG dwHash, ULONG nExtra,
                             ULONG dwFlags);
  int MonitorFullness(ULONG nSlot);
  void GrowListArray(ULONG nNewSize);
  void InternalClear(int bUnlinkOnly);

  /*0x04*/ TSExplicitList<HASHENTRY,3722304989> fullList;
  /*0x10*/ DWORD nFullness;
  /*0x14*/ TSGrowableArray<TSExplicitList<HASHENTRY,3722304989> > slotArray;
  /*0x24*/ DWORD dwHashMask;
};

typedef TSHashTable<HASHENTRY, HASHKEY_CONSTSTRI> MAPHASH;

// offsetof cannot take a template-id with a comma, so the asserts name these.
typedef TSList<HASHENTRY,TSGetExplicitLink<HASHENTRY> > HASHFULLLIST;
typedef TSExplicitList<HASHENTRY,3722304989> HASHSLOTLIST;
typedef TSFixedArray<HASHSLOTLIST> HASHSLOTFIXED;
typedef TSGrowableArray<HASHSLOTLIST> HASHSLOTARRAY;
typedef TSHashObject<HASHENTRY,HASHKEY_CONSTSTRI> HASHOBJECT;

static_assert(offsetof(HASHFULLLIST, nLinkOffset) == 0x00, "UnlinkAll mov eax, dword ptr [edi]");
static_assert(offsetof(HASHFULLLIST, link) == 0x04, "UnlinkAll mov edx, dword ptr [edi+8h]");
OC_X86_LAYOUT(sizeof(HASHFULLLIST) == 0x0C, "InternalNewNode lea ecx, [edi+edi*2] per bucket");
OC_X86_LAYOUT(sizeof(HASHSLOTLIST) == 0x0C, "SetCount add edx, 0Ch");
static_assert(offsetof(HASHSLOTFIXED, nMaxCount) == 0x00, "SetCount cmp ebx, dword ptr [edi]");
static_assert(offsetof(HASHSLOTFIXED, nCount) == 0x04, "SetCount mov eax, dword ptr [edi+4h]");
static_assert(offsetof(HASHSLOTFIXED, pData) == 0x08, "SetCount mov ecx, dword ptr [edi+8h]");
OC_X86_LAYOUT(sizeof(HASHSLOTFIXED) == 0x0C, "the chunk size follows at +0Ch");
OC_X86_LAYOUT(offsetof(HASHSLOTARRAY, nChunkSize) == 0x0C, "CalcChunkSize mov dword ptr [ecx+0Ch], 15h");
OC_X86_LAYOUT(sizeof(HASHSLOTARRAY) == 0x10, "the hash mask follows the array at +24h");
static_assert(offsetof(HASHOBJECT, dwHash) == 0x00, "Ptr cmp dword ptr [esi], ebx");
static_assert(offsetof(HASHOBJECT, linkSlot) == 0x04, "InternalNew mov dword ptr [esi+4h], 0");
OC_X86_LAYOUT(offsetof(HASHOBJECT, linkFull) == 0x0C, "~TSHashObject mov edi, dword ptr [ebx+0Ch]");
OC_X86_LAYOUT(offsetof(HASHOBJECT, pszKey) == 0x14, "InternalNew mov dword ptr [esi+14h], 0");
OC_X86_LAYOUT(sizeof(HASHOBJECT) == 0x18, "HASHENTRY's pValue follows at +18h");
OC_X86_LAYOUT(offsetof(MAPHASH, fullList) == 0x04, "the constructor stores the marker at [esi+4h]");
OC_X86_LAYOUT(offsetof(MAPHASH, nFullness) == 0x10, "MonitorFullness mov eax, dword ptr [edx+10h]");
OC_X86_LAYOUT(offsetof(MAPHASH, slotArray) == 0x14, "Destroy lea ecx, [esi+14h]");
OC_X86_LAYOUT(offsetof(MAPHASH, dwHashMask) == 0x24, "MonitorFullness mov edi, dword ptr [edx+24h]");
OC_X86_LAYOUT(sizeof(MAPHASH) == 0x28, "the vptr at +0 and the mask at +24h");

// The list head gMapList_g sits behind. TSList's 12-byte shape: a route DWORD
// at +0 and the link at +4, so 0x508204 is &gMapList_g.link and 0x508208 is
// its next slot -- both of which the bodies address absolutely.
struct MAPLIST {
  // The cached link offset TSList keeps for the instantiation; negative means
  // it has to be recovered from the data instead.
  /*0x00*/ int nCachedLinkOffset;
  /*0x04*/ TSLink<LISTENTRY> link;
};

static_assert(offsetof(MAPLIST, nCachedLinkOffset) == 0x00, "sub_45B3F0 mov esi, dword ptr [0x508200]");
static_assert(offsetof(MAPLIST, link) == 0x04, "sub_45B390 forms 0x508204 for a 0x508200 base");
OC_X86_LAYOUT(sizeof(MAPLIST) == 12, "gModemDeviceList_g @0x50820C caps the object");


// 0x45B240 (326 B). Creates a list entry for one map file, optionally
// registering it in a hash table, and links it into the map list. `ret 8h` with
// ECX the flag byte and EDX the directory; the caller takes the entry from EAX.
LISTENTRY *AddMap(BYTE bFlags, const char *pszDir, const char *pszName,
                  MAPHASH *pHash);

// 0x45B3F0 (492 B). Links pEntry into the map list in sorted order, comparing
// the display names case insensitively. `ret` with only ECX live in.
void sub_45B3F0(LISTENTRY *pEntry);

// 0x45B5E0 (212 B). Blanks every map-setting control, then clears the
// selectable-race cache, which it reaches by a tail jump. No register input and
// no stack parameters.
void sub_45B5E0(void);

// 0x45B700 (226 B). Validates the lobby settings against the matched game
// template, writes the chosen speed and copies the template's 32-byte parameter
// record into the header. Returns 0 on success and an 0x800000NN code
// otherwise; `ret` with ECX and EDX both read before written.
int ConfigureGame(GameHeader *pHdr, BYTE bSpeed);

// 0x45B080 (447 B). Recurses into one subdirectory found by the walk, adding a
// list entry for it unless it is "." or "..". `ret 8h` with ECX the directory
// name and EDX the parent path; the caller discards EAX.
void AddDirectory(const char *pszName, const char *pszDir, int bAtRoot,
                  MAPHASH *pHash);

// 0x45C9A0 (422 B). Walks one directory and files each entry into the map list.
// `ret 0Ch` with ECX the flag byte and EDX the directory to search; the single
// caller discards EAX.
void sub_45C9A0(BYTE bFlags, const char *pszSearchDir, const char *pszDir,
                int bAtRoot, MAPHASH *pHash);

// 0x45C7D0 (456 B). Reads the saved recent-maps blob and adds one list entry
// per name that still resolves to a file. No register input.
void FindRecentMaps(void);

// 0x45DCF0 (374 B). Moves pEntry to the front of the map list, marks it recent
// and writes the first five recent names back to the registry. `ret` with only
// ECX live in and no return value.
void RecentMapsUpdate(LISTENTRY *pEntry);

// 0x45B390 (87 B). Unlinks pEntry from wherever it sits and relinks it at the
// tail of gMapList_g. A NULL argument operates on the list's own link, which is
// how the head node is inserted. `ret` with only ECX live in; the caller
// discards EAX.
void sub_45B390(LISTENTRY *pEntry);

// 0x45CB50 (63 B). Returns the list entry whose szName matches pszName, case
// insensitively, or NULL. The walk ends on the signed `jle` TSList terminator,
// never on NULL.
LISTENTRY *sub_45CB50(const char *pszName);

// 0x45CB90 (100 B). Resolves pszFileName under the user data directory, then
// the install directory, and returns 1 when the result names a plain file.
// `ret 4h` with ECX and EDX read before written: the two pointers ride the
// registers and the buffer size is the stack word.
int FindMapFile(const char *pszFileName, char *pszBuffer, int cchBuffer);

// 0x45CC00 (118 B). Loads the recent-maps registry blob into a Storm
// allocation and returns it, writing the byte count through pcbData. `ret` with
// only ECX live in; FindRecentMaps uses the full 32-bit result.
void *GetRecentMaps(DWORD *pcbData);

// 0x447B10 (59 B). Returns the template-metadata record whose key matches, or
// NULL. `ret 4h` with ECX and EDX read before written and a 2-byte stack slot,
// the same contract gametype_find_template has; its one caller uses the full
// 32-bit result. The key is a WORD: the body loads it with `mov cx, [ebp+8]`.
const TTEMPLATEMETA *gametype_find_template_meta(BYTE bCategory, BYTE bSub,
                                                 WORD wType);

// 0x45D870 (787 B). The map twin of ReadSaveInfo: probes the map file, reads its
// dimensions and slot counts and fills the same display strings. `ret` with
// only ECX live in; the caller discards EAX.
void ReadScenarioInfo(LISTENTRY *pEntry);

// 0x45DFC0 (507 B). Pushes one entry's strings into the map-setting controls
// and refreshes the slot panel. `ret` with only ECX live in; it tail-jmps
// sub_455710, so the return is void.
void UpdateMapCtrl(LISTENTRY *pEntry);

// 0x45D5E0 (653 B). Reads one saved game's header off disk and fills the
// entry's display strings from it. `ret` with only ECX live in; the caller
// discards EAX.
void ReadSaveInfo(LISTENTRY *pEntry);

// 0x45C510 (363 B). Builds a single-player game header for one map, checks it
// against the game template and starts it. `ret 0Ch` with ECX and EDX read
// before written; the first stack word is never read, and the caller uses the
// full 32-bit result as an error code.
int CreateScenarioGame(const char *pszMap, char *pszName, int nArg2, int nKey,
               BYTE bSpeed);
// Hyperion: the same header made a network game (port_lanlobby.cpp).
int PortCreateNetGame(const char *pszMap, char *pszName, int nKey, BYTE bSpeed);

// 0x4A8350 (57 B). Copies pszSrc to pszDst dropping control characters, at most
// cch of them. `ret 4h` with ECX and EDX both read before written, so the two
// pointers are the register arguments and the count is the one stack word; both
// callers discard EAX.
void stat_strip_control_chars(char *pszDst, const char *pszSrc, int cch);

// 0x45BD20. Nullary, and it tail-jumps TSList::Clear; all four callers discard EAX.
void ClearMapList();

// 0x45B850. ECX a directory buffer, EDX the LB_ADDSTRING-style callback;
// returns the list index to select, 0x80000006 when there is none.
int sub_45B850(char *pszDir, void *pfnAdd);

// The browse list's per-entry add callback; restated from gluBNCreate.h, which
// this header does not include.
typedef int(__stdcall *PFN_LBOXADD)(intptr_t nItemData, intptr_t nText, int nUnused);

// 0x45BA40. Refills the map list from nDirs directories and the list box from
// the list. `ret 14h` with ECX the kind byte and EDX the directory array.
int ClearLBox(BYTE bKind, const char **ppszDirs, int nDirs, const char *pszDir,
              int bAtRoot, PFN_LBOXADD pfnAdd, const char *pszFilter);

// 0x45D250. The replay twin of ReadSaveInfo. Only ECX live in; all three callers
// discard EAX.
void ReadReplayInfo(LISTENTRY *pEntry);

// 0x45D1E0. A TIMERPROC, reached only through SetTimer; `ret 10h`.
void __stdcall ReadEntryInfo(intptr_t hWnd, DWORD uMsg, DWORD idEvent, DWORD dwTime);

#endif  // OC117_GLUMAP_H
