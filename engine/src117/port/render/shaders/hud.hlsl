// hud.hlsl -- the HUD's passes (port_gpu_hud.cpp) other than its shape layers'
// (hudshape.hlsl): a layer's effects on its own pixels, its glow, its placing, and the blur of
// the screen behind glass. They are the design center's player's (hudplayer.js, drawLayer;
// hudshape.js, backdrop), which draws them on canvases. Every colour a pass writes is
// premultiplied by its alpha. The registers follow SDL_GPU's layout: a vertex shader's uniforms
// in space 1, a fragment shader's textures in space 2 and its uniforms in space 3.

struct VSHudOut
{
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

// The viewport whole, uv 0 to 1 from its top left.
VSHudOut VSHudQuad(uint id : SV_VertexID)
{
  VSHudOut o;
  float2 p = float2((id << 1) & 2, id & 2);
  o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
  o.uv = p;
  return o;
}

// A quad placed: the unit square through gM into the target's pixels, two triangles.
[[vk::binding(0, 1)]] cbuffer HudPlace : register(b0, space1)
{
  float4 gM;       // (x, y) of the square to (a x + c y + e, b x + d y + f): a, b, c, d
  float4 gMove;    // e, f, the target's width and height in pixels
  float4 gUvRect;  // the texture's part it shows: u0, v0, u1, v1
};

static const uint kCorner[6] = {0, 1, 2, 2, 1, 3};

VSHudOut VSHudPlace(uint id : SV_VertexID)
{
  VSHudOut o;
  uint c = kCorner[id % 6];
  float2 q = float2(c & 1, c >> 1);
  float2 at = float2(gM.x * q.x + gM.z * q.y + gMove.x, gM.y * q.x + gM.w * q.y + gMove.y);
  o.pos = float4(at.x / gMove.z * 2.0 - 1.0, 1.0 - at.y / gMove.w * 2.0, 0, 1);
  o.uv = lerp(gUvRect.xy, gUvRect.zw, q);
  return o;
}

[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> gSource : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState gSourceSampler : register(s0, space2);

// A placed quad's pixels: the texture's, at an opacity. The blend says how they go on.
[[vk::binding(0, 3)]] cbuffer HudPlaced : register(b0, space3)
{
  float4 gAlpha;  // x the opacity
};

float4 PSHudPlaced(VSHudOut i) : SV_Target
{
  return gSource.Sample(gSourceSampler, i.uv) * gAlpha.x;
}

// A layer's effects on its own pixels, in drawLayer's order: the meter (part of it shown, from a
// side), tint (multiply), add (a colour added), scan (a band or lines swept across, added),
// wipe (a soft-edged reveal). gSource is the layer drawn, the target the same size.
[[vk::binding(0, 3)]] cbuffer HudFxParams : register(b0, space3)
{
  float4 gFxBox;      // the box's width and height, its margin, in units; units per pixel
  float4 gFxUv;       // the layer's part of gSource: u0, v0, u1, v1
  float4 gTint;       // straight alpha
  float4 gAddColor;   // straight alpha
  float4 gScanColor;  // straight alpha
  float4 gAddScan;    // add's strength, scan's position (0 to 1; below -1 none), its strength, its mode (0 band, 1 lines)
  float4 gScanShape;  // scan's angle (radians), band width, line spacing, the span's offset (lines)
  float4 gWipe;       // wipe (1 shown), its angle (radians), its softness, 0
  float4 gMeter;      // the meter (1 all of it), its side (0 left, 1 right, 2 top, 3 bottom), 0, 0
};

// the corners of a w x h box from its top left, along an angle: least and most
float2 HudSpan(float w, float h, float2 dir)
{
  float4 p = float4(0.0, w * dir.x, h * dir.y, w * dir.x + h * dir.y);
  return float2(min(min(p.x, p.y), min(p.z, p.w)), max(max(p.x, p.y), max(p.z, p.w)));
}

float4 PSHudFx(VSHudOut i) : SV_Target
{
  float4 c = gSource.Sample(gSourceSampler, lerp(gFxUv.xy, gFxUv.zw, i.uv));
  float w = gFxBox.x, h = gFxBox.y, m = gFxBox.z;
  float2 q = i.uv * float2(w + 2.0 * m, h + 2.0 * m) - float2(m, m);  // units from the box's top left
  if (gMeter.x < 1.0) {
    float f = max(gMeter.x, 0.0);
    float side = gMeter.y;
    bool shown = side < 0.5 ? q.x < w * f : side < 1.5 ? q.x >= w * (1.0 - f) : side < 2.5 ? q.y < h * f : q.y >= h * (1.0 - f);
    if (!shown || q.x < 0.0 || q.y < 0.0 || q.x >= w || q.y >= h)
      c = float4(0.0, 0.0, 0.0, 0.0);
  }
  float a0 = c.a;
  if (any(gTint != float4(1.0, 1.0, 1.0, 1.0)))
    c.rgb = lerp(c.rgb, c.rgb * gTint.rgb, gTint.a);
  if (gAddScan.x > 0.0) {
    float k = min(1.0, gAddScan.x) * gAddColor.a * a0;
    c.rgb += gAddColor.rgb * k;
    c.a = saturate(c.a + k);
  }
  if (gAddScan.y > -1.0 && gAddScan.z > 0.0) {
    float2 dir = float2(cos(gScanShape.x), sin(gScanShape.x));
    float2 sp = HudSpan(w, h, dir);
    float band, colA;
    if (gAddScan.w > 0.5) {
      // lines across the angle, gap apart, measured from the box's centre
      float gap = max(2.0, gScanShape.z);
      float R = length(float2(w, h)) * 0.5 + gap;
      float t = dot(q - float2(w, h) * 0.5, dir) + R - gScanShape.w;
      band = (t >= 0.0 && t - floor(t / gap) * gap < max(1.0, gap * 0.4)) ? 1.0 : 0.0;
      colA = gScanColor.a;
    } else {
      float wdt = max(1.0, gScanShape.y);
      float pos = sp.x - wdt + gAddScan.y * (sp.y - sp.x + 2.0 * wdt);
      float t = (dot(q, dir) - pos) / wdt;
      band = t >= 0.0 && t <= 1.0 ? 1.0 - abs(t * 2.0 - 1.0) : 0.0;
      colA = 1.0;
    }
    float k = band * colA * a0 * min(1.0, gAddScan.z);
    c.rgb += gScanColor.rgb * k;
    c.a = saturate(c.a + k);
  }
  if (gWipe.x < 1.0) {
    float2 dir = float2(cos(gWipe.y), sin(gWipe.y));
    float2 sp = HudSpan(w, h, dir);
    float soft = max(0.01, gWipe.z);
    float edge = sp.x + max(0.0, gWipe.x) * (sp.y - sp.x + soft);
    c *= saturate((edge - dot(q, dir)) / soft);
  }
  return c;
}

// A layer's glow: its alpha blurred (a Gaussian, one direction a pass) and, on the second
// pass, coloured. gSource is the layer for the first pass, the first pass's result for the
// second.
[[vk::binding(0, 3)]] cbuffer HudBlurParams : register(b0, space3)
{
  float4 gBlurStep;   // a texel along the pass's direction (u, v); sigma in texels; taps each side
  float4 gBlurUv;     // the part of the source the target covers: u0, v0, u1, v1
  float4 gBlurValid;  // the part that holds the layer, nothing outside it: u0, v0, u1, v1
  float4 gBlurColor;  // the glow's colour, straight alpha; its alpha 0 on the first pass
};

float4 PSHudBlur(VSHudOut i) : SV_Target
{
  float2 uv = lerp(gBlurUv.xy, gBlurUv.zw, i.uv);
  float sigma = max(gBlurStep.z, 0.5);
  int n = (int)gBlurStep.w;
  float sum = 0.0, wsum = 0.0;
  for (int k = -n; k <= n; ++k) {
    float wk = exp(-0.5 * (k * k) / (sigma * sigma));
    float2 at = uv + gBlurStep.xy * k;
    float inside = all(at >= gBlurValid.xy) && all(at <= gBlurValid.zw) ? 1.0 : 0.0;
    sum += gSource.SampleLevel(gSourceSampler, at, 0).a * wk * inside;
    wsum += wk;
  }
  float a = sum / wsum;
  if (gBlurColor.a <= 0.0)
    return float4(0.0, 0.0, 0.0, a);
  return float4(gBlurColor.rgb * gBlurColor.a * a, gBlurColor.a * a);
}

// The screen behind glass, blurred as the design center blurs it (hudshape.js, backdrop): a
// dual filter, each level down a 5-tap average at half the size, then each back up an 8-tap.
[[vk::binding(0, 3)]] cbuffer HudKawase : register(b0, space3)
{
  float4 gTexel;  // a texel of the source: 1 / width, 1 / height
};

float4 PSHudDown(VSHudOut i) : SV_Target
{
  float2 h = gTexel.xy * 0.5;
  float4 o = gSource.Sample(gSourceSampler, i.uv) * 4.0 + gSource.Sample(gSourceSampler, i.uv - h) +
             gSource.Sample(gSourceSampler, i.uv + h) + gSource.Sample(gSourceSampler, i.uv + float2(h.x, -h.y)) +
             gSource.Sample(gSourceSampler, i.uv - float2(h.x, -h.y));
  return o / 8.0;
}

float4 PSHudUp(VSHudOut i) : SV_Target
{
  float2 h = gTexel.xy * 0.5;
  float4 o = gSource.Sample(gSourceSampler, i.uv + float2(-h.x * 2.0, 0.0)) +
             gSource.Sample(gSourceSampler, i.uv + float2(-h.x, h.y)) * 2.0 +
             gSource.Sample(gSourceSampler, i.uv + float2(0.0, h.y * 2.0)) +
             gSource.Sample(gSourceSampler, i.uv + float2(h.x, h.y)) * 2.0 +
             gSource.Sample(gSourceSampler, i.uv + float2(h.x * 2.0, 0.0)) +
             gSource.Sample(gSourceSampler, i.uv + float2(h.x, -h.y)) * 2.0 +
             gSource.Sample(gSourceSampler, i.uv + float2(0.0, -h.y * 2.0)) +
             gSource.Sample(gSourceSampler, i.uv + float2(-h.x, -h.y)) * 2.0;
  return o / 12.0;
}
