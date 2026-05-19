import React from 'react';
import {
  FlatList,
  SafeAreaView,
  StyleSheet,
  Text,
  TouchableOpacity,
  View,
} from 'react-native';
import { useTuner, type PitchEvent } from 'react-native-tuner-engine';

type LogEntry = PitchEvent & { id: number };

let logCounter = 0;

export default function App() {
  const { start, stop, latest, isRunning, error } = useTuner({
    noiseGateDb: -50,
    confidenceThreshold: 0.75,
  });

  const [log, setLog] = React.useState<LogEntry[]>([]);

  React.useEffect(() => {
    if (latest?.hasPitch) {
      setLog((prev) => [
        { ...latest, id: logCounter++ },
        ...prev.slice(0, 49),
      ]);
    }
  }, [latest]);

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
          <Text style={styles.noSignal}>
            {error ? error.message : '— no signal —'}
          </Text>
        </View>
      )}

      <TouchableOpacity
        style={[styles.button, isRunning && styles.buttonStop]}
        onPress={isRunning ? stop : start}
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
  container: { flex: 1, backgroundColor: '#fff', alignItems: 'center' },
  title: { color: '#000', fontSize: 20, fontWeight: '600', marginTop: 16 },
  pitchCard: {
    alignItems: 'center',
    justifyContent: 'center',
    height: 180,
    marginVertical: 24,
  },
  note: { color: '#000', fontSize: 80, fontWeight: '700', lineHeight: 88 },
  cents: { color: '#000', fontSize: 28, marginTop: 4 },
  freq: { color: '#000', fontSize: 16, marginTop: 4 },
  meta: { color: '#000', fontSize: 12, marginTop: 4 },
  noSignal: { color: '#444', fontSize: 28, textAlign: 'center' },
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
