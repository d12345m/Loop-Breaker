# Buffer Slicing Implementation Guide

## Overview

The BufferTest application now includes advanced buffer slicing functionality that allows for sophisticated playback manipulation, similar to what you'd find in professional music production software and samplers.

## Key Features

### 1. Buffer Slicing
- Divide any loaded audio buffer into 1-64 configurable slices
- Each slice represents an equal portion of the original audio
- Default setting: 32 slices (perfect for 16th note divisions in a 2-bar loop)

### 2. Continuous Random Slice Mode
- **Start Random Slicing**: Begins continuous random slice playback
- Each slice plays to completion at the current speed setting
- Automatically jumps to a new random slice after each slice completes
- Works with both forward and reverse playback directions
- **Stop Random Slicing**: Returns to normal playback mode

### 3. Playback Direction Support
- **Forward Playback**: Slices play from start to end, then jump to random slice start
- **Reverse Playback**: Slices play from end to start, then jump to random slice end
- Speed control works normally with slicing (-2x to +2x)

### 4. Control Functions

#### Core Methods
```cpp
void setNumSlices(int numSlices);           // Set number of slices (1-64)
void triggerSlice(int sliceIndex);          // Jump to specific slice
void triggerRandomSlice();                  // Start continuous random mode
void resetToBeginning();                    // Reset position to start
void resetToDefaults();                     // Reset all settings
```

#### Query Methods
```cpp
int getCurrentSlice() const;                // Get current slice index
int getNumSlices() const;                  // Get total slice count
bool isInContinuousRandomMode() const;     // Check if in random mode
```

## User Interface

### New Controls
- **Buffer Slices Slider**: Adjusts number of slices (1-64)
- **Start/Stop Random Slicing Button**: Toggles continuous random mode
- **Reset to Defaults Button**: Resets all parameters
- **Current Slice Display**: Shows active slice and mode status

### Visual Feedback
- Orange text indicates active random slicing mode
- Button colors change to reflect current state
- Real-time slice position updates

## Integration Notes

### Thread Safety
All slice operations are thread-safe using atomic variables:
- `std::atomic<int> targetSlice`
- `std::atomic<int> currentActiveSlice`
- `std::atomic<bool> sliceTriggered`
- `std::atomic<bool> isSlicingMode`
- `std::atomic<bool> continuousRandomMode`

### Performance
- Slice calculations are lightweight (simple division/multiplication)
- No audio copying - uses position offsets only
- Maintains high-quality interpolation throughout

### Use Cases
1. **Live Performance**: Trigger random sections of loops during performances
2. **Beat Slicing**: Create glitchy, randomized drum patterns
3. **Creative Sampling**: Generate unexpected variations from existing material
4. **Sound Design**: Create evolving textures from static sounds

## Technical Implementation

### Active Slice Tracking
The system now maintains a separate `currentActiveSlice` atomic variable that tracks which slice is currently being played, independent of the playhead position. This ensures proper behavior in both forward and reverse playback modes.

### Slice Calculation
```cpp
double sliceSize = fileLengthSamples / numSlices;
double startPos = sliceIndex * sliceSize;
double endPos = (sliceIndex + 1) * sliceSize;
```

### Random Selection
Uses JUCE's Random class for consistent, high-quality random number generation:
```cpp
int nextSlice = random.nextInt(numSlices);
```

### Boundary Handling
- Forward: Jump to random slice start when current slice end reached
- Reverse: Jump to random slice end when current slice start reached
- Maintains crossfade capability for smooth transitions

## Future Enhancements

Potential additions for larger projects:
- Slice velocity/volume control
- Slice-specific effects processing
- MIDI triggering of individual slices
- Slice sequence recording/playback
- Multiple simultaneous slice voices
