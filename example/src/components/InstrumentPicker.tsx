import React from 'react';
import { StyleSheet, Text, TouchableOpacity, View } from 'react-native';
import type { Instrument } from 'react-native-tuner-engine';

const INSTRUMENTS: Instrument[] = [
  'guitar',
  'bass',
  'violin',
  'viola',
  'cello',
  'ukulele',
  'mandolin',
  'banjo',
  'chromatic',
];

export function InstrumentPicker({
  value,
  onChange,
}: Readonly<{
  value: Instrument;
  onChange: (v: Instrument) => void;
}>) {
  return (
    <View style={styles.settingRow}>
      <Text style={styles.settingLabel}>Instrument</Text>
      <View style={styles.chipRow}>
        {INSTRUMENTS.map((inst) => (
          <TouchableOpacity
            key={inst}
            style={[styles.chip, value === inst && styles.chipActive]}
            onPress={() => onChange(inst)}
          >
            <Text
              style={[styles.chipText, value === inst && styles.chipTextActive]}
            >
              {inst}
            </Text>
          </TouchableOpacity>
        ))}
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
  chipRow: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 8,
    marginTop: 10,
  },
  chip: {
    paddingVertical: 6,
    paddingHorizontal: 14,
    borderRadius: 16,
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
});
