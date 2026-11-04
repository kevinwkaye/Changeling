#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

// Several Changeling windows can be open at once. Footswitches drive one of them at a
// time: the one whose panel was last touched, or the next one along when C+D are tapped.
namespace
{
SpinLock focusLock;
Array<ChangelingProcessor*> instances;
std::atomic<ChangelingProcessor*> focused { nullptr };

bool hasFocus (ChangelingProcessor* p)
{
    auto* f = focused.load();
    return f == nullptr || f == p;
}

// set while a message from the footswitch device is being handled on the MIDI thread
thread_local bool fromDeviceNow = false;

// footswitch device messages go to the copy that is learning, else the copy in charge
void sendToFocused (const MidiMessage& m)
{
    const SpinLock::ScopedLockType l (focusLock);
    ChangelingProcessor* target = nullptr;
    for (auto* p : instances) if (p->isLearning()) target = p;
    if (target == nullptr) target = focused.load();
    if (target == nullptr && ! instances.isEmpty()) target = instances.getLast();
    if (target != nullptr) target->handleDirectMidi (m);
}

double nowSeconds() { return Time::getMillisecondCounterHiRes() * 0.001; }
} // namespace

ChangelingProcessor::ChangelingProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ChangelingState", createParameterLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            raw[rp->getParameterID()] = apvts.getRawParameterValue (rp->getParameterID());
    settings.load();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    direct->setSink (&sendToFocused);
    direct->setWanted (settings.footDevice);
    footswitches.onEvent = [this] (int sw, FootswitchLogic::Event e) { onFootEvent (sw, e); };
    makeFactoryPresets();
    {
        const SpinLock::ScopedLockType l (focusLock);
        instances.add (this);
    }
    focused.store (this);
    startTimerHz (100);
}

ChangelingProcessor::~ChangelingProcessor()
{
    stopTimer();
    const SpinLock::ScopedLockType l (focusLock);
    instances.removeAllInstancesOf (this);
    if (focused.load() == this) focused.store (instances.isEmpty() ? nullptr : instances.getLast());
}

void ChangelingProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    keeper.prepare (sampleRate);
}

bool ChangelingProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != AudioChannelSet::stereo()) return false;
    return in == AudioChannelSet::stereo() || in == AudioChannelSet::mono();
}

chg::EngineParams ChangelingProcessor::readParams()
{
    using namespace ParamIDs;
    chg::EngineParams p;
    p.mode = idx (mode); p.wave = idx (wave);
    p.pitch = f (pitch); p.glide = f (glide);
    p.cutoff = f (cutoff); p.env = f (env); p.bandwidth = f (bandwidth);
    p.filterType = idx (filterType); p.envType = idx (envType);
    p.sustain = f (sustain); p.ring = f (ring); p.mod = f (mod); p.mix = f (mix);
    p.delayLevel = f (delayLevel); p.feedback = f (feedback);
    p.trails = on (trails); p.bypass = on (bypass);
    p.input = idx (input); p.source = idx (source);
    p.osc2Interval = idx (osc2Interval); p.osc2Wave = idx (osc2Wave);
    p.osc2Level = f (osc2Level); p.osc2Detune = f (osc2Detune);
    p.character = f (character); p.lowNote = idx (lowNote);
    p.sensitivity = f (sensitivity); p.noteHold = f (noteHold); p.polySpeed = f (polySpeed);
    p.octaveGuard = on (octaveGuard);
    p.arpPattern = idx (arpPattern); p.arpStep = idx (arpStep); p.arpOctaves = idx (arpOctaves) + 1; p.arpGate = f (arpGate);
    p.ringFollow = idx (ringFollow) == 1; p.velocityMode = idx (velocity); p.bendRange = idx (bendRange); p.midiMono = idx (midiMono);
    p.latch = on (latch);
    p.sustainHold = sustainHold.load(); p.runaway = runaway.load(); p.chaos = chaos.load(); p.tuner = tunerOn.load();

    // tempo: tapped, or Ableton's when Sync is on
    float bpmNow = f (ParamIDs::bpm);
    bool synced = false;
    if (on (ParamIDs::sync))
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm()) { bpmNow = (float) *b; synced = true; }
    p.bpm = bpmNow;
    uiBpm.store (bpmNow);
    uiSynced.store (synced);
    return p;
}

void ChangelingProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || getTotalNumOutputChannels() < 2) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);

    for (const auto meta : midi) handleMidi (meta.getMessage(), false);
    midi.clear();

    const auto p = readParams();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    keeper.pushInput (L, R, n);
    engine.process (L, R, n, p);
    keeper.pushOutput (L, R, n);
}

// =====================================================================================
// MIDI: keyboard notes, footswitches, expression, presets and the Enzo's own CC chart
// =====================================================================================
void ChangelingProcessor::queue (int kind, int a, float v)
{
    if (fromDeviceNow)
    {
        const auto scope = directFifo.write (1);
        if (scope.blockSize1 > 0) directPending[(size_t) scope.startIndex1] = { kind, a, v, nullptr };
        return;
    }
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0) pending[(size_t) scope.startIndex1] = { kind, a, v, nullptr };
}

void ChangelingProcessor::queueParam (int kind, const char* id, float v)
{
    if (fromDeviceNow)
    {
        const auto scope = directFifo.write (1);
        if (scope.blockSize1 > 0) directPending[(size_t) scope.startIndex1] = { kind, 0, v, id };
        return;
    }
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0) pending[(size_t) scope.startIndex1] = { kind, 0, v, id };
}

void ChangelingProcessor::handleDirectMidi (const MidiMessage& m)
{
    // several MIDI devices can call in on their own threads
    const SpinLock::ScopedLockType l (directWriteLock);
    fromDeviceNow = true;
    handleMidi (m, true);
    fromDeviceNow = false;
}

void ChangelingProcessor::setFootDevice (const String& device)
{
    settings.footDevice = device;
    savePedals();
    direct->setWanted (device);
}

void ChangelingProcessor::savePedals()
{
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
}

void ChangelingProcessor::handleMidi (const MidiMessage& m, bool fromDevice)
{
    const int ch = m.getChannel();

    // the keyboard: notes, bend, mod wheel and sustain pedal play the synth (audio thread only)
    if (! fromDevice)
    {
        if (m.isNoteOn()) { engine.noteOn (m.getNoteNumber(), m.getFloatVelocity()); return; }
        if (m.isNoteOff()) { engine.noteOff (m.getNoteNumber()); return; }
        if (m.isPitchWheel()) { engine.pitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f); return; }
        if (m.isAllNotesOff() || m.isAllSoundOff()) { engine.allNotesOff(); return; }
    }
    if (m.isProgramChange())
    {
        if ((ch == settings.pedalMapChannel || ch == settings.fsChannel[0]) && (fromDevice || hasFocus (this)))
            queue (LoadPreset, m.getProgramChangeNumber(), 0.0f);
        return;
    }
    if (! m.isController()) return;
    const int cc = m.getControllerNumber(), val = m.getControllerValue();

    if (learnTarget.load() != LearnNone)
    {
        if (val > 0) queue (Learned, ch * 128 + cc, 0.0f);
        return;
    }

    // Footswitches and expression are offered to the pedal code first. Straight from the
    // footswitch's own cable any channel will do; through Ableton the channel must match.
    if (fromDevice || ! pedalsDirect())
    {
        int foot = -1;
        for (int k = 0; k < 4 && foot < 0; ++k)
            if ((fromDevice || ch == settings.fsChannel[(size_t) k]) && cc == settings.fsCC[(size_t) k]) foot = k;
        const bool expression = foot < 0 && (fromDevice || ch == settings.expChannel) && cc == settings.expCC;
        if (foot >= 0 || expression)
        {
            if (! fromDevice)
            {
                hostFeedsPedals.store (true);
                const SpinLock::ScopedLockType l (focusLock);
                auto* fc = focused.load();
                if (fc != this && (fc == nullptr || ! fc->hostFeedsPedals.load())) focused.store (this);
                if (focused.load() != this) return;
            }
            if (foot >= 0) queue (val >= 64 ? FootPress : FootRelease, foot, 0.0f);
            else queue (Expression, val, 0.0f);
            return;
        }
    }
    if (fromDevice) return;   // anything else from the footswitch cable is not for the synth

    // keyboard controllers (any channel but the original map's)
    if (settings.pedalMapChannel == 0 || ch != settings.pedalMapChannel)
    {
        if (cc == 1) engine.modWheel ((float) val / 127.0f);
        else if (cc == 64) engine.sustainPedal (val >= 64);
        return;
    }

    // the Enzo's own MIDI chart, so templates made for the pedal work here
    const float v = (float) val / 127.0f;
    using namespace ParamIDs;
    switch (cc)
    {
        case 4:  queue (Expression, val, 0.0f); break;
        case 14: queueParam (SetPlain, bypass, val < 64 ? 1.0f : 0.0f); break;
        case 15: if (val > 0) queueParam (SetPlain, ParamIDs::bpm, jlimit (30.0f, 960.0f, 60000.0f / ((float) val * 10.0f))); break;
        case 16: queueParam (SetNorm, pitch, v); break;
        case 17: queueParam (SetNorm, cutoff, v); break;
        case 18: queueParam (SetNorm, mix, v); break;
        case 19: queueParam (SetNorm, sustain, v); break;
        case 20: queueParam (SetNorm, env, v); break;
        case 21: queueParam (SetNorm, mod, v); break;
        case 22: queueParam (SetNorm, glide, v); break;
        case 23: queueParam (SetPlain, filterType, (float) jlimit (0, 5, val * 6 / 128)); break;
        case 24: queueParam (SetNorm, delayLevel, v); break;
        case 25: queueParam (SetNorm, ring, v); break;
        case 26: queueParam (SetNorm, bandwidth, v); break;
        case 27: queueParam (SetNorm, feedback, v); break;
        case 28: if (val > 0) queue (Tap, 0, 0.0f); break;
        case 29: queueParam (SetPlain, mode, (float) jlimit (0, 3, val * 4 / 128)); break;
        case 30: queueParam (SetPlain, wave, val >= 64 ? 1.0f : 0.0f); break;
        case 31: queueParam (SetPlain, envType, val >= 64 ? 1.0f : 0.0f); break;
        case 1:  engine.modWheel (v); break;
        case 64: engine.sustainPedal (val >= 64); break;
        default: break;
    }
}

void ChangelingProcessor::setNorm (const String& id, float v01)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (jlimit (0.0f, 1.0f, v01));
        p->endChangeGesture();
    }
}

void ChangelingProcessor::setPlain (const String& id, float plain)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

void ChangelingProcessor::say (const String& s)
{
    uiMessage = s;
    uiMessageAt = nowSeconds();
}

void ChangelingProcessor::applyExpression (float v)
{
    uiExp.store (v);
    const int t = jlimit (0, (int) names::expTargets().size() - 1, idx (ParamIDs::expTarget));
    if (t != names::morphTarget())
    {
        setNorm (names::expTargets()[(size_t) t].id, v);
        return;
    }
    // Morph: every knob moves between its heel and toe snapshot, as on the original
    if (! morphReady()) { say ("SET HEEL AND TOE FOR MORPH"); return; }
    for (const auto& id : names::morphKnobs)
    {
        const Identifier key (id);
        if (! morphHeel.hasProperty (key) || ! morphToe.hasProperty (key)) continue;
        const float a = (float) morphHeel.getProperty (key), b = (float) morphToe.getProperty (key);
        setNorm (id, a + (b - a) * v);
    }
}

void ChangelingProcessor::timerCallback()
{
    const double now = nowSeconds();
    // settings changed by another copy, or another Flip Fella plugin: pick them up
    if (now >= nextSettingsCheck)
    {
        nextSettingsCheck = now + 1.0;
        const auto stamp = PedalSettings::file().getLastModificationTime();
        if (stamp != settingsStamp && learnTarget.load() == LearnNone && ! calibrating)
        {
            settingsStamp = stamp;
            settings.load();
            direct->setWanted (settings.footDevice);
        }
        const bool d = pedalsDirect();
        if (d != wasDirect)
        {
            wasDirect = d;
            if (d && hasFocus (this)) say ("FOOTSWITCHES READY");
        }
    }

    auto apply = [&] (const std::array<Pending, 512>& from, int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& q = from[(size_t) i];
            switch (q.kind)
            {
                case FootPress: footswitches.press (q.a, now); break;
                case FootRelease: footswitches.release (q.a, now); break;
                case SetNorm: setNorm (q.id, q.v); break;
                case SetPlain: setPlain (q.id, q.v); break;
                case LoadPreset: if (q.a >= 0 && q.a < kPresets) loadPreset (q.a); break;
                case Tap: tap(); break;
                case NextMode: nextMode (1); break;
                case Expression:
                {
                    const int cc = q.a;
                    expMovedAt = now;
                    if (calibrating)
                    {
                        if (calFirst < 0) calFirst = cc;
                        calLo = jmin (calLo, cc);
                        calHi = jmax (calHi, cc);
                        uiExp.store ((float) cc / 127.0f);
                        break;
                    }
                    applyExpression (settings.expressionValue (cc));
                    break;
                }
                case Learned:
                {
                    const int ch = q.a / 128, cc = q.a % 128;
                    const int what = learnTarget.exchange (LearnNone);
                    if (what >= 0 && what < 4)
                    {
                        settings.fsChannel[(size_t) what] = ch;
                        settings.fsCC[(size_t) what] = cc;
                        say ("FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + what)) + " IS CC " + String (cc));
                    }
                    else if (what == LearnExpression)
                    {
                        settings.expChannel = ch;
                        settings.expCC = cc;
                        say ("EXPRESSION IS CC " + String (cc));
                    }
                    savePedals();
                    break;
                }
                default: break;
            }
        }
    };
    {
        const auto scope = fifo.read (fifo.getNumReady());
        apply (pending, scope.startIndex1, scope.blockSize1);
        apply (pending, scope.startIndex2, scope.blockSize2);
    }
    {
        const auto scope = directFifo.read (directFifo.getNumReady());
        apply (directPending, scope.startIndex1, scope.blockSize1);
        apply (directPending, scope.startIndex2, scope.blockSize2);
    }

    // Keeper as an automatable button
    if (raw.at (ParamIDs::keeper)->load() > 0.5f)
    {
        setPlain (ParamIDs::keeper, 0.0f);
        keepNow();
    }

    footswitches.tick (now);
    int mask = 0;
    for (int k = 0; k < 4; ++k) if (footswitches.isDown (k)) mask |= 1 << k;
    uiFootDown.store (mask);

    if (calibrating && now > calibrateUntil)
    {
        calibrating = false;
        if (calHi - calLo > 20)
        {
            // whichever end the pedal started at is the heel
            const bool startedLow = std::abs (calFirst - calLo) < std::abs (calFirst - calHi);
            settings.heel = startedLow ? calLo : calHi;
            settings.toe = startedLow ? calHi : calLo;
            savePedals();
            say ("EXPRESSION CALIBRATED");
        }
        else
            say ("NO MOVEMENT, TRY AGAIN");
    }
}

// =====================================================================================
// Footswitches. A and B are the Enzo's own two switches, in its order, and each hold
// does that switch's second job. C steps the synth mode, D is sustain.
//
//   A  tap: bypass             hold: saw or square
//   B  tap: tap tempo          hold: runaway delay while held
//   C  tap: next synth mode    hold: ring chaos while held
//   D  tap: sustain latch      hold: sustain while held, like a piano pedal
//   A+B: next preset    C+D tap: footswitches to the next Changeling    C+D held 1 s: Keeper
// =====================================================================================
void ChangelingProcessor::onFootEvent (int sw, FootswitchLogic::Event e)
{
    using E = FootswitchLogic;
    if (e == E::Down || e == E::ComboAB || e == E::ComboCD) { uiFootPulse.fetch_add (1); lastFoot = sw; }

    switch (e)
    {
        case E::ComboHoldCD:
            focused.store (this);   // Keeper on the plugin you were playing
            keepNow();
            return;
        case E::ComboCD:
        {
            const SpinLock::ScopedLockType l (focusLock);
            const int i = instances.indexOf (this);
            auto* next = instances.isEmpty() ? nullptr : instances[(i + 1) % instances.size()];
            focused.store (next);
            if (next != nullptr) next->say ("FOOTSWITCHES HERE");
            return;
        }
        case E::ComboAB: stepPreset (1); return;
        case E::ComboHoldAB: return;
        case E::Down: return;
        default: break;
    }

    if (e == E::Tap)
    {
        if (sw == 0) { const bool b = ! on (ParamIDs::bypass); setPlain (ParamIDs::bypass, b ? 1.0f : 0.0f); say (b ? "BYPASSED" : "ON"); }
        if (sw == 1) tap();
        if (sw == 2) nextMode (1);
        if (sw == 3) { const bool l = ! on (ParamIDs::latch); setPlain (ParamIDs::latch, l ? 1.0f : 0.0f); say (l ? "SUSTAIN LATCHED" : "SUSTAIN RELEASED"); }
        return;
    }
    if (e == E::HoldStart)
    {
        if (sw == 0) { const int w = 1 - idx (ParamIDs::wave); setPlain (ParamIDs::wave, (float) w); say (w ? "SQUARE" : "SAW"); }
        if (sw == 1) { runaway.store (true); say ("RUNAWAY DELAY"); }
        if (sw == 2) { chaos.store (true); say ("RING CHAOS"); }
        if (sw == 3) { sustainHold.store (true); say ("SUSTAIN WHILE HELD"); }
        return;
    }
    if (e == E::HoldEnd)
    {
        if (sw == 1) runaway.store (false);
        if (sw == 2) chaos.store (false);
        if (sw == 3) sustainHold.store (false);
    }
}

void ChangelingProcessor::footDown (int sw)
{
    focused.store (this);
    footswitches.press (sw, nowSeconds());
}

void ChangelingProcessor::footUp (int sw) { footswitches.release (sw, nowSeconds()); }

void ChangelingProcessor::nextMode (int direction)
{
    const int m = (idx (ParamIDs::mode) + direction + 4) % 4;
    setPlain (ParamIDs::mode, (float) m);
    say (names::modes[m].toUpperCase());
}

void ChangelingProcessor::tap()
{
    const double t = nowSeconds();
    if (! taps.empty() && t - taps.back() > 2.0) taps.clear();
    taps.push_back (t);
    if (taps.size() > 5) taps.erase (taps.begin());
    if (taps.size() >= 2)
    {
        const double avg = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        setPlain (ParamIDs::bpm, (float) jlimit (30.0, 960.0, 60.0 / avg));
        setPlain (ParamIDs::sync, 0.0f); // tapping takes over from Ableton's tempo
        say ("TEMPO " + String (roundToInt (60.0 / avg)) + " BPM");
    }
    else say ("TAP AGAIN FOR TEMPO");
}

void ChangelingProcessor::learnFootswitch (int sw)
{
    learnTarget.store (sw);
    say ("PRESS FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + sw)));
}

void ChangelingProcessor::learnExpression()
{
    learnTarget.store (LearnExpression);
    say ("MOVE THE EXPRESSION PEDAL");
}

void ChangelingProcessor::calibrateExpression()
{
    calibrating = true;
    calibrateUntil = nowSeconds() + 6.0;
    calLo = 127; calHi = 0; calFirst = -1;
    say ("HEEL DOWN, THEN ROCK TO TOE");
}

void ChangelingProcessor::resetFootswitches()
{
    settings.resetFootswitches();
    savePedals();
    say ("FOOTSWITCH DEFAULTS RESTORED");
}

ValueTree ChangelingProcessor::knobSnapshot()
{
    ValueTree t ("Morph");
    for (const auto& id : names::morphKnobs)
        if (auto* p = apvts.getParameter (id)) t.setProperty (Identifier (id), p->getValue(), nullptr);
    return t;
}

void ChangelingProcessor::setMorphEnd (bool toe)
{
    (toe ? morphToe : morphHeel) = knobSnapshot();
    say (toe ? "TOE SET FROM THE KNOBS" : "HEEL SET FROM THE KNOBS");
}

// =====================================================================================
// Keeper
// =====================================================================================
void ChangelingProcessor::keepNow()
{
    DynamicObject::Ptr e = new DynamicObject();
    e->setProperty ("plugin", "Changeling");
   #ifdef JucePlugin_VersionString
    e->setProperty ("version", JucePlugin_VersionString);
   #endif
    e->setProperty ("recreates", "Meris Enzo");
    e->setProperty ("time", Time::getCurrentTime().toISO8601 (true));
    e->setProperty ("context", settings.keeperLive ? "Live" : "Studio");
    e->setProperty ("preset", selected < 0 ? String ("none") : String (selected + 1));
    e->setProperty ("mode", names::modes[jlimit (0, 3, idx (ParamIDs::mode))]);
    e->setProperty ("tempo", uiBpm.load());
    e->setProperty ("tempoFromHost", uiSynced.load());

    DynamicObject::Ptr tracking = new DynamicObject();
    tracking->setProperty ("note", engine.uiNote.load());
    tracking->setProperty ("lastLockMs", engine.uiLockMs.load());
    tracking->setProperty ("clarity", engine.uiClarity.load());
    e->setProperty ("tracking", var (tracking.get()));

    DynamicObject::Ptr feet = new DynamicObject();
    feet->setProperty ("sustainLatched", on (ParamIDs::latch));
    feet->setProperty ("sustainHeld", sustainHold.load());
    feet->setProperty ("ringChaos", chaos.load());
    feet->setProperty ("runaway", runaway.load());
    e->setProperty ("footswitches", var (feet.get()));

    DynamicObject::Ptr exp = new DynamicObject();
    exp->setProperty ("target", names::expTargets()[(size_t) jlimit (0, (int) names::expTargets().size() - 1, idx (ParamIDs::expTarget))].label);
    exp->setProperty ("position", uiExp.load());
    exp->setProperty ("movedSecondsAgo", nowSeconds() - expMovedAt);
    e->setProperty ("expression", var (exp.get()));

    // every parameter, hidden ones included, as value and as shown on the panel
    DynamicObject::Ptr params = new DynamicObject();
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
        {
            if (rp->getParameterID() == ParamIDs::keeper) continue;
            DynamicObject::Ptr pv = new DynamicObject();
            pv->setProperty ("value", rp->convertFrom0to1 (rp->getValue()));
            pv->setProperty ("shown", rp->getCurrentValueAsText());
            params->setProperty (rp->getParameterID(), var (pv.get()));
        }
    e->setProperty ("parameters", var (params.get()));

    keeper.keep (e);
    uiKeepPulse.fetch_add (1);
    say ("KEPT");
}

File ChangelingProcessor::exportKeepers()
{
    const auto f = keeper.exportZip();
    if (f.existsAsFile()) { say ("EXPORTED TO DOCUMENTS"); f.revealToUser(); }
    else say ("NOTHING KEPT YET");
    return f;
}

void ChangelingProcessor::openKeepersFolder()
{
    Keeper::folder().createDirectory();
    Keeper::folder().startAsProcess();
}

void ChangelingProcessor::setKeeperLive (bool live)
{
    settings.keeperLive = live;
    savePedals();
}

// =====================================================================================
// Presets: 16 slots in four banks. Each saves the sound: knobs, mode, wave, filter, the
// Deep page sound settings and tempo. Your instrument's settings stay as they are.
// =====================================================================================
bool ChangelingProcessor::inSnapshot (const String& id)
{
    static const StringArray excluded { ParamIDs::bypass, ParamIDs::sync, ParamIDs::input, ParamIDs::source, ParamIDs::keeper,
                                        ParamIDs::latch, ParamIDs::lowNote, ParamIDs::sensitivity, ParamIDs::tunerRef,
                                        ParamIDs::octaveGuard, ParamIDs::expTarget };
    return ! excluded.contains (id);
}

ValueTree ChangelingProcessor::snapshotOf()
{
    ValueTree t ("Snapshot");
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            if (inSnapshot (rp->getParameterID()))
                t.setProperty (Identifier (rp->getParameterID()), rp->convertFrom0to1 (rp->getValue()), nullptr);
    return t;
}

void ChangelingProcessor::applySnapshot (const ValueTree& t)
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
        {
            const Identifier id (rp->getParameterID());
            if (inSnapshot (rp->getParameterID()) && t.hasProperty (id))
                setPlain (rp->getParameterID(), (float) t.getProperty (id));
        }
}

void ChangelingProcessor::makeFactoryPresets()
{
    // four starting sounds in the red bank; the rest are yours
    auto make = [&] (int slot, const std::map<String, float>& values)
    {
        ValueTree t ("Snapshot");
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
                if (inSnapshot (rp->getParameterID()))
                    t.setProperty (Identifier (rp->getParameterID()), rp->convertFrom0to1 (rp->getDefaultValue()), nullptr);
        for (const auto& [id, v] : values) t.setProperty (Identifier (id), v, nullptr);
        slots[(size_t) slot] = t;
    };
    using namespace ParamIDs;
    // 1 Low synth bass: mono, a sub oscillator an octave down
    make (0, { { mode, 1 }, { wave, 0 }, { cutoff, 0.42f }, { env, 0.6f }, { bandwidth, 0.45f }, { sustain, 0.6f }, { mix, 0.85f },
               { glide, 0.1f }, { delayLevel, 0.0f }, { osc2Interval, 1 }, { osc2Level, 0.6f }, { mod, 0.08f } });
    // 2 Glass chords: poly, square, soft state-variable low-pass, a little echo
    make (1, { { mode, 0 }, { wave, 1 }, { filterType, 3 }, { cutoff, 0.62f }, { env, 0.3f }, { bandwidth, 0.25f }, { sustain, 0.75f },
               { mix, 0.6f }, { mod, 0.35f }, { delayLevel, 0.35f }, { feedback, 0.35f } });
    // 3 Sparkarp: the arp at a racing tap tempo, as Meris's own preset does
    make (2, { { mode, 2 }, { wave, 1 }, { ParamIDs::bpm, 750.0f }, { cutoff, 0.5f }, { env, 0.7f }, { bandwidth, 0.55f }, { sustain, 0.3f },
               { mix, 0.7f }, { delayLevel, 0.4f }, { feedback, 0.45f } });
    // 4 Ring chaos: ring mod on, tracking loose and glitchy
    make (3, { { mode, 0 }, { wave, 0 }, { ring, 0.62f }, { character, 0.7f }, { cutoff, 0.7f }, { env, 0.4f }, { bandwidth, 0.6f },
               { mix, 0.7f }, { mod, 0.2f }, { delayLevel, 0.3f }, { feedback, 0.5f } });
}

void ChangelingProcessor::savePreset (int slot)
{
    if (slot < 0 || slot >= kPresets) return;
    slots[(size_t) slot] = snapshotOf();
    selected = slot;
    say ("SAVED " + String (slot + 1));
}

void ChangelingProcessor::loadPreset (int slot)
{
    if (! hasPreset (slot)) return;
    applySnapshot (slots[(size_t) slot]);
    selected = slot;
    say ("PRESET " + String (slot + 1));
}

bool ChangelingProcessor::hasPreset (int slot) const
{
    return slot >= 0 && slot < kPresets && slots[(size_t) slot].isValid();
}

void ChangelingProcessor::stepPreset (int direction)
{
    for (int k = 1; k <= kPresets; ++k)
    {
        const int s = ((selected < 0 ? (direction > 0 ? -1 : 0) : selected) + direction * k + kPresets * 2) % kPresets;
        if (hasPreset (s)) { loadPreset (s); return; }
    }
    say ("NO PRESETS SAVED YET");
}

// =====================================================================================
// Saving with the set: parameters, presets and the Morph snapshots
// =====================================================================================
void ChangelingProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    for (int i = state.getNumChildren(); --i >= 0;)
    {
        const auto type = state.getChild (i).getType().toString();
        if (type == "Presets" || type == "MorphHeel" || type == "MorphToe") state.removeChild (i, nullptr);
    }
    ValueTree presets ("Presets");
    presets.setProperty ("selected", selected, nullptr);
    for (int i = 0; i < kPresets; ++i)
    {
        ValueTree slot ("Slot");
        if (slots[(size_t) i].isValid()) slot.appendChild (slots[(size_t) i].createCopy(), nullptr);
        presets.appendChild (slot, nullptr);
    }
    state.appendChild (presets, nullptr);
    if (morphHeel.isValid()) { ValueTree h ("MorphHeel"); h.appendChild (morphHeel.createCopy(), nullptr); state.appendChild (h, nullptr); }
    if (morphToe.isValid()) { ValueTree h ("MorphToe"); h.appendChild (morphToe.createCopy(), nullptr); state.appendChild (h, nullptr); }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void ChangelingProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = ValueTree::fromXml (*xml);

    const auto presets = tree.getChildWithName ("Presets");
    if (presets.isValid())
    {
        selected = (int) presets.getProperty ("selected", -1);
        for (int i = 0; i < kPresets && i < presets.getNumChildren(); ++i)
        {
            const auto slot = presets.getChild (i);
            slots[(size_t) i] = slot.getNumChildren() > 0 ? slot.getChild (0).createCopy() : ValueTree();
        }
        tree.removeChild (presets, nullptr);
    }
    for (auto* which : { "MorphHeel", "MorphToe" })
    {
        const auto m = tree.getChildWithName (which);
        if (! m.isValid()) continue;
        (String (which) == "MorphHeel" ? morphHeel : morphToe) = m.getNumChildren() > 0 ? m.getChild (0).createCopy() : ValueTree();
        tree.removeChild (m, nullptr);
    }
    apvts.replaceState (tree);
}

AudioProcessorEditor* ChangelingProcessor::createEditor()
{
    focused.store (this); // opening a copy's window gives it the footswitches
    return new ChangelingEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChangelingProcessor();
}
