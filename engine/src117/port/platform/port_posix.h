// port_posix.h -- the command line and paths off Windows (port_posix.cpp); on Windows a path is
// used as it is.
#ifndef PORT_POSIX_H
#define PORT_POSIX_H

#if defined(__EMSCRIPTEN__)
// In a browser (port_posix.cpp): the page's turn, to draw and take input, once a
// frame's time has passed without one.
void PortWebTurn(void);
// The program is ending: the page round it is told (web/play's hyExit), as a browser's tab
// would otherwise keep its last frame.
void PortWebExit(int nCode);
#endif

#if !defined(_WIN32)
// The command line GetCommandLineA gives, from main's arguments (port_posix.cpp).
void PortPosixSetCommandLine(int argc, char **argv);
// The file system's own form of a path in the engine's, Windows', form, for the calls into
// SDL and the archive libraries (port_posix.cpp); it lasts until a few more calls.
const char *PortNativePath(const char *pszPath);
// The program's folder as the player sees it, in the file system's form with its slash
// (port_posix.cpp): the exe's, or on a Mac the folder Hyperion.app is in. PortProgramFolder
// (port_paths.h) is the same in the engine's form.
const char *PortPosixProgramFolder(void);
#else
inline const char *PortNativePath(const char *pszPath) { return pszPath; }
#endif

#if defined(__APPLE__)
// Keeps the run from App Nap, which slows a program whose windows are not seen: a game in the
// background plays on at its speed (port_posix.cpp).
void PortPosixKeepRunning(void);
#endif

#endif
