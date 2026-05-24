# react-native-tuner-engine

[![npm version](https://img.shields.io/npm/v/react-native-tuner-engine)](https://www.npmjs.com/package/react-native-tuner-engine)
[![npm downloads](https://img.shields.io/npm/dm/react-native-tuner-engine)](https://www.npmjs.com/package/react-native-tuner-engine)
[![license](https://img.shields.io/npm/l/react-native-tuner-engine)](LICENSE)
[![platform](https://img.shields.io/badge/platform-iOS%20%7C%20Android-lightgrey)]()

A React Native Turbo Module for real-time instrument pitch detection. The detection pipeline runs entirely in C++ on a dedicated audio thread and delivers per-frame results to JavaScript via the New Architecture event system.

Requires React Native **0.75 or later** with the New Architecture enabled.

## How it works

Audio is captured through platform-native APIs (AVAudioEngine on iOS, Oboe on Android) and fed into a lock-free SPSC ring buffer. A C++ worker thread drains the buffer in fixed-size frames, runs the signal through a preprocessing pipeline, then through an ensemble of three pitch detectors (YIN, PYIN, cepstrum). The ensemble votes for the best estimate and fires an `onPitch` event on the JS thread.

The audio callback allocates nothing at runtime — all working buffers are pre-allocated during initialization.

## Installation

```sh
npm install react-native-tuner-engine
# or
yarn add react-native-tuner-engine
```

### iOS

Add a microphone usage description to your app's `Info.plist`:

```xml
<key>NSMicrophoneUsageDescription</key>
<string>Microphone access is required for pitch detection.</string>
```

Then run `pod install`.

### Android

The library's `AndroidManifest.xml` already declares `RECORD_AUDIO`. You still need to request the permission at runtime — use `requestPermission()` described below, or handle it yourself before calling `start()`.

## Quick start

```tsx
import { useTuner } from 'react-native-tuner-engine';

export function TunerScreen() {
  const { start, stop, latest, isRunning, error } = useTuner({
    noiseGateDb: -50,
    confidenceThreshold: 0.75,
  });

  return (
    <>
      <Text>{latest?.noteName}{latest?.octave} {latest?.cents.toFixed(1)} ¢</Text>
      <Button title={isRunning ? 'Stop' : 'Start'} onPress={isRunning ? stop : start} />
    </>
  );
}
```

`useTuner` requests the microphone permission, configures the engine, and subscribes to events — all in one call.

## API

### `useTuner(options?)`

React hook. Returns `{ start, stop, latest, isRunning, error }`.

```typescript
import { useTuner } from 'react-native-tuner-engine';

const { start, stop, latest, isRunning, error } = useTuner({
  // All fields optional — defaults shown
  sampleRate?: number;          // 48000
  frameSize?: number;           // 2048
  noiseGateDb?: number;         // -55 dBFS
  confidenceThreshold?: number; // 0.75  (0–1)
  minFrequency?: number;        // 60 Hz
  maxFrequency?: number;        // 1200 Hz
  instrument?: Instrument;      // 'chromatic'
  a4?: number;                  // 440 Hz

  // DSP tuning
  emaAlpha?: number;            // 0.35  — smoothing (0.05 = slow/stable, 1.0 = instant)
  hysteresisFrames?: number;    // 3     — frames before a note change is confirmed (1–10)
  hpfCutoffHz?: number;         // 70 Hz — high-pass filter cutoff (30 for bass, 100 for violin)
});
```

`start()` — requests mic permission, configures the engine, then begins capture. Throws if permission is denied.

`stop()` — stops capture. Safe to call when already stopped.

`latest` — the most recent `PitchEvent`, or `null` before the first frame.

`error` — set if `start()` throws; `null` otherwise.

### `TunerEngine`

Low-level imperative API, useful outside of React components.

```typescript
import { TunerEngine } from 'react-native-tuner-engine';

await TunerEngine.requestPermission(); // → boolean
await TunerEngine.configure({ noiseGateDb: -50 });
await TunerEngine.start();

const unsub = TunerEngine.onPitch((event) => {
  if (event.hasPitch) console.log(event.noteName, event.cents);
});

// later:
unsub();
await TunerEngine.stop();
```

### Types

```typescript
type PitchEvent = {
  hasPitch: boolean;
  frequency: number;  // Hz
  confidence: number; // 0–1
  rmsDb: number;      // dBFS
  noteName: string;   // e.g. "A"
  octave: number;
  cents: number;      // −50 to +50 relative to equal temperament
};

type TunerConfig = {
  sampleRate?: number;          // default 48000
  frameSize?: number;           // default 2048
  noiseGateDb?: number;         // default -55 dBFS
  confidenceThreshold?: number; // default 0.75
  minFrequency?: number;        // default 60 Hz
  maxFrequency?: number;        // default 1200 Hz
  a4?: number;                  // default 440 Hz
  emaAlpha?: number;            // default 0.35 — PostProcessor smoothing (0.05–1.0)
  hysteresisFrames?: number;    // default 3    — frames to confirm a note change (1–10)
  hpfCutoffHz?: number;         // default 70   — high-pass filter cutoff in Hz (20–300)
  onsetDetection?: boolean;     // default false — resets smoothing on note attacks
};

type Instrument =
  | 'guitar' | 'bass' | 'violin' | 'viola' | 'cello'
  | 'ukulele' | 'mandolin' | 'banjo' | 'chromatic';

type Temperament = 'equal' | 'just';
```

### Additional methods on `TunerEngine`

```typescript
TunerEngine.setA4(hz: number): void          // default 440
TunerEngine.setInstrument(name: Instrument): void
TunerEngine.setTemperament(name: Temperament): void
TunerEngine.getStatus(): { isRunning: boolean; engineReady: boolean }
```

For the full API reference (all config options, quality presets, adaptive frame size, overlap ratio), see **[documents/API.md](documents/API.md)**.

## C++ pipeline

The shared C++ core (`cpp/`) compiles as a static library on both platforms.

| Stage | Class | Notes |
|---|---|---|
| High-pass filter | `BiquadHpf` | Direct-Form II Transposed, 70 Hz cutoff (configurable), Q 0.707 |
| Windowing | `Window` | Hann window, precomputed coefficients |
| Pitch detection | `EnsembleSelector` | Runs YIN, PYIN, and cepstrum; votes by agreement within 1 semitone |
| — detector 1 | `YinPitchDetector` | YIN with parabolic interpolation |
| — detector 2 | `PyinPitchDetector` | Probabilistic YIN; prunes harmonic aliases |
| — detector 3 | `CepstrumPitchDetector` | Real cepstrum via radix-2 FFT; SNR-based confidence |
| Note mapping | `NoteMapper` | Hz → MIDI, note name, octave, cents deviation |
| SNR estimation | `SnrEstimator` | Signal RMS vs. noise-floor EMA |
| Post-processing | `PostProcessor` | Median-5 filter, EMA smoothing (configurable), note-transition hysteresis (configurable) |
| Dispatch | `AudioFrameDispatcher` | SPSC lock-free queue, dedicated worker thread |

## Performance targets

| Metric | Target |
|---|---|
| Time to first pitch | < 300 ms |
| Per-frame CPU (midrange Android) | < 5 ms |
| Per-frame CPU (iPhone 12+) | < 3 ms |
| Audio-thread allocations | 0 |

## Requirements

- React Native 0.75+ (New Architecture / Bridgeless)
- iOS 13+
- Android API 24+, NDK r26+
