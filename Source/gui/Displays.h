#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "../dsp/VocalEngine.h"
#include "Theme.h"

namespace lavc
{
class VocalChainProcessor;

//==============================================================================
/** Scrolling piano-roll: detected pitch, corrected pitch and harmony voices over the scale. */
class PitchDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    void paintOverChildren (juce::Graphics& g) override { Hardware::drawGlass (g, getLocalBounds().toFloat()); }
    explicit PitchDisplay (VocalChainProcessor&);

    void addFrame (const PitchFrame& f);
    void setFrozen (bool f) { frozen = f; }
    void setTimeSpan (double s) { spanSeconds = juce::jlimit (1.0, 12.0, s); }
    double getTimeSpan() const { return spanSeconds; }
    void tick();

    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    std::function<void (double)> onSpanChanged;

private:
    juce::Rectangle<float> graphArea() const;
    float noteToY (float midi, juce::Rectangle<float> a) const;

    VocalChainProcessor& processor;
    std::atomic<float> *tuneOn, *harmOn, *key, *scale;
    static constexpr int ringSize = 1 << 13;
    std::vector<PitchFrame> ring;
    int64_t count = 0;
    int64_t latestPos = 0;
    double spanSeconds = 6.0;
    float centre = 60.0f, range = 26.0f;
    bool frozen = false;
};

//==============================================================================
/** EQ curve with draggable band nodes, Sculpt curve and a real-time spectrum. */
class EqDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    void paintOverChildren (juce::Graphics& g) override { Hardware::drawGlass (g, getLocalBounds().toFloat()); }
    explicit EqDisplay (VocalChainProcessor&);

    void pullSpectrum();
    void setSelectedBand (int b) { selected = b; repaint(); }
    std::function<void (int)> onBandSelected;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<float> plotArea() const;
    float freqToX (float f, juce::Rectangle<float> a) const;
    float xToFreq (float x, juce::Rectangle<float> a) const;
    float dbToY (float db, juce::Rectangle<float> a) const;
    float yToDb (float y, juce::Rectangle<float> a) const;
    int hitNode (juce::Point<float> p) const;
    juce::Point<float> nodePosition (int band, juce::Rectangle<float> a) const;
    juce::RangedAudioParameter* bandParam (int band, int which) const;

    VocalChainProcessor& processor;
    juce::AudioProcessorValueTreeState& state;

    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    std::vector<float> fifo, fftData, window, spectrumDb;
    int fifoPos = 0;

    int selected = 2, dragging = -1, hover = -1;
    static constexpr float minDb = -18.0f, maxDb = 18.0f;
};

//==============================================================================
/** Large read-out of the note being sung and how far off it is. */
class NotePanel : public juce::Component
{
public:
    void paintOverChildren (juce::Graphics& g) override { Hardware::drawGlass (g, getLocalBounds().toFloat().reduced (6.0f, 2.0f), 5.0f); }
    explicit NotePanel (VocalChainProcessor& p);
    void tick();
    void paint (juce::Graphics&) override;

private:
    VocalChainProcessor& processor;
    std::atomic<float> *tuneOn, *key, *scale;
    float inMidi = 0.0f, outMidi = 0.0f, shownCents = 0.0f;
    int silentTicks = 1000;
};

//==============================================================================
/** Signal-flow strip: every stage as a tile. Correction tiles can be dragged to change their order. */
class ChainStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum Page { pageCorrection = 0, pageTune = 1, pageEffects = 2 };

    explicit ChainStrip (VocalChainProcessor& p);
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override { layoutTiles(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    /** Called with (page, sectionKey) when a tile is clicked. */
    std::function<void (int, const juce::String&)> onTileClicked;
    std::function<void()> onOrderChanged;

private:
    struct Tile
    {
        juce::String key, name, enableId;
        juce::Colour colour;
        int page = 0;
        int module = -1; // orderable ModuleId, -1 = fixed
        juce::Rectangle<float> bounds;
    };

    void rebuildTiles();
    void layoutTiles();
    juce::String tileValue (const Tile& t) const;
    bool tileEnabled (const Tile& t) const;
    juce::Rectangle<float> powerRect (const Tile& t) const;

    VocalChainProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    std::vector<Tile> tiles;
    ModuleOrder order;
    int dragTile = -1, dragTarget = -1, hoverTile = -1;
    float dragX = 0.0f, dragOffset = 0.0f;
    bool dragMoved = false;
};

/** Colour used for each orderable module. */
juce::Colour moduleColour (ModuleId m);
} // namespace lavc
