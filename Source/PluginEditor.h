#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ModifierProbabilityPanel;
class DebugPanelContent;

class BufferTestAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit BufferTestAudioProcessorEditor (BufferTestAudioProcessor&);
    ~BufferTestAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    BufferTestAudioProcessor& processor;

    std::unique_ptr<juce::Component> content;
    std::unique_ptr<ModifierProbabilityPanel> probabilityPanel;
    std::unique_ptr<DebugPanelContent> debugPanel;
    std::unique_ptr<juce::TabbedComponent> tabComponent;

    MOONBASE_DECLARE_AND_INIT_ACTIVATION_UI (processor);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BufferTestAudioProcessorEditor)
};
