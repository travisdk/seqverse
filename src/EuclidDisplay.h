#pragma once

#include "DarkLookAndFeel.h"
#include "Music.h"

#include <cmath>

// Draws a Euclidean rhythm as a ring of dots: the steps go round like a clock from the top, a lit dot is a pulse, and
// the polygon between the pulses shows how evenly they are spread. Step 0 (the beat where the pattern starts) has a
// white ring round its dot.
class EuclidDisplay : public juce::Component
{
public:
    void setPattern (const music::EuclidPattern& newPattern)
    {
        pattern = newPattern;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (10.0f);
        const auto centre = bounds.getCentre();
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        const float alpha = isEnabled() ? 1.0f : 0.4f;
        const float pi = juce::MathConstants<float>::pi;

        const auto pointOf = [&] (int step)
        {
            const float angle = 2.0f * pi * (float) step / (float) pattern.steps;
            return juce::Point<float> (centre.x + radius * std::sin (angle), centre.y - radius * std::cos (angle));
        };

        g.setColour (theme::outline.withAlpha (alpha));
        g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);

        if (pattern.pulses >= 2)
        {
            juce::Path polygon;
            for (int i = 0; i < pattern.steps; ++i)
                if (pattern.onset[i])
                {
                    if (polygon.isEmpty())
                        polygon.startNewSubPath (pointOf (i));
                    else
                        polygon.lineTo (pointOf (i));
                }
            polygon.closeSubPath();

            g.setColour (theme::accent.withAlpha (0.08f * alpha));
            g.fillPath (polygon);
            g.setColour (theme::accent.withAlpha (0.45f * alpha));
            g.strokePath (polygon, juce::PathStrokeType (1.5f));
        }

        // The dots get smaller when there are many of them, so that they never run into each other
        const float dot = juce::jlimit (2.5f, 7.0f, radius * pi / (float) pattern.steps * 0.6f);

        for (int i = 0; i < pattern.steps; ++i)
        {
            const auto p = pointOf (i);

            if (pattern.onset[i])
            {
                g.setColour (theme::accent.withAlpha (0.22f * alpha));
                g.fillEllipse (juce::Rectangle<float> (dot * 3.2f, dot * 3.2f).withCentre (p));
                g.setColour (theme::accent.withAlpha (alpha));
                g.fillEllipse (juce::Rectangle<float> (dot * 2.0f, dot * 2.0f).withCentre (p));
            }
            else
            {
                g.setColour (theme::surface.withAlpha (alpha));
                g.fillEllipse (juce::Rectangle<float> (dot * 1.6f, dot * 1.6f).withCentre (p));
                g.setColour (theme::outline.withAlpha (alpha));
                g.drawEllipse (juce::Rectangle<float> (dot * 1.6f, dot * 1.6f).withCentre (p), 1.0f);
            }

            if (i == 0)
            {
                g.setColour (juce::Colours::white.withAlpha (alpha));
                g.drawEllipse (juce::Rectangle<float> (dot * 3.0f, dot * 3.0f).withCentre (p), 1.5f);
            }
        }
    }

private:
    music::EuclidPattern pattern { music::kDefaultEuclidSteps, music::kDefaultEuclidPulses, 0 };
};
