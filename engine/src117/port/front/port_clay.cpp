// port_clay.cpp -- Clay's implementation, for the front end's layout (port_ui.cpp,
// docs/front-end.md, F1). Clay is one C header (zlib licence, fetched by tools/fetch_clay.py).
// Its implementation is compiled here as C++20, which its designated initializers need, and
// not as C: MSVC's C compiler makes Clay's one-byte enums four bytes, so its structs would
// not match the ones the engine's C++ sees. The engine's C++ calls its functions without the
// macros, which need C++20 too.
#define CLAY_IMPLEMENTATION
#include "clay.h"
