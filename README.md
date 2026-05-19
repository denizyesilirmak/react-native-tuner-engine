# react-native-tuner-engine

A React Native Turbo Module for real-time instrument pitch detection. The detection pipeline runs entirely in C++ on a dedicated audio thread and delivers per-frame results to JavaScript via the New Architecture event system.

Requires React Native 0.75 or later with the New Architecture enabled.

## How it works

Audio is captured through platform-native APIs (AVAudioEngine on iOS, Oboe on Android) and fed into a lock-free SPSC ring buffer. A C++ worker thread drains the buffer in fixed-size frames, runs the audio through a preprocessing pipeline (high-pass filter, Hann window), detects pitch with the YIN algorithm, maps the result to a musical note, applies median smoothing and hysteresis, then fires a callback with the final `PitchResult`. The native module marshals that result to JS as an `onPitch` event.

The audio callback allocates nothing at runtime. All working buffers are pre-allocated during initialization.

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

The library's `AndroidManifest.xml` already declares `RECORD_AUDIO`. You still need to request the permission at runtime — use `requestPermission()` described below or handle it yourself before calling `start()`.

## API

### `configure(opts)`

Sets engine parameters. Call this before `start()`, or omit it to use the defaults.

```typescript
configure(opts: {
  sampleRate?: number;          // default: 48000
  frameSize?: number;           // default: 2048
  noiseGateDb?: number;         // default: -55
  confidenceThreshold?: number; // default: 0.75, range 0–1
  minFrequency?: number;        // default: 60 Hz
  maxFrequency?: number;        // default: 1200 Hz
}): Promise<void>
```

### `start()`

Starts audio capture and the processing thread. Resolves when the audio session is open and the thread is running.

```typescript
start(): Promise<void>
```

### `stop()`

Stops audio capture and the processing thread. Safe to call if already stopped.

```typescript
stop(): Promise<void>
```

### `requestPermission()`

Requests microphone permission. Returns `true` if granted.

```typescript
requestPermission(): Promise<boolean>
```

### `setA4(hz)`

Sets the reference frequency for A4. Default is 440 Hz. Takes effect immediately without restarting the engine.

```typescript
setA4(hz: number): void
```

### `setInstrument(name)`

Restricts the detection range to the frequency span of a given instrument. Reduces octave errors on instruments with a limited range.

```typescript
setInstrument(name: string): void
// "guitar" | "bass" | "violin" | "cello" | "viola" | "ukulele" | "mandolin" | "banjo" | "chromatic"
```

### `setTemperament(name)`

```typescript
setTemperament(name: string): void
// "equal" | "just"
```

### `getStatus()`

Returns the current engine state synchronously.

```typescript
getStatus(): { isRunning: boolean; engineReady: boolean }
```

### `onPitch(callback)`

Subscribes to pitch events. Returns a subscription with a `remove()` method.

```typescript
onPitch(callback: (event: PitchEvent) => void): { remove: () => void }

type PitchEvent = {
  hasPitch: boolean;
  frequency: number;  // Hz
  confidence: number; // 0–1
  rmsDb: number;      // dBFS
  noteName: string;   // e.g. "A"
  octave: number;
  cents: number;      // deviation from equal temperament, -50 to +50
}
```

## Usage

```typescript
import { useEffect } from 'react';
import {
  configure,
  start,
  stop,
  requestPermission,
  onPitch,
} from 'react-native-tuner-engine';

export function useTuner() {
  useEffect(() => {
    let subscription: ReturnType<typeof onPitch> | null = null;

    async function init() {
      const granted = await requestPermission();
      if (!granted) return;

      await configure({ noiseGateDb: -50, confidenceThreshold: 0.8 });
      await start();

      subscription = onPitch((event) => {
        if (event.hasPitch) {
          console.log(
            `${event.noteName}${event.octave} — ${event.cents > 0 ? '+' : ''}${event.cents.toFixed(1)} cents`
          );
        }
      });
    }

    init();

    return () => {
      subscription?.remove();
      stop();
    };
  }, []);
}
```

## C++ pipeline

The shared C++ core (`cpp/`) is compiled as a static library on both platforms. Stages run in order per frame:

| Stage | Class | Notes |
|---|---|---|
| High-pass filter | `BiquadHpf` | Direct-Form II Transposed, 70 Hz cutoff, Q 0.707 |
| Windowing | `Window` | Hann window, precomputed coefficients |
| Pitch detection | `YinPitchDetector` | YIN algorithm with parabolic interpolation |
| Note mapping | `NoteMapper` | Hz to MIDI, note name, octave, cents deviation |
| SNR estimation | `SnrEstimator` | Signal RMS vs. noise-floor EMA |
| Post-processing | `PostProcessor` | Median-5 filter, EMA, note-transition hysteresis |
| Dispatch | `AudioFrameDispatcher` | SPSC lock-free queue, dedicated worker thread |

## Requirements

- React Native 0.75+ (New Architecture)
- iOS 13+
- Android API 24+, NDK r26+

## License

MIT
