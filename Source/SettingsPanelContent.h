/*
 ==============================================================================
   SettingsPanelContent.h
   --------------------------------------------------------------------------
   Settings tab — Appearance section with theme dropdown, animation toggles,
   background mode radio buttons, and animation speed slider.

   Reads from and writes to both SessionSettings (for persistence) and
   ThemeEngine (for immediate visual effect).
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ThemeEngine.h"
#include "ThemeFonts.h"
#include "SessionSettings.h"

class SettingsPanelContent : public juce::Component,
                             public ThemeListener
{
public:
    explicit SettingsPanelContent (SessionSettings& s)
        : settings (s)
    {
        ThemeEngine::getInstance().addListener (this);

        // ── Theme dropdown ──────────────────────────────────────────────
        addAndMakeVisible (themeLabel);
        themeLabel.setText ("THEME", juce::dontSendNotification);
        themeLabel.setJustificationType (juce::Justification::centredRight);
        themeLabel.setFont (ThemeFonts::getInstance().controlLabelFont (14.0f));

        addAndMakeVisible (themeCombo);
        auto themes = ThemeEngine::getInstance().getAvailableThemeNames();
        for (int i = 0; i < themes.size(); ++i)
            themeCombo.addItem (themes[i], i + 1);
        int selectedThemeIndex = themes.indexOf (settings.themeName);
        if (selectedThemeIndex < 0)
        {
            settings.themeName = "Control Surface (Light)";
            selectedThemeIndex = themes.indexOf (settings.themeName);
        }
        themeCombo.setSelectedItemIndex (selectedThemeIndex, juce::dontSendNotification);
        themeCombo.onChange = [this]
        {
            auto name = themeCombo.getText();
            settings.themeName = name;
            ThemeEngine::getInstance().setTheme (name);
        };

        // ── Parts dropdown ──────────────────────────────────────────────
        addAndMakeVisible (partsLabel);
        partsLabel.setText ("PARTS", juce::dontSendNotification);
        partsLabel.setJustificationType (juce::Justification::centredRight);
        partsLabel.setFont (ThemeFonts::getInstance().controlLabelFont (14.0f));

        addAndMakeVisible (partsCombo);
        partsCombo.addItem ("1 part",  1);
        partsCombo.addItem ("2 parts", 2);
        partsCombo.addItem ("3 parts", 3);
        partsCombo.addItem ("4 parts", 4);
        {
            int p = juce::jlimit (1, 4, settings.parts.getNumParts());
            partsCombo.setSelectedId (p, juce::dontSendNotification);
        }
        partsCombo.onChange = [this]
        {
            int newParts = juce::jlimit (1, 4, partsCombo.getSelectedId());
            if (onPartsChanged)
                onPartsChanged (newParts);
            else
                settings.parts.numParts = newParts;  // fallback if no callback wired
        };

        // ── Bars per modifier slider ────────────────────────────────────
        addAndMakeVisible (barsLabel);
        barsLabel.setText ("BARS / MODIFIER", juce::dontSendNotification);
        barsLabel.setJustificationType (juce::Justification::centredRight);
        barsLabel.setFont (ThemeFonts::getInstance().controlLabelFont (14.0f));

        addAndMakeVisible (barsSlider);
        barsSlider.setRange (1.0, 32.0, 1.0);
        barsSlider.setValue (settings.barsBetweenModifiers, juce::dontSendNotification);
        barsSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        barsSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 22);
        barsSlider.onValueChange = [this]
        {
            settings.barsBetweenModifiers = juce::jlimit (1, 32, (int) barsSlider.getValue());
            if (onBarsChanged) onBarsChanged (settings.barsBetweenModifiers);
        };

        // ── Cadence mode dropdown ───────────────────────────────────────
        addAndMakeVisible (cadenceModeLabel);
        cadenceModeLabel.setText ("CADENCE MODE", juce::dontSendNotification);
        cadenceModeLabel.setJustificationType (juce::Justification::centredRight);
        cadenceModeLabel.setFont (ThemeFonts::getInstance().controlLabelFont (14.0f));

        addAndMakeVisible (cadenceModeCombo);
        cadenceModeCombo.addItem ("Fixed",    1);
        cadenceModeCombo.addItem ("Variable", 2);
        cadenceModeCombo.addItem ("Timed",    3);
        cadenceModeCombo.setSelectedId ((int) settings.cadenceMode + 1, juce::dontSendNotification);
        cadenceModeCombo.onChange = [this]
        {
            settings.cadenceMode = static_cast<CadenceMode> (cadenceModeCombo.getSelectedId() - 1);
            updateCadenceVisibility();
        };

        // ── Variable cadence range (min/max bars) ──────────────────────
        addAndMakeVisible (barsRangeMinLabel);
        barsRangeMinLabel.setText ("MIN BARS", juce::dontSendNotification);
        barsRangeMinLabel.setJustificationType (juce::Justification::centredRight);
        barsRangeMinLabel.setFont (ThemeFonts::getInstance().controlLabelFont (14.0f));

        addAndMakeVisible (barsRangeMinSlider);
        barsRangeMinSlider.setRange (1.0, 32.0, 1.0);
        barsRangeMinSlider.setValue (settings.barsRangeMin, juce::dontSendNotification);
        barsRangeMinSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        barsRangeMinSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 22);
        barsRangeMinSlider.onValueChange = [this]
        {
            settings.barsRangeMin = juce::jlimit (1, 32, (int) barsRangeMinSlider.getValue());
            if (settings.barsRangeMax < settings.barsRangeMin)
            {
                settings.barsRangeMax = settings.barsRangeMin;
                barsRangeMaxSlider.setValue (settings.barsRangeMax, juce::dontSendNotification);
            }
        };

        addAndMakeVisible (barsRangeMaxLabel);
        barsRangeMaxLabel.setText ("MAX BARS", juce::dontSendNotification);
        barsRangeMaxLabel.setJustificationType (juce::Justification::centredRight);
        barsRangeMaxLabel.setFont (ThemeFonts::getInstance().controlLabelFont (14.0f));

        addAndMakeVisible (barsRangeMaxSlider);
        barsRangeMaxSlider.setRange (1.0, 32.0, 1.0);
        barsRangeMaxSlider.setValue (settings.barsRangeMax, juce::dontSendNotification);
        barsRangeMaxSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        barsRangeMaxSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 22);
        barsRangeMaxSlider.onValueChange = [this]
        {
            settings.barsRangeMax = juce::jlimit (1, 32, (int) barsRangeMaxSlider.getValue());
            if (settings.barsRangeMin > settings.barsRangeMax)
            {
                settings.barsRangeMin = settings.barsRangeMax;
                barsRangeMinSlider.setValue (settings.barsRangeMin, juce::dontSendNotification);
            }
        };

        // ── Timed cadence range (min/max seconds) ──────────────────────
        addAndMakeVisible (timedMinLabel);
        timedMinLabel.setText ("MIN TIME", juce::dontSendNotification);
        timedMinLabel.setJustificationType (juce::Justification::centredRight);
        timedMinLabel.setFont (ThemeFonts::getInstance().controlLabelFont (14.0f));

        addAndMakeVisible (timedMinSlider);
        timedMinSlider.setRange (1.0, 300.0, 1.0);
        timedMinSlider.setValue (settings.timedIntervalMinSec, juce::dontSendNotification);
        timedMinSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        timedMinSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 22);
        timedMinSlider.setTextValueSuffix ("s");
        timedMinSlider.onValueChange = [this]
        {
            settings.timedIntervalMinSec = timedMinSlider.getValue();
            if (settings.timedIntervalMaxSec < settings.timedIntervalMinSec)
            {
                settings.timedIntervalMaxSec = settings.timedIntervalMinSec;
                timedMaxSlider.setValue (settings.timedIntervalMaxSec, juce::dontSendNotification);
            }
        };

        addAndMakeVisible (timedMaxLabel);
        timedMaxLabel.setText ("MAX TIME", juce::dontSendNotification);
        timedMaxLabel.setJustificationType (juce::Justification::centredRight);
        timedMaxLabel.setFont (ThemeFonts::getInstance().controlLabelFont (14.0f));

        addAndMakeVisible (timedMaxSlider);
        timedMaxSlider.setRange (1.0, 300.0, 1.0);
        timedMaxSlider.setValue (settings.timedIntervalMaxSec, juce::dontSendNotification);
        timedMaxSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        timedMaxSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 22);
        timedMaxSlider.setTextValueSuffix ("s");
        timedMaxSlider.onValueChange = [this]
        {
            settings.timedIntervalMaxSec = timedMaxSlider.getValue();
            if (settings.timedIntervalMinSec > settings.timedIntervalMaxSec)
            {
                settings.timedIntervalMinSec = settings.timedIntervalMaxSec;
                timedMinSlider.setValue (settings.timedIntervalMinSec, juce::dontSendNotification);
            }
        };

        // Set initial visibility based on current cadence mode
        updateCadenceVisibility();

        // ── Core modifier motion controls ──────────────────────────────
        // Glyph motion is semantic, so its master toggle remains visible.
        // Its rate follows session/host BPM; decorative controls remain hidden.
        addAndMakeVisible (animToggle);
        animToggle.setButtonText ("ANIMATE MODIFIER GLYPHS");
        animToggle.setToggleState (settings.animationsEnabled, juce::dontSendNotification);
        animToggle.onClick = [this] { syncAnimConfigFromUI(); };

        bgCycleToggle.setButtonText ("Background color cycling");
        bgCycleToggle.setToggleState (settings.bgCycleEnabled, juce::dontSendNotification);
        bgCycleToggle.onClick = [this] { syncAnimConfigFromUI(); };

        padPulseToggle.setButtonText ("Pad glow effects");
        padPulseToggle.setToggleState (settings.padPulseEnabled, juce::dontSendNotification);
        padPulseToggle.onClick = [this] { syncAnimConfigFromUI(); };

        shimmerToggle.setButtonText ("Progress bar shimmer");
        shimmerToggle.setToggleState (settings.progressShimmerEnabled, juce::dontSendNotification);
        shimmerToggle.onClick = [this] { syncAnimConfigFromUI(); };

        knobGlowToggle.setButtonText ("Knob glow on change");
        knobGlowToggle.setToggleState (settings.knobGlowEnabled, juce::dontSendNotification);
        knobGlowToggle.onClick = [this] { syncAnimConfigFromUI(); };

        bgModeLabel.setText ("Background Mode", juce::dontSendNotification);
        bgModeLabel.setJustificationType (juce::Justification::centredRight);
        bgModeLabel.setFont (ThemeFonts::getInstance().controlLabelFont (14.0f));

        for (int i = 0; i < 3; ++i)
        {
            bgModeButtons[i] = std::make_unique<juce::ToggleButton>();
            bgModeButtons[i]->setRadioGroupId (42);
            bgModeButtons[i]->onClick = [this, i] { selectBgMode (i); };
        }
        bgModeButtons[0]->setButtonText ("Static");
        bgModeButtons[1]->setButtonText ("Slow Cycle");
        bgModeButtons[2]->setButtonText ("Reactive");

        int initialMode = juce::jlimit (0, 2, settings.backgroundMode);
        bgModeButtons[initialMode]->setToggleState (true, juce::dontSendNotification);

        // Sync to ThemeEngine on construction
        syncAnimConfigFromUI();
        applyTheme();
    }

    ~SettingsPanelContent() override
    {
        ThemeEngine::getInstance().removeListener (this);
    }

    void themeChanged() override
    {
        applyTheme();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();
        g.setColour (palette.textPrimary);
        g.setFont (ThemeFonts::getInstance().headingFont (18.0f));
        g.drawText ("SYSTEM CONFIGURATION", pageHeaderBounds, juce::Justification::centredLeft);
        g.setColour (palette.textSecondary);
        g.setFont (ThemeFonts::getInstance().monoFont (11.0f));
        g.drawText ("DISPLAY  /  TIMING  /  MOTION", pageHeaderBounds,
                    juce::Justification::centredRight);

        auto drawCard = [&] (juce::Rectangle<int> bounds, const juce::String& index,
                             const juce::String& title, const juce::String& subtitle)
        {
            g.setColour (palette.panel);
            g.fillRect (bounds);
            g.setColour (palette.border.withAlpha (0.8f));
            g.drawRect (bounds, 1);

            auto header = bounds.reduced (14, 0).removeFromTop (54);
            g.setColour (palette.accent1);
            g.fillRect (header.getX(), header.getY() + 14, 4, 24);
            header.removeFromLeft (12);
            g.setFont (ThemeFonts::getInstance().headingFont (15.0f));
            g.drawText (title, header.removeFromTop (31), juce::Justification::centredLeft);
            g.setColour (palette.textSecondary);
            g.setFont (ThemeFonts::getInstance().monoFont (10.0f));
            g.drawText (subtitle, header, juce::Justification::centredLeft);

            g.setColour (palette.textSecondary.withAlpha (0.7f));
            g.setFont (ThemeFonts::getInstance().monoFont (11.0f));
            g.drawText (index, bounds.reduced (14, 0).removeFromTop (54),
                        juce::Justification::centredRight);
            g.setColour (palette.border.withAlpha (0.45f));
            g.fillRect (bounds.getX(), bounds.getY() + 54, bounds.getWidth(), 1);
        };

        drawCard (appearanceCardBounds, "01", "APPEARANCE", "CONTROL-SURFACE PALETTE");
        drawCard (sessionCardBounds, "02", "SESSION", "MODIFIER CADENCE ENGINE");
        drawCard (motionCardBounds, "03", "MOTION", "SEMANTIC GLYPH DISPLAY");
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (18, 14);
        pageHeaderBounds = area.removeFromTop (38);
        area.removeFromTop (10);

        constexpr int gap = 12;
        if (area.getWidth() >= 840)
        {
            const int appearanceW = juce::jmax (240, juce::roundToInt (area.getWidth() * 0.23f));
            const int motionW = juce::jmax (290, juce::roundToInt (area.getWidth() * 0.28f));
            appearanceCardBounds = area.removeFromLeft (appearanceW);
            area.removeFromLeft (gap);
            motionCardBounds = area.removeFromRight (motionW);
            area.removeFromRight (gap);
            sessionCardBounds = area;
        }
        else
        {
            appearanceCardBounds = area.removeFromTop (132);
            area.removeFromTop (gap);
            sessionCardBounds = area.removeFromTop (232);
            area.removeFromTop (gap);
            motionCardBounds = area;
        }

        const int rowH = 38;
        auto controlArea = [] (juce::Rectangle<int> card)
        {
            card.removeFromTop (55);
            return card.reduced (14, 12);
        };
        auto placePair = [] (juce::Rectangle<int> row, juce::Label& label,
                             juce::Component& control, int labelW)
        {
            label.setBounds (row.removeFromLeft (juce::jmin (labelW, row.getWidth() / 2)));
            row.removeFromLeft (8);
            control.setBounds (row.removeFromLeft (juce::jmin (220, row.getWidth())).reduced (0, 4));
        };

        {
            auto card = controlArea (appearanceCardBounds);
            placePair (card.removeFromTop (rowH), themeLabel, themeCombo, 58);
        }

        {
            auto card = controlArea (sessionCardBounds);
            const int labelW = sessionCardBounds.getWidth() < 360 ? 106 : 126;
            auto row0 = card.removeFromTop (rowH);
            auto row1 = card.removeFromTop (rowH);
            auto row2 = card.removeFromTop (rowH);
            auto row3 = card.removeFromTop (rowH);
            placePair (row0, partsLabel, partsCombo, labelW);
            placePair (row1, cadenceModeLabel, cadenceModeCombo, labelW);
            placePair (row2, barsLabel, barsSlider, labelW);
            placePair (row2, barsRangeMinLabel, barsRangeMinSlider, labelW);
            placePair (row3, barsRangeMaxLabel, barsRangeMaxSlider, labelW);
            placePair (row2, timedMinLabel, timedMinSlider, labelW);
            placePair (row3, timedMaxLabel, timedMaxSlider, labelW);
        }

        {
            auto card = controlArea (motionCardBounds);
            animToggle.setBounds (card.removeFromTop (rowH).reduced (0, 4));
        }
    }

private:
    void applyTheme()
    {
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();

        for (auto* label : { &themeLabel, &partsLabel, &barsLabel, &cadenceModeLabel,
                             &barsRangeMinLabel, &barsRangeMaxLabel, &timedMinLabel,
                             &timedMaxLabel, &bgModeLabel })
        {
            label->setColour (juce::Label::textColourId, palette.textSecondary);
            label->setFont (ThemeFonts::getInstance().controlLabelFont (12.0f));
        }

        for (auto* combo : { &themeCombo, &partsCombo, &cadenceModeCombo })
        {
            combo->setColour (juce::ComboBox::backgroundColourId, palette.panel);
            combo->setColour (juce::ComboBox::outlineColourId, palette.border);
            combo->setColour (juce::ComboBox::textColourId, palette.textPrimary);
            combo->setColour (juce::ComboBox::arrowColourId, palette.textSecondary);
        }

        for (auto* slider : { &barsSlider, &barsRangeMinSlider, &barsRangeMaxSlider,
                              &timedMinSlider, &timedMaxSlider })
        {
            slider->setColour (juce::Slider::backgroundColourId, palette.panelAlt);
            slider->setColour (juce::Slider::trackColourId, palette.accent1);
            slider->setColour (juce::Slider::thumbColourId, palette.accent1);
            slider->setColour (juce::Slider::textBoxBackgroundColourId, palette.panelAlt);
            slider->setColour (juce::Slider::textBoxTextColourId, palette.textPrimary);
            slider->setColour (juce::Slider::textBoxOutlineColourId, palette.border);
        }

        animToggle.setColour (juce::ToggleButton::textColourId, palette.textPrimary);
    }

    void syncAnimConfigFromUI()
    {
        // Write to SessionSettings (persisted)
        settings.animationsEnabled     = animToggle.getToggleState();
        settings.bgCycleEnabled        = bgCycleToggle.getToggleState();
        settings.padPulseEnabled       = padPulseToggle.getToggleState();
        settings.progressShimmerEnabled = shimmerToggle.getToggleState();
        settings.knobGlowEnabled       = knobGlowToggle.getToggleState();

        // Write to ThemeEngine runtime config
        auto& cfg = ThemeEngine::getInstance().getAnimationConfigMutable();
        cfg.enabled              = settings.animationsEnabled;
        cfg.backgroundColorCycle = settings.bgCycleEnabled;
        cfg.padPulseOnTrigger    = settings.padPulseEnabled;
        cfg.progressBarShimmer   = settings.progressShimmerEnabled;
        cfg.knobGlowOnChange     = settings.knobGlowEnabled;
        cfg.animationSpeed       = settings.animationSpeed;

        // Enable/disable sub-toggles based on master
        const bool master = settings.animationsEnabled;
        bgCycleToggle.setEnabled (master);
        padPulseToggle.setEnabled (master);
        shimmerToggle.setEnabled (master);
        knobGlowToggle.setEnabled (master);
        for (auto& b : bgModeButtons) b->setEnabled (master);
    }

    void selectBgMode (int index)
    {
        settings.backgroundMode = index;

        auto& cfg = ThemeEngine::getInstance().getAnimationConfigMutable();
        switch (index)
        {
            case 0: cfg.backgroundMode = BackgroundMode::Static;    break;
            case 1: cfg.backgroundMode = BackgroundMode::SlowCycle; break;
            case 2: cfg.backgroundMode = BackgroundMode::Reactive;  break;
        }

        // Force full repaint so buttons and background update immediately
        if (auto* topLevel = getTopLevelComponent())
            topLevel->repaint();
    }

    void updateCadenceVisibility()
    {
        const bool isFixed    = (settings.cadenceMode == CadenceMode::Fixed);
        const bool isVariable = (settings.cadenceMode == CadenceMode::Variable);
        const bool isTimed    = (settings.cadenceMode == CadenceMode::Timed);

        barsLabel.setVisible (isFixed);
        barsSlider.setVisible (isFixed);

        barsRangeMinLabel.setVisible (isVariable);
        barsRangeMinSlider.setVisible (isVariable);
        barsRangeMaxLabel.setVisible (isVariable);
        barsRangeMaxSlider.setVisible (isVariable);

        timedMinLabel.setVisible (isTimed);
        timedMinSlider.setVisible (isTimed);
        timedMaxLabel.setVisible (isTimed);
        timedMaxSlider.setVisible (isTimed);

        resized();
        repaint();
    }

    SessionSettings& settings;

public:
    // Callbacks (set by parent to integrate with session logic)
    std::function<void(int)> onPartsChanged;
    std::function<void(int)> onBarsChanged;

private:

    // Theme
    juce::Label    themeLabel;
    juce::ComboBox themeCombo;

    // Parts
    juce::Label    partsLabel;
    juce::ComboBox partsCombo;

    // Cadence mode
    juce::Label    cadenceModeLabel;
    juce::ComboBox cadenceModeCombo;

    // Bars per modifier (Fixed mode)
    juce::Label  barsLabel;
    juce::Slider barsSlider;

    // Variable cadence range
    juce::Label  barsRangeMinLabel;
    juce::Slider barsRangeMinSlider;
    juce::Label  barsRangeMaxLabel;
    juce::Slider barsRangeMaxSlider;

    // Timed cadence range
    juce::Label  timedMinLabel;
    juce::Slider timedMinSlider;
    juce::Label  timedMaxLabel;
    juce::Slider timedMaxSlider;

    // Animation toggles
    juce::ToggleButton animToggle;
    juce::ToggleButton bgCycleToggle;
    juce::ToggleButton padPulseToggle;
    juce::ToggleButton shimmerToggle;
    juce::ToggleButton knobGlowToggle;

    // Speed

    // Background mode
    juce::Label bgModeLabel;
    std::unique_ptr<juce::ToggleButton> bgModeButtons[3];

    juce::Rectangle<int> pageHeaderBounds;
    juce::Rectangle<int> appearanceCardBounds;
    juce::Rectangle<int> sessionCardBounds;
    juce::Rectangle<int> motionCardBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPanelContent)
};
