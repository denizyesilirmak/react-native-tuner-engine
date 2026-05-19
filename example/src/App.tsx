import React from 'react';
import {
  Animated,
  SafeAreaView,
  StyleSheet,
  Text,
  TouchableOpacity,
  View,
} from 'react-native';
import { useTuner } from 'react-native-tuner-engine';

const NEEDLE_RANGE = 50; // cents max
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
      {/* tick marks */}
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
      {/* needle */}
      <Animated.View
        style={[styles.needleHead, { transform: [{ translateX }], backgroundColor: color }]}
      />
    </View>
  );
}

export default function App() {
  const { start, stop, latest, isRunning, error } = useTuner({
    noiseGateDb: -50,
    confidenceThreshold: 0.75,
  });

  const cents = latest?.hasPitch ? latest.cents : 0;
  const color = latest?.hasPitch ? centsColor(cents) : '#888';

  return (
    <SafeAreaView style={styles.container}>
      <Text style={styles.title}>TunerEngine</Text>

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

      {/* Needle */}
      <Needle cents={cents} />

      {/* Cents label */}
      <Text style={[styles.centsLabel, { color }]}>
        {latest?.hasPitch
          ? `${cents >= 0 ? '+' : ''}${cents.toFixed(1)} ¢`
          : ''}
      </Text>

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
      </View>

      {/* Start / Stop */}
      <TouchableOpacity
        style={[styles.button, isRunning && styles.buttonStop]}
        onPress={isRunning ? stop : start}
        activeOpacity={0.8}
      >
        <Text style={styles.buttonText}>{isRunning ? 'Stop' : 'Start'}</Text>
      </TouchableOpacity>
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#0f0f0f',
    alignItems: 'center',
    justifyContent: 'center',
  },
  title: {
    position: 'absolute',
    top: 56,
    color: '#555',
    fontSize: 13,
    fontWeight: '600',
    letterSpacing: 2,
    textTransform: 'uppercase',
  },

  // note
  noteArea: {
    flexDirection: 'row',
    alignItems: 'flex-end',
    height: 120,
    marginBottom: 32,
  },
  note: { fontSize: 100, fontWeight: '700', lineHeight: 110 },
  octave: { color: '#555', fontSize: 36, fontWeight: '400', marginBottom: 10, marginLeft: 4 },
  noSignal: { color: '#444', fontSize: 32, lineHeight: 110 },

  // needle
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

  // labels
  centsLabel: {
    fontSize: 22,
    fontWeight: '600',
    marginTop: 12,
    height: 28,
  },
  metaRow: {
    flexDirection: 'row',
    gap: 20,
    marginTop: 8,
    marginBottom: 48,
    height: 20,
  },
  meta: { color: '#444', fontSize: 13 },

  // button
  button: {
    backgroundColor: '#27ae60',
    paddingHorizontal: 56,
    paddingVertical: 16,
    borderRadius: 36,
  },
  buttonStop: { backgroundColor: '#c0392b' },
  buttonText: { color: '#fff', fontSize: 18, fontWeight: '600' },
});
