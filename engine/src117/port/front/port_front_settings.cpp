// port_front_settings.cpp -- Settings (docs/front-end.md, F3): the rows, what each reads
// and writes (1.17's options, Hyperion's settings), the dialog and its controls, its keys,
// and the rows as the live channel lists and sets them. Presentation only.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <mutex>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "../platform/port_json.h"  // PortJsonQuote
#include "port_frontkit.h"
#include "port_frontscreens.h"
#include "../loop/port_clock.h"
#include "port_front.h"
#include "../render/port_gpu_hd.h"  // PortGpuHdCan, PortGpuHdSet, PortGpuHdLightingSet
#include "../gameplay/port_hotkeys.h"
#include "../gameplay/port_keys.h"
#include "../render/port_overlay.h"  // PortOverlayReread
#include "../platform/port_paths.h"  // PortDataFolder
#include "../render/port_present.h"
#include "../render/port_teamcolor.h"  // PortTeamShownReread
#include "../render/port_view.h"  // PortViewShape, PortViewChoose
#include "oc_data.h"        // OC_VA, where 1.17's data is
#include "gds_inifile.h"    // ini_file_close
#include "gds_vidinimo.h"   // get_gamma, set_gamma
#include "net_mgr.h"        // net_get_latency, netmgr_set_latency, send_net_cmd_SetSpeed
#include "replay.h"         // replay_get_initial_game_speed
#include "snd.h"            // snd_set_options
#include "statport.h"       // get_playback_option, set_playback_option
#include "videodlg.h"       // exit_video_opts
#include "../render/port_gpu.h"
#include "../loop/port_workers.h"  // PortCoresSet
#include "../data/port_install.h"  // PortInstallChosen, PortInstallChoose, PortInstallClassicCheck
#include "../data/port_language.h"  // the Language row
#include "../data/port_mods.h"
#include "../data/port_remastered.h"  // PortGameSource
#include "../render/port_console.h"  // PortConsoleEdgesSet
#include "../render/port_hwcursor.h"  // PortHwCursorSet
#include "../data/port_sound.h"  // PortSoundMaster, PortSoundBackground, PortSoundVoicesSet
#include "../data/port_subtitles.h"  // PortSubtitlesSet
#include "../data/port_chatfilter.h"  // PortChatFilterSet
#include "../platform/port_settings.h"
#include "../lan/port_instance.h"  // PortInstanceRestart
#include "port_ui.h"
#include "../../gen/globals117.h"
#include "port_front_internal.h"

using namespace FrontKit;

namespace Front {

// ---------------------------------------------------------------------------
// Settings (F3): SC2's Options dialog over the blacked-out home screen, as the design's D
// draws it (front.js, "settings"; front.css, "Settings (OptionsDialog)"), in SC2's units,
// 1200 to the screen's height (Sc). The rows are the design's list (front-data.js,
// SETTINGS); a row the engine has names its read and write, of 1.17's option values or of
// Hyperion's settings file (port_settings.cpp), and a row with neither draws disabled, its ?
// saying what it needs. Nothing applies until Accept (docs/modules/front-end.md).
//
// The game menu's Options opens the same dialog over a game (port_front_gamemenu.cpp),
// in place of 1.17's Speed, Sound, Video and Network dialogs. There the rows a game cannot take
// are greyed (Locked), the speed goes out as 1.17's Speed dialog sends it, the latency as its
// Network dialog does, and Accept waits for the game menu's step, out of the frame.

// CTL_THEME: the colour theme's presets and Custom's picker (port_front_theme.cpp)
// CTL_HEAD is a section's heading in a tab, the rows after it its own (Hotkeys' Addons).
enum Control { CTL_TOGGLE, CTL_SLIDER, CTL_STEPS, CTL_CHOICE, CTL_KEYS, CTL_PATH, CTL_BUTTON, CTL_THEME, CTL_HEAD };

// The rows the dialog draws, applies or checks its own way; BIND_NONE for the rest, whose row
// says all there is.
enum Bind {
  BIND_NONE,
  BIND_MSCROLL,         // the scroll speeds, which step 0 to 6
  BIND_KSCROLL,
  BIND_LATENCY,         // a multiplayer game's latency, changed only in one (NetGame)
  BIND_DISPLAY,         // a window, full screen or borderless (PortDisplayKind), applied by Accept
  BIND_RESOLUTION,      // the monitor and size (PortDisplayPick, g_resPick), as Display offers them
  BIND_ART,             // HD art, which needs the Remastered install
  BIND_HD_LIGHTING,     // and its lighting
  BIND_REMASTERED_FILES,  // Remastered's subtitles and chat filter, which need the install too
  BIND_REMASTERED_PAID,   // Remastered's recordings, its paid content like the HD art
  BIND_MENU_QUALITY,    // a pulldown, although it has only three choices
  BIND_KEY_ART,         // Hyperion's own hotkeys (port_keys.cpp): the art's
  BIND_KEY_VIEW,        // and the view's
  BIND_KEY_DEBUG_VIEW,  // and Debug Tools' overlay's (port_debugview.cpp)
  BIND_KEY_SUBGROUP,    // and the selection's: the subgroup's
  BIND_KEY_TERRAIN,     // the minimap terrain's
  BIND_KEY_IDLE,        // the idle worker's
  BIND_KEY_ARMY,        // the army's
  BIND_DATA_FOLDER,     // shown: PortDataFolder
  BIND_SETTINGS_FILE,   // shown: PortSettingsPath
  BIND_GAME_FILES,      // the kind of install the game's files come from (port_install.cpp)
  BIND_CLASSIC,         // the classic StarCraft folder, with Browse and its note (g_classicPick)
  BIND_REMASTERED,      // the Remastered install, with Browse and its note (g_remasteredPick)
  BIND_MODS_FOLDER,     // where the mods are, with Browse (g_modsPick)
  BIND_THEME,           // the menus' colours (port_frontkit.cpp) and Custom's picker (g_custom)
  BIND_LANGUAGE,        // the language (port_language.cpp), its words made as the row is read
};

struct SetValue {
  float f;
  bool bOn;
};

struct Setting;

// Where a row's value lives. A write returns true when what it changes waits for a restart.
typedef SetValue (*SettingRead)(const Setting &s);
typedef bool (*SettingWrite)(const Setting &s, const SetValue &v);

struct Setting {
  const char *pszTab;
  const char *pszLabel;
  Control control;
  const char *pszWords;   // choices, a slider's ends, keys or a path, split by '|'
  float fDefault;         // a toggle's 0 or 1, a slider's 0 to 1, a choice's index
  bool bToggle;           // a slider with a check box, greyed while off
  const char *pszFrom;    // "Classic", "SC:R" or "Hyperion"
  const char *pszNeeds;   // what it waits on; NULL, the engine has it
  const char *pszApplies; // "live", "next game" or "restart"
  Bind bind;
  SettingRead pfnRead;    // NULL: drawn at fDefault, as a row the engine does not have or only shows
  SettingWrite pfnWrite;  // NULL: it cannot be changed
  const char *pszKey;     // its name in the settings file's "hyperion" section, for a shared read or write
  const char *pszNote;
};

// ---------------------------------------------------------------------------
// Where the settings' values live: 1.17's globals and option dialogs for the classic ones,
// Hyperion's settings file and the modules that use them for its own. Each row of kSettings
// names its read and write from here.

#ifndef gOptions
#define gOptions (*reinterpret_cast<DWORD *>(OC_VA(0x6D3130u)))
#endif
#ifndef dword_6D3134
#define dword_6D3134 (*reinterpret_cast<int *>(OC_VA(0x6D3134u)))  // mscroll
#endif
#ifndef dword_6D3138
#define dword_6D3138 (*reinterpret_cast<int *>(OC_VA(0x6D3138u)))  // kscroll
#endif
#ifndef gnMusicVolume
#define gnMusicVolume (*reinterpret_cast<int *>(OC_VA(0x6D313Cu)))
#endif
#ifndef gnSfxVolume
#define gnSfxVolume (*reinterpret_cast<int *>(OC_VA(0x6D3140u)))
#endif
#ifndef dword_6D3148
#define dword_6D3148 (*reinterpret_cast<DWORD *>(OC_VA(0x6D3148u)))  // the option bits
#endif
#ifndef dword_6D314C
#define dword_6D314C (*reinterpret_cast<int *>(OC_VA(0x6D314Cu)))  // m_mscroll
#endif
#ifndef dword_6D3150
#define dword_6D3150 (*reinterpret_cast<int *>(OC_VA(0x6D3150u)))  // m_kscroll
#endif
#ifndef gbColorCycle_g
#define gbColorCycle_g (*reinterpret_cast<DWORD *>(OC_VA(0x50E138u)))
#endif

bool g_bSettingsGame;               // the dialog is over a game (the game menu's Options)

bool g_bAcceptAsked;                // and Accept waits for the game menu's step (SettingsGameStep)

std::string g_remasteredPick;       // "StarCraft: Remastered install" as the dialog shows it

std::string g_remasteredOpened;     // and as it was when the dialog opened

std::string g_remasteredNote;       // why the folder Browse gave was not taken

std::string g_classicPick;          // "Classic StarCraft folder" as the dialog shows it

std::string g_classicOpened;        // and as it was when the dialog opened

std::string g_classicNote;          // why the folder Browse gave was not taken

std::string g_modsPick;             // "Mods folder" as the dialog shows it

std::string g_modsOpened;           // and as it was when the dialog opened

float Clamp01(float f)
{
  return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

SetValue Value(float f)
{
  SetValue v = {f, true};
  return v;
}

bool On(const SetValue &v) { return v.f > 0.5f; }

// Hyperion's on-or-off settings, under the row's key; unset, the row's default.
SetValue ReadFlag(const Setting &s)
{
  double d = s.fDefault;
  PortSettingNumber("hyperion", s.pszKey, &d);
  return Value(d != 0.0 ? 1.0f : 0.0f);
}

bool StoreFlag(const Setting &s, const SetValue &v)
{
  PortSettingSetNumber("hyperion", s.pszKey, On(v) ? 1.0 : 0.0);
  return On(v);
}

bool WriteFlag(const Setting &s, const SetValue &v)
{
  StoreFlag(s, v);
  return false;
}

bool WriteConfine(const Setting &s, const SetValue &v)
{
  PortConfineSet(StoreFlag(s, v));
  return false;
}

bool WriteVsync(const Setting &s, const SetValue &v)
{
  PortGpuVsyncSet(StoreFlag(s, v));
  return false;
}

// the game timer, APM and the frame rate over the game (port_overlay.cpp)
bool WriteOverlayFlag(const Setting &s, const SetValue &v)
{
  StoreFlag(s, v);
  PortOverlayReread();
  return false;
}

bool WriteAllyColours(const Setting &s, const SetValue &v)
{
  StoreFlag(s, v);
  PortTeamShownReread();
  return false;
}

bool WriteHdLighting(const Setting &s, const SetValue &v)
{
  PortGpuHdLightingSet(StoreFlag(s, v));
  return false;
}

bool WriteHwCursor(const Setting &s, const SetValue &v)
{
  PortHwCursorSet(StoreFlag(s, v));
  return false;
}

bool WriteConsoleLayout(const Setting &s, const SetValue &v)
{
  PortConsoleEdgesSet(StoreFlag(s, v));
  return false;
}

bool WriteFullColour(const Setting &s, const SetValue &v)
{
  PortGpuFullColourSet(StoreFlag(s, v));
  return false;
}

// Hyperion's mixer (port_dsound.cpp): Master volume, a level of 0 to 1 under the row's key,
// and Sound in the background.
SetValue ReadLevel(const Setting &s)
{
  double d = s.fDefault;
  PortSettingNumber("hyperion", s.pszKey, &d);
  return Value(Clamp01(static_cast<float>(d)));
}

bool WriteMasterVolume(const Setting &s, const SetValue &v)
{
  const float f = Clamp01(v.f);
  PortSettingSetNumber("hyperion", s.pszKey, f);
  PortSoundMaster(f);
  return false;
}

bool WriteBackgroundSound(const Setting &s, const SetValue &v)
{
  PortSoundBackground(StoreFlag(s, v));
  return false;
}

// Drawn in HD at once when this run can; else (HYPERION_GPU_SCENE=off) it waits for a run
// with the GPU scene.
bool WriteArt(const Setting &s, const SetValue &v)
{
  const bool bOn = StoreFlag(s, v);
  if (PortGpuHdCan())
    PortGpuHdSet(bOn);
  else if (bOn)
    return true;
  return false;
}

// 1.17's own options. sgPrefs "speed": the single-player game speed, 0 to 6; a replay's is the
// one it was recorded at, as 1.17's Speed dialog shows it.
SetValue ReadSpeed(const Setting &)
{
  const DWORD dw = gwGameMode == 3 && gPlayback ? static_cast<DWORD>(replay_get_initial_game_speed()) : gOptions;
  return Value(static_cast<float>(dw > 6 ? 6 : dw));
}

// In a game, a command, as 1.17's Speed dialog sends it, which the replay keeps; a multiplayer
// game and a replay keep their own.
bool WriteSpeed(const Setting &, const SetValue &v)
{
  const int n = static_cast<int>(lroundf(v.f));
  if (gwGameMode != 3)
    gOptions = static_cast<DWORD>(n);
  else if (!gbMultiPlayerMode && !gPlayback)
    send_net_cmd_SetSpeed(n);
  return false;
}

// "mscroll" and "kscroll", 0 to 6, and the multiplayer rates alike: 1.17's dialog sets
// whichever the game is in, and shows a multiplayer game's.
SetValue ReadMouseScroll(const Setting &)
{
  return Value(Clamp01((gwGameMode == 3 && gbMultiPlayerMode ? dword_6D314C : dword_6D3134) / 6.0f));
}

bool WriteMouseScroll(const Setting &, const SetValue &v)
{
  dword_6D3134 = dword_6D314C = static_cast<int>(lroundf(v.f * 6.0f));
  return false;
}

SetValue ReadKeyScroll(const Setting &)
{
  return Value(Clamp01((gwGameMode == 3 && gbMultiPlayerMode ? dword_6D3150 : dword_6D3138) / 6.0f));
}

// A multiplayer game's latency, Low, High or Extra high, as 1.17's Network dialog reads and sets
// it: a command, which every player's game takes.
bool NetGame(void)
{
  return g_bSettingsGame && gwGameMode == 3 && gbMultiPlayerMode && !gPlayback;
}

SetValue ReadLatency(const Setting &)
{
  const int n = net_get_latency();
  return Value(static_cast<float>(n < 0 ? 0 : n > 2 ? 2 : n));
}

bool WriteLatency(const Setting &, const SetValue &v)
{
  if (NetGame())
    netmgr_set_latency(static_cast<int>(lroundf(v.f)));
  return false;
}

bool WriteKeyScroll(const Setting &, const SetValue &v)
{
  dword_6D3138 = dword_6D3150 = static_cast<int>(lroundf(v.f * 6.0f));
  return false;
}

// "Gamma", 60 to 140 (videodlg.cpp)
SetValue ReadGamma(const Setting &) { return Value(Clamp01((get_gamma() - 60) / 80.0f)); }

bool WriteGamma(const Setting &, const SetValue &v)
{
  set_gamma(static_cast<DWORD>(60 + lroundf(v.f * 80.0f)));
  return false;
}

SetValue ReadCycle(const Setting &) { return Value(gbColorCycle_g != 0 ? 1.0f : 0.0f); }

bool WriteCycle(const Setting &, const SetValue &v)
{
  gbColorCycle_g = On(v);
  return false;
}

// A volume's level while it is off, kept in Hyperion's section so turning it back on finds it.
float Muted(const char *pszName, float fDefault)
{
  double d = 0.0;
  return PortSettingNumber("hyperion", pszName, &d) && d > 0.0 && d <= 100.0 ? static_cast<float>(d / 100.0) : fDefault;
}

// "music" and "sfx", 0 to 100 and 0 while off; the level while off is under the row's key.
SetValue ReadVolume(const Setting &s, int n)
{
  SetValue v = Value(n > 0 ? Clamp01(n / 100.0f) : Muted(s.pszKey, s.fDefault));
  v.bOn = n > 0;
  return v;
}

int StoreVolume(const Setting &s, const SetValue &v)
{
  const int n = static_cast<int>(lroundf(v.f * 100.0f));
  PortSettingSetNumber("hyperion", s.pszKey, n);
  return v.bOn ? n : 0;
}

SetValue ReadMusic(const Setting &s) { return ReadVolume(s, gnMusicVolume); }

bool WriteMusic(const Setting &s, const SetValue &v)
{
  gnMusicVolume = StoreVolume(s, v);
  return false;
}

SetValue ReadSound(const Setting &s) { return ReadVolume(s, gnSfxVolume); }

bool WriteSound(const Setting &s, const SetValue &v)
{
  gnSfxVolume = StoreVolume(s, v);
  return false;
}

// The option bits (snd_dlg.cpp): "unitspeech", "unitnoise", "bldgnoise" and "trigtext".
SetValue ReadOptionBit(DWORD dwBit) { return Value((dword_6D3148 & dwBit) != 0 ? 1.0f : 0.0f); }

bool WriteOptionBit(DWORD dwBit, const SetValue &v)
{
  if (On(v))
    dword_6D3148 |= dwBit;
  else
    dword_6D3148 &= ~dwBit;
  return false;
}

SetValue ReadSpeech(const Setting &) { return ReadOptionBit(0x1); }
bool WriteSpeech(const Setting &, const SetValue &v) { return WriteOptionBit(0x1, v); }
SetValue ReadAcks(const Setting &) { return ReadOptionBit(0x2); }
bool WriteAcks(const Setting &, const SetValue &v) { return WriteOptionBit(0x2, v); }
SetValue ReadBuildingSounds(const Setting &) { return ReadOptionBit(0x4); }
bool WriteBuildingSounds(const Setting &, const SetValue &v) { return WriteOptionBit(0x4, v); }
SetValue ReadSubtitles(const Setting &) { return ReadOptionBit(0x400); }
bool WriteSubtitles(const Setting &, const SetValue &v) { return WriteOptionBit(0x400, v); }

// "UnitPortraits": 2 animated, 1 still, 0 off, the choices' order turned round.
SetValue ReadPortraits(const Setting &)
{
  const int n = get_playback_option();
  return Value(static_cast<float>(2 - (n < 0 ? 0 : n > 2 ? 2 : n)));
}

bool WritePortraits(const Setting &, const SetValue &v)
{
  set_playback_option(2 - static_cast<int>(lroundf(v.f)));
  return false;
}

// Hyperion's own. The display as saved; Accept shows a new one and saves it once it is kept
// (SettingsAccept), so the display's two rows write nothing here.
SetValue ReadDisplay(const Setting &)
{
  int nKind;
  PortDisplayPick pick;
  PortDisplaySaved(&nKind, &pick);
  return Value(static_cast<float>(nKind));
}

bool WriteDisplay(const Setting &, const SetValue &) { return false; }

// The view games start at ("view", saved by PortViewChoose). The row's choices are 4:3,
// widescreen and the screen's shape; the setting's values are 0, 2 and 1, the screen's shape
// having been 1 when it was the only wide one.
SetValue ReadView(const Setting &)
{
  const int nShape = PortViewShape();
  return Value(nShape == PORT_VIEW_4_3 ? 0.0f : nShape == PORT_VIEW_16_9 ? 1.0f : 2.0f);
}

bool WriteView(const Setting &, const SetValue &v)
{
  const long n = lroundf(v.f);
  PortViewChoose(n == 0 ? PORT_VIEW_4_3 : n == 1 ? PORT_VIEW_16_9 : PORT_VIEW_FIT);
  return false;
}

SetValue ReadFrameRate(const Setting &) { return Value(static_cast<float>(PortFrameRate())); }

bool WriteFrameRate(const Setting &, const SetValue &v)
{
  PortSettingSetNumber("hyperion", "frameRate", lroundf(v.f));
  PortFrameRateSet(static_cast<int>(lroundf(v.f)));
  return false;
}

SetValue ReadFrameTiming(const Setting &) { return Value(static_cast<float>(PortFrameTiming())); }

bool WriteFrameTiming(const Setting &, const SetValue &v)
{
  PortSettingSetNumber("hyperion", "frameTiming", lroundf(v.f));
  PortFrameTimingSet(static_cast<int>(lroundf(v.f)));
  return false;
}

// "workerThreads", 0 for Auto (port_workers.cpp); the choices are Auto, Off, 2, 4, 8 and 16.
SetValue ReadWorkers(const Setting &)
{
  double d = 0.0;
  PortSettingNumber("hyperion", "workerThreads", &d);
  return Value(d < 1.0 ? 0.0f : d < 2.0 ? 1.0f : d < 4.0 ? 2.0f : d < 8.0 ? 3.0f : d < 16.0 ? 4.0f : 5.0f);
}

bool WriteWorkers(const Setting &, const SetValue &v)
{
  static const int kThreads[] = {0, 1, 2, 4, 8, 16};
  const long n = lroundf(v.f);
  PortSettingSetNumber("hyperion", "workerThreads", kThreads[n >= 0 && n <= 5 ? n : 0]);
  return false;
}

SetValue ReadLoopRest(const Setting &) { return Value(PortClockRestOn() ? 1.0f : 0.0f); }

bool WriteLoopRest(const Setting &, const SetValue &v)
{
  PortSettingSetNumber("hyperion", "loopRest", On(v) ? 1.0 : 0.0);
  PortClockRestSet(On(v));
  return false;
}

bool WriteBackground(const Setting &s, const SetValue &v)
{
  PortClockBackgroundSet(StoreFlag(s, v));
  return false;
}

// "framesAhead", 1 to 3 and 2 when unset; the choices' 0 to 2.
SetValue ReadFramesAhead(const Setting &)
{
  double d = 2.0;
  PortSettingNumber("hyperion", "framesAhead", &d);
  return Value(d < 2.0 ? 0.0f : d < 3.0 ? 1.0f : 2.0f);
}

// "renderer": vulkan, the default, or direct3d12, which PresentCreate (port_present.cpp) takes
// when Hyperion starts; HYPERION_RENDERER goes before it.
SetValue ReadRenderer(const Setting &)
{
  char sz[16];
  return Value(PortSettingString("hyperion", "renderer", sz, sizeof sz) && SDL_strcasecmp(sz, "direct3d12") == 0 ? 1.0f : 0.0f);
}

bool WriteRenderer(const Setting &, const SetValue &v)
{
  PortSettingSetString("hyperion", "renderer", lroundf(v.f) == 1 ? "direct3d12" : "vulkan");
  return true;
}

// A choice of 0 to 3 under the row's key: the renderer's filter for the 640 x 480 art
// (port_present.cpp), and the console's skin (port_console.cpp), which the next game takes.
SetValue ReadStep(const Setting &s)
{
  double d = s.fDefault;
  PortSettingNumber("hyperion", s.pszKey, &d);
  return Value(static_cast<float>(d < 0.0 ? 0.0 : d > 3.0 ? 3.0 : SDL_floor(d)));
}

int StoreStep(const Setting &s, const SetValue &v)
{
  const long n = lroundf(v.f);
  const int nStep = n < 0 ? 0 : n > 3 ? 3 : static_cast<int>(n);
  PortSettingSetNumber("hyperion", s.pszKey, nStep);
  return nStep;
}

bool WriteStep(const Setting &s, const SetValue &v)
{
  StoreStep(s, v);
  return false;
}

// The Language row's choices: the install's, then Remastered's languages, each in its own name.
char g_szLanguageWords[640];

SetValue ReadLanguage(const Setting &)
{
  std::string words = PortTextF("The install\xE2\x80\x99s ({0})",
                                {PortLanguageNative(PortCascAvailable() ? PortCascTextLocale() : "enUS")});
  for (int i = 0; i < g_nPortLanguages; ++i)
    words += std::string("|") + PortLanguageNative(g_apszPortLanguages[i]);
  SDL_strlcpy(g_szLanguageWords, words.c_str(), sizeof g_szLanguageWords);
  char sz[16] = "";
  PortSettingString("hyperion", "language", sz, sizeof sz);
  for (int i = 0; i < g_nPortLanguages; ++i)
    if (SDL_strcasecmp(sz, g_apszPortLanguages[i]) == 0)
      return Value(static_cast<float>(i + 1));
  return Value(0.0f);
}

bool WriteLanguage(const Setting &, const SetValue &v)
{
  const long n = lroundf(v.f);
  PortSettingSetString("hyperion", "language", n >= 1 && n <= g_nPortLanguages ? g_apszPortLanguages[n - 1] : "install");
  return true;
}

bool WriteChatFilter(const Setting &s, const SetValue &v)
{
  PortChatFilterSet(StoreFlag(s, v));
  return false;
}

bool WriteCinematicSubtitles(const Setting &s, const SetValue &v)
{
  PortSubtitlesSet(StoreFlag(s, v));
  return false;
}

bool WriteVoices(const Setting &s, const SetValue &v)
{
  PortSoundVoicesSet(StoreStep(s, v) != 0);
  return false;
}

bool WriteArtFilter(const Setting &s, const SetValue &v)
{
  PortArtFilterSet(StoreStep(s, v));
  return false;
}

bool WriteFramesAhead(const Setting &, const SetValue &v)
{
  const long n = lroundf(v.f);
  const int nAhead = n <= 0 ? 1 : (n >= 2 ? 3 : 2);
  PortSettingSetNumber("hyperion", "framesAhead", nAhead);
  PortGpuAheadSet(nAhead);
  return false;
}

// "gameCores": Auto, Large cache or Any (PortCoresSet).
SetValue ReadGameCores(const Setting &)
{
  double d = 0.0;
  PortSettingNumber("hyperion", "gameCores", &d);
  return Value(d >= 0.0 && d <= 2.0 ? static_cast<float>(static_cast<int>(d)) : 0.0f);
}

bool WriteGameCores(const Setting &, const SetValue &v)
{
  const long n = lroundf(v.f);
  const int nChoice = n >= 0 && n <= 2 ? static_cast<int>(n) : 0;
  PortSettingSetNumber("hyperion", "gameCores", nChoice);
  PortCoresSet(nChoice);
  return false;
}

SetValue ReadMenuQuality(const Setting &) { return Value(static_cast<float>(MenuQuality())); }

bool WriteMenuQuality(const Setting &, const SetValue &v)
{
  g_nMenuQuality = static_cast<int>(lroundf(v.f));
  PortSettingSetNumber("hyperion", "menuQuality", g_nMenuQuality);
  return false;
}

// Hyperion's own hotkeys (port_keys.cpp), the key and its modifiers in one number.
SetValue ReadKeyArt(const Setting &) { return Value(static_cast<float>(PortHotkey(PORT_HOTKEY_ART))); }

bool WriteKeyArt(const Setting &, const SetValue &v)
{
  PortHotkeySet(PORT_HOTKEY_ART, static_cast<unsigned>(lroundf(v.f)));
  return false;
}

SetValue ReadKeyView(const Setting &) { return Value(static_cast<float>(PortHotkey(PORT_HOTKEY_VIEW))); }

bool WriteKeyView(const Setting &, const SetValue &v)
{
  PortHotkeySet(PORT_HOTKEY_VIEW, static_cast<unsigned>(lroundf(v.f)));
  return false;
}

SetValue ReadKeyDebugView(const Setting &) { return Value(static_cast<float>(PortHotkey(PORT_HOTKEY_DEBUG_VIEW))); }

bool WriteKeyDebugView(const Setting &, const SetValue &v)
{
  PortHotkeySet(PORT_HOTKEY_DEBUG_VIEW, static_cast<unsigned>(lroundf(v.f)));
  return false;
}

// Hyperion's hotkey a row sets (port_keys.cpp), or -1 for none.
int HotkeyOf(int bind)
{
  switch (bind) {
    case BIND_KEY_ART: return PORT_HOTKEY_ART;
    case BIND_KEY_VIEW: return PORT_HOTKEY_VIEW;
    case BIND_KEY_DEBUG_VIEW: return PORT_HOTKEY_DEBUG_VIEW;
    case BIND_KEY_SUBGROUP: return PORT_HOTKEY_SUBGROUP;
    case BIND_KEY_TERRAIN: return PORT_HOTKEY_TERRAIN;
    case BIND_KEY_IDLE: return PORT_HOTKEY_IDLE;
    case BIND_KEY_ARMY: return PORT_HOTKEY_ARMY;
  }
  return -1;
}

SetValue ReadKey(const Setting &s) { return Value(static_cast<float>(PortHotkey(HotkeyOf(s.bind)))); }

bool WriteKey(const Setting &s, const SetValue &v)
{
  PortHotkeySet(HotkeyOf(s.bind), static_cast<unsigned>(lroundf(v.f)));
  return false;
}

// The command card's keys, the profile's (port_hotkeys.cpp).
SetValue ReadCardLayout(const Setting &) { return Value(PortCardLayout() == PORT_CARD_GRID ? 1.0f : 0.0f); }

bool WriteCardLayout(const Setting &, const SetValue &v)
{
  PortCardLayoutSet(On(v) ? PORT_CARD_GRID : PORT_CARD_CLASSIC);
  return false;
}

// Where the game's files come from (port_install.cpp): the kind settings.json keeps, else the
// one this start found. Either takes a restart, as the files are opened as Hyperion starts.
SetValue ReadGameFiles(const Setting &)
{
  const PortInstallKind kind = PortInstallChosen();
  if (kind != PORT_INSTALL_UNSET)
    return Value(kind == PORT_INSTALL_CLASSIC ? 1.0f : 0.0f);
  return Value(PortGameSource() == PORT_SOURCE_MPQ ? 1.0f : 0.0f);
}

bool WriteGameFiles(const Setting &, const SetValue &v)
{
  PortInstallChoose(lroundf(v.f) == 1 ? PORT_INSTALL_CLASSIC : PORT_INSTALL_BATTLENET, NULL);
  return true;
}

// The folders the dialog holds as text: "classic", "remastered" (opened at once, unless
// another install is in use) and "modsFolder".
bool WriteClassic(const Setting &, const SetValue &)
{
  PortSettingSetString("hyperion", "classic", g_classicPick.c_str());
  return true;
}

bool WriteRemastered(const Setting &, const SetValue &)
{
  PortSettingSetString("hyperion", "remastered", g_remasteredPick.c_str());
  return PortCascUse(g_remasteredPick.c_str()) == 2;
}

bool WriteModsFolder(const Setting &, const SetValue &)
{
  PortSettingSetString("hyperion", "modsFolder", g_modsPick.c_str());
  return false;
}

// "colourTheme", 0 to 5, Custom the last; Custom's colours are saved apart (ThemeCustomSave).
SetValue ReadTheme(const Setting &)
{
  double d = 0.0;
  PortSettingNumber("hyperion", "colourTheme", &d);
  return Value(d >= 0.0 && d <= 5.0 ? static_cast<float>(d) : 0.0f);
}

bool WriteTheme(const Setting &, const SetValue &v)
{
  PortSettingSetNumber("hyperion", "colourTheme", lroundf(v.f));
  return false;
}

// "colourBlind", 0 off, 1 red-green, 2 blue-yellow (port_teamcolor.cpp).
SetValue ReadColourBlind(const Setting &)
{
  double d = 0.0;
  PortSettingNumber("hyperion", "colourBlind", &d);
  return Value(d >= 1.0 && d <= 2.0 ? static_cast<float>(d) : 0.0f);
}

bool WriteColourBlind(const Setting &, const SetValue &v)
{
  PortSettingSetNumber("hyperion", "colourBlind", lroundf(v.f));
  PortTeamShownReread();
  return false;
}

// "dragScroll", on unless set off, and "dragScrollSpeed", 0 to 1.
SetValue ReadDragScroll(const Setting &s)
{
  double d = 1.0;
  PortSettingNumber("hyperion", "dragScroll", &d);
  SetValue v = Value(0.0f);
  v.bOn = d != 0.0;
  d = s.fDefault;
  PortSettingNumber("hyperion", "dragScrollSpeed", &d);
  v.f = Clamp01(static_cast<float>(d));
  return v;
}

bool WriteDragScroll(const Setting &, const SetValue &v)
{
  PortSettingSetNumber("hyperion", "dragScroll", v.bOn ? 1.0 : 0.0);
  PortSettingSetNumber("hyperion", "dragScrollSpeed", v.f);
  PortDragScrollSet(v.bOn, v.f);
  return false;
}

// "mouseSensitivity", off unless set on, and "mouseSensitivitySpeed", 0 to 1 (port_present.cpp).
SetValue ReadSensitivity(const Setting &s)
{
  double d = 0.0;
  PortSettingNumber("hyperion", "mouseSensitivity", &d);
  SetValue v = Value(0.0f);
  v.bOn = d != 0.0;
  d = s.fDefault;
  PortSettingNumber("hyperion", "mouseSensitivitySpeed", &d);
  v.f = Clamp01(static_cast<float>(d));
  return v;
}

bool WriteSensitivity(const Setting &, const SetValue &v)
{
  PortSettingSetNumber("hyperion", "mouseSensitivity", v.bOn ? 1.0 : 0.0);
  PortSettingSetNumber("hyperion", "mouseSensitivitySpeed", v.f);
  PortMouseSensitivitySet(v.bOn, v.f);
  return false;
}

const Setting kSettings[] = {
    {"game", PT_("Game speed"), CTL_STEPS, PT_("Slowest|Slower|Slow|Normal|Fast|Faster|Fastest"), 4.0f, false, "Classic", NULL, "live", BIND_NONE,
     ReadSpeed, WriteSpeed, NULL, PT_("Single player; the lobby sets it in multiplayer.")},
    {"game", PT_("Game frame timing"), CTL_CHOICE, PT_("Classic|Even"), 1.0f, false, "Hyperion", NULL, "live", BIND_NONE,
     ReadFrameTiming, WriteFrameTiming, NULL, PT_("When each game frame runs. Classic, on the system timer, which steps every 15.6 ms, so at Fastest frames come 31 or 47 ms apart and units move unevenly; Even, on the precise clock, 42 ms apart. What a frame does is the same either way.")},
    {"game", PT_("Mouse scroll speed"), CTL_SLIDER, PT_("Slow|Fast"), 0.5f, false, "Classic", NULL, "live", BIND_MSCROLL,
     ReadMouseScroll, WriteMouseScroll, NULL, NULL},
    {"game", PT_("Keyboard scroll speed"), CTL_SLIDER, PT_("Slow|Fast"), 0.5f, false, "Classic", NULL, "live", BIND_KSCROLL,
     ReadKeyScroll, WriteKeyScroll, NULL, NULL},
    {"game", PT_("Mouse sensitivity"), CTL_SLIDER, PT_("Slow|Fast"), 0.5f, true, "SC:R", NULL, "live", BIND_NONE,
     ReadSensitivity, WriteSensitivity, NULL, PT_("On, the cursor in a game moves by the mouse\xE2\x80\x99""s movement times a scale, from half Windows\xE2\x80\x99"" speed to twice it, the same at the middle. It needs the cursor held in the window (Confine the mouse to the window), and stops while Ctrl+Alt has let it go. Off, Windows\xE2\x80\x99"" own speed, as the original has it.")},
    {"game", PT_("Confine the mouse to the window"), CTL_TOGGLE, NULL, 1.0f, false, "SC:R", NULL, "live", BIND_NONE,
     ReadFlag, WriteConfine, "confineMouse", PT_("In a game, the cursor stays in the window, as the original kept it; edge scrolling in a window needs it. Off, it leaves the window freely. Dragging on the minimap holds it there either way. Ctrl+Alt lets it go until you click the game, as in VMware.")},
    {"game", PT_("Smooth minimap drag"), CTL_TOGGLE, NULL, 0.0f, false, "Hyperion", NULL, "live", BIND_NONE,
     ReadFlag, WriteFlag, "minimapSmoothDrag", PT_("Dragging on the minimap moves the view a tile at a time, as the original does. On, the camera follows the drag smoothly between the tiles. A press on the minimap goes there at once either way.")},
    {"game", PT_("Hardware cursor"), CTL_TOGGLE, NULL, 0.0f, false, "SC:R", NULL, "live", BIND_NONE,
     ReadFlag, WriteHwCursor, "hardwareCursor", PT_("On, the system draws the game\xE2\x80\x99""s cursor over the window, so it moves with the mouse between the game\xE2\x80\x99""s frames; the art is the original\xE2\x80\x99""s, scaled as the game is. Off, the game draws it into each frame, as the original does. A browser\xE2\x80\x99""s player on a streamed seat always has it drawn into the frame.")},
    {"game", PT_("Save a replay of every game"), CTL_TOGGLE, NULL, 1.0f, false, "Hyperion", NULL, "next game", BIND_NONE,
     ReadFlag, WriteFlag, "keepReplays", PT_("The original writes every game\xE2\x80\x99""s replay as LastReplay, which the next game writes over. On, a copy of each is kept in the replays folder\xE2\x80\x99""s AutoSave folder, named by the date and the map.")},
    {"game", PT_("Run in the background"), CTL_TOGGLE, NULL, 0.0f, false, "Hyperion", NULL, "live", BIND_NONE,
     ReadFlag, WriteBackground, "runInBackground", PT_("Off, as the original: a single-player game and a replay pause while another window has the focus, and go on when you come back. On, they go on behind it. A multiplayer game always goes on. Sound in the background (Sound) decides whether you hear it.")},
    {"game", PT_("Network latency"), CTL_CHOICE, PT_("Low|High|Extra high"), 0.0f, false, "Classic", NULL, "live", BIND_LATENCY,
     ReadLatency, WriteLatency, NULL, PT_("How far ahead the players\xE2\x80\x99"" commands are sent in a multiplayer game. Low carries them out soonest; High and Extra high wait a little longer for each, so a slow or distant connection holds the game up less. Changed in a multiplayer game\xE2\x80\x99""s menu, as the original\xE2\x80\x99""s Network dialog changed it, it changes for every player.")},
    {"graphics", PT_("Display"), CTL_CHOICE, PT_("Window|Full screen|Borderless full screen"), 2.0f, false, "SC:R", NULL, "live", BIND_DISPLAY,
     ReadDisplay, WriteDisplay, NULL, PT_("Accept shows the new display at once and asks to keep it; Escape, Revert or 15 seconds go back. Only a kept display is saved. Alt+Enter switches between the saved window and the saved full screen.")},
    {"graphics", PT_("Monitor and resolution"), CTL_CHOICE, "Primary, 2560 \xC3\x97"" 1440", 0.0f, false, "SC:R", NULL, "live", BIND_RESOLUTION,
     NULL, WriteDisplay, NULL, PT_("What the chosen Display offers. Borderless: each monitor at its desktop size. Full screen: every display mode of each monitor. Window: the sizes that fit on the monitor with the window frame. Changing Display picks the closest on the same monitor.")},
#if defined(_WIN32)
    {"graphics", PT_("Renderer"), CTL_CHOICE, PT_("Vulkan (Recommended)|Direct3D 12"), 0.0f, false, "Hyperion", NULL, "restart", BIND_NONE,
     ReadRenderer, WriteRenderer, NULL, PT_("The graphics card\xE2\x80\x99""s interface Hyperion draws through. Vulkan shows each frame soonest; Direct3D 12 is there for a card or driver that Vulkan does not run well on. If the one chosen does not start, Hyperion draws through one that does.")},
#endif
    {"graphics", PT_("Art"), CTL_CHOICE, PT_("Classic|Remastered HD"), 0.0f, false, "SC:R", NULL, "live", BIND_ART,
     ReadFlag, WriteArt, "art", PT_("Remastered\xE2\x80\x99""s High Definition box. The art hotkey (F5, as in Remastered) switches it in a game, blending over half a second, and the setting keeps it. Needs the StarCraft: Remastered install (the Hyperion tab) and the GPU renderer.")},
    {"graphics", PT_("View shape"), CTL_CHOICE, PT_("4:3, as the original|Widescreen, 16:9|Fit the screen"), 2.0f, false, "SC:R", NULL, "next game", BIND_NONE,
     ReadView, WriteView, NULL, PT_("Remastered\xE2\x80\x99""s letterbox box: the shape a game starts at, the screen\xE2\x80\x99""s own unless another is chosen. The view hotkey (Ctrl+F5) switches between 4:3 and the wide shape in a game, and the setting keeps it. --view on the command line goes before it. Whether a lobby can hold everyone to a shape is decided with multiplayer.")},
    {"graphics", PT_("Classic art filtering"), CTL_STEPS, PT_("Sharp|Soft|Softer|Filtered"), 0.0f, false, "SC:R", NULL, "live", BIND_NONE,
     ReadStep, WriteArtFilter, "artFilter", PT_("How the 640 \xC3\x97"" 480 art is scaled up to the screen. Sharp keeps every pixel square and smooths only its edges, as Hyperion has drawn it until now; Soft and Softer blend more of each pixel into the next; Filtered blends them all. The HD art is always drawn sharp. SDL\xE2\x80\x99""s renderer has Sharp and Filtered alone.")},
    {"graphics", PT_("Frame rate"), CTL_CHOICE, PT_("Classic|60|Match the display|144|240|No limit"), 2.0f, false, "Hyperion", NULL, "live", BIND_NONE,
     ReadFrameRate, WriteFrameRate, NULL, PT_("How often a frame is drawn. Classic, only when the game has something new, about 80 a second, so a scroll steps. Past it, scrolls and the camera glide at the rate chosen. With Vertical sync on, no more are drawn than the display shows, since the rest would only wait in line for it.")},
    {"graphics", PT_("Vertical sync"), CTL_TOGGLE, NULL, 1.0f, false, "SC:R", NULL, "live", BIND_NONE,
     ReadFlag, WriteVsync, "vsync", PT_("Each frame waits for the display\xE2\x80\x99""s refresh, so it never tears. Off, frames show the moment they are drawn.")},
    {"graphics", PT_("Frames ahead"), CTL_CHOICE, PT_("1|2|3"), 1.0f, false, "Hyperion", NULL, "live", BIND_NONE,
     ReadFramesAhead, WriteFramesAhead, NULL, PT_("How many frames the game may prepare before the graphics card has drawn them. 1 shows each frame soonest after it is made; 3 keeps the card busiest when it is the slower part, a little later to the screen; 2 is between them.")},
    {"graphics", PT_("Menu background quality"), CTL_CHOICE, PT_("Auto|High|Low"), 0.0f, false, "Hyperion", NULL, "live", BIND_MENU_QUALITY,
     ReadMenuQuality, WriteMenuQuality, NULL, PT_("The planet behind the menus, the most a menu frame asks of the graphics card. Auto draws it at a lower resolution when the card cannot keep up with the display; High always at full; Low always at a lower one. It is soft, so a lower resolution hardly shows.")},
    {"graphics", PT_("Gamma"), CTL_SLIDER, PT_("Dark|Bright"), 0.5f, false, "Classic", NULL, "live", BIND_NONE,
     ReadGamma, WriteGamma, NULL, PT_("The original\xE2\x80\x99""s Gamma Correction.")},
    {"graphics", PT_("Colour cycling"), CTL_TOGGLE, NULL, 1.0f, false, "Classic", NULL, "live", BIND_NONE,
     ReadCycle, WriteCycle, NULL, PT_("Water and lava animation.")},
    {"graphics", PT_("Smooth unit turning"), CTL_TOGGLE, NULL, 1.0f, false, "SC:R", PT_("Remastered\xE2\x80\x99""s in-between facings in the HD art"), "live", BIND_NONE,
     NULL, NULL, NULL, PT_("HD only: Remastered\xE2\x80\x99""s in-between facings.")},
    {"graphics", PT_("HD lighting"), CTL_TOGGLE, NULL, 0.0f, false, "SC:R", NULL, "live", BIND_HD_LIGHTING,
     ReadFlag, WriteHdLighting, "hdLighting", PT_("Remastered\xE2\x80\x99""s real-time lighting on the HD art: each unit and building shaded by its own normal, specular and occlusion maps, lit from above and by the explosions, fire and muzzle flashes around it, with its lights glowing. Remastered\xE2\x80\x99""s box also sways foliage and stacks shadows, which Hyperion does not do yet.")},
    {"graphics", PT_("Player colours"), CTL_CHOICE, PT_("Classic|Exact"), 1.0f, false, "Hyperion", NULL, "live", BIND_NONE,
     ReadFlag, WriteFullColour, "fullColour", PT_("A Remastered map can give each player an exact colour. Exact draws the map\xE2\x80\x99""s own, with the map in full colour: shadows, the fog and translucent effects blended as colours. Classic draws every pixel as the original does, a map\xE2\x80\x99""s colour as the closest of the original\xE2\x80\x99""s. Exact needs the GPU renderer.")},
    {"sound", PT_("Music"), CTL_SLIDER, PT_("Min|Max"), 0.25f, true, "Classic", NULL, "live", BIND_NONE,
     ReadMusic, WriteMusic, "musicLevel", NULL},
    {"sound", PT_("Sound"), CTL_SLIDER, PT_("Min|Max"), 0.5f, true, "Classic", NULL, "live", BIND_NONE,
     ReadSound, WriteSound, "soundLevel", PT_("The original calls it Digital Volume. Both volumes start where the original\xE2\x80\x99""s do: music at 25, sound at 50.")},
    {"sound", PT_("Unit speech"), CTL_TOGGLE, NULL, 1.0f, false, "Classic", NULL, "live", BIND_NONE,
     ReadSpeech, WriteSpeech, NULL, NULL},
    {"sound", PT_("Unit acknowledgements"), CTL_TOGGLE, NULL, 1.0f, false, "Classic", NULL, "live", BIND_NONE,
     ReadAcks, WriteAcks, NULL, NULL},
    {"sound", PT_("Building sounds"), CTL_TOGGLE, NULL, 1.0f, false, "Classic", NULL, "live", BIND_NONE,
     ReadBuildingSounds, WriteBuildingSounds, NULL, NULL},
    {"sound", PT_("Subtitles"), CTL_TOGGLE, NULL, 0.0f, false, "Classic", NULL, "live", BIND_NONE,
     ReadSubtitles, WriteSubtitles, NULL, NULL},
    {"sound", PT_("Cinematic subtitles"), CTL_TOGGLE, NULL, 1.0f, false, "SC:R", NULL, "live", BIND_REMASTERED_FILES,
     ReadFlag, WriteCinematicSubtitles, "cinematicSubtitles", PT_("Remastered\xE2\x80\x99""s subtitles over the cinematics that speak, in English, read from the StarCraft: Remastered install. Off, the cinematics as the original shows them, without words.")},
    {"sound", PT_("Sound in the background"), CTL_TOGGLE, NULL, 0.0f, false, "SC:R", NULL, "live", BIND_NONE,
     ReadFlag, WriteBackgroundSound, "backgroundSound", PT_("Off, as the original: the game\xE2\x80\x99""s sounds stop while another window is in front. On, they go on playing behind it.")},
    {"sound", PT_("Unit voices"), CTL_CHOICE, PT_("Classic|Remastered"), 0.0f, false, "SC:R", NULL, "next game", BIND_REMASTERED_PAID,
     ReadStep, WriteVoices, "unitVoices", PT_("Remastered, the voices and the other sounds of a game as Remastered recorded them again, at twice the original\xE2\x80\x99""s sample rate, read from the StarCraft: Remastered install; a sound it does not have plays as the original\xE2\x80\x99""s. Classic, the original\xE2\x80\x99""s. Remastered\xE2\x80\x99""s original voice-over box, the other way round.")},
    {"sound", PT_("Master volume"), CTL_SLIDER, PT_("Min|Max"), 1.0f, false, "Hyperion", NULL, "live", BIND_NONE,
     ReadLevel, WriteMasterVolume, "masterVolume", PT_("One volume over everything Hyperion plays, on top of Music and Sound: the game\xE2\x80\x99""s sounds, the music and the cinematics. At Max they are as loud as the original makes them.")},
    {"interface", PT_("Colour theme"), CTL_THEME, NULL, 0.0f, false, "Hyperion", NULL, "live", BIND_THEME,
     ReadTheme, WriteTheme, NULL, PT_("The menus\xE2\x80\x99"" colours: an accent, a second accent and the panels. Blue and gold is the design\xE2\x80\x99""s own. Custom takes three colours you pick, and with its lightness yours, any text that would grow hard to read is lightened until it reads. The planet, the player colours and the map pictures are never themed.")},
    {"interface", PT_("Unit portraits"), CTL_CHOICE, PT_("Animated|Still|Off"), 0.0f, false, "Classic", NULL, "live", BIND_NONE,
     ReadPortraits, WritePortraits, NULL, NULL},
    {"hud", PT_("Console layout"), CTL_CHOICE, PT_("One block|To the edges"), 1.0f, false, "SC:R", NULL, "live", BIND_NONE,
     ReadFlag, WriteConsoleLayout, "consoleLayout", PT_("With the HD art in a view wider than 4:3: To the edges puts the console\xE2\x80\x99""s three parts, the minimap, the selection and the command card, toward the screen\xE2\x80\x99""s corners, as Remastered draws its HD console; One block keeps it in one piece in the middle, as the original draws it. A mod\xE2\x80\x99""s own console may keep it in one block.")},
    {"hud", PT_("Console skin"), CTL_CHOICE, PT_("By race|Terran|Protoss|Zerg"), 0.0f, false, "SC:R", NULL, "next game", BIND_NONE,
     ReadStep, WriteStep, "consoleSkin", PT_("The console in a game you play: the race played\xE2\x80\x99""s, as the original, or one race\xE2\x80\x99""s whatever you play, with its command buttons, chat box and panels. The advisor\xE2\x80\x99""s voice and the supply icon stay your race\xE2\x80\x99""s, and a replay keeps its own console.")},
    {"interface", PT_("Game timer"), CTL_TOGGLE, NULL, 1.0f, false, "SC:R", NULL, "live", BIND_NONE,
     ReadFlag, WriteOverlayFlag, "gameTimer", PT_("The game\xE2\x80\x99""""s time at the top left, as Remastered shows it: at Fastest, 42 ms a frame, which is real time when the game is played at Fastest. The original\xE2\x80\x99""""s replay panel counts slower, in Normal\xE2\x80\x99""""s seconds.")},
    {"interface", PT_("Actions per minute"), CTL_TOGGLE, NULL, 0.0f, false, "SC:R", NULL, "live", BIND_NONE,
     ReadFlag, WriteOverlayFlag, "apm", PT_("Your actions over the last game minute, under the timer: every command you give, but not chat, pausing or saving. Not in replays. Remastered can also alert at a chosen rate, which Hyperion does not yet.")},
    {"interface", PT_("Frame rate"), CTL_TOGGLE, NULL, 0.0f, false, "SC:R", NULL, "live", BIND_NONE,
     ReadFlag, WriteOverlayFlag, "fps", PT_("The frames shown each second, under the timer. The original\xE2\x80\x99""s loop draws about 80. Remastered has it in its exe with no control.")},
    {"interface", PT_("Mature language filter"), CTL_TOGGLE, NULL, 1.0f, false, "SC:R", NULL, "live", BIND_REMASTERED_FILES,
     ReadFlag, WriteChatFilter, "chatFilter", PT_("Players\xE2\x80\x99"" chat, in a game, a replay and the lobbies, shown with the words on Remastered\xE2\x80\x99""s English list starred out, the list read from the StarCraft: Remastered install. A replay keeps what was sent.")},
    {"interface", PT_("Language"), CTL_CHOICE, g_szLanguageWords, 0.0f, false, "SC:R", NULL, "restart", BIND_LANGUAGE,
     ReadLanguage, WriteLanguage, NULL, PT_("Hyperion\xE2\x80\x99s menus and the game\xE2\x80\x99s text, as in Remastered. The install\xE2\x80\x99s is the language StarCraft: Remastered was installed in. The game\xE2\x80\x99s text in another language needs that language\xE2\x80\x99s text in the install, which Battle.net installs for its chosen language alone, else the game stays in English. Hyperion\xE2\x80\x99s own text is a first translation, which players are welcome to correct.")},
    {"hotkeys", PT_("Hotkey editor"), CTL_BUTTON, PT_("Open"), 0.0f, false, "SC:R", PT_("hotkeys you can change"), "live", BIND_NONE,
     NULL, NULL, NULL, PT_("Per race and unit, on the unit\xE2\x80\x99""s own command card (one card a mode: siege, burrowed, lifted); the default key shown; conflicts flagged on the race\xE2\x80\x99""s tab.")},
    {"hotkeys", PT_("Layout"), CTL_CHOICE, PT_("Classic|Grid"), 0.0f, false, "Hyperion", NULL, "live", BIND_NONE,
     ReadCardLayout, WriteCardLayout, NULL, PT_("Grid puts the command card on the left hand\xE2\x80\x99""s 3 \xC3\x97"" 3 keys, as StarCraft II can. Remastered has no presets.")},
    {"hotkeys", PT_("Toggle art"), CTL_KEYS, "F5", 116.0f, false, "SC:R", NULL, "live", BIND_KEY_ART,
     ReadKeyArt, WriteKeyArt, NULL, PT_("Classic or HD art in a game, F5 as in Remastered. Click the key and press another: F5 to F9, F11 or F12, with Ctrl, Shift or Alt if you like (the original keeps F1 to F4 and F10). Escape leaves it as it was.")},
    {"hotkeys", PT_("Toggle view shape"), CTL_KEYS, "Ctrl+F5", 372.0f, false, "Hyperion", NULL, "live", BIND_KEY_VIEW,
     ReadKeyView, WriteKeyView, NULL, PT_("640 \xC3\x97"" 480 or the wide view in a game. Set as the art\xE2\x80\x99""s key is.")},
    {"hotkeys", PT_("Debug Tools\xE2\x80\x99 overlay"), CTL_KEYS, "F6", 117.0f, false, "Hyperion", NULL, "live", BIND_KEY_DEBUG_VIEW,
     ReadKeyDebugView, WriteKeyDebugView, NULL, PT_("Debug Tools\xE2\x80\x99 overlay in a game while its live channel is open: the game in a window with its readings round it, full screen, a counter, then off. Set as the art\xE2\x80\x99""s key is; with no key, the Overlay button at the top of the game opens it.")},
    {"hotkeys", PT_("Next subgroup"), CTL_KEYS, "Tab", 9.0f, false, "Hyperion", NULL, "live", BIND_KEY_SUBGROUP,
     ReadKey, WriteKey, NULL, PT_("In a game by Hyperion\xE2\x80\x99""s rules, the next kind of unit in the selection: the console shows its card and the page that holds it, as StarCraft II\xE2\x80\x99""s Tab. With Shift, the kind before. Click the key and press another: Tab, F5 to F9, F11 or F12, with Ctrl, Shift or Alt if you like.")},
    {"hotkeys", PT_("Minimap terrain"), CTL_KEYS, "Ctrl+Tab", 265.0f, false, "Hyperion", NULL, "live", BIND_KEY_TERRAIN,
     ReadKey, WriteKey, NULL, PT_("The minimap\xE2\x80\x99""s terrain on or off, as its button does; with Shift, the units\xE2\x80\x99"" colours, as the original\xE2\x80\x99""s Shift+Tab. In a game by classic rules Tab still does it, as in the original; in a game by Hyperion\xE2\x80\x99""s rules Tab is the subgroup\xE2\x80\x99""s.")},
    {"hotkeys", PT_("Idle worker"), CTL_KEYS, "Shift+F11", 634.0f, false, "Hyperion", NULL, "live", BIND_KEY_IDLE,
     ReadKey, WriteKey, NULL, PT_("Selects your next idle worker and takes the view to it, as the HUD\xE2\x80\x99""s idle worker button does.")},
    {"hotkeys", PT_("Select army"), CTL_KEYS, "Shift+F12", 635.0f, false, "Hyperion", NULL, "live", BIND_KEY_ARMY,
     ReadKey, WriteKey, NULL, PT_("In a game by Hyperion\xE2\x80\x99""s rules, selects your army, every unit that fights, up to 255, as the HUD\xE2\x80\x99""s army button does. A game by classic rules keeps the original\xE2\x80\x99""s selections and has no army key.")},
    {"hotkeys", PT_("Screenshot"), CTL_KEYS, "Print", 0.0f, false, "Classic", NULL, "live", BIND_NONE,
     NULL, NULL, NULL, NULL},
    {"access", PT_("Allies blue, enemies red"), CTL_TOGGLE, NULL, 0.0f, false, "Hyperion", NULL, "live", BIND_NONE,
     ReadFlag, WriteAllyColours, "allyColours", PT_("In a game you play, you are drawn in blue, your allies in teal and the other players in red, units, buildings and minimap alike, as StarCraft II\xE2\x80\x99""s team colours and Warcraft III\xE2\x80\x99""s ally colours do. The original\xE2\x80\x99""s minimap button does it for the minimap alone. Only what is drawn changes: replays and saved games keep every player\xE2\x80\x99""s own colour. Remastered has the string and no control.")},
    {"access", PT_("Colour-blind player colours"), CTL_CHOICE, PT_("Off|Red-green|Blue-yellow"), 0.0f, false, "Hyperion", NULL, "live", BIND_NONE,
     ReadColourBlind, WriteColourBlind, NULL, PT_("Each player in turn takes a colour from a palette that stays apart for that kind of colour blindness: Okabe and Ito\xE2\x80\x99""s for red-green, colours apart along red and cyan and in lightness for blue-yellow. With Allies blue, enemies red, you, your allies and your enemies take three of them. Only what is drawn changes. Remastered has the string and no control.")},
    {"access", PT_("Text size"), CTL_CHOICE, PT_("Classic|Larger|Largest"), 0.0f, false, "Hyperion", PT_("fonts larger than those the original draws at 640 by 480"), "live", BIND_NONE,
     NULL, NULL, NULL, NULL},
    {"access", PT_("Middle-mouse drag scroll"), CTL_SLIDER, PT_("Slow|Fast"), PORT_DRAG_SPEED_DEFAULT, true, "Hyperion", NULL, "live", BIND_NONE,
     ReadDragScroll, WriteDragScroll, NULL, PT_("Hold the middle button and move the mouse: the map moves with the cursor, at the speed set here, from a tenth of the cursor\xE2\x80\x99""s movement to one and a half times it; a little past the middle, the map stays under the cursor. Remastered has the string and no control.")},
    {"hyperion", PT_("Player data folder"), CTL_PATH, "Hyperion.exe\xE2\x80\x99""s folder", 0.0f, false, "Hyperion", NULL, "restart", BIND_DATA_FOLDER,
     NULL, NULL, NULL, PT_("Profiles, replays and saved games, kept apart from the original game\xE2\x80\x99""s. For now it is set with --data on the command line.")},
    {"hyperion", PT_("Game files"), CTL_CHOICE, PT_("StarCraft from Battle.net|Classic (1.16.1 and prior)"), 0.0f, false, "Hyperion", NULL, "restart", BIND_GAME_FILES,
     ReadGameFiles, WriteGameFiles, NULL, PT_("Where the game\xE2\x80\x99""s files come from: StarCraft from Battle.net, which is free, or your classic, pre-Remastered files from 1.16.1 and earlier, the folder with StarDat.mpq and BrooDat.mpq. Either way the game is drawn in its classic art. Hyperion asks when it starts and finds neither.")},
    {"hyperion", PT_("Classic StarCraft folder"), CTL_PATH, "Hyperion.exe\xE2\x80\x99""s folder", 0.0f, false, "Hyperion", NULL, "restart", BIND_CLASSIC,
     NULL, WriteClassic, NULL, PT_("Browse to the classic StarCraft folder, the one with StarDat.mpq and BrooDat.mpq; Game files then reads from it. Without one, Hyperion.exe\xE2\x80\x99""s own folder.")},
    {"hyperion", PT_("StarCraft from Battle.net"), CTL_PATH, "Not found", 0.0f, false, "Hyperion", NULL, "live", BIND_REMASTERED,
     NULL, WriteRemastered, NULL, PT_("Found from Battle.net\xE2\x80\x99""s uninstall entry, or Browse to it (a folder inside it will do). It gives the game\xE2\x80\x99""s files when Game files says so, and Remastered\xE2\x80\x99""s subtitles and chat filter either way. Another install than the one in use takes a restart.")},
    {"hyperion", PT_("Mods folder"), CTL_PATH, "<data folder>\\mods", 0.0f, false, "Hyperion", NULL, "restart", BIND_MODS_FOLDER,
     NULL, WriteModsFolder, NULL, PT_("Where the mods are, each in a folder with its mod.json. A new folder takes a restart, as the chosen mod\xE2\x80\x99""s data is read when Hyperion starts.")},
    {"hyperion", PT_("Check a mod\xE2\x80\x99""s fingerprint"), CTL_TOGGLE, NULL, 1.0f, false, "Hyperion", NULL, "live", BIND_NONE,
     ReadFlag, WriteFlag, "checkModFingerprint", PT_("A mod that changes the rules is named by a hash of its data, so a replay or a saved game made with another version of it is refused. Off, it plays all the same, for a mod\xE2\x80\x99""s author trying a change; it may go its own way where the data differs.")},
    {"hyperion", PT_("Hyperion\xE2\x80\x99""s menus"), CTL_TOGGLE, NULL, 1.0f, false, "Hyperion", NULL, "restart", BIND_NONE,
     ReadFlag, WriteFlag, "menus", PT_("Off: the original menus. Hyperion\xE2\x80\x99""s menus need the GPU renderer, which is the default.")},
    {"hyperion", PT_("Show in Discord"), CTL_TOGGLE, NULL, 1.0f, false, "Hyperion", NULL, "live", BIND_NONE,
     ReadFlag, WriteFlag, "discord", PT_("With the Discord app running on this computer, your friends see Playing Hyperion and what you are doing: the mode, the map and the players, or the mod you have loaded. Off, Discord is told nothing.")},
    {"hyperion", PT_("Settings file"), CTL_PATH, "<data folder>\\settings.json", 0.0f, false, "Hyperion", NULL, "live", BIND_SETTINGS_FILE,
     NULL, NULL, NULL, PT_("Hyperion\xE2\x80\x99""s settings and the original game\xE2\x80\x99""s, in the player data folder, out of its registry key.")},
    {"performance", PT_("Processor threads"), CTL_CHOICE, PT_("Auto|Off|2|4|8|16"), 0.0f, false, "Hyperion", NULL, "restart", BIND_NONE,
     ReadWorkers, WriteWorkers, NULL, PT_("The threads Hyperion spreads work over beside the game\xE2\x80\x99""s: the checks that a replay or a network match plays the same game, and in a large game some of what each frame looks up. Auto uses half your processor\xE2\x80\x99""s threads, up to 16; Off keeps it all on one. The game\xE2\x80\x99""s own steps always run one after another in the same order, so it plays the same either way.")},
    {"performance", PT_("Rest between frames"), CTL_TOGGLE, NULL, 1.0f, false, "Hyperion", NULL, "live", BIND_NONE,
     ReadLoopRest, WriteLoopRest, NULL, PT_("Between frames Hyperion waits for what is due next, so a game at its own speed uses a small part of one processor core. Off, it goes round without a pause, as the original did, keeping a core busy. Frames come as evenly either way.")},
    {"performance", PT_("Game\xE2\x80\x99""s cores"), CTL_CHOICE, PT_("Auto|Large cache|Any"), 0.0f, false, "Hyperion", NULL, "live", BIND_NONE,
     ReadGameCores, WriteGameCores, NULL, PT_("On a processor whose cores do not all share one cache, such as a Ryzen X3D with its two halves, which cores the game runs on. Large cache plays large games faster, a 64-player game by a tenth; Any lets Windows choose, which favours the fastest clocks and suits small games a little better. Auto takes the large cache once a game has 2,000 units. Where every core shares one cache it makes no difference.")},
    // the heading of a tab's rows not available yet (NotYet), last in the tab; in no tab itself
    {"", PT_("Not available yet"), CTL_HEAD, NULL, 0.0f, false, "Hyperion", NULL, "live", BIND_NONE,
     NULL, NULL, NULL, NULL},
};

const int kSettingCount = static_cast<int>(sizeof kSettings / sizeof kSettings[0]);

bool g_bSettings;                   // the dialog is open

int g_nTab;                         // kTabs

SetValue g_set[kSettingCount];      // what the dialog shows

SetValue g_setOpened[kSettingCount];  // as it was when the dialog opened

int g_nDrag = -1;                   // the slider being dragged

int g_nDropOpen = -1;               // the pulldown that is open

int g_nTip = -1;                    // the ? under the pointer

int g_nConfirm;                     // what Confirm asks, CONFIRM_*

int g_nRestartRow = -1;             // which setting needs it

ThemeCustom g_custom;               // Custom's colours as the dialog shows them

ThemeCustom g_customOpened;         // and as they were when it opened

PortDisplayPick g_resPick;          // "Monitor and resolution" as the dialog shows it

PortDisplayPick g_resOpened;        // and as it was when the dialog opened

int g_nKeepKind;                    // the display shown before Accept, for Revert (confirm 3)

PortDisplayPick g_keepPick;

int g_nNewKind;                     // the display Accept shows, saved on Keep

PortDisplayPick g_newPick;

Uint64 g_nKeepUntil;                // when it goes back by itself

int g_nDropFirst;                   // the open pulldown's first row shown

Bind g_browseBind = BIND_NONE;      // the row whose Browse asked for a folder

int g_nKeyCapture = -1;             // the hotkey whose key the next key press sets

int g_nKeyNoteRow = -1;             // a hotkey row's note, why a key was not taken

std::string g_keyNote;

// the folder dialog's answer, which SDL may give on another thread
std::mutex g_pickLock;

std::string g_picked;

bool g_bPicked;

void FolderBrowse(Bind bind);
void BrowsePicked(void);

// ---------------------------------------------------------------------------
// A row's value, as the dialog and the live channel read and write it.

// The value a slider steps through, or 0 for one that does not step.
int StepsOf(const Setting &s)
{
  if (s.control == CTL_STEPS)
    return static_cast<int>(Words(s.pszWords).size());
  if (s.bind == BIND_MSCROLL || s.bind == BIND_KSCROLL)
    return 7;
  return 0;
}

// Remastered's files can be used: the install is open, or the dialog holds a new one.
bool ArtAvailable(void)
{
  return PortCascAvailable() != 0 || (!g_remasteredPick.empty() && g_remasteredPick != g_remasteredOpened);
}

// A setting of Remastered's paid content: HD art, its lighting, Remastered's recordings
// (PortRemasteredOwned, port_casc.cpp), held for now (the PRD's decision of 2026-10-21).
bool PaidSetting(const Setting &s)
{
  return s.bind == BIND_ART || s.bind == BIND_HD_LIGHTING || s.bind == BIND_REMASTERED_PAID;
}

// Over a game, the rows it cannot take until it ends: what waits for a restart, the display, the
// install, the clock its frames run by, and the speed where 1.17's Speed dialog greys it, in a
// multiplayer game and a replay. The main menu's Settings changes them.
bool Locked(const Setting &s)
{
  if (!g_bSettingsGame)
    return false;
  return strcmp(s.pszApplies, "restart") == 0 || s.bind == BIND_DISPLAY || s.bind == BIND_RESOLUTION ||
         s.bind == BIND_REMASTERED || s.pfnWrite == WriteFrameTiming ||
         (s.pfnWrite == WriteSpeed && (gbMultiPlayerMode || gPlayback));
}

// Settings the player can change today.
bool Editable(const Setting &s)
{
  if (s.pfnWrite == NULL || Locked(s))
    return false;
  if (PaidSetting(s))
    return ArtAvailable() && PortRemasteredOwned();
  if (s.bind == BIND_LATENCY)
    return NetGame();
  return s.bind == BIND_REMASTERED_FILES ? ArtAvailable() : true;
}

// A row Hyperion does not have yet: one the table says waits on something, and Remastered's paid
// content while it is held. A tab lists them last, under their own heading, so what can be
// changed comes first (the user, 2026-10-26).
bool NotYet(const Setting &s)
{
  return (s.pfnWrite == NULL && s.pszNeeds != NULL) || (PaidSetting(s) && !PortRemasteredOwned());
}

// What a setting waits on, NULL when nothing: the table's, the Remastered install while
// there is none, Remastered's paid content while it is held, and a multiplayer game for its
// latency.
const char *NeedsOf(const Setting &s)
{
  if (s.bind == BIND_LATENCY && !NetGame())
    return PortText("a multiplayer game, in whose menu it changes");
  if ((PaidSetting(s) || s.bind == BIND_REMASTERED_FILES) && !ArtAvailable())
    return PortText("StarCraft from Battle.net, which the Hyperion tab can point to");
  if (PaidSetting(s) && !PortRemasteredOwned())
    return PortText("StarCraft: Remastered\xE2\x80\x99""s paid content, which this version of Hyperion does not use");
  return PortText(s.pszNeeds);
}

SetValue ReadSetting(const Setting &s)
{
  if (s.pfnRead != NULL && !NotYet(s))
    return s.pfnRead(s);
  SetValue v = Value(s.fDefault);
  // a setting the engine does not have yet, or one held (NotYet), is shown off, not ticked and
  // greyed, which would read as on and broken; a held choice shows its default, whatever the
  // settings file keeps for it (the Art row Classic while HD is held)
  if ((s.pfnWrite == NULL || NotYet(s)) && (s.control == CTL_TOGGLE || s.bToggle)) {
    v.bOn = false;
    if (s.control == CTL_TOGGLE)
      v.f = 0.0f;
  }
  return v;
}

// True when what it changes waits for a restart.
bool WriteSetting(const Setting &s, const SetValue &v)
{
  return s.pfnWrite != NULL && s.pfnWrite(s, v);
}

bool SamePick(const PortDisplayPick &a, const PortDisplayPick &b)
{
  return a.nMonitor == b.nMonitor && a.nWidth == b.nWidth && a.nHeight == b.nHeight;
}

bool SameValue(const Setting &s, const SetValue &a, const SetValue &b)
{
  if (s.bind == BIND_RESOLUTION)
    return SamePick(g_resPick, g_resOpened);
  if (s.bind == BIND_CLASSIC)
    return g_classicPick == g_classicOpened;
  if (s.bind == BIND_REMASTERED)
    return g_remasteredPick == g_remasteredOpened;
  if (s.bind == BIND_MODS_FOLDER)
    return g_modsPick == g_modsOpened;
  return fabsf(a.f - b.f) < 0.001f && (!s.bToggle || a.bOn == b.bOn);
}

int RowOf(Bind bind)
{
  for (int i = 0; i < kSettingCount; ++i)
    if (kSettings[i].bind == bind)
      return i;
  return -1;
}

// The classic StarCraft folder to show: the one settings.json keeps, else the game's folder when
// its MPQs are read, without its separator, as Browse gives a folder.
std::string ClassicInUse(void)
{
  char szFolder[MAX_PATH] = "";
  if (!PortSettingString("hyperion", "classic", szFolder, sizeof szFolder) || szFolder[0] == 0) {
    // the start's HYPERION_GAME, as Windows has it (SDL's copy of the environment predates it)
    if (PortGameSource() == PORT_SOURCE_REMASTERED ||
        GetEnvironmentVariableA("HYPERION_GAME", szFolder, sizeof szFolder) == 0 || !PortInstallClassicCheck(szFolder))
      return "";
  }
  size_t n = SDL_strlen(szFolder);
  while (n > 3 && (szFolder[n - 1] == '\\' || szFolder[n - 1] == '/'))
    szFolder[--n] = 0;
  return szFolder;
}

void SettingsOpen(bool bInGame, const char *pszTab)
{
  g_bSettingsGame = bInGame;
  g_bAcceptAsked = false;
  for (int t = 0; pszTab != NULL && t < kTabCount; ++t)
    if (strcmp(kTabs[t].pszId, pszTab) == 0)
      g_nTab = t;
  for (int i = 0; i < kSettingCount; ++i)
    g_set[i] = g_setOpened[i] = ReadSetting(kSettings[i]);
  g_custom = g_customOpened = ThemeCustomSaved();
  ThemePickerReset();
  int nKind;
  PortDisplaySaved(&nKind, &g_resPick);
  g_resOpened = g_resPick;
  g_classicPick = g_classicOpened = ClassicInUse();
  g_classicNote.clear();
  g_remasteredPick = g_remasteredOpened = PortCascFolder();
  g_remasteredNote.clear();
  g_modsPick = g_modsOpened = PortModsFolder();
  g_nKeyCapture = g_nKeyNoteRow = -1;
  g_keyNote.clear();
  g_bSettings = true;
  g_bMenuOpen = false;
  g_nDrag = g_nDropOpen = g_nTip = -1;
  g_nConfirm = CONFIRM_NONE;
  HudTabOpen();
}

void SettingsClose(void)
{
  g_bSettings = false;
  g_bSettingsGame = g_bAcceptAsked = false;
  ThemeApply();
  g_nKeyCapture = -1;
  g_nDrag = g_nDropOpen = g_nTip = -1;
}

// Accept: what changed goes to 1.17, applied as its dialogs apply it, and is saved by 1.17's
// own savers, all in one write of the settings file.
void SettingsAccept(void)
{
  g_nRestartRow = -1;
  PortSettingsHold();
  bool bAny = false;
  bool bDisplay = false;
  for (int i = 0; i < kSettingCount; ++i) {
    const Setting &s = kSettings[i];
    if (!Editable(s) || SameValue(s, g_set[i], g_setOpened[i]))
      continue;
    if (s.bind == BIND_DISPLAY || s.bind == BIND_RESOLUTION) {
      bDisplay = true;
      continue;
    }
    const bool bRestart = WriteSetting(s, g_set[i]);
    bAny = true;
    if ((bRestart || strcmp(s.pszApplies, "restart") == 0) && g_nRestartRow < 0)
      g_nRestartRow = i;
  }
  if (g_custom.accent != g_customOpened.accent || g_custom.accent2 != g_customOpened.accent2 ||
      g_custom.panel != g_customOpened.panel || g_custom.bLight != g_customOpened.bLight)
    ThemeCustomSave(g_custom);
  HudTabAccept();
  if (bAny) {
    snd_set_options(&gOptions);
    ini_file_close(0);
    exit_video_opts(0);
  }
  PortSettingsRelease();
  SettingsClose();
  if (g_nRestartRow >= 0)
    g_nConfirm = CONFIRM_RESTART;
  // the display is shown at once and kept only when the player says so, as SC2 asks; a
  // background window keeps its own, so there it is only saved
  if (bDisplay) {
    g_nNewKind = static_cast<int>(lroundf(g_set[RowOf(BIND_DISPLAY)].f));
    g_newPick = g_resPick;
    PortDisplayNow(&g_nKeepKind, &g_keepPick);
    if (PortDisplayShow(g_nNewKind, &g_newPick)) {
      g_nConfirm = CONFIRM_DISPLAY;
      g_nKeepUntil = SDL_GetTicks() + 15000;
    } else {
      PortDisplaySave(g_nNewKind, &g_newPick);
    }
  }
}

// Accept, at once, or over a game once the frame is drawn.
void SettingsAcceptAsk(void)
{
  if (g_bSettingsGame)
    g_bAcceptAsked = true;
  else
    SettingsAccept();
}

void SettingsRestoreTab(void)
{
  if (strcmp(kTabs[g_nTab].pszId, "hud") == 0)
    HudTabRestore();
  for (int i = 0; i < kSettingCount; ++i)
    if (strcmp(kSettings[i].pszTab, kTabs[g_nTab].pszId) == 0 && !Locked(kSettings[i])) {
      g_set[i].f = kSettings[i].fDefault;
      g_set[i].bOn = true;
      if (kSettings[i].bind == BIND_THEME)
        g_custom = kThemeCustomStart;
      // the primary display at the desktop's size
      PortDisplayPick picks[1];
      if (kSettings[i].bind == BIND_RESOLUTION && PortDisplayChoices(PORT_DISPLAY_BORDERLESS, picks, 1) == 1)
        g_resPick = picks[0];
    }
}

void Query(int i)
{
  const Clay_ElementId id = Id("setq", i);
  const bool bHot = Over(id);
  if (bHot)
    g_nTip = i;
  Decl q;
  q.d.id = id;
  q.d.layout.sizing.width = Fixed(Sc(22));
  q.d.layout.sizing.height = Fixed(Sc(22));
  q.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  q.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  q.d.custom.customData = Custom(DrawScQuery, kScButton, 0.0f, bHot ? 1 : 0);
  Open(q);
  Text("?", UI_FACE_BOLD, Fs(13), 0.0f, bHot ? kScInkHi : kScInkTip);
  Close();
}

// A row's label as drawn. The Language row adds the English word in another language, so a player
// who picked a language they cannot read still finds the way back.
const char *RowLabel(int i)
{
  std::string label = PortText(kSettings[i].pszLabel);
  if (kSettings[i].bind == BIND_LANGUAGE && label != kSettings[i].pszLabel)
    label += std::string(" \xC2\xB7 ") + kSettings[i].pszLabel;
  return Keep(label);
}

// A check box and its label; true when clicked.
bool CheckLine(int i, const char *pszId, bool bOn, bool bLive, bool bDim)
{
  const Clay_ElementId id = Id(pszId, i);
  const bool bHot = bLive && Over(id);
  const bool bClicked = bLive && Clicked(id);
  Decl line;
  line.d.id = id;
  line.d.layout.childGap = Gap(Fs(10));
  line.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(line);
  Decl box;
  box.d.layout.sizing.width = Fixed(Sc(31));
  box.d.layout.sizing.height = Fixed(Sc(30));
  box.d.custom.customData = Custom(DrawScCheck, kScButton, 0.0f, (bOn ? 1 : 0) | (bHot ? 2 : 0) | (bDim ? 8 : 0));
  Open(box);
  Close();
  Text(RowLabel(i), UI_FACE_BOLD, Fs(18), 0.0f, bDim ? kScInkOff : bHot ? kScInkHi : kScInkLabel);
  Close();
  return bClicked;
}

void Slider(int i, bool bLive, bool bDim)
{
  const Setting &s = kSettings[i];
  const int nSteps = StepsOf(s);
  SetValue &v = g_set[i];
  const Clay_ElementId id = Id("setslider", i);
  if (bLive && g_bPressed && Over(id))
    g_nDrag = i;
  if (g_nDrag == i && bLive) {
    const Clay_ElementData e = Clay_GetElementData(id);
    if (e.found && e.boundingBox.width > Sc(18)) {
      float f = Clamp01((g_mouseX - e.boundingBox.x - Sc(9)) / (e.boundingBox.width - Sc(18)));
      if (nSteps > 1)
        f = roundf(f * (nSteps - 1)) / (nSteps - 1);
      v.f = s.control == CTL_STEPS ? f * (nSteps - 1) : f;
    }
  }
  const float fAt = s.control == CTL_STEPS && nSteps > 1 ? v.f / (nSteps - 1) : v.f;
  Decl wrap;
  wrap.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  wrap.d.layout.sizing.width = Grow();
  wrap.d.layout.padding = Pad(0, 0, Fs(2), 0);
  Open(wrap);
  Decl sl;
  sl.d.id = id;
  sl.d.layout.sizing.width = Grow();
  sl.d.layout.sizing.height = Fixed(Sc(32));
  sl.d.custom.customData =
      Custom(DrawScSlider, kScButton, fAt, (bLive && (Over(id) || g_nDrag == i) ? 1 : 0) | (bDim || !bLive ? 2 : 0));
  Open(sl);
  Close();
  // its ends' names under it
  const std::vector<std::string> ends = Words(PortText(s.pszWords));
  if (ends.size() >= 2) {
    Decl row;
    row.d.layout.sizing.width = Grow();
    row.d.layout.padding = Pad(0, 0, Fs(2), 0);
    Open(row);
    Text(Keep(ends.front()), UI_FACE_TEXT, Fs(16), 0.0f, kScInkOff);
    Decl grow;
    grow.d.layout.sizing.width = Grow();
    Open(grow);
    Close();
    Text(Keep(ends.back()), UI_FACE_TEXT, Fs(16), 0.0f, kScInkOff);
    Close();
  }
  Close();
}

// A label line: the label, its ?, and a stepped slider's value.
void LabelLine(int i, bool bDim, const char *pszValue)
{
  Decl line;
  line.d.layout.childGap = Gap(Fs(10));
  line.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  line.d.layout.sizing.height = Fit();
  Open(line);
  Text(RowLabel(i), UI_FACE_BOLD, Fs(18), 0.0f, bDim ? kScInkOff : kScInkLabel);
  Query(i);
  if (pszValue != NULL)
    Text(Keep(pszValue), UI_FACE_WIDE, Fs(16), 0.0f, bDim ? kScInkOff : kScInkHi);
  Close();
}

// "Monitor and resolution": what the Display chosen above it offers, the pick the dialog
// holds found among them (or the closest in area on its monitor, which it then holds).
int ResolutionWords(std::vector<std::string> *pWords, std::vector<PortDisplayPick> *pPicks)
{
  static PortDisplayPick s_picks[256];
  const int nDisplay = RowOf(BIND_DISPLAY);
  const int nKind = nDisplay >= 0 ? static_cast<int>(lroundf(g_set[nDisplay].f)) : PORT_DISPLAY_BORDERLESS;
  const int n = PortDisplayChoices(nKind, s_picks, 256);
  int nAt = -1;
  for (int k = 0; k < n; ++k) {
    const PortDisplayPick &p = s_picks[k];
    char sz[160];
    SDL_snprintf(sz, sizeof sz, "%s, %d \xC3\x97 %d", p.bPrimary ? PortText("Primary") : p.szMonitor, p.nWidth, p.nHeight);
    pWords->push_back(sz);
    pPicks->push_back(p);
    if (nAt < 0 && SamePick(p, g_resPick))
      nAt = k;
  }
  // else the size on the same monitor closest in area
  double dBest = 0.0;
  const double dArea = static_cast<double>(g_resPick.nWidth) * g_resPick.nHeight;
  for (int k = 0; k < n && !(nAt >= 0 && SamePick(s_picks[nAt], g_resPick)); ++k) {
    if (s_picks[k].nMonitor != g_resPick.nMonitor)
      continue;
    const double d = fabs(static_cast<double>(s_picks[k].nWidth) * s_picks[k].nHeight - dArea);
    if (nAt < 0 || d < dBest) {
      nAt = k;
      dBest = d;
    }
  }
  if (nAt < 0 && n > 0)
    nAt = 0;
  if (nAt >= 0)
    g_resPick = s_picks[nAt];
  return nAt;
}

void Choices(int i, bool bLive, bool bDim)
{
  const Setting &s = kSettings[i];
  std::vector<std::string> words;
  std::vector<PortDisplayPick> picks;
  int nAt;
  if (s.bind == BIND_RESOLUTION) {
    nAt = ResolutionWords(&words, &picks);
  } else {
    words = Words(PortText(s.pszWords));
    nAt = static_cast<int>(lroundf(g_set[i].f));
  }
  // the menu background's quality is a pulldown, as the player asked, short as its words are;
  // so is a choice with a word longer than 16 characters
  bool bRadio = words.size() <= 3 && s.bind != BIND_MENU_QUALITY;
  for (const std::string &w : words)
    bRadio = bRadio && std::count_if(w.begin(), w.end(), [](char c) { return (c & 0xC0) != 0x80; }) <= 16;
  if (bRadio) {
    Decl row;
    row.d.layout.childGap = Gap(Fs(28));
    row.d.layout.padding = Pad(0, 0, Fs(4), 0);
    Open(row);
    for (size_t k = 0; k < words.size(); ++k) {
      const int nId = i * 16 + static_cast<int>(k);
      const Clay_ElementId id = Id("setradio", nId);
      const bool bOn = static_cast<int>(k) == nAt;
      const bool bHot = bLive && Over(id);
      if (bLive && Clicked(id))
        g_set[i].f = static_cast<float>(k);
      Decl r;
      r.d.id = id;
      r.d.layout.childGap = Gap(Fs(10));
      r.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      Open(r);
      Decl dot;
      dot.d.layout.sizing.width = Fixed(Sc(30));
      dot.d.layout.sizing.height = Fixed(Sc(30));
      dot.d.custom.customData =
          Custom(DrawScCheck, kScButton, 0.0f, (bOn ? 1 : 0) | (bHot ? 2 : 0) | 4 | (bDim ? 8 : 0));
      Open(dot);
      Close();
      Text(Keep(words[k]), UI_FACE_BOLD, Fs(18), 0.0f, bDim ? kScInkOff : bOn || bHot ? kScInkHi : kScInkButton);
      Close();
    }
    Close();
    return;
  }
  // the pulldown, its menu under it while open
  const Clay_ElementId id = Id("setdrop", i);
  const bool bOpen = g_nDropOpen == i;
  const bool bHot = bLive && (Over(id) || bOpen);
  const int kShown = 12;  // rows the menu shows; the wheel scrolls the rest
  const int nWords = static_cast<int>(words.size());
  if (bLive && Clicked(id)) {
    g_nDropOpen = bOpen ? -1 : i;
    g_nDropFirst = SDL_max(0, SDL_min(nAt - kShown / 2, nWords - kShown));
  }
  Decl dd;
  dd.d.id = id;
  dd.d.layout.sizing.width = Fixed(Sc(420));
  dd.d.layout.sizing.height = Fixed(Sc(44));
  dd.d.layout.padding = Pad(Fs(16), Fs(50), 0, 0);
  dd.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  dd.d.custom.customData = Custom(DrawScRaised, bDim ? kScOff : bHot ? kScDropHi : kScDrop, 0.0f, bOpen ? 2 : 1);
  Open(dd);
  Text(Keep(nAt >= 0 && nAt < static_cast<int>(words.size()) ? words[nAt] : std::string()), UI_FACE_WIDE,
       Fs(16), 0.0f, bDim ? kScInkOff : bHot ? kScInkHi : kScInkButton);
  if (bOpen) {
    Decl list;
    list.d.id = Id("setdroplist");
    list.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    list.d.layout.sizing.width = Fixed(Sc(420));
    list.d.layout.padding = Pad(Fs(10), Fs(10), Fs(10), Fs(10));
    list.d.layout.childGap = Gap(0.9f);
    list.d.floating.attachTo = CLAY_ATTACH_TO_PARENT;
    // under the face, or over it when the list (its height as last laid out) would pass the
    // screen's foot and there is more room above
    const Clay_ElementData face = Clay_GetElementData(id), was = Clay_GetElementData(list.d.id);
    const float fBelow = face.found ? UiHeight() - (face.boundingBox.y + face.boundingBox.height) : 0.0f;
    const bool bUp = face.found && was.found && was.boundingBox.height + Sc(4) > fBelow && face.boundingBox.y > fBelow;
    list.d.floating.attachPoints.element = bUp ? CLAY_ATTACH_POINT_LEFT_BOTTOM : CLAY_ATTACH_POINT_LEFT_TOP;
    list.d.floating.attachPoints.parent = bUp ? CLAY_ATTACH_POINT_LEFT_TOP : CLAY_ATTACH_POINT_LEFT_BOTTOM;
    list.d.floating.offset.y = bUp ? -Sc(4) : Sc(4);
    list.d.floating.zIndex = 80;
    const ScLook menu = {kScMenuFill, kScMenuFill, kScMenuRim, kScMenuFill, kScBlack, 6};
    list.d.custom.customData = Custom(DrawScBox, menu);
    Open(list);
    if (g_nWheel != 0 && nWords > kShown) {
      g_nDropFirst = SDL_max(0, SDL_min(g_nDropFirst + WheelRows(true), nWords - kShown));
      g_nWheel = 0;
    }
    const int nFirst = nWords > kShown ? g_nDropFirst : 0;
    const int nLast = SDL_min(nWords, nFirst + kShown);
    if (nWords > kShown) {
      // a bar down the right edge: where the rows shown are among them all
      Decl bar;
      bar.d.layout.sizing.width = Fixed(Sc(4));
      bar.d.layout.sizing.height = Fixed(Sc(28) * kShown * kShown / nWords);
      bar.d.backgroundColor = Col(UiRgb(0x00739e));
      bar.d.cornerRadius.topLeft = Sc(2);
      bar.d.floating.attachTo = CLAY_ATTACH_TO_PARENT;
      bar.d.floating.attachPoints.element = CLAY_ATTACH_POINT_RIGHT_TOP;
      bar.d.floating.attachPoints.parent = CLAY_ATTACH_POINT_RIGHT_TOP;
      bar.d.floating.offset.x = -Sc(3);
      bar.d.floating.offset.y = Sc(10) + Sc(28) * kShown * nFirst / nWords;
      bar.d.floating.zIndex = 81;
      bar.d.floating.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH;
      Open(bar);
      Close();
    }
    for (int k = nFirst; k < nLast; ++k) {
      const Clay_ElementId oid = Id("setdropopt", k);
      const bool bOptHot = Over(oid);
      const bool bSel = k == nAt;
      if (Clicked(oid)) {
        if (s.bind == BIND_RESOLUTION)
          g_resPick = picks[k];
        else
          g_set[i].f = static_cast<float>(k);
        g_nDropOpen = -1;
      }
      Decl o;
      o.d.id = oid;
      o.d.layout.sizing.width = Grow();
      o.d.layout.sizing.height = Fixed(Sc(27));
      o.d.layout.padding = Pad(Fs(8), Fs(8), 0, 0);
      o.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      o.d.clip.horizontal = true;
      o.d.cornerRadius.topLeft = Sc(3);
      o.d.backgroundColor = Col(bSel ? kScMenuSel : bOptHot ? UiColor{11 / 255.0f, 22 / 255.0f, 51 / 255.0f, 0.69f} : kNone);
      Open(o);
      Text(Keep(words[k]), UI_FACE_WIDE, Fs(16), 0.0f, bSel || bOptHot ? kScInkHi : kScInkMenu);
      Close();
    }
    Close();
  }
  Close();
}

// A path too long for its field loses its start, which says least.
std::string ElideFront(const char *psz, float fMax)
{
  const float fSize = roundf(UiPx(Fs(16)));
  if (fMax <= 0.0f || UiTextWidth(UI_FACE_WIDE, fSize, 0.0f, psz) <= fMax)
    return psz;
  const char *p = psz;
  while (*p) {
    ++p;
    while ((*p & 0xC0) == 0x80)
      ++p;
    const std::string s = std::string("\xE2\x80\xA6") + p;
    if (UiTextWidth(UI_FACE_WIDE, fSize, 0.0f, s.c_str()) <= fMax)
      return s;
  }
  return std::string();
}

// A field: a key box or the path field. A path's is as wide as it was laid out last frame.
void Field(const char *psz, bool bGrow, bool bDim, int nId = -1)
{
  Decl f;
  std::string text(psz);
  if (nId >= 0) {
    f.d.id = Id("setfield", nId);
    const Clay_ElementData e = Clay_GetElementData(f.d.id);
    if (e.found && bGrow)
      text = ElideFront(psz, e.boundingBox.width - UiPx(Fs(28)));
  }
  f.d.layout.sizing.width = bGrow ? Grow() : Fit();
  f.d.layout.sizing.height = Fixed(Sc(44));
  f.d.layout.padding = Pad(Fs(14), Fs(14), 0, 0);
  f.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  f.d.clip.horizontal = true;
  const ScLook look = {UiRgb(0x010305), UiRgb(0x09223c), kScDdRim, kScBlack, kScBlack, 5};
  f.d.custom.customData = Custom(DrawScBox, look);
  Open(f);
  Text(Keep(text), UI_FACE_WIDE, Fs(16), 0.0f, bDim ? kScInkOff : kScInkHi);
  Close();
}

void SettingGroup(int i)
{
  const Setting &s = kSettings[i];
  const bool bLive = Editable(s);
  const bool bDim = NeedsOf(s) != NULL || Locked(s);
  SetValue &v = g_set[i];
  Decl g;
  g.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  g.d.layout.sizing.width = Grow();
  g.d.layout.childGap = Gap(Fs(8));
  g.d.layout.padding = Pad(0, 0, 0, Fs(26));
  Open(g);
  switch (s.control) {
    case CTL_TOGGLE: {
      Decl line;
      line.d.layout.childGap = Gap(Fs(10));
      line.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      Open(line);
      if (CheckLine(i, "setcheck", v.f > 0.5f, bLive, bDim))
        v.f = v.f > 0.5f ? 0.0f : 1.0f;
      Query(i);
      Close();
      break;
    }
    case CTL_SLIDER:
      if (s.bToggle) {
        Decl line;
        line.d.layout.childGap = Gap(Fs(10));
        line.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
        Open(line);
        if (CheckLine(i, "setcheck", v.bOn, bLive, bDim))
          v.bOn = !v.bOn;
        Query(i);
        Close();
        Slider(i, bLive && v.bOn, bDim || !v.bOn);
      } else {
        LabelLine(i, bDim, NULL);
        Slider(i, bLive, bDim);
      }
      break;
    case CTL_STEPS: {
      const std::vector<std::string> words = Words(PortText(s.pszWords));
      const int n = static_cast<int>(lroundf(v.f));
      LabelLine(i, bDim, n >= 0 && n < static_cast<int>(words.size()) ? words[n].c_str() : "");
      Slider(i, bLive, bDim);
      break;
    }
    case CTL_CHOICE:
      LabelLine(i, bDim, NULL);
      Choices(i, bLive, bDim);
      break;
    case CTL_KEYS: {
      LabelLine(i, bDim, NULL);
      Decl row;
      row.d.layout.childGap = Gap(Fs(8));
      Open(row);
      if (HotkeyOf(s.bind) >= 0) {
        // a click, then the key pressed (KeyCapture); none when another took its default
        char szKey[48];
        SDL_strlcpy(szKey, PortText("None"), sizeof szKey);
        if (lroundf(v.f) > 0)
          PortHotkeyName(static_cast<unsigned>(lroundf(v.f)), szKey, sizeof szKey);
        Field(g_nKeyCapture == i ? PortText("Press a key") : szKey, false, bDim, i);
        if (bLive && Clicked(Id("setfield", i))) {
          g_nKeyCapture = i;
          g_nKeyNoteRow = -1;
        }
      } else {
        for (const std::string &k : Words(s.pszWords))
          Field(k.c_str(), false, bDim);
      }
      Close();
      if (g_nKeyNoteRow == i && !g_keyNote.empty())
        Text(Keep(g_keyNote), UI_FACE_WIDE, Fs(14), 0.0f, kScInkLabel);
      break;
    }
    case CTL_PATH: {
      LabelLine(i, bDim, NULL);
      Decl row;
      row.d.layout.sizing.width = Grow();
      row.d.layout.childGap = Gap(Fs(10));
      row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      Open(row);
      const char *psz = s.pszWords;
      if (s.bind == BIND_DATA_FOLDER)
        psz = PortDataFolder();
      else if (s.bind == BIND_SETTINGS_FILE)
        psz = PortSettingsPath();
      else if (s.bind == BIND_CLASSIC && !g_classicPick.empty())
        psz = g_classicPick.c_str();
      else if (s.bind == BIND_REMASTERED && !g_remasteredPick.empty())
        psz = g_remasteredPick.c_str();
      else if (s.bind == BIND_MODS_FOLDER)
        psz = g_modsPick.c_str();
      Field(psz != NULL ? psz : "", true, bDim, i);
      Decl b;
      b.d.id = Id("setbrowse", i);
      b.d.layout.sizing.height = Fixed(Sc(42));
      if (ScMini(b, PortText("Browse"), 0, bLive && (s.bind == BIND_CLASSIC || s.bind == BIND_REMASTERED || s.bind == BIND_MODS_FOLDER)))
        FolderBrowse(s.bind);
      Close();
      if (s.bind == BIND_CLASSIC && !g_classicNote.empty())
        Text(Keep(g_classicNote), UI_FACE_WIDE, Fs(14), 0.0f, kScInkLabel);
      if (s.bind == BIND_REMASTERED && !g_remasteredNote.empty())
        Text(Keep(g_remasteredNote), UI_FACE_WIDE, Fs(14), 0.0f, kScInkLabel);
      break;
    }
    case CTL_THEME: {
      LabelLine(i, bDim, NULL);
      int n = static_cast<int>(lroundf(v.f));
      ThemeGroup(&n, &g_custom, bLive);
      v.f = static_cast<float>(n);
      break;
    }
    case CTL_HEAD:
      // a section's heading, as the benchmark's and the lobby's are
      Text(Keep(Upper(PortText(s.pszLabel))), UI_FACE_WIDE, Fs(15), 0.04f, kScInkHead);
      break;
    case CTL_BUTTON: {
      LabelLine(i, bDim, NULL);
      Decl b;
      b.d.id = Id("setbutton", i);
      b.d.layout.sizing.height = Fixed(Sc(42));
      ScMini(b, s.pszWords != NULL ? PortText(s.pszWords) : PortText("Open"), 0, false);
      break;
    }
  }
  Close();
}

// How tall a setting stands, in SC2 units, for splitting a tab into two columns (front.js,
// optionsPane).
int SettingHeight(const Setting &s)
{
  if (s.control == CTL_THEME)  // the presets, and under Custom the picker: a column of its own
    return lroundf(g_set[RowOf(BIND_THEME)].f) == 5 ? 760 : 260;
  if (s.control == CTL_HEAD)
    return 50;
  return s.control == CTL_TOGGLE ? 44 : s.control == CTL_SLIDER ? (s.bToggle ? 120 : 110) : s.control == CTL_STEPS ? 110 : 90;
}

// The ? under the pointer says the setting's note, where it comes from, what it needs and
// when it applies.
void Tip(int i, bool bLeft)
{
  static const char *const kFrom[][2] = {{"Classic", PT_("In the original\xE2\x80\x99s option dialogs.")},
                                         {"SC:R", PT_("Added by Remastered.")},
                                         {"Hyperion", PT_("New in Hyperion.")}};
  const Setting &s = kSettings[i];
  // its sentences, a space apart
  std::string meta;
  const auto add = [&meta](const std::string &sentence) { meta += (meta.empty() ? "" : " ") + sentence; };
  for (const auto &f : kFrom)
    if (strcmp(f[0], s.pszFrom) == 0)
      add(PortText(f[1]));
  // a row greyed for want of the Remastered install says so, as the player can fix it; any other
  // greyed row is one Hyperion does not have yet (NotYet, under that heading), or one a game
  // cannot take
  const char *pszNeeds = NeedsOf(s);
  const bool bNotYet = NotYet(s);
  if (bNotYet)
    add(PortText("Not available yet."));
  else if (pszNeeds != NULL)
    add(PortTextF("Needs {0}.", {pszNeeds}));
  if (Locked(s))
    add(PortText("Not during a game: change it in the main menu\xE2\x80\x99s Settings."));
  else if (!bNotYet) {
    if (strcmp(s.pszApplies, "live") == 0)
      add(PortText("Applies when you accept it."));
    else if (strcmp(s.pszApplies, "next game") == 0)
      add(PortText("Applies from the next game."));
    else
      add(PortText("Applies after a restart."));
  }
  Decl t;
  t.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  t.d.layout.sizing.width = Fit(Sc(480));
  t.d.layout.padding = Pad(Fs(15), Fs(15), Fs(15), Fs(15));
  t.d.layout.childGap = Gap(Fs(8));
  t.d.floating.attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID;
  t.d.floating.parentId = Id("setq", i).id;
  t.d.floating.attachPoints.element = bLeft ? CLAY_ATTACH_POINT_RIGHT_TOP : CLAY_ATTACH_POINT_LEFT_TOP;
  t.d.floating.attachPoints.parent = bLeft ? CLAY_ATTACH_POINT_LEFT_TOP : CLAY_ATTACH_POINT_RIGHT_TOP;
  t.d.floating.offset.x = bLeft ? -Sc(10) : Sc(10);
  t.d.floating.offset.y = -Sc(8);
  t.d.floating.zIndex = 90;
  t.d.floating.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH;
  const ScLook look = {kScTipFill, kScTipFill, kScTipRim, kScTipFill, kScBlack, 3};
  t.d.custom.customData = Custom(DrawScBox, look);
  Open(t);
  if (s.pszNote != NULL)
    Text(PortText(s.pszNote), UI_FACE_TEXT, Fs(20), 0.0f, kScInkTip, -1, 1.3f);
  Text(Keep(meta), UI_FACE_TEXT, Fs(16), 0.0f, kScInkMini, -1, 1.3f);
  Close();
}

void Confirm(void)
{
  if (g_nConfirm == CONFIRM_RESTORE) {
    const std::string title = PortTextF("Restore the {0} settings?", {PortText(kTabs[g_nTab].pszLabel)});
    const char *const kButtons[] = {PortText("Continue"), PortText("Cancel")};
    const int n = ScDialog(Keep(title), PortText("Every setting on this tab goes back to its default. The other tabs keep theirs."),
                           kButtons, "C ", 2);
    if (n == 0)
      SettingsRestoreTab();
    if (n >= 0 || Key(VK_ESCAPE))
      g_nConfirm = CONFIRM_NONE;
  } else if (g_nConfirm == CONFIRM_DISPLAY) {
    const Uint64 nNow = SDL_GetTicks();
    const int nLeft = nNow >= g_nKeepUntil ? 0 : static_cast<int>((g_nKeepUntil - nNow + 999) / 1000);
    const std::string body = PortTextNF("Hyperion goes back to the display it had in {0} second.",
                                        "Hyperion goes back to the display it had in {0} seconds.", nLeft,
                                        {std::to_string(nLeft)});
    const char *const kButtons[] = {PortText("Keep"), PortText("Revert")};
    const int n = ScDialog(PortText("Keep this display?"), Keep(body), kButtons, "KR", 2);
    const bool bBack = n == 1 || (n < 0 && (nLeft == 0 || Key(VK_ESCAPE)));
    if (n == 0)
      PortDisplaySave(g_nNewKind, &g_newPick);
    else if (bBack)
      PortDisplayShow(g_nKeepKind, &g_keepPick);
    if (n == 0 || bBack)
      g_nConfirm = g_nRestartRow >= 0 ? CONFIRM_RESTART : CONFIRM_NONE;
  } else if (g_nConfirm == CONFIRM_MOD_CHANNEL) {
    const char *const kButtons[] = {PortText("Allow"), PortText("Don't allow")};
    const int n = ScDialog(PortText("Open the live channel?"), Keep(ModsChannelText()), kButtons, "AD", 2);
    if (n >= 0 || Key(VK_ESCAPE))
      ModsChannelAnswered(n == 0);
  } else if (g_nConfirm == CONFIRM_RESTART || g_nConfirm == CONFIRM_MOD_RESTART) {
    std::string body = g_nConfirm == CONFIRM_MOD_RESTART
                           ? ModsRestartText()
                           : PortTextF("The setting \xE2\x80\x9C{0}\xE2\x80\x9D changes when Hyperion next starts.",
                                       {PortText(kSettings[g_nRestartRow].pszLabel)});
    // over a game there is no restarting now, only the word
    const char *const kButtons[] = {PortText("Restart now"), PortText("Later")};
    const char *const kGameButtons[] = {PortText("OK")};
    if (gwGameMode == 3) {
      if (ScDialog(PortText("Restart Hyperion"), Keep(body), kGameButtons, "O", 1) >= 0 || Key(VK_ESCAPE) || Key(VK_RETURN))
        g_nConfirm = CONFIRM_NONE;
      return;
    }
    const int n = ScDialog(PortText("Restart Hyperion"), Keep(body), kButtons, "RL", 2);
    // Restart now: a new copy started, which waits for this one to end, and this one quits as
    // the menu's Exit does; for a mod, without the --mod this copy may have started with
    if (n == 0 && PortInstanceRestart(g_nConfirm == CONFIRM_MOD_RESTART ? "--mod" : NULL))
      g_nChoice = 2;
    if (n >= 0 || Key(VK_ESCAPE) || Key(VK_RETURN))
      g_nConfirm = CONFIRM_NONE;
  }
}

void SettingsDialog(void)
{
  g_nTip = -1;
  // the theme as the rows have it, before Accept
  ThemeShow(static_cast<int>(lroundf(g_set[RowOf(BIND_THEME)].f)), g_custom);
  BrowsePicked();
  // a click away from the hotkey waiting for a key leaves it as it was
  if (g_nKeyCapture >= 0 && g_bReleased && !Over(Id("setfield", g_nKeyCapture)))
    g_nKeyCapture = -1;
  if (!g_bDown)
    g_nDrag = -1;
  Decl black;
  black.d.id = Id("setblack");
  black.d.layout.sizing.width = Fixed(static_cast<float>(UiWidth()));
  black.d.layout.sizing.height = Fixed(static_cast<float>(UiHeight()));
  black.d.backgroundColor = Col(kScBlackout);
  black.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  black.d.floating.zIndex = 60;
  Open(black);
  Close();

  Decl dlg;
  dlg.d.id = Id("setdlg");
  dlg.d.layout.sizing.width = Fixed(Sc(1400));
  dlg.d.layout.sizing.height = Fixed(Sc(1170));
  dlg.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  dlg.d.floating.attachPoints.element = CLAY_ATTACH_POINT_CENTER_TOP;
  dlg.d.floating.attachPoints.parent = CLAY_ATTACH_POINT_CENTER_TOP;
  dlg.d.floating.offset.y = Sc(15);
  dlg.d.floating.zIndex = 61;
  static UiCustom frame = {0.0f, DrawScFrame, const_cast<UiColor *>(&kScFillSolid)};
  dlg.d.custom.customData = &frame;
  Open(dlg);

  Decl title = Floating(NULL, 0, 0, Sc(35), 62, CLAY_ATTACH_POINT_CENTER_TOP, CLAY_ATTACH_POINT_CENTER_TOP);
  Open(title);
  Text(Keep(Upper(PortText("Settings"))), UI_FACE_WIDE, Fs(24), 0.04f, kScInkHi);
  Close();

  // the tabs (StandardBattlenetVerticalTabButton)
  for (int t = 0; t < kTabCount; ++t) {
    Decl b = Floating("settab", t, Sc(35), Sc(75 + t * 75 + 12), 62, CLAY_ATTACH_POINT_LEFT_TOP,
                      CLAY_ATTACH_POINT_LEFT_TOP);
    const bool bHere = t == g_nTab;
    const bool bHot = Over(b.d.id);
    if (Clicked(b.d.id) && !bHere) {
      g_nTab = t;
      g_nDropOpen = -1;
    }
    b.d.layout.sizing.width = Fixed(Sc(341));
    b.d.layout.sizing.height = Fixed(Sc(50));
    b.d.layout.padding = Pad(Fs(28), Fs(28), 0, 0);
    b.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    b.d.custom.customData = Custom(DrawScRaised, bHere ? kScTabOn : bHot ? kScTabHi : kScTab);
    Open(b);
    Text(PortText(kTabs[t].pszLabel), UI_FACE_WIDE, Fs(18), 0.02f, bHere || bHot ? kScInkHi : kScInkButton,
         KeyAt(PortText(kTabs[t].pszLabel), kTabs[t].cKey));
    Close();
  }

  // the well, the tab's settings in two columns, the break where the first column passes half
  Decl well = Floating("setwell", 0, Sc(386), Sc(78), 62, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  well.d.layout.sizing.width = Fixed(Sc(975));
  well.d.layout.sizing.height = Fixed(Sc(975));
  well.d.backgroundColor = Col(kScWell);
  well.d.border = Border(kScWellRim, Fs(2), Fs(2), Fs(2), Fs(2));
  Open(well);
  Decl head = Floating(NULL, 0, Sc(30), Sc(22), 63, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  Open(head);
  Text(PortText(kTabs[g_nTab].pszLabel), UI_FACE_WIDE, Fs(22), 0.0f, kScInkHead);
  Close();
  Decl restore = Floating("setrestore", 0, -Sc(16), Sc(15), 63, CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_TOP);
  restore.d.layout.sizing.width = Fixed(Sc(340));
  restore.d.layout.sizing.height = Fixed(Sc(42));
  if (ScMini(restore, PortText("Restore Defaults"), 'R'))
    g_nConfirm = CONFIRM_RESTORE;

  // the tab's rows, those not available yet last under their heading (the HUD tab lays out its own)
  const bool bHudTab = strcmp(kTabs[g_nTab].pszId, "hud") == 0;
  std::vector<int> rows, later;
  int nTotal = 0;
  for (int i = 0; i < kSettingCount; ++i)
    if (strcmp(kSettings[i].pszTab, kTabs[g_nTab].pszId) == 0) {
      (NotYet(kSettings[i]) && !bHudTab ? later : rows).push_back(i);
      nTotal += SettingHeight(kSettings[i]);
    }
  if (!later.empty()) {
    rows.push_back(kSettingCount - 1);
    nTotal += SettingHeight(kSettings[kSettingCount - 1]);
    rows.insert(rows.end(), later.begin(), later.end());
  }
  Decl cols = Floating(NULL, 0, 0, Sc(76), 63, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  cols.d.layout.sizing.width = Fixed(Sc(975));
  cols.d.layout.sizing.height = Fixed(Sc(975 - 77));
  cols.d.layout.padding = Pad(Fs(30), Fs(30), 0, Fs(30));
  cols.d.layout.childGap = Gap(Fs(40));
  cols.d.floating.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH;
  Open(cols);
  int nAcc = 0;
  size_t r = 0;
  std::vector<bool> leftColumn(kSettingCount, true);
  // the HUD tab lays its own out: the designs, and the chosen one's settings beside them
  if (bHudTab) {
    for (int i : rows)
      leftColumn[i] = false;
    HudTab(rows, SettingGroup);
    r = rows.size();
  }
  for (int c = 0; c < 2 && !bHudTab; ++c) {
    Decl col;
    col.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    col.d.layout.sizing.width = Grow();
    Open(col);
    for (; r < rows.size(); ++r) {
      if (c == 0 && nAcc >= nTotal / 2)
        break;
      // a heading goes with its first row: to the next column when that row would start it
      if (c == 0 && kSettings[rows[r]].control == CTL_HEAD && nAcc + SettingHeight(kSettings[rows[r]]) >= nTotal / 2)
        break;
      nAcc += SettingHeight(kSettings[rows[r]]);
      leftColumn[rows[r]] = c == 0;
      SettingGroup(rows[r]);
    }
    Close();
  }
  Close();
  Close();

  // SC2's pair under the well: Accept in gold, Cancel beside it
  Decl accept = Floating("setaccept", 0, Sc(382), Sc(1064), 62, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  accept.d.layout.sizing.width = Fixed(Sc(315));
  accept.d.layout.sizing.height = Fixed(Sc(52));
  const bool bAccept = ScButton(accept, PortText("Accept"), 'E', true);
  Decl cancel = Floating("setcancel", 0, Sc(703), Sc(1064), 62, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  cancel.d.layout.sizing.width = Fixed(Sc(315));
  cancel.d.layout.sizing.height = Fixed(Sc(52));
  const bool bCancel = ScButton(cancel, PortText("Cancel"), 'C', false);
  // the version this is, and what Accept does, at the foot on the left
  Decl note = Floating(NULL, 0, Sc(40), Sc(1062), 62, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  note.d.layout.sizing.width = Fixed(Sc(330));
  note.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  note.d.layout.childGap = Gap(Fs(6));
  Open(note);
  Text(Keep(NotesVersionLine()), UI_FACE_TEXT, Fs(15), 0.0f, kScInkMenu);
  Text(PortText("Changes apply when you accept them."), UI_FACE_TEXT, Fs(15), 0.0f, kScInkOff);
  Close();
  Close();

  if (g_nTip >= 0 && g_nConfirm == CONFIRM_NONE && g_nDropOpen < 0)
    Tip(g_nTip, !leftColumn[g_nTip]);

  // a click outside an open pulldown closes it
  if (g_nDropOpen >= 0 && g_bReleased && !Over(Id("setdroplist")) && !Over(Id("setdrop", g_nDropOpen)))
    g_nDropOpen = -1;

  if (g_nConfirm == CONFIRM_NONE) {
    if (bAccept)
      SettingsAcceptAsk();
    else if (bCancel)
      SettingsClose();
  }
}

// The dialog's keys: the tabs' letters, R, E and C, and Escape as Cancel.
// A hotkey's new key, while one waits for it: the key pressed with the modifiers held, if it
// is one 1.17 leaves free and the other hotkey does not have; Escape leaves it as it was.
void KeyCapture(void)
{
  for (int k : g_keys) {
    if (k == VK_ESCAPE) {
      g_nKeyCapture = -1;
      return;
    }
    if (k == VK_SHIFT || k == VK_CONTROL || k == VK_MENU || (k >= VK_LSHIFT && k <= VK_RMENU))
      continue;
    const unsigned nKey = PortHotkeyHeld(static_cast<unsigned>(k));
    g_nKeyNoteRow = g_nKeyCapture;
    if (!PortHotkeyFree(nKey)) {
      g_keyNote = PortText("The original uses that key. F5 to F9, F11, F12 and Tab are free.");
      continue;
    }
    int nOther = -1;
    for (int r = 0; r < kSettingCount; ++r)
      if (r != g_nKeyCapture &&
          HotkeyOf(kSettings[r].bind) >= 0 &&
          static_cast<unsigned>(lroundf(g_set[r].f)) == nKey)
        nOther = r;
    if (nOther >= 0) {
      char szKey[48];
      PortHotkeyName(nKey, szKey, sizeof szKey);
      g_keyNote = PortTextF("{0} is {1}\xE2\x80\x99s key.", {szKey, PortText(kSettings[nOther].pszLabel)});
      continue;
    }
    g_set[g_nKeyCapture].f = static_cast<float>(nKey);
    g_keyNote.clear();
    g_nKeyCapture = -1;
    return;
  }
}

// The folder Browse asked for, once SDL's dialog gives it: the install at or above it, or a
// note saying there is none.
void SDLCALL FolderPicked(void *, const char *const *ppList, int)
{
  std::lock_guard<std::mutex> hold(g_pickLock);
  if (ppList != NULL && ppList[0] != NULL) {
    g_picked = ppList[0];
    g_bPicked = true;
  }
}

// Browse on a path row: SDL's folder dialog, starting at the row's folder.
void FolderBrowse(Bind bind)
{
  static char s_szStart[MAX_PATH];
  g_browseBind = bind;
  const std::string &start = bind == BIND_MODS_FOLDER ? g_modsPick : bind == BIND_CLASSIC ? g_classicPick : g_remasteredPick;
  SDL_strlcpy(s_szStart, start.c_str(), sizeof s_szStart);
  SDL_ShowOpenFolderDialog(FolderPicked, NULL, static_cast<SDL_Window *>(PortPresentWindow()),
                           s_szStart[0] != 0 ? s_szStart : NULL, false);
}

void BrowsePicked(void)
{
  std::string picked;
  {
    std::lock_guard<std::mutex> hold(g_pickLock);
    if (!g_bPicked)
      return;
    g_bPicked = false;
    picked = g_picked;
  }
  if (g_browseBind == BIND_MODS_FOLDER) {
    g_modsPick = picked;
    return;
  }
  // a classic StarCraft folder, which Game files then reads from
  if (g_browseBind == BIND_CLASSIC) {
    if (PortInstallClassicCheck(picked.c_str())) {
      g_classicPick = picked;
      g_classicNote.clear();
      g_set[RowOf(BIND_GAME_FILES)].f = 1.0f;
    } else {
      g_classicNote = PortText("No classic StarCraft files there: its folder holds StarDat.mpq and BrooDat.mpq.");
    }
    return;
  }
  char szFolder[MAX_PATH];
  if (PortCascCheck(picked.c_str(), szFolder, sizeof szFolder)) {
    g_remasteredPick = szFolder;
    g_remasteredNote.clear();
  } else {
    g_remasteredNote = PortText("No StarCraft: Remastered install there: its folder holds .build.info.");
  }
}

void SettingsKeys(void)
{
  if (g_nKeyCapture >= 0) {
    KeyCapture();
    return;
  }
  if (g_nConfirm != CONFIRM_NONE)
    return;  // Confirm reads its own
  if (ThemePickerTyping())
    return;  // the colour picker's entry has the keys
  if (g_nDropOpen >= 0) {
    if (Key(VK_ESCAPE))
      g_nDropOpen = -1;
    return;
  }
  if (Key(VK_ESCAPE) || Key('C')) {
    SettingsClose();
    return;
  }
  if (Key('E')) {
    SettingsAcceptAsk();
    return;
  }
  if (Key('R')) {
    g_nConfirm = CONFIRM_RESTORE;
    return;
  }
  for (int t = 0; t < kTabCount; ++t)
    if (Key(kTabs[t].cKey))
      g_nTab = t;
}

// Over a game, once a pass of the game loop: the Accept the dialog asked for, made out of the
// frame, as the main menu's is made in its loop.
void SettingsGameStep(void)
{
  if (!g_bAcceptAsked)
    return;
  g_bAcceptAsked = false;
  if (g_bSettings)
    SettingsAccept();
}

bool SettingsInGame(void)
{
  return g_bSettingsGame;
}

// ---------------------------------------------------------------------------
// The live channel's settings (Debug Tools): the rows the dialog changes by a value,
// read as the dialog reads them, and one changed at once, written, applied and saved as
// Accept writes it. The display, folders, hotkeys and the theme stay with the dialog.

bool LiveRow(const Setting &s)
{
  if (s.bind == BIND_DISPLAY || s.bind == BIND_RESOLUTION)
    return false;
  return s.control == CTL_TOGGLE || s.control == CTL_SLIDER || s.control == CTL_STEPS || s.control == CTL_CHOICE;
}

std::string LiveText(const char *psz)
{
  return PortJsonQuote(psz != NULL ? psz : "");
}

std::string LiveRowJson(int i)
{
  const Setting &s = kSettings[i];
  const SetValue v = ReadSetting(s);
  static const char *const kKinds[] = {"toggle", "slider", "steps", "choice"};
  std::string words = "[";
  const std::vector<std::string> w = Words(s.pszWords);
  for (size_t k = 0; k < w.size(); ++k)
    words += (k ? "," : "") + PortJsonQuote(w[k]);
  words += "]";
  const char *pszNeeds = NeedsOf(s);
  char sz[96];
  snprintf(sz, sizeof sz, "\"value\":%.4g,\"on\":%s,\"default\":%.4g,", v.f, v.bOn ? "true" : "false", s.fDefault);
  return "{\"tab\":" + LiveText(s.pszTab) + ",\"name\":" + LiveText(s.pszLabel) + ",\"kind\":" +
         LiveText(kKinds[s.control]) + ",\"words\":" + words + "," + sz + "\"switch\":" + (s.bToggle ? "true" : "false") +
         ",\"applies\":" + LiveText(s.pszApplies) + ",\"from\":" + LiveText(s.pszFrom) + ",\"live\":" +
         (Editable(s) && pszNeeds == NULL ? "true" : "false") + ",\"needs\":" + LiveText(pszNeeds) +
         ",\"note\":" + LiveText(s.pszNote) + "}";
}

}  // namespace Front

using namespace Front;

std::string PortSettingsLiveJson(void)
{
  std::string s = "\"settings\":[";
  bool bFirst = true;
  for (int i = 0; i < kSettingCount; ++i) {
    if (!LiveRow(kSettings[i]))
      continue;
    s += (bFirst ? "" : ",") + LiveRowJson(i);
    bFirst = false;
  }
  return s + "]";
}

// One setting's value and the word it shows (a choice's or steps' word, a toggle's on or off,
// a slider's per cent); empty when there is no such row.
std::string PortSettingLiveWord(const char *pszName, float *pf)
{
  for (int i = 0; i < kSettingCount; ++i) {
    const Setting &s = kSettings[i];
    if (!LiveRow(s) || _stricmp(s.pszLabel, pszName) != 0)
      continue;
    const SetValue v = ReadSetting(s);
    *pf = v.f;
    if (s.control == CTL_TOGGLE)
      return v.f >= 0.5f ? "on" : "off";
    if (s.control == CTL_SLIDER) {
      char sz[16];
      snprintf(sz, sizeof sz, "%d %%", static_cast<int>(lroundf(v.f * 100.0f)));
      return sz;
    }
    const std::vector<std::string> w = Words(s.pszWords);
    const long n = lroundf(v.f);
    return n >= 0 && n < static_cast<LONG>(w.size()) ? w[n] : std::string();
  }
  *pf = 0.0f;
  return std::string();
}

// One setting by its label, set to fValue (a toggle 0 or 1, a slider 0 to 1, steps and choices
// their index) and, for a slider with a check box, nOn 0 or 1 (-1 leaves it). False with the
// reason in *pAnswer; else *pAnswer is the row as it now is and whether it waits on a restart.
bool PortSettingLiveSet(const std::string &name, double fValue, int nOn, std::string *pAnswer)
{
  int i = 0;
  while (i < kSettingCount && (!LiveRow(kSettings[i]) || _stricmp(kSettings[i].pszLabel, name.c_str()) != 0))
    ++i;
  if (i == kSettingCount) {
    *pAnswer = "no setting named " + name + "; settings names them";
    return false;
  }
  const Setting &s = kSettings[i];
  if (!Editable(s) || NeedsOf(s) != NULL) {
    *pAnswer = name + " cannot be changed yet" + (NeedsOf(s) != NULL ? std::string(": it waits on ") + NeedsOf(s) : "");
    return false;
  }
  SetValue v = ReadSetting(s);
  float f = static_cast<float>(fValue);
  if (s.control == CTL_TOGGLE)
    f = f >= 0.5f ? 1.0f : 0.0f;
  else if (s.control == CTL_SLIDER)
    f = Clamp01(f);
  else {
    const int nMost = static_cast<int>(Words(s.pszWords).size()) - 1;
    const long n = lroundf(f);
    f = static_cast<float>(n < 0 ? 0 : n > nMost ? nMost : n);
  }
  v.f = f;
  if (nOn >= 0)
    v.bOn = nOn != 0;
  PortSettingsHold();
  const bool bRestart = WriteSetting(s, v);
  snd_set_options(&gOptions);
  ini_file_close(0);
  exit_video_opts(0);
  PortSettingsRelease();
  // an open dialog shows it as it now is
  if (g_bSettings)
    g_set[i] = g_setOpened[i] = ReadSetting(s);
  *pAnswer = "\"setting\":" + LiveRowJson(i) + ",\"restart\":" +
             (bRestart || strcmp(s.pszApplies, "restart") == 0 ? "true" : "false");
  return true;
}
