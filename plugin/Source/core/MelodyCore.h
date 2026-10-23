// MelodyLift core: pitch tracking, melody transcription and MIDI writing.
// Plain C++17 with no JUCE dependency, so it can be unit-tested on its own.
// This is a port of src/dsp.js, src/notes.js and src/midi.js.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ml
{

constexpr double kAnalysisRate = 16000.0;

struct PitchTrack
{
    double sr = kAnalysisRate;
    int hop = 160;
    double fmin = 65.0, fmax = 1100.0;
    std::vector<float> f0;    // Hz, 0 = no periodicity
    std::vector<float> conf;  // 1 - aperiodicity
    std::vector<float> rmsDb; // long-window loudness (voicing gate)
    std::vector<float> envDb; // short-window loudness (onsets)

    double hopSec() const { return hop / sr; }
    size_t size() const { return f0.size(); }
    bool empty() const { return f0.empty(); }
};

struct Note
{
    int id = 0;
    int pitch = 60;       // current MIDI note
    int detPitch = 60;    // pitch the detector found (pitch bends are relative to it)
    double pitchRaw = 60; // median sung pitch, fractional
    double start = 0, end = 0; // seconds from song start
    double db = 0;
    int vel = 96;
    double srcStart = 0; // detection start time, before quantize/moves
    int srcA = -1, srcB = -1; // analysis frame range (-1 = drawn by hand)
};

struct TranscribeParams
{
    double confMin = 0.55;
    double gateDb = -45.0;
    double changeCost = 10.0;
    double silenceCost = 7.0;
    double sigma = 0.45;
    double minNoteMs = 70.0;
    double splitDb = 6.0;
    bool autoTuning = true;
    double tuningCents = 0.0;
};

struct Prepared
{
    std::vector<float> midi; // NaN = unvoiced
    std::vector<float> env;
    double tuning = 0.0; // semitones
};

struct KeyResult
{
    int tonic = -1; // -1 = unknown
    bool major = true;
    double score = 0.0;
};

struct Transcription
{
    Prepared prep;
    std::vector<Note> notes;
    KeyResult key;
};

enum Scale
{
    Major = 0, Minor, HarmonicMinor, Dorian, Mixolydian, PentatonicMajor, PentatonicMinor, NumScales
};

// grid: 0 off, 1 = 1/4, 2 = 1/8, 3 = 1/8T, 4 = 1/16, 5 = 1/16T, 6 = 1/32
double gridSeconds(double bpm, int grid);

struct PostParams
{
    bool snap = false;
    int tonic = 0;
    int scale = Major;
    int transpose = 0;
    double bpm = 120.0;
    int grid = 0;
    bool quantize = false;
    double strength = 1.0;
};

struct BendPoint
{
    double t;
    double v; // semitones
};

/** Progress callback: receives 0..1, returns false to cancel. */
using Progress = std::function<bool(float)>;

void highpass(float* x, size_t n, double sr, double freq, double q = 0.70710678);

/** Returns an empty track if cancelled. */
PitchTrack trackPitch(const float* x, size_t n, double sr, double fmin, double fmax,
                      const Progress& progress = {});

Transcription transcribe(const PitchTrack& track, const TranscribeParams& params);
KeyResult detectKey(const std::vector<Note>& notes);
bool inScale(int pitch, int tonic, int scale);
std::vector<Note> postProcess(const std::vector<Note>& notes, const PostParams& p);
std::vector<BendPoint> bendCurve(const Note& n, const Prepared& prep, const PitchTrack& track);
std::vector<BendPoint> buildBendEvents(const std::vector<Note>& notes, const Prepared& prep,
                                       const PitchTrack& track, double range);
std::vector<uint8_t> writeMidi(const std::vector<Note>& notes, double bpm, int ppq,
                               const std::string& name, const std::vector<BendPoint>* bends,
                               int bendRange, int timeSigNum = 4, int timeSigDenom = 4);

double hzToMidi(double f);
double midiToHz(double m);
std::string noteName(int pitch);
const char* pitchClassName(int pc);
int newNoteId();

/**
 * Streaming resampler to the analysis rate that writes into a buffer
 * addressed by absolute song position, so captured audio lines up with the
 * timeline even when playback jumps around. Real-time safe.
 */
class PositionalResampler
{
public:
    void prepare(double hostRate);
    void reset();
    /** Feed one mono host sample at absolute host sample index k. */
    inline void push(int64_t k, float x, float* out, size_t outSize, int64_t& maxWritten)
    {
        if (k != lastK + 1)
        {
            reset();
            prevPos = (double) k * ratio;
            prev = filter(x);
            lastK = k;
            return;
        }
        lastK = k;
        const float cur = filter(x);
        const double pos = (double) k * ratio;
        for (int64_t j = (int64_t) prevPos + 1; (double) j <= pos; ++j)
        {
            if (j < 0 || (size_t) j >= outSize) continue;
            const double frac = (j - prevPos) / (pos - prevPos);
            out[j] = (float) (prev + (cur - prev) * frac);
            if (j > maxWritten) maxWritten = j;
        }
        prevPos = pos;
        prev = cur;
    }

private:
    inline float filter(float x)
    {
        double y = x;
        for (auto& s : stages)
        {
            const double o = s.b0 * y + s.z1;
            s.z1 = s.b1 * y - s.a1 * o + s.z2;
            s.z2 = s.b2 * y - s.a2 * o;
            y = o;
        }
        return (float) y;
    }
    struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };
    Biquad stages[2];
    double ratio = 1.0;
    double prevPos = 0.0;
    float prev = 0.0f;
    int64_t lastK = -10;
};

/** One-shot resample of a whole mono signal to the analysis rate. */
std::vector<float> resampleToAnalysis(const float* x, size_t n, double sr);

} // namespace ml
