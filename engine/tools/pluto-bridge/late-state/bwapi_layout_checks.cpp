#include <BW/CUnit.h>
#include <BW/BWGame.h>
#include <BW/Structures.h>
#include <BWAPI/Client/UnitData.h>
#include <BWAPI/Client/PlayerData.h>

#include <cstddef>

static_assert(sizeof(BW::CUnit) == 0x150, "BW::CUnit");
static_assert(offsetof(BW::CUnit, buildQueue) == 0x98, "CUnit.buildQueue");
static_assert(sizeof(((BW::CUnit *)0)->buildQueue) == 10, "five u16 queue entries");
static_assert(offsetof(BW::CUnit, energy) == 0xa2, "CUnit.energy");
static_assert(offsetof(BW::CUnit, buildQueueSlot) == 0xa4, "CUnit.buildQueueSlot");
static_assert(offsetof(BW::CUnit, remainingBuildTime) == 0xac, "CUnit.remainingBuildTime");
static_assert(offsetof(BW::CUnit, building.upgradeResearchTime) == 0xc6,
              "CUnit.building.upgradeResearchTime");
static_assert(offsetof(BW::CUnit, building.techType) == 0xc8, "CUnit.building.techType");
static_assert(offsetof(BW::CUnit, building.upgradeType) == 0xc9,
              "CUnit.building.upgradeType");
static_assert(offsetof(BW::CUnit, currentBuildUnit) == 0xec, "CUnit.currentBuildUnit");

static_assert(sizeof(BW::BWGame) == 96000, "BWGame size");
static_assert(offsetof(BW::BWGame, techAvailableSC) == 0xdd34, "BWGame.techAvailableSC");
static_assert(offsetof(BW::BWGame, techResearchedSC) == 0xde54, "BWGame.techResearchedSC");
static_assert(offsetof(BW::BWGame, techResearchInProgressSC) == 0xdf74,
              "BWGame.techResearchInProgressSC");
static_assert(offsetof(BW::BWGame, maxUpgradeLevelSC) == 0xdf98, "BWGame.maxUpgradeLevelSC");
static_assert(offsetof(BW::BWGame, currentUpgradeLevelSC) == 0xe1c0,
              "BWGame.currentUpgradeLevelSC");
static_assert(offsetof(BW::BWGame, techAvailableBW) == 0xff60, "BWGame.techAvailableBW");
static_assert(offsetof(BW::BWGame, techResearchedBW) == 0x10050, "BWGame.techResearchedBW");
static_assert(offsetof(BW::BWGame, techResearchInProgressBW) == 0x10140,
              "BWGame.techResearchInProgressBW");
static_assert(offsetof(BW::BWGame, maxUpgradeLevelBW) == 0x10188, "BWGame.maxUpgradeLevelBW");
static_assert(offsetof(BW::BWGame, currentUpgradeLevelBW) == 0x1023c,
              "BWGame.currentUpgradeLevelBW");
static_assert(offsetof(BW::BWGame, upgradeInProgressBW) == 0x102f0,
              "BWGame.upgradeInProgressBW");

static_assert(sizeof(BW::unitFinder) == 8, "native unitFinder is two dwords");
static_assert(offsetof(BW::unitFinder, unitIndex) == 0, "native unitFinder.unitIndex");
static_assert(offsetof(BW::unitFinder, searchValue) == 4, "native unitFinder.searchValue");

static_assert(offsetof(BWAPI::UnitData, buildType) == 148, "UnitData.buildType");
static_assert(offsetof(BWAPI::UnitData, trainingQueueCount) == 152,
              "UnitData.trainingQueueCount");
static_assert(offsetof(BWAPI::UnitData, trainingQueue) == 156, "UnitData.trainingQueue");
static_assert(sizeof(((BWAPI::UnitData *)0)->trainingQueue) == 20,
              "UnitData has five int queue entries");
static_assert(offsetof(BWAPI::UnitData, tech) == 176, "UnitData.tech");
static_assert(offsetof(BWAPI::UnitData, upgrade) == 180, "UnitData.upgrade");
static_assert(offsetof(BWAPI::UnitData, remainingBuildTime) == 184,
              "UnitData.remainingBuildTime");
static_assert(offsetof(BWAPI::UnitData, remainingTrainTime) == 188,
              "UnitData.remainingTrainTime");
static_assert(offsetof(BWAPI::UnitData, remainingResearchTime) == 192,
              "UnitData.remainingResearchTime");
static_assert(offsetof(BWAPI::UnitData, remainingUpgradeTime) == 196,
              "UnitData.remainingUpgradeTime");
static_assert(offsetof(BWAPI::UnitData, buildUnit) == 200, "UnitData.buildUnit");

static_assert(BWAPI::UpgradeTypes::Enum::MAX == 63, "PlayerData upgrade array bound");
static_assert(BWAPI::TechTypes::Enum::MAX == 47, "PlayerData tech array bound");
static_assert(sizeof(((BWAPI::PlayerData *)0)->upgradeLevel) == 63 * 4,
              "PlayerData.upgradeLevel");
static_assert(sizeof(((BWAPI::PlayerData *)0)->hasResearched) == 47,
              "PlayerData.hasResearched");
static_assert(sizeof(((BWAPI::PlayerData *)0)->isResearching) == 47,
              "PlayerData.isResearching");
static_assert(sizeof(((BWAPI::PlayerData *)0)->isUpgrading) == 63,
              "PlayerData.isUpgrading");

int main() { return 0; }
