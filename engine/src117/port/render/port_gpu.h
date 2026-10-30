// port_gpu.h -- the GPU presenter (port_gpu.cpp), for port_present.cpp, the one
// file that picks between it and SDL's renderer. SDL's types stay out of the port headers
// the game's C++ includes.
#pragma once
#include "../../h/platform.h"  // LONG and ULONG
#include <SDL3/SDL.h>

// Creates the device, on the driver SDL_GPU names ("direct3d12", "vulkan", or NULL for
// SDL's choice), and, with bPresent, claims the window's swapchain. False when the
// driver cannot be had on this machine; nothing is left behind.
bool PortGpuInit(SDL_Window *pWindow, const char *pszDriver, bool bPresent);
// Why the last PortGpuInit did not start: what failed, with SDL's error.
const char *PortGpuWhy(void);
void PortGpuShutdown(void);
// Settings, "Vertical sync": frames wait for the display's refresh, or are shown at once.
void PortGpuVsyncSet(bool bVsync);
bool PortGpuVsyncOn(void);  // the window's frames wait for the refresh
// The cover given to PortGpuFrame differs from the one the GPU holds, which is uploaded
// again at the next frame that composes; until then the GPU's own copy is used.
void PortGpuCoverChanged(void);
void PortGpuAheadSet(int nAhead);  // Settings, "Frames ahead": 1 to 3
void PortGpuArtFilterSet(int nStep);  // Settings, "Classic art filtering": 0 sharp to 3 filtered
void PortGpuFullColourSet(bool bOn);  // Settings, "Player colours": Exact is the map in full colour
const char *PortGpuDriver(void);

// Draws the 8-bit screen (nWidth x nHeight, rows nWidth apart) through the palette
// and, when bPresent, into the window's letterbox. With pCover (the screen's size),
// the pixels it marks come from the GPU scene's map instead (PortGpuScene). bCheck
// reads the resolved frame back and compares it with the screen through the palette
// (hyperion_render.log). False when the swapchain had no image to give, so the frame
// was not shown.
// A cinematic in full colour over the frame, fitted to the window at the shape nShowW by nShowH
// (port_gpu_video.cpp): its picture's planes, copied here, going up with the next frame; NULL
// when none plays.
struct PortVideoPlanes;
void PortGpuVideoSet(const PortVideoPlanes *pPlanes, int nShowW, int nShowH);
bool PortGpuFrame(const Uint8 *pScreen, const Uint8 *pCover, int nWidth, int nHeight,
                  const SDL_Color *pColors, bool bPaletteChanged, bool bPresent, bool bCheck);

// The live channel's snapshot (port_control.cpp): the next frame PortGpuFrame resolves is
// saved at pszPath, a PNG when it ends in .png, at the game's size. PortGpuSnapTake is
// true once it was taken, and says how it went.
void PortGpuSnapAsk(const char *pszPath);
bool PortGpuSnapPending(void);
bool PortGpuSnapTake(bool *pbOk, int *pnWidth, int *pnHeight);

// The check's counts so far: frames compared, and those that differed.
void PortGpuCheckCounts(ULONG *pnChecked, ULONG *pnMismatched);

// Draws a frame description (port_scene.h) into the GPU's own map (scene.hlsl,
// docs/renderer.md, R4b) and, with bCheck, compares it with the game's map; the
// counts go to hyperion_render.log beside the presenter's. At the last stage the
// pixels the screen took go into the GPU's copy of the screen, which PortGpuFrame
// shows through the cover. False when they did not, and the cover no longer holds.
struct SceneFrame;
bool PortGpuScene(const SceneFrame *pFrame, const SDL_Color *pColors, bool bCheck);
// Submits the frame's commands not yet submitted: the scene's, when no present followed it.
void PortGpuFrameEnd(void);
void PortGpuSceneCounts(ULONG *pnChecked, ULONG *pnMismatched);
// The GPU's copy of the screen (nBytes, the screen's rows), read back; false when
// there is none.
bool PortGpuReadShown(Uint8 *pOut, Uint32 nBytes);

// The frame's place in a target, as SDL's renderer letterboxes it
// (SDL_LOGICAL_PRESENTATION_LETTERBOX): the largest size of the frame's shape that
// fits, centred.
void PortGpuLetterbox(float fTargetW, float fTargetH, float fLogicalW, float fLogicalH, SDL_FRect *pDst);
void PortGpuGameBox(float fTargetW, float fTargetH, float fLogicalW, float fLogicalH, SDL_FRect *pDst);

// The front end (port_front.cpp, docs/front-end.md, F1), at the window's own resolution.
// PortGpuUiBegin starts a frame and gives the size to lay it out at: the swapchain's, or
// the window's size given when there is no image to draw into (a hidden window, for a
// snapshot). PortGpuUiEnd draws the planet, at fPlanetScale of that size, and the
// primitives over it, shows the frame, and with pszSnap saves it (a PNG when the name
// ends in .png, else a BMP).
struct PlanetParams;
struct UiPrim;
bool PortGpuUiBegin(int nWindowW, int nWindowH, int *pnWidth, int *pnHeight);
// Why the front end's pipelines could not be made, for its log.
const char *PortGpuUiError(void);
// Whether the frame PortGpuUiBegin started has a swapchain image, so it will be seen.
bool PortGpuUiShown(void);
bool PortGpuUiEnd(int nWidth, int nHeight, const PlanetParams *pPlanet, float fPlanetScale, const UiPrim *pPrims,
                  int nPrims, const char *pszSnap);

// The processor's wait for the card at a frame's submission since the last call, in ms (the
// frame log's, port_framelog.cpp).
double PortGpuWaitMs(void);
