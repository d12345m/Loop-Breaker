/*
 ==============================================================================
   DebugPanelContent.h
   --------------------------------------------------------------------------
   Aggregates debug/dev panels: Modifier History, FX Status, Tearing Debug,
   and the Modifier Selection (force-pick) panel.  Lives in its own tab so
   the Session tab stays clean.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ModifierHistoryPanel.h"
#include "ModifierSelectionPanel.h"
#include "FxStatusPanel.h"
#include "TearingDebugPanel.h"
#include "ThemeEngine.h"
#include "AppState.h"

class DebugPanelContent : public juce::Component
{
public:
    explicit DebugPanelContent(AppState& appState)
        : fxStatusPanel(appState),
          tearingDebugPanel(appState)
    {
        addAndMakeVisible(modifierHistory);

        modifierSelectionViewport.setViewedComponent(&modifierSelectionPanel, false);
        addAndMakeVisible(modifierSelectionViewport);

        addAndMakeVisible(fxStatusPanel);
        addAndMakeVisible(tearingDebugPanel);

        // Resizer bars
        addAndMakeVisible(horizontalDivider);
        addAndMakeVisible(verticalDivider1);
        addAndMakeVisible(verticalDivider2);

        // Horizontal: history (left) | divider | right stack
        horizontalLayout.setItemLayout(0, 200, -1.0, 400);
        horizontalLayout.setItemLayout(1, 8, 8, 8);
        horizontalLayout.setItemLayout(2, 200, -1.0, 400);

        // Vertical (right column): modifier selection | fx status | tearing debug
        verticalLayout.setItemLayout(0, 100, -1.0, 200);
        verticalLayout.setItemLayout(1, 8, 8, 8);
        verticalLayout.setItemLayout(2, 80, -1.0, 120);
        verticalLayout.setItemLayout(3, 8, 8, 8);
        verticalLayout.setItemLayout(4, 100, -1.0, -1.0);

        modifierSelectionPanel.setForceSelectionCallback([&appState](ModifierType type) {
            appState.scheduler.forceUpcomingModifier(type);
        });
        modifierSelectionPanel.setForceVariantCallback([&appState](ModifierType type, const juce::String& variant) {
            appState.scheduler.forceUpcomingVariant(type, variant);
        });
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Theme::bg());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);

        // Horizontal split: history | divider | right column
        juce::Component* hComps[] = { &modifierHistory, &horizontalDivider, nullptr };
        horizontalLayout.layOutComponents(hComps, 3,
                                          area.getX(), area.getY(),
                                          area.getWidth(), area.getHeight(),
                                          false, true);

        // Derive right-panel bounds from divider position
        auto rightPanel = juce::Rectangle<int>(
            horizontalDivider.getRight(), area.getY(),
            area.getRight() - horizontalDivider.getRight(), area.getHeight()).reduced(4);

        // Vertical split inside right panel
        juce::Component* vComps[] = { &modifierSelectionViewport, &verticalDivider1,
                                      &fxStatusPanel, &verticalDivider2, &tearingDebugPanel };
        verticalLayout.layOutComponents(vComps, 5,
                                        rightPanel.getX(), rightPanel.getY(),
                                        rightPanel.getWidth(), rightPanel.getHeight(),
                                        true, true);

        // Size the modifier selection content for scrolling
        int toggleCount = modifierSelectionPanel.getNumChildComponents();
        int desiredH = juce::jmax(modifierSelectionViewport.getHeight() - 20, toggleCount * 26 + 20);
        modifierSelectionPanel.setSize(modifierSelectionViewport.getWidth() - 8, desiredH);
    }

    /** Expose history panel so the Session tab can still log triggered modifiers. */
    ModifierHistoryPanel& getModifierHistory() { return modifierHistory; }

private:
    ModifierHistoryPanel modifierHistory;

    ModifierSelectionPanel modifierSelectionPanel;
    juce::Viewport modifierSelectionViewport;

    FxStatusPanel fxStatusPanel;
    TearingDebugPanel tearingDebugPanel;

    // Layout managers
    juce::StretchableLayoutManager horizontalLayout;
    juce::StretchableLayoutResizerBar horizontalDivider { &horizontalLayout, 1, false };
    juce::StretchableLayoutManager verticalLayout;
    juce::StretchableLayoutResizerBar verticalDivider1 { &verticalLayout, 1, true };
    juce::StretchableLayoutResizerBar verticalDivider2 { &verticalLayout, 3, true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DebugPanelContent)
};
