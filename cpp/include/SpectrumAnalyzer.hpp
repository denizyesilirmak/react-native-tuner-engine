#pragma once

#include <complex>
#include <vector>

// Reduces a frame to a few dozen log-spaced magnitude bands for display
// (a spectrogram, a level meter). Runs beside the pitch pipeline on the raw
// frame, so it sees silence and noise too — the pipeline's gate never hides it.
class SpectrumAnalyzer {
public:
    struct Config {
        int   bands = 64;
        float minHz = 50.0f;   // centre of the lowest band
        float maxHz = 4000.0f; // centre of the highest band
    };

    SpectrumAnalyzer(int frameSize, float sampleRate, Config cfg);

    // Writes one dBFS value per band into out() (a full-scale sine reads ~0 dB).
    // n must equal the frame size the analyzer was built for.
    void process(const float* frame, int n);

    const std::vector<float>& out() const { return bands_; }
    const Config& config() const { return cfg_; }

private:
    int frameSize_;
    int fftSize_; // frame zero-padded ×2 so low bands interpolate between finer bins
    float sampleRate_;
    Config cfg_;

    std::vector<float> window_;
    std::vector<std::complex<float>> fftBuf_;
    std::vector<float> magnitude_; // linear, normalised so a full-scale sine peaks at 1
    std::vector<float> lowBin_;    // fractional bin edges per band
    std::vector<float> highBin_;
    std::vector<float> bands_;
};
