# Modifier State Presets – Feature Plan

## Summary

Add the ability for the user to **capture the current modifier states across all 8 buffers** and save them as one of **4 named presets** (A / B / C / D). Recalling a preset restores every modifier-affected parameter to its snapshotted values. Preset buttons live in the **Session tab**, are **triggerable via MIDI note-on or mouse click**, and support **right-click context menus** for MIDI learn/clear. The feature is documented in the **Help tab**.

**Explicitly excluded:** Parts settings (`activePart`, `numParts`, `partLengthBars`, loop windows) are NOT captured or restored by presets.

---

## What Constitutes "Modifier State"

Modifier state is currently distributed across two layers per buffer (×8 buffers):

### Per-Buffer: `AudioBuffer` transform state

| Field                     | Type     | Default |
| ------------------------- | -------- | ------- |
| `speed`                   | `double` | `1.0`   |
| `stretchRatio`            | `double` | `1.0`   |
| `pitchSemiTones`          | `double` | `0.0`   |
| `continuousRandomSlicing` | `bool`   | `false` |
| `pingPongEnabled`         | `bool`   | `false` |
| `pingPongDivision`        | `double` | `0.25`  |

### Per-Buffer: `ChannelStrip` FX state

| Field                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           | Type           | Default          |
| ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------- | ---------------- |
| **Enable flags** (`EffectChainPlaceholder`): `delayEnabled`, `reverbEnabled`, `lowPassEnabled`, `highPassEnabled`, `tremoloEnabled`, `chorusEnabled`, `autoPanEnabled`, `volumeRampEnabled`                                                                                                                                                                                                                                                                                                                                                     | `bool`         | `false`          |
| **FX params** (`FxParams`): `reverbWet`, `reverbPreDelayMs`, `delayFeedback`, `delayTimeMs`, `delayWet`, `delayPingPong`, `delayFeedbackHighCutHz`, `delayFbDrive`, `duckingEnabled`, `duckAmount`, `duckReleaseMs`, `wowFlutterEnabled`, `wowDepthMs`, `wowRateHz`, `flutterDepthMs`, `flutterRateHz`, `wowPeriodBars`, `flutterPeriodBars`, `lowPassCutoff`, `highPassCutoff`, `tremoloDepth`, `tremoloRateHz`, `chorusDepth`, `chorusRateHz`, `chorusMix`, `chorusDelayMs`, `panRateHz`, `panDepth`, `panMix`, `panPeriodBars`, `volumeGain` | `float`/`bool` | see `FxParams{}` |

**Total per preset:** 8 × (6 AudioBuffer fields + 8 FX enable flags + ~31 FxParams fields) ≈ **360 values**.

---

## Effort Assessment

| Area                                                                   | Scope                                                 | Estimate     |
| ---------------------------------------------------------------------- | ----------------------------------------------------- | ------------ |
| New data structures (`BufferSnapshot`, `ModifierPreset`, `PresetBank`) | Small – plain structs + serialization                 | ~150 LOC     |
| Capture/restore logic in `AppState`                                    | Medium – read/write across AudioBuffer + ChannelStrip | ~120 LOC     |
| Session tab UI (4 buttons + layout)                                    | Medium – new `PresetBarComponent`                     | ~200 LOC     |
| MIDI control (note map, learn, process)                                | Medium – follows existing pattern exactly             | ~120 LOC     |
| Right-click context menu                                               | Small – reuse `showModifierToggleContextMenu` pattern | ~60 LOC      |
| State serialization (`getStateInformation` / `setStateInformation`)    | Medium – JSON array of 4 preset snapshots             | ~100 LOC     |
| Help tab documentation                                                 | Small – add section + table rows                      | ~30 LOC      |
| **Total**                                                              |                                                       | **~780 LOC** |

**Complexity:** Moderate. No new architectural patterns needed – every subsystem (MIDI learn, context menus, state serialization, timer-based UI polling) has an existing exemplar to follow.

---

## Detailed Changes

### 1. New Data Structures (new file: `ModifierPreset.h`)

```
BufferModifierSnapshot       – per-buffer snapshot (AudioBufferParams subset + FxParams + FX enables)
ModifierPresetSlot           – { name, bool occupied, array<BufferModifierSnapshot, 8> }
ModifierPresetBank           – { array<ModifierPresetSlot, 4> presets, array<int, 4> midiNoteMap }
  ├── captureFromState(AppState&, int slotIndex)
  ├── restoreToState(AppState&, int slotIndex)
  ├── toVar() → juce::var  (JSON serialization)
  └── fromVar(juce::var)    (JSON deserialization)
```

### 2. `AppState.h` / `AppState.cpp`

- Add `ModifierPresetBank presetBank` member.
- Add `capturePreset(int slot)` – reads current state from all 8 `channelStrips` and `bufferManager.buffers`.
- Add `restorePreset(int slot)` – writes snapshot values back, clearing active envelopes on each ChannelStrip and applying AudioBuffer transforms. Skips parts/loop-window restoration.

### 3. `SessionSettings.h`

- Add `std::array<int, 4> presetMidiNoteMap` initialized to `{-1, -1, -1, -1}` (unassigned).

### 4. Session Tab UI (`PluginEditor.cpp` – `PluginEditorContent`)

- Add a **`PresetBarComponent`** (4 buttons labeled A/B/C/D in a horizontal row).
- **Left-click:** If preset slot is occupied → restore preset. If empty → capture current state into slot.
- **Shift+click (or double-click):** Force overwrite – capture current state regardless.
- **Right-click context menu:**
  - "Save to Preset [A–D]" – capture
  - "Recall Preset [A–D]" – restore (grayed if empty)
  - "Clear Preset" – remove saved data
  - Separator
  - "MIDI Learn" – enter learn mode for this slot
  - "Clear MIDI Note" – remove assignment (grayed if none assigned)
- Visual indicator: filled/lit state for occupied slots, dimmed for empty, pulsing border during MIDI learn.
- **Layout change in `resized()`:** Insert a ~36px row between the existing top bar and the status label row.

### 5. MIDI Control (`PluginProcessor.h` / `PluginProcessor.cpp`)

Follow the established pattern for `modifierToggleMidiNote`:

- **New atomics:**
  - `std::atomic<bool> midiPresetLearnEnabled {false}`
  - `std::atomic<int> midiPresetLearnSlotIndex {-1}`
  - `std::atomic<int> learnedPresetMidiNote {-1}`
  - `std::array<std::atomic<bool>, 4> midiPresetRecallRequests` (set by audio thread, polled by UI)

- **In `processBlock()` MIDI loop:**
  1. If learn mode active for presets → store note, disable learn.
  2. Else if note matches `presetMidiNoteMap[i]` → set `midiPresetRecallRequests[i] = true`.

- **In `PluginEditorContent::timerCallback()`:**
  - Poll `learnedPresetMidiNote` – if set, assign to active learn slot, update display.
  - Poll `midiPresetRecallRequests[0..3]` – if set, trigger `app.restorePreset(i)` and refresh UI.

### 6. State Serialization (`PluginProcessor.cpp`)

**`getStateInformation()`** – add:

```json
{
  "presets": [
    {
      "occupied": true,
      "buffers": [
        { "speed": 0.5, "stretch": 1.0, "pitch": 0.0, "slicing": false,
          "pingPong": true, "ppDiv": 0.25,
          "fx": { "delayOn": true, "reverbOn": false, ... },
          "fxParams": { "reverbWet": 0.0, "delayWet": 0.5, ... }
        },
        ... // ×8
      ]
    },
    ... // ×4
  ],
  "presetMidiNotes": [-1, -1, -1, -1]
}
```

**`setStateInformation()`** – deserialize the above back into `presetBank` and `presetMidiNoteMap`.

### 7. Help Tab (`HelpPanelContent.h`)

Add rows to the **"Session Tab Controls"** section table:

| Control            | Description                                                                                          |
| ------------------ | ---------------------------------------------------------------------------------------------------- |
| Preset A–D buttons | Click to recall a saved modifier snapshot. Right-click for save/clear/MIDI options.                  |
| Save Preset        | Right-click a preset button → "Save to Preset" to capture the current modifier states of all 8 pads. |
| Recall Preset      | Click a filled preset button (or trigger via MIDI) to instantly restore all modifier states.         |
| Clear Preset       | Right-click → "Clear Preset" to remove the saved snapshot.                                           |
| Preset MIDI Learn  | Right-click → "MIDI Learn", then send a note to assign. "Clear MIDI Note" to remove.                 |

Add a new **"Modifier Presets"** section between "Session Tab Controls" and "Settings Tab":

> Modifier Presets let you snapshot the current state of all buffer transforms (speed, stretch, pitch, slicing, ping-pong) and effects (reverb, delay, filter, tremolo, chorus, auto-pan, volume ramp) across all 8 pads. Four preset slots (A–D) are available. Presets do NOT save or restore Parts settings, pad file assignments, probability weights, or timing configuration. Presets are saved with your DAW session and restored on reload.

---

## Implementation Checklist

### Data Layer

- [x] Create `ModifierPreset.h` with `BufferModifierSnapshot`, `ModifierPresetSlot`, `ModifierPresetBank` structs
- [x] Implement `toVar()` / `fromVar()` serialization on `ModifierPresetBank`
- [x] Add `ModifierPresetBank presetBank` to `AppState`
- [x] Implement `AppState::capturePreset(int slot)` (read from AudioBuffer + ChannelStrip → snapshot)
- [x] Implement `AppState::restorePreset(int slot)` (write snapshot → AudioBuffer + ChannelStrip, clear envelopes)
- [x] Add `std::array<int, 4> presetMidiNoteMap` to `SessionSettings`

### UI Layer

- [x] Create `PresetBarComponent` (4 styled buttons, occupied/empty visual states)
- [x] Add `PresetBarComponent` to `PluginEditorContent` with layout in `resized()`
- [x] Implement left-click behavior (recall if occupied, save if empty)
- [x] Implement shift+click / double-click to force-save (overwrite)
- [x] Implement right-click context menu (Save / Recall / Clear / MIDI Learn / Clear MIDI)
- [x] Add MIDI note badge display on preset buttons (matching pad badge style)
- [x] Add MIDI learn marching-ants animation (matching pad learn style)
- [x] Theme the preset buttons using `ThemeEngine` colors

### MIDI Control

- [x] Add preset MIDI atomics to `PluginProcessor` (`midiPresetRecallRequests[4]`)
- [x] Add preset note-on detection in `processBlock()` MIDI loop
- [x] Add preset MIDI learn detection in `processBlock()` MIDI loop
- [x] Poll preset MIDI events in `PluginEditorContent::timerCallback()`
- [x] Wire UI callbacks: `startPresetMidiLearn(slot)`, `clearPresetMidiNote(slot)`

### Serialization

- [x] Add preset bank JSON to `getStateInformation()`
- [x] Add `presetMidiNotes` array to `getStateInformation()`
- [x] Parse preset bank JSON in `setStateInformation()`
- [x] Parse `presetMidiNotes` in `setStateInformation()`
- [x] Verify backward compatibility (missing keys → empty presets, no crash)

### Help Documentation

- [x] Add preset button rows to "Session Tab Controls" table
- [x] Add new "Modifier Presets" explanatory section

### Build Verification

- [x] Build VST3 (Debug) — no errors
- [x] Build VST3 (Release) — no errors

### Testing (Manual)

- [ ] Verify capture → recall round-trip for all modifier types (speed, stretch, pitch, slicing, ping-pong, all FX)
- [ ] Verify Parts settings are NOT affected by preset recall
- [ ] Verify pad file assignments are NOT affected
- [ ] Verify MIDI learn and recall works for all 4 slots
- [ ] Verify presets survive DAW session save/reload (`getStateInformation` / `setStateInformation`)
- [ ] Verify empty preset slots are handled gracefully (no crash on recall)
- [ ] Verify UI updates correctly on capture/recall/clear
