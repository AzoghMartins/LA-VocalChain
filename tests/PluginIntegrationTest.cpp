// Integration test: drives the real LA-VocalChain processor + editor the way a host would.
//  - checks bus layouts, latency per mode, state save/restore (incl. module order), presets,
//    MIDI-driven harmonies, the Assistant, and renders the editor to a PNG:  lavc_integration [screenshot.png]
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../Source/PluginProcessor.h"
#include "../Source/gui/PluginEditor.h"

#include <random>

using namespace lavc;

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::printf ("%s %s\n", ok ? "  ok  " : "  FAIL", what.toRawUTF8());
    if (! ok)
        ++failures;
}

constexpr double sr = 48000.0;

/** A sung phrase: notes with vibrato, consonant noise bursts, breaths and a quiet noise floor. */
juce::AudioBuffer<float> makeVocal (double seconds, float amp = 0.15f)
{
    juce::AudioBuffer<float> buf (2, (int) (seconds * sr));
    std::mt19937 rng (42);
    std::normal_distribution<float> noise (0.0f, 1.0f);
    const float melody[] = { 69, 72, 76, 74, 72, 71, 69, 64 };
    double phase = 0.0;
    float hp = 0.0f, prev = 0.0f;
    for (int i = 0; i < buf.getNumSamples(); ++i)
    {
        const double t = i / sr;
        const int note = (int) (t / 0.4);
        const double inNote = t - note * 0.4;
        float s = 0.0003f * noise (rng);
        if (inNote > 0.03 && inNote < 0.34)
        {
            const float m = melody[note % 8] - 12.0f + 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t)
                            + 0.25f; // slightly sharp
            phase += 440.0 * std::pow (2.0, (m - 69.0) / 12.0) / sr;
            float v = 0.0f;
            for (int h = 1; h <= 10; ++h)
                v += (float) std::sin (2.0 * juce::MathConstants<double>::pi * h * phase) / (float) h;
            s += amp * v;
        }
        else if (inNote <= 0.03 && note % 2 == 0)
        {
            const float n = noise (rng);
            hp = 0.7f * (hp + n - prev);
            prev = n;
            s += 0.12f * hp; // "s" consonant
        }
        buf.setSample (0, i, s);
        buf.setSample (1, i, s);
    }
    return buf;
}

void run (VocalChainProcessor& proc, juce::AudioBuffer<float>& audio, int block, VocalChainEditor* editor = nullptr,
          juce::MidiBuffer* midiFirst = nullptr)
{
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    for (int start = 0; start + block <= audio.getNumSamples(); start += block)
    {
        for (int ch = 0; ch < 2; ++ch)
            buf.copyFrom (ch, 0, audio, ch, start, block);
        midi.clear();
        if (start == 0 && midiFirst != nullptr)
            midi = *midiFirst;
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            audio.copyFrom (ch, start, buf, ch, 0, block);
        if (editor != nullptr && (start / block) % 6 == 0)
            editor->updateDisplays();
    }
}

void setParam (VocalChainProcessor& p, const char* id, float value)
{
    auto* param = p.getState().getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

float param (VocalChainProcessor& p, const char* id) { return p.getState().getRawParameterValue (id)->load(); }
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf ("LA-VocalChain plugin integration test\n");

    auto proc = std::make_unique<VocalChainProcessor>();
    check (proc->acceptsMidi() && ! proc->producesMidi(), "accepts MIDI (for MIDI tune / harmonies)");

    {
        juce::AudioProcessor::BusesLayout mono, stereo, monoToStereo, bad;
        mono.inputBuses.add (juce::AudioChannelSet::mono()); mono.outputBuses.add (juce::AudioChannelSet::mono());
        stereo.inputBuses.add (juce::AudioChannelSet::stereo()); stereo.outputBuses.add (juce::AudioChannelSet::stereo());
        monoToStereo.inputBuses.add (juce::AudioChannelSet::mono()); monoToStereo.outputBuses.add (juce::AudioChannelSet::stereo());
        bad.inputBuses.add (juce::AudioChannelSet::create5point1()); bad.outputBuses.add (juce::AudioChannelSet::create5point1());
        check (proc->checkBusesLayoutSupported (mono) && proc->checkBusesLayoutSupported (stereo)
                   && proc->checkBusesLayoutSupported (monoToStereo) && ! proc->checkBusesLayoutSupported (bad),
               "supports mono, stereo and mono->stereo; rejects 5.1");
    }

    proc->setPlayConfigDetails (2, 2, sr, 512);
    proc->prepareToPlay (sr, 512);
    const int mixLatency = proc->getLatencySamples();
    setParam (*proc, ParamID::mode, 1.0f);
    proc->prepareToPlay (sr, 512);
    const int trackLatency = proc->getLatencySamples();
    check (mixLatency > 0 && trackLatency < mixLatency && trackLatency < sr * 0.01,
           "latency: mixing " + juce::String (1000.0 * mixLatency / sr, 1) + " ms, tracking " + juce::String (1000.0 * trackLatency / sr, 1) + " ms");
    setParam (*proc, ParamID::mode, 0.0f);
    proc->prepareToPlay (sr, 512);

    // ---- Presets ---------------------------------------------------------------------------------
    bool presetsOk = proc->getNumPrograms() >= 12;
    for (int i = 0; i < proc->getNumPrograms(); ++i)
    {
        proc->setCurrentProgram (i);
        auto audio = makeVocal (1.0);
        run (*proc, audio, 512);
        float peak = audio.getMagnitude (0, audio.getNumSamples());
        bool finite = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < audio.getNumSamples(); ++s)
                finite &= std::isfinite (audio.getSample (ch, s));
        presetsOk &= finite && peak < 1.0f && peak > 0.01f;
        if (! (finite && peak < 1.0f && peak > 0.01f))
            std::printf ("        preset %s: peak %.3f finite %d\n", proc->getProgramName (i).toRawUTF8(), peak, (int) finite);
    }
    check (presetsOk, juce::String (proc->getNumPrograms()) + " factory presets all produce clean, limited output");
    proc->setCurrentProgram (1);
    check (proc->getProgramName (1) == "Modern Pop Lead" && param (*proc, ParamID::tuneOn) > 0.5f && param (*proc, ParamID::sculptOn) > 0.5f,
           "factory preset applies (Modern Pop Lead)");
    proc->setCurrentProgram (0);
    proc->prepareToPlay (sr, 512);

    // ---- State round trip (incl. module order) --------------------------------------------------------
    {
        setParam (*proc, ParamID::compThreshold, -31.0f);
        setParam (*proc, ParamID::hv3Interval, -5.0f);
        ModuleOrder o = defaultModuleOrder();
        std::reverse (o.begin(), o.end());
        proc->setModuleOrder (o);
        juce::MemoryBlock data;
        proc->getStateInformation (data);
        VocalChainProcessor copy;
        copy.setStateInformation (data.getData(), (int) data.getSize());
        bool same = true;
        for (auto* p : proc->getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                same &= std::abs (rp->getValue() - copy.getState().getParameter (rp->paramID)->getValue()) < 1.0e-5f;
        check (same, "state save/restore round trip (" + juce::String (proc->getParameters().size()) + " parameters)");
        check (copy.getModuleOrder() == o, "module order is saved with the session");
        proc->setModuleOrder (defaultModuleOrder());
    }

    // ---- User preset save / load ---------------------------------------------------------------------------
    {
        setParam (*proc, ParamID::revMix, 33.0f);
        const bool saved = proc->saveUserPreset ("Integration Test Preset");
        setParam (*proc, ParamID::revMix, 5.0f);
        const auto file = VocalChainProcessor::getUserPresetFolder().getChildFile ("Integration Test Preset.lavc");
        const bool loaded = proc->loadUserPreset (file);
        check (saved && loaded && std::abs (param (*proc, ParamID::revMix) - 33.0f) < 0.01f, "user preset save and load");
        file.deleteFile();
        proc->setCurrentProgram (0);
    }

    // ---- Assistant -----------------------------------------------------------------------------------------
    {
        proc->prepareToPlay (sr, 512);
        auto audio = makeVocal (6.0, 0.03f); // quiet singer
        proc->startAssist();
        run (*proc, audio, 512);
        const auto summary = proc->finishAssist();
        std::printf ("        assistant: %s\n", summary.toRawUTF8());
        check (summary.isNotEmpty() && param (*proc, ParamID::inGain) > 6.0f, "assistant raises the gain of a quiet vocal ("
                                                                                + juce::String (param (*proc, ParamID::inGain), 1) + " dB)");
        check (juce::roundToInt (param (*proc, ParamID::tuneKey)) == 9 && juce::roundToInt (param (*proc, ParamID::tuneScale)) == (int) Scale::minor,
               "assistant detects the key of the melody (A minor)");
        check (param (*proc, ParamID::deessOn) > 0.5f && param (*proc, ParamID::lowCut) > 49.0f, "assistant sets de-esser and low cut");
        proc->setCurrentProgram (0);
    }

    // ---- Editor + MIDI harmonies ------------------------------------------------------------------------------
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditorIfNeeded());
    auto* editor = dynamic_cast<VocalChainEditor*> (ed.get());
    check (editor != nullptr, "editor created");

    {
        proc->setCurrentProgram (8); // Stacked Harmonies
        proc->prepareToPlay (sr, 512);
        setParam (*proc, ParamID::tuneKey, 9.0f);
        setParam (*proc, ParamID::tuneScale, (float) Scale::minor);
        setParam (*proc, ParamID::satOn, 1.0f);
        setParam (*proc, ParamID::dlyOn, 1.0f);
        setParam (*proc, ParamID::gateOn, 1.0f);
        setParam (*proc, ParamID::breath, -10.0f);
        setParam (*proc, ParamID::eq2Gain, -3.0f);
        setParam (*proc, ParamID::eq4Gain, 3.5f);
        setParam (*proc, ParamID::eq5Gain, 4.0f);
        auto audio = makeVocal (4.0);
        run (*proc, audio, 480, editor);
        check (audio.getMagnitude (0, audio.getNumSamples()) > 0.05f, "harmony preset renders with the editor open");

        juce::MidiBuffer chord;
        chord.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
        chord.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        setParam (*proc, ParamID::harmMode, 2.0f);
        auto audio2 = makeVocal (1.0);
        run (*proc, audio2, 480, editor, &chord);
        check (audio2.getMagnitude (0, audio2.getNumSamples()) > 0.05f, "MIDI chord harmonies render");
        setParam (*proc, ParamID::harmMode, 0.0f);
    }

    if (argc > 1 && editor != nullptr)
    {
        editor->getPanel().setPage (argc > 2 ? juce::String (argv[2]).getIntValue() : 0);
        auto audio = makeVocal (3.0);
        run (*proc, audio, 480, editor);
        editor->setSize (MainPanel::baseWidth, MainPanel::baseHeight);
        for (int i = 0; i < 30; ++i)
            editor->updateDisplays();
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat png;
        check (stream.openedOk() && png.writeImageToStream (image, stream), "screenshot written to " + out.getFullPathName());
    }

    ed.reset();
    proc.reset();
    std::printf (failures == 0 ? "All integration checks passed\n" : "%d integration check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
