// port_modart.cpp -- a mod's art sheets at their own resolution (port_modart.h). Each sheet's
// level nearest the HD frame's scale is read on a thread of its own, its cells made that
// scale, cut to their pixels and packed into one picture, its team layer beside it. Until
// that is done the image is drawn from its classic frames.
#include "port_modart.h"

#include <string.h>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "../data/port_imagefile.h"  // PortImageDecode
#include "../data/port_modsheet.h"   // ModSheetScaledDown
#include "../platform/port_log.h"    // PortLogger
#include "../platform/port_util.h"   // PortReadFile

namespace {

enum { STATE_NOT_ASKED, STATE_LOADING, STATE_READY, STATE_FAILED };

struct Entry {
  PortModArtSheet sheet;
  int nState = STATE_NOT_ASKED;
  std::unique_ptr<PortHdArt> art;
  unsigned nToken = 0;  // this setting's, so the loader's art for a sheet set again is dropped
};

std::mutex g_lock;
std::condition_variable g_wake;
std::deque<std::pair<int, unsigned>> g_queue;  // an image, and its entry's token
std::map<int, Entry> g_entries;
std::vector<std::unique_ptr<PortHdArt>> g_retired;  // replaced while a game was drawn
std::shared_ptr<const PortModUi> g_ui;              // the console's icons and wireframes
bool g_bGame;
bool g_bStarted;
unsigned g_nGeneration = 1;
unsigned g_nToken;
unsigned g_nSerial;

const PortLogger Log("hyperion_modart.log");

const int kTextureMost = 16384;            // a side of the picture the frames are packed into
const size_t kPixelsMost = 64u << 20;      // its pixels
const size_t kFileMost = 512u << 20;       // a sheet's file

typedef std::vector<unsigned char> Bytes;

int Gcd(int a, int b) {
  return b == 0 ? a : Gcd(b, a % b);
}

// An RGBA picture w by h at scale k made scale s: up by s / gcd, nearest, then down by
// k / gcd, each pixel the average of those it covers (ModSheetScaledDown).
Bytes Rescaled(const Bytes &src, int w, int h, int k, int s) {
  if (k == s)
    return src;
  const int g = Gcd(k, s), up = s / g, down = k / g;
  Bytes out = src;
  if (up > 1) {
    Bytes big(static_cast<size_t>(w) * up * h * up * 4);
    for (int y = 0; y < h * up; ++y)
      for (int x = 0; x < w * up; ++x)
        memcpy(&big[(static_cast<size_t>(y) * w * up + x) * 4], &src[(static_cast<size_t>(y / up) * w + x / up) * 4], 4);
    out.swap(big);
    w *= up, h *= up;
  }
  return down > 1 ? ModSheetScaledDown(out, w, h, down) : out;
}

// A cell at the HD scale, cut to its pixels: its place in the cell, its size, its colours and,
// with a team layer, its team's share.
struct Cell {
  int x = 0, y = 0, w = 0, h = 0;
  Bytes rgba, team;
};

// A team layer pixel's share: its brightest channel over its alpha.
int TeamShare(const unsigned char *t) {
  return (std::max)({t[0], t[1], t[2]}) * t[3] / 255;
}

// The hue the team pixels were drawn in, its brightest channel 1: the average of the pixels
// wholly the team's, else the red the sheet tools draw in.
void TeamHue(const std::vector<Cell> &cells, float *pHue) {
  double sum[3] = {0, 0, 0};
  size_t n = 0;
  for (const Cell &c : cells)
    for (size_t i = 0; i * 4 < c.team.size(); ++i) {
      const unsigned char *p = &c.rgba[i * 4];
      const int hi = (std::max)({p[0], p[1], p[2]});
      if (TeamShare(&c.team[i * 4]) < 230 || hi < 32)
        continue;
      for (int k = 0; k < 3; ++k)
        sum[k] += static_cast<double>(p[k]) / hi;
      ++n;
    }
  const float red[3] = {1.0f, 48.0f / 208, 48.0f / 208};
  for (int k = 0; k < 3; ++k)
    pHue[k] = n > 0 ? static_cast<float>(sum[k] / n) : red[k];
  const float most = (std::max)({pHue[0], pHue[1], pHue[2]});
  for (int k = 0; k < 3; ++k)
    pHue[k] /= most;
}

// A cell's colours where its team layer has a share m: the team colour's part taken out and
// grey of its brightness put in its place, which the player's colour multiplies by m (hd.hlsl),
// as classic gives a team pixel its step of the player's colour. That part is told from the
// rest by the hue's brightest channel over its dullest, the rest taken to be grey.
void TeamOf(Bytes *pRgba, const Bytes &team, Bytes *pMask, const float *pHue, int nBright) {
  int hi = 0, lo = 0;
  for (int k = 1; k < 3; ++k) {
    hi = pHue[k] > pHue[hi] ? k : hi;
    lo = pHue[k] < pHue[lo] ? k : lo;
  }
  const float spread = pHue[hi] - pHue[lo];
  const size_t n = pRgba->size() / 4;
  pMask->assign(n, 0);
  for (size_t i = 0; i < n; ++i) {
    const int m = TeamShare(&team[i * 4]);
    if (m == 0)
      continue;
    unsigned char *c = &(*pRgba)[i * 4];
    const float fm = m / 255.0f;
    float rgb[3] = {c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f};
    // the team colour's brightness in the pixel
    float a = spread > 0.05f ? (rgb[hi] - rgb[lo]) / (fm * spread) : rgb[hi] / fm;
    a = (std::max)(0.0f, (std::min)(a, rgb[hi] / fm));
    const float grey = (std::min)(1.0f, a * 255.0f / (std::max)(nBright, 1));
    for (int k = 0; k < 3; ++k) {
      const float v = rgb[k] - fm * a * pHue[k] + fm * grey;
      c[k] = static_cast<unsigned char>((std::max)(0.0f, (std::min)(1.0f, v)) * 255.0f + 0.5f);
    }
    (*pMask)[i] = static_cast<unsigned char>(m);
  }
}

std::unique_ptr<PortHdArt> Make(const PortModArtSheet &s, int nImage, std::string *pError) {
  const int S = PortHdScale();
  // the smallest level at the HD scale or above, else the largest
  const PortModArtSheet::Level *pLevel = &s.levels.back();
  for (const PortModArtSheet::Level &l : s.levels)
    if (l.nScale >= S) {
      pLevel = &l;
      break;
    }
  const int k = pLevel->nScale, fw = s.nFrameW * k, fh = s.nFrameH * k;
  const int cw = s.nFrameW * S, ch = s.nFrameH * S;
  const int canvasW = s.nCanvasW * S, canvasH = s.nCanvasH * S;
  if (canvasW > 32767 || canvasH > 32767) {
    *pError = "its canvas is past 32,767 pixels a side at " + std::to_string(S) + "x";
    return nullptr;
  }
  // where a cell's pixels may be: on the classic canvas
  const int nLeft = (std::max)(0, -s.nCellX * S), nTop = (std::max)(0, -s.nCellY * S);
  const int nRight = (std::min)(cw, canvasW - s.nCellX * S), nBottom = (std::min)(ch, canvasH - s.nCellY * S);
  const bool bTeam = !s.bShadow && !pLevel->team.empty();
  std::vector<Cell> cells;
  for (size_t p = 0; p < pLevel->parts.size() && static_cast<int>(cells.size()) < s.nFrames; ++p) {
    Bytes file, rgba, team;
    int w = 0, h = 0, tw = 0, th = 0;
    std::string error;
    if (!PortReadFile(s.folder + pLevel->parts[p], &file, kFileMost) ||
        !PortImageDecode(file.data(), file.size(), pLevel->parts[p], &rgba, &w, &h, &error)) {
      *pError = pLevel->parts[p] + (error.empty() ? " does not read" : " " + error);
      return nullptr;
    }
    if (bTeam && (!PortReadFile(s.folder + pLevel->team[p], &file, kFileMost) ||
                  !PortImageDecode(file.data(), file.size(), pLevel->team[p], &team, &tw, &th, &error) || tw != w ||
                  th != h)) {
      *pError = pLevel->team[p] + (error.empty() ? " does not read, or is not its part's size" : " " + error);
      return nullptr;
    }
    const int nAcross = w / fw, nDown = h / fh;
    Bytes one(static_cast<size_t>(fw) * fh * 4), oneTeam;
    for (int r = 0; r < nDown; ++r)
      for (int c = 0; c < nAcross && static_cast<int>(cells.size()) < s.nFrames; ++c) {
        for (int y = 0; y < fh; ++y)
          memcpy(&one[static_cast<size_t>(y) * fw * 4], &rgba[(static_cast<size_t>(r * fh + y) * w + c * fw) * 4],
                 static_cast<size_t>(fw) * 4);
        Bytes px = Rescaled(one, fw, fh, k, S), pxTeam;
        if (bTeam) {
          oneTeam.resize(one.size());
          for (int y = 0; y < fh; ++y)
            memcpy(&oneTeam[static_cast<size_t>(y) * fw * 4],
                   &team[(static_cast<size_t>(r * fh + y) * w + c * fw) * 4], static_cast<size_t>(fw) * 4);
          pxTeam = Rescaled(oneTeam, fw, fh, k, S);
        }
        // cut to the pixels drawn, on the canvas
        int x0 = nRight, y0 = nBottom, x1 = nLeft - 1, y1 = nTop - 1;
        for (int y = nTop; y < nBottom; ++y)
          for (int x = nLeft; x < nRight; ++x)
            if (px[(static_cast<size_t>(y) * cw + x) * 4 + 3] != 0)
              x0 = (std::min)(x0, x), x1 = (std::max)(x1, x), y0 = (std::min)(y0, y), y1 = (std::max)(y1, y);
        Cell cell;
        if (x1 >= x0) {
          cell.x = x0, cell.y = y0, cell.w = x1 - x0 + 1, cell.h = y1 - y0 + 1;
          cell.rgba.resize(static_cast<size_t>(cell.w) * cell.h * 4);
          if (bTeam)
            cell.team.resize(cell.rgba.size());
          for (int y = 0; y < cell.h; ++y) {
            const size_t from = (static_cast<size_t>(y0 + y) * cw + x0) * 4, to = static_cast<size_t>(y) * cell.w * 4;
            memcpy(&cell.rgba[to], &px[from], static_cast<size_t>(cell.w) * 4);
            if (bTeam)
              memcpy(&cell.team[to], &pxTeam[from], static_cast<size_t>(cell.w) * 4);
          }
        }
        cells.push_back(std::move(cell));
      }
  }
  if (static_cast<int>(cells.size()) < s.nFrames) {
    *pError = "has " + std::to_string(cells.size()) + " cells at " + std::to_string(k) + "x, not " +
              std::to_string(s.nFrames);
    return nullptr;
  }
  // packed in rows, a clear pixel round each so a sample at an edge takes nothing of the next
  int nWidest = 0;
  for (const Cell &c : cells)
    nWidest = (std::max)(nWidest, c.w + 2);
  int W = (std::max)(4096, (nWidest + 3) & ~3), H = 0;
  std::vector<std::pair<int, int>> at(cells.size());
  for (;;) {
    int x = 0, y = 0, nRow = 0;
    for (size_t i = 0; i < cells.size(); ++i) {
      if (x + cells[i].w + 2 > W)
        x = 0, y += nRow, nRow = 0;
      at[i] = {x + 1, y + 1};
      x += cells[i].w + 2;
      nRow = (std::max)(nRow, cells[i].h + 2);
    }
    H = (y + nRow + 3) & ~3;
    if (H <= kTextureMost || W >= kTextureMost)
      break;
    W = (std::min)(kTextureMost, W * 2);
  }
  if (W > kTextureMost || H > kTextureMost || static_cast<size_t>(W) * H > kPixelsMost) {
    *pError = "makes a picture of " + std::to_string(W) + " x " + std::to_string(H) + " at " + std::to_string(S) +
              "x, past what one texture takes";
    return nullptr;
  }
  std::unique_ptr<PortHdArt> art(new PortHdArt());
  art->nImage = nImage;
  art->nScale = S;
  art->nCanvasW = canvasW;
  art->nCanvasH = canvasH;
  PortHdLayer &diffuse = art->layers[PORT_HD_DIFFUSE];
  diffuse.nBc = PORT_HD_RGBA8;
  diffuse.nWidth = W, diffuse.nHeight = H;
  diffuse.blocks.assign(static_cast<size_t>(W) * H * 4, 0);
  PortHdLayer &mask = art->layers[PORT_HD_TEAM];
  if (bTeam) {
    mask.nBc = PORT_HD_R8;
    mask.nWidth = W, mask.nHeight = H;
    mask.blocks.assign(static_cast<size_t>(W) * H, 0);
  }
  float hue[3] = {1.0f, 1.0f, 1.0f};
  if (bTeam)
    TeamHue(cells, hue);
  Bytes cellMask;
  for (size_t i = 0; i < cells.size(); ++i) {
    Cell &c = cells[i];
    PortHdFrame f;
    f.nX = static_cast<unsigned short>(at[i].first);
    f.nY = static_cast<unsigned short>(at[i].second);
    f.nOffsetX = static_cast<short>(c.x + s.nCellX * S);
    f.nOffsetY = static_cast<short>(c.y + s.nCellY * S);
    f.nWidth = static_cast<unsigned short>(c.w);
    f.nHeight = static_cast<unsigned short>(c.h);
    art->frames.push_back(f);
    if (bTeam)
      TeamOf(&c.rgba, c.team, &cellMask, hue, s.nTeamBright);
    for (int y = 0; y < c.h; ++y) {
      memcpy(&diffuse.blocks[(static_cast<size_t>(f.nY + y) * W + f.nX) * 4], &c.rgba[static_cast<size_t>(y) * c.w * 4],
             static_cast<size_t>(c.w) * 4);
      if (bTeam)
        memcpy(&mask.blocks[static_cast<size_t>(f.nY + y) * W + f.nX], &cellMask[static_cast<size_t>(y) * c.w],
               static_cast<size_t>(c.w));
    }
  }
  Log("modart: image %d at %dx from its %dx level: %d frames in %d x %d%s", nImage, S, k,
      static_cast<int>(cells.size()), W, H, bTeam ? ", with its team layer" : "");
  return art;
}

void Loader(void) {
  for (;;) {
    std::pair<int, unsigned> job;
    PortModArtSheet sheet;
    {
      std::unique_lock<std::mutex> hold(g_lock);
      g_wake.wait(hold, [] { return !g_queue.empty(); });
      job = g_queue.front();
      g_queue.pop_front();
      auto it = g_entries.find(job.first);
      if (it == g_entries.end() || it->second.nToken != job.second)
        continue;
      sheet = it->second.sheet;
    }
    std::string error;
    std::unique_ptr<PortHdArt> art = Make(sheet, job.first, &error);
    if (!art)
      Log("modart: image %d is drawn from its classic frames: its sheet %s", job.first, error);
    std::lock_guard<std::mutex> hold(g_lock);
    auto it = g_entries.find(job.first);
    if (it == g_entries.end() || it->second.nToken != job.second)
      continue;  // set again while it was made
    if (art)
      art->nSerial = ++g_nSerial;
    it->second.nState = art ? STATE_READY : STATE_FAILED;
    it->second.art = std::move(art);
  }
}

}  // namespace

void PortModArtSet(int nImage, const PortModArtSheet *pSheet) {
  std::lock_guard<std::mutex> hold(g_lock);
  auto it = g_entries.find(nImage);
  if (it != g_entries.end()) {
    if (it->second.art)
      g_retired.push_back(std::move(it->second.art));
    g_entries.erase(it);
  }
  if (pSheet == NULL || pSheet->levels.empty())
    return;
  Entry &e = g_entries[nImage];
  e.sheet = *pSheet;
  e.nToken = ++g_nToken;
}

void PortModArtGame(bool bOn) {
  std::lock_guard<std::mutex> hold(g_lock);
  g_bGame = bOn;
  ++g_nGeneration;
  g_retired.clear();
}

bool PortModArtWanted(void) {
  std::lock_guard<std::mutex> hold(g_lock);
  return g_bGame && (!g_entries.empty() || g_ui);
}

bool PortModArtHas(int nImage) {
  std::lock_guard<std::mutex> hold(g_lock);
  return g_bGame && g_entries.count(nImage) != 0;
}

const PortHdArt *PortModArtImage(int nImage) {
  std::lock_guard<std::mutex> hold(g_lock);
  auto it = g_entries.find(nImage);
  if (!g_bGame || it == g_entries.end())
    return NULL;
  Entry &e = it->second;
  if (e.nState == STATE_NOT_ASKED) {
    e.nState = STATE_LOADING;
    g_queue.push_back({nImage, e.nToken});
    if (!g_bStarted) {
      g_bStarted = true;
      std::thread(Loader).detach();
    }
    g_wake.notify_one();
  }
  return e.nState == STATE_READY ? e.art.get() : NULL;
}

unsigned PortModArtGeneration(void) {
  std::lock_guard<std::mutex> hold(g_lock);
  return g_nGeneration;
}

void PortModUiSet(const std::vector<PortModUiPicture> &pictures) {
  std::shared_ptr<PortModUi> ui;
  if (!pictures.empty()) {
    // packed in rows of 1,024, a clear pixel round each
    const int W = 1024;
    int x = 0, y = 0, nRow = 0;
    std::vector<std::pair<int, int>> at(pictures.size());
    for (size_t i = 0; i < pictures.size(); ++i) {
      if (x + pictures[i].nW + 2 > W)
        x = 0, y += nRow, nRow = 0;
      at[i] = {x + 1, y + 1};
      x += pictures[i].nW + 2;
      nRow = (std::max)(nRow, pictures[i].nH + 2);
    }
    const int H = (y + nRow + 3) & ~3;
    if (H > kTextureMost) {
      Log("modart: the mod's icons and wireframes make a picture %d high, past what one texture takes", H);
    } else {
      ui = std::make_shared<PortModUi>();
      ui->atlas.nBc = PORT_HD_RGBA8;
      ui->atlas.nWidth = W, ui->atlas.nHeight = H;
      ui->atlas.blocks.assign(static_cast<size_t>(W) * H * 4, 0);
      for (size_t i = 0; i < pictures.size(); ++i) {
        const PortModUiPicture &p = pictures[i];
        for (int r = 0; r < p.nH; ++r)
          memcpy(&ui->atlas.blocks[(static_cast<size_t>(at[i].second + r) * W + at[i].first) * 4],
                 &p.rgba[static_cast<size_t>(r) * p.nW * 4], static_cast<size_t>(p.nW) * 4);
        ui->frames[p.nKey] = {at[i].first, at[i].second, p.nW, p.nH};
      }
      Log("modart: %d icons and wireframes at %dx in %d x %d", static_cast<int>(pictures.size()), PortHdScale(), W,
          H);
    }
  }
  std::lock_guard<std::mutex> hold(g_lock);
  if (ui)
    ui->nSerial = ++g_nSerial;
  g_ui = ui;
}

std::shared_ptr<const PortModUi> PortModUiArt(void) {
  std::lock_guard<std::mutex> hold(g_lock);
  return g_bGame ? g_ui : nullptr;
}

bool PortModUiHas(int nKind, unsigned nFrame) {
  std::lock_guard<std::mutex> hold(g_lock);
  return g_bGame && g_ui && g_ui->frames.count(static_cast<unsigned>(nKind) << 16 | nFrame) != 0;
}
