// port_discord.h -- Hyperion in Discord's "Playing" (port_discord.cpp).
#ifndef PORT_DISCORD_H
#define PORT_DISCORD_H

#include <string>

// Each pass of a loop, the front end's and the game's (beside PortLabFarmPump): at most once a
// second, what the player is doing is described and handed to the thread that tells the Discord
// app on this computer. Nothing is sent with Settings' "Show in Discord" off, and nothing at all
// in the browser's build or in a role with no window (a LAN room, a lab worker, a replay check).
void PortDiscordPump(void);
// The line in Discord cleared and the pipe closed, a second at the most. An exit needs none:
// Discord clears the line itself when the program's connection closes.
void PortDiscordStop(void);
// The game running, as the presence says it (a skirmish on a map, 1 vs 3 computers), for the
// crash report's notes (port_crashnotes.cpp); on the main thread. Empty in a browser.
std::string PortDiscordGameText(void);

#endif
