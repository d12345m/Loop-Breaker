#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "HelpPanelContent.h"
#include "UpcomingModifierDisplay.h"
#include "PadGridComponent.h"
#include "ModifierHistoryPanel.h"
#include "ModifierSelectionPanel.h"
#include "TearingDebugPanel.h"
#include "ModifierProbabilityPanel.h"
#if JUCE_DEBUG
#include "DebugPanelContent.h"
#include "GlyphLabComponent.h"
#endif
#include "ThemeEngine.h"
#include "ThemeFonts.h"
#include "ThemeLookAndFeel.h"
#include "SettingsPanelContent.h"
#include "BackgroundAnimator.h"
#include "PresetBarComponent.h"
#include "PlatformConfig.h"

#if JUCE_MAC
#include <CoreGraphics/CoreGraphics.h>
#include <objc/message.h>
#include <objc/runtime.h>
#endif

namespace
{

#if JUCE_MAC
juce::Point<int> getNativeHostViewportSize (const juce::Component& component)
{
    // An embedded VST3 peer can retain the requested editor size even after
    // its host NSView has been constrained to a smaller on-screen viewport.
    auto* nativeView = reinterpret_cast<id> (component.getWindowHandle());
    if (nativeView == nil)
        return {};

    const auto sendObjectMessage = reinterpret_cast<id (*) (id, SEL)> (objc_msgSend);
    auto* window = sendObjectMessage (nativeView, sel_registerName ("window"));
    if (window != nil
        && sendObjectMessage (window, sel_registerName ("contentView")) == nativeView)
        return {};

    auto* hostView = sendObjectMessage (nativeView, sel_registerName ("superview"));
    if (hostView == nil)
        return {};

    CGRect bounds;
   #if defined (__x86_64__)
    reinterpret_cast<void (*) (CGRect*, id, SEL)> (objc_msgSend_stret) (
        &bounds, hostView, sel_registerName ("bounds"));
   #else
    bounds = reinterpret_cast<CGRect (*) (id, SEL)> (objc_msgSend) (
        hostView, sel_registerName ("bounds"));
   #endif

    return { juce::roundToInt (bounds.size.width),
             juce::roundToInt (bounds.size.height) };
}
#else
juce::Point<int> getNativeHostViewportSize (const juce::Component&)
{
    return {};
}
#endif

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

#if JUCE_DEBUG
        addAndMakeVisible(modifiersToggle);
        modifiersToggle.setToggleState(app.settings.modifiersEnabled, juce::dontSendNotification);
        modifiersToggle.onClick = [this]{ modifiersToggleChanged(); };
        modifiersToggle.addMouseListener(this, false);
#endif

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
        masterVolumeSlider.addMouseListener (this, false);
        masterVolumeSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        masterVolumeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 54, 16);
        masterVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.getAPVTS(), "masterVolume", masterVolumeSlider);
        addAndMakeVisible(masterVolumeLabel);
        masterVolumeLabel.setJustificationType(juce::Justification::centred);
        masterVolumeLabel.setFont(ThemeFonts::getInstance().controlLabelFont(15.0f));
        updateMasterVolumeMidiTooltip();

        // statusLabel hidden — may be re-used later
        statusLabel.setVisible(false);
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
                if (previousLearnPad >= 0
                    && previousLearnPad < SessionSettings::kNumPads
                    && previousLearnPad != padIndex)
                {
                    padGrid.setMidiLearnForPad(previousLearnPad, false);
                }
            }
            
            processor.setMidiLearnMode(true, padIndex);
            padGrid.setMidiLearnForPad(padIndex, true);
        };

        padGrid.onClearMidiNote = [this](int padIndex)
        {
            app.settings.midiNoteMap[static_cast<size_t> (padIndex)] = -1;
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

            auto safeThis = juce::Component::SafePointer<PluginEditorContent> (this);
            fileChooser->launchAsync(
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safeThis, padIndex](const juce::FileChooser& fc)
                {
                    auto* self = safeThis.getComponent();
                    if (self == nullptr)
                        return;

                   #if JUCE_IOS
                    const auto result = fc.getURLResult();
                    if (! result.isEmpty())
                        self->importIOSFileIntoPad (padIndex, result);
                   #else
                    auto result = fc.getResult();
                    if (result.existsAsFile())
                        self->loadFileIntoPad(padIndex, result);
                   #endif
                });
        };

        // Initialize pad MIDI notes from settings
        for (int i = 0; i < SessionSettings::kNumPads; ++i)
            padGrid.setMidiNoteForPad (i, app.settings.midiNoteMap[static_cast<size_t> (i)]);

        // ── Platform preset bank ──
        addAndMakeVisible(presetBar);
        for (int i = 0; i < SessionSettings::kNumPresets; ++i)
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
            if (isTransportRunning())
            {
                presetBar.startPendingGlow(slot, app.settings.bpm);
                queuePresetRecall(slot);
            }
            else
            {
                // Transport is stopped — apply immediately, no queue
                app.restorePreset(slot);
            }
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

        modifierDisplay.setPlannedQueue (app.scheduler.getPlannedQueueSnapshot());

        app.scheduler.setQuantizationEnabled(app.settings.quantizeEnabled);
        app.scheduler.setQuantizationSubdivision(app.settings.quantizeSubdivision);

        if (app.settings.padFilePaths.size() > 0)
            padGrid.setPadFilePaths(app.settings.padFilePaths);

        refreshStatus();
        applyThemeColors();
        startTimerHz (LoopBreakerConfig::uiRefreshRateHz);
    }

    ~PluginEditorContent() override
    {
        stopTimer();
       #if JUCE_IOS
        fileChooser.reset();
        iosFileImportPool.removeAllJobs (true, 5000);
       #endif
        ThemeEngine::getInstance().removeListener(this);
        app.scheduler.removeListener(this);
        masterVolumeSlider.removeMouseListener (this);
        setLookAndFeel(nullptr);
    }

    void setPortraitLayout (bool shouldUsePortraitLayout)
    {
        if (portraitLayout == shouldUsePortraitLayout)
            return;

        portraitLayout = shouldUsePortraitLayout;
        masterVolumeLabel.setVisible (! portraitLayout);
        masterVolumeSlider.setVisible (! portraitLayout);
        modifierDisplay.setPortraitLayout (portraitLayout);
        padGrid.setPortraitLayout (portraitLayout);
        presetBar.setPortraitLayout (portraitLayout);
        resized();
        repaint();
    }

    void themeChanged() override
    {
        applyThemeColors();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        // No opaque fill — BackgroundAnimator paints the background

        // ── "LOOP BREAKER" branding in the top-left with progress fill ──
        {
            auto brandArea = brandingBounds.toFloat();
            auto wordmarkArea = brandArea;
            auto& fonts = ThemeFonts::getInstance();
           #if JUCE_IOS
            const auto font = fonts.displayFont(24.0f * uiScale);
           #else
            const auto font = fonts.displayFont(30.0f * uiScale);
           #endif
            g.setFont(font);

            const juce::String logoText("LOOP BREAKER");

            // Build glyph arrangement for precise clipping
            juce::GlyphArrangement glyphs;
           #if JUCE_IOS
            // Keep the compact wordmark inside the left side of a notched
            // device's sensor housing instead of centring it behind the notch.
            const float textX = wordmarkArea.getX();
           #else
            const float textW = juce::GlyphArrangement::getStringWidth(font, logoText);
            const float textX = wordmarkArea.getX() + (wordmarkArea.getWidth() - textW) * 0.5f;
           #endif
            glyphs.addLineOfText(font, logoText, textX,
                                 wordmarkArea.getY() + font.getAscent()
                                 + (wordmarkArea.getHeight() - font.getHeight()) * 0.5f);

            auto glyphBounds = glyphs.getBoundingBox(0, -1, false);
            const float fillX = glyphBounds.getX() + glyphBounds.getWidth() * brandingProgress;
            const auto& palette = ThemeEngine::getInstance().getCurrentPalette();

            // Draw unfilled portion (dim)
            {
                g.saveState();
                g.reduceClipRegion(juce::Rectangle<int>(
                    (int)fillX, brandingBounds.getY(),
                    (int)(glyphBounds.getRight() - fillX + 2), brandingBounds.getHeight()));
                g.setColour(palette.textSecondary.withAlpha(0.25f));
                glyphs.draw(g);
                g.restoreState();
            }

            // Draw filled portion (gradient: accent2 → accent1)
            if (brandingProgress > 0.001f)
            {
                g.saveState();
                g.reduceClipRegion(juce::Rectangle<int>(
                    (int)glyphBounds.getX(), brandingBounds.getY(),
                    (int)(fillX - glyphBounds.getX() + 2), brandingBounds.getHeight()));

                if (brandingSuppressed)
                {
                    g.setColour(palette.warn);
                }
                else
                {
                    juce::ColourGradient grad(palette.accent2, glyphBounds.getX(), glyphBounds.getCentreY(),
                                              palette.accent1, glyphBounds.getRight(), glyphBounds.getCentreY(), false);
                    g.setGradientFill(grad);
                }
                glyphs.draw(g);
                g.restoreState();
            }

            // When no progress (idle), show original two-tone style
            if (brandingProgress <= 0.001f && !brandingSuppressed)
            {
                const juce::String loopStr("LOOP ");
                const juce::String breakerStr("BREAKER");
                const float loopW = juce::GlyphArrangement::getStringWidth(font, loopStr);

                g.setColour(Theme::accent());
                g.drawText(loopStr, juce::Rectangle<float>(textX, wordmarkArea.getY(), loopW, wordmarkArea.getHeight()),
                           juce::Justification::centredLeft, false);
                g.setColour(Theme::text());
                g.drawText(breakerStr, juce::Rectangle<float>(textX + loopW, wordmarkArea.getY(),
                           wordmarkArea.getRight() - (textX + loopW), wordmarkArea.getHeight()),
                           juce::Justification::centredLeft, false);
            }

        }

        modifierDisplay.paintProgressBar (g, modifierProgressBounds.toFloat());

#if JUCE_DEBUG
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
#endif
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.eventComponent == &masterVolumeSlider)
        {
            if (e.mods.isPopupMenu())
                showMasterVolumeContextMenu();
            else if (processor.getMidiControlCCLearnTarget()
                     == BufferTestAudioProcessor::kMasterVolumeCCLearnTarget)
                processor.setMidiControlCCLearnTarget (-1);
            return;
        }

#if JUCE_DEBUG
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
#endif
    }

    void resized() override
    {
        const float widthScale = static_cast<float> (getWidth())
                               / (portraitLayout ? 540.0f : 1200.0f);
        const float heightScale = static_cast<float> (getHeight())
                                / (portraitLayout ? 930.0f : 770.0f);
        uiScale = juce::jlimit (0.75f, 2.0f, juce::jmin (widthScale, heightScale));
        modifierDisplay.setUiScale (uiScale);
        masterVolumeLabel.setFont (
            ThemeFonts::getInstance().controlLabelFont (15.0f * uiScale));

        const auto scaled = [this] (float value)
        {
            return juce::jmax (1, juce::roundToInt (value * uiScale));
        };

        auto area = getLocalBounds().reduced(scaled (8.0f));

        if (portraitLayout)
        {
            auto brandRow = area.removeFromTop (scaled (52.0f));
            brandingBounds = brandRow.reduced (scaled (4.0f), scaled (3.0f));

#if JUCE_IOS
            // The page popup floats in this row so navigation does not consume
            // a dedicated strip of precious phone height.
            brandingBounds.removeFromRight (scaled (116.0f));
#endif

#if JUCE_DEBUG
            auto debugArea = brandingBounds.removeFromRight (scaled (94.0f));
            modifiersToggle.setBounds (debugArea.reduced (scaled (2.0f)));
#endif

            area.removeFromTop (scaled (4.0f));
            auto modifierBlock = area.removeFromTop (scaled (116.0f));
            auto modifierPanel = modifierBlock.reduced (scaled (4.0f));
            modifierDisplay.setBounds (modifierPanel);
            modifierProgressBounds = {
                modifierPanel.getX(), modifierPanel.getBottom(),
                modifierPanel.getWidth(), scaled (3.0f)
            };
            masterVolumeLabel.setBounds ({});
            masterVolumeSlider.setBounds ({});

            area.removeFromTop (scaled (6.0f));
           #if JUCE_IOS
            presetBar.setBounds (area.removeFromTop (scaled (44.0f)));
           #else
            presetBar.setBounds (area.removeFromTop (scaled (82.0f)));
           #endif
        }
        else
        {
            // ── Top bar: branding | centred modifier display | controls ──
            auto topBar = area.removeFromTop (scaled (110.0f));
            const int sideW = scaled (245.0f);

            brandingBounds = topBar.removeFromLeft(sideW).reduced (
                scaled (4.0f), scaled (8.0f));

            auto controlBar = topBar.removeFromRight(sideW);
            auto volRegion = controlBar.reduced(scaled (2.0f));
            volRegion.removeFromBottom (scaled (4.0f));
            auto volLabelArea = volRegion.removeFromTop(scaled (16.0f));
            masterVolumeLabel.setBounds(volLabelArea);
            const auto knobSize = juce::jmin (
                scaled (80.0f), juce::jmin (volRegion.getWidth(), volRegion.getHeight()));
            masterVolumeSlider.setBounds(volRegion.withSizeKeepingCentre(knobSize, knobSize));

#if JUCE_DEBUG
            auto debugToggleBar = topBar.removeFromRight(scaled (100.0f));
            const int toggleW = scaled (100.0f);
            auto toggleArea = debugToggleBar.withSizeKeepingCentre(
                toggleW, debugToggleBar.getHeight()).reduced(scaled (2.0f));
            modifiersToggle.setBounds(toggleArea);
#endif

            auto modifierPanel = topBar.reduced(scaled (4.0f));
            modifierDisplay.setBounds(modifierPanel);
            modifierProgressBounds = {
                modifierPanel.getX(), modifierPanel.getBottom(),
                modifierPanel.getWidth(), scaled (3.0f)
            };

            area.removeFromTop(scaled (6.0f));
            presetBar.setBounds(area.removeFromTop(scaled (38.0f)));
        }

        area.removeFromTop(scaled (6.0f));
        padGrid.setBounds(area);
    }

    /** Called by the Settings tab when the parts dropdown changes.
        Applies loop braces immediately if transport is stopped,
        or defers until the next modifier trigger if transport is running. */
    void handlePartsChanged (int numParts)
    {
        const int n = juce::jlimit (1, 4, numParts);

        // Keep the internal (hidden) combo in sync
        partsCountBox.setSelectedId (n, juce::dontSendNotification);

        // If the DAW transport is stopped, apply immediately.
        if (processor.getLastHostTransportState() == BufferTestAudioProcessor::HostTransportState::Stopped)
        {
            app.settings.parts.numParts = n;
            app.setActivePart (app.getActivePart());
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

        // Not running: apply immediately so loop braces update right away.
        app.settings.parts.numParts = n;
        app.setActivePart (app.getActivePart());
        pendingPartsCount = -1;
        refreshStatus();
    }

    // ModifierSchedulerListener
    void upcomingModifierChanged(const ModifierDescriptor& desc) override
    {
        // If a preset recall is pending, don't let the scheduler's random pick
        // overwrite the "Preset X" display.
        if (app.pendingPresetRecall.load() >= 0)
            return;

        auto descCopy = desc;
        auto safeThis = juce::Component::SafePointer<PluginEditorContent>(this);
        juce::MessageManager::callAsync([safeThis, descCopy]
        {
            if (safeThis == nullptr) return;
            safeThis->modifierDisplay.setUpcoming(descCopy);
        });
    }

    void plannedQueueChanged (const std::vector<PlannedModifier>& queue) override
    {
        // AsyncUpdater in ModifierScheduler guarantees this callback is on the
        // message thread. A pending preset temporarily owns the NEXT display.
        if (app.pendingPresetRecall.load() < 0)
            modifierDisplay.setPlannedQueue (queue);
    }

    void modifierTriggered(const ModifierDescriptor& desc, const juce::Array<int>& targets) override
    {
        auto safeThis = juce::Component::SafePointer<PluginEditorContent>(this);
        juce::MessageManager::callAsync([safeThis, desc, targets]
        {
            if (safeThis == nullptr) return;
            auto* self = safeThis.getComponent();

            // Apply pending parts count change exactly on the next modifier trigger.
            if (self->pendingPartsCount >= 1 && self->pendingPartsCount <= 4)
            {
                self->app.settings.parts.numParts = self->pendingPartsCount;
                self->app.setActivePart(self->app.getActivePart());
                self->pendingPartsCount = -1;
            }

            if (self->externalModifierHistory != nullptr)
                self->externalModifierHistory->addEntry(desc, targets);
            self->padGrid.flashPads(targets);

            // Pull the post-trigger engine snapshot so resets and stack swaps
            // update every sticker together.
            self->syncModifierStickers();

            if (desc.type == ModifierType::ResetAll)
            {
                self->padGrid.clearModifierStickers (targets);
                self->syncModifierStickers();
            }
            else if (desc.type == ModifierType::SwitchPart)
            {
                self->padGrid.showTransientModifierSticker (desc.type, {}, 900.0);
            }
            else if (desc.type == ModifierType::QuarterNoteBurst)
            {
                const double durationMs = 1000.0
                    * desc.plannedBurstBars.value_or (1)
                    * self->app.settings.getSecondsPerBar();
                self->padGrid.showTransientModifierSticker (desc.type, {}, durationMs);
            }
            else if (desc.type == ModifierType::SwapModifierStack)
            {
                self->padGrid.showTransientModifierSticker (desc.type, targets, 1100.0);
            }

            if (! targets.isEmpty())
                self->padGrid.clearSelections();

            // Trigger reactive background pulse toward the theme accent
            if (self->bgAnimator != nullptr)
                self->bgAnimator->triggerReactivePulse (ThemeEngine::getInstance().getColor (ColorRole::Accent1));

            self->refreshStatus();
        });
    }

private:
    BufferTestAudioProcessor& processor;
    AppState& app;

    ThemeLookAndFeel hipLnf;

    UpcomingModifierDisplay modifierDisplay;
    PadGridComponent padGrid;

    ModifierHistoryPanel* externalModifierHistory = nullptr;

#if JUCE_DEBUG
    juce::ToggleButton modifiersToggle { "Modifiers" };
    bool modifierToggleLearnActive = false;
#endif

    juce::Rectangle<int> brandingBounds;
    juce::Rectangle<int> modifierProgressBounds;
    float brandingProgress = 0.0f;
    bool brandingSuppressed = false;
    float uiScale = 1.0f;

    PresetBarComponent presetBar;
    int presetLearnSlot = -1; // which preset slot is in MIDI learn mode (-1 = none)

    juce::ComboBox partsCountBox;
    int pendingPartsCount = -1; // -1 = none; otherwise apply on next modifier trigger
    juce::Slider barsBetweenModifiersSlider;
    juce::Label barsBetweenModifiersLabel { {}, "Bars/Mod" };
    juce::Label statusLabel { {}, "Status: Idle" };

    struct MidiSlider : public juce::Slider
    {
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) return;
            juce::Slider::mouseDown (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) return;
            juce::Slider::mouseDrag (e);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) return;
            juce::Slider::mouseUp (e);
        }
    };

    MidiSlider masterVolumeSlider;
    juce::Label masterVolumeLabel { {}, "Vol" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterVolumeAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

   #if JUCE_IOS
    class FunctionThreadPoolJob final : public juce::ThreadPoolJob
    {
    public:
        explicit FunctionThreadPoolJob (std::function<void()> taskToRun)
            : juce::ThreadPoolJob ("Import iOS audio file"),
              task (std::move (taskToRun))
        {
        }

        JobStatus runJob() override
        {
            task();
            return jobHasFinished;
        }

    private:
        std::function<void()> task;
    };

    juce::ThreadPool iosFileImportPool { 1 };
   #endif

    bool padPathsSynced = false; // true once pad file paths have been synced to the UI after state restore
    bool portraitLayout = false;
    std::array<double, AudioBufferManager::MAX_BUFFERS> lastPlayheadSamples {};
    std::array<bool, AudioBufferManager::MAX_BUFFERS> lastLoopEnabled {};

    // Queue a preset recall to replace the next scheduled modifier trigger.
    // Updates the upcoming modifier display to show the preset name.
    void queuePresetRecall(int slot)
    {
        if (slot < 0 || slot >= ModifierPresetBank::kNumPresets) return;
        if (! app.presetBank.isSlotOccupied(slot)) return;

        app.pendingPresetRecall.store(slot);

        // Build a fake descriptor so the UpcomingModifierDisplay shows "Preset A/B/C/D/E/F/G/H"
        static const char* slotLabels[] = { "A", "B", "C", "D", "E", "F", "G", "H" };
        ModifierDescriptor presetDesc;
        presetDesc.type = ModifierType::Unknown;
        presetDesc.category = ModifierCategory::GlobalUtility;
        presetDesc.shortName = juce::String("Preset ") + slotLabels[slot];
        presetDesc.description = juce::String("Recall saved modifier snapshot (slot ") + slotLabels[slot] + ")";
        modifierDisplay.setUpcoming(presetDesc);
    }

    void applyThemeColors()
    {
        // Master volume knob
        masterVolumeSlider.setColour(juce::Slider::rotarySliderFillColourId, Theme::warn());
        masterVolumeSlider.setColour(juce::Slider::rotarySliderOutlineColourId, Theme::panelAlt());
        masterVolumeSlider.setColour(juce::Slider::thumbColourId, Theme::warn());
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

   #if JUCE_IOS
    static juce::File copyPickedURLToIOSContainer (const juce::URL& source,
                                                   juce::String& error)
    {
        auto importDirectory =
            juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                .getChildFile ("Imported Audio");

        if (auto result = importDirectory.createDirectory(); result.failed())
        {
            error = result.getErrorMessage();
            return {};
        }

        auto originalName = juce::File::createLegalFileName (source.getFileName());
        if (originalName.isEmpty())
            originalName = "Imported Audio.wav";

        // A bare filename is not a valid juce::File on iOS (or any POSIX
        // platform in a debug build). Resolve it against the absolute sandbox
        // directory before asking JUCE to split the stem and extension.
        const auto sourceName = importDirectory.getChildFile (originalName);
        const auto destination = importDirectory.getNonexistentChildFile (
            sourceName.getFileNameWithoutExtension(),
            sourceName.getFileExtension(),
            false);

        auto input = source.createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress));
        if (input == nullptr)
        {
            error = "The selected file could not be opened.";
            return {};
        }

        juce::TemporaryFile temporary (destination);
        auto output = temporary.getFile().createOutputStream();
        if (output == nullptr || ! output->openedOk())
        {
            error = "The selected file could not be copied into the app.";
            return {};
        }

        output->writeFromInputStream (*input, -1);
        output->flush();
        const auto writeStatus = output->getStatus();
        output.reset();
        input.reset();

        if (writeStatus.failed())
        {
            error = writeStatus.getErrorMessage();
            return {};
        }

        if (! temporary.overwriteTargetFileWithTemporary())
        {
            error = "The imported audio file could not be finalised.";
            return {};
        }

        return destination;
    }

    void importIOSFileIntoPad (int padIndex, const juce::URL& source)
    {
        auto safeThis = juce::Component::SafePointer<PluginEditorContent> (this);

        iosFileImportPool.addJob (
            new FunctionThreadPoolJob ([safeThis, padIndex, source]
            {
                juce::String error;
                const auto importedFile = copyPickedURLToIOSContainer (source, error);

                juce::MessageManager::callAsync (
                    [safeThis, padIndex, importedFile, error]
                    {
                        auto* self = safeThis.getComponent();
                        if (self == nullptr)
                            return;

                        if (importedFile.existsAsFile())
                        {
                            self->loadFileIntoPad (padIndex, importedFile);
                            return;
                        }

                        juce::AlertWindow::showMessageBoxAsync (
                            juce::AlertWindow::WarningIcon,
                            "Import Failed",
                            error.isNotEmpty() ? error
                                               : "The selected audio file could not be imported.");
                    });
            }),
            true);
    }
   #endif

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
        updateMasterVolumeMidiTooltip();
        padGrid.setPlayingStates(app.bufferManager.getPlayingBufferIndices());
        syncModifierStickers();

        // Poll for MIDI pad toggle requests (from audio thread)
        for (int i = 0; i < SessionSettings::kNumPads; ++i)
        {
            if (processor.checkAndClearMidiToggle(i))
            {
                padGrid.togglePadSelection(i);
            }
        }

        // Poll for MIDI modifier-toggle request (from audio thread)
        if (processor.checkAndClearModifierToggle())
        {
#if JUCE_DEBUG
            modifiersToggle.setToggleState(! modifiersToggle.getToggleState(), juce::dontSendNotification);
#endif
            modifiersToggleChanged();
        }

        // If a pending glow is active but the preset has already been consumed
        // by AppState::modifierTriggered (atomic went back to -1), clear the
        // glow immediately so it doesn't linger past the measure boundary.
        {
            const int glowSlot = presetBar.getPendingGlowSlot();
            if (glowSlot >= 0 && app.pendingPresetRecall.load() < 0)
                presetBar.clearPendingGlow(glowSlot);
        }

        // Poll for MIDI preset-recall requests (from audio thread)
        for (int pi = 0; pi < SessionSettings::kNumPresets; ++pi)
        {
            if (processor.checkAndClearPresetRecall(pi))
            {
                if (app.presetBank.isSlotOccupied(pi))
                {
                    if (isTransportRunning())
                    {
                        presetBar.startPendingGlow(pi, app.settings.bpm);
                        queuePresetRecall(pi);
                    }
                    else
                    {
                        presetBar.triggerHighlight(pi, PresetBarComponent::HighlightType::Recall);
                        app.restorePreset(pi);
                    }
                }
                else
                {
                    presetBar.triggerHighlight(pi, PresetBarComponent::HighlightType::Save);
                    app.capturePreset(pi);
                    presetBar.setSlotOccupied(pi, true);
                }
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
#if JUCE_DEBUG
                modifierToggleLearnActive = false;
#endif
                processor.setMidiLearnMode(false, -1);
                repaint();
            }
            else if (padIndex >= BufferTestAudioProcessor::kPresetLearnIndexBase
                     && padIndex < BufferTestAudioProcessor::kPresetLearnIndexBase
                                     + SessionSettings::kNumPresets)
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
            else if (padIndex >= BufferTestAudioProcessor::kProbabilityActionLearnIndexBase
                     && padIndex < BufferTestAudioProcessor::kProbabilityActionLearnIndexBase
                                     + SessionSettings::kNumProbabilityActions)
            {
                const int actionIndex =
                    padIndex - BufferTestAudioProcessor::kProbabilityActionLearnIndexBase;
                app.settings.midiProbabilityActionNoteMap[static_cast<size_t> (actionIndex)] = learnedNote;

                // Clear any stale learn badges from another note-mappable control.
                for (int i = 0; i < SessionSettings::kNumPads; ++i)
                    padGrid.setMidiLearnForPad (i, false);
                if (presetLearnSlot >= 0
                    && presetLearnSlot < SessionSettings::kNumPresets)
                    presetBar.setMidiLearnActive (presetLearnSlot, false);
                presetLearnSlot = -1;
#if JUCE_DEBUG
                modifierToggleLearnActive = false;
#endif
                processor.setMidiLearnMode (false, -1);
            }
            else if (padIndex >= 0 && padIndex < SessionSettings::kNumPads)
            {
                DBG("Assigning note " + juce::String(learnedNote) + " to pad " + juce::String(padIndex));
                app.settings.midiNoteMap[static_cast<size_t> (padIndex)] = learnedNote;
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
                for (int i = 0; i < SessionSettings::kNumPads; ++i)
                    padGrid.setMidiNoteForPad (
                        i, app.settings.midiNoteMap[static_cast<size_t> (i)]);
                // Sync UI controls to restored settings
#if JUCE_DEBUG
                modifiersToggle.setToggleState(app.settings.modifiersEnabled, juce::dontSendNotification);
#endif
                {
                    int loadedParts = juce::jlimit(1, 4, app.settings.parts.getNumParts());
                    partsCountBox.setSelectedId(loadedParts, juce::dontSendNotification);
                }
                barsBetweenModifiersSlider.setValue(app.settings.barsBetweenModifiers, juce::dontSendNotification);

                // Sync preset bar state from restored settings
                for (int i = 0; i < SessionSettings::kNumPresets; ++i)
                {
                    presetBar.setSlotOccupied(i, app.presetBank.isSlotOccupied(i));
                    presetBar.setMidiNote(i, app.settings.presetMidiNoteMap[static_cast<size_t>(i)]);
                }

                padPathsSynced = true;
            }
        }

        // Update modifier countdown/progress (driven by scheduler host timeline when available).
        modifierDisplay.setTempoBpm (app.settings.bpm);
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
        bool isSuppressed = (! app.settings.modifiersEnabled) || app.scheduler.isSuppressed();
        modifierDisplay.setSuppressed(isSuppressed);
        repaint (modifierProgressBounds);

        // Update branding progress for logo fill effect
        float newProgress = app.scheduler.isRunning() ? (float)app.scheduler.getProgressToNextTrigger() : 0.0f;
        bool newSuppressed = isSuppressed;
        if (! juce::approximatelyEqual (newProgress, brandingProgress)
            || newSuppressed != brandingSuppressed)
        {
            brandingProgress = newProgress;
            brandingSuppressed = newSuppressed;
            repaint(brandingBounds);
        }

        // Update playhead positions for waveform display (only when not playing).
        bool padGridDirty = false;
        for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
        {
            if (auto* b = app.bufferManager.getBuffer(i); b && b->hasAudioLoaded())
            {
                padGrid.setTotalSamplesForPad(i, (double) b->getDurationInSamples());

                const double newPlayhead = (double) b->getPlayheadPositionInSamples();
                if (std::abs(newPlayhead - lastPlayheadSamples[(size_t)i]) > 1.0)
                {
                    lastPlayheadSamples[(size_t)i] = newPlayhead;
                    padGridDirty = true;
                }
                padGrid.setPlayheadForPad(i, newPlayhead);

                const bool loopOn = b->isLoopWindowEnabled();
                if (loopOn != lastLoopEnabled[(size_t)i])
                {
                    lastLoopEnabled[(size_t)i] = loopOn;
                    padGridDirty = true;
                }
                padGrid.setLoopWindowForPad(i,
                                           loopOn,
                                           (double) b->getLoopStartSamples(),
                                           (double) b->getLoopEndSamples());
            }
        }
        
        // Only repaint padGrid when playhead or loop state actually changed
        if (padGridDirty)
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

    void syncModifierStickers()
    {
        for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
            padGrid.setModifierStickerMask(
                i, app.getActiveModifierStickerMask(i));
    }

    void modifiersToggleChanged()
    {
#if JUCE_DEBUG
        const bool enabled = modifiersToggle.getToggleState();
#else
        const bool enabled = ! app.settings.modifiersEnabled;
#endif
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
        app.settings.barsBetweenModifiers = juce::jlimit(1, 32, bars);
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
#if JUCE_DEBUG
        // Cancel any existing pad learn mode
        if (processor.isMidiLearnEnabled())
        {
            const int prevPad = processor.getMidiLearnPadIndex();
            if (prevPad >= 0 && prevPad < SessionSettings::kNumPads)
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
#endif
    }

    void clearModifierToggleMidiNote()
    {
        app.settings.modifierToggleMidiNote = -1;
#if JUCE_DEBUG
        modifierToggleLearnActive = false;
#endif
        processor.setMidiLearnMode(false, -1);
        repaint();
    }

    void startPresetMidiLearn(int slot)
    {
        if (slot < 0 || slot >= SessionSettings::kNumPresets) return;

        // Cancel any existing learn mode (pad, modifier toggle, or another preset)
        if (processor.isMidiLearnEnabled())
        {
            const int prevPad = processor.getMidiLearnPadIndex();
            if (prevPad >= 0 && prevPad < SessionSettings::kNumPads)
                padGrid.setMidiLearnForPad(prevPad, false);
#if JUCE_DEBUG
            if (modifierToggleLearnActive)
            {
                modifierToggleLearnActive = false;
                repaint();
            }
#endif
            if (presetLearnSlot >= 0
                && presetLearnSlot < SessionSettings::kNumPresets)
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
        if (slot < 0 || slot >= SessionSettings::kNumPresets) return;
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

    void showMasterVolumeContextMenu()
    {
        const int currentCC = app.settings.masterVolumeMidiCC;
        juce::PopupMenu menu;
        menu.addItem (1, "MIDI CC Learn", ! processor.isMidiControlCCLearnActive());
        menu.addItem (2,
                      currentCC >= 0 ? "Clear CC (" + juce::String (currentCC) + ")" : "Clear CC",
                      currentCC >= 0);
        if (currentCC >= 0)
        {
            menu.addSeparator();
            menu.addItem (0, "MIDI CC: " + juce::String (currentCC), false);
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
            [this] (int result)
            {
                if (result == 1)
                {
                    processor.setMidiControlCCLearnTarget (
                        BufferTestAudioProcessor::kMasterVolumeCCLearnTarget);
                }
                else if (result == 2)
                {
                    app.settings.masterVolumeMidiCC = -1;
                    processor.setMidiControlCCLearnTarget (-1);
                    updateMasterVolumeMidiTooltip();
                }
            });
    }

    void updateMasterVolumeMidiTooltip()
    {
        juce::String tooltip = "Master output volume. Right-click for MIDI mapping.";
        if (processor.getMidiControlCCLearnTarget()
            == BufferTestAudioProcessor::kMasterVolumeCCLearnTarget)
            tooltip = "Waiting for MIDI CC...";
        else if (app.settings.masterVolumeMidiCC >= 0)
            tooltip = "Master output volume - MIDI CC "
                    + juce::String (app.settings.masterVolumeMidiCC)
                    + ". Right-click for MIDI mapping.";
        masterVolumeSlider.setTooltip (tooltip);
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
    : juce::AudioProcessorEditor (&p), audioProcessor (p)
{
   #if JUCE_IOS
    // The standalone iPhone app always uses the production portrait session
    // layout. Device screens are taller than exactly 9:16, so the layout is
    // allowed to fill the available portrait bounds below.
    audioProcessor.getAppState().settings.windowLayoutMode = WindowLayoutMode::Portrait9x16;
   #endif

   #if JUCE_DEBUG
    debugPanel = std::make_unique<DebugPanelContent> (audioProcessor.getAppState());
    glyphLabPanel = std::make_unique<GlyphLabComponent> (audioProcessor.getAppState().settings);
   #endif

    content = std::make_unique<PluginEditorContent>(audioProcessor,
                                                   #if JUCE_DEBUG
                                                    &debugPanel->getModifierHistory()
                                                   #else
                                                    nullptr
                                                   #endif
                                                    );

    probabilityPanel = std::make_unique<ModifierProbabilityPanel>(
        audioProcessor.getAppState().settings.modifierProbabilities,
        audioProcessor.getAPVTS(),
        audioProcessor);

    settingsPanel = std::make_unique<SettingsPanelContent>(
        audioProcessor.getAppState().settings);

    // Wire up parts-changed callback — delegates to PluginEditorContent which
    // handles the transport-aware deferred logic (pendingPartsCount).
    settingsPanel->onPartsChanged = [this](int numParts)
    {
        static_cast<PluginEditorContent*>(content.get())->handlePartsChanged(numParts);
    };

    // Wire up bars-per-modifier callback (keeps PluginEditorContent in sync)
    settingsPanel->onBarsChanged = [] (int)
    {
        // Setting is already stored by SettingsPanelContent; nothing extra needed.
    };
    settingsPanel->onLayoutChanged = [this](WindowLayoutMode mode)
    {
       #if JUCE_IOS
        juce::ignoreUnused (mode);
        applyWindowLayout (WindowLayoutMode::Portrait9x16, false);
       #else
        applyWindowLayout (mode, true);
       #endif
    };

    setLookAndFeel(&editorLnf);

    helpPanel = std::make_unique<HelpPanelContent>();

   #if ! JUCE_IOS
    tabComponent = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    tabComponent->setOutline (0);
    auto tabBg = juce::Colour (0x00000000);  // transparent — BackgroundAnimator shows through
    tabComponent->addTab("Session",     tabBg, content.get(), false);
    tabComponent->addTab("Probability", tabBg, probabilityPanel.get(), false);
    tabComponent->addTab("Settings",    tabBg, settingsPanel.get(), false);
   #if JUCE_DEBUG
    tabComponent->addTab("Debug",       tabBg, debugPanel.get(), false);
    tabComponent->addTab("Glyph Lab",   tabBg, glyphLabPanel.get(), false);
   #endif
    tabComponent->addTab("Help",        tabBg, helpPanel.get(), false);

    // Style the tab bar
    auto& tabBar = tabComponent->getTabbedButtonBar();
    tabBar.setMinimumTabScaleFactor (0.55);
    tabBar.setColour(juce::TabbedButtonBar::tabOutlineColourId, Theme::border());
    tabBar.setColour(juce::TabbedButtonBar::frontOutlineColourId, Theme::accent());
   #endif

    // Background animator (sits behind everything)
    backgroundAnimator = std::make_unique<BackgroundAnimator>();
    addAndMakeVisible(backgroundAnimator.get());

    // Wire up reactive background pulse from modifier triggers
    static_cast<PluginEditorContent*>(content.get())->bgAnimator = backgroundAnimator.get();

   #if JUCE_IOS
    addAndMakeVisible (content.get());
    addChildComponent (probabilityPanel.get());
    addChildComponent (settingsPanel.get());
   #if JUCE_DEBUG
    addChildComponent (debugPanel.get());
    addChildComponent (glyphLabPanel.get());
   #endif
    addChildComponent (helpPanel.get());

    pageMenuButton.setButtonText ("SESSION  \xE2\x96\xBE");
    pageMenuButton.setTooltip ("Choose page");
    pageMenuButton.onClick = [this] { showIOSPageMenu(); };
    addAndMakeVisible (pageMenuButton);
   #else
    addAndMakeVisible(tabComponent.get());
   #endif

    // Apply the saved theme on editor open
    ThemeEngine::getInstance().setTheme (audioProcessor.getAppState().settings.themeName);

   #if JUCE_IOS
    pageMenuButton.setColour (juce::TextButton::buttonColourId, Theme::panelAlt());
    pageMenuButton.setColour (juce::TextButton::textColourOffId, Theme::text());
    pageMenuButton.setColour (juce::ComboBox::outlineColourId, Theme::border());
    setIOSPage (0);
   #endif

    setSize (1200, 800);
    setResizable(true, false);
    setResizeLimits(920, 600, 2400, 1600);
    applyWindowLayout (audioProcessor.getAppState().settings.windowLayoutMode, false);
}

BufferTestAudioProcessorEditor::~BufferTestAudioProcessorEditor()
{
    // Tear down child components before editorLnf is destroyed, so they
    // never reference a dangling LookAndFeel during their own destruction.
   #if ! JUCE_IOS
    tabComponent.reset();
   #endif
    content.reset();
    probabilityPanel.reset();
    settingsPanel.reset();
   #if JUCE_DEBUG
    debugPanel.reset();
    glyphLabPanel.reset();
   #endif
    helpPanel.reset();
    backgroundAnimator.reset();
    setLookAndFeel(nullptr);
}

void BufferTestAudioProcessorEditor::paint (juce::Graphics&)
{
    // No opaque fill — BackgroundAnimator paints the background
}

#if JUCE_IOS
juce::Component* BufferTestAudioProcessorEditor::getIOSPageComponent (int pageIndex) const
{
    switch (pageIndex)
    {
        case 0: return content.get();
        case 1: return probabilityPanel.get();
        case 2: return settingsPanel.get();
       #if JUCE_DEBUG
        case 3: return debugPanel.get();
        case 4: return glyphLabPanel.get();
        case 5: return helpPanel.get();
       #else
        case 3: return helpPanel.get();
       #endif
        default: return nullptr;
    }
}

void BufferTestAudioProcessorEditor::setIOSPage (int pageIndex)
{
   #if JUCE_DEBUG
    constexpr int pageCount = 6;
    static constexpr const char* pageNames[] =
        { "SESSION", "PROBABILITY", "SETTINGS", "DEBUG", "GLYPH LAB", "HELP" };
   #else
    constexpr int pageCount = 4;
    static constexpr const char* pageNames[] =
        { "SESSION", "PROBABILITY", "SETTINGS", "HELP" };
   #endif

    currentIOSPage = juce::jlimit (0, pageCount - 1, pageIndex);
    for (int i = 0; i < pageCount; ++i)
        if (auto* page = getIOSPageComponent (i))
            page->setVisible (i == currentIOSPage);

    pageMenuButton.setButtonText (
        juce::String (pageNames[currentIOSPage]) + "  \xE2\x96\xBE");
    pageMenuButton.toFront (false);
    resized();
}

void BufferTestAudioProcessorEditor::showIOSPageMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Session", true, currentIOSPage == 0);
    menu.addItem (2, "Probability", true, currentIOSPage == 1);
    menu.addItem (3, "Settings", true, currentIOSPage == 2);
   #if JUCE_DEBUG
    menu.addSeparator();
    menu.addItem (4, "Debug", true, currentIOSPage == 3);
    menu.addItem (5, "Glyph Lab", true, currentIOSPage == 4);
    menu.addItem (6, "Help", true, currentIOSPage == 5);
   #else
    menu.addItem (4, "Help", true, currentIOSPage == 3);
   #endif

    auto safeThis = juce::Component::SafePointer<BufferTestAudioProcessorEditor> (this);
    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (&pageMenuButton),
        [safeThis] (int result)
        {
            if (safeThis != nullptr && result > 0)
                safeThis->setIOSPage (result - 1);
        });
}
#endif

void BufferTestAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    if (backgroundAnimator)
        backgroundAnimator->setBounds(bounds);

   #if JUCE_IOS
    // The animated background may bleed under the sensor housing and home
    // indicator, but every interactive/content component stays in the safe
    // viewport.
    if (const auto* display =
            juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        bounds = display->safeAreaInsets.subtractedFrom (bounds);

    if (auto* page = getIOSPageComponent (currentIOSPage))
    {
        auto pageBounds = bounds;
        if (currentIOSPage != 0)
            pageBounds.removeFromTop (40);
        page->setBounds (pageBounds);
    }

    auto menuBounds = bounds.reduced (10, 6).removeFromTop (34);
    pageMenuButton.setBounds (menuBounds.removeFromRight (
        juce::jmin (122, juce::jmax (108, bounds.getWidth() / 3))));
    pageMenuButton.toFront (false);
   #else
    if (tabComponent)
    {
        tabComponent->setBounds(bounds);
        tabComponent->resized();
    }
   #endif
}

void BufferTestAudioProcessorEditor::parentSizeChanged()
{
    if (! synchronisePortraitBoundsWithHost())
        resized();
}

void BufferTestAudioProcessorEditor::applyWindowLayout (WindowLayoutMode mode,
                                                        bool restorePreviousSize)
{
    auto* sessionContent = static_cast<PluginEditorContent*> (content.get());
    const bool portrait = mode == WindowLayoutMode::Portrait9x16;
    activeWindowLayoutMode = mode;

    if (portrait)
    {
        if (getWidth() >= getHeight())
            previousResizableBounds = getBounds();

        if (auto* constrainer = getConstrainer())
        {
           #if JUCE_IOS
            // The standalone wrapper sizes the editor to the device's full
            // portrait viewport. Do not force an exact 9:16 window because
            // modern iPhones are taller and may also apply safe-area insets.
            constrainer->setFixedAspectRatio (0.0);
            constrainer->setSizeLimits (280, 480, 2048, 4096);
           #else
            // AudioProcessorEditor::setResizeLimits() immediately constrains
            // the current bounds. Applying it while the editor is still
            // landscape therefore produces an unwanted intermediate 450x800
            // host resize before setSize() requests 540x960. Some hosts leave
            // the tab bar laid out for that transient resize. Update the
            // existing constrainer in place so there is only one size change.
            constrainer->setSizeLimits (450, 800, 1080, 1920);
            constrainer->setFixedAspectRatio (9.0 / 16.0);
           #endif
        }

        sessionContent->setPortraitLayout (true);
       #if JUCE_IOS
        setSize (390, 844);
       #else
        setSize (540, 960);
        scheduleHostBoundsReconciliation();
       #endif
    }
    else
    {
        if (auto* constrainer = getConstrainer())
        {
            constrainer->setFixedAspectRatio (0.0);
            constrainer->setSizeLimits (920, 600, 2400, 1600);
        }

        sessionContent->setPortraitLayout (false);

        if (restorePreviousSize)
            setSize (juce::jlimit (920, 2400, previousResizableBounds.getWidth()),
                     juce::jlimit (600, 1600, previousResizableBounds.getHeight()));
    }
}

bool BufferTestAudioProcessorEditor::synchronisePortraitBoundsWithHost()
{
    if (activeWindowLayoutMode != WindowLayoutMode::Portrait9x16
        || synchronisingHostBounds)
        return false;

    const auto currentSize = juce::Point<int> (getWidth(), getHeight());
    juce::Point<int> hostSize;

    const auto isCompatiblePortraitViewport = [currentSize] (juce::Point<int> candidate)
    {
        if (candidate.x < 450 || candidate.y < 800
            || candidate.x > currentSize.x || candidate.y > currentSize.y)
            return false;

        return std::abs (static_cast<double> (candidate.x) / candidate.y
                         - 9.0 / 16.0) < 0.01;
    };

    const auto considerHostSize = [&] (juce::Point<int> candidate)
    {
        if (! isCompatiblePortraitViewport (candidate))
            return;

        if (hostSize.isOrigin()
            || static_cast<int64_t> (candidate.x) * candidate.y
                < static_cast<int64_t> (hostSize.x) * hostSize.y)
            hostSize = candidate;
    };

    if (auto* parent = getParentComponent())
        considerHostSize ({ parent->getWidth(), parent->getHeight() });

    const auto nativeHostSize = getNativeHostViewportSize (*this);
    considerHostSize (nativeHostSize);

    if (auto* peer = getPeer())
        considerHostSize ({ peer->getBounds().getWidth(), peer->getBounds().getHeight() });

    if (hostSize.isOrigin())
        return false;

    const auto targetBounds = juce::Rectangle<int> (0, 0, hostSize.x, hostSize.y);
    const bool editorNeedsBounds = getBounds() != targetBounds;
    bool peerNeedsBounds = false;

    if (nativeHostSize == hostSize)
        if (auto* peer = getPeer())
            peerNeedsBounds = peer->getBounds() != targetBounds;

    if (! editorNeedsBounds && ! peerNeedsBounds)
        return false;

    const juce::ScopedValueSetter<bool> synchronising (synchronisingHostBounds, true);
    if (editorNeedsBounds)
        setBounds (targetBounds);

    // Matching only the editor size leaves a bottom-aligned native peer at
    // y = -30: the tabs stay clipped and an equal black strip appears below.
    if (nativeHostSize == hostSize)
        if (auto* peer = getPeer())
            if (peer->getBounds() != targetBounds)
                peer->setBounds (targetBounds, false);

    return true;
}

void BufferTestAudioProcessorEditor::scheduleHostBoundsReconciliation()
{
    juce::Component::SafePointer<BufferTestAudioProcessorEditor> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis != nullptr)
            safeThis->synchronisePortraitBoundsWithHost();
    });
    juce::Timer::callAfterDelay (150, [safeThis]
    {
        if (safeThis != nullptr)
            safeThis->synchronisePortraitBoundsWithHost();
    });
}
