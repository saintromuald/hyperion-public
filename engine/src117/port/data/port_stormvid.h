// port_stormvid.h -- what Hyperion asks of Storm's video player beyond Storm's own calls
// (port_stormvid.cpp).
#ifndef PORT_STORMVID_H
#define PORT_STORMVID_H

// The file a playing video was opened by, the frame it is on and a frame's length in
// hundredths of a millisecond; false when hVid is no video playing (the HD portrait,
// port_console.cpp).
bool PortVidPlaying(void *hVid, const char **ppszFile, unsigned *pnFrame, unsigned *pnInterval);

#endif
