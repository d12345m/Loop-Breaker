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
#include "ThemeEngine.h"
#include "ThemeFonts.h"
#include "ThemeLookAndFeel.h"
#include "SettingsPanelContent.h"
#include "BackgroundAnimator.h"
#include "PresetBarComponent.h"

namespace
{

class PluginEditorContent final : public juce::Component,
                                 public ModifierSchedulerListener,
                                 public ThemeListener,
                                 private juce::Timer
{
public:
    BackgroundAnimator* bgAnimator = nullptr;

    explicit PluginEditorContent (BufferTestAudioProcessor& p,
                                  ModifierHistoryPanel* historyPanel = nullptr)
        : processor(p),
          app(p.getAppState()),
          externalModifierHistory(historyPanel)
    {
        setLookAndFeel(&hipLnf);
        ThemeEngine::getInstance().addListener(this);

        addAndMakeVisible(modifierDisplay);
        addAndMakeVisible(padGrid);

        addAndMakeVisible(modifiersToggle);
        modifiersToggle.setToggleState(app.settings.modifiersEnabled, juce::dontSendNotification);
        modifiersToggle.onClick = [this]{ modifiersToggleChanged(); };
        modifiersToggle.addMouseListener(this, false);

        // Parts count selector (lives in Settings tab; created here for state management)
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

        // Bars between modifiers slider (lives in Settings tab; created here for state management)
        barsBetweenModifiersSlider.setRange(1.0, 16.0, 1.0);
        barsBetweenModifiersSlider.setValue(app.settings.barsBetweenModifiers, juce::dontSendNotification);
        barsBetweenModifiersSlider.onValueChange = [this]{ barsBetweenModifiersChanged(); };
        barsBetweenModifiersSlider.setSliderStyle(juce::Slider::LinearBar);
        barsBetweenModifiersSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 20);
        barsBetweenModifiersLabel.setJustificationType(juce::Justification::centred);
        barsBetweenModifiersLabel.setFont(ThemeFonts::getInstance().controlLabelFont(14.0f));

        // Master volume knob
        addAndMakeVisible(masterVolumeSlider);
        masterVolumeSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        masterVolumeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 54, 16);
        masterVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.getAPVTS(), "masterVolume", masterVolumeSlider);
        addAndMakeVisible(masterVolumeLabel);
        masterVolumeLabel.setJustificationType(juce::Justification::centred);
        masterVolumeLabel.setFont(ThemeFonts::getInstance().controlLabelFont(15.0f));

        addAndMakeVisible(statusLabel);
        statusLabel.setJustificationType(juce::Justification::centredLeft);
        statusLabel.setFont(ThemeFonts::getInstance().monoFont(15.0f));

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

        // ── Preset bar (A–D) ──
        addAndMakeVisible(presetBar);
        for (int i = 0; i < 4; ++i)
        {
            presetBar.setSlotOccupied(i, app.presetBank.isSlotOccupied(i));
            presetBar.setMidiNote(i, app.settings.presetMidiNoteMap[static_cast<size_t>(i)]);
        }

        presetBar.onSavePreset = [this](int slot)
        {
            app.capturePreset(slot);
            presetBar.setSlotOccupied(slot, true);
        };

        presetBar.onRecallPreset = [this](int slot)
        {
            app.restorePreset(slot);
        };

        presetBar.onClearPreset = [this](int slot)
        {
            app.presetBank.clearSlot(slot);
            presetBar.setSlotOccupied(slot, false);
        };

        presetBar.onMidiLearnRequest = [this](int slot)
        {
            startPresetMidiLearn(slot);
        };

        presetBar.onClearMidiNote = [this](int slot)
        {
            clearPresetMidiNote(slot);
        };

        app.scheduler.addListener(this);

        app.scheduler.setQuantizationEnabled(app.settings.quantizeEnabled);
        app.scheduler.setQuantizationSubdivision(app.settings.quantizeSubdivision);

        if (app.settings.padFilePaths.size() > 0)
            padGrid.setPadFilePaths(app.settings.padFilePaths);

        refreshStatus();
        applyThemeColors();
        startTimerHz(20); // 50ms UI refresh - lower overhead
    }

    ~PluginEditorContent() override
    {
        stopTimer();
        ThemeEngine::getInstance().removeListener(this);
        app.scheduler.removeListener(this);
        setLookAndFeel(nullptr);
    }

    void themeChanged() override
    {
        applyThemeColors();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        // No opaque fill — BackgroundAnimator paints the background
        g.setColour(Theme::border());
        g.drawRect(getLocalBounds(), 1);

        // Draw MIDI note badge on modifier toggle (below the toggle)
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();
        const int midiNote = app.settings.modifierToggleMidiNote;

        if (modifierToggleLearnActive)
        {
            // Marching-ants learn indicator around the toggle
            auto toggleArea = modifiersToggle.getBounds().toFloat().expanded(2.0f);
            juce::Path outline;
            outline.addRoundedRectangle(toggleArea, 4.0f);
            const float dashLengths[] = { 6.0f, 4.0f };
            juce::PathStrokeType strokeType(2.5f);
            juce::Path dashed;
            strokeType.createDashedStroke(dashed, outline, dashLengths, 2);
            g.setColour(palette.warn);
            g.strokePath(dashed, strokeType);

            // "LEARN" badge
            auto learnRect = juce::Rectangle<float>(
                modifiersToggle.getBounds().getCentreX() - 28.0f,
                (float) modifiersToggle.getBounds().getBottom() + 2.0f, 56.0f, 18.0f);
            g.setColour(palette.warn);
            g.fillRoundedRectangle(learnRect, 3.0f);
            g.setColour(palette.bg);
            g.setFont(ThemeFonts::getInstance().monoBoldFont(11.0f));
            g.drawText("LEARN", learnRect, juce::Justification::centred);
        }
        else if (midiNote >= 0)
        {
            auto noteRect = juce::Rectangle<float>(
                modifiersToggle.getBounds().getCentreX() - 18.0f,
                (float) modifiersToggle.getBounds().getBottom() + 2.0f, 36.0f, 16.0f);
            g.setColour(palette.panelAlt);
            g.fillRoundedRectangle(noteRect, 3.0f);
            g.setColour(palette.textSecondary);
            g.setFont(ThemeFonts::getInstance().monoFont(11.0f));
            g.drawText(juce::String(midiNote), noteRect, juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Only handle events targeting the modifiers toggle button
        if (e.eventComponent != &modifiersToggle)
            return;

        // Right-click: show context menu
        if (e.mods.isPopupMenu())
        {
            showModifierToggleContextMenu();
            // Consume the event so the toggle doesn't fire
            return;
        }

        // Shift+click to enter MIDI learn mode
        if (e.mods.isShiftDown() && !e.mods.isCommandDown())
        {
            startModifierToggleMidiLearn();
            return;
        }

        // Cmd+click (or Alt+click) to clear MIDI assignment
        if (e.mods.isCommandDown() || e.mods.isAltDown())
        {
            clearModifierToggleMidiNote();
            return;
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);

        // ── Top bar: enlarged modifier display + compact controls ──
        auto topBar = area.removeFromTop(110);
        modifierDisplay.setBounds(topBar.removeFromLeft(topBar.getWidth() * 0.65f).reduced(4));

        auto controlBar = topBar;

        // Volume knob on the right
        auto volRegion = controlBar.removeFromRight(100).reduced(2);
        auto volLabelArea = volRegion.removeFromTop(16);
        masterVolumeLabel.setBounds(volLabelArea);
        masterVolumeSlider.setBounds(volRegion);

        // Center the modifiers toggle in the remaining space
        const int toggleW = 140;
        auto toggleArea = controlBar.withSizeKeepingCentre(toggleW, controlBar.getHeight()).reduced(2);
        modifiersToggle.setBounds(toggleArea);

        // ── Status row ──
        auto row2 = area.removeFromTop(24).reduced(2);
        statusLabel.setBounds(row2.reduced(2));

        // ── Preset bar (A–D) ──
        area.removeFromTop(4);
        auto presetRow = area.removeFromTop(38);
        presetBar.setBounds(presetRow.reduced(4, 0));

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

            // Trigger reactive background pulse toward the theme accent
            if (bgAnimator != nullptr)
                bgAnimator->triggerReactivePulse (ThemeEngine::getInstance().getColor (ColorRole::Accent1));

            refreshStatus();
        });
    }

private:
    BufferTestAudioProcessor& processor;
    AppState& app;

    ThemeLookAndFeel hipLnf;

    UpcomingModifierDisplay modifierDisplay;
    PadGridComponent padGrid;

    ModifierHistoryPanel* externalModifierHistory = nullptr;

    juce::ToggleButton modifiersToggle { "Modifiers" };
    bool modifierToggleLearnActive = false;

    PresetBarComponent presetBar;
    int presetLearnSlot = -1; // which preset slot is in MIDI learn mode (-1 = none)

    juce::ComboBox partsCountBox;
    int pendingPartsCount = -1; // -1 = none; otherwise apply on next modifier trigger
    juce::Slider barsBetweenModifiersSlider;
    juce::Label barsBetweenModifiersLabel { {}, "Bars/Mod" };
    juce::Label statusLabel { {}, "Status: Idle" };

    juce::Slider masterVolumeSlider;
    juce::Label masterVolumeLabel { {}, "Vol" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterVolumeAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

    bool padPathsSynced = false; // true once pad file paths have been synced to the UI after state restore

    void applyThemeColors()
    {
        // Master volume knob
        masterVolumeSlider.setColour(juce::Slider::rotarySliderFillColourId, Theme::accent());
        masterVolumeSlider.setColour(juce::Slider::rotarySliderOutlineColourId, Theme::panelAlt());
        masterVolumeSlider.setColour(juce::Slider::thumbColourId, Theme::accent().brighter(0.2f));
        masterVolumeSlider.setColour(juce::Slider::textBoxBackgroundColourId, Theme::panelAlt());
        masterVolumeSlider.setColour(juce::Slider::textBoxTextColourId, Theme::text());
        masterVolumeSlider.setColour(juce::Slider::textBoxOutlineColourId, Theme::border());
        masterVolumeSlider.repaint();

        masterVolumeLabel.setColour(juce::Label::textColourId, Theme::textSubtle());

        // Status label
        statusLabel.setColour(juce::Label::textColourId, Theme::textSubtle());
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
        padGrid.setPlayingStates(app.bufferManager.getPlayingBufferIndices());

        // Poll for MIDI pad toggle requests (from audio thread)
        for (int i = 0; i < 8; ++i)
        {
            if (processor.checkAndClearMidiToggle(i))
            {
                padGrid.togglePadSelection(i);
            }
        }

        // Poll for MIDI modifier-toggle request (from audio thread)
        if (processor.checkAndClearModifierToggle())
        {
            modifiersToggle.setToggleState(! modifiersToggle.getToggleState(), juce::dontSendNotification);
            modifiersToggleChanged();
        }

        // Poll for MIDI preset-recall requests (from audio thread)
        for (int pi = 0; pi < 4; ++pi)
        {
            if (processor.checkAndClearPresetRecall(pi))
            {
                if (app.presetBank.isSlotOccupied(pi))
                    app.restorePreset(pi);
            }
        }

        // Poll for MIDI learn completion
        // Check for learned note even if learn mode was disabled by audio thread
        const int learnedNote = processor.checkAndClearLearnedNote();
        if (learnedNote >= 0)
        {
            DBG("UI received learned note: " + juce::String(learnedNote));
            const int padIndex = processor.getMidiLearnPadIndex();

            if (padIndex == BufferTestAudioProcessor::kModifierToggleLearnIndex)
            {
                // Learned note for modifier toggle
                DBG("Assigning note " + juce::String(learnedNote) + " to modifier toggle");
                app.settings.modifierToggleMidiNote = learnedNote;
                modifierToggleLearnActive = false;
                processor.setMidiLearnMode(false, -1);
                repaint();
            }
            else if (padIndex >= BufferTestAudioProcessor::kPresetLearnIndexBase
                     && padIndex <= BufferTestAudioProcessor::kPresetLearnIndexBase + 3)
            {
                // Learned note for a preset slot
                const int slot = padIndex - BufferTestAudioProcessor::kPresetLearnIndexBase;
                DBG("Assigning note " + juce::String(learnedNote) + " to preset " + juce::String(slot));
                app.settings.presetMidiNoteMap[static_cast<size_t>(slot)] = learnedNote;
                presetBar.setMidiNote(slot, learnedNote);
                presetBar.setMidiLearnActive(slot, false);
                presetLearnSlot = -1;
                processor.setMidiLearnMode(false, -1);
            }
            else if (padIndex >= 0 && padIndex < 8)
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

                // Sync preset bar state from restored settings
                for (int i = 0; i < 4; ++i)
                {
                    presetBar.setSlotOccupied(i, app.presetBank.isSlotOccupied(i));
                    presetBar.setMidiNote(i, app.settings.presetMidiNoteMap[static_cast<size_t>(i)]);
                }

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

        // Use suppression instead of stop/start so the scheduler timeline
        // stays synchronised with the DAW transport.  The progress bar
        // keeps running (with a "PAUSED" overlay) when modifiers are off,
        // and turning them back on does not reset timing.
        app.scheduler.setSuppressed(! enabled);

        // If the scheduler isn't running yet (e.g. first enable before
        // transport has started it), kick it off so there is something
        // to show in the progress display.
        if (enabled && ! app.scheduler.isRunning())
            app.scheduler.start();

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

    void startModifierToggleMidiLearn()
    {
        // Cancel any existing pad learn mode
        if (processor.isMidiLearnEnabled())
        {
            const int prevPad = processor.getMidiLearnPadIndex();
            if (prevPad >= 0 && prevPad < 8)
                padGrid.setMidiLearnForPad(prevPad, false);
        }

        // If already learning for toggle, cancel
        if (modifierToggleLearnActive)
        {
            modifierToggleLearnActive = false;
            processor.setMidiLearnMode(false, -1);
            repaint();
            return;
        }

        modifierToggleLearnActive = true;
        processor.setMidiLearnMode(true, BufferTestAudioProcessor::kModifierToggleLearnIndex);
        repaint();
    }

    void clearModifierToggleMidiNote()
    {
        app.settings.modifierToggleMidiNote = -1;
        modifierToggleLearnActive = false;
        processor.setMidiLearnMode(false, -1);
        repaint();
    }

    void startPresetMidiLearn(int slot)
    {
        if (slot < 0 || slot >= 4) return;

        // Cancel any existing learn mode (pad, modifier toggle, or another preset)
        if (processor.isMidiLearnEnabled())
        {
            const int prevPad = processor.getMidiLearnPadIndex();
            if (prevPad >= 0 && prevPad < 8)
                padGrid.setMidiLearnForPad(prevPad, false);
            if (modifierToggleLearnActive)
            {
                modifierToggleLearnActive = false;
                repaint();
            }
            if (presetLearnSlot >= 0 && presetLearnSlot < 4)
                presetBar.setMidiLearnActive(presetLearnSlot, false);
        }

        // If clicking the same slot already in learn mode, cancel
        if (presetLearnSlot == slot)
        {
            presetLearnSlot = -1;
            presetBar.setMidiLearnActive(slot, false);
            processor.setMidiLearnMode(false, -1);
            return;
        }

        presetLearnSlot = slot;
        presetBar.setMidiLearnActive(slot, true);
        processor.setMidiLearnMode(true, BufferTestAudioProcessor::kPresetLearnIndexBase + slot);
    }

    void clearPresetMidiNote(int slot)
    {
        if (slot < 0 || slot >= 4) return;
        app.settings.presetMidiNoteMap[static_cast<size_t>(slot)] = -1;
        presetBar.setMidiNote(slot, -1);
        if (presetLearnSlot == slot)
        {
            presetLearnSlot = -1;
            presetBar.setMidiLearnActive(slot, false);
            processor.setMidiLearnMode(false, -1);
        }
    }

    void showModifierToggleContextMenu()
    {
        const int midiNote = app.settings.modifierToggleMidiNote;

        juce::PopupMenu menu;

        // MIDI learn
        juce::String learnLabel = "MIDI Learn";
        learnLabel += "    [Shift+Click]";
        menu.addItem(1, learnLabel);

        // Clear MIDI note
        juce::String clearLabel = "Clear MIDI Note";
       #if JUCE_MAC
        clearLabel += "    [Cmd+Click]";
       #else
        clearLabel += "    [Alt+Click]";
       #endif
        menu.addItem(2, clearLabel, midiNote >= 0);

        if (midiNote >= 0)
        {
            menu.addSeparator();
            menu.addItem(0, "MIDI Note: " + juce::String(midiNote), false);
        }

        menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
            [this](int result)
            {
                switch (result)
                {
                    case 1: startModifierToggleMidiLearn(); break;
                    case 2: clearModifierToggleMidiNote();  break;
                    default: break;
                }
            });
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

    settingsPanel = std::make_unique<SettingsPanelContent>(processor.getAppState().settings);

    setLookAndFeel(&editorLnf);

    tabComponent = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    auto tabBg = juce::Colour (0x00000000);  // transparent — BackgroundAnimator shows through
    helpPanel = std::make_unique<HelpPanelContent>();

    tabComponent->addTab("Session",     tabBg, content.get(), false);
    tabComponent->addTab("Probability", tabBg, probabilityPanel.get(), false);
    tabComponent->addTab("Settings",    tabBg, settingsPanel.get(), false);
    tabComponent->addTab("Debug",       tabBg, debugPanel.get(), false);
    tabComponent->addTab("Help",        tabBg, helpPanel.get(), false);

    // Style the tab bar
    auto& tabBar = tabComponent->getTabbedButtonBar();
    tabBar.setColour(juce::TabbedButtonBar::tabOutlineColourId, Theme::border());
    tabBar.setColour(juce::TabbedButtonBar::frontOutlineColourId, Theme::accent());

    // Background animator (sits behind everything)
    backgroundAnimator = std::make_unique<BackgroundAnimator>();
    addAndMakeVisible(backgroundAnimator.get());

    // Wire up reactive background pulse from modifier triggers
    static_cast<PluginEditorContent*>(content.get())->bgAnimator = backgroundAnimator.get();

    addAndMakeVisible(tabComponent.get());

    // Apply the saved theme on editor open
    ThemeEngine::getInstance().setTheme(processor.getAppState().settings.themeName);

    // Moonbase: enable update badge and disable analytics
    if (activationUI)
        activationUI->enableUpdateBadge();

    if (processor.moonbaseClient)
        processor.moonbaseClient->setTransmitAnalytics (false, false);

    setSize (1200, 800);
    setResizable(true, true);
    setResizeLimits(920, 600, 2400, 1600);
}

BufferTestAudioProcessorEditor::~BufferTestAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void BufferTestAudioProcessorEditor::paint (juce::Graphics&)
{
    // No opaque fill — BackgroundAnimator paints the background
}

void BufferTestAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    if (backgroundAnimator)
        backgroundAnimator->setBounds(bounds);

    if (tabComponent)
        tabComponent->setBounds(bounds);

    MOONBASE_RESIZE_ACTIVATION_UI
}
