/*
  ==============================================================================

    Professional Audio Buffer Implementation
    
    This implementation provides:
    - High-quality time-stretching for speed changes
    - Seamless reverse playback
    - Professional DSP practices
    - Thread-safe operation
    - Clean architecture for integration

  ==============================================================================
*/

#include "MainComponent.h"

//==============================================================================
// AudioBufferProcessor Implementation
//==============================================================================

AudioBufferProcessor::AudioBufferProcessor()
{
    speedSmoother.reset(128); // Smooth parameter changes over ~3ms at 44.1kHz
    speedSmoother.setCurrentAndTargetValue(1.0);
}

void AudioBufferProcessor::prepareToPlay(double sampleRate, int samplesPerBlockExpected)
{
    hostSampleRate = sampleRate;
    speedSmoother.reset(sampleRate, 0.05); // 50ms smoothing time
    
    // Prepare buffers for processing
    stretchBuffer.setSize(2, samplesPerBlockExpected * 4); // Extra headroom for stretching
    tempProcessingBuffer.setSize(2, samplesPerBlockExpected * 4);
    crossfadeBuffer.setSize(2, crossfadeLength);
    fadeBuffer.setSize(2, sliceFadeLength);
}

void AudioBufferProcessor::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (!hasAudioLoaded() || !isPlaying.load())
    {
        buffer.clear();
        return;
    }
    
    // Smooth speed changes to avoid artifacts
    speedSmoother.setTargetValue(targetSpeed.load());
    
    // Process with time-stretching
    processWithTimeStretching(buffer);
}

void AudioBufferProcessor::processWithTimeStretching(juce::AudioBuffer<float>& outputBuffer)
{
    const int numOutputSamples = outputBuffer.getNumSamples();
    const int numChannels = juce::jmin(outputBuffer.getNumChannels(), audioFileBuffer.getNumChannels());
    
    outputBuffer.clear();
    
    for (int sample = 0; sample < numOutputSamples; ++sample)
    {
        const double speed = speedSmoother.getNextValue();
        double currentPos = playheadPosition.load();
        
        // Handle slice-based playback first
        handleSlicePlayback(currentPos);
        currentPos = playheadPosition.load(); // Get updated position after slice handling
        
        // Only handle normal boundary conditions if not in slicing mode
        if (!isSlicingMode.load())
        {
            // Handle looping and boundaries for normal playback
            if (speed > 0.0) // Forward playback
            {
                if (currentPos >= fileLengthSamples - 1)
                {
                    if (isLooping.load())
                    {
                        currentPos = 0.0;
                        applyCrossfade(outputBuffer, sample, numOutputSamples - sample);
                    }
                    else
                    {
                        isPlaying.store(false);
                        break;
                    }
                }
            }
            else if (speed < 0.0) // Reverse playback
            {
                if (currentPos <= 0.0)
                {
                    if (isLooping.load())
                    {
                        currentPos = static_cast<double>(fileLengthSamples - 1);
                        applyCrossfade(outputBuffer, sample, numOutputSamples - sample);
                    }
                    else
                    {
                        isPlaying.store(false);
                        break;
                    }
                }
            }
            else // Speed == 0, paused
            {
                break;
            }
        }
        else if (speed == 0.0) // Paused in slicing mode
        {
            break;
        }
        
        // Ensure position is within bounds
        currentPos = juce::jlimit(0.0, static_cast<double>(fileLengthSamples - 1), currentPos);
        
        // High-quality interpolation
        const int pos1 = static_cast<int>(currentPos);
        const int pos2 = juce::jmin(pos1 + 1, fileLengthSamples - 1);
        const double fraction = currentPos - pos1;
        
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float sample1 = audioFileBuffer.getSample(channel, pos1);
            const float sample2 = audioFileBuffer.getSample(channel, pos2);
            const float interpolatedSample = sample1 + static_cast<float>(fraction) * (sample2 - sample1);
            
            outputBuffer.setSample(channel, sample, interpolatedSample);
        }
        
        // Advance playhead
        currentPos += speed * (fileSampleRate / hostSampleRate);
        playheadPosition.store(currentPos);
    }
}

void AudioBufferProcessor::applyCrossfade(juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    // Simple crossfade to prevent clicks at loop points
    const int fadeLength = juce::jmin(crossfadeLength, numSamples);
    
    for (int sample = 0; sample < fadeLength; ++sample)
    {
        const float fadeGain = static_cast<float>(sample) / fadeLength;
        
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const float currentSample = buffer.getSample(channel, startSample + sample);
            buffer.setSample(channel, startSample + sample, currentSample * fadeGain);
        }
    }
}

bool AudioBufferProcessor::loadAudioFile(const juce::File& file, juce::AudioFormatManager& formatManager)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    
    if (reader == nullptr)
        return false;
    
    fileSampleRate = reader->sampleRate;
    fileLengthSamples = static_cast<int>(reader->lengthInSamples);
    const int numChannels = static_cast<int>(reader->numChannels);
    
    // Load entire file into memory for reliable playback
    audioFileBuffer.setSize(numChannels, fileLengthSamples);
    reader->read(&audioFileBuffer, 0, fileLengthSamples, 0, true, true);
    
    // Reset playback position
    playheadPosition.store(0.0);
    
    return true;
}

void AudioBufferProcessor::setNumSlices(int numSlices)
{
    this->numSlices = juce::jmax(1, numSlices);
}

void AudioBufferProcessor::triggerSlice(int sliceIndex)
{
    if (sliceIndex >= 0 && sliceIndex < numSlices)
    {
        targetSlice.store(sliceIndex);
        currentActiveSlice.store(sliceIndex);
        sliceTriggered.store(true);
        isSlicingMode.store(true);
    }
}

void AudioBufferProcessor::triggerRandomSlice()
{
    if (numSlices > 1)
    {
        int randomSlice = random.nextInt(numSlices);
        triggerSlice(randomSlice);
        continuousRandomMode.store(true);
    }
}

void AudioBufferProcessor::resetToBeginning()
{
    playheadPosition.store(0.0);
    currentActiveSlice.store(0);
    isSlicingMode.store(false);
    sliceTriggered.store(false);
    continuousRandomMode.store(false);
}

void AudioBufferProcessor::resetToDefaults()
{
    resetToBeginning();
    setSpeed(1.0);
    setLooping(true);
    setPlaying(false);
}

int AudioBufferProcessor::getCurrentSlice() const
{
    if (numSlices <= 1 || fileLengthSamples <= 0)
        return 0;
    
    // If we're in slicing mode, return the active slice rather than calculating from position
    if (isSlicingMode.load())
    {
        return currentActiveSlice.load();
    }
    
    // Otherwise, calculate from playhead position for display purposes
    double currentPos = playheadPosition.load();
    double sliceSize = static_cast<double>(fileLengthSamples) / numSlices;
    return juce::jlimit(0, numSlices - 1, static_cast<int>(currentPos / sliceSize));
}

double AudioBufferProcessor::getSliceStartPosition(int sliceIndex) const
{
    if (sliceIndex < 0 || sliceIndex >= numSlices || fileLengthSamples <= 0)
        return 0.0;
    
    double sliceSize = static_cast<double>(fileLengthSamples) / numSlices;
    int targetSample = static_cast<int>(sliceIndex * sliceSize);
    
    // Find nearest zero crossing for cleaner slice start
    int optimalStart = findNearestZeroCrossing(targetSample);
    return static_cast<double>(optimalStart);
}

double AudioBufferProcessor::getSliceEndPosition(int sliceIndex) const
{
    if (sliceIndex < 0 || sliceIndex >= numSlices || fileLengthSamples <= 0)
        return 0.0;
    
    double sliceSize = static_cast<double>(fileLengthSamples) / numSlices;
    int targetSample = static_cast<int>((sliceIndex + 1) * sliceSize);
    
    // Find nearest zero crossing for cleaner slice end
    int optimalEnd = findNearestZeroCrossing(targetSample);
    return static_cast<double>(optimalEnd);
}

void AudioBufferProcessor::stopRandomSlicing()
{
    continuousRandomMode.store(false);
}

void AudioBufferProcessor::exitSlicingMode()
{
    isSlicingMode.store(false);
    continuousRandomMode.store(false);
    sliceTriggered.store(false);
    currentActiveSlice.store(0);
}

void AudioBufferProcessor::handleSlicePlayback(double& currentPos)
{
    if (!isSlicingMode.load())
        return;
    
    // Check if we need to jump to a new slice
    if (sliceTriggered.load())
    {
        int slice = targetSlice.load();
        double speed = targetSpeed.load();
        
        // Set position based on playback direction
        if (speed >= 0.0)
        {
            currentPos = getSliceStartPosition(slice);
        }
        else
        {
            currentPos = getSliceEndPosition(slice) - 1.0;
        }
        
        currentActiveSlice.store(slice);
        playheadPosition.store(currentPos);
        sliceTriggered.store(false);
        return;
    }
    
    // Handle continuous random mode
    if (continuousRandomMode.load())
    {
        int activeSlice = currentActiveSlice.load();
        double sliceStart = getSliceStartPosition(activeSlice);
        double sliceEnd = getSliceEndPosition(activeSlice);
        
        // Check if we've reached the end/beginning of the current slice based on playback direction
        bool shouldTriggerNext = false;
        double speed = targetSpeed.load();
        
        if (speed > 0.0) // Forward playback
        {
            shouldTriggerNext = (currentPos >= sliceEnd - 1.0);
        }
        else if (speed < 0.0) // Reverse playback
        {
            shouldTriggerNext = (currentPos <= sliceStart);
        }
        
        if (shouldTriggerNext)
        {
            // Trigger a new random slice
            int nextSlice = random.nextInt(numSlices);
            currentActiveSlice.store(nextSlice);
            
            // Set position to appropriate end of new slice based on direction
            if (speed > 0.0)
            {
                currentPos = getSliceStartPosition(nextSlice);
            }
            else
            {
                currentPos = getSliceEndPosition(nextSlice) - 1.0;
            }
            
            playheadPosition.store(currentPos);
            return;
        }
    }
    else
    {
        // Non-continuous slice mode - check if we've reached the end of the current slice
        int activeSlice = currentActiveSlice.load();
        double sliceStart = getSliceStartPosition(activeSlice);
        double sliceEnd = getSliceEndPosition(activeSlice);
        double speed = targetSpeed.load();
        
        if (speed > 0.0 && currentPos >= sliceEnd - 1.0)
        {
            if (isLooping.load())
            {
                // Loop back to the start of the current slice
                currentPos = sliceStart;
                playheadPosition.store(currentPos);
            }
            else
            {
                isPlaying.store(false);
            }
        }
        else if (speed < 0.0 && currentPos <= sliceStart)
        {
            if (isLooping.load())
            {
                // Loop back to the end of the current slice
                currentPos = sliceEnd - 1.0;
                playheadPosition.store(currentPos);
            }
            else
            {
                isPlaying.store(false);
            }
        }
    }
}

void AudioBufferProcessor::releaseResources()
{
    audioFileBuffer.setSize(0, 0);
    stretchBuffer.setSize(0, 0);
    tempProcessingBuffer.setSize(0, 0);
    fadeBuffer.setSize(0, 0);
}

//==============================================================================
// MainComponent Implementation
//==============================================================================

MainComponent::MainComponent()
{
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
    audioProcessor.setSpeed(1.0);
    
    // Start timer for UI updates
    startTimer(100); // Update every 100ms
}

MainComponent::~MainComponent()
{
    stopTimer();
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
    audioProcessor.prepareToPlay(sampleRate, samplesPerBlockExpected);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    audioProcessor.processBlock(*bufferToFill.buffer);
}

void MainComponent::releaseResources()
{
    audioProcessor.releaseResources();
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
            if (audioProcessor.loadAudioFile(file, formatManager))
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
    if (!audioProcessor.hasAudioLoaded())
        return;
    
    audioProcessor.setPlaying(true);
    audioProcessor.setLooping(true);
    playButton.setEnabled(false);
    stopButton.setEnabled(true);
}

void MainComponent::stopButtonClicked()
{
    audioProcessor.setPlaying(false);
    playButton.setEnabled(true);
    stopButton.setEnabled(false);
    
    // If we were in random slicing mode, stop it and reset the button
    if (audioProcessor.isInContinuousRandomMode())
    {
        audioProcessor.exitSlicingMode();
        randomSliceButton.setButtonText("Start Random Slicing");
        randomSliceButton.setColour(juce::TextButton::buttonColourId, juce::Colours::orange);
    }
}

void MainComponent::speedDialValueChanged()
{
    const double speed = speedDial.getValue();
    audioProcessor.setSpeed(speed);
    
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
    const int numSlices = static_cast<int>(sliceCountSlider.getValue());
    audioProcessor.setNumSlices(numSlices);
    
    // Update current slice display
    updateSliceDisplay();
}

void MainComponent::randomSliceButtonClicked()
{
    if (!audioProcessor.hasAudioLoaded())
        return;
    
    // Check if we're currently in continuous random mode
    bool isCurrentlyInRandomMode = audioProcessor.isInContinuousRandomMode();
    
    if (!isCurrentlyInRandomMode)
    {
        // Start random slicing
        audioProcessor.triggerRandomSlice();
        randomSliceButton.setButtonText("Stop Random Slicing");
        randomSliceButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red);
        
        // Start playing if not already playing
        if (!audioProcessor.getIsPlaying())
        {
            audioProcessor.setPlaying(true);
            playButton.setEnabled(false);
            stopButton.setEnabled(true);
        }
    }
    else
    {
        // Stop random slicing, return to normal playback
        audioProcessor.exitSlicingMode();
        randomSliceButton.setButtonText("Start Random Slicing");
        randomSliceButton.setColour(juce::TextButton::buttonColourId, juce::Colours::orange);
    }
}

void MainComponent::resetButtonClicked()
{
    audioProcessor.resetToDefaults();
    
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
    if (audioProcessor.hasAudioLoaded())
    {
        int currentSlice = audioProcessor.getCurrentSlice();
        int totalSlices = audioProcessor.getNumSlices();
        
        juce::String sliceText = "Current Slice: " + juce::String(currentSlice + 1) + 
                                " / " + juce::String(totalSlices);
        
        if (audioProcessor.isInContinuousRandomMode())
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

//==============================================================================
// Click Removal and Crossfade Methods
//==============================================================================

int AudioBufferProcessor::findNearestZeroCrossing(int targetSample, int searchRadius) const
{
    if (audioFileBuffer.getNumSamples() == 0)
        return targetSample;
    
    // Clamp target to valid range
    targetSample = juce::jlimit(0, fileLengthSamples - 1, targetSample);
    
    // Search for zero crossing in both directions
    int bestSample = targetSample;
    float minCrossingValue = std::abs(audioFileBuffer.getSample(0, targetSample));
    
    // Search in a radius around the target
    int searchStart = juce::jmax(0, targetSample - searchRadius);
    int searchEnd = juce::jmin(fileLengthSamples - 1, targetSample + searchRadius);
    
    for (int sample = searchStart; sample < searchEnd - 1; ++sample)
    {
        float currentValue = 0.0f;
        float nextValue = 0.0f;
        
        // Check all channels and find the maximum absolute value
        for (int channel = 0; channel < audioFileBuffer.getNumChannels(); ++channel)
        {
            float channelCurrent = audioFileBuffer.getSample(channel, sample);
            float channelNext = audioFileBuffer.getSample(channel, sample + 1);
            
            currentValue = juce::jmax(currentValue, std::abs(channelCurrent));
            nextValue = juce::jmax(nextValue, std::abs(channelNext));
        }
        
        // Check if this is a zero crossing (signs differ) and closer to zero
        if ((currentValue * nextValue < 0.0f) || // Sign change (zero crossing)
            (currentValue < minCrossingValue))   // Or just closer to zero
        {
            minCrossingValue = currentValue;
            bestSample = sample;
        }
    }
    
    return bestSample;
}

void AudioBufferProcessor::applyAntiClickFade(juce::AudioBuffer<float>& buffer, int startSample, int length, bool fadeIn)
{
    if (length <= 0 || startSample < 0)
        return;
    
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    const int endSample = juce::jmin(startSample + length, numSamples);
    const int actualLength = endSample - startSample;
    
    for (int sample = 0; sample < actualLength; ++sample)
    {
        // Use a smooth cosine fade curve
        float progress = static_cast<float>(sample) / actualLength;
        float fadeGain;
        
        if (fadeIn)
        {
            // Fade in: 0 -> 1 using raised cosine
            fadeGain = 0.5f * (1.0f - std::cos(progress * juce::MathConstants<float>::pi));
        }
        else
        {
            // Fade out: 1 -> 0 using raised cosine
            fadeGain = 0.5f * (1.0f + std::cos(progress * juce::MathConstants<float>::pi));
        }
        
        for (int channel = 0; channel < numChannels; ++channel)
        {
            int sampleIndex = startSample + sample;
            if (sampleIndex < numSamples)
            {
                float currentSample = buffer.getSample(channel, sampleIndex);
                buffer.setSample(channel, sampleIndex, currentSample * fadeGain);
            }
        }
    }
}

void AudioBufferProcessor::applySliceTransitionFade(juce::AudioBuffer<float>& buffer, double currentPos, bool isSliceStart)
{
    const int currentSample = static_cast<int>(currentPos);
    const int numSamples = buffer.getNumSamples();
    
    if (isSliceStart)
    {
        // Apply fade-in at slice start to prevent clicks
        int fadeLength = juce::jmin(sliceFadeLength, numSamples);
        applyAntiClickFade(buffer, 0, fadeLength, true);
    }
    else
    {
        // Apply fade-out at slice end to prevent clicks
        int fadeStart = juce::jmax(0, numSamples - sliceFadeLength);
        int fadeLength = numSamples - fadeStart;
        applyAntiClickFade(buffer, fadeStart, fadeLength, false);
    }
}
