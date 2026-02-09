#pragma once

#include <JuceHeader.h>
#include "AppState.h"

class BufferTestAudioProcessor : public juce::AudioProcessor
{
public:
    BufferTestAudioProcessor();
    ~BufferTestAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // App accessors (used by editor)
    AppState& getAppState() { return app; }
    const AppState& getAppState() const { return app; }

    juce::AudioFormatManager& getFormatManager() { return formatManager; }

private:
    AppState app;
    juce::AudioFormatManager formatManager;

    juce::AudioBuffer<float> scratchBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BufferTestAudioProcessor)
};
