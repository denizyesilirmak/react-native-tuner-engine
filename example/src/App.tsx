import React, { useState } from 'react';
import {
  Animated,
  SafeAreaView,
  ScrollView,
  StyleSheet,
  Text,
  TouchableOpacity,
  View,
} from 'react-native';
import Slider from '@react-native-community/slider';
import { useTuner } from 'react-native-tuner-engine';
import type { Instrument } from 'react-native-tuner-engine';

const NEEDLE_RANGE = 50;
const TRACK_WIDTH = 280;

function centsColor(cents: number): string {
  const abs = Math.abs(cents);
  if (abs <= 5) return '#27ae60';
  if (abs <= 15) return '#f39c12';
  return '#c0392b';
}

function Needle({ cents }: { cents: number }) {
  const animCents = React.useRef(new Animated.Value(0)).current;

  React.useEffect(() => {
    Animated.spring(animCents, {
      toValue: cents,
      useNativeDriver: true,
      tension: 120,
      friction: 10,
    }).start();
  }, [animCents, cents]);

  const translateX = animCents.interpolate({
    inputRange: [-NEEDLE_RANGE, NEEDLE_RANGE],
    outputRange: [-TRACK_WIDTH / 2, TRACK_WIDTH / 2],
    extrapolate: 'clamp',
  });

  const color = centsColor(cents);

  return (
    <View style={styles.needleTrack}>
      {[-40, -20, 0, 20, 40].map((t) => (
        <View
          key={t}
          style={[
            styles.tick,
            { left: TRACK_WIDTH / 2 + (t / NEEDLE_RANGE) * (TRACK_WIDTH / 2) - 1 },
            t === 0 && styles.tickCenter,
          ]}
        />
      ))}
      <Animated.View
        style={[styles.needleHead, { transform: [{ translateX }], backgroundColor: color }]}
      />
    </View>
  );
}

// ─── Settings Slider Row ─────────────────────────────────────────────────────
function SettingRow({
  label,
  value,
  min,
  max,
  step,
  unit,
  onChange,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  unit?: string;
  onChange: (v: number) => void;
}) {
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

// ─── Instrument Picker ───────────────────────────────────────────────────────
const INSTRUMENTS: Instrument[] = [
  'guitar', 'bass', 'violin', 'viola', 'cello', 'ukulele', 'mandolin', 'banjo', 'chromatic',
];

function InstrumentPicker({
  value,
  onChange,
}: {
  value: Instrument;
  onChange: (v: Instrument) => void;
}) {
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
            <Text style={[styles.chipText, value === inst && styles.chipTextActive]}>
              {inst}
            </Text>
          </TouchableOpacity>
        ))}
      </View>
    </View>
  );
}

// ─── Main App ────────────────────────────────────────────────────────────────
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
  });

  const cents = latest?.hasPitch ? latest.cents : 0;
  const color = latest?.hasPitch ? centsColor(cents) : '#888';

  return (
    <SafeAreaView style={styles.container}>
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
          <Text style={[styles.tabText, tab === 'settings' && styles.tabTextActive]}>Settings</Text>
        </TouchableOpacity>
      </View>

      {tab === 'tuner' ? (
        <View style={styles.tunerContent}>
          {/* Note display */}
          <View style={styles.noteArea}>
            {latest?.hasPitch ? (
              <>
                <Text style={[styles.note, { color }]}>{latest.noteName}</Text>
                <Text style={styles.octave}>{latest.octave}</Text>
              </>
            ) : (
              <Text style={styles.noSignal}>
                {error ? error.message : isRunning ? 'listening…' : '—'}
              </Text>
            )}
          </View>

          <Needle cents={cents} />

          <Text style={[styles.centsLabel, { color }]}>
            {latest?.hasPitch
              ? `${cents >= 0 ? '+' : ''}${cents.toFixed(1)} ¢`
              : ''}
          </Text>

          {/* Nearest string */}
          <View style={styles.stringRow}>
            {latest?.hasPitch && latest.nearestString ? (
              <>
                <Text style={styles.stringName}>{latest.nearestString}</Text>
                <Text style={[styles.stringDeviation, { color: centsColor(latest.stringDeviation) }]}>
                  {latest.stringDeviation >= 0 ? '+' : ''}
                  {latest.stringDeviation.toFixed(1)} ¢
                </Text>
              </>
            ) : (
              <Text style={styles.stringPlaceholder}>— ¢</Text>
            )}
          </View>

          {/* Frequency + confidence */}
          <View style={styles.metaRow}>
            <Text style={styles.meta}>
              {latest?.hasPitch ? `${latest.frequency.toFixed(1)} Hz` : ''}
            </Text>
            <Text style={styles.meta}>
              {latest?.hasPitch
                ? `conf ${(latest.confidence * 100).toFixed(0)}%`
                : ''}
            </Text>
            <Text style={styles.meta}>
              {latest ? `${latest.rmsDb.toFixed(0)} dB` : ''}
            </Text>
          </View>

          {/* Start / Stop */}
          <TouchableOpacity
            style={[styles.button, isRunning && styles.buttonStop]}
            onPress={isRunning ? stop : start}
            activeOpacity={0.8}
          >
            <Text style={styles.buttonText}>{isRunning ? 'Stop' : 'Start'}</Text>
          </TouchableOpacity>
        </View>
      ) : (
        <ScrollView style={styles.settingsScroll} contentContainerStyle={styles.settingsContent}>
          <InstrumentPicker value={instrument} onChange={setInstrument} />

          <SettingRow
            label="A4 Reference"
            value={a4}
            min={420}
            max={460}
            step={1}
            unit="Hz"
            onChange={setA4}
          />
          <SettingRow
            label="Noise Gate"
            value={noiseGateDb}
            min={-80}
            max={-20}
            step={1}
            unit="dB"
            onChange={setNoiseGateDb}
          />
          <SettingRow
            label="Confidence Threshold"
            value={confidenceThreshold}
            min={0.1}
            max={0.99}
            step={0.05}
            onChange={setConfidenceThreshold}
          />
          <SettingRow
            label="EMA Alpha"
            value={emaAlpha}
            min={0.05}
            max={1.0}
            step={0.05}
            onChange={setEmaAlpha}
          />
          <SettingRow
            label="Hysteresis Frames"
            value={hysteresisFrames}
            min={1}
            max={10}
            step={1}
            onChange={setHysteresisFrames}
          />
          <SettingRow
            label="HPF Cutoff"
            value={hpfCutoffHz}
            min={20}
            max={300}
            step={5}
            unit="Hz"
            onChange={setHpfCutoffHz}
          />
          <SettingRow
            label="Min Frequency"
            value={minFrequency}
            min={20}
            max={200}
            step={5}
            unit="Hz"
            onChange={setMinFrequency}
          />
          <SettingRow
            label="Max Frequency"
            value={maxFrequency}
            min={500}
            max={4000}
            step={50}
            unit="Hz"
            onChange={setMaxFrequency}
          />

          <Text style={styles.hint}>
            Changes apply on next Start. Stop → tweak → Start to test.
          </Text>
        </ScrollView>
      )}
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#0f0f0f',
  },

  // tab bar
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

  // tuner
  tunerContent: {
    flex: 1,
    alignItems: 'center',
    justifyContent: 'center',
  },
  noteArea: {
    flexDirection: 'row',
    alignItems: 'flex-end',
    height: 120,
    marginBottom: 32,
  },
  note: { fontSize: 100, fontWeight: '700', lineHeight: 110 },
  octave: { color: '#555', fontSize: 36, fontWeight: '400', marginBottom: 10, marginLeft: 4 },
  noSignal: { color: '#444', fontSize: 32, lineHeight: 110 },

  needleTrack: {
    width: TRACK_WIDTH,
    height: 40,
    borderRadius: 20,
    backgroundColor: '#1e1e1e',
    justifyContent: 'center',
    alignItems: 'center',
    overflow: 'hidden',
  },
  tick: {
    position: 'absolute',
    width: 1,
    height: 12,
    backgroundColor: '#333',
    top: 14,
  },
  tickCenter: {
    width: 2,
    height: 18,
    top: 11,
    backgroundColor: '#555',
  },
  needleHead: {
    width: 4,
    height: 28,
    borderRadius: 2,
  },
  centsLabel: {
    fontSize: 22,
    fontWeight: '600',
    marginTop: 12,
    height: 28,
  },
  stringRow: {
    flexDirection: 'row',
    alignItems: 'baseline',
    gap: 8,
    marginTop: 16,
    height: 32,
  },
  stringName: {
    color: '#aaa',
    fontSize: 22,
    fontWeight: '600',
  },
  stringDeviation: {
    fontSize: 16,
    fontWeight: '500',
  },
  stringPlaceholder: {
    color: '#333',
    fontSize: 16,
  },
  metaRow: {
    flexDirection: 'row',
    gap: 16,
    marginTop: 8,
    marginBottom: 48,
    height: 20,
  },
  meta: { color: '#444', fontSize: 13 },
  button: {
    backgroundColor: '#27ae60',
    paddingHorizontal: 56,
    paddingVertical: 16,
    borderRadius: 36,
  },
  buttonStop: { backgroundColor: '#c0392b' },
  buttonText: { color: '#fff', fontSize: 18, fontWeight: '600' },

  // settings
  settingsScroll: {
    flex: 1,
  },
  settingsContent: {
    padding: 24,
    paddingBottom: 60,
  },
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
  hint: {
    color: '#555',
    fontSize: 12,
    textAlign: 'center',
    marginTop: 12,
  },
});
