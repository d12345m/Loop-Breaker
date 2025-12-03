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
    // Selection panel now inside a scrollable viewport
    modifierSelectionViewport.setViewedComponent(&modifierSelectionPanel, false);
    addAndMakeVisible(modifierSelectionViewport);
    addAndMakeVisible(fxStatusPanel);
    // Dev controls
    addAndMakeVisible(triggerNowButton); triggerNowButton.onClick = [this]{ app.scheduler.triggerNow(); };
    addAndMakeVisible(skipUpcomingButton); skipUpcomingButton.onClick = [this]{ app.scheduler.skipUpcoming(); };

    addAndMakeVisible(playAllButton); playAllButton.onClick = [this]{ playAllClicked(); };
    addAndMakeVisible(stopAllButton); stopAllButton.onClick = [this]{ stopAllClicked(); };
        addAndMakeVisible(modifiersToggle);
        modifiersToggle.setToggleState(true, juce::dontSendNotification);
        modifiersToggle.onClick = [this]{ modifiersToggleChanged(); };
    addAndMakeVisible(loadFileButton); loadFileButton.onClick = [this]{ loadFileClicked(); };
    // Project name controls
    projectNameLabel.attachToComponent(&projectNameEditor, true);
    addAndMakeVisible(projectNameEditor);
    projectNameEditor.setText(app.settings.projectName, juce::dontSendNotification);
    projectNameEditor.onFocusLost = [this]{ projectNameEdited(); };
    projectNameEditor.onReturnKey = [this]{ projectNameEdited(); };
    addAndMakeVisible(saveProjectButton); saveProjectButton.onClick = [this]{ saveProjectClicked(); };
    addAndMakeVisible(loadProjectButton); loadProjectButton.onClick = [this]{ loadProjectClicked(); };
    // Parts UI buttons removed; use partsCountBox + status only.

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

    // Quantization toggle & subdivision selector (init from settings)
    quantizeToggle.setToggleState(app.settings.quantizeEnabled, juce::dontSendNotification);
    quantizeToggle.onClick = [this]{ quantizeToggled(); };
    addAndMakeVisible(quantizeToggle);
    quantizeSubdivisionBox.addItem("Bar", 1);          // 1
    quantizeSubdivisionBox.addItem("1/2", 2);          // 2
    quantizeSubdivisionBox.addItem("1/4 (Beat)", 3);   // internally map to 4 later (IDs must be unique)
    quantizeSubdivisionBox.addItem("1/8", 4);          // map to 8
    quantizeSubdivisionBox.addItem("1/16", 5);         // map to 16
    // Helper to map settings.quantizeSubdivision to UI id
    auto subdivisionToId = [](int subdiv)->int{
        switch (subdiv) { case 1: return 1; case 2: return 2; case 4: return 3; case 8: return 4; case 16: return 5; default: return 3; }
    };
    quantizeSubdivisionBox.setSelectedId(subdivisionToId(app.settings.quantizeSubdivision), juce::dontSendNotification);
    quantizeSubdivisionBox.onChange = [this]{ quantizeSubdivisionChanged(); };
    addAndMakeVisible(quantizeSubdivisionBox);

    // Parts count selector (set before playback starts)
    partsCountBox.addItem("1 part", 1);
    partsCountBox.addItem("2 parts", 2);
    partsCountBox.addItem("3 parts", 3);
    partsCountBox.addItem("4 parts", 4);
    partsCountBox.setTextWhenNothingSelected("Parts Count");
    {
        int initialParts = app.settings.parts.getNumParts();
        if (initialParts < 1 || initialParts > 4) initialParts = 1;
        partsCountBox.setSelectedId(initialParts, juce::dontSendNotification);
    }
    partsCountBox.onChange = [this]{ partsCountChanged(); };
    addAndMakeVisible(partsCountBox);

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

    modifierSelectionPanel.setForceSelectionCallback([this](ModifierType type){
        app.scheduler.forceUpcomingModifier(type);
    });
    modifierSelectionPanel.setForceVariantCallback([this](ModifierType type, const juce::String& variant){
        app.scheduler.forceUpcomingVariant(type, variant);
    });

    // Listen for scheduler callbacks
    app.scheduler.addListener(this);

    setAudioChannels(0, 2);
    setSize(920, 600);

    startTimerHz(10); // 100ms UI refresh (status only now; selection via callbacks)

    // Apply initial scheduler settings from SessionSettings
    app.scheduler.setQuantizationEnabled(app.settings.quantizeEnabled);
    app.scheduler.setQuantizationSubdivision(app.settings.quantizeSubdivision);
}

MainAppComponent::~MainAppComponent()
{
    stopTimer();
    app.scheduler.removeListener(this);
    // Ensure scheduler stopped and audio callbacks no longer reference lambdas
    if (app.scheduler.isRunning()) app.scheduler.stop();
    // Clear per-buffer processor to avoid processing after destruction
    app.bufferManager.setPerBufferProcessor(nullptr);
    // Cancel any active file chooser to prevent late callbacks
    fileChooser.reset();
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
    double blockSeconds = bufferToFill.numSamples / hostSampleRate;
    if (app.scheduler.isRunning())
    {
        app.scheduler.updateTime(blockSeconds);
    }
    // Advance FX envelopes every block regardless of scheduler state so FX ramps occur even when modifiers are suppressed or scheduler is paused
    app.advanceFxEnvelopes(blockSeconds);
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
        modifiersToggle.setBounds(controlBar.removeFromLeft(120).reduced(2));
    // moved padSelectForLoad and loadFileButton to second row
    // Right side region for BPM & toggle & status
    auto rightRegion = controlBar;
    // Place BPM slider at the rightmost ~220px
    // Reserve extra width for BPM slider plus its left-attached label to prevent overlap
    auto bpmWidth = 270; // slider width portion (label will sit just to the left inside this block)
    auto bpmArea = rightRegion.removeFromRight(bpmWidth);
    bpmSlider.setBounds(bpmArea.reduced(2));

    // Add a small horizontal gap before the toggle to visually separate
    rightRegion.removeFromRight(36);
    implementedOnlyToggle.setBounds(rightRegion.removeFromRight(150).reduced(2));
    // Quantize controls only in right region
    quantizeSubdivisionBox.setBounds(rightRegion.removeFromRight(110).reduced(2));
    quantizeToggle.setBounds(rightRegion.removeFromRight(100).reduced(2));
    // partsCountLabel attached to partsCountBox, no explicit bounds needed
    statusLabel.setBounds(rightRegion.reduced(2));

    // New second row for project actions (Save/Load) to free room for BPM slider
    auto projectBar = area.removeFromTop(28);
    auto projArea = projectBar.reduced(2);
    // Left portion: Project name label + editor
    auto nameWidth = 260;
    auto nameArea = projArea.removeFromLeft(nameWidth);
    projectNameEditor.setBounds(nameArea);
    // Middle: Save/Load buttons
    saveProjectButton.setBounds(projArea.removeFromLeft(130).reduced(2));
    loadProjectButton.setBounds(projArea.removeFromLeft(130).reduced(2));
    // Parts selector moved to second row for dev clarity
    partsCountBox.setBounds(projArea.removeFromLeft(160).reduced(2));
    // Label is attached to partsCountBox; no explicit bounds needed
    // Parts buttons removed; reclaim space for other controls.
    // Right: Pad selector and Load File
    padSelectForLoad.setBounds(projArea.removeFromLeft(110).reduced(2));
    loadFileButton.setBounds(projArea.removeFromLeft(150).reduced(2));
    area.removeFromTop(6);
    auto gridHeight = 220; // reduced pad grid height for more dev control space
    padGrid.setBounds(area.removeFromTop(gridHeight));
    area.removeFromTop(6);
    // Split remaining bottom area: left = history, right = modifier selection panel
    auto bottomArea = area;
    auto rightPanel = bottomArea.removeFromRight(bottomArea.getWidth() / 3).reduced(4);
    auto rightTop = rightPanel.removeFromTop(int(rightPanel.getHeight() * 0.65f)); // allocate more space for selection panel
    // Desired internal height for selection panel (all toggles): rough estimate rows * 26
    int toggleCount = 0;
    // We don't store count directly; approximate by child components count of modifierSelectionPanel
    toggleCount = modifierSelectionPanel.getNumChildComponents();
    int desiredHeight = juce::jmax(rightTop.getHeight() - 60, toggleCount * 26 + 20);
    modifierSelectionPanel.setSize(rightTop.getWidth() - 8, desiredHeight);
    modifierSelectionViewport.setBounds(rightTop.removeFromTop(rightTop.getHeight() - 28));
    // Place dev controls below viewport
    triggerNowButton.setBounds(rightTop.removeFromLeft(120).reduced(2));
    skipUpcomingButton.setBounds(rightTop.removeFromLeft(90).reduced(2));
    // FX status panel gets remaining lower portion
    fxStatusPanel.setBounds(rightPanel);
    modifierHistory.setBounds(bottomArea.reduced(2));
}

// Part buttons removed

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
    // Show detailed descriptor info (including randomized parameters) in status
    juce::String details = desc.description.isNotEmpty() ? (" | " + desc.description) : juce::String();
    statusLabel.setText("Triggered: " + desc.shortName + " -> " + targetStr + details, juce::dontSendNotification);
    padGrid.flashPads(targets);
    modifierHistory.addEntry(desc, targets);
    // If specific user-selected pads were targeted, clear their selection state so user
    // must actively choose new targets for the next modifier cycle.
    if (!targets.isEmpty())
    {
        padGrid.clearSelections();
        // Scheduler already cleared its internal userSelectedBuffers; no need to update again.
    }
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
        modifierDisplay.setSuppressed(app.scheduler.isSuppressed());
    }
}

void MainAppComponent::playAllClicked()
{
    // Ensure playback starts at Part A before playing
    app.setActivePart(0);
    app.bufferManager.playAll();
    updatePlaybackModifierLink();
}

void MainAppComponent::stopAllClicked()
{
    app.bufferManager.stopAll();
    updatePlaybackModifierLink();
}

void MainAppComponent::modifiersToggleChanged()
{
    updatePlaybackModifierLink();
}

void MainAppComponent::loadFileClicked()
{
    int padIndex = padSelectForLoad.getSelectedId() - 1;
    if (padIndex < 0) return;

    fileChooser = std::make_unique<juce::FileChooser>("Select audio file...", juce::File(), "*.wav;*.mp3;*.aif;*.aiff;*.flac");
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    juce::Component::SafePointer<MainAppComponent> safeThis(this);
    fileChooser->launchAsync(flags, [safeThis, padIndex](const juce::FileChooser& fc){
        if (safeThis == nullptr) return; // component destroyed; ignore callback
        auto& self = *safeThis.getComponent();
        auto f = fc.getResult();
        if (f == juce::File()) return;
        if (self.app.bufferManager.loadAudioFile(padIndex, f, self.formatManager))
        {
            self.statusLabel.setText("Loaded to Pad " + juce::String(padIndex+1) + ": " + f.getFileName(), juce::dontSendNotification);
            self.padGrid.setPadFileName(padIndex, f.getFileNameWithoutExtension());
            while (self.app.settings.padFilePaths.size() < AudioBufferManager::MAX_BUFFERS)
                self.app.settings.padFilePaths.add(juce::String());
            self.app.settings.padFilePaths.set(padIndex, f.getFullPathName());
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
    static const char* partNames[] = { "A", "B", "C", "D" };
    int partIdx = app.getActivePart();
    juce::String partName = juce::String("Part ") + juce::String(partNames[juce::jlimit(0, 3, partIdx)]);
    juce::String base = partName + " | Playing: " + juce::String(playing) + " | BPM " + juce::String(app.settings.bpm, 0);
    if (app.scheduler.isRunning())
    {
        double secUntil = app.scheduler.getSecondsUntilNextTrigger();
        double barsUntil = app.scheduler.getBarsUntilNextTrigger();
        juce::String eta = juce::String(secUntil, 1) + "s / " + juce::String(barsUntil, 2) + " bars";
        statusLabel.setText("Modifiers ON | Next in " + eta + " | " + base, juce::dontSendNotification);
    }
    else
    {
        statusLabel.setText("Modifiers OFF | " + base, juce::dontSendNotification);
    }
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

static int mapQuantizeIdToSubdivision(int id)
{
    switch(id)
    {
        case 1: return 1;  // Bar
        case 2: return 2;  // Half bar
        case 3: return 4;  // Beat (assuming 4/4)
        case 4: return 8;  // 8th
        case 5: return 16; // 16th
        default: return 4;
    }
}

void MainAppComponent::quantizeToggled()
{
    bool enabled = quantizeToggle.getToggleState();
    app.settings.quantizeEnabled = enabled;
    app.scheduler.setQuantizationEnabled(enabled);
    if (enabled)
        quantizeSubdivisionChanged(); // apply current selection
}

void MainAppComponent::quantizeSubdivisionChanged()
{
    int id = quantizeSubdivisionBox.getSelectedId();
    int subdiv = mapQuantizeIdToSubdivision(id);
    app.settings.quantizeSubdivision = subdiv;
    app.scheduler.setQuantizationSubdivision(subdiv);
}

void MainAppComponent::partsCountChanged()
{
    int n = juce::jlimit(1, 4, partsCountBox.getSelectedId());
    app.settings.parts.numParts = n;
    // Clamp active part within new range
    if (app.getActivePart() >= n)
        app.setActivePart(n - 1);
}

void MainAppComponent::saveProjectClicked()
{
    juce::File initialDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("BufferTestProjects");
    initialDir.createDirectory();
    fileChooser = std::make_unique<juce::FileChooser>("Select project folder to save", initialDir, juce::String());
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc){
        auto dir = fc.getResult();
        if (dir == juce::File()) return;
        if (app.projectManager.saveProject(dir, true))
            statusLabel.setText("Project saved to: " + dir.getFullPathName(), juce::dontSendNotification);
        else
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Save Failed", "Could not save project.");
    });
}

void MainAppComponent::loadProjectClicked()
{
    juce::File initialDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("BufferTestProjects");
    initialDir.createDirectory();
    fileChooser = std::make_unique<juce::FileChooser>("Select project file (.json)", initialDir, "*.json");
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc){
        auto file = fc.getResult();
        if (file == juce::File()) return;
        if (app.projectManager.loadProject(file))
        {
            // Apply loaded settings to UI & scheduler
            bpmSlider.setValue(app.settings.bpm, juce::dontSendNotification);
            quantizeToggle.setToggleState(app.settings.quantizeEnabled, juce::dontSendNotification);
            auto subdivisionToId = [](int subdiv)->int{ switch (subdiv) { case 1: return 1; case 2: return 2; case 4: return 3; case 8: return 4; case 16: return 5; default: return 3; } };
            quantizeSubdivisionBox.setSelectedId(subdivisionToId(app.settings.quantizeSubdivision), juce::dontSendNotification);

            bool running = app.scheduler.isRunning();
            if (running) app.scheduler.stop();
            app.scheduler.setQuantizationEnabled(app.settings.quantizeEnabled);
            app.scheduler.setQuantizationSubdivision(app.settings.quantizeSubdivision);
            if (running) app.scheduler.start();

            // Restore pad files
            restorePadFilesFromSettings();

            // Sync parts UI to loaded settings and clamp active part if needed
            {
                int loadedNumParts = juce::jlimit(1, 4, app.settings.parts.numParts);
                partsCountBox.setSelectedId(loadedNumParts, juce::dontSendNotification);
                if (app.getActivePart() >= loadedNumParts)
                    app.setActivePart(loadedNumParts - 1);
            }

            // Update project name UI
            projectNameEditor.setText(app.settings.projectName, juce::dontSendNotification);

            statusLabel.setText("Loaded project: " + file.getFileNameWithoutExtension(), juce::dontSendNotification);
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Load Failed", "Could not load project.");
        }
    });
}

void MainAppComponent::projectNameEdited()
{
    auto raw = projectNameEditor.getText().trim();
    juce::String finalName = raw;
    if (finalName.isEmpty())
        finalName = suggestDefaultProjectName();

    auto sanitized = sanitizeProjectName(finalName);
    if (sanitized != raw)
        projectNameEditor.setText(sanitized, juce::dontSendNotification);

    if (sanitized == app.settings.projectName) return; // no effective change
    if (app.projectManager.renameProject(sanitized))
        statusLabel.setText("Project renamed: " + sanitized, juce::dontSendNotification);
}

juce::String MainAppComponent::sanitizeProjectName(const juce::String& in) const
{
    if (in.isEmpty()) return in;
    static const juce::String invalidChars = "/\\:*?\"<>|"; // cross-platform unsafe set
    juce::String out;
    out.preallocateBytes(in.getNumBytesAsUTF8());
    for (auto c : in)
    {
        if (c < 32) continue; // drop control chars
        if (invalidChars.containsChar(c)) { out << '_'; }
        else { out << c; }
    }
    out = out.trim();
    // Avoid reserved empty or dot names
    if (out.isEmpty() || out == "." || out == "..") out = "Project";
    return out;
}

juce::String MainAppComponent::suggestDefaultProjectName() const
{
    auto now = juce::Time::getCurrentTime();
    return juce::String("Project ") + now.formatted("%Y-%m-%d %H.%M.%S");
}

void MainAppComponent::restorePadFilesFromSettings()
{
    // Iterate through settings.padFilePaths and attempt to load each existing file
    for (int i = 0; i < app.settings.padFilePaths.size() && i < AudioBufferManager::MAX_BUFFERS; ++i)
    {
        auto path = app.settings.padFilePaths[i];
        if (path.isEmpty())
        {
            padGrid.setPadFileName(i, juce::String());
            continue;
        }
        juce::File f(path);
        if (f.existsAsFile())
        {
            if (app.bufferManager.loadAudioFile(i, f, formatManager))
            {
                padGrid.setPadFileName(i, f.getFileNameWithoutExtension());
            }
            else
            {
                // Loading failed despite file existing; show minimal feedback
                padGrid.setPadFileName(i, "[missing]");
            }
        }
        else
        {
            // Missing file; keep path in settings but indicate missing in UI
            padGrid.setPadFileName(i, "[missing]");
        }
    }
}

void MainAppComponent::bpmChanged()
{
    double newBpm = bpmSlider.getValue();
    app.settings.bpm = newBpm; // Direct mutation; scheduler will pick up new bar length for future scheduling
    // Re-sync any tempo-linked LFOs (tremolo, wow/flutter)
    app.resyncTempoLFOs();
    refreshStatus();
}

void MainAppComponent::updatePlaybackModifierLink()
{
    bool anyPlaying = app.bufferManager.getPlayingBufferIndices().size() > 0;
    bool linkEnabled = modifiersToggle.getToggleState();

    if (linkEnabled)
    {
        // Enable modifiers: ensure scheduler active & unsuppressed following playback
        if (anyPlaying && !app.scheduler.isRunning())
            app.scheduler.start();
        // If nothing playing we can leave scheduler running (so progress/predictive cycle persists) or optionally pause.
        // Here we choose to keep it running for continuity, but suppression already false so upcoming rotates.
        app.scheduler.setSuppressed(false);
    }
    else
    {
        // Disable modifiers: keep scheduler running for visual cycle but suppress actual triggers
        if (anyPlaying && !app.scheduler.isRunning())
            app.scheduler.start(); // ensure we still have timeline advancing
        app.scheduler.setSuppressed(true);
    }
    refreshStatus();
}
