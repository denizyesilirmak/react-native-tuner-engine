#!/usr/bin/env python3
"""Build a test set (WAV files + manifest.csv) for tuner_engine_eval.

  make_test_set.py synth OUT_DIR   synthetic plucked strings + noise-only files
  make_test_set.py nsynth DIR      NSynth wavs, MIDI note taken from the file name
  make_test_set.py wavebase DIR    cluesurf/wavebase guitar notes (string-N-Dx-as-Dx2.wav -> D#2)
  make_test_set.py own DIR         your recordings, note = file name before the first '_'

Standard library only. Writes DIR/manifest.csv (`file,expected[,instrument]`).
"""
import array
import cmath
import math
import os
import random
import re
import struct
import sys
import wave

SR = 48000
LEAD_S = 0.25
RING_S = 4.5          # note length after the lead-in; T60 is 4 s
T60_S = 4.0
BASE_PEAK_DB = -12.0  # peak level of the "clean" note
MIC_FLOOR_DB = -80.0  # white-noise floor of the "clean" and detuned files

NOTES = [  # (name, Hz at A4 = 440)
    ("E2", 82.4069), ("A2", 110.0), ("D3", 146.8324), ("G3", 195.9977),
    ("B3", 246.9417), ("E4", 329.6276), ("C3", 130.8128), ("F#3", 184.9972),
    ("A3", 220.0), ("C4", 261.6256), ("G4", 391.9954), ("A4", 440.0),
    ("D5", 587.3295),
]


def db(x):
    return 10.0 ** (x / 20.0)


# ------------------------------------------------------------------ synthesis

def karplus_strong(freq, n, seed, delay_trim=0.0):
    """y[n] = decay * (y[n-D] + y[n-D-1]) / 2, fractional D = SR/f - 0.5."""
    rng = random.Random(seed)
    D = SR / freq - 0.5 + delay_trim
    Di = int(D)
    fr = D - Di
    decay = 10.0 ** (-3.0 / (T60_S * freq))
    seed_len = Di + 3
    burst = [rng.uniform(-1.0, 1.0) for _ in range(seed_len)]
    mean = sum(burst) / len(burst)  # the averaging loop preserves DC: remove it
    burst = [b - mean for b in burst]

    y = burst + [0.0] * (n - seed_len)
    a0, a1 = 1.0 - fr, fr
    for i in range(seed_len, n):
        p = a0 * y[i - Di] + a1 * y[i - Di - 1]
        q = a0 * y[i - Di - 1] + a1 * y[i - Di - 2]
        y[i] = decay * 0.5 * (p + q)
    return y


def fft(x):
    """Iterative radix-2 FFT, len(x) a power of two."""
    n = len(x)
    j = 0
    x = list(x)
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            x[i], x[j] = x[j], x[i]
    size = 2
    while size <= n:
        w_step = cmath.exp(-2j * math.pi / size)
        half = size // 2
        tw = [1.0 + 0j] * half
        for k in range(1, half):
            tw[k] = tw[k - 1] * w_step
        for start in range(0, n, size):
            for k in range(half):
                u = x[start + k]
                v = x[start + k + half] * tw[k]
                x[start + k] = u + v
                x[start + k + half] = u - v
        size <<= 1
    return x


def measure_fundamental(y, expect_hz):
    """Long Hann-windowed FFT (2^17 samples, zero-padded x2) + log-parabola peak."""
    n = 1 << 17
    seg = y[2400:2400 + n]
    w = [0.5 - 0.5 * math.cos(2 * math.pi * i / n) for i in range(n)]
    padded = [s * wi for s, wi in zip(seg, w)] + [0.0] * n
    spec = fft(padded)
    nfft = 2 * n
    k0 = int(round(expect_hz * nfft / SR))
    lo, hi = max(1, k0 - 40), k0 + 40
    k = max(range(lo, hi), key=lambda i: abs(spec[i]))
    a, b, c = (math.log(abs(spec[k + d]) + 1e-30) for d in (-1, 0, 1))
    delta = 0.5 * (a - c) / (a - 2 * b + c)
    return (k + delta) * SR / nfft


def tuned_note(freq, n, seed):
    """KS note whose fundamental is verified within 0.1 cent by a long FFT."""
    loop_delay = SR / freq
    trim = 0.0
    for _ in range(5):
        y = karplus_strong(freq, n, seed, trim)
        ratio = measure_fundamental(y, freq) / freq
        cents = 1200.0 * math.log2(ratio)
        if abs(cents) <= 0.1:
            return y, cents
        trim += loop_delay * (ratio - 1.0)  # sharp -> longer delay
    raise RuntimeError("could not tune %.3f Hz within 0.1 cent (last error %.3f c)" % (freq, cents))


# --------------------------------------------------------------------- noise

def rms(x):
    return math.sqrt(sum(v * v for v in x) / len(x))


def scaled_to_rms(x, rms_db):
    k = db(rms_db) / rms(x)
    return [v * k for v in x]


def white(n, rms_db, rng):
    return scaled_to_rms([rng.gauss(0.0, 1.0) for _ in range(n)], rms_db)


def pink(n, rms_db, rng):
    """Paul Kellet's economical pink-noise filter."""
    b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0
    out = []
    for _ in range(n):
        w = rng.gauss(0.0, 1.0)
        b0 = 0.99886 * b0 + w * 0.0555179
        b1 = 0.99332 * b1 + w * 0.0750759
        b2 = 0.96900 * b2 + w * 0.1538520
        b3 = 0.86650 * b3 + w * 0.3104856
        b4 = 0.55000 * b4 + w * 0.5329522
        b5 = -0.7616 * b5 - w * 0.0168980
        out.append(b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362)
        b6 = w * 0.115926
    return scaled_to_rms(out, rms_db)


def hum(n, hz, rms_db):
    amp = db(rms_db) * math.sqrt(2.0)
    return [amp * math.sin(2 * math.pi * hz * i / SR) for i in range(n)]


def add(*signals):
    return [sum(v) for v in zip(*signals)]


# ------------------------------------------------------------------- wav / io

def write_wav(path, x):
    pcm = array.array("h", (max(-32768, min(32767, int(round(v * 32767.0)))) for v in x))
    if sys.byteorder == "big":
        pcm.byteswap()
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def write_manifest(directory, rows):
    with open(os.path.join(directory, "manifest.csv"), "w") as f:
        f.write("file,expected\n")
        for name, expected in rows:
            f.write("%s,%s\n" % (name, expected))
    print("wrote %d files + manifest.csv to %s" % (len(rows), directory))


# ----------------------------------------------------------------------- synth

def synth(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    lead = int(LEAD_S * SR)
    n_note = int(RING_S * SR)
    n = lead + n_note
    rows = []

    def emit(name, signal, expected):
        write_wav(os.path.join(out_dir, name + ".wav"), signal)
        rows.append((name + ".wav", expected))

    def place(note, peak_db):
        peak = max(abs(v) for v in note)
        k = db(peak_db) / peak
        return [0.0] * lead + [v * k for v in note]

    for idx, (name, hz) in enumerate(NOTES):
        tag = name.replace("#", "s")
        rng = random.Random(1000 + idx)
        base, err = tuned_note(hz, n_note, 7 + idx)
        print("%-3s %8.3f Hz  KS fundamental error %+.3f cent" % (name, hz, err), flush=True)

        clean = place(base, BASE_PEAK_DB)
        # Digital silence is not a real capture: with a floor of exactly 0 the
        # eval would call every sample of the decay tail "active". Give the
        # nominally clean files a faint mic floor.
        clean_mic = add(clean, white(n, MIC_FLOOR_DB, rng))
        # "SNR" = note RMS over its first 100 ms against the white-noise RMS.
        tone_rms_db = 20 * math.log10(rms(clean[lead:lead + SR // 10]))

        emit("clean_" + tag, clean_mic, name)
        for snr in (20, 10):
            emit("snr%d_%s" % (snr, tag), add(clean, white(n, tone_rms_db - snr, rng)), name)
        emit("hum50_" + tag, add(clean, hum(n, 50.0, -36.0 - 3.0103)), name)  # -36 dB peak
        emit("quiet_room_" + tag, add(place(base, -24.0), white(n, -68.0, rng)), name)
        emit("far_room_" + tag, add(place(base, -32.0), white(n, -62.0, rng)), name)
        for label, cents in (("detm12", -12.0), ("detp7", 7.0)):
            f = hz * 2.0 ** (cents / 1200.0)
            det, derr = tuned_note(f, n_note, 31 + idx)
            emit("%s_%s" % (label, tag), add(place(det, BASE_PEAK_DB), white(n, MIC_FLOOR_DB, rng)), "%.3f" % f)

    nn = int(5.0 * SR)
    rng = random.Random(4242)
    for level in (-72, -65, -58, -50, -35):
        emit("noise_white%d" % -level, white(nn, level, rng), "none")
    for level in (-62, -40):
        emit("noise_pink%d" % -level, pink(nn, level, rng), "none")
    emit("noise_hum50_60", hum(nn, 50.0, -60.0), "none")
    emit("noise_hum50_35", hum(nn, 50.0, -35.0), "none")
    emit("noise_hum60_35", hum(nn, 60.0, -35.0), "none")
    emit("noise_hum50_white", add(hum(nn, 50.0, -50.0), white(nn, -60.0, rng)), "none")
    write_manifest(out_dir, rows)


# ------------------------------------------------------- external recordings

NOTE_SHARPS = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def midi_name(m):
    return "%s%d" % (NOTE_SHARPS[m % 12], m // 12 - 1)


def nsynth(directory):
    rows = []
    for fn in sorted(os.listdir(directory)):
        if not fn.lower().endswith(".wav"):
            continue
        m = re.search(r"-(\d{1,3})-\d{1,3}\.wav$", fn, re.I) or re.search(r"(\d{2,3})", fn)
        if not m:
            print("skip (no MIDI number): " + fn)
            continue
        midi = int(m.group(1))
        hz = 440.0 * 2.0 ** ((midi - 69) / 12.0)
        if 60.0 <= hz <= 1200.0:
            rows.append((fn, midi_name(midi)))
    write_manifest(directory, rows)


def wavebase(directory):
    """cluesurf/wavebase guitar notes: ...-as-X<octave>.wav or ...-note-X<octave>-fret-NN-T.wav ('x' = sharp).

    Files that are still Git LFS pointers (not downloaded) are skipped."""
    rows = []
    for root, _, files in os.walk(directory):
        for fn in sorted(files):
            m = (re.search(r"-as-([A-G])(x?)(\d)\.wav$", fn)
                 or re.search(r"-note-([A-G])(x?)(\d)-fret-\d+-\d+\.wav$", fn))
            if not m:
                continue
            path = os.path.join(root, fn)
            with open(path, "rb") as f:
                if f.read(4) != b"RIFF":  # Git LFS pointer, not downloaded
                    continue
            letter, sharp, octave = m.groups()
            rel = os.path.relpath(path, directory)
            rows.append((rel, letter + ("#" if sharp else "") + octave))
    rows.sort()
    write_manifest(directory, rows)


def own(directory):
    rows = []
    for fn in sorted(os.listdir(directory)):
        if not fn.lower().endswith(".wav"):
            continue
        prefix = fn.split("_", 1)[0]
        m = re.fullmatch(r"([A-Ga-g])([sb#]?)(-?\d)", prefix)
        if not m:
            print("skip (cannot read note from '%s'): %s" % (prefix, fn))
            continue
        letter, acc, octave = m.groups()
        rows.append((fn, letter.upper() + {"s": "#", "#": "#", "b": "b", "": ""}[acc] + octave))
    write_manifest(directory, rows)


def main(argv):
    modes = {"synth": synth, "nsynth": nsynth, "wavebase": wavebase, "own": own}
    if len(argv) != 3 or argv[1] not in modes:
        print(__doc__)
        return 2
    modes[argv[1]](argv[2])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
