/*
 ==============================================================================
   ThemeLookAndFeel.h
   --------------------------------------------------------------------------
   Custom LookAndFeel_V4 subclass that reads all colours from ThemeEngine
   at paint time — theme switches take effect immediately without recreating
   the LookAndFeel.

   Replaces the private HipLookAndFeel that was embedded in PluginEditor.cpp.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ThemeEngine.h"
#include "ThemeFonts.h"
#include <unordered_map>

class ThemeLookAndFeel final : public juce::LookAndFeel_V4,
                               public ThemeListener,
                               private juce::Timer
{
public:
    ThemeLookAndFeel()
    {
        applyCurrentTheme();
        ThemeEngine::getInstance().addListener (this);
    }

    ~ThemeLookAndFeel() override
    {
        ThemeEngine::getInstance().removeListener (this);
    }

    /** Called automatically when the theme changes. */
    void themeChanged() override
    {
        applyCurrentTheme();
    }

    /** Re-read all JUCE colour IDs from the current ThemeEngine palette.
        Call this after switching themes so that components using findColour()
        pick up the new values. Components that paint via Theme::xxx() or
        ThemeEngine::color() will already reflect changes at next repaint. */
    void applyCurrentTheme()
    {
        // Window
        setColour (juce::ResizableWindow::backgroundColourId, Theme::bg());

        // ComboBox
        setColour (juce::ComboBox::backgroundColourId,  Theme::panel());
        setColour (juce::ComboBox::outlineColourId,      Theme::border());
        setColour (juce::ComboBox::textColourId,         Theme::text());
        setColour (juce::ComboBox::arrowColourId,        Theme::textSubtle());

        // TextButton
        setColour (juce::TextButton::buttonColourId,     Theme::panelAlt());
        setColour (juce::TextButton::buttonOnColourId,   Theme::panelAlt());
        setColour (juce::TextButton::textColourOffId,    Theme::text());
        setColour (juce::TextButton::textColourOnId,     Theme::text());

        // ToggleButton
        setColour (juce::ToggleButton::textColourId,         Theme::text());
        setColour (juce::ToggleButton::tickColourId,         Theme::accent());
        setColour (juce::ToggleButton::tickDisabledColourId, Theme::borderStrong());

        // ScrollBar
        setColour (juce::ScrollBar::thumbColourId, Theme::borderStrong());

        // PopupMenu
        setColour (juce::PopupMenu::backgroundColourId,            Theme::panel());
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::accent().withAlpha (0.12f));
        setColour (juce::PopupMenu::textColourId,                  Theme::text());
        setColour (juce::PopupMenu::highlightedTextColourId,       Theme::text());

        // Label
        setColour (juce::Label::textColourId,       Theme::text());
        setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);

        // Slider
        setColour (juce::Slider::rotarySliderFillColourId,    Theme::accent());
        setColour (juce::Slider::rotarySliderOutlineColourId, Theme::panelAlt());
        setColour (juce::Slider::thumbColourId,               Theme::accent());
        setColour (juce::Slider::trackColourId,               Theme::accent());
        setColour (juce::Slider::backgroundColourId,          Theme::panelAlt());

        // TabbedButtonBar
        setColour (juce::TabbedButtonBar::tabOutlineColourId,   Theme::border());
        setColour (juce::TabbedButtonBar::frontOutlineColourId, Theme::accent());
        setColour (juce::TabbedButtonBar::tabTextColourId,      Theme::textSubtle());
        setColour (juce::TabbedButtonBar::frontTextColourId,    Theme::text());
    }

    // ──────────────────────────────────────────────────────────────────────
    //  drawComboBox — rounded rect with chevron (from old HipLookAndFeel)
    // ──────────────────────────────────────────────────────────────────────

    void drawComboBox (juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                       int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                       juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height);
        const float corner = ThemeEngine::getInstance().getCurrentPalette().borderRadius;

        // Background
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, corner);

        // Border
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);

        // Small chevron (▾) on the right
        const float arrowSize = 6.0f;
        const float arrowX = (float) width - 14.0f;
        const float arrowY = (float) height * 0.5f;

        juce::Path chevron;
        chevron.addTriangle (arrowX - arrowSize * 0.5f, arrowY - arrowSize * 0.3f,
                             arrowX + arrowSize * 0.5f, arrowY - arrowSize * 0.3f,
                             arrowX,                    arrowY + arrowSize * 0.4f);

        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.fillPath (chevron);
    }

    // ──────────────────────────────────────────────────────────────────────
    //  drawRotarySlider — arc + dot indicator + center gradient
    // ──────────────────────────────────────────────────────────────────────

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override
    {
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();
        const float radius  = (float) juce::jmin (width, height) * 0.5f - 4.0f;
        const float centreX = (float) x + (float) width  * 0.5f;
        const float centreY = (float) y + (float) height * 0.5f;
        const float angle   = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        // Track arc (background)
        const float arcWidth = 3.0f;
        juce::Path trackArc;
        trackArc.addCentredArc (centreX, centreY, radius, radius, 0.0f,
                                rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (palette.knobTrack);
        g.strokePath (trackArc, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

        // Fill arc (value)
        if (sliderPosProportional > 0.0f)
        {
            juce::Path fillArc;
            fillArc.addCentredArc (centreX, centreY, radius, radius, 0.0f,
                                   rotaryStartAngle, angle, true);
            g.setColour (palette.knobFill);
            g.strokePath (fillArc, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
        }

        // Center circle fill — radial gradient for depth
        const float innerRadius = radius * 0.62f;
        juce::ColourGradient centerGrad (palette.panel, centreX, centreY,
                                         palette.panelAlt, centreX + innerRadius, centreY + innerRadius,
                                         true);
        g.setGradientFill (centerGrad);
        g.fillEllipse (centreX - innerRadius, centreY - innerRadius,
                       innerRadius * 2.0f, innerRadius * 2.0f);

        // Subtle specular highlight (small white arc at 10 o'clock)
        {
            const float highlightAngle = juce::MathConstants<float>::pi * 1.25f; // ~225°
            const float hx = centreX + (innerRadius * 0.55f) * std::cos (highlightAngle);
            const float hy = centreY + (innerRadius * 0.55f) * std::sin (highlightAngle);
            g.setColour (palette.textPrimary.withAlpha (0.08f));
            g.fillEllipse (hx - 2.0f, hy - 2.0f, 4.0f, 4.0f);
        }

        // ── Knob glow on value change ────────────────────────────────────
        const auto& animCfg = ThemeEngine::getInstance().getAnimationConfig();
        if (animCfg.enabled && animCfg.knobGlowOnChange)
        {
            auto* sliderPtr = &slider;
            auto& state = knobGlowStates[sliderPtr];

            if (state.lastValue != sliderPosProportional)
            {
                state.lastValue = sliderPosProportional;
                state.glowAlpha = 1.0f;

                if (! isTimerRunning())
                    startTimerHz (30);
            }

            if (state.glowAlpha > 0.0f)
            {
                const float glowRadius = radius + 4.0f;
                g.setColour (palette.accent1.withAlpha (state.glowAlpha * 0.5f));
                g.drawEllipse (centreX - glowRadius, centreY - glowRadius,
                               glowRadius * 2.0f, glowRadius * 2.0f, 2.5f);

                // Softer outer ring
                const float outerGlow = radius + 7.0f;
                g.setColour (palette.accent1.withAlpha (state.glowAlpha * 0.2f));
                g.drawEllipse (centreX - outerGlow, centreY - outerGlow,
                               outerGlow * 2.0f, outerGlow * 2.0f, 2.0f);
            }
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    //  drawLinearSlider — rounded track + gradient fill + circle thumb
    // ──────────────────────────────────────────────────────────────────────

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style == juce::Slider::LinearBar || style == juce::Slider::LinearBarVertical)
        {
            // For LinearBar style, keep the default rendering but with themed colours
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                              minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();
        const bool isHorizontal = (style == juce::Slider::LinearHorizontal);

        if (isHorizontal)
        {
            const float trackY = (float) y + (float) height * 0.5f;
            const float trackH = 6.0f;
            const float trackTop = trackY - trackH * 0.5f;
            const float corner = trackH * 0.5f;

            // Track background
            g.setColour (palette.knobTrack);
            g.fillRoundedRectangle ((float) x, trackTop, (float) width, trackH, corner);

            // Fill
            const float fillWidth = sliderPos - (float) x;
            if (fillWidth > 0)
            {
                g.setColour (palette.accent1);
                g.fillRoundedRectangle ((float) x, trackTop, fillWidth, trackH, corner);
            }

            // Thumb circle
            const float thumbRadius = 5.0f;
            g.setColour (palette.accent1);
            g.fillEllipse (sliderPos - thumbRadius, trackY - thumbRadius,
                           thumbRadius * 2.0f, thumbRadius * 2.0f);

            // Thumb glow ring
            g.setColour (palette.borderGlow.withAlpha (0.3f));
            g.drawEllipse (sliderPos - thumbRadius - 1.0f, trackY - thumbRadius - 1.0f,
                           (thumbRadius + 1.0f) * 2.0f, (thumbRadius + 1.0f) * 2.0f, 1.0f);
        }
        else
        {
            // Vertical — defer to default for now
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                              minSliderPos, maxSliderPos, style, slider);
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    //  drawToggleButton — rounded pill toggle with accent colour fill
    // ──────────────────────────────────────────────────────────────────────

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override
    {
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();
        auto bounds = button.getLocalBounds().toFloat();

        const float toggleW = 36.0f;
        const float toggleH = 18.0f;
        const float toggleX = bounds.getX() + 2.0f;
        const float toggleY = bounds.getCentreY() - toggleH * 0.5f;
        const float corner  = toggleH * 0.5f;

        const bool isOn = button.getToggleState();

        // Track
        g.setColour (isOn ? palette.accent1 : palette.panelAlt);
        g.fillRoundedRectangle (toggleX, toggleY, toggleW, toggleH, corner);

        // Track border
        g.setColour (isOn ? palette.accent1.brighter (0.2f) : palette.border);
        g.drawRoundedRectangle (toggleX, toggleY, toggleW, toggleH, corner, 1.0f);

        // Thumb circle
        const float thumbDiameter = toggleH - 4.0f;
        const float thumbX = isOn ? (toggleX + toggleW - thumbDiameter - 2.0f) : (toggleX + 2.0f);
        const float thumbY = toggleY + 2.0f;
        g.setColour (isOn ? palette.textOnAccent : palette.textSecondary);
        g.fillEllipse (thumbX, thumbY, thumbDiameter, thumbDiameter);

        // Label text
        const float textX = toggleX + toggleW + 6.0f;
        const float textW = bounds.getWidth() - textX;
        g.setColour (button.findColour (juce::ToggleButton::textColourId));
        g.setFont (ThemeFonts::getInstance().controlLabelFont (13.0f));
        g.drawText (button.getButtonText(),
                    juce::Rectangle<float> (textX, bounds.getY(), textW, bounds.getHeight()),
                    juce::Justification::centredLeft, true);

        (void) shouldDrawButtonAsHighlighted;
        (void) shouldDrawButtonAsDown;
    }

    // ──────────────────────────────────────────────────────────────────────
    //  drawButtonBackground — pill-shaped / rounded rect
    // ──────────────────────────────────────────────────────────────────────

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const float corner = palette.borderRadius;

        auto baseColour = backgroundColour;
        if (shouldDrawButtonAsDown)
            baseColour = baseColour.brighter (0.1f);
        else if (shouldDrawButtonAsHighlighted)
            baseColour = baseColour.brighter (0.05f);

        g.setColour (baseColour);
        g.fillRoundedRectangle (bounds, corner);

        g.setColour (palette.border);
        g.drawRoundedRectangle (bounds, corner, 1.0f);
    }

    // ──────────────────────────────────────────────────────────────────────
    //  drawScrollbar — thin rounded thumb, transparent track
    // ──────────────────────────────────────────────────────────────────────

    void drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar,
                        int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition,
                        int thumbSize, bool isMouseOver, bool isMouseDown) override
    {
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();

        // Track (transparent)
        g.setColour (palette.border.withAlpha (0.1f));
        g.fillRect (x, y, width, height);

        // Thumb
        auto alpha = 0.4f;
        if (isMouseDown)  alpha = 0.7f;
        else if (isMouseOver) alpha = 0.55f;

        g.setColour (palette.textSecondary.withAlpha (alpha));

        if (isScrollbarVertical)
        {
            const float thumbW = juce::jmin ((float) width - 2.0f, 6.0f);
            const float thumbX = (float) x + ((float) width - thumbW) * 0.5f;
            g.fillRoundedRectangle (thumbX, (float) thumbStartPosition,
                                   thumbW, (float) thumbSize, thumbW * 0.5f);
        }
        else
        {
            const float thumbH = juce::jmin ((float) height - 2.0f, 6.0f);
            const float thumbY = (float) y + ((float) height - thumbH) * 0.5f;
            g.fillRoundedRectangle ((float) thumbStartPosition, thumbY,
                                   (float) thumbSize, thumbH, thumbH * 0.5f);
        }

        (void) bar;
    }

    // ──────────────────────────────────────────────────────────────────────
    //  Tab bar — dark bg, underline active indicator, all-caps labels
    // ──────────────────────────────────────────────────────────────────────

    void drawTabbedButtonBarBackground (juce::TabbedButtonBar& bar, juce::Graphics& g) override
    {
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();
        g.setColour (palette.bgAlt);
        g.fillRect (bar.getLocalBounds());

        // Thin bottom divider
        g.setColour (palette.border.withAlpha (0.5f));
        g.fillRect (0, bar.getHeight() - 1, bar.getWidth(), 1);
    }

    void drawTabButton (juce::TabBarButton& button, juce::Graphics& g,
                        bool isMouseOver, bool isMouseDown) override
    {
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();
        auto bounds = button.getLocalBounds().toFloat();
        const bool isFront = button.isFrontTab();

        // Background — transparent for all tabs (the bar BG shows through)
        if (isMouseDown)
        {
            g.setColour (palette.panel.withAlpha (0.3f));
            g.fillRect (bounds);
        }
        else if (isMouseOver && !isFront)
        {
            g.setColour (palette.panel.withAlpha (0.15f));
            g.fillRect (bounds);
        }

        // Active underline (2px accent bar at bottom)
        if (isFront)
        {
            g.setColour (palette.accent1);
            g.fillRect (bounds.getX() + 4.0f, bounds.getBottom() - 2.5f,
                        bounds.getWidth() - 8.0f, 2.5f);
        }

        // Text — all-caps, 11px
        juce::Colour textCol;
        if (isFront)
            textCol = palette.accent1;
        else if (isMouseOver)
            textCol = palette.textPrimary;
        else
            textCol = palette.textSecondary;

        g.setColour (textCol);
        g.setFont (ThemeFonts::getInstance().tabFont (13.0f));

        auto textArea = bounds.reduced (4.0f, 0.0f).withTrimmedBottom (3.0f);
        g.drawText (button.getButtonText().toUpperCase(), textArea,
                    juce::Justification::centred, false);
    }

    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar& bar,
                                       juce::Graphics& g, int w, int h) override
    {
        // No extra painting needed — handled by drawTabbedButtonBarBackground
        juce::ignoreUnused (bar, g, w, h);
    }

    int getTabButtonBestWidth (juce::TabBarButton& button, int tabDepth) override
    {
        auto f = ThemeFonts::getInstance().tabFont (13.0f);
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (f, button.getButtonText().toUpperCase(), 0.0f, 0.0f);
        auto textWidth = (int) std::ceil (glyphs.getBoundingBox (0, -1, false).getWidth());
        return textWidth + 36; // padding
    }

    int getTabButtonOverlap (int tabDepth) override
    {
        return 0;
    }

    // ──────────────────────────────────────────────────────────────────────
    //  Font overrides — ensure all JUCE components use ThemeFonts
    // ──────────────────────────────────────────────────────────────────────

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return ThemeFonts::getInstance().controlLabelFont (juce::jmin (16.0f, (float) box.getHeight() * 0.75f));
    }

    juce::Font getPopupMenuFont() override
    {
        return ThemeFonts::getInstance().controlLabelFont (14.0f);
    }

    juce::Font getLabelFont (juce::Label& label) override
    {
        return ThemeFonts::getInstance().controlLabelFont (juce::jmin (16.0f, label.getFont().getHeight()));
    }

    juce::Font getSliderPopupFont (juce::Slider&) override
    {
        return ThemeFonts::getInstance().monoFont (13.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return ThemeFonts::getInstance().controlLabelFont (juce::jmin (16.0f, (float) buttonHeight * 0.65f));
    }

    juce::Slider::SliderLayout getSliderLayout (juce::Slider& slider) override
    {
        auto layout = juce::LookAndFeel_V4::getSliderLayout (slider);
        // Ensure the text box label uses our themed font
        return layout;
    }

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
        label->setFont (ThemeFonts::getInstance().monoFont (13.0f));
        return label;
    }

private:
    // ── Knob glow state tracking ─────────────────────────────────────
    struct KnobGlowState
    {
        float lastValue = -1.0f;
        float glowAlpha = 0.0f;
    };

    std::unordered_map<juce::Slider*, KnobGlowState> knobGlowStates;

    void timerCallback() override
    {
        bool anyActive = false;
        const float decayRate = 1.0f / 6.0f;  // fade over ~200ms at 30Hz

        for (auto it = knobGlowStates.begin(); it != knobGlowStates.end(); )
        {
            auto& state = it->second;
            if (state.glowAlpha > 0.0f)
            {
                state.glowAlpha -= decayRate;
                if (state.glowAlpha < 0.01f)
                    state.glowAlpha = 0.0f;

                // Repaint the slider to show the fading glow
                if (auto* comp = it->first)
                    comp->repaint();

                if (state.glowAlpha > 0.0f)
                    anyActive = true;
            }
            ++it;
        }

        if (! anyActive)
            stopTimer();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThemeLookAndFeel)
};
