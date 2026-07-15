#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ThemeEngine.h"

// ---------------------------------------------------------------------------
// Build the APVTS parameter layout – one AudioParameterFloat per modifier type
// ---------------------------------------------------------------------------
juce::AudioProcessorValueTreeState::ParameterLayout
BufferTestAudioProcessor::createParamLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    const auto& types = ModifierProbabilityManager::allModifierTypes();
    for (auto type : types)
    {
        const juce::String id   = "prob_" + juce::String (static_cast<int> (type));
        const juce::String name = ModifierProbabilityManager::getDisplayName (type) + " Probability";
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 },
            name,
            juce::NormalisableRange<float> (0.0f, 1.0f),   // continuous – full MIDI CC resolution
            1.0f,                                           // default: max probability
            juce::AudioParameterFloatAttributes{}
                .withLabel ("%")
                .withStringFromValueFunction ([](float v, int) {
                    const int pct = juce::roundToInt (v * 100.0f);
                    return pct <= 0 ? juce::String ("OFF")
                                    : juce::String (pct) + "%";
                })
        ));
    }
    // Per-pad target probability: controls likelihood of each pad being auto-selected
    for (int i = 0; i < 8; ++i)
    {
        const juce::String id   = "padProb_" + juce::String (i);
        const juce::String name = "Pad " + juce::String (i + 1) + " Target Probability";
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 },
            name,
            juce::NormalisableRange<float> (0.0f, 1.0f),
            1.0f,
            juce::AudioParameterFloatAttributes{}
                .withLabel ("%")
                .withStringFromValueFunction ([](float v, int) {
                    const int pct = juce::roundToInt (v * 100.0f);
                    return pct <= 0 ? juce::String ("OFF")
                                    : juce::String (pct) + "%";
                })
        ));
    }

    // Master volume knob: -12 dB to +12 dB, default 0 dB
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "masterVolume", 1 },
        "Master Volume",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes{}
            .withLabel ("dB")
            .withStringFromValueFunction ([](float v, int) {
                return juce::String (v, 1) + " dB";
            })
    ));

    return { params.begin(), params.end() };
}

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
        , apvts (*this, nullptr, "BufferTestParams", createParamLayout())
{
    formatManager.registerBasicFormats();

    // Catch future mismatches between kNumModifierTypes and allModifierTypes()
    jassert (static_cast<int> (ModifierProbabilityManager::allModifierTypes().size())
             == SessionSettings::kNumModifierTypes);
}

BufferTestAudioProcessor::~BufferTestAudioProcessor()
{
    // Do not permit a late host callback to begin normal rendering while the
    // processor's members are being dismantled.
    resourcesPrepared.store (false, std::memory_order_release);
}

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
    resourcesPrepared.store (false, std::memory_order_release);

    app.bufferManager.prepare(sampleRate, samplesPerBlock);
    app.prepareDSP(sampleRate, samplesPerBlock);

    // §9.1  Pre-allocate per-buffer scratch buffers for parallel processing.
    for (auto& buf : perBufferScratch)
        buf.setSize(2, samplesPerBlock, false, false, true);

    // Set a grace period to ignore transport-stop signals that may occur during bus reconfig.
    // Some hosts call prepareToPlay when enabling/disabling output buses, and may report
    // transport=stopped temporarily. 10 blocks (~5-10ms) should be enough.
    prepareGraceBlocks.store(10);

    // Some hosts may call setStateInformation before prepareToPlay; reload on the audio thread.
    if (pendingRestoreReload.exchange(false))
        reloadBuffersFromPadPaths();

    resourcesPrepared.store (true, std::memory_order_release);
}

void BufferTestAudioProcessor::releaseResources()
{
    // Live may issue a trailing AU render callback while releasing an instance.
    // Make it output silence immediately. AudioBufferManager deliberately keeps
    // its render buffers allocated until the next prepare/destruction so that a
    // callback which was already in flight cannot dereference released storage.
    resourcesPrepared.store (false, std::memory_order_release);

    // Check if there are pad paths that need reloading before killing jobs.
    // releaseResources kills background loader threads, so we must re-arm
    // the reload flag so that the next prepareToPlay / processBlock cycle
    // re-queues the loads.
    bool hasPaths = false;
    for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
    {
        if (i < app.settings.padFilePaths.size() && app.settings.padFilePaths[i].isNotEmpty())
        {
            hasPaths = true;
            break;
        }
    }

    app.bufferManager.releaseResources();

    if (hasPaths)
        pendingRestoreReload.store(true);
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

    if (! resourcesPrepared.load (std::memory_order_acquire))
    {
        buffer.clear();
        return;
    }

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
                if (learnMode && learnPad >= 0 && learnPad <= (kPresetLearnIndexBase + 7))
                {
                    DBG("Capturing note " + juce::String(note) + " for pad " + juce::String(learnPad));
                    learnedMidiNote.store(note);
                    midiLearnEnabled.store(false);  // Disable learn mode after capturing
                    continue;
                }
                
                // Check modifier toggle MIDI note
                if (app.settings.modifierToggleMidiNote >= 0 && app.settings.modifierToggleMidiNote == note)
                {
                    midiModifierToggleRequest.store(true);
                }

                // Check preset recall MIDI notes (A-H)
                for (int pi = 0; pi < 8; ++pi)
                {
                    if (app.settings.presetMidiNoteMap[static_cast<size_t>(pi)] >= 0
                        && app.settings.presetMidiNoteMap[static_cast<size_t>(pi)] == note)
                    {
                        midiPresetRecallRequests[pi].store(true);
                    }
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

            // MIDI CC: control modifier probability sliders
            if (msg.isController())
            {
                const int   cc        = msg.getControllerNumber();
                const float normValue = msg.getControllerValue() / 127.0f;

                const int learnIdx = midiCCLearnParamIndex.load();
                const int padLearnIdx = midiPadProbCCLearnIndex.load();
                if (learnIdx >= 0)
                {
                    // CC learn mode: record which CC was moved and assign it
                    const auto& types = ModifierProbabilityManager::allModifierTypes();
                    if (learnIdx < static_cast<int> (types.size())
                        && learnIdx < SessionSettings::kNumModifierTypes)
                    {
                        app.settings.midiProbCCMap[learnIdx] = cc;
                        learnedMidiCC.store (cc);             // signals the UI
                        midiCCLearnParamIndex.store (-1);     // exit learn mode
                    }
                }
                else if (padLearnIdx >= 0)
                {
                    // CC learn mode for pad target probabilities
                    if (padLearnIdx < 8)
                    {
                        app.settings.midiPadProbCCMap[static_cast<size_t>(padLearnIdx)] = cc;
                        learnedPadProbMidiCC.store (cc);
                        midiPadProbCCLearnIndex.store (-1);
                    }
                }
                else
                {
                    // Normal operation: look up the CC in the modifier prob map
                    const auto& types = ModifierProbabilityManager::allModifierTypes();
                    const int numMapped = juce::jmin (static_cast<int> (types.size()),
                                                      SessionSettings::kNumModifierTypes);
                    for (int idx = 0; idx < numMapped; ++idx)
                    {
                        if (app.settings.midiProbCCMap[idx] == cc)
                        {
                            const juce::String paramId = "prob_" + juce::String (static_cast<int> (types[idx]));
                            if (auto* param = apvts.getParameter (paramId))
                                param->setValueNotifyingHost (normValue);
                        }
                    }

                    // Also check pad target probability CC map
                    for (int i = 0; i < 8; ++i)
                    {
                        if (app.settings.midiPadProbCCMap[static_cast<size_t>(i)] == cc)
                        {
                            const juce::String paramId = "padProb_" + juce::String (i);
                            if (auto* param = apvts.getParameter (paramId))
                                param->setValueNotifyingHost (normValue);
                        }
                    }
                }
            }

            // Note-off is ignored in toggle mode
        }
    }

    // Sync APVTS parameter values → probManager (trivial: 22 atomic float reads per block)
    {
        const auto& types = ModifierProbabilityManager::allModifierTypes();
        for (auto type : types)
        {
            const juce::String id = "prob_" + juce::String (static_cast<int> (type));
            if (auto* rawVal = apvts.getRawParameterValue (id))
                app.settings.modifierProbabilities.setWeight (type, rawVal->load());
        }
    }

    // Sync APVTS pad target probability values → settings
    for (int i = 0; i < 8; ++i)
    {
        const juce::String id = "padProb_" + juce::String (i);
        if (auto* rawVal = apvts.getRawParameterValue (id))
            app.settings.padTargetProbabilities[static_cast<size_t>(i)] = rawVal->load();
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
    // If new audio arrived, apply the active part division (loop window) to
    // only the newly loaded buffers — not all buffers — so existing playback
    // is not disrupted.
    // §4.2  Pass transport state so newly loaded buffers are flagged for
    // musically-deferred start when the DAW is already playing.
    {
        auto loadedIndices = app.bufferManager.applyPendingLoads(hostPlaying);
        if (! loadedIndices.isEmpty())
        {
            for (int idx : loadedIndices)
                app.applyPartToBuffer(idx);

            if (! hostPlaying && !inGracePeriod)
                app.bufferManager.stopAll();
        }
    }

    // Self-healing: if pad file paths exist in settings but the corresponding
    // buffers have no audio loaded and no background loads are in flight,
    // re-trigger the load.  This covers edge cases where releaseResources
    // killed in-flight loads, or DAW lifecycle events caused the initial
    // reload to be missed.
    {
        bool needsReload = false;
        for (int i = 0; i < AudioBufferManager::MAX_BUFFERS && !needsReload; ++i)
        {
            if (i < app.settings.padFilePaths.size()
                && app.settings.padFilePaths[i].isNotEmpty()
                && ! app.bufferManager.getBuffer(i)->hasAudioLoaded())
            {
                needsReload = true;
            }
        }
        if (needsReload && ! app.bufferManager.hasAnyPendingLoads())
            reloadBuffersFromPadPaths();
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
        {
            // §4.2  Clear deferred-start flags so buffers start immediately
            // when the transport resumes (they'll begin at the top of playback).
            app.bufferManager.startBuffersAwaitingMusicalCue();
            app.bufferManager.stopAll();
        }

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

    // Always keep the scheduler running while the host transport is playing
    // so the progress bar stays synchronised with the DAW timeline.
    // Use suppression (not stop/start) to honour the modifiers-enabled toggle
    // without resetting the scheduler's accumulated time.
    if (! app.scheduler.isRunning())
        app.scheduler.start();
    app.scheduler.setSuppressed(! app.settings.modifiersEnabled);

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

    // Sync master volume from APVTS parameter (dB -> linear gain)
    if (auto* mvParam = apvts.getRawParameterValue ("masterVolume"))
        app.bufferManager.setMasterVolume (juce::Decibels::decibelsToGain (mvParam->load()));

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

    // §9.1  Parallel buffer processing — each buffer is rendered into its own
    // pre-allocated scratch buffer by a worker thread (or the audio thread).
    // The merge into mix / per-pad buses is done sequentially afterwards.
    const int mixChannels = mix.getNumChannels();

    // A host is allowed to supply a larger block than it advertised at prepare
    // time. Never resize from the realtime callback: return a silent block and
    // wait for the host's next prepareToPlay instead.
    for (const auto& scratch : perBufferScratch)
    {
        if (scratch.getNumChannels() < mixChannels || scratch.getNumSamples() < numSamples)
        {
            buffer.clear();
            return;
        }
    }

    threadPool.processAll (AudioBufferManager::MAX_BUFFERS, [&] (int bufferIndex)
    {
        auto& scratch = perBufferScratch[(size_t) bufferIndex];

        scratch.clear();
        app.bufferManager.processSingleBuffer (bufferIndex, scratch);
    });

    // Sequential merge: add each buffer's result into the output buses.
    for (int bufferIndex = 0; bufferIndex < AudioBufferManager::MAX_BUFFERS; ++bufferIndex)
    {
        const auto& scratch = perBufferScratch[(size_t) bufferIndex];

        // Always add to the main mix bus
        for (int ch = 0; ch < mixChannels; ++ch)
            mix.addFrom (ch, 0, scratch, ch, 0, numSamples);

        // Also copy to the per-pad bus if enabled: bus 1..8 correspond to pads 0..7
        const int padBusIndex = bufferIndex + 1;
        if (padBusIndex < numOutBuses)
        {
            auto padBus = getBusBuffer (buffer, false, padBusIndex);
            if (padBus.getNumChannels() > 0)
            {
                for (int ch = 0; ch < padBus.getNumChannels(); ++ch)
                    padBus.addFrom (ch, 0, scratch, ch, 0, numSamples);
            }
        }
    }

    // Clip probe: final mix bus (after all pads summed)
    app.clipDetector.mixBusProbe.inspect(mix, numSamples);

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
    // Persist session state required to restore the plugin when the DAW session reopens.
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("v", 2);

    // Pad file paths (absolute; if missing on restore the pad will remain empty)
    juce::Array<juce::var> pads;
    pads.ensureStorageAllocated(AudioBufferManager::MAX_BUFFERS);
    for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
        pads.add(app.settings.padFilePaths[i]);
    obj->setProperty("pads", juce::var(pads));

    // Modifier probability weights – read directly from APVTS (source of truth)
    {
        auto* proObj = new juce::DynamicObject();
        const auto& types = ModifierProbabilityManager::allModifierTypes();
        for (auto type : types)
        {
            const juce::String id = "prob_" + juce::String (static_cast<int> (type));
            if (auto* rawVal = apvts.getRawParameterValue (id))
                proObj->setProperty (juce::Identifier ("t" + juce::String (static_cast<int> (type))),
                                     static_cast<double> (rawVal->load()));
        }
        obj->setProperty ("modProbs", juce::var (proObj));
    }

    // MIDI CC mappings for probability sliders
    {
        juce::Array<juce::var> ccArr;
        ccArr.ensureStorageAllocated (SessionSettings::kNumModifierTypes);
        for (int i = 0; i < SessionSettings::kNumModifierTypes; ++i)
            ccArr.add (app.settings.midiProbCCMap[i]);
        obj->setProperty ("midiProbCC", juce::var (ccArr));
    }

    // Per-pad target probability weights – read directly from APVTS
    {
        juce::Array<juce::var> padProbArr;
        padProbArr.ensureStorageAllocated (8);
        for (int i = 0; i < 8; ++i)
        {
            const juce::String id = "padProb_" + juce::String (i);
            if (auto* rawVal = apvts.getRawParameterValue (id))
                padProbArr.add (static_cast<double> (rawVal->load()));
            else
                padProbArr.add (1.0);
        }
        obj->setProperty ("padProbs", juce::var (padProbArr));
    }

    // MIDI CC mappings for pad target probability sliders
    {
        juce::Array<juce::var> ccArr;
        ccArr.ensureStorageAllocated (8);
        for (int i = 0; i < 8; ++i)
            ccArr.add (app.settings.midiPadProbCCMap[static_cast<size_t>(i)]);
        obj->setProperty ("midiPadProbCC", juce::var (ccArr));
    }

    // Session settings
    obj->setProperty("bpm", app.settings.bpm);
    obj->setProperty("barsBetweenModifiers", app.settings.barsBetweenModifiers);
    obj->setProperty("modifiersEnabled", app.settings.modifiersEnabled);
    obj->setProperty("numParts", app.settings.parts.getNumParts());
    obj->setProperty("activePart", app.settings.parts.activePart);
    obj->setProperty("projectName", app.settings.projectName);

    // MIDI note mappings
    juce::Array<juce::var> midiNotes;
    midiNotes.ensureStorageAllocated(8);
    for (int i = 0; i < 8; ++i)
        midiNotes.add(app.settings.midiNoteMap[i]);
    obj->setProperty("midiNotes", juce::var(midiNotes));

    // Modifier toggle MIDI note
    obj->setProperty("modifierToggleMidiNote", app.settings.modifierToggleMidiNote);

    // Modifier preset bank (A-D snapshots)
    obj->setProperty("presets", app.presetBank.toVar());

    // Preset MIDI note mappings
    {
        juce::Array<juce::var> presetNotes;
        presetNotes.ensureStorageAllocated(4);
        for (int i = 0; i < 4; ++i)
            presetNotes.add(app.settings.presetMidiNoteMap[static_cast<size_t>(i)]);
        obj->setProperty("presetMidiNotes", juce::var(presetNotes));
    }

    // Playback enabled state
    obj->setProperty("playbackEnabled", transportPlaybackEnabled.load());

    // Theme & animation settings
    obj->setProperty("themeName", app.settings.themeName);
    obj->setProperty("animationsEnabled", app.settings.animationsEnabled);
    obj->setProperty("bgCycleEnabled", app.settings.bgCycleEnabled);
    obj->setProperty("padPulseEnabled", app.settings.padPulseEnabled);
    obj->setProperty("progressShimmerEnabled", app.settings.progressShimmerEnabled);
    obj->setProperty("knobGlowEnabled", app.settings.knobGlowEnabled);
    obj->setProperty("animationSpeed", (double) app.settings.animationSpeed);
    obj->setProperty("backgroundMode", app.settings.backgroundMode);

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

    // Update pad file paths
    app.settings.padFilePaths.clearQuick();
    for (int i = 0; i < arr->size(); ++i)
        app.settings.padFilePaths.add(arr->getReference(i).toString());

    while (app.settings.padFilePaths.size() < AudioBufferManager::MAX_BUFFERS)
        app.settings.padFilePaths.add({});
    if (app.settings.padFilePaths.size() > AudioBufferManager::MAX_BUFFERS)
        app.settings.padFilePaths.removeRange(AudioBufferManager::MAX_BUFFERS,
                                              app.settings.padFilePaths.size() - AudioBufferManager::MAX_BUFFERS);

    // Restore modifier probability weights
    if (obj->hasProperty("modProbs"))
    {
        app.settings.modifierProbabilities.fromVar(obj->getProperty("modProbs"));

        // Push restored values into the APVTS so sliders and DAW automation are in sync
        const auto& types = ModifierProbabilityManager::allModifierTypes();
        for (auto type : types)
        {
            const juce::String id = "prob_" + juce::String (static_cast<int> (type));
            if (auto* param = apvts.getParameter (id))
                param->setValueNotifyingHost (app.settings.modifierProbabilities.getWeight (type));
        }
    }

    // Restore MIDI CC mappings for probability sliders
    if (obj->hasProperty ("midiProbCC"))
    {
        auto ccVar = obj->getProperty ("midiProbCC");
        if (ccVar.isArray())
        {
            if (auto* ccArr = ccVar.getArray())
            {
                const int n = juce::jmin (static_cast<int> (ccArr->size()),
                                          SessionSettings::kNumModifierTypes);
                for (int i = 0; i < n; ++i)
                    app.settings.midiProbCCMap[i] = static_cast<int> (ccArr->getReference (i));
            }
        }
    }

    // Restore per-pad target probability weights
    if (obj->hasProperty ("padProbs"))
    {
        auto ppVar = obj->getProperty ("padProbs");
        if (ppVar.isArray())
        {
            if (auto* ppArr = ppVar.getArray())
            {
                const int n = juce::jmin (static_cast<int> (ppArr->size()), 8);
                for (int i = 0; i < n; ++i)
                {
                    const float val = static_cast<float> (static_cast<double> (ppArr->getReference (i)));
                    app.settings.padTargetProbabilities[static_cast<size_t>(i)] = val;

                    const juce::String id = "padProb_" + juce::String (i);
                    if (auto* param = apvts.getParameter (id))
                        param->setValueNotifyingHost (val);
                }
            }
        }
    }

    // Restore MIDI CC mappings for pad target probability sliders
    if (obj->hasProperty ("midiPadProbCC"))
    {
        auto ccVar = obj->getProperty ("midiPadProbCC");
        if (ccVar.isArray())
        {
            if (auto* ccArr = ccVar.getArray())
            {
                const int n = juce::jmin (static_cast<int> (ccArr->size()), 8);
                for (int i = 0; i < n; ++i)
                    app.settings.midiPadProbCCMap[static_cast<size_t>(i)] = static_cast<int> (ccArr->getReference (i));
            }
        }
    }

    // Restore session settings (v2+)
    if (obj->hasProperty("bpm"))
        app.settings.bpm = (double) obj->getProperty("bpm");
    if (obj->hasProperty("barsBetweenModifiers"))
        app.settings.barsBetweenModifiers = (int) obj->getProperty("barsBetweenModifiers");
    if (obj->hasProperty("modifiersEnabled"))
        app.settings.modifiersEnabled = (bool) obj->getProperty("modifiersEnabled");
    if (obj->hasProperty("numParts"))
        app.settings.parts.numParts = juce::jlimit(1, 4, (int) obj->getProperty("numParts"));
    if (obj->hasProperty("activePart"))
        app.settings.parts.activePart = juce::jlimit(0, 3, (int) obj->getProperty("activePart"));
    if (obj->hasProperty("projectName"))
        app.settings.projectName = obj->getProperty("projectName").toString();
    if (obj->hasProperty("playbackEnabled"))
        transportPlaybackEnabled.store((bool) obj->getProperty("playbackEnabled"));

    // Restore MIDI note mappings
    if (obj->hasProperty("midiNotes"))
    {
        auto midiVar = obj->getProperty("midiNotes");
        if (midiVar.isArray())
        {
            auto* midiArr = midiVar.getArray();
            for (int i = 0; i < juce::jmin((int) midiArr->size(), 8); ++i)
                app.settings.midiNoteMap[i] = (int) midiArr->getReference(i);
        }
    }

    // Restore modifier toggle MIDI note
    if (obj->hasProperty("modifierToggleMidiNote"))
        app.settings.modifierToggleMidiNote = (int) obj->getProperty("modifierToggleMidiNote");

    // Restore modifier preset bank
    if (obj->hasProperty("presets"))
        app.presetBank = ModifierPresetBank::fromVar(obj->getProperty("presets"));

    // Restore preset MIDI note mappings
    if (obj->hasProperty("presetMidiNotes"))
    {
        auto pnVar = obj->getProperty("presetMidiNotes");
        if (pnVar.isArray())
        {
            auto* pnArr = pnVar.getArray();
            const int n = juce::jmin((int) pnArr->size(), 4);
            for (int i = 0; i < n; ++i)
                app.settings.presetMidiNoteMap[static_cast<size_t>(i)] = (int) pnArr->getReference(i);
        }
    }

    // Restore theme & animation settings
    if (obj->hasProperty("themeName"))
        app.settings.themeName = obj->getProperty("themeName").toString();
    if (obj->hasProperty("animationsEnabled"))
        app.settings.animationsEnabled = (bool) obj->getProperty("animationsEnabled");
    if (obj->hasProperty("bgCycleEnabled"))
        app.settings.bgCycleEnabled = (bool) obj->getProperty("bgCycleEnabled");
    if (obj->hasProperty("padPulseEnabled"))
        app.settings.padPulseEnabled = (bool) obj->getProperty("padPulseEnabled");
    if (obj->hasProperty("progressShimmerEnabled"))
        app.settings.progressShimmerEnabled = (bool) obj->getProperty("progressShimmerEnabled");
    if (obj->hasProperty("knobGlowEnabled"))
        app.settings.knobGlowEnabled = (bool) obj->getProperty("knobGlowEnabled");
    if (obj->hasProperty("animationSpeed"))
        app.settings.animationSpeed = (float) (double) obj->getProperty("animationSpeed");
    if (obj->hasProperty("backgroundMode"))
        app.settings.backgroundMode = (int) obj->getProperty("backgroundMode");

    // Push restored settings to ThemeEngine runtime config
    {
        auto& cfg = ThemeEngine::getInstance().getAnimationConfigMutable();
        cfg.enabled              = app.settings.animationsEnabled;
        cfg.backgroundColorCycle = app.settings.bgCycleEnabled;
        cfg.padPulseOnTrigger    = app.settings.padPulseEnabled;
        cfg.progressBarShimmer   = app.settings.progressShimmerEnabled;
        cfg.knobGlowOnChange     = app.settings.knobGlowEnabled;
        cfg.animationSpeed       = app.settings.animationSpeed;
        switch (app.settings.backgroundMode)
        {
            case 0:  cfg.backgroundMode = BackgroundMode::Static;    break;
            case 2:  cfg.backgroundMode = BackgroundMode::Reactive;  break;
            default: cfg.backgroundMode = BackgroundMode::SlowCycle; break;
        }
    }

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
