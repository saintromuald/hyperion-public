// port_modsheet.cpp -- a mod's art sheet as classic's frames (port_modsheet.h). The rules are
// tools/hxunit.py's, which made a mod's GRPs from the SC2 sprite tool's sheets before Hyperion
// read sheets itself: the colours, the team pixels, the canvas and the frames' encoding.
#include "port_modsheet.h"

#include <math.h>
#include <string.h>
#include <algorithm>
#include <unordered_map>

#include "../../h/platform.h"
#include "../../h/grp.h"    // GROUP, FRAME
#include "../../h/Storm.h"  // SFileLoadFile, SMemAlloc
#include "port_stormapi.h"  // SFileUnloadFile

namespace {

const int kColourFirst = 16, kColourEnd = 200;  // the indexes every tileset's palette has alike
const int kTeamFirst = 8;                        // player 1's colour, 8 steps, in a unit's frames
const int kShadowIndex = 1;
const int kOpaque = 128;

// Classic's colours for a sheet: the badlands palette, whose indexes 16 to 199 every tileset's
// shares, and player 1's colour's 8 steps (game\tunit.pcx's first 8 pixels, indexes of it).
struct Colours {
  bool bRead = false;
  unsigned char pal[256][3] = {};
  unsigned char ramp[8][3] = {};
};

bool LoadFile(const char *pszName, std::vector<unsigned char> *pOut) {
  void *p = NULL;
  DWORD cb = 0;
  if (!SFileLoadFile(pszName, &p, &cb, 0, NULL) || p == NULL)
    return false;
  pOut->assign(static_cast<unsigned char *>(p), static_cast<unsigned char *>(p) + cb);
  SFileUnloadFile(p);
  return true;
}

const Colours *ColoursRead(std::string *pError) {
  static Colours c;
  if (c.bRead)
    return &c;
  std::vector<unsigned char> wpe, pcx;
  if (!LoadFile("tileset\\badlands.wpe", &wpe) || wpe.size() < 1024 || !LoadFile("game\\tunit.pcx", &pcx) ||
      pcx.size() < 129) {
    *pError = "cannot be made into the game's colours: tileset\\badlands.wpe or game\\tunit.pcx does not read";
    return nullptr;
  }
  for (int i = 0; i < 256; ++i)
    memcpy(c.pal[i], &wpe[4 * i], 3);
  // the PCX's first row: a byte of 0xC0 or more repeats the next (its low 6 bits) times
  size_t at = 128;
  for (int n = 0; n < 8 && at < pcx.size();) {
    int nRun = 1, v = pcx[at++];
    if ((v & 0xC0) == 0xC0 && at < pcx.size())
      nRun = v & 0x3F, v = pcx[at++];
    for (; nRun > 0 && n < 8; --nRun, ++n)
      memcpy(c.ramp[n], c.pal[v], 3);
  }
  c.bRead = true;
  return &c;
}

// The nearest of colours to (r, g, b), weighted for the eye as hxunit's are.
int Nearest(const unsigned char (*pColours)[3], int nColours, int r, int g, int b) {
  int nBest = 0, dBest = 0x7FFFFFFF;
  for (int i = 0; i < nColours; ++i) {
    const int dr = r - pColours[i][0], dg = g - pColours[i][1], db = b - pColours[i][2];
    const int d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
    if (d < dBest)
      dBest = d, nBest = i;
  }
  return nBest;
}

// A row of palette indexes, 0 clear, as classic encodes it: 0x80 | n clear, 0x40 | n of one
// index (three or more), n indexes as they are.
void EncodeRow(const unsigned char *pRow, int n, std::vector<unsigned char> *pOut) {
  std::vector<unsigned char> &d = *pOut;
  for (int i = 0; i < n;) {
    int j = i;
    if (pRow[i] == 0) {
      while (j < n && pRow[j] == 0 && j - i < 127)
        ++j;
      d.push_back(static_cast<unsigned char>(0x80 | (j - i)));
    } else {
      while (j < n && pRow[j] == pRow[i] && j - i < 63)
        ++j;
      if (j - i >= 3) {
        d.push_back(static_cast<unsigned char>(0x40 | (j - i)));
        d.push_back(pRow[i]);
      } else {
        j = i;
        while (j < n && pRow[j] != 0 && j - i < 63 && !(j + 2 < n && pRow[j] == pRow[j + 1] && pRow[j] == pRow[j + 2]))
          ++j;
        if (j == i)  // a run of three starts here after all
          ++j;
        d.push_back(static_cast<unsigned char>(j - i));
        d.insert(d.end(), pRow + i, pRow + j);
      }
    }
    i = j;
  }
}

}  // namespace

bool ModSheetCheck(const ModSheet &s, int *pnCells, std::string *pError) {
  if (s.levels.empty() || s.nFrameW < 1 || s.nFrameH < 1) {
    *pError = "has no picture, or no cell size";
    return false;
  }
  int nAcross = -1, nDown = -1;
  for (const ModSheetLevel &l : s.levels) {
    const int cw = s.nFrameW * l.nScale, ch = s.nFrameH * l.nScale;
    if (l.w <= 0 || l.h <= 0 || l.w % cw != 0 || l.h % ch != 0) {
      *pError = "at " + std::to_string(l.nScale) + "x is " + std::to_string(l.w) + " x " + std::to_string(l.h) +
                ", not a whole number of its " + std::to_string(cw) + " x " + std::to_string(ch) + " cells";
      return false;
    }
    if (nAcross >= 0 && (l.w / cw != nAcross || l.h / ch != nDown)) {
      *pError = "has another number of cells at " + std::to_string(l.nScale) + "x";
      return false;
    }
    nAcross = l.w / cw, nDown = l.h / ch;
  }
  *pnCells = nAcross * nDown;
  if (s.nFrames > *pnCells) {
    *pError = "has " + std::to_string(*pnCells) + " cells, fewer than its " + std::to_string(s.nFrames) + " frames";
    return false;
  }
  return true;
}

std::vector<unsigned char> ModSheetScaledDown(const std::vector<unsigned char> &rgba, int w, int h, int s) {
  const int ow = w / s, oh = h / s;
  std::vector<unsigned char> out(static_cast<size_t>(ow) * oh * 4, 0);
  for (int y = 0; y < oh; ++y)
    for (int x = 0; x < ow; ++x) {
      unsigned a = 0, r = 0, g = 0, b = 0;
      for (int v = 0; v < s; ++v)
        for (int u = 0; u < s; ++u) {
          const unsigned char *p = &rgba[(static_cast<size_t>(y * s + v) * w + x * s + u) * 4];
          a += p[3], r += p[0] * p[3], g += p[1] * p[3], b += p[2] * p[3];
        }
      unsigned char *q = &out[(static_cast<size_t>(y) * ow + x) * 4];
      if (a > 0)
        q[0] = static_cast<unsigned char>(r / a), q[1] = static_cast<unsigned char>(g / a),
        q[2] = static_cast<unsigned char>(b / a);
      q[3] = static_cast<unsigned char>(a / (s * s));
    }
  return out;
}

int ModSheetTeamBright(void) {
  std::string error;
  const Colours *p = ColoursRead(&error);
  int nBright = 0;
  for (int i = 0; p != nullptr && i < 8; ++i)
    nBright = (std::max)({nBright, static_cast<int>(p->ramp[i][0]), static_cast<int>(p->ramp[i][1]),
                          static_cast<int>(p->ramp[i][2])});
  return nBright > 0 ? nBright : 255;
}

bool ModSheetEncode(const ModSheet &s, const std::vector<unsigned char> &rgba, const std::vector<unsigned char> &team,
                    bool bShadow, ModSheetFrames *pOut, std::string *pError) {
  int nCells = 0;
  if (!ModSheetCheck(s, &nCells, pError))
    return false;
  const Colours *pColours = bShadow ? nullptr : ColoursRead(pError);
  if (!bShadow && !pColours)
    return false;
  const int lw = s.levels[0].w / s.levels[0].nScale, lh = s.levels[0].h / s.levels[0].nScale;
  if (rgba.size() != static_cast<size_t>(lw) * lh * 4 || (!team.empty() && team.size() != rgba.size())) {
    *pError = "is not the size its level gives";
    return false;
  }
  const int fw = s.nFrameW, fh = s.nFrameH, nAcross = lw / fw, nFrames = s.nFrames ? s.nFrames : nCells;
  auto Px = [&](const std::vector<unsigned char> &v, int f, int x, int y) {
    return &v[(static_cast<size_t>((f / nAcross) * fh + y) * lw + (f % nAcross) * fw + x) * 4];
  };
  auto Drawn = [&](int f, int x, int y) { return bShadow ? Px(rgba, f, x, y)[3] > 0 : Px(rgba, f, x, y)[3] >= kOpaque; };

  // the canvas, centred on the origin, that holds every frame's pixels, and where a cell's
  // corner falls on it
  double hw = 1, hh = 1;
  for (int f = 0; f < nFrames; ++f)
    for (int y = 0; y < fh; ++y)
      for (int x = 0; x < fw; ++x)
        if (Drawn(f, x, y)) {
          hw = (std::max)(hw, (std::max)(s.fOriginX - x, x + 1 - s.fOriginX));
          hh = (std::max)(hh, (std::max)(s.fOriginY - y, y + 1 - s.fOriginY));
        }
  const int w = 2 * static_cast<int>(ceil(hw)), h = 2 * static_cast<int>(ceil(hh));
  const int dx = static_cast<int>(floor(w / 2.0 - s.fOriginX + 0.5)), dy = static_cast<int>(floor(h / 2.0 - s.fOriginY + 0.5));
  if (w > 0xFFFF || h > 0xFFFF) {
    *pError = "makes a canvas of " + std::to_string(w) + " x " + std::to_string(h) + ", past 65,535 pixels a side";
    return false;
  }

  std::unordered_map<unsigned, unsigned char> nearest, nearestTeam;
  pOut->w = w, pOut->h = h;
  pOut->dx = dx, pOut->dy = dy;
  pOut->frames.assign(nFrames, ModSheetFrame());
  std::vector<unsigned char> idx;
  for (int f = 0; f < nFrames; ++f) {
    // the frame's indexes on the canvas, then cropped to its pixels
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    idx.assign(static_cast<size_t>(w) * h, 0);
    for (int y = 0; y < fh; ++y)
      for (int x = 0; x < fw; ++x) {
        const int cx = x + dx, cy = y + dy;
        if (cx < 0 || cy < 0 || cx >= w || cy >= h || !Drawn(f, x, y))
          continue;
        const unsigned char *c = Px(rgba, f, x, y);
        int v = kShadowIndex;
        if (!bShadow) {
          const unsigned char *t = team.empty() ? nullptr : Px(team, f, x, y);
          const bool bTeam = t && t[3] >= kOpaque && (std::max)({t[0], t[1], t[2]}) >= 128;
          const unsigned key = c[0] | c[1] << 8 | c[2] << 16;
          auto &cache = bTeam ? nearestTeam : nearest;
          auto it = cache.find(key);
          if (it == cache.end())
            it = cache.emplace(key, static_cast<unsigned char>(
                                        bTeam ? kTeamFirst + Nearest(pColours->ramp, 8, c[0], c[1], c[2])
                                              : kColourFirst + Nearest(pColours->pal + kColourFirst,
                                                                       kColourEnd - kColourFirst, c[0], c[1], c[2])))
                     .first;
          v = it->second;
        }
        idx[static_cast<size_t>(cy) * w + cx] = static_cast<unsigned char>(v);
        x0 = (std::min)(x0, cx), y0 = (std::min)(y0, cy), x1 = (std::max)(x1, cx), y1 = (std::max)(y1, cy);
      }
    if (x1 < 0)  // a frame with no pixels: one clear pixel in the corner
      x0 = y0 = x1 = y1 = 0;
    ModSheetFrame &o = pOut->frames[f];
    o.x = x0, o.y = y0, o.w = x1 - x0 + 1, o.h = y1 - y0 + 1;
    o.rows.resize(o.h);
    for (int y = 0; y < o.h; ++y) {
      o.rows[y] = static_cast<unsigned>(o.codes.size());
      EncodeRow(&idx[static_cast<size_t>(y0 + y) * w + x0], o.w, &o.codes);
    }
  }
  return true;
}

bool ModSheetFile(const ModSheetFrames &fr, std::vector<unsigned char> *pFile) {
  std::vector<unsigned char> &out = *pFile;
  const size_t n = fr.frames.size();
  out.assign(6 + 8 * n, 0);
  const unsigned short head[3] = {static_cast<unsigned short>(n), static_cast<unsigned short>(fr.w),
                                  static_cast<unsigned short>(fr.h)};
  memcpy(out.data(), head, 6);
  for (size_t f = 0; f < n; ++f) {
    const ModSheetFrame &o = fr.frames[f];
    if (o.w > 255 || o.h > 255)
      return false;
    unsigned char *pHead = &out[6 + 8 * f];
    pHead[0] = static_cast<unsigned char>(o.x), pHead[1] = static_cast<unsigned char>(o.y);
    pHead[2] = static_cast<unsigned char>(o.w), pHead[3] = static_cast<unsigned char>(o.h);
    const DWORD dwAt = static_cast<DWORD>(out.size());
    memcpy(pHead + 4, &dwAt, 4);
    for (int y = 0; y < o.h; ++y) {
      const size_t row = 2 * static_cast<size_t>(o.h) + o.rows[y];
      if (row > 0xFFFF)
        return false;
      const unsigned short wRow = static_cast<unsigned short>(row);
      out.insert(out.end(), reinterpret_cast<const unsigned char *>(&wRow),
                 reinterpret_cast<const unsigned char *>(&wRow) + 2);
    }
    out.insert(out.end(), o.codes.begin(), o.codes.end());
  }
  return true;
}

GROUP *ModSheetGroupNative(const ModSheetFrames &fr) {
#if !defined(_WIN64) && !defined(__LP64__)
  // the tools that compile the engine at classic's widths: the file, as PortGroupNative keeps it
  std::vector<unsigned char> file;
  if (!ModSheetFile(fr, &file))
    return NULL;
  void *p = SMemAlloc(static_cast<DWORD>(file.size()), "port_modsheet.cpp", __LINE__, 0);
  if (p != NULL)
    memcpy(p, file.data(), file.size());
  return static_cast<GROUP *>(p);
#else
  const size_t n = fr.frames.size(), table = offsetof(GROUP, frames) + n * sizeof(FRAME);
  size_t size = table;
  for (const ModSheetFrame &o : fr.frames)
    size += 4 * o.rows.size() + o.codes.size();
  BYTE *block = static_cast<BYTE *>(SMemAlloc(static_cast<DWORD>(size), "port_modsheet.cpp", __LINE__, 0));
  if (block == NULL)
    return NULL;
  memset(block, 0, table);
  GROUP *pGroup = reinterpret_cast<GROUP *>(block);
  const WORD wCount = static_cast<WORD>(n);
  memcpy(pGroup->_unmapped_00, &wCount, sizeof(wCount));
  pGroup->wMaxWidth = static_cast<WORD>(fr.w);
  pGroup->wMaxHeight = static_cast<WORD>(fr.h);
  // after the table, each frame's DWORD line table and then its codes, which pData points at
  size_t at = table;
  for (size_t f = 0; f < n; ++f) {
    const ModSheetFrame &o = fr.frames[f];
    FRAME *pFrame = &pGroup->frames[f];
    pFrame->bOffsetX = static_cast<frame_dim_t>(o.x);
    pFrame->_unmapped_01 = static_cast<frame_dim_t>(o.y);
    pFrame->bWidth = static_cast<frame_dim_t>(o.w);
    pFrame->bHeight = static_cast<frame_dim_t>(o.h);
    pFrame->dwData = 0;
    DWORD *pLines = reinterpret_cast<DWORD *>(block + at);
    for (size_t y = 0; y < o.rows.size(); ++y)
      pLines[y] = o.rows[y];
    pFrame->pLines = pLines;
    at += 4 * o.rows.size();
    pFrame->pData = block + at;
    memcpy(block + at, o.codes.data(), o.codes.size());
    at += o.codes.size();
  }
  return pGroup;
#endif
}
