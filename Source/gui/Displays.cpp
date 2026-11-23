#include "Displays.h"
#include "../PluginProcessor.h"

namespace lavc
{
namespace
{
juce::String noteName (int midi) { return juce::MidiMessage::getMidiNoteName (midi, true, true, 4); }

bool noteInScale (int note, int key, int scale)
{
    const uint16_t mask = scaleMask ((Scale) scale);
    return (mask >> ((((note - key) % 12) + 12) % 12)) & 1u;
}

void drawLegend (juce::Graphics& g, juce::Rectangle<int> area, std::initializer_list<std::pair<juce::String, juce::Colour>> items)
{
    g.setFont (uiFont (10.5f, true));
    int x = area.getRight();
    std::vector<std::pair<juce::String, juce::Colour>> list (items);
    for (auto it = list.rbegin(); it != list.rend(); ++it)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), it->first) + 26;
        x -= w;
        g.setColour (it->second);
        g.fillRoundedRectangle ((float) x, (float) area.getCentreY() - 2.0f, 12.0f, 4.0f, 2.0f);
        g.setColour (Colours::textDim);
        g.drawText (it->first, x + 16, area.getY(), w - 16, area.getHeight(), juce::Justification::centredLeft);
    }
}
} // namespace

juce::Colour moduleColour (ModuleId m)
{
    switch (m)
    {
        case ModuleId::gate:   return Colours::accent;
        case ModuleId::level:  return Colours::blue;
        case ModuleId::deess:  return Colours::pink;
        case ModuleId::sculpt: return Colours::learn;
        case ModuleId::eq:     return Colours::threshold;
        case ModuleId::comp:   return Colours::danger;
        case ModuleId::sat:    return Colours::detection;
        case ModuleId::numModules: break;
    }
    return Colours::accent;
}

//==============================================================================
PitchDisplay::PitchDisplay (VocalChainProcessor& p) : processor (p), ring ((size_t) ringSize)
{
    auto& s = processor.getState();
    tuneOn = s.getRawParameterValue (ParamID::tuneOn);
    harmOn = s.getRawParameterValue (ParamID::harmOn);
    key = s.getRawParameterValue (ParamID::tuneKey);
    scale = s.getRawParameterValue (ParamID::tuneScale);
    setOpaque (true);
}

void PitchDisplay::addFrame (const PitchFrame& f)
{
    if (frozen)
        return;
    if (f.position < latestPos - (int64_t) (processor.getEngineSampleRate() * 2.0)) // engine was reset
        count = 0;
    ring[(size_t) (count & (ringSize - 1))] = f;
    ++count;
    latestPos = f.position;
}

void PitchDisplay::tick()
{
    if (frozen || count == 0)
        return;
    // Follow the singer: centre the view on the recent pitch range
    const double sr = processor.getEngineSampleRate();
    const int64_t from = latestPos - (int64_t) (spanSeconds * sr);
    float lo = 1000.0f, hi = -1000.0f;
    for (int64_t i = std::max<int64_t> (0, count - ringSize); i < count; ++i)
    {
        const auto& f = ring[(size_t) (i & (ringSize - 1))];
        if (f.position < from || ! f.voiced)
            continue;
        lo = std::min ({ lo, f.inputMidi, f.outputMidi });
        hi = std::max ({ hi, f.inputMidi, f.outputMidi });
        for (float h : f.harmonyMidi)
            if (h > 0.0f) { lo = std::min (lo, h); hi = std::max (hi, h); }
    }
    if (hi > lo)
    {
        const float wantRange = juce::jlimit (14.0f, 48.0f, hi - lo + 8.0f);
        range += (wantRange - range) * 0.05f;
        centre += ((lo + hi) * 0.5f - centre) * 0.06f;
    }
}

juce::Rectangle<float> PitchDisplay::graphArea() const
{
    auto r = getLocalBounds().toFloat();
    r.removeFromLeft (36.0f);
    r.removeFromRight (4.0f);
    r.removeFromTop (22.0f);
    r.removeFromBottom (36.0f);
    return r;
}

float PitchDisplay::noteToY (float midi, juce::Rectangle<float> a) const
{
    const float lo = centre - range * 0.5f;
    return a.getBottom() - (midi - lo) / range * a.getHeight();
}

void PitchDisplay::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    setTimeSpan (spanSeconds * (w.deltaY > 0 ? 0.85 : 1.0 / 0.85));
    if (onSpanChanged)
        onSpanChanged (spanSeconds);
    repaint();
}

void PitchDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    Hardware::drawScreen (g, bounds);

    const auto a = graphArea();
    const int k = juce::roundToInt (key->load()), sc = juce::roundToInt (scale->load());
    const float lo = centre - range * 0.5f, hi = centre + range * 0.5f;
    const float rowH = a.getHeight() / range;

    // Piano-roll rows
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (a.toNearestInt());
        for (int n = (int) std::floor (lo) - 1; n <= (int) std::ceil (hi) + 1; ++n)
        {
            const float y = noteToY ((float) n + 0.5f, a);
            const bool in = noteInScale (n, k, sc);
            const bool root = (((n - k) % 12) + 12) % 12 == 0;
            g.setColour (root ? Colours::accent.withAlpha (0.10f) : (in ? Colours::panelLight.withAlpha (0.55f) : Colours::graphBg));
            g.fillRect (a.getX(), y, a.getWidth(), rowH - 1.0f);
        }
    }
    g.setFont (uiFont (9.5f));
    for (int n = (int) std::ceil (lo); n <= (int) std::floor (hi); ++n)
    {
        const bool in = noteInScale (n, k, sc);
        if (! in && rowH < 11.0f)
            continue;
        if (rowH < 7.0f && n % 12 != k)
            continue;
        const float y = noteToY ((float) n, a);
        g.setColour (in ? Colours::text.withAlpha (0.75f) : Colours::textDim.withAlpha (0.5f));
        g.drawText (noteName (n), 2, (int) (y - 6.0f), 32, 12, juce::Justification::centredRight);
    }

    // Time grid
    const double sr = std::max (1.0, processor.getEngineSampleRate());
    const double endT = (double) latestPos / sr, startT = endT - spanSeconds;
    const double step = spanSeconds > 6.0 ? 2.0 : (spanSeconds > 3.0 ? 1.0 : 0.5);
    g.setFont (uiFont (10.0f));
    for (double t = std::ceil (startT / step) * step; t <= endT; t += step)
    {
        const float x = a.getX() + (float) ((t - startT) / spanSeconds) * a.getWidth();
        g.setColour (Colours::grid);
        g.drawVerticalLine ((int) x, a.getY(), a.getBottom() + 18.0f);
        g.setColour (Colours::textDim);
        g.drawText (juce::String (t, 1) + "s", (int) x - 25, (int) a.getBottom() + 20, 50, 14, juce::Justification::centred);
    }

    auto xFor = [&] (int64_t pos) { return a.getX() + (float) (((double) pos / sr - startT) / spanSeconds) * a.getWidth(); };
    const int64_t first = std::max<int64_t> (0, count - ringSize);
    const int64_t fromPos = latestPos - (int64_t) (spanSeconds * sr) - (int64_t) (0.1 * sr);
    const int64_t maxGap = (int64_t) (0.04 * sr);

    // Level strip
    auto lane = juce::Rectangle<float> (a.getX(), a.getBottom() + 3.0f, a.getWidth(), 14.0f);
    g.setColour (Colours::panel);
    g.fillRect (lane);
    {
        juce::Path level;
        bool started = false;
        for (int64_t i = first; i < count; ++i)
        {
            const auto& f = ring[(size_t) (i & (ringSize - 1))];
            if (f.position < fromPos) continue;
            const float v = juce::jlimit (0.0f, 1.0f, (f.levelDb + 70.0f) / 70.0f);
            const float x = xFor (f.position), y = lane.getBottom() - v * lane.getHeight();
            if (! started) { level.startNewSubPath (x, lane.getBottom()); started = true; }
            level.lineTo (x, y);
        }
        if (started)
        {
            level.lineTo (xFor (latestPos), lane.getBottom());
            level.closeSubPath();
            g.setColour (Colours::input.withAlpha (0.6f));
            g.fillPath (level);
        }
    }

    // Pitch curves
    g.saveState();
    g.reduceClipRegion (a.toNearestInt());

    auto drawCurve = [&] (auto valueOf, juce::Colour colour, float thickness)
    {
        juce::Path p;
        bool open = false;
        int64_t lastPos = 0;
        float lastV = 0.0f;
        for (int64_t i = first; i < count; ++i)
        {
            const auto& f = ring[(size_t) (i & (ringSize - 1))];
            if (f.position < fromPos) continue;
            const float v = valueOf (f);
            if (! f.voiced || v <= 0.0f)
            {
                open = false;
                continue;
            }
            const float x = xFor (f.position), y = noteToY (v, a);
            if (! open || f.position - lastPos > maxGap || std::abs (v - lastV) > 2.5f)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
            open = true;
            lastPos = f.position;
            lastV = v;
        }
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    if (harmOn->load() > 0.5f)
        for (int v = 0; v < kNumHarmonyVoices; ++v)
            drawCurve ([v] (const PitchFrame& f) { return f.harmonyMidi[(size_t) v]; }, Colours::forVoice (v).withAlpha (0.8f), 1.8f);

    const bool tuning = tuneOn->load() > 0.5f;
    drawCurve ([] (const PitchFrame& f) { return f.inputMidi; }, tuning ? Colours::textDim.withAlpha (0.8f) : Colours::accent, tuning ? 1.4f : 2.2f);
    if (tuning)
        drawCurve ([] (const PitchFrame& f) { return f.outputMidi; }, Colours::accent, 2.4f);

    // Now line
    g.setColour (Colours::text.withAlpha (0.15f));
    g.drawVerticalLine ((int) a.getRight() - 1, a.getY(), a.getBottom());

    // Legend
    g.restoreState();
    g.setFont (legendFont (11.0f));
    g.setColour (Colours::signature);
    g.drawText (juce::String (keyName (k)) + " " + scaleName ((Scale) sc), 40, 3, 200, 18, juce::Justification::centredLeft);
    drawLegend (g, { 0, 3, getWidth() - 10, 18 },
                { { "INPUT", tuning ? Colours::textDim : Colours::accent }, { "TUNED", Colours::accent },
                  { "HARMONY", Colours::learn }, { "LEVEL", Colours::input } });
    if (frozen)
    {
        g.setColour (Colours::learn);
        g.drawText ("FROZEN", (int) a.getX() + 8, (int) a.getY() + 4, 80, 14, juce::Justification::centredLeft);
    }
}

//==============================================================================
EqDisplay::EqDisplay (VocalChainProcessor& p) : processor (p), state (p.getState())
{
    fifo.assign ((size_t) fftSize, 0.0f);
    fftData.assign ((size_t) fftSize * 2, 0.0f);
    window.resize ((size_t) fftSize);
    spectrumDb.assign ((size_t) fftSize / 2, -100.0f);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (fftSize - 1));
    setOpaque (true);
    setTooltip ("Drag a node to set frequency and gain, mouse wheel for Q / slope, double-click to reset");
}

juce::RangedAudioParameter* EqDisplay::bandParam (int band, int which) const
{
    static const char* ids[5][3] = { { ParamID::eq1Freq, ParamID::eq1Gain, ParamID::eq1Q }, { ParamID::eq2Freq, ParamID::eq2Gain, ParamID::eq2Q },
                                     { ParamID::eq3Freq, ParamID::eq3Gain, ParamID::eq3Q }, { ParamID::eq4Freq, ParamID::eq4Gain, ParamID::eq4Q },
                                     { ParamID::eq5Freq, ParamID::eq5Gain, ParamID::eq5Q } };
    return state.getParameter (ids[band][which]);
}

void EqDisplay::pullSpectrum()
{
    float tmp[2048];
    int got = 0;
    while ((got = processor.spectrumFifo.pop (tmp, 2048)) > 0)
    {
        for (int i = 0; i < got; ++i)
        {
            fifo[(size_t) fifoPos] = tmp[i];
            fifoPos = (fifoPos + 1) % fftSize;
        }
    }
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = fifo[(size_t) ((fifoPos + i) % fftSize)] * window[(size_t) i];
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
    for (int i = 0; i < fftSize / 2; ++i)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[(size_t) i] / (float) (fftSize / 4), -120.0f);
        auto& s = spectrumDb[(size_t) i];
        s = db > s ? s + (db - s) * 0.5f : s + (db - s) * 0.12f;
    }
    repaint();
}

juce::Rectangle<float> EqDisplay::plotArea() const
{
    auto r = getLocalBounds().toFloat();
    r.removeFromTop (22.0f);
    r.removeFromBottom (18.0f);
    r.removeFromLeft (30.0f);
    r.removeFromRight (6.0f);
    return r;
}

float EqDisplay::freqToX (float f, juce::Rectangle<float> a) const
{
    return a.getX() + a.getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
}

float EqDisplay::xToFreq (float x, juce::Rectangle<float> a) const
{
    return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth()));
}

float EqDisplay::dbToY (float db, juce::Rectangle<float> a) const
{
    return juce::jmap (db, maxDb, minDb, a.getY(), a.getBottom());
}

float EqDisplay::yToDb (float y, juce::Rectangle<float> a) const
{
    return juce::jmap (juce::jlimit (a.getY(), a.getBottom(), y), a.getY(), a.getBottom(), maxDb, minDb);
}

juce::Point<float> EqDisplay::nodePosition (int band, juce::Rectangle<float> a) const
{
    const auto* f = bandParam (band, 0);
    const auto* gn = bandParam (band, 1);
    return { freqToX (f->convertFrom0to1 (f->getValue()), a), dbToY (gn->convertFrom0to1 (gn->getValue()), a) };
}

int EqDisplay::hitNode (juce::Point<float> p) const
{
    const auto a = plotArea();
    for (int b = 0; b < kEqBands; ++b)
        if (nodePosition (b, a).getDistanceFrom (p) < 10.0f)
            return b;
    return -1;
}

void EqDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EqDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = hitNode (e.position);
    if (dragging < 0)
        return;
    selected = dragging;
    if (onBandSelected)
        onBandSelected (selected);
    bandParam (dragging, 0)->beginChangeGesture();
    bandParam (dragging, 1)->beginChangeGesture();
    if (auto* on = state.getParameter (ParamID::eqOn); on != nullptr && on->getValue() < 0.5f)
        on->setValueNotifyingHost (1.0f);
}

void EqDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    const auto a = plotArea();
    auto* f = bandParam (dragging, 0);
    auto* gn = bandParam (dragging, 1);
    f->setValueNotifyingHost (f->convertTo0to1 (f->getNormalisableRange().snapToLegalValue (xToFreq (e.position.x, a))));
    gn->setValueNotifyingHost (gn->convertTo0to1 (yToDb (e.position.y, a)));
    repaint();
}

void EqDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
    {
        bandParam (dragging, 0)->endChangeGesture();
        bandParam (dragging, 1)->endChangeGesture();
    }
    dragging = -1;
}

void EqDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = hitNode (e.position);
    if (b < 0)
        return;
    auto* gn = bandParam (b, 1);
    gn->beginChangeGesture();
    gn->setValueNotifyingHost (gn->getDefaultValue());
    gn->endChangeGesture();
}

void EqDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int b = hitNode (e.position);
    if (b < 0) b = selected;
    auto* q = bandParam (b, 2);
    const float norm = juce::jlimit (0.0f, 1.0f, q->getValue() + (w.deltaY > 0 ? 0.04f : -0.04f));
    q->beginChangeGesture();
    q->setValueNotifyingHost (norm);
    q->endChangeGesture();
}

void EqDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    Hardware::drawScreen (g, bounds);

    const auto a = plotArea();
    const double sr = std::max (8000.0, processor.getEngineSampleRate());

    // Grid
    g.setFont (uiFont (9.5f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = freqToX (f, a);
        g.setColour (Colours::grid);
        g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        g.setColour (Colours::textDim);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), (int) x - 20,
                    (int) a.getBottom() + 3, 40, 12, juce::Justification::centred);
    }
    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        const float y = dbToY (db, a);
        g.setColour (std::abs (db) < 0.1f ? Colours::edge : Colours::grid);
        g.drawHorizontalLine ((int) y, a.getX(), a.getRight());
        g.setColour (Colours::textDim);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), 0, (int) y - 6, 26, 12, juce::Justification::centredRight);
    }

    g.saveState();
    g.reduceClipRegion (a.toNearestInt());

    // Spectrum (output)
    {
        juce::Path sp;
        sp.startNewSubPath (a.getX(), a.getBottom());
        const int bins = fftSize / 2;
        for (int px = 0; px <= (int) a.getWidth(); px += 2)
        {
            const float f = xToFreq (a.getX() + (float) px, a);
            const float bin = f / (float) sr * (float) fftSize;
            const int b0 = juce::jlimit (1, bins - 2, (int) bin);
            const float db = juce::jmax (spectrumDb[(size_t) b0], spectrumDb[(size_t) b0 + 1]) + 4.5f * std::log2 (f / 1000.0f);
            const float y = juce::jmap (juce::jlimit (-90.0f, 0.0f, db), -90.0f, 0.0f, a.getBottom(), a.getY());
            sp.lineTo (a.getX() + (float) px, y);
        }
        sp.lineTo (a.getRight(), a.getBottom());
        sp.closeSubPath();
        g.setGradientFill (juce::ColourGradient (Colours::input.withAlpha (0.45f), 0.0f, a.getY(), Colours::input.withAlpha (0.05f), 0.0f, a.getBottom(), false));
        g.fillPath (sp);
    }

    // Response curves
    const bool eqOn = state.getRawParameterValue (ParamID::eqOn)->load() > 0.5f;
    std::array<Biquad, kEqBands> bands;
    for (int b = 0; b < kEqBands; ++b)
    {
        auto val = [&] (int which) { auto* p = bandParam (b, which); return p->convertFrom0to1 (p->getValue()); };
        EqModule::design (bands[(size_t) b], sr, b, val (0), val (1), val (2));
    }
    const float lowCut = state.getRawParameterValue (ParamID::lowCut)->load();
    const bool lc24 = state.getRawParameterValue (ParamID::lowCutSlope)->load() > 0.5f;
    Biquad lc1, lc2;
    lc1.setHighPass (sr, lowCut, lc24 ? 0.54119610 : 0.70710678);
    if (lc24) lc2.setHighPass (sr, lowCut, 1.30656296);
    std::array<Biquad, kSculptBands> sculpt;
    const bool sculptOn = state.getRawParameterValue (ParamID::sculptOn)->load() > 0.5f;
    for (int b = 0; b < kSculptBands; ++b)
        sculpt[(size_t) b].setPeak (sr, SculptModule::bandFreqs[(size_t) b], 1.1, processor.meters.sculpt[(size_t) b].load());

    juce::Path eqPath, sculptPath;
    for (int px = 0; px <= (int) a.getWidth(); px += 2)
    {
        const float f = xToFreq (a.getX() + (float) px, a);
        double db = 0.0;
        if (eqOn)
            for (auto& b : bands) db += b.magnitudeDb (sr, f);
        if (lowCut > 20.5f)
            db += lc1.magnitudeDb (sr, f) + (lc24 ? lc2.magnitudeDb (sr, f) : 0.0);
        const float y = dbToY ((float) juce::jlimit (-40.0, 40.0, db), a);
        if (px == 0) eqPath.startNewSubPath (a.getX(), y); else eqPath.lineTo (a.getX() + (float) px, y);

        double sdb = 0.0;
        for (auto& b : sculpt) sdb += b.magnitudeDb (sr, f);
        const float sy = dbToY ((float) sdb, a);
        if (px == 0) sculptPath.startNewSubPath (a.getX(), sy); else sculptPath.lineTo (a.getX() + (float) px, sy);
    }

    if (sculptOn)
    {
        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.6f).createDashedStroke (dashed, sculptPath, dashes, 2);
        g.setColour (Colours::learn.withAlpha (0.9f));
        g.fillPath (dashed);
    }

    {
        juce::Path fill (eqPath);
        const float zero = dbToY (0.0f, a);
        fill.lineTo (a.getRight(), zero);
        fill.lineTo (a.getX(), zero);
        fill.closeSubPath();
        g.setColour (Colours::threshold.withAlpha (eqOn ? 0.12f : 0.05f));
        g.fillPath (fill);
        g.setColour (eqOn ? Colours::threshold : Colours::textDim);
        g.strokePath (eqPath, juce::PathStrokeType (2.0f));
    }

    // Nodes
    static const char* labels[kEqBands] = { "L", "LM", "M", "P", "A" };
    for (int b = 0; b < kEqBands; ++b)
    {
        const auto pos = nodePosition (b, a);
        const bool sel = b == selected, hov = b == hover || b == dragging;
        const float r = sel || hov ? 8.0f : 6.5f;
        g.setColour (sel ? Colours::threshold : Colours::panelLight);
        g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (pos));
        g.setColour (Colours::threshold.withAlpha (eqOn ? 1.0f : 0.4f));
        g.drawEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (pos), 1.3f);
        g.setColour (sel ? Colours::background : Colours::text);
        g.setFont (uiFont (8.5f, true));
        g.drawText (labels[b], juce::Rectangle<float> (20, 12).withCentre (pos), juce::Justification::centred);
    }

    g.restoreState();
    g.setFont (legendFont (11.0f));
    g.setColour (Colours::signature);
    g.drawText ("EQ / SPECTRUM", 10, 3, 140, 18, juce::Justification::centredLeft);
    drawLegend (g, { 0, 3, getWidth() - 10, 18 },
                { { "EQ", Colours::threshold }, { "SCULPT", Colours::learn }, { "OUTPUT", Colours::input } });
}

//==============================================================================
NotePanel::NotePanel (VocalChainProcessor& p) : processor (p)
{
    auto& s = processor.getState();
    tuneOn = s.getRawParameterValue (ParamID::tuneOn);
    key = s.getRawParameterValue (ParamID::tuneKey);
    scale = s.getRawParameterValue (ParamID::tuneScale);
}

void NotePanel::tick()
{
    if (processor.currentVoiced.load())
    {
        inMidi = processor.currentInputMidi.load();
        outMidi = processor.currentOutputMidi.load();
        silentTicks = 0;
    }
    else
        ++silentTicks;
    const int target = quantizeToScale (inMidi, juce::roundToInt (key->load()), (Scale) juce::roundToInt (scale->load()));
    const float cents = (inMidi - (float) target) * 100.0f;
    shownCents += (cents - shownCents) * 0.3f;
    repaint();
}

void NotePanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (12, 6);
    const bool active = silentTicks < 30 && inMidi > 0.0f;
    const bool tuning = tuneOn->load() > 0.5f;
    const float shown = tuning ? outMidi : inMidi;
    const int k = juce::roundToInt (key->load()), sc = juce::roundToInt (scale->load());
    const int target = quantizeToScale (inMidi, k, (Scale) sc);

    Hardware::drawScreen (g, getLocalBounds().toFloat().reduced (6.0f, 2.0f), 5.0f);

    auto top = r.removeFromTop (16);
    g.setFont (uiFont (10.5f, true));
    g.setColour (Colours::textDim);
    g.drawText (tuning ? "TUNED NOTE" : "SUNG NOTE", top, juce::Justification::centredLeft);

    g.setFont (uiFont (46.0f, true));
    g.setColour (active ? (tuning ? Colours::accent : Colours::text) : Colours::textDim.withAlpha (0.4f));
    const auto noteText = active ? noteName (juce::roundToInt (shown)) : juce::String ("--");
    g.drawText (noteText, r.removeFromTop (54), juce::Justification::centredLeft);

    // Cents meter: where the singer is relative to the nearest scale note
    auto meter = r.removeFromTop (22).toFloat().reduced (0.0f, 6.0f);
    g.setColour (Colours::graphBg);
    g.fillRoundedRectangle (meter, 4.0f);
    const float cx = meter.getCentreX();
    g.setColour (Colours::edge);
    g.drawVerticalLine ((int) cx, meter.getY() - 3.0f, meter.getBottom() + 3.0f);
    if (active)
    {
        const float c = juce::jlimit (-50.0f, 50.0f, shownCents);
        const float x = cx + c / 50.0f * meter.getWidth() * 0.5f;
        const auto col = std::abs (c) < 10.0f ? Colours::accent : (std::abs (c) < 25.0f ? Colours::detection : Colours::danger);
        g.setColour (col.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (x, cx), meter.getY(), std::max (x, cx), meter.getBottom()));
        g.setColour (col);
        g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, meter.getHeight() + 6.0f).withCentre ({ x, meter.getCentreY() }), 2.0f);
    }
    g.setFont (uiFont (9.0f));
    g.setColour (Colours::textDim);
    auto lbl = r.removeFromTop (12);
    g.drawText ("-50", lbl, juce::Justification::centredLeft);
    g.drawText ("cents", lbl, juce::Justification::centred);
    g.drawText ("+50", lbl, juce::Justification::centredRight);

    r.removeFromTop (8);
    g.setFont (uiFont (11.5f));
    auto line = [&] (const juce::String& a, const juce::String& b, juce::Colour c)
    {
        auto row = r.removeFromTop (18);
        g.setColour (Colours::textDim);
        g.drawText (a, row.removeFromLeft (58), juce::Justification::centredLeft);
        g.setColour (c);
        g.drawText (b, row, juce::Justification::centredLeft);
    };
    const float hz = 440.0f * std::pow (2.0f, (inMidi - 69.0f) / 12.0f);
    line ("INPUT", active ? juce::String (hz, 1) + " Hz  " + (shownCents >= 0 ? "+" : "") + juce::String (juce::roundToInt (shownCents)) + " ct"
                          : juce::String ("no pitch"), Colours::text);
    line ("TARGET", active ? noteName (target) : juce::String ("-"), Colours::text);
    line ("KEY", juce::String (keyName (k)) + " " + scaleName ((Scale) sc), Colours::accent);
    const auto est = processor.getLastKeyEstimate();
    if (est.seconds > 0.0f)
        line ("FOUND", juce::String (keyName (est.key)) + (est.minor ? " minor" : " major"), Colours::learn);
}

//==============================================================================
ChainStrip::ChainStrip (VocalChainProcessor& p) : processor (p), state (p.getState())
{
    order = processor.getModuleOrder();
    rebuildTiles();
    setTooltip ("Click a stage to edit it, click its dot to switch it on/off, drag the correction stages to change their order");
}

void ChainStrip::rebuildTiles()
{
    tiles.clear();
    tiles.push_back ({ "input", "INPUT", {}, Colours::textDim, pageCorrection, -1, {} });
    tiles.push_back ({ "tune", "TUNE", ParamID::tuneOn, Colours::accent, pageTune, -1, {} });
    tiles.push_back ({ "harm", "VOICES", ParamID::harmOn, Colours::learn, pageTune, -1, {} });
    for (int m : order)
        tiles.push_back ({ juce::String ("m") + juce::String (m), moduleShortName ((ModuleId) m), moduleEnableID ((ModuleId) m),
                           moduleColour ((ModuleId) m), pageCorrection, m, {} });
    tiles.push_back ({ "doubler", "DOUBLE", ParamID::dblOn, Colours::learn, pageEffects, -1, {} });
    tiles.push_back ({ "mod", "MOD", ParamID::modOn, Colours::blue, pageEffects, -1, {} });
    tiles.push_back ({ "delay", "DELAY", ParamID::dlyOn, Colours::detection, pageEffects, -1, {} });
    tiles.push_back ({ "reverb", "REVERB", ParamID::revOn, Colours::accent, pageEffects, -1, {} });
    tiles.push_back ({ "output", "OUTPUT", {}, Colours::textDim, pageCorrection, -1, {} });
    layoutTiles();
}

void ChainStrip::layoutTiles()
{
    const float gap = 12.0f;
    const float w = ((float) getWidth() - gap * (float) (tiles.size() - 1)) / (float) tiles.size();
    float x = 0.0f;
    for (auto& t : tiles)
    {
        t.bounds = { x, 0.0f, w, (float) getHeight() };
        x += w + gap;
    }
}

juce::Rectangle<float> ChainStrip::powerRect (const Tile& t) const
{
    return { t.bounds.getX() + 7.0f, t.bounds.getY() + 8.0f, 11.0f, 11.0f };
}

bool ChainStrip::tileEnabled (const Tile& t) const
{
    if (t.enableId.isEmpty())
        return true;
    if (t.key == "m0") // gate tile also hosts breath control
        return state.getRawParameterValue (ParamID::gateOn)->load() > 0.5f || state.getRawParameterValue (ParamID::breath)->load() < -0.1f;
    return state.getRawParameterValue (t.enableId)->load() > 0.5f;
}

juce::String ChainStrip::tileValue (const Tile& t) const
{
    auto db = [] (float v) { return (v > 0.05f ? "-" : "") + juce::String (std::abs (v), 1) + " dB"; };
    auto& m = processor.meters;
    if (t.key == "input") return juce::String (state.getRawParameterValue (ParamID::inGain)->load(), 1) + " dB";
    if (t.key == "output") return "LIM " + db (m.limit.load());
    if (t.key == "tune")
    {
        const int k = juce::roundToInt (state.getRawParameterValue (ParamID::tuneKey)->load());
        const int sc = juce::roundToInt (state.getRawParameterValue (ParamID::tuneScale)->load());
        return juce::String (keyName (k)) + " " + juce::String (scaleName ((Scale) sc)).substring (0, 5);
    }
    if (t.module >= 0)
    {
        switch ((ModuleId) t.module)
        {
            case ModuleId::gate:   return m.breath.load() ? juce::String ("BREATH") : db (m.gate.load());
            case ModuleId::level:  { const float v = m.level.load(); return (v >= 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
            case ModuleId::deess:  return db (m.deess.load());
            case ModuleId::comp:   return db (m.comp.load());
            case ModuleId::sculpt: return sculptStyleName (juce::roundToInt (state.getRawParameterValue (ParamID::sculptStyle)->load()));
            case ModuleId::eq: case ModuleId::sat: case ModuleId::numModules: break;
        }
    }
    return {};
}

void ChainStrip::tick()
{
    const auto o = processor.getModuleOrder();
    if (o != order && dragTile < 0)
    {
        order = o;
        rebuildTiles();
    }
    repaint();
}

void ChainStrip::paint (juce::Graphics& g)
{
    if (tiles.empty())
        return;
    if (tiles.front().bounds.getWidth() <= 0.0f)
        layoutTiles();

    // Connectors
    for (size_t i = 0; i + 1 < tiles.size(); ++i)
    {
        const auto& a = tiles[i].bounds;
        const float x = a.getRight() + 6.0f, y = a.getCentreY();
        juce::Path arrow;
        arrow.startNewSubPath (x - 3.0f, y - 4.0f);
        arrow.lineTo (x + 1.5f, y);
        arrow.lineTo (x - 3.0f, y + 4.0f);
        g.setColour (Colours::textDim.withAlpha (0.6f));
        g.strokePath (arrow, juce::PathStrokeType (1.4f));
    }

    // Bracket over the re-orderable stages
    {
        float x0 = 1.0e9f, x1 = -1.0e9f;
        for (auto& t : tiles)
            if (t.module >= 0) { x0 = std::min (x0, t.bounds.getX()); x1 = std::max (x1, t.bounds.getRight()); }
        g.setColour (Colours::edge);
        g.drawHorizontalLine (getHeight() - 1, x0, x1);
    }

    for (size_t i = 0; i < tiles.size(); ++i)
    {
        const auto& t = tiles[i];
        auto b = t.bounds;
        if ((int) i == dragTile)
            b = b.withX (dragX - dragOffset);
        const bool on = tileEnabled (t);
        const bool hov = (int) i == hoverTile;

        Hardware::drawPlate (g, b.reduced (0.5f, 1.5f), 4.0f, false);
        if (hov || (int) i == dragTile)
        {
            g.setColour (juce::Colours::white.withAlpha ((int) i == dragTile ? 0.08f : 0.04f));
            g.fillRoundedRectangle (b.reduced (0.5f, 1.5f), 4.0f);
        }
        if (on)
        {
            g.setColour (t.colour.withAlpha (0.85f));
            g.fillRect (b.getX() + 8.0f, b.getBottom() - 6.0f, b.getWidth() - 16.0f, 2.0f);
        }
        {
            // Read-out window
            const auto lcd = b.withTrimmedTop (23.0f).withTrimmedBottom (9.0f).reduced (8.0f, 0.0f);
            if (lcd.getHeight() > 8.0f)
                Hardware::drawRecess (g, lcd, 2.0f);
        }
        if (t.enableId.isNotEmpty())
            Hardware::drawLed (g, powerRect (t).withX (b.getX() + 7.0f).reduced (1.0f), t.colour, on);
        g.setFont (legendFont (11.0f));
        g.setColour (on ? Colours::signature : Colours::textDim);
        g.drawText (t.name, b.withTrimmedLeft (t.enableId.isNotEmpty() ? 20.0f : 6.0f).withHeight (26.0f).translated (0.0f, 2.0f),
                    t.enableId.isNotEmpty() ? juce::Justification::centredLeft : juce::Justification::centred);
        g.setFont (uiFont (10.0f));
        g.setColour (on ? t.colour.withAlpha (0.9f) : Colours::textDim.withAlpha (0.6f));
        g.drawText (on ? tileValue (t) : juce::String ("off"), b.withTrimmedTop (22.0f).withTrimmedBottom (6.0f).reduced (6.0f, 0.0f),
                    juce::Justification::centred);
    }

    if (dragTile >= 0 && dragTarget >= 0)
    {
        const auto& t = tiles[(size_t) dragTarget];
        const float x = dragTarget > dragTile ? t.bounds.getRight() + 6.0f : t.bounds.getX() - 6.0f;
        g.setColour (Colours::accent);
        g.fillRoundedRectangle (x - 1.5f, 2.0f, 3.0f, (float) getHeight() - 4.0f, 1.5f);
    }
}

void ChainStrip::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (size_t i = 0; i < tiles.size(); ++i)
        if (tiles[i].bounds.contains (e.position)) h = (int) i;
    if (h != hoverTile)
    {
        hoverTile = h;
        setMouseCursor (h >= 0 && tiles[(size_t) h].module >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void ChainStrip::mouseDown (const juce::MouseEvent& e)
{
    dragTile = -1;
    dragMoved = false;
    for (size_t i = 0; i < tiles.size(); ++i)
    {
        const auto& t = tiles[i];
        if (! t.bounds.contains (e.position))
            continue;
        if (t.enableId.isNotEmpty() && powerRect (t).expanded (4.0f).contains (e.position))
        {
            if (auto* p = state.getParameter (t.enableId))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
                p->endChangeGesture();
            }
            return;
        }
        dragTile = (int) i;
        dragOffset = e.position.x - t.bounds.getX();
        dragX = e.position.x;
        return;
    }
}

void ChainStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragTile < 0 || tiles[(size_t) dragTile].module < 0)
        return;
    if (! dragMoved && std::abs (e.getDistanceFromDragStartX()) < 5)
        return;
    dragMoved = true;
    dragX = e.position.x;
    dragTarget = -1;
    for (size_t i = 0; i < tiles.size(); ++i)
        if (tiles[i].module >= 0 && e.position.x >= tiles[i].bounds.getX() - 6.0f && e.position.x <= tiles[i].bounds.getRight() + 6.0f)
            dragTarget = (int) i;
    if (dragTarget < 0)
    {
        // clamp to the first / last re-orderable tile
        int firstM = -1, lastM = -1;
        for (size_t i = 0; i < tiles.size(); ++i)
            if (tiles[i].module >= 0) { if (firstM < 0) firstM = (int) i; lastM = (int) i; }
        dragTarget = e.position.x < tiles[(size_t) firstM].bounds.getX() ? firstM : lastM;
    }
    repaint();
}

void ChainStrip::mouseUp (const juce::MouseEvent&)
{
    if (dragTile >= 0 && ! dragMoved)
    {
        if (onTileClicked)
            onTileClicked (tiles[(size_t) dragTile].page, tiles[(size_t) dragTile].key);
    }
    else if (dragTile >= 0 && dragTarget >= 0 && dragTarget != dragTile)
    {
        // Move within the orderable range
        int firstM = -1;
        for (size_t i = 0; i < tiles.size(); ++i)
            if (tiles[i].module >= 0) { firstM = (int) i; break; }
        const int from = dragTile - firstM, to = dragTarget - firstM;
        std::vector<int> o (order.begin(), order.end());
        const int moved = o[(size_t) from];
        o.erase (o.begin() + from);
        o.insert (o.begin() + to, moved);
        for (int i = 0; i < kNumOrderable; ++i) order[(size_t) i] = o[(size_t) i];
        processor.setModuleOrder (order);
        rebuildTiles();
        if (onOrderChanged)
            onOrderChanged();
    }
    dragTile = dragTarget = -1;
    dragMoved = false;
    repaint();
}
} // namespace lavc
