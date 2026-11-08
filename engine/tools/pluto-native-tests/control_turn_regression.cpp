// Pure native regressions for the sandbox turn's validation and active-player
// setup. No engine, map, window, inference process, or natural game is started.
#include "../../src117/port/tools/port_control.cpp"

#include <cstdlib>
#include <iostream>

bool g_hxPlayers = false;
int g_hxSlots = PLAYER_SLOTS_117;
PlayerTable<PlayerData, PlayerTagData> gPlayerData = {};
DWORD g_StormToNationID[8] = {};
DWORD g_StormToHumanID[8] = {};
DWORD g_ActiveNationID = 0;
DWORD g_ActiveHumanID = 0;
DWORD g_ActiveStormID = 0;
DWORD oc_dwReplayFrame = 17;
BYTE gbInPacketProcess = 0;

namespace {
int g_randEnabled = 0;
int g_dispatchCount = 0;
DWORD g_seenNation = 0;
DWORD g_seenHuman = 0;
DWORD g_seenStorm = 0;
std::vector<unsigned char> g_seenBytes;
}

bool PortSnapStatic(void *, size_t) { return true; }

int set_rand_enabled(int enabled) {
  const int previous = g_randEnabled;
  g_randEnabled = enabled;
  return previous;
}

void net_process_game_turn(char *data, unsigned int size, int) {
  ++g_dispatchCount;
  g_seenNation = g_ActiveNationID;
  g_seenHuman = g_ActiveHumanID;
  g_seenStorm = g_ActiveStormID;
  g_seenBytes.assign(data, data + size);
}

int __fastcall GetCommandByteSize(const net_cmd_select_t *pCmd) {
  const unsigned op = pCmd->bOpcode;
  if (op == 0x09 || op == 0x0A || op == 0x0B)
    return 2 + pCmd->bCount * 2;
  switch (op) {
    case 0x05: case 0x08: case 0x10: case 0x11: case 0x18: case 0x19:
    case 0x1B: case 0x1C: case 0x27: case 0x2A: case 0x2E: case 0x31:
    case 0x33: case 0x34: case 0x36: case 0x5A:
      return 1;
    case 0x1A: case 0x1E: case 0x21: case 0x22: case 0x25: case 0x26:
    case 0x28: case 0x2B: case 0x2C: case 0x2D: case 0x30: case 0x32:
    case 0x38: case 0x39: case 0x55: case 0x57:
      return 2;
    case 0x0D: case 0x1F: case 0x20: case 0x23: case 0x29: case 0x35:
      return 3;
    case 0x0E: case 0x12: case 0x2F: case 0x58:
      return 5;
    case 0x06: case 0x07:
      return 33;
    case 0x37:
      return 7;
    case 0x0C:
      return 8;
    case 0x14:
      return 10;
    case 0x15: case 0x56:
      return 11;
    case 0x5C:
      return 82;
    default:
      return 0;
  }
}

const PortJson &PortJson::operator[](const char *name) const {
  static const PortJson empty;
  if (kind == OBJECT)
    for (const PortJsonMember &member : members)
      if (member.first == name)
        return member.second;
  return empty;
}

const PortJson &PortJson::At(size_t index) const {
  static const PortJson empty;
  return kind == ARRAY && index < items.size() ? items[index] : empty;
}

std::string PortJsonQuote(const std::string &text) {
  return "\"" + text + "\"";
}

std::string PortJsonUtf8(const std::string &text) {
  return text;
}

namespace {

bool Check(bool condition, const char *message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

void ResetPlayers(DWORD invalidStorm = 0xFFFFFFFFu) {
  for (int i = 0; i < PLAYER_SLOTS_117; ++i) {
    gPlayerData[i].dwPlayerID = static_cast<DWORD>(i);
    gPlayerData[i].dwStormID = 0xFFFFFFFFu;
    gPlayerData[i].nType = 0;
  }
  for (int i = 0; i < 8; ++i)
    g_StormToNationID[i] = g_StormToHumanID[i] = 0xFFFFFFFFu;
  gPlayerData[0].nType = 1;
  gPlayerData[0].dwStormID = 0;
  gPlayerData[3].nType = 2;
  gPlayerData[3].dwPlayerID = 5;
  gPlayerData[3].dwStormID = invalidStorm;
  g_ActiveNationID = 0;
  g_ActiveHumanID = 0;
  g_ActiveStormID = 0;
  gbInPacketProcess = 0;
  g_dispatchCount = 0;
  g_seenNation = g_seenHuman = g_seenStorm = 0xFFFFFFFFu;
  g_seenBytes.clear();
  g_outbox.clear();
}

std::string Turn(int player, const std::vector<unsigned char> &bytes) {
  PortJson playerJson;
  playerJson.kind = PortJson::NUMBER;
  playerJson.d = player;
  PortJson bytesJson;
  bytesJson.kind = PortJson::ARRAY;
  for (unsigned char byte : bytes) {
    PortJson value;
    value.kind = PortJson::NUMBER;
    value.d = byte;
    bytesJson.items.push_back(value);
  }
  PortJson request;
  request.kind = PortJson::OBJECT;
  request.members.push_back({"player", playerJson});
  request.members.push_back({"bytes", bytesJson});
  g_outbox.clear();
  Call call{1, "1", "turn", request};
  CmdTurn(call);
  return g_outbox.empty() ? std::string() : g_outbox.back().text;
}

bool Ok(const std::string &answer) {
  return answer.find("\"ok\":true") != std::string::npos;
}

bool TestStormOwnerAndSeatMismatch() {
  for (DWORD invalid : {0xFFu, 0xFFFFFFFFu}) {
    ResetPlayers(invalid);
    const std::string answer = Turn(3, {0x1A, 0});
    bool ok = Check(Ok(answer), "a normal order with an invalid computer owner is safely remapped") &&
              Check(g_dispatchCount == 1, "exactly one native dispatch occurs") &&
              Check(g_seenStorm < 8, "g_ActiveStormID is a classic owner, never 0xFF/0xFFFFFFFF") &&
              Check(g_seenNation == 5 && g_seenHuman == 5,
                    "active nation/human use the resolved row's dwPlayerID, not lobby slot 3") &&
              Check(gPlayerData[3].dwStormID == g_seenStorm, "the lobby record owns the resolved Storm ID") &&
              Check(g_StormToNationID[g_seenStorm] == 5 && g_StormToHumanID[g_seenStorm] == 5,
                    "Storm-to-player arrays agree with dispatch");
    for (int i = 0; i < PlayerLobbySlots(); ++i)
      if (i != 3)
        ok = Check(gPlayerData[i].dwStormID != g_seenStorm, "remapped owner is unique") && ok;
    if (!ok)
      return false;
  }
  return true;
}

bool TestRepairClassicStormMappingParticipants() {
  const DWORD oldStorm = 6;
  for (BYTE participantType : {static_cast<BYTE>(1), static_cast<BYTE>(2)}) {
    ResetPlayers();
    gPlayerData[1].nType = participantType;
    gPlayerData[1].dwPlayerID = 4;
    gPlayerData[1].dwStormID = oldStorm;
    g_StormToNationID[oldStorm] = g_StormToHumanID[oldStorm] = 5;
    RepairClassicStormMapping(oldStorm);
    if (!Check(g_StormToNationID[oldStorm] == 4 && g_StormToHumanID[oldStorm] == 4,
               "a unique human/computer remaining row adopts its native dwPlayerID"))
      return false;
  }

  ResetPlayers();
  gPlayerData[1].nType = 0;
  gPlayerData[1].dwPlayerID = 4;
  gPlayerData[1].dwStormID = oldStorm;
  g_StormToNationID[oldStorm] = g_StormToHumanID[oldStorm] = 5;
  RepairClassicStormMapping(oldStorm);
  if (!Check(g_StormToNationID[oldStorm] == 8 && g_StormToHumanID[oldStorm] == 8,
             "a sole closed remaining row is invalidated instead of mapped"))
    return false;

  ResetPlayers();
  g_StormToNationID[oldStorm] = g_StormToHumanID[oldStorm] = 5;
  RepairClassicStormMapping(oldStorm);
  return Check(g_StormToNationID[oldStorm] == 8 && g_StormToHumanID[oldStorm] == 8,
               "no remaining owner invalidates both old Storm entries");
}

bool TestOldStormMappingFollowsRemainingOwner() {
  ResetPlayers();
  gPlayerData[3].dwStormID = 6;
  g_StormToNationID[6] = 5;  // old native mapping initially points at Pluto
  g_StormToHumanID[6] = 5;
  gPlayerData[1].nType = 2;
  gPlayerData[1].dwStormID = 6;  // the other seat subsequently owns old Storm 6

  const std::string answer = Turn(3, {0x1A, 0});
  const DWORD newPlutoStorm = gPlayerData[3].dwStormID;
  bool ok = Check(Ok(answer), "Pluto's aliased old owner forces a remap") &&
            Check(newPlutoStorm < 8 && newPlutoStorm != 6,
                  "Pluto receives a different unique Storm owner") &&
            Check(g_StormToNationID[6] == 1 && g_StormToHumanID[6] == 1,
                  "the old Storm entries immediately follow the unique remaining owner") &&
            Check(g_StormToNationID[newPlutoStorm] == 5 && g_StormToHumanID[newPlutoStorm] == 5,
                  "Pluto's new Storm entries still resolve to Pluto");

  // Native net_mgr_process_turns uses these tables directly for the other
  // seat's later turn. Do not call CmdTurn for seat 1 here: its own preparation
  // must not hide a stale old mapping left by Pluto's remap.
  g_ActiveStormID = 6;
  g_ActiveNationID = g_StormToNationID[6];
  g_ActiveHumanID = g_StormToHumanID[6];
  char bytes[]{0x1A, 0};
  net_process_game_turn(bytes, sizeof bytes, 1);
  ok = Check(g_dispatchCount == 2, "the other seat's later native turn is dispatched") &&
       Check(g_seenStorm == 6, "the later native turn uses the old Storm owner") &&
       Check(g_seenNation == 1 && g_seenHuman == 1,
             "the later native turn resolves to the other player, never Pluto") &&
       Check(g_seenNation != 5 && g_seenHuman != 5,
             "the stale Pluto mapping cannot survive reassignment") && ok;
  return ok;
}

bool TestMalformedAndUnsafeTurns() {
  const std::vector<std::vector<unsigned char>> rejected = {
      {0x09},                         // truncated Select header/body
      {0x14, 1, 2},                  // truncated targeted RightClick
      {0xFF},                         // unknown opcode
      {0x1A, 0, 0x14},               // valid prefix does not make a partial turn acceptable
      {0x37, 0, 0, 0, 0, 0, 0},      // sync report has no real network-turn context
      {0x37, 0, 0, 0, 0, 0},         // malformed sync is rejected before native dispatch
      {0x05},                         // sync counter is not a sandbox gameplay order
      {0x08},                         // restart is session control, not a native test order
      {0x57, 0},                      // quit is session control
  };
  for (const std::vector<unsigned char> &bytes : rejected) {
    ResetPlayers();
    const std::string answer = Turn(3, bytes);
    if (!Check(!Ok(answer), "malformed/unsafe turn returns an error") ||
        !Check(g_dispatchCount == 0, "malformed/unsafe turn never reaches the native dispatcher") ||
        !Check(gPlayerData[3].dwStormID == 0xFFFFFFFFu,
               "rejected bytes do not mutate the Storm-owner record"))
      return false;
  }
  return true;
}

bool TestValidTurnAfterRejections() {
  ResetPlayers();
  Turn(3, {0x37, 0, 0, 0, 0, 0, 0});
  if (!Check(g_dispatchCount == 0, "unsafe sync was rejected"))
    return false;
  const std::string answer = Turn(3, {0x1A, 0});
  return Check(Ok(answer), "a valid turn still runs after a rejected sync") &&
         Check(g_dispatchCount == 1 && g_seenBytes == std::vector<unsigned char>({0x1A, 0}),
               "the valid bytes reach the native dispatcher unchanged") &&
         Check(g_seenStorm < 8 && g_seenNation == 5 && g_seenHuman == 5,
               "the valid turn also uses a safe, replay-style owner mapping");
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  const std::string scenario = argv[1];
  bool ok = false;
  if (scenario == "storm-owner")
    ok = TestStormOwnerAndSeatMismatch();
  else if (scenario == "repair-mapping")
    ok = TestRepairClassicStormMappingParticipants();
  else if (scenario == "old-owner-remap")
    ok = TestOldStormMappingFollowsRemainingOwner();
  else if (scenario == "malformed-turns")
    ok = TestMalformedAndUnsafeTurns();
  else if (scenario == "valid-after-reject")
    ok = TestValidTurnAfterRejections();
  else
    return 2;
  return ok ? 0 : 1;
}
