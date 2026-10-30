// hudgolden.mjs: the design center's player and Hyperion's place a HUD design the same way.
//
//   node engine/tools/hud/hudgolden.mjs [<design center's 04-widescreen-ui folder>]
//
// For each built-in design in engine/huds/, with each of its styles and at 4:3, 16:9, 21:9 and
// 32:9, the player's own code (hudplayer.js's choose, wear and validate, and the source of
// its bandOf, placePanel, rectOf, boxOf and tiles, run here as written) places every panel and
// layer, with the options' picks and the 16:9 band, and
// Hyperion.exe --hud-check places them too. Every box must agree within half a unit.
// Exits 1 on a difference.
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const engine = path.resolve(here, '..', '..');
const source = process.argv[2] ||
  path.resolve(engine, '..', '..', 'local-design-center', 'public', 'hyperion', '04-widescreen-ui');
const exe = path.join(engine, 'src117', 'build', 'x64', 'Hyperion.exe');

const DESIGNS = [
  ['mech-span', ['mech-gunmetal', 'mech-graphite', 'mech-blackout']],
  ['mech-span-min', ['mech-gunmetal']],
  ['mech-flank', ['mech-gunmetal', 'mech-blackout']],
  ['mech-flank-right', ['mech-graphite']],
  ['terran-r9', ['terran-r8']],
];
const SHAPES = [[1440, 1080], [1920, 1080], [2560, 1080], [5120, 1440]];
const PICKS = [{}, { resources: 'edge', resourcesBacking: 'off' }, { width: 'band' },
  { width: 'band', resources: 'edge', resourcesBacking: 'off' }];

// the player in a context of its own: window is the context, and nothing here draws
const ctx = { console, Math, JSON, Object, Array, Number, String, isFinite, isNaN, parseInt, parseFloat };
ctx.window = ctx;
ctx.document = { createElement: () => ({ getContext: () => null, style: {} }), head: { appendChild() {} } };
vm.createContext(ctx);
const playerText = readFileSync(path.join(source, 'hudplayer.js'), 'utf8').split('\r\n').join('\n');
vm.runInContext(readFileSync(path.join(source, 'hudshape.js'), 'utf8'), ctx);
vm.runInContext(playerText, ctx);
const H = ctx.SCHudPlayer;

// a function of the player's, by its source: from "function name(" at its indent to the "}" at
// the same indent that closes it
function lift(name, indent = '    ') {
  const start = playerText.indexOf(indent + 'function ' + name + '(');
  if (start < 0) throw new Error('hudplayer.js has no ' + name);
  const end = playerText.indexOf('\n' + indent + '}\n', start);
  return playerText.slice(start, end + indent.length + 3);
}
const placeSrc = 'var SCREEN = 1080, P = {}, rectById = {};\n' + lift('bandOf', '  ') + lift('placePanel', '  ') +
  lift('rectOf') + lift('boxOf') + lift('tiles') +
  'globalThis.__place = { P: P, rectById: rectById, rectOf: rectOf, boxOf: boxOf, tiles: tiles };';
vm.runInContext(placeSrc, ctx);
const place = ctx.__place;

// a game's start, as Hyperion's check reads the bindings: every name 0
const env = {};
Object.keys(H.NAMES).forEach((n) => { env[n] = 0; });
function resolve(v) { return typeof v === 'string' && v.indexOf('{') >= 0 ? H.template(v)(env) : v; }

function playerBoxes(hud, W, Hh) {
  place.P.LW = 1080 * W / Hh;
  place.P.hud = hud;
  for (const k of Object.keys(place.rectById)) delete place.rectById[k];
  const rects = {}, layers = {};
  hud.panels.forEach((pd, p) => {
    const r = place.rectOf(pd);
    place.rectById[pd.id] = r;
    rects[pd.id] = [r.left, r.top, r.w, r.h];
    // a panel outside the band is not drawn while its margin is empty
    if (pd.outside && !(r.w > 0)) return;
    (pd.layers || []).forEach((ld, l) => {
      const s = Object.assign({}, H.LAYER_DEFAULTS);
      for (const k of Object.keys(ld)) if (k !== 'text' && k !== 'layers' && k !== 'note') s[k] = resolve(ld[k]);
      if (ld.visible != null && !s.visible) return;
      if (!(s.opacity > 0)) return;
      s.box = place.boxOf(ld, s, r.w, r.h);
      const list = ld.slot ? [s] : place.tiles(s, r.w, r.h);
      layers[p + ':' + l] = list.map((c) => [c.box.x, c.box.y, c.box.w, c.box.h]);
    });
  });
  return { rects, layers };
}

function hyperionBoxes(layout, style, picks, W, Hh) {
  const args = ['--hud-check', path.join(engine, 'huds', layout + '.json'), '--style',
    path.join(engine, 'huds', 'styles', style + '.json'), '--layout', W + 'x' + Hh];
  for (const [k, v] of Object.entries(picks)) args.push('--pick', k + '=' + v);
  let out;
  try {
    out = execFileSync(exe, args, { encoding: 'utf8' });
  } catch (e) {
    out = e.stdout;
  }
  const d = JSON.parse(out);
  const layers = {};
  for (const it of d.layers || []) (layers[it.p + ':' + it.l] ||= []).push(it.box);
  return { plays: d.plays, errors: d.errors, rects: d.rects || {}, layers };
}

const near = (a, b) => a.length === b.length && a.every((v, i) => Math.abs(v - b[i]) <= 0.5);
let bad = 0, checked = 0, compared = 0;
for (const [layout, styles] of DESIGNS) {
  const layoutJson = JSON.parse(readFileSync(path.join(engine, 'huds', layout + '.json'), 'utf8'));
  for (const style of styles) {
    const styleJson = JSON.parse(readFileSync(path.join(engine, 'huds', 'styles', style + '.json'), 'utf8'));
    for (const picks of PICKS) {
      const sv = H.validateStyle(styleJson);
      const w = H.wear(H.choose(layoutJson, picks), styleJson);
      const v = H.validate(w.hud);
      const errors = sv.errors.concat(w.errors, v.errors);
      for (const [W, Hh] of SHAPES) {
        const name = `${layout} + ${style} ${JSON.stringify(picks)} ${W}x${Hh}`;
        const hx = hyperionBoxes(layout, style, picks, W, Hh);
        if (errors.length || !hx.plays) {
          console.log(`${name}: the player ${errors.length ? 'refuses' : 'plays'}, Hyperion ${hx.plays ? 'plays' : 'refuses'}`);
          errors.concat(hx.errors).slice(0, 6).forEach((e) => console.log('   ' + e));
          bad += errors.length > 0 !== !hx.plays ? 1 : 0;
          continue;
        }
        const js = playerBoxes(v.hud, W, Hh);
        const diffs = [];
        for (const [id, r] of Object.entries(js.rects))
          if (!hx.rects[id] || !near(r, hx.rects[id])) diffs.push(`panel ${id}: player ${r} Hyperion ${hx.rects[id]}`);
        for (const [k, boxes] of Object.entries(js.layers)) {
          const theirs = hx.layers[k];
          if (!theirs) diffs.push(`layer ${k}: the player draws ${boxes.length}, Hyperion none`);
          else if (theirs.length !== boxes.length) diffs.push(`layer ${k}: the player draws ${boxes.length}, Hyperion ${theirs.length}`);
          else boxes.forEach((b, i) => { compared++; if (!near(b, theirs[i])) diffs.push(`layer ${k}[${i}]: player ${b} Hyperion ${theirs[i]}`); });
        }
        for (const k of Object.keys(hx.layers)) if (!js.layers[k]) diffs.push(`layer ${k}: Hyperion draws it, the player does not`);
        checked++;
        if (diffs.length) {
          bad++;
          console.log(`${name}: ${diffs.length} differences`);
          diffs.slice(0, 8).forEach((d) => console.log('   ' + d));
        }
      }
    }
  }
}
console.log(`${checked} placements compared (${compared} boxes), ${bad} with differences`);
process.exit(bad ? 1 : 0);
