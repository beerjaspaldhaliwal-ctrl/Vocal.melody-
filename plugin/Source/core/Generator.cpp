#include "Generator.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace ml::gen
{

namespace
{
constexpr double kPi = 3.14159265358979323846;

// ------------------------------------------------------------------ RNG
// splitmix64: identical results on every platform and compiler.
struct Rng
{
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) { next(); }
    uint64_t next()
    {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uni() { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }
    int below(int n) { return n <= 0 ? 0 : (int) (uni() * n) % n; }
    bool chance(double p) { return uni() < p; }
    int weighted(const std::vector<double>& w)
    {
        double sum = 0;
        for (double v : w) sum += std::max(0.0, v);
        if (sum <= 0) return 0;
        double x = uni() * sum;
        for (size_t i = 0; i < w.size(); ++i)
        {
            x -= std::max(0.0, w[i]);
            if (x < 0) return (int) i;
        }
        return (int) w.size() - 1;
    }
};

uint64_t mix(uint64_t a, uint64_t b) { return a * 1000003ull ^ (b + 0x9E3779B9ull + (a << 6) + (a >> 2)); }

int pcOf(int p) { return ((p % 12) + 12) % 12; }

// --------------------------------------------------------------- styles

enum Contour { Arch, Falling, HighStart, RisingArch };
enum BassKind { BassPop, Bass808, BassEdm, BassBallad, BassBhangra, BassAfro };
enum CounterKind { Arp16, TripletBells, TumbiRiff, DrillAnswer, AfroMarimba, Broken8 };

struct StyleDef
{
    const char* name;
    int scale;
    std::vector<std::vector<const char*>> majorProgs, minorProgs;
    int ext;           // 0 triads, 1 sevenths, 2 ninths
    double density;    // melody notes per beat at density 0.5
    double sync;       // style syncopation bias
    double triplets;   // weight of triplet rhythm cells
    double legato;
    double repeatOk;   // tolerance for repeated notes
    double stepTarget; // ideal share of stepwise motion
    int center;        // melody centre (MIDI)
    double rangeMul;
    Contour contour;
    double ornament;
    BassKind bass;
    std::vector<const char*> bassPatterns;
    std::vector<const char*> chordPatterns;
    double chordLegato;
    CounterKind counter;
    int bassLow;
};

const StyleDef& style(int i)
{
    static const StyleDef defs[NumStyles] = {
        { "Pop Hit", Major,
          { { "I", "V", "vi", "IV" }, { "vi", "IV", "I", "V" }, { "I", "vi", "IV", "V" }, { "IV", "I", "V", "vi" },
            { "I", "IV", "vi", "V" }, { "I", "iii", "vi", "IV" }, { "vi", "V", "IV", "V" } },
          { { "i", "bVI", "bIII", "bVII" }, { "i", "bVII", "bVI", "bVII" }, { "i", "iv", "bVI", "bVII" },
            { "bVI", "bVII", "i", "i" }, { "i", "bIII", "bVII", "bVI" } },
          0, 1.6, 0.45, 0.05, 0.9, 0.55, 0.65, 70, 1.0, Arch, 0.15, BassPop,
          { "x.x.x.x.x.x.x.x.", "x..x..x.x..x..x.", "x...x.x.x...x.x." },
          { "x..x..x...x.x...", "x.......x.......", "x..x..x.x..x..x." }, 0.9, Arp16, 33 },
        { "Melodic Rap / R&B", Minor,
          { { "IV", "iii", "vi", "V" }, { "ii", "V", "I", "vi" }, { "I", "vi", "IV", "V" }, { "IV", "V", "iii", "vi" } },
          { { "i", "bVI", "bIII", "bVII" }, { "i", "iv", "bVI", "bVII" }, { "bVI", "bVII", "i", "iv" },
            { "i", "bVII", "bVI", "bVII" }, { "i", "v", "bVI", "iv" }, { "iv", "bVI", "i", "bVII" } },
          2, 2.0, 0.65, 0.35, 0.8, 0.95, 0.6, 64, 0.75, Falling, 0.35, Bass808,
          { "x.........x.....", "x......x..x.....", "x.....x...x....." },
          { "x...............", "x.......x......." }, 1.0, TripletBells, 28 },
        { "Punjabi Pop", HarmonicMinor,
          { { "I", "V", "vi", "IV" }, { "I", "bVII", "IV", "I" }, { "I", "IV", "V", "IV" } },
          { { "i", "bVII", "bVI", "bVII" }, { "i", "iv", "bVII", "bIII" }, { "i", "bVI", "bVII", "i" },
            { "i", "bVI", "V", "i" }, { "i", "iv", "V", "i" }, { "bVI", "bVII", "i", "i" } },
          0, 1.8, 0.5, 0.15, 0.95, 0.6, 0.72, 69, 1.05, HighStart, 0.8, BassBhangra,
          { "x..x..x.x..x..x.", "x..x..x...x.x...", "x..x.x..x..x.x.." },
          { "x.......x.......", "x..............." }, 1.0, TumbiRiff, 31 },
        { "Desi / UK Drill", Minor,
          { { "I", "V", "vi", "IV" }, { "vi", "IV", "I", "V" } },
          { { "i", "bVI", "iv", "V" }, { "i", "bVI", "bVII", "V" }, { "i", "iv", "bVI", "V" }, { "i", "bII", "i", "V" },
            { "i", "bVI", "bIII", "V" } },
          0, 1.3, 0.55, 0.3, 0.75, 0.7, 0.62, 65, 0.8, Falling, 0.3, Bass808,
          { "x.....x.x.....x.", "x......x.x......", "x.....x...x.x..." },
          { "x...............", "x.......x......." }, 1.0, DrillAnswer, 28 },
        { "Afrobeats", Major,
          { { "IV", "V", "vi", "vi" }, { "I", "V", "vi", "IV" }, { "ii", "IV", "I", "V" }, { "vi", "IV", "I", "V" } },
          { { "i", "bVII", "bVI", "bVII" }, { "i", "iv", "bVII", "bIII" }, { "i", "bVI", "bVII", "i" } },
          1, 1.8, 0.75, 0.08, 0.8, 0.7, 0.62, 67, 0.9, Arch, 0.2, BassAfro,
          { "x..x..x...x.x...", "x...x.x..x..x...", "x..x...x..x.x..." },
          { "..x...x...x.x...", "..x..x..x...x..." }, 0.45, AfroMarimba, 33 },
        { "Dance Pop / EDM", Minor,
          { { "vi", "IV", "I", "V" }, { "I", "V", "vi", "IV" }, { "IV", "V", "vi", "I" }, { "vi", "V", "IV", "V" } },
          { { "i", "bVI", "bIII", "bVII" }, { "bVI", "bVII", "i", "i" }, { "i", "bVII", "bVI", "bVII" }, { "i", "iv", "bVI", "bVII" } },
          0, 2.0, 0.55, 0.03, 0.7, 0.6, 0.55, 72, 1.0, Arch, 0.1, BassEdm,
          { "..x...x...x...x.", "x.x.x.x.x.x.x.x." },
          { "..x...x...x...x.", "x..x..x...x.x..." }, 0.4, Arp16, 33 },
        { "Emotional Ballad", Major,
          { { "I", "V", "vi", "IV" }, { "I", "iii", "IV", "V" }, { "vi", "IV", "I", "V" }, { "I", "vi", "ii", "V" } },
          { { "i", "bVI", "bIII", "bVII" }, { "i", "iv", "bVII", "bIII" }, { "bVI", "bVII", "i", "i" } },
          1, 1.0, 0.3, 0.05, 0.95, 0.5, 0.6, 67, 1.15, RisingArch, 0.25, BassBallad,
          { "x.......x.......", "x.......x...x..." },
          { "x...x...x...x...", "x.......x......." }, 0.95, Broken8, 36 },
        { "Trap Melody", Minor,
          { { "vi", "IV", "I", "V" }, { "I", "vi", "IV", "V" } },
          { { "i", "bVI", "bIII", "bVII" }, { "i", "bVII", "bVI", "V" }, { "i", "iv", "bVI", "V" }, { "i", "i", "bVI", "bVII" } },
          1, 1.8, 0.6, 0.3, 0.8, 0.85, 0.6, 66, 0.8, Falling, 0.25, Bass808,
          { "x.....x...x..x..", "x..x......x.....", "x.......x.x....." },
          { "x...............", "x.......x......." }, 1.0, TripletBells, 28 },
    };
    return defs[std::clamp(i, 0, (int) NumStyles - 1)];
}

int harmonyScale(int scale)
{
    if (scale == PentatonicMajor) return Major;
    if (scale == PentatonicMinor) return Minor;
    return scale;
}

bool scaleIsMinor(int scale)
{
    const auto& st = scaleSteps(scale);
    return std::find(st.begin(), st.end(), 3) != st.end() && std::find(st.begin(), st.end(), 4) == st.end();
}

// --------------------------------------------------------------- chords

struct Roman
{
    int root = 0;    // semitones above tonic
    int quality = 0; // 0 major, 1 minor, 2 dim, 3 aug
};

Roman parseRoman(const char* r)
{
    Roman out;
    int acc = 0;
    size_t i = 0;
    while (r[i] == 'b' || r[i] == '#') acc += r[i++] == 'b' ? -1 : 1;
    std::string num;
    bool upper = false;
    while (r[i] == 'I' || r[i] == 'V' || r[i] == 'i' || r[i] == 'v')
    {
        upper = r[i] == 'I' || r[i] == 'V';
        num += (char) std::toupper((unsigned char) r[i]);
        ++i;
    }
    static const char* names[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
    static const int semis[] = { 0, 2, 4, 5, 7, 9, 11 };
    for (int k = 0; k < 7; ++k)
        if (num == names[k]) out.root = semis[k];
    out.root = ((out.root + acc) % 12 + 12) % 12;
    out.quality = upper ? 0 : 1;
    if (r[i] == 'o') out.quality = 2;
    if (r[i] == '+') out.quality = 3;
    return out;
}

/** Chord root spelled the way a musician would write it in this key. */
std::string rootName(int pc, int tonic, bool minor)
{
    static const char* sharps[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flats[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    // keys whose signatures use sharps (relative-major pitch class)
    const int major = minor ? pcOf(tonic + 3) : pcOf(tonic);
    const bool sharpKey = major == 7 || major == 2 || major == 9 || major == 4 || major == 11 || major == 6;
    return (sharpKey ? sharps : flats)[pcOf(pc)];
}

Chord makeChord(const char* roman, int tonic, int ext, bool minorKey)
{
    const Roman rn = parseRoman(roman);
    Chord c;
    c.root = rn.root;
    c.roman = roman;
    static const int triads[4][3] = { { 0, 4, 7 }, { 0, 3, 7 }, { 0, 3, 6 }, { 0, 4, 8 } };
    const int rootPc = pcOf(tonic + rn.root);
    for (int iv : triads[rn.quality]) c.pcs.push_back(pcOf(rootPc + iv));
    std::string suffix = rn.quality == 1 ? "m" : rn.quality == 2 ? "dim" : rn.quality == 3 ? "aug" : "";
    if (ext >= 1 && rn.quality != 3)
    {
        int seventh = 10;
        if (rn.quality == 0) seventh = (rn.root == 7 || rn.root == 10 || rn.root == 2) ? 10 : 11;
        c.pcs.push_back(pcOf(rootPc + seventh));
        if (ext >= 2 && rn.quality != 2)
        {
            c.pcs.push_back(pcOf(rootPc + 14));
            suffix = rn.quality == 1 ? "m9" : (seventh == 11 ? "maj9" : "9");
        }
        else
            suffix = rn.quality == 1 ? "m7" : rn.quality == 2 ? "m7b5" : (seventh == 11 ? "maj7" : "7");
    }
    c.name = rootName(rootPc, tonic, minorKey) + suffix;
    return c;
}

bool isChordTone(const Chord& c, int pitch, int ext)
{
    const int pc = pcOf(pitch);
    const size_t n = std::min(c.pcs.size(), (size_t) (3 + std::max(0, ext)));
    for (size_t i = 0; i < n; ++i)
        if (c.pcs[i] == pc) return true;
    return false;
}

/** Melody scale bent to agree with the chord (e.g. raised 7th over a major V). */
std::vector<int> chordScalePcs(int tonic, int scale, const Chord& c)
{
    std::vector<int> pcs;
    for (int st : scaleSteps(scale)) pcs.push_back(pcOf(tonic + st));
    for (size_t i = 0; i < std::min<size_t>(3, c.pcs.size()); ++i)
    {
        const int cpc = c.pcs[i];
        if (std::find(pcs.begin(), pcs.end(), cpc) != pcs.end()) continue;
        pcs.erase(std::remove_if(pcs.begin(), pcs.end(), [&](int p) { return pcOf(p - cpc) == 1 || pcOf(cpc - p) == 1; }),
                  pcs.end());
        pcs.push_back(cpc);
    }
    std::sort(pcs.begin(), pcs.end());
    return pcs;
}

std::vector<int> pitchesIn(const std::vector<int>& pcs, int lo, int hi)
{
    std::vector<int> out;
    for (int p = lo; p <= hi; ++p)
        if (std::find(pcs.begin(), pcs.end(), pcOf(p)) != pcs.end()) out.push_back(p);
    return out;
}

void voiceChords(std::vector<Chord>& chords, int ext)
{
    std::vector<int> prev;
    for (auto& c : chords)
    {
        // which chord members to voice
        std::vector<int> tones;
        if (ext >= 2 && c.pcs.size() >= 5) tones = { c.pcs[0], c.pcs[1], c.pcs[3], c.pcs[4] };
        else tones.assign(c.pcs.begin(), c.pcs.begin() + (long) std::min<size_t>(c.pcs.size(), 4));
        std::vector<int> best;
        double bestCost = 1e9;
        for (size_t rot = 0; rot < tones.size(); ++rot)
        {
            for (int start = 50; start <= 61; ++start)
            {
                if (pcOf(start) != tones[rot]) continue;
                std::vector<int> v { start };
                for (size_t k = 1; k < tones.size(); ++k)
                {
                    int p = v.back() + 1;
                    while (pcOf(p) != tones[(rot + k) % tones.size()]) ++p;
                    v.push_back(p);
                }
                double centroid = 0;
                for (int p : v) centroid += p;
                centroid /= (double) v.size();
                double cost = std::fabs(centroid - 60) * 0.35;
                if (prev.size() == v.size())
                    for (size_t k = 0; k < v.size(); ++k) cost += std::abs(v[k] - prev[k]);
                else if (rot != 0)
                    cost += 3;
                if (cost < bestCost) { bestCost = cost; best = v; }
            }
        }
        c.voicing = best;
        prev = best;
    }
}

std::vector<Chord> makeProgression(const StyleDef& sd, int tonic, int scale, int bars, uint32_t seed)
{
    Rng r(mix(seed, 11));
    const bool minor = scaleIsMinor(harmonyScale(scale));
    const auto& progs = minor ? sd.minorProgs : sd.majorProgs;
    const auto& first = progs[(size_t) r.below((int) progs.size())];
    const auto* second = &first;
    if (bars >= 16 && r.chance(0.35)) second = &progs[(size_t) r.below((int) progs.size())];
    std::vector<Chord> out;
    for (int b = 0; b < bars; ++b)
    {
        const auto& prog = (bars >= 16 && b >= 8) ? *second : first;
        Chord c = makeChord(prog[(size_t) b % prog.size()], tonic, sd.ext, minor);
        c.beat = b * 4.0;
        c.len = 4.0;
        out.push_back(c);
    }
    voiceChords(out, sd.ext);
    return out;
}

const Chord& chordAt(const std::vector<Chord>& chords, double beat)
{
    for (const auto& c : chords)
        if (beat >= c.beat - 1e-9 && beat < c.beat + c.len - 1e-9) return c;
    return beat < 0 || chords.empty() ? chords.front() : chords.back();
}

// --------------------------------------------------------------- rhythm

struct Cell
{
    std::vector<double> on;
    double sync;
    bool triplet;
};

const std::vector<Cell>& cells()
{
    static const std::vector<Cell> c = {
        { {}, 0.0, false },
        { { 0.0 }, 0.0, false },
        { { 0.0, 0.5 }, 0.1, false },
        { { 0.5 }, 0.9, false },
        { { 0.0, 0.75 }, 0.5, false },
        { { 0.25, 0.5 }, 1.0, false },
        { { 0.0, 0.25, 0.5 }, 0.35, false },
        { { 0.0, 0.5, 0.75 }, 0.45, false },
        { { 0.5, 0.75 }, 0.8, false },
        { { 0.0, 0.25, 0.5, 0.75 }, 0.3, false },
        { { 0.75 }, 1.0, false },
        { { 0.0, 1.0 / 3, 2.0 / 3 }, 0.35, true },
        { { 1.0 / 3, 2.0 / 3 }, 0.85, true },
        { { 0.0, 2.0 / 3 }, 0.45, true },
    };
    return c;
}

struct Ev
{
    double beat, len;
    bool strong;
};

double targetDensity(const StyleDef& sd, const Settings& s)
{
    return sd.density * (0.45 + 1.15 * std::clamp(s.density, 0.0, 1.0));
}

double targetSync(const StyleDef& sd, const Settings& s)
{
    return std::clamp(0.5 * sd.sync + 0.6 * s.syncopation, 0.0, 1.0);
}

std::vector<double> cellWeights(const StyleDef& sd, const Settings& s)
{
    const double dens = targetDensity(sd, s), sync = targetSync(sd, s);
    std::vector<double> w;
    for (const auto& c : cells())
    {
        const double n = (double) c.on.size();
        double x = std::exp(-(n - dens) * (n - dens) / 1.6) * std::exp(-(c.sync - sync) * (c.sync - sync) / 0.35);
        if (c.triplet) x *= sd.triplets * 2.5;
        if (c.on.empty()) x *= 0.6;
        w.push_back(x);
    }
    return w;
}

void finishDurations(std::vector<Ev>& ev, double phraseLen, double legato)
{
    std::sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.beat < b.beat; });
    for (size_t i = 0; i < ev.size(); ++i)
    {
        const double next = i + 1 < ev.size() ? ev[i + 1].beat : phraseLen;
        const double gap = next - ev[i].beat;
        ev[i].len = i + 1 < ev.size() ? std::max(0.12, std::min(gap * legato, 2.0)) : std::max(0.25, std::min(gap, 2.5) * 0.95);
        const double frac = ev[i].beat - std::floor(ev[i].beat + 1e-9);
        const int beatInBar = (int) std::floor(ev[i].beat + 1e-9) % 4;
        ev[i].strong = (frac < 1e-6 && (beatInBar == 0 || beatInBar == 2)) || ev[i].len >= 1.0 || i == 0;
    }
}

std::vector<Ev> rhythmFromCells(const StyleDef& sd, const Settings& s, Rng& r, int fromBeat, std::vector<Ev> keep)
{
    const auto w = cellWeights(sd, s);
    std::vector<Ev> ev;
    for (const auto& e : keep)
        if (e.beat < fromBeat - 1e-9) ev.push_back(e);
    for (int b = fromBeat; b < 8; ++b)
    {
        const auto& c = cells()[(size_t) r.weighted(w)];
        for (double f : c.on) ev.push_back({ b + f, 0.5, false });
    }
    return ev;
}

double rhythmScore(const std::vector<Ev>& ev, const StyleDef& sd, const Settings& s)
{
    if (ev.size() < 4) return -100;
    const double dens = ev.size() / 8.0;
    int off = 0;
    for (const auto& e : ev)
        if (std::fabs(e.beat - std::round(e.beat)) > 1e-6) ++off;
    const double sync = (double) off / (double) ev.size();
    double sc = -std::fabs(dens - targetDensity(sd, s)) * 1.5 - std::fabs(sync - targetSync(sd, s)) * 2.0;
    bool longNote = false;
    for (const auto& e : ev) longNote = longNote || e.len >= 0.9;
    if (longNote) sc += 0.5;
    if (ev.back().beat <= 6.5) sc += 0.4; // room to breathe at the end
    if (ev.front().beat < 1.0) sc += 0.3;
    return sc;
}

std::vector<Ev> makeMotifRhythm(const StyleDef& sd, const Settings& s, uint32_t seed)
{
    std::vector<Ev> best;
    double bestScore = -1e9;
    for (int k = 0; k < 10; ++k)
    {
        Rng r(mix(seed, 100 + k));
        auto ev = rhythmFromCells(sd, s, r, 0, {});
        // breathe: clear the last beat most of the time
        if (r.chance(0.75)) ev.erase(std::remove_if(ev.begin(), ev.end(), [](const Ev& e) { return e.beat >= 7.0; }), ev.end());
        finishDurations(ev, 8.0, sd.legato);
        const double sc = rhythmScore(ev, sd, s);
        if (sc > bestScore) { bestScore = sc; best = ev; }
    }
    return best;
}

// --------------------------------------------------------------- melody

struct MelCtx
{
    const StyleDef* sd;
    Settings s;
    int tonic, scale, ext;
    const std::vector<Chord>* chords;
    int lo, hi, center;
};

double contourAt(Contour c, double u)
{
    switch (c)
    {
        case Arch: return std::sin(kPi * u) * 1.2 - 0.45;
        case Falling: return 0.75 - 1.35 * u;
        case HighStart: return 0.85 - 1.4 * u + 0.25 * std::sin(4 * kPi * u);
        case RisingArch: return std::sin(kPi * std::pow(u, 0.75)) * 1.3 - 0.55;
    }
    return 0;
}

double intervalCost(int d, double repeatOk)
{
    d = std::abs(d);
    switch (d)
    {
        case 0: return 1.1 - repeatOk;
        case 1: case 2: return 0.0;
        case 3: case 4: return 0.45;
        case 5: return 0.9;
        case 6: return 3.0;
        case 7: return 1.3;
        case 8: case 9: return 2.2;
        case 12: return 2.0;
        default: return d > 12 ? 6.0 : 3.0;
    }
}

enum PhraseKind { KindA, KindAnswer, KindFinal, KindNew };

/** Picks pitches for one phrase, note by note, scoring every candidate. */
std::vector<int> composePhrase(const MelCtx& m, const std::vector<Ev>& rh, double start, PhraseKind kind, Rng& r,
                               int prevPitch, int prevInterval, const std::vector<int>* reuseFirstHalf)
{
    std::vector<int> out;
    const int n = (int) rh.size();
    const double half = (m.hi - m.lo) / 2.0;
    int prev = prevPitch, prevIv = prevInterval;
    for (int i = 0; i < n; ++i)
    {
        const double t = start + rh[(size_t) i].beat;
        const Chord& ch = chordAt(*m.chords, t);
        const auto avail = pitchesIn(chordScalePcs(m.tonic, m.scale, ch), m.lo, m.hi);
        if (avail.empty()) { out.push_back(prev); continue; }

        if (reuseFirstHalf && rh[(size_t) i].beat < 4.0 && (size_t) i < reuseFirstHalf->size())
        {
            int p = (*reuseFirstHalf)[(size_t) i];
            int bestP = avail.front();
            for (int a : avail) if (std::abs(a - p) < std::abs(bestP - p)) bestP = a;
            if (rh[(size_t) i].strong && !isChordTone(ch, bestP, m.ext))
                for (int a : avail)
                    if (isChordTone(ch, a, m.ext) && std::abs(a - p) <= 2) { bestP = a; break; }
            prevIv = bestP - prev;
            prev = bestP;
            out.push_back(bestP);
            continue;
        }

        const double u = n > 1 ? (double) i / (n - 1) : 0.0;
        double target = m.center + contourAt(m.sd->contour, u) * half;
        const bool last = i == n - 1;
        double bestCost = 1e9;
        int bestP = avail.front();
        for (int c : avail)
        {
            double cost = 2.0 * std::fabs(c - target) / std::max(1.0, half);
            if (prev > 0)
            {
                const int d = c - prev;
                cost += intervalCost(d, m.sd->repeatOk);
                if (std::abs(prevIv) >= 5)
                {
                    if ((d > 0) == (prevIv > 0) && d != 0) cost += 1.5;
                    else if (std::abs(d) > 2) cost += 0.7;
                }
            }
            const bool ct = isChordTone(ch, c, m.ext);
            if (rh[(size_t) i].strong && !ct) cost += 2.5;
            if (rh[(size_t) i].strong && pcOf(c) == ch.pcs[0] && t == ch.beat) cost -= 0.3;
            if (last)
            {
                const int deg = pcOf(c - m.tonic);
                if (kind == KindFinal) cost += deg == 0 ? -1.0 : (deg == 3 || deg == 4 ? 0.6 : 3.0);
                else if (kind == KindA || kind == KindNew) cost += deg == 0 ? 1.0 : (ct ? -0.3 : 0.5);
                else cost += deg == 0 ? 0.0 : (ct ? 0.0 : 1.0);
            }
            cost += r.uni() * (0.6 + m.s.complexity);
            if (cost < bestCost) { bestCost = cost; bestP = c; }
        }
        prevIv = prev > 0 ? bestP - prev : 0;
        prev = bestP;
        out.push_back(bestP);
    }
    return out;
}

/** Re-fits a phrase's pitches to the chords under a later repeat of it. */
std::vector<int> refit(const MelCtx& m, const std::vector<Ev>& rh, double start, const std::vector<int>& src, Rng& r)
{
    double bestCost = 1e9;
    std::vector<int> best;
    for (int shift = -2; shift <= 2; ++shift)
    {
        std::vector<int> cand;
        double cost = std::abs(shift) * 0.6;
        for (size_t i = 0; i < rh.size() && i < src.size(); ++i)
        {
            const Chord& ch = chordAt(*m.chords, start + rh[i].beat);
            const auto avail = pitchesIn(chordScalePcs(m.tonic, m.scale, ch), m.lo - 3, m.hi + 3);
            if (avail.empty()) { cand.push_back(src[i]); continue; }
            size_t idx = 0;
            for (size_t k = 0; k < avail.size(); ++k)
                if (std::abs(avail[k] - src[i]) < std::abs(avail[idx] - src[i])) idx = k;
            const long j = std::clamp((long) idx + shift, 0L, (long) avail.size() - 1);
            int p = avail[(size_t) j];
            if (rh[i].strong && !isChordTone(ch, p, m.ext))
            {
                int fix = p;
                for (int a : avail)
                    if (isChordTone(ch, a, m.ext) && std::abs(a - p) <= 2 && (fix == p || std::abs(a - p) < std::abs(fix - p))) fix = a;
                if (fix == p) cost += 2.0;
                p = fix;
            }
            cand.push_back(p);
        }
        if (cost < bestCost) { bestCost = cost; best = cand; }
    }
    // small variations on weak notes
    for (size_t i = 0; i < best.size(); ++i)
    {
        if (rh[i].strong || !r.chance(m.s.complexity * 0.25)) continue;
        const Chord& ch = chordAt(*m.chords, start + rh[i].beat);
        const auto avail = pitchesIn(chordScalePcs(m.tonic, m.scale, ch), m.lo, m.hi);
        auto it = std::find(avail.begin(), avail.end(), best[i]);
        if (it == avail.end()) continue;
        const long j = (long) (it - avail.begin()) + (r.chance(0.5) ? 1 : -1);
        if (j >= 0 && j < (long) avail.size()) best[i] = avail[(size_t) j];
    }
    return best;
}

struct Phrase
{
    PhraseKind kind;
    std::vector<Ev> rhythm;
};

std::vector<Phrase> planPhrases(const StyleDef& sd, const Settings& s, const std::vector<Ev>& motif, uint32_t seed)
{
    Rng r(mix(seed, 7));
    const int P = std::max(1, s.bars / 2);
    std::vector<PhraseKind> kinds;
    for (int p = 0; p < P; ++p)
    {
        const int q = p % 4;
        PhraseKind k = q == 0 || q == 2 ? KindA : (q == 1 ? KindAnswer : KindFinal);
        if (P == 2 && p == 1) k = KindFinal;
        if (p > 0 && k == KindA && !r.chance(0.35 + 0.65 * s.repetition)) k = KindNew;
        kinds.push_back(k);
    }
    std::vector<Phrase> out;
    for (int p = 0; p < P; ++p)
    {
        Phrase ph { kinds[(size_t) p], motif };
        if (ph.kind == KindNew)
        {
            Rng rn(mix(seed, 200 + p));
            ph.rhythm = rhythmFromCells(sd, s, rn, 0, {});
            if (rn.chance(0.7))
                ph.rhythm.erase(std::remove_if(ph.rhythm.begin(), ph.rhythm.end(), [](const Ev& e) { return e.beat >= 7.0; }), ph.rhythm.end());
            if (ph.rhythm.size() < 4) ph.rhythm = motif;
        }
        else if (ph.kind == KindAnswer || ph.kind == KindFinal)
        {
            Rng rn(mix(seed, 300 + p));
            if (rn.chance(0.25 + 0.7 * s.complexity) || ph.kind == KindFinal)
            {
                ph.rhythm = rhythmFromCells(sd, s, rn, 4, motif);
                // end the phrase on a held note
                ph.rhythm.erase(std::remove_if(ph.rhythm.begin(), ph.rhythm.end(), [](const Ev& e) { return e.beat >= 6.0 + 1e-9; }), ph.rhythm.end());
                if (ph.kind == KindFinal && (ph.rhythm.empty() || ph.rhythm.back().beat < 5.0)) ph.rhythm.push_back({ 5.0, 1.0, true });
                if (ph.rhythm.size() < 3) ph.rhythm = motif;
            }
        }
        else if (p > 0 && ph.kind == KindA)
        {
            // a repeated A keeps its rhythm exactly unless the player wants variation
            Rng rn(mix(seed, 400 + p));
            if (rn.chance(s.complexity * 0.35)) ph.rhythm = rhythmFromCells(sd, s, rn, 6, motif);
        }
        finishDurations(ph.rhythm, 8.0, sd.legato);
        out.push_back(ph);
    }
    return out;
}

std::vector<GNote> composeMelody(const MelCtx& m, const std::vector<Phrase>& plan, uint32_t pitchSeed)
{
    std::vector<GNote> notes;
    Rng r(pitchSeed);
    std::vector<int> motifPitches;
    int prev = 0, prevIv = 0;
    for (size_t p = 0; p < plan.size(); ++p)
    {
        const auto& ph = plan[p];
        const double start = p * 8.0;
        std::vector<int> pitches;
        if (ph.kind == KindA && !motifPitches.empty())
        {
            if (ph.rhythm.size() == plan[0].rhythm.size()) pitches = refit(m, ph.rhythm, start, motifPitches, r);
            else pitches = composePhrase(m, ph.rhythm, start, KindA, r, prev, prevIv, &motifPitches);
        }
        else if ((ph.kind == KindAnswer || ph.kind == KindFinal) && !motifPitches.empty())
            pitches = composePhrase(m, ph.rhythm, start, ph.kind, r, prev, prevIv, &motifPitches);
        else
            pitches = composePhrase(m, ph.rhythm, start, ph.kind == KindNew ? KindNew : KindA, r, prev, prevIv, nullptr);
        if (p == 0) motifPitches = pitches;
        for (size_t i = 0; i < ph.rhythm.size() && i < pitches.size(); ++i)
        {
            GNote g;
            g.beat = start + ph.rhythm[i].beat;
            g.len = ph.rhythm[i].len;
            g.pitch = pitches[i];
            g.vel = ph.rhythm[i].strong ? 104 : 90;
            notes.push_back(g);
        }
        if (!pitches.empty())
        {
            prevIv = pitches.size() > 1 ? pitches.back() - pitches[pitches.size() - 2] : 0;
            prev = pitches.back();
        }
    }
    return notes;
}

void ornament(std::vector<GNote>& mel, const MelCtx& m, uint32_t seed)
{
    const double amount = std::clamp(m.sd->ornament * 2.0 * m.s.ornaments, 0.0, 1.0);
    if (amount <= 0.01) return;
    Rng r(mix(seed, 55));
    std::vector<GNote> out;
    for (size_t i = 0; i < mel.size(); ++i)
    {
        GNote n = mel[i];
        const Chord& ch = chordAt(*m.chords, n.beat);
        const auto avail = pitchesIn(chordScalePcs(m.tonic, m.scale, ch), m.lo - 4, m.hi + 4);
        auto it = std::find(avail.begin(), avail.end(), n.pitch);
        const bool canUp = it != avail.end() && it + 1 != avail.end();
        const bool canDown = it != avail.end() && it != avail.begin();
        const bool lastOfPhrase = i + 1 == mel.size() || std::floor(mel[i + 1].beat / 8.0) != std::floor(n.beat / 8.0);

        if (m.sd->contour == HighStart && n.len >= 0.9 && canUp && canDown && r.chance(amount * 0.45))
        {
            // murki: quick upper-main-lower turn into the note
            const double g = 1.0 / 12.0;
            out.push_back({ n.beat, g, *(it + 1), 76 });
            out.push_back({ n.beat + g, g, n.pitch, 74 });
            out.push_back({ n.beat + 2 * g, g, *(it - 1), 74 });
            n.beat += 3 * g;
            n.len -= 3 * g;
        }
        else if (n.len >= 0.7 && canUp && r.chance(amount * 0.35))
        {
            // grace note from above
            const double g = 0.125;
            out.push_back({ n.beat, g, *(it + 1), 74 });
            n.beat += g;
            n.len -= g;
        }
        out.push_back(n);
        if (lastOfPhrase && n.len >= 0.75 && canDown && r.chance(amount * 0.4))
        {
            // fall off the end of the phrase
            auto& held = out.back();
            const double fall = std::min(0.25, held.len * 0.3);
            held.len -= fall;
            out.push_back({ held.beat + held.len, fall, *(it - 1), 70 });
        }
    }
    mel.swap(out);
}

void humanizeAndSwing(std::vector<GNote>& notes, double humanize, double swing, uint64_t seed, bool timing)
{
    Rng r(seed);
    for (auto& n : notes)
    {
        const double frac = n.beat - std::floor(n.beat + 1e-9);
        if (swing > 0 && (std::fabs(frac - 0.25) < 1e-6 || std::fabs(frac - 0.75) < 1e-6))
        {
            const double d = swing * 0.09;
            n.beat += d;
            n.len = std::max(0.06, n.len - d);
        }
        if (humanize > 0)
        {
            n.vel = std::clamp(n.vel + (int) std::lround((r.uni() * 2 - 1) * 10 * humanize), 1, 127);
            if (timing && n.beat > 0.01) n.beat += (r.uni() * 2 - 1) * 0.02 * humanize;
        }
    }
}

// ------------------------------------------------------------ other layers

std::vector<GNote> makeChordLayer(const StyleDef& sd, const std::vector<Chord>& chords, double beats, uint32_t seed)
{
    Rng r(mix(seed, 21));
    const char* pat = sd.chordPatterns[(size_t) r.below((int) sd.chordPatterns.size())];
    std::vector<GNote> out;
    const int bars = (int) std::ceil(beats / 4.0 - 1e-9);
    for (int b = 0; b < bars; ++b)
    {
        std::vector<int> hits;
        for (int st = 0; st < 16; ++st) if (pat[st] == 'x') hits.push_back(st);
        for (size_t h = 0; h < hits.size(); ++h)
        {
            const double t = b * 4.0 + hits[h] * 0.25;
            const double next = h + 1 < hits.size() ? b * 4.0 + hits[h + 1] * 0.25 : (b + 1) * 4.0;
            const Chord& c = chordAt(chords, t);
            const double end = std::min(next, c.beat + c.len);
            for (int p : c.voicing) out.push_back({ t, std::max(0.1, (end - t) * sd.chordLegato), p, h == 0 ? 84 : 76 });
        }
    }
    return out;
}

std::vector<GNote> makeBass(const StyleDef& sd, const std::vector<Chord>& chords, double beats, int tonic, int scale, uint32_t seed)
{
    Rng r(mix(seed, 31));
    const char* pat = sd.bassPatterns[(size_t) r.below((int) sd.bassPatterns.size())];
    std::vector<GNote> out;
    const int bars = (int) std::ceil(beats / 4.0 - 1e-9);
    const double legato = sd.bass == Bass808 ? 0.97 : sd.bass == BassEdm ? 0.45 : 0.8;
    auto rootPitch = [&](const Chord& c) {
        int p = sd.bassLow;
        while (pcOf(p) != c.pcs[0]) ++p;
        return p;
    };
    for (int b = 0; b < bars; ++b)
    {
        std::vector<int> hits;
        for (int st = 0; st < 16; ++st) if (pat[st] == 'x') hits.push_back(st);
        for (size_t h = 0; h < hits.size(); ++h)
        {
            const double t = b * 4.0 + hits[h] * 0.25;
            const double next = h + 1 < hits.size() ? b * 4.0 + hits[h + 1] * 0.25 : (b + 1) * 4.0;
            const Chord& c = chordAt(chords, t);
            int p = rootPitch(c);
            const bool lastHit = h + 1 == hits.size();
            if (h > 0)
            {
                if ((sd.bass == BassPop || sd.bass == BassBhangra || sd.bass == BassAfro) && r.chance(0.22)) p += 7;
                else if ((sd.bass == Bass808 || sd.bass == BassEdm) && r.chance(0.15)) p += 12;
            }
            if (lastHit && (sd.bass == BassPop || sd.bass == BassBallad) && r.chance(0.4) && b * 4.0 + 4 < beats)
            {
                // approach the next chord's root by step
                const Chord& nc = chordAt(chords, (b + 1) * 4.0);
                const int target = rootPitch(nc);
                const auto sc = pitchesIn(chordScalePcs(tonic, scale, nc), target - 3, target + 3);
                for (int q : sc) if (q != target && std::abs(q - target) <= 2) { p = q; break; }
            }
            out.push_back({ t, std::max(0.1, (next - t) * legato), p, h == 0 ? 110 : 98 });
        }
    }
    return out;
}

std::vector<GNote> makeCounter(const StyleDef& sd, const std::vector<Chord>& chords, double beats, int tonic, int scale, uint32_t seed)
{
    Rng r(mix(seed, 41));
    std::vector<GNote> out;
    const int bars = (int) std::ceil(beats / 4.0 - 1e-9);
    auto upper = [](const Chord& c, int lift) {
        std::vector<int> v;
        for (int p : c.voicing) v.push_back(p + lift);
        return v;
    };
    switch (sd.counter)
    {
        case Arp16:
        case Broken8:
        {
            const bool eighths = sd.counter == Broken8 || r.chance(0.35);
            const double step = eighths ? 0.5 : 0.25;
            const int dir = r.below(3); // up, down, up-down
            for (double t = 0; t < beats - 1e-9; t += step)
            {
                const Chord& c = chordAt(chords, t);
                const auto v = upper(c, 12);
                const int k = (int) std::lround((t - c.beat) / step);
                const int n = (int) v.size();
                int idx = dir == 0 ? k % n : dir == 1 ? n - 1 - k % n : (k / (n - 1)) % 2 == 0 ? k % (n - 1) : n - 1 - k % (n - 1);
                out.push_back({ t, step * 0.8, v[(size_t) std::clamp(idx, 0, n - 1)], (std::fmod(t, 1.0) < 1e-6) ? 84 : 70 });
            }
            break;
        }
        case TripletBells:
        {
            for (int b = 0; b < bars; ++b)
            {
                const Chord& c = chordAt(chords, b * 4.0);
                auto v = upper(c, 12);
                std::sort(v.begin(), v.end());
                for (int k = 0; k < 12; ++k)
                {
                    if (k < 3 || !r.chance(0.7)) continue;
                    const double t = b * 4.0 + k / 3.0;
                    const int idx = (k % 6 < 3) ? k % 3 : 2 - k % 3;
                    out.push_back({ t, 0.3, v[(size_t) std::min<int>(idx + 1, (int) v.size() - 1)], 72 });
                }
            }
            break;
        }
        case TumbiRiff:
        {
            // pedal-and-melody riff in the tumbi register, transposed with the chords
            const int pedalDeg = 0;
            std::vector<int> moves;
            for (int i = 0; i < 8; ++i) moves.push_back(1 + r.below(4));
            std::vector<bool> rest(16, false);
            for (int i = 0; i < 16; ++i) rest[(size_t) i] = i % 4 != 0 && r.chance(0.12);
            for (int b = 0; b < bars; ++b)
            {
                const Chord& c = chordAt(chords, b * 4.0);
                const auto sc = pitchesIn(chordScalePcs(tonic, scale, c), 72, 91);
                if (sc.empty()) continue;
                size_t root = 0;
                while (root < sc.size() && pcOf(sc[root]) != c.pcs[0]) ++root;
                if (root >= sc.size()) root = 0;
                const bool fill = b % 4 == 3;
                for (int st = 0; st < 16; ++st)
                {
                    if (rest[(size_t) st]) continue;
                    long idx = (long) root + pedalDeg;
                    if (st % 2 == 1) idx += fill ? (long) (4 - st / 4) : moves[(size_t) (st / 2)];
                    idx = std::clamp(idx, 0L, (long) sc.size() - 1);
                    out.push_back({ b * 4.0 + st * 0.25, 0.2, sc[(size_t) idx], st % 4 == 0 ? 92 : 76 });
                }
            }
            break;
        }
        case DrillAnswer:
        {
            for (int b = 1; b < bars; b += 2)
            {
                const Chord& c = chordAt(chords, b * 4.0 + 2);
                auto v = upper(c, 12);
                std::sort(v.begin(), v.end(), std::greater<int>());
                for (int k = 0; k < 4 && k < (int) v.size() + 1; ++k)
                    out.push_back({ b * 4.0 + 2.0 + k * (2.0 / 3.0), 0.55, v[(size_t) (k % (int) v.size())], 80 - k * 4 });
            }
            break;
        }
        case AfroMarimba:
        {
            const char* pat = "x..x..x.x..x.x..";
            for (int b = 0; b < bars; ++b)
            {
                int n = 0;
                for (int st = 0; st < 16; ++st)
                {
                    if (pat[st] != 'x') continue;
                    const double t = b * 4.0 + st * 0.25;
                    const auto v = upper(chordAt(chords, t), 12);
                    out.push_back({ t, 0.22, v[(size_t) ((n * 2) % (int) v.size())], n % 2 ? 72 : 84 });
                    ++n;
                }
            }
            break;
        }
    }
    return out;
}

} // namespace

// ------------------------------------------------------------- public API

const char* styleName(int s) { return style(s).name; }

const char* layerName(int l)
{
    static const char* names[NumLayers] = { "Melody", "Chords", "Bass", "Riff" };
    return names[std::clamp(l, 0, (int) NumLayers - 1)];
}

int defaultScale(int s) { return style(s).scale; }

HookStats analyseHook(const std::vector<GNote>& mel, const std::vector<Chord>& chords, int tonic, int /*scale*/, int st)
{
    HookStats h;
    if (mel.empty() || chords.empty()) return h;
    const StyleDef& sd = style(st);
    int strong = 0, strongCt = 0, steps = 0, ivs = 0, lo = 127, hi = 0;
    int run = 1, longRuns = 0;
    std::vector<int> seen;
    for (size_t i = 0; i < mel.size(); ++i)
    {
        const auto& n = mel[i];
        lo = std::min(lo, n.pitch);
        hi = std::max(hi, n.pitch);
        if (std::find(seen.begin(), seen.end(), n.pitch) == seen.end()) seen.push_back(n.pitch);
        const double frac = n.beat - std::floor(n.beat + 1e-9);
        const int bib = (int) std::floor(n.beat + 1e-9) % 4;
        if ((frac < 1e-6 && (bib == 0 || bib == 2)) || n.len >= 1.0)
        {
            ++strong;
            if (isChordTone(chordAt(chords, n.beat), n.pitch, sd.ext)) ++strongCt;
        }
        if (i > 0)
        {
            const int d = std::abs(n.pitch - mel[i - 1].pitch);
            ++ivs;
            if (d <= 2) ++steps;
            if (d > 7 || d == 6) ++h.bigLeaps;
            run = d == 0 ? run + 1 : 1;
            if (run == (sd.repeatOk > 0.8 ? 6 : 4)) ++longRuns;
        }
    }
    h.chordToneStrong = strong ? (double) strongCt / strong : 1.0;
    h.stepRatio = ivs ? (double) steps / ivs : 1.0;
    h.span = hi - lo;
    h.distinct = (int) seen.size();

    double sc = 0;
    sc += 3.0 * h.chordToneStrong;
    sc += 2.0 * std::max(0.0, 1.0 - 2.0 * std::fabs(h.stepRatio - sd.stepTarget));
    sc -= 0.5 * h.bigLeaps;
    sc -= 0.5 * longRuns;
    sc += h.distinct >= 4 ? 1.0 : 0.3 * h.distinct / 4.0;
    const int spanTarget = (int) std::lround(11 * sd.rangeMul);
    sc += h.span <= spanTarget + 2 ? (h.span >= 4 ? 1.0 : 0.4) : std::max(-1.0, 1.0 - 0.25 * (h.span - spanTarget - 2));
    // a single peak in the opening phrase makes the hook memorable
    int peakCount = 0, peak = 0;
    for (const auto& n : mel) if (n.beat < 8) peak = std::max(peak, n.pitch);
    for (const auto& n : mel) if (n.beat < 8 && n.pitch == peak) ++peakCount;
    if (peakCount >= 1 && peakCount <= 2) sc += 0.8;
    const int lastDeg = pcOf(mel.back().pitch - tonic);
    sc += lastDeg == 0 ? 1.2 : (lastDeg == 3 || lastDeg == 4 || lastDeg == 7) ? 0.6 : 0.0;
    h.score = std::clamp(sc, 0.0, 10.0);
    return h;
}

Pattern generate(const Settings& in, const Seeds& seeds)
{
    Settings s = in;
    s.bars = s.bars <= 4 ? 4 : s.bars <= 8 ? 8 : 16;
    const StyleDef& sd = style(s.style);
    Pattern pat;
    pat.style = s.style;
    pat.tonic = pcOf(s.tonic);
    pat.scale = s.scale < 0 ? sd.scale : std::clamp(s.scale, 0, (int) NumScales - 1);
    pat.bars = s.bars;
    pat.beats = s.bars * 4.0;
    pat.chords = makeProgression(sd, pat.tonic, pat.scale, s.bars, seeds.chords);

    MelCtx m;
    m.sd = &sd;
    m.s = s;
    m.tonic = pat.tonic;
    m.scale = pat.scale;
    m.ext = sd.ext;
    m.chords = &pat.chords;
    const double range = (s.range == 0 ? 8.0 : s.range == 1 ? 12.0 : 16.0) * sd.rangeMul;
    m.center = sd.center + 12 * std::clamp(s.octave, -1, 1);
    m.lo = (int) std::lround(m.center - range / 2);
    m.hi = (int) std::lround(m.center + range / 2);

    const auto motif = makeMotifRhythm(sd, s, seeds.rhythm);
    const auto plan = planPhrases(sd, s, motif, seeds.rhythm);

    // best of many candidates by the hook score
    std::vector<GNote> best;
    double bestScore = -1e9;
    for (int k = 0; k < 24; ++k)
    {
        auto mel = composeMelody(m, plan, (uint32_t) mix(seeds.pitch, 1000 + k));
        const double sc = analyseHook(mel, pat.chords, pat.tonic, pat.scale, s.style).score;
        if (sc > bestScore) { bestScore = sc; best = std::move(mel); }
    }
    pat.hookScore = bestScore;
    ornament(best, m, seeds.pitch);
    humanizeAndSwing(best, s.humanize, s.swing, mix(seeds.pitch, 9), true);
    // keep the hook monophonic after ornaments and timing feel
    std::sort(best.begin(), best.end(), [](const GNote& x, const GNote& y) { return x.beat < y.beat; });
    for (size_t i = 0; i + 1 < best.size(); ++i)
        best[i].len = std::max(0.04, std::min(best[i].len, best[i + 1].beat - best[i].beat - 0.005));
    pat.layers[Melody] = std::move(best);

    pat.layers[Chords] = makeChordLayer(sd, pat.chords, pat.beats, seeds.chords);
    pat.layers[Bass] = makeBass(sd, pat.chords, pat.beats, pat.tonic, pat.scale, seeds.bass);
    pat.layers[Counter] = makeCounter(sd, pat.chords, pat.beats, pat.tonic, pat.scale, seeds.counter);
    for (int l = Chords; l < NumLayers; ++l)
        humanizeAndSwing(pat.layers[l], s.humanize * 0.6, s.swing, mix(seeds.counter, (uint64_t) l), false);
    return pat;
}

std::vector<Chord> harmonize(const std::vector<GNote>& mel, double beats, int tonic, int scale, int st)
{
    const StyleDef& sd = style(st);
    const bool minor = scaleIsMinor(harmonyScale(scale));
    static const std::vector<const char*> majorSet { "I", "ii", "iii", "IV", "V", "vi", "bVII" };
    static const std::vector<const char*> minorSet { "i", "iv", "v", "bIII", "bVI", "bVII", "V", "iio" };
    const auto& set = minor ? minorSet : majorSet;
    std::vector<Chord> cands;
    for (const char* r : set) cands.push_back(makeChord(r, tonic, sd.ext, minor));
    const int bars = std::max(1, (int) std::ceil(beats / 4.0 - 1e-9));
    const int K = (int) cands.size();

    auto fit = [&](int bar, int k) {
        double sc = 0;
        for (const auto& n : mel)
        {
            const double a = std::max(n.beat, bar * 4.0), b = std::min(n.beat + n.len, bar * 4.0 + 4.0);
            if (b <= a) continue;
            double w = b - a;
            const double frac = n.beat - std::floor(n.beat + 1e-9);
            if (n.beat >= bar * 4.0 && frac < 1e-6 && ((int) std::floor(n.beat) % 2) == 0) w *= 1.5;
            const int pc = pcOf(n.pitch);
            const auto& c = cands[(size_t) k];
            if (pc == c.pcs[0]) sc += 1.2 * w;
            else if (isChordTone(c, n.pitch, std::min(sd.ext, 1))) sc += 1.0 * w;
            else
            {
                bool avoid = false;
                for (size_t i = 0; i < 3; ++i) if (pcOf(pc - c.pcs[i]) == 1) avoid = true;
                sc += (avoid ? -0.7 : -0.15) * w;
            }
        }
        return sc;
    };
    // functional transitions: prefer strong root motion, discourage static repeats
    auto trans = [&](int a, int b) {
        const int ra = cands[(size_t) a].root, rb = cands[(size_t) b].root;
        if (a == b) return -0.4;
        const int d = pcOf(rb - ra);
        double v = (d == 5 || d == 7) ? 0.5 : (d == 3 || d == 4 || d == 8 || d == 9) ? 0.25 : 0.0;
        if (rb == 0) v += 0.2;
        return v;
    };
    std::vector<std::vector<double>> score((size_t) bars, std::vector<double>((size_t) K, -1e9));
    std::vector<std::vector<int>> back((size_t) bars, std::vector<int>((size_t) K, 0));
    for (int k = 0; k < K; ++k) score[0][(size_t) k] = fit(0, k) + (cands[(size_t) k].root == 0 ? 0.6 : 0.0);
    for (int b = 1; b < bars; ++b)
        for (int k = 0; k < K; ++k)
        {
            const double f = fit(b, k);
            for (int j = 0; j < K; ++j)
            {
                const double v = score[(size_t) b - 1][(size_t) j] + trans(j, k) + f;
                if (v > score[(size_t) b][(size_t) k]) { score[(size_t) b][(size_t) k] = v; back[(size_t) b][(size_t) k] = j; }
            }
        }
    int k = 0;
    for (int j = 1; j < K; ++j) if (score[(size_t) bars - 1][(size_t) j] > score[(size_t) bars - 1][(size_t) k]) k = j;
    std::vector<Chord> out((size_t) bars);
    for (int b = bars - 1; b >= 0; --b)
    {
        out[(size_t) b] = cands[(size_t) k];
        out[(size_t) b].beat = b * 4.0;
        out[(size_t) b].len = 4.0;
        k = back[(size_t) b][(size_t) k];
    }
    voiceChords(out, sd.ext);
    return out;
}

Pattern buildAround(const std::vector<GNote>& melody, double beats, const Settings& s, const Seeds& seeds)
{
    const StyleDef& sd = style(s.style);
    Pattern pat;
    pat.style = s.style;
    pat.tonic = pcOf(s.tonic);
    pat.scale = s.scale < 0 ? sd.scale : s.scale;
    pat.beats = std::max(4.0, std::ceil(beats / 4.0 - 1e-9) * 4.0);
    pat.bars = (int) (pat.beats / 4.0);
    pat.chords = harmonize(melody, pat.beats, pat.tonic, pat.scale, s.style);
    pat.layers[Melody] = melody;
    pat.layers[Chords] = makeChordLayer(sd, pat.chords, pat.beats, seeds.chords);
    pat.layers[Bass] = makeBass(sd, pat.chords, pat.beats, pat.tonic, pat.scale, seeds.bass);
    pat.layers[Counter] = makeCounter(sd, pat.chords, pat.beats, pat.tonic, pat.scale, seeds.counter);
    pat.hookScore = analyseHook(melody, pat.chords, pat.tonic, pat.scale, s.style).score;
    return pat;
}

// ------------------------------------------------------------------ MIDI

std::vector<uint8_t> writeMidiTracks(const std::vector<MidiTrack>& tracks, double bpm, int ppq)
{
    auto vlq = [](std::vector<uint8_t>& o, uint32_t n) {
        uint8_t b[5];
        int len = 0;
        b[len++] = n & 0x7f;
        while ((n >>= 7) > 0) b[len++] = (uint8_t) ((n & 0x7f) | 0x80);
        while (len > 0) o.push_back(b[--len]);
    };
    auto u32 = [](std::vector<uint8_t>& o, uint32_t n) {
        for (int sh = 24; sh >= 0; sh -= 8) o.push_back((uint8_t) ((n >> sh) & 255));
    };
    auto chunk = [&](std::vector<uint8_t>& out, const std::vector<uint8_t>& trk) {
        out.insert(out.end(), { 'M', 'T', 'r', 'k' });
        u32(out, (uint32_t) trk.size());
        out.insert(out.end(), trk.begin(), trk.end());
    };
    auto tick = [&](double beat) { return (uint32_t) std::max(0L, std::lround(beat * ppq)); };

    std::vector<uint8_t> out { 'M', 'T', 'h', 'd' };
    u32(out, 6);
    const uint16_t ntr = (uint16_t) (tracks.size() + 1);
    out.insert(out.end(), { 0, 1, (uint8_t) (ntr >> 8), (uint8_t) (ntr & 255), (uint8_t) ((ppq >> 8) & 255), (uint8_t) (ppq & 255) });

    {
        const uint32_t us = (uint32_t) std::lround(60000000.0 / (bpm > 0 ? bpm : 120));
        std::vector<uint8_t> t { 0, 0xff, 0x51, 3, (uint8_t) ((us >> 16) & 255), (uint8_t) ((us >> 8) & 255), (uint8_t) (us & 255),
                                 0, 0xff, 0x58, 4, 4, 2, 24, 8, 0, 0xff, 0x2f, 0 };
        chunk(out, t);
    }
    for (const auto& tr : tracks)
    {
        struct Ev { uint32_t t; int ord; uint8_t a, b, c; };
        std::vector<Ev> ev;
        const uint8_t ch = (uint8_t) (tr.channel & 15);
        for (const auto& n : tr.notes)
        {
            const uint32_t a = tick(n.beat), b = std::max(a + 1, tick(n.beat + n.len));
            const uint8_t p = (uint8_t) std::clamp(n.pitch, 0, 127);
            ev.push_back({ a, 1, (uint8_t) (0x90 | ch), p, (uint8_t) std::clamp(n.vel, 1, 127) });
            ev.push_back({ b, 0, (uint8_t) (0x80 | ch), p, 0 });
        }
        std::stable_sort(ev.begin(), ev.end(), [](const Ev& x, const Ev& y) { return x.t != y.t ? x.t < y.t : x.ord < y.ord; });
        std::vector<uint8_t> t { 0, 0xff, 0x03 };
        vlq(t, (uint32_t) tr.name.size());
        t.insert(t.end(), tr.name.begin(), tr.name.end());
        uint32_t last = 0;
        for (const auto& e : ev)
        {
            vlq(t, e.t - last);
            t.insert(t.end(), { e.a, e.b, e.c });
            last = e.t;
        }
        t.insert(t.end(), { 0, 0xff, 0x2f, 0 });
        chunk(out, t);
    }
    return out;
}

} // namespace ml::gen
