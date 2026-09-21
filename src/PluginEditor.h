#pragma once

#include "DarkLookAndFeel.h"
#include "PluginProcessor.h"

// A slider that can limit what the user does to it (dragging, the mouse wheel, the arrow keys, typing a value), and
// nothing else: a value that arrives through its parameter, from the host or from a loaded session, is shown as it is.
// It never writes a parameter itself.
class LimitedSlider : public juce::Slider
{
public:
    std::function<double (double)> limit;

    double snapValue (double attemptedValue, DragMode) override { return limit != nullptr ? limit (attemptedValue) : attemptedValue; }
};

class SeqVerseEditor : public juce::AudioProcessorEditor
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

    struct Knob
    {
        juce::Label label;
        LimitedSlider slider;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
    };

    // Each control's component ID is its parameter ID, so tests can find it with findChildWithID().
    void setUpSelector (Selector&, const juce::String& text, juce::AudioParameterChoice&);
    void setUpKnob (Knob&, const juce::String& text, juce::RangedAudioParameter&);

    // Controls that have no effect with the current settings are disabled (dimmed), label included.
    void updateEnabledControls();
    static void setEnabled (Selector&, bool);
    static void setEnabled (Knob&, bool);

    SeqVerseProcessor& processor;

    DarkLookAndFeel look; // declared before the components that use it, so that it outlives them

    juce::Label titleLabel;
    Selector key, scale, stepRate, playMode, arpDirection, octaveNote, rhythm;
    Knob chance, noteLength, swing, velocity, velocityVariation;
    Knob lowOctave, highOctave, channel, cycleLength, euclidSteps, euclidPulses, euclidRotation, seed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeqVerseEditor)
};
