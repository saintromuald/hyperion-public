// export_hud_shader.mjs: the HUD's shape shader, made from the design center's.
//
//   node engine/tools/hud/export_hud_shader.mjs [<design center's 04-widescreen-ui folder>]
//
// hudshape.js holds one copy of the shape layers' maths (its BODY, written to read as HLSL),
// which the design center's mock runs as GLSL. This writes it for the GPU presenter:
//   src117/port/render/shaders/hudshape.hlsl   the BODY, with SDL_GPU's registers and the
//                                              entry points PSHudShape and PSHudShadow
//   src117/port/hud/port_hudshape_params.h     the uniform block as a C++ struct
// Its uniform block is padded so that it lays out the same in Direct3D and in SPIR-V
// (tools/shader_layout.py): a float2 after a lone float would sit beside it in Direct3D and on
// the next 8 bytes in SPIR-V, so a pad goes in front of it. Run it again when the design
// center changes the shader; the build compiles what it writes.
import { readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const engine = path.resolve(here, '..', '..');
const source = process.argv[2] ||
  path.resolve(engine, '..', '..', 'local-design-center', 'public', 'hyperion', '04-widescreen-ui');

const ctx = { console, Math, JSON, Object, Array, Number, String };
ctx.window = ctx;
ctx.document = { createElement: () => ({ getContext: () => null, style: {} }) };
vm.createContext(ctx);
vm.runInContext(readFileSync(path.join(source, 'hudshape.js'), 'utf8'), ctx);
const S = ctx.SCHudShape;

// the block laid out: each field's offset, with pads where Direct3D and std140 would differ
const SIZE = { float: 4, float2: 8, float4: 16 };
const fields = [];
let at = 0, pads = 0;
function pad() { fields.push({ type: 'float', name: '_pad' + pads++, at }); at += 4; }
for (const [type, decl, note] of S.UNIFORMS) {
  const m = /^(\w+)\[(\d+)\]$/.exec(decl);
  const name = m ? m[1] : decl, count = m ? Number(m[2]) : 0;
  if (!(type in SIZE)) throw new Error('hudshape.js: a uniform of type ' + type + ' (' + decl + ')');
  if (count || type === 'float4') while (at % 16) pad();
  else if (type === 'float2') while (at % 8) pad();
  fields.push({ type, name, count, note, at });
  at += count ? 16 * count : SIZE[type];
}
while (at % 16) pad();
const total = at;

const hlslBlock = fields.map((f) => '  ' + f.type + ' ' + f.name + (f.count ? '[' + f.count + ']' : '') + ';' +
  (f.note ? '   // ' + f.note : '')).join('\n');
const hlsl = `// hudshape.hlsl: a HUD file's shape layers, for the GPU presenter
// (port_gpu_hud.cpp). Written by engine/tools/hud/export_hud_shader.mjs from the design's
// hudshape.js, whose BODY below is the one copy of the maths the design's own player
// runs too; change it there and run the script again. Only the head is the script's: SDL_GPU's
// registers (a fragment shader's textures in space 2, its uniforms in space 3), the uniform
// block padded so that Direct3D and SPIR-V lay it out alike, and the entry points.
#define CONST static const
#define SAMPLE_BACK(uv) uBack.Sample(uBackSampler, uv)
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> uBack : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState uBackSampler : register(s0, space2);
[[vk::binding(0, 3)]] cbuffer HudShapeParams : register(b0, space3) {
${hlslBlock}
};

${S.BODY.split('\r\n').join('\n')}

struct VSHudOut
{
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

// the layer, its colour premultiplied by its alpha, light outside its edge added
float4 PSHudShape(VSHudOut i) : SV_Target { return HudShape(i.uv); }
// the contact shadow it casts on the shapes under it
float4 PSHudShadow(VSHudOut i) : SV_Target { return HudShadow(i.uv); }
`;

const cxxType = { float: 'float', float2: 'float', float4: 'float' };
const cxxDims = { float: '', float2: '[2]', float4: '[4]' };
const cxxBlock = fields.map((f) => '  ' + cxxType[f.type] + ' ' + f.name + (f.count ? '[' + f.count + ']' : '') +
  cxxDims[f.type] + ';' + (f.note ? '  // ' + f.note : '')).join('\n');
const asserts = fields.filter((f) => !f.name.startsWith('_pad')).map((f) =>
  'static_assert(offsetof(HudShapeParams, ' + f.name + ') == ' + f.at + ', "hudshape.hlsl lays out ' + f.name + ' there");').join('\n');
const cxx = `// port_hudshape_params.h -- hudshape.hlsl's uniform block, which the GPU presenter
// fills for each shape layer it draws (port_gpu_hud.cpp). Written by
// engine/tools/hud/export_hud_shader.mjs with the shader; run it again rather than editing this.
#pragma once

#include <stddef.h>

enum { HUD_SHAPE_EDGES = ${S.MAX_EDGES} };

struct HudShapeParams {
${cxxBlock}
};
static_assert(sizeof(HudShapeParams) == ${total}, "hudshape.hlsl's block is ${total} bytes");
${asserts}
`;

function write(file, text) {
  writeFileSync(file, text.split('\r\n').join('\n').split('\n').join('\r\n'));
  console.log('wrote', path.relative(engine, file));
}
write(path.join(engine, 'src117', 'port', 'render', 'shaders', 'hudshape.hlsl'), hlsl);
write(path.join(engine, 'src117', 'port', 'hud', 'port_hudshape_params.h'), cxx);
console.log(`the block: ${fields.length - pads} fields, ${pads} pads, ${total} bytes`);
