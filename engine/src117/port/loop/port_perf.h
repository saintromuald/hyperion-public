// port_perf.h -- where a frame's time goes, by part (port_perf.cpp), for HYPERION_PERF and the
// benchmark.
#ifndef PORT_PERF_H
#define PORT_PERF_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types

// Where a frame's time goes, by part (port_perf.cpp), for HYPERION_PERF=1's log: the game
// loop marks each part's begin and end and each game frame; PortPerfLine writes the parts'
// ms per game frame since the last line, and starts again.
enum {
  PERF_EVENTS,    // the window's messages
  PERF_CREEP,     // creep's spread and decay
  PERF_AI,        // sai_update: the computer players and the pathing they ask for
  PERF_FOG,       // the fog's refresh, every 100 frames
  PERF_UNITS,     // CUnit::EntityUpdate: orders, movement, the unit finder
  PERF_MOVERS,    // bullets, flingies, thingies
  PERF_TRIGGERS,  // the triggers
  PERF_CHECK,     // a Hyperion replay's state check and recording
  PERF_MINIMAP,   // the minimap a network turn redraws
  PERF_UI,        // the console and the UI's per-frame half
  PERF_DRAW,      // img_update: the map's pass, the layers and the present
  PERF_PARTS
};
int PortPerfOn(void);
// Debug Tools' live numbers (the live channel's "perf"): PortPerfWant turns the seconds on
// while it is asked (port_present.cpp keeps the last 600 seconds for the channel).
void PortPerfWant(bool bOn);
bool PortPerfWanted(void);
const char *PortPerfPartName(int nPart);
void PortPerfIdle(DWORD dwMs);  // 1.17's CPU throttle's sleep, left out of the parts
// A part's time held (PortPerfHold in PortPerfEnd's place), then counted or not (PortPerfKeep).
void PortPerfHold(int nPart);
void PortPerfKeep(int nPart, bool bKeep);
void PortPerfBegin(int nPart);
void PortPerfEnd(int nPart);
void PortPerfGameFrame(void);
// A code address's function name from the PDB beside the exe (dbghelp, loaded when first
// asked); false when there is none.
bool PortSymbolName(unsigned long long qAddress, char *psz, size_t n);

// pOut, when given, gets the same numbers (frames shown a second is the caller's).
struct PortBenchSecond;
void PortPerfLine(char *psz, size_t n, double fSeconds, PortBenchSecond *pOut = NULL);

#endif
