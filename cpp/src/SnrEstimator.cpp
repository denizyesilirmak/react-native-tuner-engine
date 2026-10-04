#include "SnrEstimator.hpp"

#include <algorithm>
#include <cmath>

static constexpr float kMinLinear = 1e-7f; // -140 dBFS floor

// SNR at which the weight starts above zero, and where it reaches full trust.
static constexpr float kWeightZeroSnrDb = 3.0f;
static constexpr float kWeightFullSnrDb = 12.0f;

SnrEstimator::SnrEstimator(float floorInitDb)
    : noiseFloorLinear_(std::pow(10.0f, floorInitDb / 20.0f)) {}

float SnrEstimator::update(float rmsLinear, bool voiced) {
    rmsLinear = std::max(rmsLinear, kMinLinear);

    if (rmsLinear < noiseFloorLinear_) {
        // Quieter than the floor — follow it down quickly to track silence.
        noiseFloorLinear_ = kFallAlpha * rmsLinear + (1.0f - kFallAlpha) * noiseFloorLinear_;
    } else {
        // Louder — climb toward it, but never past it. Unpitched frames are
        // most likely noise; pitched ones are the note we're measuring.
        const float riseDb = voiced ? kRiseVoicedDb : kRiseUnvoicedDb;
        noiseFloorLinear_ = std::min(rmsLinear, noiseFloorLinear_ * std::pow(10.0f, riseDb / 20.0f));
    }

    noiseFloorLinear_ = std::max(noiseFloorLinear_, kMinLinear);
    return 20.0f * std::log10(rmsLinear / noiseFloorLinear_);
}

float SnrEstimator::snrToWeight(float snrDb) {
    // Linear ramp: 3 dB SNR → 0, 12 dB → 1. A pitch 12 dB over the room is
    // clean enough to trust fully; the detectors handle the rest.
    if (snrDb <= kWeightZeroSnrDb) return 0.0f;
    if (snrDb >= kWeightFullSnrDb) return 1.0f;
    return (snrDb - kWeightZeroSnrDb) / (kWeightFullSnrDb - kWeightZeroSnrDb);
}

void SnrEstimator::reset() {
    noiseFloorLinear_ = std::pow(10.0f, kFloorInitDb / 20.0f);
}
