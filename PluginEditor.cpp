#include "PluginEditor.h"

namespace
{
    const juce::Colour textHi  (0xffe8e8ec);
    const juce::Colour textLo  (0xff9a9aa4);
    const juce::Colour textDim (0xff8a8a94);
    const juce::Colour screamAccent (0xffff3b2f), cleanAccent (0xff3aa0ff);

    constexpr int W = 1000, H = 330, knobCy = 190;

    struct KnobPos { int cx, size; };
    const KnobPos kInput { 90, 84 }, kThresh { 190, 84 }, kRatio { 290, 84 }, kAttack { 390, 84 }, kRelease { 490, 84 },
                  kSat { 625, 104 }, kPresence { 730, 84 }, kMix { 820, 84 }, kOutput { 910, 84 };

    const juce::Rectangle<int> ledArea (768, 10, 192, 50);

    juce::Font bold (float h, float kern = 0.1f)
    {
        return juce::Font (juce::FontOptions().withHeight (h).withStyle ("Bold")).withExtraKerningFactor (kern);
    }
    juce::Font regular (float h, float kern = 0.05f)
    {
        return juce::Font (juce::FontOptions().withHeight (h)).withExtraKerningFactor (kern);
    }

    int litLeds (float grDb) { return juce::jlimit (0, 12, (int) std::ceil (grDb / 2.0f)); }   // 2 dB par voyant

    void drawDivider (juce::Graphics& g, int x, int y, int w, int h)
    {
        g.setColour (juce::Colour (0xff0b0b0d));
        g.fillRect (x, y, w, h);
        g.setColour (juce::Colour (0xff3c3c42));
        g.fillRect (x + (w == 1 ? 1 : 0), y + (h == 1 ? 1 : 0), w, h);
    }

    void drawKnobLabel (juce::Graphics& g, const char* name, const juce::String& value, const KnobPos& k)
    {
        const bool big = k.size > 100;
        const int top = knobCy + k.size / 2 + 6;
        g.setColour (textHi);
        g.setFont (bold (big ? 15.0f : 13.0f, 0.12f));
        g.drawText (name, k.cx - 60, top, 120, 20, juce::Justification::centred);
        g.setColour (textLo);
        g.setFont (regular (13.0f, 0.05f));
        g.drawText (value, k.cx - 60, top + 18, 120, 18, juce::Justification::centred);
    }

    juce::String fmt (const juce::Slider& s, int decimals, const char* suffix, bool showPlus = false)
    {
        auto v = juce::String (s.getValue(), decimals);
        if (showPlus && s.getValue() > 0.0) v = "+" + v;
        return v + suffix;
    }
}

//==============================================================================
void WarlordLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float startA, float endA, juce::Slider& s)
{
    const bool big = (bool) s.getProperties().getWithDefault ("big", false);
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float k = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 100.0f;
    const auto c = bounds.getCentre();
    const float angle = startA + pos * (endA - startA);

    auto pt = [&] (float r, float a) { return juce::Point<float> (c.x + r * k * std::sin (a), c.y - r * k * std::cos (a)); };

    // Repères : 11 traits, allumes jusqu'a la valeur
    for (int t = 0; t < 11; ++t)
    {
        const float a = startA + (endA - startA) * (float) t / 10.0f;
        g.setColour ((float) t / 10.0f <= pos + 0.001f ? accent : juce::Colour (0xff5a5a62));
        g.drawLine (juce::Line<float> (pt (43.0f, a), pt (48.0f, a)), 2.0f * k);
    }

    // Corps metal
    juce::ColourGradient body (juce::Colour (0xff5c5c63), c.x, c.y - 15.0f * k, juce::Colour (0xff1a1a1d), c.x, c.y + 40.0f * k, true);
    g.setGradientFill (body);
    g.fillEllipse (c.x - 36.0f * k, c.y - 36.0f * k, 72.0f * k, 72.0f * k);
    g.setColour (juce::Colour (0xff09090a));
    g.drawEllipse (c.x - 36.0f * k, c.y - 36.0f * k, 72.0f * k, 72.0f * k, 2.0f);

    // Stries de prise
    g.setColour (juce::Colour (0xff0e0e10).withAlpha (0.55f));
    for (int i = 0; i < 72; ++i)
    {
        const float a = (float) i * juce::MathConstants<float>::twoPi / 72.0f;
        g.drawLine (juce::Line<float> (pt (32.5f, a), pt (35.5f, a)), 1.2f * k);
    }

    // Face superieure
    juce::ColourGradient face (juce::Colour (0xff46464c), c.x, c.y - 27.0f * k, juce::Colour (0xff232326), c.x, c.y + 27.0f * k, false);
    g.setGradientFill (face);
    g.fillEllipse (c.x - 27.0f * k, c.y - 27.0f * k, 54.0f * k, 54.0f * k);
    g.setColour (juce::Colour (0xff0e0e10));
    g.drawEllipse (c.x - 27.0f * k, c.y - 27.0f * k, 54.0f * k, 54.0f * k, 1.5f);

    // Trait repere
    juce::Path mark;
    mark.startNewSubPath (pt (14.0f, angle));
    mark.lineTo (pt (26.0f, angle));
    g.setColour (big ? accent : juce::Colour (0xfff2f2f4));
    g.strokePath (mark, juce::PathStrokeType (3.5f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void WarlordLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool)
{
    const auto r = b.getLocalBounds().toFloat();
    const bool active = (bool) b.getProperties().getWithDefault ("active", false);

    if (active)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff45454b), 0.0f, r.getY(), juce::Colour (0xff2c2c30), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colour (0xff5a5a62));
        g.drawLine (r.getX() + 6.0f, r.getY() + 1.0f, r.getRight() - 6.0f, r.getY() + 1.0f);
    }
    else if (over)
    {
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (r, 6.0f);
    }

    // Voyant LED
    const juce::Point<float> led (r.getX() + 30.0f, r.getCentreY());
    if (active)
    {
        g.setColour (accent.withAlpha (0.3f));
        g.fillEllipse (led.x - 8.0f, led.y - 8.0f, 16.0f, 16.0f);
    }
    g.setColour (active ? accent : juce::Colour (0xff2c2c30));
    g.fillEllipse (led.x - 4.5f, led.y - 4.5f, 9.0f, 9.0f);
}

void WarlordLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool active = (bool) b.getProperties().getWithDefault ("active", false);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (active ? juce::Colours::white : textDim);
    g.drawText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft (22), juce::Justification::centred);
}

juce::Font WarlordLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return bold (16.0f, 0.2f);
}

//==============================================================================
WarlordEditor::WarlordEditor (WarlordProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      modeAttach (*p.apvts.getParameter (ids::mode),
                  [this] (float v) { updateMode ((int) std::round (v)); })
{
    addKnob (input,    ids::input,    inputA,    false);
    addKnob (thresh,   ids::thresh,   threshA,   false);
    addKnob (ratio,    ids::ratio,    ratioA,    false);
    addKnob (attack,   ids::attack,   attackA,   false);
    addKnob (release,  ids::release,  releaseA,  false);
    addKnob (sat,      ids::sat,      satA,      true);
    addKnob (presence, ids::presence, presenceA, false);
    addKnob (mix,      ids::mix,      mixA,      false);
    addKnob (output,   ids::output,   outputA,   false);

    screamButton.setLookAndFeel (&laf);
    cleanButton.setLookAndFeel (&laf);
    screamButton.onClick = [this] { proc.setModeWithDefaults (Dsp::Scream); };
    cleanButton.onClick  = [this] { proc.setModeWithDefaults (Dsp::Clean); };
    addAndMakeVisible (screamButton);
    addAndMakeVisible (cleanButton);

    modeAttach.sendInitialUpdate();
    setSize (W, H);
    startTimerHz (30);
}

WarlordEditor::~WarlordEditor()
{
    for (auto* s : { &input, &thresh, &ratio, &attack, &release, &sat, &presence, &mix, &output })
        s->setLookAndFeel (nullptr);
    screamButton.setLookAndFeel (nullptr);
    cleanButton.setLookAndFeel (nullptr);
}

void WarlordEditor::addKnob (juce::Slider& s, const char* paramId, std::unique_ptr<SliderAttach>& attach, bool big)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    s.getProperties().set ("big", big);
    s.setLookAndFeel (&laf);
    s.onValueChange = [this] { repaint (30, 225, W - 60, 70); };   // met a jour les valeurs affichees
    addAndMakeVisible (s);
    attach = std::make_unique<SliderAttach> (proc.apvts, paramId, s);

    if (auto* param = proc.apvts.getParameter (paramId))   // double-clic = valeur par defaut
        s.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
}

void WarlordEditor::updateMode (int mode)
{
    currentMode = mode;
    const bool scream = (mode == Dsp::Scream);
    laf.accent = scream ? screamAccent : cleanAccent;
    screamButton.getProperties().set ("active", scream);
    cleanButton.getProperties().set ("active", ! scream);
    lastLit = -1;
    repaint();
}

void WarlordEditor::resized()
{
    auto place = [] (juce::Component& c, const KnobPos& k) { c.setBounds (k.cx - k.size / 2, knobCy - k.size / 2, k.size, k.size); };
    place (input, kInput);   place (thresh, kThresh);   place (ratio, kRatio);
    place (attack, kAttack); place (release, kRelease); place (sat, kSat);
    place (presence, kPresence); place (mix, kMix);     place (output, kOutput);

    screamButton.setBounds (353, 21, 146, 38);
    cleanButton.setBounds (501, 21, 146, 38);
}

void WarlordEditor::timerCallback()
{
    grLevel = juce::jmax (proc.grPeak.exchange (0.0f), grLevel * 0.90f);
    proc.inPeak.store (0.0f);     // l'interface n'affiche plus ces niveaux
    proc.outPeak.store (0.0f);

    const int lit = litLeds (grLevel);
    if (lit != lastLit)
    {
        lastLit = lit;
        repaint (ledArea);
    }
}

void WarlordEditor::paint (juce::Graphics& g)
{
    const bool scream = (currentMode == Dsp::Scream);
    const auto accent = laf.accent;

    // Panneau metal
    g.fillAll (juce::Colour (0xff0d0d0e));
    const auto panel = getLocalBounds().toFloat().reduced (1.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff323236), 0.0f, 0.0f, juce::Colour (0xff1b1b1e), 0.0f, (float) H, false));
    g.fillRoundedRectangle (panel, 10.0f);

    {   // Fines stries verticales (metal brosse)
        juce::Path clipPath;
        clipPath.addRoundedRectangle (panel, 10.0f);
        g.saveState();
        g.reduceClipRegion (clipPath);
        const auto clip = g.getClipBounds();
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        for (int x = clip.getX() - (clip.getX() % 3); x < clip.getRight(); x += 3)
            g.fillRect (x, clip.getY(), 1, clip.getHeight());
        g.restoreState();
    }
    g.setColour (juce::Colour (0xff09090a));
    g.drawRoundedRectangle (panel, 10.0f, 2.0f);
    g.setColour (juce::Colour (0xff4c4c52));
    g.drawLine (10.0f, 3.0f, (float) W - 10.0f, 3.0f);

    // Vis
    for (auto p : { juce::Point<float> (12, 12), { 978, 12 }, { 12, 308 }, { 978, 308 } })
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff77777e), p.x + 3.5f, p.y + 3.0f, juce::Colour (0xff232326), p.x + 10.0f, p.y + 10.0f, true));
        g.fillEllipse (p.x, p.y, 10.0f, 10.0f);
        g.setColour (juce::Colour (0xff0b0b0c));
        g.drawEllipse (p.x, p.y, 10.0f, 10.0f, 1.0f);
    }

    // Titre
    g.setColour (juce::Colour (0xfff2f2f4));
    g.setFont (bold (34.0f, 0.15f));
    g.drawText ("WARLORD", 40, 14, 300, 38, juce::Justification::centredLeft);
    g.setColour (textDim);
    g.setFont (bold (12.0f, 0.3f));
    g.drawText ("VOCAL COMPRESSOR / SATURATOR", 42, 52, 300, 16, juce::Justification::centredLeft);

    // Logement du selecteur de mode
    g.setColour (juce::Colour (0xff0e0e10));
    g.fillRoundedRectangle (350.0f, 16.0f, 300.0f, 46.0f, 8.0f);
    g.setColour (juce::Colour (0xff3a3a40));
    g.drawRoundedRectangle (350.5f, 16.5f, 299.0f, 45.0f, 8.0f, 1.0f);

    // Voyants de reduction de gain
    g.setColour (textDim);
    g.setFont (bold (11.0f, 0.25f));
    g.drawText ("GAIN REDUCTION", ledArea.getX(), 14, 192, 14, juce::Justification::centredLeft);
    const int lit = litLeds (grLevel);
    for (int j = 0; j < 12; ++j)
    {
        const juce::Rectangle<float> led ((float) ledArea.getX() + (float) j * 16.0f, 34.0f, 12.0f, 16.0f);
        if (j < lit)
        {
            g.setColour (accent.withAlpha (0.3f));
            g.fillRoundedRectangle (led.expanded (2.0f), 3.0f);
        }
        g.setColour (j < lit ? accent : juce::Colour (0xff26262a));
        g.fillRoundedRectangle (led, 2.0f);
    }

    // Separateurs et titres de sections
    drawDivider (g, 40, 80, W - 80, 1);
    drawDivider (g, 40, 296, W - 80, 1);
    drawDivider (g, 548, 100, 1, 190);
    g.setColour (textDim);
    g.setFont (bold (12.0f, 0.3f));
    g.drawText ("COMPRESSOR", 40, 92, 300, 16, juce::Justification::centredLeft);
    g.drawText ("SATURATION", 573, 92, 300, 16, juce::Justification::centredLeft);

    // Noms et valeurs des boutons
    drawKnobLabel (g, "INPUT",      fmt (input, 1, " dB", true), kInput);
    drawKnobLabel (g, "THRESHOLD",  fmt (thresh, 1, " dB"),      kThresh);
    drawKnobLabel (g, "RATIO",      fmt (ratio, 1, " : 1"),      kRatio);
    drawKnobLabel (g, "ATTACK",     fmt (attack, attack.getValue() < 10.0 ? 1 : 0, " ms"), kAttack);
    drawKnobLabel (g, "RELEASE",    fmt (release, 0, " ms"),     kRelease);
    drawKnobLabel (g, "SATURATION", fmt (sat, 0, " %"),          kSat);
    drawKnobLabel (g, "PRESENCE",   fmt (presence, 1, " dB", true), kPresence);
    drawKnobLabel (g, "MIX",        fmt (mix, 0, " %"),          kMix);
    drawKnobLabel (g, "OUTPUT",     fmt (output, 1, " dB", true), kOutput);

    // Description du mode
    g.setColour (textDim);
    g.setFont (bold (11.0f, 0.25f));
    g.drawText (scream ? "AGGRESSIVE  //  2-STAGE COMPRESSION  +  HARD SATURATION"
                       : "SMOOTH  //  RMS COMPRESSION  +  TAPE SATURATION",
                40, 303, 600, 16, juce::Justification::centredLeft);
}
