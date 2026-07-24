/*
 ============================================================================== 
   PadGridComponent.h
   --------------------------------------------------------------------------
   Platform-sized grid of pad toggle buttons representing the audio buffers.
   Provides selection state for scheduling modifiers (user targeted buffers).
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ThemeEngine.h"
#include "ThemeFonts.h"
#include "Animator.h"
#include "ModifierStickerOverlay.h"
#include "PlatformConfig.h"

// Simple 2x4 pad grid showing selectable pads and (new) filename indicators.
class PadGridComponent : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer,
                         private juce::ChangeListener
{
public:
    void setPortraitLayout (bool shouldUsePortraitLayout)
    {
        if (portraitLayout == shouldUsePortraitLayout)
            return;

        portraitLayout = shouldUsePortraitLayout;
        resized();
        repaint();
    }

    PadGridComponent()
    {
        for (int i = 0; i < numPads; ++i)
            padFileNames.add({});

        // Ensure the local AudioFormatManager knows basic formats so thumbnails can read files
        if (formatManager.getNumKnownFormats() == 0)
            formatManager.registerBasicFormats();
        for (int i = 0; i < numPads; ++i)
        {
            auto* btn = padButtons.add(new juce::ToggleButton(""));
            btn->setLookAndFeel(&invisibleToggleLF);
            btn->setClickingTogglesState(true);
            btn->onClick = [this, btn, i]
            {
                juce::ignoreUnused (i);
               #if JUCE_IOS
                const bool padIsEmpty = i >= padFileNames.size()
                                     || padFileNames[i].isEmpty();
                if (padIsEmpty && onLoadSampleRequest)
                {
                    // An empty pad is drawn with a large "+" affordance. On a
                    // touch-only device, make that primary tap open Files
                    // instead of requiring an inaccessible right-click menu.
                    btn->setToggleState (false, juce::dontSendNotification);
                    onLoadSampleRequest (i);
                    return;
                }
               #endif

                // Skip selection change if Shift (MIDI learn), Cmd (clear MIDI), or right-click was held
                auto mods = juce::ModifierKeys::currentModifiers;
                if (mods.isShiftDown() || mods.isCommandDown() || mods.isAltDown()
                    || mods.isPopupMenu())
                {
                    // Revert the toggle since we don't want to change selection
                    btn->setToggleState(!btn->getToggleState(), juce::dontSendNotification);
                    return;
                }
                if (selectionChanged) selectionChanged(); 
            };
            
            // Add mouse listener for MIDI learn (Shift+Click) and clear (Cmd+Click)
            btn->addMouseListener(this, false);
            
            addAndMakeVisible(btn);

            auto* label = padFileLabels.add(new juce::Label());
            label->setJustificationType(juce::Justification::centred);
            label->setFont(ThemeFonts::getInstance().controlLabelFont(14.0f));
            label->setInterceptsMouseClicks(false, false);
            label->setColour(juce::Label::textColourId, Theme::textSubtle());
            addAndMakeVisible(label);
            label->setText("", juce::dontSendNotification);

            // Initialize per-pad waveform state
            auto* thumb = new juce::AudioThumbnail(512, formatManager, thumbnailCache);
            thumbnails.add(thumb);
            thumb->addChangeListener(this);
            // no per-pad owned FileInputSource; AudioThumbnail manages its own InputSource lifetime
            playheadSamples[(size_t)i] = 0.0;
            loopEnabled[(size_t)i] = false;
            loopStartSamples[(size_t)i] = 0.0;
            loopEndSamples[(size_t)i] = 0.0;
        }

        startTimerHz (LoopBreakerConfig::uiRefreshRateHz);
    }

    // Returns indices of pads whose toggle state is on (selected by user for modifiers)
    juce::Array<int> getSelectedPadIndices() const
    {
        juce::Array<int> indices;
        for (int i = 0; i < padButtons.size(); ++i)
            if (padButtons[i]->getToggleState())
                indices.add(i);
        return indices;
    }

    // Toggle a pad's selection state (for MIDI control)
    void togglePadSelection(int padIndex)
    {
        if (!juce::isPositiveAndBelow(padIndex, numPads)) return;
        auto* btn = padButtons[padIndex];
        btn->setToggleState(!btn->getToggleState(), juce::sendNotification);
    }

    // Set MIDI note for a pad
    void setMidiNoteForPad(int padIndex, int midiNote)
    {
        if (!juce::isPositiveAndBelow(padIndex, numPads)) return;
        midiNotes[(size_t)padIndex] = midiNote;
        repaint();
    }

    // Get MIDI note for a pad
    int getMidiNoteForPad(int padIndex) const
    {
        if (!juce::isPositiveAndBelow(padIndex, numPads)) return -1;
        return midiNotes[(size_t)padIndex];
    }

    // Set MIDI learn mode for a pad
    void setMidiLearnForPad(int padIndex, bool learning)
    {
        if (!juce::isPositiveAndBelow(padIndex, numPads)) return;
        midiLearnActive[(size_t)padIndex] = learning;
        repaint();
    }

    std::function<void(int padIndex)> onMidiLearnRequest;
    std::function<void(int padIndex)> onClearMidiNote;
    std::function<void(int padIndex)> onClearSample;
    std::function<void(int padIndex)> onLoadSampleRequest;

    // Provide an AudioFormatManager to use for reading files (prepared by the app)
    void setAudioFormatManager(juce::AudioFormatManager* afm)
    {
        formatManagerPtr = afm;
        // Register formats on the provided manager
        if (formatManagerPtr != nullptr && formatManagerPtr->getNumKnownFormats() == 0)
            formatManagerPtr->registerBasicFormats();
        // Recreate thumbnails to use the provided manager reference (AudioThumbnail captures by reference in ctor)
        thumbnails.clear(true);
        for (int i = 0; i < numPads; ++i)
        {
            juce::AudioFormatManager& mgr = (formatManagerPtr != nullptr ? *formatManagerPtr : formatManager);
            auto* thumb = new juce::AudioThumbnail(512, mgr, thumbnailCache);
            thumbnails.add(thumb);
            thumb->addChangeListener(this);
        }
        // Reapply any existing file paths to rebuild sources on the new thumbnails
        for (int i = 0; i < numPads; ++i)
        {
            if (i < padFileNames.size())
                setPadFilePath(i, padFileNames[i]);
        }
        repaint();
    }

    // Rebuild thumbnails from file paths
    void setPadFilePaths(const juce::StringArray& filePaths)
    {
        const int n = juce::jmin(numPads, filePaths.size());
        for (int i = 0; i < n; ++i)
            setPadFilePath(i, filePaths[i]);
        repaint();
    }

    void setPadFilePath(int padIndex, const juce::String& filePath)
    {
        if (!juce::isPositiveAndBelow(padIndex, numPads)) return;
        padFileNames.set(padIndex, filePath);
        updatePadLabel(padIndex);

        if (filePath.isEmpty())
            clearModifierStickersForPad(padIndex);

        auto* thumb = thumbnails[padIndex];
        thumb->clear();
        if (filePath.isNotEmpty() && juce::File(filePath).existsAsFile())
        {
            auto& afm = (formatManagerPtr != nullptr ? *formatManagerPtr : formatManager);
            if (afm.getNumKnownFormats() == 0)
                afm.registerBasicFormats();

            // AudioThumbnail takes ownership of the InputSource pointer; do NOT keep our own copy.
            thumb->setSource(new juce::FileInputSource(juce::File(filePath)));
            // The thumbnail will load asynchronously and notify via change listener.
        }
        else
        {
            // No file: leave thumbnail cleared
        }
        repaint();
    }

    // Update playhead and loop info for drawing
    void setPlayheadForPad(int padIndex, double samples)
    {
        if (!juce::isPositiveAndBelow(padIndex, numPads)) return;
        playheadSamples[(size_t)padIndex] = samples;
        // Throttled repaint to reduce overhead but keep UI responsive
    }

    void setLoopWindowForPad(int padIndex, bool enabled, double startSamples, double endSamples)
    {
        if (!juce::isPositiveAndBelow(padIndex, numPads)) return;
        loopEnabled[(size_t)padIndex] = enabled;
        loopStartSamples[(size_t)padIndex] = startSamples;
        loopEndSamples[(size_t)padIndex] = endSamples;
    }

    void clearSelections()
    {
        for (auto* b : padButtons) b->setToggleState(false, juce::dontSendNotification);
        if (selectionChanged) selectionChanged();
    }

    // Set (or clear) the displayed filename for a pad. Pass empty string to clear.
    void setPadFileName(int padIndex, const juce::String& fileName)
    {
    if (!juce::isPositiveAndBelow(padIndex, numPads)) return;
        padFileNames.set(padIndex, fileName);
        updatePadLabel(padIndex);
    }

    juce::String getPadFileName(int padIndex) const
    {
    return juce::isPositiveAndBelow(padIndex, padFileNames.size()) ? padFileNames[padIndex] : juce::String();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(4);
        const int cols = portraitLayout ? 2 : (numPads + 1) / 2;
        const int rows = (numPads + cols - 1) / cols;
        const int padW = area.getWidth() / cols;
        const int padH = area.getHeight() / rows;
        const int labelHeight = 22;

        for (int r = 0; r < rows; ++r)
        {
            for (int c = 0; c < cols; ++c)
            {
                // Keep the lowest-numbered pads at the bottom in either
                // orientation (groups of four landscape, pairs portrait).
                int idx = (rows - 1 - r) * cols + c;
                if (idx < padButtons.size())
                {
                    juce::Rectangle<int> cell (area.getX() + c * padW,
                                               area.getY() + r * padH,
                                               padW, padH);
                    auto padRect = cell.reduced (4);
                    padButtons[idx]->setBounds (padRect);
                    padFileLabels[idx]->setBounds (
                        padRect.withTrimmedTop (padRect.getHeight() - labelHeight)
                               .reduced (24, 1));
                }
            }
        }
    }

private:
    static constexpr int numPads = LoopBreakerConfig::numPads;
    bool portraitLayout = false;
    // Look-and-feel that suppresses the default checkbox visuals of ToggleButton
    class InvisibleToggleLookAndFeel : public juce::LookAndFeel_V4 {
    public:
        void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool, bool) override {}
    };

    InvisibleToggleLookAndFeel invisibleToggleLF;  // must be declared before padButtons so it outlives them
    juce::OwnedArray<juce::ToggleButton> padButtons;
    juce::OwnedArray<juce::Label> padFileLabels;
    juce::StringArray padFileNames;
    std::array<int, numPads> flashCounters {};
    std::array<float, numPads> glowAlpha {};
    std::array<Animator, numPads> glowAnimators;
    float midiLearnDashOffset = 0.0f;  // marching ants
    std::array<bool, numPads> playingStates {};
    std::array<ModifierStickerOverlay::Mask, numPads> activeStickerMasks {};
    std::array<ModifierStickerOverlay::Mask, numPads> transientStickerMasks {};
    float stickerGlyphPhase = 0.0f;
    std::array<std::array<double, static_cast<size_t>(ModifierType::Unknown)>,
               numPads> transientStickerExpiryMs {};
    std::array<int, numPads> midiNotes = [] {
        std::array<int, numPads> notes {};
        for (int i = 0; i < numPads; ++i)
            notes[static_cast<size_t> (i)] = 36 + i;
        return notes;
    }();
    std::array<bool, numPads> midiLearnActive {};

    // Waveform state
    juce::AudioFormatManager formatManager;
    juce::AudioFormatManager* formatManagerPtr { nullptr };
    juce::AudioThumbnailCache thumbnailCache { 64 };
    juce::OwnedArray<juce::AudioThumbnail> thumbnails;
    std::array<double, numPads> playheadSamples {};
    std::array<bool,   numPads> loopEnabled {};
    std::array<double, numPads> loopStartSamples {};
    std::array<double, numPads> loopEndSamples {};
    // Total file length in samples for each pad (used to map loop/playhead across full waveform)
    std::array<double, numPads> totalFileSamples {};

public:
    // Callback for external listeners when any pad selection toggles.
    std::function<void()> selectionChanged;

    // Callback invoked when files are dropped on a specific pad.
    // The StringArray contains full file paths.
    std::function<void(int /*padIndex*/, const juce::StringArray& /*files*/)> filesDroppedOnPad;

    void setSelectionChangedCallback(std::function<void()> cb) { selectionChanged = std::move(cb); }

    void setFilesDroppedOnPadCallback(std::function<void(int, const juce::StringArray&)> cb)
    {
        filesDroppedOnPad = std::move(cb);
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& f : files)
        {
            const auto ext = juce::File(f).getFileExtension().toLowerCase();
            if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".mp3")
                return true;
        }
        return false;
    }

    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        if (filesDroppedOnPad == nullptr)
            return;

        const int padIndex = getPadIndexAt({ x, y });
        if (! juce::isPositiveAndBelow(padIndex, numPads))
            return;

        isFileDragActive = false;
        hoveredPadIndex = -1;
        dragAudioFileCount = 0;
        repaint();

        filesDroppedOnPad(padIndex, files);
    }

    void fileDragEnter (const juce::StringArray& files, int x, int y) override
    {
        if (! isInterestedInFileDrag(files))
            return;

        isFileDragActive = true;
        dragAudioFileCount = countAudioFiles(files);
        hoveredPadIndex = getPadIndexAt({ x, y });
        repaint();
    }

    void fileDragMove (const juce::StringArray& files, int x, int y) override
    {
        if (! isInterestedInFileDrag(files))
            return;

        isFileDragActive = true;
        dragAudioFileCount = countAudioFiles(files);

        const int newHover = getPadIndexAt({ x, y });
        if (newHover != hoveredPadIndex)
        {
            hoveredPadIndex = newHover;
            repaint();
        }
    }

    void fileDragExit (const juce::StringArray& /*files*/) override
    {
        isFileDragActive = false;
        hoveredPadIndex = -1;
        dragAudioFileCount = 0;
        repaint();
    }

    // Set total sample length for a pad (for full-file visualization mapping)
    void setTotalSamplesForPad(int padIndex, double totalSamples)
    {
        if (!juce::isPositiveAndBelow(padIndex, numPads)) return;
        totalFileSamples[(size_t)padIndex] = totalSamples;
    }

    // Trigger a short flash on given pad indices (or all if list empty and global event)
    void flashPads(const juce::Array<int>& padIndices)
    {
        const auto& anim = ThemeEngine::getInstance().getAnimationConfig();
        const bool animate = anim.enabled && anim.padPulseOnTrigger;

        auto triggerPad = [&] (int idx)
        {
            flashCounters[(size_t)idx] = flashDurationTicks;
            if (animate)
            {
                glowAnimators[(size_t)idx].start (400, [this, idx] (float p)
                {
                    glowAlpha[(size_t)idx] = 0.5f * (1.0f - p);  // 0.5 → 0
                    repaint();
                }, {}, Animator::Easing::EaseOut);
            }
        };

        if (padIndices.isEmpty())
        {
            for (int i = 0; i < numPads; ++i) triggerPad (i);
        }
        else
        {
            for (auto idx : padIndices)
                if (juce::isPositiveAndBelow(idx, numPads))
                    triggerPad (idx);
        }
        repaint();
    }

    // Update which pads are currently playing; expects indices from engine each refresh.
    void setPlayingStates(const juce::Array<int>& playingIndices)
    {
        std::array<bool, numPads> newStates {};
        for (auto idx : playingIndices)
            if (juce::isPositiveAndBelow(idx, numPads))
                newStates[(size_t)idx] = true;

        if (newStates != playingStates)
        {
            playingStates = newStates;
            repaint();
        }
    }

    void setModifierStickerMask(int padIndex, ModifierStickerOverlay::Mask mask)
    {
        if (! juce::isPositiveAndBelow(padIndex, numPads))
            return;

        auto& current = activeStickerMasks[static_cast<size_t>(padIndex)];
        if (current != mask)
        {
            current = mask;
            repaint(padButtons[padIndex]->getBounds());
        }
    }

    /** Shows a short-lived utility sticker. Empty targets means every pad. */
    void showTransientModifierSticker(ModifierType type,
                                      const juce::Array<int>& padIndices,
                                      double durationMs = 900.0)
    {
        const auto bit = ModifierStickerOverlay::bitForType(type);
        const int typeIndex = static_cast<int>(type);
        if (bit == 0
            || ! juce::isPositiveAndBelow(typeIndex,
                                          static_cast<int>(ModifierType::Unknown)))
            return;

        const double expiresAt = juce::Time::getMillisecondCounterHiRes()
                               + juce::jmax(100.0, durationMs);
        const auto applyToPad = [this, bit, typeIndex, expiresAt] (int padIndex)
        {
            if (! juce::isPositiveAndBelow(padIndex, numPads))
                return;

            transientStickerMasks[static_cast<size_t>(padIndex)] |= bit;
            auto& expiry = transientStickerExpiryMs[static_cast<size_t>(padIndex)]
                                                    [static_cast<size_t>(typeIndex)];
            expiry = juce::jmax(expiry, expiresAt);
        };

        if (padIndices.isEmpty())
            for (int i = 0; i < numPads; ++i)
                applyToPad(i);
        else
            for (const int padIndex : padIndices)
                applyToPad(padIndex);

        repaint();
    }

    /** Clears active and transient stickers on the targets; empty means all. */
    void clearModifierStickers(const juce::Array<int>& padIndices = {})
    {
        if (padIndices.isEmpty())
        {
            for (int i = 0; i < numPads; ++i)
                clearModifierStickersForPad(i);
        }
        else
        {
            for (const int padIndex : padIndices)
                clearModifierStickersForPad(padIndex);
        }

        repaint();
    }

private:
    static constexpr int flashDurationTicks =
        (LoopBreakerConfig::uiRefreshRateHz * 2) / 3; // ~667 ms

    void clearModifierStickersForPad(int padIndex)
    {
        if (! juce::isPositiveAndBelow(padIndex, numPads))
            return;

        activeStickerMasks[static_cast<size_t>(padIndex)] = 0;
        transientStickerMasks[static_cast<size_t>(padIndex)] = 0;
        transientStickerExpiryMs[static_cast<size_t>(padIndex)].fill(0.0);
    }

    void updatePadLabel(int padIndex)
    {
    if (!juce::isPositiveAndBelow(padIndex, padFileLabels.size())) return;
        const auto stored = padFileNames[padIndex];
        if (stored.isEmpty())
        {
            padFileLabels[padIndex]->setText("", juce::dontSendNotification);
            return;
        }

        // Determine whether this looks like a file path and whether it's missing on disk.
        const juce::File f(stored);
        const bool looksLikePath = juce::File::isAbsolutePath(stored) || stored.containsChar('/') || stored.containsChar('\\');
        const bool missingOnDisk = looksLikePath && ! f.existsAsFile();
        const bool isMissing = stored.equalsIgnoreCase("[missing]") || missingOnDisk;

        // Display just the basename for paths; keep the full path in state.
        auto name = looksLikePath ? f.getFileNameWithoutExtension() : stored;
        if (name.isEmpty())
            name = isMissing ? juce::String("Missing") : juce::String();

        if (isMissing)
            name << "!";

        // Truncate long names for compact display
        const int maxChars = 10;
        if (name.length() > maxChars)
            name = name.substring(0, maxChars - 3) + "...";
        // Colour-code missing pads to draw attention
        padFileLabels[padIndex]->setColour(juce::Label::textColourId, isMissing ? Theme::bad() : Theme::textSubtle());
        padFileLabels[padIndex]->setText(name, juce::dontSendNotification);
    }

    int getPadIndexAt(juce::Point<int> p) const
    {
        for (int i = 0; i < numPads; ++i)
        {
            if ((padButtons[i] != nullptr && padButtons[i]->getBounds().contains(p))
                || (padFileLabels[i] != nullptr && padFileLabels[i]->getBounds().contains(p)))
            {
                return i;
            }
        }
        return -1;
    }

    void paint(juce::Graphics& g) override
    {
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();
        const float cr = palette.borderRadius;

        // Draw waveform, overlays, then state/flash
        for (int i = 0; i < numPads; ++i)
        {
            if (auto* btn = padButtons[i])
            {
                auto r = btn->getBounds().toFloat();

                const bool hasWave = (thumbnails[i] != nullptr && thumbnails[i]->getTotalLength() > 0.0);
                const bool isSelected = padButtons[i]->getToggleState();
                const auto apertureFill = hasWave
                    ? (isSelected ? palette.padLoadedSelected : palette.padLoaded)
                    : (isSelected ? palette.padEmptySelected : palette.padEmpty);

                // ── Outer tile ──
                g.setColour (palette.padEmpty);
                g.fillRoundedRectangle(r, cr);
                g.setColour(palette.border);
                g.drawRoundedRectangle(r, cr, 1.0f);

                // The filename/status rail lives inside the tile, below the
                // instrument aperture.
                auto aperture = r.reduced (5.0f);
                aperture.removeFromBottom (22.0f);
                auto inner = aperture.reduced (6.0f);

                if (hasWave)
                {
                    auto* thumb = thumbnails[i];

                    // The four base pad states are complete opaque colours.
                    // Choosing one here avoids state-dependent alpha blending.
                    g.setColour (apertureFill);
                    g.fillRect (aperture);
                    g.setColour (palette.border.withAlpha (0.65f));
                    g.drawRect (aperture, 1.0f);

                    // Oscilloscope grid lines (4 horizontal divisions)
                    g.setColour(palette.border.withAlpha(0.3f));
                    for (int line = 1; line <= 3; ++line)
                    {
                        float ly = inner.getY() + inner.getHeight() * ((float)line / 4.0f);
                        g.drawHorizontalLine((int)ly, inner.getX(), inner.getRight());
                    }

                    const auto drawWaveform = [&] (float alpha)
                    {
                        g.setColour (palette.waveformFill.withAlpha (alpha));
                        thumb->drawChannels(g, inner.toNearestInt(), 0.0,
                                            thumb->getTotalLength(), 1.0f);
                    };

                    if (loopEnabled[(size_t)i])
                    {
                        const double denom = juce::jmax(1.0, totalFileSamples[(size_t)i]);
                        const double startProp = juce::jlimit(0.0, 1.0, loopStartSamples[(size_t)i] / denom);
                        const double endProp   = juce::jlimit(0.0, 1.0, loopEndSamples[(size_t)i]   / denom);
                        const float x1 = inner.getX() + (float)(inner.getWidth() * startProp);
                        const float x2 = inner.getX() + (float)(inner.getWidth() * endProp);

                        // Keep the state fill untouched: distinguish the loop
                        // by de-emphasising waveform content outside it.
                        drawWaveform (0.30f);
                        {
                            g.saveState();
                            g.reduceClipRegion (
                                juce::Rectangle<float> (x1, inner.getY(),
                                                        juce::jmax (1.0f, x2 - x1),
                                                        inner.getHeight())
                                    .toNearestInt());
                            drawWaveform (0.82f);
                            g.restoreState();
                        }

                        // Inward-facing caps make the boundary lines read as
                        // one bracketed region rather than two playheads.
                        g.setColour(palette.warn.withAlpha(0.85f));
                        g.drawLine(x1, inner.getY(), x1, inner.getBottom(), 1.5f);
                        g.drawLine(x2, inner.getY(), x2, inner.getBottom(), 1.5f);
                        constexpr float capLength = 5.0f;
                        g.drawLine(x1, inner.getY(), x1 + capLength, inner.getY(), 1.5f);
                        g.drawLine(x1, inner.getBottom(), x1 + capLength, inner.getBottom(), 1.5f);
                        g.drawLine(x2 - capLength, inner.getY(), x2, inner.getY(), 1.5f);
                        g.drawLine(x2 - capLength, inner.getBottom(), x2, inner.getBottom(), 1.5f);
                    }
                    else
                    {
                        drawWaveform (0.78f);
                    }

                    // Playhead with triangle head and glow
                    {
                        const double phDenom = juce::jmax(1.0, totalFileSamples[(size_t)i]);
                        const double phProp  = juce::jlimit(0.0, 1.0, playheadSamples[(size_t)i] / phDenom);
                        const float phx = inner.getX() + (float)(inner.getWidth() * phProp);

                        // Faint vertical glow behind playhead
                        g.setColour(palette.playhead.withAlpha(0.12f));
                        g.fillRect(phx - 2.0f, inner.getY(), 4.0f, inner.getHeight());

                        // Playhead line
                        g.setColour(palette.playhead);
                        g.drawLine(phx, inner.getY() + 4.0f, phx, inner.getBottom(), 2.0f);

                        // Triangle head at top
                        juce::Path tri;
                        tri.addTriangle(phx - 3.0f, inner.getY(),
                                        phx + 3.0f, inner.getY(),
                                        phx, inner.getY() + 5.0f);
                        g.fillPath(tri);
                    }
                }
                else
                {
                    // ── Empty pad: four load brackets + "+" icon ──
                    g.setColour (apertureFill);
                    g.fillRect (aperture);

                    {
                        const float bracket = juce::jlimit (8.0f, 16.0f,
                                                           aperture.getWidth() * 0.08f);
                        juce::Path brackets;
                        brackets.startNewSubPath (aperture.getX(), aperture.getY() + bracket);
                        brackets.lineTo (aperture.getX(), aperture.getY());
                        brackets.lineTo (aperture.getX() + bracket, aperture.getY());
                        brackets.startNewSubPath (aperture.getRight() - bracket, aperture.getY());
                        brackets.lineTo (aperture.getRight(), aperture.getY());
                        brackets.lineTo (aperture.getRight(), aperture.getY() + bracket);
                        brackets.startNewSubPath (aperture.getX(), aperture.getBottom() - bracket);
                        brackets.lineTo (aperture.getX(), aperture.getBottom());
                        brackets.lineTo (aperture.getX() + bracket, aperture.getBottom());
                        brackets.startNewSubPath (aperture.getRight() - bracket, aperture.getBottom());
                        brackets.lineTo (aperture.getRight(), aperture.getBottom());
                        brackets.lineTo (aperture.getRight(), aperture.getBottom() - bracket);
                        g.setColour (palette.border.withAlpha (0.55f));
                        g.strokePath (brackets, juce::PathStrokeType (1.0f));
                    }

                    // "+" icon centered
                    g.setColour(palette.textSecondary.withAlpha(0.5f));
                    const float plusSize = 14.0f;
                    const float cx = aperture.getCentreX();
                    const float cy = aperture.getCentreY();
                    g.drawLine(cx - plusSize * 0.5f, cy, cx + plusSize * 0.5f, cy, 1.5f);
                    g.drawLine(cx, cy - plusSize * 0.5f, cx, cy + plusSize * 0.5f, 1.5f);
                }

                // ── Selection indicators ──
                if (isSelected)
                {
                    g.setColour (palette.padSelectedIndicator);
                    g.fillEllipse (r.getX() + 8.0f, r.getBottom() - 14.0f, 5.0f, 5.0f);
                    const float underlineWidth = juce::jmin (52.0f, r.getWidth() * 0.28f);
                    g.fillRect (r.getCentreX() - underlineWidth * 0.5f,
                                r.getBottom() - 3.0f, underlineWidth, 2.0f);
                }

                // ── MIDI learn indicator (marching ants) ──
                if (midiLearnActive[(size_t)i])
                {
                    // Marching-ants dashed border
                    {
                        juce::Path outline;
                        outline.addRoundedRectangle(r.expanded(2.0f), cr);
                        const float dashLengths[] = { 6.0f, 4.0f };
                        juce::PathStrokeType strokeType(2.5f);
                        juce::Path dashed;
                        strokeType.createDashedStroke(dashed, outline, dashLengths, 2);
                        g.setColour(palette.warn);
                        g.strokePath(dashed, strokeType);
                    }
                    // "LEARN" badge
                    auto learnRect = juce::Rectangle<float>(r.getCentreX() - 28.0f, r.getCentreY() - 10.0f, 56.0f, 20.0f);
                    g.setColour(palette.warn);
                    g.fillRoundedRectangle(learnRect, 3.0f);
                    g.setColour(palette.bg);
                    g.setFont(ThemeFonts::getInstance().monoBoldFont(13.0f));
                    g.drawText("LEARN", learnRect, juce::Justification::centred);
                }

                // ── Playing state glow outline ──
                if (playingStates[(size_t)i])
                {
                    g.setColour (palette.padPlaying);
                    g.fillEllipse (r.getRight() - 13.0f, r.getBottom() - 14.0f, 5.0f, 5.0f);
                }

                // ── Flash overlay + animated glow pulse (modifier triggered) ──
                if (flashCounters[(size_t)i] > 0 || glowAlpha[(size_t)i] > 0.001f)
                {
                    // Solid tint overlay (always, even with animation off)
                    float flashAlpha = (float)flashCounters[(size_t)i] / (float)flashDurationTicks * 0.25f;
                    if (flashAlpha > 0.0f)
                    {
                        g.setColour(palette.padFlash.withAlpha(flashAlpha));
                        g.fillRect (aperture);
                    }

                    // Animated radial glow (only when glow animator is active)
                    if (glowAlpha[(size_t)i] > 0.001f)
                    {
                        juce::ColourGradient glow(palette.padFlash.withAlpha(glowAlpha[(size_t)i]),
                                                   aperture.getCentreX(), aperture.getCentreY(),
                                                   palette.padFlash.withAlpha(0.0f),
                                                   aperture.getX(), aperture.getY(), true);
                        g.setGradientFill(glow);
                        g.fillRect (aperture);
                    }
                }

                // ── Applied modifier stickers ──
                if (! midiLearnActive[(size_t)i])
                {
                    const auto momentaryEffectBits =
                        ModifierStickerOverlay::bitForType (
                            ModifierType::BufferDelayDubBurst)
                      | ModifierStickerOverlay::bitForType (
                            ModifierType::BufferShhhhhh)
                      | ModifierStickerOverlay::bitForType (
                            ModifierType::BufferGranularMomentary);
                    const auto transient = transientStickerMasks[(size_t)i]
                                         | (activeStickerMasks[(size_t)i]
                                            & momentaryEffectBits);
                    const auto active = activeStickerMasks[(size_t)i]
                                      | transientStickerMasks[(size_t)i];
                    const auto& animation =
                        ThemeEngine::getInstance().getAnimationConfig();
                    ModifierStickerOverlay::draw(
                        g, inner, active, transient,
                        ControlSurfacePalette::fromTheme(palette),
                        stickerGlyphPhase, ! animation.enabled);
                }

                // ── File drag hover ──
                if (isFileDragActive && hoveredPadIndex >= 0)
                {
                    const int count = juce::jmax(1, dragAudioFileCount);
                    const int previewEnd = juce::jmin(numPads, hoveredPadIndex + count);
                    const bool inPreviewRange = (i >= hoveredPadIndex && i < previewEnd);

                    if (inPreviewRange)
                    {
                        const bool isHovered = (hoveredPadIndex == i);
                        g.setColour(palette.accent1.withAlpha(isHovered ? 0.10f : 0.06f));
                        g.fillRoundedRectangle(r.expanded(2.f), cr);

                        // Dashed accent border for hover
                        {
                            juce::Path hoverPath;
                            hoverPath.addRoundedRectangle(r.expanded(2.f), cr);
                            const float dashLens[] = { 5.0f, 3.0f };
                            juce::PathStrokeType stroke(isHovered ? 2.0f : 1.2f);
                            stroke.createDashedStroke(hoverPath, hoverPath, dashLens, 2);
                            g.setColour(palette.accent1.withAlpha(isHovered ? 0.85f : 0.45f));
                            g.strokePath(hoverPath, stroke);
                        }

                        if (isHovered)
                        {
                            auto hintArea = r.reduced(10.f).toNearestInt();
                            hintArea.removeFromBottom(18);
                            g.setColour(palette.textPrimary.withAlpha(0.9f));
                            g.setFont(ThemeFonts::getInstance().bodyFont(15.0f));
                            const auto hint = (count > 1) ? ("Drop to load " + juce::String(count))
                                                         : juce::String("Drop to load");
                            g.drawFittedText(hint, hintArea, juce::Justification::centred, 1);
                        }
                    }
                }
            }
        }
    }

    bool isFileDragActive { false };
    int hoveredPadIndex { -1 };
    int dragAudioFileCount { 0 };

    static int countAudioFiles(const juce::StringArray& files)
    {
        int count = 0;
        for (const auto& f : files)
        {
            const auto ext = juce::File(f).getFileExtension().toLowerCase();
            if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".mp3")
                ++count;
        }
        return count;
    }

    void timerCallback() override
    {
        constexpr double dt = LoopBreakerConfig::uiRefreshIntervalSeconds;
        bool any = false;
        bool anyStickerVisible = false;
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < numPads; ++i)
        {
            if (flashCounters[(size_t)i] > 0) { --flashCounters[(size_t)i]; any = true; }
            if (glowAnimators[(size_t)i].isRunning())
            {
                glowAnimators[(size_t)i].tick (dt);
                any = true;
            }

            for (int typeIndex = 0;
                 typeIndex < static_cast<int>(ModifierType::Unknown);
                 ++typeIndex)
            {
                auto& expiry = transientStickerExpiryMs[(size_t)i][(size_t)typeIndex];
                if (expiry <= 0.0 || nowMs < expiry)
                    continue;

                transientStickerMasks[(size_t)i]
                    &= ~ModifierStickerOverlay::bitForType(
                        static_cast<ModifierType>(typeIndex));
                expiry = 0.0;
                any = true;
            }

            anyStickerVisible = anyStickerVisible
                             || activeStickerMasks[(size_t)i] != 0
                             || transientStickerMasks[(size_t)i] != 0;
        }

        const auto& stickerAnimation =
            ThemeEngine::getInstance().getAnimationConfig();
        if (anyStickerVisible && stickerAnimation.enabled)
        {
            // Match the main glyph display's approximately two-second cycle.
            stickerGlyphPhase += static_cast<float>(
                dt * 0.5 * static_cast<double>(stickerAnimation.animationSpeed));
            if (stickerGlyphPhase >= 1.0f)
                stickerGlyphPhase -= 1.0f;
            any = true;
        }

        // Advance marching-ants offset for MIDI learn borders
        bool anyLearn = false;
        for (auto b : midiLearnActive) if (b) { anyLearn = true; break; }
        if (anyLearn)
        {
            const auto& anim = ThemeEngine::getInstance().getAnimationConfig();
            if (anim.enabled)
            {
                midiLearnDashOffset += static_cast<float> (9.0 * dt)
                                     * anim.animationSpeed;
                if (midiLearnDashOffset > 10.0f) midiLearnDashOffset -= 10.0f;
                any = true;
            }
        }

        if (any)
            repaint();
    }

    // Listen for thumbnail content changes (async load complete) and repaint
    void changeListenerCallback(juce::ChangeBroadcaster* source) override
    {
        for (int i = 0; i < thumbnails.size(); ++i)
        {
            if (source == thumbnails[i])
            {
                repaint();
                break;
            }
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Check which pad button was clicked
        auto* clickedComponent = e.eventComponent;
        int clickedPadIndex = -1;
        
        for (int i = 0; i < numPads; ++i)
        {
            if (clickedComponent == padButtons[i])
            {
                clickedPadIndex = i;
                break;
            }
        }
        
        if (clickedPadIndex < 0)
            return;

        // Right-click: show context menu
        if (e.mods.isPopupMenu())
        {
            showPadContextMenu(clickedPadIndex);
            return;
        }
        
        // Shift+click to enter MIDI learn mode (replaces existing assignment)
        if (e.mods.isShiftDown() && !e.mods.isCommandDown())
        {
            if (onMidiLearnRequest)
            {
                onMidiLearnRequest(clickedPadIndex);
                e.eventComponent->setMouseCursor(juce::MouseCursor::WaitCursor);
            }
        }
        // Shift+Cmd+click to clear sample from pad
        else if (e.mods.isShiftDown() && e.mods.isCommandDown())
        {
            if (onClearSample)
                onClearSample(clickedPadIndex);
        }
        // Cmd+click (or Alt+click) to clear MIDI assignment
        else if (e.mods.isCommandDown() || e.mods.isAltDown())
        {
            if (onClearMidiNote)
                onClearMidiNote(clickedPadIndex);
        }
    }

    void showPadContextMenu(int padIndex)
    {
        juce::PopupMenu menu;

        const bool hasSample = padIndex < padFileNames.size()
                               && padFileNames[padIndex].isNotEmpty();
        const int  midiNote  = (padIndex < (int) midiNotes.size())
                               ? midiNotes[(size_t) padIndex] : -1;

        // 1. Load sample
        menu.addItem(1, "Load Sample...");

        // 2. Remove sample (only if one is loaded)
       #if JUCE_MAC
        menu.addItem(2, "Remove Sample    [Shift+Cmd+Click]", hasSample);
       #else
        menu.addItem(2, "Remove Sample    [Shift+Ctrl+Click]", hasSample);
       #endif

        menu.addSeparator();

        // 3. MIDI learn
        juce::String learnLabel = "MIDI Learn";
        learnLabel += "    [Shift+Click]";
        menu.addItem(3, learnLabel);

        // 4. Clear MIDI note
        juce::String clearMidiLabel = "Clear MIDI Note";
       #if JUCE_MAC
        clearMidiLabel += "    [Cmd+Click]";
       #else
        clearMidiLabel += "    [Alt+Click]";
       #endif
        const bool hasMidi = (midiNote >= 0);
        menu.addItem(4, clearMidiLabel, hasMidi);

        // Show current MIDI note info (disabled item)
        if (hasMidi)
        {
            menu.addSeparator();
            menu.addItem(0, "MIDI Note: " + juce::String(midiNote), false);
        }

        menu.showMenuAsync(juce::PopupMenu::Options()
                               .withMousePosition(),
            [this, padIndex](int result)
            {
                switch (result)
                {
                    case 1: // Load sample
                        if (onLoadSampleRequest)
                            onLoadSampleRequest(padIndex);
                        break;
                    case 2: // Remove sample
                        if (onClearSample)
                            onClearSample(padIndex);
                        break;
                    case 3: // MIDI learn
                        if (onMidiLearnRequest)
                            onMidiLearnRequest(padIndex);
                        break;
                    case 4: // Clear MIDI note
                        if (onClearMidiNote)
                            onClearMidiNote(padIndex);
                        break;
                    default:
                        break;
                }
            });
    }
};
