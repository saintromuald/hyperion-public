"""The skirmish countdown's motion as linear keyframes for the engine (port_intro.cpp).

The design's countdown (countdown-motion.js) gives each piece's state on every frame of
StarCraft II's countdown, 30 a second on a 1920 by 1200 stage. This reduces each channel to
the fewest straight-line keys that stay within a small tolerance of it (Ramer, Douglas and
Peucker) and writes them as C++ tables:

    python tools/intro_motion.py <countdown-motion.js> src117/port/front/port_intro_motion.h

Only numbers come over, no art: Hyperion draws every piece itself (port_intro.cpp).
"""
import json
import sys

# a layer's row: frame, x, y, scale x, scale y, turn (degrees), opacity, the ink's multiplier
# and addition, added light (1 on), the blur's width, the blur's steps
CHANNELS = [('x', 1, 0.5), ('y', 2, 0.5), ('sx', 3, 0.006), ('sy', 4, 0.006), ('rot', 5, 0.8),
            ('alpha', 6, 0.012), ('mult', 7, 0.012), ('add', 8, 2.5), ('blur', 10, 1.5)]
ROLES = {'lightbox': 0, 'beam': 1, 'ring': 2, 'hex': 3, 'orb': 4, 'digit3': 5, 'digit2': 6, 'digit1': 7}


def literal(v):
    text = '%.4g' % v
    if '.' not in text and 'e' not in text:
        text += '.0'
    return text + 'f'


def unwrap(values):
    out = [values[0]]
    for v in values[1:]:
        d = v - out[-1]
        while d > 180:
            d -= 360
        while d < -180:
            d += 360
        out.append(out[-1] + d)
    return out


def reduce(points, tol):
    if len(points) < 3:
        return points
    (f0, v0), (f1, v1) = points[0], points[-1]
    worst, at = -1.0, -1
    for i in range(1, len(points) - 1):
        f, v = points[i]
        d = abs(v - (v0 + (v1 - v0) * (f - f0) / (f1 - f0)))
        if d > worst:
            worst, at = d, i
    if worst <= tol:
        return [points[0], points[-1]]
    return reduce(points[:at + 1], tol)[:-1] + reduce(points[at:], tol)


def main():
    text = open(sys.argv[1], encoding='utf-8').read()
    motion = json.loads(text[text.index('{'):text.rstrip().rstrip(';').rindex('}') + 1])
    keys = []
    layers = []
    for layer in motion['layers']:
        rows = layer['frames']
        chans = []
        for name, k, tol in CHANNELS:
            if layer['role'] == 'lightbox':
                pts = [(r[0], r[1]) for r in rows] if name == 'alpha' else [(rows[0][0], 0.0)]
            else:
                values = [r[k] for r in rows]
                if name == 'rot':
                    values = unwrap(values)
                pts = reduce([(r[0], values[i]) for i, r in enumerate(rows)], tol)
            if name == 'alpha' and layer['role'] == 'lightbox':
                pts = reduce(pts, tol)
            chans.append((len(keys), len(pts)))
            keys.extend(pts)
        layers.append((ROLES[layer['role']], rows[0][0], rows[-1][0], chans,
                       1 if any(r[9] >= 0.5 for r in rows if len(r) > 9) else 0))
    lines = [
        '// port_intro_motion.h -- the skirmish countdown\'s motion, made by',
        '// tools/intro_motion.py from the design\'s countdown-motion.js: each piece\'s channels as',
        '// linear keys, frames at 30 a second on a 1920 by 1200 stage. Numbers only; port_intro.cpp',
        '// draws the pieces. Do not edit; run the tool again.',
        '#ifndef PORT_INTRO_MOTION_H',
        '#define PORT_INTRO_MOTION_H',
        '',
        'struct IntroKey {',
        '  short nFrame;',
        '  float fValue;',
        '};',
        '',
        '// x, y, scale x, scale y, turn in degrees, opacity, the ink\'s multiplier and addition, the',
        '// blur\'s width: each a run of keys in kIntroKeys, its first and its count.',
        'enum { INTRO_X, INTRO_Y, INTRO_SX, INTRO_SY, INTRO_TURN, INTRO_ALPHA, INTRO_MULT, INTRO_ADD, INTRO_BLUR,',
        '       INTRO_CHANNELS };',
        'enum { INTRO_LIGHTBOX, INTRO_BEAM, INTRO_RING, INTRO_HEX, INTRO_ORB, INTRO_DIGIT3, INTRO_DIGIT2, INTRO_DIGIT1 };',
        '',
        'struct IntroLayer {',
        '  int nRole;',
        '  short nFirst, nLast;  // the frames it is on',
        '  short anChannel[INTRO_CHANNELS][2];',
        '  bool bLight;  // drawn as added light at times',
        '};',
        '',
        'enum { INTRO_FRAMES = %d, INTRO_STAGE_W = %d, INTRO_STAGE_H = %d };' % (
            motion['frames'], motion['stage'][0], motion['stage'][1]),
        '',
        'static const IntroKey kIntroKeys[] = {',
    ]
    for i in range(0, len(keys), 6):
        lines.append('    ' + ' '.join('{%d, %s},' % (f, literal(v)) for f, v in keys[i:i + 6]))
    lines += ['};', '', '// back to front, as the design draws them', 'static const IntroLayer kIntroLayers[] = {']
    for role, first, last, chans, light in layers:
        lines.append('    {%d, %d, %d, {%s}, %s},' % (role, first, last, ', '.join('{%d, %d}' % c for c in chans),
                                                    'true' if light else 'false'))
    lines += ['};', '', '#endif', '']
    open(sys.argv[2], 'wb').write('\r\n'.join(lines).encode('ascii'))
    print('%d keys, %d layers' % (len(keys), len(layers)))


if __name__ == '__main__':
    main()
