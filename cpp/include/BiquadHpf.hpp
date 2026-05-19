#pragma once

// 2nd-order high-pass filter, Direct Form II Transposed.
// Coefficients from Audio EQ Cookbook (Bristow-Johnson).
// State (w1_, w2_) persists across frames for continuity.
class BiquadHpf {
public:
    // cutoffHz: -3 dB frequency; q: 0.707 = Butterworth (maximally flat)
    BiquadHpf(float sampleRate, float cutoffHz, float q = 0.7071f);

    // Filter frame in-place.
    void process(float* frame, int n);

    // Reset inter-frame state (call when stream restarts).
    void reset();

private:
    float b0_, b1_, b2_;
    float a1_, a2_;
    float w1_ = 0.0f;
    float w2_ = 0.0f;
};
