#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <vector>

#include "Music.h"
#include "Take.h"

// Generates MIDI notes while the host transport is playing. Incoming MIDI is ignored.
//
// The host timeline is divided into steps of the selected rate (locked to the host's PPQ position, so it follows
// loops and jumps). Each step has a chance of playing one note from the pool of notes of the selected key and scale
// over the selected octave range. Arpeggio mode walks the pool in the chosen direction, Random mode picks from it.
// Odd steps can be swung late, notes get a velocity with optional variation, and they sound for the selected length.
//
// With a cycle length, the step grid restarts every cycle (cycles start at multiples of the cycle length on the host
// timeline) and everything about a step depends on its number within the cycle, so each cycle repeats the last one.
//
// The rhythm is either "every step" (each step has its chance), or a Euclidean pattern: pulses spread evenly over a
// number of steps, repeating freely on the step grid (or from the start of each cycle, when there is one). Only the
// steps that are onsets of the pattern get their chance, and the arpeggio advances by one note per onset.
//
// With Record armed, the notes that leave the plugin are kept from the moment the host starts playing until the chosen
// number of bars are done, or the transport stops, jumps, or Record is disarmed, whichever comes first. A background
// thread then writes the take as a MIDI file in the takes folder (the newest few are kept), and the editor offers it to
// be dragged into the host, or its folder to be opened. Recording only listens to what is played: it never changes it.
//
// No parameter is ever written in reaction to another one. Whatever the host sets, in any combination (an octave range
// that reaches past the top, more pulses than steps), processBlock uses the values it can make sense of.
class SeqVerseProcessor : public juce::AudioProcessor
{
public:
    SeqVerseProcessor();
    ~SeqVerseProcessor() override;

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
    juce::AudioParameterChoice* octaveNoteParam = nullptr;
    juce::AudioParameterChoice* stepRateParam = nullptr;
    juce::AudioParameterChoice* noteLengthParam = nullptr;
    juce::AudioParameterInt* chanceParam = nullptr;            // percent
    juce::AudioParameterInt* swingParam = nullptr;             // percent, 100 is a full triplet swing
    juce::AudioParameterInt* lowOctaveParam = nullptr;
    juce::AudioParameterInt* octavesParam = nullptr;           // how many octaves from the low one, cut off at the top
    juce::AudioParameterInt* velocityParam = nullptr;
    juce::AudioParameterInt* velocityVariationParam = nullptr; // plus or minus
    juce::AudioParameterInt* channelParam = nullptr;           // 1 to 16
    juce::AudioParameterInt* cycleLengthParam = nullptr;       // in half beats, 0 is off
    juce::AudioParameterInt* seedParam = nullptr;              // 0 is a different sequence every time
    juce::AudioParameterInt* lengthVariationParam = nullptr;   // percent: notes are shortened by up to this much
    juce::AudioParameterChoice* rhythmParam = nullptr;
    juce::AudioParameterInt* euclidStepsParam = nullptr;       // Euclidean rhythm: the pattern's length
    juce::AudioParameterInt* euclidPulsesParam = nullptr;      // ... its onsets (all steps play if there are more than steps)
    juce::AudioParameterInt* euclidRotationParam = nullptr;    // ... and how many steps it is moved later
    juce::AudioParameterBool* recordParam = nullptr;           // the next start of the transport records a take
    juce::AudioParameterInt* recordBarsParam = nullptr;        // how many bars of it, 0 is until the transport stops
    juce::AudioParameterBool* midiRootParam = nullptr;         // a MIDI note in latches its pitch class as the root, instead of Key

    // The last take that was written out as a file, for the editor (message thread)
    struct TakeInfo
    {
        juce::File file;
        int notes = 0;
        double beats = 0.0;
        double beatsPerBar = 4.0;
    };

    // Where the takes are written: a folder called SeqVerse in the user's music folder, or the one in the environment
    // variable SEQVERSE_TAKE_FOLDER (the tests use it, so that they leave the music folder alone)
    static juce::File getTakeFolder();

    TakeInfo getLastTake() const;
    bool isRecording() const { return takeState.load() == take::recording; }
    int getTakeCount() const { return takeCount.load(); } // goes up with each take that is written

    // Whether the host's transport was playing in the last block, for the editor, which does not let Record be toggled then
    bool isHostPlaying() const { return hostPlaying.load(); }

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

    // What one block is like, for the recording
    struct BlockInfo
    {
        bool playing = false;
        bool justStarted = false;
        double startQuarters = 0.0;
        double samplesPerQuarter = 1.0;
        int numSamples = 0;
        double bpm = 120.0;
        int tsigNumerator = 4;
        int tsigDenominator = 4;
    };

    class TakeWriter;

    void updateTake (const juce::MidiBuffer& generated, const BlockInfo&);
    void finishTake (double endBeats);
    void writeTake();

    double sampleRate = 44100.0;
    double internalPpq = 0.0; // only used when the host does not report its PPQ position
    bool wasPlaying = false;
    juce::Random random;

    // The last note-on received while MIDI Root is on latches its pitch class as the root (-1 is none yet, so Key is
    // used until the first note). Read from incoming MIDI on the audio thread only, whether or not the host is playing.
    int latchedRootPitchClass = -1;

    int currentKey() const { return midiRootParam->get() && latchedRootPitchClass >= 0 ? latchedRootPitchClass : keyParam->getIndex(); }

    // With a cycle and the seed on Auto, this is the seed of the phrase that repeats. It is rolled again each time the
    // transport starts and when the cycle length changes.
    int autoSeed = 1;
    int lastCycleHalfBeats = -1;

    music::EuclidPattern euclidPattern { 1, 0, 0 }; // rebuilt in each block that uses it, so it needs no allocation

    std::vector<PendingNoteOff> pendingNoteOffs;
    std::vector<NewNote> newNotes;

    // The recording. `recording` and `takeEnd` belong to the audio thread. `current` is written by it while the state is
    // `recording` and read by the writer while it is `finished`, never both.
    take::Take current;
    bool recording = false;
    double takeEnd = 0.0; // beats from the start of the take to the end of the last block
    std::atomic<int> takeState { take::idle };
    std::atomic<int> takeCount { 0 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<bool> unarmRecord { false }; // a take has ended: the writer thread switches Record off (the audio thread must not)
    std::unique_ptr<TakeWriter> takeWriter;

    mutable juce::CriticalSection lastTakeLock; // the writer and the editor only, never the audio thread
    TakeInfo lastTake;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeqVerseProcessor)
};
