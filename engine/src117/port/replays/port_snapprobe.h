// port_snapprobe.h -- the trainer's memory probe (port_snapprobe.cpp, HYPERION_SNAPPROBE).
#ifndef PORT_SNAPPROBE_H
#define PORT_SNAPPROBE_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types

// The trainer's memory probe (port_snapprobe.cpp, HYPERION_SNAPPROBE=a,b): the game's heap blocks
// and the .data bytes that change between two game frames.
void PortSnapProbeAlloc(const void *pv, size_t cb, const char *pszTag, int nLine);
void PortSnapProbeFree(const void *pv);
void PortSnapProbeFrame(void);

#endif
