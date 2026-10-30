// port_gpu_hd.cpp -- the frame in HD (part 5 on): Remastered's art and the HD terrain,
// console and front end, drawn over the scene, and the colour fits they and the full
// colour share (docs/renderer.md). Presentation only.
#include <windows.h>
#include <stdio.h>
#include "port_gpu_internal.h"
#include "../platform/port_log.h"  // PortLogger
#include "port_gpu_hd.h"
#include "port_teamcolor.h"  // PortTeamColorGet, PortTeamShades
#include "port_modart.h"      // PortModArtImage, a mod's sheets at their own resolution
#include "../data/port_moddata.h"  // MOD_ART_ICON
#include "../../h/typelimits.h"  // IMAGE_TYPES_MAX
#include "VSHdQuad.dxil.h"
#include "VSHdQuad.spv.h"
#include "PSHdSprite.dxil.h"
#include "PSHdSprite.spv.h"
#include "PSHdBase.dxil.h"
#include "PSHdBase.spv.h"
#include "PSHdBlend.dxil.h"
#include "PSHdBlend.spv.h"
#include "PSHdConsole.dxil.h"
#include "PSHdConsole.spv.h"
#include "PSHdUi.dxil.h"
#include "PSHdUi.spv.h"
#include "VSHdFull.dxil.h"
#include "VSHdFull.spv.h"
#include "VSHdBatch.dxil.h"
#include "VSHdBatch.spv.h"
#include "PSHdBatch.dxil.h"
#include "PSHdBatch.spv.h"
#if defined(__APPLE__)
#include "VSHdQuad.msl.h"
#include "PSHdSprite.msl.h"
#include "PSHdBase.msl.h"
#include "PSHdBlend.msl.h"
#include "PSHdConsole.msl.h"
#include "PSHdUi.msl.h"
#include "VSHdFull.msl.h"
#include "VSHdBatch.msl.h"
#include "PSHdBatch.msl.h"
#endif

// The HD frame's pipelines and targets.
static SDL_GPUGraphicsPipeline *g_hdBasePipeline;  // the frame scaled up, or its software pixels
static SDL_GPUGraphicsPipeline *g_hdSpritePipeline;  // a frame of art, premultiplied, over
static SDL_GPUGraphicsPipeline *g_hdConsolePipeline;  // Remastered's console (hd.hlsl, PSHdConsole)
static SDL_GPUGraphicsPipeline *g_hdUiPipeline;  // the console's HD contents (hd.hlsl, PSHdUi)
static SDL_GPUGraphicsPipeline *g_hdBatchPipeline;  // sprites in batches (VSHdBatch, PSHdBatch)
static SDL_GPUTexture *g_hdPrev;  // the frame so far, which a distortion reads
static Uint64 g_nHdMixAt;  // when the blend (g_fHdMix) last moved, in nanoseconds

// The dialogs' translucency tables after every HD frame, up to 8 a frame, each blit's param
// taking its place plus 1 above 8 bits: out = table[the dialog's pixel and the map's],
// fitted when the frame is drawn, for each pixel of the dialog's, as out = c + b * under
// (GpuHdFitShares), so that over the HD frame it is the SD pixel less b times the pixel it
// was drawn over, plus b times the HD frame.
struct GpuHdOverTable {
  Uint32 nEntry, nMode;  // where the table is, and the blit's mode, 2 the dialog's pixel high
  std::vector<Uint8> table;
};
static GpuHdOverTable g_ahdOverTables[8];
static int g_nHdOverTables;

// An image's art on the GPU, by the image whose art it is (PortHdArt::nImage): its
// diffuse and team layers, and the diffuse texture's size; nWidth -1 when it failed.
struct GpuHdTex {
  SDL_GPUTexture *pDiffuse;
  SDL_GPUTexture *pTeam;
  SDL_GPUTexture *apLit[4];  // the lighting's: emissive, normal, specular, ao_depth, or NULL
  int nWidth, nHeight;
  unsigned nSerial;  // the art's (PortHdArt::nSerial): a mod's sheet set again is another
};
// The frame's lights for the HD lighting, in their own buffer, in the game's pixels.
static SDL_GPUBuffer *g_hdLightBuf;
static SDL_GPUTransferBuffer *g_hdLightUp;
static GpuHdTex g_hdTex[IMAGE_TYPES_MAX];

// A primitive drawn in HD: its art and frame, where its image's anchor is in the game's
// pixels, and for a sprite the palette index its table gives the player's first colour
// and the colour slot it drew with (stage G: a slot with an RGB colour takes that).
struct GpuHdDraw {
  const PortHdArt *pArt;
  int nFrame;
  bool bMirror;
  int nMode;  // hd.hlsl's: 0 a sprite, 1 a shadow, 2 a glow, 3 a distortion, 4 the part of
             // a cloaking unit still drawn, 5 the warp flash
  int nAnchorX, nAnchorY;  // the canvas's centre
  bool bTeam;
  Uint8 nTeam;
  int nSlot;     // the colour slot, -1 when the scene did not say
  bool bFogged;  // drawn before the fog
  Uint32 nTable; // a sprite's remap in the scene's table store (g_hdRemaps), or a
                 // translucent frame's table (g_hdEffects); SCENE_NONE with neither
  // 3 and 4: what 1.17 takes from each colour of the SD frame, the distance a distortion
  // reads from or the brightness a cloaking unit is drawn above nThreshold; the HD frame
  // takes it by its own brightness (GpuHdBrightnessTable)
  Sint16 anValue[256];
  int nThreshold;
  // 5: the warp texture in the table store, a byte a pixel of the clip at nWarpX, nWarpY
  Uint32 nWarpTable;
  int nWarpX, nWarpY, nWarpW, nWarpH;
  Uint32 nRank;  // the HD draws before it: a primitive drawn in SD after it is over it
};
static std::vector<GpuHdDraw> g_hdDraws;

int PortGpuHdDrawCount(void)
{
  return static_cast<int>(g_hdDraws.size());
}
static std::vector<ScenePrim> g_hdPrims;  // the frame's primitives less those drawn in HD
// The sprites' remap tables, by their place in the scene's table store, while its atlas
// lasts: a grey one for what the fog remembers, and the like, which the HD frames take as
// a colour matrix (GpuHdFitMatrix).
static std::unordered_map<Uint32, std::array<Uint8, 256>> g_hdRemaps;
// The translucency tables the same way, the rows the frames read: a glow's, which adds,
// the cloak's, which dims what is under it, and the like, which the HD frames take as
// out = a * frame + b * under (GpuHdFitEffect).
static std::unordered_map<Uint32, std::vector<Uint8>> g_hdEffects;
static unsigned int g_nHdRemapSerial;
// Their fits (GpuHdSpriteFits), kept from frame to frame while the store and the palette
// stay as they were: a large game draws through a hundred tables or more, and a glow's fit
// reads 65,000 of its entries. A translucent table's fit is kept with the rows it was of.
struct GpuHdEffectFit {
  size_t nSize;
  std::array<float, 4> fit;
};
static std::unordered_map<Uint32, std::array<float, 12>> g_hdRemapFits;
static std::unordered_map<Uint32, GpuHdEffectFit> g_hdEffectFits;
static Uint64 g_nHdFitPalette;  // the palette the fits are of (GpuHdKey)
static int g_nHdRows;  // the map's rows, where the art is drawn
// The camera's place for the HD frame: the map layer is the render view's, the
// frame the screen's; off zoomed in, the map layer the screen's.
struct GpuHdZoom {
  bool bOn;
  float fOffX, fOffY;  // the map layer's pixel at the screen's top left
  float fZoom;         // its pixels to a screen pixel
  int nMapW;           // the map layer's pixels across
  int nStep;           // and the game's to one of them (SceneFrame's nStep)
  int nScreenRows;     // the screen's map rows
};
static GpuHdZoom g_hdZoom;

// hd.hlsl's QuadParams, SpriteParams and BaseParams.
struct HdQuadParams {
  float fDstX, fDstY, fDstW, fDstH;
  float fU, fV, fUw, fVh;
  float fTargetW, fTargetH, fUnused[2];
};

struct HdSpriteParams {
  float fTeam[3];
  float fMode;
  float fHasTeam, fFogOn, fFogScale, fRank;
  Uint32 nFogBase, nFogStride, nMapWidth, nMapRows;
  float afRemap[12];  // the remap's colour matrix, three rows of four
  float afEffect[4];   // a translucent frame's a, per channel, and b
  float afBright[32];  // GpuHdDraw's anValue by the HD frame's brightness
  float fThreshold, fMirror;
  Uint32 nWarpTable, nWarpW;
  Sint32 nWarpX, nWarpY;
  Uint32 nWarpH, nRankAt;
  float fLit, fEmissive, fLightScale;  // the lighting: how much, the emissive layer's 1 or 0
  Uint32 nLights;
  float fTargetW, fTargetH, fLightStrength, fLitUnused;
  float afMapAt[4];  // the map layer's pixel at the target's top left, and its pixels to a
                     // game pixel of the screen: the camera's zoom, else 0, 0, 1
  Uint32 nRankLayerW, nRankStep, anRankUnused[2];  // the draw order's plane (hd.hlsl, RankAt)
};

// hd.hlsl's HdSpriteRec, BatchQuad and BatchSprite: a batched sprite's record,
// and the batch's uniforms.
struct HdSpriteRec {
  float afDst[4], afUv[4], afTeam[4], afFlags[4], afRemap[12], afEffect[4];
};
static_assert(sizeof(HdSpriteRec) == 128, "hd.hlsl's HdSpriteRec");
struct HdBatchQuad {
  Uint32 nFirst, nUnused;
  float fTargetW, fTargetH;
};
struct HdBatchSprite {
  Uint32 nFogBase, nFogStride, nMapWidth, nMapRows;
  float afMapAt[4];
  float fFogScale;
  Uint32 nRankAt, nRankLayerW, nRankStep;
};

// A run of the frame's draws: up to HD_BATCH_ARTS arts' plain, shadow and translucent
// frames drawn at once (nArts their arts, each draw's record in g_hdRecs from nRec), or one
// draw the sprite pipeline draws by itself (nArts 0). A large game changes art on nearly
// every draw, and a draw costs the processor more than the card's work on it.
enum { HD_BATCH_ARTS = 8 };
struct GpuHdRun {
  Uint32 nDraw;  // its first draw in g_hdDraws
  Uint32 nCount;
  Uint32 nRec;
  int nArts;
  const GpuHdTex *apArt[HD_BATCH_ARTS];
};
static std::vector<GpuHdRun> g_hdRuns;
static std::vector<HdSpriteRec> g_hdRecs;
static SDL_GPUBuffer *g_hdRecBuf;  // the frame's records, and their upload
static SDL_GPUTransferBuffer *g_hdRecUp;
static Uint32 g_nHdRecCap;

// The fog over the HD frames: the scene's fog primitive, where its light map is in the
// frame store, and its 32 tables of darkness, kept for the frame's brightness per shade.
// Only the draws before it in the game's order are under it.
struct GpuHdFogState {
  bool bOn;
  Uint32 nPrim;
  Uint32 nBase, nStride;
  int nMapWidth;
  Uint8 abShades[32 * 256];
};
static GpuHdFogState g_hdFog;
static double g_adHdTerrainUse[256];  // how often the tileset's cells use each colour (GpuHdFogMatrices)

struct HdBaseParams {
  float fScale, fOnlyOver, fHasCover, fTerrainOn;
  Uint32 nScrollX, nScrollY, nRingX, nRingY;
  Uint32 nRows, nTiles, nTileSize;
  float fFogOn;
  Uint32 nFogBase, nFogStride, nWidth, nOverOn;
  float afFogMat[32 * 12];  // each shade's colour matrix, three rows of four
  Uint32 nCreepCount, nUnderAt, anCreepUnused[2];
  Uint32 anCreepSize[64];
  float fWaterOn, fWaterTime, fWaterFade, fWaterUnused;
  Uint32 anWaterFrames[4];  // each normal map's frame and the next
  Uint32 anWaterCells[4];   // each normal map's frame size, in texels
  Uint32 anStarField[4];  // the star field's size in texels, its layers, its margin
  Uint32 anStarGrid[4];   // a square's texels, the squares across and down, where the stars start
  Sint32 anStarOfs[5 * 4];  // each of the file's layers' shift in texels, x and y
  Uint32 nHoleOn, anHoleUnused[3];  // gHole is the console's holes (R4f)
  float afMapAt[4];    // as HdSpriteParams'
  Uint32 nHudOn;       // 1 when gHud says where the dialogs drew over the zoomed map
  Uint32 nMapStride;   // the map layer's pixels across, gTouched's row
  Uint32 nScreenRows;  // the screen's map rows (the map layer's, unzoomed)
  Uint32 nAllMap;      // 1 for the map alone, the console's place too (the start's ground)
  Uint32 nMapBoxes, nMapStep;  // nMapStep: g_hdZoom's nStep
  Uint32 nLayerOn, nMapBoxUnused;  // nLayerOn: 1 when the map layer is bound, zoomed out
  Sint32 anMapBox[8 * 4];  // where the map shows through 1.17's drawing, in game pixels (B8)
};

// hd.hlsl's ConsoleParams.
enum { HD_UI_OWNED = 32 };
struct HdConsoleParams {
  Sint32 anPiece[3 * 4];  // each piece's box on the target
  Uint32 nPieces;
  float fScale;
  Uint32 nArtX, nArtW, nArtH, nRows, nOwned, nUnused;
  Sint32 anOwned[HD_UI_OWNED * 4];  // the controls the HD contents are drawn in, in game pixels
};

// hd.hlsl's UiParams.
enum { HD_UI_ITEMS = 64, HD_UI_TEXTS = 16 };
struct HdUiParams {
  Sint32 anDst[HD_UI_ITEMS * 4];
  Sint32 anSrc[HD_UI_ITEMS * 4];
  float afTint[HD_UI_ITEMS * 4];
  Sint32 anText[HD_UI_TEXTS * 4];  // the boxes of 1.17's numbers over the icons, in game pixels
  Uint32 anTextBits[HD_UI_TEXTS * PORT_HD_TEXT_WORDS];  // which of their pixels are the text's
  Uint32 nCount, nScale, nTexts, nUnused;
};
static_assert(sizeof(HdBaseParams) <= GPU_UNIFORM_BLOCK_MAX && sizeof(HdConsoleParams) <= GPU_UNIFORM_BLOCK_MAX &&
                  sizeof(HdUiParams) <= GPU_UNIFORM_BLOCK_MAX,
              "a uniform block past 4 KiB reads as zeros on Vulkan");

// The HD terrain (hd.hlsl, PSHdBase): the tileset's atlas on the GPU, and the view the
// scene drew, for the scroll ring.
static SDL_GPUTexture *g_hdTerrainTex;
static SDL_GPUTexture *g_hdCreepTex;  // its creep edges, or NULL
static SDL_GPUTexture *g_hdWaterTex;  // its water masks, or NULL
static SDL_GPUTexture *g_ahdNormalTex[2];  // the ripple's normal maps
static SDL_GPUBuffer *g_hdWaterOf;  // each megatile's mask plus 1 (PortHdTerrain::waterOf)
static SDL_GPUTexture *g_hdStarTex;  // the space platform's star layers, or NULL
static SDL_GPUBuffer *g_hdStars;  // and its stars (PortHdTerrain::starBins)
// The HD console (R4f): 1.17's console art as status_load_console loaded it, which the
// console's pass compares the frame with, and Remastered's pieces for its skin.
static std::vector<Uint8> g_consoleArt;
static int g_nConsoleArtW, g_nConsoleArtH, g_nConsoleArtX;
static int g_anPieceShift[3];  // each piece's console origin in game pixels (PortGpuConsolePieces)
static int g_nConsoleSkin = -1;
static bool g_bConsoleArtNew;
static SDL_GPUBuffer *g_hdConsoleArt;
// The console's holes, the screen's size: 1.17's art byte where no piece is solid over any
// of the pixel's texels, 0 elsewhere; made again when the art, the pieces or the screen do.
static SDL_GPUBuffer *g_hdConsoleHole;
static int g_nHoleW, g_nHoleH;
static SDL_GPUTexture *g_ahdConsoleTex[3];
static const PortHdConsole *g_pHdConsole;  // the pieces those hold
// The console's HD contents (R4f): the icons' atlas and the race's button frames as they
// came up (port_hd.h, PortHdUi), the selection's wireframe for unit type g_nHdWireType,
// and the items PortConsoleHdItems gave this frame.
static SDL_GPUTexture *g_ahdUiTex[PORT_HD_UI_SETS];  // the wireframe's is g_hdWireTex
static const PortHdUi *g_apHdUiUp[PORT_HD_UI_SETS];
static SDL_GPUTexture *g_hdWireTex;
static int g_nHdWireType = -1;
// a mod's own icons and wireframes (PortModUiArt), and the serial of those up
static std::shared_ptr<const PortModUi> g_hdModUi;
static SDL_GPUTexture *g_hdModUiTex;
static unsigned g_nHdModUiSerial;
// the HD portrait (PortHdPortraitFrame), its size and the picture it holds
static SDL_GPUTexture *g_hdPortraitTex;
static int g_nHdPortraitW, g_nHdPortraitH;
static unsigned g_nHdPortraitSerial;
static bool g_bHdPortraitMod;  // a mod's, fitted to its box rather than covering it
static PortHdUiItem g_ahdItems[HD_UI_ITEMS];
static PortHdUiText g_ahdTexts[HD_UI_TEXTS];
static int g_nHdTexts;
static int g_aanHdMapBoxes[8][4];  // PortConsoleHdMapBoxes, this frame's
static int g_nHdMapBoxes;
static int g_nHdItems;
static const PortHdTerrain *g_pHdTerrain;  // the atlases those hold
struct GpuHdViewState {
  int nScrollX, nScrollY, nRingX, nRingY;
  int anStarOfs[10];  // SceneFrame's
  int nLayerW;        // the map layer's pixels across, the draw order plane's
};
static GpuHdViewState g_hdView;

// The state the other parts share (port_gpu_internal.h).
SDL_GPUTexture *g_hdFrame;
SDL_GPUTexture *g_hdPost;
SDL_GPUTexture *g_hdGround;
float g_fHdMix;
float g_fHdFade = 0.5f;
bool g_bHdScene;
static bool g_bHdRemastered;  // this frame draws Remastered's art, and its terrain and console
Uint32 g_nHdOverFrom;
Uint32 g_nHdUnderAt;
SDL_GPUBuffer *g_hdShownSd;
Uint32 g_nHdShownSd;
SDL_GPUTexture *g_hdFrameSd;
int g_nHdFrameSdW, g_nHdFrameSdH;
SDL_GPUGraphicsPipeline *g_hdBlendPipeline;
SDL_GPUTransferBuffer *g_hdDown;
int g_nHdW, g_nHdH;
float g_fHdOn;
float g_fHdFog = 1.0f;
float g_fHdKinds = 63.0f;
float g_fHdTerrain = 1.0f;
float g_fHdConsole = 1.0f;
float g_fHdMods = 1.0f;
float g_fHdWater = 1.0f;
float g_fHdDebug;
float g_fHdLighting;
float g_fHdLightStrength = 0.5f;

// The frame in HD.

// The HD frame's pipelines; without them the frame is drawn in SD.
void GpuHdInit(void)
{
  SDL_GPUShader *pQuad = GpuShader(GPU_CODE(VSHdQuad), "VSHdQuad", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 1);
  SDL_GPUShader *pSprite = GpuShader(GPU_CODE(PSHdSprite), "PSHdSprite",
                                     SDL_GPU_SHADERSTAGE_FRAGMENT, 6, 4, 3);
  SDL_GPUShader *pFull = GpuShader(GPU_CODE(VSHdFull), "VSHdFull", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 0);
  SDL_GPUShader *pBase = GpuShader(GPU_CODE(PSHdBase), "PSHdBase", SDL_GPU_SHADERSTAGE_FRAGMENT, 8, 8, 4);
  SDL_GPUShader *pBlend = GpuShader(GPU_CODE(PSHdBlend), "PSHdBlend", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0, 1);
  SDL_GPUShader *pConsole = GpuShader(GPU_CODE(PSHdConsole), "PSHdConsole",
                                      SDL_GPU_SHADERSTAGE_FRAGMENT, 4, 3, 2);
  SDL_GPUShader *pUi = GpuShader(GPU_CODE(PSHdUi), "PSHdUi", SDL_GPU_SHADERSTAGE_FRAGMENT, 8, 0, 1);
  SDL_GPUShader *pBatchQuad = GpuShader(GPU_CODE(VSHdBatch), "VSHdBatch", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, 1);
  SDL_GPUShader *pBatch = GpuShader(GPU_CODE(PSHdBatch), "PSHdBatch", SDL_GPU_SHADERSTAGE_FRAGMENT,
                                    HD_BATCH_ARTS * 2, 3, 2);
  if (pQuad != NULL && pSprite != NULL && pFull != NULL && pBase != NULL &&
      SDL_GPUTextureSupportsFormat(g_device, SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM, SDL_GPU_TEXTURETYPE_2D,
                                   SDL_GPU_TEXTUREUSAGE_SAMPLER) &&
      SDL_GPUTextureSupportsFormat(g_device, SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM, SDL_GPU_TEXTURETYPE_2D,
                                   SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
    g_hdBasePipeline = GpuPipeline(pFull, pBase, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, true);
    g_hdSpritePipeline = GpuPipeline(pQuad, pSprite, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, true);
    if (pBlend != NULL)
      g_hdBlendPipeline = GpuPipeline(pFull, pBlend, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, true);
    if (pConsole != NULL)
      g_hdConsolePipeline = GpuPipeline(pFull, pConsole, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    if (pUi != NULL)
      g_hdUiPipeline = GpuPipeline(pFull, pUi, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, true);
    // HYPERION_HD_NO_BATCH=1 draws every sprite by itself, to compare
    if (pBatchQuad != NULL && pBatch != NULL && !SDL_GetHintBoolean("HYPERION_HD_NO_BATCH", false))
      g_hdBatchPipeline = GpuPipeline(pBatchQuad, pBatch, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, true);
  }
  SDL_GPUShader *apHd[] = {pQuad, pSprite, pFull, pBase, pBlend, pConsole, pUi, pBatchQuad, pBatch};
  for (SDL_GPUShader *p : apHd)
    if (p != NULL)
      SDL_ReleaseGPUShader(g_device, p);
}

static void GpuHdReleaseSized(void)
{
  if (g_hdFrame != NULL)
    SDL_ReleaseGPUTexture(g_device, g_hdFrame);
  if (g_hdPost != NULL)
    SDL_ReleaseGPUTexture(g_device, g_hdPost);
  if (g_hdPrev != NULL)
    SDL_ReleaseGPUTexture(g_device, g_hdPrev);
  if (g_hdDown != NULL)
    SDL_ReleaseGPUTransferBuffer(g_device, g_hdDown);
  if (g_hdGround != NULL)
    SDL_ReleaseGPUTexture(g_device, g_hdGround);
  g_hdGround = NULL;
  g_hdFrame = NULL;
  g_hdPost = NULL;
  g_hdPrev = NULL;
  g_hdDown = NULL;
  g_nHdW = 0;
  g_nHdH = 0;
}

static void GpuHdTexRelease(GpuHdTex *pTex)
{
  if (pTex->pDiffuse != NULL)
    SDL_ReleaseGPUTexture(g_device, pTex->pDiffuse);
  if (pTex->pTeam != NULL)
    SDL_ReleaseGPUTexture(g_device, pTex->pTeam);
  for (SDL_GPUTexture *pLit : pTex->apLit)
    if (pLit != NULL)
      SDL_ReleaseGPUTexture(g_device, pLit);
  SDL_zerop(pTex);
}

void GpuHdRelease(void)
{
  GpuHdReleaseSized();
  for (GpuHdTex &t : g_hdTex)
    GpuHdTexRelease(&t);
  if (g_hdBasePipeline != NULL)
    SDL_ReleaseGPUGraphicsPipeline(g_device, g_hdBasePipeline);
  if (g_hdSpritePipeline != NULL)
    SDL_ReleaseGPUGraphicsPipeline(g_device, g_hdSpritePipeline);
  if (g_hdBlendPipeline != NULL)
    SDL_ReleaseGPUGraphicsPipeline(g_device, g_hdBlendPipeline);
  if (g_hdConsolePipeline != NULL)
    SDL_ReleaseGPUGraphicsPipeline(g_device, g_hdConsolePipeline);
  if (g_hdUiPipeline != NULL)
    SDL_ReleaseGPUGraphicsPipeline(g_device, g_hdUiPipeline);
  if (g_hdBatchPipeline != NULL)
    SDL_ReleaseGPUGraphicsPipeline(g_device, g_hdBatchPipeline);
  if (g_hdRecBuf != NULL)
    SDL_ReleaseGPUBuffer(g_device, g_hdRecBuf);
  if (g_hdRecUp != NULL)
    SDL_ReleaseGPUTransferBuffer(g_device, g_hdRecUp);
  g_hdBatchPipeline = NULL;
  g_hdRecBuf = NULL;
  g_hdRecUp = NULL;
  g_nHdRecCap = 0;
  if (g_hdLightBuf != NULL)
    SDL_ReleaseGPUBuffer(g_device, g_hdLightBuf);
  if (g_hdLightUp != NULL)
    SDL_ReleaseGPUTransferBuffer(g_device, g_hdLightUp);
  g_hdLightBuf = NULL;
  g_hdLightUp = NULL;
  if (g_hdShownSd != NULL)
    SDL_ReleaseGPUBuffer(g_device, g_hdShownSd);
  if (g_hdFrameSd != NULL)
    SDL_ReleaseGPUTexture(g_device, g_hdFrameSd);
  g_hdBasePipeline = NULL;
  g_hdSpritePipeline = NULL;
  g_hdBlendPipeline = NULL;
  g_hdConsolePipeline = NULL;
  g_hdUiPipeline = NULL;
  g_hdShownSd = NULL;
  g_nHdShownSd = 0;
  g_hdFrameSd = NULL;
  g_nHdFrameSdW = g_nHdFrameSdH = 0;
  g_hdDraws.clear();
  GpuHdReleaseTerrain();
  GpuHdReleaseConsole();
}

// Its pipelines made, and the GPU scene at its last stage, whose cover says where the
// software drew over the map.
bool GpuHdCan(void)
{
  return g_hdBasePipeline != NULL && g_hdSpritePipeline != NULL && PortSceneStage() == SCENE_SCREEN;
}

// The blend moved on by the time since it last moved, toward hd's value; at once when
// hd.fade is 0 or the frame cannot be drawn in HD.
static void GpuHdAdvance(void)
{
  const Uint64 nNow = SDL_GetTicksNS();
  const double dt = g_nHdMixAt != 0 ? static_cast<double>(nNow - g_nHdMixAt) / 1e9 : 0.0;
  g_nHdMixAt = nNow;
  const float fTarget = g_fHdOn >= 0.5f ? 1.0f : 0.0f;
  if (g_fHdFade <= 0.0f || !GpuHdCan()) {
    g_fHdMix = fTarget;
    return;
  }
  const float fStep = static_cast<float>(SDL_min(dt, 1.0 / 30.0) / g_fHdFade);
  g_fHdMix = fTarget > g_fHdMix ? SDL_min(fTarget, g_fHdMix + fStep) : SDL_max(fTarget, g_fHdMix - fStep);
}

// Between SD and HD, mid-blend.
bool GpuHdMixing(void)
{
  return g_fHdMix > 0.0f && g_fHdMix < 1.0f;
}

// Remastered's art drawn, wholly or in part: HD switched on and its art there to draw.
static bool GpuHdRemastered(void)
{
  return g_fHdMix > 0.0f && PortHdAvailable();
}

// The frame in HD when there is Remastered's art to draw it with, or a mod's sheets at their
// own resolution (port_modart.h), which are drawn so whether Remastered's art is or not.
// Without either Art's HD is SD's frame: the scene keeps its map layer, which the console rises
// over at a skirmish's start, where the HD frame would have no ground there and show the mirror.
static bool GpuHdWanted(void)
{
  return GpuHdCan() && (GpuHdRemastered() || (g_fHdMods >= 0.5f && PortModArtWanted()));
}

// HD can be switched to: the GPU draws it and there is Remastered's HD art to draw. The art is
// Remastered's paid content, held until the files are given otherwise (PortRemasteredOwned,
// the PRD's decisions of 2026-10-21), so for now neither the hotkey, Settings nor a stored
// "art" turns HD on.
static bool GpuHdAllowed(void)
{
  return GpuHdCan() && PortHdAvailable();
}

// The hotkey's switch, kept as Settings' Art, as Remastered keeps its F5's.
void PortGpuHdToggle(void)
{
  if (!GpuHdAllowed())
    return;
  g_fHdOn = g_fHdOn >= 0.5f ? 0.0f : 1.0f;
  PortSettingSetNumber("hyperion", "art", g_fHdOn);
}

int PortGpuHdCan(void)
{
  return GpuHdCan() ? 1 : 0;
}

int PortGpuHdShown(void)
{
  return GpuHdWanted() ? 1 : 0;
}

void PortGpuHdSet(int bOn)
{
  g_fHdOn = bOn && GpuHdAllowed() ? 1.0f : 0.0f;
}

void PortGpuHdLightingSet(int bOn)
{
  g_fHdLighting = bOn ? 1.0f : 0.0f;
}

void PortGpuConsolePieces(const int anShift[3])
{
  for (int i = 0; i < 3; ++i)
    g_anPieceShift[i] = anShift[i];
  g_nHoleW = g_nHoleH = 0;
}

int PortGpuHdConsoleShown(void)
{
  return g_fHdOn >= 0.5f && g_fHdMix >= 1.0f && g_fHdConsole >= 0.5f && GpuHdCan() ? 1 : 0;
}

int PortGpuModUiShown(void)
{
  return g_hdUiPipeline != NULL && g_fHdMods >= 0.5f && GpuHdWanted() && PortModUiArt() != nullptr ? 1 : 0;
}

void PortGpuConsoleArt(const unsigned char *pPixels, int nWidth, int nHeight, int nX, int nSkin)
{
  if (pPixels == NULL || nWidth <= 0 || nHeight <= 0) {
    g_consoleArt.clear();
    g_nConsoleSkin = -1;
    return;
  }
  g_consoleArt.assign(pPixels, pPixels + static_cast<size_t>(nWidth) * nHeight);
  g_nConsoleArtW = nWidth;
  g_nConsoleArtH = nHeight;
  g_nConsoleArtX = nX;
  for (int &n : g_anPieceShift)
    n = nX;
  g_nConsoleSkin = nSkin >= 0 && nSkin < PORT_HD_CONSOLES ? nSkin : -1;
  g_bConsoleArtNew = true;
}

bool GpuHdFit(int nWidth, int nHeight)
{
  if (g_hdFrame != NULL && g_nHdW == nWidth && g_nHdH == nHeight)
    return true;
  GpuHdReleaseSized();
  const SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  g_hdFrame = GpuTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, usage, nWidth, nHeight);
  g_hdPost = GpuTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, usage, nWidth, nHeight);
  g_hdPrev = GpuTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, nWidth, nHeight);
  g_hdDown = GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, static_cast<Uint32>(nWidth * nHeight * 4));
  if (g_hdFrame == NULL || g_hdPost == NULL || g_hdPrev == NULL || g_hdDown == NULL) {
    GpuHdReleaseSized();
    return false;
  }
  g_nHdW = nWidth;
  g_nHdH = nHeight;
  return true;
}

// The HD map alone at a skirmish's start, made the first time it is wanted at the frame's size.
bool GpuHdGroundFit(void)
{
  if (g_hdGround == NULL && g_nHdW > 0 && g_nHdH > 0)
    g_hdGround = GpuTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                            SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, g_nHdW, g_nHdH);
  return g_hdGround != NULL;
}

// A layer of art up to the GPU as it is: in BC blocks, its size rounded up to whole blocks,
// or a mod sheet's pixels, RGBA or a byte each.
static SDL_GPUTexture *GpuHdLayer(const PortHdLayer &layer, SDL_GPUCopyPass *pCopy)
{
  const bool bRaw = layer.nBc == PORT_HD_RGBA8 || layer.nBc == PORT_HD_R8;
  const int nWidth = bRaw ? layer.nWidth : (layer.nWidth + 3) & ~3;
  const int nHeight = bRaw ? layer.nHeight : (layer.nHeight + 3) & ~3;
  const SDL_GPUTextureFormat format = layer.nBc == 1               ? SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM
                                      : layer.nBc == 3             ? SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM
                                      : layer.nBc == PORT_HD_RGBA8 ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
                                                                   : SDL_GPU_TEXTUREFORMAT_R8_UNORM;
  SDL_GPUTexture *pTexture = GpuTexture(format, SDL_GPU_TEXTUREUSAGE_SAMPLER, nWidth, nHeight);
  if (pTexture == NULL)
    return NULL;
  SDL_GPUTransferBuffer *pUp =
      GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, static_cast<Uint32>(layer.blocks.size()));
  void *pMapped = pUp != NULL ? SDL_MapGPUTransferBuffer(g_device, pUp, false) : NULL;
  if (pMapped == NULL) {
    if (pUp != NULL)
      SDL_ReleaseGPUTransferBuffer(g_device, pUp);
    SDL_ReleaseGPUTexture(g_device, pTexture);
    return NULL;
  }
  SDL_memcpy(pMapped, layer.blocks.data(), layer.blocks.size());
  SDL_UnmapGPUTransferBuffer(g_device, pUp);
  SDL_GPUTextureTransferInfo src;
  SDL_GPUTextureRegion dst;
  SDL_zero(src);
  SDL_zero(dst);
  src.transfer_buffer = pUp;
  src.pixels_per_row = static_cast<Uint32>(nWidth);
  src.rows_per_layer = static_cast<Uint32>(nHeight);
  dst.texture = pTexture;
  dst.w = static_cast<Uint32>(nWidth);
  dst.h = static_cast<Uint32>(nHeight);
  dst.d = 1;
  SDL_UploadToGPUTexture(pCopy, &src, &dst, false);
  SDL_ReleaseGPUTransferBuffer(g_device, pUp);  // freed once the upload is done
  return pTexture;
}

// RGBA pixels into a texture of their size, made again when the size changes.
static void GpuHdRgba(SDL_GPUTexture **ppTexture, int *pnWidth, int *pnHeight, const unsigned char *pRgba, int nWidth,
                      int nHeight, SDL_GPUCopyPass *pCopy)
{
  if (*ppTexture != NULL && (*pnWidth != nWidth || *pnHeight != nHeight)) {
    SDL_ReleaseGPUTexture(g_device, *ppTexture);
    *ppTexture = NULL;
  }
  if (*ppTexture == NULL) {
    *ppTexture = GpuTexture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, nWidth, nHeight);
    *pnWidth = *ppTexture != NULL ? nWidth : 0;
    *pnHeight = *ppTexture != NULL ? nHeight : 0;
    if (*ppTexture == NULL)
      return;
  }
  const Uint32 nSize = static_cast<Uint32>(nWidth) * static_cast<Uint32>(nHeight) * 4;
  SDL_GPUTransferBuffer *pUp = GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, nSize);
  void *pMapped = pUp != NULL ? SDL_MapGPUTransferBuffer(g_device, pUp, false) : NULL;
  if (pMapped == NULL) {
    if (pUp != NULL)
      SDL_ReleaseGPUTransferBuffer(g_device, pUp);
    return;
  }
  SDL_memcpy(pMapped, pRgba, nSize);
  SDL_UnmapGPUTransferBuffer(g_device, pUp);
  SDL_GPUTextureTransferInfo src;
  SDL_GPUTextureRegion dst;
  SDL_zero(src);
  SDL_zero(dst);
  src.transfer_buffer = pUp;
  src.pixels_per_row = static_cast<Uint32>(nWidth);
  src.rows_per_layer = static_cast<Uint32>(nHeight);
  dst.texture = *ppTexture;
  dst.w = static_cast<Uint32>(nWidth);
  dst.h = static_cast<Uint32>(nHeight);
  dst.d = 1;
  SDL_UploadToGPUTexture(pCopy, &src, &dst, false);
  SDL_ReleaseGPUTransferBuffer(g_device, pUp);
}

// A buffer up to the GPU, for the shaders to read.
static SDL_GPUBuffer *GpuHdBuffer(const void *pData, Uint32 nSize, SDL_GPUCopyPass *pCopy)
{
  SDL_GPUBufferCreateInfo info;
  SDL_zero(info);
  info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
  info.size = (nSize + 3) & ~3u;
  SDL_GPUBuffer *pBuffer = SDL_CreateGPUBuffer(g_device, &info);
  SDL_GPUTransferBuffer *pUp = pBuffer != NULL ? GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, info.size) : NULL;
  void *pMapped = pUp != NULL ? SDL_MapGPUTransferBuffer(g_device, pUp, false) : NULL;
  if (pMapped == NULL) {
    if (pUp != NULL)
      SDL_ReleaseGPUTransferBuffer(g_device, pUp);
    if (pBuffer != NULL)
      SDL_ReleaseGPUBuffer(g_device, pBuffer);
    return NULL;
  }
  SDL_memset(pMapped, 0, info.size);
  SDL_memcpy(pMapped, pData, nSize);
  SDL_UnmapGPUTransferBuffer(g_device, pUp);
  SDL_GPUTransferBufferLocation src;
  SDL_GPUBufferRegion dst;
  SDL_zero(src);
  SDL_zero(dst);
  src.transfer_buffer = pUp;
  dst.buffer = pBuffer;
  dst.size = info.size;
  SDL_UploadToGPUBuffer(pCopy, &src, &dst, false);
  SDL_ReleaseGPUTransferBuffer(g_device, pUp);  // freed once the upload is done
  return pBuffer;
}

// The tileset's textures and buffer off the GPU.
void GpuHdReleaseTerrain(void)
{
  SDL_GPUTexture **appTex[6] = {&g_hdTerrainTex, &g_hdCreepTex, &g_hdWaterTex, &g_ahdNormalTex[0], &g_ahdNormalTex[1],
                                &g_hdStarTex};
  for (SDL_GPUTexture **ppTex : appTex) {
    if (*ppTex != NULL)
      SDL_ReleaseGPUTexture(g_device, *ppTex);
    *ppTex = NULL;
  }
  if (g_hdWaterOf != NULL)
    SDL_ReleaseGPUBuffer(g_device, g_hdWaterOf);
  g_hdWaterOf = NULL;
  if (g_hdStars != NULL)
    SDL_ReleaseGPUBuffer(g_device, g_hdStars);
  g_hdStars = NULL;
  g_pHdTerrain = NULL;
}

// A mod's own icons and wireframes off the GPU.
static void GpuHdModUiRelease(void)
{
  if (g_hdModUiTex != NULL)
    SDL_ReleaseGPUTexture(g_device, g_hdModUiTex);
  g_hdModUiTex = NULL;
  g_nHdModUiSerial = 0;
  g_hdModUi.reset();
}

// The console's textures and art off the GPU.
void GpuHdReleaseConsole(void)
{
  for (SDL_GPUTexture *&pTex : g_ahdConsoleTex) {
    if (pTex != NULL)
      SDL_ReleaseGPUTexture(g_device, pTex);
    pTex = NULL;
  }
  if (g_hdConsoleArt != NULL)
    SDL_ReleaseGPUBuffer(g_device, g_hdConsoleArt);
  if (g_hdConsoleHole != NULL)
    SDL_ReleaseGPUBuffer(g_device, g_hdConsoleHole);
  g_hdConsoleArt = NULL;
  g_hdConsoleHole = NULL;
  for (int i = 0; i < PORT_HD_UI_SETS; ++i) {
    if (g_ahdUiTex[i] != NULL)
      SDL_ReleaseGPUTexture(g_device, g_ahdUiTex[i]);
    g_ahdUiTex[i] = NULL;
    g_apHdUiUp[i] = NULL;
  }
  if (g_hdWireTex != NULL)
    SDL_ReleaseGPUTexture(g_device, g_hdWireTex);
  g_hdWireTex = NULL;
  g_nHdWireType = -1;
  if (g_hdPortraitTex != NULL)
    SDL_ReleaseGPUTexture(g_device, g_hdPortraitTex);
  g_hdPortraitTex = NULL;
  g_nHdPortraitW = g_nHdPortraitH = 0;
  GpuHdModUiRelease();
  g_nHoleW = g_nHoleH = 0;
  g_pHdConsole = NULL;
  g_bConsoleArtNew = true;
}

// The console's holes for a screen nWidth x nHeight at scale nScale (g_hdConsoleHole).
static std::vector<Uint8> GpuHdConsoleHoles(const PortHdConsole *pConsole, int nWidth, int nHeight, int nScale)
{
  std::vector<Uint8> holes(static_cast<size_t>((nWidth * nHeight + 3) & ~3), 0);
  const int nTop = nHeight - g_nConsoleArtH;
  for (int y = 0; y < g_nConsoleArtH; ++y) {
    const int sy = nTop + y;
    if (sy < 0 || sy >= nHeight)
      continue;
    for (int x = 0; x < g_nConsoleArtW; ++x) {
      const Uint8 art = g_consoleArt[static_cast<size_t>(y) * g_nConsoleArtW + x];
      const int sx = g_nConsoleArtX + x;
      if (art == 0 || sx < 0 || sx >= nWidth)
        continue;
      bool bAllSolid = true;
      for (int ty = 0; ty < nScale && bAllSolid; ++ty)
        for (int tx = 0; tx < nScale && bAllSolid; ++tx) {
          bool bSolid = false;
          for (int i = 0; i < pConsole->nPieces && !bSolid; ++i) {
            const PortHdLayer &piece = pConsole->pieces[i].art;
            const int px = sx * nScale + tx - (g_anPieceShift[i] * nScale + pConsole->pieces[i].nX);
            const int py = sy * nScale + ty - (nHeight * nScale - piece.nHeight);
            bSolid = PortHdLayerSolid(piece, px, py);
          }
          bAllSolid = bSolid;
        }
      if (!bAllSolid)
        holes[static_cast<size_t>(sy) * nWidth + sx] = art;
    }
  }
  return holes;
}

// An image's art on the GPU, or on its way there while *pnBudget lasts: a few images a
// frame, so a screen of new art does not stall one frame.
static bool GpuHdResident(const PortHdArt *pArt, SDL_GPUCopyPass *pCopy, int *pnBudget)
{
  GpuHdTex &t = g_hdTex[pArt->nImage];
  if (t.nSerial != pArt->nSerial) {
    GpuHdTexRelease(&t);
    t.nSerial = pArt->nSerial;
  }
  if (t.pDiffuse != NULL)
    return true;
  if (t.nWidth < 0 || *pnBudget <= 0)
    return false;
  --*pnBudget;
  const PortHdLayer &diffuse = pArt->layers[PORT_HD_DIFFUSE];
  t.pDiffuse = GpuHdLayer(diffuse, pCopy);
  if (t.pDiffuse == NULL) {
    t.nWidth = -1;
    return false;
  }
  t.nWidth = (diffuse.nWidth + 3) & ~3;
  t.nHeight = (diffuse.nHeight + 3) & ~3;
  // the team layer shares the diffuse layer's frames, so its size
  const PortHdLayer &team = pArt->layers[PORT_HD_TEAM];
  if (team.nBc != 0 && team.nWidth == diffuse.nWidth && team.nHeight == diffuse.nHeight)
    t.pTeam = GpuHdLayer(team, pCopy);
  // the lighting's layers, which share them too
  static const int kLit[4] = {PORT_HD_EMISSIVE, PORT_HD_NORMAL, PORT_HD_SPECULAR, PORT_HD_AO_DEPTH};
  for (int k = 0; k < 4; ++k) {
    const PortHdLayer &lit = pArt->layers[kLit[k]];
    if (lit.nBc != 0 && lit.nWidth == diffuse.nWidth && lit.nHeight == diffuse.nHeight)
      t.apLit[k] = GpuHdLayer(lit, pCopy);
  }
  return true;
}

// The frame's uploads of HD art: the frame's command buffer and one copy pass, begun at the
// first upload (GpuHdCopy) and ended at the scene's end. The present submits the buffer.
struct GpuHdUploads {
  SDL_GPUCommandBuffer *pCmd;
  SDL_GPUCopyPass *pCopy;
};

static SDL_GPUCopyPass *GpuHdCopy(GpuHdUploads *pUp)
{
  if (pUp->pCopy == NULL) {
    pUp->pCmd = pUp->pCmd != NULL ? pUp->pCmd : GpuFrameCmd();
    if (pUp->pCmd != NULL)
      pUp->pCopy = SDL_BeginGPUCopyPass(pUp->pCmd);
  }
  return pUp->pCopy;
}

// The view the scene drew: the map's rows, the camera's place (zoomed, the map layer is the
// render view's and the screen shows it through the camera), and the scroll ring.
static void GpuHdSceneView(const SceneFrame *pFrame)
{
  g_nHdRows = pFrame->nRows;
  g_hdZoom.bOn = pFrame->bZoom != 0;
  g_hdZoom.fOffX = g_hdZoom.bOn ? static_cast<float>(pFrame->fCamX - pFrame->nScrollX) : 0.0f;
  g_hdZoom.fOffY = g_hdZoom.bOn ? static_cast<float>(pFrame->fCamY - pFrame->nScrollY) : 0.0f;
  g_hdZoom.fZoom = g_hdZoom.bOn ? static_cast<float>(pFrame->fZoom) : 1.0f;
  g_hdZoom.nMapW = pFrame->nLayerW;
  g_hdZoom.nStep = g_hdZoom.bOn ? pFrame->nStep : 1;
  g_hdZoom.nScreenRows = g_hdZoom.bOn ? pFrame->nScreenRows : pFrame->nRows;
  g_hdView.nScrollX = pFrame->nScrollX;
  g_hdView.nScrollY = pFrame->nScrollY;
  g_hdView.nRingX = pFrame->nRingTilesX;
  g_hdView.nRingY = pFrame->nRingTilesY;
  SDL_memcpy(g_hdView.anStarOfs, pFrame->anStarOfs, sizeof g_hdView.anStarOfs);
  g_hdView.nLayerW = pFrame->nLayerW;
}

// Image nImage's art this frame: a mod's sheet's, which is its own alone, or Remastered's
// while that is drawn.
static const PortHdArt *GpuHdArtOf(int nImage)
{
  if (nImage >= IMAGE_TYPE_MOD_FIRST)
    return g_fHdMods >= 0.5f ? PortModArtImage(nImage) : NULL;
  return g_bHdRemastered ? PortHdImage(nImage) : NULL;
}

// A game after another: the textures of the last one's mod art let go.
static void GpuHdModArtGeneration(void)
{
  static unsigned s_nGeneration;
  const unsigned nGeneration = PortModArtGeneration();
  if (nGeneration == s_nGeneration)
    return;
  s_nGeneration = nGeneration;
  for (GpuHdTex &t : g_hdTex)
    if (t.nSerial != 0)
      GpuHdTexRelease(&t);
  GpuHdModUiRelease();
}

// A mod's own icons and wireframes, once a game's are up (PortModUiArt).
static void GpuHdModUiUp(GpuHdUploads *pUp)
{
  g_hdModUi = PortModUiArt();
  if (!g_hdModUi || (g_hdModUiTex != NULL && g_nHdModUiSerial == g_hdModUi->nSerial))
    return;
  SDL_GPUCopyPass *pCopy = GpuHdCopy(pUp);
  if (pCopy == NULL)
    return;
  if (g_hdModUiTex != NULL)
    SDL_ReleaseGPUTexture(g_device, g_hdModUiTex);
  g_hdModUiTex = GpuHdLayer(g_hdModUi->atlas, pCopy);
  g_nHdModUiSerial = g_hdModUiTex != NULL ? g_hdModUi->nSerial : 0;
}

// The tileset's terrain, once it is in; until then, and for another tileset's, SD.
static void GpuHdTerrainUp(const SceneFrame *pFrame, GpuHdUploads *pUp)
{
  const PortHdTerrain *pTerrain = PortHdTileset(pFrame->nEra);
  if (pTerrain != NULL && pTerrain != g_pHdTerrain) {
    SDL_GPUCopyPass *pCopy = GpuHdCopy(pUp);
    if (pCopy != NULL) {
      GpuHdReleaseTerrain();
      g_hdTerrainTex = GpuHdLayer(pTerrain->atlas, pCopy);
      g_hdCreepTex = pTerrain->nCreep > 0 ? GpuHdLayer(pTerrain->creep, pCopy) : NULL;
      if (pTerrain->nWaterMasks > 0) {
        g_hdWaterTex = GpuHdLayer(pTerrain->waterMask, pCopy);
        g_ahdNormalTex[0] = GpuHdLayer(pTerrain->normals[0], pCopy);
        g_ahdNormalTex[1] = GpuHdLayer(pTerrain->normals[1], pCopy);
        g_hdWaterOf = GpuHdBuffer(pTerrain->waterOf.data(),
                                  static_cast<Uint32>(pTerrain->waterOf.size() * sizeof(unsigned int)), pCopy);
      }
      if (pTerrain->nStarLayers > 0) {
        g_hdStarTex = GpuHdLayer(pTerrain->starAtlas, pCopy);
        g_hdStars = GpuHdBuffer(pTerrain->starBins.data(),
                                static_cast<Uint32>(pTerrain->starBins.size() * sizeof(unsigned int)), pCopy);
      }
      g_pHdTerrain = g_hdTerrainTex != NULL ? pTerrain : NULL;
    }
  }
  if (g_pHdTerrain != NULL && g_pHdTerrain->nEra != pFrame->nEra)
    g_pHdTerrain = NULL;
}

// The console's pieces once they are in, and 1.17's art each time the game loads it; then
// the console's contents, this frame's items and the art they need; then the console's
// holes when the screen's size changed.
static void GpuHdConsoleUp(GpuHdUploads *pUp)
{
  const PortHdConsole *pConsole = g_bHdRemastered && g_nConsoleSkin >= 0 ? PortHdConsoleArt(g_nConsoleSkin) : NULL;
  if (pConsole != NULL && (pConsole != g_pHdConsole || g_bConsoleArtNew) && !g_consoleArt.empty()) {
    SDL_GPUCopyPass *pCopy = GpuHdCopy(pUp);
    if (pCopy != NULL) {
      if (pConsole != g_pHdConsole) {
        GpuHdReleaseConsole();
        for (int i = 0; i < pConsole->nPieces; ++i)
          g_ahdConsoleTex[i] = GpuHdLayer(pConsole->pieces[i].art, pCopy);
      }
      if (g_hdConsoleArt != NULL)
        SDL_ReleaseGPUBuffer(g_device, g_hdConsoleArt);
      g_hdConsoleArt = GpuHdBuffer(g_consoleArt.data(), static_cast<Uint32>(g_consoleArt.size()), pCopy);
      g_bConsoleArtNew = false;
      g_nHoleW = g_nHoleH = 0;
      g_pHdConsole = pConsole;
      for (int i = 0; i < pConsole->nPieces; ++i)
        if (g_ahdConsoleTex[i] == NULL)
          g_pHdConsole = NULL;
    }
  }
  if (g_pHdConsole != NULL &&
      (!g_bHdRemastered || g_nConsoleSkin < 0 || g_pHdConsole->nSkin != g_nConsoleSkin))
    g_pHdConsole = NULL;
  // without Remastered's console, a mod's own icons, wireframe and portrait over the classic one
  const bool bItems = g_pHdConsole != NULL || PortGpuModUiShown() != 0;
  g_nHdItems = bItems ? PortConsoleHdItems(g_ahdItems, HD_UI_ITEMS, g_pHdConsole != NULL) : 0;
  g_nHdTexts = bItems ? PortConsoleHdTexts(g_ahdTexts, HD_UI_TEXTS) : 0;
  g_nHdMapBoxes = g_pHdConsole != NULL ? PortConsoleHdMapBoxes(g_aanHdMapBoxes, 8) : 0;
  if (g_nHdItems > 0)
    GpuHdModUiUp(pUp);
  for (int k = 0; k < g_nHdItems; ++k) {
    const PortHdUiItem &item = g_ahdItems[k];
    if (item.nSet == PORT_HD_UI_MOD)
      continue;
    if (item.nSet == PORT_HD_UI_PORTRAIT) {
      const PortHdPortrait *pPortrait = PortHdPortraitFrame();
      SDL_GPUCopyPass *pCopy = pPortrait != NULL && (pPortrait->nSerial != g_nHdPortraitSerial || g_hdPortraitTex == NULL)
                                   ? GpuHdCopy(pUp)
                                   : NULL;
      if (pCopy != NULL) {
        GpuHdRgba(&g_hdPortraitTex, &g_nHdPortraitW, &g_nHdPortraitH, pPortrait->pRgba, pPortrait->nWidth,
                  pPortrait->nHeight, pCopy);
        g_nHdPortraitSerial = pPortrait->nSerial;
        g_bHdPortraitMod = pPortrait->bMod;
      }
      continue;
    }
    const PortHdUi *pUi = PortHdUiSet(item.nSet);
    if (pUi == NULL)
      continue;
    const bool bWire = item.nSet == PORT_HD_UI_WIRES;
    if (bWire ? g_nHdWireType == item.nFrame : g_apHdUiUp[item.nSet] == pUi)
      continue;
    if (bWire && (item.nFrame < 0 || item.nFrame >= pUi->nFrames || pUi->frames[item.nFrame].nBc == 0))
      continue;
    SDL_GPUCopyPass *pCopy = GpuHdCopy(pUp);
    if (pCopy == NULL)
      break;
    SDL_GPUTexture *&pTex = bWire ? g_hdWireTex : g_ahdUiTex[item.nSet];
    if (pTex != NULL)
      SDL_ReleaseGPUTexture(g_device, pTex);
    pTex = GpuHdLayer(bWire ? pUi->frames[item.nFrame] : pUi->atlas, pCopy);
    if (bWire)
      g_nHdWireType = pTex != NULL ? item.nFrame : -1;
    else
      g_apHdUiUp[item.nSet] = pTex != NULL ? pUi : NULL;
  }
  if (g_pHdConsole != NULL && (g_nHoleW != g_nWidth || g_nHoleH != g_nHeight) && g_nWidth > 0) {
    SDL_GPUCopyPass *pCopy = GpuHdCopy(pUp);
    if (pCopy != NULL) {
      const std::vector<Uint8> holes = GpuHdConsoleHoles(g_pHdConsole, g_nWidth, g_nHeight, PortHdScale());
      if (g_hdConsoleHole != NULL)
        SDL_ReleaseGPUBuffer(g_device, g_hdConsoleHole);
      g_hdConsoleHole = GpuHdBuffer(holes.data(), static_cast<Uint32>(holes.size()), pCopy);
      g_nHoleW = g_nWidth;
      g_nHoleH = g_nHeight;
    }
  }
}

// How often the tileset's cells use each colour, for the terrain's fog, and the fog itself
// when the frame has it: its primitive, its light map in the frame's store and its shades.
static void GpuHdFogOf(const SceneFrame *pFrame, Uint32 nMapPrims, Uint32 nBytesAt)
{
  static unsigned int s_nUseTileset;
  if (pFrame->nTileset != s_nUseTileset && pFrame->pCells != NULL) {
    s_nUseTileset = pFrame->nTileset;
    SDL_zero(g_adHdTerrainUse);
    for (Uint32 i = 0; i < pFrame->nCells * 64u; ++i)
      g_adHdTerrainUse[pFrame->pCells[i]] += 1.0;
  }
  g_hdFog.bOn = false;
  for (Uint32 i = 0; i < nMapPrims; ++i) {
    const ScenePrim &fog = pFrame->pPrims[i];
    if ((fog.nKind & 0xFF) != SCENE_PRIM_FOG || fog.nTable == SCENE_NONE ||
        fog.nTable + sizeof g_hdFog.abShades > pFrame->nTables)
      continue;
    g_hdFog.bOn = true;
    g_hdFog.nPrim = i;
    g_hdFog.nBase = nBytesAt + static_cast<Uint32>(fog.nX);
    g_hdFog.nStride = static_cast<Uint32>(fog.nY);
    g_hdFog.nMapWidth = pFrame->nWidth;
    SDL_memcpy(g_hdFog.abShades, pFrame->pTables + fog.nTable, sizeof g_hdFog.abShades);
    break;
  }
}

// The log's line for each image the first time it is drawn in HD in each mode, for a look
// at what is.
static void GpuHdLogFirst(int nImage, const GpuHdDraw &draw, unsigned int nGrp)
{
  static bool s_abSeen[IMAGE_TYPES_MAX][6];
  static const char *const s_apszModes[6] = {"a sprite", "a shadow", "translucent", "a distortion",
                                             "cloaking", "the warp flash"};
  if (s_abSeen[nImage][draw.nMode])
    return;
  s_abSeen[nImage][draw.nMode] = true;
  static const PortLogger s_log("hyperion_hd_draws.log");
  const PortHdArt *pArt = draw.pArt;
  s_log("image %d (art %d) as %s: GRP %dx%d, HD canvas %dx%d at %dx, %d frames of %d", nImage, pArt->nImage,
        s_apszModes[draw.nMode], static_cast<int>(nGrp & 0xFFFF), static_cast<int>(nGrp >> 16), pArt->nCanvasW,
        pArt->nCanvasH, pArt->nScale, static_cast<int>(pArt->frames.size()), draw.nFrame);
}

// What a draw takes from its primitive's tables by its kind: a distortion's distances, a
// cloaking unit's brightness and threshold, the warp flash's texture, a translucent
// frame's rows, a sprite's remap and team colour. False when the tables it needs are not
// in the store, and the primitive stays as the game draws it.
static bool GpuHdDrawTables(const SceneFrame *pFrame, Uint32 i, const ScenePrim &prim, GpuHdDraw *pDraw)
{
  GpuHdDraw &draw = *pDraw;
  const unsigned int nKind = prim.nKind & 0xFF;
  if (nKind == SCENE_PRIM_DISTORT) {
    // the distance: the brightness table's at the colour, or the colour itself (EMP)
    const bool bTable = prim.nTable != SCENE_NONE && prim.nTable + 256 <= pFrame->nTables;
    for (int v = 0; v < 256; ++v)
      draw.anValue[v] = static_cast<Sint16>(bTable ? pFrame->pTables[prim.nTable + v] : v);
    draw.bFogged = false;  // what it shows is fogged already
  }
  if (nKind == SCENE_PRIM_UNCLOAKED) {
    const Uint32 nBright = prim.nParam >> 8;
    if (prim.nTable == SCENE_NONE || prim.nTable + 256 > pFrame->nTables || nBright + 256 > pFrame->nTables)
      return false;
    for (int v = 0; v < 256; ++v)
      draw.anValue[v] = static_cast<Sint8>(pFrame->pTables[nBright + pFrame->pTables[prim.nTable + v]]);
    draw.nThreshold = static_cast<Sint8>(prim.nParam & 0xFF);
  }
  if (nKind == SCENE_PRIM_WARP) {
    draw.nWarpW = static_cast<int>(prim.nClip1 & 0xFFFF);
    draw.nWarpH = static_cast<int>(prim.nClip1 >> 16);
    if (prim.nTable == SCENE_NONE || prim.nTable + static_cast<Uint32>(draw.nWarpW * draw.nWarpH) > pFrame->nTables)
      return false;
    draw.nWarpTable = prim.nTable;
    draw.nWarpX = prim.nX;
    draw.nWarpY = prim.nY;
  }
  if (g_nHdRemapSerial != pFrame->nAtlasSerial) {
    g_nHdRemapSerial = pFrame->nAtlasSerial;
    g_hdRemaps.clear();
    g_hdEffects.clear();
    g_hdRemapFits.clear();
    g_hdEffectFits.clear();
  }
  if (nKind == SCENE_PRIM_EFFECT && prim.nTable != SCENE_NONE && prim.nTable + 256 <= pFrame->nTables) {
    // the rows up to the frame's highest colour, as many as the store holds
    const SceneEntry &entry = pFrame->pEntries[prim.nEntry];
    const Uint32 nRows = SDL_min(SDL_min(entry.nMax + 1u, 256u), (pFrame->nTables - prim.nTable) / 256u);
    std::vector<Uint8> &table = g_hdEffects[prim.nTable];
    if (table.size() < nRows * 256u)
      table.assign(pFrame->pTables + prim.nTable, pFrame->pTables + prim.nTable + nRows * 256u);
    draw.nTable = prim.nTable;
  }
  if ((nKind == SCENE_PRIM_NORMAL || nKind == SCENE_PRIM_UNCLOAKED) && prim.nTable != SCENE_NONE &&
      prim.nTable + 256 <= pFrame->nTables) {
    draw.nTeam = pFrame->pTables[prim.nTable + 8];
    draw.bTeam = true;
    draw.nSlot = pFrame->pPrimColor != NULL ? pFrame->pPrimColor[i] : -1;
    draw.nTable = prim.nTable;
    if (g_hdRemaps.find(prim.nTable) == g_hdRemaps.end())
      SDL_memcpy(g_hdRemaps[prim.nTable].data(), pFrame->pTables + prim.nTable, 256);
  }
  return true;
}

// Primitive i as a draw of its image's HD art, when it has art in a mode hd.kinds draws and
// the art is on the GPU or can go up within this frame's budget (*pnBudget). False leaves
// it as the game draws it.
static bool GpuHdDrawOf(const SceneFrame *pFrame, Uint32 i, const ScenePrim &prim, GpuHdUploads *pUp, int *pnBudget,
                        GpuHdDraw *pDraw)
{
  const unsigned int nKind = prim.nKind & 0xFF;
  const unsigned int nArt = pFrame->pPrimArt[i];
  const int nMode = nKind == SCENE_PRIM_NORMAL      ? 0
                    : nKind == SCENE_PRIM_SHADOW    ? 1
                    : nKind == SCENE_PRIM_EFFECT    ? 2
                    : nKind == SCENE_PRIM_DISTORT   ? 3
                    : nKind == SCENE_PRIM_UNCLOAKED ? 4
                    : nKind == SCENE_PRIM_WARP      ? 5
                                                    : -1;
  if (nArt == SCENE_NONE || prim.nEntry >= pFrame->nEntries || nMode < 0 ||
      (static_cast<int>(g_fHdKinds) & (1 << nMode)) == 0)
    return false;
  const PortHdArt *pArt = GpuHdArtOf(static_cast<int>(nArt & 0xFFFF));
  const int nFrame = static_cast<int>(nArt >> 16);
  if (pArt == NULL || nFrame >= static_cast<int>(pArt->frames.size()))
    return false;
  if (g_hdTex[pArt->nImage].pDiffuse == NULL || g_hdTex[pArt->nImage].nSerial != pArt->nSerial) {
    SDL_GPUCopyPass *pCopy = GpuHdCopy(pUp);
    if (pCopy == NULL || !GpuHdResident(pArt, pCopy, pnBudget))
      return false;
  }
  // the image's anchor, as CImage::DrawUpdateRect places it the other way: the frame's screen
  // box, its clip taken back off, less the GRP frame's offset, plus half the GRP's largest frame
  // (from the right when mirrored); the HD frame's own canvas is centred there
  const unsigned int nGrp = pFrame->pPrimGrp != NULL ? pFrame->pPrimGrp[i] : 0;
  if (nGrp == 0)
    return false;
  const SceneEntry &entry = pFrame->pEntries[prim.nEntry];
  const int nOffsetX = static_cast<int>(entry.nOrigin & 0xFFFF);
  const int nOffsetY = static_cast<int>(entry.nOrigin >> 16);
  const int nW = static_cast<int>(entry.nSize & 0xFFFF);
  const int nBoxX = prim.nX - static_cast<int>(prim.nClip0 & 0xFFFF);
  const int nBoxY = prim.nY - static_cast<int>(prim.nClip0 >> 16);
  const int nHalfW = static_cast<int>(nGrp & 0xFFFF) >> 1;
  const int nHalfH = static_cast<int>(nGrp >> 16) >> 1;
  GpuHdDraw &draw = *pDraw;
  SDL_zero(draw);
  draw.pArt = pArt;
  draw.nFrame = nFrame;
  draw.bMirror = (prim.nKind & SCENE_PRIM_MIRROR) != 0;
  draw.nMode = nMode;
  GpuHdLogFirst(static_cast<int>(nArt & 0xFFFF), draw, nGrp);
  draw.nAnchorX = draw.bMirror ? nBoxX + nW + nOffsetX - nHalfW : nBoxX - nOffsetX + nHalfW;
  draw.nAnchorY = nBoxY - nOffsetY + nHalfH;
  draw.bFogged = g_hdFog.bOn && i < g_hdFog.nPrim;
  draw.nTable = SCENE_NONE;
  return GpuHdDrawTables(pFrame, i, prim, pDraw);
}

// The dialogs' translucency after every HD frame (g_ahdOverTables): each blit's param takes
// its table's place plus 1 above its 8 bits.
static void GpuHdOverTablesOf(const SceneFrame *pFrame)
{
  for (Uint32 i = g_nHdOverFrom; i < pFrame->nPrims; ++i) {
    ScenePrim &blit = g_hdPrims[i];
    const Uint32 nMode = blit.nParam & 0xFF;
    if ((blit.nKind & 0xFF) != SCENE_PRIM_BLIT || nMode < 2 || blit.nEntry == SCENE_NONE ||
        blit.nEntry + 0x10000u > pFrame->nTables)
      continue;
    int k = 0;
    while (k < g_nHdOverTables && (g_ahdOverTables[k].nEntry != blit.nEntry || g_ahdOverTables[k].nMode != nMode))
      ++k;
    if (k == g_nHdOverTables) {
      if (k == 8)
        continue;  // drawn over them as it is
      GpuHdOverTable &over = g_ahdOverTables[g_nHdOverTables++];
      over.nEntry = blit.nEntry;
      over.nMode = nMode;
      over.table.assign(pFrame->pTables + blit.nEntry, pFrame->pTables + blit.nEntry + 0x10000u);
    }
    blit.nParam = nMode | static_cast<Uint32>(k + 1) << 8;
  }
}

// The frame's primitives as the GPU's map takes them: with the frame in HD, those of an
// image whose art is on the GPU become draws of that art (g_hdDraws) and are left out:
// the plain, shadow, translucent, cloaked, cloaking and warp-flash frames; the rest stay
// as the game draws them, each with the HD draws before it above its kind's 16 bits, by
// which the scene keeps the draw order (scene.hlsl, the rank).
const ScenePrim *GpuHdScene(const SceneFrame *pFrame, Uint32 nBytesAt)
{
  g_hdDraws.clear();
  GpuHdAdvance();
  g_bHdRemastered = GpuHdCan() && GpuHdRemastered();
  g_bHdScene = GpuHdWanted();
  g_nHdOverFrom = 0;
  g_nHdOverTables = 0;
  if (!g_bHdScene)
    return pFrame->pPrims;
  GpuHdModArtGeneration();
  GpuHdSceneView(pFrame);
  const Uint32 nMapPrims = g_hdZoom.bOn ? pFrame->nMapPrims : pFrame->nPrims;
  GpuHdUploads up = {NULL, NULL};
  if (g_bHdRemastered)
    GpuHdTerrainUp(pFrame, &up);
  else
    g_pHdTerrain = NULL;  // a mod's art alone: the classic terrain and console
  GpuHdConsoleUp(&up);
  GpuHdFogOf(pFrame, nMapPrims, nBytesAt);
  if (pFrame->pPrimArt != NULL) {
    g_hdPrims.assign(pFrame->pPrims, pFrame->pPrims + pFrame->nPrims);
    int nBudget = 4;
    for (Uint32 i = 0; i < nMapPrims; ++i) {
      GpuHdDraw draw;
      if (!GpuHdDrawOf(pFrame, i, g_hdPrims[i], &up, &nBudget, &draw)) {
        const Uint32 nRank = static_cast<Uint32>(SDL_min(g_hdDraws.size(), static_cast<size_t>(0xFFFF)));
        g_hdPrims[i].nKind = (g_hdPrims[i].nKind & 0xFFFFu) | nRank << 16;
        continue;
      }
      draw.nRank = static_cast<Uint32>(g_hdDraws.size());
      g_hdDraws.push_back(draw);
      g_hdPrims[i].nKind = 0;  // no kind: the GPU's map leaves it out
      g_nHdOverFrom = i + 1;
    }
    GpuHdOverTablesOf(pFrame);
  }
  if (up.pCopy != NULL)
    SDL_EndGPUCopyPass(up.pCopy);
  return pFrame->pPrimArt != NULL && pFrame->nPrims != 0 ? g_hdPrims.data() : pFrame->pPrims;
}

// A key for what a fit was made of: FNV-1a over the bytes, from nFrom.
static Uint64 GpuHdKey(const void *pData, size_t nSize, Uint64 nFrom)
{
  Uint64 h = nFrom != 0 ? nFrom : 14695981039346656037ull;
  const Uint8 *p = static_cast<const Uint8 *>(pData);
  for (size_t i = 0; i < nSize; ++i)
    h = (h ^ p[i]) * 1099511628211ull;
  return h;
}

// The colour matrix m that best takes each palette colour v to the colour pTable[v],
// weighted by padWeight[v], by least squares; the identity when the weights say nothing.

void GpuHdFitMatrix(const SDL_Color *pColors, const Uint8 *pTable, const double *padWeight, double m[3][3])
{
  double cc[3][3] = {}, oc[3][3] = {};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      m[i][j] = i == j ? 1.0 : 0.0;
  for (int v = 1; v < 256; ++v) {
    const double w = padWeight[v];
    if (w <= 0.0)
      continue;
    const SDL_Color &in = pColors[v];
    const SDL_Color &out = pColors[pTable[v]];
    const double c[3] = {in.r / 255.0, in.g / 255.0, in.b / 255.0};
    const double o[3] = {out.r / 255.0, out.g / 255.0, out.b / 255.0};
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) {
        cc[i][j] += w * c[i] * c[j];
        oc[i][j] += w * o[i] * c[j];
      }
  }
  // m = oc * cc^-1, with a little ridge so a set of few colours stays solvable
  for (int i = 0; i < 3; ++i)
    cc[i][i] += 1e-4;
  const double det = cc[0][0] * (cc[1][1] * cc[2][2] - cc[1][2] * cc[2][1]) -
                     cc[0][1] * (cc[1][0] * cc[2][2] - cc[1][2] * cc[2][0]) +
                     cc[0][2] * (cc[1][0] * cc[2][1] - cc[1][1] * cc[2][0]);
  if (SDL_fabs(det) <= 1e-12)
    return;
  double inv[3][3];
  inv[0][0] = (cc[1][1] * cc[2][2] - cc[1][2] * cc[2][1]) / det;
  inv[0][1] = (cc[0][2] * cc[2][1] - cc[0][1] * cc[2][2]) / det;
  inv[0][2] = (cc[0][1] * cc[1][2] - cc[0][2] * cc[1][1]) / det;
  inv[1][0] = (cc[1][2] * cc[2][0] - cc[1][0] * cc[2][2]) / det;
  inv[1][1] = (cc[0][0] * cc[2][2] - cc[0][2] * cc[2][0]) / det;
  inv[1][2] = (cc[0][2] * cc[1][0] - cc[0][0] * cc[1][2]) / det;
  inv[2][0] = (cc[1][0] * cc[2][1] - cc[1][1] * cc[2][0]) / det;
  inv[2][1] = (cc[0][1] * cc[2][0] - cc[0][0] * cc[2][1]) / det;
  inv[2][2] = (cc[0][0] * cc[1][1] - cc[0][1] * cc[1][0]) / det;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      m[i][j] = oc[i][0] * inv[0][j] + oc[i][1] * inv[1][j] + oc[i][2] * inv[2][j];
}

// A matrix as the shaders take it, three rows of four, scaled from the identity by k.
void GpuHdStoreMatrix(const double m[3][3], double k, float *pafRows)
{
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j)
      pafRows[i * 4 + j] = static_cast<float>((i == j ? 1.0 : 0.0) + (m[i][j] - (i == j ? 1.0 : 0.0)) * k);
    pafRows[i * 4 + 3] = 0.0f;
  }
}

// The fog over the HD frames, each of its 32 shades as a colour matrix fitted by least
// squares: what each palette colour becomes through the shade's table, weighted by how
// much the colour counts (padWeight), since 1.17's tables shift a colour's hue as well as
// darkening it where the palette has no darker one alike. The terrain's weights are how
// often the tileset's cells use each colour (g_adHdTerrainUse), the sprites' all alike. 31
// is left as it is; hd.fog scales each matrix toward that.
static void GpuHdFogMatrices(const SDL_Color *pColors, const double *padWeight, float *pafMat)
{
  for (int k = 0; k < 32; ++k) {
    double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    if (k < 31 && g_hdFog.bOn)
      GpuHdFitMatrix(pColors, &g_hdFog.abShades[k * 256], padWeight, m);
    GpuHdStoreMatrix(m, g_fHdFog, &pafMat[k * 12]);
  }
}

// A translucency table, out = table[v * 256 + d], as out = a * colour v + b * colour d by
// least squares over its rows and every d, a for each channel and b one for all, since the
// blend takes one alpha: a glow comes out near 1 and 1, the cloak's dimmer.
void GpuHdFitEffect(const SDL_Color *pColors, const std::vector<Uint8> &table, float *pafOut)
{
  double ab[3][2] = {};
  const int nRows = static_cast<int>(table.size() / 256);
  for (int ch = 0; ch < 3; ++ch) {
    double ss = 0.0, sd = 0.0, dd = 0.0, so = 0.0, dout = 0.0;
    for (int v = 1; v < nRows; ++v) {
      const SDL_Color &cs = pColors[v];
      const double s = (ch == 0 ? cs.r : ch == 1 ? cs.g : cs.b) / 255.0;
      for (int d = 1; d < 256; ++d) {
        const SDL_Color &cd = pColors[d];
        const SDL_Color &co = pColors[table[v * 256 + d]];
        const double u = (ch == 0 ? cd.r : ch == 1 ? cd.g : cd.b) / 255.0;
        const double o = (ch == 0 ? co.r : ch == 1 ? co.g : co.b) / 255.0;
        ss += s * s;
        sd += s * u;
        dd += u * u;
        so += s * o;
        dout += u * o;
      }
    }
    const double det = ss * dd - sd * sd;
    ab[ch][0] = det > 1e-9 ? (so * dd - dout * sd) / det : 1.0;
    ab[ch][1] = det > 1e-9 ? (ss * dout - sd * so) / det : 1.0;
  }
  for (int ch = 0; ch < 3; ++ch)
    pafOut[ch] = static_cast<float>(SDL_max(ab[ch][0], 0.0));
  pafOut[3] = static_cast<float>(SDL_min(SDL_max((ab[0][1] + ab[1][1] + ab[2][1]) / 3.0, 0.0), 1.0));
}

// A dialog's translucency table (mode 2: the dialog's pixel high), for each pixel of the
// dialog's v, as colour table[v, d] = c + b * colour d over every d by least squares, b one
// for all three channels: 0 where the dialog's pixel hides the map, toward 1 where it shows.
static void GpuHdFitShares(const SDL_Color *pColors, const std::vector<Uint8> &table, Uint32 nMode, float *pafOut)
{
  double adX[256][3], adMean[3] = {};
  for (int d = 0; d < 256; ++d)
    for (int ch = 0; ch < 3; ++ch) {
      adX[d][ch] = (ch == 0 ? pColors[d].r : ch == 1 ? pColors[d].g : pColors[d].b) / 255.0;
      adMean[ch] += adX[d][ch] / 256.0;
    }
  double dVar = 0.0;
  for (int d = 0; d < 256; ++d)
    for (int ch = 0; ch < 3; ++ch)
      dVar += (adX[d][ch] - adMean[ch]) * (adX[d][ch] - adMean[ch]);
  for (int v = 0; v < 256; ++v) {
    double adOutMean[3] = {};
    for (int d = 0; d < 256; ++d) {
      const int o = table[nMode == 2 ? v * 256 + d : d * 256 + v];
      for (int ch = 0; ch < 3; ++ch)
        adOutMean[ch] += adX[o][ch] / 256.0;
    }
    double dCov = 0.0;
    for (int d = 0; d < 256; ++d) {
      const int o = table[nMode == 2 ? v * 256 + d : d * 256 + v];
      for (int ch = 0; ch < 3; ++ch)
        dCov += (adX[d][ch] - adMean[ch]) * (adX[o][ch] - adOutMean[ch]);
    }
    pafOut[v] = dVar > 1e-9 ? static_cast<float>(SDL_min(SDL_max(dCov / dVar, 0.0), 1.0)) : 0.0f;
  }
}

// What 1.17 takes from each colour of an SD frame (pnValue), by brightness for the HD
// frame: 32 steps from black to white, each the mean of the palette's colours that bright,
// the steps no colour falls in taken from the nearest that has one.
static void GpuHdBrightnessTable(const SDL_Color *pColors, const Sint16 *pnValue, float *pafOut)
{
  // the last one made, as the distortions and cloaking units of a frame take one or two
  static SDL_Color s_aColors[256];
  static Sint16 s_anValue[256];
  static float s_afOut[32];
  static bool s_bHeld;
  if (s_bHeld && SDL_memcmp(s_anValue, pnValue, sizeof s_anValue) == 0 &&
      SDL_memcmp(s_aColors, pColors, sizeof s_aColors) == 0) {
    SDL_memcpy(pafOut, s_afOut, sizeof s_afOut);
    return;
  }
  double adSum[32] = {}, adCount[32] = {};
  for (int v = 1; v < 256; ++v) {
    const double l = (pColors[v].r * 0.2126 + pColors[v].g * 0.7152 + pColors[v].b * 0.0722) / 255.0;
    const int n = SDL_min(static_cast<int>(l * 31.0 + 0.5), 31);
    adSum[n] += pnValue[v];
    adCount[n] += 1.0;
  }
  for (int n = 0; n < 32; ++n) {
    int nNear = -1;
    for (int d = 0; d < 32 && nNear < 0; ++d)
      nNear = n - d >= 0 && adCount[n - d] > 0.0 ? n - d : n + d < 32 && adCount[n + d] > 0.0 ? n + d : -1;
    pafOut[n] = nNear < 0 ? 0.0f : static_cast<float>(adSum[nNear] / adCount[nNear]);
  }
  SDL_memcpy(s_aColors, pColors, sizeof s_aColors);
  SDL_memcpy(s_anValue, pnValue, sizeof s_anValue);
  SDL_memcpy(s_afOut, pafOut, sizeof s_afOut);
  s_bHeld = true;
}

// The frame's lights, for the HD lighting, uploaded before the pass begins; how many.
static int GpuHdLightsUp(SDL_GPUCommandBuffer *pCmd)
{
  if (g_fHdLighting <= 0.0f)
    return 0;
  if (g_hdLightBuf == NULL) {
    SDL_GPUBufferCreateInfo info;
    SDL_zero(info);
    info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    info.size = sizeof(PortLightGpu) * PORT_LIGHTS_MAX;
    g_hdLightBuf = SDL_CreateGPUBuffer(g_device, &info);
    g_hdLightUp = GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, sizeof(PortLightGpu) * PORT_LIGHTS_MAX);
  }
  static PortLightGpu s_lights[PORT_LIGHTS_MAX];
  const int nLights = g_hdLightBuf != NULL && g_hdLightUp != NULL ? PortLightsFrame(s_lights, PORT_LIGHTS_MAX) : 0;
  // they are the map layer's, the render view's when zoomed
  for (int k = 0; g_hdZoom.bOn && k < nLights; ++k) {
    s_lights[k].fX = (s_lights[k].fX - g_hdZoom.fOffX) / g_hdZoom.fZoom;
    s_lights[k].fY = (s_lights[k].fY - g_hdZoom.fOffY) / g_hdZoom.fZoom;
    s_lights[k].fRadius /= g_hdZoom.fZoom;
  }
  void *pMapped = nLights > 0 ? SDL_MapGPUTransferBuffer(g_device, g_hdLightUp, true) : NULL;
  if (pMapped == NULL)
    return 0;
  SDL_memcpy(pMapped, s_lights, sizeof(PortLightGpu) * nLights);
  SDL_UnmapGPUTransferBuffer(g_device, g_hdLightUp);
  SDL_GPUCopyPass *pCopy = SDL_BeginGPUCopyPass(pCmd);
  SDL_GPUTransferBufferLocation from;
  SDL_GPUBufferRegion to;
  from.transfer_buffer = g_hdLightUp;
  from.offset = 0;
  to.buffer = g_hdLightBuf;
  to.offset = 0;
  to.size = static_cast<Uint32>(sizeof(PortLightGpu) * nLights);
  SDL_UploadToGPUBuffer(pCopy, &from, &to, true);
  SDL_EndGPUCopyPass(pCopy);
  return nLights;
}

// The base pass's bindings and uniforms (hd.hlsl, PSHdBase): drawn first, the SD frame
// scaled up with the HD terrain where the map shows only terrain, and again after the
// sprites with fOnlyOver, the software's pixels over the map put back on top.
struct GpuHdBasePass {
  SDL_GPUTextureSamplerBinding aTex[8];
  SDL_GPUBuffer *apBuf[8];  // [1] is what the scene drew after every HD frame, or the cover
  HdBaseParams params;
  // hd.hlsl's BasePalette, BaseShares and BaseSharesMore, pushed apart from the params: SDL's
  // Vulkan backend binds 4 KB of a block
  float afPalette[256 * 4];
  float afOverShare[8 * 256];  // each dialog translucency's share of what is under it, by the
                               // dialog's pixel
};

static void GpuHdBaseSetUp(const SDL_Color *pColors, bool bGround, SDL_GPUTexture *pLayer, GpuHdBasePass *pBase)
{
  const int s = PortHdScale();
  HdBaseParams &base = pBase->params;
  const bool bFrameBytes = g_sceneStore[SCENE_STORE_FRAME].pBuffer != NULL;
  const bool bTerrain = g_fHdTerrain >= 0.5f && g_pHdTerrain != NULL && g_hdTerrainTex != NULL && bFrameBytes &&
                        g_sceneStore[SCENE_STORE_TOUCHED].pBuffer != NULL &&
                        g_sceneStore[SCENE_STORE_SLOTS].pBuffer != NULL;
  const bool bWater = bTerrain && g_fHdWater > 0.0f && g_hdWaterTex != NULL && g_ahdNormalTex[0] != NULL &&
                      g_ahdNormalTex[1] != NULL && g_hdWaterOf != NULL;
  const bool bStars = bTerrain && g_hdStarTex != NULL && g_hdStars != NULL;
  pBase->aTex[0] = {g_frame, g_nearest};
  pBase->aTex[1] = {bTerrain ? g_hdTerrainTex : g_frame, g_nearest};
  pBase->aTex[2] = {bTerrain && g_hdCreepTex != NULL ? g_hdCreepTex : g_frame, g_nearest};
  pBase->aTex[3] = {bWater ? g_hdWaterTex : g_frame, g_nearest};
  pBase->aTex[4] = {bWater ? g_ahdNormalTex[0] : g_frame, g_linear};
  pBase->aTex[5] = {bWater ? g_ahdNormalTex[1] : g_frame, g_linear};
  pBase->aTex[6] = {bStars ? g_hdStarTex : g_frame, g_nearest};
  // zoomed out, the map from the scene's layer, which the screen's copy has at its own size
  const bool bLayer = pLayer != NULL && g_hdZoom.bOn && g_hdZoom.nMapW == g_nGroundW;
  pBase->aTex[7] = {bLayer ? pLayer : g_frame, g_linear};
  // where the dialogs drew over the zoomed map, at the screen's size (CSHud)
  const bool bHud = g_hdZoom.bOn && g_sceneStore[SCENE_STORE_HUD].pBuffer != NULL &&
                    g_sceneStore[SCENE_STORE_HUD].nCapacity >= static_cast<Uint32>(g_nWidth * g_nHeight);
  SDL_GPUBuffer **apBase = pBase->apBuf;
  for (int k = 0; k < 8; ++k)
    apBase[k] = g_cover;
  apBase[4] = bWater ? g_hdWaterOf : g_cover;
  apBase[5] = bStars ? g_hdStars : g_cover;
  apBase[7] = bHud ? g_sceneStore[SCENE_STORE_HUD].pBuffer : g_cover;
  SDL_zero(base);
  SDL_zeroa(pBase->afOverShare);
  base.fScale = static_cast<float>(s);
  base.fHasCover = 1.0f;
  base.afMapAt[0] = g_hdZoom.fOffX;
  base.afMapAt[1] = g_hdZoom.fOffY;
  base.afMapAt[2] = g_hdZoom.fZoom;
  base.nHudOn = bHud ? 1u : 0u;
  base.nMapStride = static_cast<Uint32>(g_hdZoom.bOn ? g_hdZoom.nMapW : g_nWidth);
  base.nMapStep = static_cast<Uint32>(g_hdZoom.bOn ? g_hdZoom.nStep : 1);
  base.nScreenRows = static_cast<Uint32>(g_hdZoom.nScreenRows);
  base.nAllMap = bGround ? 1u : 0u;
  base.nLayerOn = bLayer ? 1u : 0u;
  // the resource icons' boxes, once their HD icons are up to be drawn there (GpuHdUiItems)
  if (g_pHdConsole != NULL && g_apHdUiUp[PORT_HD_UI_RESOURCES] != NULL) {
    base.nMapBoxes = static_cast<Uint32>(g_nHdMapBoxes);
    SDL_memcpy(base.anMapBox, g_aanHdMapBoxes, sizeof(int) * 4 * g_nHdMapBoxes);
  }
  base.nRows = static_cast<Uint32>(g_nHdRows);
  // what the scene drew after every HD frame goes back over them at the end
  if (g_sceneStore[SCENE_STORE_TOUCHED].pBuffer != NULL) {
    apBase[1] = g_sceneStore[SCENE_STORE_TOUCHED].pBuffer;
    base.nOverOn = 1;
    base.nRows = static_cast<Uint32>(g_nHdRows);
    base.nUnderAt = g_nHdUnderAt;
    // each dialog translucency's share of the pixel under it, fitted again when the
    // palette changes
    static SDL_Color s_aColors[256];
    static float s_afShares[8][256];
    static std::vector<Uint8> s_aTables[8];
    static Uint32 s_anMode[8];
    const bool bPalette = SDL_memcmp(s_aColors, pColors, sizeof s_aColors) != 0;
    if (bPalette)
      SDL_memcpy(s_aColors, pColors, sizeof s_aColors);
    for (int k = 0; k < g_nHdOverTables; ++k) {
      const GpuHdOverTable &over = g_ahdOverTables[k];
      if (bPalette || s_anMode[k] != over.nMode || s_aTables[k] != over.table) {
        GpuHdFitShares(pColors, over.table, over.nMode, s_afShares[k]);
        s_aTables[k] = over.table;
        s_anMode[k] = over.nMode;
      }
      SDL_memcpy(&pBase->afOverShare[k * 256], s_afShares[k], sizeof s_afShares[k]);
    }
  }
  for (int v = 0; v < 256; ++v) {
    pBase->afPalette[v * 4] = pColors[v].r / 255.0f;
    pBase->afPalette[v * 4 + 1] = pColors[v].g / 255.0f;
    pBase->afPalette[v * 4 + 2] = pColors[v].b / 255.0f;
    pBase->afPalette[v * 4 + 3] = 0.0f;
  }
  // where 1.17's console has art and Remastered's has none, the map, when nothing is
  // drawn over the art there (hd.hlsl, IsMap)
  const bool bHole = g_fHdConsole >= 0.5f && g_hdConsolePipeline != NULL && g_pHdConsole != NULL &&
                     g_hdConsoleHole != NULL && g_nHoleW == g_nWidth && g_nHoleH == g_nHeight;
  if (bHole) {
    apBase[6] = g_hdConsoleHole;
    base.nHoleOn = 1;
  }
  if (!bTerrain)
    return;
  apBase[2] = g_sceneStore[SCENE_STORE_SLOTS].pBuffer;
  apBase[3] = g_sceneStore[SCENE_STORE_FRAME].pBuffer;
  base.fTerrainOn = 1.0f;
  base.nScrollX = static_cast<Uint32>(g_hdView.nScrollX);
  base.nScrollY = static_cast<Uint32>(g_hdView.nScrollY);
  base.nRingX = static_cast<Uint32>(g_hdView.nRingX);
  base.nRingY = static_cast<Uint32>(g_hdView.nRingY);
  base.nRows = static_cast<Uint32>(g_nHdRows);
  base.nTiles = static_cast<Uint32>(g_pHdTerrain->nTiles);
  base.nTileSize = static_cast<Uint32>(g_pHdTerrain->nTileSize);
  base.fFogOn = g_hdFog.bOn ? 1.0f : 0.0f;
  base.nFogBase = g_hdFog.nBase;
  base.nFogStride = g_hdFog.nStride;
  base.nWidth = static_cast<Uint32>(g_hdFog.nMapWidth);
  GpuHdFogMatrices(pColors, g_adHdTerrainUse, base.afFogMat);
  if (bStars) {
    // 1.17's field is 648 x 488 game pixels from 8 above and left of the view; StarsDraw
    // gives the file's layer k accumulator 4 - k
    const PortHdTerrain &t = *g_pHdTerrain;
    base.anStarField[0] = static_cast<Uint32>(t.nStarFieldW);
    base.anStarField[1] = static_cast<Uint32>(t.nStarFieldH);
    base.anStarField[2] = static_cast<Uint32>(t.nStarLayers);
    base.anStarField[3] = static_cast<Uint32>(8 * s);
    base.anStarGrid[0] = static_cast<Uint32>(t.nStarBin);
    base.anStarGrid[1] = static_cast<Uint32>(t.nStarBinsX);
    base.anStarGrid[2] = static_cast<Uint32>(t.nStarBinsY);
    base.anStarGrid[3] = t.nStarsAt;
    for (int k = 0; k < t.nStarLayers && k < 5; ++k) {
      base.anStarOfs[k * 4] = g_hdView.anStarOfs[4 - k] * s;
      base.anStarOfs[k * 4 + 1] = g_hdView.anStarOfs[5 + 4 - k] * s;
    }
  }
  if (g_hdCreepTex != NULL) {
    base.nCreepCount = static_cast<Uint32>(g_pHdTerrain->nCreep);
    SDL_memcpy(base.anCreepSize, g_pHdTerrain->anCreepSize, sizeof base.anCreepSize);
  }
  if (bWater) {
    // the ripple's clock, wrapped where its scrolls come round, and the normal maps' frames
    // at 15 a second, the shader blending each with the next
    const double dTime = SDL_fmod(static_cast<double>(SDL_GetTicks()) / 1000.0, 2400.0);
    const double dFrame = dTime * 15.0;
    const Uint32 nFrame = static_cast<Uint32>(dFrame);
    base.fWaterOn = g_fHdWater;
    base.fWaterTime = static_cast<float>(dTime);
    base.fWaterFade = static_cast<float>(dFrame - SDL_floor(dFrame));
    for (int k = 0; k < 2; ++k) {
      const Uint32 nFrames = static_cast<Uint32>(SDL_max(g_pHdTerrain->anNormalFrames[k], 1));
      base.anWaterFrames[k * 2] = nFrame % nFrames;
      base.anWaterFrames[k * 2 + 1] = (nFrame + 1) % nFrames;
      base.anWaterCells[k] = static_cast<Uint32>(g_pHdTerrain->anNormalCell[k]);
    }
  }
}

static void GpuHdDrawBase(SDL_GPUCommandBuffer *pCmd, SDL_GPURenderPass *pPass, const GpuHdBasePass &base)
{
  SDL_BindGPUGraphicsPipeline(pPass, g_hdBasePipeline);
  SDL_BindGPUFragmentSamplers(pPass, 0, base.aTex, 8);
  SDL_BindGPUFragmentStorageBuffers(pPass, 0, base.apBuf, 8);
  SDL_PushGPUFragmentUniformData(pCmd, 0, &base.params, sizeof base.params);
  SDL_PushGPUFragmentUniformData(pCmd, 1, base.afPalette, sizeof base.afPalette);
  SDL_PushGPUFragmentUniformData(pCmd, 2, base.afOverShare, sizeof base.afOverShare / 2);
  SDL_PushGPUFragmentUniformData(pCmd, 3, base.afOverShare + 4 * 256, sizeof base.afOverShare / 2);
  SDL_DrawGPUPrimitives(pPass, 3, 1, 0, 0);
}

// One sprite's quad and uniforms: where its frame goes, through the camera when zoomed, its
// remap, effect, team colour and lighting, the remap's and effect's from their fits
// (GpuHdDrawSprites).
static void GpuHdSpriteParams(const GpuHdDraw &d, const SDL_Color *pColors, int nLights, HdQuadParams *pQuad,
                              HdSpriteParams *pSprite)
{
  static const float s_afIdentity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
  const int s = PortHdScale();
  const GpuHdTex &t = g_hdTex[d.pArt->nImage];
  const PortHdFrame &f = d.pArt->frames[d.nFrame];
  HdQuadParams &quad = *pQuad;
  HdSpriteParams &sprite = *pSprite;
  const int nHalfW = d.pArt->nCanvasW / 2;
  const int nHalfH = d.pArt->nCanvasH / 2;
  quad.fDstX = static_cast<float>(d.nAnchorX * s + (d.bMirror ? nHalfW - f.nOffsetX - f.nWidth : f.nOffsetX - nHalfW));
  quad.fDstY = static_cast<float>(d.nAnchorY * s + f.nOffsetY - nHalfH);
  quad.fDstW = static_cast<float>(f.nWidth);
  quad.fDstH = static_cast<float>(f.nHeight);
  if (g_hdZoom.bOn) {
    // through the camera: from the map layer's place to the screen's, smaller by the zoom
    quad.fDstX = (quad.fDstX - g_hdZoom.fOffX * s) / g_hdZoom.fZoom;
    quad.fDstY = (quad.fDstY - g_hdZoom.fOffY * s) / g_hdZoom.fZoom;
    quad.fDstW /= g_hdZoom.fZoom;
    quad.fDstH /= g_hdZoom.fZoom;
  }
  quad.fU = static_cast<float>(d.bMirror ? f.nX + f.nWidth : f.nX) / t.nWidth;
  quad.fV = static_cast<float>(f.nY) / t.nHeight;
  quad.fUw = static_cast<float>(d.bMirror ? -f.nWidth : f.nWidth) / t.nWidth;
  quad.fVh = static_cast<float>(f.nHeight) / t.nHeight;
  quad.fTargetW = static_cast<float>(g_nHdW);
  quad.fTargetH = static_cast<float>(g_nHdH);
  quad.fUnused[0] = quad.fUnused[1] = 0.0f;
  // the player's colour as Remastered takes it: the first of the 8 its table gives
  // (docs/context/graphics-formats.md); the fog when the draw is under it
  sprite.fMode = static_cast<float>(d.nMode);
  sprite.fRank = static_cast<float>(SDL_min(d.nRank, 0xFFFFu));
  sprite.fHasTeam = 0.0f;
  sprite.fFogOn = !d.bFogged ? 0.0f : g_fHdDebug >= 0.5f ? 2.0f : 1.0f;
  SDL_memcpy(sprite.afRemap, d.nTable != SCENE_NONE && d.nMode != 2 ? g_hdRemapFits.at(d.nTable).data() : s_afIdentity,
             sizeof sprite.afRemap);
  sprite.afEffect[0] = sprite.afEffect[1] = sprite.afEffect[2] = sprite.afEffect[3] = 1.0f;
  if (d.nMode == 2 && d.nTable != SCENE_NONE)
    SDL_memcpy(sprite.afEffect, g_hdEffectFits.at(d.nTable).fit.data(), sizeof sprite.afEffect);
  if (d.nMode == 3 || d.nMode == 4)
    GpuHdBrightnessTable(pColors, d.anValue, sprite.afBright);
  sprite.fThreshold = static_cast<float>(d.nThreshold);
  sprite.fMirror = d.bMirror ? 1.0f : 0.0f;
  sprite.nWarpTable = d.nWarpTable;
  sprite.nWarpW = static_cast<Uint32>(d.nWarpW);
  sprite.nWarpH = static_cast<Uint32>(d.nWarpH);
  sprite.nWarpX = d.nWarpX;
  sprite.nWarpY = d.nWarpY;
  Uint8 rgb[3];
  if (d.bTeam && t.pTeam != NULL && d.nSlot >= 0 && PortTeamColorGet(d.nSlot, rgb)) {
    // a slot with an RGB colour: the first of its shades (port_teamcolor.cpp)
    float afShades[8][3];
    PortTeamShades(d.nSlot, reinterpret_cast<const unsigned char *>(pColors), afShades);
    SDL_memcpy(sprite.fTeam, afShades[0], sizeof sprite.fTeam);
    sprite.fHasTeam = 1.0f;
  } else if (d.bTeam && t.pTeam != NULL) {
    const SDL_Color &c = pColors[d.nTeam];
    sprite.fTeam[0] = c.r / 255.0f;
    sprite.fTeam[1] = c.g / 255.0f;
    sprite.fTeam[2] = c.b / 255.0f;
    sprite.fHasTeam = 1.0f;
  }
  // the lighting, for a sprite whose art has a normal and an ao_depth layer
  const bool bLit = g_fHdLighting > 0.0f && (d.nMode == 0 || d.nMode == 4) && t.apLit[1] != NULL &&
                    t.apLit[3] != NULL;
  sprite.fLit = bLit ? g_fHdLighting : 0.0f;
  sprite.fEmissive = bLit && t.apLit[0] != NULL ? 1.0f : 0.0f;
  sprite.fLightScale = static_cast<float>(s);
  sprite.nLights = static_cast<Uint32>(nLights);
  sprite.fTargetW = static_cast<float>(g_nHdW);
  sprite.fTargetH = static_cast<float>(g_nHdH);
  sprite.fLightStrength = g_fHdLightStrength;
}

// The sprites' frame-wide uniforms and fits, for the frame's draws (GpuHdSpritesUp): the
// fog over them, the palette, and each remap's and translucent table's fit.
static float g_afHdSpriteFog[32 * 12];
static float g_afHdPalette[256 * 4];  // for the warp flash's texture

static void GpuHdSpriteFits(const SDL_Color *pColors)
{
  // the sprites' fog, each colour alike, fitted again when its shades, the palette or hd.fog
  // change
  static double s_adEvery[256];
  if (s_adEvery[1] == 0.0)
    for (int v = 1; v < 256; ++v)
      s_adEvery[v] = 1.0;
  const Uint64 nPalette = GpuHdKey(pColors, 256 * sizeof(SDL_Color), 0);
  Uint64 nFog = GpuHdKey(g_hdFog.abShades, sizeof g_hdFog.abShades, nPalette);
  nFog = GpuHdKey(&g_fHdFog, sizeof g_fHdFog, GpuHdKey(&g_hdFog.bOn, sizeof g_hdFog.bOn, nFog));
  static Uint64 s_nSpriteFog;
  if (nFog != s_nSpriteFog) {
    s_nSpriteFog = nFog;
    GpuHdFogMatrices(pColors, s_adEvery, g_afHdSpriteFog);
  }
  for (int v = 0; v < 256; ++v) {
    g_afHdPalette[v * 4] = pColors[v].r / 255.0f;
    g_afHdPalette[v * 4 + 1] = pColors[v].g / 255.0f;
    g_afHdPalette[v * 4 + 2] = pColors[v].b / 255.0f;
    g_afHdPalette[v * 4 + 3] = 1.0f;
  }
  // each remap's colour matrix, fitted over the colours a remap may change (not 0, and not 8
  // to 15, the player's, which the team layer gives)
  static double s_adRemapWeight[256];
  if (s_adRemapWeight[1] == 0.0)
    for (int v = 1; v < 256; ++v)
      s_adRemapWeight[v] = v >= 8 && v < 16 ? 0.0 : 1.0;
  if (nPalette != g_nHdFitPalette) {
    g_nHdFitPalette = nPalette;
    g_hdRemapFits.clear();
    g_hdEffectFits.clear();
  }
  for (const GpuHdDraw &d : g_hdDraws)
    if (d.nMode == 2 && d.nTable != SCENE_NONE) {
      const std::vector<Uint8> &table = g_hdEffects[d.nTable];
      GpuHdEffectFit &fit = g_hdEffectFits[d.nTable];
      if (fit.nSize != table.size()) {
        fit.nSize = table.size();
        GpuHdFitEffect(pColors, table, fit.fit.data());
      }
    }
  for (const GpuHdDraw &d : g_hdDraws)
    if (d.nTable != SCENE_NONE && d.nMode != 2 && g_hdRemapFits.find(d.nTable) == g_hdRemapFits.end()) {
      double m[3][3];
      GpuHdFitMatrix(pColors, g_hdRemaps[d.nTable].data(), s_adRemapWeight, m);
      GpuHdStoreMatrix(m, 1.0, g_hdRemapFits[d.nTable].data());
    }
}

// The pass's uniforms every sprite shares, the rest GpuHdSpriteParams' for each.
static void GpuHdSpritePass(HdSpriteParams *pSprite)
{
  SDL_zerop(pSprite);
  pSprite->fFogOn = g_hdFog.bOn ? 1.0f : 0.0f;
  pSprite->fFogScale = static_cast<float>(PortHdScale());
  pSprite->nFogBase = g_hdFog.nBase;
  pSprite->nFogStride = g_hdFog.nStride;
  pSprite->nMapWidth = static_cast<Uint32>(g_hdFog.nMapWidth);
  pSprite->nMapRows = static_cast<Uint32>(g_nHdRows);
  pSprite->afMapAt[0] = g_hdZoom.fOffX;
  pSprite->afMapAt[1] = g_hdZoom.fOffY;
  pSprite->afMapAt[2] = g_hdZoom.fZoom;
  // the draw order: the scene's rank of each pixel of the map layer, after its three planes
  // (scene.hlsl); none when the scene kept none
  pSprite->nRankAt = g_sceneStore[SCENE_STORE_TOUCHED].pBuffer != NULL ? 3u * g_nHdUnderAt : 0u;
  pSprite->nRankLayerW = static_cast<Uint32>(g_hdView.nLayerW);
  pSprite->nRankStep = static_cast<Uint32>(SDL_max(g_hdZoom.nStep, 1));
}

// Whether a draw goes in a batch (PSHdBatch): a plain, shadow or translucent frame, unlit.
static bool GpuHdBatchable(const GpuHdDraw &d)
{
  if (d.nMode > 2 || g_hdBatchPipeline == NULL)
    return false;
  const GpuHdTex &t = g_hdTex[d.pArt->nImage];
  return !(g_fHdLighting > 0.0f && d.nMode == 0 && t.apLit[1] != NULL && t.apLit[3] != NULL);
}

// Each draw a run of its own (GpuHdRun), as before the batches.
static void GpuHdRunsSingly(void)
{
  g_hdRuns.clear();
  g_hdRecs.clear();
  for (Uint32 i = 0; i < g_hdDraws.size(); ++i) {
    GpuHdRun one;
    SDL_zero(one);
    one.nDraw = i;
    one.nCount = 1;
    g_hdRuns.push_back(one);
  }
}

// The frame's draws as runs (g_hdRuns), the batches' records uploaded to g_hdRecBuf, before
// the pass begins; the fits first. A batch takes draws in order while they need no more
// than HD_BATCH_ARTS arts; a draw that cannot go in one is a run of its own.
static void GpuHdSpritesUp(SDL_GPUCommandBuffer *pCmd, const SDL_Color *pColors, int nLights)
{
  GpuHdSpriteFits(pColors);
  g_hdRuns.clear();
  g_hdRecs.clear();
  HdSpriteParams sprite;
  GpuHdSpritePass(&sprite);
  for (Uint32 i = 0; i < g_hdDraws.size(); ++i) {
    const GpuHdDraw &d = g_hdDraws[i];
    if (!GpuHdBatchable(d)) {
      GpuHdRun one;
      SDL_zero(one);
      one.nDraw = i;
      one.nCount = 1;
      g_hdRuns.push_back(one);
      continue;
    }
    // the art's place in the batch going on, or a new batch
    const GpuHdTex *pTex = &g_hdTex[d.pArt->nImage];
    GpuHdRun *pRun = g_hdRuns.empty() || g_hdRuns.back().nArts == 0 ? NULL : &g_hdRuns.back();
    int nSlot = 0;
    while (pRun != NULL && nSlot < pRun->nArts && pRun->apArt[nSlot] != pTex)
      ++nSlot;
    if (pRun != NULL && nSlot == HD_BATCH_ARTS)
      pRun = NULL;
    if (pRun == NULL) {
      GpuHdRun run;
      SDL_zero(run);
      run.nDraw = i;
      run.nRec = static_cast<Uint32>(g_hdRecs.size());
      g_hdRuns.push_back(run);
      pRun = &g_hdRuns.back();
      nSlot = 0;
    }
    if (nSlot == pRun->nArts)
      pRun->apArt[pRun->nArts++] = pTex;
    ++pRun->nCount;
    HdQuadParams quad;
    GpuHdSpriteParams(d, pColors, nLights, &quad, &sprite);
    HdSpriteRec rec;
    rec.afDst[0] = quad.fDstX;
    rec.afDst[1] = quad.fDstY;
    rec.afDst[2] = quad.fDstW;
    rec.afDst[3] = quad.fDstH;
    rec.afUv[0] = quad.fU;
    rec.afUv[1] = quad.fV;
    rec.afUv[2] = quad.fUw;
    rec.afUv[3] = quad.fVh;
    SDL_memcpy(rec.afTeam, sprite.fTeam, sizeof sprite.fTeam);
    rec.afTeam[3] = sprite.fMode;
    rec.afFlags[0] = sprite.fHasTeam;
    rec.afFlags[1] = sprite.fFogOn;
    rec.afFlags[2] = sprite.fRank;
    rec.afFlags[3] = static_cast<float>(nSlot);
    SDL_memcpy(rec.afRemap, sprite.afRemap, sizeof rec.afRemap);
    SDL_memcpy(rec.afEffect, sprite.afEffect, sizeof rec.afEffect);
    g_hdRecs.push_back(rec);
  }
  if (g_hdRecs.empty())
    return;
  const Uint32 nBytes = static_cast<Uint32>(g_hdRecs.size() * sizeof(HdSpriteRec));
  if (nBytes > g_nHdRecCap) {
    if (g_hdRecBuf != NULL)
      SDL_ReleaseGPUBuffer(g_device, g_hdRecBuf);
    if (g_hdRecUp != NULL)
      SDL_ReleaseGPUTransferBuffer(g_device, g_hdRecUp);
    Uint32 nCap = 64 * 1024;
    while (nCap < nBytes)
      nCap *= 2;
    SDL_GPUBufferCreateInfo info;
    SDL_zero(info);
    info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    info.size = nCap;
    g_hdRecBuf = SDL_CreateGPUBuffer(g_device, &info);
    g_hdRecUp = GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, nCap);
    g_nHdRecCap = g_hdRecBuf != NULL && g_hdRecUp != NULL ? nCap : 0;
  }
  void *pMapped = g_nHdRecCap != 0 ? SDL_MapGPUTransferBuffer(g_device, g_hdRecUp, true) : NULL;
  if (pMapped == NULL) {
    GpuHdRunsSingly();
    return;
  }
  SDL_memcpy(pMapped, g_hdRecs.data(), nBytes);
  SDL_UnmapGPUTransferBuffer(g_device, g_hdRecUp);
  SDL_GPUCopyPass *pCopy = SDL_BeginGPUCopyPass(pCmd);
  SDL_GPUTransferBufferLocation from;
  SDL_GPUBufferRegion to;
  from.transfer_buffer = g_hdRecUp;
  from.offset = 0;
  to.buffer = g_hdRecBuf;
  to.offset = 0;
  to.size = nBytes;
  SDL_UploadToGPUBuffer(pCopy, &from, &to, true);
  SDL_EndGPUCopyPass(pCopy);
}

// What the target has had drawn on it since g_hdPrev was copied from it, in squares of
// HD_DRAWN_SQUARE of its pixels: a distortion whose reach meets none of them reads
// g_hdPrev as the target is, and the copy before it is left out. Zoomed out the frame had a
// copy of the whole target for each distortion, its pass ended and begun again around it.
enum { HD_DRAWN_SQUARE = 32 };
struct GpuHdDrawn {
  int nW, nH;                   // squares across and down
  std::vector<Uint32> squares;  // the stamp of the last draw over each
  Uint32 nStamp, nCopied;       // the draws so far, and their count at the copy
};

static void GpuHdDrawnStart(GpuHdDrawn *pDrawn)
{
  pDrawn->nW = (g_nHdW + HD_DRAWN_SQUARE - 1) / HD_DRAWN_SQUARE;
  pDrawn->nH = (g_nHdH + HD_DRAWN_SQUARE - 1) / HD_DRAWN_SQUARE;
  pDrawn->squares.assign(static_cast<size_t>(pDrawn->nW) * pDrawn->nH, 0);
  pDrawn->nStamp = 1;
  pDrawn->nCopied = 0;  // nothing copied: g_hdPrev holds another frame
}

// The squares of the target's pixels x0 to x1 and y0 to y1 (each the first and last) that it
// meets, inside the target; false when it is outside.
static bool GpuHdDrawnSquares(const GpuHdDrawn &drawn, float x0, float y0, float x1, float y1, int *pnSx0,
                              int *pnSy0, int *pnSx1, int *pnSy1)
{
  const int nX0 = SDL_max(static_cast<int>(SDL_floorf(x0)), 0);
  const int nY0 = SDL_max(static_cast<int>(SDL_floorf(y0)), 0);
  const int nX1 = SDL_min(static_cast<int>(SDL_ceilf(x1)), g_nHdW - 1);
  const int nY1 = SDL_min(static_cast<int>(SDL_ceilf(y1)), g_nHdH - 1);
  if (nX0 > nX1 || nY0 > nY1)
    return false;
  *pnSx0 = nX0 / HD_DRAWN_SQUARE;
  *pnSy0 = nY0 / HD_DRAWN_SQUARE;
  *pnSx1 = SDL_min(nX1 / HD_DRAWN_SQUARE, drawn.nW - 1);
  *pnSy1 = SDL_min(nY1 / HD_DRAWN_SQUARE, drawn.nH - 1);
  return true;
}

// A draw over the quad at x, y, w by h.
static void GpuHdDrawnMark(GpuHdDrawn *pDrawn, float x, float y, float w, float h)
{
  int sx0, sy0, sx1, sy1;
  if (w < 0.0f) {
    x += w;
    w = -w;
  }
  if (!GpuHdDrawnSquares(*pDrawn, x, y, x + w, y + h, &sx0, &sy0, &sx1, &sy1))
    return;
  const Uint32 nStamp = pDrawn->nStamp++;
  for (int sy = sy0; sy <= sy1; ++sy)
    for (int sx = sx0; sx <= sx1; ++sx)
      pDrawn->squares[static_cast<size_t>(sy) * pDrawn->nW + sx] = nStamp;
}

// Whether a distortion over the quad, reading up to nReach pixels to either side along its
// rows (hd.hlsl, PSHdSprite's shift), reads anything drawn since the copy.
static bool GpuHdDrawnMeets(const GpuHdDrawn &drawn, float x, float y, float w, float h, int nReach)
{
  if (drawn.nCopied == 0)
    return true;
  if (w < 0.0f) {
    x += w;
    w = -w;
  }
  int sx0, sy0, sx1, sy1;
  if (!GpuHdDrawnSquares(drawn, x - nReach, y, x + w + nReach, y + h, &sx0, &sy0, &sx1, &sy1))
    return false;
  for (int sy = sy0; sy <= sy1; ++sy)
    for (int sx = sx0; sx <= sx1; ++sx)
      if (drawn.squares[static_cast<size_t>(sy) * drawn.nW + sx] >= drawn.nCopied)
        return true;
  return false;
}

// The frames drawn in HD, over the map's rows, in the game's order, run by run (g_hdRuns). A
// distortion reads the frame so far, so before one the pass ends, the frame is copied to
// g_hdPrev and the pass begins again over it (unless nothing drawn since the last
// copy is where it reads, GpuHdDrawn); the pass the sprites end in is returned.
static SDL_GPURenderPass *GpuHdDrawSprites(SDL_GPUCommandBuffer *pCmd, SDL_GPURenderPass *pPass,
                                           SDL_GPUColorTargetInfo *pTarget, const SDL_Color *pColors, bool bGround,
                                           int nLights)
{
  const int s = PortHdScale();
  HdSpriteParams sprite;
  GpuHdSpritePass(&sprite);
  const SDL_Rect map = {0, 0, g_nHdW, bGround ? g_nHdH : SDL_min(g_nHdH, g_hdZoom.nScreenRows * s)};
  SDL_SetGPUScissor(pPass, &map);
  SDL_GPUBuffer *apSprite[4] = {g_sceneStore[SCENE_STORE_FRAME].pBuffer, g_sceneStore[SCENE_STORE_TABLES].pBuffer,
                                g_hdLightBuf, g_sceneStore[SCENE_STORE_TOUCHED].pBuffer};
  for (int k = 1; k < 4; ++k)
    if (apSprite[k] == NULL)
      apSprite[k] = apSprite[0];
  SDL_GPUBuffer *apBatch[3] = {g_hdRecBuf, g_sceneStore[SCENE_STORE_FRAME].pBuffer, apSprite[3]};
  HdBatchQuad batchQuad;
  batchQuad.nFirst = 0;
  batchQuad.nUnused = 0;
  batchQuad.fTargetW = static_cast<float>(g_nHdW);
  batchQuad.fTargetH = static_cast<float>(g_nHdH);
  HdBatchSprite batchSprite;
  SDL_zero(batchSprite);
  batchSprite.nFogBase = sprite.nFogBase;
  batchSprite.nFogStride = sprite.nFogStride;
  batchSprite.nMapWidth = sprite.nMapWidth;
  batchSprite.nMapRows = sprite.nMapRows;
  SDL_memcpy(batchSprite.afMapAt, sprite.afMapAt, sizeof batchSprite.afMapAt);
  batchSprite.fFogScale = sprite.fFogScale;
  batchSprite.nRankAt = sprite.nRankAt;
  batchSprite.nRankLayerW = sprite.nRankLayerW;
  batchSprite.nRankStep = sprite.nRankStep;
  // what is drawn since the copy, kept only for a frame with a distortion
  static GpuHdDrawn s_drawn;
  bool bDistorts = false;
  for (const GpuHdRun &run : g_hdRuns)
    bDistorts = bDistorts || (run.nArts == 0 && g_hdDraws[run.nDraw].nMode == 3);
  if (bDistorts)
    GpuHdDrawnStart(&s_drawn);
  // the pipeline bound, with its storage and uniforms: 1 the sprites' one by one, 2 the batches'
  int nBound = 0;
  for (const GpuHdRun &run : g_hdRuns) {
    const GpuHdDraw &d = g_hdDraws[run.nDraw];
    if (run.nArts > 0) {
      if (bDistorts)
        for (Uint32 k = 0; k < run.nCount; ++k) {
          const float *pDst = g_hdRecs[run.nRec + k].afDst;
          GpuHdDrawnMark(&s_drawn, pDst[0], pDst[1], pDst[2], pDst[3]);
        }
      if (nBound != 2) {
        nBound = 2;
        SDL_BindGPUGraphicsPipeline(pPass, g_hdBatchPipeline);
        SDL_BindGPUVertexStorageBuffers(pPass, 0, apBatch, 1);
        SDL_BindGPUFragmentStorageBuffers(pPass, 0, apBatch, 3);
        SDL_PushGPUFragmentUniformData(pCmd, 0, &batchSprite, sizeof batchSprite);
        SDL_PushGPUFragmentUniformData(pCmd, 1, g_afHdSpriteFog, sizeof g_afHdSpriteFog);
      }
      // the slots past the batch's arts hold its first, as each slot must hold something
      SDL_GPUTextureSamplerBinding art[HD_BATCH_ARTS * 2];
      for (int k = 0; k < HD_BATCH_ARTS; ++k) {
        const GpuHdTex &t = *run.apArt[k < run.nArts ? k : 0];
        art[k * 2].texture = t.pDiffuse;
        art[k * 2].sampler = g_linear;
        art[k * 2 + 1].texture = t.pTeam != NULL ? t.pTeam : t.pDiffuse;
        art[k * 2 + 1].sampler = g_linear;
      }
      SDL_BindGPUFragmentSamplers(pPass, 0, art, HD_BATCH_ARTS * 2);
      batchQuad.nFirst = run.nRec;
      SDL_PushGPUVertexUniformData(pCmd, 0, &batchQuad, sizeof batchQuad);
      SDL_DrawGPUPrimitives(pPass, 6, run.nCount, 0, 0);
      continue;
    }
    const GpuHdTex &t = g_hdTex[d.pArt->nImage];
    if (d.nMode == 5 && g_sceneStore[SCENE_STORE_TABLES].pBuffer == NULL)
      continue;
    HdQuadParams quad;
    GpuHdSpriteParams(d, pColors, nLights, &quad, &sprite);
    bool bCopy = false;
    if (d.nMode == 3) {
      float fMost = 0.0f;
      for (int n = 0; n < 32; ++n)
        fMost = SDL_max(fMost, SDL_fabsf(sprite.afBright[n]));
      const int nReach = static_cast<int>(SDL_ceilf(fMost * sprite.fFogScale)) + 2;
      bCopy = GpuHdDrawnMeets(s_drawn, quad.fDstX, quad.fDstY, quad.fDstW, quad.fDstH, nReach);
    }
    if (bCopy) {
      s_drawn.nCopied = s_drawn.nStamp;
      SDL_EndGPURenderPass(pPass);
      SDL_GPUCopyPass *pCopy = SDL_BeginGPUCopyPass(pCmd);
      SDL_GPUTextureLocation from, to;
      SDL_zero(from);
      SDL_zero(to);
      from.texture = pTarget->texture;
      to.texture = g_hdPrev;
      SDL_CopyGPUTextureToTexture(pCopy, &from, &to, static_cast<Uint32>(g_nHdW), static_cast<Uint32>(g_nHdH), 1,
                                  false);
      SDL_EndGPUCopyPass(pCopy);
      pTarget->load_op = SDL_GPU_LOADOP_LOAD;
      pTarget->cycle = false;
      pPass = SDL_BeginGPURenderPass(pCmd, pTarget, 1, NULL);
      SDL_SetGPUScissor(pPass, &map);
      nBound = 0;
    }
    if (nBound != 1) {
      nBound = 1;
      SDL_BindGPUGraphicsPipeline(pPass, g_hdSpritePipeline);
      SDL_BindGPUFragmentStorageBuffers(pPass, 0, apSprite, 4);
      SDL_PushGPUFragmentUniformData(pCmd, 1, g_afHdSpriteFog, sizeof g_afHdSpriteFog);
      SDL_PushGPUFragmentUniformData(pCmd, 2, g_afHdPalette, sizeof g_afHdPalette);
    }
    if (bDistorts)
      GpuHdDrawnMark(&s_drawn, quad.fDstX, quad.fDstY, quad.fDstW, quad.fDstH);
    SDL_GPUTexture *const pAny = t.pDiffuse;
    SDL_GPUTextureSamplerBinding art[6] = {
        {t.pDiffuse, g_linear},
        {d.nMode == 3 ? g_hdPrev : t.pTeam != NULL ? t.pTeam : t.pDiffuse, d.nMode == 3 ? g_nearest : g_linear},
        {t.apLit[0] != NULL ? t.apLit[0] : pAny, g_linear},
        {t.apLit[1] != NULL ? t.apLit[1] : pAny, g_linear},
        {t.apLit[2] != NULL ? t.apLit[2] : pAny, g_linear},
        {t.apLit[3] != NULL ? t.apLit[3] : pAny, g_linear}};
    SDL_BindGPUFragmentSamplers(pPass, 0, art, 6);
    SDL_PushGPUVertexUniformData(pCmd, 0, &quad, sizeof quad);
    SDL_PushGPUFragmentUniformData(pCmd, 0, &sprite, sizeof sprite);
    SDL_DrawGPUPrimitives(pPass, 6, 1, 0, 0);
  }
  const SDL_Rect whole = {0, 0, g_nHdW, g_nHdH};
  SDL_SetGPUScissor(pPass, &whole);
  return pPass;
}

// The console's HD contents (R4f): where each item's frame goes, from its frame's size, its
// tint, and into pConsole the controls they are drawn in, which show the console instead of
// 1.17's drawing of them.
static void GpuHdUiItems(const SDL_Color *pColors, HdUiParams *pUi, HdConsoleParams *pConsole)
{
  const int s = PortHdScale();
  pUi->nCount = 0;
  pUi->nScale = static_cast<Uint32>(s);
  pUi->nTexts = static_cast<Uint32>(g_nHdTexts);
  for (int k = 0; k < g_nHdTexts; ++k) {
    const PortHdUiText &t = g_ahdTexts[k];
    pUi->anText[k * 4] = t.nX;
    pUi->anText[k * 4 + 1] = t.nY;
    pUi->anText[k * 4 + 2] = t.nW;
    pUi->anText[k * 4 + 3] = t.nH;
    SDL_memcpy(&pUi->anTextBits[k * PORT_HD_TEXT_WORDS], t.adwBits, sizeof t.adwBits);
  }
  pConsole->nOwned = 0;
  for (int k = 0; k < g_nHdItems && pUi->nCount < HD_UI_ITEMS; ++k) {
    const PortHdUiItem &item = g_ahdItems[k];
    if (item.nSet == PORT_HD_UI_PORTRAIT) {
      // the whole box, which the shader covers with the picture, cut to its shape, or a mod's
      // fits, its shape kept
      if (g_hdPortraitTex == NULL)
        continue;
      Sint32 *pDst = &pUi->anDst[pUi->nCount * 4];
      pDst[0] = item.nX * s;
      pDst[1] = item.nY * s;
      pDst[2] = item.nW * s;
      pDst[3] = item.nH * s;
      Sint32 *pSrc = &pUi->anSrc[pUi->nCount * 4];
      pSrc[0] = g_nHdPortraitW;
      pSrc[1] = g_nHdPortraitH;
      pSrc[2] = 3;
      pSrc[3] = g_bHdPortraitMod ? 1 : 0;
      float *pTint = &pUi->afTint[pUi->nCount * 4];
      pTint[0] = pTint[1] = pTint[2] = pTint[3] = 1.0f;
      ++pUi->nCount;
      if (pConsole->nOwned < HD_UI_OWNED) {
        Sint32 *pOwned = &pConsole->anOwned[pConsole->nOwned++ * 4];
        pOwned[0] = item.nX;
        pOwned[1] = item.nY;
        pOwned[2] = item.nW;
        pOwned[3] = item.nH;
      }
      continue;
    }
    const bool bMod = item.nSet == PORT_HD_UI_MOD;
    const PortHdUi *pSet = bMod ? NULL : PortHdUiSet(item.nSet);
    const bool bWire = item.nSet == PORT_HD_UI_WIRES;
    const bool bBar = item.nSet >= PORT_HD_UI_BARS && item.nSet < PORT_HD_UI_BARS + 3;
    const bool bResource = item.nSet == PORT_HD_UI_RESOURCES;
    if (bMod ? g_hdModUiTex == NULL || !g_hdModUi || g_nHdModUiSerial != g_hdModUi->nSerial
             : pSet == NULL || (bWire ? g_nHdWireType != item.nFrame : g_apHdUiUp[item.nSet] != pSet))
      continue;
    int nSrcX, nSrcY, nW, nH;
    if (bMod) {
      const auto it = g_hdModUi->frames.find(static_cast<unsigned>(item.nFrame));
      if (it == g_hdModUi->frames.end())
        continue;
      nSrcX = it->second.nX;
      nSrcY = it->second.nY;
      nW = it->second.nW;
      nH = it->second.nH;
    } else if (bWire) {
      const PortHdLayer &frame = pSet->frames[item.nFrame];
      nW = frame.nWidth / 8;
      nH = frame.nHeight;
      nSrcX = item.nCell * nW;
      nSrcY = 0;
    } else {
      if (item.nFrame < 0 || item.nFrame >= pSet->nFrames || pSet->sizes[item.nFrame] == 0)
        continue;
      nW = static_cast<int>(pSet->sizes[item.nFrame] & 0xFFFF);
      nH = static_cast<int>(pSet->sizes[item.nFrame] >> 16);
      nSrcX = item.nFrame % pSet->nRow * pSet->nCell;
      nSrcY = item.nFrame / pSet->nRow * pSet->nCell;
    }
    Sint32 *pDst = &pUi->anDst[pUi->nCount * 4];
    pDst[0] = item.nX * s + (item.nW * s - nW) / 2 + item.nDX * s;
    pDst[1] = item.nY * s + (item.nH * s - nH) / 2 + item.nDY * s;
    pDst[2] = bBar && item.nFrame == 1 ? nW * SDL_clamp(item.nCell, 0, 100) / 100 : nW;  // as far as it has come
    pDst[3] = nH;
    Sint32 *pSrc = &pUi->anSrc[pUi->nCount * 4];
    pSrc[0] = nSrcX;
    pSrc[1] = nSrcY;
    pSrc[2] = bMod ? 6 : bResource ? 5 : bBar ? 4 : bWire ? 2 : item.nSet == PORT_HD_UI_ICONS ? 0 : 1;
    pSrc[3] = bMod ? item.nCell : item.nSet == PORT_HD_UI_ICONS ? 1 : 0;
    // the tint: the brightest of the remap's colours, for an icon over Remastered's console at
    // full strength (its icons are grey, lit to the colour); a mod's over the classic one as it is
    float *pTint = &pUi->afTint[pUi->nCount * 4];
    pTint[0] = pTint[1] = pTint[2] = pTint[3] = 1.0f;
    int nBest = -1;
    for (int t = 0; t < item.nTints; ++t) {
      const SDL_Color &c = pColors[item.abTint[t]];
      const int nLuma = c.r * 299 + c.g * 587 + c.b * 114;
      if (nLuma > nBest) {
        nBest = nLuma;
        pTint[0] = c.r / 255.0f;
        pTint[1] = c.g / 255.0f;
        pTint[2] = c.b / 255.0f;
      }
    }
    const float fPeak = SDL_max(pTint[0], SDL_max(pTint[1], pTint[2]));
    const bool bIcon = item.nSet == PORT_HD_UI_ICONS ||
                       (bMod && g_pHdConsole != NULL && item.nFrame >> 16 == MOD_ART_ICON);
    if (bIcon && fPeak > 0.0f)
      for (int c = 0; c < 3; ++c)
        pTint[c] /= fPeak;
    ++pUi->nCount;
    if (bResource)  // over the map, which shows through 1.17's icon there (the map boxes)
      continue;
    bool bOwned = false;
    for (Uint32 o = 0; o < pConsole->nOwned && !bOwned; ++o)
      bOwned = pConsole->anOwned[o * 4] == item.nX && pConsole->anOwned[o * 4 + 1] == item.nY;
    if (!bOwned && pConsole->nOwned < HD_UI_OWNED) {
      Sint32 *pOwned = &pConsole->anOwned[pConsole->nOwned++ * 4];
      pOwned[0] = item.nX;
      pOwned[1] = item.nY;
      pOwned[2] = item.nW;
      pOwned[3] = item.nH;
    }
  }
}

static void GpuHdDrawUi(SDL_GPUCommandBuffer *pCmd, SDL_GPURenderPass *pPass, const HdUiParams &ui);

// Remastered's console last, where 1.17's frame has nothing of its own (R4f), and its HD
// contents over it.
static void GpuHdDrawConsole(SDL_GPUCommandBuffer *pCmd, SDL_GPURenderPass *pPass, const SDL_Color *pColors,
                             const GpuHdBasePass &base)
{
  const int s = PortHdScale();
  static HdConsoleParams s_console;
  static float s_afConsolePalette[256 * 4];  // hd.hlsl's ConsolePalette
  s_console.nPieces = static_cast<Uint32>(g_pHdConsole->nPieces);
  s_console.fScale = static_cast<float>(s);
  s_console.nArtX = static_cast<Uint32>(g_nConsoleArtX);
  s_console.nArtW = static_cast<Uint32>(g_nConsoleArtW);
  s_console.nArtH = static_cast<Uint32>(g_nConsoleArtH);
  // zoomed, gTouched is the render view's and not the screen's: the test is left out
  s_console.nRows = base.params.nOverOn != 0 && !g_hdZoom.bOn ? static_cast<Uint32>(g_nHdRows) : 0;
  SDL_GPUTextureSamplerBinding consoleTex[4];
  for (int i = 0; i < 3; ++i) {
    const bool bPiece = i < g_pHdConsole->nPieces;
    const PortHdLayer &art = g_pHdConsole->pieces[bPiece ? i : 0].art;
    s_console.anPiece[i * 4] = bPiece ? g_anPieceShift[i] * s + g_pHdConsole->pieces[i].nX : 0;
    s_console.anPiece[i * 4 + 1] = g_nHdH - art.nHeight;
    s_console.anPiece[i * 4 + 2] = art.nWidth;
    s_console.anPiece[i * 4 + 3] = art.nHeight;
    consoleTex[i] = {g_ahdConsoleTex[bPiece ? i : 0], g_nearest};
  }
  consoleTex[3] = {g_frame, g_nearest};
  for (int v = 0; v < 256; ++v) {
    s_afConsolePalette[v * 4] = pColors[v].r / 255.0f;
    s_afConsolePalette[v * 4 + 1] = pColors[v].g / 255.0f;
    s_afConsolePalette[v * 4 + 2] = pColors[v].b / 255.0f;
  }
  static HdUiParams s_ui;
  GpuHdUiItems(pColors, &s_ui, &s_console);
  SDL_GPUBuffer *apConsole[3] = {g_cover, base.apBuf[1], g_hdConsoleArt};
  SDL_BindGPUGraphicsPipeline(pPass, g_hdConsolePipeline);
  SDL_BindGPUFragmentSamplers(pPass, 0, consoleTex, 4);
  SDL_BindGPUFragmentStorageBuffers(pPass, 0, apConsole, 3);
  SDL_PushGPUFragmentUniformData(pCmd, 0, &s_console, sizeof s_console);
  SDL_PushGPUFragmentUniformData(pCmd, 1, s_afConsolePalette, sizeof s_afConsolePalette);
  SDL_DrawGPUPrimitives(pPass, 3, 1, 0, 0);
  GpuHdDrawUi(pCmd, pPass, s_ui);
}

// The console's HD contents, as GpuHdUiItems placed them.
static void GpuHdDrawUi(SDL_GPUCommandBuffer *pCmd, SDL_GPURenderPass *pPass, const HdUiParams &ui)
{
  if (ui.nCount == 0 || g_hdUiPipeline == NULL)
    return;
  SDL_GPUTexture *pAny = g_hdWireTex != NULL ? g_hdWireTex : g_frame;
  SDL_GPUTexture *pButtons = NULL;
  for (int r = 0; r < 3 && pButtons == NULL; ++r)
    pButtons = g_ahdUiTex[PORT_HD_UI_BUTTONS + r];
  SDL_GPUTexture *pBars = NULL;
  for (int r = 0; r < 3 && pBars == NULL; ++r)
    pBars = g_ahdUiTex[PORT_HD_UI_BARS + r];
  SDL_GPUTextureSamplerBinding uiTex[8];
  uiTex[0] = {g_ahdUiTex[PORT_HD_UI_ICONS] != NULL ? g_ahdUiTex[PORT_HD_UI_ICONS] : pAny, g_nearest};
  uiTex[1] = {pButtons != NULL ? pButtons : pAny, g_nearest};
  uiTex[2] = {pAny, g_nearest};
  uiTex[3] = {g_hdPortraitTex != NULL ? g_hdPortraitTex : pAny, g_linear};
  uiTex[4] = {pBars != NULL ? pBars : pAny, g_nearest};
  uiTex[5] = {g_frame, g_nearest};
  uiTex[6] = {g_ahdUiTex[PORT_HD_UI_RESOURCES] != NULL ? g_ahdUiTex[PORT_HD_UI_RESOURCES] : pAny, g_nearest};
  uiTex[7] = {g_hdModUiTex != NULL ? g_hdModUiTex : pAny, g_nearest};
  SDL_BindGPUGraphicsPipeline(pPass, g_hdUiPipeline);
  SDL_BindGPUFragmentSamplers(pPass, 0, uiTex, 8);
  SDL_PushGPUFragmentUniformData(pCmd, 0, &ui, sizeof ui);
  SDL_DrawGPUPrimitives(pPass, 3, 1, 0, 0);
}

// A mod's own icons, wireframe and portrait over the classic console.
static void GpuHdDrawModUi(SDL_GPUCommandBuffer *pCmd, SDL_GPURenderPass *pPass, const SDL_Color *pColors)
{
  static HdUiParams s_ui;
  static HdConsoleParams s_console;  // the boxes it would own, which no console pass reads
  GpuHdUiItems(pColors, &s_ui, &s_console);
  GpuHdDrawUi(pCmd, pPass, s_ui);
}

// Mid-blend: the whole SD frame over the HD frame, eased in and out (smootherstep), so the
// change starts and ends at rest.
static void GpuHdDrawBlend(SDL_GPUCommandBuffer *pCmd, SDL_GPURenderPass *pPass, SDL_GPUTexture *pSdWhole)
{
  const float m = g_fHdMix;
  const float fEased = m * m * m * (m * (m * 6.0f - 15.0f) + 10.0f);
  const float afBlend[4] = {static_cast<float>(PortHdScale()), 1.0f - fEased, 0.0f, 0.0f};
  SDL_GPUTextureSamplerBinding sd = {pSdWhole, g_nearest};
  SDL_BindGPUGraphicsPipeline(pPass, g_hdBlendPipeline);
  SDL_BindGPUFragmentSamplers(pPass, 0, &sd, 1);
  SDL_PushGPUFragmentUniformData(pCmd, 0, afBlend, sizeof afBlend);
  SDL_DrawGPUPrimitives(pPass, 3, 1, 0, 0);
}

// The frame in HD into g_hdFrame: the resolved frame scaled up, the draws over the map's
// rows, and the software's pixels over the map put back on top, then Remastered's console
// and, mid-blend, the whole SD frame over it.
// pLayer: zoomed out, the scene's map layer (GpuGroundDraw), else NULL.
// bGround: the map alone into g_hdGround, its terrain and sprites wherever the map layer
// reaches, the console's place too, and nothing over them (the start's ground).
void GpuHdDrawFrame(SDL_GPUCommandBuffer *pCmd, const SDL_Color *pColors, SDL_GPUTexture *pSdWhole,
                    SDL_GPUTexture *pLayer, bool bGround)
{
  const int nLights = GpuHdLightsUp(pCmd);
  SDL_GPUColorTargetInfo target;
  SDL_zero(target);
  target.texture = bGround ? g_hdGround : g_hdFrame;
  target.load_op = SDL_GPU_LOADOP_DONT_CARE;
  target.store_op = SDL_GPU_STOREOP_STORE;
  target.cycle = true;
  GpuHdBasePass base;
  GpuHdBaseSetUp(pColors, bGround, pLayer, &base);
  const bool bSprites = !g_hdDraws.empty() && g_sceneStore[SCENE_STORE_FRAME].pBuffer != NULL;
  if (bSprites)
    GpuHdSpritesUp(pCmd, pColors, nLights);
  SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(pCmd, &target, 1, NULL);
  GpuHdDrawBase(pCmd, pPass, base);
  if (bSprites)
    pPass = GpuHdDrawSprites(pCmd, pPass, &target, pColors, bGround, nLights);
  if (bGround) {
    SDL_EndGPURenderPass(pPass);
    return;
  }
  base.params.fOnlyOver = 1.0f;
  GpuHdDrawBase(pCmd, pPass, base);
  if (g_fHdConsole >= 0.5f && g_hdConsolePipeline != NULL && g_pHdConsole != NULL && g_hdConsoleArt != NULL)
    GpuHdDrawConsole(pCmd, pPass, pColors, base);
  else if (g_pHdConsole == NULL && g_nHdItems > 0)
    GpuHdDrawModUi(pCmd, pPass, pColors);
  if (pSdWhole != NULL)
    GpuHdDrawBlend(pCmd, pPass, pSdWhole);
  SDL_EndGPURenderPass(pPass);
}
