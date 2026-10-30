// port_storm.h -- Storm's blits and regions as 1.17 uses them (port_storm.cpp).
#ifndef PORT_STORM_H
#define PORT_STORM_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types

// Storm's SBltROP3 with SRCCOPY (port_storm.cpp, R2b), with Storm's arguments;
// the callers use it in its place.
int __stdcall PortBltRop3(void *pDest, const void *pSrc, int nWidth, int nHeight, int nDestPitch, int nSrcPitch,
                          ULONG dwUnused, ULONG dwRop);

// Storm's region as 1.17 uses it, a union of rectangles (port_storm.cpp, R2b):
// PortRgnGetRects gives the pieces SRgnGetRects would, in its order.
struct rect32_t;
void *PortRgnCreate(void);
void PortRgnDelete(void *hRgn);
void PortRgnAddRect(void *hRgn, const struct rect32_t *prc);
void PortRgnGetRects(void *hRgn, ULONG *pdwCount, struct rect32_t *pRects);
void PortRgnClear(void *hRgn);
void *PortRgnDuplicate(void *hRgn);

// Storm's SCodeExecute with either of the two programs 1.17 compiles, a lookup
// of each pixel in a 64 KB table by source and destination byte
// (port_storm.cpp, R2b).
void PortTableBlit(unsigned char *pDest, const unsigned char *pSrc, int nWidth, int nHeight, int nDestPitch,
                   int nSrcPitch, const unsigned char *pTable, int bSourceHigh);

#endif
