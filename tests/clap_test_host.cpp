// Minimal headless CLAP host for scripted tests: loads a .clap, runs a fixed MIDI scenario through
// process() on the calling thread and checks what comes out. No GUI, no audio device, no Bitwig.
//
//   clap_test_host <path/to/Plugin.clap>      exit code 0 = all checks passed
//
// The plugin is loaded straight from the given path, never installed, so ~/.clap is left alone.

#include <clap/clap.h>

#include <dlfcn.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
constexpr uint32_t sampleRate = 48000;
constexpr uint32_t blockSize = 512;
constexpr uint32_t numBlocks = 192; // 98304 samples, past the note-off at 96010

int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok)
        ++failures;
}

const clap_host_t host = {
    CLAP_VERSION, nullptr, "clap_test_host", "generative_midi", nullptr, "0.1",
    [] (const clap_host_t*, const char*) -> const void* { return nullptr; }, // get_extension
    [] (const clap_host_t*) {},                                              // request_restart
    [] (const clap_host_t*) {},                                              // request_process
    [] (const clap_host_t*) {},                                              // request_callback
};

clap_event_midi makeMidi (uint32_t time, uint8_t b0, uint8_t b1, uint8_t b2)
{
    clap_event_midi e {};
    e.header.size = sizeof (e);
    e.header.time = time;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = CLAP_EVENT_MIDI;
    e.port_index = 0;
    e.data[0] = b0;
    e.data[1] = b1;
    e.data[2] = b2;
    return e;
}

// Events for the current block; the plugin pulls them through the input_events callbacks.
struct Scenario
{
    std::vector<clap_event_midi> events;
    std::vector<clap_event_midi> received; // everything the plugin emitted, block time made absolute
    uint32_t blockStart = 0;
};

Scenario scenario;

uint32_t inSize (const clap_input_events_t*) { return (uint32_t) scenario.events.size(); }

const clap_event_header_t* inGet (const clap_input_events_t*, uint32_t i)
{
    return i < scenario.events.size() ? &scenario.events[i].header : nullptr;
}

bool outTryPush (const clap_output_events_t*, const clap_event_header_t* h)
{
    if (h->type == CLAP_EVENT_MIDI)
    {
        auto e = *reinterpret_cast<const clap_event_midi_t*> (h);
        e.header.time += scenario.blockStart;
        scenario.received.push_back (e);
    }
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

    auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    if (factory == nullptr || factory->get_plugin_count (factory) < 1)
    {
        std::fprintf (stderr, "no plugin in factory\n");
        return 2;
    }

    const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor (factory, 0);
    std::printf ("plugin: %s (%s)\n", desc->name, desc->id);

    const clap_plugin_t* plugin = factory->create_plugin (factory, &host, desc->id);
    check (plugin != nullptr && plugin->init (plugin), "create + init");
    if (plugin == nullptr)
        return 1;

    auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*> (plugin->get_extension (plugin, CLAP_EXT_AUDIO_PORTS));
    check (audioPorts != nullptr && audioPorts->count (plugin, true) == 0 && audioPorts->count (plugin, false) == 1,
           "audio ports: no input, one output");

    check (plugin->activate (plugin, sampleRate, 1, blockSize), "activate");
    check (plugin->start_processing (plugin), "start_processing");

    // The host sends no transport, so the plugin runs at its 120 BPM fallback: an 8th is sampleRate / 4 samples.
    // Scenario (absolute sample times): C4 held for eight 8ths, and a CC in between that must pass through.
    constexpr uint32_t eighth = sampleRate / 4;
    const uint32_t noteOnTime = 10;
    const std::vector<clap_event_midi> script = {
        makeMidi (noteOnTime, 0x90, 60, 100),
        makeMidi (blockSize + 5, 0xB0, 1, 64),
        makeMidi (noteOnTime + 8 * eighth, 0x80, 60, 0),
    };

    std::vector<float> left (blockSize), right (blockSize);
    float* channels[2] = { left.data(), right.data() };
    bool silent = true;
    bool processedOk = true;

    for (uint32_t block = 0; block < numBlocks; ++block)
    {
        scenario.blockStart = block * blockSize;
        scenario.events.clear();
        for (auto e : script)
            if (e.header.time >= scenario.blockStart && e.header.time < scenario.blockStart + blockSize)
            {
                e.header.time -= scenario.blockStart; // block-relative for the plugin
                scenario.events.push_back (e);
            }

        std::fill (left.begin(), left.end(), 1.0f); // poison, so an unwritten buffer is caught
        std::fill (right.begin(), right.end(), 1.0f);

        clap_audio_buffer_t out {};
        out.channel_count = 2;
        out.data32 = channels;

        const clap_input_events_t in { nullptr, inSize, inGet };
        const clap_output_events_t outEvents { nullptr, outTryPush };

        clap_process_t p {};
        p.steady_time = scenario.blockStart;
        p.frames_count = blockSize;
        p.audio_outputs = &out;
        p.audio_outputs_count = 1;
        p.in_events = &in;
        p.out_events = &outEvents;

        processedOk &= plugin->process (plugin, &p) != CLAP_PROCESS_ERROR;
        for (uint32_t i = 0; i < blockSize; ++i)
            silent &= left[i] == 0.0f && right[i] == 0.0f;
    }

    check (processedOk, "process() returned no error");
    check (silent, "audio output is silent");

    // Sort what came out. The trigger note itself must be gone, so anything that is a note is generated.
    std::vector<clap_event_midi> ons, offs, others;
    for (const auto& e : scenario.received)
    {
        const uint8_t status = e.data[0] & 0xF0;
        (status == 0x90 ? ons : status == 0x80 ? offs : others).push_back (e);
    }

    // C4 held for eight 8ths: C D E F G A B C5, each on the 8th grid, sounding for 90% of an 8th.
    const uint8_t expectedNotes[8] = { 60, 62, 64, 65, 67, 69, 71, 72 };
    const uint32_t gate = eighth * 9 / 10;
    auto near = [] (uint32_t a, uint32_t b) { return a + 1 >= b && a <= b + 1; };

    bool onsOk = ons.size() == 8;
    bool offsOk = offs.size() == 8;
    for (uint32_t k = 0; k < 8 && onsOk && offsOk; ++k)
    {
        const uint32_t start = noteOnTime + k * eighth;
        const bool onOk = ons[k].data[0] == 0x90 && ons[k].data[1] == expectedNotes[k] && ons[k].data[2] == 100
                          && near (ons[k].header.time, start);
        const bool offOk = offs[k].data[0] == 0x80 && offs[k].data[1] == expectedNotes[k]
                           && near (offs[k].header.time, start + gate);
        if (! onOk || ! offOk)
            std::printf ("    step %u: note-on %u@%u, note-off %u@%u, expected note %u at %u / %u\n", k, ons[k].data[1],
                         ons[k].header.time, offs[k].data[1], offs[k].header.time, expectedNotes[k], start, start + gate);
        onsOk &= onOk;
        offsOk &= offOk;
    }
    if (! onsOk || ! offsOk)
        std::printf ("    got %zu note-ons, %zu note-offs (expected 8 each)\n", ons.size(), offs.size());

    check (onsOk, "eight note-ons: C D E F G A B C5 on the 8th grid");
    check (offsOk, "eight note-offs, each 90% of an 8th after its note-on");

    // Everything that is not a note passes through unchanged.
    check (others.size() == 1 && near (others[0].header.time, blockSize + 5)
               && std::memcmp (others[0].data, script[1].data, 3) == 0,
           "CC passed through unchanged");

    plugin->stop_processing (plugin);
    plugin->deactivate (plugin);
    plugin->destroy (plugin);
    entry->deinit();
    dlclose (lib);

    std::printf ("%s\n", failures == 0 ? "PASS" : "FAILED");
    return failures == 0 ? 0 : 1;
}
