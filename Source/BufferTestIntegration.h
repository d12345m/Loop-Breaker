/*
  ==============================================================================

    BufferTestIntegration.h
    
    Example showing how to integrate the AudioBufferProcessor into a larger project.
    This demonstrates the clean API and professional architecture.

  ==============================================================================
*/

#pragma once

#include "MainComponent.h"

//==============================================================================
/**
    Example integration class showing how to use AudioBufferProcessor
    in a larger audio application or plugin.
*/
class AudioBufferIntegrationExample
{
public:
    AudioBufferIntegrationExample() = default;
    ~AudioBufferIntegrationExample() = default;
    
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
    {
        bufferProcessor.prepareToPlay(sampleRate, maximumExpectedSamplesPerBlock);
    }
    
    void processBlock (juce::AudioBuffer<float>& buffer)
    {
        // The buffer processor handles all the complex playback logic internally
        bufferProcessor.processBlock(buffer);
    }
    
    void releaseResources()
    {
        bufferProcessor.releaseResources();
    }
    
    // Simple control interface
    bool loadAudioFile (const juce::File& file, juce::AudioFormatManager& formatManager)
    {
        return bufferProcessor.loadAudioFile(file, formatManager);
    }
    
    void setPlaying (bool shouldPlay)
    {
        bufferProcessor.setPlaying(shouldPlay);
    }
    
    void setSpeed (double speedMultiplier) // -2.0 to +2.0
    {
        bufferProcessor.setSpeed(speedMultiplier);
    }
    
    void setLooping (bool shouldLoop)
    {
        bufferProcessor.setLooping(shouldLoop);
    }
    
    // State queries
    bool hasAudioLoaded() const { return bufferProcessor.hasAudioLoaded(); }
    bool isPlaying() const { return bufferProcessor.getIsPlaying(); }

private:
    AudioBufferProcessor bufferProcessor;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioBufferIntegrationExample)
};

//==============================================================================
/**
    Example showing how you might use this in an audio plugin context
*/
class ExampleAudioPlugin  : public juce::AudioProcessor
{
public:
    // AudioProcessor interface implementation...
    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        audioBufferSystem.prepareToPlay(sampleRate, samplesPerBlock);
    }
    
    void processBlock (juce::AudioBuffer<float>& buffer, 
                      juce::MidiBuffer& midiMessages) override
    {
        juce::ignoreUnused(midiMessages);
        
        // Your plugin can now include sophisticated buffer playback capabilities
        audioBufferSystem.processBlock(buffer);
    }
    
    void releaseResources() override
    {
        audioBufferSystem.releaseResources();
    }
    
    // Expose buffer controls to your plugin interface
    void setBufferSpeed(double speed) { audioBufferSystem.setSpeed(speed); }
    void startBufferPlayback() { audioBufferSystem.setPlaying(true); }
    void stopBufferPlayback() { audioBufferSystem.setPlaying(false); }
    
    // ... other AudioProcessor methods

private:
    AudioBufferIntegrationExample audioBufferSystem;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExampleAudioPlugin)
};
