// port_sbmanifest.h -- the server-owned launch record for a ShieldBattery 1v1.
#ifndef PORT_SB_MANIFEST_H
#define PORT_SB_MANIFEST_H

#include <stdint.h>
#include <string>
#include <vector>

struct SbManifestSlot {
  uint32_t rpSlot = 0;
  uint32_t stormId = 0;
  uint32_t bwPlayerId = 0;
  uint32_t userId = 0;
  uint32_t teamId = 0;
  std::string name;
  std::string race;
  bool host = false;
};

struct SbManifest {
  int protocol = 0;
  std::string gameId;
  std::string name;
  std::string gameType;
  int gameSubType = 0;
  uint32_t seed = 0;
  int speed = 6;
  bool disableAllianceChanges = false;
  uint32_t localUserId = 0;
  std::vector<SbManifestSlot> slots;
  std::string mapFormat;
  std::string mapHash;
  std::string mapPath;
  int adapterProtocol = 0;
  std::string engineBuild;
  int simRevision = 0;
  std::string rules;
  int antiMeta = 0;
  std::string modId;
  std::string modFingerprint;
  std::string dataFingerprint;
  bool rollback = false;
};

// Loads and validates the optional `hyperion` object in a launch JSON. False
// means either a legacy launch or an invalid one; SbManifestError distinguishes
// them. A valid manifest is active only after every check passes, including the
// local map's ShieldBattery hash.
bool SbManifestLoad(const std::string &json, const std::string &mapPath, std::string *why = nullptr);
void SbManifestClear();
bool SbManifestActive();
const std::string &SbManifestError();
const SbManifest &SbManifestGet();
const SbManifestSlot *SbManifestLocalSlot();
uint32_t SbManifestSeed();
// The active installation's compatibility record as JSON, after the game's
// data and active mod have loaded. Used by local install/acceptance tooling.
std::string SbManifestCompatibilityJson();
// stdout form for main.cpp, whose legacy CRT declarations cannot include this
// header's C++ standard-library types under MSVC.
void SbManifestCompatibilityPrint();
// Fills the native lobby tables from the validated roster after their normal
// initialization. Every seat runs this locally; no creator setup blob is read.
bool SbManifestApplyLobby();

#endif
