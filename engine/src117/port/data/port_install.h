// port_install.h -- which StarCraft install the game's files come from, and the first start's
// choice of it (port_install.cpp, port_install_window.cpp).

#ifndef PORT_INSTALL_H
#define PORT_INSTALL_H

#include <stddef.h>
#include <string>

// The kinds of install, as settings.json's "data" names them: "remastered", StarCraft from
// Battle.net (the free StarCraft, whose files are Remastered's storage), or "mpq", a classic
// StarCraft folder with its MPQs.
enum PortInstallKind {
  PORT_INSTALL_UNSET,
  PORT_INSTALL_BATTLENET,
  PORT_INSTALL_CLASSIC,
};

// The kind settings.json keeps, PORT_INSTALL_UNSET without one.
PortInstallKind PortInstallChosen(void);

// This run's kind from the command line, PORT_INSTALL_UNSET without one: --install classic (or
// mpq) or remastered (or battlenet), else the kind --install-folder's folder holds. It comes
// before the environment and settings.json, and is not kept.
PortInstallKind PortInstallCommandLine(void);

// --install-folder's folder, with its separator, when the command line gives a classic StarCraft
// folder: the game's folder for this run.
bool PortInstallCommandFolder(char *pszOut, size_t cchOut);

// Keeps the kind in settings.json, and its folder when one is given: "remastered" for StarCraft
// from Battle.net, "classic" for a classic StarCraft folder.
void PortInstallChoose(PortInstallKind kind, const char *pszFolder);

// Whether a folder is a classic StarCraft folder: StarDat.mpq is in it.
bool PortInstallClassicCheck(const char *pszFolder);

// The classic StarCraft folder settings.json keeps ("classic"), with its separator, when it is
// one and this run reads classic files (StarCraft from Battle.net is not the kind, on the command
// line or in settings.json).
bool PortInstallClassicFolder(char *pszOut, size_t cchOut);

// Whether the game's files are there for this run's kind, or for either kind when none is:
// StarDat.mpq in the game's folder, or an install of StarCraft from Battle.net. It decides
// nothing (PortGameSource decides once), so it can be asked again after the first start's
// choice.
bool PortInstallFound(const char *pszGameFolder);

// Whether this start is one a player made, which may ask: no replay, map, saved game, canvas,
// benchmark, lab worker, seat of a LAN room or install on the command line, no hidden window, and
// no source, install or game folder given by the environment (HYPERION_DATA_SOURCE,
// HYPERION_REMASTERED, HYPERION_GAME). Asked before the start sets HYPERION_GAME.
bool PortInstallMayAsk(void);

// The first start's choice: the folder of each install the player has, checked, and which is the
// base, kept in settings.json. On Windows a window of Windows' own controls (hyperion.rc),
// elsewhere the one Hyperion draws (PortInstallWindowRun). NO_WINDOW when the system shows none
// (no display), and the start says what is missing instead.
enum PortInstallAnswer { PORT_INSTALL_ANSWER_QUIT, PORT_INSTALL_ANSWER_CHOSEN, PORT_INSTALL_ANSWER_NO_WINDOW };
PortInstallAnswer PortInstallFirstStart(void);

// A folder checked for a kind of install, as the first start's windows show it: whether the
// game's files are there, the line that says so, and the folder to keep (the install's own
// when one inside it was given).
struct PortInstallCheck {
  bool bFound;
  std::string text;
  std::string folder;
};
PortInstallCheck PortInstallCheckFolder(PortInstallKind kind, const char *pszFolder);

// Keeps the choice and, for StarCraft from Battle.net, opens its storage: false, with the line
// that says why, when it will not open.
bool PortInstallKeep(PortInstallKind kind, const PortInstallCheck &check, std::string *pWhy);

// The first start's windows link both installs and ask which is the base, the one the game reads.
// The folder a kind's field starts with: the one settings.json keeps, else where it usually is
// (where Windows says the Battle.net app installed StarCraft, or its usual folder; a classic
// folder beside Hyperion); empty when none.
std::string PortInstallStartFolder(PortInstallKind kind);
// The base as the windows show it: the one chosen while its files are there, else the other kind
// when its are.
PortInstallKind PortInstallBaseFor(PortInstallKind chosen, bool bBattleNet, bool bClassic);
// Keeps both: the other kind's folder when its files are there, linked to switch to later, then
// the base as PortInstallKeep keeps it.
bool PortInstallKeepBoth(PortInstallKind base, const PortInstallCheck &battleNet, const PortInstallCheck &classic,
                         std::string *pWhy);

// Where StarCraft from Battle.net usually is on this system, when it is there; NULL otherwise.
const char *PortInstallBattleNetFolder(void);

// The first start's window as Hyperion draws it, with SDL (port_install_window.cpp): on Linux
// and a Mac, and on Windows with HYPERION_INSTALL_WINDOW=drawn.
PortInstallAnswer PortInstallWindowRun(void);

#endif  // PORT_INSTALL_H
