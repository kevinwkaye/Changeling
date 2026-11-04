#pragma once

// Small DSP building blocks shared by the Changeling engine. Plain C++17, no JUCE,
// so the engine and its render test build on any platform (Windows, Linux ARM64).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace chg
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float clamp01 (float v) { return std::min (1.0f, std::max (0.0f, v)); }
inline float mtof (float midi) { return 440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f); }
inline float ftom (float hz) { return 69.0f + 12.0f * std::log2 (std::max (1.0e-3f, hz) / 440.0f); }

// tanh-like soft clip, cheap and smooth
inline float softClip (float x)
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// one-pole smoother with a time constant in seconds
struct Smooth
{
    float value = 0.0f, coeff = 0.0f;
    void setTime (float seconds, float sampleRate) { coeff = std::exp (-1.0f / std::max (1.0e-5f, seconds * sampleRate)); }
    float process (float target) { value = target + coeff * (value - target); return value; }
    void reset (float v) { value = v; }
};

// RBJ biquad, transposed direct form II
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0.0f; }
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void set (float nb0, float nb1, float nb2, float na0, float na1, float na2)
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
    void lowpass (float f, float q, float sr)
    {
        const float w = kTwoPi * std::min (f, sr * 0.49f) / sr, c = std::cos (w), a = std::sin (w) / (2.0f * q);
        set ((1 - c) * 0.5f, 1 - c, (1 - c) * 0.5f, 1 + a, -2 * c, 1 - a);
    }
    void highpass (float f, float q, float sr)
    {
        const float w = kTwoPi * std::min (f, sr * 0.49f) / sr, c = std::cos (w), a = std::sin (w) / (2.0f * q);
        set ((1 + c) * 0.5f, -(1 + c), (1 + c) * 0.5f, 1 + a, -2 * c, 1 - a);
    }
    void bandpass (float f, float q, float sr) // 0 dB peak
    {
        const float w = kTwoPi * std::min (f, sr * 0.49f) / sr, c = std::cos (w), a = std::sin (w) / (2.0f * q);
        set (a, 0, -a, 1 + a, -2 * c, 1 - a);
    }
};

// PolyBLEP correction for band-limited saw and square
inline float polyBlep (float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

// Waveforms: 0 saw, 1 square, 2 pulse (25%), 3 sine
inline float oscSample (int wave, float phase, float dt)
{
    switch (wave)
    {
        case 0: return 2.0f * phase - 1.0f - polyBlep (phase, dt);
        case 1:
        case 2:
        {
            const float width = wave == 1 ? 0.5f : 0.25f;
            float v = phase < width ? 1.0f : -1.0f;
            v += polyBlep (phase, dt);
            float t2 = phase - width; if (t2 < 0.0f) t2 += 1.0f;
            v -= polyBlep (t2, dt);
            return wave == 1 ? v : v + (2.0f * width - 1.0f) * -1.0f; // keep the pulse centred
        }
        default: return std::sin (kTwoPi * phase);
    }
}

// Small deterministic random generator (audio-thread safe)
struct Rng
{
    uint32_t s = 0x9e3779b9u;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); } // 0..1
    float bi() { return uni() * 2.0f - 1.0f; }
};

// Mono delay line with fractional read
struct DelayLine
{
    std::vector<float> buf;
    int w = 0;
    void resize (int n) { buf.assign ((size_t) std::max (4, n), 0.0f); w = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    void push (float x) { buf[(size_t) w] = x; if (++w >= (int) buf.size()) w = 0; }
    float read (float delaySamples) const
    {
        const int n = (int) buf.size();
        delaySamples = std::min ((float) (n - 3), std::max (1.0f, delaySamples));
        float rp = (float) w - delaySamples;
        while (rp < 0.0f) rp += (float) n;
        const int i0 = (int) rp;
        const float fr = rp - (float) i0;
        const float a = buf[(size_t) i0], b = buf[(size_t) ((i0 + 1) % n)];
        return a + (b - a) * fr;
    }
};
} // namespace chg
