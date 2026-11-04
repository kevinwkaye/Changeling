#pragma once

#include "Effects.h"
#include "Tracker.h"
#include "Voice.h"
#include <array>
#include <atomic>

namespace chg
{
enum Mode { ModePoly = 0, ModeMono = 1, ModeArp = 2, ModeDry = 3 };
enum Input { InputGuitar = 0, InputBass = 1, InputSynth = 2 };
enum Source { SourceAudio = 0, SourceMidi = 1, SourceBoth = 2 };

struct EngineParams
{
    int mode = ModeMono, wave = 0;
    float pitch = 0.0f, glide = 0.15f;
    float cutoff = 0.55f, env = 0.5f, bandwidth = 0.3f;
    int filterType = 0, envType = 0;
    float sustain = 0.5f, ring = 0.0f, mod = 0.2f, mix = 0.6f, delayLevel = 0.3f, feedback = 0.35f;
    bool trails = true, bypass = false;
    int input = InputGuitar, source = SourceBoth;
    float bpm = 120.0f;
    int osc2Interval = 0, osc2Wave = 0;
    float osc2Level = 0.7f, osc2Detune = 0.0f;
    float character = 0.15f;
    int lowNote = 0;
    float sensitivity = 0.5f, noteHold = 0.5f, polySpeed = 0.5f;
    bool octaveGuard = true;
    int arpPattern = 0, arpStep = 0, arpOctaves = 1;
    float arpGate = 0.6f;
    bool ringFollow = false;
    int velocityMode = 2, bendRange = 2, midiMono = 0;
    bool latch = false, sustainHold = false, runaway = false, chaos = false, tuner = false;
};

class ChangelingEngine
{
public:
    static constexpr int kVoices = 6;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        mono.prepare (sampleRate);
        poly.prepare (sampleRate);
        echo.prepare (sr);
        shiftL.prepare (sr); shiftR.prepare (sr);
        compL.prepare (sr); compR.prepare (sr);
        dryFilterL.reset(); dryFilterR.reset();
        for (auto& v : voices) { v = Voice(); }
        followEnv = 0.0f; bypassSm = 0.0f; wetSm = 0.0f; ringMixSm = 0.0f; chaosSm = 0.0f;
        audioOn.fill (false); midiHeld.fill (false); midiVel.fill (0.0f);
        midiOrder.clear(); arpPlayed.clear();
        monoAudioVoice = monoMidiVoice = -1;
        arpCounter = 0.0; arpIndex = 0; arpRunning = false; now = 0; prevMode = -1;
        attackCoef = std::exp (-1.0f / (0.002f * sr));
        configured = false;
    }

    // ---- MIDI from a keyboard (call before process for the block) ----
    void noteOn (int note, float velocity)
    {
        if (note < 0 || note > 127) return;
        if (velocity <= 0.0f) { noteOff (note); return; }
        pendingMidi[(size_t) (pendingCount++ % kPending)] = { true, note, velocity };
    }
    void noteOff (int note) { if (note >= 0 && note < 128) pendingMidi[(size_t) (pendingCount++ % kPending)] = { false, note, 0.0f }; }
    void pitchBend (float minus1to1) { bend = std::max (-1.0f, std::min (1.0f, minus1to1)); }
    void modWheel (float v01) { wheel = clamp01 (v01); }
    void sustainPedal (bool down) { pedalDown = down; }
    void allNotesOff()
    {
        midiHeld.fill (false); midiOrder.clear();
        for (auto& v : voices) if (v.fromMidi) { v.release(); v.sustained = false; }
    }

    void process (float* L, float* R, int n, const EngineParams& p)
    {
        configure (p);
        applyPendingMidi (p);

        const float holdCoef = std::exp (-1.0f / (6.0f * sr));
        const float relS = 0.015f + p.sustain * p.sustain * 2.5f;
        const float relCoef = std::exp (-1.0f / (relS * sr));
        const float fdecS = 0.1f + 1.1f * p.sustain;
        const float fdecCoef = std::exp (-1.0f / (fdecS * sr));
        const float glideS = 0.002f + p.glide * p.glide * 2.0f;
        const float glideCoef = std::exp (-1.0f / (glideS * sr));
        const float baseCut = 40.0f * std::pow (400.0f, p.cutoff);
        const float bendSemis = bend * (p.bendRange == 0 ? 2.0f : (p.bendRange == 1 ? 7.0f : 12.0f));
        const float detuneCents = p.mod * 50.0f + wheel * 30.0f;
        static const float intervals[5] = { 0.0f, -12.0f, 7.0f, 12.0f, 19.0f };
        const float osc2Semis = intervals[std::max (0, std::min (4, p.osc2Interval))];
        const int osc2Wave = p.osc2Wave == 0 ? p.wave : p.osc2Wave - 1;
        const float osc2Gain = p.osc2Level;
        const float oscNorm = 1.0f / (1.0f + osc2Gain);
        const bool dry = p.mode == ModeDry;
        const bool useAudio = p.source != SourceMidi && ! dry;
        const float dryGain = std::min (1.0f, 2.0f * (1.0f - p.mix));
        const float wetGain = std::min (1.0f, 2.0f * p.mix);
        const float ringTarget = std::max (p.ring > 0.02f ? 1.0f : p.ring / 0.02f, chaosSm);
        const float fixedRingHz = 20.0f * std::pow (150.0f, p.ring);
        static const float ratios[11] = { 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
        const float followRatio = ratios[std::max (0, std::min (10, (int) (p.ring * 10.99f)))];
        const float quarter = 60.0f / std::max (20.0f, p.bpm);
        static const float stepMul[4] = { 1.0f, 0.5f, 1.0f / 3.0f, 0.25f };
        const double stepSamples = (double) (quarter * stepMul[std::max (0, std::min (3, p.arpStep))] * sr);

        if (! useAudio) releaseAudioNotes();
        if (p.bypass && ! p.trails && ! echoCleared && bypassSm > 0.99f) { echo.clear(); echoCleared = true; }
        if (! p.bypass) echoCleared = false;

        // more voices, less level each, so a six-note chord sits with a single note
        int sounding = 0;
        for (auto& v : voices) sounding += v.active && (v.gate || v.latched || v.sustained) ? 1 : 0;
        const float gainTarget = 0.34f / std::sqrt ((float) std::max (1, sounding));

        float peakIn = 0.0f, wetPeak = 0.0f;
        for (int i = 0; i < n; ++i, ++now)
        {
            const float inL = L[i], inR = R[i];
            const float in = 0.5f * (inL + inR);
            peakIn = std::max (peakIn, std::abs (in));

            // ---- tracking ----
            chaosSm += ((p.chaos ? 1.0f : 0.0f) - chaosSm) * 0.0005f;
            mono.wildBoost (chaosSm);
            const auto ev = mono.push (in);
            const bool polyTick = poly.push (in);
            const float lvl = mono.level();
            followEnv += (std::min (1.0f, lvl * 6.0f) - followEnv) * (lvl * 6.0f > followEnv ? 0.02f : 0.0006f);

            if (ev != MonoTracker::Event::None)
            {
                if (ev == MonoTracker::Event::NoteOn) { uiOnsets.fetch_add (1, std::memory_order_relaxed); dryEnvKick = true; }
                if (useAudio && ! p.bypass) handleMonoEvent (ev, p);
            }
            if (useAudio && ! p.bypass && (polyTick || ev != MonoTracker::Event::None) && (p.mode == ModePoly || p.mode == ModeArp))
                syncPolyNotes (p);

            // ---- arpeggiator ----
            if (p.mode == ModeArp && ! p.bypass) runArp (p, stepSamples);

            // ---- chaos: the ring carrier jumps about ----
            if (chaosSm > 0.05f && --chaosCountdown <= 0)
            {
                chaosHz = 30.0f * std::pow (60.0f, rng.uni());
                chaosCountdown = (int) ((0.04f + rng.uni() * 0.1f) * sr);
            }
            ringMixSm += (ringTarget - ringMixSm) * 0.002f;

            // ---- voices ----
            voiceGain += (gainTarget - voiceGain) * 0.002f;
            float synth = 0.0f;
            for (int vi = 0; vi < kVoices; ++vi)
            {
                auto& v = voices[(size_t) vi];
                if (! v.active) continue;
                if (v.arp && v.gate && ! v.latched && (double) now >= v.offAt) v.release();

                const float tgt = v.target + p.pitch + bendSemis;
                v.current = tgt + glideCoef * (v.current - tgt);
                const float f1 = mtof (v.current - detuneCents * 0.005f);
                const float f2 = mtof (v.current + osc2Semis + detuneCents * 0.005f + p.osc2Detune * 0.5f);
                const float dt1 = f1 / sr, dt2 = f2 / sr;
                v.ph1 += dt1; if (v.ph1 >= 1.0f) v.ph1 -= 1.0f;
                v.ph2 += dt2; if (v.ph2 >= 1.0f) v.ph2 -= 1.0f;
                float osc = (oscSample (p.wave, v.ph1, dt1) + osc2Gain * oscSample (osc2Wave, v.ph2, dt2)) * oscNorm;

                // amplitude: follows your playing at Sustain 0, holds full level at the top
                float ampTarget = 0.0f;
                if (v.latched) ampTarget = 0.8f;
                else if (v.sustained) ampTarget = v.amp * holdCoef;
                else if (v.gate)
                {
                    if (v.fromMidi || v.arp) ampTarget = 1.0f;
                    else ampTarget = p.sustain + (1.0f - p.sustain) * mono.noteLevel();
                }
                const float vel = p.velocityMode == 1 ? v.velocity : (p.velocityMode == 2 ? 0.6f + 0.4f * v.velocity : 1.0f);
                if (ampTarget > v.amp) v.amp = ampTarget + attackCoef * (v.amp - ampTarget);
                else v.amp = ampTarget + (v.gate || v.latched || v.sustained ? 0.9995f : relCoef) * (v.amp - ampTarget);
                if (! v.gate && ! v.latched && ! v.sustained && v.amp < 1.0e-4f) { v.active = false; continue; }

                // filter envelope: triggered, or following your playing
                if (p.envType == 0 || v.fromMidi)
                {
                    if (v.fenvAttack) { v.fenv += (1.05f - v.fenv) * 0.02f; if (v.fenv >= 1.0f) { v.fenv = 1.0f; v.fenvAttack = false; } }
                    else v.fenv *= fdecCoef;
                }
                else v.fenv = followEnv;
                const float envScale = (p.velocityMode == 2 && v.fromMidi) ? 0.3f + 0.7f * v.velocity : 1.0f;
                const float fc = baseCut * std::pow (2.0f, p.env * 6.0f * v.fenv * envScale);
                float s = v.filter.process (osc, p.filterType, fc, p.bandwidth, sr);

                if (ringMixSm > 0.001f)
                {
                    const float carrier = chaosSm > 0.3f ? chaosHz : (p.ringFollow ? mtof (v.current) * followRatio : fixedRingHz);
                    v.ringHz += (carrier - v.ringHz) * 0.01f;
                    v.ringPh += v.ringHz / sr; if (v.ringPh >= 1.0f) v.ringPh -= 1.0f;
                    s *= (1.0f - ringMixSm) + ringMixSm * std::sin (kTwoPi * v.ringPh);
                }
                synth += s * v.amp * vel;
            }
            synth *= voiceGain;

            // ---- Dry mode: your own signal, pitch shifted, compressed, filtered ----
            float wetL = synth, wetR = synth;
            if (dry)
            {
                if (dryEnvKick) { dryEnv = 1.0f; dryEnvKick = false; }
                dryEnv *= fdecCoef;
                const float denv = p.envType == 0 ? dryEnv : followEnv;
                const float fc = baseCut * std::pow (2.0f, p.env * 6.0f * denv);
                float a = compL.process (shiftL.process (inL, p.pitch), p.sustain);
                float b = compR.process (shiftR.process (inR, p.pitch), p.sustain);
                a = dryFilterL.process (a, p.filterType, fc, p.bandwidth, sr);
                b = dryFilterR.process (b, p.filterType, fc, p.bandwidth, sr);
                if (ringMixSm > 0.001f)
                {
                    const float carrier = chaosSm > 0.3f ? chaosHz : fixedRingHz;
                    dryRingPh += carrier / sr; if (dryRingPh >= 1.0f) dryRingPh -= 1.0f;
                    const float rm = (1.0f - ringMixSm) + ringMixSm * std::sin (kTwoPi * dryRingPh);
                    a *= rm; b *= rm;
                }
                wetL = a; wetR = b;
            }

            // ---- delay, mix, bypass ----
            bypassSm += ((p.bypass ? 1.0f : 0.0f) - bypassSm) * 0.003f;
            const float send = 1.0f - bypassSm;
            float eL = 0.0f, eR = 0.0f;
            echo.process (wetL * send, wetR * send, quarter, p.feedback, dry ? p.mod : 0.0f, p.runaway && ! p.bypass, eL, eR);
            const float echoGain = p.delayLevel * 0.8f;
            float outL = inL * dryGain + (wetL + eL * echoGain) * wetGain;
            float outR = inR * dryGain + (wetR + eR * echoGain) * wetGain;
            const float tails = p.trails ? (eL * echoGain * wetGain) : 0.0f;
            const float tailsR = p.trails ? (eR * echoGain * wetGain) : 0.0f;
            outL = outL * send + (inL + tails) * bypassSm;
            outR = outR * send + (inR + tailsR) * bypassSm;

            tunerSm += ((p.tuner ? 0.0f : 1.0f) - tunerSm) * 0.003f;
            L[i] = safety (outL * tunerSm);
            R[i] = safety (outR * tunerSm);
            wetPeak = std::max (wetPeak, std::abs (wetL) + std::abs (eL * echoGain));
        }

        // ---- what the panel and Pebble show ----
        int mask = 0, midiMask = 0, k = 0;
        for (auto& v : voices)
        {
            if (v.active && (v.gate || v.latched || v.sustained || v.amp > 0.05f))
            {
                mask |= 1 << k;
                if (v.fromMidi) midiMask |= 1 << k;
            }
            ++k;
        }
        uiVoiceMask.store (mask, std::memory_order_relaxed);
        uiMidiMask.store (midiMask, std::memory_order_relaxed);
        uiLevel.store (peakIn, std::memory_order_relaxed);
        uiWet.store (wetPeak, std::memory_order_relaxed);
        uiNote.store (mono.currentNote(), std::memory_order_relaxed);
        uiEstimate.store (mono.lastEstimate(), std::memory_order_relaxed);
        uiClarity.store (mono.currentClarity(), std::memory_order_relaxed);
        uiLockMs.store (mono.lockMs(), std::memory_order_relaxed);
        uiChaos.store (std::max (chaosSm, p.ring > 0.02f ? p.ring * 0.6f : 0.0f), std::memory_order_relaxed);
        float fenvMax = 0.0f;
        for (auto& v : voices) if (v.active) fenvMax = std::max (fenvMax, v.fenv);
        if (dry) fenvMax = p.envType == 0 ? dryEnv : followEnv;
        uiFilterEnv.store (fenvMax * p.env, std::memory_order_relaxed);
        uiLatched.store (anyLatched(), std::memory_order_relaxed);
    }

    // ---- what the panel and Pebble read (any thread) ----
    std::atomic<float> uiLevel { 0 }, uiWet { 0 }, uiNote { -1 }, uiEstimate { -1 }, uiClarity { 0 }, uiLockMs { 0 }, uiChaos { 0 }, uiFilterEnv { 0 };
    std::atomic<int> uiVoiceMask { 0 }, uiMidiMask { 0 }, uiArpStep { -1 }, uiOnsets { 0 }, uiArpSteps { 0 };
    std::atomic<bool> uiLatched { false };

    // for the render test
    int activeVoices() const { int c = 0; for (auto& v : voices) c += v.active && (v.gate || v.latched) ? 1 : 0; return c; }
    const MonoTracker& tracker() const { return mono; }
    bool polyHas (int midi) const { return poly.on (midi); }

private:
    struct PendingMidi { bool on = false; int note = 0; float vel = 0.0f; };
    static constexpr int kPending = 256;

    static float safety (float x)
    {
        const float a = std::abs (x);
        if (a < 0.85f) return x;
        const float y = 0.85f + 0.15f * softClip ((a - 0.85f) / 0.15f);
        return x < 0.0f ? -y : y;
    }

    void configure (const EngineParams& p)
    {
        MonoTracker::Settings s;
        s.range = p.input == InputBass ? MonoTracker::Bass : (p.input == InputSynth ? MonoTracker::Synth : MonoTracker::Guitar);
        static const float lows[3] = { 29.5f, 39.0f, 52.0f };
        s.lowestHz = lows[std::max (0, std::min (2, p.lowNote))];
        if (s.range == MonoTracker::Guitar) s.lowestHz = 62.0f;
        s.sensitivity = p.sensitivity; s.noteHold = p.noteHold; s.character = p.character; s.octaveGuard = p.octaveGuard;
        if (! configured || s.range != trackerSettings.range || s.lowestHz != trackerSettings.lowestHz || s.sensitivity != trackerSettings.sensitivity
            || s.noteHold != trackerSettings.noteHold || s.character != trackerSettings.character || s.octaveGuard != trackerSettings.octaveGuard)
        {
            trackerSettings = s;
            mono.configure (s);
            poly.setLowest ((int) std::floor (ftom (s.range == MonoTracker::Bass ? s.lowestHz : 62.0f)));
            configured = true;
        }
        poly.setSpeed (p.polySpeed, p.character);

        if (p.mode != prevMode)
        {
            // a new mode starts clean, but Sustain keeps what it holds
            for (auto& v : voices) if (! v.latched) { v.release(); v.sustained = false; }
            audioOn.fill (false);
            monoAudioVoice = monoMidiVoice = -1;
            arpRunning = false;
            uiArpStep.store (-1);
            arpPlayed.clear();
            for (int m : midiOrder) arpPlayed.push_back (m);
            prevMode = p.mode;
        }
        if (p.bypass && ! wasBypassed) for (auto& v : voices) if (! v.latched) v.release();
        wasBypassed = p.bypass;

        // Sustain latch (footswitch D tap): keep what's sounding, or catch the next notes
        if (p.latch && ! wasLatch)
        {
            bool any = false;
            for (auto& v : voices)
                if (v.active && v.gate && ! v.arp) { v.latched = true; v.key = -1; any = true; }
            catchUntil = any ? -1 : 0;
            catching = ! any;
        }
        if (! p.latch && wasLatch)
        {
            for (auto& v : voices) if (v.latched) { v.latched = false; v.release(); }
            catching = false;
            wasLatch = false;
            if (! holding()) prunePlayed();
        }
        wasLatch = p.latch;

        // Sustain while held (footswitch D hold, or the sustain pedal): like a piano pedal
        const bool hold = p.sustainHold || pedalDown;
        if (! hold && wasHold)
        {
            for (auto& v : voices) if (v.sustained) { v.sustained = false; v.release(); }
            wasHold = false;
            if (! wasLatch) prunePlayed();
        }
        wasHold = hold;
    }

    bool holding() const { return wasHold; }
    bool anyLatched() const { for (auto& v : voices) if (v.latched) return true; return false; }

    int allocate()
    {
        int best = -1; long long bestAge = 0; int pass = 0;
        for (pass = 0; pass < 3 && best < 0; ++pass)
            for (int i = 0; i < kVoices; ++i)
            {
                const auto& v = voices[(size_t) i];
                if (v.latched) continue;
                const bool ok = pass == 0 ? ! v.active : (pass == 1 ? ! v.gate && ! v.sustained : true);
                if (ok && (best < 0 || v.age < bestAge)) { best = i; bestAge = v.age; }
            }
        return best;
    }

    Voice* startVoice (int idx, int key, float midi, float vel, bool fromMidi, bool legato)
    {
        if (idx < 0) return nullptr;
        auto& v = voices[(size_t) idx];
        v.start (key, midi, vel, fromMidi, now, legato);
        v.arp = false;
        if (catching)
        {
            if (catchUntil < 0 || catchUntil == 0) catchUntil = now + (long long) (0.08f * sr);
            if (now <= catchUntil) { v.latched = true; v.key = -1; }
            else catching = false;
        }
        return &v;
    }

    void endVoice (Voice& v)
    {
        if (v.latched) return;
        if (holding()) { v.sustained = true; return; }
        v.release();
    }

    // ---- audio notes ----
    static constexpr int kMonoKey = 2000, kPolyKeyBase = 1000, kArpKey = 3000;

    void handleMonoEvent (MonoTracker::Event ev, const EngineParams& p)
    {
        if (p.mode != ModeMono) return;
        using E = MonoTracker::Event;
        const float pitch = mono.currentNote();
        if (ev == E::NoteOn)
        {
            if (monoAudioVoice >= 0 && voices[(size_t) monoAudioVoice].key != kMonoKey) monoAudioVoice = -1;
            if (monoAudioVoice < 0) monoAudioVoice = allocate();
            startVoice (monoAudioVoice, kMonoKey, pitch, 1.0f, false, false);
            if (monoAudioVoice >= 0 && voices[(size_t) monoAudioVoice].latched) monoAudioVoice = -1;
        }
        else if (ev == E::NoteChange)
        {
            if (monoAudioVoice >= 0 && voices[(size_t) monoAudioVoice].key == kMonoKey && voices[(size_t) monoAudioVoice].gate)
                voices[(size_t) monoAudioVoice].target = pitch;
        }
        else if (ev == E::NoteOff)
        {
            if (monoAudioVoice >= 0 && voices[(size_t) monoAudioVoice].key == kMonoKey) endVoice (voices[(size_t) monoAudioVoice]);
            monoAudioVoice = -1;
        }
    }

    void syncPolyNotes (const EngineParams& p)
    {
        std::array<bool, 128> want {};
        for (int m = PolyTracker::kLo; m <= PolyTracker::kHi; ++m) want[(size_t) m] = poly.on (m);
        const float mn = mono.currentNote();
        int monoKey = -1;
        if (mono.active() && mn > 0.0f)
        {
            monoKey = (int) std::lround (mn);
            if (monoKey >= 0 && monoKey < 128)
            {
                // the fast mono note leads; the bank's copies of it and its octave are the same note
                want[(size_t) monoKey] = true;
                for (int d : { -1, 1, 12, 19, 24 }) if (monoKey + d >= 0 && monoKey + d < 128 && ! audioOn[(size_t) (monoKey + d)]) want[(size_t) (monoKey + d)] = false;
            }
        }
        for (int m = 0; m < 128; ++m)
        {
            if (want[(size_t) m] == audioOn[(size_t) m]) continue;
            audioOn[(size_t) m] = want[(size_t) m];
            if (p.mode == ModeArp) { if (want[(size_t) m]) arpPlayed.push_back (m); else removePlayed (m); continue; }
            if (want[(size_t) m])
            {
                const float pitch = m == monoKey ? mn : (float) m;
                startVoice (allocate(), kPolyKeyBase + m, pitch, 1.0f, false, false);
            }
            else
                for (auto& v : voices) if (v.key == kPolyKeyBase + m && v.gate) endVoice (v);
        }
        if (p.mode == ModePoly && monoKey >= 0)
            for (auto& v : voices) if (v.key == kPolyKeyBase + monoKey && v.gate) v.target = mn;
    }

    void releaseAudioNotes()
    {
        bool any = false;
        for (auto b : audioOn) any = any || b;
        if (! any && monoAudioVoice < 0) return;
        audioOn.fill (false);
        for (auto& v : voices) if (! v.fromMidi && ! v.arp && v.gate) endVoice (v);
        monoAudioVoice = -1;
    }

    // ---- MIDI notes ----
    void applyPendingMidi (const EngineParams& p)
    {
        for (; pendingRead < pendingCount; ++pendingRead)
        {
            const auto m = pendingMidi[(size_t) (pendingRead % kPending)];
            if (p.source == SourceAudio || p.mode == ModeDry) continue;
            if (m.on)
            {
                midiHeld[(size_t) m.note] = true; midiVel[(size_t) m.note] = m.vel;
                removeOrder (m.note); midiOrder.push_back (m.note);
                if (p.mode == ModeArp) { arpPlayed.push_back (m.note); continue; }
                if (p.mode == ModeMono) { monoMidiUpdate (p, true); continue; }
                startVoice (allocate(), m.note, (float) m.note, m.vel, true, false);
            }
            else
            {
                midiHeld[(size_t) m.note] = false;
                removeOrder (m.note);
                if (p.mode == ModeArp) { removePlayed (m.note); continue; }
                if (p.mode == ModeMono) { monoMidiUpdate (p, false); continue; }
                for (auto& v : voices) if (v.fromMidi && v.key == m.note && v.gate) endVoice (v);
            }
        }
    }

    void monoMidiUpdate (const EngineParams& p, bool noteOnHappened)
    {
        if (midiOrder.empty())
        {
            if (monoMidiVoice >= 0 && voices[(size_t) monoMidiVoice].fromMidi) endVoice (voices[(size_t) monoMidiVoice]);
            monoMidiVoice = -1;
            return;
        }
        int note = midiOrder.back();
        if (p.midiMono == 1) for (int k : midiOrder) note = std::min (note, k);
        if (p.midiMono == 2) for (int k : midiOrder) note = std::max (note, k);
        auto* cur = monoMidiVoice >= 0 ? &voices[(size_t) monoMidiVoice] : nullptr;
        if (cur != nullptr && cur->fromMidi && cur->gate && ! cur->latched)
        {
            cur->target = (float) note; cur->key = note;  // legato: glide, no retrigger
            if (noteOnHappened) cur->velocity = midiVel[(size_t) note];
            return;
        }
        monoMidiVoice = allocate();
        startVoice (monoMidiVoice, note, (float) note, midiVel[(size_t) note], true, false);
        if (monoMidiVoice >= 0 && voices[(size_t) monoMidiVoice].latched) monoMidiVoice = -1;
    }

    void removeOrder (int note) { for (size_t i = 0; i < midiOrder.size(); ++i) if (midiOrder[i] == note) { midiOrder.erase (midiOrder.begin() + (long) i); return; } }
    void removePlayed (int note)
    {
        if (holding() || wasLatch) return;   // kept until Sustain lets go
        removePlayedNow (note);
    }
    void prunePlayed()
    {
        for (size_t i = arpPlayed.size(); i-- > 0;)
            if (! audioOn[(size_t) arpPlayed[i]] && ! midiHeld[(size_t) arpPlayed[i]]) arpPlayed.erase (arpPlayed.begin() + (long) i);
    }
    void removePlayedNow (int note) { for (size_t i = 0; i < arpPlayed.size(); ++i) if (arpPlayed[i] == note) { arpPlayed.erase (arpPlayed.begin() + (long) i); return; } }

    // ---- arpeggiator: the held chord as a pattern at the tapped tempo ----
    void runArp (const EngineParams& p, double stepSamples)
    {
        // what's held: notes from your instrument and from MIDI, in the order they arrived
        // Sustain (held or latched) keeps the pattern going after you let go
        const bool keep = holding() || wasLatch;
        int count = 0;
        std::array<int, 24> pool {};
        for (int m : arpPlayed) if (count < 24 && (keep || audioOn[(size_t) m] || midiHeld[(size_t) m])) pool[(size_t) count++] = m;
        if (count == 0)
        {
            arpRunning = false; arpIndex = 0; uiArpStep.store (-1, std::memory_order_relaxed);
            return;
        }

        if (! arpRunning) { arpRunning = true; arpCounter = stepSamples; arpIndex = 0; } // start on the beat you play
        arpCounter += 1.0;
        if (arpCounter < stepSamples) return;
        arpCounter -= stepSamples;

        std::array<int, 24> sorted = pool;
        std::sort (sorted.begin(), sorted.begin() + count);
        const int oct = std::max (1, std::min (3, p.arpOctaves));
        const int len = count * oct;
        auto noteAt = [&] (int i, const std::array<int, 24>& src) { return src[(size_t) (i % count)] + 12 * (i / count); };
        int note = 0;
        const int pat = p.arpPattern;
        if (pat == 0 || pat == 1) note = noteAt (arpIndex % len, sorted);
        else if (pat == 2) note = noteAt (len - 1 - arpIndex % len, sorted);
        else if (pat == 3)
        {
            const int cyc = std::max (1, 2 * len - 2);
            int k = arpIndex % cyc; if (k >= len) k = cyc - k;
            note = noteAt (k, sorted);
        }
        else if (pat == 4) note = noteAt (arpIndex % len, pool);
        else note = noteAt ((int) (rng.uni() * (float) len) % len, sorted);
        ++arpIndex;
        uiArpStep.store (arpIndex - 1, std::memory_order_relaxed);
        uiArpSteps.fetch_add (1, std::memory_order_relaxed);

        // one voice per step, round robin over the free ones
        int idx = -1;
        for (int k = 0; k < kVoices && idx < 0; ++k)
        {
            arpVoice = (arpVoice + 1) % kVoices;
            if (! voices[(size_t) arpVoice].latched) idx = arpVoice;
        }
        if (idx < 0) return;
        const float vel = midiHeld[(size_t) std::min (127, std::max (0, note % 128))] ? midiVel[(size_t) (note % 128)] : 1.0f;
        auto* v = startVoice (idx, kArpKey + note, (float) note, vel, false, false);
        if (v != nullptr)
        {
            v->arp = true;
            v->offAt = (double) now + stepSamples * (double) (0.08f + 0.9f * p.arpGate);
        }
    }

    float sr = 48000.0f;
    MonoTracker mono;
    MonoTracker::Settings trackerSettings;
    bool configured = false;
    PolyTracker poly;
    std::array<Voice, kVoices> voices;
    EchoDelay echo;
    Shifter shiftL, shiftR;
    Compressor compL, compR;
    Filter dryFilterL, dryFilterR;
    Rng rng;

    std::array<bool, 128> audioOn {}, midiHeld {};
    std::array<float, 128> midiVel {};
    std::vector<int> midiOrder, arpPlayed;
    std::array<PendingMidi, kPending> pendingMidi {};
    unsigned pendingCount = 0, pendingRead = 0;

    int monoAudioVoice = -1, monoMidiVoice = -1, prevMode = -1, arpVoice = 0;
    long long now = 0, catchUntil = 0;
    bool echoCleared = false, catching = false, wasLatch = false, wasHold = false, wasBypassed = false, pedalDown = false, arpRunning = false, dryEnvKick = false;
    double arpCounter = 0.0;
    int arpIndex = 0, chaosCountdown = 0;
    float bend = 0.0f, wheel = 0.0f, followEnv = 0.0f, bypassSm = 0.0f, wetSm = 0.0f, ringMixSm = 0.0f, chaosSm = 0.0f, chaosHz = 200.0f;
    float voiceGain = 0.34f, tunerSm = 1.0f, attackCoef = 0.0f, dryEnv = 0.0f, dryRingPh = 0.0f;
};
} // namespace chg
