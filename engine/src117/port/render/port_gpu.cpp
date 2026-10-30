// port_gpu.cpp -- the GPU presenter: the SDL_GPU device, the present, frames ahead, vsync and
// the check's compare (docs/modules/render.md, "One frame through the GPU presenter";
// docs/renderer.md, R4a). Presentation only.
//
// The game still draws its 8-bit screen on the CPU (port_present.cpp). Each frame the screen,
// and the palette when it changed, go up; a first pass resolves the indices into colour at the
// game's size (PSResolve, or PSCompose where the cover says, shaders/present.hlsl) and a second
// draws that into the window's letterbox with SDL's renderer's pixel-art filter (PSScale). The
// passes keep the check apart from the scaling: with HYPERION_GPU_CHECK set, the first pass's
// texture is read back every frame and compared with the screen through the palette on the
// CPU, the counts in hyperion_render.log. When the swapchain has no image free, the frame is
// not shown and the caller keeps it for the next. This file holds the device, the presenter and
// the state; the parts are their own files, sharing port_gpu_internal.h: the effects
// (port_gpu_fx.cpp), HD (port_gpu_hd.cpp), the scene and full colour (port_gpu_scene.cpp), the
// front end (port_gpu_ui.cpp) and a cinematic in full colour (port_gpu_video.cpp).

#include <windows.h>
#include <stdio.h>
#include "port_gpu.h"
#include "../platform/port_log.h"  // PortLogger
#include "port_scene.h"
#include <array>
#include <unordered_map>
#include <vector>
#include "../platform/port_paths.h"  // PortLogFile
#include "../platform/port_posix.h"  // PortNativePath
#include "port_present.h"  // PortGpuZoomCan
#include "port_view.h"  // PortViewCamera
#include "../tools/port_control.h"
#include "port_hd.h"
#include "port_lights.h"
#include "port_motion.h"
#include "port_overlay.h"  // PortOverlayGameArea
#include "../platform/port_settings.h"
#include "../front/port_ui.h"
#include "../tools/port_startup.h"  // PortStartupStep
#include "../tools/port_sysinfo.h"  // PortSysInfoSet
#include "../front/port_world.h"
#include "VSMain.dxil.h"
#include "VSMain.spv.h"
#include "PSResolve.dxil.h"
#include "PSResolve.spv.h"
#include "PSCompose.dxil.h"
#include "PSCompose.spv.h"
#include "PSScale.dxil.h"
#include "PSScale.spv.h"
#include "PSComposeFull.dxil.h"
#include "PSComposeFull.spv.h"
#include "PSGround.dxil.h"
#include "PSGround.spv.h"
#if defined(__APPLE__)
#include "VSMain.msl.h"
#include "PSResolve.msl.h"
#include "PSCompose.msl.h"
#include "PSScale.msl.h"
#include "PSComposeFull.msl.h"
#include "PSGround.msl.h"
#endif
#include "port_gpu_internal.h"

// The state the other parts share (port_gpu_internal.h).
SDL_GPUDevice *g_device;
SDL_Window *g_window;
bool g_claimed;
SDL_GPUTextureFormat g_swapFormat;
SDL_GPUGraphicsPipeline *g_composeFullPipeline;
SDL_GPUSampler *g_nearest;
SDL_GPUSampler *g_linear;
SDL_GPUTexture *g_frame;
SDL_GPUBuffer *g_cover;
int g_nWidth;
int g_nHeight;
bool g_bGroundOk;
int g_nGroundW;
int g_nGroundRows;
bool g_bGroundFull;

// The present's pipelines.
static SDL_GPUGraphicsPipeline *g_resolvePipeline;
static SDL_GPUGraphicsPipeline *g_composePipeline;
static SDL_GPUGraphicsPipeline *g_scalePipeline;
static SDL_GPUGraphicsPipeline *g_groundPipeline;
static SDL_GPUTexture *g_ground;  // the map layer when it moves (PSGround), at its own size
static int g_nGroundTexW, g_nGroundTexH;
static bool g_bHdGround;  // g_hdGround is this frame's, which GpuMapMoved places as the frame
static SDL_GPUTexture *g_palette;  // 256x1, RGBA8
static SDL_GPUTransferBuffer *g_paletteUp;
static bool g_paletteUploaded;
// Sized to the screen with g_frame and g_cover.
static SDL_GPUTexture *g_index;  // the screen's indices, R8
static SDL_GPUTransferBuffer *g_indexUp;
static SDL_GPUTransferBuffer *g_frameDown;
static bool g_bCoverStale = true;  // g_cover is not the present's cover (PortGpuCoverChanged)
static SDL_GPUTransferBuffer *g_coverUp;
// A snapshot of the next frame the live channel asked for (PortGpuSnapAsk), and how it went.
static char g_szSnap[MAX_PATH];
static int g_nSnapState;  // 0 none, 1 asked, 2 taken
static bool g_bSnapOk;
static int g_nSnapW, g_nSnapH;
// The check's counts.
static ULONG g_nChecked;
static ULONG g_nMismatched;
static unsigned long long g_nBadPixels;
static ULONG g_nComposed;  // frames checked that took the scene's pixels,
static unsigned long long g_nCovered;  // and how many they took
static char g_szFirstMismatch[160];

// present.hlsl's ScaleParams.
struct ScaleParams {
  float fDstX, fDstY, fDstW, fDstH;
  float fTexW, fTexH, fHasCover, fCoverScale;
  float fMoved, fHudShift, fHudAlpha, fHasGround;  // the map moved: GpuMapMoved
  float afMapAt[4], afFrameAt[4];
  float fHasHud, fArtSoft, afUnused[2];
};

// present.hlsl's GroundParams: the map layer PSGround resolves, at a skirmish's start.
struct GroundParams {
  Uint32 nWidth, nRows, nFull, nUnused;
};

// Why the last PortGpuInit did not start, with SDL's error.
static char g_szWhy[512];

static bool GpuFailed(const char *pszWhat)
{
  SDL_snprintf(g_szWhy, sizeof g_szWhy, "%s: %s", pszWhat, SDL_GetError());
  return false;
}

const char *PortGpuWhy(void) { return g_szWhy; }

void GpuWriteLog(void)
{
  // the start-up log and a crash report name the device and its driver
  if (g_device != NULL) {
    const SDL_PropertiesID props = SDL_GetGPUDeviceProperties(g_device);
    const char *pszName = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, NULL);
    const char *pszDriverName = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, NULL);
    const char *pszVersion = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, NULL);
    PortSysInfoSet("presenter", "renderer", "the GPU presenter on %s", PortGpuDriver());
    PortSysInfoSet("presenter", "device", "%s, driver %s %s", pszName ? pszName : "?",
                   pszDriverName ? pszDriverName : "?", pszVersion ? pszVersion : "");
    if (PortStartupPending())
      PortStartupStep("presenter", "the GPU presenter on %s, %s, driver %s %s", PortGpuDriver(), pszName ? pszName : "?",
                      pszDriverName ? pszDriverName : "?", pszVersion ? pszVersion : "");
  }
  FILE *f = fopen(PortLogFile("hyperion_render.log"), "w");
  if (f == NULL)
    return;
  fprintf(f, "renderer=%s\n", PortGpuDriver());
  // the device and its driver, as the driver names them (a software one shows here)
  if (g_device != NULL) {
    const SDL_PropertiesID props = SDL_GetGPUDeviceProperties(g_device);
    const char *pszName = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, NULL);
    const char *pszDriverName = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, NULL);
    const char *pszVersion = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, NULL);
    const char *pszInfo = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING, NULL);
    fprintf(f, "device=%s\ndriver=%s %s%s%s\n", pszName ? pszName : "?", pszDriverName ? pszDriverName : "?",
            pszVersion ? pszVersion : "", pszInfo ? " " : "", pszInfo ? pszInfo : "");
  }
  if (g_nChecked != 0 || g_nMismatched != 0)
    fprintf(f, "check frames=%lu mismatched=%lu pixels=%llu%s%s\n", g_nChecked, g_nMismatched,
            g_nBadPixels, g_szFirstMismatch[0] ? " first=" : "", g_szFirstMismatch);
  if (g_nComposed != 0)
    fprintf(f, "check composed=%lu covered=%llu\n", g_nComposed, g_nCovered);
  GpuSceneWriteLog(f);
  fclose(f);
}

char *GpuMslSource(const Uint8 *pMsl, Uint32 nSamplers, Uint32 nReadBuffers, Uint32 nUniforms)
{
  char *psz = NULL;
  if (SDL_asprintf(&psz,
                   "#define HX_SAMPLERS %u\n#define HX_STORAGE_TEXTURES 0\n#define HX_RO_TEXTURES 0\n"
                   "#define HX_RW_TEXTURES 0\n#define HX_RO_BUFFERS %u\n#define HX_UNIFORMS %u\n%s",
                   (unsigned)nSamplers, (unsigned)nReadBuffers, (unsigned)nUniforms, (const char *)pMsl) < 0)
    return NULL;
  return psz;
}

SDL_GPUShader *GpuShader(const Uint8 *pDxil, size_t nDxil, const Uint8 *pSpv, size_t nSpv,
                         const Uint8 *pMsl, const char *pszEntry, SDL_GPUShaderStage stage,
                         Uint32 nSamplers, Uint32 nStorageBuffers, Uint32 nUniforms)
{
  SDL_GPUShaderCreateInfo info;
  SDL_zero(info);
  char *pszMsl = NULL;
  const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(g_device);
  if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
    info.code = pDxil;
    info.code_size = nDxil;
    info.format = SDL_GPU_SHADERFORMAT_DXIL;
  } else if (pMsl != NULL && (formats & SDL_GPU_SHADERFORMAT_MSL) &&
             (pszMsl = GpuMslSource(pMsl, nSamplers, 0, nUniforms)) != NULL) {
    info.code = (const Uint8 *)pszMsl;
    info.code_size = SDL_strlen(pszMsl);
    info.format = SDL_GPU_SHADERFORMAT_MSL;
  } else {
    info.code = pSpv;
    info.code_size = nSpv;
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
  }
  info.entrypoint = pszEntry;
  info.stage = stage;
  info.num_samplers = nSamplers;
  info.num_storage_buffers = nStorageBuffers;
  info.num_uniform_buffers = nUniforms;
  SDL_GPUShader *pShader = SDL_CreateGPUShader(g_device, &info);
  SDL_free(pszMsl);
  return pShader;
}

// bOver blends a premultiplied colour over the target.
SDL_GPUGraphicsPipeline *GpuPipeline(SDL_GPUShader *pVertex, SDL_GPUShader *pFragment,
                                     SDL_GPUTextureFormat format, bool bOver)
{
  SDL_GPUColorTargetDescription target;
  SDL_GPUGraphicsPipelineCreateInfo info;
  SDL_zero(target);
  SDL_zero(info);
  target.format = format;
  if (bOver) {
    target.blend_state.enable_blend = true;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  }
  info.vertex_shader = pVertex;
  info.fragment_shader = pFragment;
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  info.target_info.color_target_descriptions = &target;
  info.target_info.num_color_targets = 1;
  return SDL_CreateGPUGraphicsPipeline(g_device, &info);
}

static SDL_GPUSampler *GpuSampler(SDL_GPUFilter filter)
{
  SDL_GPUSamplerCreateInfo info;
  SDL_zero(info);
  info.min_filter = filter;
  info.mag_filter = filter;
  info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  return SDL_CreateGPUSampler(g_device, &info);
}

SDL_GPUTexture *GpuTexture(SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, int nWidth,
                           int nHeight)
{
  SDL_GPUTextureCreateInfo info;
  SDL_zero(info);
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = format;
  info.usage = usage;
  info.width = static_cast<Uint32>(nWidth);
  info.height = static_cast<Uint32>(nHeight);
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  return SDL_CreateGPUTexture(g_device, &info);
}

SDL_GPUTransferBuffer *GpuTransfer(SDL_GPUTransferBufferUsage usage, Uint32 nSize)
{
  SDL_GPUTransferBufferCreateInfo info;
  SDL_zero(info);
  info.usage = usage;
  info.size = nSize;
  return SDL_CreateGPUTransferBuffer(g_device, &info);
}

// A drawn frame submits one command buffer (GpuFrameCmd), so g_nAhead is the frames the
// processor may run ahead of the card (Settings, "Frames ahead", 2 unless set). Without the
// wait a frame that asks for no swapchain image is never held back: on a Jetson Orin with a
// replay zoomed out to the whole map, each pass cycled the 8192-line map layer into a new
// texture, about 2 GB a second, until the driver's allocation failed and SDL crashed tidying
// up after it.
enum { GPU_AHEAD_MOST = 3 };
static SDL_GPUFence *g_apAhead[GPU_AHEAD_MOST];
static int g_nAhead = 2;
static int g_nAheadAt;

// The time the processor waited here for the card since the frame log last asked: a frame that
// waits is the card's, not the processor's (port_framelog.cpp).
static Uint64 g_nGpuWaitTicks;

double PortGpuWaitMs(void)
{
  const double dMs = 1000.0 * static_cast<double>(g_nGpuWaitTicks) / static_cast<double>(SDL_GetPerformanceFrequency());
  g_nGpuWaitTicks = 0;
  return dMs;
}

void GpuSubmit(SDL_GPUCommandBuffer *pCmd)
{
  SDL_GPUFence *pFence = SDL_SubmitGPUCommandBufferAndAcquireFence(pCmd);
  if (pFence == NULL)
    return;
  SDL_GPUFence *&pOldest = g_apAhead[g_nAheadAt];
  if (pOldest != NULL) {
    const Uint64 nFrom = SDL_GetPerformanceCounter();
    SDL_WaitForGPUFences(g_device, true, &pOldest, 1);
    g_nGpuWaitTicks += SDL_GetPerformanceCounter() - nFrom;
    SDL_ReleaseGPUFence(g_device, pOldest);
  }
  pOldest = pFence;
  g_nAheadAt = (g_nAheadAt + 1) % g_nAhead;
}

// The frame's one command buffer: the HD art's uploads, the scene and the present
// record into it, and it is submitted once, when the frame ends (PortGpuFrameEnd), since a
// submission costs the driver much: two or three a frame took a quarter of the game thread.
static SDL_GPUCommandBuffer *g_frameCmd;

SDL_GPUCommandBuffer *GpuFrameCmd(void)
{
  if (g_frameCmd == NULL)
    g_frameCmd = SDL_AcquireGPUCommandBuffer(g_device);
  return g_frameCmd;
}

SDL_GPUCommandBuffer *GpuFrameCmdTake(void)
{
  SDL_GPUCommandBuffer *pCmd = GpuFrameCmd();
  g_frameCmd = NULL;
  return pCmd;
}

void PortGpuFrameEnd(void)
{
  if (g_frameCmd != NULL)
    GpuSubmit(GpuFrameCmdTake());
}

static void GpuAheadRelease(void)
{
  for (int i = 0; i < GPU_AHEAD_MOST; ++i) {
    if (g_apAhead[i] != NULL)
      SDL_ReleaseGPUFence(g_device, g_apAhead[i]);
    g_apAhead[i] = NULL;
  }
  g_nAheadAt = 0;
}

static void GpuReleaseSized(void)
{
  if (g_index != NULL)
    SDL_ReleaseGPUTexture(g_device, g_index);
  if (g_frame != NULL)
    SDL_ReleaseGPUTexture(g_device, g_frame);
  if (g_indexUp != NULL)
    SDL_ReleaseGPUTransferBuffer(g_device, g_indexUp);
  if (g_frameDown != NULL)
    SDL_ReleaseGPUTransferBuffer(g_device, g_frameDown);
  if (g_cover != NULL)
    SDL_ReleaseGPUBuffer(g_device, g_cover);
  if (g_coverUp != NULL)
    SDL_ReleaseGPUTransferBuffer(g_device, g_coverUp);
  GpuFxReleaseSized();
  if (g_ground != NULL)
    SDL_ReleaseGPUTexture(g_device, g_ground);
  g_ground = NULL;
  g_nGroundTexW = 0;
  g_nGroundTexH = 0;
  g_index = NULL;
  g_frame = NULL;
  g_indexUp = NULL;
  g_frameDown = NULL;
  g_cover = NULL;
  g_coverUp = NULL;
  g_nWidth = 0;
  g_nHeight = 0;
}

static bool GpuFitSize(int nWidth, int nHeight)
{
  if (g_index != NULL && g_nWidth == nWidth && g_nHeight == nHeight)
    return true;
  GpuReleaseSized();
  g_index = GpuTexture(SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, nWidth, nHeight);
  g_frame = GpuTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                       SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, nWidth, nHeight);
  g_indexUp = GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, static_cast<Uint32>(nWidth * nHeight));
  g_frameDown = GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, static_cast<Uint32>(nWidth * nHeight * 4));
  {
    SDL_GPUBufferCreateInfo info;
    SDL_zero(info);
    info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    info.size = static_cast<Uint32>((nWidth * nHeight + 3) & ~3);
    g_cover = SDL_CreateGPUBuffer(g_device, &info);
    g_bCoverStale = true;
  }
  g_coverUp = GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, static_cast<Uint32>((nWidth * nHeight + 3) & ~3));
  if (g_index == NULL || g_frame == NULL || g_indexUp == NULL || g_frameDown == NULL || g_cover == NULL ||
      g_coverUp == NULL) {
    GpuReleaseSized();
    return false;
  }
  GpuFxFitSize(nWidth, nHeight);
  g_nWidth = nWidth;
  g_nHeight = nHeight;
  return true;
}

// The game's frame in a target: letterboxed into the area a layer over the game leaves it (a
// benchmark run's overlay, the trainer's Tape, Debug Tools'), all of the target when none is
// round it (port_overlay.h).
void PortGpuGameBox(float fTargetW, float fTargetH, float fLogicalW, float fLogicalH, SDL_FRect *pDst)
{
  float fX, fY, fW, fH;
  PortOverlayGameArea(fTargetW, fTargetH, &fX, &fY, &fW, &fH);
  PortGpuLetterbox(fW, fH, fLogicalW, fLogicalH, pDst);
  pDst->x = SDL_floorf(pDst->x + fX);
  pDst->y = SDL_floorf(pDst->y + fY);
}

void PortGpuLetterbox(float fTargetW, float fTargetH, float fLogicalW, float fLogicalH, SDL_FRect *pDst)
{
  const float fWant = fLogicalW / fLogicalH;
  const float fReal = fTargetW / fTargetH;

  if (SDL_fabsf(fWant - fReal) < 0.0001f) {
    pDst->x = 0.0f;
    pDst->y = 0.0f;
    pDst->w = fTargetW;
    pDst->h = fTargetH;
  } else if (fWant > fReal) {
    pDst->w = fTargetW;
    pDst->h = SDL_floorf(fLogicalH * (fTargetW / fLogicalW));
    pDst->x = 0.0f;
    pDst->y = (fTargetH - pDst->h) / 2.0f;
  } else {
    pDst->h = fTargetH;
    pDst->w = SDL_floorf(fLogicalW * (fTargetH / fLogicalH));
    pDst->y = 0.0f;
    pDst->x = (fTargetW - pDst->w) / 2.0f;
  }
}

// With HYPERION_GPU_DEBUG=1, what SDL logs, the debug layer's messages among it, goes
// to hyperion_gpu_debug.log.
static void SDLCALL GpuDebugLog(void *, int nCategory, SDL_LogPriority priority, const char *pszMessage)
{
  static const PortLogger s_log("hyperion_gpu_debug.log", PortLogger::SHARED);
  s_log("%d %d %s", nCategory, static_cast<int>(priority), pszMessage);
}

// Settings, "Vertical sync" ("vsync", on unless set off): each frame waits for the display's
// refresh, so it never tears; off, it is shown at once (immediate, or mailbox where the
// driver has no immediate mode). A frame the swapchain has no image for is shown with the
// next (PortGpuFrame), so waiting never holds the game's loop.
static bool g_bVsync = true;  // the present mode in use waits for the refresh

static void GpuPresentMode(bool bVsync)
{
  if (g_device == NULL || !g_claimed)
    return;
  const SDL_GPUPresentMode modes[] = {SDL_GPU_PRESENTMODE_IMMEDIATE, SDL_GPU_PRESENTMODE_MAILBOX};
  if (!bVsync)
    for (SDL_GPUPresentMode mode : modes)
      if (SDL_WindowSupportsGPUPresentMode(g_device, g_window, mode)) {
        SDL_SetGPUSwapchainParameters(g_device, g_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
        g_bVsync = false;
        return;
      }
  SDL_SetGPUSwapchainParameters(g_device, g_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);
  g_bVsync = true;
}

void PortGpuCoverChanged(void)
{
  g_bCoverStale = true;
}

// A change to the swapchain waits while the menus' frame holds one of its images: Settings'
// Accept runs inside that frame, and changing the swapchain there took the image away before
// the frame was shown. The next frame makes the change before it takes an image.
static int g_nVsyncWaiting = -1;  // the present mode asked for, 1 vsync, 0 not; -1 none
static int g_nAheadWaiting;       // the frames ahead asked for; 0 none

// Settings, "Classic art filtering" (PortGpuArtFilterSet): how far past one window pixel the
// scale's filter blends the 640 x 480 frame's texels, 0 the pixel-art filter to 1 a whole
// texel. The HD frame is always drawn with the pixel-art filter.
static float g_fArtSoft;

void PortGpuVsyncSet(bool bVsync)
{
  if (GpuUiFrameOpen()) {
    g_nVsyncWaiting = bVsync ? 1 : 0;
    return;
  }
  GpuPresentMode(bVsync);
}

void PortGpuArtFilterSet(int nStep)
{
  g_fArtSoft = static_cast<float>(SDL_clamp(nStep, 0, 3)) / 3.0f;
}

bool PortGpuVsyncOn(void)
{
  return g_device != NULL && g_claimed && g_bVsync;
}

// Settings, "Frames ahead" ("framesAhead", 2 unless set; HYPERION_FRAMES_AHEAD over it): the
// frames the processor may prepare before the card has drawn them, both the ring above and
// the swapchain's frames in flight. 1 shows a frame soonest after it is made; 3 keeps the
// card busiest. A change waits for the card once.
void PortGpuAheadSet(int nAhead)
{
  nAhead = nAhead < 1 ? 1 : (nAhead > GPU_AHEAD_MOST ? GPU_AHEAD_MOST : nAhead);
  if (g_device == NULL) {
    g_nAhead = nAhead;
    return;
  }
  if (GpuUiFrameOpen()) {
    g_nAheadWaiting = nAhead;
    return;
  }
  PortGpuFrameEnd();
  for (int i = 0; i < GPU_AHEAD_MOST; ++i)
    if (g_apAhead[i] != NULL)
      SDL_WaitForGPUFences(g_device, true, &g_apAhead[i], 1);
  GpuAheadRelease();
  g_nAhead = nAhead;
  if (g_claimed)
    SDL_SetGPUAllowedFramesInFlight(g_device, static_cast<Uint32>(nAhead));
}

void GpuSwapchainWaiting(void)
{
  if (g_nVsyncWaiting >= 0) {
    const bool bVsync = g_nVsyncWaiting != 0;
    g_nVsyncWaiting = -1;
    GpuPresentMode(bVsync);
  }
  if (g_nAheadWaiting > 0) {
    const int nAhead = g_nAheadWaiting;
    g_nAheadWaiting = 0;
    PortGpuAheadSet(nAhead);
  }
}

static int GpuAheadWanted(void)
{
  if (const char *psz = SDL_getenv("HYPERION_FRAMES_AHEAD"))
    if (SDL_atoi(psz) >= 1)
      return SDL_atoi(psz);
  double d = 2.0;
  PortSettingNumber("hyperion", "framesAhead", &d);
  return static_cast<int>(d);
}

bool PortGpuInit(SDL_Window *pWindow, const char *pszDriver, bool bPresent)
{
  const char *pszDebug = SDL_getenv("HYPERION_GPU_DEBUG");
  SDL_GPUShader *pVertex;
  SDL_GPUShader *pResolve;
  SDL_GPUShader *pCompose;
  SDL_GPUShader *pScale;

  // the art Settings keeps (Art; the hotkey), shown from the first frame without a blend, while
  // there is HD art to draw (held for now, port_gpu_hd.cpp's GpuHdAllowed)
  double dArt = 0.0;
  g_fHdOn = PortSettingNumber("hyperion", "art", &dArt) && dArt != 0.0 && PortHdAvailable() ? 1.0f : 0.0f;
  double dLit = 0.0;
  g_fHdLighting = PortSettingNumber("hyperion", "hdLighting", &dLit) && dLit != 0.0 ? 1.0f : 0.0f;
  g_fHdMix = g_fHdOn;
  if (pszDebug != NULL && pszDebug[0] == '1') {
    DeleteFileA(PortLogFile("hyperion_gpu_debug.log"));
    SDL_SetLogOutputFunction(GpuDebugLog, NULL);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_GPU, SDL_LOG_PRIORITY_VERBOSE);
  }
  g_szWhy[0] = 0;
  g_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV |
                                     SDL_GPU_SHADERFORMAT_MSL,
                                 pszDebug != NULL && pszDebug[0] == '1', pszDriver);
  if (g_device == NULL)
    return GpuFailed("no device");
  g_window = pWindow;
  g_swapFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  if (bPresent) {
    if (!SDL_ClaimWindowForGPUDevice(g_device, pWindow)) {
      GpuFailed("the window's swapchain");
      PortGpuShutdown();
      return false;
    }
    g_claimed = true;
    double dVsync = 1.0;
    GpuPresentMode(!PortSettingNumber("hyperion", "vsync", &dVsync) || dVsync != 0.0);
    g_swapFormat = SDL_GetGPUSwapchainTextureFormat(g_device, pWindow);
  }
  PortGpuAheadSet(GpuAheadWanted());

  pVertex = GpuShader(GPU_CODE(VSMain), "VSMain", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 0);
  pResolve = GpuShader(GPU_CODE(PSResolve), "PSResolve", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 0, 0);
  pCompose = GpuShader(GPU_CODE(PSCompose), "PSCompose", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 2, 0);
  pScale = GpuShader(GPU_CODE(PSScale), "PSScale", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 2, 1);
  if (pVertex != NULL && pResolve != NULL && pCompose != NULL && pScale != NULL) {
    g_resolvePipeline = GpuPipeline(pVertex, pResolve, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    g_composePipeline = GpuPipeline(pVertex, pCompose, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    g_scalePipeline = GpuPipeline(pVertex, pScale, g_swapFormat);
  }
  const bool bScene = GpuSceneInit();
  if (pVertex != NULL) {
    SDL_GPUShader *pComposeFull = GpuShader(GPU_CODE(PSComposeFull), "PSComposeFull",
                                            SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 3, 0);
    if (pComposeFull != NULL) {
      g_composeFullPipeline = GpuPipeline(pVertex, pComposeFull, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
      SDL_ReleaseGPUShader(g_device, pComposeFull);
    }
    // a skirmish's start: the map layer under the console (port_intro.cpp); without it the
    // map there is the mirror
    SDL_GPUShader *pGround = GpuShader(GPU_CODE(PSGround), "PSGround", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 2, 1);
    if (pGround != NULL) {
      g_groundPipeline = GpuPipeline(pVertex, pGround, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
      SDL_ReleaseGPUShader(g_device, pGround);
    }
  }
  GpuFxInit(pVertex);
  GpuVideoInit(pVertex);
  GpuHdInit();
  if (pVertex != NULL)
    SDL_ReleaseGPUShader(g_device, pVertex);
  if (pResolve != NULL)
    SDL_ReleaseGPUShader(g_device, pResolve);
  if (pCompose != NULL)
    SDL_ReleaseGPUShader(g_device, pCompose);
  if (pScale != NULL)
    SDL_ReleaseGPUShader(g_device, pScale);
  g_nearest = GpuSampler(SDL_GPU_FILTER_NEAREST);
  g_linear = GpuSampler(SDL_GPU_FILTER_LINEAR);
  g_palette = GpuTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, 256, 1);
  g_paletteUp = GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, 256 * 4);
  if (g_resolvePipeline == NULL || g_composePipeline == NULL || g_scalePipeline == NULL || !bScene ||
      g_nearest == NULL || g_linear == NULL || g_palette == NULL || g_paletteUp == NULL) {
    GpuFailed(!bScene ? "the scene's pipelines" : "the present's shaders, pipelines or textures");
    PortGpuShutdown();
    return false;
  }
  g_paletteUploaded = false;
  GpuWriteLog();
  return true;
}


void PortGpuShutdown(void)
{
  if (g_device == NULL)
    return;
  PortGpuFrameEnd();
  SDL_WaitForGPUIdle(g_device);
  GpuAheadRelease();
  GpuReleaseSized();
  GpuUiRelease();
  GpuHudRelease();
  GpuSceneRelease();
  if (g_composeFullPipeline != NULL)
    SDL_ReleaseGPUGraphicsPipeline(g_device, g_composeFullPipeline);
  if (g_groundPipeline != NULL)
    SDL_ReleaseGPUGraphicsPipeline(g_device, g_groundPipeline);
  g_composeFullPipeline = NULL;
  g_groundPipeline = NULL;
  g_bGroundOk = false;
  GpuFxRelease();
  GpuVideoRelease();
  GpuHdRelease();
  if (g_resolvePipeline != NULL)
    SDL_ReleaseGPUGraphicsPipeline(g_device, g_resolvePipeline);
  if (g_composePipeline != NULL)
    SDL_ReleaseGPUGraphicsPipeline(g_device, g_composePipeline);
  if (g_scalePipeline != NULL)
    SDL_ReleaseGPUGraphicsPipeline(g_device, g_scalePipeline);
  if (g_nearest != NULL)
    SDL_ReleaseGPUSampler(g_device, g_nearest);
  if (g_linear != NULL)
    SDL_ReleaseGPUSampler(g_device, g_linear);
  if (g_palette != NULL)
    SDL_ReleaseGPUTexture(g_device, g_palette);
  if (g_paletteUp != NULL)
    SDL_ReleaseGPUTransferBuffer(g_device, g_paletteUp);
  if (g_claimed)
    SDL_ReleaseWindowFromGPUDevice(g_device, g_window);
  SDL_DestroyGPUDevice(g_device);
  g_device = NULL;
  g_window = NULL;
  g_claimed = false;
  g_resolvePipeline = NULL;
  g_composePipeline = NULL;
  g_scalePipeline = NULL;
  g_nearest = NULL;
  g_linear = NULL;
  g_palette = NULL;
  g_paletteUp = NULL;
}

void PortGpuCheckCounts(ULONG *pnChecked, ULONG *pnMismatched)
{
  *pnChecked = g_nChecked;
  *pnMismatched = g_nMismatched;
}

const char *PortGpuDriver(void)
{
  return g_device != NULL ? SDL_GetGPUDeviceDriver(g_device) : "none";
}

// RGBA pixels (rows nWidth apart) saved as a PNG, or a BMP when pszPath does not end
// in .png, without their alpha.
bool GpuSavePixels(const void *pPixels, int nWidth, int nHeight, const char *pszPath)
{
  SDL_Surface *pRgba = SDL_CreateSurfaceFrom(nWidth, nHeight, SDL_PIXELFORMAT_ABGR8888, const_cast<void *>(pPixels),
                                             nWidth * 4);
  if (pRgba == NULL)
    return false;
  SDL_Surface *pRgb = SDL_ConvertSurface(pRgba, SDL_PIXELFORMAT_RGB24);
  SDL_DestroySurface(pRgba);
  if (pRgb == NULL)
    return false;
  const size_t n = SDL_strlen(pszPath);
  const bool bPng = n >= 4 && SDL_strcasecmp(pszPath + n - 4, ".png") == 0;
  const bool bSaved = bPng ? SDL_SavePNG(pRgb, PortNativePath(pszPath)) : SDL_SaveBMP(pRgb, PortNativePath(pszPath));
  SDL_DestroySurface(pRgb);
  return bSaved;
}

void PortGpuSnapAsk(const char *pszPath)
{
  SDL_strlcpy(g_szSnap, pszPath, sizeof g_szSnap);
  g_nSnapState = 1;
}

bool PortGpuSnapPending(void)
{
  return g_nSnapState == 1;
}

bool PortGpuSnapTake(bool *pbOk, int *pnWidth, int *pnHeight)
{
  if (g_nSnapState != 2)
    return false;
  g_nSnapState = 0;
  *pbOk = g_bSnapOk;
  *pnWidth = g_nSnapW;
  *pnHeight = g_nSnapH;
  return true;
}

// The resolved frame against the screen through the palette; not the pixels pSkip
// marks, when it is given.
static void GpuCompare(const Uint8 *pScreen, const SDL_Color *pColors, const Uint8 *pSkip)
{
  const Uint8 *pGot = static_cast<const Uint8 *>(SDL_MapGPUTransferBuffer(g_device, g_frameDown, false));
  ULONG nBad = 0;
  int i;

  if (pGot == NULL)
    return;
  for (i = 0; i < g_nWidth * g_nHeight; ++i) {
    if (pSkip != NULL && pSkip[i] != 0)
      continue;
    const SDL_Color &c = pColors[pScreen[i]];
    if (pGot[i * 4] != c.r || pGot[i * 4 + 1] != c.g || pGot[i * 4 + 2] != c.b) {
      if (nBad == 0 && g_nMismatched == 0)
        SDL_snprintf(g_szFirstMismatch, sizeof g_szFirstMismatch,
                     "frame %lu at %d,%d: index %u is %02X%02X%02X, the GPU gave %02X%02X%02X", g_nChecked,
                     i % g_nWidth, i / g_nWidth, pScreen[i], c.r, c.g, c.b, pGot[i * 4], pGot[i * 4 + 1],
                     pGot[i * 4 + 2]);
      ++nBad;
    }
  }
  SDL_UnmapGPUTransferBuffer(g_device, g_frameDown);
  ++g_nChecked;
  if (nBad != 0) {
    ++g_nMismatched;
    g_nBadPixels += nBad;
  }
  // Rewritten as it goes: a replay run ends the process without a shutdown.
  if (g_nChecked <= 16 || (g_nChecked & 255) == 0 || (nBad != 0 && g_nMismatched <= 16))
    GpuWriteLog();
}

int PortGpuZoomCan(void)
{
  return g_hudPipeline != NULL && g_groundPipeline != NULL ? 1 : 0;
}

// The window's pixels to a pixel of the game's frame, as the last frame was shown
// (GpuFrameWindow); 1 before that, and in a hidden window.
static float g_fWindowScale = 1.0f;

int PortGpuSceneStep(int nWidth, int nRows, double fZoom)
{
  static int s_nFixed = -1;
  if (s_nFixed < 0) {
    const char *psz = SDL_getenv("HYPERION_SCENE_STEP");
    s_nFixed = psz != NULL && SDL_atoi(psz) > 0 ? SDL_atoi(psz) : 0;
  }
  int nStep = 1;
  if (s_nFixed > 0) {
    while (nStep * 2 <= s_nFixed && nStep < 8)
      nStep *= 2;
  } else {
    // the layer's pixels to one of the window's at the camera, kept at 2 or more
    const double fPer = fZoom / (g_fWindowScale > 0.0f ? g_fWindowScale : 1.0f);
    while (nStep < 8 && fPer >= 4.0 * nStep)
      nStep *= 2;
  }
  // and the layer no larger than 8,192 pixels a side, which a texture holds (PSGround)
  while (nStep < 16 && ((nWidth + nStep - 1) / nStep > 8192 || (nRows + nStep - 1) / nStep > 8192))
    nStep *= 2;
  return nStep;
}

void GpuMapMoved(int nWidth, int nHeight, GpuMapMove *pMove)
{
  PortMotionNow motion;
  PortMotionRead(&motion);
  pMove->fHudShift = motion.fHudShift;
  pMove->fHudAlpha = motion.fHudAlpha;
  pMove->bZoom = g_zoomShown.bOn && g_zoomShown.nMapW > 0 && g_zoomShown.nMapH > 0;
  if (pMove->bZoom) {
    // the screen's pixel x, y shows the map's at the camera's top left plus z times it, the
    // camera as it is now, over the last pass's render view; the layer is the render view's
    double fCamX, fCamY, z;
    PortViewCamera(&fCamX, &fCamY, &z);
    const double w = nWidth;
    const double h = nHeight;
    const float afMap[4] = {static_cast<float>((fCamX - g_zoomShown.nMapX) / g_zoomShown.nMapW),
                            static_cast<float>((fCamY - g_zoomShown.nMapY) / g_zoomShown.nMapH),
                            static_cast<float>(w * z / g_zoomShown.nMapW),
                            static_cast<float>(h * z / g_zoomShown.nMapH)};
    // the lights are drawn through the camera already (GpuLights)
    const float afLight[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    for (int i = 0; i < 4; ++i) {
      pMove->afMapAt[i] = afMap[i];
      pMove->afFrameAt[i] = i < 2 ? 0.0f : 1.0f;
      pMove->afLightAt[i] = afLight[i];
    }
    pMove->bMoved = true;
  } else {
    // the picture's motion (a skirmish's start, a cutscene): the map, as large as the frame, scaled about its middle
    const float k = 1.0f / motion.fMapScale;
    const float a = 0.5f - 0.5f * k;
    for (int i = 0; i < 4; ++i)
      pMove->afMapAt[i] = pMove->afFrameAt[i] = pMove->afLightAt[i] = i < 2 ? a : k;
    pMove->bMoved = motion.fMapScale != 1.0f;
  }
  // the HD map alone is drawn as the HD frame is, so it is placed as the frame
  if (g_bHdGround) {
    for (int i = 0; i < 4; ++i)
      pMove->afMapAt[i] = pMove->afFrameAt[i];
    pMove->bMoved = true;
  }
}

// One frame's picture as its passes leave it for the window and a snapshot (PortGpuFrame).
struct GpuFrameShown {
  bool bCompose;            // the map's pixels from the scene's copy of the screen, under the cover
  bool bHd;                 // drawn again in HD
  SDL_GPUTexture *pFrame;   // g_frame, or g_hdFrame in HD
  int nW, nH;               // its size
  float fCoverScale;        // its pixels to a pixel of the cover: the HD art's scale, else 1
  GpuMapMove move;          // how the map moves under the rest (GpuMapMoved)
  bool bGround;             // a map layer under the rest, pGround
  SDL_GPUTexture *pGround;  // the scene's (g_ground), or in HD the map alone (g_hdGround)
  bool bHud;                // where the dialogs drew over the zoomed map (CSHud), as pHud
  SDL_GPUBuffer *pHud;      // that, or the cover in its place
};

// A snapshot the live channel asked for: the texture it is taken from (NULL for the
// resolved frame, which the check's download brings down) and where it comes down to.
struct GpuFrameSnap {
  bool bOn;
  SDL_GPUTexture *pFrom;
  SDL_GPUTransferBuffer *pDown;
};

// The frame's uploads: the screen's indices, the cover when it changed, the palette when it
// did. True when the map is composed from the scene's copy of the screen (CSShow has run at
// this size; until then the cover is all 0s).
static bool GpuFrameUpload(SDL_GPUCommandBuffer *pCmd, const Uint8 *pScreen, const Uint8 *pCover, int nWidth,
                           int nHeight, const SDL_Color *pColors, bool bPaletteChanged)
{
  SDL_GPUTextureTransferInfo src;
  SDL_GPUTextureRegion dst;
  void *pMapped = SDL_MapGPUTransferBuffer(g_device, g_indexUp, true);
  if (pMapped != NULL) {
    SDL_memcpy(pMapped, pScreen, static_cast<size_t>(nWidth) * nHeight);
    SDL_UnmapGPUTransferBuffer(g_device, g_indexUp);
  }
  const bool bCompose = pCover != NULL && g_sceneStore[SCENE_STORE_SHOWN].pBuffer != NULL &&
                        g_sceneStore[SCENE_STORE_SHOWN].nCapacity >= static_cast<Uint32>(nWidth * nHeight);
  // the cover goes up when it changed, and only once its bytes are in the transfer buffer: one
  // that did not map leaves it stale, to go up with the next frame
  bool bCoverUp = false;
  if (bCompose && g_bCoverStale) {
    pMapped = SDL_MapGPUTransferBuffer(g_device, g_coverUp, true);
    if (pMapped != NULL) {
      SDL_memcpy(pMapped, pCover, static_cast<size_t>(nWidth) * nHeight);
      SDL_UnmapGPUTransferBuffer(g_device, g_coverUp);
      bCoverUp = true;
    }
  }
  if (bPaletteChanged || !g_paletteUploaded) {
    Uint8 *pRgba = static_cast<Uint8 *>(SDL_MapGPUTransferBuffer(g_device, g_paletteUp, true));
    if (pRgba != NULL) {
      for (int i = 0; i < 256; ++i) {
        pRgba[i * 4] = pColors[i].r;
        pRgba[i * 4 + 1] = pColors[i].g;
        pRgba[i * 4 + 2] = pColors[i].b;
        pRgba[i * 4 + 3] = 255;
      }
      SDL_UnmapGPUTransferBuffer(g_device, g_paletteUp);
    }
  }
  SDL_GPUCopyPass *pCopy = SDL_BeginGPUCopyPass(pCmd);
  SDL_zero(src);
  SDL_zero(dst);
  src.transfer_buffer = g_indexUp;
  src.pixels_per_row = static_cast<Uint32>(nWidth);
  src.rows_per_layer = static_cast<Uint32>(nHeight);
  dst.texture = g_index;
  dst.w = static_cast<Uint32>(nWidth);
  dst.h = static_cast<Uint32>(nHeight);
  dst.d = 1;
  SDL_UploadToGPUTexture(pCopy, &src, &dst, true);
  if (bPaletteChanged || !g_paletteUploaded) {
    src.transfer_buffer = g_paletteUp;
    src.pixels_per_row = 256;
    src.rows_per_layer = 1;
    dst.texture = g_palette;
    dst.w = 256;
    dst.h = 1;
    SDL_UploadToGPUTexture(pCopy, &src, &dst, true);
    g_paletteUploaded = true;
  }
  if (bCoverUp) {
    SDL_GPUTransferBufferLocation from;
    SDL_GPUBufferRegion to;
    g_bCoverStale = false;
    from.transfer_buffer = g_coverUp;
    from.offset = 0;
    to.buffer = g_cover;
    to.offset = 0;
    to.size = static_cast<Uint32>((nWidth * nHeight + 3) & ~3);
    SDL_UploadToGPUBuffer(pCopy, &from, &to, true);
  }
  SDL_EndGPUCopyPass(pCopy);
  return bCompose;
}

// The frame at the game's size, resolved through the palette, the map's pixels composed
// from the scene's copy of the screen (in full colour from its full colour copy).
static void GpuFrameResolve(SDL_GPUCommandBuffer *pCmd, int nWidth, int nHeight, bool bCompose)
{
  SDL_GPUColorTargetInfo target;
  SDL_zero(target);
  target.texture = g_frame;
  target.load_op = SDL_GPU_LOADOP_DONT_CARE;
  target.store_op = SDL_GPU_STOREOP_STORE;
  target.cycle = true;
  const bool bComposeFull = bCompose && g_bFullShown && g_composeFullPipeline != NULL &&
                            g_sceneStore[SCENE_STORE_SHOWN_RGB].nCapacity >= static_cast<Uint32>(nWidth * nHeight) * 4;
  SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(pCmd, &target, 1, NULL);
  SDL_BindGPUGraphicsPipeline(pPass, bComposeFull ? g_composeFullPipeline
                                     : bCompose   ? g_composePipeline
                                                  : g_resolvePipeline);
  SDL_GPUTextureSamplerBinding bindings[2] = {{g_index, g_nearest}, {g_palette, g_nearest}};
  SDL_BindGPUFragmentSamplers(pPass, 0, bindings, 2);
  if (bCompose) {
    SDL_GPUBuffer *apStorage[3] = {g_sceneStore[SCENE_STORE_SHOWN].pBuffer, g_cover,
                                   g_sceneStore[SCENE_STORE_SHOWN_RGB].pBuffer};
    SDL_BindGPUFragmentStorageBuffers(pPass, 0, apStorage, bComposeFull ? 3 : 2);
  }
  SDL_DrawGPUPrimitives(pPass, 3, 1, 0, 0);
  SDL_EndGPURenderPass(pPass);
}

// The scene's map layer resolved into g_ground at nTexW by nTexH (PSGround). False when the
// scene's buffers do not hold it or there is no texture for it.
static bool GpuGroundDraw(SDL_GPUCommandBuffer *pCmd, int nTexW, int nTexH)
{
  const Uint32 nPixels = static_cast<Uint32>(g_nGroundW * g_nGroundRows);
  if (g_groundPipeline == NULL || nPixels == 0 || g_sceneStore[SCENE_STORE_MAP].nCapacity < nPixels ||
      (g_bGroundFull && g_sceneStore[SCENE_STORE_MAP_RGB].nCapacity < nPixels * 4))
    return false;
  if (g_ground == NULL || g_nGroundTexW != nTexW || g_nGroundTexH != nTexH) {
    if (g_ground != NULL)
      SDL_ReleaseGPUTexture(g_device, g_ground);
    g_ground = GpuTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                          SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, nTexW, nTexH);
    g_nGroundTexW = g_ground != NULL ? nTexW : 0;
    g_nGroundTexH = g_ground != NULL ? nTexH : 0;
  }
  if (g_ground == NULL)
    return false;
  SDL_GPUColorTargetInfo groundTarget;
  SDL_zero(groundTarget);
  groundTarget.texture = g_ground;
  groundTarget.load_op = SDL_GPU_LOADOP_DONT_CARE;
  groundTarget.store_op = SDL_GPU_STOREOP_STORE;
  groundTarget.cycle = true;
  SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(pCmd, &groundTarget, 1, NULL);
  SDL_BindGPUGraphicsPipeline(pPass, g_groundPipeline);
  SDL_GPUTextureSamplerBinding palette = {g_palette, g_nearest};
  SDL_BindGPUFragmentSamplers(pPass, 0, &palette, 1);
  SDL_GPUBuffer *apGround[2] = {g_sceneStore[SCENE_STORE_MAP].pBuffer, g_sceneStore[SCENE_STORE_MAP_RGB].pBuffer};
  SDL_BindGPUFragmentStorageBuffers(pPass, 0, apGround, 2);
  GroundParams ground;
  ground.nWidth = static_cast<Uint32>(g_nGroundW);
  ground.nRows = static_cast<Uint32>(g_nGroundRows);
  ground.nFull = g_bGroundFull ? 1u : 0u;
  ground.nUnused = 0;
  SDL_PushGPUFragmentUniformData(pCmd, 0, &ground, sizeof ground);
  SDL_DrawGPUPrimitives(pPass, 3, 1, 0, 0);
  SDL_EndGPURenderPass(pPass);
  return true;
}

// The frame in HD, from the resolved frame, which stays as it was for the
// check; what is shown and snapped is then drawn from it. Mid-blend (F5), the whole SD frame
// too, from the scene's first drawing, laid over it as the blend says; while the console
// moves (a skirmish's start, a map load), the HD map alone as well, for where the console
// rises from. True when the frame was drawn in HD.
static bool GpuFrameHd(SDL_GPUCommandBuffer *pCmd, const SDL_Color *pColors, int nWidth, int nHeight, bool bCompose)
{
  const int nHdScale = PortHdScale();
  const bool bHd = bCompose && g_bHdScene && GpuHdCan() && GpuHdFit(nWidth * nHdScale, nHeight * nHdScale);
  SDL_GPUTexture *pSdWhole = NULL;
  if (bHd && g_bHdMixed && GpuHdMixing() && g_hdBlendPipeline != NULL) {
    if (g_hdFrameSd == NULL || g_nHdFrameSdW != nWidth || g_nHdFrameSdH != nHeight) {
      if (g_hdFrameSd != NULL)
        SDL_ReleaseGPUTexture(g_device, g_hdFrameSd);
      g_hdFrameSd = GpuTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                               SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, nWidth, nHeight);
      g_nHdFrameSdW = g_hdFrameSd != NULL ? nWidth : 0;
      g_nHdFrameSdH = g_hdFrameSd != NULL ? nHeight : 0;
    }
    if (g_hdFrameSd != NULL) {
      SDL_GPUColorTargetInfo sdTarget;
      SDL_zero(sdTarget);
      sdTarget.texture = g_hdFrameSd;
      sdTarget.load_op = SDL_GPU_LOADOP_DONT_CARE;
      sdTarget.store_op = SDL_GPU_STOREOP_STORE;
      sdTarget.cycle = true;
      SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(pCmd, &sdTarget, 1, NULL);
      SDL_BindGPUGraphicsPipeline(pPass, g_composePipeline);
      SDL_GPUTextureSamplerBinding bindings[2] = {{g_index, g_nearest}, {g_palette, g_nearest}};
      SDL_BindGPUFragmentSamplers(pPass, 0, bindings, 2);
      SDL_GPUBuffer *apStorage[2] = {g_hdShownSd, g_cover};
      SDL_BindGPUFragmentStorageBuffers(pPass, 0, apStorage, 2);
      SDL_DrawGPUPrimitives(pPass, 3, 1, 0, 0);
      SDL_EndGPURenderPass(pPass);
      pSdWhole = g_hdFrameSd;
    }
  }
  const float fHudShift = PortMotionValue(MOTION_HUD_SHIFT), fHudAlpha = PortMotionValue(MOTION_HUD_ALPHA);
  g_bHdGround = bHd && (fHudShift != 0.0f || fHudAlpha != 1.0f) && GpuHdGroundFit();
  // zoomed out, the scene's map layer, which the HD frame takes the map from
  SDL_GPUTexture *pLayer =
      bHd && g_zoomShown.bOn && GpuGroundDraw(pCmd, g_nGroundW, g_nGroundRows) ? g_ground : NULL;
  if (g_bHdGround)
    GpuHdDrawFrame(pCmd, pColors, NULL, pLayer, true);
  if (bHd)
    GpuHdDrawFrame(pCmd, pColors, pSdWhole, pLayer);
  return bHd;
}

// Where the map moves under the rest: a skirmish's start (port_intro.cpp), with the whole
// map layer the scene drew, which shows where the console moves off the map, the layer the
// frame's top rows as wide as the frame; or the camera zoomed out, the layer the
// render view's, and where the dialogs drew over it (CSHud). Fills *pShown's move, ground
// and dialogs, drawing the scene's layer when it is wanted.
static void GpuFrameGround(SDL_GPUCommandBuffer *pCmd, int nWidth, int nHeight, GpuFrameShown *pShown)
{
  GpuMapMove &move = pShown->move;
  GpuMapMoved(nWidth, nHeight, &move);
  const bool bMoving = move.bMoved || move.fHudShift != 0.0f || move.fHudAlpha != 1.0f;
  const int nGroundTexW = move.bZoom ? g_nGroundW : nWidth;
  const int nGroundTexH = move.bZoom ? g_nGroundRows : nHeight;
  const bool bGround = bMoving && pShown->bCompose && !pShown->bHd && g_bGroundOk &&
                       (move.bZoom || (g_nGroundW == nWidth && g_nGroundRows <= nHeight)) &&
                       GpuGroundDraw(pCmd, nGroundTexW, nGroundTexH);
  // where the dialogs drew over the zoomed map, and without it the cover again in its place
  pShown->bHud = move.bZoom && pShown->bCompose && g_sceneStore[SCENE_STORE_HUD].pBuffer != NULL &&
                 g_sceneStore[SCENE_STORE_HUD].nCapacity >= static_cast<Uint32>(nWidth * nHeight);
  pShown->pHud = pShown->bHud ? g_sceneStore[SCENE_STORE_HUD].pBuffer : g_cover;
  // the map under the rest: the scene's layer, or in HD at a skirmish's start the HD map alone
  pShown->bGround = bGround || g_bHdGround;
  pShown->pGround = g_bHdGround ? g_hdGround : g_ground;
}

// A texture region's download into a transfer buffer, in its own copy pass.
static void GpuFrameDownload(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pFrom, int nW, int nH,
                             SDL_GPUTransferBuffer *pTo)
{
  SDL_GPUTextureRegion from;
  SDL_GPUTextureTransferInfo to;
  SDL_zero(from);
  SDL_zero(to);
  from.texture = pFrom;
  from.w = static_cast<Uint32>(nW);
  from.h = static_cast<Uint32>(nH);
  from.d = 1;
  to.transfer_buffer = pTo;
  to.pixels_per_row = static_cast<Uint32>(nW);
  to.rows_per_layer = static_cast<Uint32>(nH);
  SDL_GPUCopyPass *pCopy = SDL_BeginGPUCopyPass(pCmd);
  SDL_DownloadFromGPUTexture(pCopy, &from, &to);
  SDL_EndGPUCopyPass(pCopy);
}

// The effects, the bloom from the resolved frame, when bFx; a snapshot
// takes the frame with its effects at the frame's size, or the frame itself; the check
// takes the resolved frame.
static void GpuFrameEffects(SDL_GPUCommandBuffer *pCmd, int nWidth, int nHeight, const GpuFrameShown &shown,
                            bool bFx, bool bCheck, GpuFrameSnap *pSnap)
{
  pSnap->bOn = g_nSnapState == 1;
  pSnap->pFrom = NULL;
  pSnap->pDown = NULL;
  if (pSnap->bOn && bFx) {
    pSnap->pFrom = shown.bHd ? g_hdPost : g_post;
    pSnap->pDown = shown.bHd ? g_hdDown : g_postDown;
  } else if (pSnap->bOn && shown.bHd) {
    pSnap->pFrom = g_hdFrame;
    pSnap->pDown = g_hdDown;
  } else if (pSnap->bOn) {
    pSnap->pDown = g_frameDown;  // with the check's download below
  }
  if (bFx) {
    GpuBloom(pCmd, nWidth, nHeight, shown.bCompose);
    GpuLights(pCmd, nWidth, nHeight);
    if (pSnap->bOn) {
      PostParams post;
      const SDL_FRect whole = {0.0f, 0.0f, static_cast<float>(shown.nW), static_cast<float>(shown.nH)};
      GpuPostParams(whole, shown.nW, shown.nH, shown.bCompose, &post);
      post.fCoverScale = shown.fCoverScale;
      post.fHasGround = shown.bGround ? 1.0f : 0.0f;
      post.fHasHud = shown.bHud ? 1.0f : 0.0f;
      GpuFxPass(pCmd, pSnap->pFrom, g_postSnapPipeline, shown.pFrame, g_bloomUp[0], true, &post, sizeof post,
                g_lightTex, shown.bGround ? shown.pGround : shown.pFrame, shown.pHud);
    }
  }
  if (pSnap->pFrom != NULL)
    GpuFrameDownload(pCmd, pSnap->pFrom, shown.nW, shown.nH, pSnap->pDown);
  if (bCheck || (pSnap->bOn && pSnap->pFrom == NULL))
    GpuFrameDownload(pCmd, g_frame, nWidth, nHeight, g_frameDown);
}

// The frame, with its effects when bFx, drawn in box of the pass's target, which is of the
// swapchain's format: the window's, and the screen behind a HUD's glass (port_gpu_hud.cpp).
static void GpuFrameDraw(SDL_GPUCommandBuffer *pCmd, SDL_GPURenderPass *pPass, const SDL_FRect &box,
                         const GpuFrameShown &shown, bool bFx)
{
  SDL_BindGPUGraphicsPipeline(pPass, bFx ? g_postPipeline : g_scalePipeline);
  SDL_GPUViewport viewport;
  viewport.x = box.x;
  viewport.y = box.y;
  viewport.w = box.w;
  viewport.h = box.h;
  viewport.min_depth = 0.0f;
  viewport.max_depth = 1.0f;
  SDL_SetGPUViewport(pPass, &viewport);
  SDL_GPUTexture *pUnder = shown.bGround ? shown.pGround : shown.pFrame;
  SDL_GPUBuffer *apStorage[2] = {g_cover, shown.pHud};
  if (bFx) {
    // the frame with its effects, at the window's resolution
    PostParams post;
    SDL_GPUTextureSamplerBinding fx[4] = {
        {shown.pFrame, g_linear}, {g_bloomUp[0], g_linear}, {g_lightTex, g_linear}, {pUnder, g_linear}};
    SDL_BindGPUFragmentSamplers(pPass, 0, fx, 4);
    SDL_BindGPUFragmentStorageBuffers(pPass, 0, apStorage, 2);
    GpuPostParams(box, shown.nW, shown.nH, shown.bCompose, &post);
    post.fCoverScale = shown.fCoverScale;
    post.fHasGround = shown.bGround ? 1.0f : 0.0f;
    post.fHasHud = shown.bHud ? 1.0f : 0.0f;
    post.fArtSoft = shown.bHd ? 0.0f : g_fArtSoft;
    SDL_PushGPUFragmentUniformData(pCmd, 0, &post, sizeof post);
  } else {
    ScaleParams params;
    SDL_GPUTextureSamplerBinding scale[2] = {{shown.pFrame, g_linear}, {pUnder, g_linear}};
    SDL_BindGPUFragmentSamplers(pPass, 0, scale, 2);
    SDL_BindGPUFragmentStorageBuffers(pPass, 0, apStorage, 2);
    params.fDstX = box.x;
    params.fDstY = box.y;
    params.fDstW = box.w;
    params.fDstH = box.h;
    params.fTexW = static_cast<float>(shown.nW);
    params.fTexH = static_cast<float>(shown.nH);
    params.fHasCover = shown.bCompose ? 1.0f : 0.0f;
    params.fCoverScale = shown.fCoverScale;
    params.fMoved = shown.move.bMoved ? 1.0f : 0.0f;
    params.fHudShift = shown.move.fHudShift;
    params.fHudAlpha = shown.move.fHudAlpha;
    params.fHasGround = shown.bGround ? 1.0f : 0.0f;
    for (int k = 0; k < 4; ++k) {
      params.afMapAt[k] = shown.move.afMapAt[k];
      params.afFrameAt[k] = shown.move.afFrameAt[k];
    }
    params.fHasHud = shown.bHud ? 1.0f : 0.0f;
    params.fArtSoft = shown.bHd ? 0.0f : g_fArtSoft;
    params.afUnused[0] = params.afUnused[1] = 0.0f;
    SDL_PushGPUFragmentUniformData(pCmd, 0, &params, sizeof params);
  }
  SDL_DrawGPUPrimitives(pPass, 3, 1, 0, 0);
}

// The window: black, and the frame in its letterbox, with its effects when bFx, a cinematic in
// full colour over it, then the overlay. True when it was shown.
static bool GpuFrameWindow(SDL_GPUCommandBuffer *pCmd, int nWidth, int nHeight, const GpuFrameShown &shown, bool bFx)
{
  SDL_GPUTexture *pSwap = NULL;
  Uint32 nSwapW = 0;
  Uint32 nSwapH = 0;
  if (!g_claimed || !SDL_AcquireGPUSwapchainTexture(pCmd, g_window, &pSwap, &nSwapW, &nSwapH) || pSwap == NULL)
    return false;
  SDL_FRect box;
  PortGpuGameBox(static_cast<float>(nSwapW), static_cast<float>(nSwapH), static_cast<float>(nWidth),
                 static_cast<float>(nHeight), &box);
  if (nWidth > 0)
    g_fWindowScale = box.w / static_cast<float>(nWidth);
  SDL_GPUColorTargetInfo target;
  SDL_zero(target);
  target.texture = pSwap;
  target.clear_color.a = 1.0f;
  target.load_op = SDL_GPU_LOADOP_CLEAR;
  target.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(pCmd, &target, 1, NULL);
  GpuFrameDraw(pCmd, pPass, box, shown, bFx);
  SDL_EndGPURenderPass(pPass);
  GpuVideoDraw(pCmd, pSwap, nSwapW, nSwapH);
  // the HUD design, when one is on, between the frame and the overlay
  GpuHudDraw(pCmd, pSwap, nSwapW, nSwapH, box, [&](SDL_GPUTexture *pTarget, int w, int h) {
    SDL_GPUColorTargetInfo back;
    SDL_zero(back);
    back.texture = pTarget;
    back.clear_color.a = 1.0f;
    back.load_op = SDL_GPU_LOADOP_CLEAR;
    back.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pBack = SDL_BeginGPURenderPass(pCmd, &back, 1, NULL);
    const SDL_FRect whole = {0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};
    GpuFrameDraw(pCmd, pBack, whole, shown, bFx);
    SDL_EndGPURenderPass(pBack);
  });
  GpuUiOverlay(pCmd, pSwap, nSwapW, nSwapH, box);
  return true;
}

bool PortGpuFrame(const Uint8 *pScreen, const Uint8 *pCover, int nWidth, int nHeight,
                  const SDL_Color *pColors, bool bPaletteChanged, bool bPresent, bool bCheck)
{
  if (g_device == NULL || !GpuFitSize(nWidth, nHeight))
    return false;
  GpuSwapchainWaiting();
  // the frame's buffer, with the scene's work already in it; submitted below
  SDL_GPUCommandBuffer *pCmd = GpuFrameCmdTake();
  if (pCmd == NULL)
    return false;

  GpuFrameShown shown;
  shown.bCompose = GpuFrameUpload(pCmd, pScreen, pCover, nWidth, nHeight, pColors, bPaletteChanged);
  GpuFrameResolve(pCmd, nWidth, nHeight, shown.bCompose);
  shown.bHd = GpuFrameHd(pCmd, pColors, nWidth, nHeight, shown.bCompose);
  GpuFrameGround(pCmd, nWidth, nHeight, &shown);
  shown.pFrame = shown.bHd ? g_hdFrame : g_frame;
  shown.nW = shown.bHd ? g_nHdW : nWidth;
  shown.nH = shown.bHd ? g_nHdH : nHeight;
  shown.fCoverScale = shown.bHd ? static_cast<float>(PortHdScale()) : 1.0f;
  const bool bFx = GpuFxReady();
  GpuFrameSnap snap;
  GpuFrameEffects(pCmd, nWidth, nHeight, shown, bFx, bCheck, &snap);
  GpuVideoUp(pCmd);
  const bool bShown = !bPresent || GpuFrameWindow(pCmd, nWidth, nHeight, shown, bFx);

  if (!bCheck && !snap.bOn) {
    GpuSubmit(pCmd);
    return bShown;
  }
  SDL_GPUFence *pFence = SDL_SubmitGPUCommandBufferAndAcquireFence(pCmd);
  if (snap.bOn) {
    g_bSnapOk = false;
    g_nSnapW = snap.pFrom != NULL ? shown.nW : nWidth;
    g_nSnapH = snap.pFrom != NULL ? shown.nH : nHeight;
    g_nSnapState = 2;
  }
  if (pFence == NULL)
    return bShown;
  SDL_WaitForGPUFences(g_device, true, &pFence, 1);
  SDL_ReleaseGPUFence(g_device, pFence);
  if (snap.bOn) {
    const void *pGot = SDL_MapGPUTransferBuffer(g_device, snap.pDown, false);
    if (pGot != NULL) {
      g_bSnapOk = GpuSavePixels(pGot, g_nSnapW, g_nSnapH, g_szSnap);
      SDL_UnmapGPUTransferBuffer(g_device, snap.pDown);
    }
  }
  if (!bCheck)
    return bShown;
  if (shown.bCompose) {
    unsigned long long nCovered = 0;
    for (int i = 0; i < nWidth * nHeight; ++i)
      nCovered += pCover[i] != 0;
    g_nCovered += nCovered;
    g_nComposed += nCovered != 0;
  }
  // With the GPU alone drawing the map, the screen has only what the software drew to
  // compare with.
  GpuCompare(pScreen, pColors, shown.bCompose && PortSceneGpuOnly() ? pCover : NULL);
  return bShown;
}
