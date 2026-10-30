// port_install_window.cpp -- the first start's window as Hyperion draws it: on Linux
// and a Mac, which have no window of the system's that Hyperion can count on for it, and on
// Windows with HYPERION_INSTALL_WINDOW=drawn, to try it there. The same as Windows' own
// (hyperion.rc's IDD_PORT_INSTALL, port_install.cpp): a part for each kind of install with the
// folder it wants, a field with Browse, the line that says whether the game's files are there, in
// dark green or dark red (lighter on a dark theme), then which is the base, and Start only when
// the base's files are there.
//
// A window of its own before the game's, drawn with port_drawn.cpp. The layout is in units of a
// 640-wide window, scaled by the screen's.
#include <windows.h>
#include <math.h>
#include <string>
#include <SDL3/SDL.h>
#include "port_install.h"
#include "../front/port_text.h"
#include "../platform/port_drawn.h"
#include "../tools/port_startup.h"  // PortStartupStep

namespace {

const float kWidth = 640.0f;
const float kMargin = 20.0f;

// The two parts, StarCraft from Battle.net's and the classic one's.
enum { PART_BATTLENET, PART_CLASSIC, PART_COUNT };

struct Part {
  std::string folder;
  PortInstallCheck check;
  SDL_FRect rField, rBrowse;  // where the last frame drew them, in units
};

struct Window {
  PortDrawn d;
  Part parts[PART_COUNT];
  int nFocus;            // the part whose field takes what is typed
  PortInstallKind base;  // the one the game reads
  // where the last frame drew what a click reaches, in units
  SDL_FRect rBattleNet, rClassic, rStart, rQuit;
  float fBottom;  // the last frame's height, in units
  bool bDone;
  bool bChosen;
};

PortInstallKind KindOf(int nPart) { return nPart == PART_CLASSIC ? PORT_INSTALL_CLASSIC : PORT_INSTALL_BATTLENET; }

// Browse's answer. SDL's dialog may answer on another thread, after the window is gone, so it
// lands here and not in the window.
SDL_Mutex *g_pPickLock;
bool g_bPicking;
bool g_bPickDone;
bool g_bPickFailed;
int g_nPickFor;  // the part Browse was pressed in
std::string g_picked;

void SDLCALL FolderPicked(void *, const char *const *ppList, int)
{
  SDL_LockMutex(g_pPickLock);
  if (ppList == NULL)
    g_bPickFailed = true;
  else if (ppList[0] != NULL)
    g_picked = ppList[0];
  g_bPickDone = true;
  SDL_UnlockMutex(g_pPickLock);
}

// One of the base's two choices: its round mark and its name, the whole of it taking a click,
// greyed when that install's files are not there. Returns its width.
float Choice(Window &w, float x, float y, bool bOn, bool bEnabled, const char *pszLabel, SDL_FRect *pRect)
{
  const PortDrawnTheme &t = *w.d.pTheme;
  PortDrawnDisc(w.d, x + 8.0f, y + 10.0f, 8.0f, bOn && bEnabled ? t.accent : t.line);
  PortDrawnDisc(w.d, x + 8.0f, y + 10.0f, 6.5f, t.field);
  if (bOn && bEnabled)
    PortDrawnDisc(w.d, x + 8.0f, y + 10.0f, 4.0f, t.accent);
  const float fText =
      PortDrawnText(w.d, PORT_DRAWN_TEXT, 16.0f, pszLabel, x + 24.0f, y, bEnabled ? t.text : t.disabled);
  *pRect = {x - 4.0f, y - 4.0f, 24.0f + fText + 8.0f, 28.0f};
  return 24.0f + fText;
}

const char *const kNotLinked = PT_("Not linked. Leave it empty if you do not have this one.");

std::string BattleNetHelp(void)
{
#if defined(_WIN32)
  return PortText("Choose the StarCraft folder itself, the one that holds StarCraft Launcher.exe and the x86_64 and Data "
                  "folders, for example C:\\Program Files (x86)\\StarCraft.");
#elif defined(__APPLE__)
  return PortText("Choose the StarCraft folder itself, the one that holds the Data folder, for example "
                  "/Applications/StarCraft.");
#else
  return PortText("Choose the StarCraft folder itself, the one that holds StarCraft Launcher.exe and the x86_64 and Data "
                  "folders, for example drive_c/Program Files (x86)/StarCraft in the Wine or Lutris prefix Battle.net is "
                  "installed in.");
#endif
}

std::string ClassicHelp(void)
{
#if defined(_WIN32)
  return PortText("Choose the folder that holds StarDat.mpq and BrooDat.mpq, where the classic game is installed, for "
                  "example C:\\Program Files (x86)\\StarCraft.");
#else
  return PortText("Choose the folder that holds StarDat.mpq and BrooDat.mpq, where the classic game is installed.");
#endif
}

void Recheck(Window &w)
{
  for (int i = 0; i < PART_COUNT; i++)
    w.parts[i].check = PortInstallCheckFolder(KindOf(i), w.parts[i].folder.c_str());
  w.base = PortInstallBaseFor(w.base, w.parts[PART_BATTLENET].check.bFound, w.parts[PART_CLASSIC].check.bFound);
}

const PortInstallCheck &BaseCheck(const Window &w)
{
  return w.parts[w.base == PORT_INSTALL_CLASSIC ? PART_CLASSIC : PART_BATTLENET].check;
}

// A part: its name, the folder it wants, its field and Browse, and whether its files are there.
// Returns its height.
float DrawPart(Window &w, int nPart, float y, const char *pszTitle, const std::string &help)
{
  PortDrawn &d = w.d;
  const PortDrawnTheme &t = *d.pTheme;
  Part &p = w.parts[nPart];
  const float fInner = kWidth - kMargin * 2.0f, y0 = y;
  PortDrawnText(d, PORT_DRAWN_BOLD, 17.0f, pszTitle, kMargin, y, t.text);
  y += 25.0f;
  y += PortDrawnWrapped(d, PORT_DRAWN_TEXT, 15.0f, help, kMargin, y, fInner, t.dim) + 6.0f;
  // the folder, its end shown when it is longer than the field, with a caret in the one typed in
  const float fBrowse = 110.0f, fHeight = 34.0f;
  p.rField = {kMargin, y, fInner - fBrowse - 10.0f, fHeight};
  p.rBrowse = {kMargin + fInner - fBrowse, y, fBrowse, fHeight};
  PortDrawnFill(d, p.rField, t.field);
  PortDrawnOutline(d, p.rField, w.nFocus == nPart ? t.accent : t.line, 1.0f);
  const float fTextX = p.rField.x + 8.0f, fTextY = y + (fHeight - 16.0f * 1.35f) / 2.0f;
  const float fRoom = p.rField.w - 18.0f;
  float fCaret = fTextX;
  if (p.folder.empty())
    PortDrawnText(d, PORT_DRAWN_TEXT, 16.0f, PortText("Type or paste a folder, or Browse"), fTextX, fTextY,
                  t.disabled);
  else
    fCaret += PortDrawnText(d, PORT_DRAWN_TEXT, 16.0f, p.folder, fTextX, fTextY, t.text, fRoom);
  if (w.nFocus == nPart && (SDL_GetTicks() / 530) % 2 == 0)
    PortDrawnFill(d, {fCaret + 1.0f, y + 7.0f, 1.0f, fHeight - 14.0f}, t.text);
  PortDrawnButton(d, p.rBrowse, PortText("Browse..."), !g_bPicking, false);
  y += fHeight + 10.0f;
  // whether the files are there, two lines kept for it
  const float fStatus = p.folder.empty()
                            ? PortDrawnWrapped(d, PORT_DRAWN_TEXT, 16.0f, PortText(kNotLinked), kMargin, y, fInner,
                                               t.dim)
                            : PortDrawnWrapped(d, PORT_DRAWN_BOLD, 16.0f, p.check.text, kMargin, y, fInner,
                                               p.check.bFound ? t.found : t.missing);
  y += SDL_max(fStatus, 16.0f * 1.4f * 2.0f);
  return y - y0;
}

void Draw(Window &w)
{
  PortDrawn &d = w.d;
  const PortDrawnTheme &t = *d.pTheme;
  SDL_SetRenderDrawColor(d.pRenderer, t.back.r, t.back.g, t.back.b, 255);
  SDL_RenderClear(d.pRenderer);
  const float fInner = kWidth - kMargin * 2.0f;
  float y = 20.0f;
  y += PortDrawnWrapped(d, PORT_DRAWN_TEXT, 17.0f,
                        PortText("Hyperion plays StarCraft from your own copy of its files. Link the ones you have: one is "
                                 "enough."),
                        kMargin, y, fInner, t.text) + 16.0f;
  y += DrawPart(w, PART_BATTLENET, y, PortText("StarCraft: Remastered, from Battle.net (free)"), BattleNetHelp()) +
       14.0f;
  y += DrawPart(w, PART_CLASSIC, y, PortText("Classic (1.16.1 and prior)"), ClassicHelp()) + 16.0f;
  // the base, of those whose files are there
  const float fLabel =
      PortDrawnText(d, PORT_DRAWN_TEXT, 16.0f, PortText("Use as the game's files:"), kMargin, y, t.text);
  const float x = kMargin + fLabel + 16.0f;
  const float fFirst = Choice(w, x, y, w.base == PORT_INSTALL_BATTLENET, w.parts[PART_BATTLENET].check.bFound,
                              "StarCraft: Remastered", &w.rBattleNet);
  Choice(w, x + fFirst + 24.0f, y, w.base == PORT_INSTALL_CLASSIC, w.parts[PART_CLASSIC].check.bFound,
         PortTextC("the install", "Classic"), &w.rClassic);
  y += 30.0f;
  y += PortDrawnWrapped(d, PORT_DRAWN_TEXT, 15.0f,
                        PortText("The other stays linked: switch to it later in Settings, under Hyperion."), kMargin,
                        y, fInner, t.dim) + 16.0f;
  const float fButton = 100.0f;
  w.rQuit = {kWidth - kMargin - fButton, y, fButton, 34.0f};
  w.rStart = {w.rQuit.x - 10.0f - fButton, y, fButton, 34.0f};
  PortDrawnButton(d, w.rStart, PortText("Start"), BaseCheck(w).bFound, true);
  PortDrawnButton(d, w.rQuit, PortText("Quit"), true, false);
  w.fBottom = y + 34.0f + 20.0f;
  SDL_RenderPresent(d.pRenderer);
}

void Start(Window &w)
{
  if (!BaseCheck(w).bFound)
    return;
  std::string why;
  if (PortInstallKeepBoth(w.base, w.parts[PART_BATTLENET].check, w.parts[PART_CLASSIC].check, &why)) {
    w.bChosen = w.bDone = true;
    return;
  }
  PortInstallCheck &base = w.parts[w.base == PORT_INSTALL_CLASSIC ? PART_CLASSIC : PART_BATTLENET].check;
  base.bFound = false;
  base.text = why;
}

void Browse(Window &w, int nPart)
{
  if (g_bPicking)
    return;
  SDL_LockMutex(g_pPickLock);
  g_bPicking = true;
  g_bPickDone = g_bPickFailed = false;
  g_nPickFor = nPart;
  g_picked.clear();
  SDL_UnlockMutex(g_pPickLock);
  w.nFocus = nPart;
  const std::string &folder = w.parts[nPart].folder;
  SDL_ShowOpenFolderDialog(FolderPicked, NULL, w.d.pWindow, folder.empty() ? NULL : folder.c_str(), false);
}

void Picked(Window &w)
{
  SDL_LockMutex(g_pPickLock);
  if (g_bPicking && g_bPickDone) {
    g_bPicking = false;
    Part &p = w.parts[g_nPickFor];
    if (g_bPickFailed) {
      p.check.bFound = false;
      p.check.text = PortText("Browse could not open a folder dialog here: type or paste the folder.");
    } else if (!g_picked.empty()) {
      p.folder = g_picked;
      Recheck(w);
    }
  }
  SDL_UnlockMutex(g_pPickLock);
}

void Handle(Window &w, SDL_Event &e)
{
  PortDrawnEvent(w.d, e);
  switch (e.type) {
  case SDL_EVENT_QUIT:
  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    w.bDone = true;
    break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN: {
    if (e.button.button != SDL_BUTTON_LEFT)
      break;
    const float x = e.button.x / w.d.fScale, y = e.button.y / w.d.fScale;
    if (PortDrawnInside(w.rBattleNet, x, y) || PortDrawnInside(w.rClassic, x, y)) {
      w.base = PortDrawnInside(w.rClassic, x, y) ? PORT_INSTALL_CLASSIC : PORT_INSTALL_BATTLENET;
      Recheck(w);
    } else if (PortDrawnInside(w.parts[PART_BATTLENET].rBrowse, x, y) ||
               PortDrawnInside(w.parts[PART_CLASSIC].rBrowse, x, y)) {
      Browse(w, PortDrawnInside(w.parts[PART_CLASSIC].rBrowse, x, y) ? PART_CLASSIC : PART_BATTLENET);
    } else if (PortDrawnInside(w.parts[PART_BATTLENET].rField, x, y) ||
               PortDrawnInside(w.parts[PART_CLASSIC].rField, x, y)) {
      w.nFocus = PortDrawnInside(w.parts[PART_CLASSIC].rField, x, y) ? PART_CLASSIC : PART_BATTLENET;
    } else if (PortDrawnInside(w.rStart, x, y)) {
      Start(w);
    } else if (PortDrawnInside(w.rQuit, x, y)) {
      w.bDone = true;
    }
    break;
  }
  case SDL_EVENT_TEXT_INPUT:
    w.parts[w.nFocus].folder += e.text.text;
    Recheck(w);
    break;
  case SDL_EVENT_KEY_DOWN: {
    std::string &folder = w.parts[w.nFocus].folder;
    if (e.key.key == SDLK_BACKSPACE && !folder.empty()) {
      // the last character, its UTF-8 bytes
      size_t n = folder.size() - 1;
      while (n > 0 && (static_cast<unsigned char>(folder[n]) & 0xC0) == 0x80)
        --n;
      folder.erase(n);
      Recheck(w);
    } else if (e.key.key == SDLK_V && (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0) {
      char *pszClip = SDL_GetClipboardText();
      if (pszClip != NULL) {
        for (const char *p = pszClip; *p; ++p)
          if (*p != '\r' && *p != '\n')
            folder += *p;
        SDL_free(pszClip);
        Recheck(w);
      }
    } else if (e.key.key == SDLK_TAB) {
      w.nFocus = (w.nFocus + 1) % PART_COUNT;
    } else if (e.key.key == SDLK_UP || e.key.key == SDLK_DOWN) {
      w.base = e.key.key == SDLK_UP ? PORT_INSTALL_BATTLENET : PORT_INSTALL_CLASSIC;
      Recheck(w);
    } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
      Start(w);
    } else if (e.key.key == SDLK_ESCAPE) {
      w.bDone = true;
    }
    break;
  }
  default:
    break;
  }
}

}  // namespace

// No window: said on the standard error, for a player who started Hyperion from a terminal.
static PortInstallAnswer NoWindow(const char *pszWhat)
{
  SDL_Log("Hyperion: the first start's window could not open (%s: %s)", pszWhat, SDL_GetError());
  PortStartupStep("first start", "the window could not open: %s: %s", pszWhat, SDL_GetError());
  return PORT_INSTALL_ANSWER_NO_WINDOW;
}

PortInstallAnswer PortInstallWindowRun(void)
{
  if (g_pPickLock == NULL)
    g_pPickLock = SDL_CreateMutex();
  Window w = {};
  // the base as kept, when it is asked again for a base whose files are gone
  w.base = PortInstallChosen() == PORT_INSTALL_CLASSIC ? PORT_INSTALL_CLASSIC : PORT_INSTALL_BATTLENET;
  const char *pszFailed = "";
  if (!PortDrawnOpen(w.d, PortText("Hyperion: StarCraft's files"), kWidth, &pszFailed))
    return NoWindow(pszFailed);
  for (int i = 0; i < PART_COUNT; i++)
    w.parts[i].folder = PortInstallStartFolder(KindOf(i));
  w.nFocus = w.parts[PART_BATTLENET].folder.empty() || !w.parts[PART_CLASSIC].folder.empty() ? PART_BATTLENET
                                                                                             : PART_CLASSIC;
  Recheck(w);
  // a first frame for the layout's height, then the window at that size
  Draw(w);
  PortDrawnShow(w.d, w.fBottom);
  SDL_StartTextInput(w.d.pWindow);
  while (!w.bDone) {
    SDL_Event e;
    if (SDL_WaitEventTimeout(&e, 100)) {
      do
        Handle(w, e);
      while (!w.bDone && SDL_PollEvent(&e));
    }
    Picked(w);
    if (!w.bDone)
      Draw(w);
  }
  SDL_StopTextInput(w.d.pWindow);
  PortDrawnClose(w.d);
  return w.bChosen ? PORT_INSTALL_ANSWER_CHOSEN : PORT_INSTALL_ANSWER_QUIT;
}
