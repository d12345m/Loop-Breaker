/*
 ==============================================================================
   DebugPanelContent.h
   --------------------------------------------------------------------------
   Aggregates debug/dev panels: Modifier History, Tearing Debug,
   and the Modifier Selection (force-pick) panel.  Lives in its own tab so
   the Session tab stays clean.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ModifierHistoryPanel.h"
#include "ModifierSelectionPanel.h"
#include "TearingDebugPanel.h"
#include "NodeClipDebugPanel.h"
#include "ThemeEngine.h"
#include "AppState.h"

class DebugPanelContent : public juce::Component
{
public:
    explicit DebugPanelContent(AppState& appState)
        : tearingDebugPanel(appState),
          nodeClipDebugPanel(appState)
    {
        addAndMakeVisible(modifierHistory);

        modifierSelectionViewport.setViewedComponent(&modifierSelectionPanel, false);
        addAndMakeVisible(modifierSelectionViewport);

        addAndMakeVisible(tearingDebugPanel);
        addAndMakeVisible(nodeClipDebugPanel);

        // Resizer bars
        addAndMakeVisible(horizontalDivider);
        addAndMakeVisible(verticalDivider1);
        addAndMakeVisible(verticalDivider2);

        // Horizontal: history (left) | divider | right stack
        horizontalLayout.setItemLayout(0, 200, -1.0, 400);
        horizontalLayout.setItemLayout(1, 8, 8, 8);
        horizontalLayout.setItemLayout(2, 200, -1.0, 400);

        // Vertical (right column): modifier selection | tearing debug | node clip debug
        verticalLayout.setItemLayout(0, 100, -1.0, 180);
        verticalLayout.setItemLayout(1, 8, 8, 8);
        verticalLayout.setItemLayout(2, 200, -1.0, 260);  // tearing: 8 channels need ~200px min
        verticalLayout.setItemLayout(3, 8, 8, 8);
        verticalLayout.setItemLayout(4, 60, -1.0, 120);

        modifierSelectionPanel.setForceQueueSelectionCallback([&appState](int queueIndex, ModifierType type) {
            appState.scheduler.forcePlannedModifier (queueIndex, type);
        });
        modifierSelectionPanel.setForceVariantCallback([&appState](ModifierType type, const juce::String& variant) {
            appState.scheduler.forceUpcomingVariant(type, variant);
        });
    }

    void paint(juce::Graphics&) override
    {
        // No opaque fill — BackgroundAnimator paints the background
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
                                      &tearingDebugPanel, &verticalDivider2,
                                      &nodeClipDebugPanel };
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

    TearingDebugPanel tearingDebugPanel;
    NodeClipDebugPanel nodeClipDebugPanel;

    // Layout managers
    juce::StretchableLayoutManager horizontalLayout;
    juce::StretchableLayoutResizerBar horizontalDivider { &horizontalLayout, 1, false };
    juce::StretchableLayoutManager verticalLayout;
    juce::StretchableLayoutResizerBar verticalDivider1 { &verticalLayout, 1, true };
    juce::StretchableLayoutResizerBar verticalDivider2 { &verticalLayout, 3, true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DebugPanelContent)
};
