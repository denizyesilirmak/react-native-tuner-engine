export type Instrument =
  | 'guitar'
  | 'bass'
  | 'violin'
  | 'viola'
  | 'cello'
  | 'ukulele'
  | 'mandolin'
  | 'banjo'
  | 'chromatic';

export type Temperament = 'equal' | 'just';

/**
 * Quality preset that maps to a frame size + overlap combination.
 * - low-latency: frameSize=1024, overlap=0 — fastest response, less accuracy on low notes
 * - balanced: frameSize=2048, overlap=0.5 — good for most instruments
 * - high-accuracy: frameSize=4096, overlap=0.75 — best for bass/cello, ~21ms update rate
 */
export type QualityPreset = 'low-latency' | 'balanced' | 'high-accuracy';

export type TunerConfig = {
  /** Audio sample rate in Hz. Default: 48000 */
  sampleRate?: number;
  /** DSP frame size in samples. Default: 2048 */
  frameSize?: number;
  /** Noise gate threshold in dBFS. Frames quieter than this are ignored. Default: -55 */
  noiseGateDb?: number;
  /** Minimum detector confidence (0–1) to emit a pitch event. Default: 0.75 */
  confidenceThreshold?: number;
  /** Lowest frequency to detect in Hz. Default: 60 */
  minFrequency?: number;
  /** Highest frequency to detect in Hz. Default: 1200 */
  maxFrequency?: number;
  /** A4 reference pitch in Hz. Default: 440 */
  a4?: number;
  /**
   * PostProcessor EMA smoothing factor (0.05–1.0). Lower = smoother/slower,
   * higher = more responsive but jittery. Default: 0.35
   */
  emaAlpha?: number;
  /**
   * Consecutive frames required to confirm a note change (1–10).
   * Higher = more stable note display, lower = faster switching. Default: 3
   */
  hysteresisFrames?: number;
  /**
   * High-pass filter cutoff in Hz (20–300). Cuts rumble below this frequency.
   * Lower for bass guitar (e.g. 30), higher for violin (e.g. 100). Default: 70
   */
  hpfCutoffHz?: number;
  /**
   * Enable onset detection. When enabled, the PostProcessor resets on note
   * attacks for faster response. Minimal CPU cost (one comparison per frame).
   * Default: false
   */
  onsetDetection?: boolean;
  /**
   * Fraction of the frame that overlaps with the previous frame (0.0–0.75).
   * Higher overlap gives more frequent pitch updates at the cost of CPU.
   * - 0.0: no overlap (default, backward-compatible)
   * - 0.5: 50% overlap (2x update rate)
   * - 0.75: 75% overlap (4x update rate)
   * Default: 0
   */
  overlapRatio?: number;
  /**
   * When true, frame size is automatically selected based on the configured instrument.
   * Bass and cello use 4096 for better low-frequency resolution; others use 2048.
   * Overridden if frameSize is explicitly provided.
   * Default: true
   */
  adaptiveFrameSize?: boolean;
  /**
   * Quality preset that configures frameSize and overlapRatio together.
   * Overrides individual frameSize/overlapRatio if provided.
   */
  quality?: QualityPreset;
};

export type TuningPreset =
  | 'guitar_standard'
  | 'guitar_eb_standard'
  | 'guitar_d_standard'
  | 'guitar_drop_d'
  | 'guitar_drop_c'
  | 'guitar_open_g'
  | 'guitar_open_d'
  | 'guitar_open_c'
  | 'guitar_dadgad'
  | 'bass_standard'
  | 'bass_drop_d'
  | 'violin_standard'
  | 'viola_standard'
  | 'cello_standard'
  | 'ukulele_standard';

export type PitchEvent = {
  hasPitch: boolean;
  frequency: number;
  confidence: number;
  rmsDb: number;
  noteName: string;
  octave: number;
  cents: number;
  /** Nearest string in the active tuning, e.g. "E2". Empty string when no tuning is set. */
  nearestString: string;
  /** Cents deviation from that string's target frequency (negative = flat). */
  stringDeviation: number;
};

export type EngineStatus = {
  isRunning: boolean;
  engineReady: boolean;
  /** Monotonic counter incremented on each pitch event. Use to detect new data when polling. */
  seq: number;
  hasPitch: boolean;
  frequency: number;
  confidence: number;
  rmsDb: number;
  noteName: string;
  octave: number;
  cents: number;
  nearestString: string;
  stringDeviation: number;
};
