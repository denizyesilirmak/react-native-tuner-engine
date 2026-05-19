#include "PyinPitchDetector.hpp"

#include <algorithm>
#include <cmath>

PyinPitchDetector::PyinPitchDetector(float sampleRate, int frameSize)
    : sampleRate_(sampleRate)
    , frameSize_(frameSize)
{
    diff_.resize(static_cast<size_t>(frameSize_ / 2));
    cmnd_.resize(static_cast<size_t>(frameSize_ / 2));
    candidates_.reserve(32); // typical number of CMND minima
}

void PyinPitchDetector::setFrequencyRange(float minHz, float maxHz) {
    minHz_ = minHz;
    maxHz_ = maxHz;
}

void PyinPitchDetector::setThreshold(float threshold) {
    threshold_ = threshold;
}

DetectorResult PyinPitchDetector::detect(const float* frame, int n, float sampleRate) {
    const float sr = sampleRate > 0.0f ? sampleRate : sampleRate_;

    if (!frame || n < frameSize_) return DetectorResult{};

    const int tauMin = std::max(2, static_cast<int>(sr / maxHz_));
    const int tauMax = std::min(
        frameSize_ / 2 - 1,
        static_cast<int>(sr / minHz_)
    );

    if (tauMin >= tauMax) return DetectorResult{};

    // Squared difference function (YIN step 2)
    std::fill(diff_.begin(), diff_.end(), 0.0f);
    for (int tau = 1; tau <= tauMax; ++tau) {
        float sum = 0.0f;
        for (int i = 0; i < frameSize_ - tau; ++i) {
            const float d = frame[i] - frame[i + tau];
            sum += d * d;
        }
        diff_[tau] = sum;
    }

    // Cumulative mean normalised difference (CMND, YIN step 3)
    cmnd_[0] = 1.0f;
    float runningSum = 0.0f;
    for (int tau = 1; tau <= tauMax; ++tau) {
        runningSum += diff_[tau];
        cmnd_[tau] = (runningSum <= 0.0f)
                     ? 1.0f
                     : diff_[tau] * static_cast<float>(tau) / runningSum;
    }

    // Collect all local minima of CMND below threshold — this is the PYIN divergence point.
    // YIN stops at the first; PYIN considers all.
    candidates_.clear();

    auto tryAdd = [&](int tau) {
        if (tau < tauMin || tau > tauMax) return;
        if (cmnd_[tau] < threshold_) {
            candidates_.push_back({tau, 1.0f - cmnd_[tau] / threshold_});
        }
    };

    // Interior local minima
    for (int tau = tauMin + 1; tau < tauMax; ++tau) {
        if (cmnd_[tau] < cmnd_[tau - 1] && cmnd_[tau] < cmnd_[tau + 1]) {
            tryAdd(tau);
        }
    }
    // Boundary checks
    if (cmnd_[tauMin] < cmnd_[tauMin + 1]) tryAdd(tauMin);
    if (cmnd_[tauMax] < cmnd_[tauMax - 1]) tryAdd(tauMax);

    if (candidates_.empty()) return DetectorResult{};

    // The CMND formula makes later (longer-period) minima numerically smaller than
    // earlier ones at sub-multiples of the same fundamental period. Without pruning,
    // a pure sine would always produce candidates at tau0, 2*tau0, 3*tau0 … with the
    // highest probability at the LONGEST alias — the wrong (sub-octave) answer.
    //
    // Prune: for any candidate whose tau is within 2% of an integer multiple (≥2×) of
    // a shorter-period candidate, zero its probability. Candidates are already in
    // ascending tau order, so a single forward pass suffices.
    for (int i = 0; i < static_cast<int>(candidates_.size()); ++i) {
        if (candidates_[i].prob == 0.0f) continue;
        for (int j = i + 1; j < static_cast<int>(candidates_.size()); ++j) {
            const float ratio   = static_cast<float>(candidates_[j].tau)
                                  / static_cast<float>(candidates_[i].tau);
            const float nearest = std::round(ratio);
            if (nearest >= 2.0f && std::fabs(ratio - nearest) / nearest < 0.02f) {
                candidates_[j].prob = 0.0f;
            }
        }
    }

    // Sum surviving probabilities for voiced confidence
    float totalProb = 0.0f;
    for (const auto& c : candidates_) totalProb += c.prob;

    // Pick the candidate with the highest (surviving) probability.
    // Strict '>' keeps the first (lowest-tau, highest-frequency) on exact ties.
    const Candidate* winner = nullptr;
    for (const auto& c : candidates_) {
        if (!winner || c.prob > winner->prob) winner = &c;
    }
    if (!winner || winner->prob == 0.0f) return DetectorResult{};

    const float betterTau = parabolicInterpolation(winner->tau);
    if (betterTau <= 0.0f) return DetectorResult{};

    // Confidence: voiced probability × how much the winner dominates
    const float voicedProb  = std::min(1.0f, totalProb);
    const float winnerShare = (totalProb > 0.0f) ? winner->prob / totalProb : 0.0f;
    const float confidence  = voicedProb * (0.5f + 0.5f * winnerShare);

    return DetectorResult{true, sr / betterTau, confidence};
}

float PyinPitchDetector::parabolicInterpolation(int tau) const {
    if (tau <= 0 || tau >= static_cast<int>(cmnd_.size()) - 1) {
        return static_cast<float>(tau);
    }
    const float L = cmnd_[tau - 1];
    const float C = cmnd_[tau];
    const float R = cmnd_[tau + 1];
    const float d = L - 2.0f * C + R;
    if (std::fabs(d) < 1e-6f) return static_cast<float>(tau);
    return tau + 0.5f * (L - R) / d;
}
