// port_lanlobby.h -- a LAN room's match made and joined; port_lanlobby.cpp says how.
#ifndef PORT_LANLOBBY_H
#define PORT_LANLOBBY_H

#include <stdint.h>

struct PortJson;  // port_json.h; a plain declaration, so lang/main.cpp can include this header

// The launch the room sent (port_lanclient.h): the game made or joined, as 1.17's Create Game
// or Join would, and its chat room next (PORT_FRONT_LAN). False, with a line in
// hyperion_lan.log, when it could not be.
bool PortLaunchNetGame(const PortJson &launch);
void PortLanLobbyEnd(void);

// A LAN room's match (port_lanlobby.cpp): 1.17's chat room played for its players,
// from the chat room's poll; a match under way; its end told to the room, after the game.
void PortLanLobbyTick(void);
bool PortLanLobbyActive(void);
void PortLanAfterGame(void);
// After the score screen: true once when the game was a room's match, so home comes next.
bool PortLanBackToFront(void);
// The chat room left (gluChat.cpp): true when it was a room's match's, so home comes next.
bool PortLanChatLeft(void);
// A room's match whose chat room is under the cover, unseen (port_lanlobby.cpp's start).
bool PortLanLobbyUnseen(void);
// A popup the chat room would show as it closes: true when the lobby is unseen, which keeps the
// line for the home screen (PortLanTakeWhy) in the popup's place; false, show it.
bool PortLanChatWhy(const char *pszWhy);
// That line, once, or NULL; it stays readable until the next call.
const char *PortLanTakeWhy(void);
// A button of the chat room pressed for its player: 7 Start Game, 8 Cancel (gluChat.cpp).
void PortChatPress(int nCmd);

// Each frame of a LAN game (port_stormlan.cpp's LanGameFrame): the room told how far it has got,
// and, when the host ends the match, the game left as Quit Mission leaves it.
void PortLanGameFrame(uint32_t dwFrame);

#endif
