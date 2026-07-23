#pragma once

#include <JuceHeader.h>
#include "GlyphContactSheet.h"
#include "ModifierGlyphRenderer.h"
#include "ModifierProbabilityManager.h"
#include "SessionSettings.h"
#include "ThemeFonts.h"

/** Debug-only review surface for the production modifier glyph renderer. */
class GlyphLabComponent final : public juce::Component,
                                private juce::ListBoxModel,
                                private juce::Timer
{
public:
    explicit GlyphLabComponent (const SessionSettings& settingsRef)
        : settings (settingsRef)
    {
        const auto& registry = ModifierProbabilityManager::allModifierTypes();
        allTypes.assign (registry.begin(), registry.end());
        filteredTypes = allTypes;

        searchEditor.setTextToShowWhenEmpty ("Filter modifiers", Theme::textSubtle());
        searchEditor.onTextChange = [this] { updateFilter(); };
        addAndMakeVisible (searchEditor);

        modifierList.setModel (this);
        modifierList.setRowHeight (28);
        modifierList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        modifierList.setColour (juce::ListBox::outlineColourId, Theme::border());
        modifierList.setOutlineThickness (1);
        addAndMakeVisible (modifierList);

        animateToggle.setToggleState (true, juce::dontSendNotification);
        animateToggle.onClick = [this]
        {
            phaseSlider.setEnabled (! animateToggle.getToggleState());
            repaint();
        };
        addAndMakeVisible (animateToggle);

        reducedMotionToggle.onClick = [this] { repaint(); };
        addAndMakeVisible (reducedMotionToggle);

        phaseSlider.setRange (0.0, 1.0, 0.001);
        phaseSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        phaseSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 22);
        phaseSlider.setValue (phase, juce::dontSendNotification);
        phaseSlider.setEnabled (false);
        phaseSlider.onValueChange = [this]
        {
            phase = static_cast<float> (phaseSlider.getValue());
            repaint();
        };
        addAndMakeVisible (phaseSlider);

        phaseLabel.setJustificationType (juce::Justification::centredRight);
        phaseLabel.setFont (ThemeFonts::getInstance().monoFont (12.0f));
        addAndMakeVisible (phaseLabel);

        exportButton.onClick = [this] { chooseContactSheetDestination(); };
        addAndMakeVisible (exportButton);

        modifierList.selectRow (0);
        startTimerHz (15);
    }

    ~GlyphLabComponent() override
    {
        stopTimer();
        modifierList.setModel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& theme = ThemeEngine::getInstance().getCurrentPalette();
        const auto palette = ControlSurfacePalette::fromTheme (theme);

        g.fillAll (palette.canvas);

        auto layout = getLocalBounds().reduced (12);
        auto titleRow = layout.removeFromTop (34);
        auto controls = layout.removeFromBottom (46);
        juce::ignoreUnused (controls);

        auto& fonts = ThemeFonts::getInstance();
        g.setColour (palette.ink);
        g.setFont (fonts.headingFont (22.0f));
        g.drawText ("GLYPH LAB", titleRow, juce::Justification::centredLeft);

        g.setColour (palette.mutedInk);
        g.setFont (fonts.monoFont (11.0f));
        g.drawText ("SHARED PRODUCTION RENDERER / FIXED NORMALIZED GEOMETRY",
                    titleRow, juce::Justification::centredRight);

        const int leftWidth = juce::jmin (250, layout.getWidth() / 4);
        layout.removeFromLeft (leftWidth);
        layout.removeFromLeft (12);

        const int rightWidth = juce::jlimit (220, 330, layout.getWidth() / 3);
        auto compactColumn = layout.removeFromRight (rightWidth);
        layout.removeFromRight (12);
        auto largeColumn = layout;

        drawSectionFrame (g, largeColumn.toFloat(), "PRODUCTION NEXT", palette);
        drawLargePreview (g, largeColumn.reduced (12, 34).toFloat(), palette);

        drawSectionFrame (g, compactColumn.toFloat(), "COMPACT / STATIC", palette);
        drawCompactPreviews (g, compactColumn.reduced (10, 34).toFloat(), palette);
    }

    void resized() override
    {
        auto layout = getLocalBounds().reduced (12);
        layout.removeFromTop (34);
        auto controls = layout.removeFromBottom (46).reduced (0, 8);

        const int leftWidth = juce::jmin (250, layout.getWidth() / 4);
        auto left = layout.removeFromLeft (leftWidth);
        searchEditor.setBounds (left.removeFromTop (30));
        left.removeFromTop (6);
        modifierList.setBounds (left);

        animateToggle.setBounds (controls.removeFromLeft (110));
        reducedMotionToggle.setBounds (controls.removeFromLeft (150));
        exportButton.setBounds (controls.removeFromRight (130));
        controls.removeFromLeft (12);
        phaseLabel.setBounds (controls.removeFromLeft (54));
        phaseSlider.setBounds (controls.removeFromLeft (juce::jmin (360, controls.getWidth())));
    }

private:
    const SessionSettings& settings;

    int getNumRows() override
    {
        return static_cast<int> (filteredTypes.size());
    }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height,
                           bool rowIsSelected) override
    {
        if (! juce::isPositiveAndBelow (row, static_cast<int> (filteredTypes.size())))
            return;

        const auto& theme = ThemeEngine::getInstance().getCurrentPalette();
        const auto palette = ControlSurfacePalette::fromTheme (theme);
        const auto type = filteredTypes[static_cast<size_t> (row)];
        auto rowBounds = juce::Rectangle<int> (0, 0, width, height);

        if (rowIsSelected)
        {
            g.setColour (palette.vermilion.withAlpha (0.12f));
            g.fillRect (rowBounds);
            g.setColour (palette.vermilion);
            g.fillRect (rowBounds.removeFromLeft (3));
        }

        g.setColour (rowIsSelected ? palette.ink : palette.mutedInk);
        g.setFont (ThemeFonts::getInstance().bodyBoldFont (13.0f));
        g.drawText (ModifierProbabilityManager::getDisplayName (type).toUpperCase(),
                    10, 0, width - 18, height, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged (int lastRowSelected) override
    {
        if (juce::isPositiveAndBelow (lastRowSelected, static_cast<int> (filteredTypes.size())))
        {
            selectedType = filteredTypes[static_cast<size_t> (lastRowSelected)];
            repaint();
        }
    }

    void timerCallback() override
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        const double elapsedSeconds = lastAnimationTickMs > 0.0
            ? juce::jlimit (0.0, 0.25, (nowMs - lastAnimationTickMs) / 1000.0)
            : (1.0 / 15.0);
        lastAnimationTickMs = nowMs;

        if (! animateToggle.getToggleState() || reducedMotionToggle.getToggleState())
            return;

        constexpr double beatsPerCycle = 4.0;
        const double bpm = juce::jlimit (20.0, 400.0, settings.bpm);
        phase += static_cast<float> ((bpm / (60.0 * beatsPerCycle)) * elapsedSeconds);
        if (phase >= 1.0f) phase -= 1.0f;
        phaseSlider.setValue (phase, juce::dontSendNotification);
        repaint();
    }

    void updateFilter()
    {
        const auto query = searchEditor.getText().trim();
        filteredTypes.clear();
        for (auto type : allTypes)
        {
            const auto name = ModifierProbabilityManager::getDisplayName (type);
            const auto category = ModifierProbabilityManager::getCategory (type);
            if (query.isEmpty() || name.containsIgnoreCase (query)
                                || category.containsIgnoreCase (query))
                filteredTypes.push_back (type);
        }

        modifierList.updateContent();
        int selectedRow = -1;
        for (int i = 0; i < static_cast<int> (filteredTypes.size()); ++i)
            if (filteredTypes[static_cast<size_t> (i)] == selectedType)
                selectedRow = i;

        if (selectedRow < 0 && ! filteredTypes.empty())
            selectedRow = 0;
        modifierList.selectRow (selectedRow);
        repaint();
    }

    ModifierGlyphState makeState (bool compact, bool forceReduced = false) const
    {
        ModifierGlyphState state;
        state.descriptor = GlyphContactSheet::makeRepresentativeDescriptor (selectedType);
        state.phase01 = phase;
        state.compact = compact;
        state.reducedMotion = forceReduced || reducedMotionToggle.getToggleState();
        return state;
    }

    static void drawSectionFrame (juce::Graphics& g, juce::Rectangle<float> bounds,
                                  const juce::String& title,
                                  const ControlSurfacePalette& palette)
    {
        g.setColour (palette.raisedTile);
        g.fillRect (bounds);
        g.setColour (palette.ink.withAlpha (0.55f));
        g.drawRect (bounds, 1.0f);
        g.setColour (palette.mutedInk);
        g.setFont (ThemeFonts::getInstance().monoBoldFont (11.0f));
        g.drawText (title, bounds.removeFromTop (28.0f).reduced (10.0f, 0.0f),
                    juce::Justification::centredLeft);
    }

    void drawLargePreview (juce::Graphics& g, juce::Rectangle<float> bounds,
                           const ControlSurfacePalette& palette) const
    {
        const auto descriptor = GlyphContactSheet::makeRepresentativeDescriptor (selectedType);
        auto tile = bounds.removeFromTop (juce::jmin (230.0f, bounds.getHeight() * 0.48f));
        g.setColour (palette.canvas);
        g.fillRect (tile);
        g.setColour (palette.ink.withAlpha (0.75f));
        g.drawRect (tile, 1.0f);

        auto meta = tile.reduced (14.0f).removeFromLeft (tile.getWidth() * 0.34f);
        g.setColour (palette.vermilion);
        g.setFont (ThemeFonts::getInstance().monoBoldFont (13.0f));
        g.drawText ("NEXT", meta.removeFromTop (22.0f), juce::Justification::centredLeft);
        g.setColour (palette.ink);
        g.setFont (ThemeFonts::getInstance().modifierNameFont (25.0f));
        g.drawFittedText (descriptor.shortName.toUpperCase(), meta.removeFromTop (48.0f).toNearestInt(),
                          juce::Justification::centredLeft, 2, 0.75f);
        g.setColour (palette.mutedInk);
        g.setFont (ThemeFonts::getInstance().monoFont (11.0f));
        g.drawText (descriptor.description.toUpperCase(), meta.removeFromBottom (20.0f),
                    juce::Justification::centredLeft);

        auto glyph = tile.reduced (14.0f);
        glyph.removeFromLeft (tile.getWidth() * 0.36f);
        g.setColour (palette.ink.withAlpha (0.45f));
        g.drawVerticalLine (juce::roundToInt (glyph.getX() - 8.0f), tile.getY(), tile.getBottom());
        ModifierGlyphRenderer::draw (g, glyph, makeState (false), palette);

        bounds.removeFromTop (18.0f);
        g.setColour (palette.mutedInk);
        g.setFont (ThemeFonts::getInstance().monoFont (12.0f));
        juce::String details;
        details << "TYPE " << static_cast<int> (selectedType)
                << "   CATEGORY " << descriptor.description.toUpperCase()
                << "   PHASE " << juce::String (phase, 3);
        g.drawText (details, bounds.removeFromTop (22.0f), juce::Justification::centredLeft);

        g.setColour (palette.ink.withAlpha (0.18f));
        for (int i = 0; i < 8; ++i)
            g.drawVerticalLine (juce::roundToInt (bounds.getX() + bounds.getWidth() * i / 7.0f),
                                bounds.getY(), bounds.getBottom());
    }

    void drawCompactPreviews (juce::Graphics& g, juce::Rectangle<float> bounds,
                              const ControlSurfacePalette& palette) const
    {
        auto queueRow = bounds.removeFromTop (juce::jmin (130.0f, bounds.getHeight() * 0.30f));
        const float cellWidth = queueRow.getWidth() / 3.0f;
        for (int i = 0; i < 3; ++i)
        {
            auto cell = queueRow.withX (queueRow.getX() + i * cellWidth)
                                .withWidth (cellWidth).reduced (3.0f);
            g.setColour (palette.canvas);
            g.fillRect (cell);
            g.setColour (palette.ink.withAlpha (0.55f));
            g.drawRect (cell, 1.0f);
            ModifierGlyphRenderer::draw (g, cell.reduced (7.0f), makeState (true), palette);
        }

        bounds.removeFromTop (18.0f);
        g.setColour (palette.mutedInk);
        g.setFont (ThemeFonts::getInstance().monoBoldFont (10.0f));
        g.drawText ("1X COMPACT", bounds.removeFromTop (18.0f), juce::Justification::centredLeft);
        auto oneX = bounds.removeFromTop (100.0f).withSizeKeepingCentre (100.0f, 100.0f);
        g.setColour (palette.canvas);
        g.fillRect (oneX);
        g.setColour (palette.ink.withAlpha (0.55f));
        g.drawRect (oneX, 1.0f);
        ModifierGlyphRenderer::draw (g, oneX.reduced (8.0f), makeState (true), palette);

        bounds.removeFromTop (14.0f);
        g.setColour (palette.mutedInk);
        g.drawText ("REDUCED MOTION", bounds.removeFromTop (18.0f), juce::Justification::centredLeft);
        auto reduced = bounds.removeFromTop (100.0f).withSizeKeepingCentre (100.0f, 100.0f);
        g.setColour (palette.canvas);
        g.fillRect (reduced);
        g.setColour (palette.ink.withAlpha (0.55f));
        g.drawRect (reduced, 1.0f);
        ModifierGlyphRenderer::draw (g, reduced.reduced (8.0f), makeState (true, true), palette);
    }

    void chooseContactSheetDestination()
    {
        const auto suggested = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                   .getChildFile ("LoopBreaker-Glyph-Contact-Sheet.png");
        exportChooser = std::make_unique<juce::FileChooser> ("Export glyph contact sheet",
                                                              suggested, "*.png");
        auto safeThis = juce::Component::SafePointer<GlyphLabComponent> (this);
        exportChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
            [safeThis] (const juce::FileChooser& chooser)
            {
                if (safeThis == nullptr) return;
                const auto result = chooser.getResult();
                if (result == juce::File()) return;

                auto destination = result.hasFileExtension ("png")
                                 ? result : result.withFileExtension ("png");
                const auto palette = ControlSurfacePalette::fromTheme (
                    ThemeEngine::getInstance().getCurrentPalette());
                if (! GlyphContactSheet::exportPng (destination, palette))
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                             "Glyph export failed",
                                                             "The contact sheet could not be written to:\n"
                                                               + destination.getFullPathName());
                }
            });
    }

    juce::TextEditor searchEditor;
    juce::ListBox modifierList;
    juce::ToggleButton animateToggle { "Animate" };
    juce::ToggleButton reducedMotionToggle { "Reduced motion" };
    juce::Slider phaseSlider;
    juce::Label phaseLabel { {}, "PHASE" };
    juce::TextButton exportButton { "Export PNG..." };
    std::unique_ptr<juce::FileChooser> exportChooser;

    std::vector<ModifierType> allTypes;
    std::vector<ModifierType> filteredTypes;
    ModifierType selectedType = ModifierType::Reverse;
    float phase = 0.25f;
    double lastAnimationTickMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlyphLabComponent)
};
