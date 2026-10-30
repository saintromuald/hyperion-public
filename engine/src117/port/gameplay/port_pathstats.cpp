// port_pathstats.cpp -- the pathing's counters (h/port_pathstats.h). The build is
// timed with the performance counter, which only this measurement reads.
#include <windows.h>
#include "../../h/port_pathstats.h"

PathStats g_pathStats;

static LARGE_INTEGER s_liBuild;

void PathStatsBuildBegin()
{
  QueryPerformanceCounter(&s_liBuild);
}

void PathStatsBuildEnd()
{
  LARGE_INTEGER liNow, liFreq;
  QueryPerformanceCounter(&liNow);
  QueryPerformanceFrequency(&liFreq);
  g_pathStats.fBuildMs = (liNow.QuadPart - s_liBuild.QuadPart) * 1000.0 / liFreq.QuadPart;
}
