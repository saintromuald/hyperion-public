// port_sbmanifest.cpp -- parses and proves a server-owned ShieldBattery launch.
#include "port_sbmanifest.h"
#include "../data/port_moddata.h"
#include "../lan/port_lanclient.h"
#include "../platform/port_json.h"
#include "../platform/port_util.h"
#include "../../hyperion/version.h"
#include "../../gen/globals117.h"
#include "../../h/Storm.h"
#include "../../h/gluChat.h"
#include "../../h/hxsim.h"
#include "../../h/net_chat_cmds.h"
#include "../../h/net_player.h"
#include "../../h/net_slot.h"
#include "../../h/playerlimits.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

extern PlayerTable<PlayerData, PlayerTagNetData> gNetPlayerData_g;

namespace {

SbManifest g_manifest;
bool g_active = false;
std::string g_error;

uint32_t Rol(uint32_t v, int n) { return v >> n | v << (32 - n); }
uint32_t Ch(uint32_t a, uint32_t b, uint32_t c) { return a & b ^ ~a & c; }
uint32_t Maj(uint32_t a, uint32_t b, uint32_t c) { return a & b ^ a & c ^ b & c; }
uint32_t S0(uint32_t v) { return Rol(v, 2) ^ Rol(v, 13) ^ Rol(v, 22); }
uint32_t S1(uint32_t v) { return Rol(v, 6) ^ Rol(v, 11) ^ Rol(v, 25); }
uint32_t s0(uint32_t v) { return Rol(v, 7) ^ Rol(v, 18) ^ v >> 3; }
uint32_t s1(uint32_t v) { return Rol(v, 17) ^ Rol(v, 19) ^ v >> 10; }

class Sha256 {
public:
  Sha256() {
    static const uint32_t initial[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(h, initial, sizeof h);
  }

  void Add(const uint8_t *p, size_t n) {
    cb += n;
    while (n > 0) {
      const size_t take = 64 - nBlock < n ? 64 - nBlock : n;
      memcpy(block + nBlock, p, take);
      nBlock += take;
      p += take;
      n -= take;
      if (nBlock == 64) {
        Compress(block);
        nBlock = 0;
      }
    }
  }

  void Finish(uint8_t out[32]) {
    const uint64_t bits = cb * 8;
    const uint8_t one = 0x80, zero = 0;
    Add(&one, 1);
    while (nBlock != 56)
      Add(&zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++)
      len[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
    Add(len, 8);
    for (int i = 0; i < 32; i++)
      out[i] = static_cast<uint8_t>(h[i / 4] >> (24 - 8 * (i % 4)));
  }

private:
  uint32_t h[8];
  uint8_t block[64];
  size_t nBlock = 0;
  uint64_t cb = 0;

  void Compress(const uint8_t *p) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
      w[i] = static_cast<uint32_t>(p[i * 4]) << 24 | static_cast<uint32_t>(p[i * 4 + 1]) << 16 |
             static_cast<uint32_t>(p[i * 4 + 2]) << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++)
      w[i] = w[i - 16] + s0(w[i - 15]) + w[i - 7] + s1(w[i - 2]);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
      const uint32_t t1 = hh + S1(e) + Ch(e, f, g) + k[i] + w[i];
      const uint32_t t2 = S0(a) + Maj(a, b, c);
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
};

std::string Hex(const uint8_t *p, size_t n) {
  static const char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(n * 2);
  for (size_t i = 0; i < n; i++) {
    out += kHex[p[i] >> 4];
    out += kHex[p[i] & 15];
  }
  return out;
}

std::string ModFingerprint() {
  return Hex(ModDataFingerprint(), 20);
}

bool Fail(std::string *why, const std::string &what) {
  if (why)
    *why = what;
  return false;
}

bool IsUint(const PortJson &j) {
  return j.kind == PortJson::NUMBER && j.d >= 0 && j.d <= 4294967295.0 && floor(j.d) == j.d;
}

bool Uint(const PortJson &j, uint32_t *out) {
  if (!IsUint(j))
    return false;
  *out = static_cast<uint32_t>(j.d);
  return true;
}

bool Int(const PortJson &j, int *out) {
  uint32_t u;
  if (!Uint(j, &u) || u > 0x7fffffff)
    return false;
  *out = static_cast<int>(u);
  return true;
}

bool HexOf(const std::string &s, size_t n, bool lowerOnly = true) {
  if (s.size() != n)
    return false;
  for (char c : s) {
    const bool digit = c >= '0' && c <= '9';
    const bool lower = c >= 'a' && c <= 'f';
    const bool upper = c >= 'A' && c <= 'F';
    if (!digit && !lower && (lowerOnly || !upper))
      return false;
  }
  return true;
}

bool NameOf(const std::string &s) {
  return !s.empty() && s.size() <= 24 && s.find('\0') == std::string::npos;
}

bool Validate(const SbManifest &m, const std::string &mapPath, std::string *why) {
  if (m.protocol != 1)
    return Fail(why, "hyperion.protocol must be 1");
  if (m.gameId.empty())
    return Fail(why, "gameId is required");
  if (!NameOf(m.name))
    return Fail(why, "name must contain 1 to 24 UTF-8 bytes without NULs");
  if (m.gameType != "oneVOne" && m.gameType != "topVBottom")
    return Fail(why, "only oneVOne and topVBottom are supported");
  const int expectedPlayers = m.gameType == "oneVOne" ? 2 : m.gameSubType * 2;
  if ((m.gameType == "oneVOne" && m.gameSubType != 0) ||
      (m.gameType == "topVBottom" && m.gameSubType != 2 && m.gameSubType != 3))
    return Fail(why, "gameSubType must be 0 for 1v1 or 2/3 for team play");
  if (m.speed < 0 || m.speed > 6)
    return Fail(why, "speed must be between 0 and 6");
  if (!m.disableAllianceChanges)
    return Fail(why, "disableAllianceChanges must be true");
  if (m.localUserId == 0)
    return Fail(why, "localUserId must be positive");
  if (m.slots.size() != static_cast<size_t>(expectedPlayers))
    return Fail(why, "the slot count does not match the game type");
  for (size_t i = 0; i < m.slots.size(); ++i) {
    const SbManifestSlot &s = m.slots[i];
    if (s.rpSlot != i || s.stormId != i || s.bwPlayerId != i)
      return Fail(why, "slot ids must be contiguous from 0");
    const uint32_t teamSize = m.gameType == "topVBottom" ? static_cast<uint32_t>(m.gameSubType) : 1;
    if (s.teamId != i / teamSize)
      return Fail(why, "teamId does not match the canonical team layout");
    if (s.userId == 0)
      return Fail(why, "userIds must be positive and unique");
    for (size_t j = 0; j < i; ++j)
      if (m.slots[j].userId == s.userId)
        return Fail(why, "userIds must be positive and unique");
    if (!NameOf(s.name))
      return Fail(why, "slot names must contain 1 to 24 UTF-8 bytes without NULs");
    if (s.race != "z" && s.race != "t" && s.race != "p" && s.race != "r")
      return Fail(why, "an invalid race was supplied");
    if (s.host != (i == 0))
      return Fail(why, "only rpSlot 0 may be the host");
  }
  bool localFound = false;
  for (const SbManifestSlot &slot : m.slots)
    localFound = localFound || slot.userId == m.localUserId;
  if (!localFound)
    return Fail(why, "localUserId is not in the roster");
  if (m.mapFormat != "scm" && m.mapFormat != "scx")
    return Fail(why, "map format must be scm or scx");
  if (!HexOf(m.mapHash, 64))
    return Fail(why, "map hash must be 64 lowercase hex characters");
  if (mapPath.empty())
    return Fail(why, "map path is required");
  if (m.adapterProtocol != 1)
    return Fail(why, "adapterProtocol must be 1");
  if (m.engineBuild != HYPERION_VERSION_TEXT)
    return Fail(why, "engine build does not match this Hyperion");
  if (m.simRevision != HX_SIM_REVISION)
    return Fail(why, "simulation revision does not match this Hyperion");
  if (m.rules != "classic" && m.rules != "hyperion")
    return Fail(why, "rules must be classic or hyperion");
  if ((m.rules == "classic" && m.antiMeta != 0) || (m.rules == "hyperion" && m.antiMeta != 1))
    return Fail(why, "Rule 2 requires exactly antiMeta: 1; Rule 1 permits no mutators");
  if (m.modId != ModDataId())
    return Fail(why, "active mod does not match the manifest");
  if (!HexOf(m.modFingerprint, 40) || m.modFingerprint != ModFingerprint())
    return Fail(why, "mod fingerprint does not match this installation");
  if (!HexOf(m.dataFingerprint, 8, false) || lstrcmpiA(m.dataFingerprint.c_str(), LanFingerprint().c_str()) != 0)
    return Fail(why, "game-data fingerprint does not match this installation");
  if (m.rollback)
    return Fail(why, "rollback is not supported");

  std::vector<uint8_t> bytes;
  if (!PortReadFile(mapPath, &bytes))
    return Fail(why, "map file cannot be read");
  std::vector<uint8_t> input;
  input.insert(input.end(), m.mapFormat.begin(), m.mapFormat.end());
  input.insert(input.end(), bytes.begin(), bytes.end());
  Sha256 hash;
  hash.Add(input.data(), input.size());
  uint8_t digest[32];
  hash.Finish(digest);
  const std::string actual = Hex(digest, sizeof digest);
  if (actual != m.mapHash)
    return Fail(why, "map hash does not match the downloaded map");
  return true;
}

bool Load(const std::string &json, const std::string &mapPath, std::string *why) {
  PortJson root;
  std::string error;
  if (!PortJsonParse(json.c_str(), &root, &error) || root.kind != PortJson::OBJECT) {
    if (why)
      *why = "launch JSON is invalid: " + error;
    return false;
  }
  const PortJson &h = root["hyperion"];
  if (h.kind == PortJson::NONE)
    return false;
  if (h.kind != PortJson::OBJECT) {
    if (why)
      *why = "hyperion must be an object";
    return false;
  }

  SbManifest m;
  if (!Int(h["protocol"], &m.protocol))
    return Fail(why, "hyperion.protocol must be an integer");
  m.gameId = h["gameId"].Str();
  m.name = h["name"].Str();
  m.gameType = h["gameType"].Str();
  if (!Int(h["gameSubType"], &m.gameSubType) || !Uint(h["seed"], &m.seed) || !Int(h["speed"], &m.speed) ||
      !Uint(h["localUserId"], &m.localUserId))
    return Fail(why, "numeric manifest fields must be unsigned integers");
  m.disableAllianceChanges = h["disableAllianceChanges"].kind == PortJson::BOOL && h["disableAllianceChanges"].b;

  const PortJson &slots = h["slots"];
  if (slots.kind != PortJson::ARRAY)
    return Fail(why, "slots must be an array");
  for (size_t i = 0; i < slots.size(); ++i) {
    const PortJson &j = slots.At(i);
    SbManifestSlot s;
    if (!Uint(j["rpSlot"], &s.rpSlot) || !Uint(j["stormId"], &s.stormId) ||
        !Uint(j["bwPlayerId"], &s.bwPlayerId) || !Uint(j["userId"], &s.userId) ||
        !Uint(j["teamId"], &s.teamId))
      return Fail(why, "slot ids must be unsigned integers");
    s.name = j["name"].Str();
    s.race = j["race"].Str();
    s.host = j["host"].kind == PortJson::BOOL && j["host"].b;
    m.slots.push_back(s);
  }

  const PortJson &map = h["map"];
  m.mapFormat = map["format"].Str();
  m.mapHash = map["hash"].Str();
  m.mapPath = map["path"].Str().empty() ? mapPath : map["path"].Str();
  // The launch argument is the sidecar's per-client map location. The manifest's
  // path is a fallback for direct launches; never let it redirect verification
  // away from the file the game is actually about to load.
  const std::string &verifyPath = mapPath.empty() ? m.mapPath : mapPath;
  const PortJson &c = h["compatibility"];
  if (!Int(c["adapterProtocol"], &m.adapterProtocol) || !Int(c["simRevision"], &m.simRevision))
    return Fail(why, "compatibility versions must be integers");
  m.engineBuild = c["engineBuild"].Str();
  m.rules = c["rules"].Str();
  m.modId = c["modId"].Str();
  m.modFingerprint = c["modFingerprint"].Str();
  m.dataFingerprint = c["dataFingerprint"].Str();
  m.rollback = c["rollback"].kind == PortJson::BOOL && c["rollback"].b;

  const PortJson &mutators = h["mutators"];
  if (mutators.kind != PortJson::NONE) {
    if (mutators.kind != PortJson::OBJECT || mutators.size() != 1 ||
        mutators.members[0].first != "antiMeta" || !Int(mutators.members[0].second, &m.antiMeta) ||
        m.antiMeta != 1)
      return Fail(why, "mutators must contain exactly antiMeta: 1");
  }

  const PortJson &roster = root["roster"];
  if (roster.kind != PortJson::ARRAY || roster.size() != m.slots.size())
    return Fail(why, "the authenticated roster must match the manifest slots");
  for (const SbManifestSlot &slot : m.slots) {
    bool found = false;
    for (size_t i = 0; i < roster.size(); ++i) {
      const PortJson &entry = roster.At(i);
      uint32_t rpSlot = 0, userId = 0;
      if (!Uint(entry["slot"], &rpSlot) || !Uint(entry["userId"], &userId))
        return Fail(why, "authenticated roster ids must be unsigned integers");
      if (rpSlot == slot.rpSlot) {
        if (userId != slot.userId)
          return Fail(why, "the authenticated roster does not match the manifest slots");
        found = true;
        break;
      }
    }
    if (!found)
      return Fail(why, "the authenticated roster does not match the manifest slots");
  }
  if (root["rollback"].kind != PortJson::BOOL || root["rollback"].b)
    return Fail(why, "rollback is not supported");
  if (!Validate(m, verifyPath, why))
    return false;
  g_manifest = m;
  g_active = true;
  return true;
}

}  // namespace

bool SbManifestLoad(const std::string &json, const std::string &mapPath, std::string *why) {
  g_active = false;
  g_manifest = SbManifest();
  g_error.clear();
  std::string discarded;
  std::string *message = why != nullptr ? why : &discarded;
  message->clear();
  const bool loaded = Load(json, mapPath, message);
  g_error = *message;
  return loaded;
}

void SbManifestClear() {
  g_active = false;
  g_manifest = SbManifest();
  g_error.clear();
}

bool SbManifestActive() {
  return g_active;
}

const std::string &SbManifestError() {
  return g_error;
}

const SbManifest &SbManifestGet() {
  static const SbManifest empty;
  return g_active ? g_manifest : empty;
}

const SbManifestSlot *SbManifestLocalSlot() {
  if (!g_active)
    return nullptr;
  for (const SbManifestSlot &s : g_manifest.slots)
    if (s.userId == g_manifest.localUserId)
      return &s;
  return nullptr;
}

uint32_t SbManifestSeed() {
  return g_active ? g_manifest.seed : 0;
}

std::string SbManifestCompatibilityJson() {
  return "{\"adapterProtocol\":1,\"engineBuild\":" + PortJsonQuote(HYPERION_VERSION_TEXT) +
         ",\"simRevision\":" + std::to_string(HX_SIM_REVISION) +
         ",\"rules\":\"classic\",\"modId\":" + PortJsonQuote(ModDataId()) +
         ",\"modFingerprint\":" + PortJsonQuote(ModFingerprint()) +
         ",\"dataFingerprint\":" + PortJsonQuote(LanFingerprint()) +
         ",\"rollback\":false}";
}

void SbManifestCompatibilityPrint() {
  const std::string identity = SbManifestCompatibilityJson();
  fprintf(stdout, "%s\n", identity.c_str());
  fflush(stdout);
}

bool SbManifestApplyLobby() {
  const SbManifestSlot *local = SbManifestLocalSlot();
  if (!g_active || local == nullptr || g_LocalStormID >= 8 || local->stormId != g_LocalStormID)
    return false;

  PlayerClear(gPlayerData);
  for (int i = 0; i < 8; ++i) {
    gPlayerData[i].dwPlayerID = i;
    gPlayerData[i].dwStormID = 0xFFFFFFFFu;
    gPlayerData[i].nType = 6;  // open
    gPlayerData[i].nRace = 6;  // random, as an unoccupied lobby slot has it
  }
  for (const SbManifestSlot &slot : g_manifest.slots) {
    if (slot.bwPlayerId >= 8 || slot.stormId >= 8)
      return false;
    PlayerData &out = gPlayerData[slot.bwPlayerId];
    out.dwPlayerID = slot.bwPlayerId;
    out.dwStormID = slot.stormId;
    out.nType = 2;  // human
    out.nRace = slot.race == "z" ? 0 : slot.race == "t" ? 1 : slot.race == "p" ? 2 : 6;
    out.nTeam = g_manifest.gameType == "topVBottom" ? static_cast<BYTE>(slot.teamId + 1) : 0;
    SStrCopy(out.szName, slot.name.c_str(), sizeof out.szName);
    // The lobby table supplies open destinations for start-slot reconstruction. The player table
    // alone marks the seated human; both retain the authenticated identity, race and team.
    PlayerData &lobby = gNetPlayerData_g[slot.bwPlayerId];
    lobby.dwPlayerID = slot.bwPlayerId;
    lobby.dwStormID = slot.stormId;
    lobby.nType = 6;
    lobby.nRace = out.nRace;
    lobby.nTeam = out.nTeam;
    SStrCopy(lobby.szName, slot.name.c_str(), sizeof lobby.szName);
  }

  net_slot_init(g_LocalStormID);
  if (g_LocalNationID != local->bwPlayerId || g_LocalHumanID != local->bwPlayerId)
    return false;
  for (const SbManifestSlot &slot : g_manifest.slots)
    net_player_set_data(static_cast<BYTE>(slot.stormId), 0, 1, 5);
  gbIsGameCreator_g = local->host ? 1 : 0;
  // The roster is already complete, so no slot-setup or version exchange is
  // needed. State 8 is the native seed handler's expected pre-init state.
  sub_470910(8);
  sub_451790();
  return true;
}
