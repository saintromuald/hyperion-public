// Offline BWAPI 4.4.0 GameData snapshot inspector.
//
// This file intentionally includes the genuine BWAPI headers and accesses only
// typed members.  It never defines a parallel GameData/UnitData layout and never
// decodes a snapshot through hand-written byte offsets.  Build for the same ABI
// that produced genuine-gamedata.bin.  No game or inference code is linked.

#include <BWAPI/Client/GameData.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using BWAPI::BulletData;
using BWAPI::GameData;
using BWAPI::PlayerData;
using BWAPI::UnitData;

constexpr std::size_t kExpectedGameDataSize = 33017048;

// These values are checked against the 32-bit BWAPI 4.4.0 layout used by the
// reference capture.  A compiler with a different early layout fails at build
// time.  The total sizeof is always checked at runtime as compiler ABIs can differ
// only in later/tail layout.  MSVC also gets a compile-time total-size check.
static_assert(sizeof(UnitData) == 336, "BWAPI 4.4.0 UnitData ABI changed");
static_assert(sizeof(PlayerData) == 5788, "BWAPI 4.4.0 PlayerData ABI changed");
static_assert(sizeof(BulletData) == 80, "BWAPI 4.4.0 BulletData ABI changed");
static_assert(offsetof(GameData, units) == 69656, "GameData.units ABI mismatch");
static_assert(offsetof(GameData, hasLatCom) == 3446084, "GameData.hasLatCom ABI mismatch");
static_assert(offsetof(GameData, frameCount) == 3446096, "GameData.frameCount ABI mismatch");
static_assert(offsetof(GameData, mapWidth) == 3446400, "GameData.mapWidth ABI mismatch");
static_assert(offsetof(GameData, commands) == 32242640, "GameData.commands ABI mismatch");
static_assert(offsetof(GameData, unitCommands) == 32482644, "GameData.unitCommands ABI mismatch");
static_assert(offsetof(UnitData, positionX) == 16, "UnitData.positionX ABI mismatch");
static_assert(offsetof(UnitData, hitPoints) == 48, "UnitData.hitPoints ABI mismatch");
#if defined(_MSC_VER)
static_assert(sizeof(GameData) == kExpectedGameDataSize,
              "GameData must be 33017048 bytes for the genuine capture ABI");
#endif

struct OffsetCheck {
  const char* name;
  std::size_t actual;
  std::size_t expected;
};

const std::vector<OffsetCheck>& offsetChecks() {
  static const std::vector<OffsetCheck> checks = {
      {"GameData.units", offsetof(GameData, units), 69656},
      {"GameData.hasLatCom", offsetof(GameData, hasLatCom), 3446084},
      {"GameData.frameCount", offsetof(GameData, frameCount), 3446096},
      {"GameData.mapWidth", offsetof(GameData, mapWidth), 3446400},
      {"GameData.commands", offsetof(GameData, commands), 32242640},
      {"GameData.unitCommands", offsetof(GameData, unitCommands), 32482644},
      {"GameData.unitSearchSize", offsetof(GameData, unitSearchSize), 32962644},
      {"UnitData.positionX", offsetof(UnitData, positionX), 16},
      {"UnitData.hitPoints", offsetof(UnitData, hitPoints), 48},
      {"UnitData.replayID", offsetof(UnitData, replayID), 332},
  };
  return checks;
}

class Json {
 public:
  explicit Json(std::ostream& out) : out_(out) {}

  void beginObject() {
    comma();
    out_ << '{';
    first_.push_back(true);
    key_context_.push_back(false);
  }

  void endObject() {
    out_ << '}';
    first_.pop_back();
    key_context_.pop_back();
    afterValue();
  }

  void beginArray() {
    comma();
    out_ << '[';
    first_.push_back(true);
    key_context_.push_back(false);
  }

  void endArray() {
    out_ << ']';
    first_.pop_back();
    key_context_.pop_back();
    afterValue();
  }

  void key(const char* name) {
    comma();
    out_ << '"' << name << "\":";
    first_.push_back(true);
    key_context_.push_back(true);
  }

  void value(long long value) {
    comma();
    out_ << value;
    afterValue();
  }

  void value(unsigned long long value) {
    comma();
    out_ << value;
    afterValue();
  }

  void value(int value) { this->value(static_cast<long long>(value)); }
  void value(unsigned int value) { this->value(static_cast<unsigned long long>(value)); }
  void value(bool value) { this->value(value ? 1 : 0); }

  void value(double value) {
    if (!std::isfinite(value)) {
      throw std::runtime_error("non-finite double in GameData numeric snapshot");
    }
    comma();
    out_ << std::setprecision(17) << value;
    afterValue();
  }

  void string(const std::string& value) {
    comma();
    out_ << '"';
    for (unsigned char c : value) {
      switch (c) {
        case '"': out_ << "\\\""; break;
        case '\\': out_ << "\\\\"; break;
        case '\n': out_ << "\\n"; break;
        case '\r': out_ << "\\r"; break;
        case '\t': out_ << "\\t"; break;
        default:
          if (c < 0x20) {
            out_ << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                 << static_cast<unsigned>(c) << std::dec;
          } else {
            out_ << static_cast<char>(c);
          }
      }
    }
    out_ << '"';
    afterValue();
  }

 private:
  void comma() {
    if (!first_.empty() && !first_.back()) out_ << ',';
    if (!first_.empty()) first_.back() = false;
  }

  void afterValue() {
    // Primitive values and completed nested containers close a key's one-value
    // context.  Array element contexts remain open until endArray().
    if (!key_context_.empty() && key_context_.back()) {
      key_context_.pop_back();
      first_.pop_back();
    }
  }

  std::ostream& out_;
  std::vector<bool> first_;
  std::vector<bool> key_context_;
};

template <std::size_t N>
void intArray(Json& json, const int (&values)[N]) {
  json.beginArray();
  for (int value : values) json.value(value);
  json.endArray();
}

template <std::size_t N>
void boolArray(Json& json, const bool (&values)[N]) {
  json.beginArray();
  for (bool value : values) json.value(value);
  json.endArray();
}

void member(Json& json, const char* name, int value) {
  json.key(name);
  json.value(value);
}

void member(Json& json, const char* name, unsigned int value) {
  json.key(name);
  json.value(value);
}

void member(Json& json, const char* name, bool value) {
  json.key(name);
  json.value(value);
}

void member(Json& json, const char* name, double value) {
  json.key(name);
  json.value(value);
}

void emitLayout(Json& json) {
  json.key("layout");
  json.beginObject();
  member(json, "expected_sizeof_GameData", static_cast<int>(kExpectedGameDataSize));
  member(json, "sizeof_GameData", static_cast<int>(sizeof(GameData)));
  member(json, "sizeof_UnitData", static_cast<int>(sizeof(UnitData)));
  member(json, "sizeof_PlayerData", static_cast<int>(sizeof(PlayerData)));
  bool valid = sizeof(GameData) == kExpectedGameDataSize;
  for (const auto& check : offsetChecks()) valid = valid && check.actual == check.expected;
  member(json, "verified", valid);
  json.key("offsets");
  json.beginObject();
  for (const auto& check : offsetChecks()) {
    json.key(check.name);
    json.beginObject();
    member(json, "actual", static_cast<int>(check.actual));
    member(json, "expected", static_cast<int>(check.expected));
    json.endObject();
  }
  json.endObject();
  json.endObject();
}

void emitPlayer(Json& json, const PlayerData& player) {
  json.beginObject();
  member(json, "race", player.race);
  member(json, "type", player.type);
  member(json, "force", player.force);
  json.key("is_ally"); boolArray(json, player.isAlly);
  json.key("is_enemy"); boolArray(json, player.isEnemy);
  member(json, "is_neutral", player.isNeutral);
  member(json, "start_location_x", player.startLocationX);
  member(json, "start_location_y", player.startLocationY);
  member(json, "is_victorious", player.isVictorious);
  member(json, "is_defeated", player.isDefeated);
  member(json, "left_game", player.leftGame);
  member(json, "is_participating", player.isParticipating);
  member(json, "minerals", player.minerals);
  member(json, "gas", player.gas);
  member(json, "gathered_minerals", player.gatheredMinerals);
  member(json, "gathered_gas", player.gatheredGas);
  member(json, "repaired_minerals", player.repairedMinerals);
  member(json, "repaired_gas", player.repairedGas);
  member(json, "refunded_minerals", player.refundedMinerals);
  member(json, "refunded_gas", player.refundedGas);
  json.key("supply_total"); intArray(json, player.supplyTotal);
  json.key("supply_used"); intArray(json, player.supplyUsed);
  member(json, "color", player.color);
  member(json, "total_unit_score", player.totalUnitScore);
  member(json, "total_kill_score", player.totalKillScore);
  member(json, "total_building_score", player.totalBuildingScore);
  member(json, "total_razing_score", player.totalRazingScore);
  member(json, "custom_score", player.customScore);
  json.endObject();
}

void emitUnit(Json& json, const UnitData& unit) {
  json.beginObject();
  member(json, "clearance_level", unit.clearanceLevel);
  member(json, "id", unit.id);
  member(json, "player", unit.player);
  member(json, "type", unit.type);
  member(json, "position_x", unit.positionX);
  member(json, "position_y", unit.positionY);
  member(json, "angle", unit.angle);
  member(json, "velocity_x", unit.velocityX);
  member(json, "velocity_y", unit.velocityY);
  member(json, "hit_points", unit.hitPoints);
  member(json, "last_hit_points", unit.lastHitPoints);
  member(json, "shields", unit.shields);
  member(json, "energy", unit.energy);
  member(json, "resources", unit.resources);
  member(json, "resource_group", unit.resourceGroup);
  member(json, "kill_count", unit.killCount);
  member(json, "acid_spore_count", unit.acidSporeCount);
  member(json, "scarab_count", unit.scarabCount);
  member(json, "interceptor_count", unit.interceptorCount);
  member(json, "spider_mine_count", unit.spiderMineCount);
  member(json, "ground_weapon_cooldown", unit.groundWeaponCooldown);
  member(json, "air_weapon_cooldown", unit.airWeaponCooldown);
  member(json, "spell_cooldown", unit.spellCooldown);
  member(json, "defense_matrix_points", unit.defenseMatrixPoints);
  member(json, "defense_matrix_timer", unit.defenseMatrixTimer);
  member(json, "ensnare_timer", unit.ensnareTimer);
  member(json, "irradiate_timer", unit.irradiateTimer);
  member(json, "lockdown_timer", unit.lockdownTimer);
  member(json, "maelstrom_timer", unit.maelstromTimer);
  member(json, "order_timer", unit.orderTimer);
  member(json, "plague_timer", unit.plagueTimer);
  member(json, "remove_timer", unit.removeTimer);
  member(json, "stasis_timer", unit.stasisTimer);
  member(json, "stim_timer", unit.stimTimer);
  member(json, "build_type", unit.buildType);
  member(json, "training_queue_count", unit.trainingQueueCount);
  json.key("training_queue"); intArray(json, unit.trainingQueue);
  member(json, "tech", unit.tech);
  member(json, "upgrade", unit.upgrade);
  member(json, "remaining_build_time", unit.remainingBuildTime);
  member(json, "remaining_train_time", unit.remainingTrainTime);
  member(json, "remaining_research_time", unit.remainingResearchTime);
  member(json, "remaining_upgrade_time", unit.remainingUpgradeTime);
  member(json, "build_unit", unit.buildUnit);
  member(json, "target", unit.target);
  member(json, "target_position_x", unit.targetPositionX);
  member(json, "target_position_y", unit.targetPositionY);
  member(json, "order", unit.order);
  member(json, "order_target", unit.orderTarget);
  member(json, "order_target_position_x", unit.orderTargetPositionX);
  member(json, "order_target_position_y", unit.orderTargetPositionY);
  member(json, "secondary_order", unit.secondaryOrder);
  member(json, "rally_position_x", unit.rallyPositionX);
  member(json, "rally_position_y", unit.rallyPositionY);
  member(json, "rally_unit", unit.rallyUnit);
  member(json, "addon", unit.addon);
  member(json, "nydus_exit", unit.nydusExit);
  member(json, "power_up", unit.powerUp);
  member(json, "transport", unit.transport);
  member(json, "carrier", unit.carrier);
  member(json, "hatchery", unit.hatchery);
  member(json, "exists", unit.exists);
  member(json, "has_nuke", unit.hasNuke);
  member(json, "is_accelerating", unit.isAccelerating);
  member(json, "is_attacking", unit.isAttacking);
  member(json, "is_attack_frame", unit.isAttackFrame);
  member(json, "is_being_gathered", unit.isBeingGathered);
  member(json, "is_blind", unit.isBlind);
  member(json, "is_braking", unit.isBraking);
  member(json, "is_burrowed", unit.isBurrowed);
  member(json, "carry_resource_type", unit.carryResourceType);
  member(json, "is_cloaked", unit.isCloaked);
  member(json, "is_completed", unit.isCompleted);
  member(json, "is_constructing", unit.isConstructing);
  member(json, "is_detected", unit.isDetected);
  member(json, "is_gathering", unit.isGathering);
  member(json, "is_hallucination", unit.isHallucination);
  member(json, "is_idle", unit.isIdle);
  member(json, "is_interruptible", unit.isInterruptible);
  member(json, "is_invincible", unit.isInvincible);
  member(json, "is_lifted", unit.isLifted);
  member(json, "is_morphing", unit.isMorphing);
  member(json, "is_moving", unit.isMoving);
  member(json, "is_parasited", unit.isParasited);
  member(json, "is_selected", unit.isSelected);
  member(json, "is_starting_attack", unit.isStartingAttack);
  member(json, "is_stuck", unit.isStuck);
  member(json, "is_training", unit.isTraining);
  member(json, "is_under_storm", unit.isUnderStorm);
  member(json, "is_under_dark_swarm", unit.isUnderDarkSwarm);
  member(json, "is_under_dweb", unit.isUnderDWeb);
  member(json, "is_powered", unit.isPowered);
  json.key("is_visible"); boolArray(json, unit.isVisible);
  member(json, "buttonset", unit.buttonset);
  member(json, "last_attacker_player", unit.lastAttackerPlayer);
  member(json, "recently_attacked", unit.recentlyAttacked);
  member(json, "replay_id", unit.replayID);
  json.endObject();
}

bool runtimeLayoutValid() {
  if (sizeof(GameData) != kExpectedGameDataSize) return false;
  for (const auto& check : offsetChecks()) {
    if (check.actual != check.expected) return false;
  }
  return true;
}

void emitSnapshot(const GameData& game, std::ostream& out) {
  Json json(out);
  json.beginObject();
  emitLayout(json);
  json.key("game_data");
  json.beginObject();
  member(json, "client_version", game.client_version);
  member(json, "revision", game.revision);
  member(json, "is_debug", game.isDebug);
  member(json, "instance_id", game.instanceID);
  member(json, "bot_apm_noselects", game.botAPM_noselects);
  member(json, "bot_apm_selects", game.botAPM_selects);
  member(json, "force_count", game.forceCount);
  member(json, "player_count", game.playerCount);
  member(json, "initial_unit_count", game.initialUnitCount);
  member(json, "game_type", game.gameType);
  member(json, "latency", game.latency);
  member(json, "latency_frames", game.latencyFrames);
  member(json, "latency_time", game.latencyTime);
  member(json, "remaining_latency_frames", game.remainingLatencyFrames);
  member(json, "remaining_latency_time", game.remainingLatencyTime);
  member(json, "has_lat_com", game.hasLatCom);
  member(json, "has_gui", game.hasGUI);
  member(json, "replay_frame_count", game.replayFrameCount);
  member(json, "random_seed", game.randomSeed);
  member(json, "frame_count", game.frameCount);
  member(json, "elapsed_time", game.elapsedTime);
  member(json, "countdown_timer", game.countdownTimer);
  member(json, "fps", game.fps);
  member(json, "average_fps", game.averageFPS);
  member(json, "mouse_x", game.mouseX);
  member(json, "mouse_y", game.mouseY);
  member(json, "screen_x", game.screenX);
  member(json, "screen_y", game.screenY);
  member(json, "map_width", game.mapWidth);
  member(json, "map_height", game.mapHeight);
  member(json, "region_count", game.regionCount);
  member(json, "start_location_count", game.startLocationCount);
  json.key("start_locations");
  json.beginArray();
  for (const auto& position : game.startLocations) {
    json.beginObject();
    member(json, "x", position.x);
    member(json, "y", position.y);
    json.endObject();
  }
  json.endArray();
  member(json, "is_in_game", game.isInGame);
  member(json, "is_multiplayer", game.isMultiplayer);
  member(json, "is_battle_net", game.isBattleNet);
  member(json, "is_paused", game.isPaused);
  member(json, "is_replay", game.isReplay);
  member(json, "selected_unit_count", game.selectedUnitCount);
  json.key("selected_units"); intArray(json, game.selectedUnits);
  member(json, "self", game.self);
  member(json, "enemy", game.enemy);
  member(json, "neutral", game.neutral);
  member(json, "event_count", game.eventCount);
  member(json, "command_count", game.commandCount);
  member(json, "unit_command_count", game.unitCommandCount);
  json.key("players");
  json.beginArray();
  for (const auto& player : game.players) emitPlayer(json, player);
  json.endArray();
  json.endObject();

  json.key("unit_state");
  json.beginObject();
  // Array index is the stable capture key; UnitData.id remains a numeric field.
  // Emit all 10000 slots on every snapshot, including exists=0, so absence never
  // gets mistaken for equality or death by the differential comparator.
  for (std::size_t index = 0; index < 10000; ++index) {
    json.key(std::to_string(index).c_str());
    emitUnit(json, game.units[index]);
  }
  json.endObject();
  json.endObject();
  out << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--layout-only") {
      Json json(std::cout);
      json.beginObject();
      emitLayout(json);
      json.endObject();
      std::cout << '\n';
      return runtimeLayoutValid() ? 0 : 2;
    }
    if (argc != 2) {
      std::cerr << "usage: gamedata_inspector SNAPSHOT.bin | --layout-only\n";
      return 64;
    }
    if (!runtimeLayoutValid()) {
      std::cerr << "GameData ABI mismatch: sizeof(GameData)=" << sizeof(GameData)
                << ", required " << kExpectedGameDataSize
                << "; run --layout-only for header-derived offsets\n";
      return 2;
    }
    std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
    if (!input) {
      std::cerr << "cannot open snapshot: " << argv[1] << '\n';
      return 1;
    }
    const std::streamoff size = input.tellg();
    const std::streamoff mappedSize =
        (static_cast<std::streamoff>(sizeof(GameData)) + 4095) & ~static_cast<std::streamoff>(4095);
    if (size != static_cast<std::streamoff>(sizeof(GameData)) && size != mappedSize) {
      std::cerr << "snapshot size " << size << " is neither sizeof(GameData) " << sizeof(GameData)
                << " nor its page-rounded mapping size " << mappedSize << '\n';
      return 2;
    }
    input.seekg(0);
    // The object is roughly 33 MB and must not overflow a small process stack.
    auto game = std::make_unique<GameData>();
    input.read(reinterpret_cast<char*>(game.get()), sizeof(*game));
    if (!input || input.gcount() != static_cast<std::streamsize>(sizeof(*game))) {
      std::cerr << "short GameData snapshot read\n";
      return 2;
    }
    emitSnapshot(*game, std::cout);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "gamedata_inspector: " << error.what() << '\n';
    return 1;
  }
}
