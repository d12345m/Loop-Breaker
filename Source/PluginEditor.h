#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ThemeLookAndFeel.h"

class ModifierProbabilityPanel;
#if JUCE_DEBUG
class DebugPanelContent;
class GlyphLabComponent;
#endif
class HelpPanelContent;
class SettingsPanelContent;
class BackgroundAnimator;

class BufferTestAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit BufferTestAudioProcessorEditor (BufferTestAudioProcessor&);
    ~BufferTestAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentSizeChanged() override;

private:
    void applyWindowLayout (WindowLayoutMode mode, bool restorePreviousSize);
    bool synchronisePortraitBoundsWithHost();
    void scheduleHostBoundsReconciliation();
   #if JUCE_IOS
    void showIOSPageMenu();
    void setIOSPage (int pageIndex);
    juce::Component* getIOSPageComponent (int pageIndex) const;
   #endif

    BufferTestAudioProcessor& audioProcessor;

    std::unique_ptr<BackgroundAnimator> backgroundAnimator;
    std::unique_ptr<juce::Component> content;
    std::unique_ptr<ModifierProbabilityPanel> probabilityPanel;
    std::unique_ptr<SettingsPanelContent> settingsPanel;
   #if JUCE_DEBUG
    std::unique_ptr<DebugPanelContent> debugPanel;
    std::unique_ptr<GlyphLabComponent> glyphLabPanel;
   #endif
    std::unique_ptr<HelpPanelContent> helpPanel;
   #if JUCE_IOS
    juce::TextButton pageMenuButton;
    int currentIOSPage = 0;
   #else
    std::unique_ptr<juce::TabbedComponent> tabComponent;
   #endif

    ThemeLookAndFeel editorLnf;
    juce::Rectangle<int> previousResizableBounds { 0, 0, 1200, 800 };
    WindowLayoutMode activeWindowLayoutMode = WindowLayoutMode::Resizable;
    bool synchronisingHostBounds = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BufferTestAudioProcessorEditor)
};
