#include "MelodyCore.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace ml
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
const float kNaN = std::numeric_limits<float>::quiet_NaN();

const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

// Krumhansl-Kessler key profiles
const double kMajorProfile[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
const double kMinorProfile[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

std::atomic<int> gNoteId { 1 };

int nextPow2(int v)
{
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

double medianOf(std::vector<float> v)
{
    if (v.empty()) return std::nan("");
    const size_t n = v.size();
    std::sort(v.begin(), v.end());
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

float percentile(const std::vector<float>& a, double p)
{
    if (a.empty()) return 0.0f;
    std::vector<float> s(a);
    std::sort(s.begin(), s.end());
    const size_t idx = (size_t) std::clamp((long) std::lround(p / 100.0 * (double) (s.size() - 1)), 0L, (long) s.size() - 1);
    return s[idx];
}

// ----------------------------------------------------------------- FFT

class FFT
{
public:
    explicit FFT(int n) : n(n), cosT(n / 2), sinT(n / 2), rev(n)
    {
        int levels = 0;
        while ((1 << levels) < n) ++levels;
        for (int i = 0; i < n / 2; ++i)
        {
            cosT[i] = std::cos(2 * kPi * i / n);
            sinT[i] = std::sin(2 * kPi * i / n);
        }
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < levels; ++b) r = (r << 1) | ((i >> b) & 1);
            rev[i] = r;
        }
    }

    // In-place forward transform, X[k] = sum x[j] e^{-2πijk/n}
    void forward(double* re, double* im) const
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[i];
            if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        for (int size = 2; size <= n; size <<= 1)
        {
            const int half = size >> 1, step = n / size;
            for (int i = 0; i < n; i += size)
            {
                for (int j = i, k = 0; j < i + half; ++j, k += step)
                {
                    const int l = j + half;
                    const double tre = re[l] * cosT[k] + im[l] * sinT[k];
                    const double tim = im[l] * cosT[k] - re[l] * sinT[k];
                    re[l] = re[j] - tre;
                    im[l] = im[j] - tim;
                    re[j] += tre;
                    im[j] += tim;
                }
            }
        }
    }

private:
    int n;
    std::vector<double> cosT, sinT;
    std::vector<int> rev;
};

Prepared prepare(const PitchTrack& track, const TranscribeParams& p)
{
    const size_t T = track.size();
    Prepared out;
    const float peakDb = percentile(track.rmsDb, 99);
    const double gate = peakDb + p.gateDb;

    std::vector<float> raw(T, kNaN);
    for (size_t i = 0; i < T; ++i)
    {
        const double f = track.f0[i];
        if (f > 0 && track.conf[i] >= p.confMin && track.rmsDb[i] >= gate
            && f >= track.fmin * 0.97 && f <= track.fmax * 1.03)
            raw[i] = (float) hzToMidi(f);
    }

    // 5-frame median over voiced neighbours removes octave blips and jitter
    out.midi.assign(T, kNaN);
    std::vector<float> win;
    for (size_t i = 0; i < T; ++i)
    {
        if (std::isnan(raw[i])) continue;
        win.clear();
        for (long k = (long) i - 2; k <= (long) i + 2; ++k)
            if (k >= 0 && k < (long) T && !std::isnan(raw[k])) win.push_back(raw[k]);
        const double med = medianOf(win);
        out.midi[i] = std::fabs(raw[i] - med) > 0.8 ? (float) med : raw[i];
    }

    // drop isolated voiced specks (< 3 frames): usually noise or breaths
    for (size_t i = 0; i < T;)
    {
        if (std::isnan(out.midi[i])) { ++i; continue; }
        size_t j = i;
        while (j < T && !std::isnan(out.midi[j])) ++j;
        if (j - i < 3) for (size_t k = i; k < j; ++k) out.midi[k] = kNaN;
        i = j;
    }

    const auto& src = track.envDb.size() == T ? track.envDb : track.rmsDb;
    out.env.assign(T, 0.0f);
    for (size_t i = 0; i < T; ++i)
    {
        double s = 0;
        int c = 0;
        for (long k = (long) i - 1; k <= (long) i + 1; ++k)
            if (k >= 0 && k < (long) T) { s += src[k]; ++c; }
        out.env[i] = (float) (s / c);
    }

    if (p.autoTuning)
    {
        double sx = 0, sy = 0;
        for (size_t i = 0; i < T; ++i)
        {
            if (std::isnan(out.midi[i])) continue;
            const double a = 2 * kPi * out.midi[i];
            sx += track.conf[i] * std::cos(a);
            sy += track.conf[i] * std::sin(a);
        }
        double off = (sx == 0 && sy == 0) ? 0.0 : std::atan2(sy, sx) / (2 * kPi);
        if (off >= 0.5) off -= 1;
        if (off < -0.5) off += 1;
        out.tuning = off;
    }
    else
        out.tuning = p.tuningCents / 100.0;

    return out;
}

// Viterbi over {silence, note lo..hi}. Uniform switching costs keep it O(T*K).
std::vector<int> decode(const Prepared& prep, const TranscribeParams& p)
{
    const auto& midi = prep.midi;
    const size_t T = midi.size();
    std::vector<int> path(T, -1);
    double lo = 1e9, hi = -1e9;
    for (float m : midi)
        if (!std::isnan(m)) { lo = std::min(lo, m - prep.tuning); hi = std::max(hi, m - prep.tuning); }
    if (lo > hi) return path;
    const int nlo = std::max(0, (int) std::floor(lo) - 1);
    const int nhi = std::min(127, (int) std::ceil(hi) + 1);
    const int K = nhi - nlo + 2;

    const double unvoicedNotePenalty = 2.5, voicedSilencePenalty = 3.0, cap = 12.0;
    const double inv2s2 = 1.0 / (2 * p.sigma * p.sigma);

    std::vector<double> prev(K, -1e9), cur(K);
    std::vector<int16_t> back(T * (size_t) K);
    prev[0] = 0;

    for (size_t t = 0; t < T; ++t)
    {
        double b1 = -1e300, b2 = -1e300;
        int b1i = -1, b2i = -1;
        for (int s = 1; s < K; ++s)
        {
            const double v = prev[s];
            if (v > b1) { b2 = b1; b2i = b1i; b1 = v; b1i = s; }
            else if (v > b2) { b2 = v; b2i = s; }
        }
        const float m = midi[t];
        const bool voiced = !std::isnan(m);
        int16_t* bk = back.data() + t * (size_t) K;

        {
            const double stay = prev[0], enter = b1 - p.silenceCost;
            if (stay >= enter) { cur[0] = stay; bk[0] = 0; }
            else { cur[0] = enter; bk[0] = (int16_t) b1i; }
            if (voiced) cur[0] -= voicedSilencePenalty;
        }
        for (int s = 1; s < K; ++s)
        {
            const double other = s == b1i ? b2 : b1;
            double best = prev[s];
            int from = s;
            if (other - p.changeCost > best) { best = other - p.changeCost; from = s == b1i ? b2i : b1i; }
            if (prev[0] - p.silenceCost > best) { best = prev[0] - p.silenceCost; from = 0; }
            double e;
            if (voiced)
            {
                const double dv = m - prep.tuning - (nlo + s - 1);
                e = -std::min(dv * dv * inv2s2, cap);
            }
            else
                e = -unvoicedNotePenalty;
            cur[s] = best + e;
            bk[s] = (int16_t) from;
        }
        std::swap(prev, cur);
    }

    int st = 0;
    double bv = -1e300;
    for (int s = 0; s < K; ++s) if (prev[s] > bv) { bv = prev[s]; st = s; }
    for (long t = (long) T - 1; t >= 0; --t)
    {
        path[t] = st == 0 ? -1 : nlo + st - 1;
        st = back[(size_t) t * K + st];
    }
    return path;
}

std::vector<size_t> splitOnDips(const std::vector<float>& env, size_t a, size_t b, double splitDb)
{
    std::vector<size_t> cuts;
    if (!(splitDb > 0)) return cuts;
    double peak = -1e9, minVal = 1e9;
    long minIdx = -1;
    for (size_t j = a; j < b; ++j)
    {
        const double v = env[j];
        if (minIdx < 0)
        {
            if (v > peak) peak = v;
            if (peak - v >= splitDb) { minIdx = (long) j; minVal = v; }
        }
        else
        {
            if (v < minVal) { minVal = v; minIdx = (long) j; }
            if (v - minVal >= splitDb)
            {
                if ((size_t) minIdx - a >= 3 && b - (size_t) minIdx >= 3) cuts.push_back((size_t) minIdx);
                peak = v;
                minIdx = -1;
                minVal = 1e9;
            }
        }
    }
    return cuts;
}

std::vector<Note> absorbShortNotes(std::vector<Note> out, double minDur, double hopSec)
{
    const double touch = hopSec * 2.5;
    for (;;)
    {
        long idx = -1;
        double shortest = minDur;
        for (size_t i = 0; i < out.size(); ++i)
        {
            const double d = out[i].end - out[i].start;
            if (d < shortest) { shortest = d; idx = (long) i; }
        }
        if (idx < 0) break;
        const Note n = out[idx];
        Note* prev = idx > 0 && n.start - out[idx - 1].end <= touch ? &out[idx - 1] : nullptr;
        Note* next = idx + 1 < (long) out.size() && out[idx + 1].start - n.end <= touch ? &out[idx + 1] : nullptr;
        Note* target = nullptr;
        if (prev && next)
            target = std::fabs(prev->pitchRaw - n.pitchRaw) <= std::fabs(next->pitchRaw - n.pitchRaw) ? prev : next;
        else
            target = prev ? prev : next;
        if (target == prev && prev) { prev->end = n.end; prev->srcB = n.srcB; }
        else if (target)
        {
            target->start = n.start;
            target->srcStart = n.srcStart;
            target->srcA = n.srcA;
        }
        out.erase(out.begin() + idx);
    }
    return out;
}

double pearson(const double* a, const double* b, int n)
{
    double ma = 0, mb = 0;
    for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= n;
    mb /= n;
    double num = 0, da = 0, db = 0;
    for (int i = 0; i < n; ++i)
    {
        num += (a[i] - ma) * (b[i] - mb);
        da += (a[i] - ma) * (a[i] - ma);
        db += (b[i] - mb) * (b[i] - mb);
    }
    return da > 0 && db > 0 ? num / std::sqrt(da * db) : 0;
}

void putVlq(std::vector<uint8_t>& out, uint32_t n)
{
    uint8_t buf[5];
    int len = 0;
    buf[len++] = n & 0x7f;
    n >>= 7;
    while (n > 0) { buf[len++] = (uint8_t) ((n & 0x7f) | 0x80); n >>= 7; }
    while (len > 0) out.push_back(buf[--len]);
}

void putU32(std::vector<uint8_t>& out, uint32_t n)
{
    out.push_back((n >> 24) & 255);
    out.push_back((n >> 16) & 255);
    out.push_back((n >> 8) & 255);
    out.push_back(n & 255);
}
} // namespace

// --------------------------------------------------------------- basics

const std::vector<int>& scaleSteps(int scale)
{
    static const std::vector<int> scales[NumScales] = {
        { 0, 2, 4, 5, 7, 9, 11 },  // major
        { 0, 2, 3, 5, 7, 8, 10 },  // natural minor
        { 0, 2, 3, 5, 7, 8, 11 },  // harmonic minor
        { 0, 2, 3, 5, 7, 9, 10 },  // dorian
        { 0, 2, 4, 5, 7, 9, 10 },  // mixolydian
        { 0, 2, 4, 7, 9 },         // major pentatonic
        { 0, 3, 5, 7, 10 },        // minor pentatonic
        { 0, 1, 3, 5, 7, 8, 10 },  // phrygian
        { 0, 1, 4, 5, 7, 8, 10 },  // phrygian dominant
    };
    return scales[std::clamp(scale, 0, (int) NumScales - 1)];
}

double hzToMidi(double f) { return 69.0 + 12.0 * std::log2(f / 440.0); }
double midiToHz(double m) { return 440.0 * std::pow(2.0, (m - 69.0) / 12.0); }
const char* pitchClassName(int pc) { return kNoteNames[((pc % 12) + 12) % 12]; }
std::string noteName(int pitch)
{
    return std::string(pitchClassName(pitch)) + std::to_string((int) std::floor(pitch / 12.0) - 1);
}
int newNoteId() { return gNoteId++; }

double gridSeconds(double bpm, int grid)
{
    static const double divs[] = { 0, 4, 8, 8, 16, 16, 32 };
    if (grid <= 0 || grid > 6 || bpm <= 0) return 0;
    const bool triplet = grid == 3 || grid == 5;
    return (60.0 / bpm) * (4.0 / divs[grid]) * (triplet ? 2.0 / 3.0 : 1.0);
}

void highpass(float* x, size_t n, double sr, double freq, double q)
{
    const double w0 = 2 * kPi * freq / sr;
    const double alpha = std::sin(w0) / (2 * q);
    const double cw = std::cos(w0);
    const double a0 = 1 + alpha;
    const double b0 = (1 + cw) / 2 / a0, b1 = -(1 + cw) / a0, b2 = (1 + cw) / 2 / a0;
    const double a1 = -2 * cw / a0, a2 = (1 - alpha) / a0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double x0 = x[i];
        const double y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x0; y2 = y1; y1 = y0;
        x[i] = (float) y0;
    }
}

// ------------------------------------------------------------------ YIN

PitchTrack trackPitch(const float* signal, size_t len, double sr, double fmin, double fmax, const Progress& progress)
{
    PitchTrack tr;
    tr.sr = sr;
    tr.fmin = fmin;
    tr.fmax = fmax;
    tr.hop = (int) std::lround(sr * 0.01);
    const double threshold = 0.15;
    const int hop = tr.hop;
    const int tauMin = std::max(2, (int) std::floor(sr / fmax));
    const int tauMax = (int) std::ceil(sr / fmin);
    const int W = std::max(2 * tauMax, 256);
    const int N = W + tauMax + 2;
    const int M = nextPow2(N);
    FFT fft(M);

    const int padL = W >> 1;
    const size_t total = len + padL + N;
    std::vector<float> x(total, 0.0f);
    std::copy(signal, signal + len, x.begin() + padL);
    std::vector<double> sq(total + 1, 0.0);
    for (size_t i = 0; i < total; ++i) sq[i + 1] = sq[i] + (double) x[i] * x[i];

    const size_t frames = std::max<size_t>(1, (len + hop - 1) / hop);
    tr.f0.assign(frames, 0);
    tr.conf.assign(frames, 0);
    tr.rmsDb.assign(frames, -180);
    tr.envDb.assign(frames, -180);

    std::vector<double> re(M), im(M), cr(M), ci(M), d(tauMax + 2), dn(tauMax + 2);

    for (size_t fi = 0; fi < frames; ++fi)
    {
        if (progress && fi % 500 == 0 && !progress((float) fi / frames)) return {};
        const size_t s = fi * hop;
        const double e1 = sq[s + W] - sq[s];
        const double rms = std::sqrt(e1 / W);
        tr.rmsDb[fi] = rms > 1e-9 ? (float) (20 * std::log10(rms)) : -180.0f;
        {
            const long c0 = (long) (s + padL) - (hop >> 1);
            const double es = c0 >= 0 ? (sq[c0 + hop] - sq[c0]) / hop : 0;
            tr.envDb[fi] = es > 1e-18 ? (float) (10 * std::log10(es)) : -180.0f;
        }
        if (rms < 1e-6) continue;

        // r(tau) = sum_{j<W} x[s+j] x[s+j+tau] via one complex FFT that holds
        // the window (real part) and the extended segment (imag part)
        for (int j = 0; j < M; ++j)
        {
            re[j] = j < W ? x[s + j] : 0.0;
            im[j] = j < N ? x[s + j] : 0.0;
        }
        fft.forward(re.data(), im.data());
        for (int k = 0; k < M; ++k)
        {
            const int m = (M - k) & (M - 1);
            const double zr = re[k], zi = im[k], mr = re[m], mi = im[m];
            const double ar = (zr + mr) * 0.5, ai = (zi - mi) * 0.5;
            const double br = (zi + mi) * 0.5, bi = (mr - zr) * 0.5;
            cr[k] = ar * br + ai * bi;
            ci[k] = -(ar * bi - ai * br);
        }
        fft.forward(cr.data(), ci.data());

        d[0] = 0;
        dn[0] = 1;
        double running = 0;
        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            const double e2 = sq[s + tau + W] - sq[s + tau];
            const double v = std::max(0.0, e1 + e2 - 2 * cr[tau] / M);
            d[tau] = v;
            running += v;
            dn[tau] = running > 0 ? v * tau / running : 1;
        }

        int best = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            if (dn[tau] < threshold)
            {
                while (tau + 1 <= tauMax && dn[tau + 1] < dn[tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            double mv = 1e300;
            for (int tau = tauMin; tau <= tauMax; ++tau)
                if (dn[tau] < mv) { mv = dn[tau]; best = tau; }
        }
        double refined = best;
        if (best > 1 && best < tauMax + 1)
        {
            const double y0 = dn[best - 1], y1 = dn[best], y2 = dn[best + 1];
            const double den = y0 - 2 * y1 + y2;
            if (std::fabs(den) > 1e-12)
            {
                const double off = 0.5 * (y0 - y2) / den;
                if (std::fabs(off) < 1) refined = best + off;
            }
        }
        tr.conf[fi] = (float) std::clamp(1.0 - dn[best], 0.0, 1.0);
        tr.f0[fi] = refined > 0 ? (float) (sr / refined) : 0.0f;
    }
    if (progress) progress(1.0f);
    return tr;
}

// -------------------------------------------------------- transcription

Transcription transcribe(const PitchTrack& track, const TranscribeParams& p)
{
    Transcription res;
    if (track.empty()) return res;
    res.prep = prepare(track, p);
    const auto& midi = res.prep.midi;
    const auto& env = res.prep.env;
    const double tuning = res.prep.tuning;
    const double hopSec = track.hopSec();
    const auto path = decode(res.prep, p);
    const size_t T = path.size();

    struct Seg { size_t a, b; };
    std::vector<Seg> segs;
    for (size_t i = 0; i < T;)
    {
        if (path[i] < 0) { ++i; continue; }
        size_t j = i;
        while (j < T && path[j] == path[i]) ++j;
        auto cuts = splitOnDips(env, i, j, p.splitDb);
        cuts.push_back(j);
        size_t a = i;
        for (size_t c : cuts) { segs.push_back({ a, c }); a = c; }
        i = j;
    }

    std::vector<Note> notes;
    std::vector<float> vals;
    for (const auto& sg : segs)
    {
        vals.clear();
        double eSum = 0;
        for (size_t k = sg.a; k < sg.b; ++k)
        {
            if (!std::isnan(midi[k])) vals.push_back((float) (midi[k] - tuning));
            eSum += std::pow(10.0, env[k] / 20.0);
        }
        if ((double) vals.size() < std::max(2.0, 0.3 * (double) (sg.b - sg.a))) continue;
        size_t a = sg.a, b = sg.b;
        while (a < b && std::isnan(midi[a])) ++a;
        while (b > a && std::isnan(midi[b - 1])) --b;
        const double med = medianOf(vals);
        Note n;
        n.id = newNoteId();
        n.pitch = n.detPitch = (int) std::lround(med);
        n.pitchRaw = med;
        n.start = n.srcStart = a * hopSec;
        n.end = b * hopSec;
        n.db = 20 * std::log10(eSum / (double) (sg.b - sg.a) + 1e-12);
        n.srcA = (int) a;
        n.srcB = (int) b;
        notes.push_back(n);
    }

    notes = absorbShortNotes(std::move(notes), p.minNoteMs / 1000.0, hopSec);

    if (!notes.empty())
    {
        double top = -1e9;
        for (const auto& n : notes) top = std::max(top, n.db);
        for (auto& n : notes)
        {
            const double rel = std::max(-30.0, n.db - top);
            n.vel = (int) std::lround(std::clamp(112 + rel * 2.6, 30.0, 127.0));
        }
    }
    res.key = detectKey(notes);
    res.notes = std::move(notes);
    return res;
}

KeyResult detectKey(const std::vector<Note>& notes)
{
    double hist[12] = {};
    bool any = false;
    for (const auto& n : notes)
    {
        hist[((n.pitch % 12) + 12) % 12] += n.end - n.start;
        any = true;
    }
    KeyResult best;
    if (!any) return best;
    best.score = -2;
    for (int tonic = 0; tonic < 12; ++tonic)
    {
        double rot[12];
        for (int i = 0; i < 12; ++i) rot[i] = hist[(i + tonic) % 12];
        for (int mode = 0; mode < 2; ++mode)
        {
            const double r = pearson(rot, mode == 0 ? kMajorProfile : kMinorProfile, 12);
            if (r > best.score) { best.score = r; best.tonic = tonic; best.major = mode == 0; }
        }
    }
    return best;
}

bool inScale(int pitch, int tonic, int scale)
{
    const int pc = (((pitch - tonic) % 12) + 12) % 12;
    const auto& st = scaleSteps(scale);
    return std::find(st.begin(), st.end(), pc) != st.end();
}

std::vector<Note> postProcess(const std::vector<Note>& notes, const PostParams& p)
{
    std::vector<Note> out(notes);
    if (p.snap)
    {
        for (auto& n : out)
        {
            if (inScale(n.pitch, p.tonic, p.scale)) continue;
            const int dir = n.pitchRaw >= n.pitch ? 1 : -1;
            for (int d = 1; d <= 2; ++d)
            {
                if (inScale(n.pitch + dir * d, p.tonic, p.scale)) { n.pitch += dir * d; break; }
                if (inScale(n.pitch - dir * d, p.tonic, p.scale)) { n.pitch -= dir * d; break; }
            }
        }
    }
    if (p.transpose != 0)
        for (auto& n : out) n.pitch = std::clamp(n.pitch + p.transpose, 0, 127);

    const double g = p.quantize ? gridSeconds(p.bpm, p.grid) : 0;
    if (g > 0 && p.strength > 0)
    {
        for (auto& n : out)
        {
            const double qs = std::round(n.start / g) * g;
            double qe = std::round(n.end / g) * g;
            if (qe <= qs) qe = qs + g;
            n.start += (qs - n.start) * p.strength;
            n.end += (qe - n.end) * p.strength;
        }
        std::stable_sort(out.begin(), out.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
        std::vector<Note> kept;
        for (auto& n : out)
        {
            if (!kept.empty() && n.start <= kept.back().start + 1e-6)
            {
                const auto& last = kept.back();
                const double ln = (n.srcB - n.srcA), ll = (last.srcB - last.srcA);
                if (ln > ll) kept.back() = n;
                continue;
            }
            if (!kept.empty() && kept.back().end > n.start) kept.back().end = n.start;
            kept.push_back(n);
        }
        out = std::move(kept);
    }
    return out;
}

std::vector<BendPoint> bendCurve(const Note& n, const Prepared& prep, const PitchTrack& track)
{
    std::vector<BendPoint> pts;
    if (n.srcA < 0 || n.srcB <= n.srcA) return pts;
    const double shift = n.start - n.srcStart;
    for (int k = n.srcA; k < n.srcB && k < (int) prep.midi.size(); ++k)
    {
        if (std::isnan(prep.midi[k])) continue;
        const double t = k * track.hopSec() + shift;
        if (t < n.start - 1e-6 || t >= n.end) continue;
        pts.push_back({ t, prep.midi[k] - prep.tuning - n.detPitch });
    }
    return pts;
}

std::vector<BendPoint> buildBendEvents(const std::vector<Note>& notes, const Prepared& prep,
                                       const PitchTrack& track, double range)
{
    std::vector<BendPoint> out;
    for (const auto& n : notes)
    {
        const auto pts = bendCurve(n, prep, track);
        if (pts.empty()) continue;
        bool first = true;
        double lastV = 0, lastT = -1;
        for (const auto& pt : pts)
        {
            const double v = std::clamp(pt.v, -range, range);
            if (first || std::fabs(v - lastV) >= 0.03 || pt.t - lastT >= 0.08)
            {
                out.push_back({ std::max(n.start, pt.t), v });
                lastV = v;
                lastT = pt.t;
                first = false;
            }
        }
        out.push_back({ n.end, 0.0 });
    }
    return out;
}

// ----------------------------------------------------------------- MIDI

std::vector<uint8_t> writeMidi(const std::vector<Note>& notes, double bpm, int ppq, const std::string& name,
                               const std::vector<BendPoint>* bends, int bendRange, int tsNum, int tsDen)
{
    if (bpm <= 0) bpm = 120;
    auto toTick = [&](double sec) { return (uint32_t) std::max(0L, std::lround(sec * bpm * ppq / 60.0)); };
    struct Ev { uint32_t tick; int ord; std::vector<uint8_t> data; };
    std::vector<Ev> ev;

    const uint32_t us = (uint32_t) std::lround(60000000.0 / bpm);
    {
        std::vector<uint8_t> d { 0xff, 0x03 };
        putVlq(d, (uint32_t) name.size());
        d.insert(d.end(), name.begin(), name.end());
        ev.push_back({ 0, 0, d });
    }
    ev.push_back({ 0, 0, { 0xff, 0x51, 0x03, (uint8_t) ((us >> 16) & 255), (uint8_t) ((us >> 8) & 255), (uint8_t) (us & 255) } });
    int denPow = 0;
    while ((1 << denPow) < tsDen && denPow < 6) ++denPow;
    ev.push_back({ 0, 0, { 0xff, 0x58, 0x04, (uint8_t) std::clamp(tsNum, 1, 32), (uint8_t) denPow, 24, 8 } });

    if (bends && !bends->empty())
    {
        const int range = std::clamp(bendRange, 1, 24);
        const uint8_t rpn[6][2] = { { 101, 0 }, { 100, 0 }, { 6, (uint8_t) range }, { 38, 0 }, { 101, 127 }, { 100, 127 } };
        for (auto& r : rpn) ev.push_back({ 0, 0, { 0xb0, r[0], r[1] } });
        for (const auto& b : *bends)
        {
            const int val = std::clamp((int) std::lround(8192 + b.v / range * 8191), 0, 16383);
            ev.push_back({ toTick(b.t), 2, { 0xe0, (uint8_t) (val & 127), (uint8_t) ((val >> 7) & 127) } });
        }
    }
    for (const auto& n : notes)
    {
        const uint32_t a = toTick(n.start);
        const uint32_t b = std::max(a + 1, toTick(n.end));
        const uint8_t pitch = (uint8_t) std::clamp(n.pitch, 0, 127);
        const uint8_t vel = (uint8_t) std::clamp(n.vel > 0 ? n.vel : 96, 1, 127);
        ev.push_back({ a, 3, { 0x90, pitch, vel } });
        ev.push_back({ b, 1, { 0x80, pitch, 0 } });
    }
    std::stable_sort(ev.begin(), ev.end(), [](const Ev& x, const Ev& y) {
        return x.tick != y.tick ? x.tick < y.tick : x.ord < y.ord;
    });

    std::vector<uint8_t> trk;
    uint32_t last = 0;
    for (const auto& e : ev)
    {
        putVlq(trk, e.tick - last);
        trk.insert(trk.end(), e.data.begin(), e.data.end());
        last = e.tick;
    }
    trk.insert(trk.end(), { 0x00, 0xff, 0x2f, 0x00 });

    std::vector<uint8_t> out { 'M', 'T', 'h', 'd' };
    putU32(out, 6);
    out.insert(out.end(), { 0, 0, 0, 1, (uint8_t) ((ppq >> 8) & 255), (uint8_t) (ppq & 255), 'M', 'T', 'r', 'k' });
    putU32(out, (uint32_t) trk.size());
    out.insert(out.end(), trk.begin(), trk.end());
    return out;
}

// ----------------------------------------------------------- resampling

void PositionalResampler::prepare(double hostRate)
{
    ratio = kAnalysisRate / hostRate;
    // two-stage Butterworth-ish low-pass at 7 kHz (anti-alias for 16 kHz)
    const double fc = std::min(7000.0, hostRate * 0.45);
    const double qs[2] = { 0.5412, 1.3066 };
    for (int i = 0; i < 2; ++i)
    {
        const double w0 = 2 * kPi * fc / hostRate;
        const double alpha = std::sin(w0) / (2 * qs[i]);
        const double cw = std::cos(w0);
        const double a0 = 1 + alpha;
        stages[i].b0 = (1 - cw) / 2 / a0;
        stages[i].b1 = (1 - cw) / a0;
        stages[i].b2 = (1 - cw) / 2 / a0;
        stages[i].a1 = -2 * cw / a0;
        stages[i].a2 = (1 - alpha) / a0;
    }
    reset();
    lastK = -10;
}

void PositionalResampler::reset()
{
    for (auto& s : stages) s.z1 = s.z2 = 0;
}

std::vector<float> resampleToAnalysis(const float* x, size_t n, double sr)
{
    const size_t outLen = (size_t) std::ceil((double) n * kAnalysisRate / sr);
    std::vector<float> out(outLen + 1, 0.0f);
    PositionalResampler rs;
    rs.prepare(sr);
    int64_t maxW = -1;
    for (size_t i = 0; i < n; ++i) rs.push((int64_t) i, x[i], out.data(), out.size(), maxW);
    out.resize(outLen);
    return out;
}

} // namespace ml
