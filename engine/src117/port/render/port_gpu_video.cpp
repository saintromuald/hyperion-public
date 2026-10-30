// port_gpu_video.cpp -- a video in full colour over the frame (docs/modules/render.md, "A
// cinematic in full colour"): the cinematic port_stormvid.cpp plays, as its decoder's planes, 8
// or 16 bits a sample. PortGpuVideoSet copies a picture into an upload buffer at once, since the
// decoder may write over its planes after; the next frame sends it up, PSVideoColour
// (shaders/video.hlsl) puts it in colour at the video's size in 16-bit float, and PSVideoScale
// resamples that across, then down into the window, fitted with its shape kept and dithered to
// the swapchain's 8 bits. Presentation only.
#include <windows.h>
#include <math.h>
#include <string.h>
#include "port_gpu_internal.h"
#include "../data/port_video.h"  // PortVideoPlanes
#include "PSVideoColour.dxil.h"
#include "PSVideoColour.spv.h"
#include "PSVideoScale.dxil.h"
#include "PSVideoScale.spv.h"
#if defined(__APPLE__)
#include "PSVideoColour.msl.h"
#include "PSVideoScale.msl.h"
#endif

namespace {

// video.hlsl's VideoColourParams and VideoScaleParams.
struct VideoColourParams {
  float afLevels[4];
  float afToR[4], afToG[4], afToB[4];
  float afKind[4];
};
struct VideoScaleParams {
  float afBox[4];
  float afSrc[4];
  float afMore[4];
};

// D3D12's alignments for a texture's rows and its start in an upload, kept so SDL copies nothing
const Uint32 ROW_ALIGN = 256;
const Uint32 START_ALIGN = 512;

SDL_GPUGraphicsPipeline *g_colourPipeline;
SDL_GPUGraphicsPipeline *g_acrossPipeline;
SDL_GPUGraphicsPipeline *g_downPipeline;

// The picture's planes, as many as it has (1 with no chroma, 2 with the chroma in one), their
// upload, and where each plane's rows start in it.
struct Plane {
  SDL_GPUTexture *pTexture;
  SDL_GPUTextureFormat format;
  int nWidth, nHeight;
  Uint32 nTexel;  // bytes
  Uint32 nStart, nPitch;
};
Plane g_aPlane[3];
int g_nPlanes;
SDL_GPUTransferBuffer *g_pUp;
Uint32 g_nUp;
bool g_bOn;       // a video is shown
bool g_bWaiting;  // a picture is in g_pUp that has not gone up
int g_nVideoW, g_nVideoH;
int g_nShowW, g_nShowH;  // the shape it is fitted to the window at
VideoColourParams g_colour;
SDL_GPUTexture *g_rgb;     // the picture in colour, at its size
SDL_GPUTexture *g_across;  // resampled across: the drawn width by the picture's rows
int g_nAcrossW, g_nAcrossH;
unsigned g_nSeed;

void Release(SDL_GPUTexture *&p)
{
  if (p != NULL)
    SDL_ReleaseGPUTexture(g_device, p);
  p = NULL;
}

void ReleasePlanes(void)
{
  for (Plane &pl : g_aPlane)
    Release(pl.pTexture);
  memset(g_aPlane, 0, sizeof g_aPlane);
  g_nPlanes = 0;
  Release(g_rgb);
  Release(g_across);
  g_nAcrossW = g_nAcrossH = 0;
  if (g_pUp != NULL)
    SDL_ReleaseGPUTransferBuffer(g_device, g_pUp);
  g_pUp = NULL;
  g_nUp = 0;
  g_nVideoW = g_nVideoH = 0;
}

// The planes' textures for a picture of this shape, and the upload for them. False when the
// device gave none.
bool Fit(const PortVideoPlanes &v)
{
  const bool b16 = v.nBits > 8;
  const int cw = (v.nWidth + (1 << v.xs) - 1) >> v.xs, ch = (v.nHeight + (1 << v.ys) - 1) >> v.ys;
  Plane want[3];
  memset(want, 0, sizeof want);
  int nPlanes = v.bMono ? 1 : v.bPaired ? 2 : 3;
  want[0].format = b16 ? SDL_GPU_TEXTUREFORMAT_R16_UNORM : SDL_GPU_TEXTUREFORMAT_R8_UNORM;
  want[0].nWidth = v.nWidth;
  want[0].nHeight = v.nHeight;
  want[0].nTexel = b16 ? 2 : 1;
  for (int k = 1; k < nPlanes; ++k) {
    want[k].format = v.bPaired ? (b16 ? SDL_GPU_TEXTUREFORMAT_R16G16_UNORM : SDL_GPU_TEXTUREFORMAT_R8G8_UNORM)
                               : want[0].format;
    want[k].nWidth = cw;
    want[k].nHeight = ch;
    want[k].nTexel = (b16 ? 2 : 1) * (v.bPaired ? 2 : 1);
  }
  Uint32 nAt = 0;
  for (int k = 0; k < nPlanes; ++k) {
    want[k].nStart = nAt;
    want[k].nPitch = (static_cast<Uint32>(want[k].nWidth) * want[k].nTexel + ROW_ALIGN - 1) & ~(ROW_ALIGN - 1);
    nAt = (nAt + want[k].nPitch * static_cast<Uint32>(want[k].nHeight) + START_ALIGN - 1) & ~(START_ALIGN - 1);
  }
  bool bSame = nPlanes == g_nPlanes && v.nWidth == g_nVideoW && v.nHeight == g_nVideoH;
  for (int k = 0; bSame && k < nPlanes; ++k)
    bSame = g_aPlane[k].format == want[k].format && g_aPlane[k].nWidth == want[k].nWidth &&
            g_aPlane[k].nHeight == want[k].nHeight;
  if (bSame && g_pUp != NULL && g_rgb != NULL)
    return true;
  ReleasePlanes();
  for (int k = 0; k < nPlanes; ++k) {
    g_aPlane[k] = want[k];
    g_aPlane[k].pTexture = GpuTexture(want[k].format, SDL_GPU_TEXTUREUSAGE_SAMPLER, want[k].nWidth, want[k].nHeight);
  }
  g_nPlanes = nPlanes;
  g_rgb = GpuTexture(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
                     SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, v.nWidth, v.nHeight);
  g_pUp = GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, nAt);
  g_nUp = nAt;
  g_nVideoW = v.nWidth;
  g_nVideoH = v.nHeight;
  bool bOk = g_rgb != NULL && g_pUp != NULL;
  for (int k = 0; k < nPlanes; ++k)
    bOk = bOk && g_aPlane[k].pTexture != NULL;
  if (!bOk)
    ReleasePlanes();
  return bOk;
}

// How PSVideoColour reads the planes: a texel's value to Y', Cb and Cr at the video's depth and
// range, and those to R', G' and B' by its matrix.
void ColourOf(const PortVideoPlanes &v, VideoColourParams *c)
{
  memset(c, 0, sizeof *c);
  const double dMax = v.nBits > 8 ? 65535.0 : 255.0;  // a texel's value at 1.0
  const double dStep = ldexp(1.0, v.nBits - 8);       // a step of 8 bits at the video's depth
  if (v.bFull || v.nMatrix == 3) {
    const double dTop = ldexp(1.0, v.nBits) - 1.0;
    c->afLevels[0] = static_cast<float>(dMax / dTop);
    c->afLevels[1] = 0.0f;
    c->afLevels[2] = static_cast<float>(dMax / dTop);
    c->afLevels[3] = static_cast<float>(-ldexp(1.0, v.nBits - 1) / dTop);
  } else {
    c->afLevels[0] = static_cast<float>(dMax / (219.0 * dStep));
    c->afLevels[1] = static_cast<float>(-16.0 / 219.0);
    c->afLevels[2] = static_cast<float>(dMax / (224.0 * dStep));
    c->afLevels[3] = static_cast<float>(-128.0 / 224.0);
  }
  static const double kKr[3] = {0.299, 0.2126, 0.2627}, kKb[3] = {0.114, 0.0722, 0.0593};
  const int m = v.nMatrix >= 0 && v.nMatrix < 3 ? v.nMatrix : 0;
  const double kr = kKr[m], kb = kKb[m], kg = 1.0 - kr - kb;
  c->afToR[0] = 1.0f;
  c->afToR[2] = static_cast<float>(2.0 * (1.0 - kr));
  c->afToG[0] = 1.0f;
  c->afToG[1] = static_cast<float>(-2.0 * kb * (1.0 - kb) / kg);
  c->afToG[2] = static_cast<float>(-2.0 * kr * (1.0 - kr) / kg);
  c->afToB[0] = 1.0f;
  c->afToB[1] = static_cast<float>(2.0 * (1.0 - kb));
  c->afKind[0] = v.bPaired ? 1.0f : 0.0f;
  c->afKind[1] = v.bMono ? 1.0f : 0.0f;
  c->afKind[2] = v.nMatrix == 3 ? 1.0f : 0.0f;
}

void Pass(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pTarget, bool bClear, SDL_GPUGraphicsPipeline *pPipeline,
          const SDL_FRect &box, SDL_GPUTextureSamplerBinding *pBindings, Uint32 nBindings, const void *pParams,
          Uint32 nParams)
{
  SDL_GPUColorTargetInfo target;
  SDL_zero(target);
  target.texture = pTarget;
  target.load_op = bClear ? SDL_GPU_LOADOP_DONT_CARE : SDL_GPU_LOADOP_LOAD;
  target.store_op = SDL_GPU_STOREOP_STORE;
  target.cycle = bClear;
  SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(pCmd, &target, 1, NULL);
  SDL_BindGPUGraphicsPipeline(pPass, pPipeline);
  SDL_GPUViewport viewport = {box.x, box.y, box.w, box.h, 0.0f, 1.0f};
  SDL_SetGPUViewport(pPass, &viewport);
  SDL_BindGPUFragmentSamplers(pPass, 0, pBindings, nBindings);
  SDL_PushGPUFragmentUniformData(pCmd, 0, pParams, nParams);
  SDL_DrawGPUPrimitives(pPass, 3, 1, 0, 0);
  SDL_EndGPURenderPass(pPass);
}

}  // namespace

void GpuVideoInit(SDL_GPUShader *pVertex)
{
  SDL_GPUShader *pColour = GpuShader(GPU_CODE(PSVideoColour), "PSVideoColour", SDL_GPU_SHADERSTAGE_FRAGMENT, 3, 0, 1);
  SDL_GPUShader *pScale = GpuShader(GPU_CODE(PSVideoScale), "PSVideoScale", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0, 1);
  if (pVertex != NULL && pColour != NULL && pScale != NULL) {
    g_colourPipeline = GpuPipeline(pVertex, pColour, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
    g_acrossPipeline = GpuPipeline(pVertex, pScale, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
    g_downPipeline = GpuPipeline(pVertex, pScale, g_swapFormat);
  }
  if (pColour != NULL)
    SDL_ReleaseGPUShader(g_device, pColour);
  if (pScale != NULL)
    SDL_ReleaseGPUShader(g_device, pScale);
}

void GpuVideoRelease(void)
{
  ReleasePlanes();
  for (SDL_GPUGraphicsPipeline **pp : {&g_colourPipeline, &g_acrossPipeline, &g_downPipeline}) {
    if (*pp != NULL)
      SDL_ReleaseGPUGraphicsPipeline(g_device, *pp);
    *pp = NULL;
  }
  g_bOn = g_bWaiting = false;
}

void PortGpuVideoSet(const PortVideoPlanes *pPlanes, int nShowW, int nShowH)
{
  if (g_device == NULL || g_downPipeline == NULL)
    return;
  g_bWaiting = false;
  if (pPlanes == NULL || pPlanes->nWidth <= 0 || pPlanes->nHeight <= 0 || !Fit(*pPlanes)) {
    g_bOn = false;
    ReleasePlanes();
    return;
  }
  Uint8 *pTo = static_cast<Uint8 *>(SDL_MapGPUTransferBuffer(g_device, g_pUp, true));
  if (pTo == NULL)
    return;
  for (int k = 0; k < g_nPlanes; ++k) {
    const Plane &pl = g_aPlane[k];
    const size_t nRow = static_cast<size_t>(pl.nWidth) * pl.nTexel;
    for (int y = 0; y < pl.nHeight; ++y)
      memcpy(pTo + pl.nStart + static_cast<size_t>(y) * pl.nPitch, pPlanes->apPlane[k] + y * pPlanes->anStride[k], nRow);
  }
  SDL_UnmapGPUTransferBuffer(g_device, g_pUp);
  ColourOf(*pPlanes, &g_colour);
  g_nShowW = nShowW > 0 ? nShowW : pPlanes->nWidth;
  g_nShowH = nShowH > 0 ? nShowH : pPlanes->nHeight;
  g_bOn = g_bWaiting = true;
}

bool GpuVideoShown(void) { return g_bOn; }

void GpuVideoUp(SDL_GPUCommandBuffer *pCmd)
{
  if (!g_bOn || !g_bWaiting)
    return;
  g_bWaiting = false;
  SDL_GPUCopyPass *pCopy = SDL_BeginGPUCopyPass(pCmd);
  for (int k = 0; k < g_nPlanes; ++k) {
    const Plane &pl = g_aPlane[k];
    SDL_GPUTextureTransferInfo src;
    SDL_GPUTextureRegion dst;
    SDL_zero(src);
    SDL_zero(dst);
    src.transfer_buffer = g_pUp;
    src.offset = pl.nStart;
    src.pixels_per_row = pl.nPitch / pl.nTexel;
    src.rows_per_layer = static_cast<Uint32>(pl.nHeight);
    dst.texture = pl.pTexture;
    dst.w = static_cast<Uint32>(pl.nWidth);
    dst.h = static_cast<Uint32>(pl.nHeight);
    dst.d = 1;
    SDL_UploadToGPUTexture(pCopy, &src, &dst, true);
  }
  SDL_EndGPUCopyPass(pCopy);
  // the chroma's planes stand in for themselves where there are fewer
  SDL_GPUTexture *pCb = g_nPlanes > 1 ? g_aPlane[1].pTexture : g_aPlane[0].pTexture;
  SDL_GPUTexture *pCr = g_nPlanes > 2 ? g_aPlane[2].pTexture : pCb;
  SDL_GPUTextureSamplerBinding planes[3] = {{g_aPlane[0].pTexture, g_nearest}, {pCb, g_linear}, {pCr, g_linear}};
  const SDL_FRect whole = {0.0f, 0.0f, static_cast<float>(g_nVideoW), static_cast<float>(g_nVideoH)};
  Pass(pCmd, g_rgb, true, g_colourPipeline, whole, planes, 3, &g_colour, sizeof g_colour);
}

void GpuVideoDraw(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pSwap, Uint32 nSwapW, Uint32 nSwapH)
{
  if (!g_bOn || g_rgb == NULL)
    return;
  SDL_FRect box;
  PortGpuLetterbox(static_cast<float>(nSwapW), static_cast<float>(nSwapH), static_cast<float>(g_nShowW),
                   static_cast<float>(g_nShowH), &box);
  box.x = SDL_floorf(box.x);
  box.y = SDL_floorf(box.y);
  const int nDrawnW = static_cast<int>(box.w), nDrawnH = static_cast<int>(box.h);
  if (nDrawnW <= 0 || nDrawnH <= 0)
    return;
  if (g_across == NULL || g_nAcrossW != nDrawnW || g_nAcrossH != g_nVideoH) {
    Release(g_across);
    g_across = GpuTexture(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
                          SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, nDrawnW, g_nVideoH);
    g_nAcrossW = g_across != NULL ? nDrawnW : 0;
    g_nAcrossH = g_across != NULL ? g_nVideoH : 0;
    if (g_across == NULL)
      return;
  }
  // a kernel widened past 8 of the source's pixels to one drawn gains little for its cost
  const float fWideX = SDL_min(8.0f, static_cast<float>(g_nVideoW) / box.w);
  const float fWideY = SDL_min(8.0f, static_cast<float>(g_nVideoH) / box.h);
  VideoScaleParams across = {{0.0f, 0.0f, box.w, static_cast<float>(g_nVideoH)},
                             {static_cast<float>(g_nVideoW), static_cast<float>(g_nVideoH), fWideX, 1.0f},
                             {0.0f, 0.0f, 0.0f, 0.0f}};
  SDL_GPUTextureSamplerBinding from = {g_rgb, g_nearest};
  const SDL_FRect acrossBox = {0.0f, 0.0f, box.w, static_cast<float>(g_nVideoH)};
  Pass(pCmd, g_across, true, g_acrossPipeline, acrossBox, &from, 1, &across, sizeof across);
  VideoScaleParams down = {{box.x, box.y, box.w, box.h},
                           {box.w, static_cast<float>(g_nVideoH), fWideY, 0.0f},
                           {1.0f, static_cast<float>(++g_nSeed & 0xFFFF), 0.0f, 0.0f}};
  from.texture = g_across;
  Pass(pCmd, pSwap, false, g_downPipeline, box, &from, 1, &down, sizeof down);
}
