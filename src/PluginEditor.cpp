#include "PluginEditor.h"
#include "Music.h"

// Set by CMake (the project version); the fallback only matters for a build made without CMake
#ifndef SEQVERSE_VERSION
 #define SEQVERSE_VERSION "dev"
#endif

namespace
{
constexpr int kMargin = 16;
constexpr int kPanelGap = 12;
constexpr int kPadding = 14;        // inside a panel
constexpr int kHeadingHeight = 26;  // a panel's name, at its top
constexpr int kInnerWidth = 270;    // what a panel holds: three knobs side by side
constexpr int kPanelWidth = kInnerWidth + 2 * kPadding;
constexpr int kWidth = 2 * kMargin + 3 * kPanelWidth + 2 * kPanelGap;

constexpr int kHeaderHeight = 40;
constexpr int kCaptionHeight = 16;  // a control's name, above it
constexpr int kFieldHeight = 28;    // a dropdown or a row of chips
constexpr int kKnobHeight = 60;     // a knob and its value below it
constexpr int kValueHeight = 18;
constexpr int kRowGap = 12;
constexpr int kFieldGap = 10;       // between two fields side by side
constexpr int kKnobWidth = kInnerWidth / 3;
constexpr int kNarrowWidth = 80;    // the dropdown of a short value: key, step rate, note length
constexpr int kSwitchWidth = 52;

constexpr int kFieldRow = kCaptionHeight + kFieldHeight;
constexpr int kKnobRow = kCaptionHeight + kKnobHeight;
// Pitch is the tallest: three rows of fields and one of knobs. Rhythm and Feel spread their rows over the same height.
constexpr int kSectionHeight = kPadding + kHeadingHeight + 3 * kFieldRow + kKnobRow + 3 * kRowGap + kPadding;
constexpr int kRecordPadding = 10;  // the Record panel has no heading (the button says it) and is only as tall as the bars knob
constexpr int kRecordHeight = kRecordPadding + kKnobRow + kRecordPadding;
constexpr int kHeight = kMargin + kHeaderHeight + kPanelGap + kSectionHeight + kPanelGap + kRecordHeight + kMargin;

constexpr int kRecordButtonWidth = 110;
constexpr int kButtonHeight = 30;
constexpr int kIconWidth = 44;      // the page that is dragged out is 44 by 56
constexpr int kIconHeight = 56;
constexpr int kOpenFolderWidth = 130;
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

    setUpSelector (key, "Key", *processor.keyParam);
    setUpSelector (scale, "Scale", *processor.scaleParam);
    setUpChoice (playMode, "Play mode", *processor.playModeParam);
    setUpChoice (octaveNote, "Octave note", *processor.octaveNoteParam);
    setUpSelector (arpDirection, "Arp direction", *processor.arpDirectionParam);
    setUpKnob (lowOctave, "Low octave", *processor.lowOctaveParam);
    setUpKnob (octaves, "Octaves", *processor.octavesParam);
    setUpToggle (midiRoot, "MIDI Root", *processor.midiRootParam);
    midiRoot.button.setTooltip ("The next MIDI note received sets the root note (in place of Key) until another one arrives.");

    setUpSelector (stepRate, "Step rate", *processor.stepRateParam);
    setUpChoice (rhythm, "Rhythm", *processor.rhythmParam);

    // The value text ("Off", "4.5 beats", "Auto") and what is typed into the value box are handled by the
    // parameters through the slider attachments, so the host shows the same text
    setUpKnob (euclidSteps, "Pattern length", *processor.euclidStepsParam);
    setUpKnob (euclidPulses, "Pulses", *processor.euclidPulsesParam);
    setUpKnob (euclidRotation, "Rotation", *processor.euclidRotationParam);
    setUpKnob (cycleLength, "Cycle length", *processor.cycleLengthParam);
    setUpKnob (swing, "Swing", *processor.swingParam);
    swing.slider.setTextValueSuffix (" %");

    setUpSelector (noteLength, "Note length", *processor.noteLengthParam);
    setUpKnob (lengthVariation, "Length var.", *processor.lengthVariationParam);
    lengthVariation.slider.setTextValueSuffix (" %");
    setUpKnob (velocity, "Velocity", *processor.velocityParam);
    setUpKnob (velocityVariation, "Velocity +/-", *processor.velocityVariationParam);
    setUpKnob (chance, "Chance", *processor.chanceParam);
    chance.slider.setTextValueSuffix (" %");
    setUpKnob (channel, "Channel", *processor.channelParam);
    setUpKnob (seed, "Seed", *processor.seedParam);

    record.button.setButtonText ("Record");
    record.button.setComponentID (processor.recordParam->getParameterID());
    record.button.getProperties().set (DarkLookAndFeel::recordProperty, true);
    record.button.setClickingTogglesState (true); // the attachment does not do it, and a click would set the parameter back
    record.attachment = std::make_unique<juce::ButtonParameterAttachment> (*processor.recordParam, record.button);
    addAndMakeVisible (record.button);
    setUpKnob (recordBars, "Bars", *processor.recordBarsParam);

    takeStatus.setJustificationType (juce::Justification::centredLeft);
    takeStatus.setFont (look.labelFont (13.5f));
    addAndMakeVisible (takeStatus);
    takeIcon.setComponentID ("takeIcon");
    takeIcon.setTooltip ("Drag the take into your host. Not every host accepts a file dragged from a plugin: "
                         "if nothing happens, use Open folder and drag the file from there.");
    addAndMakeVisible (takeIcon);

    // Dragging the page out does not work in every host (Bitwig on Linux does not take it), and a file manager does
    openFolder.setButtonText ("Open folder");
    openFolder.setComponentID ("openFolder");
    openFolder.setTooltip ("Opens the folder with the takes. This works in every host: drag the file from there into it.");
    openFolder.onClick = [] { SeqVerseProcessor::getTakeFolder().revealToUser(); };
    addAndMakeVisible (openFolder);

    // The attachments notify these on host changes (automation) as well as on changes made here
    record.button.onClick = [this] { updateEnabledControls(); showTake(); };
    playMode.buttons.onChange = [this] { updateEnabledControls(); };
    rhythm.buttons.onChange = [this] { updateEnabledControls(); };
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

    // Record is armed while the host is stopped: it decides what the next start does, and a take switches it off again
    shownPlaying = processor.isHostPlaying();
    record.button.setEnabled (! shownPlaying);
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
    shownArmed = processor.recordParam->get();
}

void SeqVerseEditor::timerCallback()
{
    // Record can be switched off by the plugin when a take ends, and the host can start and stop at any time
    if (processor.isHostPlaying() != shownPlaying || processor.recordParam->get() != shownArmed)
    {
        updateEnabledControls();
        showTake();
    }

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

void SeqVerseEditor::setUpCaption (juce::Label& label, const juce::String& text, juce::Justification justification)
{
    label.setText (text.toUpperCase(), juce::dontSendNotification);
    label.setFont (look.captionFont());
    label.setJustificationType (justification);
    label.setMinimumHorizontalScale (0.7f);
    label.setBorderSize ({});
    addAndMakeVisible (label);
}

void SeqVerseEditor::setUpSelector (Selector& selector, const juce::String& text, juce::AudioParameterChoice& parameter)
{
    setUpCaption (selector.label, text, juce::Justification::centredLeft);

    selector.box.setComponentID (parameter.getParameterID());
    selector.box.addItemList (parameter.choices, 1);
    addAndMakeVisible (selector.box);

    selector.attachment = std::make_unique<juce::ComboBoxParameterAttachment> (parameter, selector.box);
}

void SeqVerseEditor::setUpChoice (Choice& choice, const juce::String& text, juce::AudioParameterChoice& parameter)
{
    setUpCaption (choice.label, text, juce::Justification::centredLeft);

    choice.buttons.setComponentID (parameter.getParameterID());
    choice.buttons.setParameter (parameter);
    addAndMakeVisible (choice.buttons);
}

void SeqVerseEditor::setUpKnob (Setting& knob, const juce::String& text, juce::RangedAudioParameter& parameter)
{
    setUpCaption (knob.label, text, juce::Justification::centred);

    knob.slider.setComponentID (parameter.getParameterID());
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, kKnobWidth, kValueHeight);
    addAndMakeVisible (knob.slider);

    knob.attachment = std::make_unique<juce::SliderParameterAttachment> (parameter, knob.slider);
    knob.slider.setDoubleClickReturnValue (true, parameter.convertFrom0to1 (parameter.getDefaultValue()));
}

void SeqVerseEditor::setUpToggle (Toggle& toggle, const juce::String& text, juce::AudioParameterBool& parameter)
{
    setUpCaption (toggle.label, text, juce::Justification::centred);

    toggle.button.setComponentID (parameter.getParameterID());
    addAndMakeVisible (toggle.button);

    toggle.attachment = std::make_unique<juce::ButtonParameterAttachment> (parameter, toggle.button);
}

void SeqVerseEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    look.drawHeader (g, header, SEQVERSE_VERSION);
    for (const auto& panel : panels)
        look.drawPanel (g, panel.area, panel.heading, kPadding, kHeadingHeight);
}

void SeqVerseEditor::resized()
{
    auto area = getLocalBounds().reduced (kMargin);
    header = area.removeFromTop (kHeaderHeight);
    area.removeFromTop (kPanelGap);
    panels.clear();

    // A panel with its heading, and what is inside it
    const auto panel = [&] (const juce::String& heading, juce::Rectangle<int> bounds)
    {
        panels.push_back ({ heading, bounds });
        return bounds.reduced (kPadding).withTrimmedTop (kHeadingHeight);
    };

    // Rows of fields and knobs, the first at the top of the panel and the last at its bottom
    const auto rows = [] (juce::Rectangle<int> content, std::initializer_list<int> heights)
    {
        int total = 0;
        for (int h : heights)
            total += h;
        const int gap = heights.size() > 1 ? (content.getHeight() - total) / (int) (heights.size() - 1) : 0;
        std::vector<juce::Rectangle<int>> result;
        for (int h : heights)
        {
            result.push_back (content.removeFromTop (h));
            content.removeFromTop (gap);
        }
        return result;
    };

    // A control with its name above it
    const auto place = [] (juce::Rectangle<int> cell, juce::Label& label, juce::Component& control)
    {
        label.setBounds (cell.removeFromTop (kCaptionHeight));
        control.setBounds (cell);
    };
    const auto placeSelector = [&] (juce::Rectangle<int> cell, Selector& selector) { place (cell, selector.label, selector.box); };
    const auto placeChoice = [&] (juce::Rectangle<int> cell, Choice& choice) { place (cell, choice.label, choice.buttons); };
    const auto knobCell = [] (juce::Rectangle<int> row, int index) { return row.withX (row.getX() + index * kKnobWidth).withWidth (kKnobWidth); };
    const auto placeKnob = [&] (juce::Rectangle<int> row, int index, Setting& knob) { place (knobCell (row, index), knob.label, knob.slider); };

    // Two fields side by side: a narrow one and the rest, or two halves
    const auto split = [] (juce::Rectangle<int> row, int leftWidth)
    {
        auto left = row.removeFromLeft (leftWidth);
        row.removeFromLeft (kFieldGap);
        return std::pair { left, row };
    };
    const int halfWidth = (kInnerWidth - kFieldGap) / 2;

    auto sections = area.removeFromTop (kSectionHeight);
    const auto nextSection = [&] (const juce::String& heading)
    {
        auto bounds = sections.removeFromLeft (kPanelWidth);
        sections.removeFromLeft (kPanelGap);
        return panel (heading, bounds);
    };

    {
        const auto r = rows (nextSection ("Pitch"), { kFieldRow, kFieldRow, kFieldRow, kKnobRow });
        const auto [keyCell, scaleCell] = split (r[0], kNarrowWidth);
        placeSelector (keyCell, key);
        placeSelector (scaleCell, scale);
        const auto [playModeCell, octaveNoteCell] = split (r[1], halfWidth);
        placeChoice (playModeCell, playMode);
        placeChoice (octaveNoteCell, octaveNote);
        placeSelector (r[2], arpDirection);
        placeKnob (r[3], 0, lowOctave);
        placeKnob (r[3], 1, octaves);
        auto rootCell = knobCell (r[3], 2);
        midiRoot.label.setBounds (rootCell.removeFromTop (kCaptionHeight));
        midiRoot.button.setBounds (rootCell.removeFromTop (kKnobHeight - kValueHeight).withSizeKeepingCentre (kSwitchWidth, 22));
    }

    {
        const auto r = rows (nextSection ("Rhythm"), { kFieldRow, kKnobRow, kKnobRow });
        const auto [stepRateCell, rhythmCell] = split (r[0], kNarrowWidth);
        placeSelector (stepRateCell, stepRate);
        placeChoice (rhythmCell, rhythm);
        placeKnob (r[1], 0, euclidSteps);
        placeKnob (r[1], 1, euclidPulses);
        placeKnob (r[1], 2, euclidRotation);
        placeKnob (r[2], 0, cycleLength);
        placeKnob (r[2], 1, swing);
    }

    {
        const auto r = rows (nextSection ("Feel"), { kFieldRow, kKnobRow, kKnobRow });
        placeSelector (r[0].withWidth (kNarrowWidth), noteLength);
        placeKnob (r[1], 0, velocity);
        placeKnob (r[1], 1, velocityVariation);
        placeKnob (r[1], 2, chance);
        placeKnob (r[2], 0, lengthVariation);
        placeKnob (r[2], 1, channel);
        placeKnob (r[2], 2, seed);
    }

    // Record: the button, the number of bars, the page with the last take, what is happening, and the folder
    area.removeFromTop (kPanelGap);
    auto band = area.removeFromTop (kRecordHeight);
    panels.push_back ({ {}, band });
    band.reduce (kPadding, kRecordPadding);
    const auto middle = [&] (juce::Rectangle<int> cell, int height) { return cell.withSizeKeepingCentre (cell.getWidth(), height); };

    record.button.setBounds (middle (band.removeFromLeft (kRecordButtonWidth), kButtonHeight));
    band.removeFromLeft (kFieldGap);
    place (band.removeFromLeft (kKnobWidth), recordBars.label, recordBars.slider);
    band.removeFromLeft (kFieldGap);
    takeIcon.setBounds (middle (band.removeFromLeft (kIconWidth), kIconHeight));
    band.removeFromLeft (kFieldGap * 2);
    openFolder.setBounds (middle (band.removeFromRight (kOpenFolderWidth), kButtonHeight));
    band.removeFromRight (kFieldGap);
    takeStatus.setBounds (band);

    repaint(); // the panels have moved
}

void SegmentedChoice::setParameter (juce::AudioParameterChoice& parameter)
{
    for (int i = 0; i < parameter.choices.size(); ++i)
    {
        auto* button = buttons.add (new juce::TextButton (parameter.choices[i]));
        button->onClick = [this, i] { attachment->setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (button);
    }

    attachment = std::make_unique<juce::ParameterAttachment> (parameter, [this] (float value) { showIndex (juce::roundToInt (value)); });
    attachment->sendInitialUpdate();
}

void SegmentedChoice::showIndex (int index)
{
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (i == index, juce::dontSendNotification);

    if (onChange != nullptr)
        onChange();
}

void SegmentedChoice::resized()
{
    constexpr int gap = 5;
    const int n = buttons.size();
    const int w = (getWidth() - gap * (n - 1)) / n;
    for (int i = 0; i < n; ++i)
        buttons[i]->setBounds (i * (w + gap), 0, i == n - 1 ? getWidth() - i * (w + gap) : w, getHeight());
}
