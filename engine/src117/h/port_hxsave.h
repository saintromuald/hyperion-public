// port_hxsave.h -- a save of a Hyperion game (port/replays/port_hxsave.cpp).
#ifndef PORT_HXSAVE_H
#define PORT_HXSAVE_H

// Whether the game is past 1.17's limits, so its save holds Hyperion's sections.
bool HxSaveNeeded();
// After GAME: the rule record and the set-up. Read before the game starts; a save without
// it is 1.17's. 0 on an error, or when the save's rules, data or revision are not this
// build's.
int HxSaveSetUp(void *pFile);
int HxLoadSetUp(void *pFile);
// A loaded game's colours in full RGB, from HXRC, as it starts (stage G).
void HxSaveColorsApply();
// Whether a Hyperion save is being loaded: what 1.17's load resets, such a load keeps as
// saved (the AI's demands).
bool HxSaveLoading();
// In a load, after the sprites and images are read and before the pylons' power fields are
// put up again (RestorePsiLinks): the sprites' and images' free lists as the save had them
// then, so the fields take the slots they took in the game saved. 0 on an error.
int HxSaveBeforePsiLinks();
// After VIEW: the extra player entries, the vision past 7, the random state and the
// state's hash as saved. 0 on an error.
int HxSaveState(void *pFile);
int HxLoadState(void *pFile);
// In a replay, where its game was saved: the save again, to a scratch file, for what saving
// does to the game (ReplayHxFrame). 0 when it could not be written.
int HxSaveReplay();
// Where a save goes instead of the save folder, while one is being made again; NULL else.
const char *HxSaveTarget();
// Once a game is set up: for a loaded Hyperion save the random state as saved, and the
// state compared with the hash saved (hyperion_save.log).
void HxSaveLoaded();
// Whether the game was loaded from a Hyperion save, so it goes on from the frame saved
// without the two steps a game's start runs (gameloop_advance2).
bool HxSaveGoesOn();
// As the game starts (run_game): the frames to the next fog update as saved, where a
// game's start would make it the first frame.
void HxSaveRunGame();

#endif
