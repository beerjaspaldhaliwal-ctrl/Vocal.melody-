// Tests for the JUCE-free core. Synthesises sung melodies and checks the
// transcription, mirroring test/run-tests.mjs for the web app.
#include "../Source/core/MelodyCore.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
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

    std::printf("\n%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
