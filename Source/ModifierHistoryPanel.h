/*
 ============================================================================== 
  ModifierHistoryPanel.h
  -----------------------------------------------------------------------------
  Lightweight panel showing recent modifier trigger events.
  Stores a ring buffer of the most recent N events with timestamp, modifier,
  and target pads. Provides clear() and addEntry().
 ==============================================================================
*/
#pragma once

#include <JuceHeader.h>
#include "Modifier.h"

class ModifierHistoryPanel : public juce::Component
{
public:
    struct Entry
    {
        juce::String timeString; // HH:MM:SS
        juce::String modifier;   // shortName
        juce::String targets;    // e.g. 1,3,5 or (global)
    };

    ModifierHistoryPanel()
    {
        addAndMakeVisible(titleLabel);
        titleLabel.setText("Modifier History", juce::dontSendNotification);
        auto f = juce::Font(juce::FontOptions().withHeight(14.0f)); f.setBold(true); titleLabel.setFont(f);
        titleLabel.setJustificationType(juce::Justification::centredLeft);

        addAndMakeVisible(editor);
        editor.setReadOnly(true);
        editor.setMultiLine(true);
        editor.setScrollbarsShown(true);
        editor.setCaretVisible(false);
        editor.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
        editor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha(0.25f));
        editor.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
        editor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(2);
        auto header = area.removeFromTop(20);
        titleLabel.setBounds(header);
        editor.setBounds(area);
    }

    void addEntry(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        Entry e;
        e.timeString = juce::Time::getCurrentTime().toString(true, true, true, false);
        e.modifier = desc.shortName;
        if (targets.isEmpty()) e.targets = "(global)"; else {
            for (int i = 0; i < targets.size(); ++i)
                e.targets << (i?",":"") << (targets[i] + 1);
        }

        entries.add(e);
        while (entries.size() > maxEntries)
            entries.remove(0);

        rebuildText();
    }

    void clear() { entries.clear(); rebuildText(); }

private:
    juce::Label titleLabel;
    juce::TextEditor editor;
    juce::Array<Entry> entries;
    static constexpr int maxEntries = 100;

    void rebuildText()
    {
        juce::String text;
        for (int i = entries.size() - 1; i >= 0; --i)
        {
            const auto& e = entries.getReference(i);
            text << e.timeString << " | " << e.modifier << " -> " << e.targets << '\n';
        }
        editor.setText(text, juce::dontSendNotification);
        editor.moveCaretToEnd();
    }
};
