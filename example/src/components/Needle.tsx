import React from 'react';
import { Animated, StyleSheet, View } from 'react-native';
import { centsColor } from '../utils';

const NEEDLE_RANGE = 50;
const TRACK_WIDTH = 280;

export function Needle({ cents }: Readonly<{ cents: number }>) {
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
            {
              left:
                TRACK_WIDTH / 2 + (t / NEEDLE_RANGE) * (TRACK_WIDTH / 2) - 1,
            },
            t === 0 && styles.tickCenter,
          ]}
        />
      ))}
      <Animated.View
        style={[
          styles.needleHead,
          { transform: [{ translateX }], backgroundColor: color },
        ]}
      />
    </View>
  );
}

const styles = StyleSheet.create({
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
});
