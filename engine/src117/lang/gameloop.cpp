// gameloop.cpp -- the frame: the network turn, the fixed order the simulation
// and the UI update in, and the outer loop that drives them until the game
// ends. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../h/gameloop.h"
#include "../h/status.h"
#include "../h/placebox.h"
#include "../h/net_cmds.h"
#include "../h/gds_palette.h"
#include "../h/mapmsg.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/net_mgr.h"
#include "../h/minimap.h"
#include "../h/cursor.h"
void accel_apply_profile();  // Hyperion: event_Win.cpp (the hotkeys milestone K2)
#include "../h/img.h"
#include "../h/CBullet.h"
#include "../h/CFlingy.h"
#include "../h/CThingy.h"
#include "../h/mask.h"
#include "../h/SAI_Main.h"
#include "../h/net_rand.h"
#include "../h/gamemap.h"
#include "../h/glues.h"
#include "../h/gluRdy.h"
#include "../h/opt_dlgs.h"
#include "../h/saveload.h"
#include "../h/snd.h"
#include "../h/TrigAct.h"
#include "../h/dlg_dlg.h"
#include "../h/net_error.h"
#include "../h/save_dlg.h"
#include "../h/Storm.h"
#include "../h/replay.h"
#include "../h/palette.h"       // init_game_font_palette
#include "../gen/globals117.h"
#include "../port/tools/port_bench.h"  // Hyperion
#include "../port/gameplay/port_watchgame.h"  // Hyperion: a skirmish watched
#include "../port/loop/port_clock.h"  // Hyperion: PortGameTicks, PortClockGameFrame, PortClockRest
#include "../port/render/port_console.h"  // Hyperion: PortConsoleUpdate
#include "../port/front/port_gamemenu.h"   // Hyperion: PortGameMenuStep
#include "../port/tools/port_control.h"  // Hyperion: PortControlKeepsRunning, PortControlFast, PortControlFrameRan
#include "../port/front/port_front.h"  // Hyperion: PortFrontSkirmishGame, PortFrontSkirmishSpeed
#include "../port/gameplay/port_hotkeys.h"  // Hyperion: PortHotkeysLoad
#include "../port/front/port_intro.h"  // Hyperion: PortIntroHolds, PortIntroGameStarts, PortIntroArmed
#include "../port/gameplay/port_launch.h"  // Hyperion: PortLaunchGameStarts
#include "../port/platform/port_paths.h"  // Hyperion: PortCommandOptions
#include "../port/loop/port_perf.h"  // Hyperion
#include "../port/render/port_present.h"  // Hyperion
#include "../port/replays/port_record.h"  // Hyperion: PortRecordFrame
#include "../port/lab/port_labworker.h"  // Hyperion: PortLabFrame
#include "../port/replays/port_snapprobe.h"  // Hyperion: PortSnapProbeFrame
#include "../port/render/port_view.h"  // Hyperion: PortViewApply, g_portView
#include "../port/replays/port_snap.h"  // Hyperion: PortSnapFrame
#include "../port/replays/port_replayseek.h"  // Hyperion: a replay moved
#include "../port/gameplay/port_trainer.h"  // Hyperion: the trainer
#include "../port/lan/port_lanfeed.h"  // Hyperion: a watched LAN match
#include "../port/lab/port_labwatch.h"  // Hyperion: a farm game watched
#include "../port/gameplay/port_cutscene.h"  // Hyperion: PortCutsceneTick
#include "../port/render/port_motion.h"    // Hyperion: PortMotionTick
#include "../h/port_hxreplay.h"  // Hyperion: ReplayHxFrame
#include "../h/port_hxsave.h"    // Hyperion: HxSaveGoesOn
#include "../port/gameplay/port_ai.h"     // Hyperion: AiFrame
#include "../port/gameplay/port_mutators.h"  // Hyperion: MutatorsVisionRefresh
#include "../port/gameplay/port_score.h"  // Hyperion: PortScoreFrame
#include "../port/gameplay/port_richbases.h"  // Hyperion: RichBasesFrame
#include "../port/hud/port_hudstate.h"  // Hyperion: the HUD's data layer

// GetTickCount, KERNEL32's, at IAT slot 0x4EB2FC; declared here, since this tree
// keeps <windows.h> off the include path.
extern "C" __declspec(dllimport) DWORD __stdcall GetTickCount(void);   // 0x4EB2FC

// Globals this TU reaches, dual-spelled, one #ifdef per name. The four
// gameloop.cpp names are Blizzard's.

#ifdef OC_DLL_MODE
  #ifndef byte_6663EC
  #define byte_6663EC (*reinterpret_cast<BYTE *>(OC_VA(0x6663ECu)))
  #endif
  #ifndef sgbGameRunning_g
  #define sgbGameRunning_g (*reinterpret_cast<BYTE *>(OC_VA(0x6663EDu)))
  #endif
  #ifndef sgdwLastCycleUpdateTime_g
  #define sgdwLastCycleUpdateTime_g (*reinterpret_cast<DWORD *>(OC_VA(0x666428u)))
  #endif
  #ifndef gbColorCycle_g
  #define gbColorCycle_g (*reinterpret_cast<DWORD *>(OC_VA(0x50E138u)))
  #endif
  #ifndef sgbWindowActive_g
  #define sgbWindowActive_g (*reinterpret_cast<DWORD *>(OC_VA(0x66FD24u)))
  #endif
  #ifndef sgdwLastScrollUpdateTime_g
  #define sgdwLastScrollUpdateTime_g (*reinterpret_cast<DWORD *>(OC_VA(0x6663F4u)))
  #endif
  #ifndef sgwReturnMode_g
  #define sgwReturnMode_g (*reinterpret_cast<WORD *>(OC_VA(0x666420u)))
  #endif
  #ifndef dword_52651C
  #define dword_52651C (*reinterpret_cast<DWORD *>(OC_VA(0x52651Cu)))
  #endif
  #ifndef gPlayback
  #define gPlayback (*reinterpret_cast<DWORD *>(OC_VA(0x67CC90u)))
  #endif
  #ifndef sgTicksPerFrame_g
  #define sgTicksPerFrame_g (reinterpret_cast<DWORD *>(OC_VA(0x503360u)))
  #endif
  #ifndef sgbNetworkTurnTimer
  #define sgbNetworkTurnTimer (*reinterpret_cast<DWORD *>(OC_VA(0x6663FCu)))
  #endif
  #ifndef byte_666400
  #define byte_666400 (*reinterpret_cast<BYTE *>(OC_VA(0x666400u)))
  #endif
  #ifndef dword_666404
  #define dword_666404 (reinterpret_cast<DWORD *>(OC_VA(0x666404u)))
  #endif
  #ifndef dword_66642C
  #define dword_66642C (*reinterpret_cast<DWORD *>(OC_VA(0x66642Cu)))
  #endif
  #ifndef sgdwLastGameUpdateTime_g
  #define sgdwLastGameUpdateTime_g (*reinterpret_cast<DWORD *>(OC_VA(0x6663F8u)))
  #endif
  #ifndef gbMultiPlayerMode
  #define gbMultiPlayerMode (*reinterpret_cast<BYTE *>(OC_VA(0x672664u)))
  #endif
  #ifndef sgdwUpdatesPerFrame_g
  #define sgdwUpdatesPerFrame_g (*reinterpret_cast<DWORD *>(OC_VA(0x50335Cu)))
  #endif
  #ifndef sgdwBreakReason_g
  #define sgdwBreakReason_g (*reinterpret_cast<DWORD *>(OC_VA(0x6663F0u)))
  #endif
  #ifndef dword_6663E8
  #define dword_6663E8 (*reinterpret_cast<DWORD *>(OC_VA(0x6663E8u)))
  #endif
  #ifndef sgdwNumUpdatesLastFrame_g
  #define sgdwNumUpdatesLastFrame_g (*reinterpret_cast<DWORD *>(OC_VA(0x666424u)))
  #endif
  #ifndef word_5264BE_g
  #define word_5264BE_g (*reinterpret_cast<WORD *>(OC_VA(0x5264BEu)))
  #endif
  #ifndef oc_wCampaignMission
  #define oc_wCampaignMission (*reinterpret_cast<WORD *>(OC_VA(0x526524u)))
  #endif
  #ifndef byte_6728F6
  #define byte_6728F6 (*reinterpret_cast<BYTE *>(OC_VA(0x6728F6u)))
  #endif
  #ifndef sgTicksPerGameTurn
  #define sgTicksPerGameTurn (reinterpret_cast<DWORD *>(OC_VA(0x50337Cu)))
  #endif
  #ifndef sgbMouseUpdated_g
  #define sgbMouseUpdated_g (*reinterpret_cast<DWORD *>(OC_VA(0x665E3Cu)))
  #endif
  #ifndef sgszGameLoopCpp
  #define sgszGameLoopCpp (reinterpret_cast<const char *>(OC_VA(0x4EC444u)))
  #endif
#else
extern BYTE  byte_6663EC;                // 0x6663EC
extern BYTE  sgbGameRunning_g;           // 0x6663ED
extern DWORD sgdwLastCycleUpdateTime_g;  // 0x666428
extern DWORD gbColorCycle_g;             // 0x50E138
extern DWORD sgbWindowActive_g;          // 0x66FD24
extern DWORD sgdwLastScrollUpdateTime_g; // 0x6663F4
extern WORD  sgwReturnMode_g;            // 0x666420
extern DWORD dword_52651C;               // 0x52651C
extern DWORD gPlayback;                  // 0x67CC90
extern DWORD sgTicksPerFrame_g[7];       // 0x503360
extern DWORD sgbNetworkTurnTimer;        // 0x6663FC
extern BYTE  byte_666400;                // 0x666400
extern DWORD dword_666404[7] OC_UNALIGNED;            // 0x666404
extern DWORD dword_66642C;               // 0x66642C
extern DWORD sgdwLastGameUpdateTime_g;   // 0x6663F8
extern BYTE  gbMultiPlayerMode;          // 0x672664
extern DWORD sgdwUpdatesPerFrame_g;      // 0x50335C
extern DWORD sgdwBreakReason_g;          // 0x6663F0
extern DWORD dword_6663E8;               // 0x6663E8
extern DWORD sgdwNumUpdatesLastFrame_g;  // 0x666424
extern WORD  word_5264BE_g;              // 0x5264BE, game.cpp's spelling
extern WORD  oc_wCampaignMission;        // 0x526524, credits.cpp's spelling
extern BYTE  byte_6728F6;                // 0x6728F6
extern DWORD sgTicksPerGameTurn[7] OC_UNALIGNED;      // 0x50337C
extern DWORD sgbMouseUpdated_g;          // 0x665E3C
extern const char sgszGameLoopCpp[1];    // 0x4EC444, "gameloop.cpp"
#endif
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
  #ifndef sgbReplayPaused_g
  #define sgbReplayPaused_g (*reinterpret_cast<DWORD *>(OC_VA(0x67CC7Cu)))
  #endif
#else
extern DWORD sgbReplayPaused_g;          // 0x67CC7C
#endif
#ifdef OC_DLL_MODE
  #ifndef gOptions
  #define gOptions (*reinterpret_cast<DWORD *>(OC_VA(0x6D3130u)))
  #endif
  #ifndef dword_6D3148
  #define dword_6D3148 (*reinterpret_cast<DWORD *>(OC_VA(0x6D3148u)))
  #endif
#else
extern DWORD gOptions;                   // 0x6D3130
extern DWORD dword_6D3148;               // 0x6D3148, gOptions + 0x18
#endif

// The caps block SNetGetProviderCaps fills; only the size prefix (0x24) and
// +0x1C are used here.
struct snet_caps_t {
  /*0x00*/ DWORD dwSize;
  /*0x04*/ DWORD _unmapped04[6];
  /*0x1C*/ DWORD dwTurnsPerSecond;
  /*0x20*/ DWORD _unmapped20;
};

static_assert(sizeof(snet_caps_t) == 0x24, "snet_caps_t size  sub_445470: sub esp, 24h and mov dword ptr [ebp-24h], 24h");
static_assert(offsetof(snet_caps_t, dwSize) == 0x00, "snet_caps_t::dwSize  sub_445470: mov dword ptr [ebp-24h], 24h");
static_assert(offsetof(snet_caps_t, dwTurnsPerSecond) == 0x1C, "snet_caps_t::dwTurnsPerSecond  sub_445470: mov eax, dword ptr [ebp-8h]");

// The body sub_445110 inlines and sub_445470 is, defined once and forced inline
// into both, since 1.17 has it twice.
static __forceinline void rebuild_turn_rate_table()
{
    snet_caps_t caps;
    caps.dwSize = sizeof(caps);
    if (!SNetGetProviderCaps(&caps))
        net_error_popup(0x51, sgszGameLoopCpp, 0x137, 1);

    DWORD dwReported = caps.dwTurnsPerSecond;
    DWORD dwTurnsPerSec = 20;
    if (dwReported < 20) {
        dwTurnsPerSec = dwReported;
        if (dwReported == 0)
            dwTurnsPerSec = 20;
    }

    memset(dword_666404, 0, 7 * sizeof(DWORD));
    int i = 7;
    do {
        i--;
        if (gbMultiPlayerMode) {
            dword_666404[i] = 1000 / (sgTicksPerFrame_g[i] * dwTurnsPerSec);
            if (dword_666404[i] != 0)
                continue;
        }
        dword_666404[i] = 1;
    } while (i != 0);
}

// The bodies, in oracle VA order.

// Advance one network turn, and report through the out-parameter whether the
// minimap was redrawn. Returns 0 only while the network is stalled; the stall
// is timed from the first blocked turn and the game clock is then advanced by
// either half a frame or a whole turn, depending on how long it lasted.
#pragma optimize("g", off)
int DoNetworkTurn(int *pbRedrew)
{
    *pbRedrew = 0;
    if (--sgbNetworkTurnTimer != 0)
        return 1;

    if (!ProgressNetwork()) {
        sgbNetworkTurnTimer = 1;
        if (!byte_666400) {
            byte_666400 = 1;
            dword_66642C = GetTickCount();
        }
        return 0;
    }

    if (byte_666400) {
        dword_66642C = GetTickCount() - dword_66642C;
        if (dword_66642C < 100)
            sgdwLastGameUpdateTime_g += sgTicksPerFrame_g[gOptions] >> 1;
        else
            sgdwLastGameUpdateTime_g = PortGameTicks()  // Hyperion: the game's clock, below
                + ((sgTicksPerFrame_g[gOptions] * dword_666404[gOptions]) >> 1);
    }
    byte_666400 = 0;
    sgbNetworkTurnTimer = dword_666404[gOptions];

    if (gbMultiPlayerMode) {
        *pbRedrew = 1;
        EnableVisibilityHashUpdate();
        PortPerfBegin(PERF_MINIMAP);
        DrawMinimap();
        PortPerfEnd(PERF_MINIMAP);
        SaveMinimapCounts();
    }
    return 1;
}
#pragma optimize("", on)

// Flash the selection circle of every live unit once. The walk is the used
// list, linked through +0x04.
void update_circle_flashes()
{
    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
        pUnit->UpdateFlash();
}

// Clear the running flag and record how the game is to be left. The replay
// writer is told the final frame count only when this is not a playback.
void gameloop_init(int nMode)
{
    if (gPlayback == 0) {
        sgbGameRunning_g = 0;
        sgwReturnMode_g = (WORD)nMode;
        replay_set_total_frames(dword_52651C);
    } else {
        sgbGameRunning_g = 0;
        sgwReturnMode_g = (WORD)nMode;
    }
}

// One frame of simulation. It runs up to sgdwUpdatesPerFrame_g network turns
// and stops early for one of seven reasons, which it publishes in
// sgdwBreakReason_g. The return says whether any turn actually ran.
#pragma optimize("g", off)
int gameloop_update(int bFlag)
{
    int nRemaining;
    int nUpdates;
    DWORD dwNow;
    int nRan;
    DWORD dwDeadline;
    int bRedrew;

    nRan = 0;
    dwDeadline = GetTickCount() + 2000;
    nUpdates = 0;
    sgdwBreakReason_g = 0;
    nRemaining = sgdwUpdatesPerFrame_g;
    while (nRemaining-- != 0) {
        // Hyperion: a LAN match watched waits for its feed (port_lanfeed.cpp), a
        // farm game watched for its worker (port_labwatch.cpp)
        if (gPlayback != 0 && (PortLanWatchHold() || PortLabWatchHold())) {
            sgdwBreakReason_g = 2;
            break;
        }
        if (gPlayback != 0)
            net_mgr_replay_turns();
        PortTrainerFeed();  // Hyperion: a trainer line's commands, as a replay's
        if (!DoNetworkTurn(&bRedrew)) {
            sgdwBreakReason_g = 1;
            break;
        }
        if (gPlayback != 0) {
            // Hyperion: a replay moved plays on past its pause to the frame
            if ((sgbReplayPaused_g != 0 && !PortReplaySeeking()) || bFlag != 0) {
                sub_480FB0();
                sgdwBreakReason_g = 2;
                break;
            }
        }
        if (!sgbGameRunning_g) {
            sgdwBreakReason_g = 4;
            break;
        }
        // Hyperion: also while the live channel keeps the game running (port_control.cpp),
        // and a LAN match watched, which goes on as the match does (port_lanfeed.cpp).
        if (sgbWindowActive_g != 0 || gbMultiPlayerMode != 0 || PortControlKeepsRunning() || PortLanWatchLive()) {
            _img_list[5].bFlags |= 2;
            int bStepped = 0;  // Hyperion: a frame of the game ran
            if (IsGamePaused()) {
                update_circle_flashes();
            } else {
                dword_52651C++;
                nRan = 1;
                nUpdates++;
                gameloop_update_game();
                PortHudFrame();  // Hyperion: the HUD's data layer looks at the frame (port_hudstate.cpp)
                bStepped = 1;
            }
            set_rand_enabled(1);
            PortPerfBegin(PERF_TRIGGERS);
            triggers_loop(sgTicksPerFrame_g[gOptions]);
            PortPerfEnd(PERF_TRIGGERS);
            set_rand_enabled(0);
            if (bStepped) {
                PortPerfBegin(PERF_CHECK);
                ReplayHxFrame();  // Hyperion: the state's check, recorded or compared
                PortRecordFrame();  // Hyperion: --record's end
                PortBenchFrame();   // Hyperion: a benchmark's two games' ends
                PortWatchFrame();  // Hyperion: a watched skirmish's two games' ends
                PortSnapProbeFrame();  // Hyperion: the trainer's memory probe
                PortSnapFrame();       // Hyperion: the trainer's snapshots
                PortReplaySeekFrame();  // Hyperion: a replay's snapshots and seek
                PortTrainerFrameEnd();
                PortPerfEnd(PERF_CHECK);
                PortPerfGameFrame();
                PortClockGameFrame();  // Hyperion: Debug Tools' game clock (port_clock.cpp)
                PortLabFrame();  // Hyperion: an AI lab worker's samples and end
                PortScoreFrame();  // Hyperion: when each player went out, for the score screen
                RichBasesFrame();  // Hyperion: which bases turned rich, said once
            }
            if (gPlayback != 0)
                sub_480FB0();
        }
        // Hyperion: the turn's sight hash, which the multiplayer sync sends (net_sync.cpp),
        // taken here, at the end of the frame the turn redrew the minimap in. DoNetworkTurn arms
        // it and 1.17 takes it at the next draw (CSprite::PriorityAddRange), which 1.17 always
        // makes before the next frame (the break below, reason 7); Hyperion may run more frames
        // before it draws, on a machine slower than the game (a browser's tab), so its hash came
        // from a later frame than the others', the sync failed as units moved, and 1.17 dropped
        // the players on each side from the other's game (seen 2026-10-20). Presentation and
        // the sync only: the draw list it builds is built again by the draw.
        if (bRedrew != 0 && gPlayback == 0)
            gamemap_priority_update();
        sgdwLastGameUpdateTime_g += sgTicksPerFrame_g[gOptions];
        dwNow = PortGameTicks();  // Hyperion: the game's clock, as RunGameLoop's
        if ((int)(dwNow - sgdwLastGameUpdateTime_g) < 0) {
            sgdwBreakReason_g = 5;
            break;
        }
        if ((int)(dwDeadline - dwNow) < 0) {
            sgdwBreakReason_g = 6;
            break;
        }
        if (bRedrew != 0) {
            if (gPlayback == 0 || dword_6663E8 == 0) {
                sgdwBreakReason_g = 7;
                break;
            }
        }
    }
    sgdwNumUpdatesLastFrame_g = nUpdates;
    return nRan;
}
#pragma optimize("", on)


// The per-frame UI half: the console and the placement box always, then the
// palette cycles at most once every ten milliseconds while the window is active,
// cycling is on and the game is not paused. Every path ends in the map message
// queue's update.
void gameloop_update_ui()
{
    status_update();
    placebox_update();
    if ((sgbWindowActive_g != 0 || PortControlKeepsRunning()) && gbColorCycle_g != 0 && !IsGamePaused()) {
        DWORD dwNow = GetTickCount();
        if (dwNow - sgdwLastCycleUpdateTime_g >= 10) {
            sgdwLastCycleUpdateTime_g = dwNow;
            // Hyperion: at a frame rate Settings chose, the new colours show with the next
            // frame due rather than in a frame of their own past that rate (port_present.cpp)
            PortPresentHold(PortFrameRate() != 0);
            do_all_cycles();
            PortPresentHold(false);
        }
    }
    map_msg_update();
}

// One simulation step, bracketed by the deterministic-random window. The fog
// counter free-runs from 100 and the tick it reaches zero on is the one that
// refreshes fog and the mask.
#pragma optimize("g", off)
void gameloop_update_game()
{
    set_rand_enabled(1);
    // Hyperion: HYPERION_PERF's parts (port_perf.cpp)
    PortPerfBegin(PERF_CREEP);
    sub_4B4010();
    PortPerfEnd(PERF_CREEP);
    PortPerfBegin(PERF_AI);
    AiFrame();  // Hyperion: sai_update, through the AI's boundary
    PortPerfEnd(PERF_AI);
    if (dword_6663E4 == 0)
        dword_6663E4 = 100;
    BYTE bTick;
    if (--dword_6663E4 == 0)
        bTick = 1;
    else
        bTick = 0;
    byte_6663EC = bTick;
    if (byte_6663EC) {
        PortPerfBegin(PERF_FOG);
        cell_fog_update();
        sub_46C180();
        AiVisionRefresh();  // Hyperion: a computer that sees the whole map
        MutatorsVisionRefresh();  // Hyperion: and the fog mutator's map
        PortPerfEnd(PERF_FOG);
    }
    PortPerfBegin(PERF_UNITS);
    CUnit::EntityUpdate();
    PortPerfEnd(PERF_UNITS);
    PortPerfBegin(PERF_MOVERS);
    CBullet::Update();
    CFlingy::UpdateFlingies();
    CThingy::UpdateThingies();
    PortPerfEnd(PERF_MOVERS);
    set_rand_enabled(0);
}
#pragma optimize("", on)

// Whether the simulation is running. The load is one byte and the zero
// extension is the return conversion; the callers test the full register.
int IsGameRunning()
{
    return sgbGameRunning_g;
}

// Take the replay's own per-speed tick table and rebuild the turn rates from
// it. The seven dwords are the whole of sgTicksPerFrame_g.
void sub_445110(DWORD *pTurnRates)
{
    for (int i = 0; i < 7; i++)
        sgTicksPerFrame_g[i] = pTurnRates[i];
    rebuild_turn_rate_table();
}

// 0x4BE950 is the one-byte `ret` the linker folded many nullary bodies into,
// called here with no argument set up; net_cmds.h declares it with two, so the
// call goes through a nullary pointer cast.
#define OC_VOICE_SQUELCH() reinterpret_cast<void (__fastcall *)()>(&voice_squelch)()

// The frame pump. It runs until the running flag clears, and each pass takes the
// event queue, the cursor, the scroll timer, the network, one simulation step
// when the game clock has caught up, and the UI. bDirty forces the image layer
// to repaint even when the mouse layer has not asked for it.
#pragma optimize("g", off)
void RunGameLoop()
{
    sgbNetworkTurnTimer = 1;
    byte_666400 = 0;
    sgdwLastGameUpdateTime_g = PortGameTicks();  // Hyperion: the game's clock, below
    BYTE bDirty = 0;
    int nBreak = 0;
    cursor_forget_target();  // Hyperion: the unit under the cursor is the last game's
    PortHotkeysLoad();       // Hyperion: the profile's command card keys (K1)
    accel_apply_profile();   // and its keys for the accelerators' commands (K2)
    while (sgbGameRunning_g) {
        // Hyperion: the picture's motion and a cutscene, a step each pass
        // (port_motion.cpp, port_cutscene.cpp)
        PortMotionTick();
        PortCutsceneTick();
        // Hyperion: a skirmish's countdown holds the game before its first frame
        // (port_intro.cpp); the view stays put and no frame runs until it lets go
        const bool bIntro = PortIntroHolds();
        bool bRanFrame = false;
        bool bDrew = false;
        PortPerfBegin(PERF_EVENTS);
        RunEvtDispatcher(3);
        PortPerfHold(PERF_EVENTS);  // counted only when this pass runs a game frame
        // Hyperion: a view switch asked for (F5, or the replay harness)
        // takes effect here, between frames (port_view.cpp).
        PortViewApply();
        PortConsoleUpdate();  // the console's layout as the HD art's (port_console.cpp)
        PortGameMenuStep();   // what Hyperion's game menu chose, outside the frame
        // Hyperion: the game's frames are due by Settings' "Game frame timing"
        // (port_clock.cpp): evenly, on the precise counter in whole ms, or on 1.17's system
        // timer, which steps every 15.6 ms, so a frame due every 42 ms ran 31 or 47 ms after
        // the last. The scroll's timer keeps 1.17's, and with it the scroll's speed.
        DWORD dwNow = PortGameTicks();
        const DWORD dwScrollNow = GetTickCount();
        // The empty then-arm is 1.17's: it branches over an unconditional jump.
        if (byte_666400) {
        } else {
            if (abs((int)(sgdwLastGameUpdateTime_g - dwNow)) > (int)sgTicksPerGameTurn[gOptions])
                sgdwLastGameUpdateTime_g = dwNow;
        }
        cursor_update();
        if (sgdwLastScrollUpdateTime_g + dwScrollNow > 10) {
            sgdwLastScrollUpdateTime_g = ~dwScrollNow + 1;
            if (!bIntro)
                scroll_update();
            bDirty = 1;
        }
        netmgr_process_messages();
        OC_VOICE_SQUELCH();
        sub_470150();
        // Hyperion: or at once, while the live channel steps fast (port_control.cpp)
        if (!bIntro && ((int)(dwNow - sgdwLastGameUpdateTime_g) >= 0 || PortControlFast() || PortBenchFast() ||
                        PortTrainerSeeking() || PortLanWatchCatching() || PortLabWatchCatching() ||
                        PortReplaySeeking())) {
            PortPerfKeep(PERF_EVENTS, true);
            nBreak = gameloop_update(0);
            // Hyperion: a step from the live channel stops at its frame
            PortControlFrameRan();
            bDirty = 1;
            bRanFrame = true;
        } else {
            PortPerfKeep(PERF_EVENTS, false);
        }
        PortPerfBegin(PERF_UI);
        gameloop_update_ui();
        // Hyperion: with no picture a benchmark's passes spin between game frames, so
        // the ui's part, like the events', counts only a pass that ran one
        PortPerfHold(PERF_UI);
        PortPerfKeep(PERF_UI, bRanFrame || !PortBenchNoPicture());
        // Hyperion: at a frame rate Settings chose (port_present.cpp), a frame only
        // when one is due, whatever is new waiting for it, so frames keep an even beat and
        // the camera glides between the steps above; as 1.17, when anything is new
        const bool bPaced = PortFrameRate() != 0;
        const bool bDue = bPaced && PortFrameDue();
        if (bDue)
            gamemap_force_redraw();
        // Hyperion: a benchmark run with no picture draws nothing of the game, and shows
        // its overlay once a second (port_bench.cpp)
        if (PortTrainerSeeking() || PortLanWatchCatching() || PortLabWatchCatching() ||
            (PortReplaySeeking() && !PortReplaySeekPictureDue())) {
            // Hyperion: a trainer seek draws nothing until it is there, nor a
            // watched LAN match catching up, nor a farm game run to its end;
            // a replay moved draws a picture every 50 ms on the way
        } else if (PortBenchNoPicture()) {
            if (PortBenchPictureDue())
                PortPresentFrame();
        } else if (bPaced ? bDue : (sgbMouseUpdated_g != 0 || bDirty)) {
            sgbMouseUpdated_g = 0;
            bDirty = 0;
            PortPerfBegin(PERF_DRAW);
            img_update();
            PortPerfEnd(PERF_DRAW);
            bDrew = true;
        }
        // Hyperion: a pass that ran no turn (nBreak is gameloop_update's return, whether
        // a turn ran) and drew nothing rests until input or what is due next, at most a
        // millisecond (port_clock.cpp); not while the game runs fast. The ms until the next
        // frame is read signed, as above: past due, a seat waiting on the network's turn
        // does not rest.
        if (!(bRanFrame && nBreak != 0) && !bDrew &&
            !(PortControlFast() || PortBenchFast() || PortTrainerSeeking() || PortLanWatchCatching() ||
              PortLabWatchCatching() || PortReplaySeeking()))
            PortClockRest(bIntro ? 1000L : static_cast<long>(static_cast<int>(sgdwLastGameUpdateTime_g - PortGameTicks())));
        if (nBreak != 0)
            nBreak = 0;
    }
}
#pragma optimize("", on)

// Play one game from the first frame to the last. The opening sets the music,
// the cursor and the palette, the middle drains the network until every player
// is in, and the tail fades out. The return is the mode gameloop_init recorded.
#pragma optimize("g", off)
WORD run_game()
{
    dword_6663E4 = 1;
    HxSaveRunGame();  // Hyperion: a Hyperion save's count to the next fog update
    PortTrainerGameStart();  // Hyperion: a trainer session's first line
    snd_play_music(word_5264BE_g);
    mou_set_pos(g_portView.nWidth / 2, g_portView.nHeight / 2);  // Hyperion: 320, 240
    sgbGameRunning_g = 1;
    unpause_game(0);
    gameloop_advance2();
    gamemap_force_redraw();
    cursor_set(DetermineCursor());
    cur_show();

    // Hyperion: not on Debug Tools' canvas or a map the live channel loaded
    // (PortLaunchGameStarts, port_launch.cpp), nor in a game --record plays unwatched or a
    // benchmark's game is made from, which the tips' pause would hold at frame 0.
    const bool bCanvas = PortLaunchGameStarts();
    // Hyperion: a skirmish's countdown, which the tips would wait under (port_intro.cpp)
    PortIntroGameStarts();
    if (!gbMultiPlayerMode && !IsALoadedGame() && gPlayback == 0
            && (dword_6D3148 & 0x100) && !bCanvas && !PortCommandOptions()->szRecord[0] && !PortIntroArmed()
            && !PortBenchMaking() && !PortWatchMaking() && !PortTrainerSession())
        do_tips_dlg(1);
    SetIsNotLoadedGame();

    vid_restore_palette(0, 0x100, sgGamePalette_g);
    init_game_font_palette();
    vid_fade_in(3);

    if (gbMultiPlayerMode)
        gOptions = byte_6728F6;
    else if (oc_wCampaignMission == 0)
        // Hyperion: 1.17 starts these at Fast; a game set up on Hyperion's skirmish
        // screen at the speed chosen there (the user, 2026-10-15)
        gOptions = gPlayback == 0 && PortFrontSkirmishGame() ? PortFrontSkirmishSpeed()
                   : PortTrainerSpeed() >= 0                       ? PortTrainerSpeed()
                                                                   : 4;

    netmgr_set_sync(1);
    RunEvtDispatcher(0);
    RestoreSelection();
    dword_672658 = 0;
    SendTurns();
    while (sgbGameRunning_g && !ProgressNetwork())
        img_update();
    RunGameLoop();
    PortTrainerGameEnd();  // Hyperion: a session saved

    netmgr_set_sync(0);
    snd_stop();
    vid_fade_out(3);
    init_game_font_palette();
    cur_hide();
    return sgwReturnMode_g;
}
#pragma optimize("", on)

// Restart the scroll timer. The stored value is the negated tick count, so the
// reader in RunGameLoop measures elapsed time with an add; `~x + 1` is how 1.17
// writes the negation.
void sub_445460()
{
    sgdwLastScrollUpdateTime_g = ~GetTickCount() + 1;
}

// Rebuild the turn rates from the live network provider's reported turn rate.
void sub_445470()
{
    rebuild_turn_rate_table();
}

// Two simulation steps and two UI updates, which is what the transition into a
// game runs before the first frame is shown.
#pragma optimize("g", off)
void gameloop_advance2()
{
    // Hyperion: a Hyperion save goes on from the frame it was taken at, without the
    // two steps, which would put it two frames ahead of the game saved
    const bool bSteps = !HxSaveGoesOn();
    if (bSteps)
        gameloop_update_game();
    gameloop_update_ui();
    if (bSteps)
        gameloop_update_game();
    gameloop_update_ui();
}
#pragma optimize("", on)
