import { Platform, DeviceEventEmitter } from 'react-native';
import NativeTunerEngine from './NativeTunerEngine';
import { INSTRUMENTS, TEMPERAMENTS, TUNING_PRESETS } from './types';
import type {
  EngineStatus,
  Instrument,
  PitchEvent,
  QualityPreset,
  Temperament,
  TunerConfig,
  TuningPreset,
} from './types';

type PitchCallback = (event: PitchEvent) => void;
type Unsubscribe = () => void;

const QUALITY_PRESETS: Record<QualityPreset, { frameSize: number; overlapRatio: number }> = {
  'low-latency': { frameSize: 1024, overlapRatio: 0 },
  'balanced': { frameSize: 2048, overlapRatio: 0.5 },
  'high-accuracy': { frameSize: 4096, overlapRatio: 0.75 },
};

class TunerEngine {
  configure(opts: TunerConfig): Promise<void> {
    const { quality, adaptiveFrameSize, ...rest } = opts;
    const resolved = { ...rest };

    // quality preset overrides frameSize and overlapRatio
    if (quality && QUALITY_PRESETS[quality]) {
      const preset = QUALITY_PRESETS[quality];
      resolved.frameSize = preset.frameSize;
      resolved.overlapRatio = preset.overlapRatio;
    }

    // If user explicitly provided frameSize or quality, disable adaptive frame sizing
    // so setInstrument won't override the chosen frame size.
    if (resolved.frameSize !== undefined || quality !== undefined) {
      (resolved as any).adaptiveFrameSize = false;
    } else {
      (resolved as any).adaptiveFrameSize = adaptiveFrameSize !== false;
    }

    return NativeTunerEngine.configure(resolved);
  }

  start(): Promise<void> {
    return NativeTunerEngine.start();
  }

  stop(): Promise<void> {
    return NativeTunerEngine.stop();
  }

  requestPermission(): Promise<boolean> {
    return NativeTunerEngine.requestPermission();
  }

  setA4(hz: number): void {
    NativeTunerEngine.setA4(hz);
  }

  setInstrument(name: Instrument): void {
    if (__DEV__ && !(INSTRUMENTS as readonly string[]).includes(name)) {
      console.warn(`[TunerEngine] Unknown instrument: "${name}"`);
      return;
    }
    NativeTunerEngine.setInstrument(name);
  }

  setTemperament(name: Temperament): void {
    if (__DEV__ && !(TEMPERAMENTS as readonly string[]).includes(name)) {
      console.warn(`[TunerEngine] Unknown temperament: "${name}"`);
      return;
    }
    NativeTunerEngine.setTemperament(name);
  }

  setTuning(name: TuningPreset | ''): void {
    if (__DEV__ && name !== '' && !(TUNING_PRESETS as readonly string[]).includes(name)) {
      console.warn(`[TunerEngine] Unknown tuning preset: "${name}"`);
      return;
    }
    NativeTunerEngine.setTuning(name);
  }

  getStatus(): EngineStatus {
    return NativeTunerEngine.getStatus() as unknown as EngineStatus;
  }

  onPitch(callback: PitchCallback): Unsubscribe {
    // Primary path (both platforms): JSI direct callback via C++ invokeAsync.
    // The native side calls __tunerEngineOnPitch directly on the JS thread,
    // matching iOS latency on Android Bridgeless / New Arch.
    (globalThis as any).__tunerEngineOnPitch = callback;

    if (Platform.OS === 'ios') {
      // Old-arch iOS fallback: DeviceEventEmitter fires when JSI global is absent.
      const sub = DeviceEventEmitter.addListener('onPitch', callback);
      return () => {
        (globalThis as any).__tunerEngineOnPitch = undefined;
        sub.remove();
      };
    }

    return () => {
      (globalThis as any).__tunerEngineOnPitch = undefined;
    };
  }
}

export default new TunerEngine();
