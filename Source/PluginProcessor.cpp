#include "PluginProcessor.h"
#include "PluginEditor.h"

BufferTestAudioProcessor::BufferTestAudioProcessor()
        : juce::AudioProcessor (BusesProperties()
                                                     #if ! JucePlugin_IsMidiEffect
                                                        #if ! JucePlugin_IsSynth
                                                         .withInput  ("Input", juce::AudioChannelSet::stereo(), true)
                                                        #endif
                                                         // Main bus is a stereo mix (what the track hosting the plugin hears in Ableton).
                                                         // Pads 1..8 are exposed as additional stereo output buses.
                                                         .withOutput ("Mix",  juce::AudioChannelSet::stereo(), true)
                                                         .withOutput ("Ch1",  juce::AudioChannelSet::stereo(), true)
                                                         .withOutput ("Ch2",  juce::AudioChannelSet::stereo(), true)
                                                         .withOutput ("Ch3",  juce::AudioChannelSet::stereo(), true)
                                                         .withOutput ("Ch4",  juce::AudioChannelSet::stereo(), true)
                                                         .withOutput ("Ch5",  juce::AudioChannelSet::stereo(), true)
                                                         .withOutput ("Ch6",  juce::AudioChannelSet::stereo(), true)
                                                         .withOutput ("Ch7",  juce::AudioChannelSet::stereo(), true)
                                                         .withOutput ("Ch8",  juce::AudioChannelSet::stereo(), true)
                                                     #endif
                                                         )
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

bool BufferTestAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if ((int) layouts.outputBuses.size() != (AudioBufferManager::MAX_BUFFERS + 1))
        return false;

    // Require stereo on the main output bus. Allow other output buses to be stereo or disabled.
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::stereo())
        return false;

    for (int busIndex = 1; busIndex < (int) layouts.outputBuses.size(); ++busIndex)
    {
        const auto& busSet = layouts.outputBuses[(size_t) busIndex];
        if (! busSet.isDisabled() && busSet != juce::AudioChannelSet::stereo())
            return false;
    }

   #if ! JucePlugin_IsSynth
    // Keep input stereo (or disabled). We don't currently process input audio.
    if (layouts.inputBuses.size() > 0)
    {
        const auto& mainIn = layouts.getMainInputChannelSet();
        if (! mainIn.isDisabled() && mainIn != juce::AudioChannelSet::stereo())
            return false;
    }
   #endif

    return true;
}

void BufferTestAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);

    juce::ScopedNoDenormals noDenormals;

    // Multi-out: main output bus is a stereo mix.
    // Individual pads 0..7 map to output buses Ch1..Ch8.
    const int numSamples = buffer.getNumSamples();
    const int numOutBuses = getBusCount(false);
    if (numOutBuses <= 0)
        return;

    // Clear all output buses
    for (int busIndex = 0; busIndex < numOutBuses; ++busIndex)
        getBusBuffer(buffer, false, busIndex).clear();

    auto mix = getBusBuffer(buffer, false, 0);
    if (mix.getNumChannels() == 0)
        return;

    if (scratchBuffer.getNumChannels() != mix.getNumChannels() || scratchBuffer.getNumSamples() < numSamples)
        scratchBuffer.setSize(mix.getNumChannels(), numSamples, false, false, true);

    for (int bufferIndex = 0; bufferIndex < AudioBufferManager::MAX_BUFFERS; ++bufferIndex)
    {
        scratchBuffer.clear();
        app.bufferManager.processSingleBuffer(bufferIndex, scratchBuffer);

        // Always add to the main mix bus (what the track hosting the plugin hears)
        for (int ch = 0; ch < mix.getNumChannels(); ++ch)
            mix.addFrom(ch, 0, scratchBuffer, ch, 0, numSamples);

        // Also copy to the per-pad bus if enabled: bus 1..8 correspond to pads 0..7
        const int padBusIndex = bufferIndex + 1;
        if (padBusIndex < numOutBuses)
        {
            auto padBus = getBusBuffer(buffer, false, padBusIndex);
            if (padBus.getNumChannels() > 0)
            {
                for (int ch = 0; ch < padBus.getNumChannels(); ++ch)
                    padBus.addFrom(ch, 0, scratchBuffer, ch, 0, numSamples);
            }
        }
    }

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
    // Persist the minimal state required to restore loaded samples when the DAW session reopens.
    // (Absolute file paths; if a file is missing on restore, the pad will remain empty.)
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("v", 1);

    juce::Array<juce::var> pads;
    pads.ensureStorageAllocated(AudioBufferManager::MAX_BUFFERS);
    for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
        pads.add(app.settings.padFilePaths[i]);
    obj->setProperty("pads", juce::var(pads));

    const juce::String json = juce::JSON::toString(juce::var(obj.get()), false);
    destData.replaceWith(json.toRawUTF8(), (size_t) json.getNumBytesAsUTF8());
}

void BufferTestAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const juce::String stateString = juce::String::fromUTF8((const char*) data, sizeInBytes);
    juce::var parsed = juce::JSON::parse(stateString);
    if (parsed.isVoid() || ! parsed.isObject())
        return;

    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return;

    if (! obj->hasProperty("pads"))
        return;

    auto padsVar = obj->getProperty("pads");
    if (! padsVar.isArray())
        return;

    auto* arr = padsVar.getArray();
    if (arr == nullptr)
        return;

    // Update settings
    app.settings.padFilePaths.clearQuick();
    for (int i = 0; i < arr->size(); ++i)
        app.settings.padFilePaths.add(arr->getReference(i).toString());

    while (app.settings.padFilePaths.size() < AudioBufferManager::MAX_BUFFERS)
        app.settings.padFilePaths.add({});
    if (app.settings.padFilePaths.size() > AudioBufferManager::MAX_BUFFERS)
        app.settings.padFilePaths.removeRange(AudioBufferManager::MAX_BUFFERS,
                                              app.settings.padFilePaths.size() - AudioBufferManager::MAX_BUFFERS);

    // Reload buffers from paths
    for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
    {
        const auto path = app.settings.padFilePaths[i];
        if (path.isNotEmpty())
        {
            const juce::File f(path);
            if (f.existsAsFile())
            {
                app.bufferManager.loadAudioFile(i, f, formatManager);
                continue;
            }
        }

        app.bufferManager.clearBuffer(i);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BufferTestAudioProcessor();
}
