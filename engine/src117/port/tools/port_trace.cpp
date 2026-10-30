// port_trace.cpp -- Debug Tools' traces: readings taken as the engine runs, for
// finding what moves wrong, which the live channel turns on and off and reads
// (docs/modules/tools.md, "Crashes and traces").
//
// The camera's trace is a row each time the camera is placed (PortViewCamera) and anything
// moved: the game view and the camera, what the camera made of the move, the zoom, the easing,
// the cursor, and the function that called cell_set_pos since the last row, named from the PDB,
// or a scroll. Stopped, it is a CSV in the data folder's traces\ and a short account. The
// channel also presses the minimap through its own handler (PortMinimapPress) and holds an
// arrow key in 1.17's key table (PortTraceScroll), a row every pass while held and a moment
// after, so a run can repeat the same moves; the account says how even the motion was.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "../platform/port_paths.h"  // PortDataFolder
#include "../loop/port_perf.h"  // PortSymbolName
#include "port_trace.h"
#include "../render/port_view.h"  // PortViewMinimapPress
#include "../platform/port_json.h"  // PortJsonQuote
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

#ifndef gKeyTbl
extern BYTE gKeyTbl[0x100] OC_UNALIGNED;  // 0x665E58, the keys held
#endif

namespace {

struct CameraRow {
  double dMs;
  unsigned int nFrame;
  double gx, gy, dx, dy;
  const char *pszMode;
  double cx, cy, offX, offY;
  double zoom, zoomTarget, k;
  LONG cursorX, cursorY;
  void *pCause;       // cell_set_pos's caller since the last row, NULL for none
  int nSetX, nSetY;   // where it was asked to put the view
  bool bHeld;         // a scroll was held (PortTraceScroll)
};

bool g_bCamera;
LARGE_INTEGER g_start;
std::vector<CameraRow> g_rows;
void *g_pPendingCause;
int g_nPendingX, g_nPendingY;
const size_t kRowsMost = 200000;

// the scroll held: its key, how long, since when, and until when every pass is a row
int g_nScrollKey;
double g_dScrollMs;
double g_dScrollFrom = -1.0;
double g_dEveryUntil = -1.0;

// the minimap's drag, a point a step
std::vector<std::pair<double, double>> g_path;
size_t g_nPathNext;
double g_dStepMs = 33.0;
LARGE_INTEGER g_pathLast;

double MsSince(const LARGE_INTEGER &from)
{
  LARGE_INTEGER now, freq;
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&freq);
  return 1000.0 * static_cast<double>(now.QuadPart - from.QuadPart) / static_cast<double>(freq.QuadPart);
}

std::string CauseName(void *p)
{
  if (p == NULL)
    return "";
  char sz[200];
  if (PortSymbolName(reinterpret_cast<unsigned long long>(p), sz, sizeof sz))
    return sz;
  snprintf(sz, sizeof sz, "%p", p);
  return sz;
}

}  // namespace

void PortTraceViewSet(void *pCaller, int nX, int nY)
{
  if (!g_bCamera)
    return;
  g_pPendingCause = pCaller;
  g_nPendingX = nX;
  g_nPendingY = nY;
}

void PortTraceCamera(double gx, double gy, double dx, double dy, const char *pszMode, double cx, double cy,
                     double offX, double offY, double zoom, double zoomTarget, double k)
{
  if (!g_bCamera)
    return;
  // a row when anything moved, and one at rest after moving
  const bool bStill = dx == 0.0 && dy == 0.0 && g_pPendingCause == NULL && strcmp(pszMode, "rest") == 0 &&
                      zoom == zoomTarget;
  const double dNow = MsSince(g_start);
  const bool bEvery = dNow <= g_dEveryUntil;
  if (bStill && !bEvery && !g_rows.empty() && strcmp(g_rows.back().pszMode, "rest") == 0)
    return;
  if (g_rows.size() >= kRowsMost)
    return;
  CameraRow r;
  r.dMs = dNow;
  r.bHeld = g_nScrollKey != 0;
  r.nFrame = oc_dwReplayFrame;
  r.gx = gx;
  r.gy = gy;
  r.dx = dx;
  r.dy = dy;
  r.pszMode = pszMode;
  r.cx = cx;
  r.cy = cy;
  r.offX = offX;
  r.offY = offY;
  r.zoom = zoom;
  r.zoomTarget = zoomTarget;
  r.k = k;
  r.cursorX = gCursorPos_g[0];
  r.cursorY = gCursorPos_g[1];
  r.pCause = g_pPendingCause;
  r.nSetX = g_nPendingX;
  r.nSetY = g_nPendingY;
  g_pPendingCause = NULL;
  g_rows.push_back(r);
}

bool PortTraceCameraOn(void)
{
  return g_bCamera;
}

void PortTraceCameraStart(void)
{
  g_rows.clear();
  g_pPendingCause = NULL;
  QueryPerformanceCounter(&g_start);
  g_dEveryUntil = -1.0;
  g_bCamera = true;
}

// The account of the passes while a scroll was held: the game view's steps (how many, the
// time between them, their sizes), the passes that showed no motion, and the camera's move a
// pass (its mean and how much it varied, as a fraction of the mean).
std::string ScrollAccount(void)
{
  int nPasses = 0, nStill = 0, nSteps = 0;
  double dLastStep = -1.0, dGapSum = 0.0, dGapMost = 0.0, dGapLeast = 1e9, dStepLeast = 1e9, dStepMost = 0.0;
  int nGaps = 0;
  std::vector<double> moves;
  const CameraRow *pPrev = NULL;
  for (const CameraRow &r : g_rows) {
    if (!r.bHeld) {
      pPrev = NULL;
      continue;
    }
    if (pPrev != NULL) {
      ++nPasses;
      const double m = hypot(r.cx - pPrev->cx, r.cy - pPrev->cy);
      moves.push_back(m);
      nStill += m < 0.01;
    }
    if (r.dx != 0.0 || r.dy != 0.0) {
      ++nSteps;
      const double d = hypot(r.dx, r.dy);
      dStepLeast = d < dStepLeast ? d : dStepLeast;
      dStepMost = d > dStepMost ? d : dStepMost;
      if (dLastStep >= 0.0) {
        const double g = r.dMs - dLastStep;
        dGapSum += g;
        ++nGaps;
        dGapMost = g > dGapMost ? g : dGapMost;
        dGapLeast = g < dGapLeast ? g : dGapLeast;
      }
      dLastStep = r.dMs;
    }
    pPrev = &r;
  }
  double dMean = 0.0, dVar = 0.0;
  for (double m : moves)
    dMean += m;
  dMean = moves.empty() ? 0.0 : dMean / moves.size();
  for (double m : moves)
    dVar += (m - dMean) * (m - dMean);
  const double dSd = moves.empty() ? 0.0 : sqrt(dVar / moves.size());
  char sz[512];
  snprintf(sz, sizeof sz,
           ",\"scroll\":{\"passes\":%d,\"still_passes\":%d,\"view_steps\":%d,\"step_ms_mean\":%.1f,"
           "\"step_ms_least\":%.1f,\"step_ms_most\":%.1f,\"step_px_least\":%.0f,\"step_px_most\":%.0f,"
           "\"camera_px_a_pass\":%.2f,\"camera_px_varies\":%.2f}",
           nPasses, nStill, nSteps, nGaps ? dGapSum / nGaps : 0.0, nGaps ? dGapLeast : 0.0, dGapMost,
           nSteps ? dStepLeast : 0.0, dStepMost, dMean, dMean > 0.0 ? dSd / dMean : 0.0);
  return sz;
}

// Stops the trace and writes it; the file's path and an account of it, as JSON members.
std::string PortTraceCameraStop(void)
{
  g_bCamera = false;
  SYSTEMTIME t;
  GetLocalTime(&t);
  char szName[64];
  snprintf(szName, sizeof szName, "%04u%02u%02u_%02u%02u%02u_camera.csv", t.wYear, t.wMonth, t.wDay, t.wHour,
           t.wMinute, t.wSecond);
  const std::string folder = std::string(PortDataFolder()) + "traces";
  CreateDirectoryA(folder.c_str(), NULL);
  const std::string path = folder + "\\" + szName;
  // an account: how many moves of each kind, the jumps, and how long each ease took to rest
  int nScroll = 0, nJump = 0, nEaseRows = 0, nSets = 0;
  double dEaseStart = -1.0, dEaseLongest = 0.0, dScrollMost = 0.0;
  FILE *f = NULL;
  if (fopen_s(&f, path.c_str(), "wb") == 0 && f != NULL) {
    fprintf(f, "ms,frame,game_x,game_y,dx,dy,mode,cam_x,cam_y,off_x,off_y,zoom,zoom_target,ease_k,cursor_x,cursor_y,"
               "held,moved_by,set_x,set_y\r\n");
    for (const CameraRow &r : g_rows) {
      const std::string cause = r.pCause != NULL ? CauseName(r.pCause) : (r.dx != 0.0 || r.dy != 0.0 ? "scroll" : "");
      fprintf(f, "%.2f,%u,%.1f,%.1f,%.1f,%.1f,%s,%.2f,%.2f,%.2f,%.2f,%.4f,%.4f,%.4f,%ld,%ld,%s,", r.dMs, r.nFrame, r.gx,
              r.gy, r.dx, r.dy, r.pszMode, r.cx, r.cy, r.offX, r.offY, r.zoom, r.zoomTarget, r.k, r.cursorX, r.cursorY,
              r.bHeld ? "scroll" : "");
      fprintf(f, "%s,", cause.c_str());
      if (r.pCause != NULL)
        fprintf(f, "%d,%d\r\n", r.nSetX, r.nSetY);
      else
        fprintf(f, ",\r\n");
    }
    fclose(f);
  }
  for (const CameraRow &r : g_rows) {
    nSets += r.pCause != NULL;
    if (strcmp(r.pszMode, "scroll") == 0) {
      ++nScroll;
      const double d = fabs(r.dx) > fabs(r.dy) ? fabs(r.dx) : fabs(r.dy);
      dScrollMost = d > dScrollMost ? d : dScrollMost;
    }
    if (strcmp(r.pszMode, "jump") == 0) {
      ++nJump;
      dEaseStart = r.dMs;
    }
    if (strcmp(r.pszMode, "ease") == 0)
      ++nEaseRows;
    if (strcmp(r.pszMode, "rest") == 0 && dEaseStart >= 0.0) {
      dEaseLongest = r.dMs - dEaseStart > dEaseLongest ? r.dMs - dEaseStart : dEaseLongest;
      dEaseStart = -1.0;
    }
  }
  char sz[512];
  snprintf(sz, sizeof sz,
           "\"file\":%s,\"rows\":%u,\"view_sets\":%d,\"scrolls\":%d,\"largest_scroll\":%.0f,\"jumps\":%d,"
           "\"ease_rows\":%d,\"longest_ease_ms\":%.0f",
           PortJsonQuote(path).c_str(), static_cast<unsigned>(g_rows.size()), nSets, nScroll, dScrollMost, nJump, nEaseRows,
           dEaseLongest);
  return sz + ScrollAccount();
}

// The rows from nSince on, as a JSON array, the most recent nMost of them.
std::string PortTraceCameraRows(size_t nSince, size_t nMost)
{
  if (g_rows.size() > nSince + nMost)
    nSince = g_rows.size() - nMost;
  std::string s = "[";
  char sz[256];
  for (size_t i = nSince; i < g_rows.size(); ++i) {
    const CameraRow &r = g_rows[i];
    snprintf(sz, sizeof sz,
             "%s{\"i\":%u,\"ms\":%.1f,\"frame\":%u,\"gx\":%.0f,\"gy\":%.0f,\"mode\":\"%s\",\"cx\":%.1f,\"cy\":%.1f,"
             "\"zoom\":%.3f,\"set\":%s}",
             i > nSince ? "," : "", static_cast<unsigned>(i), r.dMs, r.nFrame, r.gx, r.gy, r.pszMode, r.cx, r.cy,
             r.zoom, r.pCause != NULL ? "true" : "false");
    s += sz;
  }
  return s + "]";
}

size_t PortTraceCameraCount(void)
{
  return g_rows.size();
}

// Holds a scroll key (VK_LEFT, VK_UP, VK_RIGHT or VK_DOWN, or a numpad diagonal) for dMs, the
// camera trace started if it was not; PortTracePoll lets it go.
void PortTraceScroll(int nKey, double dMs)
{
  if (!g_bCamera)
    PortTraceCameraStart();
  if (g_nScrollKey != 0)
    gKeyTbl[g_nScrollKey] = 0;
  g_nScrollKey = nKey & 0xFF;
  g_dScrollMs = dMs > 1.0 ? dMs : 1.0;
  g_dScrollFrom = MsSince(g_start);
  g_dEveryUntil = g_dScrollFrom + g_dScrollMs + 400.0;
  gKeyTbl[g_nScrollKey] = 1;
}

void PortTraceMinimapPath(const std::vector<std::pair<double, double>> &path, double dStepMs)
{
  g_path = path;
  g_nPathNext = 0;
  g_dStepMs = dStepMs > 1.0 ? dStepMs : 1.0;
  g_pathLast.QuadPart = 0;
}

// Between the game's frames (PortControlPoll): the drag's next point when its time comes.
void PortTracePoll(void)
{
  if (g_nScrollKey != 0 && (!g_bCamera || MsSince(g_start) - g_dScrollFrom >= g_dScrollMs)) {
    gKeyTbl[g_nScrollKey] = 0;
    g_nScrollKey = 0;
  }
  if (g_nPathNext >= g_path.size())
    return;
  if (g_pathLast.QuadPart != 0 && MsSince(g_pathLast) < g_dStepMs)
    return;
  QueryPerformanceCounter(&g_pathLast);
  if (g_nPathNext == 0)
    PortViewMinimapPress();  // the first point is the press, the rest the drag
  PortMinimapPress(g_path[g_nPathNext].first, g_path[g_nPathNext].second);
  ++g_nPathNext;
}
