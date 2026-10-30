// gds_grp2bmpx.h -- declarations for src117/lang/gds_grp2bmpx.cpp, a bitmap view
// of one GRP frame plus its placement. Free functions are __fastcall through /Gr
// and are declared without the keyword.

#ifndef OC117_GDS_GRP2BMPX_H
#define OC117_GDS_GRP2BMPX_H

#include "platform.h"

#include "bmp.h"
#include "grp.h"

// 0x4CF570 grp_to_bmpx (111 B), `ret 14h`. grp_to_bmp's registers and bitmap,
// then four out-parameters, each written with a 16-bit store: the frame's two
// offset bytes and the group's two WORD dimensions. The body does not decide
// the pointees' signedness. All 29 callers discard EAX.
void grp_to_bmpx(GROUP *pGroup, unsigned char bFrame, bmp_t *pBmp,
                 short *pnX, short *pnY, short *pnWidth, short *pnHeight);

#endif  // OC117_GDS_GRP2BMPX_H
