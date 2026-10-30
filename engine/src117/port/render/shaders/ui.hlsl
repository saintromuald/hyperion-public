// ui.hlsl -- the front end's shaders (port_gpu.cpp, docs/front-end.md, F1).
//
// Two passes draw the front end. PSPlanet draws the world behind every screen into a
// texture of its own: one triangle over the target, each pixel's ray traced to the planet
// and its moons, everything the planet shows computed from its entry in the world table
// (port_world.cpp). It is the design's worlds.js, in HLSL. VSUi and PSUi then draw the
// screen's elements over it, each a quad the vertex shader builds out of a list of
// primitives: rectangles with rounded corners, borders, glyphs from the font atlas, the
// planet itself, the planet blurred behind a see-through panel, and pictures from the
// picture atlas.
//
// Built by tools/build_engine_x64.py with dxc, to DXIL for Direct3D 12 and to SPIR-V for
// Vulkan. The registers follow SDL_GPU's layout: a vertex shader's storage buffers in space 0
// (set 0) and its uniforms in space 1 (set 1); a fragment shader's textures and samplers in
// space 2 (set 2) and its uniforms in space 3 (set 3).

struct UiPrim
{
  float4 rect;    // x, y, width, height, in the target's pixels
  float4 color;   // straight RGBA
  float4 color2;  // the gradient's far colour
  float4 uv;      // a glyph's or picture's place in its atlas: u0, v0, u1, v1; for a rectangle, a turn:
                  // the angle in radians and the point it turns about
  float4 clip;    // x0, y0, x1, y1; nothing outside it is drawn
  float4 shape;   // corner radius, border width (a picture: 1 for its pixels as squares), kind,
                  // gradient (0 none, 1 down, 2 across, 3 across from color2 to color at the
                  // middle and back)
};

// The kinds of primitive (port_ui.h, UiPrimKind).
static const float KIND_FILL = 0;
static const float KIND_RING = 1;
static const float KIND_GLYPH = 2;
static const float KIND_PLANET = 3;
static const float KIND_BLUR = 4;
static const float KIND_PICTURE = 5;

[[vk::binding(0, 0)]] StructuredBuffer<UiPrim> gPrims : register(t0, space0);

[[vk::binding(0, 1)]] cbuffer UiTarget : register(b0, space1)
{
  float2 gTarget;  // the target's size in pixels
  float2 gUnused;
};

// The primitive goes to the fragment shader whole, the same at every pixel.
struct UiOut
{
  float4 pos : SV_Position;
  float2 local : TEXCOORD0;   // the pixel's place in the quad, from its top left
  nointerpolation float4 rect : TEXCOORD1;
  nointerpolation float4 color : TEXCOORD2;
  nointerpolation float4 color2 : TEXCOORD3;
  nointerpolation float4 uv : TEXCOORD4;
  nointerpolation float4 clip : TEXCOORD5;
  nointerpolation float4 shape : TEXCOORD6;
};

// Six vertices a primitive, two triangles over its rectangle.
UiOut VSUi(uint id : SV_VertexID)
{
  static const float2 kCorner[6] = { float2(0, 0), float2(1, 0), float2(0, 1),
                                     float2(1, 0), float2(1, 1), float2(0, 1) };
  UiOut o;
  uint n = id / 6u;
  UiPrim p = gPrims[n];
  float2 c = kCorner[id % 6u];
  float2 at = p.rect.xy + c * p.rect.zw;
  if (p.shape.z != KIND_GLYPH && p.shape.z != KIND_PICTURE && p.uv.x != 0) {
    float sn = sin(p.uv.x);
    float cs = cos(p.uv.x);
    float2 d = at - p.uv.yz;
    at = p.uv.yz + float2(d.x * cs - d.y * sn, d.x * sn + d.y * cs);
  }
  o.pos = float4(at / gTarget * float2(2, -2) + float2(-1, 1), 0, 1);
  o.local = c * p.rect.zw;
  o.rect = p.rect;
  o.color = p.color;
  o.color2 = p.color2;
  o.uv = p.uv;
  o.clip = p.clip;
  o.shape = p.shape;
  return o;
}

// The target's size again, for the fragment shader, whose uniforms are in space 3.
[[vk::binding(0, 3)]] cbuffer UiTargetPS : register(b0, space3)
{
  float2 gTargetPS;
  float2 gUnusedPS;
};

[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> gAtlas : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState gAtlasSampler : register(s0, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] Texture2D<float4> gPlanet : register(t1, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] SamplerState gPlanetSampler : register(s1, space2);
[[vk::combinedImageSampler]] [[vk::binding(2, 2)]] Texture2D<float4> gPictures : register(t2, space2);
[[vk::combinedImageSampler]] [[vk::binding(2, 2)]] SamplerState gPicturesSampler : register(s2, space2);

// The distance from p to a box of half-size h with corners of radius r, negative inside.
float RoundBox(float2 p, float2 h, float r)
{
  float2 q = abs(p) - h + r;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float4 PSUi(UiOut i) : SV_Target
{
  UiPrim p;
  p.rect = i.rect;
  p.color = i.color;
  p.color2 = i.color2;
  p.uv = i.uv;
  p.clip = i.clip;
  p.shape = i.shape;
  float2 at = i.pos.xy;
  if (at.x < p.clip.x || at.y < p.clip.y || at.x >= p.clip.z || at.y >= p.clip.w)
    discard;
  float kind = p.shape.z;
  float4 c = p.color;
  if (p.shape.w == 1)
    c = lerp(p.color, p.color2, saturate(i.local.y / max(p.rect.w, 1.0)));
  else if (p.shape.w == 2)
    c = lerp(p.color, p.color2, saturate(i.local.x / max(p.rect.z, 1.0)));
  else if (p.shape.w == 3)
    c = lerp(p.color, p.color2, abs(saturate(i.local.x / max(p.rect.z, 1.0)) * 2.0 - 1.0));
  float2 hs = p.rect.zw * 0.5;
  float d = RoundBox(i.local - hs, hs, min(p.shape.x, min(hs.x, hs.y)));
  float a = saturate(0.5 - d);
  if (kind == KIND_RING)
    a *= saturate(0.5 + d + p.shape.y);
  float3 rgb = c.rgb;
  if (kind == KIND_GLYPH) {
    float2 uv = lerp(p.uv.xy, p.uv.zw, i.local / max(p.rect.zw, 1.0));
    a = gAtlas.SampleLevel(gAtlasSampler, uv, 0).r;
  } else if (kind == KIND_PICTURE) {
    float2 f = i.local / max(p.rect.zw, 1.0);
    float4 s;
    if (p.shape.y == 1) {
      // its pixels as squares: from the first texel's edge to the last's, each texel whole
      uint w, h;
      gPictures.GetDimensions(w, h);
      float2 size = float2(w, h);
      float2 uv = lerp(p.uv.xy - 0.5 / size, p.uv.zw + 0.5 / size, f);
      s = gPictures.Load(int3(min(floor(uv * size), size - 1), 0));
    } else {
      s = gPictures.SampleLevel(gPicturesSampler, lerp(p.uv.xy, p.uv.zw, f), 0);
    }
    return float4(s.rgb * c.rgb, a * s.a * c.a);
  } else if (kind == KIND_PLANET || kind == KIND_BLUR) {
    float2 uv = at / gTargetPS;
    float3 s = gPlanet.SampleLevel(gPlanetSampler, uv, 0).rgb;
    if (kind == KIND_BLUR) {
      // Twelve taps on two rings about the pixel, the CSS blur's radius out.
      float2 r = p.shape.y / gTargetPS;
      float3 sum = s * 2.0;
      [unroll] for (int k = 0; k < 6; ++k) {
        float t = k * 1.0471976;
        float2 dir = float2(cos(t), sin(t));
        sum += gPlanet.SampleLevel(gPlanetSampler, uv + dir * r * 0.5, 0).rgb;
        sum += gPlanet.SampleLevel(gPlanetSampler, uv + float2(dir.y, -dir.x) * r, 0).rgb * 0.75;
      }
      s = sum / (2.0 + 6.0 + 4.5);
    }
    rgb = s;
    a = a * c.a;
    return float4(rgb, a);
  }
  return float4(rgb, a * c.a);
}

// ---------------------------------------------------------------------------
// The planet.

struct VSOut
{
  float4 pos : SV_Position;
};

VSOut VSFull(uint id : SV_VertexID)
{
  VSOut o;
  float2 p = float2((id << 1) & 2, id & 2);
  o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
  return o;
}

// port_world.h, PlanetParams: the camera, the suns, and the world.
[[vk::binding(0, 3)]] cbuffer PlanetParams : register(b0, space3)
{
  float4 gRes;        // width, height, time, fade
  float4 gCamPos;     // xyz, tan(fov / 2)
  float4 gCamFwd;     // xyz, the clouds' drift
  float4 gCamRight;   // xyz, how much of the sun shows
  float4 gCamUp;      // xyz, the dimming behind the screen's panels
  float4 gSun;        // the direction to the sun; how much of the second sun shows
  float4 gSun2;       // the direction to the second sun; how much the scorched ground shines
  float4 gStar2;      // the second sun's light (0: none); the night lights' brightness
  float4 gSpin0;      // the planet's turn, as three columns
  float4 gSpin1;
  float4 gSpin2;
  float4 gSunScreen;  // the sun on the target, from its bottom left; continent size
  float4 gSeedV;      // which continents and clouds
  float4 gSea;        // sea level (-1 none), moisture, ice, dunes
  float4 gDeep;       // rgb
  float4 gShallow;
  float4 gL0;         // the land, low to high
  float4 gL1;
  float4 gL2;
  float4 gL3;
  float4 gA0;         // the wet ground
  float4 gA1;
  float4 gIceCol;     // rgb, craters
  float4 gRelief;     // relief, city, scar, creep
  float4 gCityAt;     // where the city gathers, and how far it spreads
  float4 gCityCol;
  float4 gCityDay;
  float4 gScarCol;
  float4 gCreepCol;
  float4 gCreepGlow;
  float4 gLavaCol;    // rgb, lava
  float4 gCloud;      // cover, bands, the air's thickness, the star's strength
  float4 gCloudCol;
  float4 gHaze;       // rgb, amount
  float4 gAtmo;
  float4 gStar;
  float4 gNebA;
  float4 gNebB;
  float4 gMoon0;      // centre, radius (0: none)
  float4 gMoon1;
  float4 gMoon2;
  float4 gMoonCol0;   // rgb, craters
  float4 gMoonCol1;
  float4 gMoonCol2;
  float4 gMoonGas;    // 1 for a gas giant's bands, per moon
};

#define uSea gSea.x
#define uMoist gSea.y
#define uIce gSea.z
#define uDunes gSea.w
#define uCraters gIceCol.w
#define uRelief gRelief.x
#define uCity gRelief.y
#define uScar gRelief.z
#define uCreep gRelief.w
#define uLava gLavaCol.w
#define uCloud gCloud.x
#define uBands gCloud.y
#define uAtmoThick gCloud.z
#define uStarGain gCloud.w
#define uScale gSunScreen.z
#define uGlass gSun2.w
#define uCityGain gStar2.w
#define uSunVis gCamRight.w
#define uSunVis2 gSun.w

// Light the ground gives off by day as well as by night (lava); Ground sets it.
static float3 gEmit;

float Hash(float3 p)
{
  p = frac(p * 0.3183099 + 0.1);
  p *= 17.0;
  return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float3 Hash3(float3 p)
{
  return float3(Hash(p), Hash(p + 19.19), Hash(p + 47.31));
}

float Noise(float3 x)
{
  float3 i = floor(x);
  float3 f = frac(x);
  f = f * f * (3.0 - 2.0 * f);
  return lerp(lerp(lerp(Hash(i), Hash(i + float3(1, 0, 0)), f.x),
                   lerp(Hash(i + float3(0, 1, 0)), Hash(i + float3(1, 1, 0)), f.x), f.y),
              lerp(lerp(Hash(i + float3(0, 0, 1)), Hash(i + float3(1, 0, 1)), f.x),
                   lerp(Hash(i + float3(0, 1, 1)), Hash(i + float3(1, 1, 1)), f.x), f.y), f.z);
}

float Fbm(float3 p)
{
  float a = 0.5;
  float s = 0.0;
  for (int i = 0; i < 5; i++) {
    s += a * Noise(p);
    p = p * 2.03 + 0.7;
    a *= 0.5;
  }
  return s;
}

float Fbm3(float3 p)
{
  float a = 0.5;
  float s = 0.0;
  for (int i = 0; i < 3; i++) {
    s += a * Noise(p);
    p = p * 2.07 + 1.3;
    a *= 0.5;
  }
  return s;
}

// Ridges: thin bright lines where the noise crosses its middle (lava cracks, creep veins).
float Ridge(float3 p)
{
  float n = Fbm3(p);
  return 1.0 - abs(n * 2.0 - 1.0);
}

// Craters: the nearest of a jittered grid of pits, a bowl and a raised rim.
float2 Crater(float3 p)
{
  float3 i = floor(p);
  float best = 9.0;
  float size = 0.0;
  for (int x = -1; x <= 1; x++)
    for (int y = -1; y <= 1; y++)
      for (int z = -1; z <= 1; z++) {
        float3 c = i + float3(x, y, z);
        float3 h = Hash3(c);
        float d = length(p - c - h) / (0.25 + 0.45 * h.x);
        if (d < best) {
          best = d;
          size = h.y;
        }
      }
  float bowl = 1.0 - smoothstep(0.0, 0.85, best);
  float rim = smoothstep(0.7, 0.92, best) * (1.0 - smoothstep(0.92, 1.15, best));
  return float2(bowl, rim) * step(0.45, size);
}

float Height(float3 n)
{
  n = n * uScale + gSeedV.xyz;
  float3 w = float3(Fbm3(n * 1.6 + 7.0), Fbm3(n * 1.6 + 2.0), Fbm3(n * 1.6 - 4.0)) - 0.5;
  return Fbm(n * 2.1 + w * 1.2 + 3.1) * 0.78 + Fbm3(n * 9.0 - 1.3) * 0.26;
}

float CityMask(float3 n)
{
  float near = gCityAt.w > 0.0 ? smoothstep(gCityAt.w, gCityAt.w * 0.25, distance(n, gCityAt.xyz)) : 1.0;
  return near * smoothstep(0.66 - uCity * 0.32, 0.8 - uCity * 0.26, Fbm3((n + gSeedV.xyz) * 6.0 + 21.0)) *
         min(1.0, uCity * 1.6);
}

// The ground's colour and what is on it; o is the point on the sphere, e its height. The
// night's light goes to glow.
float3 Ground(float3 o, float e, out float3 glow, out float wet)
{
  glow = 0.0;
  wet = 0.0;
  gEmit = 0.0;
  float3 n = o + gSeedV.xyz;
  float m = Fbm3(n * 3.0 + 11.0);
  float t = saturate((e - max(uSea, 0.2)) / (1.0 - max(uSea, 0.2)) * 1.6);
  float3 land = t < 0.33 ? lerp(gL0.rgb, gL1.rgb, t / 0.33)
              : t < 0.66 ? lerp(gL1.rgb, gL2.rgb, (t - 0.33) / 0.33)
                         : lerp(gL2.rgb, gL3.rgb, (t - 0.66) / 0.34);
  land = lerp(land, lerp(gA0.rgb, gA1.rgb, t), smoothstep(0.42, 0.62, m) * uMoist);
  land *= 0.88 + 0.24 * Fbm3(n * 40.0);
  // sand: long ridges, bent by the noise, on the flat ground
  float dune = sin(dot(n, float3(0.6, 0.2, 0.77)) * 110.0 + Fbm3(n * 5.0) * 16.0) *
               smoothstep(0.35, 0.65, Fbm3(n * 4.0 - 9.0));
  land *= 1.0 + uDunes * 0.12 * dune * (1.0 - t);
  if (uCraters > 0.0) {
    float2 c = Crater(n * 7.0);
    land *= 1.0 - c.x * 0.35 * uCraters;
    land *= 1.0 + c.y * 0.12 * uCraters;
  }
  float3 col = land;
  if (uSea >= 0.0 && e < uSea) {
    col = lerp(gDeep.rgb, gShallow.rgb, smoothstep(uSea - 0.16, uSea, e));
    wet = 1.0;
  }
  // the built ground, and its lights
  float cm = uCity > 0.0 && e >= uSea ? CityMask(o) : 0.0;
  if (cm > 0.0) {
    float grid = smoothstep(0.62, 0.86, Noise(n * 260.0)) + 0.6 * smoothstep(0.7, 0.9, Noise(n * 610.0));
    col = lerp(col, gCityDay.rgb * (0.8 + 0.4 * Noise(n * 90.0)), cm * 0.7);
    glow += gCityCol.rgb * grid * cm * 2.4 * uCityGain;
  }
  // scorched and glassed ground: dark plains, with streaks thrown out from where blasts fell
  if (uScar > 0.0 && e >= uSea) {
    float s = smoothstep(0.64 - uScar * 0.16, 0.72 - uScar * 0.12, Fbm3(n * 2.4 + 40.0)) * min(1.0, uScar * 1.3);
    float2 pit = Crater(n * 3.2 + 9.0);
    s = max(s, pit.x * uScar * 0.9);
    col = lerp(col, gScarCol.rgb * (0.7 + 0.6 * Noise(n * 70.0)), s * 0.85);
    wet = max(wet, s * uGlass);
  }
  // creep: the Swarm's living carpet, purple-brown by day, its veins lit by night
  if (uCreep > 0.0 && e >= uSea) {
    float k = smoothstep(0.5, 0.6, Fbm3(n * 2.2 + 70.0) + uCreep * 0.45 - 0.2) * min(1.0, uCreep * 1.6);
    float vein = pow(Ridge(n * 22.0 + 5.0), 6.0);
    col = lerp(col, gCreepCol.rgb * (0.75 + 0.5 * Noise(n * 55.0)) + gCreepGlow.rgb * vein * 0.15, k * 0.9);
    glow += gCreepGlow.rgb * vein * k * 1.2;
  }
  // lava: a net of cracks, dull red by day, burning by night
  if (uLava > 0.0) {
    float r = pow(Ridge(n * 4.0 + 2.0), 8.0) + 0.5 * pow(Ridge(n * 11.0 - 6.0), 12.0);
    r *= smoothstep(0.3, 0.55, Fbm3(n * 1.5 + 30.0)) * uLava;
    col = lerp(col, gLavaCol.rgb * 0.45, saturate(r));
    glow += gLavaCol.rgb * r * 3.0;
    gEmit += gLavaCol.rgb * r * 0.35;
  }
  // the caps
  float ice = smoothstep(1.0 - uIce, 1.04 - uIce, abs(o.y) + (m - 0.5) * 0.3 + (Fbm3(n * 5.0 - 3.0) - 0.5) * 0.3);
  col = lerp(col, gIceCol.rgb, ice * step(0.001, uIce));
  wet = max(wet, ice * 0.6);
  return col;
}

float Clouds(float3 n)
{
  float3 q = (n + gSeedV.xyz * 0.7) * 2.6;
  q.y *= lerp(1.0, 7.0, uBands);
  float w = Fbm3(q * 1.7 + 5.0);
  return smoothstep(0.62 - uCloud * 0.32, 0.86 - uCloud * 0.3, Fbm(q + w * 1.6));
}

// A moon: rock in its own colour and pits, or a gas giant in bands, lit by both suns.
float3 Moon(float3 ro, float3 rd, float4 m, float3 mc, float cr, float gas, out float tm)
{
  float3 oc = ro - m.xyz;
  float b = dot(oc, rd);
  float c = dot(oc, oc) - m.w * m.w;
  float h = b * b - c;
  tm = h > 0.0 ? -b - sqrt(h) : -1.0;
  if (tm <= 0.0)
    return 0.0;
  float3 n = normalize(oc + rd * tm);
  float2 c2 = Crater(n * 5.0 + m.xyz);
  float3 a = mc * (0.8 + 0.35 * Fbm3(n * 6.0)) * (1.0 - c2.x * 0.4 * cr) + c2.y * 0.08 * cr;
  if (gas > 0.5)
    a = mc * (0.62 + 0.55 * Fbm3(float3(n.x * 1.4, n.y * 16.0, n.z * 1.4) + Fbm3(n * 3.0) * 1.5));
  float l = max(dot(n, gSun.xyz), 0.0);
  float l2 = max(dot(n, gSun2.xyz), 0.0);
  return a * ((0.01 + l * 1.1) * gStar.rgb * uStarGain + l2 * gStar2.rgb);
}

// A star's glare: the disc, its halo and a wide bloom, smaller for a faint one.
float GlareOf(float sd, float g)
{
  return pow(sd, 4000.0) * 30.0 * g + pow(sd, 400.0) * 1.6 * g + pow(sd, 40.0) * 0.28 * g * g +
         pow(sd, 8.0) * 0.08 * g * g;
}

float3 Spin(float3 v)
{
  return gSpin0.xyz * v.x + gSpin1.xyz * v.y + gSpin2.xyz * v.z;
}

// The stars of a cell grid at sq (the view direction times the grid's density), one in a
// cell whose hash passes fThreshold. Each takes its own hashes: a brightness weighted to
// the faint (a few bright ones over many dim), a colour from orange to blue-white, mostly
// white, a size, and a shimmer of its own rate and phase, slow and shallow, which most
// barely show: seen from orbit, stars hardly twinkle. bBright: the sparse layer of bright
// stars, larger and with a soft halo.
float3 StarField(float3 sq, float fThreshold, float bBright)
{
  float3 id = floor(sq);
  if (Hash(id) < fThreshold)
    return 0.0;
  float3 h = Hash3(id + 7.7);
  float3 at = Hash3(id + 3.1) * 0.5 + 0.25;  // its place in the cell, off the centre
  float d = length(frac(sq) - at);
  float size = lerp(0.4, 0.55, h.x) * lerp(1.0, 0.6, bBright);
  float core = smoothstep(size, 0.0, d);
  float halo = bBright * exp(-d * d * 14.0) * 0.3;
  float bright = lerp(0.45, 1.25, h.x * h.x) * lerp(0.9, 1.5, bBright);
  float shimmer = 1.0 - h.y * h.y * 0.35 * (0.5 + 0.5 * sin(gRes.z * lerp(0.25, 1.1, h.z) + h.y * 6.2832 * 7.0));
  float3 tint = h.z < 0.18 ? float3(1.0, 0.8, 0.6) : h.z > 0.8 ? float3(0.72, 0.82, 1.0) : float3(0.92, 0.94, 1.0);
  return tint * (core + halo) * bright * shimmer;
}

float4 PSPlanet(VSOut i) : SV_Target
{
  // worlds.js works from the bottom left, as WebGL's gl_FragCoord does
  float2 frag = float2(i.pos.x, gRes.y - i.pos.y);
  float2 p = frag / gRes.xy * 2.0 - 1.0;
  p.x *= gRes.x / gRes.y;
  float3 rd = normalize(gCamFwd.xyz + (p.x * gCamRight.xyz + p.y * gCamUp.xyz) * gCamPos.w);
  float3 ro = gCamPos.xyz;
  float b = dot(ro, rd);
  float c = dot(ro, ro) - 1.0;
  float h = b * b - c;
  float tHit = h > 0.0 ? -b - sqrt(h) : -1.0;
  // the moons: the nearest one hit wins
  float tm0 = -1.0;
  float tm1 = -1.0;
  float tm2 = -1.0;
  float3 m0 = 0.0;
  float3 m1 = 0.0;
  float3 m2 = 0.0;
  if (gMoon0.w > 0.0)
    m0 = Moon(ro, rd, gMoon0, gMoonCol0.rgb, gMoonCol0.w, gMoonGas.x, tm0);
  if (gMoon1.w > 0.0)
    m1 = Moon(ro, rd, gMoon1, gMoonCol1.rgb, gMoonCol1.w, gMoonGas.y, tm1);
  if (gMoon2.w > 0.0)
    m2 = Moon(ro, rd, gMoon2, gMoonCol2.rgb, gMoonCol2.w, gMoonGas.z, tm2);
  float tMoon = 1e9;
  float3 mCol = 0.0;
  if (tm0 > 0.0 && tm0 < tMoon) {
    tMoon = tm0;
    mCol = m0;
  }
  if (tm1 > 0.0 && tm1 < tMoon) {
    tMoon = tm1;
    mCol = m1;
  }
  if (tm2 > 0.0 && tm2 < tMoon) {
    tMoon = tm2;
    mCol = m2;
  }
  float3 col = 0.0;
  float3 sun = gSun.xyz;
  float3 sun2 = gSun2.xyz;
  float3 star2 = gStar2.rgb;
  float sd = max(dot(rd, sun), 0.0);
  float sd2 = max(dot(rd, sun2), 0.0);
  float3 sunC = gStar.rgb * uStarGain;
  float two = step(0.001, dot(star2, 1.0));
  if (tHit > 0.0 && tHit < tMoon) {
    float3 n = normalize(ro + rd * tHit);
    float3 ln = Spin(n);
    float ndl = dot(n, sun);
    float ndl2 = dot(n, sun2);
    float e = Height(ln);
    // relief: the ground is shaded by its slope toward the sun
    float3 sunL = Spin(sun);
    float e2 = Height(normalize(ln + (sunL - ln * dot(sunL, ln)) * 0.012));
    float slope = clamp((e2 - e) * 9.0 * uRelief, -0.6, 0.6) * step(uSea, e);
    float lit = smoothstep(-0.1, 0.4, ndl) * clamp(1.0 + slope, 0.3, 1.6);
    float lit2 = smoothstep(-0.1, 0.4, ndl2) * two;
    float3 glow;
    float wet;
    float3 alb = Ground(ln, e, glow, wet);
    float3 drift = float3(gCamFwd.w * 6.0, 0.0, 0.0);
    float cl = uCloud > 0.0 ? Clouds(ln + drift) : 0.0;
    float sh = uCloud > 0.0 ? Clouds(ln + drift + sunL * 0.02) : 0.0;
    // the light on the ground: the sun, and the second sun where there is one
    float3 light = (0.012 + lit * 1.05 * (1.0 - 0.5 * sh)) * sunC + lit2 * (1.0 - 0.5 * sh) * star2;
    float3 surf = alb * light;
    float3 hv = normalize(sun - rd);
    surf += sunC * pow(max(dot(n, hv), 0.0), 80.0) * lit * (1.0 - cl) * wet * 0.6;
    float dark = 1.0 - smoothstep(-0.28, 0.08, max(ndl, ndl2 * two));
    surf += glow * (0.04 + 0.96 * dark) * (1.0 - 0.75 * cl) + gEmit * (1.0 - dark) * (1.0 - 0.6 * cl);
    float3 lightA = (0.02 + lit) * sunC + lit2 * star2;
    surf = lerp(surf, gHaze.rgb * lightA, gHaze.a * (0.4 + 0.6 * pow(1.0 - max(dot(n, -rd), 0.0), 1.5)));
    surf = lerp(surf, gCloudCol.rgb * (0.02 * sunC + lit * 1.1 * sunC + lit2 * star2), saturate(cl * 0.92));
    float term = exp(-ndl * ndl * 30.0);
    surf += gAtmo.rgb * float3(1.0, 0.6, 0.4) * term * 0.08 * (1.0 - 0.4 * cl);
    float mu = max(dot(n, -rd), 0.0);
    float rim = pow(1.0 - mu, 2.6);
    surf = lerp(surf, gAtmo.rgb * 1.2 * sunC, rim * smoothstep(-0.35, 0.5, ndl) * 0.75 * uAtmoThick);
    surf += gAtmo.rgb * rim * smoothstep(-0.35, 0.5, ndl2) * 0.6 * uAtmoThick * star2;
    surf += gAtmo.rgb * pow(1.0 - mu, 6.0) * (pow(sd, 3.0) * sunC + pow(sd2, 3.0) * star2) * 1.8 * uAtmoThick;
    col = surf;
  } else {
    // the sky: nebula, stars, the moons in front of them, the air beyond the edge over all
    float nb = Fbm(rd * 2.2 + 13.0);
    float nb2 = Fbm(rd * 3.7 - 8.0);
    col = gNebA.rgb * smoothstep(0.42, 0.8, nb) * 1.3 + gNebB.rgb * smoothstep(0.45, 0.82, nb2) * 1.2;
    col += StarField(rd * 420.0, 0.9972, 0.0) + StarField(rd * 150.0, 0.9988, 1.0);
    if (tMoon < 1e8)
      col = mCol;
    float tc = max(-b, 0.0);
    float3 pc = ro + rd * tc;
    float r = length(pc);
    float hgt = max(r - 1.0, 0.0);
    float dens = (exp(-hgt / 0.028) + 0.25 * exp(-hgt / 0.12)) * uAtmoThick;
    float3 up = pc / max(r, 1e-4);
    float litA = smoothstep(-0.4, 0.55, dot(up, sun));
    float litA2 = smoothstep(-0.4, 0.55, dot(up, sun2));
    float fwd = pow(sd, 5.0) * 2.8 + pow(sd, 40.0) * 4.0;
    float fwd2 = pow(sd2, 5.0) * 2.8 + pow(sd2, 40.0) * 4.0;
    col += gAtmo.rgb * dens * ((litA * 1.1 + fwd) * sunC + (litA2 * 1.1 + fwd2) * star2) * step(0.0, -b + 0.5);
  }
  // the stars' glare in their own colours, and a streak through the first
  float g = uStarGain;
  col += gStar.rgb * GlareOf(sd, g) * uSunVis * (tHit > 0.0 ? 0.35 : 1.0);
  col += star2 * GlareOf(sd2, 0.6) * uSunVis2 * (tHit > 0.0 ? 0.35 : 1.0);
  float2 dp = frag - gSunScreen.xy;
  col += gStar.rgb * float3(1.0, 0.8, 0.6) * exp(-abs(dp.y) / (gRes.y * 0.0035)) *
         exp(-abs(dp.x) / (gRes.x * 0.3)) * 0.55 * uSunVis * g * g;
  col = 1.0 - exp(-col * 1.25);
  col *= gCamUp.w * gRes.w;
  return float4(col, 1.0);
}
