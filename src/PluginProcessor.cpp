#include "PluginProcessor.h"
#include "Music.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
constexpr double kFallbackBpm = 120.0;
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
    using Choice = juce::AudioParameterChoice;
    using Int = juce::AudioParameterInt;
    const auto percent = juce::AudioParameterIntAttributes().withLabel ("%");

    // Defaults: C major, arpeggio going up, every 1/8th step plays a 1/16th note in octave 4, velocity 100, channel 1.
    addParameter (keyParam = new Choice ({ "key", 1 }, "Key", namesOf (music::keyNames), 0));
    addParameter (scaleParam = new Choice ({ "scale", 1 }, "Scale", scaleNames(), 0));
    addParameter (playModeParam = new Choice ({ "playMode", 1 }, "Play Mode", namesOf (music::playModeNames), music::arpeggio));
    addParameter (arpDirectionParam = new Choice ({ "arpDirection", 1 }, "Arp Direction", namesOf (music::arpDirectionNames), music::up));
    addParameter (arpResetParam = new Choice ({ "arpReset", 1 }, "Arp Reset", namesOf (music::arpResetNames), music::followTimeline));
    addParameter (stepRateParam = new Choice ({ "stepRate", 1 }, "Step Rate", namesOf (music::stepRateNames), music::kDefaultStepRate));
    addParameter (noteLengthParam = new Choice ({ "noteLength", 1 }, "Note Length", namesOf (music::noteLengthNames), 1));
    addParameter (chanceParam = new Int ({ "chance", 1 }, "Chance", 0, 100, 100, percent));
    addParameter (swingParam = new Int ({ "swing", 1 }, "Swing", 0, 100, 0, percent));
    addParameter (lowOctaveParam = new Int ({ "lowOctave", 1 }, "Low Octave", music::kMinOctave, music::kMaxOctave, music::kDefaultOctave));
    addParameter (highOctaveParam = new Int ({ "highOctave", 1 }, "High Octave", music::kMinOctave, music::kMaxOctave, music::kDefaultOctave));
    addParameter (velocityParam = new Int ({ "velocity", 1 }, "Velocity", 1, 127, 100));
    addParameter (velocityVariationParam = new Int ({ "velocityVariation", 1 }, "Velocity Variation", 0, 64, 0));
    addParameter (channelParam = new Int ({ "channel", 1 }, "Channel", 1, 16, 1));
    addParameter (seedParam = new Int ({ "seed", 1 }, "Seed", 0, 9999, 0,
                                       juce::AudioParameterIntAttributes()
                                           .withStringFromValueFunction ([] (int value, int) { return value == 0 ? juce::String ("Auto") : juce::String (value); })
                                           .withValueFromStringFunction ([] (const juce::String& text)
                                                                         { return text.trim().equalsIgnoreCase ("auto") ? 0 : text.getIntValue(); })));

    lowOctaveParam->addListener (this);
    highOctaveParam->addListener (this);
}

GenerativeMidiProcessor::~GenerativeMidiProcessor()
{
    lowOctaveParam->removeListener (this);
    highOctaveParam->removeListener (this);
    cancelPendingUpdate();
}

void GenerativeMidiProcessor::parameterValueChanged (int parameterIndex, float)
{
    // May be called from any thread, including the audio thread when the host automates the parameter.
    if (parameterIndex == lowOctaveParam->getParameterIndex())
        lastChangedOctave = 1;
    else if (parameterIndex == highOctaveParam->getParameterIndex())
        lastChangedOctave = 2;
    else
        return;

    triggerAsyncUpdate();
}

void GenerativeMidiProcessor::handleAsyncUpdate()
{
    const int low = lowOctaveParam->get();
    const int high = highOctaveParam->get();
    if (low <= high)
        return;

    if (lastChangedOctave == 1)
        *highOctaveParam = low;
    else
        *lowOctaveParam = high;
}

void GenerativeMidiProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    internalPpq = 0.0;
    wasPlaying = false;
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
        wasPlaying = false;
        return;
    }

    double bpm = position->getBpm().orFallback (kFallbackBpm);
    if (bpm <= 0.0)
        bpm = kFallbackBpm;

    const double samplesPerQuarter = sampleRate * 60.0 / bpm;
    const double startQuarters = position->getPpqPosition().orFallback (internalPpq);
    const double endQuarters = startQuarters + numSamples / samplesPerQuarter;
    internalPpq = endQuarters;

    if (! wasPlaying)
        playStartQuarters = startQuarters;
    wasPlaying = true;

    // Settings for this block
    const auto& scale = music::scales[scaleParam->getIndex()];
    const int key = keyParam->getIndex();
    const bool arpeggiate = playModeParam->getIndex() == music::arpeggio;
    const int direction = arpDirectionParam->getIndex();
    const bool restartArp = arpResetParam->getIndex() == music::restartOnPlay;
    const double stepQuarters = music::stepRateQuarters[stepRateParam->getIndex()];
    const double swingDelayQuarters = swingParam->get() / 100.0 * stepQuarters / 3.0; // 100 %: the off-beat of a triplet
    const double noteLengthSamples = music::noteLengthQuarters[noteLengthParam->getIndex()] * samplesPerQuarter;
    const int chancePercent = chanceParam->get();
    const int channel = channelParam->get();
    const int velocity = velocityParam->get();
    const int velocityVariation = velocityVariationParam->get();
    const int seed = seedParam->get();

    // Whatever the two octave parameters say, the range is low..high
    const int lowOctave = juce::jmin (lowOctaveParam->get(), highOctaveParam->get());
    const int highOctave = juce::jmax (lowOctaveParam->get(), highOctaveParam->get());
    const int poolSize = (highOctave - lowOctave + 1) * scale.size;

    const auto firstStepAtOrAfter = [stepQuarters] (double quarters)
    { return (juce::int64) std::ceil (quarters / stepQuarters - kBoundaryTolerance); };

    const auto restartStep = firstStepAtOrAfter (playStartQuarters);
    const double toleranceQuarters = kBoundaryTolerance * stepQuarters;

    newNotes.clear();

    // Step k is at k * stepQuarters, and odd steps are swung later. A block owns the steps that start in it, so
    // consecutive blocks never repeat or skip a step. A swung step starts up to one delay after its grid position.
    for (auto step = firstStepAtOrAfter (startQuarters - swingDelayQuarters); step < firstStepAtOrAfter (endQuarters); ++step)
    {
        const double stepQuartersStart = (double) step * stepQuarters + (step % 2 != 0 ? swingDelayQuarters : 0.0);
        if (stepQuartersStart < startQuarters - toleranceQuarters || stepQuartersStart >= endQuarters - toleranceQuarters)
            continue;

        // All the randomness of a step comes from one generator, so it does not depend on the other settings.
        // A fixed seed makes it depend on the seed and the step number only.
        auto stepRandom = music::StepRandom { seed > 0 ? music::StepRandom::forStep (seed, step).state
                                                       : (std::uint64_t) random.nextInt64() };
        const int chanceDraw = stepRandom.nextInt (100);
        const int noteDraw = stepRandom.nextInt (poolSize);
        const int velocityDraw = stepRandom.nextInt (2 * velocityVariation + 1);

        if (chanceDraw >= chancePercent)
            continue;

        const int poolIndex = arpeggiate ? music::arpIndex (direction, poolSize, restartArp ? step - restartStep : step)
                                         : noteDraw;
        const int note = music::noteNumber (key, lowOctave + poolIndex / scale.size, scale.intervals[poolIndex % scale.size]);

        const double onPosition = juce::jlimit (0.0, (double) (numSamples - 1), (stepQuartersStart - startQuarters) * samplesPerQuarter);

        // A note that is still sounding is ended at the moment the same note is played again
        for (auto& pending : pendingNoteOffs)
            if (pending.channel == channel && pending.note == note && pending.position > onPosition)
                pending.position = onPosition;

        newNotes.push_back ({ channel, note, juce::jlimit (1, 127, velocity + velocityDraw - velocityVariation), (int) onPosition });
        pendingNoteOffs.push_back ({ channel, note, onPosition + noteLengthSamples });
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
        midi.addEvent (juce::MidiMessage::noteOn (newNote.channel, newNote.note, (juce::uint8) newNote.velocity), newNote.position);
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
