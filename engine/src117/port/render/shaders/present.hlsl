// present.hlsl -- the GPU presenter's shaders (port_gpu.cpp, docs/renderer.md, R4a).
//
// Two passes draw a frame. PSResolve turns the game's 8-bit screen into colour
// through the palette, at the game's size, one texel per pixel: that texture is what
// the check reads back and compares with the screen through the palette on the CPU.
// PSScale draws it into the window's letterbox with the pixel-art filter SDL's own
// renderer uses (SDL_SCALEMODE_PIXELART): sharp texels at any scale, their edges
// smoothed over one window pixel.
//
// Built by tools/build_engine_x64.py with dxc, to DXIL for Direct3D 12 and to SPIR-V
// for Vulkan. The registers follow SDL_GPU's layout for fragment shaders: textures
// and their samplers in space 2 (set 2), uniforms in space 3 (set 3). SDL's Vulkan
// backend binds each texture with its sampler, hence the combined image samplers.

struct VSOut
{
  float4 pos : SV_Position;
};

// One triangle over the whole target; the pass's viewport places it.
VSOut VSMain(uint id : SV_VertexID)
{
  VSOut o;
  float2 p = float2((id << 1) & 2, id & 2);
  o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
  return o;
}

// The screen's palette indices, R8_UNORM, and the palette, 256x1 RGBA8.
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> gIndex : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState gIndexSampler : register(s0, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] Texture2D<float4> gPalette : register(t1, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] SamplerState gPaletteSampler : register(s1, space2);

float4 PSResolve(VSOut i) : SV_Target
{
  uint index = (uint)(gIndex.Load(int3(i.pos.xy, 0)).r * 255.0 + 0.5);
  return float4(gPalette.Load(int3(index, 0, 0)).rgb, 1.0);
}

// PSCompose resolves as PSResolve does, taking a pixel the cover marks from the GPU
// scene's copy of the screen (scene.hlsl, CSShow) in place of the game's screen. Both
// are a byte a pixel in the screen's rows; storage buffers follow the textures in
// space 2 (set 2).
[[vk::binding(2, 2)]] ByteAddressBuffer gShown : register(t2, space2);
[[vk::binding(3, 2)]] ByteAddressBuffer gCover : register(t3, space2);

uint LoadByte(ByteAddressBuffer b, uint nAt)
{
  return (b.Load(nAt & ~3u) >> ((nAt & 3u) * 8u)) & 0xFFu;
}

float4 PSCompose(VSOut i) : SV_Target
{
  uint w;
  uint h;
  gIndex.GetDimensions(w, h);
  uint2 p = uint2(i.pos.xy);
  uint nAt = p.y * w + p.x;
  uint index = LoadByte(gCover, nAt) != 0u ? LoadByte(gShown, nAt)
                                           : (uint)(gIndex.Load(int3(p, 0)).r * 255.0 + 0.5);
  return float4(gPalette.Load(int3(index, 0, 0)).rgb, 1.0);
}

// PSComposeFull: as PSCompose, a pixel the cover marks taken from the full colour's copy
// of the screen (scene.hlsl, CSShowFull), RGBA8 a pixel, in place of the GPU scene's indices.
[[vk::binding(4, 2)]] ByteAddressBuffer gShownRgb : register(t4, space2);

float4 PSComposeFull(VSOut i) : SV_Target
{
  uint w;
  uint h;
  gIndex.GetDimensions(w, h);
  uint2 p = uint2(i.pos.xy);
  uint nAt = p.y * w + p.x;
  if (LoadByte(gCover, nAt) != 0u) {
    uint c = gShownRgb.Load(nAt * 4u);
    return float4(float3(c & 0xFFu, (c >> 8) & 0xFFu, (c >> 16) & 0xFFu) / 255.0, 1.0);
  }
  uint index = (uint)(gIndex.Load(int3(p, 0)).r * 255.0 + 0.5);
  return float4(gPalette.Load(int3(index, 0, 0)).rgb, 1.0);
}

// PSGround, at a skirmish's start (port_intro.cpp): the map layer the GPU scene drew, all of
// it, under the console too (scene.hlsl's gMap, or gMapRgb in full colour), resolved as
// PSCompose resolves the screen. Clear below the layer's rows, where there is no map.
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> gGroundPalette : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState gGroundPaletteSampler : register(s0, space2);
[[vk::binding(1, 2)]] ByteAddressBuffer gGroundMap : register(t1, space2);
[[vk::binding(2, 2)]] ByteAddressBuffer gGroundRgb : register(t2, space2);

[[vk::binding(0, 3)]] cbuffer GroundParams : register(b0, space3)
{
  uint gGroundWidth;    // the map layer's width and rows
  uint gGroundRows;
  uint gGroundFull;     // 1 to take gGroundRgb
  uint gGroundUnused;
};

float4 PSGround(VSOut i) : SV_Target
{
  uint2 p = uint2(i.pos.xy);
  if (p.x >= gGroundWidth || p.y >= gGroundRows)
    return float4(0.0, 0.0, 0.0, 0.0);
  uint nAt = p.y * gGroundWidth + p.x;
  if (gGroundFull != 0u) {
    uint c = gGroundRgb.Load(nAt * 4u);
    return float4(float3(c & 0xFFu, (c >> 8) & 0xFFu, (c >> 16) & 0xFFu) / 255.0, 1.0);
  }
  return float4(gGroundPalette.Load(int3(LoadByte(gGroundMap, nAt), 0, 0)).rgb, 1.0);
}

// The resolved frame, sampled bilinearly, and where it goes in the window. The map can move
// under the rest: at a skirmish's start (port_intro.cpp), which scales the map about the
// frame's middle and shifts and fades the rest (the console, the resources), as PSPost does,
// and with the camera zoomed out, when the map is the render view's layer seen
// through the camera. PSGround's map layer, the screen's cover and, zoomed, where the dialogs
// drew over the map (scene.hlsl, CSHud).
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> gFrame : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState gFrameSampler : register(s0, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] Texture2D<float4> gGround : register(t1, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] SamplerState gGroundSampler : register(s1, space2);
[[vk::binding(2, 2)]] ByteAddressBuffer gScaleCover : register(t2, space2);
[[vk::binding(3, 2)]] ByteAddressBuffer gScaleHud : register(t3, space2);

[[vk::binding(0, 3)]] cbuffer ScaleParams : register(b0, space3)
{
  float4 gDst;          // the letterbox in the target's pixels: x, y, width, height
  float2 gTexSize;      // the frame's size in texels
  float gHasCover;      // 1 when the cover is the frame's
  float gCoverScale;    // the frame's texels to a pixel of the cover
  float gMoved;         // 1 when the map moves under the rest
  float gHudShift;      // the rest shifted down by this part of the frame's height,
  float gHudAlpha;      // and its opacity; 0, 1 otherwise
  float gHasGround;     // 1 when gGround holds the map layer
  float4 gMapAt;        // the map layer's place: at uv, PSGround's layer at xy + uv * zw
  float4 gFrameAt;      // and the frame's, where the map is taken from it without the layer
  float gHasHud;        // 1 when gScaleHud says where the dialogs drew over the map
  float gArtSoft;       // Settings' Classic art filtering: 0 the pixel-art filter, 1 the texels blended across
  float2 gScaleUnused;
};

// The frame at uv, its texels span apart in uv a window pixel's span at the scale shown.
float3 Scaled(float2 uv, float2 span)
{
  // one window pixel, in texels, widened toward a whole texel as Classic art filtering softens it
  float2 box = lerp(clamp(gTexSize * span / gDst.zw, 1e-5, 1.0), 1.0, gArtSoft);
  float2 tx = uv * gTexSize - 0.5 * box;
  float2 offset = smoothstep(1.0 - box, 1.0, frac(tx));
  float2 at = (floor(tx) + 0.5 + offset) / gTexSize;
  return gFrame.SampleLevel(gFrameSampler, at, 0).rgb;
}

// The map layer at uv, filtered as Scaled filters the frame; its alpha 0 where there is none.
float4 Ground(float2 uv, float2 span)
{
  float w;
  float h;
  gGround.GetDimensions(w, h);
  float2 size = float2(w, h);
  float2 per = size * span / gDst.zw;  // the map's texels a window pixel covers
  if (any(per > 1.5)) {
    // zoomed far out: the average over the pixel, from up to 4 by 4 linear taps, each of
    // which covers two texels across, so the map does not shimmer as it moves
    int2 n = clamp(int2(ceil(per * 0.5)), int2(1, 1), int2(4, 4));
    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    for (int j = 0; j < n.y; ++j) {
      for (int i = 0; i < n.x; ++i) {
        float2 at = uv * size + ((float2(i, j) + 0.5) / float2(n) - 0.5) * per;
        sum += gGround.SampleLevel(gGroundSampler, at / size, 0);
      }
    }
    return sum / (n.x * n.y);
  }
  float2 box = lerp(clamp(per, 1e-5, 1.0), 1.0, gArtSoft);
  float2 tx = uv * size - 0.5 * box;
  float2 offset = smoothstep(1.0 - box, 1.0, frac(tx));
  return gGround.SampleLevel(gGroundSampler, (floor(tx) + 0.5 + offset) / size, 0);
}

bool ScaleIsMap(float2 uv)
{
  uint k = (uint)max(gCoverScale, 1.0);
  uint2 texel = min(uint2(uv * gTexSize), uint2(gTexSize) - 1u) / k;
  uint nAt = texel.y * ((uint)gTexSize.x / k) + texel.x;
  if (gHasHud > 0.5 && LoadByte(gScaleHud, nAt) != 0u)
    return false;
  return gHasCover < 0.5 || LoadByte(gScaleCover, nAt) != 0u;
}

bool ScaleInside(float2 uv)
{
  return all(uv >= 0.0) && all(uv < 1.0);
}

// Where the map is shown in place of a pixel that is not the map's when PSGround has no map
// there (1.17's own view, whose map stops at row 400, or no GPU scene): the map just above
// its edge, mirrored, so the console leaves ground behind it as it rises and not its own
// black shape. False when there is no map above it.
bool ScaleMapUnder(inout float2 m)
{
  const float dy = max(gCoverScale, 1.0) / gTexSize.y;  // a row of the cover
  for (int k = 1; k <= 200; ++k) {
    float y = m.y - k * dy;
    if (y < 0.0)
      return false;
    if (ScaleIsMap(float2(m.x, y))) {
      m.y = max(y - k * dy, 0.0);
      return true;
    }
  }
  return false;
}

float4 PSScale(VSOut i) : SV_Target
{
  float2 uv = (i.pos.xy - gDst.xy) / gDst.zw;
  if (gMoved < 0.5 && gHudShift == 0.0 && gHudAlpha == 1.0)
    return float4(Scaled(uv, float2(1.0, 1.0)), 1.0);
  // the map moved, from the map layer where there is one, from the frame where not (the
  // mirror under the console at a skirmish's start), black past both; the rest over it,
  // shifted and faded
  float2 m = gMapAt.xy + uv * gMapAt.zw;
  float2 f = gFrameAt.xy + uv * gFrameAt.zw;
  float3 c = float3(0.0, 0.0, 0.0);
  float4 g = gHasGround > 0.5 && ScaleInside(m) ? Ground(m, gMapAt.zw) : float4(0.0, 0.0, 0.0, 0.0);
  if (g.a > 0.5)
    c = g.rgb;
  else if (ScaleInside(f) && (ScaleIsMap(f) || ScaleMapUnder(f)))
    c = Scaled(f, gFrameAt.zw);
  // the console rises into place; the resources and the timer at the top only fade in,
  // where they stand (the user, 2026-10-14)
  float2 h = uv.y < 0.5 ? uv : uv - float2(0.0, gHudShift);
  if (gHudAlpha > 0.0 && ScaleInside(h) && !ScaleIsMap(h))
    c = lerp(c, Scaled(h, float2(1.0, 1.0)), gHudAlpha);
  return float4(c, 1.0);
}
