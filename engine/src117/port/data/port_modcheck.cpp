// port_modcheck.cpp -- a mod's check (port_modcheck.h): Hyperion --mod-check <id> [<report.txt>].
// The mod's data loads as start-up loads it, then each file its records name and each cinematic
// of its own is decoded as the game would decode it, with no game played and no window. Errors
// and warnings go a line each to the report, or to standard output without one, then notes of
// what loaded and a last line: "plays", "plays with errors" or "refused". The logs go to the
// data folder's mod-check\, so a Hyperion that is running keeps its own (docs/mod-data.md).
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../../h/platform.h"
#include "../../h/Storm.h"  // SMemFree
#include "../platform/port_paths.h"  // PortCommandOptions
#include "../platform/port_posix.h"  // PortNativePath
#include "../platform/port_util.h"  // PortReadFile
#include "port_imagefile.h"  // PortImageDecode
#include "port_modcheck.h"
#include "port_mods.h"
#include "port_sound.h"  // PortSoundLoad
#include "port_video.h"
#include "../tools/port_startup.h"  // PortStartupEnded
#if !defined(__EMSCRIPTEN__)
#include "port_sysvideo.h"  // SysVideoNeeds, SysSoundNeeds
#endif

void __fastcall LoadGraphics();     // CImage.cpp: the images, then the mod's data (ModDataStartUp)
extern const char *sgszFlics[28];  // playflic.cpp: the cinematics, "smk\blizzard.smk"

namespace {

std::vector<std::string> g_errors, g_warnings, g_notes;
int g_nSystem;   // videos with H.264, H.265 or AAC: the system's decoders
int g_nVideos;   // videos that are not Smacker, which a browser's build does not play
int g_nOgg;      // Ogg sounds, which it does not play either
int g_anDecoded[4];  // by ModCheckKind

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](char c) { return static_cast<char>(tolower(c)); });
  return s;
}

bool EndsWith(const std::string &s, const char *psz) {
  const size_t n = strlen(psz);
  return s.size() >= n && Lower(s.substr(s.size() - n)) == psz;
}

// A path as the report names it: from the mods folder, with forward slashes.
std::string Shown(const std::string &path) {
  const std::string root = PortModsFolder();
  std::string s = Lower(path).compare(0, root.size(), Lower(root)) == 0 ? path.substr(root.size()) : path;
  std::replace(s.begin(), s.end(), '\\', '/');
  return s;
}

std::string Count(size_t n, const char *pszOne, const char *pszMany) {
  return std::to_string(n) + " " + (n == 1 ? pszOne : pszMany);
}

void CheckSound(const ModCheckFile &f) {
  const std::string shown = f.where + ": " + Shown(f.path);
  DWORD cb = 0;
  void *p = PortSoundLoad(f.game.c_str(), &cb);
  if (p == NULL) {
    g_errors.push_back(shown + " does not decode: a sound is a WAV, Ogg (Vorbis or Opus), MP3 or FLAC file");
    return;
  }
  SMemFree(p, "port_modcheck.cpp", __LINE__, 0);
  std::vector<unsigned char> head;
  if (PortReadFile(f.path, &head) && head.size() >= 4 && memcmp(head.data(), "OggS", 4) == 0)
    ++g_nOgg;
  ++g_anDecoded[MOD_CHECK_SOUND];
}

// A portrait's or cinematic's video: a Smacker file by its header, any other by its container
// and its first frame decoded.
void CheckVideo(const std::string &where, const std::string &path) {
  const std::string shown = where + ": " + Shown(path);
  std::vector<unsigned char> b;
  if (!PortReadFile(path, &b, size_t(1) << 31)) {
    g_errors.push_back(shown + " does not read");
    return;
  }
  auto Le32 = [&](size_t i) { return b[i] | b[i + 1] << 8 | b[i + 2] << 16 | static_cast<unsigned>(b[i + 3]) << 24; };
  if (EndsWith(path, ".smk")) {
    if (b.size() < 16 || (memcmp(b.data(), "SMK2", 4) != 0 && memcmp(b.data(), "SMK4", 4) != 0)) {
      g_errors.push_back(shown + " is no Smacker video");
      return;
    }
    g_notes.push_back(shown + ": Smacker, " + std::to_string(Le32(4)) + " x " + std::to_string(Le32(8)) + ", " +
                      Count(Le32(12), "frame", "frames"));
    ++g_anDecoded[MOD_CHECK_VIDEO];
    return;
  }
  PortVideoInfo info;
  if (!PortVideoProbe(b.data(), b.size(), &info)) {
    g_errors.push_back(shown + " is no video Hyperion reads: WebM, Matroska, MP4 or QuickTime, with VP8, VP9, AV1, "
                               "H.264 or H.265 pictures");
    return;
  }
  ++g_nVideos;
  const bool bSystemVideo = strcmp(info.pszVideo, "H.264") == 0 || strcmp(info.pszVideo, "H.265") == 0;
  const bool bAac = strcmp(info.pszSound, "AAC") == 0;
  g_nSystem += bSystemVideo || bAac;
  const std::string size = std::to_string(info.nWidth) + " x " + std::to_string(info.nHeight);
  if (info.nWidth > 8192 || info.nHeight > 8192) {
    g_errors.push_back(shown + " is " + size + "; Hyperion plays videos up to 8192 a side");
    return;
  }
  PortVideo *v = PortVideoOpen(b.data(), b.size());
  if (v == NULL) {
#if !defined(__EMSCRIPTEN__)
    if (bSystemVideo) {
      const char *pszNeeds = SysVideoNeeds(strcmp(info.pszVideo, "H.265") == 0 ? VIDEO_CODEC_H265 : VIDEO_CODEC_H264);
      g_warnings.push_back(shown + " is " + info.pszVideo + ", which this system does not decode" +
                           (pszNeeds ? std::string("; it needs ") + pszNeeds : std::string()) +
                           ". Where a system does, it plays");
      return;
    }
#endif
    g_errors.push_back(shown + ": its " + info.pszVideo + " pictures do not decode");
    return;
  }
  PortVideoPlanes planes;
  const bool bFrame = PortVideoPicture(v, 0, &planes);
  size_t nSamples = 0;
  int nRate = 0, nChannels = 0;
  const bool bSound = PortVideoSound(v, &nSamples, &nRate, &nChannels) != NULL;
  const int nBits = bFrame ? planes.nBits : 0;
  PortVideoClose(v);
  if (!bFrame) {
    g_errors.push_back(shown + ": its first frame does not decode");
    return;
  }
  if (info.pszSound[0] != '\0' && !bSound) {
    const char *pszNeeds = NULL;
#if !defined(__EMSCRIPTEN__)
    pszNeeds = bAac ? SysSoundNeeds() : NULL;
#endif
    if (bAac)
      g_warnings.push_back(shown + ": its AAC sound is not decoded on this system" +
                           (pszNeeds ? std::string("; it needs ") + pszNeeds : std::string()));
    else
      g_errors.push_back(shown + ": its " + info.pszSound + " sound does not decode");
  }
  g_notes.push_back(shown + ": " + info.pszVideo + ", " + size + ", " + std::to_string(nBits) + " bits, " +
                    Count(info.nFrames, "frame", "frames") +
                    (info.pszSound[0] != '\0' ? std::string(", ") + info.pszSound + " sound" : std::string()));
  ++g_anDecoded[MOD_CHECK_VIDEO];
}

void CheckPicture(const ModCheckFile &f) {
  std::vector<unsigned char> b, rgba;
  std::string error;
  int w = 0, h = 0;
  if (!PortReadFile(f.path, &b, size_t(1) << 31) || !PortImageDecode(b.data(), b.size(), f.path, &rgba, &w, &h, &error)) {
    g_errors.push_back(f.where + ": " + Shown(f.path) + " " + (error.empty() ? "does not read" : error));
    return;
  }
  ++g_anDecoded[MOD_CHECK_PICTURE];
}

// A GRP: its frame count, size and frames' line tables inside the file.
void CheckGrp(const ModCheckFile &f) {
  const std::string shown = f.where + ": " + Shown(f.path);
  std::vector<unsigned char> b;
  if (!PortReadFile(f.path, &b) || b.size() < 6) {
    g_errors.push_back(shown + " is no GRP");
    return;
  }
  const size_t nFrames = (b[0] | b[1] << 8) & 0x7FFF;
  if (nFrames == 0 || 6 + 8 * nFrames > b.size()) {
    g_errors.push_back(shown + " is no GRP: its header names " + Count(nFrames, "frame", "frames") + " the file does not hold");
    return;
  }
  for (size_t i = 0; i < nFrames; ++i) {
    const unsigned char *p = &b[6 + 8 * i];
    const size_t off = p[4] | p[5] << 8 | p[6] << 16 | static_cast<size_t>(p[7]) << 24;
    if (off + 2 * p[3] > b.size()) {
      g_errors.push_back(shown + ": frame " + std::to_string(i) + " is past the file's end");
      return;
    }
  }
  ++g_anDecoded[MOD_CHECK_GRP];
}

std::string Stem(const std::string &name) {
  std::string s = name.substr(name.find_last_of("\\/") + 1);
  return s.substr(0, s.rfind('.'));
}

// The cinematics in each data folder's smk\, the active mod's first: a file named as one of the
// classic cinematics replaces it, the first of the extensions in the order the game tries them.
void CheckCinematics(const std::vector<int> &chain) {
  std::map<std::string, std::string> known;  // lower case, the name
  for (const char *psz : sgszFlics)
    if (psz != NULL)
      known[Lower(Stem(psz))] = Stem(psz);
  std::vector<std::string> order;
  for (const char *const *ppsz = PortVideoExtensions(); *ppsz; ++ppsz)
    order.push_back(*ppsz);
  order.push_back(".smk");
  std::set<std::string> taken;
  const std::vector<PortMod> &mods = PortMods();
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    const PortMod &m = mods[*it];
    if (m.data.empty())
      continue;
    const std::string folder = m.folder + m.data + "smk\\";
    std::map<std::string, std::vector<std::string>> byStem;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((folder + "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
      continue;
    do {
      if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        byStem[Lower(Stem(fd.cFileName))].push_back(folder + fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    for (auto &s : byStem) {
      std::vector<std::string> videos;
      std::string srt;
      for (const std::string &f : s.second) {
        const std::string ext = Lower(f.substr(f.rfind('.')));
        if (ext == ".srt")
          srt = f;
        else if (std::find(order.begin(), order.end(), ext) != order.end())
          videos.push_back(f);
        else
          g_warnings.push_back(Shown(f) + " is no video or subtitles, so nothing reads it");
      }
      auto k = known.find(s.first);
      if (k == known.end()) {
        for (const std::string &f : videos)
          g_warnings.push_back(Shown(f) + " is no cinematic's name, so nothing plays it (docs/mod-data.md, Cinematics)");
        if (!srt.empty())
          g_warnings.push_back(Shown(srt) + " is no cinematic's name, so nothing shows it");
        continue;
      }
      if (videos.empty()) {
        if (!srt.empty())
          g_warnings.push_back(Shown(srt) + " has no video beside it; a mod's subtitles go with its own video");
        continue;
      }
      const std::string where = "cinematic " + k->second;
      if (!taken.insert(s.first).second) {
        g_notes.push_back(where + ": " + Shown(videos[0]) + " is passed over for the mod's own");
        continue;
      }
      std::sort(videos.begin(), videos.end(), [&](const std::string &a, const std::string &b) {
        return std::find(order.begin(), order.end(), Lower(a.substr(a.rfind('.')))) <
               std::find(order.begin(), order.end(), Lower(b.substr(b.rfind('.'))));
      });
      for (size_t i = 1; i < videos.size(); ++i)
        g_warnings.push_back(Shown(videos[i]) + " is passed over for " + Shown(videos[0]) +
                             ", as .webm, .mkv, .mp4, .m4v, .mov and .smk are tried in that order");
      CheckVideo(where, videos[0]);
      std::string text;
      if (!srt.empty() && PortReadFile(srt, &text) && !text.empty() && text.find("-->") == std::string::npos)
        g_warnings.push_back(Shown(srt) + " has no cue: each is a number, a line such as 00:00:01,000 --> "
                             "00:00:04,000, and its text");
    }
  }
}

// The report, and the process's end.
[[noreturn]] void Finish(int nCode, const std::string &last) {
  const char *pszReport = PortCommandOptions()->szModCheckReport;
  FILE *f = pszReport[0] != '\0' ? fopen(pszReport, "w") : stdout;
  if (f == NULL)
    _Exit(2);
  for (const std::string &s : g_errors)
    fprintf(f, "error: %s\n", s.c_str());
  for (const std::string &s : g_warnings)
    fprintf(f, "warning: %s\n", s.c_str());
  for (const std::string &s : g_notes)
    fprintf(f, "note: %s\n", s.c_str());
  fprintf(f, "%s\n", last.c_str());
  fflush(f);
  if (f != stdout)
    fclose(f);
  PortStartupEnded(nCode);
  _Exit(nCode);
}

}  // namespace

void PortModCheckRun(void) {
  const std::string id = PortCommandOptions()->szModCheck;
  const std::vector<PortMod> &mods = PortMods();
  int nMod = -1;
  for (size_t i = 0; i < mods.size(); ++i)
    if (mods[i].id == id)
      nMod = static_cast<int>(i);
  if (nMod < 0) {
    g_errors.push_back("no mod has the id " + id + " in " + PortNativePath(PortModsFolder().c_str()));
    for (const std::string &s : PortModsSkipped())
      g_warnings.push_back(s);
    std::string ids;
    for (const PortMod &m : mods)
      ids += (ids.empty() ? "" : ", ") + m.id;
    g_notes.push_back("the mods: " + ids);
    Finish(2, "refused: no such mod");
  }
  const PortMod &mod = mods[nMod];
  g_notes.push_back(mod.name + " " + mod.version + " (" + mod.id + "), from " + PortNativePath(mod.folder.c_str()));
  // the mod's data, loaded as start-up loads it (ModDataStartUp)
  LoadGraphics();
  std::string error;
  std::vector<std::string> notes, tables;
  std::vector<ModCheckFile> files;
  const bool bLoaded = ModDataCheckResult(&error, &notes, &tables, &files);
  g_warnings.insert(g_warnings.end(), notes.begin(), notes.end());
  if (!bLoaded) {
    // the name that stopped the load is its error alone
    const std::string field = error.substr(0, error.find(" names "));
    g_warnings.erase(std::remove_if(g_warnings.begin(), g_warnings.end(),
                                    [&](const std::string &s) { return s.compare(0, field.size() + 7, field + " names ") == 0; }),
                     g_warnings.end());
    g_errors.push_back(error);
    Finish(1, "refused: its data does not load; " + Count(g_errors.size(), "error", "errors") + ", " +
                  Count(g_warnings.size(), "warning", "warnings"));
  }
  g_notes.insert(g_notes.end(), tables.begin(), tables.end());
  std::set<std::string> seen;
  for (const ModCheckFile &f : files) {
    if (!seen.insert(Lower(f.path)).second)
      continue;
    if (f.kind == MOD_CHECK_SOUND)
      CheckSound(f);
    else if (f.kind == MOD_CHECK_VIDEO)
      CheckVideo(f.where, f.path);
    else if (f.kind == MOD_CHECK_PICTURE)
      CheckPicture(f);
    else
      CheckGrp(f);
  }
  std::vector<int> chain;
  std::string why;
  if (PortModChain(nMod, &chain, &why))
    CheckCinematics(chain);
  if (g_nSystem > 0)
    g_warnings.push_back(Count(g_nSystem, "video is H.264 or H.265, or has AAC sound,",
                               "videos are H.264 or H.265, or have AAC sound,") +
                         " which Hyperion plays through the system's decoders: "
                         "Media Foundation on Windows (H.265 with Microsoft's HEVC Video Extensions), VideoToolbox "
                         "on a Mac, FFmpeg's libavcodec 58 to 63 on Linux. AV1, VP9 or VP8 with Opus or Vorbis "
                         "plays on all three");
  if (g_nVideos > 0 || g_nOgg > 0)
    g_warnings.push_back((g_nVideos > 0 ? Count(g_nVideos, "video", "videos") : std::string()) +
                         (g_nVideos > 0 && g_nOgg > 0 ? " and " : "") +
                         (g_nOgg > 0 ? Count(g_nOgg, "Ogg sound", "Ogg sounds") : std::string()) +
                         " do not play in the browser's build, which has Smacker, WAV, MP3 and FLAC alone");
  const int nDecoded = g_anDecoded[0] + g_anDecoded[1] + g_anDecoded[2] + g_anDecoded[3];
  if (nDecoded > 0)
    g_notes.push_back(Count(nDecoded, "file decodes", "files decode") + ": " +
                      Count(g_anDecoded[MOD_CHECK_SOUND], "sound", "sounds") + ", " +
                      Count(g_anDecoded[MOD_CHECK_VIDEO], "video", "videos") + ", " +
                      Count(g_anDecoded[MOD_CHECK_PICTURE] + g_anDecoded[MOD_CHECK_GRP], "picture", "pictures"));
  const std::string last = Count(g_errors.size(), "error", "errors") + ", " +
                           Count(g_warnings.size(), "warning", "warnings");
  Finish(g_errors.empty() ? 0 : 1, (g_errors.empty() ? "plays: " : "plays with errors: ") + last);
}
