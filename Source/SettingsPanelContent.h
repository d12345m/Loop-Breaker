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

        // ── Enable Animations toggle ────────────────────────────────────
        addAndMakeVisible (animToggle);
        animToggle.setButtonText ("Enable Animations");
        animToggle.setToggleState (settings.animationsEnabled, juce::dontSendNotification);
        animToggle.onClick = [this] { syncAnimConfigFromUI(); };

        // ── Background Color Cycling ────────────────────────────────────
        addAndMakeVisible (bgCycleToggle);
        bgCycleToggle.setButtonText ("Background color cycling");
        bgCycleToggle.setToggleState (settings.bgCycleEnabled, juce::dontSendNotification);
        bgCycleToggle.onClick = [this] { syncAnimConfigFromUI(); };

        // ── Pad Glow Effects ────────────────────────────────────────────
        addAndMakeVisible (padPulseToggle);
        padPulseToggle.setButtonText ("Pad glow effects");
        padPulseToggle.setToggleState (settings.padPulseEnabled, juce::dontSendNotification);
        padPulseToggle.onClick = [this] { syncAnimConfigFromUI(); };

        // ── Progress Bar Shimmer ────────────────────────────────────────
        addAndMakeVisible (shimmerToggle);
        shimmerToggle.setButtonText ("Progress bar shimmer");
        shimmerToggle.setToggleState (settings.progressShimmerEnabled, juce::dontSendNotification);
        shimmerToggle.onClick = [this] { syncAnimConfigFromUI(); };

        // ── Knob Glow on Change ─────────────────────────────────────────
        addAndMakeVisible (knobGlowToggle);
        knobGlowToggle.setButtonText ("Knob glow on change");
        knobGlowToggle.setToggleState (settings.knobGlowEnabled, juce::dontSendNotification);
        knobGlowToggle.onClick = [this] { syncAnimConfigFromUI(); };

        // ── Animation Speed slider ──────────────────────────────────────
        addAndMakeVisible (speedLabel);
        speedLabel.setText ("Animation Speed", juce::dontSendNotification);
        speedLabel.setJustificationType (juce::Justification::centredRight);

        addAndMakeVisible (speedSlider);
        speedSlider.setRange (0.25, 2.0, 0.05);
        speedSlider.setValue (settings.animationSpeed, juce::dontSendNotification);
        speedSlider.setTextValueSuffix ("x");
        speedSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        speedSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
        speedSlider.onValueChange = [this] { syncAnimConfigFromUI(); };

        // ── Background Mode radio buttons ───────────────────────────────
        addAndMakeVisible (bgModeLabel);
        bgModeLabel.setText ("Background Mode", juce::dontSendNotification);
        bgModeLabel.setJustificationType (juce::Justification::centredRight);

        for (int i = 0; i < 3; ++i)
        {
            bgModeButtons[i] = std::make_unique<juce::ToggleButton>();
            addAndMakeVisible (bgModeButtons[i].get());
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
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();
        g.fillAll (palette.bg);

        auto bounds = getLocalBounds().reduced (20, 16);

        // Section header
        g.setColour (palette.accent1);
        g.setFont (juce::Font (juce::FontOptions().withHeight (17.0f)).boldened());
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

        area.removeFromTop (8);

        // Enable animations
        {
            auto row = area.removeFromTop (rowH);
            row.removeFromLeft (toggleIndent);
            animToggle.setBounds (row.removeFromLeft (toggleWidth));
        }

        // Sub-toggles (indented further)
        const int subIndent = toggleIndent + 24;
        auto placeSubToggle = [&] (juce::ToggleButton& btn)
        {
            auto row = area.removeFromTop (rowH - 4);
            row.removeFromLeft (subIndent);
            btn.setBounds (row.removeFromLeft (toggleWidth));
        };

        placeSubToggle (bgCycleToggle);
        placeSubToggle (padPulseToggle);
        placeSubToggle (shimmerToggle);
        placeSubToggle (knobGlowToggle);

        area.removeFromTop (8);

        // Animation speed
        {
            auto row = area.removeFromTop (rowH);
            speedLabel.setBounds (row.removeFromLeft (labelW));
            row.removeFromLeft (10);
            speedSlider.setBounds (row.removeFromLeft (260));
        }

        area.removeFromTop (8);

        // Background mode
        {
            auto row = area.removeFromTop (rowH);
            bgModeLabel.setBounds (row.removeFromLeft (labelW));
            row.removeFromLeft (10);
            for (int i = 0; i < 3; ++i)
            {
                bgModeButtons[i]->setBounds (row.removeFromLeft (120));
                row.removeFromLeft (4);
            }
        }
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
