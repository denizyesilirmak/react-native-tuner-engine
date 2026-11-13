import { describe, expect, it } from '@jest/globals';
import * as fs from 'fs';
import * as path from 'path';
import {
  TUNING_PROFILES,
  getTuningsForInstrument,
  resolveTuningStrings,
} from '../tunings';
import { INSTRUMENTS, TUNING_PRESETS } from '../types';

type NativeRow = {
  name: string;
  stringNumber: number;
  frequency: number;
  octaveOffset: number;
};

/**
 * Parses cpp/include/TuningPresets.hpp so the JS table is checked against the
 * C++ one instead of trusted.
 */
function parseNativePresets(): Record<string, NativeRow[]> {
  const src = fs.readFileSync(
    path.join(__dirname, '../../cpp/include/TuningPresets.hpp'),
    'utf8'
  );
  const presets: Record<string, NativeRow[]> = {};
  const profileRe =
    /inline constexpr TuningProfile \w+ = \{\s*"([\w]+)",\s*\{([\s\S]*?)\},\s*(\d+)\s*\};/g;
  const rowRe = /\{"([^"]+)",\s*(\d+),\s*([\d.]+)f(?:,\s*([+-]?\d))?\}/g;
  for (const m of src.matchAll(profileRe)) {
    const [, id, body, count] = m;
    const rows: NativeRow[] = [];
    for (const r of body!.matchAll(rowRe)) {
      rows.push({
        name: r[1]!,
        stringNumber: Number(r[2]),
        frequency: Number(r[3]),
        octaveOffset: r[4] === undefined ? 0 : Number(r[4]),
      });
    }
    expect(rows).toHaveLength(Number(count));
    presets[id!] = rows;
  }
  return presets;
}

function lookupTable(): string[] {
  const src = fs.readFileSync(
    path.join(__dirname, '../../cpp/include/TuningPresets.hpp'),
    'utf8'
  );
  return [
    ...src.matchAll(/if \(name == "(\w+)"\)\s+return &tuning_presets::/g),
  ].map((m) => m[1]!);
}

function nativeInstruments(): string[] {
  const src = fs.readFileSync(
    path.join(__dirname, '../../cpp/include/InstrumentPresets.hpp'),
    'utf8'
  );
  return [...src.matchAll(/if \(name == "(\w+)"\)\s+return \{/g)].map(
    (m) => m[1]!
  );
}

describe('TUNING_PROFILES', () => {
  const native = parseNativePresets();

  it('covers every TuningPreset id and nothing else', () => {
    expect(Object.keys(TUNING_PROFILES).sort()).toEqual(
      [...TUNING_PRESETS].sort()
    );
  });

  it('matches the C++ table string for string', () => {
    expect(Object.keys(native).sort()).toEqual([...TUNING_PRESETS].sort());
    for (const id of TUNING_PRESETS) {
      const js = TUNING_PROFILES[id].strings.map((s) => ({
        name: s.name,
        stringNumber: s.stringNumber,
        frequency: s.frequency,
        octaveOffset: s.octaveOffset,
      }));
      expect({ id, strings: js }).toEqual({ id, strings: native[id] });
    }
  });

  it('every C++ preset is reachable through tuningPreset()', () => {
    expect(lookupTable().sort()).toEqual([...TUNING_PRESETS].sort());
  });

  it('every JS instrument has a native frequency range (except chromatic)', () => {
    const named = nativeInstruments();
    for (const instrument of INSTRUMENTS) {
      if (instrument === 'chromatic') continue;
      expect(named).toContain(instrument);
    }
  });

  it('string numbers are 1-based and sequential', () => {
    for (const p of Object.values(TUNING_PROFILES)) {
      expect(p.strings.map((s) => s.stringNumber)).toEqual(
        p.strings.map((_, i) => i + 1)
      );
      expect(p.strings.length).toBeLessThanOrEqual(8);
    }
  });

  it('frequencies agree with the note names at A4 = 440', () => {
    const names = [
      'C',
      'C#',
      'D',
      'D#',
      'E',
      'F',
      'F#',
      'G',
      'G#',
      'A',
      'A#',
      'B',
    ];
    const flats: Record<string, string> = {
      Eb: 'D#',
      Ab: 'G#',
      Db: 'C#',
      Gb: 'F#',
      Bb: 'A#',
    };
    for (const p of Object.values(TUNING_PROFILES)) {
      for (const s of p.strings) {
        const pitch = s.name.slice(0, -1);
        const octave = Number(s.name.slice(-1));
        const idx = names.indexOf(flats[pitch] ?? pitch);
        expect(idx).toBeGreaterThanOrEqual(0);
        const midi = 12 * (octave + 1) + idx;
        const hz = 440 * Math.pow(2, (midi - 69) / 12);
        expect(Math.abs(s.frequency - hz)).toBeLessThan(0.02);
      }
    }
  });
});

describe('getTuningsForInstrument', () => {
  it('returns the presets authored for the instrument', () => {
    expect(getTuningsForInstrument('oud').map((p) => p.id)).toEqual([
      'oud_turkish',
      'oud_turkish_d',
      'oud_arabic',
      'oud_arabic_old',
      'oud_arabic_high',
    ]);
    expect(getTuningsForInstrument('chromatic')).toEqual([]);
  });
});

describe('resolveTuningStrings', () => {
  it('scales by A4 and transpose', () => {
    const base = resolveTuningStrings('baglama_kara');
    expect(base.map((s) => s.frequency)).toEqual([196.0, 146.83, 220.0]);

    const cura = resolveTuningStrings('baglama_kara', { transpose: 5 });
    expect(cura[2]!.frequency).toBeCloseTo(293.66, 1); // alt A3 → D4
    expect(cura[2]!.name).toBe('A3'); // notated name stays

    const a442 = resolveTuningStrings('guitar_standard', { a4: 442 });
    expect(a442[0]!.frequency).toBeCloseTo(82.41 * (442 / 440), 3);
  });
});
