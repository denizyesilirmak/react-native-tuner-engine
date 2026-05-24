import React, { useState } from 'react';
import { StyleSheet, Text, TouchableOpacity, View } from 'react-native';
import { SafeAreaProvider, SafeAreaView } from 'react-native-safe-area-context';
import { useTuner } from 'react-native-tuner-engine';
import type { Instrument, QualityPreset } from 'react-native-tuner-engine';
import { TunerView } from './components/TunerView';
import { SettingsView } from './components/SettingsView';

export default function App() {
  const [tab, setTab] = useState<'tuner' | 'settings'>('tuner');

  // Settings state
  const [noiseGateDb, setNoiseGateDb] = useState(-50);
  const [confidenceThreshold, setConfidenceThreshold] = useState(0.75);
  const [instrument, setInstrument] = useState<Instrument>('guitar');
  const [emaAlpha, setEmaAlpha] = useState(0.35);
  const [hysteresisFrames, setHysteresisFrames] = useState(3);
  const [hpfCutoffHz, setHpfCutoffHz] = useState(70);
  const [minFrequency, setMinFrequency] = useState(60);
  const [maxFrequency, setMaxFrequency] = useState(1200);
  const [a4, setA4] = useState(440);
  const [onsetDetection, setOnsetDetection] = useState(true);
  const [quality, setQuality] = useState<QualityPreset | undefined>(undefined);
  const [overlapRatio, setOverlapRatio] = useState(0);

  const { start, stop, latest, isRunning, error } = useTuner({
    noiseGateDb,
    confidenceThreshold,
    instrument,
    emaAlpha,
    hysteresisFrames,
    hpfCutoffHz,
    minFrequency,
    maxFrequency,
    a4,
    onsetDetection,
    quality,
    overlapRatio,
  });

  return (
    <SafeAreaProvider>

      <SafeAreaView style={styles.container} edges={['top', 'left', 'right']}>
        {/* Tab Bar */}
        <View style={styles.tabBar}>
          <TouchableOpacity
            style={[styles.tabBtn, tab === 'tuner' && styles.tabBtnActive]}
            onPress={() => setTab('tuner')}
          >
            <Text style={[styles.tabText, tab === 'tuner' && styles.tabTextActive]}>Tuner</Text>
          </TouchableOpacity>
          <TouchableOpacity
            style={[styles.tabBtn, tab === 'settings' && styles.tabBtnActive]}
            onPress={() => setTab('settings')}
          >
            <Text style={[styles.tabText, tab === 'settings' && styles.tabTextActive]}>
              Settings
            </Text>
          </TouchableOpacity>
        </View>

        {tab === 'tuner' ? (
          <TunerView
            latest={latest}
            isRunning={isRunning}
            error={error}
            onStart={start}
            onStop={stop}
          />
        ) : (
          <SettingsView
            instrument={instrument}
            a4={a4}
            noiseGateDb={noiseGateDb}
            confidenceThreshold={confidenceThreshold}
            emaAlpha={emaAlpha}
            hysteresisFrames={hysteresisFrames}
            hpfCutoffHz={hpfCutoffHz}
            minFrequency={minFrequency}
            maxFrequency={maxFrequency}
            onsetDetection={onsetDetection}
            onInstrumentChange={setInstrument}
            onA4Change={setA4}
            onNoiseGateDbChange={setNoiseGateDb}
            onConfidenceThresholdChange={setConfidenceThreshold}
            onEmaAlphaChange={setEmaAlpha}
            onHysteresisFramesChange={setHysteresisFrames}
            onHpfCutoffHzChange={setHpfCutoffHz}
            onMinFrequencyChange={setMinFrequency}
            onMaxFrequencyChange={setMaxFrequency}
            onOnsetDetectionChange={setOnsetDetection}
            quality={quality}
            overlapRatio={overlapRatio}
            onQualityChange={setQuality}
            onOverlapRatioChange={setOverlapRatio}
          />
        )}
      </SafeAreaView>
    </SafeAreaProvider>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#0f0f0f',
  },
  tabBar: {
    flexDirection: 'row',
    paddingTop: 8,
    paddingHorizontal: 24,
    gap: 12,
  },
  tabBtn: {
    paddingVertical: 8,
    paddingHorizontal: 20,
    borderRadius: 20,
    backgroundColor: '#1a1a1a',
  },
  tabBtnActive: {
    backgroundColor: '#27ae60',
  },
  tabText: {
    color: '#666',
    fontSize: 14,
    fontWeight: '600',
  },
  tabTextActive: {
    color: '#fff',
  },
});
