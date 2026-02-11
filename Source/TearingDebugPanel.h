#pragma once
#include <JuceHeader.h>
#include "AppState.h"
#include "Theme.h"

/**
 * Debug panel that displays tearing detection statistics for all audio buffers.
 * Only shows meaningful data in JUCE_DEBUG builds.
 */
class TearingDebugPanel : public juce::Component, private juce::Timer
{
public:
    explicit TearingDebugPanel(AppState& appState) : app(appState)
    {
        startTimerHz(2); // Refresh 2 Hz
        
        resetButton.setButtonText("Reset Stats");
        resetButton.onClick = [this]{ resetAllStats(); };
        addAndMakeVisible(resetButton);
        
        enableToggle.setButtonText("Debug Enabled");
        enableToggle.setToggleState(true, juce::dontSendNotification);
        enableToggle.onClick = [this]{ toggleDebugEnabled(); };
        addAndMakeVisible(enableToggle);
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
        const int lineH = 15;
        
        // Title
        g.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
        g.drawText("Audio Tearing Debug", area.removeFromTop(lineH + 2), juce::Justification::centredLeft);
        area.removeFromTop(2); // Small gap
        
        g.setFont(juce::FontOptions(12.0f));
        
#if JUCE_DEBUG
        // Column headers
        g.setColour(Theme::textSubtle());
        auto headerRow = area.removeFromTop(lineH);
        g.drawText("Pad", headerRow.removeFromLeft(30), juce::Justification::centredLeft);
        g.drawText("Empty", headerRow.removeFromLeft(45), juce::Justification::centredLeft);
        g.drawText("Under", headerRow.removeFromLeft(45), juce::Justification::centredLeft);
        g.drawText("Disc", headerRow.removeFromLeft(40), juce::Justification::centredLeft);
        g.drawText("Dir", headerRow.removeFromLeft(35), juce::Justification::centredLeft);
        g.drawText("Jump", headerRow.removeFromLeft(40), juce::Justification::centredLeft);
        g.drawText("Trans", headerRow.removeFromLeft(45), juce::Justification::centredLeft);
        g.drawText("Reset", headerRow.removeFromLeft(45), juce::Justification::centredLeft);
        g.drawText("Zeros", headerRow.removeFromLeft(45), juce::Justification::centredLeft);
        g.drawText("Clip", headerRow.removeFromLeft(40), juce::Justification::centredLeft);
        g.drawText("NaN", headerRow, juce::Justification::centredLeft);
        
        area.removeFromTop(1);
        
        // Data rows for each buffer
        for (int i = 0; i < app.channelStrips.size(); ++i)
        {
            if (area.getHeight() < lineH)
                break;
                
            const auto& strip = *app.channelStrips[i];
            const auto* buffer = strip.getAudioBuffer();
            if (!buffer)
                continue;
                
            const auto& stats = buffer->getTearingStats();
            
            const int totalEvents = stats.getTotalEvents();
            
            // Color code based on severity
            if (totalEvents == 0)
                g.setColour(Theme::textSubtle());
            else if (totalEvents < 10)
                g.setColour(Theme::warn());
            else
                g.setColour(Theme::bad());
            
            auto row = area.removeFromTop(lineH);
            
            // Pad number
            g.drawText(juce::String(i + 1), row.removeFromLeft(30), juce::Justification::centredLeft);
            
            // Stats columns
            g.drawText(juce::String(stats.emptyOutputBuffers.load()), row.removeFromLeft(45), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.partialUnderfills.load()), row.removeFromLeft(45), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.discontinuities.load()), row.removeFromLeft(40), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.directionFlips.load()), row.removeFromLeft(35), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.sliceJumps.load()), row.removeFromLeft(40), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.modeTransitions.load()), row.removeFromLeft(45), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.soundTouchResets.load()), row.removeFromLeft(45), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.zeroSampleRuns.load()), row.removeFromLeft(45), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.clippedSamples.load()), row.removeFromLeft(40), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.nanOrInfSamples.load()), row, juce::Justification::centredLeft);
        }
        
        // Summary at bottom
        if (area.getHeight() >= lineH * 2)
        {
            area.removeFromTop(4);
            g.setColour(Theme::border());
            g.drawHorizontalLine(area.getY(), (float)area.getX(), (float)area.getRight());
            area.removeFromTop(4);
            
            int totalAllPads = 0;
            for (int i = 0; i < app.channelStrips.size(); ++i)
            {
                const auto& strip = *app.channelStrips[i];
                if (const auto* buffer = strip.getAudioBuffer())
                    totalAllPads += buffer->getTearingStats().getTotalEvents();
            }
            
            g.setColour(totalAllPads == 0 ? Theme::textSubtle() : Theme::warn());
            g.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
            g.drawText("Total Events: " + juce::String(totalAllPads), 
                      area.removeFromTop(lineH), 
                      juce::Justification::centredLeft);
        }
#else
        // Release build message
        g.setColour(Theme::textSubtle());
        g.drawText("Tearing debug stats only available in Debug builds.",
                  area.removeFromTop(lineH * 2),
                  juce::Justification::centred);
#endif
    }
    
    void resized() override
    {
        auto area = getLocalBounds().reduced(4);
        const int buttonH = 22;
        auto buttonRow = area.removeFromBottom(buttonH + 4);
        
        resetButton.setBounds(buttonRow.removeFromLeft(100).reduced(2));
        buttonRow.removeFromLeft(4);
        enableToggle.setBounds(buttonRow.removeFromLeft(120).reduced(2));
    }

private:
    AppState& app;
    juce::TextButton resetButton;
    juce::ToggleButton enableToggle;
    
    void timerCallback() override 
    { 
        repaint(); 
    }
    
    void resetAllStats()
    {
#if JUCE_DEBUG
        for (int i = 0; i < app.channelStrips.size(); ++i)
        {
            auto& strip = *app.channelStrips[i];
            if (auto* buffer = strip.getAudioBuffer())
                buffer->resetTearingStats();
        }
        DBG("Tearing stats reset for all buffers");
#endif
    }
    
    void toggleDebugEnabled()
    {
#if JUCE_DEBUG
        const bool enabled = enableToggle.getToggleState();
        for (int i = 0; i < app.channelStrips.size(); ++i)
        {
            auto& strip = *app.channelStrips[i];
            if (auto* buffer = strip.getAudioBuffer())
                buffer->setTearingDebugEnabled(enabled);
        }
        DBG("Tearing debug " + juce::String(enabled ? "enabled" : "disabled"));
#endif
    }
};
