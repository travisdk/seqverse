// Design prototype, not part of the plugin: lays out the real controls (names, ranges and default-ish values) in the real
// dark theme in other arrangements, with linear sliders, and saves PNG snapshots so that a layout can be chosen by looking
// at it. Nothing here is connected to the processor.
//
//   LayoutMockup <output directory>      writes layout_A_hybrid.png and layout_B_all_linear.png

#include "../src/DarkLookAndFeel.h"

#include <cstdio>
#include <functional>
#include <vector>

namespace
{
// One picture: the controls are created where they are wanted, and section headings are painted.
class Sheet : public juce::Component
{
public:
    void header (const juce::String& text, juce::Rectangle<int> area) { headers.push_back ({ text, area }); }

    void title (juce::Rectangle<int> area)
    {
        auto* label = addLabel ("SeqVerse", area, juce::Justification::centredLeft);
        label->setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
        label->setColour (juce::Label::textColourId, theme::text);
    }

    // A dropdown that shows its current value, with its name to the left (row) or above it
    void combo (const juce::String& name, const juce::String& value, juce::Rectangle<int> area, bool labelLeft, int labelWidth, bool enabled = true)
    {
        auto cell = area;
        auto labelArea = labelLeft ? cell.removeFromLeft (labelWidth) : cell.removeFromTop (20);
        addLabel (name, labelArea, labelLeft ? juce::Justification::centredLeft : juce::Justification::centredLeft)->setEnabled (enabled);

        auto* box = add<juce::ComboBox>();
        box->addItem (value, 1);
        box->setSelectedId (1, juce::dontSendNotification);
        box->setBounds (cell.removeFromTop (26));
        box->setEnabled (enabled);
    }

    // A horizontal slider with its value in a box at the right and its name to the left
    void linear (const juce::String& name, double min, double max, double value, juce::Rectangle<int> area, int labelWidth,
                 const juce::String& suffix = {}, bool enabled = true, std::function<juce::String (double)> text = nullptr)
    {
        auto cell = area.withHeight (26);
        addLabel (name, cell.removeFromLeft (labelWidth), juce::Justification::centredLeft)->setEnabled (enabled);

        auto* slider = add<juce::Slider>();
        slider->setSliderStyle (juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 22);
        slider->setRange (min, max, 1.0);
        slider->setTextValueSuffix (suffix);
        if (text != nullptr)
            slider->textFromValueFunction = text;
        slider->setValue (value, juce::dontSendNotification);
        slider->updateText(); // the text function may have been set after the value
        slider->setBounds (cell);
        slider->setEnabled (enabled);
    }

    // The rotary knob of the current editor, with its name above and value below
    void knob (const juce::String& name, double min, double max, double value, juce::Rectangle<int> area,
               const juce::String& suffix = {}, bool enabled = true, std::function<juce::String (double)> text = nullptr)
    {
        auto cell = area;
        addLabel (name, cell.removeFromTop (20), juce::Justification::centred)->setEnabled (enabled);

        auto* slider = add<juce::Slider>();
        slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 22);
        slider->setRange (min, max, 1.0);
        slider->setTextValueSuffix (suffix);
        if (text != nullptr)
            slider->textFromValueFunction = text;
        slider->setValue (value, juce::dontSendNotification);
        slider->updateText();
        slider->setBounds (cell);
        slider->setEnabled (enabled);
    }

    void footerBadge (juce::Rectangle<int> area) { badge = area; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::background);

        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        for (const auto& h : headers)
        {
            g.setColour (theme::accent);
            g.drawText (h.text.toUpperCase(), h.area, juce::Justification::centredLeft);
            const int textWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)), h.text.toUpperCase())) + 10;
            g.setColour (theme::outline);
            g.drawHorizontalLine (h.area.getCentreY(), (float) h.area.getX() + (float) textWidth, (float) h.area.getRight());
        }

        if (! badge.isEmpty())
        {
            g.setColour (theme::outline);
            g.drawRoundedRectangle (badge.toFloat().reduced (0.5f), 8.0f, 1.0f);
            g.setColour (theme::dimText.withAlpha (0.6f));
            g.setFont (11.0f);
            g.drawText ("space for the JUCE badge", badge, juce::Justification::centred);
        }
    }

private:
    struct Header { juce::String text; juce::Rectangle<int> area; };

    template <typename T>
    T* add()
    {
        auto* component = new T();
        owned.add (component);
        addAndMakeVisible (component);
        return component;
    }

    juce::Label* addLabel (const juce::String& text, juce::Rectangle<int> area, juce::Justification justification)
    {
        auto* label = add<juce::Label>();
        label->setText (text, juce::dontSendNotification);
        label->setJustificationType (justification);
        label->setBounds (area);
        return label;
    }

    juce::OwnedArray<juce::Component> owned;
    std::vector<Header> headers;
    juce::Rectangle<int> badge;
};

const auto cycleText = [] (double v) { return v <= 0.0 ? juce::String ("Off") : juce::String (v * 0.5, v > 0 && ((int) v % 2) != 0 ? 1 : 0) + " beats"; };

constexpr int margin = 16;
constexpr int rowHeight = 34;       // a control is 26 pixels high, with the gap to the next row
constexpr int headerHeight = 26;
constexpr int footerHeight = 64;    // the JUCE badge, at the bottom right

// A: two columns of rows for the pitch and the rhythm (dropdowns and linear sliders), and the settings that are played
// with by ear, the "feel", as knobs in a band below.
void layoutHybrid (Sheet& sheet)
{
    constexpr int width = 660;
    constexpr int columnGap = 24;
    const int columnWidth = (width - 2 * margin - columnGap) / 2;
    const int labelWidth = 92;

    int y = margin;
    sheet.title ({ margin, y, width - 2 * margin, 36 });
    y += 44;

    const int left = margin, right = margin + columnWidth + columnGap;
    sheet.header ("Pitch", { left, y, columnWidth, headerHeight });
    sheet.header ("Rhythm", { right, y, columnWidth, headerHeight });
    y += headerHeight + 4;

    const auto row = [&] (int column, int index) { return juce::Rectangle<int> (column, y + index * rowHeight, columnWidth, 26); };

    sheet.combo ("Key", "C", row (left, 0), true, labelWidth);
    sheet.combo ("Scale", "Major (Ionian)", row (left, 1), true, labelWidth);
    sheet.combo ("Octave note", "Include", row (left, 2), true, labelWidth);
    sheet.linear ("Low octave", 0, 7, 4, row (left, 3), labelWidth);
    sheet.linear ("Octaves", 1, 8, 1, row (left, 4), labelWidth);
    sheet.combo ("Play mode", "Arpeggio", row (left, 5), true, labelWidth);
    sheet.combo ("Arp direction", "Up", row (left, 6), true, labelWidth);

    sheet.combo ("Step rate", "1/8", row (right, 0), true, labelWidth);
    sheet.combo ("Rhythm", "Euclidean", row (right, 1), true, labelWidth);
    sheet.linear ("Pattern length", 2, 32, 8, row (right, 2), labelWidth);
    sheet.linear ("Pulses", 0, 32, 5, row (right, 3), labelWidth);
    sheet.linear ("Rotation", 0, 31, 0, row (right, 4), labelWidth);
    sheet.linear ("Cycle length", 0, 128, 0, row (right, 5), labelWidth, {}, true, cycleText);
    sheet.linear ("Swing", 0, 100, 0, row (right, 6), labelWidth, " %");
    y += 7 * rowHeight + 10;

    sheet.header ("Feel", { margin, y, width - 2 * margin, headerHeight });
    y += headerHeight + 2;

    const int knobWidth = 88;
    const int knobHeight = 118;
    const char* names[] = { "Chance", "Note length", "Length var.", "Velocity", "Velocity +/-" };
    const double values[] = { 100, 1, 0, 100, 0 };
    const double maxima[] = { 100, 5, 100, 127, 64 };
    const char* suffixes[] = { " %", "", " %", "", "" };
    for (int i = 0; i < 5; ++i)
    {
        std::function<juce::String (double)> text;
        if (i == 1)
            text = [] (double v) { static const char* lengths[] = { "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" }; return juce::String (lengths[(int) v]); };
        sheet.knob (names[i], i == 3 ? 1 : 0, maxima[i], values[i], { margin + i * knobWidth, y, knobWidth, knobHeight }, suffixes[i], true, text);
    }

    // Channel and the seed are whole numbers that are set once: sliders, next to the knobs
    const int setupX = margin + 5 * knobWidth + 20;
    const juce::Rectangle<int> setupColumn (setupX, y + 20, width - margin - setupX, 26);
    sheet.linear ("Channel", 1, 16, 1, setupColumn, 54);
    sheet.linear ("Seed", 0, 9999, 0, setupColumn.translated (0, rowHeight), 54, {}, false, [] (double v) { return v <= 0.0 ? juce::String ("Auto") : juce::String ((int) v); });

    y += knobHeight + 6;
    sheet.footerBadge ({ width - margin - 150, y, 150, footerHeight - 6 });
    sheet.setSize (width, y + footerHeight);
}

// B: everything as rows, in three columns of seven: dropdowns and linear sliders only.
void layoutAllLinear (Sheet& sheet)
{
    constexpr int width = 780;
    constexpr int columnGap = 20;
    const int columnWidth = (width - 2 * margin - 2 * columnGap) / 3;
    const int labelWidth = 92;

    int y = margin;
    sheet.title ({ margin, y, width - 2 * margin, 36 });
    y += 44;

    const int columns[3] = { margin, margin + columnWidth + columnGap, margin + 2 * (columnWidth + columnGap) };
    sheet.header ("Pitch", { columns[0], y, columnWidth, headerHeight });
    sheet.header ("Rhythm", { columns[1], y, columnWidth, headerHeight });
    sheet.header ("Feel", { columns[2], y, columnWidth, headerHeight });
    y += headerHeight + 4;

    const auto row = [&] (int column, int index) { return juce::Rectangle<int> (columns[column], y + index * rowHeight, columnWidth, 26); };

    sheet.combo ("Key", "C", row (0, 0), true, labelWidth);
    sheet.combo ("Scale", "Major (Ionian)", row (0, 1), true, labelWidth);
    sheet.combo ("Octave note", "Include", row (0, 2), true, labelWidth);
    sheet.linear ("Low octave", 0, 7, 4, row (0, 3), labelWidth);
    sheet.linear ("Octaves", 1, 8, 1, row (0, 4), labelWidth);
    sheet.combo ("Play mode", "Arpeggio", row (0, 5), true, labelWidth);
    sheet.combo ("Arp direction", "Up", row (0, 6), true, labelWidth);

    sheet.combo ("Step rate", "1/8", row (1, 0), true, labelWidth);
    sheet.combo ("Rhythm", "Euclidean", row (1, 1), true, labelWidth);
    sheet.linear ("Pattern length", 2, 32, 8, row (1, 2), labelWidth);
    sheet.linear ("Pulses", 0, 32, 5, row (1, 3), labelWidth);
    sheet.linear ("Rotation", 0, 31, 0, row (1, 4), labelWidth);
    sheet.linear ("Cycle length", 0, 128, 0, row (1, 5), labelWidth, {}, true, cycleText);
    sheet.linear ("Swing", 0, 100, 0, row (1, 6), labelWidth, " %");

    sheet.combo ("Note length", "1/16", row (2, 0), true, labelWidth);
    sheet.linear ("Length var.", 0, 100, 0, row (2, 1), labelWidth, " %");
    sheet.linear ("Velocity", 1, 127, 100, row (2, 2), labelWidth);
    sheet.linear ("Velocity +/-", 0, 64, 0, row (2, 3), labelWidth);
    sheet.linear ("Chance", 0, 100, 100, row (2, 4), labelWidth, " %");
    sheet.linear ("Channel", 1, 16, 1, row (2, 5), labelWidth);
    sheet.linear ("Seed", 0, 9999, 0, row (2, 6), labelWidth, {}, false, [] (double v) { return v <= 0.0 ? juce::String ("Auto") : juce::String ((int) v); });

    y += 7 * rowHeight + 8;
    sheet.footerBadge ({ width - margin - 150, y, 150, footerHeight - 6 });
    sheet.setSize (width, y + footerHeight);
}

void save (Sheet& sheet, const juce::File& file)
{
    const auto image = sheet.createComponentSnapshot (sheet.getLocalBounds());
    file.deleteFile();
    if (auto stream = file.createOutputStream())
        juce::PNGImageFormat().writeImageToStream (image, *stream);
    std::printf ("wrote %s (%d x %d)\n", file.getFullPathName().toRawUTF8(), sheet.getWidth(), sheet.getHeight());
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf (stderr, "usage: %s <output directory>\n", argv[0]);
        return 2;
    }

    const juce::ScopedJuceInitialiser_GUI gui;
    const juce::File directory (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    directory.createDirectory();

    DarkLookAndFeel look; // has the linear slider too

    Sheet hybrid;
    hybrid.setLookAndFeel (&look);
    layoutHybrid (hybrid);
    hybrid.sendLookAndFeelChange();
    save (hybrid, directory.getChildFile ("layout_A_hybrid.png"));

    Sheet allLinear;
    allLinear.setLookAndFeel (&look);
    layoutAllLinear (allLinear);
    allLinear.sendLookAndFeelChange();
    save (allLinear, directory.getChildFile ("layout_B_all_linear.png"));

    hybrid.setLookAndFeel (nullptr);
    allLinear.setLookAndFeel (nullptr);
    return 0;
}
