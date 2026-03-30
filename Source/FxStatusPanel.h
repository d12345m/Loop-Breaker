#pragma once
#include <JuceHeader.h>
#include "AppState.h"
#include "ThemeEngine.h"

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
        auto bounds = getLocalBounds();
        g.setColour(Theme::panel());
        g.fillRect(bounds);
        g.setColour(Theme::border());
        g.drawRect(bounds, 1);

        g.setColour(Theme::text());
        auto area = getLocalBounds().reduced(4);
        int lineH = 16;
        g.drawText("FX Status", area.removeFromTop(lineH), juce::Justification::centredLeft);
        for (int i = 0; i < (int)cachedLines.size(); ++i)
        {
            auto row = area.removeFromTop(lineH);
            g.setColour(Theme::textSubtle());
            g.drawText(cachedLines[(size_t)i], row, juce::Justification::centredLeft);
            g.drawText(cachedParams[(size_t)i], row.withX(row.getX()+180), juce::Justification::centredLeft);
        }
    }

private:
    AppState& app;
    std::vector<juce::String> cachedLines;
    std::vector<juce::String> cachedParams;

    void timerCallback() override
    {
        if (!isShowing()) return;

        const auto numStrips = app.channelStrips.size();
        cachedLines.resize(numStrips);
        cachedParams.resize(numStrips);

        for (int i = 0; i < (int)numStrips; ++i)
        {
            const auto& strip = *app.channelStrips[(size_t)i];
            const auto& fx = strip.effects();
            const auto& params = strip.getFxParams();
            cachedLines[(size_t)i] = "Pad " + juce::String(i+1) + ": "
                + (fx.reverbEnabled ? "Reverb " : "")
                + (fx.delayEnabled ? "Delay " : "")
                + (fx.lowPassEnabled ? "LPF " : "")
                + (fx.highPassEnabled ? "HPF " : "")
                + (fx.tremoloEnabled ? "Trem " : "")
                + (fx.chorusEnabled ? "Chorus " : "")
                + (fx.autoPanEnabled ? "Pan " : "")
                + (fx.shLowPassEnabled ? "S&H-LPF " : "")
                + (fx.shHighPassEnabled ? "S&H-HPF " : "");
            cachedParams[(size_t)i] = juce::String::formatted(" rvb=%.2f pd=%.0fms fb=%.2f lpf=%.0f hpf=%.0f trem=%.2f chor=%.2f pan=%.2f",
                                                     params.reverbWet, params.reverbPreDelayMs, params.delayFeedback,
                                                     params.lowPassCutoff, params.highPassCutoff,
                                                     params.tremoloDepth, params.chorusMix, params.panMix);
        }
        repaint();
    }
};
