import { TurboModuleRegistry, type TurboModule } from 'react-native';
import type { TunerConfig, EngineStatus } from './types';

export interface Spec extends TurboModule {
  configure(opts: TunerConfig): Promise<void>;
  start(): Promise<void>;
  stop(): Promise<void>;
  setA4(hz: number): void;
  setInstrument(name: string): void;
  setTemperament(name: string): void;
  setTuning(name: string): void;
  requestPermission(): Promise<boolean>;
  getStatus(): EngineStatus;
  addListener(eventName: string): void;
  removeListeners(count: number): void;
}

export default TurboModuleRegistry.getEnforcing<Spec>('TunerEngine');
