#include "OnsetDetector.hpp"

#include <algorithm>

OnsetDetector::OnsetDetector(Config cfg) : cfg_(cfg) {}

bool OnsetDetector::detect(float rmsDb) {
    // Fast path: disabled → no work at all.
    if (!enabled_) return false;

    // Decrement cooldown counter
    if (cooldown_ > 0) {
        --cooldown_;
        // Still update envelope during cooldown so we don't miss the next onset
        envelopeDb_ += cfg_.envelopeAlpha * (rmsDb - envelopeDb_);
        return false;
    }

    const float rise = rmsDb - envelopeDb_;

    // Update envelope (exponential follower — tracks slow changes)
    envelopeDb_ += cfg_.envelopeAlpha * (rmsDb - envelopeDb_);

    if (rise > cfg_.thresholdDb) {
        cooldown_ = cfg_.cooldownFrames;
        return true;
    }

    return false;
}

void OnsetDetector::setConfig(Config cfg) {
    cfg_ = cfg;
}

void OnsetDetector::reset() {
    envelopeDb_ = -100.0f;
    cooldown_ = 0;
}
