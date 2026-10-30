// port_local.cpp -- Hyperion's stand-in for what an install may lack of the classic game's small
// files.
//
// The game reads its key tables, a few dialogs and its locale from Local.dll, and the menus'
// error pop-up from it too. A StarCraft: Remastered install has one only in English, German,
// French, Spanish and Italian, and only on Windows, so in any other language, and on a Mac, the
// game could not start. With none found (InitLocal) the game runs on this instead:
//   - the key tables and the pop-up's two layouts, the classic ones as generated source
//     (gen/local117.cpp, tools/local117.py);
//   - the pop-up's two pictures, which every Remastered install keeps under SD\glue\error\ with
//     the same bytes, or a plain one Hyperion draws;
//   - the dialogs in Hyperion's own words (PortLocalDialog);
//   - the locale, US English, as the game text is (GetLocale).
// The game types (Templates\*.got) are in the English folder of an install alone, so another
// language's has none; the classic ones stand in (PortLocalTemplate, port_remastered.cpp).
#include <SDL3/SDL.h>
#include <string.h>
#include <string>
#include <vector>
#include "port_local.h"
#include "../front/port_ui.h"  // PortCascAvailable, PortCascRead

#include "../../gen/local117.cpp"

namespace {

bool g_bOwn;

struct Held {
  std::string name;
  void *p;
  unsigned long cb;
};
std::vector<Held> g_held;  // what PortLocalResource gave, kept as a module's resources are

// A plain picture of the pop-up's size, a dark field in a lighter frame, for an install that
// has none of the pop-up's own (a classic folder without its Local.dll): a PCX, as the game reads it.
void *PlainPicture(unsigned long *pcbSize)
{
  const int w = 288, h = 153;
  std::string pcx(128, '\0');
  pcx[0] = 10;  // PCX
  pcx[1] = 5;   // version 3.0 on, a palette at the end
  pcx[2] = 1;   // run-length encoded
  pcx[3] = 8;   // bits a pixel
  auto Put16 = [&](size_t at, int v) {
    pcx[at] = static_cast<char>(v & 0xFF);
    pcx[at + 1] = static_cast<char>(v >> 8);
  };
  Put16(8, w - 1);
  Put16(10, h - 1);
  Put16(12, 72);
  Put16(14, 72);
  pcx[65] = 1;  // one plane
  Put16(66, w);
  Put16(68, 1);
  for (int y = 0; y < h; ++y) {
    const bool bEdge = y < 2 || y >= h - 2;
    // a row: the frame's two pixels, the field, the frame's two
    const int aRun[3][2] = {{1, bEdge ? w : 2}, {0, bEdge ? 0 : w - 4}, {1, bEdge ? 0 : 2}};
    for (const auto &run : aRun)
      for (int n = run[1]; n > 0; n -= 63) {
        pcx += static_cast<char>(0xC0 | (n < 63 ? n : 63));
        pcx += static_cast<char>(run[0]);
      }
  }
  pcx += static_cast<char>(12);
  std::string pal(768, '\0');
  const unsigned char aField[3] = {16, 16, 24}, aFrame[3] = {96, 104, 128};
  memcpy(&pal[0], aField, 3);
  memcpy(&pal[3], aFrame, 3);
  pcx += pal;
  void *p = SDL_malloc(pcx.size());
  if (p != NULL) {
    memcpy(p, pcx.data(), pcx.size());
    *pcbSize = static_cast<unsigned long>(pcx.size());
  }
  return p;
}

}  // namespace

void PortLocalUseOwn(void)
{
  g_bOwn = true;
}

bool PortLocalOwn(void)
{
  return g_bOwn;
}

const void *PortLocalResource(const char *pszName, unsigned long *pcbSize)
{
  for (const Held &h : g_held)
    if (SDL_strcasecmp(h.name.c_str(), pszName) == 0) {
      *pcbSize = h.cb;
      return h.p;
    }
  void *p = NULL;
  unsigned long cb = 0;
  for (int i = 0; i < g_nPortLocalLayouts && p == NULL; ++i)
    if (SDL_strcasecmp(g_aPortLocalLayouts[i].pszName, pszName) == 0)
      p = PortLayoutMake(&g_aPortLocalLayouts[i], &cb);
  const char *pszFile = SDL_strcasecmp(pszName, "GLUEOK_PCX") == 0         ? "SD\\glue\\error\\glueok.pcx"
                        : SDL_strcasecmp(pszName, "GLUEOKCANCEL_PCX") == 0 ? "SD\\glue\\error\\glueokcancel.pcx"
                                                                            : NULL;
  if (p == NULL && pszFile != NULL) {
    size_t n = 0;
    if (PortCascAvailable())
      p = PortCascRead(pszFile, &n);
    cb = static_cast<unsigned long>(n);
    if (p == NULL)
      p = PlainPicture(&cb);
  }
  if (p == NULL)
    return NULL;
  g_held.push_back({pszName, p, cb});
  *pcbSize = cb;
  return p;
}

const void *PortLocalAccelerators(int nResId, unsigned long *pcbSize)
{
  for (int i = 0; i < g_nPortLocalAccels; ++i)
    if (SDL_strtol(g_aPortLocalAccels[i].pszName, NULL, 16) == nResId) {
      *pcbSize = static_cast<unsigned long>(g_aPortLocalAccels[i].cbData);
      return g_aPortLocalAccels[i].pData;
    }
  return NULL;
}

int PortLocalDialog(int nTemplate, const char *pszText)
{
  // the file error (0x6A) and the other errors that end the game: the start-up's window or the
  // crash report's has said it already, and the game ends after this
  char szText[512];
  const char *pszGo = "Go on";
  switch (nTemplate) {
    case 0x6B:
      SDL_strlcpy(szText, "A file only the StarCraft CD has is needed and the CD was not found. Put it in a drive and "
                          "choose Retry, or Quit.", sizeof szText);
      pszGo = "Retry";
      break;
    case 0x6C:
      SDL_strlcpy(szText, "This computer's processor is older than the game needs. Hyperion may not run.",
                  sizeof szText);
      break;
    case 0x6D:
      SDL_strlcpy(szText, "This computer has less memory than the game needs. Hyperion may not run well.",
                  sizeof szText);
      break;
    case 0x6F:
      SDL_strlcpy(szText, "This computer has less virtual memory than the game needs. Hyperion may not run well.",
                  sizeof szText);
      break;
    case 0x70:
      // the classic text is the megabytes as "%d M"
      SDL_snprintf(szText, sizeof szText, "Drive C: has %sB free. Saved games and replays may not fit.",
                   pszText != NULL ? pszText : "?");
      break;
    default:
      return 1;
  }
  // a run with no one to answer (port_startup.cpp's Quiet) goes on
  if (SDL_getenv("HYPERION_QUIET") != NULL || SDL_GetPathInfo("hyperion_quiet.flag", NULL))
    return 1;
  const SDL_MessageBoxButtonData aButtons[] = {
      {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, pszGo},
      {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 2, "Quit"},
  };
  const SDL_MessageBoxData box = {SDL_MESSAGEBOX_WARNING, NULL, "Hyperion", szText, 2, aButtons, NULL};
  int nButton = 1;
  if (!SDL_ShowMessageBox(&box, &nButton))
    return 1;
  return nButton == 2 ? 2 : 1;
}

void *PortLocalTemplate(const char *pszName, unsigned long *pcbSize)
{
  if (SDL_strncasecmp(pszName, "Templates\\", 10) != 0)
    return NULL;
  for (int i = 0; i < g_nPortLocalTemplates; ++i) {
    const PortLocalFile &f = g_aPortLocalTemplates[i];
    if (SDL_strcasecmp(f.pszName, pszName + 10) != 0)
      continue;
    void *p = SDL_malloc(f.cbData);
    if (p != NULL) {
      memcpy(p, f.pData, f.cbData);
      *pcbSize = static_cast<unsigned long>(f.cbData);
    }
    return p;
  }
  return NULL;
}
