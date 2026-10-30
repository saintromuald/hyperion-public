// port_present.h -- the presenter: the window and the screen the game draws into, frames and their
// rate, the palette, the cursor, the displays and the video (port_present.cpp).
#ifndef PORT_PRESENT_H
#define PORT_PRESENT_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types
#include <string>

// A small picture of the game as shown (port_present.cpp): a LAN room's copy of a match.
bool PortPresentThumb(unsigned char *pRgb, int w, int h);  // w * h * 3 bytes
// A streamed seat's (port_lanstream.cpp): its window is never shown, and the mouse comes from the
// browser in game pixels, marked PORT_TEST_MOUSE as a test's is.
void PortPresentRemoteInput(bool bOn);
// The game's box being typed in, in game pixels, and its caret's x from the box's left: where an
// input method shows the text it composes and its candidates.
void PortPresentTextArea(int nLeft, int nTop, int nRight, int nBottom, int nCaret);
bool PortPresentRemote(void);  // a streamed seat's: its player sees the game's frame alone

// The last nMax live seconds, oldest first, and the last one's present ms (port_present.cpp).
struct PortBenchSecond;
int PortPerfLiveSeconds(PortBenchSecond *pOut, double *pdPresentMs, int nMax);

// The screen (port_present.cpp): an 8-bit 640x480 buffer in place of Storm's
// SDraw surface, shown in the game window through SDL3. init_directx and
// free_directx bring it up and down; the sites that locked Storm's surface lock
// it (gds_vidblit.cpp); flip_palette hands it the palette; img_update presents
// the frame when its pass is done.
void PortPresentInit(void *pWnd);
void PortPresentShutdown(void);
int PortScreenIsUp(void);
// Whether the screen is shown by the GPU presenter (port_gpu.cpp), not SDL's renderer.
int PortPresentGpu(void);
// Whether the GPU presenter can show the camera zoomed out, its ground and HUD pipelines
// made (port_gpu.cpp).
int PortGpuZoomCan(void);
// The GPU map layer's step with the camera at fZoom over a render view nWidth x nRows
// (SceneFrame's nStep): the most of the game's pixels to one of the layer's that leaves two of
// its pixels to one of the window's, which the present averages, a power of 2 up to 8 (16 for
// a render view past 8,192 pixels). HYPERION_SCENE_STEP=<n> fixes it.
int PortGpuSceneStep(int nWidth, int nRows, double fZoom);
// The SDL window the screen is shown in (an SDL_Window), for a dialog over it.
void *PortPresentWindow(void);
// Draws the frame again at the next present though the screen did not change: a live
// value changed how it looks (port_control.cpp).
void PortPresentRedraw(void);
// Which pixels of the screen show the GPU scene's map (1) and which the screen's own
// bytes (0), for the GPU presenter's frame at the scene's last stage. The copy of the
// map layer marks what it copies (port_scene.cpp, PortSceneScreenDone), 1 below nRows
// when the scene has the frame and 0 otherwise; the dialogs, the fade's copy and the
// video player clear what they write.
void PortScreenCoverCopied(const unsigned char *pCopied, int nRows, int bScene);
void PortScreenCoverRect(int nLeft, int nTop, int nWidth, int nHeight);
void PortScreenCoverClear(void);
int PortScreenLock(void **ppBits, int *pnPitch);
int PortScreenUnlock(void);
// A count of the writes to the screen (each unlock, each clear): unchanged, the screen holds
// what it held when the count was last read.
unsigned int PortScreenWrites(void);
void PortPresentPalette(const void *pEntries, int nFirst, int nCount);
// The screen's palette as it stands, 256 entries of red, green and blue.
void PortPresentPaletteRGB(unsigned char *pRGB);
void PortPresentFrame(void);
// Settings, "Frame rate" (port_present.cpp): PortFrameRate is the choice, 0 as 1.17 (frames
// only on the game loop's steps) to 5 no limit; PortFrameDue is true once a frame period,
// for the game loop to draw one.
int PortFrameRate(void);
// While held, PortPresentFrame shows nothing and what changed waits for the next frame drawn:
// gameloop_update_ui holds it round the colour cycle at a frame rate Settings chose, so the
// cycle's palette pushes do not show frames of their own past that rate.
void PortPresentHold(bool bHold);
void PortFrameRateSet(int nRate);
// Settings, "Classic art filtering" ("artFilter"): how the 640 x 480 frame is scaled to the
// window, 0 Sharp (the pixel-art filter, as before), 1 Soft, 2 Softer, 3 Filtered. SDL's
// renderer has the first and the last; Soft and Softer are filtered there.
void PortArtFilterSet(int nStep);
bool PortFrameDue(void);
// The seconds until PortFrameDue next gives a frame: 0 when one is due or the rate has no
// limit, below 0 at 1.17's way, which draws whenever anything is new.
double PortFrameDueIn(void);
// A benchmark run's seconds (port_bench.cpp): true hands on the part of a second since the last;
// false drops it, as a run starts.
void PortPresentPerfFlush(bool bKeep);
// The presenter in use and the GPU presenter's check counts, for the replay
// harness (port_observe.cpp, pfnPresenter).
void PortPresenterReport(char *pszName, unsigned int nCap, ULONG *pnChecked,
                         ULONG *pnMismatched);
// The screen as the video player has it (port_stormvid.cpp), where Storm's SVid
// had SDraw: its size, its lock, the unlock that presents the frame, a palette
// shown at once, and a clear.
void PortVidScreenSize(ULONG *pdwWidth, ULONG *pdwHeight);
int PortVidLock(void **ppBits, int *pnPitch);
void PortVidUnlock(void);
void PortVidPalette(const void *pEntries, int nFirst, int nCount);
void PortVidClear(void);
// A cinematic in full colour: whether the presenter draws one (the GPU presenter's), the window's
// height in pixels, which picks the copy played, and a picture shown at once over the screen,
// fitted to the window at the shape nShowW by nShowH, until NULL ends it.
struct PortVideoPlanes;
bool PortVidFullColour(void);
int PortVidWindowRows(void);
void PortVidPicture(const PortVideoPlanes *pPlanes, int nShowW, int nShowH);
void PortToggleFullscreen(void);
int PortScreenshot(const char *pszFile);
// The width in game pixels, at 480 rows, of the display the window is on, or 0
// before the window is up.
int PortDisplayViewWidth(void);

// How the window is shown (port_present.cpp; Settings' "Display" and "Monitor and
// resolution"): in a window, in full screen at a display mode of its own, or borderless over
// the display at the desktop's size. A pick is a monitor and, for the first two, a size.
// The kinds are in Settings' order.
enum PortDisplayKind { PORT_DISPLAY_WINDOW, PORT_DISPLAY_FULLSCREEN, PORT_DISPLAY_BORDERLESS };
struct PortDisplayPick {
  char szMonitor[96];  // the display's name, SDL's
  int nMonitor;        // its place among the displays, for two of the same name
  bool bPrimary;
  int nWidth;
  int nHeight;
};
// The picks a kind offers, every monitor's, the primary's first: its desktop size borderless,
// its display modes in full screen, the sizes that fit it in a window.
int PortDisplayChoices(int nKind, PortDisplayPick *pPicks, int nMax);
// What the settings hold, or borderless on the primary display when they hold nothing.
void PortDisplaySaved(int *pnKind, PortDisplayPick *pPick);
// What the window shows now.
void PortDisplayNow(int *pnKind, PortDisplayPick *pPick);
// Shows it now; false for a background or hidden window, which keeps its own.
bool PortDisplayShow(int nKind, const PortDisplayPick *pPick);
// Saves it for the next start.
void PortDisplaySave(int nKind, const PortDisplayPick *pPick);

// The mouse between the window and the game's 640x480 pixels: whether mouse messages are
// dropped (a background or hidden window takes none), a message's lParam, the pointer read and
// set in game pixels (the system cursor in a player's window, the port's own pointer in a
// background or hidden one), and the cursor cage in game pixels (NULL releases it). With
// HYPERION_TEST_MOUSE=1 a background window takes the mouse messages a test posts, marked
// PORT_TEST_MOUSE in wParam, which is taken off; the user's own mouse still reaches nothing.
#define PORT_TEST_MOUSE 0x1000
int PortMouseIgnored(UINT_PTR *pwParam);
// The front end (port_front.cpp) draws through the GPU presenter at the window's own
// resolution. While it is up the system's cursor shows, and a background or hidden window
// takes HYPERION_FRONT_SIZE (WxH); PortPresentFrontBegin gives the client's size and the
// display's refresh rate, PortPresentFrontEnd puts it all back for the game's frame.
void PortPresentFrontBegin(int *pnWidth, int *pnHeight, float *pfRefresh);
void PortPresentFrontEnd(void);
// The front end's last frame stays up while 1.17's screens pass between two of its own (a section
// chosen from another, through 1.17's main menu): the game's frames, which would show those
// screens black, are not presented until the next PortPresentFrontBegin, or 1.5 s at most.
void PortPresentFrontKeep(void);
void PortPresentClientSize(int *pnWidth, int *pnHeight);
LONG PortMouseToGame(LONG lParam);
void PortCursorGet(int *pnX, int *pnY);
void PortCursorSet(int nX, int nY);
struct tagRECT;
void PortClipCursor(const struct tagRECT *prcGame);
// Settings: whether a game's whole-view cage holds the cursor ("confineMouse"), and the
// middle button's drag ("dragScroll"; scroll_update asks PortDragScroll each step, which
// says whether the button is held and how far to move the view).
void PortConfineSet(bool bConfine);
// Ctrl+Alt in a game lets the cursor go, as in VMware (event_Win.cpp): no cage holds it, and off
// the game's picture the game keeps its pointer in from the edges. PortCursorTakeBack, for each
// mouse message: true when it is a press on the game that takes the cursor back, or that
// press's release; the game sees neither. The menus take it back too.
void PortCursorFreeSet(bool bFree);
bool PortCursorFree(void);
bool PortCursorTakeBack(unsigned uMsg, LONG lParam);
// For each mouse move in a game: Windows' arrow shows while the pointer is off the game's picture
// (round the overlay's window, or anywhere once Ctrl+Alt let it go), where the game draws none.
void PortCursorOffGame(LONG lParam);
// The same where the pointer is now, with no move: a layer over the whole window came or went (the
// game menu, port_front_gamemenu.cpp).
void PortCursorRecheck(void);
// Whether game pixel nX, nY is under the panels of a layer that are the console's to the game
// (PORT_OVERLAY_CONSOLE, the music player's windows): console_collision answers yes there.
bool PortCursorOverLayer(int nX, int nY);
// While a layer drags something of its own (a window of the music player's by its title), the
// map does not scroll at the edges: the game's pointer for scrolling stays a few pixels in.
void PortCursorHoldScroll(bool bHold);
#define PORT_DRAG_SPEED_DEFAULT 0.3f  // the drag at about half the cursor's movement
void PortDragScrollSet(bool bOn, float fSpeed);
bool PortDragScroll(int *pnX, int *pnY);
// Settings' "Mouse sensitivity": while a cage holds the cursor in a game, each mouse move puts the
// pointer where the mouse's movement times the scale takes it. PortMouseScale, for each move:
// true with that place in client pixels; false leaves the move as Windows made it.
void PortMouseSensitivitySet(bool bOn, float fSpeed);
bool PortMouseScale(int *pnX, int *pnY);

// The live channel's readings, a second each, from nSince on as a JSON array; *pnCount the
// seconds counted so far.
std::string PortPerfSecondsJson(size_t nSince, size_t *pnCount);

#endif
