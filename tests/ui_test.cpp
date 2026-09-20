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
                                          "lowOctave", "highOctave", "velocity", "velocityVariation", "channel", "cycleLength" };
    const auto othersEnabled = [&]
    {
        bool ok = true;
        for (const char* id : alwaysEnabled)
            ok &= enabled (id);
        return ok;
    };

    std::printf ("arpeggio, 100 %%, no velocity variation (the defaults)\n");
    check (enabled ("arpDirection"), "arp direction is enabled");
    check (! enabled ("seed"), "the seed is disabled: nothing is random");
    check (! enabled ("euclidSteps") && ! enabled ("euclidPulses") && ! enabled ("euclidRotation"), "the Euclidean settings are disabled");
    check (othersEnabled(), "all the other controls are enabled");
    snapshot ("arpeggio");

    std::printf ("random mode\n");
    *processor.playModeParam = music::randomNotes;
    check (! enabled ("arpDirection"), "arp direction is disabled");
    check (enabled ("seed"), "the seed is enabled");
    check (! enabled ("euclidSteps") && ! enabled ("euclidPulses") && ! enabled ("euclidRotation"), "the Euclidean settings are disabled");
    check (othersEnabled(), "all the other controls are enabled");
    snapshot ("random");

    std::printf ("euclidean mode\n");
    *processor.playModeParam = music::euclidean;
    check (enabled ("arpDirection"), "arp direction is enabled: the pulses walk the scale");
    check (enabled ("euclidSteps") && enabled ("euclidPulses") && enabled ("euclidRotation"), "the Euclidean settings are enabled");
    check (! enabled ("seed"), "the seed is disabled: nothing is random at 100 %");
    check (othersEnabled(), "all the other controls are enabled");
    *processor.chanceParam = 70;
    check (enabled ("seed"), "the seed is enabled with a chance below 100 %");
    *processor.chanceParam = 100;
    *processor.euclidStepsParam = 12;
    *processor.euclidPulsesParam = 7;
    snapshot ("euclidean");

    std::printf ("back to arpeggio\n");
    *processor.playModeParam = music::arpeggio;
    check (enabled ("arpDirection") && ! enabled ("seed"), "as at the start");

    std::printf ("arpeggio with a chance below 100 %%\n");
    *processor.chanceParam = 60;
    check (enabled ("seed"), "the seed is enabled: which steps play is random");
    check (enabled ("arpDirection"), "arp direction is still enabled");
    snapshot ("arpeggio_chance60");
    *processor.chanceParam = 100;
    check (! enabled ("seed"), "the seed is disabled again at 100 %");

    std::printf ("arpeggio with velocity variation\n");
    *processor.velocityVariationParam = 10;
    check (enabled ("seed"), "the seed is enabled: the velocities are random");
    *processor.velocityVariationParam = 0;
    check (! enabled ("seed"), "the seed is disabled again without variation");

    std::printf ("cycle length\n");
    {
        auto* knob = dynamic_cast<juce::Slider*> (editor->findChildWithID ("cycleLength"));
        check (knob != nullptr, "the cycle length is a knob");
        if (knob != nullptr)
        {
            check (knob->getTextFromValue (0) == "Endless", "at its minimum it says Endless");
            check (knob->getTextFromValue (1) == "0.5 beats" && knob->getTextFromValue (2) == "1 beat"
                       && knob->getTextFromValue (8) == "4 beats" && knob->getTextFromValue (9) == "4.5 beats",
                   "half beats read as 0.5 beats, 1 beat, 4 beats, 4.5 beats");
        }

        // What is typed into the value box is taken the way the parameter takes it, not as half beats
        if (knob != nullptr)
            check (juce::roundToInt (knob->getValueFromText ("4.5")) == 9 && juce::roundToInt (knob->getValueFromText ("4.5 beats")) == 9
                       && juce::roundToInt (knob->getValueFromText ("1")) == 2 && juce::roundToInt (knob->getValueFromText ("Endless")) == 0,
                   "typing 4.5, 4.5 beats, 1 or Endless gives 9, 9, 2 and 0 half beats");
        auto* seedKnob = dynamic_cast<juce::Slider*> (editor->findChildWithID ("seed"));
        if (seedKnob != nullptr)
            check (juce::roundToInt (seedKnob->getValueFromText ("Auto")) == 0 && juce::roundToInt (seedKnob->getValueFromText ("42")) == 42,
                   "typing Auto or 42 in the seed box gives 0 and 42");

        *processor.cycleLengthParam = 9;
        check (knob != nullptr && juce::roundToInt (knob->getValue()) == 9, "the knob follows the parameter");
        check (enabled ("arpDirection") && ! enabled ("seed") && othersEnabled(), "a cycle changes nothing about which controls are enabled");
        snapshot ("cycle");
    }

    std::printf ("every control has a parameter behind it\n");
    for (auto* parameter : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            check (editor->findChildWithID (ranged->getParameterID()) != nullptr, ranged->getParameterID().toStdString());

    editor.reset();

    std::printf ("%s\n", failures == 0 ? "PASS" : "FAILED");
    return failures == 0 ? 0 : 1;
}
