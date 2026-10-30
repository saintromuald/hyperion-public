// port_gpu_scene.cpp -- the scene (R4b) and the full colour (stage G): the frame description into the
// GPU's map, its check against the game's, and the screen's copy (docs/renderer.md). Presentation only.
#include <windows.h>
#include <limits.h>
#include <stdio.h>
#include <algorithm>
#include <vector>
#include "port_gpu_internal.h"
#include "../../h/playerlimits.h"  // PLAYER_SLOTS_MAX
#include "port_teamcolor.h"  // PortTeamRamp, PortTeamShades, PortTeamSlots
#include "CSScene.dxil.h"
#include "CSScene.spv.h"
#include "CSDistort.dxil.h"
#include "CSDistort.spv.h"
#include "CSShow.dxil.h"
#include "CSShow.spv.h"
#include "CSSceneFull.dxil.h"
#include "CSSceneFull.spv.h"
#include "CSDistortFull.dxil.h"
#include "CSDistortFull.spv.h"
#include "CSShowFull.dxil.h"
#include "CSShowFull.spv.h"
#include "CSHud.dxil.h"
#include "CSHud.spv.h"
#include "CSFogLight.dxil.h"
#include "CSFogLight.spv.h"
#if defined(__APPLE__)
#include "CSScene.msl.h"
#include "CSDistort.msl.h"
#include "CSShow.msl.h"
#include "CSSceneFull.msl.h"
#include "CSDistortFull.msl.h"
#include "CSShowFull.msl.h"
#include "CSHud.msl.h"
#include "CSFogLight.msl.h"
#endif

// The scene's uniforms (scene.hlsl, SceneParams).
struct SceneParams {
  Uint32 nWidth, nRows, nScrollX, nScrollY;
  Uint32 nRingX, nRingY, nHeight, nBinsX;
  Uint32 nFirst, nLast, nFromTerrain, nPrim;
  Uint32 nCellsAt, nBinListAt, nBytesAt, nCopiedAt;
  Uint32 nOverFrom, nUnderAt, nExtraAt, nOrigin;  // nOrigin: scene.hlsl's gOrigin
  Uint32 nStep, nViewW;  // the layer's step, and the render view's width
  Uint32 nLooksAt, nTimeMs;  // the looks' data in the frame's store, 0 for none, and the time
};
// CSHud's uniforms (scene.hlsl, HudParams).
struct HudParams {
  float fCamX, fCamY, fZoom;
  Uint32 nFull;
  Uint32 nMapX, nMapY, nMapW, nMapH;
  Uint32 nStep, nStride, anUnused[2];
};

// The full colour's uniforms (scene.hlsl, FullParams and FogParams; stage G): the palette as
// shown and each colour slot's eight team shades, RGBA8 a word, and apart from them the
// fog's colour matrices at each of its 32 shades, three rows of four, for the sprites and
// for the terrain. Each block stays within the 4 KiB SDL's Vulkan binds of one.
enum { FULL_SHADES = 544 };  // scene.hlsl's gShade, uint4s of 4 words
static_assert(FULL_SHADES >= PLAYER_SLOTS_MAX * 8 && FULL_SHADES % 4 == 0,
              "FullParams' shades hold 8 for every slot, and scene.hlsl's gShade is FULL_SHADES / 4");
struct FullParams {
  Uint32 anPal[256];
  Uint32 anShade[FULL_SHADES];
};
struct FogParams {
  float afFogSprite[32 * 12];
  float afFogTerrain[32 * 12];
};
static_assert(sizeof(FullParams) <= GPU_UNIFORM_BLOCK_MAX && sizeof(FogParams) <= GPU_UNIFORM_BLOCK_MAX,
              "a uniform block past 4 KiB reads as zeros on Vulkan");
static FullParams g_full;
static FogParams g_fullFog;

// The scene's pipelines, the description's buffers and what the GPU has of them.
static SDL_GPUComputePipeline *g_scenePipeline;
static SDL_GPUComputePipeline *g_distortPipeline;
static SDL_GPUComputePipeline *g_showPipeline;
static SDL_GPUComputePipeline *g_sceneFullPipeline;  // the full colour's (stage G)
static SDL_GPUComputePipeline *g_distortFullPipeline;
static SDL_GPUComputePipeline *g_showFullPipeline;
static SDL_GPUComputePipeline *g_fogLightPipeline;  // the fog's light map (fog.hlsl)
static SDL_GPUTransferBuffer *g_sceneUp;
static Uint32 g_nSceneUp;
static SDL_GPUTransferBuffer *g_sceneDown;
static Uint32 g_nSceneDown;
static unsigned int g_nSceneTileset;  // the tileset and atlas the GPU has
static unsigned int g_nSceneAtlasSerial;
static unsigned int g_nSceneEntries;
static unsigned int g_nScenePixels;
static unsigned int g_nSceneTables;
// Which ring's slots the slots' store holds whole and at what size (SceneFrame's
// nSlotsRing), so that a frame uploads only those written since; -1 for none, until a frame is
// drawn.
static int g_nSceneSlotsRing = -1;
static Uint32 g_nSceneSlotsBytes;
// The scene's check counts.
static ULONG g_nSceneChecked;
static ULONG g_nSceneMismatched;
static unsigned long long g_nSceneBadPixels;
static unsigned long long g_nSceneStale;
static unsigned long long g_nSceneBelow;  // SCENE_SCREEN: pixels the screen took below the map
static ULONG g_nSceneZoomed;      // frames checked through the camera's render view
static char g_szSceneFirst[200];
// How many primitives of each kind (ScenePrimKind, plain and mirrored) the check has
// covered, for the log.
static unsigned long long g_nSceneKinds[16][2];

// The state the other parts share (port_gpu_internal.h).
SDL_GPUComputePipeline *g_hudPipeline;
GpuZoomShown g_zoomShown;
GpuStore g_sceneStore[SCENE_STORES];
float g_fFull = 1.0f;

void PortGpuFullColourSet(bool bOn)
{
  g_fFull = bOn ? 1.0f : 0.0f;
}
bool g_bFullShown;
bool g_bHdMixed;

// The scene (R4b).

// The scene's shaders (scene.hlsl) all bind the same buffers and the same four blocks of
// uniforms (GpuSceneDispatch pushes each).
static SDL_GPUComputePipeline *GpuComputePipeline(const Uint8 *pDxil, size_t nDxil, const Uint8 *pSpv,
                                                  size_t nSpv, const Uint8 *pMsl, const char *pszEntry,
                                                  Uint32 nUniforms = 4, Uint32 nReadOnly = SCENE_STORE_READ,
                                                  Uint32 nReadWrite = SCENE_STORES - SCENE_STORE_READ)
{
  SDL_GPUComputePipelineCreateInfo info;
  SDL_zero(info);
  char *pszMsl = NULL;
  const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(g_device);
  if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
    info.code = pDxil;
    info.code_size = nDxil;
    info.format = SDL_GPU_SHADERFORMAT_DXIL;
  } else if (pMsl != NULL && (formats & SDL_GPU_SHADERFORMAT_MSL) &&
             (pszMsl = GpuMslSource(pMsl, 0, nReadOnly, nUniforms)) != NULL) {
    info.code = (const Uint8 *)pszMsl;
    info.code_size = SDL_strlen(pszMsl);
    info.format = SDL_GPU_SHADERFORMAT_MSL;
  } else {
    info.code = pSpv;
    info.code_size = nSpv;
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
  }
  info.entrypoint = pszEntry;
  info.num_readonly_storage_buffers = nReadOnly;
  info.num_readwrite_storage_buffers = nReadWrite;
  info.num_uniform_buffers = nUniforms;
  info.threadcount_x = 8;
  info.threadcount_y = 8;
  info.threadcount_z = 1;
  SDL_GPUComputePipeline *pPipeline = SDL_CreateGPUComputePipeline(g_device, &info);
  SDL_free(pszMsl);
  return pPipeline;
}

// The scene's compute pipelines; false when the scene's, the distortion's or the show's
// failed, which the presenter cannot draw without.
bool GpuSceneInit(void)
{
  g_scenePipeline = GpuComputePipeline(GPU_CODE(CSScene), "CSScene");
  g_distortPipeline = GpuComputePipeline(GPU_CODE(CSDistort), "CSDistort");
  g_showPipeline = GpuComputePipeline(GPU_CODE(CSShow), "CSShow");
  // The full colour (stage G); without its pipelines the map is drawn as 1.17 draws it.
  g_sceneFullPipeline = GpuComputePipeline(GPU_CODE(CSSceneFull), "CSSceneFull");
  g_distortFullPipeline = GpuComputePipeline(GPU_CODE(CSDistortFull), "CSDistortFull");
  g_showFullPipeline = GpuComputePipeline(GPU_CODE(CSShowFull), "CSShowFull");
  // the camera zoomed out; without it the zoom holds at 1
  g_hudPipeline = GpuComputePipeline(GPU_CODE(CSHud), "CSHud");
  // the fog's light map; without it the software makes it, as 1.17 does
  g_fogLightPipeline = GpuComputePipeline(GPU_CODE(CSFogLight), "CSFogLight", 1, 0, 1);
  PortSceneFogGpu(g_fogLightPipeline != NULL && !SDL_GetHintBoolean("HYPERION_FOG_CPU", false));
  return g_scenePipeline != NULL && g_distortPipeline != NULL && g_showPipeline != NULL;
}

void GpuSceneRelease(void)
{
  for (int i = 0; i < SCENE_STORES; ++i) {
    if (g_sceneStore[i].pBuffer != NULL)
      SDL_ReleaseGPUBuffer(g_device, g_sceneStore[i].pBuffer);
    g_sceneStore[i].pBuffer = NULL;
    g_sceneStore[i].nCapacity = 0;
  }
  if (g_sceneUp != NULL)
    SDL_ReleaseGPUTransferBuffer(g_device, g_sceneUp);
  if (g_sceneDown != NULL)
    SDL_ReleaseGPUTransferBuffer(g_device, g_sceneDown);
  g_sceneUp = NULL;
  g_sceneDown = NULL;
  g_nSceneUp = 0;
  g_nSceneDown = 0;
  g_nSceneTileset = 0;
  g_nSceneAtlasSerial = 0;
  SDL_GPUComputePipeline **app[] = {&g_scenePipeline, &g_distortPipeline, &g_showPipeline, &g_sceneFullPipeline,
                                    &g_distortFullPipeline, &g_showFullPipeline, &g_hudPipeline, &g_fogLightPipeline};
  for (SDL_GPUComputePipeline **pp : app) {
    if (*pp != NULL)
      SDL_ReleaseGPUComputePipeline(g_device, *pp);
    *pp = NULL;
  }
  g_zoomShown.bOn = false;
  g_bFullShown = false;
  PortSceneFogGpu(false);
}

// The scene's check counts, and the primitives of each kind it covered.
void GpuSceneWriteLog(FILE *f)
{
  if (g_nSceneChecked != 0) {
    static const char *const s_apszKind[16] = {"?", "normal", "shadow", "effect", "fade", "uncloaked", "warp",
                                               "span", "distort", "star", "fog", "glyph", "blit", "?", "?", "?"};
    fprintf(f, "scene frames=%lu mismatched=%lu pixels=%llu stale=%llu below=%llu zoomed=%lu%s%s\n",
            g_nSceneChecked, g_nSceneMismatched, g_nSceneBadPixels, g_nSceneStale, g_nSceneBelow, g_nSceneZoomed,
            g_szSceneFirst[0] ? " first=" : "", g_szSceneFirst);
    fprintf(f, "scene kinds");
    for (int i = 0; i < 16; ++i)
      if (g_nSceneKinds[i][0] != 0 || g_nSceneKinds[i][1] != 0)
        fprintf(f, " %s=%llu+%llu", s_apszKind[i], g_nSceneKinds[i][0], g_nSceneKinds[i][1]);
    fprintf(f, " (plain+mirrored)\n");
  }
}

// A storage buffer of at least nBytes. *pbFresh says it was made again, empty.
static bool GpuStoreFit(GpuStore *pStore, Uint32 nBytes, SDL_GPUBufferUsageFlags usage, bool *pbFresh)
{
  Uint32 nCapacity;
  SDL_GPUBufferCreateInfo info;

  *pbFresh = false;
  if (nBytes < 256)
    nBytes = 256;
  if (pStore->pBuffer != NULL && pStore->nCapacity >= nBytes)
    return true;
  nCapacity = pStore->nCapacity != 0 ? pStore->nCapacity : 65536;
  while (nCapacity < nBytes)
    nCapacity *= 2;
  if (pStore->pBuffer != NULL)
    SDL_ReleaseGPUBuffer(g_device, pStore->pBuffer);
  SDL_zero(info);
  info.usage = usage;
  info.size = nCapacity;
  pStore->pBuffer = SDL_CreateGPUBuffer(g_device, &info);
  pStore->nCapacity = pStore->pBuffer != NULL ? nCapacity : 0;
  *pbFresh = true;
  return pStore->pBuffer != NULL;
}

// A transfer buffer of at least nBytes.
static bool GpuTransferFit(SDL_GPUTransferBuffer **ppBuffer, Uint32 *pnCapacity, Uint32 nBytes,
                           SDL_GPUTransferBufferUsage usage)
{
  Uint32 nCapacity;

  if (*ppBuffer != NULL && *pnCapacity >= nBytes)
    return true;
  nCapacity = *pnCapacity != 0 ? *pnCapacity : 65536;
  while (nCapacity < nBytes)
    nCapacity *= 2;
  if (*ppBuffer != NULL)
    SDL_ReleaseGPUTransferBuffer(g_device, *ppBuffer);
  *ppBuffer = GpuTransfer(usage, nCapacity);
  *pnCapacity = *ppBuffer != NULL ? nCapacity : 0;
  return *ppBuffer != NULL;
}

// The GPU's copy of the screen, nBytes of it, read back and waited for.
bool PortGpuReadShown(Uint8 *pOut, Uint32 nBytes)
{
  SDL_GPUCommandBuffer *pCmd;
  SDL_GPUCopyPass *pCopy;
  SDL_GPUBufferRegion from;
  SDL_GPUTransferBufferLocation to;
  SDL_GPUFence *pFence;
  const Uint8 *pGot;

  if (g_device == NULL || g_sceneStore[SCENE_STORE_SHOWN].pBuffer == NULL ||
      g_sceneStore[SCENE_STORE_SHOWN].nCapacity < nBytes ||
      !GpuTransferFit(&g_sceneDown, &g_nSceneDown, nBytes, SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD))
    return false;
  // the frame's buffer, with the scene that drew the copy in it
  pCmd = GpuFrameCmdTake();
  if (pCmd == NULL)
    return false;
  pCopy = SDL_BeginGPUCopyPass(pCmd);
  from.buffer = g_sceneStore[SCENE_STORE_SHOWN].pBuffer;
  from.offset = 0;
  from.size = nBytes;
  to.transfer_buffer = g_sceneDown;
  to.offset = 0;
  SDL_DownloadFromGPUBuffer(pCopy, &from, &to);
  SDL_EndGPUCopyPass(pCopy);
  pFence = SDL_SubmitGPUCommandBufferAndAcquireFence(pCmd);
  if (pFence == NULL)
    return false;
  SDL_WaitForGPUFences(g_device, true, &pFence, 1);
  SDL_ReleaseGPUFence(g_device, pFence);
  pGot = static_cast<const Uint8 *>(SDL_MapGPUTransferBuffer(g_device, g_sceneDown, false));
  if (pGot == NULL)
    return false;
  SDL_memcpy(pOut, pGot, nBytes);
  SDL_UnmapGPUTransferBuffer(g_device, g_sceneDown);
  return true;
}

void PortGpuSceneCounts(ULONG *pnChecked, ULONG *pnMismatched)
{
  *pnChecked = g_nSceneChecked;
  *pnMismatched = g_nSceneMismatched;
}

// The GPU's map against the game's; with the camera's render view, the GPU's copy
// of the screen's map rows, the map seen through the camera with the dialogs over it.
static void GpuSceneCompare(const SceneFrame *pFrame)
{
  const Uint8 *pGot = static_cast<const Uint8 *>(SDL_MapGPUTransferBuffer(g_device, g_sceneDown, false));
  const int nCompareW = pFrame->bZoom ? pFrame->nScreenW : pFrame->nWidth;
  const int nCompareRows = pFrame->bZoom ? pFrame->nScreenRows : pFrame->nRows;
  const int nCompareH = pFrame->bZoom ? pFrame->nScreenH : pFrame->nHeight;
  const Uint32 nMap = static_cast<Uint32>(nCompareW * nCompareRows);
  ULONG nBad = 0;
  Uint32 nStale;
  Uint32 i;

  if (pGot == NULL)
    return;
  SDL_memcpy(&nStale, pGot + ((nMap + 15) & ~15u), sizeof nStale);
  // At SCENE_SCREEN only the pixels the screen takes count; those below the map's rows
  // are the console's, which the GPU does not draw, and are counted apart.
  const Uint8 *pCopied = pFrame->nStage == SCENE_SCREEN ? pFrame->pCopied : NULL;
  if (pCopied != NULL)
    for (i = nMap; i < static_cast<Uint32>(nCompareW * nCompareH); ++i)
      g_nSceneBelow += pCopied[i] != 0;
  for (i = 0; i < nMap; ++i) {
    if (pCopied != NULL && pCopied[i] == 0)
      continue;
    if (pGot[i] != pFrame->pGame[i]) {
      if (nBad == 0 && g_nSceneMismatched == 0)
        SDL_snprintf(g_szSceneFirst, sizeof g_szSceneFirst,
                     "frame %lu at %u,%u (map %d,%d): the game has %u, the GPU %u", g_nSceneChecked,
                     i % nCompareW, i / nCompareW, pFrame->nGameScrollX + static_cast<int>(i % nCompareW),
                     pFrame->nGameScrollY + static_cast<int>(i / nCompareW), pFrame->pGame[i], pGot[i]);
      ++nBad;
    }
  }
  // With HYPERION_GPU_SCENE_DUMP set, the first frames that differ are written out as
  // they are, the game's and the GPU's, for a look, and so is the frame it names.
  const char *pszDump = SDL_getenv("HYPERION_GPU_SCENE_DUMP");
  if (pszDump != NULL && ((nBad != 0 && g_nSceneMismatched < 4) ||
                          SDL_strtoul(pszDump, NULL, 10) == g_nSceneChecked)) {
    char szName[64];
    SDL_snprintf(szName, sizeof szName, "scene_%lu_%dx%d_game.raw", g_nSceneChecked, nCompareW, nCompareRows);
    if (FILE *f = fopen(szName, "wb")) {
      fwrite(pFrame->pGame, 1, nMap, f);
      fclose(f);
    }
    SDL_snprintf(szName, sizeof szName, "scene_%lu_%dx%d_gpu.raw", g_nSceneChecked, nCompareW, nCompareRows);
    if (FILE *f = fopen(szName, "wb")) {
      fwrite(pGot, 1, nMap, f);
      fclose(f);
    }
    // The primitives too, as the description has them.
    SDL_snprintf(szName, sizeof szName, "scene_%lu_prims.bin", g_nSceneChecked);
    if (FILE *f = fopen(szName, "wb")) {
      if (pFrame->nPrims != 0)
        fwrite(pFrame->pPrims, sizeof(ScenePrim), pFrame->nPrims, f);
      fclose(f);
    }
  }
  SDL_UnmapGPUTransferBuffer(g_device, g_sceneDown);
  for (i = 0; i < pFrame->nPrims; ++i)
    ++g_nSceneKinds[pFrame->pPrims[i].nKind & 15][(pFrame->pPrims[i].nKind & SCENE_PRIM_MIRROR) != 0];
  ++g_nSceneChecked;
  g_nSceneZoomed += pFrame->bZoom != 0;
  g_nSceneStale += nStale;
  if (nBad != 0) {
    ++g_nSceneMismatched;
    g_nSceneBadPixels += nBad;
  }
  if (g_nSceneChecked <= 16 || (g_nSceneChecked & 255) == 0 || (nBad != 0 && g_nSceneMismatched <= 16))
    GpuWriteLog();
}

// ---------------------------------------------------------------------------
// The full colour (stage G; docs/renderer.md). CSSceneFull draws the map's colours beside
// its indices, from the palette, the colour slots' team shades (port_teamcolor.cpp) and
// fits of 1.17's tables, which are made here: each primitive's extras, the fog's matrices.

// HYPERION_GPU_CHECK compares the frame with 1.17's, so the full colour is off under it.
static bool GpuCheckEnv(void)
{
  static int s_nCheck = -1;
  if (s_nCheck < 0) {
    const char *psz = SDL_getenv("HYPERION_GPU_CHECK");
    s_nCheck = psz != NULL && psz[0] != '\0' && psz[0] != '0' ? 1 : 0;
  }
  return s_nCheck != 0;
}

static Uint32 GpuPack(float r, float g, float b)
{
  const auto u = [](float c) { return static_cast<Uint32>(SDL_clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f); };
  return u(r) | (u(g) << 8) | (u(b) << 16) | 0xFF000000u;
}

// A shadow's or a fade's table, out = table[d], as out = k * colour d for each channel by
// least squares over every d.
static void GpuFullFitScale(const SDL_Color *pColors, const Uint8 *pTable, float *pafOut)
{
  for (int ch = 0; ch < 3; ++ch) {
    double dd = 0.0, dout = 0.0;
    for (int d = 1; d < 256; ++d) {
      const SDL_Color &cd = pColors[d];
      const SDL_Color &co = pColors[pTable[d]];
      const double u = (ch == 0 ? cd.r : ch == 1 ? cd.g : cd.b) / 255.0;
      const double o = (ch == 0 ? co.r : ch == 1 ? co.g : co.b) / 255.0;
      dd += u * u;
      dout += u * o;
    }
    pafOut[ch] = dd > 1e-9 ? static_cast<float>(SDL_max(dout / dd, 0.0)) : 1.0f;
  }
  pafOut[3] = 0.0f;
}

// The fits made of each table, by its place in the table store, while the atlas and the
// palette last; and whether a remap's team colours are a slot's ramp, by the remap's place
// and the slot.
static std::unordered_map<Uint32, std::array<float, 4>> g_fullFits;
static std::unordered_map<Uint64, bool> g_fullPlain;
static unsigned int g_nFullSerial;
static SDL_Color g_aFullColors[256];
static std::vector<Uint32> g_fullExtras;  // 8 words a primitive (scene.hlsl, ExtraWord)
static unsigned int g_nFullTileset;
static double g_adFullTerrainUse[256];
static Uint32 g_nFullFogTable = SCENE_NONE;

// The frame's extras and uniforms for CSSceneFull.
static void GpuFullPrepare(const SceneFrame *pFrame, const SDL_Color *pColors)
{
  if (pFrame->nAtlasSerial != g_nFullSerial || SDL_memcmp(pColors, g_aFullColors, sizeof g_aFullColors) != 0) {
    g_nFullSerial = pFrame->nAtlasSerial;
    SDL_memcpy(g_aFullColors, pColors, sizeof g_aFullColors);
    g_fullFits.clear();
    g_fullPlain.clear();
    g_nFullFogTable = SCENE_NONE;
  }
  // the palette, and the slots' shades
  for (int i = 0; i < 256; ++i)
    g_full.anPal[i] = GpuPack(pColors[i].r / 255.0f, pColors[i].g / 255.0f, pColors[i].b / 255.0f);
  const int nSlots = SDL_min(PortTeamSlots(), FULL_SHADES / 8);
  for (int s = 0; s < nSlots; ++s) {
    float afShades[8][3];
    PortTeamShades(s, reinterpret_cast<const unsigned char *>(pColors), afShades);
    for (int k = 0; k < 8; ++k)
      g_full.anShade[s * 8 + k] = GpuPack(afShades[k][0], afShades[k][1], afShades[k][2]);
  }
  // the fog's matrices, when its table or the palette changes: the terrain's weighted by how
  // often the tileset's cells use each colour, the sprites' all alike
  if (pFrame->nTileset != g_nFullTileset && pFrame->pCells != NULL) {
    g_nFullTileset = pFrame->nTileset;
    SDL_zero(g_adFullTerrainUse);
    for (Uint32 i = 0; i < pFrame->nCells * 64u; ++i)
      g_adFullTerrainUse[pFrame->pCells[i]] += 1.0;
    g_nFullFogTable = SCENE_NONE;
  }
  for (Uint32 i = 0; i < pFrame->nPrims; ++i) {
    const ScenePrim &fog = pFrame->pPrims[i];
    if ((fog.nKind & 0xFF) != SCENE_PRIM_FOG || fog.nTable == SCENE_NONE || fog.nTable + 32 * 256 > pFrame->nTables)
      continue;
    if (fog.nTable != g_nFullFogTable) {
      g_nFullFogTable = fog.nTable;
      static double s_adEvery[256];
      for (int v = 0; v < 256; ++v)
        s_adEvery[v] = 1.0;
      for (int k = 0; k < 32; ++k) {
        double sprite[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        double terrain[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        if (k < 31) {
          GpuHdFitMatrix(pColors, pFrame->pTables + fog.nTable + k * 256, s_adEvery, sprite);
          GpuHdFitMatrix(pColors, pFrame->pTables + fog.nTable + k * 256, g_adFullTerrainUse, terrain);
        }
        GpuHdStoreMatrix(sprite, 1.0, &g_fullFog.afFogSprite[k * 12]);
        GpuHdStoreMatrix(terrain, 1.0, &g_fullFog.afFogTerrain[k * 12]);
      }
    }
    break;
  }
  // each primitive's extras: its colour slot, whether its remap's team colours are the slot's
  // ramp, a shadow's fit (a translucent frame needs none: CSSceneFull carries its change over)
  g_fullExtras.assign(static_cast<size_t>(pFrame->nPrims) * 8, 0);
  for (Uint32 i = 0; i < pFrame->nPrims; ++i) {
    const ScenePrim &prim = pFrame->pPrims[i];
    const unsigned int nKind = prim.nKind & 0xFF;
    Uint32 *pOut = &g_fullExtras[static_cast<size_t>(i) * 8];
    if (prim.nTable == SCENE_NONE)
      continue;
    if ((nKind == SCENE_PRIM_NORMAL || nKind == SCENE_PRIM_UNCLOAKED) && prim.nTable + 256 <= pFrame->nTables) {
      const unsigned int nSlot = pFrame->pPrimColor != NULL ? pFrame->pPrimColor[i] : 0;
      const Uint64 nKey = (static_cast<Uint64>(prim.nTable) << 8) | nSlot;
      auto it = g_fullPlain.find(nKey);
      if (it == g_fullPlain.end()) {
        Uint8 abRamp[8];
        PortTeamRamp(static_cast<int>(nSlot), abRamp);
        it = g_fullPlain.emplace(nKey, SDL_memcmp(pFrame->pTables + prim.nTable + 8, abRamp, 8) == 0).first;
      }
      pOut[0] = nSlot | (it->second ? 0x100u : 0u);
      // and its look
      if (nKind == SCENE_PRIM_NORMAL && pFrame->pPrimLook != NULL && pFrame->pLooks != NULL)
        pOut[1] = pFrame->pPrimLook[i];
      continue;
    }
    if ((nKind == SCENE_PRIM_SHADOW || nKind == SCENE_PRIM_FADE) && prim.nTable + 256 <= pFrame->nTables) {
      auto it = g_fullFits.find(prim.nTable);
      if (it == g_fullFits.end()) {
        std::array<float, 4> fit;
        GpuFullFitScale(pColors, pFrame->pTables + prim.nTable, fit.data());
        it = g_fullFits.emplace(prim.nTable, fit).first;
      }
      SDL_memcpy(pOut + 4, it->second.data(), 16);
    }
  }
}

// Binds the description's buffers and runs one of the scene's shaders.
// x / d rounded down, for a box that starts left of the layer or above it.
static int GpuFloorDiv(int x, int d)
{
  return x >= 0 ? x / d : -((-x + d - 1) / d);
}

static void GpuSceneDispatch(SDL_GPUCommandBuffer *pCmd, SDL_GPUComputePipeline *pPipeline,
                             const SceneParams &params, Uint32 nGroupsX, Uint32 nGroupsY,
                             const HudParams *pHud = NULL)
{
  SDL_GPUStorageBufferReadWriteBinding rw[SCENE_STORES - SCENE_STORE_READ];
  SDL_GPUBuffer *ro[SCENE_STORE_READ];
  SDL_GPUComputePass *pPass;
  int i;

  if (nGroupsX == 0 || nGroupsY == 0)
    return;
  SDL_zero(rw);
  for (i = SCENE_STORE_READ; i < SCENE_STORES; ++i)
    rw[i - SCENE_STORE_READ].buffer = g_sceneStore[i].pBuffer;
  pPass = SDL_BeginGPUComputePass(pCmd, NULL, 0, rw, SCENE_STORES - SCENE_STORE_READ);
  SDL_BindGPUComputePipeline(pPass, pPipeline);
  for (i = 0; i < SCENE_STORE_READ; ++i)
    ro[i] = g_sceneStore[i].pBuffer;
  SDL_BindGPUComputeStorageBuffers(pPass, 0, ro, SCENE_STORE_READ);
  SDL_PushGPUComputeUniformData(pCmd, 0, &params, sizeof params);
  SDL_PushGPUComputeUniformData(pCmd, 1, &g_full, sizeof g_full);
  // every block the pipelines declare is pushed, the camera's as zeros when it is not used
  static const HudParams s_noHud = {};
  SDL_PushGPUComputeUniformData(pCmd, 2, pHud != NULL ? pHud : &s_noHud, sizeof *pHud);
  SDL_PushGPUComputeUniformData(pCmd, 3, &g_fullFog, sizeof g_fullFog);
  SDL_DispatchGPUCompute(pPass, nGroupsX, nGroupsY, 1);
  SDL_EndGPUComputePass(pPass);
}

// Where a frame's description goes in the stores, and the sizes it takes.
struct GpuSceneLayout {
  Uint32 nMap;         // the rows the map layer draws
  Uint32 nBuffer;      // and the screen buffer's, which a distortion reads
  Uint32 nScreen;      // the screen's, the GPU's copy of it: the buffer's but for the camera's
  Uint32 nCopied;      // the pixels the screen takes, of its map rows
  Uint32 nSlots;       // bytes
  Uint32 nBins;
  Uint32 nHudBins;
  Uint32 nCellsAt;     // bytes into the tileset's store
  Uint32 nBinListAt;   // words into the frame's store
  Uint32 nHudStartAt;  // the camera's: the squares over the map at the screen's size,
  Uint32 nHudListAt;   // their starts and their lists, in words into the frame's store
  Uint32 nBytesAt;     // bytes into it
  Uint32 nCopiedAt;    // the pixels the screen takes, bytes into the frame's own
  Uint32 nExtraAt;     // the full colour's extras, bytes into the frame's store
  Uint32 nLooksAt;     // the looks' data after them (port_looks.h), with the full colour
  Uint32 nMapPrims;    // the primitives of the map layer; with the camera, those after are over it
};

static void GpuSceneLayoutOf(const SceneFrame *pFrame, bool bShow, bool bZoom, GpuSceneLayout *pLay)
{
  GpuSceneLayout &lay = *pLay;
  // in the layer's pixels (SceneFrame's nStep)
  const int nBufferRows = pFrame->nHeight > pFrame->nRows ? pFrame->nHeight : pFrame->nRows;
  lay.nMap = static_cast<Uint32>(pFrame->nLayerW * pFrame->nLayerRows);
  lay.nBuffer = static_cast<Uint32>(pFrame->nLayerW * ((nBufferRows + pFrame->nStep - 1) / pFrame->nStep));
  lay.nScreen = bZoom ? static_cast<Uint32>(pFrame->nScreenW * pFrame->nScreenH) : lay.nBuffer;
  lay.nCopied = bZoom ? static_cast<Uint32>(pFrame->nScreenW * pFrame->nScreenRows) : lay.nMap;
  lay.nSlots = static_cast<Uint32>(pFrame->nRingTilesX * pFrame->nRingTilesY) * sizeof(SceneSlot);
  lay.nBins = static_cast<Uint32>(pFrame->nBinsX * pFrame->nBinsY + 1);
  lay.nHudBins = bZoom ? static_cast<Uint32>(pFrame->nHudBinsX * pFrame->nHudBinsY + 1) : 0;
  lay.nCellsAt = (pFrame->nMegatiles * 64 + 15) & ~15u;
  lay.nBinListAt = (lay.nBins + 3) & ~3u;
  lay.nHudStartAt = (lay.nBinListAt + pFrame->nBinList + 3) & ~3u;
  lay.nHudListAt = (lay.nHudStartAt + lay.nHudBins + 3) & ~3u;
  lay.nBytesAt = ((bZoom ? lay.nHudListAt + pFrame->nHudBinList : lay.nBinListAt + pFrame->nBinList) * 4 + 15) & ~15u;
  lay.nCopiedAt = (pFrame->nBytes + 15) & ~15u;
  if (pFrame->nFogLightBytes != 0)  // the light map the GPU makes, past the bytes
    lay.nCopiedAt = (pFrame->nFogLightAt + pFrame->nFogLightBytes + 15) & ~15u;
  lay.nExtraAt = (lay.nBytesAt + lay.nCopiedAt + (bShow ? lay.nCopied : 0) + 15) & ~15u;
  lay.nLooksAt = lay.nExtraAt + pFrame->nPrims * 32;
  lay.nMapPrims = bZoom ? pFrame->nMapPrims : pFrame->nPrims;
}

// Each store at the size this frame needs; one made again starts empty, so what the GPU has
// of the atlas, entries, tables and tileset is counted again. *pbShownFresh: the screen's
// copy was made again; *pbTileset: the tileset goes up.
static bool GpuSceneStoresFit(const SceneFrame *pFrame, const GpuSceneLayout &lay, bool bZoom, bool bFull,
                              bool *pbShownFresh, bool *pbTileset)
{
  const SDL_GPUBufferUsageFlags ro = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
  const SDL_GPUBufferUsageFlags rw =
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
  const Uint32 nRgb = bFull ? lay.nBuffer * 4 : 16;
  const Uint32 nRgbShown = bFull ? lay.nScreen * 4 : 16;
  bool bFresh;
  if (pFrame->nAtlasSerial != g_nSceneAtlasSerial) {
    g_nSceneAtlasSerial = pFrame->nAtlasSerial;
    g_nSceneEntries = 0;
    g_nScenePixels = 0;
    g_nSceneTables = 0;
  }
  *pbTileset = pFrame->nTileset != g_nSceneTileset;
  if (!GpuStoreFit(&g_sceneStore[SCENE_STORE_SHOWN], lay.nScreen, rw | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                   pbShownFresh))
    return false;
  if (!GpuStoreFit(&g_sceneStore[SCENE_STORE_HUD], bZoom ? lay.nScreen : 16,
                   rw | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, &bFresh))
    return false;
  if (!GpuStoreFit(&g_sceneStore[SCENE_STORE_SLOTS], lay.nSlots, ro | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                   &bFresh))
    return false;
  if (bFresh)
    g_nSceneSlotsRing = -1;
  if (!GpuStoreFit(&g_sceneStore[SCENE_STORE_TILESET], lay.nCellsAt + pFrame->nCells * 64, ro, &bFresh))
    return false;
  *pbTileset = *pbTileset || bFresh;
  if (!GpuStoreFit(&g_sceneStore[SCENE_STORE_ENTRIES], pFrame->nEntries * sizeof(SceneEntry), ro, &bFresh))
    return false;
  if (bFresh)
    g_nSceneEntries = 0;
  if (!GpuStoreFit(&g_sceneStore[SCENE_STORE_ATLAS], pFrame->nPixels * 2, ro, &bFresh))
    return false;
  if (bFresh)
    g_nScenePixels = 0;
  if (!GpuStoreFit(&g_sceneStore[SCENE_STORE_TABLES], pFrame->nTables, ro | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                   &bFresh))
    return false;
  if (bFresh)
    g_nSceneTables = 0;
  return GpuStoreFit(&g_sceneStore[SCENE_STORE_PRIMS], pFrame->nPrims * sizeof(ScenePrim), ro, &bFresh) &&
         // the HD frames read the fog's light map in it too
         GpuStoreFit(&g_sceneStore[SCENE_STORE_FRAME],
                     lay.nExtraAt + (bFull ? pFrame->nPrims * 32 + pFrame->nLooksBytes : 0),
                     rw | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, &bFresh) &&  // CSFogLight writes it
         GpuStoreFit(&g_sceneStore[SCENE_STORE_MAP_RGB], nRgb, rw | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, &bFresh) &&
         GpuStoreFit(&g_sceneStore[SCENE_STORE_SHOWN_RGB], nRgbShown, rw | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                     &bFresh) &&
         GpuStoreFit(&g_sceneStore[SCENE_STORE_PREV_RGB], nRgb, rw, &bFresh) &&
         GpuStoreFit(&g_sceneStore[SCENE_STORE_PREV], lay.nBuffer, ro, &bFresh) &&
         GpuStoreFit(&g_sceneStore[SCENE_STORE_MAP], lay.nBuffer, rw | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                     &bFresh) &&
         GpuStoreFit(&g_sceneStore[SCENE_STORE_STATS], 16, rw, &bFresh) &&
         // and after it the pixels under a dialog's translucency in HD, then the dialog's, then
         // the draw order, two bytes a pixel
         GpuStoreFit(&g_sceneStore[SCENE_STORE_TOUCHED], lay.nBuffer * 5, rw | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                     &bFresh);
}

// A piece of the frame's upload: where it goes and what it is. nStore -1 is kept in the
// upload and copied later (the second drawing's primitives mid-blend).
struct GpuScenePiece {
  int nStore;
  Uint32 nAt;  // in the store
  const void *pData;
  Uint32 nBytes;
};
enum { SCENE_PIECES_MAX = 20 };  // 17 at most: every piece below at once

// The pieces this frame uploads: what changed of the tileset, entries, atlas and tables,
// the primitives, the squares' lists and the frame's bytes. With the frame in HD the
// primitives are those less the ones drawn in HD (pHdPrims); mid-blend between SD and HD
// (F5), all of them first, for the whole SD frame, and those after.
static int GpuScenePieces(const SceneFrame *pFrame, const SDL_Color *pColors, const GpuSceneLayout &lay,
                          bool bTileset, bool bShow, bool bZoom, bool bFull, const ScenePrim *pHdPrims, bool bMix,
                          GpuScenePiece *pieces)
{
  static const Uint32 s_zero[4] = {0, 0, 0, 0};
  int nPieces = 0;
  // the ring's slots written since its last frame, or all of them when the store holds another
  // ring or another size
  if (pFrame->nSlotsRing != g_nSceneSlotsRing || lay.nSlots != g_nSceneSlotsBytes)
    pieces[nPieces++] = {SCENE_STORE_SLOTS, 0, pFrame->pSlots, lay.nSlots};
  else if (pFrame->nSlotsFirst < pFrame->nSlotsEnd)
    pieces[nPieces++] = {SCENE_STORE_SLOTS, pFrame->nSlotsFirst * static_cast<Uint32>(sizeof(SceneSlot)),
                         pFrame->pSlots + pFrame->nSlotsFirst,
                         (pFrame->nSlotsEnd - pFrame->nSlotsFirst) * static_cast<Uint32>(sizeof(SceneSlot))};
  if (bTileset) {
    pieces[nPieces++] = {SCENE_STORE_TILESET, 0, pFrame->pMegatiles, pFrame->nMegatiles * 64};
    pieces[nPieces++] = {SCENE_STORE_TILESET, lay.nCellsAt, pFrame->pCells, pFrame->nCells * 64};
  }
  if (pFrame->nEntries > g_nSceneEntries)
    pieces[nPieces++] = {SCENE_STORE_ENTRIES, g_nSceneEntries * static_cast<Uint32>(sizeof(SceneEntry)),
                         pFrame->pEntries + g_nSceneEntries,
                         (pFrame->nEntries - g_nSceneEntries) * static_cast<Uint32>(sizeof(SceneEntry))};
  if (pFrame->nPixels > g_nScenePixels)
    pieces[nPieces++] = {SCENE_STORE_ATLAS, g_nScenePixels * 2, pFrame->pPixels + g_nScenePixels,
                         (pFrame->nPixels - g_nScenePixels) * 2};
  if (pFrame->nTables > g_nSceneTables)
    pieces[nPieces++] = {SCENE_STORE_TABLES, g_nSceneTables, pFrame->pTables + g_nSceneTables,
                         pFrame->nTables - g_nSceneTables};
  const Uint32 nPrimBytes = pFrame->nPrims * static_cast<Uint32>(sizeof(ScenePrim));
  if (pFrame->nPrims != 0)
    pieces[nPieces++] = {SCENE_STORE_PRIMS, 0, bMix ? pFrame->pPrims : pHdPrims, nPrimBytes};
  if (bMix)
    pieces[nPieces++] = {-1, 0, pHdPrims, nPrimBytes};
  pieces[nPieces++] = {SCENE_STORE_FRAME, 0, pFrame->pBinStart, lay.nBins * 4};
  if (pFrame->nBinList != 0)
    pieces[nPieces++] = {SCENE_STORE_FRAME, lay.nBinListAt * 4, pFrame->pBinList, pFrame->nBinList * 4};
  if (bZoom) {
    pieces[nPieces++] = {SCENE_STORE_FRAME, lay.nHudStartAt * 4, pFrame->pHudBinStart, lay.nHudBins * 4};
    if (pFrame->nHudBinList != 0)
      pieces[nPieces++] = {SCENE_STORE_FRAME, lay.nHudListAt * 4, pFrame->pHudBinList, pFrame->nHudBinList * 4};
  }
  if (pFrame->nBytes != 0)
    pieces[nPieces++] = {SCENE_STORE_FRAME, lay.nBytesAt, pFrame->pBytes, pFrame->nBytes};
  if (bShow)
    pieces[nPieces++] = {SCENE_STORE_FRAME, lay.nBytesAt + lay.nCopiedAt, pFrame->pCopied, lay.nCopied};
  pieces[nPieces++] = {SCENE_STORE_STATS, 0, s_zero, sizeof s_zero};
  if (bFull) {
    GpuFullPrepare(pFrame, pColors);
    if (pFrame->nPrims != 0)
      pieces[nPieces++] = {SCENE_STORE_FRAME, lay.nExtraAt, g_fullExtras.data(), pFrame->nPrims * 32};
    if (pFrame->nLooksBytes != 0)
      pieces[nPieces++] = {SCENE_STORE_FRAME, lay.nLooksAt, pFrame->pLooks, pFrame->nLooksBytes};
  }
  return nPieces;
}

// The pieces into the upload buffer, after making room for them and, for the check, for the
// readback of nDown bytes and the counts.
static bool GpuSceneUploadFill(const GpuScenePiece *pieces, int nPieces, bool bCheck, Uint32 nDown)
{
  Uint32 nUp = 0;
  for (int i = 0; i < nPieces; ++i)
    nUp += (pieces[i].nBytes + 15) & ~15u;
  if (!GpuTransferFit(&g_sceneUp, &g_nSceneUp, nUp, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD))
    return false;
  if (bCheck && !GpuTransferFit(&g_sceneDown, &g_nSceneDown, ((nDown + 15) & ~15u) + 16,
                                SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD))
    return false;
  Uint8 *pUp = static_cast<Uint8 *>(SDL_MapGPUTransferBuffer(g_device, g_sceneUp, true));
  if (pUp == NULL)
    return false;
  Uint32 nAt = 0;
  for (int i = 0; i < nPieces; ++i) {
    SDL_memcpy(pUp + nAt, pieces[i].pData, pieces[i].nBytes);
    nAt += (pieces[i].nBytes + 15) & ~15u;
  }
  SDL_UnmapGPUTransferBuffer(g_device, g_sceneUp);
  return true;
}

// The upload into the stores; returns where a piece kept in the upload is (nStore -1).
static Uint32 GpuSceneUploadCopy(SDL_GPUCommandBuffer *pCmd, const GpuScenePiece *pieces, int nPieces)
{
  Uint32 nKeptAt = 0;
  Uint32 nAt = 0;
  SDL_GPUCopyPass *pCopy = SDL_BeginGPUCopyPass(pCmd);
  for (int i = 0; i < nPieces; ++i) {
    SDL_GPUTransferBufferLocation from;
    SDL_GPUBufferRegion to;
    if (pieces[i].nStore < 0) {
      nKeptAt = nAt;
      nAt += (pieces[i].nBytes + 15) & ~15u;
      continue;
    }
    from.transfer_buffer = g_sceneUp;
    from.offset = nAt;
    to.buffer = g_sceneStore[pieces[i].nStore].pBuffer;
    to.offset = pieces[i].nAt;
    to.size = pieces[i].nBytes;
    SDL_UploadToGPUBuffer(pCopy, &from, &to, false);
    nAt += (pieces[i].nBytes + 15) & ~15u;
  }
  SDL_EndGPUCopyPass(pCopy);
  return nKeptAt;
}

// A distortion's dispatch, and what it reads and writes, in the layer's pixels (the
// layer's step).
struct GpuDistortAt {
  Uint32 nPrim;
  int nFirstWord, nWords;  // its words across
  int nTop, nRows;         // and its rows
  int nLeft, nRight;       // its words' first and last pixel: what it writes
  int nReadL, nReadT, nReadR, nReadB;
  bool bWhole;             // it can read past an end of the buffer, where the read wraps
};

static void GpuDistortOf(const SceneFrame *pFrame, Uint32 n, const SceneParams &params, int nBufferRows,
                         GpuDistortAt *pAt)
{
  const ScenePrim &prim = pFrame->pPrims[n];
  const int d = static_cast<int>(params.nStep);
  const int nX0 = GpuFloorDiv(prim.nX, d);
  const int nX1 = GpuFloorDiv(prim.nX + static_cast<int>(prim.nClip1 & 0xFFFF) - 1, d);
  const int nY0 = GpuFloorDiv(prim.nY, d);
  const int nY1 = GpuFloorDiv(prim.nY + static_cast<int>(prim.nClip1 >> 16) - 1, d);
  const int nW = static_cast<int>(params.nWidth);
  pAt->nPrim = n;
  pAt->nFirstWord = nX0 >> 2;
  pAt->nWords = (nX1 >> 2) - pAt->nFirstWord + 1;
  pAt->nTop = nY0 < 0 ? 0 : nY0;
  pAt->nRows = nY1 + 1 > pAt->nTop ? nY1 + 1 - pAt->nTop : 0;
  pAt->nLeft = pAt->nFirstWord * 4;
  pAt->nRight = nX1 | 3;
  // A pixel of its box takes the pixel up to 255 of the game's along the screen buffer from it
  // (scene.hlsl, CSDistort's shift), past a row's end into the next row's start.
  const int nReach = 255 / d + 2;
  pAt->nReadL = pAt->nLeft - nReach;
  pAt->nReadR = pAt->nRight + nReach;
  pAt->nReadT = pAt->nTop;
  pAt->nReadB = nY1;
  if (pAt->nReadL < 0) {
    pAt->nReadL = 0;
    pAt->nReadR = nW - 1;
    --pAt->nReadT;
  }
  if (pAt->nReadR >= nW) {
    pAt->nReadL = 0;
    pAt->nReadR = nW - 1;
    ++pAt->nReadB;
  }
  pAt->bWhole = pAt->nReadT < 0 || pAt->nReadB >= nBufferRows;
}

// Whether distortion k of the frame's reads what the batch from distortion i draws before it:
// an earlier distortion's box, or a primitive between distortion i and it (the squares' lists,
// SceneBin, in order). Its box is in what it reads, so what is drawn after it in 1.17's order
// and before it in the batch's is found too. With the layer at a step of 4 or more only the
// words the distortions write are kept apart: a square is then 128 of the game's pixels or
// more, a distortion's reach 64 or more, nearly every one met a primitive or another, and a
// batch was one distortion, its passes' barriers a third of the frame. A primitive between two
// of a batch is then drawn over the later one where 1.17 draws it under, and a distortion reads
// the map without another's of its batch: a shimmer of a pixel or two of the layer's.
static bool GpuDistortMeets(const SceneFrame *pFrame, const std::vector<GpuDistortAt> &at, size_t i, size_t k)
{
  const GpuDistortAt &a = at[k];
  const bool bCoarse = pFrame->nStep >= 4;
  if (a.bWhole && !bCoarse)
    return true;
  for (size_t m = i; m < k; ++m) {
    const GpuDistortAt &b = at[m];
    if (b.nRows <= 0 || b.nWords <= 0)
      continue;
    // coarse, only where their words meet: two dispatches of a pass would write the same word
    const int nL = bCoarse ? a.nLeft : a.nReadL;
    const int nR = bCoarse ? a.nRight : a.nReadR;
    const int nT = bCoarse ? a.nTop : a.nReadT;
    const int nB = bCoarse ? a.nTop + a.nRows - 1 : a.nReadB;
    if (b.nRight >= nL && b.nLeft <= nR && b.nTop + b.nRows - 1 >= nT && b.nTop <= nB)
      return true;
  }
  const Uint32 nFirst = at[i].nPrim + 1;
  const Uint32 nBefore = a.nPrim;
  if (nFirst >= nBefore || bCoarse)
    return false;
  const int nBinsX = pFrame->nBinsX;
  const int nBinsY = pFrame->nBinsY;
  const int bx0 = SDL_max(a.nReadL, 0) / SCENE_BIN;
  const int bx1 = SDL_min(a.nReadR / SCENE_BIN, nBinsX - 1);
  const int by0 = SDL_max(a.nReadT, 0) / SCENE_BIN;
  const int by1 = SDL_min(a.nReadB / SCENE_BIN, nBinsY - 1);
  for (int by = by0; by <= by1; ++by)
    for (int bx = bx0; bx <= bx1; ++bx) {
      const size_t nBin = static_cast<size_t>(by) * nBinsX + bx;
      const unsigned int *pBegin = pFrame->pBinList + pFrame->pBinStart[nBin];
      const unsigned int *pEnd = pFrame->pBinList + pFrame->pBinStart[nBin + 1];
      const unsigned int *pAt = std::lower_bound(pBegin, pEnd, nFirst);
      if (pAt != pEnd && *pAt < nBefore)
        return true;
    }
  return false;
}

// Distortions i up to j, none of which reads what another draws: the map copied once where they
// read, then each over its own box, in one pass, as their boxes' words are apart.
static void GpuSceneDistorts(SDL_GPUCommandBuffer *pCmd, const std::vector<GpuDistortAt> &at, size_t i, size_t j,
                             const SceneParams &params, Uint32 nBuffer, bool bFull)
{
  // The copy they read: only their rows, or the whole buffer where a read wraps. A copy
  // of the whole map a distortion took most of a zoomed frame's time: 50 of them over a
  // 67-megapixel render view.
  bool bWhole = false;
  int nTop = INT_MAX, nBottom = -1;
  for (size_t m = i; m < j; ++m) {
    bWhole = bWhole || at[m].bWhole;
    nTop = SDL_min(nTop, at[m].nReadT);
    nBottom = SDL_max(nBottom, at[m].nReadB);
  }
  const Uint32 nW = params.nWidth;
  Uint32 nFrom = 0, nBytes = nBuffer;
  if (!bWhole && nBottom >= nTop) {
    nFrom = (static_cast<Uint32>(nTop) * nW) & ~15u;
    Uint32 nEnd = (static_cast<Uint32>(nBottom + 1) * nW + 15u) & ~15u;
    if (nEnd > nBuffer)
      nEnd = nBuffer;
    nBytes = nEnd - nFrom;
  }
  SDL_GPUBufferLocation from;
  SDL_GPUBufferLocation to;
  SDL_GPUCopyPass *pCopy = SDL_BeginGPUCopyPass(pCmd);
  from.buffer = g_sceneStore[SCENE_STORE_MAP].pBuffer;
  from.offset = nFrom;
  to.buffer = g_sceneStore[SCENE_STORE_PREV].pBuffer;
  to.offset = nFrom;
  SDL_CopyGPUBufferToBuffer(pCopy, &from, &to, nBytes, false);
  if (bFull) {
    from.buffer = g_sceneStore[SCENE_STORE_MAP_RGB].pBuffer;
    to.buffer = g_sceneStore[SCENE_STORE_PREV_RGB].pBuffer;
    from.offset = to.offset = nFrom * 4;
    SDL_CopyGPUBufferToBuffer(pCopy, &from, &to, nBytes * 4, false);
  }
  SDL_EndGPUCopyPass(pCopy);

  SDL_GPUStorageBufferReadWriteBinding rw[SCENE_STORES - SCENE_STORE_READ];
  SDL_GPUBuffer *ro[SCENE_STORE_READ];
  SDL_zero(rw);
  for (int k = SCENE_STORE_READ; k < SCENE_STORES; ++k)
    rw[k - SCENE_STORE_READ].buffer = g_sceneStore[k].pBuffer;
  for (int k = 0; k < SCENE_STORE_READ; ++k)
    ro[k] = g_sceneStore[k].pBuffer;
  SDL_GPUComputePass *pPass = SDL_BeginGPUComputePass(pCmd, NULL, 0, rw, SCENE_STORES - SCENE_STORE_READ);
  SDL_BindGPUComputePipeline(pPass, bFull ? g_distortFullPipeline : g_distortPipeline);
  SDL_BindGPUComputeStorageBuffers(pPass, 0, ro, SCENE_STORE_READ);
  static const HudParams s_noHud = {};
  SDL_PushGPUComputeUniformData(pCmd, 1, &g_full, sizeof g_full);
  SDL_PushGPUComputeUniformData(pCmd, 2, &s_noHud, sizeof s_noHud);
  SDL_PushGPUComputeUniformData(pCmd, 3, &g_fullFog, sizeof g_fullFog);
  for (size_t m = i; m < j; ++m) {
    const GpuDistortAt &a = at[m];
    if (a.nWords <= 0 || a.nRows <= 0)
      continue;
    SceneParams distort = params;
    distort.nPrim = a.nPrim;
    distort.nFirst = static_cast<Uint32>(a.nFirstWord);
    distort.nLast = static_cast<Uint32>(a.nWords);
    distort.nOrigin = static_cast<Uint32>(a.nTop);
    SDL_PushGPUComputeUniformData(pCmd, 0, &distort, sizeof distort);
    SDL_DispatchGPUCompute(pPass, (distort.nLast + 7) / 8, (static_cast<Uint32>(a.nRows) + 7) / 8, 1);
  }
  SDL_EndGPUComputePass(pPass);
}

// With the camera: the map seen through it at the screen's size, with what was
// drawn over it (CSHud).
static void GpuSceneHud(SDL_GPUCommandBuffer *pCmd, const SceneFrame *pFrame, const GpuSceneLayout &lay,
                        const SceneParams &params, bool bFull)
{
  SceneParams hud = params;
  hud.nWidth = static_cast<Uint32>(pFrame->nScreenW);
  hud.nRows = static_cast<Uint32>(pFrame->nScreenRows);
  hud.nHeight = static_cast<Uint32>(pFrame->nScreenH);
  hud.nBinsX = static_cast<Uint32>(pFrame->nHudBinsX);
  hud.nBinListAt = lay.nHudListAt;
  hud.nPrim = lay.nHudStartAt;
  hud.nFirst = lay.nMapPrims;
  hud.nLast = pFrame->nPrims;
  HudParams camera;
  camera.fCamX = static_cast<float>(pFrame->fCamX);
  camera.fCamY = static_cast<float>(pFrame->fCamY);
  camera.fZoom = static_cast<float>(pFrame->fZoom);
  camera.nFull = bFull ? 1u : 0u;
  camera.nMapX = static_cast<Uint32>(pFrame->nScrollX);
  camera.nMapY = static_cast<Uint32>(pFrame->nScrollY);
  camera.nMapW = static_cast<Uint32>(pFrame->nWidth);
  camera.nMapH = static_cast<Uint32>(pFrame->nRows);
  camera.nStep = params.nStep;
  camera.nStride = static_cast<Uint32>(pFrame->nLayerW);
  camera.anUnused[0] = camera.anUnused[1] = 0;
  GpuSceneDispatch(pCmd, g_hudPipeline, hud, (hud.nWidth / 4 + 7) / 8, (hud.nRows + 7) / 8, &camera);
}

// The scene's dispatches: the terrain and the primitives up to the first distortion; then
// each distortion over a copy of the map, and the primitives up to the next; then the
// screen's copy (CSShow), or with the camera CSHud. pRun is what the store holds, whose
// distortions drawn in HD are no longer distortions.
static void GpuSceneRun(SDL_GPUCommandBuffer *pCmd, const SceneFrame *pFrame, const GpuSceneLayout &lay,
                        SceneParams params, const ScenePrim *pRun, bool bShow, bool bZoom, bool bFull)
{
  // the distortions, each of which reads the map as it is before it (in batches of
  // those that read nothing another draws, GpuDistortMeets)
  static std::vector<GpuDistortAt> s_at;
  s_at.clear();
  const int nBufferRows = static_cast<int>(lay.nBuffer / params.nWidth);
  for (Uint32 n = 0; n < lay.nMapPrims; ++n)
    if ((pRun[n].nKind & 0xFF) == SCENE_PRIM_DISTORT) {
      GpuDistortAt at;
      GpuDistortOf(pFrame, n, params, nBufferRows, &at);
      s_at.push_back(at);
    }
  params.nFromTerrain = 1;
  params.nFirst = 0;
  params.nOrigin = 0;
  params.nLast = s_at.empty() ? lay.nMapPrims : s_at[0].nPrim;
  GpuSceneDispatch(pCmd, bFull ? g_sceneFullPipeline : g_scenePipeline, params, (params.nWidth / 4 + 7) / 8,
                   (params.nRows + 7) / 8);
  params.nFromTerrain = 0;
  for (size_t i = 0; i < s_at.size();) {
    size_t j = i + 1;
    while (j < s_at.size() && !GpuDistortMeets(pFrame, s_at, i, j))
      ++j;
    GpuSceneDistorts(pCmd, s_at, i, j, params, lay.nBuffer, bFull);
    // then the primitives after them, up to the next batch's first. After a distortion the map
    // is taken as it is, so only the pixels this run of primitives can draw change: the dispatch
    // covers the box of their boxes, and none when no primitive reaches the layer. A
    // pixel outside every box of the run is stored as it was loaded, so the frame is the same.
    params.nFirst = s_at[i].nPrim + 1;
    params.nLast = j < s_at.size() ? s_at[j].nPrim : lay.nMapPrims;
    int nL, nT, nR, nB;
    if (params.nFirst < params.nLast &&
        PortSceneRunBox(pRun, params.nFirst, params.nLast, pFrame->nWidth, pFrame->nRows, &nL, &nT, &nR, &nB)) {
      // in the layer's pixels, each of which is the game's at the middle of its square
      const int d = static_cast<int>(params.nStep);
      nL /= d;
      nT /= d;
      nR /= d;
      nB /= d;
      const Uint32 nX0 = static_cast<Uint32>(nL) & ~3u;
      params.nOrigin = nX0 | (static_cast<Uint32>(nT) << 16);
      GpuSceneDispatch(pCmd, bFull ? g_sceneFullPipeline : g_scenePipeline, params,
                       ((static_cast<Uint32>(nR) + 1 - nX0 + 3) / 4 + 7) / 8,
                       (static_cast<Uint32>(nB - nT) + 1 + 7) / 8);
      params.nOrigin = 0;
    }
    i = j;
  }
  if (bShow && !bZoom)
    GpuSceneDispatch(pCmd, bFull ? g_showFullPipeline : g_showPipeline, params, (params.nWidth / 4 + 7) / 8,
                     (params.nRows + 7) / 8);
  if (bZoom)
    GpuSceneHud(pCmd, pFrame, lay, params, bFull);
}

// Mid-blend, after the first drawing: the whole SD screen kept in g_hdShownSd, and the
// primitives less those drawn in HD (kept in the upload at nMixAt) into the store for the
// second drawing.
static void GpuSceneKeepSd(SDL_GPUCommandBuffer *pCmd, Uint32 nMixAt, Uint32 nPrimBytes)
{
  const Uint32 nShown = g_sceneStore[SCENE_STORE_SHOWN].nCapacity;
  if (g_hdShownSd == NULL || g_nHdShownSd < nShown) {
    if (g_hdShownSd != NULL)
      SDL_ReleaseGPUBuffer(g_device, g_hdShownSd);
    SDL_GPUBufferCreateInfo info;
    SDL_zero(info);
    info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    info.size = nShown;
    g_hdShownSd = SDL_CreateGPUBuffer(g_device, &info);
    g_nHdShownSd = g_hdShownSd != NULL ? nShown : 0;
  }
  SDL_GPUCopyPass *pCopy = SDL_BeginGPUCopyPass(pCmd);
  if (g_hdShownSd != NULL) {
    SDL_GPUBufferLocation from;
    SDL_GPUBufferLocation to;
    from.buffer = g_sceneStore[SCENE_STORE_SHOWN].pBuffer;
    from.offset = 0;
    to.buffer = g_hdShownSd;
    to.offset = 0;
    SDL_CopyGPUBufferToBuffer(pCopy, &from, &to, nShown, false);
  }
  SDL_GPUTransferBufferLocation from;
  SDL_GPUBufferRegion to;
  from.transfer_buffer = g_sceneUp;
  from.offset = nMixAt;
  to.buffer = g_sceneStore[SCENE_STORE_PRIMS].pBuffer;
  to.offset = 0;
  to.size = nPrimBytes;
  SDL_UploadToGPUBuffer(pCopy, &from, &to, false);
  SDL_EndGPUCopyPass(pCopy);
}

// What the frame leaves for the present and the next frame: the map layer as drawn, for a
// skirmish's start and the camera (present.hlsl, PSGround), the camera's place, and what
// the GPU now has of the description.
static void GpuSceneDrawn(const SceneFrame *pFrame, bool bShow, bool bCheck, bool bZoom, bool bFull)
{
  if (bShow)
    g_bFullShown = bFull;
  g_bGroundOk = bShow && !bCheck && !g_bHdScene;
  if (bShow) {
    g_zoomShown.bOn = bZoom;
    g_zoomShown.fCamX = pFrame->fCamX;
    g_zoomShown.fCamY = pFrame->fCamY;
    g_zoomShown.fZoom = pFrame->fZoom;
    g_zoomShown.nMapX = pFrame->nScrollX;
    g_zoomShown.nMapY = pFrame->nScrollY;
    // the game's pixels the layer covers, which a coarser layer may take past the render view
    g_zoomShown.nMapW = pFrame->nLayerW * pFrame->nStep;
    g_zoomShown.nMapH = pFrame->nLayerRows * pFrame->nStep;
    g_zoomShown.nGameX = pFrame->nGameScrollX;
    g_zoomShown.nGameY = pFrame->nGameScrollY;
  }
  g_nGroundW = pFrame->nLayerW;
  g_nGroundRows = pFrame->nLayerRows;
  g_nSceneSlotsRing = pFrame->nSlotsRing;
  g_nSceneSlotsBytes = static_cast<Uint32>(pFrame->nRingTilesX * pFrame->nRingTilesY) * sizeof(SceneSlot);
  g_bGroundFull = bFull;
  g_nSceneTileset = pFrame->nTileset;
  g_nSceneEntries = pFrame->nEntries;
  g_nScenePixels = pFrame->nPixels;
  g_nSceneTables = pFrame->nTables;
}

// The check: the map, or with the camera the screen's copy of its map rows (nDown bytes),
// and the shader's counts read back; the frame's commands submitted and waited for, and the
// map compared with the software's.
static void GpuSceneCheck(SDL_GPUCommandBuffer *pCmd, const SceneFrame *pFrame, bool bZoom, Uint32 nDown)
{
  SDL_GPUCopyPass *pCopy = SDL_BeginGPUCopyPass(pCmd);
  SDL_GPUBufferRegion from;
  SDL_GPUTransferBufferLocation to;
  from.buffer = g_sceneStore[bZoom ? SCENE_STORE_SHOWN : SCENE_STORE_MAP].pBuffer;
  from.offset = 0;
  from.size = nDown;
  to.transfer_buffer = g_sceneDown;
  to.offset = 0;
  SDL_DownloadFromGPUBuffer(pCopy, &from, &to);
  from.buffer = g_sceneStore[SCENE_STORE_STATS].pBuffer;
  from.size = 16;
  to.offset = (nDown + 15) & ~15u;
  SDL_DownloadFromGPUBuffer(pCopy, &from, &to);
  SDL_EndGPUCopyPass(pCopy);
  SDL_GPUFence *pFence = SDL_SubmitGPUCommandBufferAndAcquireFence(pCmd);
  if (pFence != NULL) {
    SDL_WaitForGPUFences(g_device, true, &pFence, 1);
    SDL_ReleaseGPUFence(g_device, pFence);
    GpuSceneCompare(pFrame);
  }
}

// The fog's light map, made where the frame's bytes leave room for it: fog.hlsl's
// CSFogLight from the grid the pass uploaded, before the scene reads it.
struct FogLightParams {
  Uint32 nGridAt, nGrid, nRampAt, nLightAt, nRingX, nRingY, nPitch, nUnused;
};

static void GpuSceneFogLight(SDL_GPUCommandBuffer *pCmd, const SceneFrame *pFrame, const GpuSceneLayout &lay)
{
  if (pFrame->nFogLightBytes == 0 || g_fogLightPipeline == NULL)
    return;
  FogLightParams fog;
  fog.nGridAt = lay.nBytesAt + pFrame->nFogGridAt;
  fog.nGrid = pFrame->nFogGrid;
  fog.nRampAt = lay.nBytesAt + pFrame->nFogRampAt;
  fog.nLightAt = lay.nBytesAt + pFrame->nFogLightAt;
  fog.nRingX = pFrame->nFogRingX;
  fog.nRingY = pFrame->nFogRingY;
  fog.nPitch = pFrame->nFogPitch;
  fog.nUnused = 0;
  SDL_GPUStorageBufferReadWriteBinding rw;
  SDL_zero(rw);
  rw.buffer = g_sceneStore[SCENE_STORE_FRAME].pBuffer;
  SDL_GPUComputePass *pPass = SDL_BeginGPUComputePass(pCmd, NULL, 0, &rw, 1);
  SDL_BindGPUComputePipeline(pPass, g_fogLightPipeline);
  SDL_PushGPUComputeUniformData(pCmd, 0, &fog, sizeof fog);
  SDL_DispatchGPUCompute(pPass, (fog.nRingX + 1 + 7) / 8, ((fog.nRingY + 1) * 4 + 7) / 8, 1);
  SDL_EndGPUComputePass(pPass);
}

bool PortGpuScene(const SceneFrame *pFrame, const SDL_Color *pColors, bool bCheck)
{
  if (g_device == NULL || g_scenePipeline == NULL || pFrame == NULL)
    return false;
  if (pFrame->pGame == NULL)
    bCheck = false;  // the software drew nothing to compare with
  const bool bShow = pFrame->nStage == SCENE_SCREEN && pFrame->pCopied != NULL;
  // the camera's render view: the map layer is its, the primitives over the map
  // drawn at the screen's size after it (CSHud)
  const bool bZoom = bShow && pFrame->bZoom != 0 && g_hudPipeline != NULL;
  GpuSceneLayout lay;
  GpuSceneLayoutOf(pFrame, bShow, bZoom, &lay);
  // the full colour (stage G): at the last stage only, and never under the check, which
  // compares the frame with 1.17's
  const bool bFull = bShow && !bCheck && !GpuCheckEnv() && g_fFull >= 0.5f && pColors != NULL &&
                     g_sceneFullPipeline != NULL && g_distortFullPipeline != NULL && g_showFullPipeline != NULL &&
                     g_composeFullPipeline != NULL;
  bool bShownFresh = false;
  bool bTileset;
  if (!GpuSceneStoresFit(pFrame, lay, bZoom, bFull, &bShownFresh, &bTileset))
    return false;
  const ScenePrim *pHdPrims = GpuHdScene(pFrame, lay.nBytesAt);
  const bool bMix = bShow && !bCheck && pFrame->nPrims != 0 && pHdPrims != pFrame->pPrims && GpuHdMixing();
  g_bHdMixed = false;
  GpuScenePiece pieces[SCENE_PIECES_MAX];
  const int nPieces = GpuScenePieces(pFrame, pColors, lay, bTileset, bShow, bZoom, bFull, pHdPrims, bMix, pieces);
  g_nSceneSlotsRing = -1;  // unknown should the frame not be drawn (GpuSceneDrawn)
  // the check reads back the map, or with the camera the screen's copy of its map rows
  const Uint32 nDown = bZoom ? lay.nCopied : lay.nMap;
  if (!GpuSceneUploadFill(pieces, nPieces, bCheck, nDown))
    return false;
  // the frame's buffer, which the present submits; the check submits it here and waits
  SDL_GPUCommandBuffer *pCmd = bCheck ? GpuFrameCmdTake() : GpuFrameCmd();
  if (pCmd == NULL)
    return false;
  const Uint32 nMixAt = GpuSceneUploadCopy(pCmd, pieces, nPieces);
  GpuSceneFogLight(pCmd, pFrame, lay);

  SceneParams params;
  SDL_zero(params);
  params.nWidth = static_cast<Uint32>(pFrame->nLayerW);
  params.nRows = static_cast<Uint32>(pFrame->nLayerRows);
  params.nStep = static_cast<Uint32>(pFrame->nStep);
  params.nViewW = static_cast<Uint32>(pFrame->nWidth);
  params.nScrollX = static_cast<Uint32>(pFrame->nScrollX);
  params.nScrollY = static_cast<Uint32>(pFrame->nScrollY);
  params.nRingX = static_cast<Uint32>(pFrame->nRingTilesX);
  params.nRingY = static_cast<Uint32>(pFrame->nRingTilesY);
  params.nHeight = static_cast<Uint32>(pFrame->nHeight > pFrame->nRows ? pFrame->nHeight : pFrame->nRows);
  params.nBinsX = static_cast<Uint32>(pFrame->nBinsX);
  params.nCellsAt = lay.nCellsAt;
  params.nBinListAt = lay.nBinListAt;
  params.nBytesAt = lay.nBytesAt;
  params.nCopiedAt = lay.nCopiedAt;
  params.nOverFrom = g_nHdOverFrom;
  params.nExtraAt = lay.nExtraAt;
  params.nLooksAt = bFull && pFrame->nLooksBytes != 0 ? lay.nLooksAt : 0;
  params.nTimeMs = static_cast<Uint32>(SDL_GetTicks() % 3600000);
  params.nUnderAt = g_bHdScene ? lay.nBuffer : 0;
  g_nHdUnderAt = params.nUnderAt;
  GpuSceneRun(pCmd, pFrame, lay, params, bMix ? pFrame->pPrims : pHdPrims, bShow, bZoom, bFull);
  if (bMix) {
    GpuSceneKeepSd(pCmd, nMixAt, pFrame->nPrims * static_cast<Uint32>(sizeof(ScenePrim)));
    GpuSceneRun(pCmd, pFrame, lay, params, pHdPrims, bShow, bZoom, bFull);
    g_bHdMixed = g_hdShownSd != NULL;
  }
  GpuSceneDrawn(pFrame, bShow, bCheck, bZoom, bFull);

  // A copy of the screen made again holds nothing the cover can point at.
  const bool bShown = bShow && !bShownFresh;
  if (bCheck)
    GpuSceneCheck(pCmd, pFrame, bZoom, nDown);
  return bShown;
}
