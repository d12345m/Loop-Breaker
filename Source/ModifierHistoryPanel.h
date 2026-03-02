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
#include "ThemeEngine.h"
#include "ThemeFonts.h"

class ModifierHistoryPanel : public juce::Component, public juce::ListBoxModel
{
public:
    struct Entry
    {
        juce::String timeString; // HH:MM:SS
        juce::String modifier;   // shortName
        juce::String targets;    // e.g. 1,3,5 or (global)
        juce::String details;    // descriptor description (randomized params)
        ModifierType type { ModifierType::Unknown };
    };

    ModifierHistoryPanel()
    {
        addAndMakeVisible(titleLabel);
        titleLabel.setText("Modifier History", juce::dontSendNotification);
        auto f = ThemeFonts::getInstance().headingFont(14.0f); titleLabel.setFont(f);
        titleLabel.setJustificationType(juce::Justification::centredLeft);

        addAndMakeVisible(clearButton);
        clearButton.onClick = [this]{ clear(); };
        addAndMakeVisible(countLabel);
        countLabel.setJustificationType(juce::Justification::centredRight);
        countLabel.setColour(juce::Label::textColourId, Theme::textSubtle());

        list.setModel(this);
        list.setRowHeight(18);
        list.setColour(juce::ListBox::backgroundColourId, Theme::panelAlt());
        addAndMakeVisible(list);
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        g.setColour(Theme::panel());
        g.fillRect(bounds);
        g.setColour(Theme::border());
        g.drawRect(bounds, 1);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(2);
        auto header = area.removeFromTop(22);
        auto leftHeader = header.removeFromLeft(header.getWidth() * 0.5f);
        titleLabel.setBounds(leftHeader);
        countLabel.setBounds(header.removeFromLeft(header.getWidth() - 60));
        clearButton.setBounds(header);
        list.setBounds(area);
    }

    void addEntry(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        Entry e;
        e.timeString = juce::Time::getCurrentTime().toString(true, true, true, false);
        e.modifier = desc.shortName;
    e.type = desc.type;
    e.details = desc.description; // include randomized details if present
        if (targets.isEmpty()) e.targets = "(global)"; else {
            for (int i = 0; i < targets.size(); ++i)
                e.targets << (i?",":"") << (targets[i] + 1);
        }

        entries.add(e);
        while (entries.size() > maxEntries)
            entries.remove(0);
        list.updateContent();
        list.scrollToEnsureRowIsOnscreen(entries.size()-1);
        updateCount();
    }

    void clear() { entries.clear(); list.updateContent(); updateCount(); }

    // ListBoxModel
    int getNumRows() override { return entries.size(); }

    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override
    {
        if (rowIsSelected)
            g.fillAll(Theme::accent().withAlpha(0.12f));
        else if (rowNumber % 2 == 1)
            g.fillAll(Theme::panelAlt());
        if (rowNumber < 0 || rowNumber >= entries.size()) return;
        const auto& e = entries.getReference(rowNumber);

        juce::Colour typeColour = Theme::textSubtle();
        switch (e.type)
        {
            case ModifierType::Reverse:         typeColour = Theme::accent2(); break;
            case ModifierType::Speed:           typeColour = Theme::accent(); break;
            case ModifierType::ResetAll:        typeColour = Theme::warn(); break;
            case ModifierType::BeatSliceRandom: typeColour = Theme::good(); break;
            case ModifierType::ArpSlice:        typeColour = Theme::good(); break;
            default:                            typeColour = Theme::textSubtle(); break;
        }

    juce::String extra = e.details.isNotEmpty() ? ("  |  " + e.details) : juce::String();
    auto text = e.timeString + "  |  " + e.modifier + "  ->  " + e.targets + extra;
        g.setColour(typeColour);
        g.setFont(ThemeFonts::getInstance().monoFont(12.0f));
        g.drawFittedText(text, { 6, 0, width - 12, height }, juce::Justification::centredLeft, 1);
    }

private:
    juce::Label titleLabel;
    juce::TextButton clearButton { "Clear" };
    juce::Label countLabel;
    juce::ListBox list { "history", this };
    juce::Array<Entry> entries;
    static constexpr int maxEntries = 100;

    void updateCount()
    {
        countLabel.setText("Total: " + juce::String(entries.size()), juce::dontSendNotification);
    }
};
