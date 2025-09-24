/*
  ==============================================================================

    Professional Audio Buffer Component with Repitching
    
    Features:
    - Variable speed playback (-2x to +2x with repitching - speed and pitch change together)
    - Seamless looping with crossfade
    - Reverse playback capability
    - Professional DSP practices
    - Ready for integration into larger projects

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

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
    
    void setPlaying (bool shouldPlay) { isPlaying.store(shouldPlay); }
    void setSpeed (double newSpeed) { targetSpeed.store(newSpeed); }
    void setLooping (bool shouldLoop) { isLooping.store(shouldLoop); }
    
    // Buffer slicing functionality
    void setNumSlices (int numSlices);
    void triggerSlice (int sliceIndex);
    void triggerRandomSlice();
    void resetToBeginning();
    void resetToDefaults();
    int getCurrentSlice() const;
    int getNumSlices() const { return numSlices; }
    bool isInContinuousRandomMode() const { return continuousRandomMode.load(); }
    void stopRandomSlicing();
    void exitSlicingMode();

    
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
    
    // Buffer slicing
    int numSlices = 1;
    std::atomic<int> targetSlice { 0 };
    std::atomic<int> currentActiveSlice { 0 };
    std::atomic<bool> sliceTriggered { false };
    std::atomic<bool> isSlicingMode { false };
    std::atomic<bool> continuousRandomMode { false };
    juce::Random random;
    
    // DSP parameters
    double fileSampleRate = 44100.0;
    double hostSampleRate = 44100.0;
    int fileLengthSamples = 0;
    
    // Repitching buffer and processing
    juce::AudioBuffer<float> repitchBuffer;
    juce::AudioBuffer<float> tempProcessingBuffer;
    
    // Smooth parameter changes
    juce::SmoothedValue<double> speedSmoother;
    
    void processWithRepitching (juce::AudioBuffer<float>& outputBuffer);
    void handleSlicePlayback (double& currentPos);
    double getSliceStartPosition (int sliceIndex) const;
    double getSliceEndPosition (int sliceIndex) const;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioBufferProcessor)
};

//==============================================================================
/**
    Main component that provides the user interface for the audio buffer processor.
    Clean separation allows easy integration into larger projects.
*/
class MainComponent : public juce::AudioAppComponent,
                      public juce::Timer
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

private:
    // Core processor
    AudioBufferProcessor audioProcessor;
    
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
