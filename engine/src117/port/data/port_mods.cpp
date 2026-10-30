// port_mods.cpp -- the mods port_mods.h describes. An installed mod is a folder under
// mods\ in the player data folder with a mod.json:
//
//   {"id": "debug", "name": "Debug Tools", "by": "Hyperion", "version": "0.1",
//    "line": "What it is, in a sentence.", "holds": ["tools", "maps"], "rules": "standard",
//    "world": "braxis", "channel": true, "data": "data/",
//    "modes": [{"id": "canvas", "button": "Start canvas", "tileset": "jungle"}]}
//
// "data" names a folder of the mod's with new types in it (port_moddata.h), and "depends" the
// mods whose data loads before it, [{"id": "base", "version": "1.2"}] or ["base"]
// (PortModChain). Only the first mode is used for now; a mode Hyperion does not know leaves
// the mod without a main button. A folder whose mod.json is missing or does not read is
// skipped; hyperion_mods.log says why.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include "../platform/port_paths.h"  // PortDataFolder, PortCommandOptions
#include "../platform/port_log.h"  // PortLogger
#include "../platform/port_util.h"  // PortReadFile
#include "../platform/port_json.h"
#include "port_mods.h"
#include "../platform/port_settings.h"
#include "../tools/port_canvas.h"  // PORT_CANVAS_TILESET
#include "../front/port_text.h"    // PT_

namespace {

std::vector<PortMod> g_mods;
std::vector<std::string> g_skipped;  // PortModsSkipped
bool g_bRead;

const PortLogger ModsLog("hyperion_mods.log");

void Skip(const std::string &why) {
  ModsLog("mods: %s", why);
  g_skipped.push_back(why);
}

void BuiltIn(void) {
  PortMod bw;
  bw.id = "bw";
  bw.name = "Brood War";
  bw.by = "Blizzard Entertainment";
  bw.version = "1.17";
  // the built-in mods' words, translated where they are shown
  bw.line = PT_("StarCraft and its expansion, as the original plays them.");
  bw.holds = {"campaign", "maps"};
  bw.rules = "standard";
  bw.world = "char";
  bw.bBuiltIn = bw.bExpansion = true;
  bw.mode = "campaign";
  bw.button = PT_("Continue: Episode VI");
  g_mods.push_back(bw);

  PortMod sc = bw;
  sc.id = "sc";
  sc.name = "StarCraft";
  sc.line = PT_("The original game without the expansion: the classic game's \"Original\" choice.");
  sc.world = "marsara";
  sc.bExpansion = false;
  sc.button = PT_("Continue: Episode I");
  g_mods.push_back(sc);
}

void Installed(void) {
  const std::string root = PortModsFolder();
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA((root + "*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return;
  std::vector<std::string> folders;
  do {
    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && fd.cFileName[0] != '.')
      folders.push_back(fd.cFileName);
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  std::sort(folders.begin(), folders.end());
  for (const std::string &f : folders) {
    const std::string folder = root + f + "\\";
    std::string text, error;
    PortJson j;
    if (!PortReadFile(folder + "mod.json", &text, 1u << 20)) {
      Skip(f + " has no mod.json");
      continue;
    }
    if (!PortJsonParse(text.c_str(), &j, &error) || j.kind != PortJson::OBJECT) {
      Skip(f + "'s mod.json does not read: " + error);
      continue;
    }
    PortMod m;
    m.id = j["id"].Str(f.c_str());
    m.name = j["name"].Str(f.c_str());
    m.by = j["by"].Str("an unnamed author");
    m.version = j["version"].Str("0");
    m.line = j["line"].Str();
    for (size_t i = 0; i < j["holds"].size(); ++i)
      m.holds.push_back(j["holds"].At(i).Str());
    m.rules = j["rules"].Str("standard");
    m.world = j["world"].Str("char");
    m.folder = folder;
    m.data = j["data"].Str();
    // a folder inside the mod's: none that climbs out of it or names another drive
    if (m.data.find("..") != std::string::npos || m.data.find(':') != std::string::npos ||
        (!m.data.empty() && (m.data[0] == '/' || m.data[0] == '\\'))) {
      ModsLog("mods: %s: \"data\" is a folder inside the mod's; its new types are left out", folder.c_str());
      m.data.clear();
    }
    if (!m.data.empty() && m.data.back() != '/' && m.data.back() != '\\')
      m.data += '\\';
    // each one an id, or {"id", "version"} with the least version it needs
    const PortJson &depends = j["depends"];
    for (size_t i = 0; i < depends.size(); ++i) {
      const PortJson &d = depends.At(i);
      PortModDependency dep;
      dep.id = d.kind == PortJson::STRING ? d.s : d["id"].Str();
      dep.version = d.kind == PortJson::STRING ? "" : d["version"].Str();
      if (dep.id.empty())
        ModsLog("mods: %s depends on a mod with no id", f.c_str());
      else
        m.depends.push_back(dep);
    }
    m.bExpansion = true;
    m.bChannel = j["channel"].kind == PortJson::BOOL && j["channel"].b;
    const PortJson &mode = j["modes"].At(0);
    if (mode["id"].Str() == "canvas") {
      m.mode = "canvas";
      m.button = mode["button"].Str(PT_("Start canvas"));
      m.tileset = mode["tileset"].Str(PORT_CANVAS_TILESET);
    } else if (mode["id"].Str() == "bench") {
      m.mode = "bench";
      m.button = mode["button"].Str(PT_("Set Up a Benchmark"));
    } else if (mode["id"].Str() == "trainer") {
      m.mode = "trainer";
      m.button = mode["button"].Str(PT_("Start Training"));
    } else if (mode["id"].Str() == "lab") {
      m.mode = "lab";
      m.button = mode["button"].Str("Open the Lab");
    }
    bool bTaken = false;
    for (const PortMod &o : g_mods)
      bTaken = bTaken || o.id == m.id;
    if (bTaken) {
      Skip(f + " uses the id " + m.id + ", which another mod has");
      continue;
    }
    ModsLog("mods: %s, from %s", m.name.c_str(), folder.c_str());
    g_mods.push_back(m);
  }
}

void Read(void) {
  if (g_bRead)
    return;
  g_bRead = true;
  g_mods.clear();
  g_skipped.clear();
  BuiltIn();
  Installed();
}

}  // namespace

// Settings' "Mods folder" ("modsFolder"), or mods\ in the player data folder: read once, so
// a new one takes a restart, as the active mod's data is read at start-up.
std::string PortModsFolder(void) {
  static std::string s_folder;
  if (s_folder.empty()) {
    char sz[MAX_PATH];
    if (PortSettingString("hyperion", "modsFolder", sz, sizeof sz) && sz[0] != '\0') {
      s_folder = sz;
      if (s_folder.back() != '\\' && s_folder.back() != '/')
        s_folder += '\\';
    } else {
      s_folder = std::string(PortDataFolder()) + "mods\\";
    }
  }
  return s_folder;
}

const std::vector<PortMod> &PortMods(void) {
  Read();
  return g_mods;
}

const std::vector<std::string> &PortModsSkipped(void) {
  Read();
  return g_skipped;
}

void PortModsRescan(void) {
  g_bRead = false;
  Read();
}

// Whether a mod was made active this session (the Mods screen's Activate), which then
// outranks --mod.
static bool s_bSetActive;

int PortModActive(void) {
  char sz[64];
  const std::vector<PortMod> &mods = PortMods();
  // --mod names the session's mod, the one whose data it loaded (ModDataStartUp)
  const char *pszWanted = PortCommandOptions()->szMod;
  if (!s_bSetActive && pszWanted[0] != '\0')
    for (size_t i = 0; i < mods.size(); ++i)
      if (mods[i].id == pszWanted)
        return static_cast<int>(i);
  if (PortSettingString("hyperion", "mod", sz, sizeof sz))
    for (size_t i = 0; i < mods.size(); ++i)
      if (mods[i].id == sz)
        return static_cast<int>(i);
  return 0;
}

void PortModSetActive(int i) {
  const std::vector<PortMod> &mods = PortMods();
  if (i >= 0 && i < static_cast<int>(mods.size())) {
    PortSettingSetString("hyperion", "mod", mods[i].id.c_str());
    s_bSetActive = true;
  }
}

// kept as "modChannel.<id>": the folder the player allowed, so another mod taking the id elsewhere asks again
bool PortModChannelAllowed(int i) {
  const std::vector<PortMod> &mods = PortMods();
  if (i < 0 || i >= static_cast<int>(mods.size()) || !mods[i].bChannel)
    return false;
  if (!s_bSetActive && mods[i].id == PortCommandOptions()->szMod)
    return true;
  char sz[MAX_PATH] = "";
  return PortSettingString("hyperion", ("modChannel." + mods[i].id).c_str(), sz, sizeof sz) && mods[i].folder == sz;
}

void PortModAllowChannel(int i) {
  const std::vector<PortMod> &mods = PortMods();
  if (i >= 0 && i < static_cast<int>(mods.size()))
    PortSettingSetString("hyperion", ("modChannel." + mods[i].id).c_str(), mods[i].folder.c_str());
}

namespace {

// Whether a version is the one named or later, part by dotted part ("1.10" after "1.9"); a
// part missing is 0.
bool VersionAtLeast(const std::string &have, const std::string &want) {
  const char *pHave = have.c_str(), *pWant = want.c_str();
  while (*pHave != '\0' || *pWant != '\0') {
    char *pEnd;
    const unsigned long nHave = strtoul(pHave, &pEnd, 10);
    pHave = *pEnd == '.' ? pEnd + 1 : pEnd + strlen(pEnd);
    const unsigned long nWant = strtoul(pWant, &pEnd, 10);
    pWant = *pEnd == '.' ? pEnd + 1 : pEnd + strlen(pEnd);
    if (nHave != nWant)
      return nHave > nWant;
  }
  return true;
}

bool ChainAdd(int nMod, std::vector<int> *pChain, std::vector<int> *pOpen, std::string *pWhy) {
  if (std::find(pChain->begin(), pChain->end(), nMod) != pChain->end())
    return true;
  const PortMod &mod = g_mods[nMod];
  if (std::find(pOpen->begin(), pOpen->end(), nMod) != pOpen->end()) {
    *pWhy = mod.name + " depends on itself, through the mods it depends on";
    return false;
  }
  pOpen->push_back(nMod);
  for (const PortModDependency &d : mod.depends) {
    int k = -1;
    for (size_t i = 0; i < g_mods.size(); ++i)
      if (g_mods[i].id == d.id)
        k = static_cast<int>(i);
    if (k < 0) {
      *pWhy = mod.name + " needs the mod " + d.id + ", which is not installed";
      return false;
    }
    if (!d.version.empty() && !VersionAtLeast(g_mods[k].version, d.version)) {
      *pWhy = mod.name + " needs " + g_mods[k].name + " " + d.version + " or later; the one installed is " +
              g_mods[k].version;
      return false;
    }
    if (!ChainAdd(k, pChain, pOpen, pWhy))
      return false;
  }
  pOpen->pop_back();
  pChain->push_back(nMod);
  return true;
}

}  // namespace

bool PortModChain(int nMod, std::vector<int> *pChain, std::string *pWhy) {
  Read();
  pChain->clear();
  if (nMod < 0 || nMod >= static_cast<int>(g_mods.size())) {
    *pWhy = "no such mod";
    return false;
  }
  std::vector<int> open;
  return ChainAdd(nMod, pChain, &open, pWhy);
}
