#include "SpectrumAnalyzer.hpp"
#include "Fft.hpp"

#include <algorithm>
#include <cmath>

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kMinLinear = 1e-6f; // -120 dBFS floor

SpectrumAnalyzer::SpectrumAnalyzer(int frameSize, float sampleRate, Config cfg)
    : frameSize_(frameSize)
    , fftSize_(frameSize * 2)
    , sampleRate_(sampleRate)
    , cfg_(cfg)
    , window_(static_cast<size_t>(frameSize))
    , fftBuf_(static_cast<size_t>(frameSize * 2))
    , magnitude_(static_cast<size_t>(frameSize + 1))
{
    cfg_.bands = std::clamp(cfg_.bands, 2, 512);
    const float nyquist = sampleRate_ * 0.5f;
    cfg_.minHz = std::clamp(cfg_.minHz, 1.0f, nyquist * 0.5f);
    cfg_.maxHz = std::clamp(cfg_.maxHz, cfg_.minHz * 1.01f, nyquist * 0.95f);

    // Periodic Hann. A full-scale sine then peaks at |X| = N/4.
    for (int i = 0; i < frameSize_; ++i) {
        window_[static_cast<size_t>(i)] =
            0.5f * (1.0f - std::cos(2.0f * kPi * i / static_cast<float>(frameSize_)));
    }

    // Log-spaced band centres; each band's edges sit halfway (geometrically)
    // to its neighbours, so the bands tile the range without gaps.
    const int bands = cfg_.bands;
    const float ratio = std::pow(cfg_.maxHz / cfg_.minHz, 1.0f / static_cast<float>(bands - 1));
    const float halfStep = std::sqrt(ratio);
    const float binHz = sampleRate_ / static_cast<float>(fftSize_);
    lowBin_.resize(static_cast<size_t>(bands));
    highBin_.resize(static_cast<size_t>(bands));
    bands_.assign(static_cast<size_t>(bands), -120.0f);
    for (int b = 0; b < bands; ++b) {
        const float centre = cfg_.minHz * std::pow(ratio, static_cast<float>(b));
        lowBin_[static_cast<size_t>(b)]  = centre / halfStep / binHz;
        highBin_[static_cast<size_t>(b)] = centre * halfStep / binHz;
    }
}

void SpectrumAnalyzer::process(const float* frame, int n) {
    if (!frame || n != frameSize_) return;

    for (int i = 0; i < frameSize_; ++i) {
        fftBuf_[static_cast<size_t>(i)] = {frame[i] * window_[static_cast<size_t>(i)], 0.0f};
    }
    std::fill(fftBuf_.begin() + frameSize_, fftBuf_.end(), std::complex<float>{});
    tuner::fft(fftBuf_);

    const float norm = 4.0f / static_cast<float>(frameSize_);
    const int lastBin = fftSize_ / 2;
    for (int k = 0; k <= lastBin; ++k) {
        magnitude_[static_cast<size_t>(k)] = std::abs(fftBuf_[static_cast<size_t>(k)]) * norm;
    }

    for (size_t b = 0; b < bands_.size(); ++b) {
        const float lo = lowBin_[b];
        const float hi = highBin_[b];

        // Narrow low bands may hold no whole bin: read the spectrum at the
        // band's centre between its two nearest bins instead.
        const float centre = std::sqrt(lo * hi);
        const int k0 = std::min(static_cast<int>(centre), lastBin - 1);
        const float t = centre - static_cast<float>(k0);
        float peak = magnitude_[static_cast<size_t>(k0)] * (1.0f - t)
                   + magnitude_[static_cast<size_t>(k0 + 1)] * t;

        const int first = std::max(0, static_cast<int>(std::ceil(lo)));
        const int last  = std::min(lastBin, static_cast<int>(std::floor(hi)));
        for (int k = first; k <= last; ++k) {
            peak = std::max(peak, magnitude_[static_cast<size_t>(k)]);
        }

        bands_[b] = 20.0f * std::log10(std::max(peak, kMinLinear));
    }
}
