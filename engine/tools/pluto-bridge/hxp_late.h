#ifndef PLUTO_BRIDGE_HXP_LATE_H
#define PLUTO_BRIDGE_HXP_LATE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

// Little-endian HXP2/HXPF/HXP3/HXP4 codec and late-state application.
// HXP4 is a new format, never an HXP3 frame with trailing bytes reinterpreted.
namespace pluto_late {

constexpr std::uint32_t kHxpf = 0x50465848u;
constexpr std::uint32_t kHxp2 = 0x32505848u;
constexpr std::uint32_t kHxp3 = 0x33505848u;
constexpr std::uint32_t kHxp4 = 0x34505848u;
constexpr std::size_t kLegacyHeaderBytes = 24;
constexpr std::size_t kHxp4HeaderBytes = 32;
constexpr std::size_t kUnitBytes = 40;
constexpr std::size_t kHxp3ExtraBytes = 64;
constexpr std::size_t kHxp4ExtraBytes = 78;
constexpr std::size_t kPlayerBytes = 28;
constexpr std::size_t kPlayerTechBytes = 119;
constexpr std::size_t kMaxUnits = 9999;
constexpr std::size_t kMaxPlayers = 12;
constexpr std::size_t kMaxMapSide = 256;
constexpr std::uint16_t kUnitNone = 228;
constexpr std::uint8_t kTechNone = 44;
constexpr std::uint8_t kUpgradeNone = 61;
constexpr std::uint8_t kResearchTechOrder = 75;
constexpr std::uint8_t kUpgradeOrder = 76;

struct Unit {
  std::uint32_t id = 0, type = 0, player = 0, x = 0, y = 0;
  std::uint32_t hp = 0, shields = 0, energy = 0, order = 0;
  std::uint8_t visible = 0, completed = 0, direction = 0, generation = 0;
};

#pragma pack(push, 1)
struct UnitExtra {
  std::uint32_t statusFlags = 0, orderTargetId = 0, moveTargetId = 0;
  std::uint32_t connectedId = 0, currentBuildUnitId = 0;
  std::int32_t speedX = 0, speedY = 0;
  std::uint32_t buildType = kUnitNone, previousType = 0, resourceCount = 0;
  std::uint32_t orderUnitType = 0;
  std::uint16_t moveTargetX = 0, moveTargetY = 0, orderTargetX = 0, orderTargetY = 0;
  std::uint16_t remainingBuildTime = 0;
  std::uint8_t buildQueueSlot = 0, orderState = 0, groundCooldown = 0;
  std::uint8_t airCooldown = 0, spellCooldown = 0, movementFlags = 0;
  std::uint8_t currentDirection = 0, orderFlags = 0, detected = 0, mainOrderTimer = 0;

  // HXP4 only. HXP3 decoding leaves these at their explicit legacy defaults.
  // The research fields carry only the active ResearchTech/Upgrade union arm;
  // other orders require the neutral defaults shown here (validated below).
  std::array<std::uint16_t, 5> buildQueue{kUnitNone, kUnitNone, kUnitNone, kUnitNone, kUnitNone};
  std::uint16_t remainingResearchTime = 0;
  std::uint8_t techType = kTechNone;
  std::uint8_t upgradeType = kUpgradeNone;
};
#pragma pack(pop)
static_assert(sizeof(UnitExtra) == kHxp4ExtraBytes, "packed HXP4 unit-extra bytes");
static_assert(offsetof(UnitExtra, buildQueue) == 64, "HXP4 buildQueue offset");
static_assert(offsetof(UnitExtra, remainingResearchTime) == 74, "HXP4 research timer offset");
static_assert(offsetof(UnitExtra, techType) == 76, "HXP4 tech offset");
static_assert(offsetof(UnitExtra, upgradeType) == 77, "HXP4 upgrade offset");

struct Player {
  std::uint32_t race = 0, minerals = 0, gas = 0, supplyTotal = 0, supplyUsed = 0;
  std::uint32_t startX = 0, startY = 0;
};

struct PlayerTech {
  std::array<std::uint8_t, 44> hasResearched{};
  std::array<std::uint8_t, 61> upgradeLevel{};
  std::array<std::uint8_t, 6> techInProgress{};
  std::array<std::uint8_t, 8> upgradeInProgress{};
};
static_assert(sizeof(PlayerTech) == kPlayerTechBytes, "packed HXP4 player-tech bytes");

// Native producer input. The pointer names CUnit::wBuildQueue; order is the
// widened native order id, and tech/upgrade stay wide until after range checks
// so a mod id can never be silently truncated into the classic byte protocol.
struct NativeUnitProduction {
  const std::uint16_t *buildQueue = nullptr;
  std::uint8_t buildQueueSlot = 0;
  std::uint32_t order = 0;
  std::uint32_t techType = kTechNone;
  std::uint32_t upgradeType = kUpgradeNone;
  std::uint16_t remainingResearchTime = 0;
};

inline bool PopulateUnitProduction(const NativeUnitProduction &native, UnitExtra *out,
                                   std::string *error) {
  if (error != nullptr) error->clear();
  if (out == nullptr || native.buildQueue == nullptr) {
    if (error != nullptr) *error = "null native production input";
    return false;
  }
  if (native.buildQueueSlot >= 5) {
    if (error != nullptr) *error = "native build queue slot is outside 0..4";
    return false;
  }
  for (std::size_t i = 0; i < 5; ++i) {
    if (native.buildQueue[i] > kUnitNone) {
      if (error != nullptr) *error = "native build queue type exceeds classic HXP4 range";
      return false;
    }
    out->buildQueue[i] = native.buildQueue[i];
  }
  out->buildQueueSlot = native.buildQueueSlot;
  out->buildType = out->buildQueue[native.buildQueueSlot];
  out->remainingResearchTime = 0;
  out->techType = kTechNone;
  out->upgradeType = kUpgradeNone;
  if (native.order == kResearchTechOrder) {
    if (native.techType > kTechNone) {
      if (error != nullptr) *error = "research tech id exceeds classic HXP4 range";
      return false;
    }
    out->remainingResearchTime = native.remainingResearchTime;
    out->techType = static_cast<std::uint8_t>(native.techType);
  } else if (native.order == kUpgradeOrder) {
    if (native.upgradeType > kUpgradeNone) {
      if (error != nullptr) *error = "research upgrade id exceeds classic HXP4 range";
      return false;
    }
    out->remainingResearchTime = native.remainingResearchTime;
    out->upgradeType = static_cast<std::uint8_t>(native.upgradeType);
  }
  return true;
}

// Produce player-tech in exactly the same order as the existing HXP player
// records. Unoccupied and neutral output slots remain zero-initialized.
inline bool ReorderPlayerTech(const std::vector<PlayerTech> &native,
                              const std::vector<int> &orderedPlayers,
                              std::size_t playerCount, std::vector<PlayerTech> *out,
                              std::string *error) {
  if (error != nullptr) error->clear();
  if (out == nullptr || orderedPlayers.size() > playerCount) {
    if (error != nullptr) *error = "invalid player-tech output or ordering";
    return false;
  }
  out->assign(playerCount, PlayerTech{});
  for (std::size_t i = 0; i < orderedPlayers.size(); ++i) {
    const int slot = orderedPlayers[i];
    if (slot < 0 || static_cast<std::size_t>(slot) >= native.size()) {
      if (error != nullptr) *error = "native player-tech slot is out of range";
      return false;
    }
    (*out)[i] = native[static_cast<std::size_t>(slot)];
  }
  return true;
}

struct Frame {
  std::uint32_t magic = kHxp3;
  std::uint32_t frame = 0, mapWidth = 1, mapHeight = 1;
  std::vector<Unit> units;
  std::vector<UnitExtra> extras;
  std::vector<Player> players;
  std::vector<PlayerTech> playerTech;
  std::vector<std::uint8_t> tiles, seen, explored;
};

inline bool Fail(std::string *error, const std::string &text) {
  if (error != nullptr)
    *error = text;
  return false;
}

inline std::uint16_t Read16(const std::uint8_t *p) {
  return static_cast<std::uint16_t>(p[0]) |
         static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8);
}
inline std::uint32_t Read32(const std::uint8_t *p) {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}
inline void Append16(std::vector<std::uint8_t> *out, std::uint16_t v) {
  out->push_back(static_cast<std::uint8_t>(v));
  out->push_back(static_cast<std::uint8_t>(v >> 8));
}
inline void Append32(std::vector<std::uint8_t> *out, std::uint32_t v) {
  out->push_back(static_cast<std::uint8_t>(v));
  out->push_back(static_cast<std::uint8_t>(v >> 8));
  out->push_back(static_cast<std::uint8_t>(v >> 16));
  out->push_back(static_cast<std::uint8_t>(v >> 24));
}

class Cursor {
 public:
  Cursor(const std::vector<std::uint8_t> &bytes, std::size_t at, std::string *error)
      : bytes_(bytes), at_(at), error_(error) {}
  const std::uint8_t *Take(std::size_t count, const char *what) {
    if (count > bytes_.size() - at_) {
      Fail(error_, std::string("truncated ") + what);
      return nullptr;
    }
    const std::uint8_t *p = bytes_.data() + at_;
    at_ += count;
    return p;
  }
  std::uint8_t U8(const char *what) {
    const std::uint8_t *p = Take(1, what);
    return p == nullptr ? 0 : p[0];
  }
  std::uint16_t U16(const char *what) {
    const std::uint8_t *p = Take(2, what);
    return p == nullptr ? 0 : Read16(p);
  }
  std::uint32_t U32(const char *what) {
    const std::uint8_t *p = Take(4, what);
    return p == nullptr ? 0 : Read32(p);
  }
  std::size_t at() const { return at_; }
  bool ok() const { return error_ == nullptr || error_->empty(); }

 private:
  const std::vector<std::uint8_t> &bytes_;
  std::size_t at_;
  std::string *error_;
};

inline bool ValidateHxp4Unit(const Unit &unit, const UnitExtra &e, std::string *error) {
  if (e.buildQueueSlot >= 5) {
    if (error != nullptr) *error = "HXP4 build queue slot is outside 0..4";
    return false;
  }
  for (std::uint16_t type : e.buildQueue) {
    if (type > kUnitNone) {
      if (error != nullptr) *error = "HXP4 build queue unit type exceeds 228";
      return false;
    }
  }
  if (e.buildType > kUnitNone || e.buildType != e.buildQueue[e.buildQueueSlot]) {
    if (error != nullptr) *error = "HXP4 active buildType disagrees with the full queue";
    return false;
  }
  // CUnit's research fields share a union with pointers and hangar state.
  // HXP4 carries only the research arm; other orders use neutral defaults so
  // unrelated native pointer bytes can never be reinterpreted as research.
  if (unit.order == kResearchTechOrder) {
    if (e.techType > kTechNone || e.upgradeType != kUpgradeNone) {
      if (error != nullptr) *error = "HXP4 research tech arm is invalid";
      return false;
    }
  } else if (unit.order == kUpgradeOrder) {
    if (e.techType != kTechNone || e.upgradeType > kUpgradeNone) {
      if (error != nullptr) *error = "HXP4 research upgrade arm is invalid";
      return false;
    }
  } else if (e.remainingResearchTime != 0 || e.techType != kTechNone ||
             e.upgradeType != kUpgradeNone) {
    if (error != nullptr) *error = "HXP4 non-research unit carries a research union arm";
    return false;
  }
  return true;
}

inline bool ValidateFrame(const Frame &f, std::string *error) {
  if (error != nullptr)
    error->clear();
  if (f.magic != kHxpf && f.magic != kHxp2 && f.magic != kHxp3 && f.magic != kHxp4)
    return Fail(error, "unknown HXP magic");
  if (f.units.size() > kMaxUnits || f.players.size() > kMaxPlayers)
    return Fail(error, "declared unit/player count exceeds protocol bound");
  if (f.mapWidth == 0 || f.mapHeight == 0 || f.mapWidth > kMaxMapSide ||
      f.mapHeight > kMaxMapSide)
    return Fail(error, "invalid map dimensions");
  const std::size_t tileCount = static_cast<std::size_t>(f.mapWidth) * f.mapHeight;
  const std::size_t planeBytes = (tileCount + 7) / 8;
  if (f.magic == kHxpf) {
    if (!f.extras.empty() || !f.playerTech.empty() || !f.tiles.empty() ||
        f.seen.size() != planeBytes || f.explored.size() != planeBytes)
      return Fail(error, "invalid HXPF sections");
  } else {
    if (!f.seen.empty() || !f.explored.empty() || f.tiles.size() != tileCount * 4)
      return Fail(error, "invalid HXP tile sections");
    if (f.magic == kHxp2) {
      if (!f.extras.empty() || !f.playerTech.empty())
        return Fail(error, "HXP2 cannot carry late-state sections");
    } else if (f.magic == kHxp3) {
      if (f.extras.size() != f.units.size() || !f.playerTech.empty())
        return Fail(error, "invalid HXP3 late-state sections");
    } else {
      if (f.extras.size() != f.units.size() || f.playerTech.size() != f.players.size())
        return Fail(error, "HXP4 unit/player late-state counts do not match declarations");
    }
  }
  if (f.magic != kHxp4)
    return true;

  for (std::size_t i = 0; i < f.extras.size(); ++i)
    if (!ValidateHxp4Unit(f.units[i], f.extras[i], error))
      return false;
  return true;
}

inline bool ParseFrameAt(const std::vector<std::uint8_t> &bytes, std::size_t offset,
                         Frame *out, std::size_t *next, std::string *error) {
  if (error != nullptr)
    error->clear();
  if (out == nullptr || next == nullptr)
    return Fail(error, "null parse output");
  if (offset > bytes.size())
    return Fail(error, "frame offset exceeds input");
  Cursor c(bytes, offset, error);
  const std::uint32_t magic = c.U32("frame magic");
  if (magic != kHxpf && magic != kHxp2 && magic != kHxp3 && magic != kHxp4)
    return Fail(error, "unknown HXP magic");
  // Continue through the fixed header selected by the magic, not a data-derived guess.
  Frame f;
  f.magic = magic;
  f.frame = c.U32("frame number");
  const std::uint32_t unitCount = c.U32("unit count");
  f.mapWidth = c.U32("map width");
  f.mapHeight = c.U32("map height");
  const std::uint32_t playerCount = c.U32("player count");
  if (magic == kHxp4) {
    const std::uint32_t extraBytes = c.U32("HXP4 unit-extra size");
    const std::uint32_t techBytes = c.U32("HXP4 player-tech size");
    if (extraBytes != kHxp4ExtraBytes || techBytes != kPlayerTechBytes)
      return Fail(error, "HXP4 explicit section sizes do not match version 4");
  }
  if (!c.ok())
    return false;
  if (unitCount > kMaxUnits || playerCount > kMaxPlayers)
    return Fail(error, "declared unit/player count exceeds protocol bound");
  if (f.mapWidth == 0 || f.mapHeight == 0 || f.mapWidth > kMaxMapSide ||
      f.mapHeight > kMaxMapSide)
    return Fail(error, "invalid map dimensions");

  f.units.reserve(unitCount);
  for (std::uint32_t i = 0; i < unitCount; ++i) {
    Unit u;
    u.id = c.U32("unit id"); u.type = c.U32("unit type"); u.player = c.U32("unit player");
    u.x = c.U32("unit x"); u.y = c.U32("unit y"); u.hp = c.U32("unit hp");
    u.shields = c.U32("unit shields"); u.energy = c.U32("unit energy");
    u.order = c.U32("unit order"); u.visible = c.U8("unit visibility");
    u.completed = c.U8("unit completion"); u.direction = c.U8("unit direction");
    u.generation = c.U8("unit generation");
    f.units.push_back(u);
  }
  if (magic == kHxp3 || magic == kHxp4) {
    f.extras.reserve(unitCount);
    for (std::uint32_t i = 0; i < unitCount; ++i) {
      UnitExtra e;
      e.statusFlags = c.U32("status flags"); e.orderTargetId = c.U32("order target id");
      e.moveTargetId = c.U32("move target id"); e.connectedId = c.U32("connected id");
      e.currentBuildUnitId = c.U32("current build unit id");
      e.speedX = static_cast<std::int32_t>(c.U32("speed x"));
      e.speedY = static_cast<std::int32_t>(c.U32("speed y"));
      e.buildType = c.U32("build type"); e.previousType = c.U32("previous type");
      e.resourceCount = c.U32("resource count"); e.orderUnitType = c.U32("order unit type");
      e.moveTargetX = c.U16("move target x"); e.moveTargetY = c.U16("move target y");
      e.orderTargetX = c.U16("order target x"); e.orderTargetY = c.U16("order target y");
      e.remainingBuildTime = c.U16("remaining build time");
      e.buildQueueSlot = c.U8("build queue slot"); e.orderState = c.U8("order state");
      e.groundCooldown = c.U8("ground cooldown"); e.airCooldown = c.U8("air cooldown");
      e.spellCooldown = c.U8("spell cooldown"); e.movementFlags = c.U8("movement flags");
      e.currentDirection = c.U8("current direction"); e.orderFlags = c.U8("order flags");
      e.detected = c.U8("detected"); e.mainOrderTimer = c.U8("main order timer");
      if (magic == kHxp4) {
        for (std::uint16_t &type : e.buildQueue)
          type = c.U16("full build queue");
        e.remainingResearchTime = c.U16("remaining research time");
        e.techType = c.U8("research tech type");
        e.upgradeType = c.U8("research upgrade type");
      }
      f.extras.push_back(e);
    }
  }
  f.players.reserve(playerCount);
  for (std::uint32_t i = 0; i < playerCount; ++i) {
    Player p;
    p.race = c.U32("player race"); p.minerals = c.U32("player minerals");
    p.gas = c.U32("player gas"); p.supplyTotal = c.U32("player supply total");
    p.supplyUsed = c.U32("player supply used"); p.startX = c.U32("player start x");
    p.startY = c.U32("player start y");
    f.players.push_back(p);
  }
  if (magic == kHxp4) {
    f.playerTech.reserve(playerCount);
    for (std::uint32_t i = 0; i < playerCount; ++i) {
      PlayerTech p;
      const std::uint8_t *raw = c.Take(kPlayerTechBytes, "HXP4 player-tech record");
      if (raw == nullptr)
        return false;
      std::size_t at = 0;
      std::memcpy(p.hasResearched.data(), raw + at, p.hasResearched.size()); at += p.hasResearched.size();
      std::memcpy(p.upgradeLevel.data(), raw + at, p.upgradeLevel.size()); at += p.upgradeLevel.size();
      std::memcpy(p.techInProgress.data(), raw + at, p.techInProgress.size()); at += p.techInProgress.size();
      std::memcpy(p.upgradeInProgress.data(), raw + at, p.upgradeInProgress.size());
      f.playerTech.push_back(p);
    }
  }
  const std::size_t tileCount = static_cast<std::size_t>(f.mapWidth) * f.mapHeight;
  if (magic == kHxpf) {
    const std::size_t planeBytes = (tileCount + 7) / 8;
    const std::uint8_t *seen = c.Take(planeBytes, "HXPF seen plane");
    const std::uint8_t *explored = c.Take(planeBytes, "HXPF explored plane");
    if (seen == nullptr || explored == nullptr)
      return false;
    f.seen.assign(seen, seen + planeBytes);
    f.explored.assign(explored, explored + planeBytes);
  } else {
    const std::uint8_t *tiles = c.Take(tileCount * 4, "HXP tile flags");
    if (tiles == nullptr)
      return false;
    f.tiles.assign(tiles, tiles + tileCount * 4);
  }
  if (!c.ok())
    return false;
  if (!ValidateFrame(f, error))
    return false;
  *out = std::move(f);
  *next = c.at();
  return true;
}

inline bool ParseFrame(const std::vector<std::uint8_t> &bytes, Frame *out, std::string *error) {
  std::size_t next = 0;
  if (!ParseFrameAt(bytes, 0, out, &next, error))
    return false;
  if (next != bytes.size())
    return Fail(error, "trailing bytes after one HXP frame");
  return true;
}

inline bool ParseStream(const std::vector<std::uint8_t> &bytes, std::vector<Frame> *out,
                        std::string *error) {
  if (out == nullptr)
    return Fail(error, "null stream output");
  out->clear();
  std::size_t at = 0;
  while (at < bytes.size()) {
    Frame f;
    std::size_t next = at;
    if (!ParseFrameAt(bytes, at, &f, &next, error))
      return false;
    out->push_back(std::move(f));
    at = next;
  }
  return true;
}

inline void AppendExtra(std::vector<std::uint8_t> *out, const UnitExtra &e, std::uint32_t magic) {
  Append32(out, e.statusFlags); Append32(out, e.orderTargetId); Append32(out, e.moveTargetId);
  Append32(out, e.connectedId); Append32(out, e.currentBuildUnitId);
  Append32(out, static_cast<std::uint32_t>(e.speedX));
  Append32(out, static_cast<std::uint32_t>(e.speedY));
  Append32(out, e.buildType); Append32(out, e.previousType); Append32(out, e.resourceCount);
  Append32(out, e.orderUnitType); Append16(out, e.moveTargetX); Append16(out, e.moveTargetY);
  Append16(out, e.orderTargetX); Append16(out, e.orderTargetY);
  Append16(out, e.remainingBuildTime);
  out->push_back(e.buildQueueSlot); out->push_back(e.orderState);
  out->push_back(e.groundCooldown); out->push_back(e.airCooldown);
  out->push_back(e.spellCooldown); out->push_back(e.movementFlags);
  out->push_back(e.currentDirection); out->push_back(e.orderFlags);
  out->push_back(e.detected); out->push_back(e.mainOrderTimer);
  if (magic == kHxp4) {
    for (std::uint16_t type : e.buildQueue)
      Append16(out, type);
    Append16(out, e.remainingResearchTime);
    out->push_back(e.techType);
    out->push_back(e.upgradeType);
  }
}

inline bool EncodeFrame(const Frame &f, std::vector<std::uint8_t> *out, std::string *error) {
  if (out == nullptr)
    return Fail(error, "null encode output");
  if (!ValidateFrame(f, error))
    return false;
  out->clear();
  Append32(out, f.magic); Append32(out, f.frame);
  Append32(out, static_cast<std::uint32_t>(f.units.size()));
  Append32(out, f.mapWidth); Append32(out, f.mapHeight);
  Append32(out, static_cast<std::uint32_t>(f.players.size()));
  if (f.magic == kHxp4) {
    Append32(out, kHxp4ExtraBytes);
    Append32(out, kPlayerTechBytes);
  }
  for (const Unit &u : f.units) {
    Append32(out, u.id); Append32(out, u.type); Append32(out, u.player);
    Append32(out, u.x); Append32(out, u.y); Append32(out, u.hp);
    Append32(out, u.shields); Append32(out, u.energy); Append32(out, u.order);
    out->push_back(u.visible); out->push_back(u.completed);
    out->push_back(u.direction); out->push_back(u.generation);
  }
  if (f.magic == kHxp3 || f.magic == kHxp4)
    for (const UnitExtra &e : f.extras)
      AppendExtra(out, e, f.magic);
  for (const Player &p : f.players) {
    Append32(out, p.race); Append32(out, p.minerals); Append32(out, p.gas);
    Append32(out, p.supplyTotal); Append32(out, p.supplyUsed);
    Append32(out, p.startX); Append32(out, p.startY);
  }
  if (f.magic == kHxp4)
    for (const PlayerTech &p : f.playerTech) {
      out->insert(out->end(), p.hasResearched.begin(), p.hasResearched.end());
      out->insert(out->end(), p.upgradeLevel.begin(), p.upgradeLevel.end());
      out->insert(out->end(), p.techInProgress.begin(), p.techInProgress.end());
      out->insert(out->end(), p.upgradeInProgress.begin(), p.upgradeInProgress.end());
    }
  if (f.magic == kHxpf) {
    out->insert(out->end(), f.seen.begin(), f.seen.end());
    out->insert(out->end(), f.explored.begin(), f.explored.end());
  } else {
    out->insert(out->end(), f.tiles.begin(), f.tiles.end());
  }
  return true;
}

// Native StarCraft 1.16.1 CUnit, size 0x150. Only fields used by this prototype
// are named; unions remain byte-exact. The reserved PE .game range holds this
// layout at 0x59CCA8 + slot * 0x150 in the production architecture.
#pragma pack(push, 1)
struct ScUnit {
  std::uint32_t prev, next;
  std::int32_t hitPoints;
  std::uint32_t sprite;
  std::uint16_t moveTargetX, moveTargetY;
  std::uint32_t moveTargetUnit;
  std::uint8_t _p18[0x20 - 0x18];
  std::uint8_t movementFlags, currentDirection1, turnRadius, velocityDirection1;
  std::uint8_t _p24[0x28 - 0x24];
  std::uint16_t posX, posY;
  std::uint8_t _p2c[0x40 - 0x2c];
  std::int16_t speedX, speedY;
  std::uint8_t _p44[0x4c - 0x44];
  std::uint8_t playerID, orderID, orderState, orderSignal;
  std::uint16_t orderUnitType;
  std::uint8_t _p52[0x54 - 0x52];
  std::uint8_t mainOrderTimer, groundCooldown, airCooldown, spellCooldown;
  std::uint16_t orderTargetX, orderTargetY;
  std::uint32_t orderTargetUnit, shieldPoints;
  std::uint16_t unitType;
  std::uint8_t _p66[0x68 - 0x66];
  std::uint32_t prevPlayerUnit, nextPlayerUnit;
  std::uint8_t _p70[0x80 - 0x70];
  std::uint32_t connectedUnit;
  std::uint8_t _p84[0x88 - 0x84];
  std::uint16_t previousUnitType;
  std::uint8_t _p8a[0x98 - 0x8a];
  std::uint16_t buildQueue[5];
  std::uint16_t energy;
  std::uint8_t buildQueueSlot, uniquenessIdentifier;
  std::uint8_t _pa6[0xac - 0xa6];
  std::uint16_t remainingBuildTime;
  std::uint8_t _pae[0xc6 - 0xae];
  std::uint16_t upgradeResearchTime;
  std::uint8_t techType, upgradeType;
  std::uint8_t _pca[0xd0 - 0xca];
  std::uint16_t resourceCount;
  std::uint8_t _pd2[0xdc - 0xd2];
  std::uint32_t statusFlags;
  std::uint8_t _pe0[0xe4 - 0xe0];
  std::int32_t visibilityStatus;
  std::uint8_t _pe8[0xec - 0xe8];
  std::uint32_t currentBuildUnit;
  std::uint8_t _pf0[0x150 - 0xf0];
};
#pragma pack(pop)
static_assert(sizeof(ScUnit) == 0x150, "native CUnit size");
static_assert(offsetof(ScUnit, buildQueue) == 0x98, "CUnit buildQueue");
static_assert(offsetof(ScUnit, buildQueueSlot) == 0xa4, "CUnit buildQueueSlot");
static_assert(offsetof(ScUnit, remainingBuildTime) == 0xac, "CUnit remainingBuildTime");
static_assert(offsetof(ScUnit, upgradeResearchTime) == 0xc6, "CUnit upgradeResearchTime");
static_assert(offsetof(ScUnit, techType) == 0xc8, "CUnit techType");
static_assert(offsetof(ScUnit, upgradeType) == 0xc9, "CUnit upgradeType");

constexpr std::uint32_t kScGame = 0x57f0f0u;
constexpr std::uint32_t kScUnits = 0x59cca8u;
constexpr std::uint32_t kScUnitStride = 0x150;
constexpr std::uint32_t kScTechResearchedSC = kScGame + 0xde54;
constexpr std::uint32_t kScCurrentUpgradeSC = kScGame + 0xe1c0;
constexpr std::uint32_t kScTechResearchedBW = kScGame + 0x10050;
constexpr std::uint32_t kScTechProgressBW = kScGame + 0x10140;
constexpr std::uint32_t kScCurrentUpgradeBW = kScGame + 0x1023c;
constexpr std::uint32_t kScUpgradeProgressBW = kScGame + 0x102f0;
static_assert(kScTechResearchedSC == 0x58cf44, "BWGame techResearchedSC address");
static_assert(kScCurrentUpgradeSC == 0x58d2b0, "BWGame currentUpgradeLevelSC address");
static_assert(kScTechResearchedBW == 0x58f140, "BWGame techResearchedBW address");
static_assert(kScTechProgressBW == 0x58f230, "BWGame techResearchInProgressBW address");
static_assert(kScCurrentUpgradeBW == 0x58f32c, "BWGame currentUpgradeLevelBW address");
static_assert(kScUpgradeProgressBW == 0x58f3e0, "BWGame upgradeInProgressBW address");

// A bounds-checked model of the identity address mapping used by the PE .game
// reservation. Production can perform these same writes through ScT<T>(addr).
class ScImage {
 public:
  ScImage() : bytes_(0x400000, 0) {}
  bool Contains(std::uint32_t address, std::size_t size) const {
    return address >= 0x401000u && address <= 0x800000u &&
           size <= 0x800000u - address;
  }
  bool Write8(std::uint32_t address, std::uint8_t value) {
    if (!Contains(address, 1)) return false;
    bytes_[address - 0x400000u] = value;
    return true;
  }
  bool Write16(std::uint32_t address, std::uint16_t value) {
    if (!Contains(address, 2)) return false;
    bytes_[address - 0x400000u] = static_cast<std::uint8_t>(value);
    bytes_[address - 0x400000u + 1] = static_cast<std::uint8_t>(value >> 8);
    return true;
  }
  bool Write(std::uint32_t address, const void *data, std::size_t size) {
    if (!Contains(address, size)) return false;
    if (size != 0)
      std::memcpy(bytes_.data() + address - 0x400000u, data, size);
    return true;
  }
  std::uint8_t Read8(std::uint32_t address) const {
    return Contains(address, 1) ? bytes_[address - 0x400000u] : 0;
  }
  std::uint16_t Read16(std::uint32_t address) const {
    if (!Contains(address, 2)) return 0;
    const std::uint8_t *p = bytes_.data() + address - 0x400000u;
    return pluto_late::Read16(p);
  }

 private:
  std::vector<std::uint8_t> bytes_;
};

// Like the existing bridge, this starts with a cleared native node. Call it at
// the current per-unit reset point, before filling the other CUnit fields; it
// is not an in-place patch over an already populated production node.
inline void ApplyLateUnit(const Frame &f, std::size_t index, ScUnit *out) {
  ScUnit zero{};
  *out = zero;
  for (std::uint16_t &type : out->buildQueue)
    type = kUnitNone;
  if (index >= f.extras.size()) {
    out->buildQueueSlot = 0;  // HXP2/HXPF legacy behavior
    return;
  }
  const UnitExtra &e = f.extras[index];
  if (f.magic == kHxp4) {
    out->buildQueueSlot = e.buildQueueSlot;
    for (std::size_t i = 0; i < 5; ++i)
      out->buildQueue[i] = e.buildQueue[i];
    out->techType = kTechNone;
    out->upgradeType = kUpgradeNone;
    const std::uint32_t order = f.units[index].order;
    if (order == kResearchTechOrder || order == kUpgradeOrder) {
      out->upgradeResearchTime = e.remainingResearchTime;
      out->techType = order == kResearchTechOrder ? e.techType : kTechNone;
      out->upgradeType = order == kUpgradeOrder ? e.upgradeType : kUpgradeNone;
    }
  } else {
    // Preserve HXP3's one-active-entry fallback and zeroed research union.
    out->buildQueueSlot = e.buildQueueSlot < 5 ? e.buildQueueSlot : 0;
    if (e.buildType <= kUnitNone)
      out->buildQueue[out->buildQueueSlot] = static_cast<std::uint16_t>(e.buildType);
  }
}

inline bool ApplyLatePlayers(const Frame &f, ScImage *image) {
  if (image == nullptr)
    return false;
  static const PlayerTech empty{};
  for (std::size_t player = 0; player < kMaxPlayers; ++player) {
    const PlayerTech &p = f.magic == kHxp4 && player < f.playerTech.size()
                              ? f.playerTech[player] : empty;
    for (std::size_t tech = 0; tech < 44; ++tech) {
      if (tech < 24) {
        if (!image->Write8(kScTechResearchedSC + player * 24 + tech, p.hasResearched[tech]))
          return false;
      } else if (!image->Write8(kScTechResearchedBW + player * 20 + tech - 24,
                                p.hasResearched[tech])) {
        return false;
      }
    }
    for (std::size_t upgrade = 0; upgrade < 61; ++upgrade) {
      if (upgrade < 46) {
        if (!image->Write8(kScCurrentUpgradeSC + player * 46 + upgrade,
                           p.upgradeLevel[upgrade]))
          return false;
      } else if (!image->Write8(kScCurrentUpgradeBW + player * 15 + upgrade - 46,
                                p.upgradeLevel[upgrade])) {
        return false;
      }
    }
    if (!image->Write(kScTechProgressBW + player * 6, p.techInProgress.data(), 6) ||
        !image->Write(kScUpgradeProgressBW + player * 8, p.upgradeInProgress.data(), 8))
      return false;
  }
  return true;
}

inline bool Bit(const std::uint8_t *bitset, std::size_t index) {
  return (bitset[index / 8] & (1u << (index % 8))) != 0;
}

struct PlayerProjection {
  std::array<int, 63> upgradeLevel{};       // BWAPI UpgradeTypes::Enum::MAX
  std::array<bool, 47> hasResearched{};     // BWAPI TechTypes::Enum::MAX
  std::array<bool, 47> isResearching{};
  std::array<bool, 63> isUpgrading{};
};

inline PlayerProjection ProjectPlayer(const PlayerTech &p) {
  PlayerProjection out;
  for (std::size_t i = 0; i < 44; ++i) {
    out.hasResearched[i] = p.hasResearched[i] != 0;
    out.isResearching[i] = Bit(p.techInProgress.data(), i);
  }
  for (std::size_t i = 0; i < 61; ++i) {
    out.upgradeLevel[i] = p.upgradeLevel[i];
    out.isUpgrading[i] = Bit(p.upgradeInProgress.data(), i);
  }
  return out;
}

struct ProductionProjection {
  int trainingQueueCount = 0;
  std::array<int, 5> trainingQueue{kUnitNone, kUnitNone, kUnitNone, kUnitNone, kUnitNone};
  int tech = kTechNone;
  int upgrade = kUpgradeNone;
  int remainingResearchTime = 0;
  int remainingUpgradeTime = 0;
};

// HXP4's inside-access production projection, shared by codec tests and the
// bridge. The native queue starts at buildQueueSlot and rotates modulo five.
inline ProductionProjection ProjectHxp4Production(std::uint32_t order, const UnitExtra &extra) {
  ProductionProjection out;
  if (extra.buildQueueSlot < 5 && extra.buildQueue[extra.buildQueueSlot] != kUnitNone) {
    for (std::size_t i = extra.buildQueueSlot % 5;
         extra.buildQueue[i] != kUnitNone && out.trainingQueueCount < 5;
         i = (i + 1) % 5) {
      out.trainingQueue[out.trainingQueueCount++] = extra.buildQueue[i];
    }
  }
  if (order == kResearchTechOrder) {
    out.tech = extra.techType;
    out.remainingResearchTime = extra.remainingResearchTime;
  } else if (order == kUpgradeOrder) {
    out.upgrade = extra.upgradeType;
    out.remainingUpgradeTime = extra.remainingResearchTime;
  }
  return out;
}

// Mirrors BWAPI 4.4.0 UnitUpdate.cpp's inside-access queue rotation and the
// ResearchTech/Upgrade order cases. Enemy production remains hidden.
inline ProductionProjection ProjectProduction(const Frame &f, std::size_t index, bool own) {
  ProductionProjection out;
  if (!own || index >= f.units.size() || index >= f.extras.size())
    return out;
  if (f.magic == kHxp4)
    return ProjectHxp4Production(f.units[index].order, f.extras[index]);
  ScUnit native;
  ApplyLateUnit(f, index, &native);
  if (native.buildQueue[native.buildQueueSlot] != kUnitNone) {
    for (std::size_t i = native.buildQueueSlot % 5;
         native.buildQueue[i] != kUnitNone && out.trainingQueueCount < 5;
         i = (i + 1) % 5) {
      out.trainingQueue[out.trainingQueueCount++] = native.buildQueue[i];
    }
  }
  return out;
}

}  // namespace pluto_late

#endif  // PLUTO_BRIDGE_HXP_LATE_H
