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
    setUpSelector (playMode, "Play mode", *processor.playModeParam);
    setUpSelector (arpDirection, "Arp direction", *processor.arpDirectionParam);

    // The arp direction only matters in arpeggio mode. The attachment notifies on host changes too.
    const auto updateArpDirection = [this] { arpDirection.box.setEnabled (processor.playModeParam->getIndex() == music::arpeggio); };
    playMode.box.onChange = updateArpDirection;
    updateArpDirection();

    setUpKnob (chance, "Chance", *processor.chanceParam);
    chance.slider.setTextValueSuffix (" %");

    setUpKnob (noteLength, "Note length", *processor.noteLengthParam);
    noteLength.slider.textFromValueFunction = [] (double value)
    { return juce::String (music::noteLengthNames[juce::jlimit (0, music::kNumNoteLengths - 1, juce::roundToInt (value))]); };
    noteLength.slider.updateText();

    setSize (480, 380);
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

    const auto placeSelectors = [] (juce::Rectangle<int> row, Selector& left, Selector& right)
    {
        const auto place = [] (juce::Rectangle<int> cell, Selector& selector)
        {
            selector.label.setBounds (cell.removeFromTop (20));
            selector.box.setBounds (cell.removeFromTop (26));
        };

        place (row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (8), left);
        place (row.withTrimmedLeft (8), right);
    };

    placeSelectors (area.removeFromTop (46), key, scale);
    area.removeFromTop (10);
    placeSelectors (area.removeFromTop (46), playMode, arpDirection);
    area.removeFromTop (14);

    const auto placeKnob = [] (juce::Rectangle<int> cell, Knob& knob)
    {
        knob.label.setBounds (cell.removeFromTop (20));
        knob.slider.setBounds (cell);
    };

    placeKnob (area.removeFromLeft (area.getWidth() / 2), chance);
    placeKnob (area, noteLength);
}
