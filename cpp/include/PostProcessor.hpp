#pragma once

// Stabilises pitch output via:
//   1. Median-5 filter on frequency (kills transient spikes)
//   2. EMA smoothing on the median frequency
//   3. Note-change hysteresis (debounce rapid note switches)
class PostProcessor {
public:
    struct Config {
        float emaAlpha         = 0.35f; // smoothing speed (higher = faster response)
        int   hysteresisFrames = 3;     // consecutive frames needed to confirm a new note
    };

    PostProcessor();
    explicit PostProcessor(Config cfg);

    struct Result {
        float frequency  = 0.0f;
        int   midiNote   = -1;   // -1 = no stable note yet
        float cents      = 0.0f; // cents deviation from locked note
        bool  isStable   = false;
    };

    // Call once per detected frame. frequency = 0 means "no pitch".
    Result process(float frequency, float confidence);

    void reset();
    void setConfig(Config cfg);

private:
    Config cfg_;

    // Median-5 circular buffer
    static constexpr int kMedianLen = 5;
    float medianBuf_[kMedianLen] = {};
    int   medianIdx_ = 0;
    int   medianFill_ = 0;

    float median5() const;

    // EMA state
    float smoothedFreq_ = 0.0f;

    // Hysteresis state
    int  lockedMidi_     = -1;
    int  candidateMidi_  = -1;
    int  candidateCount_ = 0;

    static int freqToMidi(float hz);
    static float freqToCentsFromMidi(float hz, int midi);
};
