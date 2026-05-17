#pragma once

#include <string>

struct FrequencyRange {
    float minHz;
    float maxHz;
};

// Returns the recommended pitch-detection frequency range for a given instrument name.
// Falls back to chromatic (60–1200 Hz) for unknown names.
inline FrequencyRange instrumentPreset(const std::string& name) {
    if (name == "guitar")    return { 75.0f,  1320.0f };
    if (name == "bass")      return { 38.0f,   330.0f };
    if (name == "ukulele")   return { 250.0f,  880.0f };
    if (name == "violin")    return { 190.0f, 3200.0f };
    if (name == "cello")     return {  60.0f, 1050.0f };
    if (name == "viola")     return { 125.0f, 1320.0f };
    if (name == "mandolin")  return { 190.0f, 2100.0f };
    if (name == "banjo")     return { 190.0f, 1320.0f };
    return { 60.0f, 1200.0f }; // chromatic default
}
