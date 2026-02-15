/*
 ==============================================================================
   ModifierProbabilityPanel.h
   --------------------------------------------------------------------------
   Scrollable UI panel showing one row per modifier type with a horizontal
   slider controlling its selection probability weight (0 = never, 1 = max).
   Rows are grouped by category with section headers.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ModifierProbabilityManager.h"
#include "Theme.h"
#include <vector>

class ModifierProbabilityPanel : public juce::Component
{
public:
    explicit ModifierProbabilityPanel(ModifierProbabilityManager& manager)
        : probManager(manager)
    {
        buildRows();

        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);

        resetButton.setButtonText("Reset All to 100%");
        resetButton.onClick = [this]
        {
            probManager.resetToDefaults();
            refreshFromManager();
        };
        addAndMakeVisible(resetButton);
    }

    void resized() override
    {
        auto area = getLocalBounds();

        // Bottom button strip
        auto bottomBar = area.removeFromBottom(32).reduced(4, 2);
        resetButton.setBounds(bottomBar.removeFromRight(160));

        viewport.setBounds(area);
        layoutContent();
    }

    /** Refresh slider positions from manager (e.g. after preset load). */
    void refreshFromManager()
    {
        for (auto& row : rows)
        {
            row.slider->setValue(probManager.getWeight(row.type), juce::dontSendNotification);
            updateValueLabel(row);
        }
    }

private:
    struct Row
    {
        ModifierType type;
        bool showCategoryHeader = false;
        juce::String category;
        std::unique_ptr<juce::Label>  categoryLabel;
        std::unique_ptr<juce::Label>  nameLabel;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label>  valueLabel;
    };

    ModifierProbabilityManager& probManager;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<Row> rows;
    juce::TextButton resetButton;

    void buildRows()
    {
        const auto& allTypes = ModifierProbabilityManager::allModifierTypes();
        juce::String lastCategory;

        for (size_t i = 0; i < allTypes.size(); ++i)
        {
            auto type = allTypes[i];
            Row row;
            row.type = type;
            row.category = ModifierProbabilityManager::getCategory(type);
            row.showCategoryHeader = (row.category != lastCategory);
            lastCategory = row.category;

            // Category header label
            row.categoryLabel = std::make_unique<juce::Label>();
            row.categoryLabel->setText(row.category, juce::dontSendNotification);
            row.categoryLabel->setFont(juce::FontOptions(15.0f, juce::Font::bold));
            row.categoryLabel->setColour(juce::Label::textColourId, Theme::text());
            content.addAndMakeVisible(row.categoryLabel.get());

            // Modifier name
            row.nameLabel = std::make_unique<juce::Label>();
            row.nameLabel->setText(ModifierProbabilityManager::getDisplayName(type), juce::dontSendNotification);
            row.nameLabel->setFont(juce::FontOptions(14.0f));
            row.nameLabel->setColour(juce::Label::textColourId, Theme::textSubtle());
            content.addAndMakeVisible(row.nameLabel.get());

            // Slider
            row.slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
                                                         juce::Slider::NoTextBox);
            row.slider->setRange(0.0, 1.0, 0.01);
            row.slider->setValue(probManager.getWeight(type), juce::dontSendNotification);
            row.slider->setColour(juce::Slider::trackColourId, Theme::accent());
            row.slider->setColour(juce::Slider::backgroundColourId, Theme::panelAlt());
            auto idx = i; // capture for lambda
            row.slider->onValueChange = [this, idx]
            {
                auto& r = rows[idx];
                probManager.setWeight(r.type, static_cast<float>(r.slider->getValue()));
                updateValueLabel(r);
            };
            content.addAndMakeVisible(row.slider.get());

            // Numeric value label
            row.valueLabel = std::make_unique<juce::Label>();
            row.valueLabel->setFont(juce::FontOptions(13.0f));
            row.valueLabel->setColour(juce::Label::textColourId, Theme::textSubtle());
            row.valueLabel->setJustificationType(juce::Justification::centredLeft);
            content.addAndMakeVisible(row.valueLabel.get());

            updateValueLabel(row);
            rows.push_back(std::move(row));
        }
    }

    void layoutContent()
    {
        const int rowHeight = 32;
        const int headerHeight = 24;
        const int padding = 6;
        const int labelWidth = 150;
        const int valueLabelWidth = 48;

        auto contentWidth = viewport.getMaximumVisibleWidth();
        int y = padding;

        for (auto& row : rows)
        {
            if (row.showCategoryHeader)
            {
                row.categoryLabel->setBounds(padding, y, contentWidth - padding * 2, headerHeight);
                row.categoryLabel->setVisible(true);
                y += headerHeight + 2;
            }
            else
            {
                row.categoryLabel->setVisible(false);
            }

            int x = padding + 8;
            row.nameLabel->setBounds(x, y, labelWidth, rowHeight);
            x += labelWidth + 4;

            int sliderWidth = contentWidth - x - valueLabelWidth - padding - 4;
            row.slider->setBounds(x, y, sliderWidth, rowHeight);
            x += sliderWidth + 4;

            row.valueLabel->setBounds(x, y, valueLabelWidth, rowHeight);
            y += rowHeight;
        }

        content.setSize(contentWidth, y + padding);
    }

    void updateValueLabel(Row& row)
    {
        float w = probManager.getWeight(row.type);
        if (w <= 0.0f)
            row.valueLabel->setText("OFF", juce::dontSendNotification);
        else
            row.valueLabel->setText(juce::String(static_cast<int>(w * 100)) + "%",
                                    juce::dontSendNotification);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModifierProbabilityPanel)
};
