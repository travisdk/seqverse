#include "PluginEditor.h"
#include "Music.h"

namespace
{
constexpr int kWidth = 840;
constexpr int kMargin = 16;
constexpr int kColumnGap = 20;
constexpr int kColumns = 3;

constexpr int kControlHeight = 26;   // a dropdown or a slider
constexpr int kRowHeight = 34;       // a control and the gap to the next row
constexpr int kRowsPerColumn = 7;
constexpr int kLabelWidth = 88;
constexpr int kValueWidth = 64;      // the value box of a slider, wide enough for "4.5 beats"
constexpr int kHeadingHeight = 26;

// The margins, the title, the heading and the rows
constexpr int kHeight = kMargin + 44 + kHeadingHeight + 4 + kRowsPerColumn * kRowHeight + kMargin;
}

SeqVerseEditor::SeqVerseEditor (SeqVerseProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&look); // the components below take their look from here

    titleLabel.setText ("SeqVerse", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setFont (juce::Font (26.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (titleLabel);

    setUpSelector (key, "Key", *processor.keyParam);
    setUpSelector (scale, "Scale", *processor.scaleParam);
    setUpSelector (octaveNote, "Octave note", *processor.octaveNoteParam);
    setUpRow (lowOctave, "Low octave", *processor.lowOctaveParam);
    setUpRow (octaves, "Octaves", *processor.octavesParam);
    setUpSelector (playMode, "Play mode", *processor.playModeParam);
    setUpSelector (arpDirection, "Arp direction", *processor.arpDirectionParam);

    setUpSelector (stepRate, "Step rate", *processor.stepRateParam);
    setUpSelector (rhythm, "Rhythm", *processor.rhythmParam);

    // The value text ("Off", "4.5 beats", "Auto") and what is typed into the value box are handled by the
    // parameters through the slider attachments, so the host shows the same text
    setUpRow (euclidSteps, "Pattern length", *processor.euclidStepsParam);
    setUpRow (euclidPulses, "Pulses", *processor.euclidPulsesParam);
    setUpRow (euclidRotation, "Rotation", *processor.euclidRotationParam);
    setUpRow (cycleLength, "Cycle length", *processor.cycleLengthParam);
    setUpRow (swing, "Swing", *processor.swingParam);
    swing.slider.setTextValueSuffix (" %");

    setUpSelector (noteLength, "Note length", *processor.noteLengthParam);
    setUpRow (lengthVariation, "Length var.", *processor.lengthVariationParam);
    lengthVariation.slider.setTextValueSuffix (" %");
    setUpRow (velocity, "Velocity", *processor.velocityParam);
    setUpRow (velocityVariation, "Velocity +/-", *processor.velocityVariationParam);
    setUpRow (chance, "Chance", *processor.chanceParam);
    chance.slider.setTextValueSuffix (" %");
    setUpRow (channel, "Channel", *processor.channelParam);
    setUpRow (seed, "Seed", *processor.seedParam);

    // The attachments notify these on host changes (automation) as well as on changes made here
    playMode.box.onChange = [this] { updateEnabledControls(); };
    rhythm.box.onChange = [this] { updateEnabledControls(); };
    chance.slider.onValueChange = [this] { updateEnabledControls(); };
    velocityVariation.slider.onValueChange = [this] { updateEnabledControls(); };
    lengthVariation.slider.onValueChange = [this] { updateEnabledControls(); };
    updateEnabledControls();

    // The value boxes of the sliders were made before the sliders were children of the editor, so they did not know
    // about its look yet. This makes every control take it.
    sendLookAndFeelChange();

    setSize (kWidth, kHeight);
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

    // The seed only matters where something is random: the notes of Random mode, a chance below 100 %, or a velocity or
    // length variation. An arpeggio at 100 % with a fixed velocity and length has nothing to seed.
    const bool anythingRandom = ! arpeggio || processor.chanceParam->get() < 100 || processor.velocityVariationParam->get() > 0
                                || processor.lengthVariationParam->get() > 0;
    setEnabled (seed, anythingRandom);
}

void SeqVerseEditor::setEnabled (Selector& selector, bool enabled)
{
    selector.label.setEnabled (enabled);
    selector.box.setEnabled (enabled);
}

void SeqVerseEditor::setEnabled (Setting& setting, bool enabled)
{
    setting.label.setEnabled (enabled);
    setting.slider.setEnabled (enabled);
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

void SeqVerseEditor::setUpRow (Setting& row, const juce::String& text, juce::RangedAudioParameter& parameter)
{
    row.label.setText (text, juce::dontSendNotification);
    row.label.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (row.label);

    row.slider.setComponentID (parameter.getParameterID());
    row.slider.setSliderStyle (juce::Slider::LinearHorizontal);
    row.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, kValueWidth, 22);
    addAndMakeVisible (row.slider);

    row.attachment = std::make_unique<juce::SliderParameterAttachment> (parameter, row.slider);
}

void SeqVerseEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    // A heading is its name in the accent colour and a line to the end of its section
    const juce::Font font (12.0f, juce::Font::bold);
    g.setFont (font);
    for (const auto& heading : headings)
    {
        const auto text = heading.text.toUpperCase();
        g.setColour (theme::accent);
        g.drawText (text, heading.area, juce::Justification::centredLeft);
        g.setColour (theme::outline);
        g.drawHorizontalLine (heading.area.getCentreY(), (float) heading.area.getX() + font.getStringWidthFloat (text) + 10.0f,
                              (float) heading.area.getRight());
    }
}

void SeqVerseEditor::resized()
{
    auto area = getLocalBounds().reduced (kMargin);
    titleLabel.setBounds (area.removeFromTop (36));
    area.removeFromTop (8);
    headings.clear();

    // Three columns of seven rows, dropdowns and sliders, each with its name to the left
    const int columnWidth = (area.getWidth() - (kColumns - 1) * kColumnGap) / kColumns;
    const auto column = [&] (const juce::String& heading)
    {
        auto cell = area.removeFromLeft (columnWidth);
        area.removeFromLeft (kColumnGap);
        headings.push_back ({ heading, cell.removeFromTop (kHeadingHeight) });
        cell.removeFromTop (4);
        return cell;
    };

    const auto place = [] (juce::Rectangle<int>& cell, juce::Component& label, juce::Component& control)
    {
        auto row = cell.removeFromTop (kRowHeight).withHeight (kControlHeight);
        label.setBounds (row.removeFromLeft (kLabelWidth));
        control.setBounds (row);
    };
    const auto placeSelector = [&] (juce::Rectangle<int>& cell, Selector& selector) { place (cell, selector.label, selector.box); };
    const auto placeRow = [&] (juce::Rectangle<int>& cell, Setting& setting) { place (cell, setting.label, setting.slider); };

    auto pitchColumn = column ("Pitch");
    placeSelector (pitchColumn, key);
    placeSelector (pitchColumn, scale);
    placeSelector (pitchColumn, octaveNote);
    placeRow (pitchColumn, lowOctave);
    placeRow (pitchColumn, octaves);
    placeSelector (pitchColumn, playMode);
    placeSelector (pitchColumn, arpDirection);

    auto rhythmColumn = column ("Rhythm");
    placeSelector (rhythmColumn, stepRate);
    placeSelector (rhythmColumn, rhythm);
    placeRow (rhythmColumn, euclidSteps);
    placeRow (rhythmColumn, euclidPulses);
    placeRow (rhythmColumn, euclidRotation);
    placeRow (rhythmColumn, cycleLength);
    placeRow (rhythmColumn, swing);

    auto feelColumn = column ("Feel");
    placeSelector (feelColumn, noteLength);
    placeRow (feelColumn, lengthVariation);
    placeRow (feelColumn, velocity);
    placeRow (feelColumn, velocityVariation);
    placeRow (feelColumn, chance);
    placeRow (feelColumn, channel);
    placeRow (feelColumn, seed);

    repaint(); // the headings have moved
}
