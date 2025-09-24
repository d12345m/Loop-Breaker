/*
  ==============================================================================

    Professional Audio Buffer Implementation
    
    This implementation provides:
    - High-quality repitching for speed changes (speed and pitch change together)
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
    
    // Calculate crossfade length in samples
    crossfadeLengthSamples = static_cast<int>(crossfadeLengthMs * sampleRate / 1000.0);
    
    // Prepare buffers for processing
    repitchBuffer.setSize(2, samplesPerBlockExpected * 4); // Extra headroom for repitching
    tempProcessingBuffer.setSize(2, samplesPerBlockExpected * 4);
    crossfadeBuffer.setSize(2, crossfadeLengthSamples * 2); // Buffer for crossfading
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
    
    // Process with repitching (speed and pitch change together)
    processWithRepitching(buffer);
}

void AudioBufferProcessor::processWithRepitching(juce::AudioBuffer<float>& outputBuffer)
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
        
        // Apply crossfading for slice transitions (if active)
        if (isInCrossfade)
        {
            applyCrossfadeToSliceTransition(outputBuffer, sample, 1);
        }
        
        // Advance playhead
        currentPos += speed * (fileSampleRate / hostSampleRate);
        playheadPosition.store(currentPos);
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
    
    return static_cast<double>(targetSample);
}

double AudioBufferProcessor::getSliceEndPosition(int sliceIndex) const
{
    if (sliceIndex < 0 || sliceIndex >= numSlices || fileLengthSamples <= 0)
        return 0.0;
    
    double sliceSize = static_cast<double>(fileLengthSamples) / numSlices;
    int targetSample = static_cast<int>((sliceIndex + 1) * sliceSize);
    
    return static_cast<double>(targetSample);
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
    
    // Reset crossfade state
    isInCrossfade = false;
    crossfadePosition = 0;
    previousSliceIndex = -1;
}

void AudioBufferProcessor::startSliceCrossfade(int newSliceIndex, double newPlayheadPos)
{
    // Store previous slice information for crossfading
    previousSliceIndex = currentActiveSlice.load();
    previousSlicePlayheadPos = playheadPosition.load();
    
    // Start the crossfade
    isInCrossfade = true;
    crossfadePosition = 0;
    
    // Update to new slice
    currentActiveSlice.store(newSliceIndex);
    playheadPosition.store(newPlayheadPos);
}

void AudioBufferProcessor::applyCrossfadeToSliceTransition(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (!isInCrossfade || previousSliceIndex == -1)
        return;
        
    const int numChannels = juce::jmin(outputBuffer.getNumChannels(), audioFileBuffer.getNumChannels());
    const int remainingCrossfadeSamples = crossfadeLengthSamples - crossfadePosition;
    const int samplesToProcess = juce::jmin(numSamples, remainingCrossfadeSamples);
    
    if (samplesToProcess <= 0)
    {
        isInCrossfade = false;
        return;
    }
    
    // Process crossfade sample by sample
    for (int sample = 0; sample < samplesToProcess; ++sample)
    {
        const double fadeProgress = static_cast<double>(crossfadePosition + sample) / static_cast<double>(crossfadeLengthSamples);
        const float fadeOut = static_cast<float>(1.0 - fadeProgress); // Previous slice fades out
        const float fadeIn = static_cast<float>(fadeProgress); // New slice fades in
        
        // Get sample from previous slice position
        const double prevPos = previousSlicePlayheadPos + sample * targetSpeed.load();
        const int prevSampleIndex = static_cast<int>(prevPos);
        const double prevFraction = prevPos - prevSampleIndex;
        
        if (prevSampleIndex >= 0 && prevSampleIndex < fileLengthSamples - 1)
        {
            for (int channel = 0; channel < numChannels; ++channel)
            {
                // Linear interpolation for previous slice
                const float* readPtr = audioFileBuffer.getReadPointer(channel);
                const float prevSample1 = readPtr[prevSampleIndex];
                const float prevSample2 = readPtr[prevSampleIndex + 1];
                const float prevInterpolatedSample = prevSample1 + static_cast<float>(prevFraction) * (prevSample2 - prevSample1);
                
                // Blend previous slice (fading out) with current slice (fading in)
                float* writePtr = outputBuffer.getWritePointer(channel);
                const int outputIndex = startSample + sample;
                writePtr[outputIndex] = writePtr[outputIndex] * fadeIn + prevInterpolatedSample * fadeOut;
            }
        }
    }
    
    crossfadePosition += samplesToProcess;
    
    // End crossfade if we've processed all samples
    if (crossfadePosition >= crossfadeLengthSamples)
    {
        isInCrossfade = false;
        crossfadePosition = 0;
        previousSliceIndex = -1;
    }
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
        
        // Calculate new position based on playback direction
        double newPos;
        if (speed >= 0.0)
        {
            newPos = getSliceStartPosition(slice);
        }
        else
        {
            newPos = getSliceEndPosition(slice) - 1.0;
        }
        
        // Start crossfade transition to new slice
        startSliceCrossfade(slice, newPos);
        currentPos = newPos;
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
            // Trigger a new random slice with crossfading
            int nextSlice = random.nextInt(numSlices);
            
            // Calculate new position based on playback direction
            double newPos;
            if (speed > 0.0)
            {
                newPos = getSliceStartPosition(nextSlice);
            }
            else
            {
                newPos = getSliceEndPosition(nextSlice) - 1.0;
            }
            
            // Start crossfade transition to new random slice
            startSliceCrossfade(nextSlice, newPos);
            currentPos = newPos;
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
    repitchBuffer.setSize(0, 0);
    tempProcessingBuffer.setSize(0, 0);
    crossfadeBuffer.setSize(0, 0);
    
    // Reset crossfade state
    isInCrossfade = false;
    crossfadePosition = 0;
    previousSliceIndex = -1;
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




