// port_mask.h -- the screen's masks, in place of Storm's STrans functions (port_mask.cpp).
#ifndef PORT_MASK_H
#define PORT_MASK_H

// The screen's masks (port_mask.cpp), in place of Storm's STrans functions: a
// mask marks the pixels a copy takes over its own width and height. Storm's
// handle type, HSTRANS, points at one of these. Each returns NULL where
// Storm's call would fail.
struct _HSTRANS;
struct _HSTRANS *PortMaskCreate(const unsigned char *pBits, int nWidth, int nHeight);
struct _HSTRANS *PortMaskFull(int nWidth, int nHeight);
struct _HSTRANS *PortMaskInvert(const struct _HSTRANS *pMask);
struct _HSTRANS *PortMaskSubtract(const struct _HSTRANS *pMask, const struct _HSTRANS *pCut, int nX, int nY);
struct _HSTRANS *PortMaskDuplicate(const struct _HSTRANS *pMask);
void PortMaskSetDirtyGrid(int nWidth, int nHeight, int nCellWidth, int nCellHeight);
struct _HSTRANS *PortMaskIntersectDirty(const struct _HSTRANS *pMask, const unsigned char *pDirty,
                                        unsigned char bDirty);
void PortMaskBlt(unsigned char *pDst, const unsigned char *pSrc, int nDstPitch, int nSrcPitch,
                 const struct _HSTRANS *pMask);
int PortMaskHasPixel(const struct _HSTRANS *pMask, int nX, int nY);
void PortMaskDelete(struct _HSTRANS *pMask);

const unsigned char *PortMaskBits(const struct _HSTRANS *pMask, int *pnWidth, int *pnHeight);

#endif
