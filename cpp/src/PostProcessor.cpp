#include "PostProcessor.hpp"

#include <algorithm>
#include <cmath>

// Smoothing of measured readings: this slow while the note barely moves,
// the caller's rate once a step reaches kFastStepCents.
static constexpr float kSteadyAlpha   = 0.12f;
static constexpr float kFastStepCents = 6.0f;
// Following a moving note: how much of each error goes into the trend, the
// error below which nothing is learned, the fade when errors disagree, and
// the fastest motion followed (cents per frame).
static constexpr float kTrendGain     = 0.25f;
static constexpr float kTrendMinCents = 0.7f;
static constexpr float kTrendFade     = 0.5f;
static constexpr float kTrendMaxCents = 15.0f;
// How far ahead of the smoothed value the reading is carried, in frames of
// trend. One made a fast turn 6 cents behind instead of 8.5; two overshot.
static constexpr float kLeadFrames    = 1.0f;

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
        trendCents_     = 0.0f;
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

PostProcessor::Result PostProcessor::process(float frequency, float confidence, float emaAlpha,
                                             bool measured) {
    const float alpha = emaAlpha > 0.0f ? std::clamp(emaAlpha, 0.01f, 1.0f) : cfg_.emaAlpha;

    if (frequency <= 0.0f || confidence <= 0.0f) {
        return Result{ 0.0f, lockedMidi_, 0.0f, false };
    }

    // 1. Which note is this frame? Decided on the frame itself, not on the
    // median: the median kept answering with the old note for two frames
    // after the string had changed, and those stale readings were shown.
    const int frameMidi = freqToMidi(frequency);

    if (lockedMidi_ < 0) {
        lockedMidi_ = frameMidi;
    }

    if (frameMidi != lockedMidi_) {
        if (frameMidi == candidateMidi_) {
            ++candidateCount_;
        } else {
            candidateMidi_  = frameMidi;
            candidateCount_ = 1;
        }
        if (candidateCount_ < cfg_.hysteresisFrames) {
            // A different note is being confirmed (or this frame is a stray).
            // Report nothing: neither the old note, which may be gone, nor a
            // blend of the two, which was never played.
            return Result{ 0.0f, lockedMidi_, 0.0f, false };
        }
        // Confirmed: start the new note clean.
        lockedMidi_     = candidateMidi_;
        candidateMidi_  = -1;
        candidateCount_ = 0;
        medianIdx_      = 0;
        medianFill_     = 0;
        smoothedFreq_   = 0.0f;
        trendCents_     = 0.0f;
    } else {
        candidateMidi_  = -1;
        candidateCount_ = 0;
    }

    // 2. Median over the frames of this note: drops a stray value inside it.
    medianBuf_[medianIdx_] = frequency;
    medianIdx_ = (medianIdx_ + 1) % kMedianLen;
    if (medianFill_ < kMedianLen) ++medianFill_;
    const float med = (!measured && medianFill_ == kMedianLen) ? median5() : frequency;

    // 3. EMA for a steady display within the note.
    if (smoothedFreq_ <= 0.0f) {
        smoothedFreq_ = med;
    } else if (measured) {
        // A ringing string wanders by a cent or two and the display should
        // not; a peg being turned moves it by many and the display must keep
        // up. Slow for small steps, fast for large ones.
        //
        // Smoothing alone trails a moving note: at 60 cents a second the
        // reading was 9 cents behind a loud string and 25 behind a faint
        // one. So the motion itself is followed. The reading is first moved
        // on by the trend, then corrected by what was measured, and the
        // trend learns from the same error; it fades as soon as the errors
        // stop agreeing, so a ringing string's wander does not start one.
        const float predicted = smoothedFreq_ * std::pow(2.0f, trendCents_ / 1200.0f);
        const float error = 1200.0f * std::log2(med / predicted);
        const float adaptive = kSteadyAlpha
            + (alpha - kSteadyAlpha) * std::min(1.0f, std::fabs(error) / kFastStepCents);
        smoothedFreq_ = predicted * std::pow(2.0f, adaptive * error / 1200.0f);
        const bool sameWay = (error > 0.0f) == (lastError_ > 0.0f)
            && std::fabs(error) >= kTrendMinCents && std::fabs(lastError_) >= kTrendMinCents;
        trendCents_ = sameWay
            ? std::clamp(trendCents_ + kTrendGain * error, -kTrendMaxCents, kTrendMaxCents)
            : trendCents_ * kTrendFade;
        lastError_ = error;
        leading_ = sameWay;
    } else {
        smoothedFreq_ = alpha * med + (1.0f - alpha) * smoothedFreq_;
    }

    // What was measured is the note as it was about half a measuring window
    // ago. While it moves, the reading is carried forward by that much; the
    // moment the errors stop agreeing it is not, or the reading would run on
    // past where the peg stopped.
    const float shown = measured
        ? smoothedFreq_ * std::pow(2.0f, (leading_ ? kLeadFrames * trendCents_ : 0.0f) / 1200.0f)
        : smoothedFreq_;
    const float cents = freqToCentsFromMidi(shown, lockedMidi_);

    return Result{
        shown,
        lockedMidi_,
        cents,
        true
    };
}
