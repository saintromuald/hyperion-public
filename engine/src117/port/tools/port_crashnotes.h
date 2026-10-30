// port_crashnotes.h -- the session's part of the crash report (port_crashnotes.cpp,
// docs/modules/tools.md, "Crashes and traces").
#ifndef PORT_CRASHNOTES_H
#define PORT_CRASHNOTES_H

// A game begun and ended: what it is, kept as text for a report (game.cpp, main.cpp).
void PortCrashNotesGameStart(void);
void PortCrashNotesGameEnd(void);

// What the session is doing now, read when a report is written: the game's mode and frame and
// the front end's screen. Makes nothing, so a crash handler can call it.
int PortCrashNotesNow(char *buf, int cb);

#endif
