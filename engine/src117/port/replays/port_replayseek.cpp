// port_replayseek.cpp -- a replay taken to any frame of it (port_replayseek.h).
//
// While a replay plays under a HUD that can move it (PortHudCanSeek), the game's state is kept
// every kSeekEvery frames on a snapshot line of its own (port_snap.cpp, the trainer's store, which
// keeps only the pages that changed), and once at the first frame the HUD could move it. A seek
// puts back the latest kept at or before its frame, or plays on from where the replay is when that
// is nearer, and the loop then runs the replay's own commands frame after frame at once, drawing a
// picture now and then, to the frame. A replay plays the same however it was reached, so the
// frames played again are the frames the replay plays, and the snapshots past the frame stay good
// (the recorded checks follow the game back, ReplayHxRewound). What is the viewer's, not the
// game's, is kept across a put back: the speed, the pause, the players' vision shown and Show
// All. A seek plays past the pause (the game loop asks PortReplaySeeking), and the sound is off
// while it plays.
#include <windows.h>
#include "port_replayseek.h"
#include "port_snap.h"
#include "../hud/port_hud.h"            // PortHudCanSeek
#include "../hud/port_hudstate.h"       // PortHudRewound
#include "../lan/port_lanfeed.h"        // PortLanWatchLive
#include "../lab/port_labwatch.h"       // PortLabWatchLive
#include "../gameplay/port_watchgame.h" // PortWatchLive
#include "../gameplay/port_trainer.h"   // PortTrainerSession
#include "../platform/port_util.h"      // PortNowMs
#include "../../h/replay.h"             // replay_get_total_frames, replay_set_speed, replay_dlg_get_mult
#include "../../h/status.h"             // status_get_new_selection
#include "../../h/cursor.h"             // cursor_forget_target
#include "../../h/net_cmds.h"           // status_set_update
#include "../../h/snd.h"                // stop_digital
#include "../../gen/globals117.h"       // gPlayback, gwGameMode, gOptions, gnSfxVolume, gdwReplayVisionMask, sgbShowAll_g
#include "../platform/port_globals117.h"

#ifdef OC_DLL_MODE
#define sgbReplayPaused_g (*reinterpret_cast<int *>(OC_VA(0x67CC7Cu)))
#else
extern int sgbReplayPaused_g;  // the replay is paused (replay.cpp)
#endif

namespace {

// a snapshot line of its own, apart from the trainer's lines and the self-test's (0)
const int kSeekLine = 0x5EE0;
// frames between snapshots, doubled while the store holds more than kStoreMost
const unsigned kSeekEvery = 240;
const size_t kStoreMost = static_cast<size_t>(768) << 20;
// a seek's pictures, so the replay is seen moving and a drag's handle follows the pointer
const uint64_t kPictureMs = 50;

unsigned g_nEvery = kSeekEvery;
unsigned g_nTaken = 0;       // snapshots taken, to measure the store every sixteenth
unsigned g_nFirst = ~0u;     // the frame of the line's first snapshot, ~0u for none
bool g_bSeeking = false;
unsigned g_nTo = 0;          // the frame a seek goes to
int g_nSfx = 0;              // the sound's volume while a seek has it off, put back at its end
uint64_t g_qPictureAt = 0;

void SoundBack(void) {
  if (g_nSfx != 0) {
    stop_digital();
    gnSfxVolume = g_nSfx;
  }
  g_nSfx = 0;
}

void SeekEnd(void) {
  g_bSeeking = false;
  SoundBack();
}

}  // namespace

bool PortReplaySeekable(void) {
  return gwGameMode == 3 && gPlayback != 0 && !PortLanWatchLive() && !PortLabWatchLive() && !PortWatchLive() &&
         !PortTrainerSession() && replay_get_total_frames() > 0;
}

unsigned PortReplaySeekLength(void) {
  return gPlayback != 0 ? static_cast<unsigned>(replay_get_total_frames()) : 0u;
}

void PortReplaySeekFrame(void) {
  if (!PortReplaySeekable()) {
    if (g_bSeeking)
      SeekEnd();
    return;
  }
  const unsigned nFrame = oc_dwReplayFrame;
  if ((g_nFirst == ~0u || nFrame % g_nEvery == 0) && !PortSnapHas(kSeekLine, nFrame) && PortHudCanSeek()) {
    PortSnapTake(kSeekLine, nFrame);
    if (g_nFirst == ~0u || nFrame < g_nFirst)
      g_nFirst = nFrame;
    // the store measured now and then: past its most, every other snapshot goes
    if (++g_nTaken % 16 == 0 && PortSnapStoreBytes() > kStoreMost) {
      g_nEvery *= 2;
      PortSnapThin(kSeekLine, g_nEvery);
    }
  }
  if (g_bSeeking && nFrame >= g_nTo)
    SeekEnd();
}

void PortReplaySeekAfterGame(void) {
  SeekEnd();
  PortSnapDropLine(kSeekLine);
  g_nEvery = kSeekEvery;
  g_nTaken = 0;
  g_nFirst = ~0u;
}

bool PortReplaySeekTo(unsigned nTo) {
  if (!PortReplaySeekable() || !PortHudCanSeek())
    return false;
  const unsigned nTotal = static_cast<unsigned>(replay_get_total_frames());
  if (nTo >= nTotal)
    nTo = nTotal - 1;
  // no further back than the first snapshot
  if (g_nFirst != ~0u && nTo < g_nFirst)
    nTo = g_nFirst;
  const unsigned nNow = oc_dwReplayFrame;
  // back, or on past a snapshot nearer the frame than the replay is: the snapshot put back
  const unsigned nKept = PortSnapLatest(kSeekLine, nTo);
  if (nTo < nNow || (nKept != ~0u && nKept > nNow)) {
    if (nKept == ~0u)
      return false;
    const DWORD dwSpeed = gOptions, dwMult = static_cast<DWORD>(replay_dlg_get_mult());
    const DWORD dwVision = gdwReplayVisionMask, dwShowAll = sgbShowAll_g;
    const bool bPaused = sgbReplayPaused_g != 0;
    if (PortSnapRestore(kSeekLine, nTo, false) == ~0u)
      return false;
    replay_set_speed(dwSpeed, dwMult, static_cast<BYTE>(bPaused ? 1 : 0));
    gdwReplayVisionMask = dwVision;
    sgbShowAll_g = dwShowAll;
    // the selection came back with the game, but the console's copy and the unit under the cursor
    // are the program's and may name units gone now; the HUD's own counts drop what came later
    status_get_new_selection();
    status_set_update();
    cursor_forget_target();
    PortHudRewound();
  }
  g_nTo = nTo;
  if (oc_dwReplayFrame >= nTo) {
    SeekEnd();
    return true;
  }
  if (g_nSfx == 0 && gnSfxVolume != 0) {
    g_nSfx = gnSfxVolume;
    gnSfxVolume = 0;
    stop_digital();
  }
  if (!g_bSeeking)
    g_qPictureAt = PortNowMs() + kPictureMs;
  g_bSeeking = true;
  return true;
}

bool PortReplaySeeking(void) {
  if (g_bSeeking && !(gwGameMode == 3 && gPlayback != 0))
    SeekEnd();
  return g_bSeeking;
}

bool PortReplaySeekPictureDue(void) {
  const uint64_t qNow = PortNowMs();
  if (qNow < g_qPictureAt)
    return false;
  g_qPictureAt = qNow + kPictureMs;
  return true;
}

unsigned PortReplaySeekTarget(void) {
  return g_bSeeking ? g_nTo : oc_dwReplayFrame;
}
