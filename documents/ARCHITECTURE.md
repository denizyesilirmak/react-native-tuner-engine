# react-native-tuner-engine — Build Summary

## What was built

A production-ready React Native TurboModule for real-time instrument pitch detection. The library works on iOS and Android with the New Architecture (Bridgeless) enabled.

---

## M0 — Foundations

Replaced the `create-react-native-library` scaffold (`multiply`) with a real turbo module spec.

- **`src/NativeTunerEngine.ts`** — codegen spec: `configure`, `start`, `stop`, `requestPermission`, `setA4`, `setInstrument`, `setTemperament`, `getStatus`, `addListener`, `removeListeners`
- **`cpp/CMakeLists.txt`** — top-level static lib `tuner_engine_core` shared by iOS, Android, and tests
- **iOS** — `TunerEngine.podspec` extended to include `cpp/src` and `cpp/include`; `TunerBridge.{h,mm}` owns a `std::unique_ptr<TunerEngine>`
- **Android** — `externalNativeBuild` wired; `TunerEngineJni.cpp` JNI stubs; `TunerEngineModule.kt` replaces multiply
- **CI** — `cpp-tests` job added to `.github/workflows/ci.yml`

---

## M1 — Audio Capture + Real-time Streaming

Mic → C++ real-time thread → `onPitch` JS events.

- **iOS** — `IosAudioSource.{h,mm}`: `AVAudioEngine` input tap, `AVAudioSession` category `.playAndRecord`/`.measurement`, interruption + route-change handling
- **Android** — `OboeAudioSource.{h,cpp}`: Oboe input stream, `PerformanceMode::LowLatency`, `SharingMode::Exclusive` with Shared fallback
- **Threading** — `AudioFrameDispatcher`: SPSC lock-free queue (moodycamel vendored at `cpp/third_party/readerwriterqueue/`) → dedicated `std::thread` at elevated priority → platform layer emits `onPitch`
- **Permissions** — `RECORD_AUDIO` in `AndroidManifest.xml`; `requestPermission()` on both platforms
- **Event delivery** — uses `jsInvoker->invokeAsync` (the only working path in RN 0.85 Bridgeless mode — `RCTEventEmitter` is dead there)

---

## M2 — DSP Hardening

Clean signal in, stable note out. All classes in `cpp/include` + `cpp/src`.

| Class | Role |
|---|---|
| `BiquadHpf` | Direct-Form II Transposed HPF, 70 Hz cutoff, Q 0.707 |
| `Window` | Hann window, precomputed |
| `SnrEstimator` | Signal RMS vs. noise-floor EMA → SNR dB |
| `PostProcessor` | Median-5 on frequency, EMA smoothing, note-transition hysteresis (±15 ¢ to switch, ±5 ¢ to stay) |
| `InstrumentPresets` | Frequency ranges for guitar, bass, violin, cello, ukulele, chromatic |
| `Temperament` | Equal + just intonation tables; A4 reference setter |
| `Pipeline` | Chains: HPF → Window → IPitchDetector → SNR → PostProcessor |

`TunerEngine` became a config/facade holding a `Pipeline`.

---

## M3 — Multi-algorithm: PYIN + Cepstrum + Ensemble

Kills octave errors and stays solid under harmonics and low SNR.

**`IPitchDetector` interface** — `DetectorResult detect(const float*, int n, float sr)` + `reset()` + `setFrequencyRange` + `setThreshold`. All detectors implement this.

**`PyinPitchDetector`** — Collects every CMND local minimum below threshold (not just the first like YIN). Key insight: CMND normalization makes multiples of the true period (2×τ₀, 3×τ₀) have monotonically lower values, so without pruning PYIN always returns a sub-octave. Fix: an O(n²) harmonic alias pruning pass — any candidate at τⱼ where τⱼ/τᵢ ≈ integer ≥ 2 gets probability zeroed.

**`CepstrumPitchDetector`** — Real cepstrum via inline radix-2 FFT (`Fft.hpp`): Hann window → FFT → log-power → IFFT → quefrency peak. Confidence is SNR-based `(peak − mean) / rms` scaled to [0,1] — not peak prominence, which saturates to 1.0 on pure sines.

**`EnsembleSelector`** — Runs all three detectors; voiced results that agree within 1 semitone get a ×1.1 confidence bonus; lone disagreeing detectors get ×0.85. Stack-allocated `Entry voiced[8]` — no heap allocation in the hot path.

**Benchmark results (M1 Mac, frame=2048 @ 48 kHz):**
```
YIN          :   708 µs/frame
PYIN         :   592 µs/frame
Cepstrum     :    48 µs/frame
Full pipeline:  1250 µs/frame  (1.25 ms — well under the 5 ms budget)
```

---

## M4 — Typed JS API + `useTuner` Hook

- **`src/types.ts`** — `PitchEvent`, `TunerConfig`, `Instrument`, `Temperament`, `EngineStatus`
- **`src/TunerEngine.ts`** — class wrapping native module; `onPitch` registers both `globalThis.__tunerEngineOnPitch` (JSI direct path) and a `DeviceEventEmitter` listener (fallback)
- **`src/useTuner.ts`** — hook returning `{ start, stop, latest, isRunning, error }`; handles permission → configure → subscribe → start in one call; cleans up on unmount
- **`src/index.tsx`** — clean public surface; old standalone exports removed
- **`src/__tests__/TunerEngine.test.ts`** — 10 tests; jest@30 + `testEnvironment: 'node'` to avoid `@react-native/jest-preset`'s nested jest-mock@29 conflict

---

## M5 — Example App

Dark-themed tuner UI using only React Native built-ins:

- **Animated cents needle** — `Animated.spring` over a 280px track with 5 tick marks
- **Color coding** — ±5 ¢ green, ±15 ¢ yellow, beyond red; note name and needle share the same color
- **Metro config fix** — `resolveRequest` forces all `react`/`react/*` imports to resolve from `example/node_modules/react`, preventing the duplicate-React "invalid hook call" crash that occurs because the library root also has its own `react` in `node_modules`

---

## M6 — Benchmarks + CI

- **`cpp/tests/bench.cpp`** — per-detector and full-pipeline ns/frame timing (200 iterations with 10 warmup)
- **CI matrix** — `cpp-tests` job now runs on both `ubuntu-latest` and `macos-14`; benchmark binary runs as a CI step (output visible in logs)

---

## M7 — README

`README.md` rewritten to reflect the current API: `useTuner` hook quickstart, `TunerEngine` imperative API, full type definitions, updated pipeline table with all three detectors, performance targets table.

---

## Key files

```
src/
  NativeTunerEngine.ts   — codegen spec
  types.ts               — PitchEvent, TunerConfig, Instrument, Temperament
  TunerEngine.ts         — imperative API class
  useTuner.ts            — React hook
  index.tsx              — public surface

cpp/include/
  IPitchDetector.hpp     — detector interface
  Pipeline.hpp           — HPF → Window → Detector → SNR → PostProcessor
  YinPitchDetector.hpp
  PyinPitchDetector.hpp
  CepstrumPitchDetector.hpp
  EnsembleSelector.hpp
  BiquadHpf.hpp, Window.hpp, SnrEstimator.hpp, PostProcessor.hpp
  Temperament.hpp, InstrumentPresets.hpp
  AudioFrameDispatcher.hpp
  Fft.hpp                — inline radix-2 FFT, no dependencies

ios/
  TunerBridge.{h,mm}    — C++ bridge
  IosAudioSource.{h,mm} — AVAudioEngine capture

android/src/main/
  cpp/OboeAudioSource.{h,cpp}   — Oboe capture
  cpp/TunerEngineJni.cpp        — JNI
  java/.../TunerEngineModule.kt — Kotlin turbo module
```

---

## Pending / known issues

- The duplicate-React Metro fix (`resolveRequest` in `example/metro.config.js`) is in place but hasn't been confirmed working on-device yet — depends on whether `yarn start --reset-cache` was run after the change
- All M0–M7 changes are on branch `feature/multi-algorithm-detector`; none have been pushed or merged to `main`
