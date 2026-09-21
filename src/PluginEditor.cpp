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

constexpr int kRecordRowHeight = 56;  // the page that is dragged out is 44 by 56
constexpr int kIconWidth = 44;
constexpr int kRecordButtonWidth = 96;
constexpr int kOpenFolderWidth = 104;

// The margins, the title, the three columns of rows, and the record band
constexpr int kHeight = kMargin + 44 + kHeadingHeight + 4 + kRowsPerColumn * kRowHeight
                        + kHeadingHeight + 4 + kRecordRowHeight + kMargin;
}

void TakeIcon::setFile (const juce::File& newFile)
{
    file = newFile;
    setEnabled (file.existsAsFile());
    setMouseCursor (isEnabled() ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void TakeIcon::paint (juce::Graphics& g)
{
    if (auto* look = dynamic_cast<DarkLookAndFeel*> (&getLookAndFeel()))
        look->drawDocumentIcon (g, getLocalBounds().toFloat(), "MIDI", isEnabled(), isMouseOver());
}

// The drag is the operating system's: the host takes the file as it takes one from a file manager
void TakeIcon::mouseDrag (const juce::MouseEvent& event)
{
    if (! dragStarted && isEnabled() && event.getDistanceFromDragStart() > 4 && file.existsAsFile())
    {
        dragStarted = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
    }
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

    record.button.setButtonText ("Record");
    record.button.setComponentID (processor.recordParam->getParameterID());
    record.button.setClickingTogglesState (true); // the attachment does not do it, and a click would set the parameter back
    record.attachment = std::make_unique<juce::ButtonParameterAttachment> (*processor.recordParam, record.button);
    addAndMakeVisible (record.button);
    setUpRow (recordBars, "Bars", *processor.recordBarsParam);

    takeStatus.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (takeStatus);
    takeIcon.setComponentID ("takeIcon");
    addAndMakeVisible (takeIcon);

    // Dragging the page out does not work in every host (Bitwig on Linux does not take it), and a file manager does
    openFolder.setButtonText ("Open folder");
    openFolder.setComponentID ("openFolder");
    openFolder.onClick = [] { SeqVerseProcessor::getTakeFolder().revealToUser(); };
    addAndMakeVisible (openFolder);

    // The attachments notify these on host changes (automation) as well as on changes made here
    record.button.onClick = [this] { updateEnabledControls(); showTake(); };
    playMode.box.onChange = [this] { updateEnabledControls(); };
    rhythm.box.onChange = [this] { updateEnabledControls(); };
    chance.slider.onValueChange = [this] { updateEnabledControls(); };
    velocityVariation.slider.onValueChange = [this] { updateEnabledControls(); };
    lengthVariation.slider.onValueChange = [this] { updateEnabledControls(); };
    updateEnabledControls();
    showTake();
    startTimerHz (10); // the take is written a moment after it ends, and the recording state changes with the host

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

    // The number of bars only matters when recording
    setEnabled (recordBars, processor.recordParam->get());
}

// What the recording is doing, and the last take as a page to drag
void SeqVerseEditor::showTake()
{
    const auto take = processor.getLastTake();
    takeIcon.setFile (take.file);
    openFolder.setEnabled (SeqVerseProcessor::getTakeFolder().isDirectory()); // there is one once a take has been written

    juce::String text;
    if (processor.isRecording())
        text = "Recording...";
    else if (take.file != juce::File())
    {
        const int tenthsOfBars = juce::roundToInt (take.beats / take.beatsPerBar * 10.0);
        text = "Last take: " + juce::String (take.notes) + (take.notes == 1 ? " note, " : " notes, ")
               + (tenthsOfBars % 10 == 0 ? juce::String (tenthsOfBars / 10) : juce::String (tenthsOfBars / 10.0, 1))
               + (tenthsOfBars == 10 ? " bar" : " bars") + ".";
    }
    else if (processor.recordParam->get())
        text = "Armed. It records when the host starts playing.";
    else
        text = "Arm Record, then start the host.";

    takeStatus.setText (text, juce::dontSendNotification);
    shownTakeCount = processor.getTakeCount();
    shownRecording = processor.isRecording();
}

void SeqVerseEditor::timerCallback()
{
    if (processor.getTakeCount() != shownTakeCount || processor.isRecording() != shownRecording)
        showTake();
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
    auto columns = area.removeFromTop (kHeadingHeight + 4 + kRowsPerColumn * kRowHeight);
    const int columnWidth = (columns.getWidth() - (kColumns - 1) * kColumnGap) / kColumns;
    const auto column = [&] (const juce::String& heading)
    {
        auto cell = columns.removeFromLeft (columnWidth);
        columns.removeFromLeft (kColumnGap);
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

    // Record: the button, the number of bars, the page with the last take, and what is happening
    headings.push_back ({ "Record", area.removeFromTop (kHeadingHeight) });
    area.removeFromTop (4);
    auto band = area.removeFromTop (kRecordRowHeight);

    record.button.setBounds (band.removeFromLeft (kRecordButtonWidth).withSizeKeepingCentre (kRecordButtonWidth, kControlHeight + 4));
    band.removeFromLeft (kColumnGap);
    auto barsCell = band.removeFromLeft (kLabelWidth + columnWidth - 40).withSizeKeepingCentre (kLabelWidth + columnWidth - 40, kControlHeight);
    recordBars.label.setBounds (barsCell.removeFromLeft (kLabelWidth));
    recordBars.slider.setBounds (barsCell);
    band.removeFromLeft (kColumnGap);
    takeIcon.setBounds (band.removeFromLeft (kIconWidth));
    band.removeFromLeft (kColumnGap / 2);
    openFolder.setBounds (band.removeFromRight (kOpenFolderWidth).withSizeKeepingCentre (kOpenFolderWidth, kControlHeight + 4));
    band.removeFromRight (kColumnGap / 2);
    takeStatus.setBounds (band);

    repaint(); // the headings have moved
}
