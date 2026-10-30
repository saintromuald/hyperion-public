// port_gpu_ui.cpp -- the front end (F1): the planet and the screen's primitives at the window's
// resolution (docs/renderer.md). Presentation only.
#include <windows.h>
#include <stdio.h>
#include "VSUi.dxil.h"
#include "VSUi.spv.h"
#include "PSUi.dxil.h"
#include "PSUi.spv.h"
#include "VSFull.dxil.h"
#include "VSFull.spv.h"
#include "PSPlanet.dxil.h"
#include "PSPlanet.spv.h"
#if defined(__APPLE__)
#include "VSUi.msl.h"
#include "PSUi.msl.h"
#include "VSFull.msl.h"
#include "PSPlanet.msl.h"
#endif
#include "port_gpu_internal.h"
#include "port_overlay.h"  // PortOverlayBuild
#include "../hud/port_hud.h"  // PortHudPreviewTake

// The front end (F1).

static SDL_GPUGraphicsPipeline *g_uiPipeline;
static SDL_GPUGraphicsPipeline *g_planetPipeline;
static SDL_GPUGraphicsPipeline *g_overlayPipeline;  // the UI pipeline for the swapchain's format
static bool g_uiTried;
static SDL_GPUTexture *g_uiAtlas;           // the glyph atlas, R8
static SDL_GPUTransferBuffer *g_uiAtlasUp;
static SDL_GPUTexture *g_uiPictures;        // the picture atlas, RGBA8
static SDL_GPUTransferBuffer *g_uiPicturesUp;
static SDL_GPUTexture *g_uiPlanet;          // the planet, RGBA8, at its own resolution
static int g_nUiPlanetW;
static int g_nUiPlanetH;
static SDL_GPUTexture *g_uiTarget;          // the screen, RGBA8, at the window's size
static int g_nUiW;
static int g_nUiH;
static SDL_GPUTransferBuffer *g_uiDown;     // the screen read back, for a snapshot
static Uint32 g_nUiDown;
static SDL_GPUBuffer *g_uiPrims;
static SDL_GPUTransferBuffer *g_uiPrimsUp;
static Uint32 g_nUiPrims;                   // bytes the two hold
static SDL_GPUCommandBuffer *g_uiCmd;        // the frame begun
static SDL_GPUTexture *g_uiSwap;
static char g_szUiError[256];               // why the front end cannot draw

struct UiTargetParams {
  float fWidth, fHeight;
  float fUnused[2];
};

void GpuUiRelease(void)
{
  if (g_device == NULL)
    return;
  if (g_uiPipeline != NULL)
    SDL_ReleaseGPUGraphicsPipeline(g_device, g_uiPipeline);
  if (g_planetPipeline != NULL)
    SDL_ReleaseGPUGraphicsPipeline(g_device, g_planetPipeline);
  if (g_overlayPipeline != NULL)
    SDL_ReleaseGPUGraphicsPipeline(g_device, g_overlayPipeline);
  g_overlayPipeline = NULL;
  if (g_uiAtlas != NULL)
    SDL_ReleaseGPUTexture(g_device, g_uiAtlas);
  if (g_uiAtlasUp != NULL)
    SDL_ReleaseGPUTransferBuffer(g_device, g_uiAtlasUp);
  if (g_uiPictures != NULL)
    SDL_ReleaseGPUTexture(g_device, g_uiPictures);
  if (g_uiPicturesUp != NULL)
    SDL_ReleaseGPUTransferBuffer(g_device, g_uiPicturesUp);
  if (g_uiPlanet != NULL)
    SDL_ReleaseGPUTexture(g_device, g_uiPlanet);
  if (g_uiTarget != NULL)
    SDL_ReleaseGPUTexture(g_device, g_uiTarget);
  if (g_uiDown != NULL)
    SDL_ReleaseGPUTransferBuffer(g_device, g_uiDown);
  if (g_uiPrims != NULL)
    SDL_ReleaseGPUBuffer(g_device, g_uiPrims);
  if (g_uiPrimsUp != NULL)
    SDL_ReleaseGPUTransferBuffer(g_device, g_uiPrimsUp);
  g_uiPipeline = g_planetPipeline = NULL;
  g_uiAtlas = g_uiPlanet = g_uiTarget = g_uiPictures = NULL;
  g_uiAtlasUp = g_uiDown = g_uiPrimsUp = g_uiPicturesUp = NULL;
  g_uiPrims = NULL;
  g_nUiPlanetW = g_nUiPlanetH = g_nUiW = g_nUiH = 0;
  g_nUiDown = g_nUiPrims = 0;
  g_uiTried = false;
}

// The pipelines and the atlas, made on the front end's first frame.
// The UI's pipeline for a target of a format: straight alpha over what is there.
static SDL_GPUGraphicsPipeline *GpuUiPipeline(SDL_GPUShader *pVs, SDL_GPUShader *pPs, SDL_GPUTextureFormat format)
{
  SDL_GPUColorTargetDescription target;
  SDL_GPUGraphicsPipelineCreateInfo info;
  SDL_zero(target);
  SDL_zero(info);
  target.format = format;
  target.blend_state.enable_blend = true;
  target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  info.vertex_shader = pVs;
  info.fragment_shader = pPs;
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  info.target_info.color_target_descriptions = &target;
  info.target_info.num_color_targets = 1;
  return SDL_CreateGPUGraphicsPipeline(g_device, &info);
}

static bool GpuUiInit(void)
{
  if (g_uiTried)
    return g_uiPipeline != NULL && g_planetPipeline != NULL && g_uiAtlas != NULL && g_uiPictures != NULL;
  g_uiTried = true;
  SDL_GPUShader *pVsUi = GpuShader(GPU_CODE(VSUi), "VSUi", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, 1);
  SDL_GPUShader *pPsUi = GpuShader(GPU_CODE(PSUi), "PSUi", SDL_GPU_SHADERSTAGE_FRAGMENT, 3, 0, 1);
  SDL_GPUShader *pVsFull = GpuShader(GPU_CODE(VSFull), "VSFull", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 0);
  SDL_GPUShader *pPsPlanet = GpuShader(GPU_CODE(PSPlanet), "PSPlanet", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 1);
  if (pVsUi != NULL && pPsUi != NULL)
    g_uiPipeline = GpuUiPipeline(pVsUi, pPsUi, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
  if (pVsFull != NULL && pPsPlanet != NULL)
    g_planetPipeline = GpuPipeline(pVsFull, pPsPlanet, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
  // the overlay over a game's frame (GpuUiOverlay) draws straight into the swapchain
  if (pVsUi != NULL && pPsUi != NULL && g_claimed && g_swapFormat != SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM)
    g_overlayPipeline = GpuUiPipeline(pVsUi, pPsUi, g_swapFormat);
  const char *pszShaderError = pVsUi == NULL || pPsUi == NULL || pVsFull == NULL || pPsPlanet == NULL
                                   ? SDL_GetError() : NULL;
  if (pszShaderError != NULL)
    SDL_snprintf(g_szUiError, sizeof g_szUiError, "a shader did not build:%s%s%s%s: %s", pVsUi == NULL ? " VSUi" : "",
                 pPsUi == NULL ? " PSUi" : "", pVsFull == NULL ? " VSFull" : "", pPsPlanet == NULL ? " PSPlanet" : "",
                 pszShaderError);
  if (pVsUi != NULL)
    SDL_ReleaseGPUShader(g_device, pVsUi);
  if (pPsUi != NULL)
    SDL_ReleaseGPUShader(g_device, pPsUi);
  if (pVsFull != NULL)
    SDL_ReleaseGPUShader(g_device, pVsFull);
  if (pPsPlanet != NULL)
    SDL_ReleaseGPUShader(g_device, pPsPlanet);
  int nAtlasW = 0;
  int nAtlasH = 0;
  UiAtlas(&nAtlasW, &nAtlasH);
  g_uiAtlas = GpuTexture(SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, nAtlasW, nAtlasH);
  g_uiAtlasUp = GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, static_cast<Uint32>(nAtlasW * nAtlasH));
  int nPicturesW = 0;
  int nPicturesH = 0;
  UiPictures(&nPicturesW, &nPicturesH);
  g_uiPictures = GpuTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, nPicturesW, nPicturesH);
  g_uiPicturesUp = GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, static_cast<Uint32>(nPicturesW * nPicturesH * 4));
  if (g_uiPipeline == NULL || g_planetPipeline == NULL || g_uiAtlas == NULL || g_uiAtlasUp == NULL ||
      g_uiPictures == NULL || g_uiPicturesUp == NULL) {
    if (g_szUiError[0] == 0)
      SDL_snprintf(g_szUiError, sizeof g_szUiError, "did not build:%s%s%s: %s",
                   g_uiPipeline == NULL ? " the UI pipeline" : "", g_planetPipeline == NULL ? " the planet pipeline" : "",
                   g_uiAtlas == NULL || g_uiAtlasUp == NULL || g_uiPictures == NULL || g_uiPicturesUp == NULL ? " the atlases" : "",
                   SDL_GetError());
    return false;
  }
  return true;
}

const char *PortGpuUiError(void)
{
  if (g_device == NULL)
    return "no GPU device";
  return g_szUiError[0] != 0 ? g_szUiError : "none";
}

bool PortGpuUiShown(void)
{
  return g_uiSwap != NULL;
}

bool GpuUiFrameOpen(void)
{
  return g_uiCmd != NULL;
}

bool PortGpuUiBegin(int nWindowW, int nWindowH, int *pnWidth, int *pnHeight)
{
  if (g_device == NULL || !GpuUiInit())
    return false;
  GpuSwapchainWaiting();
  g_uiCmd = SDL_AcquireGPUCommandBuffer(g_device);
  if (g_uiCmd == NULL)
    return false;
  g_uiSwap = NULL;
  Uint32 nSwapW = 0;
  Uint32 nSwapH = 0;
  // waits for an image: without one the frame is drawn and never seen, and on a slow GPU (an
  // Intel one on Linux) the unseen frames' planets held the seen ones to a few a second
  if (g_claimed && SDL_WaitAndAcquireGPUSwapchainTexture(g_uiCmd, g_window, &g_uiSwap, &nSwapW, &nSwapH) &&
      g_uiSwap != NULL) {
    *pnWidth = static_cast<int>(nSwapW);
    *pnHeight = static_cast<int>(nSwapH);
  } else {
    // hidden or minimised: drawn all the same, for a snapshot
    g_uiSwap = NULL;
    *pnWidth = nWindowW;
    *pnHeight = nWindowH;
  }
  if (*pnWidth > 0 && *pnHeight > 0)
    return true;
  // nothing to draw to (a window made 0 high): the buffer goes back, not left to the next frame
  SDL_SubmitGPUCommandBuffer(g_uiCmd);
  g_uiCmd = NULL;
  return false;
}

// Fits a texture to a size, made again when it changes.
static bool GpuUiFit(SDL_GPUTexture **ppTexture, int *pnW, int *pnH, int nW, int nH, SDL_GPUTextureUsageFlags usage)
{
  if (*ppTexture != NULL && *pnW == nW && *pnH == nH)
    return true;
  if (*ppTexture != NULL)
    SDL_ReleaseGPUTexture(g_device, *ppTexture);
  *ppTexture = GpuTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, usage, nW, nH);
  *pnW = nW;
  *pnH = nH;
  return *ppTexture != NULL;
}

// Up: the primitives, and the atlases' new rows, in a copy pass of pCmd. False when the
// primitives' buffer could not be had.
static bool GpuUiUpload(SDL_GPUCommandBuffer *pCmd, const UiPrim *pPrims, int nPrims)
{
  const Uint32 nPrimBytes = static_cast<Uint32>(SDL_max(nPrims, 1) * sizeof(UiPrim));
  if (g_nUiPrims < nPrimBytes) {
    if (g_uiPrims != NULL)
      SDL_ReleaseGPUBuffer(g_device, g_uiPrims);
    if (g_uiPrimsUp != NULL)
      SDL_ReleaseGPUTransferBuffer(g_device, g_uiPrimsUp);
    const Uint32 nCapacity = nPrimBytes * 2;
    SDL_GPUBufferCreateInfo info;
    SDL_zero(info);
    info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    info.size = nCapacity;
    g_uiPrims = SDL_CreateGPUBuffer(g_device, &info);
    g_uiPrimsUp = GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, nCapacity);
    g_nUiPrims = g_uiPrims != NULL && g_uiPrimsUp != NULL ? nCapacity : 0;
    if (g_nUiPrims == 0)
      return false;
  }
  SDL_GPUCopyPass *pCopy = SDL_BeginGPUCopyPass(pCmd);
  if (nPrims > 0) {
    void *pMapped = SDL_MapGPUTransferBuffer(g_device, g_uiPrimsUp, true);
    if (pMapped != NULL) {
      SDL_memcpy(pMapped, pPrims, static_cast<size_t>(nPrims) * sizeof(UiPrim));
      SDL_UnmapGPUTransferBuffer(g_device, g_uiPrimsUp);
      SDL_GPUTransferBufferLocation from;
      SDL_GPUBufferRegion to;
      from.transfer_buffer = g_uiPrimsUp;
      from.offset = 0;
      to.buffer = g_uiPrims;
      to.offset = 0;
      to.size = static_cast<Uint32>(nPrims * sizeof(UiPrim));
      SDL_UploadToGPUBuffer(pCopy, &from, &to, true);
    }
  }
  int nFirst = 0;
  int nRows = 0;
  if (UiAtlasDirty(&nFirst, &nRows)) {
    int nAtlasW = 0;
    int nAtlasH = 0;
    const unsigned char *pAtlas = UiAtlas(&nAtlasW, &nAtlasH);
    Uint8 *pMapped = static_cast<Uint8 *>(SDL_MapGPUTransferBuffer(g_device, g_uiAtlasUp, true));
    if (pMapped != NULL) {
      SDL_memcpy(pMapped, pAtlas + static_cast<size_t>(nFirst) * nAtlasW, static_cast<size_t>(nRows) * nAtlasW);
      SDL_UnmapGPUTransferBuffer(g_device, g_uiAtlasUp);
      SDL_GPUTextureTransferInfo src;
      SDL_GPUTextureRegion dst;
      SDL_zero(src);
      SDL_zero(dst);
      src.transfer_buffer = g_uiAtlasUp;
      src.pixels_per_row = static_cast<Uint32>(nAtlasW);
      src.rows_per_layer = static_cast<Uint32>(nRows);
      dst.texture = g_uiAtlas;
      dst.y = static_cast<Uint32>(nFirst);
      dst.w = static_cast<Uint32>(nAtlasW);
      dst.h = static_cast<Uint32>(nRows);
      dst.d = 1;
      // not cycled: a cycled texture keeps only the rows written, and the glyphs already
      // in the atlas would be lost
      SDL_UploadToGPUTexture(pCopy, &src, &dst, false);
      UiAtlasClean();
    }
  }
  if (UiPicturesDirty(&nFirst, &nRows)) {
    int nPicturesW = 0;
    int nPicturesH = 0;
    const unsigned char *pPictures = UiPictures(&nPicturesW, &nPicturesH);
    Uint8 *pMapped = static_cast<Uint8 *>(SDL_MapGPUTransferBuffer(g_device, g_uiPicturesUp, true));
    if (pMapped != NULL) {
      const size_t nRow = static_cast<size_t>(nPicturesW) * 4;
      SDL_memcpy(pMapped, pPictures + static_cast<size_t>(nFirst) * nRow, static_cast<size_t>(nRows) * nRow);
      SDL_UnmapGPUTransferBuffer(g_device, g_uiPicturesUp);
      SDL_GPUTextureTransferInfo src;
      SDL_GPUTextureRegion dst;
      SDL_zero(src);
      SDL_zero(dst);
      src.transfer_buffer = g_uiPicturesUp;
      src.pixels_per_row = static_cast<Uint32>(nPicturesW);
      src.rows_per_layer = static_cast<Uint32>(nRows);
      dst.texture = g_uiPictures;
      dst.y = static_cast<Uint32>(nFirst);
      dst.w = static_cast<Uint32>(nPicturesW);
      dst.h = static_cast<Uint32>(nRows);
      dst.d = 1;
      SDL_UploadToGPUTexture(pCopy, &src, &dst, false);  // not cycled, as the glyphs' atlas
      UiPicturesClean();
    }
  }
  SDL_EndGPUCopyPass(pCopy);
  return true;
}

bool PortGpuUiEnd(int nWidth, int nHeight, const PlanetParams *pPlanet, float fPlanetScale, const UiPrim *pPrims,
                  int nPrims, const char *pszSnap)
{
  SDL_GPUCommandBuffer *pCmd = g_uiCmd;
  g_uiCmd = NULL;
  if (pCmd == NULL)
    return false;
  // Settings' HUD tab's picture goes between the list's first part and the rest, which the HUD's
  // drawing would write over in the UI kit's list (port_hud.h, PortHudPreview)
  const PortHudPreview *pPreview = PortHudPreviewTake();
  const int nFirst = pPreview != NULL ? SDL_clamp(pPreview->nAtPrim, 0, nPrims) : nPrims;
  std::vector<UiPrim> rest;
  if (pPreview != NULL)
    rest.assign(pPrims + nFirst, pPrims + nPrims);
  const int nPlanetW = SDL_max(1, static_cast<int>(nWidth * fPlanetScale + 0.5f));
  const int nPlanetH = SDL_max(1, static_cast<int>(nHeight * fPlanetScale + 0.5f));
  const SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  bool bOk = GpuUiFit(&g_uiPlanet, &g_nUiPlanetW, &g_nUiPlanetH, nPlanetW, nPlanetH, usage) &&
             GpuUiFit(&g_uiTarget, &g_nUiW, &g_nUiH, nWidth, nHeight, usage) && GpuUiUpload(pCmd, pPrims, nPrims);
  if (!bOk) {
    SDL_SubmitGPUCommandBuffer(pCmd);
    return false;
  }

  // the planet
  SDL_GPUColorTargetInfo target;
  SDL_zero(target);
  target.texture = g_uiPlanet;
  target.load_op = SDL_GPU_LOADOP_DONT_CARE;
  target.store_op = SDL_GPU_STOREOP_STORE;
  target.cycle = true;
  SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(pCmd, &target, 1, NULL);
  SDL_BindGPUGraphicsPipeline(pPass, g_planetPipeline);
  PlanetParams params = *pPlanet;
  // the shader works in the planet texture's pixels
  params.res[0] = static_cast<float>(nPlanetW);
  params.res[1] = static_cast<float>(nPlanetH);
  params.sunScreen[0] *= static_cast<float>(nPlanetW) / nWidth;
  params.sunScreen[1] *= static_cast<float>(nPlanetH) / nHeight;
  SDL_PushGPUFragmentUniformData(pCmd, 0, &params, sizeof params);
  SDL_DrawGPUPrimitives(pPass, 3, 1, 0, 0);
  SDL_EndGPURenderPass(pPass);

  // the screen: black, then the primitives in order
  SDL_zero(target);
  target.texture = g_uiTarget;
  target.clear_color.a = 1.0f;
  target.load_op = SDL_GPU_LOADOP_CLEAR;
  target.store_op = SDL_GPU_STOREOP_STORE;
  target.cycle = true;
  pPass = SDL_BeginGPURenderPass(pCmd, &target, 1, NULL);
  if (nFirst > 0) {
    SDL_BindGPUGraphicsPipeline(pPass, g_uiPipeline);
    SDL_BindGPUVertexStorageBuffers(pPass, 0, &g_uiPrims, 1);
    SDL_GPUTextureSamplerBinding bindings[3];
    bindings[0].texture = g_uiAtlas;
    bindings[0].sampler = g_linear;
    bindings[1].texture = g_uiPlanet;
    bindings[1].sampler = g_linear;
    bindings[2].texture = g_uiPictures;
    bindings[2].sampler = g_linear;
    SDL_BindGPUFragmentSamplers(pPass, 0, bindings, 3);
    UiTargetParams tp;
    tp.fWidth = static_cast<float>(nWidth);
    tp.fHeight = static_cast<float>(nHeight);
    tp.fUnused[0] = tp.fUnused[1] = 0.0f;
    SDL_PushGPUVertexUniformData(pCmd, 0, &tp, sizeof tp);
    SDL_PushGPUFragmentUniformData(pCmd, 0, &tp, sizeof tp);
    SDL_DrawGPUPrimitives(pPass, static_cast<Uint32>(nFirst) * 6u, 1, 0, 0);
  }
  SDL_EndGPURenderPass(pPass);
  if (pPreview != NULL) {
    GpuHudPreview(pCmd, g_uiTarget, static_cast<Uint32>(nWidth), static_cast<Uint32>(nHeight), *pPreview);
    if (GpuUiStage(pCmd, rest.data(), static_cast<int>(rest.size()), false)) {
      SDL_zero(target);
      target.texture = g_uiTarget;
      target.load_op = SDL_GPU_LOADOP_LOAD;
      target.store_op = SDL_GPU_STOREOP_STORE;
      pPass = SDL_BeginGPURenderPass(pCmd, &target, 1, NULL);
      GpuUiDrawPart(pCmd, pPass, static_cast<Uint32>(nWidth), static_cast<Uint32>(nHeight), 0,
                    static_cast<int>(rest.size()), false);
      SDL_EndGPURenderPass(pPass);
    }
  }

  if (g_uiSwap != NULL) {
    SDL_GPUBlitInfo blit;
    SDL_zero(blit);
    blit.source.texture = g_uiTarget;
    blit.source.w = static_cast<Uint32>(nWidth);
    blit.source.h = static_cast<Uint32>(nHeight);
    blit.destination.texture = g_uiSwap;
    blit.destination.w = static_cast<Uint32>(nWidth);
    blit.destination.h = static_cast<Uint32>(nHeight);
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit.filter = SDL_GPU_FILTER_NEAREST;
    SDL_BlitGPUTexture(pCmd, &blit);
  }
  g_uiSwap = NULL;

  if (pszSnap == NULL) {
    SDL_SubmitGPUCommandBuffer(pCmd);
    return true;
  }
  const Uint32 nBytes = static_cast<Uint32>(nWidth * nHeight * 4);
  if (g_nUiDown < nBytes) {
    if (g_uiDown != NULL)
      SDL_ReleaseGPUTransferBuffer(g_device, g_uiDown);
    g_uiDown = GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, nBytes);
    g_nUiDown = g_uiDown != NULL ? nBytes : 0;
  }
  if (g_uiDown == NULL) {
    SDL_SubmitGPUCommandBuffer(pCmd);
    return false;
  }
  SDL_GPUTextureRegion from;
  SDL_GPUTextureTransferInfo to;
  SDL_zero(from);
  SDL_zero(to);
  from.texture = g_uiTarget;
  from.w = static_cast<Uint32>(nWidth);
  from.h = static_cast<Uint32>(nHeight);
  from.d = 1;
  to.transfer_buffer = g_uiDown;
  to.pixels_per_row = static_cast<Uint32>(nWidth);
  to.rows_per_layer = static_cast<Uint32>(nHeight);
  SDL_GPUCopyPass *pCopy = SDL_BeginGPUCopyPass(pCmd);
  SDL_DownloadFromGPUTexture(pCopy, &from, &to);
  SDL_EndGPUCopyPass(pCopy);
  SDL_GPUFence *pFence = SDL_SubmitGPUCommandBufferAndAcquireFence(pCmd);
  if (pFence == NULL)
    return false;
  SDL_WaitForGPUFences(g_device, true, &pFence, 1);
  SDL_ReleaseGPUFence(g_device, pFence);
  bool bSaved = false;
  void *pPixels = SDL_MapGPUTransferBuffer(g_device, g_uiDown, false);
  if (pPixels != NULL) {
    bSaved = GpuSavePixels(pPixels, nWidth, nHeight, pszSnap);
    SDL_UnmapGPUTransferBuffer(g_device, g_uiDown);
  }
  return bSaved;
}

bool GpuUiDrawInto(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pTarget, int nWidth, int nHeight, const UiPrim *pPrims,
                   int nPrims)
{
  if (!GpuUiInit() || !GpuUiUpload(pCmd, pPrims, nPrims))
    return false;
  SDL_GPUColorTargetInfo target;
  SDL_zero(target);
  target.texture = pTarget;
  target.load_op = SDL_GPU_LOADOP_CLEAR;
  target.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(pCmd, &target, 1, NULL);
  if (nPrims > 0) {
    SDL_BindGPUGraphicsPipeline(pPass, g_uiPipeline);
    SDL_GPUViewport viewport = {0.0f, 0.0f, static_cast<float>(nWidth), static_cast<float>(nHeight), 0.0f, 1.0f};
    SDL_SetGPUViewport(pPass, &viewport);
    SDL_BindGPUVertexStorageBuffers(pPass, 0, &g_uiPrims, 1);
    SDL_GPUTextureSamplerBinding bindings[3];
    bindings[0].texture = g_uiAtlas;
    bindings[0].sampler = g_linear;
    bindings[1].texture = g_uiPlanet != NULL ? g_uiPlanet : g_uiAtlas;
    bindings[1].sampler = g_linear;
    bindings[2].texture = g_uiPictures;
    bindings[2].sampler = g_linear;
    SDL_BindGPUFragmentSamplers(pPass, 0, bindings, 3);
    UiTargetParams tp;
    tp.fWidth = static_cast<float>(nWidth);
    tp.fHeight = static_cast<float>(nHeight);
    tp.fUnused[0] = tp.fUnused[1] = 0.0f;
    SDL_PushGPUVertexUniformData(pCmd, 0, &tp, sizeof tp);
    SDL_PushGPUFragmentUniformData(pCmd, 0, &tp, sizeof tp);
    SDL_DrawGPUPrimitives(pPass, static_cast<Uint32>(nPrims) * 6u, 1, 0, 0);
  }
  SDL_EndGPURenderPass(pPass);
  return true;
}

// A list drawn in parts inside a pass on the swapchain the caller began (the HUD's slots,
// port_gpu_hud.cpp): GpuUiStage uploads it before the pass, GpuUiDrawPart draws nCount of its
// primitives from nFirst.
// The pipeline for a target: the swapchain's format, or RGBA8.
static SDL_GPUGraphicsPipeline *GpuUiPipelineFor(bool bSwap)
{
  return !bSwap || g_swapFormat == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ? g_uiPipeline : g_overlayPipeline;
}

bool GpuUiStage(SDL_GPUCommandBuffer *pCmd, const UiPrim *pPrims, int nPrims, bool bSwap)
{
  return nPrims > 0 && GpuUiInit() && GpuUiPipelineFor(bSwap) != NULL && GpuUiUpload(pCmd, pPrims, nPrims);
}

void GpuUiDrawPart(SDL_GPUCommandBuffer *pCmd, SDL_GPURenderPass *pPass, Uint32 nWidth, Uint32 nHeight, int nFirst,
                   int nCount, bool bSwap)
{
  if (nCount <= 0)
    return;
  SDL_BindGPUGraphicsPipeline(pPass, GpuUiPipelineFor(bSwap));
  SDL_BindGPUVertexStorageBuffers(pPass, 0, &g_uiPrims, 1);
  SDL_GPUTextureSamplerBinding bindings[3];
  bindings[0].texture = g_uiAtlas;
  bindings[0].sampler = g_linear;
  bindings[1].texture = g_uiPlanet != NULL ? g_uiPlanet : g_uiAtlas;
  bindings[1].sampler = g_linear;
  bindings[2].texture = g_uiPictures;
  bindings[2].sampler = g_linear;
  SDL_BindGPUFragmentSamplers(pPass, 0, bindings, 3);
  UiTargetParams tp;
  tp.fWidth = static_cast<float>(nWidth);
  tp.fHeight = static_cast<float>(nHeight);
  tp.fUnused[0] = tp.fUnused[1] = 0.0f;
  SDL_PushGPUVertexUniformData(pCmd, 0, &tp, sizeof tp);
  SDL_PushGPUFragmentUniformData(pCmd, 0, &tp, sizeof tp);
  SDL_DrawGPUPrimitives(pPass, static_cast<Uint32>(nCount) * 6u, 1, static_cast<Uint32>(nFirst) * 6u, 0);
}

// The overlay over a game's frame (port_overlay.cpp): its list drawn into the swapchain
// pSwap, nWidth x nHeight, after the frame, the game's frame in box.
void GpuUiOverlay(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pSwap, Uint32 nWidth, Uint32 nHeight, const SDL_FRect &box)
{
  if (!PortOverlayBuild(static_cast<int>(nWidth), static_cast<int>(nHeight), box.x, box.y, box.w, box.h))
    return;
  int nPrims = 0;
  const UiPrim *pPrims = UiPrims(&nPrims);
  if (nPrims == 0 || !GpuUiInit())
    return;
  SDL_GPUGraphicsPipeline *pPipeline = g_swapFormat == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ? g_uiPipeline : g_overlayPipeline;
  if (pPipeline == NULL || !GpuUiUpload(pCmd, pPrims, nPrims))
    return;
  SDL_GPUColorTargetInfo target;
  SDL_zero(target);
  target.texture = pSwap;
  target.load_op = SDL_GPU_LOADOP_LOAD;
  target.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(pCmd, &target, 1, NULL);
  SDL_BindGPUGraphicsPipeline(pPass, pPipeline);
  SDL_BindGPUVertexStorageBuffers(pPass, 0, &g_uiPrims, 1);
  // the planet's texture is the front end's, made when it first draws; the overlay draws
  // none, and the atlas stands in for it until then
  SDL_GPUTextureSamplerBinding bindings[3];
  bindings[0].texture = g_uiAtlas;
  bindings[0].sampler = g_linear;
  bindings[1].texture = g_uiPlanet != NULL ? g_uiPlanet : g_uiAtlas;
  bindings[1].sampler = g_linear;
  bindings[2].texture = g_uiPictures;
  bindings[2].sampler = g_linear;
  SDL_BindGPUFragmentSamplers(pPass, 0, bindings, 3);
  UiTargetParams tp;
  tp.fWidth = static_cast<float>(nWidth);
  tp.fHeight = static_cast<float>(nHeight);
  tp.fUnused[0] = tp.fUnused[1] = 0.0f;
  SDL_PushGPUVertexUniformData(pCmd, 0, &tp, sizeof tp);
  SDL_PushGPUFragmentUniformData(pCmd, 0, &tp, sizeof tp);
  SDL_DrawGPUPrimitives(pPass, static_cast<Uint32>(nPrims) * 6u, 1, 0, 0);
  SDL_EndGPURenderPass(pPass);
}
