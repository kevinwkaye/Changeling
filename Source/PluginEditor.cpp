#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

// =====================================================================================
// Palette helpers
// =====================================================================================
namespace ff
{
Font font (float height, float kerning)
{
    static Typeface::Ptr typeface = Typeface::createSystemTypefaceFor (BinaryData::RobotoCondensedBold_ttf,
                                                                       BinaryData::RobotoCondensedBold_ttfSize);
    return Font (FontOptions (typeface).withHeight (height).withKerningFactor (kerning));
}

// Source literals are UTF-8 but JUCE reads plain C strings as Latin-1, so "·" arrives as
// two characters. Put such text back together before drawing it.
String utf8 (const String& s)
{
    bool suspect = false;
    for (auto p = s.getCharPointer(); ! p.isEmpty() && ! suspect; ++p)
        suspect = *p >= 0xC2 && *p <= 0xF4;
    if (! suspect) return s;
    MemoryBlock bytes;
    for (auto p = s.getCharPointer(); ! p.isEmpty(); ++p)
    {
        if (*p > 0xFF) return s;   // already proper text
        const char b = (char) (uint8) *p;
        bytes.append (&b, 1);
    }
    return String::fromUTF8 ((const char*) bytes.getData(), (int) bytes.getSize());
}

void label (Graphics& g, const String& text, Rectangle<int> r, Justification j, Colour c, float size)
{
    g.setColour (c);
    g.setFont (font (size, 0.08f));
    g.drawText (utf8 (text), r, j, false);
}

void altTag (Graphics& g, const String& rawText, Point<float> p, bool lit)
{
    const String text = utf8 (rawText);
    const auto f = font (9.5f, 0.1f);
    const float w = GlyphArrangement::getStringWidth (f, text) + 8.0f;
    Rectangle<float> r (p.x, p.y, w, 13.0f);
    if (lit) { g.setColour (amber); g.fillRoundedRectangle (r, 3.0f); }
    else { g.setColour (amber.withAlpha (0.55f)); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f); }
    g.setColour (lit ? bg : amber);
    g.setFont (f);
    g.drawText (text, r, Justification::centred, false);
}
} // namespace ff

namespace ui
{
static double nowSeconds() { return Time::getMillisecondCounterHiRes() * 0.001; }

static void pill (Graphics& g, Rectangle<float> b, const String& text, bool on, float size, Colour onColour = ff::ink)
{
    const float r = b.getHeight() * 0.5f;
    if (on) { g.setColour (onColour); g.fillRoundedRectangle (b, r); }
    else { g.setColour (ff::line); g.drawRoundedRectangle (b.reduced (1.0f), r, 2.0f); }
    g.setColour (on ? ff::bg : ff::ink);
    g.setFont (ff::font (size, 0.06f));
    g.drawFittedText (text, b.reduced (4.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.8f);
}

static String noteName (float midi)
{
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int m = (int) std::lround (midi);
    return String (n[((m % 12) + 12) % 12]) + String (m / 12 - 1);
}

// A transparent area that does something when clicked
class ClickArea : public Component
{
public:
    std::function<void()> onClick;
    ClickArea() { setMouseCursor (MouseCursor::PointingHandCursor); }
    void mouseDown (const MouseEvent&) override { if (onClick) onClick(); }
};

// =====================================================================================
// FlipCard
// =====================================================================================
void FlipCard::setValue (const String& v)
{
    if (v == current) return;
    previous = current;
    current = v;
    progress = previous.isEmpty() ? 1.0f : 0.0f;
    repaint();
}

void FlipCard::advance (double dt)
{
    if (progress < 1.0f)
    {
        progress = jmin (1.0f, progress + (float) (dt / 0.26));
        repaint();
    }
}

void FlipCard::drawHalf (Graphics& g, Rectangle<float> card, const String& text, bool top, float scale, bool shade)
{
    const float hinge = card.getCentreY();
    const float halfH = card.getHeight() * 0.5f;
    if (scale <= 0.001f) return;

    Graphics::ScopedSaveState save (g);
    auto clip = top ? Rectangle<float> (card.getX(), hinge - halfH * scale, card.getWidth(), halfH * scale)
                    : Rectangle<float> (card.getX(), hinge, card.getWidth(), halfH * scale);
    g.reduceClipRegion (clip.getSmallestIntegerContainer());
    g.addTransform (AffineTransform::scale (1.0f, scale, 0.0f, hinge));

    if (top) g.setGradientFill (ColourGradient (ff::cardHi, 0, card.getY(), ff::card, 0, hinge, false));
    else     g.setGradientFill (ColourGradient (ff::card, 0, hinge, ff::cardLow, 0, card.getBottom(), false));
    g.fillRoundedRectangle (card, 7.0f);
    g.setColour (ff::ink);
    g.setFont (ff::font (textHeight, 0.02f));
    g.drawFittedText (text, card.reduced (6.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.7f);

    if (shade)
    {
        g.setColour (Colours::black.withAlpha (0.45f * (1.0f - scale)));
        g.fillRoundedRectangle (card, 7.0f);
    }
}

void FlipCard::paint (Graphics& g)
{
    auto outer = getLocalBounds().toFloat();
    g.setColour (ff::housing);
    g.fillRoundedRectangle (outer, 10.0f);
    auto card = outer.reduced (6.0f);

    if (progress >= 1.0f)
    {
        drawHalf (g, card, current, true, 1.0f, false);
        drawHalf (g, card, current, false, 1.0f, false);
    }
    else
    {
        drawHalf (g, card, current, true, 1.0f, false);
        drawHalf (g, card, previous, false, 1.0f, false);
        const float eased = 1.0f - std::pow (1.0f - progress, 2.0f);
        if (eased < 0.5f) drawHalf (g, card, previous, true, 1.0f - eased * 2.0f, true);
        else              drawHalf (g, card, current, false, (eased - 0.5f) * 2.0f, true);
    }

    g.setColour (Colours::black);
    g.fillRect (card.getX(), card.getCentreY() - 1.5f, card.getWidth(), 3.0f);
    g.setColour (ff::pin);
    g.fillRoundedRectangle (outer.getX() + 1.0f, card.getCentreY() - 6.0f, 3.0f, 12.0f, 1.0f);
    g.fillRoundedRectangle (outer.getRight() - 4.0f, card.getCentreY() - 6.0f, 3.0f, 12.0f, 1.0f);
}

// =====================================================================================
// Pills
// =====================================================================================
Pills::Pills (AudioProcessorValueTreeState& s, const String& paramId, StringArray l, float size, float g)
    : labels (std::move (l)), textSize (size), gap (g)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*s.getParameter (paramId), [this] (float v)
    {
        selected = (int) std::lround (v);
        repaint();
    });
    attachment->sendInitialUpdate();
}

Rectangle<float> Pills::cell (int i) const
{
    const int perRow = (labels.size() + rows - 1) / rows;
    const float w = ((float) getWidth() - gap * (float) (perRow - 1)) / (float) perRow;
    const float h = ((float) getHeight() - gap * (float) (rows - 1)) / (float) rows;
    return { (float) (i % perRow) * (w + gap), (float) (i / perRow) * (h + gap), w, h };
}

void Pills::paint (Graphics& g)
{
    for (int i = 0; i < labels.size(); ++i)
        pill (g, cell (i).reduced (1.0f), labels[i], i == selected, textSize);
}

void Pills::mouseDown (const MouseEvent& e)
{
    for (int i = 0; i < labels.size(); ++i)
        if (cell (i).contains (e.position)) { attachment->setValueAsCompleteGesture ((float) i); return; }
}

ActionPills::ActionPills (StringArray l, float size, float g) : labels (std::move (l)), textSize (size), gap (g)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
}

Rectangle<float> ActionPills::cell (int i) const
{
    const float w = ((float) getWidth() - gap * (float) (labels.size() - 1)) / (float) labels.size();
    return { (float) i * (w + gap), 0.0f, w, (float) getHeight() };
}

void ActionPills::paint (Graphics& g)
{
    const int sel = selected ? selected() : -1;
    for (int i = 0; i < labels.size(); ++i)
        pill (g, cell (i).reduced (1.0f), labels[i], i == sel, textSize);
}

void ActionPills::mouseDown (const MouseEvent& e)
{
    for (int i = 0; i < labels.size(); ++i)
        if (cell (i).contains (e.position)) { if (onClick) onClick (i); repaint(); return; }
}

// =====================================================================================
// Look and feel: clock-face knobs
// =====================================================================================
void FlipLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider&)
{
    const float size = (float) jmin (w, h);
    const Point<float> c ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f);
    const float r = size * 0.5f - 1.0f;

    g.setColour (ff::ring);
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - r * 0.9f, c.y - r * 0.9f, r * 1.8f, r * 1.8f);

    g.setColour (ff::ink);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (end - start) * (float) i / 10.0f;
        const Point<float> p1 (c.x + std::sin (a) * r * 0.80f, c.y - std::cos (a) * r * 0.80f);
        const Point<float> p2 (c.x + std::sin (a) * r * 0.92f, c.y - std::cos (a) * r * 0.92f);
        g.drawLine ({ p1, p2 }, 2.0f);
    }
    const float a = start + pos * (end - start);
    Path hand;
    hand.addRoundedRectangle (-3.0f, -r * 0.68f, 6.0f, r * 0.68f + r * 0.15f, 2.0f);
    g.fillPath (hand, AffineTransform::rotation (a).translated (c));
    g.fillEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - 1.8f, c.y - 1.8f, 3.6f, 3.6f);
}

void FlipLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle, Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f;
    const float left = (float) x + 1.0f, right = (float) (x + w) - 1.0f;
    g.setColour (ff::ring);
    g.fillRoundedRectangle (left, cy - 5.0f, right - left, 10.0f, 5.0f);
    g.setColour (s.findColour (Slider::trackColourId));
    g.fillRoundedRectangle (left, cy - 5.0f, jmax (10.0f, pos - left), 10.0f, 5.0f);
}

Dial::Dial (AudioProcessorValueTreeState& s, const String& paramId, const String& c, const String& alt, int size)
    : caption (c), altOf (alt), knobSize (size)
{
    param = s.getParameter (paramId);
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setMouseCursor (MouseCursor::PointingHandCursor);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
}

void Dial::resized()
{
    slider.setBounds (getLocalBounds().removeFromTop (18 + knobSize + 6).withTrimmedTop (21).withSizeKeepingCentre (knobSize, knobSize));
}

void Dial::paint (Graphics& g)
{
    auto r = getLocalBounds();
    ff::label (g, caption.toUpperCase(), r.removeFromTop (18), Justification::centred, isAlt() ? ff::hint : ff::dim);
    r.removeFromTop (knobSize + 6);
    ff::label (g, param->getCurrentValueAsText().toUpperCase(), r.removeFromTop (20), Justification::centred, ff::ink, 15.0f);
    if (isAlt())
    {
        const String t = ff::utf8 ("ALT · " + altOf);
        const float w = GlyphArrangement::getStringWidth (ff::font (9.5f, 0.1f), t) + 8.0f;
        ff::altTag (g, t, { ((float) getWidth() - w) * 0.5f, (float) r.getY() + 2.0f }, lit);
    }
}

BarSlider::BarSlider (AudioProcessorValueTreeState& s, const String& paramId, Colour fill)
{
    slider.setSliderStyle (Slider::LinearHorizontal);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setColour (Slider::trackColourId, fill);
    slider.setMouseCursor (MouseCursor::PointingHandCursor);
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
}

// =====================================================================================
// Filter curve
// =====================================================================================
void FilterCurve::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (ff::housing);
    g.fillRoundedRectangle (b, 10.0f);
    const float W = b.getWidth(), H = b.getHeight();
    g.setColour (Colour (0xff161616));
    for (int i = 1; i < 6; ++i) g.drawVerticalLine (roundToInt ((float) i * W / 6.0f), 6.0f, H - 6.0f);

    auto& s = proc.apvts;
    const float cut = s.getRawParameterValue (ParamIDs::cutoff)->load();
    const float bw = s.getRawParameterValue (ParamIDs::bandwidth)->load();
    const int type = roundToInt (s.getRawParameterValue (ParamIDs::filterType)->load());
    const float envMove = proc.getEngine().uiFilterEnv.load();
    const float fc = jlimit (0.02f, 0.98f, cut + envMove * 0.45f);
    const bool ladder = type < 3;
    const int shape = type % 3;
    auto resp = [&] (float x)
    {
        const float r = (x - fc) * 9.0f;
        float base;
        if (shape == 0) base = r < 0.0f ? 1.0f : std::exp (-r * (ladder ? 1.3f : 0.9f));
        else if (shape == 2) base = r > 0.0f ? 1.0f : std::exp (r * (ladder ? 1.3f : 0.9f));
        else base = std::exp (-std::abs (r) * (ladder ? 0.9f : 1.1f) * (1.0f + bw));
        const float peak = std::exp (-r * r * (2.0f + bw * 10.0f)) * bw * 5.0f * 0.18f;
        return jmin (1.25f, base + peak);
    };
    auto yOf = [&] (float v) { return H - 10.0f - v * (H - 26.0f); };
    Path fill, line;
    fill.startNewSubPath (0.0f, H);
    for (float x = 0.0f; x <= W; x += 2.0f)
    {
        const float y = yOf (resp (x / W));
        fill.lineTo (x, y);
        if (x == 0.0f) line.startNewSubPath (x, y); else line.lineTo (x, y);
    }
    fill.lineTo (W, H);
    fill.closeSubPath();
    g.setColour (ff::teal.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (ff::teal);
    g.strokePath (line, PathStrokeType (2.5f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (ff::ink);
    g.fillEllipse (fc * W - 4.0f, yOf (resp (fc)) - 4.0f, 8.0f, 8.0f);
    ff::label (g, "40 HZ", { 8, (int) H - 16, 60, 14 }, Justification::centredLeft, ff::faint, 10.0f);
    ff::label (g, "16 KHZ", { (int) W - 68, (int) H - 16, 60, 14 }, Justification::centredRight, ff::faint, 10.0f);
}

// =====================================================================================
// Lights
// =====================================================================================
void Lights::tick (double now)
{
    if (kind == Echo)
    {
        const int steps = proc.getEngine().uiOnsets.load() + proc.getEngine().uiArpSteps.load();
        if (steps != lastSteps) { lastSteps = steps; echoStart = now; }
        lastNowStore = now;
    }
    repaint();
}

void Lights::paint (Graphics& g)
{
    auto& eng = proc.getEngine();
    const float d = kind == Arp ? 20.0f : 16.0f, gap = kind == Arp ? 5.0f : 8.0f;
    const int n = kind == Voices ? 6 : 8;
    for (int i = 0; i < n; ++i)
    {
        Rectangle<float> r ((float) i * (d + gap) + 4.0f, ((float) getHeight() - d) * 0.5f, d, d);
        bool lit = false; Colour c = ff::teal;
        if (kind == Voices)
        {
            lit = (eng.uiVoiceMask.load() >> i) & 1;
            if ((eng.uiMidiMask.load() >> i) & 1) c = ff::blob;
        }
        else if (kind == Arp)
        {
            const int st = eng.uiArpStep.load();
            lit = st >= 0 && st % 8 == i;
            c = ff::amber;
        }
        else
        {
            // repeats walk outwards at the tempo after each note, more with more feedback
            const float fb = proc.apvts.getRawParameterValue (ParamIDs::feedback)->load();
            const bool runaway = proc.runawayOn();
            const int count = runaway ? 8 : 1 + roundToInt (fb * 6.0f);
            const double beat = 60.0 / jmax (30.0, (double) proc.currentBpm());
            const double since = lastNowStore - echoStart;
            const int k = (int) (since / beat);
            lit = i < count && k >= i && since < beat * (count + 1);
            if (proc.apvts.getRawParameterValue (ParamIDs::delayLevel)->load() < 0.02f) lit = false;
        }
        if (lit)
        {
            g.setColour (c.withAlpha (0.2f));
            if (kind == Arp) g.fillRoundedRectangle (r.expanded (4.0f), 7.0f); else g.fillEllipse (r.expanded (4.0f));
        }
        g.setColour (lit ? c : ff::pin);
        if (kind == Arp) g.fillRoundedRectangle (r, 5.0f); else g.fillEllipse (r);
    }
}

void ChaosBar::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (ff::ring);
    g.fillRoundedRectangle (b, 4.0f);
    const float v = jlimit (0.0f, 1.0f, proc.getEngine().uiChaos.load());
    if (v > 0.005f)
    {
        g.setColour (ff::coral);
        g.fillRoundedRectangle (b.withWidth (jmax (8.0f, b.getWidth() * v)), 4.0f);
    }
}

// =====================================================================================
// Tracking strip
// =====================================================================================
TrackingStrip::TrackingStrip (ChangelingProcessor& p) : proc (p)
{
    note.setTextHeight (30.0f);
    note.setValue (String::fromUTF8 ("—"));
    note.onClick = [this] { proc.setTuner (! proc.tunerOpen()); };
    addAndMakeVisible (note);
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void TrackingStrip::resized() { note.setBounds (0, 20, 92, 60); }

void TrackingStrip::mouseDown (const MouseEvent&) { proc.setTuner (! proc.tunerOpen()); }

void TrackingStrip::tick (double now, double dt)
{
    auto& eng = proc.getEngine();
    note.advance (dt);
    const bool tuner = proc.tunerOpen();
    static const float refs[3] = { 432.0f, 440.0f, 442.0f };
    const float refHz = refs[jlimit (0, 2, roundToInt (proc.apvts.getRawParameterValue (ParamIDs::tunerRef)->load()))];
    const float live = eng.uiNote.load();
    const float shownNote = tuner ? eng.uiEstimate.load() : live;
    String newHz = hzText, newStatus = status;

    if (shownNote > 0.0f)
    {
        lastHeard = now;
        const float adj = shownNote - 12.0f * std::log2 (refHz / 440.0f);
        cents = (adj - (float) std::lround (adj)) * 100.0f;
        note.setValue (noteName (adj));
        const float hz = 440.0f * std::pow (2.0f, (shownNote - 69.0f) / 12.0f);
        newHz = String (hz, 1) + " HZ · " + (cents >= 0.0f ? "+" : "") + String (roundToInt (cents)) + " CENTS";
        if (tuner) newStatus = std::abs (cents) < 3.0f ? "IN TUNE · OUTPUT MUTED" : "TUNER · OUTPUT MUTED";
        else newStatus = "LOCKED IN " + String (roundToInt (eng.uiLockMs.load())) + " MS";
    }
    else if (now - lastHeard > 1.5)
    {
        if (eng.uiMidiMask.load() != 0) { newStatus = "PLAYING FROM MIDI"; newHz = "KEYBOARD"; }
        else
        {
            newHz = tuner ? "PLAY ONE STRING" : "WAITING FOR A NOTE";
            newStatus = tuner ? "TUNER · OUTPUT MUTED" : "CLICK THE NOTE TO TUNE";
            cents = 0.0f;
        }
        if (! tuner) note.setValue (String::fromUTF8 ("—"));
    }
    if (newHz != hzText || newStatus != status) { hzText = newHz; status = newStatus; }
    repaint();
}

void TrackingStrip::paint (Graphics& g)
{
    const int input = roundToInt (proc.apvts.getRawParameterValue (ParamIDs::input)->load());
    static const char* inputs[3] = { "GUITAR", "BASS", "SYNTH" };
    ff::label (g, proc.tunerOpen() ? String ("TUNER") : String ("TRACKING · ") + inputs[jlimit (0, 2, input)], { 0, 0, 250, 16 });
    ff::label (g, hzText, { 102, 28, 160, 16 }, Justification::centredLeft, ff::ink, 12.0f);
    Rectangle<float> bar (102.0f, 50.0f, 150.0f, 8.0f);
    g.setColour (ff::ring);
    g.fillRoundedRectangle (bar, 4.0f);
    g.setColour (ff::faint);
    g.fillRect (bar.getCentreX() - 0.5f, bar.getY() - 2.0f, 1.0f, bar.getHeight() + 4.0f);
    const float x = bar.getCentreX() + jlimit (-50.0f, 50.0f, cents) * (bar.getWidth() * 0.5f / 50.0f);
    g.setColour (std::abs (cents) < 3.0f && proc.tunerOpen() ? ff::green : ff::teal);
    g.fillRoundedRectangle (x - 4.0f, bar.getY(), 8.0f, bar.getHeight(), 4.0f);
    ff::label (g, status, { 102, 64, 160, 14 }, Justification::centredLeft, ff::hint, 11.0f);
}

// =====================================================================================
// Buttons
// =====================================================================================
void TapButton::paint (Graphics& g)
{
    pill (g, getLocalBounds().toFloat().reduced (1.0f), "TAP", false, 13.0f);
    if (nowSeconds() < flashUntil)
    {
        g.setColour (ff::coral);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), (float) getHeight() * 0.5f, 2.0f);
    }
}

void TapButton::mouseDown (const MouseEvent&)
{
    proc.tap();
    flashUntil = nowSeconds() + 0.09;
    repaint();
    Timer::callAfterDelay (100, [safe = Component::SafePointer<TapButton> (this)] { if (safe) safe->repaint(); });
}

void FootButton::paint (Graphics& g)
{
    static const char* letters[4] = { "A", "B", "C", "D" };
    static const char* taps[4] = { "BYPASS", "TAP TEMPO", "NEXT MODE", "SUSTAIN LATCH" };
    static const char* holds[4] = { "HOLD WAVE", "HOLD RUNAWAY", "HOLD CHAOS", "HOLD SUSTAIN" };
    auto& s = proc.apvts;
    bool lit = false;
    if (index == 0) lit = s.getRawParameterValue (ParamIDs::bypass)->load() > 0.5f;
    if (index == 1) lit = proc.runawayOn();
    if (index == 2) lit = proc.chaosOn();
    if (index == 3) lit = s.getRawParameterValue (ParamIDs::latch)->load() > 0.5f || proc.sustainHeld();
    const bool down = (proc.uiFootDown.load() >> index) & 1;

    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (ff::housing);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (down ? ff::coral : ff::line);
    g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.0f);
    const Point<float> led (r.getX() + 13.0f, r.getY() + 17.0f);
    if (lit) { g.setColour (ff::coral.withAlpha (0.22f)); g.fillEllipse (led.x - 9.0f, led.y - 9.0f, 18.0f, 18.0f); }
    g.setColour (lit ? ff::coral : ff::pin);
    g.fillEllipse (led.x - 5.0f, led.y - 5.0f, 10.0f, 10.0f);
    g.setColour (ff::ink);
    g.setFont (ff::font (19.0f, 0.06f));
    g.drawText (letters[index], Rectangle<float> (r.getX() + 24.0f, r.getY() + 5.0f, 30.0f, 24.0f), Justification::centredLeft, false);
    ff::label (g, taps[index], { (int) r.getX() + 7, (int) r.getY() + 30, (int) r.getWidth() - 10, 12 }, Justification::centredLeft, ff::dim, 10.0f);
    ff::label (g, holds[index], { (int) r.getX() + 7, (int) r.getY() + 44, (int) r.getWidth() - 10, 12 }, Justification::centredLeft, ff::dim, 10.0f);
}

void BypassButton::paint (Graphics& g)
{
    const bool bypassed = proc.apvts.getRawParameterValue (ParamIDs::bypass)->load() > 0.5f;
    const bool tails = proc.apvts.getRawParameterValue (ParamIDs::trails)->load() > 0.5f;
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (ff::housing);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (bypassed ? ff::line : ff::ink);
    g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.0f);
    const Point<float> p (r.getX() + 24.0f, r.getCentreY());
    if (! bypassed) { g.setColour (ff::red.withAlpha (0.22f)); g.fillEllipse (p.x - 12.0f, p.y - 12.0f, 24.0f, 24.0f); }
    g.setColour (bypassed ? ff::pin : ff::red);
    g.fillEllipse (p.x - 7.0f, p.y - 7.0f, 14.0f, 14.0f);
    auto text = r.withTrimmedLeft (46.0f);
    g.setColour (ff::ink);
    g.setFont (ff::font (19.0f, 0.08f));
    g.drawText (bypassed ? "BYPASSED" : "ON", text.removeFromTop (r.getHeight() * 0.55f).withTrimmedTop (6.0f), Justification::bottomLeft, false);
    ff::label (g, tails ? "TAP TO BYPASS · TRAILS" : "TAP TO BYPASS · CUT", text.toNearestInt(), Justification::topLeft, ff::dim, 11.0f);
}

void BypassButton::mouseDown (const MouseEvent&)
{
    auto* p = proc.apvts.getParameter (ParamIDs::bypass);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
    p->endChangeGesture();
    repaint();
}

// =====================================================================================
// Presets
// =====================================================================================
Rectangle<float> PresetBanks::cell (int i) const
{
    return { (float) (i % 8) * 34.0f, (float) (i / 8) * 26.0f, 30.0f, 22.0f };
}

void PresetBanks::paint (Graphics& g)
{
    static const Colour bankCols[4] = { ff::bankR, ff::bankY, ff::bankG, ff::bankB };
    const int sel = proc.selectedPreset();
    for (int i = 0; i < ChangelingProcessor::kPresets; ++i)
    {
        auto c = cell (i);
        const Colour col = bankCols[i / ChangelingProcessor::kPerBank];
        const bool flash = i == flashSlot && nowSeconds() < flashUntil;
        const bool full = proc.hasPreset (i);
        if (i == sel || flash) { g.setColour (flash ? ff::ink : col); g.fillRoundedRectangle (c, 5.0f); }
        else { g.setColour (col.withAlpha (full ? 1.0f : 0.4f)); g.drawRoundedRectangle (c.reduced (1.0f), 5.0f, 2.0f); }
        g.setColour (i == sel || flash ? ff::bg : (full ? ff::ink : ff::faint));
        g.setFont (ff::font (11.0f, 0.04f));
        g.drawText (String (i + 1), c, Justification::centred, false);
    }
}

void PresetBanks::mouseDown (const MouseEvent& e)
{
    pressed = -1;
    for (int i = 0; i < ChangelingProcessor::kPresets; ++i)
        if (cell (i).expanded (2.0f).contains (e.position)) pressed = i;
    downAt = nowSeconds();
    saved = false;
    startTimer (40);
}

void PresetBanks::timerCallback()
{
    if (pressed >= 0 && ! saved && nowSeconds() - downAt > 0.7)
    {
        proc.savePreset (pressed);
        saved = true;
        flashSlot = pressed;
        flashUntil = nowSeconds() + 0.9;
    }
    if (pressed < 0 && nowSeconds() > flashUntil) stopTimer();
    repaint();
}

void PresetBanks::mouseUp (const MouseEvent&)
{
    if (pressed >= 0 && ! saved) proc.loadPreset (pressed);
    pressed = -1;
    repaint();
}

// =====================================================================================
// Expression bar
// =====================================================================================
void ExpressionBar::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (ff::housing);
    g.fillRoundedRectangle (r, 10.0f);
    const float v = proc.isCalibrating() ? proc.expressionLevel() : proc.expressionLevel();
    g.setColour (ff::blob.withAlpha (0.35f));
    g.fillRoundedRectangle (r.withWidth (jmax (0.0f, r.getWidth() * v)), 10.0f);
    g.setColour (ff::line);
    g.drawRoundedRectangle (r.reduced (1.0f), 10.0f, 2.0f);
    const int t = roundToInt (proc.apvts.getRawParameterValue (ParamIDs::expTarget)->load());
    const String target = String (names::expTargets()[(size_t) jlimit (0, (int) names::expTargets().size() - 1, t)].label).toUpperCase();
    ff::label (g, target, r.reduced (14.0f, 0.0f).toNearestInt(), Justification::centredLeft, ff::ink, 14.0f);
    const String pos = proc.isCalibrating() ? String ("ROCK HEEL TO TOE") : (v < 0.02f ? String ("HEEL") : (v > 0.98f ? String ("TOE") : String (roundToInt (v * 100.0f)) + "%"));
    ff::label (g, pos, r.reduced (14.0f, 0.0f).toNearestInt(), Justification::centredRight, ff::ink, 14.0f);
}

void ExpressionBar::mouseDown (const MouseEvent&)
{
    PopupMenu m;
    const int cur = roundToInt (proc.apvts.getRawParameterValue (ParamIDs::expTarget)->load());
    const auto& targets = names::expTargets();
    m.addSectionHeader ("The expression pedal sweeps");
    for (int i = 0; i < (int) targets.size(); ++i) m.addItem (100 + i, targets[(size_t) i].label, true, i == cur);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this] (int r)
    {
        if (r >= 100)
            if (auto* p = proc.apvts.getParameter (ParamIDs::expTarget))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 ((float) (r - 100)));
                p->endChangeGesture();
            }
    });
}

// =====================================================================================
// Pebble
// =====================================================================================
void Pebble::update (double t, double dt)
{
    now = t;
    auto& eng = proc.getEngine();
    auto& s = proc.apvts;
    const float level = eng.uiLevel.load();
    const int voices = eng.uiVoiceMask.load();
    wet += ((voices != 0 ? 1.0f : 0.0f) - wet) * (float) (1.0 - std::exp (-dt / 0.15));
    held = s.getRawParameterValue (ParamIDs::latch)->load() > 0.5f || proc.sustainHeld();
    const bool bypassed = s.getRawParameterValue (ParamIDs::bypass)->load() > 0.5f;
    if (level > 0.004f || voices != 0 || held) lastSound = t;
    const bool expMoving = t - proc.lastExpressionMove() < 2.0;
    if (expMoving) lastSound = t;
    asleep = bypassed || t - lastSound > 6.0;

    // hop on each new note, bob on each arp step, tap a foot on each footswitch press
    const int onsets = eng.uiOnsets.load();
    if (onsets != lastOnsets) { lastOnsets = onsets; hop = 1.0f; }
    const int steps = eng.uiArpSteps.load();
    if (steps != lastSteps) { lastSteps = steps; beat = 1.0f; }
    const int fp = proc.uiFootPulse.load();
    if (fp != lastFoot) { lastFoot = fp; foot = 1.0f; footSide = jmax (0, proc.lastFootSwitch()) % 2; }
    hop *= (float) std::exp (-dt / 0.12);
    beat *= (float) std::exp (-dt / 0.08);
    foot *= (float) std::exp (-dt / 0.1);

    // eyes: down for low notes, up for high ones, darting with the arp
    const float noteNow = eng.uiNote.load();
    const float yTarget = noteNow > 0.0f ? -jlimit (-1.0f, 1.0f, (noteNow - 50.0f) / 22.0f) : 0.0f;
    lookY += (yTarget - lookY) * (float) (1.0 - std::exp (-dt / 0.15));
    const int mode = roundToInt (s.getRawParameterValue (ParamIDs::mode)->load());
    const float lookTarget = mode == chg::ModeArp && eng.uiArpStep.load() >= 0 ? (float) std::sin (t * 9.0) : (float) std::sin (t * 0.7) * 0.5f * wet;
    look += (lookTarget - look) * (float) (1.0 - std::exp (-dt / 0.12));

    // mouth opens with the filter, body leans with the expression pedal, shakes in chaos
    const float cut = s.getRawParameterValue (ParamIDs::cutoff)->load();
    const float mouthTarget = (wet > 0.05f || expMoving) ? jlimit (0.0f, 1.0f, (cut - 0.25f) * 1.4f + eng.uiFilterEnv.load() * 0.6f) : 0.0f;
    mouth += (mouthTarget - mouth) * (float) (1.0 - std::exp (-dt / 0.06));
    const float leanTarget = expMoving ? (proc.expressionLevel() - 0.5f) * 0.35f : 0.0f;
    lean += (leanTarget - lean) * (float) (1.0 - std::exp (-dt / 0.1));
    const float chaosTarget = proc.chaosOn() ? 1.0f : 0.0f;
    chaos += (chaosTarget - chaos) * (float) (1.0 - std::exp (-dt / 0.1));

    if (t > nextBlink)
    {
        blinkUntil = t + 0.13;
        nextBlink = t + 2.5 + random.nextDouble() * 3.0;
    }
    blinking = t < blinkUntil;
    repaint();
}

void Pebble::paint (Graphics& g)
{
    auto a = getLocalBounds().toFloat();
    const float s = a.getWidth() / 150.0f;
    const float bob = asleep ? 0.0f : 1.5f * (float) std::sin (now * 2.4) - beat * 4.0f;
    const float breathe = asleep ? 1.0f + 0.025f * (float) std::sin (now * 1.6) : 1.0f;
    const float jx = chaos > 0.1f ? (random.nextFloat() - 0.5f) * 6.0f * chaos : 0.0f;
    const float jy = chaos > 0.1f ? (random.nextFloat() - 0.5f) * 4.0f * chaos : 0.0f;
    const float dy = bob - hop * 10.0f + jy;
    const auto tf = AffineTransform::translation (-100.0f, -100.0f).scaled (s * breathe, s / breathe)
                        .rotated (lean).translated (a.getCentreX() + jx, a.getCentreY() + 2.0f);
    auto ell = [&] (float cx, float cy, float rx, float ry, Colour col)
    {
        Path p; p.addEllipse (cx - rx, cy - ry, rx * 2, ry * 2);
        g.setColour (col); g.fillPath (p, tf);
    };
    auto stroke = [&] (Path& p, float w, Colour col = ff::faceInk)
    {
        g.setColour (col);
        g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded), tf);
    };

    // feet: one taps on a footswitch press
    const float lift = foot * 9.0f;
    ell (78.0f, 150.0f - (footSide == 0 ? lift : 0.0f), 13.0f, 6.0f, ff::blobShade);
    ell (122.0f, 150.0f - (footSide == 1 ? lift : 0.0f), 13.0f, 6.0f, ff::blobShade);

    const float glow = held ? 1.0f : jlimit (0.0f, 1.0f, wet * 0.8f + hop * 0.4f);
    if (glow > 0.02f && ! asleep)
    {
        Path ringPath; ringPath.addEllipse (100 - 66, 100 - 56 + dy, 132, 112);
        const Colour c = held ? ff::coral : (chaos > 0.4f && random.nextBool() ? ff::coral : ff::teal);
        stroke (ringPath, 5.0f, c.withAlpha (0.65f * glow));
    }
    ell (107, 109 + dy, 58, 48, ff::blobShade);
    ell (100, 100 + dy, 58, 48, ff::blob);

    if (asleep || blinking)
    {
        for (float ex : { 80.0f, 120.0f })
        {
            Path lid; lid.startNewSubPath (ex - 12, 94 + dy); lid.quadraticTo (ex, 102 + dy, ex + 12, 94 + dy);
            stroke (lid, 6.0f);
        }
    }
    else
    {
        int k = 0;
        for (float ex : { 80.0f, 120.0f })
        {
            const float r = chaos > 0.4f ? (k++ ? 16.0f : 12.0f) : 14.0f;
            ell (ex, 92 + dy, r, r, ff::eye);
            ell (ex + look * 5.0f, 94 + dy + lookY * 5.0f, 7.0f, 7.0f, ff::faceInk);
        }
    }

    if (asleep)
    {
        Path m; m.startNewSubPath (94, 124 + dy); m.lineTo (106, 124 + dy);
        stroke (m, 6.0f);
    }
    else if (chaos > 0.4f)
    {
        Path m; m.startNewSubPath (86, 124 + dy);
        for (int i = 1; i <= 6; ++i) m.lineTo (86.0f + (float) i * 5.0f, 124.0f + dy + (i % 2 ? -4.0f : 4.0f));
        stroke (m, 5.0f);
    }
    else if (held) ell (100, 123 + dy, 6.0f, 6.0f, ff::faceInk);   // holding its breath
    else if (mouth > 0.15f) ell (100, 124 + dy, 7.0f + 3.0f * mouth, 4.0f + 8.0f * mouth, ff::faceInk);
    else
    {
        Path m; m.startNewSubPath (90, 120 + dy); m.quadraticTo (100, 130 + dy, 110, 120 + dy);
        stroke (m, 6.0f);
    }

    if (asleep)
    {
        g.setFont (ff::font (15.0f, 0.0f));
        for (int k = 0; k < 3; ++k)
        {
            const float ph = (float) std::fmod (now * 0.5 + k / 3.0, 1.0);
            g.setColour (ff::ink.withAlpha (1.0f - ph));
            g.drawText ("z", Rectangle<float> (a.getRight() - 16.0f + ph * 8.0f, a.getY() + 8.0f - ph * 14.0f, 16.0f, 16.0f), Justification::centred, false);
        }
    }
}
} // namespace ui

// =====================================================================================
// Editor
// =====================================================================================
using namespace ui;

template <typename T, typename... Args>
T& ChangelingEditor::make (int pg, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T& ref = *c;
    addAndMakeVisible (ref);
    if (pg >= 0) pages[(size_t) pg].push_back (&ref);
    owned.push_back (std::move (c));
    return ref;
}

ChangelingEditor::ChangelingEditor (ChangelingProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& s = p.apvts;
    using namespace ParamIDs;

    // ---- header ----
    make<Pills> (-1, s, input, StringArray { "GUITAR", "BASS", "SYNTH" }, 13.0f, 2.0f).setBounds (434, 24, 186, 32);
    make<Pills> (-1, s, source, StringArray { "AUDIO", "MIDI", "BOTH" }, 13.0f, 2.0f).setBounds (694, 24, 176, 32);
    addAndMakeVisible (tempoCard);
    tempoCard.setTextHeight (22.0f);
    tempoCard.setBounds (884, 20, 76, 40);
    tapButton = &make<TapButton> (-1, p);
    tapButton->setBounds (966, 24, 56, 32);
    altButton = &make<ActionPills> (-1, StringArray { "ALT" }, 13.0f);
    altButton->setBounds (1034, 24, 52, 32);
    altButton->selected = [this] { return altMode ? 0 : -1; };
    altButton->onClick = [this] (int)
    {
        altMode = ! altMode;
        for (auto* d : mainDials) { d->setAlpha (altMode && ! d->isAlt() ? 0.25f : 1.0f); d->setLit (altMode && d->isAlt()); }
        repaint();
    };
    tabs = &make<ActionPills> (-1, StringArray { "MAIN", "DEEP", "PEDAL" }, 13.0f, 2.0f);
    tabs->setBounds (1094, 24, 146, 32);
    tabs->selected = [this] { return page; };
    tabs->onClick = [this] (int i) { showPage (i); };

    auto dial = [&] (int pg, const char* id, const char* caption, int x, int y, int w, const char* altOf = "", int knob = 84) -> Dial&
    {
        auto& d = make<Dial> (pg, s, id, caption, String (altOf), knob);
        d.setBounds (x, y, w, 18 + knob + 6 + 20 + (String (altOf).isNotEmpty() ? 16 : 0));
        if (pg == 0) mainDials.push_back (&d);
        return d;
    };

    // ---- MAIN: synth ----
    modeCard = &make<FlipCard> (0);
    modeCard->setTextHeight (34.0f);
    modeCard->setBounds (36, 116, 268, 72);
    modeCard->onClick = [this] { proc.nextMode (1); };
    make<Pills> (0, s, mode, StringArray { "POLY", "MONO", "ARP", "DRY" }, 13.0f, 2.0f).setBounds (36, 196, 268, 32);
    dial (0, pitch, "Pitch", 36, 240, 130);
    dial (0, glide, "Glide", 170, 240, 134, "PITCH");
    make<Pills> (0, s, wave, StringArray { "SAW", "SQUARE" }, 13.0f, 2.0f).setBounds (36, 416, 268, 30);
    voiceLights = &make<Lights> (0, p, Lights::Voices);
    voiceLights->setBounds (108, 472, 196, 24);

    // ---- MAIN: filter ----
    curve = &make<FilterCurve> (0, p);
    curve->setBounds (356, 116, 368, 112);
    make<Pills> (0, s, filterType, StringArray { "LADDER LP", "LADDER BP", "LADDER HP", "SV LP", "SV BP", "SV HP" }, 12.0f, 2.0f).setBounds (356, 256, 368, 26);
    dial (0, cutoff, "Cutoff", 356, 300, 120);
    dial (0, env, "Env", 480, 300, 120);
    dial (0, bandwidth, "Bandwidth", 604, 300, 120, "ENV");
    make<Pills> (0, s, envType, StringArray { "TRIGGER", "FOLLOW" }, 13.0f, 2.0f).setBounds (466, 470, 258, 30);

    // ---- MAIN: voice ----
    dial (0, sustain, "Sustain", 776, 116, 98, "", 76);
    dial (0, ring, "Ring", 878, 116, 98, "SUSTAIN", 76);
    dial (0, mod, "Mod", 776, 270, 98, "", 76);
    dial (0, mix, "Mix", 878, 270, 98, "", 76);
    chaosBar = &make<ChaosBar> (0, p);
    chaosBar->setBounds (776, 432, 198, 8);
    make<Pills> (0, s, trails, StringArray { "CUT", "TRAILS" }, 12.0f, 2.0f).setBounds (846, 466, 128, 30);

    // ---- MAIN: delay + arp ----
    dial (0, delayLevel, "Level", 1026, 116, 98, "MIX", 76);
    dial (0, feedback, "Feedback", 1128, 116, 98, "MOD", 76);
    echoLights = &make<Lights> (0, p, Lights::Echo);
    echoLights->setBounds (1022, 276, 206, 24);
    arpLights = &make<Lights> (0, p, Lights::Arp);
    arpLights->setBounds (1022, 354, 206, 28);
    make<Pills> (0, s, latch, StringArray { "OFF", "LATCHED" }, 12.0f, 2.0f).setBounds (1026, 440, 198, 30);

    // ---- DEEP ----
    make<Pills> (1, s, osc2Interval, StringArray { "UNISON", String::fromUTF8 ("−12"), "+7", "+12", "+19" }, 12.0f, 2.0f).setBounds (36, 140, 268, 28);
    make<Pills> (1, s, osc2Wave, StringArray { "SAME", "SAW", "SQUARE", "PULSE", "SINE" }, 11.0f, 2.0f).setBounds (36, 202, 268, 28);
    dial (1, osc2Level, "Level", 36, 244, 130);
    dial (1, osc2Detune, "Detune", 170, 244, 134);
    make<Pills> (1, s, tunerRef, StringArray { "432", "440", "442" }, 12.0f, 2.0f).setBounds (110, 444, 194, 30);

    make<BarSlider> (1, s, character, ff::teal).setBounds (356, 140, 368, 22);
    make<Pills> (1, s, lowNote, StringArray { String::fromUTF8 ("LOW B · 31 HZ"), String::fromUTF8 ("LOW E · 41 HZ"), String::fromUTF8 ("LOW A · 55 HZ") }, 12.0f, 2.0f).setBounds (356, 220, 368, 28);
    dial (1, sensitivity, "Sensitivity", 356, 280, 120);
    dial (1, noteHold, "Note hold", 480, 280, 120);
    dial (1, polySpeed, "Poly speed", 604, 280, 120);
    make<Pills> (1, s, octaveGuard, StringArray { "OFF", "ON" }, 12.0f, 2.0f).setBounds (500, 434, 224, 30);

    auto& pattern = make<Pills> (1, s, arpPattern, StringArray { "ORIGINAL", "UP", "DOWN", "BOUNCE", "PLAYED", "RANDOM" }, 11.0f, 4.0f);
    pattern.setRows (2);
    pattern.setBounds (776, 140, 198, 60);
    make<Pills> (1, s, arpStep, StringArray { "1/4", "1/8", "1/8T", "1/16" }, 12.0f, 2.0f).setBounds (776, 234, 198, 28);
    make<Pills> (1, s, arpOctaves, StringArray { "1", "2", "3" }, 12.0f, 2.0f).setBounds (776, 296, 198, 28);
    dial (1, arpGate, "Gate", 776, 336, 198, "", 76);

    make<Pills> (1, s, ringFollow, StringArray { "FIXED", "FOLLOW" }, 12.0f, 2.0f).setBounds (1026, 140, 198, 28);
    make<Pills> (1, s, velocity, StringArray { "OFF", "LEVEL", "FILTER" }, 12.0f, 2.0f).setBounds (1026, 226, 198, 28);
    make<Pills> (1, s, bendRange, StringArray { String::fromUTF8 ("±2"), String::fromUTF8 ("±7"), String::fromUTF8 ("±12") }, 12.0f, 2.0f).setBounds (1026, 288, 198, 28);
    make<Pills> (1, s, midiMono, StringArray { "LAST", "LOW", "HIGH" }, 12.0f, 2.0f).setBounds (1026, 350, 198, 28);
    make<Pills> (1, s, ParamIDs::sync, StringArray { "TAP", "ABLETON" }, 12.0f, 2.0f).setBounds (1026, 438, 198, 28);

    // ---- PEDAL ----
    for (int i = 0; i < 4; ++i)
    {
        const int x = 36 + i * 300;
        auto& card = make<FlipCard> (2);
        card.setTextHeight (40.0f);
        card.setValue (String::charToString ((juce_wchar) ('A' + i)));
        card.setBounds (x, 116, 286, 76);
        pedalCards[i] = &card;
        auto& lp = make<ActionPills> (2, StringArray { "LEARN", "DEFAULTS" }, 12.0f, 4.0f);
        lp.setBounds (x, 274, 286, 28);
        lp.selected = [this, i] { return proc.learningWhat() == i ? 0 : -1; };
        lp.onClick = [this, i] (int k) { if (k == 0) proc.learnFootswitch (i); else proc.resetFootswitches(); };
    }
    expBar = &make<ExpressionBar> (2, p);
    expBar->setBounds (36, 430, 580, 40);
    auto& expActions = make<ActionPills> (2, StringArray { "CALIBRATE", "LEARN", "CURVE", "MORPH" }, 12.0f, 4.0f);
    expActions.setBounds (36, 478, 580, 28);
    expActions.selected = [this] { return proc.isCalibrating() ? 0 : (proc.learningWhat() == ChangelingProcessor::LearnExpression ? 1 : -1); };
    expActions.onClick = [this, &expActions] (int k)
    {
        if (k == 0) proc.calibrateExpression();
        if (k == 1) proc.learnExpression();
        if (k == 2)
        {
            PopupMenu m;
            static const char* curves[3] = { "Linear", "Slow start", "Fast start" };
            for (int c = 0; c < 3; ++c) m.addItem (1 + c, curves[c], true, proc.pedals().curve == c);
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (&expActions), [this] (int r)
            {
                if (r > 0) { proc.pedals().curve = r - 1; proc.savePedals(); }
            });
        }
        if (k == 3)
        {
            PopupMenu m;
            m.addSectionHeader ("Morph sweeps every knob from heel to toe");
            m.addItem (1, "Set heel from the knobs now");
            m.addItem (2, "Set toe from the knobs now");
            m.addItem (3, "Use Morph on the expression pedal", proc.morphReady());
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (&expActions), [this] (int r)
            {
                if (r == 1 || r == 2) proc.setMorphEnd (r == 2);
                if (r == 3)
                    if (auto* prm = proc.apvts.getParameter (ParamIDs::expTarget))
                        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) names::morphTarget()));
            });
        }
    };

    auto& mapPills = make<ActionPills> (2, StringArray { "OFF", "CH 1", "CH 2", "CH 3", "CH 4", String::fromUTF8 ("…") }, 12.0f, 4.0f);
    mapPills.setBounds (648, 344, 576, 28);
    mapPills.selected = [this]
    {
        const int c = proc.pedals().pedalMapChannel;
        return c == 0 ? 0 : (c <= 4 ? c : 5);
    };
    mapPills.onClick = [this, &mapPills] (int k)
    {
        if (k < 5) { proc.pedals().pedalMapChannel = k; proc.savePedals(); return; }
        PopupMenu m;
        for (int c = 1; c <= 16; ++c) m.addItem (c, "Channel " + String (c), true, proc.pedals().pedalMapChannel == c);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&mapPills), [this] (int r)
        {
            if (r > 0) { proc.pedals().pedalMapChannel = r; proc.savePedals(); }
        });
    };
    auto& keeperPills = make<ActionPills> (2, StringArray { "KEEP THIS MOMENT", "LIVE", "STUDIO", "EXPORT" }, 13.0f, 4.0f);
    keeperPills.setBounds (648, 430, 576, 40);
    keeperPills.selected = [this] { return proc.pedals().keeperLive ? 1 : 2; };
    keeperPills.onClick = [this] (int k)
    {
        if (k == 0) proc.keepNow();
        if (k == 1 || k == 2) proc.setKeeperLive (k == 1);
        if (k == 3) proc.exportKeepers();
    };

    // ---- bottom strip ----
    pebble = &make<Pebble> (-1, p);
    pebble->setBounds (30, 536, 100, 104);
    tracking = &make<TrackingStrip> (-1, p);
    tracking->setBounds (140, 548, 262, 84);
    banks = &make<PresetBanks> (-1, p);
    banks->setBounds (410, 570, 272, 48);
    auto& footArea = make<ClickArea> (-1);
    footArea.setBounds (700, 544, 340, 22);
    footArea.onClick = [this] { footswitchMenu(); };
    for (int i = 0; i < 4; ++i)
    {
        feet[i] = &make<FootButton> (-1, p, i);
        feet[i]->setBounds (700 + i * 86, 570, 82, 64);
    }
    bypass = &make<BypassButton> (-1, p);
    bypass->setBounds (1054, 556, 170, 72);

    showPage (0);
    setSize (1260, 660);
    lastTick = Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (60);
}

ChangelingEditor::~ChangelingEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ChangelingEditor::showPage (int pg)
{
    page = pg;
    for (int i = 0; i < 3; ++i)
        for (auto* c : pages[(size_t) i]) c->setVisible (i == pg);
    altButton->setVisible (pg == 0);
    repaint();
}

void ChangelingEditor::section (Graphics& g, Rectangle<int> r) const
{
    g.setColour (ff::face);
    g.fillRoundedRectangle (r.toFloat(), 12.0f);
}

void ChangelingEditor::footswitchMenu()
{
    PopupMenu m;
    m.addSectionHeader (proc.footInputStatus());
    const auto want = proc.footDevice();
    m.addItem (1, "Find my footswitch automatically", true, want.isEmpty());
    const auto devices = proc.footDeviceNames();
    for (int i = 0; i < devices.size(); ++i) m.addItem (100 + i, devices[i], true, want == devices[i]);
    m.addSeparator();
    m.addItem (2, "Only from a MIDI track in Ableton", true, want == "none");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (feet[0]), [this, devices] (int r)
    {
        if (r == 1) proc.setFootDevice ({});
        if (r == 2) proc.setFootDevice ("none");
        if (r >= 100 && r - 100 < devices.size()) proc.setFootDevice (devices[r - 100]);
    });
}

void ChangelingEditor::paint (Graphics& g)
{
    g.fillAll (ff::bg);
    g.setColour (ff::ink);
    g.setFont (ff::font (26.0f, 0.06f));
    g.drawText ("CHANGELING", 20, 20, 180, 40, Justification::centredLeft, false);
    ff::label (g, "EVERY NOTE, A NEW VOICE", { 196, 20, 200, 40 });
    ff::label (g, "INPUT", { 386, 20, 50, 40 });
    ff::label (g, "SOURCE", { 634, 20, 60, 40 });

    auto tag = [&] (const String& t, int x, int y) { ff::altTag (g, t, { (float) x, (float) y }, altMode && page == 0); };
    auto hint = [&] (const String& t, Rectangle<int> r, Justification j = Justification::centredLeft) { ff::label (g, t, r, j, ff::hint, 11.0f); };

    if (page == 0)
    {
        section (g, { 20, 76, 300, 440 });
        section (g, { 340, 76, 400, 440 });
        section (g, { 760, 76, 230, 440 });
        section (g, { 1010, 76, 230, 440 });
        ff::label (g, "SYNTH", { 36, 92, 100, 16 });
        ff::label (g, modeLine, { 96, 92, 208, 16 }, Justification::centredRight, ff::ink);
        ff::label (g, "WAVE", { 36, 396, 60, 16 }); tag ("ALT · BYPASS", 80, 397);
        hint ("HOLD FOOTSWITCH A", { 36, 450, 268, 14 });
        ff::label (g, "VOICES", { 36, 476, 80, 16 });
        hint ("TEAL FROM YOUR INSTRUMENT · BLUE FROM MIDI", { 36, 498, 268, 14 });

        ff::label (g, "FILTER", { 356, 92, 100, 16 });
        ff::label (g, filterName, { 520, 92, 204, 16 }, Justification::centredRight, ff::ink);
        ff::label (g, "TYPE", { 356, 238, 60, 16 }); tag ("ALT · FILTER", 398, 239);
        ff::label (g, "ENVELOPE", { 356, 472, 90, 16 }); tag ("ALT · TAP", 356, 492);

        ff::label (g, "VOICE", { 776, 92, 100, 16 });
        ff::label (g, "RING CHAOS", { 776, 410, 120, 16 });
        hint ("HOLD FOOTSWITCH C", { 776, 446, 198, 14 });
        ff::label (g, "TRAILS", { 776, 472, 70, 16 });

        ff::label (g, "DELAY + ARP", { 1026, 92, 140, 16 });
        hint ("REPEATS · HOLD B FOR RUNAWAY", { 1026, 304, 198, 14 });
        ff::label (g, "ARP", { 1026, 336, 60, 16 });
        ff::label (g, arpLine, { 1090, 336, 134, 16 }, Justification::centredRight, ff::ink);
        hint ("PATTERN AND STEP ON DEEP", { 1026, 388, 198, 14 });
        ff::label (g, "SUSTAIN", { 1026, 418, 80, 16 });
        hint ("TAP D TO LATCH · HOLD D WHILE HELD", { 1026, 476, 198, 14 });
    }
    else if (page == 1)
    {
        section (g, { 20, 76, 300, 440 });
        section (g, { 340, 76, 400, 440 });
        section (g, { 760, 76, 230, 440 });
        section (g, { 1010, 76, 230, 440 });
        ff::label (g, "OSCILLATOR 2", { 36, 92, 200, 16 });
        hint ("FROM THE ENZO X", { 160, 93, 144, 14 }, Justification::centredRight);
        ff::label (g, "INTERVAL", { 36, 120, 100, 16 });
        ff::label (g, "WAVE", { 36, 182, 100, 16 });
        hint ("SAME AND UNISON: BOTH OSCILLATORS", { 36, 400, 268, 14 });
        hint ("MOVE TOGETHER, AS ON THE ORIGINAL", { 36, 416, 268, 14 });
        ff::label (g, "TUNER", { 36, 450, 80, 16 });
        hint ("REFERENCE HZ · CLICK THE NOTE TO TUNE", { 36, 486, 268, 14 });

        ff::label (g, "TRACKING", { 356, 92, 120, 16 });
        ff::label (g, "CHARACTER", { 356, 120, 120, 16 });
        hint ("TIGHT", { 356, 166, 100, 14 });
        hint ("WILD · THE ORIGINAL'S GLITCHES", { 474, 166, 250, 14 }, Justification::centredRight);
        ff::label (g, "LOWEST NOTE ON BASS", { 356, 200, 220, 16 });
        hint ("LOWER REACH WAITS A LITTLE LONGER FOR THE FIRST NOTE", { 356, 254, 368, 14 });
        ff::label (g, "OCTAVE GUARD", { 356, 440, 140, 16 });
        hint ("KEEPS THE FUNDAMENTAL WHEN THE 2ND HARMONIC IS LOUDER", { 356, 474, 368, 14 });

        ff::label (g, "ARP", { 776, 92, 100, 16 });
        ff::label (g, "PATTERN", { 776, 120, 100, 16 });
        ff::label (g, "STEP", { 776, 214, 100, 16 });
        ff::label (g, "OCTAVES", { 776, 276, 100, 16 });
        hint ("ORIGINAL + 1/4 IS THE PEDAL'S OWN ARP", { 776, 476, 198, 14 });

        ff::label (g, "RING + MIDI", { 1026, 92, 140, 16 });
        ff::label (g, "RING CARRIER", { 1026, 120, 160, 16 });
        hint ("FIXED CLANGS AGAINST YOUR NOTES", { 1026, 174, 198, 14 });
        ff::label (g, "VELOCITY", { 1026, 206, 100, 16 });
        ff::label (g, "PITCH BEND", { 1026, 268, 120, 16 });
        ff::label (g, "MIDI MONO", { 1026, 330, 120, 16 });
        hint ("LOW PRIORITY SUITS BASS LINES", { 1026, 384, 198, 14 });
        ff::label (g, "TEMPO FROM", { 1026, 418, 120, 16 });
        hint ("TAPPING SWITCHES BACK TO TAP", { 1026, 474, 198, 14 });
    }
    else
    {
        section (g, { 20, 76, 1220, 440 });
        ff::label (g, "FOOTSWITCHES", { 36, 92, 200, 16 });
        hint ("READ STRAIGHT FROM THE USB CABLE · NO MAPPING NEEDED", { 580, 93, 644, 14 }, Justification::centredRight);
        static const char* tapJobs[4] = { "BYPASS", "TAP TEMPO", "NEXT MODE", "SUSTAIN LATCH" };
        static const char* holdJobs[4] = { "SAW OR SQUARE", "RUNAWAY DELAY", "RING CHAOS", "SUSTAIN WHILE HELD" };
        auto& ps = proc.pedals();
        for (int i = 0; i < 4; ++i)
        {
            const int x = 40 + i * 300;
            auto row = [&] (const String& k, const String& v, int y)
            {
                ff::label (g, k, { x, y, 140, 18 }, Justification::centredLeft, ff::dim, 13.0f);
                ff::label (g, v, { x + 60, y, 218, 18 }, Justification::centredRight, ff::ink, 13.0f);
            };
            row ("TAP", tapJobs[i], 204);
            row ("HOLD", holdJobs[i], 226);
            row ("LISTENS FOR", "CC " + String (ps.fsCC[(size_t) i]) + (proc.pedalsDirect() ? String::fromUTF8 (" · USB") : String::fromUTF8 (" · CH ") + String (ps.fsChannel[(size_t) i])), 248);
        }
        auto combo = [&] (const String& k, const String& v, int y)
        {
            ff::label (g, k, { 36, y, 200, 18 }, Justification::centredLeft, ff::dim, 13.0f);
            ff::label (g, v, { 236, y, 380, 18 }, Justification::centredRight, ff::ink, 13.0f);
        };
        combo ("A + B", "NEXT PRESET", 322);
        combo ("C + D", "HAND FOOTSWITCHES TO THE NEXT CHANGELING", 346);
        combo ("C + D HELD 1 SECOND", "KEEPER", 370);
        ff::label (g, "EXPRESSION", { 36, 408, 140, 16 });
        hint ("CC " + String (ps.expCC) + (proc.pedalsDirect() ? String::fromUTF8 (" · USB · CLICK THE BAR TO CHOOSE WHAT IT SWEEPS") : String::fromUTF8 (" · CH ") + String (ps.expChannel) + String::fromUTF8 (" · CLICK THE BAR TO CHOOSE")), { 150, 409, 466, 14 });
        ff::label (g, "ORIGINAL PEDAL MIDI MAP", { 648, 322, 300, 16 });
        hint ("OFF BY DEFAULT: THE ENZO USES CC 20 TO 23 FOR KNOBS", { 648, 378, 576, 14 });
        ff::label (g, "KEEPER", { 648, 408, 120, 16 });
        hint ("SAVES THE LAST 8 SECONDS IN AND OUT, PLUS EVERY SETTING", { 740, 409, 484, 14 });
        hint ("TO DOCUMENTS / FLIP FELLA KEEPERS", { 648, 482, 576, 14 });
    }

    // ---- bottom strip ----
    section (g, { 20, 532, 1220, 112 });
    ff::label (g, "PRESETS", { 410, 548, 120, 16 });
    hint ("TAP TO LOAD · HOLD TO SAVE", { 486, 550, 196, 14 });
    ff::label (g, "FOOTSWITCHES", { 700, 548, 120, 16 });
    const bool directOk = proc.pedalsDirect();
    const Colour dot = directOk || proc.hostFeedsPedals.load() ? ff::green : ff::pin;
    g.setColour (dot.withAlpha (0.25f));
    g.fillEllipse (824.0f, 547.0f, 18.0f, 18.0f);
    g.setColour (dot);
    g.fillEllipse (828.0f, 551.0f, 10.0f, 10.0f);
    ff::label (g, footSay, { 848, 548, 200, 16 }, Justification::centredLeft, footSay == "READY" || footSay == "NOT CONNECTED" ? ff::dim : ff::ink, 12.0f);
}

void ChangelingEditor::resized() {}

void ChangelingEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = jlimit (0.0, 0.1, now - lastTick);
    lastTick = now;
    auto& s = proc.apvts;

    tempoCard.advance (dt);
    tempoCard.setValue (String (roundToInt (proc.currentBpm())));
    const int m = jlimit (0, 3, roundToInt (s.getRawParameterValue (ParamIDs::mode)->load()));
    modeCard->advance (dt);
    modeCard->setValue (names::modes[m].toUpperCase());
    for (auto* c : pedalCards) c->advance (dt);

    const String ml = names::modeLines[m];
    const String fn = names::filterTypes[jlimit (0, 5, roundToInt (s.getRawParameterValue (ParamIDs::filterType)->load()))].toUpperCase();
    static const char* steps[4] = { "QUARTERS OF TAP", "EIGHTHS OF TAP", "TRIPLETS OF TAP", "SIXTEENTHS OF TAP" };
    const String al = steps[jlimit (0, 3, roundToInt (s.getRawParameterValue (ParamIDs::arpStep)->load()))];
    String fs = proc.pedalsDirect() || proc.hostFeedsPedals.load() ? "READY" : "NOT CONNECTED";
    if (now - proc.uiMessageAt < 2.5 && proc.uiMessage.isNotEmpty()) fs = proc.uiMessage;
    if (ml != modeLine || fn != filterName || al != arpLine || fs != footSay)
    {
        modeLine = ml; filterName = fn; arpLine = al; footSay = fs;
        repaint();
    }
    if (page == 2) repaint (20, 76, 1220, 440);   // footswitch learn state, expression channel

    pebble->update (now, dt);
    tracking->tick (now, dt);
    for (auto* l : { voiceLights, echoLights, arpLights }) l->tick (now);
    curve->repaint();
    chaosBar->repaint();
    for (auto* f : feet) f->repaint();
    bypass->repaint();
    banks->repaint();
    if (page == 2) expBar->repaint();
}
