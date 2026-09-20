#include "PluginProcessor.h"
#include "Music.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
constexpr double kFallbackBpm = 120.0;
constexpr double kStepQuarters = 0.5; // the step grid is fixed at 1/8th for now
constexpr int kChannel = 1;
constexpr juce::uint8 kVelocity = 100;
constexpr double kBoundaryTolerance = 1.0e-6; // in steps: keeps a step on a block boundary in exactly one block

template <typename Names>
juce::StringArray namesOf (const Names& names)
{
    juce::StringArray result;
    for (const char* name : names)
        result.add (name);
    return result;
}

juce::StringArray scaleNames()
{
    juce::StringArray result;
    for (const auto& scale : music::scales)
        result.add (scale.name);
    return result;
}

const char* const stateTag = "GenerativeMidi";
}

GenerativeMidiProcessor::GenerativeMidiProcessor()
    // A silent stereo output. Some hosts (Bitwig) do not run a plugin that has no audio ports.
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // Defaults: C major, arpeggio going up, every step plays, 1/16th notes.
    addParameter (keyParam = new juce::AudioParameterChoice ({ "key", 1 }, "Key", namesOf (music::keyNames), 0));
    addParameter (scaleParam = new juce::AudioParameterChoice ({ "scale", 1 }, "Scale", scaleNames(), 0));
    addParameter (playModeParam = new juce::AudioParameterChoice ({ "playMode", 1 }, "Play Mode",
                                                                  namesOf (music::playModeNames), music::arpeggio));
    addParameter (arpDirectionParam = new juce::AudioParameterChoice ({ "arpDirection", 1 }, "Arp Direction",
                                                                      namesOf (music::arpDirectionNames), music::up));
    addParameter (chanceParam = new juce::AudioParameterInt ({ "chance", 1 }, "Chance", 0, 100, 100));
    addParameter (noteLengthParam = new juce::AudioParameterChoice ({ "noteLength", 1 }, "Note Length",
                                                                    namesOf (music::noteLengthNames), 1));
}

void GenerativeMidiProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    internalPpq = 0.0;
    pendingNoteOffs.clear();
    pendingNoteOffs.reserve (256);
    newNotes.reserve (256);
}

bool GenerativeMidiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void GenerativeMidiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    midi.clear(); // incoming MIDI is not used, only generated notes leave the plugin

    const int numSamples = buffer.getNumSamples();

    auto* transport = getPlayHead();
    const auto position = transport != nullptr ? transport->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>();

    if (! position.hasValue() || ! position->getIsPlaying())
    {
        // The transport is stopped: end whatever still sounds, and generate nothing.
        for (const auto& pending : pendingNoteOffs)
            midi.addEvent (juce::MidiMessage::noteOff (pending.channel, pending.note), 0);

        pendingNoteOffs.clear();
        internalPpq = 0.0;
        return;
    }

    double bpm = position->getBpm().orFallback (kFallbackBpm);
    if (bpm <= 0.0)
        bpm = kFallbackBpm;

    const double samplesPerQuarter = sampleRate * 60.0 / bpm;
    const double startQuarters = position->getPpqPosition().orFallback (internalPpq);
    const double endQuarters = startQuarters + numSamples / samplesPerQuarter;
    internalPpq = endQuarters;

    const auto& scale = music::scales[scaleParam->getIndex()];
    const int rootNote = music::kRootNote + keyParam->getIndex();
    const bool arpeggiate = playModeParam->getIndex() == music::arpeggio;
    const int direction = arpDirectionParam->getIndex();
    const int chancePercent = chanceParam->get();
    const double noteLengthSamples = music::noteLengthQuarters[noteLengthParam->getIndex()] * samplesPerQuarter;

    // Step k starts at k * kStepQuarters. A block owns the steps from the first one at or after its start up to,
    // not including, the first one at or after its end, so consecutive blocks never repeat or skip a step.
    const auto firstStepAtOrAfter = [] (double quarters)
    { return (juce::int64) std::ceil (quarters / kStepQuarters - kBoundaryTolerance); };

    newNotes.clear();

    for (auto step = firstStepAtOrAfter (startQuarters); step < firstStepAtOrAfter (endQuarters); ++step)
    {
        if (random.nextInt (100) >= chancePercent)
            continue;

        const double offset = ((double) step * kStepQuarters - startQuarters) * samplesPerQuarter;
        const double onPosition = juce::jlimit (0.0, (double) (numSamples - 1), offset);

        const int interval = arpeggiate ? scale.intervals[music::arpIndex (direction, scale.size, step)]
                                        : scale.intervals[random.nextInt (scale.size)];
        const int note = rootNote + interval;

        // A note that is still sounding is ended at the moment the same note is played again
        for (auto& pending : pendingNoteOffs)
            if (pending.channel == kChannel && pending.note == note && pending.position > onPosition)
                pending.position = onPosition;

        newNotes.push_back ({ note, (int) onPosition });
        pendingNoteOffs.push_back ({ kChannel, note, onPosition + noteLengthSamples });
    }

    // Note-offs go into the buffer before the note-ons: at the same sample a MidiBuffer keeps insertion order,
    // and a note that ends where the same note starts again must end first.
    for (auto it = pendingNoteOffs.begin(); it != pendingNoteOffs.end();)
    {
        if (it->position < numSamples)
        {
            midi.addEvent (juce::MidiMessage::noteOff (it->channel, it->note), (int) it->position);
            it = pendingNoteOffs.erase (it);
        }
        else
        {
            it->position -= numSamples;
            ++it;
        }
    }

    for (const auto& newNote : newNotes)
        midi.addEvent (juce::MidiMessage::noteOn (kChannel, newNote.note, kVelocity), newNote.position);
}

juce::AudioProcessorEditor* GenerativeMidiProcessor::createEditor()
{
    return new GenerativeMidiEditor (*this);
}

void GenerativeMidiProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml (stateTag);

    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            xml.setAttribute (ranged->getParameterID(), (double) ranged->getValue());

    copyXmlToBinary (xml, destData);
}

void GenerativeMidiProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (stateTag))
        return;

    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (xml->hasAttribute (ranged->getParameterID()))
                ranged->setValueNotifyingHost ((float) xml->getDoubleAttribute (ranged->getParameterID()));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GenerativeMidiProcessor();
}
