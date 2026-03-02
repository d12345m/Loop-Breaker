/*
 ==============================================================================
   PresetBarComponent.h
   --------------------------------------------------------------------------
   Horizontal bar of 4 preset buttons (A–D) for the Session tab.
   Supports click-to-recall, right-click context menu, MIDI learn, and
   visual indicators for occupied / empty / learn states.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ModifierPreset.h"
#include "ThemeEngine.h"
#include "ThemeFonts.h"

class PresetBarComponent : public juce::Component
{
public:
    // Callbacks set by the parent (PluginEditorContent)
    std::function<void(int)> onSavePreset;          // slot index
    std::function<void(int)> onRecallPreset;         // slot index
    std::function<void(int)> onClearPreset;          // slot index
    std::function<void(int)> onMidiLearnRequest;     // slot index
    std::function<void(int)> onClearMidiNote;        // slot index

    PresetBarComponent()
    {
        for (int i = 0; i < 4; ++i)
        {
            buttons[i].reset(new juce::Component());
            buttons[i]->setInterceptsMouseClicks(false, false);
            addAndMakeVisible(buttons[i].get());
        }
    }

    void setSlotOccupied(int index, bool occupied)
    {
        if (index >= 0 && index < 4)
        {
            slotOccupied[static_cast<size_t>(index)] = occupied;
            repaint();
        }
    }

    void setMidiNote(int index, int note)
    {
        if (index >= 0 && index < 4)
        {
            midiNotes[static_cast<size_t>(index)] = note;
            repaint();
        }
    }

    void setMidiLearnActive(int index, bool active)
    {
        if (index >= 0 && index < 4)
        {
            midiLearnActive[static_cast<size_t>(index)] = active;
            repaint();
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const int spacing = 6;
        const int totalSpacing = spacing * 3; // 3 gaps between 4 buttons
        const int btnWidth = (area.getWidth() - totalSpacing) / 4;

        for (int i = 0; i < 4; ++i)
        {
            auto btnArea = area.removeFromLeft(btnWidth);
            buttons[i]->setBounds(btnArea);
            if (i < 3)
                area.removeFromLeft(spacing);
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();
        static const char* labels[] = { "A", "B", "C", "D" };

        for (int i = 0; i < 4; ++i)
        {
            auto btnBounds = buttons[i]->getBounds().toFloat();
            const bool occupied = slotOccupied[static_cast<size_t>(i)];
            const bool learning = midiLearnActive[static_cast<size_t>(i)];
            const int midiNote = midiNotes[static_cast<size_t>(i)];

            // Background
            if (occupied)
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
            g.setFont(ThemeFonts::getInstance().monoBoldFont(15.0f));
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
            if (onRecallPreset) onRecallPreset(slot);
        }
        else
        {
            if (onSavePreset) onSavePreset(slot);
        }
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        const int slot = getSlotAtPoint(e.getPosition());
        if (slot < 0) return;

        // Double-click: force save (overwrite)
        if (onSavePreset) onSavePreset(slot);
    }

private:
    std::unique_ptr<juce::Component> buttons[4];
    std::array<bool, 4> slotOccupied { false, false, false, false };
    std::array<int, 4>  midiNotes { -1, -1, -1, -1 };
    std::array<bool, 4> midiLearnActive { false, false, false, false };

    int getSlotAtPoint(juce::Point<int> point) const
    {
        for (int i = 0; i < 4; ++i)
        {
            if (buttons[i]->getBounds().contains(point))
                return i;
        }
        return -1;
    }

    void showContextMenu(int slot)
    {
        static const char* names[] = { "A", "B", "C", "D" };
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
                    case 1: if (onSavePreset) onSavePreset(slot); break;
                    case 2: if (onRecallPreset) onRecallPreset(slot); break;
                    case 3: if (onClearPreset) onClearPreset(slot); break;
                    case 4: if (onMidiLearnRequest) onMidiLearnRequest(slot); break;
                    case 5: if (onClearMidiNote) onClearMidiNote(slot); break;
                    default: break;
                }
            });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBarComponent)
};
