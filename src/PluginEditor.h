#pragma once

#include "PluginProcessor.h"

class GenerativeMidiEditor : public juce::AudioProcessorEditor
{
public:
    explicit GenerativeMidiEditor (GenerativeMidiProcessor&);
    ~GenerativeMidiEditor() override = default;

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

    struct Knob
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
    };

    // Each control's component ID is its parameter ID, so tests can find it with findChildWithID().
    void setUpSelector (Selector&, const juce::String& text, juce::AudioParameterChoice&);
    void setUpKnob (Knob&, const juce::String& text, juce::RangedAudioParameter&);

    // Controls that have no effect with the current settings are disabled (dimmed), label included.
    void updateEnabledControls();
    static void setEnabled (Selector&, bool);
    static void setEnabled (Knob&, bool);

    GenerativeMidiProcessor& processor;

    juce::Label titleLabel;
    Selector key, scale, stepRate, playMode, arpDirection, arpReset;
    Knob chance, noteLength, swing, velocity, velocityVariation;
    Knob lowOctave, highOctave, channel, seed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenerativeMidiEditor)
};
