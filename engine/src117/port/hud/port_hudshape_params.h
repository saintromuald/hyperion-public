// port_hudshape_params.h -- hudshape.hlsl's uniform block, which the GPU presenter
// fills for each shape layer it draws (port_gpu_hud.cpp). Written by
// engine/tools/hud/export_hud_shader.mjs with the shader; run it again rather than editing this.
#pragma once

#include <stddef.h>

enum { HUD_SHAPE_EDGES = 96 };

struct HudShapeParams {
  float uHalf[2];  // half the box, units
  float uPx;  // units per pixel
  float uMargin;  // units drawn round the box for light outside it
  float uOrigin[2];  // the box's top left on the screen, units
  float uScreen[2];  // the screen, units
  float uTime;  // seconds
  float _pad0;
  float _pad1;
  float _pad2;
  float uCorner[4];  // corner sizes: top left, top right, bottom right, bottom left
  float uKind[4];  // corner kinds: 0 round, 1 chamfer, 2 notch
  float uCut[4][4];  // cut-outs: centre from the box centre, half size
  float uCutR[4];  // cut-out radii
  float uCutN;  // cut-outs used
  float _pad3;
  float _pad4;
  float _pad5;
  float uFill[4][4];  // fill stops, straight alpha
  float uFillN;  // stops used
  float _pad6;
  float uFillDir[2];  // the gradient runs this way
  float uGlass;
  float _pad7;
  float _pad8;
  float _pad9;
  float uGlassTint[4];
  float uGlassNoise;
  float uGlassStreak;
  float uBevel;
  float uBevelStrength;
  float uLightFrom[2];  // unit vector toward the light
  float _pad10;
  float _pad11;
  float uHighlight[4];
  float uInnerShadow;
  float uInnerShadowStrength;
  float uRim;
  float uRimWidth;
  float uRimColor[4];
  float uInnerGlow[4];
  float uInnerGlowSize;
  float uLines;
  float uLineSpacing;
  float uHolo;
  float uStroke[4];
  float uLine;
  float uEnergy;
  float _pad12;
  float _pad13;
  float uEnergyColor[4];
  float uEnergyWidth;
  float _pad14;
  float _pad15;
  float _pad16;
  float uOuterGlow[4];
  float uOuterGlowSize;
  float uSurf;  // surface: an index into SURFACES, 0 none
  float _pad17;
  float _pad18;
  float uSurfA[4];  // surface colours
  float uSurfB[4];
  float uSurfC[4];
  float uSurfScale;  // pattern size
  float _pad19;
  float _pad20;
  float _pad21;
  float uSurfP[4];  // gloss, wear, detail, speed
  float uMirror;  // 1: patterns mirror about the centre
  float _pad22;
  float _pad23;
  float _pad24;
  float uWobble[4];  // amplitude, scale, speed
  float uSpines[4];  // height, spacing, width, lean
  float uBow;  // units the top arcs up
  float uBlend;  // units unions and cuts blend over
  float uSegments[2];  // count, gap
  float uSeams[4];  // spacing, width, strength, angle
  float uBolts[4];  // radius, inset, spacing, head (0 dome, 1 hex, 2 socket)
  float uBoltColor[4];
  float uInlay[4];  // width, depth, glow
  float uInlayColor[4];
  float uReveal;  // 0 hidden, 1 shown
  float uRevealKind;  // 0 peel, 1 warp
  float _pad25;
  float _pad26;
  float uRevealColor[4];
  float uPath[96][4];  // a path outline: edges (a.x, a.y, b.x, b.y) from the box centre, units
  float uPathN;  // edges used; 0 draws the box
  float _pad27;
  float _pad28;
  float _pad29;
  float uProfile[4];  // kind (0 slope, 1 step, 2 double, 3 lip, 4 groove, 5 chamfer), steps, groove depth, groove width
  float uPanel[4];  // panel lines: count, inset, gap, width
  float uPattern[4];  // kind (0 none, 1 honeycomb, 2 slats, 3 perforated), cell, depth, inset
  float uPattern2[4];  // angle, wall
  float uScratch[4];  // amount, length, angle, wander
  float uShadow[4];  // contact shadow: strength, height, softness
  float uRelief[4];  // relief (how hard the light models the surface), crest lines
  float uWear[4];  // worn edges: amount, depth in units
  float uWearColor[4];  // the bare metal
  float uSheen[4];  // a studio's reflection on the surface: strength
  float uSheenColor[4];
};
static_assert(sizeof(HudShapeParams) == 2480, "hudshape.hlsl's block is 2480 bytes");
static_assert(offsetof(HudShapeParams, uHalf) == 0, "hudshape.hlsl lays out uHalf there");
static_assert(offsetof(HudShapeParams, uPx) == 8, "hudshape.hlsl lays out uPx there");
static_assert(offsetof(HudShapeParams, uMargin) == 12, "hudshape.hlsl lays out uMargin there");
static_assert(offsetof(HudShapeParams, uOrigin) == 16, "hudshape.hlsl lays out uOrigin there");
static_assert(offsetof(HudShapeParams, uScreen) == 24, "hudshape.hlsl lays out uScreen there");
static_assert(offsetof(HudShapeParams, uTime) == 32, "hudshape.hlsl lays out uTime there");
static_assert(offsetof(HudShapeParams, uCorner) == 48, "hudshape.hlsl lays out uCorner there");
static_assert(offsetof(HudShapeParams, uKind) == 64, "hudshape.hlsl lays out uKind there");
static_assert(offsetof(HudShapeParams, uCut) == 80, "hudshape.hlsl lays out uCut there");
static_assert(offsetof(HudShapeParams, uCutR) == 144, "hudshape.hlsl lays out uCutR there");
static_assert(offsetof(HudShapeParams, uCutN) == 160, "hudshape.hlsl lays out uCutN there");
static_assert(offsetof(HudShapeParams, uFill) == 176, "hudshape.hlsl lays out uFill there");
static_assert(offsetof(HudShapeParams, uFillN) == 240, "hudshape.hlsl lays out uFillN there");
static_assert(offsetof(HudShapeParams, uFillDir) == 248, "hudshape.hlsl lays out uFillDir there");
static_assert(offsetof(HudShapeParams, uGlass) == 256, "hudshape.hlsl lays out uGlass there");
static_assert(offsetof(HudShapeParams, uGlassTint) == 272, "hudshape.hlsl lays out uGlassTint there");
static_assert(offsetof(HudShapeParams, uGlassNoise) == 288, "hudshape.hlsl lays out uGlassNoise there");
static_assert(offsetof(HudShapeParams, uGlassStreak) == 292, "hudshape.hlsl lays out uGlassStreak there");
static_assert(offsetof(HudShapeParams, uBevel) == 296, "hudshape.hlsl lays out uBevel there");
static_assert(offsetof(HudShapeParams, uBevelStrength) == 300, "hudshape.hlsl lays out uBevelStrength there");
static_assert(offsetof(HudShapeParams, uLightFrom) == 304, "hudshape.hlsl lays out uLightFrom there");
static_assert(offsetof(HudShapeParams, uHighlight) == 320, "hudshape.hlsl lays out uHighlight there");
static_assert(offsetof(HudShapeParams, uInnerShadow) == 336, "hudshape.hlsl lays out uInnerShadow there");
static_assert(offsetof(HudShapeParams, uInnerShadowStrength) == 340, "hudshape.hlsl lays out uInnerShadowStrength there");
static_assert(offsetof(HudShapeParams, uRim) == 344, "hudshape.hlsl lays out uRim there");
static_assert(offsetof(HudShapeParams, uRimWidth) == 348, "hudshape.hlsl lays out uRimWidth there");
static_assert(offsetof(HudShapeParams, uRimColor) == 352, "hudshape.hlsl lays out uRimColor there");
static_assert(offsetof(HudShapeParams, uInnerGlow) == 368, "hudshape.hlsl lays out uInnerGlow there");
static_assert(offsetof(HudShapeParams, uInnerGlowSize) == 384, "hudshape.hlsl lays out uInnerGlowSize there");
static_assert(offsetof(HudShapeParams, uLines) == 388, "hudshape.hlsl lays out uLines there");
static_assert(offsetof(HudShapeParams, uLineSpacing) == 392, "hudshape.hlsl lays out uLineSpacing there");
static_assert(offsetof(HudShapeParams, uHolo) == 396, "hudshape.hlsl lays out uHolo there");
static_assert(offsetof(HudShapeParams, uStroke) == 400, "hudshape.hlsl lays out uStroke there");
static_assert(offsetof(HudShapeParams, uLine) == 416, "hudshape.hlsl lays out uLine there");
static_assert(offsetof(HudShapeParams, uEnergy) == 420, "hudshape.hlsl lays out uEnergy there");
static_assert(offsetof(HudShapeParams, uEnergyColor) == 432, "hudshape.hlsl lays out uEnergyColor there");
static_assert(offsetof(HudShapeParams, uEnergyWidth) == 448, "hudshape.hlsl lays out uEnergyWidth there");
static_assert(offsetof(HudShapeParams, uOuterGlow) == 464, "hudshape.hlsl lays out uOuterGlow there");
static_assert(offsetof(HudShapeParams, uOuterGlowSize) == 480, "hudshape.hlsl lays out uOuterGlowSize there");
static_assert(offsetof(HudShapeParams, uSurf) == 484, "hudshape.hlsl lays out uSurf there");
static_assert(offsetof(HudShapeParams, uSurfA) == 496, "hudshape.hlsl lays out uSurfA there");
static_assert(offsetof(HudShapeParams, uSurfB) == 512, "hudshape.hlsl lays out uSurfB there");
static_assert(offsetof(HudShapeParams, uSurfC) == 528, "hudshape.hlsl lays out uSurfC there");
static_assert(offsetof(HudShapeParams, uSurfScale) == 544, "hudshape.hlsl lays out uSurfScale there");
static_assert(offsetof(HudShapeParams, uSurfP) == 560, "hudshape.hlsl lays out uSurfP there");
static_assert(offsetof(HudShapeParams, uMirror) == 576, "hudshape.hlsl lays out uMirror there");
static_assert(offsetof(HudShapeParams, uWobble) == 592, "hudshape.hlsl lays out uWobble there");
static_assert(offsetof(HudShapeParams, uSpines) == 608, "hudshape.hlsl lays out uSpines there");
static_assert(offsetof(HudShapeParams, uBow) == 624, "hudshape.hlsl lays out uBow there");
static_assert(offsetof(HudShapeParams, uBlend) == 628, "hudshape.hlsl lays out uBlend there");
static_assert(offsetof(HudShapeParams, uSegments) == 632, "hudshape.hlsl lays out uSegments there");
static_assert(offsetof(HudShapeParams, uSeams) == 640, "hudshape.hlsl lays out uSeams there");
static_assert(offsetof(HudShapeParams, uBolts) == 656, "hudshape.hlsl lays out uBolts there");
static_assert(offsetof(HudShapeParams, uBoltColor) == 672, "hudshape.hlsl lays out uBoltColor there");
static_assert(offsetof(HudShapeParams, uInlay) == 688, "hudshape.hlsl lays out uInlay there");
static_assert(offsetof(HudShapeParams, uInlayColor) == 704, "hudshape.hlsl lays out uInlayColor there");
static_assert(offsetof(HudShapeParams, uReveal) == 720, "hudshape.hlsl lays out uReveal there");
static_assert(offsetof(HudShapeParams, uRevealKind) == 724, "hudshape.hlsl lays out uRevealKind there");
static_assert(offsetof(HudShapeParams, uRevealColor) == 736, "hudshape.hlsl lays out uRevealColor there");
static_assert(offsetof(HudShapeParams, uPath) == 752, "hudshape.hlsl lays out uPath there");
static_assert(offsetof(HudShapeParams, uPathN) == 2288, "hudshape.hlsl lays out uPathN there");
static_assert(offsetof(HudShapeParams, uProfile) == 2304, "hudshape.hlsl lays out uProfile there");
static_assert(offsetof(HudShapeParams, uPanel) == 2320, "hudshape.hlsl lays out uPanel there");
static_assert(offsetof(HudShapeParams, uPattern) == 2336, "hudshape.hlsl lays out uPattern there");
static_assert(offsetof(HudShapeParams, uPattern2) == 2352, "hudshape.hlsl lays out uPattern2 there");
static_assert(offsetof(HudShapeParams, uScratch) == 2368, "hudshape.hlsl lays out uScratch there");
static_assert(offsetof(HudShapeParams, uShadow) == 2384, "hudshape.hlsl lays out uShadow there");
static_assert(offsetof(HudShapeParams, uRelief) == 2400, "hudshape.hlsl lays out uRelief there");
static_assert(offsetof(HudShapeParams, uWear) == 2416, "hudshape.hlsl lays out uWear there");
static_assert(offsetof(HudShapeParams, uWearColor) == 2432, "hudshape.hlsl lays out uWearColor there");
static_assert(offsetof(HudShapeParams, uSheen) == 2448, "hudshape.hlsl lays out uSheen there");
static_assert(offsetof(HudShapeParams, uSheenColor) == 2464, "hudshape.hlsl lays out uSheenColor there");
