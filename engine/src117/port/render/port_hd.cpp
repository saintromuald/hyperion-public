// port_hd.cpp -- the HD art port_hd.h describes: ANIM files read out of the install's CASC
// storage (port_casc.cpp) on a thread of their own and kept as their DDS blocks
// (docs/modules/render.md, "HD art").
//
// ANIM, its entries and frames, and DDS.VR4 (the HD terrain, a frame a megatile) are laid out
// in docs/context/graphics-formats.md, as tools/assets/scranim.py reads them. An SD entry of 0
// frames borrows the image its second u16 names; HD files keep frames at 4x whatever their
// scale, so a 2x file's are halved; of a layer's DDS, DXT1 or DXT5, the first level is taken.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <SDL3/SDL.h>
#include "../platform/port_log.h"  // PortLogger
#include "port_hd.h"
#include "../front/port_ui.h"  // PortCascAvailable, PortCascRead

namespace {

enum { STATE_NONE_ASKED, STATE_LOADING, STATE_READY, STATE_MISSING };

std::mutex g_lock;
std::condition_variable g_wake;
std::deque<int> g_queue;
// the images, then the tilesets, then the consoles
enum {
  FIRST_TILESET = PORT_HD_IMAGES,
  FIRST_CONSOLE = FIRST_TILESET + PORT_HD_TILESETS,
  FIRST_UI = FIRST_CONSOLE + PORT_HD_CONSOLES
};
int g_anState[FIRST_UI + PORT_HD_UI_SETS];
PortHdUi *g_apUi[PORT_HD_UI_SETS];
PortHdArt *g_apArt[PORT_HD_IMAGES];
PortHdTerrain *g_apTerrain[PORT_HD_TILESETS];
PortHdConsole *g_apConsole[PORT_HD_CONSOLES];
bool g_bStarted;
bool g_bAvailable;
std::vector<unsigned short> g_borrow;  // image -> the image whose file it uses (mainSD)

const PortLogger Log("hyperion_hd.log");

unsigned short U16(const unsigned char *p) { return static_cast<unsigned short>(p[0] | p[1] << 8); }
unsigned int U32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<unsigned int>(p[3]) << 24; }

// The first level of a DXT1 or DXT5 DDS file.
bool ReadDds(const unsigned char *p, size_t n, PortHdLayer *pLayer) {
  if (n < 128 || memcmp(p, "DDS ", 4) != 0)
    return false;
  const int h = static_cast<int>(U32(p + 12));
  const int w = static_cast<int>(U32(p + 16));
  int nBc = memcmp(p + 84, "DXT1", 4) == 0 ? 1 : memcmp(p + 84, "DXT5", 4) == 0 ? 3 : 0;
  if (nBc == 0 || w <= 0 || h <= 0)
    return false;
  const size_t nBytes = static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4) * (nBc == 1 ? 8 : 16);
  if (128 + nBytes > n)
    return false;
  pLayer->nBc = nBc;
  pLayer->nWidth = w;
  pLayer->nHeight = h;
  pLayer->blocks.assign(p + 128, p + 128 + nBytes);
  return true;
}

// SD/mainSD.anim's borrowing: which images use another's frames.
void ReadBorrowing(void) {
  g_borrow.resize(PORT_HD_IMAGES);
  for (int i = 0; i < PORT_HD_IMAGES; ++i)
    g_borrow[i] = static_cast<unsigned short>(i);
  size_t n = 0;
  unsigned char *p = static_cast<unsigned char *>(PortCascRead("SD/mainSD.anim", &n));
  if (p == NULL)
    return;
  if (n > 0x14C + 4 * PORT_HD_IMAGES && memcmp(p, "ANIM", 4) == 0) {
    int nBorrowing = 0;
    for (int i = 0; i < PORT_HD_IMAGES; ++i) {
      const unsigned int nAt = U32(p + 0x14C + 4 * i);
      if (nAt + 4 <= n && U16(p + nAt) == 0 && U16(p + nAt + 2) < PORT_HD_IMAGES) {
        g_borrow[i] = U16(p + nAt + 2);
        ++nBorrowing;
      }
    }
    Log("hd: %d images borrow another's frames", nBorrowing);
  }
  SDL_free(p);
}

PortHdArt *ReadImage(int nImage, int nScale) {
  // the image it borrows from, followed a few steps at most
  for (int nStep = 0; nStep < 4 && g_borrow[nImage] != nImage; ++nStep)
    nImage = g_borrow[nImage];
  char szName[64];
  SDL_snprintf(szName, sizeof szName, nScale == 4 ? "anim/main_%03d.anim" : "HD2/anim/main_%03d.anim", nImage);
  size_t n = 0;
  unsigned char *p = static_cast<unsigned char *>(PortCascRead(szName, &n));
  if (p == NULL)
    return NULL;
  PortHdArt *pArt = NULL;
  if (n >= 0x14C + 12 && memcmp(p, "ANIM", 4) == 0 && p[5] == 2) {
    const int nLayers = U16(p + 8);
    const unsigned char *e = p + 0x14C;
    const int nFrames = U16(e);
    const unsigned int nFrameTable = U32(e + 8);
    if (nFrames > 0 && 0x14C + 12 + 12 * nLayers <= n && nFrameTable + 16u * nFrames <= n) {
      pArt = new PortHdArt();
      pArt->nImage = nImage;
      pArt->nScale = p[4];
      const float k = pArt->nScale / 4.0f;  // frames are kept at 4x
      pArt->nCanvasW = static_cast<int>(U16(e + 4) * k + 0.5f);
      pArt->nCanvasH = static_cast<int>(U16(e + 6) * k + 0.5f);
      for (int f = 0; f < nFrames; ++f) {
        const unsigned char *q = p + nFrameTable + 16 * f;
        PortHdFrame fr;
        fr.nX = static_cast<unsigned short>(U16(q) * k);
        fr.nY = static_cast<unsigned short>(U16(q + 2) * k);
        fr.nOffsetX = static_cast<short>(static_cast<short>(U16(q + 4)) * k);
        fr.nOffsetY = static_cast<short>(static_cast<short>(U16(q + 6)) * k);
        fr.nWidth = static_cast<unsigned short>(U16(q + 8) * k + 0.5f);
        fr.nHeight = static_cast<unsigned short>(U16(q + 10) * k + 0.5f);
        pArt->frames.push_back(fr);
      }
      for (int l = 0; l < nLayers && l < 10; ++l) {
        const char *pszName = reinterpret_cast<const char *>(p + 12 + 32 * l);
        const int nKind = strcmp(pszName, "diffuse") == 0     ? PORT_HD_DIFFUSE
                          : strcmp(pszName, "teamcolor") == 0 ? PORT_HD_TEAM
                          : strcmp(pszName, "bright") == 0    ? PORT_HD_BRIGHT
                          : strcmp(pszName, "emissive") == 0  ? PORT_HD_EMISSIVE
                          : strcmp(pszName, "normal") == 0    ? PORT_HD_NORMAL
                          : strcmp(pszName, "specular") == 0  ? PORT_HD_SPECULAR
                          : strcmp(pszName, "ao_depth") == 0  ? PORT_HD_AO_DEPTH
                                                              : -1;
        const unsigned char *pl = e + 12 + 12 * l;
        const unsigned int nAt = U32(pl), nSize = U32(pl + 4);
        if (nKind >= 0 && nSize != 0 && nAt + nSize <= n)
          ReadDds(p + nAt, nSize, &pArt->layers[nKind]);
      }
      if (pArt->layers[PORT_HD_DIFFUSE].nBc == 0) {
        delete pArt;
        pArt = NULL;
      }
    }
  }
  SDL_free(p);
  return pArt;
}

// A .dds.grp's DXT1 frames into one BC1 atlas, a cell of nCell texels (0: the first frame's
// width) each, nRow to a row, at most nMax of them; each frame's size into pnSizes when it
// is given. A frame that is not DXT1 or does not fit its cell is left clear. The frames
// in the file, at most nMax, and 0 when there is no such file; *pnCell the cell's size.
int ReadGrpAtlas(const char *pszPath, int nCell, int nRow, int nMax, PortHdLayer *pAtlas, unsigned int *pnSizes,
                 int *pnCell) {
  size_t n = 0;
  unsigned char *p = static_cast<unsigned char *>(PortCascRead(pszPath, &n));
  if (p == NULL)
    return 0;
  const int nFrames = n >= 8 ? SDL_min(static_cast<int>(U16(p + 4)), nMax) : 0;
  if (nCell == 0)
    nCell = n >= 8 + 12 ? (U16(p + 8 + 4) + 3) & ~3 : 0;
  if (nFrames == 0 || nCell == 0) {
    SDL_free(p);
    return 0;
  }
  PortHdLayer &atlas = *pAtlas;
  atlas.nBc = 1;
  atlas.nWidth = nRow * nCell;
  atlas.nHeight = (nFrames + nRow - 1) / nRow * nCell;
  // clear blocks: BC1 with both colours 0 and every index 3, transparent black
  atlas.blocks.resize(static_cast<size_t>(atlas.nWidth / 4) * (atlas.nHeight / 4) * 8);
  for (size_t i = 0; i < atlas.blocks.size(); i += 8) {
    memset(&atlas.blocks[i], 0, 4);
    memset(&atlas.blocks[i + 4], 0xFF, 4);
  }
  const size_t nAtlasRow = static_cast<size_t>(atlas.nWidth / 4) * 8;
  size_t nAt = 8;
  PortHdLayer frame;
  for (int i = 0; i < nFrames && nAt + 12 <= n; ++i) {
    const unsigned int nBytes = U32(p + nAt + 8);
    const unsigned char *pDds = p + nAt + 12;
    nAt += 12 + nBytes;
    if (nAt > n || !ReadDds(pDds, nBytes, &frame) || frame.nBc != 1 || frame.nWidth > nCell || frame.nHeight > nCell)
      continue;
    const int nBlocksX = (frame.nWidth + 3) / 4;
    const int nBlocksY = (frame.nHeight + 3) / 4;
    const size_t nX = static_cast<size_t>(i % nRow) * (nCell / 4) * 8;
    const size_t nY = static_cast<size_t>(i / nRow) * (nCell / 4);
    for (int by = 0; by < nBlocksY; ++by)
      memcpy(&atlas.blocks[(nY + by) * nAtlasRow + nX], &frame.blocks[static_cast<size_t>(by) * nBlocksX * 8],
             static_cast<size_t>(nBlocksX) * 8);
    if (pnSizes != NULL)
      pnSizes[i] = static_cast<unsigned int>(frame.nWidth) | (static_cast<unsigned int>(frame.nHeight) << 16);
  }
  SDL_free(p);
  if (pnCell != NULL)
    *pnCell = nCell;
  return nFrames;
}

// The tileset's creep edges into an atlas of their own, a megatile's cell each.
void ReadCreep(const char *pszName, int nScale, PortHdTerrain *pTerrain) {
  char szName[64];
  SDL_snprintf(szName, sizeof szName, nScale == 4 ? "TileSet/%s.dds.grp" : "HD2/TileSet/%s.dds.grp", pszName);
  pTerrain->nCreep = ReadGrpAtlas(szName, pTerrain->nTileSize, PORT_HD_CREEP_ROW, PORT_HD_CREEP_MAX, &pTerrain->creep,
                                  pTerrain->anCreepSize, NULL);
  Log("hd: %d creep edge frames", pTerrain->nCreep);
}

// The tileset's water: its masks, which megatile takes which (.tmsk: "KSMT", a u16 version,
// a u16 count, then that many u16 pairs, a megatile and its mask, counted from 0), and
// the ripple's normal maps.
void ReadWater(const char *pszName, int nScale, PortHdTerrain *pTerrain) {
  const char *pszDir = nScale == 4 ? "" : "HD2/";
  char szName[96];
  SDL_snprintf(szName, sizeof szName, "%sTileSet/%s.tmsk", pszDir, pszName);
  size_t n = 0;
  unsigned char *p = static_cast<unsigned char *>(PortCascRead(szName, &n));
  if (p == NULL)
    return;
  if (n < 8 || memcmp(p, "KSMT", 4) != 0) {
    SDL_free(p);
    return;
  }
  SDL_snprintf(szName, sizeof szName, "%sTileSet/%s_mask.dds.grp", pszDir, pszName);
  pTerrain->nWaterMasks = ReadGrpAtlas(szName, pTerrain->nTileSize, PORT_HD_TILES_ROW, 65535, &pTerrain->waterMask,
                                       NULL, NULL);
  pTerrain->waterOf.assign(static_cast<size_t>(pTerrain->nTiles), 0);
  const int nPairs = U16(p + 6);
  int nWet = 0;
  for (int i = 0; i < nPairs && 8 + static_cast<size_t>(i) * 4 + 4 <= n; ++i) {
    const int nMtx = U16(p + 8 + i * 4);
    const int nMask = U16(p + 8 + i * 4 + 2);
    if (nMtx < pTerrain->nTiles && nMask < pTerrain->nWaterMasks) {
      pTerrain->waterOf[nMtx] = static_cast<unsigned int>(nMask) + 1;
      ++nWet;
    }
  }
  SDL_free(p);
  for (int k = 0; k < 2; ++k) {
    SDL_snprintf(szName, sizeof szName, "%seffect/water_normal_%d.dds.grp", pszDir, k + 1);
    pTerrain->anNormalFrames[k] =
        ReadGrpAtlas(szName, 0, PORT_HD_CREEP_ROW, 256, &pTerrain->normals[k], NULL, &pTerrain->anNormalCell[k]);
  }
  if (pTerrain->anNormalFrames[0] == 0 || pTerrain->anNormalFrames[1] == 0)
    pTerrain->nWaterMasks = 0;
  Log("hd: water on %d megatiles, %d masks", nWet, pTerrain->nWaterMasks);
  Log("hd: water normals, %d and %d frames", pTerrain->anNormalFrames[0], pTerrain->anNormalFrames[1]);
}

// The space platform's star field. Remastered's star.spk is not 1.17's: a u16 layer count,
// a u16 0, a u32 offset of the stars, then each layer {u32 star count, u16 field width,
// u16 field height}, then the stars in layer order, {u16 x, u16 y, u16 x and y in the
// layer's frame of star.dds.grp, u16 width, u16 height}. The frames (DXT5, the first
// 128 or 256 across) go one under another into the atlas, and each star is put in the
// list of every square of the field it reaches, the field wrapping round.
void ReadStars(int nScale, PortHdTerrain *pTerrain) {
  const char *pszDir = nScale == 4 ? "" : "HD2/";
  char szName[64];
  SDL_snprintf(szName, sizeof szName, "%sparallax/star.spk", pszDir);
  size_t n = 0;
  unsigned char *p = static_cast<unsigned char *>(PortCascRead(szName, &n));
  if (p == NULL)
    return;
  const int nLayers = n >= 8 ? U16(p) : 0;
  const size_t nAt = n >= 8 ? U32(p + 4) : 0;
  int anCount[5] = {};
  int nTotal = 0;
  if (nLayers == 5 && 8 + 8 * static_cast<size_t>(nLayers) <= n)
    for (int k = 0; k < nLayers; ++k) {
      anCount[k] = static_cast<int>(U32(p + 8 + 8 * k));
      nTotal += anCount[k];
    }
  const int nFieldW = nTotal > 0 ? U16(p + 12) : 0;
  const int nFieldH = nTotal > 0 ? U16(p + 14) : 0;
  if (nTotal == 0 || nFieldW == 0 || nFieldH == 0 || nAt + static_cast<size_t>(nTotal) * 12 > n) {
    SDL_free(p);
    return;
  }

  // the layers' frames, one under another
  SDL_snprintf(szName, sizeof szName, "%sparallax/star.dds.grp", pszDir);
  size_t nGrp = 0;
  unsigned char *pGrp = static_cast<unsigned char *>(PortCascRead(szName, &nGrp));
  std::vector<PortHdLayer> frames;
  int nCellW = 0;
  int nCellH = 0;
  for (size_t nFrame = 8; pGrp != NULL && nGrp >= 8 && frames.size() < static_cast<size_t>(U16(pGrp + 4)) &&
                          nFrame + 12 <= nGrp;) {
    const unsigned int nBytes = U32(pGrp + nFrame + 8);
    PortHdLayer frame;
    if (nFrame + 12 + nBytes > nGrp || !ReadDds(pGrp + nFrame + 12, nBytes, &frame) || frame.nBc != 3)
      break;
    nCellW = SDL_max(nCellW, (frame.nWidth + 3) & ~3);
    nCellH = SDL_max(nCellH, (frame.nHeight + 3) & ~3);
    frames.push_back(frame);
    nFrame += 12 + nBytes;
  }
  SDL_free(pGrp);
  if (frames.size() < static_cast<size_t>(nLayers)) {
    SDL_free(p);
    Log("hd: no star field (%d frames of %d)", static_cast<int>(frames.size()), nLayers);
    return;
  }
  PortHdLayer &atlas = pTerrain->starAtlas;
  atlas.nBc = 3;
  atlas.nWidth = nCellW;
  atlas.nHeight = nCellH * nLayers;
  atlas.blocks.assign(static_cast<size_t>(atlas.nWidth / 4) * (atlas.nHeight / 4) * 16, 0);  // clear
  const size_t nAtlasRow = static_cast<size_t>(atlas.nWidth / 4) * 16;
  for (int k = 0; k < nLayers; ++k) {
    const PortHdLayer &frame = frames[k];
    const int nBlocksX = (frame.nWidth + 3) / 4;
    for (int by = 0; by < (frame.nHeight + 3) / 4; ++by)
      memcpy(&atlas.blocks[(static_cast<size_t>(k) * (nCellH / 4) + by) * nAtlasRow],
             &frame.blocks[static_cast<size_t>(by) * nBlocksX * 16], static_cast<size_t>(nBlocksX) * 16);
  }

  // each layer's squares and their lists, then the stars
  const int nBin = 16 * nScale;
  const int nBinsX = (nFieldW + nBin - 1) / nBin;
  const int nBinsY = (nFieldH + nBin - 1) / nBin;
  const size_t nBins = static_cast<size_t>(nBinsX) * nBinsY;
  std::vector<std::vector<unsigned int>> lists(nBins * nLayers);
  std::vector<unsigned int> stars;
  stars.reserve(static_cast<size_t>(nTotal) * 3);
  int nStar = 0;
  for (int k = 0; k < nLayers; ++k)
    for (int i = 0; i < anCount[k]; ++i, ++nStar) {
      const unsigned char *s = p + nAt + static_cast<size_t>(nStar) * 12;
      const int x = U16(s), y = U16(s + 2), w = U16(s + 8), h = U16(s + 10);
      stars.push_back(static_cast<unsigned int>(x) | static_cast<unsigned int>(y) << 16);
      stars.push_back(static_cast<unsigned int>(U16(s + 4)) |
                      static_cast<unsigned int>(U16(s + 6) + k * nCellH) << 16);
      stars.push_back(static_cast<unsigned int>(w) | static_cast<unsigned int>(h) << 16);
      int nLastY = -1;
      for (int dy = 0; dy < h; ++dy) {
        const int by = (y + dy) % nFieldH / nBin;
        if (by == nLastY)
          continue;
        nLastY = by;
        int nLastX = -1;
        for (int dx = 0; dx < w; ++dx) {
          const int bx = (x + dx) % nFieldW / nBin;
          if (bx == nLastX)
            continue;
          nLastX = bx;
          lists[k * nBins + static_cast<size_t>(by) * nBinsX + bx].push_back(static_cast<unsigned int>(nStar));
        }
      }
    }
  SDL_free(p);
  std::vector<unsigned int> &out = pTerrain->starBins;
  const size_t nHeads = (nBins + 1) * nLayers;
  out.assign(nHeads, 0);
  for (int k = 0; k < nLayers; ++k) {
    for (size_t b = 0; b < nBins; ++b) {
      out[k * (nBins + 1) + b] = static_cast<unsigned int>(out.size());
      out.insert(out.end(), lists[k * nBins + b].begin(), lists[k * nBins + b].end());
    }
    out[k * (nBins + 1) + nBins] = static_cast<unsigned int>(out.size());
  }
  pTerrain->nStarsAt = static_cast<unsigned int>(out.size());
  out.insert(out.end(), stars.begin(), stars.end());
  pTerrain->nStarFieldW = nFieldW;
  pTerrain->nStarFieldH = nFieldH;
  pTerrain->nStarCellW = nCellW;
  pTerrain->nStarCellH = nCellH;
  pTerrain->nStarBin = nBin;
  pTerrain->nStarBinsX = nBinsX;
  pTerrain->nStarBinsY = nBinsY;
  pTerrain->nStarLayers = nLayers;
  Log("hd: star field, %d stars in a field of %d x %d", nTotal, nFieldW, nFieldH);
}

// A tileset's megatiles into one atlas, by the names 1.17 gives its tilesets.
PortHdTerrain *ReadTileset(int nEra, int nScale) {
  static const char *const s_apszNames[PORT_HD_TILESETS] = {"badlands", "platform", "install", "ashworld",
                                                            "jungle",   "desert",   "ice",     "twilight"};
  char szName[64];
  SDL_snprintf(szName, sizeof szName, nScale == 4 ? "TileSet/%s.dds.vr4" : "HD2/TileSet/%s.dds.vr4",
               s_apszNames[nEra]);
  size_t n = 0;
  unsigned char *p = static_cast<unsigned char *>(PortCascRead(szName, &n));
  if (p == NULL)
    return NULL;
  PortHdTerrain *pTerrain = NULL;
  const int nTiles = n >= 8 ? U16(p + 4) : 0;
  const int nSize = 32 * nScale;
  if (nTiles > 0) {
    pTerrain = new PortHdTerrain();
    pTerrain->nEra = nEra;
    pTerrain->nTiles = nTiles;
    pTerrain->nTileSize = nSize;
    PortHdLayer &atlas = pTerrain->atlas;
    atlas.nBc = 1;
    atlas.nWidth = PORT_HD_TILES_ROW * nSize;
    atlas.nHeight = (nTiles + PORT_HD_TILES_ROW - 1) / PORT_HD_TILES_ROW * nSize;
    atlas.blocks.assign(static_cast<size_t>(atlas.nWidth / 4) * (atlas.nHeight / 4) * 8, 0);
    const int nTileBlocks = nSize / 4;  // a tile's blocks across and down
    const size_t nAtlasRow = static_cast<size_t>(atlas.nWidth / 4) * 8;
    size_t nAt = 8;
    int nRead = 0;
    PortHdLayer tile;
    for (int i = 0; i < nTiles && nAt + 12 <= n; ++i) {
      const unsigned int nBytes = U32(p + nAt + 8);
      const unsigned char *pDds = p + nAt + 12;
      nAt += 12 + nBytes;
      if (nAt > n || !ReadDds(pDds, nBytes, &tile) || tile.nBc != 1 || tile.nWidth != nSize ||
          tile.nHeight != nSize)
        continue;
      const size_t nX = static_cast<size_t>(i % PORT_HD_TILES_ROW) * nTileBlocks * 8;
      const size_t nY = static_cast<size_t>(i / PORT_HD_TILES_ROW) * nTileBlocks;
      for (int by = 0; by < nTileBlocks; ++by)
        memcpy(&atlas.blocks[(nY + by) * nAtlasRow + nX], &tile.blocks[static_cast<size_t>(by) * nTileBlocks * 8],
               static_cast<size_t>(nTileBlocks) * 8);
      ++nRead;
    }
    Log("hd: tileset %d, %d megatiles of %d read", nEra, nRead, nTiles);
  }
  SDL_free(p);
  if (pTerrain != NULL) {
    ReadCreep(s_apszNames[nEra], nScale, pTerrain);
    ReadWater(s_apszNames[nEra], nScale, pTerrain);
    if (nEra == 1)  // 1.17 draws its stars on the space platform alone (gamemap_update)
      ReadStars(nScale, pTerrain);
  }
  return pTerrain;
}

// A console's pieces. The places are Remastered's (the design kit's schud.js, PIECES, in
// 2x texels): the left piece 213 left of 1.17's console, the centre 283 and the right 350
// into it. The neutral console is 1.17's replay console with 213 texels more art at each
// side, a 16:9 screen's width.
PortHdConsole *ReadConsole(int nSkin, int nScale) {
  static const char *const s_apszSkins[PORT_HD_CONSOLES] = {"zerg", "terran", "protoss", "neutral"};
  static const char *const s_apszRace[3] = {"console_left", "console_right", "console_center"};
  static const int s_anRaceX[3] = {-213, 350, 283};
  const bool bNeutral = nSkin == 3;
  PortHdConsole *pConsole = new PortHdConsole();
  pConsole->nSkin = nSkin;
  for (int i = 0; i < (bNeutral ? 1 : 3); ++i) {
    char szName[96];
    SDL_snprintf(szName, sizeof szName, "%sgame/consoles/%s/%s.DDS", nScale == 4 ? "" : "HD2/", s_apszSkins[nSkin],
                 bNeutral ? "console" : s_apszRace[i]);
    size_t n = 0;
    unsigned char *p = static_cast<unsigned char *>(PortCascRead(szName, &n));
    PortHdConsolePiece &piece = pConsole->pieces[pConsole->nPieces];
    const bool bRead = p != NULL && ReadDds(p, n, &piece.art);
    SDL_free(p);
    if (!bRead) {
      delete pConsole;
      return NULL;
    }
    piece.nX = (bNeutral ? -213 : s_anRaceX[i]) * nScale / 2;
    ++pConsole->nPieces;
  }
  Log("hd: console %d, %d pieces", nSkin, pConsole->nPieces);
  return pConsole;
}

// One of the console's contents (port_hd.h).
PortHdUi *ReadUi(int nSet, int nScale) {
  static const char *const s_apszRace[3] = {"zerg", "terran", "protoss"};
  const char *pszDir = nScale == 4 ? "" : "HD2/";
  char szName[96];
  PortHdUi *pUi = new PortHdUi();
  pUi->nSet = nSet;
  if (nSet == PORT_HD_UI_WIRES) {
    SDL_snprintf(szName, sizeof szName, "%sunit/wirefram/wirefram.dds.grp", pszDir);
    size_t n = 0;
    unsigned char *p = static_cast<unsigned char *>(PortCascRead(szName, &n));
    const int nFrames = p != NULL && n >= 8 ? U16(p + 4) : 0;
    pUi->frames.resize(nFrames);
    size_t nAt = 8;
    for (int i = 0; i < nFrames && nAt + 12 <= n; ++i) {
      const unsigned int nBytes = U32(p + nAt + 8);
      if (nAt + 12 + nBytes > n || !ReadDds(p + nAt + 12, nBytes, &pUi->frames[i]))
        pUi->frames[i] = PortHdLayer();
      nAt += 12 + nBytes;
    }
    SDL_free(p);
    pUi->nFrames = nFrames;
  } else if (nSet >= PORT_HD_UI_BARS && nSet < PORT_HD_UI_BARS + 3) {
    // the progress bar, empty and full, the full one a whole row of blocks below
    static const char *const s_apszBar[2] = {"pbrempt", "pbrfull"};
    PortHdLayer aBar[2];
    bool bRead = true;
    for (int i = 0; i < 2 && bRead; ++i) {
      SDL_snprintf(szName, sizeof szName, "%sgame/consoles/%s/%s.dds", pszDir,
                   s_apszRace[(nSet - PORT_HD_UI_BARS) % 3], s_apszBar[i]);
      size_t n = 0;
      unsigned char *p = static_cast<unsigned char *>(PortCascRead(szName, &n));
      bRead = p != NULL && ReadDds(p, n, &aBar[i]);
      SDL_free(p);
    }
    if (bRead && aBar[0].nBc == aBar[1].nBc && aBar[0].nWidth == aBar[1].nWidth &&
        aBar[0].nHeight == aBar[1].nHeight) {
      const int nRows = (aBar[0].nHeight + 3) / 4 * 4;
      pUi->nRow = 1;
      pUi->nCell = nRows;
      pUi->sizes.assign(2, static_cast<unsigned int>(aBar[0].nWidth) | static_cast<unsigned int>(aBar[0].nHeight) << 16);
      pUi->atlas = aBar[0];
      pUi->atlas.nHeight = 2 * nRows;
      pUi->atlas.blocks.insert(pUi->atlas.blocks.end(), aBar[1].blocks.begin(), aBar[1].blocks.end());
      pUi->nFrames = 2;
    }
  } else {
    const bool bIcons = nSet == PORT_HD_UI_ICONS;
    const bool bResources = nSet == PORT_HD_UI_RESOURCES;
    if (bIcons)
      SDL_snprintf(szName, sizeof szName, "%sunit/cmdicons/cmdicons.dds.grp", pszDir);
    else if (bResources)
      SDL_snprintf(szName, sizeof szName, "%sgame/icons.dds.grp", pszDir);
    else
      SDL_snprintf(szName, sizeof szName, "%sgame/consoles/%s/cmdbtns.dds.grp", pszDir,
                   s_apszRace[(nSet - PORT_HD_UI_BUTTONS) % 3]);
    pUi->nRow = bIcons ? 20 : bResources ? 4 : 8;
    pUi->sizes.assign(bIcons ? 512 : bResources ? 16 : 64, 0);
    pUi->nFrames = ReadGrpAtlas(szName, (bIcons ? 32 : bResources ? 56 : 72) * nScale, pUi->nRow,
                                static_cast<int>(pUi->sizes.size()), &pUi->atlas, pUi->sizes.data(), &pUi->nCell);
  }
  if (pUi->nFrames == 0) {
    delete pUi;
    return NULL;
  }
  Log("hd: console contents %d, %d frames", nSet, pUi->nFrames);
  return pUi;
}

void Loader(void) {
  ReadBorrowing();
  const int nScale = PortHdScale();
  for (;;) {
    int nImage;
    {
      std::unique_lock<std::mutex> hold(g_lock);
      g_wake.wait(hold, [] { return !g_queue.empty(); });
      nImage = g_queue.front();
      g_queue.pop_front();
    }
    if (nImage >= FIRST_UI) {
      PortHdUi *pUi = ReadUi(nImage - FIRST_UI, nScale);
      if (pUi == NULL)
        Log("hd: console contents %d have no HD art", nImage - FIRST_UI);
      std::lock_guard<std::mutex> hold(g_lock);
      g_apUi[nImage - FIRST_UI] = pUi;
      g_anState[nImage] = pUi != NULL ? STATE_READY : STATE_MISSING;
      continue;
    }
    if (nImage >= FIRST_CONSOLE) {
      PortHdConsole *pConsole = ReadConsole(nImage - FIRST_CONSOLE, nScale);
      if (pConsole == NULL)
        Log("hd: console %d has no HD art", nImage - FIRST_CONSOLE);
      std::lock_guard<std::mutex> hold(g_lock);
      g_apConsole[nImage - FIRST_CONSOLE] = pConsole;
      g_anState[nImage] = pConsole != NULL ? STATE_READY : STATE_MISSING;
      continue;
    }
    if (nImage >= PORT_HD_IMAGES) {
      PortHdTerrain *pTerrain = ReadTileset(nImage - PORT_HD_IMAGES, nScale);
      if (pTerrain == NULL)
        Log("hd: tileset %d has no HD terrain", nImage - PORT_HD_IMAGES);
      std::lock_guard<std::mutex> hold(g_lock);
      g_apTerrain[nImage - PORT_HD_IMAGES] = pTerrain;
      g_anState[nImage] = pTerrain != NULL ? STATE_READY : STATE_MISSING;
      continue;
    }
    PortHdArt *pArt = ReadImage(nImage, nScale);
    if (pArt == NULL)
      Log("hd: image %d has no HD art", nImage);
    std::lock_guard<std::mutex> hold(g_lock);
    g_apArt[nImage] = pArt;
    g_anState[nImage] = pArt != NULL ? STATE_READY : STATE_MISSING;
  }
}

// HD art needs the install and Remastered's paid content (PortRemasteredOwned, port_casc.cpp),
// which is held for now (the PRD's decision of 2026-10-21): without it HD is as it is without
// an install, and the game is drawn in the classic art.
static bool HdInstall(void) {
  return PortCascAvailable() != 0 && PortRemasteredOwned();
}

bool Start(void) {
  if (g_bStarted && !g_bAvailable && HdInstall()) {
    // an install Settings was given after the start
    g_bAvailable = true;
    Log("hd: Remastered's storage is open now, art at %dx", PortHdScale());
    std::thread(Loader).detach();
  }
  if (g_bStarted)
    return g_bAvailable;
  g_bStarted = true;
  g_bAvailable = HdInstall();
  Log(g_bAvailable                ? "hd: Remastered's storage is open, art at %dx"
      : PortCascAvailable() != 0 ? "hd: Remastered's HD is held, the classic art drawn (%dx asked)"
                                 : "hd: no Remastered install (%dx asked)",
      PortHdScale());
  if (g_bAvailable)
    std::thread(Loader).detach();
  return g_bAvailable;
}

}  // namespace

int PortHdScale(void) {
  // read once: the HD frames ask for it for each draw
  static const int s_nScale = [] {
    const char *psz = SDL_getenv("HYPERION_HD_SCALE");
    return psz != NULL && psz[0] == '4' ? 4 : 2;
  }();
  return s_nScale;
}

bool PortHdAvailable(void) {
  return Start();
}

const PortHdArt *PortHdImage(int nImage) {
  if (nImage < 0 || nImage >= PORT_HD_IMAGES || !Start())
    return NULL;
  std::lock_guard<std::mutex> hold(g_lock);
  if (g_anState[nImage] == STATE_NONE_ASKED) {
    g_anState[nImage] = STATE_LOADING;
    g_queue.push_back(nImage);
    g_wake.notify_one();
  }
  return g_anState[nImage] == STATE_READY ? g_apArt[nImage] : NULL;
}

const PortHdTerrain *PortHdTileset(int nEra) {
  if (nEra < 0 || nEra >= PORT_HD_TILESETS || !Start())
    return NULL;
  const int n = PORT_HD_IMAGES + nEra;
  std::lock_guard<std::mutex> hold(g_lock);
  if (g_anState[n] == STATE_NONE_ASKED) {
    g_anState[n] = STATE_LOADING;
    g_queue.push_back(n);
    g_wake.notify_one();
  }
  return g_anState[n] == STATE_READY ? g_apTerrain[nEra] : NULL;
}

// Whether a layer's texel at x, y is solid: its alpha at least half, from the block as
// stored (BC1's transparent index, BC3's alpha block).
bool PortHdLayerSolid(const PortHdLayer &layer, int x, int y)
{
  if (x < 0 || y < 0 || x >= layer.nWidth || y >= layer.nHeight)
    return false;
  const int nBlockBytes = layer.nBc == 1 ? 8 : 16;
  const size_t nAt = (static_cast<size_t>(y / 4) * ((layer.nWidth + 3) / 4) + x / 4) * nBlockBytes;
  if (nAt + nBlockBytes > layer.blocks.size())
    return false;
  const unsigned char *p = &layer.blocks[nAt];
  const int nTexel = (y & 3) * 4 + (x & 3);
  if (layer.nBc == 1) {
    const unsigned int c0 = p[0] | (p[1] << 8);
    const unsigned int c1 = p[2] | (p[3] << 8);
    const unsigned int nIndex = (p[4 + (y & 3)] >> ((x & 3) * 2)) & 3;
    return c0 > c1 || nIndex != 3;
  }
  const unsigned int a0 = p[0];
  const unsigned int a1 = p[1];
  const unsigned long long bits = static_cast<unsigned long long>(p[2]) | static_cast<unsigned long long>(p[3]) << 8 |
                                  static_cast<unsigned long long>(p[4]) << 16 |
                                  static_cast<unsigned long long>(p[5]) << 24 |
                                  static_cast<unsigned long long>(p[6]) << 32 |
                                  static_cast<unsigned long long>(p[7]) << 40;
  const unsigned int i = static_cast<unsigned int>(bits >> (nTexel * 3)) & 7;
  unsigned int a;
  if (i == 0)
    a = a0;
  else if (i == 1)
    a = a1;
  else if (a0 > a1)
    a = ((8 - i) * a0 + (i - 1) * a1) / 7;
  else
    a = i == 6 ? 0 : i == 7 ? 255 : ((6 - i) * a0 + (i - 1) * a1) / 5;
  return a >= 128;
}

const PortHdConsole *PortHdConsoleArt(int nSkin) {
  if (nSkin < 0 || nSkin >= PORT_HD_CONSOLES || !Start())
    return NULL;
  const int n = FIRST_CONSOLE + nSkin;
  std::lock_guard<std::mutex> hold(g_lock);
  if (g_anState[n] == STATE_NONE_ASKED) {
    g_anState[n] = STATE_LOADING;
    g_queue.push_back(n);
    g_wake.notify_one();
  }
  return g_anState[n] == STATE_READY ? g_apConsole[nSkin] : NULL;
}

const PortHdUi *PortHdUiSet(int nSet) {
  if (nSet < 0 || nSet >= PORT_HD_UI_SETS || !Start())
    return NULL;
  const int n = FIRST_UI + nSet;
  std::lock_guard<std::mutex> hold(g_lock);
  if (g_anState[n] == STATE_NONE_ASKED) {
    g_anState[n] = STATE_LOADING;
    g_queue.push_back(n);
    g_wake.notify_one();
  }
  return g_anState[n] == STATE_READY ? g_apUi[nSet] : NULL;
}

void PortHdCounts(int *pnReady, int *pnLoading, int *pnNone) {
  *pnReady = *pnLoading = *pnNone = 0;
  std::lock_guard<std::mutex> hold(g_lock);
  for (int i = 0; i < PORT_HD_IMAGES; ++i) {
    *pnReady += g_anState[i] == STATE_READY;
    *pnLoading += g_anState[i] == STATE_LOADING;
    *pnNone += g_anState[i] == STATE_MISSING;
  }
}
