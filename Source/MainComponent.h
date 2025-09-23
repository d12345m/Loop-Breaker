/*
  ==============================================================================

    Professional Audio Buffer Component with Time-Stretching
    
    Features:
    - Variable speed playback (-2x to +2x with time-stretching)
    - Seamless looping with crossfade
    - Reverse playback capability
    - Professional DSP practices
    - Ready for integration into larger projects

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "EmbeddedAudio.h"

//==============================================================================
/**
    A professional audio buffer processor that handles variable speed playback
    with proper time-stretching, reverse playback, and seamless looping.
*/
class AudioBufferProcessor
{
public:
    AudioBufferProcessor();
    ~AudioBufferProcessor() = default;
    
    void prepareToPlay (double sampleRate, int samplesPerBlockExpected);
    void processBlock (juce::AudioBuffer<float>& buffer);
    void releaseResources();
    
    bool loadAudioFile (const juce::File& file, juce::AudioFormatManager& formatManager);
    bool loadAudioBuffer (const juce::AudioBuffer<float>& buffer, double sampleRate);
    
    void setPlaying (bool shouldPlay) { isPlaying.store(shouldPlay); }
    void setSpeed (double newSpeed) { targetSpeed.store(newSpeed); }
    void setLooping (bool shouldLoop) { isLooping.store(shouldLoop); }
    
    bool hasAudioLoaded() const { return audioFileBuffer.getNumSamples() > 0; }
    bool getIsPlaying() const { return isPlaying.load(); }
    
private:
    // Core audio data
    juce::AudioBuffer<float> audioFileBuffer;
    std::atomic<double> playheadPosition { 0.0 };
    std::atomic<double> currentSpeed { 1.0 };
    std::atomic<double> targetSpeed { 1.0 };
    
    // State management
    std::atomic<bool> isPlaying { false };
    std::atomic<bool> isLooping { true };
    
    // DSP parameters
    double fileSampleRate = 44100.0;
    double hostSampleRate = 44100.0;
    int fileLengthSamples = 0;
    
    // Time-stretching buffer and processing
    juce::AudioBuffer<float> stretchBuffer;
    juce::AudioBuffer<float> tempProcessingBuffer;
    
    // Smooth parameter changes
    juce::SmoothedValue<double> speedSmoother;
    
    // Crossfade for seamless looping
    static constexpr int crossfadeLength = 1024;
    juce::AudioBuffer<float> crossfadeBuffer;
    
    void processWithTimeStretching (juce::AudioBuffer<float>& outputBuffer);
    void applyCrossfade (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioBufferProcessor)
};

//==============================================================================
/**
    Main component that provides the user interface for the audio buffer processor.
    Clean separation allows easy integration into larger projects.
*/
class MainComponent : public juce::AudioAppComponent
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

private:
    // Core processor
    AudioBufferProcessor audioProcessor;
    
    // Audio format management
    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::FileChooser> fileChooser;
    
    // UI Components
    juce::TextButton loadButton;
    juce::TextButton loadTestAudioButton;
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::Slider speedDial;
    juce::Label speedLabel;
    juce::Label instructionLabel;
    
    // UI event handlers
    void loadButtonClicked();
    void loadTestAudioClicked();
    void playButtonClicked();
    void stopButtonClicked();
    void speedDialValueChanged();
    
    void updateUI();
    void createSpeedDial();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
