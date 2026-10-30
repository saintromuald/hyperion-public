// port_hudcheck.cpp -- Hyperion.exe --hud-check: a HUD file read and checked from the command
// line, with no game (port_hudcheck.h). A maker's check of a design before a game, and the
// layout's boxes for the comparison with the design center's player (tools/hud/hudgolden.mjs).
//
//   --hud-check <layout.json | folder | package.hud> [--style <style.json>] [--pick <option>=<value>]...
//               [--light #rrggbb] [--layout <W>x<H>] [--out <report.json>]
//
// A folder is read as a design's: its hud.json, its style.json if there is one, and its parts
// files. A package is unpacked first, as a game does. Exits 0 when the file plays, 1 when it has
// errors, 2 when a file does not read.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <SDL3/SDL.h>  // SDL_strcasecmp
#include "port_hud.h"
#include "port_hudcheck.h"
#include "port_hudfile.h"
#include "port_hudplay.h"
#include "port_hudstate.h"
#include "../platform/port_json.h"
#include "../platform/port_util.h"

namespace {

const char *const kKinds[] = {"image", "run", "rect", "text", "shape", "slot"};

std::string Num(double d) {
  char sz[32];
  snprintf(sz, sizeof sz, "%.4f", d);
  // the trailing zeros off, as JSON readers print it
  std::string s = sz;
  while (!s.empty() && s.back() == '0')
    s.pop_back();
  if (!s.empty() && s.back() == '.')
    s.pop_back();
  return s == "-0" ? "0" : s;
}

}  // namespace

int PortHudCheckCommand(const std::vector<std::string> &args) {
  std::string layoutPath, stylePath, outPath;
  HudPicks picks;
  int nW = 0, nH = 0;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string &a = args[i];
    const bool bNext = i + 1 < args.size();
    if (a == "--style" && bNext) {
      stylePath = args[++i];
    } else if (a == "--pick" && bNext) {
      const std::string p = args[++i];
      const size_t eq = p.find('=');
      if (eq != std::string::npos)
        picks.options.push_back({p.substr(0, eq), p.substr(eq + 1)});
    } else if (a == "--light" && bNext) {
      picks.light = args[++i];
    } else if (a == "--layout" && bNext) {
      if (sscanf(args[++i].c_str(), "%dx%d", &nW, &nH) != 2)
        nW = nH = 0;
    } else if (a == "--out" && bNext) {
      outPath = args[++i];
    } else if (layoutPath.empty()) {
      layoutPath = a;
    }
  }
  // a design's folder or a .hud package, as Settings lists them: its hud.json and style.json
  std::vector<std::string> unpacked;
  std::string folder;
  const DWORD dwAttr = GetFileAttributesA(layoutPath.c_str());
  if (dwAttr != INVALID_FILE_ATTRIBUTES && (dwAttr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    folder = layoutPath + "\\";
  } else if (layoutPath.size() > 4 && SDL_strcasecmp(layoutPath.c_str() + layoutPath.size() - 4, ".hud") == 0 &&
             !PortHudUnpack(layoutPath, &folder, &unpacked)) {
    fprintf(stderr, "--hud-check: the package %s does not unpack\n", layoutPath.c_str());
    return 2;
  }
  if (!folder.empty()) {
    layoutPath = folder + "hud.json";
    if (stylePath.empty() && GetFileAttributesA((folder + "style.json").c_str()) != INVALID_FILE_ATTRIBUTES)
      stylePath = folder + "style.json";
  }
  std::string layout, style;
  if (layoutPath.empty() || !PortReadFile(layoutPath, &layout)) {
    fprintf(stderr, "--hud-check: the layout %s does not read\n", layoutPath.c_str());
    return 2;
  }
  if (!stylePath.empty() && !PortReadFile(stylePath, &style)) {
    fprintf(stderr, "--hud-check: the style %s does not read\n", stylePath.c_str());
    return 2;
  }
  // read as a game reads it, with the parts files beside it
  HudDesign design;
  std::vector<std::string> errors, warnings;
  const bool bOk = PortHudRead(layoutPath, stylePath, picks.light,
                               [&](const std::string &id) {
                                 for (const auto &p : picks.options)
                                   if (p.first == id)
                                     return p.second;
                                 return std::string();
                               },
                               &design, &errors, &warnings);
  warnings.insert(warnings.begin(), unpacked.begin(), unpacked.end());
  std::string j = "{\"plays\":" + std::string(bOk ? "true" : "false") + ",\"errors\":[";
  for (size_t i = 0; i < errors.size(); ++i)
    j += (i ? "," : "") + PortJsonQuote(errors[i]);
  j += "],\"warnings\":[";
  for (size_t i = 0; i < warnings.size(); ++i)
    j += (i ? "," : "") + PortJsonQuote(warnings[i]);
  j += "]";
  if (bOk) {
    j += ",\"panels\":" + std::to_string(design.panels.size()) + ",\"timelines\":" +
         std::to_string(design.timelines.size());
  }
  if (bOk && nW > 0 && nH > 0) {
    // the boxes at that shape, with no game: the bindings read as a game's start would
    HudPlayer player;
    player.Load(&design);
    player.Layout(nW, nH);
    HudEnv env;
    PortHudState none = {};
    env.Fill(none);
    const std::vector<HudDrawItem> &items = player.Frame(env);
    j += ",\"lw\":" + Num(player.LW()) + ",\"rects\":{";
    for (size_t p = 0; p < design.panels.size(); ++p) {
      double r[4];
      player.PanelRect(static_cast<int>(p), r);
      j += (p ? "," : "") + PortJsonQuote(design.panels[p].id) + ":[" + Num(r[0]) + "," + Num(r[1]) + "," + Num(r[2]) +
           "," + Num(r[3]) + "]";
    }
    j += "},\"layers\":[";
    for (size_t i = 0; i < items.size(); ++i) {
      const HudDrawItem &it = items[i];
      const HudLayerDef &ld = design.panels[it.nPanel].layers[it.nLayer];
      j += std::string(i ? "," : "") + "{\"p\":" + std::to_string(it.nPanel) + ",\"l\":" + std::to_string(it.nLayer) +
           ",\"target\":" + PortJsonQuote(design.panels[it.nPanel].id + "." + ld.id) +
           ",\"kind\":" + PortJsonQuote(kKinds[it.kind]) + ",\"box\":[" + Num(it.box[0]) + "," + Num(it.box[1]) + "," +
           Num(it.box[2]) + "," + Num(it.box[3]) + "],\"margin\":" + Num(it.margin) + ",\"m\":[" + Num(it.m[0]) + "," +
           Num(it.m[1]) + "," + Num(it.m[2]) + "," + Num(it.m[3]) + "," + Num(it.m[4]) + "," + Num(it.m[5]) + "]}";
    }
    j += "]";
  }
  j += "}\n";
  FILE *f = outPath.empty() ? stdout : fopen(outPath.c_str(), "wb");
  if (f == NULL)
    return 2;
  fputs(j.c_str(), f);
  if (f != stdout)
    fclose(f);
  return bOk ? 0 : 1;
}
