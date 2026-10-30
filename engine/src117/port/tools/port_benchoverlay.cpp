// port_benchoverlay.cpp -- what a benchmark run's overlay shows and where.
//
// The overlay is a layout (the panels, a readout in a corner, a strip along an edge, or
// nothing), how much of it shows (full, compact, the figures only, or the boxes ticked), which
// figures a readout or a strip carries, and how see-through its panels are. With the picture
// (in a window, full screen, none) it makes the run's screen, and the set-up names the seven
// the design offers as its Screen styles.
//
// A run records the overlay it began with in settings.json, and is held only against runs
// under the same one; O during a run goes to the next view, each change noted with its game
// frame for summary.json. Presentation only: the overlay never reaches the game.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "port_benchoverlay.h"
#include "../front/port_text.h"
#include "../platform/port_json.h"
#include "../platform/port_globals117.h"

namespace {

const char *const kLayoutIds[4] = {"panels", "readout", "strip", "off"};
const char *const kCornerIds[4] = {"tl", "tr", "bl", "br"};
const char *const kEdgeIds[2] = {"top", "bottom"};
const char *const kDensityIds[4] = {"full", "compact", "figures", "own"};
const char *const kShowIds[6] = {"players", "selection", "settings", "graph", "parts", "minimap"};
const char *const kFigureIds[7] = {"fps", "low", "ms", "gfps", "units", "clock", "vs"};

// The screen styles, in the set-up's order (bench.js STYLES): the picture (0 in a window, 1 full
// screen, 2 none), the layout, large or not.
struct Style {
  const char *pszId, *pszName;
  int nShown, nLayout;
  bool bLarge;
};
const Style kStyles[PORT_BENCH_STYLES] = {
    {"full", PT_("Full overlay"), 1, 0, false},
    {"large", PT_("Large readings"), 1, 1, true},
    {"minimal", PT_("Minimal"), 1, 3, false},
    {"readout", PT_("Full screen, readout"), 1, 1, false},
    {"strip", PT_("Full screen, strip"), 1, 2, false},
    {"window", PT_("In a window"), 0, 0, false},
    {"hidden", PT_("No picture"), 2, 0, false},
};

int IndexOf(const char *const *ppIds, int nIds, const char *psz, int nDefault)
{
  for (int i = 0; i < nIds; ++i)
    if (_stricmp(psz, ppIds[i]) == 0)
      return i;
  return nDefault;
}

// "a,b,c" of the ids into bits; "none" none; "" the default
unsigned BitsOf(const char *const *ppIds, int nIds, const char *psz, unsigned nDefault)
{
  if (psz == NULL || psz[0] == '\0')
    return nDefault;
  if (_stricmp(psz, "none") == 0)
    return 0;
  unsigned n = 0;
  std::string word;
  for (const char *p = psz;; ++p) {
    if (*p == ',' || *p == '\0') {
      for (int i = 0; i < nIds; ++i)
        if (_stricmp(word.c_str(), ppIds[i]) == 0)
          n |= 1u << i;
      word.clear();
      if (*p == '\0')
        break;
    } else if (*p != ' ') {
      word += *p;
    }
  }
  return n;
}

std::string ListOf(const char *const *ppIds, int nIds, unsigned nBits)
{
  std::string s;
  for (int i = 0; i < nIds; ++i)
    if (nBits & (1u << i))
      s += (s.empty() ? "" : ",") + std::string(ppIds[i]);
  return s;
}

// The live overlay during a run, and its changes.
PortBenchOverlay g_live;
std::vector<PortBenchOverlayChange> g_changes;

}  // namespace

unsigned PortBenchOverlayShows(const PortBenchOverlay &o)
{
  // DENSITY_SHOW: full all six, compact the selection, the graph and the minimap, figures none
  switch (o.nDensity) {
    case 0: return PORT_OV_ALL;
    case 1: return PORT_OV_SELECTION | PORT_OV_GRAPH | PORT_OV_MINIMAP;
    case 2: return 0;
    default: return o.nShow & PORT_OV_ALL;
  }
}

int PortBenchOverlaySee(const PortBenchOverlay &o, int nShown)
{
  return o.nSee >= 0 ? o.nSee : nShown == 1 ? 50 : 0;
}

int PortBenchStyleOf(int nShown, const PortBenchOverlay &o)
{
  if (nShown == 2)
    return PORT_BENCH_STYLES - 1;
  for (int i = 0; i < PORT_BENCH_STYLES - 1; ++i) {
    const Style &s = kStyles[i];
    if (s.nShown == nShown && s.nLayout == o.nLayout && (o.nLayout != 1 || s.bLarge == o.bLarge))
      return i;
  }
  return -1;
}

const char *PortBenchStyleName(int nStyle)
{
  return nStyle >= 0 && nStyle < PORT_BENCH_STYLES ? PortText(kStyles[nStyle].pszName) : "";
}

const char *PortBenchStyleId(int nStyle)
{
  return nStyle >= 0 && nStyle < PORT_BENCH_STYLES ? kStyles[nStyle].pszId : "";
}

void PortBenchStyleApply(int nStyle, int *pnShown, PortBenchOverlay *pOverlay)
{
  if (nStyle < 0 || nStyle >= PORT_BENCH_STYLES)
    return;
  const Style &s = kStyles[nStyle];
  const int nWasShown = *pnShown;
  *pnShown = s.nShown;
  pOverlay->nLayout = s.nLayout;
  pOverlay->bLarge = s.bLarge;
  // see-through left at the old picture's own follows the new picture's (bench.js)
  if (nWasShown != s.nShown && pOverlay->nSee == (nWasShown == 1 ? 50 : 0))
    pOverlay->nSee = -1;
}

// What the design calls a combination the styles do not name: "Window, readout" and so on.
std::string PortBenchScreenName(int nShown, const PortBenchOverlay &o)
{
  const int n = PortBenchStyleOf(nShown, o);
  if (n >= 0)
    return PortText(kStyles[n].pszName);
  static const char *const kWindow[4] = {PT_("In a window"), PT_("Window, readout"), PT_("Window, strip"),
                                         PT_("Window, nothing")};
  if (nShown == 0)
    return o.nLayout == 1 && o.bLarge ? PortText("Window, large readings") : PortText(kWindow[o.nLayout & 3]);
  return PortText("Full screen");
}

const char *PortBenchOverlayLayoutId(const PortBenchOverlay &o)
{
  return kLayoutIds[o.nLayout & 3];
}

void PortBenchOverlayFromOptions(const PortOptions *pOpt, PortBenchOverlay *pOut)
{
  PortBenchOverlay o;
  o.nLayout = IndexOf(kLayoutIds, 4, pOpt->szOverlay, 0);
  if (o.nLayout == 1)
    o.nCorner = IndexOf(kCornerIds, 4, pOpt->szOverlayAt, 1);
  else if (o.nLayout == 2)
    o.nEdge = IndexOf(kEdgeIds, 2, pOpt->szOverlayAt, 1);
  o.bLarge = _stricmp(pOpt->szOverlaySize, "large") == 0;
  o.nDensity = IndexOf(kDensityIds, 4, pOpt->szOverlayDensity, 0);
  if (pOpt->szOverlayShow[0] != '\0') {
    o.nShow = BitsOf(kShowIds, 6, pOpt->szOverlayShow, PORT_OV_ALL);
    o.nDensity = 3;
  }
  o.nFigures = BitsOf(kFigureIds, 7, pOpt->szOverlayFigures, PORT_FIG_ALL);
  if (o.nFigures == 0)
    o.nFigures = PORT_FIG_FPS;
  o.nSee = pOpt->szOverlaySee[0] != '\0' ? atoi(pOpt->szOverlaySee) : -1;
  *pOut = o;
}

std::string PortBenchOverlayArgs(int nShown, const PortBenchOverlay &o)
{
  if (nShown == 2)
    return "";
  std::string s;
  if (o.nLayout != 0)
    s += std::string(" --overlay ") + kLayoutIds[o.nLayout & 3];
  if (o.nLayout == 1)
    s += std::string(" --overlay-at ") + kCornerIds[o.nCorner & 3] + (o.bLarge ? " --overlay-size large" : "");
  else if (o.nLayout == 2)
    s += std::string(" --overlay-at ") + kEdgeIds[o.nEdge & 1];
  if (o.nLayout != 3) {
    if (o.nDensity == 1 || o.nDensity == 2)
      s += std::string(" --overlay-density ") + kDensityIds[o.nDensity];
    else if (o.nDensity == 3)
      s += " --overlay-show " + (o.nShow & PORT_OV_ALL ? ListOf(kShowIds, 6, o.nShow) : std::string("none"));
    if (o.nLayout != 0 && (o.nFigures & PORT_FIG_ALL) != PORT_FIG_ALL)
      s += " --overlay-figures " + ListOf(kFigureIds, 7, o.nFigures);
  }
  if (o.nSee >= 0 && o.nSee != (nShown == 1 ? 50 : 0))
    s += " --overlay-see " + std::to_string(o.nSee);
  return s;
}

std::string PortBenchOverlayJson(int nShown, const PortBenchOverlay &o)
{
  char sz[512];
  snprintf(sz, sizeof sz,
           "{\"layout\": \"%s\", \"size\": \"%s\", \"corner\": \"%s\", \"edge\": \"%s\", \"density\": \"%s\", "
           "\"show\": [%s], \"figures\": [%s], \"see\": %.2f}",
           kLayoutIds[o.nLayout & 3], o.bLarge ? "large" : "small", kCornerIds[o.nCorner & 3], kEdgeIds[o.nEdge & 1],
           kDensityIds[o.nDensity & 3], ("\"" + ListOf(kShowIds, 6, PortBenchOverlayShows(o)) + "\"").c_str(),
           ("\"" + ListOf(kFigureIds, 7, o.nFigures) + "\"").c_str(), PortBenchOverlaySee(o, nShown) / 100.0);
  // the lists as JSON arrays of words
  std::string s = sz;
  for (const char *pszKey : {"\"show\": [\"", "\"figures\": [\""}) {
    const size_t at = s.find(pszKey);
    if (at == std::string::npos)
      continue;
    const size_t from = at + strlen(pszKey) - 1, to = s.find("\"]", from + 1);
    std::string list = s.substr(from + 1, to - from - 1), out;
    for (size_t i = 0; i < list.size(); ++i)
      out += list[i] == ',' ? std::string("\", \"") : std::string(1, list[i]);
    s = s.substr(0, from) + (list.empty() ? "" : "\"" + out + "\"") + s.substr(to + 1);
  }
  return s;
}

void PortBenchOverlayFromJson(const PortJson &j, PortBenchOverlay *pOut)
{
  PortBenchOverlay o;
  if (j.kind != PortJson::OBJECT) {
    *pOut = o;
    return;
  }
  o.nLayout = IndexOf(kLayoutIds, 4, j["layout"].Str("panels").c_str(), 0);
  o.bLarge = j["size"].Str() == "large";
  o.nCorner = IndexOf(kCornerIds, 4, j["corner"].Str("tr").c_str(), 1);
  o.nEdge = IndexOf(kEdgeIds, 2, j["edge"].Str("bottom").c_str(), 1);
  o.nDensity = IndexOf(kDensityIds, 4, j["density"].Str("full").c_str(), 0);
  auto bits = [](const PortJson &a, const char *const *ppIds, int nIds, unsigned nDefault) {
    if (a.kind != PortJson::ARRAY)
      return nDefault;
    unsigned n = 0;
    for (int i = 0; i < a.size(); ++i)
      for (int k = 0; k < nIds; ++k)
        if (a.At(i).Str() == ppIds[k])
          n |= 1u << k;
    return n;
  };
  o.nShow = bits(j["show"], kShowIds, 6, PORT_OV_ALL);
  o.nFigures = bits(j["figures"], kFigureIds, 7, PORT_FIG_ALL);
  o.nSee = j["see"].kind == PortJson::NUMBER ? static_cast<int>(j["see"].d * 100.0 + 0.5) : -1;
  *pOut = o;
}

std::string PortBenchOverlayKey(int nShown, const PortBenchOverlay &o)
{
  // the same game measured the same way: the layout and its size, what shows, the see-through
  // over the game (bench.js's sameOverlay)
  std::string s = std::string(kLayoutIds[o.nLayout & 3]) + (o.nLayout == 1 && o.bLarge ? "-large" : "");
  if (o.nLayout != 3)
    s += ":" + ListOf(kShowIds, 6, PortBenchOverlayShows(o));
  if (nShown == 1 && o.nLayout != 3)
    s += ":" + std::to_string(PortBenchOverlaySee(o, nShown));
  return s;
}

void PortBenchOverlayBegin(const PortBenchOverlay &o)
{
  g_live = o;
  g_changes.clear();
}

PortBenchOverlay &PortBenchOverlayLive(void)
{
  return g_live;
}

void PortBenchOverlayChanged(const char *pszHow)
{
  PortBenchOverlayChange c;
  c.nFrame = oc_dwReplayFrame;
  c.overlay = g_live;
  lstrcpynA(c.szHow, pszHow, sizeof c.szHow);
  g_changes.push_back(c);
}

int PortBenchOverlayChanges(const PortBenchOverlayChange **ppChanges)
{
  *ppChanges = g_changes.empty() ? NULL : g_changes.data();
  return static_cast<int>(g_changes.size());
}

std::string PortBenchOverlayViewName(const PortBenchOverlay &o)
{
  // the summary's words for a view: "large readings", "the full overlay", ...
  switch (o.nLayout) {
    case 0: return "the full overlay";
    case 1: return o.bLarge ? "large readings" : "a readout";
    case 2: return "a strip";
    default: return "nothing";
  }
}
