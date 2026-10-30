// port_errorwin.cpp -- the window a start that failed or a crash shows (port_errorwin.h).
//
// On Windows it is the system's own dialog (hyperion.rc's IDD_PORT_ERROR), which needs no SDL, so
// a start that cannot load SDL3.dll shows it too. Elsewhere, and on Windows with
// HYPERION_ERROR_WINDOW=drawn, it is a window drawn with port_drawn.cpp. Both have the same
// parts: the title, what happened, a line on what to do, the text to share in a box that
// scrolls, Copy to clipboard, Open the folder and Close. The box shows the text as it is; Copy
// puts it on the clipboard as a code block (PortShareBlock).
#include <windows.h>
#if defined(_WIN32)
#include <shellapi.h>  // ShellExecuteA
#endif
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "port_errorwin.h"
#include "../platform/port_paths.h"  // PortExplorer
#include "port_errorwin_dlg.h"
#include "port_share.h"
#include "../front/port_text.h"
#include "../platform/port_drawn.h"
#include "../platform/port_json.h"
#include "../platform/port_posix.h"  // PortNativePath

namespace {

std::string ReadAll(const std::string &path)
{
  std::string s;
  if (FILE *f = fopen(path.c_str(), "rb")) {
    char buf[8192];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;)
      s.append(buf, n);
    fclose(f);
  }
  return s;
}

#if !defined(__EMSCRIPTEN__)

std::string Hint(void)
{
  std::string s = PortText("To ask for help, copy this and paste it in a GitHub issue or on Discord. It names no folder, "
                           "network address or name of yours.");
#if !defined(_WIN32) && !defined(__APPLE__)
  // what is copied stays only while the program that copied it runs, unless the desktop keeps it
  s += PortText(" Paste it before you close this window.");
#endif
  return s;
}

// Explorer, or the system's file manager, open at the file, chosen in its folder.
void OpenAt(const char *pszFile)
{
#if defined(_WIN32)
  const std::string args = std::string("/select,\"") + pszFile + "\"";
  ShellExecuteA(NULL, "open", PortExplorer(), args.c_str(), NULL, SW_SHOWNORMAL);
#else
  std::string folder = pszFile;
  const size_t at = folder.find_last_of("\\/");
  if (at != std::string::npos)
    folder.erase(at + 1);
  ShellExecuteA(NULL, "open", folder.c_str(), NULL, NULL, SW_SHOWNORMAL);
#endif
}

// ---------------------------------------------------------------------------
// The window Hyperion draws.

const float kWidth = 680.0f;
const float kMargin = 20.0f;
const float kMono = 13.0f;           // the box's text, in units
const float kMonoLine = 13.0f * 1.4f;
const int kBoxLines = 18;            // the lines the box shows at once

struct Drawn {
  PortDrawn d;
  std::string title, what, hint, copy;
  const char *pszOpen;
  std::vector<std::string> lines;  // the shared text, each line cut to the box's width
  int nTop;                        // the first line the box shows
  SDL_FRect rBox, rCopy, rOpen, rClose;
  Uint64 qwCopied;  // when Copy was pressed, so it says so for a moment
  bool bCopyFailed;
  float fBottom;
  bool bDone;
};

// The text's lines cut to nColumns characters, a cut line's rest indented.
std::vector<std::string> Fit(const std::string &text, int nColumns)
{
  std::vector<std::string> out;
  size_t i = 0;
  while (i <= text.size()) {
    size_t j = text.find('\n', i);
    if (j == std::string::npos)
      j = text.size();
    std::string line = text.substr(i, j - i);
    bool bFirst = true;
    do {
      // nColumns characters, by their UTF-8 bytes
      const int nRoom = bFirst ? nColumns : nColumns - 2;
      size_t n = 0;
      for (int k = 0; n < line.size() && k < nRoom; k++)
        for (n++; n < line.size() && (static_cast<unsigned char>(line[n]) & 0xC0) == 0x80;)
          n++;
      out.push_back((bFirst ? "" : "  ") + line.substr(0, n));
      line.erase(0, n);
      bFirst = false;
    } while (!line.empty());
    i = j + 1;
  }
  return out;
}

void Scroll(Drawn &w, int nBy)
{
  const int nMost = SDL_max(0, static_cast<int>(w.lines.size()) - kBoxLines);
  w.nTop = SDL_clamp(w.nTop + nBy, 0, nMost);
}

void Copy(Drawn &w)
{
  w.bCopyFailed = !SDL_SetClipboardText(w.copy.c_str());
  w.qwCopied = SDL_GetTicks();
}

void Draw(Drawn &w)
{
  PortDrawn &d = w.d;
  const PortDrawnTheme &t = *d.pTheme;
  SDL_SetRenderDrawColor(d.pRenderer, t.back.r, t.back.g, t.back.b, 255);
  SDL_RenderClear(d.pRenderer);
  const float fInner = kWidth - kMargin * 2.0f;
  float y = 18.0f;
  PortDrawnText(d, PORT_DRAWN_BOLD, 20.0f, w.title, kMargin, y, t.text);
  y += 34.0f;
  y += PortDrawnWrapped(d, PORT_DRAWN_TEXT, 16.0f, w.what, kMargin, y, fInner, t.text) + 10.0f;
  y += PortDrawnWrapped(d, PORT_DRAWN_TEXT, 15.0f, w.hint, kMargin, y, fInner, t.dim) + 8.0f;
  // the box, with its lines from nTop, and where it is in them when they are more than it shows
  w.rBox = {kMargin, y, fInner, kBoxLines * kMonoLine + 16.0f};
  PortDrawnFill(d, w.rBox, t.field);
  PortDrawnOutline(d, w.rBox, t.line, 1.0f);
  const SDL_FRect rInner = {w.rBox.x + 1.0f, w.rBox.y + 1.0f, w.rBox.w - 2.0f, w.rBox.h - 2.0f};
  PortDrawnClip(d, &rInner);
  for (int i = 0; i < kBoxLines && w.nTop + i < static_cast<int>(w.lines.size()); i++)
    PortDrawnText(d, PORT_DRAWN_MONO, kMono, w.lines[static_cast<size_t>(w.nTop + i)], w.rBox.x + 8.0f,
                  w.rBox.y + 8.0f + i * kMonoLine, t.text);
  PortDrawnClip(d, NULL);
  if (static_cast<int>(w.lines.size()) > kBoxLines) {
    const float fTrack = w.rBox.h - 8.0f;
    const float fThumb = SDL_max(20.0f, fTrack * kBoxLines / static_cast<float>(w.lines.size()));
    const float fAt = (fTrack - fThumb) * w.nTop / static_cast<float>(w.lines.size() - kBoxLines);
    PortDrawnFill(d, {w.rBox.x + w.rBox.w - 8.0f, w.rBox.y + 4.0f + fAt, 4.0f, fThumb}, t.line);
  }
  y += w.rBox.h + 14.0f;
  const bool bCopied = w.qwCopied != 0 && SDL_GetTicks() - w.qwCopied < 2000;
  w.rCopy = {kMargin, y, 170.0f, 34.0f};
  w.rOpen = {kMargin + 180.0f, y, 170.0f, 34.0f};
  w.rClose = {kWidth - kMargin - 100.0f, y, 100.0f, 34.0f};
  PortDrawnButton(d, w.rCopy,
                  bCopied ? (w.bCopyFailed ? PortText("Could not copy") : PortText("Copied"))
                          : PortText("Copy to clipboard"),
                  true, true);
  PortDrawnButton(d, w.rOpen, PortText("Open the folder"), w.pszOpen != NULL, false);
  PortDrawnButton(d, w.rClose, PortText("Close"), true, false);
  w.fBottom = y + 34.0f + 20.0f;
  SDL_RenderPresent(d.pRenderer);
}

void Handle(Drawn &w, SDL_Event &e)
{
  PortDrawnEvent(w.d, e);
  switch (e.type) {
  case SDL_EVENT_QUIT:
  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    w.bDone = true;
    break;
  case SDL_EVENT_MOUSE_WHEEL:
    Scroll(w, e.wheel.y > 0 ? -3 : e.wheel.y < 0 ? 3 : 0);
    break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN: {
    if (e.button.button != SDL_BUTTON_LEFT)
      break;
    const float x = e.button.x / w.d.fScale, y = e.button.y / w.d.fScale;
    if (PortDrawnInside(w.rCopy, x, y))
      Copy(w);
    else if (PortDrawnInside(w.rOpen, x, y) && w.pszOpen != NULL)
      OpenAt(w.pszOpen);
    else if (PortDrawnInside(w.rClose, x, y))
      w.bDone = true;
    break;
  }
  case SDL_EVENT_KEY_DOWN:
    if ((e.key.key == SDLK_C && (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0) || e.key.key == SDLK_RETURN ||
        e.key.key == SDLK_KP_ENTER)
      Copy(w);
    else if (e.key.key == SDLK_ESCAPE)
      w.bDone = true;
    else if (e.key.key == SDLK_UP || e.key.key == SDLK_DOWN)
      Scroll(w, e.key.key == SDLK_UP ? -1 : 1);
    else if (e.key.key == SDLK_PAGEUP || e.key.key == SDLK_PAGEDOWN)
      Scroll(w, e.key.key == SDLK_PAGEUP ? -kBoxLines : kBoxLines);
    break;
  default:
    break;
  }
}

bool ShowDrawn(const char *pszTitle, const std::string &what, const std::string &share, const char *pszOpen)
{
  Drawn w = {};
  const char *pszFailed = "";
  if (!PortDrawnOpen(w.d, "Hyperion", kWidth, &pszFailed)) {
    SDL_Log("Hyperion: the error window could not open (%s: %s)", pszFailed, SDL_GetError());
    return false;
  }
  w.title = pszTitle;
  w.what = what;
  w.hint = Hint();
  w.copy = PortShareBlock(share);
  w.pszOpen = pszOpen;
  const float fChar = SDL_max(1.0f, PortDrawnTextWidth(w.d, PORT_DRAWN_MONO, kMono, "M"));
  w.lines = Fit(share, static_cast<int>((kWidth - kMargin * 2.0f - 24.0f) / fChar));
  Draw(w);
  PortDrawnShow(w.d, w.fBottom);
  while (!w.bDone) {
    SDL_Event e;
    if (SDL_WaitEventTimeout(&e, 100)) {
      do
        Handle(w, e);
      while (!w.bDone && SDL_PollEvent(&e));
    }
    if (!w.bDone)
      Draw(w);
  }
  PortDrawnClose(w.d);
  return true;
}

#if defined(_WIN32)

// ---------------------------------------------------------------------------
// Windows' own dialog.

// UTF-8 as Windows' text, or the code page's where it is not UTF-8.
std::wstring Wide(const std::string &s)
{
  UINT nPage = CP_UTF8;
  DWORD dwFlags = MB_ERR_INVALID_CHARS;
  int n = MultiByteToWideChar(nPage, dwFlags, s.data(), static_cast<int>(s.size()), NULL, 0);
  if (n == 0 && !s.empty()) {
    nPage = CP_ACP;
    dwFlags = 0;
    n = MultiByteToWideChar(nPage, dwFlags, s.data(), static_cast<int>(s.size()), NULL, 0);
  }
  std::wstring w(static_cast<size_t>(n), L'\0');
  if (n > 0)
    MultiByteToWideChar(nPage, dwFlags, s.data(), static_cast<int>(s.size()), &w[0], n);
  return w;
}

// Each line ending in \r\n, as an edit control and the clipboard want.
std::wstring Crlf(const std::wstring &s)
{
  std::wstring out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == L'\n' && (i == 0 || s[i - 1] != L'\r'))
      out += L'\r';
    out += s[i];
  }
  return out;
}

struct Dialog {
  std::wstring title, what, hint, share, copy;
  const char *pszOpen;
  HFONT hBold, hMono;
};

// What happened at the height its text needs, what is under it moved by the difference, and the
// dialog grown or shrunk about its middle.
void FitWhat(HWND hDlg)
{
  HWND hWhat = GetDlgItem(hDlg, IDC_ERROR_WHAT);
  RECT rc;
  GetWindowRect(hWhat, &rc);
  MapWindowPoints(NULL, hDlg, reinterpret_cast<POINT *>(&rc), 2);
  std::wstring text(static_cast<size_t>(GetWindowTextLengthW(hWhat)) + 1, L'\0');
  GetWindowTextW(hWhat, &text[0], static_cast<int>(text.size()));
  HDC hdc = GetDC(hWhat);
  HGDIOBJ hOld = SelectObject(hdc, reinterpret_cast<HGDIOBJ>(SendMessageW(hWhat, WM_GETFONT, 0, 0)));
  RECT calc = {0, 0, rc.right - rc.left, 0};
  DrawTextW(hdc, text.c_str(), -1, &calc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
  SelectObject(hdc, hOld);
  ReleaseDC(hWhat, hdc);
  const int nShift = (calc.bottom + 4) - (rc.bottom - rc.top);
  SetWindowPos(hWhat, NULL, 0, 0, rc.right - rc.left, calc.bottom + 4, SWP_NOMOVE | SWP_NOZORDER);
  const int anBelow[] = {IDC_ERROR_HINT, IDC_ERROR_TEXT, IDC_ERROR_COPY, IDC_ERROR_OPEN, IDCANCEL};
  for (int nId : anBelow) {
    HWND h = GetDlgItem(hDlg, nId);
    RECT r;
    GetWindowRect(h, &r);
    MapWindowPoints(NULL, hDlg, reinterpret_cast<POINT *>(&r), 2);
    SetWindowPos(h, NULL, r.left, r.top + nShift, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
  }
  RECT rw;
  GetWindowRect(hDlg, &rw);
  SetWindowPos(hDlg, NULL, rw.left, rw.top - nShift / 2, rw.right - rw.left, rw.bottom - rw.top + nShift, SWP_NOZORDER);
}

// HYPERION_DISPLAY=left or right, as a test run's window takes it (port_present.cpp): the dialog
// centred on the left- or right-most monitor.
struct TestMonitor {
  bool bLeft;
  RECT rc;
  bool bFound;
};

BOOL CALLBACK TestMonitorAt(HMONITOR, HDC, LPRECT pRect, LPARAM lParam)
{
  TestMonitor &t = *reinterpret_cast<TestMonitor *>(lParam);
  if (!t.bFound || (t.bLeft ? pRect->left < t.rc.left : pRect->left > t.rc.left)) {
    t.rc = *pRect;
    t.bFound = true;
  }
  return TRUE;
}

void MoveToTestDisplay(HWND hDlg)
{
  char szWhich[16];
  if (GetEnvironmentVariableA("HYPERION_DISPLAY", szWhich, sizeof szWhich) == 0 ||
      (lstrcmpiA(szWhich, "left") != 0 && lstrcmpiA(szWhich, "right") != 0))
    return;
  TestMonitor t = {lstrcmpiA(szWhich, "left") == 0, {}, false};
  EnumDisplayMonitors(NULL, NULL, TestMonitorAt, reinterpret_cast<LPARAM>(&t));
  RECT rw;
  if (!t.bFound || !GetWindowRect(hDlg, &rw))
    return;
  SetWindowPos(hDlg, NULL, (t.rc.left + t.rc.right - (rw.right - rw.left)) / 2,
               (t.rc.top + t.rc.bottom - (rw.bottom - rw.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

bool CopyText(HWND hDlg, const std::wstring &text)
{
  if (!OpenClipboard(hDlg))
    return false;
  EmptyClipboard();
  const size_t cb = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, cb);
  bool bDone = false;
  if (hMem != NULL) {
    memcpy(GlobalLock(hMem), text.c_str(), cb);
    GlobalUnlock(hMem);
    bDone = SetClipboardData(CF_UNICODETEXT, hMem) != NULL;
    if (!bDone)
      GlobalFree(hMem);
  }
  CloseClipboard();
  return bDone;
}

INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  Dialog *p = reinterpret_cast<Dialog *>(GetWindowLongPtrW(hDlg, DWLP_USER));
  switch (uMsg) {
  case WM_INITDIALOG: {
    p = reinterpret_cast<Dialog *>(lParam);
    SetWindowLongPtrW(hDlg, DWLP_USER, lParam);
    SetDlgItemTextW(hDlg, IDC_ERROR_TITLE, p->title.c_str());
    SetDlgItemTextW(hDlg, IDC_ERROR_WHAT, p->what.c_str());
    SetDlgItemTextW(hDlg, IDC_ERROR_HINT, p->hint.c_str());
    SetDlgItemTextW(hDlg, IDC_ERROR_TEXT, p->share.c_str());
    // the title larger and bold, the box in a typeface of even widths
    LOGFONTW lf;
    HFONT hFont = reinterpret_cast<HFONT>(SendMessageW(hDlg, WM_GETFONT, 0, 0));
    if (hFont != NULL && GetObjectW(hFont, sizeof lf, &lf) != 0) {
      LOGFONTW bold = lf, mono = lf;
      bold.lfWeight = FW_SEMIBOLD;
      bold.lfHeight = lf.lfHeight * 4 / 3;
      p->hBold = CreateFontIndirectW(&bold);
      SendDlgItemMessageW(hDlg, IDC_ERROR_TITLE, WM_SETFONT, reinterpret_cast<WPARAM>(p->hBold), TRUE);
      mono.lfWeight = FW_NORMAL;
      mono.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
      wcscpy_s(mono.lfFaceName, L"Consolas");
      p->hMono = CreateFontIndirectW(&mono);
      SendDlgItemMessageW(hDlg, IDC_ERROR_TEXT, WM_SETFONT, reinterpret_cast<WPARAM>(p->hMono), TRUE);
    }
    if (p->pszOpen == NULL)
      EnableWindow(GetDlgItem(hDlg, IDC_ERROR_OPEN), FALSE);
    FitWhat(hDlg);
    MoveToTestDisplay(hDlg);
    // the box from its first line
    SendDlgItemMessageW(hDlg, IDC_ERROR_TEXT, EM_SETSEL, 0, 0);
    SendDlgItemMessageW(hDlg, IDC_ERROR_TEXT, WM_VSCROLL, SB_TOP, 0);
    // in front of the game's window, which may still be there
    SetWindowPos(hDlg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(hDlg);
    SetFocus(GetDlgItem(hDlg, IDC_ERROR_COPY));
    return FALSE;  // the focus is set
  }
  case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case IDC_ERROR_COPY:
      SetDlgItemTextW(hDlg, IDC_ERROR_COPY,
                      Wide(CopyText(hDlg, p->copy) ? PortText("Copied") : PortText("Could not copy")).c_str());
      SetTimer(hDlg, 1, 2000, NULL);
      return TRUE;
    case IDC_ERROR_OPEN:
      if (p->pszOpen != NULL)
        OpenAt(p->pszOpen);
      return TRUE;
    case IDOK:
    case IDCANCEL:
      EndDialog(hDlg, 0);
      return TRUE;
    }
    break;
  case WM_TIMER:
    KillTimer(hDlg, 1);
    SetDlgItemTextW(hDlg, IDC_ERROR_COPY, Wide(PortText("Copy to clipboard")).c_str());
    return TRUE;
  case WM_DESTROY:
    if (p != NULL) {
      if (p->hBold != NULL)
        DeleteObject(p->hBold);
      if (p->hMono != NULL)
        DeleteObject(p->hMono);
      p->hBold = p->hMono = NULL;
    }
    break;
  }
  return FALSE;
}

bool ShowDialog(const char *pszTitle, const std::string &what, const std::string &share, const char *pszOpen)
{
  Dialog dialog = {Wide(pszTitle), Crlf(Wide(what)), Wide(Hint()), Crlf(Wide(share)),
                   Crlf(Wide(PortShareBlock(share))), pszOpen, NULL, NULL};
  // drawn at the screen's own scale, as the first start's dialog is
  typedef DPI_AWARENESS_CONTEXT(WINAPI * SetContextFn)(DPI_AWARENESS_CONTEXT);
  SetContextFn pfnSetContext =
      reinterpret_cast<SetContextFn>(GetProcAddress(GetModuleHandleA("user32.dll"), "SetThreadDpiAwarenessContext"));
  DPI_AWARENESS_CONTEXT previous = NULL;
  if (pfnSetContext != NULL)
    previous = pfnSetContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  const INT_PTR nResult = DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_PORT_ERROR), NULL, DialogProc,
                                          reinterpret_cast<LPARAM>(&dialog));
  if (pfnSetContext != NULL && previous != NULL)
    pfnSetContext(previous);
  return nResult != -1;
}

#endif  // _WIN32
#endif  // !__EMSCRIPTEN__

}  // namespace

#if !defined(_WIN32)
// The paths a window names, each on a line of its own, in the file system's form, not Z:\.
static std::string NativeLines(const std::string &s)
{
  std::string out;
  for (size_t p = 0; p < s.size();) {
    size_t q = s.find('\n', p);
    if (q == std::string::npos)
      q = s.size();
    const std::string line = s.substr(p, q - p);
    out += line.compare(0, 3, "Z:\\") == 0 ? std::string(PortNativePath(line.c_str())) : line;
    if (q < s.size())
      out += '\n';
    p = q + 1;
  }
  return out;
}
#endif

void PortErrorWindow(const char *pszTitle, const std::string &whatIn, const std::string &share, const char *pszOpen)
{
#if !defined(_WIN32)
  const std::string what = NativeLines(whatIn);
  fprintf(stderr, "%s\n%s\n\n%s\n", pszTitle, what.c_str(), PortShareBlock(share).c_str());
#else
  const std::string &what = whatIn;
#endif
#if !defined(__EMSCRIPTEN__)
#if defined(_WIN32)
  // HYPERION_ERROR_WINDOW=drawn: the window Linux and a Mac have, to try it here, when SDL is there
  char szWindow[16];
  const bool bDrawn = GetEnvironmentVariableA("HYPERION_ERROR_WINDOW", szWindow, sizeof szWindow) != 0 &&
                      lstrcmpiA(szWindow, "drawn") == 0 && LoadLibraryA("SDL3.dll") != NULL;
  if (bDrawn ? ShowDrawn(pszTitle, what, share, pszOpen) : ShowDialog(pszTitle, what, share, pszOpen))
    return;
  const std::wstring text = Crlf(Wide(what + "\n\n" + share));
  MessageBoxW(NULL, text.c_str(), Wide(pszTitle).c_str(), MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
#else
  if (ShowDrawn(pszTitle, what, share, pszOpen))
    return;
  const std::string text = what + "\n\n" + share;
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, pszTitle, text.c_str(), NULL);
#endif
#endif
}

int PortErrorCrashReport(const char *pszFolder)
{
  std::string folder = pszFolder != NULL ? pszFolder : "";
  if (!folder.empty() && folder.back() != '\\' && folder.back() != '/')
    folder += '\\';
  const std::string report = folder + "report.txt";
  PortJson json;
  std::string share, kind;
  if (PortJsonParse(ReadAll(folder + "report.json").c_str(), &json)) {
    share = PortShareCrash(json);
    kind = json["what"].Str();
  } else {
    // no JSON beside it: the text's first part, as cleaned
    const std::string text = ReadAll(report);
    const size_t at = text.find("thread:");
    share = "Hyperion stopped on an error\n" +
            PortShareClean(text.substr(0, at == std::string::npos ? 2000 : text.find("\r\n\r\n", at)), true);
    share = share.substr(0, PORT_SHARE_MAX - 8);
  }
  // {0} the error itself, as the report names it, which stays as it is
  std::string what = kind.empty() ? std::string() : PortTextF("The error: {0}.", {kind}) + "\n\n";
  // {0} the folder the report is in
  what += PortTextF("A report is saved in\n{0}", {folder});
  PortErrorWindow(PortText("Hyperion stopped on an error"), what, share, report.c_str());
  return 0;
}
