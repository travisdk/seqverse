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

    // A slider with its name to the left and its value in a box to the right
    struct Setting
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
    };

    // Each control's component ID is its parameter ID, so tests can find it with findChildWithID().
    void setUpSelector (Selector&, const juce::String& text, juce::AudioParameterChoice&);
    void setUpRow (Setting&, const juce::String& text, juce::RangedAudioParameter&);

    void timerCallback() override;
    void showTake();

    // Controls that have no effect with the current settings are disabled (dimmed), label included.
    void updateEnabledControls();
    static void setEnabled (Selector&, bool);
    static void setEnabled (Setting&, bool);

    SeqVerseProcessor& processor;

    DarkLookAndFeel look; // declared before the components that use it, so that it outlives them
    juce::TooltipWindow tooltipWindow { this }; // shows the tooltips of the take icon and the folder button

    juce::Label titleLabel;
    // In three columns: pitch, rhythm and feel
    Selector key, scale, octaveNote, playMode, arpDirection;
    Setting lowOctave, octaves;
    Selector stepRate, rhythm;
    Setting euclidSteps, euclidPulses, euclidRotation, cycleLength, swing;
    Selector noteLength;
    Setting lengthVariation, velocity, velocityVariation, chance, channel, seed;

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

    // The section headings, painted where resized() puts them
    struct Heading { juce::String text; juce::Rectangle<int> area; };
    std::vector<Heading> headings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeqVerseEditor)
};
