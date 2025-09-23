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
    speedSlider.setValue(0.0);
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
    if (readerSource.get() == nullptr)
    {
        bufferToFill.clearActiveBufferRegion();
        return;
    }
    
    // This is a workaround for reverse playback
    if (speedSlider.getValue() < 0)
    {
        auto* buffer = bufferToFill.buffer;
        auto numSamples = bufferToFill.numSamples;
        auto startSample = bufferToFill.startSample;
        
        // This is a simplified reverse logic and might not be perfect
        // For a robust solution, a custom AudioSource would be better
        auto currentPos = transportSource.getCurrentPosition();
        auto samplesToMove = numSamples * std::abs(speedSlider.getValue());
        
        if (currentSampleRate > 0)
            transportSource.setPosition(currentPos - samplesToMove / currentSampleRate);
    }

    resampleSource.getNextAudioBlock (bufferToFill);

    // Loop
    if (transportSource.hasStreamFinished())
        transportSource.setPosition (0);
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
                auto newSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
                transportSource.setSource (newSource.get(), 0, nullptr, reader->sampleRate);
                playButton.setEnabled (true);
                readerSource.reset (newSource.release());
            }
        }
    });
}

void MainComponent::playButtonClicked()
{
    changeState (TransportState::Starting);
}

void MainComponent::stopButtonClicked()
{
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
        if (speed == 0.0)
        {
            changeState(TransportState::Stopped);
        }
        else
        {
            changeState(TransportState::Starting);
        }
        resampleSource.setResamplingRatio (std::abs(speed));
    }
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &transportSource)
    {
        if (transportSource.isPlaying())
            changeState (TransportState::Playing);
        else
            changeState (TransportState::Stopped);
    }
}
