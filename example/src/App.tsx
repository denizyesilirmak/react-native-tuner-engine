import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  FlatList,
  SafeAreaView,
  StyleSheet,
  Text,
  TouchableOpacity,
  View,
} from 'react-native';
import {
  type PitchEvent,
  onPitch,
  requestPermission,
  start,
  stop,
} from 'react-native-tuner-engine';

type LogEntry = PitchEvent & { id: number };

export default function App() {
  const [isRunning, setIsRunning] = useState(false);
  const [latest, setLatest] = useState<PitchEvent | null>(null);
  const [log, setLog] = useState<LogEntry[]>([]);
  const counter = useRef(0);

  useEffect(() => {
    const sub = onPitch((event) => {
      setLatest(event);
      if (event.hasPitch) {
        setLog((prev) => [
          { ...event, id: counter.current++ },
          ...prev.slice(0, 49),
        ]);
      }
    });
    return () => sub.remove();
  }, []);

  const handleToggle = useCallback(async () => {
    if (isRunning) {
      await stop();
      setIsRunning(false);
    } else {
      const granted = await requestPermission();
      if (!granted) return;
      await start();
      setIsRunning(true);
    }
  }, [isRunning]);

  return (
    <SafeAreaView style={styles.container}>
      <Text style={styles.title}>TunerEngine</Text>

      {latest?.hasPitch ? (
        <View style={styles.pitchCard}>
          <Text style={styles.note}>
            {latest.noteName}
            {latest.octave}
          </Text>
          <Text style={styles.cents}>
            {latest.cents >= 0 ? '+' : ''}
            {latest.cents.toFixed(1)} ¢
          </Text>
          <Text style={styles.freq}>{latest.frequency.toFixed(2)} Hz</Text>
          <Text style={styles.meta}>
            confidence: {(latest.confidence * 100).toFixed(0)}% · rms:{' '}
            {latest.rmsDb.toFixed(1)} dB
          </Text>
        </View>
      ) : (
        <View style={styles.pitchCard}>
          <Text style={styles.noSignal}>— no signal —</Text>
        </View>
      )}

      <TouchableOpacity
        style={[styles.button, isRunning && styles.buttonStop]}
        onPress={handleToggle}
      >
        <Text style={styles.buttonText}>{isRunning ? 'Stop' : 'Start'}</Text>
      </TouchableOpacity>

      <FlatList
        style={styles.log}
        data={log}
        keyExtractor={(item) => String(item.id)}
        renderItem={({ item }) => (
          <Text style={styles.logRow}>
            {item.noteName}
            {item.octave} {item.cents >= 0 ? '+' : ''}
            {item.cents.toFixed(1)} ¢ · {item.frequency.toFixed(1)} Hz
          </Text>
        )}
      />
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: '#111', alignItems: 'center' },
  title: { color: '#fff', fontSize: 20, fontWeight: '600', marginTop: 16 },
  pitchCard: {
    alignItems: 'center',
    justifyContent: 'center',
    height: 180,
    marginVertical: 24,
  },
  note: { color: '#fff', fontSize: 80, fontWeight: '700', lineHeight: 88 },
  cents: { color: '#aef', fontSize: 28, marginTop: 4 },
  freq: { color: '#888', fontSize: 16, marginTop: 4 },
  meta: { color: '#555', fontSize: 12, marginTop: 4 },
  noSignal: { color: '#444', fontSize: 28 },
  button: {
    backgroundColor: '#27ae60',
    paddingHorizontal: 48,
    paddingVertical: 14,
    borderRadius: 32,
    marginBottom: 16,
  },
  buttonStop: { backgroundColor: '#c0392b' },
  buttonText: { color: '#fff', fontSize: 18, fontWeight: '600' },
  log: { width: '100%', paddingHorizontal: 16 },
  logRow: { color: '#556', fontSize: 13, paddingVertical: 2 },
});
