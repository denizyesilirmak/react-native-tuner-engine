import React from 'react';
import { StyleSheet, Text, TouchableOpacity, View } from 'react-native';
import type { PitchEvent } from 'react-native-tuner-engine';
import { Needle } from './Needle';
import { centsColor } from '../utils';

type TunerViewProps = Readonly<{
  latest: PitchEvent | null;
  isRunning: boolean;
  error: Error | null;
  onStart: () => void;
  onStop: () => void;
}>;

export function TunerView({
  latest,
  isRunning,
  error,
  onStart,
  onStop,
}: TunerViewProps) {
  const cents = latest?.hasPitch ? latest.cents : 0;
  const color = latest?.hasPitch ? centsColor(cents) : '#888';

  const statusText = error ? error.message : isRunning ? 'listening…' : '—';
  const centsText = latest?.hasPitch
    ? `${cents >= 0 ? '+' : ''}${cents.toFixed(1)} ¢`
    : '';

  return (
    <View style={styles.tunerContent}>
      {/* Note display */}
      <View style={styles.noteArea}>
        {latest?.hasPitch ? (
          <>
            <Text style={[styles.note, { color }]}>{latest.noteName}</Text>
            <Text style={styles.octave}>{latest.octave}</Text>
          </>
        ) : (
          <Text style={styles.noSignal}>{statusText}</Text>
        )}
      </View>

      <Needle cents={cents} />

      <Text style={[styles.centsLabel, { color }]}>{centsText}</Text>

      {/* Nearest string */}
      <View style={styles.stringRow}>
        {latest?.hasPitch && latest.nearestString ? (
          <>
            <Text style={styles.stringName}>{latest.nearestString}</Text>
            <Text
              style={[
                styles.stringDeviation,
                { color: centsColor(latest.stringDeviation) },
              ]}
            >
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
        onPress={isRunning ? onStop : onStart}
        activeOpacity={0.8}
      >
        <Text style={styles.buttonText}>{isRunning ? 'Stop' : 'Start'}</Text>
      </TouchableOpacity>
    </View>
  );
}

const styles = StyleSheet.create({
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
  octave: {
    color: '#555',
    fontSize: 36,
    fontWeight: '400',
    marginBottom: 10,
    marginLeft: 4,
  },
  noSignal: { color: '#444', fontSize: 32, lineHeight: 110 },
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
});
