#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lavc
{
namespace Colours
{
    // ---- Hardware: black brushed-metal chassis, silk-screened legends, glass screens ----------------
    inline const juce::Colour background  { 0xff0b0b0c };   // chassis
    inline const juce::Colour panel       { 0xff1b1c1e };   // brushed plate
    inline const juce::Colour panelLight  { 0xff27292c };   // raised caps, tiles
    inline const juce::Colour edge        { 0xff3a3d41 };
    inline const juce::Colour graphBg     { 0xff07090a };   // screen glass
    inline const juce::Colour grid        { 0xff18201f };
    inline const juce::Colour text        { 0xffece6d8 };   // silk-screen off-white
    inline const juce::Colour textDim     { 0xff8b8882 };

    /** The family colour of this unit: legends, knob scales, logo, screen tint and trim. */
    inline const juce::Colour signature   { 0xffc8a8ff };   // VocalChain: lavender

    inline const juce::Colour accent      { 0xff2fe0cf };   // teal: triggers, output
    inline const juce::Colour accentDim   { 0xff1a7d74 };
    inline const juce::Colour threshold   { 0xffffa53d };   // orange: thresholds
    inline const juce::Colour danger      { 0xffff4d6a };   // rejections, gain reduction
    inline const juce::Colour learn       { 0xffb08cff };   // learning
    inline const juce::Colour input       { 0xff5b6b82 };   // input waveform
    inline const juce::Colour detection   { 0xfff4e06d };   // detection envelope, yellow
    inline const juce::Colour blue        { 0xff5aa9ff };
    inline const juce::Colour pink        { 0xffff7ab8 };

    /** Colours for the four harmony voices. */
    inline juce::Colour forVoice (int v)
    {
        static const juce::Colour c[] = { juce::Colour (0xffb08cff), juce::Colour (0xffff7ab8), juce::Colour (0xff5aa9ff), juce::Colour (0xfff4e06d) };
        return c[((v % 4) + 4) % 4];
    }

    inline juce::Colour forVelocity (int velocity)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (float) velocity / 127.0f);
        return t < 0.5f ? accent.interpolatedWith (detection, t * 2.0f)
                        : detection.interpolatedWith (danger, (t - 0.5f) * 2.0f);
    }
} // namespace Colours

inline juce::Font uiFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

/** Silk-screened legend lettering: bold, slightly spaced. */
inline juce::Font legendFont (float size, float spacing = 0.06f)
{
    return juce::Font (juce::FontOptions (size, juce::Font::bold).withKerningFactor (spacing));
}

/** Drawing for the physical look shared by the LA family: brushed metal, screws, glass screens, LEDs. */
namespace Hardware
{
    /** Tileable grain used for the brushed metal. */
    const juce::Image& brushedTexture();
    /** Brushed black metal with a soft sheen. */
    void drawBrushedMetal (juce::Graphics&, juce::Rectangle<float> area, juce::Colour base, float corner = 0.0f);
    /** A raised, bevelled metal plate (module panel). Screws go in the corners when there is room. */
    void drawPlate (juce::Graphics&, juce::Rectangle<float> area, float corner = 5.0f, bool screws = true);
    /** A countersunk slotted screw. */
    void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius);
    /** The whole front panel: chassis, the header plate (height `headerHeight`) and its screws. */
    void drawChassis (juce::Graphics&, juce::Rectangle<int> area, int headerHeight);
    /** A recessed screen: bezel and dark glass. Draw the content afterwards, then drawGlass(). */
    void drawScreen (juce::Graphics&, juce::Rectangle<float> area, float corner = 6.0f);
    /** Reflection and inner shadow over a screen's content. */
    void drawGlass (juce::Graphics&, juce::Rectangle<float> area, float corner = 6.0f);
    /** A round indicator LED. */
    void drawLed (juce::Graphics&, juce::Rectangle<float> area, juce::Colour colour, bool lit);
    /** A recessed slot (meter windows, faders). */
    void drawRecess (juce::Graphics&, juce::Rectangle<float> area, float corner = 3.0f);
} // namespace Hardware

class VocalChainLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VocalChainLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return uiFont (13.0f); }
    juce::Font getPopupMenuFont() override { return uiFont (14.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
};
} // namespace lavc
