#!/usr/bin/env python3
"""Synthesize the small bundled drum kit for the WASM resource pack.

Four one-shots (kick, snare, hat, tom), 16-bit mono 44.1 kHz WAV, written by
code so the kit has no third-party licence and regenerates bit-identically
(fixed seed, no numpy). Output: wasm/resource-pack/kits/basic/*.wav.

Usage: scripts/gen_drum_kit.py [output_dir]
"""

import math
import os
import random
import struct
import sys
import wave

RATE = 44100


def norm(samples, peak=0.9):
    top = max(abs(s) for s in samples) or 1.0
    return [s * peak / top for s in samples]


def fade_tail(samples, ms=4):
    n = int(RATE * ms / 1000)
    for i in range(n):
        samples[-1 - i] *= i / n
    return samples


def highpass(samples, cutoff):
    # One-pole high-pass: enough to take the rumble out of the noise sources.
    rc = 1.0 / (2.0 * math.pi * cutoff)
    alpha = rc / (rc + 1.0 / RATE)
    out, prev_in, prev_out = [], 0.0, 0.0
    for s in samples:
        prev_out = alpha * (prev_out + s - prev_in)
        prev_in = s
        out.append(prev_out)
    return out


def sweep(seconds, f_start, f_end, sweep_rate, decay):
    out, phase = [], 0.0
    for i in range(int(RATE * seconds)):
        t = i / RATE
        freq = f_end + (f_start - f_end) * math.exp(-t * sweep_rate)
        phase += 2.0 * math.pi * freq / RATE
        out.append(math.sin(phase) * math.exp(-t * decay))
    return out


def kick():
    body = sweep(0.38, 160.0, 46.0, 28.0, 9.0)
    click = [math.exp(-i / (RATE * 0.0012)) * (1.0 if i % 2 else -1.0) * 0.25 for i in range(int(RATE * 0.004))]
    for i, c in enumerate(click):
        body[i] += c
    return fade_tail(norm(body))


def snare(rng):
    n = int(RATE * 0.24)
    tone = sweep(0.24, 230.0, 180.0, 18.0, 24.0)
    noise = highpass([(rng.random() * 2.0 - 1.0) * math.exp(-(i / RATE) * 17.0) for i in range(n)], 1400.0)
    return fade_tail(norm([0.55 * t + 0.85 * x for t, x in zip(tone, noise)]))


def hat(rng):
    # Six detuned square oscillators (the classic 808 metallic cluster) plus a little noise.
    freqs = (205.3, 304.4, 369.6, 522.7, 540.0, 800.0)
    n = int(RATE * 0.11)
    out = []
    for i in range(n):
        t = i / RATE
        metal = sum(1.0 if math.sin(2.0 * math.pi * f * t) > 0 else -1.0 for f in freqs) / len(freqs)
        out.append((0.7 * metal + 0.3 * (rng.random() * 2.0 - 1.0)) * math.exp(-t * 42.0))
    return fade_tail(norm(highpass(out, 6500.0)))


def tom():
    return fade_tail(norm(sweep(0.42, 190.0, 96.0, 14.0, 7.0)))


def write_wav(path, samples):
    with wave.open(path, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(b"".join(struct.pack("<h", int(round(max(-1.0, min(1.0, s)) * 32767))) for s in samples))


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "wasm", "resource-pack", "kits", "basic")
    os.makedirs(out_dir, exist_ok=True)
    rng = random.Random(0xB35B0)
    for name, samples in (("kick", kick()), ("snare", snare(rng)), ("hat", hat(rng)), ("tom", tom())):
        path = os.path.join(out_dir, name + ".wav")
        write_wav(path, samples)
        print(f"{path}: {os.path.getsize(path)} bytes")


if __name__ == "__main__":
    main()
