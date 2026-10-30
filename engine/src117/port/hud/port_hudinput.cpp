// port_hudinput.cpp -- the mouse over the HUD (port_hudinput.h).
#include <windows.h>
#include <math.h>
#include <string.h>
#include <SDL3/SDL.h>  // SDL_clamp
#include "oc_data.h"
#include "port_hud.h"
#include "port_hudfile.h"
#include "port_hudinput.h"
#include "port_hudstate.h"
#include "port_hudstats.h"
#include "../../h/dialog.h"
#include "../../h/dlg_dlg.h"  // dlg_event
#include "../../h/rect16.h"
#include "../../h/rect32.h"
#include "../../gen/globals117.h"
#include "../front/port_text.h"
#include "../render/port_console.h"
#include "../render/port_maptext.h"  // MapTextUtf8
#include "../render/port_view.h"     // g_portView
#include "port_hudplay.h"             // HudTipContent
#include "CUnit.h"
#include "../gameplay/port_keys.h"     // PortHotkey, PortHotkeyName
#include "../../h/selectlimits.h"     // SelectionWide, StatUnits
#include "../../h/statdata.h"         // StatDataSelectAmong
#include "../../h/CUnitMisc.h"        // get_unit_name_from_type
#include "../../h/order.h"            // status_get_str
#include "../../h/replay.h"           // replay_get_total_frames, replay_get_initial_game_speed
#include "../platform/port_winmsg.h"  // WinMsgKeyState
#include "../render/port_overlay.h"
#include "../replays/port_replayseek.h"  // PortReplaySeekTo, PortReplaySeekTarget
#include "../tools/port_bench.h"  // PortBenchPlaying

#ifdef OC_DLL_MODE
#define sgMinimapArea_g (*reinterpret_cast<rect32_t *>(OC_VA(0x508BB0u)))
#define byte_665E69 (*reinterpret_cast<BYTE *>(OC_VA(0x665E69u)))
#define sgReplayTicks_g (reinterpret_cast<DWORD *>(OC_VA(0x4EF660u)))
#else
extern DWORD sgReplayTicks_g[10];  // a turn's milliseconds at each game speed (replay.cpp)
extern rect32_t sgMinimapArea_g;  // the minimap's cage (minimap.cpp)
extern BYTE byte_665E69;          // Ctrl is down, as byte_665E68 Shift (event_Win.cpp)
#endif
extern BYTE gbInMsgMode;  // 0x6BE658 (textbox.cpp): a message is being typed

namespace {

// The game's pixels and the HUD's units: the HUD is laid over the game's view.
void ToUnits(double x, double y, double lw, double *pu, double *pv) {
  *pu = x * lw / g_portView.nWidth;
  *pv = y * 1080.0 / g_portView.nHeight;
}

void ToGame(double u, double v, double lw, double *px, double *py) {
  *px = u * g_portView.nWidth / lw;
  *py = v * g_portView.nHeight / 1080.0;
}

void ToBox(const HudHit &h, double u, double v, double *px, double *py) {
  *px = h.inv[0] * u + h.inv[2] * v + h.inv[4];
  *py = h.inv[1] * u + h.inv[3] * v + h.inv[5];
}

// A stats slot's view and rows, and whether a point of it is the slot's: the side view lets the
// game have all but its "+N more" row.
bool StatsTakes(const HudHit &h, double lx, double ly) {
  int nView = 0;
  double rowH[2], backing;
  HudStatsSlot(h.pLayer->def, &nView, rowH, &backing);
  PortHudState st;
  PortHudGet(&st);
  return HudStatsTakes(nView, h.w, h.h, rowH, lx, ly, st);
}

// The layer drawn last under game pixel x, y, and the point in its box's units.
const HudHit *HitAt(int x, int y, double *plx, double *ply) {
  double lw = 0.0;
  const std::vector<HudHit> &hits = PortHudHits(&lw);
  if (hits.empty() || lw <= 0.0)
    return NULL;
  double u, v;
  ToUnits(x + 0.5, y + 0.5, lw, &u, &v);
  // the panel drawn last there; in it a slot before the layers drawn over it (a well's glass,
  // a button's frame)
  const HudHit *pTop = NULL;
  for (size_t i = hits.size(); i-- > 0;) {
    const HudHit &h = hits[i];
    if (pTop != NULL && h.nPanel != pTop->nPanel)
      break;
    double lx, ly;
    ToBox(h, u, v, &lx, &ly);
    const bool bPress = h.nSlot >= 0 || h.nAction >= 0;
    if (lx < 0 || ly < 0 || lx > h.w || ly > h.h || (pTop != NULL && !bPress))
      continue;
    if (h.nSlot == HUD_SLOT_STATS && !StatsTakes(h, lx, ly))
      continue;
    pTop = &h;
    *plx = lx;
    *ply = ly;
    if (bPress)
      break;
  }
  return pTop;
}

// A slot's place nIndex of its "places", or the box.
bool Place(const HudHit &h, int nIndex, double q[4]) {
  const PortJson &places = h.pLayer->def["places"];
  if (nIndex < 0 || static_cast<size_t>(nIndex) >= places.size() || places.At(nIndex).size() < 4)
    return false;
  for (int i = 0; i < 4; ++i)
    q[i] = places.At(nIndex).At(i).Num();
  return true;
}

// The place of a slot's places a point is in, or -1.
int PlaceAt(const HudHit &h, double lx, double ly) {
  double q[4];
  for (int i = 0; Place(h, i, q); ++i)
    if (lx >= q[0] && ly >= q[1] && lx <= q[0] + q[2] && ly <= q[1] + q[3])
      return i;
  return -1;
}

// Where a press on a slot goes in 1.17's console: the dialog and the screen pixel there.
struct Target {
  ctrl_t *pDlg = NULL;
  int x = 0, y = 0;
  int nPlace = -1;  // the card's place
};

// A slot's part of the console and where the HUD draws it: the part's dialog and rectangle, and
// its picture's box in the slot's units. nPlace is the card's place.
struct SlotPart {
  ctrl_t *pDlg = NULL;
  rect16_t rc = {};
  double fit[4] = {};
  bool bWhole = false;  // a button or a well: pressed anywhere in it
};

bool PartOf(const HudHit &h, int nPlace, SlotPart *pPart) {
  int nSource = -1, nIndex = 0;
  double box[4] = {0, 0, h.w, h.h};
  switch (h.nSlot) {
  case HUD_SLOT_MINIMAP:
    nSource = PORT_HUD_SRC_MINIMAP;
    break;
  case HUD_SLOT_SELECTION:
    nSource = PORT_HUD_SRC_SELECTION;
    break;
  case HUD_SLOT_PORTRAIT:
    nSource = PORT_HUD_SRC_PORTRAIT;
    break;
  case HUD_SLOT_CARD: {
    // the button fitted in its well as drawn (port_gpu_hud.cpp); no places: nine across
    double q[4];
    if (nPlace < 0 || nPlace > 8)
      return false;
    if (!Place(h, nPlace, q))
      q[0] = h.w * nPlace / 9, q[1] = 0, q[2] = h.w / 9, q[3] = h.h;
    box[0] = q[0] + q[2] * 0.06, box[1] = q[1] + q[3] * 0.06, box[2] = q[2] * 0.88, box[3] = q[3] * 0.88;
    nSource = PORT_HUD_SRC_CARD;
    nIndex = nPlace;
    pPart->bWhole = true;
    break;
  }
  case HUD_SLOT_MENU:
  case HUD_SLOT_TERRAIN:
  case HUD_SLOT_DIPLOMACY:
  case HUD_SLOT_MESSAGING:
    nSource = PORT_HUD_SRC_BUTTON;
    nIndex = h.nSlot == HUD_SLOT_MENU        ? PORT_HUD_BTN_MENU
             : h.nSlot == HUD_SLOT_TERRAIN   ? PORT_HUD_BTN_TERRAIN
             : h.nSlot == HUD_SLOT_DIPLOMACY ? PORT_HUD_BTN_DIPLOMACY
                                             : PORT_HUD_BTN_MESSAGING;
    pPart->bWhole = true;
    break;
  default:
    return false;
  }
  if (!PortConsoleHudPart(nSource, nIndex, &pPart->pDlg, &pPart->rc))
    return false;
  // the part's picture as drawn, fitted in the box; a console button's face is the whole slot
  memcpy(pPart->fit, box, sizeof box);
  if (nSource != PORT_HUD_SRC_BUTTON)
    HudFit(pPart->rc.right - pPart->rc.left + 1, pPart->rc.bottom - pPart->rc.top + 1, box[0], box[1], box[2],
           box[3], pPart->fit);
  return true;
}

// The slot's point lx, ly as a point of the part of the console it shows; nPlace the card's
// place, or -1 to find it. With bClamp (a drag) a point off the picture is taken to its edge.
bool SlotTarget(const HudHit &h, double lx, double ly, int nPlace, bool bClamp, Target *t) {
  if (h.nSlot == HUD_SLOT_CARD && nPlace < 0) {
    double q[4];
    nPlace = Place(h, 0, q) ? PlaceAt(h, lx, ly) : SDL_clamp(static_cast<int>(lx * 9 / h.w), 0, 8);
    if (nPlace < 0)
      return false;  // between the wells
  }
  SlotPart part;
  if (!PartOf(h, nPlace, &part))
    return false;
  const rect16_t &rc = part.rc;
  const int w = rc.right - rc.left + 1, hh = rc.bottom - rc.top + 1;
  double fx = (lx - part.fit[0]) / part.fit[2], fy = (ly - part.fit[1]) / part.fit[3];
  if (!bClamp && !part.bWhole && (fx < 0 || fy < 0 || fx >= 1 || fy >= 1))
    return false;
  fx = SDL_clamp(fx, 0.0, 0.999);
  fy = SDL_clamp(fy, 0.0, 0.999);
  t->pDlg = part.pDlg;
  t->x = part.pDlg->rc.left + rc.left + static_cast<int>(fx * w);
  t->y = part.pDlg->rc.top + rc.top + static_cast<int>(fy * hh);
  t->nPlace = h.nSlot == HUD_SLOT_CARD ? nPlace : -1;
  return true;
}

// The event given to the held dialog at its place in 1.17's console, the cursor there while it
// is handled, as the dialog's handlers read it (the minimap's drag).
bool g_bGiving;  // an event is being given to a held dialog, the cursor at its place there

void Give(const dlgevt_t &evt, const Target &t) {
  dlgevt_t e = evt;
  e.nX = static_cast<short>(t.x);
  e.nY = static_cast<short>(t.y);
  const LONG anCursor[2] = {gCursorPos_g[0], gCursorPos_g[1]};
  gCursorPos_g[0] = t.x;
  gCursorPos_g[1] = t.y;
  PortConsoleHudDeliver(t.pDlg);
  g_bGiving = true;
  dlg_event(&e);
  g_bGiving = false;
  PortConsoleHudDeliver(NULL);
  gCursorPos_g[0] = anCursor[0];
  gCursorPos_g[1] = anCursor[1];
}

// A drag begun on a slot: the layer, its card's place, and the button's up that ends it.
struct Drag {
  bool bOn = false;
  const HudLayerDef *pLayer = NULL;
  int nPlace = -1;
  WORD wUp = 0;
};
Drag g_drag;
ctrl_t *g_pHover;  // the held dialog the last move was given to

// The mouse has left the held dialog it was over: told with a move off it, so its buttons and
// its help go out.
void Leave(void) {
  // still one of the game's held dialogs, compared and not read: a game's end frees them
  if (g_pHover == NULL || !PortConsoleHudHeld(g_pHover)) {
    g_pHover = NULL;
    return;
  }
  dlgevt_t e;
  memset(&e, 0, sizeof e);
  e.wTag = 3;
  e.nX = e.nY = -1;
  Target t;
  t.pDlg = g_pHover;
  t.x = t.y = -1;
  g_pHover = NULL;
  Give(e, t);
}

const HudHit *HitOf(const HudLayerDef *pLayer) {
  double lw = 0.0;
  for (const HudHit &h : PortHudHits(&lw))
    if (h.pLayer == pLayer)
      return &h;
  return NULL;
}

// A button's action, through the same handlers as 1.17's console's buttons and the HUD's own, and
// The HUD's own: the replay's buttons as their slots, the stats' views and pages as F1, Shift+F1 and
// Tab, a seat's vision as the board's eye. The replay's position is a slider (SeekAt).
void PressAction(int nAction, int nArg) {
  static const int kButtons[] = {PORT_HUD_BTN_MENU,    PORT_HUD_BTN_DIPLOMACY, PORT_HUD_BTN_MESSAGING,
                                 PORT_HUD_BTN_TERRAIN, PORT_HUD_BTN_IDLE,      PORT_HUD_BTN_ARMY};
  const char *pszWhy = NULL;
  switch (nAction) {
    case HUD_ACT_GROUP:
      PortHudPressGroup(nArg, &pszWhy);
      return;
    case HUD_ACT_REPLAY_PLAY:
      PortHudPressButton(PORT_HUD_BTN_PLAY, &pszWhy);
      return;
    case HUD_ACT_REPLAY_SLOWER:
      PortHudPressButton(PORT_HUD_BTN_SLOWER, &pszWhy);
      return;
    case HUD_ACT_REPLAY_FASTER:
      PortHudPressButton(PORT_HUD_BTN_FASTER, &pszWhy);
      return;
    case HUD_ACT_REPLAY_TV:
      PortHudPressButton(PORT_HUD_BTN_TV, &pszWhy);
      return;
    case HUD_ACT_REPLAY_FOLLOW:
      PortHudPressButton(PORT_HUD_BTN_FOLLOW, &pszWhy);
      return;
    case HUD_ACT_STATS_POPUP:
      PortHudStatsSet(HUD_STATS_POPUP, !PortHudStatsOn(HUD_STATS_POPUP));
      return;
    case HUD_ACT_STATS_SIDE:
      PortHudStatsSet(HUD_STATS_SIDE, !PortHudStatsOn(HUD_STATS_SIDE));
      return;
    case HUD_ACT_STATS_PAGE: {
      PortHudState st;
      PortHudGet(&st);
      PortHudStatsTurn(nArg == HUD_PAGE_NEXT ? st.nStatsPage + 1 : nArg == HUD_PAGE_PREV ? st.nStatsPage - 1 : nArg);
      return;
    }
    case HUD_ACT_VISION_SEAT:
      PortHudPressEye(nArg, &pszWhy);
      return;
  }
  if (nAction >= 0 && nAction < static_cast<int>(sizeof kButtons / sizeof kButtons[0]))
    PortHudPressButton(kButtons[nAction], &pszWhy);
}

// ---------------------------------------------------------------------------
// The replay's position, a slider over a layer whose action is replay.seek. A press
// moves the replay to the point's share of the box (from the side its "meterFrom" names), a drag
// moves it as it goes, and the release gives replay.seek; with the pointer over it, Left and Right
// step 10 seconds, Page Up and Page Down a minute, Home and End to the ends, and the camera's
// arrows do not see them.

struct Seek {
  bool bOn = false;  // a drag
  const HudLayerDef *pLayer = NULL;
};
Seek g_seek;
bool g_bSeekHover;  // the pointer is over the position

// The replay's frames a second, at the speed it was recorded at, as 1.17's panel counts its time.
double FramesPerSecond(void) {
  const int nInitial = replay_get_initial_game_speed();
  const unsigned nTicks = nInitial >= 0 && nInitial < 10 ? sgReplayTicks_g[nInitial] : 42;
  return 1000.0 / (nTicks != 0 ? nTicks : 42);
}

void SeekAt(const HudHit &h, double lx, double ly) {
  double f = h.nMeterFrom >= 2 ? ly / SDL_max(1.0, h.h) : lx / SDL_max(1.0, h.w);
  if (h.nMeterFrom == 1 || h.nMeterFrom == 3)
    f = 1 - f;
  f = SDL_clamp(f, 0.0, 1.0);
  PortReplaySeekTo(static_cast<unsigned>(f * replay_get_total_frames() + 0.5));
}

void SeekEnd(void) {
  if (!g_seek.bOn)
    return;
  g_seek.bOn = false;
  PortHudSeekDrag(false);
  PortHudSeekEvent();
}

bool SeekShown(void) { return g_bSeekHover || g_seek.bOn; }

bool SeekKey(unsigned vk, bool bDown, bool bRepeat) {
  (void)bRepeat;
  if (!bDown || !SeekShown() || gbInMsgMode || !PortReplaySeekable() || PortOverlayHas(PORT_OVERLAY_HIDES_CURSOR))
    return false;
  const double fps = FramesPerSecond();
  const double now = PortReplaySeekTarget();
  double to;
  switch (vk) {
    case VK_LEFT:
    case VK_DOWN:
      to = now - 10 * fps;
      break;
    case VK_RIGHT:
    case VK_UP:
      to = now + 10 * fps;
      break;
    case VK_NEXT:  // Page Down
      to = now - 60 * fps;
      break;
    case VK_PRIOR:  // Page Up
      to = now + 60 * fps;
      break;
    case VK_HOME:
      to = 0;
      break;
    case VK_END:
      to = replay_get_total_frames();
      break;
    default:
      return false;
  }
  if (PortReplaySeekTo(static_cast<unsigned>(SDL_max(0.0, to))))
    PortHudSeekEvent();
  return true;
}

const PortOverlayLayer kSeekLayer = {"hudseek", PORT_OVERLAY_HUDSEEK, PORT_OVERLAY_BEFORE_ACCEL, SeekShown, NULL,
                                     NULL,      NULL,                 NULL,                      SeekKey,   NULL,
                                     NULL};
PORT_OVERLAY_LAYER(kSeekLayer);

// How a press on a control group's button uses it: as its key does, Ctrl with it making it of
// the selection and Shift adding the selection to it; a right press makes it of the selection,
// with Shift adds to it (Hyperion's; 1.17's console has no such buttons).
int GroupHow(bool bRight) {
  if (byte_665E68)  // Shift
    return PORT_HUD_USE_ADD;
  return bRight || byte_665E69 ? PORT_HUD_USE_SET : PORT_HUD_USE_RECALL;
}

// A replay's button a slot is, or -1.
int ReplayButton(int nSlot) {
  switch (nSlot) {
  case HUD_SLOT_PLAY:
    return PORT_HUD_BTN_PLAY;
  case HUD_SLOT_SLOWER:
    return PORT_HUD_BTN_SLOWER;
  case HUD_SLOT_FASTER:
    return PORT_HUD_BTN_FASTER;
  case HUD_SLOT_TV:
    return PORT_HUD_BTN_TV;
  case HUD_SLOT_FOLLOW:
    return PORT_HUD_BTN_FOLLOW;
  }
  return -1;
}

// The players' board's row whose eye a point of the slot is on, or NULL; the rows into *pRows.
const HudBoardRow *EyeAt(const HudHit &h, double lx, double ly, const PortHudState &st, std::vector<HudBoardRow> *pRows) {
  HudBoardOf(h.pLayer->def, h.w, h.h, st, pRows);
  for (const HudBoardRow &r : *pRows)
    if (r.nPlayer >= 0 && st.aPlayers[r.nPlayer].bEye && lx >= r.eye[0] && ly >= r.eye[1] && lx < r.eye[0] + r.eye[2] &&
        ly < r.eye[1] + r.eye[3])
      return &r;
  return NULL;
}

// The players' board's "+N more" row a point of the slot is on, or NULL.
const HudBoardRow *MoreAt(const HudHit &h, double lx, double ly, const PortHudState &st, std::vector<HudBoardRow> *pRows) {
  HudBoardOf(h.pLayer->def, h.w, h.h, st, pRows);
  for (const HudBoardRow &r : *pRows)
    if (r.nPlayer < 0 && lx >= r.row[0] && ly >= r.row[1] && lx < r.row[0] + r.row[2] && ly < r.row[1] + r.row[3])
      return &r;
  return NULL;
}

// The HUD's own buttons: the next idle worker, the army, a control group, a replay's buttons and
// the players' eyes; a right press only a group's.
void PressOwn(const HudHit &h, double lx, double ly, bool bRight) {
  const char *pszWhy = NULL;
  if (h.nAction == HUD_ACT_GROUP) {
    PortHudPressGroup(h.nActionArg, &pszWhy, GroupHow(bRight));
  } else if (h.nSlot == HUD_SLOT_GROUPS) {
    int i = PlaceAt(h, lx, ly);
    double q[4];
    if (i < 0 && !Place(h, 0, q))
      i = SDL_clamp(static_cast<int>(lx * 10 / h.w), 0, 9);
    if (i >= 0 && i < 10)
      PortHudPressGroup((i + 1) % 10, &pszWhy, GroupHow(bRight));
  } else if (bRight) {
    return;
  } else if (h.nAction == HUD_ACT_REPLAY_SEEK) {
    return;  // a slider (SeekAt)
  } else if (h.nAction >= 0) {
    PressAction(h.nAction, h.nActionArg);
  } else if (h.nSlot == HUD_SLOT_IDLE) {
    PortHudPressButton(PORT_HUD_BTN_IDLE, &pszWhy);
  } else if (h.nSlot == HUD_SLOT_ARMY) {
    PortHudPressButton(PORT_HUD_BTN_ARMY, &pszWhy);
  } else if (ReplayButton(h.nSlot) >= 0) {
    PortHudPressButton(ReplayButton(h.nSlot), &pszWhy);
  } else if (h.nSlot == HUD_SLOT_PLAYERS) {
    PortHudState st;
    PortHudGet(&st);
    std::vector<HudBoardRow> rows;
    const HudBoardRow *r = EyeAt(h, lx, ly, st, &rows);
    if (r != NULL)
      PortHudPressEye(st.aPlayers[r->nPlayer].nSlot, &pszWhy);
    else if (MoreAt(h, lx, ly, st, &rows) != NULL)
      PortHudStatsSet(HUD_STATS_POPUP, true);
  } else if (h.nSlot == HUD_SLOT_STATS) {
    int nView = 0;
    double rowH[2], backing;
    HudStatsSlot(h.pLayer->def, &nView, rowH, &backing);
    PortHudState st;
    PortHudGet(&st);
    HudStatsPress(nView, h.w, h.h, rowH, lx, ly, st);
  }
}

// The HUD's own buttons' tooltips, which no bubble of 1.17's has: the idle worker, the
// army, a control group, and a layer's action. The one showing: its action, a group's 100 and up,
// -1 for none.
int g_nOwnTip = -1;

// An own tooltip, nId, for the box q of a layer in its units: the box through the layer's
// transform, in the screen's units.
void OwnTipAt(int nId, const HudTipContent &c, const HudHit &h, const double q[4]) {
  if (nId == g_nOwnTip)
    return;
  double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
  for (int k = 0; k < 4; ++k) {
    const double bx = q[0] + ((k & 1) ? q[2] : 0), by = q[1] + ((k & 2) ? q[3] : 0);
    const double u = h.m[0] * bx + h.m[2] * by + h.m[4], v = h.m[1] * bx + h.m[3] * by + h.m[5];
    x0 = SDL_min(x0, u), y0 = SDL_min(y0, v), x1 = SDL_max(x1, u), y1 = SDL_max(y1, v);
  }
  const double box[4] = {x0, y0, x1 - x0, y1 - y0};
  g_nOwnTip = PortHudTip(&c, box, h.nPanel) ? nId : -1;
}

// A replay's buttons' and the board's eyes' tooltips, in the design center's words
// (hud.js's buttonFace), and the players' stats': true when the pointer is on one, its
// tooltip shown.
bool ReplayTip(const HudHit *h, double lx, double ly) {
  if (h == NULL || (ReplayButton(h->nSlot) < 0 && h->nSlot != HUD_SLOT_PLAYERS && h->nSlot != HUD_SLOT_STATS))
    return false;
  PortHudState st;
  PortHudGet(&st);
  HudTipContent c;
  char szKey[48] = "";
  double q[4] = {0, 0, h->w, h->h};
  int nId = -1;
  switch (h->nSlot) {
    case HUD_SLOT_PLAY:
      nId = st.bPaused ? 200 : 201;  // its words change with the replay's state
      c.title = st.bPaused ? PortText("Play") : PortText("Pause");
      lstrcpynA(szKey, "P", sizeof szKey);
      break;
    case HUD_SLOT_SLOWER:
      nId = 202;
      c.title = PortText("Slower");
      lstrcpynA(szKey, "D", sizeof szKey);
      break;
    case HUD_SLOT_FASTER:
      nId = 203;
      c.title = PortText("Faster");
      lstrcpynA(szKey, "U", sizeof szKey);
      break;
    case HUD_SLOT_TV:
      nId = 204;
      c.title = PortText("Replay TV");
      if (PortHotkey(PORT_HOTKEY_TV) != 0)
        PortHotkeyName(PortHotkey(PORT_HOTKEY_TV), szKey, sizeof szKey);
      c.lines.push_back(PortText("The camera goes where the fighting is."));
      break;
    case HUD_SLOT_FOLLOW:
      nId = 205;
      c.title = PortText("Follow");
      if (PortHotkey(PORT_HOTKEY_FOLLOW) != 0)
        PortHotkeyName(PortHotkey(PORT_HOTKEY_FOLLOW), szKey, sizeof szKey);
      c.lines.push_back(PortText("Keeps the selected unit in the middle of the screen."));
      break;
    case HUD_SLOT_STATS: {
      int nView = 0;
      double rowH[2], backing;
      HudStatsSlot(h->pLayer->def, &nView, rowH, &backing);
      std::string key;
      nId = HudStatsTip(nView, h->w, h->h, rowH, lx, ly, st, &c.title, &key, q);
      lstrcpynA(szKey, key.c_str(), sizeof szKey);
      break;
    }
    default: {
      std::vector<HudBoardRow> rows;
      const HudBoardRow *r = EyeAt(*h, lx, ly, st, &rows);
      if (r == NULL && (r = MoreAt(*h, lx, ly, st, &rows)) != NULL) {
        nId = 403;
        c.title = PortText("Every player, in the stats");
        lstrcpynA(szKey, "F1", sizeof szKey);
        memcpy(q, r->row, sizeof q);
        break;
      }
      if (r == NULL)
        break;
      const PortHudPlayer &p = st.aPlayers[r->nPlayer];
      nId = 300 + p.nSlot * 2 + (p.bSeen ? 1 : 0);
      // {0} the player's name
      c.title = p.bSeen ? PortTextF("Hide {0}\xE2\x80\x99s vision", {p.szName})
                        : PortTextF("Show {0}\xE2\x80\x99s vision", {p.szName});
      memcpy(q, r->eye, sizeof q);
      break;
    }
  }
  if (nId < 0) {
    if (g_nOwnTip >= 0) {
      g_nOwnTip = -1;
      PortHudTip(NULL, NULL, -1);
    }
    return true;
  }
  c.hotkey = szKey;
  OwnTipAt(nId, c, *h, q);
  return true;
}

void OwnTip(const HudHit *h, double lx, double ly) {
  if (ReplayTip(h, lx, ly))
    return;
  int nKind = -1, nArg = 0;
  double q[4] = {0, 0, 0, 0};
  if (h != NULL) {
    q[2] = h->w, q[3] = h->h;
    if (h->nAction >= 0 && h->nAction != HUD_ACT_UNKNOWN) {
      nKind = h->nAction, nArg = h->nActionArg;
    } else if (h->nSlot == HUD_SLOT_IDLE) {
      nKind = HUD_ACT_IDLE;
    } else if (h->nSlot == HUD_SLOT_ARMY) {
      nKind = HUD_ACT_ARMY;
    } else if (h->nSlot == HUD_SLOT_GROUPS) {
      int i = PlaceAt(*h, lx, ly);
      if (i >= 0 && i < 10) {
        Place(*h, i, q);
      } else if (!Place(*h, 0, q)) {
        i = SDL_clamp(static_cast<int>(lx * 10 / h->w), 0, 9);
        q[0] = h->w * i / 10, q[1] = 0, q[2] = h->w / 10, q[3] = h->h;
      }
      if (i >= 0 && i < 10)
        nKind = HUD_ACT_GROUP, nArg = (i + 1) % 10;
    }
  }
  int nId = nKind == HUD_ACT_GROUP ? 100 + nArg : nKind;
  if (nKind == HUD_ACT_REPLAY_SEEK) {
    PortHudState st;
    PortHudGet(&st);
    nId = 1000 + st.nReplaySeconds;  // shown again as the time moves
  } else if (nKind == HUD_ACT_STATS_PAGE) {
    nId = 600 + nArg + 2;
  } else if (nKind == HUD_ACT_VISION_SEAT) {
    nId = 700 + nArg;
  }
  if (nId == g_nOwnTip)
    return;
  if (nId < 0) {
    g_nOwnTip = -1;
    PortHudTip(NULL, NULL, -1);
    return;
  }
  HudTipContent c;
  char szKey[48] = "";
  auto key = [&](int nWhich) {
    if (PortHotkey(nWhich) != 0)
      PortHotkeyName(PortHotkey(nWhich), szKey, sizeof szKey);
  };
  switch (nKind) {
    case HUD_ACT_MENU:
      c.title = PortText("Game Menu");
      lstrcpynA(szKey, "F10", sizeof szKey);
      break;
    case HUD_ACT_REPLAY_PLAY:
      c.title = PortText("Play or Pause");
      lstrcpynA(szKey, "P", sizeof szKey);
      break;
    case HUD_ACT_REPLAY_SLOWER:
      c.title = PortText("Slower");
      lstrcpynA(szKey, "D", sizeof szKey);
      break;
    case HUD_ACT_REPLAY_FASTER:
      c.title = PortText("Faster");
      lstrcpynA(szKey, "U", sizeof szKey);
      break;
    case HUD_ACT_REPLAY_TV:
      c.title = PortText("Replay TV");
      key(PORT_HOTKEY_TV);
      c.lines.push_back(PortText("The camera goes where the fighting is."));
      break;
    case HUD_ACT_REPLAY_FOLLOW:
      c.title = PortText("Follow");
      key(PORT_HOTKEY_FOLLOW);
      c.lines.push_back(PortText("Keeps the selected unit in the middle of the screen."));
      break;
    case HUD_ACT_REPLAY_SEEK: {
      PortHudState st;
      PortHudGet(&st);
      char szAt[32], szOf[32];
      SDL_snprintf(szAt, sizeof szAt, "%d:%02d", st.nReplaySeconds / 60, st.nReplaySeconds % 60);
      SDL_snprintf(szOf, sizeof szOf, "%d:%02d", st.nReplayLength / 60, st.nReplayLength % 60);
      c.title = PortText("Replay Position");
      // {0} where the replay is, {1} how long it is
      c.lines.push_back(PortTextF("{0} of {1}", {szAt, szOf}));
      c.lines.push_back(PortText("Click or drag to move through the replay."));
      c.lines.push_back(
          PortText("Left and Right: 10 seconds. Page Up and Page Down: a minute. Home and End: the start or the end."));
      break;
    }
    case HUD_ACT_STATS_POPUP:
      c.title = PortText("Stats");
      lstrcpynA(szKey, "F1", sizeof szKey);
      c.lines.push_back(PortText("Every figure of every player."));
      break;
    case HUD_ACT_STATS_SIDE:
      c.title = PortText("Stats at the Side");
      lstrcpynA(szKey, "Shift+F1", sizeof szKey);
      break;
    case HUD_ACT_STATS_PAGE: {
      static const char *const s_apszPages[] = {PT_("Overview"), PT_("Income"), PT_("Army"), PT_("Units Lost")};
      c.title = nArg == HUD_PAGE_NEXT   ? PortText("Next Page")
                : nArg == HUD_PAGE_PREV ? PortText("Page Before")
                                        : PortText(s_apszPages[nArg & 3]);
      lstrcpynA(szKey, nArg == HUD_PAGE_PREV ? "Shift+Tab" : nArg == HUD_PAGE_NEXT ? "Tab" : "", sizeof szKey);
      break;
    }
    case HUD_ACT_VISION_SEAT: {
      PortHudState st;
      PortHudGet(&st);
      const PortHudPlayer *p = NULL;
      for (int i = 0; i < st.nPlayers; ++i)
        if (st.aPlayers[i].nSlot == nArg)
          p = &st.aPlayers[i];
      if (p == NULL)
        return;
      // {0} the player's name
      c.title = p->bSeen ? PortTextF("Hide {0}\xE2\x80\x99s vision", {p->szName})
                         : PortTextF("Show {0}\xE2\x80\x99s vision", {p->szName});
      break;
    }
    case HUD_ACT_DIPLOMACY:
      c.title = gPlayback ? PortText("View Players") : PortText("Diplomacy");
      break;
    case HUD_ACT_MESSAGING:
      c.title = PortText("Messaging");
      break;
    case HUD_ACT_TERRAIN:
      c.title = PortText("Minimap Terrain");
      if (SelectionWide())
        key(PORT_HOTKEY_TERRAIN);
      else
        lstrcpynA(szKey, "Tab", sizeof szKey);
      c.lines.push_back(PortText("The minimap's terrain on or off."));
      break;
    case HUD_ACT_IDLE:
      c.title = PortText("Idle Worker");
      key(PORT_HOTKEY_IDLE);
      c.lines.push_back(PortText("Selects your next idle worker and takes the view to it."));
      break;
    case HUD_ACT_ARMY:
      c.title = PortText("Select Army");
      key(PORT_HOTKEY_ARMY);
      c.lines.push_back(PortText("Selects every unit of yours that fights."));
      break;
    case HUD_ACT_GROUP:
      c.title = PortTextF("Control Group {0}", {std::to_string(nArg)});
      szKey[0] = static_cast<char>('0' + nArg), szKey[1] = '\0';
      c.lines.push_back(PortText("Selects the group; twice, takes the view to it."));
      c.lines.push_back(PortText("Right-click or Ctrl: makes it of the selection. Shift: adds the selection to it."));
      break;
    default:
      return;
  }
  if (!h->label.empty())
    c.title = h->label;
  c.hotkey = szKey;
  OwnTipAt(nId, c, *h, q);
}

// A page of the selection's grid, with its layout, when the slot shows one now.
bool GridOf(const HudHit &h, HudGrid *pGrid, int *pnFirst) {
  PortHudState st;
  PortHudGet(&st);
  if (h.nSlot != HUD_SLOT_SELECTION || st.nSelected < 2 || !HudGridOf(h.pLayer->def["grid"], h.w, h.h, st.nSelected, pGrid))
    return false;
  *pnFirst = PortHudGridPage(pGrid->per, st.nSelected) * pGrid->per;
  return true;
}

// The mouse over the selection's grid: a unit's cell selects among the selection as a click on
// its wireframe in 1.17's panel does, a page tab shows its page, and a cell's tooltip is the
// unit's name and what a click does, 1.17's words. False when the slot shows no grid now: its
// press is 1.17's panel's.
bool GridMouse(const HudHit &h, double lx, double ly, WORD wTag) {
  HudGrid g;
  int nFirst = 0;
  if (!GridOf(h, &g, &nFirst))
    return false;
  double q[4];
  int nCell = -1, nTab = -1;
  for (int p = 0; g.bTabs && p < g.pages && nTab < 0; ++p) {
    g.Tab(p, q);
    if (lx >= q[0] && ly >= q[1] && lx < q[0] + q[2] && ly < q[1] + q[3])
      nTab = p;
  }
  if (nTab < 0 && lx >= g.x0 && ly >= g.y0) {
    const int c = static_cast<int>((lx - g.x0) / g.cw), r = static_cast<int>((ly - g.y0) / g.ch);
    if (c < g.cols && r < g.rows && nFirst + r * g.cols + c < SelectionMax())
      nCell = r * g.cols + c;
  }
  CUnit *pUnit = nCell >= 0 ? StatUnits()[nFirst + nCell] : NULL;
  if (wTag == 3) {
    if (pUnit == NULL) {
      OwnTip(NULL, 0, 0);
      return true;
    }
    HudTipContent c;
    c.title = MapTextUtf8(get_unit_name_from_type(pUnit->wType));
    const char *psz = status_get_str(0x329);  // Click: Select Unit, Shift: Deselect, Ctrl: its type
    for (const char *p = psz; p != NULL && *p != '\0';) {
      const char *pEnd = strchr(p, '\n');
      const std::string line = MapTextUtf8(std::string(p, pEnd != NULL ? pEnd - p : strlen(p)).c_str());
      if (!line.empty() && c.lines.size() < HUD_TIP_LINES)
        c.lines.push_back(line);
      p = pEnd != NULL ? pEnd + 1 : p + strlen(p);
    }
    g.Cell(nCell, q);
    OwnTipAt(1000 + nFirst + nCell, c, h, q);
    return true;
  }
  if (wTag == 4 || wTag == 6) {
    if (nTab >= 0)
      PortHudGridTurn(nTab);
    else if (pUnit != NULL && !gPlayback)
      StatDataSelectAmong(pUnit);
  }
  return true;
}

}  // namespace

bool PortHudOver(int nX, int nY) {
  double lx, ly;
  return PortConsoleHudOn() && HitAt(nX, nY, &lx, &ly) != NULL;
}

bool PortHudMouse(const dlgevt_t *pEvent) {
  const WORD wTag = pEvent->wTag;
  if (!PortConsoleHudOn() || gwGameMode != 3) {
    g_drag.bOn = false;
    if (g_seek.bOn) {
      g_seek.bOn = false;
      PortHudSeekDrag(false);
    }
    g_bSeekHover = false;
    g_pHover = NULL;
    PortHudPointer(false, 0, 0);
    return false;
  }
  if (wTag < 3 || wTag > 12)
    return false;
  // a drag begun on a slot goes on there until its button is up
  if (g_drag.bOn) {
    const HudHit *h = HitOf(g_drag.pLayer);
    double lw = 0.0, u, v, lx, ly;
    PortHudHits(&lw);
    Target t;
    if (h != NULL && lw > 0.0) {
      ToUnits(pEvent->nX + 0.5, pEvent->nY + 0.5, lw, &u, &v);
      ToBox(*h, u, v, &lx, &ly);
      if (SlotTarget(*h, lx, ly, g_drag.nPlace, true, &t))
        Give(*pEvent, t);
    }
    if (wTag == g_drag.wUp)
      g_drag.bOn = false;
    return true;
  }
  // a drag of the replay's position goes on until the left button is up
  if (g_seek.bOn) {
    const HudHit *h = HitOf(g_seek.pLayer);
    double lw = 0.0, u, v, lx, ly;
    PortHudHits(&lw);
    if (h != NULL && lw > 0.0 && (wTag == 3 || wTag == 5)) {
      ToUnits(pEvent->nX + 0.5, pEvent->nY + 0.5, lw, &u, &v);
      ToBox(*h, u, v, &lx, &ly);
      SeekAt(*h, lx, ly);
    }
    if (wTag != 3)
      SeekEnd();
    return true;
  }
  // the panels that light under the pointer (panel.hover, panel.leave)
  if (wTag == 3) {
    double lw = 0.0, u, v;
    PortHudHits(&lw);
    if (lw > 0.0) {
      ToUnits(pEvent->nX + 0.5, pEvent->nY + 0.5, lw, &u, &v);
      PortHudPointer(true, u, v);
    }
  }
  // a drag of the game's own, a box drawn over the map, keeps its events
  if (sgCtrlHooks[wTag] != NULL)
    return false;
  const bool bDown = wTag == 4 || wTag == 6 || wTag == 7 || wTag == 9 || wTag == 10 || wTag == 12;
  double lx, ly;
  const HudHit *h = HitAt(pEvent->nX, pEvent->nY, &lx, &ly);
  if (wTag == 3)
    g_bSeekHover = h != NULL && h->nAction == HUD_ACT_REPLAY_SEEK;
  if (h != NULL && (h->nSlot >= 0 || h->nAction >= 0) && (wTag == 4 || wTag == 6))
    PortHudPressed(h->nPanel, h->nSlot, h->nSlot < 0 ? h->nAction : -1, h->nActionArg);
  // the replay's position: the press moves the replay there, and a drag begins
  if (h != NULL && h->nAction == HUD_ACT_REPLAY_SEEK && (wTag == 4 || wTag == 6)) {
    g_seek.bOn = true;
    g_seek.pLayer = h->pLayer;
    PortHudSeekDrag(true);
    SeekAt(*h, lx, ly);
    return true;
  }
  // the selection's grid, the HUD's own over 1.17's panel of twelve
  if (h != NULL && h->nSlot == HUD_SLOT_SELECTION && GridMouse(*h, lx, ly, wTag)) {
    if (wTag == 3)
      Leave();
    return bDown;
  }
  // a benchmark run's Menu, whose button in 1.17's console the run's overlay hides: the game's
  // Menu, which in a run asks whether to stop (do_gamemenu_dlg)
  if (h != NULL && h->nSlot == HUD_SLOT_MENU && PortBenchPlaying()) {
    if (wTag == 4) {
      const char *pszWhy = NULL;
      PortHudPressButton(PORT_HUD_BTN_MENU, &pszWhy);
    }
    return bDown;
  }
  Target t;
  if (h != NULL && h->nSlot >= 0 && SlotTarget(*h, lx, ly, -1, false, &t)) {
    if (wTag == 3 && g_nOwnTip >= 0)
      OwnTip(NULL, 0, 0);  // the own button's tooltip goes; 1.17's may come
    if (wTag == 3 && g_pHover != t.pDlg)
      Leave();
    if (wTag == 3)
      g_pHover = t.pDlg;
    Give(*pEvent, t);
    if (bDown) {
      g_drag.bOn = true;
      g_drag.pLayer = h->pLayer;
      g_drag.nPlace = t.nPlace;
      g_drag.wUp = wTag <= 6 ? 5 : wTag <= 9 ? 8 : 11;
    }
    return true;
  }
  if (wTag == 3) {
    Leave();
    OwnTip(h, lx, ly);
  }
  if (h == NULL)
    return false;
  // the HUD's own buttons take a left press, a group's a right one too
  if ((h->nSlot >= 0 || h->nAction >= 0) && (wTag == 4 || wTag == 6 || wTag == 7))
    PressOwn(*h, lx, ly, wTag == 7);
  // a press on a panel is the console's, never the map's; a move is the game's, which finds the
  // console there (console_collision), and so is a button's up, which a drag of its own may want
  return bDown;
}

// The box, in the screen's units, that the HUD draws a control of 1.17's console in, and its
// panel: a card button's place, or the least part of a slot that holds game pixel nX, nY.
static bool ControlBox(int nPlace, int nX, int nY, double box[4], int *pnPanel) {
  double lw = 0.0;
  const std::vector<HudHit> &hits = PortHudHits(&lw);
  if (lw <= 0.0)
    return false;
  const HudHit *pHit = NULL;
  SlotPart best;
  long nLeast = 0;
  for (const HudHit &h : hits) {
    if (h.nSlot < 0 || (nPlace >= 0 && h.nSlot != HUD_SLOT_CARD))
      continue;
    for (int p = h.nSlot == HUD_SLOT_CARD ? 0 : -1; p < (h.nSlot == HUD_SLOT_CARD ? 9 : 0); ++p) {
      SlotPart part;
      if ((nPlace >= 0 && p != nPlace) || !PartOf(h, p, &part))
        continue;
      const rect16_t &rc = part.rc;
      const int w = rc.right - rc.left + 1, hh = rc.bottom - rc.top + 1;
      if (nPlace < 0) {
        const int px = nX - part.pDlg->rc.left - rc.left, py = nY - part.pDlg->rc.top - rc.top;
        if (px < 0 || py < 0 || px >= w || py >= hh || (pHit != NULL && static_cast<long>(w) * hh >= nLeast))
          continue;
      }
      pHit = &h;
      best = part;
      nLeast = static_cast<long>(w) * hh;
    }
  }
  if (pHit == NULL)
    return false;
  // the part's picture's corners through the slot's transform, and the box round them
  double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
  const double *m = pHit->m, *f = best.fit;
  for (int k = 0; k < 4; ++k) {
    const double bx = f[0] + ((k & 1) ? f[2] : 0), by = f[1] + ((k & 2) ? f[3] : 0);
    const double u = m[0] * bx + m[2] * by + m[4], v = m[1] * bx + m[3] * by + m[5];
    x0 = SDL_min(x0, u), y0 = SDL_min(y0, v), x1 = SDL_max(x1, u), y1 = SDL_max(y1, v);
  }
  box[0] = x0, box[1] = y0, box[2] = x1 - x0, box[3] = y1 - y0;
  *pnPanel = pHit->nPanel;
  return true;
}

bool PortHudTipShow(int nPlace, int nX, int nY, const char *pszName, bool bMark, const char *pszKey,
                    const char *pszLines, const int anCosts[4], const int anRaces[4]) {
  if (!PortConsoleHudOn() || gwGameMode != 3 || pszName == NULL)
    return false;
  double box[4];
  int nPanel = -1;
  if (!ControlBox(nPlace, nX, nY, box, &nPanel))
    return false;
  HudTipContent c;
  // the name: its printable bytes, the letter after 1.17's highlight colour marked, then UTF-8
  std::string name;
  int nMark = -1;
  bool bNext = false;
  for (const char *p = pszName; *p != '\0' && *p != '\n'; ++p) {
    const unsigned char b = static_cast<unsigned char>(*p);
    if (b < 0x20) {
      bNext = bMark && b == 0x03 && nMark < 0;
      continue;
    }
    if (bNext) {
      nMark = static_cast<int>(name.size());
      bNext = false;
    }
    name += *p;
  }
  c.title = MapTextUtf8(name.c_str());
  if (nMark >= 0)
    c.nMark = static_cast<int>(MapTextUtf8(name.substr(0, nMark).c_str()).size());
  c.hotkey = pszKey != NULL ? pszKey : "";
  for (const char *p = pszLines; p != NULL && *p != '\0';) {
    const char *pEnd = strchr(p, '\n');
    const std::string line = MapTextUtf8(std::string(p, pEnd != NULL ? pEnd - p : strlen(p)).c_str());
    if (!line.empty() && c.lines.size() < HUD_TIP_LINES)
      c.lines.push_back(line);
    p = pEnd != NULL ? pEnd + 1 : p + strlen(p);
  }
  // the costs, and those the player lacks, as the game would refuse them
  PortHudState st;
  PortHudGet(&st);
  for (int k = 0; k < HUD_TIP_COSTS; ++k) {
    if (anCosts != NULL && anCosts[k] > 0)
      c.costs[k] = std::to_string(anCosts[k]);
    if (anRaces != NULL)
      c.anRace[k] = anRaces[k];
  }
  if (anCosts != NULL) {
    c.abShort[0] = anCosts[0] > st.nMinerals;
    c.abShort[1] = anCosts[1] > st.nGas;
    c.abShort[2] = anCosts[2] > 0 && st.nSupplyUsed + anCosts[2] > st.nSupplyMax;
    c.abShort[3] = anCosts[3] > 0 && gpStatUnit != NULL && gpStatUnit->wEnergy / 256 < anCosts[3];
  }
  g_nOwnTip = -1;
  return PortHudTip(&c, box, nPanel);
}

void PortHudTipHide(void) {
  g_nOwnTip = -1;  // shown again at the next move over it
  PortHudTip(NULL, NULL, -1);
}

void PortHudHelpPlace(int nX, int nY, rect16_t *pRect) {
  if (!PortConsoleHudOn() || gwGameMode != 3)
    return;
  double lw = 0.0;
  const std::vector<HudHit> &hits = PortHudHits(&lw);
  if (lw <= 0.0)
    return;
  // the slot that shows the point, the least part that holds it (a button before the dialog it
  // is in), and the point where the HUD draws it, in game pixels
  const HudHit *pHit = NULL;
  double ax = 0, ay = 0;
  long nLeast = 0;
  for (const HudHit &h : hits) {
    for (int nPlace = h.nSlot == HUD_SLOT_CARD ? 0 : -1; nPlace < (h.nSlot == HUD_SLOT_CARD ? 9 : 0); ++nPlace) {
      SlotPart part;
      if (h.nSlot < 0 || !PartOf(h, nPlace, &part))
        continue;
      const rect16_t &rc = part.rc;
      const int px = nX - part.pDlg->rc.left - rc.left, py = nY - part.pDlg->rc.top - rc.top;
      const int w = rc.right - rc.left + 1, hh = rc.bottom - rc.top + 1;
      if (px < 0 || py < 0 || px >= w || py >= hh || (pHit != NULL && static_cast<long>(w) * hh >= nLeast))
        continue;
      const double bx = part.fit[0] + part.fit[2] * px / w, by = part.fit[1] + part.fit[3] * py / hh;
      ToGame(h.m[0] * bx + h.m[2] * by + h.m[4], h.m[1] * bx + h.m[3] * by + h.m[5], lw, &ax, &ay);
      pHit = &h;
      nLeast = static_cast<long>(w) * hh;
    }
  }
  // off the panel, which is drawn over the game: above it, or beside a column
  double r[4];
  if (pHit == NULL || !PortHudPanelRect(pHit->nPanel, r))
    return;
  double x0, y0, x1, y1;
  ToGame(r[0], r[1], lw, &x0, &y0);
  ToGame(r[0] + r[2], r[1] + r[3], lw, &x1, &y1);
  const int bw = pRect->right - pRect->left + 1, bh = pRect->bottom - pRect->top + 1;
  const int nW = g_portView.nWidth, nH = g_portView.nHeight;
  int left = static_cast<int>(ax), top = static_cast<int>(y0) - bh - 4;
  if (top < 0) {
    top = static_cast<int>(ay);
    left = x0 > nW - x1 ? static_cast<int>(x0) - bw - 4 : static_cast<int>(x1) + 4;
  }
  left = SDL_clamp(left, 0, SDL_max(0, nW - bw));
  top = SDL_clamp(top, 0, SDL_max(0, nH - bh));
  pRect->left = static_cast<short>(left);
  pRect->top = static_cast<short>(top);
  pRect->right = static_cast<short>(left + bw - 1);
  pRect->bottom = static_cast<short>(top + bh - 1);
}

void PortHudConsoleCursor(LONG anOut[2]) {
  anOut[0] = gCursorPos_g[0];
  anOut[1] = gCursorPos_g[1];
  if (!PortConsoleHudOn() || gwGameMode != 3 || g_bGiving)
    return;
  // where the pointer is in 1.17's console: through the slot it is over, else nowhere in it
  anOut[0] = anOut[1] = -1;
  double lx, ly;
  Target t;
  const HudHit *h = HitAt(gCursorPos_g[0], gCursorPos_g[1], &lx, &ly);
  if (h != NULL && h->nSlot >= 0 && SlotTarget(*h, lx, ly, -1, false, &t)) {
    anOut[0] = t.x;
    anOut[1] = t.y;
  }
}

std::string PortHudHitsJson(void) {
  double lw = 0.0;
  const std::vector<HudHit> &hits = PortHudHits(&lw);
  std::string j = "\"hits\":[";
  for (size_t i = 0; i < hits.size() && lw > 0.0; ++i) {
    const HudHit &h = hits[i];
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (int c = 0; c < 4; ++c) {
      const double bx = c & 1 ? h.w : 0, by = c & 2 ? h.h : 0;
      double gx, gy;
      ToGame(h.m[0] * bx + h.m[2] * by + h.m[4], h.m[1] * bx + h.m[3] * by + h.m[5], lw, &gx, &gy);
      x0 = SDL_min(x0, gx), y0 = SDL_min(y0, gy), x1 = SDL_max(x1, gx), y1 = SDL_max(y1, gy);
    }
    char sz[160];
    SDL_snprintf(sz, sizeof sz, "%s{\"panel\":%d,\"slot\":\"%s\",\"action\":%d,\"box\":[%d,%d,%d,%d]}", i ? "," : "",
                 h.nPanel, HudSlotName(h.nSlot), h.nAction, static_cast<int>(x0), static_cast<int>(y0),
                 static_cast<int>(x1), static_cast<int>(y1));
    j += sz;
  }
  return j + "]";
}

void PortHudCage(rect32_t *pRect) {
  if (!PortConsoleHudOn() || pRect == NULL || memcmp(pRect, &sgMinimapArea_g, sizeof *pRect) != 0)
    return;
  double lw = 0.0;
  const std::vector<HudHit> &hits = PortHudHits(&lw);
  ctrl_t *pDlg = NULL;
  rect16_t rc;
  if (lw <= 0.0 || !PortConsoleHudPart(PORT_HUD_SRC_MINIMAP, 0, &pDlg, &rc))
    return;
  for (const HudHit &h : hits) {
    if (h.nSlot != HUD_SLOT_MINIMAP)
      continue;
    double fit[4];
    HudFit(rc.right - rc.left + 1, rc.bottom - rc.top + 1, 0, 0, h.w, h.h, fit);
    // the picture's corners through the slot's placing, to the game's pixels
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (int c = 0; c < 4; ++c) {
      const double bx = fit[0] + (c & 1 ? fit[2] : 0), by = fit[1] + (c & 2 ? fit[3] : 0);
      const double u = h.m[0] * bx + h.m[2] * by + h.m[4], v = h.m[1] * bx + h.m[3] * by + h.m[5];
      double gx, gy;
      ToGame(u, v, lw, &gx, &gy);
      x0 = gx < x0 ? gx : x0, y0 = gy < y0 ? gy : y0, x1 = gx > x1 ? gx : x1, y1 = gy > y1 ? gy : y1;
    }
    pRect->left = static_cast<LONG>(ceil(x0));
    pRect->top = static_cast<LONG>(ceil(y0));
    pRect->right = static_cast<LONG>(floor(x1));
    pRect->bottom = static_cast<LONG>(floor(y1));
    return;
  }
}
