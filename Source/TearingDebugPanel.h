#pragma once
#include <JuceHeader.h>
#include "AppState.h"
#include "ThemeEngine.h"

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
        
        g.setFont(juce::FontOptions(10.0f));
        
#if JUCE_DEBUG
        // Single row header - compact columns
        g.setColour(Theme::textSubtle());
        auto headerRow = area.removeFromTop(lineH);
        const int colW = 30; // Compact column width
        g.drawText("P", headerRow.removeFromLeft(18), juce::Justification::centredLeft);
        g.drawText("Emty", headerRow.removeFromLeft(colW), juce::Justification::centredLeft);
        g.drawText("Undr", headerRow.removeFromLeft(colW), juce::Justification::centredLeft);
        g.drawText("Maj", headerRow.removeFromLeft(colW), juce::Justification::centredLeft);
        g.drawText("Med", headerRow.removeFromLeft(colW), juce::Justification::centredLeft);
        g.drawText("Min", headerRow.removeFromLeft(colW), juce::Justification::centredLeft);
        g.drawText("Zero", headerRow.removeFromLeft(colW), juce::Justification::centredLeft);
        g.drawText("Clip", headerRow.removeFromLeft(colW), juce::Justification::centredLeft);
        g.drawText("NaN", headerRow.removeFromLeft(colW), juce::Justification::centredLeft);
        g.drawText("RMS", headerRow.removeFromLeft(colW), juce::Justification::centredLeft);
        g.drawText("Dir", headerRow.removeFromLeft(colW), juce::Justification::centredLeft);
        g.drawText("Jmp", headerRow.removeFromLeft(colW), juce::Justification::centredLeft);
        g.drawText("Trn", headerRow.removeFromLeft(colW), juce::Justification::centredLeft);
        g.drawText("Rst", headerRow.removeFromLeft(colW), juce::Justification::centredLeft);
        g.drawText("DC", headerRow, juce::Justification::centredLeft);
        
        area.removeFromTop(1);
        
        // Data rows for each buffer - single row per pad
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
            const int criticalEvents = stats.emptyOutputBuffers.load() + stats.discontinuities.load() 
                                      + stats.nanOrInfSamples.load();
            const int mediumEvents = stats.mediumDiscontinuities.load() + stats.rmsJumps.load() 
                                   + stats.partialUnderfills.load();
            
            // Color code based on severity
            if (totalEvents == 0)
                g.setColour(Theme::textSubtle());
            else if (criticalEvents > 0)
                g.setColour(Theme::bad());
            else if (mediumEvents > 5 || totalEvents > 20)
                g.setColour(Theme::warn());
            else
                g.setColour(Theme::text());
            
            auto row = area.removeFromTop(lineH);
            
            // All columns in one row
            g.drawText(juce::String(i + 1), row.removeFromLeft(18), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.emptyOutputBuffers.load()), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.partialUnderfills.load()), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.discontinuities.load()), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.mediumDiscontinuities.load()), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.minorDiscontinuities.load()), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.zeroSampleRuns.load()), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.clippedSamples.load()), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.nanOrInfSamples.load()), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.rmsJumps.load()), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.directionFlips.load()), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.sliceJumps.load()), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.modeTransitions.load()), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.soundTouchResets.load()), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(stats.dcOffsetDrifts.load()), row, juce::Justification::centredLeft);
        }
        
        // Summary at bottom
        if (area.getHeight() >= lineH * 3)
        {
            area.removeFromTop(4);
            g.setColour(Theme::border());
            g.drawHorizontalLine(area.getY(), (float)area.getX(), (float)area.getRight());
            area.removeFromTop(4);
            
            int totalAllPads = 0;
            int totalCritical = 0;
            int totalMedium = 0;
            int totalMinor = 0;
            
            for (int i = 0; i < app.channelStrips.size(); ++i)
            {
                const auto& strip = *app.channelStrips[i];
                if (const auto* buffer = strip.getAudioBuffer())
                {
                    const auto& stats = buffer->getTearingStats();
                    totalAllPads += stats.getTotalEvents();
                    totalCritical += stats.emptyOutputBuffers.load() + stats.discontinuities.load() 
                                   + stats.nanOrInfSamples.load();
                    totalMedium += stats.mediumDiscontinuities.load() + stats.rmsJumps.load() 
                                 + stats.partialUnderfills.load() + stats.dcOffsetDrifts.load();
                    totalMinor += stats.minorDiscontinuities.load() + stats.directionFlips.load() 
                                + stats.sliceJumps.load() + stats.modeTransitions.load() 
                                + stats.soundTouchResets.load();
                }
            }
            
            g.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
            
            // Color-coded summary by severity
            auto summaryRow = area.removeFromTop(lineH);
            g.setColour(Theme::text());
            g.drawText("Total: " + juce::String(totalAllPads), summaryRow.removeFromLeft(100), juce::Justification::centredLeft);
            
            if (totalCritical > 0)
                g.setColour(Theme::bad());
            else
                g.setColour(Theme::textSubtle());
            g.drawText("Critical: " + juce::String(totalCritical), summaryRow.removeFromLeft(90), juce::Justification::centredLeft);
            
            if (totalMedium > 0)
                g.setColour(Theme::warn());
            else
                g.setColour(Theme::textSubtle());
            g.drawText("Medium: " + juce::String(totalMedium), summaryRow.removeFromLeft(90), juce::Justification::centredLeft);
            
            g.setColour(Theme::textSubtle());
            g.drawText("Minor: " + juce::String(totalMinor), summaryRow, juce::Justification::centredLeft);
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
