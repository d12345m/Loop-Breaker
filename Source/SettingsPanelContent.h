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
        themeLabel.setText ("Theme", juce::dontSendNotification);
        themeLabel.setJustificationType (juce::Justification::centredRight);
        themeLabel.setFont (ThemeFonts::getInstance().controlLabelFont (14.0f));

        addAndMakeVisible (themeCombo);
        auto themes = ThemeEngine::getInstance().getAvailableThemeNames();
        for (int i = 0; i < themes.size(); ++i)
            themeCombo.addItem (themes[i], i + 1);
        themeCombo.setSelectedItemIndex (themes.indexOf (settings.themeName), juce::dontSendNotification);
        themeCombo.onChange = [this]
        {
            auto name = themeCombo.getText();
            settings.themeName = name;
            ThemeEngine::getInstance().setTheme (name);
        };

        // ── Animation controls (hidden for now — kept for future use) ──
        // All animation widgets are created but not made visible.
        animToggle.setButtonText ("Enable Animations");
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

        speedLabel.setText ("Animation Speed", juce::dontSendNotification);
        speedLabel.setJustificationType (juce::Justification::centredRight);
        speedLabel.setFont (ThemeFonts::getInstance().controlLabelFont (14.0f));

        speedSlider.setRange (0.25, 2.0, 0.05);
        speedSlider.setValue (settings.animationSpeed, juce::dontSendNotification);
        speedSlider.setTextValueSuffix ("x");
        speedSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        speedSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
        speedSlider.onValueChange = [this] { syncAnimConfigFromUI(); };

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
    }

    ~SettingsPanelContent() override
    {
        ThemeEngine::getInstance().removeListener (this);
    }

    void themeChanged() override
    {
        // Refresh label colours / fonts for the new theme
        themeLabel.setColour (juce::Label::textColourId, Theme::text());
        themeLabel.setFont (ThemeFonts::getInstance().controlLabelFont (14.0f));
        speedLabel.setColour (juce::Label::textColourId, Theme::text());
        speedLabel.setFont (ThemeFonts::getInstance().controlLabelFont (14.0f));
        bgModeLabel.setColour (juce::Label::textColourId, Theme::text());
        bgModeLabel.setFont (ThemeFonts::getInstance().controlLabelFont (14.0f));

        // Refresh combo box colours
        themeCombo.setColour (juce::ComboBox::backgroundColourId, Theme::panel());
        themeCombo.setColour (juce::ComboBox::outlineColourId,    Theme::border());
        themeCombo.setColour (juce::ComboBox::textColourId,       Theme::text());
        themeCombo.setColour (juce::ComboBox::arrowColourId,      Theme::textSubtle());

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();
        // No opaque fill — BackgroundAnimator paints the background

        auto bounds = getLocalBounds().reduced (20, 16);

        // Section header
        g.setColour (palette.accent1);
        g.setFont (ThemeFonts::getInstance().headingFont (17.0f));
        g.drawText ("Appearance", bounds.removeFromTop (28), juce::Justification::centredLeft);

        // Divider
        g.setColour (palette.border);
        g.fillRect (bounds.removeFromTop (1));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (20, 16);
        area.removeFromTop (30);  // header + divider

        const int rowH = 34;
        const int labelW = 140;
        const int toggleIndent = labelW + 20;
        const int toggleWidth = 250;

        // Theme row
        {
            auto row = area.removeFromTop (rowH);
            themeLabel.setBounds (row.removeFromLeft (labelW));
            row.removeFromLeft (10);
            themeCombo.setBounds (row.removeFromLeft (200));
        }

        // Animation controls are hidden — layout only contains the theme row above.
        // When re-enabling, restore the resized() layout for animation toggles,
        // speed slider, and background mode radio buttons here.
    }

private:
    void syncAnimConfigFromUI()
    {
        // Write to SessionSettings (persisted)
        settings.animationsEnabled     = animToggle.getToggleState();
        settings.bgCycleEnabled        = bgCycleToggle.getToggleState();
        settings.padPulseEnabled       = padPulseToggle.getToggleState();
        settings.progressShimmerEnabled = shimmerToggle.getToggleState();
        settings.knobGlowEnabled       = knobGlowToggle.getToggleState();
        settings.animationSpeed        = (float) speedSlider.getValue();

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
        speedSlider.setEnabled (master);
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

    SessionSettings& settings;

    // Theme
    juce::Label    themeLabel;
    juce::ComboBox themeCombo;

    // Animation toggles
    juce::ToggleButton animToggle;
    juce::ToggleButton bgCycleToggle;
    juce::ToggleButton padPulseToggle;
    juce::ToggleButton shimmerToggle;
    juce::ToggleButton knobGlowToggle;

    // Speed
    juce::Label  speedLabel;
    juce::Slider speedSlider;

    // Background mode
    juce::Label bgModeLabel;
    std::unique_ptr<juce::ToggleButton> bgModeButtons[3];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPanelContent)
};
