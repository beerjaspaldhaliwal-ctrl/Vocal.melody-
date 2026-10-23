// MelodyLift generator: pop chord progressions, hooks, basslines and counter
// melodies. Plain C++17, deterministic for a given (settings, seeds) pair, so
// moving a control re-renders the same idea instead of a random new one.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "MelodyCore.h"

namespace ml::gen
{

enum Style { Pop = 0, MelodicRap, PunjabiPop, Drill, Afrobeats, DancePop, Ballad, Trap, NumStyles };
enum Layer { Melody = 0, Chords, Bass, Counter, NumLayers };

const char* styleName(int style);
const char* layerName(int layer);
int defaultScale(int style);

struct Settings
{
    int style = Pop;
    int tonic = 0;      // pitch class of the key
    int scale = -1;     // ml::Scale, -1 = the style's default
    int bars = 8;       // 4, 8 or 16
    double density = 0.5;
    double syncopation = 0.5;
    double repetition = 0.7; // how strongly the hook repeats its motif
    double complexity = 0.35;
    double ornaments = 0.5;
    int range = 1;  // 0 narrow, 1 medium, 2 wide
    int octave = 0; // -1, 0, +1
    double humanize = 0.2;
    double swing = 0.0;
};

/** One seed per independently re-rollable part. */
struct Seeds
{
    uint32_t chords = 1, rhythm = 2, pitch = 3, bass = 4, counter = 5;
};

struct GNote
{
    double beat = 0, len = 0.5;
    int pitch = 60;
    int vel = 96;
};

struct Chord
{
    double beat = 0, len = 4;
    int root = 0;              // semitones above the tonic
    std::vector<int> pcs;      // pitch classes (absolute), triad + extensions
    std::vector<int> voicing;  // MIDI notes for the chord layer
    std::string name;          // e.g. "Am7"
    std::string roman;         // e.g. "vi"
};

struct Pattern
{
    int style = Pop, tonic = 0, scale = Major, bars = 8;
    double beats = 32;
    std::vector<Chord> chords;
    std::vector<GNote> layers[NumLayers];
    double hookScore = 0; // 0..10
};

struct HookStats
{
    double chordToneStrong = 0; // share of strong-beat notes that are chord tones
    double stepRatio = 0;       // share of steps / repeats among intervals
    int span = 0;               // semitones
    int distinct = 0;           // distinct pitches
    int bigLeaps = 0;           // leaps over a fifth (and tritones)
    double score = 0;           // 0..10
};

Pattern generate(const Settings& s, const Seeds& seeds);

/** Chords, bass and counter for an existing melody (e.g. a captured vocal). */
Pattern buildAround(const std::vector<GNote>& melody, double beats, const Settings& s, const Seeds& seeds);

/** Best-fitting chord per bar for a melody (Viterbi over diatonic chords). */
std::vector<Chord> harmonize(const std::vector<GNote>& melody, double beats, int tonic, int scale, int style);

HookStats analyseHook(const std::vector<GNote>& melody, const std::vector<Chord>& chords, int tonic, int scale, int style);

/** Multi-track (type 1) MIDI file, one track per layer. */
struct MidiTrack
{
    std::string name;
    std::vector<GNote> notes;
    int channel = 0;
};
std::vector<uint8_t> writeMidiTracks(const std::vector<MidiTrack>& tracks, double bpm, int ppq = 480);

} // namespace ml::gen
