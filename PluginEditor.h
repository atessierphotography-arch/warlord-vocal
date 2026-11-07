#pragma once
#include "PluginProcessor.h"
#include <array>

// Look "materiel de studio" : panneau metal sombre, gros boutons a stries, voyants LED.
// La couleur d'accent change avec le mode (rouge = Scream, bleu = Clean).
class WarlordLookAndFeel : public juce::LookAndFeel_V4
{
public:
    juce::Colour accent { 0xffff3b2f };

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

class WarlordEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit WarlordEditor (WarlordProcessor&);
    ~WarlordEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override;
    void addKnob (juce::Slider&, const char* paramId, std::unique_ptr<SliderAttach>&, bool big);
    void updateMode (int activeMode);

    WarlordProcessor& proc;
    WarlordLookAndFeel laf;

    juce::Slider input, thresh, ratio, attack, release, sat, presence, mix, output;
    std::unique_ptr<SliderAttach> inputA, threshA, ratioA, attackA, releaseA, satA, presenceA, mixA, outputA;

    juce::TextButton screamButton { "SCREAM" }, cleanButton { "CLEAN" };
    juce::ParameterAttachment modeAttach;

    int currentMode = 0;
    float grLevel = 0.0f;
    int lastLit = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WarlordEditor)
};
