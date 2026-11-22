#include "Theme.h"

namespace lavc
{
VocalChainLookAndFeel::VocalChainLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
    setColour (juce::ComboBox::backgroundColourId, Colours::panelLight);
    setColour (juce::ComboBox::outlineColourId, Colours::edge);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::arrowColourId, Colours::textDim);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panelLight);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::text);
    setColour (juce::TextButton::buttonColourId, Colours::panelLight);
    setColour (juce::TextButton::buttonOnColourId, Colours::accent);
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::TextButton::textColourOnId, Colours::background);
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    setColour (juce::Slider::trackColourId, Colours::accent);
    setColour (juce::TooltipWindow::backgroundColourId, Colours::panelLight);
    setColour (juce::TooltipWindow::textColourId, Colours::text);
    setColour (juce::TooltipWindow::outlineColourId, Colours::edge);
}

void VocalChainLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                             float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float trackWidth = juce::jmax (2.5f, radius * 0.12f);
    const float arcRadius = radius - trackWidth * 0.5f;
    const auto colour = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const bool enabled = slider.isEnabled();
    const float angle = startAngle + pos * (endAngle - startAngle);

    // Track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::graphBg);
    g.strokePath (track, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc (bipolar parameters fill from the centre)
    const bool bipolar = slider.getProperties().getWithDefault ("bipolar", false);
    const float fromAngle = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (fromAngle, angle),
                         juce::jmax (fromAngle, angle), true);
    g.setColour (enabled ? colour : colour.withSaturation (0.0f).withAlpha (0.5f));
    g.strokePath (value, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Knob body
    const float bodyRadius = arcRadius - trackWidth * 1.3f;
    juce::ColourGradient grad (Colours::panelLight.brighter (0.25f), centre.x, centre.y - bodyRadius,
                               Colours::panel.darker (0.3f), centre.x, centre.y + bodyRadius, false);
    g.setGradientFill (grad);
    g.fillEllipse (juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre));
    g.setColour (Colours::edge.brighter (0.2f));
    g.drawEllipse (juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre), 1.0f);

    // Pointer
    juce::Path pointer;
    const float pLen = bodyRadius * 0.62f;
    pointer.addRoundedRectangle (-1.5f, -bodyRadius + 3.0f, 3.0f, pLen, 1.5f);
    g.setColour (enabled ? Colours::text : Colours::textDim);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void VocalChainLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                             float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0, 0, style, slider);
        return;
    }

    const auto r = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto track = r.withSizeKeepingCentre (r.getWidth(), 6.0f);
    g.setColour (Colours::graphBg);
    g.fillRoundedRectangle (track, 3.0f);
    g.setColour (slider.findColour (juce::Slider::trackColourId).withAlpha (slider.isEnabled() ? 1.0f : 0.35f));
    g.fillRoundedRectangle (track.withRight (sliderPos), 3.0f);
    g.setColour (Colours::text);
    g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ sliderPos, r.getCentreY() }));
}

void VocalChainLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                                 bool over, bool down)
{
    auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();
    auto onColour = button.findColour (juce::TextButton::buttonOnColourId);
    auto base = on ? onColour : button.findColour (juce::TextButton::buttonColourId);
    if (over)
        base = base.brighter (0.12f);
    if (down)
        base = base.darker (0.15f);

    g.setColour (base);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? onColour.brighter (0.3f) : Colours::edge);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void VocalChainLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 2), juce::Justification::centred, 1);
}

juce::Font VocalChainLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return uiFont (juce::jmin (13.0f, (float) buttonHeight * 0.55f), true);
}

void VocalChainLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                         juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) width - 14.0f, ay = (float) height * 0.5f;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.5f);
    arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.strokePath (arrow, juce::PathStrokeType (1.5f));
}

void VocalChainLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}
} // namespace lavc
