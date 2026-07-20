/*
 ==============================================================================
   HelpPanelContent.h
   --------------------------------------------------------------------------
   In-plugin help / documentation panel displayed in the "Help" tab.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ThemeEngine.h"
#include "ThemeFonts.h"
#include "BuildInfo.h"

class HelpPanelContent : public juce::Component
{
public:
    HelpPanelContent()
    {
        buildContent();

        addAndMakeVisible(viewport);
        viewport.setViewedComponent(&contentHolder, false);
        viewport.setScrollBarsShown(true, false);
        viewport.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId,
                                                  Theme::borderStrong());
    }

    void resized() override
    {
        viewport.setBounds(getLocalBounds());
        contentHolder.setSize(viewport.getWidth() - viewport.getScrollBarThickness(), contentHolder.getHeight());
    }

    void paint(juce::Graphics&) override
    {
        // No opaque fill — BackgroundAnimator paints the background
    }

private:
    // ===========================================================================

    struct Section
    {
        juce::String heading;
        juce::String body;

        // Optional: each row is { key, description }
        std::vector<std::pair<juce::String, juce::String>> table;
    };

    // ===========================================================================

    class ContentHolder : public juce::Component
    {
    public:
        void paint(juce::Graphics& g) override
        {
            // No opaque fill — BackgroundAnimator paints the background

            const int padX = 28;
            int y = 20;
            const int w = getWidth() - padX * 2;

            for (const auto& sec : sections)
            {
                // ----- Heading -----
                g.setFont(ThemeFonts::getInstance().headingFont(17.0f));
                g.setColour(Theme::accent());
                g.drawText(sec.heading, padX, y, w, 22, juce::Justification::left, false);
                y += 26;

                // Divider
                g.setColour(Theme::border());
                g.fillRect(padX, y, w, 1);
                y += 8;

                // ----- Table (if present) -----
                if (!sec.table.empty())
                {
                    const int keyColW = 220;
                    const int rowH    = 22;

                    g.setFont(ThemeFonts::getInstance().monoFont(13.0f));

                    bool alt = false;
                    for (const auto& row : sec.table)
                    {
                        if (alt)
                        {
                            g.setColour(Theme::panel());
                            g.fillRect(padX, y, w, rowH);
                        }
                        alt = !alt;

                        // Key badge
                        juce::Rectangle<int> badgeRect(padX + 2, y + 3, keyColW - 8, rowH - 6);
                        g.setColour(Theme::panelAlt());
                        g.fillRoundedRectangle(badgeRect.toFloat(), 3.0f);
                        g.setColour(Theme::border().interpolatedWith(Theme::borderStrong(), 0.2f));
                        g.drawRoundedRectangle(badgeRect.toFloat(), 3.0f, 1.0f);

                        g.setColour(Theme::text());
                        g.drawText(row.first, badgeRect.reduced(6, 0),
                                   juce::Justification::centredLeft, true);

                        // Description
                        g.setColour(Theme::text().withAlpha(0.85f));
                        g.drawText(row.second, padX + keyColW, y, w - keyColW, rowH,
                                   juce::Justification::centredLeft, true);

                        y += rowH;
                    }
                    y += 4;
                }

                // ----- Body text (if present) -----
                if (sec.body.isNotEmpty())
                {
                    auto helpBodyFont = ThemeFonts::getInstance().bodyFont(13.5f);
                    auto bodyColour = Theme::text().withAlpha(0.85f);
                    g.setFont(helpBodyFont);
                    g.setColour(bodyColour);

                    juce::AttributedString as;
                    as.setWordWrap(juce::AttributedString::byWord);
                    as.setJustification(juce::Justification::topLeft);
                    as.append(sec.body, helpBodyFont, bodyColour);

                    juce::TextLayout tl;
                    tl.createLayout(as, (float) w);
                    tl.draw(g, juce::Rectangle<float>((float) padX, (float) y, (float) w, 2000.0f));

                    y += (int) tl.getHeight() + 4;
                }

                y += 18; // gap between sections
            }

            // Cache total height so the viewport can scroll correctly
            const_cast<ContentHolder*>(this)->setSize(getWidth(), y + 20);
        }

        std::vector<Section> sections;
    };

    // ===========================================================================

    ContentHolder contentHolder;
    juce::Viewport viewport;

    void buildContent()
    {
        // ---- Quick start ----
        {
            Section s;
            s.heading = "Quick Start";
            s.body =
                "1.  Load an audio sample onto each pad: drag a WAV/AIFF/FLAC file from Finder "
                "onto any pad, or click an empty pad to open a file chooser.\n\n"
                "2.  Start your DAW transport.  The plugin will follow the host timeline "
                "and begin looping all loaded buffers.\n\n"
                "3.  Select pads you want to affect (click to toggle them on/off).  "
                "The selected pads will be targeted by the next modifier.  "
                "If no pads are selected when the timer fires, 1-4 pads are chosen automatically.\n\n"
                "4.  Watch the upcoming-modifier display in the Session tab header.  "
                "When the bar counter reaches zero the modifier fires, "
                "the pads flash, and the next modifier is randomly queued.\n\n"
                "5.  Use the Probability tab to adjust how likely each modifier type is to appear.";
            contentHolder.sections.push_back(s);
        }

        // ---- Important: DAW buffer size ----
        {
            Section s;
            s.heading = "Important: Set DAW Buffer Size to Maximum";
            s.body =
                "BufferTest performs time-stretching and pitch-shifting in real time on the audio "
                "thread.  These operations need large, contiguous blocks of samples to work "
                "correctly.\n\n"
                "Set your DAW's audio buffer size to the LARGEST available value "
                "(typically 2048 or 4096 samples).  "
                "Using a small buffer (e.g. 64 or 128 samples) will cause audible glitching, "
                "clicking, and tearing when speed or pitch modifiers are active.\n\n"
                "In most DAWs this is found under:\n"
                "  Preferences > Audio > Buffer Size  (or Block Size / Device Buffer).";
            contentHolder.sections.push_back(s);
        }

        // ---- Pad interactions ----
        {
            Section s;
            s.heading = "Pad Interactions";
            s.table = {
                { "Click",                       "Toggle pad selection on / off" },
                { "Right-click",                 "Open context menu (Load Sample, Remove Sample, "
                                                 "MIDI Learn, Clear MIDI Note)" },
                { "Drag & drop file onto pad",   "Load audio file into that pad" },
                { "Shift + Click",               "Enter MIDI learn mode for this pad - "
                                                 "play a MIDI note to assign it; "
                                                 "click again to cancel" },
                { "Cmd + Click  (macOS)",        "Clear the MIDI note assignment for this pad" },
                { "Alt + Click",                 "Clear the MIDI note assignment for this pad (alias)" },
                { "Shift + Cmd + Click  (macOS)","Remove the loaded sample from this pad" },
            };
            s.body =
                "The Modifiers toggle also supports the same keyboard shortcuts and "
                "right-click context menu for MIDI note assignment.";
            contentHolder.sections.push_back(s);
        }

        // ---- Session tab controls ----
        {
            Section s;
            s.heading = "Session Tab Controls";
            s.table = {
                { "Modifiers toggle",     "Enable or disable the modifier scheduler entirely. "
                                          "Supports MIDI note assignment (right-click or "
                                          "Shift+Click to learn, Cmd/Alt+Click to clear)." },
                { "Master Volume knob",   "Boost or cut overall output level (-12 dB to +12 dB); "
                                          "applies equally to all pads and output buses" },
                { "Preset buttons (A\u2013D)","Click to recall a saved modifier snapshot. "
                                          "If the slot is empty, clicking saves the current state. "
                                          "Double-click to overwrite an existing preset. "
                                          "Right-click for save/recall/clear/MIDI options." },
            };
            contentHolder.sections.push_back(s);
        }

        // ---- Settings tab ----
        {
            Section s;
            s.heading = "Modifier Presets";
            s.body =
                "Modifier Presets let you snapshot the current state of all buffer transforms "
                "(speed, stretch, pitch, slicing, ping-pong) and effects (reverb, delay, filter, "
                "tremolo, chorus, auto-pan, volume ramp) across all 8 pads.\n\n"
                "Four preset slots (A\u2013D) are available in the Session tab.  "
                "Click an empty slot to save, click a filled slot to recall.  "
                "Double-click any slot to force-overwrite it with the current state.\n\n"
                "Presets do NOT save or restore Parts settings, pad file assignments, "
                "probability weights, or timing configuration.  "
                "Presets are saved with your DAW session and restored on reload.";
            s.table = {
                { "Click (empty slot)",       "Save the current modifier states into this preset" },
                { "Click (filled slot)",      "Recall the saved modifier states from this preset" },
                { "Double-click",             "Force-save (overwrite) the current states into this preset" },
                { "Right-click",              "Open context menu: Save, Recall, Clear, MIDI Learn, Clear MIDI" },
                { "Shift + Click",            "Enter MIDI learn mode for this preset slot" },
                { "Cmd + Click  (macOS)",     "Clear the MIDI note assignment for this preset" },
            };
            contentHolder.sections.push_back(s);
        }

        // ---- Settings tab ----
        {
            Section s;
            s.heading = "Settings Tab";
            s.table = {
                { "Theme dropdown",       "Choose a visual theme for the plugin interface" },
                { "Parts selector",       "Split each buffer into 1-4 equal sections (A-D); "
                                          "change takes effect on the next modifier trigger "
                                          "when the transport is running" },
                { "Bars / Modifier slider","How many bars elapse between each modifier application "
                                          "(1-16 bars)" },
            };
            contentHolder.sections.push_back(s);
        }

        // ---- Probability tab ----
        {
            Section s;
            s.heading = "Probability Tab";
            s.body =
                "Each modifier type has a probability slider.  "
                "Set a slider to 0 to completely disable that modifier.  "
                "All enabled sliders are normalised automatically - "
                "a modifier at 2.0 is twice as likely as one at 1.0.\n\n"
                "Probability sliders are exposed as DAW automation parameters and can also be "
                "controlled via MIDI CC: click the small CC button beside a slider to enter "
                "MIDI CC learn mode, then move any CC knob/fader on your controller.";
            contentHolder.sections.push_back(s);
        }

        // ---- Probability Presets ----
        {
            Section s;
            s.heading = "Probability Presets";
            s.body =
                "Probability Presets let you save and recall entire probability configurations "
                "(all modifier weights and pad target probabilities) as named presets that "
                "persist globally across all projects and DAW sessions.\n\n"
                "A preset bar at the top of the Probability tab provides quick access:";
            s.table = {
                { "Preset dropdown",    "Select a saved preset to load it instantly" },
                { "Save",               "Overwrite the currently selected preset with the "
                                         "current probability settings" },
                { "Save As",            "Save the current settings under a new name; "
                                         "confirms before overwriting an existing preset" },
                { "Delete",             "Delete the currently selected preset (with confirmation)" },
            };
            contentHolder.sections.push_back(s);
        }

        // ---- MIDI ----
        {
            Section s;
            s.heading = "MIDI Control";
            s.table = {
                { "Note 36 (C1)  - bottom-left",  "Toggle pad 5 (bottom row, col 1)" },
                { "Note 37 (C#1) - bottom row",   "Toggle pad 6 (bottom row, col 2)" },
                { "Note 38 (D1)  - bottom row",   "Toggle pad 7 (bottom row, col 3)" },
                { "Note 39 (D#1) - bottom-right", "Toggle pad 8 (bottom row, col 4)" },
                { "Note 40 (E1)  - top-left",     "Toggle pad 1 (top row, col 1)" },
                { "Note 41 (F1)  - top row",      "Toggle pad 2 (top row, col 2)" },
                { "Note 42 (F#1) - top row",      "Toggle pad 3 (top row, col 3)" },
                { "Note 43 (G1)  - top-right",    "Toggle pad 4 (top row, col 4)" },
            };
            s.body =
                "Use Shift+Click on any pad to re-assign it to a different MIDI note.  "
                "Compatible with most drum-pad controllers (Akai MPD, NI Maschine, "
                "Novation Launchpad, etc.).  "
                "Note-off is ignored; velocity is ignored.\n\n"
                "The Modifiers toggle can also be assigned to a MIDI note so you can "
                "enable or disable the modifier scheduler from your controller. "
                "Right-click the toggle and choose MIDI Learn, or Shift+Click it. "
                "Use Cmd+Click (macOS) or Alt+Click to clear the assignment.";
            contentHolder.sections.push_back(s);
        }

        // ---- Modifier types ----
        {
            Section s;
            s.heading = "Modifier Types";
            s.table = {
                { "Reverse",          "Flip playback direction of the buffer" },
                { "Speed",            "Multiply playback rate: x0.25, x0.5, x1, or x2 "
                                      "(time-stretched to maintain pitch)" },
                { "Stretch",          "Time-stretching variant of the speed modifier" },
                { "Pitch Up Octave",  "Raise pitch by one octave" },
                { "Pitch Down Octave","Lower pitch by one octave" },
                { "Beat Slice",       "Subdivide the buffer into note-length slices "
                                      "(1/4, 1/8, 1/16, 1/32...) and play them in random order" },
                { "Ping Pong",        "Alternate forward and reverse playback on each loop cycle" },
                { "Delay",            "Enable delay wet signal on the buffer's FX chain" },
                { "Reverb",           "Enable reverb wet signal on the buffer's FX chain" },
                { "Low-Pass Filter",  "Enable low-pass filter on the buffer's FX chain" },
                { "High-Pass Filter", "Enable high-pass filter on the buffer's FX chain" },
                { "Volume Ramp",      "Fade the buffer volume down over n bars" },
                { "Tremolo",          "Apply volume modulation (LFO) to the buffer" },
                { "Reset",            "Remove all active modifiers and return buffer to normal" },
            };
            contentHolder.sections.push_back(s);
        }

        // ---- Multi-output routing ----
        {
            Section s;
            s.heading = "Multi-Output Routing";
            s.body =
                "The plugin exposes 8 independent stereo output buses (plus a master mix bus).\n\n"
                "In your DAW, enable the additional outputs on the plugin instance to route "
                "each pad to its own mixer channel:  "
                "buffer 1 -> output 1/2,  buffer 2 -> output 3/4, ... buffer 8 -> output 15/16.\n\n"
                "If the DAW only activates a subset of buses, the remaining buffers are "
                "automatically folded into the master mix output.";
            contentHolder.sections.push_back(s);
        }

        // ---- Tips & known issues ----
        {
            Section s;
            s.heading = "Tips & Known Issues";
            s.body =
                "- Modifier display and scheduling are only active while the plugin editor "
                "window is open.  Closing the window suspends modifiers; re-opening resumes them.\n\n"
                "- Octave pitch-shifting may produce audible artifacts at extreme settings.  "
                "Staying within +/-1 octave typically sounds cleaner.\n\n"
                "- Sample loading is always done off the audio thread - "
                "you can load new files at any time without interrupting playback.\n\n"
                "- If a loaded file goes missing (e.g. sample drive unmounted) "
                "the pad shows an error state and outputs silence.\n\n"
                "- Use the Master Volume knob to compensate if the default -12 dB "
                "per-pad headroom reduction makes the mix too quiet.";
            contentHolder.sections.push_back(s);
        }

        // ---- Version / licensing ----
        {
            Section s;
            s.heading = "About & License";
            s.table = {
                { "Version",     LOOP_BREAKER_VERSION },
                { "Built",       LOOP_BREAKER_BUILD_TIMESTAMP },
                { "License",     "GNU GPL v3 or later" },
            };
            s.body =
                "Copyright (C) 2025-2026 Glow Machine, LLC.\n\n"
                "Loop Breaker is free software: you can redistribute it and/or modify it under "
                "the terms of the GNU General Public License as published by the Free Software "
                "Foundation, either version 3 of the License, or (at your option) any later version.\n\n"
                "Loop Breaker is distributed without any warranty; without even the implied "
                "warranty of merchantability or fitness for a particular purpose. The complete "
                "license is included in the source distribution as LICENSE and is available at "
                "gnu.org/licenses/gpl-3.0.html.";
            contentHolder.sections.push_back(s);
        }

        // Set an initial height; will be updated when paint() runs
        contentHolder.setSize(600, 1200);
    }
};
