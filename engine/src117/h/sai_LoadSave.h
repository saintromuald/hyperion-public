// sai_LoadSave.h -- declarations for src117/lang/sai_LoadSave.cpp, saving and
// loading the AI pathing data. Free functions are __fastcall through /Gr and are
// declared without the keyword.

#ifndef OC117_SAI_LOADSAVE_H
#define OC117_SAI_LOADSAVE_H

#include "platform.h"
#include "sai_Contours.h"

// Turn the region-block's saved indices back into live pointers. 0x48C940,
// nullary, both callers discard EAX.
void sai_DecodeGlobFromSave(void);

// The inverse: pointers back to indices before the block is written out.
// 0x48C9C0, nullary, its one caller discards EAX.
void sai_EncodeGlobForSave(void);

// Write the region block, the contour table and the path pool into pDest.
// 0x48CA50, ECX only (the caller sets EDX and the body never reads it);
// returns the literal 1, consumed 32-bit by saveload_write_save_data.
int sai_PathingGlobWriteSave(void *pDest, int nSize);

// The byte count sai_PathingGlobWriteSave will write. 0x48CB90, nullary,
// EAX consumed 32-bit by saveload_write_save_data.
int sai_get_contour_bytes(void);

// Read back what sai_PathingGlobWriteSave wrote. 0x48CBD0. The body reads only
// ECX, but its one caller reloads the buffer size into EDX, so nSize is unused
// rather than absent. Returns 0 when the header word is not 0x0E, else 1,
// consumed 32-bit by saveload_read_save_data. pIn is the walking pointer
// itself: a `const void *` cast into a local loses the oracle's entry
// `mov esi, ecx`.
int read_save_paths(const BYTE *pIn, int nSize);

#endif  // OC117_SAI_LOADSAVE_H
