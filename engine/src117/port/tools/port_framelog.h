// port_framelog.h -- a row for every game frame and every frame shown
// (port_framelog.cpp), for measuring how a large game's cost moves frame by frame.
#ifndef PORT_FRAMELOG_H
#define PORT_FRAMELOG_H

// Whether a log is being written: HYPERION_FRAME_LOG=<folder> from the start, or the live
// channel's framelog {on: true, folder?} from then on. While it is, the perf parts are timed
// (PortPerfOn) whether or not HYPERION_PERF asks.
int PortFrameLogOn(void);
// A game frame ran (PortPerfGameFrame): each part's ms in it, PERF_PARTS of them, and the units.
void PortFrameLogGame(const double *adPartMs, unsigned int nUnits);
// A frame was shown (PortPresentFrame): what the presenter measured of it. The row is written
// when the pass's draw ends (PortFrameLogDrawEnd, from PortPerfEnd), with that pass's parts'
// ms, PERF_PARTS of them: the events', the UI's and the draw's whole time.
struct PortFrameLogFrame {
  double dSceneMs;          // PortGpuScene: the description's upload and recording
  double dPresentMs;        // PortGpuFrame or SDL's present, the CPU's side
  double dPassMs;           // the pass's description (port_scene.cpp's SceneTimePass)
  unsigned int nPrims;      // the scene's primitives
  unsigned int nImagePrims; // of them an image's (a unit, a building, a bullet's sprite...)
  int nHdDraws;             // the HD art's draws
  double dGpuWaitMs;        // the present's wait for the card (PortGpuWaitMs): the card's share
  unsigned int nDistorts;   // the distortions, each a pass of its own over the map layer
  unsigned int nLayerPx;    // the map layer's pixels (the render view's when zoomed)
};
void PortFrameLogShown(const PortFrameLogFrame &frame);
void PortFrameLogDrawEnd(const double *adPartMs);

#endif
