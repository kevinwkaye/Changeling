// Footswitch and expression test. Drives the real processor with plain 127/0 control
// changes on channel 16, as the footswitch sends them, and checks every tap, hold and
// combo does its job and nothing else. Also checks the original Enzo MIDI map never
// collides with the footswitches, whether it is off or on another channel.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>

using namespace juce;

static int failures = 0;
static void check (bool ok, const String& what)
{
    std::printf ("%s %s\n", ok ? "  ok  " : "  FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

struct Rig
{
    ChangelingProcessor proc;
    AudioBuffer<float> buffer { 2, 256 };

    Rig()
    {
        proc.pedals() = PedalSettings();   // defaults, whatever is saved on this computer
        proc.pedals().footDevice = "none";
        proc.prepareToPlay (48000.0, 256);
        proc.setPlayConfigDetails (2, 2, 48000.0, 256);
    }

    void block (MidiBuffer& midi)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);
    }
    void cc (int number, int value, int channel = 16)
    {
        MidiBuffer m;
        m.addEvent (MidiMessage::controllerEvent (channel, number, value), 0);
        block (m);
    }
    void note (int number, int channel)
    {
        MidiBuffer m;
        m.addEvent (MidiMessage::noteOn (channel, number, (uint8) 100), 0);
        block (m);
    }
    void pump (int ms) { MessageManager::getInstance()->runDispatchLoopUntil (ms); }
    void tapSwitch (int cc20to23) { cc (cc20to23, 127); pump (60); cc (cc20to23, 0); pump (60); }
    void holdSwitch (int number, int ms) { cc (number, 127); pump (ms); }
    void releaseSwitch (int number) { cc (number, 0); pump (60); }

    float value (const char* id) { auto* p = proc.apvts.getParameter (id); return p->convertFrom0to1 (p->getValue()); }

    std::map<String, float> snapshot()
    {
        std::map<String, float> m;
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<RangedAudioParameter*> (p)) m[rp->getParameterID()] = rp->getValue();
        return m;
    }
    StringArray changedSince (const std::map<String, float>& before)
    {
        StringArray out;
        for (auto& [id, v] : snapshot()) if (std::abs (before.at (id) - v) > 1.0e-6f) out.add (id);
        return out;
    }
};

static bool only (const StringArray& changed, const StringArray& allowed)
{
    for (auto& c : changed) if (! allowed.contains (c)) return false;
    return true;
}

// changeling_foot_test --snapshot <folder> writes the three panel pages as PNGs
static int snapshot (const File& dir)
{
    Rig r;
    dir.createDirectory();
    std::unique_ptr<AudioProcessorEditor> ed (r.proc.createEditor());
    auto* editor = dynamic_cast<ChangelingEditor*> (ed.get());
    // something to look at: a held chord from the keyboard and a plucked note
    r.proc.apvts.getParameter (ParamIDs::mode)->setValueNotifyingHost (0.0f);
    for (int k = 0; k < 40; ++k)
    {
        MidiBuffer m;
        if (k == 0) for (int n : { 48, 52, 55 }) m.addEvent (MidiMessage::noteOn (1, n, (uint8) 100), 0);
        r.buffer.clear();
        for (int i = 0; i < 256; ++i) r.buffer.setSample (0, i, k > 4 && k < 20 ? 0.3f * std::sin ((float) (k * 256 + i) * 0.0054f) : 0.0f);
        r.buffer.copyFrom (1, 0, r.buffer, 0, 0, 256);
        r.proc.processBlock (r.buffer, m);
    }
    for (int pg = 0; pg < 3; ++pg)
    {
        editor->showPage (pg);
        for (int i = 0; i < 30; ++i) { editor->tickForSnapshot(); r.pump (5); }
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        static const char* names[3] = { "main", "deep", "pedal" };
        FileOutputStream out (dir.getChildFile (String (names[pg]) + ".png"));
        out.setPosition (0); out.truncate();
        PNGImageFormat().writeImageToStream (img, out);
    }
    std::printf ("Snapshots written to %s\n", dir.getFullPathName().toRawUTF8());
    return 0;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI juce;
    if (argc > 2 && String (argv[1]) == "--snapshot") return snapshot (File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    Rig r;
    using namespace ParamIDs;

    std::printf ("Taps\n");
    {
        auto before = r.snapshot();
        r.tapSwitch (20);
        check (r.value (bypass) > 0.5f, "A tap bypasses");
        check (only (r.changedSince (before), { bypass }), "A tap changes nothing else: " + r.changedSince (before).joinIntoString (", "));
        r.tapSwitch (20);
        check (r.value (bypass) < 0.5f, "A tap again turns it back on");

        before = r.snapshot();
        r.tapSwitch (21); r.pump (340); r.tapSwitch (21);
        const float bpmNow = r.value (ParamIDs::bpm);
        check (bpmNow > 90.0f && bpmNow < 160.0f, "B taps set the tempo (" + String (bpmNow, 1) + " BPM)");
        check (only (r.changedSince (before), { ParamIDs::bpm, ParamIDs::sync }), "B taps change only the tempo");

        before = r.snapshot();
        const int m0 = (int) r.value (mode);
        r.tapSwitch (22);
        check ((int) r.value (mode) == (m0 + 1) % 4, "C tap steps the synth mode");
        check (only (r.changedSince (before), { mode }), "C tap changes only the mode");
        r.tapSwitch (22); r.tapSwitch (22); r.tapSwitch (22);
        check ((int) r.value (mode) == m0, "four C taps come back round to the same mode");

        before = r.snapshot();
        r.tapSwitch (23);
        check (r.value (latch) > 0.5f, "D tap latches sustain");
        check (only (r.changedSince (before), { latch }), "D tap changes only the latch");
        r.tapSwitch (23);
        check (r.value (latch) < 0.5f, "D tap again releases it");
    }

    std::printf ("Holds\n");
    {
        auto before = r.snapshot();
        const float w0 = r.value (wave);
        r.holdSwitch (20, 600); r.releaseSwitch (20);
        check (r.value (wave) != w0, "A hold flips saw and square");
        check (r.value (bypass) < 0.5f && only (r.changedSince (before), { wave }), "A hold doesn't bypass");

        r.holdSwitch (21, 600);
        check (r.proc.runawayOn(), "B hold runs the delay away");
        r.releaseSwitch (21);
        check (! r.proc.runawayOn(), "letting go of B stops it");

        before = r.snapshot();
        r.holdSwitch (22, 600);
        check (r.proc.chaosOn(), "C hold throws in ring chaos");
        r.releaseSwitch (22);
        check (! r.proc.chaosOn() && r.changedSince (before).isEmpty(), "letting go of C ends it, mode unchanged");

        r.holdSwitch (23, 600);
        check (r.proc.sustainHeld(), "D hold sustains while held");
        r.releaseSwitch (23);
        check (! r.proc.sustainHeld() && r.value (latch) < 0.5f, "letting go of D releases, latch untouched");
    }

    std::printf ("Combos\n");
    {
        auto before = r.snapshot();
        r.cc (20, 127); r.pump (30); r.cc (21, 127); r.pump (80); r.cc (20, 0); r.cc (21, 0); r.pump (80);
        check (r.proc.selectedPreset() == 0, "A+B steps to the next preset");
        check (r.value (bypass) < 0.5f, "A+B doesn't bypass or tap");

        before = r.snapshot();
        const int keeps = r.proc.uiKeepPulse.load();
        r.cc (22, 127); r.pump (30); r.cc (23, 127); r.pump (80); r.cc (22, 0); r.cc (23, 0); r.pump (80);
        check (r.changedSince (before).isEmpty() && r.proc.uiKeepPulse.load() == keeps, "C+D tap only hands the footswitches on");

        r.cc (22, 127); r.pump (30); r.cc (23, 127); r.pump (1250);
        check (r.proc.uiKeepPulse.load() == keeps + 1, "C+D held 1 second saves a Keeper");
        check (! r.proc.chaosOn() && ! r.proc.sustainHeld(), "C+D hold never fires ring chaos or sustain");
        r.cc (22, 0); r.cc (23, 0); r.pump (80);
        check (r.changedSince (before).isEmpty(), "C+D hold changes no settings");
    }

    std::printf ("Expression\n");
    {
        auto before = r.snapshot();
        r.cc (11, 127); r.pump (40);
        check (r.value (cutoff) > 0.99f, "toe opens the filter cutoff");
        r.cc (11, 0); r.pump (40);
        check (r.value (cutoff) < 0.01f, "heel closes it");
        check (only (r.changedSince (before), { cutoff }), "expression moves only the cutoff");
    }

    std::printf ("Clashes with the original Enzo MIDI map\n");
    for (int mapChannel : { 0, 1 })
    {
        r.proc.pedals().pedalMapChannel = mapChannel;
        const String when = mapChannel == 0 ? "map off" : "map on channel 1";
        auto before = r.snapshot();
        r.cc (20, 127); r.cc (20, 0); r.cc (21, 127); r.cc (21, 0); r.pump (60);
        r.cc (22, 127); r.cc (22, 0); r.cc (23, 127); r.cc (23, 0); r.pump (60);
        r.cc (11, 64); r.pump (60);
        const auto changed = r.changedSince (before);
        check (only (changed, { bypass, ParamIDs::bpm, ParamIDs::sync, mode, latch, cutoff }),
               when + ": CC 20 to 23 and 11 on channel 16 do only the footswitch jobs (" + changed.joinIntoString (", ") + ")");
        check (! changed.contains (env) && ! changed.contains (mod) && ! changed.contains (glide) && ! changed.contains (filterType),
               when + ": the Enzo's CC 20 to 23 knobs stay put");
        // put things back
        r.tapSwitch (20); r.tapSwitch (22); r.tapSwitch (22); r.tapSwitch (22); r.tapSwitch (23);
    }
    {
        r.proc.pedals().pedalMapChannel = 1;
        auto before = r.snapshot();
        r.cc (17, 0, 1); r.pump (40);
        check (r.value (cutoff) < 0.01f, "the map still works on its own channel (CC 17 filter)");
        r.proc.pedals().pedalMapChannel = 0;
    }

    std::printf ("Keyboard\n");
    {
        r.proc.apvts.getParameter (mode)->setValueNotifyingHost (r.proc.apvts.getParameter (mode)->convertTo0to1 (0.0f));
        r.pump (20);
        r.note (60, 16);
        check (r.proc.getEngine().activeVoices() >= 1, "a note on channel 16 still plays the synth");
    }

    std::printf (failures == 0 ? "All passed\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
