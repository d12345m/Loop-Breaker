/*
 ============================================================================== 
   PadGridComponent.h
   --------------------------------------------------------------------------
   Simple 2x4 grid of pad toggle buttons representing the 8 audio buffers.
   Provides selection state for scheduling modifiers (user targeted buffers).
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ThemeEngine.h"
#include "ThemeFonts.h"
#include "Animator.h"

// Simple 2x4 pad grid showing selectable pads and (new) filename indicators.
class PadGridComponent : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer,
                         private juce::ChangeListener
{
public:
    PadGridComponent()
    {
        // Ensure the local AudioFormatManager knows basic formats so thumbnails can read files
        if (formatManager.getNumKnownFormats() == 0)
            formatManager.registerBasicFormats();
        for (int i = 0; i < numPads; ++i)
        {
            auto* btn = padButtons.add(new juce::ToggleButton(""));
            btn->setLookAndFeel(&invisibleToggleLF);
            btn->setClickingTogglesState(true);
            btn->onClick = [this, btn]
            { 
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

        startTimerHz(20); // for transient flash highlight decay - lower overhead
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
        const int rows = 2;
        const int cols = 4;
        const int padW = area.getWidth() / cols;
        const int padH = area.getHeight() / rows;
        const int labelHeight = 20;

        for (int r = 0; r < rows; ++r)
        {
            for (int c = 0; c < cols; ++c)
            {
                // Bottom row = pads 1-4 (idx 0-3), top row = pads 5-8 (idx 4-7)
                int idx = (rows - 1 - r) * cols + c;
                if (idx < padButtons.size())
                {
                    juce::Rectangle<int> padRect(area.getX() + c * padW, area.getY() + r * padH, padW - 4, padH - 4);
                    auto labelRect = padRect.removeFromBottom(labelHeight);
                    padFileLabels[idx]->setBounds(labelRect.reduced(2, 0));
                    padButtons[idx]->setBounds(padRect.reduced(4));
                }
            }
        }
    }

private:
    static constexpr int numPads = 8;
    // Look-and-feel that suppresses the default checkbox visuals of ToggleButton
    class InvisibleToggleLookAndFeel : public juce::LookAndFeel_V4 {
    public:
        void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool, bool) override {}
    };

    juce::OwnedArray<juce::ToggleButton> padButtons;
    InvisibleToggleLookAndFeel invisibleToggleLF;
    juce::OwnedArray<juce::Label> padFileLabels;
    juce::StringArray padFileNames { "", "", "", "", "", "", "", "" };
    std::array<int, numPads> flashCounters { {0,0,0,0,0,0,0,0} };
    std::array<float, numPads> glowAlpha { {0,0,0,0,0,0,0,0} };
    std::array<Animator, numPads> glowAnimators;
    float midiLearnDashOffset = 0.0f;  // marching ants
    std::array<bool, numPads> playingStates { {false,false,false,false,false,false,false,false} };
    // Bottom row left->right = 36-39 (pads 1-4, idx 0-3), top row left->right = 40-43 (pads 5-8, idx 4-7)
    std::array<int, numPads> midiNotes { {36, 37, 38, 39, 40, 41, 42, 43} };
    std::array<bool, numPads> midiLearnActive { {false,false,false,false,false,false,false,false} };

    // Waveform state
    juce::AudioFormatManager formatManager;
    juce::AudioFormatManager* formatManagerPtr { nullptr };
    juce::AudioThumbnailCache thumbnailCache { 64 };
    juce::OwnedArray<juce::AudioThumbnail> thumbnails;
    std::array<double, numPads> playheadSamples { {0,0,0,0,0,0,0,0} };
    std::array<bool,   numPads> loopEnabled     { {false,false,false,false,false,false,false,false} };
    std::array<double, numPads> loopStartSamples{ {0,0,0,0,0,0,0,0} };
    std::array<double, numPads> loopEndSamples  { {0,0,0,0,0,0,0,0} };
    // Total file length in samples for each pad (used to map loop/playhead across full waveform)
    std::array<double, numPads> totalFileSamples { {0,0,0,0,0,0,0,0} };

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
        std::array<bool, numPads> newStates { {false,false,false,false,false,false,false,false} };
        for (auto idx : playingIndices)
            if (juce::isPositiveAndBelow(idx, numPads))
                newStates[(size_t)idx] = true;

        if (newStates != playingStates)
        {
            playingStates = newStates;
            repaint();
        }
    }

private:
    static constexpr int flashDurationTicks = 10; // ~333ms at 30Hz

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
            name = name.substring(0, maxChars - 1) + "…";
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

    void paintOverChildren(juce::Graphics& g) override
    {
        const auto& palette = ThemeEngine::getInstance().getCurrentPalette();
        const float cr = palette.borderRadius;

        // Draw waveform, overlays, then state/flash
        for (int i = 0; i < numPads; ++i)
        {
            if (auto* btn = padButtons[i])
            {
                auto r = btn->getBounds().toFloat();

                // ── Pad background ──
                const bool hasWave = (thumbnails[i] != nullptr && thumbnails[i]->getTotalLength() > 0.0);
                g.setColour(hasWave ? palette.padLoaded : palette.padEmpty);
                g.fillRoundedRectangle(r, cr);

                // ── Inner shadow (dark vignette around edges) ──
                {
                    auto insetRect = r.reduced(1.0f);
                    juce::ColourGradient vignette(juce::Colours::transparentBlack, insetRect.getCentreX(), insetRect.getCentreY(),
                                                   juce::Colours::black.withAlpha(0.25f), insetRect.getX(), insetRect.getY(), true);
                    g.setGradientFill(vignette);
                    g.fillRoundedRectangle(insetRect, cr);
                }

                // ── Border ──
                g.setColour(palette.border);
                g.drawRoundedRectangle(r, cr, 1.0f);

                // Waveform rendering region
                auto inner = r.reduced(6.f, 6.f);

                if (hasWave)
                {
                    auto* thumb = thumbnails[i];

                    // Oscilloscope grid lines (4 horizontal divisions)
                    g.setColour(palette.border.withAlpha(0.3f));
                    for (int line = 1; line <= 3; ++line)
                    {
                        float ly = inner.getY() + inner.getHeight() * ((float)line / 4.0f);
                        g.drawHorizontalLine((int)ly, inner.getX(), inner.getRight());
                    }

                    // Waveform with vertical gradient
                    {
                        juce::ColourGradient waveGrad(palette.waveformFill.withAlpha(0.6f), inner.getX(), inner.getY(),
                                                       palette.waveformFill.darker(0.3f).withAlpha(0.6f), inner.getX(), inner.getBottom(), false);
                        g.setGradientFill(waveGrad);
                        thumb->drawChannels(g, inner.toNearestInt(), 0.0, thumb->getTotalLength(), 1.0f);
                    }

                    // Loop overlay with diagonal hatching
                    if (loopEnabled[(size_t)i])
                    {
                        const double denom = juce::jmax(1.0, totalFileSamples[(size_t)i]);
                        const double startProp = juce::jlimit(0.0, 1.0, loopStartSamples[(size_t)i] / denom);
                        const double endProp   = juce::jlimit(0.0, 1.0, loopEndSamples[(size_t)i]   / denom);
                        const float x1 = inner.getX() + (float)(inner.getWidth() * startProp);
                        const float x2 = inner.getX() + (float)(inner.getWidth() * endProp);
                        juce::Rectangle<float> loopRect(x1, inner.getY(), juce::jmax(1.0f, x2 - x1), inner.getHeight());

                        // Semi-transparent fill
                        g.setColour(palette.warn.withAlpha(0.12f));
                        g.fillRect(loopRect);

                        // Diagonal hatching (45° lines, 4px apart)
                        {
                            g.saveState();
                            g.reduceClipRegion(loopRect.toNearestInt());
                            g.setColour(palette.warn.withAlpha(0.10f));
                            const float step = 4.0f;
                            for (float d = -loopRect.getHeight(); d < loopRect.getWidth() + loopRect.getHeight(); d += step)
                                g.drawLine(loopRect.getX() + d, loopRect.getBottom(),
                                           loopRect.getX() + d + loopRect.getHeight(), loopRect.getY(), 0.5f);
                            g.restoreState();
                        }

                        // Loop boundary lines
                        g.setColour(palette.warn.withAlpha(0.85f));
                        g.drawLine(x1, inner.getY(), x1, inner.getBottom(), 1.5f);
                        g.drawLine(x2, inner.getY(), x2, inner.getBottom(), 1.5f);
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
                    // ── Empty pad: dashed border + "+" icon ──
                    {
                        juce::Path dashRect;
                        dashRect.addRoundedRectangle(inner, cr * 0.5f);
                        const float dashLengths[] = { 4.0f, 4.0f };
                        juce::PathStrokeType stroke(1.0f);
                        stroke.createDashedStroke(dashRect, dashRect, dashLengths, 2);
                        g.setColour(palette.border.withAlpha(0.5f));
                        g.strokePath(dashRect, stroke);
                    }

                    // "+" icon centered
                    g.setColour(palette.textSecondary.withAlpha(0.5f));
                    const float plusSize = 14.0f;
                    const float cx = inner.getCentreX();
                    const float cy = inner.getCentreY();
                    g.drawLine(cx - plusSize * 0.5f, cy, cx + plusSize * 0.5f, cy, 1.5f);
                    g.drawLine(cx, cy - plusSize * 0.5f, cx, cy + plusSize * 0.5f, 1.5f);
                }

                // ── Pad number badge (top-left) ──
                {
                    auto badgeRect = juce::Rectangle<float>(r.getX() + 4.0f, r.getY() + 4.0f, 20.0f, 18.0f);
                    g.setColour(palette.accent1.withAlpha(0.30f));
                    g.fillRoundedRectangle(badgeRect, 3.0f);
                    g.setColour(palette.textPrimary);
                    g.setFont(ThemeFonts::getInstance().monoBoldFont(12.0f));
                    g.drawText(juce::String(i + 1), badgeRect, juce::Justification::centred);
                }

                // ── MIDI note badge (top-right) ──
                if (!midiLearnActive[(size_t)i] && midiNotes[(size_t)i] >= 0)
                {
                    auto noteRect = juce::Rectangle<float>(r.getRight() - 40.0f, r.getY() + 4.0f, 36.0f, 18.0f);
                    g.setColour(palette.panelAlt);
                    g.fillRoundedRectangle(noteRect, 3.0f);
                    g.setColour(palette.textSecondary);
                    g.setFont(ThemeFonts::getInstance().monoFont(12.0f));
                    g.drawText(juce::String(midiNotes[(size_t)i]), noteRect, juce::Justification::centred);
                }

                // ── Selection overlay ──
                if (padButtons[i]->getToggleState())
                {
                    g.setColour(palette.padSelected);
                    g.fillRoundedRectangle(r, cr);
                    // Glow border
                    g.setColour(palette.accent1.withAlpha(palette.glowIntensity));
                    g.drawRoundedRectangle(r.expanded(1.5f), cr, 2.0f);
                    // Outer glow (concentric transparent rects)
                    g.setColour(palette.accent1.withAlpha(0.08f));
                    g.drawRoundedRectangle(r.expanded(3.5f), cr + 1.0f, 1.5f);
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
                    g.setColour(palette.padPlaying.withAlpha(0.85f));
                    g.drawRoundedRectangle(r.expanded(2.f), cr, 2.0f);
                }

                // ── Flash overlay + animated glow pulse (modifier triggered) ──
                if (flashCounters[(size_t)i] > 0 || glowAlpha[(size_t)i] > 0.001f)
                {
                    // Solid tint overlay (always, even with animation off)
                    float flashAlpha = (float)flashCounters[(size_t)i] / (float)flashDurationTicks * 0.25f;
                    if (flashAlpha > 0.0f)
                    {
                        g.setColour(palette.accent2.withAlpha(flashAlpha));
                        g.fillRoundedRectangle(r.expanded(2.f), cr);
                    }

                    // Animated radial glow (only when glow animator is active)
                    if (glowAlpha[(size_t)i] > 0.001f)
                    {
                        juce::ColourGradient glow(palette.accent2.withAlpha(glowAlpha[(size_t)i]),
                                                   r.getCentreX(), r.getCentreY(),
                                                   palette.accent2.withAlpha(0.0f),
                                                   r.getX() - 4.0f, r.getY() - 4.0f, true);
                        g.setGradientFill(glow);
                        g.fillRoundedRectangle(r.expanded(4.f), cr + 1.0f);
                    }
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
        const double dt = 1.0 / 20.0;  // 50 ms per tick
        bool any = false;
        for (int i = 0; i < numPads; ++i)
        {
            if (flashCounters[(size_t)i] > 0) { --flashCounters[(size_t)i]; any = true; }
            if (glowAnimators[(size_t)i].isRunning())
            {
                glowAnimators[(size_t)i].tick (dt);
                any = true;
            }
        }

        // Advance marching-ants offset for MIDI learn borders
        bool anyLearn = false;
        for (auto b : midiLearnActive) if (b) { anyLearn = true; break; }
        if (anyLearn)
        {
            const auto& anim = ThemeEngine::getInstance().getAnimationConfig();
            if (anim.enabled)
            {
                midiLearnDashOffset += 0.6f * anim.animationSpeed;
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
