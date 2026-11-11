#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace ParamIDs
{
// the six Enzo knobs and their alt jobs
inline constexpr auto pitch = "pitch";
inline constexpr auto glide = "glide";             // alt of Pitch
inline constexpr auto cutoff = "cutoff";
inline constexpr auto filterType = "filterType";   // alt of Filter
inline constexpr auto mix = "mix";
inline constexpr auto delayLevel = "delayLevel";   // alt of Mix
inline constexpr auto sustain = "sustain";
inline constexpr auto ring = "ring";               // alt of Sustain
inline constexpr auto env = "env";
inline constexpr auto bandwidth = "bandwidth";     // alt of Filter Env
inline constexpr auto mod = "mod";
inline constexpr auto feedback = "feedback";       // alt of Modulation
// mode and switches
inline constexpr auto mode = "mode";
inline constexpr auto wave = "wave";               // alt of Bypass
inline constexpr auto envType = "envType";         // alt of Tap
inline constexpr auto bypass = "bypass";
inline constexpr auto trails = "trails";
inline constexpr auto latch = "latch";
inline constexpr auto input = "input";
inline constexpr auto source = "source";
inline constexpr auto bpm = "bpm";
inline constexpr auto sync = "sync";
// deep page
inline constexpr auto osc2Interval = "osc2Interval";
inline constexpr auto osc2Wave = "osc2Wave";
inline constexpr auto osc2Level = "osc2Level";
inline constexpr auto osc2Detune = "osc2Detune";
inline constexpr auto tunerRef = "tunerRef";
inline constexpr auto character = "character";
inline constexpr auto lowNote = "lowNote";
inline constexpr auto sensitivity = "sensitivity";
inline constexpr auto noteHold = "noteHold";
inline constexpr auto polySpeed = "polySpeed";
inline constexpr auto octaveGuard = "octaveGuard";
inline constexpr auto arpPattern = "arpPattern";
inline constexpr auto arpStep = "arpStep";
inline constexpr auto arpOctaves = "arpOctaves";
inline constexpr auto arpGate = "arpGate";
inline constexpr auto ringFollow = "ringFollow";
inline constexpr auto velocity = "velocity";
inline constexpr auto bendRange = "bendRange";
inline constexpr auto midiMono = "midiMono";
// pedal page
inline constexpr auto expTarget = "expTarget";
inline constexpr auto keeper = "keeper";
} // namespace ParamIDs

namespace names
{
inline const juce::StringArray modes { "Poly", "Mono", "Arp", "Dry" };
inline const juce::StringArray modeLines { "UP TO 6 VOICES", "ONE VOICE, TWO OSCS", "CHORD TO PATTERN", "SYNTH OFF" };
inline const juce::StringArray filterTypes { "Ladder low-pass", "Ladder band-pass", "Ladder high-pass", "SV low-pass", "SV band-pass", "SV high-pass" };

struct ExpTarget { const char* id; const char* label; };
inline const std::vector<ExpTarget>& expTargets()
{
    static const std::vector<ExpTarget> t {
        { ParamIDs::cutoff, "Filter cutoff" }, { ParamIDs::env, "Filter env" }, { ParamIDs::bandwidth, "Bandwidth" },
        { ParamIDs::mix, "Mix" }, { ParamIDs::sustain, "Sustain" }, { ParamIDs::ring, "Ring mod" },
        { ParamIDs::mod, "Modulation" }, { ParamIDs::glide, "Glide" }, { ParamIDs::pitch, "Pitch" },
        { ParamIDs::delayLevel, "Delay level" }, { ParamIDs::feedback, "Feedback" }, { "morph", "Morph" }
    };
    return t;
}
inline juce::StringArray expTargetNames()
{
    juce::StringArray s;
    for (const auto& t : expTargets()) s.add (t.label);
    return s;
}
inline int morphTarget() { return (int) expTargets().size() - 1; }

// the knobs Morph sweeps between its heel and toe snapshots, as on the original
inline const juce::StringArray morphKnobs { ParamIDs::pitch, ParamIDs::glide, ParamIDs::cutoff, ParamIDs::env, ParamIDs::bandwidth,
                                            ParamIDs::mix, ParamIDs::delayLevel, ParamIDs::sustain, ParamIDs::ring,
                                            ParamIDs::mod, ParamIDs::feedback };
} // namespace names

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; };
    auto knob = [&] (const char* id, const char* name, float def, std::function<String (float, int)> text = {})
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def,
                                                            AudioParameterFloatAttributes().withStringFromValueFunction (text ? text : pct)));
    };
    auto choice = [&] (const char* id, const char* name, const StringArray& opts, int def)
    {
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, opts, def));
    };
    auto toggle = [&] (const char* id, const char* name, bool def)
    {
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };

    // ---- the six knobs and their alt jobs ----
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::pitch, 1 }, "Pitch", -24, 24, 0,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return (v > 0 ? "+" : "") + String (v) + " st"; })));
    knob (ParamIDs::glide, "Glide", 0.15f, [] (float v, int) { return String (roundToInt (2 + v * v * 2000.0f)) + " ms"; });
    knob (ParamIDs::cutoff, "Filter", 0.55f, [] (float v, int)
    {
        const float hz = 40.0f * std::pow (400.0f, v);
        return hz < 1000.0f ? String (roundToInt (hz)) + " Hz" : String (hz / 1000.0f, 1) + " kHz";
    });
    choice (ParamIDs::filterType, "Filter Type", names::filterTypes, 0);
    knob (ParamIDs::mix, "Mix", 0.6f);
    knob (ParamIDs::delayLevel, "Delay Level", 0.3f, [] (float v, int) { return v < 0.02f ? String ("Off") : (v < 0.98f ? "Min " + String (roundToInt (v * 100.0f)) + "%" : String ("Mid")); });
    knob (ParamIDs::sustain, "Sustain", 0.75f);
    knob (ParamIDs::ring, "Ring Mod", 0.0f, [] (float v, int) { return v < 0.02f ? String ("Off") : String (roundToInt (20.0f * std::pow (150.0f, v))) + " Hz"; });
    knob (ParamIDs::env, "Filter Env", 0.5f);
    knob (ParamIDs::bandwidth, "Bandwidth", 0.3f, [] (float v, int) { return v < 0.33f ? String ("Wide") : (v < 0.66f ? String ("Medium") : String ("Peaky")); });
    knob (ParamIDs::mod, "Modulation", 0.2f, [] (float v, int) { return v < 0.01f ? String ("Unison") : String (roundToInt (v * 50.0f)) + " cents"; });
    knob (ParamIDs::feedback, "Delay Feedback", 0.35f);

    // ---- modes and switches ----
    choice (ParamIDs::mode, "Synth Mode", names::modes, 1);
    choice (ParamIDs::wave, "Waveshape", { "Saw", "Square" }, 0);
    choice (ParamIDs::envType, "Envelope Type", { "Trigger", "Follow" }, 0);
    toggle (ParamIDs::bypass, "Bypass", false);
    toggle (ParamIDs::trails, "Trails", true);
    toggle (ParamIDs::latch, "Sustain Latch", false);
    choice (ParamIDs::input, "Input", { "Guitar", "Bass", "Synth" }, 0);
    choice (ParamIDs::source, "Source", { "Audio", "MIDI", "Both" }, 2);
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::bpm, 1 }, "Tempo", NormalisableRange<float> (30.0f, 960.0f, 0.1f, 0.5f), 120.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " BPM"; })));
    toggle (ParamIDs::sync, "Sync to Host", false);

    // ---- deep page ----
    choice (ParamIDs::osc2Interval, "Osc 2 Interval", { "Unison", "-12", "+7", "+12", "+19" }, 0);
    choice (ParamIDs::osc2Wave, "Osc 2 Wave", { "Same", "Saw", "Square", "Pulse", "Sine" }, 0);
    knob (ParamIDs::osc2Level, "Osc 2 Level", 0.7f);
    knob (ParamIDs::osc2Detune, "Osc 2 Detune", 0.0f, [] (float v, int) { return String (roundToInt (v * 50.0f)) + " cents"; });
    choice (ParamIDs::tunerRef, "Tuner Reference", { "432", "440", "442" }, 1);
    knob (ParamIDs::character, "Character", 0.15f, [] (float v, int) { return v < 0.3f ? String ("Tight") : (v < 0.6f ? String ("Loose") : String ("Wild")); });
    choice (ParamIDs::lowNote, "Lowest Note", { "Low B", "Low E", "Low A" }, 0);
    knob (ParamIDs::sensitivity, "Sensitivity", 0.5f);
    knob (ParamIDs::noteHold, "Note Hold", 0.5f);
    knob (ParamIDs::polySpeed, "Poly Speed", 0.5f, [] (float v, int) { return v < 0.5f ? String ("Careful") : String ("Quick"); });
    toggle (ParamIDs::octaveGuard, "Octave Guard", true);
    choice (ParamIDs::arpPattern, "Arp Pattern", { "Original", "Up", "Down", "Bounce", "Played", "Random" }, 0);
    choice (ParamIDs::arpStep, "Arp Step", { "1/4", "1/8", "1/8T", "1/16" }, 0);
    choice (ParamIDs::arpOctaves, "Arp Octaves", { "1", "2", "3" }, 0);
    knob (ParamIDs::arpGate, "Arp Gate", 0.6f);
    choice (ParamIDs::ringFollow, "Ring Carrier", { "Fixed", "Follow" }, 0);
    choice (ParamIDs::velocity, "Velocity", { "Off", "Level", "Filter" }, 2);
    choice (ParamIDs::bendRange, "Pitch Bend", { "2", "7", "12" }, 0);
    choice (ParamIDs::midiMono, "MIDI Mono", { "Last", "Low", "High" }, 0);

    // ---- pedal page ----
    choice (ParamIDs::expTarget, "Expression Target", names::expTargetNames(), 0);
    toggle (ParamIDs::keeper, "Keeper", false);

    return { p.begin(), p.end() };
}
