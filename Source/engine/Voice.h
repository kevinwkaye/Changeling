#pragma once

#include "DspUtil.h"

namespace chg
{
// Ladder (4-pole, tanh feedback) and state-variable (TPT) filters with the Enzo's six types:
// ladder low-pass, band-pass (shelving), high-pass, and state-variable low, band and high.
struct Filter
{
    float s[4] {}, ic1 = 0, ic2 = 0;

    void reset() { for (auto& v : s) v = 0.0f; ic1 = ic2 = 0.0f; }

    // type 0..5, cutoff in Hz, res 0..1 (the Bandwidth knob: wide and gentle to narrow and peaky)
    float process (float x, int type, float fc, float res, float sr)
    {
        fc = std::min (fc, sr * 0.42f);
        fc = std::max (fc, 16.0f);
        if (type < 3)
        {
            const float g = 1.0f - std::exp (-kTwoPi * fc / sr);
            const float k = res * 3.9f;
            const float in = softClip (x - k * (s[3] - 0.5f * x * res));
            s[0] += g * (in - s[0]);
            s[1] += g * (s[0] - s[1]);
            s[2] += g * (s[1] - s[2]);
            s[3] += g * (softClip (s[2]) - s[3]);
            const float comp = 1.0f + res * 0.7f; // keep the level up as resonance rises
            if (type == 0) return s[3] * comp;
            if (type == 1) return (s[1] - s[3]) * 1.8f * comp;                       // shelving band-pass
            return (in - 4.0f * s[0] + 6.0f * s[1] - 4.0f * s[2] + s[3]) * 0.6f * comp; // high-pass
        }
        const float g = std::tan (kPi * fc / sr);
        const float k = 2.0f - 1.94f * res;
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        if (type == 3) return v2;
        if (type == 4) return v1 * k;
        return x - k * v1 - v2;
    }
};

// One synth voice: two oscillators, the filter, envelopes and its own ring modulator.
struct Voice
{
    bool active = false;     // sounding at all (including the release)
    bool gate = false;       // note held
    bool latched = false;    // kept by Sustain (footswitch D)
    bool sustained = false;  // note let go while the sustain pedal is held
    bool fromMidi = false;
    bool arp = false;
    int key = -1;            // the note that owns this voice (integer, for note-offs)
    long long age = 0;
    float target = 60.0f, current = 60.0f, glideCoef = 0.0f;
    float velocity = 1.0f;
    float amp = 0.0f, fenv = 0.0f;
    bool fenvAttack = false;
    float ph1 = 0.0f, ph2 = 0.0f, ringPh = 0.0f, ringHz = 200.0f;
    double offAt = 0.0;      // arp gate end, in samples
    Filter filter;

    void start (int k, float midi, float vel, bool midiSource, long long when, bool legato)
    {
        if (! active) { current = midi; filter.reset(); amp = 0.0f; ph1 = 0.0f; ph2 = 0.37f; }
        key = k; target = midi; velocity = vel; fromMidi = midiSource;
        active = true; gate = true; sustained = false; age = when;
        if (! legato) { fenvAttack = true; }
    }
    void release() { gate = false; }
};
} // namespace chg
