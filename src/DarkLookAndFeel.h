#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

// The dark theme of the editor: dark surfaces, light text, and a teal accent. Knobs are a thin ring with a glowing
// value arc. Everything a component looks like comes from here, so the editor code only lays things out.
namespace theme
{
inline const juce::Colour background (0xff20252b);
inline const juce::Colour surface (0xff2c343b);    // knob hubs, dropdowns
inline const juce::Colour sunken (0xff1a1e23);     // value boxes
inline const juce::Colour menu (0xff262d34);
inline const juce::Colour outline (0xff3a434c);    // rings, box outlines
inline const juce::Colour track (0xff4a5560);      // the track of a horizontal slider: lighter than a ring, it is a thin line
inline const juce::Colour text (0xffe6edf1);
inline const juce::Colour dimText (0xffaab6bf);    // labels above the controls
inline const juce::Colour accent (0xff3dd6c6);     // value arcs, highlights
inline const juce::Colour onAccent (0xff101418);   // text on the accent colour
inline const juce::Colour record (0xfff23a3a);     // the Record button when armed
}

class DarkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DarkLookAndFeel()
    {
        // windowBackground, widgetBackground, menuBackground, outline, defaultText, defaultFill,
        // highlightedText, highlightedFill, menuText
        setColourScheme ({ theme::background, theme::surface, theme::menu, theme::outline, theme::text, theme::accent,
                           theme::onAccent, theme::accent, theme::text });

        setColour (juce::Label::textColourId, theme::dimText);

        setColour (juce::ComboBox::arrowColourId, theme::accent);

        setColour (juce::TextButton::buttonColourId, theme::surface);
        setColour (juce::TextButton::buttonOnColourId, theme::record);
        setColour (juce::TextButton::textColourOffId, theme::text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);

        setColour (juce::ToggleButton::tickColourId, theme::accent);
        setColour (juce::ToggleButton::tickDisabledColourId, theme::outline);

        setColour (juce::Slider::textBoxTextColourId, theme::text);
        setColour (juce::Slider::textBoxBackgroundColourId, theme::sunken);
        setColour (juce::Slider::textBoxOutlineColourId, theme::outline);
        setColour (juce::Slider::textBoxHighlightColourId, theme::accent.withAlpha (0.4f));

        // What is typed into a value box
        setColour (juce::TextEditor::backgroundColourId, theme::sunken);
        setColour (juce::TextEditor::textColourId, theme::text);
        setColour (juce::TextEditor::highlightColourId, theme::accent.withAlpha (0.4f));
        setColour (juce::TextEditor::highlightedTextColourId, theme::text);
        setColour (juce::TextEditor::outlineColourId, theme::outline);
        setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
        setColour (juce::CaretComponent::caretColourId, theme::accent);

        setColour (juce::TooltipWindow::backgroundColourId, theme::menu);
        setColour (juce::TooltipWindow::textColourId, theme::text);
        setColour (juce::TooltipWindow::outlineColourId, theme::outline);
    }

    // A thin ring on a dark hub, with the value drawn as a glowing arc that ends in a small white dot
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position, float start, float end,
                           juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (6.0f);
        const auto centre = bounds.getCentre();
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f - 6.0f;
        const float angle = start + position * (end - start); // from 12 o'clock, clockwise
        const float alpha = slider.isEnabled() ? 1.0f : 0.5f;

        g.setColour (theme::surface.withAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (radius * 1.5f, radius * 1.5f).withCentre (centre));

        const auto arc = [&] (float from, float to)
        {
            juce::Path path;
            path.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, from, to, true);
            return path;
        };
        const auto stroke = [] (float thickness)
        { return juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };

        g.setColour (theme::outline.withAlpha (alpha));
        g.strokePath (arc (start, end), stroke (3.0f));

        const auto value = arc (start, angle);
        g.setColour (theme::accent.withAlpha (0.22f * alpha)); // the glow
        g.strokePath (value, stroke (10.0f));
        g.setColour (theme::accent.withAlpha (alpha));
        g.strokePath (value, stroke (3.5f));

        const juce::Point<float> tip (centre.x + radius * std::sin (angle), centre.y - radius * std::cos (angle));
        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (tip));
    }

    // A sheet of paper with a folded corner and a few notes on it, with `label` at the bottom: the take that can be dragged
    // into the host. Dimmed when there is nothing to drag.
    void drawDocumentIcon (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& label, bool enabled, bool highlighted)
    {
        const float alpha = enabled ? 1.0f : 0.45f;
        const auto page = area.reduced (3.0f);
        const float fold = page.getWidth() * 0.32f;
        const auto stroke = [] (float thickness)
        { return juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };

        juce::Path sheet;
        sheet.startNewSubPath (page.getX(), page.getY());
        sheet.lineTo (page.getRight() - fold, page.getY());
        sheet.lineTo (page.getRight(), page.getY() + fold);
        sheet.lineTo (page.getRight(), page.getBottom());
        sheet.lineTo (page.getX(), page.getBottom());
        sheet.closeSubPath();

        g.setColour ((highlighted && enabled ? theme::outline : theme::surface).withAlpha (alpha));
        g.fillPath (sheet);
        g.setColour ((enabled ? theme::accent : theme::dimText).withAlpha (alpha));
        g.strokePath (sheet, stroke (1.5f));

        juce::Path corner;
        corner.startNewSubPath (page.getRight() - fold, page.getY());
        corner.lineTo (page.getRight() - fold, page.getY() + fold);
        corner.lineTo (page.getRight(), page.getY() + fold);
        g.strokePath (corner, stroke (1.0f));

        // Notes, as in a piano roll
        const float left = page.getX() + page.getWidth() * 0.18f;
        const float unit = page.getWidth() * 0.64f;
        const float rows[3][2] = { { 0.0f, 0.4f }, { 0.25f, 0.45f }, { 0.55f, 0.45f } }; // start and length, in units
        for (int i = 0; i < 3; ++i)
            g.fillRoundedRectangle (left + rows[i][0] * unit, page.getY() + page.getHeight() * (0.44f + 0.11f * (float) i), rows[i][1] * unit, 3.0f, 1.5f);

        g.setFont (juce::Font (10.0f, juce::Font::bold));
        g.drawText (label, page.withTrimmedTop (page.getHeight() * 0.78f), juce::Justification::centred);
    }

    int getSliderThumbRadius (juce::Slider&) override { return 6; }

    // A little air between a horizontal slider and its value box, so that the thumb at the maximum does not touch it
    juce::Slider::SliderLayout getSliderLayout (juce::Slider& slider) override
    {
        auto layout = LookAndFeel_V4::getSliderLayout (slider);
        if (slider.getSliderStyle() == juce::Slider::LinearHorizontal && slider.getTextBoxPosition() == juce::Slider::TextBoxRight)
            layout.sliderBounds.removeFromRight (10);
        return layout;
    }

    // The knob's look in a line: a thin track, a glowing value part and a white dot
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, slider);
            return;
        }

        const float alpha = slider.isEnabled() ? 1.0f : 0.5f;
        const float centreY = (float) y + (float) height * 0.5f;
        const auto stroke = [] (float thickness)
        { return juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };

        // The track runs between the thumb's centre at the minimum and at the maximum
        const float trackStart = (float) x + (float) getSliderThumbRadius (slider);
        const float trackEnd = (float) (x + width) - (float) getSliderThumbRadius (slider);

        juce::Path track;
        track.startNewSubPath (trackStart, centreY);
        track.lineTo (trackEnd, centreY);
        g.setColour (theme::track.withAlpha (alpha));
        g.strokePath (track, stroke (4.0f));

        // At the minimum there is no value to show: a line of no length would still be drawn as its round end caps
        if (slider.getValue() > slider.getMinimum())
        {
            // The round end cap of the glow would reach past the dot: nothing of the value is drawn to its right
            const juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (juce::Rectangle<float> ((float) x - 20.0f, (float) y, sliderPos - (float) x + 20.0f, (float) height).getSmallestIntegerContainer());

            juce::Path value;
            value.startNewSubPath (trackStart, centreY);
            value.lineTo (sliderPos, centreY);
            g.setColour (theme::accent.withAlpha (0.22f * alpha)); // the glow
            g.strokePath (value, stroke (10.0f));
            g.setColour (theme::accent.withAlpha (alpha));
            g.strokePath (value, stroke (3.5f));
        }

        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ sliderPos, centreY }));
    }
};
