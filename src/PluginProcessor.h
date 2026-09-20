#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// Generates MIDI notes while the host transport is playing. Incoming MIDI is ignored.
//
// Every 1/8th step of the host timeline (locked to the host's PPQ position, so it follows loops and jumps) has a
// chance of playing one note from the selected key and scale. Arpeggio mode walks the scale's notes in the chosen
// direction, Random mode picks one at random. Notes are played in the octave of C4, each for the selected length.
class GenerativeMidiProcessor : public juce::AudioProcessor
{
public:
    GenerativeMidiProcessor();
    ~GenerativeMidiProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    // The MIDI input port stays declared (hosts such as Bitwig place the plugin in a note chain by it), but is ignored.
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Owned by the processor (addParameter). Read on the audio thread with get() / getIndex() only.
    juce::AudioParameterChoice* keyParam = nullptr;
    juce::AudioParameterChoice* scaleParam = nullptr;
    juce::AudioParameterChoice* playModeParam = nullptr;
    juce::AudioParameterChoice* arpDirectionParam = nullptr;
    juce::AudioParameterInt* chanceParam = nullptr; // percent
    juce::AudioParameterChoice* noteLengthParam = nullptr;

private:
    struct PendingNoteOff
    {
        int channel;
        int note;
        double position; // samples from the start of the current block
    };

    struct NewNote
    {
        int note;
        int position; // samples from the start of the current block
    };

    double sampleRate = 44100.0;
    double internalPpq = 0.0; // only used when the host does not report its PPQ position
    juce::Random random;

    std::vector<PendingNoteOff> pendingNoteOffs;
    std::vector<NewNote> newNotes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenerativeMidiProcessor)
};
