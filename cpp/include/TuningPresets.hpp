#pragma once

#include <string>

struct StringTarget {
    const char* name;         // e.g. "E2", "A2", "D3"
    int   stringNumber;       // 1-based, thickest/lowest string = 1
    float frequency;          // Hz, equal temperament A4=440
    // Octave-paired course: the course also carries a string one octave above
    // (+1) or below (-1) `frequency`. 0 = unison course (every existing preset).
    int   octaveOffset = 0;
};

struct TuningProfile {
    const char*  name;
    StringTarget strings[8]; // max 8 strings / courses
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

inline constexpr TuningProfile guitar_eb_standard = {
    "guitar_eb_standard",
    {{"Eb2",1,77.78f},{"Ab2",2,103.83f},{"Db3",3,138.59f},
     {"Gb3",4,185.00f},{"Bb3",5,233.08f},{"Eb4",6,311.13f}},
    6
};

inline constexpr TuningProfile guitar_d_standard = {
    "guitar_d_standard",
    {{"D2",1,73.42f},{"G2",2,98.00f},{"C3",3,130.81f},
     {"F3",4,174.61f},{"A3",5,220.00f},{"D4",6,293.66f}},
    6
};

inline constexpr TuningProfile guitar_drop_c = {
    "guitar_drop_c",
    {{"C2",1,65.41f},{"G2",2,98.00f},{"C3",3,130.81f},
     {"F3",4,174.61f},{"A3",5,220.00f},{"D4",6,293.66f}},
    6
};

inline constexpr TuningProfile guitar_open_g = {
    "guitar_open_g",
    {{"D2",1,73.42f},{"G2",2,98.00f},{"D3",3,146.83f},
     {"G3",4,196.00f},{"B3",5,246.94f},{"D4",6,293.66f}},
    6
};

inline constexpr TuningProfile guitar_open_d = {
    "guitar_open_d",
    {{"D2",1,73.42f},{"A2",2,110.00f},{"D3",3,146.83f},
     {"F#3",4,185.00f},{"A3",5,220.00f},{"D4",6,293.66f}},
    6
};

inline constexpr TuningProfile guitar_open_c = {
    "guitar_open_c",
    {{"C2",1,65.41f},{"G2",2,98.00f},{"C3",3,130.81f},
     {"G3",4,196.00f},{"C4",5,261.63f},{"E4",6,329.63f}},
    6
};

inline constexpr TuningProfile guitar_dadgad = {
    "guitar_dadgad",
    {{"D2",1,73.42f},{"A2",2,110.00f},{"D3",3,146.83f},
     {"G3",4,196.00f},{"A3",5,220.00f},{"D4",6,293.66f}},
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

// --- Oud: six unison courses, lowest course first ---

// Turkish (Bolahenk) tuning — the most common modern Turkish setup.
inline constexpr TuningProfile oud_turkish = {
    "oud_turkish",
    {{"C#2",1,69.30f},{"F#2",2,92.50f},{"B2",3,123.47f},
     {"E3",4,164.81f},{"A3",5,220.00f},{"D4",6,293.66f}},
    6
};

// Turkish variant with the two bass courses on D and A.
inline constexpr TuningProfile oud_turkish_d = {
    "oud_turkish_d",
    {{"D2",1,73.42f},{"A2",2,110.00f},{"B2",3,123.47f},
     {"E3",4,164.81f},{"A3",5,220.00f},{"D4",6,293.66f}},
    6
};

// Standard Arabic tuning (a whole step below Turkish on the top four courses).
inline constexpr TuningProfile oud_arabic = {
    "oud_arabic",
    {{"C2",1,65.41f},{"F2",2,87.31f},{"A2",3,110.00f},
     {"D3",4,146.83f},{"G3",5,196.00f},{"C4",6,261.63f}},
    6
};

// Older Arabic / Egyptian pattern.
inline constexpr TuningProfile oud_arabic_old = {
    "oud_arabic_old",
    {{"D2",1,73.42f},{"G2",2,98.00f},{"A2",3,110.00f},
     {"D3",4,146.83f},{"G3",5,196.00f},{"C4",6,261.63f}},
    6
};

// Higher Arabic / Iraqi tuning with a top F course.
inline constexpr TuningProfile oud_arabic_high = {
    "oud_arabic_high",
    {{"F2",1,87.31f},{"A2",2,110.00f},{"D3",3,146.83f},
     {"G3",4,196.00f},{"C4",5,261.63f},{"F4",6,349.23f}},
    6
};

// --- Cümbüş: six unison courses. Any oud preset also applies. ---

inline constexpr TuningProfile cumbus_standard = {
    "cumbus_standard",
    {{"A2",1,110.00f},{"B2",2,123.47f},{"E3",3,164.81f},
     {"A3",4,220.00f},{"D4",5,293.66f},{"G4",6,392.00f}},
    6
};

// --- Bağlama: three courses, physical order üst (1) → orta (2) → alt (3).
// Notated in Turkish folk convention with the alt (melody) course on A3; the
// real pitch is set by the player, so pair these with setTuningTranspose().
// The alt course carries a wound string an octave below (A2); the orta course
// a plain string an octave above (D4). The üst course is usually unison.

inline constexpr TuningProfile baglama_kara = {
    "baglama_kara",
    {{"G3",1,196.00f,0},{"D3",2,146.83f,+1},{"A3",3,220.00f,-1}},
    3
};

inline constexpr TuningProfile baglama_baglama = {
    "baglama_baglama",
    {{"E3",1,164.81f,0},{"D3",2,146.83f,+1},{"A3",3,220.00f,-1}},
    3
};

inline constexpr TuningProfile baglama_misket = {
    "baglama_misket",
    {{"F#3",1,185.00f,0},{"D3",2,146.83f,+1},{"A3",3,220.00f,-1}},
    3
};

inline constexpr TuningProfile baglama_mustezat = {
    "baglama_mustezat",
    {{"F3",1,174.61f,0},{"D3",2,146.83f,+1},{"A3",3,220.00f,-1}},
    3
};

inline constexpr TuningProfile baglama_abdal = {
    "baglama_abdal",
    {{"G3",1,196.00f,0},{"A3",2,220.00f,-1},{"A3",3,220.00f,-1}},
    3
};

inline constexpr TuningProfile baglama_fidayda = {
    "baglama_fidayda",
    {{"D3",1,146.83f,0},{"D3",2,146.83f,+1},{"A3",3,220.00f,-1}},
    3
};

// --- Bouzouki: octave pairs on the bass courses only ---

// Greek four-course (tetrachordo): C and F courses are octave pairs.
inline constexpr TuningProfile bouzouki_tetrachordo = {
    "bouzouki_tetrachordo",
    {{"C3",1,130.81f,+1},{"F3",2,174.61f,+1},{"A3",3,220.00f},{"D4",4,293.66f}},
    4
};

// Greek three-course (trichordo): the D course is an octave pair.
inline constexpr TuningProfile bouzouki_trichordo = {
    "bouzouki_trichordo",
    {{"D3",1,146.83f,+1},{"A3",2,220.00f},{"D4",3,293.66f}},
    3
};

// Irish bouzouki GDAD: G and D courses are commonly octave pairs.
inline constexpr TuningProfile bouzouki_irish = {
    "bouzouki_irish",
    {{"G2",1,98.00f,+1},{"D3",2,146.83f,+1},{"A3",3,220.00f},{"D4",4,293.66f}},
    4
};

} // namespace tuning_presets

// Returns a pointer to a built-in TuningProfile by name, or nullptr for unknown names.
inline const TuningProfile* tuningPreset(const std::string& name) {
    if (name == "guitar_standard")    return &tuning_presets::guitar_standard;
    if (name == "guitar_eb_standard") return &tuning_presets::guitar_eb_standard;
    if (name == "guitar_d_standard")  return &tuning_presets::guitar_d_standard;
    if (name == "guitar_drop_d")      return &tuning_presets::guitar_drop_d;
    if (name == "guitar_drop_c")      return &tuning_presets::guitar_drop_c;
    if (name == "guitar_open_g")      return &tuning_presets::guitar_open_g;
    if (name == "guitar_open_d")      return &tuning_presets::guitar_open_d;
    if (name == "guitar_open_c")      return &tuning_presets::guitar_open_c;
    if (name == "guitar_dadgad")      return &tuning_presets::guitar_dadgad;
    if (name == "bass_standard")      return &tuning_presets::bass_standard;
    if (name == "bass_drop_d")        return &tuning_presets::bass_drop_d;
    if (name == "violin_standard")    return &tuning_presets::violin_standard;
    if (name == "viola_standard")     return &tuning_presets::viola_standard;
    if (name == "cello_standard")     return &tuning_presets::cello_standard;
    if (name == "ukulele_standard")   return &tuning_presets::ukulele_standard;
    if (name == "oud_turkish")        return &tuning_presets::oud_turkish;
    if (name == "oud_turkish_d")      return &tuning_presets::oud_turkish_d;
    if (name == "oud_arabic")         return &tuning_presets::oud_arabic;
    if (name == "oud_arabic_old")     return &tuning_presets::oud_arabic_old;
    if (name == "oud_arabic_high")    return &tuning_presets::oud_arabic_high;
    if (name == "cumbus_standard")    return &tuning_presets::cumbus_standard;
    if (name == "baglama_kara")       return &tuning_presets::baglama_kara;
    if (name == "baglama_baglama")    return &tuning_presets::baglama_baglama;
    if (name == "baglama_misket")     return &tuning_presets::baglama_misket;
    if (name == "baglama_mustezat")   return &tuning_presets::baglama_mustezat;
    if (name == "baglama_abdal")      return &tuning_presets::baglama_abdal;
    if (name == "baglama_fidayda")    return &tuning_presets::baglama_fidayda;
    if (name == "bouzouki_tetrachordo") return &tuning_presets::bouzouki_tetrachordo;
    if (name == "bouzouki_trichordo") return &tuning_presets::bouzouki_trichordo;
    if (name == "bouzouki_irish")     return &tuning_presets::bouzouki_irish;
    return nullptr;
}

// Returns the default tuning name for a given instrument name (mirrors InstrumentPresets).
// Returns "" if the instrument has no default tuning (chromatic, mandolin, banjo).
inline std::string defaultTuningForInstrument(const std::string& instrument) {
    if (instrument == "guitar")   return "guitar_standard";
    if (instrument == "bass")     return "bass_standard";
    if (instrument == "violin")   return "violin_standard";
    if (instrument == "viola")    return "viola_standard";
    if (instrument == "cello")    return "cello_standard";
    if (instrument == "ukulele")  return "ukulele_standard";
    if (instrument == "oud")      return "oud_turkish";
    if (instrument == "cumbus")   return "cumbus_standard";
    if (instrument == "baglama")  return "baglama_kara";
    if (instrument == "bouzouki") return "bouzouki_tetrachordo";
    return "";
}
