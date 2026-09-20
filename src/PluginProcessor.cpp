#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr double kFallbackBpm = 120.0;
constexpr double kGate = 0.9; // fraction of an 8th that a generated note sounds
constexpr double kEndTolerance = 1.0; // samples: an 8th starting this close to (or after) the note-off is not played
}

GenerativeMidiProcessor::GenerativeMidiProcessor()
    // A silent stereo output. Some hosts (Bitwig) do not run a plugin that has no audio ports.
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void GenerativeMidiProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    sequenceRunning = false;
    pendingNoteOffs.clear();
    pendingNoteOffs.reserve (64);
    events.reserve (256);
}

bool GenerativeMidiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

int GenerativeMidiProcessor::majorScaleNote (int root, int step)
{
    static constexpr int semitones[7] = { 0, 2, 4, 5, 7, 9, 11 };
    const int note = root + 12 * (step / 7) + semitones[step % 7];
    return note > 127 ? -1 : note;
}

double GenerativeMidiProcessor::currentBpm() const
{
    if (auto* head = getPlayHead())
        if (auto position = head->getPosition())
            if (auto bpm = position->getBpm())
                if (*bpm > 0.0)
                    return *bpm;

    return kFallbackBpm;
}

void GenerativeMidiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();

    const int numSamples = buffer.getNumSamples();
    const double eighthLength = sampleRate * 60.0 / currentBpm() / 2.0;

    // Incoming notes are only triggers, so they are taken out of the buffer. Everything else passes through.
    events.clear();
    for (const auto metadata : midi)
        events.push_back ({ metadata.getMessage(), metadata.samplePosition, false });
    midi.clear();

    // The lowest of the notes that start on the same sample is the root of that chord.
    for (size_t i = 0; i < events.size(); ++i)
    {
        if (! events[i].message.isNoteOn())
            continue;

        bool lowest = true;
        for (size_t j = 0; j < events.size() && lowest; ++j)
        {
            if (j == i || ! events[j].message.isNoteOn() || events[j].position != events[i].position)
                continue;

            const int other = events[j].message.getNoteNumber();
            const int mine = events[i].message.getNoteNumber();
            lowest = mine < other || (mine == other && i < j);
        }
        events[i].isRootTrigger = lowest;
    }

    const auto playStepsBefore = [&] (double limit)
    {
        while (sequenceRunning && nextStepPosition < limit)
        {
            int note = majorScaleNote (rootNote, nextStep);
            if (note < 0) // ran out of keys, start over from the root
            {
                nextStep = 0;
                note = rootNote;
            }

            const int position = juce::jlimit (0, juce::jmax (0, numSamples - 1), (int) nextStepPosition);
            midi.addEvent (juce::MidiMessage::noteOn (rootChannel, note, rootVelocity), position);
            pendingNoteOffs.push_back ({ rootChannel, note, nextStepPosition + kGate * eighthLength });

            nextStepPosition += eighthLength;
            ++nextStep;
        }
    };

    for (const auto& event : events)
    {
        // 8ths that start before this event have to be played before it changes the sequence
        playStepsBefore ((double) event.position - kEndTolerance);

        const auto& message = event.message;

        if (message.isNoteOn())
        {
            if (event.isRootTrigger)
            {
                sequenceRunning = true;
                rootChannel = message.getChannel();
                rootNote = message.getNoteNumber();
                rootVelocity = message.getVelocity();
                nextStep = 0;
                nextStepPosition = event.position;
            }
        }
        else if (message.isNoteOff())
        {
            if (sequenceRunning && message.getChannel() == rootChannel && message.getNoteNumber() == rootNote)
                sequenceRunning = false;
        }
        else
        {
            if (message.isAllNotesOff() || message.isAllSoundOff())
                sequenceRunning = false;

            midi.addEvent (message, event.position);
        }
    }

    playStepsBefore ((double) numSamples);

    if (sequenceRunning)
        nextStepPosition -= numSamples;

    for (auto it = pendingNoteOffs.begin(); it != pendingNoteOffs.end();)
    {
        if (it->position < numSamples)
        {
            midi.addEvent (juce::MidiMessage::noteOff (it->channel, it->note),
                           juce::jmax (0, (int) it->position));
            it = pendingNoteOffs.erase (it);
        }
        else
        {
            it->position -= numSamples;
            ++it;
        }
    }
}

juce::AudioProcessorEditor* GenerativeMidiProcessor::createEditor()
{
    return new GenerativeMidiEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GenerativeMidiProcessor();
}
