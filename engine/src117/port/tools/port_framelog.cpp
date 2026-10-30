// port_framelog.cpp -- a row for every game frame and every frame shown, for measuring how a
// large game's cost moves frame by frame. HYPERION_PERF's log gives a second's
// means, which hide the frames that matter to a player: the fog's rebuild one game frame in
// 100, the state check's every 24th, a zoom that puts ten times the sprites on screen. This
// log keeps each frame, and tools/hxrender.py reads it.
//
// HYPERION_FRAME_LOG=<folder> starts a log with the program; the live channel's framelog
// {on, folder?, mark?} starts or stops one and marks a moment in it (a camera move beginning,
// a sweep's step). A log is a folder of three files:
//
//   game.csv   a game frame: its time, number, units, and each of its parts in ms
//   drawn.csv  a frame shown: the time since the last, the pass's parts, the presenter's
//              times, the camera, what the scene held and the units in the camera's sight
//   marks.csv  the marks
//
// Presentation only: it reads the game between frames and the performance counter.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "port_framelog.h"
#include "port_control.h"
#include "../loop/port_perf.h"  // PERF_PARTS and the parts' names
#include "../platform/port_json.h"
#include "../platform/port_paths.h"  // PortDataFolder
#include "../render/port_view.h"     // PortViewCamera, PortViewShape
#include "../render/port_gpu_hd.h"   // PortGpuHdShown
#include "../../gen/globals117.h"  // gwGameMode
#include "../platform/port_globals117.h"  // oc_dwReplayFrame
#include "CUnit.h"
#include "CSprite.h"

namespace {

FILE *g_pfGame;
FILE *g_pfDrawn;
FILE *g_pfMarks;
std::string g_folder;
LARGE_INTEGER g_start;
LARGE_INTEGER g_lastShown;
double g_fTicksPerMs;
unsigned int g_nGameFrames;  // game frames since the last frame shown
unsigned long long g_nGameRows, g_nDrawnRows;
bool g_bPending;  // a frame was shown in this pass, its row waiting for the draw's end
PortFrameLogFrame g_pending;
int g_nEnvChecked;

double NowMs(void)
{
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return static_cast<double>(now.QuadPart - g_start.QuadPart) / g_fTicksPerMs;
}

FILE *OpenCsv(const std::string &name, const char *pszHeader)
{
  FILE *pf = fopen((g_folder + "/" + name).c_str(), "w");
  if (pf == NULL)
    return NULL;
  setvbuf(pf, NULL, _IOFBF, 1 << 20);
  fputs(pszHeader, pf);
  return pf;
}

void Stop(void)
{
  for (FILE **ppf : {&g_pfGame, &g_pfDrawn, &g_pfMarks})
    if (*ppf != NULL) {
      fclose(*ppf);
      *ppf = NULL;
    }
  g_bPending = false;
}

bool Start(const std::string &folder)
{
  Stop();
  g_folder = folder;
  if (g_folder.empty()) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char sz[64];
    snprintf(sz, sizeof sz, "/framelogs/%04d%02d%02d_%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour,
             t.wMinute, t.wSecond);
    g_folder = std::string(PortDataFolder()) + sz;
  }
  // the folder and its parents
  for (size_t i = 3; i <= g_folder.size(); ++i)
    if (i == g_folder.size() || g_folder[i] == '\\' || g_folder[i] == '/')
      CreateDirectoryA(g_folder.substr(0, i).c_str(), NULL);
  LARGE_INTEGER freq;
  QueryPerformanceFrequency(&freq);
  g_fTicksPerMs = static_cast<double>(freq.QuadPart) / 1000.0;
  QueryPerformanceCounter(&g_start);
  g_lastShown = g_start;
  g_nGameFrames = 0;
  g_nGameRows = g_nDrawnRows = 0;
  g_pfGame = OpenCsv("game.csv", "t_ms,frame,units,creep,ai,fog,units_ms,movers,triggers,check,minimap,game_ms\n");
  g_pfDrawn = OpenCsv("drawn.csv",
                      "t_ms,interval_ms,frame,game_frames,events_ms,ui_ms,draw_ms,pass_ms,scene_ms,present_ms,gpu_wait_ms,distorts,layer_px,"
                      "zoom,cam_x,cam_y,sight_w,sight_h,prims,image_prims,hd_draws,hd,units,units_in_sight\n");
  g_pfMarks = OpenCsv("marks.csv", "t_ms,frame,name\n");
  if (g_pfGame == NULL || g_pfDrawn == NULL || g_pfMarks == NULL) {
    Stop();
    return false;
  }
  return true;
}

void Mark(const std::string &name)
{
  if (g_pfMarks == NULL)
    return;
  std::string clean = name;
  for (char &c : clean)
    if (c == ',' || c == '\n' || c == '\r')
      c = ' ';
  fprintf(g_pfMarks, "%.3f,%u,%s\n", NowMs(), oc_dwReplayFrame, clean.c_str());
  fflush(g_pfMarks);
}

// framelog {on?, folder?, mark?}: starts (on true) or stops (on false) a log, marks it, and
// answers where it is and how many rows it has.
bool CmdFrameLog(const PortJson &j, std::string *pAnswer)
{
  if (j["on"].kind == PortJson::BOOL) {
    if (j["on"].b) {
      if (!Start(j["folder"].Str(""))) {
        *pAnswer = "the log's folder could not be written";
        return false;
      }
    } else {
      Stop();
    }
  }
  if (j["mark"].kind == PortJson::STRING)
    Mark(j["mark"].Str());
  *pAnswer = "\"on\":" + std::string(PortFrameLogOn() ? "true" : "false") + ",\"folder\":" +
             PortJsonQuote(g_folder) + ",\"game_rows\":" + std::to_string(g_nGameRows) +
             ",\"drawn_rows\":" + std::to_string(g_nDrawnRows);
  return true;
}

}  // namespace

int PortFrameLogOn(void)
{
  if (!g_nEnvChecked) {
    g_nEnvChecked = 1;
    PortControlCommand("framelog", CmdFrameLog);
    char sz[MAX_PATH];
    const DWORD n = GetEnvironmentVariableA("HYPERION_FRAME_LOG", sz, sizeof sz);
    if (n > 0 && n < sizeof sz)
      Start(strcmp(sz, "1") == 0 ? std::string() : std::string(sz));
  }
  return g_pfDrawn != NULL ? 1 : 0;
}

void PortFrameLogGame(const double *adPartMs, unsigned int nUnits)
{
  if (g_pfGame == NULL)
    return;
  static const int kParts[] = {PERF_CREEP, PERF_AI,     PERF_FOG,   PERF_UNITS,
                               PERF_MOVERS, PERF_TRIGGERS, PERF_CHECK, PERF_MINIMAP};
  double dSum = 0.0;
  fprintf(g_pfGame, "%.3f,%u,%u", NowMs(), oc_dwReplayFrame, nUnits);
  for (int nPart : kParts) {
    fprintf(g_pfGame, ",%.4f", adPartMs[nPart]);
    dSum += adPartMs[nPart];
  }
  fprintf(g_pfGame, ",%.4f\n", dSum);
  ++g_nGameFrames;
  ++g_nGameRows;
}

void PortFrameLogShown(const PortFrameLogFrame &frame)
{
  if (g_pfDrawn == NULL)
    return;
  g_pending = frame;
  g_bPending = true;
}

void PortFrameLogDrawEnd(const double *adPartMs)
{
  if (g_pfDrawn == NULL || !g_bPending)
    return;
  g_bPending = false;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  const double dInterval = static_cast<double>(now.QuadPart - g_lastShown.QuadPart) / g_fTicksPerMs;
  g_lastShown = now;
  // the camera's sight in map pixels, and the units whose sprite is in it
  double fX = 0.0, fY = 0.0, fZoom = 1.0, fW = 0.0, fRows = 0.0;
  if (gwGameMode == 3) {
    PortViewCamera(&fX, &fY, &fZoom);
    PortViewShape(&fW, &fRows);
  }
  const double fRight = fX + fW * fZoom, fBottom = fY + fRows * fZoom;
  // counted again only after a game frame or with the sight moved: the walk over every unit
  // was a twentieth of a frame shown between game frames, which the log is to measure
  static unsigned int s_nUnits, s_nSeen;
  static double s_afSight[4] = {-1.0, -1.0, -1.0, -1.0};
  if (g_nGameFrames != 0 || s_afSight[0] != fX || s_afSight[1] != fY || s_afSight[2] != fRight ||
      s_afSight[3] != fBottom) {
    s_afSight[0] = fX;
    s_afSight[1] = fY;
    s_afSight[2] = fRight;
    s_afSight[3] = fBottom;
    s_nUnits = 0;
    s_nSeen = 0;
    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
      ++s_nUnits;
      const CSprite *pSprite = pUnit->pSprite;
      if (pSprite != NULL && pSprite->wPosX >= fX && pSprite->wPosX < fRight && pSprite->wPosY >= fY &&
          pSprite->wPosY < fBottom)
        ++s_nSeen;
    }
  }
  const unsigned int nUnits = s_nUnits, nSeen = s_nSeen;
  fprintf(g_pfDrawn, "%.3f,%.4f,%u,%u,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%u,%u,%.4f,%.1f,%.1f,%.1f,%.1f,%u,%u,%d,%d,%u,%u\n",
          static_cast<double>(now.QuadPart - g_start.QuadPart) / g_fTicksPerMs, dInterval, oc_dwReplayFrame,
          g_nGameFrames, adPartMs[PERF_EVENTS], adPartMs[PERF_UI], adPartMs[PERF_DRAW], g_pending.dPassMs,
          g_pending.dSceneMs, g_pending.dPresentMs, g_pending.dGpuWaitMs, g_pending.nDistorts, g_pending.nLayerPx,
          fZoom, fX, fY, fW * fZoom, fRows * fZoom, g_pending.nPrims,
          g_pending.nImagePrims, g_pending.nHdDraws, PortGpuHdShown(), nUnits, nSeen);
  g_nGameFrames = 0;
  ++g_nDrawnRows;
}
