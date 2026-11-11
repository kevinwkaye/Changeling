#pragma once

#include "Params.h"
#include "PedalControl.h"
#include "Keeper.h"
#include "engine/ChangelingEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

class ChangelingProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int kPresets = 16;          // red, yellow, green and blue banks of four
    static constexpr int kPerBank = 4;

    ChangelingProcessor();
    ~ChangelingProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Changeling"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    chg::ChangelingEngine& getEngine() { return engine; }

    // ---- called from the panel (message thread) ----
    void footDown (int sw);
    void footUp (int sw);
    void tap();
    void nextMode (int direction = 1);

    void savePreset (int slot);
    void loadPreset (int slot);
    bool hasPreset (int slot) const;
    juce::String presetName (int slot) const;
    void stepPreset (int direction);
    int selectedPreset() const { return selected; }

    // footswitch and expression setup
    PedalSettings& pedals() { return settings; }
    void savePedals();
    void learnFootswitch (int sw);
    void learnExpression();
    void calibrateExpression();
    void resetFootswitches();
    void setMorphEnd (bool toe);              // Morph: snapshot every knob for heel or toe
    bool morphReady() const { return morphHeel.isValid() && morphToe.isValid(); }

    // Keeper: log this moment, the last 8 seconds of audio included
    void keepNow();
    juce::File exportKeepers();
    void openKeepersFolder();
    void setKeeperLive (bool live);

    // footswitch MIDI read straight from the device, so every copy works without a MIDI track
    void handleDirectMidi (const juce::MidiMessage&);   // MIDI thread
    bool pedalsDirect() const { return direct->connected(); }
    juce::String footInputStatus() const { return direct->status(); }
    juce::String footDevice() const { return settings.footDevice; }
    void setFootDevice (const juce::String& device);
    juce::StringArray footDeviceNames() const { return DirectMidiInput::deviceNames(); }
    std::atomic<bool> hostFeedsPedals { false };
    bool isLearning() const { return learnTarget != LearnNone; }
    int learningWhat() const { return learnTarget; }
    bool isCalibrating() const { return calibrating; }
    enum { LearnNone = -1, LearnExpression = 4 };

    // momentary footswitch jobs, also on screen
    void setRunaway (bool on) { runaway.store (on); }
    void setChaos (bool on) { chaos.store (on); }
    void setSustainHold (bool on) { sustainHold.store (on); }
    bool runawayOn() const { return runaway.load(); }
    bool chaosOn() const { return chaos.load(); }
    bool sustainHeld() const { return sustainHold.load(); }
    void setTuner (bool on) { tunerOn.store (on); }
    bool tunerOpen() const { return tunerOn.load(); }

    // ---- what the panel and Pebble show ----
    float currentBpm() const { return uiBpm.load(); }
    bool hostSynced() const { return uiSynced.load(); }
    float expressionLevel() const { return uiExp.load(); }
    double lastExpressionMove() const { return expMovedAt; }
    std::atomic<int> uiFootPulse { 0 };       // counts footswitch presses, Pebble taps its foot
    std::atomic<int> uiFootDown { 0 };        // bitmask of footswitches held now
    std::atomic<int> uiKeepPulse { 0 };
    juce::String uiMessage;                   // a short line for the footswitch status
    double uiMessageAt = 0.0;
    int lastFootSwitch() const { return lastFoot; }

private:
    void timerCallback() override;
    chg::EngineParams readParams();
    void handleMidi (const juce::MidiMessage&, bool fromDevice);
    void onFootEvent (int sw, FootswitchLogic::Event);
    void setNorm (const juce::String& id, float v01);
    void setPlain (const juce::String& id, float plain);
    void say (const juce::String& s);
    void applyExpression (float v);

    juce::ValueTree snapshotOf();
    void applySnapshot (const juce::ValueTree&);
    static bool inSnapshot (const juce::String& id);
    void makeFactoryPresets();
    juce::ValueTree knobSnapshot();

    chg::ChangelingEngine engine;
    Keeper keeper;

    std::map<juce::String, std::atomic<float>*> raw;
    float f (const char* id) const { return raw.at (id)->load (std::memory_order_relaxed); }
    bool on (const char* id) const { return f (id) > 0.5f; }
    int idx (const char* id) const { return (int) std::lround (f (id)); }

    // MIDI handed to the message thread
    struct Pending { int kind = 0; int a = 0; float v = 0; const char* id = nullptr; };
    enum PendingKind { FootPress, FootRelease, Expression, SetNorm, SetPlain, LoadPreset, Tap, Learned, NextMode };
    juce::AbstractFifo fifo { 512 };
    std::array<Pending, 512> pending;
    juce::AbstractFifo directFifo { 512 };    // the same, written from the MIDI thread
    std::array<Pending, 512> directPending;
    juce::SpinLock directWriteLock;
    juce::SharedResourcePointer<DirectMidiInput> direct;
    juce::Time settingsStamp;
    bool wasDirect = false;
    double nextSettingsCheck = 0.0;
    void queue (int kind, int a, float v);
    void queueParam (int kind, const char* id, float v);

    std::array<juce::ValueTree, kPresets> slots;
    int selected = -1;
    juce::ValueTree morphHeel, morphToe;

    PedalSettings settings;
    FootswitchLogic footswitches;
    std::atomic<int> learnTarget { LearnNone };
    bool calibrating = false;
    double calibrateUntil = 0.0;
    int calLo = 127, calHi = 0, calFirst = -1;
    double expMovedAt = -100.0;
    int lastFoot = -1;

    std::atomic<bool> runaway { false }, chaos { false }, sustainHold { false }, tunerOn { false };
    std::atomic<float> uiBpm { 120.0f }, uiExp { 0.0f };
    std::atomic<bool> uiSynced { false };
    std::vector<double> taps;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChangelingProcessor)
};
