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

juce::String cycleLengthText (int halfBeats)
{
    if (halfBeats <= 0)
        return "Off";

    return juce::String (halfBeats * 0.5, halfBeats % 2 != 0 ? 1 : 0) + (halfBeats == 2 ? " beat" : " beats");
}

int cycleLengthFromText (const juce::String& text)
{
    const auto trimmed = text.trim();
    if (trimmed.isEmpty() || trimmed.equalsIgnoreCase ("off") || trimmed.equalsIgnoreCase ("endless"))
        return 0;

    return juce::jlimit (0, music::kMaxCycleHalfBeats, juce::roundToInt (trimmed.getDoubleValue() * 2.0));
}

const char* const stateTag = "SeqVerse";
constexpr int stateVersion = 2; // 1: parameters as fractions of their range, 2: as their values
}

SeqVerseProcessor::SeqVerseProcessor()
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
    addParameter (octaveNoteParam = new Choice ({ "octaveNote", 1 }, "Octave Note", namesOf (music::octaveNoteNames), music::includeOctaveNote));
    addParameter (stepRateParam = new Choice ({ "stepRate", 1 }, "Step Rate", namesOf (music::stepRateNames), music::kDefaultStepRate));
    addParameter (noteLengthParam = new Choice ({ "noteLength", 1 }, "Note Length", namesOf (music::noteLengthNames), 1));
    addParameter (chanceParam = new Int ({ "chance", 1 }, "Chance", 0, 100, 100, percent));
    addParameter (swingParam = new Int ({ "swing", 1 }, "Swing", 0, 100, 0, percent));
    addParameter (lowOctaveParam = new Int ({ "lowOctave", 1 }, "Low Octave", music::kMinOctave, music::kMaxOctave, music::kDefaultOctave));
    addParameter (octavesParam = new Int ({ "octaves", 1 }, "Octaves", 1, music::kMaxOctaves, music::kDefaultOctaves));
    addParameter (velocityParam = new Int ({ "velocity", 1 }, "Velocity", 1, 127, 100));
    addParameter (velocityVariationParam = new Int ({ "velocityVariation", 1 }, "Velocity Variation", 0, 64, 0));
    addParameter (channelParam = new Int ({ "channel", 1 }, "Channel", 1, 16, 1));
    addParameter (cycleLengthParam = new Int ({ "cycleLength", 1 }, "Cycle Length", 0, music::kMaxCycleHalfBeats, 0,
                                              juce::AudioParameterIntAttributes()
                                                  .withStringFromValueFunction ([] (int halfBeats, int) { return cycleLengthText (halfBeats); })
                                                  .withValueFromStringFunction (cycleLengthFromText)));
    addParameter (seedParam = new Int ({ "seed", 1 }, "Seed", 0, 9999, 0,
                                       juce::AudioParameterIntAttributes()
                                           .withStringFromValueFunction ([] (int value, int) { return value == 0 ? juce::String ("Auto") : juce::String (value); })
                                           .withValueFromStringFunction ([] (const juce::String& text)
                                                                         { return text.trim().equalsIgnoreCase ("auto") ? 0 : text.getIntValue(); })));

    // Added last, so that the parameters of sessions saved earlier keep their place
    addParameter (lengthVariationParam = new Int ({ "lengthVariation", 1 }, "Length Variation", 0, 100, 0, percent));
    addParameter (rhythmParam = new Choice ({ "rhythm", 1 }, "Rhythm", namesOf (music::rhythmNames), music::everyStep));
    addParameter (euclidStepsParam = new Int ({ "euclidSteps", 1 }, "Pattern Length", music::kMinEuclidSteps, music::kMaxEuclidSteps, music::kDefaultEuclidSteps));
    addParameter (euclidPulsesParam = new Int ({ "euclidPulses", 1 }, "Pulses", 0, music::kMaxEuclidPulses, music::kDefaultEuclidPulses));
    addParameter (euclidRotationParam = new Int ({ "euclidRotation", 1 }, "Rotation", 0, music::kMaxEuclidRotation, 0));
}

SeqVerseProcessor::~SeqVerseProcessor() = default;

void SeqVerseProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    internalPpq = 0.0;
    wasPlaying = false;
    pendingNoteOffs.clear();
    pendingNoteOffs.reserve (256);
    newNotes.reserve (256);
}

bool SeqVerseProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SeqVerseProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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

    const bool justStarted = ! wasPlaying;
    wasPlaying = true;

    // Settings for this block
    const auto& scale = music::scales[scaleParam->getIndex()];
    const int key = keyParam->getIndex();
    const bool arpeggiate = playModeParam->getIndex() == music::arpeggio;
    const int direction = arpDirectionParam->getIndex();
    const double stepQuarters = music::stepRateQuarters[stepRateParam->getIndex()];
    const double swingDelayQuarters = swingParam->get() / 100.0 * stepQuarters / 3.0; // 100 %: the off-beat of a triplet
    const double noteLengthSamples = music::noteLengthQuarters[noteLengthParam->getIndex()] * samplesPerQuarter;
    const int chancePercent = chanceParam->get();
    const int channel = channelParam->get();
    const int velocity = velocityParam->get();
    const int velocityVariation = velocityVariationParam->get();
    const double lengthVariation = lengthVariationParam->get() / 100.0;
    const int seed = seedParam->get();

    const int cycleHalfBeats = cycleLengthParam->get(); // 0: off
    const double cycleQuarters = cycleHalfBeats * 0.5;

    // With a cycle and the seed on Auto, every new play (and every new cycle length) rolls a new phrase, which then
    // repeats. A transport that loops or jumps while playing keeps its phrase.
    if (justStarted || cycleHalfBeats != lastCycleHalfBeats)
        autoSeed = 1 + random.nextInt (0x7ffffffe);
    lastCycleHalfBeats = cycleHalfBeats;
    const int cycleSeed = seed > 0 ? seed : autoSeed;

    // The pattern repeats every `steps` steps: on the step grid of the timeline, or from the start of each cycle.
    // EuclidPattern takes any values: pulses above the steps play every step, and the rotation wraps round.
    const bool euclidean = rhythmParam->getIndex() == music::euclidean;
    if (euclidean)
        euclidPattern = music::EuclidPattern (euclidStepsParam->get(), euclidPulsesParam->get(), euclidRotationParam->get());

    // The range is `octaves` octaves from the low one, and stops at the top octave
    const int lowOctave = lowOctaveParam->get();
    const int highOctave = juce::jmin (music::kMaxOctave, lowOctave + octavesParam->get() - 1);
    const int scaleNotes = (highOctave - lowOctave + 1) * scale.size;
    const int poolSize = scaleNotes + (octaveNoteParam->getIndex() == music::includeOctaveNote ? 1 : 0); // the last is the octave note

    const auto firstStepAtOrAfter = [stepQuarters] (double quarters)
    { return (juce::int64) std::ceil (quarters / stepQuarters - kBoundaryTolerance); };

    const double toleranceQuarters = kBoundaryTolerance * stepQuarters;

    newNotes.clear();

    // Plays step number `step` (its number in its cycle, or on the timeline when there is no cycle) that starts at
    // `stepStart` (in quarters). `randomState` seeds its random numbers.
    const auto playStep = [&] (double stepStart, juce::int64 step, std::uint64_t randomState)
    {
        // In a Euclidean rhythm only the onsets play, and the arpeggio counts them instead of the steps
        auto arpStep = step;
        if (euclidean)
        {
            if (! euclidPattern.isOnset (step))
                return;

            arpStep = euclidPattern.pulseNumber (step);
        }

        // All the randomness of a step comes from one generator, so it does not depend on the other settings
        auto stepRandom = music::StepRandom { randomState };
        const int chanceDraw = stepRandom.nextInt (100);
        const int noteDraw = stepRandom.nextInt (poolSize);
        const int velocityDraw = stepRandom.nextInt (2 * velocityVariation + 1);
        const int lengthDraw = stepRandom.nextInt (101); // always drawn, last, so that the ones before do not depend on it

        if (chanceDraw >= chancePercent)
            return;

        const int poolIndex = arpeggiate ? music::arpIndex (direction, poolSize, arpStep) : noteDraw;
        const int note = poolIndex < scaleNotes ? music::noteNumber (key, lowOctave + poolIndex / scale.size, scale.intervals[poolIndex % scale.size])
                                                : music::noteNumber (key, highOctave + 1, 0); // the root above the range

        const double onPosition = juce::jlimit (0.0, (double) (numSamples - 1), (stepStart - startQuarters) * samplesPerQuarter);

        // A note that is still sounding is ended at the moment the same note is played again
        for (auto& pending : pendingNoteOffs)
            if (pending.channel == channel && pending.note == note && pending.position > onPosition)
                pending.position = onPosition;

        newNotes.push_back ({ channel, note, juce::jlimit (1, 127, velocity + velocityDraw - velocityVariation), (int) onPosition });
        // The variation only shortens: between (100 - variation) % and the whole of the note length, but never below a
        // tenth of it (and never to nothing, or a note-off would come before its own note-on).
        const double lengthFactor = juce::jmax (0.1, 1.0 - lengthVariation * lengthDraw / 100.0);
        const double length = juce::jmax (2.0, noteLengthSamples * lengthFactor);
        pendingNoteOffs.push_back ({ channel, note, onPosition + length });
    };

    // A block owns the steps that start in it, so consecutive blocks never repeat or skip a step
    const auto startsInThisBlock = [&] (double stepStart)
    { return stepStart >= startQuarters - toleranceQuarters && stepStart < endQuarters - toleranceQuarters; };

    if (cycleHalfBeats == 0)
    {
        // No cycle. Step k is at k * stepQuarters, and odd steps are swung later, so a swung step starts up to one
        // delay after its grid position. A fixed seed makes the random numbers depend on the seed and k only.
        for (auto step = firstStepAtOrAfter (startQuarters - swingDelayQuarters); step < firstStepAtOrAfter (endQuarters); ++step)
        {
            const double stepStart = (double) step * stepQuarters + (step % 2 != 0 ? swingDelayQuarters : 0.0);
            if (startsInThisBlock (stepStart))
                playStep (stepStart, step,
                          seed > 0 ? music::StepRandom::forStep (seed, step).state : (std::uint64_t) random.nextInt64());
        }
    }
    else
    {
        // Cycles start at multiples of the cycle length on the host timeline, and the step grid restarts in each of
        // them. Everything about a step (its note, chance, velocity and swing) depends on its number in its cycle
        // and on the cycle seed only, so every cycle plays what the first one did. The steps are counted from the
        // start of the cycle, so a cycle that is not a whole number of steps just ends with a shorter last step.
        const auto stepsInCycle = (juce::int64) std::ceil (cycleQuarters / stepQuarters - kBoundaryTolerance);
        const auto firstCycle = (juce::int64) std::floor ((startQuarters - swingDelayQuarters - toleranceQuarters) / cycleQuarters);
        const auto lastCycle = (juce::int64) std::floor (endQuarters / cycleQuarters);

        for (auto cycle = firstCycle; cycle <= lastCycle; ++cycle)
        {
            const double cycleStart = (double) cycle * cycleQuarters;
            const auto firstStep = juce::jmax ((juce::int64) 0, firstStepAtOrAfter (startQuarters - swingDelayQuarters - cycleStart));
            const auto endStep = juce::jmin (stepsInCycle, firstStepAtOrAfter (endQuarters - cycleStart));

            for (auto step = firstStep; step < endStep; ++step)
            {
                const double stepStart = cycleStart + (double) step * stepQuarters + (step % 2 != 0 ? swingDelayQuarters : 0.0);
                if (startsInThisBlock (stepStart))
                    playStep (stepStart, step, music::StepRandom::forStep (cycleSeed, step).state);
            }
        }
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

juce::AudioProcessorEditor* SeqVerseProcessor::createEditor()
{
    return new SeqVerseEditor (*this);
}

void SeqVerseProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml (stateTag);
    xml.setAttribute ("version", stateVersion);

    // The value itself is stored, the index of a choice or the number of an integer parameter. A fraction of the range
    // would change its meaning whenever a list gets another entry.
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            xml.setAttribute (ranged->getParameterID(), (double) ranged->convertFrom0to1 (ranged->getValue()));

    copyXmlToBinary (xml, destData);
}

void SeqVerseProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (stateTag))
        return;

    // Sessions saved before there was a version (1) stored every parameter as a fraction of its range
    const int version = xml->getIntAttribute ("version", 1);

    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (xml->hasAttribute (ranged->getParameterID()))
            {
                const double stored = xml->getDoubleAttribute (ranged->getParameterID());

                float normalised = (float) stored;
                if (version >= 2)
                    normalised = ranged->convertTo0to1 ((float) stored);
                else if (ranged == arpDirectionParam) // the list had 7 entries then, and more have been added since
                    normalised = ranged->convertTo0to1 ((float) juce::roundToInt (stored * (music::kNumArpDirectionsInFirstVersion - 1)));

                ranged->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
            }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SeqVerseProcessor();
}
