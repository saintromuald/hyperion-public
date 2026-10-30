// port_util.h -- small helpers the port's own modules share: a steady clock, ASCII lower case,
// whole files read, and 1.17's game speeds. Header only, and free of the game's headers, so the
// LAN room and the tools can take it as the game's files do.
#ifndef PORT_UTIL_H
#define PORT_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <chrono>
#include <string>
#include <vector>

// Milliseconds on a steady clock, for timeouts and ages; not the game's time (port_clock.cpp).
inline uint64_t PortNowMs(void)
{
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// s in lower case, A to Z only: names and the protocols' words. Other bytes, UTF-8's included,
// are left as they are.
inline std::string PortLower(std::string s)
{
  for (char &c : s)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  return s;
}

// Bytes in base64 (RFC 4648, padded), as a JSON message carries them.
inline std::string PortBase64(const void *pv, size_t n)
{
  static const char k[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const unsigned char *p = static_cast<const unsigned char *>(pv);
  std::string out;
  out.reserve((n + 2) / 3 * 4);
  for (size_t i = 0; i < n; i += 3) {
    const unsigned v = p[i] << 16 | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
    out += k[v >> 18 & 63];
    out += k[v >> 12 & 63];
    out += i + 1 < n ? k[v >> 6 & 63] : '=';
    out += i + 2 < n ? k[v & 63] : '=';
  }
  return out;
}

// Base64 back to bytes; what is not base64 (padding, white space) is passed over.
inline std::vector<unsigned char> PortBase64Bytes(const std::string &s)
{
  std::vector<unsigned char> out;
  out.reserve(s.size() / 4 * 3);
  unsigned v = 0;
  int nBits = 0;
  for (char c : s) {
    const int n = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
                  : c == '+' ? 62 : c == '/' ? 63 : -1;
    if (n < 0)
      continue;
    v = (v << 6 | static_cast<unsigned>(n)) & 0xFFFFFF;
    nBits += 6;
    if (nBits >= 8) {
      nBits -= 8;
      out.push_back(static_cast<unsigned char>(v >> nBits & 0xFF));
    }
  }
  return out;
}

// The most a whole-file read takes unless it is told otherwise.
const size_t kPortReadMost = 64u << 20;

// A whole file into *pOut (a std::string or a std::vector of bytes): false when it does not
// open, or holds more than nMost bytes. fopen is the port's on every system (port_posix.cpp
// wraps it on Linux), so paths are written as the game writes them.
template <class Bytes>
bool PortReadFile(const std::string &path, Bytes *pOut, size_t nMost = kPortReadMost)
{
  pOut->clear();
  FILE *f = fopen(path.c_str(), "rb");
  if (f == NULL)
    return false;
  char buf[65536];
  size_t n;
  bool bOk = true;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
    if (pOut->size() + n > nMost) {
      bOk = false;
      break;
    }
    pOut->insert(pOut->end(), buf, buf + n);
  }
  fclose(f);
  if (!bOk)
    pOut->clear();
  return bOk;
}

// A file's text, "" when it does not read: for a file whose absence reads as empty.
inline std::string PortReadText(const std::string &path)
{
  std::string s;
  PortReadFile(path, &s);
  return s;
}

// 1.17's game speeds, Slowest (0) to Fastest (6): a game frame's milliseconds, as the game's
// sgTicksPerFrame_g starts. A speed past them is Fastest.
inline uint32_t PortSpeedFrameMs(int nSpeed)
{
  static const uint32_t kMs[7] = {167, 111, 83, 67, 56, 48, 42};
  return kMs[nSpeed >= 0 && nSpeed < 7 ? nSpeed : 6];
}
const int kPortSpeeds = 7;

#endif
