import React from 'react';
import { StyleSheet, Text, View } from 'react-native';
import Slider from '@react-native-community/slider';

export function SettingRow({
  label,
  value,
  min,
  max,
  step,
  unit,
  onChange,
}: Readonly<{
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  unit?: string;
  onChange: (v: number) => void;
}>) {
  return (
    <View style={styles.settingRow}>
      <View style={styles.settingHeader}>
        <Text style={styles.settingLabel}>{label}</Text>
        <Text style={styles.settingValue}>
          {step < 1 ? value.toFixed(2) : value.toFixed(0)}
          {unit ? ` ${unit}` : ''}
        </Text>
      </View>
      <Slider
        style={styles.slider}
        minimumValue={min}
        maximumValue={max}
        step={step}
        value={value}
        onSlidingComplete={onChange}
        minimumTrackTintColor="#27ae60"
        maximumTrackTintColor="#333"
        thumbTintColor="#fff"
      />
      <View style={styles.settingRange}>
        <Text style={styles.rangeText}>{min}</Text>
        <Text style={styles.rangeText}>{max}</Text>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  settingRow: {
    marginBottom: 24,
  },
  settingHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 6,
  },
  settingLabel: {
    color: '#ccc',
    fontSize: 14,
    fontWeight: '600',
  },
  settingValue: {
    color: '#27ae60',
    fontSize: 14,
    fontWeight: '700',
    fontVariant: ['tabular-nums'],
  },
  slider: {
    width: '100%',
    height: 36,
  },
  settingRange: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    marginTop: -4,
  },
  rangeText: {
    color: '#444',
    fontSize: 11,
  },
});
