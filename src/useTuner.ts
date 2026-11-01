import { useCallback, useEffect, useRef, useState } from 'react';
import TunerEngine from './TunerEngine';
import type {
  Instrument,
  PitchEvent,
  TunerConfig,
  TuningPreset,
} from './types';

type UseTunerOptions = TunerConfig & {
  instrument?: Instrument;
  a4?: number;
  /** Overrides the instrument's default tuning. `''` disables string matching. */
  tuning?: TuningPreset | '';
  /** Shifts the tuning's string targets by this many semitones. */
  tuningTranspose?: number;
};

type UseTunerResult = {
  start: () => Promise<void>;
  stop: () => Promise<void>;
  latest: PitchEvent | null;
  isRunning: boolean;
  error: Error | null;
};

export function useTuner(opts: UseTunerOptions = {}): UseTunerResult {
  const [latest, setLatest] = useState<PitchEvent | null>(null);
  const [isRunning, setIsRunning] = useState(false);
  const [error, setError] = useState<Error | null>(null);

  const optsRef = useRef(opts);
  optsRef.current = opts;

  const unsubscribeRef = useRef<(() => void) | null>(null);

  const stop = useCallback(async () => {
    unsubscribeRef.current?.();
    unsubscribeRef.current = null;
    await TunerEngine.stop();
    setIsRunning(false);
  }, []);

  const start = useCallback(async () => {
    setError(null);
    try {
      const granted = await TunerEngine.requestPermission();
      if (!granted) {
        throw new Error('Microphone permission denied');
      }

      const { instrument, a4, tuning, tuningTranspose, ...config } =
        optsRef.current;
      await TunerEngine.configure(config);

      if (instrument) TunerEngine.setInstrument(instrument);
      if (tuning !== undefined) TunerEngine.setTuning(tuning);
      if (tuningTranspose !== undefined) {
        TunerEngine.setTuningTranspose(tuningTranspose);
      }
      if (a4 !== undefined) TunerEngine.setA4(a4);

      unsubscribeRef.current = TunerEngine.onPitch(setLatest);

      await TunerEngine.start();
      setIsRunning(true);
    } catch (e) {
      unsubscribeRef.current?.();
      unsubscribeRef.current = null;
      setError(e instanceof Error ? e : new Error(String(e)));
    }
  }, []);

  useEffect(() => {
    return () => {
      unsubscribeRef.current?.();
      TunerEngine.stop();
    };
  }, []);

  return { start, stop, latest, isRunning, error };
}
