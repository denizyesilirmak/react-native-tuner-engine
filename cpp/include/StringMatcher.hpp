#pragma once

#include "TuningPresets.hpp"

#include <optional>

struct StringMatch {
    const char* name;           // "E2", "A2", "D3" ... (the course's notated name)
    int         stringNumber;   // 1-based (thickest = 1)
    float       targetHz;       // frequency the deviation was measured against
    float       deviationCents; // signed: negative = flat, positive = sharp
};

// Maps a detected frequency to the nearest string in the active TuningProfile.
//
// Targets are authored at A4 = 440 Hz. The matcher scales them by the A4
// reference and by an optional transpose (in semitones) so bağlama-style
// "notated on A, tuned wherever the player likes" setups keep matching. A
// course flagged with `octaveOffset` is also matched one octave up or down,
// so a pitch detector locking onto either string of an octave pair still
// lands on the right course.
class StringMatcher {
public:
    void setTuning(const TuningProfile* profile);
    bool hasTuning() const;

    // A4 reference in Hz (default 440). Scales every target.
    void setA4(float hz);
    // Shift every target by this many semitones (fractional allowed). Default 0.
    void setTranspose(float semitones);

    float a4() const { return a4_; }
    float transpose() const { return transposeSemitones_; }

    // Returns the nearest string within maxDeviationCents (default 100 ¢ = 1 semitone).
    // Returns std::nullopt if no tuning is set or no string is close enough.
    std::optional<StringMatch> match(float frequency,
                                     float maxDeviationCents = 100.0f) const;

private:
    const TuningProfile* profile_ = nullptr;
    float a4_ = 440.0f;
    float transposeSemitones_ = 0.0f;

    float scale() const; // a4 and transpose combined into one multiplier
};
