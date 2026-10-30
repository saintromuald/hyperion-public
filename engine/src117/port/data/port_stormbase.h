// port_stormbase.h -- the size of a block of the port's SMemAlloc (port_stormbase.cpp).
#ifndef PORT_STORMBASE_H
#define PORT_STORMBASE_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types

// The size a block of the port's SMemAlloc was allocated with, 0 for one that is not
// its own (port_stormbase.cpp).
size_t __cdecl PortMemSize(const void *pMem);

#endif
