#include "PluginEditor.h"
#include "Music.h"

namespace
{
const juce::Colour textColour (0xff14181c);
}

GenerativeMidiEditor::GenerativeMidiEditor (GenerativeMidiProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    titleLabel.setText ("Generative MIDI", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setFont (juce::Font (28.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, textColour);
    addAndMakeVisible (titleLabel);

    setUpSelector (key, "Key", *processor.keyParam);
    setUpSelector (scale, "Scale", *processor.scaleParam);
    setUpSelector (stepRate, "Step rate", *processor.stepRateParam);
    setUpSelector (playMode, "Play mode", *processor.playModeParam);
    setUpSelector (arpDirection, "Arp direction", *processor.arpDirectionParam);
    setUpSelector (arpReset, "Arp reset", *processor.arpResetParam);

    // The arp settings only matter in arpeggio mode. The attachment notifies on host changes too.
    const auto updateArpSelectors = [this]
    {
        const bool arpeggio = processor.playModeParam->getIndex() == music::arpeggio;
        arpDirection.box.setEnabled (arpeggio);
        arpReset.box.setEnabled (arpeggio);
    };
    playMode.box.onChange = updateArpSelectors;
    updateArpSelectors();

    setUpKnob (chance, "Chance", *processor.chanceParam);
    chance.slider.setTextValueSuffix (" %");

    setUpKnob (noteLength, "Note length", *processor.noteLengthParam);
    noteLength.slider.textFromValueFunction = [] (double value)
    { return juce::String (music::noteLengthNames[juce::jlimit (0, music::kNumNoteLengths - 1, juce::roundToInt (value))]); };
    noteLength.slider.updateText();

    setUpKnob (swing, "Swing", *processor.swingParam);
    swing.slider.setTextValueSuffix (" %");

    setUpKnob (velocity, "Velocity", *processor.velocityParam);
    setUpKnob (velocityVariation, "Velocity +/-", *processor.velocityVariationParam);
    setUpKnob (lowOctave, "Low octave", *processor.lowOctaveParam);
    setUpKnob (highOctave, "High octave", *processor.highOctaveParam);
    setUpKnob (channel, "Channel", *processor.channelParam);

    setUpKnob (seed, "Seed", *processor.seedParam);
    seed.slider.textFromValueFunction = [] (double value)
    { return value < 1.0 ? juce::String ("Auto") : juce::String (juce::roundToInt (value)); };
    seed.slider.updateText();

    setSize (660, 500);
}

void GenerativeMidiEditor::setUpSelector (Selector& selector, const juce::String& text, juce::AudioParameterChoice& parameter)
{
    selector.label.setText (text, juce::dontSendNotification);
    selector.label.setColour (juce::Label::textColourId, textColour);
    addAndMakeVisible (selector.label);

    selector.box.addItemList (parameter.choices, 1);
    addAndMakeVisible (selector.box);

    selector.attachment = std::make_unique<juce::ComboBoxParameterAttachment> (parameter, selector.box);
}

void GenerativeMidiEditor::setUpKnob (Knob& knob, const juce::String& text, juce::RangedAudioParameter& parameter)
{
    knob.label.setText (text, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setColour (juce::Label::textColourId, textColour);
    addAndMakeVisible (knob.label);

    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 22);
    addAndMakeVisible (knob.slider);

    knob.attachment = std::make_unique<juce::SliderParameterAttachment> (parameter, knob.slider);
}

void GenerativeMidiEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::lightblue);
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

    // Knobs: five in the first row, four in the second
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

    const int knobRowHeight = area.getHeight() / 2;
    placeKnobs (area.removeFromTop (knobRowHeight), { &chance, &noteLength, &swing, &velocity, &velocityVariation });
    placeKnobs (area, { &lowOctave, &highOctave, &channel, &seed });
}
