// port_modcheck.h -- a mod's check, Hyperion --mod-check <id> [<report.txt>] (port_modcheck.cpp):
// its data loaded as start-up loads it, then every file it names decoded, with no game played.
#ifndef PORT_MODCHECK_H
#define PORT_MODCHECK_H

#include <string>
#include <vector>

enum ModCheckKind { MOD_CHECK_SOUND, MOD_CHECK_VIDEO, MOD_CHECK_PICTURE, MOD_CHECK_GRP };

// A file a mod's record names: where ("units trooper.icon"), the file on disk, and for a sound
// the name the game reads it by.
struct ModCheckFile {
  std::string where;
  std::string path;
  std::string game;
  ModCheckKind kind;
};

// What the mod's load found (port_moddata.cpp), once start-up has loaded it: false with why when
// its data did not load; what the load passed over, a line each; a line for each table it adds to
// or edits; and the files its records name that its folders have.
bool ModDataCheckResult(std::string *pError, std::vector<std::string> *pNotes, std::vector<std::string> *pTables,
                        std::vector<ModCheckFile> *pFiles);

// The check, once the game's files are open (RunGame): it writes the report and ends the process,
// with 0 when nothing is wrong, 1 when something is, and 2 when there is no such mod.
void PortModCheckRun(void);

#endif
