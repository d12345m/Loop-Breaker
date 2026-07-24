/*
 ============================================================================== 
   MainAppComponent.cpp
   --------------------------------------------------------------------------
   Initial implementation of new main application UI.
   NOTE: Currently co-exists with legacy MainComponent until feature parity.
 ==============================================================================
*/

#include "MainAppComponent.h"
#include "ThemeEngine.h"

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
    // Project name controls
    projectNameLabel.attachToComponent(&projectNameEditor, true);
    addAndMakeVisible(projectNameEditor);
    projectNameEditor.setText(app.settings.projectName, juce::dontSendNotification);
    projectNameEditor.onFocusLost = [this]{ projectNameEdited(); };
    projectNameEditor.onReturnKey = [this]{ projectNameEdited(); };
    addAndMakeVisible(saveProjectButton); saveProjectButton.onClick = [this]{ saveProjectClicked(); };
    addAndMakeVisible(loadProjectButton); loadProjectButton.onClick = [this]{ loadProjectClicked(); };
    // Parts UI buttons removed; use partsCountBox + status only.

    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::centredLeft);

    // Implemented-only toggle (default on)
    implementedOnlyToggle.setToggleState(true, juce::dontSendNotification);
    implementedOnlyToggle.onClick = [this]{ implementedOnlyToggled(); };
    addAndMakeVisible(implementedOnlyToggle);

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

    // Bars between modifiers slider
    barsBetweenModifiersSlider.setRange(1.0, 16.0, 1.0);
    barsBetweenModifiersSlider.setValue(app.settings.barsBetweenModifiers, juce::dontSendNotification);
    barsBetweenModifiersSlider.onValueChange = [this]{ barsBetweenModifiersChanged(); };
    barsBetweenModifiersSlider.setSliderStyle(juce::Slider::LinearBar);
    barsBetweenModifiersSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);
    // Make the bar itself readable (separate colours for track vs background).
    barsBetweenModifiersSlider.setColour(juce::Slider::backgroundColourId, Theme::panelAlt());
    barsBetweenModifiersSlider.setColour(juce::Slider::trackColourId, Theme::warn());
    barsBetweenModifiersSlider.setColour(juce::Slider::thumbColourId, Theme::warn().brighter(0.2f));
    // Improve readability: blue textbox background with white text.
    barsBetweenModifiersSlider.setColour(juce::Slider::textBoxBackgroundColourId, Theme::accent());
    barsBetweenModifiersSlider.setColour(juce::Slider::textBoxTextColourId, Theme::text());
    barsBetweenModifiersSlider.setColour(juce::Slider::textBoxOutlineColourId, Theme::accent().darker(0.35f));
    // Some LookAndFeels/styles end up with a transparent child Label; force it opaque.
    for (int i = 0; i < barsBetweenModifiersSlider.getNumChildComponents(); ++i)
    {
        if (auto* label = dynamic_cast<juce::Label*>(barsBetweenModifiersSlider.getChildComponent(i)))
        {
            label->setColour(juce::Label::backgroundColourId, Theme::accent());
            label->setColour(juce::Label::textColourId, Theme::text());
            label->setColour(juce::Label::outlineColourId, Theme::accent().darker(0.35f));
            label->setOpaque(true);
        }
    }
    addAndMakeVisible(barsBetweenModifiersSlider);
    barsBetweenModifiersLabel.attachToComponent(&barsBetweenModifiersSlider, true);
    addAndMakeVisible(barsBetweenModifiersLabel);

    attachPadCallbacks();

    // Provide AudioFormatManager to padGrid so thumbnails can read files
    padGrid.setAudioFormatManager(&formatManager);

    modifierSelectionPanel.setForceSelectionCallback([this](ModifierType type){
        app.scheduler.forceUpcomingModifier(type);
    });
    modifierSelectionPanel.setForceVariantCallback([this](ModifierType type, const juce::String& variant){
        app.scheduler.forceUpcomingVariant(type, variant);
    });

    // Listen for scheduler callbacks
    app.scheduler.addListener(this);

    if (auto up = app.scheduler.getUpcomingModifier())
        modifierDisplay.setUpcoming(*up);

    setAudioChannels(0, 2);
    setSize(920, 600);

    startTimerHz(20); // 50ms UI refresh - lower overhead to avoid blocking audio thread

    // Apply initial scheduler settings from SessionSettings
    app.scheduler.setQuantizationEnabled(app.settings.quantizeEnabled);
    app.scheduler.setQuantizationSubdivision(app.settings.quantizeSubdivision);

    // Initialize status text immediately
    refreshStatus();

    // If there are existing pad file paths in settings (e.g., from a previous run), prime thumbnails
    if (app.settings.padFilePaths.size() > 0)
        padGrid.setPadFilePaths(app.settings.padFilePaths);

    // Removed hardcoded test auto-load; rely solely on SessionSettings restore and user loads
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
    g.fillAll(Theme::bg());

    g.setColour(Theme::border());
    g.drawRect(getLocalBounds(), 1);
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
    // partsCountLabel attached to partsCountBox, no explicit bounds needed
    // Moved status label to the second row to avoid overlap and ensure visibility

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
    // Bars between modifiers slider
    barsBetweenModifiersSlider.setBounds(projArea.removeFromLeft(240).reduced(2));
    // Label is attached to partsCountBox; no explicit bounds needed
    // Parts buttons removed; reclaim space for other controls.
    // Status label occupies the remaining right-side space on this row
    statusLabel.setBounds(projArea.reduced(2));
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
    // Ensure UI update happens on the message thread
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
        && juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        modifierDisplay.setUpcoming(desc);
    }
    else
    {
        auto descCopy = desc; // copy for async lambda capture
        juce::MessageManager::callAsync([this, descCopy]
        {
            modifierDisplay.setUpcoming(descCopy);
        });
    }
}

void MainAppComponent::modifierTriggered(const ModifierDescriptor& desc, const juce::Array<int>& targets)
{
    // Apply pending parts count change if one is queued
    if (pendingPartsCount >= 1 && pendingPartsCount <= 4)
    {
        app.settings.parts.numParts = pendingPartsCount;
        app.setActivePart(app.getActivePart());
        pendingPartsCount = -1;
    }

    auto doUiWork = [this](const ModifierDescriptor& d, const juce::Array<int>& t)
    {
        juce::String targetStr;
        if (t.isEmpty()) targetStr = "(master/global)"; else {
            for (int i = 0; i < t.size(); ++i) targetStr << (i?",":"") << (t[i]+1);
        }
        juce::String details = d.description.isNotEmpty() ? (" | " + d.description) : juce::String();
        statusLabel.setText("Triggered: " + d.shortName + " -> " + targetStr + details, juce::dontSendNotification);
        padGrid.flashPads(t);
        syncModifierStickers();

        switch (d.type)
        {
            case ModifierType::ResetAll:
                padGrid.clearModifierStickers(t);
                syncModifierStickers();
                break;
            case ModifierType::SwitchPart:
                padGrid.showTransientModifierSticker(d.type, {}, 900.0);
                break;
            case ModifierType::QuarterNoteBurst:
                padGrid.showTransientModifierSticker(
                    d.type, {},
                    1000.0 * d.plannedBurstBars.value_or(1)
                        * app.settings.getSecondsPerBar());
                break;
            case ModifierType::SwapModifierStack:
                padGrid.showTransientModifierSticker(d.type, t, 1100.0);
                break;
            default:
                break;
        }

        modifierHistory.addEntry(d, t);
        if (!t.isEmpty())
        {
            padGrid.clearSelections();
        }
    };

    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
        && juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        doUiWork(desc, targets);
    }
    else
    {
        auto descCopy = desc;
        auto targetsCopy = targets;
        juce::MessageManager::callAsync([this, descCopy, targetsCopy, doUiWork]() mutable
        {
            doUiWork(descCopy, targetsCopy);
        });
    }
}

void MainAppComponent::timerCallback()
{
    refreshStatus();
    const auto playingIndices = app.bufferManager.getPlayingBufferIndices();
    padGrid.setPlayingStates(playingIndices);
    syncModifierStickers();
    
    // Update per-pad playheads and loop windows for waveform overlays
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
    
    // Repaint pad grid for smooth playhead animation
    padGrid.repaint();
    
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
    refreshStatus();
}

void MainAppComponent::stopAllClicked()
{
    app.bufferManager.stopAll();
    // Also stop and reset the scheduler timeline so beats/measures reset
    if (app.scheduler.isRunning()) app.scheduler.stop();
    app.scheduler.resetTimeline();
    updatePlaybackModifierLink();
}

void MainAppComponent::modifiersToggleChanged()
{
    updatePlaybackModifierLink();
}

void MainAppComponent::updatePadSelectionTargets()
{
    auto selected = padGrid.getSelectedPadIndices();
    app.scheduler.setUserSelectedBuffers(selected);
}

void MainAppComponent::syncModifierStickers()
{
    for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
        padGrid.setModifierStickerMask(
            i, app.getActiveModifierStickerMask(i));
}

void MainAppComponent::refreshStatus()
{
    // Could display playing buffer count
    int playing = app.bufferManager.getPlayingBufferIndices().size();
    static const char* partNames[] = { "A", "B", "C", "D" };
    int partIdx = app.getActivePart();
    juce::String partName = juce::String("Part ") + juce::String(partNames[juce::jlimit(0, 3, partIdx)]);
    const int numParts = app.settings.parts.getNumParts();
    // Show span in bars. Prefer a currently playing buffer; fall back to first loaded.
    double startBars = 0.0, endBars = 0.0;
    int bufferForSpan = -1;
    auto playingIdxs = app.bufferManager.getPlayingBufferIndices();
    if (playingIdxs.size() > 0)
        bufferForSpan = playingIdxs[0];
    else
    {
        auto loaded = app.bufferManager.getLoadedBufferIndices();
        if (loaded.size() > 0)
            bufferForSpan = loaded[0];
    }
    if (bufferForSpan >= 0)
    {
        auto spanBars = app.getActivePartSpanBarsForBuffer(bufferForSpan);
        startBars = spanBars.first;
        endBars = spanBars.second;
    }
    juce::String spanStr = juce::String(startBars, 2) + " bars-" + juce::String(endBars, 2) + " bars";
    juce::String partsStr = juce::String(numParts);
    if (pendingPartsCount >= 1 && pendingPartsCount <= 4)
        partsStr << " (pending: " << pendingPartsCount << ")";

    juce::String base = partName + " [" + spanStr + "] | Parts: " + partsStr
        + " | Bars/Mod: " + juce::String(app.settings.barsBetweenModifiers)
        + " | Playing: " + juce::String(playing) + " | BPM " + juce::String(app.settings.bpm, 0);
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

void MainAppComponent::partsCountChanged()
{
    int n = juce::jlimit(1, 4, partsCountBox.getSelectedId());
    
    // If transport is running (scheduler active or buffers playing), defer the change until next modifier
    if (isTransportRunning())
    {
        pendingPartsCount = n;
        statusLabel.setText(juce::String("Parts change pending: ") + juce::String(n) + " part" + (n > 1 ? "s" : ""), juce::dontSendNotification);
    }
    else
    {
        // Apply immediately - just update the settings, don't call setActivePart yet
        app.settings.parts.numParts = n;
        pendingPartsCount = -1;
        statusLabel.setText(juce::String("Parts set to: ") + juce::String(n) + " part" + (n > 1 ? "s" : ""), juce::dontSendNotification);
    }
}

void MainAppComponent::barsBetweenModifiersChanged()
{
    int bars = (int) barsBetweenModifiersSlider.getValue();
    app.settings.barsBetweenModifiers = juce::jlimit(1, 32, bars);
}

bool MainAppComponent::isTransportRunning() const
{
    // Transport is running if scheduler is active OR any buffers are playing
    if (app.scheduler.isRunning() && !app.scheduler.isSuppressed())
        return true;
    
    // Also check if any buffers are currently playing
    auto playing = app.bufferManager.getPlayingBufferIndices();
    return !playing.isEmpty();
}

void MainAppComponent::saveProjectClicked()
{
    juce::File initialDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("LoopBreakerProjects");
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
    juce::File initialDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("LoopBreakerProjects");
    initialDir.createDirectory();
    fileChooser = std::make_unique<juce::FileChooser>("Select project file (.json)", initialDir, "*.json");
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc){
        auto file = fc.getResult();
        if (file == juce::File()) return;
        if (app.projectManager.loadProject(file))
        {
            // Clean slate: stop playback, clear audio buffers and UI pad names, reset FX state
            app.bufferManager.stopAll();
            app.bufferManager.clearAllBuffers();
            for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
                padGrid.setPadFileName(i, juce::String());
            // Reset channel strips (FX) to defaults to avoid lingering state
            for (int i = 0; i < app.channelStrips.size(); ++i)
                app.channelStrips[i]->reset();

            // Apply loaded settings to UI & scheduler
            bpmSlider.setValue(app.settings.bpm, juce::dontSendNotification);

            // Quantization UI was removed; still keep scheduler in sync with persisted settings.
            const bool running = app.scheduler.isRunning();
            if (running) app.scheduler.stop();
            app.scheduler.setQuantizationEnabled(app.settings.quantizeEnabled);
            app.scheduler.setQuantizationSubdivision(app.settings.quantizeSubdivision);
            if (running) app.scheduler.start();

            // Restore pad files
            restorePadFilesFromSettings();
            // Push file paths to padGrid thumbnails
            if (app.settings.padFilePaths.size() > 0)
                padGrid.setPadFilePaths(app.settings.padFilePaths);

            // Sync parts UI to loaded settings and clamp active part if needed
            {
                int loadedNumParts = juce::jlimit(1, 4, app.settings.parts.numParts);
                partsCountBox.setSelectedId(loadedNumParts, juce::dontSendNotification);
                // Re-apply active part so loop windows are recomputed immediately.
                app.setActivePart(app.getActivePart());
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
            // Clear thumbnail as well
            padGrid.setPadFilePath(i, juce::String());
            continue;
        }
        juce::File f(path);
        if (f.existsAsFile())
        {
            // Always set the thumbnail to the file path; even if audio load fails, user can spot the file visually
            padGrid.setPadFilePath(i, f.getFullPathName());
            bool ok = app.bufferManager.loadAudioFile(i, f, formatManager);
            padGrid.setPadFileName(i, ok ? f.getFileNameWithoutExtension() : f.getFileNameWithoutExtension());
        }
        else
        {
            // Missing file; keep path in settings but indicate missing in UI
            padGrid.setPadFileName(i, "[missing]");
            padGrid.setPadFilePath(i, juce::String());
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
