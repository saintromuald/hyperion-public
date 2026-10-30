// port_lanweb.h -- the page the LAN host serves: HTTP and a WebSocket on port 7117 of
// the room (port_lanroom.cpp). A phone's browser opens the page, and the page talks to the room
// over the WebSocket as a Hyperion does over its connection. port_lanweb.cpp says what it serves.
#ifndef PORT_LANWEB_H
#define PORT_LANWEB_H

#include <stdint.h>
#include <string>
#include <vector>

namespace LanWeb {

// Listens on 7117 (HYPERION_LAN_WEB_PORT for another); the page's files are web/lan beside
// Hyperion.exe. False when the port is taken.
bool Start(void);
void Stop(void);
int Port(void);

// Reads the requests that came, answers the files, and keeps the WebSockets.
void Pump(void);

// NOTE is a line for the room's log: how a bridge (/tcp) ended, and after how long.
enum EventKind { OPENED, TEXT, CLOSED, NOTE };
struct Event {
  EventKind kind;
  int nConn;
  std::string text;
};
// What the WebSockets did since the last call.
std::vector<Event> Events(void);

void SendText(int nConn, const std::string &text);
void SendBinary(int nConn, const void *pv, size_t cb);
void Close(int nConn);

// A file a path names, served from memory (an install's package, the game data): the room
// adds them as it has them. Empty data removes it.
void Serve(const std::string &path, const std::string &type, const std::vector<uint8_t> &data);
// A file on disk a path names, read as it is asked for; an empty file removes it.
void ServeFile(const std::string &path, const std::string &type, const std::string &file);
// The files under /files/ (the host's game files) go only to a page whose player is in the room:
// asked for as /files/<token>/<name>, with a token AllowFiles gave, until RevokeFiles.
void AllowFiles(const std::string &token);
void RevokeFiles(const std::string &token);
// Whether Hyperion for the browser is there to serve at /play/ (web/play beside Hyperion.exe,
// or HYPERION_LAN_PLAY_ROOT): the page that runs it and the program built by
// tools/build_engine_web.py.
bool PlayOn(void);

}  // namespace LanWeb

#endif
