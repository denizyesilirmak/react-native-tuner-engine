#include "PyinPitchDetector.hpp"

#include <algorithm>
#include <cmath>

namespace {

// Number of discrete levels the Beta threshold prior is sampled at.
constexpr int kThresholdCount = 100;

// Beta(2, 18) — mean 0.1, the threshold prior used in the pYIN paper.
constexpr float kBetaAlpha = 2.0f;
constexpr float kBetaBeta  = 18.0f;

// When no minimum clears a threshold, the deepest minimum still receives this
// fraction of that threshold's prior mass (the paper's "no candidate" fallback).
constexpr float kNoCandidateFallbackWeight = 0.01f;

// Candidates within this distance of the previous frame's pitch get a selection
// bonus — a lightweight stand-in for the paper's HMM tracking stage.
constexpr float kContinuitySemitones = 0.75f;
constexpr float kContinuityBonus     = 1.2f;

} // namespace

PyinPitchDetector::PyinPitchDetector(float sampleRate, int frameSize)
    : sampleRate_(sampleRate)
    , frameSize_(frameSize)
{
    squaredDifference_.resize(static_cast<size_t>(frameSize_ / 2));
    normalizedDifference_.resize(static_cast<size_t>(frameSize_ / 2));
    candidates_.reserve(32); // typical upper bound on CMND minima per frame

    // Discretize the Beta(2, 18) prior once; detect() only does lookups.
    thresholdLevels_.resize(kThresholdCount);
    thresholdPriors_.resize(kThresholdCount);
    float priorSum = 0.0f;
    for (int i = 0; i < kThresholdCount; ++i) {
        const float threshold = static_cast<float>(i + 1) / static_cast<float>(kThresholdCount);
        thresholdLevels_[i] = threshold;
        thresholdPriors_[i] = std::pow(threshold, kBetaAlpha - 1.0f)
                            * std::pow(1.0f - threshold, kBetaBeta - 1.0f);
        priorSum += thresholdPriors_[i];
    }
    for (float& prior : thresholdPriors_) prior /= priorSum;
}

void PyinPitchDetector::setFrequencyRange(float minHz, float maxHz) {
    minFrequencyHz_ = minHz;
    maxFrequencyHz_ = maxHz;
}

void PyinPitchDetector::reset() {
    previousPitchHz_ = 0.0f;
}

DetectorResult PyinPitchDetector::detect(const float* frame, int frameLength, float sampleRate) {
    const float rate = sampleRate > 0.0f ? sampleRate : sampleRate_;

    if (!frame || frameLength < frameSize_) return DetectorResult{};

    const int minLag = std::max(2, static_cast<int>(rate / maxFrequencyHz_));
    const int maxLag = std::min(
        frameSize_ / 2 - 2, // leave room for the lag+1 neighbour reads below
        static_cast<int>(rate / minFrequencyHz_)
    );
    if (minLag >= maxLag) return DetectorResult{};

    // YIN step 2: squared difference between the frame and its lagged copy.
    // Computed one lag past maxLag so the minima scan can look at lag+1.
    const int lagLimit = maxLag + 1;
    for (int lag = 1; lag <= lagLimit; ++lag) {
        float sum = 0.0f;
        for (int i = 0; i < frameSize_ - lag; ++i) {
            const float delta = frame[i] - frame[i + lag];
            sum += delta * delta;
        }
        squaredDifference_[lag] = sum;
    }

    // YIN step 3: cumulative-mean-normalized difference (CMND).
    normalizedDifference_[0] = 1.0f;
    float differenceSum = 0.0f;
    for (int lag = 1; lag <= lagLimit; ++lag) {
        differenceSum += squaredDifference_[lag];
        normalizedDifference_[lag] = (differenceSum <= 0.0f)
            ? 1.0f
            : squaredDifference_[lag] * static_cast<float>(lag) / differenceSum;
    }

    // Every CMND local minimum in the lag range is a pitch candidate.
    candidates_.clear();
    for (int lag = minLag; lag <= maxLag; ++lag) {
        const float left  = normalizedDifference_[lag - 1];
        const float here  = normalizedDifference_[lag];
        const float right = normalizedDifference_[lag + 1];
        if (here < left && here <= right) {
            candidates_.push_back({lag, here, 0.0f});
        }
    }
    if (candidates_.empty()) return DetectorResult{};

    PitchCandidate* deepest = &candidates_[0];
    for (auto& candidate : candidates_) {
        if (candidate.cmndDepth < deepest->cmndDepth) deepest = &candidate;
    }

    // pYIN core: accumulate probability mass over Beta-distributed thresholds.
    // For each threshold, YIN's rule picks the first (lowest-lag) minimum below
    // it — so each candidate's mass is the prior probability of the thresholds
    // at which YIN would have chosen it.
    for (int i = 0; i < kThresholdCount; ++i) {
        const float threshold = thresholdLevels_[i];
        PitchCandidate* firstBelowThreshold = nullptr;
        for (auto& candidate : candidates_) {
            if (candidate.cmndDepth < threshold) {
                firstBelowThreshold = &candidate;
                break;
            }
        }
        if (firstBelowThreshold) {
            firstBelowThreshold->probability += thresholdPriors_[i];
        } else {
            deepest->probability += thresholdPriors_[i] * kNoCandidateFallbackWeight;
        }
    }

    // Winner = highest mass, with a small bonus for staying near the previous
    // pitch. The bonus only affects the ranking; reported confidence uses the
    // unbiased mass so a sustained note cannot inflate its own confidence.
    const PitchCandidate* winner = nullptr;
    float bestScore = 0.0f;
    for (const auto& candidate : candidates_) {
        if (candidate.probability <= 0.0f) continue;
        float score = candidate.probability;
        if (previousPitchHz_ > 0.0f) {
            const float candidateHz = rate / static_cast<float>(candidate.lag);
            const float semitonesAway =
                std::fabs(12.0f * std::log2(candidateHz / previousPitchHz_));
            if (semitonesAway <= kContinuitySemitones) score *= kContinuityBonus;
        }
        if (!winner || score > bestScore) {
            winner = &candidate;
            bestScore = score;
        }
    }
    if (!winner) return DetectorResult{};

    const float refinedLag = refineLagByParabola(winner->lag);
    if (refinedLag <= 0.0f) return DetectorResult{};
    const float pitchHz = rate / refinedLag;

    // Confidence = periodicity strength × share of the pYIN mass the winner
    // captured. Both factors are in [0, 1]: a clean periodic frame with an
    // unambiguous winner scores near 1, an ambiguous or aperiodic frame scores
    // low. The pYIN mass decides WHICH candidate wins; the CMND depth keeps the
    // scale calibrated to the pipeline's confidence threshold.
    float totalMass = 0.0f;
    for (const auto& candidate : candidates_) totalMass += candidate.probability;
    const float winnerMassShare = totalMass > 0.0f ? winner->probability / totalMass : 0.0f;
    const float periodicity     = std::max(0.0f, 1.0f - winner->cmndDepth);
    const float confidence      = periodicity * winnerMassShare;

    previousPitchHz_ = pitchHz;
    return DetectorResult{true, pitchHz, confidence};
}

float PyinPitchDetector::refineLagByParabola(int lag) const {
    if (lag <= 0 || lag >= static_cast<int>(normalizedDifference_.size()) - 1) {
        return static_cast<float>(lag);
    }
    const float left   = normalizedDifference_[lag - 1];
    const float center = normalizedDifference_[lag];
    const float right  = normalizedDifference_[lag + 1];
    const float curvature = left - 2.0f * center + right;
    if (std::fabs(curvature) < 1e-6f) return static_cast<float>(lag);
    return static_cast<float>(lag) + 0.5f * (left - right) / curvature;
}
