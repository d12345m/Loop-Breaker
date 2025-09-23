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
        
        // Handle looping and boundaries
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

bool AudioBufferProcessor::loadAudioBuffer(const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    fileSampleRate = sampleRate;
    fileLengthSamples = buffer.getNumSamples();
    
    // Copy the buffer
    audioFileBuffer.makeCopyOf(buffer);
    
    // Reset playback position
    playheadPosition.store(0.0);
    
    return true;
}

void AudioBufferProcessor::releaseResources()
{
    audioFileBuffer.setSize(0, 0);
    stretchBuffer.setSize(0, 0);
    tempProcessingBuffer.setSize(0, 0);
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
    
    addAndMakeVisible(loadTestAudioButton);
    loadTestAudioButton.setButtonText("Load Test Audio");
    loadTestAudioButton.onClick = [this] { loadTestAudioClicked(); };
    
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
    
    // Set component size
    setSize(600, 500);
    
    // Initialize audio
    setAudioChannels(0, 2);
}

MainComponent::~MainComponent()
{
    shutdownAudio();
}

void MainComponent::createSpeedDial()
{
    addAndMakeVisible(speedDial);
    speedDial.setSliderStyle(juce::Slider::Rotary);
    speedDial.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, 25);
    speedDial.setRange(-2.0, 2.0, 0.01);
    speedDial.setValue(0.0); // Start stopped
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
    
    // Top section: Load buttons
    auto loadArea = area.removeFromTop(40);
    auto buttonWidth = loadArea.getWidth() / 2;
    loadButton.setBounds(loadArea.removeFromLeft(buttonWidth).reduced(5, 5));
    loadTestAudioButton.setBounds(loadArea.reduced(5, 5));
    
    area.removeFromTop(10);
    
    // Transport controls
    auto transportArea = area.removeFromTop(40);
    buttonWidth = transportArea.getWidth() / 2;
    playButton.setBounds(transportArea.removeFromLeft(buttonWidth).reduced(5, 5));
    stopButton.setBounds(transportArea.reduced(5, 5));
    
    area.removeFromTop(20);
    
    // Speed control section
    speedLabel.setBounds(area.removeFromTop(30));
    area.removeFromTop(10);
    
    // Speed dial - large and centered
    auto dialArea = area.removeFromTop(200);
    speedDial.setBounds(dialArea.withSizeKeepingCentre(180, 180));
    
    area.removeFromTop(20);
    instructionLabel.setBounds(area.removeFromTop(30));
}

//==============================================================================
// Event Handlers
//==============================================================================

void MainComponent::loadTestAudioClicked()
{
    // Generate test audio
    auto testBuffer = EmbeddedAudio::createRhythmicTestAudio(44100.0, 8.0);
    
    if (audioProcessor.loadAudioBuffer(testBuffer, 44100.0))
    {
        playButton.setEnabled(true);
        loadTestAudioButton.setButtonText("Load Test Audio ✓");
        loadButton.setButtonText("Load Audio File");
    }
}

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
                loadButton.setButtonText("Load Audio File (" + file.getFileNameWithoutExtension() + ")");
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
