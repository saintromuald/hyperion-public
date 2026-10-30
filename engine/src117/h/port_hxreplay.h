// port_hxreplay.h -- Hyperion's replays: the games past 1.17's limits, and their rule
// record (port/replays/port_hxreplay.cpp).
#ifndef PORT_HXREPLAY_H
#define PORT_HXREPLAY_H

#include <string>
#include <vector>

// "hxRS": a replay of Hyperion's, in place of 1.17's "reRS".
#define REPLAY_MAGIC_HX 0x53527868

// Whether the game plays by Hyperion's rules (h/hxrules.h), so its replay is Hyperion's:
// noted as the game starts (game_init) and again as it ends, before its map is closed, and asked
// when its replay is written, whole or a room's feed as it goes (replay_live_parts).
bool ReplayHxGame();
void ReplayHxNoteGame();
bool ReplayHxNoted();
// After the map block: Hyperion's sections. 0 on a write error.
int ReplayHxWrite(void *pFile);
// After the map block of an "hxRS" replay: its sections. 0 when they are missing or broken,
// or when the replay's rules or game data are not this build's.
int ReplayHxRead(void *pFile);
// The rule record (the rule version, the game data's fingerprint, the build, the
// simulation's revision, the game's options and mod), which a save of a Hyperion game
// carries too: its size; whether a record of cb bytes is one this build reads (its own,
// or revision 11's shorter one, which it then refuses by its revision); this build's
// record; why one of cbRule bytes would be refused, or NULL when this build plays it; and
// its rules and options set for the game it starts (h/hxrules.h, RulesRecorded).
unsigned ReplayHxRuleSize();
bool ReplayHxRuleSizeKnown(unsigned cb);
void ReplayHxRuleMake(void *pOut);
const char *ReplayHxRuleRefusal(const void *pRule, unsigned cbRule, const unsigned char *pabData = 0);
void ReplayHxRuleApply(const void *pRule, unsigned cbRule);
// After 1.17 writes a game's LastReplay: a copy kept under the game's own name in
// maps\replays\AutoSave, while Settings' "Save a replay of every game" is on. pszMap names the
// map when the current one is not the game's (a watched skirmish's, which plays as a replay).
void PortReplayKeep(const char *pszMap = 0);
// In a replay's game set-up: the slots past 12 as recorded, after 1.17's twelve.
void ReplayHxAssignSlots();
// After the game has coloured the slots: each slot from 8 on as the replay recorded it.
void ReplayHxColors();
// A replay read that is not played, or a 1.17 replay: nothing recorded applies, and a
// 1.17 replay plays by classic rules.
void ReplayHxForget();

// A replay read for a list and not played (the replays screen, port_front_replays.cpp):
// 1.17's header as the file has it, and a Hyperion replay's record; with pChk, the map it
// carries. False when the file is not a replay.
struct ReplayHxInfo {
  bool bHx;                          // "hxRS", else 1.17's "reRS"
  unsigned char abHead[0x279];       // the header (replay.cpp, LoadReplayData)
  std::vector<unsigned char> past;   // the player records past 1.17's 12 (SLOT), 36 bytes each
  std::vector<unsigned char> races;  // a mod's race a slot played, 0 for none (RACE)
  bool bRule;                        // the rule record was read
  char szRule[16];
  char szBuild[32];
  char szMod[32];
  char szDepends[96];                // the mods with data szMod depends on, "id version, ..."
  unsigned int dwSimRevision;
  unsigned int dwOptions;
  unsigned int dwChecks;             // the state's checks it holds (SYNC)
  std::string refusal;               // why this build would not play it, "" when it plays
};
bool ReplayHxPeek(const char *pszPath, ReplayHxInfo *pOut, std::vector<unsigned char> *pChk);

// The state's check (port_synchash.h): a Hyperion game records its hash every second of
// game time, and its replay, played back, compares each with the state as it plays.
enum {
  REPLAY_SYNC_NONE,        // a 1.17 game, or a replay with no checks
  REPLAY_SYNC_RECORDING,   // a Hyperion game being played
  REPLAY_SYNC_OK,          // a replay whose checks so far all match
  REPLAY_SYNC_MISMATCH,    // a replay that differs from the game it recorded
  REPLAY_SYNC_REFUSED,     // a replay this build would not play
};
#define REPLAY_SYNC_RAND 0x100  // in dwBadDomains: the random state, beside the hash's domains
struct ReplayHxSyncStatus {
  int nState;
  unsigned int dwRecorded;    // checks recorded, or in the replay
  unsigned int dwChecked;     // checks made so far
  unsigned int dwFirstBad;    // the first frame that differed
  unsigned int dwBadDomains;  // its domains, a bit each (SYNC_UNITS first), and REPLAY_SYNC_RAND
  char szReason[64];          // why a replay was refused
};
// At a game's start, after its set-up: what is recorded or checked starts again.
void ReplayHxGameStart();
// The checks of the replay being played, as its SYNC holds them (the trainer holds a
// session started from the replay to them).
struct SyncHash;
const std::vector<SyncHash> &ReplayHxExpected();
// After each frame's update and triggers: the frame's check, and in a replay the saves its
// game made after that frame.
void ReplayHxFrame();
// A Hyperion game is being saved (saveload_write_save_data): the replay notes the frame.
void ReplayHxSaved();
// The game went back to the end of a frame (the trainer's snapshots, port_snap.cpp): a playback
// checks the frames after it again, a recording drops what it recorded after it.
void ReplayHxRewound(unsigned long dwFrame);
const ReplayHxSyncStatus *ReplayHxSync();

#endif
