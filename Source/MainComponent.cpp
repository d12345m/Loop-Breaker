/*
  ==============================================================================

    MainComponent.cpp - Test UI for Refactored Audio Buffer Components
    
    This implementation provides a test interface for the new AudioBuffer
    and AudioBufferManager classes, maintaining the same UI for verification.

  ==============================================================================
*/

#include "MainComponent.h"

//==============================================================================
// MainComponent Implementation
//==============================================================================

MainComponent::MainComponent()
    : testBuffer(nullptr)
{
    // Get reference to the first buffer for testing
    testBuffer = bufferManager.getBuffer(0);
    
    // Add ourselves as a listener to receive notifications
    bufferManager.addListener(this);
    
    // Configure format manager
    formatManager.registerBasicFormats();
    
    // Create and configure UI components
    addAndMakeVisible(loadButton);
    loadButton.setButtonText("Load Audio File");
    loadButton.onClick = [this] { loadButtonClicked(); };
    
    addAndMakeVisible(playButton);
    playButton.setButtonText("Play");
    playButton.setColour(juce::TextButton::buttonColourId, juce::Colours::green);
    playButton.onClick = [this] { playButtonClicked(); };
    playButton.setEnabled(false);
    
    addAndMakeVisible(stopButton);
    stopButton.setButtonText("Stop");
    stopButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red);
    stopButton.onClick = [this] { stopButtonClicked(); };
    stopButton.setEnabled(false);
    
    // Create speed dial
    createSpeedDial();
    
    // Create labels
    addAndMakeVisible(speedLabel);
    speedLabel.setText("Speed & Direction", juce::dontSendNotification);
    speedLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    speedLabel.setJustificationType(juce::Justification::centred);
    
    addAndMakeVisible(instructionLabel);
    instructionLabel.setText("Left: Reverse 2x | Center: Stop | Right: Forward 2x", juce::dontSendNotification);
    instructionLabel.setFont(juce::Font(12.0f));
    instructionLabel.setJustificationType(juce::Justification::centred);
    instructionLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    
    // Create slicing controls
    addAndMakeVisible(sliceCountLabel);
    sliceCountLabel.setText("Buffer Slices", juce::dontSendNotification);
    sliceCountLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    sliceCountLabel.setJustificationType(juce::Justification::centred);
    
    addAndMakeVisible(sliceCountSlider);
    sliceCountSlider.setRange(1, 64, 1);
    sliceCountSlider.setValue(32);
    sliceCountSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    sliceCountSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    sliceCountSlider.onValueChange = [this] { sliceCountChanged(); };
    
    addAndMakeVisible(randomSliceButton);
    randomSliceButton.setButtonText("Start Random Slicing");
    randomSliceButton.setColour(juce::TextButton::buttonColourId, juce::Colours::orange);
    randomSliceButton.onClick = [this] { randomSliceButtonClicked(); };
    randomSliceButton.setEnabled(false);
    
    addAndMakeVisible(resetButton);
    resetButton.setButtonText("Reset to Defaults");
    resetButton.setColour(juce::TextButton::buttonColourId, juce::Colours::purple);
    resetButton.onClick = [this] { resetButtonClicked(); };
    resetButton.setEnabled(false);
    
    addAndMakeVisible(currentSliceLabel);
    currentSliceLabel.setText("Current Slice: 0", juce::dontSendNotification);
    currentSliceLabel.setFont(juce::Font(12.0f));
    currentSliceLabel.setJustificationType(juce::Justification::centred);
    currentSliceLabel.setColour(juce::Label::textColourId, juce::Colours::lightblue);
    
    // Set component size (larger to accommodate new controls)
    setSize(600, 650);
    
    // Initialize audio
    setAudioChannels(0, 2);
    
    // Set initial speed to match the dial
    if (testBuffer)
        testBuffer->setSpeed(1.0);
    
    // Start timer for UI updates
    startTimer(100); // Update every 100ms
}

MainComponent::~MainComponent()
{
    stopTimer();
    bufferManager.removeListener(this);
    shutdownAudio();
}

void MainComponent::createSpeedDial()
{
    addAndMakeVisible(speedDial);
    speedDial.setSliderStyle(juce::Slider::Rotary);
    speedDial.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, 25);
    speedDial.setRange(-2.0, 2.0, 0.01);
    speedDial.setValue(1.0); // Start at normal speed
    speedDial.setTextValueSuffix("x");
    speedDial.setPopupDisplayEnabled(true, true, this);
    speedDial.onValueChange = [this] { speedDialValueChanged(); };
    
    // Customize the dial appearance
    speedDial.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::blue);
    speedDial.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colours::darkgrey);
    speedDial.setColour(juce::Slider::thumbColourId, juce::Colours::white);
}

//==============================================================================
// Audio processing
//==============================================================================

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    bufferManager.prepare(sampleRate, samplesPerBlockExpected);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    bufferManager.processBlock(*bufferToFill.buffer);
}

void MainComponent::releaseResources()
{
    bufferManager.releaseResources();
}

//==============================================================================
// AudioBufferListener Implementation
//==============================================================================

void MainComponent::audioBufferPlaybackStarted(int bufferIndex)
{
    if (bufferIndex == 0) // Only respond to our test buffer
    {
        playButton.setEnabled(false);
        stopButton.setEnabled(true);
    }
}

void MainComponent::audioBufferPlaybackStopped(int bufferIndex)
{
    if (bufferIndex == 0) // Only respond to our test buffer
    {
        playButton.setEnabled(true);
        stopButton.setEnabled(false);
        
        // If we were in random slicing mode, reset the button
        if (testBuffer && testBuffer->isInContinuousRandomMode())
        {
            testBuffer->exitSlicingMode();
            randomSliceButton.setButtonText("Start Random Slicing");
            randomSliceButton.setColour(juce::TextButton::buttonColourId, juce::Colours::orange);
        }
    }
}

void MainComponent::audioBufferSliceChanged(int bufferIndex, int newSliceIndex)
{
    // Update slice display when slice changes
    if (bufferIndex == 0)
        updateSliceDisplay();
}

//==============================================================================
// Timer callback
//==============================================================================

void MainComponent::timerCallback()
{
    updateSliceDisplay();
}

//==============================================================================
// UI Implementation
//==============================================================================

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    
    // Draw a border around the main area
    g.setColour(juce::Colours::grey);
    g.drawRoundedRectangle(getLocalBounds().reduced(10).toFloat(), 5.0f, 2.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(20);
    
    // Top section: Load button
    loadButton.setBounds(area.removeFromTop(40).reduced(0, 5));
    area.removeFromTop(10);
    
    // Transport controls
    auto transportArea = area.removeFromTop(40);
    auto buttonWidth = transportArea.getWidth() / 2;
    playButton.setBounds(transportArea.removeFromLeft(buttonWidth).reduced(5, 5));
    stopButton.setBounds(transportArea.reduced(5, 5));
    
    area.removeFromTop(20);
    
    // Speed control section
    speedLabel.setBounds(area.removeFromTop(30));
    area.removeFromTop(10);
    
    // Speed dial - large and centered
    auto dialArea = area.removeFromTop(180);
    speedDial.setBounds(dialArea.withSizeKeepingCentre(160, 160));
    
    area.removeFromTop(10);
    instructionLabel.setBounds(area.removeFromTop(30));
    
    area.removeFromTop(20);
    
    // Slicing controls section
    sliceCountLabel.setBounds(area.removeFromTop(25));
    area.removeFromTop(5);
    sliceCountSlider.setBounds(area.removeFromTop(30).reduced(20, 0));
    
    area.removeFromTop(15);
    
    // Slice control buttons
    auto sliceButtonArea = area.removeFromTop(40);
    auto sliceButtonWidth = sliceButtonArea.getWidth() / 2;
    randomSliceButton.setBounds(sliceButtonArea.removeFromLeft(sliceButtonWidth).reduced(5, 5));
    resetButton.setBounds(sliceButtonArea.reduced(5, 5));
    
    area.removeFromTop(10);
    currentSliceLabel.setBounds(area.removeFromTop(25));
}

//==============================================================================
// Event Handlers
//==============================================================================

void MainComponent::loadButtonClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select an audio file to load...",
        juce::File{},
        "*.wav;*.mp3;*.aif;*.aiff;*.flac");
    
    auto chooserFlags = juce::FileBrowserComponent::openMode
                      | juce::FileBrowserComponent::canSelectFiles;
    
    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file != juce::File{})
        {
            if (bufferManager.loadAudioFile(0, file, formatManager))
            {
                playButton.setEnabled(true);
                randomSliceButton.setEnabled(true);
                resetButton.setEnabled(true);
                loadButton.setButtonText("Load Audio File (" + file.getFileNameWithoutExtension() + ")");
                
                // Initialize slice count
                sliceCountChanged();
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Error",
                    "Could not load the selected audio file.");
            }
        }
    });
}

void MainComponent::playButtonClicked()
{
    if (!testBuffer || !testBuffer->hasAudioLoaded())
        return;
    
    testBuffer->setPlaying(true);
    testBuffer->setLooping(true);
    playButton.setEnabled(false);
    stopButton.setEnabled(true);
}

void MainComponent::stopButtonClicked()
{
    if (!testBuffer)
        return;
        
    testBuffer->setPlaying(false);
    playButton.setEnabled(true);
    stopButton.setEnabled(false);
    
    // If we were in random slicing mode, stop it and reset the button
    if (testBuffer->isInContinuousRandomMode())
    {
        testBuffer->exitSlicingMode();
        randomSliceButton.setButtonText("Start Random Slicing");
        randomSliceButton.setColour(juce::TextButton::buttonColourId, juce::Colours::orange);
    }
}

void MainComponent::speedDialValueChanged()
{
    if (!testBuffer)
        return;
        
    const double speed = speedDial.getValue();
    testBuffer->setSpeed(speed);
    
    // Update UI feedback
    if (std::abs(speed) < 0.01)
    {
        speedDial.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::grey);
    }
    else if (speed < 0.0)
    {
        speedDial.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::red);
    }
    else
    {
        speedDial.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::blue);
    }
}

void MainComponent::sliceCountChanged()
{
    if (!testBuffer)
        return;
        
    const int numSlices = static_cast<int>(sliceCountSlider.getValue());
    testBuffer->setNumSlices(numSlices);
    
    // Update current slice display
    updateSliceDisplay();
}

void MainComponent::randomSliceButtonClicked()
{
    if (!testBuffer || !testBuffer->hasAudioLoaded())
        return;
    
    // Check if we're currently in continuous random mode
    bool isCurrentlyInRandomMode = testBuffer->isInContinuousRandomMode();
    
    if (!isCurrentlyInRandomMode)
    {
        // Start random slicing
        testBuffer->startContinuousRandomSlicing();
        randomSliceButton.setButtonText("Stop Random Slicing");
        randomSliceButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red);
        
        // Start playing if not already playing
        if (!testBuffer->isPlaying())
        {
            testBuffer->setPlaying(true);
            playButton.setEnabled(false);
            stopButton.setEnabled(true);
        }
    }
    else
    {
        // Stop random slicing, return to normal playback
        testBuffer->exitSlicingMode();
        randomSliceButton.setButtonText("Start Random Slicing");
        randomSliceButton.setColour(juce::TextButton::buttonColourId, juce::Colours::orange);
    }
}

void MainComponent::resetButtonClicked()
{
    if (!testBuffer)
        return;
        
    testBuffer->resetToDefaults();
    
    // Update UI to reflect reset state
    speedDial.setValue(1.0, juce::dontSendNotification);
    speedDial.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::blue);
    
    playButton.setEnabled(true);
    stopButton.setEnabled(false);
    
    // Reset random slice button
    randomSliceButton.setButtonText("Start Random Slicing");
    randomSliceButton.setColour(juce::TextButton::buttonColourId, juce::Colours::orange);
    
    updateSliceDisplay();
}

void MainComponent::updateSliceDisplay()
{
    if (!testBuffer)
    {
        currentSliceLabel.setText("Current Slice: 0", juce::dontSendNotification);
        currentSliceLabel.setColour(juce::Label::textColourId, juce::Colours::lightblue);
        return;
    }
    
    if (testBuffer->hasAudioLoaded())
    {
        int currentSlice = testBuffer->getCurrentSlice();
        int totalSlices = testBuffer->getNumSlices();
        
        juce::String sliceText = "Current Slice: " + juce::String(currentSlice + 1) + 
                                " / " + juce::String(totalSlices);
        
        if (testBuffer->isInContinuousRandomMode())
        {
            sliceText += " (Random Mode)";
            currentSliceLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
        }
        else
        {
            currentSliceLabel.setColour(juce::Label::textColourId, juce::Colours::lightblue);
        }
        
        currentSliceLabel.setText(sliceText, juce::dontSendNotification);
    }
    else
    {
        currentSliceLabel.setText("Current Slice: 0", juce::dontSendNotification);
        currentSliceLabel.setColour(juce::Label::textColourId, juce::Colours::lightblue);
    }
}




