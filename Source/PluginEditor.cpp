#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "UpcomingModifierDisplay.h"
#include "PadGridComponent.h"
#include "ModifierHistoryPanel.h"
#include "ModifierSelectionPanel.h"
#include "FxStatusPanel.h"

namespace
{
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
        addAndMakeVisible(modifierDisplay);
        addAndMakeVisible(padGrid);
        addAndMakeVisible(modifierHistory);

        modifierSelectionViewport.setViewedComponent(&modifierSelectionPanel, false);
        addAndMakeVisible(modifierSelectionViewport);
        addAndMakeVisible(fxStatusPanel);

        addAndMakeVisible(playAllButton);
        playAllButton.onClick = [this]{ app.bufferManager.playAll(); };

        addAndMakeVisible(stopAllButton);
        stopAllButton.onClick = [this]{ app.bufferManager.stopAll(); };

        addAndMakeVisible(modifiersToggle);
        modifiersToggle.setToggleState(true, juce::dontSendNotification);
        modifiersToggle.onClick = [this]{ modifiersToggleChanged(); };

        addAndMakeVisible(implementedOnlyToggle);
        implementedOnlyToggle.setToggleState(true, juce::dontSendNotification);
        implementedOnlyToggle.onClick = [this]{ implementedOnlyToggled(); };

        addAndMakeVisible(quantizeToggle);
        quantizeToggle.setToggleState(app.settings.quantizeEnabled, juce::dontSendNotification);
        quantizeToggle.onClick = [this]{ quantizeToggled(); };

        quantizeSubdivisionBox.addItem("Bar", 1);
        quantizeSubdivisionBox.addItem("1/2", 2);
        quantizeSubdivisionBox.addItem("1/4 (Beat)", 3);
        quantizeSubdivisionBox.addItem("1/8", 4);
        quantizeSubdivisionBox.addItem("1/16", 5);
        quantizeSubdivisionBox.onChange = [this]{ quantizeSubdivisionChanged(); };
        addAndMakeVisible(quantizeSubdivisionBox);

        auto subdivisionToId = [](int subdiv)->int{
            switch (subdiv) { case 1: return 1; case 2: return 2; case 4: return 3; case 8: return 4; case 16: return 5; default: return 3; }
        };
        quantizeSubdivisionBox.setSelectedId(subdivisionToId(app.settings.quantizeSubdivision), juce::dontSendNotification);

        addAndMakeVisible(padSelectForLoad);
        for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
            padSelectForLoad.addItem("Pad " + juce::String(i+1), i+1);
        padSelectForLoad.setSelectedId(1);

        addAndMakeVisible(loadFileButton);
        loadFileButton.onClick = [this]{ loadFileClicked(); };

        addAndMakeVisible(statusLabel);
        statusLabel.setJustificationType(juce::Justification::centredLeft);

        padGrid.setAudioFormatManager(&processor.getFormatManager());
        attachPadCallbacks();

        padGrid.setFilesDroppedOnPadCallback([this](int startPadIndex, const juce::StringArray& files)
        {
            loadDroppedFiles(startPadIndex, files);
        });

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
        startTimerHz(30);
    }

    ~PluginEditorContent() override
    {
        stopTimer();
        app.scheduler.removeListener(this);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::darkgrey.darker());
        g.setColour(juce::Colours::grey);
        g.drawRect(getLocalBounds());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto topBar = area.removeFromTop(60);
        modifierDisplay.setBounds(topBar.removeFromLeft(topBar.getWidth() * 0.5f).reduced(4));

        auto controlBar = topBar;
        playAllButton.setBounds(controlBar.removeFromLeft(90).reduced(2));
        stopAllButton.setBounds(controlBar.removeFromLeft(90).reduced(2));
        modifiersToggle.setBounds(controlBar.removeFromLeft(120).reduced(2));

        auto rightRegion = controlBar;
        implementedOnlyToggle.setBounds(rightRegion.removeFromRight(150).reduced(2));
        quantizeSubdivisionBox.setBounds(rightRegion.removeFromRight(110).reduced(2));
        quantizeToggle.setBounds(rightRegion.removeFromRight(100).reduced(2));

        auto row2 = area.removeFromTop(28).reduced(2);
        padSelectForLoad.setBounds(row2.removeFromLeft(110).reduced(2));
        loadFileButton.setBounds(row2.removeFromLeft(170).reduced(2));
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
            refreshStatus();
        });
    }

private:
    BufferTestAudioProcessor& processor;
    AppState& app;

    UpcomingModifierDisplay modifierDisplay;
    ModifierHistoryPanel modifierHistory;
    PadGridComponent padGrid;

    ModifierSelectionPanel modifierSelectionPanel;
    juce::Viewport modifierSelectionViewport;
    FxStatusPanel fxStatusPanel;

    juce::TextButton playAllButton { "Play All" };
    juce::TextButton stopAllButton { "Stop All" };
    juce::ToggleButton modifiersToggle { "Modifiers" };

    juce::ToggleButton implementedOnlyToggle { "Implemented Only" };
    juce::ToggleButton quantizeToggle { "Quantize" };
    juce::ComboBox quantizeSubdivisionBox;

    juce::ComboBox padSelectForLoad;
    juce::TextButton loadFileButton { "Load File To Pad..." };
    juce::Label statusLabel { {}, "Status: Idle" };

    std::unique_ptr<juce::FileChooser> fileChooser;

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

        const bool ok = app.bufferManager.loadAudioFile(padIndex, file, processor.getFormatManager());
        if (! ok)
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
        padGrid.setPlayingStates(app.bufferManager.getPlayingBufferIndices());

        // Update playhead positions for waveform display.
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
        repaint();
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

    void quantizeToggled()
    {
        app.settings.quantizeEnabled = quantizeToggle.getToggleState();
        app.scheduler.setQuantizationEnabled(app.settings.quantizeEnabled);
    }

    void quantizeSubdivisionChanged()
    {
        int id = quantizeSubdivisionBox.getSelectedId();
        int subdiv = 4;
        switch (id)
        {
            case 1: subdiv = 1; break;
            case 2: subdiv = 2; break;
            case 3: subdiv = 4; break;
            case 4: subdiv = 8; break;
            case 5: subdiv = 16; break;
            default: subdiv = 4; break;
        }
        app.settings.quantizeSubdivision = subdiv;
        app.scheduler.setQuantizationSubdivision(subdiv);
    }

    void loadFileClicked()
    {
        const int padIndex = padSelectForLoad.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow(padIndex, AudioBufferManager::MAX_BUFFERS))
            return;

        fileChooser = std::make_unique<juce::FileChooser>(
            "Select an audio file...",
            juce::File{},
            "*.wav;*.aiff;*.aif;*.flac;*.mp3");

        fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, padIndex] (const juce::FileChooser& chooser)
            {
                auto file = chooser.getResult();
                if (file.existsAsFile())
                {
                    loadFileIntoPad(padIndex, file);
                }
                fileChooser.reset();
            });
    }

    void refreshStatus()
    {
        juce::String s;
        s << (app.scheduler.isRunning() ? "Running" : "Stopped");
        s << " | Selected: " << padGrid.getSelectedPadIndices().size();
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
    g.fillAll (juce::Colours::black);
}

void BufferTestAudioProcessorEditor::resized()
{
    if (content)
        content->setBounds(getLocalBounds());
}
