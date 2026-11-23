#include "Widgets.h"

namespace lavc
{
//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& n,
            juce::Colour colour, bool bipolar)
    : name (n)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityBasedMode (false);
    slider.getProperties().set ("bipolar", bipolar);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramID, slider);

    if (auto* p = state.getParameter (paramID))
    {
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        slider.setTooltip (p->getName (64) + "  (double-click to reset)");
    }

    slider.onValueChange = [this] { repaint(); };
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (15);
    r.removeFromBottom (24); // value read-out + spacing to the next row
    const int size = juce::jmin (r.getWidth() - 8, r.getHeight(), 64);
    slider.setBounds (r.withSizeKeepingCentre (size, size));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (Colours::signature.withAlpha (isEnabled() ? 0.9f : 0.45f));
    {
        // Shrink long legends to fit the column rather than clipping them
        const auto caption = name.toUpperCase();
        const auto area = r.removeFromTop (14);
        auto font = legendFont (10.5f, 0.02f);
        for (float size = 10.5f; size > 8.5f && juce::GlyphArrangement::getStringWidth (font, caption) > (float) area.getWidth(); size -= 0.5f)
            font = legendFont (size - 0.5f, 0.0f);
        g.setFont (font);
        g.drawFittedText (caption, area, juce::Justification::centred, 1, 0.7f);
    }

    g.setColour (isEnabled() ? Colours::text : Colours::textDim);
    g.setFont (uiFont (12.0f));
    g.drawFittedText (slider.getTextFromValue (slider.getValue()),
                      r.withTop (slider.getBottom() + 1).withHeight (15), juce::Justification::centred, 1);
}

//==============================================================================
Choice::Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& c)
    : caption (c)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramID)))
    {
        box.addItemList (p->choices, 1);
        box.setTooltip (p->getName (64));
    }
    addAndMakeVisible (box);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramID, box);
}

void Choice::resized()
{
    auto r = getLocalBounds();
    if (caption.isNotEmpty())
        r.removeFromTop (15);
    box.setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 24)));
}

void Choice::paint (juce::Graphics& g)
{
    if (caption.isEmpty())
        return;
    g.setColour (Colours::signature.withAlpha (0.9f));
    g.setFont (legendFont (10.5f, 0.02f));
    g.drawFittedText (caption.toUpperCase(), getLocalBounds().removeFromTop (14), juce::Justification::centred, 1, 0.6f);
}

//==============================================================================
void PowerButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    // A round push switch in a metal bezel, with a lit lens when on
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float size = juce::jmin (r.getWidth(), r.getHeight());
    const auto c = r.withSizeKeepingCentre (size, size);
    const bool on = getToggleState();

    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillEllipse (c.expanded (1.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a6d72), c.getX(), c.getY(), juce::Colour (0xff1c1d1f),
                                             c.getRight(), c.getBottom(), false));
    g.fillEllipse (c);
    const auto lens = c.reduced (size * 0.16f).translated (0.0f, down ? 0.5f : 0.0f);
    Hardware::drawLed (g, lens, colour, on);

    const auto centre = lens.getCentre();
    const float rad = lens.getWidth() * 0.27f;
    juce::Path icon;
    icon.addCentredArc (centre.x, centre.y, rad, rad, 0.0f, 0.75f, juce::MathConstants<float>::twoPi - 0.75f, true);
    icon.startNewSubPath (centre.x, centre.y - rad - 1.0f);
    icon.lineTo (centre.x, centre.y - 0.5f);
    g.setColour (on ? juce::Colours::black.withAlpha (0.55f) : (over ? Colours::text : Colours::textDim).withAlpha (0.8f));
    g.strokePath (icon, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
Section::Section (const juce::String& t, juce::AudioProcessorValueTreeState* state, const juce::String& enableParamID,
                  juce::Colour c)
    : title (t), colour (c)
{
    if (state != nullptr && enableParamID.isNotEmpty())
    {
        power = std::make_unique<PowerButton> (c);
        addAndMakeVisible (*power);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (*state, enableParamID, *power);
        power->onStateChange = [this]
        {
            if (onActiveChanged)
                onActiveChanged();
            repaint();
        };
        if (auto* p = state->getParameter (enableParamID))
            power->setTooltip ("Enable " + p->getName (64));
    }
}

juce::Rectangle<int> Section::getContentArea() const
{
    return getLocalBounds().withTrimmedTop (28).reduced (8, 4);
}

void Section::resized()
{
    if (power != nullptr)
        power->setBounds (getWidth() - 26, 5, 18, 18);
}

void Section::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    Hardware::drawPlate (g, r, 5.0f, false);
    if (highlight > 0.0f)
    {
        g.setColour (colour.withAlpha (0.8f * highlight));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f + highlight);
    }

    // Small screws in the bottom corners, when the panel is tall enough
    if (getHeight() > 150)
        for (float x : { r.getX() + 7.0f, r.getRight() - 7.0f })
            Hardware::drawScrew (g, { x, r.getBottom() - 7.0f }, 2.6f);

    Hardware::drawLed (g, { 9.0f, 12.0f, 7.0f, 7.0f }, colour, isActive());
    g.setColour (Colours::signature.withAlpha (isActive() ? 1.0f : 0.5f));
    g.setFont (legendFont (12.5f));
    g.drawText (title.toUpperCase(), 21, 6, getWidth() - 52, 20, juce::Justification::centredLeft);

    // Engraved rule under the title
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawHorizontalLine (28, 8.0f, (float) getWidth() - 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawHorizontalLine (29, 8.0f, (float) getWidth() - 8.0f);
}

//==============================================================================
void LevelMeters::setLevels (float inPeak, float outPeak, float grDb)
{
    auto toDb = [] (float v) { return juce::Decibels::gainToDecibels (v, -100.0f); };
    const float inDb = toDb (inPeak), outDb = toDb (outPeak);

    // Smooth fall-off
    in = juce::jmax (inDb, in - 1.2f);
    out = juce::jmax (outDb, out - 1.2f);
    gr = juce::jmax (grDb, gr - 0.6f);

    if (inDb >= inHold || ++inHoldAge > 60) { inHold = inDb; inHoldAge = 0; }
    if (outDb >= outHold || ++outHoldAge > 60) { outHold = outDb; outHoldAge = 0; }
    repaint();
}

void LevelMeters::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (2, 0);
    const int labelH = 14;
    const int w = (r.getWidth() - 4) / 3;

    // LED ladder: lit segments up to the level, the rest dark but visible behind the lens
    auto drawBar = [&] (juce::Rectangle<int> area, float db, float hold, const juce::String& label, bool reduction)
    {
        g.setColour (Colours::signature.withAlpha (0.85f));
        g.setFont (legendFont (9.0f));
        g.drawText (label, area.removeFromBottom (labelH).expanded (4, 0), juce::Justification::centred, false);
        const auto slot = area.toFloat();
        Hardware::drawRecess (g, slot, 2.0f);

        const auto inner = slot.reduced (2.0f, 2.0f);
        const float seg = 3.0f, gap = 1.0f;
        const int n = juce::jmax (1, (int) ((inner.getHeight() + gap) / (seg + gap)));
        const float level = reduction ? juce::jlimit (0.0f, 1.0f, db / 24.0f) : juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        const float holdFrac = reduction ? 0.0f : juce::jlimit (0.0f, 1.0f, (hold + 60.0f) / 60.0f);
        const int holdSeg = holdFrac > 0.0f ? juce::jlimit (0, n - 1, (int) (holdFrac * (float) n)) : -1;

        for (int i = 0; i < n; ++i)
        {
            const float frac = ((float) i + 0.5f) / (float) n; // 0 = bottom
            juce::Rectangle<float> s;
            juce::Colour c;
            bool lit;
            if (reduction)
            {
                s = { inner.getX(), inner.getY() + (float) i * (seg + gap), inner.getWidth(), seg };
                c = Colours::danger;
                lit = frac <= level;
            }
            else
            {
                s = { inner.getX(), inner.getBottom() - (float) (i + 1) * (seg + gap) + gap, inner.getWidth(), seg };
                c = frac > 0.9f ? Colours::danger : (frac > 0.75f ? Colours::threshold : Colours::accent);
                lit = frac <= level || i == holdSeg;
            }
            g.setColour (lit ? c : c.withAlpha (0.1f));
            g.fillRect (s);
            if (lit)
            {
                g.setColour (c.withAlpha (0.25f));
                g.fillRect (s.expanded (1.0f, 0.5f));
            }
        }
    };

    drawBar (r.removeFromLeft (w), in, inHold, "IN", false);
    r.removeFromLeft (2);
    drawBar (r.removeFromLeft (w), out, outHold, "OUT", false);
    r.removeFromLeft (2);
    drawBar (r, gr, 0.0f, "GR", true);
}
} // namespace lavc
