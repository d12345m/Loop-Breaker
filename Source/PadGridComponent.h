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
#include "Theme.h"

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
                // Skip selection change if Shift (MIDI learn), Cmd (clear MIDI), or Ctrl (clear sample) was held
                auto mods = juce::ModifierKeys::currentModifiers;
                if (mods.isShiftDown() || mods.isCommandDown() || mods.isAltDown() || mods.isCtrlDown())
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
            label->setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
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
                int idx = r * cols + c;
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
    std::array<bool, numPads> playingStates { {false,false,false,false,false,false,false,false} };
    // Bottom row left->right = 36-39 (pads 4-7), top row left->right = 40-43 (pads 0-3)
    std::array<int, numPads> midiNotes { {40, 41, 42, 43, 36, 37, 38, 39} };
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
        if (padIndices.isEmpty())
        {
            for (int i = 0; i < numPads; ++i) flashCounters[i] = flashDurationTicks;
        }
        else
        {
            for (auto idx : padIndices)
                if (juce::isPositiveAndBelow(idx, numPads))
                    flashCounters[(size_t)idx] = flashDurationTicks;
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
        // Draw waveform, overlays, then state/flash
        for (int i = 0; i < numPads; ++i)
        {
            if (auto* btn = padButtons[i])
            {
                auto r = btn->getBounds().toFloat();

                // Background
                g.setColour(Theme::panelAlt());
                g.fillRoundedRectangle(r, 3.f);
                g.setColour(Theme::border());
                g.drawRoundedRectangle(r, 3.f, 1.0f);

                // Waveform rendering region
                auto inner = r.reduced(6.f, 6.f);
                if (auto* thumb = thumbnails[i])
                {
                    const bool hasWave = thumb->getTotalLength() > 0.0;
                    g.setColour(hasWave ? Theme::borderStrong() : Theme::border());
                    g.drawRoundedRectangle(r, 3.f, 1.0f);

                    if (hasWave)
                    {
                        g.setColour(Theme::textSubtle().withAlpha(0.75f));
                        thumb->drawChannels(g, inner.toNearestInt(), 0.0, thumb->getTotalLength(), 1.0f);

                        // Loop overlay (proportional across full file waveform)
                        if (loopEnabled[(size_t)i])
                        {
                            const double denom = juce::jmax(1.0, totalFileSamples[(size_t)i]);
                            const double startProp = juce::jlimit(0.0, 1.0, loopStartSamples[(size_t)i] / denom);
                            const double endProp   = juce::jlimit(0.0, 1.0, loopEndSamples[(size_t)i]   / denom);
                            const float x1 = inner.getX() + (float)(inner.getWidth() * startProp);
                            const float x2 = inner.getX() + (float)(inner.getWidth() * endProp);
                            juce::Rectangle<float> loopRect(x1, inner.getY(), juce::jmax(1.0f, x2 - x1), inner.getHeight());
                            g.setColour(Theme::warn().withAlpha(0.14f));
                            g.fillRect(loopRect);
                            g.setColour(Theme::warn().withAlpha(0.85f));
                            g.drawLine(x1, inner.getY(), x1, inner.getBottom(), 1.5f);
                            g.drawLine(x2, inner.getY(), x2, inner.getBottom(), 1.5f);
                        }

                        // Playhead line (proportional across full file waveform)
                        const double phDenom = juce::jmax(1.0, totalFileSamples[(size_t)i]);
                        const double phProp  = juce::jlimit(0.0, 1.0, playheadSamples[(size_t)i] / phDenom);
                        const float phx = inner.getX() + (float)(inner.getWidth() * phProp);
                        g.setColour(Theme::accent());
                        g.drawLine(phx, inner.getY(), phx, inner.getBottom(), 2.0f);
                    }
                    else
                    {
                        // Empty pad visual
                        g.setColour(Theme::border().withAlpha(0.35f));
                        g.fillRoundedRectangle(inner, 6.0f);
                        g.setColour(Theme::textSubtle());
                        g.drawText("(empty)", inner.toNearestInt(), juce::Justification::centred);
                    }
                }

                // Selection overlay (tinted) to replace checkbox visuals
                if (padButtons[i]->getToggleState())
                {
                    g.setColour(Theme::accent().withAlpha(0.10f));
                    g.fillRoundedRectangle(r, 3.f);
                    g.setColour(Theme::accent().withAlpha(0.85f));
                    g.drawRoundedRectangle(r.expanded(1.5f), 3.f, 1.8f);
                }

                // MIDI learn indicator
                if (midiLearnActive[(size_t)i])
                {
                    g.setColour(Theme::warn());
                    g.drawRoundedRectangle(r.expanded(2.0f), 3.f, 2.5f);
                    auto learnRect = r.removeFromTop(20).reduced(4);
                    g.fillRoundedRectangle(learnRect, 2.0f);
                    g.setColour(Theme::bg());
                    g.drawText("LEARN", learnRect, juce::Justification::centred);
                }
                // MIDI note display (top-right corner)
                else if (midiNotes[(size_t)i] >= 0)
                {
                    auto noteRect = r.removeFromTop(18).removeFromRight(40).reduced(2);
                    g.setColour(Theme::panel());
                    g.fillRoundedRectangle(noteRect.toFloat(), 2.0f);
                    g.setColour(Theme::textSubtle());
                    g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
                    g.drawText(juce::String(midiNotes[(size_t)i]), noteRect, juce::Justification::centred);
                }

                // Playing state outline
                if (playingStates[(size_t)i])
                {
                    g.setColour(Theme::good().withAlpha(0.85f));
                    g.drawRoundedRectangle(r.expanded(2.f), 3.f, 2.0f);
                }

                // Flash overlay
                if (flashCounters[(size_t)i] > 0)
                {
                    g.setColour(Theme::warn().withAlpha(0.18f));
                    g.fillRoundedRectangle(r.expanded(2.f), 3.f);
                }

                // File-drag hover overlay + hint
                if (isFileDragActive && hoveredPadIndex >= 0)
                {
                    const int count = juce::jmax(1, dragAudioFileCount);
                    const int previewEnd = juce::jmin(numPads, hoveredPadIndex + count);
                    const bool inPreviewRange = (i >= hoveredPadIndex && i < previewEnd);

                    if (inPreviewRange)
                    {
                        const bool isHovered = (hoveredPadIndex == i);
                        g.setColour(Theme::accent().withAlpha(isHovered ? 0.10f : 0.06f));
                        g.fillRoundedRectangle(r.expanded(2.f), 3.f);
                        g.setColour(Theme::accent().withAlpha(isHovered ? 0.85f : 0.45f));
                        g.drawRoundedRectangle(r.expanded(2.f), 3.f, isHovered ? 2.0f : 1.2f);

                        if (isHovered)
                        {
                            auto hintArea = r.reduced(10.f).toNearestInt();
                            hintArea.removeFromBottom(18); // keep clear of filename label region
                            g.setColour(Theme::text().withAlpha(0.9f));
                            g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));
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
        bool any = false;
        for (auto& c : flashCounters)
        {
            if (c > 0) { --c; any = true; }
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
        
        // Shift+click to enter MIDI learn mode (replaces existing assignment)
        if (e.mods.isShiftDown())
        {
            if (onMidiLearnRequest)
            {
                onMidiLearnRequest(clickedPadIndex);
                e.eventComponent->setMouseCursor(juce::MouseCursor::WaitCursor);
            }
        }
        // Cmd+click (or Alt+click) to clear MIDI assignment
        else if (e.mods.isCommandDown() || e.mods.isAltDown())
        {
            if (onClearMidiNote)
                onClearMidiNote(clickedPadIndex);
        }
        // Ctrl+click to clear sample from pad
        else if (e.mods.isCtrlDown())
        {
            if (onClearSample)
                onClearSample(clickedPadIndex);
        }
    }
};
