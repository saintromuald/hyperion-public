// port_hud.cpp -- the HUD design a game shows (port_hud.h).
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <SDL3/SDL.h>
#include "port_hud.h"
#include "port_hudfile.h"
#include "port_hudplay.h"
#include "port_hudstate.h"
#include "port_hudstats.h"  // PortHudStatsSet, PortHudStatsTurn, PortHudStatsTouched
#include "../front/port_text.h"
#include "../platform/port_paths.h"
#include "../platform/port_settings.h"
#include "../platform/port_util.h"
#include "../platform/port_json.h"
#include "../data/port_zip.h"
#include "../render/port_console.h"  // PortConsoleHudOn
#include "../render/port_present.h"  // PortPresentGpu
#include "../data/port_soundshot.h"  // PortSoundOneShot
#include "../../gen/globals117.h"    // gnSfxVolume, gPlayback
#if !defined(__EMSCRIPTEN__)
// stb_vorbis's declarations; its code is built with the videos' (port_video.cpp)
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"
#endif

#ifdef OC_DLL_MODE
#define sgSndVolTbl_g (reinterpret_cast<const LONG *>(OC_VA(0x4EFC00u)))
#else
extern const LONG sgSndVolTbl_g[100];  // the game's volumes, in hundredths of a decibel (snd.cpp)
#endif

namespace {

// What a design is made from: its files and the player's picks for it.
struct Want {
  std::string layout, style;
  std::string package;  // the .hud a player's design was unpacked from, or ""
  HudPicks picks;
  bool bSettings = false;  // chosen in Settings, which holds its option picks
  std::string options;     // the settings' names of its option picks, less the option's id
  std::string shared;      // in a replay layout, the game layout's names: an option both have follows the game's
  bool bReplay = false;    // read in a replay, where a layout's replay layout is worn in its place
  std::string named;       // then the layout that names it, else ""
  std::vector<bool> hidden = std::vector<bool>(HUD_SLOTS, false);  // the slots left out
  bool bSound = true;
  bool bTips = true;      // the style's tooltips, else 1.17's bubbles
  std::string selection;  // the selection's boxes and the tooltip's lit parts: "", "light" or #rrggbb
  double mapRaise = 0;    // the map raised over the HUD, in units, for every design (PortHudMapRaise)
  std::string Key() const {
    std::string k = layout + "|" + style + "|" + picks.light + (bSound ? "|s" : "|q") + (bTips ? "|t" : "|b") + "|" +
                    selection;
    for (bool b : hidden)
      k += b ? "1" : "0";
    for (const auto &p : picks.options)
      k += "|" + p.first + "=" + p.second;
    return k;
  }
};

struct Active {
  std::string key;  // the Want loaded, "" for none
  bool bOk = false;
  HudDesign design;
  HudPlayer player;
  HudEnv env;
  std::vector<std::string> errors, warnings;
  unsigned int nSerial = 0;  // the last event fired
  std::vector<bool> hovered;  // the panels the pointer is over, of those that ask (PortHudPointer)
  std::vector<bool> hidden;   // the slots the player left out
  bool bSound = true;
  bool bTips = true;          // the style's tooltips (Settings, "tooltips")
  bool bAccent = false;       // the selection's boxes in a colour of the player's
  float accent[3] = {0, 0, 0};
  std::map<std::string, std::shared_ptr<const std::vector<float>>> sounds;  // by path; NULL: does not play
  Uint64 nLastMs = 0;
  int nWidth = 0, nHeight = 0;
  unsigned int nLoads = 0;  // the loads so far (PortHudLoads)
  // live reload: the design's files and their last writes when it loaded, looked at once a
  // second, and how many files its own folder held
  std::vector<std::pair<std::string, Uint64>> watched;
  std::string folder;  // the layout's
  size_t nOwnFiles = 0, nPartsFiles = 0;
  Uint64 nWatchedMs = 0;
  // a layout's "start": the serial of the game's game.start, and the layout, game or replay and
  // game it was last put on for
  unsigned int nGameSerial = 0;
  std::string startKey;
  unsigned int nStartGame = 0;
};
Active *g_pHud;
// what Wanted last gave, asked again once a second or at once after PortHudReload, so a frame
// neither reads the settings nor looks for files each time
Want g_want;
Uint64 g_nWantMs;
bool g_bWant;
std::vector<HudHit> g_hits;  // the layers last drawn (PortHudHits)
double g_hitsLw;

std::string Setting(const char *pszName, const char *pszDefault) {
  char sz[512];
  return PortSettingString("hud", pszName, sz, sizeof sz) ? std::string(sz) : std::string(pszDefault);
}

std::string Packaged(const std::string &rel) { return PortPackagedFile(rel.c_str()); }

// The player's designs' folder: huds\ in the player data folder.
std::string UserFolder(void) { return std::string(PortDataFolder()) + "huds\\"; }

bool Exists(const std::string &path) {
  FILE *f = fopen(path.c_str(), "rb");
  if (f != NULL)
    fclose(f);
  return f != NULL;
}

Uint64 LastWrite(const std::string &path);
std::string FolderOf(const std::string &path);

// The replay layout a layout names in "replay", beside it, or "" for none: worn in a
// replay in its place. Read again when the layout file is.
std::string ReplayLayout(const std::string &layout) {
  static std::map<std::string, std::pair<Uint64, std::string>> s_read;  // by layout: its write, its replay's
  const Uint64 nWrite = LastWrite(layout);
  auto it = s_read.find(layout);
  if (it != s_read.end() && it->second.first == nWrite)
    return it->second.second;
  std::string text, replay;
  PortJson j;
  if (PortReadFile(layout, &text) && PortJsonParse(text.c_str(), &j) && j["mode"].Str() != "replay") {
    // as the checker has it: a .json beside the layout (port_hudfile.cpp)
    const std::string name = j["replay"].Str();
    if (name.size() > 5 && SDL_strcasecmp(name.c_str() + name.size() - 5, ".json") == 0 &&
        name.find_first_of("/\\:") == std::string::npos && name.compare(0, 2, "..") != 0 &&
        Exists(FolderOf(layout) + name))
      replay = FolderOf(layout) + name;
  }
  s_read[layout] = {nWrite, replay};
  return replay;
}

// In a replay, the layout swapped for its replay layout, its option picks read under its own
// names in Settings, and an option the game layout has too (Width) as the game layout's.
void ForReplay(Want *w, const std::string &design) {
  w->bReplay = gPlayback != 0;
  if (!w->bReplay)
    return;
  const std::string replay = ReplayLayout(w->layout);
  if (replay.empty())
    return;
  w->named = w->layout;
  w->layout = replay;
  w->shared = w->options;
  w->options = design + ".replay.option.";
}

// The design Settings or HYPERION_HUD name, with the player's picks; empty for Classic.
Want Wanted(void) {
  Want w;
  const double fRaise = floor(atof(Setting("mapRaise", "0").c_str()));
  w.mapRaise = fRaise > 0 ? (fRaise < 200 ? fRaise : 200) : 0;
  const char *pszEnv = SDL_getenv("HYPERION_HUD");
  if (pszEnv != NULL && pszEnv[0] != 0) {
    const std::string s = pszEnv;
    const size_t semi = s.find(';');
    w.layout = s.substr(0, semi);
    if (semi != std::string::npos)
      w.style = s.substr(semi + 1);
    ForReplay(&w, std::string());
    return w;
  }
  const std::string design = Setting("design", "classic");
  if (PortHudDesignHeld(design))
    return w;
  if (!PortHudDesignFiles(design, Setting("layout", "span"), Setting("size", "full"), Setting("material", "gunmetal"),
                          &w.layout, &w.style, &w.package)) {
    w.layout.clear();
    w.style.clear();
    return w;
  }
  w.picks.light = Setting((design + ".light").c_str(), "");
  w.bSettings = true;
  w.options = design + ".option.";
  ForReplay(&w, design);
  // the buttons left out; never the menu, diplomacy or messaging, which a game needs, nor a
  // replay's play, slower and faster
  for (int s = 0; s < HUD_SLOTS; ++s)
    w.hidden[s] = s != HUD_SLOT_MENU && s != HUD_SLOT_DIPLOMACY && s != HUD_SLOT_MESSAGING && s != HUD_SLOT_PLAY &&
                  s != HUD_SLOT_SLOWER && s != HUD_SLOT_FASTER &&
                  Setting((design + ".hide." + HudSlotName(s)).c_str(), "") == "1";
  w.bSound = Setting("sound", "off") == "on";  // off unless the player turns it on (2026-10-27)
  w.bTips = Setting("tooltips", "on") != "off";
  w.selection = Setting((design + ".selection").c_str(), "");
  return w;
}

// The want as of the last second, or of a replay's start or end.
const Want &WantedNow(void) {
  const Uint64 nNow = SDL_GetTicks();
  if (!g_bWant || nNow - g_nWantMs >= 1000 || g_want.bReplay != (gPlayback != 0)) {
    g_want = Wanted();
    g_nWantMs = nNow;
    g_bWant = true;
  }
  return g_want;
}

Uint64 Stamp(const FILETIME &t) { return (static_cast<Uint64>(t.dwHighDateTime) << 32) | t.dwLowDateTime; }

// A file's last write, 0 for none.
Uint64 LastWrite(const std::string &path) {
  WIN32_FILE_ATTRIBUTE_DATA a;
  return GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &a) ? Stamp(a.ftLastWriteTime) : 0;
}

// The folder a file is in, with its slash.
std::string FolderOf(const std::string &path) {
  const size_t nSlash = path.find_last_of("/\\");
  return nSlash == std::string::npos ? std::string() : path.substr(0, nSlash + 1);
}

// The files in a folder matching a pattern, with their last writes, by name.
std::vector<std::pair<std::string, Uint64>> FilesIn(const std::string &folder, const char *pszPattern) {
  std::vector<std::pair<std::string, Uint64>> v;
  WIN32_FIND_DATAA fd;
  const HANDLE hFind = FindFirstFileA((folder + pszPattern).c_str(), &fd);
  if (hFind == INVALID_HANDLE_VALUE)
    return v;
  do
    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && v.size() < 256)
      v.push_back({folder + fd.cFileName, Stamp(fd.ftLastWriteTime)});
  while (FindNextFileA(hFind, &fd));
  FindClose(hFind);
  std::sort(v.begin(), v.end());
  return v;
}

// A design's own files (images, sounds), and the parts files beside its layout.
std::vector<std::pair<std::string, Uint64>> OwnFiles(const std::string &files) { return FilesIn(files, "*"); }
std::vector<std::pair<std::string, Uint64>> PartsFiles(const std::string &folder) {
  return FilesIn(folder + "parts\\", "*.json");
}

// ---------------------------------------------------------------------------
// A player's .hud package: a zip of a design's folder, unpacked into a folder of its name in
// huds\.unpacked to be read as a folder is, again whenever the package is newer.

const size_t kPackageEntries = 512;
const size_t kPackageMost = 64u << 20;  // all its entries unpacked

std::string UnpackedFolder(const std::string &name) { return UserFolder() + ".unpacked\\" + name + "\\"; }

// An entry a design may hold: hud.json, style.json, parts/<name>.json, and files/<name> that is
// a PNG, a WebM or an Ogg by its first bytes; no other folder and no way out of its own.
bool PackageEntry(const std::string &name, const std::vector<uint8_t> &data, std::string *pRel, std::string *pWhy) {
  std::string n = name;
  for (char &c : n)
    c = c == '\\' ? '/' : c;
  const size_t nSlash = n.find('/');
  const std::string leaf = nSlash == std::string::npos ? n : n.substr(nSlash + 1);
  if (leaf.empty() || leaf[0] == '.' || leaf.find_first_of("/:") != std::string::npos) {
    *pWhy = "it is not a file of the design's own folders";
    return false;
  }
  if (n == "hud.json" || n == "style.json") {
    *pRel = n;
    return true;
  }
  // a second layout beside hud.json, the replay layout it names
  if (nSlash == std::string::npos && n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0) {
    *pRel = n;
    return true;
  }
  if (n.compare(0, 6, "parts/") == 0 && leaf.size() > 5 && leaf.compare(leaf.size() - 5, 5, ".json") == 0) {
    *pRel = "parts\\" + leaf;
    return true;
  }
  if (n.compare(0, 6, "files/") == 0) {
    const bool bPng = data.size() >= 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G';
    const bool bWebm = data.size() >= 4 && data[0] == 0x1A && data[1] == 0x45 && data[2] == 0xDF && data[3] == 0xA3;
    const bool bOgg = data.size() >= 4 && memcmp(&data[0], "OggS", 4) == 0;
    if (!bPng && !bWebm && !bOgg) {
      *pWhy = "it is not a PNG, a WebM or an Ogg";
      return false;
    }
    *pRel = "files\\" + leaf;
    return true;
  }
  *pWhy = "a package holds hud.json, a replay layout, style.json, parts/ and files/";
  return false;
}

// The package unpacked, unless it was already since it was last written. Its entries that are
// not a design's are left out and told in pWarnings.
bool Unpack(const std::string &package, std::vector<std::string> *pWarnings) {
  const std::string leaf = package.substr(FolderOf(package).size());
  const std::string folder = UnpackedFolder(leaf);
  const std::string stamp = std::to_string(LastWrite(package));
  if (PortReadText(folder + "package.stamp") == stamp)
    return true;
  std::vector<uint8_t> zip;
  std::vector<PortZipEntry> entries;
  if (!PortReadFile(package, &zip) || !PortZipDirectory(zip, &entries)) {
    pWarnings->push_back("The package " + package + " is not a zip.");
    return false;
  }
  CreateDirectoryA((UserFolder() + ".unpacked").c_str(), NULL);
  CreateDirectoryA(folder.c_str(), NULL);
  CreateDirectoryA((folder + "files").c_str(), NULL);
  CreateDirectoryA((folder + "parts").c_str(), NULL);
  // what an earlier unpack left goes, so a file taken out of the package is gone
  for (const char *pszSub : {"", "files\\", "parts\\"})
    for (const auto &f : FilesIn(folder + pszSub, "*"))
      DeleteFileA(f.first.c_str());
  size_t nTotal = 0, nDone = 0;
  for (const PortZipEntry &e : entries) {
    if (!e.name.empty() && (e.name.back() == '/' || e.name.back() == '\\'))
      continue;  // a folder
    std::vector<uint8_t> data;
    std::string rel, why;
    if (nDone >= kPackageEntries || nTotal + e.cbSize > kPackageMost) {
      pWarnings->push_back("The package " + leaf + " holds more than a design may; the rest is left out.");
      break;
    }
    if (!PortZipRead(zip, e, &data)) {
      pWarnings->push_back("The package " + leaf + ": " + e.name + " does not unpack.");
      continue;
    }
    if (!PackageEntry(e.name, data, &rel, &why)) {
      pWarnings->push_back("The package " + leaf + ": " + e.name + " is left out: " + why + ".");
      continue;
    }
    FILE *f = fopen((folder + rel).c_str(), "wb");
    if (f == NULL || (!data.empty() && fwrite(&data[0], 1, data.size(), f) != data.size()))
      pWarnings->push_back("The package " + leaf + ": " + e.name + " does not write.");
    if (f != NULL)
      fclose(f);
    nTotal += data.size();
    ++nDone;
  }
  FILE *f = fopen((folder + "package.stamp").c_str(), "wb");
  if (f != NULL) {
    fputs(stamp.c_str(), f);
    fclose(f);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Live reload.

// The design's files as they are now: its layout (and in a replay the layout that names it), its
// style, its package, its parts files and its own folder's.
void Watch(Active *h, const Want &w) {
  h->watched.clear();
  for (const std::string &path : {w.layout, w.style, w.package, w.named})
    if (!path.empty())
      h->watched.push_back({path, LastWrite(path)});
  h->folder = FolderOf(w.layout);
  const auto parts = PartsFiles(h->folder), own = OwnFiles(h->design.files);
  h->watched.insert(h->watched.end(), parts.begin(), parts.end());
  h->watched.insert(h->watched.end(), own.begin(), own.end());
  h->nPartsFiles = parts.size();
  h->nOwnFiles = own.size();
  h->nWatchedMs = SDL_GetTicks();
}

// True, once a second at most, when a file of the design was saved since it loaded, or one
// came or went in its folders.
bool Changed(Active *h) {
  const Uint64 nNow = SDL_GetTicks();
  if (nNow - h->nWatchedMs < 1000)
    return false;
  h->nWatchedMs = nNow;
  for (const auto &f : h->watched)
    if (LastWrite(f.first) != f.second)
      return true;
  return OwnFiles(h->design.files).size() != h->nOwnFiles || PartsFiles(h->folder).size() != h->nPartsFiles;
}

// A sound of the design's, decoded once: stereo at the mixer's rate.
std::shared_ptr<const std::vector<float>> Sound(Active *h, const std::string &path) {
  auto it = h->sounds.find(path);
  if (it != h->sounds.end())
    return it->second;
  std::shared_ptr<const std::vector<float>> &out = h->sounds[path];
#if !defined(__EMSCRIPTEN__)
  std::string bytes;
  int nChannels = 0, nRate = 0;
  short *pSamples = NULL;
  const int nFrames = PortReadFile(path, &bytes) && bytes.size() < (1u << 24)
                          ? stb_vorbis_decode_memory(reinterpret_cast<const unsigned char *>(bytes.data()),
                                                     static_cast<int>(bytes.size()), &nChannels, &nRate, &pSamples)
                          : -1;
  if (nFrames > 0 && nChannels > 0 && nRate > 0 && pSamples != NULL) {
    // to the mixer's rate, a straight line between the file's samples
    const int nOut = static_cast<int>(static_cast<long long>(nFrames) * PortSoundRate() / nRate);
    auto pcm = std::make_shared<std::vector<float>>(static_cast<size_t>(nOut) * 2);
    for (int i = 0; i < nOut; ++i) {
      const double t = static_cast<double>(i) * nRate / PortSoundRate();
      const int k = static_cast<int>(t), k1 = k + 1 < nFrames ? k + 1 : k;
      const double f = t - k;
      for (int ch = 0; ch < 2; ++ch) {
        const int c = ch < nChannels ? ch : 0;
        const double a = pSamples[k * nChannels + c], b = pSamples[k1 * nChannels + c];
        (*pcm)[i * 2 + ch] = static_cast<float>((a + (b - a) * f) / 32768.0);
      }
    }
    out = pcm;
  }
  free(pSamples);
#endif
  return out;
}

// A cue of a timeline: the design's audio asset, at its volume and the game's sound volume.
void PlayCue(Active *h, const std::string &asset, double volume) {
  if (!h->bSound || gnSfxVolume <= 0 || volume <= 0.0)
    return;
  for (const HudAsset &a : h->design.assets) {
    if (a.name != asset || a.audio.empty())
      continue;
    const int nVol = gnSfxVolume > 100 ? 99 : gnSfxVolume * 99 / 100;
    const double game = pow(10.0, sgSndVolTbl_g[nVol] / 2000.0);
    PortSoundOneShot(Sound(h, h->design.files + a.audio), static_cast<float>(volume * game));
    return;
  }
}

// The viewer's state a layout starts on (its "start"), when it is put on for a game or a
// replay: at the start of each, and when the player picks it during one; not again while the same
// layout stays on (its style, light or options changed, a live reload, the intro again). A stats
// view or page set at the game's start by other means (the live channel) is left as it was set.
void ApplyStart(Active *h, const Want &w) {
  if (h->nGameSerial == 0)
    return;
  const std::string key = w.layout + (w.bReplay ? "|replay|" : "|game|") + std::to_string(h->nGameSerial);
  if (key == h->startKey)
    return;
  h->startKey = key;
  const bool bLinked = h->nStartGame != h->nGameSerial && PortHudStatsTouched();
  h->nStartGame = h->nGameSerial;
  PortHudState st;
  PortHudGet(&st);
  const char *pszWhy = NULL;
  for (const auto &s : h->design.start) {
    const bool bOn = s.second != 0;
    if (s.first == "stats.side" && !bLinked)
      PortHudStatsSet(HUD_STATS_SIDE, bOn);
    else if (s.first == "stats.popup" && !bLinked)
      PortHudStatsSet(HUD_STATS_POPUP, bOn);
    else if (s.first == "stats.page" && !bLinked)
      PortHudStatsTurn(s.second);
    else if (s.first == "replay.tv" && st.bReplay && st.bTv != bOn)
      PortHudPressButton(PORT_HUD_BTN_TV, &pszWhy);
    else if (s.first == "replay.follow" && st.bFollow != bOn)
      PortHudPressButton(PORT_HUD_BTN_FOLLOW, &pszWhy);
    else if (s.first == "minimap.terrain" && st.bTerrain != bOn)
      PortHudPressButton(PORT_HUD_BTN_TERRAIN, &pszWhy);
  }
}

void Load(Active *h, const Want &w) {
  h->key = w.Key();
  h->errors.clear();
  h->warnings.clear();
  if (!w.package.empty())
    Unpack(w.package, &h->warnings);
  // the option picks are Settings' when the design is chosen there, a replay layout's its own
  std::function<std::string(const std::string &)> option;
  if (w.bSettings)
    option = [&](const std::string &id) {
      const std::string v = Setting((w.options + id).c_str(), "");
      return v.empty() && !w.shared.empty() ? Setting((w.shared + id).c_str(), "") : v;
    };
  // the tooltip's lit parts take the selection boxes' colour when the player picked one
  const std::string tipAccent = w.selection == "light" ? w.picks.light : w.selection;
  h->bOk = PortHudRead(w.layout, w.style, w.picks.light, option, &h->design, &h->errors, &h->warnings, tipAccent);
  ++h->nLoads;
  Watch(h, w);
  FILE *f = fopen(PortLogFile("hyperion_hud.log"), "w");
  if (f != NULL) {
    fprintf(f, "HUD %s%s%s: %s\n", w.layout.c_str(), w.style.empty() ? "" : " with ", w.style.c_str(),
            h->bOk ? "plays" : "refused");
    for (const std::string &s : h->errors)
      fprintf(f, "error: %s\n", s.c_str());
    for (const std::string &s : h->warnings)
      fprintf(f, "warning: %s\n", s.c_str());
    fclose(f);
  }
  if (!h->bOk)
    return;
  h->player.Load(&h->design);
  h->hovered.clear();
  h->hidden = w.hidden;
  h->bSound = w.bSound;
  h->bTips = w.bTips;
  // the selection's boxes: the design's light (the player's pick of it, else its lamps' colour,
  // else its accent), or a colour
  float c[4];
  const auto accent = h->design.palette.find("accent");
  const std::string light = !w.picks.light.empty()                 ? w.picks.light
                            : !h->design.lampColour.empty()        ? h->design.lampColour
                            : accent != h->design.palette.end() ? accent->second
                                                                : std::string();
  h->bAccent = !w.selection.empty() && HudParseColour(w.selection == "light" ? light : w.selection, c);
  for (int i = 0; i < 3; ++i)
    h->accent[i] = h->bAccent ? c[i] : 0.0f;
  h->sounds.clear();
  h->nWidth = h->nHeight = 0;
  // the events from this game's start on, so its intro plays when the design loads late
  PortHudEvent events[256];
  const int n = PortHudEventsSince(0, events, 256);
  h->nSerial = 0;
  for (int i = 0; i < n; ++i)
    if (events[i].nKind == PORT_HUD_GAME_START)
      h->nSerial = events[i].nSerial - 1;
  h->nLastMs = SDL_GetTicks();
}

}  // namespace

bool PortHudChosen(void) { return !Wanted().layout.empty(); }

bool PortHudPlays(void) {
  if (!PortPresentGpu())
    return false;
  const Want &w = WantedNow();
  if (w.layout.empty())
    return false;
  if (g_pHud == NULL)
    g_pHud = new Active;
  Active *h = g_pHud;
  if (h->key != w.Key()) {
    Load(h, w);
    // the console loads before the game starts: its start, the next event, plays the intro
    PortHudEvent events[64];
    int n;
    while ((n = PortHudEventsSince(h->nSerial, events, 64)) > 0)
      h->nSerial = events[n - 1].nSerial;
  }
  return h->bOk;
}

bool PortHudDesignHeld(const std::string &id) { return id == "r9"; }

std::vector<PortHudDesignInfo> PortHudDesigns(void) {
  std::vector<PortHudDesignInfo> v;
  const char *const kBuiltIn[][3] = {
      {"classic", PortTextC("the HUD design", "Classic"), PortText("The classic console, by race.")},
      {"mech", PortText("Mech"),
       PortText("Dark metal plates with lit wells: across the bottom, or a column at either side.")},
      {"r9", "R9", PortText("A Terran console of plates and glass across the bottom.")}};
  for (const auto &b : kBuiltIn) {
    PortHudDesignInfo d;
    d.id = b[0];
    d.name = b[1];
    d.note = b[2];
    d.bBuiltIn = true;
    d.bHeld = PortHudDesignHeld(d.id);
    v.push_back(d);
  }
  // the player's: each folder in huds\ with a hud.json, by name
  std::vector<PortHudDesignInfo> mine;
  WIN32_FIND_DATAA fd;
  const std::string root = UserFolder();
  const HANDLE h = FindFirstFileA((root + "*").c_str(), &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (fd.cFileName[0] == '.')
        continue;
      // a folder with a hud.json, or a .hud package with one
      const std::string leaf = fd.cFileName;
      std::string text;
      if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        if (!PortReadFile(root + leaf + "\\hud.json", &text))
          continue;
      } else {
        std::vector<uint8_t> zip, data;
        std::vector<PortZipEntry> entries;
        if (leaf.size() < 5 || SDL_strcasecmp(leaf.c_str() + leaf.size() - 4, ".hud") != 0 ||
            !PortReadFile(root + leaf, &zip) || !PortZipDirectory(zip, &entries))
          continue;
        for (const PortZipEntry &e : entries)
          if (e.name == "hud.json" && e.cbSize < (1u << 20) && PortZipRead(zip, e, &data))
            text.assign(data.begin(), data.end());
        if (text.empty())
          continue;
      }
      PortHudDesignInfo d;
      d.id = std::string("user:") + fd.cFileName;
      PortJson j;
      const bool bRead = PortJsonParse(text.c_str(), &j);
      d.name = bRead && !j["name"].Str().empty() ? j["name"].Str() : leaf;
      d.note = bRead ? j["note"].Str() : std::string();
      mine.push_back(d);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
  std::sort(mine.begin(), mine.end(),
            [](const PortHudDesignInfo &a, const PortHudDesignInfo &b) { return SDL_strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
  v.insert(v.end(), mine.begin(), mine.end());
  return v;
}

bool PortHudDesignFiles(const std::string &id, const std::string &layout, const std::string &size,
                        const std::string &material, std::string *pLayout, std::string *pStyle, std::string *pPackage) {
  pStyle->clear();
  if (pPackage != NULL)
    pPackage->clear();
  if (id == "mech") {
    const std::string file = layout == "flank"         ? "mech-flank"
                             : layout == "flank-right" ? "mech-flank-right"
                             : size == "min"           ? "mech-span-min"
                                                       : "mech-span";
    *pLayout = Packaged("huds\\" + file + ".json");
    *pStyle = Packaged("huds\\styles\\mech-" +
                       (material == "graphite" || material == "blackout" ? material : std::string("gunmetal")) + ".json");
    return true;
  }
  if (id == "r9") {
    *pLayout = Packaged("huds\\terran-r9.json");
    *pStyle = Packaged("huds\\styles\\terran-r8.json");
    return true;
  }
  if (id.compare(0, 5, "user:") == 0 && id.size() > 5 && id.find_first_of("/\\", 5) == std::string::npos &&
      id.find("..") == std::string::npos) {
    const std::string leaf = id.substr(5);
    std::string folder = UserFolder() + leaf + "\\";
    // a package: read from where it is unpacked, which it is again when it is newer
    if (leaf.size() > 4 && SDL_strcasecmp(leaf.c_str() + leaf.size() - 4, ".hud") == 0) {
      std::vector<std::string> warnings;
      Unpack(UserFolder() + leaf, &warnings);
      folder = UnpackedFolder(leaf);
      if (pPackage != NULL)
        *pPackage = UserFolder() + leaf;
    }
    *pLayout = folder + "hud.json";
    if (Exists(folder + "style.json"))
      *pStyle = folder + "style.json";
    return Exists(*pLayout);
  }
  return false;
}

bool PortHudUnpack(const std::string &package, std::string *pFolder, std::vector<std::string> *pWarnings) {
  *pFolder = UnpackedFolder(package.substr(FolderOf(package).size()));
  return Unpack(package, pWarnings);
}

bool PortHudRead(const std::string &layout, const std::string &style, const std::string &light,
                 const std::function<std::string(const std::string &)> &option, HudDesign *pDesign,
                 std::vector<std::string> *pErrors, std::vector<std::string> *pWarnings,
                 const std::string &tipAccent) {
  *pDesign = HudDesign();
  std::string layoutText, styleText;
  if (!PortReadFile(layout, &layoutText)) {
    pErrors->push_back("The HUD file " + layout + " does not read.");
    return false;
  }
  if (!style.empty() && !PortReadFile(style, &styleText)) {
    pErrors->push_back("The HUD style " + style + " does not read.");
    return false;
  }
  const std::string folder = FolderOf(layout);
  std::vector<std::string> parts;
  for (const auto &f : PartsFiles(folder)) {
    parts.emplace_back();
    if (!PortReadFile(f.first, &parts.back()))
      pWarnings->push_back("The parts file " + f.first + " does not read.");
  }
  HudPicks picks;
  picks.light = light;
  picks.tipAccent = tipAccent;
  bool bOk = HudLoad(layoutText, styleText, picks, pDesign, pErrors, pWarnings, &parts);
  // the options are the file's, so they are known once it has read: read again with the picks
  // for them laid over, when there are any
  if (bOk && option) {
    for (const HudOption &o : pDesign->options) {
      const std::string v = option(o.id);
      if (!v.empty())
        picks.options.push_back({o.id, v});
    }
    if (!picks.options.empty()) {
      *pDesign = HudDesign();
      pErrors->clear();
      pWarnings->clear();
      bOk = HudLoad(layoutText, styleText, picks, pDesign, pErrors, pWarnings, &parts);
    }
  }
  // its own files are in the files folder beside its layout file, as a package's are
  pDesign->files = folder + "files/";
  return bOk;
}

bool HudGridOf(const PortJson &g, double w, double h, int nUnits, HudGrid *p) {
  *p = HudGrid();
  if (g.kind != PortJson::OBJECT || g["cell"].size() < 2)
    return false;
  const double cw = g["cell"].At(0).Num(), ch = g["cell"].At(1).Num();
  const std::string tabs = g["tabs"].Str();
  const bool bNone = tabs == "none", bRight = tabs == "right";
  const double tabW = bNone ? 0.0 : g["tabW"].Num(24.0);
  int lo = 8, hi = 8;
  if (g["cols"].size() >= 2) {
    lo = (std::max)(1, static_cast<int>(g["cols"].At(0).Num(8)));
    hi = (std::max)(lo, static_cast<int>(g["cols"].At(1).Num(8)));
  }
  if (cw <= 0 || ch <= 0)
    return false;
  p->rows = (std::max)(1, static_cast<int>(g["rows"].Num(3)));
  p->cols = (std::min)(hi, (std::max)(lo, static_cast<int>(floor((w - tabW - 4) / cw))));
  p->per = p->cols * p->rows;
  p->pages = (std::max)(1, (nUnits + p->per - 1) / p->per);
  // the box's padding, 4 by 6, and the tabs' gap to the cells, 4
  const double padX = 6, padY = 4, gap = 4, innerH = h - 2 * padY;
  const double areaX = padX + (bNone || bRight ? 0.0 : tabW + gap), areaW = w - 2 * padX - (bNone ? 0.0 : tabW + gap);
  const double k = (std::min)(1.0, (std::min)(areaW / (p->cols * cw), innerH / (p->rows * ch)));
  if (k <= 0)
    return false;
  p->cw = cw * k;
  p->ch = ch * k;
  // the cells beside the tabs, as StarCraft II's (the user, 2026-10-23): from the left, or up to
  // the tabs at the right
  p->x0 = bRight ? areaX + areaW - p->cols * p->cw : areaX;
  p->y0 = padY + (innerH - p->rows * p->ch) / 2;
  p->bTabs = !bNone && p->pages > 1;
  p->tabX = bRight ? w - padX - tabW : padX;
  p->tabY = padY;
  p->tabW = tabW;
  p->tabH = (std::min)(40.0, (innerH - 3.0 * (p->pages - 1)) / p->pages);
  return true;
}

void HudBoardOf(const PortJson &layer, double w, double h, const PortHudState &st, std::vector<HudBoardRow> *pRows) {
  pRows->clear();
  if (st.nPlayers <= 0)
    return;
  const bool bSplit = layer["split"].kind == PortJson::BOOL && layer["split"].b;
  double least = 20, most = 40;
  if (layer["rowH"].size() >= 2) {
    least = layer["rowH"].At(0).Num(20);
    most = (std::max)(least, layer["rowH"].At(1).Num(40));
  }
  // the sides: by team when the game has teams, else its players' first half and the rest
  std::vector<int> sides[2];
  bool bTeams = false;
  for (int i = 1; i < st.nPlayers; ++i)
    bTeams = bTeams || st.aPlayers[i].nTeam != st.aPlayers[0].nTeam;
  for (int i = 0; i < st.nPlayers; ++i) {
    const bool bRight = bSplit && (bTeams ? st.aPlayers[i].nTeam != st.aPlayers[0].nTeam : i >= (st.nPlayers + 1) / 2);
    sides[bRight ? 1 : 0].push_back(i);
  }
  const int per = (std::max)(1, static_cast<int>((std::max)(sides[0].size(), sides[1].size())));
  // never past the slot's foot; a side with more players than fit at the least height
  // shows one row fewer and "+N more" in its place
  const int fit = (std::max)(1, static_cast<int>(floor((h + 3) / (least + 3)))), rows = (std::min)(per, fit);
  const double gap = 3, rh = (std::max)(least, (std::min)(most, (h - (rows - 1) * gap) / rows));
  const double sideGap = bSplit ? w * 0.06 : gap, sideW = bSplit ? (w - sideGap) / 2 : w;
  const bool bTwo = rh >= 36;
  const double size = bTwo ? 13 : (std::min)(rh, 30.0) * 0.5, eye = (std::min)(rh, 30.0) * 0.9;
  for (int s = 0; s < 2; ++s) {
    const size_t shown = static_cast<int>(sides[s].size()) > fit ? static_cast<size_t>(fit - 1) : sides[s].size();
    for (size_t k = 0; k < shown; ++k) {
      HudBoardRow r;
      r.nPlayer = sides[s][k];
      r.bRight = s == 1;
      r.bTwo = bTwo;
      r.size = size;
      const double x = s == 1 ? sideW + sideGap : 0, y = k * (rh + gap);
      r.row[0] = x, r.row[1] = y, r.row[2] = sideW, r.row[3] = rh;
      // the columns from the row's outer edge: the colour, 8 apart, the name and the figures (on two
      // lines, one over the other), the eye's, 2 in from the inner edge
      const double colEye = bTwo ? rh * 0.6 : rh * 0.9, inner = sideW - 2 - 4 - colEye - (bTwo ? 2 : 3) * 8;
      const double nameW = bTwo ? inner : inner * 0.9 / 3.1, figW = bTwo ? inner : inner * 2.2 / 3.1;
      const double sw[4] = {0, 0, 4, rh};
      const double nm[4] = {12, 0, nameW, bTwo ? rh / 2 - 2 : rh};
      const double fg[4] = {bTwo ? 12 : 12 + nameW + 8, bTwo ? rh / 2 + 2 : 0, figW, bTwo ? rh / 2 - 2 : rh};
      const double ey[4] = {sideW - 2 - eye, (rh - eye) / 2, eye, eye};
      // mirrored on the right: each box's x from the row's other edge
      auto put = [&](const double q[4], double out[4]) {
        out[0] = x + (r.bRight ? sideW - q[0] - q[2] : q[0]), out[1] = y + q[1], out[2] = q[2], out[3] = q[3];
      };
      put(sw, r.swatch);
      put(nm, r.name);
      put(fg, r.figures);
      put(ey, r.eye);
      pRows->push_back(r);
    }
    if (shown < sides[s].size()) {
      HudBoardRow r;
      r.nPlayer = -1;
      r.nMore = static_cast<int>(sides[s].size() - shown);
      r.bRight = s == 1;
      r.size = (std::min)(rh, 30.0) * 0.46;
      r.row[0] = s == 1 ? sideW + sideGap : 0, r.row[1] = shown * (rh + gap), r.row[2] = sideW, r.row[3] = rh;
      pRows->push_back(r);
    }
  }
}

const HudMusicSkin *PortHudMusicSkin(void) {
  // the design draws from the game's start (PortHudFrameBegin), its plates with it
  if (g_pHud == NULL || !g_pHud->bOk || !g_pHud->design.music.bOn || !PortConsoleHudOn() || !PortHudGameStarted())
    return NULL;
  return &g_pHud->design.music;
}

void PortHudMusicSound(const char *pszMoment) {
  const HudMusicSkin *p = PortHudMusicSkin();
  if (p == NULL)
    return;
  const auto it = p->sounds.find(pszMoment);
  if (it != p->sounds.end() && !it->second.empty())
    PlayCue(g_pHud, it->second, 1.0);
}

void PortHudMusicPlates(const HudMusicPlate *pPlates, int nPlates) {
  if (g_pHud != NULL && g_pHud->bOk)
    g_pHud->player.MusicPlates(pPlates, nPlates);
}

bool PortHudTip(const HudTipContent *pContent, const double box[4], int nPanel) {
  if (g_pHud == NULL || !g_pHud->bOk || !g_pHud->bTips || !g_pHud->player.HasTip())
    return false;
  return g_pHud->player.Tip(pContent, box, nPanel);
}

double PortHudMapRaise(void) { return WantedNow().mapRaise; }

static PortHudPreview g_preview;
static bool g_bPreview;

void PortHudPreviewSet(const PortHudPreview &p) {
  g_preview = p;
  g_bPreview = true;
}

const PortHudPreview *PortHudPreviewTake(void) {
  const bool b = g_bPreview;
  g_bPreview = false;
  return b ? &g_preview : NULL;
}

void PortHudSides(double *pfLeft, double *pfRight) {
  *pfLeft = *pfRight = 0.0;
  if (g_pHud != NULL && g_pHud->bOk && g_pHud->nWidth > 0 && g_pHud->key == WantedNow().Key())
    g_pHud->player.Sides(pfLeft, pfRight);
}

bool PortHudSelectionAccent(float rgb[3]) {
  if (g_pHud == NULL || !g_pHud->bOk || !g_pHud->bAccent)
    return false;
  for (int i = 0; i < 3; ++i)
    rgb[i] = g_pHud->accent[i];
  return true;
}

bool PortHudSlotHidden(int nSlot) {
  return g_pHud != NULL && g_pHud->bOk && nSlot >= 0 && nSlot < static_cast<int>(g_pHud->hidden.size()) &&
         g_pHud->hidden[nSlot];
}

// The layers of the design worn now, while it is the game's console.
template <typename F> bool AnyLayer(F f) {
  const Active *h = g_pHud;
  if (h == NULL || !h->bOk || !PortConsoleHudOn() || h->key != WantedNow().Key())
    return false;
  for (const HudPanelDef &p : h->design.panels)
    for (const HudLayerDef &l : p.layers)
      if (f(l))
        return true;
  return false;
}

bool PortHudStatsPlaced(int nView) {
  const char *pszView = nView == 1 ? "popup" : "side";
  return AnyLayer([&](const HudLayerDef &l) { return l.nSlot == HUD_SLOT_STATS && l.def["view"].Str() == pszView; });
}

bool PortHudCountersOff(void) {
  if (g_pHud == NULL || !g_pHud->bOk || !g_pHud->design.bReplayMode || !AnyLayer([](const HudLayerDef &) { return true; }))
    return false;
  return !AnyLayer([](const HudLayerDef &l) { return l.nSlot == HUD_SLOT_TIME || l.nSlot == HUD_SLOT_APM; });
}

bool PortHudCovers(double u, double v) {
  const Active *h = g_pHud;
  if (h == NULL || !h->bOk || !PortConsoleHudOn() || g_hits.empty())
    return false;
  const std::vector<HudPanelFrame> &frames = h->player.PanelFrames();
  for (size_t p = 0; p < frames.size() && p < h->design.panels.size(); ++p) {
    double r[4];
    if (!frames[p].bVisible || frames[p].opacity < 0.05 || h->design.panels[p].bMusic ||
        !h->player.PanelRect(static_cast<int>(p), r))
      continue;
    if (u >= r[0] && v >= r[1] && u <= r[0] + r[2] && v <= r[1] + r[3])
      return true;
  }
  return false;
}

void PortHudReload(void) {
  g_bWant = false;
  if (g_pHud != NULL)
    g_pHud->key.clear();
}

unsigned int PortHudLoads(void) { return g_pHud != NULL ? g_pHud->nLoads : 0; }

HudPlayer *PortHudFrameBegin(int nWidth, int nHeight, const HudEnv **ppEnv) {
  PortHudState st;
  PortHudGet(&st);
  g_hits.clear();  // drawn again below, or none
  if (!st.bInGame)
    return NULL;
  const Want &w = WantedNow();
  if (w.layout.empty())
    return NULL;
  if (g_pHud == NULL)
    g_pHud = new Active;
  Active *h = g_pHud;
  // a design whose files were saved reads again, a refused one too, so an author sees a fix
  if (h->key != w.Key() || Changed(h))
    Load(h, w);
  // nor over the classic console, until the console is built again for it (PortConsoleUpdate)
  if (!h->bOk || !PortConsoleHudOn())
    return NULL;
  if (nWidth != h->nWidth || nHeight != h->nHeight) {
    h->player.Layout(nWidth, nHeight);
    h->nWidth = nWidth;
    h->nHeight = nHeight;
  }
  // laid out from the game's first frame, so the map is beside its columns from the start
  // (PortHudSides); drawn from its start: a skirmish's map comes in alone, and the design with
  // its intro where the original's console rises, before the countdown (PortHudArrive)
  if (!PortHudGameStarted())
    return NULL;
  PortHudReplayWatch();  // a paused replay runs no game frames to look at it
  PortHudEvent events[64];
  int n;
  while ((n = PortHudEventsSince(h->nSerial, events, 64)) > 0) {
    for (int i = 0; i < n; ++i) {
      const int nEvent = HudEventOf(PortHudEventName(events[i].nKind));
      if (nEvent >= 0)
        h->player.Fire(nEvent, std::string(), -1, events[i].nGroup, events[i].nView);
      if (events[i].nKind == PORT_HUD_GAME_START)
        h->nGameSerial = events[i].nSerial;
      h->nSerial = events[i].nSerial;
    }
  }
  ApplyStart(h, w);
  // the HUD's own clock, the window's time and not the game's, as the design center's; a
  // stall of more than a tenth of a second is taken as one
  const Uint64 nNow = SDL_GetTicks();
  const double dt = static_cast<double>(nNow - h->nLastMs);
  h->nLastMs = nNow;
  h->player.Advance(dt < 100.0 ? dt : 100.0);
  for (const HudPlayer::Cue &c : h->player.Cues())
    PlayCue(h, c.asset, c.volume);
  h->env.Fill(st);
  *ppEnv = &h->env;
  return &h->player;
}

void PortHudHitsSet(std::vector<HudHit> &&hits, double lw) {
  g_hits = std::move(hits);
  g_hitsLw = lw;
}

const std::vector<HudHit> &PortHudHits(double *pLw) {
  *pLw = g_hitsLw;
  return g_hits;
}

bool PortHudPanelRect(int nPanel, double out[4]) {
  return g_pHud != NULL && g_pHud->bOk && g_pHud->player.PanelRect(nPanel, out);
}

void PortHudPointer(bool bOn, double u, double v) {
  if (g_pHud == NULL || !g_pHud->bOk)
    return;
  Active *h = g_pHud;
  const std::vector<HudPanelDef> &panels = h->design.panels;
  h->hovered.resize(panels.size(), false);
  for (size_t p = 0; p < panels.size(); ++p) {
    if (!panels[p].bHover)
      continue;
    double r[4];
    const bool bInside = bOn && h->player.PanelRect(static_cast<int>(p), r) && u >= r[0] && u <= r[0] + r[2] &&
                         v >= r[1] && v <= r[1] + r[3];
    if (bInside != h->hovered[p]) {
      h->hovered[p] = bInside;
      h->player.Fire(bInside ? HUD_EV_PANEL_HOVER : HUD_EV_PANEL_LEAVE, panels[p].id);
    }
  }
}

void PortHudPressed(int nPanel, int nSlot, int nAction, int nActionArg) {
  if (g_pHud != NULL && g_pHud->bOk && nPanel >= 0 && nPanel < static_cast<int>(g_pHud->design.panels.size()))
    g_pHud->player.Fire(HUD_EV_PANEL_PRESS, g_pHud->design.panels[nPanel].id, nSlot, -1, -1, nAction, nActionArg);
}

bool PortHudCanSeek(void) {
  return AnyLayer([](const HudLayerDef &l) { return l.nAction == HUD_ACT_REPLAY_SEEK; });
}

const std::vector<std::string> &PortHudErrors(void) {
  static const std::vector<std::string> s_none;
  return g_pHud != NULL ? g_pHud->errors : s_none;
}

const std::vector<std::string> &PortHudWarnings(void) {
  static const std::vector<std::string> s_none;
  return g_pHud != NULL ? g_pHud->warnings : s_none;
}
