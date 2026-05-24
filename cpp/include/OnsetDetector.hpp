#pragma once

// Lightweight onset detector based on energy rise.
//
// Computes the difference between the current frame's RMS and a slow-decay
// envelope.  When the rise exceeds a threshold, an onset is signalled.
//
// Cost: one subtraction + one comparison per frame when enabled.
// When disabled (`enabled_ == false`), detect() returns false immediately — zero work.
class OnsetDetector {
public:
    struct Config {
        float thresholdDb   = 6.0f;   // minimum dB rise to trigger an onset
        float envelopeAlpha = 0.15f;  // envelope decay speed (lower = slower)
        int   cooldownFrames = 8;     // minimum frames between consecutive onsets
    };

    OnsetDetector() = default;
    explicit OnsetDetector(Config cfg);

    // Returns true if an onset is detected for the current frame.
    // rmsDb: current frame's RMS in dBFS (already computed by Pipeline — free).
    bool detect(float rmsDb);

    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    void setConfig(Config cfg);
    void reset();

private:
    Config cfg_{};
    bool   enabled_ = false;  // disabled by default — zero cost

    float  envelopeDb_ = -100.0f;
    int    cooldown_   = 0;
};
