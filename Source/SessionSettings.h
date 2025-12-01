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

struct SessionSettings
{
    // Musical timing
    double bpm = 120.0;                 // Beats per minute
    int timeSigNumerator = 4;           // e.g. 4 in 4/4
    int timeSigDenominator = 4;         // e.g. 4 in 4/4
    int barsBetweenModifiers = 4;       // How many bars elapse before next modifier is applied

  // Scheduler quantization
  bool quantizeEnabled = false;       // If true, triggers snap to grid
  int quantizeSubdivision = 4;        // Subdivisions per bar when quantized (1,2,4,8,16)

    // Project / playback configuration
    bool multiChannelRecording = false; // If true, render each buffer to its own output bus (future)
    int numBuffers = 8;                 // Always 8 for MPC-style grid (matches AudioBufferManager::MAX_BUFFERS)
    int maxPartsPerBuffer = 4;          // A-D parts; currently unused

    // Visual / UX
    juce::String themeName { "Default" };

    // Persistence meta
    juce::String projectName { "Untitled Project" };
    juce::String projectId { juce::Uuid().toString() };

    // Utility helpers
    double getBeatsPerBar() const { return static_cast<double>(timeSigNumerator); }
    double getSecondsPerBeat() const { return 60.0 / juce::jmax(1.0, bpm); }
    double getSecondsPerBar() const { return getSecondsPerBeat() * getBeatsPerBar(); }
    double getSecondsBetweenModifiers() const { return getSecondsPerBar() * barsBetweenModifiers; }
};
