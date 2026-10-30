// port_hudstate.h -- the HUD's data layer: what a HUD file's bindings and events
// read from the game, the same values 1.17's console shows, and the presses its buttons and
// slots make, each ending in 1.17's own handler so the same command is sent. The names follow
// the HUD format (docs/hud-format.md, "Bindings" and "Events").
//
// Read only, apart from the presses: nothing here changes the game, and a replay plays the
// same with it. The game's files call the hooks below; the HUD and the live channel read the
// rest. Plain types only, since game files include it.
#pragma once

class CUnit;

// The game's hooks, each reading only. PortHudGameReset as a game's console is set up;
// PortHudFrame after each game frame; PortHudAttack as one of a player's units is hit
// (gamesnd_under_attack, before 1.17's own tests); PortHudError as a player is told an
// order failed (the string's id); PortHudDone as research or an upgrade finishes (0), a unit
// is made (1) or a building is finished (2); PortHudCommand as a player's command is carried
// out (net_process_game_turn), for the actions a minute; PortHudGroup as a player's control
// group is recalled (bSet false) or stored (bSet true), carried out (group_un.cpp).
void PortHudGameReset(void);
void PortHudFrame(void);
// The game's start for the HUD, game.start and console.in, once a game: as a skirmish's map
// comes in for its countdown, when the original's console rises (port_intro.cpp), else at the
// game's first frame (PortHudFrame). PortHudGameStarted: it came.
void PortHudArrive(void);
bool PortHudGameStarted(void);
void PortHudAttack(CUnit *pUnit);
void PortHudError(int nPlayer, int nStringId);
void PortHudDone(CUnit *pUnit, int nWhat);
void PortHudCommand(const unsigned char *pCmd);
void PortHudGroup(int nGroup, bool bSet);

// One of the command card's nine places.
struct PortHudButton {
  bool bShown;         // a button is there
  bool bEnabled;       // and it can be pressed (1.17 greys one it cannot)
  int nIcon;           // its icon, a frame of cmdicons.grp
  char cHotkey;        // the key that presses it, lowercase, or 0
  int nCostKind;       // the tooltip's kind: 1 unit, 2 upgrade, 3 tech energy, 4 tech cost, 5 cost only
  char szText[256];    // the tooltip as 1.17 writes it, its colour bytes kept
};

// The buttons 1.17's console has beside the card, and Hyperion's own.
enum PortHudButtonKind {
  PORT_HUD_BTN_MENU,       // the Game Menu (F10), always shown
  PORT_HUD_BTN_TERRAIN,    // the minimap's terrain toggle, single player
  PORT_HUD_BTN_DIPLOMACY,  // Diplomacy, View Players in a replay
  PORT_HUD_BTN_MESSAGING,  // Messaging, multiplayer
  PORT_HUD_BTN_IDLE,       // the next idle worker (Hyperion's)
  PORT_HUD_BTN_ARMY,       // the army (Hyperion's)
  // a replay's: 1.17's replay card's three, as its keys press them, and Hyperion's camera
  PORT_HUD_BTN_PLAY,       // Play or Pause (P)
  PORT_HUD_BTN_SLOWER,     // Slower (D), greyed at Slowest and while paused
  PORT_HUD_BTN_FASTER,     // Faster (U), greyed at Fastest x16 and while paused
  PORT_HUD_BTN_TV,         // Replay TV (F12), a replay
  PORT_HUD_BTN_FOLLOW,     // Follow the selected unit (F11), any game
  PORT_HUD_BTN_KINDS
};

// A player on the players' board: every seat of the game, those gone included. The
// stats a viewer calls up list up to PORT_HUD_UNIT_TYPES of a player's army's unit types.
enum { PORT_HUD_PLAYERS = 64, PORT_HUD_UNIT_TYPES = 24 };
struct PortHudPlayer {
  int nSlot;
  int nRace;                    // 0 zerg, 1 terran, 2 protoss
  int nTeam;                    // the lobby's team, 0 in a game without teams
  unsigned int nColour;         // the minimap colour, 0xRRGGBB
  char szName[48];              // UTF-8
  char szLevel[24];             // a computer's AI level in a game played with chosen levels, else ""
  int nMinerals, nGas;
  int nSupplyUsed, nSupplyMax;  // the player's race's, in whole supply
  int nWorkers;
  int nApm;                     // as nApm below, for this player
  bool bSeen;                   // the viewer sees what the player sees (a replay's vision)
  bool bEye;                    // and can turn it: 1.17's vision has the lobby's eight
  // the stats' (port_hudstats.cpp): the army, the living units that are not workers,
  // Overlords or buildings, its supply (whole) and what its units cost, minerals and gas; what
  // was mined over the last minute of game time; the units lost and killed and what they cost;
  // and the army's unit types with their counts, the most first
  int nArmySupply, nArmyValue;
  int nIncomeMinerals, nIncomeGas;
  int nLost, nLostValue, nKilled, nKilledValue;
  int nUnitTypes;
  unsigned short awUnitType[PORT_HUD_UNIT_TYPES], awUnitCount[PORT_HUD_UNIT_TYPES];
};

// The values, at the moment they are read. With no game, bInGame is false and the rest 0.
struct PortHudState {
  bool bInGame;
  bool bReplay;
  bool bMultiplayer;            // game.multiplayer: over the network with two or more players in it
  bool bObserver;               // game.observer: a game watched as it is played, a room's match or the lab's live
  unsigned int nFrame;          // game frames since the start
  int nSeconds;                 // the game clock (1.17's elapsed time)
  int nPlayer;                  // whose console: the local player, or the selected unit's owner in a replay
  int nRace;                    // 0 zerg, 1 terran, 2 protoss
  unsigned int nColour;         // the player's minimap colour, 0xRRGGBB
  int nMinerals, nGas;
  int nSupplyUsed, nSupplyMax;  // the player's race's, as the console shows it (halved)
  int anSupplyUsed[3], anSupplyMax[3];
  int nApm;                     // the player's actions over the last minute of game time, as the overlay counts them; 0 for the first ten seconds
  int nSelected;                // units selected
  int anSelectedTypes[12];
  int nPages, nPage;            // selection.pages, selection.page: pages of 12, one in 1.17
  int nSubgroups, nSubgroup;    // the unit types selected, and which the card is for
  // the first selected unit, as the console's status panel shows it
  bool bUnit;
  int nUnitType;
  int nUnitOrder;               // its order (1.17's orders.dat number)
  char szUnitName[64];
  int nHp, nHpMax, nShields, nShieldsMax, nEnergy, nEnergyMax, nKills;
  bool bShields, bEnergy, bKills;  // whether the panel shows each
  int nIdleWorkers;
  int nArmy;                    // army.count: the player's units that fight, not workers or Overlords
  int anGroups[10];             // live units in the control groups, index 1 the key 1, index 0 the key 0
  int nGroupLast;               // group.last: the group recalled last, -1 for none
  bool bTerrain;                // minimap.terrain: the minimap shows the terrain, true until its button turns it off
  bool abButton[PORT_HUD_BTN_KINDS];  // each button shown, by 1.17's rules for the game
  bool abButtonOff[PORT_HUD_BTN_KINDS];  // and greyed (Slower and Faster)
  bool abButtonLit[PORT_HUD_BTN_KINDS];  // and on (Replay TV, Follow)
  PortHudButton aCard[9];       // the command card, left to right and top to bottom
  // a replay: its state and speed as 1.17's status panel shows them, where it is,
  // and Hyperion's camera
  bool bPaused;                 // replay.paused
  int nSpeedStep;               // replay.step: 0 Slowest to 6 Fastest, then 7 to 10 Fastest x2 to x16
  char szSpeed[32];             // replay.speed: "Fastest", or past it "Fastest x2" with a times sign
  double fProgress;             // replay.progress: the share played, 0 to 1; 0 while live
  int nReplaySeconds;           // replay.elapsed: the time played, as 1.17's panel counts it
  int nReplayLength;            // replay.length: the replay's whole time; while live none, "" to a design
  bool bLive;                   // replay.live: a game watched as it is played, which has no length
  bool bSeeking;                // replay.seeking: the viewer drags the replay's position
  bool bTv, bFollow;            // replay.tv, replay.follow
  int nPlayers;                 // players.count
  int nTeams;                   // teams.count: the lobby's teams, a player without one alone
  PortHudPlayer aPlayers[PORT_HUD_PLAYERS];  // by slot
  // the players' stats: the side view and the popup each shown, and the popup's page
  bool bStatsSide, bStatsPopup;  // stats.side, stats.popup
  int nStatsPage;                // 0 Overview, 1 Income, 2 Army, 3 Units lost
};

enum PortHudEventKind {
  PORT_HUD_GAME_START,
  PORT_HUD_GAME_VICTORY,
  PORT_HUD_GAME_DEFEAT,
  PORT_HUD_SELECT_CHANGE,
  PORT_HUD_ALERT_ATTACK,
  PORT_HUD_ALERT_MINERALS,
  PORT_HUD_ALERT_GAS,
  PORT_HUD_ALERT_SUPPLY,
  PORT_HUD_RESEARCH_DONE,
  PORT_HUD_UNIT_DONE,
  PORT_HUD_GROUP_RECALL,
  PORT_HUD_GROUP_SET,
  PORT_HUD_CONSOLE_IN,
  PORT_HUD_CONSOLE_OUT,
  PORT_HUD_SELECTION_PAGE,
  PORT_HUD_SELECTION_SUBGROUP,
  PORT_HUD_REPLAY_PAUSE,   // a replay paused, played on, its speed changed, a player's
  PORT_HUD_REPLAY_RESUME,  // vision turned on or off
  PORT_HUD_REPLAY_SPEED,
  PORT_HUD_REPLAY_VISION,
  PORT_HUD_STATS_SHOW,     // the players' stats shown or hidden, one view
  PORT_HUD_STATS_HIDE,
  PORT_HUD_REPLAY_SEEK,    // the replay moved by the viewer, at a drag's end or by a key
  PORT_HUD_EVENT_KINDS
};

struct PortHudEvent {
  unsigned int nSerial;  // from 1, one more for each event since Hyperion started
  int nKind;             // PortHudEventKind
  unsigned int nFrame;   // the game frame it came on
  int nUnitType;         // the unit it is about, or -1
  int x, y;              // where, in map pixels (the attacked unit, the unit made), or -1
  bool bBuilding;        // alert.attack and unit.done: the unit is a building
  int nGroup;            // group.recall and group.set: the group, 0 to 9; else -1
  int nPlayer;           // replay.vision: the player turned, -1 for several
  int nStep;             // replay.speed: the step it went to; else -1
  int nView;             // stats.show and stats.hide: 0 the side view, 1 the popup; else -1
};

// The values now (the caller's copy).
void PortHudGet(PortHudState *pState);
// The replay's state against its last look, its events given on a change: once a game frame, and
// once a drawn frame, since a paused replay runs no frames (port_hud.cpp).
void PortHudReplayWatch(void);
// The serial of the last event.
unsigned int PortHudSerial(void);
// stats.show or stats.hide for a view, 0 the side view, 1 the popup (port_hudstats.cpp).
void PortHudStatsEvent(bool bShow, int nView);
// The replay position (port_hudinput.cpp): a drag of it on or off (replay.seeking), and
// replay.seek as the replay lands where the viewer moved it.
void PortHudSeekDrag(bool bOn);
void PortHudSeekEvent(void);
// The replay was put back to an earlier frame (port_replayseek.cpp): what was counted past it goes.
void PortHudRewound(void);
// The events after nSerial, oldest first, up to nMax; the last 256 are kept.
int PortHudEventsSince(unsigned int nSerial, PortHudEvent *pEvents, int nMax);
// The format's name for an event kind ("alert.attack").
const char *PortHudEventName(int nKind);
// The format's name for a button ("menu"), and the button a name names, or -1.
const char *PortHudButtonName(int nKind);
int PortHudButtonByName(const char *pszName);

// The presses. Each returns false, with why (a short English phrase, for the live channel's
// answers and the log), when there is no game or nothing to press. The command card's button
// at nPlace (0 to 8), as its hotkey presses it; one of the buttons beside the card, as a click
// on 1.17's own; a control group recalled as its key recalls it (nGroup 0 to 9).
bool PortHudPress(int nPlace, const char **ppszWhy);
bool PortHudPressButton(int nKind, const char **ppszWhy);
// A player's eye on the players' board: what the player sees shown or hidden, as View Players'
// box for them and its OK do (a replay; nSlot 0 to 7).
bool PortHudPressEye(int nSlot, const char **ppszWhy);
// The card button's tooltip at place nPlace (0 to 8) as a pointer resting on it shows it, -1 none
// (statcmd.cpp; the live channel's hud_tip). False when no button is there.
bool StatCmdTipAt(int nPlace);
// nHow: recalled as its key recalls it, or made of the selection (Ctrl with the key), or the
// selection added to it (Shift with the key).
enum { PORT_HUD_USE_RECALL, PORT_HUD_USE_SET, PORT_HUD_USE_ADD };
bool PortHudPressGroup(int nGroup, const char **ppszWhy, int nHow = PORT_HUD_USE_RECALL);
// The HUD's selection grid (port_gpu_hud.cpp), which shows nUnits in pages of nPer: the page
// shown, the one holding the console's subgroup's first unit since the selection or the
// subgroup last changed; PortHudGridTurn shows another (selection.page). Presentation only.
int PortHudGridPage(int nPer, int nUnits);
void PortHudGridTurn(int nPage);
