// gds_grptobmp.h -- declarations for src117/lang/gds_grptobmp.cpp, a bitmap view
// of one GRP frame. Free functions are __fastcall through /Gr and are declared
// without the keyword.

#ifndef OC117_GDS_GRPTOBMP_H
#define OC117_GDS_GRPTOBMP_H

#include "platform.h"

#include "bmp.h"
#include "grp.h"

// 0x4CFDA0 grp_to_bmp (70 B), `ret 4h`. ECX is the group, DL the frame, the
// stack word the bitmap it fills. All four callers discard EAX.
void grp_to_bmp(GROUP *pGroup, unsigned char bFrame, bmp_t *pBmp);

#endif  // OC117_GDS_GRPTOBMP_H
