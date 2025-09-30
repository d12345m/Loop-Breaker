/*
 ============================================================================== 
   MainAppComponent.cpp
   --------------------------------------------------------------------------
   Initial implementation of new main application UI.
   NOTE: Currently co-exists with legacy MainComponent until feature parity.
 ==============================================================================
*/

#include "MainAppComponent.h"

MainAppComponent::MainAppComponent()
{
    formatManager.registerBasicFormats();

    addAndMakeVisible(modifierDisplay);
    addAndMakeVisible(padGrid);
    addAndMakeVisible(modifierHistory);

    addAndMakeVisible(playAllButton); playAllButton.onClick = [this]{ playAllClicked(); };
    addAndMakeVisible(stopAllButton); stopAllButton.onClick = [this]{ stopAllClicked(); };
    addAndMakeVisible(startSchedulerButton); startSchedulerButton.onClick = [this]{ startSchedulerClicked(); };
    addAndMakeVisible(stopSchedulerButton); stopSchedulerButton.onClick = [this]{ stopSchedulerClicked(); };
    addAndMakeVisible(loadFileButton); loadFileButton.onClick = [this]{ loadFileClicked(); };

    addAndMakeVisible(padSelectForLoad);
    for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
        padSelectForLoad.addItem("Pad " + juce::String(i+1), i+1);
    padSelectForLoad.setSelectedId(1);

    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::centredLeft);

    // Implemented-only toggle (default on)
    implementedOnlyToggle.setToggleState(true, juce::dontSendNotification);
    implementedOnlyToggle.onClick = [this]{ implementedOnlyToggled(); };
    addAndMakeVisible(implementedOnlyToggle);

    // BPM slider
    bpmSlider.setRange(40.0, 240.0, 1.0);
    bpmSlider.setValue(app.settings.bpm, juce::dontSendNotification);
    bpmSlider.onValueChange = [this]{ bpmChanged(); };
    bpmSlider.setSliderStyle(juce::Slider::LinearBar);
    bpmSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    addAndMakeVisible(bpmSlider);
    bpmLabel.attachToComponent(&bpmSlider, true);
    addAndMakeVisible(bpmLabel);

    attachPadCallbacks();

    // Listen for scheduler callbacks
    app.scheduler.addListener(this);

    setAudioChannels(0, 2);
    setSize(920, 600);

    startTimerHz(10); // 100ms UI refresh (status only now; selection via callbacks)
}

MainAppComponent::~MainAppComponent()
{
    stopTimer();
    app.scheduler.removeListener(this);
    shutdownAudio();
}

void MainAppComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    hostSampleRate = sampleRate;
    app.bufferManager.prepare(sampleRate, samplesPerBlockExpected);
}

void MainAppComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    app.bufferManager.processBlock(*bufferToFill.buffer);

    // Feed scheduler with accurate elapsed seconds for this block (sample accurate scheduling step coming later)
    if (app.scheduler.isRunning())
    {
        double blockSeconds = bufferToFill.numSamples / hostSampleRate;
        app.scheduler.updateTime(blockSeconds);
    }
}

void MainAppComponent::releaseResources()
{
    app.bufferManager.releaseResources();
}

void MainAppComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey.darker());
    g.setColour(juce::Colours::grey);
    g.drawRect(getLocalBounds());
}

void MainAppComponent::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto topBar = area.removeFromTop(60);
    modifierDisplay.setBounds(topBar.removeFromLeft(topBar.getWidth() * 0.5f).reduced(4));

    auto controlBar = topBar;
    playAllButton.setBounds(controlBar.removeFromLeft(90).reduced(2));
    stopAllButton.setBounds(controlBar.removeFromLeft(90).reduced(2));
    startSchedulerButton.setBounds(controlBar.removeFromLeft(130).reduced(2));
    stopSchedulerButton.setBounds(controlBar.removeFromLeft(130).reduced(2));
    padSelectForLoad.setBounds(controlBar.removeFromLeft(110).reduced(2));
    loadFileButton.setBounds(controlBar.removeFromLeft(150).reduced(2));
    // Right side region for BPM & toggle & status
    auto rightRegion = controlBar;
    // Place BPM slider at the rightmost ~220px
    auto bpmWidth = 200;
    auto bpmArea = rightRegion.removeFromRight(bpmWidth);
    bpmSlider.setBounds(bpmArea.reduced(2));
    implementedOnlyToggle.setBounds(rightRegion.removeFromRight(140).reduced(2));
    statusLabel.setBounds(rightRegion.reduced(2));

    area.removeFromTop(10);
    auto gridHeight = 300;
    padGrid.setBounds(area.removeFromTop(gridHeight));
    area.removeFromTop(6);
    modifierHistory.setBounds(area);
}

void MainAppComponent::upcomingModifierChanged(const ModifierDescriptor& desc)
{
    modifierDisplay.setUpcoming(desc);
}

void MainAppComponent::modifierTriggered(const ModifierDescriptor& desc, const juce::Array<int>& targets)
{
    // Simple status update; later we'll add visual pad flashes
    juce::String targetStr;
    if (targets.isEmpty()) targetStr = "(master/global)"; else {
        for (int i = 0; i < targets.size(); ++i) targetStr << (i?",":"") << (targets[i]+1);
    }
    statusLabel.setText("Triggered: " + desc.shortName + " -> " + targetStr, juce::dontSendNotification);
    padGrid.flashPads(targets);
    modifierHistory.addEntry(desc, targets);
}

void MainAppComponent::timerCallback()
{
    refreshStatus();
    padGrid.setPlayingStates(app.bufferManager.getPlayingBufferIndices());
    if (app.scheduler.isRunning())
    {
        modifierDisplay.setCountdown(app.scheduler.getSecondsUntilNextTrigger(),
                                     app.scheduler.getBarsUntilNextTrigger(),
                                     app.scheduler.getProgressToNextTrigger());
    }
}

void MainAppComponent::playAllClicked()
{
    app.bufferManager.playAll();
    refreshStatus();
}

void MainAppComponent::stopAllClicked()
{
    app.bufferManager.stopAll();
    refreshStatus();
}

void MainAppComponent::startSchedulerClicked()
{
    app.scheduler.start();
    statusLabel.setText("Scheduler Running", juce::dontSendNotification);
}

void MainAppComponent::stopSchedulerClicked()
{
    app.scheduler.stop();
    statusLabel.setText("Scheduler Stopped", juce::dontSendNotification);
}

void MainAppComponent::loadFileClicked()
{
    int padIndex = padSelectForLoad.getSelectedId() - 1;
    if (padIndex < 0) return;

    fileChooser = std::make_unique<juce::FileChooser>("Select audio file...", juce::File(), "*.wav;*.mp3;*.aif;*.aiff;*.flac");
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(flags, [this, padIndex](const juce::FileChooser& fc){
        auto f = fc.getResult();
        if (f == juce::File()) return;
        if (app.bufferManager.loadAudioFile(padIndex, f, formatManager))
        {
            statusLabel.setText("Loaded to Pad " + juce::String(padIndex+1) + ": " + f.getFileName(), juce::dontSendNotification);
            padGrid.setPadFileName(padIndex, f.getFileNameWithoutExtension());
        } else {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Load Failed", "Could not load file.");
        }
    });
}

void MainAppComponent::updatePadSelectionTargets()
{
    auto selected = padGrid.getSelectedPadIndices();
    app.scheduler.setUserSelectedBuffers(selected);
}

void MainAppComponent::refreshStatus()
{
    // Could display playing buffer count
    int playing = app.bufferManager.getPlayingBufferIndices().size();
    if (!app.scheduler.isRunning()) return; // Preserve explicit status messages when scheduler stopped
    double secUntil = app.scheduler.getSecondsUntilNextTrigger();
    double barsUntil = app.scheduler.getBarsUntilNextTrigger();
    juce::String eta = juce::String(secUntil, 1) + "s / " + juce::String(barsUntil, 2) + " bars";
    statusLabel.setText("Scheduler Running | Next in " + eta + " | Playing: " + juce::String(playing) + " | BPM " + juce::String(app.settings.bpm, 0), juce::dontSendNotification);
}

void MainAppComponent::attachPadCallbacks()
{
    padGrid.setSelectionChangedCallback([this]{ updatePadSelectionTargets(); });
}

void MainAppComponent::implementedOnlyToggled()
{
    bool enabled = implementedOnlyToggle.getToggleState();
    app.scheduler.setRestrictToImplemented(enabled);
}

void MainAppComponent::bpmChanged()
{
    double newBpm = bpmSlider.getValue();
    app.settings.bpm = newBpm; // Direct mutation; scheduler will pick up new bar length for future scheduling
    refreshStatus();
}
