#!/usr/bin/env python3
"""Write the demo gallery patches (wasm/resource-pack/demos/*.json + index.json).

Each demo is a WASM patch-state file the gallery (src/ui/demoGallery.ts) loads in one click,
and an OfflineRender golden test (wasm/tests/test_demo_gallery.cpp, golden values in
wasm/tests/golden/demos.json). Sample references use the content hashes of the bundled kit
and tonal pad, which scripts/gen_drum_kit.py and scripts/gen_tonal_samples.py synthesize
deterministically, so this output is deterministic too.

Usage: scripts/gen_demos.py [output_dir]
"""

import hashlib
import json
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
PACK = os.path.join(ROOT, 'wasm', 'resource-pack')
SCHEMA_VERSION = 5  # kPatchSchemaVersion (wasm/include/BespokeWasm/WasmPatchState.h)


def sample_hash(relative):
    with open(os.path.join(PACK, relative), 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def seq_steps(tones, vels=None, lens=None):
    """notesequencer per-step controls; a velocity of 0 is a rest."""
    controls = {'steps': len(tones)}
    for i, tone in enumerate(tones):
        controls['tone%d' % i] = tone
        controls['vel%d' % i] = (vels[i] if vels else 0.8)
        controls['len%d' % i] = (lens[i] if lens else 0.5)
    return controls


def kit_samplers(x, y):
    kit = [('kick', 36, 0.95), ('snare', 38, 0.8), ('hat', 42, 0.5), ('tom', 45, 0.7)]
    return [('s_' + name, 'sampler', x, y + i * 120,
             {'volume': volume, 'root': pitch, 'mode': 0},
             {'sampleHash': sample_hash('kits/basic/%s.wav' % name)})
            for i, (name, pitch, volume) in enumerate(kit)]


def drums(pattern, x=30, y=200):
    modules = [('drums', 'drumseq', x, y, dict(pattern, steps=16, gate=0.4), {})]
    modules += kit_samplers(x + 290, y - 120)
    cables = [('transport', 0, 'drums', 0)]
    cables += [('drums', i, 's_' + name, 0) for i, name in enumerate(['kick', 'snare', 'hat', 'tom'])]
    return modules, cables


def demo_fm_arp():
    return {
        'title': 'FM arpeggio, ping-pong',
        'description': 'Held chords arpeggiated into the 8-voice FM synth, shaped by the EQ and bounced '
                       'left and right by a tempo-synced ping-pong delay.',
        'bpm': 118,
        'modules': [
            ('seq', 'notesequencer', 30, 120,
             dict(seq_steps([0, 0, 5, 3], lens=[1.0] * 4), interval=0, octave=3), {}),
            ('chord', 'chorder', 300, 120, {'chord': 1}, {}),  # Minor
            ('arp', 'arpeggiator', 520, 120, {'rate': 2, 'mode': 2, 'octaves': 2, 'gate': 0.5}, {}),
            ('fm', 'fmsynth', 740, 120, {'vol': 1.6, 'harmratio': 6, 'mod': 3.5, 'decay': 220, 'sustain': 0.3,
                                          'release': 260, 'modDecay': 250, 'voices': 8}, {}),
            ('eq', 'eq', 1010, 120, {'lowGain': -3, 'midFreq': 900, 'midGain': 2.5, 'highGain': -4}, {}),
            ('pp', 'pingpongdelay', 1300, 120, {'interval': 3, 'feedback': 0.5, 'mix': 0.35}, {}),
            ('out', 'output', 1560, 120, {}, {}),
        ],
        'cables': [('seq', 0, 'chord', 0), ('chord', 0, 'arp', 0), ('arp', 0, 'fm', 0), ('fm', 0, 'eq', 0),
                   ('eq', 0, 'pp', 0), ('pp', 0, 'out', 0)],
    }


def demo_fm_pad():
    return {
        'title': 'FM chord pad',
        'description': 'Slow chords on the FM synth with a soft attack, widened by the chorus and '
                       'washed in reverb.',
        'bpm': 84,
        'modules': [
            ('seq', 'notesequencer', 30, 120,
             dict(seq_steps([0, 3, 5, 4], lens=[1.0] * 4), interval=0, octave=3), {}),
            ('chord', 'chorder', 300, 120, {'chord': 6}, {}),  # Maj7
            ('fm', 'fmsynth', 520, 120, {'harmratio': 7, 'mod': 1.2, 'attack': 400, 'decay': 900,
                                          'sustain': 0.7, 'release': 1200, 'modSustain': 0.5}, {}),
            ('chorus', 'chorus', 800, 120, {'rate': 0.4, 'depth': 0.6, 'mix': 0.5}, {}),
            ('verb', 'reverb', 1060, 120, {'room': 0.85, 'mix': 0.45}, {}),
            ('out', 'output', 1300, 120, {}, {}),
        ],
        'cables': [('seq', 0, 'chord', 0), ('chord', 0, 'fm', 0), ('fm', 0, 'chorus', 0), ('chorus', 0, 'verb', 0),
                   ('verb', 0, 'out', 0)],
    }


def demo_strings():
    return {
        'title': 'Plucked strings',
        'description': 'A sixteenth-note line on the Karplus-Strong string, thinned out by Note chance '
                       '(repeatable seed) and spread with the Haas widener.',
        'bpm': 104,
        'modules': [
            ('seq', 'notesequencer', 30, 120,
             dict(seq_steps([0, 4, 7, 4, 9, 7, 4, 2, 0, 4, 7, 11, 9, 7, 4, 2]), interval=2, octave=4,
                  rows=12), {}),
            ('chance', 'notechance', 300, 120, {'chance': 0.75, 'deterministic': 1, 'length': 16, 'seed': 7}, {}),
            ('ks', 'karplusstrong', 520, 120, {'feedback': 0.985, 'filter': 1.4, 'source': 2}, {}),
            ('wide', 'stereowidth', 800, 120, {'width': 1.4, 'widen': 12}, {}),
            ('verb', 'reverb', 1040, 120, {'room': 0.6, 'mix': 0.3}, {}),
            ('out', 'output', 1280, 120, {}, {}),
        ],
        'cables': [('seq', 0, 'chance', 0), ('chance', 0, 'ks', 0), ('ks', 0, 'wide', 0), ('wide', 0, 'verb', 0),
                   ('verb', 0, 'out', 0)],
    }


def demo_granular():
    return {
        'title': 'Granular pad',
        'description': 'Sea of Grain sprays grains of the bundled pad sample at each note\'s pitch, '
                       'with octave grains and a long release into the reverb.',
        'bpm': 72,
        'modules': [
            ('seq', 'notesequencer', 30, 120,
             dict(seq_steps([0, 2, 4, 1], vels=[0.8] * 4, lens=[1.0] * 4), interval=0, octave=3), {}),
            ('grain', 'seaofgrain', 300, 120,
             {'mode': 1, 'pos': 0.4, 'overlap': 8, 'lenMs': 120, 'posRandom': 40, 'octaves': 1,
              'attack': 300, 'release': 1500, 'volume': 0.7},
             {'sampleHash': sample_hash('samples/tonal/pad.wav')}),
            ('verb', 'reverb', 620, 120, {'room': 0.9, 'damping': 0.5, 'mix': 0.5}, {}),
            ('out', 'output', 860, 120, {}, {}),
        ],
        'cables': [('seq', 0, 'grain', 0), ('grain', 0, 'verb', 0), ('verb', 0, 'out', 0)],
    }


def demo_drum_bus():
    modules, cables = drums({'kick': 0x1111 | 0x0100, 'snare': 0x1010, 'hat': 0x5555, 'tom': 0x8008})
    modules += [
        ('mix', 'mixer', 620, 200, {'level1': 0.9, 'level2': 0.8, 'level3': 0.6, 'level4': 0.7,
                                     'pan3': 0.35, 'pan4': 0.65}, {}),
        ('comp', 'compressor', 880, 200, {'threshold': -20, 'ratio': 4, 'makeup': 4}, {}),
        ('crush', 'bitcrush', 1120, 200, {'crush': 10, 'downsamp': 2, 'mix': 0.35}, {}),
        ('out', 'output', 1340, 200, {}, {}),
    ]
    cables += [('s_kick', 0, 'mix', 0), ('s_snare', 0, 'mix', 1), ('s_hat', 0, 'mix', 2), ('s_tom', 0, 'mix', 3),
               ('mix', 0, 'comp', 0), ('comp', 0, 'crush', 0), ('crush', 0, 'out', 0)]
    return {
        'title': 'Drum bus',
        'description': 'Drum sequencer into the bundled kit, panned on the mixer, glued by the '
                       'compressor and roughened with a little bitcrush.',
        'bpm': 112,
        'modules': modules,
        'cables': cables,
    }


def demo_vocoder():
    modules, cables = drums({'kick': 0x1111, 'snare': 0x1010, 'hat': 0x5555, 'tom': 0x0000}, y=420)
    modules += [
        ('mix', 'mixer', 620, 420, {'level1': 0.9, 'level2': 0.9, 'level3': 0.8, 'level4': 0.0}, {}),
        ('seq', 'notesequencer', 30, 60,
         dict(seq_steps([0, 0, 3, 4], lens=[1.0] * 4), interval=0, octave=3), {}),
        ('chord', 'chorder', 300, 60, {'chord': 7}, {}),  # Min7
        ('fm', 'fmsynth', 520, 60, {'harmratio': 6, 'mod': 6, 'sustain': 0.9, 'modSustain': 0.8,
                                     'release': 200}, {}),
        ('voc', 'vocoder', 880, 200, {'bands': 24, 'smooth': 0.4, 'mix': 1.0, 'volume': 1.4}, {}),
        ('out', 'output', 1120, 200, {}, {}),
    ]
    cables += [('s_kick', 0, 'mix', 0), ('s_snare', 0, 'mix', 1), ('s_hat', 0, 'mix', 2),
               ('seq', 0, 'chord', 0), ('chord', 0, 'fm', 0),
               ('fm', 0, 'voc', 0), ('mix', 0, 'voc', 1), ('voc', 0, 'out', 0)]
    return {
        'title': 'Robot drums (vocoder)',
        'description': 'The vocoder\'s carrier is an FM chord; its modulator is the drum kit, so the '
                       'chords speak in the rhythm of the beat.',
        'bpm': 100,
        'modules': modules,
        'cables': cables,
    }


def demo_send_bus():
    return {
        'title': 'Send to an effects bus',
        'description': 'A pluck runs dry to the output while Send taps half of it into a reverb and '
                       'ping-pong delay bus.',
        'bpm': 96,
        'modules': [
            ('seq', 'notesequencer', 30, 120,
             dict(seq_steps([0, 4, 2, 7, 0, 4, 9, 7], vels=[0.9, 0, 0.7, 0, 0.9, 0.6, 0, 0.7]), interval=2,
                  octave=4), {}),
            ('ks', 'karplusstrong', 300, 120, {'vol': 1.8, 'feedback': 0.97, 'source': 1, 'filter': 2}, {}),
            ('send', 'send', 580, 120, {'amount': 0.5}, {}),
            ('verb', 'reverb', 820, 260, {'room': 0.92, 'mix': 1.0}, {}),
            ('pp', 'pingpongdelay', 1060, 260, {'interval': 2, 'feedback': 0.55, 'mix': 1.0}, {}),
            ('out', 'output', 1320, 120, {}, {}),
        ],
        'cables': [('seq', 0, 'ks', 0), ('ks', 0, 'send', 0), ('send', 0, 'out', 0), ('send', 1, 'verb', 0),
                   ('verb', 0, 'pp', 0), ('pp', 0, 'out', 0)],
    }


def demo_mod_sweep():
    return {
        'title': 'Modulated bass',
        'description': 'A step-sequenced saw bass transposed by Capo and Pitch bender; a looping Ramper '
                       'sweeps the filter and a note-triggered Envelope pumps the gain.',
        'bpm': 124,
        'modules': [
            ('steps', 'stepsequencer', 30, 120, {'pattern': 0xB6DB, 'pitch': 48, 'gate': 0.6, 'steps': 16}, {}),
            ('capo', 'capo', 300, 120, {'capo': -12}, {}),
            ('bend', 'pitchbender', 520, 120, {'bend': 0.25, 'range': 2}, {}),
            ('osc', 'oscillator', 740, 120, {'waveform': 1, 'volume': 0.6}, {}),
            ('ramp', 'ramper', 740, 320, {'start': -0.6, 'target': 0.8, 'length': 5, 'mode': 1}, {}),
            ('filt', 'filter', 980, 120, {'cutoff': 700, 'resonance': 0.65, 'type': 0}, {}),
            ('dist', 'distortion', 1220, 120, {'type': 1, 'preamp': 2.5, 'level': 0.6}, {}),
            ('env', 'envelope', 980, 320, {'attack': 2, 'decay': 160, 'sustain': 0.2, 'release': 120,
                                            'low': 0.0, 'high': 0.3}, {}),
            ('gain', 'gain', 1460, 120, {'gain': 0.5}, {}),
            ('out', 'output', 1700, 120, {}, {}),
        ],
        'cables': [('steps', 0, 'capo', 0), ('capo', 0, 'bend', 0), ('bend', 0, 'osc', 0), ('osc', 0, 'filt', 0),
                   ('ramp', 0, 'filt', 1), ('filt', 0, 'dist', 0), ('dist', 0, 'gain', 0), ('steps', 0, 'env', 0),
                   ('env', 0, 'gain', 1), ('gain', 0, 'out', 0)],
    }


DEMOS = [
    ('fm-arpeggio', demo_fm_arp),
    ('fm-chord-pad', demo_fm_pad),
    ('plucked-strings', demo_strings),
    ('granular-pad', demo_granular),
    ('drum-bus', demo_drum_bus),
    ('robot-vocoder', demo_vocoder),
    ('send-bus', demo_send_bus),
    ('modulated-bass', demo_mod_sweep),
]


def build_patch(demo):
    ids = {'transport': 1}
    modules = [{'id': 1, 'type': 'transport', 'position': {'x': 30.0, 'y': 20.0},
                'controls': {'bpm': float(demo['bpm']), 'swing': 0.0}}]
    for key, type_, x, y, controls, extras in demo['modules']:
        ids[key] = len(modules) + 1
        module = {'id': ids[key], 'type': type_, 'position': {'x': float(x), 'y': float(y + 60)},
                  'controls': {name: float(value) for name, value in sorted(controls.items())}}
        if extras:
            module['extras'] = extras
        modules.append(module)
    connections = [{'source': {'module': ids[src], 'port': src_port},
                    'destination': {'module': ids[dst], 'port': dst_port}}
                   for src, src_port, dst, dst_port in demo['cables']]
    return {
        'filetype': 'bespokesynth',
        'format': 'bespokesynth-wasm-state',
        'version': SCHEMA_VERSION,
        'wasm': {'view_mode': 0, 'canvas': {'offset_x': 0.0, 'offset_y': 0.0, 'scale': 0.8}},
        'transport': {'bpm': float(demo['bpm']), 'playing': False},
        'modules': modules,
        'connections': connections,
    }


def main(argv):
    global PACK
    out_dir = argv[0] if argv else os.path.join(PACK, 'demos')
    PACK = os.path.dirname(os.path.abspath(out_dir))  # the kit and pad live next to demos/
    os.makedirs(out_dir, exist_ok=True)
    index = []
    for slug, make in DEMOS:
        demo = make()
        path = os.path.join(out_dir, slug + '.json')
        with open(path, 'w', encoding='utf-8') as f:
            json.dump(build_patch(demo), f, indent=1)
            f.write('\n')
        index.append({
            'id': slug,
            'file': slug + '.json',
            'title': demo['title'],
            'description': demo['description'],
            'modules': sorted({m[1] for m in demo['modules']}),
        })
    with open(os.path.join(out_dir, 'index.json'), 'w', encoding='utf-8') as f:
        json.dump({'version': 1, 'demos': index}, f, indent=1)
        f.write('\n')
    print('wrote %d demos to %s' % (len(index), out_dir))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
