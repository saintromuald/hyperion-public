// port_gpu_internal.h -- what the parts of the GPU presenter share (port_gpu.cpp and
// port_gpu_fx.cpp, port_gpu_hd.cpp, port_gpu_scene.cpp, port_gpu_ui.cpp, port_gpu_video.cpp): the types and
// uniform blocks more than one part uses, the state more than one part reads, and the
// functions one part calls in another. The rest of each part's state, and its own types,
// are in its own file. Presentation only.
#pragma once
#include <windows.h>
#include <stdio.h>
#include "port_gpu.h"
#include "port_scene.h"
#include <array>
#include <functional>
#include <unordered_map>
#include <vector>
#include "../tools/port_control.h"
#include "port_hd.h"
#include "port_lights.h"
#include "../platform/port_settings.h"
#include "../front/port_ui.h"
#include "../front/port_world.h"

// The build writes each shader's MSL (tools/shader_msl.py), which only a Mac's build includes.
#if defined(__APPLE__)
#define GPU_MSL(name) k##name##Msl
#else
#define GPU_MSL(name) NULL
#endif
// A shader's code as GpuShader and GpuComputePipeline take it.
#define GPU_CODE(name) k##name##Dxil, sizeof k##name##Dxil, k##name##Spv, sizeof k##name##Spv, GPU_MSL(name)

// The device and the window (port_gpu.cpp).
extern SDL_GPUDevice *g_device;
extern SDL_Window *g_window;
extern bool g_claimed;
extern SDL_GPUTextureFormat g_swapFormat;
// The overlay over a game's frame (port_overlay.cpp), drawn into the swapchain after it.
void GpuUiOverlay(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pSwap, Uint32 nWidth, Uint32 nHeight, const SDL_FRect &box);
extern SDL_GPUGraphicsPipeline *g_composeFullPipeline;  // the full colour's (stage G)
extern SDL_GPUSampler *g_nearest;
extern SDL_GPUSampler *g_linear;
// Sized to the screen, and made again when the view changes size.
extern SDL_GPUTexture *g_frame;  // the resolved frame, RGBA8
extern SDL_GPUBuffer *g_cover;  // the screen's cover, a byte a pixel
extern int g_nWidth;
extern int g_nHeight;
// All of a uniform block SDL's Vulkan binds (MAX_UBO_SECTION_SIZE): past it a shader reads
// zeros, so a larger one is split into blocks pushed to slots of their own. The shaders' side
// is tools/shader_layout.py's, which the build runs.
enum { GPU_UNIFORM_BLOCK_MAX = 4096 };

// The effects after the scene (shaders/effects.hlsl; port_gpu_fx.cpp):
// what the presenter draws them with, the bloom's first level on the way back up, and the
// frame with its effects at the game's size, made only for a snapshot.
enum { BLOOM_LEVELS = 5 };
extern SDL_GPUGraphicsPipeline *g_postPipeline;  // into the swapchain
extern SDL_GPUGraphicsPipeline *g_postSnapPipeline;  // into g_post
extern SDL_GPUTexture *g_bloomUp[BLOOM_LEVELS - 1];
extern SDL_GPUTexture *g_post;
extern SDL_GPUTransferBuffer *g_postDown;
// The 2D lights (port_lights.cpp): the light buffer at the bloom's first
// level's size.
extern SDL_GPUTexture *g_lightTex;

// effects.hlsl's PostParams.
struct PostParams {
  float fDstX, fDstY, fDstW, fDstH;
  float fTexW, fTexH, fHasCover, fBloom;
  float fExposure, fContrast, fSaturation, fTemperature;
  float fTint, fVignette, fVignetteSoft, fCoverScale;
  float fLightsOn, fLightStrength, fAmbient, fLightAdd;
  float fMoved, fHudShift, fHudAlpha, fHasGround;  // the map moved: GpuMapMoved
  float afMapAt[4], afFrameAt[4], afLightAt[4];
  float fHasHud, fArtSoft, afUnused[2];
};

// The frame in HD (port_hd.cpp; shaders/hd.hlsl; port_gpu_hd.cpp): at the
// scene's last stage the frame is drawn again at the HD art's scale, the resolved frame
// scaled up with each primitive of an image whose art is on the GPU drawn over it from
// that art, in the game's order; those primitives are left out of the GPU's map, so the
// SD frame under them does not show. The frame with its effects, and a snapshot, are
// taken from it.
extern SDL_GPUTexture *g_hdFrame;
extern SDL_GPUTexture *g_hdPost;  // with its effects, for a snapshot
// The map alone in HD at a skirmish's start, the console's place too, which the presenter
// shows where the console has risen from (GpuHdDrawFrame with bGround).
extern SDL_GPUTexture *g_hdGround;
bool GpuHdGroundFit(void);
// The blend between SD and HD (F5): how far it is, 0 SD to 1 HD, moving toward hd's value
// over hd.fade seconds; while it is between, the scene is drawn whole too (PortGpuScene),
// its copy of the screen kept in g_hdShownSd, and the whole SD frame made from it
// (g_hdFrameSd) is laid over the HD frame as the blend says.
extern float g_fHdMix;
extern float g_fHdFade;
extern bool g_bHdMixed;  // the scene this frame was drawn whole too
// The scene was drawn for the frame in HD, its frames drawn in HD left out, so the frame
// must be drawn in HD to show them. Set with the blend, which moves once a frame, when the
// scene is drawn (GpuHdScene): the frame then follows what the scene did, never the blend
// moved on since, which once showed a frame without its units as the blend reached SD.
extern bool g_bHdScene;
extern Uint32 g_nHdOverFrom;  // the first primitive after every one drawn in HD (scene.hlsl)
extern Uint32 g_nHdUnderAt;  // where the scene kept the pixels under the dialogs' translucency (scene.hlsl), 0 nowhere
extern SDL_GPUBuffer *g_hdShownSd;
extern Uint32 g_nHdShownSd;
extern SDL_GPUTexture *g_hdFrameSd;
extern int g_nHdFrameSdW, g_nHdFrameSdH;
extern SDL_GPUGraphicsPipeline *g_hdBlendPipeline;
extern SDL_GPUTransferBuffer *g_hdDown;
extern int g_nHdW, g_nHdH;
// The HD art's live values (port_gpu_fx.cpp, GpuFxValues).
extern float g_fHdOn;
extern float g_fHdFog;  // how much the fog darkens the HD frames: 0 none, 1 as 1.17's
extern float g_fHdKinds;  // which kinds are drawn in HD (hd.kinds)
extern float g_fHdTerrain;  // the terrain in HD too
extern float g_fHdMods;  // a mod's sheets at their own resolution (hd.mods; port_modart.h)
extern float g_fHdConsole;  // Remastered's console too (R4f)
extern float g_fHdWater;  // its water: 0 still, 1 rippling and lit as Remastered's
extern float g_fHdDebug;  // 1: the HD frames show the fog's brightness they take, as grey
// The HD lighting (hd.lighting, Settings' "HD lighting"): how much of it the HD frames take,
// 0 none to 1 Remastered's, and how strongly the frame's lights (port_lights.cpp) light
// them.
extern float g_fHdLighting;
extern float g_fHdLightStrength;

// How the map moves under the rest of the frame: the picture's motion (port_motion.cpp) and
// with the camera zoomed out. At window uv the map is PSGround's layer at
// afMapAt's xy + uv * zw, the frame at afFrameAt's, the lights at afLightAt's; bMoved false
// when it does not move, bZoom when the camera's, and bGround whether the layer is wanted.
struct GpuMapMove {
  bool bMoved, bZoom;
  float fHudShift, fHudAlpha;
  float afMapAt[4], afFrameAt[4], afLightAt[4];
};
void GpuMapMoved(int nWidth, int nHeight, GpuMapMove *pMove);
// The last scene shown, when its map was the render view's (port_gpu_scene.cpp, CSHud).
struct GpuZoomShown {
  bool bOn;
  double fCamX, fCamY, fZoom;
  int nMapX, nMapY, nMapW, nMapH;
  int nGameX, nGameY;
};
extern GpuZoomShown g_zoomShown;
extern SDL_GPUComputePipeline *g_hudPipeline;  // CSHud

// The last scene's map layer, which PSGround can show (port_gpu.cpp): its size, and whether
// it is the full colour's; g_bGroundOk false when the scene did not draw all of it (the
// check's stages, or HD, which leaves out what it draws).
extern bool g_bGroundOk;
extern int g_nGroundW;
extern int g_nGroundRows;
extern bool g_bGroundFull;

// The full colour (stage G; port_gpu_scene.cpp).
extern float g_fFull;  // colour.full
extern bool g_bFullShown;  // the full colour's copy of the screen is the last pass's

// The scene's storage buffers (port_gpu_scene.cpp).
struct GpuStore {
  SDL_GPUBuffer *pBuffer;
  Uint32 nCapacity;  // bytes
};

// In the order scene.hlsl binds them: the read-only ones, then the read-write. SDL_GPU
// binds at most 8 of each.
enum {
  SCENE_STORE_SLOTS,
  SCENE_STORE_TILESET,  // the megatiles, then the cells
  SCENE_STORE_ENTRIES,
  SCENE_STORE_ATLAS,
  SCENE_STORE_PRIMS,
  SCENE_STORE_TABLES,
  SCENE_STORE_FRAME,    // the squares' starts, their lists, then the frame's own bytes
  SCENE_STORE_PREV,     // the map before a distortion
  SCENE_STORE_READ,
  SCENE_STORE_MAP = SCENE_STORE_READ,  // read-write: the GPU's map
  SCENE_STORE_STATS,                   // read-write: what the shader counts
  SCENE_STORE_SHOWN,                   // read-write: the screen as the GPU has it, which
                                       // PSCompose reads too
  SCENE_STORE_TOUCHED,                 // read-write: where anything but the terrain drew, which
                                       // the HD frame reads (hd.hlsl, PSHdBase)
  SCENE_STORE_MAP_RGB,                 // read-write: the full colour's map (stage G), RGBA8 a pixel
  SCENE_STORE_SHOWN_RGB,               // read-write: its copy of the screen, which PSComposeFull
                                       // reads too
  SCENE_STORE_PREV_RGB,                // read-write: its map before a distortion
  SCENE_STORE_HUD,                     // read-write: with the camera zoomed out, where the
                                       // dialogs drew over the map (CSHud), which the
                                       // presenter reads too
  SCENE_STORES
};
extern GpuStore g_sceneStore[SCENE_STORES];


// The functions the parts call in each other.
void GpuBloom(SDL_GPUCommandBuffer *pCmd, int nWidth, int nHeight, bool bCover);
void GpuFxFitSize(int nWidth, int nHeight);
void GpuFxInit(SDL_GPUShader *pVertex);
void GpuFxPass(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pTarget, SDL_GPUGraphicsPipeline *pPipeline,
               SDL_GPUTexture *pFirst, SDL_GPUTexture *pSecond, bool bCover, const void *pParams,
               Uint32 nParams, SDL_GPUTexture *pThird = NULL, SDL_GPUTexture *pFourth = NULL,
               SDL_GPUBuffer *pHud = NULL);
bool GpuFxReady(void);
void GpuFxRelease(void);
void GpuFxReleaseSized(void);
void GpuFxValues(void);
bool GpuHdCan(void);
void GpuHdDrawFrame(SDL_GPUCommandBuffer *pCmd, const SDL_Color *pColors, SDL_GPUTexture *pSdWhole,
                    SDL_GPUTexture *pLayer, bool bGround = false);
bool GpuHdFit(int nWidth, int nHeight);
void GpuHdFitEffect(const SDL_Color *pColors, const std::vector<Uint8> &table, float *pafOut);
void GpuHdFitMatrix(const SDL_Color *pColors, const Uint8 *pTable, const double *padWeight, double m[3][3]);
void GpuHdInit(void);
bool GpuHdMixing(void);
void GpuHdRelease(void);
void GpuHdReleaseConsole(void);
void GpuHdReleaseTerrain(void);
const ScenePrim *GpuHdScene(const SceneFrame *pFrame, Uint32 nBytesAt);
void GpuHdStoreMatrix(const double m[3][3], double k, float *pafRows);
void GpuLights(SDL_GPUCommandBuffer *pCmd, int nWidth, int nHeight);
SDL_GPUGraphicsPipeline *GpuPipeline(SDL_GPUShader *pVertex, SDL_GPUShader *pFragment,
                                     SDL_GPUTextureFormat format, bool bOver = false);
void GpuPostParams(const SDL_FRect &box, int nWidth, int nHeight, bool bCover, PostParams *p);
bool GpuSavePixels(const void *pPixels, int nWidth, int nHeight, const char *pszPath);
bool GpuSceneInit(void);
void GpuSceneRelease(void);
void GpuSceneWriteLog(FILE *f);
// A shader in the three forms SDL takes: DXIL for Direct3D 12, SPIR-V for Vulkan and, on a Mac,
// MSL for Metal (pMsl NULL elsewhere: GPU_MSL), each made by the build from the one HLSL.
SDL_GPUShader *GpuShader(const Uint8 *pDxil, size_t nDxil, const Uint8 *pSpv, size_t nSpv,
                         const Uint8 *pMsl, const char *pszEntry, SDL_GPUShaderStage stage,
                         Uint32 nSamplers, Uint32 nStorageBuffers, Uint32 nUniforms);
// A shader's MSL with the counts its indices name defined ahead of it (tools/shader_msl.py): SDL's
// Metal path puts a storage buffer after every uniform buffer the pipeline declares. SDL_free it.
char *GpuMslSource(const Uint8 *pMsl, Uint32 nSamplers, Uint32 nReadBuffers, Uint32 nUniforms);
SDL_GPUTexture *GpuTexture(SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, int nWidth,
                           int nHeight);
SDL_GPUTransferBuffer *GpuTransfer(SDL_GPUTransferBufferUsage usage, Uint32 nSize);
// Submits a frame's commands without waiting for them, but first waits for the GPU to finish
// those submitted GPU_AHEAD submits before, so a slow GPU slows the frames instead of SDL
// making a new copy of each cycled texture a frame while the old ones are still in use.
void GpuSubmit(SDL_GPUCommandBuffer *pCmd);
// The frame's one command buffer, acquired at its first use: the HD art's uploads,
// the scene and the present record into it. GpuFrameCmdTake hands it to a caller that
// submits it itself (the present, the check, a readback); PortGpuFrameEnd submits what is
// left at the frame's end.
SDL_GPUCommandBuffer *GpuFrameCmd(void);
SDL_GPUCommandBuffer *GpuFrameCmdTake(void);
void GpuUiRelease(void);
// A cinematic in full colour (port_gpu_video.cpp): its pipelines, its picture sent up and put in
// colour when a new one waits, and drawn into the swapchain, fitted to it.
void GpuVideoInit(SDL_GPUShader *pVertex);
void GpuVideoRelease(void);
void GpuVideoUp(SDL_GPUCommandBuffer *pCmd);
void GpuVideoDraw(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pSwap, Uint32 nSwapW, Uint32 nSwapH);
// The UI kit's primitives drawn into the top left nWidth x nHeight of an RGBA8 target, cleared
// first (port_gpu_ui.cpp): the HUD's text layers. A cleared target takes them premultiplied.
bool GpuUiDrawInto(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pTarget, int nWidth, int nHeight, const UiPrim *pPrims,
                   int nPrims);
// The UI kit's primitives drawn in parts inside a pass the caller began (the HUD's slots), on the
// swapchain, or with bSwap false on a texture of RGBA8 (the menus' frame): GpuUiStage uploads the
// list before the pass, GpuUiDrawPart draws nCount of them from nFirst.
bool GpuUiStage(SDL_GPUCommandBuffer *pCmd, const UiPrim *pPrims, int nPrims, bool bSwap = true);
void GpuUiDrawPart(SDL_GPUCommandBuffer *pCmd, SDL_GPURenderPass *pPass, Uint32 nWidth, Uint32 nHeight, int nFirst,
                   int nCount, bool bSwap = true);
// The HUD design over a game's frame (port_gpu_hud.cpp): drawn into the swapchain pSwap,
// nWidth x nHeight, over the game's frame in box, before the overlay. drawFrame draws the frame
// again into a texture of the swapchain's format, w x h, for the screen behind a design's glass.
void GpuHudDraw(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pSwap, Uint32 nWidth, Uint32 nHeight, const SDL_FRect &box,
                const std::function<void(SDL_GPUTexture *, int, int)> &drawFrame);
// Settings' HUD tab's picture (port_hud.h's PortHudPreview): its map picture and the
// design over it, in its box of pTarget, the menus' frame (RGBA8), nWidth x nHeight.
struct PortHudPreview;
void GpuHudPreview(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pTarget, Uint32 nWidth, Uint32 nHeight,
                   const PortHudPreview &p);
void GpuHudRelease(void);
// The menus' frame is between PortGpuUiBegin and PortGpuUiEnd, holding a swapchain image.
bool GpuUiFrameOpen(void);
// Makes the vsync and frames-ahead changes held while it was (PortGpuVsyncSet,
// PortGpuAheadSet); each frame calls it before it takes an image.
void GpuSwapchainWaiting(void);
void GpuWriteLog(void);
