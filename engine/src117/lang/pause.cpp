// pause.cpp -- the game's pause state: the flag itself (gbPauseGame), the
// per-player pause budget that multiplayer spends, and the pause and unpause
// transitions. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>        // memset (pause_init)

#include "../h/pause.h"
#include "../h/replay.h"     // sub_480E00 -- forces/releases the replay pause
#include "../h/usel.h"       // usel_cancel_drag_evt
#include "../h/net_cmds.h"   // PlayerData, complete, for player_can_pause
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled. gbPauseGame, sgdwLastPauseTime_g,
// dword_675178, gbMultiPlayerMode, gPlayback and byte_5349F8 come from
// gen/globals117.h; byte_5349F8 is the eight players' pauses left.

// 0x67CC7C sgbReplayPaused_g, an int, spelled as replay.cpp spells it.
#if !defined(OC_DLL_MODE) && !defined(OC_PAYLOAD)
extern int sgbReplayPaused_g;
#endif
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgbReplayPaused_g (*reinterpret_cast<int *>(OC_VA(0x67CC7Cu)))
#endif

// GetTickCount, imported from KERNEL32 at IAT slot 0x4EB2FC, declared here so
// the call goes through the import pointer as 1.17's does.
extern "C" __declspec(dllimport) ULONG __stdcall GetTickCount(void);

// The bodies, in oracle VA order.

// Single-player may always pause. In multiplayer the caller must be past the
// get_lobby_state gate, the player's slot record must read type 2, and the
// player must have pauses left.
BOOL player_can_pause(int nPlayer) {
  if (!gbMultiPlayerMode)
    return 1;

  if (get_lobby_state() < 9)
    return 0;

  if (gPlayerData[nPlayer].nType == 2 && byte_5349F8[nPlayer] > 0)
    return 1;

  return 0;
}

// The pause flag, a DWORD, returned whole.
BOOL IsGamePaused() {
  return gbPauseGame;
}

// Whether the game is paused outside multiplayer. 1.17 returns the 1 and the 0
// from two blocks, hence the if and the two returns.
BOOL IsPausedSingleplayer() {
  if (gbPauseGame && !gbMultiPlayerMode)
    return 1;
  return 0;
}

// Gives each of the eight players three pauses. The 8 is a literal: in the DLL
// form byte_5349F8 is a pointer, and sizeof would give 4.
void pause_init() {
  memset(byte_5349F8, 3, 8);
}

// Idempotent by its own first test, then three things in order: raises the
// flag, drops any unit-selection drag in progress, and stamps the tick the
// pause began. In a replay it then saves the replay's own pause state in
// dword_675178 and forces the replay paused; unpause_game releases it only when
// that saved state was zero.
void pause_game(void) {
  if (gbPauseGame)
    return;
  gbPauseGame = 1;
  usel_cancel_drag_evt();
  sgdwLastPauseTime_g = GetTickCount();
  if (!gPlayback)
    return;
  dword_675178 = sgbReplayPaused_g;
  sub_480E00(1);
}

// The player's pauses left.
BYTE RemainingPauses(int nPlayer) {
  return byte_5349F8[nPlayer];
}

// Clears the flag. bFlag decides only whether the paused span, in seconds by an
// unsigned divide, is taken out of the banked game time; the unpause always
// happens. In a replay it releases the replay pause unless pause_game found the
// replay already paused.
void unpause_game(int bFlag) {
  if (!gbPauseGame)
    return;
  if (bFlag)
    sub_47A620((GetTickCount() - sgdwLastPauseTime_g) / 1000);
  gbPauseGame = 0;
  if (!gPlayback)
    return;
  if (dword_675178 != 0)
    return;
  sub_480E00(0);
}

// Spends one of the player's pauses, in multiplayer only and never below zero.
// The byte is read once into a local, as in 1.17.
void SubtractPauseCount(int nPlayer) {
  if (!gbMultiPlayerMode)
    return;
  BYTE bLeft = byte_5349F8[nPlayer];
  if (bLeft == 0)
    return;
  byte_5349F8[nPlayer] = bLeft - 1;
}
