import { DeviceEventEmitter } from 'react-native';
import NativeTunerEngine from './NativeTunerEngine';
import type {
  EngineStatus,
  Instrument,
  PitchEvent,
  Temperament,
  TunerConfig,
  TuningPreset,
} from './types';

type PitchCallback = (event: PitchEvent) => void;
type Unsubscribe = () => void;

class TunerEngine {
  configure(opts: TunerConfig): Promise<void> {
    return NativeTunerEngine.configure(opts);
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
    NativeTunerEngine.setInstrument(name);
  }

  setTemperament(name: Temperament): void {
    NativeTunerEngine.setTemperament(name);
  }

  setTuning(name: TuningPreset | ''): void {
    NativeTunerEngine.setTuning(name);
  }

  getStatus(): EngineStatus {
    return NativeTunerEngine.getStatus() as EngineStatus;
  }

  onPitch(callback: PitchCallback): Unsubscribe {
    (globalThis as any).__tunerEngineOnPitch = callback;
    const sub = DeviceEventEmitter.addListener('onPitch', callback);
    return () => {
      (globalThis as any).__tunerEngineOnPitch = undefined;
      sub.remove();
    };
  }
}

export default new TunerEngine();
