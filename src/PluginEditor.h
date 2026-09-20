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
    juce::Label titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenerativeMidiEditor)
};
