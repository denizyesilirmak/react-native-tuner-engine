#pragma once

// Estimates signal-to-noise ratio against a tracked noise floor.
//
// The floor is learned from every frame, including the ones the noise gate
// rejects: it falls quickly to any quieter frame, climbs steadily through
// frames with no pitch (room noise, fan hum) and only creeps up under pitched
// frames, so a long sustained note doesn't slowly become "noise".
//
// Rates are per frame. At the usual ~47 frames/s (48 kHz, hop 1024) the floor
// can climb ~9 dB/s through unpitched noise and ~0.5 dB/s under a held note.
class SnrEstimator {
public:
    explicit SnrEstimator(float floorInitDb = kFloorInitDb);

    // Update with the current frame's linear RMS and whether the detector heard
    // a pitch in it. Returns the frame's SNR in dB.
    float update(float rmsLinear, bool voiced);

    // Map SNR to a confidence weight in [0, 1].
    static float snrToWeight(float snrDb);

    void reset();

private:
    static constexpr float kFloorInitDb    = -70.0f;
    static constexpr float kFallAlpha      = 0.3f;   // linear EMA toward a quieter frame
    static constexpr float kRiseUnvoicedDb = 0.2f;   // per-frame climb on unpitched frames
    static constexpr float kRiseVoicedDb   = 0.01f;  // per-frame climb under a note

    float noiseFloorLinear_;
};
