// chunk.h -- declarations for src117/lang/chunk.cpp: the chunk reader that
// splits a loaded file image into tagged chunks and dispatches each to a
// handler table. ReadBufferChunks, ReadChunk and sub_4C1350 keep the spellings
// maphdr.h gave them, so their consumers compile unchanged.

#ifndef OC117_CHUNK_H
#define OC117_CHUNK_H

#include "platform.h"
#include "TSList.h"

// One chunk: the end of the image, the chunk's data, its tag and its length.
// ReadChunk copies dwSize bytes from pData when they end inside the image.
struct chunk_t {
  /*0x00*/ BYTE *pEnd;
  /*0x04*/ BYTE *pData;
  /*0x08*/ DWORD dwId;
  /*0x0C*/ DWORD dwSize;
};
OC_X86_LAYOUT(sizeof(chunk_t) == 0x10, "chunk_t size");
static_assert(offsetof(chunk_t, pEnd) == 0x00, "chunk_t::pEnd");
OC_X86_LAYOUT(offsetof(chunk_t, pData) == 0x04, "chunk_t::pData");
OC_X86_LAYOUT(offsetof(chunk_t, dwId) == 0x08, "chunk_t::dwId");
OC_X86_LAYOUT(offsetof(chunk_t, dwSize) == 0x0C, "chunk_t::dwSize");

// A list node holding one chunk; ChunkCreate allocates 0x18 bytes.
struct CHUNKNODE {
  /*0x00*/ TSLink<CHUNKNODE> link;
  /*0x08*/ chunk_t chunk;
};
OC_X86_LAYOUT(sizeof(CHUNKNODE) == 0x18, "CHUNKNODE size");
static_assert(offsetof(CHUNKNODE, link) == 0x00, "CHUNKNODE::link");
OC_X86_LAYOUT(offsetof(CHUNKNODE, chunk) == 0x08, "CHUNKNODE::chunk");

// The chunk list, an explicit specialization with TSList.h's layout.
template <> struct TSList<CHUNKNODE,TSGetLink<CHUNKNODE> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<CHUNKNODE> link;
  // 0x4C1110 (84 B). Unlink and free every node.
  void Clear(void);
};

typedef TSList<CHUNKNODE, TSGetLink<CHUNKNODE> > CHUNKLIST;

static_assert(offsetof(CHUNKLIST, _unmapped_00) == 0x00, "TSList.h layout");
static_assert(offsetof(CHUNKLIST, link) == 0x04, "Clear reads [this+8] as link.next");
OC_X86_LAYOUT(sizeof(CHUNKLIST) == 12, "TSList.h layout");

// A handler gets the chunk, its length and the caller's destination.
typedef BOOL(__stdcall *chunkhandler_fn)(void *pChunk, int nSize, void *pDest);

// One handler-table entry, 12 bytes. A required tag must be present in the
// image or CheckChunks fails the load.
struct chunkhandler_t {
  /*0x00*/ DWORD dwId;
  /*0x04*/ chunkhandler_fn pfnHandler;
  /*0x08*/ BYTE bRequired;
  /*0x09*/ BYTE _pad09[3];
};
OC_X86_LAYOUT(sizeof(chunkhandler_t) == 0x0C, "chunkhandler_t size");
static_assert(offsetof(chunkhandler_t, dwId) == 0x00, "chunkhandler_t::dwId");
OC_X86_LAYOUT(offsetof(chunkhandler_t, pfnHandler) == 0x04, "chunkhandler_t::pfnHandler");
OC_X86_LAYOUT(offsetof(chunkhandler_t, bRequired) == 0x08, "chunkhandler_t::bRequired");
OC_X86_LAYOUT(offsetof(chunkhandler_t, _pad09) == 0x09, "chunkhandler_t::_pad09");

// InitNodes' per-chunk callback; ChunkCreate is the only one.
typedef BOOL(__stdcall *chunkcreate_fn)(chunk_t *pChunk, int nIndex,
                                        CHUNKLIST *pList);

// sub_4C1350's callback: the entry's name, its version id, the context.
typedef int(__stdcall *chunkenum_fn)(const char *pszName, int nId,
                                     void *pvContext);

// 0x4C1020 (73 B). TRUE when every required tag in the table is in the list.
BOOL CheckChunks(CHUNKLIST *pList, const chunkhandler_t *pTbl, int nCount);

// 0x4C1070 (147 B). Appends a node holding a copy of the chunk; always TRUE.
BOOL __stdcall ChunkCreate(chunk_t *pChunk, int nIndex, CHUNKLIST *pList);

// 0x4C1170 (134 B). Walks the image's 8-byte chunk headers, calling pfnCreate
// for each until it fails or the next header would pass the end. Always TRUE;
// the one caller discards it.
BOOL InitNodes(void *pBuffer, DWORD dwSize, chunkcreate_fn pfnCreate,
               CHUNKLIST *pList);

// 0x4C1200 (171 B). ECX is the loaded file image, EDX its length.
BOOL __fastcall ReadBufferChunks(void *pBuffer, DWORD dwSize,
                                 const chunkhandler_t *pTbl, int nCount,
                                 void *pDest);

// 0x4C12B0 (39 B). Declared twice: the extern "C" line is the one the payload
// links against, the plain line is the one the generator reads.
extern "C" BOOL __fastcall ReadChunk(void *pFile, void *pDest);
BOOL ReadChunk(void *pFile, void *pDest);

// 0x4C12E0 (108 B). Calls each table entry's handler for every node carrying
// its tag; FALSE as soon as one handler fails.
BOOL LoadChunks(CHUNKLIST *pList, const chunkhandler_t *pTbl, int nCount,
                void *pDest);

// 0x4C1350 (81 B). Offers each sgAuthTemplates_g entry to pfnEnum, highest
// version first, until the callback returns FALSE. Both callers discard EAX,
// and the value left there is not a result.
void __stdcall sub_4C1350(chunkenum_fn pfnEnum, void *pvContext);

#endif  // OC117_CHUNK_H
