import { NativeEventEmitter, NativeModules } from 'react-native';
import TunerEngineModule from './NativeTunerEngine';

export type PitchEvent = {
  frequency: number;
  confidence: number;
  rmsDb: number;
  noteName: string;
  octave: number;
  cents: number;
  hasPitch: boolean;
};

export type TunerConfig = {
  sampleRate?: number;
  frameSize?: number;
  noiseGateDb?: number;
  confidenceThreshold?: number;
  minFrequency?: number;
  maxFrequency?: number;
};

const emitter = new NativeEventEmitter(NativeModules.TunerEngine);

export function configure(opts: TunerConfig): Promise<void> {
  return TunerEngineModule.configure(opts);
}

export function start(): Promise<void> {
  return TunerEngineModule.start();
}

export function stop(): Promise<void> {
  return TunerEngineModule.stop();
}

export function setA4(hz: number): void {
  TunerEngineModule.setA4(hz);
}

export function setInstrument(name: string): void {
  TunerEngineModule.setInstrument(name);
}

export function setTemperament(name: string): void {
  TunerEngineModule.setTemperament(name);
}

export function requestPermission(): Promise<boolean> {
  return TunerEngineModule.requestPermission();
}

export function getStatus(): Object {
  return TunerEngineModule.getStatus();
}

export function onPitch(callback: (event: PitchEvent) => void) {
  return emitter.addListener('onPitch', callback);
}
