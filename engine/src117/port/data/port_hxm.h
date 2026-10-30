// port_hxm.h -- Hyperion's own map format, .hxm (port_hxm.cpp).
#ifndef PORT_HXM_H
#define PORT_HXM_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types

// Hyperion's own map format, .hxm (port_hxm.cpp). IsMap: the path names one.
// Compiled: the MPQ the map compiles to, in the data folder's cache, made when missing
// or older than the map; FALSE when the map does not compile (hyperion_hxm.log says
// why). CompileCommand: Hyperion.exe --hxm-compile <in.hxm> <out.chk>, which writes the
// scenario.chk alone and returns the process's exit code. CheckCommand: Hyperion.exe
// --hxm-check <in.hxm> [<report.txt>], the check an editor runs on a map it wrote
// (docs/hxm-for-editors.md): its errors and warnings to the report, or to standard output
// without one; exit code 0 when the map plays, 1 when it is refused, 2 when it cannot be read.
bool PortHxmIsMap(const char *pszPath);
bool PortHxmCompiled(const char *pszHxm, char *pszOut, size_t cchOut);
int PortHxmCompileCommand(const char *pszIn, const char *pszOut);
int PortHxmCheckCommand(const char *pszIn, const char *pszReport);

#endif
