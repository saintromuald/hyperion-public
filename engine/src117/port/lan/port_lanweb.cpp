// port_lanweb.cpp -- the page the LAN host serves; port_lanweb.h says what it is for.
//
// A small HTTP/1.1 server over port_lannet.cpp's raw connections: GET and HEAD of the page's
// files (web/lan beside Hyperion.exe, or HYPERION_LAN_WEB_ROOT), of Hyperion for the browser at
// /play/ (web/play; or HYPERION_LAN_PLAY_ROOT), of the files the room serves
// from memory or disk (Serve, ServeFile: the packages and the game data), with Range for a
// download that stops and goes on, and /ws upgraded to a WebSocket (RFC 6455), which carries
// the room's JSON. /tcp/<the game port> is a WebSocket whose binary messages are the bytes
// of a connection to the room's game port, as Emscripten carries a Hyperion's TCP in a
// browser. Everything is polled from the room's loop; a big file goes out as
// the connection drains. Nothing here reaches the simulation.
#include "port_lanweb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include "port_lannet.h"
#include "../platform/port_util.h"  // PortNowMs
#include "../platform/port_paths.h"  // PortPackagedFile
#include "../platform/port_posix.h"  // PortNativePath
#if defined(_WIN32)
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace {

struct Conn {
  std::vector<uint8_t> in;
  bool bWs = false;
  bool bClosing = false;
  int nPeer = -1;  // a bridge's connection to the room's game port (/tcp)
  uint64_t qwOpened = 0;  // a bridge's: when it opened, and its bytes each way
  uint64_t qwToGame = 0, qwToBrowser = 0;
  std::vector<uint8_t> message;  // a WebSocket message in fragments
  uint8_t bMessageOp = 0;
  // a file going out
  FILE *pFile = NULL;
  // a served buffer, held while it goes out: a settings change may replace its path meanwhile
  std::shared_ptr<const std::vector<uint8_t>> pData;
  uint64_t qwAt = 0, qwEnd = 0;
  bool bKeepAlive = true;
};

struct Served {
  std::string type;
  std::shared_ptr<std::vector<uint8_t>> data;
  std::string file;
};

int g_nListener = -1;
int g_nPort = 0;
std::string g_root;
std::string g_play;  // Hyperion for the browser, served at /play/
std::map<int, Conn> g_conns;
std::map<std::string, Served> g_served;
std::vector<LanWeb::Event> g_events;
std::set<std::string> g_filesTokens;  // AllowFiles'

const size_t kHeadMax = 16384;
const size_t kMessageMax = 1 << 20;
const size_t kChunk = 256 * 1024;

// ---------------------------------------------------------------------------
// SHA-1 and base64, for the WebSocket's handshake.

struct Sha1 {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  uint8_t block[64];
  size_t nBlock = 0;
  uint64_t qwBits = 0;
  static uint32_t Rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
  void Compress(void)
  {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
      w[i] = static_cast<uint32_t>(block[i * 4]) << 24 | block[i * 4 + 1] << 16 | block[i * 4 + 2] << 8 | block[i * 4 + 3];
    for (int i = 16; i < 80; ++i)
      w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) {
        f = (b & c) | (~b & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      const uint32_t t = Rol(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = Rol(b, 30);
      b = a;
      a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }
  void Add(const void *pv, size_t cb)
  {
    const uint8_t *p = static_cast<const uint8_t *>(pv);
    qwBits += static_cast<uint64_t>(cb) * 8;
    while (cb--) {
      block[nBlock++] = *p++;
      if (nBlock == 64) {
        Compress();
        nBlock = 0;
      }
    }
  }
  void Finish(uint8_t out[20])
  {
    const uint64_t qwLen = qwBits;
    const uint8_t b80 = 0x80, b0 = 0;
    Add(&b80, 1);
    while (nBlock != 56)
      Add(&b0, 1);
    for (int i = 7; i >= 0; --i)
      block[nBlock++] = static_cast<uint8_t>(qwLen >> (i * 8));
    Compress();
    for (int i = 0; i < 20; ++i)
      out[i] = static_cast<uint8_t>(h[i / 4] >> (24 - (i % 4) * 8));
  }
};

std::string Base64(const uint8_t *p, size_t cb)
{
  static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < cb; i += 3) {
    const uint32_t v = static_cast<uint32_t>(p[i]) << 16 | (i + 1 < cb ? p[i + 1] << 8 : 0) | (i + 2 < cb ? p[i + 2] : 0);
    out += kAlphabet[(v >> 18) & 63];
    out += kAlphabet[(v >> 12) & 63];
    out += i + 1 < cb ? kAlphabet[(v >> 6) & 63] : '=';
    out += i + 2 < cb ? kAlphabet[v & 63] : '=';
  }
  return out;
}

// ---------------------------------------------------------------------------

// A folder of the package's (web\lan, web\play) where PortPackagedFile finds it, as the system
// names it
std::string PackagedFolder(const char *pszRel)
{
  return PortNativePath(PortPackagedFile(pszRel));
}

std::string TypeOf(const std::string &path)
{
  const size_t n = path.find_last_of('.');
  const std::string ext = n == std::string::npos ? "" : path.substr(n + 1);
  if (ext == "html")
    return "text/html; charset=utf-8";
  if (ext == "js")
    return "text/javascript; charset=utf-8";
  if (ext == "css")
    return "text/css; charset=utf-8";
  if (ext == "json")
    return "application/json";
  if (ext == "svg")
    return "image/svg+xml";
  if (ext == "png")
    return "image/png";
  if (ext == "ico")
    return "image/x-icon";
  if (ext == "woff2")
    return "font/woff2";
  if (ext == "wasm")
    return "application/wasm";
  if (ext == "zip")
    return "application/zip";
  if (ext == "gz")
    return "application/gzip";
  return "application/octet-stream";
}

std::string Header(const std::string &head, const char *pszName)
{
  const std::string want = std::string("\r\n") + pszName + ":";
  std::string lower = head;
  for (char &c : lower)
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  std::string wantLower = want;
  for (char &c : wantLower)
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  const size_t n = lower.find(wantLower);
  if (n == std::string::npos)
    return std::string();
  size_t a = n + want.size();
  while (a < head.size() && head[a] == ' ')
    ++a;
  const size_t b = head.find("\r\n", a);
  return head.substr(a, b == std::string::npos ? std::string::npos : b - a);
}

void Reply(int nConn, int nCode, const char *pszReason, const std::string &type, const std::string &body,
           const std::string &extra = std::string())
{
  std::string s = "HTTP/1.1 " + std::to_string(nCode) + " " + pszReason + "\r\nContent-Type: " + type +
                  "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nCache-Control: no-cache\r\n" + extra + "\r\n" + body;
  LanNet::SendRaw(nConn, s.data(), s.size());
}

// A file out: its head now, its bytes as the connection drains.
void Send(int nConn, Conn &c, uint64_t qwSize, const std::string &type, const std::string &range, bool bHead)
{
  uint64_t qwFrom = 0, qwTo = qwSize ? qwSize - 1 : 0;
  bool bPartial = false;
  if (range.compare(0, 6, "bytes=") == 0 && qwSize) {
    const char *p = range.c_str() + 6;
    char *pEnd;
    const unsigned long long a = strtoull(p, &pEnd, 10);
    if (*pEnd == '-') {
      const unsigned long long b = pEnd[1] ? strtoull(pEnd + 1, NULL, 10) : qwSize - 1;
      if (a <= b && a < qwSize) {
        qwFrom = a;
        qwTo = b < qwSize ? b : qwSize - 1;
        bPartial = true;
      }
    }
  }
  const uint64_t qwLen = qwSize ? qwTo - qwFrom + 1 : 0;
  std::string s = std::string("HTTP/1.1 ") + (bPartial ? "206 Partial Content" : "200 OK") + "\r\nContent-Type: " + type +
                  "\r\nContent-Length: " + std::to_string(qwLen) + "\r\nAccept-Ranges: bytes\r\nCache-Control: no-cache\r\n";
  if (bPartial)
    s += "Content-Range: bytes " + std::to_string(qwFrom) + "-" + std::to_string(qwTo) + "/" + std::to_string(qwSize) + "\r\n";
  s += "\r\n";
  LanNet::SendRaw(nConn, s.data(), s.size());
  c.qwAt = qwFrom;
  c.qwEnd = bHead ? qwFrom : qwFrom + qwLen;
}

// Whether a WebSocket may open: from a page this server served, its Origin the Host it asked
// for, and that Host this machine by its address or name, which a name rebound to it is not; or
// from a program, which sends no Origin. Another site's page in a player's browser cannot.
bool OriginOk(const std::string &head)
{
  const std::string origin = Header(head, "Origin");
  if (origin.empty())
    return true;
  const std::string host = Header(head, "Host");
  if (host.empty() || origin != "http://" + host)
    return false;
  std::string name = host.substr(0, host.rfind(':') == std::string::npos ? host.size() : host.rfind(':'));
  for (char &ch : name)
    ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
  uint32_t dwAddr;
  std::string me = LanNet::HostName();
  for (char &ch : me)
    ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
  return LanNet::AddrParse(name.c_str(), &dwAddr) || name == "localhost" || name == "[::1]" ||
         (!me.empty() && (name == me || name == me + ".local"));
}

void Get(int nConn, Conn &c, const std::string &head)
{
  const size_t nSp1 = head.find(' '), nSp2 = head.find(' ', nSp1 + 1);
  const std::string method = head.substr(0, nSp1);
  std::string path = nSp1 == std::string::npos ? "" : head.substr(nSp1 + 1, nSp2 - nSp1 - 1);
  const size_t nQuery = path.find('?');
  if (nQuery != std::string::npos)
    path = path.substr(0, nQuery);
  c.bKeepAlive = Header(head, "Connection").find("close") == std::string::npos;
  const bool bHead = method == "HEAD";
  if (method != "GET" && !bHead) {
    Reply(nConn, 405, "Method Not Allowed", "text/plain", "GET only\n");
    return;
  }
  // the WebSocket: the room's JSON on /ws; on /tcp/<port>, the bytes of a connection to the
  // room's game port, for a Hyperion in a browser, which has no other way to it
  const bool bBridge = path.compare(0, 5, "/tcp/") == 0;
  if ((path == "/ws" || bBridge) && Header(head, "Upgrade").find("ebsocket") != std::string::npos) {
    if (!OriginOk(head)) {
      Reply(nConn, 403, "Forbidden", "text/plain", "only the room's own page\n");
      return;
    }
    if (bBridge) {
      if (atoi(path.c_str() + 5) != LanNet::GamePort()) {
        Reply(nConn, 403, "Forbidden", "text/plain", "only the game port\n");
        return;
      }
      uint32_t dwAddr = 0;
      LanNet::AddrParse("127.0.0.1", &dwAddr);
      c.nPeer = LanNet::Connect(dwAddr, LanNet::GamePort(), 2000);
      if (c.nPeer < 0)
        c.nPeer = LanNet::Connect(LanNet::LocalAddr(), LanNet::GamePort(), 2000);
      if (c.nPeer < 0) {
        Reply(nConn, 502, "Bad Gateway", "text/plain", "the game port does not answer\n");
        return;
      }
      LanNet::MarkBridge(c.nPeer, LanNet::PeerAddr(nConn));
    }
    const std::string key = Header(head, "Sec-WebSocket-Key");
    Sha1 sha;
    const std::string both = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    sha.Add(both.data(), both.size());
    uint8_t digest[20];
    sha.Finish(digest);
    // a browser's Emscripten asks for "binary", which it is
    const bool bBinary = Header(head, "Sec-WebSocket-Protocol").find("binary") != std::string::npos;
    const std::string s = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                          "Sec-WebSocket-Accept: " + Base64(digest, 20) + "\r\n" +
                          (bBinary ? "Sec-WebSocket-Protocol: binary\r\n" : "") + "\r\n";
    LanNet::SendRaw(nConn, s.data(), s.size());
    c.bWs = true;
    c.qwOpened = PortNowMs();
    if (!bBridge)
      g_events.push_back(LanWeb::Event{LanWeb::OPENED, nConn, std::string()});
    return;
  }
  if (path.empty() || path[0] != '/' || path.find("..") != std::string::npos || path.find('\\') != std::string::npos) {
    Reply(nConn, 400, "Bad Request", "text/plain", "bad path\n");
    return;
  }
  // the host's game files, by a token of a player in the room: /files/<token>/<name>
  if (path.compare(0, 7, "/files/") == 0) {
    const size_t nEnd = path.find('/', 7);
    if (nEnd == std::string::npos || g_filesTokens.count(path.substr(7, nEnd - 7)) == 0) {
      Reply(nConn, 404, "Not Found", "text/plain", "not here\n");
      return;
    }
    path = "/files" + path.substr(nEnd);
  }
  if (path.back() == '/')
    path += "index.html";
  const std::string range = Header(head, "Range");
  auto it = g_served.find(path);
  if (it != g_served.end() && it->second.data) {
    c.pData = it->second.data;
    Send(nConn, c, c.pData->size(), it->second.type, range, bHead);
    if (c.qwAt >= c.qwEnd)
      c.pData.reset();
    return;
  }
  // Hyperion for the browser is its own folder, web/play
  const std::string file = it != g_served.end()                ? it->second.file
                           : path.compare(0, 6, "/play/") == 0 ? g_play + path.substr(6)
                                                                : g_root + path.substr(1);
  FILE *f = fopen(file.c_str(), "rb");
  if (f == NULL) {
    Reply(nConn, 404, "Not Found", "text/plain", "not here\n");
    return;
  }
#if defined(_WIN32)
  _fseeki64(f, 0, SEEK_END);
  const uint64_t qwSize = static_cast<uint64_t>(_ftelli64(f));
#else
  fseeko(f, 0, SEEK_END);
  const uint64_t qwSize = static_cast<uint64_t>(ftello(f));
#endif
  c.pFile = f;
  Send(nConn, c, qwSize, it != g_served.end() ? it->second.type : TypeOf(path), range, bHead);
  if (c.qwAt >= c.qwEnd) {
    // no body (HEAD, or an empty file): the pump, which closes a file it has sent, never sees it
    fclose(f);
    c.pFile = NULL;
    return;
  }
#if defined(_WIN32)
  _fseeki64(f, static_cast<long long>(c.qwAt), SEEK_SET);
#else
  fseeko(f, static_cast<off_t>(c.qwAt), SEEK_SET);
#endif
}

void WsFrame(int nConn, uint8_t bOp, const void *pv, size_t cb)
{
  std::vector<uint8_t> f;
  f.push_back(static_cast<uint8_t>(0x80 | bOp));
  if (cb < 126) {
    f.push_back(static_cast<uint8_t>(cb));
  } else if (cb < 65536) {
    f.push_back(126);
    f.push_back(static_cast<uint8_t>(cb >> 8));
    f.push_back(static_cast<uint8_t>(cb));
  } else {
    f.push_back(127);
    for (int i = 7; i >= 0; --i)
      f.push_back(static_cast<uint8_t>(static_cast<uint64_t>(cb) >> (i * 8)));
  }
  f.insert(f.end(), static_cast<const uint8_t *>(pv), static_cast<const uint8_t *>(pv) + cb);
  LanNet::SendRaw(nConn, f.data(), f.size());
}

// How a bridge ended, for the room's log.
void BridgeNote(int nConn, const Conn &c, const char *pszHow)
{
  if (c.nPeer < 0)
    return;
  char sz[256];
  snprintf(sz, sizeof sz, "bridge from %s ended after %.1f s (%llu bytes to the game, %llu back): %s",
           LanNet::AddrText(LanNet::PeerAddr(nConn)).c_str(), (PortNowMs() - c.qwOpened) / 1000.0,
           static_cast<unsigned long long>(c.qwToGame), static_cast<unsigned long long>(c.qwToBrowser), pszHow);
  g_events.push_back(LanWeb::Event{LanWeb::NOTE, nConn, sz});
}

void WsClosed(int nConn, Conn &c)
{
  if (!c.bClosing) {
    c.bClosing = true;
    if (c.nPeer >= 0)
      LanNet::Drop(c.nPeer);
    else
      g_events.push_back(LanWeb::Event{LanWeb::CLOSED, nConn, std::string()});
  }
}

// The WebSocket's frames that have come whole.
void WsRead(int nConn, Conn &c)
{
  for (;;) {
    if (c.in.size() < 2)
      return;
    const uint8_t b0 = c.in[0], b1 = c.in[1];
    size_t at = 2;
    uint64_t qwLen = b1 & 0x7F;
    if (qwLen == 126) {
      if (c.in.size() < 4)
        return;
      qwLen = static_cast<uint64_t>(c.in[2]) << 8 | c.in[3];
      at = 4;
    } else if (qwLen == 127) {
      if (c.in.size() < 10)
        return;
      qwLen = 0;
      for (int i = 0; i < 8; ++i)
        qwLen = qwLen << 8 | c.in[2 + i];
      at = 10;
    }
    if (qwLen > kMessageMax) {
      if (!c.bClosing)
        BridgeNote(nConn, c, "the browser sent a message too long");
      WsClosed(nConn, c);
      LanNet::Drop(nConn);
      return;
    }
    const bool bMasked = (b1 & 0x80) != 0;
    if (c.in.size() < at + (bMasked ? 4 : 0) + qwLen)
      return;
    uint8_t mask[4] = {0, 0, 0, 0};
    if (bMasked) {
      memcpy(mask, &c.in[at], 4);
      at += 4;
    }
    std::vector<uint8_t> payload(c.in.begin() + static_cast<std::ptrdiff_t>(at),
                                 c.in.begin() + static_cast<std::ptrdiff_t>(at + qwLen));
    for (size_t i = 0; i < payload.size(); ++i)
      payload[i] ^= mask[i % 4];
    c.in.erase(c.in.begin(), c.in.begin() + static_cast<std::ptrdiff_t>(at + qwLen));
    const uint8_t bOp = b0 & 0x0F;
    const bool bFin = (b0 & 0x80) != 0;
    if (bOp == 8) {  // close
      if (!c.bClosing) {
        char sz[64];
        snprintf(sz, sizeof sz, "the browser closed it (code %d)", payload.size() >= 2 ? payload[0] << 8 | payload[1] : 0);
        BridgeNote(nConn, c, sz);
      }
      WsFrame(nConn, 8, payload.data(), payload.size() >= 2 ? 2 : 0);
      WsClosed(nConn, c);
      LanNet::Drop(nConn);
      return;
    }
    if (bOp == 9) {  // ping
      WsFrame(nConn, 10, payload.data(), payload.size());
      continue;
    }
    if (bOp == 10)
      continue;
    if (bOp == 1 || bOp == 2) {
      c.message.clear();
      c.bMessageOp = bOp;
    }
    c.message.insert(c.message.end(), payload.begin(), payload.end());
    if (c.message.size() > kMessageMax) {
      if (!c.bClosing)
        BridgeNote(nConn, c, "the browser sent a message too long");
      WsClosed(nConn, c);
      LanNet::Drop(nConn);
      return;
    }
    if (bFin) {
      if (c.nPeer >= 0) {
        LanNet::SendRaw(c.nPeer, c.message.data(), c.message.size());  // a bridge's bytes, as they came
        c.qwToGame += c.message.size();
      }
      else if (c.bMessageOp == 1)
        g_events.push_back(LanWeb::Event{LanWeb::TEXT, nConn, std::string(c.message.begin(), c.message.end())});
      c.message.clear();
    }
  }
}

}  // namespace

namespace LanWeb {

bool Start(void)
{
  const char *psz = getenv("HYPERION_LAN_WEB_PORT");
  g_nPort = psz != NULL && atoi(psz) > 0 && atoi(psz) < 65536 ? atoi(psz) : 7117;
  const char *pszRoot = getenv("HYPERION_LAN_WEB_ROOT");
  g_root = pszRoot != NULL && pszRoot[0] ? pszRoot : PackagedFolder("web\\lan");
  if (!g_root.empty() && g_root.back() != '/' && g_root.back() != '\\')
    g_root += '/';
  const char *pszPlay = getenv("HYPERION_LAN_PLAY_ROOT");
  g_play = pszPlay != NULL && pszPlay[0] ? pszPlay : PackagedFolder("web\\play");
  if (!g_play.empty() && g_play.back() != '/' && g_play.back() != '\\')
    g_play += '/';
  g_nListener = LanNet::ListenOn(g_nPort, true);
  if (g_nListener < 0)
    g_nPort = 0;
  return g_nListener >= 0;
}

void Stop(void)
{
  for (auto &kv : g_conns) {
    if (kv.second.pFile)
      fclose(kv.second.pFile);
    if (kv.second.nPeer >= 0)
      LanNet::Drop(kv.second.nPeer);
    LanNet::Drop(kv.first);
  }
  g_conns.clear();
  if (g_nListener >= 0)
    LanNet::CloseListener(g_nListener);
  g_nListener = -1;
}

int Port(void)
{
  return g_nPort;
}

void Pump(void)
{
  if (g_nListener < 0)
    return;
  for (int n; (n = LanNet::AcceptOn(g_nListener)) >= 0;)
    g_conns[n] = Conn();
  for (auto it = g_conns.begin(); it != g_conns.end();) {
    const int nConn = it->first;
    Conn &c = it->second;
    LanNet::TakeRaw(nConn, &c.in);
    if (!LanNet::Alive(nConn)) {
      if (c.bWs && !c.bClosing)
        BridgeNote(nConn, c, "the browser's connection ended without a close");
      if (c.bWs)
        WsClosed(nConn, c);  // a bridge's game connection with it
      if (c.pFile)
        fclose(c.pFile);
      LanNet::Drop(nConn);
      it = g_conns.erase(it);
      continue;
    }
    if (c.bWs) {
      WsRead(nConn, c);
      if (c.nPeer >= 0 && !c.bClosing) {
        // the game port's bytes to the browser, and its end the WebSocket's
        std::vector<uint8_t> from;
        LanNet::TakeRaw(c.nPeer, &from);
        if (!from.empty()) {
          WsFrame(nConn, 2, from.data(), from.size());
          c.qwToBrowser += from.size();
        }
        if (!LanNet::Alive(c.nPeer)) {
          BridgeNote(nConn, c, "the game port closed it");
          WsFrame(nConn, 8, NULL, 0);
          WsClosed(nConn, c);
          LanNet::Drop(nConn);
        }
      }
    } else if (c.qwAt < c.qwEnd) {
      // a file going out, a chunk at a time as the connection drains
      while (c.qwAt < c.qwEnd && LanNet::Pending(nConn) < 4 * kChunk) {
        const size_t n = static_cast<size_t>(c.qwEnd - c.qwAt < kChunk ? c.qwEnd - c.qwAt : kChunk);
        if (c.pData) {
          LanNet::SendRaw(nConn, c.pData->data() + c.qwAt, n);
        } else {
          std::vector<uint8_t> ab(n);
          const size_t got = c.pFile ? fread(ab.data(), 1, n, c.pFile) : 0;
          if (got == 0) {
            c.qwEnd = c.qwAt;
            LanNet::Drop(nConn);
            break;
          }
          LanNet::SendRaw(nConn, ab.data(), got);
          c.qwAt += got;
          continue;
        }
        c.qwAt += n;
      }
      if (c.qwAt >= c.qwEnd) {
        if (c.pFile)
          fclose(c.pFile);
        c.pFile = NULL;
        c.pData.reset();
        if (!c.bKeepAlive)
          LanNet::Drop(nConn);
      }
    } else {
      // a request whose head has come whole
      const uint8_t kEnd[4] = {'\r', '\n', '\r', '\n'};
      auto e = std::search(c.in.begin(), c.in.end(), kEnd, kEnd + 4);
      if (e != c.in.end()) {
        const std::string head(c.in.begin(), e + 2);
        c.in.erase(c.in.begin(), e + 4);
        Get(nConn, c, head);
        if (!c.bKeepAlive && !c.bWs && c.qwAt >= c.qwEnd)
          LanNet::Drop(nConn);
      } else if (c.in.size() > kHeadMax) {
        LanNet::Drop(nConn);
      }
    }
    ++it;
  }
}

std::vector<Event> Events(void)
{
  std::vector<Event> out;
  out.swap(g_events);
  return out;
}

void SendText(int nConn, const std::string &text)
{
  auto it = g_conns.find(nConn);
  if (it != g_conns.end() && it->second.bWs && !it->second.bClosing)
    WsFrame(nConn, 1, text.data(), text.size());
}

void SendBinary(int nConn, const void *pv, size_t cb)
{
  auto it = g_conns.find(nConn);
  if (it != g_conns.end() && it->second.bWs && !it->second.bClosing)
    WsFrame(nConn, 2, pv, cb);
}

void Close(int nConn)
{
  auto it = g_conns.find(nConn);
  if (it == g_conns.end())
    return;
  if (it->second.bWs && !it->second.bClosing) {
    WsFrame(nConn, 8, NULL, 0);
    it->second.bClosing = true;
  }
  LanNet::Drop(nConn);
}

void Serve(const std::string &path, const std::string &type, const std::vector<uint8_t> &data)
{
  if (data.empty()) {
    g_served.erase(path);
    return;
  }
  Served s;
  s.type = type;
  s.data = std::make_shared<std::vector<uint8_t>>(data);
  g_served[path] = s;
}

void ServeFile(const std::string &path, const std::string &type, const std::string &file)
{
  if (file.empty()) {
    g_served.erase(path);
    return;
  }
  Served s;
  s.type = type;
  s.file = file;
  g_served[path] = s;
}

void AllowFiles(const std::string &token)
{
  if (!token.empty())
    g_filesTokens.insert(token);
}

void RevokeFiles(const std::string &token)
{
  g_filesTokens.erase(token);
}

bool PlayOn(void)
{
  // looked for once, when the server starts
  static int s_nOn = -1;
  if (s_nOn < 0 && !g_play.empty()) {
    FILE *f = fopen((g_play + "hyperion.wasm").c_str(), "rb");
    if (f != NULL)
      fclose(f);
    s_nOn = f != NULL;
  }
  return s_nOn > 0;
}

}  // namespace LanWeb
