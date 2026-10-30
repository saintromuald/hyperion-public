// hxlive.h: Hyperion's live channel from C++, for a map editor that wants to test a map in a
// running game without restarting it. Header only, C++17, nothing beyond Windows' own WinHTTP.
//
// The channel is a WebSocket on 127.0.0.1, port 27117 unless Hyperion was started with
// --control=<port>, which takes a connection that gives the key Hyperion writes each time it
// starts (ChannelKey; Connect reads it). A message is one JSON object, {"id": <any>, "cmd": "<name>", ...}, and the
// answer carries the same id and "ok", with "error" when ok is false. Send one message and wait
// for its answer before the next. The protocol and the cutscene step format are in
// docs/live-channel-for-editors.md of a Hyperion package; hxlive.py beside it is the same client
// in Python.
//
// This header deliberately has no JSON parser: an editor already has one, and the messages are
// passed as text. What it does carry is the handshake, the framing, and the few things that are
// easy to get wrong: base64 of a map's tiles, quoting a Windows path into a JSON string, the
// message builders, and a wait for a map switch to finish.
//
//   hxlive::Client client;
//   std::string why, answer;
//   if (!client.Connect(27117, &why))
//     return printf("%s\n", why.c_str());
//   client.Call(hxlive::MapPushMessage("jungle", 128, 128, "", 1000, ""), &answer, &why);
//   hxlive::WaitForSwitch(client, 60000, &why);
//   client.Call("{\"cmd\": \"reveal\"}", &answer, &why);
//
// Needs Windows 8 or newer, which is where WinHTTP learned WebSockets. Free to use in any
// editor, change it as you like, no warranty of any kind.

#ifndef HXLIVE_H
#define HXLIVE_H

#include <windows.h>
#include <winhttp.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace hxlive {

// The port Hyperion opens with a bare --control.
const int DEFAULT_PORT = 27117;

// ---------------------------------------------------------------------------
// Small pieces: base64, JSON quoting, and a reader for the flat fields of an answer.

// base64 of any bytes, as the channel wants tiles_b64.
inline std::string Base64(const unsigned char *pBytes, size_t count) {
  static const char *const ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((count + 2) / 3 * 4);
  for (size_t at = 0; at < count; at += 3) {
    const unsigned b0 = pBytes[at];
    const unsigned b1 = at + 1 < count ? pBytes[at + 1] : 0u;
    const unsigned b2 = at + 2 < count ? pBytes[at + 2] : 0u;
    const unsigned triple = b0 << 16 | b1 << 8 | b2;
    out += ALPHABET[(triple >> 18) & 0x3F];
    out += ALPHABET[(triple >> 12) & 0x3F];
    out += at + 1 < count ? ALPHABET[(triple >> 6) & 0x3F] : '=';
    out += at + 2 < count ? ALPHABET[triple & 0x3F] : '=';
  }
  return out;
}

// A map's tiles for map_push: count MTXM values (group * 16 + variation), written as
// little-endian 16-bit values and then base64, which is the MTXM section's own bytes. An editor
// that keeps MTXM in memory can hand its array straight to this.
inline std::string TilesBase64(const uint16_t *pMtxm, size_t count) {
  std::vector<unsigned char> bytes(count * 2);
  for (size_t i = 0; i < count; ++i) {
    bytes[i * 2] = static_cast<unsigned char>(pMtxm[i] & 0xFF);
    bytes[i * 2 + 1] = static_cast<unsigned char>(pMtxm[i] >> 8);
  }
  return Base64(bytes.data(), bytes.size());
}

// Text as a JSON string, the quotes included, which is what a path with backslashes needs:
// C:\maps\test.scx comes back as "C:\\maps\\test.scx". The text has to be UTF-8 already; bytes
// above 0x7F are passed through, as JSON allows.
inline std::string JsonQuote(const std::string &text) {
  std::string out = "\"";
  for (char raw : text) {
    const unsigned char ch = static_cast<unsigned char>(raw);
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (ch < 0x20) {
          char sz[8];
          snprintf(sz, sizeof sz, "\\u%04x", static_cast<unsigned>(ch));
          out += sz;
        } else {
          out += raw;
        }
    }
  }
  return out + "\"";
}

namespace detail {

inline bool IsSpace(char ch) {
  return isspace(static_cast<unsigned char>(ch)) != 0;
}

// The end of the string that starts at json[at] (at is the opening quote).
inline size_t EndOfString(const std::string &json, size_t at) {
  for (size_t i = at + 1; i < json.size(); ++i) {
    if (json[i] == '\\')
      ++i;
    else if (json[i] == '"')
      return i + 1;
  }
  return json.size();
}

// The value that starts at json[from], as it is written: a string with its quotes, an object or
// an array whole, a number or true/false/null as their text.
inline std::string ValueAt(const std::string &json, size_t from) {
  while (from < json.size() && IsSpace(json[from]))
    ++from;
  if (from >= json.size())
    return "";
  if (json[from] == '"')
    return json.substr(from, EndOfString(json, from) - from);
  if (json[from] == '{' || json[from] == '[') {
    int depth = 0;
    for (size_t i = from; i < json.size(); ++i) {
      if (json[i] == '"') {
        i = EndOfString(json, i) - 1;
      } else if (json[i] == '{' || json[i] == '[') {
        ++depth;
      } else if (json[i] == '}' || json[i] == ']') {
        if (--depth == 0)
          return json.substr(from, i + 1 - from);
      }
    }
    return json.substr(from);
  }
  size_t to = from;
  while (to < json.size() && json[to] != ',' && json[to] != '}' && json[to] != ']')
    ++to;
  while (to > from && IsSpace(json[to - 1]))
    --to;
  return json.substr(from, to - from);
}

// A JSON string's own text: the quotes off and the escapes undone.
inline std::string Unquote(const std::string &quoted) {
  if (quoted.size() < 2 || quoted.front() != '"')
    return quoted;
  std::string out;
  for (size_t i = 1; i + 1 < quoted.size(); ++i) {
    if (quoted[i] != '\\') {
      out += quoted[i];
      continue;
    }
    const char next = ++i < quoted.size() ? quoted[i] : '"';
    switch (next) {
      case 'b': out += '\b'; break;
      case 'f': out += '\f'; break;
      case 'n': out += '\n'; break;
      case 'r': out += '\r'; break;
      case 't': out += '\t'; break;
      case 'u': {
        unsigned code = 0;
        for (int k = 0; k < 4 && i + 1 < quoted.size(); ++k) {
          const char hex = quoted[++i];
          const int digit = hex >= '0' && hex <= '9' ? hex - '0'
                            : hex >= 'a' && hex <= 'f' ? hex - 'a' + 10
                            : hex >= 'A' && hex <= 'F' ? hex - 'A' + 10 : 0;
          code = code * 16 + static_cast<unsigned>(digit);
        }
        // the code point as UTF-8; a surrogate pair is not put back together, which the channel
        // never sends anyway (it escapes only the control characters)
        if (code < 0x80) {
          out += static_cast<char>(code);
        } else if (code < 0x800) {
          out += static_cast<char>(0xC0 | code >> 6);
          out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
          out += static_cast<char>(0xE0 | code >> 12);
          out += static_cast<char>(0x80 | (code >> 6 & 0x3F));
          out += static_cast<char>(0x80 | (code & 0x3F));
        }
        break;
      }
      default: out += next;
    }
  }
  return out;
}

inline void Say(std::string *pWhy, const std::string &text) {
  if (pWhy != nullptr)
    *pWhy = text;
}

}  // namespace detail

// The text of a top-level member of a JSON object, or "" when it has none of that name. It walks
// the object once, counting braces and brackets and stepping over strings, so status's nested
// sync.state is not mistaken for its own state, and map_swap_error is not mistaken for map_swap.
// The value comes back as it is written: a string still in its quotes, an object or an array
// whole. This is a reader for flat fields, not a parser; for a nested answer such as status's
// "cutscene" or a list of units, hand the text to a real JSON library.
inline std::string AnswerValue(const std::string &json, const char *pszKey) {
  const std::string key = std::string("\"") + pszKey + "\"";
  int depth = 0;
  for (size_t at = 0; at < json.size(); ++at) {
    const char ch = json[at];
    if (ch == '"') {
      if (depth == 1 && json.compare(at, key.size(), key) == 0) {
        size_t colon = at + key.size();
        while (colon < json.size() && detail::IsSpace(json[colon]))
          ++colon;
        if (colon < json.size() && json[colon] == ':')
          return detail::ValueAt(json, colon + 1);
      }
      at = detail::EndOfString(json, at) - 1;
    } else if (ch == '{' || ch == '[') {
      ++depth;
    } else if (ch == '}' || ch == ']') {
      --depth;
    }
  }
  return "";
}

// Whether an answer says ok.
inline bool AnswerOk(const std::string &answer) {
  return AnswerValue(answer, "ok") == "true";
}

// A top-level string field of an answer, its escapes undone: "error", "state", "map_swap",
// "map_swap_error". A number comes back as the text of the number ("frame"), and a field the
// answer does not carry comes back empty.
inline std::string AnswerString(const std::string &answer, const char *pszKey) {
  return detail::Unquote(AnswerValue(answer, pszKey));
}

// ---------------------------------------------------------------------------
// The messages. Each builder returns the text of one JSON object with no id; Call puts the id in.
// A scene is a whole cutscene object, {"steps": [...]}, which is a sample scene file's own text;
// leave it empty for no scene.

// A command with no fields of its own: "status", "reveal", "cutscene_stop", "cutscene_status".
inline std::string CommandMessage(const char *pszCmd) {
  return std::string("{\"cmd\": ") + JsonQuote(pszCmd) + "}";
}

inline std::string StatusMessage() {
  return CommandMessage("status");
}

// map_push: a map made from what it is given, played at once. tileset is badlands, platform,
// install, ashworld, jungle, desert, ice or twilight; width and height are each 64, 96, 128, 192
// or 256 tiles. With tilesB64 empty the ground is the tileset's first plain buildable group,
// varied as a brush varies it, which is what an editor wants for a quick look; otherwise it is
// TilesBase64 of width * height MTXM values, and every value's group must be one the tileset has.
// ms is how long the picture takes to go out of the game that is running.
inline std::string MapPushMessage(const std::string &tileset, int width, int height,
                                  const std::string &tilesB64, int ms, const std::string &sceneJson) {
  std::string msg = "{\"cmd\": \"map_push\", \"tileset\": " + JsonQuote(tileset) +
                    ", \"width\": " + std::to_string(width) + ", \"height\": " + std::to_string(height) +
                    ", \"ms\": " + std::to_string(ms);
  if (!tilesB64.empty())
    msg += ", \"tiles_b64\": " + JsonQuote(tilesB64);
  if (!sceneJson.empty())
    msg += ", \"scene\": " + sceneJson;
  return msg + "}";
}

// map_load: a map file on the player's machine, .scm, .scx or .hxm, by its full path or by a name
// Hyperion looks for as the game does ("maps\(2)Volcanis.scm" in the StarCraft folder). With ums
// it plays as Use Map Settings, with the map's own players and triggers; otherwise as melee.
inline std::string MapLoadMessage(const std::string &path, bool ums, int ms, const std::string &sceneJson) {
  std::string msg = "{\"cmd\": \"map_load\", \"path\": " + JsonQuote(path) +
                    ", \"ums\": " + (ums ? "true" : "false") + ", \"ms\": " + std::to_string(ms);
  if (!sceneJson.empty())
    msg += ", \"scene\": " + sceneJson;
  return msg + "}";
}

// cutscene_play: the steps of a scene, as the text of a JSON array. hold holds the player's keys
// and clicks, skip lets Escape end it, rest eases the picture back to the game's at the end.
inline std::string CutscenePlayMessage(const std::string &stepsJsonArray, bool hold, bool skip, bool rest) {
  return "{\"cmd\": \"cutscene_play\", \"steps\": " + (stepsJsonArray.empty() ? "[]" : stepsJsonArray) +
         ", \"hold\": " + (hold ? "true" : "false") + ", \"skip\": " + (skip ? "true" : "false") +
         ", \"rest\": " + (rest ? "true" : "false") + "}";
}

// ---------------------------------------------------------------------------
// The connection.

// The channel's key, which Hyperion makes each time the channel starts and writes to
// %LOCALAPPDATA%\Hyperion\control-<port>.key, a file only this user can read; HYPERION_CONTROL_KEY
// instead when it is set, for a channel reached through a tunnel. Empty when there is neither.
inline std::wstring ChannelKey(int port) {
  wchar_t sz[256];
  DWORD n = GetEnvironmentVariableW(L"HYPERION_CONTROL_KEY", sz, 256);
  if (n > 0 && n < 256)
    return std::wstring(sz, n);
  n = GetEnvironmentVariableW(L"LOCALAPPDATA", sz, 256);
  if (n == 0 || n >= 256)
    return std::wstring();
  const std::wstring path = std::wstring(sz, n) + L"\\Hyperion\\control-" + std::to_wstring(port) + L".key";
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE)
    return std::wstring();
  char key[128];
  DWORD cb = 0;
  ReadFile(h, key, sizeof key, &cb, NULL);
  CloseHandle(h);
  std::wstring out;
  for (DWORD i = 0; i < cb; ++i)
    if (isxdigit(static_cast<unsigned char>(key[i])))
      out += static_cast<wchar_t>(key[i]);
  return out;
}

class Client {
 public:
  Client() : m_hSession(NULL), m_hConnect(NULL), m_hSocket(NULL), m_nPort(0), m_nNextId(1),
             m_nConnectMs(2000), m_nReceiveMs(30000) {}
  ~Client() { Close(); }

  Client(const Client &) = delete;
  Client &operator=(const Client &) = delete;

  // How long to wait for the connection and for an answer. Set before Connect. The connect wait
  // is short on purpose: nothing listening on the loopback address is refused at once, so this
  // only matters on a machine where the port is filtered.
  void SetTimeouts(int connectMs, int receiveMs) {
    m_nConnectMs = connectMs;
    m_nReceiveMs = receiveMs;
  }

  bool IsOpen() const { return m_hSocket != NULL; }
  int Port() const { return m_nPort; }

  // Open the channel. False with *pWhy when Hyperion is not running, is running without
  // --control, or answers something other than an upgrade.
  bool Connect(int port = DEFAULT_PORT, std::string *pWhy = nullptr) {
    Close();
    m_nPort = port;
    // one session for the process, with no proxy: the channel is on the loopback address, and a
    // proxy lookup would only cost time
    m_hSession = WinHttpOpen(L"hxlive-cpp", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                             WINHTTP_NO_PROXY_BYPASS, 0);
    if (m_hSession == NULL)
      return Fail(pWhy, "WinHttpOpen", GetLastError());
    WinHttpSetTimeouts(m_hSession, m_nConnectMs, m_nConnectMs, m_nReceiveMs, m_nReceiveMs);

    m_hConnect = WinHttpConnect(m_hSession, L"127.0.0.1", static_cast<INTERNET_PORT>(port), 0);
    if (m_hConnect == NULL)
      return Fail(pWhy, "WinHttpConnect", GetLastError());

    // a plain GET of / with the channel's key, which the channel answers with an upgrade
    const std::wstring target = L"/?t=" + ChannelKey(port);
    HINTERNET hRequest = WinHttpOpenRequest(m_hConnect, L"GET", target.c_str(), NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (hRequest == NULL)
      return Fail(pWhy, "WinHttpOpenRequest", GetLastError());
    // this is what turns the GET into a WebSocket handshake: WinHTTP adds Upgrade, Connection,
    // Sec-WebSocket-Key and Sec-WebSocket-Version itself, and sends no Origin header, which the
    // channel takes from a client that is not a browser; the key is what lets it in
    if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0))
      return FailRequest(pWhy, hRequest, "WinHttpSetOption(upgrade)", GetLastError());
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0))
      return FailRequest(pWhy, hRequest, "WinHttpSendRequest", GetLastError());
    if (!WinHttpReceiveResponse(hRequest, NULL))
      return FailRequest(pWhy, hRequest, "WinHttpReceiveResponse", GetLastError());

    // 101 Switching Protocols, or the channel turned the handshake down and says why in the body
    DWORD dwStatus = 0, cbStatus = sizeof dwStatus;
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &dwStatus, &cbStatus, WINHTTP_NO_HEADER_INDEX);
    if (dwStatus != 101) {
      std::string body = ReadBody(hRequest);
      while (!body.empty() && (body.back() == '\n' || body.back() == '\r'))
        body.pop_back();
      WinHttpCloseHandle(hRequest);
      Close();
      detail::Say(pWhy, "the channel answered " + std::to_string(dwStatus) +
                            " instead of upgrading" + (body.empty() ? "" : ": " + body));
      return false;
    }

    // the handshake is done; from here the WebSocket handle carries the messages and the request
    // handle is finished with. The session and connection handles stay open under it.
    m_hSocket = WinHttpWebSocketCompleteUpgrade(hRequest, 0);
    const DWORD dwUpgrade = GetLastError();
    WinHttpCloseHandle(hRequest);
    if (m_hSocket == NULL)
      return Fail(pWhy, "WinHttpWebSocketCompleteUpgrade", dwUpgrade);
    return true;
  }

  // One message, and its answer. jsonMessage is the text of a JSON object, from a builder above
  // or from the editor's own JSON library. An "id" is put in as the object's first member unless
  // the text has one already, and the answer is matched to it, so the caller never has to count
  // ids. An id of your own comes back as it was sent, a whole number or a string.
  bool Call(const std::string &jsonMessage, std::string *pAnswer, std::string *pWhy = nullptr) {
    if (pAnswer != nullptr)
      pAnswer->clear();
    if (m_hSocket == NULL) {
      detail::Say(pWhy, "the channel is not open");
      return false;
    }
    const size_t brace = jsonMessage.find('{');
    if (brace == std::string::npos) {
      detail::Say(pWhy, "a message is one JSON object, {\"cmd\": \"...\"}");
      return false;
    }
    std::string id = AnswerValue(jsonMessage, "id");
    std::string text = jsonMessage;
    if (id.empty()) {
      id = std::to_string(m_nNextId);
      m_nNextId = m_nNextId >= 999999 ? 1 : m_nNextId + 1;
      const bool bOnly = jsonMessage.find_first_not_of(" \t\r\n", brace + 1) == jsonMessage.find('}');
      text.insert(brace + 1, "\"id\": " + id + (bOnly ? "" : ", "));
    }

    const DWORD dwError = WinHttpWebSocketSend(m_hSocket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                               text.data(), static_cast<DWORD>(text.size()));
    if (dwError != ERROR_SUCCESS)
      return Fail(pWhy, "WinHttpWebSocketSend", dwError);

    // the answer, which may come in several fragments; the channel answers one message at a time
    // and in order, but an answer under another id is skipped rather than handed back as this one
    for (int nSkipped = 0; nSkipped < 8; ++nSkipped) {
      std::string answer;
      if (!Receive(&answer, pWhy))
        return false;
      if (AnswerValue(answer, "id") == id) {
        if (pAnswer != nullptr)
          *pAnswer = answer;
        return true;
      }
    }
    detail::Say(pWhy, "no answer came back under id " + id);
    return false;
  }

  void Close() {
    if (m_hSocket != NULL) {
      WinHttpWebSocketClose(m_hSocket, static_cast<USHORT>(WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS), NULL, 0);
      WinHttpCloseHandle(m_hSocket);
      m_hSocket = NULL;
    }
    if (m_hConnect != NULL) {
      WinHttpCloseHandle(m_hConnect);
      m_hConnect = NULL;
    }
    if (m_hSession != NULL) {
      WinHttpCloseHandle(m_hSession);
      m_hSession = NULL;
    }
  }

 private:
  // One whole message off the socket. WinHTTP hands back a piece at a time and says which kind it
  // is: a fragment type means more of the same message follows, a message type means that was the
  // last piece of it.
  bool Receive(std::string *pText, std::string *pWhy) {
    pText->clear();
    char buf[16384];
    for (;;) {
      DWORD cbRead = 0;
      WINHTTP_WEB_SOCKET_BUFFER_TYPE eType = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
      const DWORD dwError = WinHttpWebSocketReceive(m_hSocket, buf, sizeof buf, &cbRead, &eType);
      if (dwError != ERROR_SUCCESS)
        return Fail(pWhy, "WinHttpWebSocketReceive", dwError);
      if (eType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
        detail::Say(pWhy, "the channel closed before answering");
        return false;
      }
      pText->append(buf, cbRead);
      if (eType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
          eType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
        return true;
    }
  }

  static std::string ReadBody(HINTERNET hRequest) {
    std::string body;
    char buf[512];
    DWORD cbRead = 0;
    while (body.size() < 2048 && WinHttpReadData(hRequest, buf, sizeof buf, &cbRead) && cbRead != 0)
      body.append(buf, cbRead);
    return body;
  }

  bool Fail(std::string *pWhy, const char *pszWhere, DWORD dwError) {
    detail::Say(pWhy, Explain(pszWhere, dwError));
    Close();
    return false;
  }

  bool FailRequest(std::string *pWhy, HINTERNET hRequest, const char *pszWhere, DWORD dwError) {
    WinHttpCloseHandle(hRequest);
    return Fail(pWhy, pszWhere, dwError);
  }

  std::string Explain(const char *pszWhere, DWORD dwError) const {
    const std::string where = std::string(" (") + pszWhere + ", WinHTTP error " + std::to_string(dwError) + ")";
    const std::string at = "127.0.0.1:" + std::to_string(m_nPort);
    switch (dwError) {
      case ERROR_WINHTTP_CANNOT_CONNECT:
      case ERROR_WINHTTP_CONNECTION_ERROR:
        return "nothing is listening on " + at + ": start Hyperion.exe --control=" +
               std::to_string(m_nPort) + " first" + where;
      case ERROR_WINHTTP_TIMEOUT:
        return "the channel on " + at + " did not answer in time" + where;
      case ERROR_WINHTTP_OPERATION_CANCELLED:
        return "the channel on " + at + " was closed under us" + where;
      default:
        return "the channel on " + at + " could not be reached" + where;
    }
  }

  HINTERNET m_hSession;
  HINTERNET m_hConnect;
  HINTERNET m_hSocket;
  int m_nPort;
  int m_nNextId;
  int m_nConnectMs;
  int m_nReceiveMs;
};

// ---------------------------------------------------------------------------
// Waiting for a map switch

// After a map_push or a map_load, wait until the new game is running: status every 250 ms until
// map_swap is "none" with state "game". False with *pWhy when the switch ended anywhere else,
// which is where map_swap_error says why, or when timeoutMs went by first. Call it only after a
// map command, since with no switch on its way the first status already says "none".
//
// A switch that fails, say over a map file the editor saved wrong, leaves Hyperion at its front
// end, not in the game that was running: the game is left before the new map is read. Its
// map_swap_error stays in status until the next switch starts; the test here is where the switch
// ended, which holds either way.
inline bool WaitForSwitch(Client &client, int timeoutMs, std::string *pWhy = nullptr) {
  const ULONGLONG from = GetTickCount64();
  std::string state, swap;
  for (;;) {
    std::string answer;
    if (!client.Call(StatusMessage(), &answer, pWhy))
      return false;
    if (!AnswerOk(answer)) {
      detail::Say(pWhy, "status: " + AnswerString(answer, "error"));
      return false;
    }
    state = AnswerString(answer, "state");
    swap = AnswerString(answer, "map_swap");
    if (swap == "none") {
      if (state == "game")
        return true;
      // the switch is over and no game is running, so it did not get there; map_swap_error holds
      // the reason when the launch itself failed
      const std::string error = AnswerString(answer, "map_swap_error");
      detail::Say(pWhy, error.empty() ? "the switch ended with the game in \"" + state + "\"" : error);
      return false;
    }
    if (GetTickCount64() - from >= static_cast<ULONGLONG>(timeoutMs < 0 ? 0 : timeoutMs)) {
      detail::Say(pWhy, "the map switch was still \"" + swap + "\" after " +
                            std::to_string(timeoutMs) + " ms");
      return false;
    }
    Sleep(250);
  }
}

}  // namespace hxlive

#endif  // HXLIVE_H
