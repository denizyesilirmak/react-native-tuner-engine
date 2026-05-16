#pragma once

#include "PitchResult.hpp"

class NoteMapper {
public:
    explicit NoteMapper(float a4 = 440.0f);

    PitchResult map(float frequency, float confidence, float rmsDb) const;

    void setA4(float value);

private:
    float a4_;
};