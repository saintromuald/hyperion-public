// port_errorwin.h -- the window a start that failed or a crash shows (port_errorwin.cpp).
#ifndef PORT_ERRORWIN_H
#define PORT_ERRORWIN_H

#include <string>

// The window: its title, what happened, and the text a player copies to ask for help
// (port_share.cpp) in a box with Copy to clipboard, which copies it as a code block, and Open the
// folder, which shows pszOpen in its folder (none when NULL). Windows' own dialog, or off Windows
// and with HYPERION_ERROR_WINDOW=drawn a window Hyperion draws. Returns once it is closed. Off
// Windows the text also goes to the standard error, and with no window to show it is all there is.
void PortErrorWindow(const char *pszTitle, const std::string &what, const std::string &share, const char *pszOpen);

// --crash-report <folder>: the window for the crash whose report is in that folder, in a copy of
// Hyperion the one that crashed starts for it, since a process that has crashed cannot safely
// show one (port_crash.cpp). Returns the program's exit code.
int PortErrorCrashReport(const char *pszFolder);

#endif
