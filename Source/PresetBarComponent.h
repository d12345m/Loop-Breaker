/*
 ==============================================================================
   PresetBarComponent.h
   --------------------------------------------------------------------------
   Horizontal bar of 8 preset buttons (A–H) for the Session tab.
   Supports click-to-recall, right-click context menu, MIDI learn, and
   visual indicators for occupied / empty / learn states.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ModifierPreset.h"
#include "ThemeEngine.h"
#include "ThemeFonts.h"
#include "Animator.h"

class PresetBarComponent : public juce::Component,
                           private juce::Timer
{
public:
    // Callbacks set by the parent (PluginEditorContent)
    std::function<void(int)> onSavePreset;          // slot index
    std::function<void(int)> onRecallPreset;         // slot index
    std::function<void(int)> onClearPreset;          // slot index
    std::function<void(int)> onMidiLearnRequest;     // slot index
    std::function<void(int)> onClearMidiNote;        // slot index

    enum class HighlightType { None, Save, Recall };

    PresetBarComponent()
    {
        for (int i = 0; i < kNumSlots; ++i)
        {
            buttons[i].reset(new juce::Component());
            buttons[i]->setInterceptsMouseClicks(false, false);
            addAndMakeVisible(buttons[i].get());
        }
        startTimerHz(15); // drive highlight animations (reduced from 30 Hz)
    }

    ~PresetBarComponent() override
    {
        stopTimer();
    }

    void setSlotOccupied(int index, bool occupied)
    {
        if (index >= 0 && index < kNumSlots)
        {
            slotOccupied[static_cast<size_t>(index)] = occupied;
            repaint();
        }
    }

    void setMidiNote(int index, int note)
    {
        if (index >= 0 && index < kNumSlots)
        {
            midiNotes[static_cast<size_t>(index)] = note;
            repaint();
        }
    }

    void setMidiLearnActive(int index, bool active)
    {
        if (index >= 0 && index < kNumSlots)
        {
            midiLearnActive[static_cast<size_t>(index)] = active;
            repaint();
        }
    }

    void resized() override
    {
        // Derive from the same 4-column grid as PadGridComponent,
        // splitting each pad column into 2 preset buttons so edges align.
        auto area = getLocalBounds().reduced(4);
        const int padCols = 4;
        const int padColW = area.getWidth() / padCols;

        for (int i = 0; i < kNumSlots; ++i)
        {
            int padCol = i / 2;
            int sub    = i % 2;
            int colLeft = area.getX() + padCol * padColW;
            int halfW   = padColW / 2;
            // Give the second sub-button the remainder so both halves
            // sum to the full padColW, avoiding accumulated rounding loss.
            int x = colLeft + (sub == 0 ? 0 : halfW);
            int w = (sub == 0) ? halfW : (padColW - halfW);
            juce::Rectangle<int> btnRect(x, area.getY(), w - 4, area.getHeight());
            buttons[i]->setBounds(btnRect.reduced(4, 0));
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();
        static const char* labels[] = { "A", "B", "C", "D", "E", "F", "G", "H" };

        for (int i = 0; i < kNumSlots; ++i)
        {
            auto btnBounds = buttons[i]->getBounds().toFloat();
            const bool occupied = slotOccupied[static_cast<size_t>(i)];
            const bool learning = midiLearnActive[static_cast<size_t>(i)];
            const int midiNote = midiNotes[static_cast<size_t>(i)];

            const bool isHighlighted = (highlightedSlot == i && highlightType != HighlightType::None);

            // Background
            if (isHighlighted)
            {
                // Highlighted background: accent2 (teal) for recall, accent1 (periwinkle) for save
                auto hlColour = (highlightType == HighlightType::Recall)
                    ? palette.accent2 : palette.accent1;
                g.setColour(hlColour.withAlpha(0.15f + highlightGlowAlpha[static_cast<size_t>(i)] * 0.35f));
            }
            else if (occupied)
                g.setColour(palette.accent1.withAlpha(0.25f));
            else
                g.setColour(palette.panelAlt.withAlpha(0.5f));
            g.fillRoundedRectangle(btnBounds, 5.0f);

            // Border
            if (learning)
            {
                // Marching-ants learn indicator
                juce::Path outline;
                outline.addRoundedRectangle(btnBounds.reduced(1.0f), 4.0f);
                const float dashLengths[] = { 6.0f, 4.0f };
                juce::PathStrokeType strokeType(2.0f);
                juce::Path dashed;
                strokeType.createDashedStroke(dashed, outline, dashLengths, 2);
                g.setColour(palette.warn);
                g.strokePath(dashed, strokeType);
            }
            else if (isHighlighted)
            {
                // Glow border matching highlight type
                auto hlColour = (highlightType == HighlightType::Recall)
                    ? palette.accent2 : palette.accent1;
                float glowA = highlightGlowAlpha[static_cast<size_t>(i)];
                g.setColour(hlColour.withAlpha(0.6f + glowA * 0.4f));
                g.drawRoundedRectangle(btnBounds.reduced(0.5f), 5.0f, 2.0f);
                // Outer glow ring
                g.setColour(hlColour.withAlpha(glowA * 0.15f));
                g.drawRoundedRectangle(btnBounds.expanded(2.0f), 6.0f, 1.5f);
            }
            else if (occupied)
            {
                g.setColour(palette.accent1.withAlpha(0.6f));
                g.drawRoundedRectangle(btnBounds.reduced(0.5f), 5.0f, 1.5f);
            }
            else
            {
                g.setColour(palette.border);
                g.drawRoundedRectangle(btnBounds.reduced(0.5f), 5.0f, 1.0f);
            }

            // Label (A/B/C/D)
            g.setColour(occupied ? palette.textPrimary : palette.textSecondary);
            g.setFont(ThemeFonts::getInstance().monoBoldFont(13.0f));
            auto labelArea = btnBounds;

            // If there's a MIDI note badge or LEARN badge, shift label up
            if (learning || midiNote >= 0)
                labelArea = btnBounds.withTrimmedBottom(14.0f);

            g.drawText(labels[i], labelArea, juce::Justification::centred);

            // MIDI Learn badge
            if (learning)
            {
                auto badgeRect = juce::Rectangle<float>(
                    btnBounds.getCentreX() - 24.0f,
                    btnBounds.getBottom() - 16.0f,
                    48.0f, 14.0f);
                g.setColour(palette.warn);
                g.fillRoundedRectangle(badgeRect, 3.0f);
                g.setColour(palette.bg);
                g.setFont(ThemeFonts::getInstance().monoFont(10.0f));
                g.drawText("LEARN", badgeRect, juce::Justification::centred);
            }
            else if (midiNote >= 0)
            {
                // MIDI note badge
                auto badgeRect = juce::Rectangle<float>(
                    btnBounds.getCentreX() - 14.0f,
                    btnBounds.getBottom() - 16.0f,
                    28.0f, 14.0f);
                g.setColour(palette.panelAlt);
                g.fillRoundedRectangle(badgeRect, 3.0f);
                g.setColour(palette.textSecondary);
                g.setFont(ThemeFonts::getInstance().monoFont(10.0f));
                g.drawText(juce::String(midiNote), badgeRect, juce::Justification::centred);
            }
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const int slot = getSlotAtPoint(e.getPosition());
        if (slot < 0) return;

        // Right-click: context menu
        if (e.mods.isPopupMenu())
        {
            showContextMenu(slot);
            return;
        }

        // Shift+click: MIDI learn
        if (e.mods.isShiftDown() && !e.mods.isCommandDown())
        {
            if (onMidiLearnRequest) onMidiLearnRequest(slot);
            return;
        }

        // Cmd+click (macOS) or Alt+click: clear MIDI note
        if (e.mods.isCommandDown() || e.mods.isAltDown())
        {
            if (onClearMidiNote) onClearMidiNote(slot);
            return;
        }

        // Normal click: recall if occupied, save if empty
        if (slotOccupied[static_cast<size_t>(slot)])
        {
            triggerHighlight(slot, HighlightType::Recall);
            if (onRecallPreset) onRecallPreset(slot);
        }
        else
        {
            triggerHighlight(slot, HighlightType::Save);
            if (onSavePreset) onSavePreset(slot);
        }
    }

    /** Trigger a transient highlight on a preset slot.
        Save = accent1 (periwinkle), Recall = accent2 (teal). */
    void triggerHighlight(int slot, HighlightType type)
    {
        if (slot < 0 || slot >= kNumSlots) return;

        highlightedSlot = slot;
        highlightType = type;

        // Animate glow: quick ramp up then fade out
        highlightAnimators[static_cast<size_t>(slot)].start(
            600, // duration ms
            [this, slot](float progress)
            {
                // Quick in (first 20%), then fade out
                float alpha;
                if (progress < 0.2f)
                    alpha = progress / 0.2f;          // 0→1 ramp
                else
                    alpha = 1.0f - (progress - 0.2f) / 0.8f; // 1→0 fade
                highlightGlowAlpha[static_cast<size_t>(slot)] = juce::jlimit(0.0f, 1.0f, alpha);
                repaint();
            },
            [this, slot]()
            {
                highlightGlowAlpha[static_cast<size_t>(slot)] = 0.0f;
                // Only clear if this slot is still the highlighted one
                if (highlightedSlot == slot)
                {
                    highlightedSlot = -1;
                    highlightType = HighlightType::None;
                }
                repaint();
            },
            Animator::Easing::EaseOut);

        repaint();
    }

    /** Start a looping pulse glow on a preset slot, indicating the recall
        is pending and will be applied at the next modifier point.
        @p bpm  current session tempo — pulse cycle = one beat. */
    void startPendingGlow(int slot, double bpm = 120.0)
    {
        if (slot < 0 || slot >= kNumSlots) return;

        // Stop any one-shot highlight that mouseDown may have started,
        // so its completion callback can't reset the glow alpha to 0.
        highlightAnimators[static_cast<size_t>(slot)].stop();

        pendingGlowSlot = slot;
        highlightedSlot = slot;
        highlightType = HighlightType::Recall;

        // Pulse cycle = one beat at the current BPM (clamped to reasonable range)
        const double safeBpm = juce::jlimit(30.0, 300.0, bpm);
        const int cycleDurationMs = juce::roundToInt(60000.0 / safeBpm);

        // Pulsing glow: sinusoidal 0.35→1.0 over one beat
        pendingGlowAnimators[static_cast<size_t>(slot)].startLoop(
            cycleDurationMs,
            [this, slot](float progress)
            {
                // Sine-based pulse between 0.35 and 1.0
                float alpha = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(progress * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi));
                highlightGlowAlpha[static_cast<size_t>(slot)] = alpha;
                repaint();
            },
            Animator::Easing::Linear);

        repaint();
    }

    /** Stop the pending glow on a slot (called when the preset is applied). */
    void clearPendingGlow(int slot)
    {
        if (slot < 0 || slot >= kNumSlots) return;

        pendingGlowAnimators[static_cast<size_t>(slot)].stop();
        highlightAnimators[static_cast<size_t>(slot)].stop();

        highlightGlowAlpha[static_cast<size_t>(slot)] = 0.0f;

        if (pendingGlowSlot == slot)
            pendingGlowSlot = -1;

        if (highlightedSlot == slot)
        {
            highlightedSlot = -1;
            highlightType = HighlightType::None;
        }

        repaint();
    }

    /** Clear any pending glow regardless of slot. */
    void clearAllPendingGlows()
    {
        for (int i = 0; i < kNumSlots; ++i)
        {
            pendingGlowAnimators[static_cast<size_t>(i)].stop();
        }
        pendingGlowSlot = -1;
    }

    /** Returns the slot currently showing a pending glow, or -1 if none. */
    int getPendingGlowSlot() const { return pendingGlowSlot; }

private:
    void timerCallback() override
    {
        const double dt = 1.0 / 15.0;
        for (int i = 0; i < kNumSlots; ++i)
        {
            highlightAnimators[static_cast<size_t>(i)].tick(dt);
            pendingGlowAnimators[static_cast<size_t>(i)].tick(dt);
        }
    }

    static constexpr int kNumSlots = 8;

    std::unique_ptr<juce::Component> buttons[kNumSlots];
    std::array<bool, kNumSlots> slotOccupied { false, false, false, false, false, false, false, false };
    std::array<int, kNumSlots>  midiNotes { -1, -1, -1, -1, -1, -1, -1, -1 };
    std::array<bool, kNumSlots> midiLearnActive { false, false, false, false, false, false, false, false };

    // Highlight state
    int highlightedSlot = -1;
    HighlightType highlightType = HighlightType::None;
    std::array<float, kNumSlots> highlightGlowAlpha { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<Animator, kNumSlots> highlightAnimators;

    // Pending recall glow state (looping pulse until applied)
    int pendingGlowSlot = -1;
    std::array<Animator, kNumSlots> pendingGlowAnimators;

    int getSlotAtPoint(juce::Point<int> point) const
    {
        for (int i = 0; i < kNumSlots; ++i)
        {
            if (buttons[i]->getBounds().contains(point))
                return i;
        }
        return -1;
    }

    void showContextMenu(int slot)
    {
        static const char* names[] = { "A", "B", "C", "D", "E", "F", "G", "H" };
        const bool occupied = slotOccupied[static_cast<size_t>(slot)];
        const int midiNote = midiNotes[static_cast<size_t>(slot)];

        juce::PopupMenu menu;

        // Save
        menu.addItem(1, "Save to Preset " + juce::String(names[slot]));

        // Recall
        menu.addItem(2, "Recall Preset " + juce::String(names[slot]), occupied);

        // Clear
        menu.addItem(3, "Clear Preset " + juce::String(names[slot]), occupied);

        menu.addSeparator();

        // MIDI Learn
        juce::String learnLabel = "MIDI Learn";
        learnLabel += "    [Shift+Click]";
        menu.addItem(4, learnLabel);

        // Clear MIDI Note
        juce::String clearLabel = "Clear MIDI Note";
       #if JUCE_MAC
        clearLabel += "    [Cmd+Click]";
       #else
        clearLabel += "    [Alt+Click]";
       #endif
        menu.addItem(5, clearLabel, midiNote >= 0);

        if (midiNote >= 0)
        {
            menu.addSeparator();
            menu.addItem(0, "MIDI Note: " + juce::String(midiNote), false);
        }

        menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
            [this, slot](int result)
            {
                switch (result)
                {
                    case 1: triggerHighlight(slot, HighlightType::Save); if (onSavePreset) onSavePreset(slot); break;
                    case 2: triggerHighlight(slot, HighlightType::Recall); if (onRecallPreset) onRecallPreset(slot); break;
                    case 3: if (onClearPreset) onClearPreset(slot); break;
                    case 4: if (onMidiLearnRequest) onMidiLearnRequest(slot); break;
                    case 5: if (onClearMidiNote) onClearMidiNote(slot); break;
                    default: break;
                }
            });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBarComponent)
};
