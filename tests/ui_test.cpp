// Headless test of the editor: builds the real editor (no window, no host) next to a real processor, changes
// parameters the way a host does, and checks which controls are enabled. Optionally writes PNG snapshots of the
// editor, so that the layout can be looked at:
//
//   GenerativeMidiUiTest [snapshot-prefix]      exit code 0 = all checks passed

#include "../src/Music.h"
#include "../src/PluginEditor.h"

#include <cstdio>
#include <memory>
#include <string>

namespace
{
int failures = 0;

void check (bool ok, const std::string& what)
{
    std::printf ("  %s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (! ok)
        ++failures;
}

void saveSnapshot (juce::AudioProcessorEditor& editor, const std::string& path)
{
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds());
    juce::File file (path);
    file.deleteFile();
    if (auto stream = file.createOutputStream())
        juce::PNGImageFormat().writeImageToStream (image, *stream);
}
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI gui;

    GenerativeMidiProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    const std::string snapshotPrefix = argc > 1 ? argv[1] : "";

    const auto enabled = [&] (const char* id)
    {
        auto* component = editor->findChildWithID (id);
        if (component == nullptr)
            std::printf ("    no control with the ID '%s'\n", id);
        return component != nullptr && component->isEnabled();
    };

    const auto snapshot = [&] (const char* name)
    {
        if (! snapshotPrefix.empty())
            saveSnapshot (*editor, snapshotPrefix + "_" + name + ".png");
    };

    const char* const alwaysEnabled[] = { "key", "scale", "playMode", "stepRate", "noteLength", "chance", "swing",
                                          "lowOctave", "highOctave", "velocity", "velocityVariation", "channel" };
    const auto othersEnabled = [&]
    {
        bool ok = true;
        for (const char* id : alwaysEnabled)
            ok &= enabled (id);
        return ok;
    };

    std::printf ("arpeggio, 100 %%, no velocity variation (the defaults)\n");
    check (enabled ("arpDirection") && enabled ("arpReset"), "arp direction and arp reset are enabled");
    check (! enabled ("seed"), "the seed is disabled: nothing is random");
    check (othersEnabled(), "all the other controls are enabled");
    snapshot ("arpeggio");

    std::printf ("random mode\n");
    *processor.playModeParam = music::randomNotes;
    check (! enabled ("arpDirection") && ! enabled ("arpReset"), "arp direction and arp reset are disabled");
    check (enabled ("seed"), "the seed is enabled");
    check (othersEnabled(), "all the other controls are enabled");
    snapshot ("random");

    std::printf ("back to arpeggio\n");
    *processor.playModeParam = music::arpeggio;
    check (enabled ("arpDirection") && enabled ("arpReset") && ! enabled ("seed"), "as at the start");

    std::printf ("arpeggio with a chance below 100 %%\n");
    *processor.chanceParam = 60;
    check (enabled ("seed"), "the seed is enabled: which steps play is random");
    check (enabled ("arpDirection") && enabled ("arpReset"), "arp direction and arp reset are still enabled");
    snapshot ("arpeggio_chance60");
    *processor.chanceParam = 100;
    check (! enabled ("seed"), "the seed is disabled again at 100 %");

    std::printf ("arpeggio with velocity variation\n");
    *processor.velocityVariationParam = 10;
    check (enabled ("seed"), "the seed is enabled: the velocities are random");
    *processor.velocityVariationParam = 0;
    check (! enabled ("seed"), "the seed is disabled again without variation");

    std::printf ("every control has a parameter behind it\n");
    for (auto* parameter : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            check (editor->findChildWithID (ranged->getParameterID()) != nullptr, ranged->getParameterID().toStdString());

    editor.reset();

    std::printf ("%s\n", failures == 0 ? "PASS" : "FAILED");
    return failures == 0 ? 0 : 1;
}
