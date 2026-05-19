export type Instrument =
  | 'guitar'
  | 'bass'
  | 'violin'
  | 'viola'
  | 'cello'
  | 'ukulele'
  | 'mandolin'
  | 'banjo'
  | 'chromatic';

export type Temperament = 'equal' | 'just';

export type TunerConfig = {
  sampleRate?: number;
  frameSize?: number;
  noiseGateDb?: number;
  confidenceThreshold?: number;
  minFrequency?: number;
  maxFrequency?: number;
};

export type PitchEvent = {
  hasPitch: boolean;
  frequency: number;
  confidence: number;
  rmsDb: number;
  noteName: string;
  octave: number;
  cents: number;
};

export type EngineStatus = {
  isRunning: boolean;
  engineReady: boolean;
};
