#include "PluginProcessor.h"
#include "PluginEditor.h"

BufferTestAudioProcessor::BufferTestAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : juce::AudioProcessor (BusesProperties()
                           #if ! JucePlugin_IsMidiEffect
                            #if ! JucePlugin_IsSynth
                             .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                            #endif
                             .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                           #endif
                             )
#endif
{
    formatManager.registerBasicFormats();
}

BufferTestAudioProcessor::~BufferTestAudioProcessor() = default;

const juce::String BufferTestAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool BufferTestAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool BufferTestAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool BufferTestAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double BufferTestAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int BufferTestAudioProcessor::getNumPrograms()
{
    return 1;
}

int BufferTestAudioProcessor::getCurrentProgram()
{
    return 0;
}

void BufferTestAudioProcessor::setCurrentProgram (int)
{
}

const juce::String BufferTestAudioProcessor::getProgramName (int)
{
    return {};
}

void BufferTestAudioProcessor::changeProgramName (int, const juce::String&)
{
}

void BufferTestAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    app.bufferManager.prepare(sampleRate, samplesPerBlock);
}

void BufferTestAudioProcessor::releaseResources()
{
    app.bufferManager.releaseResources();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool BufferTestAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // For now, keep it simple: require the main output to be mono or stereo.
    // Multi-out bus layouts will be implemented next.
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    const auto& mainIn = layouts.getMainInputChannelSet();
    if (mainIn != juce::AudioChannelSet::disabled()
        && mainIn != juce::AudioChannelSet::mono()
        && mainIn != juce::AudioChannelSet::stereo())
        return false;

    if (mainIn.isDisabled() == false && mainIn != mainOut)
        return false;
   #endif

    return true;
}
#endif

void BufferTestAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);

    juce::ScopedNoDenormals noDenormals;

    // Currently the engine generates audio from internal buffers.
    // Ignore input and render into the provided output buffer.
    app.bufferManager.processBlock(buffer);

    const double sr = getSampleRate();
    if (sr > 0.0)
    {
        const double blockSeconds = (double) buffer.getNumSamples() / sr;
        if (app.scheduler.isRunning())
            app.scheduler.updateTime(blockSeconds);
        app.advanceFxEnvelopes(blockSeconds);
    }
}

bool BufferTestAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* BufferTestAudioProcessor::createEditor()
{
    return new BufferTestAudioProcessorEditor (*this);
}

void BufferTestAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // TODO: plugin state persistence will be wired to ProjectManager/SessionSettings.
    destData.reset();
}

void BufferTestAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::ignoreUnused(data, sizeInBytes);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BufferTestAudioProcessor();
}
