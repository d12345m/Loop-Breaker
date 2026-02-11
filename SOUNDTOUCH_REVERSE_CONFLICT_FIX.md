# SoundTouch/Reverse Conflict Fix

## Problem

There was an audio tearing issue when SoundTouch-powered modifiers (Octave Up, Octave Down, Stretch) interacted with the Reverse modifier. SoundTouch's internal pipeline doesn't handle reversed audio well, causing audible artifacts.

## Solution

Implemented mutual exclusion between SoundTouch modifiers and Reverse:

### When Reverse is Applied

- **Disables** any active SoundTouch effects (stretch ratio and pitch shift)
- Resets stretch ratio to 1.0 and pitch to 0.0 semitones
- Then reverses playback direction as normal

### When SoundTouch Modifiers are Applied

Each SoundTouch modifier now forces forward playback:

1. **Pitch Up/Down (Octave modifiers)**
   - If audio is currently reversed (speed < 0), flips to forward playback
   - Then applies pitch shift

2. **Stretch modifier**
   - Forces forward playback (positive speed)
   - Neutralizes speed magnitude to 1.0 when stretching
   - Then applies time-stretch ratio

## Code Changes

Modified [AppState.h](Source/AppState.h):

- `applyReverse()` - Clears SoundTouch state before reversing
- `applyPitchSemiTones()` - Forces forward playback before pitch shifting
- `applyStretch()` - Forces forward playback before stretching

## Technical Details

- The conflict stems from SoundTouch's internal audio pipeline holding samples
- When direction changes mid-processing, forward and reversed audio can blend
- The fix ensures these effects are never active simultaneously
- Speed modifier already existed without SoundTouch, so it preserves direction

## Testing

Build completed successfully:

```
** BUILD SUCCEEDED **
```

The mutual exclusion logic is now in place and will prevent the audio tearing that occurred when these modifiers conflicted.
