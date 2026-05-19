#include "CepstrumPitchDetector.hpp"
#include "Fft.hpp"

#include <algorithm>
#include <cmath>

static constexpr float kEps = 1e-10f;
static constexpr float kPi  = 3.14159265358979323846f;

CepstrumPitchDetector::CepstrumPitchDetector(float sampleRate, int frameSize)
    : sampleRate_(sampleRate)
    , frameSize_(frameSize)
    , hann_(static_cast<size_t>(frameSize))
    , fftBuf_(static_cast<size_t>(frameSize))
    , logPow_(static_cast<size_t>(frameSize))
{
    for (int i = 0; i < frameSize_; ++i) {
        hann_[i] = 0.5f * (1.0f - std::cos(2.0f * kPi * i / (frameSize_ - 1)));
    }
}

void CepstrumPitchDetector::setFrequencyRange(float minHz, float maxHz) {
    minHz_ = minHz;
    maxHz_ = maxHz;
}

void CepstrumPitchDetector::setThreshold(float threshold) {
    threshold_ = threshold;
}

DetectorResult CepstrumPitchDetector::detect(const float* frame, int n, float sampleRate) {
    const float sr  = sampleRate > 0.0f ? sampleRate : sampleRate_;
    const int   len = std::min(n, frameSize_);

    // Hann-window the frame into the FFT buffer
    for (int i = 0; i < len; ++i) {
        fftBuf_[i] = {frame[i] * hann_[i], 0.0f};
    }
    for (int i = len; i < frameSize_; ++i) {
        fftBuf_[i] = {};
    }

    tuner::fft(fftBuf_);

    // Log power spectrum — build full two-sided symmetric array in logPow_
    for (int k = 0; k <= frameSize_ / 2; ++k) {
        const float re = fftBuf_[k].real();
        const float im = fftBuf_[k].imag();
        logPow_[k]    = std::log(re * re + im * im + kEps);
    }
    for (int k = 1; k < frameSize_ / 2; ++k) {
        logPow_[frameSize_ - k] = logPow_[k];
    }

    // Load log-power into fftBuf_ (real) and IFFT → real cepstrum
    for (int k = 0; k < frameSize_; ++k) {
        fftBuf_[k] = {logPow_[k], 0.0f};
    }
    tuner::ifft(fftBuf_);

    // Quefrency search range
    const int tauMin = std::max(1,  static_cast<int>(sr / maxHz_));
    const int tauMax = std::min(frameSize_ / 2 - 1, static_cast<int>(sr / minHz_));
    if (tauMin >= tauMax) return DetectorResult{};

    // Find the quefrency peak
    float maxVal = -1e30f;
    int   bestTau = -1;
    for (int q = tauMin; q <= tauMax; ++q) {
        const float v = fftBuf_[q].real();
        if (v > maxVal) { maxVal = v; bestTau = q; }
    }
    if (bestTau < 0) return DetectorResult{};

    const float prominence = peakProminence(tauMin, tauMax, bestTau);
    if (prominence < threshold_) return DetectorResult{};

    // Sub-sample refinement via parabolic interpolation
    float peakTau = static_cast<float>(bestTau);
    if (bestTau > tauMin && bestTau < tauMax) {
        const float L = fftBuf_[bestTau - 1].real();
        const float C = fftBuf_[bestTau].real();
        const float R = fftBuf_[bestTau + 1].real();
        const float d = L - 2.0f * C + R;
        if (std::fabs(d) > 1e-6f) {
            peakTau += 0.5f * (L - R) / d;
        }
    }
    if (peakTau <= 0.0f) return DetectorResult{};

    return DetectorResult{true, sr / peakTau, prominence};
}

float CepstrumPitchDetector::peakProminence(int tauMin, int tauMax, int peakTau) const {
    const float peak = fftBuf_[peakTau].real();

    // RMS and mean of the quefrency range (excluding the peak itself)
    float sum = 0.0f;
    float sumSq = 0.0f;
    const int count = tauMax - tauMin + 1;
    for (int q = tauMin; q <= tauMax; ++q) {
        const float v = fftBuf_[q].real();
        sum   += v;
        sumSq += v * v;
    }
    const float mean = sum   / static_cast<float>(count);
    const float rms  = std::sqrt(sumSq / static_cast<float>(count));

    // Confidence: how much the peak exceeds the RMS level.
    // A cepstrum with no clear periodicity (pure sine) has peak ≈ RMS → conf ≈ 0.
    // A strong harmonic signal has peak >> RMS → conf → 1.
    if (rms < kEps) return 0.0f;
    const float snr = (peak - mean) / rms;
    // Map snr range [0, 5] → [0, 1]; cap both sides
    return std::max(0.0f, std::min(1.0f, snr / 5.0f));
}
