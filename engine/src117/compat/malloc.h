// malloc.h, off Windows: the C runtime's header of _alloca and the heap's calls, which
// the game includes. glibc has one; macOS has none of the name, its heap's calls are in
// <malloc/malloc.h>, and alloca comes from compiler.h.
#pragma once
#if defined(__APPLE__)
#include <stdlib.h>
#include <malloc/malloc.h>
#else
#include_next <malloc.h>
#endif
