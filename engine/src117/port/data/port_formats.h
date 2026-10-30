// port_formats.h -- 1.17's files in the engine's word size: GRPs, dialogs, the star field and
// saved games (port_formats.cpp).
#ifndef PORT_FORMATS_H
#define PORT_FORMATS_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types

// A GRP file the game has loaded, as a block of SMemAlloc's, made the engine's own:
// at 64 bits a new block with a frame table whose pData points at each frame's data
// and the file after it, the file's block freed; on x86 the file itself, where 1.17's
// code turns offsets into pointers as it did (port_formats.cpp). bLines, for a mod's file,
// which the RLE blitters draw: each frame's line table and lines held inside the file.
struct GROUP;
GROUP *__cdecl PortGroupNative(GROUP *pFile, bool bLines = false);
// A dialog file the game has loaded, as a block of SMemAlloc's, as the engine's
// ctrl_t records in a new block, still linked by offsets for dlg_load_ctrl to
// relocate; the file's block is freed (port_formats.cpp).
struct ctrl_t;
ctrl_t *__cdecl PortDialogNative(ctrl_t *pFile);
// parallax\star.spk as the game has loaded it, with the stars in the engine's
// starent_t layout, still offsets for scroll_init_game to relocate: at 64 bits a
// new block, the file's freed; on x86 the file (port_formats.cpp).
unsigned short *__cdecl PortStarsNative(unsigned short *pFile);
// A tileset's megatiles (pszVx4 names its .vx4) as the engine holds them: 16 cell references
// a megatile, each a DWORD, (cell << 1) | mirrored, in a block of SMemAlloc's. Remastered's
// <name>.vx4ex as it is when the source has one, whose newer doodads name cells past 1.17's
// 16 bits; otherwise the .vx4 widened. NULL, after load_game_data's error, when neither opens.
ULONG *__cdecl PortMegatilesLoad(const char *pszVx4);
// The saved game (port_formats.cpp). A save holds 1.17's
// records in 1.17's layout on both widths; layout is a SAVE117_ value of
// src117/gen/save117.h. Put and Get translate one record and return the cursor past
// its 1.17 bytes; Compress and Decompress do a run of records as one of compress's
// chunks.
unsigned int __cdecl PortSave117Size(unsigned int layout);
// This build's size of a record, and the offset in 1.17's record of the field at off in
// this build's, or -1 where none starts there (a pointer into a record, as the state
// check hashes it).
unsigned int PortSave117SizeHere(unsigned int layout);
int PortSave117Offset86(unsigned int layout, unsigned int off);
// Whether a record uses the point arm of a union of a pointer and a point
// (SAVE117_PTRPOINT) at off86, 1.17's offset of the union.
bool PortSaveUsesPoint(unsigned int layout, unsigned int off86, const void *pRec);
// Whether a record uses the pointer arm of a union of a pointer and bytes (SAVE117_PTRBYTES)
// at off86, else the bytes'.
bool PortSaveUsesPointer(unsigned int layout, unsigned int off86, const void *pRec);
unsigned char *__cdecl PortSave117Put(unsigned int layout, const void *pRec, unsigned char *pOut);
const unsigned char *__cdecl PortSave117Get(unsigned int layout, const unsigned char *pIn, void *pRec);
// A chunk of count records a codec has laid out itself, as compress writes it; the
// first in a section gives its header the count and the record size.
int __cdecl PortSaveChunk(const void *pBuf, int nBytes, unsigned int count, void *pFile);
int __cdecl PortSave117Compress(unsigned int layout, const void *pRecs, unsigned int count, void *pFile);
int __cdecl PortSave117Decompress(unsigned int layout, void *pRecs, unsigned int count, void *pFile);
// The high bytes of the ids this build keeps in 16 bits where 1.17's record has a byte
// (SAVE117_UNSIGNED8, a mod's past 255), which Put drops: how many a record
// has, and a run's, record by record, copied out to pOut or back in from pIn.
unsigned int PortSave117HighCount(unsigned int layout);
void PortSave117HighGet(unsigned int layout, const void *pRecs, unsigned int count, unsigned char *pOut);
void PortSave117HighSet(unsigned int layout, void *pRecs, unsigned int count, const unsigned char *pIn);
// Pointer slots that hold ids for the save, as 1.17's dwords: written as they are or
// as one chunk, read back sign-extended.
int __cdecl PortSaveWriteIds(void *pFile, const void *pSlots, unsigned int count);
int __cdecl PortSaveReadIds(void *pFile, void *pSlots, unsigned int count);
int __cdecl PortSaveCompressIds(const void *pSlots, unsigned int count, void *pFile);
int __cdecl PortSaveDecompressIds(void *pSlots, unsigned int count, void *pFile);
// Hyperion's container around what 1.17 writes: after the header a marker, then each part
// as a section. PortSaveSection closes the open section, if any, and opens the next;
// PortSaveEnd closes the last. PortLoadBegin tells the two formats apart, and PortLoadSection
// and PortLoadEnd check each section's id and length; for a 1.17 save they do nothing, and
// the same readers read it.
#define PORT_SAVE_ID(a, b, c, d) \
  ((ULONG)(a) | ((ULONG)(b) << 8) | ((ULONG)(c) << 16) | ((ULONG)(d) << 24))
int __cdecl PortSaveBegin(void *pFile);
int __cdecl PortSaveSection(void *pFile, ULONG id);
int __cdecl PortSaveEnd(void *pFile);
int __cdecl PortLoadBegin(void *pFile);
int __cdecl PortLoadSection(void *pFile, ULONG id);
// A section that may be absent: 1 when the next section is id and is now open, 0 when the
// next is another (or the save is 1.17's), and the file is where it was; -1 on an error.
int __cdecl PortLoadSectionIf(void *pFile, ULONG id);
// The open section's length past its head, 0 when none is open.
int __cdecl PortLoadSectionBytes(void);
int __cdecl PortLoadEnd(void *pFile);

#endif
