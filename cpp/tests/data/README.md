# Test data

`strat_Asharp3_frames.f32` — three consecutive-ish 2048-sample frames (float32,
little-endian, mono, 44.1 kHz) cut from a recorded Stratocaster A#3
(`string-3-Ax-as-Ax3.wav`, 1.5 s, 2.5 s and 3.5 s into the note) from
https://github.com/cluesurf/wavebase (CC0 / public domain).

The 2nd harmonic of this string makes pYIN's threshold prior crown the T/2 dip,
so the old detector reported 469 Hz (one octave up) with confidence > 0.9.
