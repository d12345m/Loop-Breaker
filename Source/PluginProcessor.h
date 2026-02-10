#pragma once

#include <JuceHeader.h>
#include "AppState.h"

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
    AppState app;
    juce::AudioFormatManager formatManager;

    juce::AudioBuffer<float> scratchBuffer;

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

    // State restore: defer disk I/O until we are on the audio thread.
    std::atomic<bool> pendingRestoreReload { false };

    void reloadBuffersFromPadPaths();

    // MIDI pad control: atomic flags for toggle requests (written on audio thread, read/cleared on UI thread)
    std::atomic<bool> midiPadToggleRequests[8] { {false}, {false}, {false}, {false}, {false}, {false}, {false}, {false} };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BufferTestAudioProcessor)
};
