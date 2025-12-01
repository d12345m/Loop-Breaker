#pragma once
#include <JuceHeader.h>
#include "AppState.h"

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
        g.fillAll(juce::Colours::darkgrey.darker());
        g.setColour(juce::Colours::white);
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
            g.setColour(juce::Colours::lightgrey);
            g.drawText(line, row, juce::Justification::centredLeft);
            // Params line
            juce::String p = juce::String::formatted(" wet=%.2f fb=%.2f lpf=%.0f hpf=%.0f trem=%.2f",
                                                     params.reverbWet, params.delayFeedback,
                                                     params.lowPassCutoff, params.highPassCutoff,
                                                     params.tremoloDepth);
            g.drawText(p, row.withX(row.getX()+180), juce::Justification::centredLeft);
        }
    }

private:
    AppState& app;
    void timerCallback() override { repaint(); }
};
