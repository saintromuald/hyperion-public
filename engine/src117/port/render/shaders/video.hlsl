// video.hlsl -- a video in full colour over the frame, a cinematic (port_gpu_video.cpp).
//
// PSVideoColour puts a picture's planes in colour at the video's size, one texel a pixel, into a
// 16-bit float target. PSVideoScale resamples that in one direction a pass, with Catmull-Rom's
// cubic widened as far as the picture shrinks: across into a target the drawn width by the
// video's rows, then down into the window, dithered there to the swapchain's 8 bits.
//
// Built as present.hlsl is: textures and their samplers in space 2 (set 2), uniforms in space 3
// (set 3), combined image samplers for SDL's Vulkan backend.

struct VSOut
{
  float4 pos : SV_Position;
};

// The planes: Y, Cb and Cr (G, B and R for an RGB video), each R8 or R16; chroma in one plane is
// plane 1's two channels, Cb first.
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> gPlaneY : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState gPlaneYSampler : register(s0, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] Texture2D<float4> gPlaneCb : register(t1, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] SamplerState gPlaneCbSampler : register(s1, space2);
[[vk::combinedImageSampler]] [[vk::binding(2, 2)]] Texture2D<float4> gPlaneCr : register(t2, space2);
[[vk::combinedImageSampler]] [[vk::binding(2, 2)]] SamplerState gPlaneCrSampler : register(s2, space2);

[[vk::binding(0, 3)]] cbuffer VideoColourParams : register(b0, space3)
{
  float4 gLevels;  // a texel to Y': x times it plus y; to Cb or Cr: z times it plus w
  float4 gToR;     // R' from Y', Cb and Cr: their dot product with xyz
  float4 gToG;
  float4 gToB;
  float4 gKind;    // x 1 for the chroma in one plane, y 1 for none, z 1 for G, B and R planes
};

float4 PSVideoColour(VSOut i) : SV_Target
{
  int2 p = int2(i.pos.xy);
  uint w;
  uint h;
  gPlaneY.GetDimensions(w, h);
  float y = gPlaneY.Load(int3(p, 0)).r;
  // the chroma at the pixel's centre, between its samples where it has fewer
  float2 uv = (float2(p) + 0.5) / float2(w, h);
  float2 c = gKind.x > 0.5 ? gPlaneCb.SampleLevel(gPlaneCbSampler, uv, 0).rg
                           : float2(gPlaneCb.SampleLevel(gPlaneCbSampler, uv, 0).r,
                                    gPlaneCr.SampleLevel(gPlaneCrSampler, uv, 0).r);
  if (gKind.z > 0.5)
    return float4(saturate(float3(c.y, y, c.x) * gLevels.x + gLevels.y), 1.0);
  float2 cbcr = gKind.y > 0.5 ? float2(0.0, 0.0) : c * gLevels.z + gLevels.w;
  float3 ycc = float3(y * gLevels.x + gLevels.y, cbcr);
  return float4(saturate(float3(dot(gToR.xyz, ycc), dot(gToG.xyz, ycc), dot(gToB.xyz, ycc))), 1.0);
}

[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> gScaleFrom : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState gScaleFromSampler : register(s0, space2);

[[vk::binding(0, 3)]] cbuffer VideoScaleParams : register(b0, space3)
{
  float4 gScaleBox;   // where the pass draws in its target: x, y, width, height
  float4 gScaleSrc;   // the source's width and rows, its pixels to one drawn this way, 1 across
  float4 gScaleMore;  // x 1 to dither, y the frame's seed
};

// Catmull-Rom's cubic, over two samples either side.
float Cubic(float x)
{
  x = abs(x);
  if (x < 1.0)
    return (1.5 * x - 2.5) * x * x + 1.0;
  if (x < 2.0)
    return ((-0.5 * x + 2.5) * x - 4.0) * x + 2.0;
  return 0.0;
}

uint Hash(uint x)
{
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

float4 PSVideoScale(VSOut i) : SV_Target
{
  float2 at = i.pos.xy - gScaleBox.xy;
  bool bAcross = gScaleSrc.w > 0.5;
  float fDrawn = bAcross ? gScaleBox.z : gScaleBox.w;
  int nSrc = int(bAcross ? gScaleSrc.x : gScaleSrc.y);
  float fWide = max(gScaleSrc.z, 1.0);
  // the pixel's centre in the source's pixels, and the line it keeps the other way
  float fAt = (bAcross ? at.x : at.y) * float(nSrc) / fDrawn - 0.5;
  int nLine = int(bAcross ? at.y : at.x);
  int nFrom = int(floor(fAt - 2.0 * fWide)) + 1;
  int nTo = int(floor(fAt + 2.0 * fWide));
  float3 sum = float3(0.0, 0.0, 0.0);
  float fWeights = 0.0;
  for (int k = nFrom; k <= nTo; ++k) {
    int s = clamp(k, 0, nSrc - 1);
    float wt = Cubic((float(k) - fAt) / fWide);
    sum += wt * gScaleFrom.Load(int3(bAcross ? int2(s, nLine) : int2(nLine, s), 0)).rgb;
    fWeights += wt;
  }
  float3 c = saturate(sum / fWeights);
  if (gScaleMore.x > 0.5) {
    // triangular noise of one step either way, so the video's finer levels show as a grain
    // rather than bands; none at black and white, which it would only lift or dim
    uint2 p = uint2(i.pos.xy);
    uint nSeed = uint(gScaleMore.y);
    uint a = Hash(p.x + Hash(p.y + Hash(nSeed)));
    uint b = Hash(a);
    float fNoise = (float(a & 0xFFFFu) + float(b & 0xFFFFu)) / 65536.0 - 1.0;
    float3 fRoom = saturate(min(c, 1.0 - c) * 255.0);
    c = saturate(c + fRoom * fNoise / 255.0);
  }
  return float4(c, 1.0);
}
