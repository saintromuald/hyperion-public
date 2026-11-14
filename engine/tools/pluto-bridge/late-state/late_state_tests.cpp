#include "hxp_late.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace pluto_late;

namespace {

void Expect(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename T, std::size_t N>
void ExpectArray(const std::array<T, N> &actual, const std::array<T, N> &expected,
                 const std::string &message) {
  Expect(actual == expected, message);
}

Frame MakeFrame(std::uint32_t magic) {
  Frame f;
  f.magic = magic;
  f.frame = 17;
  f.mapWidth = 2;
  f.mapHeight = 1;
  Unit u;
  u.id = 3; u.type = 105; u.player = 0; u.x = 640; u.y = 864;
  u.hp = 1500; u.shields = 0; u.energy = 0; u.order = kResearchTechOrder;
  u.visible = 1; u.completed = 1; u.direction = 64; u.generation = 7;
  f.units.push_back(u);

  UnitExtra e;
  e.statusFlags = 0x12345678; e.orderTargetId = 1; e.moveTargetId = 2;
  e.connectedId = 3; e.currentBuildUnitId = 4; e.speedX = -256; e.speedY = 512;
  e.buildType = 10; e.previousType = 104; e.resourceCount = 1500; e.orderUnitType = 7;
  e.moveTargetX = 1; e.moveTargetY = 2; e.orderTargetX = 3; e.orderTargetY = 4;
  e.remainingBuildTime = 360; e.buildQueueSlot = 2; e.orderState = 5;
  e.groundCooldown = 6; e.airCooldown = 7; e.spellCooldown = 8;
  e.movementFlags = 9; e.currentDirection = 10; e.orderFlags = 11;
  e.detected = 1; e.mainOrderTimer = 12;
  const std::array<std::uint16_t, 5> queue{8, 9, 10, 11, 12};
  NativeUnitProduction production;
  production.buildQueue = queue.data();
  production.buildQueueSlot = 2;
  production.order = u.order;
  production.techType = 5;
  production.upgradeType = kUpgradeNone;
  production.remainingResearchTime = 120;
  std::string productionError;
  Expect(PopulateUnitProduction(production, &e, &productionError),
         "native producer failed: " + productionError);
  if (magic == kHxp3 || magic == kHxp4)
    f.extras.push_back(e);

  f.players.resize(2);
  f.players[0].race = 2; f.players[0].minerals = 500; f.players[0].gas = 100;
  f.players[0].supplyTotal = 40; f.players[0].supplyUsed = 22;
  f.players[0].startX = 608; f.players[0].startY = 928;
  f.players[1].race = 1; f.players[1].startX = 1024; f.players[1].startY = 2048;

  if (magic == kHxp4) {
    std::vector<PlayerTech> nativeTech(12);
    PlayerTech &p = nativeTech[5];
    p.hasResearched[0] = 1; p.hasResearched[24] = 1; p.hasResearched[43] = 1;
    p.upgradeLevel[0] = 2; p.upgradeLevel[45] = 1;
    p.upgradeLevel[46] = 3; p.upgradeLevel[60] = 2;
    p.techInProgress[0] = 0x01; p.techInProgress[3] = 0x01; p.techInProgress[5] = 0x08;
    p.upgradeInProgress[0] = 0x01; p.upgradeInProgress[5] = 0x20;
    p.upgradeInProgress[5] |= 0x40; p.upgradeInProgress[7] = 0x10;
    nativeTech[1].upgradeLevel[0] = 1;
    std::string orderingError;
    Expect(ReorderPlayerTech(nativeTech, {5, 1}, f.players.size(),
                             &f.playerTech, &orderingError),
           "player-tech producer reordering failed: " + orderingError);
  }
  if (magic == kHxpf) {
    f.seen = {0x03}; f.explored = {0x01};
  } else {
    f.tiles = {1, 2, 3, 4, 5, 6, 7, 8};
  }
  return f;
}

std::vector<std::uint8_t> Encode(const Frame &f) {
  std::vector<std::uint8_t> bytes;
  std::string error;
  Expect(EncodeFrame(f, &bytes, &error), "encode failed: " + error);
  return bytes;
}

Frame Parse(const std::vector<std::uint8_t> &bytes) {
  Frame f;
  std::string error;
  Expect(ParseFrame(bytes, &f, &error), "parse failed: " + error);
  return f;
}

void ExpectReject(std::vector<std::uint8_t> bytes, const std::string &name) {
  Frame f;
  std::string error;
  Expect(!ParseFrame(bytes, &f, &error), "malformed input accepted: " + name);
  Expect(!error.empty(), "malformed input had no error: " + name);
}

void TestRoundTrips() {
  const std::uint32_t magics[] = {kHxpf, kHxp2, kHxp3, kHxp4};
  const std::size_t sizes[] = {122, 128, 192, 452};
  for (std::size_t i = 0; i < 4; ++i) {
    const Frame source = MakeFrame(magics[i]);
    const std::vector<std::uint8_t> bytes = Encode(source);
    Expect(bytes.size() == sizes[i], "unexpected encoded frame size");
    const Frame decoded = Parse(bytes);
    Expect(decoded.magic == magics[i], "round-trip magic changed");
    Expect(decoded.frame == 17 && decoded.units.size() == 1 && decoded.players.size() == 2,
           "round-trip header changed");
    Expect(Encode(decoded) == bytes, "round trip was not byte exact");
  }

  const Frame hxp4 = Parse(Encode(MakeFrame(kHxp4)));
  ExpectArray(hxp4.extras[0].buildQueue, std::array<std::uint16_t, 5>{8, 9, 10, 11, 12},
              "HXP4 queue did not round trip");
  Expect(hxp4.extras[0].remainingResearchTime == 120 && hxp4.extras[0].techType == 5 &&
             hxp4.extras[0].upgradeType == kUpgradeNone,
         "HXP4 research state did not round trip");
  Expect(hxp4.playerTech[0].hasResearched[24] == 1 &&
             hxp4.playerTech[0].upgradeLevel[60] == 2 &&
             hxp4.playerTech[0].techInProgress[5] == 0x08 &&
             hxp4.playerTech[0].upgradeInProgress[7] == 0x10,
         "HXP4 player-tech state did not round trip");

  const Frame hxp3 = Parse(Encode(MakeFrame(kHxp3)));
  ExpectArray(hxp3.extras[0].buildQueue,
              std::array<std::uint16_t, 5>{kUnitNone, kUnitNone, kUnitNone, kUnitNone, kUnitNone},
              "HXP3 was silently reinterpreted as HXP4");
  Expect(hxp3.extras[0].remainingResearchTime == 0 &&
             hxp3.extras[0].techType == kTechNone &&
             hxp3.extras[0].upgradeType == kUpgradeNone,
         "HXP3 legacy research defaults changed");
}

void TestNativeApplication() {
  const Frame f = MakeFrame(kHxp4);
  ScUnit native;
  ApplyLateUnit(f, 0, &native);
  Expect(native.buildQueueSlot == 2, "native queue slot incorrect");
  for (std::size_t i = 0; i < 5; ++i)
    Expect(native.buildQueue[i] == f.extras[0].buildQueue[i], "native queue entry incorrect");
  Expect(native.upgradeResearchTime == 120 && native.techType == 5 &&
             native.upgradeType == kUpgradeNone,
         "native research union incorrect");
  const std::uint8_t *raw = reinterpret_cast<const std::uint8_t *>(&native);
  Expect(Read16(raw + 0x98) == 8 && Read16(raw + 0x9a) == 9 && Read16(raw + 0x9c) == 10 &&
             Read16(raw + 0x9e) == 11 && Read16(raw + 0xa0) == 12 &&
             raw[0xa4] == 2 && Read16(raw + 0xc6) == 120 &&
             raw[0xc8] == 5 && raw[0xc9] == kUpgradeNone,
         "native CUnit byte offsets incorrect");

  ScImage image;
  const std::uint32_t unitAddress = kScUnits + 3 * kScUnitStride;
  Expect(image.Write(unitAddress, &native, sizeof native), "failed to write .game CUnit");
  Expect(image.Read16(unitAddress + 0x98) == 8 && image.Read16(unitAddress + 0xa0) == 12 &&
             image.Read16(unitAddress + 0xc6) == 120 &&
             image.Read8(unitAddress + 0xc9) == kUpgradeNone,
         ".game CUnit identity-address mapping incorrect");
  Expect(!image.Contains(0x400000, 1) && image.Contains(0x401000, 1) &&
             image.Contains(0x7fffff, 1) && !image.Contains(0x800000, 1) &&
             !image.Contains(0x7fffff, 2),
         "PE .game address-boundary model incorrect");
  Expect(image.Write8(kScGame + 0xdd34, 1), "failed to seed tech availability");
  Expect(image.Write8(kScGame + 0xdf98, 9), "failed to seed max upgrade");
  Expect(ApplyLatePlayers(f, &image), "native player-tech application failed");
  Expect(image.Read8(kScTechResearchedSC + 0) == 1 &&
             image.Read8(kScTechResearchedBW + 0) == 1 &&
             image.Read8(kScTechResearchedBW + 19) == 1,
         "SC/BW researched-table mapping incorrect");
  Expect(image.Read8(kScCurrentUpgradeSC + 0) == 2 &&
             image.Read8(kScCurrentUpgradeSC + 45) == 1 &&
             image.Read8(kScCurrentUpgradeBW + 0) == 3 &&
             image.Read8(kScCurrentUpgradeBW + 14) == 2,
         "SC/BW upgrade-level mapping incorrect");
  Expect(image.Read8(kScCurrentUpgradeSC + 46 + 0) == 1,
         "second player's upgrade level missing");
  Expect(image.Read8(kScTechProgressBW + 0) == 0x01 &&
             image.Read8(kScTechProgressBW + 3) == 0x01 &&
             image.Read8(kScTechProgressBW + 5) == 0x08 &&
             image.Read8(kScUpgradeProgressBW + 5) == 0x60 &&
             image.Read8(kScUpgradeProgressBW + 7) == 0x10,
         "native progress bitsets incorrect");
  Expect(image.Read8(kScGame + 0xdd34) == 1 && image.Read8(kScGame + 0xdf98) == 9,
         "available/max tables were overwritten");

  const PlayerProjection player = ProjectPlayer(f.playerTech[0]);
  Expect(player.hasResearched[0] && player.hasResearched[24] && player.hasResearched[43] &&
             player.isResearching[0] && player.isResearching[24] && player.isResearching[43],
         "BWAPI player research projection incorrect");
  Expect(player.upgradeLevel[0] == 2 && player.upgradeLevel[46] == 3 &&
             player.upgradeLevel[60] == 2 && player.isUpgrading[0] &&
             player.isUpgrading[45] && player.isUpgrading[46] && player.isUpgrading[60],
         "BWAPI player upgrade projection incorrect");

  const ProductionProjection research = ProjectProduction(f, 0, true);
  Expect(research.trainingQueueCount == 5 &&
             research.trainingQueue[0] == 10 && research.trainingQueue[1] == 11 &&
             research.trainingQueue[2] == 12 && research.trainingQueue[3] == 8 &&
             research.trainingQueue[4] == 9,
         "BWAPI rotated five-entry training queue incorrect");
  Expect(research.tech == 5 && research.remainingResearchTime == 120 &&
             research.upgrade == kUpgradeNone && research.remainingUpgradeTime == 0,
         "BWAPI research projection incorrect");
  const ProductionProjection enemy = ProjectProduction(f, 0, false);
  Expect(enemy.trainingQueueCount == 0 && enemy.tech == kTechNone &&
             enemy.upgrade == kUpgradeNone && enemy.remainingResearchTime == 0,
         "enemy production was exposed");

  Frame upgrading = f;
  upgrading.units[0].order = kUpgradeOrder;
  upgrading.extras[0].techType = kTechNone;
  upgrading.extras[0].upgradeType = 7;
  const ProductionProjection upgrade = ProjectProduction(upgrading, 0, true);
  Expect(upgrade.upgrade == 7 && upgrade.remainingUpgradeTime == 120 &&
             upgrade.tech == kTechNone && upgrade.remainingResearchTime == 0,
         "BWAPI upgrade projection incorrect");

  const int base = 100, factor = 50;
  const int currentLevel = image.Read8(kScCurrentUpgradeSC + 0);
  const int totalTime = base + factor * currentLevel;
  const double progress = 1.0 - static_cast<double>(native.upgradeResearchTime) / totalTime;
  Expect(totalTime == 200 && std::fabs(progress - 0.4) < 1e-12,
         "upgrade progress inputs do not produce base+factor*currentLevel");

  const Frame legacy = MakeFrame(kHxp3);
  ApplyLateUnit(legacy, 0, &native);
  Expect(native.buildQueueSlot == 2 && native.buildQueue[2] == 10 &&
             native.buildQueue[0] == kUnitNone && native.buildQueue[1] == kUnitNone &&
             native.buildQueue[3] == kUnitNone && native.buildQueue[4] == kUnitNone &&
             native.upgradeResearchTime == 0 && native.techType == 0 && native.upgradeType == 0,
         "HXP3 native fallback changed");
  const ProductionProjection legacyProduction = ProjectProduction(legacy, 0, true);
  Expect(legacyProduction.trainingQueueCount == 1 && legacyProduction.trainingQueue[0] == 10 &&
             legacyProduction.trainingQueue[1] == kUnitNone &&
             legacyProduction.tech == kTechNone && legacyProduction.upgrade == kUpgradeNone,
         "HXP3 BWAPI fallback projection changed");
  Expect(ApplyLatePlayers(legacy, &image), "legacy player reset failed");
  Expect(image.Read8(kScCurrentUpgradeSC) == 0 && image.Read8(kScTechResearchedSC) == 0 &&
             image.Read8(kScTechProgressBW) == 0 && image.Read8(kScUpgradeProgressBW) == 0,
         "legacy player tech left stale HXP4 state");

  const Frame hxp2 = MakeFrame(kHxp2);
  ApplyLateUnit(hxp2, 0, &native);
  Expect(native.buildQueueSlot == 0 && native.buildQueue[0] == kUnitNone &&
             native.buildQueue[4] == kUnitNone && native.upgradeResearchTime == 0,
         "HXP2 native fallback changed");

  Frame idle = f;
  idle.units[0].order = 10;
  idle.extras[0].remainingResearchTime = 0;
  idle.extras[0].techType = kTechNone;
  idle.extras[0].upgradeType = kUpgradeNone;
  ApplyLateUnit(idle, 0, &native);
  Expect(native.upgradeResearchTime == 0 && native.techType == kTechNone &&
             native.upgradeType == kUpgradeNone,
         "HXP4 non-research neutral union defaults changed");
}

void TestProducerContract() {
  const std::array<std::uint16_t, 5> queue{8, 9, 10, 11, 12};
  NativeUnitProduction input;
  input.buildQueue = queue.data();
  input.buildQueueSlot = 2;
  input.order = 10;
  input.techType = 0xfe;  // pointer/hangar aliases, not research IDs
  input.upgradeType = 0xff;
  input.remainingResearchTime = 0x1234;
  UnitExtra extra;
  std::string error;
  Expect(PopulateUnitProduction(input, &extra, &error),
         "neutral native producer rejected non-research union aliases");
  Expect(extra.buildQueueSlot == 2 && extra.buildType == 10 &&
             extra.remainingResearchTime == 0 && extra.techType == kTechNone &&
             extra.upgradeType == kUpgradeNone,
         "producer did not preserve the queue slot and neutral research arm");
  Expect(sizeof extra == 78 && offsetof(UnitExtra, buildQueue) == 64 &&
             offsetof(UnitExtra, remainingResearchTime) == 74 &&
             offsetof(UnitExtra, techType) == 76 && offsetof(UnitExtra, upgradeType) == 77,
         "native HXP4 producer layout is not packed 64+14 bytes");

  input.order = kResearchTechOrder;
  input.techType = 45;
  Expect(!PopulateUnitProduction(input, &extra, &error) && !error.empty(),
         "wide/mod research tech id was silently truncated");
  input.techType = kTechNone;
  input.order = kUpgradeOrder;
  input.upgradeType = 62;
  Expect(!PopulateUnitProduction(input, &extra, &error) && !error.empty(),
         "wide/mod research upgrade id was silently truncated");
  input.buildQueueSlot = 5;
  Expect(!PopulateUnitProduction(input, &extra, &error), "producer accepted queue slot 5");
  input.buildQueueSlot = 2;
  const std::array<std::uint16_t, 5> badQueue{8, 9, 10, 11, 229};
  input.buildQueue = badQueue.data();
  Expect(!PopulateUnitProduction(input, &extra, &error) && !error.empty(),
         "producer accepted a queue type outside the classic range");

  std::vector<PlayerTech> nativeTech(12);
  nativeTech[5].upgradeLevel[60] = 3;
  nativeTech[5].hasResearched[43] = 1;
  nativeTech[1].upgradeLevel[0] = 1;
  std::vector<PlayerTech> reordered;
  Expect(ReorderPlayerTech(nativeTech, {5, 1}, 12, &reordered, &error),
         "12-player tech reorder failed");
  Expect(reordered.size() == 12 && reordered[0].upgradeLevel[60] == 3 &&
             reordered[0].hasResearched[43] == 1 && reordered[1].upgradeLevel[0] == 1 &&
             reordered[2].upgradeLevel[60] == 0 && reordered[11].upgradeLevel[60] == 0 &&
             reordered[11].hasResearched[43] == 0,
         "player-tech ordering or neutral zero slots incorrect");
}

void TestMalformedAndStrictFraming() {
  std::string error;
  const std::vector<std::uint8_t> good = Encode(MakeFrame(kHxp4));
  ExpectReject({}, "empty");
  ExpectReject(std::vector<std::uint8_t>(good.begin(), good.begin() + 4), "header only");
  ExpectReject(std::vector<std::uint8_t>(good.begin(), good.end() - 1), "one byte truncated");

  const std::size_t cuts[] = {1, 4, 23, 24, 31, 32, 71, 72, 100, 135,
                              149, 150, 205, 206, 300, 443, 451};
  for (std::size_t cut : cuts)
    ExpectReject(std::vector<std::uint8_t>(good.begin(), good.begin() + cut),
                 "truncated at " + std::to_string(cut));

  std::vector<std::uint8_t> bytes = good;
  bytes.push_back(0);
  ExpectReject(bytes, "trailing byte");

  bytes = good;
  bytes[0] = '5';
  ExpectReject(bytes, "unknown HXP5 magic");

  bytes = good;
  bytes[24] = 64; bytes[25] = bytes[26] = bytes[27] = 0;
  ExpectReject(bytes, "HXP4 with HXP3 unit-extra size");

  bytes = good;
  bytes[28] = bytes[29] = bytes[30] = bytes[31] = 0;
  ExpectReject(bytes, "HXP4 with missing player-tech size");

  bytes = good;
  bytes[8] = 0x10; bytes[9] = 0x27;  // 10000 units
  ExpectReject(bytes, "too many units");

  bytes = good;
  bytes[20] = 13;
  ExpectReject(bytes, "too many players");

  bytes = good;
  bytes[12] = 0;
  ExpectReject(bytes, "zero map width");

  bytes = good;
  bytes[12] = 1; bytes[13] = 1;  // 257
  ExpectReject(bytes, "oversized map width");

  // HXP4 offsets: extra 72, slot +54, active buildType +28,
  // appended queue +64, research time +74, tech +76, upgrade +77.
  bytes = good;
  bytes[72 + 54] = 5;
  ExpectReject(bytes, "queue slot 5");

  bytes = good;
  bytes[72 + 64] = 229; bytes[72 + 65] = 0;
  ExpectReject(bytes, "queue type 229");

  bytes = good;
  bytes[72 + 28] = 11;
  ExpectReject(bytes, "active buildType/queue disagreement");

  bytes = good;
  bytes[72 + 76] = 45;
  ExpectReject(bytes, "tech id 45");

  bytes = good;
  bytes[64] = kUpgradeOrder;  // unit.order at header32 + unit offset 32
  bytes[72 + 76] = kTechNone;
  bytes[72 + 77] = 62;
  ExpectReject(bytes, "upgrade id 62 under Upgrade order");

  // Non-research pointer/hangar union arms are not silently treated as research.
  Frame unionAliased = MakeFrame(kHxp4);
  unionAliased.units[0].order = 10;
  unionAliased.extras[0].techType = 0xfe;
  unionAliased.extras[0].upgradeType = 0xff;
  std::vector<std::uint8_t> unionBytes;
  error.clear();
  Expect(!EncodeFrame(unionAliased, &unionBytes, &error),
         "non-research union pointer bytes were accepted as research");

  bytes = Encode(MakeFrame(kHxp3));
  bytes.insert(bytes.end(), 14, 0xaa);
  ExpectReject(bytes, "HXP3 plus trailing HXP4-looking data");

  bytes = Encode(MakeFrame(kHxp3));
  bytes[3] = '4';
  ExpectReject(bytes, "HXP3 relabeled HXP4 without new header/sections");

  Frame invalid = MakeFrame(kHxp4);
  invalid.extras[0].buildQueueSlot = 5;
  std::vector<std::uint8_t> encoded;
  Expect(!EncodeFrame(invalid, &encoded, &error), "encoder accepted invalid queue slot");
}

void TestConcatenatedStreams() {
  std::vector<std::uint8_t> stream;
  for (std::uint32_t magic : {kHxpf, kHxp2, kHxp3, kHxp4}) {
    const std::vector<std::uint8_t> frame = Encode(MakeFrame(magic));
    stream.insert(stream.end(), frame.begin(), frame.end());
  }
  std::vector<Frame> parsed;
  std::string error;
  Expect(ParseStream(stream, &parsed, &error), "valid concatenated stream rejected: " + error);
  Expect(parsed.size() == 4 && parsed[0].magic == kHxpf && parsed[1].magic == kHxp2 &&
             parsed[2].magic == kHxp3 && parsed[3].magic == kHxp4,
         "concatenated stream versions/order changed");
  stream.push_back(0x42);
  Expect(!ParseStream(stream, &parsed, &error), "stream trailing garbage accepted");

  // Every generation can follow every other without carrying HXP4 state into it.
  stream.clear();
  for (std::uint32_t magic : {kHxp4, kHxp3, kHxp4, kHxp2, kHxpf}) {
    const std::vector<std::uint8_t> frame = Encode(MakeFrame(magic));
    stream.insert(stream.end(), frame.begin(), frame.end());
  }
  Expect(ParseStream(stream, &parsed, &error) && parsed.size() == 5,
         "mixed-generation stream failed");
  Expect(parsed[1].playerTech.empty() && parsed[3].playerTech.empty() &&
             parsed[4].playerTech.empty(),
         "player-tech state leaked across frame generations");
}

void TestDeterministicFuzzSmoke() {
  std::mt19937 rng(0x48585034);
  for (int i = 0; i < 500; ++i) {
    const std::size_t size = rng() % 512;
    std::vector<std::uint8_t> bytes(size);
    for (std::uint8_t &b : bytes)
      b = static_cast<std::uint8_t>(rng());
    Frame f;
    std::size_t next = 0;
    std::string error;
    if (ParseFrameAt(bytes, 0, &f, &next, &error)) {
      Expect(next <= bytes.size(), "parser advanced beyond random input");
      Expect(Encode(f) == std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + next),
             "accepted random frame was not canonical");
    } else {
      Expect(!error.empty(), "random parse failure lacked an error");
    }
  }

  const std::vector<std::uint8_t> good = Encode(MakeFrame(kHxp4));
  for (int i = 0; i < 250; ++i) {
    std::vector<std::uint8_t> bytes = good;
    const std::size_t at = rng() % bytes.size();
    bytes[at] ^= static_cast<std::uint8_t>(1u << (rng() % 8));
    Frame f;
    std::string error;
    if (ParseFrame(bytes, &f, &error))
      Expect(Encode(f) == bytes, "mutated accepted frame was not canonical");
  }
}

}  // namespace

int main() {
  const std::pair<const char *, std::function<void()>> tests[] = {
      {"round trips", TestRoundTrips},
      {"native application", TestNativeApplication},
      {"producer contract", TestProducerContract},
      {"malformed and strict framing", TestMalformedAndStrictFraming},
      {"concatenated streams", TestConcatenatedStreams},
      {"deterministic fuzz smoke", TestDeterministicFuzzSmoke},
  };
  int passed = 0;
  try {
    for (const auto &test : tests) {
      test.second();
      ++passed;
      std::cout << "PASS " << test.first << '\n';
    }
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
  }
  std::cout << passed << " test groups passed\n";
  return 0;
}
