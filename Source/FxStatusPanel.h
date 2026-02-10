#pragma once
#include <JuceHeader.h>
#include "AppState.h"
#include "Theme.h"

// Simple read-only panel to display FX enabled flags and current params per pad.
class FxStatusPanel : public juce::Component, private juce::Timer
{
public:
    explicit FxStatusPanel(AppState& appState) : app(appState)
    {
        startTimerHz(5); // refresh 5 Hz
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(Theme::panel());
        g.fillRoundedRectangle(bounds.reduced(1.0f), 10.0f);
        g.setColour(Theme::border());
        g.drawRoundedRectangle(bounds.reduced(1.0f), 10.0f, 1.0f);

        g.setColour(Theme::text());
        auto area = getLocalBounds().reduced(4);
        int lineH = 16;
        g.drawText("FX Status", area.removeFromTop(lineH), juce::Justification::centredLeft);
        for (int i = 0; i < app.channelStrips.size(); ++i)
        {
            const auto& strip = *app.channelStrips[i];
            const auto& fx = strip.effects();
            const auto& params = strip.getFxParams();
            juce::String line = "Pad " + juce::String(i+1) + ": "
                + (fx.reverbEnabled ? "Reverb " : "")
                + (fx.delayEnabled ? "Delay " : "")
                + (fx.lowPassEnabled ? "LPF " : "")
                + (fx.highPassEnabled ? "HPF " : "")
                + (fx.tremoloEnabled ? "Trem " : "");
            auto row = area.removeFromTop(lineH);
            g.setColour(Theme::textSubtle());
            g.drawText(line, row, juce::Justification::centredLeft);
            // Params line
            juce::String p = juce::String::formatted(" rvb=%.2f pd=%.0fms fb=%.2f lpf=%.0f hpf=%.0f trem=%.2f",
                                                     params.reverbWet, params.reverbPreDelayMs, params.delayFeedback,
                                                     params.lowPassCutoff, params.highPassCutoff,
                                                     params.tremoloDepth);
            g.drawText(p, row.withX(row.getX()+180), juce::Justification::centredLeft);
        }
    }

private:
    AppState& app;
    void timerCallback() override { repaint(); }
};
