// port_replaykraken.h -- Replay Kraken's self-hosted HTTP protocol and the
// source-aware match model used by the native replay library.
#ifndef PORT_REPLAYKRAKEN_H
#define PORT_REPLAYKRAKEN_H

#include <cstdint>
#include <string>
#include <vector>

namespace ReplayKraken {

// The current server accepts a 2 MiB JSON body. Keep replay input bounded before
// Base64 encoding; the final encoded body is checked exactly as well.
inline constexpr size_t kMaxRequestBytes = 2 * 1024 * 1024;
inline constexpr size_t kMaxReplayBytes = 1500 * 1024;
inline constexpr size_t kMaxResponseBytes = 8 * 1024 * 1024;
inline constexpr size_t kPageSize = 50;
inline constexpr size_t kMaxPageSize = 200;

struct Endpoint {
  std::string input;   // The trimmed setting, for display only.
  std::string origin;  // Scheme, host and non-default port; no path or trailing slash.
  std::string api;     // origin plus an optional base path and exactly one /api.
  bool bLoopbackHttp = false;
};

// Accepts https://host[/base][/api] and explicit loopback HTTP. Credentials,
// query strings, fragments and non-loopback plain HTTP are rejected.
bool EndpointParse(const std::string &text, Endpoint *pOut, std::string *pError);
std::string UrlEncode(const std::string &text);
std::string MatchPath(const Endpoint &server, const std::string &uuid);
bool MatchUuidValid(const std::string &uuid);
std::string MatchupKey(const std::string &matchup);
std::string SafeFilePart(const std::string &text, size_t maxBytes = 64);

struct Player {
  std::string name;
  std::string race;
  int64_t slot = 0;
  std::string result;  // Empty means not authoritative; list responses normally say "unknown".
  std::string type;    // "human", "computer", or empty for unknown.
  std::string discordId;
};

struct Match {
  std::string uuid;
  std::string serverOrigin;  // Normalized partition key, persisted with every cached row.
  std::string serverApi;     // The only root from which this row may be downloaded.
  std::string gameVersion;
  std::string map;
  std::string gameType;
  int64_t durationFrames = 0;
  std::string matchup;
  std::string uploadedBy;
  std::string uploadedAt;
  std::vector<Player> players;
};

struct MatchPage {
  std::vector<Match> matches;
  int64_t total = 0;
  int64_t limit = 0;
  int64_t offset = 0;
  int64_t nextOffset = 0;  // Raw server offset + raw rows consumed, not unique merged rows.
  bool bComplete = false;
};

struct Query {
  std::string player;     // Server-side archive filter.
  std::string race;       // Server-side archive filter.
  std::string result;     // Server-side archive filter.
  std::string matchup;    // Server-side, direction-insensitive (including mirrors).
  std::string from;       // Server-side upload-time filter.
  std::string to;         // Server-side upload-time filter.
  std::string followedBy; // "@me" requires the bearer token; other names are public.
  std::string search;     // Fetched-rows-only map/player/uploader/version/UUID search.
  bool bFavourites = false;  // Fetched-rows-only library filter.
  bool bStarted = false;     // Fetched-rows-only library filter.
  bool bNotes = false;       // Fetched-rows-only library filter.
};

std::string ListUrl(const Endpoint &server, const Query &query, size_t offset, size_t limit = kPageSize);
bool ParsePage(const std::string &json, const Endpoint &server, MatchPage *pOut, std::string *pError);
// Merges a page by UUID without duplicating rows already fetched. Existing rows
// are refreshed in place, preserving their position and selection identity.
void MergePage(MatchPage *pInto, const MatchPage &page);

struct UploadPlayer {
  std::string name;
  std::string race;
  int64_t slot = 0;
  std::string result;  // Omitted when empty; the server stores "unknown".
  std::string type;    // Omitted when empty.
};

struct UploadMetadata {
  std::string gameVersion;
  std::string map;
  std::string gameType;
  int64_t durationFrames = 0;
  std::string matchup;
  std::vector<UploadPlayer> players;
};

std::string Base64(const std::vector<unsigned char> &bytes);
// Builds the exact camelCase request. replayFile and matchUuid are always sent;
// optional artifacts and unknown player results/types are omitted, never guessed.
// False means the replay or final JSON exceeds the unmodified server's body limit.
bool UploadBody(const std::string &uuid, const std::string &gameVersion,
                const UploadMetadata &meta, const std::vector<unsigned char> &replay,
                std::string *pOut, std::string *pError);
bool ParseCreatedUuid(const std::string &json, const std::string &expected, std::string *pError);

enum class Failure { None, Transient, Permanent, Unauthorized, Conflict, Unsupported, Cancelled };
struct RetryDecision {
  Failure kind = Failure::None;
  int64_t retryAfterMs = -1;
};
RetryDecision ClassifyHttp(long status, const std::string &retryAfter);
int64_t BackoffMs(unsigned attempt, int64_t retryAfterMs, uint32_t jitter);
// Returns false for HTTPS-to-HTTP and for any redirect that would carry a token
// to another origin. Same-origin redirects retain it; public requests may follow
// a cross-origin HTTPS redirect without credentials.
bool RedirectAllowed(const Endpoint &from, const std::string &nextUrl, bool bCredentials,
                     Endpoint *pNext, std::string *pError);

std::string UuidNew();
std::string Sha256Hex(const void *pData, size_t bytes);
std::string ContentId(const std::vector<unsigned char> &bytes);
std::string ServerRecordId(const Endpoint &server, const std::string &uuid);

}  // namespace ReplayKraken

#endif
