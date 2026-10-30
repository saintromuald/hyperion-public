// port_lannet.cpp -- the LAN host's transport; port_lannet.h says what it
// is. Plain sockets: Winsock on Windows, POSIX's elsewhere, so Linux and the Mac build it as it
// is. Presentation and transport only: nothing here reaches the simulation.
#if defined(_WIN32)
#define FD_SETSIZE 1024  // a room's connections, for Wait
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef intptr_t SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#endif
#if defined(__EMSCRIPTEN__)
extern "C" void __stdcall Sleep(unsigned int dwMs);  // port_posix.cpp's, which gives the page its turns
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <map>
#include <thread>
#include "port_lannet.h"

namespace {

struct Conn {
  SOCKET s = INVALID_SOCKET;
  std::vector<uint8_t> in, out;
  bool bDead = false;
  bool bClosing = false;  // dropped: closed once what waits to go has gone
  uint32_t dwPeer = 0;
  size_t outAt = 0;  // out's bytes before this have gone
  // the largest frame it may send (Trust), and what in holds at most: past it the socket is read
  // no further until in is taken, so a peer cannot make it grow
  size_t frameMost = 0;
  size_t inMost = 0;
  std::chrono::steady_clock::time_point opened = std::chrono::steady_clock::now();
};

std::map<int, Conn> g_conns;
int g_nNextConn = 1;
SOCKET g_listen = INVALID_SOCKET;
std::map<int, SOCKET> g_listeners;  // ListenOn's
int g_nNextListener = 1;
SOCKET g_query = INVALID_SOCKET;   // a host's: answers queries
SOCKET g_finder = INVALID_SOCKET;  // a joiner's: sends queries, hears answers
std::vector<uint8_t> g_advert;
std::vector<LanNet::Answer> g_answers;
bool g_bStarted;
// the bridges' connections to this machine, by their local port: whom each is for (MarkBridge)
std::map<int, uint32_t> g_bridges;

const uint8_t kQuery[4] = {'H', 'X', 'L', 'Q'};
const uint8_t kAnswer[4] = {'H', 'X', 'L', 'A'};
const size_t kFrameMax = 1 << 26;  // a map the room sends with a launch
// A connection accepted from another machine may send frames this large until it is trusted
// (its greeting passed): a greeting is a few hundred bytes, so 512 of them hold 32 MB at most.
const size_t kFrameFirst = 1 << 16;
const size_t kRawMost = 1 << 20;  // a raw connection's bytes not yet taken (an HTTP request, a bridge's)
// Connections held at once, well under FD_SETSIZE (1,024 on both systems): past it a new one is
// closed as it is accepted, so connections that say nothing cannot fill Wait's sets.
const size_t kConnsMost = 512;
std::map<int, bool> g_rawListeners;  // ListenOn's with bRaw

uint32_t BindAddr(void)
{
  const char *psz = getenv("HYPERION_LAN_BIND");
  in_addr a;
  if (psz != NULL && inet_pton(AF_INET, psz, &a) == 1)
    return a.s_addr;
  return htonl(INADDR_ANY);
}

void NonBlocking(SOCKET s)
{
#if defined(_WIN32)
  u_long nOne = 1;
  ioctlsocket(s, FIONBIO, &nOne);
#else
  fcntl(static_cast<int>(s), F_SETFL, fcntl(static_cast<int>(s), F_GETFL, 0) | O_NONBLOCK);
#endif
}

bool WouldBlock(void)
{
#if defined(_WIN32)
  const int n = WSAGetLastError();
  return n == WSAEWOULDBLOCK || n == WSAEINPROGRESS;
#else
  return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS || errno == EINTR;
#endif
}

void NoDelay(SOCKET s)
{
  int nOne = 1;  // a turn goes at once, not with the next one
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&nOne), sizeof nOne);
}

SOCKET Udp(int nPort, uint32_t dwAddr)
{
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET)
    return s;
  int nOne = 1;
  setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&nOne), sizeof nOne);
  sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(static_cast<unsigned short>(nPort));
  a.sin_addr.s_addr = dwAddr;
  if (bind(s, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0) {
    closesocket(s);
    return INVALID_SOCKET;
  }
  NonBlocking(s);
  return s;
}

SOCKET TcpListener(int nPort, uint32_t dwAddr)
{
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET)
    return s;
#if !defined(_WIN32)
  int nOne = 1;  // the port again at once after a game that ended (on Windows it would share it)
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&nOne), sizeof nOne);
#endif
  sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(static_cast<unsigned short>(nPort));
  a.sin_addr.s_addr = dwAddr;
  if (bind(s, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0 || listen(s, 16) != 0) {
    closesocket(s);
    return INVALID_SOCKET;
  }
  NonBlocking(s);
  return s;
}

int Adopt(SOCKET listener, bool bRaw)
{
  sockaddr_in a;
  socklen_t cb = sizeof a;
  SOCKET s = accept(listener, reinterpret_cast<sockaddr *>(&a), &cb);
  if (s == INVALID_SOCKET)
    return -1;
  if (g_conns.size() >= kConnsMost) {
    closesocket(s);
    return -1;
  }
  NonBlocking(s);
  NoDelay(s);
  const int n = g_nNextConn++;
  g_conns[n].s = s;
  g_conns[n].dwPeer = a.sin_addr.s_addr;
  g_conns[n].frameMost = bRaw ? 0 : kFrameFirst;
  g_conns[n].inMost = bRaw ? kRawMost : kFrameFirst + 5;
  // the room's own end of a browser's bridge (port_lanweb.cpp): the browser's address
  auto b = g_bridges.find(ntohs(a.sin_port));
  if (b != g_bridges.end() && (ntohl(a.sin_addr.s_addr) >> 24) == 127) {
    g_conns[n].dwPeer = b->second;
    g_bridges.erase(b);
  }
  return n;
}

void SendTo(SOCKET s, uint32_t dwAddr, int nPort, const void *pv, size_t cb)
{
  sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(static_cast<unsigned short>(nPort));
  a.sin_addr.s_addr = dwAddr;
  sendto(s, static_cast<const char *>(pv), static_cast<int>(cb), 0, reinterpret_cast<sockaddr *>(&a), sizeof a);
}

void Flush(Conn &c)
{
  while (c.outAt < c.out.size() && !c.bDead) {
    const int n = send(c.s, reinterpret_cast<const char *>(c.out.data() + c.outAt),
                       static_cast<int>(c.out.size() - c.outAt), 0);
    if (n > 0) {
      c.outAt += static_cast<size_t>(n);
    } else {
      if (n < 0 && !WouldBlock())
        c.bDead = true;
      break;
    }
  }
  // what has gone is let go of all at once, or a megabyte at a time while a file streams
  if (c.outAt == c.out.size()) {
    c.out.clear();
    c.outAt = 0;
  } else if (c.outAt > (1u << 20)) {
    c.out.erase(c.out.begin(), c.out.begin() + static_cast<std::ptrdiff_t>(c.outAt));
    c.outAt = 0;
  }
}

void Fill(Conn &c)
{
  uint8_t ab[16384];
  while (!c.bDead && c.in.size() < c.inMost) {
    const size_t cbRoom = c.inMost - c.in.size();
    const int n = recv(c.s, reinterpret_cast<char *>(ab), static_cast<int>(cbRoom < sizeof ab ? cbRoom : sizeof ab), 0);
    if (n > 0) {
      c.in.insert(c.in.end(), ab, ab + n);
    } else {
      if (n == 0 || !WouldBlock())
        c.bDead = true;
      break;
    }
  }
}

}  // namespace

namespace LanNet {

bool Startup(void)
{
  if (g_bStarted)
    return true;
#if defined(_WIN32)
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    return false;
#endif
  g_bStarted = true;
  return true;
}

int GamePort(void)
{
  const char *psz = getenv("HYPERION_LAN_PORT");
  const int n = psz != NULL ? atoi(psz) : 0;
  return n > 1 && n < 65536 ? n : kGamePort;
}

bool Listen(void)
{
  if (!Startup())
    return false;
  const uint32_t dwAddr = BindAddr();
  g_listen = TcpListener(GamePort(), dwAddr);
  if (g_listen == INVALID_SOCKET)
    return false;
  g_query = Udp(GamePort() - 1, dwAddr);  // a second host on one machine is found by address only
  return true;
}

void CloseGameListener(void)
{
  for (SOCKET *p : {&g_listen, &g_query})
    if (*p != INVALID_SOCKET) {
      closesocket(*p);
      *p = INVALID_SOCKET;
    }
  g_advert.clear();
}

int ListenOn(int nPort, bool bRaw)
{
  // a raw connection is read with TakeRaw; the socket is the same, its buffer larger
  if (!Startup())
    return -1;
  SOCKET s = TcpListener(nPort, BindAddr());
  if (s == INVALID_SOCKET)
    return -1;
  const int n = g_nNextListener++;
  g_listeners[n] = s;
  g_rawListeners[n] = bRaw;
  return n;
}

int ListenLocal(int *pnPort)
{
  if (!Startup())
    return -1;
  SOCKET s = TcpListener(0, htonl(INADDR_LOOPBACK));
  if (s == INVALID_SOCKET)
    return -1;
  sockaddr_in a;
  socklen_t cb = sizeof a;
  if (getsockname(s, reinterpret_cast<sockaddr *>(&a), &cb) != 0) {
    closesocket(s);
    return -1;
  }
  *pnPort = ntohs(a.sin_port);
  const int n = g_nNextListener++;
  g_listeners[n] = s;
  return n;
}

int AcceptOn(int nListener)
{
  auto it = g_listeners.find(nListener);
  return it == g_listeners.end() ? -1 : Adopt(it->second, g_rawListeners[nListener]);
}

void CloseListener(int nListener)
{
  auto it = g_listeners.find(nListener);
  if (it == g_listeners.end())
    return;
  closesocket(it->second);
  g_listeners.erase(it);
  g_rawListeners.erase(nListener);
}

void SetAdvert(const std::vector<uint8_t> &advert)
{
  g_advert = advert;
}

int Accept(void)
{
  return g_listen == INVALID_SOCKET ? -1 : Adopt(g_listen, false);
}

int Connect(uint32_t dwAddr, int nPort, int nTimeoutMs)
{
  if (!Startup())
    return -1;
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET)
    return -1;
#if !defined(_WIN32)
  if (s >= FD_SETSIZE) {  // the wait below could not hold it
    closesocket(s);
    return -1;
  }
#endif
  NonBlocking(s);
  NoDelay(s);
  sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(static_cast<unsigned short>(nPort));
  a.sin_addr.s_addr = dwAddr;
  if (connect(s, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0 && !WouldBlock()) {
    closesocket(s);
    return -1;
  }
  fd_set w, e;
  FD_ZERO(&w);
  FD_ZERO(&e);
  FD_SET(s, &w);
  FD_SET(s, &e);
  timeval t = {nTimeoutMs / 1000, (nTimeoutMs % 1000) * 1000};
#if defined(__EMSCRIPTEN__)
  // in a browser the connection is a WebSocket, which opens only while the page has its turn,
  // and select cannot wait: it is asked again between turns
  for (int nWaited = 0; nWaited < nTimeoutMs; nWaited += 10) {
    timeval none = {0, 0};
    fd_set w2 = w, e2 = e;
    if (select(static_cast<int>(s) + 1, NULL, &w2, &e2, &none) != 0)
      break;
    Sleep(10);
  }
  t = {0, 0};
#endif
  int nErr = 0;
  socklen_t cbErr = sizeof nErr;
  if (select(static_cast<int>(s) + 1, NULL, &w, &e, &t) <= 0 || FD_ISSET(s, &e) ||
      getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&nErr), &cbErr) != 0 || nErr != 0) {
    closesocket(s);
    return -1;
  }
  const int n = g_nNextConn++;
  g_conns[n].s = s;
  g_conns[n].dwPeer = dwAddr;
  // the end that joined: the room or relay it chose sends maps
  g_conns[n].frameMost = kFrameMax;
  g_conns[n].inMost = kFrameMax + 5;
  return n;
}

void Send(int nConn, uint8_t bKind, const void *pv, size_t cb)
{
  auto it = g_conns.find(nConn);
  if (it == g_conns.end() || it->second.bDead || it->second.bClosing)
    return;
  Conn &c = it->second;
  const uint32_t dwLen = static_cast<uint32_t>(cb + 1);
  const uint8_t abHead[5] = {static_cast<uint8_t>(dwLen), static_cast<uint8_t>(dwLen >> 8),
                             static_cast<uint8_t>(dwLen >> 16), static_cast<uint8_t>(dwLen >> 24), bKind};
  c.out.insert(c.out.end(), abHead, abHead + 5);
  c.out.insert(c.out.end(), static_cast<const uint8_t *>(pv), static_cast<const uint8_t *>(pv) + cb);
  Flush(c);
}

bool Receive(int nConn, uint8_t *pbKind, std::vector<uint8_t> *pData)
{
  auto it = g_conns.find(nConn);
  if (it == g_conns.end())
    return false;
  Conn &c = it->second;
  if (c.in.size() < 5)
    return false;
  const uint32_t dwLen = c.in[0] | (c.in[1] << 8) | (c.in[2] << 16) | (static_cast<uint32_t>(c.in[3]) << 24);
  if (dwLen == 0 || dwLen > c.frameMost) {
    c.bDead = true;  // not Hyperion's framing, or more than the peer may send yet
    return false;
  }
  if (c.in.size() < 4 + static_cast<size_t>(dwLen))
    return false;
  *pbKind = c.in[4];
  pData->assign(c.in.begin() + 5, c.in.begin() + 4 + dwLen);
  c.in.erase(c.in.begin(), c.in.begin() + 4 + dwLen);
  return true;
}

void SendRaw(int nConn, const void *pv, size_t cb)
{
  auto it = g_conns.find(nConn);
  if (it == g_conns.end() || it->second.bDead || it->second.bClosing)
    return;
  it->second.out.insert(it->second.out.end(), static_cast<const uint8_t *>(pv), static_cast<const uint8_t *>(pv) + cb);
  Flush(it->second);
}

bool TakeRaw(int nConn, std::vector<uint8_t> *pIn)
{
  auto it = g_conns.find(nConn);
  if (it == g_conns.end() || it->second.in.empty())
    return false;
  pIn->insert(pIn->end(), it->second.in.begin(), it->second.in.end());
  it->second.in.clear();
  return true;
}

size_t Pending(int nConn)
{
  auto it = g_conns.find(nConn);
  return it == g_conns.end() ? 0 : it->second.out.size() - it->second.outAt;
}

uint32_t PeerAddr(int nConn)
{
  auto it = g_conns.find(nConn);
  return it == g_conns.end() ? 0 : it->second.dwPeer;
}

void MarkBridge(int nConn, uint32_t dwFor)
{
  auto it = g_conns.find(nConn);
  if (it == g_conns.end())
    return;
  sockaddr_in a;
  socklen_t cb = sizeof a;
  if (getsockname(it->second.s, reinterpret_cast<sockaddr *>(&a), &cb) == 0)
    g_bridges[ntohs(a.sin_port)] = dwFor;
}

void Trust(int nConn)
{
  auto it = g_conns.find(nConn);
  if (it == g_conns.end() || it->second.frameMost == 0)
    return;
  it->second.frameMost = kFrameMax;
  it->second.inMost = kFrameMax + 5;
}

bool Alive(int nConn)
{
  auto it = g_conns.find(nConn);
  return it != g_conns.end() && !it->second.bDead && !it->second.bClosing;
}

bool Greeted(int nConn, int nMs)
{
  auto it = g_conns.find(nConn);
  if (it == g_conns.end() || it->second.bDead || it->second.bClosing)
    return false;
  return std::chrono::steady_clock::now() - it->second.opened < std::chrono::milliseconds(nMs);
}

void Drop(int nConn)
{
  auto it = g_conns.find(nConn);
  if (it == g_conns.end())
    return;
  Flush(it->second);
  it->second.bClosing = true;
  if (it->second.out.empty() || it->second.bDead) {
    closesocket(it->second.s);
    g_conns.erase(it);
  }
}

void CloseAll(void)
{
  for (auto &kv : g_conns) {
    Flush(kv.second);
    closesocket(kv.second.s);
  }
  g_conns.clear();
  for (auto &kv : g_listeners)
    closesocket(kv.second);
  g_listeners.clear();
  g_rawListeners.clear();
  for (SOCKET *p : {&g_listen, &g_query, &g_finder})
    if (*p != INVALID_SOCKET) {
      closesocket(*p);
      *p = INVALID_SOCKET;
    }
  g_advert.clear();
  g_answers.clear();
}

void Pump(void)
{
  for (auto it = g_conns.begin(); it != g_conns.end();) {
    Conn &c = it->second;
    Flush(c);
    if (c.bClosing) {
      if (c.out.empty() || c.bDead) {
        closesocket(c.s);
        it = g_conns.erase(it);
        continue;
      }
    } else {
      Fill(c);
    }
    ++it;
  }
  // a host answers the queries that came
  if (g_query != INVALID_SOCKET) {
    uint8_t ab[64];
    sockaddr_in from;
    socklen_t cb = sizeof from;
    int n;
    while ((n = recvfrom(g_query, reinterpret_cast<char *>(ab), sizeof ab, 0, reinterpret_cast<sockaddr *>(&from), &cb)) > 0) {
      if (n >= 4 && memcmp(ab, kQuery, 4) == 0 && !g_advert.empty()) {
        std::vector<uint8_t> out(kAnswer, kAnswer + 4);
        out.insert(out.end(), g_advert.begin(), g_advert.end());
        sendto(g_query, reinterpret_cast<const char *>(out.data()), static_cast<int>(out.size()), 0,
               reinterpret_cast<sockaddr *>(&from), cb);
      }
      cb = sizeof from;
    }
  }
  // a joiner hears the answers
  if (g_finder != INVALID_SOCKET) {
    uint8_t ab[2048];
    sockaddr_in from;
    socklen_t cb = sizeof from;
    int n;
    while ((n = recvfrom(g_finder, reinterpret_cast<char *>(ab), sizeof ab, 0, reinterpret_cast<sockaddr *>(&from), &cb)) > 0) {
      if (n > 4 && memcmp(ab, kAnswer, 4) == 0) {
        Answer a;
        a.dwAddr = from.sin_addr.s_addr;
        a.advert.assign(ab + 4, ab + n);
        g_answers.push_back(a);
      }
      cb = sizeof from;
    }
  }
}

void Query(void)
{
#if defined(__EMSCRIPTEN__)
  return;  // a browser has no UDP: its Hyperion joins the room the page came from
#endif
  if (!Startup())
    return;
  if (g_finder == INVALID_SOCKET)
    g_finder = Udp(0, htonl(INADDR_ANY));
  if (g_finder == INVALID_SOCKET)
    return;
  const int nPort = GamePort() - 1;
  SendTo(g_finder, htonl(INADDR_BROADCAST), nPort, kQuery, 4);
  SendTo(g_finder, htonl(INADDR_LOOPBACK), nPort, kQuery, 4);
  // hosts the broadcast does not reach: another subnet, or a network that drops broadcasts
  if (const char *psz = getenv("HYPERION_LAN_HOSTS")) {
    std::string all = psz;
    size_t at = 0;
    while (at <= all.size()) {
      size_t end = all.find_first_of(",; ", at);
      if (end == std::string::npos)
        end = all.size();
      const std::string one = all.substr(at, end - at);
      in_addr a;
      if (!one.empty() && inet_pton(AF_INET, one.c_str(), &a) == 1)
        SendTo(g_finder, a.s_addr, nPort, kQuery, 4);
      at = end + 1;
    }
  }
}

std::vector<Answer> Answers(void)
{
  std::vector<Answer> out;
  out.swap(g_answers);
  return out;
}

std::string AddrText(uint32_t dwAddr)
{
  in_addr a;
  a.s_addr = dwAddr;
  char sz[32];
  return inet_ntop(AF_INET, &a, sz, sizeof sz) != NULL ? std::string(sz) : std::string("?");
}

bool AddrParse(const char *psz, uint32_t *pdwAddr)
{
  in_addr a;
  if (psz == NULL || inet_pton(AF_INET, psz, &a) != 1)
    return false;
  *pdwAddr = a.s_addr;
  return true;
}

// A datagram socket "connected" to an address past this machine sends nothing, and is given
// the address its route goes from.
uint32_t LocalAddr(void)
{
  if (!Startup())
    return htonl(INADDR_LOOPBACK);
  const uint32_t dwBind = BindAddr();
  if (dwBind != htonl(INADDR_ANY))
    return dwBind;
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  uint32_t dwAddr = htonl(INADDR_LOOPBACK);
  if (s == INVALID_SOCKET)
    return dwAddr;
  sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(53);
  a.sin_addr.s_addr = htonl(0x0A000001);  // 10.0.0.1
  if (connect(s, reinterpret_cast<sockaddr *>(&a), sizeof a) == 0) {
    sockaddr_in me;
    socklen_t cb = sizeof me;
    if (getsockname(s, reinterpret_cast<sockaddr *>(&me), &cb) == 0 && me.sin_addr.s_addr != 0)
      dwAddr = me.sin_addr.s_addr;
  }
  closesocket(s);
  return dwAddr;
}

bool IsLoopback(uint32_t dwAddr)
{
  return (ntohl(dwAddr) >> 24) == 127;
}

std::string HostName(void)
{
  char sz[256] = "";
  if (!Startup() || gethostname(sz, sizeof sz) != 0)
    sz[0] = '\0';
  return sz;
}

void Wait(int nMs)
{
  fd_set r, w;
  FD_ZERO(&r);
  FD_ZERO(&w);
  SOCKET nMax = 0;
  int nSockets = 0;
  auto add = [&](SOCKET s, fd_set *p) {
    if (s == INVALID_SOCKET)
      return;
#if !defined(_WIN32)
    if (s >= FD_SETSIZE)  // past the set's bits: FD_SET would write beyond it
      return;
#endif
    FD_SET(s, p);
    ++nSockets;
    if (s > nMax)
      nMax = s;
  };
  for (const auto &kv : g_conns)
    if (!kv.second.bDead) {
      add(kv.second.s, &r);
      if (!kv.second.out.empty())
        add(kv.second.s, &w);
    }
  for (const auto &kv : g_listeners)
    add(kv.second, &r);
  for (SOCKET s : {g_listen, g_query, g_finder})
    add(s, &r);
#if defined(__EMSCRIPTEN__)
  // a browser's select cannot wait, and the page's turns are what bring its bytes
  timeval none = {0, 0};
  if (nSockets == 0 || select(static_cast<int>(nMax) + 1, &r, &w, NULL, &none) == 0)
    Sleep(static_cast<unsigned int>(nMs));
  return;
#endif
  if (nSockets == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(nMs));
    return;
  }
  timeval t = {nMs / 1000, (nMs % 1000) * 1000};
  select(static_cast<int>(nMax) + 1, &r, &w, NULL, &t);
}

}  // namespace LanNet
