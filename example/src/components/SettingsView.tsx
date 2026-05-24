import React from 'react';
import { ScrollView, StyleSheet, Switch, Text, View } from 'react-native';
import type { Instrument } from 'react-native-tuner-engine';
import { InstrumentPicker } from './InstrumentPicker';
import { SettingRow } from './SettingRow';

type SettingsViewProps = Readonly<{
  instrument: Instrument;
  a4: number;
  noiseGateDb: number;
  confidenceThreshold: number;
  emaAlpha: number;
  hysteresisFrames: number;
  hpfCutoffHz: number;
  minFrequency: number;
  maxFrequency: number;
  onsetDetection: boolean;
  onInstrumentChange: (v: Instrument) => void;
  onA4Change: (v: number) => void;
  onNoiseGateDbChange: (v: number) => void;
  onConfidenceThresholdChange: (v: number) => void;
  onEmaAlphaChange: (v: number) => void;
  onHysteresisFramesChange: (v: number) => void;
  onHpfCutoffHzChange: (v: number) => void;
  onMinFrequencyChange: (v: number) => void;
  onMaxFrequencyChange: (v: number) => void;
  onOnsetDetectionChange: (v: boolean) => void;
}>;

export function SettingsView({
  instrument,
  a4,
  noiseGateDb,
  confidenceThreshold,
  emaAlpha,
  hysteresisFrames,
  hpfCutoffHz,
  minFrequency,
  maxFrequency,
  onsetDetection,
  onInstrumentChange,
  onA4Change,
  onNoiseGateDbChange,
  onConfidenceThresholdChange,
  onEmaAlphaChange,
  onHysteresisFramesChange,
  onHpfCutoffHzChange,
  onMinFrequencyChange,
  onMaxFrequencyChange,
  onOnsetDetectionChange,
}: SettingsViewProps) {
  return (
    <ScrollView style={styles.settingsScroll} contentContainerStyle={styles.settingsContent}>
      <InstrumentPicker value={instrument} onChange={onInstrumentChange} />

      <SettingRow
        label="A4 Reference"
        value={a4}
        min={420}
        max={460}
        step={1}
        unit="Hz"
        onChange={onA4Change}
      />
      <SettingRow
        label="Noise Gate"
        value={noiseGateDb}
        min={-80}
        max={-20}
        step={1}
        unit="dB"
        onChange={onNoiseGateDbChange}
      />
      <SettingRow
        label="Confidence Threshold"
        value={confidenceThreshold}
        min={0.1}
        max={0.99}
        step={0.05}
        onChange={onConfidenceThresholdChange}
      />
      <SettingRow
        label="EMA Alpha"
        value={emaAlpha}
        min={0.05}
        max={1}
        step={0.05}
        onChange={onEmaAlphaChange}
      />
      <SettingRow
        label="Hysteresis Frames"
        value={hysteresisFrames}
        min={1}
        max={10}
        step={1}
        onChange={onHysteresisFramesChange}
      />
      <SettingRow
        label="HPF Cutoff"
        value={hpfCutoffHz}
        min={20}
        max={300}
        step={5}
        unit="Hz"
        onChange={onHpfCutoffHzChange}
      />
      <SettingRow
        label="Min Frequency"
        value={minFrequency}
        min={20}
        max={200}
        step={5}
        unit="Hz"
        onChange={onMinFrequencyChange}
      />
      <SettingRow
        label="Max Frequency"
        value={maxFrequency}
        min={500}
        max={4000}
        step={50}
        unit="Hz"
        onChange={onMaxFrequencyChange}
      />

      <View style={styles.switchRow}>
        <Text style={styles.switchLabel}>Onset Detection</Text>
        <Switch
          value={onsetDetection}
          onValueChange={onOnsetDetectionChange}
          trackColor={{ false: '#333', true: '#27ae60' }}
          thumbColor="#fff"
        />
      </View>

      <Text style={styles.hint}>
        Changes apply on next Start. Stop → tweak → Start to test.
      </Text>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  settingsScroll: {
    flex: 1,
  },
  settingsContent: {
    padding: 24,
    paddingBottom: 60,
  },
  hint: {
    color: '#555',
    fontSize: 12,
    textAlign: 'center',
    marginTop: 12,
  },
  switchRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 24,
  },
  switchLabel: {
    color: '#ccc',
    fontSize: 14,
    fontWeight: '600',
  },
});
