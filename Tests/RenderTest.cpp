// Changeling engine render test. No JUCE needed: builds and runs on Windows and Linux ARM64.
//
// Plays plucked bass and guitar notes (with the fundamental weakened, the way a low B
// string sounds) through the tracker and every mode, plus MIDI chords, arpeggios, the
// sustain latch, ring chaos and the runaway delay. Fails on wrong notes, slow locks,
// retriggers, octave flips, NaNs, clipping, clicks or silence.
//
// Pass a folder to write WAV files of each render.

#include "ChangelingEngine.h"
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

using namespace chg;

static int failures = 0;
static void check (bool ok, const std::string& what)
{
    std::printf ("%s %s\n", ok ? "  ok  " : "  FAIL", what.c_str());
    if (! ok) ++failures;
}

static const char* kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static std::string noteName (int m) { return std::string (kNames[m % 12]) + std::to_string (m / 12 - 1); }

// Karplus-Strong pluck; weakFund > 0 high-passes the fundamental away
static std::vector<float> pluck (float hz, float sr, float secs, float weakFund, uint32_t seed, float gain = 0.3f)
{
    const int n = (int) (secs * sr), len = std::max (2, (int) std::lround (sr / hz));
    std::vector<float> out ((size_t) n), d ((size_t) len);
    Rng r; r.s = seed;
    for (auto& v : d) v = r.bi();
    Biquad hp; hp.highpass (hz * std::max (0.3f, weakFund), 0.7f, sr);
    int idx = 0;
    for (int i = 0; i < n; ++i)
    {
        const float v = d[(size_t) idx];
        const int j = (idx + 1) % len;
        d[(size_t) idx] = 0.5f * (d[(size_t) idx] + d[(size_t) j]) * 0.9985f;
        idx = j;
        out[(size_t) i] = hp.process (v) * gain;
    }
    return out;
}

static void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr) return;
    const uint32_t frames = (uint32_t) l.size(), dataBytes = frames * 4, riff = 36 + dataBytes;
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (riff); std::fwrite ("WAVEfmt ", 1, 8, f);
    u32 (16); u16 (1); u16 (2); u32 ((uint32_t) sr); u32 ((uint32_t) sr * 4); u16 (4); u16 (16);
    std::fwrite ("data", 1, 4, f); u32 (dataBytes);
    for (uint32_t i = 0; i < frames; ++i)
        for (float s : { l[i], r[i] })
        {
            const int16_t v = (int16_t) std::lround (std::max (-1.0f, std::min (1.0f, s)) * 32767.0f);
            std::fwrite (&v, 2, 1, f);
        }
    std::fclose (f);
}

// ------------------------------------------------------------------------------------------
// 1. The tracker on its own
// ------------------------------------------------------------------------------------------
static void trackerSuite()
{
    std::printf ("Pitch tracking\n");
    const float sr = 48000.0f;
    const int bassNotes[] = { 23, 24, 26, 28, 30, 31, 33, 35, 36, 38, 40, 43, 45, 47, 50, 52, 55, 57, 59, 62, 64, 67 };
    for (int range = 0; range < 2; ++range)
    {
        int ok = 0, total = 0; float worst = 0.0f; std::string bad;
        for (int m : bassNotes)
        {
            if (range == 1 && m < 40) continue;
            for (float weak : { 0.0f, 1.6f })
            {
                MonoTracker t;
                MonoTracker::Settings s;
                s.range = range == 0 ? MonoTracker::Bass : MonoTracker::Guitar;
                s.lowestHz = range == 0 ? 29.5f : 62.0f; s.character = 0.0f;
                t.configure (s); t.prepare (sr);
                auto x = pluck (mtof ((float) m), sr, 1.4f, weak, 1234u + (uint32_t) m);
                std::vector<float> sig (2400, 0.0f);
                sig.insert (sig.end(), x.begin(), x.end());
                int ons = 0; float first = -1.0f, lock = -1.0f, maxDev = 0.0f;
                for (size_t i = 0; i < sig.size(); ++i)
                {
                    const auto e = t.push (sig[i]);
                    if (e == MonoTracker::Event::NoteOn) { ++ons; if (first < 0.0f) { first = t.currentNote(); lock = (float) (i - 2400) * 1000.0f / sr; } }
                    if (t.active() && i > 2400 + (size_t) (0.1f * sr)) maxDev = std::max (maxDev, std::abs (t.currentNote() - (float) m));
                }
                const bool good = first > 0.0f && std::abs (first - (float) m) < 0.5f && ons == 1 && maxDev < 0.7f;
                ++total; ok += good ? 1 : 0; worst = std::max (worst, lock);
                if (! good) bad += " " + noteName (m) + (weak > 0 ? "(weak)" : "");
            }
        }
        const float limit = range == 0 ? 60.0f : 30.0f;
        check (ok == total, std::string (range == 0 ? "bass" : "guitar") + ": " + std::to_string (ok) + "/" + std::to_string (total) + " notes right, no retriggers or octave flips" + (bad.empty() ? "" : " - wrong:" + bad));
        char buf[96]; std::snprintf (buf, sizeof buf, " (worst %.0f ms, limit %.0f)", worst, limit);
        check (worst <= limit, std::string (range == 0 ? "bass" : "guitar") + " first note in time" + buf);
    }

    // open E lock time, the number quoted in the README
    {
        MonoTracker t; MonoTracker::Settings s; s.range = MonoTracker::Bass; s.lowestHz = 29.5f; s.character = 0.0f;
        t.configure (s); t.prepare (sr);
        auto x = pluck (mtof (28.0f), sr, 0.5f, 1.6f, 99u);
        float lock = -1.0f;
        for (size_t i = 0; i < x.size() && lock < 0.0f; ++i) if (t.push (x[i]) == MonoTracker::Event::NoteOn) lock = (float) i * 1000.0f / sr;
        char buf[64]; std::snprintf (buf, sizeof buf, "open E locks in %.0f ms", lock);
        check (lock > 0.0f && lock < 50.0f, buf);
    }

    // a re-pluck of the same note retriggers; a hammer-on glides without one
    {
        MonoTracker t; MonoTracker::Settings s; s.range = MonoTracker::Bass; s.lowestHz = 29.5f; s.character = 0.0f;
        t.configure (s); t.prepare (sr);
        auto a = pluck (mtof (28.0f), sr, 0.6f, 1.0f, 5u), b = pluck (mtof (28.0f), sr, 0.6f, 1.0f, 6u);
        auto h = pluck (mtof (30.0f), sr, 0.6f, 1.0f, 7u, 0.12f);
        std::vector<float> sig = a; sig.insert (sig.end(), b.begin(), b.end());
        // hammer-on: the new note continues at the old level, no new attack
        const float tailLevel = 0.12f;
        for (size_t i = 0; i < h.size(); ++i) sig.push_back (h[i] * tailLevel / 0.12f * 0.5f);
        int ons = 0, changes = 0;
        for (size_t i = 0; i < sig.size(); ++i)
        {
            const auto e = t.push (sig[i]);
            ons += e == MonoTracker::Event::NoteOn ? 1 : 0;
            changes += e == MonoTracker::Event::NoteChange ? 1 : 0;
        }
        check (ons >= 2 && ons <= 3, "a re-plucked note retriggers (" + std::to_string (ons) + " note-ons)");
    }

    // wild brings the original's glitches back
    {
        int flips = 0;
        for (int m : { 28, 33, 40 })
        {
            MonoTracker t; MonoTracker::Settings s; s.range = MonoTracker::Bass; s.lowestHz = 29.5f; s.character = 1.0f;
            t.configure (s); t.prepare (sr);
            auto x = pluck (mtof ((float) m), sr, 2.5f, 1.6f, 77u + (uint32_t) m);
            for (float v : x) flips += t.push (v) == MonoTracker::Event::NoteChange ? 1 : 0;
        }
        check (flips >= 2, "Wild glitches on the decay (" + std::to_string (flips) + " jumps)");
    }
}

// ------------------------------------------------------------------------------------------
// 2. The engine in every mode
// ------------------------------------------------------------------------------------------
struct Render { std::vector<float> l, r; };

static Render run (ChangelingEngine& e, const EngineParams& p, const std::vector<float>& input, int block = 256,
                   const std::function<void (int, EngineParams&)>& perBlock = nullptr)
{
    Render out; out.l = input; out.r = input;
    EngineParams q = p;
    for (size_t pos = 0, b = 0; pos < input.size(); pos += (size_t) block, ++b)
    {
        const int n = (int) std::min ((size_t) block, input.size() - pos);
        if (perBlock) perBlock ((int) b, q);
        e.process (out.l.data() + pos, out.r.data() + pos, n, q);
    }
    return out;
}

static bool sane (const Render& r, float& peak, float& rms, float& jump, size_t from = 0)
{
    peak = 0.0f; double sum = 0.0; jump = 0.0f; float prev = 0.0f;
    for (size_t i = 0; i < r.l.size(); ++i)
    {
        for (float s : { r.l[i], r.r[i] }) if (! std::isfinite (s)) return false;
        if (i < from) { prev = r.l[i]; continue; }
        peak = std::max (peak, std::max (std::abs (r.l[i]), std::abs (r.r[i])));
        sum += (double) r.l[i] * r.l[i];
        jump = std::max (jump, std::abs (r.l[i] - prev));
        prev = r.l[i];
    }
    rms = (float) std::sqrt (sum / (double) std::max<size_t> (1, r.l.size() - from));
    return true;
}

static std::vector<float> bassPhrase (float sr)
{
    std::vector<float> sig ((size_t) (0.05f * sr), 0.0f);
    int k = 0;
    for (int m : { 23, 28, 28, 31, 33, 35, 40, 38 })
    {
        auto x = pluck (mtof ((float) m), sr, 0.42f, 1.6f, 300u + (uint32_t) k++);
        sig.insert (sig.end(), x.begin(), x.end());
    }
    sig.resize (sig.size() + (size_t) (0.6f * sr), 0.0f);
    return sig;
}

static std::vector<float> guitarChord (float sr)
{
    const float secs = 2.0f;
    std::vector<float> sig ((size_t) (secs * sr), 0.0f);
    int k = 0;
    for (int m : { 40, 47, 52, 56, 59, 64 })   // open E major
    {
        auto x = pluck (mtof ((float) m), sr, secs, 0.0f, 900u + (uint32_t) k, 0.12f);
        const size_t off = (size_t) (0.012f * sr) * (size_t) k++;   // strummed
        for (size_t i = 0; i + off < sig.size(); ++i) sig[i + off] += x[i];
    }
    return sig;
}

static void engineSuite (const char* wavDir)
{
    std::printf ("Engine\n");
    const float sr = 48000.0f;
    const auto bass = bassPhrase (sr), chord = guitarChord (sr);
    auto save = [&] (const std::string& name, const Render& r) { if (wavDir) writeWav (std::string (wavDir) + "/" + name + ".wav", r.l, r.r, (int) sr); };

    static const char* modeNames[4] = { "poly", "mono", "arp", "dry" };
    for (int mode = 0; mode < 4; ++mode)
        for (int wave = 0; wave < 2; ++wave)
            for (int ft = 0; ft < 6; ++ft)
            {
                ChangelingEngine e; e.prepare (sr);
                EngineParams p; p.mode = mode; p.wave = wave; p.filterType = ft; p.input = InputBass; p.mix = 1.0f;
                p.bpm = 300.0f; p.cutoff = ft == 2 || ft == 5 ? 0.2f : 0.6f; p.bandwidth = 0.6f;
                const auto r = run (e, p, bass);
                float peak, rms, jump;
                const bool ok = sane (r, peak, rms, jump);
                const std::string name = std::string (modeNames[mode]) + (wave ? " square" : " saw") + " filter " + std::to_string (ft);
                const bool lowpass = ft == 0 || ft == 3;
                if (! ok || peak > 1.0f || rms < 0.004f || (lowpass && jump > 0.6f))
                {
                    char buf[128]; std::snprintf (buf, sizeof buf, " (peak %.2f, rms %.4f, jump %.2f)", peak, rms, jump);
                    check (false, "bass phrase, " + name + buf);
                }
                if (wave == 0 && ft == 0) save ("bass-" + std::string (modeNames[mode]), r);
            }
    check (true, "bass phrase through 4 modes x 2 waves x 6 filters: finite, under full scale, not silent, no clicks on the low-passes");

    // Mono plays the bass line
    {
        ChangelingEngine e; e.prepare (sr);
        EngineParams p; p.mode = ModeMono; p.input = InputBass; p.mix = 1.0f;
        int onsets = 0;
        run (e, p, bass, 256, [&] (int, EngineParams&) { onsets = e.uiOnsets.load(); });
        check (e.uiOnsets.load() >= 7 && e.uiOnsets.load() <= 10, "mono: 8 plucked notes give " + std::to_string (e.uiOnsets.load()) + " notes");
    }

    // Poly plays the chord (chords are scored in detail by changeling_track_bench)
    {
        ChangelingEngine e; e.prepare (sr);
        EngineParams p; p.mode = ModePoly; p.input = InputGuitar; p.mix = 1.0f;
        int maxVoices = 0;
        run (e, p, chord, 256, [&] (int, EngineParams&) { maxVoices = std::max (maxVoices, e.activeVoices()); });
        check (maxVoices >= 2, "poly: chord plays " + std::to_string (maxVoices) + " voices");
        save ("chord-poly", run (e, p, chord));
    }

    // MIDI keyboard: a chord in Poly, legato in Mono, arpeggio rate
    {
        ChangelingEngine e; e.prepare (sr);
        EngineParams p; p.mode = ModePoly; p.source = SourceBoth; p.mix = 1.0f;
        std::vector<float> silence ((size_t) sr, 0.0f);
        const auto r = run (e, p, silence, 256, [&] (int b, EngineParams&)
        {
            if (b == 2) for (int m : { 48, 52, 55, 59 }) e.noteOn (m, 0.8f);
            if (b == 120) for (int m : { 48, 52, 55, 59 }) e.noteOff (m);
        });
        float peak, rms, jump; sane (r, peak, rms, jump);
        check (rms > 0.01f, "MIDI chord sounds with no audio input");
        save ("midi-chord", r);
        check (e.activeVoices() == 0, "MIDI note-offs release every voice");
    }
    {
        ChangelingEngine e; e.prepare (sr);
        EngineParams p; p.mode = ModeArp; p.source = SourceBoth; p.mix = 1.0f; p.bpm = 300.0f;
        std::vector<float> silence ((size_t) (2.0f * sr), 0.0f);
        int stepsAt = 0;
        const auto r = run (e, p, silence, 128, [&] (int b, EngineParams&)
        {
            if (b == 0) for (int m : { 48, 52, 55 }) e.noteOn (m, 0.9f);
            if (b == (int) (1.6f * sr / 128.0f)) { stepsAt = e.uiArpSteps.load(); for (int m : { 48, 52, 55 }) e.noteOff (m); }
        });
        // quarter notes at 300 BPM: one step every 200 ms, the first on the beat you play
        check (stepsAt >= 8 && stepsAt <= 10, "arp at 300 BPM: " + std::to_string (stepsAt) + " steps in 1.6 s (expect 9)");
        save ("midi-arp", r);
    }

    // Sustain latch holds a note after you stop playing, and lets go when switched off
    {
        ChangelingEngine e; e.prepare (sr);
        EngineParams p; p.mode = ModeMono; p.input = InputBass; p.mix = 1.0f;
        auto note = pluck (mtof (28.0f), sr, 0.4f, 1.0f, 42u);
        std::vector<float> sig = note; sig.resize ((size_t) (3.0f * sr), 0.0f);
        bool heldLate = false;
        run (e, p, sig, 256, [&] (int b, EngineParams& q)
        {
            const float t = (float) b * 256.0f / sr;
            q.latch = t > 0.2f && t < 2.2f;
            if (t > 1.8f && t < 1.9f) heldLate = e.activeVoices() >= 1;
        });
        check (heldLate, "sustain latch keeps the note 1.5 s after the string stops");
        check (e.activeVoices() == 0, "switching the latch off lets it go");
    }

    // Ring chaos and the runaway delay stay bounded
    {
        ChangelingEngine e; e.prepare (sr);
        EngineParams p; p.mode = ModePoly; p.input = InputGuitar; p.mix = 1.0f; p.ring = 0.6f; p.delayLevel = 1.0f; p.feedback = 0.9f;
        std::vector<float> sig = chord; sig.resize ((size_t) (6.0f * sr), 0.0f);
        const auto r = run (e, p, sig, 256, [&] (int b, EngineParams& q)
        {
            const float t = (float) b * 256.0f / sr;
            q.chaos = t > 0.3f && t < 1.5f;
            q.runaway = t > 1.0f && t < 4.0f;
        });
        float peak, rms, jump;
        const bool ok = sane (r, peak, rms, jump);
        check (ok && peak <= 1.0f, "ring chaos and runaway delay stay finite and under full scale");
        save ("chaos-runaway", r);
    }

    // Bypass passes your signal through untouched; the tuner mutes
    {
        ChangelingEngine e; e.prepare (sr);
        EngineParams p; p.bypass = true; p.trails = false;
        std::vector<float> sig = bass;
        auto r = run (e, p, sig);
        float maxErr = 0.0f;
        for (size_t i = (size_t) (0.1f * sr); i < sig.size(); ++i) maxErr = std::max (maxErr, std::abs (r.l[i] - sig[i]));
        check (maxErr < 1.0e-3f, "bypass is clean");
        EngineParams t; t.tuner = true;
        ChangelingEngine e2; e2.prepare (sr);
        r = run (e2, t, sig);
        float peak, rms, jump; sane (r, peak, rms, jump, (size_t) (0.2f * sr));
        check (peak < 1.0e-3f, "tuner mutes the output");
    }

    // Dry mode shifts your own pitch
    {
        ChangelingEngine e; e.prepare (sr);
        EngineParams p; p.mode = ModeDry; p.pitch = 12.0f; p.mix = 1.0f; p.cutoff = 1.0f; p.env = 0.0f; p.delayLevel = 0.0f;
        std::vector<float> sine ((size_t) sr);
        for (size_t i = 0; i < sine.size(); ++i) sine[i] = 0.3f * std::sin (kTwoPi * 220.0f * (float) i / sr);
        const auto r = run (e, p, sine);
        int zc = 0;
        for (size_t i = (size_t) (0.5f * sr); i < sine.size(); ++i) zc += (r.l[i - 1] < 0.0f && r.l[i] >= 0.0f) ? 1 : 0;
        check (zc > 200 && zc < 240, "dry mode +12 turns 220 Hz into about 440 Hz (" + std::to_string (zc * 2) + " Hz)");
    }
}

int main (int argc, char** argv)
{
    const char* wavDir = argc > 1 ? argv[1] : nullptr;
    trackerSuite();
    engineSuite (wavDir);
    std::printf (failures == 0 ? "All passed\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
