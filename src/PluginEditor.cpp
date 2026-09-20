#include "PluginEditor.h"
#include "Music.h"

namespace
{
// JUCE draws its "Made with JUCE" badge in the bottom right corner of the editor. The last strip is left empty so
// that it does not cover the seed knob.
constexpr int kFooterHeight = 70;

// The space between the two rows of knobs, so that the value boxes of the first do not crowd the labels of the second
constexpr int kKnobRowGap = 30;

constexpr int kHeight = 500 + kFooterHeight + kKnobRowGap;
}

GenerativeMidiEditor::GenerativeMidiEditor (GenerativeMidiProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&look); // the components below take their look from here

    titleLabel.setText ("Generative MIDI", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setFont (juce::Font (28.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (titleLabel);

    setUpSelector (key, "Key", *processor.keyParam);
    setUpSelector (scale, "Scale", *processor.scaleParam);
    setUpSelector (stepRate, "Step rate", *processor.stepRateParam);
    setUpSelector (playMode, "Play mode", *processor.playModeParam);
    setUpSelector (arpDirection, "Arp direction", *processor.arpDirectionParam);
    setUpSelector (arpReset, "Arp reset", *processor.arpResetParam);

    setUpKnob (chance, "Chance", *processor.chanceParam);
    chance.slider.setTextValueSuffix (" %");

    setUpKnob (noteLength, "Note length", *processor.noteLengthParam);

    setUpKnob (swing, "Swing", *processor.swingParam);
    swing.slider.setTextValueSuffix (" %");

    setUpKnob (velocity, "Velocity", *processor.velocityParam);
    setUpKnob (velocityVariation, "Velocity +/-", *processor.velocityVariationParam);
    setUpKnob (lowOctave, "Low octave", *processor.lowOctaveParam);
    setUpKnob (highOctave, "High octave", *processor.highOctaveParam);
    setUpKnob (channel, "Channel", *processor.channelParam);

    // The value text ("Endless", "4.5 beats", "Auto") and what is typed into the value box are handled by the
    // parameters through the slider attachments, so the host shows the same text
    setUpKnob (cycleLength, "Cycle length", *processor.cycleLengthParam);
    setUpKnob (seed, "Seed", *processor.seedParam);

    // The attachments notify these on host changes (automation) as well as on changes made here
    playMode.box.onChange = [this] { updateEnabledControls(); };
    chance.slider.onValueChange = [this] { updateEnabledControls(); };
    velocityVariation.slider.onValueChange = [this] { updateEnabledControls(); };
    cycleLength.slider.onValueChange = [this] { updateEnabledControls(); };
    updateEnabledControls();

    // The value boxes of the sliders were made before the sliders were children of the editor, so they did not know
    // about its look yet. This makes every control take it.
    sendLookAndFeelChange();

    setSize (660, kHeight);
}

GenerativeMidiEditor::~GenerativeMidiEditor()
{
    setLookAndFeel (nullptr);
}

void GenerativeMidiEditor::updateEnabledControls()
{
    const bool arpeggio = processor.playModeParam->getIndex() == music::arpeggio;

    // Only the arpeggio walks the pool in a direction. It restarts on play unless it runs in a cycle, where
    // it starts over every cycle and the reset setting has no say.
    setEnabled (arpDirection, arpeggio);
    setEnabled (arpReset, arpeggio && processor.cycleLengthParam->get() == 0);

    // The seed only matters where something is random: the notes of Random mode, a chance below 100 %, or a velocity
    // variation. An arpeggio at 100 % with a fixed velocity has nothing to seed.
    const bool anythingRandom = ! arpeggio || processor.chanceParam->get() < 100 || processor.velocityVariationParam->get() > 0;
    setEnabled (seed, anythingRandom);
}

void GenerativeMidiEditor::setEnabled (Selector& selector, bool enabled)
{
    selector.label.setEnabled (enabled);
    selector.box.setEnabled (enabled);
}

void GenerativeMidiEditor::setEnabled (Knob& knob, bool enabled)
{
    knob.label.setEnabled (enabled);
    knob.slider.setEnabled (enabled);
}

void GenerativeMidiEditor::setUpSelector (Selector& selector, const juce::String& text, juce::AudioParameterChoice& parameter)
{
    selector.label.setText (text, juce::dontSendNotification);
    addAndMakeVisible (selector.label);

    selector.box.setComponentID (parameter.getParameterID());
    selector.box.addItemList (parameter.choices, 1);
    addAndMakeVisible (selector.box);

    selector.attachment = std::make_unique<juce::ComboBoxParameterAttachment> (parameter, selector.box);
}

void GenerativeMidiEditor::setUpKnob (Knob& knob, const juce::String& text, juce::RangedAudioParameter& parameter)
{
    knob.label.setText (text, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.slider.setComponentID (parameter.getParameterID());
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 22);
    addAndMakeVisible (knob.slider);

    knob.attachment = std::make_unique<juce::SliderParameterAttachment> (parameter, knob.slider);
}

void GenerativeMidiEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void GenerativeMidiEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    titleLabel.setBounds (area.removeFromTop (40));
    area.removeFromTop (8);

    // Three selectors across, two rows
    const auto placeSelectors = [] (juce::Rectangle<int> row, Selector& a, Selector& b, Selector& c)
    {
        const auto place = [] (juce::Rectangle<int> cell, Selector& selector)
        {
            cell.reduce (6, 0);
            selector.label.setBounds (cell.removeFromTop (20));
            selector.box.setBounds (cell.removeFromTop (26));
        };

        const int width = row.getWidth() / 3;
        place (row.removeFromLeft (width), a);
        place (row.removeFromLeft (width), b);
        place (row, c);
    };

    placeSelectors (area.removeFromTop (46), key, scale, stepRate);
    area.removeFromTop (10);
    placeSelectors (area.removeFromTop (46), playMode, arpDirection, arpReset);
    area.removeFromTop (14);
    area.removeFromBottom (kFooterHeight - 16); // the editor's 16 pixel margin is already taken off

    // Knobs: five in each of the two rows
    const auto placeKnobs = [] (juce::Rectangle<int> row, std::initializer_list<Knob*> knobs)
    {
        const int width = row.getWidth() / (int) knobs.size();
        for (auto* knob : knobs)
        {
            auto cell = row.removeFromLeft (width);
            knob->label.setBounds (cell.removeFromTop (20));
            knob->slider.setBounds (cell);
        }
    };

    const int knobRowHeight = (area.getHeight() - kKnobRowGap) / 2;
    placeKnobs (area.removeFromTop (knobRowHeight), { &chance, &noteLength, &swing, &velocity, &velocityVariation });
    area.removeFromTop (kKnobRowGap);
    placeKnobs (area, { &lowOctave, &highOctave, &channel, &cycleLength, &seed });
}
