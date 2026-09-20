#include "PluginProcessor.h"
#include "PluginEditor.h"

GenerativeMidiProcessor::GenerativeMidiProcessor()
    // A silent stereo output. Some hosts (Bitwig) do not run a plugin that has no audio ports.
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool GenerativeMidiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void GenerativeMidiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear(); // the MIDI buffer is left as it came in
}

juce::AudioProcessorEditor* GenerativeMidiProcessor::createEditor()
{
    return new GenerativeMidiEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GenerativeMidiProcessor();
}
