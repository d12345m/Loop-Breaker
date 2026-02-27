/*
 ==============================================================================
   ModifierProbabilityPanel.h
   --------------------------------------------------------------------------
   Scrollable UI panel showing one row per modifier type with:
     - A probability slider bound to an AudioProcessorValueTreeState parameter
       (exposes DAW automation on the host side).
   Rows are grouped by category with section headers.
   A "Pad Target Probability" section at the bottom controls per-pad weighting.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ModifierProbabilityManager.h"
#include "ThemeEngine.h"
#include "ThemeFonts.h"
#include <vector>

class ModifierProbabilityPanel : public juce::Component,
                                 private juce::Timer
{
public:
    // -------------------------------------------------------------------------
    ModifierProbabilityPanel (ModifierProbabilityManager& manager,
                              juce::AudioProcessorValueTreeState& apvtsRef)
        : probManager (manager)
        , apvts (apvtsRef)
    {
        buildRows();
        buildPadRows();

        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        resetButton.setButtonText ("Reset All to 100%");
        resetButton.onClick = [this] { resetAllToDefault(); };
        addAndMakeVisible (resetButton);

        startTimerHz (10);
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
    };

    ModifierProbabilityManager&             probManager;
    juce::AudioProcessorValueTreeState&     apvts;
    juce::Viewport                          viewport;
    juce::Component                         content;
    std::vector<Row>                        rows;
    juce::TextButton                        resetButton;

    // Per-pad target probability rows
    struct PadRow
    {
        int padIndex = 0;
        std::unique_ptr<juce::Label>   nameLabel;
        std::unique_ptr<juce::Slider>  slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::Label>   valueLabel;
    };
    std::vector<PadRow> padRows;
    juce::Label padSectionLabel;

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
            row.categoryLabel->setFont (ThemeFonts::getInstance().headingFont (15.0f));
            row.categoryLabel->setColour (juce::Label::textColourId, Theme::text());
            content.addAndMakeVisible (row.categoryLabel.get());

            // -- Modifier name --
            row.nameLabel = std::make_unique<juce::Label>();
            row.nameLabel->setText (ModifierProbabilityManager::getDisplayName (type),
                                    juce::dontSendNotification);
            row.nameLabel->setFont (ThemeFonts::getInstance().bodyFont (14.0f));
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
            row.valueLabel->setFont (ThemeFonts::getInstance().monoFont (13.0f));
            row.valueLabel->setColour (juce::Label::textColourId, Theme::textSubtle());
            row.valueLabel->setJustificationType (juce::Justification::centredLeft);
            content.addAndMakeVisible (row.valueLabel.get());
            updateValueLabel (row);

            rows.push_back (std::move (row));
        }
    }

    void buildPadRows()
    {
        padSectionLabel.setText ("Pad Target Probability", juce::dontSendNotification);
        padSectionLabel.setFont (ThemeFonts::getInstance().headingFont (15.0f));
        padSectionLabel.setColour (juce::Label::textColourId, Theme::text());
        content.addAndMakeVisible (padSectionLabel);

        for (int i = 0; i < 8; ++i)
        {
            PadRow pr;
            pr.padIndex = i;

            pr.nameLabel = std::make_unique<juce::Label>();
            pr.nameLabel->setText ("Pad " + juce::String (i + 1), juce::dontSendNotification);
            pr.nameLabel->setFont (ThemeFonts::getInstance().bodyFont (14.0f));
            pr.nameLabel->setColour (juce::Label::textColourId, Theme::textSubtle());
            content.addAndMakeVisible (pr.nameLabel.get());

            pr.slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal,
                                                         juce::Slider::NoTextBox);
            pr.slider->setRange (0.0, 1.0, 0.01);
            pr.slider->setColour (juce::Slider::trackColourId,       Theme::accent());
            pr.slider->setColour (juce::Slider::backgroundColourId,  Theme::panelAlt());
            content.addAndMakeVisible (pr.slider.get());

            const juce::String paramId = "padProb_" + juce::String (i);
            pr.sliderAttachment =
                std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                    apvts, paramId, *pr.slider);

            pr.valueLabel = std::make_unique<juce::Label>();
            pr.valueLabel->setFont (ThemeFonts::getInstance().monoFont (13.0f));
            pr.valueLabel->setColour (juce::Label::textColourId, Theme::textSubtle());
            pr.valueLabel->setJustificationType (juce::Justification::centredLeft);
            content.addAndMakeVisible (pr.valueLabel.get());
            updatePadValueLabel (pr);

            padRows.push_back (std::move (pr));
        }
    }

    // -------------------------------------------------------------------------
    void timerCallback() override
    {
        // Refresh value labels from live APVTS atomics
        for (auto& row : rows)
            updateValueLabel (row);

        // Refresh pad probability value labels
        for (auto& pr : padRows)
            updatePadValueLabel (pr);
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
        for (int i = 0; i < 8; ++i)
        {
            const juce::String id = "padProb_" + juce::String (i);
            if (auto* param = apvts.getParameter (id))
                param->setValueNotifyingHost (1.0f);
        }
    }

    void updatePadValueLabel (PadRow& pr)
    {
        const juce::String id = "padProb_" + juce::String (pr.padIndex);
        float w = 1.0f;
        if (auto* rawVal = apvts.getRawParameterValue (id))
            w = rawVal->load();
        const int pct = juce::roundToInt (w * 100.0f);
        pr.valueLabel->setText (pct <= 0 ? "OFF"
                                         : juce::String (pct) + "%",
                                juce::dontSendNotification);
    }

    void layoutContent()
    {
        const int rowHeight       = 32;
        const int headerHeight    = 24;
        const int padding         = 6;
        const int labelWidth      = 150;
        const int valueLabelWidth = 48;

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

            const int sliderWidth = contentWidth - x - valueLabelWidth - padding - 8;
            row.slider->setBounds (x, y, juce::jmax (20, sliderWidth), rowHeight);
            x += juce::jmax (20, sliderWidth) + 4;

            row.valueLabel->setBounds (x, y, valueLabelWidth, rowHeight);
            y += rowHeight;
        }

        // Pad Target Probability section
        if (!padRows.empty())
        {
            y += 8;
            padSectionLabel.setBounds (padding, y, contentWidth - padding * 2, headerHeight);
            y += headerHeight + 2;

            for (auto& pr : padRows)
            {
                int x = padding + 8;
                pr.nameLabel->setBounds (x, y, labelWidth, rowHeight);
                x += labelWidth + 4;

                const int sliderWidth = contentWidth - x - valueLabelWidth - padding - 8;
                pr.slider->setBounds (x, y, juce::jmax (20, sliderWidth), rowHeight);
                x += juce::jmax (20, sliderWidth) + 4;

                pr.valueLabel->setBounds (x, y, valueLabelWidth, rowHeight);
                y += rowHeight;
            }
        }

        content.setSize (contentWidth, y + padding);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModifierProbabilityPanel)
};
