/*
 ==============================================================================
   NodeClipDebugPanel.h
   --------------------------------------------------------------------------
   UI panel displaying per-node clip detection results across all pads.
   Shows a heat-map style grid: rows = pads, columns = signal-chain nodes.
   Color-coded cells show peak level and clip counts at a glance.
   
   Integrates into the existing Debug tab alongside TearingDebugPanel.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "AppState.h"
#include "NodeClipDetector.h"
#include "ThemeEngine.h"
#include "ThemeFonts.h"

class NodeClipDebugPanel : public juce::Component, private juce::Timer
{
public:
    explicit NodeClipDebugPanel(AppState& appState)
        : app(appState)
    {
        startTimerHz(4); // 4 Hz refresh

        resetButton.setButtonText("Reset Clip Stats");
        resetButton.onClick = [this] { app.clipDetector.resetAll(); repaint(); };
        addAndMakeVisible(resetButton);

        enableToggle.setButtonText("Clip Detection");
        enableToggle.setToggleState(true, juce::dontSendNotification);
        enableToggle.onClick = [this] {
            app.clipDetector.setGlobalEnabled(enableToggle.getToggleState());
        };
        addAndMakeVisible(enableToggle);
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        g.setColour(Theme::panel());
        g.fillRect(bounds);
        g.setColour(Theme::border());
        g.drawRect(bounds, 1);

        auto area = bounds.reduced(4);
        const int lineH = 14;
        const int headerH = 18;

        // Title bar with status indicator
        g.setFont(ThemeFonts::getInstance().headingFont(14.0f));
        auto titleRow = area.removeFromTop(headerH);

        if (app.clipDetector.hasAnyClipping())
        {
            // Pulsing red indicator
            float alpha = 0.6f + 0.4f * std::sin((float)juce::Time::getMillisecondCounterHiRes() * 0.006f);
            g.setColour(Theme::bad().withAlpha(alpha));
            g.fillEllipse(titleRow.removeFromLeft(14).toFloat().reduced(2));
            titleRow.removeFromLeft(4);
            g.setColour(Theme::bad());
        }
        else
        {
            g.setColour(juce::Colours::green);
            g.fillEllipse(titleRow.removeFromLeft(14).toFloat().reduced(2));
            titleRow.removeFromLeft(4);
            g.setColour(Theme::text());
        }
        g.drawText("Node Clip Detector", titleRow, juce::Justification::centredLeft);
        area.removeFromTop(2);

        g.setFont(ThemeFonts::getInstance().monoFont(9.0f));

        // Column header: Pad | Raw | Dly | HPF | LPF | Trm | Chr | Pan | Vol | PLm | Rvb | RLm | Pad | Mst
        const int padColW = 22;
        const int nodeColW = 30;

        g.setColour(Theme::textSubtle());
        auto headerRow = area.removeFromTop(lineH);
        headerRow.removeFromLeft(padColW); // "P" column

        for (int n = 0; n < kNumNodes; ++n)
        {
            auto nodeId = static_cast<NodeId>(n);
            if (nodeId == NodeId::FinalMix) continue; // shown separately
            g.drawText(nodeIdToShortString(nodeId),
                       headerRow.removeFromLeft(nodeColW),
                       juce::Justification::centred);
        }
        area.removeFromTop(1);

        // Draw a thin separator
        g.setColour(Theme::border().withAlpha(0.5f));
        g.drawHorizontalLine(area.getY(), (float)area.getX(), (float)area.getRight());
        area.removeFromTop(2);

        // Per-pad rows
        for (int padIdx = 0; padIdx < ClipDetectorSystem::kMaxPads; ++padIdx)
        {
            if (area.getHeight() < lineH)
                break;

            const auto& padProbes = app.clipDetector.padProbes[(size_t)padIdx];
            auto row = area.removeFromTop(lineH);

            // Pad number
            g.setColour(Theme::text());
            g.drawText(juce::String(padIdx + 1), row.removeFromLeft(padColW), juce::Justification::centredLeft);

            // Per-node cells
            for (int n = 0; n < kNumNodes; ++n)
            {
                auto nodeId = static_cast<NodeId>(n);
                if (nodeId == NodeId::FinalMix) continue;

                auto cell = row.removeFromLeft(nodeColW);
                const auto& stats = padProbes.probes[(size_t)n].getStats();

                // Color based on severity
                juce::Colour cellColour;
                if (stats.hasNanInf())
                    cellColour = juce::Colours::magenta;
                else if (stats.hasHardClipping())
                    cellColour = juce::Colours::red;
                else if (stats.hasClipping())
                    cellColour = juce::Colours::orange;
                else if (stats.peakLevel.load() > 0.9f)
                    cellColour = juce::Colours::yellow.darker(0.2f);
                else if (stats.peakLevel.load() > 0.001f)
                    cellColour = juce::Colours::green.darker(0.3f);
                else
                    cellColour = juce::Colour(0x00000000); // transparent = no signal

                if (!cellColour.isTransparent())
                {
                    g.setColour(cellColour.withAlpha(0.3f));
                    g.fillRect(cell.reduced(1));
                }

                // Draw clip count or peak
                if (stats.hasNanInf())
                {
                    g.setColour(juce::Colours::magenta);
                    g.drawText("NaN", cell, juce::Justification::centred);
                }
                else if (stats.hasClipping())
                {
                    g.setColour(Theme::bad());
                    int clips = stats.clipCount.load();
                    g.drawText(clips > 999 ? juce::String(clips / 1000) + "k" : juce::String(clips),
                               cell, juce::Justification::centred);
                }
                else if (stats.peakLevel.load() > 0.001f)
                {
                    // Show peak in dB (compact)
                    float peakDb = juce::Decibels::gainToDecibels(stats.peakLevel.load(), -96.0f);
                    g.setColour(peakDb > -3.0f ? Theme::warn() : Theme::textSubtle());
                    g.drawText(juce::String((int)peakDb), cell, juce::Justification::centred);
                }
            }
        }

        // Mix bus row
        area.removeFromTop(2);
        g.setColour(Theme::border().withAlpha(0.5f));
        g.drawHorizontalLine(area.getY(), (float)area.getX(), (float)area.getRight());
        area.removeFromTop(3);

        if (area.getHeight() >= lineH)
        {
            auto mixRow = area.removeFromTop(lineH);
            const auto& mixStats = app.clipDetector.mixBusProbe.getStats();

            g.setFont(ThemeFonts::getInstance().monoBoldFont(10.0f));
            g.setColour(Theme::text());
            g.drawText("Mix", mixRow.removeFromLeft(padColW + 4), juce::Justification::centredLeft);

            float mixPeakDb = juce::Decibels::gainToDecibels(mixStats.peakLevel.load(), -96.0f);
            juce::String mixInfo = "Peak: " + juce::String(mixPeakDb, 1) + " dB";
            if (mixStats.hasClipping())
            {
                g.setColour(Theme::bad());
                mixInfo += "  CLIP: " + juce::String(mixStats.clipCount.load());
            }
            else if (mixPeakDb > -3.0f)
                g.setColour(Theme::warn());
            else
                g.setColour(Theme::textSubtle());

            g.drawText(mixInfo, mixRow, juce::Justification::centredLeft);
        }

        // Diagnostic summary at bottom
        area.removeFromTop(4);
        if (area.getHeight() >= lineH * 2 && app.clipDetector.hasAnyClipping())
        {
            g.setFont(ThemeFonts::getInstance().monoFont(9.0f));
            g.setColour(Theme::bad());

            // Find the first clipping source per pad
            for (int padIdx = 0; padIdx < ClipDetectorSystem::kMaxPads; ++padIdx)
            {
                if (area.getHeight() < lineH) break;

                const auto& padProbes = app.clipDetector.padProbes[(size_t)padIdx];
                auto firstClip = padProbes.findFirstClippingNode();
                if (firstClip != NodeId::NumNodes)
                {
                    auto hottest = padProbes.findHottestNode();
                    auto diagRow = area.removeFromTop(lineH);
                    g.drawText("Pad " + juce::String(padIdx + 1)
                               + ": First clip at [" + nodeIdToString(firstClip)
                               + "] | Hottest: [" + nodeIdToString(hottest)
                               + "] pk=" + juce::String(padProbes.probes[(size_t)hottest].getStats().peakLevel.load(), 3),
                               diagRow, juce::Justification::centredLeft);
                }
            }
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(4);
        auto topBar = area.removeFromTop(22);
        enableToggle.setBounds(topBar.removeFromRight(130));
        resetButton.setBounds(topBar.removeFromRight(120));
    }

private:
    void timerCallback() override { repaint(); }

    AppState& app;
    juce::TextButton resetButton;
    juce::ToggleButton enableToggle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodeClipDebugPanel)
};
