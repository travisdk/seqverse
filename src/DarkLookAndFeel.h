#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BinaryData.h"

#include <cmath>

// The dark theme of the editor, SubForge's "Syre" look with SeqVerse's teal accent: dark panels, vector knobs (a value
// arc around a dark body with a pointer), flat chips for buttons and choices, Chakra Petch for names and JetBrains Mono
// for values. Everything a component looks like comes from here, so the editor code only lays things out.
namespace theme
{
inline const juce::Colour background (0xff0b0d0c);
inline const juce::Colour panel (0xff141716);      // the sections
inline const juce::Colour outline (0xff232826);    // panel edges, field outlines
inline const juce::Colour field (0xff1c201e);      // dropdowns, buttons, knob bodies
inline const juce::Colour track (0xff2a302d);      // a knob's arc beyond its value
inline const juce::Colour text (0xffe8ede6);
inline const juce::Colour dimText (0xff8a938d);    // the names of the controls
inline const juce::Colour accent (0xff3dd6c6);     // values, highlights, what is on
inline const juce::Colour onAccent (0xff0b0d0c);   // text on the accent colour
inline const juce::Colour record (0xffff5252);     // the Record button
inline const juce::Colour tooltip (0xff2c3330);    // clearly lighter than the panels
}

class DarkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static constexpr int kKnobSize = 40; // the largest a knob is drawn, whatever room it has

    // A TextButton with this property is drawn in the record colour: outlined when off, filled when on
    static inline const juce::Identifier recordProperty { "recordButton" };

    DarkLookAndFeel()
    {
        labelFace = juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchSemiBold_ttf, BinaryData::ChakraPetchSemiBold_ttfSize);
        titleFace = juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchBold_ttf, BinaryData::ChakraPetchBold_ttfSize);
        valueFace = juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMonoMedium_ttf, BinaryData::JetBrainsMonoMedium_ttfSize);

        // windowBackground, widgetBackground, menuBackground, outline, defaultText, defaultFill,
        // highlightedText, highlightedFill, menuText
        setColourScheme ({ theme::background, theme::field, theme::panel, theme::outline, theme::text, theme::accent,
                           theme::onAccent, theme::accent, theme::text });

        setColour (juce::ResizableWindow::backgroundColourId, theme::background);
        setColour (juce::Label::textColourId, theme::dimText);

        setColour (juce::ComboBox::textColourId, theme::text);
        setColour (juce::PopupMenu::textColourId, theme::text);
        setColour (juce::PopupMenu::highlightedTextColourId, theme::text);

        setColour (juce::TextButton::buttonColourId, theme::field);
        setColour (juce::TextButton::textColourOffId, theme::text);
        setColour (juce::TextButton::textColourOnId, theme::onAccent);

        setColour (juce::Slider::textBoxTextColourId, theme::text);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, theme::accent.withAlpha (0.4f));

        // What is typed into a value box
        setColour (juce::TextEditor::backgroundColourId, theme::field);
        setColour (juce::TextEditor::textColourId, theme::text);
        setColour (juce::TextEditor::highlightColourId, theme::accent.withAlpha (0.4f));
        setColour (juce::TextEditor::highlightedTextColourId, theme::text);
        setColour (juce::TextEditor::outlineColourId, theme::outline);
        setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
        setColour (juce::CaretComponent::caretColourId, theme::text);
    }

    juce::Font labelFont (float height) const { return juce::Font (juce::FontOptions (labelFace).withHeight (height)); }
    juce::Font titleFont (float height) const { return juce::Font (juce::FontOptions (titleFace).withHeight (height)); }
    juce::Font valueFont (float height) const { return juce::Font (juce::FontOptions (valueFace).withHeight (height)); }

    // The name of a control, in capitals above it
    juce::Font captionFont() const { return labelFont (12.0f).withExtraKerningFactor (0.08f); }

    // The plugin's name and what it is, at the top left
    void drawHeader (juce::Graphics& g, juce::Rectangle<int> area)
    {
        auto bounds = area.toFloat();
        g.setColour (theme::accent);
        g.setFont (titleFont (23.0f).withExtraKerningFactor (0.08f));
        g.drawText ("SEQVERSE", bounds.removeFromTop (bounds.getHeight() * 0.62f), juce::Justification::bottomLeft);
        g.setColour (theme::dimText);
        g.setFont (labelFont (11.0f).withExtraKerningFactor (0.16f));
        g.drawText ("GENERATIVE MIDI SEQUENCER", bounds, juce::Justification::centredLeft);
    }

    // A section: a raised panel with its name at the top left, if it has one
    void drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& heading, int padding, int headingHeight)
    {
        const auto bounds = area.toFloat().reduced (0.5f);
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

        if (heading.isEmpty())
            return;
        g.setColour (theme::text);
        g.setFont (titleFont (13.0f).withExtraKerningFactor (0.12f));
        g.drawText (heading.toUpperCase(), area.reduced (padding, 0).withTrimmedTop (padding - 4).withHeight (headingHeight - 8),
                    juce::Justification::centredLeft);
    }

    // An arc from the minimum to the value around a dark body with a white pointer. Brighter under the mouse, faded when
    // disabled.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float startAngle,
                           float endAngle, juce::Slider& slider) override
    {
        const float size = (float) juce::jmin (width, height, kKnobSize);
        const auto centre = juce::Rectangle<int> (x, y, width, height).toFloat().getCentre();
        const float thickness = juce::jmax (3.0f, size * 0.09f);
        const float ringRadius = size * 0.5f - thickness * 0.5f - 1.0f;
        const float angle = startAngle + sliderPos * (endAngle - startAngle);
        const bool hot = slider.isEnabled() && slider.isMouseOverOrDragging();
        const float alpha = slider.isEnabled() ? 1.0f : 0.4f;
        const juce::PathStrokeType arcStroke (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (theme::track);
        g.strokePath (track, arcStroke);

        if (angle - startAngle > 0.01f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, startAngle, angle, true);
            g.setColour ((hot ? theme::accent.brighter (0.2f) : theme::accent).withMultipliedAlpha (alpha));
            g.strokePath (value, arcStroke);
        }

        const float bodyRadius = ringRadius - thickness * 1.1f;
        const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
        g.setColour (theme::field);
        g.fillEllipse (body);
        g.setColour (hot ? theme::outline.brighter (0.4f) : theme::outline);
        g.drawEllipse (body.reduced (0.5f), 1.0f);

        juce::Path pointer;
        pointer.startNewSubPath (centre.getPointOnCircumference (bodyRadius * 0.35f, angle));
        pointer.lineTo (centre.getPointOnCircumference (bodyRadius * 0.88f, angle));
        g.setColour (theme::text.withMultipliedAlpha (alpha));
        g.strokePath (pointer, juce::PathStrokeType (juce::jmax (2.0f, size * 0.045f), juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    // The value under a knob, in the mono font on no background; it can be clicked to type a value
    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* label = LookAndFeel_V4::createSliderTextBox (slider);
        label->setFont (valueFont (13.0f));
        label->setMinimumHorizontalScale (0.5f); // squeeze long values rather than cut them off
        label->setColour (juce::Label::textColourId, theme::text);
        label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        return label;
    }

    // Labels keep the font they were given (captions, value boxes, the status line)
    juce::Font getLabelFont (juce::Label& label) override { return label.getFont(); }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
        const bool hot = box.isEnabled() && (box.isMouseOver (true) || box.isPopupActive());
        const float alpha = box.isEnabled() ? 1.0f : 0.5f;

        g.setColour (theme::field.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (hot ? theme::accent.withAlpha (0.7f) : theme::outline);
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

        const float cx = (float) width - 12.0f, cy = (float) height * 0.5f;
        juce::Path chevron;
        chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
        chevron.lineTo (cx, cy + 2.0f);
        chevron.lineTo (cx + 4.0f, cy - 2.0f);
        g.setColour (theme::dimText.withMultipliedAlpha (alpha));
        g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
        label.setMinimumHorizontalScale (0.7f);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return labelFont (14.0f); }

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        g.fillAll (theme::panel);
        g.setColour (theme::outline.brighter (0.2f));
        g.drawRect (0, 0, width, height, 1);
    }

    juce::Font getPopupMenuFont() override { return labelFont (14.0f); }

    // A row of a menu: the current choice has a dot and is in the accent colour, the one under the mouse a tinted band
    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override
    {
        if (isSeparator || icon != nullptr || shortcutKeyText.isNotEmpty())
        {
            LookAndFeel_V4::drawPopupMenuItem (g, area, isSeparator, isActive, isHighlighted, isTicked, hasSubMenu, text,
                                               shortcutKeyText, icon, textColour);
            return;
        }

        auto r = area.reduced (3, 1);
        if (isHighlighted && isActive)
        {
            g.setColour (theme::accent.withAlpha (0.18f));
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
        }

        if (isTicked)
        {
            g.setColour (theme::accent);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ (float) r.getX() + 10.0f, (float) r.getCentreY() }));
        }

        g.setColour (isActive ? (isTicked ? theme::accent : theme::text) : theme::dimText);
        g.setFont (getPopupMenuFont());
        g.drawFittedText (text, r.withTrimmedLeft (20).withTrimmedRight (hasSubMenu ? 20 : 6), juce::Justification::centredLeft, 1);
    }

    // Buttons are flat chips. One that is on is filled with its colour; otherwise it is outlined on the field colour, in
    // its colour for the Record button.
    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool isHighlighted, bool isDown) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const bool lit = button.getToggleState();
        const bool isRecord = button.getProperties().getWithDefault (recordProperty, false);
        const float alpha = button.isEnabled() ? 1.0f : 0.5f;
        auto fill = lit ? colourOf (button) : backgroundColour;
        if (isDown)
            fill = fill.darker (0.2f);
        else if (isHighlighted)
            fill = fill.brighter (0.08f);

        g.setColour (fill.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (bounds, 6.0f);
        if (isRecord && ! lit)
        {
            g.setColour (theme::record.withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.5f);
        }
        else if (! lit)
        {
            g.setColour (isHighlighted ? theme::outline.brighter (0.5f) : theme::outline.brighter (0.15f));
            g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return titleFont (13.0f); }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        const bool lit = button.getToggleState();
        const bool isRecord = button.getProperties().getWithDefault (recordProperty, false);
        g.setFont (getTextButtonFont (button, button.getHeight()).withExtraKerningFactor (0.08f));
        g.setColour ((lit ? theme::onAccent : isRecord ? theme::record : theme::text).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
        g.drawFittedText (button.getButtonText().toUpperCase(), button.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1, 0.7f);
    }

    // A toggle is a pill-shaped ON/OFF switch: the knob sits left and grey when off, right on an accent-filled track when
    // on, with the state written on the free side
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool isHighlighted, bool) override
    {
        const bool on = button.getToggleState();
        const float alpha = button.isEnabled() ? 1.0f : 0.5f;
        const auto track = button.getLocalBounds().toFloat().reduced (1.0f);
        const float radius = track.getHeight() * 0.5f;

        g.setColour ((on ? theme::accent : theme::field).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (track, radius);
        g.setColour (on ? theme::accent.brighter (0.2f) : (isHighlighted ? theme::outline.brighter (0.6f) : theme::outline.brighter (0.3f)));
        g.drawRoundedRectangle (track.reduced (0.5f), radius, 1.0f);

        const float knobSize = track.getHeight() - 6.0f;
        const auto knob = juce::Rectangle<float> (knobSize, knobSize)
                              .withCentre ({ on ? track.getRight() - radius : track.getX() + radius, track.getCentreY() });
        g.setColour ((on ? theme::onAccent : theme::dimText).withMultipliedAlpha (alpha));
        g.fillEllipse (knob);

        auto label = track.reduced (4.0f, 0.0f);
        label = on ? label.withTrimmedRight (knobSize + 2.0f) : label.withTrimmedLeft (knobSize + 2.0f);
        g.setFont (titleFont (10.5f).withExtraKerningFactor (0.06f));
        g.drawText (on ? "ON" : "OFF", label, juce::Justification::centred);
    }

    // Tooltips: a raised box with an accent edge, the text wrapped to balanced lines, beside the mouse
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override
    {
        const auto layout = tooltipLayout (tipText);
        const int w = (int) std::ceil (layout.getWidth() + 2.0f * kTooltipPadX);
        const int h = (int) std::ceil (layout.getHeight() + 2.0f * kTooltipPadY);
        return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                     screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
            .constrainedWithin (parentArea);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height) override
    {
        const auto bounds = juce::Rectangle<float> ((float) width, (float) height);
        g.setColour (theme::tooltip);
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (theme::accent);
        g.drawRoundedRectangle (bounds.reduced (1.0f), 5.0f, 1.5f);
        tooltipLayout (text).draw (g, bounds.reduced (kTooltipPadX, kTooltipPadY));
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

        g.setColour ((highlighted && enabled ? theme::outline.brighter (0.3f) : theme::field).withAlpha (alpha));
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

        g.setFont (titleFont (11.0f).withExtraKerningFactor (0.04f));
        g.drawText (label, page.withTrimmedTop (page.getHeight() * 0.78f), juce::Justification::centred);
    }

    // The horizontal slider, which the editor no longer uses but tools/layout_mockup.cpp does: a thin track, the value in
    // the accent colour with a glow, and a white dot
    int getSliderThumbRadius (juce::Slider&) override { return 6; }

    // A little air between a horizontal slider and its value box, so that the thumb at the maximum does not touch it
    juce::Slider::SliderLayout getSliderLayout (juce::Slider& slider) override
    {
        auto layout = LookAndFeel_V4::getSliderLayout (slider);
        if (slider.getSliderStyle() == juce::Slider::LinearHorizontal && slider.getTextBoxPosition() == juce::Slider::TextBoxRight)
            layout.sliderBounds.removeFromRight (10);
        return layout;
    }

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

private:
    static constexpr float kTooltipMaxWidth = 240.0f, kTooltipPadX = 10.0f, kTooltipPadY = 7.0f;

    static juce::Colour colourOf (const juce::Component& c)
    {
        return (bool) c.getProperties().getWithDefault (recordProperty, false) ? theme::record : theme::accent;
    }

    juce::TextLayout tooltipLayout (const juce::String& text) const
    {
        juce::AttributedString s;
        s.setJustification (juce::Justification::centredLeft);
        s.append (text, labelFont (13.5f), theme::text);
        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (s, kTooltipMaxWidth);
        return layout;
    }

    juce::Typeface::Ptr labelFace, titleFace, valueFace;
};
