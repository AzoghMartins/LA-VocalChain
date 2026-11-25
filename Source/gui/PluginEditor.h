#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../PluginProcessor.h"
#include "Displays.h"
#include "Theme.h"
#include "Widgets.h"

namespace lavc
{
/** One harmony voice: on/off, interval, level, pan and formant. */
class VoiceStrip : public juce::Component
{
public:
    VoiceStrip (juce::AudioProcessorValueTreeState& state, int voiceIndex);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() { interval.repaint(); }

private:
    int index;
    PowerButton power;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    Knob interval, level, pan, formant;
};

/** All UI content, laid out at a fixed base size and scaled by the editor. */
class MainPanel : public juce::Component
{
public:
    static constexpr int baseWidth = 1440, baseHeight = 780;

    explicit MainPanel (VocalChainProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

    void setPage (int page);
    int getPage() const { return currentPage; }

private:
    Knob* knob (Section& s, const char* id, const juce::String& name, juce::Colour c, bool bipolar = false);
    Choice* choice (Section& s, const char* id, const juce::String& caption = {});
    static void grid (std::initializer_list<juce::Component*> items, int columns, juce::Rectangle<int> area);
    void layoutPages();
    void layoutCorrection (juce::Rectangle<int> area);
    void selectEqBand (int band);
    void refreshPresetList();
    void showToast (const juce::String& text);
    void focusSection (const juce::String& key);

    VocalChainProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    std::vector<std::unique_ptr<juce::Component>> owned;

    // Header
    MenuBox presetBox;
    PillButton saveButton { "SAVE" }, mixButton { "MIXING" }, trackButton { "TRACKING", Colours::threshold };
    PillButton assistButton { "ASSIST", Colours::learn }, freezeButton { "FREEZE", Colours::learn };
    juce::File userPresetFiles[256];
    int numUserPresets = 0;

    // Displays
    ChainStrip chain;
    PitchDisplay pitchDisplay;
    EqDisplay eqDisplay;
    NotePanel notePanel;
    LevelMeters meters;

    // Pages
    PillButton tabCorrection { "CORRECTION" }, tabTune { "TUNE & VOICES" }, tabEffects { "EFFECTS" };
    int currentPage = 0;

    Section inputSection, outputSection;
    Section gateSection, levelSection, deessSection, sculptSection, eqSection, compSection, satSection;
    Section tuneSection, harmSection;
    Section dblSection, modSection, dlySection, revSection;
    std::map<juce::String, Section*> sectionByKey;

    std::vector<Knob*> inputKnobs, outputKnobs, gateKnobs, levelKnobs, deessKnobs, sculptKnobs, compKnobs, satKnobs;
    std::vector<Knob*> tuneKnobs, harmKnobs, dblKnobs, modKnobs, dlyKnobs, revKnobs;
    std::array<std::array<Knob*, 3>, kEqBands> eqKnobs {};
    std::array<std::unique_ptr<PillButton>, kEqBands> eqBandButtons;
    int eqBand = 2;
    Choice *deessMode = nullptr, *sculptStyle = nullptr, *compMode = nullptr, *satMode = nullptr;
    Choice *tuneKey = nullptr, *tuneScale = nullptr, *tuneTarget = nullptr, *harmMode = nullptr;
    Choice *modMode = nullptr, *dlyMode = nullptr, *dlySync = nullptr, *revType = nullptr;
    std::unique_ptr<ParamPill> phasePill, slopePill, listenPill, limitPill;
    PillButton learnKeyButton { "LEARN KEY", Colours::learn };
    std::array<std::unique_ptr<VoiceStrip>, kNumHarmonyVoices> voices;
    Knob* dlyTimeKnob = nullptr;
    ModuleOrder shownOrder {};

    int blink = 0, toastTicks = 0;
    juce::String infoText, toastText;
    juce::String highlightKey;
    float highlightAmount = 0.0f;
};

class VocalChainEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VocalChainEditor (VocalChainProcessor&);
    ~VocalChainEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Pulls new data from the audio thread and refreshes the displays (called at 60 Hz). */
    void updateDisplays() { panel.tick(); }
    MainPanel& getPanel() { return panel; }

private:
    void timerCallback() override { updateDisplays(); }

    VocalChainLookAndFeel lookAndFeel;
    MainPanel panel;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChainEditor)
};
} // namespace lavc
