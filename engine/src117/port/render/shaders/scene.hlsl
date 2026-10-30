// scene.hlsl -- the map drawn from the frame description (port_scene.cpp,
// docs/renderer.md, R4b), into an 8-bit map of the GPU's own that the check compares
// with the game's.
//
// CSScene draws the terrain, or takes the map as it is, and draws the primitives
// from gFirst up to gLast over it: one thread draws four pixels side by side and
// writes them as one word, so the map is the game's bytes in the game's order, and
// it walks only the primitives of its 32-pixel square. A distortion takes the pixels
// a distance away as they were before it, so CSDistort draws one on its own, reading
// a copy of the map made just before (gPrev). At the last stage CSShow puts the
// pixels the screen takes into the GPU's copy of the screen (gShown), which the
// presenter shows (present.hlsl, PSCompose).
//
// Bindings follow SDL_GPU's layout for compute: read-only storage buffers in space 0
// (set 0), read-write ones in space 1 (set 1), uniforms in space 2 (set 2). SDL_GPU
// binds at most 8 storage buffers of each kind, so the tileset's two tables share a
// buffer, and so do the squares' starts and lists.

cbuffer SceneParams : register(b0, space2)
{
  uint gWidth;       // the map layer's width and rows
  uint gRows;
  uint gScrollX;     // the map pixel at its top left
  uint gScrollY;
  uint gRingX;       // the scroll ring's size in megatiles
  uint gRingY;
  uint gHeight;      // the screen buffer's rows, where a distortion wraps
  uint gBinsX;       // the squares in a row
  uint gFirst;       // CSScene: the primitives to draw; CSDistort: the first word
  uint gLast;        // column of the distortion's box and how many
  uint gFromTerrain; // CSScene: draw the terrain first
  uint gPrim;        // CSDistort: the distortion
  uint gCellsAt;     // where the cells start in gTileset, in bytes
  uint gBinListAt;   // where the lists start in gFrame, in words
  uint gBytesAt;     // where the frame's own bytes start in gFrame
  uint gCopiedAt;    // CSShow: where the pixels the screen takes start in them
  uint gOverFrom;    // CSScene: the first primitive after every one drawn in HD, from which
                     // a pixel drawn is over the HD frames (gTouched's 2 and 4)
  uint gUnderAt;     // CSScene: where in gTouched the pixels under a dialog's translucency
                     // are kept, and at twice it the dialog's own; 0 for nowhere
  uint gExtraAt;     // CSSceneFull: where each primitive's full-colour extras start in gFrame
  uint gOrigin;      // CSScene, CSSceneFull: the dispatch's top left in the layer, x in the low 16
                     // bits (a multiple of 4), y in the high: a pass after a distortion covers
                     // only the box of its primitives; CSDistort: its first row
  uint gStep;        // the game's pixels to one of the layer's, across and down: each
                     // pixel of the layer is the game's at the middle of its square. 1 but with
                     // the camera far out, where a pixel of the window is many of the game's
  uint gViewW;       // the render view's width in the game's pixels, a row of the screen buffer
  uint gLooksAt;     // CSSceneFull: where the looks' data starts in gFrame, 0 for none
  uint gTimeMs;      // and the time, in milliseconds within the hour
};

// The full colour's (stage G; CSSceneFull, CSDistortFull, CSShowFull): the palette as shown
// and each colour slot's eight team shades, RGBA8 a word.
cbuffer FullParams : register(b1, space2)
{
  uint4 gPal[64];
  uint4 gShade[136];       // PLAYER_SLOTS_MAX * 8 words, rounded up
};

// And the fog's colour matrices at each of its 32 shades, three rows each, for the sprites
// and for the terrain, fitted to 1.17's tables (port_gpu.cpp, GpuHdFogMatrices). A block of
// their own: SDL's Vulkan binds 4 KiB of a block, and past it the matrices read as zeros,
// which drew the fog's edges black, a block at a time.
cbuffer FogParams : register(b3, space2)
{
  float4 gFogSprite[96];
  float4 gFogTerrain[96];
};

struct Slot { uint mtx; uint tile; uint edge; uint edgeFrame; };
struct Entry { uint offset; uint size; uint origin; uint pad; };
struct Prim { uint kind; int x; int y; uint entry; uint clip0; uint clip1; uint table; uint param; };

StructuredBuffer<Slot> gSlots : register(t0, space0);       // what the ring holds, per tile
ByteAddressBuffer gTileset : register(t1, space0);          // the megatiles, 16 cell references a megatile in dwords,
                                                            // then .vr4, 64 bytes a cell
StructuredBuffer<Entry> gEntries : register(t2, space0);    // the atlas's frames
ByteAddressBuffer gAtlas : register(t3, space0);            // their pixels, 16 bits each
StructuredBuffer<Prim> gPrims : register(t4, space0);       // the primitives, in order
ByteAddressBuffer gTables : register(t5, space0);           // the tables they read
ByteAddressBuffer gFrame : register(t6, space0);            // each square's first in the lists, the
                                                            // lists (the primitives of each square),
                                                            // then the frame's own bytes
ByteAddressBuffer gPrev : register(t7, space0);             // the map before a distortion

RWByteAddressBuffer gMap : register(u0, space1);            // the map, a byte a pixel
RWByteAddressBuffer gStats : register(u1, space1);          // [0] pixels of a tile the ring no longer holds
RWByteAddressBuffer gShown : register(u2, space1);          // the screen as the GPU has it
RWByteAddressBuffer gTouched : register(u3, space1);        // beside the map, a byte a pixel: 1 where
                                                            // anything but the terrain and the fog
                                                            // drew, where the HD frame keeps the map's
                                                            // pixel and does not draw HD terrain; 2
                                                            // too where a primitive after every HD
                                                            // frame drew (the cursor, a health bar,
                                                            // a dialog), which goes back over them;
                                                            // 4 instead where that was a dialog's
                                                            // translucency, its fit (the blit's
                                                            // param above 8 bits, less 1) in bits 3
                                                            // to 5, the pixel under it at gUnderAt
                                                            // on, and the dialog's at twice that;
                                                            // from three times that two bytes a
                                                            // pixel, the draw order (RankSet)
RWByteAddressBuffer gMapRgb : register(u4, space1);         // the full colour's map, RGBA8 a pixel; alpha
                                                            // 1 where a sprite or a line drew, 0 terrain
RWByteAddressBuffer gShownRgb : register(u5, space1);       // its copy of the screen, which PSComposeFull reads
RWByteAddressBuffer gPrevRgb : register(u6, space1);        // its map before a distortion
RWByteAddressBuffer gHud : register(u7, space1);            // with the camera zoomed out, the
                                                            // screen's, a byte a pixel: 1 where a
                                                            // primitive over the map drew (CSHud)

// CSHud's: the camera, and the render view's map layer it looks at.
cbuffer HudParams : register(b2, space2)
{
  float gCamX;     // the map pixel at the screen's top left
  float gCamY;
  float gZoom;     // the map's pixels to a screen pixel
  uint gHudFull;   // 1: the full colour's copy of the screen too
  uint gMapX;      // the map layer's top left in map pixels, and its size
  uint gMapY;
  uint gMapW;
  uint gMapH;
  uint gMapStep;   // the layer's step (SceneParams' gStep), and its pixels across
  uint gMapStride;
  uint gHudUnused0;
  uint gHudUnused1;
};

static const uint NONE = 0xFFFFFFFFu;
static const uint PRIM_NORMAL = 1u;
static const uint PRIM_SHADOW = 2u;
static const uint PRIM_EFFECT = 3u;
static const uint PRIM_FADE = 4u;
static const uint PRIM_UNCLOAKED = 5u;
static const uint PRIM_WARP = 6u;
static const uint PRIM_SPAN = 7u;
static const uint PRIM_STAR = 9u;
static const uint PRIM_FOG = 10u;
static const uint PRIM_GLYPH = 11u;
static const uint PRIM_BLIT = 12u;
static const uint PRIM_LOOK = 13u;
static const uint PRIM_MIRROR = 0x100u;

uint LoadByte(ByteAddressBuffer b, uint nAt)
{
  return (b.Load(nAt & ~3u) >> ((nAt & 3u) * 8u)) & 0xFFu;
}

uint LoadHalf(ByteAddressBuffer b, uint nAt)
{
  return (b.Load(nAt & ~3u) >> ((nAt & 2u) * 8u)) & 0xFFFFu;
}

uint Table(uint nAt)
{
  return LoadByte(gTables, nAt);
}

// An atlas pixel of frame e at x, y inside it: the colour with bit 8 set, or 0 where
// the frame is clear or x, y is outside it.
uint AtlasPixel(uint e, int x, int y)
{
  Entry entry = gEntries[e];
  int w = int(entry.size & 0xFFFFu);
  int h = int(entry.size >> 16);
  if (x < 0 || y < 0 || x >= w || y >= h)
    return 0;
  return LoadHalf(gAtlas, (entry.offset + uint(y * w + x)) * 2u);
}

// The terrain at map pixel px, py: the megatile the ring holds for its tile, and the
// creep edge over it (cell_draw_mtx, draw_cell, rle_creep).
uint Terrain(uint px, uint py, inout uint nStale)
{
  uint tx = px >> 5;
  uint ty = py >> 5;
  Slot s = gSlots[(ty % gRingY) * gRingX + tx % gRingX];
  if (s.tile != (tx | (ty << 16))) {
    ++nStale;
    return 0;
  }
  uint lx = px & 31u;
  uint ly = py & 31u;
  if (s.edge != NONE) {
    Entry entry = gEntries[s.edge];
    uint v = AtlasPixel(s.edge, int(lx) - int(entry.origin & 0xFFFFu), int(ly) - int(entry.origin >> 16));
    if (v & 0x100u)
      return v & 0xFFu;
  }
  uint mtx = s.mtx & 0x7FFFu;
  uint cellRef = gTileset.Load((mtx * 16u + (ly >> 3) * 4u + (lx >> 3)) * 4u);
  uint cx = lx & 7u;
  if (cellRef & 1u)
    cx = 7u - cx;
  return LoadByte(gTileset, gCellsAt + (cellRef >> 1) * 64u + (ly & 7u) * 8u + cx);
}

// The frame's pixel a primitive draws at dx, dy inside its clip: the clip keeps
// columns from its left, counted from the frame's right edge when it is mirrored
// (port_draw.cpp, DrawRle).
uint PrimPixel(Prim p, int dx, int dy)
{
  Entry entry = gEntries[p.entry];
  int w = int(entry.size & 0xFFFFu);
  int col = int(p.clip0 & 0xFFFFu) + dx;
  if (p.kind & PRIM_MIRROR)
    col = w - 1 - col;
  return AtlasPixel(p.entry, col, int(p.clip0 >> 16) + dy);
}

int SignedByte(uint b)
{
  return int(b << 24) >> 24;
}

uint FrameWord(uint n)
{
  return gFrame.Load(n * 4u);
}

uint FrameByte(uint nAt)
{
  uint nByte = gBytesAt + nAt;
  return (gFrame.Load(nByte & ~3u) >> ((nByte & 3u) * 8u)) & 0xFFu;
}

// The fog over pixel x, y (mask_apply_light): its 8x8 block's shade from the light
// map's four corners, flat when they agree (0 is black, 0x1F leaves the pixel), else
// stepped down the block's sides and then across each row from its right end
// (mask_block_lerp).
uint Fog(Prim p, uint x, uint y, uint v)
{
  uint nAt = uint(p.x) + (y >> 3) * uint(p.y) + (x >> 3);
  int tl = int(FrameByte(nAt));
  int tr = int(FrameByte(nAt + 1u));
  int bl = int(FrameByte(nAt + uint(p.y)));
  int br = int(FrameByte(nAt + uint(p.y) + 1u));
  if (tl == tr && tl == bl && tl == br) {
    if (tl == 0)
      return 0;
    if (tl == 0x1F)
      return v;
    return Table(p.table + uint(tl) * 256u + v);
  }
  int i = int(y & 7u);
  int left = (tl << 8) + i * (((bl << 8) - (tl << 8)) / 8);
  int right = (tr << 8) + i * (((br << 8) - (tr << 8)) / 8);
  int step = (left - right) >> 3;
  int shade = right + (7 - int(x & 7u)) * step;
  return Table(p.table + uint((shade >> 8) & 0x1F) * 256u + v);
}

// Primitive p over pixel x, y (nAt in the map) that has v.
uint Apply(Prim p, int x, int y, uint nAt, uint v)
{
  uint kind = p.kind & 0xFFu;
  if (kind == PRIM_LOOK)
    return v;
  if (kind == PRIM_FOG)
    return Fog(p, uint(x), uint(y), v);
  if (kind == PRIM_SPAN) {
    int d = int(nAt) - p.x;
    if (d < 0 || (uint(d) % p.clip0) != 0u || uint(d) / p.clip0 >= uint(p.y))
      return v;
    return p.table & 0xFFu;
  }
  int cw = int(p.clip1 & 0xFFFFu);
  int ch = int(p.clip1 >> 16);
  int dx = x - p.x;
  int dy = y - p.y;
  if (dx < 0 || dy < 0 || dx >= cw || dy >= ch)
    return v;
  if (kind == PRIM_STAR)
    return v == 0u ? Table(p.table + uint(dy * cw + dx)) : v;
  if (kind == PRIM_BLIT) {
    // A bitmap's pixels in the frame's bytes: copied, or those not 0 (bmp_mask_rect),
    // or through a table with the map's pixel (the dialogs' translucency).
    uint b = FrameByte(p.table + uint(dy * cw + dx));
    uint mode = p.param & 0xFFu;  // above it, the HD frame's fit (port_gpu.cpp)
    if (mode == 0u)
      return b;
    if (mode == 1u)
      return b != 0u ? b : v;
    return Table(p.entry + (mode == 2u ? (b << 8) | v : (v << 8) | b));
  }
  uint s = PrimPixel(p, dx, dy);
  if ((s & 0x100u) == 0u)
    return v;
  s &= 0xFFu;
  if (kind == PRIM_NORMAL)
    return Table(p.table + s);
  if (kind == PRIM_GLYPH)
    return Table(p.table + (s & 7u));
  if (kind == PRIM_SHADOW || kind == PRIM_FADE)
    return Table(p.table + v);
  if (kind == PRIM_EFFECT)
    return Table(p.table + s * 256u + v);
  if (kind == PRIM_UNCLOAKED) {
    uint c = Table(p.table + s);
    if (SignedByte(Table((p.param >> 8) + c)) > SignedByte(p.param & 0xFFu))
      return c;
    return v;
  }
  if (kind == PRIM_WARP) {
    // The texture is read on through the pixels drawn, one row of the clip after
    // another, from the right when mirrored.
    int col = (p.kind & PRIM_MIRROR) ? cw - 1 - dx : dx;
    return Table(p.table + uint(dy * cw + col));
  }
  return v;
}

// The draw order with the frame in HD: the HD draws before the last primitive that drew a
// pixel solidly, from its kind's 16 bits on (port_gpu_hd.cpp, GpuHdScene). An HD frame drawn
// before that leaves the pixel to it (hd.hlsl, RankOver). A shadow, a translucency and the
// fog leave the order as it is.
bool RankKind(Prim p)
{
  uint kind = p.kind & 0xFFu;
  return kind == PRIM_NORMAL || kind == PRIM_UNCLOAKED || kind == PRIM_WARP || kind == PRIM_SPAN ||
         kind == PRIM_GLYPH || (kind == PRIM_BLIT && (p.param & 0xFFu) < 2u);
}

uint2 RankLoad(uint nAt)
{
  return gUnderAt != 0u && gFromTerrain == 0u ? gTouched.Load2(3u * gUnderAt + 2u * nAt) : uint2(0u, 0u);
}

// Pixel i of four (two to a word) given primitive p's rank.
uint2 RankSet(uint2 rank, uint i, Prim p)
{
  uint shift = (i & 1u) * 16u;
  uint w = ((i < 2u ? rank.x : rank.y) & ~(0xFFFFu << shift)) | ((p.kind >> 16) << shift);
  return i < 2u ? uint2(w, rank.y) : uint2(rank.x, w);
}

[numthreads(8, 8, 1)]
void CSScene(uint3 id : SV_DispatchThreadID)
{
  uint x0 = id.x * 4u + (gOrigin & 0xFFFFu);
  uint y = id.y + (gOrigin >> 16);
  if (x0 >= gWidth || y >= gRows)
    return;
  uint nAt = y * gWidth + x0;
  uint old = gFromTerrain != 0u ? 0u : gMap.Load(nAt);
  uint oldTouched = gFromTerrain != 0u ? 0u : gTouched.Load(nAt);
  uint bin = (y / 32u) * gBinsX + x0 / 32u;
  uint nStart = gBinListAt + FrameWord(bin);
  uint nEnd = gBinListAt + FrameWord(bin + 1u);
  uint nStale = 0;
  uint word = 0;
  uint touched = 0;
  uint under = gUnderAt != 0u ? gTouched.Load(gUnderAt + nAt) : 0u;
  uint panel = gUnderAt != 0u ? gTouched.Load(2u * gUnderAt + nAt) : 0u;
  uint2 rank = RankLoad(nAt);
  // the game's pixel each of the layer's is (gStep)
  uint gy = y * gStep + (gStep >> 1);
  for (uint i = 0; i < 4u; ++i) {
    uint gx = (x0 + i) * gStep + (gStep >> 1);
    uint v = gFromTerrain != 0u ? Terrain(gScrollX + gx, gScrollY + gy, nStale) : (old >> (i * 8u)) & 0xFFu;
    uint t = (oldTouched >> (i * 8u)) & 0xFFu;
    for (uint k = nStart; k < nEnd; ++k) {
      uint n = FrameWord(k);
      if (n < gFirst)
        continue;
      if (n >= gLast)
        break;
      Prim p = gPrims[n];
      uint before = v;
      v = Apply(p, int(gx), int(gy), gy * gViewW + gx, v);
      // a star leaves the pixel the terrain's, under the HD frames: the HD terrain draws its
      // own star field where it is clear (hd.hlsl, HdStars), and 1.17's stars land on every
      // pixel still 0, which is also under each sprite drawn in HD
      if (v == before || (p.kind & 0xFFu) == PRIM_FOG || (p.kind & 0xFFu) == PRIM_STAR)
        continue;
      if (RankKind(p))
        rank = RankSet(rank, i, p);
      uint fit = (p.kind & 0xFFu) == PRIM_BLIT && (p.param & 0xFFu) >= 2u ? p.param >> 8 : 0u;
      if (n < gOverFrom) {
        t |= 1u;
      } else if (fit != 0u && gUnderAt != 0u) {
        // a dialog's translucency, over the HD frames by its fit, unless something drawn
        // after them is under it; the pixel under the first of them kept, and the dialog's
        if ((t & 2u) == 0u) {
          if ((t & 4u) == 0u)
            under = (under & ~(0xFFu << (i * 8u))) | (before << (i * 8u));
          uint src = FrameByte(p.table + uint((int(gy) - p.y) * int(p.clip1 & 0xFFFFu) + (int(gx) - p.x)));
          panel = (panel & ~(0xFFu << (i * 8u))) | (src << (i * 8u));
          t = 5u | ((fit - 1u) << 3);
        }
      } else {
        t = 3u;
      }
    }
    word |= v << (i * 8u);
    touched |= t << (i * 8u);
  }
  gMap.Store(nAt, word);
  gTouched.Store(nAt, touched);
  if (gUnderAt != 0u) {
    gTouched.Store(gUnderAt + nAt, under);
    gTouched.Store(2u * gUnderAt + nAt, panel);
    gTouched.Store2(3u * gUnderAt + 2u * nAt, rank);
  }
  if (nStale != 0) {
    uint nOld;
    gStats.InterlockedAdd(0, nStale, nOld);
  }
}

// The layer's pixel a distortion takes for the game's pixel gx, gy of its box: the pixel
// nShift along the screen buffer from it, wrapping at the buffer's ends, in the layer.
uint DistortFrom(Prim p, uint gx, uint gy, uint nShift)
{
  int nSize = int(gViewW * gHeight);
  int nAt = int(gy * gViewW + gx);
  int nSrc = (p.kind & PRIM_MIRROR) ? nAt - int(nShift) : nAt + int(nShift);
  if (nSrc >= nSize)
    nSrc -= nSize;
  if (nSrc < 0)
    nSrc += nSize;
  return (uint(nSrc) / gViewW / gStep) * gWidth + (uint(nSrc) % gViewW) / gStep;
}

// The game's pixel of distortion p a pixel of the layer is, and its shift there, false where
// the frame is clear. Its words are from gFirst, gLast of them, its rows from gOrigin.
bool DistortAt(Prim p, int x, int gy, out uint gx, out uint nShift)
{
  gx = uint(x) * gStep + (gStep >> 1);
  nShift = 0u;
  int dx = int(gx) - p.x;
  if (dx < 0 || dx >= int(p.clip1 & 0xFFFFu))
    return false;
  uint s = PrimPixel(p, dx, gy - p.y);
  if ((s & 0x100u) == 0u)
    return false;
  s &= 0xFFu;
  nShift = p.table != NONE ? Table(p.table + s) : s;
  return true;
}

// One distortion (port_draw.cpp, DrawDistort): each opaque pixel of the frame takes
// the pixel a distance to its right, to its left when mirrored, the distance the
// frame's own pixel (the EMP shockwave) or its brightness (a cloaked unit), wrapping
// at the ends of the screen buffer.
[numthreads(8, 8, 1)]
void CSDistort(uint3 id : SV_DispatchThreadID)
{
  Prim p = gPrims[gPrim];
  int y = int(gOrigin + id.y);
  int x0 = (int(gFirst) + int(id.x)) * 4;
  int gy = y * int(gStep) + int(gStep >> 1);
  if (id.x >= gLast || y >= int(gRows) || x0 < 0 || x0 >= int(gWidth) || gy < p.y ||
      gy - p.y >= int(p.clip1 >> 16))
    return;
  uint nAt = uint(y) * gWidth + uint(x0);
  uint word = gPrev.Load(nAt);
  uint touched = gTouched.Load(nAt);
  for (uint i = 0; i < 4u; ++i) {
    uint gx, nShift;
    if (!DistortAt(p, x0 + int(i), gy, gx, nShift))
      continue;
    uint b = LoadByte(gPrev, DistortFrom(p, gx, uint(gy), nShift));
    word = (word & ~(0xFFu << (i * 8u))) | (b << (i * 8u));
    touched |= 1u << (i * 8u);
  }
  gMap.Store(nAt, word);
  gTouched.Store(nAt, touched);
}

// The pixels of the map's rows the screen takes (the console mask where the grid is
// dirty, vid_draw_mask) go from the map into the GPU's copy of the screen.
[numthreads(8, 8, 1)]
void CSShow(uint3 id : SV_DispatchThreadID)
{
  uint x0 = id.x * 4u;
  uint y = id.y;
  if (x0 >= gWidth || y >= gRows)
    return;
  uint nAt = y * gWidth + x0;
  uint map = gMap.Load(nAt);
  uint word = gShown.Load(nAt);
  for (uint i = 0; i < 4u; ++i)
    if (FrameByte(gCopiedAt + nAt + i) != 0u)
      word = (word & ~(0xFFu << (i * 8u))) | (map & (0xFFu << (i * 8u)));
  gShown.Store(nAt, word);
}

// ---------------------------------------------------------------------------
// The full colour (stage G, docs/renderer.md). CSSceneFull draws the map as CSScene does,
// the same bytes into gMap and gTouched, and beside them each pixel's colour: the palette's
// colour of its index, but a team pixel of a sprite in its slot's shade, a shadow scaling
// the colour under it by the fit port_gpu.cpp makes of its table, a translucent frame's
// change to the index carried onto the colour under it, and the fog's shade taken smoothly
// between its corners and applied as a colour matrix.

float3 Unpack(uint c)
{
  return float3(c & 0xFFu, (c >> 8) & 0xFFu, (c >> 16) & 0xFFu) / 255.0;
}

uint Pack(float3 c, uint sprite)
{
  uint3 u = uint3(saturate(c) * 255.0 + 0.5);
  return u.x | (u.y << 8) | (u.z << 16) | (sprite << 24);
}

float3 Pal(uint i)
{
  return Unpack(gPal[i >> 2][i & 3u]);
}

float3 Shade(uint slot, uint k)
{
  uint n = slot * 8u + k;
  return Unpack(gShade[n >> 2][n & 3u]);
}

// A primitive's extras, 32 bytes each (port_gpu.cpp, GpuFullExtras): its colour slot in
// the low byte and 0x100 where its remap's team colours are the slot's ramp; then four
// floats, a shadow's scale of each channel.
uint ExtraWord(uint n, uint k)
{
  return gFrame.Load(gExtraAt + n * 32u + k * 4u);
}

float4 ExtraFit(uint n)
{
  return float4(asfloat(ExtraWord(n, 4u)), asfloat(ExtraWord(n, 5u)), asfloat(ExtraWord(n, 6u)),
                asfloat(ExtraWord(n, 7u)));
}

// Colour s of primitive n's frame as its remap gives it, c: the slot's shade for a team
// colour when the remap's team colours are the slot's ramp, else c's colour.
float3 Remapped(uint n, uint s, uint c)
{
  uint e = ExtraWord(n, 0u);
  if ((e & 0x100u) != 0u && s >= 8u && s <= 15u)
    return Shade(e & 0xFFu, s - 8u);
  return Pal(c);
}

float3 FogRow(uint sprite, uint n, uint r)
{
  return sprite != 0u ? gFogSprite[n * 3u + r].xyz : gFogTerrain[n * 3u + r].xyz;
}

// The fog in full colour: the shade 1.17 steps across each 8x8 block (Fog), taken smoothly
// between the same corners, and the colour matrices at the two shades around it.
float3 FogColour(Prim p, uint x, uint y, float3 c, uint sprite)
{
  uint nAt = uint(p.x) + (y >> 3) * uint(p.y) + (x >> 3);
  float tl = float(FrameByte(nAt));
  float tr = float(FrameByte(nAt + 1u));
  float bl = float(FrameByte(nAt + uint(p.y)));
  float br = float(FrameByte(nAt + uint(p.y) + 1u));
  float fy = float(y & 7u) / 8.0;
  float fx = float((x & 7u) + 1u) / 8.0;
  float s = lerp(lerp(tl, bl, fy), lerp(tr, br, fy), fx);
  if (s >= 31.0)
    return c;
  if (s <= 0.0)
    return float3(0, 0, 0);
  uint n = min(uint(s), 30u);
  float t = s - float(n);
  float3 r0 = lerp(FogRow(sprite, n, 0u), FogRow(sprite, n + 1u, 0u), t);
  float3 r1 = lerp(FogRow(sprite, n, 1u), FogRow(sprite, n + 1u, 1u), t);
  float3 r2 = lerp(FogRow(sprite, n, 2u), FogRow(sprite, n + 1u, 2u), t);
  return saturate(float3(dot(r0, c), dot(r1, c), dot(r2, c)));
}

// ---------------------------------------------------------------------------
// Looks (port_looks.h): in full colour, a looked image's pixels drawn as the design
// draws a rich resource (local-design-center, 41-rich-bases/richfx.js), and a look's light on
// the ground and its plume. Each look's data is LOOK_BYTES at gLooksAt in gFrame: the tone of
// each palette entry its rule takes (0 none, else 1 + the tone * 254), the ramp as six float4s
// at 256, the clarity, shine, glow and sparkle at 352, the plume's height, density and rise at
// 368, the light's colour and strength at 384, and whether it is gas and in steps at 400.

static const uint LOOK_BYTES = 416u;
static const float2 LOOK_ART_LIGHT = float2(-0.62, -0.78);  // 1.17's art is lit from the top left
static const float3 LOOK_SUN = float3(-0.55, -0.62, 0.5596);
static const float LOOK_BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
static const int2 LOOK_RING[8] = {int2(1, 0), int2(1, 1), int2(0, 1), int2(-1, 1), int2(-1, 0), int2(-1, -1),
                                  int2(0, -1), int2(1, -1)};
// a star where a bright face catches the light: this pixel as an arm of each centre near it
static const float3 LOOK_STAR[9] = {float3(0, 0, 1), float3(1, 0, 0.6), float3(-1, 0, 0.6), float3(0, 1, 0.6),
                                    float3(0, -1, 0.6), float3(2, 0, 0.25), float3(-2, 0, 0.25), float3(0, 2, 0.25),
                                    float3(0, -2, 0.25)};

uint LookAt(uint look)
{
  return gLooksAt + ((look & 0xFFu) - 1u) * LOOK_BYTES;
}

// The tone of palette entry s for the look at at, -1 where its rule does not take it.
float LookTone(uint at, uint s)
{
  uint b = (gFrame.Load(at + (s & ~3u)) >> ((s & 3u) * 8u)) & 0xFFu;
  return b == 0u ? -1.0 : float(b - 1u) / 254.0;
}

float4 LookF4(uint at, uint off)
{
  return asfloat(gFrame.Load4(at + off));
}

float3 LookRampAt(uint at, uint i)
{
  return LookF4(at, 256u + i * 16u).xyz;
}

float3 LookRamp(uint at, float t)
{
  t = saturate(t) * 5.0;
  uint i = min(uint(t), 4u);
  return lerp(LookRampAt(at, i), LookRampAt(at, i + 1u), t - float(i));
}

float LookTime()
{
  return float(gTimeMs) / 1000.0;
}

float LookBayer(int2 p)
{
  uint2 q = uint2(p) & 3u;
  return (LOOK_BAYER[q.x + q.y * 4u] + 0.5) / 16.0;
}

float LookHash(float2 p)
{
  p = frac(p * float2(123.34, 456.21));
  p += dot(p, p + 45.32);
  return frac(p.x * p.y);
}

float LookNoise(float2 p)
{
  float2 i = floor(p);
  float2 f = frac(p);
  f = f * f * (3.0 - 2.0 * f);
  return lerp(lerp(LookHash(i), LookHash(i + float2(1, 0)), f.x), lerp(LookHash(i + float2(0, 1)), LookHash(i + float2(1, 1)), f.x),
              f.y);
}

float LookFbm(float2 p)
{
  float s = 0.0;
  float a = 0.5;
  for (int i = 0; i < 4; ++i) {
    s += a * LookNoise(p);
    p = p * 2.03 + float2(1.7, 9.2);
    a *= 0.5;
  }
  return s;
}

float LookLuma(uint s)
{
  return dot(Pal(s & 0xFFu), float3(0.299, 0.587, 0.114));
}

// The frame's own column and row of primitive p's pixel at dx, dy, mirrored as it is drawn.
int2 LookFramePos(Prim p, int dx, int dy)
{
  Entry entry = gEntries[p.entry];
  int col = int(p.clip0 & 0xFFFFu) + dx;
  if (p.kind & PRIM_MIRROR)
    col = int(entry.size & 0xFFFFu) - 1 - col;
  return int2(col, int(p.clip0 >> 16) + dy);
}

// The stars on a crystal at dx, dy: a few of its bright pixels, chosen by their place in the
// frame so a star stays on its face, flash now and then.
float LookSparkle(Prim p, uint at, int dx, int dy, float amount, float t)
{
  float s = 0.0;
  for (uint k = 0; k < 9u; ++k) {
    int qx = dx - int(LOOK_STAR[k].x);
    int qy = dy - int(LOOK_STAR[k].y);
    uint q = PrimPixel(p, qx, qy);
    if ((q & 0x100u) == 0u || LookTone(at, q & 0xFFu) < 0.55)
      continue;
    float2 own = float2(LookFramePos(p, qx, qy)) + 0.5 + float(p.entry & 0xFFu) * 0.37;
    if (LookHash(own) < 1.0 - 0.03 * amount)
      continue;
    s += pow(max(0.0, sin(t * 1.9 + LookHash(own + 17.0) * 6.283)), 28.0) * LOOK_STAR[k].z;
  }
  return s;
}

// Pixel dx, dy of looked primitive p whose frame colour is s: under is the colour beneath it
// and flat the one the 8-bit recolour gives.
float3 LookColour(Prim p, uint look, uint s, int dx, int dy, float3 under, float3 flat)
{
  uint at = LookAt(look);
  float4 mat = LookF4(at, 352u);  // clarity, shine, glow, sparkle
  bool gas = gFrame.Load(at + 400u) != 0u;
  float t = LookTime();
  float pulse = 0.8 + 0.2 * sin(t * 2.2);
  float3 glowCol = LookRampAt(at, 4u);
  // how deep in the frame it is, and how much of what is round it the look takes
  float nOpaque = 0.0;
  float nTaken = 0.0;
  for (uint r = 2u; r <= 8u; r += 3u)
    for (uint k = 0; k < 8u; ++k) {
      uint q = PrimPixel(p, dx + LOOK_RING[k].x * int(r), dy + LOOK_RING[k].y * int(r));
      if ((q & 0x100u) == 0u)
        continue;
      nOpaque += 1.0;
      if (LookTone(at, q & 0xFFu) >= 0.0)
        nTaken += 1.0;
    }
  float thick = saturate((nOpaque / 24.0 - 0.45) * 1.8);
  float tone = LookTone(at, s);
  if (tone < 0.0) {
    // the rock and the metal round it, lit by what glows beside them
    float g = nTaken / 24.0 * 1.3;
    return saturate(flat + (flat * glowCol * 1.2 + glowCol * 0.08) * g * mat.z);
  }
  // how it faces: a crystal's faces turned out from its middle and toward the art's light by
  // how bright each band of it was drawn; gas and metal by the slope of their brightness
  int2 fp = LookFramePos(p, dx, dy);
  float2 v;
  if (gas) {
    float l0 = LookLuma(s);
    uint a = PrimPixel(p, dx - 1, dy), b = PrimPixel(p, dx + 1, dy), c = PrimPixel(p, dx, dy - 1), d = PrimPixel(p, dx, dy + 1);
    float la = (a & 0x100u) != 0u ? LookLuma(a) : l0, lb = (b & 0x100u) != 0u ? LookLuma(b) : l0;
    float lc = (c & 0x100u) != 0u ? LookLuma(c) : l0, ld = (d & 0x100u) != 0u ? LookLuma(d) : l0;
    v = -float2(lb - la, ld - lc) * 0.5 * 2.8;
  } else {
    Entry entry = gEntries[p.entry];
    float2 size = float2(entry.size & 0xFFFFu, entry.size >> 16);
    float rad = max(4.0, 0.4 * min(size.x, size.y));
    float2 o = (float2(fp) + 0.5 - size * 0.5) / rad;
    float ol = length(o);
    if (ol > 1.0)
      o /= ol;
    float band = (floor(tone * 6.0) + 0.5) / 6.0;
    v = (o * 0.45 + LOOK_ART_LIGHT * clamp((band - 0.5) * 2.0, -1.0, 1.0) * 0.55) * 0.875;
  }
  float l2 = dot(v, v);
  if (l2 > 0.85) {
    v *= sqrt(0.85 / l2);
    l2 = 0.85;
  }
  float3 nrm = float3(v, sqrt(1.0 - l2));
  float relit = max(dot(nrm, LOOK_SUN), 0.0) / max(LOOK_SUN.z, 0.2);
  float3 col = LookRamp(at, tone) * lerp(1.0, 0.4 + 0.6 * relit, 0.55);
  if (!gas) {
    // the ground under it, deeper toward amber where it is thick
    float3 tint = LookRampAt(at, 3u);
    float3 absorb = exp(-(1.0 - tint) * 1.1 * (0.4 + thick * 2.6));
    float edge = pow(1.0 - saturate(nrm.z), 1.5) * 0.45;
    float3 through = under * absorb * lerp(float3(1, 1, 1), tint * 1.25, 0.3) + col * 0.3;
    col = lerp(col, through, saturate(mat.x * (1.0 - edge)));
    // a warm sky, more at faces turned away, glints toward the sun, and a sweep now and then
    float3 V = normalize(float3(0.0, 0.45, 1.0));
    float3 R = reflect(-V, nrm);
    float3 sky = lerp(LookRampAt(at, 5u), float3(1, 1, 1), 0.5);
    float3 ground = LookRampAt(at, 1u) * 0.6;
    float3 env = lerp(ground, sky, saturate(R.y * -0.5 + 0.55 + R.z * 0.25));
    float f = 0.04 + 0.96 * pow(1.0 - saturate(dot(nrm, V)), 5.0);
    float spec = pow(max(dot(R, LOOK_SUN), 0.0), 70.0) * 2.0;
    float sweep = smoothstep(0.06, 0.0, abs(frac((float(fp.x) + float(fp.y) * 0.6) / 150.0 - t * 0.16) - 0.5));
    col = lerp(col, env, saturate(mat.y * f * 0.9)) +
          (spec + sweep * 0.55) * mat.y * lerp(LookRampAt(at, 5u), float3(1, 1, 1), 0.6);
  }
  // light inside it, strongest at its heart
  col += glowCol * (0.25 + 0.75 * thick) * mat.z * (gas ? 0.6 : 0.45) * pulse;
  if (!gas)
    col += float3(1.0, 0.97, 0.86) * LookSparkle(p, at, dx, dy, mat.w, t) * mat.w * 1.6;
  return saturate(col);
}

int LookLow(uint u)
{
  return int(u << 16) >> 16;
}

int LookHigh(uint u)
{
  return int(u) >> 16;
}

// Past a crystal's edge, at dx, dy of its frame (the halo primitive carries the image's frame):
// the glow of the crystal near it over what is behind, as the design gathers it (richfx.js,
// gathered), in steps, and the arms of its stars that reach past the edge.
float3 LookHalo(Prim p, uint at, int dx, int dy, float3 c, bool steps, int2 mp)
{
  if ((PrimPixel(p, dx, dy) & 0x100u) != 0u)
    return c;
  float4 mat = LookF4(at, 352u);  // clarity, shine, glow, sparkle
  float acc = 0.0;
  float ws = 0.0;
  for (int j = -3; j <= 3; ++j)
    for (int i = -3; i <= 3; ++i) {
      float w = 1.0 / (1.0 + float(i * i + j * j));
      uint q = PrimPixel(p, dx + int(floor(float(i) * 1.5 + 0.5)), dy + int(floor(float(j) * 1.5 + 0.5)));
      if ((q & 0x100u) != 0u && LookTone(at, q & 0xFFu) >= 0.0)
        acc += w;
      ws += w;
    }
  float t = LookTime();
  float hl = acc / ws * mat.z * 1.5 * (0.8 + 0.2 * sin(t * 2.2));
  if (steps)
    hl = floor(hl * 6.0 + LookBayer(mp)) / 6.0;
  float3 glowCol = LookRampAt(at, 4u);
  float3 col = c * (1.0 + glowCol * hl * 0.9) + glowCol * hl * 0.1;
  col += float3(1.0, 0.97, 0.86) * LookSparkle(p, at, dx, dy, mat.w, t) * mat.w * 1.2;
  return saturate(col);
}

// A look primitive over pixel x, y of colour c: the light on the ground where nothing but the
// terrain is drawn yet, a crystal's halo, or a vent's plume over whatever is.
float3 LookPrim(Prim p, int x, int y, float3 c, uint sprite)
{
  if (gLooksAt == 0u)
    return c;
  uint at = LookAt(p.param);
  float2 centre = float2(LookLow(p.clip0), LookHigh(p.clip0));
  float t = LookTime();
  bool steps = gFrame.Load(at + 404u) != 0u;
  float2 px = float2(x, y);
  int2 mp = int2(x + int(gScrollX), y + int(gScrollY));  // the dither stays on the map as it scrolls
  float seed = float((p.param >> 16) & 0xFFu);
  uint what = (p.param >> 8) & 0xFFu;
  if (what == 3u) {
    int reach = int((p.param >> 16) & 0xFFu);
    return LookHalo(p, at, x - p.x - reach, y - p.y - reach, c, steps, mp);
  }
  if (what == 1u) {
    if (sprite != 0u)
      return c;
    float r = float(p.entry & 0xFFFFu);
    float4 light = LookF4(at, 384u);
    float2 d = (px - centre) / float2(r, r * 0.55);
    float3 acc = light.rgb * exp(-dot(d, d) * 2.0) * (float(p.entry >> 16) / 1000.0) * light.w *
                 (0.88 + 0.12 * sin(t * 2.2 + seed * 1.3));
    float m = max(acc.r, max(acc.g, acc.b));
    if (steps && m > 0.0)
      acc *= (floor(m * 8.0 + LookBayer(mp)) / 8.0) / m;
    // the faint edge of a light adds little to black ground, so its steps do not show there
    return saturate(c * (1.0 + acc * 1.3) + acc * acc * 0.12);
  }
  // a column from the vent, widening and drifting as it rises, broken by noise that rises with it;
  // swollen higher, wider and denser while 1.17's puff there plays (port_looks.cpp, PuffSwell)
  float4 plume = LookF4(at, 368u);  // height, density, rise
  float strength = float(p.entry & 0xFFFFu) / 1000.0;
  float width = float(p.entry >> 16) / 100.0;
  float swell = float(p.param >> 24) / 255.0;
  float h = centre.y - px.y;
  if (h < -2.0)
    return c;
  float top = max(8.0, plume.x * (0.45 + 0.55 * strength)) * (1.0 + 0.5 * swell);
  float k = saturate(h / top);
  float sway = sin(h * 0.055 - t * 1.2 + seed * 1.9) * h * 0.09 + h * 0.16;
  float w = width * (1.0 + h * 0.075) * (1.0 + 0.5 * swell);
  float dx = (px.x - centre.x - sway) / w;
  float n = LookFbm(float2((px.x - sway) * 0.12 + seed * 7.3, (px.y + t * plume.z) * 0.1));
  float dd = max(0.0, exp(-dx * dx * 1.4) - 0.1) * (1.0 - k) * (1.0 - k) * smoothstep(-2.0, 2.5, h) * (0.5 + n);
  float a = saturate((dd * plume.y * (0.5 + 0.5 * strength) * (1.0 + 0.6 * swell) - 0.06) * 1.15);
  float core = dd * exp(-max(h, 0.0) * 0.07) * strength * (1.0 + 0.5 * swell);
  if (steps)
    a = floor(a * 4.0 + LookBayer(mp)) / 4.0;
  float3 col = lerp(LookRampAt(at, 2u), LookRampAt(at, 4u), saturate(core * 0.8 + a * 0.35));
  return lerp(c, col, min(a, 0.9));
}

// Primitive p, the n-th, over pixel x, y in full colour: c the colour so far, v the index
// before it and vAfter the one Apply gives. sprite becomes 1 where a sprite or a line draws.
float3 ApplyColour(Prim p, uint n, int x, int y, uint nAt, uint v, uint vAfter, float3 c, inout uint sprite)
{
  uint kind = p.kind & 0xFFu;
  if (kind == PRIM_FOG)
    return FogColour(p, uint(x), uint(y), c, sprite);
  if (kind == PRIM_SPAN) {
    int d = int(nAt) - p.x;
    if (d < 0 || (uint(d) % p.clip0) != 0u || uint(d) / p.clip0 >= uint(p.y))
      return c;
    sprite = 1u;
    return Pal(vAfter);
  }
  int cw = int(p.clip1 & 0xFFFFu);
  int ch = int(p.clip1 >> 16);
  int dx = x - p.x;
  int dy = y - p.y;
  if (dx < 0 || dy < 0 || dx >= cw || dy >= ch)
    return c;
  if (kind == PRIM_LOOK)
    return LookPrim(p, x, y, c, sprite);
  if (kind == PRIM_STAR)
    return v == 0u ? Pal(vAfter) : c;
  if (kind == PRIM_BLIT) {
    if ((p.param & 0xFFu) == 1u && FrameByte(p.table + uint(dy * cw + dx)) == 0u)
      return c;
    sprite = 1u;
    return Pal(vAfter);
  }
  uint s = PrimPixel(p, dx, dy);
  if ((s & 0x100u) == 0u)
    return c;
  s &= 0xFFu;
  if (kind == PRIM_NORMAL) {
    // a looked image, and 1.17's puffs where the look's plume replaces them
    uint look = gLooksAt != 0u ? ExtraWord(n, 1u) : 0u;
    if ((look & 0x100u) != 0u)
      return c;
    sprite = 1u;
    if (look != 0u)
      return LookColour(p, look, s, dx, dy, c, Remapped(n, s, vAfter));
    return Remapped(n, s, vAfter);
  }
  if (kind == PRIM_SHADOW || kind == PRIM_FADE)
    return saturate(c * ExtraFit(n).xyz);
  if (kind == PRIM_EFFECT) {
    // 1.17's own table on the index, its change carried onto the colour under: exact where
    // that colour is the palette's, and a team shade under it keeps its detail. No blend of
    // the frame's colour fits these tables, since a remapped light's frame (the SCV's
    // engine, the warp-in's glow) holds levels into its table, not colours.
    sprite = 1u;
    return saturate(c + Pal(vAfter) - Pal(v));
  }
  if (kind == PRIM_UNCLOAKED) {
    uint r = Table(p.table + s);
    if (SignedByte(Table((p.param >> 8) + r)) <= SignedByte(p.param & 0xFFu))
      return c;
    sprite = 1u;
    return Remapped(n, s, r);
  }
  if (kind == PRIM_GLYPH || kind == PRIM_WARP) {
    sprite = 1u;
    return Pal(vAfter);
  }
  return c;
}

[numthreads(8, 8, 1)]
void CSSceneFull(uint3 id : SV_DispatchThreadID)
{
  uint x0 = id.x * 4u + (gOrigin & 0xFFFFu);
  uint y = id.y + (gOrigin >> 16);
  if (x0 >= gWidth || y >= gRows)
    return;
  uint nAt = y * gWidth + x0;
  uint old = gFromTerrain != 0u ? 0u : gMap.Load(nAt);
  uint oldTouched = gFromTerrain != 0u ? 0u : gTouched.Load(nAt);
  uint bin = (y / 32u) * gBinsX + x0 / 32u;
  uint nStart = gBinListAt + FrameWord(bin);
  uint nEnd = gBinListAt + FrameWord(bin + 1u);
  uint nStale = 0;
  uint word = 0;
  uint touched = 0;
  uint2 rank = RankLoad(nAt);
  uint gy = y * gStep + (gStep >> 1);
  for (uint i = 0; i < 4u; ++i) {
    uint gx = (x0 + i) * gStep + (gStep >> 1);
    uint v = gFromTerrain != 0u ? Terrain(gScrollX + gx, gScrollY + gy, nStale) : (old >> (i * 8u)) & 0xFFu;
    uint t = (oldTouched >> (i * 8u)) & 0xFFu;
    uint rgb = gFromTerrain != 0u ? 0u : gMapRgb.Load((nAt + i) * 4u);
    float3 c = gFromTerrain != 0u ? Pal(v) : Unpack(rgb);
    uint sprite = gFromTerrain != 0u ? 0u : rgb >> 24;
    for (uint k = nStart; k < nEnd; ++k) {
      uint n = FrameWord(k);
      if (n < gFirst)
        continue;
      if (n >= gLast)
        break;
      Prim p = gPrims[n];
      uint before = v;
      v = Apply(p, int(gx), int(gy), gy * gViewW + gx, v);
      c = ApplyColour(p, n, int(gx), int(gy), gy * gViewW + gx, before, v, c, sprite);
      if (v == before || (p.kind & 0xFFu) == PRIM_FOG || (p.kind & 0xFFu) == PRIM_STAR)
        continue;
      if (RankKind(p))
        rank = RankSet(rank, i, p);
      t |= n < gOverFrom ? 1u : 3u;
    }
    word |= v << (i * 8u);
    touched |= t << (i * 8u);
    gMapRgb.Store((nAt + i) * 4u, Pack(c, sprite));
  }
  gMap.Store(nAt, word);
  gTouched.Store(nAt, touched);
  if (gUnderAt != 0u)
    gTouched.Store2(3u * gUnderAt + 2u * nAt, rank);
  if (nStale != 0) {
    uint nOld;
    gStats.InterlockedAdd(0, nStale, nOld);
  }
}

// A distortion in full colour: as CSDistort, the colour from the same pixel of the map's
// colours before it.
[numthreads(8, 8, 1)]
void CSDistortFull(uint3 id : SV_DispatchThreadID)
{
  Prim p = gPrims[gPrim];
  int y = int(gOrigin + id.y);
  int x0 = (int(gFirst) + int(id.x)) * 4;
  int gy = y * int(gStep) + int(gStep >> 1);
  if (id.x >= gLast || y >= int(gRows) || x0 < 0 || x0 >= int(gWidth) || gy < p.y ||
      gy - p.y >= int(p.clip1 >> 16))
    return;
  uint nAt = uint(y) * gWidth + uint(x0);
  uint word = gPrev.Load(nAt);
  uint touched = gTouched.Load(nAt);
  for (uint i = 0; i < 4u; ++i) {
    uint gx, nShift;
    if (!DistortAt(p, x0 + int(i), gy, gx, nShift))
      continue;
    uint nSrc = DistortFrom(p, gx, uint(gy), nShift);
    uint b = LoadByte(gPrev, nSrc);
    word = (word & ~(0xFFu << (i * 8u))) | (b << (i * 8u));
    touched |= 1u << (i * 8u);
    gMapRgb.Store((nAt + i) * 4u, (gPrevRgb.Load(nSrc * 4u) & 0xFFFFFFu) | (1u << 24));
  }
  gMap.Store(nAt, word);
  gTouched.Store(nAt, touched);
}

// CSShow in full colour: the screen's copy of the colours too.
[numthreads(8, 8, 1)]
void CSShowFull(uint3 id : SV_DispatchThreadID)
{
  uint x0 = id.x * 4u;
  uint y = id.y;
  if (x0 >= gWidth || y >= gRows)
    return;
  uint nAt = y * gWidth + x0;
  uint map = gMap.Load(nAt);
  uint word = gShown.Load(nAt);
  for (uint i = 0; i < 4u; ++i)
    if (FrameByte(gCopiedAt + nAt + i) != 0u) {
      word = (word & ~(0xFFu << (i * 8u))) | (map & (0xFFu << (i * 8u)));
      gShownRgb.Store((nAt + i) * 4u, gMapRgb.Load((nAt + i) * 4u));
    }
  gShown.Store(nAt, word);
}

// ---------------------------------------------------------------------------
// With the camera zoomed out (port_view.cpp's render view), the map layer above is
// the render view's, and CSHud draws what was drawn over the map at the screen's size (the
// dialogs, the map's text, the drag box, the cursor): the primitives gFirst up to gLast,
// each square's listed from word gPrim of the frame, over the map at each pixel the screen
// takes (the render view's pixel nearest the camera's), into the GPU's copy of the screen.
// gHud says where they drew, which the presenter shows in place of the zoomed map.
[numthreads(8, 8, 1)]
void CSHud(uint3 id : SV_DispatchThreadID)
{
  uint x0 = id.x * 4u;
  uint y = id.y;
  if (x0 >= gWidth || y >= gRows)
    return;
  uint nAt = y * gWidth + x0;
  uint bin = (y / 32u) * gBinsX + x0 / 32u;
  uint nStart = gBinListAt + FrameWord(gPrim + bin);
  uint nEnd = gBinListAt + FrameWord(gPrim + bin + 1u);
  uint word = gShown.Load(nAt);
  uint hud = 0u;
  for (uint i = 0; i < 4u; ++i) {
    uint x = x0 + i;
    if (FrameByte(gCopiedAt + nAt + i) == 0u)
      continue;
    int mx = int(floor(gCamX + (float(x) + 0.5) * gZoom)) - int(gMapX);
    int my = int(floor(gCamY + (float(y) + 0.5) * gZoom)) - int(gMapY);
    uint v = 0u;
    uint rgb = 0u;
    if (mx >= 0 && my >= 0 && uint(mx) < gMapW && uint(my) < gMapH) {
      uint m = (uint(my) / gMapStep) * gMapStride + uint(mx) / gMapStep;
      v = (gMap.Load(m & ~3u) >> ((m & 3u) * 8u)) & 0xFFu;
      if (gHudFull != 0u)
        rgb = gMapRgb.Load(m * 4u);
    }
    bool drew = false;
    for (uint k = nStart; k < nEnd; ++k) {
      uint n = FrameWord(k);
      if (n < gFirst)
        continue;
      if (n >= gLast)
        break;
      uint before = v;
      v = Apply(gPrims[n], int(x), int(y), nAt + i, v);
      drew = drew || v != before;
    }
    word = (word & ~(0xFFu << (i * 8u))) | (v << (i * 8u));
    if (drew)
      hud |= 1u << (i * 8u);
    if (gHudFull != 0u)
      gShownRgb.Store((nAt + i) * 4u, drew ? Pack(Pal(v), 1u) : rgb);
  }
  gShown.Store(nAt, word);
  gHud.Store(nAt, hud);
}
