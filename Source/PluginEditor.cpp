#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "UpcomingModifierDisplay.h"
#include "PadGridComponent.h"
#include "ModifierHistoryPanel.h"
#include "ModifierSelectionPanel.h"
#include "FxStatusPanel.h"
#include "Theme.h"

namespace
{
class HipLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    HipLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, Theme::bg());
        setColour(juce::ComboBox::backgroundColourId, Theme::panel());
        setColour(juce::ComboBox::outlineColourId, Theme::borderStrong());
        setColour(juce::ComboBox::textColourId, Theme::text());
        setColour(juce::ComboBox::arrowColourId, Theme::textSubtle());

        setColour(juce::TextButton::buttonColourId, Theme::panelAlt());
        setColour(juce::TextButton::buttonOnColourId, Theme::panelAlt());
        setColour(juce::TextButton::textColourOffId, Theme::text());
        setColour(juce::TextButton::textColourOnId, Theme::text());

        setColour(juce::ToggleButton::textColourId, Theme::text());
        setColour(juce::ToggleButton::tickColourId, Theme::accent());
        setColour(juce::ToggleButton::tickDisabledColourId, Theme::borderStrong());

        setColour(juce::ScrollBar::thumbColourId, Theme::borderStrong());
        setColour(juce::PopupMenu::backgroundColourId, Theme::panel());
        setColour(juce::PopupMenu::highlightedBackgroundColourId, Theme::accent().withAlpha(0.12f));
        setColour(juce::PopupMenu::textColourId, Theme::text());
        setColour(juce::PopupMenu::highlightedTextColourId, Theme::text());
    }
};

class PluginEditorContent final : public juce::Component,
                                 public ModifierSchedulerListener,
                                 private juce::Timer
{
public:
    explicit PluginEditorContent (BufferTestAudioProcessor& p)
        : processor(p),
          app(p.getAppState()),
          fxStatusPanel(app)
    {
        setLookAndFeel(&hipLnf);

        addAndMakeVisible(modifierDisplay);
        addAndMakeVisible(padGrid);
        addAndMakeVisible(modifierHistory);

        modifierSelectionViewport.setViewedComponent(&modifierSelectionPanel, false);
        addAndMakeVisible(modifierSelectionViewport);
        addAndMakeVisible(fxStatusPanel);

        addAndMakeVisible(modifiersToggle);
        modifiersToggle.setToggleState(app.settings.modifiersEnabled, juce::dontSendNotification);
        modifiersToggle.onClick = [this]{ modifiersToggleChanged(); };

        addAndMakeVisible(implementedOnlyToggle);
        implementedOnlyToggle.setToggleState(true, juce::dontSendNotification);
        implementedOnlyToggle.onClick = [this]{ implementedOnlyToggled(); };

        // Parts count selector
        addAndMakeVisible(partsCountBox);
        partsCountBox.addItem("1 part", 1);
        partsCountBox.addItem("2 parts", 2);
        partsCountBox.addItem("3 parts", 3);
        partsCountBox.addItem("4 parts", 4);
        {
            int initialParts = app.settings.parts.getNumParts();
            if (initialParts < 1 || initialParts > 4) initialParts = 1;
            partsCountBox.setSelectedId(initialParts, juce::dontSendNotification);
        }
        partsCountBox.onChange = [this]{ partsCountChanged(); };

        addAndMakeVisible(statusLabel);
        statusLabel.setJustificationType(juce::Justification::centredLeft);
        statusLabel.setColour(juce::Label::textColourId, Theme::textSubtle());

        addAndMakeVisible(hostTransportLabel);
        hostTransportLabel.setJustificationType(juce::Justification::centredRight);
        hostTransportLabel.setColour(juce::Label::textColourId, Theme::textSubtle());

        padGrid.setAudioFormatManager(&processor.getFormatManager());
        attachPadCallbacks();

        padGrid.setFilesDroppedOnPadCallback([this](int startPadIndex, const juce::StringArray& files)
        {
            loadDroppedFiles(startPadIndex, files);
        });

        // MIDI learn callback - Shift+click on a pad to assign/reassign MIDI note
        padGrid.onMidiLearnRequest = [this](int padIndex)
        {
            processor.setMidiLearnMode(true, padIndex);
            padGrid.setMidiLearnForPad(padIndex, true);
        };

        // Initialize pad MIDI notes from settings
        for (int i = 0; i < 8; ++i)
            padGrid.setMidiNoteForPad(i, app.settings.midiNoteMap[i]);

        modifierSelectionPanel.setForceSelectionCallback([this](ModifierType type){
            app.scheduler.forceUpcomingModifier(type);
        });
        modifierSelectionPanel.setForceVariantCallback([this](ModifierType type, const juce::String& variant){
            app.scheduler.forceUpcomingVariant(type, variant);
        });

        app.scheduler.addListener(this);

        app.scheduler.setQuantizationEnabled(app.settings.quantizeEnabled);
        app.scheduler.setQuantizationSubdivision(app.settings.quantizeSubdivision);

        if (app.settings.padFilePaths.size() > 0)
            padGrid.setPadFilePaths(app.settings.padFilePaths);

        refreshStatus();
        refreshHostTransportReadout();
        startTimerHz(20); // 50ms UI refresh - lower overhead
    }

    ~PluginEditorContent() override
    {
        stopTimer();
        app.scheduler.removeListener(this);
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Theme::bg());

        g.setColour(Theme::border());
        g.drawRect(getLocalBounds(), 1);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto topBar = area.removeFromTop(60);
        modifierDisplay.setBounds(topBar.removeFromLeft(topBar.getWidth() * 0.5f).reduced(4));

        auto controlBar = topBar;
        modifiersToggle.setBounds(controlBar.removeFromLeft(120).reduced(2));
        partsCountBox.setBounds(controlBar.removeFromLeft(120).reduced(2));

        auto rightRegion = controlBar;
        implementedOnlyToggle.setBounds(rightRegion.removeFromRight(150).reduced(2));

        auto row2 = area.removeFromTop(28).reduced(2);
        hostTransportLabel.setBounds(row2.removeFromRight(240).reduced(2));
        statusLabel.setBounds(row2.reduced(2));

        area.removeFromTop(6);
        auto gridHeight = 220;
        padGrid.setBounds(area.removeFromTop(gridHeight));

        area.removeFromTop(6);
        auto bottomArea = area;
        auto rightPanel = bottomArea.removeFromRight(bottomArea.getWidth() / 3).reduced(4);
        auto rightTop = rightPanel.removeFromTop(int(rightPanel.getHeight() * 0.65f));

        int toggleCount = modifierSelectionPanel.getNumChildComponents();
        int desiredHeight = juce::jmax(rightTop.getHeight() - 60, toggleCount * 26 + 20);
        modifierSelectionPanel.setSize(rightTop.getWidth() - 8, desiredHeight);
        modifierSelectionViewport.setBounds(rightTop);

        fxStatusPanel.setBounds(rightPanel);
        modifierHistory.setBounds(bottomArea.reduced(2));
    }

    // ModifierSchedulerListener
    void upcomingModifierChanged(const ModifierDescriptor& desc) override
    {
        auto descCopy = desc;
        juce::MessageManager::callAsync([this, descCopy]
        {
            modifierDisplay.setUpcoming(descCopy);
        });
    }

    void modifierTriggered(const ModifierDescriptor& desc, const juce::Array<int>& targets) override
    {
        juce::MessageManager::callAsync([this, desc, targets]
        {
            modifierHistory.addEntry(desc, targets);
            padGrid.flashPads(targets);
            if (! targets.isEmpty())
                padGrid.clearSelections();
            refreshStatus();
        });
    }

private:
    BufferTestAudioProcessor& processor;
    AppState& app;

    HipLookAndFeel hipLnf;

    UpcomingModifierDisplay modifierDisplay;
    ModifierHistoryPanel modifierHistory;
    PadGridComponent padGrid;

    ModifierSelectionPanel modifierSelectionPanel;
    juce::Viewport modifierSelectionViewport;
    FxStatusPanel fxStatusPanel;

    juce::ToggleButton modifiersToggle { "Modifiers" };

    juce::ToggleButton implementedOnlyToggle { "Implemented Only" };

    juce::ComboBox partsCountBox;
    juce::Label statusLabel { {}, "Status: Idle" };
    juce::Label hostTransportLabel { {}, "Host: Unknown" };

    std::unique_ptr<juce::FileChooser> fileChooser;

    void refreshHostTransportReadout()
    {
        const auto state = processor.getLastHostTransportState();
        const auto source = processor.getLastHostTransportSource();

        juce::String s;
        s << "Host: ";

        switch (state)
        {
            case BufferTestAudioProcessor::HostTransportState::Playing: s << "Playing"; break;
            case BufferTestAudioProcessor::HostTransportState::Stopped: s << "Stopped"; break;
            default: s << "Unknown"; break;
        }

        s << " (";
        switch (source)
        {
            case BufferTestAudioProcessor::HostTransportSource::Reported: s << "reported"; break;
            case BufferTestAudioProcessor::HostTransportSource::Inferred: s << "inferred"; break;
            default: s << "unknown"; break;
        }
        s << ")";

        if (! processor.isPlaybackEnabled())
            s << " | gated";

        hostTransportLabel.setText(s, juce::dontSendNotification);
    }

    void ensurePadFilePathsSized()
    {
        while (app.settings.padFilePaths.size() < AudioBufferManager::MAX_BUFFERS)
            app.settings.padFilePaths.add({});
    }

    void loadFileIntoPad(int padIndex, const juce::File& file)
    {
        if (! juce::isPositiveAndBelow(padIndex, AudioBufferManager::MAX_BUFFERS))
            return;

        if (! file.existsAsFile())
            return;

        const bool scheduled = app.bufferManager.requestLoadAudioFile(padIndex, file);
        if (! scheduled)
            return;

        ensurePadFilePathsSized();
        app.settings.padFilePaths.set(padIndex, file.getFullPathName());
        padGrid.setPadFilePath(padIndex, file.getFullPathName());
    }

    void loadDroppedFiles(int startPadIndex, const juce::StringArray& files)
    {
        if (! juce::isPositiveAndBelow(startPadIndex, AudioBufferManager::MAX_BUFFERS))
            return;

        int padIndex = startPadIndex;
        for (const auto& path : files)
        {
            if (padIndex >= AudioBufferManager::MAX_BUFFERS)
                break;

            loadFileIntoPad(padIndex, juce::File(path));
            ++padIndex;
        }
    }

    void timerCallback() override
    {
        // Lightweight UI refresh only; timing is driven by the audio thread in the processor.
        refreshStatus();
        refreshHostTransportReadout();
        padGrid.setPlayingStates(app.bufferManager.getPlayingBufferIndices());

        // Poll for MIDI pad toggle requests (from audio thread)
        for (int i = 0; i < 8; ++i)
        {
            if (processor.checkAndClearMidiToggle(i))
            {
                padGrid.togglePadSelection(i);
            }
        }

        // Poll for MIDI learn completion
        if (processor.isMidiLearnEnabled())
        {
            const int learnedNote = processor.checkAndClearLearnedNote();
            if (learnedNote >= 0)
            {
                const int padIndex = processor.getMidiLearnPadIndex();
                if (padIndex >= 0 && padIndex < 8)
                {
                    app.settings.midiNoteMap[padIndex] = learnedNote;
                    padGrid.setMidiNoteForPad(padIndex, learnedNote);
                    padGrid.setMidiLearnForPad(padIndex, false);
                    processor.setMidiLearnMode(false, -1);
                }
            }
        }

        // Update modifier countdown/progress (driven by scheduler host timeline when available).
        if (app.scheduler.isRunning())
        {
            modifierDisplay.setCountdown(app.scheduler.getSecondsUntilNextTrigger(),
                                         app.scheduler.getBarsUntilNextTrigger(),
                                         app.scheduler.getProgressToNextTrigger());
        }
        else
        {
            modifierDisplay.setCountdown(0.0, 0.0, 0.0);
        }

        // Show paused styling when modifiers are disabled.
        modifierDisplay.setSuppressed((! app.settings.modifiersEnabled) || app.scheduler.isSuppressed());

        // Update playhead positions for waveform display (only when not playing).
        for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
        {
            if (auto* b = app.bufferManager.getBuffer(i); b && b->hasAudioLoaded())
            {
                padGrid.setTotalSamplesForPad(i, (double) b->getDurationInSamples());
                padGrid.setPlayheadForPad(i, (double) b->getPlayheadPositionInSamples());
                padGrid.setLoopWindowForPad(i,
                                           b->isLoopWindowEnabled(),
                                           (double) b->getLoopStartSamples(),
                                           (double) b->getLoopEndSamples());
            }
        }
        
        // Repaint for smooth playhead animation
        padGrid.repaint();
    }

    void attachPadCallbacks()
    {
        padGrid.setSelectionChangedCallback([this]{ updatePadSelectionTargets(); });
        updatePadSelectionTargets();
    }

    void updatePadSelectionTargets()
    {
        app.scheduler.setUserSelectedBuffers(padGrid.getSelectedPadIndices());
        refreshStatus();
    }

    void modifiersToggleChanged()
    {
        const bool enabled = modifiersToggle.getToggleState();
        app.settings.modifiersEnabled = enabled;
        if (enabled)
        {
            if (! app.scheduler.isRunning())
                app.scheduler.start();
        }
        else
        {
            if (app.scheduler.isRunning())
                app.scheduler.stop();
        }
        refreshStatus();
    }

    void implementedOnlyToggled()
    {
        app.scheduler.setRestrictToImplemented(implementedOnlyToggle.getToggleState());
    }

    void partsCountChanged()
    {
        const int n = juce::jlimit(1, 4, partsCountBox.getSelectedId());
        app.settings.parts.numParts = n;
        // Re-apply active part to recompute loop windows immediately.
        app.setActivePart(app.getActivePart());
    }



    void refreshStatus()
    {
        juce::String s;
        s << (app.scheduler.isRunning() ? "Running" : "Stopped");
        s << " | Selected: " << padGrid.getSelectedPadIndices().size();
        static const char* partNames[] = { "A", "B", "C", "D" };
        const int numParts = app.settings.parts.getNumParts();
        const int active = juce::jlimit(0, 3, app.getActivePart());
        s << " | Parts: " << numParts << " | Active: " << partNames[active];
        statusLabel.setText("Status: " + s, juce::dontSendNotification);
    }
};
} // namespace

BufferTestAudioProcessorEditor::BufferTestAudioProcessorEditor (BufferTestAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    content = std::make_unique<PluginEditorContent>(processor);
    addAndMakeVisible(*content);

    setSize (920, 600);
}

BufferTestAudioProcessorEditor::~BufferTestAudioProcessorEditor() = default;

void BufferTestAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll(Theme::bg());
}

void BufferTestAudioProcessorEditor::resized()
{
    if (content)
        content->setBounds(getLocalBounds());
}
