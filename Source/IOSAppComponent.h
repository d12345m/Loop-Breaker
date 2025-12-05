#pragma once

#if JUCE_IOS

#include <JuceHeader.h>
#include "AppState.h"
#include "PadGridComponent.h"
#include "UpcomingModifierDisplay.h"

// Minimal iOS-friendly UI that reuses AppState and core components.
// Header-only implementation to avoid modifying project files.
class IOSAppComponent : public juce::AudioAppComponent,
                        public ModifierSchedulerListener,
                        private juce::Timer
{
public:
    IOSAppComponent()
    {
        formatManager.registerBasicFormats();

        addAndMakeVisible(modifierDisplay);
        addAndMakeVisible(padGrid);
        addAndMakeVisible(playAllButton);
        addAndMakeVisible(stopAllButton);
        addAndMakeVisible(modifiersToggle);
        addAndMakeVisible(statusLabel);
        statusLabel.setJustificationType(juce::Justification::centredLeft);

        playAllButton.setButtonText("Play");
        stopAllButton.setButtonText("Stop");
        modifiersToggle.setButtonText("Modifiers");
        modifiersToggle.setToggleState(true, juce::dontSendNotification);

        playAllButton.onClick = [this]{ playAllClicked(); };
        stopAllButton.onClick = [this]{ stopAllClicked(); };
        modifiersToggle.onClick = [this]{ updatePlaybackModifierLink(); };

        padGrid.setAudioFormatManager(&formatManager);

        app.scheduler.addListener(this);
        setAudioChannels(0, 2);
        setSize(400, 700);
        startTimerHz(30);

        // Apply initial scheduler settings
        app.scheduler.setQuantizationEnabled(app.settings.quantizeEnabled);
        app.scheduler.setQuantizationSubdivision(app.settings.quantizeSubdivision);

        refreshStatus();
        if (app.settings.padFilePaths.size() > 0)
            padGrid.setPadFilePaths(app.settings.padFilePaths);
    }

    ~IOSAppComponent() override
    {
        stopTimer();
        app.scheduler.removeListener(this);
        if (app.scheduler.isRunning()) app.scheduler.stop();
        app.bufferManager.setPerBufferProcessor(nullptr);
        shutdownAudio();
    }

    // AudioAppComponent
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override
    {
        juce::ignoreUnused(samplesPerBlockExpected);
        hostSampleRate = sampleRate;
        app.bufferManager.prepare(sampleRate, samplesPerBlockExpected);
    }

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        app.bufferManager.processBlock(*bufferToFill.buffer);
        double blockSeconds = bufferToFill.numSamples / juce::jmax(1.0, hostSampleRate);
        if (app.scheduler.isRunning())
            app.scheduler.updateTime(blockSeconds);
        app.advanceFxEnvelopes(blockSeconds);
    }

    void releaseResources() override
    {
        app.bufferManager.releaseResources();
    }

    // Component
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(6);
        auto top = area.removeFromTop(70);
        modifierDisplay.setBounds(top);

        auto controls = area.removeFromTop(44).reduced(2);
        playAllButton.setBounds(controls.removeFromLeft(90));
        stopAllButton.setBounds(controls.removeFromLeft(90));
        modifiersToggle.setBounds(controls.removeFromLeft(120));
        statusLabel.setBounds(controls);

        area.removeFromTop(6);
        padGrid.setBounds(area);
    }

    // Scheduler listener
    void upcomingModifierChanged(const ModifierDescriptor& desc) override
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            modifierDisplay.setUpcoming(desc);
        else
        {
            auto copy = desc;
            juce::MessageManager::callAsync([this, copy]{ modifierDisplay.setUpcoming(copy); });
        }
    }

    void modifierTriggered(const ModifierDescriptor& desc, const juce::Array<int>& targets) override
    {
        auto doUi = [this](const ModifierDescriptor& d, const juce::Array<int>& t){
            juce::String targetStr;
            if (t.isEmpty()) targetStr = "(master/global)"; else {
                for (int i = 0; i < t.size(); ++i) targetStr << (i?",":"") << (t[i]+1);
            }
            juce::String details = d.description.isNotEmpty() ? (" | " + d.description) : juce::String();
            statusLabel.setText("Triggered: " + d.shortName + " -> " + targetStr + details, juce::dontSendNotification);
            padGrid.flashPads(t);
            if (!t.isEmpty()) padGrid.clearSelections();
        };
        if (juce::MessageManager::getInstance()->isThisTheMessageThread()) doUi(desc, targets);
        else { auto d=desc; auto t=targets; juce::MessageManager::callAsync([this, d, t]{ doUi(d, t); }); }
    }

private:
    AppState app;
    UpcomingModifierDisplay modifierDisplay;
    PadGridComponent padGrid;
    juce::TextButton playAllButton;
    juce::TextButton stopAllButton;
    juce::ToggleButton modifiersToggle;
    juce::Label statusLabel { {}, "Status" };

    juce::AudioFormatManager formatManager;
    double hostSampleRate = 44100.0;

    void timerCallback() override
    {
        refreshStatus();
        padGrid.setPlayingStates(app.bufferManager.getPlayingBufferIndices());
        for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
        {
            if (auto* buf = app.bufferManager.getBuffer(i))
            {
                padGrid.setPlayheadForPad(i, (double) buf->getPlayheadPositionInSamples());
                padGrid.setTotalSamplesForPad(i, (double) buf->getDurationInSamples());
                padGrid.setLoopWindowForPad(i,
                                            buf->isLoopWindowEnabled(),
                                            (double) buf->getLoopStartSamples(),
                                            (double) buf->getLoopEndSamples());
            }
        }
        padGrid.repaint();
        if (app.scheduler.isRunning())
        {
            modifierDisplay.setCountdown(app.scheduler.getSecondsUntilNextTrigger(),
                                         app.scheduler.getBarsUntilNextTrigger(),
                                         app.scheduler.getProgressToNextTrigger());
            modifierDisplay.setSuppressed(app.scheduler.isSuppressed());
        }
    }

    void playAllClicked()
    {
        app.setActivePart(0);
        app.bufferManager.playAll();
        updatePlaybackModifierLink();
        refreshStatus();
    }

    void stopAllClicked()
    {
        app.bufferManager.stopAll();
        updatePlaybackModifierLink();
    }

    void refreshStatus()
    {
        int playing = app.bufferManager.getPlayingBufferIndices().size();
        static const char* partNames[] = { "A", "B", "C", "D" };
        int partIdx = app.getActivePart();
        juce::String partName = juce::String("Part ") + juce::String(partNames[juce::jlimit(0, 3, partIdx)]);
        juce::String base = partName + " | Playing: " + juce::String(playing) + " | BPM " + juce::String(app.settings.bpm, 0);
        statusLabel.setText(app.scheduler.isRunning() ? ("Modifiers ON | " + base) : ("Modifiers OFF | " + base), juce::dontSendNotification);
    }

    void updatePlaybackModifierLink()
    {
        bool anyPlaying = app.bufferManager.getPlayingBufferIndices().size() > 0;
        bool linkEnabled = modifiersToggle.getToggleState();
        if (linkEnabled)
        {
            if (anyPlaying && !app.scheduler.isRunning()) app.scheduler.start();
            app.scheduler.setSuppressed(false);
        }
        else
        {
            if (anyPlaying && !app.scheduler.isRunning()) app.scheduler.start();
            app.scheduler.setSuppressed(true);
        }
        refreshStatus();
    }
};

#endif // JUCE_IOS
