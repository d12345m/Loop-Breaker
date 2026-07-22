#pragma once

#include <JuceHeader.h>
#include "ModifierGlyphRenderer.h"
#include "ModifierProbabilityManager.h"
#include "ModifierRegistry.h"
#include "ThemeFonts.h"

/** Debug-only review surface for the production modifier glyph renderer. */
class GlyphLabComponent final : public juce::Component,
                                private juce::ListBoxModel,
                                private juce::Timer
{
public:
    GlyphLabComponent()
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
        if (! animateToggle.getToggleState() || reducedMotionToggle.getToggleState())
            return;

        const auto speed = ThemeEngine::getInstance().getAnimationConfig().animationSpeed;
        phase += (1.0f / 15.0f) * speed * 0.45f;
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

    ModifierDescriptor makePreviewDescriptor (ModifierType type) const
    {
        ModifierDescriptor descriptor = ModifierRegistry::makeDescriptor (type);

        switch (type)
        {
            case ModifierType::Speed:                   descriptor.plannedSpeed = 2.0; break;
            case ModifierType::Stretch:                 descriptor.plannedStretch = 0.5; break;
            case ModifierType::BeatSliceRandom:         descriptor.plannedSliceDivision = "1/16"; break;
            case ModifierType::ArpSlice:
                descriptor.plannedArpSequenceLength = 4;
                descriptor.plannedArpTotalSlices = 16;
                descriptor.plannedArpRepeatBars = 2;
                break;
            case ModifierType::SliceRepeater:
                descriptor.plannedSliceRepeaterReps = 8;
                descriptor.plannedSliceRepeaterTotal = 16;
                break;
            case ModifierType::PingPong:                descriptor.plannedPingPongDivision = 0.125; break;
            case ModifierType::BufferDelayOn:
            case ModifierType::BufferDelayDubBurst:
                descriptor.plannedDelayDivision = "1/8";
                descriptor.plannedDelayWet = 0.5;
                descriptor.plannedDelayFeedback = 0.65;
                break;
            case ModifierType::BufferReverbOn:
                descriptor.plannedWet = 0.5;
                descriptor.plannedFxFadeBars = 2.0;
                break;
            case ModifierType::BufferChorusOn:
                descriptor.plannedChorusMix = 0.5;
                descriptor.plannedChorusDepth = 0.7;
                descriptor.plannedChorusRateHz = 0.8;
                break;
            case ModifierType::BufferAutoPan:
                descriptor.plannedPanMix = 0.75;
                descriptor.plannedPanDepth = 0.8;
                descriptor.plannedPanRateHz = 0.5;
                break;
            case ModifierType::BufferSHLowPassOn:
            case ModifierType::BufferSHHighPassOn:     descriptor.plannedSHDivisionBars = 0.125; break;
            case ModifierType::BufferGranularOn:
            case ModifierType::BufferGranularMomentary:
                descriptor.plannedGrainDensityHz = 18.0;
                descriptor.plannedGrainSizeMs = 180.0;
                descriptor.plannedGrainPitchSpread = 12.0;
                descriptor.plannedGrainMix = 0.65;
                break;
            default: break;
        }
        return descriptor;
    }

    ModifierGlyphState makeState (bool compact, bool forceReduced = false) const
    {
        ModifierGlyphState state;
        state.descriptor = makePreviewDescriptor (selectedType);
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
        const auto descriptor = makePreviewDescriptor (selectedType);
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
                if (! safeThis->exportContactSheet (destination))
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                             "Glyph export failed",
                                                             "The contact sheet could not be written to:\n"
                                                               + destination.getFullPathName());
                }
            });
    }

    bool exportContactSheet (const juce::File& destination) const
    {
        constexpr int imageWidth = 1800;
        constexpr int headerHeight = 72;
        constexpr int rowHeight = 158;
        constexpr int nameWidth = 270;
        constexpr int phaseWidth = 300;
        constexpr int compactWidth = 220;
        const int imageHeight = headerHeight + rowHeight * static_cast<int> (allTypes.size());

        juce::Image image (juce::Image::RGB, imageWidth, imageHeight, true);
        juce::Graphics g (image);
        const auto& theme = ThemeEngine::getInstance().getCurrentPalette();
        const auto palette = ControlSurfacePalette::fromTheme (theme);
        auto& fonts = ThemeFonts::getInstance();

        g.fillAll (palette.canvas);
        g.setColour (palette.ink);
        g.setFont (fonts.headingFont (28.0f));
        g.drawText ("LOOP BREAKER / GLYPH CONTACT SHEET", 24, 12, 760, 36,
                    juce::Justification::centredLeft);
        g.setColour (palette.mutedInk);
        g.setFont (fonts.monoFont (13.0f));
        g.drawText ("PHASE 0.00       PHASE 0.25       PHASE 0.50       PHASE 0.75       COMPACT",
                    nameWidth, 18, imageWidth - nameWidth - 20, 30,
                    juce::Justification::centredLeft);

        const float phases[] = { 0.0f, 0.25f, 0.5f, 0.75f };
        for (int row = 0; row < static_cast<int> (allTypes.size()); ++row)
        {
            const auto type = allTypes[static_cast<size_t> (row)];
            const auto descriptor = makePreviewDescriptor (type);
            const int top = headerHeight + row * rowHeight;

            g.setColour ((row % 2 == 0 ? palette.raisedTile : palette.canvas).withAlpha (0.92f));
            g.fillRect (0, top, imageWidth, rowHeight);
            g.setColour (palette.ink.withAlpha (0.24f));
            g.drawHorizontalLine (top, 0.0f, static_cast<float> (imageWidth));

            g.setColour (palette.ink);
            g.setFont (fonts.bodyBoldFont (18.0f));
            g.drawFittedText (descriptor.shortName.toUpperCase(), 20, top + 36,
                              nameWidth - 36, 34, juce::Justification::centredLeft, 1);
            g.setColour (palette.mutedInk);
            g.setFont (fonts.monoFont (11.0f));
            g.drawText (descriptor.description.toUpperCase(), 20, top + 75,
                        nameWidth - 36, 24, juce::Justification::centredLeft);

            for (int column = 0; column < 4; ++column)
            {
                auto cell = juce::Rectangle<float> (static_cast<float> (nameWidth + column * phaseWidth),
                                                     static_cast<float> (top),
                                                     static_cast<float> (phaseWidth),
                                                     static_cast<float> (rowHeight));
                g.setColour (palette.ink.withAlpha (0.18f));
                g.drawVerticalLine (juce::roundToInt (cell.getX()), cell.getY(), cell.getBottom());
                ModifierGlyphState state;
                state.descriptor = descriptor;
                state.phase01 = phases[column];
                ModifierGlyphRenderer::draw (g, cell.reduced (72.0f, 10.0f), state, palette);
            }

            auto compactCell = juce::Rectangle<float> (
                static_cast<float> (nameWidth + 4 * phaseWidth), static_cast<float> (top),
                static_cast<float> (compactWidth), static_cast<float> (rowHeight));
            g.setColour (palette.ink.withAlpha (0.18f));
            g.drawVerticalLine (juce::roundToInt (compactCell.getX()), compactCell.getY(), compactCell.getBottom());
            ModifierGlyphState compactState;
            compactState.descriptor = descriptor;
            compactState.phase01 = 0.25f;
            compactState.compact = true;
            compactState.reducedMotion = true;
            ModifierGlyphRenderer::draw (g, compactCell.withSizeKeepingCentre (104.0f, 104.0f),
                                         compactState, palette);
        }

        destination.deleteFile();
        juce::FileOutputStream stream (destination);
        if (! stream.openedOk()) return false;
        const bool written = juce::PNGImageFormat().writeImageToStream (image, stream);
        stream.flush();
        return written && stream.getStatus().wasOk();
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlyphLabComponent)
};
