#include "PluginProcessor.h"
#include "gui/PluginEditor.h"

namespace lavc
{
namespace
{
uint32_t packOrder (const ModuleOrder& o) noexcept
{
    uint32_t v = 0;
    for (int i = 0; i < kNumOrderable; ++i)
        v |= (uint32_t) (o[(size_t) i] & 7) << (3 * i);
    return v | 0x80000000u;
}

ModuleOrder unpackOrder (uint32_t v) noexcept
{
    if ((v & 0x80000000u) == 0)
        return defaultModuleOrder();
    ModuleOrder o {};
    for (int i = 0; i < kNumOrderable; ++i)
        o[(size_t) i] = (int) ((v >> (3 * i)) & 7);
    return o;
}

juce::String orderString (const ModuleOrder& o)
{
    char buf[kNumOrderable + 1];
    orderToString (o, buf);
    return buf;
}
} // namespace

VocalChainProcessor::VocalChainProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LAVocalChain", createParameterLayout()),
      params (apvts)
{
    listener.owner = this;
    packedOrder.store (packOrder (defaultModuleOrder()));
    apvts.addParameterListener (ParamID::mode, this);
}

VocalChainProcessor::~VocalChainProcessor()
{
    cancelPendingUpdate();
    apvts.removeParameterListener (ParamID::mode, this);
}

bool VocalChainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    return in.size() <= out.size();
}

void VocalChainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedRate = sampleRate;
    preparedBlockSize = std::max (32, samplesPerBlock);
    rebuildEngine();
}

void VocalChainProcessor::rebuildEngine()
{
    if (preparedRate <= 0.0)
        return;
    const bool tracking = params.get (P::mode) > 0.5f;
    params.read (engineParams);
    engineParams.order = unpackOrder (packedOrder.load());
    engine.setParams (engineParams);
    engine.prepare (preparedRate, preparedBlockSize, tracking);
    engineTracking.store (tracking);
    engineSampleRate.store (preparedRate);
    setLatencySamples (engine.getLatency());
}

void VocalChainProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == ParamID::mode)
        triggerAsyncUpdate();
}

void VocalChainProcessor::handleAsyncUpdate()
{
    const bool tracking = params.get (P::mode) > 0.5f;
    if (tracking == engineTracking.load() || preparedRate <= 0.0)
        return;
    suspendProcessing (true);
    rebuildEngine();
    suspendProcessing (false);
}

//==============================================================================
void VocalChainProcessor::handleMidi (const juce::MidiBuffer& midi) noexcept
{
    auto& pe = engine.getPitchEngine();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            pe.noteOn (m.getNoteNumber());
        else if (m.isNoteOff())
            pe.noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            pe.allNotesOff();
    }
}

void VocalChainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();
    if (numIn == 1 && numOut > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples); // mono in, stereo out
    else
        for (int ch = numIn; ch < numOut; ++ch)
            buffer.clear (ch, 0, numSamples);

    handleMidi (midi);

    // ---- Requests from the UI ----------------------------------------------------------------
    const int as = assistState.load();
    if (as == 1)
        engine.setAssistActive (true);
    else if (as == 2)
    {
        engine.setAssistActive (false);
        assistState.store (3);
    }
    const bool learnKey = keyLearn.load();
    engine.getPitchEngine().setKeyLearning (learnKey || as == 1);
    if (learnKey != keyLearnActive.load())
    {
        if (learnKey)
            engine.getPitchEngine().resetKeyHistogram();
        keyLearnActive.store (learnKey);
    }

    params.read (engineParams);
    engineParams.tracking = engineTracking.load();
    engineParams.order = unpackOrder (packedOrder.load (std::memory_order_relaxed));
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                engineParams.bpm = *bpm;
    engine.setParams (engineParams);

    const int channels = std::min (2, numOut);
    for (int start = 0; start < numSamples; start += preparedBlockSize)
    {
        const int n = std::min (preparedBlockSize, numSamples - start);
        float* ptrs[2] = { buffer.getWritePointer (0, start), buffer.getWritePointer (channels > 1 ? 1 : 0, start) };
        engine.process (ptrs, channels, n, &listener);
        spectrumFifo.push (engine.getAnalysisBuffer(), n);
    }

    if (as == 1)
        assistSeconds.store ((double) engine.getAssistStats().samples / std::max (1.0, preparedRate));

    const auto& m = engine.getMeters();
    meters.inPeak.store (std::max (meters.inPeak.load (std::memory_order_relaxed), m.inPeak), std::memory_order_relaxed);
    meters.outPeak.store (std::max (meters.outPeak.load (std::memory_order_relaxed), m.outPeak), std::memory_order_relaxed);
    meters.gate.store (m.gateDb, std::memory_order_relaxed);
    meters.level.store (m.levelDb, std::memory_order_relaxed);
    meters.deess.store (m.deessDb, std::memory_order_relaxed);
    meters.comp.store (m.compDb, std::memory_order_relaxed);
    meters.limit.store (m.limitDb, std::memory_order_relaxed);
    meters.breath.store (m.breath, std::memory_order_relaxed);
    for (size_t b = 0; b < m.sculpt.size(); ++b)
        meters.sculpt[b].store (m.sculpt[b], std::memory_order_relaxed);

    midi.clear();
}

//==============================================================================
ModuleOrder VocalChainProcessor::getModuleOrder() const noexcept
{
    return unpackOrder (packedOrder.load());
}

void VocalChainProcessor::setModuleOrder (const ModuleOrder& order)
{
    packedOrder.store (packOrder (order));
    apvts.state.setProperty ("order", orderString (order), nullptr);
}

void VocalChainProcessor::setParam (const char* id, float value)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

//==============================================================================
void VocalChainProcessor::startAssist()
{
    assistSeconds.store (0.0);
    assistState.store (1);
}

juce::String VocalChainProcessor::finishAssist()
{
    if (assistState.load() != 1)
        return {};
    assistState.store (2);
    // Wait for the audio thread to acknowledge (it stops writing the statistics)
    for (int i = 0; i < 40 && assistState.load() != 3; ++i)
        juce::Thread::sleep (5);
    assistState.store (0);

    const auto& st = engine.getAssistStats();
    const float voicedLevel = AssistStats::percentile (st.voicedFrames, 60.0f, -200.0f);
    if (voicedLevel < -150.0f)
        return {};

    juce::StringArray summary;

    // Input gain: bring sung parts to about -18 dBFS RMS
    const float gain = juce::jlimit (-24.0f, 24.0f, -18.0f - voicedLevel);
    setParam (ParamID::inGain, gain);
    summary.add ("Gain " + juce::String (gain, 1) + " dB");

    // Gate just above the noise floor (only when there is a clear gap)
    const float noise = AssistStats::percentile (st.allFrames, 10.0f, -90.0f);
    if (voicedLevel - noise > 25.0f)
    {
        const float thr = juce::jlimit (-90.0f, -20.0f, noise + gain + 8.0f);
        setParam (ParamID::gateThreshold, thr);
        setParam (ParamID::gateOn, 1.0f);
        summary.add ("Gate " + juce::String (thr, 0) + " dB");
    }

    // De-esser threshold under the loudest sibilants
    const float sib = AssistStats::percentile (st.sibilance, 90.0f, -200.0f);
    if (sib > -150.0f)
    {
        const float thr = juce::jlimit (-60.0f, 0.0f, sib + gain - 4.0f);
        setParam (ParamID::deessThreshold, thr);
        setParam (ParamID::deessOn, 1.0f);
        summary.add ("De-ess " + juce::String (thr, 0) + " dB");
    }

    // Low cut below the lowest sung notes
    int lowNote = -1, total = 0;
    for (int v : st.pitchHist) total += v;
    for (int n = 0, acc = 0; n < 128 && total > 0; ++n)
    {
        acc += st.pitchHist[(size_t) n];
        if (acc >= total / 20) { lowNote = n; break; }
    }
    if (lowNote > 0)
    {
        const float f0 = 440.0f * std::pow (2.0f, (float) (lowNote - 69) / 12.0f);
        const float cut = juce::jlimit (50.0f, 160.0f, f0 * 0.7f);
        setParam (ParamID::lowCut, cut);
        summary.add ("Low cut " + juce::String (juce::roundToInt (cut)) + " Hz");
    }

    // Compression around the (new) vocal level, leveler target to match
    setParam (ParamID::compThreshold, juce::jlimit (-40.0f, -6.0f, -18.0f - 3.0f));
    setParam (ParamID::compOn, 1.0f);
    setParam (ParamID::levelTarget, -18.0f);
    setParam (ParamID::sculptOn, 1.0f);

    // Key
    const auto key = engine.getPitchEngine().estimateKey();
    if (key.seconds > 2.0f && key.confidence > 0.25f)
    {
        lastKey = key;
        setParam (ParamID::tuneKey, (float) key.key);
        setParam (ParamID::tuneScale, (float) (key.minor ? Scale::minor : Scale::major));
        summary.add (juce::String ("Key ") + keyName (key.key) + (key.minor ? " minor" : " major"));
    }
    return summary.joinIntoString ("  /  ");
}

void VocalChainProcessor::startKeyLearn()
{
    keyLearn.store (true);
}

juce::String VocalChainProcessor::finishKeyLearn()
{
    keyLearn.store (false);
    for (int i = 0; i < 40 && keyLearnActive.load(); ++i)
        juce::Thread::sleep (5);
    keyLearnActive.store (false);
    const auto key = engine.getPitchEngine().estimateKey();
    if (key.seconds < 1.0f)
        return "Not enough singing to detect the key";
    lastKey = key;
    setParam (ParamID::tuneKey, (float) key.key);
    setParam (ParamID::tuneScale, (float) (key.minor ? Scale::minor : Scale::major));
    return juce::String ("Key: ") + keyName (key.key) + (key.minor ? " minor" : " major") + " ("
           + juce::String (juce::roundToInt (key.confidence * 100.0f)) + "% confidence)";
}

KeyEstimate VocalChainProcessor::getLastKeyEstimate() const noexcept { return lastKey; }

//==============================================================================
void VocalChainProcessor::resetToDefaults()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
}

void VocalChainProcessor::setCurrentProgram (int index)
{
    const auto& presets = getFactoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;
    currentProgram = index;
    currentPresetName = presets[(size_t) index].name;
    resetToDefaults();
    for (const auto& [id, value] : presets[(size_t) index].values)
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    setModuleOrder (presets[(size_t) index].order.isEmpty() ? defaultModuleOrder()
                                                           : orderFromString (presets[(size_t) index].order.toRawUTF8()));
}

const juce::String VocalChainProcessor::getProgramName (int index)
{
    const auto& presets = getFactoryPresets();
    return juce::isPositiveAndBelow (index, (int) presets.size()) ? presets[(size_t) index].name : juce::String();
}

juce::File VocalChainProcessor::getUserPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("LA Audio").getChildFile ("LA-VocalChain").getChildFile ("Presets");
}

bool VocalChainProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return false;
    auto folder = getUserPresetFolder();
    if (! folder.createDirectory())
        return false;
    auto state = apvts.copyState();
    state.setProperty ("order", orderString (getModuleOrder()), nullptr);
    state.setProperty ("presetName", name.trim(), nullptr);
    auto xml = state.createXml();
    if (xml == nullptr || ! xml->writeTo (folder.getChildFile (clean + ".lavc")))
        return false;
    currentPresetName = name.trim();
    return true;
}

bool VocalChainProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;
    auto tree = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (tree);
    setModuleOrder (orderFromString (tree.getProperty ("order").toString().toRawUTF8()));
    currentPresetName = tree.getProperty ("presetName", file.getFileNameWithoutExtension()).toString();
    currentProgram = -1;
    return true;
}

void VocalChainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("order", orderString (getModuleOrder()), nullptr);
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("presetName", currentPresetName, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VocalChainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto tree = juce::ValueTree::fromXml (*xml);
    currentProgram = (int) tree.getProperty ("program", 0);
    currentPresetName = tree.getProperty ("presetName", "Default").toString();
    apvts.replaceState (tree);
    setModuleOrder (orderFromString (tree.getProperty ("order").toString().toRawUTF8()));
}

juce::AudioProcessorEditor* VocalChainProcessor::createEditor()
{
    return new VocalChainEditor (*this);
}
} // namespace lavc

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new lavc::VocalChainProcessor();
}
