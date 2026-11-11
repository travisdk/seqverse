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
#include "../src/Take.h"

#if defined(_WIN32)
#include <windows.h>
#define LOAD_LIBRARY(path) LoadLibraryA(path)
#define GET_PROC_ADDRESS(lib, name) GetProcAddress(lib, name)
#define SLEEP_US(us) Sleep((us) / 1000)
#else
#include <dlfcn.h>
#include <unistd.h>
#define LOAD_LIBRARY(path) dlopen(path, RTLD_NOW | RTLD_LOCAL)
#define GET_PROC_ADDRESS(lib, name) dlsym(lib, name)
#define SLEEP_US(us) usleep(us)
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
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

// The plugin writes a finished take as a MIDI file in the folder SEQVERSE_TAKE_FOLDER names (main() points it at a
// folder of its own, which does not exist until the plugin makes it).
std::filesystem::path takeFolder;

void clearTakes()
{
    std::error_code ignored;
    for (const auto& entry : std::filesystem::directory_iterator (takeFolder, ignored))
        std::filesystem::remove (entry.path(), ignored);
}

// A take's name must survive an unencoded file:// URI, which is how JUCE's X11 drag sends it: letters, digits, "-" and "_"
// (and the one dot of ".mid") only
bool onlySafeCharacters (const std::string& name)
{
    return std::regex_match (name, std::regex ("[A-Za-z0-9_-]+\\.mid"));
}

// The files that are named like a take, "SeqVerse_... .mid"
std::vector<std::filesystem::path> takeFiles()
{
    std::vector<std::filesystem::path> files;
    std::error_code ignored;
    for (const auto& entry : std::filesystem::directory_iterator (takeFolder, ignored))
        if (entry.path().extension() == ".mid" && entry.path().filename().string().rfind ("SeqVerse_", 0) == 0)
            files.push_back (entry.path());
    return files;
}

struct Scenario
{
    std::vector<std::pair<const char*, const char*>> params; // by name and text, set before the first block
    std::vector<std::pair<const char*, double>> rawParams;   // by name and the raw value a host sends: 5 is the sixth choice
    std::vector<clap_event_midi_t> midiIn;                    // MIDI input, with absolute times
    double startQuarters = 0.0;                               // transport position at sample 0
    uint32_t numBlocks = oneBar;
    uint32_t stopBlock = neverStop; // the transport stops from this block on
    uint32_t restartBlock = neverStop; // after a stop, the transport plays again from this block (after a pause of a human's length)
    uint32_t jumpBlock = neverStop;    // from this block on the transport is at jumpToQuarters + the time since, as after a loop
    double jumpToQuarters = 0.0;
    int tsigNumerator = 4;
    int tsigDenominator = 4;
    bool playing = true;
    bool sendTransport = true;
    bool roundTripState = false; // set the parameters on one instance, save, and load into a fresh one
    std::vector<uint8_t> savedState; // a saved session to load into the instance before anything else

    // The plugin includes the octave note by default. Most scenarios are about something else, and their expectations
    // are written for one octave of seven notes, so they run without it unless they ask for it (or set it themselves).
    bool octaveNote = false;

    bool keepTakes = false; // the files of the earlier runs are left in the takes folder
    bool readRecordAtEnd = false; // wait for the writer, then read the Record parameter into recordAtEnd
};

double recordAtEnd = -1.0; // what the host sees Record at after a scenario with readRecordAtEnd

// Runs a scenario on a fresh plugin instance and returns what the plugin emitted.
std::vector<clap_event_midi_t> run (const Scenario& s)
{
    Instance inst = createInstance();

    if (! s.savedState.empty())
        check (loadState (inst, s.savedState), "a saved session was loaded");

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

    bool setsOctaveNote = s.octaveNote;
    for (const auto& p : s.params)
        setsOctaveNote |= std::string (p.first) == "Octave Note";
    for (const auto& p : s.rawParams)
        setsOctaveNote |= std::string (p.first) == "Octave Note";

    if (! setsOctaveNote)
    {
        InEvent e {};
        if (paramEvent (inst, "Octave Note", "Exclude", e))
            paramEvents.insert (paramEvents.begin(), e);
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
    if (! s.keepTakes)
        clearTakes();

    for (uint32_t block = 0; block < s.numBlocks; ++block)
    {
        const uint32_t start = block * blockSize;

        // A human takes longer than a test to press stop and then play, and the file of a take is written in the
        // background a moment after the take is finished
        if (block == s.restartBlock)
            SLEEP_US (300 * 1000);

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
        if (s.playing && (block < s.stopBlock || block >= s.restartBlock))
            transport.flags |= CLAP_TRANSPORT_IS_PLAYING;
        transport.tempo = 120.0;
        transport.tsig_num = (uint16_t) s.tsigNumerator;
        transport.tsig_denom = (uint16_t) s.tsigDenominator;
        const double quarters = block >= s.jumpBlock ? s.jumpToQuarters + (double) (block - s.jumpBlock) * blockSize / samplesPerQuarter
                                                     : s.startQuarters + (double) start / samplesPerQuarter;
        transport.song_pos_beats = (clap_beattime) std::llround (quarters * CLAP_BEATTIME_FACTOR);

        processBlock (inst, start, s.sendTransport ? &transport : nullptr);
    }

    if (s.readRecordAtEnd)
    {
        SLEEP_US (300 * 1000); // Record is switched off by the writer thread, a moment after the take is finished
        double value = -1.0;
        const auto found = inst.infos.find ("Record");
        if (found != inst.infos.end() && inst.params != nullptr)
            inst.params->get_value (inst.plugin, found->second.id, &value);
        recordAtEnd = value;
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
// A take as it was read back from the MIDI file the plugin wrote
struct FileNote
{
    int channel;
    int note;
    int velocity;
    uint32_t on;
    uint32_t off;
};

struct TakeFile
{
    int files = 0; // how many files there are (there should be one, or none)
    std::string name;
    int division = 0;
    uint32_t endTick = 0;
    uint32_t microsecondsPerQuarter = 0;
    int tsigNumerator = 0;
    int tsigDenominator = 0;
    bool wellFormed = false;
    std::vector<FileNote> notes; // in the order they start
};

TakeFile readTakeFile (const std::filesystem::path& path)
{
    TakeFile take;
    take.files = 1;
    take.name = path.filename().string();

    std::ifstream in (path, std::ios::binary);
    const std::vector<uint8_t> data ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());

    size_t pos = 0;
    const auto u16 = [&] { const uint32_t v = (uint32_t) data[pos] << 8 | data[pos + 1]; pos += 2; return v; };
    const auto u32 = [&] { const uint32_t v = (uint32_t) u16() << 16; return v | u16(); };
    const auto variable = [&]
    {
        uint32_t v = 0;
        while (pos < data.size())
        {
            const uint8_t b = data[pos++];
            v = v << 7 | (b & 0x7f);
            if ((b & 0x80) == 0)
                break;
        }
        return v;
    };

    if (data.size() < 22 || std::memcmp (data.data(), "MThd", 4) != 0)
        return take;

    pos = 4;
    u32(); // the length of the header
    const uint32_t format = u16();
    const uint32_t tracks = u16();
    take.division = (int) u16();
    if (format > 1 || tracks != 1 || std::memcmp (data.data() + pos, "MTrk", 4) != 0)
        return take;

    pos += 4;
    const size_t trackEnd = pos + 4 + u32(); // u32 reads the length, and the start of the track's data follows it
    uint32_t tick = 0;
    uint8_t running = 0;
    std::vector<FileNote> open;

    while (pos < trackEnd && pos < data.size())
    {
        tick += variable();
        uint8_t status = data[pos];
        if (status & 0x80)
            ++pos;
        else
            status = running;

        if (status == 0xff)
        {
            const uint8_t type = data[pos++];
            const uint32_t length = variable();
            if (type == 0x51 && length == 3)
                take.microsecondsPerQuarter = (uint32_t) data[pos] << 16 | (uint32_t) data[pos + 1] << 8 | data[pos + 2];
            if (type == 0x58 && length >= 2)
            {
                take.tsigNumerator = data[pos];
                take.tsigDenominator = 1 << data[pos + 1];
            }
            pos += length;
            if (type == 0x2f)
            {
                take.endTick = tick;
                take.wellFormed = true;
                break;
            }
            continue;
        }

        running = status;
        const int kind = status & 0xf0;
        const int channel = (status & 0x0f) + 1;
        if (kind == 0xc0 || kind == 0xd0)
        {
            ++pos;
            continue;
        }

        const uint8_t first = data[pos++];
        const uint8_t second = data[pos++];
        if (kind == 0x90 && second > 0)
            open.push_back ({ channel, first, second, tick, 0 });

        if (kind == 0x80 || (kind == 0x90 && second == 0))
            for (auto it = open.begin(); it != open.end(); ++it)
                if (it->channel == channel && it->note == first)
                {
                    it->off = tick;
                    take.notes.push_back (*it);
                    open.erase (it);
                    break;
                }
    }

    take.wellFormed &= open.empty(); // every note has an end
    std::sort (take.notes.begin(), take.notes.end(), [] (const FileNote& a, const FileNote& b) { return a.on < b.on; });
    return take;
}

// The one take file there is (files is 0 when there is none, and 2 or more when there are several: nothing is read then)
TakeFile readTake()
{
    const auto files = takeFiles();
    if (files.size() != 1)
    {
        TakeFile none;
        none.files = (int) files.size();
        return none;
    }
    return readTakeFile (files[0]);
}

// C major going up on the 1/8 grid: `count` notes starting with the arpeggio's `first`-th, the first at `firstTick` and
// then every 480 ticks (a 1/8th at 960 per quarter), each lasting a 1/16th (240 ticks).
std::vector<FileNote> takeGrid (int count, int first, uint32_t firstTick, int channel = 1, int velocity = 100)
{
    std::vector<FileNote> notes;
    for (int i = 0; i < count; ++i)
        notes.push_back ({ channel, 60 + cMajor[(first + i) % 7], velocity, firstTick + (uint32_t) i * 480, firstTick + (uint32_t) i * 480 + 240 });
    return notes;
}

// The same notes, on the same ticks (give or take one) and with the same velocity and channel
bool sameTakeNotes (const std::vector<FileNote>& got, const std::vector<FileNote>& want)
{
    const auto near = [] (uint32_t a, uint32_t b) { return a + 1 >= b && a <= b + 1; };
    bool ok = got.size() == want.size();
    for (size_t i = 0; ok && i < want.size(); ++i)
        ok = got[i].note == want[i].note && got[i].channel == want[i].channel && got[i].velocity == want[i].velocity
             && near (got[i].on, want[i].on) && near (got[i].off, want[i].off);

    if (! ok)
    {
        std::printf ("    got %zu notes, wanted %zu. first ones (note on-off), got vs wanted:\n", got.size(), want.size());
        for (size_t i = 0; i < std::max (got.size(), want.size()) && i < 10; ++i)
        {
            const auto text = [] (const std::vector<FileNote>& v, size_t n)
            { return n < v.size() ? std::to_string (v[n].note) + " " + std::to_string (v[n].on) + "-" + std::to_string (v[n].off) : std::string ("-"); };
            std::printf ("      %-18s %-18s\n", text (got, i).c_str(), text (want, i).c_str());
        }
    }
    return ok;
}

bool endsAtTick (const TakeFile& take, uint32_t tick) { return take.endTick + 1 >= tick && take.endTick <= tick + 1; }
} // namespace

int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf (stderr, "usage: %s <plugin.clap>\n", argv[0]);
        return 2;
    }

    // The plugin writes its takes in a folder of this test's own, so that the user's music folder is left alone. The folder
    // is not made here: the plugin makes it when it writes the first take.
    std::error_code ignored;
    const auto tempRoot = std::filesystem::temp_directory_path (ignored) / ("seqverse_test_" + std::to_string (getpid()));
    takeFolder = tempRoot / "takes";
    setenv ("SEQVERSE_TAKE_FOLDER", takeFolder.c_str(), 1);

    #if defined(_WIN32)
    HMODULE lib = (HMODULE) LOAD_LIBRARY(argv[1]);
    if (lib == nullptr)
    {
        std::fprintf (stderr, "LoadLibraryA failed: %lu\n", GetLastError());
        return 2;
    }

    auto* entry = reinterpret_cast<const clap_plugin_entry_t*> (GET_PROC_ADDRESS(lib, "clap_entry"));
#else
    void* lib = LOAD_LIBRARY(argv[1]);
    if (lib == nullptr)
    {
        std::fprintf (stderr, "dlopen failed: %s\n", dlerror());
        return 2;
    }

    auto* entry = static_cast<const clap_plugin_entry_t*> (GET_PROC_ADDRESS(lib, "clap_entry"));
#endif
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
        // the 44 scales, Bitwig could pick just two of them, so the reported span has to be one per choice. Since
        // clap-juce-extensions e286d28 (needed for JUCE 8) only booleans are flagged stepped: choices and integers
        // are continuous to the host over their real range, and the plugin rounds what it gets (see below).
        struct Span { const char* name; double span; bool mustBeStepped; };
        const Span spans[] = {
            { "Key", music::kNumKeys - 1, false },
            { "Scale", music::kNumScales - 1, false },
            { "Play Mode", music::numPlayModes - 1, false },
            { "Arp Direction", music::numArpDirections - 1, false },
            { "Octave Note", music::numOctaveNoteChoices - 1, false },
            { "Step Rate", music::kNumStepRates - 1, false },
            { "Note Length", music::kNumNoteLengths - 1, false },
            { "Chance", 100, false },
            { "Swing", 100, false },
            { "Low Octave", music::kMaxOctave - music::kMinOctave, false },
            { "Octaves", music::kMaxOctaves - 1, false },
            { "Velocity", 126, false },
            { "Velocity Variation", 64, false },
            { "Channel", 15, false },
            { "Cycle Length", music::kMaxCycleHalfBeats, false },
            { "Seed", 9999, false },
            { "Length Variation", 100, false },
            { "Rhythm", music::numRhythms - 1, false },
            { "Pattern Length", music::kMaxEuclidSteps - music::kMinEuclidSteps, false },
            { "Pulses", music::kMaxEuclidPulses, false },
            { "Rotation", music::kMaxEuclidRotation, false },
            { "Record", 1, true },
            { "Record Bars", take::kMaxBars, false },
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
    // Nothing changed from the defaults: C major, one octave with the octave note on top, so the arpeggio is eight notes
    std::printf ("default settings, transport playing from the start\n");
    {
        Scenario s;
        s.octaveNote = true;
        s.midiIn = { makeMidi (100, 0x90, 61, 100), makeMidi (5000, 0x80, 61, 0) }; // must be ignored
        const auto out = run (s);
        check (sameNotes (notesOf (out, 0x90), grid ({ 60, 62, 64, 65, 67, 69, 71, 72, 60 }, 0)),
               "note-ons: C D E F G A B C5 C on the 1/8th grid, up to the octave and over again, and no trace of the MIDI input");
        check (sameNotes (notesOf (out, 0x80), grid ({ 60, 62, 64, 65, 67, 69, 71, 72 }, step / 2)),
               "note-offs: each 1/16th after its note-on");
    }

    // The root an octave up ends the run: one octave of C major goes C4 to C5, and several octaves go up to the root
    // above the range. Random mode picks from the same pool.
    std::printf ("octave note\n");
    {
        Scenario s;
        s.octaveNote = true;
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 60, 62, 64, 65, 67, 69, 71, 72, 60 }, 0)), "Up: C D E F G A B C5, then round again");

        s.params = { { "Arp Direction", "Down" } };
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 72, 71, 69, 67, 65, 64, 62, 60, 72 }, 0)), "Down starts on the octave note: C5 B A G F E D C");

        s.params = { { "Arp Direction", "Up/Down" }, { "Note Length", "1/32" } };
        s.numBlocks = 330;
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62, 60 }, 0), true),
               "Up/Down turns at the octave note, and not at B");

        s = Scenario();
        s.octaveNote = true;
        s.params = { { "Arp Direction", "Thumb Up" } };
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 60, 62, 60, 64, 60, 65, 60, 67, 60 }, 0)), "Thumb Up still returns to the root between the notes");

        // Octaves 3 to 4 is 14 notes, and the root above the range is octave 5
        s.params = { { "Low Octave", "3" }, { "Octaves", "2" }, { "Arp Direction", "Down" } };
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 72, 71, 69, 67, 65, 64, 62, 60, 59 }, 0)),
               "over octaves 3 to 4 the top note is the root of octave 5: C5 B A G F E D C B");

        s.params = { { "Key", "D" }, { "Scale", "Natural Minor (Aeolian)" }, { "Arp Direction", "Down" } };
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 74, 72, 70, 69, 67, 65, 64, 62, 74 }, 0)), "in D minor the octave note is D5: D5 C Bb A G F E D");

        s = Scenario();
        s.octaveNote = true;
        s.params = { { "Play Mode", "Random" }, { "Scale", "Major Pentatonic" }, { "Note Length", "1/32" }, { "Seed", "42" } };
        s.numBlocks = 750;
        bool inPool = true, sawOctaveNote = false;
        for (const auto& n : notesOf (run (s), 0x90))
        {
            inPool &= n.note == 60 || n.note == 62 || n.note == 64 || n.note == 67 || n.note == 69 || n.note == 72;
            sawOctaveNote |= n.note == 72;
        }
        check (inPool && sawOctaveNote, "Random mode picks from the same pool, the octave note included");

        s = Scenario(); // Exclude, as before
        s.params = { { "Octave Note", "Exclude" } };
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 60, 62, 64, 65, 67, 69, 71, 60, 62 }, 0)), "Exclude: C D E F G A B, then C again");

        s.params = { { "Octave Note", "Include" } };
        s.rawParams = {};
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 60, 62, 64, 65, 67, 69, 71, 72, 60 }, 0)), "Include, picked by text");

        s = Scenario();
        s.rawParams = { { "Octave Note", 0 } };
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 60, 62, 64, 65, 67, 69, 71, 72, 60 }, 0)), "and by value, as a host sends it: 0 is Include");
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

            { music::downAndUp,       { 6, 5, 4, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6 } },
            { music::convergeDiverge, { 0, 6, 1, 5, 2, 4, 3, 3, 4, 2, 5, 1, 6, 0 } },
            { music::thumbUp,         { 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 1 } },
            { music::thumbDown,       { 0, 6, 0, 5, 0, 4, 0, 3, 0, 2, 0, 1, 0, 6 } },
            { music::pinkyUp,         { 6, 0, 6, 1, 6, 2, 6, 3, 6, 4, 6, 5, 6, 0 } },
            { music::pinkyDown,       { 6, 5, 6, 4, 6, 3, 6, 2, 6, 1, 6, 0, 6, 5 } },
            { music::upTwoBackOne,    { 0, 2, 1, 3, 2, 4, 3, 5, 4, 6, 5, 0, 2, 1 } },
            { music::skip,            { 0, 2, 4, 6, 1, 3, 5, 0, 2, 4, 6, 1, 3, 5 } },
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

        // Whatever the size of the pool (one octave of a pentatonic scale, or several octaves of a chromatic one), every
        // direction stays inside it, plays every note of it, and is the same at every step number, also before step 0
        int wrong = 0;
        for (int direction = 0; direction < music::numArpDirections; ++direction)
            for (int size = 1; size <= 96; ++size)
            {
                std::vector<bool> seen ((size_t) size, false);
                bool ok = true;
                for (int64_t step = -3 * size; step < 5 * size; ++step)
                {
                    const int index = music::arpIndex (direction, size, step);
                    ok &= index >= 0 && index < size;
                    if (index >= 0 && index < size)
                        seen[(size_t) index] = true;
                }
                ok &= std::find (seen.begin(), seen.end(), false) == seen.end();
                wrong += ok ? 0 : 1;
                if (! ok)
                    std::printf ("    %s with %d notes\n", music::arpDirectionNames[direction], size);
            }
        check (wrong == 0, "every direction, for pools of 1 to 96 notes, stays inside the pool and plays all of it");
    }

    // Sessions saved before there was a state version stored every parameter as a fraction of its range, and the arp
    // direction list had 7 entries: 0.5 is the fourth, Down/Up, and has to stay Down/Up now that the list is longer.
    // (The format of the blob is JUCE's: a magic number, the length, and the XML text.)
    std::printf ("sessions saved by an earlier version\n");
    {
        const auto blobOf = [] (const std::string& xml)
        {
            std::vector<uint8_t> blob;
            for (uint32_t word : { 0x21324356u, (uint32_t) xml.size() })
                for (int i = 0; i < 4; ++i)
                    blob.push_back ((uint8_t) (word >> (8 * i)));
            blob.insert (blob.end(), xml.begin(), xml.end());
            return blob;
        };

        Scenario s;
        s.savedState = blobOf ("<SeqVerse arpDirection=\"0.5\"/>");
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 71, 69, 67, 65, 64, 62, 60, 62, 64 }, 0)),
               "arp direction 0.5 in an old session is still Down/Up: B A G F E D C D E");

        // chance 0.5 of 0..100 is 50, and seed 0.0005 of 0..9999 is 5
        s.savedState = blobOf ("<SeqVerse chance=\"0.5\" seed=\"0.0005\"/>");
        Scenario same;
        same.params = { { "Chance", "50" }, { "Seed", "5" } };
        const auto fromOldSession = notesOf (run (s), 0x90);
        check (! fromOldSession.empty() && fromOldSession.size() < 9 && sameNotes (fromOldSession, notesOf (run (same), 0x90)),
               "chance and seed in an old session are read as fractions of their range: 50 % and 5");
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

    std::printf ("MIDI Root\n");
    {
        // Off (the default): the note in is still not passed on, and it does not change the key either
        Scenario s;
        s.midiIn = { makeMidi (0, 0x90, 64, 100) }; // E, at the very first sample
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 60, 62, 64, 65, 67, 69, 71, 60, 62 }, 0)),
               "MIDI Root off: an incoming note does not change the key");

        // On, and a note received before the first block it plays on: the root latches to its pitch class, not Key
        s.rawParams = { { "MIDI Root", 1 } };
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 64, 66, 68, 69, 71, 73, 75, 64, 66 }, 0)),
               "MIDI Root on: E in latches E major, in place of C major");

        // On, but nothing has been received yet: Key is used until the first note
        s.midiIn.clear();
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 60, 62, 64, 65, 67, 69, 71, 60, 62 }, 0)),
               "MIDI Root on, no note received: falls back to Key");

        // A note-on with velocity 0 (a note-off in running status) must not move the latch
        s.midiIn = { makeMidi (0, 0x90, 64, 100), makeMidi (100, 0x90, 60, 0) }; // E on, then a "note on" at velocity 0 for C
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 64, 66, 68, 69, 71, 73, 75, 64, 66 }, 0)),
               "a note-on with velocity 0 does not move the latch");
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

        // Choices are not stepped to the host, so it can send a value between two indices (a knob in its parameter
        // list, a curve of automation): the nearest choice is taken, not the one below
        s.rawParams = { { "Key", 1.6 }, { "Scale", 4.6 } };
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 62, 63, 65, 67, 69, 70, 72, 62, 63 }, 0)), "Key 1.6 and Scale 4.6 round up to D Phrygian");
        s.rawParams = { { "Key", 2.4 }, { "Scale", 5.4 } };
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 62, 63, 65, 67, 69, 70, 72, 62, 63 }, 0)), "Key 2.4 and Scale 5.4 round down to it");
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
        s.params = { { "Low Octave", "3" }, { "Octaves", "3" } };
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 48, 50, 52, 53, 55, 57, 59, 60, 62 }, 0)),
               "octaves 3 to 5: the arp starts in octave 3 and climbs into 4");

        // Octaves that reach past the top are cut off there: from octave 6, four octaves are just 6 and 7
        s.params = { { "Low Octave", "6" }, { "Octaves", "4" }, { "Arp Direction", "Down" } };
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 107, 105, 103, 101, 100, 98, 96, 95, 93 }, 0)),
               "four octaves from octave 6, going down: the top is B7, cut off at octave 7");

        // The most a host can ask for, sent by value: the top note is the root above octave 7, still a valid note
        s = Scenario();
        s.octaveNote = true;
        s.rawParams = { { "Low Octave", 7 }, { "Octaves", 8 }, { "Arp Direction", 1 } };
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 108, 107, 105, 103 }, 0), true), "octave 7 with 8 octaves, going down: from C8, the root above the range");
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
    std::printf ("note length variation\n");
    {
        // Each note's length: the distance from its note-on to the next note-off. 1/8 steps and 1/16th notes (6000
        // samples) never overlap, so the n-th note-off belongs to the n-th note-on.
        const auto lengths = [] (const std::vector<clap_event_midi_t>& events)
        {
            const auto ons = notesOf (events, 0x90);
            const auto offs = notesOf (events, 0x80);
            std::vector<uint32_t> result;
            for (size_t i = 0; i < ons.size() && i < offs.size(); ++i)
                result.push_back (offs[i].time - ons[i].time);
            return result;
        };

        Scenario s;
        s.numBlocks = 750; // 32 steps
        auto out = run (s);
        auto len = lengths (out);
        check (! len.empty() && std::all_of (len.begin(), len.end(), [] (uint32_t l) { return l >= 5999 && l <= 6001; }), "no variation (the default): every note is 6000 samples");

        s.params = { { "Length Variation", "100" }, { "Seed", "5" } };
        out = run (s);
        len = lengths (out);
        check (len.size() == 32 && notesOf (out, 0x90).size() == notesOf (out, 0x80).size(), "every note has its note-off");
        check (std::all_of (len.begin(), len.end(), [] (uint32_t l) { return l >= 590 && l <= 6001; }), "100 %: no note is longer than the note length, or shorter than a tenth of it");
        check (*std::min_element (len.begin(), len.end()) < 3000 && *std::max_element (len.begin(), len.end()) > 4500, "and they spread over that range");

        s.params = { { "Length Variation", "40" }, { "Seed", "5" } };
        len = lengths (run (s));
        check (std::all_of (len.begin(), len.end(), [] (uint32_t l) { return l >= 3599 && l <= 6001; }), "40 %: between 60 % and all of the length");
        check (lengths (run (s)) == len, "a fixed seed gives the same lengths again");

        s.params = { { "Length Variation", "40" }, { "Seed", "6" } };
        check (lengths (run (s)) != len, "and another seed other lengths");

        // The length is the last of the step's random draws: what plays, when and how hard does not depend on it
        Scenario without;
        without.params = { { "Play Mode", "Random" }, { "Chance", "60" }, { "Velocity Variation", "20" }, { "Scale", "Chromatic" }, { "Seed", "9" } };
        without.numBlocks = 750;
        Scenario with = without;
        with.params.push_back ({ "Length Variation", "80" });
        const auto plain = notesOf (run (without), 0x90);
        const auto varied = notesOf (run (with), 0x90);
        bool same = plain.size() == varied.size() && ! plain.empty();
        for (size_t i = 0; same && i < plain.size(); ++i)
            same = plain[i].note == varied[i].note && plain[i].time == varied[i].time && plain[i].velocity == varied[i].velocity;
        check (same, "the notes, their times and velocities are the same with and without length variation");

        // With a cycle and the seed on Auto, every cycle has the same lengths as the one before
        Scenario cycled;
        cycled.params = { { "Length Variation", "100" }, { "Cycle Length", "4" } };
        cycled.numBlocks = 563;
        const auto cycledLengths = lengths (run (cycled));
        bool repeats = cycledLengths.size() >= 17;
        for (size_t i = 0; repeats && i + 8 < cycledLengths.size(); ++i)
            repeats = cycledLengths[i] + 1 >= cycledLengths[i + 8] && cycledLengths[i] <= cycledLengths[i + 8] + 1;
        check (repeats, "with a cycle of eight steps every cycle has the lengths of the one before");
    }

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
        s.params = { { "Cycle Length", "Off" } };
        check (sameNotes (notesOf (run (s), 0x90), cycledArp (9, 7)), "Off plays as if there was no cycle");

        s.params = { { "Cycle Length", "Endless" } }; // what it was called before
        check (sameNotes (notesOf (run (s), 0x90), cycledArp (9, 7)), "the old name Endless is still understood when typed");

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

    // The Euclidean rhythm: `Pulses` spread over `Steps` steps, repeating freely on the step grid. With the defaults (8
    // steps, 5 pulses) the pattern is x.xx.xx., and with 3 pulses x..x..x. Notes are C D E F G A B C D ... going up, one
    // for each onset. A step is 12000 samples, so eight of them are a bar.
    std::printf ("euclidean rhythm\n");
    {
        const auto at = [] (std::vector<int> steps, std::vector<int> notes)
        {
            std::vector<Note> result;
            for (size_t i = 0; i < steps.size(); ++i)
                result.push_back ({ notes[i], (uint32_t) steps[i] * step });
            return result;
        };

        Scenario s;
        s.params = { { "Rhythm", "Euclidean" } };
        s.numBlocks = 370; // two bars: 16 steps, the first step of the third bar is not reached
        check (sameNotes (notesOf (run (s), 0x90), at ({ 0, 2, 3, 5, 6, 8, 10, 11, 13, 14 }, { 60, 62, 64, 65, 67, 69, 71, 60, 62, 64 })),
               "5 pulses in 8 steps (the defaults): x.xx.xx., the arpeggio counting onsets, not steps");

        s.params = { { "Rhythm", "Euclidean" }, { "Pulses", "3" } };
        check (sameNotes (notesOf (run (s), 0x90), at ({ 0, 3, 6, 8, 11, 14 }, { 60, 62, 64, 65, 67, 69 })), "3 pulses: x..x..x.");

        s.params = { { "Rhythm", "Euclidean" }, { "Pulses", "3" }, { "Rotation", "1" } };
        check (sameNotes (notesOf (run (s), 0x90), at ({ 1, 4, 7, 9, 12, 15 }, { 60, 62, 64, 65, 67, 69 })), "rotation 1 moves the pattern one step later");

        s.params = { { "Rhythm", "Euclidean" }, { "Pulses", "3" }, { "Rotation", "9" } };
        check (sameNotes (notesOf (run (s), 0x90), at ({ 1, 4, 7, 9, 12, 15 }, { 60, 62, 64, 65, 67, 69 })), "rotation wraps round: 9 is 1 with eight steps");

        s.params = { { "Rhythm", "Euclidean" }, { "Pulses", "0" } };
        check (notesOf (run (s), 0x90).empty(), "0 pulses: silence");

        // The length is a setting of its own and free of the bar: 7 steps, one pulse, is a note every 7 steps
        s.params = { { "Rhythm", "Euclidean" }, { "Pattern Length", "7" }, { "Pulses", "1" } };
        check (sameNotes (notesOf (run (s), 0x90), at ({ 0, 7, 14 }, { 60, 62, 64 })), "7 steps, 1 pulse: a note every 7 steps, drifting against the bar");

        // Three in seven is x.x.x.. in some rotation: 7 onsets in 16 steps (the third repeat has begun), all two or three steps apart
        s.params = { { "Rhythm", "Euclidean" }, { "Pattern Length", "7" }, { "Pulses", "3" } };
        {
            const auto ons = notesOf (run (s), 0x90);
            bool evenlySpread = ons.size() == 7;
            for (size_t i = 1; i < ons.size(); ++i)
            {
                const uint32_t gap = (ons[i].time + 1 - ons[i - 1].time) / step; // +1: the times are within a sample
                evenlySpread &= gap == 2 || gap == 3;
            }
            check (evenlySpread && ons[0].time == 0, "7 steps, 3 pulses: seven onsets in 16 steps, two or three steps apart, starting on one");
        }

        // The arp direction is followed. The pattern is where it was, only which note is played changes.
        s = Scenario();
        s.params = { { "Rhythm", "Euclidean" }, { "Pulses", "3" }, { "Arp Direction", "Down" } };
        s.numBlocks = 190;
        check (sameNotes (notesOf (run (s), 0x90), at ({ 0, 3, 6, 8 }, { 71, 69, 67, 65 })), "the arp direction is followed: down");

        // Values a host can send that a knob would not: more pulses than steps plays every step
        s = Scenario();
        s.rawParams = { { "Rhythm", 1 }, { "Pattern Length", 4 }, { "Pulses", 20 } };
        s.numBlocks = 100;
        check (sameNotes (notesOf (run (s), 0x90), at ({ 0, 1, 2, 3, 4 }, { 60, 62, 64, 65, 67 })), "20 pulses in 4 steps, set by a host: every step plays");

        s.rawParams = { { "Rhythm", 1 }, { "Pattern Length", 6 }, { "Pulses", 3 }, { "Rotation", 31 } };
        s.numBlocks = 190;
        check (sameNotes (notesOf (run (s), 0x90), at ({ 1, 3, 5, 7 }, { 60, 62, 64, 65 })), "rotation 31 with 6 steps wraps round to 1: .x.x.x");

        // With a cycle the pattern starts over in every cycle, and so does the arpeggio. 2 beats are four 1/8 steps: the
        // pattern of 8 steps is cut short after four (x..x), and again in the next cycle.
        s = Scenario();
        s.params = { { "Rhythm", "Euclidean" }, { "Pulses", "3" }, { "Cycle Length", "2" } };
        s.numBlocks = 190;
        check (sameNotes (notesOf (run (s), 0x90), at ({ 0, 3, 4, 7, 8 }, { 60, 62, 60, 62, 60 })),
               "cycle length 2 beats: the pattern of eight steps is cut short after four, and starts over with the arpeggio");

        // A pattern of the same length as the cycle just fits: 3 pulses in 4 steps
        s.params = { { "Rhythm", "Euclidean" }, { "Pattern Length", "4" }, { "Pulses", "3" }, { "Cycle Length", "2" } };
        check (sameNotes (notesOf (run (s), 0x90), at ({ 0, 1, 2, 4, 5, 6, 8 }, { 60, 62, 64, 60, 62, 64, 60 })), "4 steps in a cycle of 4 steps: 3 pulses are xxx.");

        // 4.5 beats are nine steps, so an 8 step pattern gets a second onset at its ninth step (position 0 again) in
        // each cycle, and the arpeggio counts the onsets in the cycle
        s = Scenario();
        s.params = { { "Rhythm", "Euclidean" }, { "Pulses", "1" }, { "Cycle Length", "4.5" } };
        s.numBlocks = 422;
        check (sameNotes (notesOf (run (s), 0x90), at ({ 0, 8, 9, 17, 18 }, { 60, 62, 60, 62, 60 })),
               "cycle of 4.5 beats, 8 steps and 1 pulse: onsets at steps 0 and 8 of each cycle, the arpeggio starting over");

        // 1/4T steps are 2/3 of a beat: two in a 1 beat cycle, the second one cut short. One pulse plays the first only.
        s = Scenario();
        s.params = { { "Rhythm", "Euclidean" }, { "Pulses", "1" }, { "Cycle Length", "1" }, { "Step Rate", "1/4T" } };
        s.numBlocks = 150;
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 60, 60, 60, 60 }, 0, samplesPerQuarter)), "1/4T steps in a 1 beat cycle: the pulse is on the first");

        // The step rate is the pattern's step: 1/4 steps, 4 of them, one pulse is a note every four beats
        s = Scenario();
        s.params = { { "Rhythm", "Euclidean" }, { "Pattern Length", "4" }, { "Pulses", "1" }, { "Step Rate", "1/4" } };
        s.numBlocks = 370;
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 60, 62 }, 0, 4 * samplesPerQuarter)), "1/4 steps, 4 steps and 1 pulse: a note every four beats");

        // Joining in the middle of a pattern: the pattern is where the timeline is, and so is the arpeggio (onset number 2 is E)
        s = Scenario();
        s.params = { { "Rhythm", "Euclidean" }, { "Pulses", "3" } };
        s.startQuarters = 2.0; // step 4 of the bar
        s.numBlocks = 90;
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 64 }, 2 * step)), "starting at step 4: the next onset is step 6, the third pulse");

        // Chance and swing work on the onsets
        s = Scenario();
        s.params = { { "Rhythm", "Euclidean" }, { "Pulses", "3" }, { "Swing", "100" } };
        s.numBlocks = 190;
        check (sameNotes (notesOf (run (s), 0x90), { { 60, 0 }, { 62, 3 * step + step / 3 }, { 64, 6 * step }, { 65, 8 * step } }),
               "swing delays the onset on an odd step (step 3), not the ones on even steps");

        s = Scenario();
        s.params = { { "Rhythm", "Euclidean" }, { "Pulses", "5" }, { "Chance", "50" }, { "Seed", "3" }, { "Play Mode", "Random" } };
        s.numBlocks = 1500; // eight bars
        const auto ons = notesOf (run (s), 0x90);
        bool onOnsets = true;
        for (const auto& n : ons)
        {
            const int stepInBar = (int) ((n.time + step / 2) / step) % 8;
            onOnsets &= stepInBar == 0 || stepInBar == 2 || stepInBar == 3 || stepInBar == 5 || stepInBar == 6;
        }
        check (! ons.empty() && ons.size() < 40 && onOnsets, "chance 50 % and random notes: only some of the onsets play, and nothing else");

        s = Scenario();
        s.params = { { "Rhythm", "Euclidean" }, { "Pulses", "3" }, { "Chance", "0" } };
        check (notesOf (run (s), 0x90).empty(), "chance 0: silence");

        s = Scenario();
        s.rawParams = { { "Rhythm", 1 }, { "Pulses", 3 } };
        s.numBlocks = 100;
        check (sameNotes (notesOf (run (s), 0x90), at ({ 0, 3 }, { 60, 62 })), "picked by index, as a host sends them: Euclidean, 3 pulses");

        // 3 pulses in 6 steps, one step later: .x.x.x, so every odd step, and a seven note scale comes round to C again
        s = Scenario();
        s.params = { { "Rhythm", "Euclidean" }, { "Pattern Length", "6" }, { "Pulses", "3" }, { "Rotation", "1" } };
        s.numBlocks = 370;
        s.roundTripState = true;
        check (sameNotes (notesOf (run (s), 0x90), at ({ 1, 3, 5, 7, 9, 11, 13, 15 }, { 60, 62, 64, 65, 67, 69, 71, 60 })),
               "rhythm, steps, pulses and rotation come back after saving and loading");
    }

    std::printf ("saved state\n");
    {
        Scenario s;
        s.params = { { "Key", "E" }, { "Scale", "Dorian" } };
        s.roundTripState = true;
        check (sameNotes (notesOf (run (s), 0x90), grid ({ 64, 66, 67, 69, 71, 73, 74, 64, 66 }, 0)),
               "key and scale come back after loading: E dorian");

        s.params = { { "Step Rate", "1/4" }, { "Low Octave", "3" }, { "Channel", "3" }, { "Velocity", "77" } };
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

    std::printf ("recording a take\n");
    {
        // Two bars of 4/4 are eight beats, and the plugin plays a 1/8th every half beat: sixteen notes
        Scenario s;
        s.rawParams = { { "Record", 1 } };
        s.params = { { "Record Bars", "2" } };
        s.numBlocks = 600; // longer than the two bars, which are 375 blocks
        auto take = (run (s), readTake());
        check (take.files == 1 && take.wellFormed, "one MIDI file was written, and it reads back");
        check (take.division == 960 && take.microsecondsPerQuarter == 0 && take.tsigNumerator == 0 && take.tsigDenominator == 0,
               "960 ticks per quarter, and no tempo or time signature in the file (Bitwig would ask whether to import them)");
        check (sameTakeNotes (take.notes, takeGrid (16, 0, 0)), "2 bars: the sixteen notes of the first two bars, as they were played");
        check (endsAtTick (take, 7680), "and the clip ends after exactly two bars");

        s.params = { { "Record Bars", "1" } };
        take = (run (s), readTake());
        check (sameTakeNotes (take.notes, takeGrid (8, 0, 0)) && endsAtTick (take, 3840), "1 bar: eight notes, ending after four beats");

        // The transport stops after 100 blocks, 2.13 beats: five notes have started, and the fifth is ended by the stop
        s.numBlocks = 300;
        s.stopBlock = 100;
        s.params = { { "Record Bars", "4" } };
        take = (run (s), readTake());
        auto want = takeGrid (5, 0, 0);
        want[4].off = 2048;
        check (sameTakeNotes (take.notes, want) && endsAtTick (take, 2048), "stopped after 2.1 beats of 4 bars: only what was played, the last note ended by the stop");

        s.params = { { "Record Bars", "No limit" } };
        s.numBlocks = 300;
        take = (run (s), readTake());
        check (sameTakeNotes (take.notes, want) && endsAtTick (take, 2048), "No limit: what was played until the stop");

        s.params = { { "Record Bars", "No limit" } };
        s.stopBlock = neverStop;
        s.numBlocks = 400;
        take = (run (s), readTake());
        check (take.files == 0, "No limit and no stop: nothing is written until the transport stops");

        // A loop: the transport jumps back after 100 blocks, and the take ends there
        s = Scenario();
        s.rawParams = { { "Record", 1 } };
        s.params = { { "Record Bars", "4" } };
        s.numBlocks = 400;
        s.jumpBlock = 100;
        s.jumpToQuarters = 0.0;
        take = (run (s), readTake());
        check (sameTakeNotes (take.notes, want) && endsAtTick (take, 2048), "the transport jumps back after 2.1 beats: the take ends there and the loop is not added to it");

        // Joining the song at beat 3: the take counts from where it began, and the arpeggio is where it would be (step 6)
        s = Scenario();
        s.rawParams = { { "Record", 1 } };
        s.params = { { "Record Bars", "1" } };
        s.startQuarters = 3.0;
        s.numBlocks = 400;
        take = (run (s), readTake());
        check (sameTakeNotes (take.notes, takeGrid (8, 6, 0)) && endsAtTick (take, 3840), "started at beat 3 of the song: the take starts at 0, with the notes of that place");

        // Not a whole number of bars from a step: the first step is at the play position plus a bit
        s = Scenario();
        s.rawParams = { { "Record", 1 } };
        s.params = { { "Record Bars", "1" } };
        s.startQuarters = 0.2;
        s.numBlocks = 400;
        take = (run (s), readTake());
        want = takeGrid (8, 1, 288);
        want[7].off = 3840; // it would have ended after the take
        check (sameTakeNotes (take.notes, want) && endsAtTick (take, 3840),
               "started 0.2 beats before a step: the first note is 0.3 beats into the take, and the last is ended with it");

        // 3/4 has three quarters in a bar: two bars are six beats, or twelve notes
        s = Scenario();
        s.rawParams = { { "Record", 1 } };
        s.params = { { "Record Bars", "2" } };
        s.tsigNumerator = 3;
        s.numBlocks = 500; // 281 blocks are two bars of 3/4
        take = (run (s), readTake());
        check (sameTakeNotes (take.notes, takeGrid (12, 0, 0)) && endsAtTick (take, 5760),
               "3/4: two bars are six beats");

        s.tsigNumerator = 6;
        s.tsigDenominator = 8;
        s.params = { { "Record Bars", "2" } };
        take = (run (s), readTake());
        check (endsAtTick (take, 5760), "6/8 is three quarters to the bar too");

        // The notes are kept as they were played: channel and velocity
        s = Scenario();
        s.rawParams = { { "Record", 1 } };
        s.params = { { "Record Bars", "1" }, { "Channel", "3" }, { "Velocity", "77" } };
        s.numBlocks = 300;
        take = (run (s), readTake());
        check (sameTakeNotes (take.notes, takeGrid (8, 0, 0, 3, 77)), "channel 3 and velocity 77 are in the file");

        // Swing and a Euclidean rhythm are as played: 3 pulses in 8 steps are steps 0, 3 and 6, and the odd step 3 is
        // swung 50 % of a third of a step (80 ticks) late
        s = Scenario();
        s.rawParams = { { "Record", 1 } };
        s.params = { { "Record Bars", "1" }, { "Swing", "50" }, { "Rhythm", "Euclidean" }, { "Pulses", "3" } };
        s.numBlocks = 300;
        take = (run (s), readTake());
        check (sameTakeNotes (take.notes, { { 1, 60, 100, 0, 240 }, { 1, 62, 100, 1520, 1760 }, { 1, 64, 100, 2880, 3120 } }),
               "a Euclidean rhythm and swing come out in the file as they were played");

        // Nothing is recorded when it is not armed, when nothing plays, or when the transport is stopped
        s = Scenario();
        s.params = { { "Record Bars", "2" } };
        check ((run (s), readTake()).files == 0, "Record off: no file");

        s.rawParams = { { "Record", 1 } };
        s.params = { { "Record Bars", "2" }, { "Chance", "0" } };
        check ((run (s), readTake()).files == 0, "a take with no notes is not kept");

        s.params = { { "Record Bars", "2" } };
        s.playing = false;
        check ((run (s), readTake()).files == 0, "a stopped transport records nothing");

        s.playing = true;
        s.sendTransport = false;
        check ((run (s), readTake()).files == 0, "and neither does a host that sends no transport");

        // Record is one-shot: a start after the first take has ended does not record a second one
        s = Scenario();
        s.rawParams = { { "Record", 1 } };
        s.params = { { "Record Bars", "1" } };
        s.stopBlock = 100;
        s.restartBlock = 200; // at 4.27 beats, so the first step of the second take is step 9 at 4.5
        s.numBlocks = 500;
        run (s);
        const auto both = takeFiles();
        bool firstKept = false;
        for (const auto& file : both)
        {
            const auto t = readTakeFile (file);
            firstKept |= t.notes.size() == 5 && endsAtTick (t, 2048);
        }
        check (both.size() == 1 && firstKept, "started again after a stop: Record was switched off by the first take, so only that one is a file");

        // The name says when, and what was played
        s = Scenario();
        s.rawParams = { { "Record", 1 } };
        s.params = { { "Record Bars", "1" } };
        s.numBlocks = 300;
        take = (run (s), readTake());
        const std::regex plain ("SeqVerse_[0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}-[0-9]{2}-[0-9]{2}_C-Major-Ionian_Up_1-8_1bar\\.mid");
        check (std::regex_match (take.name, plain), "the name: the date and time, then key, scale, direction, step rate and length");
        check (onlySafeCharacters (take.name), "the name has only letters, digits, - and _ (no space, #, parenthesis)");

        s.numBlocks = 500; // two bars are 375 blocks
        s.params = { { "Record Bars", "2" }, { "Key", "F#/Gb" }, { "Scale", "Dorian" }, { "Play Mode", "Random" }, { "Step Rate", "1/16T" },
                     { "Rhythm", "Euclidean" }, { "Pulses", "5" }, { "Pattern Length", "12" } };
        take = (run (s), readTake());
        check (take.name.find ("_Fsharp-Dorian_Random_1-16T_Euclid-5-in-12_2bars.mid") != std::string::npos && onlySafeCharacters (take.name),
               "another take: F sharp, Dorian, Random, triplets, Euclid 5 in 12, 2 bars, and nothing but letters, digits, - and _ in the name");

        s = Scenario();
        s.rawParams = { { "Record", 1 } };
        s.params = { { "Record Bars", "4" }, { "Arp Direction", "Up/Down" } };
        s.numBlocks = 300;
        s.stopBlock = 100;
        take = (run (s), readTake());
        check (take.name.find ("_Up-Down_1-8_0-5bars.mid") != std::string::npos, "a take that was stopped early says how long it was: 0.5 bars");

        // Record is one-shot: it is unarmed again once a take is done, however it ended
        s = Scenario();
        s.rawParams = { { "Record", 1 } };
        s.params = { { "Record Bars", "1" } };
        s.readRecordAtEnd = true;
        s.numBlocks = 300; // the bar is 188 blocks: the take ends by its length, and the transport is still playing
        run (s);
        check (recordAtEnd == 0.0, "Record is off again after a take of its bars");

        s.params = { { "Record Bars", "No limit" } };
        s.stopBlock = 100;
        run (s);
        check (recordAtEnd == 0.0, "and after a take that the stop ended");

        s.params = { { "Record Bars", "No limit" }, { "Chance", "0" } };
        run (s);
        check (recordAtEnd == 0.0, "and after a take without notes, which is not kept");

        s = Scenario();
        s.params = { { "Record Bars", "1" } };
        s.readRecordAtEnd = true;
        s.numBlocks = 300;
        run (s);
        check (recordAtEnd == 0.0, "Record that was never armed stays off");

        s = Scenario();
        s.rawParams = { { "Record", 1 } };
        s.params = { { "Record Bars", "1" } };
        s.readRecordAtEnd = true;
        s.playing = false;
        s.numBlocks = 300;
        run (s);
        check (recordAtEnd == 1.0, "Record stays armed when the host never plays");

        // Only the newest five are kept, the others of the same kind are deleted, and other files are not touched
        s = Scenario();
        s.rawParams = { { "Record", 1 } };
        s.numBlocks = 1400;
        clearTakes();
        std::filesystem::create_directories (takeFolder);
        { std::ofstream decoy (takeFolder / "My riff.mid"); decoy << "not a take"; }
        s.keepTakes = true;
        std::string bars[7] = { "1", "2", "3", "4", "5", "6", "7" };
        for (int i = 0; i < 7; ++i)
        {
            s.params = { { "Record Bars", bars[i].c_str() } };
            run (s);
            usleep (20 * 1000); // the files are told apart by the time they were written
        }
        const auto kept = takeFiles();
        std::vector<uint32_t> ends;
        for (const auto& file : kept)
            ends.push_back (readTakeFile (file).endTick);
        std::sort (ends.begin(), ends.end());
        check (kept.size() == 5 && ends == std::vector<uint32_t> { 3 * 3840, 4 * 3840, 5 * 3840, 6 * 3840, 7 * 3840 },
               "after seven takes the five newest are left: the takes of 3 to 7 bars");
        check (std::filesystem::exists (takeFolder / "My riff.mid"), "a file that is not named like a take is left alone");
    }

    std::printf ("all blocks\n");
    check (everyBlockOk, "process() never returned an error");
    check (everyBlockSilent, "the audio output was always silent");

    entry->deinit();
    dlclose (lib);
    std::filesystem::remove_all (tempRoot, ignored);

    std::printf ("%s\n", failures == 0 ? "PASS" : "FAILED");
    return failures == 0 ? 0 : 1;
}
