#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// Turns incoming notes into a run of 1/8th notes in the major scale of the incoming note.
//
// Notes arriving with the same start time are one chord: the lowest of them is the root, the others are dropped.
// While the root is held, the plugin plays 8ths that climb the major scale from the root (C4: C D E F G A B C D ...),
// starting on the root itself. The first 8th starts with the incoming note and the run ends with its note-off,
// so a 1/1 note gives 8 eighths. The last eighth is always played to its end.
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
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // MIDI note of scale step `step` (0 is the root) in the major scale of `root`, steps past 6 continue in the next octave.
    // Returns -1 when the note would be above 127.
    static int majorScaleNote (int root, int step);

private:
    struct Event
    {
        juce::MidiMessage message;
        int position;
        bool isRootTrigger;
    };

    struct PendingNoteOff
    {
        int channel;
        int note;
        double position; // samples from the start of the current block
    };

    double currentBpm() const;

    double sampleRate = 44100.0;

    std::vector<Event> events;
    std::vector<PendingNoteOff> pendingNoteOffs;

    bool sequenceRunning = false;
    int rootChannel = 1;
    int rootNote = 0;
    juce::uint8 rootVelocity = 100;
    int nextStep = 0;
    double nextStepPosition = 0.0; // samples from the start of the current block

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenerativeMidiProcessor)
};
