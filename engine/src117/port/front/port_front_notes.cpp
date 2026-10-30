// port_front_notes.cpp -- Hyperion's release notes: the home screen's notifications and the
// Patch Notes window (docs/versioning.md). Presentation only.
//
// The log, engine/notes/release-notes.json, is compiled in as release_notes.h
// (tools/release_notes.py), so a build's notes are always its own. Its first three entries
// are the notifications; one closed stays closed, its id kept in settings.json
// ("hyperion"/"notesDismissed"). An entry's media is a list the window steps through:
//   image   a PNG in the notes folder beside Hyperion.exe (notes\<file>)
//   frames  a PNG of frames side by side, played at "fps"
//   clip    a Smacker video from the player's own install, as the campaign screen plays them
// The pictures go in the picture atlas's slots 24 and 25, one picture up to 768 by 384.
#include <windows.h>
#include <math.h>
#include <string.h>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "../platform/port_posix.h"  // PortNativePath
#include "../platform/port_paths.h"  // PortPackagedFile
#include "../data/port_smacker.h"
#include "port_frontkit.h"
#include "port_frontscreens.h"
#include "../platform/port_json.h"
#include "../platform/port_settings.h"
#include "../data/port_stormapi.h"  // SFileOpenFileEx
#include "port_ui.h"
#include "../../../hyperion/version.h"
#if __has_include("build_id.h")
#include "build_id.h"  // the commit and the build's number, written by tools/build_engine_x64.py
#endif
#if __has_include("release_notes.h")
#include "release_notes.h"  // kReleaseNotesJson, written by tools/release_notes.py
#define HAVE_RELEASE_NOTES 1
#endif
#ifndef HYPERION_BUILD_NUMBER
#define HYPERION_BUILD_NUMBER 0
#endif
#ifndef HYPERION_COMMIT
#define HYPERION_COMMIT ""
#endif

using namespace FrontKit;

namespace {

// ---------------------------------------------------------------------------
// The log.

struct Item {
  std::string title, text;
};

struct Media {
  std::string kind, file, caption;
  int nFrames = 1;
  float fFps = 10.0f;
};

enum { SECTION_NEW, SECTION_IMPROVED, SECTION_CHANGED, SECTION_FIXED, SECTIONS };
const char *const kSectionKeys[SECTIONS] = {"new", "improved", "changed", "fixed"};
const char *const kSectionHeads[SECTIONS] = {PT_("New features"), PT_("Improvements"), PT_("Changes"),
                                             PT_("Bug fixes")};

struct Entry {
  std::string id, kind, version, date, title, summary, line;  // line: the notification's, else the summary
  std::vector<std::string> body;
  std::vector<Media> media;
  std::vector<Item> sections[SECTIONS];
};

std::vector<Entry> g_entries;
bool g_bLoaded;

// The notifications: the newest entries, at most three, and which were closed.
const int kToasts = 3;
bool g_bGone[kToasts];

void Load(void)
{
  if (g_bLoaded)
    return;
  g_bLoaded = true;
#ifdef HAVE_RELEASE_NOTES
  PortJson root;
  std::string error;
  if (!PortJsonParse(reinterpret_cast<const char *>(kReleaseNotesJson), &root, &error)) {
    FrontLog("notes: the release notes do not read: %s\n", error.c_str());
    return;
  }
  const PortJson &entries = root["entries"];
  for (size_t i = 0; i < entries.size(); ++i) {
    const PortJson &e = entries.At(i);
    Entry n;
    n.id = e["id"].Str();
    n.kind = e["kind"].Str("release");
    n.version = e["version"].Str();
    n.date = e["date"].Str();
    n.title = e["title"].Str();
    n.summary = e["summary"].Str();
    n.line = e["line"].Str(n.summary.c_str());
    for (size_t k = 0; k < e["body"].size(); ++k)
      n.body.push_back(e["body"].At(k).Str());
    for (size_t k = 0; k < e["media"].size(); ++k) {
      const PortJson &m = e["media"].At(k);
      Media md;
      md.kind = m["kind"].Str();
      md.file = m["file"].Str();
      md.caption = m["caption"].Str();
      md.nFrames = SDL_max(1, static_cast<int>(m["frames"].Num(1)));
      md.fFps = static_cast<float>(SDL_clamp(m["fps"].Num(10), 1.0, 60.0));
      n.media.push_back(md);
    }
    for (int s = 0; s < SECTIONS; ++s)
      for (size_t k = 0; k < e[kSectionKeys[s]].size(); ++k) {
        const PortJson &it = e[kSectionKeys[s]].At(k);
        n.sections[s].push_back({it["title"].Str(), it.kind == PortJson::STRING ? it.s : it["text"].Str()});
      }
    if (!n.id.empty())
      g_entries.push_back(n);
  }
#endif
  // the notifications closed before
  char sz[1024] = "";
  if (PortSettingString("hyperion", "notesDismissed", sz, sizeof sz)) {
    const std::string ids = std::string(" ") + sz + " ";
    for (int i = 0; i < kToasts && i < static_cast<int>(g_entries.size()); ++i)
      g_bGone[i] = ids.find(" " + g_entries[i].id + " ") != std::string::npos;
  }
}

void SaveDismissed(void)
{
  std::string ids;
  for (int i = 0; i < kToasts && i < static_cast<int>(g_entries.size()); ++i)
    if (g_bGone[i])
      ids += (ids.empty() ? "" : " ") + g_entries[i].id;
  PortSettingSetString("hyperion", "notesDismissed", ids.c_str());
}

// A day's number from a date, to count the days between two (Howard Hinnant's days_from_civil).
LONG DayNumber(int y, int m, int d)
{
  y -= m <= 2;
  const LONG era = (y >= 0 ? y : y - 399) / 400;
  const LONG yoe = y - era * 400;
  const LONG doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const LONG doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

// An entry's date as the notifications say it: Today, Yesterday, so many days ago, or the date.
std::string Age(const std::string &date)
{
  int y = 0, m = 0, d = 0;
  if (SDL_sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12)
    return date;
  // day, month and year, in the language's order
  const std::string full =
      PortTextF("{0} {1} {2}", {std::to_string(d), PortTextC("month", kMonths[m - 1]), std::to_string(y)});
  SDL_Time now = 0;
  SDL_DateTime dt;
  if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &dt, true))
    return full;
  const LONG n = DayNumber(dt.year, dt.month, dt.day) - DayNumber(y, m, d);
  if (n == 0)
    return PortText("Today");
  if (n == 1)
    return PortText("Yesterday");
  if (n > 1 && n < 7)
    return PortTextNF("{0} day ago", "{0} days ago", n, {std::to_string(n)});
  return full;
}

// ---------------------------------------------------------------------------
// Media: a picture, frames or a clip into the notes' slot.

const int kSlot = 24;
const int kSlotW = 2 * UI_PICTURE_SIDE, kSlotH = UI_PICTURE_SIDE;

struct Player {
  int nEntry = -1, nMedia = -1;  // what it plays
  bool bFailed = false;
  std::string why;               // why it failed
  int nPicW = 0, nPicH = 0;      // the picture in the slot, 0 none
  // image and frames: the PNG, at most kSlotW by kSlotH a frame
  std::vector<unsigned char> sheet;
  int nSheetW = 0, nSheetH = 0, nFrameW = 0;
  int nFrame = -1;
  Uint64 nStart = 0;
  // clip
  HANDLE hFile = INVALID_HANDLE_VALUE;
  SmkFile *pSmk = nullptr;
  std::vector<BYTE> frame;
  int nW = 0, nH = 0, nShrink = 1;
};
Player g_play;

void PlayerStop(void)
{
  if (g_play.pSmk != nullptr)
    SmkClose(g_play.pSmk);
  if (g_play.hFile != INVALID_HANDLE_VALUE)
    CloseHandle(g_play.hFile);
  g_play = Player();
}

void Show(const unsigned char *pRgba, int w, int h)
{
  UiPictureSet(kSlot, pRgba, w, h);
  g_play.nPicW = w;
  g_play.nPicH = h;
}

bool LoadSheet(const Media &m)
{
  const std::string path = PortPackagedFile(("notes\\" + m.file).c_str());
  SDL_Surface *png = SDL_LoadPNG(PortNativePath(path.c_str()));
  if (png == nullptr) {
    // {0} the file the notes asked for
    g_play.why = PortTextF("Not found: {0}", {"notes\\" + m.file});
    FrontLog("notes: %s does not load: %s\n", path.c_str(), SDL_GetError());
    return false;
  }
  SDL_Surface *rgba = SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(png);
  if (rgba == nullptr)
    return false;
  // a frame at most the slot's size, the sheet scaled with it
  const int nFrames = SDL_max(1, m.nFrames);
  const float fw = static_cast<float>(rgba->w) / nFrames, fh = static_cast<float>(rgba->h);
  const float k = SDL_min(1.0f, SDL_min(kSlotW / fw, kSlotH / fh));
  int nFrameW = SDL_max(1, static_cast<int>(fw * k));
  const int nH = SDL_max(1, static_cast<int>(fh * k));
  if (k < 1.0f) {
    SDL_Surface *s = SDL_ScaleSurface(rgba, nFrameW * nFrames, nH, SDL_SCALEMODE_LINEAR);
    SDL_DestroySurface(rgba);
    rgba = s;
    if (rgba == nullptr)
      return false;
  } else {
    nFrameW = rgba->w / nFrames;
  }
  g_play.nSheetW = rgba->w;
  g_play.nSheetH = rgba->h;
  g_play.nFrameW = nFrameW;
  g_play.sheet.resize(static_cast<size_t>(rgba->w) * rgba->h * 4);
  for (int y = 0; y < rgba->h; ++y)
    SDL_memcpy(&g_play.sheet[static_cast<size_t>(y) * rgba->w * 4],
               static_cast<const unsigned char *>(rgba->pixels) + static_cast<size_t>(y) * rgba->pitch,
               static_cast<size_t>(rgba->w) * 4);
  SDL_DestroySurface(rgba);
  return true;
}

void SheetFrame(int n)
{
  if (n == g_play.nFrame || g_play.sheet.empty())
    return;
  g_play.nFrame = n;
  std::vector<unsigned char> px(static_cast<size_t>(g_play.nFrameW) * g_play.nSheetH * 4);
  for (int y = 0; y < g_play.nSheetH; ++y)
    SDL_memcpy(&px[static_cast<size_t>(y) * g_play.nFrameW * 4],
               &g_play.sheet[(static_cast<size_t>(y) * g_play.nSheetW + static_cast<size_t>(n) * g_play.nFrameW) * 4],
               static_cast<size_t>(g_play.nFrameW) * 4);
  Show(px.data(), g_play.nFrameW, g_play.nSheetH);
}

bool ClipOpen(const Media &m)
{
  if (!SFileOpenFileEx(NULL, m.file.c_str(), 5, &g_play.hFile)) {  // scope 5: a handle at the data
    g_play.hFile = INVALID_HANDLE_VALUE;
    g_play.why = PortTextF("Not in this install: {0}", {m.file});
    FrontLog("notes: no %s\n", m.file.c_str());
    return false;
  }
  g_play.pSmk = SmkOpen(g_play.hFile, 0x1000);  // from the handle, with no sound
  if (g_play.pSmk == nullptr) {
    g_play.why = PortTextF("Does not play: {0}", {m.file});
    return false;
  }
  g_play.nW = static_cast<int>(SmkWidth(g_play.pSmk));
  g_play.nH = static_cast<int>(SmkHeight(g_play.pSmk));
  g_play.nShrink = 1;
  while (g_play.nW / g_play.nShrink > kSlotW || g_play.nH / g_play.nShrink > kSlotH)
    ++g_play.nShrink;
  g_play.frame.assign(static_cast<size_t>(g_play.nW) * g_play.nH, 0);
  SmkToBuffer(g_play.pSmk, 0, 0, static_cast<ULONG>(g_play.nW), g_play.frame.data());
  return true;
}

// The clip's next frame when it is due, smaller by whole steps when it is larger than the
// slot; from its first frame again after its last.
void ClipStep(void)
{
  SmkFile *p = g_play.pSmk;
  if (p == nullptr || (g_play.nPicW > 0 && SmkWait(p)))
    return;
  SmkDoFrame(p);
  const BYTE *pPal = SmkPaletteRGB(p);
  const int k = g_play.nShrink, w = g_play.nW / k, h = g_play.nH / k;
  std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      int r = 0, g = 0, b = 0;
      for (int dy = 0; dy < k; ++dy)
        for (int dx = 0; dx < k; ++dx) {
          const BYTE *e = pPal + g_play.frame[static_cast<size_t>(y * k + dy) * g_play.nW + x * k + dx] * 3;
          r += e[0];
          g += e[1];
          b += e[2];
        }
      unsigned char *o = &rgba[(static_cast<size_t>(y) * w + x) * 4];
      o[0] = static_cast<unsigned char>(r / (k * k));
      o[1] = static_cast<unsigned char>(g / (k * k));
      o[2] = static_cast<unsigned char>(b / (k * k));
      o[3] = 255;
    }
  Show(rgba.data(), w, h);
  if (SmkFrameNum(p) + 1 >= SmkFrames(p)) {
    const Media m = g_entries[g_play.nEntry].media[g_play.nMedia];
    const int nEntry = g_play.nEntry, nMedia = g_play.nMedia;
    const int nW = g_play.nPicW, nH = g_play.nPicH;
    PlayerStop();
    g_play.nEntry = nEntry;
    g_play.nMedia = nMedia;
    g_play.nPicW = nW;
    g_play.nPicH = nH;
    g_play.bFailed = !ClipOpen(m);
    return;
  }
  SmkNextFrame(p);
}

// What the window shows this frame, put in the slot when it is a picture.
void PlayerStep(int nEntry, int nMedia)
{
  if (nEntry != g_play.nEntry || nMedia != g_play.nMedia) {
    PlayerStop();
    g_play.nEntry = nEntry;
    g_play.nMedia = nMedia;
    g_play.nStart = SDL_GetTicks();
    if (nEntry < 0 || nMedia < 0)
      return;
    const Media &m = g_entries[nEntry].media[nMedia];
    if (m.kind == "image" || m.kind == "frames")
      g_play.bFailed = !LoadSheet(m);
    else if (m.kind == "clip")
      g_play.bFailed = !ClipOpen(m);
  }
  if (nEntry < 0 || nMedia < 0 || g_play.bFailed)
    return;
  const Media &m = g_entries[nEntry].media[nMedia];
  if (m.kind == "image")
    SheetFrame(0);
  else if (m.kind == "frames")
    SheetFrame(static_cast<int>((SDL_GetTicks() - g_play.nStart) / 1000.0f * m.fFps) % SDL_max(1, m.nFrames));
  else if (m.kind == "clip")
    ClipStep();
}

// ---------------------------------------------------------------------------
// The window.

bool g_bUp;
int g_nPick;             // the entry shown
int g_nMedia;            // its media shown
float g_fScroll;         // the entry's page, scrolled
float g_fScrollMax;      // as far as it scrolls, from the last frame
float g_fListScroll;     // the list of entries, scrolled
float g_fListMax;

void Pick(int n)
{
  if (g_entries.empty())
    return;
  n = SDL_clamp(n, 0, static_cast<int>(g_entries.size()) - 1);
  if (n != g_nPick) {
    g_nPick = n;
    g_nMedia = 0;
    g_fScroll = 0.0f;
  }
}

void MediaStepBy(int d)
{
  const int n = static_cast<int>(g_entries[g_nPick].media.size());
  if (n > 1)
    g_nMedia = (g_nMedia + d + n) % n;
}

// The media frame: its well, then the media fitted in it.
void DrawMedia(float x, float y, float w, float h, void *)
{
  const float u = Sc(1.0f);
  UiFill(x, y, w, h, UiRgb(0x05080c), 6 * u);
  if (g_nPick < 0 || g_nPick >= static_cast<int>(g_entries.size()) ||
      g_nMedia >= static_cast<int>(g_entries[g_nPick].media.size())) {
    UiRing(x, y, w, h, kScWellRim, 6 * u, 2 * u);
    return;
  }
  if (g_play.bFailed || g_play.nPicW == 0) {
    const char *psz = g_play.bFailed ? g_play.why.c_str() : "";
    const float fSize = Sc(16);
    UiText(x + (w - UiTextWidth(UI_FACE_TEXT, fSize, 0.0f, psz)) * 0.5f, y + h * 0.5f, UI_FACE_TEXT, fSize, 0.0f,
           kScInkOff, psz);
  } else {
    // the picture as large as fits, in its own shape
    const float k = SDL_min(w / g_play.nPicW, h / g_play.nPicH);
    const float pw = g_play.nPicW * k, ph = g_play.nPicH * k;
    UiPicture(x + (w - pw) * 0.5f, y + (h - ph) * 0.5f, pw, ph, kSlot, UiRgb(0xffffff), 0.0f);
  }
  UiRing(x, y, w, h, kScWellRim, 6 * u, 2 * u);
}

const UiColor &KindInk(const Entry &e)
{
  return e.kind == "announcement" ? kTool : kScInkAccept;
}

std::string TagOf(const Entry &e)
{
  // {0} the version the entry is for
  return e.kind == "announcement" ? std::string(PortText("Announcement")) : PortTextF("Update v{0}", {e.version});
}

void Gap1(float f)
{
  Decl g;
  g.d.layout.sizing.height = Fixed(Sc(f));
  Open(g);
  Close();
}

void Page(const Entry &e)
{
  Decl meta;
  meta.d.layout.childGap = Gap(Fs(12));
  Open(meta);
  Text(Keep(Upper(TagOf(e).c_str())), UI_FACE_TEXT, Fs(15), 0.14f, KindInk(e));
  Text(Keep(Upper(Age(e.date).c_str())), UI_FACE_TEXT, Fs(15), 0.14f, kScInkOff);
  Close();
  Decl head;
  head.d.layout.padding = Pad(0, 0, Fs(4), Fs(6));
  Open(head);
  TextGlow(Keep(e.title), UI_FACE_WIDE, Fs(30), 0.02f, kScInkHi);
  Close();
  Decl sum;
  sum.d.layout.sizing.width = Grow();
  Open(sum);
  Text(Keep(e.summary), UI_FACE_TEXT, Fs(19), 0.0f, kScInkButton, -1, 1.4f);
  Close();

  if (!e.media.empty()) {
    Gap1(10);
    static UiCustom media = {0.0f, DrawMedia, NULL};
    Decl box;
    box.d.id = Id("ntmedia");
    box.d.layout.sizing.width = Grow();
    box.d.layout.sizing.height = Fixed(Sc(895 * 9 / 16));
    box.d.custom.customData = &media;
    Open(box);
    Close();
    // the caption, and with more than one, the arrows between them
    Decl cap;
    cap.d.layout.sizing.width = Grow();
    cap.d.layout.childGap = Gap(Fs(12));
    cap.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(cap);
    Decl words;
    words.d.layout.sizing.width = Grow();
    Open(words);
    Text(Keep(e.media[g_nMedia].caption), UI_FACE_TEXT, Fs(16), 0.0f, kScInkMenu, -1, 1.35f);
    Close();
    if (e.media.size() > 1) {
      for (int d = -1; d <= 1; d += 2) {
        Decl a;
        a.d.id = Id("ntarrow", d + 1);
        a.d.layout.sizing.width = Fixed(Sc(40));
        a.d.layout.sizing.height = Fixed(Sc(36));
        a.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
        a.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
        const bool bHot = Over(a.d.id);
        if (Clicked(a.d.id))
          MediaStepBy(d);
        a.d.custom.customData = Custom(DrawScRaised, bHot ? kScTabHi : kScTab);
        Open(a);
        Text(d < 0 ? "\xE2\x80\xB9" : "\xE2\x80\xBA", UI_FACE_TEXT, Fs(26), 0.0f, bHot ? kScInkHi : kScInkButton);
        Close();
        if (d < 0) {
          const std::string n = std::to_string(g_nMedia + 1) + " / " + std::to_string(e.media.size());
          Text(Keep(n), UI_FACE_TEXT, Fs(16), 0.04f, kScInkMenu);
        }
      }
    }
    Close();
  }

  for (const std::string &para : e.body) {
    Decl p;
    p.d.layout.sizing.width = Grow();
    p.d.layout.padding = Pad(0, 0, Fs(6), 0);
    Open(p);
    Text(para.c_str(), UI_FACE_TEXT, Fs(17), 0.0f, kScInkButton, -1, 1.45f);
    Close();
  }

  static const UiColor *const kHeadInk[SECTIONS] = {&kScInkAccept, &kOk, &kScInkLabel, &kScInkNav};
  for (int s = 0; s < SECTIONS; ++s) {
    if (e.sections[s].empty())
      continue;
    Gap1(14);
    Decl head2;
    head2.d.layout.childGap = Gap(Fs(12));
    head2.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    head2.d.layout.sizing.width = Grow();
    head2.d.layout.padding = Pad(0, 0, 0, Fs(6));
    head2.d.border = Border(kLbDivider, 0, 0, 0, 1);
    Open(head2);
    Decl bar;
    bar.d.layout.sizing.width = Fixed(Sc(4));
    bar.d.layout.sizing.height = Fixed(Sc(20));
    bar.d.backgroundColor = Col(*kHeadInk[s]);
    Open(bar);
    Close();
    Text(Keep(Upper(PortText(kSectionHeads[s]))), UI_FACE_WIDE, Fs(16), 0.08f, *kHeadInk[s]);
    Close();
    for (const Item &it : e.sections[s]) {
      Decl row;
      row.d.layout.sizing.width = Grow();
      row.d.layout.childGap = Gap(Fs(12));
      row.d.layout.padding = Pad(Fs(4), 0, Fs(6), 0);
      Open(row);
      Decl dot;
      dot.d.layout.sizing.width = Fixed(Sc(7));
      dot.d.layout.sizing.height = Fixed(Sc(7));
      dot.d.layout.padding = Pad(0, 0, 0, 0);
      dot.d.backgroundColor = Col(*kHeadInk[s]);
      dot.d.cornerRadius.topLeft = dot.d.cornerRadius.topRight = dot.d.cornerRadius.bottomLeft =
          dot.d.cornerRadius.bottomRight = Sc(3.5f);
      Decl dotWrap;
      dotWrap.d.layout.padding = Pad(0, 0, Fs(9), 0);
      Open(dotWrap);
      Open(dot);
      Close();
      Close();
      Decl col;
      col.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
      col.d.layout.sizing.width = Grow();
      col.d.layout.childGap = Gap(Fs(2));
      Open(col);
      if (!it.title.empty())
        Text(Keep(it.title), UI_FACE_BOLD, Fs(17), 0.0f, kScInkHi);
      Text(Keep(it.text), UI_FACE_TEXT, Fs(16), 0.0f, kScInkButton, -1, 1.4f);
      Close();
      Close();
    }
  }
  Gap1(20);
}

}  // namespace

namespace FrontKit {

int NotesToasts(void)
{
  Load();
  return SDL_min(kToasts, static_cast<int>(g_entries.size()));
}

bool NotesToastGone(int i)
{
  Load();
  return i < 0 || i >= kToasts || g_bGone[i];
}

int NotesLeft(void)
{
  int n = 0;
  for (int i = 0; i < NotesToasts(); ++i)
    n += !g_bGone[i];
  return n;
}

void NotesDismiss(int i)
{
  if (i < 0 || i >= NotesToasts())
    return;
  g_bGone[i] = true;
  SaveDismissed();
}

void NotesDismissAll(void)
{
  for (int i = 0; i < NotesToasts(); ++i)
    g_bGone[i] = true;
  SaveDismissed();
}

NotesToastText NotesToast(int i)
{
  Load();
  NotesToastText t;
  if (i < 0 || i >= static_cast<int>(g_entries.size()))
    return t;
  const Entry &e = g_entries[i];
  t.bAnnouncement = e.kind == "announcement";
  t.tag = TagOf(e);
  t.age = Age(e.date);
  t.pszTitle = e.title.c_str();
  t.pszLine = e.line.c_str();
  return t;
}

std::string NotesVersionLine(void)
{
  std::string s = "Hyperion v" HYPERION_FILE_VERSION_STR;
  if (HYPERION_BUILD_NUMBER > 0)
    // {0} the build's number
    s += std::string(" \xC2\xB7 ") + PortTextF("build {0}", {std::to_string(HYPERION_BUILD_NUMBER)});
  if (HYPERION_COMMIT[0] != 0)
    s += std::string(" \xC2\xB7 ") + HYPERION_COMMIT;
  return s;
}

void NotesOpen(int nEntry)
{
  Load();
  if (g_entries.empty())
    return;
  g_bUp = true;
  g_nPick = -1;
  Pick(nEntry < 0 ? 0 : nEntry);
  g_fListScroll = 0.0f;
  // the list shows the entry picked
  if (g_nPick > 6)
    g_fListScroll = Sc(100.0f * (g_nPick - 6));
}

void NotesClose(void)
{
  g_bUp = false;
  PlayerStop();
}

bool NotesUp(void)
{
  return g_bUp;
}

// The keys: Escape or C closes, Up and Down go between the entries, Left and Right between an
// entry's media.
void NotesKeys(void)
{
  if (Key(VK_ESCAPE) || Key('C'))
    NotesClose();
  else if (Key(VK_DOWN))
    Pick(g_nPick + 1);
  else if (Key(VK_UP))
    Pick(g_nPick - 1);
  else if (Key(VK_RIGHT))
    MediaStepBy(1);
  else if (Key(VK_LEFT))
    MediaStepBy(-1);
}

// Patch Notes: in the Settings dialog's frame, the entries down the left, newest first, and
// the one picked in the well, scrolled by the wheel.
void NotesDialog(void)
{
  if (!g_bUp || g_entries.empty())
    return;
  if (g_nMedia >= static_cast<int>(g_entries[g_nPick].media.size()))
    g_nMedia = 0;
  PlayerStep(g_nPick, g_entries[g_nPick].media.empty() ? -1 : g_nMedia);

  Decl black;
  black.d.id = Id("ntblack");
  black.d.layout.sizing.width = Fixed(static_cast<float>(UiWidth()));
  black.d.layout.sizing.height = Fixed(static_cast<float>(UiHeight()));
  black.d.backgroundColor = Col(kScBlackout);
  black.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  black.d.floating.zIndex = 60;
  Open(black);
  Close();

  Decl dlg;
  dlg.d.id = Id("ntdlg");
  dlg.d.layout.sizing.width = Fixed(Sc(1400));
  dlg.d.layout.sizing.height = Fixed(Sc(1170));
  dlg.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  dlg.d.floating.attachPoints.element = CLAY_ATTACH_POINT_CENTER_TOP;
  dlg.d.floating.attachPoints.parent = CLAY_ATTACH_POINT_CENTER_TOP;
  dlg.d.floating.offset.y = Sc(15);
  dlg.d.floating.zIndex = 61;
  static UiCustom frame = {0.0f, DrawScFrame, const_cast<UiColor *>(&kScFillSolid)};
  dlg.d.custom.customData = &frame;
  Open(dlg);

  Decl title = Floating(NULL, 0, 0, Sc(35), 62, CLAY_ATTACH_POINT_CENTER_TOP, CLAY_ATTACH_POINT_CENTER_TOP);
  Open(title);
  Text(PortText("PATCH NOTES"), UI_FACE_WIDE, Fs(24), 0.04f, kScInkHi);
  Close();

  // the entries: one raised row each, its version or Announcement, its title and date
  Decl list = Floating("ntlist", 0, Sc(35), Sc(78), 62, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  list.d.layout.sizing.width = Fixed(Sc(341));
  list.d.layout.sizing.height = Fixed(Sc(905));
  list.d.clip.vertical = true;
  g_fListScroll += WheelPixels(Over(list.d.id), Sc(100));
  g_fListScroll = SDL_clamp(g_fListScroll, 0.0f, g_fListMax);
  list.d.clip.childOffset.y = -g_fListScroll;
  Open(list);
  Decl rows;
  rows.d.id = Id("ntrows");
  rows.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  rows.d.layout.sizing.width = Grow();
  rows.d.layout.padding = Pad(0, 0, Fs(9), 0);
  rows.d.layout.childGap = Gap(Fs(10));
  Open(rows);
  for (int i = 0; i < static_cast<int>(g_entries.size()); ++i) {
    const Entry &e = g_entries[i];
    Decl b;
    b.d.id = Id("ntrow", i);
    const bool bHere = i == g_nPick;
    const bool bHot = Over(b.d.id);
    if (Clicked(b.d.id))
      Pick(i);
    b.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    b.d.layout.sizing.width = Grow();
    b.d.layout.sizing.height = Fixed(Sc(90));
    b.d.layout.padding = Pad(Fs(24), Fs(20), 0, 0);
    b.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    b.d.layout.childGap = Gap(Fs(3));
    b.d.custom.customData = Custom(DrawScRaised, bHere ? kScTabOn : bHot ? kScTabHi : kScTab);
    Open(b);
    Text(Keep(e.kind == "announcement" ? std::string(PortText("Announcement")) : "v" + e.version), UI_FACE_WIDE, Fs(16),
         0.02f, bHere || bHot ? kScInkHi : kScInkButton);
    Text(Keep(e.title), UI_FACE_TEXT, Fs(15), 0.0f, bHere ? kScInkAccept : kScInkMenu);
    Text(Keep(Age(e.date)), UI_FACE_TEXT, Fs(13), 0.04f, kScInkOff);
    Close();
  }
  Close();
  Close();

  // the version this is, under the list
  Decl ver = Floating(NULL, 0, Sc(40), Sc(1000), 62, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  ver.d.layout.sizing.width = Fixed(Sc(330));
  ver.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  ver.d.layout.childGap = Gap(Fs(4));
  Open(ver);
  Text(PortText("YOU HAVE"), UI_FACE_TEXT, Fs(13), 0.14f, kScInkOff);
  Text(Keep(NotesVersionLine()), UI_FACE_TEXT, Fs(15), 0.0f, kScInkMenu, -1, 1.35f);
  Close();

  // the entry's page
  Decl well = Floating("ntwell", 0, Sc(386), Sc(78), 62, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  well.d.layout.sizing.width = Fixed(Sc(975));
  well.d.layout.sizing.height = Fixed(Sc(975));
  well.d.backgroundColor = Col(kScWell);
  well.d.border = Border(kScWellRim, Fs(2), Fs(2), Fs(2), Fs(2));
  Open(well);
  Decl body;
  body.d.id = Id("ntbody");
  body.d.layout.sizing.width = Grow();
  body.d.layout.sizing.height = Grow();
  body.d.clip.vertical = true;
  g_fScroll += WheelPixels(Over(body.d.id), Sc(80));
  g_fScroll = SDL_clamp(g_fScroll, 0.0f, g_fScrollMax);
  body.d.clip.childOffset.y = -g_fScroll;
  Open(body);
  Decl inner;
  inner.d.id = Id("ntinner");
  inner.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  inner.d.layout.sizing.width = Grow();
  inner.d.layout.padding = Pad(Fs(40), Fs(40), Fs(34), 0);
  inner.d.layout.childGap = Gap(Fs(8));
  Open(inner);
  Page(g_entries[g_nPick]);
  Close();
  Close();
  Close();

  Decl close = Floating("ntclose", 0, Sc(542), Sc(1064), 62, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  close.d.layout.sizing.width = Fixed(Sc(315));
  close.d.layout.sizing.height = Fixed(Sc(52));
  const bool bClose = ScButton(close, PortText("Close"), 'C', false);
  Close();

  // how far the page and the list scroll, from this frame's layout for the next
  const Clay_ElementData in = Clay_GetElementData(Id("ntinner"));
  const Clay_ElementData out = Clay_GetElementData(Id("ntbody"));
  g_fScrollMax = in.found && out.found && in.boundingBox.height > out.boundingBox.height
                     ? in.boundingBox.height - out.boundingBox.height
                     : 0.0f;
  const Clay_ElementData rin = Clay_GetElementData(Id("ntrows"));
  const Clay_ElementData rout = Clay_GetElementData(Id("ntlist"));
  g_fListMax = rin.found && rout.found && rin.boundingBox.height > rout.boundingBox.height
                   ? rin.boundingBox.height - rout.boundingBox.height
                   : 0.0f;
  if (bClose)
    NotesClose();
}

}  // namespace FrontKit
