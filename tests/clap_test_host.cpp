// Minimal headless CLAP host for scripted tests: loads a .clap, runs scenarios through process() on the calling
// thread and checks the MIDI that comes out. No GUI, no audio device, no Bitwig.
//
//   clap_test_host <path/to/Plugin.clap>      exit code 0 = all checks passed
//
// The plugin is loaded straight from the given path, never installed, so ~/.clap is left alone.
//
// Timing: the host runs at 120 BPM and 48 kHz, so a quarter note is 24000 samples and the plugin's 1/8th step is
// 12000 samples. Each scenario creates a fresh plugin instance, sets parameters by name through the CLAP params
// extension, drives the transport, and compares the note-ons/offs with expectations written out by hand.

#include <clap/clap.h>

#include "../src/Music.h"

#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr uint32_t sampleRate = 48000;
constexpr uint32_t blockSize = 512;
constexpr uint32_t samplesPerQuarter = sampleRate * 60 / 120;
constexpr uint32_t step = samplesPerQuarter / 2; // the plugin's step grid: 1/8th
constexpr uint32_t oneBar = 188;                 // blocks: 96256 samples, eight steps and a bit
constexpr uint32_t neverStop = UINT32_MAX;
constexpr int cMajor[7] = { 0, 2, 4, 5, 7, 9, 11 }; // written out here on purpose, apart from music::scales

int failures = 0;
bool everyBlockOk = true;
bool everyBlockSilent = true;

void check (bool ok, const char* what)
{
    std::printf ("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok)
        ++failures;
}

const clap_host_t host = {
    CLAP_VERSION, nullptr, "clap_test_host", "seqverse", nullptr, "0.1",
    [] (const clap_host_t*, const char*) -> const void* { return nullptr; }, // get_extension
    [] (const clap_host_t*) {},                                              // request_restart
    [] (const clap_host_t*) {},                                              // request_process
    [] (const clap_host_t*) {},                                              // request_callback
};

const clap_plugin_factory_t* factory = nullptr;
const clap_plugin_descriptor_t* descriptor = nullptr;

struct InEvent
{
    union
    {
        clap_event_header_t header;
        clap_event_midi_t midi;
        clap_event_param_value_t param;
    };
};

// What the plugin pulls through the event callbacks during the current process() call.
std::vector<InEvent> blockEvents;
std::vector<clap_event_midi_t> received; // everything the plugin emitted, with the time made absolute
uint32_t blockStartSample = 0;

uint32_t inSize (const clap_input_events_t*) { return (uint32_t) blockEvents.size(); }

const clap_event_header_t* inGet (const clap_input_events_t*, uint32_t i)
{
    return i < blockEvents.size() ? &blockEvents[i].header : nullptr;
}

bool outTryPush (const clap_output_events_t*, const clap_event_header_t* h)
{
    if (h->type == CLAP_EVENT_MIDI)
    {
        auto e = *reinterpret_cast<const clap_event_midi_t*> (h);
        e.header.time += blockStartSample;
        received.push_back (e);
    }
    return true;
}

clap_event_midi_t makeMidi (uint32_t time, uint8_t b0, uint8_t b1, uint8_t b2)
{
    clap_event_midi_t e {};
    e.header.size = sizeof (e);
    e.header.time = time;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = CLAP_EVENT_MIDI;
    e.data[0] = b0;
    e.data[1] = b1;
    e.data[2] = b2;
    return e;
}

struct Instance
{
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_params_t* params = nullptr;
    std::map<std::string, clap_param_info_t> infos; // by parameter name
};

Instance createInstance()
{
    Instance i;
    i.plugin = factory->create_plugin (factory, &host, descriptor->id);
    if (i.plugin == nullptr || ! i.plugin->init (i.plugin))
    {
        std::fprintf (stderr, "could not create the plugin\n");
        std::exit (2);
    }

    i.params = static_cast<const clap_plugin_params_t*> (i.plugin->get_extension (i.plugin, CLAP_EXT_PARAMS));
    if (i.params != nullptr)
        for (uint32_t n = 0; n < i.params->count (i.plugin); ++n)
        {
            clap_param_info_t info {};
            if (i.params->get_info (i.plugin, n, &info))
                i.infos[info.name] = info;
        }

    if (! i.plugin->activate (i.plugin, sampleRate, 1, blockSize) || ! i.plugin->start_processing (i.plugin))
    {
        std::fprintf (stderr, "could not activate the plugin\n");
        std::exit (2);
    }
    return i;
}

void destroyInstance (Instance& i)
{
    i.plugin->stop_processing (i.plugin);
    i.plugin->deactivate (i.plugin);
    i.plugin->destroy (i.plugin);
    i.plugin = nullptr;
}

// A parameter change by name and text ("Key", "D"), as an event for the next process() call.
bool paramEvent (Instance& i, const char* name, const char* text, InEvent& out)
{
    const auto it = i.infos.find (name);
    double value = 0.0;
    if (it == i.infos.end() || i.params == nullptr || ! i.params->text_to_value (i.plugin, it->second.id, text, &value))
    {
        std::printf ("  FAIL: no parameter '%s' that accepts '%s'\n", name, text);
        ++failures;
        return false;
    }

    out = {};
    out.param.header.size = sizeof (clap_event_param_value_t);
    out.param.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    out.param.header.type = CLAP_EVENT_PARAM_VALUE;
    out.param.param_id = it->second.id;
    out.param.cookie = it->second.cookie;
    out.param.note_id = -1;
    out.param.port_index = -1;
    out.param.channel = -1;
    out.param.key = -1;
    out.param.value = value;
    return true;
}

void processBlock (Instance& i, uint32_t startSample, const clap_event_transport_t* transport)
{
    static std::vector<float> left (blockSize), right (blockSize);
    float* channels[2] = { left.data(), right.data() };
    std::fill (left.begin(), left.end(), 1.0f); // poison, so an unwritten buffer is caught
    std::fill (right.begin(), right.end(), 1.0f);

    clap_audio_buffer_t out {};
    out.channel_count = 2;
    out.data32 = channels;

    const clap_input_events_t in { nullptr, inSize, inGet };
    const clap_output_events_t outEvents { nullptr, outTryPush };

    clap_process_t p {};
    p.steady_time = startSample;
    p.frames_count = blockSize;
    p.transport = transport;
    p.audio_outputs = &out;
    p.audio_outputs_count = 1;
    p.in_events = &in;
    p.out_events = &outEvents;

    blockStartSample = startSample;
    everyBlockOk &= i.plugin->process (i.plugin, &p) != CLAP_PROCESS_ERROR;
    for (uint32_t n = 0; n < blockSize; ++n)
        everyBlockSilent &= left[n] == 0.0f && right[n] == 0.0f;
}

std::vector<uint8_t> saveState (Instance& i)
{
    std::vector<uint8_t> blob;
    const auto* state = static_cast<const clap_plugin_state_t*> (i.plugin->get_extension (i.plugin, CLAP_EXT_STATE));
    const clap_ostream_t stream { &blob,
                                  [] (const clap_ostream_t* s, const void* data, uint64_t size) -> int64_t
                                  {
                                      auto* b = static_cast<std::vector<uint8_t>*> (s->ctx);
                                      b->insert (b->end(), static_cast<const uint8_t*> (data), static_cast<const uint8_t*> (data) + size);
                                      return (int64_t) size;
                                  } };
    if (state == nullptr || ! state->save (i.plugin, &stream))
        blob.clear();
    return blob;
}

bool loadState (Instance& i, const std::vector<uint8_t>& blob)
{
    struct Reader { const std::vector<uint8_t>* data; size_t pos; } reader { &blob, 0 };
    const auto* state = static_cast<const clap_plugin_state_t*> (i.plugin->get_extension (i.plugin, CLAP_EXT_STATE));
    const clap_istream_t stream { &reader,
                                  [] (const clap_istream_t* s, void* data, uint64_t size) -> int64_t
                                  {
                                      auto* r = static_cast<Reader*> (s->ctx);
                                      const size_t n = std::min<size_t> ((size_t) size, r->data->size() - r->pos);
                                      std::memcpy (data, r->data->data() + r->pos, n);
                                      r->pos += n;
                                      return (int64_t) n;
                                  } };
    return state != nullptr && state->load (i.plugin, &stream);
}

struct Scenario
{
    std::vector<std::pair<const char*, const char*>> params; // by name and text, set before the first block
    std::vector<std::pair<const char*, double>> rawParams;   // by name and the raw value a host sends: 5 is the sixth choice
    std::vector<clap_event_midi_t> midiIn;                    // MIDI input, with absolute times
    double startQuarters = 0.0;                               // transport position at sample 0
    uint32_t numBlocks = oneBar;
    uint32_t stopBlock = neverStop; // the transport stops from this block on
    bool playing = true;
    bool sendTransport = true;
    bool roundTripState = false; // set the parameters on one instance, save, and load into a fresh one
};

// Runs a scenario on a fresh plugin instance and returns what the plugin emitted.
std::vector<clap_event_midi_t> run (const Scenario& s)
{
    Instance inst = createInstance();

    std::vector<InEvent> paramEvents;
    for (const auto& [name, text] : s.params)
    {
        InEvent e {};
        if (paramEvent (inst, name, text, e))
            paramEvents.push_back (e);
    }

    for (const auto& [name, value] : s.rawParams)
    {
        InEvent e {};
        if (paramEvent (inst, name, "0", e)) // finds the parameter, the value is replaced
        {
            e.param.value = value;
            paramEvents.push_back (e);
        }
    }

    if (s.roundTripState)
    {
        blockEvents = paramEvents; // one stopped block so the parameters take effect, then save into a fresh instance
        processBlock (inst, 0, nullptr);
        const auto blob = saveState (inst);
        destroyInstance (inst);
        inst = createInstance();
        check (! blob.empty() && loadState (inst, blob), "state saved and loaded into a fresh instance");
        paramEvents.clear();
    }

    received.clear();

    for (uint32_t block = 0; block < s.numBlocks; ++block)
    {
        const uint32_t start = block * blockSize;

        blockEvents.clear();
        if (block == 0)
            blockEvents = paramEvents;

        for (auto e : s.midiIn)
            if (e.header.time >= start && e.header.time < start + blockSize)
            {
                e.header.time -= start; // block-relative for the plugin
                InEvent in {};
                in.midi = e;
                blockEvents.push_back (in);
            }

        clap_event_transport_t transport {};
        transport.header.size = sizeof (transport);
        transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        transport.header.type = CLAP_EVENT_TRANSPORT;
        transport.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
        if (s.playing && block < s.stopBlock)
            transport.flags |= CLAP_TRANSPORT_IS_PLAYING;
        transport.tempo = 120.0;
        transport.tsig_num = 4;
        transport.tsig_denom = 4;
        transport.song_pos_beats = (clap_beattime) std::llround ((s.startQuarters + (double) start / samplesPerQuarter) * CLAP_BEATTIME_FACTOR);

        processBlock (inst, start, s.sendTransport ? &transport : nullptr);
    }

    destroyInstance (inst);
    return received;
}

struct Note
{
    int note;
    uint32_t time;
    int velocity = 0; // only filled in by notesOf, and not compared by sameNotes
};

// Note-ons (status 0x90) or note-offs (0x80) among the events, in the order they came out.
std::vector<Note> notesOf (const std::vector<clap_event_midi_t>& events, uint8_t status)
{
    std::vector<Note> result;
    for (const auto& e : events)
        if ((e.data[0] & 0xF0) == status)
            result.push_back ({ e.data[1], e.header.time, e.data[2] });
    return result;
}

// True when there are events and all of them are on the given MIDI channel (1 to 16).
bool allOnChannel (const std::vector<clap_event_midi_t>& events, int channel)
{
    bool ok = ! events.empty();
    for (const auto& e : events)
        ok &= (e.data[0] & 0x0F) == channel - 1;
    return ok;
}

// The note numbers only, for comparing two runs.
std::vector<int> noteNumbers (const std::vector<Note>& notes)
{
    std::vector<int> result;
    for (const auto& n : notes)
        result.push_back (n.note);
    return result;
}

// Notes on a regular grid: the i-th note starts at firstTime + i * spacing.
std::vector<Note> grid (std::vector<int> notes, uint32_t firstTime, uint32_t spacing = step)
{
    std::vector<Note> result;
    for (size_t i = 0; i < notes.size(); ++i)
        result.push_back ({ notes[i], firstTime + (uint32_t) i * spacing });
    return result;
}

// Same notes at the same sample times, give or take one sample. `prefixOnly` compares just the first want.size().
bool sameNotes (const std::vector<Note>& got, const std::vector<Note>& want, bool prefixOnly = false)
{
    bool ok = prefixOnly ? got.size() >= want.size() : got.size() == want.size();
    for (size_t i = 0; ok && i < want.size(); ++i)
        ok = got[i].note == want[i].note && got[i].time + 1 >= want[i].time && got[i].time <= want[i].time + 1;

    if (! ok)
    {
        std::printf ("    got %zu notes, wanted %zu%s. first ones, got vs wanted:\n", got.size(), want.size(), prefixOnly ? "+" : "");
        for (size_t i = 0; i < std::max (got.size(), want.size()) && i < 10; ++i)
        {
            const std::string g = i < got.size() ? std::to_string (got[i].note) + "@" + std::to_string (got[i].time) : "-";
            const std::string w = i < want.size() ? std::to_string (want[i].note) + "@" + std::to_string (want[i].time) : "-";
            std::printf ("      %-12s %-12s\n", g.c_str(), w.c_str());
        }
    }
    return ok;
}

// C major going up on the 1/8 grid, where the arp starts over after `stepsPerCycle` steps: the notes a cycle of
// that many steps gives. With a cycle of 7 steps or a multiple of it the pattern is the same as without a cycle.
std::vector<Note> cycledArp (int count, int stepsPerCycle)
{
    std::vector<Note> result;
    for (int i = 0; i < count; ++i)
        result.push_back ({ 60 + cMajor[(i % stepsPerCycle) % 7], (uint32_t) i * step });
    return result;
}

// True when every note-on has a twin one cycle earlier or later, with the same note and velocity, at the same place in
// its cycle: whatever plays in one cycle plays in the next. Only the `cycles` complete cycles are compared.
bool cyclesRepeat (const std::vector<Note>& ons, uint32_t cycleSamples, uint32_t cycles)
{
    const auto hasTwin = [&] (const Note& n, uint32_t time)
    {
        for (const auto& m : ons)
            if (m.note == n.note && m.velocity == n.velocity && m.time + 1 >= time && m.time <= time + 1)
                return true;
        return false;
    };

    const uint32_t end = cycleSamples * cycles;
    bool ok = true;
    for (const auto& n : ons)
    {
        if (n.time + cycleSamples < end)
            ok &= hasTwin (n, n.time + cycleSamples);
        if (n.time >= cycleSamples && n.time < end)
            ok &= hasTwin (n, n.time - cycleSamples);
    }
    return ok;
}

// A note that ends on the sample where the same note starts again has to end first.
bool offsBeforeOnsAtSameSample (const std::vector<clap_event_midi_t>& events)
{
    for (size_t i = 0; i < events.size(); ++i)
        for (size_t j = i + 1; j < events.size(); ++j)
            if ((events[i].data[0] & 0xF0) == 0x90 && (events[j].data[0] & 0xF0) == 0x80
                && events[i].header.time == events[j].header.time && events[i].data[1] == events[j].data[1])
                return false;
    return true;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf (stderr, "usage: %s <plugin.clap>\n", argv[0]);
        return 2;
    }

    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr)
    {
        std::fprintf (stderr, "dlopen failed: %s\n", dlerror());
        return 2;
    }

    auto* entry = static_cast<const clap_plugin_entry_t*> (dlsym (lib, "clap_entry"));
    if (entry == nullptr || ! entry->init (argv[1]))
    {
        std::fprintf (stderr, "no usable clap_entry in %s\n", argv[1]);
        return 2;
    }

    factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    if (factory == nullptr || factory->get_plugin_count (factory) < 1)
    {
        std::fprintf (stderr, "no plugin in factory\n");
        return 2;
    }

    descriptor = factory->get_plugin_descriptor (factory, 0);
    std::printf ("plugin: %s (%s)\n", descriptor->name, descriptor->id);

    {
        std::printf ("setup\n");
        Instance i = createInstance();
        auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*> (i.plugin->get_extension (i.plugin, CLAP_EXT_AUDIO_PORTS));
        check (audioPorts != nullptr && audioPorts->count (i.plugin, true) == 0 && audioPorts->count (i.plugin, false) == 1,
               "audio ports: no input, one output");
        // A host only offers a stepped parameter the values between its min and max. If the range were 0..1 for
        // the 44 scales, Bitwig could pick just two of them, so the reported span has to be one step per choice.
        // Integer parameters are not flagged stepped by JUCE, so for them only the span is required.
        struct Span { const char* name; double span; bool mustBeStepped; };
        const Span spans[] = {
            { "Key", music::kNumKeys - 1, true },
            { "Scale", music::kNumScales - 1, true },
            { "Play Mode", music::numPlayModes - 1, true },
            { "Arp Direction", music::numArpDirections - 1, true },
            { "Step Rate", music::kNumStepRates - 1, true },
            { "Note Length", music::kNumNoteLengths - 1, true },
            { "Chance", 100, false },
            { "Swing", 100, false },
            { "Low Octave", music::kMaxOctave - music::kMinOctave, false },
            { "High Octave", music::kMaxOctave - music::kMinOctave, false },
            { "Velocity", 126, false },
            { "Velocity Variation", 64, false },
            { "Channel", 15, false },
            { "Cycle Length", music::kMaxCycleHalfBeats, false },
            { "Seed", 9999, false },
        };
        for (const auto& expected : spans)
        {
            const auto it = i.infos.find (expected.name);
            check (it != i.infos.end(), expected.name);
            if (it != i.infos.end())
            {
                const auto& info = it->second;
                const bool ok = ((info.flags & CLAP_PARAM_IS_STEPPED) != 0 || ! expected.mustBeStepped)
                                && info.max_value - info.min_value == expected.span;
                if (! ok)
                    std::printf ("    '%s': flags %u, range %g to %g, expected %sa range spanning %g\n", expected.name, info.flags,
                                 info.min_value, info.max_value, expected.mustBeStepped ? "stepped, " : "", expected.span);
                check (ok, (std::string (expected.name) + " reports one step per value to the host").c_str());
            }
        }
        destroyInstance (i);
    }

    // Defaults: C major, arpeggio up, chance 100 %, 1/16th notes. The 1/8th steps start at 0, 12000, 24000 ...
    std::printf ("default settings, transport playing from the start\n");
    {
        Scenario s;
        s.midiIn = { makeMidi (100, 0x90, 61, 100), makeMidi (5000, 0x80, 61, 0) }; // must be ignored
        const auto out = run (s);
        check (sameNotes (notesOf (out, 0x90), grid ({ 60, 62, 64, 65, 67, 69, 71, 60, 62 }, 0)),
               "note-ons: C D E F G A B C D on the 1/8th grid, and no trace of the MIDI input");
        check (sameNotes (notesOf (out, 0x80), grid ({ 60, 62, 64, 65, 67, 69, 71, 60 }, step / 2)),
               "note-offs: each 1/16th after its note-on");
    }

    std::printf ("transport stopped, or no transport information\n");
    {
        Scenario s;
        s.playing = false;
        check (run (s).empty(), "nothing is generated while the transport is stopped");

        s = Scenario();
        s.sendTransport = false;
        check (run (s).empty(), "nothing is generated when the host sends no transport");
    }

    std::printf ("transport stops while a note sounds\n");
    {
        Scenario s;
        s.params = { { "Note Length", "1/1" } };
        s.numBlocks = 40;
        s.stopBlock = 6;
        const auto out = run (s);
        check (sameNotes (notesOf (out, 0x90), grid ({ 60 }, 0)), "one note-on");
        check (sameNotes (notesOf (out, 0x80), grid ({ 60 }, 6 * blockSize)), "its note-off comes when the transport stops");
    }

    std::printf ("transport starts between two steps: the pattern follows the timeline\n");
    {
        Scenario s;
        s.startQuarters = 1.25; // step 2 is at quarter 1.0, step 3 at 1.5, which is 6000 samples from the start
        s.numBlocks = 100;
        const auto out = run (s);
        check (sameNotes (notesOf (out, 0x90), grid ({ 65, 67, 69, 71 }, 6000)), "starts on step 3 of the arp (F G A B)");
    }

    std::printf ("D natural minor, arp down, 1/8th notes\n");
    {
        Scenario s;
        s.params = { { "Key", "D" }, { "Scale", "Natural Minor (Aeolian)" }, { "Arp Direction", "Down" }, { "Note Length", "1/8" } };
        const auto out = run (s);
        check (sameNotes (notesOf (out, 0x90), grid ({ 72, 70, 69, 67, 65, 64, 62, 72, 70 }, 0)), "note-ons: C Bb A G F E D C Bb");
        check (sameNotes (notesOf (out, 0x80), grid ({ 72, 70, 69, 67, 65, 64, 62, 72 }, step)), "each note ends where the next starts");
        check (offsBeforeOnsAtSameSample (out), "a note-off comes before a note-on on the same sample");
    }

    // 1/1 notes last 96000 samples, so C (step 0) is still sounding when the arp comes round to C again at step 7.
    std::printf ("a note is played again while it still sounds\n");
    {
        Scenario s;
        s.params = { { "Note Length", "1/1" } };
        const auto out = run (s);
        check (sameNotes (notesOf (out, 0x90), grid ({ 60, 62, 64, 65, 67, 69, 71, 60, 62 }, 0)), "every step still plays its note");
        check (sameNotes (notesOf (out, 0x80), { { 60, 7 * step }, { 62, 8 * step } }),
               "the sounding C ends where the new C starts, D likewise, and nothing else ends yet");
        check (offsBeforeOnsAtSameSample (out), "a note-off comes before the note-on of the same note on the same sample");
    }

    std::printf ("arp directions over C major (7 notes)\n");
    {
        struct Pattern { int direction; std::vector<int> indices; };
        const Pattern patterns[] = {
            { music::up,        { 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6 } },
            { music::down,      { 6, 5, 4, 3, 2, 1, 0, 6, 5, 4, 3, 2, 1, 0 } },
            { music::upDown,    { 0, 1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1, 0, 1 } },
            { music::downUp,    { 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6, 5 } },
            { music::upAndDown, { 0, 1, 2, 3, 4, 5, 6, 6, 5, 4, 3, 2, 1, 0 } },
            { music::converge,  { 0, 6, 1, 5, 2, 4, 3, 0, 6, 1, 5, 2, 4, 3 } },
            { music::diverge,   { 3, 4, 2, 5, 1, 6, 0, 3, 4, 2, 5, 1, 6, 0 } },
        };

        for (const auto& pattern : patterns)
        {
            Scenario s;
            s.params = { { "Arp Direction", music::arpDirectionNames[pattern.direction] }, { "Note Length", "1/32" } };
            s.numBlocks = 330; // 14 steps and a bit

            std::vector<int> notes;
            for (int index : pattern.indices)
                notes.push_back (60 + cMajor[index]);

            check (sameNotes (notesOf (run (s), 0x90), grid (notes, 0), true), music::arpDirectionNames[pattern.direction]);
        }
    }

    std::printf ("random mode stays in the scale\n");
    {
        Scenario s;
        s.params = { { "Play Mode", "Random" }, { "Scale", "Major Pentatonic" }, { "Note Length", "1/32" } };
        s.numBlocks = 750; // four bars, 32 steps
        const auto ons = notesOf (run (s), 0x90);

        bool inScale = true;
        std::vector<int> distinct;
        for (const auto& n : ons)
        {
            inScale &= n.note == 60 || n.note == 62 || n.note == 64 || n.note == 67 || n.note == 69;
            if (std::find (distinct.begin(), distinct.end(), n.note) == distinct.end())
                distinct.push_back (n.note);
        }
        check (ons.size() == 32, "32 steps, 32 notes");
        check (inScale, "every note is in C major pentatonic");
        check (distinct.size() >= 3, "and they are not all the same note");
    }

    std::printf ("every scale, in a different key each, in random mode\n");
    {
        int wrong = 0;
        for (int i = 0; i < music::kNumScales; ++i)
        {
            const int key = i % music::kNumKeys;
            Scenario s;
            s.params = { { "Key", music::keyNames[key] }, { "Scale", music::scales[i].name }, { "Play Mode", "Random" }, { "Note Length", "1/32" } };
            const auto ons = notesOf (run (s), 0x90);

            bool ok = ons.size() == 9;
            for (const auto& n : ons)
            {
                bool inScale = false;
                for (int k = 0; k < music::scales[i].size; ++k)
                    inScale |= n.note == music::noteNumber (key, music::kDefaultOctave, music::scales[i].intervals[k]);
                ok &= inScale;
            }

            if (! ok)
            {
                std::printf ("    %s in %s: %zu notes, not all in the scale\n", music::scales[i].name, music::keyNames[key], ons.size());
                ++wrong;
            }
        }
        check (wrong == 0, "all notes are in the scale, and a bar has 9 of them");
    }

    std::printf ("chance 0 %% and MIDI input\n");
    {
        Scenario s;
        s.params = { { "Chance", "0" } };
        s.midiIn = { makeMidi (100, 0x90, 60, 100), makeMidi (5000, 0x80, 60, 0) };
        check (run (s).empty(), "no notes at all, and the MIDI input is not passed on");
    }

    // What a host does when the user picks an entry in its own parameter list, or when automation runs: it sends the
    // choice's index, without going through any text.
    std::printf ("choices picked by index, as a host sends them\n");
    {
        Scenario s;
        s.rawParams = { { "Key", 2 }, { "Scale", 5 } }; // D, and the sixth scale: Phrygian
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 62, 63, 65, 67, 69, 70, 72, 62, 63 }, 0)), "Key 2 and Scale 5 play D Phrygian");

        s.rawParams = { { "Scale", (double) music::kNumScales - 1 } }; // the last one, Egyptian: 0 2 5 7 10
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 60, 62, 65, 67, 70, 60, 62, 65, 67 }, 0)), "the last scale can be reached too");

        s.rawParams = { { "Scale", 20 } }; // Double Harmonic (Byzantine): 0 1 4 5 7 8 11
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 60, 61, 64, 65, 67, 68, 71, 60, 61 }, 0)), "and one from the middle of the list");
    }

    std::printf ("step rate\n");
    {
        Scenario s;
        s.params = { { "Step Rate", "1/4" } };
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 60, 62, 64, 65, 67 }, 0, 2 * step)), "1/4: a note every 24000 samples");

        s.params = { { "Step Rate", "1/8T" } };
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 60, 62, 64, 65, 67, 69, 71, 60, 62, 64, 65, 67, 69 }, 0, 8000)),
               "1/8T: a note every 8000 samples");
    }

    std::printf ("octave range\n");
    {
        Scenario s;
        s.params = { { "Low Octave", "3" }, { "High Octave", "5" } };
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 48, 50, 52, 53, 55, 57, 59, 60, 62 }, 0)),
               "octaves 3 to 5: the arp starts in octave 3 and climbs into 4");

        s.params = { { "Low Octave", "6" }, { "High Octave", "4" } };
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 60, 62, 64, 65, 67, 69, 71, 72, 74 }, 0)),
               "low above high: the range is 4 to 6 anyway");
    }

    std::printf ("velocity, variation and channel\n");
    {
        Scenario s;
        s.params = { { "Velocity", "64" } };
        bool all64 = true;
        for (const auto& n : notesOf (run (s), 0x90))
            all64 &= n.velocity == 64;
        check (all64, "every note has the velocity that was set");

        s.params = { { "Velocity", "120" }, { "Velocity Variation", "20" } };
        s.numBlocks = 750;
        bool inRange = true;
        std::vector<int> distinct;
        for (const auto& n : notesOf (run (s), 0x90))
        {
            inRange &= n.velocity >= 100 && n.velocity <= 127;
            if (std::find (distinct.begin(), distinct.end(), n.velocity) == distinct.end())
                distinct.push_back (n.velocity);
        }
        check (inRange, "120 plus or minus 20 stays within 100 and the MIDI maximum 127");
        check (distinct.size() >= 5, "and the velocities do vary");

        s = Scenario();
        s.params = { { "Channel", "10" } };
        check (allOnChannel (run (s), 10), "all notes, on and off, are on channel 10");
    }

    std::printf ("swing: every second step comes late\n");
    {
        Scenario s;
        s.params = { { "Swing", "100" } }; // 100 %: a third of a step, 4000 samples at 1/8
        std::vector<Note> want;
        for (uint32_t k = 0; k < 9; ++k)
            want.push_back ({ 60 + cMajor[k % 7], k * step + (k % 2 == 1 ? step / 3 : 0) });
        check (sameNotes (notesOf (run (s), 0x90), want), "1/8 steps: C at 0, D at 16000, E at 24000, F at 40000 ...");

        // 3000-sample steps land on block boundaries in many different ways: no step may repeat or go missing
        s.params = { { "Swing", "100" }, { "Step Rate", "1/32" }, { "Note Length", "1/32" } };
        want.clear();
        for (uint32_t k = 0; k < 33; ++k)
            want.push_back ({ 60 + cMajor[k % 7], k * 3000 + (k % 2 == 1 ? 1000 : 0) });
        check (sameNotes (notesOf (run (s), 0x90), want), "1/32 steps: 33 notes, none twice and none missing");
    }

    std::printf ("seed\n");
    {
        Scenario fixed;
        fixed.params = { { "Play Mode", "Random" }, { "Scale", "Major Pentatonic" }, { "Note Length", "1/32" }, { "Seed", "42" } };
        fixed.numBlocks = 750;
        const auto first = noteNumbers (notesOf (run (fixed), 0x90));
        check (first.size() == 32 && first == noteNumbers (notesOf (run (fixed), 0x90)), "a fixed seed gives the same notes every time");

        Scenario other = fixed;
        other.params.back() = { "Seed", "43" };
        check (first != noteNumbers (notesOf (run (other), 0x90)), "another seed gives other notes");

        Scenario later = fixed; // starts two quarters in, at step 4: the notes must be those of steps 4 to 8 above
        later.startQuarters = 2.0;
        later.numBlocks = 100;
        const auto fromStep4 = noteNumbers (notesOf (run (later), 0x90));
        check (fromStep4.size() == 5 && std::equal (fromStep4.begin(), fromStep4.end(), first.begin() + 4),
               "a step plays the same note wherever the transport starts");

        Scenario automatic = fixed;
        automatic.params.back() = { "Seed", "Auto" };
        check (noteNumbers (notesOf (run (automatic), 0x90)) != noteNumbers (notesOf (run (automatic), 0x90)),
               "seed Auto gives different notes each time");
    }

    // 4.5 beats are nine 1/8 steps, so C major (7 notes) going up starts over after every nine steps: C D E F G A B C D | C D E ...
    std::printf ("cycle length\n");
    {
        Scenario s;
        s.params = { { "Cycle Length", "4.5" } };
        s.numBlocks = 422; // two cycles of 108000 samples, and the first step of the third
        check (sameNotes (notesOf (run (s), 0x90), cycledArp (19, 9)), "4.5 beats: the arpeggio starts over every nine steps");

        s = Scenario();
        s.rawParams = { { "Cycle Length", 9 } }; // what a host sends: nine half beats
        s.numBlocks = 422;
        check (sameNotes (notesOf (run (s), 0x90), cycledArp (19, 9)), "picked by value, as a host sends it: 9 is 4.5 beats");

        s = Scenario();
        s.params = { { "Cycle Length", "Endless" } };
        check (sameNotes (notesOf (run (s), 0x90), cycledArp (9, 7)), "Endless plays as if there was no cycle");

        s = Scenario();
        s.params = { { "Cycle Length", "0.5" } }; // one step per cycle
        check (sameNotes (notesOf (run (s), 0x90), cycledArp (9, 1)), "half a beat: every step is the first step of a cycle");

        // 1/4T steps are 2/3 of a beat, so a one beat cycle has two of them, the second one 16000 samples in, and then the
        // grid starts over: the last step of a cycle is cut short
        s = Scenario();
        s.params = { { "Cycle Length", "1" }, { "Step Rate", "1/4T" } };
        check (sameNotes (notesOf (run (s), 0x90),
                          { { 60, 0 }, { 62, 16000 }, { 60, 24000 }, { 62, 40000 }, { 60, 48000 }, { 62, 64000 }, { 60, 72000 }, { 62, 88000 }, { 60, 96000 } }),
               "1 beat with 1/4T steps: two steps per cycle, then the grid starts over");

        // Cycles are laid out on the host timeline from zero: at 5.5 beats the second cycle (4.5 to 9) is in its third step
        s = Scenario();
        s.params = { { "Cycle Length", "4.5" } };
        s.startQuarters = 5.5;
        s.numBlocks = 100;
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 64, 65, 67, 69, 71 }, 0)), "starting inside a cycle joins it where the timeline is");
    }

    std::printf ("cycle length with random choices\n");
    {
        // 4 beats: eight 1/8 steps, 96000 samples. Three cycles and the first step of the fourth.
        Scenario s;
        s.params = { { "Play Mode", "Random" }, { "Scale", "Chromatic" }, { "Note Length", "1/32" }, { "Cycle Length", "4" } };
        s.numBlocks = 563;
        const auto ons = notesOf (run (s), 0x90);
        std::vector<int> distinct;
        for (const auto& n : ons)
            if (std::find (distinct.begin(), distinct.end(), n.note) == distinct.end())
                distinct.push_back (n.note);
        check (ons.size() == 25 && distinct.size() >= 4, "random notes, eight steps a cycle");
        check (cyclesRepeat (ons, 96000, 3), "seed Auto: each cycle repeats the notes of the one before");
        check (noteNumbers (ons) != noteNumbers (notesOf (run (s), 0x90)), "and a new play rolls a new phrase");

        s.params.push_back ({ "Seed", "42" });
        check (noteNumbers (notesOf (run (s), 0x90)) == noteNumbers (notesOf (run (s), 0x90)), "with a fixed seed the phrase is the same every time");

        // Arpeggio: which steps play, how hard and how late, are all random or swung here, and repeat in every cycle
        s = Scenario();
        s.params = { { "Chance", "50" }, { "Velocity Variation", "20" }, { "Swing", "60" }, { "Seed", "5" }, { "Cycle Length", "4" } };
        s.numBlocks = 563;
        const auto arp = notesOf (run (s), 0x90);
        int inFirstCycle = 0;
        for (const auto& n : arp)
            inFirstCycle += n.time < 96000;
        check (inFirstCycle > 0 && inFirstCycle < 8, "arpeggio at 50 %: some of the eight steps play, not all");
        check (cyclesRepeat (arp, 96000, 3), "and the same ones play, with the same velocity and swing, in every cycle");

        // Without a cycle the same settings do not repeat: the cycle is what makes them
        s.params.pop_back();
        check (! cyclesRepeat (notesOf (run (s), 0x90), 96000, 3), "without a cycle length nothing repeats");
    }

    std::printf ("saved state\n");
    {
        Scenario s;
        s.params = { { "Key", "E" }, { "Scale", "Dorian" } };
        s.roundTripState = true;
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 64, 66, 67, 69, 71, 73, 74, 64, 66 }, 0)),
               "key and scale come back after loading: E dorian");

        s.params = { { "Step Rate", "1/4" }, { "Low Octave", "3" }, { "High Octave", "3" }, { "Channel", "3" }, { "Velocity", "77" } };
        const auto out = run (s);
        const auto ons = notesOf (out, 0x90);
        check (sameNotes (ons, grid ({ 48, 50, 52, 53, 55 }, 0, 2 * step)), "step rate and octaves come back: 1/4 steps in octave 3");
        check (allOnChannel (out, 3) && ! ons.empty() && ons[0].velocity == 77, "channel and velocity come back");

        s = Scenario();
        s.params = { { "Cycle Length", "4.5" } };
        s.numBlocks = 250; // 11 steps
        s.roundTripState = true;
        check (sameNotes (notesOf (run (s), 0x90), cycledArp (11, 9)), "the cycle length comes back: 4.5 beats");
    }

    std::printf ("all blocks\n");
    check (everyBlockOk, "process() never returned an error");
    check (everyBlockSilent, "the audio output was always silent");

    entry->deinit();
    dlclose (lib);

    std::printf ("%s\n", failures == 0 ? "PASS" : "FAILED");
    return failures == 0 ? 0 : 1;
}
