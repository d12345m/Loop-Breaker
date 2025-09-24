/*
  ==============================================================================

    MainComponent.h - Test UI for AudioBuffer Component
    
    This component provides a test interface for the refactored AudioBuffer
    and AudioBufferManager classes. It maintains the same UI as before to
    verify that the refactored buffer code still works correctly.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "AudioBuffer.h"
#include "AudioBufferManager.h"
//==============================================================================
/**
    Test UI component for the refactored AudioBuffer classes.
    Maintains the same interface as before to verify functionality.
*/
class MainComponent : public juce::AudioAppComponent,
                      public juce::Timer,
                      public AudioBufferListener
{
public:
    MainComponent();
    ~MainComponent() override;

    // AudioAppComponent overrides
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    // Component overrides
    void paint (juce::Graphics& g) override;
    void resized() override;
    
    // Timer override
    void timerCallback() override;
    
    // AudioBufferListener overrides
    void audioBufferPlaybackStarted(int bufferIndex) override;
    void audioBufferPlaybackStopped(int bufferIndex) override;
    void audioBufferSliceChanged(int bufferIndex, int newSliceIndex) override;

private:
    // Core components - using the new refactored classes
    AudioBufferManager bufferManager;
    AudioBuffer* testBuffer; // Pointer to buffer 0 for testing
    
    // Audio format management
    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::FileChooser> fileChooser;
    
    // UI Components
    juce::TextButton loadButton;
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::Slider speedDial;
    juce::Label speedLabel;
    juce::Label instructionLabel;
    
    // Slicing UI Components
    juce::Slider sliceCountSlider;
    juce::Label sliceCountLabel;
    juce::TextButton randomSliceButton;
    juce::TextButton resetButton;
    juce::Label currentSliceLabel;
    
    // UI event handlers
    void loadButtonClicked();
    void playButtonClicked();
    void stopButtonClicked();
    void speedDialValueChanged();
    void sliceCountChanged();
    void randomSliceButtonClicked();
    void resetButtonClicked();
    
    void updateUI();
    void createSpeedDial();
    void updateSliceDisplay();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
