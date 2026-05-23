#include "BiquadHpf.hpp"

#include <cmath>

static constexpr float kPi = 3.14159265358979323846f;

BiquadHpf::BiquadHpf(float sampleRate, float cutoffHz, float q) {
    // Audio EQ Cookbook — High Pass Filter
    const float w0    = 2.0f * kPi * cutoffHz / sampleRate;
    const float cosW0 = std::cos(w0);
    const float alpha = std::sin(w0) / (2.0f * q);
    const float a0    = 1.0f + alpha;

    b0_ =  (1.0f + cosW0) / 2.0f / a0;
    b1_ = -(1.0f + cosW0)        / a0;
    b2_ =  (1.0f + cosW0) / 2.0f / a0;
    a1_ = -2.0f * cosW0          / a0;
    a2_ =  (1.0f - alpha)        / a0;
}

void BiquadHpf::process(float* frame, int n) {
    if (!frame || n <= 0) return;
    for (int i = 0; i < n; ++i) {
        const float x = frame[i];
        const float y = b0_ * x + w1_;
        w1_ = b1_ * x - a1_ * y + w2_;
        w2_ = b2_ * x - a2_ * y;
        frame[i] = y;
    }
}

void BiquadHpf::reset() {
    w1_ = 0.0f;
    w2_ = 0.0f;
}
