// port_lights.cpp -- the lights port_lights.h describes: the table, its file, and the lights of
// each map pass.
//
// The built-in table is Hyperion's own choice of images, by their place in images.dat and the
// name of their GRP in images.tbl (each entry's comment), with colours and reaches picked by eye
// on Debug Tools' canvas. lights.json in the data folder replaces it whole:
//
//   {"lights": [{"image": 332, "name": "explosion, small", "color": "#ffa040",
//                "radius": 48, "intensity": 1.2, "flicker": 0.25}, ...]}
//
// radius is in game pixels; intensity scales the colour; flicker, 0 to 1, how much it wavers.
// hyperion_lights.log says which table was read.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "../platform/port_paths.h"  // PortDataFolder
#include "../platform/port_log.h"  // PortLogger
#include "../platform/port_json.h"
#include "port_lights.h"
#include "../../gen/globals117.h"  // gwGameMode, glScrollPixelX, glScrollPixelY

namespace {

enum { IMAGES = 999 };

struct Entry {
  int nImage;
  std::string name;
  float fR, fG, fB;
  float fRadius, fIntensity, fFlicker;
};

// Colours, as the table writes them.
const char *const kOrange = "#ffa040";   // explosions
const char *const kFire = "#ff7a28";     // flames
const char *const kFlash = "#ffd890";    // muzzle flashes, sparks
const char *const kPsi = "#88b8ff";      // protoss energy
const char *const kEngineP = "#7fb0ff";  // protoss engines
const char *const kEngineT = "#ffb070";  // terran engines

struct Default {
  int nImage;
  const char *pszName;
  const char *pszColor;
  float fRadius, fIntensity, fFlicker;
};

const Default kDefaults[] = {
    // explosions
    {213, "explosion, small (tBangS)", kOrange, 48, 1.2f, 0.25f},
    {214, "explosion, large (tBangL)", kOrange, 80, 1.4f, 0.25f},
    {215, "explosion, extra large (tBangX)", kOrange, 128, 1.6f, 0.25f},
    {332, "explosion, small (tBangS)", kOrange, 48, 1.2f, 0.25f},
    {333, "explosion, large (tBangL)", kOrange, 80, 1.4f, 0.25f},
    {334, "explosion, extra large (tBangX)", kOrange, 128, 1.6f, 0.25f},
    {425, "explosion (explo2)", kOrange, 64, 1.3f, 0.25f},
    {426, "double explosion (dbl_exp)", kOrange, 96, 1.5f, 0.25f},
    {427, "rocket explosion (HKexplod)", kOrange, 56, 1.2f, 0.25f},
    {442, "rocket explosion (HKexplod)", kOrange, 56, 1.2f, 0.25f},
    {428, "explosion, little (small)", kOrange, 32, 1.0f, 0.25f},
    {429, "mine explosion (tmnExplo)", kOrange, 64, 1.3f, 0.25f},
    {444, "protoss explosion (psaExplo)", kPsi, 64, 1.3f, 0.25f},
    {318, "nuclear blast (NukeHit)", "#fff0c8", 220, 2.0f, 0.15f},
    {543, "Yamato blast (eycBlast)", "#ffc070", 120, 1.6f, 0.2f},
    {541, "Yamato bolt (eycBull)", "#ffc070", 48, 1.2f, 0.1f},
    {542, "Yamato bolt (eycBull)", "#ffc070", 48, 1.2f, 0.1f},
    // energy
    {134, "Archon (archon)", "#a8c8ff", 96, 1.2f, 0.15f},
    {925, "Dark Archon (darchn)", "#c088ff", 96, 1.1f, 0.15f},
    {525, "psionic storm (psiStorm)", "#6a90ff", 112, 1.0f, 0.35f},
    {555, "EMP shockwave (emp)", "#90c0ff", 120, 1.2f, 0.2f},
    {556, "EMP (empl)", "#90c0ff", 60, 1.0f, 0.2f},
    {419, "plasma (plasma)", kPsi, 40, 1.1f, 0.15f},
    {520, "phase disruptor (ephFire)", kPsi, 36, 1.1f, 0.1f},
    {523, "Dragoon bolt (dragbull)", kPsi, 36, 1.1f, 0.1f},
    {524, "photon bolt (epbBul)", kPsi, 30, 1.0f, 0.1f},
    {519, "protoss spark (pspark)", kPsi, 24, 0.9f, 0.3f},
    // fire
    {421, "flame (flamer)", kFire, 64, 1.3f, 0.4f},
    {450, "building fire (oFireC)", kFire, 40, 1.0f, 0.45f},
    {451, "building fire (oFireF)", kFire, 40, 1.0f, 0.45f},
    {452, "building fire (oFireV)", kFire, 40, 1.0f, 0.45f},
    {453, "building fire (oFireV)", kFire, 40, 1.0f, 0.45f},
    {454, "building fire (oFireV)", kFire, 40, 1.0f, 0.45f},
    {455, "building fire (oFireV)", kFire, 40, 1.0f, 0.45f},
    {456, "building fire (oFireV)", kFire, 40, 1.0f, 0.45f},
    {457, "building fire (oFireV)", kFire, 40, 1.0f, 0.45f},
    {466, "building fire (oFireC)", kFire, 40, 1.0f, 0.45f},
    {467, "building fire (oFireF)", kFire, 40, 1.0f, 0.45f},
    {468, "building fire (oFireV)", kFire, 40, 1.0f, 0.45f},
    {469, "building fire (oFireC)", kFire, 40, 1.0f, 0.45f},
    {470, "building fire (oFireF)", kFire, 40, 1.0f, 0.45f},
    {471, "building fire (oFireV)", kFire, 40, 1.0f, 0.45f},
    {472, "building fire (oFireC)", kFire, 40, 1.0f, 0.45f},
    {473, "building fire (oFireF)", kFire, 40, 1.0f, 0.45f},
    {474, "building fire (oFireV)", kFire, 40, 1.0f, 0.45f},
    {475, "building fire (oFireV)", kFire, 40, 1.0f, 0.45f},
    {476, "building fire (oFireV)", kFire, 40, 1.0f, 0.45f},
    {477, "building fire (oFireV)", kFire, 40, 1.0f, 0.45f},
    {478, "building fire (oFireV)", kFire, 40, 1.0f, 0.45f},
    {479, "building fire (oFireV)", kFire, 40, 1.0f, 0.45f},
    {488, "building fire (oFireC)", kFire, 40, 1.0f, 0.45f},
    {489, "building fire (oFireF)", kFire, 40, 1.0f, 0.45f},
    {490, "building fire (oFireV)", kFire, 40, 1.0f, 0.45f},
    {491, "building fire (oFireC)", kFire, 40, 1.0f, 0.45f},
    {492, "building fire (oFireF)", kFire, 40, 1.0f, 0.45f},
    {493, "building fire (oFireV)", kFire, 40, 1.0f, 0.45f},
    // flashes, sparks, missiles
    {448, "muzzle flash (elbMuzz)", kFlash, 40, 1.1f, 0.3f},
    {518, "muzzle flash (eplMuzz)", kFlash, 40, 1.1f, 0.3f},
    {536, "tank flash (ettFlash)", kFlash, 52, 1.3f, 0.3f},
    {537, "siege fire (esiFire)", kOrange, 48, 1.2f, 0.3f},
    {527, "terran spark (tspark)", kFlash, 24, 0.9f, 0.4f},
    {526, "welding spark (scvspark)", "#c8e0ff", 20, 0.9f, 0.5f},
    {529, "missile (missile)", "#ffb060", 28, 0.9f, 0.2f},
    {554, "missile (missile)", "#ffb060", 28, 0.9f, 0.2f},
    {965, "missile (SMmissle)", "#ffb060", 28, 0.9f, 0.2f},
    {528, "missile (gemini)", "#ffb060", 28, 0.9f, 0.2f},
    {522, "missile (blastcan)", "#ffb060", 28, 0.9f, 0.2f},
    {976, "optical flare (TmeFlshS)", "#ffffff", 40, 1.2f, 0.1f},
    {977, "optical flare (TmeFlshM)", "#ffffff", 60, 1.2f, 0.1f},
    {978, "optical flare (TmeFlshL)", "#ffffff", 80, 1.2f, 0.1f},
    // engine glows
    {114, "Carrier engines (pcaGlow)", kEngineP, 24, 0.8f, 0.1f},
    {120, "Shuttle engines (pshGlow)", kEngineP, 20, 0.8f, 0.1f},
    {132, "Arbiter engines (pabGlow)", kEngineP, 24, 0.8f, 0.1f},
    {142, "Scout engines (pscGlow)", kEngineP, 20, 0.8f, 0.1f},
    {931, "Corsair engines (pcsGlow)", kEngineP, 20, 0.8f, 0.1f},
    {220, "Battlecruiser engines (tbaGlow)", kEngineT, 28, 0.8f, 0.1f},
    {225, "Dropship engines (tdrGlow)", kEngineT, 20, 0.8f, 0.1f},
    {249, "Science Vessel engines (tscGlow)", kEngineT, 20, 0.8f, 0.1f},
    {941, "Valkyrie engines (tbmGlow)", kEngineT, 20, 0.8f, 0.1f},
    {942, "Valkyrie engines (tbmGlow)", kEngineT, 20, 0.8f, 0.1f},
};

std::vector<Entry> g_table;
short g_anIndex[IMAGES];  // image -> entry, or -1
bool g_bRead;
std::string g_source = "built in";

// The pass being collected, and the last one done.
struct Lit {
  short nEntry;
  int nX, nY;  // map pixels
};
std::vector<Lit> g_pass;
std::vector<Lit> g_done;
LONG g_nScrollX, g_nScrollY;  // the view when the pass was done

const PortLogger Log("hyperion_lights.log");

bool ParseColor(const std::string &text, float *pR, float *pG, float *pB) {
  unsigned int v = 0;
  if (text.size() != 7 || text[0] != '#' || sscanf_s(text.c_str() + 1, "%6x", &v) != 1)
    return false;
  *pR = ((v >> 16) & 255) / 255.0f;
  *pG = ((v >> 8) & 255) / 255.0f;
  *pB = (v & 255) / 255.0f;
  return true;
}

std::string ColorText(const Entry &e) {
  char sz[8];
  sprintf_s(sz, "#%02x%02x%02x", static_cast<int>(e.fR * 255.0f + 0.5f), static_cast<int>(e.fG * 255.0f + 0.5f),
            static_cast<int>(e.fB * 255.0f + 0.5f));
  return sz;
}

void Reindex(void) {
  memset(g_anIndex, 0xFF, sizeof g_anIndex);
  for (size_t i = 0; i < g_table.size(); ++i)
    if (g_table[i].nImage >= 0 && g_table[i].nImage < IMAGES)
      g_anIndex[g_table[i].nImage] = static_cast<short>(i);
}

std::string FilePath(void) {
  return std::string(PortDataFolder()) + "lights.json";
}

// The fields of an entry a message or the file gives; the rest stay as they are.
bool ReadEntry(const PortJson &j, Entry *pEntry, std::string *pError) {
  if (j["name"].kind == PortJson::STRING)
    pEntry->name = j["name"].s;
  if (j["color"].kind == PortJson::STRING && !ParseColor(j["color"].s, &pEntry->fR, &pEntry->fG, &pEntry->fB)) {
    *pError = "color is \"#rrggbb\"";
    return false;
  }
  if (j["radius"].kind == PortJson::NUMBER)
    pEntry->fRadius = static_cast<float>(j["radius"].d < 1 ? 1 : j["radius"].d > 512 ? 512 : j["radius"].d);
  if (j["intensity"].kind == PortJson::NUMBER)
    pEntry->fIntensity = static_cast<float>(j["intensity"].d < 0 ? 0 : j["intensity"].d > 8 ? 8 : j["intensity"].d);
  if (j["flicker"].kind == PortJson::NUMBER)
    pEntry->fFlicker = static_cast<float>(j["flicker"].d < 0 ? 0 : j["flicker"].d > 1 ? 1 : j["flicker"].d);
  return true;
}

void Read(void) {
  if (g_bRead)
    return;
  for (const Default &d : kDefaults) {
    Entry e = {d.nImage, d.pszName, 1, 1, 1, d.fRadius, d.fIntensity, d.fFlicker};
    ParseColor(d.pszColor, &e.fR, &e.fG, &e.fB);
    g_table.push_back(e);
  }
  const std::string path = FilePath();
  HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  if (h != INVALID_HANDLE_VALUE) {
    std::string text(GetFileSize(h, NULL), '\0');
    DWORD cb = 0;
    ReadFile(h, &text[0], static_cast<DWORD>(text.size()), &cb, NULL);
    CloseHandle(h);
    PortJson j;
    std::string error;
    if (PortJsonParse(text.c_str(), &j, &error) && j["lights"].kind == PortJson::ARRAY) {
      std::vector<Entry> table;
      for (size_t i = 0; i < j["lights"].size(); ++i) {
        const PortJson &l = j["lights"].At(i);
        Entry e = {static_cast<int>(l["image"].Num(-1)), "", 1, 1, 1, 32, 1, 0};
        if (e.nImage >= 0 && e.nImage < IMAGES && ReadEntry(l, &e, &error))
          table.push_back(e);
      }
      g_table = table;
      g_source = path;
    } else {
      Log("lights: lights.json does not read, the built-in table is used: %s", error.c_str());
    }
  }
  Reindex();
  Log("lights: the table from %s", g_source.c_str());
  g_bRead = true;
}

// How much a light wavers this moment, 1 when it does not: two slow waves at its own
// phase, from where it is and what it is.
float Flicker(const Entry &e, int nX, int nY, double dSeconds) {
  if (e.fFlicker <= 0.0f)
    return 1.0f;
  const unsigned int nSeed = static_cast<unsigned int>(e.nImage * 7919 + (nX >> 4) * 104729 + (nY >> 4) * 1299709);
  const double dPhase = (nSeed % 1000) * 0.00628;
  const double dWave = 0.5 + 0.25 * sin(dSeconds * 11.0 + dPhase) + 0.25 * sin(dSeconds * 23.7 + dPhase * 1.7);
  return 1.0f - e.fFlicker * static_cast<float>(dWave);
}

}  // namespace

// ---------------------------------------------------------------------------
// From the game: each map pass's images (CSprite::DrawAll, CSprite::Draw).

void PortLightsBegin(void) {
  Read();
  g_pass.clear();
}

void PortLightImage(unsigned int nImage, int nMapX, int nMapY) {
  if (nImage >= IMAGES || g_anIndex[nImage] < 0 || g_pass.size() >= PORT_LIGHTS_MAX)
    return;
  g_pass.push_back({g_anIndex[nImage], nMapX, nMapY});
}

void PortLightsEnd(void) {
  g_done.swap(g_pass);
  g_nScrollX = glScrollPixelX;
  g_nScrollY = glScrollPixelY;
}

// ---------------------------------------------------------------------------

int PortLightsFrame(PortLightGpu *pOut, int nMax) {
  if (gwGameMode != 3 || !g_bRead)
    return 0;
  const double dSeconds = GetTickCount64() / 1000.0;
  int n = 0;
  for (const Lit &l : g_done) {
    if (n >= nMax || static_cast<size_t>(l.nEntry) >= g_table.size())
      break;
    const Entry &e = g_table[l.nEntry];
    PortLightGpu &g = pOut[n++];
    g.fX = static_cast<float>(l.nX - g_nScrollX);
    g.fY = static_cast<float>(l.nY - g_nScrollY);
    g.fRadius = e.fRadius;
    g.fIntensity = e.fIntensity * Flicker(e, l.nX, l.nY, dSeconds);
    g.fR = e.fR;
    g.fG = e.fG;
    g.fB = e.fB;
    g.fPad = 0.0f;
  }
  return n;
}

std::string PortLightsJson(void) {
  Read();
  std::string s = "\"source\":" + PortJsonQuote(g_source) + ",\"frame\":" + std::to_string(g_done.size()) + ",\"lights\":[";
  for (size_t i = 0; i < g_table.size(); ++i) {
    const Entry &e = g_table[i];
    char sz[160];
    sprintf_s(sz, ",\"color\":\"%s\",\"radius\":%g,\"intensity\":%g,\"flicker\":%g}", ColorText(e).c_str(),
              e.fRadius, e.fIntensity, e.fFlicker);
    s += std::string(i ? "," : "") + "{\"image\":" + std::to_string(e.nImage) + ",\"name\":" + PortJsonQuote(e.name) + sz;
  }
  return s + "]";
}

bool PortLightSet(const PortJson &j, std::string *pError) {
  Read();
  const int nImage = static_cast<int>(j["image"].Num(-1));
  if (nImage < 0 || nImage >= IMAGES) {
    *pError = "light needs image, 0 to 998 (its place in images.dat)";
    return false;
  }
  const short nAt = g_anIndex[nImage];
  if (j["remove"].kind == PortJson::BOOL && j["remove"].b) {
    if (nAt >= 0) {
      g_table.erase(g_table.begin() + nAt);
      g_done.clear();  // its places in the table moved
      Reindex();
    }
    return true;
  }
  Entry e = nAt >= 0 ? g_table[nAt] : Entry{nImage, "image " + std::to_string(nImage), 1, 1, 1, 48, 1, 0};
  if (!ReadEntry(j, &e, pError))
    return false;
  if (nAt >= 0) {
    g_table[nAt] = e;
  } else {
    g_table.push_back(e);
    Reindex();
  }
  return true;
}

bool PortLightsSave(std::string *pPath) {
  Read();
  *pPath = FilePath();
  std::string text = "{\n  \"lights\": [\n";
  for (size_t i = 0; i < g_table.size(); ++i) {
    const Entry &e = g_table[i];
    char sz[200];
    sprintf_s(sz, "\"color\": \"%s\", \"radius\": %g, \"intensity\": %g, \"flicker\": %g}", ColorText(e).c_str(),
              e.fRadius, e.fIntensity, e.fFlicker);
    text += "    {\"image\": " + std::to_string(e.nImage) + ", \"name\": " + PortJsonQuote(e.name) + ", " + sz +
            (i + 1 < g_table.size() ? ",\n" : "\n");
  }
  text += "  ]\n}\n";
  HANDLE h = CreateFileA(pPath->c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE)
    return false;
  DWORD cb = 0;
  const bool bOk = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &cb, NULL) && cb == text.size();
  CloseHandle(h);
  if (bOk)
    g_source = *pPath;
  return bOk;
}
