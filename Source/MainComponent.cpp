/*
  ==============================================================================

    This file was auto-generated!

  ==============================================================================
*/

#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent() : state (TransportState::Stopped)
{
    addAndMakeVisible (&openButton);
    openButton.setButtonText ("Open...");
    openButton.addListener (this);

    addAndMakeVisible (&playButton);
    playButton.setButtonText ("Play");
    playButton.addListener (this);
    playButton.setColour (juce::TextButton::buttonColourId, juce::Colours::green);
    playButton.setEnabled (false);

    addAndMakeVisible (&stopButton);
    stopButton.setButtonText ("Stop");
    stopButton.addListener (this);
    stopButton.setColour (juce::TextButton::buttonColourId, juce::Colours::red);
    stopButton.setEnabled (false);

    addAndMakeVisible (&speedSlider);
    speedSlider.setRange (-2.0, 2.0, 0.01);
    speedSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 100, 20);
    speedSlider.setPopupDisplayEnabled (true, false, this);
    speedSlider.setTextValueSuffix (" x Speed");
    speedSlider.setValue(1.0);
    speedSlider.addListener (this);


    setSize (600, 400);

    formatManager.registerBasicFormats();
    transportSource.addChangeListener (this);

    setAudioChannels (0, 2);
}

MainComponent::~MainComponent()
{
    shutdownAudio();
}

//==============================================================================
void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;
    transportSource.prepareToPlay (samplesPerBlockExpected, sampleRate);
    resampleSource.prepareToPlay (samplesPerBlockExpected, sampleRate);
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (readerSource.get() == nullptr || fileLengthSamples <= 0)
    {
        bufferToFill.clearActiveBufferRegion();
        return;
    }
    
    double speed = speedSlider.getValue();
    
    if (speed == 0.0)
    {
        bufferToFill.clearActiveBufferRegion();
        return;
    }
    
    auto numSamples = bufferToFill.numSamples;
    auto numChannels = bufferToFill.buffer->getNumChannels();
    
    bufferToFill.clearActiveBufferRegion();
    
    // Industry standard approach: Direct sample-accurate playback from memory buffer
    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Calculate the source sample position (with sub-sample precision)
        double sourcePos = filePlayPosition;
        
        // Handle looping at boundaries
        if (speed > 0.0) // Forward playback
        {
            if (sourcePos >= fileLengthSamples - 1)
            {
                if (isLooping)
                    sourcePos = filePlayPosition = 0.0;
                else
                    break;
            }
        }
        else // Reverse playback
        {
            if (sourcePos <= 0.0)
            {
                if (isLooping)
                    sourcePos = filePlayPosition = static_cast<double>(fileLengthSamples - 1);
                else
                    break;
            }
        }
        
        // Linear interpolation for high-quality resampling
        int pos1 = static_cast<int>(sourcePos);
        int pos2 = pos1 + 1;
        double fraction = sourcePos - pos1;
        
        // Ensure positions are within bounds
        pos1 = juce::jlimit<int>(0, static_cast<int>(fileLengthSamples - 1), pos1);
        pos2 = juce::jlimit<int>(0, static_cast<int>(fileLengthSamples - 1), pos2);
        
        for (int channel = 0; channel < juce::jmin(numChannels, audioFileBuffer.getNumChannels()); ++channel)
        {
            // Linear interpolation between adjacent samples
            float sample1 = audioFileBuffer.getSample(channel, pos1);
            float sample2 = audioFileBuffer.getSample(channel, pos2);
            float interpolatedSample = sample1 + fraction * (sample2 - sample1);
            
            bufferToFill.buffer->setSample(channel, bufferToFill.startSample + sample, interpolatedSample);
        }
        
        // Advance playback position
        filePlayPosition += speed;
    }
}

void MainComponent::releaseResources()
{
    transportSource.releaseResources();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    openButton.setBounds (10, 10, getWidth() - 20, 20);
    playButton.setBounds (10, 40, getWidth() - 20, 20);
    stopButton.setBounds (10, 70, getWidth() - 20, 20);
    speedSlider.setBounds (10, 100, getWidth() - 20, 100);
}

void MainComponent::changeState (TransportState newState)
{
    if (state != newState)
    {
        state = newState;

        switch (state)
        {
            case TransportState::Stopped:
                stopButton.setEnabled (false);
                playButton.setEnabled (true);
                transportSource.stop();
                break;

            case TransportState::Starting:
                playButton.setEnabled (false);
                transportSource.start();
                break;

            case TransportState::Playing:
                stopButton.setEnabled (true);
                break;

            case TransportState::Stopping:
                transportSource.stop();
                break;
        }
    }
}

void MainComponent::openButtonClicked()
{
    chooser = std::make_unique<juce::FileChooser> ("Select a Wave file to play...",
                                                   juce::File{},
                                                   "*.wav;*.mp3;*.aif");
    auto chooserFlags = juce::FileBrowserComponent::openMode
                      | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();

        if (file != juce::File{})
        {
            auto* reader = formatManager.createReaderFor (file);

            if (reader != nullptr)
            {
                // Store file properties
                fileSampleRate = reader->sampleRate;
                fileLengthSamples = reader->lengthInSamples;
                auto numChannels = static_cast<int>(reader->numChannels);
                
                // Load entire file into memory for reliable reverse playback
                audioFileBuffer.setSize(numChannels, static_cast<int>(fileLengthSamples));
                reader->read(&audioFileBuffer, 0, static_cast<int>(fileLengthSamples), 0, true, true);
                
                // Set up the traditional audio source as well for forward playback fallback
                auto newSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
                transportSource.setSource (newSource.get(), 0, nullptr, reader->sampleRate);
                readerSource.reset (newSource.release());
                
                // Ensure transport is stopped after loading
                transportSource.stop();
                
                // Reset playback position
                filePlayPosition = 0.0;
                
                // Ensure we're in stopped state
                changeState (TransportState::Stopped);
                
                playButton.setEnabled (true);
            }
        }
    });
}

void MainComponent::playButtonClicked()
{
    filePlayPosition = 0.0;
    transportSource.setPosition(0);
    isLooping = true;
    changeState (TransportState::Starting);
}

void MainComponent::stopButtonClicked()
{
    isLooping = false;
    if (state == TransportState::Playing)
        changeState (TransportState::Stopping);
    else
        changeState (TransportState::Stopped);
}


void MainComponent::buttonClicked (juce::Button* button)
{
    if (button == &openButton)  openButtonClicked();
    if (button == &playButton)  playButtonClicked();
    if (button == &stopButton)  stopButtonClicked();
}

void MainComponent::sliderValueChanged (juce::Slider* slider)
{
    if (slider == &speedSlider)
    {
        double speed = speedSlider.getValue();
        
        // Only affect playback if the user has explicitly started playback
        // Don't automatically start playback just from moving the slider
        if (state == TransportState::Playing || state == TransportState::Starting)
        {
            if (speed == 0.0)
            {
                transportSource.stop();
            }
            else
            {
                if (!transportSource.isPlaying())
                    transportSource.start();
            }
        }
        
        // Note: We handle all speed and direction changes in getNextAudioBlock
        // No need to manipulate transport source for reverse playback
    }
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &transportSource)
    {
        if (transportSource.isPlaying())
            changeState (TransportState::Playing);
        else if (isLooping && readerSource != nullptr)
        {
            transportSource.setPosition(0);
            transportSource.start();
        }
        else
            changeState (TransportState::Stopped);
    }
}
