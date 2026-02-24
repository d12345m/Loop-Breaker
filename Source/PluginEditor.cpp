#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "HelpPanelContent.h"
#include "UpcomingModifierDisplay.h"
#include "PadGridComponent.h"
#include "ModifierHistoryPanel.h"
#include "ModifierSelectionPanel.h"
#include "FxStatusPanel.h"
#include "TearingDebugPanel.h"
#include "ModifierProbabilityPanel.h"
#include "DebugPanelContent.h"
#include "Theme.h"

namespace
{
class HipLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    HipLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, Theme::bg());
        setColour(juce::ComboBox::backgroundColourId, Theme::panel());
        setColour(juce::ComboBox::outlineColourId, Theme::borderStrong());
        setColour(juce::ComboBox::textColourId, Theme::text());
        setColour(juce::ComboBox::arrowColourId, Theme::textSubtle());

        setColour(juce::TextButton::buttonColourId, Theme::panelAlt());
        setColour(juce::TextButton::buttonOnColourId, Theme::panelAlt());
        setColour(juce::TextButton::textColourOffId, Theme::text());
        setColour(juce::TextButton::textColourOnId, Theme::text());

        setColour(juce::ToggleButton::textColourId, Theme::text());
        setColour(juce::ToggleButton::tickColourId, Theme::accent());
        setColour(juce::ToggleButton::tickDisabledColourId, Theme::borderStrong());

        setColour(juce::ScrollBar::thumbColourId, Theme::borderStrong());
        setColour(juce::PopupMenu::backgroundColourId, Theme::panel());
        setColour(juce::PopupMenu::highlightedBackgroundColourId, Theme::accent().withAlpha(0.12f));
        setColour(juce::PopupMenu::textColourId, Theme::text());
        setColour(juce::PopupMenu::highlightedTextColourId, Theme::text());
    }
};

class PluginEditorContent final : public juce::Component,
                                 public ModifierSchedulerListener,
                                 private juce::Timer
{
public:
    explicit PluginEditorContent (BufferTestAudioProcessor& p,
                                  ModifierHistoryPanel* historyPanel = nullptr)
        : processor(p),
          app(p.getAppState()),
          externalModifierHistory(historyPanel)
    {
        setLookAndFeel(&hipLnf);

        addAndMakeVisible(modifierDisplay);
        addAndMakeVisible(padGrid);

        addAndMakeVisible(modifiersToggle);
        modifiersToggle.setToggleState(app.settings.modifiersEnabled, juce::dontSendNotification);
        modifiersToggle.onClick = [this]{ modifiersToggleChanged(); };

        // Parts count selector
        addAndMakeVisible(partsCountBox);
        partsCountBox.addItem("1 part", 1);
        partsCountBox.addItem("2 parts", 2);
        partsCountBox.addItem("3 parts", 3);
        partsCountBox.addItem("4 parts", 4);
        {
            int initialParts = app.settings.parts.getNumParts();
            if (initialParts < 1 || initialParts > 4) initialParts = 1;
            partsCountBox.setSelectedId(initialParts, juce::dontSendNotification);
        }
        partsCountBox.onChange = [this]{ partsCountChanged(); };

        // Bars between modifiers slider
        addAndMakeVisible(barsBetweenModifiersSlider);
        barsBetweenModifiersSlider.setRange(1.0, 16.0, 1.0);
        barsBetweenModifiersSlider.setValue(app.settings.barsBetweenModifiers, juce::dontSendNotification);
        barsBetweenModifiersSlider.onValueChange = [this]{ barsBetweenModifiersChanged(); };
        barsBetweenModifiersSlider.setSliderStyle(juce::Slider::LinearBar);
        barsBetweenModifiersSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 20);
        // Make the bar itself readable (separate colours for track vs background).
        barsBetweenModifiersSlider.setColour(juce::Slider::backgroundColourId, Theme::panelAlt());
        barsBetweenModifiersSlider.setColour(juce::Slider::trackColourId, Theme::warn());
        barsBetweenModifiersSlider.setColour(juce::Slider::thumbColourId, Theme::warn().brighter(0.2f));
        // Improve readability: blue textbox background with white text.
        barsBetweenModifiersSlider.setColour(juce::Slider::textBoxBackgroundColourId, Theme::accent());
        barsBetweenModifiersSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        barsBetweenModifiersSlider.setColour(juce::Slider::textBoxOutlineColourId, Theme::accent().darker(0.35f));
        // Some LookAndFeels/styles end up with a transparent child Label; force it opaque.
        for (int i = 0; i < barsBetweenModifiersSlider.getNumChildComponents(); ++i)
        {
            if (auto* label = dynamic_cast<juce::Label*>(barsBetweenModifiersSlider.getChildComponent(i)))
            {
                label->setColour(juce::Label::backgroundColourId, Theme::accent());
                label->setColour(juce::Label::textColourId, juce::Colours::white);
                label->setColour(juce::Label::outlineColourId, Theme::accent().darker(0.35f));
                label->setOpaque(true);
            }
        }
        addAndMakeVisible(barsBetweenModifiersLabel);
        barsBetweenModifiersLabel.attachToComponent(&barsBetweenModifiersSlider, true);

        // Master volume knob
        addAndMakeVisible(masterVolumeSlider);
        masterVolumeSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        masterVolumeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 54, 16);
        masterVolumeSlider.setColour(juce::Slider::rotarySliderFillColourId, Theme::accent());
        masterVolumeSlider.setColour(juce::Slider::rotarySliderOutlineColourId, Theme::panelAlt());
        masterVolumeSlider.setColour(juce::Slider::thumbColourId, Theme::accent().brighter(0.2f));
        masterVolumeSlider.setColour(juce::Slider::textBoxBackgroundColourId, Theme::panelAlt());
        masterVolumeSlider.setColour(juce::Slider::textBoxTextColourId, Theme::text());
        masterVolumeSlider.setColour(juce::Slider::textBoxOutlineColourId, Theme::border());
        masterVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.getAPVTS(), "masterVolume", masterVolumeSlider);
        addAndMakeVisible(masterVolumeLabel);
        masterVolumeLabel.setJustificationType(juce::Justification::centred);
        masterVolumeLabel.setColour(juce::Label::textColourId, Theme::textSubtle());
        masterVolumeLabel.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        masterVolumeLabel.attachToComponent(&masterVolumeSlider, false);

        addAndMakeVisible(statusLabel);
        statusLabel.setJustificationType(juce::Justification::centredLeft);
        statusLabel.setColour(juce::Label::textColourId, Theme::textSubtle());

        addAndMakeVisible(hostTransportLabel);
        hostTransportLabel.setJustificationType(juce::Justification::centredRight);
        hostTransportLabel.setColour(juce::Label::textColourId, Theme::textSubtle());

        padGrid.setAudioFormatManager(&processor.getFormatManager());
        attachPadCallbacks();

        padGrid.setFilesDroppedOnPadCallback([this](int startPadIndex, const juce::StringArray& files)
        {
            loadDroppedFiles(startPadIndex, files);
        });

        // MIDI learn callbacks
        padGrid.onMidiLearnRequest = [this](int padIndex)
        {
            // If clicking on the pad already in learn mode, cancel learn mode
            if (processor.isMidiLearnEnabled() && processor.getMidiLearnPadIndex() == padIndex)
            {
                processor.setMidiLearnMode(false, -1);
                padGrid.setMidiLearnForPad(padIndex, false);
                return;
            }
            
            // If another pad is already in learn mode, clear it first
            if (processor.isMidiLearnEnabled())
            {
                const int previousLearnPad = processor.getMidiLearnPadIndex();
                if (previousLearnPad >= 0 && previousLearnPad < 8 && previousLearnPad != padIndex)
                {
                    padGrid.setMidiLearnForPad(previousLearnPad, false);
                }
            }
            
            processor.setMidiLearnMode(true, padIndex);
            padGrid.setMidiLearnForPad(padIndex, true);
        };

        padGrid.onClearMidiNote = [this](int padIndex)
        {
            app.settings.midiNoteMap[padIndex] = -1;
            padGrid.setMidiNoteForPad(padIndex, -1);
        };

        padGrid.onClearSample = [this](int padIndex)
        {
            // Clear the buffer
            app.bufferManager.clearBuffer(padIndex);
            
            // Clear the stored file path
            ensurePadFilePathsSized();
            app.settings.padFilePaths.set(padIndex, {});
            padGrid.setPadFilePath(padIndex, {});
        };

        padGrid.onLoadSampleRequest = [this](int padIndex)
        {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Load sample for Pad " + juce::String(padIndex + 1),
                juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                "*.wav;*.aif;*.aiff;*.flac;*.mp3");

            fileChooser->launchAsync(
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this, padIndex](const juce::FileChooser& fc)
                {
                    auto result = fc.getResult();
                    if (result.existsAsFile())
                        loadFileIntoPad(padIndex, result);
                });
        };

        // Initialize pad MIDI notes from settings
        for (int i = 0; i < 8; ++i)
            padGrid.setMidiNoteForPad(i, app.settings.midiNoteMap[i]);

        app.scheduler.addListener(this);

        app.scheduler.setQuantizationEnabled(app.settings.quantizeEnabled);
        app.scheduler.setQuantizationSubdivision(app.settings.quantizeSubdivision);

        if (app.settings.padFilePaths.size() > 0)
            padGrid.setPadFilePaths(app.settings.padFilePaths);

        refreshStatus();
        refreshHostTransportReadout();
        startTimerHz(20); // 50ms UI refresh - lower overhead
    }

    ~PluginEditorContent() override
    {
        stopTimer();
        app.scheduler.removeListener(this);
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Theme::bg());

        g.setColour(Theme::border());
        g.drawRect(getLocalBounds(), 1);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto topBar = area.removeFromTop(60);
        modifierDisplay.setBounds(topBar.removeFromLeft(topBar.getWidth() * 0.5f).reduced(4));

        auto controlBar = topBar;
        modifiersToggle.setBounds(controlBar.removeFromLeft(120).reduced(2));
        partsCountBox.setBounds(controlBar.removeFromLeft(120).reduced(2));
        barsBetweenModifiersSlider.setBounds(controlBar.removeFromLeft(220).reduced(2));

        auto rightRegion = controlBar;
        masterVolumeSlider.setBounds(rightRegion.removeFromRight(64).reduced(2));

        auto row2 = area.removeFromTop(28).reduced(2);
        hostTransportLabel.setBounds(row2.removeFromRight(240).reduced(2));
        statusLabel.setBounds(row2.reduced(2));

        area.removeFromTop(6);
        padGrid.setBounds(area);
    }

    // ModifierSchedulerListener
    void upcomingModifierChanged(const ModifierDescriptor& desc) override
    {
        auto descCopy = desc;
        juce::MessageManager::callAsync([this, descCopy]
        {
            modifierDisplay.setUpcoming(descCopy);
        });
    }

    void modifierTriggered(const ModifierDescriptor& desc, const juce::Array<int>& targets) override
    {
        juce::MessageManager::callAsync([this, desc, targets]
        {
            // Apply pending parts count change exactly on the next modifier trigger.
            if (pendingPartsCount >= 1 && pendingPartsCount <= 4)
            {
                app.settings.parts.numParts = pendingPartsCount;
                app.setActivePart(app.getActivePart());
                pendingPartsCount = -1;
            }
            if (externalModifierHistory != nullptr)
                externalModifierHistory->addEntry(desc, targets);
            padGrid.flashPads(targets);
            if (! targets.isEmpty())
                padGrid.clearSelections();
            refreshStatus();
        });
    }

private:
    BufferTestAudioProcessor& processor;
    AppState& app;

    HipLookAndFeel hipLnf;

    UpcomingModifierDisplay modifierDisplay;
    PadGridComponent padGrid;

    ModifierHistoryPanel* externalModifierHistory = nullptr;

    juce::ToggleButton modifiersToggle { "Modifiers" };

    juce::ComboBox partsCountBox;
    int pendingPartsCount = -1; // -1 = none; otherwise apply on next modifier trigger
    juce::Slider barsBetweenModifiersSlider;
    juce::Label barsBetweenModifiersLabel { {}, "Bars/Mod" };
    juce::Label statusLabel { {}, "Status: Idle" };
    juce::Label hostTransportLabel { {}, "Host: Unknown" };

    juce::Slider masterVolumeSlider;
    juce::Label masterVolumeLabel { {}, "Vol" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterVolumeAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

    bool padPathsSynced = false; // true once pad file paths have been synced to the UI after state restore

    void refreshHostTransportReadout()
    {
        const auto state = processor.getLastHostTransportState();
        const auto source = processor.getLastHostTransportSource();

        juce::String s;
        s << "Host: ";

        switch (state)
        {
            case BufferTestAudioProcessor::HostTransportState::Playing: s << "Playing"; break;
            case BufferTestAudioProcessor::HostTransportState::Stopped: s << "Stopped"; break;
            default: s << "Unknown"; break;
        }

        s << " (";
        switch (source)
        {
            case BufferTestAudioProcessor::HostTransportSource::Reported: s << "reported"; break;
            case BufferTestAudioProcessor::HostTransportSource::Inferred: s << "inferred"; break;
            default: s << "unknown"; break;
        }
        s << ")";

        if (! processor.isPlaybackEnabled())
            s << " | gated";

        hostTransportLabel.setText(s, juce::dontSendNotification);
    }

    void ensurePadFilePathsSized()
    {
        while (app.settings.padFilePaths.size() < AudioBufferManager::MAX_BUFFERS)
            app.settings.padFilePaths.add({});
    }

    void loadFileIntoPad(int padIndex, const juce::File& file)
    {
        if (! juce::isPositiveAndBelow(padIndex, AudioBufferManager::MAX_BUFFERS))
            return;

        if (! file.existsAsFile())
            return;

        const bool scheduled = app.bufferManager.requestLoadAudioFile(padIndex, file);
        if (! scheduled)
            return;

        ensurePadFilePathsSized();
        app.settings.padFilePaths.set(padIndex, file.getFullPathName());
        padGrid.setPadFilePath(padIndex, file.getFullPathName());
    }

    void loadDroppedFiles(int startPadIndex, const juce::StringArray& files)
    {
        if (! juce::isPositiveAndBelow(startPadIndex, AudioBufferManager::MAX_BUFFERS))
            return;

        int padIndex = startPadIndex;
        for (const auto& path : files)
        {
            if (padIndex >= AudioBufferManager::MAX_BUFFERS)
                break;

            loadFileIntoPad(padIndex, juce::File(path));
            ++padIndex;
        }
    }

    void timerCallback() override
    {
        // Lightweight UI refresh only; timing is driven by the audio thread in the processor.
        refreshStatus();
        refreshHostTransportReadout();
        padGrid.setPlayingStates(app.bufferManager.getPlayingBufferIndices());

        // Poll for MIDI pad toggle requests (from audio thread)
        for (int i = 0; i < 8; ++i)
        {
            if (processor.checkAndClearMidiToggle(i))
            {
                padGrid.togglePadSelection(i);
            }
        }

        // Poll for MIDI learn completion
        // Check for learned note even if learn mode was disabled by audio thread
        const int learnedNote = processor.checkAndClearLearnedNote();
        if (learnedNote >= 0)
        {
            DBG("UI received learned note: " + juce::String(learnedNote));
            const int padIndex = processor.getMidiLearnPadIndex();
            if (padIndex >= 0 && padIndex < 8)
            {
                DBG("Assigning note " + juce::String(learnedNote) + " to pad " + juce::String(padIndex));
                app.settings.midiNoteMap[padIndex] = learnedNote;
                padGrid.setMidiNoteForPad(padIndex, learnedNote);
                padGrid.setMidiLearnForPad(padIndex, false);
                processor.setMidiLearnMode(false, -1);
            }
        }

        // Sync pad file paths and MIDI notes from settings.
        // This covers the case where setStateInformation is called after the
        // editor was created (some DAWs restore state lazily).
        if (! padPathsSynced)
        {
            bool anyPathSet = false;
            for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
            {
                if (i < app.settings.padFilePaths.size() && app.settings.padFilePaths[i].isNotEmpty())
                {
                    anyPathSet = true;
                    break;
                }
            }

            // Wait until paths are populated AND at least one buffer has finished loading
            bool anyBufferLoaded = false;
            for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
            {
                if (auto* b = app.bufferManager.getBuffer(i); b && b->hasAudioLoaded())
                {
                    anyBufferLoaded = true;
                    break;
                }
            }

            if (anyPathSet && anyBufferLoaded)
            {
                padGrid.setPadFilePaths(app.settings.padFilePaths);
                for (int i = 0; i < 8; ++i)
                    padGrid.setMidiNoteForPad(i, app.settings.midiNoteMap[i]);
                // Sync UI controls to restored settings
                modifiersToggle.setToggleState(app.settings.modifiersEnabled, juce::dontSendNotification);
                {
                    int loadedParts = juce::jlimit(1, 4, app.settings.parts.getNumParts());
                    partsCountBox.setSelectedId(loadedParts, juce::dontSendNotification);
                }
                barsBetweenModifiersSlider.setValue(app.settings.barsBetweenModifiers, juce::dontSendNotification);
                padPathsSynced = true;
            }
        }

        // Update modifier countdown/progress (driven by scheduler host timeline when available).
        if (app.scheduler.isRunning())
        {
            modifierDisplay.setCountdown(app.scheduler.getSecondsUntilNextTrigger(),
                                         app.scheduler.getBarsUntilNextTrigger(),
                                         app.scheduler.getProgressToNextTrigger());
        }
        else
        {
            modifierDisplay.setCountdown(0.0, 0.0, 0.0);
        }

        // Show paused styling when modifiers are disabled.
        modifierDisplay.setSuppressed((! app.settings.modifiersEnabled) || app.scheduler.isSuppressed());

        // Update playhead positions for waveform display (only when not playing).
        for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
        {
            if (auto* b = app.bufferManager.getBuffer(i); b && b->hasAudioLoaded())
            {
                padGrid.setTotalSamplesForPad(i, (double) b->getDurationInSamples());
                padGrid.setPlayheadForPad(i, (double) b->getPlayheadPositionInSamples());
                padGrid.setLoopWindowForPad(i,
                                           b->isLoopWindowEnabled(),
                                           (double) b->getLoopStartSamples(),
                                           (double) b->getLoopEndSamples());
            }
        }
        
        // Repaint for smooth playhead animation
        padGrid.repaint();
    }

    void attachPadCallbacks()
    {
        padGrid.setSelectionChangedCallback([this]{ updatePadSelectionTargets(); });
        updatePadSelectionTargets();
    }

    void updatePadSelectionTargets()
    {
        app.scheduler.setUserSelectedBuffers(padGrid.getSelectedPadIndices());
        refreshStatus();
    }

    void modifiersToggleChanged()
    {
        const bool enabled = modifiersToggle.getToggleState();
        app.settings.modifiersEnabled = enabled;
        if (enabled)
        {
            if (! app.scheduler.isRunning())
                app.scheduler.start();
        }
        else
        {
            if (app.scheduler.isRunning())
                app.scheduler.stop();
        }
        refreshStatus();
    }

    void partsCountChanged()
    {
        const int n = juce::jlimit(1, 4, partsCountBox.getSelectedId());

        // If the DAW transport is stopped, apply immediately.
        // This matches user expectation: edits while stopped should take effect now.
        if (processor.getLastHostTransportState() == BufferTestAudioProcessor::HostTransportState::Stopped)
        {
            app.settings.parts.numParts = n;
            app.setActivePart(app.getActivePart());
            pendingPartsCount = -1;
            refreshStatus();
            return;
        }

        // If transport is running (host or internal playback), defer until next modifier trigger.
        if (isTransportRunning())
        {
            pendingPartsCount = n;
            refreshStatus();
            return;
        }

        // Not running: store the setting only. Loop windows will be recomputed when playback starts
        // or on the next modifier trigger if modifiers are enabled.
        app.settings.parts.numParts = n;
        pendingPartsCount = -1;
        refreshStatus();
    }

    void barsBetweenModifiersChanged()
    {
        const int bars = (int) barsBetweenModifiersSlider.getValue();
        app.settings.barsBetweenModifiers = juce::jlimit(1, 16, bars);
        refreshStatus();
    }

    bool isTransportRunning() const
    {
        // If scheduler is actively ticking modifiers, treat as running.
        if (app.scheduler.isRunning() && ! app.scheduler.isSuppressed() && app.settings.modifiersEnabled)
            return true;

        // If any buffers are playing, treat as running.
        if (! app.bufferManager.getPlayingBufferIndices().isEmpty())
            return true;

        // Fall back to host transport state (DAW).
        if (processor.getLastHostTransportState() == BufferTestAudioProcessor::HostTransportState::Playing
            && processor.isPlaybackEnabled())
            return true;

        return false;
    }



    void refreshStatus()
    {
        juce::String s;
        s << (app.scheduler.isRunning() ? "Running" : "Stopped");
        s << " | Selected: " << padGrid.getSelectedPadIndices().size();
        static const char* partNames[] = { "A", "B", "C", "D" };
        const int numParts = app.settings.parts.getNumParts();
        const int active = juce::jlimit(0, 3, app.getActivePart());
        s << " | Parts: " << numParts;
        if (pendingPartsCount >= 1 && pendingPartsCount <= 4)
            s << " (pending: " << pendingPartsCount << ")";
        s << " | Active: " << partNames[active];
        s << " | Bars/Mod: " << app.settings.barsBetweenModifiers;
        statusLabel.setText("Status: " + s, juce::dontSendNotification);
    }
};
} // namespace

BufferTestAudioProcessorEditor::BufferTestAudioProcessorEditor (BufferTestAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    // Create debug panel first so we can pass its history panel to the session content
    debugPanel = std::make_unique<DebugPanelContent>(processor.getAppState());

    content = std::make_unique<PluginEditorContent>(processor,
                                                    &debugPanel->getModifierHistory());

    probabilityPanel = std::make_unique<ModifierProbabilityPanel>(
        processor.getAppState().settings.modifierProbabilities,
        processor.getAPVTS());

    tabComponent = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    auto tabBg = Theme::bg().brighter(0.05f);
    helpPanel = std::make_unique<HelpPanelContent>();

    tabComponent->addTab("Session",   tabBg, content.get(), false);
    tabComponent->addTab("Modifiers", tabBg, probabilityPanel.get(), false);
    tabComponent->addTab("Debug",     tabBg, debugPanel.get(), false);
    tabComponent->addTab("Help",      tabBg, helpPanel.get(), false);

    // Style the tab bar
    auto& tabBar = tabComponent->getTabbedButtonBar();
    tabBar.setColour(juce::TabbedButtonBar::tabOutlineColourId, Theme::border());
    tabBar.setColour(juce::TabbedButtonBar::frontOutlineColourId, Theme::accent());

    addAndMakeVisible(tabComponent.get());

    // Moonbase: enable update badge and disable analytics
    if (activationUI)
        activationUI->enableUpdateBadge();

    if (processor.moonbaseClient)
        processor.moonbaseClient->setTransmitAnalytics (false, false);

    setSize (1200, 800);
    setResizable(true, true);
    setResizeLimits(920, 600, 2400, 1600);
}

BufferTestAudioProcessorEditor::~BufferTestAudioProcessorEditor() = default;

void BufferTestAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll(Theme::bg());
}

void BufferTestAudioProcessorEditor::resized()
{
    if (tabComponent)
        tabComponent->setBounds(getLocalBounds());

    MOONBASE_RESIZE_ACTIVATION_UI
}
