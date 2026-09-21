#pragma once

#include <cstdint>

// Musical tables and pure functions, no JUCE. The order of every table is the order of the matching parameter's
// choices, so an index stored in a saved session means the same thing after the tables grow only at the end.
namespace music
{
constexpr int kNumKeys = 12;
constexpr const char* keyNames[kNumKeys] = { "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };

// Octaves are numbered so that C4 is MIDI note 60. The top of the range keeps every note below 127, also the root of
// the octave above it (octave 8 at most, note 119 in B).
constexpr int kMinOctave = 0;
constexpr int kMaxOctave = 7;
constexpr int kDefaultOctave = 4;

constexpr int noteNumber (int key, int octave, int interval) { return 12 * (octave + 1) + key + interval; }

struct Scale
{
    const char* name;
    int size;
    int intervals[12]; // semitones above the root, ascending, all within one octave
};

constexpr Scale scales[] = {
    { "Major (Ionian)", 7, { 0, 2, 4, 5, 7, 9, 11 } },
    { "Natural Minor (Aeolian)", 7, { 0, 2, 3, 5, 7, 8, 10 } },
    { "Harmonic Minor", 7, { 0, 2, 3, 5, 7, 8, 11 } },
    { "Melodic Minor", 7, { 0, 2, 3, 5, 7, 9, 11 } },
    { "Dorian", 7, { 0, 2, 3, 5, 7, 9, 10 } },
    { "Phrygian", 7, { 0, 1, 3, 5, 7, 8, 10 } },
    { "Lydian", 7, { 0, 2, 4, 6, 7, 9, 11 } },
    { "Mixolydian", 7, { 0, 2, 4, 5, 7, 9, 10 } },
    { "Locrian", 7, { 0, 1, 3, 5, 6, 8, 10 } },
    { "Major Pentatonic", 5, { 0, 2, 4, 7, 9 } },
    { "Minor Pentatonic", 5, { 0, 3, 5, 7, 10 } },
    { "Blues (minor)", 6, { 0, 3, 5, 6, 7, 10 } },
    { "Blues (major)", 6, { 0, 2, 3, 4, 7, 9 } },
    { "Whole Tone", 6, { 0, 2, 4, 6, 8, 10 } },
    { "Diminished (whole-half)", 8, { 0, 2, 3, 5, 6, 8, 9, 11 } },
    { "Diminished (half-whole)", 8, { 0, 1, 3, 4, 6, 7, 9, 10 } },
    { "Augmented", 6, { 0, 3, 4, 7, 8, 11 } },
    { "Chromatic", 12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
    { "Harmonic Major", 7, { 0, 2, 4, 5, 7, 8, 11 } },
    { "Phrygian Dominant", 7, { 0, 1, 4, 5, 7, 8, 10 } },
    { "Double Harmonic (Byzantine)", 7, { 0, 1, 4, 5, 7, 8, 11 } },
    { "Hungarian Minor", 7, { 0, 2, 3, 6, 7, 8, 11 } },
    { "Hungarian Major", 7, { 0, 3, 4, 6, 7, 9, 10 } },
    { "Neapolitan Minor", 7, { 0, 1, 3, 5, 7, 8, 11 } },
    { "Neapolitan Major", 7, { 0, 1, 3, 5, 7, 9, 11 } },
    { "Lydian Dominant", 7, { 0, 2, 4, 6, 7, 9, 10 } },
    { "Lydian Augmented", 7, { 0, 2, 4, 6, 8, 9, 11 } },
    { "Locrian Natural 2", 7, { 0, 2, 3, 5, 6, 8, 10 } },
    { "Altered (Super Locrian)", 7, { 0, 1, 3, 4, 6, 8, 10 } },
    { "Mixolydian b6", 7, { 0, 2, 4, 5, 7, 8, 10 } },
    { "Dorian b2", 7, { 0, 1, 3, 5, 7, 9, 10 } },
    { "Ukrainian Dorian", 7, { 0, 2, 3, 6, 7, 9, 10 } },
    { "Persian", 7, { 0, 1, 4, 5, 6, 8, 11 } },
    { "Enigmatic", 7, { 0, 1, 4, 6, 8, 10, 11 } },
    { "Prometheus", 6, { 0, 2, 4, 6, 9, 10 } },
    { "Tritone", 6, { 0, 1, 4, 6, 7, 10 } },
    { "Bebop Dominant", 8, { 0, 2, 4, 5, 7, 9, 10, 11 } },
    { "Bebop Major", 8, { 0, 2, 4, 5, 7, 8, 9, 11 } },
    { "Hirajoshi", 5, { 0, 2, 3, 7, 8 } },
    { "Kumoi", 5, { 0, 2, 3, 7, 9 } },
    { "In Sen", 5, { 0, 1, 5, 7, 10 } },
    { "Iwato", 5, { 0, 1, 5, 6, 10 } },
    { "Egyptian", 5, { 0, 2, 5, 7, 10 } },
};
constexpr int kNumScales = (int) (sizeof (scales) / sizeof (scales[0]));

enum PlayMode { arpeggio, randomNotes, numPlayModes };
constexpr const char* playModeNames[numPlayModes] = { "Arpeggio", "Random" };

// The pool of notes is the scale over the octave range, and, when the octave note is included, the root of the octave
// above the range as its top note: C D E F G A B C over one octave of C major.
enum OctaveNote { includeOctaveNote, excludeOctaveNote, numOctaveNoteChoices };
constexpr const char* octaveNoteNames[numOctaveNoteChoices] = { "Include", "Exclude" };

// What a saved session stores is the index of the choice, so the order here can only grow at the end
enum ArpDirection { up, down, upDown, downUp, upAndDown, converge, diverge,
                    downAndUp, convergeDiverge, thumbUp, thumbDown, pinkyUp, pinkyDown, upTwoBackOne, skip,
                    numArpDirections };
constexpr const char* arpDirectionNames[numArpDirections] = { "Up",
                                                             "Down",
                                                             "Up/Down",
                                                             "Down/Up",
                                                             "Up & Down (repeat ends)",
                                                             "Converge",
                                                             "Diverge",
                                                             "Down & Up (repeat ends)",
                                                             "Converge & Diverge",
                                                             "Thumb Up",
                                                             "Thumb Down",
                                                             "Pinky Up",
                                                             "Pinky Down",
                                                             "Up 2, Back 1",
                                                             "Skip (odd, then even)" };

// The number of directions before the later ones were added. Sessions saved back then stored the choice as a fraction
// of this, see SeqVerseProcessor::setStateInformation.
constexpr int kNumArpDirectionsInFirstVersion = 7;

// The distance between two steps, in quarter notes. Triplets are 2/3 of the straight value.
constexpr int kNumStepRates = 9;
constexpr int kDefaultStepRate = 4; // 1/8
constexpr const char* stepRateNames[kNumStepRates] = { "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1/1" };
constexpr double stepRateQuarters[kNumStepRates] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 2.0, 4.0 };

// The cycle length is counted in half beats: 0 is endless (no cycle), 1 is half a beat, 9 is 4.5 beats, up to 64 beats.
constexpr int kMaxCycleHalfBeats = 128;

constexpr int kNumNoteLengths = 6;
constexpr const char* noteLengthNames[kNumNoteLengths] = { "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" };
constexpr double noteLengthQuarters[kNumNoteLengths] = { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0 };

// A small random number generator (splitmix64) that is cheap to create, so every step can have its own. With a
// fixed seed the numbers of a step depend only on the seed and the step number: the same step plays the same
// thing in every loop, and after every restart.
struct StepRandom
{
    std::uint64_t state;

    static constexpr std::uint64_t golden = 0x9e3779b97f4a7c15;
    static constexpr std::uint64_t stepMultiplier = 0xd1342543de82ef95;
    static constexpr std::uint64_t mixA = 0xbf58476d1ce4e5b9;
    static constexpr std::uint64_t mixB = 0x94d049bb133111eb;

    static constexpr StepRandom forStep (int seed, std::int64_t step)
    {
        return { (std::uint64_t) seed * golden ^ (std::uint64_t) step * stepMultiplier };
    }

    constexpr std::uint64_t next()
    {
        state += golden;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * mixA;
        z = (z ^ (z >> 27)) * mixB;
        return z ^ (z >> 31);
    }

    // 0 .. n-1
    constexpr int nextInt (int n) { return (int) ((next() >> 33) % (std::uint64_t) n); }
};

// A Euclidean rhythm: `pulses` onsets spread as evenly as possible over `steps` steps, made with Bjorklund's
// algorithm, so E(3,8) is x..x..x. and E(5,8) is x.xx.xx. The pattern starts on an onset, and `rotation` moves it
// later by that many steps (wrapping round). It repeats every `steps` steps, and pulses are numbered from the start
// of step 0 upwards, which is what the arpeggio position of the notes follows. The number of steps is not a
// parameter: it is how many steps of the step rate fit in the cycle length (see SeqVerseProcessor::processBlock),
// at most 64 beats of 1/32 steps.
enum Rhythm { everyStep, euclidean, numRhythms };
constexpr const char* rhythmNames[numRhythms] = { "Every step", "Euclidean" };

constexpr int kMaxEuclidSteps = 512;
constexpr int kMaxEuclidPulses = 64;
constexpr int kMaxEuclidRotation = 63;
constexpr int kDefaultEuclidPulses = 5;

struct EuclidPattern
{
    int steps;
    int pulses;
    bool onset[kMaxEuclidSteps] = {};
    int pulsesBefore[kMaxEuclidSteps + 1] = {}; // onsets at the positions below p, for p up to steps

    // Any values are accepted: steps is limited to 1..512, pulses to 0..steps, and the rotation wraps round
    EuclidPattern (int stepsIn, int pulsesIn, int rotation)
        : steps (stepsIn < 1 ? 1 : stepsIn > kMaxEuclidSteps ? kMaxEuclidSteps : stepsIn), pulses (0)
    {
        pulses = pulsesIn < 0 ? 0 : pulsesIn > steps ? steps : pulsesIn;

        bool base[kMaxEuclidSteps] = {};
        bjorklund (base);

        const int shift = ((rotation % steps) + steps) % steps;
        for (int i = 0; i < steps; ++i)
            onset[(i + shift) % steps] = base[i];

        int count = 0;
        for (int i = 0; i < steps; ++i)
        {
            pulsesBefore[i] = count;
            count += onset[i] ? 1 : 0;
        }
        pulsesBefore[steps] = count;
    }

    // Steps are counted from 0 and may be negative (before the start of the timeline)
    int position (std::int64_t step) const { return (int) (((step % steps) + steps) % steps); }
    bool isOnset (std::int64_t step) const { return onset[position (step)]; }

    // How many onsets came before this step, counting from step 0: the number of this onset if it is one
    std::int64_t pulseNumber (std::int64_t step) const
    {
        const int p = position (step);
        return (step - p) / steps * pulses + pulsesBefore[p];
    }

private:
    struct Builder
    {
        const int* counts;
        const int* remainders;
        int* pattern;
        int length;

        void build (int level)
        {
            if (level == -1)
                pattern[length++] = 0;
            else if (level == -2)
                pattern[length++] = 1;
            else
            {
                for (int i = 0; i < counts[level]; ++i)
                    build (level - 1);
                if (remainders[level] != 0)
                    build (level - 2);
            }
        }
    };

    void bjorklund (bool (&out)[kMaxEuclidSteps]) const
    {
        if (pulses == 0)
            return;

        if (pulses == steps)
        {
            for (int i = 0; i < steps; ++i)
                out[i] = true;
            return;
        }

        int counts[kMaxEuclidSteps + 2] = {};
        int remainders[kMaxEuclidSteps + 2] = {};
        int divisor = steps - pulses;
        remainders[0] = pulses;

        int level = 0;
        do
        {
            counts[level] = divisor / remainders[level];
            remainders[level + 1] = divisor % remainders[level];
            divisor = remainders[level];
            ++level;
        } while (remainders[level] > 1);
        counts[level] = divisor;

        int pattern[kMaxEuclidSteps] = {};
        Builder builder { counts, remainders, pattern, 0 };
        builder.build (level);

        int first = 0;
        while (first < steps - 1 && pattern[first] == 0)
            ++first;
        for (int i = 0; i < steps; ++i)
            out[i] = pattern[(i + first) % steps] != 0;
    }
};

// Index into the pool of `size` notes (0 is the lowest) for arp step number `step`, which counts up forever.
// Every direction is a cycle over the pool, so the pattern is fixed by the step number alone.
constexpr int arpIndex (int direction, int size, std::int64_t step)
{
    const auto cycle = [&] (std::int64_t length) { return (int) (((step % length) + length) % length); };
    const auto pingPong = [&] (int pos) { return pos < size ? pos : 2 * size - 2 - pos; }; // 0..n-1, n-2..1
    const auto outsideIn = [&] (int pos) { return pos % 2 == 0 ? pos / 2 : size - 1 - pos / 2; }; // 0, n-1, 1, n-2 ...

    // Patterns that come back to one note between all the others go through the other n - 1 notes: 2 * (n - 1) steps
    const int anchored = size > 1 ? 2 * size - 2 : 1;

    switch (direction)
    {
        case down:            return size - 1 - cycle (size);
        case upDown:          return pingPong (cycle (anchored));
        case downUp:          return size - 1 - pingPong (cycle (anchored));
        case upAndDown:       { const int pos = cycle (2 * size); return pos < size ? pos : 2 * size - 1 - pos; }
        case converge:        return outsideIn (cycle (size));
        case diverge:         return outsideIn (size - 1 - cycle (size));
        case downAndUp:       { const int pos = cycle (2 * size); return pos < size ? size - 1 - pos : pos - size; }
        case convergeDiverge: { const int pos = cycle (2 * size); return outsideIn (pos < size ? pos : 2 * size - 1 - pos); }
        case thumbUp:         { const int pos = cycle (anchored); return pos % 2 == 0 ? 0 : (pos + 1) / 2; }            // 0 1 0 2 0 3 ...
        case thumbDown:       { const int pos = cycle (anchored); return pos % 2 == 0 ? 0 : size - 1 - (pos - 1) / 2; } // 0 n-1 0 n-2 ...
        case pinkyUp:         { const int pos = cycle (anchored); return pos % 2 == 0 ? size - 1 : (pos - 1) / 2; }     // n-1 0 n-1 1 ...
        case pinkyDown:       { const int pos = cycle (anchored); return pos % 2 == 0 ? size - 1 : size - 2 - (pos - 1) / 2; }
        case upTwoBackOne:    // 0 2 1 3 2 4 ... up to the top and the note below it, then over again
        {
            if (size < 3)
                return cycle (size);
            const int pos = cycle (2 * size - 3);
            return pos % 2 == 0 ? pos / 2 : (pos - 1) / 2 + 2;
        }
        case skip:            // 0 2 4 6 1 3 5: every other note, then the ones in between
        {
            const int evens = (size + 1) / 2;
            const int pos = cycle (size);
            return pos < evens ? 2 * pos : 2 * (pos - evens) + 1;
        }
        default:              return cycle (size); // up
    }
}
} // namespace music
