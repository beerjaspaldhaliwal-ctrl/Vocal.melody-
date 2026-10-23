// Small polyphonic preview synth: a lead, a pad, an 808-style bass and a
// pluck, one per generator layer. Real-time safe (no allocation).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

class PreviewSynth
{
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        allOff(true);
    }

    /** Starts a note `delay` samples into the next render() call, lasting `length` samples. */
    void noteOn(int layer, int pitch, int vel, int64_t length, int delay)
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
    struct Voice
    {
        bool active = false, releasing = false;
        int layer = 0, delay = 0;
        double freq = 440, phase = 0, phase2 = 0, t = 0;
        float amp = 1, env = 0, lp = 0;
        int64_t remaining = 0;
        int64_t age = 0;
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

    float tick(Voice& v)
    {
        constexpr double twoPi = 6.283185307179586;
        ++v.age;
        if (!v.releasing && --v.remaining <= 0) v.releasing = true;
        const double dt = 1.0 / sr;
        v.t += dt;

        // envelope per layer: attack, decay/sustain, release (seconds)
        float attack = 0.005f, release = 0.08f, sustain = 0.75f, decay = 0.2f;
        switch (v.layer)
        {
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
        {
            const float target = sustain;
            v.env += (target - v.env) * (float) (dt / decay) * 3.0f;
        }

        double s = 0;
        switch (v.layer)
        {
            case 0: // lead: two saws, gentle delayed vibrato, low-pass
            {
                const double vib = 1.0 + 0.004 * std::sin(twoPi * 5.5 * v.t) * std::min(1.0, std::max(0.0, (v.t - 0.25) * 3));
                s = 0.6 * saw(v.phase, v.freq * vib) + 0.4 * saw(v.phase2, v.freq * vib * 1.004);
                v.lp += (float) ((s - v.lp) * 0.35);
                s = v.lp * 0.22;
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
    Voice voices[48];
};
