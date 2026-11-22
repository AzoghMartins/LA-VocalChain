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
    g.setColour (Colours::textDim);
    g.setFont (uiFont (11.0f, true));
    g.drawFittedText (name.toUpperCase(), r.removeFromTop (14), juce::Justification::centred, 1);

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
    g.setColour (Colours::textDim);
    g.setFont (uiFont (11.0f, true));
    g.drawFittedText (caption.toUpperCase(), getLocalBounds().removeFromTop (14), juce::Justification::centred, 1);
}

//==============================================================================
void PowerButton::paintButton (juce::Graphics& g, bool over, bool)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float size = juce::jmin (r.getWidth(), r.getHeight());
    const auto c = r.withSizeKeepingCentre (size, size);
    const bool on = getToggleState();

    g.setColour (on ? colour.withAlpha (0.18f) : Colours::graphBg);
    g.fillEllipse (c);
    g.setColour (on ? colour : (over ? Colours::text : Colours::textDim));
    g.drawEllipse (c, 1.0f);

    const auto centre = c.getCentre();
    const float rad = size * 0.26f;
    juce::Path icon;
    icon.addCentredArc (centre.x, centre.y, rad, rad, 0.0f, 0.75f, juce::MathConstants<float>::twoPi - 0.75f, true);
    icon.startNewSubPath (centre.x, centre.y - rad - 1.5f);
    icon.lineTo (centre.x, centre.y - 0.5f);
    g.strokePath (icon, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::edge.interpolatedWith (colour, highlight));
    g.drawRoundedRectangle (r, 6.0f, 1.0f + highlight);

    g.setColour (isActive() ? colour : Colours::textDim);
    g.fillRoundedRectangle (juce::Rectangle<float> (10.0f, 11.0f, 3.0f, 10.0f), 1.5f);
    g.setColour (isActive() ? Colours::text : Colours::textDim);
    g.setFont (uiFont (12.5f, true));
    g.drawText (title.toUpperCase(), 18, 6, getWidth() - 50, 20, juce::Justification::centredLeft);

    g.setColour (Colours::edge.withAlpha (0.6f));
    g.drawHorizontalLine (28, 8.0f, (float) getWidth() - 8.0f);
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

    auto drawBar = [&] (juce::Rectangle<int> area, float db, float hold, const juce::String& label, bool reduction)
    {
        g.setColour (Colours::textDim);
        g.setFont (uiFont (9.0f, true));
        g.drawText (label, area.removeFromBottom (labelH).expanded (4, 0), juce::Justification::centred, false);
        const auto bar = area.toFloat();
        g.setColour (Colours::graphBg);
        g.fillRoundedRectangle (bar, 2.0f);

        if (reduction)
        {
            const float frac = juce::jlimit (0.0f, 1.0f, db / 24.0f);
            g.setColour (Colours::danger);
            g.fillRoundedRectangle (bar.withHeight (bar.getHeight() * frac), 2.0f);
            return;
        }

        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        const auto fill = bar.withTrimmedTop (bar.getHeight() * (1.0f - frac));
        juce::ColourGradient grad (Colours::danger, bar.getX(), bar.getY(), Colours::accent, bar.getX(), bar.getBottom(), false);
        grad.addColour (0.12, Colours::threshold);
        grad.addColour (0.3, Colours::accent);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 2.0f);

        const float holdFrac = juce::jlimit (0.0f, 1.0f, (hold + 60.0f) / 60.0f);
        if (holdFrac > 0.0f)
        {
            g.setColour (Colours::text);
            g.fillRect (bar.getX(), bar.getBottom() - bar.getHeight() * holdFrac, bar.getWidth(), 1.5f);
        }
    };

    drawBar (r.removeFromLeft (w), in, inHold, "IN", false);
    r.removeFromLeft (2);
    drawBar (r.removeFromLeft (w), out, outHold, "OUT", false);
    r.removeFromLeft (2);
    drawBar (r, gr, 0.0f, "GR", true);
}
} // namespace lavc
