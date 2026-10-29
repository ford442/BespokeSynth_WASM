#!/usr/bin/env python3
"""Synthesize the bundled tonal sample(s) for the WASM resource pack.

One evolving additive pad at middle C (MIDI 60), 16-bit mono 44.1 kHz WAV, for the
granular synth (seaofgrain) and the demo gallery. Written by code so it has no
third-party licence and regenerates bit-identically (no randomness, no numpy).
Output: wasm/resource-pack/samples/tonal/*.wav. The hash in wasm/src/BundledKit.cpp
must match the file; the unit tests check it.

Usage: scripts/gen_tonal_samples.py [output_dir]
"""

import math
import os
import struct
import sys
import wave

# 14 harmonics of C4 top out near 3.7 kHz, so 22.05 kHz loses nothing and halves the pack bytes;
# SampleStore resamples to the device rate on decode.
RATE = 22050


def pad():
    seconds = 1.6
    root = 261.6256
    n = int(RATE * seconds)
    harmonics = 14
    out = []
    phases = [0.0] * harmonics
    for i in range(n):
        t = i / RATE
        # Brightness drifts so grains taken from different positions sound different.
        bright = 0.35 + 0.3 * math.sin(2.0 * math.pi * 0.7 * t)
        vibrato = 1.0 + 0.003 * math.sin(2.0 * math.pi * 5.1 * t)
        s = 0.0
        for h in range(harmonics):
            k = h + 1
            detune = 1.0 + 0.0007 * math.sin(2.0 * math.pi * (0.23 + 0.11 * h) * t)
            phases[h] += 2.0 * math.pi * root * k * vibrato * detune / RATE
            s += math.sin(phases[h]) * (bright ** h) / k
        out.append(s)
    top = max(abs(s) for s in out) or 1.0
    out = [s * 0.85 / top for s in out]
    fade = int(RATE * 0.02)
    for i in range(fade):
        out[i] *= i / fade
        out[-1 - i] *= i / fade
    return out


def write(path, samples):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<h', int(round(max(-1.0, min(1.0, s)) * 32767))) for s in samples))


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', 'wasm', 'resource-pack', 'samples', 'tonal')
    os.makedirs(out_dir, exist_ok=True)
    write(os.path.join(out_dir, 'pad.wav'), pad())


if __name__ == '__main__':
    main()
