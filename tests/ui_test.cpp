// Headless test of the editor: builds the real editor (no window, no host) next to a real processor, changes
// parameters the way a host does, and checks which controls are enabled. Optionally writes PNG snapshots of the
// editor, so that the layout can be looked at:
//
//   SeqVerseUiTest [snapshot-prefix]      exit code 0 = all checks passed

#include "../src/Music.h"
#include "../src/PluginEditor.h"

#include <cstdio>
#include <cstdlib>
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

// A host's transport that plays or does not, at a position given in quarter notes
class FakeTransport : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setPpqPosition (quarters);
        info.setBpm (120.0);
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }

    bool playing = false;
    double quarters = 0.0;
};

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
    // The plugin writes its takes in a folder of this test's own, so that the music folder of the user is left alone. The
    // folder does not exist until the plugin has written a take.
    const auto tempFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("seqverse_ui_test_" + juce::String (juce::Time::currentTimeMillis()));
    setenv ("SEQVERSE_TAKE_FOLDER", tempFolder.getFullPathName().toRawUTF8(), 1);

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
                                          "lowOctave", "octaves", "velocity", "velocityVariation", "channel", "cycleLength", "rhythm", "lengthVariation" };
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

    std::printf ("arpeggio with length variation\n");
    *processor.lengthVariationParam = 30;
    check (enabled ("seed"), "the seed is enabled: the lengths are random");
    check (othersEnabled(), "and every other control is enabled, the length variation among them");
    *processor.lengthVariationParam = 0;
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

    std::printf ("a knob just above its minimum\n");
    *processor.swingParam = 2;
    snapshot ("swing2");
    *processor.swingParam = 0;

    std::printf ("cycle length\n");
    {
        auto* cycleSlider = dynamic_cast<juce::Slider*> (editor->findChildWithID ("cycleLength"));
        check (cycleSlider != nullptr, "the cycle length is a slider");
        if (cycleSlider != nullptr)
        {
            check (cycleSlider->getTextFromValue (0) == "Off", "at its minimum it says Off");
            check (cycleSlider->getTextFromValue (1) == "0.5 beats" && cycleSlider->getTextFromValue (2) == "1 beat"
                       && cycleSlider->getTextFromValue (8) == "4 beats" && cycleSlider->getTextFromValue (9) == "4.5 beats",
                   "half beats read as 0.5 beats, 1 beat, 4 beats, 4.5 beats");
        }

        // What is typed into the value box is taken the way the parameter takes it, not as half beats
        if (cycleSlider != nullptr)
            check (juce::roundToInt (cycleSlider->getValueFromText ("4.5")) == 9 && juce::roundToInt (cycleSlider->getValueFromText ("4.5 beats")) == 9
                       && juce::roundToInt (cycleSlider->getValueFromText ("1")) == 2 && juce::roundToInt (cycleSlider->getValueFromText ("Off")) == 0
                       && juce::roundToInt (cycleSlider->getValueFromText ("Endless")) == 0,
                   "typing 4.5, 4.5 beats, 1, Off or Endless (the old name) gives 9, 9, 2, 0 and 0 half beats");
        auto* seedKnob = dynamic_cast<juce::Slider*> (editor->findChildWithID ("seed"));
        if (seedKnob != nullptr)
            check (juce::roundToInt (seedKnob->getValueFromText ("Auto")) == 0 && juce::roundToInt (seedKnob->getValueFromText ("42")) == 42,
                   "typing Auto or 42 in the seed box gives 0 and 42");

        *processor.cycleLengthParam = 9;
        check (cycleSlider != nullptr && juce::roundToInt (cycleSlider->getValue()) == 9, "the cycleSlider follows the parameter");
        check (enabled ("arpDirection") && ! enabled ("seed") && othersEnabled(), "a cycle changes nothing about which controls are enabled");
        snapshot ("cycle");
    }

    std::printf ("record\n");
    {
        check (! enabled ("recordBars"), "the number of bars is disabled while Record is off");
        check (enabled ("record"), "the Record button is enabled");
        check (enabled ("midiRoot"), "the MIDI Root button is enabled");

        // Clicked the way a user does, not set through the parameter: the button has to toggle
        auto* midiRootButton = dynamic_cast<juce::Button*> (editor->findChildWithID ("midiRoot"));
        check (midiRootButton != nullptr, "the MIDI Root button is a button");
        if (midiRootButton != nullptr)
        {
            midiRootButton->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        }
        check (processor.midiRootParam->get() && midiRootButton != nullptr && midiRootButton->getToggleState(),
               "clicking MIDI Root arms it, and the button shows it");

        for (const char* id : { "takeIcon", "openFolder" })
        {
            auto* tip = dynamic_cast<juce::TooltipClient*> (editor->findChildWithID (id));
            check (tip != nullptr && tip->getTooltip().containsIgnoreCase ("folder"),
                   (std::string (id) + " has a tooltip that points to the folder").c_str());
        }
        check (! enabled ("takeIcon") && ! enabled ("openFolder"), "there is no take yet, so the page cannot be dragged and there is no folder to open");
        snapshot ("record_off");

        // Clicked the way a user does, not set through the parameter: the button has to toggle
        auto* recordButton = dynamic_cast<juce::Button*> (editor->findChildWithID ("record"));
        check (recordButton != nullptr, "the Record button is a button");
        const auto click = [&]
        {
            recordButton->triggerClick(); // it is delivered as a message
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        };
        if (recordButton != nullptr)
            click();
        check (processor.recordParam->get() && recordButton != nullptr && recordButton->getToggleState(), "clicking Record arms it, and the button shows it");
        check (enabled ("recordBars"), "armed: the number of bars is enabled");
        *processor.recordBarsParam = 2;
        snapshot ("record_armed");

        // Two beats of playing and then a stop, through the processor as a host would drive it
        FakeTransport transport;
        processor.setPlayHead (&transport);
        processor.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;

        transport.playing = true;
        for (int block = 0; block < 100; ++block)
        {
            transport.quarters = block * 512.0 / 24000.0;
            processor.processBlock (audio, midi);
            if (block == 0)
                check (processor.isRecording(), "the first block after the start begins the take");
        }

        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        check (! enabled ("record"), "Record cannot be toggled while the host is playing");
        check (processor.recordParam->get(), "and it is still armed while the take is being recorded");
        check (enabled ("midiRoot"), "MIDI Root is unrelated to recording, so it stays toggleable while the host plays");

        transport.playing = false;
        processor.processBlock (audio, midi);
        check (! processor.isRecording(), "the stop ends the take");

        // The file is written a moment later, and the editor's timer picks it up
        for (int wait = 0; wait < 100 && processor.getTakeCount() == 0; ++wait)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);

        const auto take = processor.getLastTake();
        check (processor.getTakeCount() == 1 && take.file.existsAsFile() && take.notes == 5, "a file with the five notes was written");
        check (enabled ("takeIcon") && enabled ("openFolder"), "the page can be dragged now, and the folder can be opened");
        snapshot ("record_take");

        check (! processor.recordParam->get() && recordButton != nullptr && ! recordButton->getToggleState(),
               "the take is done, so Record is unarmed again, and the button shows it");
        check (enabled ("record") && ! enabled ("recordBars"), "the host is stopped: Record can be toggled again, and the number of bars is disabled");

        processor.setPlayHead (nullptr);
        if (recordButton != nullptr)
            click();
        check (processor.recordParam->get(), "clicking Record arms it again");
        if (recordButton != nullptr)
            click();
        check (! processor.recordParam->get() && ! enabled ("recordBars"), "and clicking it again disarms it");
    }

    std::printf ("every control has a parameter behind it\n");
    for (auto* parameter : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            check (editor->findChildWithID (ranged->getParameterID()) != nullptr, ranged->getParameterID().toStdString());

    editor.reset();
    tempFolder.deleteRecursively();

    std::printf ("%s\n", failures == 0 ? "PASS" : "FAILED");
    return failures == 0 ? 0 : 1;
}
