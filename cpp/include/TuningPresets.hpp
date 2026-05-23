#pragma once

#include <string>

struct StringTarget {
    const char* name;   // e.g. "E2", "A2", "D3"
    int   stringNumber; // 1-based, thickest/lowest string = 1
    float frequency;    // Hz, equal temperament A4=440
};

struct TuningProfile {
    const char*  name;
    StringTarget strings[8]; // max 8 strings
    int          stringCount;
};

// Standard tunings — frequencies in Hz (equal temperament, A4=440)
namespace tuning_presets {

inline constexpr TuningProfile guitar_standard = {
    "guitar_standard",
    {{"E2",1,82.41f},{"A2",2,110.00f},{"D3",3,146.83f},
     {"G3",4,196.00f},{"B3",5,246.94f},{"E4",6,329.63f}},
    6
};

inline constexpr TuningProfile guitar_drop_d = {
    "guitar_drop_d",
    {{"D2",1,73.42f},{"A2",2,110.00f},{"D3",3,146.83f},
     {"G3",4,196.00f},{"B3",5,246.94f},{"E4",6,329.63f}},
    6
};

inline constexpr TuningProfile guitar_open_g = {
    "guitar_open_g",
    {{"D2",1,73.42f},{"G2",2,98.00f},{"D3",3,146.83f},
     {"G3",4,196.00f},{"B3",5,246.94f},{"D4",6,293.66f}},
    6
};

inline constexpr TuningProfile bass_standard = {
    "bass_standard",
    {{"E1",1,41.20f},{"A1",2,55.00f},{"D2",3,73.42f},{"G2",4,98.00f}},
    4
};

inline constexpr TuningProfile bass_drop_d = {
    "bass_drop_d",
    {{"D1",1,36.71f},{"A1",2,55.00f},{"D2",3,73.42f},{"G2",4,98.00f}},
    4
};

inline constexpr TuningProfile violin_standard = {
    "violin_standard",
    {{"G3",1,196.00f},{"D4",2,293.66f},{"A4",3,440.00f},{"E5",4,659.26f}},
    4
};

inline constexpr TuningProfile viola_standard = {
    "viola_standard",
    {{"C3",1,130.81f},{"G3",2,196.00f},{"D4",3,293.66f},{"A4",4,440.00f}},
    4
};

inline constexpr TuningProfile cello_standard = {
    "cello_standard",
    {{"C2",1,65.41f},{"G2",2,98.00f},{"D3",3,146.83f},{"A3",4,220.00f}},
    4
};

inline constexpr TuningProfile ukulele_standard = {
    "ukulele_standard",
    {{"G4",1,392.00f},{"C4",2,261.63f},{"E4",3,329.63f},{"A4",4,440.00f}},
    4
};

} // namespace tuning_presets

// Returns a pointer to a built-in TuningProfile by name, or nullptr for unknown names.
inline const TuningProfile* tuningPreset(const std::string& name) {
    if (name == "guitar_standard") return &tuning_presets::guitar_standard;
    if (name == "guitar_drop_d")   return &tuning_presets::guitar_drop_d;
    if (name == "guitar_open_g")   return &tuning_presets::guitar_open_g;
    if (name == "bass_standard")   return &tuning_presets::bass_standard;
    if (name == "bass_drop_d")     return &tuning_presets::bass_drop_d;
    if (name == "violin_standard") return &tuning_presets::violin_standard;
    if (name == "viola_standard")  return &tuning_presets::viola_standard;
    if (name == "cello_standard")  return &tuning_presets::cello_standard;
    if (name == "ukulele_standard")return &tuning_presets::ukulele_standard;
    return nullptr;
}

// Returns the default tuning name for a given instrument name (mirrors InstrumentPresets).
// Returns "" if the instrument has no default tuning (chromatic, mandolin, banjo).
inline std::string defaultTuningForInstrument(const std::string& instrument) {
    if (instrument == "guitar")  return "guitar_standard";
    if (instrument == "bass")    return "bass_standard";
    if (instrument == "violin")  return "violin_standard";
    if (instrument == "viola")   return "viola_standard";
    if (instrument == "cello")   return "cello_standard";
    if (instrument == "ukulele") return "ukulele_standard";
    return "";
}
