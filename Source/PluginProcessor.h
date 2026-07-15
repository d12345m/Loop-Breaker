#pragma once

#include <JuceHeader.h>
#include "AppState.h"
#include "ModifierProbabilityManager.h"
#include "RealtimeThreadPool.h"

class BufferTestAudioProcessor : public juce::AudioProcessor
{
public:
    BufferTestAudioProcessor();
    ~BufferTestAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // App accessors (used by editor)
    AppState& getAppState() { return app; }
    const AppState& getAppState() const { return app; }

    juce::AudioFormatManager& getFormatManager() { return formatManager; }

    // Transport-tied playback control
    void requestPlayAll();
    void requestStopAll();
    bool isPlaybackEnabled() const { return transportPlaybackEnabled.load(); }

    // Check and clear any pending MIDI toggle requests for a pad (call from UI thread)
    bool checkAndClearMidiToggle(int padIndex)
    {
        if (padIndex < 0 || padIndex >= 8) return false;
        return midiPadToggleRequests[padIndex].exchange(false);
    }

    // Check and clear a pending MIDI modifier-toggle request (call from UI thread)
    bool checkAndClearModifierToggle()
    {
        return midiModifierToggleRequest.exchange(false);
    }

    // Special learn-mode pad index used for the modifier toggle button
    static constexpr int kModifierToggleLearnIndex = 8;

    // Preset learn-mode sentinel indices (9..16 → preset 0..7)
    static constexpr int kPresetLearnIndexBase = 9;

    // Check and clear a pending MIDI preset-recall request (call from UI thread)
    bool checkAndClearPresetRecall(int slotIndex)
    {
        if (slotIndex < 0 || slotIndex >= 8) return false;
        return midiPresetRecallRequests[slotIndex].exchange(false);
    }

    // MIDI learn mode (pad notes)
    void setMidiLearnMode(bool enabled, int padIndex = -1)
    {
        midiLearnEnabled.store(enabled);
        midiLearnPadIndex.store(padIndex);
    }

    int getMidiLearnPadIndex() const { return midiLearnPadIndex.load(); }
    bool isMidiLearnEnabled() const { return midiLearnEnabled.load(); }

    // Check if a MIDI note was learned (call from UI thread)
    // learnedPadIndex is set to the pad index the note was learned for
    int checkAndClearLearnedNote()
    {
        return learnedMidiNote.exchange(-1);
    }

    // APVTS – exposes probability sliders as automatable plugin parameters
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // MIDI CC learn for probability sliders
    // paramIndex = index into ModifierProbabilityManager::allModifierTypes(), -1 = cancel
    void setMidiCCLearnMode(int paramIndex)
    {
        midiCCLearnParamIndex.store(paramIndex);
    }

    int getMidiCCLearnParamIndex() const { return midiCCLearnParamIndex.load(); }
    bool isMidiCCLearnActive() const    { return midiCCLearnParamIndex.load() >= 0; }

    // Returns the CC# just assigned via learn (-1 if none), clears the pending value.
    // Call from UI thread only.
    int checkAndClearLearnedCC()
    {
        return learnedMidiCC.exchange(-1);
    }

    // MIDI CC learn for pad target probability sliders
    // padIndex = pad index (0-7), -1 = cancel
    void setMidiPadProbCCLearnMode(int padIndex)
    {
        midiPadProbCCLearnIndex.store(padIndex);
    }

    int getMidiPadProbCCLearnIndex() const { return midiPadProbCCLearnIndex.load(); }
    bool isMidiPadProbCCLearnActive() const { return midiPadProbCCLearnIndex.load() >= 0; }

    int checkAndClearLearnedPadProbCC()
    {
        return learnedPadProbMidiCC.exchange(-1);
    }

    enum class HostTransportState : int
    {
        Unknown = 0,
        Playing = 1,
        Stopped = 2,
    };

    enum class HostTransportSource : int
    {
        Unknown = 0,
        Reported = 1,
        Inferred = 2,
    };

    HostTransportState getLastHostTransportState() const
    {
        return static_cast<HostTransportState>(lastHostTransportState.load());
    }

    HostTransportSource getLastHostTransportSource() const
    {
        return static_cast<HostTransportSource>(lastHostTransportSource.load());
    }

private:
    // APVTS must be declared before AppState so its parameters exist before
    // any scheduler or UI component tries to reference them.
    juce::AudioProcessorValueTreeState apvts;

    AppState app;
    juce::AudioFormatManager formatManager;

    // §9.1  Per-buffer scratch buffers for parallel processing.
    // Each worker thread writes to its own scratch, avoiding data races.
    std::array<juce::AudioBuffer<float>, AudioBufferManager::MAX_BUFFERS> perBufferScratch;
    RealtimeThreadPool threadPool { 3 };  // 3 workers + audio thread = 4-way parallelism

    // AU hosts can issue a final render callback during release/reconfiguration.
    // Keep that callback silent rather than touching DSP state which is not prepared.
    std::atomic<bool> resourcesPrepared { false };

    std::atomic<bool> transportPlaybackEnabled { true }; // user-facing enable/disable
    std::atomic<bool> startRequested { false };          // start on next audio block (or immediately if host already playing)
    std::atomic<bool> stopRequested { false };           // stop on next audio block
    std::atomic<bool> lastHostPlaying { false };         // track transport transitions

    // Host transport readout for UI (written on audio thread)
    std::atomic<int> lastHostTransportState { (int) HostTransportState::Unknown };
    std::atomic<int> lastHostTransportSource { (int) HostTransportSource::Unknown };

    // Host play detection fallbacks (audio thread only)
    int64_t lastHostTimeInSamples = -1;
    bool lastHostPpqValid = false;
    double lastHostPpqPosition = 0.0;

    // Cache host tempo to avoid resync work on every block (audio thread only)
    double lastAppliedHostBpm = 0.0;

    // Reference BPM captured on transport start, used to scale playback speed
    // so loops follow DAW tempo changes.
    double tempoReferenceBpm = 0.0;

    // Grace period counter: when > 0, don't stop buffers due to transport state changes.
    // This protects against false transport-stop signals during bus reconfiguration.
    std::atomic<int> prepareGraceBlocks { 0 };

    // State restore: defer disk I/O until we are on the audio thread.
    std::atomic<bool> pendingRestoreReload { false };

    void reloadBuffersFromPadPaths();

    // MIDI pad control: atomic flags for toggle requests (written on audio thread, read/cleared on UI thread)
    std::atomic<bool> midiPadToggleRequests[8] { {false}, {false}, {false}, {false}, {false}, {false}, {false}, {false} };

    // MIDI modifier toggle request (written on audio thread, read/cleared on UI thread)
    std::atomic<bool> midiModifierToggleRequest { false };

    // MIDI learn mode (written on audio thread, read/written on UI thread)
    std::atomic<bool> midiLearnEnabled { false };
    std::atomic<int> midiLearnPadIndex { -1 };
    std::atomic<int> learnedMidiNote { -1 };

    // MIDI CC learn state for modifier probability sliders
    std::atomic<int> midiCCLearnParamIndex { -1 }; // which param index is being learned
    std::atomic<int> learnedMidiCC { -1 };          // the CC# just captured (UI polls & clears)

    // MIDI CC learn state for pad target probability sliders
    std::atomic<int> midiPadProbCCLearnIndex { -1 }; // which pad index is being learned
    std::atomic<int> learnedPadProbMidiCC { -1 };    // the CC# just captured (UI polls & clears)

    // MIDI preset recall requests (written on audio thread, polled from UI thread)
    std::atomic<bool> midiPresetRecallRequests[8] { {false}, {false}, {false}, {false}, {false}, {false}, {false}, {false} };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParamLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BufferTestAudioProcessor)
};
