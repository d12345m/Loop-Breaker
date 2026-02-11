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
#include "FxStatusPanel.h"

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
  juce::Viewport modifierSelectionViewport; // scroll container for selection panel
  FxStatusPanel fxStatusPanel { app }; // read-only FX status
    juce::TextButton playAllButton { "Play All" };
    juce::TextButton stopAllButton { "Stop All" };
  juce::ToggleButton modifiersToggle { "Modifiers" }; // Links playback to scheduler when ON
    juce::TextButton saveProjectButton { "Save Project" };
    juce::TextButton loadProjectButton { "Load Project" };
    juce::Label projectNameLabel { {}, "Project" };
    juce::TextEditor projectNameEditor;
    juce::Label statusLabel { {}, "Status: Idle" };
  juce::ToggleButton implementedOnlyToggle { "Implemented Only" };
  juce::Slider bpmSlider; // horizontal BPM control
  juce::Label bpmLabel { {}, "BPM" };
  // Parts count selector (1–4)
  juce::ComboBox partsCountBox;
  int pendingPartsCount = -1; // -1 = no pending change
  // Bars between modifiers slider
  juce::Slider barsBetweenModifiersSlider;
  juce::Label barsBetweenModifiersLabel { {}, "Bars/Mod" };
  // Dev controls
  juce::TextButton triggerNowButton { "Trigger Now" };
  juce::TextButton skipUpcomingButton { "Skip" };

    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // Timing for scheduler sample-accurate feed (temporary simple method)
    double hostSampleRate = 44100.0;

    void timerCallback() override; // UI refresh + feed scheduler using wall clock fallback

    // UI handlers
    void playAllClicked();
    void stopAllClicked();
  void modifiersToggleChanged();

    void updatePadSelectionTargets();
    void refreshStatus();
  void implementedOnlyToggled();
  void bpmChanged();
  void updatePlaybackModifierLink();
  void partsCountChanged();
  void barsBetweenModifiersChanged();
  bool isTransportRunning() const;
  void saveProjectClicked();
  void loadProjectClicked();
  void restorePadFilesFromSettings();
  void projectNameEdited();
  juce::String sanitizeProjectName(const juce::String& in) const;
  juce::String suggestDefaultProjectName() const;

    void attachPadCallbacks();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainAppComponent)
};
