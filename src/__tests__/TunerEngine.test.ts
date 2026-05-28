import { beforeEach, describe, expect, it, jest } from '@jest/globals';
import { DeviceEventEmitter } from 'react-native';
import TunerEngine from '../TunerEngine';
import NativeTunerEngine from '../NativeTunerEngine';

jest.mock('../NativeTunerEngine', () => ({
  __esModule: true,
  default: {
    configure: jest.fn<() => Promise<void>>().mockResolvedValue(undefined),
    start: jest.fn<() => Promise<void>>().mockResolvedValue(undefined),
    stop: jest.fn<() => Promise<void>>().mockResolvedValue(undefined),
    requestPermission: jest
      .fn<() => Promise<boolean>>()
      .mockResolvedValue(true),
    setA4: jest.fn(),
    setInstrument: jest.fn(),
    setTemperament: jest.fn(),
    setTuning: jest.fn(),
    getStatus: jest
      .fn()
      .mockReturnValue({ isRunning: false, engineReady: true }),
    addListener: jest.fn(),
    removeListeners: jest.fn(),
  },
}));

const native = NativeTunerEngine as jest.Mocked<typeof NativeTunerEngine>;

const makePitchEvent = (overrides = {}) => ({
  hasPitch: true,
  frequency: 440,
  confidence: 0.95,
  rmsDb: -20,
  noteName: 'A',
  octave: 4,
  cents: 0,
  ...overrides,
});

beforeEach(() => {
  jest.clearAllMocks();
  delete (globalThis as any).__tunerEngineOnPitch;
});

describe('TunerEngine', () => {
  it('calls configure with provided options', async () => {
    await TunerEngine.configure({ sampleRate: 44100, noiseGateDb: -50 });
    expect(native.configure).toHaveBeenCalledWith(
      expect.objectContaining({
        sampleRate: 44100,
        noiseGateDb: -50,
      })
    );
  });

  it('calls start and stop', async () => {
    await TunerEngine.start();
    expect(native.start).toHaveBeenCalledTimes(1);
    await TunerEngine.stop();
    expect(native.stop).toHaveBeenCalledTimes(1);
  });

  it('returns false when permission is denied', async () => {
    (
      native.requestPermission as jest.Mock<() => Promise<boolean>>
    ).mockResolvedValueOnce(false);
    const result = await TunerEngine.requestPermission();
    expect(result).toBe(false);
  });

  it('sets A4 reference frequency', () => {
    TunerEngine.setA4(432);
    expect(native.setA4).toHaveBeenCalledWith(432);
  });

  it('sets instrument', () => {
    TunerEngine.setInstrument('guitar');
    expect(native.setInstrument).toHaveBeenCalledWith('guitar');
  });

  it('sets temperament', () => {
    TunerEngine.setTemperament('just');
    expect(native.setTemperament).toHaveBeenCalledWith('just');
  });

  it('returns engine status', () => {
    const status = TunerEngine.getStatus();
    expect(status).toEqual({ isRunning: false, engineReady: true });
  });

  it('onPitch registers the JSI global and a DeviceEventEmitter listener', () => {
    const cb = jest.fn();
    const unsub = TunerEngine.onPitch(cb);

    expect((globalThis as any).__tunerEngineOnPitch).toBe(cb);

    const event = makePitchEvent();
    DeviceEventEmitter.emit('onPitch', event);
    expect(cb).toHaveBeenCalledWith(event);

    unsub();
    expect((globalThis as any).__tunerEngineOnPitch).toBeUndefined();
  });

  it('unsubscribing stops DeviceEventEmitter delivery', () => {
    const cb = jest.fn();
    const unsub = TunerEngine.onPitch(cb);
    unsub();

    DeviceEventEmitter.emit('onPitch', makePitchEvent());
    expect(cb).not.toHaveBeenCalled();
  });

  it('delivers hasPitch=false events', () => {
    const cb = jest.fn();
    const unsub = TunerEngine.onPitch(cb);
    const event = makePitchEvent({ hasPitch: false, frequency: 0 });
    DeviceEventEmitter.emit('onPitch', event);
    expect(cb).toHaveBeenCalledWith(event);
    unsub();
  });
});
