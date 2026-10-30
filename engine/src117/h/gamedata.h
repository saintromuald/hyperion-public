// gamedata.h -- declarations for src117/lang/gamedata.cpp: the game data file
// layer, the archive-backed open, read and close wrappers, the asynchronous load
// queue, and the GRP, overlay and bitmap loaders other subsystems read MPQs with.
//
// Eleven of gamedata.cpp's functions are declared elsewhere and not repeated
// here: load_array_data and load_game_data in snd.h, gamedata_free_data in
// CSprite.h, CheckAnyAsyncFailed in event_Win.h, and GameLoadImage, CloseFile,
// GetFileSize_gds, OpenFileEx, ReadFile_gds, alloc_load_bmp and mem_free in
// glues.h; the definitions match those spellings. The Storm imports these bodies
// call are declared in Storm.h.

#ifndef OC117_GAMEDATA_H
#define OC117_GAMEDATA_H

#include "platform.h"

struct GROUP;

// Hyperion: moved here from src117/lang/gamedata.cpp so the eleven DAT tables
// (gUnitsArrayDataTbl and the rest)
// have one type in every TU; its layout changes at 64 bits. Each table holds its
// entries and a NULL pDest after them.
// load_array_data's descriptor table. Its walk is
//   `mov eax,[edx]` / `lea edi,[edx+4]` / `mov esi,[edi+4]` /
//   `imul esi,[edi]` / `add edi,0Ch` / `mov eax,[edi-4]`
// -- a 12-byte stride with a dword at +0 (the destination, NULL-terminates the
// table), a dword at +4 and a dword at +8 whose product is a BYTE COUNT
// (`cmp esi,ebx` against the remaining file bytes, then memcpy's third
// argument). Which of +4/+8 is the count and which the element size is NOT
// derivable from a product; the names below are a convention and the byte
// order in the source (`+8 * +4`) is what is load-bearing.
struct array_data_t
{
  /*0x00*/ void *pDest;
  /*0x04*/ DWORD dwCount;
  /*0x08*/ DWORD dwElemSize;
};
static_assert(offsetof(array_data_t, pDest) == 0x00, "array_data_t::pDest");
OC_X86_LAYOUT(offsetof(array_data_t, dwCount) == 0x04, "array_data_t::dwCount");
OC_X86_LAYOUT(offsetof(array_data_t, dwElemSize) == 0x08, "array_data_t::dwElemSize");
OC_X86_LAYOUT(sizeof(array_data_t) == 0x0C, "array_data_t size");

// 0x4444D0, 13 B. Calls SFileSetIoErrorMode.
void InitFileErrorMode();

// 0x4443D0, 153 B. One argument in ECX.
intptr_t OpenMemVariantFile(intptr_t nArg0);

// 0x4443A0, 27 B. Both registers are only passed on to a callee, so their
// types are placeholders.
intptr_t sub_4443A0(intptr_t nArg0, intptr_t nArg1);

// 0x4445A0, 66 B. ECX, EDX and one stack dword (`ret 4h`).
BOOL OpenFile(intptr_t nArg0, intptr_t nArg1, int nArg2);

// 0x444750, 189 B. ECX, EDX and one stack dword. ECX is a pointer whose
// pointee the body does not show.
void wait_object_loading(void *pArg0, intptr_t nArg1, int nArg2);

// 0x444B10, 177 B. ECX, EDX and four stack dwords (`ret 10h`).
intptr_t LoadGroupFile(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2, intptr_t nArg3, intptr_t nArg4, int nArg5);

// 0x444BD0, 204 B. ECX, EDX and four stack dwords (`ret 10h`).
intptr_t LoadOverlayFile(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2, intptr_t nArg3, intptr_t nArg4, int nArg5);

// Two callbacks, reached only through their address, so the arity is a lower
// bound. Both pop their whole argument list and read no register: __stdcall,
// which under /Gr is the same code as a fastcall that ignores ECX and EDX.

// 0x444810, 27 B. Passes its one argument to SMemAlloc.
// Hyperion: Storm reads it, the block left in EAX, so it is returned: a void
// function's result is no register's leftover in WebAssembly.
void *__stdcall AllocCB(int nArg0);

// 0x444830, 87 B. The SFileSetIoErrorMode handler; its second argument is never
// read. BOOL because every path sets EAX to 0 or 1.
BOOL __stdcall sfile_io_err_cb(intptr_t nArg0, int nArg1, int nArg2);

// Two functions other .cpp files also declare locally, spelled here exactly as
// they spell them: a different spelling mangles differently and fails their link.

// 0x444A60, 174 B. As glues.cpp, opt_dlgs.cpp and cur.cpp spell it.
GROUP *__fastcall ReadGroupFile(const char *pszFile, intptr_t nFlags,
                                const char *pszTag, int nLine);

// 0x444990, 197 B. As maphdr.cpp spells it. ECX is the file name, passed on to
// SFileOpenFileEx with nScope; nFlags is only compared with 0, pdwSize receives
// the size and the last two are Storm's owner tag. Every caller passes 0 in EDX.
void *__fastcall MemFileLoad(const char *pszFile, DWORD_PTR dwArg2, DWORD *pdwSize,
                             int nFlags, int nScope, const char *pszOwnerFile,
                             int nOwnerLine);

#endif  // OC117_GAMEDATA_H
