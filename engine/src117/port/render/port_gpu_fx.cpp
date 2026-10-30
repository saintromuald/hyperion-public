// port_gpu_fx.cpp -- the effects after the scene (parts 3 and 4): the bloom, the grade and
// vignette, and the 2D lights, with their live values (docs/renderer.md). Presentation only.
#include <windows.h>
#include <stdio.h>
#include "port_gpu_internal.h"
#include "port_view.h"  // PortViewCamera
#include "PSBright.dxil.h"
#include "PSBright.spv.h"
#include "PSDown.dxil.h"
#include "PSDown.spv.h"
#include "PSUp.dxil.h"
#include "PSUp.spv.h"
#include "PSPost.dxil.h"
#include "PSPost.spv.h"
#include "PSLights.dxil.h"
#include "PSLights.spv.h"
#if defined(__APPLE__)
#include "PSBright.msl.h"
#include "PSDown.msl.h"
#include "PSUp.msl.h"
#include "PSPost.msl.h"
#include "PSLights.msl.h"
#endif

// The effects' passes and the bloom's levels, each half the one above, from half the
// frame's size down; the way back up (g_bloomUp) is at the size of the level each adds.
static SDL_GPUGraphicsPipeline *g_brightPipeline;
static SDL_GPUGraphicsPipeline *g_downPipeline;
static SDL_GPUGraphicsPipeline *g_upPipeline;
static SDL_GPUTexture *g_bloomDown[BLOOM_LEVELS];
static int g_anBloomW[BLOOM_LEVELS];
static int g_anBloomH[BLOOM_LEVELS];
// The 2D lights' pass, and the frame's lights as it reads them.
static SDL_GPUGraphicsPipeline *g_lightsPipeline;
static SDL_GPUBuffer *g_lightBuf;
static SDL_GPUTransferBuffer *g_lightUp;

// Their values, live in the channel (port_control.h). "effects" turns them all on or
// off; it starts as the settings file's hyperion/effects, off when it is not there. The
// rest start at a first look to tune from.
static float g_fFxOn;
static float g_fBloom = 0.8f;
static float g_fBloomThreshold = 0.6f;
static float g_fBloomKnee = 0.25f;
static float g_fBloomRadius = 1.0f;
static float g_fExposure;
static float g_fContrast = 1.05f;
static float g_fSaturation = 1.1f;
static float g_fTemperature;
static float g_fTint;
static float g_fVignette = 0.3f;
static float g_fVignetteSoft = 0.5f;
static float g_fLightsOn = 1.0f;
static float g_fLightStrength = 1.0f;
static float g_fAmbient = 1.0f;
static float g_fLightAdd = 0.06f;

// effects.hlsl's BloomParams and LightParams.
struct BloomParams {
  float fDstW, fDstH, fSrcW, fSrcH;
  float fThreshold, fKnee, fRadius, fHasCover;
};

struct LightParams {
  float fDstW, fDstH, fFrameW, fFrameH;
  Uint32 nCount;
  float fUnused[3];
};

// The state the other parts share (port_gpu_internal.h).
SDL_GPUGraphicsPipeline *g_postPipeline;
SDL_GPUGraphicsPipeline *g_postSnapPipeline;
SDL_GPUTexture *g_bloomUp[BLOOM_LEVELS - 1];
SDL_GPUTexture *g_post;
SDL_GPUTransferBuffer *g_postDown;
SDL_GPUTexture *g_lightTex;

// The effects' pipelines and the lights' buffer, with pVertex, the present's vertex shader;
// without them the frame is drawn without its effects.
void GpuFxInit(SDL_GPUShader *pVertex)
{
  SDL_GPUShader *pBright = GpuShader(GPU_CODE(PSBright), "PSBright", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, 1);
  SDL_GPUShader *pDown = GpuShader(GPU_CODE(PSDown), "PSDown", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0, 1);
  SDL_GPUShader *pUp = GpuShader(GPU_CODE(PSUp), "PSUp", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 0, 1);
  SDL_GPUShader *pPost = GpuShader(GPU_CODE(PSPost), "PSPost", SDL_GPU_SHADERSTAGE_FRAGMENT, 4, 2, 1);
  SDL_GPUShader *pLights = GpuShader(GPU_CODE(PSLights), "PSLights", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1, 1);
  if (pVertex != NULL && pBright != NULL && pDown != NULL && pUp != NULL && pPost != NULL && pLights != NULL) {
    g_lightsPipeline = GpuPipeline(pVertex, pLights, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
    SDL_GPUBufferCreateInfo info;
    SDL_zero(info);
    info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    info.size = sizeof(PortLightGpu) * PORT_LIGHTS_MAX;
    g_lightBuf = SDL_CreateGPUBuffer(g_device, &info);
    g_lightUp = GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, sizeof(PortLightGpu) * PORT_LIGHTS_MAX);
    g_brightPipeline = GpuPipeline(pVertex, pBright, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
    g_downPipeline = GpuPipeline(pVertex, pDown, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
    g_upPipeline = GpuPipeline(pVertex, pUp, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
    g_postSnapPipeline = GpuPipeline(pVertex, pPost, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    g_postPipeline = g_swapFormat == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ? g_postSnapPipeline
                                                                         : GpuPipeline(pVertex, pPost, g_swapFormat);
  }
  SDL_GPUShader *apFx[] = {pBright, pDown, pUp, pPost, pLights};
  for (SDL_GPUShader *p : apFx)
    if (p != NULL)
      SDL_ReleaseGPUShader(g_device, p);
  if (g_brightPipeline == NULL || g_downPipeline == NULL || g_upPipeline == NULL || g_postSnapPipeline == NULL ||
      g_postPipeline == NULL || g_lightsPipeline == NULL || g_lightBuf == NULL || g_lightUp == NULL)
    GpuFxRelease();
  GpuFxValues();
}

void GpuFxRelease(void)
{
  SDL_GPUGraphicsPipeline **app[] = {&g_brightPipeline, &g_downPipeline, &g_upPipeline, &g_lightsPipeline,
                                     &g_postSnapPipeline, &g_postPipeline};
  for (SDL_GPUGraphicsPipeline **pp : app) {
    // the swapchain's may be the snapshot's
    if (*pp != NULL && (pp != &g_postPipeline || g_postPipeline != g_postSnapPipeline))
      SDL_ReleaseGPUGraphicsPipeline(g_device, *pp);
  }
  for (SDL_GPUGraphicsPipeline **pp : app)
    *pp = NULL;
  if (g_lightBuf != NULL)
    SDL_ReleaseGPUBuffer(g_device, g_lightBuf);
  if (g_lightUp != NULL)
    SDL_ReleaseGPUTransferBuffer(g_device, g_lightUp);
  g_lightBuf = NULL;
  g_lightUp = NULL;
}

void GpuFxReleaseSized(void)
{
  for (int i = 0; i < BLOOM_LEVELS; ++i) {
    if (g_bloomDown[i] != NULL)
      SDL_ReleaseGPUTexture(g_device, g_bloomDown[i]);
    g_bloomDown[i] = NULL;
    if (i < BLOOM_LEVELS - 1 && g_bloomUp[i] != NULL)
      SDL_ReleaseGPUTexture(g_device, g_bloomUp[i]);
    if (i < BLOOM_LEVELS - 1)
      g_bloomUp[i] = NULL;
  }
  if (g_post != NULL)
    SDL_ReleaseGPUTexture(g_device, g_post);
  if (g_postDown != NULL)
    SDL_ReleaseGPUTransferBuffer(g_device, g_postDown);
  g_post = NULL;
  g_postDown = NULL;
  if (g_lightTex != NULL)
    SDL_ReleaseGPUTexture(g_device, g_lightTex);
  g_lightTex = NULL;
}

// The effects' textures for a frame nWidth by nHeight; without them the frame is drawn
// without its effects.
void GpuFxFitSize(int nWidth, int nHeight)
{
  const SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  bool bFx = g_postPipeline != NULL;
  for (int i = 0; i < BLOOM_LEVELS && bFx; ++i) {
    g_anBloomW[i] = SDL_max(1, nWidth >> (i + 1));
    g_anBloomH[i] = SDL_max(1, nHeight >> (i + 1));
    g_bloomDown[i] = GpuTexture(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, usage, g_anBloomW[i], g_anBloomH[i]);
    if (i < BLOOM_LEVELS - 1)
      g_bloomUp[i] = GpuTexture(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, usage, g_anBloomW[i], g_anBloomH[i]);
    bFx = g_bloomDown[i] != NULL && (i == BLOOM_LEVELS - 1 || g_bloomUp[i] != NULL);
  }
  if (bFx) {
    g_lightTex = GpuTexture(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, usage, g_anBloomW[0], g_anBloomH[0]);
    bFx = g_lightTex != NULL;
  }
  if (bFx) {
    g_post = GpuTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, usage, nWidth, nHeight);
    g_postDown = GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, static_cast<Uint32>(nWidth * nHeight * 4));
  }
}

bool GpuFxReady(void)
{
  return g_fFxOn >= 0.5f && g_post != NULL && g_postDown != NULL;
}

// One full-target pass of an effect: up to four textures, the cover, with pHud beside it
// for PSPost, and its values.
void GpuFxPass(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pTarget, SDL_GPUGraphicsPipeline *pPipeline,
               SDL_GPUTexture *pFirst, SDL_GPUTexture *pSecond, bool bCover, const void *pParams,
               Uint32 nParams, SDL_GPUTexture *pThird, SDL_GPUTexture *pFourth, SDL_GPUBuffer *pHud)
{
  SDL_GPUColorTargetInfo target;
  SDL_zero(target);
  target.texture = pTarget;
  target.load_op = SDL_GPU_LOADOP_DONT_CARE;
  target.store_op = SDL_GPU_STOREOP_STORE;
  target.cycle = true;
  SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(pCmd, &target, 1, NULL);
  SDL_BindGPUGraphicsPipeline(pPass, pPipeline);
  SDL_GPUTextureSamplerBinding bindings[4];
  bindings[0].texture = pFirst;
  bindings[0].sampler = g_linear;
  bindings[1].texture = pSecond;
  bindings[1].sampler = g_linear;
  bindings[2].texture = pThird;
  bindings[2].sampler = g_linear;
  bindings[3].texture = pFourth;
  bindings[3].sampler = g_linear;
  SDL_BindGPUFragmentSamplers(pPass, 0, bindings,
                              pFourth != NULL ? 4 : pThird != NULL ? 3 : pSecond != NULL ? 2 : 1);
  if (bCover) {
    SDL_GPUBuffer *apStorage[2] = {g_cover, pHud};
    SDL_BindGPUFragmentStorageBuffers(pPass, 0, apStorage, pHud != NULL ? 2 : 1);
  }
  SDL_PushGPUFragmentUniformData(pCmd, 0, pParams, nParams);
  SDL_DrawGPUPrimitives(pPass, 3, 1, 0, 0);
  SDL_EndGPURenderPass(pPass);
}

// The bloom, from the resolved frame into g_bloomUp[0]: the bright pixels, down the
// levels, and back up.
void GpuBloom(SDL_GPUCommandBuffer *pCmd, int nWidth, int nHeight, bool bCover)
{
  BloomParams p;
  p.fThreshold = g_fBloomThreshold;
  p.fKnee = SDL_max(g_fBloomKnee, 0.001f);
  p.fRadius = g_fBloomRadius;
  p.fHasCover = bCover ? 1.0f : 0.0f;
  p.fDstW = static_cast<float>(g_anBloomW[0]);
  p.fDstH = static_cast<float>(g_anBloomH[0]);
  p.fSrcW = static_cast<float>(nWidth);
  p.fSrcH = static_cast<float>(nHeight);
  GpuFxPass(pCmd, g_bloomDown[0], g_brightPipeline, g_frame, NULL, true, &p, sizeof p);
  for (int i = 1; i < BLOOM_LEVELS; ++i) {
    p.fDstW = static_cast<float>(g_anBloomW[i]);
    p.fDstH = static_cast<float>(g_anBloomH[i]);
    p.fSrcW = static_cast<float>(g_anBloomW[i - 1]);
    p.fSrcH = static_cast<float>(g_anBloomH[i - 1]);
    GpuFxPass(pCmd, g_bloomDown[i], g_downPipeline, g_bloomDown[i - 1], NULL, false, &p, sizeof p);
  }
  SDL_GPUTexture *pBelow = g_bloomDown[BLOOM_LEVELS - 1];
  for (int i = BLOOM_LEVELS - 2; i >= 0; --i) {
    p.fDstW = static_cast<float>(g_anBloomW[i]);
    p.fDstH = static_cast<float>(g_anBloomH[i]);
    p.fSrcW = static_cast<float>(g_anBloomW[i + 1]);
    p.fSrcH = static_cast<float>(g_anBloomH[i + 1]);
    GpuFxPass(pCmd, g_bloomUp[i], g_upPipeline, pBelow, g_bloomDown[i], false, &p, sizeof p);
    pBelow = g_bloomUp[i];
  }
}

void GpuPostParams(const SDL_FRect &box, int nWidth, int nHeight, bool bCover, PostParams *p)
{
  p->fDstX = box.x;
  p->fDstY = box.y;
  p->fDstW = box.w;
  p->fDstH = box.h;
  p->fTexW = static_cast<float>(nWidth);
  p->fTexH = static_cast<float>(nHeight);
  p->fHasCover = bCover ? 1.0f : 0.0f;
  p->fBloom = g_fBloom;
  p->fExposure = g_fExposure;
  p->fContrast = g_fContrast;
  p->fSaturation = g_fSaturation;
  p->fTemperature = g_fTemperature;
  p->fTint = g_fTint;
  p->fVignette = g_fVignette;
  p->fVignetteSoft = SDL_max(g_fVignetteSoft, 0.001f);
  p->fCoverScale = 1.0f;
  p->fLightsOn = g_fLightsOn >= 0.5f ? 1.0f : 0.0f;
  p->fLightStrength = g_fLightStrength;
  p->fAmbient = g_fAmbient;
  p->fLightAdd = g_fLightAdd;
  GpuMapMove move;
  GpuMapMoved(nWidth, nHeight, &move);
  p->fMoved = move.bMoved ? 1.0f : 0.0f;
  p->fHudShift = move.fHudShift;
  p->fHudAlpha = move.fHudAlpha;
  p->fHasGround = 0.0f;
  for (int i = 0; i < 4; ++i) {
    p->afMapAt[i] = move.afMapAt[i];
    p->afFrameAt[i] = move.afFrameAt[i];
    p->afLightAt[i] = move.afLightAt[i];
  }
  p->fHasHud = 0.0f;
  p->fArtSoft = 0.0f;
  p->afUnused[0] = p->afUnused[1] = 0.0f;
}

// The frame's lights into the light buffer: uploaded, then added up (PSLights). With
// none, the buffer is black, and the map takes the ambient light alone.
void GpuLights(SDL_GPUCommandBuffer *pCmd, int nWidth, int nHeight)
{
  static PortLightGpu s_lights[PORT_LIGHTS_MAX];
  // the lights come from the map passes the GPU scene makes whole (port_lights.cpp)
  const int nLights = PortSceneStage() != SCENE_OFF ? PortLightsFrame(s_lights, PORT_LIGHTS_MAX) : 0;
  if (g_zoomShown.bOn) {
    // zoomed: they are the render view's, and go on the screen through the camera
    // as it is now, smaller by the zoom, so the light texture is the screen's as it stands
    double fCamX, fCamY, z;
    PortViewCamera(&fCamX, &fCamY, &z);
    for (int k = 0; k < nLights; ++k) {
      s_lights[k].fX = static_cast<float>((s_lights[k].fX + g_zoomShown.nMapX - fCamX) / z);
      s_lights[k].fY = static_cast<float>((s_lights[k].fY + g_zoomShown.nMapY - fCamY) / z);
      s_lights[k].fRadius = static_cast<float>(s_lights[k].fRadius / z);
    }
  }
  if (nLights > 0) {
    void *pMapped = SDL_MapGPUTransferBuffer(g_device, g_lightUp, true);
    if (pMapped != NULL) {
      SDL_memcpy(pMapped, s_lights, sizeof(PortLightGpu) * nLights);
      SDL_UnmapGPUTransferBuffer(g_device, g_lightUp);
      SDL_GPUCopyPass *pCopy = SDL_BeginGPUCopyPass(pCmd);
      SDL_GPUTransferBufferLocation from;
      SDL_GPUBufferRegion to;
      from.transfer_buffer = g_lightUp;
      from.offset = 0;
      to.buffer = g_lightBuf;
      to.offset = 0;
      to.size = static_cast<Uint32>(sizeof(PortLightGpu) * nLights);
      SDL_UploadToGPUBuffer(pCopy, &from, &to, true);
      SDL_EndGPUCopyPass(pCopy);
    }
  }
  LightParams p;
  p.fDstW = static_cast<float>(g_anBloomW[0]);
  p.fDstH = static_cast<float>(g_anBloomH[0]);
  p.fFrameW = static_cast<float>(nWidth);
  p.fFrameH = static_cast<float>(nHeight);
  p.nCount = static_cast<Uint32>(nLights);
  p.fUnused[0] = p.fUnused[1] = p.fUnused[2] = 0.0f;
  SDL_GPUColorTargetInfo target;
  SDL_zero(target);
  target.texture = g_lightTex;
  target.load_op = SDL_GPU_LOADOP_DONT_CARE;
  target.store_op = SDL_GPU_STOREOP_STORE;
  target.cycle = true;
  SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(pCmd, &target, 1, NULL);
  SDL_BindGPUGraphicsPipeline(pPass, g_lightsPipeline);
  SDL_BindGPUFragmentStorageBuffers(pPass, 0, &g_lightBuf, 1);
  SDL_PushGPUFragmentUniformData(pCmd, 0, &p, sizeof p);
  SDL_DrawGPUPrimitives(pPass, 3, 1, 0, 0);
  SDL_EndGPURenderPass(pPass);
}

// The values in the channel, once.
void GpuFxValues(void)
{
  static bool s_bDone;
  if (s_bDone)
    return;
  s_bDone = true;
  double dOn = 0.0;
  PortSettingNumber("hyperion", "effects", &dOn);
  g_fFxOn = dOn != 0.0 ? 1.0f : 0.0f;
  PortControlFloat("effects", &g_fFxOn, 0.0f, 1.0f, "the effects after the scene: on (1) or off (0); GPU presenter only",
                   1.0f);
  PortControlFloat("bloom.strength", &g_fBloom, 0.0f, 3.0f, "how much the bright pixels glow");
  PortControlFloat("bloom.threshold", &g_fBloomThreshold, 0.0f, 1.0f, "how bright a pixel is before it glows");
  PortControlFloat("bloom.knee", &g_fBloomKnee, 0.0f, 0.5f, "how softly the glow starts below the threshold");
  PortControlFloat("bloom.radius", &g_fBloomRadius, 0.25f, 3.0f, "how far the glow spreads");
  PortControlFloat("grade.exposure", &g_fExposure, -2.0f, 2.0f, "the map's brightness, in stops");
  PortControlFloat("grade.contrast", &g_fContrast, 0.5f, 1.5f, "the map's contrast; 1 leaves it");
  PortControlFloat("grade.saturation", &g_fSaturation, 0.0f, 2.0f, "the map's colour; 0 grey, 1 leaves it");
  PortControlFloat("grade.temperature", &g_fTemperature, -1.0f, 1.0f, "the map's white balance: cooler below 0, warmer above");
  PortControlFloat("grade.tint", &g_fTint, -1.0f, 1.0f, "the map's tint: greener below 0, more magenta above");
  PortControlFloat("vignette.strength", &g_fVignette, 0.0f, 1.0f, "how dark the corners go");
  PortControlFloat("vignette.softness", &g_fVignetteSoft, 0.05f, 1.0f, "how far in from the corners the darkening starts");
  PortControlFloat("lights", &g_fLightsOn, 0.0f, 1.0f,
                   "the 2D lights from explosions, fire and energy (with the GPU scene on); the lights command has "
                   "their table",
                   1.0f);
  PortControlFloat("lights.strength", &g_fLightStrength, 0.0f, 4.0f, "how strongly the lights light the map");
  PortControlFloat("lights.ambient", &g_fAmbient, 0.1f, 1.5f,
                   "the map's light where no light reaches: 1 as drawn, lower for dusk or night");
  PortControlFloat("lights.add", &g_fLightAdd, 0.0f, 1.0f, "how much light is added on top, so it shows on dark ground");
  double dFull = 1.0;
  PortSettingNumber("hyperion", "fullColour", &dFull);
  g_fFull = dFull != 0.0 ? 1.0f : 0.0f;
  PortControlFloat("colour.full", &g_fFull, 0.0f, 1.0f,
                   "the map in full colour (1): the players' colours in RGB, the fog, shadows and "
                   "translucency blended as colours; 0 draws it as the original does, to the pixel. GPU scene only",
                   1.0f);
  PortControlFloat("hd", &g_fHdOn, 0.0f, 1.0f,
                   "the sprites drawn from Remastered's HD art (with the GPU scene at its last stage and a "
                   "Remastered install); HYPERION_HD_SCALE=4 for 4x",
                   1.0f);
  PortControlFloat("hd.fog", &g_fHdFog, 0.0f, 2.0f,
                   "how much the fog darkens the HD frames: 0 none, 1 as the original's shades, more is darker");
  PortControlFloat("hd.debug", &g_fHdDebug, 0.0f, 1.0f,
                   "1 paints each HD frame with the fog's brightness it takes, as grey, to check it against the ground",
                   1.0f);
  PortControlFloat("hd.terrain", &g_fHdTerrain, 0.0f, 1.0f,
                   "the terrain from Remastered's HD tileset too, where the map shows only terrain", 1.0f);
  PortControlFloat("hd.mods", &g_fHdMods, 0.0f, 1.0f,
                   "a mod's art sheets drawn at their own resolution in full colour, whether or not Remastered's "
                   "art is; 0 draws them from their classic frames", 1.0f);
  PortControlFloat("hd.console", &g_fHdConsole, 0.0f, 1.0f,
                   "Remastered's console too, under the panels' content the original draws", 1.0f);
  PortControlFloat("hd.lighting", &g_fHdLighting, 0.0f, 1.0f,
                   "how much of Remastered's lighting the HD frames take (normal, specular and ambient "
                   "occlusion maps, a light from above, the frame's lights, the emissive glow): 0 none, 1 "
                   "all; Settings' HD lighting");
  PortControlFloat("hd.light.strength", &g_fHdLightStrength, 0.0f, 4.0f,
                   "how strongly the frame's lights (the lights table) light the HD frames");
  PortControlFloat("hd.fade", &g_fHdFade, 0.0f, 2.0f,
                   "seconds the art takes to blend between SD and HD when hd changes (F5); 0 at once");
  PortControlFloat("hd.water", &g_fHdWater, 0.0f, 1.0f,
                   "the HD terrain's water: 0 still; above it ripples, Remastered's normal maps bending what is "
                   "under it, and its light toward the screen's middle is this strong, 1 as Remastered's");
  PortControlFloat("hd.kinds", &g_fHdKinds, 0.0f, 63.0f,
                   "which of an image's frames are drawn in HD, added up: 1 sprites, 2 shadows, 4 translucent, "
                   "8 cloaked, 16 cloaking, 32 the warp flash",
                   1.0f);
}
