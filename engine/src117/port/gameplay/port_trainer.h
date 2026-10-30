// port_trainer.h -- the trainer: a game played as a session of lines that can go back
// to any moment. Each line is the commands its game was given, frame by frame, from the game's
// start; a line branched from another shares the other's snapshots up to where they part
// (port_snap.cpp). The game itself is always at the playhead: going back puts a snapshot back
// and plays the line's commands on to the frame asked for.
//
// A line plays by the rules its game started with. Nothing here changes a rule: a line's game
// is the game its commands make, and the replay the game records is the line it ends on.
#pragma once

#include <string>
#include <vector>
#include "../front/port_frontscreens.h"  // SkirmishGame

enum { TRAINER_TIMELINE, TRAINER_ATTEMPT, TRAINER_REPLAY };
enum { TRAINER_MARK_YOURS, TRAINER_MARK_WAVE, TRAINER_MARK_FELL, TRAINER_MARK_END, TRAINER_MARK_FIGHT };
enum { TRAINER_RESULT_NONE, TRAINER_RESULT_WON, TRAINER_RESULT_LOST };
// What the game does: plays on at a line's end, its commands the player's; plays a line's
// commands on (any order of the player's takes over); or runs at once to a frame asked for.
enum { TRAINER_LIVE, TRAINER_WATCH, TRAINER_SEEK };
// Going on from a line's middle (the Tape's four choices).
enum { TRAINER_GO_AGAIN, TRAINER_GO_TIMELINE, TRAINER_GO_REWRITE, TRAINER_GO_WATCH };

const unsigned TRAINER_SNAP_EVERY = 120;  // frames between snapshots, 5 seconds at Fastest
const unsigned TRAINER_SECOND = 24;       // frames a game second, as the Tape counts them
const int TRAINER_ATTEMPTS_HELD = 0;      // the tries a session holds: 0, as many as it makes
const unsigned TRAINER_CHECK_EVERY = 1200;

struct TrainerCommand {
  unsigned nFrame;  // the game frame it acts in (the counter as it runs)
  unsigned char bStorm;
  std::vector<unsigned char> ab;
};

// A mark: the player's own (M), an attack wave at the player's base (nWho its units), a base
// that fell (nWho the player's slot whose it was), the game's end (nWho the result), a fight
// (nWho the player's army units in it).
struct TrainerMark {
  unsigned nFrame;
  int nKind;
  int nWho = 0;
};

// A unit or building of the player's that came to be: placed, started or morphed into.
struct TrainerBuilt {
  unsigned nFrame;
  unsigned short wType;
  unsigned short nSupply;  // the supply used then, whole
};

// The player's figures at the end of a game second.
struct TrainerSecond {
  int nMinerals, nGas;          // the bank
  int nMined;                   // gathered, minerals and gas
  int nSupplyUsed, nSupplyMax;  // whole supply, the player's race
  int nWorkers, nArmy;          // completed workers; the army's cost, minerals and gas
  int nMade, nLost, nKilled;    // units
  int nActions;                 // the player's commands since the game's start
  int nLostValue, nKilledValue; // minerals and gas, units and buildings
  int nTheirArmy;               // the enemies' armies' cost, together
  int nBlocked, nBlocks;        // seconds the supply was full, and the times it filled
  int nEffective;               // the actions EAPM keeps
  int nWorkersLost;
  int nNearBase;                // enemy army units at the player's bases
  int nHallsLost;               // the player's town halls lost
  int nEnemiesOut;              // enemies out of the game
  int nFighting;                // the player's army units within 6 tiles of an enemy's
};
// TrainerSecond's fields in their saved order (a session saved before the later ones reads 0).
extern int TrainerSecond::*const kTrainerSecondFields[];
extern const int kTrainerSecondCount;

// The game's state hash at a frame, as a line first played it: a saved session played again
// is held to them.
struct TrainerCheck {
  unsigned nFrame;
  unsigned long long qHash;
};

struct TrainerLine {
  int nId, nParent;  // nParent 0 for the first line
  unsigned nSplit;   // the frame it parts from its parent: its own from there
  unsigned nEnd;     // the furthest frame played on it
  int nKind;
  std::string name;
  int nPoint;        // the retry point it was tried from, -1
  int nResult;
  unsigned nOrder;   // made, in order
  std::vector<TrainerCommand> commands;  // by frame, from the game's start
  std::vector<TrainerMark> marks;
  std::vector<TrainerSecond> seconds;    // one a game second from frame 0
  std::vector<TrainerCheck> checks;      // every TRAINER_CHECK_EVERY frames
  std::vector<TrainerBuilt> built;       // by frame
  unsigned nBuiltTo = 0;                 // the last frame looked at for what was built
};

struct TrainerPoint {
  int nLine;
  unsigned nFrame;
  std::string name;
  int nTries;
};

// A session's set-up (the mod's entry screen, port_front_trainer.cpp): how it runs, and its
// game, which a saved session keeps to make the same game again.
struct TrainerSetup {
  std::string name;                // the session's, as the Sessions tab lists it
  std::string open;                // a saved session's file to open, "" for a new one
  bool bPointsEachMinute = false;  // a retry point each game minute as well as the player's own
  int nAttemptsHeld = TRAINER_ATTEMPTS_HELD;  // 0: no limit
  bool bAsk = true;                // going on from a line's middle asks the four ways; else tries again
  std::string map, mapName;        // the map's file and title
  std::string race = "terran";     // the player's: terran, zerg, protoss or random
  std::string races;               // the computers', a letter each (t, z, p, r)
  int nComputers = 1;
  bool bFfa = false;               // each for itself; else the computers allied (1.17's melee)
  int nSpeed = 6;                  // 1.17's speeds, 6 Fastest
  int nRules = -1;                 // Hyperion's (1) or 1.17's (0), -1 the setting's
  unsigned nSeed = 0;              // new_game's; 0 the clock's, kept once the game starts
  // A replay the session starts from, in place of the game above: loaded as a replay is, then
  // played live with the player in nTakeSlot's seat (the replay's player record, -1 for the
  // person whose name is the profile's, else the first person), the replay its first line.
  std::string replay;
  int nTakeSlot = -1;
  // The game as the skirmish set-up made it (the trainer's New Session, set up on the skirmish
  // screen), in place of the map, races and sides above, which then only describe it.
  bool bGame = false;
  SkirmishGame game;
};

// The next game a session set up so, and made (the mod's start); false when it could not be.
bool PortTrainerFromMenus(const TrainerSetup &s);
// The running session's set-up.
const TrainerSetup &PortTrainerSetupNow(void);
// new_game's seed for a session making its game again (PortObserveSeed); false for the clock's.
bool PortTrainerSeed(int *pnSeed);
// The speed a session's game starts at, -1 when this game is not one set up from the menus.
int PortTrainerSpeed(void);
// The game's end: the session's timelines and what they came from saved, the attempts let go.
void PortTrainerGameEnd(void);
// Once after a session set up on the menus has ended: the menus go home, not on to 1.17's
// campaign screen (port_front.cpp).
bool PortTrainerAfterGame(void);
// Once after a session set up on the menus has ended, saved or not: the trainer's entry opens
// on its sessions.
bool PortTrainerCameBack(void);
// The file the last session was saved to, "" for none yet.
std::string PortTrainerLastSaved(void);

// A saved session, as the Sessions tab lists it (Trainer\Sessions in the data folder).
struct TrainerSaved {
  std::string path, name, when, source;
  TrainerSetup setup;
  int nTimelines = 0, nAttempts = 0;
  unsigned nLongest = 0;
  bool bOtherBuild = false;  // made by a build whose rules differ: it may not play the same
  std::vector<TrainerLine> shape;  // its lines, no commands, for the tree
};
std::vector<TrainerSaved> PortTrainerSessions(void);
// Asked once: the Tape to come up now (a saved session opens paused at its start).
bool PortTrainerTapeWanted(void);
// Frames where a saved session played differently than when it was made, 0 when none.
unsigned PortTrainerDiffered(void);

// The session is running in this game.
bool PortTrainerSession(void);
// The game loop's: a game's start, the top of each turn of the loop (a watched line's
// commands), a command the game ran (the recorder's), a frame's end.
void PortTrainerGameStart(void);
void PortTrainerFeed(void);
void PortTrainerCommand(int nStorm, const void *pCmd, unsigned cbCmd);
void PortTrainerFrameEnd(void);
// The loop runs frame after frame at once and draws nothing while a seek runs.
bool PortTrainerSeeking(void);
// A line's commands are being played on: --record-orders' own orders hold off.
bool PortTrainerWatching(void);

// The session, for the views.
const std::vector<TrainerLine> &PortTrainerLines(void);
const TrainerLine *PortTrainerLine(int nId);
int PortTrainerCurrent(void);
int PortTrainerState(void);
const std::vector<TrainerPoint> &PortTrainerPoints(void);
// The retry point R goes to from here, -1 for none.
int PortTrainerRetryPoint(void);
// The snapshot the game was last put back from and the frames played on from it.
void PortTrainerLastSeek(unsigned *pnSnap, unsigned *pnFrames);
// A line's figures at a frame (its last second at or before it); false before its first.
bool PortTrainerSecondAt(int nLine, unsigned nFrame, TrainerSecond *pOut);

// The player's actions. Each says what it did in a line for the view to show (the toasts), and
// returns false when it could not.
bool PortTrainerRetry(std::string *pSaid);
bool PortTrainerSetPoint(std::string *pSaid);
bool PortTrainerKeep(int nLine, std::string *pSaid);
bool PortTrainerBranch(std::string *pSaid);
bool PortTrainerMarkHere(std::string *pSaid);
// Paused: the playhead to a line and frame, the game put there.
bool PortTrainerScrub(int nLine, unsigned nFrame);
// The game stopped for the Tape, and on again at a line's end; held is paused or going to a
// frame to pause at.
void PortTrainerPause(void);
void PortTrainerResume(void);
bool PortTrainerHeld(void);
// A new attempt from this frame of the line being watched (or paused in), yours from here.
bool PortTrainerTakeOver(std::string *pSaid);
// Out of the game to the menus, as 1.17's Quit Mission.
// Leaving a session: it is saved with every line, retries too, with its timelines only, or not
// at all (a session opened from a file keeps the file as it was). A game ended any other way
// (the Game Menu, the window closed) saves every line.
enum { TRAINER_SAVE_ALL, TRAINER_SAVE_TIMELINES, TRAINER_SAVE_NONE };
void PortTrainerLeave(int nSave, const std::string &name);
// Where saving under a name would write (Trainer\Sessions\<name>.hxtrain, or the session's own
// file when it was opened and keeps its name).
std::string PortTrainerSavePath(const std::string &name);
// Whether a session opened from its file has changed since: one left unchanged leaves without
// asking. A new session has always changed.
bool PortTrainerChanged(void);
// Paused mid-line, going on: try again, try again as a timeline, rewrite or watch.
bool PortTrainerGoOn(int nChoice, std::string *pSaid);

// m:ss of a frame, as the Tape writes times.
std::string PortTrainerTime(unsigned nFrame);

// The trainer's screen (port_trainerview.cpp), a layer over the game (port_overlay.h): the game
// placed in it, the strip or the Tape drawn, the mouse and keys meant for it taken.
bool PortTrainerViewShown(void);
// The live channel's (port_control.cpp): a key with the modifiers given, the Tape up, and the
// session as JSON.
bool PortTrainerViewKeyAs(unsigned vk, bool bCtrl, bool bShift);
bool PortTrainerViewTape(void);
std::string PortTrainerJson(void);
