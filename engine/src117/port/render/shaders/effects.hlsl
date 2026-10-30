// effects.hlsl -- the effects after the scene (port_gpu.cpp; docs/renderer.md).
//
// They take the resolved frame (present.hlsl's PSResolve or PSCompose) and draw it into
// the window in PSScale's place:
//
//   PSBright  the frame's bright pixels at half its size, those of the map only when the
//             cover says which are (the GPU scene's pixels, HYPERION_GPU_SCENE=gpu), so
//             the console and the text do not glow; colours from sRGB to linear
//   PSDown    each smaller level, a quarter of the one above (the dual filter's 5 taps)
//   PSUp      back up the levels, each the one below spread out (8 taps) and its own
//             level added: the bloom, wide and soft
//   PSLights  the 2D lights (port_lights.cpp) added up at half the
//             frame's size: each a colour that falls off smoothly to nothing at its reach
//   PSPost    the frame with PSScale's pixel-art filter, the map lit by the lights, the
//             bloom added, the map's colours graded (exposure, white balance, contrast,
//             saturation), and a vignette, at the window's resolution, so the glow and
//             the light stay smooth; at a skirmish's start (port_intro.cpp) the map
//             scaled about the frame's middle and the rest (the console, the resources)
//             shifted down and faded, each where the cover says it is
//
// Presentation only: the game's screen and the check (which reads the resolved frame)
// are as without them. Registers as present.hlsl: textures and samplers in space 2, then
// the storage buffers; uniforms in space 3.

struct VSOut
{
  float4 pos : SV_Position;
};

float3 ToLinear(float3 c) { return pow(max(c, 0.0), 2.2); }
float3 ToGamma(float3 c) { return pow(max(c, 0.0), 1.0 / 2.2); }

uint LoadByte(ByteAddressBuffer b, uint nAt)
{
  return (b.Load(nAt & ~3u) >> ((nAt & 3u) * 8u)) & 0xFFu;
}

// The bloom passes' values.
[[vk::binding(0, 3)]] cbuffer BloomParams : register(b0, space3)
{
  float2 gDstSize;    // the target's size
  float2 gSrcSize;    // the source's size
  float gThreshold;   // brightness the glow starts at, 0 to 1
  float gKnee;        // how softly it starts
  float gRadius;      // how far the taps reach, in source texels
  float gHasCover;    // 1 when the cover marks the map's pixels
};

// ---------------------------------------------------------------------------
// PSBright: the frame (t0) and the cover (t1, a byte a pixel in the frame's rows).

[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> gSource : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState gSourceSampler : register(s0, space2);
[[vk::binding(1, 2)]] ByteAddressBuffer gBrightCover : register(t1, space2);

float3 BrightTexel(int2 p)
{
  p = clamp(p, int2(0, 0), int2(gSrcSize) - 1);
  if (gHasCover > 0.5 && LoadByte(gBrightCover, (uint)p.y * (uint)gSrcSize.x + (uint)p.x) == 0u)
    return float3(0, 0, 0);
  return ToLinear(gSource.Load(int3(p, 0)).rgb);
}

float4 PSBright(VSOut i) : SV_Target
{
  int2 p = int2(i.pos.xy) * 2;
  float3 c = 0.25 * (BrightTexel(p) + BrightTexel(p + int2(1, 0)) + BrightTexel(p + int2(0, 1)) +
                     BrightTexel(p + int2(1, 1)));
  // the soft threshold: nothing below threshold - knee, all of it above threshold + knee.
  // Brightness is mostly luminance, so energy and fire glow and the blood's deep red does not.
  float br = max(dot(c, float3(0.2126, 0.7152, 0.0722)), 0.6 * max(c.r, max(c.g, c.b)));
  float soft = clamp(br - gThreshold + gKnee, 0.0, 2.0 * gKnee);
  soft = soft * soft / (4.0 * gKnee + 1e-5);
  float w = max(soft, br - gThreshold) / max(br, 1e-5);
  return float4(c * w, 1.0);
}

// ---------------------------------------------------------------------------
// PSDown: the level above (t0), sampled bilinearly.

float4 PSDown(VSOut i) : SV_Target
{
  float2 uv = i.pos.xy / gDstSize;
  float2 o = gRadius / gSrcSize;
  float3 c = gSource.SampleLevel(gSourceSampler, uv, 0).rgb * 4.0;
  c += gSource.SampleLevel(gSourceSampler, uv - o, 0).rgb;
  c += gSource.SampleLevel(gSourceSampler, uv + o, 0).rgb;
  c += gSource.SampleLevel(gSourceSampler, uv + float2(o.x, -o.y), 0).rgb;
  c += gSource.SampleLevel(gSourceSampler, uv - float2(o.x, -o.y), 0).rgb;
  return float4(c / 8.0, 1.0);
}

// ---------------------------------------------------------------------------
// PSUp: the level below (t0, the source) spread out, and this level's own (t1).

[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] Texture2D<float4> gLevel : register(t1, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] SamplerState gLevelSampler : register(s1, space2);

float4 PSUp(VSOut i) : SV_Target
{
  float2 uv = i.pos.xy / gDstSize;
  float2 o = 0.5 * gRadius / gSrcSize;
  float3 c = gSource.SampleLevel(gSourceSampler, uv + float2(-2.0 * o.x, 0), 0).rgb;
  c += gSource.SampleLevel(gSourceSampler, uv + float2(-o.x, o.y), 0).rgb * 2.0;
  c += gSource.SampleLevel(gSourceSampler, uv + float2(0, 2.0 * o.y), 0).rgb;
  c += gSource.SampleLevel(gSourceSampler, uv + float2(o.x, o.y), 0).rgb * 2.0;
  c += gSource.SampleLevel(gSourceSampler, uv + float2(2.0 * o.x, 0), 0).rgb;
  c += gSource.SampleLevel(gSourceSampler, uv + float2(o.x, -o.y), 0).rgb * 2.0;
  c += gSource.SampleLevel(gSourceSampler, uv + float2(0, -2.0 * o.y), 0).rgb;
  c += gSource.SampleLevel(gSourceSampler, uv + float2(-o.x, -o.y), 0).rgb * 2.0;
  return float4(c / 12.0 + gLevel.SampleLevel(gLevelSampler, uv, 0).rgb, 1.0);
}

// ---------------------------------------------------------------------------
// PSLights: the frame's lights (t0, port_lights.h's PortLightGpu), in the frame's pixels.

struct Light
{
  float2 pos;
  float radius;
  float intensity;
  float3 color;
  float pad;
};

[[vk::binding(0, 2)]] StructuredBuffer<Light> gLights : register(t0, space2);

[[vk::binding(0, 3)]] cbuffer LightParams : register(b0, space3)
{
  float2 gLightDstSize;   // the light buffer's size
  float2 gLightFrameSize; // the frame's
  uint gLightCount;
  float gLightUnused;
  float2 gLightUnused2;
};

float4 PSLights(VSOut i) : SV_Target
{
  float2 p = i.pos.xy * gLightFrameSize / gLightDstSize;  // in the frame's pixels
  float3 sum = float3(0, 0, 0);
  for (uint n = 0; n < gLightCount; ++n) {
    Light l = gLights[n];
    float2 d = p - l.pos;
    float f = saturate(1.0 - dot(d, d) / (l.radius * l.radius));
    sum += l.color * (l.intensity * f * f);
  }
  return float4(sum, 1.0);
}

// ---------------------------------------------------------------------------
// PSPost: the frame (t0), the bloom (t1), the lights (t2), the map layer when the map moves
// under the rest (t3, present.hlsl's PSGround: a skirmish's start, the camera zoomed out),
// the cover (t4), and zoomed, where the dialogs drew over the map (t5, scene.hlsl's CSHud).

[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> gFrame : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState gFrameSampler : register(s0, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] Texture2D<float4> gBloom : register(t1, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] SamplerState gBloomSampler : register(s1, space2);
[[vk::combinedImageSampler]] [[vk::binding(2, 2)]] Texture2D<float4> gLight : register(t2, space2);
[[vk::combinedImageSampler]] [[vk::binding(2, 2)]] SamplerState gLightSampler : register(s2, space2);
[[vk::combinedImageSampler]] [[vk::binding(3, 2)]] Texture2D<float4> gGround : register(t3, space2);
[[vk::combinedImageSampler]] [[vk::binding(3, 2)]] SamplerState gGroundSampler : register(s3, space2);
[[vk::binding(4, 2)]] ByteAddressBuffer gPostCover : register(t4, space2);
[[vk::binding(5, 2)]] ByteAddressBuffer gPostHud : register(t5, space2);

[[vk::binding(0, 3)]] cbuffer PostParams : register(b0, space3)
{
  float4 gDst;          // the letterbox in the target's pixels: x, y, width, height
  float2 gTexSize;      // the frame's size in texels
  float gPostHasCover;
  float gBloomStrength;
  float gExposure;      // in stops
  float gContrast;
  float gSaturation;
  float gTemperature;   // -1 cooler to 1 warmer
  float gTint;          // -1 greener to 1 more magenta
  float gVignette;      // how dark the corners go
  float gVignetteSoft;  // how far in the darkening starts
  float gCoverScale;    // the frame's texels to a pixel of the cover: 1, or the HD frame's scale
  float gLightsOn;      // 1 when the lights are drawn
  float gLightStrength; // how strongly they light the map
  float gAmbient;       // the map's light where no light reaches: 1 as drawn, less for night
  float gLightAdd;      // how much of the light is added as well, so it shows on dark ground
  float gMoved;         // 1 when the map moves under the rest (present.hlsl, PSScale)
  float gHudShift;      // the rest shifted down by this part of the frame's height,
  float gHudAlpha;      // and its opacity; 0, 1 otherwise
  float gPostHasGround; // 1 when gGround holds the map layer
  float4 gMapAt;        // the map layer's place: at uv, PSGround's layer at xy + uv * zw
  float4 gFrameAt;      // the frame's, where the map is taken from it without the layer
  float4 gLightAt;      // and the lights', whose texture is the game's own view
  float gPostHasHud;    // 1 when gPostHud says where the dialogs drew over the map
  float gPostArtSoft;   // Settings' Classic art filtering, as present.hlsl's gArtSoft
  float2 gPostUnused;
};

// Whether the frame's pixel at uv is the map's, as the cover says (all of it without one).
bool IsMap(float2 uv)
{
  uint k = (uint)max(gCoverScale, 1.0);
  uint2 texel = min(uint2(uv * gTexSize), uint2(gTexSize) - 1u) / k;
  uint nAt = texel.y * ((uint)gTexSize.x / k) + texel.x;
  if (gPostHasHud > 0.5 && LoadByte(gPostHud, nAt) != 0u)
    return false;
  return gPostHasCover < 0.5 || LoadByte(gPostCover, nAt) != 0u;
}

// The map layer at uv as PSScale's filter samples it, its texels span apart in uv a window
// pixel's span; its alpha 0 where there is none.
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
  float2 box = lerp(clamp(per, 1e-5, 1.0), 1.0, gPostArtSoft);
  float2 tx = uv * size - 0.5 * box;
  float2 offset = smoothstep(1.0 - box, 1.0, frac(tx));
  return gGround.SampleLevel(gGroundSampler, (floor(tx) + 0.5 + offset) / size, 0);
}

// Colour c of the frame with the effects, as the map's or not: its bloom at uvBloom in the
// frame, its light at uvLight in the game's view.
float3 GradedColour(float3 c, float2 uvBloom, float2 uvLight, bool bMap)
{
  float3 lin = ToLinear(c);
  if (bMap && gLightsOn > 0.5) {
    // compressed, so lights that overlap brighten toward full light and not to white
    float3 light = 1.0 - exp(-gLight.SampleLevel(gLightSampler, uvLight, 0).rgb * gLightStrength);
    lin = lin * (gAmbient + light) + light * gLightAdd;
  }
  lin += gBloom.SampleLevel(gBloomSampler, uvBloom, 0).rgb * gBloomStrength;
  if (bMap) {
    lin *= exp2(gExposure);
    lin *= float3(1.0 + 0.12 * gTemperature, 1.0 - 0.06 * gTint, 1.0 - 0.12 * gTemperature);
  }
  c = ToGamma(lin);
  if (bMap) {
    c = (c - 0.5) * gContrast + 0.5;
    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = lerp(float3(luma, luma, luma), c, gSaturation);
  }
  return c;
}

// The frame at uv as PSScale samples it, its texels span apart in uv a window pixel's span.
float3 Framed(float2 uv, float2 span)
{
  float2 box = lerp(clamp(gTexSize * span / gDst.zw, 1e-5, 1.0), 1.0, gPostArtSoft);
  float2 tx = uv * gTexSize - 0.5 * box;
  float2 offset = smoothstep(1.0 - box, 1.0, frac(tx));
  return gFrame.SampleLevel(gFrameSampler, (floor(tx) + 0.5 + offset) / gTexSize, 0).rgb;
}

// The frame at uv with the effects, as the map's or not.
float3 Graded(float2 uv, bool bMap)
{
  return GradedColour(Framed(uv, float2(1.0, 1.0)), uv, uv, bMap);
}

bool Inside(float2 uv)
{
  return all(uv >= 0.0) && all(uv < 1.0);
}

// The map under the console when the map layer has none there: the map just above its edge,
// mirrored (present.hlsl, ScaleMapUnder).
bool MapUnder(inout float2 m)
{
  const float dy = max(gCoverScale, 1.0) / gTexSize.y;  // a row of the cover
  for (int k = 1; k <= 200; ++k) {
    float y = m.y - k * dy;
    if (y < 0.0)
      return false;
    if (IsMap(float2(m.x, y))) {
      m.y = max(y - k * dy, 0.0);
      return true;
    }
  }
  return false;
}

float4 PSPost(VSOut i) : SV_Target
{
  float2 uv = (i.pos.xy - gDst.xy) / gDst.zw;
  float3 c;
  if (gMoved < 0.5 && gHudShift == 0.0 && gHudAlpha == 1.0) {
    c = Graded(uv, IsMap(uv));
  } else {
    // the map moved, from the map layer where there is one, from the frame where not, black
    // past both; the rest over it, shifted and faded
    float2 m = gMapAt.xy + uv * gMapAt.zw;
    float2 f = gFrameAt.xy + uv * gFrameAt.zw;
    float2 l = gLightAt.xy + uv * gLightAt.zw;
    c = float3(0.0, 0.0, 0.0);
    float4 g = gPostHasGround > 0.5 && Inside(m) ? Ground(m, gMapAt.zw) : float4(0.0, 0.0, 0.0, 0.0);
    if (g.a > 0.5)
      c = GradedColour(g.rgb, f, l, true);
    else if (Inside(f) && (IsMap(f) || MapUnder(f)))
      c = GradedColour(Framed(f, gFrameAt.zw), f, l, true);
    // the console rises into place; the resources and the timer only fade in
    float2 h = uv.y < 0.5 ? uv : uv - float2(0.0, gHudShift);
    if (gHudAlpha > 0.0 && Inside(h) && !IsMap(h))
      c = lerp(c, Graded(h, false), gHudAlpha);
  }
  // the vignette over the whole frame, round on any shape of window
  float2 q = (uv - 0.5) * float2(gDst.z / gDst.w, 1.0);
  float r = length(q) / length(float2(0.5 * gDst.z / gDst.w, 0.5));
  c *= 1.0 - gVignette * smoothstep(1.0 - gVignetteSoft, 1.0, r);
  return float4(saturate(c), 1.0);
}
