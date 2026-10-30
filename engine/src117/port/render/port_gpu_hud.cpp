// port_gpu_hud.cpp -- the HUD design drawn over a game's frame (port_hud.h,
// shaders/hud.hlsl and hudshape.hlsl): each layer's content into a buffer the size of its box and margin, its effects on
// that buffer, its glow blurred from it and its contact shadow; the layers between two slots of
// a panel laid into that panel's run, each with its own placing; each run laid on the screen with
// its panel's placing and opacity. A layer is drawn again only when what it draws changes, and a
// run only when a layer of it or their placing does, so most frames only lay the runs.
// Presentation only.
#define NOMINMAX
#include <windows.h>
#include <math.h>
#include <string.h>
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include "VSHudQuad.dxil.h"
#include "VSHudQuad.spv.h"
#include "VSHudPlace.dxil.h"
#include "VSHudPlace.spv.h"
#include "PSHudPlaced.dxil.h"
#include "PSHudPlaced.spv.h"
#include "PSHudFx.dxil.h"
#include "PSHudFx.spv.h"
#include "PSHudBlur.dxil.h"
#include "PSHudBlur.spv.h"
#include "PSHudDown.dxil.h"
#include "PSHudDown.spv.h"
#include "PSHudUp.dxil.h"
#include "PSHudUp.spv.h"
#include "PSHudShape.dxil.h"
#include "PSHudShape.spv.h"
#include "PSHudShadow.dxil.h"
#include "PSHudShadow.spv.h"
#if defined(__APPLE__)
#include "VSHudQuad.msl.h"
#include "VSHudPlace.msl.h"
#include "PSHudPlaced.msl.h"
#include "PSHudFx.msl.h"
#include "PSHudBlur.msl.h"
#include "PSHudDown.msl.h"
#include "PSHudUp.msl.h"
#include "PSHudShape.msl.h"
#include "PSHudShadow.msl.h"
#endif
#include "port_gpu_internal.h"
#include "port_console.h"  // PortConsoleHudSource, PortConsoleHudLabel, PortWireframeCell
#include "port_present.h"  // PortPresentPaletteRGB
#include "../data/port_video.h"  // an image layer's video
#include "../front/port_text.h"
#include "../front/port_ui.h"
#include "../platform/port_util.h"  // PortReadFile
#include "../hud/port_hud.h"
#include "../hud/port_hudstate.h"
#include "../hud/port_hudplay.h"
#include "../hud/port_hudshape.h"
#include "../hud/port_hudstats.h"
#include "../hud/port_hudshape_params.h"
#include "port_hwcursor.h"  // PortGrpFrameDecode
#include "CUnit.h"
#include "../../h/selectlimits.h"  // StatUnits, SelectionSubgroups: the selection's grid
#include "../../h/statdata.h"      // StatDataBoxFrame
#include "../../gen/globals117.h"  // gpStatUnit
#include "../../h/Storm.h"  // SFileLoadFile, 1.17's art for an asset's "art"
#include "../data/port_stormapi.h"  // SFileUnloadFile
#include "../../h/grp.h"  // FRAME

namespace {

const double SCREEN = 1080.0;
const double kPi = 3.14159265358979323846;
const double PAD = 72.0;  // the room round a run for light and layers that lean out (hudplayer.js)
const SDL_GPUTextureFormat RGBA8 = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

enum Blend { BLEND_NONE, BLEND_OVER, BLEND_ADD, BLEND_ATOP };

struct Tex {
  SDL_GPUTexture *p = NULL;
  int w = 0, h = 0;
};

void Free(Tex *t) {
  if (t->p != NULL)
    SDL_ReleaseGPUTexture(g_device, t->p);
  t->p = NULL;
  t->w = t->h = 0;
}

// a target and a source, exactly w x h
bool Fit(Tex *t, int w, int h, SDL_GPUTextureFormat format = RGBA8) {
  w = SDL_max(w, 1);
  h = SDL_max(h, 1);
  if (t->p != NULL && t->w == w && t->h == h)
    return true;
  Free(t);
  t->p = GpuTexture(format, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h);
  t->w = w;
  t->h = h;
  return t->p != NULL;
}

// a scratch at least w x h, which grows and never shrinks
bool FitAtLeast(Tex *t, int w, int h) {
  if (t->p != NULL && t->w >= w && t->h >= h)
    return true;
  return Fit(t, SDL_max(w, t->w), SDL_max(h, t->h));
}

// A 2D affine matrix (a, b, c, d, e, f): x' = a x + c y + e, y' = b x + d y + f.
struct Mat {
  double m[6] = {1, 0, 0, 1, 0, 0};
  Mat() {}
  Mat(const double *p) { memcpy(m, p, sizeof m); }
  Mat(double a, double b, double c, double d, double e, double f) {
    m[0] = a, m[1] = b, m[2] = c, m[3] = d, m[4] = e, m[5] = f;
  }
  Mat operator*(const Mat &o) const {
    return Mat(m[0] * o.m[0] + m[2] * o.m[1], m[1] * o.m[0] + m[3] * o.m[1], m[0] * o.m[2] + m[2] * o.m[3],
               m[1] * o.m[2] + m[3] * o.m[3], m[0] * o.m[4] + m[2] * o.m[5] + m[4],
               m[1] * o.m[4] + m[3] * o.m[5] + m[5]);
  }
};

// hud.hlsl's blocks
struct PlaceParams {
  float m[4], move[4], uv[4];
};
struct FxParams {
  float box[4], uv[4], tint[4], addColor[4], scanColor[4], addScan[4], scanShape[4], wipe[4], meter[4];
};
struct BlurParams {
  float step[4], uv[4], valid[4], color[4];
};
struct KawaseParams {
  float texel[4];
};

struct Pipes {
  SDL_GPUGraphicsPipeline *shape, *shadow, *fx, *blur, *down, *up;
  SDL_GPUGraphicsPipeline *over, *add, *atop;  // laying a layer into a run
  SDL_GPUGraphicsPipeline *overSwap, *addSwap;  // laying a run on the screen
};
Pipes g_pipes;
bool g_bTried;
bool g_bReady;
Tex g_white;  // 1 x 1, behind glass with no screen to show

// A layer's buffers: its pixels after its effects, its glow and its contact shadow, and the
// key of what they show.
struct LayerCache {
  unsigned long long key = 0;
  bool bSeen = false;
  Tex img, glow, shadow;
  int nGlowPad = 0;
  bool bGlow = false, bShadow = false;
};
std::map<unsigned long long, LayerCache> g_layers;  // by panel, layer and tile

// The design's runs: the layers of a panel between two slots, of one blend.
struct Run {
  int nPanel;
  bool bAdd;
  unsigned long long key = 0;
  Tex tex;
};
struct Part {
  int nRun;    // a run, or -1 for a slot
  int nLayer;  // the slot's layer
};
const HudDesign *g_pDesign;
unsigned long long g_nStructKey;  // the design's loads when it was built: PortHudLoads, or the preview's
std::vector<Run> g_runs;
std::vector<std::vector<Part>> g_parts;     // per panel
std::vector<std::vector<int>> g_layerRun;   // per panel and layer: its run, -1 a slot
std::vector<int> g_order;                   // the panels bottom to top
Tex g_scratch, g_scratchGlow;
Tex g_back[7];  // the screen behind glass, as each level of its blur has it
unsigned int g_nFrame;

SDL_GPUGraphicsPipeline *Pipeline(SDL_GPUShader *pVs, SDL_GPUShader *pPs, SDL_GPUTextureFormat format, Blend blend) {
  SDL_GPUColorTargetDescription target;
  SDL_GPUGraphicsPipelineCreateInfo info;
  SDL_zero(target);
  SDL_zero(info);
  target.format = format;
  SDL_GPUColorTargetBlendState &b = target.blend_state;
  if (blend != BLEND_NONE) {
    b.enable_blend = true;
    b.color_blend_op = b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    if (blend == BLEND_OVER) {
      b.src_color_blendfactor = b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
      b.dst_color_blendfactor = b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    } else if (blend == BLEND_ADD) {
      b.src_color_blendfactor = b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
      b.dst_color_blendfactor = b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    } else {
      // source-atop: only over what is there, which keeps its alpha
      b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_ALPHA;
      b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
      b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
      b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    }
  }
  info.vertex_shader = pVs;
  info.fragment_shader = pPs;
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  info.target_info.color_target_descriptions = &target;
  info.target_info.num_color_targets = 1;
  return SDL_CreateGPUGraphicsPipeline(g_device, &info);
}

bool Init(void) {
  if (g_bTried)
    return g_bReady;
  g_bTried = true;
  const SDL_GPUShaderStage V = SDL_GPU_SHADERSTAGE_VERTEX, F = SDL_GPU_SHADERSTAGE_FRAGMENT;
  SDL_GPUShader *pQuad = GpuShader(GPU_CODE(VSHudQuad), "VSHudQuad", V, 0, 0, 0);
  SDL_GPUShader *pPlace = GpuShader(GPU_CODE(VSHudPlace), "VSHudPlace", V, 0, 0, 1);
  SDL_GPUShader *pPlaced = GpuShader(GPU_CODE(PSHudPlaced), "PSHudPlaced", F, 1, 0, 1);
  SDL_GPUShader *pFx = GpuShader(GPU_CODE(PSHudFx), "PSHudFx", F, 1, 0, 1);
  SDL_GPUShader *pBlur = GpuShader(GPU_CODE(PSHudBlur), "PSHudBlur", F, 1, 0, 1);
  SDL_GPUShader *pDown = GpuShader(GPU_CODE(PSHudDown), "PSHudDown", F, 1, 0, 1);
  SDL_GPUShader *pUp = GpuShader(GPU_CODE(PSHudUp), "PSHudUp", F, 1, 0, 1);
  SDL_GPUShader *pShape = GpuShader(GPU_CODE(PSHudShape), "PSHudShape", F, 1, 0, 1);
  SDL_GPUShader *pShadow = GpuShader(GPU_CODE(PSHudShadow), "PSHudShadow", F, 1, 0, 1);
  if (pQuad != NULL && pShape != NULL)
    g_pipes.shape = Pipeline(pQuad, pShape, RGBA8, BLEND_NONE);
  if (pQuad != NULL && pShadow != NULL)
    g_pipes.shadow = Pipeline(pQuad, pShadow, RGBA8, BLEND_NONE);
  if (pQuad != NULL && pFx != NULL)
    g_pipes.fx = Pipeline(pQuad, pFx, RGBA8, BLEND_NONE);
  if (pQuad != NULL && pBlur != NULL)
    g_pipes.blur = Pipeline(pQuad, pBlur, RGBA8, BLEND_NONE);
  if (pQuad != NULL && pDown != NULL)
    g_pipes.down = Pipeline(pQuad, pDown, RGBA8, BLEND_NONE);
  if (pQuad != NULL && pUp != NULL)
    g_pipes.up = Pipeline(pQuad, pUp, RGBA8, BLEND_NONE);
  if (pPlace != NULL && pPlaced != NULL) {
    g_pipes.over = Pipeline(pPlace, pPlaced, RGBA8, BLEND_OVER);
    g_pipes.add = Pipeline(pPlace, pPlaced, RGBA8, BLEND_ADD);
    g_pipes.atop = Pipeline(pPlace, pPlaced, RGBA8, BLEND_ATOP);
    g_pipes.overSwap = Pipeline(pPlace, pPlaced, g_swapFormat, BLEND_OVER);
    g_pipes.addSwap = Pipeline(pPlace, pPlaced, g_swapFormat, BLEND_ADD);
  }
  SDL_GPUShader *apShaders[] = {pQuad, pPlace, pPlaced, pFx, pBlur, pDown, pUp, pShape, pShadow};
  for (SDL_GPUShader *p : apShaders)
    if (p != NULL)
      SDL_ReleaseGPUShader(g_device, p);
  SDL_GPUGraphicsPipeline *const apPipes[] = {g_pipes.shape, g_pipes.shadow, g_pipes.fx, g_pipes.blur, g_pipes.down,
                                              g_pipes.up, g_pipes.over, g_pipes.add, g_pipes.atop, g_pipes.overSwap,
                                              g_pipes.addSwap};
  g_bReady = Fit(&g_white, 1, 1);
  for (SDL_GPUGraphicsPipeline *p : apPipes)
    g_bReady = g_bReady && p != NULL;
  if (!g_bReady)
    SDL_Log("Hyperion: the HUD's shaders did not build: %s", SDL_GetError());
  return g_bReady;
}

// A pass over the top left nW x nH of a target, cleared first: one quad, the pipeline's
// fragment shader reading a source with its block.
void Quad(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pTarget, int nW, int nH, SDL_GPUGraphicsPipeline *pPipe,
          SDL_GPUTexture *pSource, SDL_GPUSampler *pSampler, const void *pParams, Uint32 nParams) {
  SDL_GPUColorTargetInfo target;
  SDL_zero(target);
  target.texture = pTarget;
  target.load_op = SDL_GPU_LOADOP_CLEAR;
  target.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(pCmd, &target, 1, NULL);
  SDL_BindGPUGraphicsPipeline(pPass, pPipe);
  SDL_GPUViewport viewport = {0.0f, 0.0f, static_cast<float>(nW), static_cast<float>(nH), 0.0f, 1.0f};
  SDL_SetGPUViewport(pPass, &viewport);
  SDL_GPUTextureSamplerBinding binding = {pSource, pSampler};
  SDL_BindGPUFragmentSamplers(pPass, 0, &binding, 1);
  SDL_PushGPUFragmentUniformData(pCmd, 0, pParams, nParams);
  SDL_DrawGPUPrimitives(pPass, 3, 1, 0, 0);
  SDL_EndGPURenderPass(pPass);
}

// A texture's part (u0, v0, u1, v1) laid with a matrix from the unit square to the target's
// pixels.
void PlacePart(SDL_GPUCommandBuffer *pCmd, SDL_GPURenderPass *pPass, SDL_GPUTexture *pTex, const Mat &m,
               int nTargetW, int nTargetH, double alpha, const float uv[4], SDL_GPUSampler *pSampler) {
  PlaceParams vp;
  for (int i = 0; i < 4; ++i)
    vp.m[i] = static_cast<float>(m.m[i]);
  vp.move[0] = static_cast<float>(m.m[4]);
  vp.move[1] = static_cast<float>(m.m[5]);
  vp.move[2] = static_cast<float>(nTargetW);
  vp.move[3] = static_cast<float>(nTargetH);
  memcpy(vp.uv, uv, sizeof vp.uv);
  float a[4] = {static_cast<float>(alpha), 0.0f, 0.0f, 0.0f};
  SDL_PushGPUVertexUniformData(pCmd, 0, &vp, sizeof vp);
  SDL_PushGPUFragmentUniformData(pCmd, 0, a, sizeof a);
  SDL_GPUTextureSamplerBinding binding = {pTex, pSampler};
  SDL_BindGPUFragmentSamplers(pPass, 0, &binding, 1);
  SDL_DrawGPUPrimitives(pPass, 6, 1, 0, 0);
}

// A texture's top left part, to u1, v1, laid as PlacePart lays it, smoothed.
void Place(SDL_GPUCommandBuffer *pCmd, SDL_GPURenderPass *pPass, SDL_GPUTexture *pTex, const Mat &m, int nTargetW,
           int nTargetH, double alpha, float u1 = 1.0f, float v1 = 1.0f) {
  const float uv[4] = {0.0f, 0.0f, u1, v1};
  PlacePart(pCmd, pPass, pTex, m, nTargetW, nTargetH, alpha, uv, g_linear);
}

void Colour(const HudColour &c, const char *pszFallback, float out[4]) {
  if (c.bSet) {
    memcpy(out, c.c, sizeof c.c);
    return;
  }
  if (pszFallback == NULL || !HudParseColour(pszFallback, out))
    out[0] = out[1] = out[2] = out[3] = 0.0f;
}

void Set2(float *p, double a, double b) {
  p[0] = static_cast<float>(a);
  p[1] = static_cast<float>(b);
}
void Set4(float *p, double a, double b, double c, double d) {
  p[0] = static_cast<float>(a);
  p[1] = static_cast<float>(b);
  p[2] = static_cast<float>(c);
  p[3] = static_cast<float>(d);
}

// A shape layer's values for the shader, as hudshape.js's draw gives them.
void ShapeParams(const HudState &s, double w, double h, double px, double margin, const double at[2], double lw,
                 double time, HudShapeParams *u) {
  memset(u, 0, sizeof *u);
  Set2(u->uHalf, w / 2, h / 2);
  u->uPx = static_cast<float>(1.0 / px);
  u->uMargin = static_cast<float>(margin);
  Set2(u->uOrigin, at[0], at[1]);
  Set2(u->uScreen, lw, SCREEN);
  u->uTime = static_cast<float>(time);
  for (int i = 0; i < 4; ++i) {
    u->uCorner[i] = static_cast<float>(s.corners[i]);
    u->uKind[i] = static_cast<float>(s.cornerKinds[i]);
  }
  for (int i = 0; i < s.nCutouts; ++i) {
    const double *c = s.cutouts[i];
    Set4(u->uCut[i], c[0] + c[2] / 2 - w / 2, c[1] + c[3] / 2 - h / 2, c[2] / 2, c[3] / 2);
    u->uCutR[i] = static_cast<float>(c[4]);
  }
  u->uCutN = static_cast<float>(s.nCutouts);
  // a surface with no fill is lit white
  if (s.nFill > 0) {
    for (int i = 0; i < s.nFill; ++i)
      memcpy(u->uFill[i], s.fill[i], sizeof u->uFill[i]);
    u->uFillN = static_cast<float>(s.nFill);
  } else {
    if (s.nSurface != 0)
      Set4(u->uFill[0], 1, 1, 1, 1);
    u->uFillN = 1.0f;
  }
  const double fa = s.fillAngle * kPi / 180.0;
  Set2(u->uFillDir, cos(fa), sin(fa));
  u->uGlass = static_cast<float>(s.glass);
  Colour(s.glassTint, NULL, u->uGlassTint);
  u->uGlassNoise = static_cast<float>(s.glassNoise);
  u->uGlassStreak = static_cast<float>(s.glassStreak);
  const double la = s.light * kPi / 180.0;
  u->uBevel = static_cast<float>(s.bevel);
  u->uBevelStrength = static_cast<float>(s.bevelStrength);
  Set2(u->uLightFrom, cos(la), sin(la));
  Colour(s.highlight, "#ffffff", u->uHighlight);
  u->uInnerShadow = static_cast<float>(s.innerShadow);
  u->uInnerShadowStrength = static_cast<float>(s.innerShadowStrength);
  u->uRim = static_cast<float>(s.rim);
  u->uRimWidth = static_cast<float>(s.rimWidth);
  Colour(s.rimColor, "#ffffff", u->uRimColor);
  Colour(s.innerGlow, NULL, u->uInnerGlow);
  u->uInnerGlowSize = static_cast<float>(s.innerGlowSize);
  u->uLines = static_cast<float>(s.lines);
  u->uLineSpacing = static_cast<float>(s.lineSpacing);
  u->uHolo = static_cast<float>(s.holo);
  // a shape's stroke is one colour; a list of them strokes nothing, as in the design center
  if (s.nStroke == 1) {
    memcpy(u->uStroke, s.stroke[0], sizeof u->uStroke);
    u->uLine = static_cast<float>(s.line);
  }
  u->uEnergy = static_cast<float>(s.energy);
  Colour(s.energyColor, "#5fd0ff", u->uEnergyColor);
  u->uEnergyWidth = static_cast<float>(s.energyWidth);
  Colour(s.outerGlow, NULL, u->uOuterGlow);
  u->uOuterGlowSize = static_cast<float>(s.outerGlowSize);
  u->uSurf = static_cast<float>(s.nSurface);
  Colour(s.surfaceA, "#808080", u->uSurfA);
  Colour(s.surfaceB, "#404040", u->uSurfB);
  Colour(s.surfaceC, "#ffffff", u->uSurfC);
  u->uSurfScale = static_cast<float>(s.surfaceScale);
  Set4(u->uSurfP, s.gloss, s.wear, s.detail, s.surfaceSpeed);
  u->uMirror = s.bMirror ? 1.0f : 0.0f;
  Set4(u->uWobble, s.wobble, s.wobbleScale != 0 ? s.wobbleScale : 40, s.wobbleSpeed, 0);
  Set4(u->uSpines, s.spines, s.spineSpacing != 0 ? s.spineSpacing : 40, s.spineWidth, s.spineLean);
  u->uBow = static_cast<float>(s.bow);
  u->uBlend = static_cast<float>(s.smooth);
  Set2(u->uSegments, s.segments, s.segmentGap);
  Set4(u->uSeams, s.seams, s.seamWidth, s.seamStrength, s.seamAngle);
  Set4(u->uBolts, s.bolts, s.boltInset, s.boltSpacing != 0 ? s.boltSpacing : 100, s.nBoltStyle);
  Colour(s.boltColor, "#9aa4ad", u->uBoltColor);
  Set4(u->uInlay, s.inlay, s.inlayDepth, s.inlayGlow, 0);
  Colour(s.inlayColor, "#f4d273", u->uInlayColor);
  u->uReveal = static_cast<float>(s.reveal);
  u->uRevealKind = static_cast<float>(s.nRevealStyle);
  Colour(s.revealColor, "#ffffff00", u->uRevealColor);
  if (s.pPath != NULL) {
    HudPathFit fit;
    fit.nFit = s.nPathFit;
    fit.bSize = s.bPathSize;
    fit.size[0] = s.pathSize[0];
    fit.size[1] = s.pathSize[1];
    fit.scale = s.pathScale.bSet ? s.pathScale.d : 0.0;
    fit.nSlice = s.nPathSlice;
    memcpy(fit.slice, s.pathSlice, sizeof fit.slice);
    std::vector<float> edges;
    HudPathEdges(*s.pPath, fit, w, h, &edges);
    const size_t n = SDL_min(edges.size() / 4, static_cast<size_t>(HUD_SHAPE_EDGES));
    memcpy(u->uPath, edges.data(), n * 4 * sizeof(float));
    u->uPathN = static_cast<float>(n);
  }
  Set4(u->uProfile, s.nProfile, s.profileSteps, s.profileAt, s.profileWidth);
  Set4(u->uPanel, s.panelLines, s.panelLineInset, s.panelLineGap, s.panelLineWidth);
  Set4(u->uPattern, s.nPattern, s.patternScale != 0 ? s.patternScale : 8, s.patternDepth, s.patternInset);
  Set4(u->uPattern2, s.patternAngle, s.patternWall, 0, 0);
  Set4(u->uScratch, s.scratches, s.scratchScale != 0 ? s.scratchScale : 30, s.scratchAngle, s.scratchWander);
  Set4(u->uShadow, s.castShadow, s.elevation, s.shadowSoft, 0);
  Set4(u->uRelief, s.relief, s.crest, 0, 0);
  Set4(u->uWear, s.edgeWear, s.edgeWearWidth, 0, 0);
  Colour(s.edgeWearColor, "#8f9aa6", u->uWearColor);
  Set4(u->uSheen, s.sheen, 0, 0, 0);
  Colour(s.sheenColor, "#b8c6d4", u->uSheenColor);
}

UiColor Ui(const HudColour &c) { return UiColor{c.c[0], c.c[1], c.c[2], c.bSet ? c.c[3] : 0.0f}; }

// A text layer's glyphs into the UI kit's list, in its buffer's pixels (hudplayer.js, drawText):
// the format's fonts are the ones Hyperion ships, Saira for eurostile and blizzard, Michroma for
// eurostile-ext, as the design center draws them without Remastered's, and Chakra Petch, each by
// its weight (HudFaceOf).
void TextPrims(const HudState &s, double w, double h, double px) {
  if (s.text.empty())
    return;
  const UiFace face = static_cast<UiFace>(HudFaceOf(s.font, s.weight));
  const float fSize = static_cast<float>(s.size * px);
  const float fSpacing = static_cast<float>(s.tracking * px);
  std::vector<std::string> lines;
  const double wrap = s.wrap < 0 ? w : s.wrap;
  if (wrap > 0) {
    size_t at = 0;
    while (at <= s.text.size()) {
      const size_t nl = s.text.find('\n', at);
      const std::string para = s.text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
      std::string cur;
      size_t i = 0;
      while (i < para.size()) {
        while (i < para.size() && isspace(static_cast<unsigned char>(para[i])))
          ++i;
        size_t j = i;
        while (j < para.size() && !isspace(static_cast<unsigned char>(para[j])))
          ++j;
        if (j == i)
          break;
        const std::string word = para.substr(i, j - i);
        const std::string tryS = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && UiTextWidth(face, fSize, fSpacing, tryS.c_str()) > wrap * px) {
          lines.push_back(cur);
          cur = word;
        } else {
          cur = tryS;
        }
        i = j;
      }
      lines.push_back(cur);
      if (nl == std::string::npos)
        break;
      at = nl + 1;
    }
  } else {
    lines.push_back(s.text);
  }
  float fAscent = 0.0f, fDescent = 0.0f, fGap = 0.0f;
  UiFontMetrics(face, fSize, &fAscent, &fDescent, &fGap);
  const double lh = s.size * s.lineHeight, total = lh * lines.size();
  const double y0 = s.nValign == 0 ? lh / 2 : s.nValign == 2 ? h - total + lh / 2 : (h - total) / 2 + lh / 2;
  for (size_t i = 0; i < lines.size(); ++i) {
    const char *psz = lines[i].c_str();
    // canvas's middle baseline: the em box's middle on the line's centre
    const float fBase = static_cast<float>((y0 + i * lh) * px + (fAscent + fDescent) / 2);
    const float fWidth = UiTextWidth(face, fSize, fSpacing, psz);
    const float x = s.nAlign == 1 ? static_cast<float>(w * px - fWidth) / 2
                  : s.nAlign == 2 ? static_cast<float>(w * px) - fWidth : 0.0f;
    if (s.shadow.bSet)
      UiText(x + static_cast<float>(s.shadowOffset[0] * px), fBase + static_cast<float>(s.shadowOffset[1] * px), face,
             fSize, fSpacing, Ui(s.shadow), psz);
    if (s.outline.bSet && s.outlineWidth > 0) {
      // canvas strokes the glyphs' edges; eight copies round them come close
      const float r = static_cast<float>(s.outlineWidth * px);
      for (int k = 0; k < 8; ++k)
        UiText(x + r * cosf(k * 0.785398f), fBase + r * sinf(k * 0.785398f), face, fSize, fSpacing, Ui(s.outline), psz);
    }
    UiText(x, fBase, face, fSize, fSpacing, Ui(s.color), psz);
  }
}

// A rect layer's fill and stroke into the UI kit's list: a fill's first and last stops make
// its gradient, down or across as its angle is nearer.
void RectPrims(const HudState &s, double w, double h, double px) {
  const float W = static_cast<float>(w * px), H = static_cast<float>(h * px), r = static_cast<float>(s.radius * px);
  if (s.nFill == 1) {
    UiFill(0, 0, W, H, UiColor{s.fill[0][0], s.fill[0][1], s.fill[0][2], s.fill[0][3]}, r);
  } else if (s.nFill > 1) {
    const float *a = s.fill[0], *b = s.fill[s.nFill - 1];
    const bool bAcross = fabs(cos(s.fillAngle * kPi / 180.0)) > 0.7071;
    UiGradient(0, 0, W, H, UiColor{a[0], a[1], a[2], a[3]}, UiColor{b[0], b[1], b[2], b[3]}, bAcross, r);
  }
  if (s.nStroke > 0 && s.line > 0)
    UiRing(0, 0, W, H, UiColor{s.stroke[0][0], s.stroke[0][1], s.stroke[0][2], s.stroke[0][3]}, r,
           static_cast<float>(s.line * px));
}

// A shape draws on the clock or reads the screen behind it, so it is drawn again each frame.
bool Live(const HudLayerDef &ld, const HudState &s) {
  return ld.kind == HUD_SHAPE &&
         (s.glass > 0 || s.holo > 0 || s.lines > 0 || s.energy > 0 || (s.wobble > 0 && s.wobbleSpeed > 0) ||
          (s.reveal < 1 && s.nRevealStyle == 1) || s.bSurfaceLive);
}

unsigned long long Mix(unsigned long long h, double d) {
  unsigned char b[sizeof d];
  memcpy(b, &d, sizeof d);
  for (unsigned char c : b)
    h = (h ^ c) * 1099511628211ull;
  return h;
}

// ---- images: an image layer's asset, a PNG or a WebM video of the design's own
// files (its "file"), or 1.17's art from the player's install ("art" sd:, a GRP's frame in the
// game's palette); Remastered's (hd:) is not read yet and draws the plate a missing image draws.
// An asset's "frames" {cols, rows, count, fps, loop}, an engine addition to the format, makes a
// PNG a sheet of frames played from the design's start; a video plays its own frames, in a loop
// unless "frames" says "loop": false. The clock is the HUD's.

struct Image {
  bool bTried = false, bOk = false;
  int w = 0, h = 0;
  std::vector<unsigned char> rgba;  // a picture's pixels, premultiplied, until they are uploaded
  Tex tex;
  PortVideo *pVideo = NULL;
  unsigned nFrames = 0, nShown = ~0u;  // a video's frames, and the one in tex
  double fps = 0.0;
  std::vector<int> art;            // 1.17's art: its palette indices, -1 clear
  unsigned long long nArtPal = 0;  // and the colours they had when rgba was made
};
std::map<std::string, Image> g_images;  // by path

void ImagesFree(void) {
  for (auto &e : g_images) {
    Free(&e.second.tex);
    if (e.second.pVideo != NULL)
      PortVideoClose(e.second.pVideo);
  }
  g_images.clear();
}

void Premultiply(unsigned char *p, size_t nPixels) {
  for (size_t i = 0; i < nPixels; ++i, p += 4) {
    const unsigned a = p[3];
    p[0] = static_cast<unsigned char>((p[0] * a + 127) / 255);
    p[1] = static_cast<unsigned char>((p[1] * a + 127) / 255);
    p[2] = static_cast<unsigned char>((p[2] * a + 127) / 255);
  }
}

// A file read and decoded once: a PNG's pixels, or a video opened.
Image &ImageOf(const std::string &path) {
  Image &im = g_images[path];
  if (im.bTried)
    return im;
  im.bTried = true;
  std::string bytes;
  if (!PortReadFile(path, &bytes) || bytes.size() < 8)
    return im;
  if (memcmp(bytes.data(), "\x1A\x45\xDF\xA3", 4) == 0) {
    // EBML's mark: a WebM video
    im.pVideo = PortVideoOpen(bytes.data(), bytes.size());
    if (im.pVideo == NULL)
      return im;
    im.w = PortVideoWidth(im.pVideo);
    im.h = PortVideoHeight(im.pVideo);
    im.nFrames = PortVideoFrames(im.pVideo);
    const unsigned nInterval = PortVideoInterval(im.pVideo);  // hundredths of a millisecond
    im.fps = nInterval > 0 ? 100000.0 / nInterval : 0.0;
    im.rgba.assign(static_cast<size_t>(im.w) * im.h * 4, 0);
    im.bOk = im.w > 0 && im.h > 0 && im.nFrames > 0;
    return im;
  }
  SDL_IOStream *pIo = SDL_IOFromConstMem(bytes.data(), bytes.size());
  SDL_Surface *pPng = pIo != NULL ? SDL_LoadPNG_IO(pIo, true) : NULL;
  SDL_Surface *pRgba = pPng != NULL ? SDL_ConvertSurface(pPng, SDL_PIXELFORMAT_RGBA32) : NULL;
  if (pPng != NULL)
    SDL_DestroySurface(pPng);
  if (pRgba == NULL)
    return im;
  im.w = pRgba->w;
  im.h = pRgba->h;
  im.rgba.resize(static_cast<size_t>(im.w) * im.h * 4);
  for (int y = 0; y < im.h; ++y)
    memcpy(&im.rgba[static_cast<size_t>(y) * im.w * 4], static_cast<const unsigned char *>(pRgba->pixels) + y * pRgba->pitch,
           static_cast<size_t>(im.w) * 4);
  SDL_DestroySurface(pRgba);
  Premultiply(im.rgba.data(), static_cast<size_t>(im.w) * im.h);
  im.bOk = im.w > 0 && im.h > 0;
  return im;
}

// The colours of the palette's indices a picture uses, as a number that changes with them.
unsigned long long PaletteOf(const std::vector<int> &px, const unsigned char *pRgb) {
  bool abUsed[256] = {};
  for (int v : px)
    if (v >= 0 && v < 256)
      abUsed[v] = true;
  unsigned long long h = 1469598103934665603ull;
  for (int i = 0; i < 256; ++i)
    if (abUsed[i])
      for (int c = 0; c < 3; ++c)
        h = (h ^ pRgb[i * 3 + c]) * 1099511628211ull;
  return h;
}

// Palette indices, -1 clear, as RGBA in a palette.
void ArtColour(const std::vector<int> &px, const unsigned char *pRgb, std::vector<unsigned char> *pOut) {
  pOut->assign(px.size() * 4, 0);
  for (size_t i = 0; i < px.size(); ++i)
    if (px[i] >= 0) {
      memcpy(&(*pOut)[i * 4], &pRgb[px[i] * 3], 3);
      (*pOut)[i * 4 + 3] = 255;
    }
}

// 1.17's art an asset names, "sd:<path>[#n]": frame n of <path>.grp from the game's files, its
// own box, in the game's palette as it is now, clear where the frame draws nothing. Read once,
// and coloured again when the colours of the indices it uses change (a tileset's palette).
Image &ArtOf(const std::string &art) {
  static Image s_none;
  if (art.compare(0, 3, "sd:") != 0)
    return s_none;
  unsigned char rgb[768];
  PortPresentPaletteRGB(rgb);
  Image &im = g_images[art];
  if (im.bTried) {
    if (im.bOk && PaletteOf(im.art, rgb) != im.nArtPal) {
      ArtColour(im.art, rgb, &im.rgba);
      im.nArtPal = PaletteOf(im.art, rgb);
      Free(&im.tex);  // uploaded again as it is drawn
    }
    return im;
  }
  im.bTried = true;
  std::string path = art.substr(3);
  int nFrame = 0;
  const size_t nHash = path.find('#');
  if (nHash != std::string::npos) {
    nFrame = atoi(path.c_str() + nHash + 1);
    path.resize(nHash);
  }
  for (char &c : path)
    c = c == '/' ? '\\' : c;
  path += ".grp";
  void *pData = NULL;
  DWORD cb = 0;
  if (!SFileLoadFile(path.c_str(), &pData, &cb, 0, NULL) || pData == NULL)
    return im;
  const BYTE *p = static_cast<const BYTE *>(pData);
  const int nFrames = cb >= 6 ? p[0] | p[1] << 8 : 0;
  const size_t at = 6 + static_cast<size_t>(nFrame) * 8;
  if (nFrame >= 0 && nFrame < nFrames && at + 8 <= cb) {
    DWORD dwOff;
    memcpy(&dwOff, p + at + 4, sizeof dwOff);
    dwOff &= 0x7FFFFFFFu;  // bit 31 adds 256 to the width; no frame here is that wide
    FRAME f;
    memset(&f, 0, sizeof f);
    f.bWidth = p[at + 2];
    f.bHeight = p[at + 3];
    f.pData = const_cast<BYTE *>(p + dwOff);
    // a frame of runs, as the units' are, starts with its first row's offset, which is past the
    // rows' table; a GRP of whole frames, as game\icons.grp, its pixels, 0 clear (grp_to_bmp)
    WORD wFirst = 0;
    if (dwOff + 2u <= cb)
      memcpy(&wFirst, p + dwOff, sizeof wFirst);
    const size_t nPx = static_cast<size_t>(f.bWidth) * f.bHeight;
    if (f.bWidth > 0 && f.bHeight > 0 && wFirst == 2u * f.bHeight && dwOff + 2u * f.bHeight <= cb) {
      PortGrpFrameDecode(f, &im.art);
    } else if (f.bWidth > 0 && f.bHeight > 0 && dwOff + nPx <= cb) {
      im.art.resize(nPx);
      for (size_t i = 0; i < nPx; ++i)
        im.art[i] = p[dwOff + i] != 0 ? p[dwOff + i] : -1;
    }
    if (!im.art.empty()) {
      im.w = f.bWidth;
      im.h = f.bHeight;
      ArtColour(im.art, rgb, &im.rgba);
      im.nArtPal = PaletteOf(im.art, rgb);
      im.bOk = true;
    }
  }
  SFileUnloadFile(pData);
  return im;
}

// The asset an image layer names, and its image when it is a file of the design's or 1.17's art.
const HudAsset *AssetOf(const HudLayerDef &ld, Image **ppImage) {
  *ppImage = NULL;
  const std::string name = ld.def["image"].Str();
  for (const HudAsset &a : g_pDesign->assets) {
    if (a.name != name)
      continue;
    Image &im = !a.file.empty() ? ImageOf(g_pDesign->files + a.file) : ArtOf(a.art);
    if (im.bOk)
      *ppImage = &im;
    return &a;
  }
  return NULL;
}

// The frame an image shows at the HUD's time t, in seconds.
unsigned ImageFrame(const HudAsset &a, const Image &im, double t) {
  const PortJson &fr = a.def["frames"];
  const bool bLoop = !(fr["loop"].kind == PortJson::BOOL && !fr["loop"].b);
  unsigned n = 1;
  double fps = 0.0;
  if (im.pVideo != NULL) {
    n = im.nFrames;
    fps = im.fps;
  } else if (fr.kind == PortJson::OBJECT) {
    const int nCols = SDL_max(1, static_cast<int>(fr["cols"].Num(1))), nRows = SDL_max(1, static_cast<int>(fr["rows"].Num(1)));
    n = static_cast<unsigned>(fr["count"].Num(nCols * nRows));
    n = SDL_min(n, static_cast<unsigned>(nCols * nRows));
    fps = fr["fps"].Num(12.0);
  }
  if (n <= 1 || fps <= 0.0 || t < 0.0)
    return 0;
  const unsigned f = static_cast<unsigned>(t * fps);
  return bLoop ? f % n : SDL_min(f, n - 1);
}

// The frame an image layer shows now, for its key: a layer whose frame moves is drawn again.
unsigned ImageLayerFrame(const HudLayerDef &ld, double t) {
  Image *pImage = NULL;
  const HudAsset *pAsset = AssetOf(ld, &pImage);
  return pAsset != NULL && pImage != NULL ? ImageFrame(*pAsset, *pImage, t) : 0;
}

// The image's pixels on the GPU: a picture's once, a video's frame when it changes.
bool ImageReady(SDL_GPUCommandBuffer *pCmd, Image *im, unsigned nFrame) {
  if (im->tex.p != NULL && (im->pVideo == NULL || im->nShown == nFrame))
    return true;
  if (im->pVideo != NULL) {
    if (!PortVideoFrame(im->pVideo, nFrame, im->rgba.data(), im->w * 4))
      return im->tex.p != NULL;
    Premultiply(im->rgba.data(), static_cast<size_t>(im->w) * im->h);
    im->nShown = nFrame;
  }
  if (im->tex.p == NULL) {
    im->tex.p = GpuTexture(RGBA8, SDL_GPU_TEXTUREUSAGE_SAMPLER, im->w, im->h);
    im->tex.w = im->w;
    im->tex.h = im->h;
    if (im->tex.p == NULL || im->rgba.empty())
      return false;
  }
  const Uint32 nBytes = static_cast<Uint32>(im->rgba.size());
  SDL_GPUTransferBuffer *pUp = GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, nBytes);
  void *pMapped = pUp != NULL ? SDL_MapGPUTransferBuffer(g_device, pUp, false) : NULL;
  if (pMapped == NULL) {
    if (pUp != NULL)
      SDL_ReleaseGPUTransferBuffer(g_device, pUp);
    return false;
  }
  memcpy(pMapped, im->rgba.data(), nBytes);
  SDL_UnmapGPUTransferBuffer(g_device, pUp);
  SDL_GPUCopyPass *pCopy = SDL_BeginGPUCopyPass(pCmd);
  SDL_GPUTextureTransferInfo src;
  SDL_GPUTextureRegion dst;
  SDL_zero(src);
  SDL_zero(dst);
  src.transfer_buffer = pUp;
  src.pixels_per_row = static_cast<Uint32>(im->w);
  src.rows_per_layer = static_cast<Uint32>(im->h);
  dst.texture = im->tex.p;
  dst.w = static_cast<Uint32>(im->w);
  dst.h = static_cast<Uint32>(im->h);
  dst.d = 1;
  SDL_UploadToGPUTexture(pCopy, &src, &dst, false);
  SDL_EndGPUCopyPass(pCopy);
  SDL_ReleaseGPUTransferBuffer(g_device, pUp);  // freed once the upload is done
  if (im->pVideo == NULL)
    std::vector<unsigned char>().swap(im->rgba);  // a picture's pixels are on the GPU now
  return true;
}

// An image layer into the scratch, a w x h box at margin: the asset stretched to the box, or
// nine-sliced by its "slice" at "scale" units an image pixel; its part the layer's "crop", else
// the asset's, else the whole, and a sheet's frame within it. False when there is no image to
// draw, and the layer is the plate (PlatePrims).
bool DrawImage(SDL_GPUCommandBuffer *pCmd, const HudLayerDef &ld, double w, double h, double px, double margin, int nW,
               int nH, double time) {
  Image *im = NULL;
  const HudAsset *a = AssetOf(ld, &im);
  if (a == NULL || im == NULL)
    return false;
  const unsigned nFrame = ImageFrame(*a, *im, time);
  if (!ImageReady(pCmd, im, nFrame))
    return false;
  double part[4] = {0.0, 0.0, static_cast<double>(im->w), static_cast<double>(im->h)};
  const PortJson &crop = ld.def["crop"].size() >= 4 ? ld.def["crop"] : a->def["crop"];
  if (crop.size() >= 4)
    for (int i = 0; i < 4; ++i)
      part[i] = crop.At(i).Num();
  const PortJson &fr = a->def["frames"];
  if (im->pVideo == NULL && fr.kind == PortJson::OBJECT) {
    const int nCols = SDL_max(1, static_cast<int>(fr["cols"].Num(1))), nRows = SDL_max(1, static_cast<int>(fr["rows"].Num(1)));
    const double cw = part[2] / nCols, ch = part[3] / nRows;
    part[0] += cw * (nFrame % nCols);
    part[1] += ch * (nFrame / nCols % nRows);
    part[2] = cw;
    part[3] = ch;
  }
  SDL_GPUColorTargetInfo target;
  SDL_zero(target);
  target.texture = g_scratch.p;
  target.load_op = SDL_GPU_LOADOP_CLEAR;
  target.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(pCmd, &target, 1, NULL);
  SDL_GPUViewport viewport = {0.0f, 0.0f, static_cast<float>(nW), static_cast<float>(nH), 0.0f, 1.0f};
  SDL_SetGPUViewport(pPass, &viewport);
  SDL_BindGPUGraphicsPipeline(pPass, g_pipes.over);
  SDL_GPUSampler *pSampler = a->def["pixelated"].kind == PortJson::BOOL && a->def["pixelated"].b ? g_nearest : g_linear;
  // a piece: the image's part sx, sy, sw, sh to the box's dx, dy, dw, dh
  auto piece = [&](double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh) {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
      return;
    const float uv[4] = {static_cast<float>(sx / im->w), static_cast<float>(sy / im->h),
                         static_cast<float>((sx + sw) / im->w), static_cast<float>((sy + sh) / im->h)};
    PlacePart(pCmd, pPass, im->tex.p, Mat(dw * px, 0, 0, dh * px, (dx + margin) * px, (dy + margin) * px), nW, nH, 1.0,
              uv, pSampler);
  };
  const PortJson &slice = a->def["slice"];
  if (slice.size() >= 4) {
    const double scale = a->def["scale"].Num(1.0);
    const double l = slice.At(0).Num(), t = slice.At(1).Num(), r = slice.At(2).Num(), b = slice.At(3).Num();
    // the corners keep their size, as much of it as the box has room for
    const double fx = (l + r) * scale > w ? w / ((l + r) * scale) : 1.0, fy = (t + b) * scale > h ? h / ((t + b) * scale) : 1.0;
    const double xs[4] = {0, l * scale * fx, w - r * scale * fx, w}, ys[4] = {0, t * scale * fy, h - b * scale * fy, h};
    const double us[4] = {part[0], part[0] + l, part[0] + part[2] - r, part[0] + part[2]};
    const double vs[4] = {part[1], part[1] + t, part[1] + part[3] - b, part[1] + part[3]};
    for (int j = 0; j < 3; ++j)
      for (int i = 0; i < 3; ++i)
        piece(us[i], vs[j], us[i + 1] - us[i], vs[j + 1] - vs[j], xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j]);
  } else {
    piece(part[0], part[1], part[2], part[3], 0, 0, w, h);
  }
  SDL_EndGPURenderPass(pPass);
  return true;
}

// An image layer with no image: the plate the format draws, #232c36 to #141a21 top to bottom
// with a 1.5-unit #3b4755 edge, unless its asset's "placeholder" is "none".
void PlatePrims(const HudLayerDef &ld, double w, double h, double px, double margin) {
  Image *im = NULL;
  const HudAsset *a = AssetOf(ld, &im);
  if (a != NULL && a->def["placeholder"].Str() == "none")
    return;
  const float x = static_cast<float>(margin * px), y = x, W = static_cast<float>(w * px), H = static_cast<float>(h * px);
  UiGradient(x, y, W, H, UiColor{0x23 / 255.0f, 0x2c / 255.0f, 0x36 / 255.0f, 1.0f},
             UiColor{0x14 / 255.0f, 0x1a / 255.0f, 0x21 / 255.0f, 1.0f}, false);
  UiRing(x, y, W, H, UiColor{0x3b / 255.0f, 0x47 / 255.0f, 0x55 / 255.0f, 1.0f}, 0.0f, static_cast<float>(1.5 * px));
}

// A layer drawn into its buffers.
void DrawLayer(SDL_GPUCommandBuffer *pCmd, const HudDrawItem &it, const HudLayerDef &ld, LayerCache *c, double px,
               double lw, double time, SDL_GPUTexture *pBack) {
  const HudState &s = *it.pState;
  const double w = it.box[2], h = it.box[3], margin = it.margin;
  const int nW = static_cast<int>(ceil((w + 2 * margin) * px)), nH = static_cast<int>(ceil((h + 2 * margin) * px));
  if (!FitAtLeast(&g_scratch, nW, nH) || !Fit(&c->img, nW, nH))
    return;
  // its content
  if (ld.kind == HUD_SHAPE) {
    HudShapeParams u;
    ShapeParams(s, w, h, px, margin, it.screenAt, lw, time, &u);
    Quad(pCmd, g_scratch.p, nW, nH, g_pipes.shape, pBack, g_linear, &u, sizeof u);
  } else if (ld.kind == HUD_IMAGE && DrawImage(pCmd, ld, w, h, px, margin, nW, nH, time)) {
    // drawn into the scratch
  } else {
    UiBegin(nW, nH);
    if (ld.kind == HUD_TEXT)
      TextPrims(s, w, h, px);
    else if (ld.kind == HUD_RECT)
      RectPrims(s, w, h, px);
    else if (ld.kind == HUD_IMAGE)
      PlatePrims(ld, w, h, px, margin);  // its image missing
    // a run is not drawn yet: its buffer stays clear
    int nPrims = 0;
    const UiPrim *pPrims = UiPrims(&nPrims);
    GpuUiDrawInto(pCmd, g_scratch.p, nW, nH, pPrims, nPrims);
  }
  // its effects
  FxParams fx;
  memset(&fx, 0, sizeof fx);
  Set4(fx.box, w, h, margin, 1.0 / px);
  Set4(fx.uv, 0, 0, static_cast<double>(nW) / g_scratch.w, static_cast<double>(nH) / g_scratch.h);
  if (s.tint.bSet)
    memcpy(fx.tint, s.tint.c, sizeof fx.tint);
  else
    Set4(fx.tint, 1, 1, 1, 1);
  Colour(s.addColor, "#ffffff", fx.addColor);
  Colour(s.scanColor, "#9fe8ff", fx.scanColor);
  const double scanAngle = s.scanAngle * kPi / 180.0;
  double off = 0.0;
  if (s.scan.bSet && s.nScanMode == 1) {
    const double dx = cos(scanAngle), dy = sin(scanAngle);
    const double ps[4] = {0, w * dx, h * dy, w * dx + h * dy};
    const double lo = std::min(std::min(ps[0], ps[1]), std::min(ps[2], ps[3]));
    const double hi = std::max(std::max(ps[0], ps[1]), std::max(ps[2], ps[3]));
    off = fmod(s.scan.d * (hi - lo), std::max(2.0, s.scanSpacing));
  }
  Set4(fx.addScan, s.add, s.scan.bSet ? s.scan.d : -2.0, s.scanStrength, s.nScanMode);
  Set4(fx.scanShape, scanAngle, s.scanWidth, s.scanSpacing, off);
  Set4(fx.wipe, s.wipe, s.wipeAngle * kPi / 180.0, s.wipeSoft, 0);
  Set4(fx.meter, s.meter, s.nMeterFrom, 0, 0);
  Quad(pCmd, c->img.p, nW, nH, g_pipes.fx, g_scratch.p, g_nearest, &fx, sizeof fx);
  // its glow: canvas's shadow blur, a Gaussian of half the radius
  c->bGlow = s.glow > 0;
  if (c->bGlow) {
    const double radius = std::max(1.0, s.glowRadius);
    const int g = static_cast<int>(ceil(radius * 2 * px));
    const int nGW = nW + 2 * g, nGH = nH + 2 * g;
    c->nGlowPad = g;
    if (FitAtLeast(&g_scratchGlow, nGW, nGH) && Fit(&c->glow, nGW, nGH)) {
      const double sigma = radius * px / 2;
      const int nTaps = static_cast<int>(std::min(128.0, ceil(sigma * 3)));
      BlurParams b;
      memset(&b, 0, sizeof b);
      Set4(b.step, 1.0 / nW, 0, sigma, nTaps);
      Set4(b.uv, -static_cast<double>(g) / nW, -static_cast<double>(g) / nH, 1.0 + static_cast<double>(g) / nW,
           1.0 + static_cast<double>(g) / nH);
      Set4(b.valid, 0, 0, 1, 1);
      Quad(pCmd, g_scratchGlow.p, nGW, nGH, g_pipes.blur, c->img.p, g_nearest, &b, sizeof b);
      const double u1 = static_cast<double>(nGW) / g_scratchGlow.w, v1 = static_cast<double>(nGH) / g_scratchGlow.h;
      Set4(b.step, 0, 1.0 / g_scratchGlow.h, sigma, nTaps);
      Set4(b.uv, 0, 0, u1, v1);
      Set4(b.valid, 0, 0, u1, v1);
      Colour(s.glowColor, "#5fd0ff", b.color);
      Quad(pCmd, c->glow.p, nGW, nGH, g_pipes.blur, g_scratchGlow.p, g_nearest, &b, sizeof b);
    } else {
      c->bGlow = false;
    }
  }
  // its contact shadow
  c->bShadow = ld.kind == HUD_SHAPE && s.castShadow > 0 && Fit(&c->shadow, nW, nH);
  if (c->bShadow) {
    HudShapeParams u;
    ShapeParams(s, w, h, px, margin, it.screenAt, lw, time, &u);
    Quad(pCmd, c->shadow.p, nW, nH, g_pipes.shadow, pBack, g_linear, &u, sizeof u);
  }
}

// The design's runs and parts, and its panels' order.
void Structure(const HudDesign *pDesign) {
  g_pDesign = pDesign;
  ImagesFree();
  for (Run &r : g_runs)
    Free(&r.tex);
  g_runs.clear();
  for (auto &e : g_layers) {
    Free(&e.second.img);
    Free(&e.second.glow);
    Free(&e.second.shadow);
  }
  g_layers.clear();
  g_parts.assign(pDesign->panels.size(), {});
  g_layerRun.assign(pDesign->panels.size(), {});
  for (size_t p = 0; p < pDesign->panels.size(); ++p) {
    const HudPanelDef &pd = pDesign->panels[p];
    int nRun = -1;
    g_layerRun[p].assign(pd.layers.size(), -1);
    for (size_t l = 0; l < pd.layers.size(); ++l) {
      const HudLayerDef &ld = pd.layers[l];
      if (ld.kind == HUD_SLOT) {
        g_parts[p].push_back({-1, static_cast<int>(l)});
        nRun = -1;
        continue;
      }
      // additive layers get a run of their own, so a run never mixes the two blends
      const bool bAdd = ld.def["blend"].Str() == "add";
      if (nRun < 0 || g_runs[nRun].bAdd != bAdd) {
        Run r;
        r.nPanel = static_cast<int>(p);
        r.bAdd = bAdd;
        g_runs.push_back(r);
        nRun = static_cast<int>(g_runs.size()) - 1;
        g_parts[p].push_back({nRun, -1});
      }
      g_layerRun[p][l] = nRun;
    }
  }
  // a panel's "z" stacks it over or under the others, which are in file order
  g_order.resize(pDesign->panels.size());
  for (size_t p = 0; p < g_order.size(); ++p)
    g_order[p] = static_cast<int>(p);
  std::stable_sort(g_order.begin(), g_order.end(), [&](int a, int b) {
    const HudPanelDef &pa = pDesign->panels[a], &pb = pDesign->panels[b];
    return (pa.bZ ? pa.z : 0.0) < (pb.bZ ? pb.z : 0.0);
  });
}

// The screen behind glass at half the frame's size, blurred in passes levels.
SDL_GPUTexture *Backdrop(SDL_GPUCommandBuffer *pCmd, int nWidth, int nHeight, int nPasses,
                         const std::function<void(SDL_GPUTexture *, int, int)> &drawFrame) {
  nPasses = SDL_clamp(nPasses, 1, 6);
  int w = SDL_max(1, nWidth / 2), h = SDL_max(1, nHeight / 2);
  if (!Fit(&g_back[0], w, h, g_swapFormat))
    return NULL;
  drawFrame(g_back[0].p, w, h);
  if (nPasses == 1)
    return g_back[0].p;
  int aw[7] = {w}, ah[7] = {h};
  for (int i = 1; i < nPasses; ++i) {
    aw[i] = SDL_max(1, aw[i - 1] / 2);
    ah[i] = SDL_max(1, ah[i - 1] / 2);
    if (!Fit(&g_back[i], aw[i], ah[i]))
      return g_back[0].p;
    KawaseParams k = {{1.0f / aw[i - 1], 1.0f / ah[i - 1], 0.0f, 0.0f}};
    Quad(pCmd, g_back[i].p, aw[i], ah[i], g_pipes.down, g_back[i - 1].p, g_linear, &k, sizeof k);
  }
  // back up, the last into a level 0 of its own (g_back[6] at level 0's size)
  for (int i = nPasses - 1; i > 0; --i) {
    Tex *pTo = i == 1 ? &g_back[6] : &g_back[i - 1];
    if (!Fit(pTo, aw[i - 1], ah[i - 1]))
      return g_back[0].p;
    KawaseParams k = {{1.0f / aw[i], 1.0f / ah[i], 0.0f, 0.0f}};
    Quad(pCmd, pTo->p, aw[i - 1], ah[i - 1], g_pipes.up, g_back[i].p, g_linear, &k, sizeof k);
  }
  return g_back[6].p;
}


// ---------------------------------------------------------------------------
// The slots (H2): what 1.17's console shows, read from its dialogs' bitmaps (port_console.h,
// PortConsoleHudSource) onto a sheet in the UI kit's picture atlas, and the readouts the design
// center's page puts in a slot (hud.html's .shp-slot rules, hud.js's fillSlot), in the style's
// colours as its dressSlots gives them; drawn with the UI kit in each slot's place between the
// panels' runs. Alerts and chat are the game's own text over the map, and the time is the
// overlay's (the user, 2026-10-22), so those slots stay empty.

// The slots left out: the tab's picks while its picture is drawn (GpuHudPreview), else the game's.
const std::vector<bool> *g_pHiddenNow;
bool SlotHidden(int nSlot) {
  if (g_pHiddenNow != NULL)
    return nSlot >= 0 && static_cast<size_t>(nSlot) < g_pHiddenNow->size() && (*g_pHiddenNow)[nSlot];
  return PortHudSlotHidden(nSlot);
}

const int kSheetSlot = 34;  // the picture atlas's slot (port_ui.h)
const int kSheet = UI_PICTURE_SIDE;

struct SheetPart {
  int x = 0, y = 0, w = 0, h = 0;
  bool bOk = false;
};
std::vector<unsigned char> g_sheet, g_sheetShown;  // RGBA, kSheet square
SheetPart g_shMinimap, g_shPortrait, g_shSelection, g_shCard[9], g_shRes[3], g_shWire[2];

// A source's pixels through the palette onto the sheet at (x, y), at most w x h of them; with
// bClear the dialog's 0, where it drew nothing, is left clear.
SheetPart Sheet(int nSource, int nIndex, int x, int y, int w, int h, bool bClear, const unsigned char *pRgb) {
  SheetPart part;
  part.x = x;
  part.y = y;
  PortHudPixels px;
  if (!PortConsoleHudSource(nSource, nIndex, &px))
    return part;
  part.w = std::min(w, px.nWidth);
  part.h = std::min(h, px.nHeight);
  for (int r = 0; r < part.h; ++r) {
    const unsigned char *pIn = px.pPixels + static_cast<size_t>(r) * px.nPitch;
    unsigned char *pOut = &g_sheet[(static_cast<size_t>(y + r) * kSheet + x) * 4];
    for (int c = 0; c < part.w; ++c, pOut += 4) {
      const unsigned char i = pIn[c];
      if (i == 0 && bClear) {
        pOut[0] = pOut[1] = pOut[2] = pOut[3] = 0;
        continue;
      }
      pOut[0] = pRgb[i * 3];
      pOut[1] = pRgb[i * 3 + 1];
      pOut[2] = pRgb[i * 3 + 2];
      pOut[3] = 255;
    }
  }
  part.bOk = part.w > 0 && part.h > 0;
  return part;
}

// The progress bar's frame over its fill, where a full picture leaves it out
// (PortConsoleHudBarEdge), in the palette's colours; SelectionAccent then colours it as the rest.
void SelectionBarEdge(const SheetPart &part, const unsigned char *pRgb) {
  static std::vector<int> s_edge;
  int x0 = 0, y0 = 0, w = 0, h = 0;
  if (!part.bOk || !PortConsoleHudBarEdge(&s_edge, &x0, &y0, &w, &h))
    return;
  for (int r = 0; r < h; ++r)
    for (int c = 0; c < w; ++c) {
      const int v = s_edge[static_cast<size_t>(r) * w + c];
      if (v < 0 || x0 + c < 0 || y0 + r < 0 || x0 + c >= part.w || y0 + r >= part.h)
        continue;
      unsigned char *p = &g_sheet[(static_cast<size_t>(part.y + y0 + r) * kSheet + part.x + x0 + c) * 4];
      memcpy(p, &pRgb[v * 3], 3);
      p[3] = 255;
    }
}

// The selection's unit boxes and one unit's slot frames in the player's colour (Settings' HUD
// tab): each box's pixel in the accent, as bright against the box's brightest as it was, so the
// box's shading stays.
void SelectionAccent(const SheetPart &part) {
  float accent[3];
  PortHudPixels px;
  if (!part.bOk || !PortHudSelectionAccent(accent) || !PortConsoleHudSource(PORT_HUD_SRC_SELECTION, 0, &px))
    return;
  static std::vector<unsigned char> s_mask;
  s_mask.assign(static_cast<size_t>(px.nWidth) * px.nHeight, 0);
  if (!PortConsoleHudSelectionBoxes(s_mask.data(), px.nWidth, px.nHeight))
    return;
  auto luma = [](const unsigned char *p) { return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]; };
  float most = 1.0f;
  for (int r = 0; r < part.h; ++r)
    for (int c = 0; c < part.w; ++c)
      if (s_mask[static_cast<size_t>(r) * px.nWidth + c])
        most = std::max(most, luma(&g_sheet[(static_cast<size_t>(part.y + r) * kSheet + part.x + c) * 4]));
  for (int r = 0; r < part.h; ++r) {
    for (int c = 0; c < part.w; ++c) {
      if (!s_mask[static_cast<size_t>(r) * px.nWidth + c])
        continue;
      unsigned char *p = &g_sheet[(static_cast<size_t>(part.y + r) * kSheet + part.x + c) * 4];
      const float k = luma(p) / most;
      for (int i = 0; i < 3; ++i)
        p[i] = static_cast<unsigned char>(std::min(255.0f, accent[i] * 255.0f * k + 0.5f));
    }
  }
}

// The sheet as the console shows it now: the minimap, the portrait, the counter's icons and the
// idle and army buttons' wireframes along the top; the command buttons in a row below; the
// selection's panel at the foot. Put in the atlas when it changed.
void SheetUpdate(void) {
  g_sheet.assign(static_cast<size_t>(kSheet) * kSheet * 4, 0);
  unsigned char rgb[768];
  PortPresentPaletteRGB(rgb);
  g_shMinimap = Sheet(PORT_HUD_SRC_MINIMAP, 0, 0, 0, 128, 128, false, rgb);
  g_shPortrait = Sheet(PORT_HUD_SRC_PORTRAIT, 0, 128, 0, 64, 64, false, rgb);
  for (int i = 0; i < 3; ++i)
    g_shRes[i] = Sheet(PORT_HUD_SRC_RESOURCE, i, 192 + 16 * i, 0, 16, 16, true, rgb);
  for (int i = 0; i < 9; ++i)
    g_shCard[i] = Sheet(PORT_HUD_SRC_CARD, i, 42 * i, 128, 42, 40, true, rgb);
  g_shSelection = Sheet(PORT_HUD_SRC_SELECTION, 0, 0, 256, kSheet, kSheet - 256, true, rgb);
  SelectionBarEdge(g_shSelection, rgb);
  SelectionAccent(g_shSelection);
  // the SCV's and the Marine's wireframes, white, for the idle and army buttons
  const int anWire[2] = {7, 0};
  for (int i = 0; i < 2; ++i) {
    SheetPart &w = g_shWire[i];
    w.x = 256 + 64 * i;
    w.y = 0;
    w.w = w.h = 64;
    w.bOk = PortWireframeCell(anWire[i], &g_sheet[static_cast<size_t>(w.x) * 4], kSheet, 64);
  }
  if (g_sheet != g_sheetShown) {
    UiPictureSet(kSheetSlot, g_sheet.data(), kSheet, kSheet);
    g_sheetShown = g_sheet;
  }
}

// A slot's units to the screen's pixels, and what it is drawn at.
struct SlotAt {
  Mat m;           // the slot's units, (0,0) its box's top left, to the target's pixels
  double k = 1.0;  // the target's pixels a unit
  double w = 0, h = 0;
  float fAlpha = 1.0f;
};

SDL_FRect Rect(const SlotAt &at, double x, double y, double w, double h) {
  const double ax[4] = {x, x + w, x, x + w}, ay[4] = {y, y, y + h, y + h};
  double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
  for (int i = 0; i < 4; ++i) {
    const double px = at.m.m[0] * ax[i] + at.m.m[2] * ay[i] + at.m.m[4];
    const double py = at.m.m[1] * ax[i] + at.m.m[3] * ay[i] + at.m.m[5];
    x0 = std::min(x0, px), y0 = std::min(y0, py), x1 = std::max(x1, px), y1 = std::max(y1, py);
  }
  return SDL_FRect{static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1 - x0),
                   static_cast<float>(y1 - y0)};
}

UiColor Alpha(UiColor c, float a) {
  c.a *= a;
  return c;
}

// The style's colour by its palette name, or the page's own when the style has none.
UiColor Dress(const HudDesign *pDesign, const char *pszName, const char *pszDefault) {
  float rgba[4] = {1, 1, 1, 1};
  const auto it = pDesign->palette.find(pszName);
  if (it == pDesign->palette.end() || !HudParseColour(it->second, rgba))
    HudParseColour(pszDefault, rgba);
  return UiColor{rgba[0], rgba[1], rgba[2], rgba[3]};
}

// The button a slot is (port_hudstate.h's PORT_HUD_BTN_*), or -1: one the game does not show
// draws nothing and takes no press.
int SlotButton(int nSlot) {
  switch (nSlot) {
  case HUD_SLOT_MENU:
    return PORT_HUD_BTN_MENU;
  case HUD_SLOT_TERRAIN:
    return PORT_HUD_BTN_TERRAIN;
  case HUD_SLOT_DIPLOMACY:
    return PORT_HUD_BTN_DIPLOMACY;
  case HUD_SLOT_MESSAGING:
    return PORT_HUD_BTN_MESSAGING;
  case HUD_SLOT_IDLE:
    return PORT_HUD_BTN_IDLE;
  case HUD_SLOT_ARMY:
    return PORT_HUD_BTN_ARMY;
  case HUD_SLOT_PLAY:
    return PORT_HUD_BTN_PLAY;
  case HUD_SLOT_SLOWER:
    return PORT_HUD_BTN_SLOWER;
  case HUD_SLOT_FASTER:
    return PORT_HUD_BTN_FASTER;
  case HUD_SLOT_TV:
    return PORT_HUD_BTN_TV;
  case HUD_SLOT_FOLLOW:
    return PORT_HUD_BTN_FOLLOW;
  }
  return -1;
}

// A button with an action is shown as its slot would be: left out in Settings, or one the game
// does not show (the army under the classic rules), it draws nothing and takes no press. As
// the format's own hudActionShown: idle, army and the groups in a game the player
// plays, the replay's position and vision in a replay, the stats where they may show.
bool ActionShown(const HudLayerDef &ld, const PortHudState &st) {
  if (ld.nAction < 0 || ld.nAction == HUD_ACT_UNKNOWN)
    return true;
  switch (ld.nAction) {
    case HUD_ACT_IDLE:
    case HUD_ACT_ARMY:
    case HUD_ACT_GROUP:
      if (st.bReplay)
        return false;
      break;
    case HUD_ACT_REPLAY_SEEK:
    case HUD_ACT_VISION:
    case HUD_ACT_VISION_SEAT:
      return st.bReplay;
    case HUD_ACT_STATS_POPUP:
    case HUD_ACT_STATS_SIDE:
    case HUD_ACT_STATS_PAGE:
      return PortHudStatsAllowed();
  }
  const int nSlot = HudActionSlot(ld.nAction), nButton = SlotButton(nSlot);
  return !SlotHidden(nSlot) && (nButton < 0 || st.abButton[nButton]);
}

// A button shown greyed, at 40%, which takes no press: a name the engine does not know, "vision"
// with no row's player, or one its "enabled" turns off.
bool ActionGreyed(const HudLayerDef &ld, const HudState &s) {
  return ld.nAction == HUD_ACT_UNKNOWN || ld.nAction == HUD_ACT_VISION || (ld.nAction >= 0 && !s.bEnabled);
}

// A sheet's part fitted whole in a box of the slot's units, in its middle, a 1.17 pixel a
// square of the screen's.
void Picture(const SlotAt &at, const SheetPart &part, double x, double y, double w, double h, float fAlpha) {
  if (!part.bOk || w <= 0 || h <= 0)
    return;
  double fit[4];
  HudFit(part.w, part.h, x, y, w, h, fit);
  const SDL_FRect r = Rect(at, fit[0], fit[1], fit[2], fit[3]);
  UiPicturePart(r.x, r.y, r.w, r.h, kSheetSlot, static_cast<float>(part.x), static_cast<float>(part.y),
                static_cast<float>(part.x + part.w), static_cast<float>(part.y + part.h),
                UiColor{1, 1, 1, fAlpha * at.fAlpha}, true);
}

// Text in the slot's units: its size and spacing in units, its left at x (or centred on x with
// nAlign 1, right at x with 2), its middle on y.
float Text(const SlotAt &at, UiFace face, double size, double spacing, UiColor c, const std::string &text, double x,
           double y, int nAlign) {
  if (text.empty() || size <= 0)
    return 0.0f;
  const float fSize = static_cast<float>(size * at.k), fSpacing = static_cast<float>(spacing * at.k);
  const float fWidth = UiTextWidth(face, fSize, fSpacing, text.c_str());
  float fAscent = 0, fDescent = 0, fGap = 0;
  UiFontMetrics(face, fSize, &fAscent, &fDescent, &fGap);
  const SDL_FRect r = Rect(at, x, y, 0, 0);
  const float fX = nAlign == 1 ? r.x - fWidth / 2 : nAlign == 2 ? r.x - fWidth : r.x;
  UiText(fX, r.y + (fAscent + fDescent) / 2, face, fSize, fSpacing, Alpha(c, at.fAlpha), text.c_str());
  return fWidth / static_cast<float>(at.k);
}

double TextWidth(const SlotAt &at, UiFace face, double size, double spacing, const std::string &text) {
  return UiTextWidth(face, static_cast<float>(size * at.k), static_cast<float>(spacing * at.k), text.c_str()) / at.k;
}

// A slot's places, its own "places" or else n of the box across.
std::vector<std::array<double, 4>> Places(const HudLayerDef &ld, const SlotAt &at, int n) {
  std::vector<std::array<double, 4>> out;
  for (const PortJson &q : ld.def["places"].items)
    if (q.size() >= 4)
      out.push_back({q.At(0).Num(), q.At(1).Num(), q.At(2).Num(), q.At(3).Num()});
  for (int i = static_cast<int>(out.size()); i < n; ++i)
    out.push_back({at.w * i / n, 0, at.w / n, at.h});
  return out;
}

std::string Upper(std::string s) {
  return UiUpper(s.c_str());
}

// Text cut to a width, with an ellipsis, as the page's text-overflow cuts it.
std::string Elide(const SlotAt &at, UiFace face, double size, double spacing, const std::string &text, double most) {
  if (TextWidth(at, face, size, spacing, text) <= most)
    return text;
  std::string s = text;
  while (!s.empty()) {
    s.pop_back();
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80)
      s.pop_back();  // the rest of a UTF-8 letter
    if (TextWidth(at, face, size, spacing, s + "\xE2\x80\xA6") <= most)
      return s + "\xE2\x80\xA6";
  }
  return std::string();
}

// ---- a replay's (docs/hud-format.md; "Replays"), as the design center's page draws
// them (hud.js's buttonFace and drawBoard)

// A triangle with an upright base at x from y0 to y1 and its point at px, halfway down: two right
// triangles, each clipped to its box with its slant the edge of a turned fill, so the slant is
// smooth. In the target's pixels.
void Arrow(float x, float y0, float y1, float px, UiColor c) {
  const float ym = (y0 + y1) / 2, left = std::min(x, px), right = std::max(x, px);
  for (int half = 0; half < 2; ++half) {
    const float sy = half == 0 ? y0 : y1, dx = px - x, dy = ym - sy, len = sqrtf(dx * dx + dy * dy);
    if (len <= 0.0f)
      continue;
    // the fill's edge on the slant, the fill toward the right angle at (x, ym)
    const float mx = (x + px) / 2, my = (sy + ym) / 2;
    float nx = -dy / len, ny = dx / len;
    if ((x - mx) * nx + (ym - my) * ny < 0)
      nx = -nx, ny = -ny;
    const float t = len * 2, cx = mx + nx * t / 2, cy = my + ny * t / 2;
    UiPushClip(left, half == 0 ? y0 : ym, right, half == 0 ? ym : y1);
    UiSetTurn(atan2f(dy, dx), cx, cy);
    UiFill(cx - len, cy - t / 2, len * 2, t, c);
    UiSetTurn(0.0f, 0.0f, 0.0f);
    UiPopClip();
  }
}

// A replay button's glyph on a 24-unit square (hud.js's: play, pause, slower, faster), 0.46 of
// the button's height, in its middle or at the side its align names.
void Glyph(const SlotAt &at, int nGlyph, double w, double h, const std::string &align, UiColor c) {
  const double g = h * 0.46, pad = h * 0.22;
  const double gx = align == "left" ? pad : align == "right" ? w - pad - g : (w - g) / 2;
  const SDL_FRect r = Rect(at, gx, (h - g) / 2, g, g);
  const float kx = r.w / 24.0f, ky = r.h / 24.0f;
  auto X = [&](float u) { return r.x + u * kx; };
  auto Y = [&](float v) { return r.y + v * ky; };
  c = Alpha(c, at.fAlpha);
  switch (nGlyph) {
  case 0:  // play
    Arrow(X(7), Y(4.5f), Y(19.5f), X(19), c);
    break;
  case 1:  // pause
    UiFill(X(6.5f), Y(4.5f), 4 * kx, 15 * ky, c);
    UiFill(X(13.5f), Y(4.5f), 4 * kx, 15 * ky, c);
    break;
  case 2:  // slower
    Arrow(X(12), Y(5.5f), Y(18.5f), X(3), c);
    Arrow(X(21), Y(5.5f), Y(18.5f), X(12), c);
    break;
  default:  // faster
    Arrow(X(3), Y(5.5f), Y(18.5f), X(12), c);
    Arrow(X(12), Y(5.5f), Y(18.5f), X(21), c);
    break;
  }
}

// The players' board: a row a player on the page's dark glass, the player's colour at its outer
// edge, the name and race, the figures, and the eye, lit while the viewer sees what the player sees;
// a player not seen dims.
void BoardPrims(const HudDesign *pDesign, const HudLayerDef &ld, const SlotAt &at, const PortHudState &st) {
  std::vector<HudBoardRow> rows;
  HudBoardOf(ld.def, at.w, at.h, st, &rows);
  const UiColor text = Dress(pDesign, "text", "#e4eef6"), figure = Dress(pDesign, "figure", "#f2f8ff");
  const UiColor label = Dress(pDesign, "label", "#8fa3b4"), lamp = Dress(pDesign, "lamp", "#8fe3ff");
  // the race's one letter beside a name
  static const char *const kRace[3] = {PTC_("race letter", "Z"), PTC_("race letter", "T"), PTC_("race letter", "P")};
  for (const HudBoardRow &r : rows) {
    if (r.nPlayer < 0) {
      // the players past those that fit, in the stats popup
      const SDL_FRect q = Rect(at, r.row[0], r.row[1], r.row[2], r.row[3]);
      UiFill(q.x, q.y, q.w, q.h, UiColor{6 / 255.0f, 8 / 255.0f, 10 / 255.0f, 0.5f * at.fAlpha});
      // {0} how many players the board has no room for
      Text(at, UI_FACE_TEXT, r.size, r.size * 0.08, lamp,
           PortTextF("+{0} more \xC2\xB7 F1", {std::to_string(r.nMore)}), r.row[0] + r.row[2] / 2,
           r.row[1] + r.row[3] / 2, 1);
      continue;
    }
    const PortHudPlayer &p = st.aPlayers[r.nPlayer];
    const float fDim = p.bSeen ? 1.0f : 0.5f;
    SDL_FRect q = Rect(at, r.row[0], r.row[1], r.row[2], r.row[3]);
    UiFill(q.x, q.y, q.w, q.h, UiColor{6 / 255.0f, 8 / 255.0f, 10 / 255.0f, 0.5f * at.fAlpha});
    q = Rect(at, r.swatch[0], r.swatch[1], r.swatch[2], r.swatch[3]);
    UiFill(q.x, q.y, q.w, q.h, UiRgb(p.nColour, at.fAlpha));
    // the name, toward the middle on the right
    const double size = r.size, spacing = size * 0.04;
    const std::string name = Elide(at, UI_FACE_BOLD, size, spacing,
                                   std::string(p.szName) + " " + PortTextC("race letter", kRace[p.nRace]), r.name[2]);
    Text(at, UI_FACE_BOLD, size, spacing, Alpha(figure, fDim), name, r.bRight ? r.name[0] + r.name[2] : r.name[0],
         r.name[1] + r.name[3] / 2, r.bRight ? 2 : 0);
    // the figures across their box, each its letter keySize and its figure
    const std::string vals[5] = {std::to_string(p.nMinerals), std::to_string(p.nGas),
                                 std::to_string(p.nSupplyUsed) + "/" + std::to_string(p.nSupplyMax),
                                 std::to_string(p.nWorkers), std::to_string(p.nApm)};
    // the one letter before each figure: minerals, gas, supply, workers
    static const char *const kKeys[5] = {PTC_("figure letter", "M"), PTC_("figure letter", "G"),
                                         PTC_("figure letter", "S"), PTC_("figure letter", "W"), "APM"};
    const double keySize = size * 0.72, keySp = keySize * 0.08, inner = size * 0.3;
    double widths[5], total = 0;
    for (int i = 0; i < 5; ++i)
      total += widths[i] = TextWidth(at, UI_FACE_TEXT, keySize, keySp, PortTextC("figure letter", kKeys[i])) + inner +
                           TextWidth(at, UI_FACE_TEXT, size, 0, vals[i]);
    const double step = std::max(6.0, (r.figures[2] - total) / 4);
    double x = r.figures[0];
    const double y = r.figures[1] + r.figures[3] / 2;
    for (int i = 0; i < 5; ++i) {
      const double wk =
          Text(at, UI_FACE_TEXT, keySize, keySp, Alpha(label, fDim), PortTextC("figure letter", kKeys[i]), x, y, 0);
      Text(at, UI_FACE_TEXT, size, 0, Alpha(text, fDim), vals[i], x + wk + inner, y, 0);
      x += widths[i] + step;
    }
    // the eye: an almond and its pupil, in the light while seen; none past the eighth player, whose
    // vision has no switch
    if (!p.bEye)
      continue;
    q = Rect(at, r.eye[0] + 2, r.eye[1] + 2, r.eye[2] - 4, r.eye[3] - 4);
    HudStatsEye(q.x, q.y, q.w, p.bSeen ? Alpha(lamp, at.fAlpha) : Alpha(label, 0.55f * at.fAlpha));
  }
}

// ---- the selection's grid (docs/hud-format.md; "The selection's pages"): a page of the
// console's units, each as 1.17's panel draws one (statdata_select_btn_draw), its box by the
// unit's state and its wireframe in its hit points' and shields' colours, on a sheet of their
// own in the picture atlas: the boxes along the top, each frame plain and lighter, the lighter
// the console's subgroup's when the selection has more than one kind (the user, 2026-10-23); the
// page's wireframes below, a 32-pixel cell each. The boxes of units in their normal state take
// the selection's colour the player picked, as the panel's do (SelectionAccent).

const int kGridSlot = 36;  // the picture atlas's slots 36 to 39 (port_ui.h)
const int kBoxFrames[4] = {0x0D, 0x13, 0x19, 0x1F};  // StatDataBoxFrame's
const int kBoxW = 34, kBoxH = 34, kWireCell = 32, kWireTop = 36;
// four rows of 48 wireframes below the boxes: 192, the most a page of the format holds (64 by 3)
const int kGridW = 4 * UI_PICTURE_SIDE, kGridH = kWireTop + 4 * kWireCell;
std::vector<unsigned char> g_grid, g_gridShown;  // RGBA, kGridW x kGridH
struct GridBox {
  int w = 0, h = 0;
  bool bOk = false;
} g_gridBoxes[4];
struct GridWire {
  int w = 0, h = 0;
  bool bOk = false;
};
std::vector<GridWire> g_gridWires;  // the page's units'

// Palette indices onto the grid's sheet at x, y, through rgb; bAccent recolours them in the
// player's selection colour, as bright against their brightest as they were; bLight lightens
// them (PortConsoleLighten).
void GridPut(const std::vector<int> &px, int w, int h, int x, int y, const unsigned char *pRgb, const float *pAccent,
             bool bLight) {
  auto luma = [](const unsigned char *p) { return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]; };
  float most = 1.0f;
  if (pAccent != NULL)
    for (int v : px)
      if (v >= 0)
        most = std::max(most, luma(&pRgb[v * 3]));
  for (int r = 0; r < h && y + r < kGridH; ++r)
    for (int c = 0; c < w && x + c < kGridW; ++c) {
      const int v = px[static_cast<size_t>(r) * w + c];
      if (v < 0)
        continue;
      unsigned char *q = &g_grid[(static_cast<size_t>(y + r) * kGridW + x + c) * 4];
      memcpy(q, &pRgb[v * 3], 3);
      if (pAccent != NULL) {
        const float k = luma(q) / most;
        for (int i = 0; i < 3; ++i)
          q[i] = static_cast<unsigned char>(std::min(255.0f, pAccent[i] * 255.0f * k + 0.5f));
      }
      if (bLight)
        PortConsoleLighten(q);
      q[3] = 255;
    }
}

// The sheet for a page: the boxes, and the units from nFirst, nCount of them.
void GridSheet(int nFirst, int nCount) {
  g_grid.assign(static_cast<size_t>(kGridW) * kGridH * 4, 0);
  unsigned char rgb[768];
  PortPresentPaletteRGB(rgb);
  float accent[3];
  const bool bAccent = PortHudSelectionAccent(accent);
  std::vector<int> px;
  for (int b = 0; b < 4; ++b) {
    GridBox &box = g_gridBoxes[b];
    int fw = 0, fh = 0;
    box.bOk = PortConsoleHudBox(kBoxFrames[b], &px, &fw, &fh);
    box.w = std::min(fw, kBoxW);
    box.h = std::min(fh, kBoxH);
    if (!box.bOk)
      continue;
    // only a box in its normal state takes the player's colour; the others say something
    const float *pAccent = bAccent && b == 0 ? accent : NULL;
    for (int bLight = 0; bLight < 2; ++bLight) {
      std::vector<int> cut(static_cast<size_t>(box.w) * box.h);
      for (int r = 0; r < box.h; ++r)
        for (int c = 0; c < box.w; ++c)
          cut[static_cast<size_t>(r) * box.w + c] = px[static_cast<size_t>(r) * fw + c];
      GridPut(cut, box.w, box.h, (b * 2 + bLight) * kBoxW, 0, rgb, pAccent, bLight != 0);
    }
  }
  g_gridWires.assign(static_cast<size_t>(std::max(0, nCount)), GridWire());
  const int nAcross = kGridW / kWireCell;
  for (int i = 0; i < nCount && nFirst + i < SelectionMax(); ++i) {
    GridWire &wire = g_gridWires[i];
    CUnit *pUnit = StatUnits()[nFirst + i];
    if (!PortConsoleHudWire(pUnit, &px, &wire.w, &wire.h) || wire.w > kWireCell || wire.h > kWireCell)
      continue;
    wire.bOk = true;
    GridPut(px, wire.w, wire.h, (i % nAcross) * kWireCell, kWireTop + (i / nAcross) * kWireCell, rgb, NULL, false);
  }
  if (g_grid != g_gridShown) {
    UiPictureSet(kGridSlot, g_grid.data(), kGridW, kGridH);
    g_gridShown = g_grid;
  }
}

// The grid in the slot, false when the layer has none or the console shows one unit or none:
// then 1.17's panel is shown, with a unit's figures, its queue or its cargo.
bool GridPrims(const HudDesign *pDesign, const HudLayerDef &ld, const SlotAt &at, const PortHudState &st) {
  HudGrid g;
  const int n = st.nSelected;
  if (n < 2 || !HudGridOf(ld.def["grid"], at.w, at.h, n, &g))
    return false;
  const int nPage = PortHudGridPage(g.per, n), nFirst = nPage * g.per;
  const int nCount = std::min(g.per, n - nFirst);
  const int nAcross = kGridW / kWireCell;
  if (nCount > (kGridH - kWireTop) / kWireCell * nAcross)
    return false;  // more a page than the sheet holds
  GridSheet(nFirst, nCount);
  const bool bKinds = SelectionSubgroups() > 1;
  for (int i = 0; i < nCount; ++i) {
    CUnit *pUnit = StatUnits()[nFirst + i];
    if (pUnit == NULL)
      continue;
    double q[4], fit[4];
    g.Cell(i, q);
    int b = 0;
    while (b < 3 && kBoxFrames[b] != StatDataBoxFrame(pUnit))
      ++b;
    const GridBox &box = g_gridBoxes[b];
    // the box fitted in the cell, a unit's margin round it, and the wireframe in its middle at
    // the box's scale, as the panel centres both in a button
    HudFit(box.bOk ? box.w : 33, box.bOk ? box.h : 34, q[0] + 1, q[1] + 1, q[2] - 2, q[3] - 2, fit);
    const double s = fit[2] / (box.bOk ? box.w : 33);
    if (box.bOk) {
      const int bLight = bKinds && gpStatUnit != NULL && pUnit->wType == gpStatUnit->wType ? 1 : 0;
      const SDL_FRect r = Rect(at, fit[0], fit[1], fit[2], fit[3]);
      const float u0 = static_cast<float>((b * 2 + bLight) * kBoxW);
      UiPicturePart(r.x, r.y, r.w, r.h, kGridSlot, u0, 0.0f, u0 + box.w, static_cast<float>(box.h),
                    UiColor{1, 1, 1, at.fAlpha}, true);
    }
    const GridWire &wire = g_gridWires[i];
    if (wire.bOk) {
      const double ww = wire.w * s, wh = wire.h * s;
      const SDL_FRect r = Rect(at, fit[0] + (fit[2] - ww) / 2, fit[1] + (fit[3] - wh) / 2, ww, wh);
      const float u0 = static_cast<float>((i % nAcross) * kWireCell), v0 = static_cast<float>(kWireTop + (i / nAcross) * kWireCell);
      UiPicturePart(r.x, r.y, r.w, r.h, kGridSlot, u0, v0, u0 + wire.w, v0 + wire.h, UiColor{1, 1, 1, at.fAlpha}, true);
    }
  }
  // the page tabs, the page shown lit, in the style's cells' colours
  if (g.bTabs) {
    const UiColor lcd = Dress(pDesign, "lcd", "#c4f4ff"), off = Dress(pDesign, "lcdOff", "#a6e6f8");
    for (int p = 0; p < g.pages; ++p) {
      double q[4];
      g.Tab(p, q);
      const bool bOn = p == nPage;
      const SDL_FRect r = Rect(at, q[0], q[1], q[2], q[3]);
      UiFill(r.x, r.y, r.w, r.h, Alpha(lcd, (bOn ? 0.2f : 0.04f) * at.fAlpha));
      UiRing(r.x, r.y, r.w, r.h, Alpha(lcd, (bOn ? 0.75f : 0.22f) * at.fAlpha), 0.0f, static_cast<float>(at.k));
      Text(at, UI_FACE_BOLD, std::min(15.0, q[3] * 0.62), 0, bOn ? lcd : Alpha(off, 0.6f), std::to_string(p + 1),
           q[0] + q[2] / 2, q[1] + q[3] / 2, 1);
    }
  }
  return true;
}

// A slot's content into the UI kit's list.
void SlotPrims(const HudDesign *pDesign, const HudLayerDef &ld, const SlotAt &at, const PortHudState &st) {
  if (SlotHidden(ld.nSlot))
    return;  // left out in Settings
  const UiColor text = Dress(pDesign, "text", "#e4eef6"), figure = Dress(pDesign, "figure", "#f2f8ff");
  const UiColor label = Dress(pDesign, "label", "#8fc4dc"), alert = Dress(pDesign, "alert", "#ff3a2a");
  const bool bMech = pDesign->palette.count("lcd") != 0;  // the mech styles dress the slots
  const UiColor lcd = Dress(pDesign, "lcd", "#c4f4ff");
  const UiColor lcdOff = Alpha(Dress(pDesign, "lcdOff", "#a6e6f8"), bMech ? 0.55f : 1.0f);
  const double w = at.w, h = at.h;
  switch (ld.nSlot) {
  case HUD_SLOT_MINIMAP:
    Picture(at, g_shMinimap, 0, 0, w, h, 1.0f);
    break;
  case HUD_SLOT_PORTRAIT:
    Picture(at, g_shPortrait, 0, 0, w, h, 1.0f);
    break;
  case HUD_SLOT_SELECTION:
    if (!GridPrims(pDesign, ld, at, st))
      Picture(at, g_shSelection, 0, 0, w, h, 1.0f);
    break;
  case HUD_SLOT_CARD: {
    // each of 1.17's nine places in the design's well for it
    const std::vector<std::array<double, 4>> places = Places(ld, at, 9);
    for (int i = 0; i < 9 && i < static_cast<int>(places.size()); ++i) {
      const std::array<double, 4> &q = places[i];
      Picture(at, g_shCard[i], q[0] + q[2] * 0.06, q[1] + q[3] * 0.06, q[2] * 0.88, q[3] * 0.88, 1.0f);
    }
    break;
  }
  case HUD_SLOT_RESOURCES: {
    // minerals, gas and supply a cell each: the race's icon, then its figure, the supply's lit
    const std::vector<std::array<double, 4>> places = Places(ld, at, 3);
    const int anValue[2] = {st.nMinerals, st.nGas};
    for (int i = 0; i < 3; ++i) {
      const std::array<double, 4> &q = places[i];
      const double ch = q[3], icon = ch * 0.6;
      Picture(at, g_shRes[i], q[0] + ch * 0.2, q[1] + (ch - icon) / 2, icon, icon, 1.0f);
      const std::string fig = i < 2 ? std::to_string(anValue[i])
                                    : std::to_string(st.nSupplyUsed) + "/" + std::to_string(st.nSupplyMax);
      const UiColor c = i < 2 ? text : st.nSupplyUsed > st.nSupplyMax ? alert : figure;
      const double x = q[0] + ch * 0.2 + icon + ch * 0.25;
      double size = 22;  // stepped down until the cell holds it, as the page does
      while (size > 11 && x + TextWidth(at, UI_FACE_TEXT, size, 0, fig) > q[0] + q[2])
        size -= 1;
      Text(at, UI_FACE_TEXT, size, 0, c, fig, x, q[1] + ch / 2, 0);
    }
    break;
  }
  case HUD_SLOT_APM: {
    const double size = h * 0.52, sizeK = size * 0.6, gap = h * 0.2;
    const std::string k = "APM", n = std::to_string(st.nApm);
    const double wk = TextWidth(at, UI_FACE_TEXT, sizeK, sizeK * 0.06, k), wn = TextWidth(at, UI_FACE_TEXT, size, 0, n);
    const double x = (w - wk - gap - wn) / 2;
    Text(at, UI_FACE_TEXT, sizeK, sizeK * 0.06, label, k, x, h / 2, 0);
    Text(at, UI_FACE_TEXT, size, 0, text, n, x + wk + gap, h / 2, 0);
    break;
  }
  case HUD_SLOT_IDLE:
  case HUD_SLOT_ARMY: {
    // the worker's or the Marine's wireframe and the count, faint at none
    const bool bIdle = ld.nSlot == HUD_SLOT_IDLE;
    if (!st.abButton[SlotButton(ld.nSlot)])
      break;
    const int n = bIdle ? st.nIdleWorkers : st.nArmy;
    const float fAlpha = n > 0 ? 1.0f : 0.45f;
    const double icon = std::min(h * 0.92, bMech ? 24.0 : 30.0), size = bMech ? 16 : 18, gap = 6;
    const std::string fig = std::to_string(n);
    const double total = icon + gap + TextWidth(at, UI_FACE_TEXT, size, 0, fig), x = (w - total) / 2;
    const SheetPart &wire = g_shWire[bIdle ? 0 : 1];
    if (wire.bOk) {
      const SDL_FRect r = Rect(at, x, (h - icon) / 2, icon, icon);
      UiPicturePart(r.x, r.y, r.w, r.h, kSheetSlot, static_cast<float>(wire.x), static_cast<float>(wire.y),
                    static_cast<float>(wire.x + wire.w), static_cast<float>(wire.y + wire.h),
                    UiColor{0.24f, 0.83f, 0.24f, fAlpha * at.fAlpha});
    }
    Text(at, UI_FACE_TEXT, size, 0, Alpha(text, fAlpha), fig, x + icon + gap, h / 2, 0);
    break;
  }
  case HUD_SLOT_GROUPS: {
    // the ten wells: the key, and the count once the group has units; the last recalled lit
    const std::vector<std::array<double, 4>> places = Places(ld, at, 10);
    for (int i = 0; i < 10 && i < static_cast<int>(places.size()); ++i) {
      const std::array<double, 4> &q = places[i];
      const int g = (i + 1) % 10, n = st.anGroups[g];
      const bool bHas = n > 0, bLast = bHas && st.nGroupLast == g;
      if (bLast) {
        const SDL_FRect r = Rect(at, q[0], q[1], q[2], q[3]);
        UiFill(r.x, r.y, r.w, r.h, Alpha(lcd, 0.16f * at.fAlpha));
      }
      const double size = bHas ? (bMech ? 13 : 15) : (bMech ? 17 : 19);
      Text(at, UI_FACE_BOLD, size, 0, bHas ? lcd : lcdOff, std::to_string(g), q[0] + q[2] / 2,
           q[1] + q[3] * (bHas ? 0.34 : 0.5), 1);
      if (bHas)
        Text(at, UI_FACE_BOLD, 13, 0, lcd, std::to_string(n), q[0] + q[2] - 6, q[1] + q[3] - 3 - 13 * 0.5, 2);
    }
    break;
  }
  case HUD_SLOT_MENU:
  case HUD_SLOT_TERRAIN:
  case HUD_SLOT_DIPLOMACY:
  case HUD_SLOT_MESSAGING: {
    // 1.17's button, shown by its rules, in its own words
    const int nButton = SlotButton(ld.nSlot);
    if (!st.abButton[nButton])
      break;
    char sz[64];
    std::string face = PortConsoleHudLabel(nButton, sz, sizeof sz) > 0 ? std::string(sz) : std::string();
    if (nButton == PORT_HUD_BTN_DIPLOMACY && st.bReplay)
      face = PortText("View Players");  // as its tooltip and its dialog have it in a replay
    if (face.empty())
      face = nButton == PORT_HUD_BTN_MENU        ? PortText("Menu")
             : nButton == PORT_HUD_BTN_TERRAIN   ? PortText("Terrain")
             : nButton == PORT_HUD_BTN_DIPLOMACY ? PortText("Diplomacy")
                                                 : PortText("Messaging");
    const bool bMenu = nButton == PORT_HUD_BTN_MENU;
    const double size = h * (bMenu ? 0.52 : 0.4), spacing = size * (bMenu ? 0.1 : 0.05);
    const std::string align = ld.def["align"].Str();
    const int nAlign = align == "right" ? 2 : align == "left" ? 0 : 1;
    const double x = nAlign == 2 ? w - h * (bMenu ? 0.62 : 0.22) : nAlign == 0 ? h * 0.22 : w / 2;
    Text(at, UI_FACE_TEXT, size, spacing, Alpha(text, 0.78f), Upper(face), x, h / 2, nAlign);
    break;
  }
  case HUD_SLOT_PLAY:
  case HUD_SLOT_SLOWER:
  case HUD_SLOT_FASTER:
  case HUD_SLOT_TV:
  case HUD_SLOT_FOLLOW: {
    // a replay's buttons: the transport's glyphs and Replay TV's and Follow's words, in the
    // buttons' colour, dim while greyed and in the light while on
    const int nButton = SlotButton(ld.nSlot);
    if (!st.abButton[nButton])
      break;
    UiColor c = st.abButtonLit[nButton] ? Dress(pDesign, "lamp", "#8fe3ff") : Alpha(text, 0.78f);
    if (st.abButtonOff[nButton])
      c = Alpha(c, 0.32f);
    const std::string align = ld.def["align"].Str();
    if (ld.nSlot == HUD_SLOT_TV || ld.nSlot == HUD_SLOT_FOLLOW) {
      const double size = h * 0.4;
      const int nAlign = align == "right" ? 2 : align == "left" ? 0 : 1;
      const double x = nAlign == 2 ? w - h * 0.22 : nAlign == 0 ? h * 0.22 : w / 2;
      Text(at, UI_FACE_TEXT, size, size * 0.05, c, ld.nSlot == HUD_SLOT_TV ? "TV" : PortText("FOLLOW"), x, h / 2,
           nAlign);
      break;
    }
    Glyph(at, ld.nSlot == HUD_SLOT_PLAY ? (st.bPaused ? 0 : 1) : ld.nSlot == HUD_SLOT_SLOWER ? 2 : 3, w, h, align, c);
    break;
  }
  case HUD_SLOT_PLAYERS:
    BoardPrims(pDesign, ld, at, st);
    break;
  case HUD_SLOT_STATS: {
    // a view of the players' stats, drawn by port_hudstats.cpp in the slot's box
    int nView = 0;
    double rowH[2], backing = 0;
    HudStatsSlot(ld.def, &nView, rowH, &backing);
    HudStatsAt sa;
    sa.x = at.m.m[4], sa.y = at.m.m[5], sa.k = at.m.m[0], sa.w = at.w, sa.h = at.h, sa.fAlpha = at.fAlpha;
    const HudStatsLook look = {text, figure, label, Dress(pDesign, "lamp", "#8fe3ff")};
    HudStatsDraw(nView, sa, look, backing, rowH, st);
    break;
  }
  default:
    break;
  }
}

// The design pPlayer plays, drawn into pTarget, nWidth x nHeight: the swapchain (bSwap), else a
// texture of RGBA8 (Settings' HUD tab's picture); the screen is box. st is what its slots show,
// nKey the design's loads, so its structure is made again when they change, and bHits keeps the
// layers drawn for the mouse (the game's HUD). drawFrame draws the screen behind it again into a
// texture of the swapchain's format, w x h, for a design's glass.
void Draw(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pTarget, bool bSwap, Uint32 nWidth, Uint32 nHeight,
          const SDL_FRect &box, HudPlayer *pPlayer, const HudEnv &env, const PortHudState &st, unsigned long long nKey,
          bool bHits, const std::function<void(SDL_GPUTexture *, int, int)> &drawFrame) {
  const HudDesign *pDesign = pPlayer->Design();
  if (pDesign != g_pDesign || nKey != g_nStructKey) {
    g_nStructKey = nKey;
    Structure(pDesign);
  }
  ++g_nFrame;
  if (g_white.p != NULL && g_nFrame == 1) {
    SDL_GPUColorTargetInfo t;
    SDL_zero(t);
    t.texture = g_white.p;
    t.load_op = SDL_GPU_LOADOP_CLEAR;
    t.clear_color.r = t.clear_color.g = t.clear_color.b = t.clear_color.a = 1.0f;
    t.store_op = SDL_GPU_STOREOP_STORE;
    SDL_EndGPURenderPass(SDL_BeginGPURenderPass(pCmd, &t, 1, NULL));
  }
  const std::vector<HudDrawItem> &items = pPlayer->Frame(env);
  const std::vector<HudPanelFrame> &panels = pPlayer->PanelFrames();
  const double k = box.h / SCREEN;     // the window's pixels a unit
  const double px = std::min(3.0, k);  // a layer's buffer's
  const double lw = pPlayer->LW();
  const double time = pPlayer->Clock() / 1000.0;
  // the layers as drawn, panel by panel in their order, for the mouse (port_hudinput.cpp); the
  // slots that only show the game's own text over the map take none
  const PortHudState &shown = st;
  if (bHits) {
    std::vector<HudHit> hits;
    for (int p : g_order) {
      // a music add-on's plates are its windows', which take their own mouse
      if (!panels[p].bVisible || panels[p].opacity < 0.05 || pDesign->panels[p].bMusic)
        continue;
      for (const HudDrawItem &it : items) {
        if (it.nPanel != p || it.opacity < 0.05 || it.box[2] <= 0 || it.box[3] <= 0)
          continue;
        const HudLayerDef &ld = pDesign->panels[p].layers[it.nLayer];
        const int nSlot = it.kind == HUD_SLOT ? ld.nSlot : -1;
        if (nSlot == HUD_SLOT_ALERTS || nSlot == HUD_SLOT_CHAT || nSlot == HUD_SLOT_TIME || PortHudSlotHidden(nSlot) ||
            (SlotButton(nSlot) >= 0 && !shown.abButton[SlotButton(nSlot)]) || !ActionShown(ld, shown))
          continue;
        const double *m = it.m, det = m[0] * m[3] - m[1] * m[2];
        if (fabs(det) < 1e-9)
          continue;
        HudHit h;
        memcpy(h.m, m, sizeof h.m);
        h.inv[0] = m[3] / det;
        h.inv[1] = -m[1] / det;
        h.inv[2] = -m[2] / det;
        h.inv[3] = m[0] / det;
        h.inv[4] = -(h.inv[0] * m[4] + h.inv[2] * m[5]);
        h.inv[5] = -(h.inv[1] * m[4] + h.inv[3] * m[5]);
        h.w = it.box[2];
        h.h = it.box[3];
        h.nSlot = nSlot;
        h.nAction = nSlot < 0 && !ActionGreyed(ld, *it.pState) ? ld.nAction : -1;
        h.nActionArg = ld.nActionArg;
        h.nMeterFrom = it.pState->nMeterFrom;
        h.label = it.pState->label;
        h.nPanel = p;
        h.pLayer = &ld;
        hits.push_back(h);
      }
    }
    PortHudHitsSet(std::move(hits), lw);
  }
  SDL_GPUTexture *pBack = g_white.p;
  if (pDesign->bGlass && drawFrame) {
    SDL_GPUTexture *p = Backdrop(pCmd, static_cast<int>(box.w), static_cast<int>(box.h), pDesign->nGlassPasses, drawFrame);
    if (p != NULL)
      pBack = p;
  }
  // the layers whose pixels changed, drawn again; each run's layers listed
  for (auto &e : g_layers)
    e.second.bSeen = false;
  std::vector<std::vector<int>> runItems(g_runs.size());
  std::vector<unsigned long long> itemKey(items.size(), 0);
  std::vector<std::vector<int>> slotItem(pDesign->panels.size());  // per panel and layer: a slot's item, -1 none
  for (size_t p = 0; p < slotItem.size(); ++p)
    slotItem[p].assign(pDesign->panels[p].layers.size(), -1);
  int nPrevPanel = -1, nPrevLayer = -1, nTile = 0;
  for (size_t i = 0; i < items.size(); ++i) {
    const HudDrawItem &it = items[i];
    nTile = it.nPanel == nPrevPanel && it.nLayer == nPrevLayer ? nTile + 1 : 0;
    nPrevPanel = it.nPanel;
    nPrevLayer = it.nLayer;
    const int nRun = g_layerRun[it.nPanel][it.nLayer];
    if (nRun < 0) {
      slotItem[it.nPanel][it.nLayer] = static_cast<int>(i);  // a slot: drawn below (SlotPrims)
      continue;
    }
    const HudLayerDef &ld = pDesign->panels[it.nPanel].layers[it.nLayer];
    if (!ActionShown(ld, shown))
      continue;
    unsigned long long key = HudStateHash(*it.pState, false);
    key = Mix(Mix(Mix(key, it.box[2]), it.box[3]), it.margin);
    key = Mix(key, px);
    if (ld.kind == HUD_SHAPE)
      key = Mix(Mix(Mix(key, it.screenAt[0]), it.screenAt[1]), lw);
    if (Live(ld, *it.pState))
      key = Mix(key, static_cast<double>(g_nFrame));
    if (ld.kind == HUD_IMAGE)
      key = Mix(key, static_cast<double>(ImageLayerFrame(ld, time)));
    const unsigned long long id = (static_cast<unsigned long long>(it.nPanel) << 40) |
                                  (static_cast<unsigned long long>(it.nLayer) << 20) | static_cast<unsigned long long>(nTile);
    LayerCache &c = g_layers[id];
    c.bSeen = true;
    if (c.key != key || c.img.p == NULL) {
      DrawLayer(pCmd, it, ld, &c, px, lw, time, pBack);
      c.key = key;
    }
    itemKey[i] = id;
    runItems[nRun].push_back(static_cast<int>(i));
  }
  // the runs whose layers or their placing changed, laid again
  for (size_t r = 0; r < g_runs.size(); ++r) {
    Run &run = g_runs[r];
    const HudPanelDef &pd = pDesign->panels[run.nPanel];
    double rect[4];
    pPlayer->PanelRect(run.nPanel, rect);
    const int nRW = static_cast<int>(ceil((rect[2] + 2 * PAD) * px)), nRH = static_cast<int>(ceil((rect[3] + 2 * PAD) * px));
    unsigned long long key = Mix(Mix(1469598103934665603ull, px), static_cast<double>(nRW * 65536 + nRH));
    for (int i : runItems[r]) {
      const HudDrawItem &it = items[i];
      key = Mix(key, static_cast<double>(g_layers[itemKey[i]].key));
      for (double v : it.ml)
        key = Mix(key, v);
      key = Mix(key, it.layerOpacity);
    }
    if (run.key == key && run.tex.p != NULL)
      continue;
    run.key = key;
    if (!Fit(&run.tex, nRW, nRH))
      continue;
    SDL_GPUColorTargetInfo target;
    SDL_zero(target);
    target.texture = run.tex.p;
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(pCmd, &target, 1, NULL);
    const Mat R(px, 0, 0, px, px * PAD, px * PAD);
    for (int i : runItems[r]) {
      const HudDrawItem &it = items[i];
      const HudState &s = *it.pState;
      const LayerCache &c = g_layers[itemKey[i]];
      if (c.img.p == NULL)
        continue;
      const Mat L(it.ml);
      const double margin = it.margin;
      const Mat toImg = R * L * Mat(c.img.w / px, 0, 0, c.img.h / px, -margin, -margin);
      // a button greyed: an action the engine does not know, or one turned off
      const double alpha = it.layerOpacity * (ActionGreyed(pd.layers[it.nLayer], s) ? 0.4 : 1.0);
      auto glow = [&]() {
        const double g = c.nGlowPad / px;
        const Mat toGlow = R * L * Mat(c.glow.w / px, 0, 0, c.glow.h / px, -margin - g, -margin - g);
        SDL_BindGPUGraphicsPipeline(pPass, g_pipes.add);
        for (double n = s.glow; n > 0; n -= 1)
          Place(pCmd, pPass, c.glow.p, toGlow, nRW, nRH, alpha * std::min(1.0, n));
      };
      if (c.bGlow && s.nGlowMode == 0)
        glow();
      if (c.bShadow) {
        SDL_BindGPUGraphicsPipeline(pPass, g_pipes.atop);
        Place(pCmd, pPass, c.shadow.p, toImg, nRW, nRH,
              alpha * SDL_clamp(s.wipe, 0.0, 1.0) * SDL_clamp(s.reveal, 0.0, 1.0));
      }
      SDL_BindGPUGraphicsPipeline(pPass, s.nBlend == 1 ? g_pipes.add : g_pipes.over);
      Place(pCmd, pPass, c.img.p, toImg, nRW, nRH, alpha);
      if (c.bGlow && s.nGlowMode == 1)
        glow();
    }
    SDL_EndGPURenderPass(pPass);
    (void)pd;
  }
  // the buffers of layers gone
  for (auto e = g_layers.begin(); e != g_layers.end();) {
    if (!e->second.bSeen) {
      Free(&e->second.img);
      Free(&e->second.glow);
      Free(&e->second.shadow);
      e = g_layers.erase(e);
    } else {
      ++e;
    }
  }
  // the slots' content, each a part of one list of the UI kit's
  const Mat K(k, 0, 0, k, box.x, box.y);
  std::vector<std::vector<std::array<int, 2>>> slotPart(pDesign->panels.size());
  std::vector<UiPrim> slotPrims;
  {
    SheetUpdate();
    UiBegin(static_cast<int>(nWidth), static_cast<int>(nHeight));
    for (int p : g_order) {
      slotPart[p].assign(pDesign->panels[p].layers.size(), {0, 0});
      for (const Part &part : g_parts[p]) {
        const int i = part.nRun < 0 ? slotItem[p][part.nLayer] : -1;
        if (i < 0)
          continue;
        const HudDrawItem &it = items[i];
        SlotAt at;
        at.m = K * Mat(it.m);
        at.k = k;
        at.w = it.box[2];
        at.h = it.box[3];
        at.fAlpha = static_cast<float>(it.opacity);
        int n0 = 0, n1 = 0;
        UiPrims(&n0);
        SlotPrims(pDesign, pDesign->panels[p].layers[part.nLayer], at, st);
        UiPrims(&n1);
        slotPart[p][part.nLayer] = {n0, n1 - n0};
      }
    }
    int n = 0;
    const UiPrim *pPrims = UiPrims(&n);
    slotPrims.assign(pPrims, pPrims + n);
  }
  const bool bSlots = GpuUiStage(pCmd, slotPrims.data(), static_cast<int>(slotPrims.size()), bSwap);
  // the runs and the slots on the screen, panel by panel
  SDL_GPUColorTargetInfo target;
  SDL_zero(target);
  target.texture = pTarget;
  target.load_op = SDL_GPU_LOADOP_LOAD;
  target.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(pCmd, &target, 1, NULL);
  for (int p : g_order) {
    const HudPanelFrame &pf = panels[p];
    if (!pf.bVisible)
      continue;
    const Mat P(pf.m);
    for (const Part &part : g_parts[p]) {
      if (part.nRun < 0) {
        if (bSlots)
          GpuUiDrawPart(pCmd, pPass, nWidth, nHeight, slotPart[p][part.nLayer][0], slotPart[p][part.nLayer][1], bSwap);
        continue;
      }
      const Run &run = g_runs[part.nRun];
      if (run.tex.p == NULL)
        continue;
      const Mat m = K * P * Mat(run.tex.w / px, 0, 0, run.tex.h / px, -PAD, -PAD);
      SDL_BindGPUGraphicsPipeline(pPass, run.bAdd ? (bSwap ? g_pipes.addSwap : g_pipes.add)
                                                  : (bSwap ? g_pipes.overSwap : g_pipes.over));
      Place(pCmd, pPass, run.tex.p, m, static_cast<int>(nWidth), static_cast<int>(nHeight), pf.opacity);
    }
  }
  SDL_EndGPURenderPass(pPass);
}

// Settings' HUD tab's map picture, on the GPU while the tab shows it (GpuHudPreview).
Image g_previewMap;
unsigned long long g_nPreviewMapKey;

}  // namespace

void GpuHudDraw(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pSwap, Uint32 nWidth, Uint32 nHeight, const SDL_FRect &box,
                const std::function<void(SDL_GPUTexture *, int, int)> &drawFrame) {
  if (box.w < 1.0f || box.h < 1.0f)
    return;
  const HudEnv *pEnv = NULL;
  HudPlayer *pPlayer = PortHudFrameBegin(static_cast<int>(box.w), static_cast<int>(box.h), &pEnv);
  if (pPlayer == NULL || !Init())
    return;
  PortHudState st;
  PortHudGet(&st);
  Draw(pCmd, pSwap, true, nWidth, nHeight, box, pPlayer, *pEnv, st, PortHudLoads(), true, drawFrame);
}

void GpuHudPreview(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pTarget, Uint32 nWidth, Uint32 nHeight,
                   const PortHudPreview &p) {
  if (p.w < 1.0f || p.h < 1.0f || p.pPlayer == NULL || p.pPlayer->Design() == NULL || p.pEnv == NULL ||
      p.pState == NULL || !Init())
    return;
  // the map's picture, sent up again when it changes
  if (p.pMap != NULL && p.nMapW > 0 && p.nMapH > 0 && (g_previewMap.tex.p == NULL || p.nMapKey != g_nPreviewMapKey)) {
    Free(&g_previewMap.tex);
    g_previewMap.w = p.nMapW;
    g_previewMap.h = p.nMapH;
    g_previewMap.rgba.assign(p.pMap, p.pMap + static_cast<size_t>(p.nMapW) * p.nMapH * 4);
    g_previewMap.bOk = true;
    g_nPreviewMapKey = p.nMapKey;
    if (!ImageReady(pCmd, &g_previewMap, 0))
      Free(&g_previewMap.tex);
  }
  const SDL_FRect box = {p.x, p.y, p.w, p.h};
  // the picture into a target, w x h, over all of it at at
  auto picture = [&](SDL_GPUTexture *pTo, int w, int h, SDL_GPUGraphicsPipeline *pPipe, const SDL_FRect &at, bool bClear) {
    SDL_GPUColorTargetInfo t;
    SDL_zero(t);
    t.texture = pTo;
    t.clear_color.a = 1.0f;
    t.load_op = bClear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    t.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(pCmd, &t, 1, NULL);
    if (g_previewMap.tex.p != NULL) {
      SDL_BindGPUGraphicsPipeline(pPass, pPipe);
      Place(pCmd, pPass, g_previewMap.tex.p, Mat(at.w, 0, 0, at.h, at.x, at.y), w, h, 1.0);
    }
    SDL_EndGPURenderPass(pPass);
  };
  picture(pTarget, static_cast<int>(nWidth), static_cast<int>(nHeight), g_pipes.over, box, false);
  static const std::vector<bool> s_none;
  g_pHiddenNow = p.pHidden != NULL ? p.pHidden : &s_none;
  Draw(pCmd, pTarget, false, nWidth, nHeight, box, p.pPlayer, *p.pEnv, *p.pState, (1ull << 40) | p.nKey, false,
       [&](SDL_GPUTexture *pTo, int w, int h) {
         const SDL_FRect whole = {0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};
         picture(pTo, w, h, g_pipes.overSwap, whole, true);
       });
  g_pHiddenNow = NULL;
}

void GpuHudRelease(void) {
  SDL_GPUGraphicsPipeline **const apPipes[] = {&g_pipes.shape, &g_pipes.shadow, &g_pipes.fx, &g_pipes.blur,
                                               &g_pipes.down, &g_pipes.up, &g_pipes.over, &g_pipes.add,
                                               &g_pipes.atop, &g_pipes.overSwap, &g_pipes.addSwap};
  for (SDL_GPUGraphicsPipeline **pp : apPipes) {
    if (*pp != NULL)
      SDL_ReleaseGPUGraphicsPipeline(g_device, *pp);
    *pp = NULL;
  }
  for (Run &r : g_runs)
    Free(&r.tex);
  g_runs.clear();
  for (auto &e : g_layers) {
    Free(&e.second.img);
    Free(&e.second.glow);
    Free(&e.second.shadow);
  }
  g_layers.clear();
  for (Tex &t : g_back)
    Free(&t);
  Free(&g_scratch);
  Free(&g_scratchGlow);
  Free(&g_white);
  Free(&g_previewMap.tex);
  g_previewMap = Image();
  g_pDesign = NULL;
  g_bTried = g_bReady = false;
  g_nFrame = 0;
}
