#pragma once

#include <JuceHeader.h>

class BufferTestAudioProcessor;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BufferTestAudioProcessorEditor)
};
