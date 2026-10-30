// port_benchoverlay.h -- a benchmark run's overlay: its styles, flags, JSON and changes
// (port_benchoverlay.cpp). The overlay itself, PortBenchOverlay, is in port_bench.h.
#pragma once
#include <string>
#include "port_bench.h"  // PortBenchOverlay, PortBenchOverlayChange
#include "../platform/port_paths.h"  // PortOptions

struct PortJson;
// What shows (PORT_OV_*): by the density, or the boxes ticked.
unsigned PortBenchOverlayShows(const PortBenchOverlay &o);
// The see-through in percent: as set, or 50 full screen and 0 in a window.
int PortBenchOverlaySee(const PortBenchOverlay &o, int nShown);
// The set-up's Screen styles: Full overlay, Large readings, Minimal, Full screen readout and
// strip, In a window, No picture; the one a picture and an overlay make, -1 for none of them.
int PortBenchStyleOf(int nShown, const PortBenchOverlay &o);
const char *PortBenchStyleName(int nStyle);
const char *PortBenchStyleId(int nStyle);
void PortBenchStyleApply(int nStyle, int *pnShown, PortBenchOverlay *pOverlay);
std::string PortBenchScreenName(int nShown, const PortBenchOverlay &o);
const char *PortBenchOverlayLayoutId(const PortBenchOverlay &o);
std::string PortBenchOverlayViewName(const PortBenchOverlay &o);
// The command line's flags, settings.json's object, and the key runs compare by.
void PortBenchOverlayFromOptions(const struct PortOptions *pOpt, PortBenchOverlay *pOut);
std::string PortBenchOverlayArgs(int nShown, const PortBenchOverlay &o);
std::string PortBenchOverlayJson(int nShown, const PortBenchOverlay &o);
void PortBenchOverlayFromJson(const PortJson &j, PortBenchOverlay *pOut);
std::string PortBenchOverlayKey(int nShown, const PortBenchOverlay &o);
// During a run: the overlay as it is now, which O changes, and its changes with their frames.
void PortBenchOverlayBegin(const PortBenchOverlay &o);
PortBenchOverlay &PortBenchOverlayLive(void);
void PortBenchOverlayChanged(const char *pszHow);
int PortBenchOverlayChanges(const PortBenchOverlayChange **ppChanges);
