# MIDI Control for Pads

The BufferTest plugin now supports MIDI input to control pad selection.

## MIDI Note Mapping

The plugin uses the **General MIDI drum map layout** for pad control:

| MIDI Note | Note Name | Pad Index |
|-----------|-----------|-----------|
| 36        | C1        | Pad 0     |
| 37        | C#1       | Pad 1     |
| 38        | D1        | Pad 2     |
| 39        | D#1       | Pad 3     |
| 40        | E1        | Pad 4     |
| 41        | F1        | Pad 5     |
| 42        | F#1       | Pad 6     |
| 43        | G1        | Pad 7     |

## Behavior

- **Toggle Mode**: Note-on messages toggle the pad selection on/off
- **Note-off Ignored**: Releasing a pad does not change its state
- **Velocity Ignored**: The velocity value has no effect (for now)

## Usage

1. Connect a MIDI controller to your DAW
2. Route MIDI to the BufferTest plugin track
3. Play notes 36-43 to toggle pad selections
4. Selected pads will be targeted by the next modifier

This mapping is compatible with most drum pad controllers like:
- Akai MPD series
- Native Instruments Maschine
- Novation Launchpad
- Any MIDI keyboard (notes C1-G1)

## Implementation Details

- MIDI messages are processed on the audio thread in `processBlock`
- Thread-safe atomic flags communicate toggle requests to the UI thread
- The editor polls these flags in its timer callback (~30-60Hz)
- No blocking or allocation occurs on the audio thread

## Future Enhancements

Potential future additions:
- Configurable note mapping
- Gate mode (note-off deselects)
- Velocity-sensitive selection
- LED feedback to controllers
- MIDI learn functionality
