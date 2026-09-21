// Headless test of the editor: builds the real editor (no window, no host) next to a real processor, changes
// parameters the way a host does, and checks which controls are enabled. Optionally writes PNG snapshots of the
// editor, so that the layout can be looked at:
//
//   SeqVerseUiTest [snapshot-prefix]      exit code 0 = all checks passed

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

    SeqVerseProcessor processor;
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

    const char* const alwaysEnabled[] = { "key", "scale", "playMode", "octaveNote", "stepRate", "noteLength", "chance", "swing",
                                          "lowOctave", "highOctave", "velocity", "velocityVariation", "channel", "cycleLength", "rhythm" };
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
    check (othersEnabled(), "all the other controls are enabled");
    snapshot ("random");

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

    std::printf ("euclidean rhythm\n");
    *processor.rhythmParam = music::euclidean;
    check (enabled ("euclidSteps") && enabled ("euclidPulses") && enabled ("euclidRotation"), "the steps, pulses and rotation are enabled");
    check (enabled ("arpDirection") && ! enabled ("seed"), "arp direction is enabled, and the seed is not: nothing is random at 100 %");
    check (othersEnabled(), "all the other controls are enabled, the cycle length among them");
    *processor.chanceParam = 70;
    check (enabled ("seed"), "the seed is enabled with a chance below 100 %");
    *processor.chanceParam = 100;
    *processor.euclidPulsesParam = 3;
    *processor.euclidStepsParam = 7;
    snapshot ("euclidean");
    *processor.euclidStepsParam = music::kDefaultEuclidSteps;
    *processor.euclidPulsesParam = music::kDefaultEuclidPulses;
    *processor.rhythmParam = music::everyStep;
    check (! enabled ("euclidSteps") && ! enabled ("euclidPulses") && ! enabled ("euclidRotation"), "the Euclidean settings are disabled again");

    // The low octave is not above the high one, and the other way round, as far as the user is concerned: what they do
    // to a knob here is limited. What the host sets is never touched: nothing writes a parameter because another one
    // changed, so a host that automates both, in any order, cannot be fought or pushed around.
    std::printf ("low and high octave\n");
    {
        auto* low = dynamic_cast<LimitedSlider*> (editor->findChildWithID ("lowOctave"));
        auto* high = dynamic_cast<LimitedSlider*> (editor->findChildWithID ("highOctave"));
        check (low != nullptr && high != nullptr, "both are limited sliders");

        // What the user can get the knob to, for a value they try
        const auto limited = [] (LimitedSlider* slider, double attempted) { return juce::roundToInt (slider->limit (attempted)); };

        if (low != nullptr && high != nullptr)
        {
            *processor.lowOctaveParam = 2;
            *processor.highOctaveParam = 5;
            check (limited (low, 7) == 5 && limited (low, 1) == 1, "the user cannot drag or type the low octave above the high one (5)");
            check (limited (high, 0) == 2 && limited (high, 7) == 7, "nor the high octave below the low one (2)");

            // The host sets the low octave above the high one, as automation or a generic parameter list can
            *processor.lowOctaveParam = 6;
            check (processor.lowOctaveParam->get() == 6 && processor.highOctaveParam->get() == 5, "set by the host, the other parameter is left alone");
            check (juce::roundToInt (low->getValue()) == 6 && juce::roundToInt (high->getValue()) == 5, "and the knobs show what the parameters say");
            check (limited (low, 7) == 7 && limited (high, 0) == 0, "while they are crossed the user is not restricted, so it can be put right");

            *processor.highOctaveParam = 7;
            check (limited (low, 9) == 7 && limited (low, 3) == 3 && limited (high, 0) == 6, "back in order (6 and 7): limited again");

            *processor.lowOctaveParam = music::kDefaultOctave;
            *processor.highOctaveParam = music::kDefaultOctave;
        }
    }

    std::printf ("cycle length\n");
    {
        auto* knob = dynamic_cast<juce::Slider*> (editor->findChildWithID ("cycleLength"));
        check (knob != nullptr, "the cycle length is a knob");
        if (knob != nullptr)
        {
            check (knob->getTextFromValue (0) == "Off", "at its minimum it says Off");
            check (knob->getTextFromValue (1) == "0.5 beats" && knob->getTextFromValue (2) == "1 beat"
                       && knob->getTextFromValue (8) == "4 beats" && knob->getTextFromValue (9) == "4.5 beats",
                   "half beats read as 0.5 beats, 1 beat, 4 beats, 4.5 beats");
        }

        // What is typed into the value box is taken the way the parameter takes it, not as half beats
        if (knob != nullptr)
            check (juce::roundToInt (knob->getValueFromText ("4.5")) == 9 && juce::roundToInt (knob->getValueFromText ("4.5 beats")) == 9
                       && juce::roundToInt (knob->getValueFromText ("1")) == 2 && juce::roundToInt (knob->getValueFromText ("Off")) == 0
                       && juce::roundToInt (knob->getValueFromText ("Endless")) == 0,
                   "typing 4.5, 4.5 beats, 1, Off or Endless (the old name) gives 9, 9, 2, 0 and 0 half beats");
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
