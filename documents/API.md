# react-native-tuner-engine — API Documentation

A high-performance React Native Turbo Module for real-time instrument tuning. Native C++ DSP runs on a dedicated worker thread; pitch results stream to JavaScript via JSI (no bridge serialization).

---

## Table of Contents

- [Exports](#exports)
- [Quick Start](#quick-start)
- [useTuner Hook](#usetuner-hook)
- [TunerEngine Class](#tunerengine-class)
- [Types](#types)
- [Data Flow](#data-flow)
- [Configuration Guide](#configuration-guide)

---

## Exports

```typescript
import {
  TunerEngine,       // Singleton class — imperative API
  useTuner,          // React hook — declarative API
} from 'react-native-tuner-engine';

// Types
import type {
  EngineStatus,
  Instrument,
  PitchEvent,
  QualityPreset,
  Temperament,
  TunerConfig,
  TuningPreset,
} from 'react-native-tuner-engine';
```

---

## Quick Start

```tsx
import { useTuner } from 'react-native-tuner-engine';

function Tuner() {
  const { start, stop, latest, isRunning, error } = useTuner({
    instrument: 'guitar',
    noiseGateDb: -50,
    confidenceThreshold: 0.75,
  });

  return (
    <View>
      <Text>{latest?.noteName ?? '—'}</Text>
      <Text>{latest?.cents.toFixed(1)} ¢</Text>
      <Button title={isRunning ? 'Stop' : 'Start'} onPress={isRunning ? stop : start} />
    </View>
  );
}
```

---

## useTuner Hook

The recommended way to integrate the tuner. Manages permissions, lifecycle, and state automatically.

### Signature

```typescript
function useTuner(opts?: UseTunerOptions): UseTunerResult;
```

### UseTunerOptions

All fields are optional. Extends `TunerConfig` with:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `instrument` | `Instrument` | `undefined` | Activates the string-matching profile for this instrument |
| `a4` | `number` | `440` | A4 reference pitch in Hz |
| _...all `TunerConfig` fields_ | | | See [TunerConfig](#tunerconfig) |

### UseTunerResult

| Field | Type | Description |
|-------|------|-------------|
| `start` | `() => Promise<void>` | Request permission → configure → start listening |
| `stop` | `() => Promise<void>` | Stop audio capture and unsubscribe |
| `latest` | `PitchEvent \| null` | Most recent pitch detection result (null before first event) |
| `isRunning` | `boolean` | Whether the engine is actively processing audio |
| `error` | `Error \| null` | Last error (permission denied, audio failure, etc.) |

### Lifecycle

1. Calling `start()`:
   - Requests microphone permission
   - Calls `TunerEngine.configure(config)`
   - Sets instrument and A4 if provided
   - Subscribes to pitch events
   - Starts native audio capture
2. Calling `stop()`:
   - Unsubscribes from events
   - Stops native audio + DSP pipeline
3. On unmount:
   - Automatic cleanup (unsubscribe + stop)

---

## TunerEngine Class

Singleton instance for imperative control. Use this when you need fine-grained control beyond what the hook provides.

### Methods

#### `configure(opts: TunerConfig): Promise<void>`

Configures the DSP pipeline. Must be called before `start()`. Safe to call while stopped to change settings.

```typescript
await TunerEngine.configure({
  noiseGateDb: -50,
  confidenceThreshold: 0.8,
  emaAlpha: 0.4,
  hpfCutoffHz: 80,
});
```

---

#### `start(): Promise<void>`

Starts audio capture and pitch detection. Rejects if the audio session fails to initialize.

```typescript
await TunerEngine.start();
```

---

#### `stop(): Promise<void>`

Stops audio capture and the DSP worker thread. Safe to call multiple times.

```typescript
await TunerEngine.stop();
```

---

#### `requestPermission(): Promise<boolean>`

Requests microphone permission from the OS. Returns `true` if granted, `false` if denied.

- **iOS**: Uses `AVCaptureDevice.requestAccess(for: .audio)`
- **Android**: Uses `PermissionAwareActivity` with `RECORD_AUDIO`

```typescript
const granted = await TunerEngine.requestPermission();
if (!granted) Alert.alert('Microphone access required');
```

---

#### `setA4(hz: number): void`

Changes the A4 reference frequency. Affects note mapping and cents calculation in real-time (no restart needed).

```typescript
TunerEngine.setA4(442); // Orchestral tuning
```

---

#### `setInstrument(name: Instrument): void`

Activates a built-in instrument profile. This sets the tuning preset and optimizes frequency range.

```typescript
TunerEngine.setInstrument('guitar');
```

---

#### `setTuning(name: TuningPreset | ''): void`

Sets a specific tuning preset. Pass `''` to disable string matching.

```typescript
TunerEngine.setTuning('guitar_drop_d');
```

---

#### `setTemperament(name: Temperament): void`

Sets the temperament system (currently `'equal'` only; `'just'` planned).

```typescript
TunerEngine.setTemperament('equal');
```

---

#### `getStatus(): EngineStatus`

Returns the current engine state synchronously.

```typescript
const { isRunning, engineReady } = TunerEngine.getStatus();
```

---

#### `onPitch(callback: (event: PitchEvent) => void): () => void`

Subscribes to pitch detection events. Returns an unsubscribe function.

- Events fire on every processed audio frame (~23ms at 48kHz / 1024 samples)
- The callback receives a `PitchEvent` regardless of whether pitch was detected (`hasPitch` may be `false`)

```typescript
const unsubscribe = TunerEngine.onPitch((event) => {
  if (event.hasPitch) {
    console.log(`${event.noteName}${event.octave} — ${event.cents.toFixed(1)}¢`);
  }
});

// Later:
unsubscribe();
```

---

## Types

### `TunerConfig`

Configuration for the DSP pipeline. All fields optional — defaults are optimized for guitar.

```typescript
type TunerConfig = {
  sampleRate?: number;           // Default: 48000. Audio sample rate in Hz.
  frameSize?: number;            // Default: 2048. DSP frame size in samples.
  noiseGateDb?: number;          // Default: -55. Frames quieter than this (dBFS) are ignored.
  confidenceThreshold?: number;  // Default: 0.75. Minimum confidence (0–1) to report pitch.
  minFrequency?: number;         // Default: 60. Lowest detectable frequency in Hz.
  maxFrequency?: number;         // Default: 1200. Highest detectable frequency in Hz.
  a4?: number;                   // Default: 440. A4 reference pitch in Hz.
  emaAlpha?: number;             // Default: 0.35. EMA smoothing (0.05–1.0). Lower = smoother.
  hysteresisFrames?: number;     // Default: 3. Frames needed to confirm note change (1–10).
  hpfCutoffHz?: number;          // Default: 70. High-pass filter cutoff in Hz (20–300).
  onsetDetection?: boolean;      // Default: false. Resets PostProcessor on note attacks for faster response.
  overlapRatio?: number;         // Default: 0. Frame overlap (0.0–0.75). Higher = more updates, more CPU.
  adaptiveFrameSize?: boolean;   // Default: true. Auto-select frame size per instrument preset.
  quality?: QualityPreset;       // Overrides frameSize + overlapRatio with a named preset.
};
```

**Overlap & Adaptive Frame Size:**

The `overlapRatio` controls how much of the analysis frame overlaps with the previous frame. With 75% overlap and 2048 frame size, the engine produces pitch updates every ~10.7ms instead of every ~42.7ms.

When `adaptiveFrameSize` is `true` (default), setting the instrument to `'bass'` or `'cello'` automatically switches to a 4096-sample frame for better low-frequency resolution (min detectable drops from 46.9 Hz to 23.4 Hz at 48 kHz). Explicitly setting `frameSize` overrides this behavior.

The `quality` preset is a shorthand:

| Preset | Frame Size | Overlap | Update Rate | Best For |
|--------|-----------|---------|-------------|----------|
| `'low-latency'` | 1024 | 0% | ~21 ms | Real-time feedback, high strings |
| `'balanced'` | 2048 | 50% | ~21 ms | General use |
| `'high-accuracy'` | 4096 | 75% | ~21 ms | Bass guitar, cello |

---

### `PitchEvent`

Emitted on every processed audio frame.

```typescript
type PitchEvent = {
  hasPitch: boolean;        // true if a valid pitch was detected this frame
  frequency: number;        // Detected fundamental frequency in Hz (0 if no pitch)
  confidence: number;       // Detection confidence 0.0–1.0
  rmsDb: number;            // Frame RMS level in dBFS (e.g. -30 = moderate volume)
  noteName: string;         // Nearest note name: "C", "C#", "D", ... "B"
  octave: number;           // MIDI octave number (e.g. 4 for A4=440Hz)
  cents: number;            // Deviation from nearest note in cents (-50 to +50)
  nearestString: string;    // Nearest string name (e.g. "E2") or "" if no tuning active
  stringDeviation: number;  // Cents from that string's target (negative = flat)
};
```

**Note on `hasPitch: false` frames:**  
When the signal is below the noise gate or confidence is too low, `hasPitch` is `false`. All other fields will be zero/empty. Use this to show "listening…" states in your UI.

---

### `Instrument`

```typescript
type Instrument =
  | 'guitar'    // Standard 6-string (E2–E4)
  | 'bass'      // 4-string bass (E1–G3)
  | 'violin'    // (G3–E6)
  | 'viola'     // (C3–A5)
  | 'cello'     // (C2–A4)
  | 'ukulele'   // (G4–A4)
  | 'mandolin'  // (G3–E5)
  | 'banjo'     // 5-string (D3–D4)
  | 'chromatic'; // No string matching, full range
```

---

### `TuningPreset`

```typescript
type TuningPreset =
  | 'guitar_standard'    // E2 A2 D3 G3 B3 E4
  | 'guitar_drop_d'      // D2 A2 D3 G3 B3 E4
  | 'guitar_open_g'      // D2 G2 D3 G3 B3 D4
  | 'bass_standard'      // E1 A1 D2 G2
  | 'bass_drop_d'        // D1 A1 D2 G2
  | 'violin_standard'    // G3 D4 A4 E5
  | 'viola_standard'     // C3 G3 D4 A4
  | 'cello_standard'     // C2 G2 D3 A3
  | 'ukulele_standard';  // G4 C4 E4 A4
```

---

### `Temperament`

```typescript
type Temperament = 'equal' | 'just';
```

Currently only `'equal'` is implemented. `'just'` intonation is planned for a future release.

---

### `QualityPreset`

```typescript
type QualityPreset = 'low-latency' | 'balanced' | 'high-accuracy';
```

A convenience type that configures `frameSize` and `overlapRatio` together. See [TunerConfig](#tunerconfig) for the mapping table.

---

### `EngineStatus`

```typescript
type EngineStatus = {
  isRunning: boolean;    // Audio capture is active
  engineReady: boolean;  // Native DSP pipeline is initialized
};
```

---

## Data Flow

```
┌──────────────────────────────────────────────────────────────────────────┐
│  NATIVE (C++ worker thread)                                              │
│                                                                          │
│  Microphone (iOS: AVAudioEngine / Android: Oboe)                         │
│       │                                                                  │
│       ▼                                                                  │
│  ┌─────────────────────┐                                                 │
│  │ Lock-free Ring Buffer│ ◄── Audio thread pushes PCM float samples      │
│  └──────────┬──────────┘                                                 │
│             │ Worker thread pops fixed-size frames                        │
│             ▼                                                            │
│  ┌─────────────────────┐                                                 │
│  │ High-Pass Filter     │  Biquad HPF (removes DC + rumble)              │
│  └──────────┬──────────┘                                                 │
│             ▼                                                            │
│  ┌─────────────────────┐                                                 │
│  │ Noise Gate (RMS)     │  Below threshold → emit hasPitch=false         │
│  └──────────┬──────────┘                                                 │
│             ▼                                                            │
│  ┌─────────────────────┐                                                 │
│  │ Onset Detector       │  Energy rise → resets PostProcessor (optional) │
│  └──────────┬──────────┘                                                 │
│             ▼                                                            │
│  ┌─────────────────────┐                                                 │
│  │ Hann Window          │  Spectral leakage reduction                    │
│  └──────────┬──────────┘                                                 │
│             ▼                                                            │
│  ┌─────────────────────────────────────────┐                             │
│  │ Pitch Detectors (parallel)              │                             │
│  │  • YIN          (time-domain CMND)      │                             │
│  │  • PYIN         (probabilistic + alias  │                             │
│  │                   pruning)              │                             │
│  │  • Cepstrum     (frequency-domain)      │                             │
│  └──────────┬──────────────────────────────┘                             │
│             ▼                                                            │
│  ┌─────────────────────┐                                                 │
│  │ Ensemble Selector    │  Picks most consistent result across detectors │
│  └──────────┬──────────┘                                                 │
│             ▼                                                            │
│  ┌─────────────────────┐                                                 │
│  │ PostProcessor        │  Median-5 → EMA smooth → hysteresis            │
│  └──────────┬──────────┘                                                 │
│             ▼                                                            │
│  ┌─────────────────────┐                                                 │
│  │ Note Mapper          │  freq → MIDI note → name + octave + cents      │
│  └──────────┬──────────┘                                                 │
│             ▼                                                            │
│  ┌─────────────────────┐                                                 │
│  │ String Matcher       │  freq → nearest string + deviation (optional)  │
│  └──────────┬──────────┘                                                 │
│             │                                                            │
└─────────────┼────────────────────────────────────────────────────────────┘
              │
              │  PitchResult struct
              ▼
┌──────────────────────────────────────────────────────────────────────────┐
│  BRIDGE                                                                  │
│                                                                          │
│  iOS:  jsInvoker->invokeAsync() → sets JSI object properties             │
│  Android: JNI CallVoidMethod → RCTDeviceEventEmitter.emit("onPitch")     │
│                                                                          │
└──────────────┬───────────────────────────────────────────────────────────┘
               │
               │  PitchEvent (JS object)
               ▼
┌──────────────────────────────────────────────────────────────────────────┐
│  JAVASCRIPT                                                              │
│                                                                          │
│  • JSI path: globalThis.__tunerEngineOnPitch(event)  ← zero-copy        │
│  • Fallback: DeviceEventEmitter "onPitch" listener                       │
│                                                                          │
│  useTuner hook:  latest state updates → React re-render                  │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

### Latency Breakdown

| Stage | Typical Duration |
|-------|-----------------|
| Audio buffer fill (1024 samples @ 48kHz) | ~21 ms |
| HPF + Window + Detectors | ~1–3 ms |
| PostProcessor + NoteMapper | < 0.1 ms |
| JSI invoke (iOS) / JNI callback (Android) | < 0.5 ms |
| **Total glass-to-glass** | **~23–25 ms** |

---

## Configuration Guide

### For Guitar (Default)

```typescript
useTuner({
  instrument: 'guitar',
  noiseGateDb: -50,
  confidenceThreshold: 0.75,
  hpfCutoffHz: 70,
  emaAlpha: 0.35,
  hysteresisFrames: 3,
});
```

### For Bass Guitar

```typescript
useTuner({
  instrument: 'bass',
  noiseGateDb: -55,
  confidenceThreshold: 0.7,
  hpfCutoffHz: 30,        // Lower cutoff to preserve fundamental
  minFrequency: 25,       // B0 = 30.87 Hz
  emaAlpha: 0.25,         // Smoother — bass notes ring longer
  hysteresisFrames: 5,
});
```

### For Violin

```typescript
useTuner({
  instrument: 'violin',
  noiseGateDb: -45,
  confidenceThreshold: 0.8,
  hpfCutoffHz: 150,       // Cut all rumble below G3
  minFrequency: 180,
  maxFrequency: 2000,
  emaAlpha: 0.5,          // Faster response for bowed strings
  hysteresisFrames: 2,
});
```

### For Maximum Responsiveness

```typescript
useTuner({
  instrument: 'chromatic',
  onsetDetection: true,   // Instant response on attacks
  emaAlpha: 1.0,          // No smoothing
  hysteresisFrames: 1,    // Instant note switching
  confidenceThreshold: 0.6,
});
```

### For Maximum Stability

```typescript
useTuner({
  instrument: 'guitar',
  emaAlpha: 0.15,         // Heavy smoothing
  hysteresisFrames: 8,    // Very stable display
  confidenceThreshold: 0.85,
  noiseGateDb: -40,       // Ignore quiet signals
});
```

---

## Parameter Tuning Tips

| Parameter | Lower Value | Higher Value |
|-----------|-------------|--------------|
| `noiseGateDb` | More sensitive to quiet signals | Ignores background noise better |
| `confidenceThreshold` | Detects uncertain pitches (may flicker) | Only shows confident detections |
| `emaAlpha` | Smoother, slower response | Jittery but instant |
| `hysteresisFrames` | Fast note switching (may bounce) | Stable display (slight delay) |
| `hpfCutoffHz` | Preserves low fundamentals | Removes more rumble/handling noise |
| `onsetDetection` | — (disabled) | Resets smoothing on note attacks → faster note switching |

### Finding the Right `noiseGateDb`

Use the `rmsDb` field from `PitchEvent`:
1. Start the tuner in a quiet room
2. Note the `rmsDb` values when silent (e.g. -65 dB)
3. Play your instrument softly, note the `rmsDb` (e.g. -35 dB)
4. Set `noiseGateDb` halfway between (e.g. -50 dB)
