// port_looks.cpp -- looks: a unit's images drawn past their own colours
// (port_looks.h). Presentation only.
#define NOMINMAX
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

#include "port_looks.h"
#include "port_present.h"           // PortPresentRedraw
#include "port_scene.h"             // PortSceneLook
#include "port_teamcolor.h"         // PortRgbToLab
#include "../data/port_mods.h"      // PortMods, PortModActive: the active mod's folder
#include "../platform/port_json.h"
#include "../platform/port_log.h"   // PortLogger
#include "../platform/port_paths.h" // PortDataFolder
#include "../platform/port_util.h"  // PortReadFile
#include "../tools/port_control.h"  // PortControlFloat, PortControlCommand
#include "../../h/CUnit.h"
#include "../../h/CSprite.h"
#include "../../h/CImage.h"
#include "../../h/CThingy.h"        // the fogged copies
#include "../../h/grp.h"            // GROUP: an image's canvas
#include "../../h/poollimits.h"     // POOL_UNITS_STORE, g_pools
#include "../../h/typelimits.h"     // UNIT_TYPES_MAX, IMAGE_TYPES_MAX
#include "../../gen/globals117.h"   // sgGamePalette_g, _gbRemapColorTbl, glScrollPixelX

namespace {

// The palette entries a look takes, and what it is made of on the GPU.
enum { RULE_BLUES, RULE_GREENS, RULE_ALL };
enum { MAT_CRYSTAL, MAT_GAS };
enum { RAMP = 6, LOOKS_MAX = 254 };
const char *const kRules[] = {"blues", "greens", "all"};
const char *const kMaterials[] = {"crystal", "gas"};

// A vent of an image a plume rises from: its place in the image's canvas, how strong it is
// and how wide (the design's numbers, local-design-center, 41-rich-bases/richfx.js), and the
// frame it is for, -1 for any. An image's vents for the frame drawn stand in for its others.
struct Vent {
  int nImage;
  float x, y, fStrength, fWidth;
  int nFrame;
};

struct Look {
  std::string name;
  int nRule = RULE_ALL;
  int nMaterial = MAT_CRYSTAL;
  unsigned int adwRamp[RAMP] = {};  // 0xRRGGBB, dark to light
  std::vector<int> images;          // the images it covers, in order; none for all of a sprite's
  std::vector<int> units;           // the unit types that have it
  std::vector<int> replaced;        // the images its plume replaces in full colour, in order
  float afMat[4] = {};              // clarity, shine, glow, sparkle, 0 to 1
  float afPlume[3] = {};            // height in pixels, density, rise in pixels a second
  unsigned int dwLight = 0;         // the ground light's colour
  float afLight[4] = {};            // its radius and strength, and its centre from the unit's place
  std::vector<Vent> vents;
  // made for the palette in g_abPalette
  bool abTakes[256] = {};
  BYTE abRemap[256] = {};
  BYTE abTone[256] = {};  // 0 where the rule does not take the entry, else 1 + its tone * 254
  BYTE bPen = 0;
};

std::vector<Look> g_looks;  // [0] is none
std::string g_source;       // where the looks past the built-in ones came from
BYTE g_abPalette[1024];     // the palette the looks were last made for
bool g_bMade;
unsigned char g_abUnitLook[POOL_UNITS_STORE];  // given by hand
unsigned char g_abTypeLook[UNIT_TYPES_MAX];    // given by type
PFN_PORT_LOOK_OF_UNIT g_pfnProvider;

struct Looked {
  const CSprite *pSprite;
  int nLook;
  int x, y, nType;  // a unit's place and type, for a fogged copy and the minimap
  bool bUnit;
};
std::vector<Looked> g_looked;  // this pass's, in the order of the sprites
BYTE g_abSaved[256];
unsigned int g_nNow;  // PortLookNow's

// The live values (port_looks.h)
float g_fOn = 1.0f, g_fSteps = 1.0f;
float g_fClarity = 1.0f, g_fShine = 1.0f, g_fGlow = 1.0f, g_fSparkle = 1.0f, g_fPlume = 1.0f, g_fLight = 1.0f;

std::vector<unsigned char> g_gpu;  // PortLooksGpu's

const PortLogger Log("hyperion_mods.log");

// 1.17's mineral fields, its geyser, and the buildings on one; what a worker carries (the
// mineral chunks, the protoss orbs, the zerg sacs and the terran tanks, each with its smaller
// one); and the geyser's puffs.
const int kGoldImages[] = {347, 349, 351, 0x18D, 0x18E};
const int kVioletImages[] = {93, 158, 307, 344, 0x18F, 0x190, 0x191, 0x192, 0x193, 0x194,
                             430, 431, 432, 433, 434, 435, 436, 437, 438, 439};
const int kPuffs[] = {430, 431, 432, 433, 434, 435, 436, 437, 438, 439};
// the geyser's three vents, and those of the refinery, the assimilator and the extractor on it.
// The geyser's frame is the tileset's: space platform's and installation's (1 and 2) are the
// others' drawn 6 pixels to the right, ash world's (3) is drawn apart; each vent is at its
// crater's mouth, set after play (2026-10-26) where the design's sat left of and below it
const Vent kVents[] = {
    {344, 73, 19, 1.0f, 4.5f, -1},   {344, 91, 40, 0.45f, 2.5f, -1},  {344, 25, 29, 0.5f, 2.5f, -1},
    {344, 79, 19, 1.0f, 4.5f, 1},    {344, 97, 40, 0.45f, 2.5f, 1},   {344, 31, 29, 0.5f, 2.5f, 1},
    {344, 79, 19, 1.0f, 4.5f, 2},    {344, 97, 40, 0.45f, 2.5f, 2},   {344, 31, 29, 0.5f, 2.5f, 2},
    {344, 74, 16, 1.0f, 4.5f, 3},    {344, 95, 35, 0.45f, 2.5f, 3},   {344, 25, 24, 0.5f, 2.5f, 3},
    {307, 103, 39, 1.0f, 4.5f, -1},  {158, 53, 50, 0.75f, 3.0f, -1},  {158, 123, 41, 0.75f, 3.0f, -1},
    {158, 137, 80, 0.7f, 3.0f, -1},  {93, 64, 50, 1.0f, 4.0f, -1},    {93, 23, 68, 0.4f, 2.5f, -1},
    {93, 107, 92, 0.4f, 2.5f, -1}};
// the design's ramps
const unsigned int kGold[RAMP] = {0x2C1403, 0x6E3A06, 0xB8740E, 0xEAA81C, 0xFFD64A, 0xFFF3B8};
const unsigned int kViolet[RAMP] = {0x1C0628, 0x4C1166, 0x8C2EB4, 0xC35EE8, 0xEBA6FF, 0xFFF0FF};

// A new look as the design's gold: clarity 38, shine 55, glow 50, sparkle 55; a plume 54 high at
// 62, with no vents; a light of radius 40 at three quarters' strength below the unit's place (the
// design's half, raised after play: "The ground light is too faint, make it stronger just a bit").
Look Fresh(const char *pszName) {
  Look l;
  l.name = pszName;
  memcpy(l.adwRamp, kGold, sizeof l.adwRamp);
  const float afMat[4] = {0.38f, 0.55f, 0.50f, 0.55f};
  memcpy(l.afMat, afMat, sizeof l.afMat);
  l.afPlume[0] = 54.0f;
  l.afPlume[1] = 0.62f * 1.6f;
  l.afPlume[2] = 20.0f;
  l.dwLight = kGold[4];
  l.afLight[0] = 40.0f;
  l.afLight[1] = 0.75f;
  l.afLight[3] = 8.0f;
  for (int e = 0; e < 256; e++)
    l.abRemap[e] = static_cast<BYTE>(e);
  return l;
}

// The built-in looks (local-design-center, 41-rich-bases/richfx.js).
void Builtins(void) {
  g_looks.assign(1, Look());
  Look gold = Fresh("rich-gold");
  gold.nRule = RULE_BLUES;
  gold.images.assign(kGoldImages, kGoldImages + sizeof kGoldImages / sizeof kGoldImages[0]);
  g_looks.push_back(gold);
  // the gas lights the ground wider and stronger, from a little right of the geyser and above
  // its middle, in the ramp's middle violet; its plume replaces 1.17's puffs
  Look violet = Fresh("rich-violet");
  violet.nRule = RULE_GREENS;
  violet.nMaterial = MAT_GAS;
  memcpy(violet.adwRamp, kViolet, sizeof violet.adwRamp);
  violet.images.assign(kVioletImages, kVioletImages + sizeof kVioletImages / sizeof kVioletImages[0]);
  violet.dwLight = kViolet[3];
  violet.afLight[0] = 74.0f;
  violet.afLight[1] = 1.25f;
  violet.afLight[2] = 6.0f;
  violet.afLight[3] = -4.0f;
  violet.replaced.assign(kPuffs, kPuffs + sizeof kPuffs / sizeof kPuffs[0]);
  violet.vents.assign(kVents, kVents + sizeof kVents / sizeof kVents[0]);
  g_looks.push_back(violet);
  std::sort(g_looks[1].images.begin(), g_looks[1].images.end());
  std::sort(g_looks[2].images.begin(), g_looks[2].images.end());
  g_bMade = false;
}

void Reindex(void) {
  memset(g_abTypeLook, 0, sizeof g_abTypeLook);
  for (size_t i = 1; i < g_looks.size(); i++)
    for (int t : g_looks[i].units)
      g_abTypeLook[t] = static_cast<unsigned char>(i);
  g_bMade = false;
}

// looks.json: the active mod's, else the data folder's.
std::string FilePath(void) {
  const std::vector<PortMod> &mods = PortMods();
  const int nActive = PortModActive();
  if (nActive >= 0 && nActive < static_cast<int>(mods.size())) {
    const std::string folder = mods[nActive].data.empty() ? mods[nActive].folder : mods[nActive].data;
    if (!folder.empty())
      return folder + "looks.json";
  }
  return std::string(PortDataFolder()) + "looks.json";
}

bool ParseColour(const PortJson &j, unsigned int *pdw) {
  unsigned int v = 0;
  if (j.kind != PortJson::STRING || j.s.size() != 7 || j.s[0] != '#' || sscanf_s(j.s.c_str() + 1, "%6x", &v) != 1)
    return false;
  *pdw = v;
  return true;
}

std::string ColourText(unsigned int dw) {
  char sz[8];
  sprintf_s(sz, "#%06x", dw & 0xFFFFFF);
  return sz;
}

float Clamp(double d, float fLo, float fHi) {
  return static_cast<float>(d < fLo ? fLo : d > fHi ? fHi : d);
}

bool Name(const PortJson &j, const char *const *ppszNames, int nNames, int *pn) {
  for (int i = 0; i < nNames; i++)
    if (j.s == ppszNames[i]) {
      *pn = i;
      return true;
    }
  return false;
}

// A list of numbers below nEnd, in order.
bool Numbers(const PortJson &j, int nEnd, std::vector<int> *pOut) {
  if (j.kind != PortJson::ARRAY)
    return false;
  std::vector<int> out;
  for (size_t i = 0; i < j.size(); i++) {
    const double d = j.At(i).Num(-1);
    if (d < 0 || d >= nEnd)
      return false;
    out.push_back(static_cast<int>(d));
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  *pOut = out;
  return true;
}

// The fields of a look a message or the file gives; the rest stay as they are.
bool ReadLook(const PortJson &j, Look *pLook, std::string *pWhy) {
  if (j["rule"].kind != PortJson::NONE && !Name(j["rule"], kRules, 3, &pLook->nRule)) {
    *pWhy = "rule is \"blues\", \"greens\" or \"all\"";
    return false;
  }
  if (j["material"].kind != PortJson::NONE && !Name(j["material"], kMaterials, 2, &pLook->nMaterial)) {
    *pWhy = "material is \"crystal\" or \"gas\"";
    return false;
  }
  if (j["ramp"].kind != PortJson::NONE) {
    unsigned int adw[RAMP];
    bool bOk = j["ramp"].size() == RAMP;
    for (int k = 0; bOk && k < RAMP; k++)
      bOk = ParseColour(j["ramp"].At(k), &adw[k]);
    if (!bOk) {
      *pWhy = "ramp is six colours, \"#rrggbb\", dark to light";
      return false;
    }
    memcpy(pLook->adwRamp, adw, sizeof adw);
  }
  if (j["images"].kind != PortJson::NONE && !Numbers(j["images"], IMAGE_TYPES_MAX, &pLook->images)) {
    *pWhy = "images is a list of places in images.dat";
    return false;
  }
  if (j["units"].kind != PortJson::NONE && !Numbers(j["units"], UNIT_TYPES_MAX, &pLook->units)) {
    *pWhy = "units is a list of places in units.dat";
    return false;
  }
  const char *const apszMat[4] = {"clarity", "shine", "glow", "sparkle"};
  for (int k = 0; k < 4; k++)
    if (j[apszMat[k]].kind == PortJson::NUMBER)
      pLook->afMat[k] = Clamp(j[apszMat[k]].d, 0.0f, 1.0f);
  const PortJson &plume = j["plume"];
  if (plume["height"].kind == PortJson::NUMBER)
    pLook->afPlume[0] = Clamp(plume["height"].d, 0.0f, 256.0f);
  if (plume["density"].kind == PortJson::NUMBER)
    pLook->afPlume[1] = Clamp(plume["density"].d, 0.0f, 4.0f);
  if (plume["rise"].kind == PortJson::NUMBER)
    pLook->afPlume[2] = Clamp(plume["rise"].d, 0.0f, 200.0f);
  if (plume["replaces"].kind != PortJson::NONE && !Numbers(plume["replaces"], IMAGE_TYPES_MAX, &pLook->replaced)) {
    *pWhy = "plume.replaces is a list of places in images.dat";
    return false;
  }
  if (plume["vents"].kind != PortJson::NONE) {
    std::vector<Vent> vents;
    for (size_t i = 0; i < plume["vents"].size(); i++) {
      const PortJson &v = plume["vents"].At(i);
      const double dImage = v["image"].Num(-1);
      if (dImage < 0 || dImage >= IMAGE_TYPES_MAX) {
        *pWhy = "a vent is {image, x, y, strength, width, frame}, x and y in the image's frame";
        return false;
      }
      const double dFrame = v["frame"].Num(-1);
      vents.push_back({static_cast<int>(dImage), Clamp(v["x"].Num(0), -512.0f, 512.0f), Clamp(v["y"].Num(0), -512.0f, 512.0f),
                       Clamp(v["strength"].Num(1), 0.0f, 4.0f), Clamp(v["width"].Num(3), 0.5f, 32.0f),
                       dFrame >= 0 && dFrame < 65536 ? static_cast<int>(dFrame) : -1});
    }
    pLook->vents = vents;
  }
  const PortJson &light = j["light"];
  if (light["color"].kind != PortJson::NONE && !ParseColour(light["color"], &pLook->dwLight)) {
    *pWhy = "light.color is \"#rrggbb\"";
    return false;
  }
  if (light["radius"].kind == PortJson::NUMBER)
    pLook->afLight[0] = Clamp(light["radius"].d, 1.0f, 512.0f);
  if (light["strength"].kind == PortJson::NUMBER)
    pLook->afLight[1] = Clamp(light["strength"].d, 0.0f, 4.0f);
  if (light["x"].kind == PortJson::NUMBER)
    pLook->afLight[2] = Clamp(light["x"].d, -256.0f, 256.0f);
  if (light["y"].kind == PortJson::NUMBER)
    pLook->afLight[3] = Clamp(light["y"].d, -256.0f, 256.0f);
  return true;
}

std::string NumbersText(const std::vector<int> &v) {
  std::string s = "[";
  for (size_t i = 0; i < v.size(); i++)
    s += (i ? "," : "") + std::to_string(v[i]);
  return s + "]";
}

// A look as looks.json and the channel give it, on one line.
std::string LookJson(const Look &l) {
  char sz[256];
  std::string s = "{\"name\":" + PortJsonQuote(l.name) + ",\"rule\":\"" + kRules[l.nRule] + "\",\"material\":\"" +
                  kMaterials[l.nMaterial] + "\",\"ramp\":[";
  for (int k = 0; k < RAMP; k++)
    s += std::string(k ? "," : "") + "\"" + ColourText(l.adwRamp[k]) + "\"";
  s += "],\"images\":" + NumbersText(l.images) + ",\"units\":" + NumbersText(l.units);
  sprintf_s(sz, ",\"clarity\":%g,\"shine\":%g,\"glow\":%g,\"sparkle\":%g,\"plume\":{\"height\":%g,\"density\":%g,\"rise\":%g",
            l.afMat[0], l.afMat[1], l.afMat[2], l.afMat[3], l.afPlume[0], l.afPlume[1], l.afPlume[2]);
  s += sz;
  s += ",\"replaces\":" + NumbersText(l.replaced) + ",\"vents\":[";
  for (size_t i = 0; i < l.vents.size(); i++) {
    const Vent &v = l.vents[i];
    sprintf_s(sz, "%s{\"image\":%d,\"x\":%g,\"y\":%g,\"strength\":%g,\"width\":%g", i ? "," : "", v.nImage, v.x, v.y,
              v.fStrength, v.fWidth);
    s += sz;
    if (v.nFrame >= 0) {
      sprintf_s(sz, ",\"frame\":%d", v.nFrame);
      s += sz;
    }
    s += "}";
  }
  sprintf_s(sz, "]},\"light\":{\"color\":\"%s\",\"radius\":%g,\"strength\":%g,\"x\":%g,\"y\":%g}}",
            ColourText(l.dwLight).c_str(), l.afLight[0], l.afLight[1], l.afLight[2], l.afLight[3]);
  return s + sz;
}

// looks.json over the built-in looks: each look in it changes the one of its name, or is added.
void ReadLooksFile(void) {
  g_source = "built in";
  const std::string path = FilePath();
  std::string text;
  if (!PortReadFile(path, &text))
    return;
  PortJson j;
  std::string why;
  if (!PortJsonParse(text.c_str(), &j, &why) || j["looks"].kind != PortJson::ARRAY) {
    Log("looks: %s does not read, the built-in looks are used: %s", path.c_str(),
        why.empty() ? "it has no \"looks\" list" : why.c_str());
    return;
  }
  for (size_t i = 0; i < j["looks"].size(); i++) {
    const PortJson &l = j["looks"].At(i);
    const std::string name = l["name"].Str("");
    if (name.empty()) {
      Log("looks: look %d in %s has no name, and is left out", static_cast<int>(i), path.c_str());
      continue;
    }
    const int nAt = PortLookFind(name.c_str());
    Look look = nAt != 0 ? g_looks[nAt] : Fresh(name.c_str());
    if (!ReadLook(l, &look, &why)) {
      Log("looks: %s in %s is left out: %s", name.c_str(), path.c_str(), why.c_str());
      continue;
    }
    if (nAt != 0)
      g_looks[nAt] = look;
    else if (g_looks.size() <= LOOKS_MAX)
      g_looks.push_back(look);
  }
  g_source = path;
  Log("looks: %d looks, from %s", static_cast<int>(g_looks.size()) - 1, path.c_str());
}

bool CmdLooks(const PortJson &, std::string *pAnswer) {
  std::string s = "\"source\":" + PortJsonQuote(g_source) + ",\"file\":" + PortJsonQuote(FilePath()) + ",\"looks\":[";
  for (size_t i = 1; i < g_looks.size(); i++)
    s += (i > 1 ? "," : "") + LookJson(g_looks[i]);
  *pAnswer = s + "]";
  return true;
}

bool CmdLook(const PortJson &j, std::string *pAnswer) {
  const std::string name = j["name"].Str("");
  if (name.empty()) {
    *pAnswer = "look needs name";
    return false;
  }
  const int nAt = PortLookFind(name.c_str());
  if (nAt == 0 && g_looks.size() > LOOKS_MAX) {
    *pAnswer = "there are 254 looks already";
    return false;
  }
  Look look = nAt != 0 ? g_looks[nAt] : Fresh(name.c_str());
  if (!ReadLook(j, &look, pAnswer))
    return false;
  if (nAt != 0)
    g_looks[nAt] = look;
  else
    g_looks.push_back(look);
  Reindex();
  PortPresentRedraw();
  *pAnswer = "\"look\":" + std::to_string(nAt != 0 ? nAt : static_cast<int>(g_looks.size()) - 1);
  return true;
}

bool CmdLookUnit(const PortJson &j, std::string *pAnswer) {
  const double dUnit = j["unit"].Num(-1);
  const std::string name = j["look"].Str("");
  if (dUnit < 0 || dUnit >= g_pools.units) {
    *pAnswer = "look_unit needs unit, its id as units gives it";
    return false;
  }
  const int nLook = name.empty() ? 0 : name == "none" ? PORT_LOOK_NONE : PortLookFind(name.c_str());
  if (!name.empty() && nLook == 0) {
    *pAnswer = "no look is named " + name;
    return false;
  }
  PortLookUnitSet(static_cast<int>(dUnit), nLook);
  PortPresentRedraw();
  *pAnswer = "";
  return true;
}

bool CmdLooksSave(const PortJson &, std::string *pAnswer) {
  const std::string path = FilePath();
  std::string text = "{\n  \"looks\": [\n";
  for (size_t i = 1; i < g_looks.size(); i++)
    text += "    " + LookJson(g_looks[i]) + (i + 1 < g_looks.size() ? ",\n" : "\n");
  text += "  ]\n}\n";
  FILE *f = fopen(path.c_str(), "wb");
  const bool bOk = f != NULL && fwrite(text.data(), 1, text.size(), f) == text.size();
  if (f != NULL)
    fclose(f);
  if (!bOk) {
    *pAnswer = "could not write " + path;
    return false;
  }
  g_source = path;
  *pAnswer = "\"path\":" + PortJsonQuote(path);
  return true;
}

// The built-in looks, the live values and the channel's commands, once.
void Init(void) {
  if (!g_looks.empty())
    return;
  Builtins();
  Reindex();
  g_source = "built in";
  PortControlFloat("looks", &g_fOn, 0.0f, 1.0f,
                   "the looks in full colour (1): a look's relief, transparency, shine, glow and sparkle, its light on "
                   "the ground and its plume; 0 leaves the recolour alone. GPU scene only",
                   1.0f);
  PortControlFloat("looks.steps", &g_fSteps, 0.0f, 1.0f,
                   "the light on the ground and the plume in steps with a dither, as 1.17 draws its smoke (1), or "
                   "smooth (0)",
                   1.0f);
  PortControlFloat("looks.clarity", &g_fClarity, 0.0f, 2.0f, "how much of the ground shows through a crystal; 1 as the look gives");
  PortControlFloat("looks.shine", &g_fShine, 0.0f, 2.0f, "how much a crystal reflects; 1 as the look gives");
  PortControlFloat("looks.glow", &g_fGlow, 0.0f, 2.0f, "how much a look glows from within; 1 as the look gives");
  PortControlFloat("looks.sparkle", &g_fSparkle, 0.0f, 2.0f, "how many stars catch the light on a crystal; 1 as the look gives");
  PortControlFloat("looks.plume", &g_fPlume, 0.0f, 2.0f, "how dense a plume is; 1 as the look gives");
  PortControlFloat("looks.light", &g_fLight, 0.0f, 2.0f, "how strongly a look lights the ground; 1 as the look gives");
  PortControlCommand("looks", CmdLooks);
  PortControlCommand("look", CmdLook);
  PortControlCommand("look_unit", CmdLookUnit);
  PortControlCommand("looks_save", CmdLooksSave);
}

void Lab(unsigned int dwRgb, float *pLab) {
  const float rgb[3] = {(dwRgb >> 16 & 0xFF) / 255.0f, (dwRgb >> 8 & 0xFF) / 255.0f, (dwRgb & 0xFF) / 255.0f};
  PortRgbToLab(rgb, pLab);
}

unsigned int Entry(int e) {
  return static_cast<unsigned int>(sgGamePalette_g[4 * e]) << 16 | static_cast<unsigned int>(sgGamePalette_g[4 * e + 1]) << 8 |
         sgGamePalette_g[4 * e + 2];
}

bool Takes(int nRule, unsigned int dwRgb) {
  const int r = dwRgb >> 16 & 0xFF, g = dwRgb >> 8 & 0xFF, b = dwRgb & 0xFF;
  // a crystal's blues, and its darkest outline, a little bluer than it is red or green
  if (nRule == RULE_BLUES)
    return (b > r + 24 && b + g > 2 * r + 40) || (b >= r + 12 && b >= g + 4);
  if (nRule == RULE_GREENS)
    return g > r + 16 && g > b + 6;
  return nRule == RULE_ALL;
}

// The entries a look may draw with: not the transparent one, the team colours or the
// colour-cycling ones a tileset animates.
bool Drawable(int e) {
  return e >= 16 && e < 248;
}

// Each look's remap and pen for the palette as it is now: each entry its rule takes goes to the
// palette entry nearest the ramp at its tone, the darkest it takes to the ramp's start and the
// lightest to its end, fitted in OKLab as the team colours are (port_teamcolor.cpp).
void Make(void) {
  if (g_bMade && memcmp(g_abPalette, sgGamePalette_g, sizeof g_abPalette) == 0)
    return;
  memcpy(g_abPalette, sgGamePalette_g, sizeof g_abPalette);
  g_bMade = true;
  float lab[256][3];
  for (int e = 0; e < 256; e++)
    Lab(Entry(e), lab[e]);
  auto nearest = [&lab](const float *p) {
    int nBest = 16;
    float fBest = 1e9f;
    for (int e = 0; e < 256; e++) {
      if (!Drawable(e))
        continue;
      const float d0 = lab[e][0] - p[0], d1 = lab[e][1] - p[1], d2 = lab[e][2] - p[2];
      const float d = d0 * d0 + d1 * d1 + d2 * d2;
      if (d < fBest) {
        fBest = d;
        nBest = e;
      }
    }
    return static_cast<BYTE>(nBest);
  };
  for (size_t i = 1; i < g_looks.size(); i++) {
    Look &l = g_looks[i];
    float ramp[RAMP][3];
    for (int k = 0; k < RAMP; k++)
      Lab(l.adwRamp[k], ramp[k]);
    float fLo = 1.0f, fHi = 0.0f;
    for (int e = 0; e < 256; e++) {
      l.abTakes[e] = e != 0 && (e < 8 || e > 15) && Takes(l.nRule, Entry(e));
      l.abRemap[e] = static_cast<BYTE>(e);
      l.abTone[e] = 0;
      if (l.abTakes[e]) {
        fLo = std::min(fLo, lab[e][0]);
        fHi = std::max(fHi, lab[e][0]);
      }
    }
    for (int e = 0; e < 256; e++) {
      if (!l.abTakes[e])
        continue;
      const float fTone = fHi > fLo ? (lab[e][0] - fLo) / (fHi - fLo) : 0.5f;
      const float t = fTone * (RAMP - 1);
      const int k = std::min(static_cast<int>(t), RAMP - 2);
      const float f = t - k;
      float at[3];
      for (int c = 0; c < 3; c++)
        at[c] = ramp[k][c] + (ramp[k + 1][c] - ramp[k][c]) * f;
      l.abRemap[e] = nearest(at);
      // the GPU's tone, a little toward the light as the design's is
      l.abTone[e] = static_cast<BYTE>(1 + lroundf(powf(fTone, 0.85f) * 254.0f));
    }
    l.bPen = nearest(ramp[3]);
  }
}

// A unit's look: given by hand, else by the provider, else by its type.
int LookOfUnit(const CUnit *pUnit) {
  const int n = static_cast<int>(pUnit - CUnit::Head());
  if (n < 0 || n >= g_pools.units)
    return 0;
  int nLook = g_abUnitLook[n];
  if (nLook == 0 && g_pfnProvider != NULL)
    nLook = g_pfnProvider(n, pUnit);
  if (nLook == 0 && pUnit->wType < UNIT_TYPES_MAX)
    nLook = g_abTypeLook[pUnit->wType];
  return nLook < static_cast<int>(g_looks.size()) ? nLook : 0;
}

bool Covers(const Look &l, int nImage) {
  return l.images.empty() || std::binary_search(l.images.begin(), l.images.end(), nImage);
}

const Looked *LookedOf(const CSprite *pSprite) {
  auto it = std::lower_bound(g_looked.begin(), g_looked.end(), pSprite,
                             [](const Looked &l, const CSprite *p) { return l.pSprite < p; });
  return it != g_looked.end() && it->pSprite == pSprite ? &*it : NULL;
}

// The top left of an image's canvas in the map layer's pixels: where its vents are measured from.
bool CanvasOf(const CSprite *pSprite, const CImage *pImage, int *pnX, int *pnY) {
  if (pImage == NULL || pImage->pGroup == NULL)
    return false;
  *pnX = static_cast<int>(pSprite->wPosX) + pImage->cHorzOffset - (pImage->pGroup->wMaxWidth >> 1) - glScrollPixelX;
  *pnY = static_cast<int>(pSprite->wPosY) + pImage->cVertOffset - (pImage->pGroup->wMaxHeight >> 1) - glScrollPixelY;
  return true;
}

unsigned int Pack16(int nLow, int nHigh) {
  return (static_cast<unsigned int>(nLow) & 0xFFFF) | (static_cast<unsigned int>(nHigh) << 16);
}

// The light a looked unit casts on the ground, recorded before the sprites so that only the
// ground under it takes it (scene.hlsl, LookPrim).
void Light(const Looked &looked, int nSeed) {
  const Look &l = g_looks[looked.nLook];
  const CImage *pMain = looked.pSprite->pMainImage;
  if (pMain == NULL || !Covers(l, pMain->wImageID) || l.afLight[1] <= 0.0f)
    return;
  const int x = static_cast<int>(looked.pSprite->wPosX) - glScrollPixelX + static_cast<int>(l.afLight[2]);
  const int y = static_cast<int>(looked.pSprite->wPosY) - glScrollPixelY + static_cast<int>(l.afLight[3]);
  const int r = static_cast<int>(l.afLight[0]);
  // to where the light is a hundredth of its middle
  const int w = r * 8 / 5, h = r * 8 * 55 / 500;
  PortSceneLook(x - w, y - h, 2 * w + 1, 2 * h + 1, Pack16(x, y), Pack16(r, lroundf(l.afLight[1] * 1000.0f)),
                static_cast<unsigned int>(looked.nLook) | (PORT_LOOK_LIGHT << 8) |
                    (static_cast<unsigned int>(nSeed & 0xFF) << 16));
}

void PutFloat(unsigned char *p, float f) {
  memcpy(p, &f, 4);
}

// How much 1.17's puffs swell the plume of the vent at x, y (the map layer's pixels), 0 to 255:
// a puff the plume replaces, playing on the sprite with its middle near the vent, swells it as
// it starts and lets it go as it plays out, so the puffs' own timing (their iscript's waitrand,
// part of the simulation) is the plume's rhythm.
int PuffSwell(const CSprite *pSprite, const Look &l, int x, int y) {
  float fSwell = 0.0f;
  for (const CImage *p = pSprite->pImageHead; p != NULL; p = p->pNext) {
    if (p->flags.b.Hidden || !std::binary_search(l.replaced.begin(), l.replaced.end(), static_cast<int>(p->wImageID)))
      continue;
    int px, py;
    if (!CanvasOf(pSprite, p, &px, &py))
      continue;
    // the puff's frame is drawn above its middle as it rises: from its foot
    const int dx = px + (p->pGroup->wMaxWidth >> 1) - x, dy = py + p->pGroup->wMaxHeight - y;
    if (dx * dx + dy * dy > 24 * 24)
      continue;
    // the group's frame count (grp.h: unmapped, as 1.17 never reads it)
    WORD wFrames;
    memcpy(&wFrames, p->pGroup, sizeof wFrames);
    const float t = wFrames > 1 ? std::min(1.0f, p->wFrameIndex / static_cast<float>(wFrames - 1)) : 0.0f;
    fSwell = std::max(fSwell, std::min(1.0f, t * 6.0f) * (1.0f - t * t));
  }
  return lroundf(fSwell * 255.0f);
}

}  // namespace

int PortLookFind(const char *pszName) {
  Init();
  for (size_t i = 1; i < g_looks.size(); i++)
    if (g_looks[i].name == pszName)
      return static_cast<int>(i);
  return 0;
}

const char *PortLookName(int nLook) {
  Init();
  return nLook > 0 && nLook < static_cast<int>(g_looks.size()) ? g_looks[nLook].name.c_str() : "";
}

void PortLookUnitSet(int nUnit, int nLook) {
  if (nUnit >= 0 && nUnit < POOL_UNITS_STORE)
    g_abUnitLook[nUnit] = static_cast<unsigned char>(nLook);
}

void PortLooksGameBegin(void) {
  Init();
  memset(g_abUnitLook, 0, sizeof g_abUnitLook);
  g_looked.clear();
  Builtins();
  ReadLooksFile();
  Reindex();
}

void PortLooksProvider(PFN_PORT_LOOK_OF_UNIT pfn) {
  g_pfnProvider = pfn;
}

void PortLooksPassBegin(void) {
  Init();
  g_looked.clear();
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
    const int nLook = p->pSprite != NULL ? LookOfUnit(p) : 0;
    if (nLook != 0 && nLook != PORT_LOOK_NONE) {
      const Looked l = {p->pSprite, nLook, p->wPositionX, p->wPositionY, p->wType, true};
      g_looked.push_back(l);
    }
  }
  // a looked resource under the fog keeps its look: its copy stands where it stood
  const size_t nUnits = g_looked.size();
  if (nUnits != 0)
    for (CThingy *p = CThingy::FirstFoggedThingy(); p != NULL; p = p->pNext) {
      if (p->pSprite == NULL)
        continue;
      for (size_t i = 0; i < nUnits; i++)
        if (g_looked[i].x == p->pSprite->wPosX && g_looked[i].y == p->pSprite->wPosY && g_looked[i].nType == p->wUnitType) {
          const Looked l = {p->pSprite, g_looked[i].nLook, g_looked[i].x, g_looked[i].y, g_looked[i].nType, false};
          g_looked.push_back(l);
          break;
        }
    }
  std::sort(g_looked.begin(), g_looked.end(), [](const Looked &a, const Looked &b) { return a.pSprite < b.pSprite; });
  if (g_looked.empty())
    return;
  Make();
  if (g_fOn != 0.0f && g_fLight > 0.0f)
    for (size_t i = 0; i < g_looked.size(); i++)
      Light(g_looked[i], static_cast<int>(i));
}

int PortLookOfSprite(const CSprite *pSprite) {
  if (g_looked.empty())
    return 0;
  const Looked *p = LookedOf(pSprite);
  return p != NULL ? p->nLook : 0;
}

bool PortLookImageBegin(int nLook, int nImage) {
  if (nLook <= 0 || nLook >= static_cast<int>(g_looks.size()))
    return false;
  const Look &l = g_looks[nLook];
  if (!Covers(l, nImage))
    return false;
  memcpy(g_abSaved, _gbRemapColorTbl, sizeof g_abSaved);
  for (int e = 0; e < 256; e++)
    if ((e < 8 || e > 15) && l.abTakes[_gbRemapColorTbl[e]])
      _gbRemapColorTbl[e] = l.abRemap[_gbRemapColorTbl[e]];
  if (g_fOn != 0.0f)
    g_nNow = static_cast<unsigned int>(nLook) |
             (std::binary_search(l.replaced.begin(), l.replaced.end(), nImage) ? PORT_LOOK_REPLACED : 0u);
  return true;
}

void PortLookImageEnd(void) {
  memcpy(_gbRemapColorTbl, g_abSaved, sizeof g_abSaved);
  g_nNow = 0;
}

unsigned int PortLookNow(void) {
  return g_nNow;
}

int PortLookHalo(unsigned int nNow) {
  const int nLook = static_cast<int>(nNow & 0xFF);
  if (g_fOn == 0.0f || (nNow & PORT_LOOK_REPLACED) != 0 || nLook <= 0 || nLook >= static_cast<int>(g_looks.size()))
    return 0;
  const Look &l = g_looks[nLook];
  // the design's gather reaches 4.5 pixels, a star's arms 2
  return l.nMaterial == MAT_CRYSTAL && l.afMat[2] * g_fGlow > 0.0f ? 6 : 0;
}

void PortLookSpriteDrawn(const CSprite *pSprite, int nLook) {
  if (g_fOn == 0.0f || g_fPlume <= 0.0f || nLook <= 0 || nLook >= static_cast<int>(g_looks.size()))
    return;
  const Look &l = g_looks[nLook];
  const Looked *pLooked = LookedOf(pSprite);
  const CImage *pMain = pSprite->pMainImage;
  int x0, y0;
  if (l.vents.empty() || pLooked == NULL || !pLooked->bUnit || !CanvasOf(pSprite, pMain, &x0, &y0))
    return;
  // a column from each vent, up to its height and as wide as it spreads and sways there
  const int nTop = static_cast<int>(l.afPlume[0]) + 12;
  const int nFrame = pMain->wFrameIndex;
  bool bFramed = false;
  for (size_t i = 0; i < l.vents.size(); i++)
    bFramed |= l.vents[i].nImage == pMain->wImageID && l.vents[i].nFrame == nFrame;
  for (size_t i = 0; i < l.vents.size(); i++) {
    const Vent &v = l.vents[i];
    if (v.nImage != pMain->wImageID || v.nFrame != (bFramed ? nFrame : -1))
      continue;
    const int x = x0 + static_cast<int>(v.x), y = y0 + static_cast<int>(v.y);
    // a swollen column rises higher and spreads wider
    const int nSwell = PuffSwell(pSprite, l, x, y);
    const int nHigh = nTop + nTop * nSwell / 512;
    const int w = static_cast<int>(v.fWidth * (1.0f + nHigh * 0.075f) * (1.0f + nSwell / 512.0f)) + nHigh / 4 + 6;
    PortSceneLook(x - w, y - nHigh, 2 * w + 1, nHigh + 4, Pack16(x, y),
                  Pack16(lroundf(v.fStrength * 1000.0f), lroundf(v.fWidth * 100.0f)),
                  static_cast<unsigned int>(nLook) | (PORT_LOOK_PLUME << 8) | (static_cast<unsigned int>(i) << 16) |
                      (static_cast<unsigned int>(nSwell) << 24));
  }
}

const unsigned char *PortLooksGpu(unsigned int *pnBytes) {
  *pnBytes = 0;
  if (g_fOn == 0.0f || g_looks.size() < 2 || !g_bMade)
    return NULL;
  const size_t nLooks = g_looks.size() - 1;
  g_gpu.assign(nLooks * PORT_LOOK_GPU_BYTES, 0);
  for (size_t i = 0; i < nLooks; i++) {
    const Look &l = g_looks[i + 1];
    unsigned char *p = &g_gpu[i * PORT_LOOK_GPU_BYTES];
    memcpy(p, l.abTone, 256);
    for (int k = 0; k < RAMP; k++) {
      PutFloat(p + 256 + k * 16, (l.adwRamp[k] >> 16 & 0xFF) / 255.0f);
      PutFloat(p + 260 + k * 16, (l.adwRamp[k] >> 8 & 0xFF) / 255.0f);
      PutFloat(p + 264 + k * 16, (l.adwRamp[k] & 0xFF) / 255.0f);
    }
    PutFloat(p + 352, std::min(1.0f, l.afMat[0] * g_fClarity));
    PutFloat(p + 356, std::min(1.0f, l.afMat[1] * g_fShine));
    PutFloat(p + 360, std::min(1.0f, l.afMat[2] * g_fGlow));
    PutFloat(p + 364, std::min(1.0f, l.afMat[3] * g_fSparkle));
    PutFloat(p + 368, l.afPlume[0]);
    PutFloat(p + 372, l.afPlume[1] * g_fPlume);
    PutFloat(p + 376, l.afPlume[2]);
    PutFloat(p + 384, (l.dwLight >> 16 & 0xFF) / 255.0f);
    PutFloat(p + 388, (l.dwLight >> 8 & 0xFF) / 255.0f);
    PutFloat(p + 392, (l.dwLight & 0xFF) / 255.0f);
    PutFloat(p + 396, 0.55f * g_fLight);
    const unsigned int anFlags[2] = {l.nMaterial == MAT_GAS ? 1u : 0u, g_fSteps != 0.0f ? 1u : 0u};
    memcpy(p + 400, anFlags, sizeof anFlags);
  }
  *pnBytes = static_cast<unsigned int>(g_gpu.size());
  return g_gpu.data();
}

unsigned char PortLookUnitPen(const CUnit *pUnit, unsigned char bColour) {
  Init();
  const int nLook = LookOfUnit(pUnit);
  if (nLook <= 0 || nLook == PORT_LOOK_NONE)
    return bColour;
  Make();
  return g_looks[nLook].bPen;
}

unsigned char PortLookPlacePen(int x, int y, int nType, unsigned char bColour) {
  for (const Looked &l : g_looked)
    if (l.bUnit && l.x == x && l.y == y && l.nType == nType)
      return g_looks[l.nLook].bPen;
  return bColour;
}
