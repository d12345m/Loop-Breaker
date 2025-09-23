/*
  ==============================================================================

    EmbeddedAudio.h
    
    Utilities for creating embedded audio data in the project.
    This allows you to include a test audio file directly in the binary.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    Utility class for generating embedded test audio data.
    This creates a simple musical pattern for testing the buffer implementation.
*/
class EmbeddedAudio
{
public:
    /**
        Creates a test audio buffer containing a musical pattern.
        This is useful for testing without requiring external audio files.
        
        @param sampleRate    The sample rate for the generated audio
        @param lengthSeconds The length of the audio in seconds
        @returns             AudioBuffer containing the generated audio
    */
    static juce::AudioBuffer<float> createTestAudio(double sampleRate = 44100.0, 
                                                   double lengthSeconds = 10.0)
    {
        const int numSamples = static_cast<int>(sampleRate * lengthSeconds);
        const int numChannels = 2;
        
        juce::AudioBuffer<float> buffer(numChannels, numSamples);
        
        // Generate a musical pattern with different frequencies for each channel
        const double baseFreq = 220.0; // A3
        const double harmonicFreq = 330.0; // E4
        
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const double time = sample / sampleRate;
            
            // Create a melody with envelope
            const double envelope = std::sin(juce::MathConstants<double>::twoPi * time / lengthSeconds);
            const double modulation = 1.0 + 0.3 * std::sin(juce::MathConstants<double>::twoPi * time * 0.5);
            
            // Left channel: Base frequency with vibrato
            const double leftSample = envelope * 0.3 * std::sin(juce::MathConstants<double>::twoPi * baseFreq * time * modulation);
            
            // Right channel: Harmonic with different modulation
            const double rightSample = envelope * 0.2 * std::sin(juce::MathConstants<double>::twoPi * harmonicFreq * time * (modulation * 1.1));
            
            buffer.setSample(0, sample, static_cast<float>(leftSample));
            buffer.setSample(1, sample, static_cast<float>(rightSample));
        }
        
        return buffer;
    }
    
    /**
        Creates a more complex test audio with rhythm patterns.
        Good for testing time-stretching and reverse playback capabilities.
    */
    static juce::AudioBuffer<float> createRhythmicTestAudio(double sampleRate = 44100.0,
                                                           double lengthSeconds = 8.0)
    {
        const int numSamples = static_cast<int>(sampleRate * lengthSeconds);
        const int numChannels = 2;
        
        juce::AudioBuffer<float> buffer(numChannels, numSamples);
        
        const double beatLength = lengthSeconds / 16.0; // 16 beats
        
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const double time = sample / sampleRate;
            const double beatTime = std::fmod(time, beatLength);
            const int beatNumber = static_cast<int>(time / beatLength);
            
            double leftSample = 0.0;
            double rightSample = 0.0;
            
            // Create different patterns for different beats
            if (beatNumber % 4 == 0) // Kick on beats 0, 4, 8, 12
            {
                const double kickEnv = std::exp(-beatTime * 20.0);
                leftSample = kickEnv * 0.4 * std::sin(juce::MathConstants<double>::twoPi * 60.0 * time);
                rightSample = leftSample;
            }
            else if (beatNumber % 2 == 1) // Snare on beats 1, 3, 5, 7, etc.
            {
                const double snareEnv = std::exp(-beatTime * 30.0);
                const double noise = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f;
                leftSample = snareEnv * 0.3 * noise;
                rightSample = leftSample;
            }
            
            // Add hi-hat pattern
            if (beatNumber % 1 == 0)
            {
                const double hatEnv = std::exp(-beatTime * 50.0);
                const double hatNoise = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f;
                leftSample += hatEnv * 0.1 * hatNoise;
                rightSample += hatEnv * 0.1 * hatNoise;
            }
            
            buffer.setSample(0, sample, static_cast<float>(leftSample));
            buffer.setSample(1, sample, static_cast<float>(rightSample));
        }
        
        return buffer;
    }
};
