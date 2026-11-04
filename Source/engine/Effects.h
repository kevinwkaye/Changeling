#pragma once

#include "DspUtil.h"

namespace chg
{
// The Enzo's single delay: one tap at the tapped tempo, with feedback. Holding footswitch B
// pushes the feedback past unity (the runaway), with a soft clip and a darkening filter in
// the loop so it grows without blowing up. Modulation wobbles the time in Dry mode.
struct EchoDelay
{
    DelayLine l, r;
    Biquad toneL, toneR;
    float sr = 48000.0f, lfo = 0.0f, timeSm = 0.0f, fbSm = 0.0f;

    void prepare (float sampleRate)
    {
        sr = sampleRate;
        l.resize ((int) (sr * 2.6f));
        r.resize ((int) (sr * 2.6f));
        toneL.lowpass (4200.0f, 0.6f, sr); toneR.lowpass (4200.0f, 0.6f, sr);
        toneL.reset(); toneR.reset();
        timeSm = 0.5f * sr; fbSm = 0.0f;
    }
    void clear() { l.clear(); r.clear(); toneL.reset(); toneR.reset(); }

    // returns the wet echo for one stereo sample
    void process (float inL, float inR, float seconds, float feedback, float modDepth, bool runaway, float& outL, float& outR)
    {
        const float wantT = std::min (2.5f, std::max (0.02f, seconds)) * sr;
        timeSm += (wantT - timeSm) * 0.0004f;                       // tape-like glide when the tempo moves
        const float fb = runaway ? 1.06f : feedback * 0.92f;
        fbSm += (fb - fbSm) * (runaway ? 0.0015f : 0.002f);
        lfo += 0.9f / sr; if (lfo >= 1.0f) lfo -= 1.0f;
        const float wob = modDepth * 0.004f * sr * std::sin (kTwoPi * lfo);
        const float dl = l.read (timeSm + wob);
        const float dr = r.read (timeSm * 1.012f - wob);              // a hair wider on the right
        outL = dl; outR = dr;
        l.push (softClip (inL + toneL.process (dl) * fbSm));
        r.push (softClip (inR + toneR.process (dr) * fbSm));
    }
};

// Two-grain pitch shifter for Dry mode: the Pitch knob shifts your own signal.
struct Shifter
{
    DelayLine d;
    float sr = 48000.0f, ph = 0.0f, window = 0.04f;
    void prepare (float sampleRate) { sr = sampleRate; d.resize ((int) (sr * 0.2f)); ph = 0.0f; }
    float process (float x, float semitones)
    {
        d.push (x);
        if (std::abs (semitones) < 0.01f) return d.read (1.0f);
        const float ratio = std::pow (2.0f, semitones / 12.0f);
        const float wlen = window * sr;
        ph += (1.0f - ratio) / wlen;
        while (ph >= 1.0f) ph -= 1.0f;
        while (ph < 0.0f) ph += 1.0f;
        const float p2 = ph + 0.5f >= 1.0f ? ph - 0.5f : ph + 0.5f;
        const float g1 = std::sin (kPi * ph), g2 = std::sin (kPi * p2);
        return d.read (2.0f + ph * wlen) * g1 + d.read (2.0f + p2 * wlen) * g2;
    }
};

// Sustain in Dry mode compresses your signal.
struct Compressor
{
    float env = 0.0f, a = 0.0f, r = 0.0f;
    void prepare (float sr) { a = std::exp (-1.0f / (0.003f * sr)); r = std::exp (-1.0f / (0.15f * sr)); env = 0.0f; }
    float process (float x, float amount)
    {
        const float ax = std::abs (x);
        env = ax > env ? ax + a * (env - ax) : ax + r * (env - ax);
        if (amount < 0.01f) return x;
        const float thresh = 0.25f - 0.23f * amount;
        const float ratio = 1.0f + 9.0f * amount;
        float gain = 1.0f;
        if (env > thresh) gain = std::pow (env / thresh, 1.0f / ratio - 1.0f);
        return x * gain * (1.0f + 1.2f * amount);
    }
};
} // namespace chg
