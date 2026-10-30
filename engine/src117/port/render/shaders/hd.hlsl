// hd.hlsl -- the frame in HD (R4d in docs/renderer.md; port_gpu.cpp).
//
// The GPU presenter draws the frame at 2x (or 4x) the game's pixels: PSHdBase lays down
// the resolved SD frame, scaled up, with the tileset's HD terrain where the map shows
// only terrain; each primitive of an image that has HD art
// (port_hd.h) is drawn over it in the game's own order as a quad of its HD frame
// (VSHdQuad, PSHdSprite), with the player's colour where the art's team layer says, a
// shadow as darkness, a glow added, a cloaked unit as the frame so far shifted, a cloaking
// one where 1.17's brightness test passes, the warp flash as its texture, under the fog as
// 1.17's tables darken; and PSHdBase again, where the cover says the software drew over
// the map (the cursor, the text, the dialogs), puts those pixels back on top. An image
// whose art is not in yet is the SD frame's.
//
// Registers as present.hlsl: a vertex shader's uniforms in space 1, a fragment shader's
// textures and samplers in space 2 then its storage buffers, its uniforms in space 3.

struct VSOut
{
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

// ---------------------------------------------------------------------------
// VSHdQuad: one quad, two triangles, at a rectangle of the target.

[[vk::binding(0, 1)]] cbuffer QuadParams : register(b0, space1)
{
  float4 gQuadDst;     // x, y, width, height in the target's pixels
  float4 gQuadUv;      // u, v, width, height in the texture, 0 to 1; a negative width mirrors
  float2 gTargetSize;
  float2 gQuadUnused;
};

VSOut VSHdQuad(uint id : SV_VertexID)
{
  // 0 1 2, 2 1 3 over the corners 0 (0,0) 1 (1,0) 2 (0,1) 3 (1,1)
  static const uint kCorner[6] = {0u, 1u, 2u, 2u, 1u, 3u};
  uint c = kCorner[id];
  float2 k = float2(c & 1u, c >> 1);
  float2 p = gQuadDst.xy + k * gQuadDst.zw;
  VSOut o;
  o.pos = float4(p / gTargetSize * float2(2, -2) + float2(-1, 1), 0, 1);
  o.uv = gQuadUv.xy + k * gQuadUv.zw;
  return o;
}

// ---------------------------------------------------------------------------
// PSHdSprite: the frame's diffuse layer (t0) and team layer (t1), premultiplied. For a
// distortion t1 is a copy of the frame so far (g_hdPrev). The lighting's layers follow
// (t2 to t5), the diffuse layer again for an image without them.

[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> gDiffuse : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState gDiffuseSampler : register(s0, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] Texture2D<float4> gTeam : register(t1, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] SamplerState gTeamSampler : register(s1, space2);
[[vk::combinedImageSampler]] [[vk::binding(2, 2)]] Texture2D<float4> gEmissive : register(t2, space2);
[[vk::combinedImageSampler]] [[vk::binding(2, 2)]] SamplerState gEmissiveSampler : register(s2, space2);
[[vk::combinedImageSampler]] [[vk::binding(3, 2)]] Texture2D<float4> gNormal : register(t3, space2);
[[vk::combinedImageSampler]] [[vk::binding(3, 2)]] SamplerState gNormalSampler : register(s3, space2);
[[vk::combinedImageSampler]] [[vk::binding(4, 2)]] Texture2D<float4> gSpecular : register(t4, space2);
[[vk::combinedImageSampler]] [[vk::binding(4, 2)]] SamplerState gSpecularSampler : register(s4, space2);
[[vk::combinedImageSampler]] [[vk::binding(5, 2)]] Texture2D<float4> gAoDepth : register(t5, space2);
[[vk::combinedImageSampler]] [[vk::binding(5, 2)]] SamplerState gAoDepthSampler : register(s5, space2);

// The frame description's bytes (t6), the fog's light map among them: a shade a corner of
// each 8x8 block of the map, 0 black to 31 as drawn (scene.hlsl's Fog).
[[vk::binding(6, 2)]] ByteAddressBuffer gFrameBytes : register(t6, space2);
// The scene's tables (t7), the warp flash's texture among them.
[[vk::binding(7, 2)]] ByteAddressBuffer gTables : register(t7, space2);
// The frame's lights (t8; port_lights.h, PortLightGpu): in the game's pixels, the map
// layer's top at 0.
struct HdLight
{
  float x;
  float y;
  float radius;
  float intensity;
  float3 color;
  float pad;
};
[[vk::binding(8, 2)]] StructuredBuffer<HdLight> gHdLights : register(t8, space2);
// The scene's planes beside the map (t9; scene.hlsl's gTouched), its draw order among them.
[[vk::binding(9, 2)]] ByteAddressBuffer gSpriteRanks : register(t9, space2);

// Whether something the game drew after an HD frame, rank the HD draws before it, covers its
// pixel at map, in the game's pixels from the map layer's top left: the scene's draw order,
// two bytes a pixel of the layer from rankAt (scene.hlsl, RankSet).
bool RankOver(ByteAddressBuffer ranks, uint rankAt, uint layerW, uint layerStep, float2 map, float rank)
{
  if (rankAt == 0u || layerW == 0u)
    return false;
  int2 m = int2(floor(map / float(max(layerStep, 1u))));
  if (m.x < 0 || m.y < 0 || uint(m.x) >= layerW || uint(m.y) * layerW + uint(m.x) >= rankAt / 3u)
    return false;
  uint nAt = rankAt + 2u * (uint(m.y) * layerW + uint(m.x));
  uint w = ranks.Load(nAt & ~3u);
  return ((nAt & 2u) != 0u ? w >> 16 : w & 0xFFFFu) > uint(rank + 0.5);
}

[[vk::binding(0, 3)]] cbuffer SpriteParams : register(b0, space3)
{
  float3 gTeamColor;  // the player's colour, 0 to 1
  float gMode;        // 0 a sprite, 1 a shadow, 2 a glow (added), 3 a distortion, 4 the part
                      // of a cloaking unit still drawn, 5 the warp flash
  float gHasTeam;
  float gFogOn;       // 1 when the fog darkens this frame
  float gFogScale;    // target pixels to a game pixel
  float gRank;        // the HD draws before this one (RankOver)
  uint gFogBase;      // the light map's first byte in gFrameBytes
  uint gFogStride;    // its bytes to a row
  uint gMapWidth;     // the map's pixels, where the light map ends
  uint gMapRows;
  float4 gRemap[3];   // the sprite's remap as a colour matrix: a grey one for what the fog
                      // remembers, the identity for most
  float4 gEffect;     // 2: its table as out = a * frame + b * under, a in rgb, b in w
  float4 gBright[8];  // 3 and 4: what 1.17 takes from a colour, by the HD pixel's brightness
                      // in 32 steps: the distance a distortion reads from, in game pixels,
                      // or the brightness a cloaking unit is drawn above gThreshold
  float gThreshold;
  float gMirror;
  uint gWarpTable;    // 5: the texture's first byte in gTables, a byte a pixel of the
  uint gWarpW;        // clip at gWarpX, gWarpY in game pixels
  int gWarpX;
  int gWarpY;
  uint gWarpH;
  uint gRankAt;       // the scene's draw order in gSpriteRanks, 0 none
  float gLit;           // how much of the lighting this frame takes, 0 none
  float gEmissiveOn;    // 1 when its art has the emissive layer
  float gLightScale;    // the art's scale: target pixels to a game pixel
  uint gLightCount;     // the frame's lights in gHdLights
  float2 gLitTarget;    // the target's size in pixels
  float gLightStrength; // hd.light.strength
  float gLitUnused;
  float4 gMapAt;        // the map layer's pixel at the target's top left (xy) and its pixels
                        // to a screen pixel (z): the camera's, else 0, 0, 1
  uint gRankLayerW;     // the map layer's pixels across, and the game's to one of them
  uint gRankStep;
  uint2 gRankUnused;
};

// The palette, for the warp flash's texture.
[[vk::binding(2, 3)]] cbuffer SpritePalette : register(b2, space3)
{
  float4 gPalette[256];
};

// The fog's colour matrix at each shade for the sprites, pushed once a frame (port_gpu.cpp,
// GpuHdFogMatrices): three rows of each.
[[vk::binding(1, 3)]] cbuffer SpriteFog : register(b1, space3)
{
  float4 gSpriteFogMat[96];
};

uint LoadByte(ByteAddressBuffer b, uint nAt)
{
  return (b.Load(nAt & ~3u) >> ((nAt & 3u) * 8u)) & 0xFFu;
}

// The fog's shade at the map layer's pixel m, 0 black to 31 as drawn: the light map's four
// corners around it in bytes, blended smoothly where 1.17 steps them by game pixels.
float FogShadeIn(ByteAddressBuffer bytes, float2 m, uint base, uint stride, uint width, uint rows)
{
  float2 sd = clamp(m - 0.5, 0.0, float2(width - 1u, rows - 1u));
  float2 b = sd / 8.0;
  uint2 b0 = uint2(b);
  float2 f = b - float2(b0);
  uint nAt = base + b0.y * stride + b0.x;
  float s = lerp(lerp(float(LoadByte(bytes, nAt)), float(LoadByte(bytes, nAt + 1u)), f.x),
                 lerp(float(LoadByte(bytes, nAt + stride)), float(LoadByte(bytes, nAt + stride + 1u)), f.x), f.y);
  return clamp(s, 0.0, 31.0);
}

// A colour under the fog at shade f: the shade's colour matrix, between the two shades
// around it, fitted over the palette to what 1.17's tables make of each colour.
float3 FogApply(float4 mat[96], float f, float3 c)
{
  uint n = min(uint(f), 30u);
  float t = f - float(n);
  float3 r0 = lerp(mat[n * 3u].xyz, mat[n * 3u + 3u].xyz, t);
  float3 r1 = lerp(mat[n * 3u + 1u].xyz, mat[n * 3u + 4u].xyz, t);
  float3 r2 = lerp(mat[n * 3u + 2u].xyz, mat[n * 3u + 5u].xyz, t);
  return saturate(float3(dot(r0, c), dot(r1, c), dot(r2, c)));
}

// The map layer's pixel at a target pixel.
float2 SpriteMapAt(float2 pos)
{
  return gMapAt.xy + pos / gFogScale * gMapAt.z;
}

float FogShade(float2 pos)
{
  return FogShadeIn(gFrameBytes, SpriteMapAt(pos), gFogBase, gFogStride, gMapWidth, gMapRows);
}

// Remastered's lighting (its util_lighting.glsl, deferred_blit.glsl and
// sprite_forward_lit.glsl, read for the technique; this is Hyperion's own): each lit pixel
// has a normal (x in the normal layer's red, y in its alpha; z made up to unit length, x
// turned over when the frame is mirrored), a specular colour, ambient occlusion and a
// height above the ground (the ao_depth layer's green and alpha). It is lit Blinn-Phong
// style, as the light's colour on its diffuse colour by the angle, with a highlight of
// power 16, both darkened by the occlusion: once by a light from above and to the right,
// and once by each of the frame's lights within reach, falling off to nothing at its
// radius, from 125 game pixels above the ground at the scale. The emissive layer is added
// unlit, and gLit blends from the frame as drawn to the lit one. Positions are as
// Remastered's, with y up: the eye a quarter of the way up the target, 500 above it.
float3 HdLightOn(float3 lightPos, float3 lightColor, float3 pos, float3 n, float3 diffuse, float3 specular, float ao,
                 float power, float3 eye)
{
  float3 l = normalize(lightPos - pos);
  float lambert = saturate(dot(l, n));
  float spec = 0.0;
  if (lambert > 0.0)
    spec = pow(saturate(dot(normalize(l + normalize(eye - pos)), n)), 16.0);
  return (lambert * diffuse * lightColor + spec * specular) * ao * power;
}

float3 HdLit(float3 base, float2 uv, float2 px)
{
  float4 ns = gNormal.Sample(gNormalSampler, uv);
  float2 nxy = float2(ns.r, ns.a) * 2.0 - 1.0;
  if (gMirror > 0.5)
    nxy.x = -nxy.x;
  float3 n = normalize(float3(nxy, sqrt(max(1.0 - dot(nxy, nxy), 0.1))));
  float3 specular = gSpecular.Sample(gSpecularSampler, uv).rgb;
  float4 aoDepth = gAoDepth.Sample(gAoDepthSampler, uv);
  float ao = aoDepth.g;
  float k = gLightScale;
  float3 eye = float3(gLitTarget.x * 0.5, gLitTarget.y * 0.25, 500.0 * k);
  // a light from above: the pixel at the origin, so the light and the eye are directions
  float3 lit = HdLightOn(float3(200.0, 50.0, 500.0), float3(1.0, 1.0, 1.0), float3(0.0, 0.0, 0.0), n, base,
                         specular, ao, 1.0, eye);
  float3 pos = float3(px.x, gLitTarget.y - px.y, aoDepth.a * 75.0 * k);
  for (uint i = 0u; i < gLightCount; ++i) {
    HdLight light = gHdLights[i];
    float2 at = float2(light.x, light.y) * k;
    float radius = light.radius * k;
    float d = distance(px, at);
    if (d >= radius || radius <= 0.0)
      continue;
    float power = light.intensity * gLightStrength * (radius - d) / radius;
    lit += HdLightOn(float3(at.x, gLitTarget.y - at.y, 125.0 * k), light.color, pos, n, base, specular, ao, power,
                     eye);
  }
  if (gEmissiveOn > 0.5)
    lit += gEmissive.Sample(gEmissiveSampler, uv).rgb;
  return lerp(base, lit, gLit);
}

// gBright at a colour's brightness, between the steps around it.
float BrightValue(float3 c)
{
  float f = saturate(dot(c, float3(0.2126, 0.7152, 0.0722))) * 31.0;
  uint n = min(uint(f), 30u);
  return lerp(gBright[n >> 2][n & 3u], gBright[(n + 1u) >> 2][(n + 1u) & 3u], f - float(n));
}

// A layer of the art at uv, averaged over the target pixel when it spans more than a texel
// and a half (zoomed out): up to 4 by 4 linear taps across the pixel's footprint,
// dx and dy its uv across and down; colour by alpha, so the clear texels do not darken it.
float4 ArtAt(Texture2D<float4> t, SamplerState s, float2 uv, float2 dx, float2 dy)
{
  uint2 size;
  t.GetDimensions(size.x, size.y);
  float2 span = float2(length(dx * float2(size)), length(dy * float2(size)));
  if (all(span <= 1.5))
    return t.Sample(s, uv);
  int2 n = clamp(int2(ceil(span / 1.5)), int2(1, 1), int2(4, 4));
  float4 sum = float4(0.0, 0.0, 0.0, 0.0);
  for (int j = 0; j < n.y; ++j) {
    for (int k = 0; k < n.x; ++k) {
      float2 f = (float2(k, j) + 0.5) / float2(n) - 0.5;
      float4 a = t.SampleLevel(s, uv + f.x * dx + f.y * dy, 0.0);
      sum += float4(a.rgb * a.a, a.a);
    }
  }
  float fTaps = float(n.x * n.y);
  return sum.a > 0.0 ? float4(sum.rgb / sum.a, sum.a / fTaps) : float4(0.0, 0.0, 0.0, 0.0);
}

float4 PSHdSprite(VSOut i) : SV_Target
{
  float2 uvDx = ddx(i.uv);
  float2 uvDy = ddy(i.uv);
  if (RankOver(gSpriteRanks, gRankAt, gRankLayerW, gRankStep, SpriteMapAt(i.pos.xy), gRank))
    return float4(0, 0, 0, 0);
  float4 c = ArtAt(gDiffuse, gDiffuseSampler, i.uv, uvDx, uvDy);
  uint mode = uint(gMode + 0.5);
  if (mode == 1u)
    return float4(0, 0, 0, c.a * 0.5);  // a shadow: half dark where the shadow is
  if (mode == 3u) {
    // a distortion: the frame so far a distance to the right, to the left when mirrored
    if (c.a <= 0.0)
      return float4(0, 0, 0, 0);
    float shift = BrightValue(c.rgb) * gFogScale * (gMirror > 0.5 ? -1.0 : 1.0);
    uint2 size;
    gTeam.GetDimensions(size.x, size.y);
    int2 at = clamp(int2(i.pos.xy + float2(shift, 0.0)), int2(0, 0), int2(size) - 1);
    float3 behind = gTeam.Load(int3(at, 0)).rgb;
    return float4(behind * c.a, c.a);
  }
  if (mode == 4u && (c.a <= 0.0 || BrightValue(c.rgb) <= gThreshold))
    return float4(0, 0, 0, 0);  // cloaking: drawn only above the threshold
  if (mode == 5u) {
    // the warp flash: its texture where the frame is, the clip's pixel under the HD one
    int2 d = clamp(int2(floor(SpriteMapAt(i.pos.xy))) - int2(gWarpX, gWarpY), int2(0, 0),
                   int2(int(gWarpW) - 1, int(gWarpH) - 1));
    int col = gMirror > 0.5 ? int(gWarpW) - 1 - d.x : d.x;
    c.rgb = gPalette[LoadByte(gTables, gWarpTable + uint(d.y) * gWarpW + uint(col))].rgb;
    c.a = step(0.5, c.a);
  }
  if ((mode == 0u || mode == 4u) && gHasTeam > 0.5) {
    float mask = ArtAt(gTeam, gTeamSampler, i.uv, uvDx, uvDy).r;
    c.rgb = lerp(c.rgb, c.rgb * gTeamColor, mask);
  }
  if (mode == 0u || mode == 4u)
    c.rgb = saturate(float3(dot(gRemap[0].xyz, c.rgb), dot(gRemap[1].xyz, c.rgb), dot(gRemap[2].xyz, c.rgb)));
  if (gLit > 0.0 && c.a > 0.0)
    c.rgb = saturate(HdLit(c.rgb, i.uv, i.pos.xy));
  if (gFogOn > 1.5) {
    float3 w = FogApply(gSpriteFogMat, FogShade(i.pos.xy), float3(1, 1, 1));  // hd.debug: the fog's
    float k = dot(w, float3(1, 1, 1)) / 3.0;                                     // brightness, as grey
    return float4(k, k, k, 1.0) * step(0.5, c.a);
  }
  if (gFogOn > 0.5)
    c.rgb = FogApply(gSpriteFogMat, FogShade(i.pos.xy), c.rgb);
  if (mode == 2u)  // translucent: a of the frame, and what is under kept by b
    return float4(gEffect.rgb * c.rgb * c.a, c.a * (1.0 - gEffect.w));
  return float4(c.rgb * c.a, c.a);
}

// ---------------------------------------------------------------------------
// VSHdBatch and PSHdBatch: the plain, shadow and translucent frames of up to 8 arts in one
// draw, unlit, an instance each in the game's order (port_gpu_hd.cpp; GpuHdRun).
// PSHdSprite's work for those modes, with what it takes from its uniforms in a record of
// the frame's (HdSpriteRec), and the art in the batch's slots: art k's diffuse layer in
// slot 2k, its team layer in 2k + 1. A large game changes art on nearly every draw, so one
// draw a frame takes costs the processor more than the card's work on it.

struct HdSpriteRec
{
  float4 dst;       // x, y, width, height in the target's pixels
  float4 uv;        // u, v, width, height in the texture; a negative width mirrors
  float4 team;      // the player's colour (rgb), and the mode (w): 0 a sprite, 1 a shadow, 2 a glow
  float4 flags;     // x 1 with the team layer, y gFogOn's, z its rank (RankOver), w the art's place in the batch
  float4 remap[3];  // gRemap's
  float4 effect;    // gEffect's
};

[[vk::binding(0, 0)]] StructuredBuffer<HdSpriteRec> gBatchRecsV : register(t0, space0);

[[vk::binding(0, 1)]] cbuffer BatchQuad : register(b0, space1)
{
  uint gBatchFirst;  // the batch's first record
  uint gBatchUnused;
  float2 gBatchTarget;
};

struct VSBatchOut
{
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
  nointerpolation uint rec : TEXCOORD1;
};

VSBatchOut VSHdBatch(uint id : SV_VertexID, uint inst : SV_InstanceID)
{
  static const uint kCorner[6] = {0u, 1u, 2u, 2u, 1u, 3u};
  HdSpriteRec r = gBatchRecsV[gBatchFirst + inst];
  uint c = kCorner[id];
  float2 k = float2(c & 1u, c >> 1);
  float2 p = r.dst.xy + k * r.dst.zw;
  VSBatchOut o;
  o.pos = float4(p / gBatchTarget * float2(2, -2) + float2(-1, 1), 0, 1);
  o.uv = r.uv.xy + k * r.uv.zw;
  o.rec = gBatchFirst + inst;
  return o;
}

#define HD_BATCH_SLOT(n)                                                                                  \
  [[vk::combinedImageSampler]] [[vk::binding(n, 2)]] Texture2D<float4> gBatchArt##n : register(t##n, space2); \
  [[vk::combinedImageSampler]] [[vk::binding(n, 2)]] SamplerState gBatchArtSampler##n : register(s##n, space2);
HD_BATCH_SLOT(0)
HD_BATCH_SLOT(1)
HD_BATCH_SLOT(2)
HD_BATCH_SLOT(3)
HD_BATCH_SLOT(4)
HD_BATCH_SLOT(5)
HD_BATCH_SLOT(6)
HD_BATCH_SLOT(7)
HD_BATCH_SLOT(8)
HD_BATCH_SLOT(9)
HD_BATCH_SLOT(10)
HD_BATCH_SLOT(11)
HD_BATCH_SLOT(12)
HD_BATCH_SLOT(13)
HD_BATCH_SLOT(14)
HD_BATCH_SLOT(15)

[[vk::binding(16, 2)]] StructuredBuffer<HdSpriteRec> gBatchRecs : register(t16, space2);
[[vk::binding(17, 2)]] ByteAddressBuffer gBatchBytes : register(t17, space2);  // gFrameBytes'
[[vk::binding(18, 2)]] ByteAddressBuffer gBatchRanks : register(t18, space2);  // gSpriteRanks'

[[vk::binding(0, 3)]] cbuffer BatchSprite : register(b0, space3)
{
  uint gBatchFogBase;  // as gFogBase and the rest
  uint gBatchFogStride;
  uint gBatchMapWidth;
  uint gBatchMapRows;
  float4 gBatchMapAt;
  float gBatchFogScale;
  uint gBatchRankAt;  // as gRankAt and the rest; a record's own rank in its flags' z
  uint gBatchRankLayerW;
  uint gBatchRankStep;
};

// The batch's slot n at uv. The four pixels of a quad are one instance's, so one slot's,
// and the derivatives hold.
float4 BatchArt(uint n, float2 uv, float2 dx, float2 dy)
{
  switch (n) {
  case 0u: return ArtAt(gBatchArt0, gBatchArtSampler0, uv, dx, dy);
  case 1u: return ArtAt(gBatchArt1, gBatchArtSampler1, uv, dx, dy);
  case 2u: return ArtAt(gBatchArt2, gBatchArtSampler2, uv, dx, dy);
  case 3u: return ArtAt(gBatchArt3, gBatchArtSampler3, uv, dx, dy);
  case 4u: return ArtAt(gBatchArt4, gBatchArtSampler4, uv, dx, dy);
  case 5u: return ArtAt(gBatchArt5, gBatchArtSampler5, uv, dx, dy);
  case 6u: return ArtAt(gBatchArt6, gBatchArtSampler6, uv, dx, dy);
  case 7u: return ArtAt(gBatchArt7, gBatchArtSampler7, uv, dx, dy);
  case 8u: return ArtAt(gBatchArt8, gBatchArtSampler8, uv, dx, dy);
  case 9u: return ArtAt(gBatchArt9, gBatchArtSampler9, uv, dx, dy);
  case 10u: return ArtAt(gBatchArt10, gBatchArtSampler10, uv, dx, dy);
  case 11u: return ArtAt(gBatchArt11, gBatchArtSampler11, uv, dx, dy);
  case 12u: return ArtAt(gBatchArt12, gBatchArtSampler12, uv, dx, dy);
  case 13u: return ArtAt(gBatchArt13, gBatchArtSampler13, uv, dx, dy);
  case 14u: return ArtAt(gBatchArt14, gBatchArtSampler14, uv, dx, dy);
  default: return ArtAt(gBatchArt15, gBatchArtSampler15, uv, dx, dy);
  }
}

float4 PSHdBatch(VSBatchOut i) : SV_Target
{
  HdSpriteRec r = gBatchRecs[i.rec];
  float2 uvDx = ddx(i.uv);
  float2 uvDy = ddy(i.uv);
  if (RankOver(gBatchRanks, gBatchRankAt, gBatchRankLayerW, gBatchRankStep,
               gBatchMapAt.xy + i.pos.xy / gBatchFogScale * gBatchMapAt.z, r.flags.z))
    return float4(0, 0, 0, 0);
  uint slot = uint(r.flags.w + 0.5) * 2u;
  float4 c = BatchArt(slot, i.uv, uvDx, uvDy);
  uint mode = uint(r.team.w + 0.5);
  if (mode == 1u)
    return float4(0, 0, 0, c.a * 0.5);
  if (mode == 0u && r.flags.x > 0.5) {
    float mask = BatchArt(slot + 1u, i.uv, uvDx, uvDy).r;
    c.rgb = lerp(c.rgb, c.rgb * r.team.rgb, mask);
  }
  if (mode == 0u)
    c.rgb = saturate(float3(dot(r.remap[0].xyz, c.rgb), dot(r.remap[1].xyz, c.rgb), dot(r.remap[2].xyz, c.rgb)));
  float2 m = gBatchMapAt.xy + i.pos.xy / gBatchFogScale * gBatchMapAt.z;
  if (r.flags.y > 1.5) {
    float3 w = FogApply(gSpriteFogMat, FogShadeIn(gBatchBytes, m, gBatchFogBase, gBatchFogStride, gBatchMapWidth,
                                                  gBatchMapRows), float3(1, 1, 1));
    float k = dot(w, float3(1, 1, 1)) / 3.0;
    return float4(k, k, k, 1.0) * step(0.5, c.a);
  }
  if (r.flags.y > 0.5)
    c.rgb = FogApply(gSpriteFogMat, FogShadeIn(gBatchBytes, m, gBatchFogBase, gBatchFogStride, gBatchMapWidth,
                                               gBatchMapRows), c.rgb);
  if (mode == 2u)
    return float4(r.effect.rgb * c.rgb * c.a, c.a * (1.0 - r.effect.w));
  return float4(c.rgb * c.a, c.a);
}

// ---------------------------------------------------------------------------
// PSHdBase: the SD frame (t0), scaled up by whole pixels, with the HD terrain (t1, the
// tileset's atlas) where the map shows only terrain: where the cover (t8) says the screen
// takes the map's pixel and the scene marked it as no primitive's (t9), from the megatile
// the scroll ring holds for its tile (t10), under the fog as the HD frames are (t11, the
// frame description's bytes), with the HD frame of a creep edge over it where 1.17 draws
// one (t2, the edges' atlas). Its water ripples as Remastered's does: where the megatile's
// mask (t3, the masks' atlas; t12, which megatile takes which) says there is water, two
// scrolling, animated normal maps (t4, t5) bend which terrain pixel shows, and light the
// water a little toward the middle of the screen. Where the space platform's terrain is
// clear, its star field shows (t6, the layers' frames; t13, the stars). Zoomed out, the rest
// of the map is the scene's own layer (t7), not the screen's copy of it. With gOnlyOver,
// only the software's pixels over the map, where the cover is 0, and the map's that the
// scene drew after every HD frame (gTouched's 2: the cursor, a health bar, a dialog), a
// dialog's translucency over the HD frame (gTouched's 4) as it was over the SD pixel.

[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> gSd : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState gSdSampler : register(s0, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] Texture2D<float4> gTerrain : register(t1, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] SamplerState gTerrainSampler : register(s1, space2);
[[vk::combinedImageSampler]] [[vk::binding(2, 2)]] Texture2D<float4> gCreep : register(t2, space2);
[[vk::combinedImageSampler]] [[vk::binding(2, 2)]] SamplerState gCreepSampler : register(s2, space2);
[[vk::combinedImageSampler]] [[vk::binding(3, 2)]] Texture2D<float4> gWaterMask : register(t3, space2);
[[vk::combinedImageSampler]] [[vk::binding(3, 2)]] SamplerState gWaterMaskSampler : register(s3, space2);
[[vk::combinedImageSampler]] [[vk::binding(4, 2)]] Texture2D<float4> gNormalWide : register(t4, space2);
[[vk::combinedImageSampler]] [[vk::binding(4, 2)]] SamplerState gNormalWideSampler : register(s4, space2);
[[vk::combinedImageSampler]] [[vk::binding(5, 2)]] Texture2D<float4> gNormalFine : register(t5, space2);
[[vk::combinedImageSampler]] [[vk::binding(5, 2)]] SamplerState gNormalFineSampler : register(s5, space2);
[[vk::combinedImageSampler]] [[vk::binding(6, 2)]] Texture2D<float4> gStarAtlas : register(t6, space2);
[[vk::combinedImageSampler]] [[vk::binding(6, 2)]] SamplerState gStarAtlasSampler : register(s6, space2);
[[vk::combinedImageSampler]] [[vk::binding(7, 2)]] Texture2D<float4> gLayer : register(t7, space2);
[[vk::combinedImageSampler]] [[vk::binding(7, 2)]] SamplerState gLayerSampler : register(s7, space2);
[[vk::binding(8, 2)]] ByteAddressBuffer gCover : register(t8, space2);
[[vk::binding(9, 2)]] ByteAddressBuffer gTouched : register(t9, space2);
struct HdSlot { uint mtx; uint tile; uint edge; uint edgeFrame; };
[[vk::binding(10, 2)]] StructuredBuffer<HdSlot> gRing : register(t10, space2);
[[vk::binding(11, 2)]] ByteAddressBuffer gBaseBytes : register(t11, space2);
[[vk::binding(12, 2)]] ByteAddressBuffer gWaterOf : register(t12, space2);
[[vk::binding(13, 2)]] ByteAddressBuffer gStars : register(t13, space2);
[[vk::binding(14, 2)]] ByteAddressBuffer gHole : register(t14, space2);
[[vk::binding(15, 2)]] ByteAddressBuffer gHud : register(t15, space2);

[[vk::binding(0, 3)]] cbuffer BaseParams : register(b0, space3)
{
  float gScale;      // target pixels to a game pixel
  float gOnlyOver;
  float gHasCover;
  float gTerrainOn;  // 1 when the tileset's atlas is in
  uint gScrollX;     // the map pixel at the view's top left
  uint gScrollY;
  uint gRingX;       // the scroll ring's size in megatiles
  uint gRingY;
  uint gRows;        // the map's rows on the screen
  uint gTiles;       // the atlas's megatiles
  uint gTileSize;    // a megatile's texels across
  float gBaseFogOn;
  uint gBaseFogBase;
  uint gBaseFogStride;
  uint gBaseWidth;
  uint gOverOn;      // 1 when gTouched says what the scene drew after every HD frame
  float4 gBaseFogMat[96];  // the terrain's fog: each shade's colour matrix, three rows
  uint gCreepCount;      // the edges' atlas's frames, 0 with none
  uint gUnderAt;         // where in gTouched the pixels under a dialog's translucency are
  uint2 gCreepUnused;
  uint4 gCreepSize[16];  // each frame's width, and its height above
  float gWaterOn;        // above 0 when the water ripples: its light's strength, 1 as Remastered's
  float gWaterTime;      // its clock, in seconds
  float gWaterFade;      // how far each normal map is from its frame to the next
  float gWaterUnused;
  uint4 gWaterFrames;    // the wide map's frame and the next, then the fine map's
  uint4 gWaterCells;     // each map's frame size in texels
  uint4 gStarField;  // the star field's width and height in texels, its layers (0: none), and
                     // how far it starts above and left of the view
  uint4 gStarGrid;   // a square's texels, the squares across and down, where the stars start
  int4 gStarOfs[5];  // each of the file's layers' shift, in texels, x and y
  uint gHoleOn;      // 1 when gHole holds the console's holes (R4f)
  uint gHoleUnused;  // not a uint3: SPIR-V would start it 16 bytes on (tools/shader_layout.py)
  uint2 gHoleUnused2;
  float4 gBaseMapAt;  // the map layer's pixel at the target's top left (xy) and its pixels to
                      // a screen pixel (z): the camera's, else 0, 0, 1
  uint gHudOn;        // 1 when gHud says where the dialogs drew over the zoomed map
  uint gMapStride;    // the map layer's pixels across, gTouched's row
  uint gScreenRows;   // the screen's map rows; gRows is the map layer's
  uint gAllMap;       // 1 for the map alone, the console's place too (the start's ground)
  uint gMapBoxes;     // where the map shows through 1.17's drawing, an HD item drawn there
                      // (the resource icons)
  uint gMapStep;      // the game's pixels to one of the map layer's
  uint gLayerOn;      // 1 when gLayer is the map layer, zoomed out
  uint gMapBoxUnused;
  int4 gMapBox[8];    // each one's box on the screen, in game pixels
};

// The palette, and each dialog translucency's share of the pixel under it (256 a table by the
// dialog's pixel, four to an element; tables 0 to 3, then 4 to 7): blocks of their own, as
// SDL's Vulkan backend binds 4 KB of a block (tools/shader_layout.py).
[[vk::binding(1, 3)]] cbuffer BasePalette : register(b1, space3)
{
  float4 gBasePalette[256];
};
[[vk::binding(2, 3)]] cbuffer BaseShares : register(b2, space3)
{
  float4 gOverShare[256];
};
[[vk::binding(3, 3)]] cbuffer BaseSharesMore : register(b3, space3)
{
  float4 gOverShareMore[256];
};

// The map layer's pixel at a target pixel.
float2 BaseMapAt(float2 pos)
{
  return gBaseMapAt.xy + pos / gScale * gBaseMapAt.z;
}

// What gTouched says of the map layer's pixel at a target pixel, 0 outside the layer.
uint TouchedAt(float2 pos)
{
  float2 q = BaseMapAt(pos);
  if (any(q < 0.0) || q.x >= float(gMapStride * gMapStep) || q.y >= float(gRows))
    return 0u;
  uint2 qi = uint2(q) / gMapStep;
  return LoadByte(gTouched, qi.y * gMapStride + qi.x);
}

// The map layer's colour at a target pixel, zoomed out, filtered as present.hlsl's Ground
// filters it: the average over the pixel far out, else sharp with its edges smoothed.
float3 LayerAt(float2 pos)
{
  float w;
  float h;
  gLayer.GetDimensions(w, h);
  float2 size = float2(w, h);
  float2 at = BaseMapAt(pos) / float(gMapStep);
  float per = gBaseMapAt.z / gScale / float(gMapStep);  // the layer's pixels a target pixel covers
  if (per > 1.5) {
    int n = clamp(int(ceil(per * 0.5)), 1, 4);
    float3 sum = float3(0.0, 0.0, 0.0);
    for (int j = 0; j < n; ++j)
      for (int k = 0; k < n; ++k)
        sum += gLayer.SampleLevel(gLayerSampler, (at + ((float2(k, j) + 0.5) / float(n) - 0.5) * per) / size, 0).rgb;
    return sum / float(n * n);
  }
  float box = clamp(per, 1e-5, 1.0);
  float2 tx = at - 0.5 * box;
  float2 offset = smoothstep(1.0 - box, 1.0, frac(tx));
  return gLayer.SampleLevel(gLayerSampler, (floor(tx) + 0.5 + offset) / size, 0).rgb;
}

// Whether a dialog drew over the zoomed map at screen pixel p.
bool HudAt(uint2 p, uint w)
{
  return gHudOn != 0u && gAllMap == 0u && LoadByte(gHud, p.y * w + p.x) != 0u;
}

// Whether the map shows at screen pixel p: the cover says so, or 1.17's console has art
// there that Remastered's has not (gHole, its byte) and the frame still shows that art.
bool IsMap(uint2 p, uint w)
{
  if (gAllMap != 0u)
    return true;
  uint nAt = p.y * w + p.x;
  if (LoadByte(gCover, nAt) != 0u)
    return true;
  for (uint b = 0; b < gMapBoxes; ++b)
    if (all(int2(p) >= gMapBox[b].xy) && all(int2(p) < gMapBox[b].xy + gMapBox[b].zw))
      return true;
  if (gHoleOn == 0u)
    return false;
  uint art = LoadByte(gHole, nAt);
  return art != 0u && all(abs(gSd.Load(int3(p, 0)).rgb - gBasePalette[art].rgb) <= 0.002);
}

// The space platform's star field at a target pixel, under the HD terrain where it is clear:
// the five layers each shifted as 1.17 shifts its own (gStarOfs), the file's first on top, as
// 1.17 draws it first and a star lands only on a pixel still 0. Each layer's stars are found
// through the square of the field the pixel falls in (gStars, port_hd.cpp's ReadStars).
float3 HdStars(float2 pos)
{
  float3 c = float3(0, 0, 0);
  int2 field = int2(gStarField.xy);
  uint nBins = gStarGrid.y * gStarGrid.z;
  for (int k = int(gStarField.z) - 1; k >= 0; --k) {
    int2 f = int2(floor(pos)) + int(gStarField.w) - gStarOfs[k].xy;
    f = ((f % field) + field) % field;
    uint nHead = uint(k) * (nBins + 1u) + uint(f.y) / gStarGrid.x * gStarGrid.y + uint(f.x) / gStarGrid.x;
    uint nTo = gStars.Load((nHead + 1u) * 4u);
    for (uint n = gStars.Load(nHead * 4u); n < nTo; ++n) {
      uint nAt = (gStarGrid.w + gStars.Load(n * 4u) * 3u) * 4u;
      uint xy = gStars.Load(nAt);
      uint wh = gStars.Load(nAt + 8u);
      int2 d = (f - int2(xy & 0xFFFFu, xy >> 16) + field) % field;
      if (all(d < int2(wh & 0xFFFFu, wh >> 16))) {
        uint uv = gStars.Load(nAt + 4u);
        float4 t = gStarAtlas.Load(int3(int2(uv & 0xFFFFu, uv >> 16) + d, 0));
        c = lerp(c, t.rgb, t.a);
        break;
      }
    }
  }
  return c;
}

// The HD terrain at a target pixel where the map shows only terrain there: its colour, with
// any creep edge over it, and how much of it is water, 0 to 1. False anywhere else.
bool HdTerrainAt(float2 pos, out float3 c, out float water)
{
  c = float3(0, 0, 0);
  water = 0.0;
  uint w;
  uint h;
  gSd.GetDimensions(w, h);
  if (any(pos < 0.0))
    return false;
  uint2 p = uint2(pos / gScale);
  if (p.x >= w || p.y >= h || (p.y >= gScreenRows && gAllMap == 0u))
    return false;
  if (gHasCover < 0.5 || !IsMap(p, w) || HudAt(p, w) || TouchedAt(pos) != 0u)
    return false;
  float2 q = BaseMapAt(pos);
  if (any(q < 0.0) || q.y >= float(gRows))
    return false;
  float2 m = float2(gScrollX, gScrollY) + q;
  uint tx = uint(m.x) >> 5;
  uint ty = uint(m.y) >> 5;
  HdSlot s = gRing[(ty % gRingY) * gRingX + tx % gRingX];
  uint mtx = s.mtx & 0x7FFFu;
  bool bEdge = s.edge != 0xFFFFFFFFu;
  uint nEdge = s.edgeFrame & 0xFFu;
  if (s.tile != (tx | (ty << 16)) || mtx >= gTiles || (bEdge && ((s.edgeFrame >> 31) == 0u || nEdge >= gCreepCount)))
    return false;
  uint2 local = min(uint2(frac(m / 32.0) * float(gTileSize)), uint2(gTileSize - 1u, gTileSize - 1u));
  float4 ground = gTerrain.Load(int3(uint2(mtx % 64u, mtx / 64u) * gTileSize + local, 0));
  // the space platform's tiles are clear where space is, and the star field shows there
  c = gStarField.z != 0u && ground.a < 1.0 ? lerp(HdStars(pos), ground.rgb, ground.a) : ground.rgb;
  if (bEdge) {
    // the edge's HD frame where 1.17 draws its frame in the tile, at the scale
    uint k = gTileSize / 32u;
    uint2 off = uint2((s.edgeFrame >> 8) & 0xFFu, (s.edgeFrame >> 16) & 0x7Fu) * k;
    uint size = gCreepSize[nEdge >> 2][nEdge & 3u];
    uint2 dim = uint2(size & 0xFFFFu, size >> 16);
    if (all(local >= off) && all(local < off + dim)) {
      float4 e = gCreep.Load(int3(uint2(nEdge % 8u, nEdge / 8u) * gTileSize + local - off, 0));
      c = lerp(c, e.rgb, e.a);
    }
  }
  if (gWaterOn > 0.0) {
    uint nMask = gWaterOf.Load(mtx * 4u);
    if (nMask != 0u) {
      nMask -= 1u;
      water = gWaterMask.Load(int3(uint2(nMask % 64u, nMask / 64u) * gTileSize + local, 0)).r;
    }
  }
  return true;
}

// A normal map's frame, eight to a row of its atlas, at uv repeating across the frame.
float3 WaterNormalIn(Texture2D<float4> t, SamplerState smp, uint nCell, uint nFrame, float2 uv)
{
  uint2 dim;
  t.GetDimensions(dim.x, dim.y);
  float fCell = float(nCell);
  float2 at = float2(nFrame % 8u, nFrame / 8u) * fCell + clamp(frac(uv) * fCell, 0.5, fCell - 0.5);
  return t.SampleLevel(smp, at / float2(dim), 0.0).rgb * 2.0 - 1.0;
}

// The water's surface at map pixel m: a wide, slow map and a fine, faster one, each
// scrolling its own way and blended from its frame to the next, the fine one weaker.
float3 WaterNormal(float2 m)
{
  float t = gWaterTime / 20.0;
  float2 uvWide = m / 160.0 - float2(t * 0.5, t);
  float2 uvFine = m / 80.0 - float2(t * 2.0, t * 1.8);
  float3 wide = lerp(WaterNormalIn(gNormalWide, gNormalWideSampler, gWaterCells.x, gWaterFrames.x, uvWide),
                     WaterNormalIn(gNormalWide, gNormalWideSampler, gWaterCells.x, gWaterFrames.y, uvWide), gWaterFade);
  float3 fine = lerp(WaterNormalIn(gNormalFine, gNormalFineSampler, gWaterCells.y, gWaterFrames.z, uvFine),
                     WaterNormalIn(gNormalFine, gNormalFineSampler, gWaterCells.y, gWaterFrames.w, uvFine), gWaterFade);
  return normalize(float3((wide.xy + fine.xy * 0.6) * 0.6, wide.z));
}

float4 PSHdBase(VSOut i) : SV_Target
{
  uint w;
  uint h;
  gSd.GetDimensions(w, h);
  uint2 p = min(uint2(i.pos.xy / gScale), uint2(w, h) - 1u);
  bool bMap = gHasCover > 0.5 && IsMap(p, w);
  if (gOnlyOver > 0.5) {
    // the software's pixels, and the map's that the scene drew after every HD frame (the
    // cursor, a health bar, a dialog), back on top
    uint t = bMap && gOverOn != 0u && p.y < gScreenRows ? TouchedAt(i.pos.xy) : 0u;
    bool bHud = HudAt(p, w);
    if (gHasCover < 0.5 || (bMap && (t & 6u) == 0u && !bHud))
      discard;
    if ((t & 6u) == 4u && !bHud && gHudOn == 0u) {
      // a dialog's translucency: the SD pixel less its share of the pixel it was drawn
      // over, and that share of the HD frame (the blend, premultiplied)
      uint nAt = p.y * w + p.x;
      uint src = LoadByte(gTouched, 2u * gUnderAt + nAt);
      uint e = ((t >> 3) & 7u) * 64u + src / 4u;
      float share = saturate((e < 256u ? gOverShare[e] : gOverShareMore[e - 256u])[src & 3u]);
      float3 under = gBasePalette[LoadByte(gTouched, gUnderAt + nAt)].rgb;
      return float4(max(gSd.Load(int3(p, 0)).rgb - share * under, 0.0), 1.0 - share);
    }
  }
  float3 c;
  float water;
  if (gOnlyOver < 0.5 && gTerrainOn > 0.5 && HdTerrainAt(i.pos.xy, c, water)) {
    // zoomed out, a target pixel spans several of the atlas's texels: their average, up to
    // 4 by 4 of them, over those still terrain
    float fSpan = gBaseMapAt.z * float(gTileSize) / 32.0 / gScale;
    if (fSpan > 1.25) {
      int n = clamp(int(ceil(fSpan)), 2, 4);
      float3 sum = float3(0.0, 0.0, 0.0);
      float fGot = 0.0;
      for (int j = 0; j < n; ++j) {
        for (int k = 0; k < n; ++k) {
          float3 t;
          float tw;
          if (HdTerrainAt(i.pos.xy + (float2(k, j) + 0.5) / float(n) - 0.5, t, tw)) {
            sum += t;
            fGot += 1.0;
          }
        }
      }
      if (fGot > 0.0)
        c = sum / fGot;
    }
    if (water > 0.0) {
      // the terrain a little way off, by the surface's slope, where both are water (not
      // across the shore, nor from under a unit), and a light toward the screen's middle
      float3 n = WaterNormal(float2(gScrollX, gScrollY) + BaseMapAt(i.pos.xy));
      float3 bent;
      float bentWater;
      float wet = 0.0;
      if (HdTerrainAt(i.pos.xy + n.xy * float2(18.0, 15.0) * gScale / gBaseMapAt.z, bent, bentWater))
        wet = min(water, bentWater);
      else
        bent = c;
      float2 screen = i.pos.xy / (float2(w, gScreenRows) * gScale) * 2.0 - 1.0;
      float lit = saturate(1.0 - abs(screen.y + 0.2)) * pow(saturate(1.0 - abs(screen.x)), 3.0) * 0.6;
      float shine = (n.x * 1.2 + n.y + n.z) / 3.0 * lit * gWaterOn;
      float3 ground = c + shine * water;
      c = lerp(ground, lerp(c, bent, wet) + shine, wet);
    }
    if (gBaseFogOn > 0.5) {
      float f = FogShadeIn(gBaseBytes, BaseMapAt(i.pos.xy), gBaseFogBase, gBaseFogStride, gBaseWidth, gRows);
      c = FogApply(gBaseFogMat, f, c);
    }
    return float4(saturate(c), 1.0);
  }
  if (gAllMap != 0u)
    return float4(0.0, 0.0, 0.0, 0.0);  // none of the map's own: the presenter's choice
  if (gLayerOn != 0u && bMap && p.y < gScreenRows && !HudAt(p, w))
    return float4(LayerAt(i.pos.xy), 1.0);
  return float4(gSd.Load(int3(p, 0)).rgb, 1.0);
}

// PSHdBlend: mid-blend between SD and HD (F5), the whole SD frame (t0) scaled up by
// whole pixels, premultiplied at the blend's weight over the HD frame.
[[vk::binding(0, 3)]] cbuffer BlendParams : register(b0, space3)
{
  float gBlendScale;   // target pixels to a game pixel
  float gBlendWeight;  // how much of the SD frame shows, 1 all of it
  float2 gBlendUnused;
};

float4 PSHdBlend(VSOut i) : SV_Target
{
  uint w;
  uint h;
  gSd.GetDimensions(w, h);
  uint2 p = min(uint2(i.pos.xy / gBlendScale), uint2(w, h) - 1u);
  return float4(gSd.Load(int3(p, 0)).rgb * gBlendWeight, gBlendWeight);
}

// ---------------------------------------------------------------------------
// PSHdConsole: Remastered's console (R4f), drawn last: its pieces (t0 to t2, each placed by
// gPiece, the last solid one on top) wherever they are solid, except where 1.17's frame has
// something of its own there. That is a pixel the software drew (the cover, t4, is 0) that
// is not the console art's own (t6, game\<race>console.pcx as status_load_console loaded
// it, compared by colour with the frame, t3): the panels' content, a dialog, the cursor;
// or a map pixel the scene drew over the HD frames (t5, gTouched's 2 and 4: a health bar,
// map text). Where 1.17's console has a hole onto the map and the piece is solid, the
// piece covers the map, as Remastered's does.

[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> gPiece0 : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState gPiece0Sampler : register(s0, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] Texture2D<float4> gPiece1 : register(t1, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] SamplerState gPiece1Sampler : register(s1, space2);
[[vk::combinedImageSampler]] [[vk::binding(2, 2)]] Texture2D<float4> gPiece2 : register(t2, space2);
[[vk::combinedImageSampler]] [[vk::binding(2, 2)]] SamplerState gPiece2Sampler : register(s2, space2);
[[vk::combinedImageSampler]] [[vk::binding(3, 2)]] Texture2D<float4> gConsoleSd : register(t3, space2);
[[vk::combinedImageSampler]] [[vk::binding(3, 2)]] SamplerState gConsoleSdSampler : register(s3, space2);
[[vk::binding(4, 2)]] ByteAddressBuffer gConsoleCover : register(t4, space2);
[[vk::binding(5, 2)]] ByteAddressBuffer gConsoleTouched : register(t5, space2);
[[vk::binding(6, 2)]] ByteAddressBuffer gConsoleArt : register(t6, space2);

[[vk::binding(0, 3)]] cbuffer ConsoleParams : register(b0, space3)
{
  int4 gPiece[3];       // each piece's box on the target, in texels
  uint gPieces;
  float gConsoleScale;  // target pixels to a game pixel
  uint gArtX;           // the art's left edge on the screen, and its size
  uint gArtW;
  uint gArtH;
  uint gConsoleRows;    // the map's rows, which gConsoleTouched covers; 0 without it
  uint gOwnedCount;     // the controls the console's HD contents are drawn in (PSHdUi)
  uint gConsoleUnused;
  int4 gOwned[32];      // each one's box on the screen, in game pixels
};

// The palette, a block of its own (BasePalette's reason).
[[vk::binding(1, 3)]] cbuffer ConsolePalette : register(b1, space3)
{
  float4 gConsolePalette[256];
};

float4 PSHdConsole(VSOut i) : SV_Target
{
  int2 q = int2(i.pos.xy);
  float4 c = float4(0, 0, 0, 0);
  for (uint k = 0; k < gPieces; ++k) {
    int2 d = q - gPiece[k].xy;
    if (any(d < 0) || any(d >= gPiece[k].zw))
      continue;
    float4 t = k == 0u ? gPiece0.Load(int3(d, 0)) : k == 1u ? gPiece1.Load(int3(d, 0)) : gPiece2.Load(int3(d, 0));
    if (t.a > 0.5)
      c = t;
  }
  if (c.a < 0.5)
    discard;
  uint w;
  uint h;
  gConsoleSd.GetDimensions(w, h);
  uint2 p = min(uint2(i.pos.xy / gConsoleScale), uint2(w, h) - 1u);
  // under the HD contents the console, not 1.17's drawing of them
  for (uint o = 0; o < gOwnedCount; ++o)
    if (all(int2(p) >= gOwned[o].xy) && all(int2(p) < gOwned[o].xy + gOwned[o].zw))
      return float4(c.rgb, 1.0);
  uint nAt = p.y * w + p.x;
  if (LoadByte(gConsoleCover, nAt) != 0u) {
    if (p.y < gConsoleRows && (LoadByte(gConsoleTouched, nAt) & 6u) != 0u)
      discard;
    return float4(c.rgb, 1.0);
  }
  int ax = int(p.x) - int(gArtX);
  int ay = int(p.y) - int(h - gArtH);
  if (ax < 0 || ax >= int(gArtW) || ay < 0)
    discard;
  uint art = LoadByte(gConsoleArt, uint(ay) * gArtW + uint(ax));
  if (art == 0u || any(abs(gConsoleSd.Load(int3(p, 0)).rgb - gConsolePalette[art].rgb) > 0.002))
    discard;
  return float4(c.rgb, 1.0);
}

// ---------------------------------------------------------------------------
// PSHdUi: the console's HD contents (R4f), over the HD console: each item a frame of the
// command icons' atlas (t0), the race's button frames (t1) or the selection's wireframe
// (t2), at its box on the target, multiplied by its tint, one over another in order; the
// HD portrait, scaled to cover its box and cut to it; or a progress bar
// (t4), its empty frame and its full one as far as it has come. Over them, the numbers
// 1.17 wrote over the selection panel's icons, the pixels its text changed taken from the SD
// frame (t5), as the console's other text is.

[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> gUiIcons : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState gUiIconsSampler : register(s0, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] Texture2D<float4> gUiButtons : register(t1, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] SamplerState gUiButtonsSampler : register(s1, space2);
[[vk::combinedImageSampler]] [[vk::binding(2, 2)]] Texture2D<float4> gUiWire : register(t2, space2);
[[vk::combinedImageSampler]] [[vk::binding(2, 2)]] SamplerState gUiWireSampler : register(s2, space2);
[[vk::combinedImageSampler]] [[vk::binding(3, 2)]] Texture2D<float4> gUiPortrait : register(t3, space2);
[[vk::combinedImageSampler]] [[vk::binding(3, 2)]] SamplerState gUiPortraitSampler : register(s3, space2);
[[vk::combinedImageSampler]] [[vk::binding(4, 2)]] Texture2D<float4> gUiBars : register(t4, space2);
[[vk::combinedImageSampler]] [[vk::binding(4, 2)]] SamplerState gUiBarsSampler : register(s4, space2);
[[vk::combinedImageSampler]] [[vk::binding(5, 2)]] Texture2D<float4> gUiSd : register(t5, space2);
[[vk::combinedImageSampler]] [[vk::binding(5, 2)]] SamplerState gUiSdSampler : register(s5, space2);
[[vk::combinedImageSampler]] [[vk::binding(6, 2)]] Texture2D<float4> gUiResources : register(t6, space2);
[[vk::combinedImageSampler]] [[vk::binding(6, 2)]] SamplerState gUiResourcesSampler : register(s6, space2);
[[vk::combinedImageSampler]] [[vk::binding(7, 2)]] Texture2D<float4> gUiMod : register(t7, space2);
[[vk::combinedImageSampler]] [[vk::binding(7, 2)]] SamplerState gUiModSampler : register(s7, space2);

[[vk::binding(0, 3)]] cbuffer UiParams : register(b0, space3)
{
  int4 gUiDst[64];    // each item's box on the target, in texels
  int4 gUiSrc[64];    // where its frame is in its texture (the portrait: its size), which texture, and 1 for an icon
                      // (a mod's own, texture 6: the part it draws, 0 all)
  float4 gUiTint[64];
  int4 gUiText[16];      // the boxes of 1.17's numbers over the icons, in game pixels
  uint4 gUiTextBits[32]; // which of their pixels are the text's, 256 bits a box, row after row
  uint gUiCount;
  uint gUiScale;         // target pixels to a game pixel
  uint gUiTexts;
  uint gUiUnused;
};

float4 PSHdUi(VSOut i) : SV_Target
{
  int2 q = int2(i.pos.xy);
  float4 acc = float4(0, 0, 0, 0);
  for (uint k = 0; k < gUiCount; ++k) {
    int2 d = q - gUiDst[k].xy;
    if (any(d < 0) || any(d >= gUiDst[k].zw))
      continue;
    int3 t = int3(gUiSrc[k].xy + d, 0);
    float4 c;
    if (gUiSrc[k].z == 3) {
      // w 1: fitted to the box, black about it
      float2 box = float2(gUiDst[k].zw);
      float2 size = float2(gUiSrc[k].xy);
      float scale = gUiSrc[k].w != 0 ? min(box.x / size.x, box.y / size.y) : max(box.x / size.x, box.y / size.y);
      float2 uv = ((float2(d) + 0.5 - box * 0.5) / scale + size * 0.5) / size;
      c = any(uv < 0.0) || any(uv > 1.0) ? float4(0.0, 0.0, 0.0, 1.0)
                                         : float4(gUiPortrait.SampleLevel(gUiPortraitSampler, uv, 0).rgb, 1.0);
    } else if (gUiSrc[k].z == 6) {
      // a mod's own: its brightness, and the part it is (port_modart.h, PortModUi)
      float4 m = gUiMod.Load(t);
      if (gUiSrc[k].w != 0 && int(m.g * 255.0 + 0.5) / 32 != gUiSrc[k].w)
        continue;
      c = float4(m.rrr, m.a);
    } else {
      c = gUiSrc[k].z == 0 ? gUiIcons.Load(t) : gUiSrc[k].z == 1 ? gUiButtons.Load(t) :
          gUiSrc[k].z == 4 ? gUiBars.Load(t) : gUiSrc[k].z == 5 ? gUiResources.Load(t) : gUiWire.Load(t);
    }
    // an icon is grey, lifted to its colour as Remastered shows it; the rest is multiplied
    c.rgb = gUiSrc[k].w != 0 && gUiSrc[k].z == 0 ? gUiTint[k].rgb * saturate(c.rgb * 1.7) : c.rgb * gUiTint[k].rgb;
    acc = float4(c.rgb * c.a + acc.rgb * (1.0 - c.a), c.a + acc.a * (1.0 - c.a));
  }
  int2 g = q / int(max(gUiScale, 1u));
  for (uint b = 0; b < gUiTexts; ++b) {
    if (any(g < gUiText[b].xy) || any(g >= gUiText[b].xy + gUiText[b].zw))
      continue;
    uint n = uint((g.y - gUiText[b].y) * gUiText[b].z + (g.x - gUiText[b].x));
    if ((gUiTextBits[b * 2u + (n >> 7)][(n >> 5) & 3u] >> (n & 31u)) & 1u)
      return float4(gUiSd.Load(int3(g, 0)).rgb, 1.0);
  }
  if (acc.a <= 0.0)
    discard;
  return acc;
}

// The full-target triangle for PSHdBase, PSHdBlend, PSHdConsole and PSHdUi.
VSOut VSHdFull(uint id : SV_VertexID)
{
  VSOut o;
  float2 p = float2((id << 1) & 2, id & 2);
  o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
  o.uv = p;
  return o;
}
