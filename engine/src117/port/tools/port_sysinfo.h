// port_sysinfo.h -- what the program is and what it runs on, for the start-up log and the crash
// report (port_sysinfo.cpp, docs/modules/tools.md, "The start-up log").
#ifndef PORT_SYSINFO_H
#define PORT_SYSINFO_H

#include <stddef.h>
#include <string>

// The facts are named in parts: "build", "system", "run", then those learned later, such as
// "install", "display 1" and "presenter". Gather fills the first three once, and asks SDL for its
// version when bSdl, which is false when SDL3.dll could not be loaded.
void PortSysInfoGather(bool bSdl = true);

// Sets a fact, replacing one of the same part and name.
void PortSysInfoSet(const char *pszPart, const char *pszName, const char *pszFormat, ...);

// Every fact, a line each ("part  name: value"), and the same as a JSON object of parts.
std::string PortSysInfoText(void);
std::string PortSysInfoJson(void);

// The text and JSON as of the last change, kept in buffers of their own so the crash report
// can write them without making anything.
const char *PortSysInfoTextNow(void);
const char *PortSysInfoJsonNow(void);

// Text with the player's own folder written %USERPROFILE% (~ off Windows), so a log names no
// account, and the values of the options that carry a key, a password or a token as "(hidden)".
std::string PortSysInfoMask(const char *psz);

// A string as a JSON string, with its quotes.
std::string PortSysInfoJsonString(const char *psz);

#endif
