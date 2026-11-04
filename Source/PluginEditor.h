#pragma once

#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

// Flip Fella palette, shared across all of Kevin's builds.
namespace ff
{
const juce::Colour bg        { 0xff050505 };
const juce::Colour face      { 0xff0d0d0d };
const juce::Colour housing   { 0xff060606 };
const juce::Colour card      { 0xff1b1b1b };
const juce::Colour cardHi    { 0xff252525 };
const juce::Colour cardLow   { 0xff141414 };
const juce::Colour ink       { 0xfff3f0e8 };
const juce::Colour dim       { 0xffa29e95 };
const juce::Colour faint     { 0xff5a5852 };
const juce::Colour hint      { 0xff8c887f };
const juce::Colour line      { 0xff3a3a3a };
const juce::Colour ring      { 0xff1a1a1a };
const juce::Colour pin       { 0xff3c3c3c };
const juce::Colour red       { 0xffe8392f };
const juce::Colour teal      { 0xff3fd6c8 };
const juce::Colour coral     { 0xffff7a59 };
const juce::Colour amber     { 0xfff2c230 };
const juce::Colour blob      { 0xff4d5bff };
const juce::Colour blobShade { 0xff2329a8 };
const juce::Colour eye       { 0xffffffff };
const juce::Colour faceInk   { 0xff0b0b12 };
const juce::Colour green     { 0xff4fcf6a };
// the four preset banks
const juce::Colour bankR     { 0xffe8392f };
const juce::Colour bankY     { 0xfff2c230 };
const juce::Colour bankG     { 0xff4fcf6a };
const juce::Colour bankB     { 0xff4d8dff };

juce::Font font (float height, float kerning = 0.04f);
juce::String utf8 (const juce::String&);
void label (juce::Graphics&, const juce::String& text, juce::Rectangle<int> r,
            juce::Justification j = juce::Justification::centredLeft, juce::Colour c = dim, float size = 13.0f);
void altTag (juce::Graphics&, const juce::String& text, juce::Point<float> topLeft, bool lit = false);
} // namespace ff

namespace ui
{
// A split-flap card that flips like the Karlsson clock when its value changes.
class FlipCard : public juce::Component
{
public:
    void setValue (const juce::String& v);
    void setTextHeight (float h) { textHeight = h; }
    void advance (double dt);
    void paint (juce::Graphics&) override;
    std::function<void()> onClick;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }

private:
    void drawHalf (juce::Graphics&, juce::Rectangle<float> card, const juce::String& text, bool top, float scale, bool shade);
    juce::String current, previous;
    float progress = 1.0f;
    float textHeight = 34.0f;
};

// Rounded pill buttons bound to a choice parameter.
class Pills : public juce::Component
{
public:
    Pills (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::StringArray labels, float textSize = 13.0f, float gap = 4.0f);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setRows (int r) { rows = juce::jmax (1, r); repaint(); }

private:
    juce::Rectangle<float> cell (int i) const;
    juce::StringArray labels;
    int selected = 0, rows = 1;
    float textSize, gap;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// Pills not tied to a parameter: tabs, menus and one-shot actions.
class ActionPills : public juce::Component
{
public:
    ActionPills (juce::StringArray labels, float textSize = 12.0f, float gap = 4.0f);
    std::function<void (int)> onClick;
    std::function<int()> selected;        // which one is lit, or -1
    void setLabels (juce::StringArray l) { labels = std::move (l); repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cell (int i) const;
    juce::StringArray labels;
    float textSize, gap;
};

class FlipLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override;
};

// A knob drawn as a small clock face, with its name above and value below. An alt knob
// carries a small amber tag naming the knob it hides behind on the pedal.
class Dial : public juce::Component
{
public:
    Dial (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption,
          const juce::String& altOf = {}, int knobSize = 84);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setLit (bool l) { if (l != lit) { lit = l; repaint(); } }
    bool isAlt() const { return altOf.isNotEmpty(); }

private:
    juce::Slider slider;
    juce::RangedAudioParameter* param = nullptr;
    juce::String caption, altOf;
    int knobSize;
    bool lit = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// A slim horizontal bar for one parameter, with words at each end.
class BarSlider : public juce::Component
{
public:
    BarSlider (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::Colour fill);
    void resized() override { slider.setBounds (getLocalBounds()); }

private:
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// The filter's response, opening and closing with the envelope.
class FilterCurve : public juce::Component
{
public:
    explicit FilterCurve (ChangelingProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    ChangelingProcessor& proc;
};

// Rows of round lights: synth voices, echo repeats, arp steps.
class Lights : public juce::Component
{
public:
    enum Kind { Voices, Echo, Arp };
    Lights (ChangelingProcessor& p, Kind k) : proc (p), kind (k) {}
    void paint (juce::Graphics&) override;
    void tick (double now);

private:
    ChangelingProcessor& proc;
    Kind kind;
    int lastSteps = 0;
    double echoStart = -100.0, lastNowStore = 0.0;
};

class ChaosBar : public juce::Component
{
public:
    explicit ChaosBar (ChangelingProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    ChangelingProcessor& proc;
};

// The note Changeling heard, its frequency and how sure it is. Click for the tuner.
class TrackingStrip : public juce::Component
{
public:
    explicit TrackingStrip (ChangelingProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick (double now, double dt);

private:
    ChangelingProcessor& proc;
    FlipCard note;
    juce::String hzText = "WAITING FOR A NOTE", status = "CLICK THE NOTE TO TUNE";
    float cents = 0.0f;
    double lastHeard = -100.0;
};

class TapButton : public juce::Component
{
public:
    explicit TapButton (ChangelingProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    ChangelingProcessor& proc;
    double flashUntil = 0;
};

// The four footswitches as on-screen buttons: tap and hold work as on the floor.
class FootButton : public juce::Component
{
public:
    FootButton (ChangelingProcessor& p, int sw) : proc (p), index (sw) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { proc.footDown (index); repaint(); }
    void mouseUp (const juce::MouseEvent&) override { proc.footUp (index); repaint(); }

private:
    ChangelingProcessor& proc;
    int index;
};

class BypassButton : public juce::Component
{
public:
    explicit BypassButton (ChangelingProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    ChangelingProcessor& proc;
};

// Sixteen presets in red, yellow, green and blue banks. Tap to load, hold to save.
class PresetBanks : public juce::Component, private juce::Timer
{
public:
    explicit PresetBanks (ChangelingProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> cell (int i) const;
    ChangelingProcessor& proc;
    int pressed = -1, flashSlot = -1;
    double downAt = 0, flashUntil = 0;
    bool saved = false;
};

// The expression pedal: where it is and what it sweeps. Click to choose the target.
class ExpressionBar : public juce::Component
{
public:
    explicit ExpressionBar (ChangelingProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    ChangelingProcessor& proc;
};

// Pebble from Flip Fella, reacting to notes, the filter, arpeggios and your feet.
class Pebble : public juce::Component
{
public:
    explicit Pebble (ChangelingProcessor& p) : proc (p) {}
    void update (double now, double dt);
    void paint (juce::Graphics&) override;

private:
    ChangelingProcessor& proc;
    float wet = 0, look = 0, lookY = 0, mouth = 0, hop = 0, beat = 0, foot = 0, lean = 0, chaos = 0;
    int footSide = 0, lastOnsets = 0, lastSteps = 0, lastFoot = 0;
    bool asleep = false, blinking = false, held = false;
    double lastSound = 0, nextBlink = 2.0, blinkUntil = 0, now = 0;
    juce::Random random;
};
} // namespace ui

class ChangelingEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ChangelingEditor (ChangelingProcessor&);
    ~ChangelingEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void showPage (int page);
    void tickForSnapshot() { timerCallback(); }

private:
    void timerCallback() override;
    void section (juce::Graphics&, juce::Rectangle<int>) const;
    void footswitchMenu();
    template <typename T, typename... Args> T& make (int page, Args&&... args);

    ChangelingProcessor& proc;
    ui::FlipLookAndFeel lnf;
    int page = 0;
    bool altMode = false;
    double lastTick = 0;
    juce::String modeLine, filterName, arpLine, footSay;

    std::vector<std::unique_ptr<juce::Component>> owned;
    std::array<std::vector<juce::Component*>, 3> pages;   // main, deep, pedal
    std::vector<ui::Dial*> mainDials;

    // header and always-visible parts
    ui::FlipCard tempoCard;
    ui::FlipCard* modeCard = nullptr;
    ui::FlipCard* pedalCards[4] {};
    ui::ActionPills* tabs = nullptr;
    ui::ActionPills* altButton = nullptr;
    ui::Lights* voiceLights = nullptr;
    ui::Lights* echoLights = nullptr;
    ui::Lights* arpLights = nullptr;
    ui::FilterCurve* curve = nullptr;
    ui::ChaosBar* chaosBar = nullptr;
    ui::TrackingStrip* tracking = nullptr;
    ui::Pebble* pebble = nullptr;
    ui::PresetBanks* banks = nullptr;
    ui::FootButton* feet[4] {};
    ui::BypassButton* bypass = nullptr;
    ui::TapButton* tapButton = nullptr;
    ui::ExpressionBar* expBar = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChangelingEditor)
};
