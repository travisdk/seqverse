#pragma once

#include "DarkLookAndFeel.h"
#include "PluginProcessor.h"

class SeqVerseEditor : public juce::AudioProcessorEditor
{
public:
    explicit SeqVerseEditor (SeqVerseProcessor&);
    ~SeqVerseEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // The attachment is declared after the component it drives, so it is destroyed first.
    struct Selector
    {
        juce::Label label;
        juce::ComboBox box;
        std::unique_ptr<juce::ComboBoxParameterAttachment> attachment;
    };

    // A slider with its name to the left and its value in a box to the right
    struct Setting
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
    };

    // Each control's component ID is its parameter ID, so tests can find it with findChildWithID().
    void setUpSelector (Selector&, const juce::String& text, juce::AudioParameterChoice&);
    void setUpRow (Setting&, const juce::String& text, juce::RangedAudioParameter&);

    // Controls that have no effect with the current settings are disabled (dimmed), label included.
    void updateEnabledControls();
    static void setEnabled (Selector&, bool);
    static void setEnabled (Setting&, bool);

    SeqVerseProcessor& processor;

    DarkLookAndFeel look; // declared before the components that use it, so that it outlives them

    juce::Label titleLabel;
    // In three columns: pitch, rhythm and feel
    Selector key, scale, octaveNote, playMode, arpDirection;
    Setting lowOctave, octaves;
    Selector stepRate, rhythm;
    Setting euclidSteps, euclidPulses, euclidRotation, cycleLength, swing;
    Selector noteLength;
    Setting lengthVariation, velocity, velocityVariation, chance, channel, seed;

    // The section headings, painted where resized() puts them
    struct Heading { juce::String text; juce::Rectangle<int> area; };
    std::vector<Heading> headings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeqVerseEditor)
};
