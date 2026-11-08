// Pure native regressions for Pluto's reply translation and classic owner mapping.
// The production TU is included so these tests call its real anonymous-namespace
// helpers; --gc-sections removes the bridge/process code and all its dependencies.
#include "../../src117/port/gameplay/port_pluto.cpp"

#include <cstdarg>
#include <cstdlib>
#include <iostream>

bool g_hxPlayers = false;
int g_hxSlots = PLAYER_SLOTS_117;
PlayerTable<PlayerData, PlayerTagData> gPlayerData = {};
DWORD g_StormToNationID[8] = {};
DWORD g_StormToHumanID[8] = {};

std::string g_lastPortLog;
void PortLogger::Line(const char *) const {}
void PortLogger::Write(const char *format, ...) const {
  char text[512];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  g_lastPortLog = text;
}
bool PortSnapStatic(void *, size_t) { return true; }

namespace {
CUnit g_applyLiveUnit;
DWORD g_applyLiveId = 0;
DWORD g_applyLastLookup = 0;
unsigned g_applyLookupCount = 0;

void ResetApplyLookup(DWORD liveId) {
  g_applyLiveId = liveId;
  g_applyLastLookup = 0;
  g_applyLookupCount = 0;
}
}

// Model the native packed-id contract at apply time: a dead id or an older
// generation of a reused slot resolves null; only the current live id resolves.
CUnit *__fastcall CUnit::FromIdAlive(DWORD dwUnitId) {
  ++g_applyLookupCount;
  g_applyLastLookup = dwUnitId;
  return dwUnitId != 0 && dwUnitId == g_applyLiveId ? &g_applyLiveUnit : NULL;
}

int __fastcall GetCommandByteSize(const net_cmd_select_t *pCmd) {
  const unsigned op = pCmd->bOpcode;
  if (op == 0x09 || op == 0x0A || op == 0x0B)
    return 2 + pCmd->bCount * 2;
  switch (op) {
    case 0x14: return 10;
    case 0x15: return 11;
    case 0x1A: return 2;
    case 0x29: return 3;
    default: return 0;
  }
}

namespace {

bool Check(bool condition, const char *message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

void Put16(std::vector<unsigned char> *bytes, unsigned value) {
  bytes->push_back(static_cast<unsigned char>(value));
  bytes->push_back(static_cast<unsigned char>(value >> 8));
}

std::vector<unsigned char> Select(unsigned observationId) {
  std::vector<unsigned char> bytes{0x09, 0x01};
  Put16(&bytes, observationId);
  return bytes;
}

std::vector<unsigned char> RightClick(unsigned target) {
  std::vector<unsigned char> bytes{0x14};
  Put16(&bytes, 320);
  Put16(&bytes, 480);
  Put16(&bytes, target);
  Put16(&bytes, 228);
  bytes.push_back(0);
  return bytes;
}

std::vector<unsigned char> Action(unsigned target) {
  std::vector<unsigned char> bytes{0x15};
  Put16(&bytes, 640);
  Put16(&bytes, 704);
  Put16(&bytes, target);
  Put16(&bytes, 228);
  bytes.push_back(14);  // attack-move when there is no target
  bytes.push_back(0);
  return bytes;
}

std::vector<unsigned char> Unload(unsigned target) {
  std::vector<unsigned char> bytes{0x29};
  Put16(&bytes, target);
  return bytes;
}

WORD Packed(unsigned generation, unsigned slot) {
  return static_cast<WORD>((generation << 11) | (slot + 1));
}

void ResetPlayers() {
  for (int i = 0; i < PLAYER_SLOTS_117; ++i) {
    gPlayerData[i].dwPlayerID = static_cast<DWORD>(i);
    gPlayerData[i].dwStormID = 0xFFFFFFFFu;
    gPlayerData[i].nType = 0;
  }
  for (int i = 0; i < 8; ++i)
    g_StormToNationID[i] = g_StormToHumanID[i] = 0xFFFFFFFFu;
  gPlayerData[0].nType = 1;
  gPlayerData[0].dwStormID = 0;
}

bool TestNoTargetSentinel() {
  memset(s_aFrameIds, 0, sizeof s_aFrameIds);
  ResetApplyLookup(0);
  std::vector<unsigned char> right = RightClick(0);
  const std::vector<unsigned char> rightBefore = right;
  bool ok = Check(TranslatePlutoCommands(4, &right), "no-target RightClick translates") &&
            Check(right == rightBefore, "target ID zero remains a genuine RightClick position order");
  std::vector<unsigned char> action = Action(0);
  const std::vector<unsigned char> actionBefore = action;
  return Check(TranslatePlutoCommands(4, &action), "no-target Action translates") &&
         Check(action == actionBefore, "target ID zero remains a genuine Action position order") &&
         Check(g_applyLookupCount == 0, "genuine no-target commands never require a unit liveness lookup") && ok;
}

bool TestMissingTargetDropsWholeCommand() {
  memset(s_aFrameIds, 0, sizeof s_aFrameIds);
  const DWORD emitted = 10;
  const unsigned targetSlot = 0;
  const unsigned selectedSlot = 1;
  // The selected unit existed in the observation. The target did not. A new
  // generation occupies the target's slot by apply time, but must never be used.
  s_aFrameIds[emitted % 8][selectedSlot] = Packed(2, selectedSlot);
  s_aFrameIds[(emitted + 3) % 8][targetSlot] = Packed(9, targetSlot);
  ResetApplyLookup(Packed(9, targetSlot));
  PendingCommand logTiming{emitted + 3, emitted, emitted + 1, {}};

  std::vector<unsigned char> mixed = Select(kPlutoIdBase + selectedSlot);
  const std::vector<unsigned char> right = RightClick(kPlutoIdBase + targetSlot);
  mixed.insert(mixed.end(), right.begin(), right.end());
  mixed.push_back(0x1A);
  mixed.push_back(0);
  std::vector<unsigned char> expected = Select(kPlutoIdBase + selectedSlot);
  expected[2] = static_cast<unsigned char>(Packed(2, selectedSlot));
  expected[3] = static_cast<unsigned char>(Packed(2, selectedSlot) >> 8);
  expected.push_back(0x1A);
  expected.push_back(0);
  const unsigned mixedReceivedBytes = static_cast<unsigned>(mixed.size());
  bool ok = Check(TranslatePlutoCommands(emitted, &mixed), "mixed reply remains valid") &&
            Check(mixed == expected, "stale RightClick is removed whole; selection and Stop survive");
  LogPlutoDispatch(logTiming, mixedReceivedBytes, static_cast<unsigned>(mixed.size()));
  ok = Check(g_lastPortLog ==
                 "Pluto: emitted 10 received 11 dispatched 13 received_bytes 16 accepted_bytes 6",
             "partial filtering logs raw-received and post-translation accepted byte counts") && ok;

  std::vector<unsigned char> action = Action(kPlutoIdBase + targetSlot);
  const unsigned actionReceivedBytes = static_cast<unsigned>(action.size());
  ok = Check(TranslatePlutoCommands(emitted, &action), "stale Action parses") &&
       Check(action.empty(), "stale Action cannot become an attack-move/location order") && ok;
  LogPlutoDispatch(logTiming, actionReceivedBytes, static_cast<unsigned>(action.size()));
  ok = Check(g_lastPortLog ==
                 "Pluto: emitted 10 received 11 dispatched 13 received_bytes 11 accepted_bytes 0",
             "whole filtering logs the raw byte count and an explicit zero accepted count") && ok;
  std::vector<unsigned char> unload = Unload(kPlutoIdBase + targetSlot);
  ok = Check(TranslatePlutoCommands(emitted, &unload), "stale targeted unload parses") &&
       Check(unload.empty(), "stale targeted unload is removed whole") &&
       Check(g_applyLookupCount == 0, "an emission-frame zero target is dropped without a native lookup") && ok;
  return ok;
}

bool TestApplyTimeDeathAndSlotReuse() {
  memset(s_aFrameIds, 0, sizeof s_aFrameIds);
  const DWORD emitted = 20;
  const WORD oldId = Packed(3, 0);
  const WORD reusedId = Packed(4, 0);
  s_aFrameIds[emitted % 8][0] = oldId;
  s_aFrameIds[(emitted + 3) % 8][0] = reusedId;

  // The target was alive as oldId in the observation, then died during the
  // three-frame delay. Its slot now holds reusedId. Native lookup of oldId is
  // therefore null even though the emission-frame packed id is still nonzero.
  ResetApplyLookup(reusedId);
  std::vector<unsigned char> action = Action(kPlutoIdBase);
  bool ok = Check(TranslatePlutoCommands(emitted, &action), "apply-dead Action parses") &&
            Check(g_applyLookupCount == 1 && g_applyLastLookup == oldId,
                  "Action performs its liveness check on the emission id at apply time") &&
            Check(action.empty(), "apply-dead Action is removed, not translated to an old nonzero id");

  ResetApplyLookup(reusedId);
  std::vector<unsigned char> right = RightClick(kPlutoIdBase);
  ok = Check(TranslatePlutoCommands(emitted, &right), "apply-dead RightClick parses") &&
       Check(g_applyLookupCount == 1 && g_applyLastLookup == oldId,
             "RightClick performs the same apply-time liveness check") &&
       Check(right.empty(), "apply-dead RightClick cannot fall through as a location order") && ok;

  ResetApplyLookup(reusedId);
  std::vector<unsigned char> unload = Unload(kPlutoIdBase);
  ok = Check(TranslatePlutoCommands(emitted, &unload), "apply-dead targeted unload parses") &&
       Check(g_applyLookupCount == 1 && g_applyLastLookup == oldId,
             "targeted unload performs the same apply-time liveness check") &&
       Check(unload.empty(), "apply-dead targeted unload is removed") && ok;

  // Control: the same emission id still resolving to its live unit must retain
  // the command and its original nonzero packed id.
  ResetApplyLookup(oldId);
  action = Action(kPlutoIdBase);
  ok = Check(TranslatePlutoCommands(emitted, &action), "still-live Action parses") &&
       Check(g_applyLookupCount == 1 && g_applyLastLookup == oldId,
             "the live control also exercises the apply-time lookup") &&
       Check(action.size() == 11 && (action[5] | (action[6] << 8)) == oldId,
             "a target still resolving to the same live unit remains dispatchable") && ok;
  return ok;
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
  gPlayerData[1].nType = 0;  // closed row with an otherwise valid numeric id
  gPlayerData[1].dwPlayerID = 4;
  gPlayerData[1].dwStormID = oldStorm;
  g_StormToNationID[oldStorm] = g_StormToHumanID[oldStorm] = 5;
  RepairClassicStormMapping(oldStorm);
  if (!Check(g_StormToNationID[oldStorm] == 8 && g_StormToHumanID[oldStorm] == 8,
             "a sole closed remaining row cannot become a native participant"))
    return false;

  ResetPlayers();  // no row retains oldStorm
  g_StormToNationID[oldStorm] = g_StormToHumanID[oldStorm] = 5;
  RepairClassicStormMapping(oldStorm);
  return Check(g_StormToNationID[oldStorm] == 8 && g_StormToHumanID[oldStorm] == 8,
               "no remaining owner invalidates both old Storm entries");
}

bool TestPendingReplyTiming() {
  s_pendingCommands.clear();
  const DWORD emitted = 40;
  const DWORD received = emitted + 1;
  // PlutoFrame has sent frame E and incremented its clock before reading the
  // reply. Queueing at E+1 must preserve all three distinct timestamps.
  s_dwFrame = received;
  std::vector<unsigned char> bytes{0x1A, 0};
  QueuePlutoReply(emitted, std::move(bytes));
  bool ok = Check(s_pendingCommands.size() == 1, "one nonempty reply becomes one pending command");
  if (!ok) {
    s_pendingCommands.clear();
    return false;
  }
  const PendingCommand &pending = s_pendingCommands.front();
  ok = Check(pending.dwEmitted == emitted, "pending emission remains frame E") &&
       Check(pending.dwReceived == received, "pending receipt is frame E+1, never emission frame E") &&
       Check(pending.dwApplyAt == emitted + 3 && pending.dwApplyAt == received + 2,
             "pending dispatch is E+3 (two frames after actual receipt)") &&
       Check(pending.bytes == std::vector<unsigned char>({0x1A, 0}),
             "queueing preserves the reply bytes") && ok;
  s_pendingCommands.clear();
  return ok;
}

bool TestSeatAndPlayerIdMismatch() {
  ResetPlayers();
  gPlayerData[3].nType = 2;
  gPlayerData[3].dwPlayerID = 5;  // lobby seat and native player ID deliberately differ
  gPlayerData[3].dwStormID = 6;
  DWORD storm = 0, nation = 0, human = 0;
  bool ok = Check(PrepareClassicPlayer(3, &storm, &nation, &human), "valid unique owner prepares") &&
            Check(storm == 6, "an already valid unique Storm owner is preserved") &&
            Check(nation == 5 && human == 5, "nation/human come from dwPlayerID, not lobby slot 3") &&
            Check(g_StormToNationID[6] == 5 && g_StormToHumanID[6] == 5,
                  "Storm lookup arrays agree with the player-record mapping");
  storm = nation = human = 0;
  ok = Check(ResolveClassicPlayer(3, &storm, &nation, &human), "prepared owner resolves") &&
       Check(storm == 6 && nation == 5 && human == 5, "dispatch-time replay-style mapping agrees") && ok;

  // Aliasing another seat must not dispatch as that seat. Remap to a unique
  // classic owner, then resolve back to seat 3 and native player ID 5.
  gPlayerData[1].nType = 2;
  gPlayerData[1].dwStormID = 2;
  gPlayerData[3].dwStormID = 2;
  ok = Check(PrepareClassicPlayer(3, &storm, &nation, &human), "aliased owner is repaired") &&
       Check(storm < 8 && storm != 2, "aliased owner receives a free classic Storm slot") &&
       Check(nation == 5 && human == 5, "remapping retains the seat's native player ID") && ok;
  for (int i = 0; i < PlayerLobbySlots(); ++i)
    if (i != 3)
      ok = Check(gPlayerData[i].dwStormID != storm, "assigned Storm owner is unique") && ok;
  return ok;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  const std::string scenario = argv[1];
  bool ok = false;
  if (scenario == "no-target")
    ok = TestNoTargetSentinel();
  else if (scenario == "missing-target")
    ok = TestMissingTargetDropsWholeCommand();
  else if (scenario == "apply-reuse")
    ok = TestApplyTimeDeathAndSlotReuse();
  else if (scenario == "pending-timing")
    ok = TestPendingReplyTiming();
  else if (scenario == "repair-mapping")
    ok = TestRepairClassicStormMappingParticipants();
  else if (scenario == "seat-mismatch")
    ok = TestSeatAndPlayerIdMismatch();
  else
    return 2;
  return ok ? 0 : 1;
}
