#pragma once

#include "DarkLookAndFeel.h"
#include "PluginProcessor.h"

// The take, as a page that is dragged into the host: dragging it out starts a drag of the MIDI file
class TakeIcon : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setFile (const juce::File& newFile);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { dragStarted = false; }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::File file;
    bool dragStarted = false;
};

// A choice of a few, as a row of chips of which the current one is lit (Play mode, Octave note, Rhythm)
class SegmentedChoice : public juce::Component
{
public:
    void setParameter (juce::AudioParameterChoice&);
    void resized() override;

    std::function<void()> onChange; // after the choice has changed, here or in the host

private:
    void showIndex (int);

    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

class SeqVerseEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SeqVerseEditor (SeqVerseProcessor&);
    ~SeqVerseEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // The attachment is declared after the component it drives, so it is destroyed first.
    struct Selector
    {
        juce::Label label;
        juce::ComboBox box;
        std::unique_ptr<juce::ComboBoxParameterAttachment> attachment;
    };

    struct Choice
    {
        juce::Label label;
        SegmentedChoice buttons;
    };

    // A knob with its value below it, which can be clicked to type one
    struct Setting
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
    };

    // Each control's component ID is its parameter ID, so tests can find it with findChildWithID(). Its name is above it.
    void setUpCaption (juce::Label&, const juce::String& text, juce::Justification);
    void setUpSelector (Selector&, const juce::String& text, juce::AudioParameterChoice&);
    void setUpChoice (Choice&, const juce::String& text, juce::AudioParameterChoice&);
    void setUpKnob (Setting&, const juce::String& text, juce::RangedAudioParameter&);

    void timerCallback() override;
    void showTake();

    // Controls that have no effect with the current settings are disabled (dimmed), label included.
    void updateEnabledControls();
    static void setEnabled (Selector&, bool);
    static void setEnabled (Setting&, bool);

    SeqVerseProcessor& processor;

    DarkLookAndFeel look; // declared before the components that use it, so that it outlives them
    juce::TooltipWindow tooltipWindow { this }; // shows the tooltips of the take icon and the folder button

    // In three panels: pitch, rhythm and feel
    Selector key, scale, arpDirection;
    Choice playMode, octaveNote;
    Setting lowOctave, octaves;
    Selector stepRate;
    Choice rhythm;
    Setting euclidSteps, euclidPulses, euclidRotation, cycleLength, swing;
    Selector noteLength;
    Setting lengthVariation, velocity, velocityVariation, chance, channel, seed;

    // An ON/OFF switch for a bool parameter: MIDI Root, beside the octave knobs
    struct Toggle
    {
        juce::Label label;
        juce::ToggleButton button;
        std::unique_ptr<juce::ButtonParameterAttachment> attachment;
    };
    void setUpToggle (Toggle&, const juce::String& text, juce::AudioParameterBool&);

    Toggle midiRoot;

    // Record: the button arms it, and the take that has been recorded is dragged from the page
    struct RecordButton
    {
        juce::TextButton button;
        std::unique_ptr<juce::ButtonParameterAttachment> attachment;
    };

    RecordButton record;
    Setting recordBars;
    juce::Label takeStatus;
    TakeIcon takeIcon;
    juce::TextButton openFolder; // the takes folder, where the files can be dragged into the host from the file manager
    int shownTakeCount = -1;
    bool shownRecording = false;
    bool shownPlaying = false;
    bool shownArmed = false;

    // The header and the panels of the sections, painted where resized() puts them
    struct Panel { juce::String heading; juce::Rectangle<int> area; };
    std::vector<Panel> panels;
    juce::Rectangle<int> header;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeqVerseEditor)
};
