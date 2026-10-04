# Offline evaluation

Measures what the engine does on audio files, frame by frame, so DSP changes
can be judged by numbers instead of by ear on a phone.

## 1. Build the tool

Use Debug if you also run the unit tests (`cpp/tests/main.cpp` uses `assert()`,
which Release compiles out).

```sh
cmake -S cpp/tests -B build/eval -DCMAKE_BUILD_TYPE=Debug
cmake --build build/eval --target tuner_engine_eval
```

## 2. Make a test set

`make_test_set.py` (standard library only) writes WAV files plus a
`manifest.csv` (`file,expected[,instrument]`, paths relative to the manifest).
`expected` is a note name (`A2`, `F#3`, `Bb1`), a frequency in Hz, or `none`
for noise-only files.

```sh
python3 eval/make_test_set.py synth  eval/data/synth   # synthetic strings + noise
python3 eval/make_test_set.py nsynth /path/to/nsynth   # MIDI number from the file name, 60-1200 Hz only
python3 eval/make_test_set.py own    /path/to/mine     # Fs3_take1.wav -> F#3 (note before the first '_')
```

### `synth`

48 kHz, 16-bit mono, 250 ms of leading silence, 4.5 s of note. Karplus-Strong
strings with fractional delay `D = SR/f - 0.5`, decay set for T60 = 4 s, mean of
the initial noise burst removed (the loop preserves DC, and a DC offset makes
the RMS look about 12 dB louder than the tone). Each fundamental is checked
with a 2^17-point FFT to be within 0.1 cent.

Notes: E2 A2 D3 G3 B3 E4 C3 F#3 A3 C4 G4 A4 D5. Variants per note:

| name | content |
| --- | --- |
| `clean` | peak -12 dBFS + -80 dBFS white mic floor |
| `snr20`, `snr10` | clean + white noise, SNR measured on the first 100 ms of the note |
| `hum50` | clean + 50 Hz sine at -36 dB (peak) |
| `quiet_room` | peak -24 dBFS + white at -68 dBFS |
| `far_room` | peak -32 dBFS + white at -62 dBFS |
| `detm12`, `detp7` | -12 / +7 cent detuned (expected = the detuned Hz), with mic floor |

The mic floor on `clean`/`detm12`/`detp7` is deliberate: in digital silence the
noise floor is -140 dB, so the eval would call every sample of the decay tail
"active".

Noise-only files (5 s, expected `none`): white -72/-65/-58/-50/-35 dBFS RMS,
pink -62 and -40, hum 50 Hz -60 and -35, hum 60 Hz -35, hum 50 Hz -50 + white -60.

## 3. Run

```sh
build/eval/tuner_engine_eval eval/data/synth/manifest.csv \
    [--frame 2048] [--overlap 0.5] [--gate DB] \
    [--hold-min C] [--hold-cents C] [--hold-miss N] [--no-hold] \
    [--frames-dir DIR] [--max-gross PCT]
```

- Audio is fed through `TunerEngine` with the same sliding window as
  `AudioFrameDispatcher` (first a full frame, then `frame * (1 - overlap)` new samples).
- `--gate` is applied only when given; otherwise the engine's own default is
  measured, so a gate change can't be hidden by the tool.
- `--hold-*` / `--no-hold` override the note-hold settings (`Pipeline::NoteHold`); only what is passed is overridden.
- `--frames-dir` writes one CSV per file: `time_ms, rms_db, has_pitch, frequency,
  confidence, note, error_cents, stage, detector_confidence, snr_db`.
- `--max-gross PCT` exits 1 when more than PCT % of pitched active frames are
  off by more than 50 cents (for CI).

### Definitions

- **Noise floor** of a file: 10th percentile of per-frame AC RMS (mean removed).
  **Active** = floor + 12 dB, **audible** = floor + 6 dB. They are computed from
  the audio only, never from the engine's gate, so a gated audible note counts as a drop.
- **Onset**: first 256-sample block above the active level (sample accurate).
  Times are measured from the onset to the *end* of the analysis window, so
  latency includes filling the first window.
- **voiced %**: pitched share of active frames. **|cents|**: against the expected pitch.
  **gross**: > 50 cents; **octave**: gross errors within 50 cents of a whole number of octaves.
- **1st ms**: onset to first pitched frame. **lock ms**: onset to the 5th of 5
  consecutive pitched frames within ±5 cents.
- **false %**: pitched frames in `none` files, or before the onset, as a share of those frames.
- **drops g/u/l/s**: active frames without a pitch, by `PitchStage`: gated /
  unvoiced / low confidence / settling.
- **tail ms**: last audible frame minus last pitched frame, i.e. how long the
  needle is gone before the note has faded.

The summary lists totals, drop reasons, tail median / p90 / worst and the 8 worst tails.

## 4. Plot a file

```sh
python3 eval/plot_frames.py frames/clean_G3.wav.csv      # needs matplotlib; writes clean_G3.wav.png
```

Cents error with a ±5 c band on top; RMS and confidence below.
