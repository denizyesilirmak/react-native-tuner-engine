#pragma once

#include "PitchResult.hpp"

#include <string>

class NoteMapper {
public:
    explicit NoteMapper(float a4 = 440.0f);

    PitchResult map(float frequency, float confidence, float rmsDb) const;

    void setA4(float value);
    void setTemperament(const std::string& name); // "equal" or "just"

private:
    float a4_;
    bool useJust_ = false;

    // Cents offset from equal temperament for each pitch class (C=0..B=11)
    // using 5-limit just intonation with C as root.
    static constexpr float kJustCentsOffset[12] = {
        0.0f,      // C   1/1
        11.73f,    // C#  16/15
        3.91f,     // D   9/8
        15.64f,    // D#  6/5
        -13.69f,   // E   5/4
        -1.96f,    // F   4/3
        -9.78f,    // F#  45/32
        1.96f,     // G   3/2
        13.69f,    // G#  8/5
        -15.64f,   // A   5/3
        17.60f,    // A#  9/5
        -11.73f,   // B   15/8
    };
};