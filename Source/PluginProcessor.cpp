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

void BufferTestAudioProcessor::requestPlayAll()
{
    transportPlaybackEnabled.store(true);
    startRequested.store(true);
}

void BufferTestAudioProcessor::requestStopAll()
{
    transportPlaybackEnabled.store(false);
    stopRequested.store(true);
}

namespace
{
bool positionInfoLooksEmpty(const juce::AudioPlayHead::PositionInfo& pos)
{
    // If a host provides a PositionInfo object but doesn't populate any optional fields,
    // treat it as "unknown" rather than incorrectly gating audio.
    if (pos.getTimeInSamples().hasValue()) return false;
    if (pos.getTimeInSeconds().hasValue()) return false;
    if (pos.getPpqPosition().hasValue()) return false;
    if (pos.getBpm().hasValue()) return false;
    if (pos.getBarCount().hasValue()) return false;
    return true;
}

bool getHostTimeline(juce::AudioProcessor& processor, double& outPpq, double& outBpm)
{
    if (auto* ph = processor.getPlayHead())
    {
       #if JUCE_MAJOR_VERSION >= 7
        if (auto pos = ph->getPosition())
        {
            auto ppq = pos->getPpqPosition();
            auto bpm = pos->getBpm();
            if (ppq.hasValue() && bpm.hasValue())
            {
                outPpq = *ppq;
                outBpm = *bpm;
                return true;
            }
        }
       #else
        juce::AudioPlayHead::CurrentPositionInfo info;
        if (ph->getCurrentPosition(info) && info.bpm > 0.0)
        {
            outPpq = info.ppqPosition;
            outBpm = info.bpm;
            return true;
        }
       #endif
    }
    return false;
}
}

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

    // Set a grace period to ignore transport-stop signals that may occur during bus reconfig.
    // Some hosts call prepareToPlay when enabling/disabling output buses, and may report
    // transport=stopped temporarily. 10 blocks (~5-10ms) should be enough.
    prepareGraceBlocks.store(10);

    // Some hosts may call setStateInformation before prepareToPlay; reload on the audio thread.
    if (pendingRestoreReload.exchange(false))
        reloadBuffersFromPadPaths();
}

void BufferTestAudioProcessor::releaseResources()
{
    app.bufferManager.releaseResources();
}

bool BufferTestAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Allow flexible bus configurations - DAWs may enable/disable buses dynamically
    const int numOutputBuses = (int) layouts.outputBuses.size();
    
    // Must have at least the main output bus
    if (numOutputBuses < 1)
        return false;

    // Require stereo on the main output bus. Allow other output buses to be stereo or disabled.
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::stereo())
        return false;

    // Check all additional output buses (Ch1..Ch8)
    for (int busIndex = 1; busIndex < numOutputBuses; ++busIndex)
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
    juce::ScopedNoDenormals noDenormals;

    // Process MIDI input
    if (!midi.isEmpty())
    {
        const bool learnMode = midiLearnEnabled.load();
        const int learnPad = midiLearnPadIndex.load();
        
        DBG("MIDI received - learnMode: " + juce::String(learnMode ? "true" : "false") + " learnPad: " + juce::String(learnPad));
        
        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();
            
            if (msg.isNoteOn())
            {
                const int note = msg.getNoteNumber();
                
                DBG("MIDI Note On: " + juce::String(note));
                
                // MIDI learn mode: capture note for assignment
                if (learnMode && learnPad >= 0 && learnPad < 8)
                {
                    DBG("Capturing note " + juce::String(note) + " for pad " + juce::String(learnPad));
                    learnedMidiNote.store(note);
                    midiLearnEnabled.store(false);  // Disable learn mode after capturing
                    continue;
                }
                
                // Normal mode: check if note matches any pad mapping
                const auto& noteMap = app.settings.midiNoteMap;
                for (int i = 0; i < 8; ++i)
                {
                    if (noteMap[i] == note)
                    {
                        midiPadToggleRequests[i].store(true);
                    }
                }
            }
            // Note-off is ignored in toggle mode
        }
    }

    // Transport-tied playback: stop output when the host is stopped.
    struct HostTransportResult
    {
        bool gateAsPlaying = true;
        HostTransportState state = HostTransportState::Unknown;
        HostTransportSource source = HostTransportSource::Unknown;
        bool restartedToBeginning = false;
    };

    const auto host = [this, numSamples = buffer.getNumSamples()]() -> HostTransportResult
    {
        HostTransportResult r;
        auto* ph = getPlayHead();
        if (ph == nullptr)
            return r; // unknown: don't gate

       #if JUCE_MAJOR_VERSION >= 7
        auto posOpt = ph->getPosition();
        if (!posOpt.hasValue())
            return r; // unknown: don't gate

        const auto& pos = *posOpt;
        if (positionInfoLooksEmpty(pos))
            return r; // host gave us nothing meaningful

        auto detectRestart = [&pos, numSamples](HostTransportResult& out,
                                                int64_t prevTimeInSamples,
                                                bool prevPpqValid,
                                                double prevPpqPosition)
        {
            // If the host playhead jumps backwards to (near) the start, treat it as a restart.
            // This makes pads restart from the beginning when the user sets the DAW playhead to bar 1.
            bool restarted = false;

            if (auto tis = pos.getTimeInSamples(); tis.hasValue())
            {
                const int64_t current = (int64_t) *tis;
                if (prevTimeInSamples >= 0)
                {
                    const int64_t delta = current - prevTimeInSamples;
                    const int64_t nearStart = (int64_t) numSamples * 4;

                    if (delta < -(int64_t) numSamples * 4 && current <= nearStart)
                        restarted = true;
                }
            }

            if (auto ppq = pos.getPpqPosition(); ppq.hasValue())
            {
                const double current = (double) *ppq;
                // Most hosts report bar 1 beat 1 near PPQ = 0.0.
                const bool nearStart = current <= 0.25;
                if (prevPpqValid)
                {
                    const double delta = current - prevPpqPosition;
                    if (delta < -0.5 && nearStart)
                        restarted = true;
                }
            }

            out.restartedToBeginning = restarted;
        };

        const bool reportedPlaying = pos.getIsPlaying();
        if (reportedPlaying)
        {
            r.gateAsPlaying = true;
            r.state = HostTransportState::Playing;
            r.source = HostTransportSource::Reported;

            const int64_t prevTime = lastHostTimeInSamples;
            const bool prevPpqValid = lastHostPpqValid;
            const double prevPpq = lastHostPpqPosition;
            detectRestart(r, prevTime, prevPpqValid, prevPpq);

            // Keep inference state warm when playing.
            if (auto tis = pos.getTimeInSamples(); tis.hasValue())
                lastHostTimeInSamples = (int64_t) *tis;
            if (auto ppq = pos.getPpqPosition(); ppq.hasValue())
            {
                lastHostPpqValid = true;
                lastHostPpqPosition = (double) *ppq;
            }
            return r;
        }

        // Fallback inference: some hosts/plugins report isPlaying=false while timeline still advances.
        bool inferredPlaying = false;

        const int64_t prevTime = lastHostTimeInSamples;
        const bool prevPpqValid = lastHostPpqValid;
        const double prevPpq = lastHostPpqPosition;

        if (auto tis = pos.getTimeInSamples(); tis.hasValue())
        {
            const int64_t current = (int64_t) *tis;
            if (lastHostTimeInSamples >= 0)
            {
                const int64_t delta = current - lastHostTimeInSamples;
                if (delta > 0 && delta <= (int64_t) numSamples * 8)
                    inferredPlaying = true;
            }
            lastHostTimeInSamples = current;
        }
        else
        {
            lastHostTimeInSamples = -1;
        }

        if (auto ppq = pos.getPpqPosition(); ppq.hasValue())
        {
            const double current = (double) *ppq;
            if (lastHostPpqValid)
            {
                const double delta = current - lastHostPpqPosition;
                if (delta > 0.0 && delta < 16.0)
                    inferredPlaying = true;
            }
            lastHostPpqValid = true;
            lastHostPpqPosition = current;
        }
        else
        {
            lastHostPpqValid = false;
        }

        // Even if the host reports stopped, we still want to detect explicit playhead jumps.
        detectRestart(r, prevTime, prevPpqValid, prevPpq);

        r.gateAsPlaying = inferredPlaying;
        r.state = inferredPlaying ? HostTransportState::Playing : HostTransportState::Stopped;
        r.source = inferredPlaying ? HostTransportSource::Inferred : HostTransportSource::Reported;
        return r;

       #else
        juce::AudioPlayHead::CurrentPositionInfo info;
        if (!ph->getCurrentPosition(info))
            return r; // unknown: don't gate

        if (info.isPlaying)
        {
            r.gateAsPlaying = true;
            r.state = HostTransportState::Playing;
            r.source = HostTransportSource::Reported;
            lastHostTimeInSamples = (int64_t) info.timeInSamples;
            lastHostPpqValid = true;
            lastHostPpqPosition = info.ppqPosition;
            return r;
        }

        bool inferredPlaying = false;
        if (lastHostTimeInSamples >= 0)
        {
            const int64_t delta = (int64_t) info.timeInSamples - lastHostTimeInSamples;
            if (delta > 0 && delta <= (int64_t) numSamples * 8)
                inferredPlaying = true;
        }
        lastHostTimeInSamples = (int64_t) info.timeInSamples;
        lastHostPpqValid = true;
        lastHostPpqPosition = info.ppqPosition;
        r.gateAsPlaying = inferredPlaying;
        r.state = inferredPlaying ? HostTransportState::Playing : HostTransportState::Stopped;
        r.source = inferredPlaying ? HostTransportSource::Inferred : HostTransportSource::Reported;
        return r;
       #endif
    }();

    lastHostTransportState.store((int) host.state);
    lastHostTransportSource.store((int) host.source);

    const bool hostPlaying = host.gateAsPlaying;
    const bool wasHostPlaying = lastHostPlaying.exchange(hostPlaying);

    // If the user restarts the DAW playhead to the beginning, restart pads too.
    // Do this even while stopped so it's armed before pressing play.
    if (host.restartedToBeginning)
        app.bufferManager.restartAllLoadedBuffersToBeginning();

    // If we have pending state-restored samples to reload, schedule background decode now.
    if (pendingRestoreReload.exchange(false))
        reloadBuffersFromPadPaths();

    // Decrement grace period counter (protects against false stops during bus reconfig).
    int graceRemaining = prepareGraceBlocks.load();
    if (graceRemaining > 0)
        prepareGraceBlocks.store(graceRemaining - 1);
    const bool inGracePeriod = graceRemaining > 0;

    // Apply any completed background loads (fast pointer swap; no disk I/O).
    // If new audio arrived, immediately apply the active part division (loop window) so
    // the buffer is segmented without waiting for a SwitchPart modifier.
    if (app.bufferManager.applyPendingLoads() > 0)
    {
        app.setActivePart(app.getActivePart());
        if (! hostPlaying && !inGracePeriod)
            app.bufferManager.stopAll();
    }

    if (! hostPlaying)
    {
        // Stop modifier queue when the host transport is stopped.
        if (app.scheduler.isRunning())
            app.scheduler.stop();

        // When stopped, reset tempo-follow multiplier so the next transport start establishes
        // a fresh reference BPM.
        tempoReferenceBpm = 0.0;
        app.bufferManager.setTempoMultiplier(1.0);

        // On transport stop transition, stop buffers so the next play starts cleanly.
        // BUT: don't stop during grace period (bus reconfig may falsely report transport stopped).
        if (wasHostPlaying && !inGracePeriod)
            app.bufferManager.stopAll();

        // Clear any pending start request (so we don't "surprise start" when transport resumes unless user pressed Play again).
        startRequested.store(false);

        // Always output silence when the host transport is stopped.
        const int numOutBuses = getBusCount(false);
        for (int busIndex = 0; busIndex < numOutBuses; ++busIndex)
            getBusBuffer(buffer, false, busIndex).clear();
        return;
    }

    // Handle stop request first (disables playback).
    if (stopRequested.exchange(false))
        app.bufferManager.stopAll();

    const bool enabled = transportPlaybackEnabled.load();

    // Start modifier queue when the host begins playback (and user hasn't disabled modifiers).
    if (app.settings.modifiersEnabled)
    {
        if (! app.scheduler.isRunning())
            app.scheduler.start();
    }
    else
    {
        if (app.scheduler.isRunning())
            app.scheduler.stop();
    }

    // When following the DAW transport, keep buffers playing while the host is playing.
    // This makes transport-start reliable and also starts newly-loaded pads immediately
    // when the transport is already running.
    if (enabled)
        app.bufferManager.playAll();

    // Clear any pending start request (kept for UI semantics).
    startRequested.store(false);

    // If playback is disabled, output silence (but keep UI/state responsive).
    if (! enabled)
    {
        const int numOutBuses = getBusCount(false);
        for (int busIndex = 0; busIndex < numOutBuses; ++busIndex)
            getBusBuffer(buffer, false, busIndex).clear();
        return;
    }

    // Host timeline (tempo) sync. Read once per block and use for tempo-follow and scheduler.
    double ppq = 0.0;
    double bpm = 0.0;
    const bool haveHostTimeline = getHostTimeline(*this, ppq, bpm);

    // Apply tempo-follow playback scaling (repitch) so playback speed tracks DAW tempo changes.
    if (haveHostTimeline && bpm > 0.0)
    {
        if (! wasHostPlaying || tempoReferenceBpm <= 0.0)
            tempoReferenceBpm = bpm;

        const double mult = (tempoReferenceBpm > 0.0 ? (bpm / tempoReferenceBpm) : 1.0);
        app.bufferManager.setTempoMultiplier(mult);

        // Keep our internal BPM in sync with host tempo so any beat/bar-based logic
        // (slicing + FX envelopes/LFOs) tracks DAW tempo changes.
        constexpr double bpmEpsilon = 1.0e-3; // avoid thrashing on tiny float jitter
        if (lastAppliedHostBpm <= 0.0 || std::abs(bpm - lastAppliedHostBpm) > bpmEpsilon)
        {
            app.settings.bpm = bpm;
            app.resyncTempoLFOs();
            lastAppliedHostBpm = bpm;
        }
    }
    else
    {
        // No reliable host tempo -> don't apply tempo-follow scaling.
        app.bufferManager.setTempoMultiplier(1.0);
    }

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
        {
            if (haveHostTimeline)
                app.scheduler.updateHostTimeline(ppq, bpm);
            else
                app.scheduler.updateTime(blockSeconds);
        }
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

    // Defer reloading buffers until we are on the audio thread (prepareToPlay/processBlock).
    pendingRestoreReload.store(true);
}

void BufferTestAudioProcessor::reloadBuffersFromPadPaths()
{
    for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
    {
        const auto path = app.settings.padFilePaths[i];
        if (path.isNotEmpty())
        {
            const juce::File f(path);

            // existsAsFile can be true even if the reader fails to open (codec/permissions).
            if (f.existsAsFile())
            {
                const bool scheduled = app.bufferManager.requestLoadAudioFile(i, f);
                if (! scheduled)
                    juce::Logger::writeToLog("State restore: failed to schedule pad " + juce::String(i + 1)
                                             + " from " + f.getFullPathName());
                continue;
            }

            juce::Logger::writeToLog("State restore: missing file for pad " + juce::String(i + 1)
                                     + " -> " + f.getFullPathName());
        }

        app.bufferManager.clearBuffer(i);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BufferTestAudioProcessor();
}

//==============================================================================
// SoundTouch integration
//
// We don't have Projucer available in this environment to regenerate the Xcode project
// with additional compilation units. To keep things simple and unblock development,
// we compile SoundTouch by unity-including its .cpp files here.
//
// If/when you regenerate project files with Projucer, replace this with proper
// compilation units in the Xcode project instead.
#ifndef BUFFERTEST_ENABLE_SOUNDTOUCH
 #define BUFFERTEST_ENABLE_SOUNDTOUCH 1
#endif

#if BUFFERTEST_ENABLE_SOUNDTOUCH
 // SoundTouch sources use helper macros like `max(...)` in a few .cpp files.
 // In a unity build these can collide/redefine, so we aggressively undef around each include.
 #undef max
 #undef min
 #undef PI
 #undef TWOPI
 #include "../ThirdParty/soundtouch/source/SoundTouch/AAFilter.cpp"
 #undef max
 #undef min
 #undef PI
 #undef TWOPI
 #include "../ThirdParty/soundtouch/source/SoundTouch/BPMDetect.cpp"
 #undef max
 #undef min
 #undef PI
 #undef TWOPI
 #include "../ThirdParty/soundtouch/source/SoundTouch/FIFOSampleBuffer.cpp"
 #undef max
 #undef min
 #undef PI
 #undef TWOPI
 #include "../ThirdParty/soundtouch/source/SoundTouch/FIRFilter.cpp"
 #undef max
 #undef min
 #undef PI
 #undef TWOPI
 #include "../ThirdParty/soundtouch/source/SoundTouch/InterpolateCubic.cpp"
 #undef max
 #undef min
 #undef PI
 #undef TWOPI
 #include "../ThirdParty/soundtouch/source/SoundTouch/InterpolateLinear.cpp"
 #undef max
 #undef min
 #undef PI
 #undef TWOPI
 #include "../ThirdParty/soundtouch/source/SoundTouch/InterpolateShannon.cpp"
 #undef max
 #undef min
 #undef PI
 #undef TWOPI
 #include "../ThirdParty/soundtouch/source/SoundTouch/PeakFinder.cpp"
 #undef max
 #undef min
 #undef PI
 #undef TWOPI
 #include "../ThirdParty/soundtouch/source/SoundTouch/RateTransposer.cpp"
 #undef max
 #undef min
 #undef PI
 #undef TWOPI
 #include "../ThirdParty/soundtouch/source/SoundTouch/SoundTouch.cpp"
 #undef max
 #undef min
 #undef PI
 #undef TWOPI
 #include "../ThirdParty/soundtouch/source/SoundTouch/TDStretch.cpp"
 #undef max
 #undef min
 #undef PI
 #undef TWOPI
 #include "../ThirdParty/soundtouch/source/SoundTouch/cpu_detect_x86.cpp"
 #undef max
 #undef min
 #undef PI
 #undef TWOPI
 #include "../ThirdParty/soundtouch/source/SoundTouch/mmx_optimized.cpp"
 #undef max
 #undef min
 #undef PI
 #undef TWOPI
 #include "../ThirdParty/soundtouch/source/SoundTouch/sse_optimized.cpp"
 #undef max
 #undef min
 #undef PI
 #undef TWOPI
#endif
