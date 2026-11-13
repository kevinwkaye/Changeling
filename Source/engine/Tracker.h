#pragma once

#include "DspUtil.h"
#include <array>

namespace chg
{
// Monophonic pitch tracker built for the low end.
//
// The input is low-passed and decimated to about 8 kHz. Every 2 ms a normalised square
// difference (McLeod) is computed over a short window: each lag tau compares the newest
// tau/3 samples with the ones one period earlier, so a note can be called after about
// 1.33 cycles, about 30 ms on open E. One cycle is the physical floor for any tracker.
//
// Octave guard: the second harmonic of a bass string is often louder than the
// fundamental, so a candidate period is only accepted once double that period has been
// checked too (unless double is out of range). Hysteresis keeps a fading note as one
// note: no retriggers and no octave flips on the decay.
//
// Character (0 tight .. 1 wild) brings back the original's glitches on purpose:
// harmonic grabbing, octave flips on the decay and nervous retriggers.
class MonoTracker
{
public:
    enum class Event { None, NoteOn, NoteChange, NoteOff };
    enum Range { Bass = 0, Guitar = 1, Synth = 2 };

    struct Settings
    {
        int range = Guitar;
        float lowestHz = 28.0f;     // in Bass: low B is 30.9 Hz
        float sensitivity = 0.5f;   // gate level
        float noteHold = 0.6f;      // how long a fading note survives below the gate
        float character = 0.15f;    // tight .. wild
        bool octaveGuard = true;
    };

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        decim = std::max (1, (int) std::lround (sr / 8000.0f));
        fsA = sr / (float) decim;
        for (auto& b : aa) b.reset();
        aa[0].lowpass (fsA * 0.36f, 0.54f, sr);
        aa[1].lowpass (fsA * 0.36f, 1.31f, sr);
        dcBlock.highpass (18.0f, 0.707f, sr);
        buf.fill (0.0f);
        pos = 0; written = 0; counter = 0; hopCounter = 0;
        envFast = envSlow = 0.0f; noiseFloor = 0.004f; gate = 0.014f;   // assume a noisy input until it proves quiet
        steadyRef = 0.0f; steadyHops = 0; power = 0.0f; levelSm = 0.0f; bgRef = 0.0f; bgHops = 0; humMemory = 0.0f; warmup = (int) (0.6f * fsA);
        fastA = std::exp (-1.0f / (0.0007f * sr)); fastR = std::exp (-1.0f / (0.030f * sr));
        slowA = std::exp (-1.0f / (0.040f * sr)); slowR = std::exp (-1.0f / (0.250f * sr));
        state = Idle; note = -1.0f; shown = -1.0f; clarity = 0.0f;
        sinceOnset = 1 << 20; belowGate = 0; refractory = 0;
        emphHz = 0.0f; emphasis.reset();
        configure (settings);
    }

    void configure (const Settings& s)
    {
        settings = s;
        float lo = 70.0f, hi = 1400.0f, emph = 1300.0f;
        if (s.range == Bass) { lo = std::max (25.0f, s.lowestHz * 0.94f); hi = 1000.0f; emph = 520.0f; }
        if (s.range == Synth) { lo = 40.0f; hi = 2000.0f; emph = 2400.0f; }
        tauMax = std::min ((int) kBuf / 2 - 4, (int) std::ceil (fsA / lo));
        tauMin = std::max (4, (int) std::floor (fsA / hi));
        if (std::abs (emph - emphHz) > 1.0f)
        {
            emphHz = emph;
            emphasis.lowpass (std::min (emph, fsA * 0.45f), 0.707f, fsA);
        }

    }

    // Feed one input sample. Returns the event decided at this sample, if any.
    Event push (float x)
    {
        x = dcBlock.process (x);
        const float ax = std::abs (x);
        envFast = ax > envFast ? ax + fastA * (envFast - ax) : ax + fastR * (envFast - ax);
        envSlow = envFast > envSlow ? envFast + slowA * (envSlow - envFast) : envFast + slowR * (envSlow - envFast);

        const float y = aa[1].process (aa[0].process (x));
        if (++counter < decim) return Event::None;
        counter = 0;

        buf[(size_t) pos] = emphasis.process (y);
        pos = (pos + 1) & (kBuf - 1);
        ++written;
        if (sinceOnset < (1 << 20)) ++sinceOnset;
        if (refractory > 0) --refractory;

        // the noise floor: drops quickly, creeps up slowly, and only between notes
        // smooth level (120 ms): steady for hum, falling for a plucked string
        power += (y * y - power) * (1.0f / (0.12f * fsA));
        const float lvl = std::sqrt (power);
        levelSm = lvl;
        // the first 0.6 s after loading only listens, so hum that's already there is never a note
        if (warmup > 0)
        {
            --warmup;
            noiseFloor += (envFast - noiseFloor) * 0.01f;
            noiseFloor = std::max (1.0e-5f, std::min (0.05f, noiseFloor));
            gate = noiseFloor * 3.5f;
            return Event::None;
        }
        humMemory *= 1.0f - 0.17f / fsA;   // remembered hum fades by 1.5 dB a second
        if (envFast < noiseFloor) noiseFloor += (envFast - noiseFloor) * 0.02f;
        else if (state == Idle) noiseFloor *= 1.0f + 0.35f / fsA;   // about +3 dB per second
        noiseFloor = std::max (1.0e-5f, std::min (0.05f, noiseFloor));
        const float sens = 0.25f * std::pow (16.0f, 1.0f - settings.sensitivity);   // 0.25x .. 4x, 1x at noon
        gate = std::max (2.0e-4f * sens, std::max (noiseFloor * 3.5f, humMemory * 2.0f) * sens);

        detectOnset();
        if (++hopCounter < kHop) return Event::None;
        hopCounter = 0;
        return analyse();
    }

    // ---- what the engine and panel read ----
    float currentNote() const { return shown; }          // midi note with cents, -1 when silent
    float lastEstimate() const { return estimate; }       // keeps the last estimate for the tuner
    float currentClarity() const { return clarity; }
    float level() const { return envFast; }
    float noteLevel() const { return std::min (1.0f, envFast / std::max (1.0e-4f, notePeak)); }
    float lockMs() const { return lastLockMs; }
    bool active() const { return state == Active; }
    float analysisRate() const { return fsA; }
    void wildBoost (float amount) { boost = amount; }     // ring chaos turns the tracker wild while held
    float sampleRate() const { return sr; }

private:
    static constexpr int kBuf = 1024, kHop = 16; // 1024 analysis samples is about 128 ms
  public:
    static inline int kMinWin = 40, kAgree = 3;
    static inline float kWinFrac = 0.5f, kAgreeTol = 0.35f;
  private:
    enum State { Idle, Seeking, Active };

    float wild() const { return clamp01 (std::max (settings.character, boost)); }

    // Mains hum is perfectly periodic, so it reads as a very clear note. It sits at 50 or
    // 60 Hz and their multiples, which standard-tuned notes miss by 2% or more.
    static bool mainsPitch (float midi)
    {
        const float hz = mtof (midi);
        for (float m : { 50.0f, 100.0f, 150.0f, 200.0f, 250.0f, 300.0f, 60.0f, 120.0f, 180.0f, 240.0f, 360.0f })
            if (std::abs (hz - m) < m * 0.01f) return true;
        return false;
    }
    // While a note sounds: the pitch has settled on the mains and the level is dead steady,
    // or the note has faded and the mains is what's left.
    bool hummingAlong (float est)
    {
        if (settings.range == Synth) return false;
        const bool onMains = (est > 0.0f && mainsPitch (est)) || mainsPitch (note);
        if (! onMains) { steadyHops = 0; steadyRef = levelSm; return false; }
        if (est > 0.0f && mainsPitch (est) && ! mainsPitch (note) && envFast < 0.3f * notePeak) return true;
        if (levelSm > steadyRef * 0.84f && levelSm < steadyRef * 1.19f) ++steadyHops; else { steadyHops = 0; steadyRef = levelSm; }
        return mainsPitch (note) && (float) steadyHops * (float) kHop > 0.35f * fsA;
    }
  public:
    float floorLevel() const { return std::max (noiseFloor, humMemory * 0.6f); }
    bool warmingUp() const { return warmup > 0; }
    static bool isMainsPitch (float midi) { return mainsPitch (midi); }
  private:

    void detectOnset()
    {
        // a pluck: the fast envelope jumps over the slow one
        const float w = wild();
        const float ratio = 1.55f - 0.45f * w;
        if (refractory == 0 && envFast > gate * 1.5f && envFast > envSlow * ratio + gate * 0.5f)
        {
            onsetPending = true;
            sinceOnset = 0;
            refractory = (int) (0.045f * fsA * (1.0f - 0.6f * w));
        }
    }

    // NSDF at lag tau over the newest w samples, limited to data since the onset
    float nsdf (int tau, int w) const
    {
        float acf = 0.0f, m = 0.0f;
        int i = (pos - 1) & (kBuf - 1);
        int j = (pos - 1 - tau) & (kBuf - 1);
        for (int k = 0; k < w; ++k)
        {
            const float a = buf[(size_t) i], b = buf[(size_t) j];
            acf += a * b; m += a * a + b * b;
            i = (i - 1) & (kBuf - 1); j = (j - 1) & (kBuf - 1);
        }
        return m > 1.0e-9f ? 2.0f * acf / m : 0.0f;
    }

    int windowFor (int tau) const { return std::max (kMinWin, (int) ((float) tau * kWinFrac)); }

    // Returns a period in analysis samples (fractional), or 0 when nothing is clear yet.
    // waiting is set when a candidate exists but its octave check needs more data.
    float findPeriod (int valid, bool& waiting, float& clar)
    {
        waiting = false; clar = 0.0f;
        int last = tauMin - 1;
        for (int t = tauMin; t <= tauMax; ++t)
        {
            if (t + windowFor (t) > valid) break;
            curve[(size_t) t] = nsdf (t, windowFor (t));
            last = t;
        }
        if (last < tauMin + 2) return 0.0f;

        // key maxima: the highest point of each positive lobe after the first zero crossing
        int nPeaks = 0;
        float best = 0.0f;
        int t = tauMin;
        while (t <= last && curve[(size_t) t] > 0.0f) ++t;  // skip the zero-lag lobe
        while (t <= last && nPeaks < (int) peaks.size())
        {
            while (t <= last && curve[(size_t) t] <= 0.0f) ++t;
            if (t > last) break;
            int pk = t;
            while (t <= last && curve[(size_t) t] > 0.0f) { if (curve[(size_t) t] > curve[(size_t) pk]) pk = t; ++t; }
            const bool closed = t <= last;  // the lobe ended inside the data we have
            if (closed || pk < last) { peaks[(size_t) nPeaks++] = pk; best = std::max (best, curve[(size_t) pk]); }
        }
        if (nPeaks == 0 || best < 0.5f) return 0.0f;

        const float w = wild();

        // while a note holds, stay with the peak nearest the note unless something clearly better appears
        if (state == Active && note >= 0.0f)
        {
            const float expect = fsA / mtof (note);
            for (int i = 0; i < nPeaks; ++i)
            {
                const int pk = peaks[(size_t) i];
                if (std::abs ((float) pk - expect) <= expect * 0.045f + 1.0f && curve[(size_t) pk] >= 0.72f * best && curve[(size_t) pk] > 0.55f)
                    return refine (pk, valid, clar);
            }
        }

        const float k = 0.88f - 0.2f * w;
        int chosen = -1;
        for (int i = 0; i < nPeaks; ++i)
            if (curve[(size_t) peaks[(size_t) i]] >= k * best) { chosen = peaks[(size_t) i]; break; }
        if (chosen < 0) return 0.0f;

        // octave guard: a period could be a harmonic of a longer one, so check 2, 3 and 4 times it
        const bool grab = w > 0.45f && rng.uni() < (w - 0.45f) * 1.2f;   // wild: grab the harmonic anyway
        if (settings.octaveGuard && ! grab)
        {
            const float base = peakValue (chosen, valid);
            int pick = chosen; float pickV = base + 0.04f;
            for (int mult = 2; mult <= 4; ++mult)
            {
                const int t2 = chosen * mult;
                if (t2 > tauMax) break;
                if (t2 + mult + windowFor (t2 + mult) > valid)
                {
                    if (base < 0.97f) { waiting = true; return 0.0f; }
                    break;
                }
                float v2 = -1.0f; int t2best = t2;
                for (int d = -mult; d <= mult; ++d)
                {
                    const int tt = t2 + d;
                    if (tt > tauMax) continue;
                    const float v = nsdf (tt, windowFor (tt));
                    if (v > v2) { v2 = v; t2best = tt; }
                }
                v2 = peakValue (t2best, valid);
                if (v2 > pickV) { pick = t2best; pickV = v2 + 0.01f; }
            }
            chosen = pick;
        }
        return refine (chosen, valid, clar);
    }

    // interpolated height of the NSDF peak at or next to lag t
    float peakValue (int t, int valid) const
    {
        auto at = [&] (int tt) -> float
        {
            if (tt < tauMin || tt > tauMax || tt + windowFor (tt) > valid) return -2.0f;
            return nsdf (tt, windowFor (tt));
        };
        const float a = at (t - 1), b = at (t), c = at (t + 1);
        if (a < -1.5f || c < -1.5f) return b;
        const float den = a - 2.0f * b + c;
        if (den >= -1.0e-6f) return std::max (b, std::max (a, c));
        const float off = std::max (-0.5f, std::min (0.5f, 0.5f * (a - c) / den));
        return b - 0.25f * (a - c) * off;
    }

    float refine (int t, int valid, float& clar)
    {
        auto at = [&] (int tt) -> float
        {
            if (tt < tauMin || tt > tauMax || tt + windowFor (tt) > valid) return -1.0f;
            return nsdf (tt, windowFor (tt));
        };
        const float a = at (t - 1), b = at (t), c = at (t + 1);
        clar = b;
        if (a < -0.5f || c < -0.5f) return (float) t;
        const float den = a - 2.0f * b + c;
        const float off = std::abs (den) > 1.0e-6f ? 0.5f * (a - c) / den : 0.0f;
        clar = b - 0.25f * (a - c) * off;
        return (float) t + std::max (-0.5f, std::min (0.5f, off));
    }

    Event analyse()
    {
        // Background is whatever holds dead steady: a string always fades, hum and hiss never
        // do. Half a second within a third of a dB and the level is learned as the noise floor.
        // Only while nothing is being played: evenly repeated notes can look steady too.
        const bool listening = state == Idle || (state == Seeking && written - seekStart > (long long) (0.3f * fsA)) || (state == Active && mainsPitch (note));
        if (listening && levelSm > bgRef * 0.96f && levelSm < bgRef * 1.04f) ++bgHops; else { bgRef = levelSm; bgHops = 0; }
        if (settings.range != Synth && (float) bgHops * (float) kHop > 0.45f * fsA)
            humMemory = std::min (0.1f, std::max (humMemory, envFast));   // its peaks, so the gate sits clear above it

        const float w = wild();
        const bool aboveGate = envFast > gate;
        const int full = (int) std::min<long long> (written, kBuf - 1);
        Event ev = Event::None;

        if (onsetPending)
        {
            onsetPending = false;
            if (state == Active || state == Idle) { state = Seeking; seekStart = written; agree = 0; }
        }
        if (state == Idle && aboveGate) { state = Seeking; seekStart = written; sinceOnset = std::min (sinceOnset, 8); agree = 0; }

        // data since the onset (plus a little pre-roll), or everything while a note holds
        const int valid = state == Seeking ? std::min (full, sinceOnset + 6) : full;
        bool waiting = false; float clar = 0.0f;
        const float period = aboveGate || state == Active ? findPeriod (valid, waiting, clar) : 0.0f;
        clarity = clar;
        float est = period > 0.0f ? ftom (fsA / period) : -1.0f;
        if (est > 0.0f) estimate = est;

        if (state == Seeking)
        {
            if (est > 0.0f && clar > 0.80f - 0.15f * w && std::abs (est - candidate) < kAgreeTol) ++agree;
            else agree = est > 0.0f ? 1 : 0;
            candidate = est;
            if (! aboveGate && written - seekStart > (long long) (0.08f * fsA))
            {
                if (note >= 0.0f) { ev = Event::NoteOff; note = -1.0f; }
                state = Idle;
            }
            else if (est > 0.0f && clar > 0.80f - 0.15f * w && (agree >= kAgree || (w > 0.7f && agree >= 1)))
            {
                const bool retrigger = note >= 0.0f;
                note = est; shown = est; notePeak = std::max (envFast, gate * 2.0f);
                state = Active; belowGate = 0; changeVotes = 0; octaveVotes = 0;
                lastLockMs = (float) sinceOnset * 1000.0f / fsA;
                ev = retrigger ? Event::NoteOn : Event::NoteOn;
            }
            else if (written - seekStart > (long long) (0.35f * fsA) && note >= 0.0f)
            {
                state = Active; // nothing clear after a re-pluck: keep the old note
            }
        }
        else if (state == Active)
        {
            notePeak = std::max (notePeak * 0.9995f, envFast);
            // a clean, clear note keeps sounding as it fades, down to near the noise floor
            const bool clearNote = clarity > 0.85f && envFast > noiseFloor * 2.0f;
            if (envFast < gate * (0.55f + 0.3f * w) && ! clearNote) ++belowGate; else belowGate = 0;
            const float holdS = 0.015f + 0.15f * settings.noteHold * settings.noteHold;
            if ((float) belowGate > holdS * fsA / (float) kHop)
            {
                state = Idle; note = -1.0f; shown = -1.0f; ev = Event::NoteOff;
            }
            else if (hummingAlong (est))
            {
                // the string has stopped and what's left is mains hum: end the note and learn the level
                humMemory = std::min (0.1f, std::max (humMemory, envFast));
                state = Idle; note = -1.0f; shown = -1.0f; ev = Event::NoteOff;
            }
            else if (est > 0.0f && clar > 0.72f)
            {
                const float d = est - note;
                const bool octave = std::abs (std::abs (d) - 12.0f) < 0.7f;
                const float tol = 0.6f - 0.35f * w;
                if (std::abs (d) < tol)
                {
                    note += (est - note) * 0.5f;   // vibrato and bends follow smoothly
                    changeVotes = 0; octaveVotes = 0;
                }
                else if (octave && settings.octaveGuard && w < 0.5f)
                {
                    // an octave jump mid-note is almost always a harmonic: needs many votes
                    if (++octaveVotes > 12) { note = est; ev = Event::NoteChange; octaveVotes = 0; }
                }
                else
                {
                    if (changeVotes > 0 && std::abs (est - changeTo) < kAgreeTol) ++changeVotes; else { changeVotes = 1; changeTo = est; }
                    if (changeVotes >= (w > 0.6f ? 1 : kAgree)) { note = est; changeVotes = 0; ev = Event::NoteChange; }
                }
                shown = note;
            }

            // wild: octave flips while the note fades, as the original does
            if (ev == Event::None && w > 0.3f && envFast < notePeak * 0.35f && rng.uni() < (w - 0.3f) * 0.03f)
            {
                shown = note + (rng.uni() < 0.5f ? 12.0f : -12.0f);
                flipHops = 10 + (int) (rng.uni() * 40.0f);
                ev = Event::NoteChange;
            }
            else if (flipHops > 0 && --flipHops == 0) { shown = note; ev = Event::NoteChange; }
        }
        return ev;
    }

    Settings settings;
    float sr = 48000.0f, fsA = 8000.0f;
    int decim = 6, counter = 0, hopCounter = 0;
    Biquad aa[2], dcBlock, emphasis;
    float emphHz = 0.0f;
    std::array<float, kBuf> buf {};
    std::array<float, kBuf> curve {};
    std::array<int, 64> peaks {};
    int pos = 0;
    long long written = 0, seekStart = 0;
    int tauMin = 6, tauMax = 300;
    float steadyRef = 0.0f, power = 0.0f, levelSm = 0.0f, bgRef = 0.0f, humMemory = 0.0f; int steadyHops = 0, bgHops = 0, warmup = 0;
    float noiseFloor = 0.004f, envFast = 0, envSlow = 0, fastA = 0, fastR = 0, slowA = 0, slowR = 0, gate = 0.002f;
    int sinceOnset = 1 << 20, refractory = 0, belowGate = 0, changeVotes = 0, octaveVotes = 0, flipHops = 0;
    bool onsetPending = false;
    State state = Idle;
    int agree = 0;
    float changeTo = 0.0f, candidate = -100.0f, note = -1.0f, shown = -1.0f, estimate = -1.0f, clarity = 0.0f, notePeak = 0.01f, lastLockMs = 0.0f, boost = 0.0f;
    Rng rng;
};
} // namespace chg
