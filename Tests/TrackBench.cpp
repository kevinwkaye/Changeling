// Tracking bench: plays modelled bass and guitar phrases through the engine and scores
// what the synth actually plays, every 10 ms, against what was really played.
//
//   changeling_track_bench [folder]    prints the scores; with a folder, writes in/out WAVs

#include "ChangelingEngine.h"
#include "GuitarModel.h"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace chg;

struct Score
{
    int frames = 0, correct = 0, octave = 0, wrong = 0, silent = 0, ghost = 0, idleFrames = 0;
    double latencySum = 0; int latencyN = 0; float latencyMax = 0; int missedNotes = 0;
    int voiceStarts = 0, truthNotes = 0;
    double recallSum = 0, precisionSum = 0, pcRecall = 0, pcPrecision = 0; int polyFrames = 0;
};

static void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const uint32_t frames = (uint32_t) l.size(), bytes = frames * 4;
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + bytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    u32 (16); u16 (1); u16 (2); u32 ((uint32_t) sr); u32 ((uint32_t) sr * 4); u16 (4); u16 (16);
    std::fwrite ("data", 1, 4, f); u32 (bytes);
    for (uint32_t i = 0; i < frames; ++i)
        for (float s : { l[i], r[i] }) { const int16_t v = (int16_t) std::lround (std::max (-1.0f, std::min (1.0f, s)) * 32767.0f); std::fwrite (&v, 2, 1, f); }
    std::fclose (f);
}

// The engine only listens for its first 0.6 s; give it that before a phrase starts
static void warm (ChangelingEngine& e, const EngineParams& p)
{
    std::vector<float> z (128, 0.0f), z2 (128, 0.0f);
    for (int i = 0; i < 270; ++i) { std::fill (z.begin(), z.end(), 0.0f); std::fill (z2.begin(), z2.end(), 0.0f); e.process (z.data(), z2.data(), 128, p); }
}

static Score runOn (const char* name, const std::vector<bench::Note>& notes, const std::vector<float>& in, bool bass, bool poly,
                    const EngineParams& base, const char* dir)
{
    const float sr = 48000.0f;
    ChangelingEngine e; e.prepare (sr);
    EngineParams p = base;
    warm (e, p);
    p.mode = poly ? ModePoly : ModeMono;
    p.input = bass ? InputBass : InputGuitar;
    p.delayLevel = 0.0f;
    std::vector<float> L = in, R = in;
    Score s;
    s.truthNotes = (int) notes.size();
    std::vector<float> firstHit (notes.size(), -1.0f);
    std::array<bool, 6> wasOn {};
    const int block = 128, frameEvery = (int) (0.01f * sr);
    int nextFrame = 0;
    for (int pos = 0; pos < (int) in.size(); pos += block)
    {
        const int n = std::min (block, (int) in.size() - pos);
        e.process (L.data() + pos, R.data() + pos, n, p);
        const auto vv = e.voiceView();
        for (int k = 0; k < 6; ++k) { const bool on = vv[(size_t) k].on && vv[(size_t) k].amp > 0.05f; if (on && ! wasOn[(size_t) k]) ++s.voiceStarts; wasOn[(size_t) k] = on; }
        if (pos < nextFrame) continue;
        nextFrame += frameEvery;
        const double t = (double) pos / sr;

        // what is really sounding now
        std::vector<std::pair<int, float>> truth;
        bool releasing = false;
        for (size_t k = 0; k < notes.size(); ++k)
        {
            const auto& nt = notes[k];
            if (t >= nt.start && t < nt.end) truth.push_back ({ (int) k, bench::truePitch (nt, t) });
            else if (t >= nt.end && t < nt.end + 0.6) releasing = true;
        }
        // a later note on the same string replaces the earlier one
        if (! poly && truth.size() > 1) { auto last = truth.back(); truth.clear(); truth.push_back (last); }

        // what the synth plays: voices above a third of full level
        std::vector<float> synth;
        float loudest = 0, loudPitch = 0;
        // held notes only: a release tail is the Sustain knob, not tracking
        for (auto& v : vv) if (v.on && v.held && v.amp > 0.3f) { synth.push_back (v.pitch); if (v.amp > loudest) { loudest = v.amp; loudPitch = v.pitch; } }

        for (auto& [k, m] : truth)
        {
            const auto& nt = notes[(size_t) k];
            for (float sp : synth)
                if (std::abs (sp - m) < 0.5f && firstHit[(size_t) k] < 0) firstHit[(size_t) k] = (float) (t - nt.start);
        }

        if (truth.empty())
        {
            if (! releasing) { ++s.idleFrames; if (! synth.empty()) ++s.ghost; }
            continue;
        }
        if (! poly)
        {
            const auto& nt = notes[(size_t) truth[0].first];
            if (t - nt.start < 0.06 && ! nt.hammer) continue;   // grace for the attack
            ++s.frames;
            if (synth.empty()) ++s.silent;
            else
            {
                const float d = loudPitch - truth[0].second;
                if (std::abs (d) < 0.5f) ++s.correct;
                else if (std::abs (std::abs (d) - 12.0f) < 0.7f || std::abs (std::abs (d) - 24.0f) < 0.7f) ++s.octave;
                else ++s.wrong;
            }
        }
        else
        {
            double startT = 1e9; for (auto& [k, m] : truth) startT = std::min (startT, notes[(size_t) k].start);
            bool fresh = false; for (auto& [k, m] : truth) fresh = fresh || t - notes[(size_t) k].start < 0.1;
            if (fresh) continue;
            ++s.polyFrames;
            int hit = 0; for (auto& [k, m] : truth) { bool f = false; for (float sp : synth) f = f || std::abs (sp - m) < 0.5f; hit += f ? 1 : 0; }
            int good = 0; for (float sp : synth) { bool f = false; for (auto& [k, m] : truth) f = f || std::abs (sp - m) < 0.5f; good += f ? 1 : 0; }
            s.recallSum += (double) hit / (double) truth.size();
            // by pitch class: the chord's notes sounding, in any octave, and nothing else
            bool tpc[12] {}, spc[12] {}; int nt = 0, ns = 0, both = 0, extra = 0;
            for (auto& [k, m] : truth) tpc[((int) std::lround (m)) % 12] = true;
            for (float sp : synth) spc[((int) std::lround (sp)) % 12] = true;
            for (int c = 0; c < 12; ++c) { nt += tpc[c]; ns += spc[c]; both += tpc[c] && spc[c]; extra += spc[c] && ! tpc[c]; }
            s.pcRecall += nt ? (double) both / nt : 1.0;
            s.pcPrecision += ns ? (double) (ns - extra) / ns : 0.0;
            s.precisionSum += synth.empty() ? 0.0 : (double) good / (double) synth.size();
        }
    }
    for (size_t k = 0; k < notes.size(); ++k)
    {
        if (notes[k].hammer) continue;
        if (firstHit[k] < 0) { ++s.missedNotes; continue; }
        s.latencySum += firstHit[k]; ++s.latencyN; s.latencyMax = std::max (s.latencyMax, firstHit[k]);
    }
    if (! poly)
        std::printf ("%-14s right %5.1f%%  octave %4.1f%%  wrong %4.1f%%  silent %4.1f%%  | first right pitch avg %3.0f ms, worst %3.0f ms, missed %d/%d | ghost frames %d | voice starts %d for %d notes\n",
                     name, 100.0 * s.correct / std::max (1, s.frames), 100.0 * s.octave / std::max (1, s.frames), 100.0 * s.wrong / std::max (1, s.frames),
                     100.0 * s.silent / std::max (1, s.frames), 1000.0 * s.latencySum / std::max (1, s.latencyN), 1000.0 * s.latencyMax, s.missedNotes, s.truthNotes, s.ghost, s.voiceStarts, s.truthNotes);
    else
        std::printf ("%-14s chord tones %5.1f%%, wrong tones %4.1f%% | exact notes %5.1f%%, voices that are chord notes %5.1f%% | first right pitch avg %3.0f ms, worst %3.0f ms, missed %d/%d | voice starts %d for %d notes\n",
                     name, 100.0 * s.pcRecall / std::max (1, s.polyFrames), 100.0 - 100.0 * s.pcPrecision / std::max (1, s.polyFrames),
                     100.0 * s.recallSum / std::max (1, s.polyFrames), 100.0 * s.precisionSum / std::max (1, s.polyFrames),
                     1000.0 * s.latencySum / std::max (1, s.latencyN), 1000.0 * s.latencyMax, s.missedNotes, s.truthNotes, s.voiceStarts, s.truthNotes);
    if (dir)
    {
        writeWav (std::string (dir) + "/" + name + "-in.wav", in, in, (int) sr);
        writeWav (std::string (dir) + "/" + name + "-out.wav", L, R, (int) sr);
    }
    return s;
}

// Mains hum as a single-coil guitar picks it up: the mains frequency and its harmonics,
// steady, plus hiss. With nothing played, the synth must stay silent.
static std::vector<float> hum (float sr, double seconds, float mainsHz, float levelDb, double startAt, double gapFrom = -1.0, double gapTo = -1.0)
{
    std::vector<float> out ((size_t) (seconds * sr), 0.0f);
    const float a = std::pow (10.0f, levelDb / 20.0f);
    Rng r; r.s = 99u;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double t = (double) i / sr;
        float fade = (float) std::min (1.0, std::max (0.0, (t - startAt) / 0.05));   // a volume knob coming up
        if (gapFrom >= 0.0) fade *= (float) std::min (1.0, std::max (std::max (0.0, (gapFrom - t) / 0.05), std::max (0.0, (t - gapTo) / 0.05)));   // down, then up again
        const float ph = kTwoPi * mainsHz * (float) t;
        out[i] = fade * a * (std::sin (ph) + 0.7f * std::sin (2 * ph + 0.4f) + 0.5f * std::sin (3 * ph + 1.1f) + 0.3f * std::sin (5 * ph) + 0.15f * std::sin (7 * ph))
               + a * 0.05f * r.bi();
    }
    return out;
}

static int humStarts (const std::vector<float>& in, int mode, int input, double countFrom = 0.0)
{
    ChangelingEngine e; e.prepare (48000.0);
    EngineParams p; p.mode = mode; p.input = input; p.mix = 1.0f; p.delayLevel = 0.0f;
    std::vector<float> L = in, R = in;
    int starts = 0; std::array<bool, 6> was {};
    for (int pos = 0; pos + 128 <= (int) in.size(); pos += 128)
    {
        e.process (L.data() + pos, R.data() + pos, 128, p);
        const auto vv = e.voiceView();
        const bool counting = (double) pos / 48000.0 >= countFrom;
        for (int k = 0; k < 6; ++k) { const bool on = vv[(size_t) k].on && vv[(size_t) k].held; if (counting && on) ++starts; was[(size_t) k] = on; }
    }
    return starts;
}

static Score run (const char* name, const std::vector<bench::Note>& notes, bool bass, bool poly, const EngineParams& base,
                  const char* dir, float level)
{
    double len = 0; for (auto& n : notes) len = std::max (len, n.end);
    return runOn (name, notes, bench::render (notes, 48000.0f, len + 1.0, bass, level), bass, poly, base, dir);
}

int main (int argc, char** argv)
{
    const char* dir = argc > 1 ? argv[1] : nullptr;
    if (auto* v = std::getenv ("MINWIN")) MonoTracker::kMinWin = std::atoi (v);
    if (auto* v = std::getenv ("WINFRAC")) MonoTracker::kWinFrac = (float) std::atof (v);
    if (auto* v = std::getenv ("AGREE")) MonoTracker::kAgree = std::atoi (v);
    EngineParams p;
    p.mix = 1.0f; p.sustain = 0.6f; p.glide = 0.0f;
    int failures = 0;
    auto need = [&] (bool ok, const char* what) { if (! ok) { std::printf ("  FAIL %s\n", what); ++failures; } };
    auto pct = [] (int a, int b) { return 100.0 * a / std::max (1, b); };
    for (float level : { 0.25f, 0.03f })
    {
        const auto b = run (level > 0.1f ? "bass" : "bass-quiet", bench::bassLine(), true, false, p, level > 0.1f ? dir : nullptr, level);
        need (pct (b.correct, b.frames) >= 97.0 && b.octave == 0 && b.missedNotes == 0, "bass: right pitch at least 97% of the time, no octave errors, no missed notes");
        need (b.latencyMax <= 0.12f, "bass: every note speaks within 120 ms");
        const auto l = run (level > 0.1f ? "lead" : "lead-quiet", bench::guitarLead(), false, false, p, level > 0.1f ? dir : nullptr, level);
        need (pct (l.correct, l.frames) >= 97.0 && l.octave == 0 && l.missedNotes == 0, "lead: right pitch at least 97% of the time through bends, vibrato and slides");
        need (l.latencyMax <= 0.05f, "lead: every note speaks within 50 ms");
    }
    const auto c = run ("chords", bench::guitarChords(), false, true, p, dir, 0.25f);
    need (100.0 - 100.0 * c.pcPrecision / std::max (1, c.polyFrames) <= 6.0, "chords: wrong tones under 6%");
    need (100.0 * c.pcRecall / std::max (1, c.polyFrames) >= 75.0, "chords: at least three quarters of the chord's tones sound");
    // hum with nothing played: silence, whatever the mains, level, mode or input
    {
        int there = 0, back = 0, late = 0, cases = 0;
        for (float mains : { 50.0f, 60.0f })
            for (float db : { -55.0f, -45.0f, -36.0f })
                for (int mode : { ModeMono, ModePoly, ModeArp })
                    for (int input : { InputGuitar, InputBass })
                    {
                        ++cases;
                        // hum already there when the plugin loads: the everyday case
                        there += humStarts (hum (48000.0f, 7.0, mains, db, 0.0), mode, input);
                        // volume rolled down for 1.5 s and back up
                        { const int n = humStarts (hum (48000.0f, 9.0, mains, db, 0.0, 3.0, 4.5), mode, input); if (n) std::printf ("       back: %.0f Hz %.0f dB mode %d input %d: %d frames\n", mains, db, mode, input, n); back += n; }
                        // hum appearing for the first time: a short blip at most, gone within a second
                        late += humStarts (hum (48000.0f, 7.0, mains, db, 1.5), mode, input, 2.5);
                    }
        std::printf ("hum only       sounding frames: %d with hum from the start, %d after rolling the volume down and up, %d a second after hum first appears (%d cases each)\n", there, back, late, cases);
        need (there == 0, "hum that's there from the start never plays a note");
        need (back == 0, "rolling the volume down and back up never plays a note");
        need (late == 0, "hum appearing for the first time is silent within a second");
    }
    // and real playing over loud hum still tracks
    for (bool bass : { true, false })
    {
        auto notes = bass ? bench::bassLine() : bench::guitarLead();
        double len = 0; for (auto& n : notes) len = std::max (len, n.end);
        auto in = bench::render (notes, 48000.0f, len + 1.0, bass, 0.25f);
        const auto h = hum (48000.0f, len + 1.0, 50.0f, -52.0f, 0.0);   // 40 dB under the loudest pluck: a noisy single coil
        for (size_t i = 0; i < in.size(); ++i) in[i] += h[i];
        const auto r = runOn (bass ? "bass+hum" : "lead+hum", notes, in, bass, false, p, nullptr);
        need (pct (r.correct, r.frames) >= 90.0 && r.octave == 0 && r.missedNotes <= 1, bass ? "bass over hum still tracks" : "lead over hum still tracks");
    }
    std::printf (failures == 0 ? "All passed\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
