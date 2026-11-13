#pragma once

#include "DspUtil.h"
#include <array>

namespace chg
{
// Polyphonic note finder for chords.
//
// Every 4 ms it takes the audio since the last pick (up to 128 ms), finds the notes by
// harmonic sum (a note scores with its overtones), and takes each found note's overtones
// out before looking for the next one, so a ringing E chord doesn't read as one low E1
// or spawn ghost octaves. A chord is decided about 40 ms after the strum and then held:
// notes stay until their strings fade or you play again, so voices never flicker.
// Each note keeps its own exact pitch, so bends and out-of-tune strings are followed.
class ChordTracker
{
public:
    static constexpr int kMaxNotes = 6;
    struct Found { int id = 0; float midi = 0.0f; float strength = 0.0f; bool on = false; };

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        decim = std::max (1, (int) std::lround (sr / 8000.0f));
        fsA = sr / (float) decim;
        aa[0].lowpass (fsA * 0.4f, 0.54f, sr); aa[1].lowpass (fsA * 0.4f, 1.31f, sr);
        aa[0].reset(); aa[1].reset();
        dc.highpass (25.0f, 0.707f, sr); dc.reset();
        buf.assign (kBuf, 0.0f); pos = 0; written = 0; counter = 0; hop = 0;
        spec.assign ((size_t) (2 * kFft), 0.0f);
        mag.assign ((size_t) (kFft / 2 + 1), 0.0f);
        work.assign ((size_t) (kFft / 2 + 1), 0.0f);
        for (auto& n : notes) n = Found();
        envFast = envSlow = 0.0f; noiseFloor = 0.004f;
        sinceOnset = 1 << 20; refractory = 0; nextId = 1;
        fastA = std::exp (-1.0f / (0.0007f * sr)); fastR = std::exp (-1.0f / (0.030f * sr));
        slowA = std::exp (-1.0f / (0.040f * sr)); slowR = std::exp (-1.0f / (0.250f * sr));
    }

    void setRange (float lowestMidi, float highestMidi) { lowM = lowestMidi; highM = highestMidi; }
    void setStyle (float quick01, float wild01) { quick = quick01; wild = wild01; }
    // the noise floor and hum check are shared with the mono tracker, which learns them
    void setNoise (float floor, bool rejectMains, bool listenOnly) { noiseFloor = floor; noMains = rejectMains; warm = listenOnly; }

    // Feed one input sample; returns true when the note list changed
    bool push (float x)
    {
        x = dc.process (x);
        const float ax = std::abs (x);
        envFast = ax > envFast ? ax + fastA * (envFast - ax) : ax + fastR * (envFast - ax);
        envSlow = envFast > envSlow ? envFast + slowA * (envSlow - envFast) : envFast + slowR * (envSlow - envFast);
        const float y = aa[1].process (aa[0].process (x));
        if (++counter < decim) return false;
        counter = 0;
        buf[(size_t) pos] = y;
        pos = (pos + 1) & (kBuf - 1);
        ++written;
        if (sinceOnset < (1 << 20)) ++sinceOnset;
        if (refractory > 0) --refractory;

        const float gate = std::max (2.0e-4f, noiseFloor * 3.5f);

        // a strum or pick: the fast envelope jumps over the slow one
        if (! warm && refractory == 0 && envFast > gate * 1.5f && envFast > envSlow * 1.5f + gate * 0.5f)
        {
            sinceOnset = 0;
            refractory = (int) (0.05f * fsA);
            decided = false;
            peakSinceOnset = 0.0f;
            pendCount.fill (0);
        }
        peakSinceOnset = std::max (peakSinceOnset, envFast);

        if (++hop < kHop) return false;
        hop = 0;
        if (warm) return false;
        return analyse (gate);
    }

    const std::array<Found, kMaxNotes>& found() const { return notes; }
    bool anyOn() const { for (auto& n : notes) if (n.on) return true; return false; }
    int msSinceOnset() const { return (int) ((float) sinceOnset * 1000.0f / fsA); }

private:
    static constexpr int kBuf = 2048, kFft = 2048, kHop = 32, kMaxLen = 1024;

    // magnitude at a frequency: the highest bin within a quarter semitone, interpolated
    float peakAt (const std::vector<float>& m, float hz) const
    {
        const float bin = hz * (float) kFft / fsA;
        const float span = std::max (1.0f, bin * 0.0145f);
        const int lo = std::max (1, (int) std::floor (bin - span)), hi = std::min ((int) m.size() - 2, (int) std::ceil (bin + span));
        float best = 0.0f;
        for (int b = lo; b <= hi; ++b) best = std::max (best, m[(size_t) b]);
        return best;
    }

    // how many of a note's first six partials stand out as real peaks: noise from the pick
    // makes random bumps, a string makes a row of them
    int harmonicPeaks (float f0) const
    {
        float top = 0.0f; float amp[7] {};
        for (int h = 1; h <= 6; ++h) { amp[h] = peakAt (work, f0 * (float) h); top = std::max (top, amp[h]); }
        int count = 0;
        for (int h = 1; h <= 6; ++h)
        {
            if (f0 * (float) h > fsA * 0.45f) break;
            const float bin = f0 * (float) h * (float) kFft / fsA;
            const float side = std::max (3.0f, bin * 0.03f);   // compare with the spectrum either side
            const float floorL = work[(size_t) std::max (1, (int) (bin - 2.5f * side))];
            const float floorR = work[(size_t) std::min ((int) work.size() - 1, (int) (bin + 2.5f * side))];
            if (amp[h] > 0.25f * top && amp[h] > 1.6f * std::max (floorL, floorR)) ++count;
        }
        return count;
    }

    static bool isMains (float midi)
    {
        const float hz = mtof (midi);
        for (float m : { 50.0f, 100.0f, 150.0f, 200.0f, 250.0f, 300.0f, 60.0f, 120.0f, 180.0f, 240.0f, 360.0f })
            if (std::abs (hz - m) < m * 0.01f) return true;
        return false;
    }

    bool analyse (float gate)
    {
        const bool fresh = sinceOnset < (int) (0.5f * fsA);
        const int minLen = (int) ((0.026f + 0.02f * (1.0f - quick)) * fsA);   // about 36 ms at noon
        if (fresh && sinceOnset < minLen) return false;
        // the gate opens above the noise floor and closes below it; hum sits under both
        if (envFast > gate) open = true;
        if (! open || envFast < gate * 0.6f)
        {
            if (envFast < gate * 0.6f) open = false;
            // silence for 30 ms ends the chord
            if (++quietHops < 8) return false;
            bool changed = false;
            for (auto& n : notes) if (n.on) { n.on = false; changed = true; }
            return changed;
        }
        quietHops = 0;

        // the audio since the pick (or the last 128 ms), Hann windowed and zero padded
        const int len = fresh ? std::min (kMaxLen, sinceOnset + 8) : kMaxLen;
        std::fill (spec.begin(), spec.end(), 0.0f);
        for (int i = 0; i < len; ++i)
        {
            const float wv = 0.5f - 0.5f * std::cos (kTwoPi * ((float) i + 0.5f) / (float) len);
            spec[(size_t) (2 * i)] = buf[(size_t) ((pos - len + i) & (kBuf - 1))] * wv;
        }
        fft (spec, kFft);
        float maxMag = 1.0e-9f;
        for (int b = 0; b <= kFft / 2; ++b)
        {
            const float re = spec[(size_t) (2 * b)], im = spec[(size_t) (2 * b + 1)];
            mag[(size_t) b] = std::sqrt (std::sqrt (re * re + im * im));   // compressed magnitude
            maxMag = std::max (maxMag, mag[(size_t) b]);
        }
        work = mag;

        // find notes one at a time, removing each one's overtones
        std::array<float, kMaxNotes> picks {}; std::array<float, kMaxNotes> strength {};
        int count = 0;
        float first = 0.0f;
        const float step = 0.25f;   // a quarter of a semitone; the exact pitch is refined below
        int humFound = 0;
        for (int n = 0; n < kMaxNotes + 3 && count < kMaxNotes && humFound < 3; ++n)
        {
            float bestS = 0.0f, bestM = -1.0f;
            for (float m = lowM; m <= highM; m += step)
            {
                bool taken = false;
                for (int k = 0; k < count; ++k) taken = taken || std::abs (picks[(size_t) k] - m) < 1.2f;
                if (taken) continue;
                const float f0 = mtof (m);
                // the note itself, or its first overtone, must really be there: no phantom low roots
                const float h1 = peakAt (work, f0), h2 = peakAt (work, 2.0f * f0);
                if (std::max (h1, h2) < 0.18f * maxMag) continue;
                float sal = 0.0f;
                for (int h = 1; h <= 10; ++h)
                {
                    const float fh = f0 * (float) h;
                    if (fh > fsA * 0.45f) break;
                    sal += peakAt (work, fh) * (f0 + 30.0f) / (fh + 30.0f);
                }
                if (sal > bestS && harmonicPeaks (f0) >= 3) { bestS = sal; bestM = m; }
            }
            if (bestM < 0.0f) break;
            if (n == 0) first = bestS;
            else if (bestS < first * (0.42f - 0.15f * wild)) break;

            // exact pitch: where its first partials really are, interpolated between bins
            float refined = bestM;
            {
                const float f0 = mtof (bestM);
                // each partial votes; the median ignores one that collides with another string's
                float votes[4]; int nv = 0;
                for (int h = 1; h <= 4; ++h)
                {
                    const float bin = f0 * (float) h * (float) kFft / fsA;
                    if (f0 * (float) h > fsA * 0.45f) break;
                    const int lo = std::max (2, (int) std::floor (bin * 0.982f)), hi = std::min ((int) mag.size() - 3, (int) std::ceil (bin * 1.018f));
                    int pk = lo;
                    for (int q = lo; q <= hi; ++q) if (mag[(size_t) q] > mag[(size_t) pk]) pk = q;
                    if (pk == lo || pk == hi) continue;   // not a peak inside the note's window
                    const float a = mag[(size_t) (pk - 1)], c = mag[(size_t) (pk + 1)], m0 = mag[(size_t) pk];
                    const float d = a - 2.0f * m0 + c;
                    const float off = d < -1.0e-9f ? 0.5f * (a - c) / d : 0.0f;
                    const float fh = ((float) pk + off) * fsA / (float) kFft;
                    const float e = ftom (fh / (float) h);
                    if (std::abs (e - bestM) < 0.6f) votes[nv++] = e;
                }
                if (nv > 0)
                {
                    std::sort (votes, votes + nv);
                    refined = nv % 2 ? votes[nv / 2] : 0.5f * (votes[nv / 2 - 1] + votes[nv / 2]);
                }
            }
            // mains hum, not a string: take it out of the spectrum below but don't play it
            const bool humNote = noMains && isMains (refined);
            if (! humNote) { picks[(size_t) count] = refined; strength[(size_t) count] = bestS; ++count; }
            else ++humFound;

            // take its overtones out, smoothed across neighbouring harmonics so a note sharing
            // overtones with this one (an octave above) keeps what's left for it
            const float f0 = mtof (refined);
            float amp[12] {};
            for (int h = 1; h <= 10; ++h) amp[h] = peakAt (work, f0 * (float) h);
            for (int h = 1; h <= 10; ++h)
            {
                const float smooth = std::min (amp[h], (amp[h - 1 > 0 ? h - 1 : 1] + amp[h] + amp[h + 1 <= 10 ? h + 1 : 10]) / 3.0f) * (1.0f - 0.6f * wild);
                const float bin = f0 * (float) h * (float) kFft / fsA;
                // as wide as the window smears a partial: shorter windows smear wider
                const float span = std::max (2.2f * (float) kFft / (float) len, bin * 0.02f);
                for (int b = std::max (1, (int) (bin - span)); b <= std::min ((int) work.size() - 1, (int) (bin + span)); ++b)
                    work[(size_t) b] = std::max (0.0f, work[(size_t) b] - smooth);
            }
        }

        // hold the chord: match to the notes already sounding
        bool changed = false;
        std::array<bool, kMaxNotes> matched {};
        for (int k = 0; k < count; ++k)
        {
            int at = -1;
            for (int i = 0; i < kMaxNotes; ++i)
                if (notes[(size_t) i].on && ! matched[(size_t) i] && std::abs (notes[(size_t) i].midi - picks[(size_t) k]) < ((fresh && ! decided) ? 0.4f : 1.1f)) { at = i; break; }
            if (at >= 0)
            {
                auto& nt = notes[(size_t) at];
                // follows bends and drift, but a stray estimate can't drag it to the next note
                const float d = picks[(size_t) k] - nt.midi;
                if (std::abs (d) < 0.4f) { nt.midi += d * 0.3f; offCount[(size_t) at] = 0; }
                else if (++offCount[(size_t) at] >= 3) { nt.midi = picks[(size_t) k]; offCount[(size_t) at] = 0; }   // it really is the next note along
                nt.strength = strength[(size_t) k];
                matched[(size_t) at] = true;
                missing[(size_t) at] = 0;
                continue;
            }
            // a new note: soon after a pick, or while you're still playing hard. A fading or
            // damped chord can't spawn notes from what's left of it.
            if (! fresh && envFast < 0.5f * peakSinceOnset) continue;
            {
                int pi = -1;
                for (int q = 0; q < kMaxNotes; ++q) if (pendCount[(size_t) q] > 0 && std::abs (pending[(size_t) q] - picks[(size_t) k]) < 0.5f) pi = q;
                if (pi < 0)
                {
                    int slot = 0;
                    for (int q = 1; q < kMaxNotes; ++q) if (pendCount[(size_t) q] < pendCount[(size_t) slot]) slot = q;
                    pending[(size_t) slot] = picks[(size_t) k]; pendCount[(size_t) slot] = 1;
                    continue;
                }
                if (++pendCount[(size_t) pi] < 2) continue;
                pendCount[(size_t) pi] = 0;
            }
            bool dup = false;   // never two copies of one pitch
            for (int i = 0; i < kMaxNotes; ++i) dup = dup || (notes[(size_t) i].on && std::abs (notes[(size_t) i].midi - picks[(size_t) k]) < 1.2f);
            if (dup) continue;
            for (int i = 0; i < kMaxNotes; ++i)
                if (! notes[(size_t) i].on)
                {
                    notes[(size_t) i] = { nextId++, picks[(size_t) k], strength[(size_t) k], true };
                    matched[(size_t) i] = true; missing[(size_t) i] = 0;
                    changed = true;
                    break;
                }
        }
        // notes not heard: a new strum replaces the chord, otherwise they fade out slowly
        for (int i = 0; i < kMaxNotes; ++i)
        {
            auto& nt = notes[(size_t) i];
            if (! nt.on || matched[(size_t) i]) continue;
            const int limit = (fresh && ! decided) ? 1 : 15;   // 60 ms of absence once the chord has settled
            if (++missing[(size_t) i] >= limit) { nt.on = false; changed = true; }
        }
        if (fresh) decided = true;
        return changed;
    }

    float sr = 48000.0f, fsA = 8000.0f, lowM = 40.0f, highM = 88.0f, quick = 0.5f, wild = 0.0f;
    int decim = 6, counter = 0, hop = 0, pos = 0, sinceOnset = 1 << 20, refractory = 0, nextId = 1;
    long long written = 0;
    bool decided = false, noMains = true, warm = true, open = false;
    Biquad aa[2], dc;
    std::vector<float> buf, spec, mag, work;
    std::array<Found, kMaxNotes> notes {};
    std::array<int, kMaxNotes> missing {}, offCount {};
    std::array<float, kMaxNotes> pending {};
    std::array<int, kMaxNotes> pendCount {};
    int quietHops = 0;
    float envFast = 0, envSlow = 0, fastA = 0, fastR = 0, slowA = 0, slowR = 0, noiseFloor = 0.004f, peakSinceOnset = 0.0f;
};
} // namespace chg
