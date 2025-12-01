/*
 ============================================================================== 
   MainAppComponent.h
   --------------------------------------------------------------------------
   New application UI root that will supersede the legacy MainComponent test UI.
   Provides: pad grid, upcoming modifier banner, basic transport, per-pad load.
   Incremental: minimal functionality now; more features added over checklist.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "AppState.h"
#include "PadGridComponent.h"
#include "UpcomingModifierDisplay.h"
#include "ModifierHistoryPanel.h"
#include "ModifierSelectionPanel.h"

class MainAppComponent : public juce::AudioAppComponent,
                         public ModifierSchedulerListener,
                         private juce::Timer
{
public:
    MainAppComponent();
    ~MainAppComponent() override;

    // AudioAppComponent
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    // Component
    void paint(juce::Graphics&) override;
    void resized() override;

    // ModifierSchedulerListener
    void upcomingModifierChanged(const ModifierDescriptor& desc) override;
    void modifierTriggered(const ModifierDescriptor& desc, const juce::Array<int>& targets) override;

private:
    AppState app; // Owns buffer manager & scheduler

    // UI components
    UpcomingModifierDisplay modifierDisplay;
  ModifierHistoryPanel modifierHistory;
    PadGridComponent padGrid;
  ModifierSelectionPanel modifierSelectionPanel; // new developer panel
    juce::TextButton playAllButton { "Play All" };
    juce::TextButton stopAllButton { "Stop All" };
  juce::ToggleButton modifiersToggle { "Modifiers" }; // Links playback to scheduler when ON
    juce::TextButton loadFileButton { "Load File To Pad..." };
    juce::TextButton saveProjectButton { "Save Project" };
    juce::TextButton loadProjectButton { "Load Project" };
    juce::ComboBox padSelectForLoad;
    juce::Label statusLabel { {}, "Status: Idle" };
  juce::ToggleButton implementedOnlyToggle { "Implemented Only" };
  juce::ToggleButton quantizeToggle { "Quantize" };
  juce::ComboBox quantizeSubdivisionBox; // values: 1,2,4,8,16
  juce::Slider bpmSlider; // horizontal BPM control
  juce::Label bpmLabel { {}, "BPM" };

    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // Timing for scheduler sample-accurate feed (temporary simple method)
    double hostSampleRate = 44100.0;

    void timerCallback() override; // UI refresh + feed scheduler using wall clock fallback

    // UI handlers
    void playAllClicked();
    void stopAllClicked();
  void modifiersToggleChanged();
    void loadFileClicked();
    void updatePadSelectionTargets();
    void refreshStatus();
  void implementedOnlyToggled();
  void bpmChanged();
  void updatePlaybackModifierLink();
  void quantizeToggled();
  void quantizeSubdivisionChanged();
  void saveProjectClicked();
  void loadProjectClicked();

    void attachPadCallbacks();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainAppComponent)
};
