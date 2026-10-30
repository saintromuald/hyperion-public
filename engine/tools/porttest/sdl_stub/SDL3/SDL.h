// SDL.h for the 32-bit port tests (build_porttest.bat): the few SDL calls the Storm files make
// (port_stormfile.cpp, port_stormvid.cpp), on the C runtime, so the tests build with VS2013 and
// need no SDL3.dll.
#pragma once
#include <stdlib.h>
#define SDL_malloc malloc
#define SDL_free free
#define SDL_getenv getenv
#define SDL_atoi atoi
