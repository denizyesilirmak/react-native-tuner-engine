#pragma once

#include <cmath>
#include <complex>
#include <vector>

namespace tuner {

// In-place radix-2 Cooley-Tukey DFT. n = x.size() must be a power of 2.
inline void fft(std::vector<std::complex<float>>& x) {
    const int n = static_cast<int>(x.size());

    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }

    for (int len = 2; len <= n; len <<= 1) {
        const float angle = -2.0f * 3.14159265358979323846f / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                const std::complex<float> u = x[i + j];
                const std::complex<float> v = x[i + j + len / 2] * w;
                x[i + j]           = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// In-place inverse DFT.
inline void ifft(std::vector<std::complex<float>>& x) {
    for (auto& c : x) c = std::conj(c);
    fft(x);
    const float inv = 1.0f / static_cast<float>(x.size());
    for (auto& c : x) c = std::conj(c) * inv;
}

} // namespace tuner
