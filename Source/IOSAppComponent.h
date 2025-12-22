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
        // JUCE ComboBox item IDs must be non-zero; reserve a high ID for "First Empty"
        constexpr int kFirstEmptyId = 100;
        slotBox.addItem("First Empty", kFirstEmptyId);
        for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
            slotBox.addItem("Pad " + juce::String(i+1), i+1);
        // Default to First Empty
        slotBox.setSelectedId(kFirstEmptyId, juce::dontSendNotification);
        selectedLoadSlot = -1; // -1 means First Empty
        slotBox.onChange = [this]
        {
            int id = slotBox.getSelectedId();
            if (id == kFirstEmptyId) selectedLoadSlot = -1; // First Empty
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
        auto bounds = getLocalBounds();
        tabs.setBounds(bounds);

        // Playback layout
        {
            auto pb = playbackContainer.getLocalBounds().reduced(10);
            auto topRow = pb.removeFromTop(28);
            statusLabel.setBounds(topRow);

            auto buttonRow = pb.removeFromTop(40);
            auto half = buttonRow.removeFromLeft(buttonRow.getWidth() / 2);
            playAllButton.setBounds(half.reduced(4));
            stopAllButton.setBounds(buttonRow.reduced(4));

            auto modRow = pb.removeFromTop(60);
            modifierDisplay.setBounds(modRow.reduced(4));

            padGrid.setBounds(pb.reduced(4));
        }

        // Settings layout
        {
            auto sb = settingsContainer.getLocalBounds().reduced(12);

            auto row1 = sb.removeFromTop(36);
            modifiersToggle.setBounds(row1.removeFromLeft(row1.getWidth() / 2).reduced(4));
            loadButton.setBounds(row1.removeFromLeft(row1.getWidth() / 2).reduced(4));
            saveButton.setBounds(row1.reduced(4));

            auto row2 = sb.removeFromTop(36);
            bpmLabel.setBounds(row2.removeFromLeft(50));
            bpmSlider.setBounds(row2.reduced(4));

            auto row3 = sb.removeFromTop(32);
            quantizeToggle.setBounds(row3.reduced(4));

            auto row4 = sb.removeFromTop(32);
            partLabel.setBounds(row4.removeFromLeft(60));
            partBox.setBounds(row4.reduced(4));

            auto row5 = sb.removeFromTop(32);
            slotLabel.setBounds(row5.removeFromLeft(80));
            slotBox.setBounds(row5.reduced(4));
        }

        // Logs layout
        logEditor.setBounds(logsContainer.getLocalBounds().reduced(6));
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
        appendLog("loadClicked() invoked");
        juce::Logger::outputDebugString("IOSAppComponent::loadClicked invoked");
        auto flags = juce::FileBrowserComponent::openMode
               | juce::FileBrowserComponent::canSelectFiles;

        // Leave patterns empty on iOS to prevent JUCE from querying LaunchServices for
        // every extension (which caused repeated -54 sandbox errors and noticeable delay).
        juce::String patterns;
        auto safeThis = juce::Component::SafePointer<IOSAppComponent>(this);
        
        // Keep fileChooser alive in member variable
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select audio file...",
            juce::File(), patterns);

        fileChooser->launchAsync(flags, [safeThis](const juce::FileChooser& fc){
            auto urls = fc.getURLResults();
            auto resultFile = fc.getResult();
            
            if (urls.isEmpty()) return;

            // CRITICAL: Perform file operations IMMEDIATELY inside the callback.
            if (auto* self = safeThis.getComponent())
            {
                juce::File f = resultFile;
                juce::Logger::outputDebugString("FileChooser callback for: " + f.getFullPathName());

                static const juce::StringArray allowedExtensions { ".wav", ".mp3", ".aif", ".aiff", ".flac" };
                auto ext = f.getFileExtension().toLowerCase();
                if (! allowedExtensions.contains(ext))
                {
                    juce::String msg = "Unsupported file type: " + (ext.isNotEmpty() ? ext : juce::String("<none>"));
                    juce::Logger::outputDebugString(msg);
                    juce::MessageManager::callAsync([safeThis, msg]{
                        if (auto* s = safeThis.getComponent())
                        {
                            s->fileChooser.reset();
                            s->appendLog(msg);
                            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Unsupported", msg);
                        }
                    });
                    return;
                }

                auto docs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
                juce::File dest = docs.getChildFile(f.getFileName());
                
                // Ensure unique filename
                int suffix = 1;
                while (dest.existsAsFile()) {
                    dest = docs.getChildFile(f.getFileNameWithoutExtension() + "-" + juce::String(suffix) + f.getFileExtension());
                    ++suffix;
                }

                juce::String errorMsg;
                bool copyOk = false;

                // Method 1: Try JUCE URL InputStream (safest if JUCE handles scope)
                juce::URL sourceUrl = urls[0];
                std::unique_ptr<juce::InputStream> inStream = sourceUrl.createInputStream(false);
                if (inStream != nullptr)
                {
                    juce::Logger::outputDebugString("Opened InputStream from URL successfully.");
                    juce::FileOutputStream outStream(dest);
                    if (outStream.openedOk())
                    {
                        auto bytesWritten = outStream.writeFromInputStream(*inStream, -1);
                        if (bytesWritten > 0)
                        {
                            copyOk = true;
                            juce::Logger::outputDebugString("Copied " + juce::String(bytesWritten) + " bytes via InputStream.");
                        }
                        else
                        {
                            errorMsg = "Wrote 0 bytes from stream";
                            juce::Logger::outputDebugString(errorMsg);
                        }
                    }
                    else
                    {
                        errorMsg = "Could not open output stream: " + dest.getFullPathName();
                        juce::Logger::outputDebugString(errorMsg);
                    }
                }
                else
                {
                    juce::Logger::outputDebugString("InputStream failed, trying secureCopyFile...");
                    // Method 2: Fallback to native secure copy
                    copyOk = self->secureCopyFile(f, dest, errorMsg);
                }

                // Now schedule the UI update and cleanup. 
                juce::MessageManager::callAsync([safeThis, dest, copyOk, errorMsg]{
                    if (auto* s = safeThis.getComponent())
                    {
                        s->fileChooser.reset(); // Safe to reset now that we are out of the callback

                        if (copyOk)
                        {
                            s->appendLog("Copied OK to: " + dest.getFullPathName());
                            juce::Logger::outputDebugString("UI Update: Copy OK");
                            
                            int targetPad = 0;
                            if (s->selectedLoadSlot < 0)
                            {
                                auto loaded = s->app.bufferManager.getLoadedBufferIndices();
                                bool found = false;
                                for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
                                {
                                    if (! loaded.contains(i)) { targetPad = i; found = true; break; }
                                }
                                if (!found) targetPad = 0;
                            }
                            else
                            {
                                targetPad = juce::jlimit(0, AudioBufferManager::MAX_BUFFERS-1, s->selectedLoadSlot);
                            }

                            if (s->app.bufferManager.loadAudioFile(targetPad, dest, s->formatManager))
                            {
                                s->statusLabel.setText("Loaded to Pad " + juce::String(targetPad+1) + ": " + dest.getFileName(), juce::dontSendNotification);
                                s->padGrid.setPadFileName(targetPad, dest.getFileNameWithoutExtension());
                                while (s->app.settings.padFilePaths.size() < AudioBufferManager::MAX_BUFFERS)
                                    s->app.settings.padFilePaths.add(juce::String());
                                s->app.settings.padFilePaths.set(targetPad, dest.getFullPathName());
                                s->padGrid.setPadFilePath(targetPad, dest.getFullPathName());
                                s->padGrid.setPadFilePaths(s->app.settings.padFilePaths);
                                s->padGrid.repaint();
                            }
                            else
                            {
                                s->appendLog("Load FAILED from sandbox: " + dest.getFullPathName());
                                juce::Logger::outputDebugString("Load FAILED from sandbox");
                                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Load Failed", "Could not load file.");
                            }
                        }
                        else
                        {
                            s->appendLog("Copy FAILED: " + errorMsg);
                            juce::Logger::outputDebugString("UI Update: Copy FAILED: " + errorMsg);
                            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Copy Failed", "Could not copy file. " + errorMsg);
                        }
                    }
                });
            }
        });
    }

    void saveClicked()
    {
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

    // Helper: secure copy using NSFileCoordinator
    bool secureCopyFile(const juce::File& src, const juce::File& dest, juce::String& errorMsg)
    {
       #if JUCE_IOS && __OBJC__
        @autoreleasepool {
            NSString* srcPath = [NSString stringWithUTF8String: src.getFullPathName().toRawUTF8()];
            NSURL* srcUrl = [NSURL fileURLWithPath:srcPath];
            
            BOOL accessing = [srcUrl startAccessingSecurityScopedResource];
            if (!accessing) {
                juce::Logger::outputDebugString("secureCopyFile: startAccessing returned NO (might be normal for local files)");
            }

            __block BOOL copySuccess = NO;
            __block NSError* copyError = nil;

            NSFileCoordinator* coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
            NSError* coordError = nil;
            
            [coordinator coordinateReadingItemAtURL:srcUrl 
                                            options:NSFileCoordinatorReadingWithoutChanges 
                                              error:&coordError 
                                         byAccessor:^(NSURL *newURL) {
                
                NSFileManager* fm = [NSFileManager defaultManager];
                NSString* dstPath = [NSString stringWithUTF8String: dest.getFullPathName().toRawUTF8()];
                NSURL* dstUrl = [NSURL fileURLWithPath:dstPath];
                
                if ([fm fileExistsAtPath:dstUrl.path]) {
                    [fm removeItemAtURL:dstUrl error:nil];
                }
                
                copySuccess = [fm copyItemAtURL:newURL toURL:dstUrl error:&copyError];
            }];

            if (accessing) [srcUrl stopAccessingSecurityScopedResource];

            if (coordError != nil) {
                const char* errStr = [coordError.localizedDescription UTF8String];
                errorMsg = "Coordinator error: " + juce::String(errStr ? errStr : "unknown");
                return false;
            }
            
            if (!copySuccess) {
                if (copyError) {
                    const char* errStr = [copyError.localizedDescription UTF8String];
                    errorMsg = "Copy error: " + juce::String(errStr ? errStr : "unknown");
                } else {
                    errorMsg = "Copy failed unknown error";
                }
                return false;
            }
            
            return true;
        }
       #else
        bool ok = src.copyFileTo(dest);
        if (!ok) errorMsg = "copy failed";
        return ok;
       #endif
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
    int selectedLoadSlot = -1; // -1 means first empty, otherwise 0-based index

    void appendLog(const juce::String& msg)
    {
        logEditor.moveCaretToEnd();
        logEditor.insertTextAtCaret(msg + "\n");
    }
};

#endif // JUCE_IOS
