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
        envFast = envSlow = 0.0f;
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
        gate = 0.0009f + 0.03f * (1.0f - s.sensitivity) * (1.0f - s.sensitivity);
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
    enum State { Idle, Seeking, Active };

    float wild() const { return clamp01 (std::max (settings.character, boost)); }

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

    int windowFor (int tau) const { return std::max (32, tau / 3); }

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
        const float w = wild();
        const bool aboveGate = envFast > gate;
        const int full = (int) std::min<long long> (written, kBuf - 1);
        Event ev = Event::None;

        if (onsetPending)
        {
            onsetPending = false;
            if (state == Active || state == Idle) { state = Seeking; seekStart = written; }
        }
        if (state == Idle && aboveGate) { state = Seeking; seekStart = written; sinceOnset = std::min (sinceOnset, 8); }

        // data since the onset (plus a little pre-roll), or everything while a note holds
        const int valid = state == Seeking ? std::min (full, sinceOnset + 6) : full;
        bool waiting = false; float clar = 0.0f;
        const float period = aboveGate || state == Active ? findPeriod (valid, waiting, clar) : 0.0f;
        clarity = clar;
        float est = period > 0.0f ? ftom (fsA / period) : -1.0f;
        if (est > 0.0f) estimate = est;

        if (state == Seeking)
        {
            const float prevCandidate = candidate;
            candidate = est;
            if (! aboveGate && written - seekStart > (long long) (0.08f * fsA))
            {
                if (note >= 0.0f) { ev = Event::NoteOff; note = -1.0f; }
                state = Idle;
            }
            else if (est > 0.0f && clar > 0.80f - 0.15f * w && (std::abs (est - prevCandidate) < 0.5f || w > 0.7f))
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
            if (envFast < gate * (0.55f + 0.3f * w)) ++belowGate; else belowGate = 0;
            const float holdS = 0.015f + 0.15f * settings.noteHold * settings.noteHold;
            if ((float) belowGate > holdS * fsA / (float) kHop)
            {
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
                else if (++changeVotes >= (w > 0.6f ? 1 : 2))
                {
                    note = est; changeVotes = 0; ev = Event::NoteChange;
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
    float envFast = 0, envSlow = 0, fastA = 0, fastR = 0, slowA = 0, slowR = 0, gate = 0.002f;
    int sinceOnset = 1 << 20, refractory = 0, belowGate = 0, changeVotes = 0, octaveVotes = 0, flipHops = 0;
    bool onsetPending = false;
    State state = Idle;
    float candidate = -100.0f, note = -1.0f, shown = -1.0f, estimate = -1.0f, clarity = 0.0f, notePeak = 0.01f, lastLockMs = 0.0f, boost = 0.0f;
    Rng rng;
};

// Polyphonic note finder for chords: a bank of semitone band-passes at the analysis
// rate, a harmonic-sum salience, and harmonic subtraction so overtones don't spawn ghost
// notes. Low chords take longer, as on the original. Mono notes come from MonoTracker.
class PolyTracker
{
public:
    static constexpr int kLo = 26, kHi = 88, kN = kHi - kLo + 1, kMaxNotes = 6;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        decim = std::max (1, (int) std::lround (sr / 8000.0f));
        fsA = sr / (float) decim;
        aa[0].lowpass (fsA * 0.36f, 0.54f, sr); aa[1].lowpass (fsA * 0.36f, 1.31f, sr);
        aa[0].reset(); aa[1].reset();
        for (int i = 0; i < kN; ++i)
        {
            const float f = mtof ((float) (kLo + i));
            bank[(size_t) i].bandpass (std::min (f, fsA * 0.45f), 17.0f, fsA);
            bank[(size_t) i].reset();
            energy[(size_t) i] = 0.0f;
            const float tc = std::max (0.012f, 4.0f / f);
            coef[(size_t) i] = std::exp (-1.0f / (tc * fsA));
            onFor[(size_t) i] = 0; offFor[(size_t) i] = 0; isOn[(size_t) i] = false;
            norm[(size_t) i] = std::sqrt (100.0f / f);
        }
        counter = 0; hop = 0;
    }

    void setSpeed (float quick01, float wild01) { speed = quick01; wildness = wild01; }
    void setLowest (int midi) { lowest = std::max (kLo, midi); }

    // returns true when the note set was updated
    bool push (float x)
    {
        const float y = aa[1].process (aa[0].process (x));
        if (++counter < decim) return false;
        counter = 0;
        for (int i = 0; i < kN; ++i)
        {
            const float b = bank[(size_t) i].process (y);
            energy[(size_t) i] = b * b + coef[(size_t) i] * (energy[(size_t) i] - b * b);
        }
        if (++hop < 32) return false;   // 4 ms
        hop = 0;
        decide();
        return true;
    }

    bool on (int midi) const { return midi >= kLo && midi <= kHi && isOn[(size_t) (midi - kLo)]; }

private:
    void decide()
    {
        // amplitude per band, corrected for the wider bands of higher notes
        std::array<float, kN> e;
        float peak = 0.0f;
        for (int i = 0; i < kN; ++i) { e[(size_t) i] = std::sqrt (energy[(size_t) i]) * norm[(size_t) i]; peak = std::max (peak, e[(size_t) i]); }
        std::array<bool, kN> want {};
        const float absGate = 0.003f;
        if (peak > absGate)
        {
            // harmonic sum: a note scores with its overtones, so fundamentals beat their harmonics
            static const int harm[] = { 0, 12, 19, 24, 28, 31 };
            static const float hw[] = { 1.0f, 0.7f, 0.5f, 0.4f, 0.3f, 0.25f };
            auto salience = [&] (int i)
            {
                float sum = 0.0f;
                for (int h = 0; h < 6; ++h) if (i + harm[h] < kN) sum += hw[h] * e[(size_t) (i + harm[h])];
                return sum;
            };
            const float sub = 1.0f - 0.7f * wildness;
            float first = 0.0f;
            for (int n = 0; n < kMaxNotes; ++n)
            {
                int best = -1; float bv = 0.0f;
                for (int i = 0; i < kN; ++i)
                {
                    if (kLo + i < lowest || want[(size_t) i]) continue;
                    const float here = e[(size_t) i];
                    if (here < absGate * 0.5f) continue;
                    if (i > 0 && e[(size_t) (i - 1)] > here) continue;      // a peak along the keyboard
                    if (i < kN - 1 && e[(size_t) (i + 1)] > here) continue;
                    const float sv = salience (i);
                    if (sv > bv) { bv = sv; best = i; }
                }
                if (best < 0) break;
                if (n == 0) first = bv;
                else if (bv < first * (0.38f - 0.12f * wildness)) break;
                want[(size_t) best] = true;
                // take this note's overtones out before looking for the next note
                const float a0 = e[(size_t) best];
                for (int h = 1; h < 6; ++h)
                {
                    const int j = best + harm[h];
                    if (j < kN) e[(size_t) j] = std::max (0.0f, e[(size_t) j] - a0 * 1.1f * sub);
                }
                if (best > 0) e[(size_t) (best - 1)] *= 0.25f;   // neighbours leak through the bank
                if (best < kN - 1) e[(size_t) (best + 1)] *= 0.25f;
            }
        }
        const int needOn = speed > 0.5f ? 2 : 3 + (int) (wildness * 3.0f);
        for (int i = 0; i < kN; ++i)
        {
            if (want[(size_t) i]) { ++onFor[(size_t) i]; offFor[(size_t) i] = 0; }
            else { ++offFor[(size_t) i]; onFor[(size_t) i] = 0; }
            if (! isOn[(size_t) i] && onFor[(size_t) i] >= needOn) isOn[(size_t) i] = true;
            if (isOn[(size_t) i] && offFor[(size_t) i] >= 12) isOn[(size_t) i] = false;
        }
    }

    float sr = 48000.0f, fsA = 8000.0f, speed = 0.5f, wildness = 0.0f;
    int decim = 6, counter = 0, hop = 0, lowest = kLo;
    Biquad aa[2];
    std::array<Biquad, kN> bank;
    std::array<float, kN> energy {}, coef {}, norm {};
    std::array<int, kN> onFor {}, offFor {};
    std::array<bool, kN> isOn {};
};
} // namespace chg
