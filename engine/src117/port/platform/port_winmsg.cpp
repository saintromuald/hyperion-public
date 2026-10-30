// port_winmsg.cpp -- the game window's messages, made from SDL's events.
// port_winmsg.h says what this is for. Presentation only: input reaches the simulation as
// the commands the game makes of it, which replays record, and nothing here is read by the
// game's frame.
#if defined(_WIN32)
#include <windows.h>  // MSG, for SDL's message hook
#endif
#include "port_winmsg.h"
#include "../render/port_present.h"  // PORT_TEST_MOUSE
#include "../data/port_sound.h"  // PortSoundForeground
#include "port_standalone.h"  // PortModuleResource
#include <SDL3/SDL.h>
#include <string.h>
#include <deque>
#include <vector>
#if defined(_WIN32)
#define SDL_MAIN_HANDLED  // Hyperion has its own entry (hyperion/hyperion.cpp)
#include <SDL3/SDL_main.h>  // SDL_RegisterApp
#endif
#if defined(__EMSCRIPTEN__)
#include <emscripten/html5.h>  // emscripten_get_pointerlock_status
#endif

// The messages this layer makes and the game reads (Windows' values).
enum {
  WM_DESTROY_ = 0x0002, WM_PAINT_ = 0x000F, WM_CLOSE_ = 0x0010, WM_ACTIVATEAPP_ = 0x001C,
  WM_KEYDOWN_ = 0x0100, WM_KEYUP_ = 0x0101, WM_CHAR_ = 0x0102, WM_SYSKEYDOWN_ = 0x0104,
  WM_SYSKEYUP_ = 0x0105, WM_SYSCHAR_ = 0x0106, WM_COMMAND_ = 0x0111, WM_SYSCOMMAND_ = 0x0112,
  WM_TIMER_ = 0x0113, WM_MOUSEMOVE_ = 0x0200, WM_LBUTTONDOWN_ = 0x0201, WM_LBUTTONUP_ = 0x0202,
  WM_LBUTTONDBLCLK_ = 0x0203, WM_RBUTTONDOWN_ = 0x0204, WM_RBUTTONUP_ = 0x0205,
  WM_RBUTTONDBLCLK_ = 0x0206, WM_MBUTTONDOWN_ = 0x0207, WM_MBUTTONUP_ = 0x0208,
  WM_MBUTTONDBLCLK_ = 0x0209, WM_MOUSEWHEEL_ = 0x020A, WM_CAPTURECHANGED_ = 0x0215,
  WM_QUIT_ = 0x0012
};
enum { SC_CLOSE_ = 0xF060 };
enum { MK_LBUTTON_ = 1, MK_RBUTTON_ = 2, MK_SHIFT_ = 4, MK_CONTROL_ = 8, MK_MBUTTON_ = 0x10 };
enum {
  VK_LBUTTON_ = 0x01, VK_RBUTTON_ = 0x02, VK_MBUTTON_ = 0x04, VK_BACK_ = 0x08, VK_TAB_ = 0x09,
  VK_RETURN_ = 0x0D, VK_SHIFT_ = 0x10, VK_CONTROL_ = 0x11, VK_MENU_ = 0x12, VK_PAUSE_ = 0x13,
  VK_CAPITAL_ = 0x14, VK_ESCAPE_ = 0x1B, VK_SPACE_ = 0x20, VK_F10_ = 0x79, VK_NUMLOCK_ = 0x90,
  VK_SCROLL_ = 0x91, VK_LSHIFT_ = 0xA0, VK_RSHIFT_ = 0xA1, VK_LCONTROL_ = 0xA2,
  VK_RCONTROL_ = 0xA3, VK_LMENU_ = 0xA4, VK_RMENU_ = 0xA5
};

static SDL_Window *g_window;
static void *g_hWnd;          // the HWND on Windows, the SDL window elsewhere
static WinMsgProc g_pfnProc;
static std::deque<WinMsg> g_queue;  // made from SDL's events and posted, in order

// A key's state, as GetKeyState gives it: bit 7 down, bit 0 toggled.
static BYTE g_keys[256];
static void *g_capture;
static int g_nCursorShown;  // ShowCursor's count: shown while it is 0 or more
static Uint64 g_nUtf8Until;  // typing comes as UTF-8 until then (WinMsgTypeUtf8)

struct Timer {
  void *hWnd;
  UINT_PTR id;
  unsigned int nMs;
  Uint64 nDue;
  WinMsgTimerProc pfn;
};
static std::vector<Timer> g_timers;
static UINT_PTR g_nNextTimerId = 0x7F00;  // ids SetTimer(NULL, 0, ...) hands out

DWORD WinMsgTicks(void)
{
  return static_cast<DWORD>(SDL_GetTicks());
}

// ---------------------------------------------------------------------------
// Keys: an SDL scancode's virtual-key code and Windows' scan code (set 1), with the
// extended flag, as a Windows US keyboard gives them. Letters take their code from the
// layout (SDL's keycode), as Windows' do.

struct KeyInfo {
  BYTE vk;
  BYTE scan;
  bool bExtended;
};

static KeyInfo KeyOf(SDL_Scancode sc, SDL_Keycode key, SDL_Keymod mod)
{
  KeyInfo k = {0, 0, false};
  if (key >= 'a' && key <= 'z')
    k.vk = static_cast<BYTE>(key - 'a' + 'A');
  static const BYTE kLetterScan[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                       0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
  if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
    k.scan = kLetterScan[sc - SDL_SCANCODE_A];
    if (!k.vk)
      k.vk = static_cast<BYTE>('A' + (sc - SDL_SCANCODE_A));
    return k;
  }
  if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
    k.vk = static_cast<BYTE>('1' + (sc - SDL_SCANCODE_1));
    k.scan = static_cast<BYTE>(0x02 + (sc - SDL_SCANCODE_1));
    return k;
  }
  if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) {
    k.vk = static_cast<BYTE>(0x70 + (sc - SDL_SCANCODE_F1));
    k.scan = sc <= SDL_SCANCODE_F10 ? static_cast<BYTE>(0x3B + (sc - SDL_SCANCODE_F1))
                                    : static_cast<BYTE>(sc == SDL_SCANCODE_F11 ? 0x57 : 0x58);
    return k;
  }
  // The keypad's digits and point are numbers with Num Lock on, else the keys they carry.
  const bool bNum = (mod & SDL_KMOD_NUM) != 0;
  struct Row { SDL_Scancode sc; BYTE vk; BYTE scan; bool bExt; BYTE vkNoNum; };
  static const Row kRows[] = {
    {SDL_SCANCODE_0, '0', 0x0B, false, 0}, {SDL_SCANCODE_RETURN, VK_RETURN_, 0x1C, false, 0},
    {SDL_SCANCODE_ESCAPE, VK_ESCAPE_, 0x01, false, 0}, {SDL_SCANCODE_BACKSPACE, VK_BACK_, 0x0E, false, 0},
    {SDL_SCANCODE_TAB, VK_TAB_, 0x0F, false, 0}, {SDL_SCANCODE_SPACE, VK_SPACE_, 0x39, false, 0},
    {SDL_SCANCODE_MINUS, 0xBD, 0x0C, false, 0}, {SDL_SCANCODE_EQUALS, 0xBB, 0x0D, false, 0},
    {SDL_SCANCODE_LEFTBRACKET, 0xDB, 0x1A, false, 0}, {SDL_SCANCODE_RIGHTBRACKET, 0xDD, 0x1B, false, 0},
    {SDL_SCANCODE_BACKSLASH, 0xDC, 0x2B, false, 0}, {SDL_SCANCODE_NONUSBACKSLASH, 0xE2, 0x56, false, 0},
    {SDL_SCANCODE_SEMICOLON, 0xBA, 0x27, false, 0}, {SDL_SCANCODE_APOSTROPHE, 0xDE, 0x28, false, 0},
    {SDL_SCANCODE_GRAVE, 0xC0, 0x29, false, 0}, {SDL_SCANCODE_COMMA, 0xBC, 0x33, false, 0},
    {SDL_SCANCODE_PERIOD, 0xBE, 0x34, false, 0}, {SDL_SCANCODE_SLASH, 0xBF, 0x35, false, 0},
    {SDL_SCANCODE_CAPSLOCK, VK_CAPITAL_, 0x3A, false, 0},
    {SDL_SCANCODE_PRINTSCREEN, 0x2C, 0x37, true, 0}, {SDL_SCANCODE_SCROLLLOCK, VK_SCROLL_, 0x46, false, 0},
    {SDL_SCANCODE_PAUSE, VK_PAUSE_, 0x45, false, 0}, {SDL_SCANCODE_INSERT, 0x2D, 0x52, true, 0},
    {SDL_SCANCODE_HOME, 0x24, 0x47, true, 0}, {SDL_SCANCODE_PAGEUP, 0x21, 0x49, true, 0},
    {SDL_SCANCODE_DELETE, 0x2E, 0x53, true, 0}, {SDL_SCANCODE_END, 0x23, 0x4F, true, 0},
    {SDL_SCANCODE_PAGEDOWN, 0x22, 0x51, true, 0}, {SDL_SCANCODE_RIGHT, 0x27, 0x4D, true, 0},
    {SDL_SCANCODE_LEFT, 0x25, 0x4B, true, 0}, {SDL_SCANCODE_DOWN, 0x28, 0x50, true, 0},
    {SDL_SCANCODE_UP, 0x26, 0x48, true, 0}, {SDL_SCANCODE_NUMLOCKCLEAR, VK_NUMLOCK_, 0x45, true, 0},
    {SDL_SCANCODE_KP_DIVIDE, 0x6F, 0x35, true, 0}, {SDL_SCANCODE_KP_MULTIPLY, 0x6A, 0x37, false, 0},
    {SDL_SCANCODE_KP_MINUS, 0x6D, 0x4A, false, 0}, {SDL_SCANCODE_KP_PLUS, 0x6B, 0x4E, false, 0},
    {SDL_SCANCODE_KP_ENTER, VK_RETURN_, 0x1C, true, 0},
    {SDL_SCANCODE_KP_1, 0x61, 0x4F, false, 0x23}, {SDL_SCANCODE_KP_2, 0x62, 0x50, false, 0x28},
    {SDL_SCANCODE_KP_3, 0x63, 0x51, false, 0x22}, {SDL_SCANCODE_KP_4, 0x64, 0x4B, false, 0x25},
    {SDL_SCANCODE_KP_5, 0x65, 0x4C, false, 0x0C}, {SDL_SCANCODE_KP_6, 0x66, 0x4D, false, 0x27},
    {SDL_SCANCODE_KP_7, 0x67, 0x47, false, 0x24}, {SDL_SCANCODE_KP_8, 0x68, 0x48, false, 0x26},
    {SDL_SCANCODE_KP_9, 0x69, 0x49, false, 0x21}, {SDL_SCANCODE_KP_0, 0x60, 0x52, false, 0x2D},
    {SDL_SCANCODE_KP_PERIOD, 0x6E, 0x53, false, 0x2E}, {SDL_SCANCODE_APPLICATION, 0x5D, 0x5D, true, 0},
    {SDL_SCANCODE_LCTRL, VK_CONTROL_, 0x1D, false, 0}, {SDL_SCANCODE_LSHIFT, VK_SHIFT_, 0x2A, false, 0},
    {SDL_SCANCODE_LALT, VK_MENU_, 0x38, false, 0}, {SDL_SCANCODE_LGUI, 0x5B, 0x5B, true, 0},
    {SDL_SCANCODE_RCTRL, VK_CONTROL_, 0x1D, true, 0}, {SDL_SCANCODE_RSHIFT, VK_SHIFT_, 0x36, false, 0},
    {SDL_SCANCODE_RALT, VK_MENU_, 0x38, true, 0}, {SDL_SCANCODE_RGUI, 0x5C, 0x5C, true, 0},
    {SDL_SCANCODE_F13, 0x7C, 0x64, false, 0}, {SDL_SCANCODE_F14, 0x7D, 0x65, false, 0},
    {SDL_SCANCODE_F15, 0x7E, 0x66, false, 0},
  };
  for (const Row &r : kRows) {
    if (r.sc == sc) {
      k.vk = (!bNum && r.vkNoNum) ? r.vkNoNum : r.vk;
      k.scan = r.scan;
      k.bExtended = r.bExt;
      return k;
    }
  }
  return k;
}

// The left or right key's own code, which GetKeyState also answers for.
static int SideVk(SDL_Scancode sc)
{
  switch (sc) {
  case SDL_SCANCODE_LSHIFT: return VK_LSHIFT_;
  case SDL_SCANCODE_RSHIFT: return VK_RSHIFT_;
  case SDL_SCANCODE_LCTRL: return VK_LCONTROL_;
  case SDL_SCANCODE_RCTRL: return VK_RCONTROL_;
  case SDL_SCANCODE_LALT: return VK_LMENU_;
  case SDL_SCANCODE_RALT: return VK_RMENU_;
  default: return 0;
  }
}

static bool Down(int vk)
{
  return (g_keys[vk & 0xFF] & 0x80) != 0;
}

static void Push(unsigned int uMsg, UINT_PTR wParam, LONG_PTR lParam)
{
  WinMsg m;
  memset(&m, 0, sizeof m);
  m.hWnd = g_hWnd;
  m.uMsg = uMsg;
  m.wParam = wParam;
  m.lParam = lParam;
  m.dwTime = WinMsgTicks();
  g_queue.push_back(m);
}

static void KeyEvent(const SDL_KeyboardEvent &e)
{
  const KeyInfo k = KeyOf(e.scancode, e.key, e.mod);
  if (k.vk == 0)
    return;
  const bool bWasDown = Down(k.vk);
  if (e.down) {
    if (!bWasDown)
      g_keys[k.vk] ^= 1;  // toggled on each press, as Windows keeps it
    g_keys[k.vk] |= 0x80;
    if (const int s = SideVk(e.scancode)) {
      if (!Down(s))
        g_keys[s] ^= 1;
      g_keys[s] |= 0x80;
    }
  } else {
    g_keys[k.vk] &= ~0x80;
    if (const int s = SideVk(e.scancode))
      g_keys[s] &= ~0x80;
    // the generic key is up only when both sides are
    if (k.vk == VK_SHIFT_ && (Down(VK_LSHIFT_) || Down(VK_RSHIFT_)))
      g_keys[VK_SHIFT_] |= 0x80;
    if (k.vk == VK_CONTROL_ && (Down(VK_LCONTROL_) || Down(VK_RCONTROL_)))
      g_keys[VK_CONTROL_] |= 0x80;
    if (k.vk == VK_MENU_ && (Down(VK_LMENU_) || Down(VK_RMENU_)))
      g_keys[VK_MENU_] |= 0x80;
  }
  const bool bAlt = Down(VK_MENU_) && !Down(VK_CONTROL_);
  // Alt with a key, Alt itself and F10 are system keys, as Windows makes them.
  const bool bSys = bAlt || k.vk == VK_F10_ || (k.vk == VK_MENU_ && !Down(VK_CONTROL_));
  DWORD lParam = 1u | (static_cast<DWORD>(k.scan) << 16);
  if (k.bExtended)
    lParam |= 1u << 24;
  if (bAlt || (k.vk == VK_MENU_ && e.down))
    lParam |= 1u << 29;
  if (e.down) {
    if (bWasDown || e.repeat)
      lParam |= 1u << 30;
    Push(bSys ? WM_SYSKEYDOWN_ : WM_KEYDOWN_, k.vk, static_cast<LONG_PTR>(lParam));
  } else {
    lParam |= (1u << 30) | (1u << 31);
    Push(bSys ? WM_SYSKEYUP_ : WM_KEYUP_, k.vk, static_cast<LONG_PTR>(lParam));
  }
}

// UTF-8 to Windows-1252, the code page 1.17's text is in; a character it does not have is
// left out.
static int Cp1252(Uint32 c)
{
  if (c < 0x80 || (c >= 0xA0 && c <= 0xFF))
    return static_cast<int>(c);
  static const Uint16 kHigh[32] = {0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030,
                                   0x0160, 0x2039, 0x0152, 0, 0x017D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D,
                                   0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E,
                                   0x0178};
  for (int i = 0; i < 32; i++)
    if (kHigh[i] && kHigh[i] == c)
      return 0x80 + i;
  return -1;
}

// A typed character as WM_CHAR, a byte each: the byte Windows-1252 has for it, as 1.17 had it,
// else (Cyrillic, Greek, Polish's letters, the East Asian scripts', an input method's) its UTF-8
// bytes, which the edit boxes keep together and the game's text drawing reads.
static void PushChar(Uint32 c)
{
  const int b = c >= 0x80 && SDL_GetTicks() < g_nUtf8Until ? -1 : Cp1252(c);
  if (b >= 0x20) {
    Push(WM_CHAR_, static_cast<UINT_PTR>(b), 1);
    return;
  }
  if (b >= 0 || (c >= 0xD800 && c < 0xE000) || c > 0x10FFFF)
    return;
  char sz[8];
  const char *pEnd = SDL_UCS4ToUTF8(c, sz);
  for (const char *p = sz; p < pEnd; ++p)
    Push(WM_CHAR_, static_cast<UINT_PTR>(static_cast<unsigned char>(*p)), 1);
}

void WinMsgTypeUtf8(void)
{
  g_nUtf8Until = SDL_GetTicks() + 250;
}

static void TextEvent(const char *pszText)
{
  // Alt with a key makes WM_SYSCHAR from the key (WinMsgTranslate), not text, on Windows.
  if (Down(VK_MENU_) && !Down(VK_CONTROL_))
    return;
  const char *p = pszText;
  for (;;) {
    const Uint32 c = SDL_StepUTF8(&p, NULL);
    if (c == 0)
      break;
    if (c != SDL_INVALID_UNICODE_CODEPOINT)
      PushChar(c);
  }
}

// A mouse position in the window's client pixels, as a mouse message's lParam.
static LONG_PTR MousePos(float x, float y)
{
  const float fScale = g_window ? SDL_GetWindowPixelDensity(g_window) : 1.0f;
  const int nX = static_cast<int>(x * fScale);
  const int nY = static_cast<int>(y * fScale);
  return static_cast<LONG_PTR>(static_cast<DWORD>(static_cast<WORD>(nX)) |
                               (static_cast<DWORD>(static_cast<WORD>(nY)) << 16));
}

static UINT_PTR MouseFlags(void)
{
  UINT_PTR w = 0;
  if (Down(VK_LBUTTON_))
    w |= MK_LBUTTON_;
  if (Down(VK_RBUTTON_))
    w |= MK_RBUTTON_;
  if (Down(VK_MBUTTON_))
    w |= MK_MBUTTON_;
  if (Down(VK_SHIFT_))
    w |= MK_SHIFT_;
  if (Down(VK_CONTROL_))
    w |= MK_CONTROL_;
  return w;
}

void WinMsgRemoteKey(int vk, bool bDown, unsigned int uChar)
{
  vk &= 0xFF;
  if (vk == 0)
    return;
  const bool bWasDown = Down(vk);
  if (bDown) {
    if (!bWasDown)
      g_keys[vk] ^= 1;
    g_keys[vk] |= 0x80;
  } else {
    g_keys[vk] &= ~0x80;
  }
  const bool bAlt = Down(VK_MENU_) && !Down(VK_CONTROL_);
  const bool bSys = bAlt || vk == VK_F10_ || (vk == VK_MENU_ && !Down(VK_CONTROL_));
  DWORD lParam = 1u;
  if (bAlt || (vk == VK_MENU_ && bDown))
    lParam |= 1u << 29;
  if (bDown) {
    if (bWasDown)
      lParam |= 1u << 30;
    Push(bSys ? WM_SYSKEYDOWN_ : WM_KEYDOWN_, static_cast<UINT_PTR>(vk), static_cast<LONG_PTR>(lParam));
    // the character, as SDL's text gives it after the key (TextEvent)
    if (uChar >= 0x20 && uChar != 0x7F && !bAlt && !(Down(VK_CONTROL_) && !Down(VK_MENU_)))
      PushChar(uChar);
  } else {
    lParam |= (1u << 30) | (1u << 31);
    Push(bSys ? WM_SYSKEYUP_ : WM_KEYUP_, static_cast<UINT_PTR>(vk), static_cast<LONG_PTR>(lParam));
  }
}

void WinMsgRemoteMouse(unsigned int uMsg, int nX, int nY)
{
  const int vk = uMsg == WM_LBUTTONDOWN_ || uMsg == WM_LBUTTONUP_ || uMsg == WM_LBUTTONDOWN_ + 2 ? VK_LBUTTON_
                 : uMsg == WM_RBUTTONDOWN_ || uMsg == WM_RBUTTONUP_ || uMsg == WM_RBUTTONDOWN_ + 2 ? VK_RBUTTON_
                 : uMsg == WM_MBUTTONDOWN_ || uMsg == WM_MBUTTONUP_ || uMsg == WM_MBUTTONDOWN_ + 2 ? VK_MBUTTON_
                                                                                                  : 0;
  if (vk != 0) {
    if (uMsg == WM_LBUTTONUP_ || uMsg == WM_RBUTTONUP_ || uMsg == WM_MBUTTONUP_)
      g_keys[vk] &= ~0x80;
    else
      g_keys[vk] |= 0x80;
  }
  Push(uMsg, MouseFlags() | PORT_TEST_MOUSE,
       static_cast<LONG_PTR>(static_cast<DWORD>(static_cast<WORD>(nX)) | (static_cast<DWORD>(static_cast<WORD>(nY)) << 16)));
}

static float g_fMouseX, g_fMouseY;
static float g_fWheel;  // the wheel's part of a notch not yet sent

#if defined(__EMSCRIPTEN__)
void WinMsgWebHeld(float *pX, float *pY)
{
  EmscriptenPointerlockChangeEvent lock;
  if (g_window == NULL || emscripten_get_pointerlock_status(&lock) != EMSCRIPTEN_RESULT_SUCCESS || !lock.isActive)
    return;
  int nW = 0, nH = 0;
  SDL_GetWindowSize(g_window, &nW, &nH);
  const float fX = SDL_clamp(*pX, 0.0f, static_cast<float>(nW - 1));
  const float fY = SDL_clamp(*pY, 0.0f, static_cast<float>(nH - 1));
  if (fX == *pX && fY == *pY)
    return;
  *pX = fX;
  *pY = fY;
  SDL_WarpMouseInWindow(g_window, fX, fY);
}
#endif

static void Translate(const SDL_Event &e)
{
  switch (e.type) {
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP:
    KeyEvent(e.key);
    break;
  case SDL_EVENT_TEXT_INPUT:
    TextEvent(e.text.text);
    break;
  case SDL_EVENT_MOUSE_MOTION: {
    g_fMouseX = e.motion.x;
    g_fMouseY = e.motion.y;
#if defined(__EMSCRIPTEN__)
    WinMsgWebHeld(&g_fMouseX, &g_fMouseY);
#endif
    int nX, nY;
    if (PortMouseScale(&nX, &nY)) {
      // Mouse sensitivity put the pointer elsewhere; the half pixel keeps MousePos on it
      const float fScale = g_window ? SDL_GetWindowPixelDensity(g_window) : 1.0f;
      g_fMouseX = (nX + 0.5f) / fScale;
      g_fMouseY = (nY + 0.5f) / fScale;
    }
    Push(WM_MOUSEMOVE_, MouseFlags(), MousePos(g_fMouseX, g_fMouseY));
    break;
  }
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP: {
    int vk, nDown;
    if (e.button.button == SDL_BUTTON_LEFT) {
      vk = VK_LBUTTON_;
      nDown = WM_LBUTTONDOWN_;
    } else if (e.button.button == SDL_BUTTON_RIGHT) {
      vk = VK_RBUTTON_;
      nDown = WM_RBUTTONDOWN_;
    } else if (e.button.button == SDL_BUTTON_MIDDLE) {
      vk = VK_MBUTTON_;
      nDown = WM_MBUTTONDOWN_;
    } else {
      break;
    }
    g_fMouseX = e.button.x;
    g_fMouseY = e.button.y;
#if defined(__EMSCRIPTEN__)
    WinMsgWebHeld(&g_fMouseX, &g_fMouseY);
#endif
    if (e.button.down)
      g_keys[vk] |= 0x80;
    else
      g_keys[vk] &= ~0x80;
    // the window class asked for double clicks (CS_DBLCLKS); the second click of a pair
    // is the double click, as Windows sends it
    const unsigned int uMsg = !e.button.down ? nDown + 1
                              : (e.button.clicks >= 2 && e.button.clicks % 2 == 0) ? nDown + 2 : nDown;
    Push(uMsg, MouseFlags(), MousePos(g_fMouseX, g_fMouseY));
    break;
  }
  case SDL_EVENT_MOUSE_WHEEL: {
    // WM_MOUSEWHEEL counts 120 a notch and gives the screen's position
    float y = e.wheel.y;
    if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
      y = -y;
    g_fWheel += y * 120.0f;
    const int nDelta = static_cast<int>(g_fWheel);
    if (nDelta == 0)
      break;
    g_fWheel -= static_cast<float>(nDelta);
    int nWinX = 0, nWinY = 0;
    if (g_window)
      SDL_GetWindowPosition(g_window, &nWinX, &nWinY);
    const LONG_PTR lClient = MousePos(e.wheel.mouse_x, e.wheel.mouse_y);
    const int nX = static_cast<short>(lClient & 0xFFFF) + nWinX;
    const int nY = static_cast<short>((lClient >> 16) & 0xFFFF) + nWinY;
    Push(WM_MOUSEWHEEL_, (static_cast<UINT_PTR>(static_cast<WORD>(nDelta)) << 16) | MouseFlags(),
         static_cast<LONG_PTR>(static_cast<DWORD>(static_cast<WORD>(nX)) |
                               (static_cast<DWORD>(static_cast<WORD>(nY)) << 16)));
    break;
  }
  case SDL_EVENT_WINDOW_FOCUS_GAINED:
    PortSoundForeground(true);
    Push(WM_ACTIVATEAPP_, 1, 0);
    break;
  case SDL_EVENT_WINDOW_FOCUS_LOST:
    // keys held when the focus went are up for the game, as Windows has them
    for (int vk = 0; vk < 256; vk++)
      g_keys[vk] &= ~0x80;
    if (g_capture) {
      g_capture = NULL;
      SDL_CaptureMouse(false);
      Push(WM_CAPTURECHANGED_, 0, 0);
    }
    PortSoundForeground(false);
    Push(WM_ACTIVATEAPP_, 0, 0);
    break;
  case SDL_EVENT_WINDOW_EXPOSED:
    Push(WM_PAINT_, 0, 0);
    break;
  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
  case SDL_EVENT_QUIT:
    // the window's close box and Alt+F4, which Windows sends as SC_CLOSE
    Push(WM_SYSCOMMAND_, SC_CLOSE_, 0);
    break;
  default:
    break;
  }
}

// Windows' own messages to the window that SDL does not turn into events: a WM_COMMAND
// or a message of WM_APP's range a tool posts, and a test harness's mouse (its keys SDL
// does turn into events). SDL's own messages are of WM_USER's range.
#if defined(_WIN32)
static bool SDLCALL WindowsHook(void *, MSG *pMsg)
{
  // a test's mouse, which carries PORT_TEST_MOUSE in wParam for PortMouseIgnored and
  // would lose it in SDL's events, comes through as it was posted
  const bool bTestMouse = pMsg->message >= WM_MOUSEMOVE_ && pMsg->message <= WM_MOUSEWHEEL_ &&
                          (pMsg->wParam & PORT_TEST_MOUSE) != 0;
  // a key or character reaching the window while it lacks the keyboard is a tool's, as a test's
  // background window cannot take the focus and SDL keeps text for the focused one: as posted
  const unsigned int u = pMsg->message;
  const bool bKey = u == WM_KEYDOWN_ || u == WM_KEYUP_ || u == WM_SYSKEYDOWN_ || u == WM_SYSKEYUP_;
  const bool bToolKey = (bKey || u == WM_CHAR_ || u == WM_SYSCHAR_) && SDL_GetKeyboardFocus() != g_window;
  if (pMsg->hwnd == g_hWnd && (bTestMouse || bToolKey || u == WM_COMMAND_ || (u >= 0x8000 && u < 0xC000))) {
    if (bKey && pMsg->wParam < 256) {
      if (u == WM_KEYDOWN_ || u == WM_SYSKEYDOWN_)
        g_keys[pMsg->wParam] |= 0x80;
      else
        g_keys[pMsg->wParam] &= ~0x80;
    }
    // a character past ASCII, a UTF-16 unit as Windows posts it, as typed text has it
    if (u == WM_CHAR_ && pMsg->wParam >= 0x80)
      PushChar(static_cast<Uint32>(pMsg->wParam));
    else
      Push(u, pMsg->wParam, pMsg->lParam);
    return false;
  }
  return true;
}
#endif

static void Pump(void)
{
  SDL_PumpEvents();
  SDL_Event e;
  while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST) == 1)
    Translate(e);
}

// The first timer that is due becomes WM_TIMER, and comes due again a period later.
static bool TimerDue(WinMsg *pMsg)
{
  const Uint64 nNow = SDL_GetTicks();
  for (Timer &t : g_timers) {
    if (t.nDue <= nNow) {
      t.nDue = nNow + t.nMs;
      memset(pMsg, 0, sizeof *pMsg);
      pMsg->hWnd = t.hWnd;
      pMsg->uMsg = WM_TIMER_;
      pMsg->wParam = t.id;
      pMsg->lParam = reinterpret_cast<LONG_PTR>(t.pfn);
      pMsg->dwTime = static_cast<DWORD>(nNow);
      return true;
    }
  }
  return false;
}

static void Fill(WinMsg *pMsg, const WinMsg &m)
{
  *pMsg = m;
  float x = g_fMouseX, y = g_fMouseY;
  const LONG_PTR l = MousePos(x, y);
  pMsg->nPtX = static_cast<short>(l & 0xFFFF);
  pMsg->nPtY = static_cast<short>((l >> 16) & 0xFFFF);
}

BOOL WinMsgPeek(WinMsg *pMsg)
{
  if (g_queue.empty())
    Pump();
  if (!g_queue.empty()) {
    Fill(pMsg, g_queue.front());
    g_queue.pop_front();
    return TRUE;
  }
  return TimerDue(pMsg) ? TRUE : FALSE;
}

BOOL WinMsgGet(WinMsg *pMsg)
{
  for (;;) {
    if (WinMsgPeek(pMsg))
      return pMsg->uMsg != WM_QUIT_;
    // until an event, or the next timer
    Sint32 nWait = 100;
    const Uint64 nNow = SDL_GetTicks();
    for (const Timer &t : g_timers) {
      const Sint64 nLeft = static_cast<Sint64>(t.nDue) - static_cast<Sint64>(nNow);
      if (nLeft < nWait)
        nWait = nLeft < 0 ? 0 : static_cast<Sint32>(nLeft);
    }
    SDL_WaitEventTimeout(NULL, nWait);
  }
}

LONG_PTR WinMsgDispatch(const WinMsg *pMsg)
{
  if (pMsg->uMsg == WM_TIMER_ && pMsg->lParam != 0) {
    reinterpret_cast<WinMsgTimerProc>(pMsg->lParam)(pMsg->hWnd, WM_TIMER_, pMsg->wParam, pMsg->dwTime);
    return 0;
  }
  if (g_pfnProc == NULL || pMsg->hWnd == NULL)
    return 0;
  return g_pfnProc(pMsg->hWnd, pMsg->uMsg, pMsg->wParam, pMsg->lParam);
}

// TranslateMessage's part: the characters Windows makes of a key that text does not carry
// (Backspace, Tab, Enter, Escape and Ctrl with a letter), and WM_SYSCHAR for Alt with a
// key. SDL's text gives the rest.
BOOL WinMsgTranslate(const WinMsg *pMsg)
{
  if (pMsg->uMsg != WM_KEYDOWN_ && pMsg->uMsg != WM_SYSKEYDOWN_)
    return FALSE;
  const int vk = static_cast<int>(pMsg->wParam);
  const LONG_PTR lRepeat = pMsg->lParam & 0xFFFF;
  int c = 0;
  if (vk == VK_BACK_ || vk == VK_TAB_ || vk == VK_RETURN_ || vk == VK_ESCAPE_)
    c = vk;
  else if (Down(VK_CONTROL_) && !Down(VK_MENU_) && vk >= 'A' && vk <= 'Z')
    c = vk - 'A' + 1;
  else if (pMsg->uMsg == WM_SYSKEYDOWN_ && vk >= 'A' && vk <= 'Z')
    c = Down(VK_SHIFT_) ? vk : vk - 'A' + 'a';
  else if (pMsg->uMsg == WM_SYSKEYDOWN_ && ((vk >= '0' && vk <= '9') || vk == VK_SPACE_))
    c = vk;
  if (c == 0)
    return FALSE;
  WinMsg m = *pMsg;
  m.uMsg = pMsg->uMsg == WM_SYSKEYDOWN_ ? WM_SYSCHAR_ : WM_CHAR_;
  m.wParam = static_cast<UINT_PTR>(c);
  m.lParam = (pMsg->lParam & ~0xFFFF) | lRepeat;
  // ahead of what is queued, as Windows posts it to the front of the input
  g_queue.push_front(m);
  return TRUE;
}

BOOL WinMsgPost(void *hWnd, unsigned int uMsg, UINT_PTR wParam, LONG_PTR lParam)
{
  WinMsg m;
  memset(&m, 0, sizeof m);
  m.hWnd = hWnd ? hWnd : g_hWnd;
  m.uMsg = uMsg;
  m.wParam = wParam;
  m.lParam = lParam;
  m.dwTime = WinMsgTicks();
  g_queue.push_back(m);
  return TRUE;
}

LONG_PTR WinMsgSend(void *hWnd, unsigned int uMsg, UINT_PTR wParam, LONG_PTR lParam)
{
  if (g_pfnProc == NULL)
    return 0;
  return g_pfnProc(hWnd ? hWnd : g_hWnd, uMsg, wParam, lParam);
}

LONG_PTR WinMsgDefProc(void *hWnd, unsigned int uMsg, UINT_PTR wParam, LONG_PTR)
{
  if (uMsg == WM_SYSCOMMAND_ && (wParam & 0xFFF0) == SC_CLOSE_)
    WinMsgPost(hWnd, WM_CLOSE_, 0, 0);
  else if (uMsg == WM_CLOSE_)
    WinMsgPost(hWnd, WM_DESTROY_, 0, 0);
  return 0;
}

UINT_PTR WinMsgSetTimer(void *hWnd, UINT_PTR idEvent, unsigned int nMs, WinMsgTimerProc pfnProc)
{
  if (hWnd == NULL)
    idEvent = ++g_nNextTimerId;
  for (Timer &t : g_timers) {
    if (t.hWnd == hWnd && t.id == idEvent) {
      t.nMs = nMs;
      t.nDue = SDL_GetTicks() + nMs;
      t.pfn = pfnProc;
      return idEvent;
    }
  }
  Timer t = {hWnd, idEvent, nMs, SDL_GetTicks() + nMs, pfnProc};
  g_timers.push_back(t);
  return idEvent ? idEvent : 1;
}

BOOL WinMsgKillTimer(void *hWnd, UINT_PTR idEvent)
{
  for (size_t i = 0; i < g_timers.size(); i++) {
    if (g_timers[i].id == idEvent && (g_timers[i].hWnd == hWnd || hWnd == NULL)) {
      g_timers.erase(g_timers.begin() + i);
      return TRUE;
    }
  }
  return FALSE;
}

short WinMsgKeyState(int nVirtKey)
{
  const BYTE b = g_keys[nVirtKey & 0xFF];
  return static_cast<short>(((b & 0x80) ? 0x8000 : 0) | (b & 1));
}

short WinMsgAsyncKeyState(int nVirtKey)
{
  // what is down now, not as of the message read: the buttons from SDL's own state
  if (nVirtKey == VK_LBUTTON_ || nVirtKey == VK_RBUTTON_ || nVirtKey == VK_MBUTTON_) {
    const SDL_MouseButtonFlags f = SDL_GetGlobalMouseState(NULL, NULL);
    const SDL_MouseButtonFlags want = nVirtKey == VK_LBUTTON_ ? SDL_BUTTON_LMASK
                                      : nVirtKey == VK_RBUTTON_ ? SDL_BUTTON_RMASK : SDL_BUTTON_MMASK;
    return static_cast<short>((f & want) ? 0x8000 : 0);
  }
  return static_cast<short>(Down(nVirtKey) ? 0x8000 : 0);
}

void *WinMsgSetCapture(void *hWnd)
{
  void *pOld = g_capture;
  g_capture = hWnd;
  SDL_CaptureMouse(hWnd != NULL);
  return pOld;
}

BOOL WinMsgReleaseCapture(void)
{
  void *pOld = g_capture;
  g_capture = NULL;
  SDL_CaptureMouse(false);
  // Windows sends the window that had the capture WM_CAPTURECHANGED, there and then
  if (pOld)
    WinMsgSend(pOld, WM_CAPTURECHANGED_, 0, 0);
  return TRUE;
}

int WinMsgShowCursor(BOOL bShow)
{
  g_nCursorShown += bShow ? 1 : -1;
  return g_nCursorShown;
}

// ---------------------------------------------------------------------------
// Accelerators.

#pragma pack(push, 2)
struct Accel {
  BYTE fVirt;
  WORD key;
  WORD cmd;
};
#pragma pack(pop)
static_assert(sizeof(Accel) == 6, "Accel is Windows' ACCEL");
enum { FVIRTKEY_ = 1, FSHIFT_ = 4, FCONTROL_ = 8, FALT_ = 0x10 };

struct AccelTable {
  std::vector<Accel> entries;
};

void *WinMsgCreateAccelerators(const void *pEntries, int nCount)
{
  if (pEntries == NULL || nCount <= 0)
    return NULL;
  AccelTable *t = new AccelTable;
  const Accel *p = static_cast<const Accel *>(pEntries);
  t->entries.assign(p, p + nCount);
  return t;
}

int WinMsgCopyAccelerators(void *hAccel, void *pEntries, int nCount)
{
  const AccelTable *t = static_cast<const AccelTable *>(hAccel);
  if (t == NULL)
    return 0;
  const int n = static_cast<int>(t->entries.size());
  if (pEntries == NULL)
    return n;
  const int nCopy = nCount < n ? nCount : n;
  memcpy(pEntries, t->entries.data(), nCopy * sizeof(Accel));
  return nCopy;
}

BOOL WinMsgDestroyAccelerators(void *hAccel)
{
  delete static_cast<AccelTable *>(hAccel);
  return hAccel != NULL;
}

void *WinMsgLoadAccelerators(void *hModule, int nResId)
{
  DWORD cb = 0;
  const void *p = PortModuleResource(hModule, nResId, 9, &cb);
  return p != NULL ? WinMsgAccelerators(p, cb) : NULL;
}

// An RT_ACCELERATOR resource is 8 bytes an entry (flags, key, command, padding), the
// last with flag 0x80.
void *WinMsgAccelerators(const void *pResource, unsigned long cb)
{
  const BYTE *p = static_cast<const BYTE *>(pResource);
  if (p == NULL || cb < 8)
    return NULL;
  std::vector<Accel> v;
  for (DWORD off = 0; off + 8 <= cb; off += 8) {
    Accel a;
    a.fVirt = static_cast<BYTE>(p[off] & 0x7F);
    a.key = static_cast<WORD>(p[off + 2] | (p[off + 3] << 8));
    a.cmd = static_cast<WORD>(p[off + 4] | (p[off + 5] << 8));
    v.push_back(a);
    if (p[off] & 0x80)
      break;
  }
  return WinMsgCreateAccelerators(v.data(), static_cast<int>(v.size()));
}

int WinMsgTranslateAccelerator(void *hWnd, void *hAccel, const WinMsg *pMsg)
{
  const AccelTable *t = static_cast<const AccelTable *>(hAccel);
  if (t == NULL)
    return 0;
  const unsigned int u = pMsg->uMsg;
  const bool bKey = u == WM_KEYDOWN_ || u == WM_SYSKEYDOWN_;
  const bool bChar = u == WM_CHAR_ || u == WM_SYSCHAR_;
  if (!bKey && !bChar)
    return 0;
  const BYTE mods = static_cast<BYTE>((Down(VK_SHIFT_) ? FSHIFT_ : 0) | (Down(VK_CONTROL_) ? FCONTROL_ : 0) |
                                      (Down(VK_MENU_) ? FALT_ : 0));
  for (const Accel &a : t->entries) {
    if (bKey && (a.fVirt & FVIRTKEY_)) {
      if (a.key != pMsg->wParam || (a.fVirt & (FSHIFT_ | FCONTROL_ | FALT_)) != mods)
        continue;
    } else if (bChar && !(a.fVirt & FVIRTKEY_)) {
      if (a.key != pMsg->wParam || ((a.fVirt & FALT_) != 0) != (u == WM_SYSCHAR_))
        continue;
    } else {
      continue;
    }
    WinMsgSend(hWnd, WM_COMMAND_, static_cast<UINT_PTR>(a.cmd) | (1u << 16), 0);
    return 1;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// The window.

void *WinMsgCreateWindow(WinMsgProc pfnProc, const char *pszTitle)
{
  g_pfnProc = pfnProc;
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    return NULL;
#if defined(_WIN32)
  // 1.17's window class name, which an instance check and the test tools look for, and
  // its double clicks
  SDL_RegisterApp("SWarClass", 0x0008 /* CS_DBLCLKS */, NULL);
#endif
  SDL_WindowFlags flags = SDL_WINDOW_HIDDEN;
#if defined(__APPLE__)
  // A Mac's window has a pixel for each of a Retina screen's, two to a point, so the menus and
  // the game are drawn at the screen's own resolution; the client area and the mouse are in
  // those pixels, as on Windows, and port_posix.cpp's positions too. Not a test's window, hidden
  // or in the background, whose client pixels are the positions a harness posts.
  const char *pszMode = SDL_getenv("HYPERION_WINDOW");
  if (pszMode == NULL || (SDL_strcasecmp(pszMode, "hidden") != 0 && SDL_strcasecmp(pszMode, "background") != 0))
    flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
  g_window = SDL_CreateWindow(pszTitle, 640, 480, flags);
  if (g_window == NULL)
    return NULL;
#if defined(_WIN32)
  g_hWnd = SDL_GetPointerProperty(SDL_GetWindowProperties(g_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
  SDL_SetWindowsMessageHook(WindowsHook, NULL);
#else
  g_hWnd = g_window;
#endif
  // keys' text comes as SDL's text events, for the whole of the game
  SDL_StartTextInput(g_window);
  const SDL_Keymod mod = SDL_GetModState();
  g_keys[VK_CAPITAL_] = (mod & SDL_KMOD_CAPS) ? 1 : 0;
  g_keys[VK_NUMLOCK_] = (mod & SDL_KMOD_NUM) ? 1 : 0;
  g_keys[VK_SCROLL_] = (mod & SDL_KMOD_SCROLL) ? 1 : 0;
  return g_hWnd;
}

SDL_Window *WinMsgSdlWindow(void)
{
  return g_window;
}

void *WinMsgWindowHandle(void)
{
  return g_hWnd;
}
