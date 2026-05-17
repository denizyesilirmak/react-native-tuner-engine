#include "PostProcessor.hpp"

#include <algorithm>
#include <array>
#include <cmath>

static constexpr float kA4 = 440.0f;

PostProcessor::PostProcessor() : cfg_{} {}
PostProcessor::PostProcessor(Config cfg) : cfg_(cfg) {}

void PostProcessor::reset() {
    std::fill(medianBuf_, medianBuf_ + kMedianLen, 0.0f);
    medianIdx_      = 0;
    medianFill_     = 0;
    smoothedFreq_   = 0.0f;
    lockedMidi_     = -1;
    candidateMidi_  = -1;
    candidateCount_ = 0;
}

void PostProcessor::setConfig(Config cfg) {
    cfg_ = cfg;
}

float PostProcessor::median5() const {
    std::array<float, kMedianLen> sorted;
    std::copy(medianBuf_, medianBuf_ + kMedianLen, sorted.begin());
    std::sort(sorted.begin(), sorted.end());
    return sorted[kMedianLen / 2];
}

int PostProcessor::freqToMidi(float hz) {
    if (hz <= 0.0f) return -1;
    return static_cast<int>(std::round(69.0f + 12.0f * std::log2(hz / kA4)));
}

float PostProcessor::freqToCentsFromMidi(float hz, int midi) {
    if (hz <= 0.0f || midi < 0) return 0.0f;
    const float targetHz = kA4 * std::pow(2.0f, (midi - 69) / 12.0f);
    return 1200.0f * std::log2(hz / targetHz);
}

PostProcessor::Result PostProcessor::process(float frequency, float confidence) {
    // 1. Feed median buffer (treat 0 as "silent frame")
    medianBuf_[medianIdx_] = frequency;
    medianIdx_ = (medianIdx_ + 1) % kMedianLen;
    if (medianFill_ < kMedianLen) ++medianFill_;

    const float med = (medianFill_ == kMedianLen) ? median5() : frequency;

    if (med <= 0.0f || confidence <= 0.0f) {
        return Result{ 0.0f, lockedMidi_, 0.0f, false };
    }

    // 3. Hysteresis — use the raw median for note detection (not the smoothed EMA).
    // This prevents the slow EMA from drifting through enharmonic neighbours during transitions.
    const int newMidi = freqToMidi(med);

    if (newMidi == lockedMidi_) {
        candidateMidi_  = -1;
        candidateCount_ = 0;
    } else {
        if (newMidi == candidateMidi_) {
            ++candidateCount_;
        } else {
            candidateMidi_  = newMidi;
            candidateCount_ = 1;
        }

        if (candidateCount_ >= cfg_.hysteresisFrames) {
            lockedMidi_    = candidateMidi_;
            smoothedFreq_  = med; // snap EMA to new note to avoid lag artefacts
            candidateMidi_  = -1;
            candidateCount_ = 0;
        }
    }

    if (lockedMidi_ < 0) {
        lockedMidi_ = newMidi;
    }

    // 4. EMA — only for smooth cents/frequency display within the locked note
    smoothedFreq_ = (smoothedFreq_ <= 0.0f)
        ? med
        : cfg_.emaAlpha * med + (1.0f - cfg_.emaAlpha) * smoothedFreq_;

    const float cents = freqToCentsFromMidi(smoothedFreq_, lockedMidi_);

    return Result{
        smoothedFreq_,
        lockedMidi_,
        cents,
        true
    };
}
