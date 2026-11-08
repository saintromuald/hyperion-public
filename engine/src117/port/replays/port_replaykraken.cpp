// port_replaykraken.cpp -- immutable Replay Kraken protocol work. No function
// here reads game state or performs file/network I/O, so callers may use this
// file on workers as well as on the front end's main thread.
#include "../../h/port_replaykraken.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <random>
#include <set>
#include <sstream>

#include "../platform/port_json.h"

namespace ReplayKraken {
namespace {

std::string Lower(std::string s)
{
  for (char &c : s)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  return s;
}

std::string Trim(const std::string &s)
{
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'))
    ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
    --b;
  return s.substr(a, b - a);
}

bool Error(std::string *pError, const std::string &text)
{
  if (pError)
    *pError = text;
  return false;
}

bool TextField(const PortJson &v, const char *name, std::string *pOut, std::string *pError, bool bRequired = true)
{
  const PortJson &field = v[name];
  if (field.kind == PortJson::NULLV || field.kind == PortJson::NONE) {
    pOut->clear();
    return !bRequired || Error(pError, std::string("missing text field ") + name);
  }
  if (field.kind != PortJson::STRING)
    return Error(pError, std::string("field is not a string: ") + name);
  if (field.s.size() > 16384)
    return Error(pError, std::string("field is too long: ") + name);
  *pOut = field.s;
  return true;
}

bool IntegerField(const PortJson &v, const char *name, int64_t *pOut, std::string *pError)
{
  const PortJson &field = v[name];
  if (field.kind != PortJson::NUMBER || field.d < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
      field.d > 9007199254740991.0)
    return Error(pError, std::string("field is not a safe integer: ") + name);
  *pOut = static_cast<int64_t>(field.d);
  return true;
}

std::string JsonStringArray(const std::vector<std::string> &items)
{
  std::string out = "[";
  for (size_t i = 0; i < items.size(); ++i)
    out += (i ? "," : "") + PortJsonQuote(items[i]);
  return out + "]";
}

int64_t HttpDateMs(const std::string &text)
{
  // Retry-After's HTTP-date form, RFC 7231's IMF-fixdate.
  char weekday[4] = {}, month[4] = {}, zone[4] = {};
  int day = 0, year = 0, hour = 0, minute = 0, second = 0;
  if (sscanf(text.c_str(), "%3[A-Za-z], %d %3[A-Za-z] %d %d:%d:%d %3[GMT]", weekday, &day, month, &year, &hour,
             &minute, &second, zone) != 8 || strcmp(zone, "GMT") != 0)
    return -1;
  static const char *const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  int mon = -1;
  for (int i = 0; i < 12; ++i)
    if (strcmp(month, months[i]) == 0)
      mon = i;
  if (mon < 0 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
      second > 60)
    return -1;
  std::tm tm = {};
  tm.tm_year = year - 1900;
  tm.tm_mon = mon;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = second;
#ifdef _WIN32
  const __time64_t at = _mkgmtime64(&tm);
  const __time64_t now = _time64(nullptr);
#else
  const time_t at = timegm(&tm);
  const time_t now = time(nullptr);
#endif
  if (at < 0)
    return -1;
  return at > now ? static_cast<int64_t>(at - now) * 1000 : 0;
}

int64_t RetryAfterValue(const std::string &text)
{
  const std::string value = Trim(text);
  if (value.empty())
    return -1;
  if (value.find_first_not_of("0123456789") == std::string::npos) {
    if (value.size() > 8)
      return 24 * 60 * 60 * 1000LL;
    return strtoll(value.c_str(), nullptr, 10) * 1000LL;
  }
  return HttpDateMs(value);
}

uint32_t Rotate(uint32_t v, unsigned n)
{
  return v >> n | v << (32 - n);
}

// A small, worker-owned SHA-256. Content identities are local persistence keys;
// keeping the implementation here also makes them identical on every target.
std::array<unsigned char, 32> Sha256(const unsigned char *pData, size_t bytes)
{
  static const uint32_t initial[] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  static const uint32_t constants[] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t h[8];
  std::copy(initial, initial + 8, h);
  const uint64_t totalBits = static_cast<uint64_t>(bytes) * 8;
  const size_t padded = (bytes + 9 + 63) / 64 * 64;
  std::vector<unsigned char> message(padded, 0);
  if (bytes)
    memcpy(message.data(), pData, bytes);
  message[bytes] = 0x80;
  for (unsigned i = 0; i < 8; ++i)
    message[padded - 1 - i] = static_cast<unsigned char>(totalBits >> (i * 8));
  for (size_t at = 0; at < padded; at += 64) {
    uint32_t w[64];
    for (unsigned i = 0; i < 16; ++i)
      w[i] = static_cast<uint32_t>(message[at + i * 4]) << 24 | static_cast<uint32_t>(message[at + i * 4 + 1]) << 16 |
             static_cast<uint32_t>(message[at + i * 4 + 2]) << 8 | message[at + i * 4 + 3];
    for (unsigned i = 16; i < 64; ++i) {
      const uint32_t s0 = Rotate(w[i - 15], 7) ^ Rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = Rotate(w[i - 2], 17) ^ Rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (unsigned i = 0; i < 64; ++i) {
      const uint32_t s1 = Rotate(e, 6) ^ Rotate(e, 11) ^ Rotate(e, 25);
      const uint32_t choose = e & f ^ ~e & g;
      const uint32_t t1 = hh + s1 + choose + constants[i] + w[i];
      const uint32_t s0 = Rotate(a, 2) ^ Rotate(a, 13) ^ Rotate(a, 22);
      const uint32_t majority = a & b ^ a & c ^ b & c;
      const uint32_t t2 = s0 + majority;
      hh = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
  }
  std::array<unsigned char, 32> out = {};
  for (unsigned i = 0; i < 8; ++i)
    for (unsigned j = 0; j < 4; ++j)
      out[i * 4 + j] = static_cast<unsigned char>(h[i] >> (24 - j * 8));
  return out;
}

}  // namespace

bool EndpointParse(const std::string &text, Endpoint *pOut, std::string *pError)
{
  *pOut = Endpoint();
  pOut->input = Trim(text);
  const std::string &url = pOut->input;
  if (url.empty())
    return Error(pError, "Enter a Replay Kraken server URL.");
  if (url.size() > 2048 || url.find_first_of("?#\r\n\t ") != std::string::npos)
    return Error(pError, "The server URL cannot contain spaces, credentials, a query or a fragment.");
  const size_t schemeAt = url.find("://");
  if (schemeAt == std::string::npos)
    return Error(pError, "Use an absolute https:// URL (plain HTTP is allowed only on loopback).");
  const std::string scheme = Lower(url.substr(0, schemeAt));
  if (scheme != "https" && scheme != "http")
    return Error(pError, "Only HTTPS and explicit loopback HTTP are supported.");
  const size_t pathAt = url.find('/', schemeAt + 3);
  std::string authority = url.substr(schemeAt + 3, pathAt == std::string::npos ? std::string::npos : pathAt - schemeAt - 3);
  if (authority.empty() || authority.find('@') != std::string::npos || authority.find('\\') != std::string::npos)
    return Error(pError, "The server URL needs a host and cannot contain credentials.");
  std::string host, port;
  if (authority[0] == '[') {
    const size_t close = authority.find(']');
    if (close == std::string::npos)
      return Error(pError, "The bracketed IPv6 host is incomplete.");
    host = Lower(authority.substr(1, close - 1));
    const std::string rest = authority.substr(close + 1);
    if (!rest.empty()) {
      if (rest[0] != ':')
        return Error(pError, "Unexpected text after the server host.");
      port = rest.substr(1);
    }
  } else {
    const size_t colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(':') == colon) {
      host = authority.substr(0, colon);
      port = authority.substr(colon + 1);
    } else {
      host = authority;
      if (authority.find(':') != std::string::npos)
        return Error(pError, "IPv6 server hosts must be bracketed.");
    }
    host = Lower(host);
  }
  while (!host.empty() && host.back() == '.')
    host.pop_back();
  if (host.empty() || host.size() > 253)
    return Error(pError, "The server host is empty or too long.");
  for (unsigned char c : host)
    if (c < 0x21 || c == '/' || c == '\\' || c == '@')
      return Error(pError, "The server host is invalid.");
  int portNumber = scheme == "https" ? 443 : 80;
  if (!port.empty()) {
    if (port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos)
      return Error(pError, "The server port is invalid.");
    portNumber = atoi(port.c_str());
    if (portNumber < 1 || portNumber > 65535)
      return Error(pError, "The server port is invalid.");
  }
  const bool bIpv6 = host.find(':') != std::string::npos;
  const std::string shownHost = bIpv6 ? "[" + host + "]" : host;
  pOut->origin = scheme + "://" + shownHost;
  if ((scheme == "https" && portNumber != 443) || (scheme == "http" && portNumber != 80))
    pOut->origin += ":" + std::to_string(portNumber);
  pOut->bLoopbackHttp = scheme == "http" && (host == "localhost" || host == "127.0.0.1" || host == "::1");
  if (scheme == "http" && !pOut->bLoopbackHttp)
    return Error(pError, "Plain HTTP is allowed only for an explicit loopback test server.");
  std::string path = pathAt == std::string::npos ? "" : url.substr(pathAt);
  while (!path.empty() && path.back() == '/')
    path.pop_back();
  if (path == "/")
    path.clear();
  if (path.find('\\') != std::string::npos || path.find("//") != std::string::npos)
    return Error(pError, "The server base path is invalid.");
  size_t partAt = 0;
  while (partAt < path.size()) {
    const size_t slash = path.find('/', partAt + 1);
    const std::string part = path.substr(partAt + 1, slash == std::string::npos ? std::string::npos : slash - partAt - 1);
    if (part == "." || part == "..")
      return Error(pError, "The server base path cannot contain dot segments.");
    if (slash == std::string::npos)
      break;
    partAt = slash;
  }
  while (path.size() >= 4 && path.compare(path.size() - 4, 4, "/api") == 0)
    path.resize(path.size() - 4);
  pOut->api = pOut->origin + path + "/api";
  return true;
}

std::string UrlEncode(const std::string &text)
{
  static const char hex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size() * 3);
  for (unsigned char c : text) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~')
      out += static_cast<char>(c);
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

bool MatchUuidValid(const std::string &uuid)
{
  if (uuid.empty() || uuid.size() > 128)
    return false;
  for (unsigned char c : uuid)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'))
      return false;
  return true;
}

std::string MatchPath(const Endpoint &server, const std::string &uuid)
{
  return server.api + "/matches/" + UrlEncode(uuid) + "/replay";
}

std::string MatchupKey(const std::string &matchup)
{
  std::vector<std::string> parts;
  size_t at = 0;
  while (at <= matchup.size()) {
    const size_t v = matchup.find_first_of("vV", at);
    std::string part = Lower(Trim(matchup.substr(at, v == std::string::npos ? std::string::npos : v - at)));
    if (!part.empty() && std::find(parts.begin(), parts.end(), part) == parts.end())
      parts.push_back(part);
    if (v == std::string::npos)
      break;
    at = v + 1;
  }
  std::sort(parts.begin(), parts.end());
  std::string out;
  for (const std::string &part : parts)
    out += (out.empty() ? "" : "v") + part;
  return out;
}

std::string SafeFilePart(const std::string &text, size_t maxBytes)
{
  std::string out;
  for (size_t i = 0; i < text.size() && out.size() < maxBytes;) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    size_t count = 1;
    if (c >= 0xC2 && c <= 0xDF && i + 1 < text.size() && (text[i + 1] & 0xC0) == 0x80)
      count = 2;
    else if (c >= 0xE0 && c <= 0xEF && i + 2 < text.size() && (text[i + 1] & 0xC0) == 0x80 &&
             (text[i + 2] & 0xC0) == 0x80)
      count = 3;
    else if (c >= 0xF0 && c <= 0xF4 && i + 3 < text.size() && (text[i + 1] & 0xC0) == 0x80 &&
             (text[i + 2] & 0xC0) == 0x80 && (text[i + 3] & 0xC0) == 0x80)
      count = 4;
    const bool bForbidden = c < 0x20 || c == 0x7F || c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
                            c == '"' || c == '<' || c == '>' || c == '|';
    if (count > 1 && out.size() + count <= maxBytes) {
      out.append(text, i, count);
    } else if (count == 1 && !bForbidden) {
      out += static_cast<char>(c);
    } else if (count == 1) {
      out += '_';
    }
    i += count;
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
    out.pop_back();
  size_t first = 0;
  while (first < out.size() && out[first] == ' ')
    ++first;
  out.erase(0, first);
  if (out.empty())
    out = "replay";
  const std::string upper = Lower(out);
  static const std::set<std::string> reserved = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4",
                                                 "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2",
                                                 "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
  const std::string stem = upper.substr(0, upper.find('.'));
  if (reserved.count(stem))
    out = "_" + out;
  return out;
}

std::string ListUrl(const Endpoint &server, const Query &query, size_t offset, size_t limit)
{
  limit = std::max<size_t>(1, std::min(limit, kMaxPageSize));
  std::string url = server.api + "/matches?limit=" + std::to_string(limit) + "&offset=" + std::to_string(offset);
  const std::pair<const char *, const std::string *> fields[] = {
      {"player", &query.player}, {"race", &query.race},         {"result", &query.result},
      {"matchup", &query.matchup}, {"from", &query.from},       {"to", &query.to},
      {"followed_by", &query.followedBy}};
  for (const auto &field : fields)
    if (!field.second->empty())
      url += "&" + std::string(field.first) + "=" + UrlEncode(*field.second);
  return url;
}

bool ParsePage(const std::string &json, const Endpoint &server, MatchPage *pOut, std::string *pError)
{
  *pOut = MatchPage();
  PortJson root;
  if (!PortJsonParse(json.c_str(), &root, pError))
    return false;
  if (root.kind != PortJson::OBJECT || root["matches"].kind != PortJson::ARRAY)
    return Error(pError, "The server response has no matches array.");
  if (root["matches"].items.size() > kMaxPageSize)
    return Error(pError, "The server returned more than the 200-row protocol page bound.");
  if (!IntegerField(root, "total", &pOut->total, pError) || !IntegerField(root, "limit", &pOut->limit, pError) ||
      !IntegerField(root, "offset", &pOut->offset, pError))
    return false;
  if (pOut->total < 0 || pOut->total > 100000000)
    return Error(pError, "The server returned an invalid total.");
  for (const PortJson &row : root["matches"].items) {
    if (row.kind != PortJson::OBJECT)
      return Error(pError, "A match row is not an object.");
    Match match;
    if (!TextField(row, "uuid", &match.uuid, pError) || !MatchUuidValid(match.uuid))
      return Error(pError, "A match row has an invalid UUID.");
    if (!TextField(row, "game_version", &match.gameVersion, pError, false) ||
        !TextField(row, "map", &match.map, pError, false) ||
        !TextField(row, "game_type", &match.gameType, pError, false) ||
        !TextField(row, "matchup", &match.matchup, pError, false) ||
        !TextField(row, "uploaded_by", &match.uploadedBy, pError, false) ||
        !TextField(row, "uploaded_at", &match.uploadedAt, pError, false))
      return false;
    const PortJson &duration = row["duration_frames"];
    if (duration.kind != PortJson::NONE && duration.kind != PortJson::NULLV) {
      if (!IntegerField(row, "duration_frames", &match.durationFrames, pError) || match.durationFrames < 0 ||
          match.durationFrames > 0xFFFFFFFFLL)
        return Error(pError, "A match row has an invalid duration.");
    }
    if (row["players"].kind != PortJson::ARRAY || row["players"].items.size() > 64)
      return Error(pError, "A match row has an invalid players array.");
    for (const PortJson &slot : row["players"].items) {
      Player player;
      if (slot.kind != PortJson::OBJECT || !TextField(slot, "name", &player.name, pError) ||
          !TextField(slot, "race", &player.race, pError) || !IntegerField(slot, "slot", &player.slot, pError) ||
          player.slot < 0 || player.slot > 255 ||
          !TextField(slot, "result", &player.result, pError, false) ||
          !TextField(slot, "type", &player.type, pError, false) ||
          !TextField(slot, "discord_id", &player.discordId, pError, false))
        return false;
      match.players.push_back(player);
    }
    match.serverOrigin = server.origin;
    match.serverApi = server.api;
    pOut->matches.push_back(match);
  }
  pOut->nextOffset = pOut->offset + static_cast<int64_t>(pOut->matches.size());
  pOut->bComplete = pOut->offset >= pOut->total ||
                    (pOut->offset <= pOut->total && static_cast<int64_t>(pOut->matches.size()) >= pOut->total - pOut->offset);
  return true;
}

void MergePage(MatchPage *pInto, const MatchPage &page)
{
  for (const Match &match : page.matches) {
    auto at = std::find_if(pInto->matches.begin(), pInto->matches.end(), [&](const Match &old) {
      return old.uuid == match.uuid && old.serverApi == match.serverApi;
    });
    if (at == pInto->matches.end())
      pInto->matches.push_back(match);
    else
      *at = match;
  }
  pInto->total = page.total;
  pInto->limit = page.limit;
  pInto->offset = page.offset;
  pInto->nextOffset = page.nextOffset;
  pInto->bComplete = page.bComplete;
}

std::string Base64(const std::vector<unsigned char> &bytes)
{
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (bytes.size() > (std::numeric_limits<size_t>::max() - 2) / 3)
    return "";
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  for (size_t i = 0; i < bytes.size(); i += 3) {
    const uint32_t value = static_cast<uint32_t>(bytes[i]) << 16 |
                           (i + 1 < bytes.size() ? static_cast<uint32_t>(bytes[i + 1]) << 8 : 0) |
                           (i + 2 < bytes.size() ? static_cast<uint32_t>(bytes[i + 2]) : 0);
    out += alphabet[value >> 18 & 63];
    out += alphabet[value >> 12 & 63];
    out += i + 1 < bytes.size() ? alphabet[value >> 6 & 63] : '=';
    out += i + 2 < bytes.size() ? alphabet[value & 63] : '=';
  }
  return out;
}

bool UploadBody(const std::string &uuid, const std::string &gameVersion, const UploadMetadata &meta,
                const std::vector<unsigned char> &replay, std::string *pOut, std::string *pError)
{
  pOut->clear();
  if (!MatchUuidValid(uuid))
    return Error(pError, "The native upload UUID is invalid.");
  if (replay.empty())
    return Error(pError, "An upload needs a nonempty replay.");
  if (replay.size() > kMaxReplayBytes || meta.players.size() > 64)
    return Error(pError, "Unsupported by this Replay Kraken server: its 2 MiB JSON limit cannot hold this replay.");
  const std::string encoded = Base64(replay);
  std::string body = "{\"matchUuid\":" + PortJsonQuote(uuid) + ",\"gameVersion\":" + PortJsonQuote(gameVersion) +
                     ",\"replayFile\":" + PortJsonQuote(encoded) + ",\"meta\":{\"map\":" + PortJsonQuote(meta.map) +
                     ",\"gameType\":" + PortJsonQuote(meta.gameType) +
                     ",\"durationFrames\":" + std::to_string(meta.durationFrames) + ",\"players\":[";
  for (size_t i = 0; i < meta.players.size(); ++i) {
    const UploadPlayer &player = meta.players[i];
    body += (i ? "," : "") + std::string("{\"name\":") + PortJsonQuote(player.name) + ",\"race\":" +
            PortJsonQuote(player.race.empty() ? "unknown" : player.race) + ",\"slot\":" + std::to_string(player.slot);
    if (!player.result.empty())
      body += ",\"result\":" + PortJsonQuote(player.result);
    if (!player.type.empty())
      body += ",\"type\":" + PortJsonQuote(player.type);
    body += "}";
  }
  body += "],\"matchup\":" + PortJsonQuote(meta.matchup) + "}}";
  if (body.size() > kMaxRequestBytes)
    return Error(pError, "Unsupported by this Replay Kraken server: the encoded upload exceeds its 2 MiB JSON limit.");
  *pOut = std::move(body);
  return true;
}

bool ParseCreatedUuid(const std::string &json, const std::string &expected, std::string *pError)
{
  PortJson root;
  if (!PortJsonParse(json.c_str(), &root, pError))
    return false;
  if (root.kind != PortJson::OBJECT)
    return Error(pError, "The upload response is not an object.");
  std::string uuid;
  if (!TextField(root, "uuid", &uuid, pError) || uuid != expected)
    return Error(pError, "The server acknowledged a different upload UUID.");
  return true;
}

RetryDecision ClassifyHttp(long status, const std::string &retryAfter)
{
  RetryDecision out;
  out.retryAfterMs = RetryAfterValue(retryAfter);
  if (status >= 200 && status < 300)
    out.kind = Failure::None;
  else if (status == 401 || status == 403)
    out.kind = Failure::Unauthorized;
  else if (status == 409)
    out.kind = Failure::Conflict;
  else if (status == 413 || status == 415 || status == 422)
    out.kind = Failure::Unsupported;
  else if (status == 408 || status == 429 || status >= 500 || status == 0)
    out.kind = Failure::Transient;
  else
    out.kind = Failure::Permanent;
  return out;
}

int64_t BackoffMs(unsigned attempt, int64_t retryAfterMs, uint32_t jitter)
{
  if (retryAfterMs >= 0)
    return std::min<int64_t>(retryAfterMs, 24 * 60 * 60 * 1000LL);
  attempt = std::max(1u, std::min(attempt, 20u));
  int64_t delay = 1000;
  for (unsigned i = 1; i < attempt && delay < 15 * 60 * 1000; ++i)
    delay *= 2;
  delay = std::min<int64_t>(delay, 15 * 60 * 1000);
  return delay + jitter % 1000;
}

bool RedirectAllowed(const Endpoint &from, const std::string &nextUrl, bool bCredentials, Endpoint *pNext,
                     std::string *pError)
{
  if (nextUrl.find('#') != std::string::npos)
    return Error(pError, "Refused a redirect containing a fragment.");
  if (!EndpointParse(nextUrl.substr(0, nextUrl.find('?')), pNext, pError))
    return false;
  const bool bDowngrade = from.origin.compare(0, 6, "https:") == 0 && pNext->origin.compare(0, 5, "http:") == 0;
  if (bDowngrade)
    return Error(pError, "Refused an HTTPS-to-HTTP redirect.");
  if (bCredentials && from.origin != pNext->origin)
    return Error(pError, "Refused to forward Replay Kraken credentials to another origin.");
  return true;
}

std::string UuidNew()
{
  static std::atomic<uint64_t> sequence{0};
  static thread_local std::mt19937_64 random{std::random_device{}() ^
      static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count())};
  uint64_t a = random() ^ ++sequence;
  uint64_t b = random();
  unsigned char bytes[16];
  for (unsigned i = 0; i < 8; ++i) {
    bytes[i] = static_cast<unsigned char>(a >> (56 - i * 8));
    bytes[i + 8] = static_cast<unsigned char>(b >> (56 - i * 8));
  }
  bytes[6] = bytes[6] & 0x0F | 0x40;
  bytes[8] = bytes[8] & 0x3F | 0x80;
  char out[37];
  snprintf(out, sizeof out,
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9],
           bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
  return out;
}

std::string Sha256Hex(const void *pData, size_t bytes)
{
  if (bytes > 64 * 1024 * 1024 || (bytes && !pData))
    return "";
  const auto digest = Sha256(static_cast<const unsigned char *>(pData), bytes);
  static const char hex[] = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (unsigned char c : digest) {
    out += hex[c >> 4];
    out += hex[c & 15];
  }
  return out;
}

std::string ContentId(const std::vector<unsigned char> &bytes)
{
  const std::string hash = Sha256Hex(bytes.data(), bytes.size());
  return hash.empty() ? "" : "sha256:" + hash;
}

std::string ServerRecordId(const Endpoint &server, const std::string &uuid)
{
  return "server:" + Sha256Hex(server.api.data(), server.api.size()) + ":" + uuid;
}

}  // namespace ReplayKraken
