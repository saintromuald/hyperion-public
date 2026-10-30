// port_control.h -- the live channel:
// a WebSocket on 127.0.0.1 through which a tool or a script reads and
// changes what Hyperion shows while it runs. Off unless --control[=<port>] is given or
// Debug Tools' canvas starts; the port is 27117 unless given. port_control.cpp has the
// messages.
#ifndef PORT_CONTROL_H
#define PORT_CONTROL_H

#include <string>

enum { PORT_CONTROL_PORT = 27117 };

// Starts listening, once; nPort 0 takes the default. The channel's thread only moves
// bytes: every message is handled on the game's thread by PortControlPoll, which the
// presenter calls each frame it shows (port_present.cpp) and the front end each of its
// frames (port_front.cpp).
void PortControlStart(int nPort);
bool PortControlRunning(void);
void PortControlPoll(void);

// The values a tool can change while the game runs, each by name ("gamma",
// "bloom.strength"), with its range, its default and a line of help. A value is a float
// the caller owns (pf), or a pair of functions when it lives elsewhere. Registered once,
// at start or when the effect that owns it is made; a name registered again is replaced.
// fStep is how far one notch moves it: 1 for a switch (0 off, 1 on), 0 for a value that
// slides freely.
typedef float (*PFN_CONTROL_GET)(void);
typedef void (*PFN_CONTROL_SET)(float f);
void PortControlFloat(const char *pszName, float *pf, float fMin, float fMax, const char *pszHelp,
                      float fStep = 0.0f);
void PortControlValue(const char *pszName, PFN_CONTROL_GET pfnGet, PFN_CONTROL_SET pfnSet, float fMin, float fMax,
                      float fDefault, const char *pszHelp, float fStep = 0.0f);

// The front end's part: whether a snapshot is wanted of the frame it is about to show
// (its path), and how it went; and whether a tool asked for the canvas (its tileset).
bool PortControlFrontSnap(std::string *pPath);
void PortControlFrontSnapped(bool bOk, int nWidth, int nHeight);
bool PortControlFrontCanvas(std::string *pTileset);

// The game marked as changed by a sandbox command (status reads it): a spawn, an order, a
// cutscene's steps that act on the game (port_trigrun.cpp, port_cutscene.cpp).
void PortControlChanged(void);
// The front end could not start the map the channel asked for (port_front.cpp): a map_push or
// map_load waiting on it ends, and status says why.
void PortControlLaunchFailed(const char *pszWhy);

// The port the channel listens on, 0 before it starts.
int PortControlPort(void);
// The channel's key, made at each start: a client sends it as the t= of its WebSocket's URL. It is
// written to PortControlKeyFile(port), which only this user can read, for tools to find; empty
// before the channel starts. A page the channel serves is given it in its link (Debug Tools'
// Copy link).
const std::string &PortControlKey(void);
std::string PortControlKeyFile(int nPort);

// A command another part of the program answers (the AI lab's lab_ commands): the
// message's JSON in, the answer's fields out ("\"name\":value,..." without the braces), or false
// and why. Added once; the same name again replaces it.
struct PortJson;
typedef bool (*PFN_CONTROL_COMMAND)(const PortJson &j, std::string *pAnswer);
void PortControlCommand(const char *pszName, PFN_CONTROL_COMMAND pfn);
// Published messages (the AI lab's stream): a client's subscribe {topics} has every
// message on those topics sent to it, as {"topic": ..., "data": ...}, until it closes; a client
// more than 8 MB behind misses them until it catches up. PortControlSubscribed: whether any
// client takes a topic, so a publisher makes nothing no one reads.
bool PortControlSubscribed(const char *pszTopic);
void PortControlPublish(const char *pszTopic, const std::string &json);
// A map's file as map_load finds it: a full path, a name the game finds, or a file name in any
// folder of the maps Hyperion lists.
bool PortControlMapPath(const std::string &name, std::string *pPath);
// What Debug Tools' overlay copies for an AI assistant, and /ai-context serves: the mod's
// ai-context.md with its {{...}} filled in (the port, the folders, the files there are to read,
// the sample scenes). With bState it includes the game's status, which only the main thread
// may read.
std::string PortControlAiContext(bool bState);

// Whether a single-player game keeps running while the window is not the active one:
// while the live channel is open (port_control.cpp), so a tool or the panel can be used
// beside it; while --record or a benchmark plays; and while Settings' Run in the background is
// on (PortClockBackgroundOn). 1.17 stops the game otherwise (GetNextMessage, run_game_loop).
bool PortControlKeepsRunning(void);
// Whether the channel's step asked for its frames back to back, not at the game's speed
// (step with fast, for pathbench): the clock paces frames and is not part of the game.
bool PortControlFast(void);
// After each game frame: a step that has reached its frame pauses the game there, and the
// channel's next poll answers it. Without it the step ran on until a frame was drawn.
void PortControlFrameRan(void);

// A click or key of the player's in the window (WndProc). The channel's pause has no dialog
// to take it back from, as 1.17's pauses all have, and a paused game takes no other input,
// so the player's own input ends it: the game runs on and the input goes through.
void PortControlPlayerInput(void);

#endif
