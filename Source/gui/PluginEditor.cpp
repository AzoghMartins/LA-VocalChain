#include "PluginEditor.h"

namespace lavc
{
namespace
{
const char* voiceIds[4][5] = { { ParamID::hv1On, ParamID::hv1Interval, ParamID::hv1Level, ParamID::hv1Pan, ParamID::hv1Formant },
                               { ParamID::hv2On, ParamID::hv2Interval, ParamID::hv2Level, ParamID::hv2Pan, ParamID::hv2Formant },
                               { ParamID::hv3On, ParamID::hv3Interval, ParamID::hv3Level, ParamID::hv3Pan, ParamID::hv3Formant },
                               { ParamID::hv4On, ParamID::hv4Interval, ParamID::hv4Level, ParamID::hv4Pan, ParamID::hv4Formant } };
const char* eqIds[5][3] = { { ParamID::eq1Freq, ParamID::eq1Gain, ParamID::eq1Q }, { ParamID::eq2Freq, ParamID::eq2Gain, ParamID::eq2Q },
                            { ParamID::eq3Freq, ParamID::eq3Gain, ParamID::eq3Q }, { ParamID::eq4Freq, ParamID::eq4Gain, ParamID::eq4Q },
                            { ParamID::eq5Freq, ParamID::eq5Gain, ParamID::eq5Q } };
const char* eqBandNames[5] = { "LOW", "L-MID", "MID", "PRES", "AIR" };

void gridVec (const std::vector<juce::Component*>& items, int columns, juce::Rectangle<int> area)
{
    const int rows = ((int) items.size() + columns - 1) / columns;
    const int cw = area.getWidth() / juce::jmax (1, columns);
    const int rh = area.getHeight() / juce::jmax (1, rows);
    int i = 0;
    for (auto* c : items)
    {
        if (c != nullptr)
            c->setBounds (area.getX() + (i % columns) * cw, area.getY() + (i / columns) * rh, cw, rh);
        ++i;
    }
}
} // namespace

//==============================================================================
VoiceStrip::VoiceStrip (juce::AudioProcessorValueTreeState& s, int v)
    : index (v),
      power (Colours::forVoice (v)),
      interval (s, voiceIds[v][1], "Interval", Colours::forVoice (v), true),
      level (s, voiceIds[v][2], "Level", Colours::forVoice (v)),
      pan (s, voiceIds[v][3], "Pan", Colours::forVoice (v), true),
      formant (s, voiceIds[v][4], "Formant", Colours::forVoice (v), true)
{
    addAndMakeVisible (power);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, voiceIds[v][0], power);
    power.setTooltip ("Enable voice " + juce::String (v + 1));
    power.onStateChange = [this] { repaint(); for (auto* k : { &interval, &level, &pan, &formant }) k->setAlpha (power.getToggleState() ? 1.0f : 0.45f); };
    for (auto* k : { &interval, &level, &pan, &formant })
        addAndMakeVisible (*k);
    auto* modeValue = s.getRawParameterValue (ParamID::harmMode);
    interval.setFormatter ([modeValue] (double value) { return harmonyIntervalText ((int) std::lround (value), (int) std::lround (modeValue->load())); });
    interval.slider.setTooltip ("Harmony interval. Scale mode: scale degrees in the Tune key. Semitones: fixed shift. MIDI Chord: voice follows held MIDI notes");
    power.onStateChange();
}

void VoiceStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f, 0.0f);
    g.setColour (Colours::panelLight.withAlpha (0.5f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (power.getToggleState() ? Colours::forVoice (index) : Colours::textDim);
    g.setFont (uiFont (11.0f, true));
    g.drawText ("VOICE " + juce::String (index + 1), 30, 4, getWidth() - 34, 18, juce::Justification::centredLeft);
}

void VoiceStrip::resized()
{
    auto r = getLocalBounds().reduced (2, 0);
    auto top = r.removeFromTop (24);
    power.setBounds (top.removeFromLeft (26).reduced (4, 3));
    gridVec ({ &interval, &level, &pan, &formant }, 2, r);
}

//==============================================================================
MainPanel::MainPanel (VocalChainProcessor& p)
    : processor (p),
      state (p.getState()),
      chain (p),
      pitchDisplay (p),
      eqDisplay (p),
      notePanel (p),
      inputSection ("Input"),
      outputSection ("Output"),
      gateSection ("Gate / Breath", &state, ParamID::gateOn, moduleColour (ModuleId::gate)),
      levelSection ("Auto Level", &state, ParamID::levelOn, moduleColour (ModuleId::level)),
      deessSection ("De-Esser", &state, ParamID::deessOn, moduleColour (ModuleId::deess)),
      sculptSection ("Sculpt", &state, ParamID::sculptOn, moduleColour (ModuleId::sculpt)),
      eqSection ("EQ", &state, ParamID::eqOn, moduleColour (ModuleId::eq)),
      compSection ("Compressor", &state, ParamID::compOn, moduleColour (ModuleId::comp)),
      satSection ("Saturation", &state, ParamID::satOn, moduleColour (ModuleId::sat)),
      tuneSection ("Tune", &state, ParamID::tuneOn, Colours::accent),
      harmSection ("Harmonies", &state, ParamID::harmOn, Colours::learn),
      dblSection ("Doubler", &state, ParamID::dblOn, Colours::learn),
      modSection ("Modulation", &state, ParamID::modOn, Colours::blue),
      dlySection ("Delay", &state, ParamID::dlyOn, Colours::detection),
      revSection ("Reverb", &state, ParamID::revOn, Colours::accent)
{
    // ---- Header ----------------------------------------------------------------------------
    refreshPresetList();
    presetBox.setTooltip ("Factory and user presets");
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id > 0 && id < 1000)
            processor.setCurrentProgram (id - 1);
        else if (id >= 1000 && id - 1000 < numUserPresets)
            processor.loadUserPreset (userPresetFiles[id - 1000]);
    };
    addAndMakeVisible (presetBox);

    saveButton.setTooltip ("Save the current settings as a user preset");
    saveButton.onClick = [this]
    {
        auto* w = new juce::AlertWindow ("Save preset", "Name for the new preset:", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", processor.getCurrentPresetName());
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r == 1)
            {
                const auto name = w->getTextEditorContents ("name");
                showToast (processor.saveUserPreset (name) ? "Saved preset \"" + name + "\"" : juce::String ("Could not save the preset"));
                refreshPresetList();
            }
        }), true);
    };
    addAndMakeVisible (saveButton);

    for (auto* b : { &mixButton, &trackButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    mixButton.setTooltip ("Mixing mode: best quality, more latency (about 30 ms)");
    trackButton.setTooltip ("Tracking mode: low latency for monitoring while recording");
    auto setMode = [this] (float v)
    {
        if (auto* prm = state.getParameter (ParamID::mode))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            prm->endChangeGesture();
        }
    };
    mixButton.onClick = [setMode] { setMode (0.0f); };
    trackButton.onClick = [setMode] { setMode (1.0f); };

    assistButton.setTooltip ("Assistant: press, sing or play the vocal for 10-20 seconds, press again. "
                             "Sets input gain, gate, de-esser, low cut, compressor and the key");
    assistButton.onClick = [this]
    {
        if (processor.isAssistRunning())
        {
            const auto result = processor.finishAssist();
            showToast (result.isEmpty() ? juce::String ("Assistant: no vocal heard, nothing changed") : "Assistant: " + result);
        }
        else
            processor.startAssist();
    };
    addAndMakeVisible (assistButton);

    freezeButton.setClickingTogglesState (true);
    freezeButton.onClick = [this] { pitchDisplay.setFrozen (freezeButton.getToggleState()); };
    freezeButton.setTooltip ("Freeze the pitch display");
    addAndMakeVisible (freezeButton);

    // ---- Displays -------------------------------------------------------------------------------
    addAndMakeVisible (chain);
    chain.onTileClicked = [this] (int page, const juce::String& key)
    {
        setPage (page);
        focusSection (key);
    };
    chain.onOrderChanged = [this] { layoutPages(); };
    pitchDisplay.setTooltip ("Pitch over time on the scale: grey = sung, teal = tuned, colours = harmony voices. Mouse wheel zooms time");
    addAndMakeVisible (pitchDisplay);
    addAndMakeVisible (eqDisplay);
    eqDisplay.onBandSelected = [this] (int b) { selectEqBand (b); };
    addAndMakeVisible (notePanel);
    addAndMakeVisible (meters);

    // ---- Tabs -------------------------------------------------------------------------------------
    int tabIndex = 0;
    for (auto* t : { &tabCorrection, &tabTune, &tabEffects })
    {
        t->setRadioGroupId (77);
        t->setClickingTogglesState (true);
        const int page = tabIndex++;
        t->onClick = [this, page] { setPage (page); };
        addAndMakeVisible (*t);
    }

    // ---- Input / output ----------------------------------------------------------------------
    for (auto* s : { &inputSection, &outputSection, &gateSection, &levelSection, &deessSection, &sculptSection, &eqSection,
                     &compSection, &satSection, &tuneSection, &harmSection, &dblSection, &modSection, &dlySection, &revSection })
        addChildComponent (*s);
    inputSection.setVisible (true);
    outputSection.setVisible (true);

    inputKnobs = { knob (inputSection, ParamID::inGain, "Gain", Colours::accent, true),
                   knob (inputSection, ParamID::lowCut, "Low Cut", Colours::threshold) };
    phasePill = std::make_unique<ParamPill> (state, ParamID::phase, juce::String::charToString (0x00d8), Colours::threshold);
    slopePill = std::make_unique<ParamPill> (state, ParamID::lowCutSlope, "24dB", Colours::threshold);
    slopePill->setTooltip ("Low cut slope: 12 or 24 dB/oct");
    inputSection.addAndMakeVisible (*phasePill);
    inputSection.addAndMakeVisible (*slopePill);

    outputKnobs = { knob (outputSection, ParamID::outGain, "Gain", Colours::accent, true),
                    knob (outputSection, ParamID::outWidth, "Width", Colours::accent),
                    knob (outputSection, ParamID::outMix, "Dry/Wet", Colours::accent),
                    knob (outputSection, ParamID::limCeiling, "Ceiling", Colours::danger) };
    limitPill = std::make_unique<ParamPill> (state, ParamID::limOn, "LIMIT", Colours::danger);
    outputSection.addAndMakeVisible (*limitPill);

    // ---- Correction ------------------------------------------------------------------------------
    const auto cg = moduleColour (ModuleId::gate);
    gateKnobs = { knob (gateSection, ParamID::gateThreshold, "Threshold", Colours::threshold),
                  knob (gateSection, ParamID::gateRange, "Range", cg),
                  knob (gateSection, ParamID::gateAttack, "Attack", cg),
                  knob (gateSection, ParamID::gateRelease, "Release", cg),
                  knob (gateSection, ParamID::gateHold, "Hold", cg),
                  knob (gateSection, ParamID::breath, "Breath", Colours::learn),
                  knob (gateSection, ParamID::breathSens, "Br. Sens", Colours::learn) };
    gateKnobs[5]->slider.setTooltip ("Breath control: lowers breaths between phrases by this amount (works even with the gate off)");

    const auto cl = moduleColour (ModuleId::level);
    levelKnobs = { knob (levelSection, ParamID::levelTarget, "Target", cl), knob (levelSection, ParamID::levelRange, "Range", cl),
                   knob (levelSection, ParamID::levelSpeed, "Speed", cl) };
    levelKnobs[0]->slider.setTooltip ("Auto level (vocal rider): rides the vocal towards this loudness before compression");

    const auto cd = moduleColour (ModuleId::deess);
    deessKnobs = { knob (deessSection, ParamID::deessFreq, "Freq", cd), knob (deessSection, ParamID::deessThreshold, "Threshold", Colours::threshold),
                   knob (deessSection, ParamID::deessRange, "Range", cd) };
    deessMode = choice (deessSection, ParamID::deessMode);
    listenPill = std::make_unique<ParamPill> (state, ParamID::deessListen, "LISTEN", cd);
    listenPill->setTooltip ("Listen to the sibilance band only");
    deessSection.addAndMakeVisible (*listenPill);

    const auto cs = moduleColour (ModuleId::sculpt);
    sculptStyle = choice (sculptSection, ParamID::sculptStyle);
    sculptKnobs = { knob (sculptSection, ParamID::sculptAmount, "Amount", cs), knob (sculptSection, ParamID::sculptSpeed, "Speed", cs),
                    knob (sculptSection, ParamID::sculptTilt, "Tilt", cs, true) };
    sculptStyle->box.setTooltip ("Sculpt continuously re-balances the vocal's tone towards this target style");

    const auto ce = moduleColour (ModuleId::eq);
    for (int b = 0; b < kEqBands; ++b)
    {
        const bool shelf = b == 0 || b == kEqBands - 1;
        eqKnobs[(size_t) b] = { knob (eqSection, eqIds[b][0], "Freq", ce), knob (eqSection, eqIds[b][1], "Gain", ce, true),
                                knob (eqSection, eqIds[b][2], shelf ? "Slope" : "Q", ce) };
        eqBandButtons[(size_t) b] = std::make_unique<PillButton> (eqBandNames[b], ce);
        eqBandButtons[(size_t) b]->setClickingTogglesState (false);
        eqBandButtons[(size_t) b]->onClick = [this, b] { selectEqBand (b); };
        eqSection.addAndMakeVisible (*eqBandButtons[(size_t) b]);
    }

    const auto cc = moduleColour (ModuleId::comp);
    compMode = choice (compSection, ParamID::compMode);
    compKnobs = { knob (compSection, ParamID::compThreshold, "Threshold", Colours::threshold), knob (compSection, ParamID::compRatio, "Ratio", cc),
                  knob (compSection, ParamID::compAttack, "Attack", cc), knob (compSection, ParamID::compRelease, "Release", cc),
                  knob (compSection, ParamID::compMakeup, "Makeup", cc), knob (compSection, ParamID::compMix, "Mix", cc) };
    compMode->box.setTooltip ("Clean: transparent VCA. Opto: smooth, program-dependent. FET: fast and punchy. Tube: soft vari-mu. Multiband: 3 bands");

    const auto ct = moduleColour (ModuleId::sat);
    satMode = choice (satSection, ParamID::satMode);
    satKnobs = { knob (satSection, ParamID::satDrive, "Drive", ct), knob (satSection, ParamID::satTone, "Tone", ct, true),
                 knob (satSection, ParamID::satMix, "Mix", ct) };

    // ---- Tune & voices ---------------------------------------------------------------------------
    tuneKey = choice (tuneSection, ParamID::tuneKey, "Key");
    tuneScale = choice (tuneSection, ParamID::tuneScale, "Scale");
    tuneTarget = choice (tuneSection, ParamID::tuneTarget, "Target");
    tuneKnobs = { knob (tuneSection, ParamID::tuneSpeed, "Speed", Colours::accent), knob (tuneSection, ParamID::tuneTolerance, "Tolerance", Colours::accent),
                  knob (tuneSection, ParamID::tuneAmount, "Amount", Colours::accent), knob (tuneSection, ParamID::transpose, "Transpose", Colours::threshold, true),
                  knob (tuneSection, ParamID::formant, "Formant", Colours::learn, true) };
    tuneKnobs[0]->slider.setTooltip ("Retune speed: 0 = hard, robotic tuning. Higher = more natural glides");
    tuneKnobs[1]->slider.setTooltip ("Notes closer than this to the target are left alone (keeps vibrato and expression)");
    tuneKnobs[4]->slider.setTooltip ("Formant shift: change the vocal character (gender/size) without changing the pitch");
    tuneTarget->box.setTooltip ("Scale: tune to the key/scale. MIDI Notes: tune to the notes played on the plugin's MIDI input");
    learnKeyButton.setTooltip ("Detect the key: press, sing for a few seconds, press again");
    learnKeyButton.onClick = [this]
    {
        if (processor.isKeyLearnRunning())
            showToast (processor.finishKeyLearn());
        else
            processor.startKeyLearn();
    };
    tuneSection.addAndMakeVisible (learnKeyButton);

    harmMode = choice (harmSection, ParamID::harmMode, "Mode");
    harmKnobs = { knob (harmSection, ParamID::harmHumanize, "Humanize", Colours::learn), knob (harmSection, ParamID::harmLevel, "Level", Colours::learn) };
    harmMode->box.setTooltip ("Scale: intervals follow the key. Semitones: fixed intervals. MIDI Chord: voices sing the notes held on the MIDI input");
    for (int v = 0; v < kNumHarmonyVoices; ++v)
    {
        voices[(size_t) v] = std::make_unique<VoiceStrip> (state, v);
        harmSection.addAndMakeVisible (*voices[(size_t) v]);
    }
    harmMode->box.onChange = [this, previous = harmMode->box.onChange]
    {
        if (previous) previous();
        for (auto& v : voices) v->refresh();
    };

    // ---- Effects ----------------------------------------------------------------------------------
    dblKnobs = { knob (dblSection, ParamID::dblAmount, "Amount", Colours::learn), knob (dblSection, ParamID::dblDetune, "Detune", Colours::learn),
                 knob (dblSection, ParamID::dblDelay, "Delay", Colours::learn), knob (dblSection, ParamID::dblWidth, "Width", Colours::learn),
                 knob (dblSection, ParamID::dblDrift, "Humanize", Colours::learn) };
    modMode = choice (modSection, ParamID::modMode);
    modKnobs = { knob (modSection, ParamID::modRate, "Rate", Colours::blue), knob (modSection, ParamID::modDepth, "Depth", Colours::blue),
                 knob (modSection, ParamID::modFeedback, "Feedback", Colours::blue), knob (modSection, ParamID::modMix, "Mix", Colours::blue) };
    dlyMode = choice (dlySection, ParamID::dlyMode);
    dlySync = choice (dlySection, ParamID::dlySync);
    dlySync->box.setTooltip ("Sync the delay time to the host tempo (Free = use the Time knob)");
    const auto cy = Colours::detection;
    dlyKnobs = { knob (dlySection, ParamID::dlyTime, "Time", cy), knob (dlySection, ParamID::dlyFeedback, "Feedback", cy),
                 knob (dlySection, ParamID::dlyHp, "Low Cut", cy), knob (dlySection, ParamID::dlyLp, "High Cut", cy),
                 knob (dlySection, ParamID::dlyDuck, "Duck", cy), knob (dlySection, ParamID::dlyWidth, "Width", cy),
                 knob (dlySection, ParamID::dlyMix, "Level", cy) };
    dlyTimeKnob = dlyKnobs[0];
    dlyKnobs[4]->slider.setTooltip ("Ducking: lowers the echoes while the vocal sings, so they bloom in the gaps");
    revType = choice (revSection, ParamID::revType);
    const auto cr = Colours::accent;
    revKnobs = { knob (revSection, ParamID::revDecay, "Decay", cr), knob (revSection, ParamID::revPredelay, "Pre-Delay", cr),
                 knob (revSection, ParamID::revSize, "Size", cr), knob (revSection, ParamID::revDamp, "Damping", cr),
                 knob (revSection, ParamID::revLowCut, "Low Cut", cr), knob (revSection, ParamID::revDuck, "Duck", cr),
                 knob (revSection, ParamID::revWidth, "Width", cr), knob (revSection, ParamID::revMix, "Level", cr) };

    sectionByKey = { { "input", &inputSection }, { "output", &outputSection }, { "tune", &tuneSection }, { "harm", &harmSection },
                     { "doubler", &dblSection }, { "mod", &modSection }, { "delay", &dlySection }, { "reverb", &revSection },
                     { "m0", &gateSection }, { "m1", &levelSection }, { "m2", &deessSection }, { "m3", &sculptSection },
                     { "m4", &eqSection }, { "m5", &compSection }, { "m6", &satSection } };

    // Dim knobs of switched-off sections
    for (auto* s : { &gateSection, &levelSection, &deessSection, &sculptSection, &eqSection, &compSection, &satSection,
                     &tuneSection, &harmSection, &dblSection, &modSection, &dlySection, &revSection })
    {
        s->onActiveChanged = [s]
        {
            for (auto* c : s->getChildren())
                if (dynamic_cast<Knob*> (c) != nullptr || dynamic_cast<Choice*> (c) != nullptr || dynamic_cast<VoiceStrip*> (c) != nullptr)
                    c->setAlpha (s->isActive() ? 1.0f : 0.5f);
        };
        s->onActiveChanged();
    }
    // Breath control works without the gate: keep its knobs bright
    gateSection.onActiveChanged = [this]
    {
        for (int i = 0; i < 5; ++i) gateKnobs[(size_t) i]->setAlpha (gateSection.isActive() ? 1.0f : 0.5f);
    };
    gateSection.onActiveChanged();

    selectEqBand (2);
    setPage (0);
    setSize (baseWidth, baseHeight);
}

Knob* MainPanel::knob (Section& s, const char* id, const juce::String& name, juce::Colour c, bool bipolar)
{
    auto k = std::make_unique<Knob> (state, id, name, c, bipolar);
    auto* raw = k.get();
    s.addAndMakeVisible (*raw);
    owned.push_back (std::move (k));
    return raw;
}

Choice* MainPanel::choice (Section& s, const char* id, const juce::String& caption)
{
    auto c = std::make_unique<Choice> (state, id, caption);
    auto* raw = c.get();
    s.addAndMakeVisible (*raw);
    owned.push_back (std::move (c));
    return raw;
}

void MainPanel::grid (std::initializer_list<juce::Component*> items, int columns, juce::Rectangle<int> area)
{
    gridVec (std::vector<juce::Component*> (items), columns, area);
}

void MainPanel::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem (processor.getProgramName (i), i + 1);

    numUserPresets = 0;
    auto files = VocalChainProcessor::getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.lavc");
    files.sort();
    if (! files.isEmpty())
    {
        presetBox.addSectionHeading ("User presets");
        for (auto& f : files)
        {
            if (numUserPresets >= 256) break;
            userPresetFiles[numUserPresets] = f;
            presetBox.addItem (f.getFileNameWithoutExtension(), 1000 + numUserPresets);
            ++numUserPresets;
        }
    }
    presetBox.setText (processor.getCurrentPresetName(), juce::dontSendNotification);
}

void MainPanel::showToast (const juce::String& text)
{
    toastText = text;
    toastTicks = 60 * 8;
    repaint (0, 0, baseWidth, 46);
}

void MainPanel::selectEqBand (int band)
{
    eqBand = juce::jlimit (0, kEqBands - 1, band);
    for (int b = 0; b < kEqBands; ++b)
    {
        eqBandButtons[(size_t) b]->setToggleState (b == eqBand, juce::dontSendNotification);
        for (auto* k : eqKnobs[(size_t) b])
            k->setVisible (b == eqBand);
    }
    eqDisplay.setSelectedBand (eqBand);
}

void MainPanel::setPage (int page)
{
    currentPage = juce::jlimit (0, 2, page);
    tabCorrection.setToggleState (currentPage == 0, juce::dontSendNotification);
    tabTune.setToggleState (currentPage == 1, juce::dontSendNotification);
    tabEffects.setToggleState (currentPage == 2, juce::dontSendNotification);
    for (auto* s : { &gateSection, &levelSection, &deessSection, &sculptSection, &eqSection, &compSection, &satSection })
        s->setVisible (currentPage == 0);
    for (auto* s : { &tuneSection, &harmSection })
        s->setVisible (currentPage == 1);
    for (auto* s : { &dblSection, &modSection, &dlySection, &revSection })
        s->setVisible (currentPage == 2);
}

void MainPanel::focusSection (const juce::String& key)
{
    highlightKey = key;
    highlightAmount = 1.0f;
}

//==============================================================================
void MainPanel::resized()
{
    const int margin = 12, gap = 8;

    // Header
    presetBox.setBounds (330, 11, 220, 26);
    saveButton.setBounds (556, 11, 52, 26);
    mixButton.setBounds (626, 11, 76, 26);
    trackButton.setBounds (704, 11, 84, 26);
    assistButton.setBounds (806, 11, 108, 26);
    freezeButton.setBounds (920, 11, 72, 26);

    chain.setBounds (margin, 54, baseWidth - 2 * margin, 44);

    const int dispY = 106, dispH = 284;
    pitchDisplay.setBounds (margin, dispY, 640, dispH);
    eqDisplay.setBounds (660, dispY, 470, dispH);
    notePanel.setBounds (1138, dispY, 216, dispH);
    meters.setBounds (1362, dispY + 4, 66, dispH - 8);

    const int tabsY = dispY + dispH + gap;
    const int sideW = 128, outW = 148;
    const int pageX = margin + sideW + gap;
    tabCorrection.setBounds (pageX, tabsY, 120, 24);
    tabTune.setBounds (pageX + 126, tabsY, 130, 24);
    tabEffects.setBounds (pageX + 262, tabsY, 100, 24);

    const int botY = tabsY + 24 + gap;
    const int botH = baseHeight - botY - margin;
    inputSection.setBounds (margin, tabsY, sideW, baseHeight - tabsY - margin);
    outputSection.setBounds (baseWidth - margin - outW, tabsY, outW, baseHeight - tabsY - margin);

    {
        auto c = inputSection.getContentArea();
        auto pills = c.removeFromTop (24);
        phasePill->setBounds (pills.removeFromLeft (pills.getWidth() / 2 - 2));
        pills.removeFromLeft (4);
        slopePill->setBounds (pills);
        c.removeFromTop (6);
        grid ({ inputKnobs[0], inputKnobs[1] }, 1, c);
    }
    {
        limitPill->setBounds (outputSection.getWidth() - 58, 5, 50, 19);
        grid ({ outputKnobs[0], outputKnobs[1], outputKnobs[2], outputKnobs[3] }, 2, outputSection.getContentArea().withTrimmedTop (6));
    }

    layoutPages();
    juce::ignoreUnused (botH);
}

void MainPanel::layoutCorrection (juce::Rectangle<int> area)
{
    const int gap = 8;
    shownOrder = processor.getModuleOrder();
    auto columnsOf = [] (ModuleId m)
    {
        switch (m)
        {
            case ModuleId::gate: return 4.0f;
            case ModuleId::eq: case ModuleId::comp: return 3.0f;
            case ModuleId::level: case ModuleId::deess: case ModuleId::sculpt: case ModuleId::sat:
            case ModuleId::numModules: break;
        }
        return 2.0f;
    };
    float total = 0.0f;
    for (int m : shownOrder) total += columnsOf ((ModuleId) m);
    const float pad = 14.0f;
    const float cw = ((float) area.getWidth() - gap * (kNumOrderable - 1) - pad * kNumOrderable) / total;
    int x = area.getX();
    Section* sections[kNumOrderable] = { &gateSection, &levelSection, &deessSection, &sculptSection, &eqSection, &compSection, &satSection };
    for (int i = 0; i < kNumOrderable; ++i)
    {
        const int m = shownOrder[(size_t) i];
        const int w = i == kNumOrderable - 1 ? area.getRight() - x : juce::roundToInt (columnsOf ((ModuleId) m) * cw + pad);
        sections[m]->setBounds (x, area.getY(), w, area.getHeight());
        x += w + gap;
    }

    grid ({ gateKnobs[0], gateKnobs[1], gateKnobs[2], gateKnobs[3], gateKnobs[4], gateKnobs[5], gateKnobs[6] }, 4, gateSection.getContentArea());
    grid ({ levelKnobs[0], levelKnobs[1], levelKnobs[2] }, 2, levelSection.getContentArea());

    {
        auto c = deessSection.getContentArea();
        deessMode->setBounds (c.removeFromTop (26));
        c.removeFromTop (4);
        auto bottom = c.removeFromBottom (c.getHeight() / 2);
        grid ({ deessKnobs[0], deessKnobs[1] }, 2, c);
        auto listenCell = bottom.removeFromRight (bottom.getWidth() / 2);
        deessKnobs[2]->setBounds (bottom);
        listenPill->setBounds (listenCell.withSizeKeepingCentre (listenCell.getWidth() - 8, 22).translated (0, -8));
    }
    {
        auto c = sculptSection.getContentArea();
        sculptStyle->setBounds (c.removeFromTop (26));
        c.removeFromTop (4);
        grid ({ sculptKnobs[0], sculptKnobs[1], sculptKnobs[2] }, 2, c);
    }
    {
        auto c = eqSection.getContentArea();
        auto row = c.removeFromTop (22);
        const int bw = row.getWidth() / kEqBands;
        for (int b = 0; b < kEqBands; ++b)
            eqBandButtons[(size_t) b]->setBounds (row.removeFromLeft (bw).reduced (1, 0));
        c.removeFromTop (8);
        const auto knobRow = c.withSizeKeepingCentre (c.getWidth(), juce::jmin (c.getHeight(), 120));
        for (auto& band : eqKnobs)
            grid ({ band[0], band[1], band[2] }, 3, knobRow);
    }
    {
        auto c = compSection.getContentArea();
        compMode->setBounds (c.removeFromTop (26));
        c.removeFromTop (4);
        grid ({ compKnobs[0], compKnobs[1], compKnobs[2], compKnobs[3], compKnobs[4], compKnobs[5] }, 3, c);
    }
    {
        auto c = satSection.getContentArea();
        satMode->setBounds (c.removeFromTop (26));
        c.removeFromTop (4);
        grid ({ satKnobs[0], satKnobs[1], satKnobs[2] }, 2, c);
    }
}

void MainPanel::layoutPages()
{
    const int margin = 12, gap = 8, sideW = 128, outW = 148;
    const int tabsY = 106 + 284 + gap;
    const int botY = tabsY + 24 + gap;
    const auto area = juce::Rectangle<int> (margin + sideW + gap, botY, baseWidth - 2 * margin - sideW - outW - 2 * gap, baseHeight - botY - margin);

    layoutCorrection (area);

    // ---- Tune & voices ----
    {
        const int tuneW = 380;
        tuneSection.setBounds (area.withWidth (tuneW));
        harmSection.setBounds (area.withTrimmedLeft (tuneW + gap));

        learnKeyButton.setBounds (tuneSection.getWidth() - 110, 5, 76, 19);
        auto c = tuneSection.getContentArea();
        auto left = c.removeFromLeft (110);
        const int ch = left.getHeight() / 3;
        tuneKey->setBounds (left.removeFromTop (ch).reduced (2, 4));
        tuneScale->setBounds (left.removeFromTop (ch).reduced (2, 4));
        tuneTarget->setBounds (left.reduced (2, 4));
        c.removeFromLeft (6);
        grid ({ tuneKnobs[0], tuneKnobs[1], tuneKnobs[2], tuneKnobs[3], tuneKnobs[4] }, 3, c);

        auto h = harmSection.getContentArea();
        auto ctrl = h.removeFromLeft (96);
        harmMode->setBounds (ctrl.removeFromTop (48).reduced (2, 2));
        grid ({ harmKnobs[0], harmKnobs[1] }, 1, ctrl);
        h.removeFromLeft (6);
        const int vw = h.getWidth() / kNumHarmonyVoices;
        for (auto& v : voices)
            v->setBounds (h.removeFromLeft (vw).reduced (2, 0));
    }

    // ---- Effects ----
    {
        const float cols[] = { 3.0f, 2.0f, 4.0f, 4.0f };
        const float pad = 14.0f;
        const float cw = ((float) area.getWidth() - 3 * gap - 4 * pad) / 13.0f;
        int x = area.getX();
        Section* fx[] = { &dblSection, &modSection, &dlySection, &revSection };
        for (int i = 0; i < 4; ++i)
        {
            const int w = i == 3 ? area.getRight() - x : juce::roundToInt (cols[i] * cw + pad);
            fx[i]->setBounds (x, area.getY(), w, area.getHeight());
            x += w + gap;
        }
        {
            auto c = dblSection.getContentArea();
            c.removeFromTop (30);
            grid ({ dblKnobs[0], dblKnobs[1], dblKnobs[2], dblKnobs[3], dblKnobs[4] }, 3, c);
        }
        {
            auto c = modSection.getContentArea();
            modMode->setBounds (c.removeFromTop (26));
            c.removeFromTop (4);
            grid ({ modKnobs[0], modKnobs[1], modKnobs[2], modKnobs[3] }, 2, c);
        }
        {
            auto c = dlySection.getContentArea();
            auto top = c.removeFromTop (26);
            dlyMode->setBounds (top.removeFromLeft (top.getWidth() / 2 - 3));
            top.removeFromLeft (6);
            dlySync->setBounds (top);
            c.removeFromTop (4);
            grid ({ dlyKnobs[0], dlyKnobs[1], dlyKnobs[2], dlyKnobs[3], dlyKnobs[4], dlyKnobs[5], dlyKnobs[6] }, 4, c);
        }
        {
            auto c = revSection.getContentArea();
            revType->setBounds (c.removeFromTop (26).withTrimmedRight (c.getWidth() / 2));
            c.removeFromTop (4);
            grid ({ revKnobs[0], revKnobs[1], revKnobs[2], revKnobs[3], revKnobs[4], revKnobs[5], revKnobs[6], revKnobs[7] }, 4, c);
        }
    }
}

void MainPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    auto header = juce::Rectangle<int> (0, 0, baseWidth, 46);
    g.setGradientFill (juce::ColourGradient (Colours::panelLight, 0.0f, 0.0f, Colours::panel, 0.0f, 46.0f, false));
    g.fillRect (header);
    g.setColour (Colours::edge);
    g.drawHorizontalLine (46, 0.0f, (float) baseWidth);

    // Logo: a pitch line snapping onto note steps
    {
        juce::Path mark;
        const float x0 = 14.0f, y0 = 23.0f;
        mark.startNewSubPath (x0, y0 + 6.0f);
        mark.quadraticTo (x0 + 5.0f, y0 + 7.0f, x0 + 7.0f, y0 + 1.0f);
        mark.lineTo (x0 + 13.0f, y0 + 1.0f);
        mark.quadraticTo (x0 + 15.0f, y0 - 7.0f, x0 + 19.0f, y0 - 7.0f);
        mark.lineTo (x0 + 26.0f, y0 - 7.0f);
        g.setColour (Colours::accent);
        g.strokePath (mark, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (Colours::learn);
        g.fillEllipse (x0 + 23.0f, y0 - 10.0f, 6.0f, 6.0f);

        g.setFont (uiFont (21.0f, true));
        g.setColour (Colours::text);
        g.drawText ("LA", 48, 8, 30, 30, juce::Justification::centredLeft);
        g.setColour (Colours::accent);
        g.drawText ("VOCALCHAIN", 76, 8, 140, 30, juce::Justification::centredLeft);
        g.setColour (Colours::textDim);
        g.setFont (uiFont (10.5f));
        g.drawText ("TUNE  /  MIX  /  FX", 212, 15, 110, 16, juce::Justification::centredLeft);
    }

    g.setFont (uiFont (11.5f));
    if (toastTicks > 0)
    {
        g.setColour (Colours::learn);
        g.drawFittedText (toastText, baseWidth - 440, 6, 428, 34, juce::Justification::centredRight, 2);
    }
    else
    {
        g.setColour (Colours::textDim);
        g.drawText (infoText, baseWidth - 440, 10, 428, 26, juce::Justification::centredRight);
    }

    // Hint beside the tabs
    g.setFont (uiFont (10.5f));
    g.setColour (Colours::textDim);
    if (currentPage == 0)
        g.drawText ("Sections follow the chain order - drag the stages above to reorder", tabEffects.getRight() + 16, tabEffects.getY(),
                    420, 24, juce::Justification::centredLeft);
}

void MainPanel::tick()
{
    PitchFrame f;
    while (processor.pitchQueue.pop (f))
        pitchDisplay.addFrame (f);
    pitchDisplay.tick();
    pitchDisplay.repaint();
    eqDisplay.pullSpectrum();
    notePanel.tick();
    chain.tick();

    const float inPeak = processor.meters.inPeak.exchange (0.0f);
    const float outPeak = processor.meters.outPeak.exchange (0.0f);
    const float gr = processor.meters.comp.load() + processor.meters.limit.load();
    meters.setLevels (inPeak, outPeak, gr);

    if (processor.getModuleOrder() != shownOrder)
        layoutPages();

    // Header state
    const bool tracking = state.getRawParameterValue (ParamID::mode)->load() > 0.5f;
    mixButton.setToggleState (! tracking, juce::dontSendNotification);
    trackButton.setToggleState (tracking, juce::dontSendNotification);

    blink = (blink + 1) % 40;
    const bool assisting = processor.isAssistRunning();
    assistButton.setToggleState (assisting && blink < 28, juce::dontSendNotification);
    assistButton.setButtonText (assisting ? "LISTENING " + juce::String ((int) processor.getAssistSeconds()) + "s" : juce::String ("ASSIST"));
    const bool learning = processor.isKeyLearnRunning();
    learnKeyButton.setToggleState (learning && blink < 28, juce::dontSendNotification);
    learnKeyButton.setButtonText (learning ? "LISTENING" : "LEARN KEY");

    const bool synced = state.getRawParameterValue (ParamID::dlySync)->load() > 0.5f;
    dlyTimeKnob->setEnabled (! synced);
    dlyTimeKnob->setAlpha (synced ? 0.4f : (dlySection.isActive() ? 1.0f : 0.5f));

    if (highlightAmount > 0.0f)
    {
        highlightAmount = juce::jmax (0.0f, highlightAmount - 0.02f);
        if (auto it = sectionByKey.find (highlightKey); it != sectionByKey.end())
            it->second->setHighlight (highlightAmount);
    }

    if (toastTicks > 0 && --toastTicks == 0)
        repaint (0, 0, baseWidth, 46);

    const double sr = processor.getEngineSampleRate();
    juce::String info;
    info << (tracking ? "Tracking" : "Mixing") << "    Latency "
         << juce::String (1000.0 * processor.getLatencySamples() / juce::jmax (1.0, sr), 1) << " ms    "
         << juce::String (sr / 1000.0, 1) << " kHz";
    if (info != infoText)
    {
        infoText = info;
        repaint (0, 0, baseWidth, 46);
    }

    const auto name = processor.getCurrentPresetName();
    if (presetBox.getText() != name && ! presetBox.isPopupActive())
        presetBox.setText (name, juce::dontSendNotification);
}

//==============================================================================
VocalChainEditor::VocalChainEditor (VocalChainProcessor& p)
    : AudioProcessorEditor (p), panel (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (panel);

    setResizable (true, true);
    setResizeLimits (MainPanel::baseWidth * 5 / 10, MainPanel::baseHeight * 5 / 10, MainPanel::baseWidth * 2, MainPanel::baseHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) MainPanel::baseWidth / MainPanel::baseHeight);
    setSize (MainPanel::baseWidth * 9 / 10, MainPanel::baseHeight * 9 / 10);

    startTimerHz (60);
}

VocalChainEditor::~VocalChainEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void VocalChainEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
}

void VocalChainEditor::resized()
{
    const float scale = (float) getWidth() / (float) MainPanel::baseWidth;
    panel.setTransform (juce::AffineTransform::scale (scale));
    panel.setBounds (0, 0, MainPanel::baseWidth, MainPanel::baseHeight);
}
} // namespace lavc
