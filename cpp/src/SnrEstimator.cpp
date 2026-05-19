#include "SnrEstimator.hpp"

#include <algorithm>
#include <cmath>

static constexpr float kMinLinear = 1e-7f; // -140 dBFS floor

SnrEstimator::SnrEstimator(float floorInitDb)
    : noiseFloorLinear_(std::pow(10.0f, floorInitDb / 20.0f)) {}

float SnrEstimator::update(float rmsLinear) {
    rmsLinear = std::max(rmsLinear, kMinLinear);

    if (rmsLinear < noiseFloorLinear_) {
        // Signal dropped below floor — pull floor down quickly
        noiseFloorLinear_ = kAttackAlpha * rmsLinear + (1.0f - kAttackAlpha) * noiseFloorLinear_;
    } else {
        // Signal above floor — let floor decay slowly upward (tracks long-term quiet level)
        noiseFloorLinear_ = kDecayAlpha * noiseFloorLinear_ + (1.0f - kDecayAlpha) * rmsLinear;
    }

    noiseFloorLinear_ = std::max(noiseFloorLinear_, kMinLinear);
    return 20.0f * std::log10(rmsLinear / noiseFloorLinear_);
}

float SnrEstimator::snrToWeight(float snrDb) {
    // Sigmoid-like mapping: 0 dB SNR → ~0.0 weight, 20 dB → ~1.0 weight
    if (snrDb <= 0.0f)  return 0.0f;
    if (snrDb >= 30.0f) return 1.0f;
    return snrDb / 30.0f;
}

void SnrEstimator::reset() {
    noiseFloorLinear_ = std::pow(10.0f, -70.0f / 20.0f);
}
