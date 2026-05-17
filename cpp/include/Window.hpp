#pragma once

#include <vector>

// Precomputed Hann window. apply() multiplies a frame in-place.
class HannWindow {
public:
    explicit HannWindow(int size);
    void apply(float* frame, int n) const;

private:
    std::vector<float> coeffs_;
};
