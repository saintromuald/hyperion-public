// port_overlay.h -- what Hyperion draws over a game: the timer, actions per minute and the frame
// rate, and the layers other modules put over the game (port_overlay.cpp).
#ifndef PORT_OVERLAY_H
#define PORT_OVERLAY_H

// What Hyperion draws over a game (port_overlay.cpp): Settings' game timer, actions per
// minute and frame rate. netmgr_queue_cmd hands it each command the player sends, the
// presenter each frame it shows; PortOverlayBuild makes the list the GPU presenter draws.
// PortOverlayReread: Settings changed one of the three ("gameTimer", "apm", "fps").
void PortOverlayReread(void);
void PortOverlayAction(const void *pCmd);
void PortOverlayFrameShown(void);
bool PortOverlayBuild(int nWidth, int nHeight, float fX, float fY, float fW, float fH);

// A layer over the game (the picture's black, a cutscene's words, Debug Tools', the trainer's, a
// watched match's Live bug, a benchmark run's, a room's match starting, the countdown), in one
// list by nOrder: drawn the lowest first, the mouse and keys offered to the top one first. A
// module adds its own with PORT_OVERLAY_LAYER at file scope; a hook it has no use for is NULL.
// Shown says whether it shows now; the other hooks are called either way and answer for
// themselves, so a layer can draw something small while off (Debug Tools' Overlay button).
struct PortOverlayLayer {
  const char *pszName;
  int nOrder;       // PORT_OVERLAY_* below; a new layer takes a number between them
  unsigned uFlags;  // PORT_OVERLAY_FREE_CURSOR and the others below
  bool (*Shown)(void);
  // Where the game goes in a target fW x fH when the layer puts panels round it: the top shown
  // layer with an Area places the game, which otherwise has the whole target.
  void (*Area)(float fW, float fH, float *pfX, float *pfY, float *pfW, float *pfH);
  // Draws into the overlay's list over the game's frame, in the target's pixels, the game's box
  // being fX, fY, fW, fH. True when it drew.
  bool (*Draw)(int nWidth, int nHeight, float fX, float fY, float fW, float fH);
  // Every window message, before anything else sees it, while the layer holds the player's input
  // (a countdown, a cutscene). True: taken.
  bool (*Hold)(unsigned uMsg, unsigned long long wParam, long long lParam);
  // A mouse message, in the window's pixels. True: taken, and the game does not see it.
  bool (*Mouse)(unsigned uMsg, unsigned long long wParam, long long lParam);
  // A key in a game: bDown for a key going down (bRepeat when it is held), else the character a
  // key made. True: taken.
  bool (*Key)(unsigned vk, bool bDown, bool bRepeat);
  // Whether a point in the window is over the layer's panels rather than the game.
  bool (*Outside)(float fX, float fY);
  // The wheel turned, nDelta 120 a notch away from the player, the pointer where the last mouse
  // message put it. True: taken, and the map does not zoom (an add-on's list scrolls).
  bool (*Wheel)(int nDelta);
};

// The built-in layers' places, the lowest first. A benchmark run's, the trainer's, a watched
// match's and a watched farm game's never show together.
enum {
  PORT_OVERLAY_MOTION = 100,    // the picture's black and cinema bars (port_motion.cpp)
  PORT_OVERLAY_CUTSCENE = 200,  // a cutscene's words (port_cutscene.cpp)
  PORT_OVERLAY_SUBTITLES = 250, // a cinematic's subtitles (port_subtitles.cpp)
  PORT_OVERLAY_DEBUG = 300,     // Debug Tools' (port_debugview.cpp)
  PORT_OVERLAY_TRAINER = 400,   // the trainer's strip and Tape (port_trainerview.cpp)
  PORT_OVERLAY_LAB = 450,       // a game of the AI lab's farm watched, its panel (port_labwatch.cpp)
  PORT_OVERLAY_LANWATCH = 500,  // a watched LAN match's Live bug (port_lanfeed.cpp)
  PORT_OVERLAY_BENCH = 600,     // a benchmark run's (port_benchview.cpp)
  PORT_OVERLAY_LANSTART = 650,  // a LAN room's match starting, over the cover (port_lanlobby.cpp)
  PORT_OVERLAY_INTRO = 700,     // the countdown of a skirmish or a room's match (port_intro.cpp)
  PORT_OVERLAY_HUDSEEK = 740,   // the keys of a HUD design's replay position under the pointer (port_hudinput.cpp)
  PORT_OVERLAY_STATS = 750,     // the players' stats a viewer calls up, where no HUD design places them (port_hudstats.cpp)
  PORT_OVERLAY_GAMEMENU = 850,  // the game menu in Hyperion's style (port_front_gamemenu.cpp)
  PORT_OVERLAY_CURSOR = 900,    // the cursor over a HUD design (port_hwcursor.cpp)
};

// What a layer does to the rest while it shows.
enum {
  // A cursor kept to the whole view may reach the whole window, and so the layer's panels.
  PORT_OVERLAY_FREE_CURSOR = 1,
  // The game shows no console (port_console.cpp).
  PORT_OVERLAY_HIDES_CONSOLE = 2,
  // It shows the game's time and frame rate itself, so Settings' timer and frame rate are left out.
  PORT_OVERLAY_OWN_COUNTERS = 4,
  // Drawn between the games of a map switch too, over the black, round the game's box.
  PORT_OVERLAY_OVER_COVER = 8,
  // Drawn out of a game too, over 1.17's own screens and the cinematics; only these are.
  PORT_OVERLAY_OUT_OF_GAME = 16,
  // Its panels are the console's to the game, as a HUD design's are (an add-on's windows):
  // over them the game keeps the pointer and draws its own cursor, the arrow, and the map scrolls
  // at the screen's edges; the layer's Mouse takes the presses, and the moves go on to the game,
  // which sends them no further (SendMouseMovedEvt).
  PORT_OVERLAY_CONSOLE = 32,
  // Alt's keys come to its Key too, and F10's (WM_SYSKEYDOWN, as a key going down) with the
  // characters they make (WM_SYSCHAR): the music player's Alt+G, Alt+E and Shift+F10.
  PORT_OVERLAY_SYSKEYS = 64,
  // The game draws no cursor while it shows: Windows' arrow is the pointer over the whole window,
  // as in Hyperion's menus (the game menu's).
  PORT_OVERLAY_HIDES_CURSOR = 128,
  // Its keys going down come to its Key before 1.17's accelerators, which make some of them
  // commands (F1 Help), and not again through the window: the players' stats' F1 (port_hudstats.cpp).
  PORT_OVERLAY_BEFORE_ACCEL = 256,
};

// Adds a layer; false when the list is full. The layer is kept by its address.
bool PortOverlayAdd(const PortOverlayLayer *pLayer);
#define PORT_OVERLAY_LAYER(layer) static const bool s_bOverlay_##layer = PortOverlayAdd(&(layer))

// The layers asked, by the window's code: where the game goes in a target (PortGpuGameBox), a
// window message held (WndProc, first), a mouse message or a key in a game taken, a point over a
// layer's panels (the cursor's), and whether a shown layer has a flag.
void PortOverlayGameArea(float fW, float fH, float *pfX, float *pfY, float *pfW, float *pfH);
bool PortOverlayHold(unsigned uMsg, unsigned long long wParam, long long lParam);
bool PortOverlayMouse(unsigned uMsg, unsigned long long wParam, long long lParam);
bool PortOverlayKey(unsigned uMsg, unsigned long long wParam, long long lParam);
// A key going down taken before 1.17's accelerators (RunEvtDispatcher), by a layer flagged
// PORT_OVERLAY_BEFORE_ACCEL.
bool PortOverlayKeyFirst(unsigned uMsg, unsigned long long wParam, long long lParam);
bool PortOverlayWheel(int nDelta);
bool PortOverlayOutside(float fX, float fY);
bool PortOverlayHas(unsigned uFlag);
// Whether a point in the window is over the panels of a layer flagged PORT_OVERLAY_CONSOLE, which
// PortOverlayOutside leaves out.
bool PortOverlayConsoleAt(float fX, float fY);

#endif
