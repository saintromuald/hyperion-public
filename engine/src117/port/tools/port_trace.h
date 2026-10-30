// port_trace.h -- Debug Tools' traces (port_trace.cpp): the camera's, and the live
// channel's presses on the minimap.
#ifndef PORT_TRACE_H
#define PORT_TRACE_H

#include <stddef.h>
#include <string>
#include <utility>
#include <vector>

// Debug Tools' traces (port_trace.cpp). The camera's: cell_set_pos notes who moved
// the game's view (PortTraceViewSet), PortViewCamera adds a row as it places the camera;
// the live channel starts and stops it. PortMinimapPress (minimap.cpp) puts the cursor at a
// point of the minimap, u and v from 0 to 1, and runs the minimap's own drag handler.
void PortTraceViewSet(void *pCaller, int nX, int nY);
void PortTraceCamera(double gx, double gy, double dx, double dy, const char *pszMode, double cx, double cy,
                     double offX, double offY, double zoom, double zoomTarget, double k);
bool PortTraceCameraOn(void);
void PortTraceCameraStart(void);
void PortTracePoll(void);
void PortMinimapPress(double u, double v);

// The camera trace stopped and written: the file's path and an account of it, as JSON members.
// Its rows from nSince on, as a JSON array, the most recent nMost of them; and how many so far.
std::string PortTraceCameraStop(void);
std::string PortTraceCameraRows(size_t nSince, size_t nMost);
size_t PortTraceCameraCount(void);
// The minimap pressed along a path of points (each 0 to 1 across and down), one every dStepMs;
// PortTracePoll presses each in its turn.
void PortTraceMinimapPath(const std::vector<std::pair<double, double>> &path, double dStepMs);
// A scroll key (VK_LEFT, VK_UP, VK_RIGHT or VK_DOWN, or a numpad diagonal) held for dMs, the
// camera trace started if it was not; PortTracePoll lets it go.
void PortTraceScroll(int nKey, double dMs);

#endif
