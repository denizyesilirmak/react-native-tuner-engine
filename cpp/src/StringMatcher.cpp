#include "StringMatcher.hpp"

#include <cmath>

void StringMatcher::setTuning(const TuningProfile* profile) {
    profile_ = profile;
}

bool StringMatcher::hasTuning() const {
    return profile_ != nullptr && profile_->stringCount > 0;
}

void StringMatcher::setA4(float hz) {
    if (hz > 0.0f) a4_ = hz;
}

void StringMatcher::setTranspose(float semitones) {
    if (std::isfinite(semitones)) transposeSemitones_ = semitones;
}

float StringMatcher::scale() const {
    return (a4_ / 440.0f) * std::pow(2.0f, transposeSemitones_ / 12.0f);
}

std::optional<StringMatch> StringMatcher::match(float frequency,
                                                  float maxDeviationCents) const {
    if (!hasTuning() || frequency <= 0.0f) return std::nullopt;

    const float k = scale();
    bool found = false;
    float bestAbsCents = maxDeviationCents + 1.0f; // sentinel > threshold
    StringMatch bestMatch{};

    for (int i = 0; i < profile_->stringCount; ++i) {
        const StringTarget& s = profile_->strings[i];
        if (s.frequency <= 0.0f) continue;

        // The notated string, plus its octave partner when the course has one.
        const float base = s.frequency * k;
        const float candidates[2] = {
            base,
            s.octaveOffset == 0 ? 0.0f : base * std::pow(2.0f, static_cast<float>(s.octaveOffset)),
        };

        for (float target : candidates) {
            if (target <= 0.0f) continue;
            const float cents = 1200.0f * std::log2(frequency / target);
            const float absCents = std::fabs(cents);
            if (absCents < bestAbsCents) {
                bestAbsCents = absCents;
                bestMatch    = {s.name, s.stringNumber, target, cents};
                found        = true;
            }
        }
    }

    if (!found || bestAbsCents > maxDeviationCents) return std::nullopt;
    return bestMatch;
}
