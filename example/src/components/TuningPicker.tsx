import React from 'react';
import { StyleSheet, Text, TouchableOpacity, View } from 'react-native';
import { getTuningsForInstrument } from 'react-native-tuner-engine';
import type { Instrument, TuningPreset } from 'react-native-tuner-engine';

/**
 * Picks one of the presets authored for `instrument`. `undefined` keeps the
 * instrument's default tuning (what the engine applies on setInstrument).
 */
export function TuningPicker({
  instrument,
  value,
  onChange,
}: Readonly<{
  instrument: Instrument;
  value: TuningPreset | undefined;
  onChange: (v: TuningPreset | undefined) => void;
}>) {
  const profiles = getTuningsForInstrument(instrument);
  if (profiles.length === 0) return null;

  return (
    <View style={styles.settingRow}>
      <Text style={styles.settingLabel}>Tuning</Text>
      <View style={styles.chipColumn}>
        <TouchableOpacity
          style={[styles.chip, value === undefined && styles.chipActive]}
          onPress={() => onChange(undefined)}
        >
          <Text
            style={[
              styles.chipText,
              value === undefined && styles.chipTextActive,
            ]}
          >
            default
          </Text>
        </TouchableOpacity>
        {profiles.map((p) => {
          const active = value === p.id;
          const label = p.id.replace(`${instrument}_`, '');
          const strings = p.strings
            .map((s) =>
              s.octaveOffset === 0
                ? s.name
                : `${s.name}${s.octaveOffset > 0 ? '↑' : '↓'}`
            )
            .join(' ');
          return (
            <TouchableOpacity
              key={p.id}
              style={[styles.chip, active && styles.chipActive]}
              onPress={() => onChange(p.id)}
            >
              <Text style={[styles.chipText, active && styles.chipTextActive]}>
                {label}
              </Text>
              <Text style={[styles.chipSub, active && styles.chipSubActive]}>
                {strings}
              </Text>
            </TouchableOpacity>
          );
        })}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  settingRow: {
    marginBottom: 24,
  },
  settingLabel: {
    color: '#ccc',
    fontSize: 14,
    fontWeight: '600',
  },
  chipColumn: {
    gap: 8,
    marginTop: 10,
  },
  chip: {
    paddingVertical: 8,
    paddingHorizontal: 14,
    borderRadius: 12,
    backgroundColor: '#1e1e1e',
  },
  chipActive: {
    backgroundColor: '#27ae60',
  },
  chipText: {
    color: '#888',
    fontSize: 13,
    fontWeight: '500',
  },
  chipTextActive: {
    color: '#fff',
  },
  chipSub: {
    color: '#555',
    fontSize: 12,
    marginTop: 2,
    fontVariant: ['tabular-nums'],
  },
  chipSubActive: {
    color: '#dfffe9',
  },
});
