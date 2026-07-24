/*
 ==============================================================================
   ModifierProbabilityPanel.h
   --------------------------------------------------------------------------
   Scrollable UI panel showing one row per modifier type with:
     - A probability slider bound to an AudioProcessorValueTreeState parameter
       (exposes DAW automation on the host side).
   Rows are grouped by category with section headers.
   A "Pad Target Probability" section at the bottom controls per-pad weighting.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ModifierProbabilityManager.h"
#include "ProbabilityPresetManager.h"
#include "PluginProcessor.h"
#include "ThemeEngine.h"
#include "ThemeFonts.h"
#include <vector>

class ModifierProbabilityPanel : public juce::Component,
                                 public ThemeListener,
                                 private juce::Timer
{
public:
    // -------------------------------------------------------------------------
    ModifierProbabilityPanel (ModifierProbabilityManager& manager,
                              juce::AudioProcessorValueTreeState& apvtsRef,
                              BufferTestAudioProcessor& processorRef)
        : probManager (manager)
        , apvts (apvtsRef)
        , processor (processorRef)
    {
        ThemeEngine::getInstance().addListener (this);
        buildRows();
        buildPadRows();
        buildPresetBar();

        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        // Catch clicks on empty areas to cancel learn mode
        content.addMouseListener (this, true);

        zeroButton.setButtonText ("SET ALL 0%");
        zeroButton.setTooltip ("Set every modifier and pad-target probability to 0%.");
        zeroButton.onClick = [this] { setAllProbabilities (0.0f); };
        addAndMakeVisible (zeroButton);
        zeroButton.addMouseListener (this, false);

        randomizeButton.setButtonText ("RANDOMIZE");
        randomizeButton.setTooltip ("Assign a new random value to every modifier and pad-target probability.");
        randomizeButton.onClick = [this] { randomizeAllProbabilities(); };
        addAndMakeVisible (randomizeButton);
        randomizeButton.addMouseListener (this, false);

        resetButton.setButtonText ("SET ALL 100%");
        resetButton.setTooltip ("Set every modifier and pad-target probability to 100%.");
        resetButton.onClick = [this] { resetAllToDefault(); };
        addAndMakeVisible (resetButton);
        resetButton.addMouseListener (this, false);

        applyTheme();
        startTimerHz (10);
    }

    ~ModifierProbabilityPanel() override
    {
        stopTimer();
        ThemeEngine::getInstance().removeListener (this);
        for (auto& row : rows)
            if (row.slider) row.slider->removeMouseListener (this);
        for (auto& pr : padRows)
            if (pr.slider) pr.slider->removeMouseListener (this);
        zeroButton.removeMouseListener (this);
        randomizeButton.removeMouseListener (this);
        resetButton.removeMouseListener (this);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14, 12);
        const bool compact = area.getWidth() < 700;

        // Presets live in a compact instrument strip rather than a generic toolbar.
        presetStripBounds = area.removeFromTop (compact ? 92 : 52);
        auto presetBarArea = presetStripBounds.reduced (12, 9);
        if (compact)
        {
            presetBarArea.removeFromTop (32);
            const int comboW = juce::jmax (104, juce::roundToInt (presetBarArea.getWidth() * 0.34f));
            presetCombo.setBounds (presetBarArea.removeFromLeft (comboW));
            presetBarArea.removeFromLeft (4);
            const int buttonW = (presetBarArea.getWidth() - 8) / 3;
            presetSaveBtn.setBounds (presetBarArea.removeFromLeft (buttonW));
            presetBarArea.removeFromLeft (4);
            presetSaveAsBtn.setBounds (presetBarArea.removeFromLeft (buttonW));
            presetBarArea.removeFromLeft (4);
            presetDeleteBtn.setBounds (presetBarArea);
        }
        else
        {
            presetBarArea.removeFromLeft (146);
            presetCombo.setBounds (presetBarArea.removeFromLeft (juce::jmin (240, presetBarArea.getWidth() / 2)));
            presetBarArea.removeFromLeft (6);
            presetSaveBtn.setBounds (presetBarArea.removeFromLeft (64));
            presetBarArea.removeFromLeft (6);
            presetSaveAsBtn.setBounds (presetBarArea.removeFromLeft (78));
            presetBarArea.removeFromLeft (6);
            presetDeleteBtn.setBounds (presetBarArea.removeFromLeft (68));
        }

        area.removeFromTop (10);
        actionStripBounds = area.removeFromBottom (compact ? 82 : 48);
        auto actions = actionStripBounds.reduced (12, 8);
        if (compact)
        {
            actions.removeFromTop (28);
            const int buttonW = (actions.getWidth() - 8) / 3;
            zeroButton.setBounds (actions.removeFromLeft (buttonW));
            actions.removeFromLeft (4);
            randomizeButton.setBounds (actions.removeFromLeft (buttonW));
            actions.removeFromLeft (4);
            resetButton.setBounds (actions);
        }
        else
        {
            resetButton.setBounds (actions.removeFromRight (118));
            actions.removeFromRight (6);
            randomizeButton.setBounds (actions.removeFromRight (112));
            actions.removeFromRight (6);
            zeroButton.setBounds (actions.removeFromRight (106));
        }

        area.removeFromBottom (10);
        viewportBounds = area;
        viewport.setBounds (viewportBounds);
        layoutContent();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();

        auto drawModule = [&] (juce::Rectangle<int> bounds)
        {
            g.setColour (palette.panel);
            g.fillRect (bounds);
            g.setColour (palette.border.withAlpha (0.75f));
            g.drawRect (bounds, 1);
        };

        drawModule (presetStripBounds);
        drawModule (viewportBounds);
        drawModule (actionStripBounds);

        g.setColour (palette.accent1);
        g.fillRect (presetStripBounds.getX(), presetStripBounds.getY(), 4, presetStripBounds.getHeight());
        g.setFont (ThemeFonts::getInstance().headingFont (14.0f));
        const bool compact = presetStripBounds.getWidth() < 700;
        auto presetHeading = presetStripBounds.reduced (14, 0);
        if (compact)
            presetHeading = presetHeading.removeFromTop (38);
        else
            presetHeading = presetHeading.removeFromLeft (132);
        g.drawText ("PROBABILITY BANK",
                    presetHeading,
                    juce::Justification::centredLeft);

        g.setColour (palette.textSecondary);
        g.setFont (ThemeFonts::getInstance().monoFont (11.0f));
        auto actionHeading = actionStripBounds.reduced (12, 0);
        if (compact)
            actionHeading = actionHeading.removeFromTop (34);
        g.drawText ("BULK VALUES", actionHeading,
                    juce::Justification::centredLeft);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        // The Viewport and its content paint after paint(), so draw this module
        // rule in the foreground to keep the central panel outline visible.
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();
        g.setColour (palette.border.withAlpha (0.75f));
        g.drawRect (viewportBounds, 1);
    }

    void themeChanged() override
    {
        applyTheme();
        repaint();
    }

    /** Push current weights back into APVTS (e.g. after an external preset load). */
    void refreshFromManager()
    {
        const auto& types = ModifierProbabilityManager::allModifierTypes();
        for (auto type : types)
        {
            const juce::String id = "prob_" + juce::String (static_cast<int> (type));
            if (auto* param = apvts.getParameter (id))
                param->setValueNotifyingHost (probManager.getWeight (type));
        }
    }

private:
    // -------------------------------------------------------------------------
    // Slider subclass that ignores right-clicks so they only trigger the popup menu
    struct ProbSlider : public juce::Slider
    {
        using juce::Slider::Slider;
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

    struct ActionButton : public juce::TextButton
    {
        using juce::TextButton::TextButton;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) return;
            juce::TextButton::mouseDown (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) return;
            juce::TextButton::mouseDrag (e);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) return;
            juce::TextButton::mouseUp (e);
        }
    };

    struct Row
    {
        ModifierType type;
        int  paramIndex = 0;
        bool showCategoryHeader = false;
        juce::String category;

        std::unique_ptr<juce::Label>   categoryLabel;
        std::unique_ptr<juce::Label>   nameLabel;
        std::unique_ptr<ProbSlider>    slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::Label>   valueLabel;
        std::unique_ptr<juce::Label>   ccLabel;
    };

    ModifierProbabilityManager&             probManager;
    juce::AudioProcessorValueTreeState&     apvts;
    BufferTestAudioProcessor&               processor;
    juce::Viewport                          viewport;
    struct SurfaceContent : public juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            const auto& palette = ThemeEngine::getInstance().getCurrentPalette();
            g.fillAll (palette.panel);
        }
    } content;
    std::vector<Row>                        rows;
    ActionButton                            zeroButton;
    ActionButton                            randomizeButton;
    ActionButton                            resetButton;
    juce::Rectangle<int>                    presetStripBounds;
    juce::Rectangle<int>                    viewportBounds;
    juce::Rectangle<int>                    actionStripBounds;

    // Probability preset bar
    ProbabilityPresetManager                presetManager;
    juce::ComboBox                          presetCombo;
    juce::TextButton                        presetSaveBtn;
    juce::TextButton                        presetSaveAsBtn;
    juce::TextButton                        presetDeleteBtn;
    juce::String                            currentPresetName;

    // Per-pad target probability rows
    struct PadRow
    {
        int padIndex = 0;
        std::unique_ptr<juce::Label>   nameLabel;
        std::unique_ptr<ProbSlider>    slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::Label>   valueLabel;
        std::unique_ptr<juce::Label>   ccLabel;
    };
    std::vector<PadRow> padRows;
    juce::Label padSectionLabel;

    // -------------------------------------------------------------------------
    void buildRows()
    {
        const auto& allTypes  = ModifierProbabilityManager::visibleModifierTypes();
        juce::String lastCategory;

        for (size_t i = 0; i < allTypes.size(); ++i)
        {
            const auto type = allTypes[i];
            Row row;
            row.type       = type;
            // MIDI/automation mappings retain the serialized enum index even
            // when legacy probability rows are hidden.
            row.paramIndex = static_cast<int> (type);
            row.category   = ModifierProbabilityManager::getCategory (type);
            row.showCategoryHeader = (row.category != lastCategory);
            lastCategory = row.category;

            // -- Category header --
            row.categoryLabel = std::make_unique<juce::Label>();
            row.categoryLabel->setText (row.category.toUpperCase(), juce::dontSendNotification);
            row.categoryLabel->setFont (ThemeFonts::getInstance().headingFont (15.0f));
            row.categoryLabel->setColour (juce::Label::textColourId, Theme::text());
            content.addAndMakeVisible (row.categoryLabel.get());

            // -- Modifier name --
            row.nameLabel = std::make_unique<juce::Label>();
            row.nameLabel->setText (ModifierProbabilityManager::getDisplayName (type).toUpperCase(),
                                    juce::dontSendNotification);
            row.nameLabel->setFont (ThemeFonts::getInstance().bodyFont (14.0f));
            row.nameLabel->setColour (juce::Label::textColourId, Theme::textSubtle());
            content.addAndMakeVisible (row.nameLabel.get());

            // -- Probability slider --
            row.slider = std::make_unique<ProbSlider> (juce::Slider::LinearHorizontal,
                                                       juce::Slider::NoTextBox);
            row.slider->setRange (0.0, 1.0, 0.01);
            row.slider->setColour (juce::Slider::trackColourId,       Theme::accent());
            row.slider->setColour (juce::Slider::backgroundColourId,  Theme::panelAlt());
            content.addAndMakeVisible (row.slider.get());

            // SliderAttachment binds to APVTS param: handles DAW automation &
            // thread-safe updates automatically.
            const juce::String paramId = "prob_" + juce::String (static_cast<int> (type));
            row.sliderAttachment =
                std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                    apvts, paramId, *row.slider);

            // -- Numeric value label (updated by timer) --
            row.valueLabel = std::make_unique<juce::Label>();
            row.valueLabel->setFont (ThemeFonts::getInstance().monoFont (13.0f));
            row.valueLabel->setColour (juce::Label::textColourId, Theme::textSubtle());
            row.valueLabel->setJustificationType (juce::Justification::centredLeft);
            content.addAndMakeVisible (row.valueLabel.get());
            updateValueLabel (row);

            // -- CC assignment label --
            row.ccLabel = std::make_unique<juce::Label>();
            row.ccLabel->setFont (ThemeFonts::getInstance().monoFont (11.0f));
            row.ccLabel->setColour (juce::Label::textColourId, Theme::textSubtle());
            row.ccLabel->setJustificationType (juce::Justification::centred);
            content.addAndMakeVisible (row.ccLabel.get());
            updateCCLabel (row);

            // Right-click for MIDI CC learn
            row.slider->addMouseListener (this, false);

            rows.push_back (std::move (row));
        }
    }

    // ── Preset bar ─────────────────────────────────────────────────────
    void buildPresetBar()
    {
        presetCombo.setTextWhenNothingSelected ("(no preset)");
        presetCombo.setTextWhenNoChoicesAvailable ("(no presets saved)");
        presetCombo.onChange = [this] { onPresetSelected(); };
        addAndMakeVisible (presetCombo);
        rebuildPresetCombo();

        presetSaveBtn.setButtonText ("SAVE");
        presetSaveBtn.onClick = [this] { onSavePreset(); };
        addAndMakeVisible (presetSaveBtn);

        presetSaveAsBtn.setButtonText ("SAVE AS");
        presetSaveAsBtn.onClick = [this] { onSaveAsPreset(); };
        addAndMakeVisible (presetSaveAsBtn);

        presetDeleteBtn.setButtonText ("DELETE");
        presetDeleteBtn.onClick = [this] { onDeletePreset(); };
        addAndMakeVisible (presetDeleteBtn);
    }

    void rebuildPresetCombo()
    {
        presetCombo.clear (juce::dontSendNotification);
        const auto& names = presetManager.getPresetNames();
        for (int i = 0; i < names.size(); ++i)
            presetCombo.addItem (names[i], i + 1);

        // Re-select the current preset if it still exists
        if (currentPresetName.isNotEmpty())
        {
            const int idx = names.indexOf (currentPresetName);
            if (idx >= 0)
                presetCombo.setSelectedId (idx + 1, juce::dontSendNotification);
            else
                currentPresetName.clear();
        }
    }

    ProbabilityPreset captureCurrentSettings() const
    {
        ProbabilityPreset preset;
        preset.name = currentPresetName;

        for (auto type : ModifierProbabilityManager::allModifierTypes())
        {
            const juce::String id = "prob_" + juce::String (static_cast<int> (type));
            float w = 1.0f;
            if (auto* rawVal = apvts.getRawParameterValue (id))
                w = rawVal->load();
            preset.modifierWeights[type] = w;
        }

        for (int i = 0; i < 8; ++i)
        {
            const juce::String id = "padProb_" + juce::String (i);
            float w = 1.0f;
            if (auto* rawVal = apvts.getRawParameterValue (id))
                w = rawVal->load();
            preset.padProbabilities[static_cast<size_t> (i)] = w;
        }

        return preset;
    }

    void applyPreset (const ProbabilityPreset& preset)
    {
        for (auto type : ModifierProbabilityManager::allModifierTypes())
        {
            const juce::String id = "prob_" + juce::String (static_cast<int> (type));
            auto it = preset.modifierWeights.find (type);
            float w = (it != preset.modifierWeights.end()) ? it->second : 1.0f;
            if (auto* param = apvts.getParameter (id))
                param->setValueNotifyingHost (w);
        }

        for (int i = 0; i < 8; ++i)
        {
            const juce::String id = "padProb_" + juce::String (i);
            if (auto* param = apvts.getParameter (id))
                param->setValueNotifyingHost (preset.padProbabilities[static_cast<size_t> (i)]);
        }
    }

    void onPresetSelected()
    {
        const int sel = presetCombo.getSelectedId();
        if (sel <= 0) return;

        const auto& names = presetManager.getPresetNames();
        const int idx = sel - 1;
        if (idx < 0 || idx >= names.size()) return;

        if (auto preset = presetManager.loadPreset (names[idx]))
        {
            currentPresetName = preset->name;
            applyPreset (*preset);
        }
    }

    void onSavePreset()
    {
        if (currentPresetName.isEmpty())
        {
            onSaveAsPreset();
            return;
        }

        auto preset = captureCurrentSettings();
        preset.name = currentPresetName;
        presetManager.savePreset (preset);
        rebuildPresetCombo();
    }

    void onSaveAsPreset()
    {
        auto* aw = new juce::AlertWindow ("Save Probability Preset",
                                           "Enter a name for this preset:",
                                           juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor ("name", currentPresetName.isNotEmpty() ? currentPresetName
                                                                   : "My Preset");
        aw->addButton ("Save",   1);
        aw->addButton ("Cancel", 0);

        aw->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, aw] (int result)
            {
                if (result == 1)
                {
                    auto name = aw->getTextEditorContents ("name").trim();
                    if (name.isNotEmpty())
                    {
                        // Check if overwriting
                        bool exists = presetManager.getPresetNames().contains (name);
                        if (exists)
                        {
                            // Confirm overwrite
                            juce::AlertWindow::showOkCancelBox (
                                juce::MessageBoxIconType::WarningIcon,
                                "Overwrite Preset?",
                                "A preset named \"" + name + "\" already exists. Overwrite?",
                                "Overwrite", "Cancel", nullptr,
                                juce::ModalCallbackFunction::create (
                                    [this, name] (int confirmResult)
                                    {
                                        if (confirmResult == 1)
                                        {
                                            currentPresetName = name;
                                            auto preset = captureCurrentSettings();
                                            preset.name = name;
                                            presetManager.savePreset (preset);
                                            rebuildPresetCombo();
                                        }
                                    }));
                        }
                        else
                        {
                            currentPresetName = name;
                            auto preset = captureCurrentSettings();
                            preset.name = name;
                            presetManager.savePreset (preset);
                            rebuildPresetCombo();
                        }
                    }
                }
                delete aw;
            }), true);
    }

    void onDeletePreset()
    {
        if (currentPresetName.isEmpty()) return;

        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::WarningIcon,
            "Delete Preset?",
            "Delete \"" + currentPresetName + "\"?",
            "Delete", "Cancel", nullptr,
            juce::ModalCallbackFunction::create (
                [this] (int result)
                {
                    if (result == 1)
                    {
                        presetManager.deletePreset (currentPresetName);
                        currentPresetName.clear();
                        presetCombo.setSelectedId (0, juce::dontSendNotification);
                        rebuildPresetCombo();
                    }
                }));
    }

    void buildPadRows()
    {
        padSectionLabel.setText ("PAD TARGET PROBABILITY", juce::dontSendNotification);
        padSectionLabel.setFont (ThemeFonts::getInstance().headingFont (15.0f));
        padSectionLabel.setColour (juce::Label::textColourId, Theme::text());
        content.addAndMakeVisible (padSectionLabel);

        for (int i = 0; i < 8; ++i)
        {
            PadRow pr;
            pr.padIndex = i;

            pr.nameLabel = std::make_unique<juce::Label>();
            pr.nameLabel->setText ("PAD " + juce::String (i + 1), juce::dontSendNotification);
            pr.nameLabel->setFont (ThemeFonts::getInstance().bodyFont (14.0f));
            pr.nameLabel->setColour (juce::Label::textColourId, Theme::textSubtle());
            content.addAndMakeVisible (pr.nameLabel.get());

            pr.slider = std::make_unique<ProbSlider> (juce::Slider::LinearHorizontal,
                                                        juce::Slider::NoTextBox);
            pr.slider->setRange (0.0, 1.0, 0.01);
            pr.slider->setColour (juce::Slider::trackColourId,       Theme::accent());
            pr.slider->setColour (juce::Slider::backgroundColourId,  Theme::panelAlt());
            content.addAndMakeVisible (pr.slider.get());

            const juce::String paramId = "padProb_" + juce::String (i);
            pr.sliderAttachment =
                std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                    apvts, paramId, *pr.slider);

            pr.valueLabel = std::make_unique<juce::Label>();
            pr.valueLabel->setFont (ThemeFonts::getInstance().monoFont (13.0f));
            pr.valueLabel->setColour (juce::Label::textColourId, Theme::textSubtle());
            pr.valueLabel->setJustificationType (juce::Justification::centredLeft);
            content.addAndMakeVisible (pr.valueLabel.get());
            updatePadValueLabel (pr);

            // -- CC assignment label --
            pr.ccLabel = std::make_unique<juce::Label>();
            pr.ccLabel->setFont (ThemeFonts::getInstance().monoFont (11.0f));
            pr.ccLabel->setColour (juce::Label::textColourId, Theme::textSubtle());
            pr.ccLabel->setJustificationType (juce::Justification::centred);
            content.addAndMakeVisible (pr.ccLabel.get());
            updatePadCCLabel (pr);

            // Right-click for MIDI CC learn
            pr.slider->addMouseListener (this, false);

            padRows.push_back (std::move (pr));
        }
    }

    // -------------------------------------------------------------------------
    void timerCallback() override
    {
        if (!isShowing()) return;

        // Refresh value labels from live APVTS atomics
        for (auto& row : rows)
            updateValueLabel (row);

        // Refresh pad probability value labels
        for (auto& pr : padRows)
            updatePadValueLabel (pr);

        // Poll for MIDI CC learn completion (modifier prob sliders)
        {
            const int learnedCC = processor.checkAndClearLearnedCC();
            if (learnedCC >= 0)
            {
                const int paramIdx = processor.getMidiCCLearnParamIndex();
                if (paramIdx >= 0 && paramIdx < static_cast<int> (rows.size()))
                {
                    auto& settings = processor.getAppState().settings;
                    settings.midiProbCCMap[static_cast<size_t> (paramIdx)] = learnedCC;
                    updateCCLabel (rows[static_cast<size_t> (paramIdx)]);
                }
                processor.setMidiCCLearnMode (-1);
            }
        }

        // Poll for MIDI CC learn completion (pad target prob sliders)
        {
            const int learnedCC = processor.checkAndClearLearnedPadProbCC();
            if (learnedCC >= 0)
            {
                const int padIdx = processor.getMidiPadProbCCLearnIndex();
                if (padIdx >= 0 && padIdx < static_cast<int> (padRows.size()))
                {
                    auto& settings = processor.getAppState().settings;
                    settings.midiPadProbCCMap[static_cast<size_t> (padIdx)] = learnedCC;
                    updatePadCCLabel (padRows[static_cast<size_t> (padIdx)]);
                }
                processor.setMidiPadProbCCLearnMode (-1);
            }
        }

        // Update LEARN indicator while learn mode is active
        refreshLearnIndicators();
        updateActionButtonTooltips();
    }

    void updateValueLabel (Row& row)
    {
        const juce::String id = "prob_" + juce::String (static_cast<int> (row.type));
        float w = 1.0f;
        if (auto* rawVal = apvts.getRawParameterValue (id))
            w = rawVal->load();
        const int pct = juce::roundToInt (w * 100.0f);
        row.valueLabel->setText (pct <= 0 ? "OFF"
                                          : juce::String (pct) + "%",
                                 juce::dontSendNotification);
    }

    void updateCCLabel (Row& row)
    {
        auto& settings = processor.getAppState().settings;
        const auto idx = static_cast<size_t> (row.paramIndex);
        if (idx < settings.midiProbCCMap.size())
        {
            const int cc = settings.midiProbCCMap[idx];
            row.ccLabel->setText (cc >= 0 ? "CC" + juce::String (cc) : "",
                                  juce::dontSendNotification);
        }
    }

    void updatePadCCLabel (PadRow& pr)
    {
        auto& settings = processor.getAppState().settings;
        const auto idx = static_cast<size_t> (pr.padIndex);
        if (idx < settings.midiPadProbCCMap.size())
        {
            const int cc = settings.midiPadProbCCMap[idx];
            pr.ccLabel->setText (cc >= 0 ? "CC" + juce::String (cc) : "",
                                 juce::dontSendNotification);
        }
    }

    // -- Right-click handling via mouseDown -------

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
        {
            // Any left-click cancels an active learn mode
            if (processor.isMidiCCLearnActive())
                processor.setMidiCCLearnMode (-1);
            if (processor.isMidiPadProbCCLearnActive())
                processor.setMidiPadProbCCLearnMode (-1);
            if (processor.isMidiControlCCLearnActive())
                processor.setMidiControlCCLearnTarget (-1);
            if (processor.isMidiLearnEnabled())
                processor.setMidiLearnMode (false, -1);
            return;
        }

        if (e.eventComponent == &zeroButton)
        {
            showActionNoteMenu (BufferTestAudioProcessor::kProbabilityActionZero);
            return;
        }
        if (e.eventComponent == &randomizeButton)
        {
            showActionNoteMenu (BufferTestAudioProcessor::kProbabilityActionRandomize);
            return;
        }
        if (e.eventComponent == &resetButton)
        {
            showActionNoteMenu (BufferTestAudioProcessor::kProbabilityActionReset);
            return;
        }

        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (e.eventComponent == rows[i].slider.get())
            {
                showModifierCCMenu (static_cast<int> (i));
                return;
            }
        }
        for (size_t i = 0; i < padRows.size(); ++i)
        {
            if (e.eventComponent == padRows[i].slider.get())
            {
                showPadCCMenu (static_cast<int> (i));
                return;
            }
        }
    }

    void refreshLearnIndicators()
    {
        const int learnParam = processor.getMidiCCLearnParamIndex();
        for (auto& row : rows)
        {
            if (row.paramIndex == learnParam)
            {
                row.ccLabel->setText ("LEARN", juce::dontSendNotification);
                row.ccLabel->setColour (juce::Label::textColourId, Theme::accent());
            }
            else
            {
                row.ccLabel->setColour (juce::Label::textColourId, Theme::textSubtle());
                updateCCLabel (row);
            }
        }

        const int learnPad = processor.getMidiPadProbCCLearnIndex();
        for (auto& pr : padRows)
        {
            if (pr.padIndex == learnPad)
            {
                pr.ccLabel->setText ("LEARN", juce::dontSendNotification);
                pr.ccLabel->setColour (juce::Label::textColourId, Theme::accent());
            }
            else
            {
                pr.ccLabel->setColour (juce::Label::textColourId, Theme::textSubtle());
                updatePadCCLabel (pr);
            }
        }
    }

    // ── Right-click popup menus ──────────────────────────────────────────

    void showActionNoteMenu (int actionIndex)
    {
        if (! juce::isPositiveAndBelow (actionIndex, SessionSettings::kNumProbabilityActions))
            return;

        auto& mappings = processor.getAppState().settings.midiProbabilityActionNoteMap;
        const int currentNote = mappings[static_cast<size_t> (actionIndex)];

        juce::PopupMenu menu;
        menu.addItem (1, "MIDI Learn", ! processor.isMidiLearnEnabled());
        menu.addItem (2,
                      currentNote >= 0 ? "Clear MIDI Note (" + juce::String (currentNote) + ")"
                                       : "Clear MIDI Note",
                      currentNote >= 0);
        if (currentNote >= 0)
        {
            menu.addSeparator();
            menu.addItem (0, "MIDI Note: " + juce::String (currentNote), false);
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
            [this, actionIndex] (int result)
            {
                if (result == 1)
                {
                    processor.setMidiLearnMode (
                        true,
                        BufferTestAudioProcessor::kProbabilityActionLearnIndexBase + actionIndex);
                }
                else if (result == 2)
                {
                    processor.getAppState().settings
                        .midiProbabilityActionNoteMap[static_cast<size_t> (actionIndex)] = -1;
                    processor.setMidiLearnMode (false, -1);
                    updateActionButtonTooltips();
                }
            });
    }

    void updateActionButtonTooltips()
    {
        const auto& mappings = processor.getAppState().settings.midiProbabilityActionNoteMap;
        const int learningTarget = processor.isMidiLearnEnabled()
            ? processor.getMidiLearnPadIndex() : -1;
        auto setTooltip = [&] (ActionButton& button, int actionIndex, const juce::String& description)
        {
            juce::String suffix;
            if (learningTarget == BufferTestAudioProcessor::kProbabilityActionLearnIndexBase + actionIndex)
                suffix = " Waiting for MIDI note...";
            else if (mappings[static_cast<size_t> (actionIndex)] >= 0)
                suffix = " MIDI note " + juce::String (mappings[static_cast<size_t> (actionIndex)]) + ".";
            button.setTooltip (description + suffix + " Right-click for MIDI mapping.");
        };

        setTooltip (zeroButton, BufferTestAudioProcessor::kProbabilityActionZero,
                    "Set every modifier and pad-target probability to 0%.");
        setTooltip (randomizeButton, BufferTestAudioProcessor::kProbabilityActionRandomize,
                    "Assign a new random value to every modifier and pad-target probability.");
        setTooltip (resetButton, BufferTestAudioProcessor::kProbabilityActionReset,
                    "Set every modifier and pad-target probability to 100%.");
    }

    void showModifierCCMenu (int paramIndex)
    {
        if (paramIndex < 0 || static_cast<size_t> (paramIndex) >= processor.getAppState().settings.midiProbCCMap.size())
            return;

        auto& settings = processor.getAppState().settings;
        const int currentCC = settings.midiProbCCMap[static_cast<size_t> (paramIndex)];

        juce::PopupMenu menu;
        menu.addItem (1, "MIDI CC Learn", ! processor.isMidiCCLearnActive());
        if (currentCC >= 0)
            menu.addItem (2, "Clear CC (" + juce::String (currentCC) + ")");
        else
            menu.addItem (2, "Clear CC", false);

        menu.showMenuAsync (juce::PopupMenu::Options(),
            [this, paramIndex] (int result)
            {
                if (result == 1)
                {
                    processor.setMidiCCLearnMode (paramIndex);
                }
                else if (result == 2)
                {
                    processor.getAppState().settings.midiProbCCMap[static_cast<size_t> (paramIndex)] = -1;
                    processor.setMidiCCLearnMode (-1);
                    updateCCLabel (rows[static_cast<size_t> (paramIndex)]);
                }
            });
    }

    void showPadCCMenu (int padIndex)
    {
        if (padIndex < 0 || static_cast<size_t> (padIndex) >= processor.getAppState().settings.midiPadProbCCMap.size())
            return;

        auto& settings = processor.getAppState().settings;
        const int currentCC = settings.midiPadProbCCMap[static_cast<size_t> (padIndex)];

        juce::PopupMenu menu;
        menu.addItem (1, "MIDI CC Learn", ! processor.isMidiPadProbCCLearnActive());
        if (currentCC >= 0)
            menu.addItem (2, "Clear CC (" + juce::String (currentCC) + ")");
        else
            menu.addItem (2, "Clear CC", false);

        menu.showMenuAsync (juce::PopupMenu::Options(),
            [this, padIndex] (int result)
            {
                if (result == 1)
                {
                    processor.setMidiPadProbCCLearnMode (padIndex);
                }
                else if (result == 2)
                {
                    processor.getAppState().settings.midiPadProbCCMap[static_cast<size_t> (padIndex)] = -1;
                    processor.setMidiPadProbCCLearnMode (-1);
                    updatePadCCLabel (padRows[static_cast<size_t> (padIndex)]);
                }
            });
    }

    void resetAllToDefault()
    {
        setAllProbabilities (1.0f);
    }

    void setAllProbabilities (float value)
    {
        const auto& types = ModifierProbabilityManager::allModifierTypes();
        for (auto type : types)
        {
            const juce::String id = "prob_" + juce::String (static_cast<int> (type));
            if (auto* param = apvts.getParameter (id))
                param->setValueNotifyingHost (value);
        }
        for (int i = 0; i < 8; ++i)
        {
            const juce::String id = "padProb_" + juce::String (i);
            if (auto* param = apvts.getParameter (id))
                param->setValueNotifyingHost (value);
        }
    }

    void randomizeAllProbabilities()
    {
        const auto& types = ModifierProbabilityManager::allModifierTypes();
        for (auto type : types)
        {
            const juce::String id = "prob_" + juce::String (static_cast<int> (type));
            if (auto* param = apvts.getParameter (id))
                param->setValueNotifyingHost (probabilityRandom.nextFloat());
        }

        for (int i = 0; i < 8; ++i)
        {
            const juce::String id = "padProb_" + juce::String (i);
            if (auto* param = apvts.getParameter (id))
                param->setValueNotifyingHost (probabilityRandom.nextFloat());
        }

        processor.requestProbabilityQueueRefresh();
    }

    void applyTheme()
    {
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();

        auto styleButton = [&] (juce::TextButton& button, bool signal = false)
        {
            button.setColour (juce::TextButton::buttonColourId,
                              signal ? palette.accent1 : palette.panelAlt);
            button.setColour (juce::TextButton::buttonOnColourId,
                              signal ? palette.accent1 : palette.panelAlt);
            button.setColour (juce::TextButton::textColourOffId,
                              signal ? palette.textOnAccent : palette.textPrimary);
            button.setColour (juce::TextButton::textColourOnId,
                              signal ? palette.textOnAccent : palette.textPrimary);
        };

        auto styleSlider = [&] (juce::Slider& slider)
        {
            slider.setColour (juce::Slider::trackColourId, palette.accent1);
            slider.setColour (juce::Slider::backgroundColourId, palette.panelAlt);
            slider.setColour (juce::Slider::thumbColourId, palette.accent1);
        };

        for (auto& row : rows)
        {
            row.categoryLabel->setColour (juce::Label::textColourId, palette.textPrimary);
            row.categoryLabel->setColour (juce::Label::backgroundColourId, palette.panelAlt);
            row.nameLabel->setColour (juce::Label::textColourId, palette.textSecondary);
            row.valueLabel->setColour (juce::Label::textColourId, palette.textPrimary);
            row.ccLabel->setColour (juce::Label::textColourId, palette.textSecondary);
            styleSlider (*row.slider);
        }

        padSectionLabel.setColour (juce::Label::textColourId, palette.textPrimary);
        padSectionLabel.setColour (juce::Label::backgroundColourId, palette.panelAlt);
        for (auto& row : padRows)
        {
            row.nameLabel->setColour (juce::Label::textColourId, palette.textSecondary);
            row.valueLabel->setColour (juce::Label::textColourId, palette.textPrimary);
            row.ccLabel->setColour (juce::Label::textColourId, palette.textSecondary);
            styleSlider (*row.slider);
        }

        presetCombo.setColour (juce::ComboBox::backgroundColourId, palette.panel);
        presetCombo.setColour (juce::ComboBox::outlineColourId, palette.border);
        presetCombo.setColour (juce::ComboBox::textColourId, palette.textPrimary);
        presetCombo.setColour (juce::ComboBox::arrowColourId, palette.textSecondary);

        styleButton (presetSaveBtn);
        styleButton (presetSaveAsBtn);
        styleButton (presetDeleteBtn);
        styleButton (zeroButton);
        styleButton (randomizeButton);
        styleButton (resetButton, true);

        viewport.setColour (juce::ScrollBar::thumbColourId, palette.textSecondary);
        content.repaint();
    }

    void updatePadValueLabel (PadRow& pr)
    {
        const juce::String id = "padProb_" + juce::String (pr.padIndex);
        float w = 1.0f;
        if (auto* rawVal = apvts.getRawParameterValue (id))
            w = rawVal->load();
        const int pct = juce::roundToInt (w * 100.0f);
        pr.valueLabel->setText (pct <= 0 ? "OFF"
                                         : juce::String (pct) + "%",
                                juce::dontSendNotification);
    }

    void layoutContent()
    {
        const int rowHeight       = 30;
        const int headerHeight    = 28;
        const int padding         = 10;
        const int labelWidth      = 176;
        const int valueLabelWidth = 48;

        const int contentWidth = viewport.getMaximumVisibleWidth();
        int y = padding;

        for (auto& row : rows)
        {
            if (row.showCategoryHeader)
            {
                row.categoryLabel->setBounds (padding, y,
                                              contentWidth - padding * 2, headerHeight);
                row.categoryLabel->setVisible (true);
                y += headerHeight + 4;
            }
            else
            {
                row.categoryLabel->setVisible (false);
            }

            int x = padding + 10;
            row.nameLabel->setBounds (x, y, labelWidth, rowHeight);
            x += labelWidth + 4;

            const int ccLabelWidth = 40;
            const int sliderWidth = contentWidth - x - valueLabelWidth - ccLabelWidth - padding - 12;
            row.slider->setBounds (x, y, juce::jmax (20, sliderWidth), rowHeight);
            x += juce::jmax (20, sliderWidth) + 4;

            row.valueLabel->setBounds (x, y, valueLabelWidth, rowHeight);
            x += valueLabelWidth + 4;

            row.ccLabel->setBounds (x, y, ccLabelWidth, rowHeight);
            y += rowHeight + 1;
        }

        // Pad Target Probability section
        if (!padRows.empty())
        {
            y += 12;
            padSectionLabel.setBounds (padding, y, contentWidth - padding * 2, headerHeight);
            y += headerHeight + 2;

            for (auto& pr : padRows)
            {
                int x = padding + 10;
                pr.nameLabel->setBounds (x, y, labelWidth, rowHeight);
                x += labelWidth + 4;

                const int ccLabelWidth = 40;
                const int sliderWidth = contentWidth - x - valueLabelWidth - ccLabelWidth - padding - 12;
                pr.slider->setBounds (x, y, juce::jmax (20, sliderWidth), rowHeight);
                x += juce::jmax (20, sliderWidth) + 4;

                pr.valueLabel->setBounds (x, y, valueLabelWidth, rowHeight);
                x += valueLabelWidth + 4;

                pr.ccLabel->setBounds (x, y, ccLabelWidth, rowHeight);
                y += rowHeight + 1;
            }
        }

        content.setSize (contentWidth, y + padding);
    }

    juce::Random probabilityRandom;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModifierProbabilityPanel)
};
