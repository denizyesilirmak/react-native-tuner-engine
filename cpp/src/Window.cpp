#include "Window.hpp"

#include <algorithm>
#include <cmath>

static constexpr float kPi = 3.14159265358979323846f;

HannWindow::HannWindow(int size) : coeffs_(static_cast<size_t>(size)) {
    for (int i = 0; i < size; ++i) {
        coeffs_[static_cast<size_t>(i)] =
            0.5f * (1.0f - std::cos(2.0f * kPi * i / (size - 1)));
    }
}

void HannWindow::apply(float* frame, int n) const {
    if (!frame || n <= 0) return;
    const int len = std::min(n, static_cast<int>(coeffs_.size()));
    for (int i = 0; i < len; ++i) {
        frame[i] *= coeffs_[static_cast<size_t>(i)];
    }
}
