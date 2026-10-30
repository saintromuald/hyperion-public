// port_moddata_push.h -- a mod's data pushed over the live channel, and what was pushed
// (port_moddata.cpp). Apart from port_moddata.h, which the game's files include without
// <string>.
#ifndef PORT_MODDATA_PUSH_H
#define PORT_MODDATA_PUSH_H

#include <string>

// Pushes: in a Debug Tools canvas game, a mod's data sent again while it runs, as a
// new submission beside the earlier ones: types.json and iscript.txt from pszFolder, or the
// texts given, and the art from pszFolder. Its records take new ids, so the units on the map
// keep theirs, and names find the newest. True with the submission in *pAnswer (JSON
// members), or false with why. A new game starts from the data loaded at start-up.
bool ModDataPush(const char *pszFolder, const char *pszMod, const std::string *pTypes, const std::string *pScript,
                 std::string *pAnswer);
std::string ModDataSubmissionsJson();

#endif
