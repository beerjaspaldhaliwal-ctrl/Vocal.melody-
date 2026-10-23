// Tests for the JUCE-free core. Synthesises sung melodies and checks the
// transcription, mirroring test/run-tests.mjs for the web app.
#include "../Source/core/Generator.h"
#include "../Source/core/MelodyCore.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;

struct SynthNote { int pitch; double start, dur; double scoop = 0; };
struct SynthOpts { double vibrato = 0.3; double detuneCents = 0; double noise = 0.003; };

int failures = 0;
std::string failMsg;

#define CHECK(cond, msg) do { if (!(cond)) { failMsg = std::string(msg); throw 1; } } while (0)

std::vector<float> synth(const std::vector<SynthNote>& mel, double sr, SynthOpts o = {})
{
    double endT = 0;
    for (auto& n : mel) endT = std::max(endT, n.start + n.dur);
    std::vector<float> x((size_t) std::ceil(endT * sr + sr * 0.3), 0.0f);
    uint32_t seed = 1;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 8388608.0 - 1.0; };
    for (auto& n : mel)
    {
        const double f0 = ml::midiToHz(n.pitch + o.detuneCents / 100);
        const size_t a = (size_t) std::lround(n.start * sr), len = (size_t) std::lround(n.dur * sr);
        double phase = 0;
        for (size_t i = 0; i < len; ++i)
        {
            const double t = i / sr;
            const double vib = o.vibrato * std::sin(2 * kPi * 5.5 * t) * std::min(1.0, t / 0.25);
            const double scoop = n.scoop * std::exp(-t / 0.04);
            const double f = f0 * std::pow(2.0, (vib + scoop) / 12);
            phase += 2 * kPi * f / sr;
            const double env = std::min(1.0, t / 0.02) * std::min(1.0, (n.dur - t) / 0.03) * 0.5;
            double v = 0;
            for (int h = 1; h <= 12; ++h) if (f * h < sr / 2) v += std::sin(h * phase) / std::pow(h, 1.1);
            x[a + i] += (float) (env * v * 0.4);
        }
    }
    for (auto& s : x) s += (float) (o.noise * rnd());
    return x;
}

ml::Transcription run(const std::vector<SynthNote>& mel, SynthOpts o = {}, double sr = ml::kAnalysisRate)
{
    auto x = synth(mel, sr, o);
    std::vector<float> a = sr == ml::kAnalysisRate ? x : ml::resampleToAnalysis(x.data(), x.size(), sr);
    ml::highpass(a.data(), a.size(), ml::kAnalysisRate, 60);
    auto track = ml::trackPitch(a.data(), a.size(), ml::kAnalysisRate, 65, 1100);
    return ml::transcribe(track, {});
}

std::string dump(const std::vector<ml::Note>& ns)
{
    std::string s;
    for (auto& n : ns) s += "[" + std::to_string(n.pitch) + " " + std::to_string(n.start).substr(0, 5) + "-" + std::to_string(n.end).substr(0, 5) + "] ";
    return s;
}

void checkMelody(const std::vector<SynthNote>& mel, const ml::Transcription& r, double tol = 0.06)
{
    CHECK(r.notes.size() == mel.size(), "note count " + std::to_string(r.notes.size()) + ": " + dump(r.notes));
    for (size_t i = 0; i < mel.size(); ++i)
    {
        const auto& n = r.notes[i];
        CHECK(n.pitch == mel[i].pitch, "pitch of note " + std::to_string(i) + ": " + dump(r.notes));
        CHECK(std::fabs(n.start - mel[i].start) < tol, "start of note " + std::to_string(i) + ": " + dump(r.notes));
        CHECK(std::fabs(n.end - (mel[i].start + mel[i].dur)) < tol * 1.5, "end of note " + std::to_string(i) + ": " + dump(r.notes));
    }
}

std::vector<SynthNote> seq(std::vector<int> pitches, double t0, double step, double dur, double scoop = 0)
{
    std::vector<SynthNote> m;
    for (size_t i = 0; i < pitches.size(); ++i) m.push_back({ pitches[i], t0 + i * step, dur, scoop });
    return m;
}

void test(const char* name, const std::function<void()>& fn)
{
    try { fn(); std::printf("ok   %s\n", name); }
    catch (...) { ++failures; std::printf("FAIL %s\n     %s\n", name, failMsg.c_str()); }
}
} // namespace

int main()
{
    test("simple melody with vibrato", [] {
        auto m = seq({ 60, 62, 64, 65, 67, 65, 64, 62 }, 0.2, 0.4, 0.33);
        checkMelody(m, run(m));
    });
    test("legato line with scoops", [] {
        auto m = seq({ 57, 60, 64, 62, 59, 57 }, 0.1, 0.35, 0.35, -1);
        checkMelody(m, run(m));
    });
    test("low and high voices", [] {
        auto lo = seq({ 43, 45, 47, 48 }, 0.2, 0.45, 0.4);
        checkMelody(lo, run(lo));
        auto hi = seq({ 79, 81, 83, 84 }, 0.2, 0.45, 0.4);
        checkMelody(hi, run(hi));
    });
    test("long low notes in noise", [] {
        auto m = seq({ 40, 43, 41 }, 0.2, 1.1, 1.0);
        SynthOpts o; o.noise = 0.02; o.vibrato = 0.5;
        checkMelody(m, run(m, o), 0.08);
    });
    test("repeated syllables split", [] {
        auto m = seq({ 64, 64, 64, 64 }, 0.2, 0.3, 0.3);
        auto r = run(m);
        CHECK(r.notes.size() == 4, dump(r.notes));
    });
    test("auto tuning, 35 cents sharp", [] {
        auto m = seq({ 60, 64, 67, 72 }, 0.2, 0.4, 0.35);
        SynthOpts o; o.detuneCents = 35;
        auto r = run(m, o);
        CHECK(std::fabs(r.prep.tuning - 0.35) < 0.06, "tuning " + std::to_string(r.prep.tuning));
        checkMelody(m, r);
    });
    test("44.1 kHz input through the resampler", [] {
        auto m = seq({ 62, 66, 69, 74, 69 }, 0.25, 0.4, 0.34);
        checkMelody(m, run(m, {}, 44100.0));
    });
    test("48 kHz positional capture with a jump", [] {
        // feed the second half first, as if the user played bar 2 before bar 1
        const double sr = 48000;
        auto m = seq({ 60, 64, 67, 72 }, 0.2, 0.5, 0.4);
        auto x = synth(m, sr);
        std::vector<float> out((size_t) (x.size() * ml::kAnalysisRate / sr) + 10, 0.0f);
        ml::PositionalResampler rs;
        rs.prepare(sr);
        int64_t maxW = -1;
        const size_t half = x.size() / 2;
        for (size_t i = half; i < x.size(); ++i) rs.push((int64_t) i, x[i], out.data(), out.size(), maxW);
        for (size_t i = 0; i < half; ++i) rs.push((int64_t) i, x[i], out.data(), out.size(), maxW);
        ml::highpass(out.data(), out.size(), ml::kAnalysisRate, 60);
        auto track = ml::trackPitch(out.data(), out.size(), ml::kAnalysisRate, 65, 1100);
        checkMelody(m, ml::transcribe(track, {}));
    });
    test("key detection", [] {
        auto m = seq({ 57, 59, 60, 62, 64, 65, 67, 69, 64, 60, 57 }, 0.2, 0.3, 0.25);
        auto k = run(m).key;
        CHECK((k.tonic == 9 && !k.major) || (k.tonic == 0 && k.major), "key " + std::to_string(k.tonic));
    });
    test("post-processing", [] {
        std::vector<ml::Note> ns(2);
        ns[0].pitch = ns[0].detPitch = 61; ns[0].pitchRaw = 61.3; ns[0].start = ns[0].srcStart = 0.03; ns[0].end = 0.48;
        ns[1].pitch = ns[1].detPitch = 64; ns[1].pitchRaw = 64; ns[1].start = ns[1].srcStart = 0.52; ns[1].end = 0.97;
        ml::PostParams p; p.snap = true; p.tonic = 0; p.scale = ml::Major; p.transpose = 12; p.bpm = 120; p.grid = 2; p.quantize = true;
        auto out = ml::postProcess(ns, p);
        CHECK(out.size() == 2 && out[0].pitch == 74 && out[1].pitch == 76, "pitches");
        CHECK(std::fabs(out[0].start) < 1e-9 && std::fabs(out[0].end - 0.5) < 1e-9 && std::fabs(out[1].end - 1) < 1e-9, "times");
        CHECK(std::fabs(ml::gridSeconds(120, 3) - 1.0 / 6) < 1e-9, "triplet grid");
    });
    test("MIDI writer", [] {
        std::vector<ml::Note> ns(2);
        ns[0].pitch = 60; ns[0].start = 0; ns[0].end = 0.5; ns[0].vel = 100;
        ns[1].pitch = 62; ns[1].start = 0.5; ns[1].end = 1; ns[1].vel = 80;
        std::vector<ml::BendPoint> bends { { 0, 0.5 }, { 0.5, 0 } };
        auto b = ml::writeMidi(ns, 120, 480, "Test", &bends, 2);
        CHECK(std::memcmp(b.data(), "MThd", 4) == 0 && std::memcmp(b.data() + 14, "MTrk", 4) == 0, "headers");
        const uint32_t len = (b[18] << 24) | (b[19] << 16) | (b[20] << 8) | b[21];
        CHECK(len == b.size() - 22, "track length");
        size_t i = 22;
        uint32_t tick = 0;
        std::vector<std::pair<uint32_t, int>> ons;
        while (i < b.size())
        {
            uint32_t d = 0; uint8_t c;
            do { c = b[i++]; d = (d << 7) | (c & 127); } while (c & 128);
            tick += d;
            const uint8_t st = b[i++];
            if (st == 0xff) { ++i; uint32_t l = 0; do { c = b[i++]; l = (l << 7) | (c & 127); } while (c & 128); i += l; }
            else { if ((st & 0xf0) == 0x90) ons.push_back({ tick, b[i] }); i += 2; }
        }
        CHECK(ons.size() == 2 && ons[0] == std::make_pair(0u, 60) && ons[1] == std::make_pair(480u, 62), "note ons");
    });
    test("silence gives no notes", [] {
        std::vector<float> z(16000, 0.0f);
        auto tr = ml::trackPitch(z.data(), z.size(), ml::kAnalysisRate, 65, 1100);
        CHECK(ml::transcribe(tr, {}).notes.empty(), "notes in silence");
    });
    test("performance, 60 s", [] {
        std::vector<SynthNote> m;
        for (int i = 0; i < 150; ++i) m.push_back({ 55 + (i * 7) % 15, 0.1 + i * 0.4, 0.35 });
        auto x = synth(m, ml::kAnalysisRate);
        auto t0 = std::chrono::steady_clock::now();
        auto tr = ml::trackPitch(x.data(), x.size(), ml::kAnalysisRate, 65, 1100);
        auto r = ml::transcribe(tr, {});
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("     (60 s analysed in %.0f ms, %zu notes)\n", ms, r.notes.size());
        CHECK(r.notes.size() == m.size(), "count " + std::to_string(r.notes.size()));
    });


    // ------------------------------------------------------------- generator
    test("generator: every style, key and length makes a full pattern", [] {
        for (int st = 0; st < ml::gen::NumStyles; ++st)
            for (int bars : { 4, 8, 16 })
                for (int key : { 0, 5, 9 })
                {
                    ml::gen::Settings s; s.style = st; s.bars = bars; s.tonic = key;
                    ml::gen::Seeds sd; sd.chords = 11 + st; sd.rhythm = 22 + bars; sd.pitch = 33 + key;
                    auto p = ml::gen::generate(s, sd);
                    const std::string tag = std::string(ml::gen::styleName(st)) + " " + std::to_string(bars) + " bars key " + std::to_string(key);
                    CHECK(p.chords.size() == (size_t) bars, tag + ": chords");
                    for (int l = 0; l < ml::gen::NumLayers; ++l) CHECK(!p.layers[l].empty(), tag + ": empty layer " + ml::gen::layerName(l));
                    for (int l = 0; l < ml::gen::NumLayers; ++l)
                        for (auto& n : p.layers[l])
                            CHECK(n.beat >= -0.05 && n.beat + n.len <= p.beats + 0.6 && n.len > 0 && n.pitch >= 0 && n.pitch <= 127, tag + ": note bounds");
                    const auto& mel = p.layers[ml::gen::Melody];
                    for (size_t i = 0; i + 1 < mel.size(); ++i)
                        CHECK(mel[i].beat + mel[i].len <= mel[i + 1].beat + 1e-6, tag + ": melody overlaps");
                    for (auto& n : p.layers[ml::gen::Bass]) CHECK(n.pitch < 60, tag + ": bass register");
                }
    });
    test("generator: deterministic for the same seeds", [] {
        ml::gen::Settings s; s.style = ml::gen::PunjabiPop;
        ml::gen::Seeds sd; sd.pitch = 99;
        auto a = ml::gen::generate(s, sd), b = ml::gen::generate(s, sd);
        CHECK(a.layers[0].size() == b.layers[0].size(), "size");
        for (size_t i = 0; i < a.layers[0].size(); ++i)
            CHECK(a.layers[0][i].pitch == b.layers[0][i].pitch && a.layers[0][i].beat == b.layers[0][i].beat, "notes differ");
        sd.pitch = 100;
        auto c = ml::gen::generate(s, sd);
        bool differs = c.layers[0].size() != a.layers[0].size();
        for (size_t i = 0; !differs && i < a.layers[0].size(); ++i) differs = a.layers[0][i].pitch != c.layers[0][i].pitch;
        CHECK(differs, "a new pitch seed should change the hook");
    });
    test("generator: hooks follow the harmony and stay singable", [] {
        double ctSum = 0, scoreSum = 0;
        int n = 0;
        for (int st = 0; st < ml::gen::NumStyles; ++st)
            for (uint32_t seed = 1; seed <= 12; ++seed)
            {
                ml::gen::Settings s; s.style = st; s.ornaments = 0; s.humanize = 0;
                ml::gen::Seeds sd; sd.chords = seed; sd.rhythm = seed * 3; sd.pitch = seed * 7;
                auto p = ml::gen::generate(s, sd);
                auto h = ml::gen::analyseHook(p.layers[0], p.chords, p.tonic, p.scale, st);
                CHECK(h.span <= 22, std::string(ml::gen::styleName(st)) + ": span " + std::to_string(h.span));
                CHECK(h.distinct >= 3, std::string(ml::gen::styleName(st)) + ": too few pitches");
                // every melody note belongs to the scale bent by the chord under it
                for (auto& m : p.layers[0])
                {
                    bool ok = false;
                    for (auto& c : p.chords)
                        if (m.beat >= c.beat - 0.05 && m.beat < c.beat + c.len)
                        {
                            const int pc = ((m.pitch % 12) + 12) % 12;
                            ok = ml::inScale(m.pitch, p.tonic, p.scale) || std::find(c.pcs.begin(), c.pcs.end(), pc) != c.pcs.end();
                        }
                    CHECK(ok, std::string(ml::gen::styleName(st)) + ": out-of-key note " + std::to_string(m.pitch));
                }
                ctSum += h.chordToneStrong;
                scoreSum += h.score;
                ++n;
            }
        std::printf("     (strong-beat chord tones %.0f%%, mean hook score %.1f / 10)\n", 100 * ctSum / n, scoreSum / n);
        CHECK(ctSum / n > 0.85, "chord tones on strong beats");
        CHECK(scoreSum / n > 7.0, "hook score");
    });
    test("generator: the hook repeats its motif", [] {
        ml::gen::Settings s; s.style = ml::gen::Pop; s.repetition = 1.0; s.complexity = 0.0; s.ornaments = 0; s.humanize = 0;
        auto p = ml::gen::generate(s, {});
        std::vector<double> a, b;
        for (auto& n : p.layers[0]) { if (n.beat < 8) a.push_back(n.beat); else if (n.beat >= 16 && n.beat < 24) b.push_back(n.beat - 16); }
        CHECK(a.size() == b.size(), "phrase 3 should restate phrase 1's rhythm");
        for (size_t i = 0; i < a.size(); ++i) CHECK(std::fabs(a[i] - b[i]) < 1e-6, "rhythm differs");
    });
    test("generator: key and scale follow the settings", [] {
        ml::gen::Settings s; s.style = ml::gen::MelodicRap; s.tonic = 2; s.scale = ml::Minor; s.ornaments = 0;
        auto p = ml::gen::generate(s, {});
        CHECK(p.chords[0].name.rfind("D", 0) == 0 || p.chords[0].name.rfind("A#", 0) == 0 || p.chords[0].name.rfind("G", 0) == 0
              || p.chords[0].name.rfind("F", 0) == 0 || p.chords[0].name.rfind("C", 0) == 0 || p.chords[0].name.rfind("A", 0) == 0,
              "first chord " + p.chords[0].name);
        CHECK(p.chords[0].name.find('m') != std::string::npos || p.chords[0].name.find("maj") != std::string::npos
              || p.chords[0].name.find('9') != std::string::npos, "R&B chords carry extensions: " + p.chords[0].name);
    });
    test("generator: chords for a vocal melody", [] {
        // C major melody outlining C | F | G | C
        std::vector<ml::gen::GNote> mel;
        const int bars[4][3] = { { 60, 64, 67 }, { 65, 69, 72 }, { 67, 71, 74 }, { 72, 67, 64 } };
        for (int b = 0; b < 4; ++b)
            for (int k = 0; k < 3; ++k) mel.push_back({ b * 4.0 + k * 1.3, 1.2, bars[b][k], 96 });
        auto ch = ml::gen::harmonize(mel, 16, 0, ml::Major, ml::gen::Pop);
        CHECK(ch.size() == 4, "4 chords");
        CHECK(ch[0].name == "C" && ch[1].name == "F" && ch[2].name == "G" && ch[3].name == "C",
              "got " + ch[0].name + " " + ch[1].name + " " + ch[2].name + " " + ch[3].name);
        ml::gen::Settings s;
        auto p = ml::gen::buildAround(mel, 16, s, {});
        CHECK(p.layers[ml::gen::Bass].size() > 4 && p.layers[ml::gen::Chords].size() > 4, "layers around the vocal");
    });
    test("generator: multi-track MIDI file", [] {
        auto p = ml::gen::generate({}, {});
        std::vector<ml::gen::MidiTrack> tr;
        for (int l = 0; l < ml::gen::NumLayers; ++l) tr.push_back({ ml::gen::layerName(l), p.layers[l], l });
        auto b = ml::gen::writeMidiTracks(tr, 100);
        CHECK(std::memcmp(b.data(), "MThd", 4) == 0 && b[9] == 1 && b[11] == 5, "type 1 with 5 tracks");
        size_t pos = 14, chunks = 0;
        while (pos + 8 <= b.size())
        {
            CHECK(std::memcmp(b.data() + pos, "MTrk", 4) == 0, "chunk id");
            const uint32_t len = (b[pos + 4] << 24) | (b[pos + 5] << 16) | (b[pos + 6] << 8) | b[pos + 7];
            pos += 8 + len;
            ++chunks;
        }
        CHECK(chunks == 5 && pos == b.size(), "chunk layout");
    });

    std::printf("\n%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
