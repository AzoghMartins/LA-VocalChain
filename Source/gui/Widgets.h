#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

namespace lavc
{
/** Rotary knob with name above and value read-out below, bound to a parameter. */
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& name,
          juce::Colour colour = Colours::accent, bool bipolar = false);

    void paint (juce::Graphics&) override;
    void resized() override;
    void setName (const juce::String& n) { name = n; repaint(); }
    /** Overrides the parameter's value text (e.g. when it depends on another parameter). */
    void setFormatter (std::function<juce::String (double)> f) { slider.textFromValueFunction = std::move (f); repaint(); }

    juce::Slider slider;

private:
    juce::String name;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** Small round on/off switch drawn with a power icon. */
class PowerButton : public juce::ToggleButton
{
public:
    explicit PowerButton (juce::Colour onColour = Colours::accent) : colour (onColour) {}
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    juce::Colour colour;
};

/** Titled panel that groups controls. Optionally hosts a power switch bound to a bool parameter. */
class Section : public juce::Component
{
public:
    Section (const juce::String& title, juce::AudioProcessorValueTreeState* state = nullptr,
             const juce::String& enableParamID = {}, juce::Colour colour = Colours::accent);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Area below the title where children go (in this component's coordinates). */
    juce::Rectangle<int> getContentArea() const;
    bool isActive() const { return power == nullptr || power->getToggleState(); }
    juce::Colour getColour() const { return colour; }
    /** Briefly outlines the section (0..1), used when it is selected from the chain strip. */
    void setHighlight (float amount) { if (std::abs (amount - highlight) > 0.01f) { highlight = amount; repaint(); } }
    float getHighlight() const { return highlight; }

    std::unique_ptr<PowerButton> power;
    std::function<void()> onActiveChanged;

private:
    juce::String title;
    juce::Colour colour;
    float highlight = 0.0f;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

/** Vertical peak meters: input, output and gain reduction. */
class LevelMeters : public juce::Component
{
public:
    void setLevels (float inPeak, float outPeak, float grDb);
    void paint (juce::Graphics&) override;

private:
    float in = 0.0f, out = 0.0f, gr = 0.0f;
    float inHold = -100.0f, outHold = -100.0f;
    int inHoldAge = 0, outHoldAge = 0;
};

/** Combo box with a small caption above it, bound to a choice parameter. */
class Choice : public juce::Component
{
public:
    Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& caption = {});
    void paint (juce::Graphics&) override;
    void resized() override;

    juce::ComboBox box;

private:
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** Pill toggle/text button. */
class PillButton : public juce::TextButton
{
public:
    PillButton (const juce::String& buttonText, juce::Colour onColour = Colours::accent)
        : juce::TextButton (buttonText)
    {
        setColour (juce::TextButton::buttonOnColourId, onColour);
    }
};

/** Pill toggle bound to a bool parameter. */
class ParamPill : public PillButton
{
public:
    ParamPill (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& label,
               juce::Colour onColour = Colours::accent)
        : PillButton (label, onColour)
    {
        setClickingTogglesState (true);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramID, *this);
        if (auto* p = state.getParameter (paramID))
            setTooltip (p->getName (64));
    }

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};
} // namespace lavc
