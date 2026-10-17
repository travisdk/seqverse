// Design prototype: draws the same six sample controls in several knob styles and saves PNG snapshots, so that a
// style can be chosen by looking at it. Nothing here is part of the plugin.
//
//   KnobStyles <output directory>      writes knob_styles_all.png and one PNG per style

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

namespace
{
constexpr float pi = juce::MathConstants<float>::pi;

const juce::Colour lightBackground (0xffadd8e6); // what the editor uses today
const juce::Colour ink (0xff14181c);
const juce::Colour accent (0xff0f5c85);

juce::Point<float> onCircle (juce::Point<float> centre, float radius, float angle)
{
    return { centre.x + radius * std::sin (angle), centre.y - radius * std::cos (angle) };
}

struct Geometry
{
    juce::Point<float> centre;
    float radius;
    float angle; // of the current value, from 12 o'clock, clockwise
    float alpha; // 1, or less for a disabled control
};

Geometry geometryOf (int x, int y, int width, int height, float position, float start, float end, const juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (6.0f);
    return { bounds.getCentre(), juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f, start + position * (end - start),
             slider.isEnabled() ? 1.0f : 0.35f };
}

juce::Path arc (juce::Point<float> centre, float radius, float from, float to)
{
    juce::Path path;
    path.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, from, to, true);
    return path;
}

// B: a thin track with the value drawn over it in the accent colour, and a light thumb at the end of it
class FilledArcLook : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position, float start, float end, juce::Slider& slider) override
    {
        const auto geo = geometryOf (x, y, width, height, position, start, end, slider);
        const float r = geo.radius - 5.0f;

        g.setColour (ink.withAlpha (0.18f * geo.alpha));
        g.strokePath (arc (geo.centre, r, start, end), juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (accent.withAlpha (geo.alpha));
        g.strokePath (arc (geo.centre, r, start, geo.angle), juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto thumb = onCircle (geo.centre, r, geo.angle);
        g.setColour (juce::Colours::white.withAlpha (geo.alpha));
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (thumb));
        g.setColour (accent.withAlpha (geo.alpha));
        g.drawEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (thumb), 2.0f);
    }
};

// C: a hardware-style knob: a shaded body with a pointer, and tick marks around it
class HardwareLook : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position, float start, float end, juce::Slider& slider) override
    {
        const auto geo = geometryOf (x, y, width, height, position, start, end, slider);
        const float body = geo.radius * 0.66f;

        for (int i = 0; i <= 10; ++i)
        {
            const float a = start + (end - start) * (float) i / 10.0f;
            const bool major = i == 0 || i == 5 || i == 10;
            g.setColour (ink.withAlpha ((major ? 0.75f : 0.45f) * geo.alpha));
            g.drawLine ({ onCircle (geo.centre, geo.radius * (major ? 0.80f : 0.84f), a), onCircle (geo.centre, geo.radius * 0.95f, a) },
                        major ? 2.0f : 1.4f);
        }

        juce::Path shape;
        shape.addEllipse (geo.centre.x - body, geo.centre.y - body, body * 2.0f, body * 2.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.35f * geo.alpha), 7, { 0, 3 }).drawForPath (g, shape);

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff7f9fa).withAlpha (geo.alpha), geo.centre.x, geo.centre.y - body,
                                                 juce::Colour (0xffaebbc4).withAlpha (geo.alpha), geo.centre.x, geo.centre.y + body, false));
        g.fillPath (shape);
        g.setColour (ink.withAlpha (0.55f * geo.alpha));
        g.strokePath (shape, juce::PathStrokeType (1.2f));

        g.setColour (ink.withAlpha (geo.alpha));
        g.drawLine ({ onCircle (geo.centre, body * 0.30f, geo.angle), onCircle (geo.centre, body * 0.92f, geo.angle) }, 3.6f);
    }
};

// D: a dark theme with a thin ring and a glowing value arc
class DarkLook : public juce::LookAndFeel_V4
{
public:
    static const juce::Colour glow;

    DarkLook()
    {
        setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffe6edf1));
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff3a434c));
        setColour (juce::Label::textColourId, juce::Colour (0xffaab6bf));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position, float start, float end, juce::Slider& slider) override
    {
        const auto geo = geometryOf (x, y, width, height, position, start, end, slider);
        const float r = geo.radius - 6.0f;

        g.setColour (juce::Colour (0xff2c343b).withAlpha (geo.alpha));
        g.fillEllipse (juce::Rectangle<float> (r * 1.5f, r * 1.5f).withCentre (geo.centre));

        g.setColour (juce::Colour (0xff3a434c).withAlpha (geo.alpha));
        g.strokePath (arc (geo.centre, r, start, end), juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto value = arc (geo.centre, r, start, geo.angle);
        g.setColour (glow.withAlpha (0.22f * geo.alpha));
        g.strokePath (value, juce::PathStrokeType (10.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (glow.withAlpha (geo.alpha));
        g.strokePath (value, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (juce::Colours::white.withAlpha (geo.alpha));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (onCircle (geo.centre, r, geo.angle)));
    }
};
const juce::Colour DarkLook::glow (0xff3dd6c6);

// E: a thick ring with the value printed inside the knob
class ValueInsideLook : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position, float start, float end, juce::Slider& slider) override
    {
        const auto geo = geometryOf (x, y, width, height, position, start, end, slider);
        const float r = geo.radius - 6.0f;

        g.setColour (ink.withAlpha (0.14f * geo.alpha));
        g.strokePath (arc (geo.centre, r, start, end), juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        g.setColour (accent.withAlpha (geo.alpha));
        g.strokePath (arc (geo.centre, r, start, geo.angle), juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

        g.setColour (ink.withAlpha (geo.alpha));
        g.setFont (juce::Font (juce::FontOptions (r * 0.36f, juce::Font::bold)));
        g.drawFittedText (slider.getTextFromValue (slider.getValue()), juce::Rectangle<float> (r * 1.5f, r * 0.9f).withCentre (geo.centre).toNearestInt(),
                          juce::Justification::centred, 2, 0.8f);
    }
};

// F: an arc of dots, lit up to the value
class DotsLook : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position, float start, float end, juce::Slider& slider) override
    {
        const auto geo = geometryOf (x, y, width, height, position, start, end, slider);
        constexpr int dots = 17;
        const float r = geo.radius - 6.0f;

        for (int i = 0; i < dots; ++i)
        {
            const float p = (float) i / (float) (dots - 1);
            const bool lit = p <= position + 0.001f;
            g.setColour ((lit ? accent : ink.withAlpha (0.22f)).withMultipliedAlpha (geo.alpha));
            g.fillEllipse (juce::Rectangle<float> (lit ? 8.0f : 6.0f, lit ? 8.0f : 6.0f).withCentre (onCircle (geo.centre, r, start + p * (end - start))));
        }

        g.setColour (ink.withAlpha (geo.alpha));
        g.drawLine ({ onCircle (geo.centre, r * 0.22f, geo.angle), onCircle (geo.centre, r * 0.62f, geo.angle) }, 3.0f);
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (geo.centre));
    }
};

// The six controls every style is shown with: a nearly full one, a low one, one at its minimum, a whole-number one,
// text with units, and a disabled one
struct Sample
{
    const char* name;
    double minimum, maximum, value;
    std::function<juce::String (double)> text;
    bool enabled;
};

std::vector<Sample> samples()
{
    static const char* const lengths[] = { "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" };
    return {
        { "Chance", 0, 100, 85, [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; }, true },
        { "Note length", 0, 5, 1, [] (double v) { return juce::String (lengths[juce::jlimit (0, 5, juce::roundToInt (v))]); }, true },
        { "Swing", 0, 100, 0, [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; }, true },
        { "Velocity", 1, 127, 64, [] (double v) { return juce::String (juce::roundToInt (v)); }, true },
        { "Cycle length", 0, 128, 9, [] (double v) { return v < 1.0 ? juce::String ("Endless") : juce::String (v * 0.5, 1) + " beats"; }, true },
        { "Seed", 0, 9999, 0, [] (double v) { return v < 1.0 ? juce::String ("Auto") : juce::String (juce::roundToInt (v)); }, false },
    };
}

class StyleRow : public juce::Component
{
public:
    StyleRow (const juce::String& title, juce::LookAndFeel& look, juce::Colour backgroundColour, juce::Colour textColour, bool textBoxBelow)
        : background (backgroundColour)
    {
        setLookAndFeel (&look);

        titleLabel.setText (title, juce::dontSendNotification);
        titleLabel.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
        titleLabel.setColour (juce::Label::textColourId, textColour);
        addAndMakeVisible (titleLabel);

        for (const auto& sample : samples())
        {
            auto knob = std::make_unique<Knob>();
            knob->label.setText (sample.name, juce::dontSendNotification);
            knob->label.setJustificationType (juce::Justification::centred);
            knob->label.setColour (juce::Label::textColourId, textColour);
            knob->label.setEnabled (sample.enabled);
            addAndMakeVisible (knob->label);

            knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            knob->slider.setRange (sample.minimum, sample.maximum, 1.0);
            knob->slider.setValue (sample.value, juce::dontSendNotification);
            knob->slider.textFromValueFunction = sample.text;
            knob->slider.setTextBoxStyle (textBoxBelow ? juce::Slider::TextBoxBelow : juce::Slider::NoTextBox, false, 80, 22);
            knob->slider.setColour (juce::Slider::textBoxTextColourId, textColour);
            knob->slider.setColour (juce::Slider::textBoxOutlineColourId, textColour.withAlpha (0.35f));
            knob->slider.setEnabled (sample.enabled);
            knob->slider.updateText(); // the text box was filled before the text function was set
            addAndMakeVisible (knob->slider);

            knobs.push_back (std::move (knob));
        }
    }

    ~StyleRow() override { setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override { g.fillAll (background); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16, 8);
        titleLabel.setBounds (area.removeFromTop (24));
        const int width = area.getWidth() / (int) knobs.size();
        for (auto& knob : knobs)
        {
            auto cell = area.removeFromLeft (width);
            knob->label.setBounds (cell.removeFromTop (20));
            knob->slider.setBounds (cell);
        }
    }

private:
    struct Knob
    {
        juce::Label label;
        juce::Slider slider;
    };

    juce::Colour background;
    juce::Label titleLabel;
    std::vector<std::unique_ptr<Knob>> knobs;
};

void save (juce::Component& component, const juce::File& file)
{
    const auto image = component.createComponentSnapshot (component.getLocalBounds(), true, 2.0f);
    file.deleteFile();
    if (auto stream = file.createOutputStream())
        juce::PNGImageFormat().writeImageToStream (image, *stream);
}
}

int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf (stderr, "usage: %s <output directory>\n", argv[0]);
        return 2;
    }

    const juce::ScopedJuceInitialiser_GUI gui;
    const juce::File directory (argv[1]);
    directory.createDirectory();

    juce::LookAndFeel_V4 currentLook; // what the plugin has now
    FilledArcLook filledArcLook;
    HardwareLook hardwareLook;
    DarkLook darkLook;
    ValueInsideLook valueInsideLook;
    DotsLook dotsLook;

    const juce::Colour darkBackground (0xff20252b);
    const juce::Colour lightText = juce::Colour (0xffe6edf1);

    struct Entry
    {
        const char* file;
        std::unique_ptr<StyleRow> row;
    };

    std::vector<Entry> entries;
    entries.push_back ({ "A_current", std::make_unique<StyleRow> ("A  Current (JUCE default look)", currentLook, lightBackground, ink, true) });
    entries.push_back ({ "B_filled_arc", std::make_unique<StyleRow> ("B  Filled arc: thin track, value arc, light thumb", filledArcLook, lightBackground, ink, true) });
    entries.push_back ({ "C_hardware", std::make_unique<StyleRow> ("C  Hardware: shaded body, pointer, tick marks", hardwareLook, lightBackground, ink, true) });
    entries.push_back ({ "D_dark", std::make_unique<StyleRow> ("D  Dark theme: thin ring, glowing value arc", darkLook, darkBackground, lightText, true) });
    entries.push_back ({ "E_value_inside", std::make_unique<StyleRow> ("E  Value inside: thick ring, value in the middle", valueInsideLook, lightBackground, ink, false) });
    entries.push_back ({ "F_dots", std::make_unique<StyleRow> ("F  Dots: lit dots up to the value, pointer", dotsLook, lightBackground, ink, true) });

    constexpr int width = 660;
    constexpr int rowHeight = 205;

    juce::Component page;
    page.setSize (width, rowHeight * (int) entries.size());

    int y = 0;
    for (auto& entry : entries)
    {
        entry.row->setBounds (0, y, width, rowHeight);
        page.addAndMakeVisible (*entry.row);
        y += rowHeight;

        // each style on its own as well, as a component of its own size
        auto alone = juce::Rectangle<int> (0, 0, width, rowHeight);
        juce::Image image = entry.row->createComponentSnapshot (alone, true, 2.0f);
        const auto file = directory.getChildFile (juce::String ("knob_style_") + entry.file + ".png");
        file.deleteFile();
        if (auto stream = file.createOutputStream())
            juce::PNGImageFormat().writeImageToStream (image, *stream);
    }

    save (page, directory.getChildFile ("knob_styles_all.png"));

    for (auto& entry : entries)
        page.removeChildComponent (entry.row.get());

    std::printf ("wrote %d styles to %s\n", (int) entries.size(), directory.getFullPathName().toRawUTF8());
    return 0;
}
