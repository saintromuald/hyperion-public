// port_lannet.h -- the LAN host's transport: framed TCP connections and
// the UDP query by which a joining Hyperion finds the games on its network. Storm's LAN
// provider (port_stormlan.cpp) speaks through it and never sees a socket, so the transport is
// a part that can be swapped (a relay in a host program, SpacetimeDB later).
//
// Everything here is polled from the game's thread: nothing blocks but Connect, which waits
// at most its timeout. A frame is a 32-bit length, a kind byte and its bytes.
#ifndef PORT_LANNET_H
#define PORT_LANNET_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace LanNet {

// The TCP port a host listens on and the UDP port it answers queries on, unless
// HYPERION_LAN_PORT names another (the UDP port is always the TCP port less one).
const int kGamePort = 6112;

bool Startup(void);

// Hosting: listens on the game port, and answers queries with the advert given.
// HYPERION_LAN_BIND=<address> binds to that address only (127.0.0.1 for a test on one machine).
bool Listen(void);
void SetAdvert(const std::vector<uint8_t> &advert);
int Accept(void);  // a new connection's id, or -1

// Joining: a connection to a host, or -1 after the timeout.
int Connect(uint32_t dwAddr, int nPort, int nTimeoutMs);

void Send(int nConn, uint8_t bKind, const void *pv, size_t cb);
// The next whole frame from a connection, if one has come. A connection that sends a frame
// larger than it may is dropped: one accepted here may send 64 KB a frame until Trust, one that
// Connect made 64 MB, the most (a map the room sends with a launch).
bool Receive(int nConn, uint8_t *pbKind, std::vector<uint8_t> *pData);
// An accepted connection whose greeting passed (the room's, the relay's, the lab's): its frames
// may be as large as any from then on.
void Trust(int nConn);
bool Alive(int nConn);
// Alive, and opened less than nMs ago: a connection that has said nothing yet is given that long
// for its first frame (the room's and the relay's greeting).
bool Greeted(int nConn, int nMs);
void Drop(int nConn);  // after what was sent to it has gone, as far as it can
void CloseAll(void);   // every connection, the listener and the query socket

// Writes what is waiting and reads what has come, on every connection; answers queries.
void Pump(void);

// Finding games: a query to the broadcast address, this machine and the hosts in
// HYPERION_LAN_HOSTS (a list of addresses), and the answers since.
struct Answer {
  uint32_t dwAddr;  // the host's address, in network order
  std::vector<uint8_t> advert;
};
void Query(void);
std::vector<Answer> Answers(void);

std::string AddrText(uint32_t dwAddr);
// An address as text, in network order; false when it is not one.
bool AddrParse(const char *psz, uint32_t *pdwAddr);
// The address this machine is reached at on its network: the one its route out goes from, or
// the loopback when it has none.
uint32_t LocalAddr(void);
uint32_t PeerAddr(int nConn);
// A connection this machine made to its own game port for another (port_lanweb.cpp's bridge
// for a browser): the end the game port accepts takes dwFor as its peer's address, so the
// browser is not taken for this machine.
void MarkBridge(int nConn, uint32_t dwFor);
bool IsLoopback(uint32_t dwAddr);
// This machine's name on the network.
std::string HostName(void);
// The game port (6112, or HYPERION_LAN_PORT's).
int GamePort(void);

// The room (port_lanroom.cpp) listens on more than the game port: the page the host serves.
// A raw listener's connections carry bytes, not frames (HTTP and the WebSocket on it).
int ListenOn(int nPort, bool bRaw);  // a listener's id, or -1
// A listener on 127.0.0.1 alone, at a port the system gives (*pnPort): the AI lab's, which its
// workers connect to.
int ListenLocal(int *pnPort);
int AcceptOn(int nListener);         // a new connection's id, or -1
void CloseListener(int nListener);
void CloseGameListener(void);        // Listen's, with the query socket
void SendRaw(int nConn, const void *pv, size_t cb);
// Everything a raw connection has had since the last call.
bool TakeRaw(int nConn, std::vector<uint8_t> *pIn);
// The bytes still waiting to go on a connection: a big file is sent as they drain.
size_t Pending(int nConn);
// Waits at most nMs for a connection or a listener to have something, or for a write to go.
void Wait(int nMs);

}  // namespace LanNet

#endif
