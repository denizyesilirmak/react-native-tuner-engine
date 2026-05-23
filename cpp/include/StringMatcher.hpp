#pragma once

#include "TuningPresets.hpp"

#include <optional>

struct StringMatch {
    const char* name;           // "E2", "A2", "D3" ...
    int         stringNumber;   // 1-based (thickest = 1)
    float       targetHz;       // exact equal-temperament frequency
    float       deviationCents; // signed: negative = flat, positive = sharp
};

// Maps a detected frequency to the nearest string in the active TuningProfile.
class StringMatcher {
public:
    void setTuning(const TuningProfile* profile);
    bool hasTuning() const;

    // Returns the nearest string within maxDeviationCents (default 100 ¢ = 1 semitone).
    // Returns std::nullopt if no tuning is set or no string is close enough.
    std::optional<StringMatch> match(float frequency,
                                     float maxDeviationCents = 100.0f) const;

private:
    const TuningProfile* profile_ = nullptr;
};
