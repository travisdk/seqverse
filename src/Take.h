#pragma once

#include <cstdint>
#include <vector>

// A take: the notes the plugin played while Record was armed, kept as they left the plugin so that they can be written
// out as a MIDI file. No JUCE dependency. The audio thread fills it in (nothing is allocated there: the space is made
// in the constructor), and a background thread writes the file once the take is finished.
namespace take
{
constexpr int kMaxBars = 10;             // "Record Bars" is 0 (until the transport stops) to this
constexpr int kCapacity = 32768;         // events: enough for ten bars of 32nd notes at any time signature the hosts have
constexpr int kTicksPerQuarter = 960;
constexpr int kTakesKept = 5;            // the newest files in the takes folder; older ones of the same kind are deleted

// Nothing is being written while `finished` is set: the audio thread starts no new take and the writer owns the data
enum State { idle, recording, finished };

struct Event
{
    double beats;         // from the start of the take
    std::uint8_t status;  // note-on or note-off, with the channel
    std::uint8_t note;
    std::uint8_t velocity;
};

inline double beatsPerBar (int numerator, int denominator)
{
    return numerator * 4.0 / (denominator > 0 ? denominator : 4);
}

// The settings the take was played with, for its name (indices into the tables of Music.h)
struct Description
{
    int key = 0;
    int scale = 0;
    int playMode = 0;
    int arpDirection = 0;
    int stepRate = 0;
    int rhythm = 0;
    int pulses = 0;
    int patternLength = 0;
};

struct Take
{
    Take() : events ((size_t) kCapacity) {}

    bool add (double beats, std::uint8_t status, std::uint8_t note, std::uint8_t velocity)
    {
        if (count >= kCapacity)
            return false;

        events[(size_t) count++] = { beats, status, note, velocity };
        if ((status & 0xf0) == 0x90)
            ++noteOns;
        return true;
    }

    void begin (double startPpq, double tempo, int numerator, int denominator, double lengthBeats)
    {
        count = 0;
        noteOns = 0;
        startQuarters = startPpq;
        bpm = tempo;
        tsigNumerator = numerator;
        tsigDenominator = denominator;
        limitBeats = lengthBeats;
        endBeats = 0.0;
    }

    std::vector<Event> events; // `count` of them are in use
    int count = 0;
    int noteOns = 0;

    double startQuarters = 0.0; // on the host's timeline
    double bpm = 120.0;
    int tsigNumerator = 4;
    int tsigDenominator = 4;
    Description description;
    double limitBeats = 0.0;    // 0: until the transport stops
    double endBeats = 0.0;      // set when the take is finished
};
}
