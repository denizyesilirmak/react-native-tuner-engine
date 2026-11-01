import type {
  Instrument,
  TuningPreset,
  TuningProfile,
  TuningString,
} from './types';

type Row = [name: string, frequency: number, octaveOffset?: -1 | 1];

/**
 * Builds a profile from `[name, hz, octaveOffset?]` rows, lowest string first.
 * Mirrors `cpp/include/TuningPresets.hpp` — the jest suite checks the two
 * tables stay identical.
 */
function profile(
  id: TuningPreset,
  instrument: Instrument,
  rows: Row[]
): TuningProfile {
  return {
    id,
    instrument,
    strings: rows.map(([name, frequency, octaveOffset], i) => ({
      name,
      stringNumber: i + 1,
      frequency,
      octaveOffset: octaveOffset ?? 0,
    })),
  };
}

/**
 * Every tuning preset the native core knows, with its open-string targets at
 * A4 = 440 Hz. Lets an app draw strings and labels without keeping its own
 * copy of the table.
 */
export const TUNING_PROFILES: Record<TuningPreset, TuningProfile> = {
  guitar_standard: profile('guitar_standard', 'guitar', [
    ['E2', 82.41],
    ['A2', 110.0],
    ['D3', 146.83],
    ['G3', 196.0],
    ['B3', 246.94],
    ['E4', 329.63],
  ]),
  guitar_eb_standard: profile('guitar_eb_standard', 'guitar', [
    ['Eb2', 77.78],
    ['Ab2', 103.83],
    ['Db3', 138.59],
    ['Gb3', 185.0],
    ['Bb3', 233.08],
    ['Eb4', 311.13],
  ]),
  guitar_d_standard: profile('guitar_d_standard', 'guitar', [
    ['D2', 73.42],
    ['G2', 98.0],
    ['C3', 130.81],
    ['F3', 174.61],
    ['A3', 220.0],
    ['D4', 293.66],
  ]),
  guitar_drop_d: profile('guitar_drop_d', 'guitar', [
    ['D2', 73.42],
    ['A2', 110.0],
    ['D3', 146.83],
    ['G3', 196.0],
    ['B3', 246.94],
    ['E4', 329.63],
  ]),
  guitar_drop_c: profile('guitar_drop_c', 'guitar', [
    ['C2', 65.41],
    ['G2', 98.0],
    ['C3', 130.81],
    ['F3', 174.61],
    ['A3', 220.0],
    ['D4', 293.66],
  ]),
  guitar_open_g: profile('guitar_open_g', 'guitar', [
    ['D2', 73.42],
    ['G2', 98.0],
    ['D3', 146.83],
    ['G3', 196.0],
    ['B3', 246.94],
    ['D4', 293.66],
  ]),
  guitar_open_d: profile('guitar_open_d', 'guitar', [
    ['D2', 73.42],
    ['A2', 110.0],
    ['D3', 146.83],
    ['F#3', 185.0],
    ['A3', 220.0],
    ['D4', 293.66],
  ]),
  guitar_open_c: profile('guitar_open_c', 'guitar', [
    ['C2', 65.41],
    ['G2', 98.0],
    ['C3', 130.81],
    ['G3', 196.0],
    ['C4', 261.63],
    ['E4', 329.63],
  ]),
  guitar_dadgad: profile('guitar_dadgad', 'guitar', [
    ['D2', 73.42],
    ['A2', 110.0],
    ['D3', 146.83],
    ['G3', 196.0],
    ['A3', 220.0],
    ['D4', 293.66],
  ]),
  bass_standard: profile('bass_standard', 'bass', [
    ['E1', 41.2],
    ['A1', 55.0],
    ['D2', 73.42],
    ['G2', 98.0],
  ]),
  bass_drop_d: profile('bass_drop_d', 'bass', [
    ['D1', 36.71],
    ['A1', 55.0],
    ['D2', 73.42],
    ['G2', 98.0],
  ]),
  violin_standard: profile('violin_standard', 'violin', [
    ['G3', 196.0],
    ['D4', 293.66],
    ['A4', 440.0],
    ['E5', 659.26],
  ]),
  viola_standard: profile('viola_standard', 'viola', [
    ['C3', 130.81],
    ['G3', 196.0],
    ['D4', 293.66],
    ['A4', 440.0],
  ]),
  cello_standard: profile('cello_standard', 'cello', [
    ['C2', 65.41],
    ['G2', 98.0],
    ['D3', 146.83],
    ['A3', 220.0],
  ]),
  ukulele_standard: profile('ukulele_standard', 'ukulele', [
    ['G4', 392.0],
    ['C4', 261.63],
    ['E4', 329.63],
    ['A4', 440.0],
  ]),

  // Oud — six unison courses.
  oud_turkish: profile('oud_turkish', 'oud', [
    ['C#2', 69.3],
    ['F#2', 92.5],
    ['B2', 123.47],
    ['E3', 164.81],
    ['A3', 220.0],
    ['D4', 293.66],
  ]),
  oud_turkish_d: profile('oud_turkish_d', 'oud', [
    ['D2', 73.42],
    ['A2', 110.0],
    ['B2', 123.47],
    ['E3', 164.81],
    ['A3', 220.0],
    ['D4', 293.66],
  ]),
  oud_arabic: profile('oud_arabic', 'oud', [
    ['C2', 65.41],
    ['F2', 87.31],
    ['A2', 110.0],
    ['D3', 146.83],
    ['G3', 196.0],
    ['C4', 261.63],
  ]),
  oud_arabic_old: profile('oud_arabic_old', 'oud', [
    ['D2', 73.42],
    ['G2', 98.0],
    ['A2', 110.0],
    ['D3', 146.83],
    ['G3', 196.0],
    ['C4', 261.63],
  ]),
  oud_arabic_high: profile('oud_arabic_high', 'oud', [
    ['F2', 87.31],
    ['A2', 110.0],
    ['D3', 146.83],
    ['G3', 196.0],
    ['C4', 261.63],
    ['F4', 349.23],
  ]),

  // Cümbüş — six unison courses; oud presets also apply.
  cumbus_standard: profile('cumbus_standard', 'cumbus', [
    ['A2', 110.0],
    ['B2', 123.47],
    ['E3', 164.81],
    ['A3', 220.0],
    ['D4', 293.66],
    ['G4', 392.0],
  ]),

  // Bağlama — üst (1), orta (2), alt (3). Pair with setTuningTranspose().
  baglama_kara: profile('baglama_kara', 'baglama', [
    ['G3', 196.0],
    ['D3', 146.83, 1],
    ['A3', 220.0, -1],
  ]),
  baglama_baglama: profile('baglama_baglama', 'baglama', [
    ['E3', 164.81],
    ['D3', 146.83, 1],
    ['A3', 220.0, -1],
  ]),
  baglama_misket: profile('baglama_misket', 'baglama', [
    ['F#3', 185.0],
    ['D3', 146.83, 1],
    ['A3', 220.0, -1],
  ]),
  baglama_mustezat: profile('baglama_mustezat', 'baglama', [
    ['F3', 174.61],
    ['D3', 146.83, 1],
    ['A3', 220.0, -1],
  ]),
  baglama_abdal: profile('baglama_abdal', 'baglama', [
    ['G3', 196.0],
    ['A3', 220.0, -1],
    ['A3', 220.0, -1],
  ]),
  baglama_fidayda: profile('baglama_fidayda', 'baglama', [
    ['D3', 146.83],
    ['D3', 146.83, 1],
    ['A3', 220.0, -1],
  ]),

  // Bouzouki — octave pairs on the bass courses only.
  bouzouki_tetrachordo: profile('bouzouki_tetrachordo', 'bouzouki', [
    ['C3', 130.81, 1],
    ['F3', 174.61, 1],
    ['A3', 220.0],
    ['D4', 293.66],
  ]),
  bouzouki_trichordo: profile('bouzouki_trichordo', 'bouzouki', [
    ['D3', 146.83, 1],
    ['A3', 220.0],
    ['D4', 293.66],
  ]),
  bouzouki_irish: profile('bouzouki_irish', 'bouzouki', [
    ['G2', 98.0, 1],
    ['D3', 146.83, 1],
    ['A3', 220.0],
    ['D4', 293.66],
  ]),
};

export function getTuningProfile(id: TuningPreset): TuningProfile {
  return TUNING_PROFILES[id];
}

/** Presets authored for `instrument`, in declaration order. */
export function getTuningsForInstrument(
  instrument: Instrument
): TuningProfile[] {
  return Object.values(TUNING_PROFILES).filter(
    (p) => p.instrument === instrument
  );
}

/**
 * The open-string frequencies the engine actually matches against for the
 * given A4 reference and transpose — the same numbers that drive
 * `PitchEvent.stringDeviation`. Names are left as notated.
 */
export function resolveTuningStrings(
  id: TuningPreset,
  options: { a4?: number; transpose?: number } = {}
): TuningString[] {
  const a4 = options.a4 ?? 440;
  const transpose = options.transpose ?? 0;
  const k = (a4 / 440) * Math.pow(2, transpose / 12);
  return TUNING_PROFILES[id].strings.map((s) => ({
    ...s,
    frequency: s.frequency * k,
  }));
}
