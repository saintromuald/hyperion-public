// port_clock.h -- the game's clock: Settings' frame timing, the speeds' frame times, and Debug
// Tools' clock (port_clock.cpp).
#ifndef PORT_CLOCK_H
#define PORT_CLOCK_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types
#include <string>

// The game time a game frame counts, in ms, on the clocks Hyperion shows (the game timer, Debug
// Tools'), as 1.17's replay panel counts its Time Elapsed: a replay's frames at the speed it was
// recorded at, any other game's at the speed it is played at. A game whose speed is changed
// counts all its frames at the new one.
unsigned PortClockFrameMs(void);
// Debug Tools' game clock (port_clock.cpp): each game frame, frame shown and scroll step timed
// while it is on, for the live channel's "clock".
// Settings' "Game frame timing" (port_clock.cpp): 1 even, 0 as 1.17; PortGameTicks is the
// clock in ms the game loop runs its frames by.
int PortFrameTiming(void);
void PortFrameTimingSet(int nTiming);
ULONG PortGameTicks(void);
// The game's frames due n times as often as its speed has them (the trainer's x2 to x8):
// the clock above runs n times as fast from now. Pacing only, so nothing the game does
// changes; 1 is the game's own pace, and the clock is then the plain one.
void PortGameSpeedTimesSet(int n);
int PortGameSpeedTimes(void);
// The game loop's rest after a pass that ran no turn and drew nothing: until input comes,
// the game's next frame (lGameDueMs on the clock above) or the next frame to draw, at most a
// millisecond. Without it the loop went round again at once and held a core at full load.
// It does not rest with DLLs injected, whose clock is the replay harness's, nor in a browser.
void PortClockRest(long lGameDueMs);
// Settings' "Rest between frames" ("loopRest"), on by default; HYPERION_LOOP_REST=0 or 1 goes
// before it. Off, the loop goes round without resting, as 1.17's did.
bool PortClockRestOn(void);
void PortClockRestSet(bool bOn);
// Settings' "Run in the background" ("runInBackground"), off by default: on, a single-player
// game and 1.17's screens go on while another window has the focus, where 1.17 waits for it
// (PortControlKeepsRunning, which the gates ask).
bool PortClockBackgroundOn(void);
void PortClockBackgroundSet(bool bOn);
void PortClockGameFrame(void);
void PortClockShown(void);
void PortClockScroll(void);
void PortClockStart(void);
void PortClockStop(void);
bool PortClockOn(void);
struct PortClockNow {
  bool bOn;
  double fSeconds;      // since it started
  double fNominalMs;    // a game frame's due time at this speed
  double fPacePerS;     // game frames a second since the last gap
  double fDriftMs;      // the game behind wall time since then (ahead below 0)
  unsigned nGaps;       // pauses, holds and changes of speed
  unsigned nFrames;     // game frames timed
  unsigned nRecent;     // the intervals the rest are over
  double fMeanMs, fSdMs, fLeastMs, fMostMs, fWithinPct;
  int nShownMost, nShownLeast = -1, nShownMax;  // frames shown in a game frame: most often, fewest, most
  int nGameGot, nShownGot;
};
void PortClockRead(PortClockNow *p, int nRecent, float *pfGame, int nGameMax, float *pfShown, int nShownMax);

// Debug Tools' clock's readings so far, as JSON members, for the live channel.
std::string PortClockJson(void);

#endif
