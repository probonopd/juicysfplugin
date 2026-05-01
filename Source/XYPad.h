#pragma once

#include "../JuceLibraryCode/JuceHeader.h"

/**
 * XYPad: a draggable 2-D control that drives two AudioProcessorValueTreeState
 * parameters (vectorX and vectorY).  The four corners are labelled A–D,
 * matching the four vector synthesis layers:
 *   A = bottom-left  (0,0)
 *   B = bottom-right (1,0)
 *   C = top-right    (1,1)
 *   D = top-left     (0,1)
 */
class XYPad : public juce::Component,
              private juce::Timer
{
public:
    XYPad (juce::AudioProcessorValueTreeState& state,
           const juce::String& xParamID,
           const juce::String& yParamID)
        : apvts (state), xID (xParamID), yID (yParamID)
    {
        xParam = apvts.getParameter (xID);
        yParam = apvts.getParameter (yID);
        jassert (xParam != nullptr && yParam != nullptr);
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        startTimerHz (60);
    }

    ~XYPad() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        // Background
        g.setColour (juce::Colour (28, 28, 35));
        g.fillRoundedRectangle (b, 4.0f);

        // Subtle grid lines
        g.setColour (juce::Colour (55, 55, 68));
        g.drawRect (b, 1.0f);
        g.drawVerticalLine   (static_cast<int> (b.getCentreX()), b.getY(), b.getBottom());
        g.drawHorizontalLine (static_cast<int> (b.getCentreY()), b.getX(), b.getRight());

        // Corner labels
        const float pad = 6.0f;
        g.setColour (juce::Colours::grey);
        g.setFont (11.0f);
        g.drawText ("A", b.withWidth (20).withHeight (16).translated (pad, b.getHeight() - 16 - pad), juce::Justification::centred);
        g.drawText ("B", b.withWidth (20).withHeight (16).translated (b.getWidth() - 20 - pad, b.getHeight() - 16 - pad), juce::Justification::centred);
        g.drawText ("C", b.withWidth (20).withHeight (16).translated (b.getWidth() - 20 - pad, pad), juce::Justification::centred);
        g.drawText ("D", b.withWidth (20).withHeight (16).translated (pad, pad), juce::Justification::centred);

        // Crosshair dot at current XY position
        const auto pos = getPositionFromParams();
        const float cx = b.getX() + pos.x * b.getWidth();
        const float cy = b.getY() + (1.0f - pos.y) * b.getHeight();

        // Glow ring
        g.setColour (juce::Colours::cyan.withAlpha (0.25f));
        g.drawEllipse (cx - 10.0f, cy - 10.0f, 20.0f, 20.0f, 1.5f);

        // Solid dot
        g.setColour (juce::Colours::white);
        g.fillEllipse (cx - 5.0f, cy - 5.0f, 10.0f, 10.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override { updateFromMouse (e); }
    void mouseDrag (const juce::MouseEvent& e) override { updateFromMouse (e); }

    void timerCallback() override { repaint(); }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::RangedAudioParameter* xParam = nullptr;
    juce::RangedAudioParameter* yParam = nullptr;
    juce::String xID, yID;

    juce::Point<float> getPositionFromParams() const
    {
        return { xParam->getValue(), yParam->getValue() };
    }

    void updateFromMouse (const juce::MouseEvent& e)
    {
        auto b = getLocalBounds().toFloat();
        const float x = juce::jlimit (0.0f, 1.0f, e.position.x / b.getWidth());
        const float y = juce::jlimit (0.0f, 1.0f, 1.0f - e.position.y / b.getHeight());
        if (xParam) xParam->setValueNotifyingHost (x);
        if (yParam) yParam->setValueNotifyingHost (y);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XYPad)
};
