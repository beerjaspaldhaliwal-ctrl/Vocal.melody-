// Small polyphonic preview synth: a melody voice (lead synth or a formant
// "aah" vocal), a pad, an 808-style bass and a pluck, one per generator
// layer. Plays the melody's sung expression (scoops, meend slides, vibrato,
// falls). Real-time safe (no allocation).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

class PreviewSynth
{
public:
    struct Expression
    {
        float scoop = 0;       // start offset in semitones (scoop or slide), glides to 0
        float glideSec = 0.06f;
        float vib = 0;         // vibrato depth in semitones
        float vibOnsetSec = 0.2f;
        bool fall = false;
        float fallSec = 0.12f;
    };

    void prepare(double sampleRate)
    {
        sr = sampleRate;
        // vowel "aah" formants (frequency, bandwidth, gain)
        const double f[3][3] = { { 800, 90, 1.0 }, { 1200, 110, 0.6 }, { 2800, 170, 0.28 } };
        for (int i = 0; i < 3; ++i)
        {
            const double w0 = 2 * 3.141592653589793 * f[i][0] / sr;
            const double q = f[i][0] / f[i][1];
            const double alpha = std::sin(w0) / (2 * q);
            const double a0 = 1 + alpha;
            formant[i] = { alpha / a0, 0.0, -alpha / a0, -2 * std::cos(w0) / a0, (1 - alpha) / a0, f[i][2] };
        }
        allOff(true);
    }

    void setMelodyTimbre(int t) { melodyTimbre = t; }

    /** Starts a note `delay` samples into the next render() call, lasting `length` samples. */
    void noteOn(int layer, int pitch, int vel, int64_t length, int delay) { noteOn(layer, pitch, vel, length, delay, Expression()); }

    void noteOn(int layer, int pitch, int vel, int64_t length, int delay, const Expression& ex)
    {
        Voice* v = nullptr;
        for (auto& x : voices) if (!x.active) { v = &x; break; }
        if (v == nullptr)
        {
            v = &voices[0];
            for (auto& x : voices) if (x.age > v->age) v = &x;
        }
        *v = Voice {};
        v->active = true;
        v->layer = layer;
        v->freq = 440.0 * std::pow(2.0, (pitch - 69) / 12.0);
        v->amp = (float) (vel / 127.0);
        v->remaining = std::max<int64_t>(1, length);
        v->delay = std::max(0, delay);
        v->phase2 = 0.37; // detune partner starts out of phase
        v->ex = ex;
        v->hasExpr = ex.scoop != 0 || ex.vib > 0 || ex.fall;
    }

    void allOff(bool hard = false)
    {
        for (auto& v : voices)
        {
            if (hard) v.active = false;
            else v.releasing = true;
        }
    }

    /** Adds the voices into `left`/`right` (scratch buffers the caller clears), soft-clipped. */
    void render(float* left, float* right, int n, float gain, const bool* muted)
    {
        for (auto& v : voices)
        {
            if (!v.active) continue;
            const bool mute = muted != nullptr && muted[v.layer];
            for (int i = 0; i < n; ++i)
            {
                if (v.delay > 0) { --v.delay; continue; }
                const float s = tick(v) * gain;
                if (!mute)
                {
                    left[i] += s;
                    right[i] += s;
                }
                if (!v.active) break;
            }
        }
        for (int i = 0; i < n; ++i)
        {
            left[i] = std::tanh(left[i]);
            right[i] = std::tanh(right[i]);
        }
    }

private:
    struct Biquad { double b0, b1, b2, a1, a2, gain; };

    struct Voice
    {
        bool active = false, releasing = false, hasExpr = false;
        int layer = 0, delay = 0;
        double freq = 440, phase = 0, phase2 = 0, t = 0;
        float amp = 1, env = 0, lp = 0;
        int64_t remaining = 0;
        int64_t age = 0;
        Expression ex;
        double fz1[3] = {}, fz2[3] = {};
        uint32_t noise = 22222;
    };

    static double polyBlep(double t, double dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }

    double saw(double& ph, double f)
    {
        const double dt = f / sr;
        double v = 2.0 * ph - 1.0 - polyBlep(ph, dt);
        ph += dt;
        if (ph >= 1.0) ph -= 1.0;
        return v;
    }

    /** Pitch offset in semitones from the note's sung expression. */
    double expressionOffset(const Voice& v) const
    {
        double off = 0;
        const auto& ex = v.ex;
        if (ex.scoop != 0 && v.t < ex.glideSec)
        {
            const double u = v.t / std::max(1e-4f, ex.glideSec);
            off += ex.scoop * (1.0 - u * u * (3.0 - 2.0 * u));
        }
        if (ex.vib > 0 && v.t > ex.vibOnsetSec)
            off += ex.vib * std::min(1.0, (v.t - ex.vibOnsetSec) / 0.15) * std::sin(6.283185307179586 * 5.5 * v.t);
        if (ex.fall && !v.releasing)
        {
            const double left = v.remaining / sr;
            if (left < ex.fallSec)
            {
                const double u = 1.0 - left / std::max(1e-4f, ex.fallSec);
                off -= 1.5 * u * u;
            }
        }
        return off;
    }

    float tick(Voice& v)
    {
        constexpr double twoPi = 6.283185307179586;
        ++v.age;
        if (!v.releasing && --v.remaining <= 0) v.releasing = true;
        const double dt = 1.0 / sr;
        v.t += dt;
        const bool vocal = v.layer == 0 && melodyTimbre == 1;

        // envelope per layer: attack, decay/sustain, release (seconds)
        float attack = 0.005f, release = 0.08f, sustain = 0.75f, decay = 0.2f;
        switch (v.layer)
        {
            case 0: if (vocal) { attack = 0.04f; release = 0.12f; sustain = 0.85f; decay = 0.3f; } break;
            case 1: attack = 0.03f; release = 0.25f; sustain = 0.9f; decay = 0.4f; break;
            case 2: attack = 0.002f; release = 0.12f; sustain = 0.85f; decay = 1.2f; break;
            case 3: attack = 0.001f; release = 0.1f; sustain = 0.0f; decay = 0.28f; break;
            default: break;
        }
        if (v.releasing)
        {
            v.env -= (float) (dt / release) * std::max(v.env, 0.05f) * 3.0f;
            if (v.env <= 0.0005f) { v.active = false; return 0.0f; }
        }
        else if (v.t < attack)
            v.env = (float) (v.t / attack);
        else
            v.env += (sustain - v.env) * (float) (dt / decay) * 3.0f;

        double s = 0;
        switch (v.layer)
        {
            case 0:
            {
                const double bend = v.hasExpr ? std::pow(2.0, expressionOffset(v) / 12.0) : 1.0;
                if (vocal)
                {
                    // glottal-ish saw plus a little breath, through three vowel formants
                    v.noise = v.noise * 1664525u + 1013904223u;
                    const double breath = ((v.noise >> 9) / 4194304.0 - 1.0) * 0.06;
                    const double src = saw(v.phase, v.freq * bend) + breath;
                    double y = 0;
                    for (int k = 0; k < 3; ++k)
                    {
                        const auto& c = formant[k];
                        const double o = c.b0 * src + v.fz1[k];
                        v.fz1[k] = c.b1 * src - c.a1 * o + v.fz2[k];
                        v.fz2[k] = c.b2 * src - c.a2 * o;
                        y += c.gain * o;
                    }
                    s = y * 0.55;
                }
                else
                {
                    // lead: two saws, gentle delayed vibrato unless the note brings its own
                    const double vib = v.hasExpr ? 1.0 : 1.0 + 0.004 * std::sin(twoPi * 5.5 * v.t) * std::min(1.0, std::max(0.0, (v.t - 0.25) * 3));
                    s = 0.6 * saw(v.phase, v.freq * vib * bend) + 0.4 * saw(v.phase2, v.freq * vib * bend * 1.004);
                    v.lp += (float) ((s - v.lp) * 0.35);
                    s = v.lp * 0.22;
                }
                break;
            }
            case 1: // pad: detuned saws, dark low-pass
            {
                s = 0.5 * saw(v.phase, v.freq * 0.997) + 0.5 * saw(v.phase2, v.freq * 1.003);
                v.lp += (float) ((s - v.lp) * 0.12);
                s = v.lp * 0.09;
                break;
            }
            case 2: // 808: sine with a pitch drop and soft drive
            {
                const double f = v.freq * (1.0 + 1.0 * std::exp(-v.t / 0.03));
                v.phase += f * dt;
                if (v.phase >= 1.0) v.phase -= 1.0;
                s = std::tanh(1.8 * std::sin(twoPi * v.phase)) * 0.42;
                break;
            }
            default: // pluck: triangle-ish with fast decay
            {
                v.phase += v.freq * dt;
                if (v.phase >= 1.0) v.phase -= 1.0;
                s = (4.0 * std::fabs(v.phase - 0.5) - 1.0) * 0.2;
                break;
            }
        }
        return (float) (s * v.env * v.amp);
    }

    double sr = 44100.0;
    int melodyTimbre = 1;
    Biquad formant[3] {};
    Voice voices[48];
};
