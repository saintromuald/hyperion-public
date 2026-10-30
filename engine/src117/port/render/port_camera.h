// port_camera.h -- the camera: how far the picture of the map is zoomed out, and how it moves
// (port_camera.cpp).
#ifndef PORT_CAMERA_H
#define PORT_CAMERA_H

// The camera (port_camera.cpp): how far the picture of the map is zoomed out from
// the game's own view, which never changes for it. PortCameraZoom is the zoom drawn, 1 the
// game's view, 2 twice the map across and down; PortCameraZoomTo and PortCameraWheel (in
// notches, out positive) set where it eases to, up to PortCameraZoomMax (the "zoomMax"
// setting, 2 unless set). PortCameraStep eases it once a pass and says whether it is still
// moving. PortCameraForced is HYPERION_ZOOM_FORCE: the render view's path at zoom 1 too.
double PortCameraZoom(void);
double PortCameraZoomTarget(void);  // where the zoom eases to
double PortCameraZoomMin(void);     // 0.5 in a replay, which may look closer, 1 in a game
// A game or a replay begins: the zoom the player last chose for it ("zoom" or "replayZoom"
// in the settings), unless HYPERION_ZOOM set one. PortCameraRemember keeps the zoom the
// player chose now. PortCameraSmooth is the "smoothCamera" setting, 1 unless set, or the live
// channel's camera.smooth once a tool sets it, for the run: 0 moves the camera and the zoom at
// once.
void PortCameraGameStarts(int bReplay);
void PortCameraRemember(void);
int PortCameraSmooth(void);
// How fast the camera eases toward its aim: TV's slower, so a cut is a glide.
void PortCameraSlow(int bSlow);
int PortCameraIsSlow(void);
double PortCameraZoomMax(void);
// The zoom shown now, eased to this moment: the camera's, over the picture's motion's map
// scale (port_motion.cpp: a skirmish's start, a cutscene, a map load), as at a start it comes
// in from 1 / 0.86.
double PortCameraShown(void);
// A scroll of nPixels the player asked for (an edge, the keys, a drag), times the zoom drawn,
// so the picture moves across the screen as fast at any zoom; 1.17's own at zoom 1.
int PortCameraScrollPixels(int nPixels);
void PortCameraZoomTo(double fZoom);
// The zoom put at once, the setting left as it was (a cutscene's glide, port_cutscene.cpp).
void PortCameraZoomSet(double fZoom);
void PortCameraWheel(double fNotches);
int PortCameraStep(void);
int PortCameraForced(void);
// While held the zoom eases to 1 and comes back after.
void PortCameraHold(int bHold);

#endif
