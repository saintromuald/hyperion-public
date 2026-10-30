// port_motion.h -- the picture's motion: how the game's picture is shown over its frame,
// shared by a skirmish's start (port_intro.cpp), the cutscenes (port_cutscene.cpp) and the
// map loads (port_control.cpp). Presentation only.
#ifndef PORT_MOTION_H
#define PORT_MOTION_H

// What moves: the map's scale about the middle (1 as the game draws it, 0.86 the skirmish's
// start), the console's drop (a share of its height) and how much of it shows, black over the
// game's picture, and the cinema bars (a share of the picture's height, top and bottom each).
enum PortMotionChannel {
  MOTION_MAP_SCALE,
  MOTION_HUD_SHIFT,
  MOTION_HUD_ALPHA,
  MOTION_BLACK,
  MOTION_BARS,
  MOTION_CHANNELS
};

// How a move eases: even, the front end's ease (UiEase), fast then slow (a cubic), slow at
// both ends.
enum PortMotionEase { MOTION_EVEN, MOTION_EASE, MOTION_OUT, MOTION_IN_OUT };

struct PortMotionNow {
  float fMapScale, fHudShift, fHudAlpha, fBlack, fBars;
};

// The values as they are now.
void PortMotionRead(PortMotionNow *p);
float PortMotionValue(int nChannel);
// A channel moved from where it is to fTo over fSeconds, starting fDelay seconds from now;
// PortMotionSet puts it there at once.
void PortMotionTo(int nChannel, float fTo, double fSeconds, double fDelay = 0.0, int nEase = MOTION_EASE);
void PortMotionSet(int nChannel, float fValue);
// Every channel at rest at once: the picture as the game draws it.
void PortMotionRest(void);
// A game's picture coming in as a skirmish's does: PortMotionArriveHold sets it black, the map
// at 86 % and the console down and hidden; PortMotionArrive then brings the map out of black
// (1.2 s) as it settles to 100 % (1.6 s), the console rising from 1 s (0.6 s). Seconds from
// the start of the arrival to when it is all at rest: PORT_MOTION_ARRIVE_SECONDS.
#define PORT_MOTION_ARRIVE_SECONDS 1.6
void PortMotionArriveHold(void);
void PortMotionArrive(void);
// The picture going out, the arrival backwards: the console dropping and fading, the map
// shrinking to 86 % as black comes over it, in fSeconds.
void PortMotionDepart(double fSeconds);
// At a game's start (port_intro.cpp): at rest, or, when PortMotionArriveNext asked for it (a
// map load), held black to arrive at the loop's first pass.
void PortMotionGameStarts(void);
void PortMotionArriveNext(void);
void PortMotionArriveCancel(void);
// The whole window black between games (a map switch): from when the switch leaves a game
// until the next one starts, while 1.17 ends the old game and loads the new, the frames shown
// are black (port_overlay.cpp), so the last of the old map does not come back between.
void PortMotionCover(void);
bool PortMotionCovered(void);
// Whether a channel is still moving, or any is.
bool PortMotionMoving(int nChannel = -1);
// The motion's clock, in seconds: moved on once a pass of the game's loop (PortMotionTick), a
// tenth of a second at most a pass, so a stall pauses it and it never jumps.
// HYPERION_MOTION_SLOW=n (or HYPERION_INTRO_SLOW, its first name) plays it n times slower.
double PortMotionClock(void);
void PortMotionTick(void);
// The black and the bars over the game's picture are a layer over the game (port_overlay.h).
// Whether a window message is the player acting (a key, a button), which a skirmish's start
// and a cutscene hold back: keys and buttons, not the mouse's moves; the system keys too (F10
// comes as one) but not with Alt held (Alt+F4, Alt+Enter).
bool PortMotionIsAction(unsigned int uMsg, long long lParam);

#endif
