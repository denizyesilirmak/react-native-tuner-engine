// Offline evaluation of TunerEngine on WAV recordings.
//
//   tuner_engine_eval manifest.csv [--frame 2048] [--overlap 0.5] [--gate DB]
//                                  [--hold-min C] [--hold-cents C] [--hold-miss N] [--no-hold]
//                                  [--frames-dir DIR] [--max-gross PCT]
//
// manifest.csv: `file,expected[,instrument]`, paths relative to the manifest.
// `expected` is a note name (A2, F#3, Bb1), a frequency in Hz, or `none` for
// files that contain no note (noise only).
//
// Frames are fed exactly like AudioFrameDispatcher does: one full frame first,
// then `hop = round(frame * (1 - overlap))` new samples per step.
//
// "Active" / "audible" frames are defined from the audio alone (per-file noise
// floor + 12 / + 6 dB), never from the engine's gate, so a gated audible note
// shows up as a drop instead of disappearing from the statistics.

#include "TunerEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int   kOnsetBlock    = 256;
constexpr float kActiveAboveDb = 12.0f;
constexpr float kAudibleAboveDb = 6.0f;
constexpr float kGrossCents    = 50.0f;
constexpr float kLockCents     = 5.0f;
constexpr int   kLockFrames    = 5;
constexpr float kMinLinear     = 1e-7f;

// ---------------------------------------------------------------- WAV reader

struct Audio {
    std::vector<float> mono;
    int sampleRate = 0;
};

uint32_t rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
uint16_t rd16(const unsigned char* p) { return uint16_t(p[0] | (p[1] << 8)); }

bool readWav(const std::string& path, Audio& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "cannot open"; return false; }
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) {
        err = "not a RIFF/WAVE file";
        return false;
    }

    int format = 0, channels = 0, bits = 0, rate = 0;
    const unsigned char* data = nullptr;
    size_t dataLen = 0;

    size_t pos = 12;
    while (pos + 8 <= d.size()) {
        const unsigned char* c = d.data() + pos;
        size_t len = rd32(c + 4);
        const size_t body = pos + 8;
        if (body + len > d.size()) len = d.size() - body; // truncated / streaming header

        if (std::memcmp(c, "fmt ", 4) == 0 && len >= 16) {
            format   = rd16(c + 8);
            channels = rd16(c + 10);
            rate     = int(rd32(c + 12));
            bits     = rd16(c + 22);
            if (format == 0xFFFE && len >= 26) format = rd16(c + 8 + 24); // sub-format GUID, first 2 bytes
        } else if (std::memcmp(c, "data", 4) == 0) {
            data = d.data() + body;
            dataLen = len;
            break;
        }
        pos = body + len + (len & 1);
    }

    if (!data || channels < 1 || rate <= 0) { err = "missing fmt/data chunk"; return false; }
    const bool isFloat = format == 3;
    if (!(format == 1 || isFloat)) { err = "unsupported format tag " + std::to_string(format); return false; }
    if (isFloat ? bits != 32 : !(bits == 16 || bits == 24 || bits == 32)) {
        err = "unsupported bit depth " + std::to_string(bits);
        return false;
    }

    const size_t bytes = size_t(bits / 8);
    const size_t frames = dataLen / (bytes * size_t(channels));
    out.sampleRate = rate;
    out.mono.assign(frames, 0.0f);

    for (size_t i = 0; i < frames; ++i) {
        double acc = 0.0;
        for (int ch = 0; ch < channels; ++ch) {
            const unsigned char* p = data + (i * size_t(channels) + size_t(ch)) * bytes;
            double v;
            if (isFloat) {
                float x; std::memcpy(&x, p, 4); v = x;
            } else if (bits == 16) {
                v = int16_t(rd16(p)) / 32768.0;
            } else if (bits == 24) {
                int32_t x = (p[0] << 8) | (p[1] << 16) | (int32_t(p[2]) << 24);
                v = (x >> 8) / 8388608.0;
            } else {
                v = int32_t(rd32(p)) / 2147483648.0;
            }
            acc += v;
        }
        out.mono[i] = float(acc / channels);
    }
    return true;
}

// ------------------------------------------------------------------ helpers

float acRmsDb(const float* x, int n) {
    double mean = 0.0;
    for (int i = 0; i < n; ++i) mean += x[i];
    mean /= n;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) { const double v = x[i] - mean; sum += v * v; }
    const float rms = float(std::sqrt(sum / n));
    return 20.0f * std::log10(std::max(rms, kMinLinear));
}

float percentile(std::vector<float> v, float p) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    const size_t idx = std::min(v.size() - 1, size_t(std::floor(p * float(v.size() - 1) + 0.5f)));
    return v[idx];
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

// "A2", "F#3", "Bb1" or a number in Hz. Returns 0 for "none", -1 for garbage.
float parseExpected(const std::string& text) {
    if (text == "none" || text.empty()) return 0.0f;
    char* end = nullptr;
    const float hz = std::strtof(text.c_str(), &end);
    if (end && *end == '\0' && hz > 0.0f) return hz;

    static const int semis[7] = {9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
    const char letter = char(std::toupper(text[0]));
    if (letter < 'A' || letter > 'G') return -1.0f;
    int semi = semis[letter - 'A'];
    size_t i = 1;
    if (i < text.size() && text[i] == '#') { ++semi; ++i; }
    else if (i < text.size() && text[i] == 'b') { --semi; ++i; }
    if (i >= text.size()) return -1.0f;
    const int octave = std::atoi(text.c_str() + i);
    const int midi = 12 * (octave + 1) + semi;
    return 440.0f * std::pow(2.0f, float(midi - 69) / 12.0f);
}

const char* stageName(PitchStage s) {
    switch (s) {
        case PitchStage::Ok:            return "ok";
        case PitchStage::Gated:         return "gated";
        case PitchStage::Unvoiced:      return "unvoiced";
        case PitchStage::LowConfidence: return "lowconf";
        case PitchStage::Settling:      return "settling";
    }
    return "?";
}

// ------------------------------------------------------------------ per file

struct Frame {
    double tMs;     // end of the analysis window = when the result exists
    long   endSample;
    float  rmsDb;
    PitchResult r;
    float  errCents = 0.0f;
};

struct FileStats {
    std::string name;
    bool hasNote = false;
    int  frames = 0, active = 0, pitchedActive = 0;
    float medianAbs = 0, p95Abs = 0;
    int  gross = 0, octave = 0;
    double firstPitchMs = -1, lockMs = -1;
    int  falseFrames = 0, falseConsidered = 0;
    int  drops[5] = {0, 0, 0, 0, 0};
    double tailMs = -1; // -1 = n/a
    std::vector<float> absCents;
};

struct Options {
    std::string manifest;
    int    frame = 2048;
    float  overlap = 0.5f;
    bool   hasGate = false;
    float  gate = 0.0f;
    std::string framesDir;
    bool   holdTouched = false;
    Pipeline::NoteHold hold;
    bool   hasMaxGross = false;
    float  maxGross = 0.0f;
};

FileStats evaluate(const std::string& label, float expectedHz,
                   const std::string& instrument, const Audio& audio, const Options& opt) {
    FileStats st;
    st.name = label;
    st.hasNote = expectedHz > 0.0f;

    const float sr = float(audio.sampleRate);
    const int   frameSize = opt.frame;
    const int   hop = std::max(1, int(std::lround(frameSize * (1.0f - opt.overlap))));
    const std::vector<float>& x = audio.mono;

    TunerEngine engine(sr, frameSize);
    if (opt.hasGate) engine.setNoiseGateDb(opt.gate); // only on request: don't hide the engine default
    if (opt.holdTouched) engine.setNoteHold(opt.hold); // only when a --hold-* flag was passed
    if (!instrument.empty()) engine.setInstrument(instrument);

    std::vector<Frame> frames;
    for (long start = 0; start + frameSize <= long(x.size()); start += hop) {
        // dispatcher: first frame is [0, frame), then the window slides by hop
        Frame fr;
        fr.endSample = start + frameSize;
        fr.tMs = 1000.0 * double(fr.endSample) / sr;
        fr.rmsDb = acRmsDb(x.data() + start, frameSize);
        fr.r = engine.process(x.data() + start, frameSize);
        if (fr.r.hasPitch && expectedHz > 0.0f) fr.errCents = 1200.0f * std::log2(fr.r.frequency / expectedHz);
        frames.push_back(fr);
    }
    st.frames = int(frames.size());
    if (frames.empty()) return st;

    std::vector<float> levels;
    for (const auto& f : frames) levels.push_back(f.rmsDb);
    const float floorDb   = percentile(levels, 0.10f);
    const float activeDb  = floorDb + kActiveAboveDb;
    const float audibleDb = floorDb + kAudibleAboveDb;

    // Sample-accurate onset: first 256-sample block above the active level.
    long onset = -1;
    if (st.hasNote) {
        for (long s = 0; s + kOnsetBlock <= long(x.size()); s += kOnsetBlock) {
            if (acRmsDb(x.data() + s, kOnsetBlock) >= activeDb) { onset = s; break; }
        }
    }
    const double onsetMs = onset >= 0 ? 1000.0 * double(onset) / sr : 0.0;

    int lockRun = 0;
    double lastAudibleMs = -1, lastPitchedMs = -1;

    for (auto& f : frames) {
        const bool pitched = f.r.hasPitch;
        const bool active  = f.rmsDb >= activeDb;
        const bool beforeOnset = st.hasNote && (onset < 0 || f.endSample <= onset);

        if (!st.hasNote || beforeOnset) {
            ++st.falseConsidered;
            if (pitched) ++st.falseFrames;
        }
        if (!st.hasNote) continue;
        if (beforeOnset) { lockRun = 0; continue; }

        if (f.rmsDb >= audibleDb) lastAudibleMs = f.tMs;
        if (pitched) lastPitchedMs = f.tMs;

        if (active) {
            ++st.active;
            if (pitched) {
                ++st.pitchedActive;
                const float a = std::fabs(f.errCents);
                st.absCents.push_back(a);
                if (a > kGrossCents) {
                    ++st.gross;
                    const float nearestOct = 1200.0f * std::round(f.errCents / 1200.0f);
                    if (nearestOct != 0.0f && std::fabs(f.errCents - nearestOct) <= kGrossCents) ++st.octave;
                }
            } else {
                ++st.drops[int(f.r.stage)];
            }
        }

        if (pitched) {
            if (st.firstPitchMs < 0) st.firstPitchMs = f.tMs - onsetMs;
            lockRun = std::fabs(f.errCents) <= kLockCents ? lockRun + 1 : 0;
            if (lockRun == kLockFrames && st.lockMs < 0) st.lockMs = f.tMs - onsetMs;
        } else {
            lockRun = 0;
        }
    }

    if (st.hasNote && onset >= 0 && lastAudibleMs >= 0) {
        const double pitchedUntil = lastPitchedMs >= 0 ? lastPitchedMs : onsetMs;
        st.tailMs = std::max(0.0, lastAudibleMs - pitchedUntil);
    }
    st.medianAbs = percentile(st.absCents, 0.5f);
    st.p95Abs    = percentile(st.absCents, 0.95f);

    if (!opt.framesDir.empty()) {
        std::string base = label;
        for (char& c : base) if (c == '/' || c == '\\') c = '_';
        std::ofstream out(opt.framesDir + "/" + base + ".csv");
        out << "time_ms,rms_db,has_pitch,frequency,confidence,note,error_cents,stage,detector_confidence,snr_db\n";
        for (const auto& f : frames) {
            out << f.tMs << ',' << f.rmsDb << ',' << (f.r.hasPitch ? 1 : 0) << ','
                << f.r.frequency << ',' << f.r.confidence << ','
                << (f.r.hasPitch ? f.r.noteName + std::to_string(f.r.octave) : std::string()) << ','
                << (f.r.hasPitch && st.hasNote ? f.errCents : 0.0f) << ','
                << stageName(f.r.stage) << ',' << f.r.detectorConfidence << ',' << f.r.snrDb << '\n';
        }
    }
    return st;
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) { std::cerr << name << " needs a value\n"; std::exit(2); }
            return argv[++i];
        };
        if      (a == "--frame")      opt.frame = std::atoi(next("--frame"));
        else if (a == "--overlap")    opt.overlap = std::clamp(float(std::atof(next("--overlap"))), 0.0f, 0.75f);
        else if (a == "--gate")       { opt.hasGate = true; opt.gate = float(std::atof(next("--gate"))); }
        else if (a == "--hold-min")   { opt.holdTouched = true; opt.hold.minConfidence = float(std::atof(next("--hold-min"))); }
        else if (a == "--hold-cents") { opt.holdTouched = true; opt.hold.maxCents = float(std::atof(next("--hold-cents"))); }
        else if (a == "--hold-miss")  { opt.holdTouched = true; opt.hold.maxMissedFrames = std::atoi(next("--hold-miss")); }
        else if (a == "--no-hold")    { opt.holdTouched = true; opt.hold.enabled = false; }
        else if (a == "--frames-dir") opt.framesDir = next("--frames-dir");
        else if (a == "--max-gross")  { opt.hasMaxGross = true; opt.maxGross = float(std::atof(next("--max-gross"))); }
        else if (!a.empty() && a[0] != '-' && opt.manifest.empty()) opt.manifest = a;
        else { std::cerr << "unknown argument: " << a << "\n"; return 2; }
    }
    if (opt.manifest.empty() || opt.frame < 256) {
        std::cerr << "usage: tuner_engine_eval manifest.csv [--frame N] [--overlap R] [--gate DB]"
                     " [--hold-min C] [--hold-cents C] [--hold-miss N] [--no-hold]"
                     " [--frames-dir DIR] [--max-gross PCT]\n";
        return 2;
    }

    std::ifstream mf(opt.manifest);
    if (!mf) { std::cerr << "cannot open manifest " << opt.manifest << "\n"; return 2; }
    std::string dir;
    {
        const size_t slash = opt.manifest.find_last_of("/\\");
        if (slash != std::string::npos) dir = opt.manifest.substr(0, slash + 1);
    }
    if (!opt.framesDir.empty()) {
        std::string cmd = "mkdir -p '" + opt.framesDir + "'";
        if (std::system(cmd.c_str()) != 0) { std::cerr << "cannot create " << opt.framesDir << "\n"; return 2; }
    }

    std::cout << "frame=" << opt.frame << " overlap=" << opt.overlap;
    if (opt.hasGate) std::cout << " gate=" << opt.gate << " dB"; else std::cout << " gate=engine default";
    std::cout << "\n\n";
    std::printf("%-28s %-8s %6s %5s %7s %7s %5s %5s %7s %7s %6s %7s  %s\n",
                "file", "expected", "active", "voic%", "med|c|", "p95|c|", "gross", "oct",
                "1st ms", "lock ms", "false%", "tail ms", "drops g/u/l/s");

    std::vector<FileStats> all;
    std::string line;
    while (std::getline(mf, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cols;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, ',')) cols.push_back(trim(cell));
        if (cols.size() < 2 || cols[0] == "file") continue;

        const float expected = parseExpected(cols[1]);
        if (expected < 0.0f) { std::cerr << "skip " << cols[0] << ": bad expected '" << cols[1] << "'\n"; continue; }

        Audio audio;
        std::string err;
        if (!readWav(dir + cols[0], audio, err)) { std::cerr << "skip " << cols[0] << ": " << err << "\n"; continue; }
        if (long(audio.mono.size()) < opt.frame) { std::cerr << "skip " << cols[0] << ": shorter than one frame\n"; continue; }

        const std::string instrument = cols.size() > 2 ? cols[2] : "";
        FileStats st = evaluate(cols[0], expected, instrument, audio, opt);

        const double voiced = st.active ? 100.0 * st.pitchedActive / st.active : 0.0;
        const double falsePct = st.falseConsidered ? 100.0 * st.falseFrames / st.falseConsidered : 0.0;
        std::printf("%-28s %-8s %6d %5.1f %7.2f %7.2f %5d %5d %7.0f %7.0f %6.1f %7.0f  %d/%d/%d/%d\n",
                    st.name.c_str(), cols[1].c_str(), st.active, voiced, st.medianAbs, st.p95Abs,
                    st.gross, st.octave, st.firstPitchMs, st.lockMs, falsePct, st.tailMs,
                    st.drops[int(PitchStage::Gated)], st.drops[int(PitchStage::Unvoiced)],
                    st.drops[int(PitchStage::LowConfidence)], st.drops[int(PitchStage::Settling)]);
        all.push_back(std::move(st));
    }

    // ------------------------------------------------------------- summary
    int noteFiles = 0, noiseFiles = 0, active = 0, pitched = 0, gross = 0, octave = 0;
    int falseFrames = 0, falseConsidered = 0, drops[5] = {0, 0, 0, 0, 0};
    std::vector<float> absAll, firstMs, lockMs, tails;
    std::vector<std::pair<double, std::string>> tailByFile;
    for (const auto& s : all) {
        (s.hasNote ? noteFiles : noiseFiles)++;
        active += s.active; pitched += s.pitchedActive; gross += s.gross; octave += s.octave;
        falseFrames += s.falseFrames; falseConsidered += s.falseConsidered;
        for (int i = 0; i < 5; ++i) drops[i] += s.drops[i];
        absAll.insert(absAll.end(), s.absCents.begin(), s.absCents.end());
        if (s.firstPitchMs >= 0) firstMs.push_back(float(s.firstPitchMs));
        if (s.lockMs >= 0) lockMs.push_back(float(s.lockMs));
        if (s.tailMs >= 0) { tails.push_back(float(s.tailMs)); tailByFile.push_back({s.tailMs, s.name}); }
    }
    const int dropTotal = drops[1] + drops[2] + drops[3] + drops[4];
    const double grossPct = pitched ? 100.0 * gross / pitched : 0.0;

    std::printf("\n=== summary ===\n");
    std::printf("files: %d with note, %d noise-only\n", noteFiles, noiseFiles);
    std::printf("active frames: %d, pitched: %d (voiced %.1f%%)\n", active, pitched,
                active ? 100.0 * pitched / active : 0.0);
    std::printf("dropped active frames: %d  gated %d (%.0f%%)  unvoiced %d (%.0f%%)  lowconf %d (%.0f%%)  settling %d (%.0f%%)\n",
                dropTotal,
                drops[1], dropTotal ? 100.0 * drops[1] / dropTotal : 0.0,
                drops[2], dropTotal ? 100.0 * drops[2] / dropTotal : 0.0,
                drops[3], dropTotal ? 100.0 * drops[3] / dropTotal : 0.0,
                drops[4], dropTotal ? 100.0 * drops[4] / dropTotal : 0.0);
    std::printf("|cents| median %.2f  p95 %.2f   gross(>50c) %d (%.2f%%)  octave %d\n",
                percentile(absAll, 0.5f), percentile(absAll, 0.95f), gross, grossPct, octave);
    std::printf("first pitch ms median %.0f   lock ms median %.0f (%zu/%d files locked)\n",
                percentile(firstMs, 0.5f), percentile(lockMs, 0.5f), lockMs.size(), noteFiles);
    std::printf("false pitch: %d / %d frames = %.2f%%\n", falseFrames, falseConsidered,
                falseConsidered ? 100.0 * falseFrames / falseConsidered : 0.0);
    std::printf("tail ms (needle gone before note fades): median %.0f  p90 %.0f  worst %.0f\n",
                percentile(tails, 0.5f), percentile(tails, 0.9f), percentile(tails, 1.0f));

    std::sort(tailByFile.begin(), tailByFile.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::printf("worst tails:\n");
    for (size_t i = 0; i < tailByFile.size() && i < 8; ++i) {
        std::printf("  %7.0f ms  %s\n", tailByFile[i].first, tailByFile[i].second.c_str());
    }

    if (opt.hasMaxGross && grossPct > opt.maxGross) {
        std::fprintf(stderr, "FAIL: gross error rate %.2f%% > %.2f%%\n", grossPct, opt.maxGross);
        return 1;
    }
    return 0;
}
