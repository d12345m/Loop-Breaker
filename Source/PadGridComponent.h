/*
 ============================================================================== 
   PadGridComponent.h
   --------------------------------------------------------------------------
   Simple 2x4 grid of pad toggle buttons representing the 8 audio buffers.
   Provides selection state for scheduling modifiers (user targeted buffers).
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

// Simple 2x4 pad grid showing selectable pads and (new) filename indicators.
class PadGridComponent : public juce::Component,
                         private juce::Timer
{
public:
    PadGridComponent()
    {
        for (int i = 0; i < numPads; ++i)
        {
            auto* btn = padButtons.add(new juce::ToggleButton("Pad " + juce::String(i + 1)));
            btn->setClickingTogglesState(true);
            btn->onClick = [this]{ if (selectionChanged) selectionChanged(); };
            addAndMakeVisible(btn);

            auto* label = padFileLabels.add(new juce::Label());
            label->setJustificationType(juce::Justification::centred);
            label->setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
            label->setInterceptsMouseClicks(false, false);
            label->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
            addAndMakeVisible(label);
            label->setText("", juce::dontSendNotification);
        }

        startTimerHz(30); // for transient flash highlight decay
    }

    // Returns indices of pads whose toggle state is on (selected by user for modifiers)
    juce::Array<int> getSelectedPadIndices() const
    {
        juce::Array<int> indices;
        for (int i = 0; i < padButtons.size(); ++i)
            if (padButtons[i]->getToggleState())
                indices.add(i);
        return indices;
    }

    void clearSelections()
    {
        for (auto* b : padButtons) b->setToggleState(false, juce::dontSendNotification);
        if (selectionChanged) selectionChanged();
    }

    // Set (or clear) the displayed filename for a pad. Pass empty string to clear.
    void setPadFileName(int padIndex, const juce::String& fileName)
    {
    if (!juce::isPositiveAndBelow(padIndex, numPads)) return;
        padFileNames.set(padIndex, fileName);
        updatePadLabel(padIndex);
    }

    juce::String getPadFileName(int padIndex) const
    {
    return juce::isPositiveAndBelow(padIndex, padFileNames.size()) ? padFileNames[padIndex] : juce::String();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(4);
        const int rows = 2;
        const int cols = 4;
        const int padW = area.getWidth() / cols;
        const int padH = area.getHeight() / rows;
        const int labelHeight = 20;

        for (int r = 0; r < rows; ++r)
        {
            for (int c = 0; c < cols; ++c)
            {
                int idx = r * cols + c;
                if (idx < padButtons.size())
                {
                    juce::Rectangle<int> padRect(area.getX() + c * padW, area.getY() + r * padH, padW - 4, padH - 4);
                    auto labelRect = padRect.removeFromBottom(labelHeight);
                    padFileLabels[idx]->setBounds(labelRect.reduced(2, 0));
                    padButtons[idx]->setBounds(padRect.reduced(4));
                }
            }
        }
    }

private:
    static constexpr int numPads = 8;
    juce::OwnedArray<juce::ToggleButton> padButtons;
    juce::OwnedArray<juce::Label> padFileLabels;
    juce::StringArray padFileNames { "", "", "", "", "", "", "", "" };
    std::array<int, numPads> flashCounters { {0,0,0,0,0,0,0,0} };
    std::array<bool, numPads> playingStates { {false,false,false,false,false,false,false,false} };

public:
    // Callback for external listeners when any pad selection toggles.
    std::function<void()> selectionChanged;

    void setSelectionChangedCallback(std::function<void()> cb) { selectionChanged = std::move(cb); }

    // Trigger a short flash on given pad indices (or all if list empty and global event)
    void flashPads(const juce::Array<int>& padIndices)
    {
        if (padIndices.isEmpty())
        {
            for (int i = 0; i < numPads; ++i) flashCounters[i] = flashDurationTicks;
        }
        else
        {
            for (auto idx : padIndices)
                if (juce::isPositiveAndBelow(idx, numPads))
                    flashCounters[(size_t)idx] = flashDurationTicks;
        }
        repaint();
    }

    // Update which pads are currently playing; expects indices from engine each refresh.
    void setPlayingStates(const juce::Array<int>& playingIndices)
    {
        std::array<bool, numPads> newStates { {false,false,false,false,false,false,false,false} };
        for (auto idx : playingIndices)
            if (juce::isPositiveAndBelow(idx, numPads))
                newStates[(size_t)idx] = true;

        if (newStates != playingStates)
        {
            playingStates = newStates;
            repaint();
        }
    }

private:
    static constexpr int flashDurationTicks = 10; // ~333ms at 30Hz

    void updatePadLabel(int padIndex)
    {
    if (!juce::isPositiveAndBelow(padIndex, padFileLabels.size())) return;
        auto name = padFileNames[padIndex];
        const bool isMissing = name.equalsIgnoreCase("[missing]");
        if (name.isEmpty())
        {
            padFileLabels[padIndex]->setText("", juce::dontSendNotification);
            return;
        }
        // Truncate long names for compact display
        const int maxChars = 10;
        if (name.length() > maxChars)
            name = name.substring(0, maxChars - 1) + "…";
        // Colour-code missing pads to draw attention
        padFileLabels[padIndex]->setColour(juce::Label::textColourId, isMissing ? juce::Colours::orangered : juce::Colours::lightgrey);
        padFileLabels[padIndex]->setText(name, juce::dontSendNotification);
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        // Draw playing state (green outline) then flash (yellow fill) so flash is prominent.
        for (int i = 0; i < numPads; ++i)
        {
            if (auto* btn = padButtons[i])
            {
                auto r = btn->getBounds().toFloat().expanded(2.f);
                if (playingStates[(size_t)i])
                {
                    g.setColour(juce::Colours::lime.withAlpha(0.9f));
                    g.drawRoundedRectangle(r, 6.f, 2.2f);
                }
            }
        }

        g.setColour(juce::Colours::yellow.withAlpha(0.35f));
        for (int i = 0; i < numPads; ++i)
        {
            if (flashCounters[(size_t)i] > 0)
            {
                if (auto* btn = padButtons[i])
                {
                    auto r = btn->getBounds().toFloat();
                    g.fillRoundedRectangle(r.expanded(2.f), 6.f);
                }
            }
        }
    }

    void timerCallback() override
    {
        bool any = false;
        for (auto& c : flashCounters)
        {
            if (c > 0) { --c; any = true; }
        }
        if (any)
            repaint();
    }
};
