#include "PluginEditor.h"
#include "Music.h"

namespace
{
// JUCE draws its "Made with JUCE" badge in the bottom right corner of the editor. The last strip is left empty so
// that it does not cover the seed knob.
constexpr int kFooterHeight = 70;

// Two rows of knobs, with space between them so that the value boxes of the first do not crowd the labels of the second
constexpr int kKnobRowHeight = 160;
constexpr int kKnobRowGap = 30;
constexpr int kKnobsPerRow = 7;

// Above the knobs: the margins, the title, and the three rows of selectors (252 pixels), then the footer without the
// margin that is already counted
constexpr int kHeight = 252 + (kFooterHeight - 16) + 2 * kKnobRowHeight + kKnobRowGap;
}

SeqVerseEditor::SeqVerseEditor (SeqVerseProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&look); // the components below take their look from here

    titleLabel.setText ("SeqVerse", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setFont (juce::Font (28.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (titleLabel);

    setUpSelector (key, "Key", *processor.keyParam);
    setUpSelector (scale, "Scale", *processor.scaleParam);
    setUpSelector (stepRate, "Step rate", *processor.stepRateParam);
    setUpSelector (playMode, "Play mode", *processor.playModeParam);
    setUpSelector (arpDirection, "Arp direction", *processor.arpDirectionParam);
    setUpSelector (octaveNote, "Octave note", *processor.octaveNoteParam);
    setUpSelector (rhythm, "Rhythm", *processor.rhythmParam);

    setUpKnob (chance, "Chance", *processor.chanceParam);
    chance.slider.setTextValueSuffix (" %");

    setUpKnob (noteLength, "Note length", *processor.noteLengthParam);

    setUpKnob (swing, "Swing", *processor.swingParam);
    swing.slider.setTextValueSuffix (" %");

    setUpKnob (velocity, "Velocity", *processor.velocityParam);
    setUpKnob (velocityVariation, "Velocity +/-", *processor.velocityVariationParam);
    setUpKnob (lowOctave, "Low octave", *processor.lowOctaveParam);
    setUpKnob (highOctave, "High octave", *processor.highOctaveParam);
    setUpKnob (channel, "Channel", *processor.channelParam);

    // The value text ("Off", "4.5 beats", "Auto") and what is typed into the value box are handled by the
    // parameters through the slider attachments, so the host shows the same text
    setUpKnob (cycleLength, "Cycle length", *processor.cycleLengthParam);
    setUpKnob (euclidSteps, "Pattern length", *processor.euclidStepsParam);
    setUpKnob (euclidPulses, "Pulses", *processor.euclidPulsesParam);
    setUpKnob (euclidRotation, "Rotation", *processor.euclidRotationParam);
    setUpKnob (seed, "Seed", *processor.seedParam);

    // The low octave cannot be dragged above the high one, nor the high one below the low one. This is only about what
    // the user does here: when the host has set them the other way round they are left alone (the sound is the same,
    // the range is low..high either way), and nothing here ever writes the other parameter.
    lowOctave.slider.limit = [this] (double value)
    {
        const int low = processor.lowOctaveParam->get(), high = processor.highOctaveParam->get();
        return low <= high ? juce::jmin (value, (double) high) : value;
    };
    highOctave.slider.limit = [this] (double value)
    {
        const int low = processor.lowOctaveParam->get(), high = processor.highOctaveParam->get();
        return low <= high ? juce::jmax (value, (double) low) : value;
    };

    // The attachments notify these on host changes (automation) as well as on changes made here
    playMode.box.onChange = [this] { updateEnabledControls(); };
    rhythm.box.onChange = [this] { updateEnabledControls(); };
    chance.slider.onValueChange = [this] { updateEnabledControls(); };
    velocityVariation.slider.onValueChange = [this] { updateEnabledControls(); };
    updateEnabledControls();

    // The value boxes of the sliders were made before the sliders were children of the editor, so they did not know
    // about its look yet. This makes every control take it.
    sendLookAndFeelChange();

    setSize (660, kHeight);
}

SeqVerseEditor::~SeqVerseEditor()
{
    setLookAndFeel (nullptr);
}

void SeqVerseEditor::updateEnabledControls()
{
    const bool arpeggio = processor.playModeParam->getIndex() == music::arpeggio;

    // Only the arpeggio walks the pool in a direction
    setEnabled (arpDirection, arpeggio);

    // The steps, pulses and rotation are the Euclidean rhythm's
    const bool euclidean = processor.rhythmParam->getIndex() == music::euclidean;
    setEnabled (euclidSteps, euclidean);
    setEnabled (euclidPulses, euclidean);
    setEnabled (euclidRotation, euclidean);

    // The seed only matters where something is random: the notes of Random mode, a chance below 100 %, or a velocity
    // variation. An arpeggio at 100 % with a fixed velocity has nothing to seed.
    const bool anythingRandom = ! arpeggio || processor.chanceParam->get() < 100 || processor.velocityVariationParam->get() > 0;
    setEnabled (seed, anythingRandom);
}

void SeqVerseEditor::setEnabled (Selector& selector, bool enabled)
{
    selector.label.setEnabled (enabled);
    selector.box.setEnabled (enabled);
}

void SeqVerseEditor::setEnabled (Knob& knob, bool enabled)
{
    knob.label.setEnabled (enabled);
    knob.slider.setEnabled (enabled);
}

void SeqVerseEditor::setUpSelector (Selector& selector, const juce::String& text, juce::AudioParameterChoice& parameter)
{
    selector.label.setText (text, juce::dontSendNotification);
    addAndMakeVisible (selector.label);

    selector.box.setComponentID (parameter.getParameterID());
    selector.box.addItemList (parameter.choices, 1);
    addAndMakeVisible (selector.box);

    selector.attachment = std::make_unique<juce::ComboBoxParameterAttachment> (parameter, selector.box);
}

void SeqVerseEditor::setUpKnob (Knob& knob, const juce::String& text, juce::RangedAudioParameter& parameter)
{
    knob.label.setText (text, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.slider.setComponentID (parameter.getParameterID());
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 22);
    addAndMakeVisible (knob.slider);

    knob.attachment = std::make_unique<juce::SliderParameterAttachment> (parameter, knob.slider);
}

void SeqVerseEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void SeqVerseEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    titleLabel.setBounds (area.removeFromTop (40));
    area.removeFromTop (8);

    // Selectors in a grid of three columns, two rows
    const auto placeSelectors = [] (juce::Rectangle<int> row, std::initializer_list<Selector*> selectors)
    {
        const int width = row.getWidth() / 3;
        for (auto* selector : selectors)
        {
            auto cell = row.removeFromLeft (width).reduced (6, 0);
            selector->label.setBounds (cell.removeFromTop (20));
            selector->box.setBounds (cell.removeFromTop (26));
        }
    };

    placeSelectors (area.removeFromTop (46), { &key, &scale, &stepRate });
    area.removeFromTop (10);
    placeSelectors (area.removeFromTop (46), { &playMode, &arpDirection, &octaveNote });
    area.removeFromTop (10);
    placeSelectors (area.removeFromTop (46), { &rhythm });
    area.removeFromTop (14);
    area.removeFromBottom (kFooterHeight - 16); // the editor's 16 pixel margin is already taken off

    // Knobs: a grid of seven columns and two rows
    const auto placeKnobs = [] (juce::Rectangle<int> row, std::initializer_list<Knob*> knobs)
    {
        const int width = row.getWidth() / kKnobsPerRow;
        for (auto* knob : knobs)
        {
            auto cell = row.removeFromLeft (width);
            knob->label.setBounds (cell.removeFromTop (20));
            knob->slider.setBounds (cell);
        }
    };

    placeKnobs (area.removeFromTop (kKnobRowHeight), { &chance, &noteLength, &swing, &velocity, &velocityVariation, &channel });
    area.removeFromTop (kKnobRowGap);
    placeKnobs (area.removeFromTop (kKnobRowHeight), { &lowOctave, &highOctave, &euclidSteps, &euclidPulses, &euclidRotation, &cycleLength, &seed });
}
