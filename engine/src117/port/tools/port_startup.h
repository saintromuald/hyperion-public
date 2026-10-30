// port_startup.h -- the start-up log (port_startup.cpp, docs/modules/tools.md, "The start-up
// log").
#ifndef PORT_STARTUP_H
#define PORT_STARTUP_H

// The program's first line, before anything else: the start's clock, and on Windows the check
// that SDL3.dll is there (it is loaded on first use, so a missing one can be said).
void PortStartupBegin(void);

// The log opened in the data folder, once this copy holds it: the previous start's log kept
// under logs\ (the last five), what it says of how that start or session ended, the header
// (port_sysinfo.cpp) and the lines kept until now. bPlayer: a start a player made, which may
// ask and be told; a script's or the harness's is only logged.
void PortStartupOpen(bool bPlayer);

// One step of the start, with what it found: written at once, so the last line of a start that
// hangs or dies says where.
void PortStartupStep(const char *pszStage, const char *pszFormat, ...);

// A wait on the player (the first-start window, 1.17's dialogs, a cinematic): no step is due
// while it lasts.
void PortStartupWait(bool bOn);

// The program has reached its menus or its game: the next frame shown ends the start.
void PortStartupReady(void);
void PortStartupFrame(void);
bool PortStartupDone(void);

// A start that cannot go on: the line, then the error window, which says so and where the log is
// and has what a player copies to ask for help (port_errorwin.cpp), then the process ends with 1.
[[noreturn]] void PortStartupFail(const char *pszStage, const char *pszFormat, ...);

// Whether the start has not got to its menus or game yet, when a failure ends it through
// PortStartupFail rather than as a crash.
bool PortStartupPending(void);

// This copy found another on its data folder and is ending: its log goes under logs\, beside
// the running copy's.
void PortStartupOtherCopy(void);

// The session's end, in the log: a crash's report folder, or an ordinary end with its code.
void PortStartupCrashed(const char *pszFolder);
void PortStartupEnded(int nCode);

// The log's path, once opened.
const char *PortStartupLogPath(void);

#endif
