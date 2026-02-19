/*
 ==============================================================================
   ModifierProbabilityPanel.h
   --------------------------------------------------------------------------
   Scrollable UI panel showing one row per modifier type with:
     - A probability slider bound to an AudioProcessorValueTreeState parameter
       (exposes DAW automation on the host side).
     - A CC button for MIDI learn: click to enter learn mode, the next incoming
       CC is assigned to that slider; click again to cancel; right-click to clear.
   Rows are grouped by category with section headers.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ModifierProbabilityManager.h"
#include "Theme.h"
#include <vector>

class ModifierProbabilityPanel : public juce::Component,
                                 private juce::Timer
{
public:
    // -------------------------------------------------------------------------
    // Callbacks wired up by the plugin editor to route CC learn through the
    // processor (which lives on the audio thread side).
    // -------------------------------------------------------------------------
    struct MidiCCControl
    {
        std::function<void(int)>  startLearn;           // paramIndex -> start learn
        std::function<void()>     cancelLearn;          // cancel any active learn
        std::function<int()>      pollLearnedCC;        // returns CC# or -1; clears pending
        std::function<bool()>     isLearning;           // true while learn is active
        std::function<int()>      getLearningParamIndex;// which paramIndex is learning
        std::function<int(int)>   getAssignedCC;        // paramIndex -> CC# or -1
        std::function<void(int)>  clearAssignment;      // remove CC mapping for paramIndex
    };

    // -------------------------------------------------------------------------
    ModifierProbabilityPanel (ModifierProbabilityManager& manager,
                              juce::AudioProcessorValueTreeState& apvtsRef,
                              MidiCCControl ccControl)
        : probManager (manager)
        , apvts (apvtsRef)
        , midiCC (std::move (ccControl))
    {
        buildRows();

        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        resetButton.setButtonText ("Reset All to 100%");
        resetButton.onClick = [this] { resetAllToDefault(); };
        addAndMakeVisible (resetButton);

        startTimerHz (10); // polls for CC learn completion at 10 Hz
    }

    ~ModifierProbabilityPanel() override { stopTimer(); }

    void resized() override
    {
        auto area = getLocalBounds();
        auto bottomBar = area.removeFromBottom (32).reduced (4, 2);
        resetButton.setBounds (bottomBar.removeFromRight (160));
        viewport.setBounds (area);
        layoutContent();
    }

    /** Push current weights back into APVTS (e.g. after an external preset load). */
    void refreshFromManager()
    {
        const auto& types = ModifierProbabilityManager::allModifierTypes();
        for (auto type : types)
        {
            const juce::String id = "prob_" + juce::String (static_cast<int> (type));
            if (auto* param = apvts.getParameter (id))
                param->setValueNotifyingHost (probManager.getWeight (type));
        }
        refreshCCButtons();
    }

private:
    // -------------------------------------------------------------------------
    struct Row
    {
        ModifierType type;
        int  paramIndex = 0;
        bool showCategoryHeader = false;
        juce::String category;

        std::unique_ptr<juce::Label>   categoryLabel;
        std::unique_ptr<juce::Label>   nameLabel;
        std::unique_ptr<juce::Slider>  slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::Label>   valueLabel;
        std::unique_ptr<juce::TextButton> ccButton; // "CC:--" / "CC:42" / "LEARN..."
    };

    ModifierProbabilityManager&             probManager;
    juce::AudioProcessorValueTreeState&     apvts;
    MidiCCControl                           midiCC;
    juce::Viewport                          viewport;
    juce::Component                         content;
    std::vector<Row>                        rows;
    juce::TextButton                        resetButton;

    // -------------------------------------------------------------------------
    void buildRows()
    {
        const auto& allTypes  = ModifierProbabilityManager::allModifierTypes();
        juce::String lastCategory;

        for (size_t i = 0; i < allTypes.size(); ++i)
        {
            const auto type = allTypes[i];
            Row row;
            row.type       = type;
            row.paramIndex = static_cast<int> (i);
            row.category   = ModifierProbabilityManager::getCategory (type);
            row.showCategoryHeader = (row.category != lastCategory);
            lastCategory = row.category;

            // -- Category header --
            row.categoryLabel = std::make_unique<juce::Label>();
            row.categoryLabel->setText (row.category, juce::dontSendNotification);
            row.categoryLabel->setFont (juce::FontOptions (15.0f, juce::Font::bold));
            row.categoryLabel->setColour (juce::Label::textColourId, Theme::text());
            content.addAndMakeVisible (row.categoryLabel.get());

            // -- Modifier name --
            row.nameLabel = std::make_unique<juce::Label>();
            row.nameLabel->setText (ModifierProbabilityManager::getDisplayName (type),
                                    juce::dontSendNotification);
            row.nameLabel->setFont (juce::FontOptions (14.0f));
            row.nameLabel->setColour (juce::Label::textColourId, Theme::textSubtle());
            content.addAndMakeVisible (row.nameLabel.get());

            // -- Probability slider --
            row.slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal,
                                                          juce::Slider::NoTextBox);
            row.slider->setRange (0.0, 1.0, 0.01);
            row.slider->setColour (juce::Slider::trackColourId,       Theme::accent());
            row.slider->setColour (juce::Slider::backgroundColourId,  Theme::panelAlt());
            content.addAndMakeVisible (row.slider.get());

            // SliderAttachment binds to APVTS param: handles DAW automation &
            // thread-safe updates automatically.
            const juce::String paramId = "prob_" + juce::String (static_cast<int> (type));
            row.sliderAttachment =
                std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                    apvts, paramId, *row.slider);

            // -- Numeric value label (updated by timer) --
            row.valueLabel = std::make_unique<juce::Label>();
            row.valueLabel->setFont (juce::FontOptions (13.0f));
            row.valueLabel->setColour (juce::Label::textColourId, Theme::textSubtle());
            row.valueLabel->setJustificationType (juce::Justification::centredLeft);
            content.addAndMakeVisible (row.valueLabel.get());
            updateValueLabel (row);

            // -- MIDI CC learn button --
            row.ccButton = std::make_unique<juce::TextButton>();
            row.ccButton->setTooltip (
                "Click to map a MIDI CC to this slider (move a CC after clicking).\n"
                "Click again to cancel learn.\n"
                "Right-click to clear assignment.");
            const int idx = static_cast<int> (i);
            row.ccButton->onClick = [this, idx]
            {
                if (! midiCC.startLearn) return;
                const bool learning    = midiCC.isLearning    ? midiCC.isLearning()            : false;
                const int  learnIdx    = midiCC.getLearningParamIndex
                                         ? midiCC.getLearningParamIndex() : -1;
                // Clicking the active learn button cancels; clicking another starts a new one.
                if (learning && learnIdx == idx)
                {
                    if (midiCC.cancelLearn) midiCC.cancelLearn();
                }
                else
                {
                    if (learning && midiCC.cancelLearn) midiCC.cancelLearn();
                    midiCC.startLearn (idx);
                }
                refreshCCButtons();
            };
            // Right-click clears assignment
            row.ccButton->onStateChange = [this, idx] { };  // placeholder; handled below
            content.addAndMakeVisible (row.ccButton.get());
            updateCCButton (row);

            rows.push_back (std::move (row));
        }

        // Wire right-click clear after rows vector is stable
        for (auto& r : rows)
        {
            r.ccButton->addMouseListener (this, false);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
        {
            for (auto& row : rows)
            {
                if (e.eventComponent == row.ccButton.get())
                {
                    if (midiCC.clearAssignment) midiCC.clearAssignment (row.paramIndex);
                    updateCCButton (row);
                    return;
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    void timerCallback() override
    {
        // Refresh value labels from live APVTS atomics
        for (auto& row : rows)
            updateValueLabel (row);

        // Check if a CC learn just completed
        if (midiCC.pollLearnedCC)
        {
            if (midiCC.pollLearnedCC() >= 0)
                refreshCCButtons();
        }

        // Update the learn-in-progress button label
        if (midiCC.isLearning && midiCC.isLearning())
        {
            const int learnIdx = midiCC.getLearningParamIndex
                                 ? midiCC.getLearningParamIndex() : -1;
            for (auto& row : rows)
                if (row.paramIndex == learnIdx)
                    row.ccButton->setButtonText ("LEARN...");
        }
    }

    // -------------------------------------------------------------------------
    void refreshCCButtons()
    {
        for (auto& row : rows)
            updateCCButton (row);
    }

    void updateCCButton (Row& row)
    {
        if (! midiCC.getAssignedCC)
        {
            row.ccButton->setButtonText ("CC:--");
            return;
        }

        const bool learning = midiCC.isLearning && midiCC.isLearning()
                              && midiCC.getLearningParamIndex
                              && midiCC.getLearningParamIndex() == row.paramIndex;
        if (learning)
        {
            row.ccButton->setButtonText ("LEARN...");
            return;
        }

        const int cc = midiCC.getAssignedCC (row.paramIndex);
        row.ccButton->setButtonText (cc >= 0 ? "CC:" + juce::String (cc) : "CC:--");
    }

    void updateValueLabel (Row& row)
    {
        const juce::String id = "prob_" + juce::String (static_cast<int> (row.type));
        float w = 1.0f;
        if (auto* rawVal = apvts.getRawParameterValue (id))
            w = rawVal->load();
        const int pct = juce::roundToInt (w * 100.0f);
        row.valueLabel->setText (pct <= 0 ? "OFF"
                                          : juce::String (pct) + "%",
                                 juce::dontSendNotification);
    }

    void resetAllToDefault()
    {
        const auto& types = ModifierProbabilityManager::allModifierTypes();
        for (auto type : types)
        {
            const juce::String id = "prob_" + juce::String (static_cast<int> (type));
            if (auto* param = apvts.getParameter (id))
                param->setValueNotifyingHost (1.0f);
        }
    }

    void layoutContent()
    {
        const int rowHeight       = 32;
        const int headerHeight    = 24;
        const int padding         = 6;
        const int labelWidth      = 150;
        const int valueLabelWidth = 48;
        const int ccButtonWidth   = 78;

        const int contentWidth = viewport.getMaximumVisibleWidth();
        int y = padding;

        for (auto& row : rows)
        {
            if (row.showCategoryHeader)
            {
                row.categoryLabel->setBounds (padding, y,
                                              contentWidth - padding * 2, headerHeight);
                row.categoryLabel->setVisible (true);
                y += headerHeight + 2;
            }
            else
            {
                row.categoryLabel->setVisible (false);
            }

            int x = padding + 8;
            row.nameLabel->setBounds (x, y, labelWidth, rowHeight);
            x += labelWidth + 4;

            const int sliderWidth = contentWidth - x - valueLabelWidth - ccButtonWidth - padding - 8;
            row.slider->setBounds (x, y, juce::jmax (20, sliderWidth), rowHeight);
            x += juce::jmax (20, sliderWidth) + 4;

            row.valueLabel->setBounds (x, y, valueLabelWidth, rowHeight);
            x += valueLabelWidth + 4;

            row.ccButton->setBounds (x, y, ccButtonWidth, rowHeight);
            y += rowHeight;
        }

        content.setSize (contentWidth, y + padding);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModifierProbabilityPanel)
};
