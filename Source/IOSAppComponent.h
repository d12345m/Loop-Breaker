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

        // Build tabbed UI
        addAndMakeVisible(tabs);
        tabs.setTabBarDepth(40);
        tabs.addTab("Playback", juce::Colours::darkgrey, &playbackContainer, false);
        tabs.addTab("Settings", juce::Colours::darkgrey, &settingsContainer, false);
        tabs.addTab("Logs", juce::Colours::darkgrey, &logsContainer, false);

        // Playback screen contents
        playbackContainer.addAndMakeVisible(modifierDisplay);
        playbackContainer.addAndMakeVisible(padGrid);
    playbackContainer.addAndMakeVisible(playAllButton);
    playbackContainer.addAndMakeVisible(stopAllButton);
        playbackContainer.addAndMakeVisible(statusLabel);
        statusLabel.setJustificationType(juce::Justification::centredLeft);

        playAllButton.setButtonText("Play");
        stopAllButton.setButtonText("Stop");
    playAllButton.onClick = [this]{ playAllClicked(); };
        stopAllButton.onClick = [this]{ stopAllClicked(); };

    // Settings screen contents: Modifiers toggle, BPM, Quantize, Part selector, Load/Save, and Sample Slot selector
    settingsContainer.addAndMakeVisible(modifiersToggle);
    settingsContainer.addAndMakeVisible(loadButton);
    settingsContainer.addAndMakeVisible(saveButton);
    settingsContainer.addAndMakeVisible(slotLabel);
    settingsContainer.addAndMakeVisible(slotBox);
    loadButton.setButtonText("Load Audio");
    saveButton.setButtonText("Save Project");
    loadButton.onClick = [this]{ loadClicked(); };
    saveButton.onClick = [this]{ saveClicked(); };
    modifiersToggle.setButtonText("Modifiers Enabled");
        // Slot selector (which pad to load the sample into)
        slotLabel.setText("Load to Pad", juce::dontSendNotification);
        slotLabel.setJustificationType(juce::Justification::centredLeft);
        slotBox.addItem("First Empty", 0);
        for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
            slotBox.addItem("Pad " + juce::String(i+1), i+1);
        // Default to First Empty
        slotBox.setSelectedId(0, juce::dontSendNotification);
        selectedLoadSlot = -1; // -1 means First Empty
        slotBox.onChange = [this]
        {
            int id = slotBox.getSelectedId();
            if (id == 0) selectedLoadSlot = -1; // First Empty
            else selectedLoadSlot = juce::jlimit(0, AudioBufferManager::MAX_BUFFERS-1, id-1);
        };

    // Default ON at launch: modifiers enabled
    modifiersToggle.setToggleState(true, juce::dontSendNotification);
    modifiersToggle.onClick = [this]{ updatePlaybackModifierLink(); };

    settingsContainer.addAndMakeVisible(bpmLabel);
    bpmLabel.setText("BPM", juce::dontSendNotification);
    bpmLabel.setJustificationType(juce::Justification::centredLeft);
    settingsContainer.addAndMakeVisible(bpmSlider);
    bpmSlider.setRange(60.0, 200.0, 1.0);
    bpmSlider.setValue(app.settings.bpm);
    bpmSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    bpmSlider.onValueChange = [this]{ app.settings.bpm = bpmSlider.getValue(); refreshStatus(); };

    settingsContainer.addAndMakeVisible(quantizeToggle);
    quantizeToggle.setButtonText("Quantize Triggers");
    quantizeToggle.setToggleState(app.settings.quantizeEnabled, juce::dontSendNotification);
    quantizeToggle.onClick = [this]{ app.settings.quantizeEnabled = quantizeToggle.getToggleState(); app.scheduler.setQuantizationEnabled(app.settings.quantizeEnabled); refreshStatus(); };

    // Subdivision removed for now

        settingsContainer.addAndMakeVisible(partLabel);
        partLabel.setText("Parts", juce::dontSendNotification);
    partLabel.setJustificationType(juce::Justification::centredLeft);
        settingsContainer.addAndMakeVisible(partBox);
        partBox.addItem("1 part", 1);
        partBox.addItem("2 parts", 2);
        partBox.addItem("3 parts", 3);
        partBox.addItem("4 parts", 4);
        {
            int initialParts = app.settings.parts.getNumParts();
            if (initialParts < 1 || initialParts > 4) initialParts = 1;
            partBox.setSelectedId(initialParts, juce::dontSendNotification);
        }
        partBox.onChange = [this]{
            int count = partBox.getSelectedId();
            app.settings.parts.numParts = juce::jlimit(1, 4, count);
            // Re-apply active part to recompute loop windows
            app.setActivePart(app.getActivePart());
            refreshStatus();
        };

        // Logs screen
        logsContainer.addAndMakeVisible(logEditor);
        logEditor.setReadOnly(true);
        logEditor.setAlwaysOnTop(true);
        logEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black);
        logEditor.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);

        padGrid.setAudioFormatManager(&formatManager);

        app.scheduler.addListener(this);
        setAudioChannels(0, 2);
        setSize(420, 800);
        startTimerHz(30);

        // Apply initial scheduler settings
        app.scheduler.setQuantizationEnabled(app.settings.quantizeEnabled);
        app.scheduler.setQuantizationSubdivision(app.settings.quantizeSubdivision);
        // Ensure modifiers are not disabled at launch
        app.scheduler.setSuppressed(false);

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
        // Respect iOS safe area (Dynamic Island / notch)
        auto bounds = getLocalBounds();
        int safeTop = 0;
        if (auto* peer = getPeer())
        {
            auto display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds());
            if (display != nullptr)
                safeTop = display->safeAreaInsets.getTop();
        }
        tabs.setBounds(bounds.withTrimmedTop(safeTop));
        // Layout playback screen
        auto area = playbackContainer.getLocalBounds().reduced(8);
        auto banner = area.removeFromTop(80);
        modifierDisplay.setBounds(banner);
        auto bottomControlsArea = area.removeFromBottom(88).reduced(2);
        padGrid.setBounds(area.reduced(0, 6));
    auto row1 = bottomControlsArea.removeFromTop(44);
    playAllButton.setBounds(row1.removeFromLeft(140).reduced(2));
    stopAllButton.setBounds(row1.removeFromLeft(140).reduced(2));
        statusLabel.setBounds(bottomControlsArea);

    // Layout settings screen
    auto sArea = settingsContainer.getLocalBounds().reduced(16);
    modifiersToggle.setBounds(sArea.removeFromTop(40));
    auto bpmRow = sArea.removeFromTop(40);
    bpmLabel.setBounds(bpmRow.removeFromLeft(80));
    bpmSlider.setBounds(bpmRow);
    auto qRow = sArea.removeFromTop(40);
    quantizeToggle.setBounds(qRow);
    auto partRow = sArea.removeFromTop(40);
    partLabel.setBounds(partRow.removeFromLeft(120));
    partBox.setBounds(partRow.removeFromLeft(140));
    auto slotRow = sArea.removeFromTop(40);
    slotLabel.setBounds(slotRow.removeFromLeft(120));
    slotBox.setBounds(slotRow.removeFromLeft(140));
    auto ioRow = sArea.removeFromTop(40);
    loadButton.setBounds(ioRow.removeFromLeft(140).reduced(2));
    saveButton.setBounds(ioRow.removeFromLeft(160).reduced(2));

        // Layout logs screen
        logEditor.setBounds(logsContainer.getLocalBounds().reduced(8));
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
            auto msg = "Triggered: " + d.shortName + " -> " + targetStr + details;
            statusLabel.setText(msg, juce::dontSendNotification);
            appendLog(msg);
            padGrid.flashPads(t);
            if (!t.isEmpty()) padGrid.clearSelections();
        };
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) doUi(desc, targets);
    else { auto d=desc; auto t=targets; juce::MessageManager::callAsync([this, d, t, doUi]{ doUi(d, t); }); }
    }

private:
    AppState app;
    UpcomingModifierDisplay modifierDisplay;
    PadGridComponent padGrid;
    juce::TextButton playAllButton;
    juce::TextButton stopAllButton;
    juce::TextButton loadButton;
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
        // Stop and reset scheduler timeline so beats/measures reset
        if (app.scheduler.isRunning()) app.scheduler.stop();
        app.scheduler.resetTimeline();
        updatePlaybackModifierLink();
    }

    void loadClicked()
    {
        // Native iOS Document Picker via JUCE FileChooser
        auto flags = juce::FileBrowserComponent::openMode
                   | juce::FileBrowserComponent::canSelectFiles;
        // Audio file patterns supported
        juce::String patterns = "*.wav;*.mp3;*.aif;*.aiff;*.flac";
        auto safeThis = juce::Component::SafePointer<IOSAppComponent>(this);
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select audio file...",
            juce::File(), patterns);
        // Use a simple lambda with minimal captures; release chooser once we have results.
        fileChooser->launchAsync(flags, [safeThis](const juce::FileChooser& fc){
            // Marshal processing to the message thread and avoid touching the chooser inside its callback
            auto urls = fc.getURLResults();
            auto resultFile = (urls.size() > 0) ? urls[0].getLocalFile() : fc.getResult();
            juce::MessageManager::callAsync([safeThis, resultFile]{
                if (auto* self = safeThis.getComponent())
                {
                    // Clear the chooser after we return to the message thread
                    self->fileChooser.reset();

                    juce::File f = resultFile;
                    if (f.existsAsFile())
                    {
                        int targetPad = 0;
                        if (self->selectedLoadSlot < 0)
                        {
                            auto loaded = self->app.bufferManager.getLoadedBufferIndices();
                            juce::Array<int> used = loaded;
                            bool found = false;
                            for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
                            {
                                if (! used.contains(i)) { targetPad = i; found = true; break; }
                            }
                            if (!found) targetPad = 0; // fallback
                        }
                        else
                        {
                            targetPad = juce::jlimit(0, AudioBufferManager::MAX_BUFFERS-1, self->selectedLoadSlot);
                        }

                        if (self->app.bufferManager.loadAudioFile(targetPad, f, self->formatManager))
                        {
                            self->statusLabel.setText("Loaded to Pad " + juce::String(targetPad+1) + ": " + f.getFileName(), juce::dontSendNotification);
                            self->padGrid.setPadFileName(targetPad, f.getFileNameWithoutExtension());
                            while (self->app.settings.padFilePaths.size() < AudioBufferManager::MAX_BUFFERS)
                                self->app.settings.padFilePaths.add(juce::String());
                            self->app.settings.padFilePaths.set(targetPad, f.getFullPathName());
                            self->padGrid.setPadFilePath(targetPad, f.getFullPathName());
                            // Sync full list in case grid expects batched paths
                            self->padGrid.setPadFilePaths(self->app.settings.padFilePaths);
                            self->padGrid.repaint();
                        }
                        else
                        {
                            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Load Failed", "Could not load file.");
                        }
                    }
                }
            });
        });
    }

    void saveClicked()
    {
        // Choose a directory to save the project (settings) JSON/state
        auto flags = juce::FileBrowserComponent::saveMode
                   | juce::FileBrowserComponent::canSelectDirectories
                   | juce::FileBrowserComponent::warnAboutOverwriting;
        auto safeThis = juce::Component::SafePointer<IOSAppComponent>(this);
        auto defaultDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        fileChooser = std::make_unique<juce::FileChooser>("Choose save location...", defaultDir);
        fileChooser->launchAsync(flags, [safeThis](const juce::FileChooser& fc){
            if (auto* self = safeThis.getComponent())
            {
                self->fileChooser.reset();
                auto f = fc.getResult();
                if (f.exists())
                {
                    juce::File dir = f;
                    if (! dir.isDirectory()) dir = f.getParentDirectory();
                    bool ok = self->app.projectManager.saveProject(dir, true);
                    if (ok)
                    {
                        self->statusLabel.setText("Project saved: " + dir.getFullPathName(), juce::dontSendNotification);
                        self->appendLog("Saved project -> " + dir.getFullPathName());
                    }
                    else
                    {
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Save Failed", "Could not save project.");
                    }
                }
            }
        });
    }

    void refreshStatus()
    {
        int playing = app.bufferManager.getPlayingBufferIndices().size();
        static const char* partNames[] = { "A", "B", "C", "D" };
        int partIdx = app.getActivePart();
        juce::String partName = juce::String("Part ") + juce::String(partNames[juce::jlimit(0, 3, partIdx)]);
        juce::String base = partName + " | Playing: " + juce::String(playing) + " | BPM " + juce::String(app.settings.bpm, 0);
        // Reflect suppression, not running state
        auto msg = app.scheduler.isSuppressed() ? ("Modifiers OFF | " + base) : ("Modifiers ON | " + base);
        statusLabel.setText(msg, juce::dontSendNotification);
        appendLog(msg);
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

    // UI containers
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::Component playbackContainer;
    juce::Component settingsContainer;
    juce::Component logsContainer;

    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::TextEditor logEditor;

    // Settings controls
    juce::Label bpmLabel;
    juce::Slider bpmSlider;
    juce::ToggleButton quantizeToggle;
    juce::Label partLabel;
    juce::ComboBox partBox;
    juce::Label slotLabel;
    juce::ComboBox slotBox;
    juce::TextButton saveButton;
    int selectedLoadSlot = 0; // 0-based index

    void appendLog(const juce::String& msg)
    {
        logEditor.moveCaretToEnd();
        logEditor.insertTextAtCaret(msg + "\n");
    }
};

#endif // JUCE_IOS
