// hudshape.hlsl: a HUD file's shape layers, for the GPU presenter
// (port_gpu_hud.cpp). Written by engine/tools/hud/export_hud_shader.mjs from the design
// center's hudshape.js, whose BODY below is the one copy of the maths the design center's mock
// runs too; change it there and run the script again. Only the head is the script's: SDL_GPU's
// registers (a fragment shader's textures in space 2, its uniforms in space 3), the uniform
// block padded so that Direct3D and SPIR-V lay it out alike, and the entry points.
#define CONST static const
#define SAMPLE_BACK(uv) uBack.Sample(uBackSampler, uv)
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> uBack : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState uBackSampler : register(s0, space2);
[[vk::binding(0, 3)]] cbuffer HudShapeParams : register(b0, space3) {
  float2 uHalf;   // half the box, units
  float uPx;   // units per pixel
  float uMargin;   // units drawn round the box for light outside it
  float2 uOrigin;   // the box's top left on the screen, units
  float2 uScreen;   // the screen, units
  float uTime;   // seconds
  float _pad0;
  float _pad1;
  float _pad2;
  float4 uCorner;   // corner sizes: top left, top right, bottom right, bottom left
  float4 uKind;   // corner kinds: 0 round, 1 chamfer, 2 notch
  float4 uCut[4];   // cut-outs: centre from the box centre, half size
  float4 uCutR;   // cut-out radii
  float uCutN;   // cut-outs used
  float _pad3;
  float _pad4;
  float _pad5;
  float4 uFill[4];   // fill stops, straight alpha
  float uFillN;   // stops used
  float _pad6;
  float2 uFillDir;   // the gradient runs this way
  float uGlass;
  float _pad7;
  float _pad8;
  float _pad9;
  float4 uGlassTint;
  float uGlassNoise;
  float uGlassStreak;
  float uBevel;
  float uBevelStrength;
  float2 uLightFrom;   // unit vector toward the light
  float _pad10;
  float _pad11;
  float4 uHighlight;
  float uInnerShadow;
  float uInnerShadowStrength;
  float uRim;
  float uRimWidth;
  float4 uRimColor;
  float4 uInnerGlow;
  float uInnerGlowSize;
  float uLines;
  float uLineSpacing;
  float uHolo;
  float4 uStroke;
  float uLine;
  float uEnergy;
  float _pad12;
  float _pad13;
  float4 uEnergyColor;
  float uEnergyWidth;
  float _pad14;
  float _pad15;
  float _pad16;
  float4 uOuterGlow;
  float uOuterGlowSize;
  float uSurf;   // surface: an index into SURFACES, 0 none
  float _pad17;
  float _pad18;
  float4 uSurfA;   // surface colours
  float4 uSurfB;
  float4 uSurfC;
  float uSurfScale;   // pattern size
  float _pad19;
  float _pad20;
  float _pad21;
  float4 uSurfP;   // gloss, wear, detail, speed
  float uMirror;   // 1: patterns mirror about the centre
  float _pad22;
  float _pad23;
  float _pad24;
  float4 uWobble;   // amplitude, scale, speed
  float4 uSpines;   // height, spacing, width, lean
  float uBow;   // units the top arcs up
  float uBlend;   // units unions and cuts blend over
  float2 uSegments;   // count, gap
  float4 uSeams;   // spacing, width, strength, angle
  float4 uBolts;   // radius, inset, spacing, head (0 dome, 1 hex, 2 socket)
  float4 uBoltColor;
  float4 uInlay;   // width, depth, glow
  float4 uInlayColor;
  float uReveal;   // 0 hidden, 1 shown
  float uRevealKind;   // 0 peel, 1 warp
  float _pad25;
  float _pad26;
  float4 uRevealColor;
  float4 uPath[96];   // a path outline: edges (a.x, a.y, b.x, b.y) from the box centre, units
  float uPathN;   // edges used; 0 draws the box
  float _pad27;
  float _pad28;
  float _pad29;
  float4 uProfile;   // kind (0 slope, 1 step, 2 double, 3 lip, 4 groove, 5 chamfer), steps, groove depth, groove width
  float4 uPanel;   // panel lines: count, inset, gap, width
  float4 uPattern;   // kind (0 none, 1 honeycomb, 2 slats, 3 perforated), cell, depth, inset
  float4 uPattern2;   // angle, wall
  float4 uScratch;   // amount, length, angle, wander
  float4 uShadow;   // contact shadow: strength, height, softness
  float4 uRelief;   // relief (how hard the light models the surface), crest lines
  float4 uWear;   // worn edges: amount, depth in units
  float4 uWearColor;   // the bare metal
  float4 uSheen;   // a studio's reflection on the surface: strength
  float4 uSheenColor;
};

CONST float PI2 = 6.2831853;

// a square that is safe for negative numbers (pow is not, on every GPU)
float sq(float x) { return x * x; }

float hash21(float2 p) {
  p = frac(p * float2(123.34, 456.21));
  p += dot(p, p + float2(45.32, 45.32));
  return frac(p.x * p.y);
}
float2 hash22(float2 p) {
  float n = hash21(p);
  return float2(n, hash21(p + float2(n * 13.7 + 7.1, 3.3)));
}

// value noise, 0 to 1, and four octaves of it (0 to about 0.94)
float vnoise(float2 p) {
  float2 i = floor(p);
  float2 f = frac(p);
  float2 u = f * f * (3.0 - 2.0 * f);
  float a = hash21(i);
  float b = hash21(i + float2(1.0, 0.0));
  float c = hash21(i + float2(0.0, 1.0));
  float d = hash21(i + float2(1.0, 1.0));
  return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
float fbm(float2 p) {
  float v = 0.0;
  float a = 0.5;
  for (int i = 0; i < 4; i++) {
    v += a * vnoise(p);
    p = p * 2.03 + float2(17.1, 9.2);
    a *= 0.5;
  }
  return v;
}

// Voronoi cells: x the distance to the nearest cell point, y to the second nearest, zw the
// nearest point itself (in the same units as p)
float4 voronoi(float2 p) {
  float2 i = floor(p);
  float2 f = frac(p);
  float f1 = 8.0;
  float f2 = 8.0;
  float2 id = float2(0.0, 0.0);
  for (int y = -1; y <= 1; y++) {
    for (int x = -1; x <= 1; x++) {
      float2 g = float2(float(x), float(y));
      float2 r = g + hash22(i + g) - f;
      float dd = dot(r, r);
      if (dd < f1) { f2 = f1; f1 = dd; id = i + g + hash22(i + g); }
      else if (dd < f2) { f2 = dd; }
    }
  }
  return float4(sqrt(f1), sqrt(f2), id);
}

// ---------------------------------------------------------------------------
// the outline

// a box of half size b, square corners
float sdBox0(float2 p, float2 b) {
  float2 q = abs(p) - b;
  return min(max(q.x, q.y), 0.0) + length(max(q, float2(0.0, 0.0)));
}

float sdRoundBox(float2 p, float2 b, float r) {
  return sdBox0(p, b - float2(r, r)) - r;
}

// an isosceles triangle, its apex at the origin and its base q.y further down, q.x half wide
float sdTriIso(float2 p, float2 q) {
  p.x = abs(p.x);
  float2 a = p - q * saturate(dot(p, q) / dot(q, q));
  float2 b = p - q * float2(saturate(p.x / q.x), 1.0);
  float s = -sign(q.y);
  float2 d = min(float2(dot(a, a), s * (p.x * q.y - p.y * q.x)), float2(dot(b, b), s * (p.y - q.y)));
  return -sqrt(d.x) * sign(d.y);
}

// a smooth union and difference, blended over k units; k 0 is the hard one
float smin(float a, float b, float k) {
  if (k <= 0.0) return min(a, b);
  float h = saturate(0.5 + 0.5 * (b - a) / k);
  return lerp(b, a, h) - k * h * (1.0 - h);
}
float smax(float a, float b, float k) { return -smin(-a, -b, k); }

// a value per corner (top left, top right, bottom right, bottom left; y points down)
float pick(float2 p, float4 v) {
  return p.x < 0.0 ? (p.y < 0.0 ? v.x : v.w) : (p.y < 0.0 ? v.y : v.z);
}

// spines along the top edge: one per uSpines.y units, each up to uSpines.x tall, uSpines.z of
// its cell wide, leaning away from the middle by uSpines.w of its height
float sdSpines(float2 p) {
  float sp = max(uSpines.y, 4.0);
  float best = 100000.0;
  float c0 = floor(p.x / sp);
  for (int k = -1; k <= 1; k++) {
    float c = c0 + float(k);
    float cx = (c + 0.5) * sp;
    if (abs(cx) < uHalf.x - sp * 0.5) {
      float r = hash21(float2(abs(c + 0.5) * 1.7, 3.7));
      float h = uSpines.x * (0.55 + 0.45 * r);
      float qy = h + 6.0;
      float lean = uSpines.w * h * sign(cx) * (0.6 + 0.4 * r);
      float2 pl = p - float2(cx + lean, -uHalf.y - h);
      pl.x += lean * pl.y / qy;
      best = min(best, sdTriIso(pl, float2(sp * uSpines.z * 0.5, qy)));
    }
  }
  return best;
}

// a path outline: uPathN straight edges (a, b) from the box centre, the curves and arcs already
// cut into short runs. The distance is to the nearest edge; inside is even-odd, so a second
// loop inside the first is a hole.
float sdPath(float2 p) {
  float dd = 100000000.0;
  float s = 1.0;
  for (int i = 0; i < 96; i++) {
    if (float(i) >= uPathN) break;
    float2 a = uPath[i].xy;
    float2 e = uPath[i].zw - a;
    float2 w = p - a;
    float2 q = w - e * saturate(dot(w, e) / max(dot(e, e), 0.000001));
    dd = min(dd, dot(q, q));
    bool c1 = p.y >= a.y;
    bool c2 = p.y < uPath[i].w;
    bool c3 = e.x * w.y > e.y * w.x;
    if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) s = -s;
  }
  return s * sqrt(dd);
}

// how far round a path p is, along its nearest edge, from the path's first point
float pathRunAt(float2 p) {
  float best = 100000000.0;
  float run = 0.0;
  float acc = 0.0;
  for (int i = 0; i < 96; i++) {
    if (float(i) >= uPathN) break;
    float2 a = uPath[i].xy;
    float2 e = uPath[i].zw - a;
    float len = length(e);
    float t = saturate(dot(p - a, e) / max(dot(e, e), 0.000001));
    float2 q = p - a - e * t;
    float dd = dot(q, q);
    if (dd < best) { best = dd; run = acc + t * len; }
    acc += len;
  }
  return run;
}
float pathLength() {
  float acc = 0.0;
  for (int i = 0; i < 96; i++) {
    if (float(i) >= uPathN) break;
    acc += length(uPath[i].zw - uPath[i].xy);
  }
  return acc;
}

// The shape: the box with its corners (0 round, 1 chamfer, 2 notch), an arc over its top,
// spines, the cut-outs taken away, gaps that split it into floating segments, and last a slow
// wobble of the whole edge.
float sdShape(float2 p) {
  float c = pick(p, uCorner);
  float k = pick(p, uKind);
  float2 a = abs(p);
  float d;
  if (uPathN > 0.5) d = sdPath(p);
  else if (k < 0.5) d = sdRoundBox(p, uHalf, min(c, min(uHalf.x, uHalf.y)));
  else if (k < 1.5) d = max(sdBox0(p, uHalf), (a.x + a.y - (uHalf.x + uHalf.y - c)) * 0.70710678);
  else d = max(sdBox0(p, uHalf), c - length(a - uHalf));
  if (uBow > 0.0) {
    // the arc spans the top edge between its corners, so it meets them and never overhangs
    float hc = max(uHalf.x - max(uCorner.x, uCorner.y), 1.0);
    float R = (hc * hc + uBow * uBow) / (2.0 * uBow);
    float cap = max(length(p - float2(0.0, -uHalf.y + R - uBow)) - R, p.y + uHalf.y - 1.0);
    cap = max(cap, abs(p.x) - hc);
    d = smin(d, cap, uBlend);
  }
  if (uSpines.x > 0.0) d = smin(d, sdSpines(p), uBlend);
  for (int i = 0; i < 4; i++) {
    if (float(i) < uCutN) d = smax(d, -sdRoundBox(p - uCut[i].xy, uCut[i].zw, uCutR[i]), uBlend);
  }
  if (uSegments.x > 1.5) {
    float cell = 2.0 * uHalf.x / uSegments.x;
    float t = (p.x + uHalf.x) / cell;
    float j = floor(t + 0.5);
    if (j > 0.5 && j < uSegments.x - 0.5) d = smax(d, uSegments.y * 0.5 - abs(t - j) * cell, uBlend * 0.5);
  }
  if (uWobble.x > 0.0) {
    float2 drift = float2(uTime * uWobble.z, -uTime * uWobble.z * 0.7);
    d += uWobble.x * (fbm(p / max(uWobble.y, 1.0) + drift) - 0.47) * 2.0;
  }
  return d;
}

// how far round the box p is, clockwise from the top left corner, measured along the edge
// nearest it; the energy edge flows along this, with a whole number of waves so it has no seam
float perimeterAt(float2 p) {
  float2 h = uHalf;
  float r = p.y < 0.0 ? h.x + p.x : 3.0 * h.x + 2.0 * h.y - p.x;
  if (abs(p.x) - h.x > abs(p.y) - h.y)
    r = p.x > 0.0 ? 2.0 * h.x + h.y + p.y : 4.0 * h.x + 3.0 * h.y - p.y;
  if (uPathN > 0.5) r = pathRunAt(p);
  return r;
}

// the outward direction of the edge nearest p
float2 sdNormal(float2 p) {
  float e = 0.5;
  float2 g = float2(sdShape(p + float2(e, 0.0)) - sdShape(p - float2(e, 0.0)),
                    sdShape(p + float2(0.0, e)) - sdShape(p - float2(0.0, e)));
  return g / max(length(g), 0.0001);
}

// the fill gradient: up to four evenly spaced stops
float4 fillAt(float t) {
  float x = saturate(t) * max(uFillN - 1.0, 1.0);
  float4 c = uFill[0];
  for (int i = 1; i < 4; i++) {
    if (float(i) < uFillN) c = lerp(c, uFill[i], saturate(x - float(i - 1)));
  }
  return c;
}

// ---------------------------------------------------------------------------
// the edge's profile, the patterns, the scratches

// 1 between a and b, its edges aa soft (a pixel, in x's own units)
float band(float x, float a, float b, float aa) {
  return smoothstep(a - aa * 0.5, a + aa * 0.5, x) - smoothstep(b - aa * 0.5, b + aa * 0.5, x);
}

// The edge's profile at a depth in from the edge: x how steeply the surface rises going in (the
// normal leans out by it; negative falls back), y how much of the ambient light a crease or
// channel shuts out, z how near a crest (where a slope meets a flat, the edge that catches a
// line of light). uProfile: kind (0 slope, 1 step, 2 double, 3 lip, 4 groove, 5 chamfer), steps, where
// the groove is, its width. Panel lines are grooves too, uPanel: count, inset, gap, width.
float crestAt(float depth, float at) {
  return exp(-abs(depth - at) / max(uRimWidth, uPx));
}
float3 edgeProfile(float depth) {
  float w = max(uBevel, 0.001);
  float k = uProfile.x;
  float s = 0.0;
  float occ = 0.0;
  float cr = 0.0;
  if (uBevel > 0.0) {
    if (k > 4.5) {
      // a chamfer: one flat face at 45 degrees, a crest where it meets the top
      s = band(depth, -1.0, w, uPx) * 1.2;
      cr = crestAt(depth, w);
    } else if (k < 0.5 || k > 3.5) {
      s = 1.0 - smoothstep(0.0, w, depth);
      cr = crestAt(depth, w);
    } else if (k < 1.5) {
      // terraces: each a riser (its outer 45%) and a flat, the crease at the riser's foot dark
      float n = max(uProfile.y, 1.0);
      float cell = w / n;
      float f = depth / cell;
      float fr = frac(f);
      float inside = 1.0 - smoothstep(n - uPx / cell, n, f);
      s = (1.0 - smoothstep(0.45 - uPx / cell, 0.45, fr)) * 1.5 * inside;
      occ = smoothstep(0.75, 1.0, fr) * 0.35 * inside;
      cr = exp(-abs(fr - 0.45) * cell / max(uRimWidth, uPx)) * step(f, n);
    } else if (k < 2.5) {
      // a steep outer chamfer, then a shallow one
      s = band(depth, -1.0, w * 0.35, uPx) * 1.8 + band(depth, w * 0.35, w, uPx) * 0.45;
      cr = max(crestAt(depth, w * 0.35), crestAt(depth, w) * 0.7);
    } else {
      // a raised lip: up, then down into what it rims, the foot of the fall in shadow
      s = band(depth, -1.0, w * 0.45, uPx) * 1.3 - band(depth, w * 0.45, w, uPx) * 1.0;
      occ = band(depth, w * 0.75, w * 1.15, uPx) * 0.35;
      cr = crestAt(depth, w * 0.45);
    }
  }
  // a groove: down and up again, its floor in shadow (kind 4 at uProfile.z, and the panel lines)
  float gw = max(uProfile.w, 0.5);
  if (k > 3.5 && k < 4.5) {
    float t = (depth - uProfile.z) / gw;
    float ga = uPx / gw;
    s += band(t, 0.0, 0.5, ga) * -1.4 + band(t, 0.5, 1.0, ga) * 1.4;
    occ += band(t, 0.0, 1.0, ga) * 0.45 * (1.0 - abs(2.0 * t - 1.0));
    cr = max(cr, max(crestAt(depth, uProfile.z), crestAt(depth, uProfile.z + gw)) * 0.8);
  }
  if (uPanel.x > 0.0) {
    float lw = max(uPanel.w, 0.3);
    for (int i = 0; i < 4; i++) {
      if (float(i) >= uPanel.x) break;
      float t = (depth - (uPanel.y + float(i) * (uPanel.z + lw))) / lw;
      float la = uPx / lw;
      s += band(t, 0.0, 0.5, la) * -1.6 + band(t, 0.5, 1.0, la) * 1.6;
      occ += band(t, 0.0, 1.0, la) * 0.5;
      cr = max(cr, crestAt(depth, uPanel.y + float(i) * (uPanel.z + lw) + lw) * 0.8);
    }
  }
  return float3(s, saturate(occ), cr);
}

// the distance from the centre of a hexagon, 0.5 at its flats (cells 1 across, flat top)
float hexDist(float2 v) {
  v = abs(v);
  return max(dot(v, float2(0.8660254, 0.5)), v.y);
}

// The pattern at a point: xy a lean of the normal, z what the light that reaches it is
// multiplied by, w how much of it is open (a hole, a gap). uPattern: kind (1 honeycomb, 2 slats,
// 3 perforated), cell size, depth, inset from the edge; uPattern2: angle, wall.
float4 patternAt(float2 p, float depth) {
  float k = uPattern.x;
  float4 o = float4(0.0, 0.0, 1.0, 0.0);
  if (k < 0.5 || depth < uPattern.w) return o;
  float fade = smoothstep(uPattern.w, uPattern.w + uPx, depth);
  float sc = max(uPattern.y, 1.0);
  float an = uPattern2.x * 0.0174533;
  float2 r = float2(p.x * cos(an) + p.y * sin(an), -p.x * sin(an) + p.y * cos(an));
  float wall = clamp(uPattern2.y, 0.05, 0.9);
  float aa = uPx / sc;
  if (k < 1.5) {
    // honeycomb: hexagonal holes, the walls between them rounded ridges, the far side of each
    // hole's wall catching the light
    float2 u = r / sc;
    float2 g = float2(1.7320508, 1.0);
    float2 h = g * 0.5;
    float2 a = u - g * floor(u / g + 0.5);
    float2 b = u - h - g * floor((u - h) / g + 0.5);
    float2 v = dot(a, a) < dot(b, b) ? a : b;
    float hd = hexDist(v);
    float ro = 0.5 - wall * 0.5;
    float hole = 1.0 - smoothstep(ro - aa, ro, hd);
    float2 dir = v / max(length(v), 0.0001);
    float across = (hd - 0.5) / (wall * 0.5);
    o.xy = dir * across * 0.9 * (1.0 - hole);
    float inner = smoothstep(ro - 0.12, ro, hd) * hole;
    o.xy -= dir * inner * 1.2;
    o.z = lerp(1.0, lerp(0.08, 0.5, inner), hole * uPattern.z);
    o.w = hole;
  } else if (k < 2.5) {
    // slats: bars across, each rounded, the gaps between them dark
    float f = frac(r.y / sc);
    float bar = 1.0 - wall;
    float gap = smoothstep(bar - aa, bar, f) * smoothstep(0.0, aa, 1.0 - f);
    float t = f / bar;
    float lean = (t - 0.5) * 1.6 * (1.0 - gap);
    o.xy = float2(-sin(an), cos(an)) * lean;
    o.z = lerp(1.0, 0.07, gap * uPattern.z) * lerp(1.0, 0.75, smoothstep(0.75, 1.0, t) * (1.0 - gap));
    o.w = gap;
  } else {
    // perforated: round holes on a square grid
    float2 u = r / sc;
    float2 c = u - floor(u) - 0.5;
    float rr = 0.5 - wall * 0.5;
    float hole = 1.0 - smoothstep(rr - aa, rr, length(c));
    float2 dir = c / max(length(c), 0.0001);
    o.xy = -dir * smoothstep(rr - 0.1, rr, length(c)) * hole * 1.2;
    o.z = lerp(1.0, 0.1, hole * uPattern.z);
    o.w = hole;
  }
  o.xy *= fade;
  o.z = lerp(1.0, o.z, fade);
  o.w *= fade;
  return o;
}

// scratches: short thin lines at random places and angles, bright where they cut through to
// bare metal, uScratch: amount, length (units), the main direction (degrees), how far they wander
float scratchAt(float2 p) {
  float sc = max(uScratch.y, 4.0);
  float2 u = p / sc;
  float2 i0 = floor(u);
  float best = 0.0;
  for (int y = -1; y <= 1; y++) {
    for (int x = -1; x <= 1; x++) {
      float2 c = i0 + float2(float(x), float(y));
      float hsh = hash21(c + float2(7.7, 1.3));
      if (hsh > 0.55) continue;
      float2 o = c + hash22(c);
      float an = (uScratch.z + (hash21(c * 1.7 + float2(3.0, 9.0)) - 0.5) * 2.0 * uScratch.w) * 0.0174533;
      float2 dir = float2(cos(an), sin(an));
      float len = 0.35 + 0.6 * hash21(c + float2(2.1, 5.5));
      float2 q = u - o;
      float t = clamp(dot(q, dir), -len, len);
      float dist = length(q - dir * t) * sc;
      float wid = 0.25 + 0.35 * hsh;
      float fade = 1.0 - abs(t) / len;
      best = max(best, (1.0 - smoothstep(wid * 0.5, wid * 0.5 + uPx, dist)) * fade * (0.5 + hsh));
    }
  }
  return saturate(best) * uScratch.x;
}

// ---------------------------------------------------------------------------
// surfaces: what the material is at a point, before the light

struct Surf {
  float3 albedo;
  float2 tilt;     // the surface normal's lean, x and y
  float gloss;
  float metal;     // 1 tints the highlight with the albedo
  float3 emit;     // light of its own, not shaded
  float alpha;
  float bend;      // units the glass behind is displaced by the tilt
};

// p from the box centre, b01 across the box 0 to 1, depth units in from the edge
Surf surfaceAt(float2 p, float2 b01, float depth) {
  Surf s;
  s.albedo = uSurfA.rgb;
  s.tilt = float2(0.0, 0.0);
  s.gloss = uSurfP.x;
  s.metal = 0.0;
  s.emit = float3(0.0, 0.0, 0.0);
  s.alpha = 1.0;
  s.bend = 0.0;
  float k = uSurf;
  float det = uSurfP.z;
  float t = uTime * uSurfP.w;
  float2 m = uMirror > 0.5 ? float2(abs(p.x), p.y) : p;
  float2 u = m / max(uSurfScale, 0.05);
  if (k < 0.5) {
    s.albedo = uSurfA.rgb;
  } else if (k < 1.5) {
    // brushed steel: fine streaks along x, the highlight smeared across them
    float g = vnoise(float2(u.x * 0.04, u.y * 1.6)) * 0.6 + vnoise(float2(u.x * 0.15, u.y * 5.0)) * 0.4;
    s.albedo = lerp(uSurfB.rgb, uSurfA.rgb, 0.55 + 0.45 * g);
    s.tilt = float2(0.0, (g - 0.5) * 0.35 * det);
    s.metal = 1.0;
  } else if (k < 2.5) {
    // gunmetal: dark, mottled, a fine speckle
    float g = fbm(u * 0.08);
    float sp = hash21(floor(u * 1.5));
    s.albedo = lerp(uSurfB.rgb, uSurfA.rgb, g) * (0.92 + 0.16 * sp * det);
    s.metal = 1.0;
  } else if (k < 4.5) {
    // painted plate (3) or hazard stripes (4): paint over bare metal, worn through at the
    // edges and in patches, with scratches
    float3 paint = uSurfA.rgb;
    if (k > 3.5) paint = frac((u.x + u.y) / 28.0) < 0.5 ? uSurfA.rgb : uSurfC.rgb;
    float w = fbm(u * 0.06) + (1.0 - saturate(depth / 14.0)) * 0.55;
    float worn = smoothstep(1.02 - uSurfP.y * 0.6, 1.08 - uSurfP.y * 0.6, w);
    float scratch = step(0.985, vnoise(float2(u.x * 0.6, u.y * 0.05))) * uSurfP.y;
    worn = max(worn, scratch);
    s.albedo = lerp(paint * (0.9 + 0.2 * fbm(u * 0.3) * det), uSurfB.rgb, worn);
    s.gloss = lerp(uSurfP.x * 0.4, uSurfP.x, worn);
    s.metal = worn;
  } else if (k < 5.5) {
    // chrome: a sky and a ground reflected, bent by the edge
    float y = b01.y * 0.9 - 0.12 * sin(b01.x * 5.0);
    float horizon = smoothstep(0.42, 0.5, y);
    s.albedo = lerp(uSurfA.rgb, uSurfB.rgb, horizon) + float3(1.0, 1.0, 1.0) * exp(-sq((y - 0.5) / 0.015)) * 0.6;
    s.metal = 1.0;
  } else if (k < 6.5) {
    // polished trim (gold, bronze): a soft band of reflection across it
    float band = 0.5 + 0.5 * sin(b01.y * PI2 * 0.8 + b01.x * 2.0);
    s.albedo = lerp(uSurfB.rgb, uSurfA.rgb, band) * (0.95 + 0.1 * vnoise(u * 0.5) * det);
    s.metal = 1.0;
  } else if (k < 7.5) {
    // carbon fibre: a twill of fibres crossing every 6 units
    float2 c = u / 6.0;
    float2 ci = floor(c);
    float2 cf = frac(c);
    float along = frac((ci.x + ci.y) * 0.5) > 0.25 ? cf.x : cf.y;
    float weave = 0.5 + 0.5 * sin(along * PI2 * 3.0);
    s.albedo = lerp(uSurfB.rgb, uSurfA.rgb, 0.35 + 0.4 * weave * det);
    s.tilt = frac((ci.x + ci.y) * 0.5) > 0.25 ? float2((cf.x - 0.5) * 0.6, 0.0) : float2(0.0, (cf.y - 0.5) * 0.6);
    s.tilt *= det;
  } else if (k < 8.5) {
    // matte polymer
    s.albedo = uSurfA.rgb * (0.95 + 0.1 * vnoise(u * 0.8) * det);
  } else if (k < 9.5) {
    // chitin: Voronoi plates, each domed, glossy at the rim, dark in the seams between
    float4 v = voronoi(u / 22.0);
    float edge = v.y - v.x;
    float h = hash21(floor(v.zw * 9.0));
    float rim = smoothstep(0.03, 0.09, edge) * (1.0 - smoothstep(0.09, 0.3, edge));
    s.albedo = lerp(uSurfA.rgb * (0.8 + 0.4 * h), uSurfB.rgb, rim * 0.6);
    s.albedo = lerp(uSurfC.rgb, s.albedo, smoothstep(0.0, 0.04, edge));
    s.tilt = (u / 22.0 - v.zw) * 1.4 * det;
    s.gloss = uSurfP.x * (0.5 + rim);
  } else if (k < 10.5) {
    // wet membrane: soft folds that drift, very glossy, a little see-through
    float g = fbm(u * 0.05 + float2(t * 0.05, 0.0));
    float g2 = fbm(u * 0.12 - float2(0.0, t * 0.03));
    s.albedo = lerp(uSurfB.rgb, uSurfA.rgb, g);
    s.tilt = float2(g - g2, g2 - 0.5) * 1.2 * det;
    s.alpha = 0.78 + 0.2 * g;
    s.bend = 10.0;
  } else if (k < 11.5) {
    // flesh with veins that swell with a slow pulse
    float g = fbm(u * 0.04);
    float r = abs(fbm(u * 0.025 + float2(3.1, 7.7)) - 0.47);
    float vein = 1.0 - smoothstep(0.0, 0.035, r);
    float pulse = 0.5 + 0.5 * sin(t * 2.4);
    s.albedo = lerp(uSurfA.rgb * (0.8 + 0.4 * g), uSurfB.rgb, vein * (0.7 + 0.3 * pulse)) * (0.92 + 0.08 * pulse);
    s.emit = uSurfC.rgb * uSurfC.a * vein * pulse * 0.4 * det;
    s.tilt = float2(g - 0.5, 0.5 - g) * 0.6 * det;
  } else if (k < 12.5) {
    // bioluminescent sacs: round cells that glow, each on its own beat
    float4 v = voronoi(u / 30.0);
    float ph = hash21(floor(v.zw * 7.0));
    float rad = 0.28 + 0.12 * ph;
    float sac = 1.0 - smoothstep(rad - 0.06, rad, v.x);
    float pulse = 0.55 + 0.45 * sin(t * 1.7 + ph * PI2);
    float3 flesh = lerp(uSurfA.rgb, uSurfB.rgb, fbm(u * 0.05));
    s.albedo = lerp(flesh, uSurfC.rgb * 0.6, sac);
    s.emit = uSurfC.rgb * uSurfC.a * sac * pulse * (1.0 - 0.6 * v.x / rad) * det;
    s.tilt = (u / 30.0 - v.zw) * 1.5 * sac;
  } else if (k < 13.5) {
    // khaydarin crystal: facets that each lean their own way, the light caught on their edges,
    // a caustic moving inside, and the glass behind bent by the facets
    float4 v = voronoi(u / 26.0);
    float h = hash21(floor(v.zw * 5.0 + 0.5));
    float2 facet = float2(hash21(v.zw * 3.1), hash21(v.zw * 7.3)) - float2(0.5, 0.5);
    s.tilt = facet * 1.4 * det;
    s.albedo = lerp(uSurfB.rgb, uSurfA.rgb, 0.4 + 0.6 * h);
    float caustic = saturate(vnoise(u * 0.08 + facet * 4.0 + float2(t * 0.6, -t * 0.4)) * 1.25);
    s.emit = uSurfC.rgb * uSurfC.a * (sq(sq(caustic)) * 1.2 + (1.0 - smoothstep(0.0, 0.05, v.y - v.x)) * 0.25);
    s.bend = 24.0;
  } else if (k < 14.5) {
    // gold filigree: raised gold scrollwork over a pale ground
    float2 c = u / 34.0;
    float f1 = abs(sin((c.x + 0.3 * sin(c.y * 3.14159)) * 3.14159));
    float f2 = abs(sin((c.y + 0.3 * sin(c.x * 3.14159)) * 3.14159));
    float fil = max(1.0 - smoothstep(0.05, 0.12, f1), 1.0 - smoothstep(0.05, 0.12, f2)) * det;
    s.albedo = lerp(uSurfB.rgb, uSurfA.rgb, fil);
    s.metal = fil;
    s.gloss = lerp(uSurfP.x * 0.6, uSurfP.x, fil);
    s.tilt = float2(f1 - f2, f2 - f1) * 0.3 * fil;
  } else if (k < 15.5) {
    // psionic energy: a domain-warped flow, glowing where it gathers
    float2 w = u * 0.03;
    float g = fbm(w + float2(fbm(w + float2(t * 0.2, 0.0)), fbm(w - float2(t * 0.15, -4.0))) * 1.6);
    s.albedo = lerp(uSurfB.rgb, uSurfA.rgb, g);
    s.emit = uSurfC.rgb * uSurfC.a * sq(saturate(g * 1.4 - 0.2)) * det * 1.5;
    s.alpha = 0.6 + 0.4 * g;
  } else if (k < 16.5) {
    // white ceramic: smooth, pale, a faint cloud in the glaze
    s.albedo = lerp(uSurfB.rgb, uSurfA.rgb, 0.85 + 0.15 * fbm(u * 0.05)) ;
  } else if (k < 17.5) {
    // machined: dark anodised metal cut by a tool, fine parallel marks along x that
    // change with each wider pass (every 40 units), a slow mottle; metal
    float fine = vnoise(float2(u.x * 0.05, u.y * 2.2));
    float cut = vnoise(float2(u.x * 0.012, floor(u.y / 40.0) * 7.31));
    float mot = fbm(u * 0.035);
    s.albedo = lerp(uSurfB.rgb, uSurfA.rgb, saturate(0.35 + 0.35 * mot + 0.3 * fine * det)) * (0.94 + 0.12 * cut * det);
    s.tilt = float2(0.0, (fine - 0.5) * 0.22 * det);
    s.metal = 1.0;
  } else if (k < 18.5) {
    // cerakote: a matte ceramic coat over metal, a fine stipple and the faintest orange peel
    float st = hash21(floor(u * 1.2));
    float peel = fbm(u * 0.11);
    s.albedo = lerp(uSurfB.rgb, uSurfA.rgb, 0.5 + 0.5 * peel) * (0.95 + 0.1 * st * det);
    s.tilt = float2(peel - 0.47, 0.47 - fbm(u * 0.11 + float2(4.1, 2.3))) * 0.25 * det;
  } else if (k < 19.5) {
    // forged carbon: chopped fibre pressed into flakes, each flake its own shade and lean, so
    // the light finds a few at a time; dark, the flakes close in tone
    float4 v = voronoi(u / 9.0 + float2(fbm(u * 0.04) * 1.2, 0.0));
    float h = hash21(floor(v.zw * 13.0));
    float2 ang = float2(hash21(v.zw * 2.9), hash21(v.zw * 5.3)) - float2(0.5, 0.5);
    float rimf = smoothstep(0.0, 0.06, v.y - v.x);
    s.albedo = lerp(uSurfB.rgb, uSurfA.rgb, 0.3 + 0.7 * h * det) * (0.85 + 0.15 * rimf);
    s.tilt = ang * 0.32 * det;
    s.gloss = uSurfP.x * (0.7 + 0.3 * h);
  } else {
    // armour: a hull's plating. The plane is cut into cells (120 x 72 units at surfaceScale 1),
    // each split up to three times across its longer side at 30 to 70%, so the plates come in
    // many sizes, as panel lines on a hull do. Each plate is its own shade; the joins are dark
    // grooves (surfaceC) lit along one lip; a plate big enough has a socket screw in each corner;
    // about one in fourteen is louvred
    float2 cell = float2(120.0, 72.0);
    float2 c = u / cell;
    float2 ci = floor(c);
    float2 f = (c - ci) * cell;
    float2 lo = float2(0.0, 0.0);
    float2 hi = cell;
    float2 key = ci;
    for (int i = 0; i < 3; i++) {
      float fi = float(i);
      float2 size = hi - lo;
      if (i > 0 && hash21(key + float2(1.7 * fi, 9.2)) < 0.3) break;
      if (size.x < 22.0 && size.y < 22.0) break;
      float across = size.x > size.y * 1.25 ? 1.0 : (size.y > size.x * 1.25 ? 0.0 : step(0.5, hash21(key + float2(4.3, 2.1 * fi))));
      float at = 0.3 + 0.4 * hash21(key + float2(7.7, 3.3 * fi));
      if (across > 0.5) {
        float sx = lo.x + size.x * at;
        if (f.x < sx) { hi.x = sx; key = key * 1.31 + float2(0.17, 0.53); } else { lo.x = sx; key = key * 1.27 + float2(0.71, 0.29); }
      } else {
        float sy = lo.y + size.y * at;
        if (f.y < sy) { hi.y = sy; key = key * 1.13 + float2(0.37, 0.91); } else { lo.y = sy; key = key * 1.41 + float2(0.53, 0.11); }
      }
    }
    float2 dl = f - lo;
    float2 dh = hi - f;
    float2 ps = hi - lo;
    float gx = min(dl.x, dh.x);
    float gy = min(dl.y, dh.y);
    float g = min(gx, gy);
    float h = hash21(key + float2(5.1, 2.7));
    s.albedo = lerp(uSurfB.rgb, uSurfA.rgb, 0.5 + 0.5 * h) * (0.95 + 0.1 * fbm(u * 0.06) * det);
    s.metal = 1.0;
    float groove = 1.0 - smoothstep(0.6, 1.3, g);
    float2 toward = gx < gy ? float2(dl.x < dh.x ? -1.0 : 1.0, 0.0) : float2(0.0, dl.y < dh.y ? -1.0 : 1.0);
    s.tilt = -toward * exp(-max(g - 0.6, 0.0) / 1.1) * 0.85 * det;
    if (hash21(key + float2(8.8, 4.4)) < 0.07 && ps.x > 34.0 && ps.y > 18.0) {
      // louvres: slats across the plate, inside a margin of 7 units
      float inner = step(7.0, gx) * step(5.0, gy);
      float fr = frac((f.y - lo.y) / 3.6);
      float slot = smoothstep(0.58, 0.66, fr) * (1.0 - smoothstep(0.9, 0.98, fr)) * inner;
      s.albedo = lerp(s.albedo, uSurfC.rgb, slot * 0.85);
      s.tilt += float2(0.0, (fr - 0.5) * 0.8) * (1.0 - slot) * inner * det;
    }
    if (ps.x > 26.0 && ps.y > 18.0) {
      float2 sv = float2(dl.x < dh.x ? dl.x - 4.5 : 4.5 - dh.x, dl.y < dh.y ? dl.y - 4.5 : 4.5 - dh.y);
      float head = 1.0 - smoothstep(1.5, 2.1, length(sv));
      float sock = 1.0 - smoothstep(0.42, 0.55, hexDist(sv / 1.3));
      s.tilt += sv / 2.0 * head * 0.7;
      s.albedo = lerp(s.albedo, uSurfA.rgb * 1.25, head * 0.55);
      s.albedo = lerp(s.albedo, uSurfC.rgb, sock * head);
    }
    s.albedo = lerp(s.albedo, uSurfC.rgb, groove * 0.92);
  }
  return s;
}

// ---------------------------------------------------------------------------

// The contact shadow, its own pass (and its own program: in the main one, an early return for
// it crashed D3D's shader compiler): the shape moved away from the light by its height
// (uShadow.y), soft by uShadow.z, as strong as uShadow.x. The player lays it only over what is
// already drawn under the shape.
float4 HudShadow(float2 uv) {
  float2 size = uHalf * 2.0;
  float2 q = uv * (size + float2(uMargin, uMargin) * 2.0) - float2(uMargin, uMargin);
  float sd = sdShape(q - uHalf + uLightFrom * uShadow.y);
  float soft = max(uShadow.z, uPx);
  return float4(0.0, 0.0, 0.0, (1.0 - smoothstep(-soft, soft, sd)) * uShadow.x);
}

// uv: 0..1 across the drawn area, which is the box plus uMargin on every side, y down.
// Returns premultiplied colour.
float4 HudShape(float2 uv) {
  float2 size = uHalf * 2.0;
  float2 q = uv * (size + float2(uMargin, uMargin) * 2.0) - float2(uMargin, uMargin);
  float2 p = q - uHalf;

  float d = sdShape(p);
  float cover = saturate(0.5 - d / uPx);
  float depth = max(-d, 0.0);
  float2 n = sdNormal(p);
  float2 box01 = q / size;
  float2 screenPos = uOrigin + q;
  float lit = dot(n, uLightFrom);
  float3 L3 = normalize(float3(uLightFrom, 0.9));

  float4 base = fillAt(dot(box01 - float2(0.5, 0.5), uFillDir) + 0.5);
  float3 col = base.rgb;
  float alpha = base.a;
  float2 bendBy = float2(0.0, 0.0);

  // the edge's profile and the pattern, both leaning the normal
  float3 prof = edgeProfile(depth);
  float4 pat = patternAt(p, depth);
  float scr = uScratch.x > 0.0 ? scratchAt(p) : 0.0;

  // a surface: the material, lit as a 3D surface whose bevel slopes down to the edge
  if (uSurf > 0.5) {
    Surf s = surfaceAt(p, box01, depth);
    float slope = prof.x * uBevelStrength * 1.6;
    float3 N = normalize(float3(n * slope + s.tilt + pat.xy, 1.0));
    float dif = saturate(dot(N, L3));
    float3 R = reflect(-L3, N);
    float spec = pow(saturate(R.z), 6.0 + 60.0 * s.gloss) * s.gloss;
    float3 specCol = lerp(uHighlight.rgb, s.albedo, s.metal) * uHighlight.a;
    // relief: how hard the light models the surface, about how a flat face looks
    float shadeK = 0.88 + (0.38 * (1.0 - prof.y) + 0.75 * dif - 0.88) * uRelief.x;
    col = s.albedo * max(shadeK, 0.0) * pat.z + specCol * spec * 1.4 * pat.z * uRelief.x + s.emit;
    // sheen: a studio's reflection, so dark metal reads as metal: the normal's
    // reflection of the view finds a soft box on the light's side, and a faint fall of light
    // down the face; strongest at a glancing lean
    if (uSheen.x > 0.0) {
      float3 Rv = float3(2.0 * N.z * N.x, 2.0 * N.z * N.y, 2.0 * N.z * N.z - 1.0);
      float softbox = smoothstep(0.05, 0.5, dot(Rv.xy, uLightFrom)) * (0.35 + 0.65 * (1.0 - N.z));
      float fall = saturate(0.5 + dot(box01 - float2(0.5, 0.5), uLightFrom) * 1.1);
      float sheen = softbox * 0.9 + 0.2 * fall * fall;
      col += uSheenColor.rgb * uSheenColor.a * uSheen.x * sheen * lerp(0.45, 1.0, s.metal) * pat.z;
    }
    alpha *= s.alpha;
    bendBy = s.tilt * s.bend;
  }

  // frosted glass: the screen behind, blurred and tinted, the fill laid over it
  if (uGlass > 0.0) {
    float3 back = SAMPLE_BACK((screenPos + bendBy) / uScreen).rgb;
    back = lerp(back, uGlassTint.rgb, uGlassTint.a);
    back += (hash21(floor(screenPos / uPx)) - 0.5) * uGlassNoise;
    float gs = box01.x + box01.y * 0.55;
    back += float3(1.0, 1.0, 1.0) * uGlassStreak * (exp(-sq((gs - 0.3) / 0.07)) * 0.5 + exp(-sq((gs - 0.43) / 0.018)) * 0.35);
    col = lerp(col, lerp(back, col, alpha), uGlass);
    alpha = lerp(alpha, 1.0, uGlass);
  }

  // the flat bevel of a shape without a surface: the edge slopes up over uBevel units, in its
  // profile; a pattern on it shades the same way
  if (uSurf < 0.5) {
    float b = (lit * prof.x * uBevelStrength + dot(pat.xy, uLightFrom) * 0.5) * uRelief.x;
    col = col + uHighlight.rgb * uHighlight.a * max(b, 0.0) - col * max(-b, 0.0) * 0.8;
    col *= (1.0 - prof.y) * pat.z;
  }

  // scratches: bare metal catching the light, a hairline of shadow beside each
  if (scr > 0.0) col = lerp(col, col * 0.6 + uHighlight.rgb * uHighlight.a * 0.55, scr * step(0.0, -d));

  // worn edges: bare metal where the coat has rubbed off the outline and the profile's
  // crests, uWear.y units deep, broken by noise into chips; more wear reaches further in
  if (uWear.x > 0.0 && d < 0.0) {
    float near = max(exp(-depth / max(uWear.y, 0.3)), prof.z * 0.7);
    float chip = fbm(p * 0.07 + float2(5.3, 1.7)) * 0.75 + vnoise(p * 0.9) * 0.25;
    float worn = smoothstep(0.0, 0.12, near * (0.25 + chip) - (1.0 - uWear.x) * 0.75);
    float3 bare = uWearColor.rgb * (0.5 + 0.5 * saturate(lit * 0.5 + 0.5));
    col = lerp(col, bare, worn * uWearColor.a * 0.85);
  }

  // seams: grooves across the plate every uSeams.x units, at uSeams.w degrees (0 upright),
  // the wall facing the light lit
  if (uSeams.x > 0.0 && depth > uBevel) {
    float sa = uSeams.w * 0.0174533;
    float along = p.x * cos(sa) + p.y * sin(sa);
    float sx = along - (floor(along / uSeams.x) + 0.5) * uSeams.x;
    float hw = uSeams.y * 0.5;
    float groove = 1.0 - smoothstep(hw, hw + uPx, abs(sx));
    float lip = exp(-sq((sx + sign(uLightFrom.x * cos(sa) + uLightFrom.y * sin(sa)) * (hw + 0.8)) / 0.7));
    col = col * (1.0 - 0.6 * groove * uSeams.z) + uHighlight.rgb * 0.25 * lip * uSeams.z * (1.0 - groove);
  }

  // bolts: domed heads every uBolts.z units round the edge, uBolts.y in from it
  if (uBolts.x > 0.0 && d < 0.0) {
    float run = perimeterAt(p);
    float sc = run - (floor(run / max(uBolts.z, 8.0)) + 0.5) * max(uBolts.z, 8.0);
    float2 bp = float2(sc, depth - uBolts.y);
    float bd = length(bp) - uBolts.x;
    // uBolts.w 1 a hex head (flats uBolts.x from its centre), 2 a socket screw (the
    // same head, flat, with a dark hex socket in it)
    if (uBolts.w > 0.5) bd = (hexDist(bp / (2.0 * uBolts.x)) - 0.5) * 2.0 * uBolts.x;
    float2 tangent = float2(-n.y, n.x);
    float2 onScreen = tangent * bp.x - n * bp.y;
    float3 BN = normalize(float3(onScreen / uBolts.x * 0.9, 0.6));
    if (uBolts.w > 0.5) BN = normalize(float3(onScreen / uBolts.x * 1.6 * smoothstep(0.6, 0.95, saturate(1.0 + bd / uBolts.x)), 1.0));
    float bl = saturate(dot(BN, L3));
    float head = saturate(0.5 - bd / uPx);
    col *= 1.0 - 0.45 * exp(-sq(max(bd, 0.0) / 1.2)) * (1.0 - head);
    col = lerp(col, uBoltColor.rgb * (0.35 + 0.9 * bl) + float3(1.0, 1.0, 1.0) * pow(bl, 12.0) * 0.6, head * uBoltColor.a);
    if (uBolts.w > 1.5) {
      float sk = saturate(0.5 - (hexDist(bp / (0.9 * uBolts.x)) - 0.5) * 0.9 * uBolts.x / uPx);
      col = lerp(col, col * 0.12, sk * head);
    }
  }

  // inlay: a line of metal set into the surface at a depth, with a faint glow
  if (uInlay.x > 0.0 && d < 0.0) {
    float off = abs(depth - uInlay.y);
    float il = 1.0 - smoothstep(uInlay.x * 0.5, uInlay.x * 0.5 + uPx, off);
    float3 ic = uInlayColor.rgb * (0.7 + 0.6 * saturate(lit * 0.5 + 0.5)) + float3(1.0, 1.0, 1.0) * sq(sq(saturate(lit))) * 0.4;
    col = lerp(col, ic, il * uInlayColor.a);
    col += uInlayColor.rgb * uInlay.z * exp(-off / 3.0) * 0.5;
  }

  // inner shadow: what the shape, moved away from the light, no longer covers
  if (uInnerShadow > 0.0) {
    float sh = smoothstep(-uInnerShadow * 0.6, uInnerShadow * 0.6, sdShape(p + uLightFrom * uInnerShadow));
    col *= 1.0 - sh * uInnerShadowStrength;
  }

  // rim light: a thin line just inside the edge, brightest facing the light
  if (uRim > 0.0) {
    float r = exp(-depth / max(uRimWidth, 0.01)) * (0.35 + 0.65 * saturate(lit));
    col += uRimColor.rgb * uRimColor.a * r * uRim;
  }
  // crest lines: the same light where the profile breaks, inside the edge
  if (uRelief.y > 0.0 && d < 0.0) col += uRimColor.rgb * uRimColor.a * prof.z * uRelief.y * (0.35 + 0.65 * saturate(lit));

  if (uInnerGlow.a > 0.0) col += uInnerGlow.rgb * uInnerGlow.a * exp(-depth / max(uInnerGlowSize, 0.01));

  // scanlines, drifting down the screen
  if (uLines > 0.0) {
    float l = 0.5 + 0.5 * sin((screenPos.y + uTime * 18.0) * PI2 / max(uLineSpacing, 1.0));
    col *= 1.0 - uLines * l * 0.6;
  }

  // holographic shimmer: a slow band of shifting hue
  if (uHolo > 0.0) {
    float h = frac(box01.x * 0.8 + box01.y * 0.35 - uTime * 0.12);
    float band = exp(-sq((h - 0.5) / 0.09));
    float3 hue = float3(0.5, 0.5, 0.5) + 0.5 * cos(PI2 * (box01.x * 1.3 + uTime * 0.25 + float3(0.0, 0.33, 0.67)));
    col += hue * band * uHolo;
  }

  // stroke, just inside the edge
  if (uLine > 0.0 && uStroke.a > 0.0) {
    float sa = saturate(0.5 - (abs(d + uLine * 0.5) - uLine * 0.5) / uPx) * uStroke.a;
    col = lerp(col, uStroke.rgb, sa);
    alpha = max(alpha, sa);
  }

  // light outside the edge: the energy edge and the outer glow
  float3 glow = float3(0.0, 0.0, 0.0);
  float glowA = 0.0;
  if (uEnergy > 0.0) {
    float run = perimeterAt(p);
    float per = uPathN > 0.5 ? pathLength() : 4.0 * (uHalf.x + uHalf.y);
    float k1 = PI2 * max(1.0, floor(per * 0.09 / PI2 + 0.5)) / per;
    float k2 = PI2 * max(1.0, floor(per * 0.023 / PI2 + 0.5)) / per;
    float flow = 0.55 + 0.45 * sin(run * k1 - uTime * 7.0) * sin(run * k2 + uTime * 2.3);
    float e = exp(-abs(d) / max(uEnergyWidth, 0.01)) * flow * uEnergy;
    col += uEnergyColor.rgb * uEnergyColor.a * e;
    glow += uEnergyColor.rgb * uEnergyColor.a * e;
    glowA = max(glowA, e * uEnergyColor.a);
  }
  if (uOuterGlow.a > 0.0) {
    float og = exp(-max(d, 0.0) / max(uOuterGlowSize, 0.01)) * uOuterGlow.a;
    glow += uOuterGlow.rgb * og;
    glowA = max(glowA, og);
  }

  // reveal: how much of the shape has appeared. Peel (0) opens from the middle outward with
  // a ragged wet edge; warp (1) opens from the vertical centre line out to both sides, the
  // edge shimmering. The edge glows in uRevealColor.
  if (uReveal < 0.999) {
    float rr;
    if (uRevealKind < 0.5) rr = length(p / uHalf) * 0.65 + (fbm(p * 0.03 + float2(3.0, 3.0)) - 0.47) * 0.5;
    else rr = abs(box01.x - 0.5) * 1.8 + 0.08 * vnoise(float2(box01.y * 40.0, uTime * 6.0));
    float th = uReveal * 1.35 - 0.1;
    float shown = 1.0 - smoothstep(th - 0.06, th, rr);
    float edge = exp(-sq((rr - th + 0.03) / 0.035));
    col += uRevealColor.rgb * uRevealColor.a * edge * 1.5;
    glow += uRevealColor.rgb * uRevealColor.a * edge * shown;
    alpha *= max(shown, edge * uRevealColor.a);
    glow *= shown;
    glowA *= shown;
  }

  float a = alpha * cover;
  return float4(col * a + glow * (1.0 - cover), saturate(a + saturate(glowA) * (1.0 - cover)));
}


struct VSHudOut
{
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

// the layer, its colour premultiplied by its alpha, light outside its edge added
float4 PSHudShape(VSHudOut i) : SV_Target { return HudShape(i.uv); }
// the contact shadow it casts on the shapes under it
float4 PSHudShadow(VSHudOut i) : SV_Target { return HudShadow(i.uv); }
