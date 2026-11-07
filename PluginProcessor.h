#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <cmath>

namespace Dsp
{
    enum Mode { Scream = 0, Clean = 1 };

    // Etage 2 (mode Scream uniquement) : compresseur rapide qui "ecrase" les pics restants
    inline constexpr float s2Offset = 4.0f;     // seuil = seuil principal + 4 dB
    inline constexpr float s2Ratio  = 12.0f;
    inline constexpr float s2Knee   = 2.0f;
    inline constexpr float s2AttackMs  = 0.3f;
    inline constexpr float s2ReleaseMs = 40.0f;

    inline float kneeFor (int mode) noexcept { return mode == Scream ? 3.0f : 9.0f; }

    // Reduction de gain statique (dB, <= 0) avec coude doux
    inline float staticGr (float levelDb, float thresh, float ratio, float knee) noexcept
    {
        const float over  = levelDb - thresh;
        const float slope = 1.0f / ratio - 1.0f;
        if (2.0f * over < -knee)              return 0.0f;
        if (2.0f * std::abs (over) <= knee)   { const float t = over + knee * 0.5f; return slope * t * t / (2.0f * knee); }
        return slope * over;
    }

    // Saturation : Scream = asymetrique et dure (harmoniques paires + impaires),
    //              Clean  = douce, type bande, tres legere asymetrie
    inline float saturate (float x, int mode) noexcept
    {
        if (mode == Scream)
            return x >= 0.0f ? std::tanh (1.5f * x) / 1.5f : std::tanh (x);
        return std::tanh (x + 0.15f) - 0.148885f;
    }

    // Mise a jour du gain de l'enveloppe (attaque/relache) sur la reduction en dB
    struct Ballistics
    {
        float gr = 0.0f;
        float process (float target, float attackCoef, float releaseCoef) noexcept
        {
            const float c = target < gr ? attackCoef : releaseCoef;
            gr = c * gr + (1.0f - c) * target;
            return gr;
        }
    };

    inline float timeCoef (float ms, double sampleRate) noexcept
    {
        return std::exp (-1.0f / (ms * 0.001f * (float) sampleRate));
    }

    // Filtre en plateau haut (RBJ), sans allocation
    struct HighShelf
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1[2] { 0, 0 }, z2[2] { 0, 0 };

        void set (double fs, float freq, float gainDb) noexcept
        {
            const float A = std::pow (10.0f, gainDb / 40.0f);
            const float w0 = juce::MathConstants<float>::twoPi * freq / (float) fs;
            const float cw = std::cos (w0), sw = std::sin (w0);
            const float alpha = sw * 0.5f * std::sqrt (2.0f);
            const float sA = 2.0f * std::sqrt (A) * alpha;
            const float a0 = (A + 1) - (A - 1) * cw + sA;
            b0 = A * ((A + 1) + (A - 1) * cw + sA) / a0;
            b1 = -2.0f * A * ((A - 1) + (A + 1) * cw) / a0;
            b2 = A * ((A + 1) + (A - 1) * cw - sA) / a0;
            a1 = 2.0f * ((A - 1) - (A + 1) * cw) / a0;
            a2 = ((A + 1) - (A - 1) * cw - sA) / a0;
        }
        float process (int ch, float x) noexcept
        {
            const float y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return y;
        }
        void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0f; }
    };
}

namespace ids
{
    inline constexpr const char* input    = "input";
    inline constexpr const char* thresh   = "thresh";
    inline constexpr const char* ratio    = "ratio";
    inline constexpr const char* attack   = "attack";
    inline constexpr const char* release  = "release";
    inline constexpr const char* sat      = "sat";
    inline constexpr const char* presence = "presence";
    inline constexpr const char* mix      = "mix";
    inline constexpr const char* output   = "output";
    inline constexpr const char* mode     = "mode";
}

class WarlordProcessor : public juce::AudioProcessor
{
public:
    WarlordProcessor();
    ~WarlordProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Warlord Vocal"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Choisit le mode ET charge les reglages de depart de ce mode (appele par l'interface)
    void setModeWithDefaults (int mode);

    juce::AudioProcessorValueTreeState apvts;

    // Niveaux lus par l'interface (remis a zero a chaque lecture)
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, grPeak { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* inputP = nullptr; std::atomic<float>* threshP = nullptr;
    std::atomic<float>* ratioP = nullptr; std::atomic<float>* attackP = nullptr;
    std::atomic<float>* releaseP = nullptr; std::atomic<float>* satP = nullptr;
    std::atomic<float>* presenceP = nullptr; std::atomic<float>* mixP = nullptr;
    std::atomic<float>* outputP = nullptr; std::atomic<float>* modeP = nullptr;

    juce::dsp::Oversampling<float> oversampling { 2, 2,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true };

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 4096 };
    juce::AudioBuffer<float> dryBuffer;

    juce::dsp::StateVariableTPTFilter<float> scHP, dcHP;   // filtre de detection / anti-DC
    Dsp::HighShelf presence;
    Dsp::Ballistics stage1, stage2;
    float rmsState = 0.0f;
    double sr = 44100.0;

    juce::SmoothedValue<float> inS, outS, mixS;   // cadence normale
    juce::SmoothedValue<float> satS;              // cadence surechantillonnee

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WarlordProcessor)
};
