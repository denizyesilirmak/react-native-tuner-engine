#include "StringMatcher.hpp"

#include <cmath>

void StringMatcher::setTuning(const TuningProfile* profile) {
    profile_ = profile;
}

bool StringMatcher::hasTuning() const {
    return profile_ != nullptr && profile_->stringCount > 0;
}

std::optional<StringMatch> StringMatcher::match(float frequency,
                                                  float maxDeviationCents) const {
    if (!hasTuning() || frequency <= 0.0f) return std::nullopt;

    const StringMatch* best = nullptr;
    float bestAbsCents = maxDeviationCents + 1.0f; // sentinel > threshold
    StringMatch bestMatch{};

    for (int i = 0; i < profile_->stringCount; ++i) {
        const StringTarget& s = profile_->strings[i];
        if (s.frequency <= 0.0f) continue;

        const float cents = 1200.0f * std::log2(frequency / s.frequency);
        const float absCents = std::fabs(cents);

        if (absCents < bestAbsCents) {
            bestAbsCents  = absCents;
            bestMatch     = {s.name, s.stringNumber, s.frequency, cents};
            best          = &bestMatch;
        }
    }

    if (!best || bestAbsCents > maxDeviationCents) return std::nullopt;
    return bestMatch;
}
