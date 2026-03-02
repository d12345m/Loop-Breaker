/*
 ============================================================================== 
   SessionSettings.h
   --------------------------------------------------------------------------
   Central configuration struct for session & project level settings.
   This will evolve; for now provides basic timing and project metadata.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ModifierProbabilityManager.h"

struct SessionSettings
{
    // Modifier probability weights (per-type slider values)
    ModifierProbabilityManager modifierProbabilities;
    // Musical timing
    double bpm = 120.0;                 // Beats per minute
    int timeSigNumerator = 4;           // e.g. 4 in 4/4
    int timeSigDenominator = 4;         // e.g. 4 in 4/4
    int barsBetweenModifiers = 4;       // How many bars elapse before next modifier is applied

  // Modifier scheduling
  bool modifiersEnabled = true;       // If false, the modifier queue is stopped/suppressed

  // Scheduler quantization
  bool quantizeEnabled = false;       // If true, triggers snap to grid
  int quantizeSubdivision = 4;        // Subdivisions per bar when quantized (1,2,4,8,16)

    // Project / playback configuration
    bool multiChannelRecording = false; // If true, render each buffer to its own output bus (future)
    int numBuffers = 8;                 // Always 8 for MPC-style grid (matches AudioBufferManager::MAX_BUFFERS)
  int maxPartsPerBuffer = 4;          // A-D parts
  // Parts (Musical Sections A–D): equal-length boundaries
  struct PartsConfig {
    int activePart = 0;            // 0-based index
    int partLengthBars = 4;        // equal length per design
    int numParts = 1;              // configurable 1..4 (default to 1 on new instance)
    // Derived helpers
    int getNumParts() const { return juce::jlimit(1, 4, numParts); }
    // Get start bar offset for a given part index (0..3)
    int getPartStartBar(int partIndex) const { return juce::jlimit(0, getNumParts()-1, partIndex) * partLengthBars; }
  } parts;

    // Visual / UX
    juce::String themeName { "Neon Rave (Dark)" };
    bool animationsEnabled = false;
    bool bgCycleEnabled = false;
    bool padPulseEnabled = false;
    bool progressShimmerEnabled = false;
    bool knobGlowEnabled = false;
    float animationSpeed = 1.0f;
    int backgroundMode = 0; // 0=Static, 1=SlowCycle, 2=Reactive

  // Pad file paths (absolute). Index corresponds to pad/buffer index. Empty means no file.
  juce::StringArray padFilePaths { "", "", "", "", "", "", "", "" };

    // MIDI note mappings (per pad). -1 means unassigned.
    // Layout: bottom row left->right = 36-39 (pads 1-4), top row left->right = 40-43 (pads 5-8)
    std::array<int, 8> midiNoteMap { 36, 37, 38, 39, 40, 41, 42, 43 };

    // MIDI note for toggling modifiers on/off. -1 means unassigned.
    int modifierToggleMidiNote = -1;

    // MIDI note mappings for modifier preset recall (A-D). -1 means unassigned.
    static constexpr int kNumPresets = 4;
    std::array<int, kNumPresets> presetMidiNoteMap = { -1, -1, -1, -1 };

    // MIDI CC mappings for modifier probability sliders.
    // Index i corresponds to ModifierProbabilityManager::allModifierTypes()[i].
    // Value is CC number (0-127), or -1 if unassigned.
    static constexpr int kNumModifierTypes = 22;
    std::array<int, kNumModifierTypes> midiProbCCMap = []() {
        std::array<int, kNumModifierTypes> a;
        a.fill(-1);
        return a;
    }();

    // Per-pad target probability: controls the likelihood of each pad being
    // auto-selected as a modifier target. 1.0 = always eligible, 0.0 = never.
    static constexpr int kNumPads = 8;
    std::array<float, kNumPads> padTargetProbabilities = { 1.0f, 1.0f, 1.0f, 1.0f,
                                                           1.0f, 1.0f, 1.0f, 1.0f };

    // MIDI CC mappings for pad target probability sliders.
    // Index is pad index (0-7). Value is CC number (0-127), or -1 if unassigned.
    std::array<int, kNumPads> midiPadProbCCMap = []() {
        std::array<int, kNumPads> a;
        a.fill(-1);
        return a;
    }();

    // Persistence meta
    juce::String projectName { "Untitled Project" };
    juce::String projectId { juce::Uuid().toString() };

    // Utility helpers
    double getBeatsPerBar() const { return static_cast<double>(timeSigNumerator); }
    double getSecondsPerBeat() const { return 60.0 / juce::jmax(1.0, bpm); }
    double getSecondsPerBar() const { return getSecondsPerBeat() * getBeatsPerBar(); }
    double getSecondsBetweenModifiers() const { return getSecondsPerBar() * barsBetweenModifiers; }
};
