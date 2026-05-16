#include "YinPitchDetector.hpp"

#include <algorithm>
#include <cmath>

YinPitchDetector::YinPitchDetector(float sampleRate, int frameSize)
    : sampleRate_(sampleRate), frameSize_(frameSize) {
    difference_.resize(frameSize_ / 2);
    cmnd_.resize(frameSize_ / 2);
}

void YinPitchDetector::setFrequencyRange(float minFrequency, float maxFrequency) {
    minFrequency_ = minFrequency;
    maxFrequency_ = maxFrequency;
}

void YinPitchDetector::setThreshold(float threshold) {
    threshold_ = threshold;
}

YinResult YinPitchDetector::detect(const float* input, int frameCount) {
    YinResult result;

    if (input == nullptr || frameCount < frameSize_) {
        return result;
    }

    const int tauMin = std::max(2, static_cast<int>(sampleRate_ / maxFrequency_));
    const int tauMax = std::min(
        frameSize_ / 2 - 1,
        static_cast<int>(sampleRate_ / minFrequency_)
    );

    std::fill(difference_.begin(), difference_.end(), 0.0f);

    for (int tau = 1; tau <= tauMax; ++tau) {
        float sum = 0.0f;

        for (int i = 0; i < frameSize_ - tau; ++i) {
            const float delta = input[i] - input[i + tau];
            sum += delta * delta;
        }

        difference_[tau] = sum;
    }

    cmnd_[0] = 1.0f;

    float runningSum = 0.0f;

    for (int tau = 1; tau <= tauMax; ++tau) {
        runningSum += difference_[tau];

        if (runningSum <= 0.0f) {
            cmnd_[tau] = 1.0f;
        } else {
            cmnd_[tau] = difference_[tau] * tau / runningSum;
        }
    }

    int tauEstimate = -1;

    for (int tau = tauMin; tau <= tauMax; ++tau) {
        if (cmnd_[tau] < threshold_) {
            while (tau + 1 <= tauMax && cmnd_[tau + 1] < cmnd_[tau]) {
                tau++;
            }

            tauEstimate = tau;
            break;
        }
    }

    if (tauEstimate == -1) {
        return result;
    }

    const float betterTau = parabolicInterpolation(tauEstimate);

    if (betterTau <= 0.0f) {
        return result;
    }

    result.hasPitch = true;
    result.frequency = sampleRate_ / betterTau;
    result.confidence = 1.0f - cmnd_[tauEstimate];

    return result;
}

float YinPitchDetector::parabolicInterpolation(int tau) const {
    if (tau <= 0 || tau >= static_cast<int>(cmnd_.size()) - 1) {
        return static_cast<float>(tau);
    }

    const float left = cmnd_[tau - 1];
    const float center = cmnd_[tau];
    const float right = cmnd_[tau + 1];

    const float denominator = left - 2.0f * center + right;

    if (std::fabs(denominator) < 1e-6f) {
        return static_cast<float>(tau);
    }

    return tau + 0.5f * (left - right) / denominator;
}