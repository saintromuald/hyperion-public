// port_frontscreens.h -- the front end's screens past the home screen (port_front.cpp runs
// them; docs/front-end.md, F4). Each screen lays itself out in the frame the loop draws, reads
// the frame's keys, and says when it is done.
#ifndef PORT_FRONTSCREENS_H
#define PORT_FRONTSCREENS_H

#include <string>
#include <vector>

#include "../gameplay/port_session.h"  // MutatorSet

struct PortBenchSettings;
struct TrainerSetup;
struct ScoreGame;

// A game as the skirmish set-up makes it, held as a record so it can be made again from the
// record alone (a trainer session's game): the map, the game type's template key,
// the speed, the rules, and the seats, the local player's first and then the computers in turn
// (the rows PlayersChooseColour counts). PortLaunchSkirmishGame (port_launch.cpp) makes it as
// Start Game does, through CreateScenarioGame.
struct SkirmishSeat {
  int nRace = 6;     // 0 Zerg, 1 Terran, 2 Protoss, 6 random; 0x100 + n a mod's race n
  int nColour = -1;  // the game's colour chosen, one of 65 (PLAYER_COLOURS), -1 the slot's own
  unsigned rgb = 0;  // a team's shade or the row's own colour in place of it, 0 none
  int nTeam = 0;     // 1 and on; 0 none
  int nLevel = -1;   // a computer's level (port_ai.h), -1 none chosen
  int nStart = -1;   // the map's start it plays from, -1 drawn
  int nHandicap = 100;  // the share of its units' life and shields, 50 to 100 (port_handicap.h)
};
struct SkirmishGame {
  std::string map;    // the path the game opens
  std::string title;  // the map's
  std::string type;   // the game type's name, as the set-up lists it
  int nKey = 0;       // the template's: category | sub << 8 | type << 16
  bool bMelee = true;     // a melee template: the seats' races are the game's
  bool bColours = false;  // the seats' colours chosen
  int nSpeed = 6;
  bool bHyperion = false;
  MutatorSet mut;  // the game's mutators (port_mutators.h), 1.17's when none was chosen
  std::vector<SkirmishSeat> seats;
  // watched: every seat a computer, the player's own too, and the watch's speed past Fastest,
  // 1 to 16 (port_observe.h); Start Game's alone
  bool bObserve = false;
  int nWatchMul = 1;
};
int PortLaunchSkirmishGame(const SkirmishGame &g);
// Its choices for the game, which the session keeps (port_session.h): the races, colours,
// teams, starts and computers' levels a seat each, the computers past seven, the rules and
// the mutators.
// Start Game and PortLaunchSkirmishGame both make them.
void PortLaunchSkirmishChoices(const SkirmishGame &g);
// A seat's race as 1.17 seats it: a mod's race as its base.
unsigned char PortLaunchRaceBase(int nRace);

namespace FrontKit {

// Another screen can host the skirmish set-up (the trainer's New Session): the same map step
// and set-up, with the host's buttons at the front of the header, its box in the mutators'
// place, and its own Start. A host keeps a set-up of its own, so it never changes Skirmish's.
struct SkirmishHost {
  const char *pszId;
  const char *pszLine;      // in the line under the map's name, where "Skirmish" stands
  const char *pszStart;     // Start Game's label
  const char *pszPickHint;  // under the map step's list, before a map is chosen
  void (*pfnHead)(void);    // its buttons, laid out in the header's row
  void (*pfnBox)(float x, float y, float w, float h);  // in the mutators' place
  void (*pfnStart)(void);   // Start, pressed
  bool (*pfnKeys)(void);    // its keys, before the set-up's; true when it took one
  void (*pfnBack)(void);    // Back from the map step (a guest's Leave)
  // A LAN room's lobby: 1 the host's set-up, whose seats may be open to the room's
  // people or hold one of them; 2 a guest's, the host's set-up shown, only their own race
  // theirs to change. A room offers the game types 1.17's own lobby seats as it is told.
  int nRoom;
  const char *(*pfnStartLabel)(void);  // Start's label as it stands (Ready, Not Ready), or NULL
};
// The set-up as a game to make, checked as Start Game checks it; false with the reason shown
// in the set-up's note.
bool SkirmishGameNow(SkirmishGame *pOut);

// A room's lobby as the room keeps it (port_lanroom.cpp): the map, the game type, speed and
// rules, and each seat: "open", "closed", "computer" or "player", its race (as SkirmishSeat's)
// and handicap, and a player's name, seat chip, Ready, and whether it is this copy's own seat.
struct SkirmishRoomSeat {
  std::string owner;
  int nRace = 6;
  int nHandicap = 100;  // a person's their own, a computer's the host's (port_handicap.h)
  std::string name, chip;
  bool bReady = false, bYou = false;
};
struct SkirmishRoomSetUp {
  std::string map, title, type;  // the map's path here, its title, the game type's name
  int nKey = 0, nSpeed = 6, nPlayers = 0;
  bool bHyperion = false;  // the match plays by Hyperion's rules, the host's choice
  MutatorSet mut;          // and its mutators, the host's
  std::vector<SkirmishRoomSeat> seats;
};
// The host's set-up as the room should have it; false before a map is chosen.
bool SkirmishRoomNow(SkirmishRoomSetUp *pOut);
// The room's people in the host's open seats, each frame; a guest's whole set-up likewise.
void SkirmishRoomSeats(const std::vector<SkirmishRoomSeat> &seats);
void SkirmishGuestSetUp(const SkirmishRoomSetUp &r);

// What a screen chose: 0 while it runs.
// (Past 1.17's main-menu results, which the loop's g_nChoice also holds.)
enum {
  FRONT_SKIRMISH_STARTED = 0x10, FRONT_SKIRMISH_HOME = 0x11, FRONT_CAMPAIGN_DONE = 0x12,
  FRONT_REPLAY_STARTED = 0x13, FRONT_REPLAYS_HOME = 0x14, FRONT_LOAD_STARTED = 0x15,
  FRONT_BENCH_STARTED = 0x16, FRONT_BENCH_HOME = 0x17, FRONT_TRAINER_STARTED = 0x18, FRONT_TRAINER_HOME = 0x19
};

// Skirmish (port_front_skirmish.cpp): Choose a Map, then the set-up; Start Game makes the
// game as Play Custom does. nSpeed is the speed it starts at; pHost the screen hosting it, NULL
// for Skirmish itself.
void SkirmishOpen(int nSpeed, const SkirmishHost *pHost = NULL);
void SkirmishScreen(void);
bool SkirmishKeys(void);
int SkirmishResult(void);

// The score screen (port_front_score.cpp), in 1.17's score screen's place after a
// game (PortFrontScore): the game's players and numbers (port_scoregame.h); OK gives
// FRONT_SCORE_DONE. bFromHome when a check opens it from home with stand-in players
// (HYPERION_FRONT_OPEN=score-<preset>). ScoreClose stops its counting's sound.
enum { FRONT_SCORE_DONE = 0x1F };
void ScoreOpen(const ScoreGame &g, bool bFromHome);
void ScoreScreen(void);
bool ScoreKeys(void);
int ScoreResult(void);
void ScoreClose(void);
// The set-up of the game the skirmish screen last started, as the score screen names it: the
// map's title, the game type's name, and its teams' names (a team's number less one), none
// when it had no teams.
void SkirmishPlayed(std::string *pTitle, std::string *pType, std::vector<std::string> *pTeams);

// Campaign (port_front_campaign.cpp): the episodes, then an episode's missions; Play Mission
// makes the mission as 1.17's campaign screen does. Done, CampaignGluesMode is the glue screen
// to go on to.
void CampaignOpen(void);
void CampaignClose(void);
void CampaignScreen(void);
bool CampaignKeys(void);
int CampaignResult(void);
int CampaignGluesMode(void);
// Watch Again: whether a cinematic was chosen; with the screen black, it set to play as the
// history list sets one (game mode 10); and whether one was, once, so the game comes back here.
bool CampaignWatching(void);
void CampaignWatchStart(void);
bool CampaignWatchedTake(void);

// Settings' colour theme (port_front_theme.cpp): the presets and Custom as buttons, and under
// Custom its picker, in the settings well. *pnTheme is the choice ("colourTheme"), *pCustom
// Custom's colours, both changed as the player picks. ThemePickerReset as Settings opens.
// ThemePickerTyping: an entry has the keyboard, so the dialog's letter keys stand aside
// (Escape leaves the entry).
struct ThemeCustom;
void ThemeGroup(int *pnTheme, ThemeCustom *pCustom, bool bLive);
void ThemePickerReset(void);
bool ThemePickerTyping(void);

// Rereads the profile's progress after the profile changed under the campaign screen.
void CampaignReread(void);

// The episodes of the game in use, each one's number ("Episode I"), and how many of an
// episode's missions a character record (1.17's 0x70 bytes) has done, of *pnTotal.
int CampaignEpisodes(void);
const char *CampaignEpisodeNumber(int nEpisode);
int CampaignDone(const unsigned char *pRec, int nEpisode, int *pnTotal);

// Replays (port_front_replays.cpp): My Replays, the replays folder read for a list; Watch
// plays one as 1.17's replay list does.
void ReplaysOpen(void);
void ReplaysClose(void);
void ReplaysScreen(void);
bool ReplaysKeys(void);
int ReplaysResult(void);

// The benchmark's set-up (port_front_bench.cpp), the benchmark mod's own screen:
// New Run and the runs logged. Start Benchmark gives FRONT_BENCH_STARTED, and BenchTake then
// hands over its settings, once, for PortBenchFromMenus. BenchOpen(true) opens on Runs with the
// last run picked; BenchCameBack is true, once, when a run has ended since it was last asked.
void BenchOpen(bool bRuns);
bool BenchTake(PortBenchSettings *pOut);
bool BenchCameBack(void);
void BenchScreen(void);
bool BenchKeys(void);
int BenchResult(void);

// The trainer's entry screen (port_front_trainer.cpp), the trainer mod's own screen:
// New Session and the sessions saved. Start Session or Open Session gives FRONT_TRAINER_STARTED,
// and TrainerTake then hands over the set-up, once, for PortTrainerFromMenus. TrainerOpen(true)
// opens on Sessions; TrainerCameBack is true, once, when a session has ended since it was last
// asked.
void TrainerOpen(bool bSessions);
bool TrainerTake(TrainerSetup *pOut);
bool TrainerCameBack(void);
void TrainerScreen(void);
bool TrainerKeys(void);
int TrainerResult(void);

// The AI lab's screen (port/lab/port_front_lab.cpp), the lab mod's own: its farm of
// games and the games played. LabOpen(true) opens on the games. LabResult gives FRONT_LAB_HOME to
// go home, or FRONT_LAB_WATCH when a game is to be watched; LabCameBack is true, once, when a
// game watched from the lab has ended.
enum { FRONT_LAB_HOME = 0x1D, FRONT_LAB_WATCH = 0x1E };
void LabOpen(bool bGames);
bool LabCameBack(void);
void LabScreen(void);
bool LabKeys(void);
int LabResult(void);
const char *LabPanelId(void);

// Load Game (port_front_load.cpp), the dialog over the campaign screen: the profile's
// saves; Load loads one as 1.17's Load screen does, and LoadGameStarted is then true.
// GameSavesOpen opens it over a game as the game menu's Load Game or Save Game: what
// it chose waits for GameSavesStep, out of the frame, which is true once the load or the save
// has gone out; GameSavesClose puts it away.
void LoadGameOpen(void);
void GameSavesOpen(bool bSave);
bool GameSavesStep(void);
void GameSavesClose(void);
bool LoadGameUp(void);
void LoadGameDialog(void);
bool LoadGameKeys(void);
bool LoadGameStarted(void);

// The player card and Create a Profile (port_front_profile.cpp). ProfileCurrent is the
// profile chosen, "" when there is none or its file is gone. ProfileToggle opens and closes
// the card, ProfileScreen draws what is open and ProfileKeys reads its keys (true when taken).
// ProfileGate is true when there is a profile, else opens Create a Profile, and ProfileThen
// hands back pszThen, once, when one is made. ProfileChanged is true, once, after a change.
// ProfilesOpen opens the profiles dialog (Manage Profiles), with Import StarCraft IDs.
std::string ProfileCurrent(void);
// The current profile's folder, profiles\<name> in the data folder; "" with no profile.
std::string ProfileFolder(void);
void ProfilesOpen(void);
// From profiles\<name>\profile.json: the race last started with ("zerg", "terran", "protoss",
// "random" or a mod race's name; "" before the first); ProfileCountGame counts a game started.
std::string ProfileLastRace(void);
void ProfileSetLastRace(const char *pszRace);
void ProfileCountGame(void);
void ProfileToggle(void);
void ProfileClose(void);
bool ProfileGate(const char *pszThen);
const char *ProfileThen(void);
bool ProfileChanged(void);
void ProfileScreen(void);
bool ProfileKeys(void);

// The release notes (port_front_notes.cpp, docs/versioning.md): the log the build carries.
// Its newest entries, at most three, are the home screen's notifications: NotesToasts says
// how many, NotesToastGone whether one was closed (kept in the settings file), NotesLeft how
// many are open, NotesToast what one says. NotesOpen opens Patch Notes at an entry, the
// window NotesDialog draws over the home screen while NotesUp, NotesKeys reading its keys.
// NotesVersionLine is the version with the build's number and commit, as Settings shows it.
struct NotesToastText {
  bool bAnnouncement = false;
  std::string tag, age;
  const char *pszTitle = "";
  const char *pszLine = "";
};
int NotesToasts(void);
bool NotesToastGone(int i);
int NotesLeft(void);
void NotesDismiss(int i);
void NotesDismissAll(void);
NotesToastText NotesToast(int i);
void NotesOpen(int nEntry);
void NotesClose(void);
bool NotesUp(void);
void NotesDialog(void);
void NotesKeys(void);
std::string NotesVersionLine(void);

// The standard dialog (port_front.cpp): the button chosen, -1 none yet.
int FrontDialog(const char *pszTitle, const char *pszBody, const char *const *ppszButtons, const char *pszKeys,
                int nButtons);

// The front end's log (hyperion_front.log).
void FrontLog(const char *pszFormat, ...);

// The LAN room this copy is in (port_front_lan.cpp), each frame of any screen: what
// the room sent, and true when a match it launched has been made or joined, so the front end
// hands over to 1.17's chat room (PORT_FRONT_LAN).
bool LanFrontFrame(void);

// Multiplayer (port_front_lan.cpp): the rooms on this network, Host a Room and Join a
// Room; the host's console (port_front_lanhost.cpp); a room as a guest sees it; and a match's
// lobby, the skirmish set-up hosted. LanOpen opens on the console or the room this copy is in,
// else Multiplayer; LanCameBack says, once, that the front end has come back from a room's
// match (or is in a room), so it opens here again. LanResult gives FRONT_LAN_HOME, or
// FRONT_LAN_ORIGINAL for 1.17's own multiplayer screens.
enum { FRONT_LAN_HOME = 0x1A, FRONT_LAN_ORIGINAL = 0x1B, FRONT_LAN_WATCH = 0x1C };
void LanOpen(void);
void LanScreen(void);
bool LanKeys(void);
int LanResult(void);
bool LanCameBack(void);
// --join's room has let this copy in, once: the front end opens the room.
bool LanJoinedNow(void);
const char *LanPanelId(void);

}  // namespace FrontKit

#endif
