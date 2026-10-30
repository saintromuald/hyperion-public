// port_crashnotes.cpp -- the session's part of the crash report (port_crashnotes.h): the game
// being played and the games played before it, kept as text when a game starts and ends, so the
// report only copies it (PortCrashSession), and what it reads of the game as it is written.
#include "port_crashnotes.h"
#include <stdio.h>
#include <string>
#include "port_crash.h"               // PortCrashSession
#include "oc_data.h"                  // OC_VA, where 1.17's data is
#include "../../gen/globals117.h"     // gwGameMode, gwMapWidth, gwMapHeight, g_LocalNationID
#include "../platform/port_globals117.h"  // oc_dwReplayFrame
#include "../platform/port_discord.h"     // PortDiscordGameText
#include "../data/port_mods.h"            // PortMods, PortModActive
#include "../front/port_front.h"          // PortFrontScreen
#include "../gameplay/port_mutators.h"    // MutatorsRevisionOf
#include "../gameplay/port_session.h"     // Session

namespace {

const int PLAYED_KEPT = 5;  // the games played before, newest last
std::string g_game;
std::string g_played[PLAYED_KEPT];
int g_nPlayed;

}  // namespace

void PortCrashNotesGameStart(void)
{
  const PortMod &mod = PortMods()[PortModActive()];
  char sz[768];
  snprintf(sz, sizeof sz, "%s; map %ux%u tiles; local player %lu; mutators' revision %d; mutators %s; mod %s %s",
           PortDiscordGameText().c_str(), static_cast<unsigned>(gwMapWidth), static_cast<unsigned>(gwMapHeight),
           static_cast<unsigned long>(g_LocalNationID), MutatorsRevisionOf(),
           Session().bMutators ? "chosen" : "none", mod.name.c_str(), mod.version.c_str());
  g_game = sz;
  PortCrashSession("game", sz);
}

void PortCrashNotesGameEnd(void)
{
  if (g_game.empty())
    return;
  char sz[64];
  snprintf(sz, sizeof sz, ", ended at frame %u", oc_dwReplayFrame);
  g_played[g_nPlayed++ % PLAYED_KEPT] = g_game + sz;
  std::string played;
  for (int k = g_nPlayed > PLAYED_KEPT ? g_nPlayed - PLAYED_KEPT : 0; k < g_nPlayed; k++)
    played += "\r\n    " + std::to_string(k + 1) + ". " + g_played[k % PLAYED_KEPT];
  PortCrashSession("games played", played.c_str());
  PortCrashSession("game", "none; in the menus");
  g_game.clear();
}

int PortCrashNotesNow(char *buf, int cb)
{
  // the front end's screen is the last one shown, so only out of a game (mode 3)
  const char *pszScreen = PortFrontScreen();
  const int n = gwGameMode == 3
                    ? snprintf(buf, static_cast<size_t>(cb), "in a game, frame %u", oc_dwReplayFrame)
                    : snprintf(buf, static_cast<size_t>(cb), "game mode %u, the front end's screen %s",
                               static_cast<unsigned>(gwGameMode),
                               pszScreen != NULL && pszScreen[0] != 0 ? pszScreen : "none");
  return n < 0 ? 0 : n >= cb ? cb - 1 : n;
}
