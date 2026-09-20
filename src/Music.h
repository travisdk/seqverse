#pragma once

#include <cstdint>

// Musical tables and pure functions, no JUCE. The order of every table is the order of the matching parameter's
// choices, so an index stored in a saved session means the same thing after the tables grow only at the end.
namespace music
{
constexpr int kNumKeys = 12;
constexpr const char* keyNames[kNumKeys] = { "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };

// Octaves are numbered so that C4 is MIDI note 60. The top of the range keeps every scale note below 127.
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

enum ArpDirection { up, down, upDown, downUp, upAndDown, converge, diverge, numArpDirections };
constexpr const char* arpDirectionNames[numArpDirections] = { "Up",
                                                             "Down",
                                                             "Up/Down",
                                                             "Down/Up",
                                                             "Up & Down (repeat ends)",
                                                             "Converge",
                                                             "Diverge" };

enum ArpReset { followTimeline, restartOnPlay, numArpResets };
constexpr const char* arpResetNames[numArpResets] = { "Follow timeline", "Restart on play" };

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

// Index into the pool of `size` notes (0 is the lowest) for arp step number `step`, which counts up forever.
// Every direction is a cycle over the pool, so the pattern is fixed by the step number alone.
constexpr int arpIndex (int direction, int size, std::int64_t step)
{
    const auto cycle = [&] (std::int64_t length) { return (int) (((step % length) + length) % length); };
    const auto pingPong = [&] (int pos) { return pos < size ? pos : 2 * size - 2 - pos; }; // 0..n-1, n-2..1

    switch (direction)
    {
        case down:      return size - 1 - cycle (size);
        case upDown:    return pingPong (cycle (size > 1 ? 2 * size - 2 : 1));
        case downUp:    return size - 1 - pingPong (cycle (size > 1 ? 2 * size - 2 : 1));
        case upAndDown: { const int pos = cycle (2 * size); return pos < size ? pos : 2 * size - 1 - pos; }
        case converge:  { const int pos = cycle (size); return pos % 2 == 0 ? pos / 2 : size - 1 - pos / 2; }
        case diverge:   { const int pos = size - 1 - cycle (size); return pos % 2 == 0 ? pos / 2 : size - 1 - pos / 2; }
        default:        return cycle (size); // up
    }
}
} // namespace music
