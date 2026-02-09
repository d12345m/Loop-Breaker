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
            btn->onClick = [this]{ if (selectionChanged) selectionChanged(); };
            addAndMakeVisible(btn);

            auto* label = padFileLabels.add(new juce::Label());
            label->setJustificationType(juce::Justification::centred);
            label->setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
            label->setInterceptsMouseClicks(false, false);
            label->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
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

        startTimerHz(30); // for transient flash highlight decay
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
        repaint();

        filesDroppedOnPad(padIndex, files);
    }

    void fileDragEnter (const juce::StringArray& files, int x, int y) override
    {
        if (! isInterestedInFileDrag(files))
            return;

        isFileDragActive = true;
        hoveredPadIndex = getPadIndexAt({ x, y });
        repaint();
    }

    void fileDragMove (const juce::StringArray& files, int x, int y) override
    {
        if (! isInterestedInFileDrag(files))
            return;

        isFileDragActive = true;

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
        auto name = padFileNames[padIndex];
        const bool isMissing = name.equalsIgnoreCase("[missing]");
        if (name.isEmpty())
        {
            padFileLabels[padIndex]->setText("", juce::dontSendNotification);
            return;
        }
        // Truncate long names for compact display
        const int maxChars = 10;
        if (name.length() > maxChars)
            name = name.substring(0, maxChars - 1) + "…";
        // Colour-code missing pads to draw attention
        padFileLabels[padIndex]->setColour(juce::Label::textColourId, isMissing ? juce::Colours::orangered : juce::Colours::lightgrey);
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
                g.setColour(juce::Colours::black.withAlpha(0.85f));
                g.fillRoundedRectangle(r, 6.f);

                // Waveform rendering region
                auto inner = r.reduced(6.f, 6.f);
                if (auto* thumb = thumbnails[i])
                {
                    const bool hasWave = thumb->getTotalLength() > 0.0;
                    g.setColour(hasWave ? juce::Colours::darkgrey : juce::Colours::dimgrey);
                    g.drawRoundedRectangle(r, 6.f, 1.0f);

                    if (hasWave)
                    {
                        g.setColour(juce::Colours::lightsteelblue);
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
                            g.setColour(juce::Colours::orange.withAlpha(0.18f));
                            g.fillRect(loopRect);
                            g.setColour(juce::Colours::orange.withAlpha(0.8f));
                            g.drawLine(x1, inner.getY(), x1, inner.getBottom(), 1.5f);
                            g.drawLine(x2, inner.getY(), x2, inner.getBottom(), 1.5f);
                        }

                        // Playhead line (proportional across full file waveform)
                        const double phDenom = juce::jmax(1.0, totalFileSamples[(size_t)i]);
                        const double phProp  = juce::jlimit(0.0, 1.0, playheadSamples[(size_t)i] / phDenom);
                        const float phx = inner.getX() + (float)(inner.getWidth() * phProp);
                        g.setColour(juce::Colours::aqua);
                        g.drawLine(phx, inner.getY(), phx, inner.getBottom(), 2.0f);
                    }
                    else
                    {
                        // Empty pad visual
                        g.setColour(juce::Colours::darkred.withAlpha(0.35f));
                        g.fillRect(inner);
                        g.setColour(juce::Colours::grey);
                        g.drawText("(empty)", inner.toNearestInt(), juce::Justification::centred);
                    }
                }

                // Selection overlay (tinted) to replace checkbox visuals
                if (padButtons[i]->getToggleState())
                {
                    g.setColour(juce::Colours::cornflowerblue.withAlpha(0.20f));
                    g.fillRoundedRectangle(r, 6.f);
                    g.setColour(juce::Colours::cornflowerblue.withAlpha(0.8f));
                    g.drawRoundedRectangle(r.expanded(1.5f), 6.f, 2.0f);
                }

                // Playing state outline
                if (playingStates[(size_t)i])
                {
                    g.setColour(juce::Colours::lime.withAlpha(0.9f));
                    g.drawRoundedRectangle(r.expanded(2.f), 6.f, 2.2f);
                }

                // Flash overlay
                if (flashCounters[(size_t)i] > 0)
                {
                    g.setColour(juce::Colours::yellow.withAlpha(0.35f));
                    g.fillRoundedRectangle(r.expanded(2.f), 6.f);
                }

                // File-drag hover overlay + hint
                if (isFileDragActive && hoveredPadIndex == i)
                {
                    g.setColour(juce::Colours::cornflowerblue.withAlpha(0.18f));
                    g.fillRoundedRectangle(r.expanded(2.f), 6.f);
                    g.setColour(juce::Colours::cornflowerblue.withAlpha(0.9f));
                    g.drawRoundedRectangle(r.expanded(2.f), 6.f, 2.0f);

                    auto hintArea = r.reduced(10.f).toNearestInt();
                    hintArea.removeFromBottom(18); // keep clear of filename label region
                    g.setColour(juce::Colours::lightgrey.withAlpha(0.9f));
                    g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));
                    g.drawFittedText("Drop to load", hintArea, juce::Justification::centred, 1);
                }
            }
        }
    }

    bool isFileDragActive { false };
    int hoveredPadIndex { -1 };

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
};
