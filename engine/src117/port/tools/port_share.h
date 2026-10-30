// port_share.h -- the text a player copies from the error window to ask for help, in a GitHub
// issue or on Discord (port_share.cpp): what failed or stopped, the build, the system and the
// last of what was logged, with none of the player's folders, network addresses or names in
// it, and short enough for one Discord message.
#ifndef PORT_SHARE_H
#define PORT_SHARE_H

#include <stddef.h>
#include <string>

struct PortJson;

// The longest a shared text is, its code block's marks among it: a Discord message's 2,000
// characters, less a line of the player's own.
const size_t PORT_SHARE_MAX = 1900;

// A start that failed or did not finish, from its start-up log (port_startup.cpp), under
// pszTitle.
std::string PortShareStartup(const char *pszTitle, const std::string &log);

// A crash, from its report.json (port_crash.cpp).
std::string PortShareCrash(const PortJson &report);

// The text as it is copied: a code block, so it keeps its lines where it is pasted.
std::string PortShareBlock(const std::string &text);

// Text with the folders of every path in it left out, the file it names kept, and with network
// addresses left out when bAddresses: how each line of a shared text is made.
std::string PortShareClean(const std::string &text, bool bAddresses);

#endif
