#include "PluginEditor.h"

GenerativeMidiEditor::GenerativeMidiEditor (GenerativeMidiProcessor& p)
    : AudioProcessorEditor (&p)
{
    titleLabel.setText ("Generative MIDI", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setFont (juce::Font (32.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colour (0xff14181c));
    addAndMakeVisible (titleLabel);

    setSize (400, 300);
}

void GenerativeMidiEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::lightblue);
}

void GenerativeMidiEditor::resized()
{
    titleLabel.setBounds (getLocalBounds().reduced (10));
}
