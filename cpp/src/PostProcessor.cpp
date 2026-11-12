#include "PostProcessor.hpp"

#include <algorithm>
#include <cmath>

PostProcessor::PostProcessor() : cfg_{} {}
PostProcessor::PostProcessor(Config cfg) : cfg_(cfg) {
    cfg_.emaAlpha = std::clamp(cfg_.emaAlpha, 0.01f, 1.0f);
    cfg_.hysteresisFrames = std::max(cfg_.hysteresisFrames, 1);
}

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
    cfg.emaAlpha = std::clamp(cfg.emaAlpha, 0.01f, 1.0f);
    cfg.hysteresisFrames = std::max(cfg.hysteresisFrames, 1);
    cfg_ = cfg;
    reset();
}

float PostProcessor::median5() const {
    // Optimal median-of-5 via sorting network (6 comparisons, no allocation)
    float a = medianBuf_[0], b = medianBuf_[1], c = medianBuf_[2],
          d = medianBuf_[3], e = medianBuf_[4];
    // Sort pairs
    if (a > b) std::swap(a, b);
    if (c > d) std::swap(c, d);
    // Eliminate the lowest of the two pairs
    if (a > c) { std::swap(a, c); std::swap(b, d); }
    // Now a is the global minimum, discard it. Median is among {b, c, d, e}
    // Find median of {b, c, d, e} by eliminating max and min
    if (b > e) std::swap(b, e);
    // Median is middle of {b, c, d}
    if (b > c) std::swap(b, c);
    if (c > d) std::swap(c, d);
    return std::min(c, e);
}

void PostProcessor::setA4(float hz) {
    if (hz <= 0.0f || hz == a4_) return;
    a4_ = hz;
    reset(); // the note boundaries moved; the locked note may no longer hold
}

int PostProcessor::freqToMidi(float hz) const {
    if (hz <= 0.0f) return -1;
    return static_cast<int>(std::round(69.0f + 12.0f * std::log2(hz / a4_)));
}

float PostProcessor::freqToCentsFromMidi(float hz, int midi) const {
    if (hz <= 0.0f || midi < 0) return 0.0f;
    const float targetHz = a4_ * std::pow(2.0f, (midi - 69) / 12.0f);
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

    // A different note is being confirmed. Blending its frequency into the
    // EMA of the locked note reported notes that were never played (A2 to D3
    // passed through B2 and C3), so hold the output until the change settles.
    if (newMidi != lockedMidi_) {
        return Result{ 0.0f, lockedMidi_, 0.0f, false };
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
