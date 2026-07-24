#pragma once
#include <JuceHeader.h>
#include "AppState.h"
#include "ThemeEngine.h"
#include "ThemeFonts.h"

/**
 * Custom LookAndFeel for tearing debug tooltips with high-contrast styling.
 */
class TearingTooltipLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override
    {
        g.fillAll(juce::Colour(0xFF1A1A2E));
        g.setColour(juce::Colour(0xFF5599FF));
        g.drawRect(0, 0, width, height, 1);
        g.setColour(juce::Colour(0xFFE0E0E0));
        g.setFont(12.0f);
        g.drawFittedText(text, 4, 2, width - 8, height - 4, juce::Justification::centredLeft, 3);
    }

    juce::Rectangle<int> getTooltipBounds(const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override
    {
        auto font = juce::Font (juce::FontOptions (12.0f));
        auto w = juce::jmin(400, juce::GlyphArrangement::getStringWidthInt(font, text) + 16);
        auto h = 22;
        return { screenPos.x > parentArea.getCentreX() ? screenPos.x - w - 4 : screenPos.x + 8,
                 screenPos.y + 18, w, h };
    }
};

/**
 * Debug panel that displays tearing detection statistics for all audio buffers.
 * Only shows meaningful data in JUCE_DEBUG builds.
 */
class TearingDebugPanel : public juce::Component, public juce::TooltipClient, private juce::Timer
{
public:
    // Snapshot of one pad's tearing stats (plain ints, no atomics)
    struct StatsSnapshot
    {
        int emptyOutputBuffers = 0, partialUnderfills = 0, discontinuities = 0;
        int mediumDiscontinuities = 0, minorDiscontinuities = 0, zeroSampleRuns = 0;
        int clippedSamples = 0, nanOrInfSamples = 0, rmsJumps = 0;
        int directionFlips = 0, sliceJumps = 0, modeTransitions = 0;
        int soundTouchResets = 0, dcOffsetDrifts = 0;
        int lookaheadPreCrossfades = 0, lookaheadMispredictions = 0, lookaheadAborts = 0;
        int totalEvents = 0, criticalEvents = 0, mediumEvents = 0;
    };

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
        
        tooltipWindow.setLookAndFeel(&tooltipLnf);
        tooltipWindow.setMillisecondsBeforeTipAppears(400);
    }
    
    ~TearingDebugPanel() override
    {
        tooltipWindow.setLookAndFeel(nullptr);
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
        g.setFont(ThemeFonts::getInstance().headingFont(14.0f));
        g.drawText("Audio Tearing Debug", area.removeFromTop(lineH + 2), juce::Justification::centredLeft);
        area.removeFromTop(2); // Small gap
        
        g.setFont(ThemeFonts::getInstance().monoFont(10.0f));
        
#if JUCE_DEBUG
        // Single row header - compact columns
        g.setColour(Theme::textSubtle());
        auto headerRow = area.removeFromTop(lineH);
        const int colW = 30; // Compact column width
        headerColumnBounds.clear();
        
        auto drawHeader = [&](const juce::String& label, int width, bool takeRemaining = false)
        {
            auto colRect = takeRemaining ? headerRow : headerRow.removeFromLeft(width);
            g.drawText(label, colRect, juce::Justification::centredLeft);
            headerColumnBounds.push_back(colRect);
        };
        
        drawHeader("P", 18);
        drawHeader("Emty", colW);
        drawHeader("Undr", colW);
        drawHeader("Maj", colW);
        drawHeader("Med", colW);
        drawHeader("Min", colW);
        drawHeader("Zero", colW);
        drawHeader("Clip", colW);
        drawHeader("NaN", colW);
        drawHeader("RMS", colW);
        drawHeader("Dir", colW);
        drawHeader("Jmp", colW);
        drawHeader("Trn", colW);
        drawHeader("Rst", colW);
        drawHeader("DC", colW);
        drawHeader("LaXF", colW);
        drawHeader("LaMs", colW);
        drawHeader("LaAb", colW, true);
        
        area.removeFromTop(1);
        
        // Data rows for each buffer - single row per pad (from cached snapshots)
        for (int i = 0; i < (int)cachedStats.size(); ++i)
        {
            if (area.getHeight() < lineH)
                break;
                
            const auto& s = cachedStats[(size_t)i];
            
            // Color code based on severity
            if (s.totalEvents == 0)
                g.setColour(Theme::textSubtle());
            else if (s.criticalEvents > 0)
                g.setColour(Theme::bad());
            else if (s.mediumEvents > 5 || s.totalEvents > 20)
                g.setColour(Theme::warn());
            else
                g.setColour(Theme::text());
            
            auto row = area.removeFromTop(lineH);
            
            // All columns in one row
            g.drawText(juce::String(i + 1), row.removeFromLeft(18), juce::Justification::centredLeft);
            g.drawText(juce::String(s.emptyOutputBuffers), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(s.partialUnderfills), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(s.discontinuities), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(s.mediumDiscontinuities), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(s.minorDiscontinuities), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(s.zeroSampleRuns), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(s.clippedSamples), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(s.nanOrInfSamples), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(s.rmsJumps), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(s.directionFlips), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(s.sliceJumps), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(s.modeTransitions), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(s.soundTouchResets), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(s.dcOffsetDrifts), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(s.lookaheadPreCrossfades), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(s.lookaheadMispredictions), row.removeFromLeft(colW), juce::Justification::centredLeft);
            g.drawText(juce::String(s.lookaheadAborts), row, juce::Justification::centredLeft);
        }
        
        // Summary at bottom (from cached totals)
        if (area.getHeight() >= lineH * 3)
        {
            area.removeFromTop(4);
            g.setColour(Theme::border());
            g.drawHorizontalLine(area.getY(), (float)area.getX(), (float)area.getRight());
            area.removeFromTop(4);
            
            g.setFont(ThemeFonts::getInstance().monoBoldFont(12.0f));
            
            // Color-coded summary by severity
            auto summaryRow = area.removeFromTop(lineH);
            g.setColour(Theme::text());
            g.drawText("Total: " + juce::String(cachedTotalAll), summaryRow.removeFromLeft(100), juce::Justification::centredLeft);
            
            if (cachedTotalCritical > 0)
                g.setColour(Theme::bad());
            else
                g.setColour(Theme::textSubtle());
            g.drawText("Critical: " + juce::String(cachedTotalCritical), summaryRow.removeFromLeft(90), juce::Justification::centredLeft);
            
            if (cachedTotalMedium > 0)
                g.setColour(Theme::warn());
            else
                g.setColour(Theme::textSubtle());
            g.drawText("Medium: " + juce::String(cachedTotalMedium), summaryRow.removeFromLeft(90), juce::Justification::centredLeft);
            
            g.setColour(Theme::textSubtle());
            g.drawText("Minor: " + juce::String(cachedTotalMinor), summaryRow, juce::Justification::centredLeft);
        }
#else
        // Release build message
        g.setColour(Theme::textSubtle());
        g.drawText("Tearing debug stats only available in Debug builds.",
                  area.removeFromTop(lineH * 2),
                  juce::Justification::centred);
#endif
    }
    
    juce::String getTooltip() override
    {
        static const juce::String descriptions[] = {
            "Pad number",
            "Empty output buffers - no audio data available",
            "Partial underfills - buffer not completely filled",
            "Major discontinuities - large sample jumps",
            "Medium discontinuities - moderate sample jumps",
            "Minor discontinuities - small sample jumps",
            "Zero sample runs - consecutive zero-value samples",
            "Clipped samples - values exceeding +/-1.0",
            "NaN/Inf samples - invalid floating-point values",
            "RMS jumps - sudden level changes",
            "Direction flips - playback direction reversals",
            "Slice jumps - non-contiguous slice transitions",
            "Mode transitions - playback mode changes",
            "SoundTouch resets - time-stretch engine resets",
            "DC offset drifts - signal baseline shifts",
            "Lookahead pre-crossfades - crossfade blends applied",
            "Lookahead mispredictions - incorrect lookahead estimates",
            "Lookahead aborts - cancelled lookahead operations"
        };
        
        auto mousePos = getMouseXYRelative();
        for (size_t i = 0; i < headerColumnBounds.size(); ++i)
            if (headerColumnBounds[i].contains(mousePos))
                return descriptions[i];
        
        return {};
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
    TearingTooltipLookAndFeel tooltipLnf;
    juce::TooltipWindow tooltipWindow { this };
    std::vector<juce::Rectangle<int>> headerColumnBounds;

    // Cached stats snapshots (populated in timerCallback, used in paint)
    std::vector<StatsSnapshot> cachedStats;
    int cachedTotalAll = 0, cachedTotalCritical = 0, cachedTotalMedium = 0, cachedTotalMinor = 0;
    
    void timerCallback() override 
    { 
        if (!isShowing()) return;

        // Snapshot all atomic stats into plain structs
        const auto numStrips = static_cast<size_t> (app.channelStrips.size());
        cachedStats.resize(numStrips);
        cachedTotalAll = cachedTotalCritical = cachedTotalMedium = cachedTotalMinor = 0;

        for (size_t i = 0; i < numStrips; ++i)
        {
            const auto& strip = *app.channelStrips[static_cast<int> (i)];
            const auto* buffer = strip.getAudioBuffer();
            auto& s = cachedStats[i];
            if (!buffer)
            {
                s = {};
                continue;
            }
            const auto& st = buffer->getTearingStats();
            s.emptyOutputBuffers     = st.emptyOutputBuffers.load();
            s.partialUnderfills      = st.partialUnderfills.load();
            s.discontinuities        = st.discontinuities.load();
            s.mediumDiscontinuities  = st.mediumDiscontinuities.load();
            s.minorDiscontinuities   = st.minorDiscontinuities.load();
            s.zeroSampleRuns         = st.zeroSampleRuns.load();
            s.clippedSamples         = st.clippedSamples.load();
            s.nanOrInfSamples        = st.nanOrInfSamples.load();
            s.rmsJumps               = st.rmsJumps.load();
            s.directionFlips         = st.directionFlips.load();
            s.sliceJumps             = st.sliceJumps.load();
            s.modeTransitions        = st.modeTransitions.load();
            s.soundTouchResets       = st.soundTouchResets.load();
            s.dcOffsetDrifts         = st.dcOffsetDrifts.load();
            s.lookaheadPreCrossfades = st.lookaheadPreCrossfades.load();
            s.lookaheadMispredictions = st.lookaheadMispredictions.load();
            s.lookaheadAborts        = st.lookaheadAborts.load();
            s.totalEvents    = s.emptyOutputBuffers + s.partialUnderfills + s.discontinuities
                             + s.mediumDiscontinuities + s.minorDiscontinuities + s.zeroSampleRuns
                             + s.clippedSamples + s.nanOrInfSamples + s.rmsJumps + s.directionFlips
                             + s.sliceJumps + s.modeTransitions + s.soundTouchResets + s.dcOffsetDrifts
                             + s.lookaheadPreCrossfades + s.lookaheadMispredictions + s.lookaheadAborts;
            s.criticalEvents = s.emptyOutputBuffers + s.discontinuities + s.nanOrInfSamples;
            s.mediumEvents   = s.mediumDiscontinuities + s.rmsJumps + s.partialUnderfills;

            cachedTotalAll      += s.totalEvents;
            cachedTotalCritical += s.criticalEvents;
            cachedTotalMedium   += s.mediumDiscontinuities + s.rmsJumps + s.partialUnderfills + s.dcOffsetDrifts;
            cachedTotalMinor    += s.minorDiscontinuities + s.directionFlips + s.sliceJumps
                                + s.modeTransitions + s.soundTouchResets;
        }

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
