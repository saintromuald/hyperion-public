// port_subtitles.cpp -- Settings' "Cinematic subtitles" (docs/modules/data.md, "Video"): the
// original cinematics have no words on screen, and Remastered has subtitle files for those that
// speak, under the same names, which this reads from the Remastered install and draws over the
// picture as a layer of the overlay, in the cutscene captions' style. English's, or those of the
// install's language when it has no English ones. Presentation only.
#include <windows.h>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "port_subtitles.h"
#include "port_language.h"            // PortLanguageGame
#include "../front/port_ui.h"        // PortCascRead, UiText
#include "../render/port_overlay.h"  // PORT_OVERLAY_LAYER
#include "../platform/port_settings.h"

namespace {

struct Cue {
  double t0, t1;  // seconds into the video
  std::vector<std::string> lines;
};

bool g_bOn = true;
bool g_bRead;  // the setting was read
bool g_bPlaying;
double g_dAt;
std::vector<Cue> g_cues;

// Most of the files are UTF-8; a few are Windows-1252 (starintr's apostrophes), which the text
// drawing would show as broken characters, so a file that is not UTF-8 is converted.
std::string SrtUtf8(const std::string &in)
{
  bool bUtf8 = true;
  for (size_t i = 0; i < in.size() && bUtf8;) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    const size_t n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (n == 0 || i + n > in.size())
      bUtf8 = false;
    for (size_t k = 1; k < n && bUtf8; ++k)
      bUtf8 = (static_cast<unsigned char>(in[i + k]) & 0xC0) == 0x80;
    i += n == 0 ? 1 : n;
  }
  if (bUtf8)
    return in;
  // 0x80 to 0x9F as Windows-1252 has them; the rest of the upper half is Latin-1's
  static const unsigned short kau1252[32] = {
      0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
      0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};
  std::string out;
  for (const char ch : in) {
    const unsigned char c = static_cast<unsigned char>(ch);
    const unsigned u = c < 0x80 ? c : c < 0xA0 ? kau1252[c - 0x80] : c;
    if (u < 0x80) {
      out += static_cast<char>(u);
    } else if (u < 0x800) {
      out += static_cast<char>(0xC0 | (u >> 6));
      out += static_cast<char>(0x80 | (u & 0x3F));
    } else {
      out += static_cast<char>(0xE0 | (u >> 12));
      out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (u & 0x3F));
    }
  }
  return out;
}

// "hh:mm:ss,mmm" as seconds; false when the text is not one.
bool SrtTime(const char *p, double *pd)
{
  int h, m, s, ms;
  if (SDL_sscanf(p, "%d:%d:%d,%d", &h, &m, &s, &ms) != 4)
    return false;
  *pd = h * 3600.0 + m * 60.0 + s + ms / 1000.0;
  return true;
}

// SubRip: blocks split by an empty line, each a number, "from --> to" and the lines shown.
void SrtParse(const std::string &text)
{
  g_cues.clear();
  size_t p = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
  Cue cue;
  int nPart = 0;  // 0 the number, 1 the times, 2 the lines
  while (p <= text.size()) {
    size_t q = text.find('\n', p);
    if (q == std::string::npos)
      q = text.size();
    std::string line = text.substr(p, q - p);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    p = q + 1;
    if (line.empty()) {
      if (nPart == 2 && !cue.lines.empty())
        g_cues.push_back(cue);
      cue = Cue();
      nPart = 0;
      continue;
    }
    if (nPart == 0) {
      nPart = 1;
    } else if (nPart == 1) {
      const size_t nArrow = line.find("-->");
      const char *pszTo = nArrow == std::string::npos ? NULL : line.c_str() + nArrow + 3;
      while (pszTo != NULL && *pszTo == ' ')
        ++pszTo;
      if (pszTo == NULL || !SrtTime(line.c_str(), &cue.t0) || !SrtTime(pszTo, &cue.t1))
        nPart = 3;  // not a cue; skipped to the next empty line
      else
        nPart = 2;
    } else if (nPart == 2) {
      cue.lines.push_back(line);
    }
  }
  if (nPart == 2 && !cue.lines.empty())
    g_cues.push_back(cue);
}

bool LayerShown(void)
{
  if (!g_bPlaying || !g_bOn)
    return false;
  for (const Cue &c : g_cues)
    if (g_dAt >= c.t0 && g_dAt < c.t1)
      return true;
  return false;
}

// In the lower part of the picture, where the cinematics' letterbox is black.
bool LayerDraw(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  (void)nWidth;
  (void)nHeight;
  const float fSize = SDL_clamp(fH * 0.034f, 14.0f, 44.0f);
  const float fLine = fSize * 1.3f;
  bool bDrew = false;
  for (const Cue &c : g_cues) {
    if (g_dAt < c.t0 || g_dAt >= c.t1)
      continue;
    const float fTop = fY + fH * 0.94f - fLine * static_cast<float>(c.lines.size());
    for (size_t i = 0; i < c.lines.size(); ++i) {
      const char *psz = c.lines[i].c_str();
      const float w = UiTextWidth(UI_FACE_TEXT, fSize, 0.0f, psz);
      const float x = fX + (fW - w) * 0.5f, fBase = fTop + fLine * static_cast<float>(i) + fSize;
      UiText(x + 1.5f, fBase + 1.5f, UI_FACE_TEXT, fSize, 0.0f, UiRgb(0x000000, 0.8f), psz);
      UiText(x, fBase, UI_FACE_TEXT, fSize, 0.0f, UiRgb(0xF2F0E6), psz);
    }
    bDrew = true;
  }
  return bDrew;
}

const PortOverlayLayer kLayer = {
    "subtitles", PORT_OVERLAY_SUBTITLES, PORT_OVERLAY_OUT_OF_GAME,
    LayerShown, NULL, LayerDraw, NULL, NULL, NULL, NULL};
PORT_OVERLAY_LAYER(kLayer);

}  // namespace

void PortSubtitlesSet(bool bOn)
{
  g_bOn = bOn;
  g_bRead = true;
}

void PortSubtitlesBegin(const char *pszFile, const char *pszOwn)
{
  g_bPlaying = false;
  g_cues.clear();
  if (!g_bRead) {
    double d = 1.0;
    PortSettingNumber("hyperion", "cinematicSubtitles", &d);
    PortSubtitlesSet(d != 0.0);
  }
  if (!g_bOn || pszFile == NULL)
    return;
  // the file's name without its folder or extension: smk\terran1.smk is terran1
  const char *pszName = pszFile;
  for (const char *p = pszFile; *p; ++p)
    if (*p == '\\' || *p == '/')
      pszName = p + 1;
  std::string name(pszName);
  const size_t nDot = name.rfind('.');
  if (nDot != std::string::npos)
    name.erase(nDot);
  size_t nSize = 0;
  char *pText = NULL;
  if (pszOwn != NULL) {
    std::string own(pszOwn);
    const size_t nOwnDot = own.rfind('.');
    if (nOwnDot != std::string::npos)
      own.erase(nOwnDot);
    pText = static_cast<char *>(SDL_LoadFile((own + ".srt").c_str(), &nSize));
  }
  // the game's language's when the font has its letters (it has none of Cyrillic's or the East
  // Asian scripts'), else English's
  const char *apszLocale[2] = {PortLanguageGame(), "enUS"};
  for (int i = 0; i < 2 && pText == NULL; ++i) {
    pText = static_cast<char *>(PortCascRead(
        (std::string("locales\\") + apszLocale[i] + "\\Assets\\Smk\\" + name + ".srt").c_str(), &nSize));
    if (pText != NULL && i == 0 && SDL_strcmp(apszLocale[0], "enUS") != 0) {
      const std::string text = SrtUtf8(std::string(pText, nSize));
      if (!UiTextCovered(UI_FACE_TEXT, text.data(), text.size())) {
        SDL_free(pText);
        pText = NULL;
      }
    }
  }
  if (pText == NULL)
    return;
  const std::string text = SrtUtf8(std::string(pText, nSize));
  SDL_free(pText);
  if (!UiTextCovered(UI_FACE_TEXT, text.data(), text.size()))
    return;
  SrtParse(text);
  g_dAt = 0.0;
  g_bPlaying = !g_cues.empty();
}

void PortSubtitlesAt(double dSeconds)
{
  g_dAt = dSeconds;
}

void PortSubtitlesEnd(void)
{
  g_bPlaying = false;
  g_cues.clear();
}
