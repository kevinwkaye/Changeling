#pragma once

// A plucked-string model for testing the tracker with something close to a real
// instrument: a waveguide string with a pick position, a pickup, frequency-dependent
// damping, bends, vibrato, slides, hammer-ons and palm-muted stops, plus mains hum and
// a noise floor. Each phrase comes with the notes that were really played, so the
// tracker can be scored frame by frame.

#include "DspUtil.h"
#include <vector>

namespace bench
{
using namespace chg;

struct Note
{
    double start = 0, end = 0;       // seconds
    float midi = 40;                 // fretted pitch
    float velocity = 0.8f;           // 0..1
    float bend = 0;                  // semitones reached by bendTime after start (0 = none)
    float bendTime = 0.25f;
    float vibrato = 0;               // semitones depth
    float slideTo = -1;              // slide to this pitch over the note (no new pluck)
    bool hammer = false;             // legato from the previous note on the same string: no pluck
    int string = 0;                  // notes on one string cut each other off
    bool mute = false;               // palm-muted: dark and short
};

struct Truth { double start, end; float midi; float bend, bendTime, vibrato, slideTo; };

class StringModel
{
public:
    void prepare (float sampleRate, bool bass)
    {
        sr = sampleRate; isBass = bass;
        line.assign (8192, 0.0f); w = 0;
        lp = 0; damp = 1.0f; dampTarget = 1.0f; excite.clear(); exPos = 0;
        pickup.assign (4096, 0.0f); pw = 0;
    }

    void pluck (float midi, float velocity, bool mute, uint32_t seed)
    {
        freq = mtof (midi);
        const int L = std::max (8, (int) (sr / freq));
        excite.assign ((size_t) L, 0.0f);
        Rng r; r.s = seed;
        // pick: a short noisy triangle, comb-filtered by the pick position
        const int pickAt = std::max (1, (int) (L * (isBass ? 0.18f : 0.13f)));
        std::vector<float> raw ((size_t) L);
        for (int i = 0; i < L; ++i)
        {
            const float tri = 1.0f - std::abs (2.0f * (float) i / (float) L - 1.0f);
            raw[(size_t) i] = 0.65f * tri + 0.35f * r.bi();
        }
        // a pick puts in similar energy whatever the note, so short strings get a stronger burst
        const float scale = std::sqrt ((sr / 110.0f) / (float) L);
        for (int i = 0; i < L; ++i)
            excite[(size_t) i] = scale * velocity * (raw[(size_t) i] - (i >= pickAt ? raw[(size_t) (i - pickAt)] : 0.0f));
        exPos = 0;
        damp = 1.0f;
        dampTarget = 1.0f;
        brightness = mute ? 0.25f : (isBass ? 0.55f : 0.75f) * (0.6f + 0.4f * velocity);
        loss = mute ? 0.985f : (isBass ? 0.9993f : 0.9986f);
        sounding = true;
    }

    void setPitch (float midi) { freq = mtof (midi); }
    void stop() { dampTarget = 0.0f; }    // fretting hand lets go, or the palm lands

    float process()
    {
        damp += (dampTarget - damp) * 0.004f;
        const float period = sr / freq;
        float rp = (float) w - period + 0.5f;
        while (rp < 0) rp += (float) line.size();
        const int i0 = (int) rp;
        const float fr = rp - (float) i0;
        const float a = line[(size_t) (i0 % (int) line.size())], b = line[(size_t) ((i0 + 1) % (int) line.size())];
        float y = a + (b - a) * fr;
        lp += brightness * (y - lp);
        float fb = lp * loss * (0.2f + 0.8f * damp);
        if (exPos < (int) excite.size()) fb += excite[(size_t) exPos++];
        line[(size_t) w] = fb;
        w = (w + 1) % (int) line.size();

        // magnetic pickup near the bridge: a comb that thins the fundamental
        pickup[(size_t) pw] = fb;
        const int off = std::max (1, (int) (period * (isBass ? 0.16f : 0.11f)));
        const float out = fb - 0.7f * pickup[(size_t) ((pw - off + 4096) % 4096)];
        pw = (pw + 1) % 4096;
        return out;
    }

private:
    float sr = 48000, freq = 110, lp = 0, brightness = 0.6f, loss = 0.999f, damp = 1, dampTarget = 1;
    bool isBass = false, sounding = false;
    std::vector<float> line, excite, pickup;
    int w = 0, exPos = 0, pw = 0;
};

// Renders notes to audio. Six strings, hum and noise floor, peak level set by `level`.
inline std::vector<float> render (const std::vector<Note>& notes, float sr, double seconds, bool bass, float level = 0.25f,
                                  float noiseDb = -72.0f, uint32_t seed = 1)
{
    const int n = (int) (seconds * sr);
    std::vector<float> out ((size_t) n, 0.0f);
    StringModel strings[6];
    for (auto& s : strings) s.prepare (sr, bass);
    std::vector<int> active (6, -1);
    Rng noise; noise.s = seed * 7919u + 3u;
    const float nf = std::pow (10.0f, noiseDb / 20.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        for (size_t k = 0; k < notes.size(); ++k)
        {
            const auto& nt = notes[k];
            const int s = std::max (0, std::min (5, nt.string));
            const long long startAt = std::llround (nt.start * sr), endAt = std::llround (nt.end * sr);
            if (i == startAt)
            {
                if (nt.hammer && active[(size_t) s] >= 0) strings[s].setPitch (nt.midi);
                else strings[s].pluck (nt.midi, nt.velocity, nt.mute, seed * 131u + (uint32_t) k * 17u);
                active[(size_t) s] = (int) k;
            }
            if (active[(size_t) s] == (int) k)
            {
                if (i >= startAt && i < endAt)
                {
                    float m = nt.midi;
                    const float el = (float) (t - nt.start);
                    if (nt.bend != 0) m += nt.bend * std::min (1.0f, el / nt.bendTime);
                    if (nt.slideTo >= 0)
                    {
                        const float dur = (float) (nt.end - nt.start);
                        const float u = std::min (1.0f, std::max (0.0f, (el - 0.15f * dur) / (0.5f * dur)));
                        m += (nt.slideTo - nt.midi) * u;
                    }
                    if (nt.vibrato > 0) m += nt.vibrato * std::sin (kTwoPi * 5.5f * std::max (0.0f, el - 0.3f)) * std::min (1.0f, std::max (0.0f, el - 0.3f) * 3.0f);
                    strings[s].setPitch (m);
                }
                else if (i == endAt)
                {
                    bool legatoNext = false;   // a hammer-on or slide carries straight on
                    for (const auto& o : notes) legatoNext = legatoNext || (o.string == nt.string && o.hammer && std::abs (o.start - nt.end) < 0.002);
                    if (! legatoNext) strings[s].stop();
                }
            }
        }
        float v = 0.0f;
        for (auto& s : strings) v += s.process();
        const float hum = 0.4f * nf * (std::sin (kTwoPi * 50.0f * (float) t) + 0.5f * std::sin (kTwoPi * 150.0f * (float) t));
        out[(size_t) i] = v + nf * noise.bi() + hum;
    }
    float peak = 1.0e-9f;
    for (float v : out) peak = std::max (peak, std::abs (v));
    for (auto& v : out) v *= level / peak;
    return out;
}

// What was really sounding at time t on a given note (the pitch the synth should play)
inline float truePitch (const Note& nt, double t)
{
    float m = nt.midi;
    const float el = (float) (t - nt.start);
    if (nt.bend != 0) m += nt.bend * std::min (1.0f, el / nt.bendTime);
    if (nt.slideTo >= 0)
    {
        const float dur = (float) (nt.end - nt.start);
        const float u = std::min (1.0f, std::max (0.0f, (el - 0.15f * dur) / (0.5f * dur)));
        m += (nt.slideTo - nt.midi) * u;
    }
    if (nt.vibrato > 0) m += nt.vibrato * std::sin (kTwoPi * 5.5f * std::max (0.0f, el - 0.3f)) * std::min (1.0f, std::max (0.0f, el - 0.3f) * 3.0f);
    return m;
}

// ---- phrases ----
inline std::vector<Note> bassLine()
{
    // a 5-string line from low B up, with legato, staccato, a slide, vibrato, a hammer-on and palm mutes
    std::vector<Note> v;
    double t = 0.2;
    auto add = [&] (float m, double len, double gap, float vel = 0.8f) { Note n; n.start = t; n.end = t + len; n.midi = m; n.velocity = vel; v.push_back (n); t += len + gap; return &v.back(); };
    add (23, 0.9, 0.05);                // low B
    add (23, 0.4, 0.05, 0.6);           // repeated low B, softer
    add (28, 0.6, 0.02);                // E
    add (31, 0.3, 0.02);                // G
    add (33, 1.2, 0.05)->vibrato = 0.25f;   // A with vibrato
    { auto* n = add (28, 1.0, 0.05); n->slideTo = 31; }   // slide E to G
    add (35, 0.25, 0.1, 0.9);           // B staccato
    add (35, 0.25, 0.1, 0.5);
    add (38, 0.5, 0.0);                 // D, then hammer to E
    { Note h; h.start = t; h.end = t + 0.5; h.midi = 40; h.hammer = true; v.push_back (h); t += 0.55; }
    for (int k = 0; k < 4; ++k) { auto* n = add (28, 0.18, 0.08, 0.7f); n->mute = true; }   // palm-muted eighths
    add (26, 1.6, 0.3, 0.9);            // low D, let ring to decay
    add (24, 1.4, 0.2, 0.4);            // quiet low C
    return v;
}

inline std::vector<Note> guitarLead()
{
    std::vector<Note> v;
    double t = 0.2;
    auto add = [&] (float m, double len, double gap, float vel = 0.8f) { Note n; n.start = t; n.end = t + len; n.midi = m; n.velocity = vel; n.string = 0; v.push_back (n); t += len + gap; return &v.back(); };
    add (64, 0.5, 0.02);
    add (67, 0.5, 0.02);
    { auto* n = add (69, 1.2, 0.05); n->bend = 2.0f; n->bendTime = 0.3f; }   // whole-step bend
    { auto* n = add (71, 1.4, 0.05); n->vibrato = 0.35f; }                  // wide vibrato
    add (74, 0.15, 0.05, 0.9); add (72, 0.15, 0.05, 0.9); add (71, 0.15, 0.05, 0.9); add (69, 0.15, 0.05, 0.9);
    { auto* n = add (67, 1.0, 0.05); n->slideTo = 72; }
    add (76, 0.5, 0.0);
    { Note h; h.start = t; h.end = t + 0.5; h.midi = 79; h.hammer = true; h.string = 0; v.push_back (h); t += 0.6; }
    add (52, 0.8, 0.05);                 // low E string, then low notes
    add (45, 0.8, 0.05);
    add (40, 1.5, 0.2, 0.9);
    return v;
}

inline std::vector<Note> guitarChords()
{
    std::vector<Note> v;
    auto chord = [&] (std::initializer_list<int> ms, double t, double len, double strum)
    {
        int s = 0;
        for (int m : ms) { Note n; n.start = t + strum * s; n.end = t + len; n.midi = (float) m; n.velocity = 0.7f; n.string = s; v.push_back (n); ++s; }
    };
    chord ({ 40, 47, 52, 56, 59, 64 }, 0.2, 1.8, 0.012);   // E major
    chord ({ 45, 52, 57, 61, 64 }, 2.2, 1.8, 0.015);       // A major
    chord ({ 50, 57, 62, 66 }, 4.2, 1.6, 0.02);            // D major
    chord ({ 43, 47, 50, 55, 59, 67 }, 6.0, 2.0, 0.012);   // G major
    chord ({ 52, 59, 64 }, 8.2, 1.5, 0.08);                // slow arpeggiated E5 power chord
    return v;
}
} // namespace bench
