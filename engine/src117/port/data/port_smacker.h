// port_smacker.h -- the Smacker player, in place of RAD's smackw32.dll (port_smacker.cpp).
#ifndef PORT_SMACKER_H
#define PORT_SMACKER_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types

// The Smacker player (port_smacker.cpp), in place of RAD's smackw32.dll: its
// calls are smackw32's that storm.dll's player made, and do what they did.
struct SmkFile;
struct PortVideoPlanes;
struct SmkRect {
  ULONG x, y, w, h;
};
bool SmkSoundUseDirectSound(void *pDirectSound);
SmkFile *SmkOpen(void *hFile, ULONG dwOpenFlags);
// A video in full colour (port_video.h), as a StarCraft: Remastered install's WebM or a mod's,
// played as a Smacker file:
// its frames put in 256 colours, entries nFirst to nLast of the palette pfnPalette writes (768
// bytes of RGB), asked again as each frame is painted, when it is given; else a palette of each
// group of pictures' own, in entries 1 to 254. The bytes are copied.
SmkFile *SmkOpenVideo(const void *pData, ULONG cb, ULONG dwOpenFlags, void (*pfnPalette)(unsigned char *pRGB),
                     int nFirst, int nLast);
// The same for the GPU presenter, which puts the frames in colour itself: each is handed on as
// its planes (SmkVideoPlanes), and what the file paints is nothing.
SmkFile *SmkOpenVideoPlanes(const void *pData, ULONG cb, ULONG dwOpenFlags);
// The planes of the frame set up, until the next is; NULL when it did not decode or the file is
// not one of those.
const PortVideoPlanes *SmkVideoPlanes(const SmkFile *p);
void SmkClose(SmkFile *p);
void SmkToBuffer(SmkFile *p, ULONG dwLeft, ULONG dwTop, ULONG dwPitch, void *pvBuffer);
ULONG SmkDoFrame(SmkFile *p);
void SmkNextFrame(SmkFile *p);
ULONG SmkWait(SmkFile *p);
bool SmkToBufferRect(SmkFile *p, int nMode, SmkRect *pRect);
ULONG SmkWidth(const SmkFile *p);
ULONG SmkHeight(const SmkFile *p);
ULONG SmkFrames(const SmkFile *p);
ULONG SmkFrameNum(const SmkFile *p);
// A frame's length in hundredths of a millisecond, 0 for a video that sets no pace.
ULONG SmkInterval(const SmkFile *p);
bool SmkNewPalette(const SmkFile *p);
const unsigned char *SmkPaletteRGB(const SmkFile *p);
const unsigned char *SmkTrackData(SmkFile *p, int nTrack, ULONG *pcb);

#endif
