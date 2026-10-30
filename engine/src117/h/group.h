// group.h -- declarations for src117/lang/group.cpp: the .grp group table and its
// lo-file overlays

#ifndef OC117_GROUP_H
#define OC117_GROUP_H

#include "platform.h"

// Named only by pointer here; grp.h and statdata.h carry the definitions.
struct GROUP;
struct FRAME;
struct grhsize_t;

// 0x4639B0. Frees each distinct entry of a group table and nulls every slot that
// held it. Plain `ret`, two register arguments: ECX is a table of pointers and
// EDX the entry count, read 32 bits wide and tested `jle`. Nothing is returned.
void group_free_tbl(void **ppTbl, int nCount);

// One pending .grp load. Only +0x10 is named: load_grh stores CreateEventA's
// handle there. The 0x14 size is the stride group_load_tbl walks the array with.
struct grhreq_t {
  /*0x00*/ BYTE _unmapped00[0x10];
  /*0x10*/ void *hEvent;
};
OC_X86_LAYOUT(sizeof(grhreq_t) == 0x14, "grhreq_t size     group_load_tbl: lea esi,[edi+edi*4] / shl esi,2 and add edx,14h");
static_assert(offsetof(grhreq_t, hEvent) == 0x10, "grhreq_t::hEvent  load_grh: mov dword ptr [esi+10h], eax after CreateEventA");

// 0x463A30. Loads one entry of the group table, reusing an earlier slot when the
// same id has already been loaded. `ret 10h`: ECX is the table, dereferenced only
// on the reuse path, and EDX the string pointer str_get_string takes; the four
// stack words are the index, the id array, the request and an out slot. The one
// caller uses the full EAX.
intptr_t load_grh(void **ppTbl, void *pStr, int nIndex, DWORD *pIds,
             grhreq_t *pReq, intptr_t *pnOut);

// 0x463AE0. Loads the whole group table, then rebases every frame pointer in the
// groups that ended up distinct. `ret 10h`, every slot read 4-byte. nUnused is
// never read by the callee, and EAX at the ret is a leftover no caller consumes.
void group_load_tbl(void **ppTbl, DWORD *pIds, void *pStr, int nUnused,
                    grhreq_t *pReq, intptr_t *pnSizes);

// 0x463C20. The lo-file pass. Same `ret 10h` contract and the same six slots,
// with nUnused again never read, and EAX at the ret again a leftover.
void group_load_lo(void **ppTbl, DWORD *pIds, void *pStr, int nUnused,
                   grhreq_t *pReq, intptr_t *pnSizes);

// 0x463BD0. Copies one frame header out of a group and rebases its data pointer.
// `ret 8h`: two register arguments and two stack words. The frame is a WORD, the
// width the callee compares it at before narrowing to index.
void grh_get_header(GROUP *pGroup, WORD wFrame, FRAME *pFrame,
                    grhsize_t *pSize);

#endif  // OC117_GROUP_H
