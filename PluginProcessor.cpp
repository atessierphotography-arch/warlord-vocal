#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    struct ModeSettings { float input, thresh, ratio, attack, release, sat, presence, mix, output; };

    // Reglages de depart de chaque mode (tous modifiables ensuite)
    const ModeSettings screamDefaults { 6.0f, -28.0f, 8.0f,  2.0f,  60.0f, 70.0f, 5.0f,  85.0f, 0.0f };
    const ModeSettings cleanDefaults  { 0.0f, -20.0f, 3.5f, 12.0f, 120.0f, 25.0f, 3.0f, 100.0f, 0.0f };
}

juce::AudioProcessorValueTreeState::ParameterLayout WarlordProcessor::createParameterLayout()
{
    using Range = juce::NormalisableRange<float>;
    using PF = juce::AudioParameterFloat;
    using PID = juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (PID { ids::mode, 1 }, "Mode",
                juce::StringArray { "Scream", "Clean" }, (int) Dsp::Scream));
    layout.add (std::make_unique<PF> (PID { ids::input,    1 }, "Input",      Range (-12.0f, 24.0f, 0.1f), screamDefaults.input));
    layout.add (std::make_unique<PF> (PID { ids::thresh,   1 }, "Threshold",  Range (-48.0f, 0.0f, 0.1f),  screamDefaults.thresh));
    layout.add (std::make_unique<PF> (PID { ids::ratio,    1 }, "Ratio",      Range (1.0f, 20.0f, 0.1f, 0.5f), screamDefaults.ratio));
    layout.add (std::make_unique<PF> (PID { ids::attack,   1 }, "Attack",     Range (0.1f, 100.0f, 0.01f, 0.4f), screamDefaults.attack));
    layout.add (std::make_unique<PF> (PID { ids::release,  1 }, "Release",    Range (10.0f, 1000.0f, 0.1f, 0.4f), screamDefaults.release));
    layout.add (std::make_unique<PF> (PID { ids::sat,      1 }, "Saturation", Range (0.0f, 100.0f, 0.1f), screamDefaults.sat));
    layout.add (std::make_unique<PF> (PID { ids::presence, 1 }, "Presence",   Range (-6.0f, 9.0f, 0.1f),   screamDefaults.presence));
    layout.add (std::make_unique<PF> (PID { ids::mix,      1 }, "Mix",        Range (0.0f, 100.0f, 0.1f),  screamDefaults.mix));
    layout.add (std::make_unique<PF> (PID { ids::output,   1 }, "Output",     Range (-24.0f, 12.0f, 0.1f), screamDefaults.output));
    return layout;
}

WarlordProcessor::WarlordProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createParameterLayout())
{
    inputP = apvts.getRawParameterValue (ids::input);       threshP = apvts.getRawParameterValue (ids::thresh);
    ratioP = apvts.getRawParameterValue (ids::ratio);       attackP = apvts.getRawParameterValue (ids::attack);
    releaseP = apvts.getRawParameterValue (ids::release);   satP = apvts.getRawParameterValue (ids::sat);
    presenceP = apvts.getRawParameterValue (ids::presence); mixP = apvts.getRawParameterValue (ids::mix);
    outputP = apvts.getRawParameterValue (ids::output);     modeP = apvts.getRawParameterValue (ids::mode);
}

bool WarlordProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
        && layouts.getMainInputChannelSet() == out;
}

void WarlordProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };

    oversampling.initProcessing ((size_t) samplesPerBlock);
    oversampling.reset();
    const int latency = juce::roundToInt (oversampling.getLatencyInSamples());
    setLatencySamples (latency);

    dryDelay.prepare (spec);
    dryDelay.setDelay ((float) latency);
    dryDelay.reset();
    dryBuffer.setSize (2, samplesPerBlock);

    scHP.prepare (spec);
    scHP.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    scHP.setCutoffFrequency (110.0f);   // la detection ignore les graves (souffle, plosives)
    scHP.reset();

    dcHP.prepare (spec);
    dcHP.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    dcHP.setCutoffFrequency (20.0f);
    dcHP.reset();

    presence.reset();
    stage1.gr = 0.0f;
    stage2.gr = 0.0f;
    rmsState = 0.0f;

    const double osRate = sampleRate * (double) oversampling.getOversamplingFactor();
    inS.reset (sampleRate, 0.02);  outS.reset (sampleRate, 0.02);
    mixS.reset (sampleRate, 0.02); satS.reset (osRate, 0.02);
    inS.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputP->load()));
    outS.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputP->load()));
    mixS.setCurrentAndTargetValue (mixP->load() * 0.01f);
    const int mode = (int) modeP->load();
    satS.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (satP->load() * (mode == Dsp::Scream ? 0.30f : 0.14f)));
}

void WarlordProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int n     = buffer.getNumSamples();
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);
    if (numCh == 0 || n == 0)
        return;

    // ---- Parametres ----
    const int   mode      = (int) modeP->load();
    const bool  scream    = (mode == Dsp::Scream);
    const float thresh    = threshP->load();
    const float ratio     = ratioP->load();
    const float knee      = Dsp::kneeFor (mode);
    const float makeupDb  = -thresh * (1.0f - 1.0f / ratio) * 0.5f;   // rattrapage auto (moitie de la reduction max)
    const float aC1 = Dsp::timeCoef (attackP->load(), sr),  rC1 = Dsp::timeCoef (releaseP->load(), sr);
    const float aC2 = Dsp::timeCoef (Dsp::s2AttackMs, sr), rC2 = Dsp::timeCoef (Dsp::s2ReleaseMs, sr);
    const float rmsK = 1.0f - std::exp (-1.0f / (0.010f * (float) sr));   // fenetre RMS ~10 ms (mode Clean)

    inS.setTargetValue  (juce::Decibels::decibelsToGain (inputP->load()));
    outS.setTargetValue (juce::Decibels::decibelsToGain (outputP->load()));
    mixS.setTargetValue (mixP->load() * 0.01f);
    satS.setTargetValue (juce::Decibels::decibelsToGain (satP->load() * (scream ? 0.30f : 0.14f)));
    presence.set (sr, 4000.0f, presenceP->load());

    // ---- Etape 1 : gain d'entree, copie seche, compression ----
    dryBuffer.setSize (2, n, false, false, true);
    float* data[2] = { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };
    float inP = 0.0f, grMaxBlock = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float g = inS.getNextValue();
        float sc = 0.0f;

        for (int ch = 0; ch < numCh; ++ch)
        {
            inP = juce::jmax (inP, std::abs (data[ch][i]));
            const float x = data[ch][i] * g;
            data[ch][i] = x;
            dryDelay.pushSample (ch, x);
            dryBuffer.setSample (ch, i, dryDelay.popSample (ch));
            sc = juce::jmax (sc, std::abs (scHP.processSample (ch, x)));
        }

        float det = sc;                                   // Scream : detection crete
        if (! scream)                                     // Clean : detection de type RMS (plus musicale)
        {
            rmsState += rmsK * (sc * sc - rmsState);
            det = std::sqrt (rmsState);
        }

        const float levelDb = juce::Decibels::gainToDecibels (det, -120.0f);
        const float gr1 = stage1.process (Dsp::staticGr (levelDb, thresh, ratio, knee), aC1, rC1);
        float gr2 = 0.0f;
        if (scream)
            gr2 = stage2.process (Dsp::staticGr (levelDb + gr1, thresh + Dsp::s2Offset, Dsp::s2Ratio, Dsp::s2Knee), aC2, rC2);
        else
            stage2.gr = 0.0f;

        const float totalGr = gr1 + gr2;
        grMaxBlock = juce::jmax (grMaxBlock, -totalGr);

        const float gain = juce::Decibels::decibelsToGain (totalGr + makeupDb);
        for (int ch = 0; ch < numCh; ++ch)
            data[ch][i] *= gain;
    }

    // ---- Etape 2 : saturation (surechantillonnage x4) ----
    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);
    auto osBlock = oversampling.processSamplesUp (block);
    const size_t osN = osBlock.getNumSamples();

    for (size_t i = 0; i < osN; ++i)
    {
        const float gs = satS.getNextValue();
        const float comp = 1.0f / std::sqrt (gs);       // compense la hausse de niveau due au drive
        for (size_t ch = 0; ch < (size_t) numCh; ++ch)
        {
            float* d = osBlock.getChannelPointer (ch);
            d[i] = Dsp::saturate (d[i] * gs, mode) * comp;
        }
    }
    oversampling.processSamplesDown (block);

    // ---- Etape 3 : anti-DC, presence, mix parallele, sortie ----
    float outPk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float m = mixS.getNextValue();
        const float o = outS.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
        {
            float wet = dcHP.processSample (ch, data[ch][i]);
            wet = presence.process (ch, wet);
            data[ch][i] = (wet * m + dryBuffer.getSample (ch, i) * (1.0f - m)) * o;
            outPk = juce::jmax (outPk, std::abs (data[ch][i]));
        }
    }

    inPeak.store  (juce::jmax (inPeak.load(), inP));
    outPeak.store (juce::jmax (outPeak.load(), outPk));
    grPeak.store  (juce::jmax (grPeak.load(), grMaxBlock));
}

void WarlordProcessor::setModeWithDefaults (int mode)
{
    const auto& s = (mode == Dsp::Scream) ? screamDefaults : cleanDefaults;

    auto set = [this] (const char* id, float value)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    set (ids::mode, (float) mode);
    set (ids::input, s.input);       set (ids::thresh, s.thresh);
    set (ids::ratio, s.ratio);       set (ids::attack, s.attack);
    set (ids::release, s.release);   set (ids::sat, s.sat);
    set (ids::presence, s.presence); set (ids::mix, s.mix);
    set (ids::output, s.output);
}

juce::AudioProcessorEditor* WarlordProcessor::createEditor() { return new WarlordEditor (*this); }

void WarlordProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void WarlordProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new WarlordProcessor(); }
