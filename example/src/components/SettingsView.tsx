import React from 'react';
import {
  ScrollView,
  StyleSheet,
  Switch,
  Text,
  TouchableOpacity,
  View,
} from 'react-native';
import type { Instrument, QualityPreset } from 'react-native-tuner-engine';
import { InstrumentPicker } from './InstrumentPicker';
import { SettingRow } from './SettingRow';

const QUALITY_OPTIONS: { label: string; value: QualityPreset | undefined }[] = [
  { label: 'Auto', value: undefined },
  { label: 'Low Latency', value: 'low-latency' },
  { label: 'Balanced', value: 'balanced' },
  { label: 'High Accuracy', value: 'high-accuracy' },
];

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
  quality: QualityPreset | undefined;
  overlapRatio: number;
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
  onQualityChange: (v: QualityPreset | undefined) => void;
  onOverlapRatioChange: (v: number) => void;
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
  quality,
  overlapRatio,
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
  onQualityChange,
  onOverlapRatioChange,
}: SettingsViewProps) {
  return (
    <ScrollView
      style={styles.settingsScroll}
      contentContainerStyle={styles.settingsContent}
    >
      <InstrumentPicker value={instrument} onChange={onInstrumentChange} />

      {/* Quality Preset Picker */}
      <Text style={styles.sectionLabel}>Quality Preset</Text>
      <View style={styles.qualityRow}>
        {QUALITY_OPTIONS.map((opt) => (
          <TouchableOpacity
            key={opt.label}
            style={[
              styles.qualityBtn,
              quality === opt.value && styles.qualityBtnActive,
            ]}
            onPress={() => onQualityChange(opt.value)}
          >
            <Text
              style={[
                styles.qualityBtnText,
                quality === opt.value && styles.qualityBtnTextActive,
              ]}
            >
              {opt.label}
            </Text>
          </TouchableOpacity>
        ))}
      </View>

      <SettingRow
        label="Overlap Ratio"
        value={overlapRatio}
        min={0}
        max={0.75}
        step={0.25}
        onChange={onOverlapRatioChange}
      />

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
  sectionLabel: {
    color: '#ccc',
    fontSize: 14,
    fontWeight: '600',
    marginBottom: 8,
  },
  qualityRow: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 8,
    marginBottom: 24,
  },
  qualityBtn: {
    paddingVertical: 8,
    paddingHorizontal: 14,
    borderRadius: 16,
    backgroundColor: '#1a1a1a',
    borderWidth: 1,
    borderColor: '#333',
  },
  qualityBtnActive: {
    backgroundColor: '#27ae60',
    borderColor: '#27ae60',
  },
  qualityBtnText: {
    color: '#888',
    fontSize: 12,
    fontWeight: '600',
  },
  qualityBtnTextActive: {
    color: '#fff',
  },
});
