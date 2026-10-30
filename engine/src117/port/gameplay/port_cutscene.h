// port_cutscene.h -- cutscenes scripted over the live channel: a list of steps a
// tool, an editor or an AI sends (cutscene_play), played on the game as it runs. The camera
// glides, zooms and follows units; the picture fades, zooms and takes cinema bars
// (port_motion.cpp); units are made, ordered and removed as the trigger actions do it
// (port_trigrun.cpp); words come as captions, the game's messages or a transmission with its
// portrait. The steps and their fields: docs/live-channel-for-editors.md. A cutscene that acts
// on units changes the game and plays only in single-player games; one that only shows plays
// in any game and in a replay.
#ifndef PORT_CUTSCENE_H
#define PORT_CUTSCENE_H

#include <string>

struct PortJson;

// Starts a cutscene from its JSON ({steps: [...], hold?, skip?}), ending one that plays; false
// with *pError saying which step is wrong and why.
bool PortCutscenePlay(const PortJson &scene, std::string *pError);
// Ends the one playing: the picture back as the game draws it, in a moment.
void PortCutsceneStop(void);
// What plays, as JSON members: whether one does, its step, how far into it.
std::string PortCutsceneStatusJson(void);
// One pass of the game's loop (gameloop.cpp): the steps due are run.
void PortCutsceneTick(void);
// Whether one plays (until its end has eased out).
bool PortCutsceneRunning(void);
// The camera's aim while one moves it: the view's middle in map pixels (port_view.cpp).
bool PortCutsceneAim(double *pfX, double *pfY);
// The captions, and the player's keys and buttons held back while one plays (Escape ends one
// that may be skipped), are a layer over the game (port_overlay.h).

#endif
