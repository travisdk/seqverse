#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// Generates MIDI notes while the host transport is playing. Incoming MIDI is ignored.
//
// The host timeline is divided into steps of the selected rate (locked to the host's PPQ position, so it follows
// loops and jumps). Each step has a chance of playing one note from the pool of notes of the selected key and scale
// over the selected octave range. Arpeggio mode walks the pool in the chosen direction, Random mode picks from it.
// Odd steps can be swung late, notes get a velocity with optional variation, and they sound for the selected length.
class GenerativeMidiProcessor : public juce::AudioProcessor,
                                private juce::AudioProcessorParameter::Listener,
                                private juce::AsyncUpdater
{
public:
    GenerativeMidiProcessor();
    ~GenerativeMidiProcessor() override;

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
    juce::AudioParameterChoice* arpResetParam = nullptr;
    juce::AudioParameterChoice* stepRateParam = nullptr;
    juce::AudioParameterChoice* noteLengthParam = nullptr;
    juce::AudioParameterInt* chanceParam = nullptr;            // percent
    juce::AudioParameterInt* swingParam = nullptr;             // percent, 100 is a full triplet swing
    juce::AudioParameterInt* lowOctaveParam = nullptr;         // never above the high octave, see below
    juce::AudioParameterInt* highOctaveParam = nullptr;
    juce::AudioParameterInt* velocityParam = nullptr;
    juce::AudioParameterInt* velocityVariationParam = nullptr; // plus or minus
    juce::AudioParameterInt* channelParam = nullptr;           // 1 to 16
    juce::AudioParameterInt* seedParam = nullptr;              // 0 is a different sequence every time

private:
    struct PendingNoteOff
    {
        int channel;
        int note;
        double position; // samples from the start of the current block
    };

    struct NewNote
    {
        int channel;
        int note;
        int velocity;
        int position; // samples from the start of the current block
    };

    // The low octave must not be above the high octave. processBlock copes with any combination, and this keeps
    // the two parameters themselves consistent: the one that was not just changed follows, on the message thread.
    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int, bool) override {}
    void handleAsyncUpdate() override;

    double sampleRate = 44100.0;
    double internalPpq = 0.0; // only used when the host does not report its PPQ position
    bool wasPlaying = false;
    double playStartQuarters = 0.0; // where the transport was when it last started playing
    juce::Random random;

    std::atomic<int> lastChangedOctave { 0 }; // 0 none, 1 low, 2 high

    std::vector<PendingNoteOff> pendingNoteOffs;
    std::vector<NewNote> newNotes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenerativeMidiProcessor)
};
