# UI Performance Investigation: 8 Individual Outputs

## Summary

When all 8 individual output buses are active, the LoopBreaker UI becomes progressively sluggish. The root cause is **not** the output buses themselves — audio-thread output routing is efficient (`addFrom` loops). The problem is on the **UI thread**, where multiple independent timer-driven components iterate over all 8 channel strips every repaint, performing expensive string formatting, atomic reads, and text rendering inside `paint()` methods without any caching or dirty-state checks.

---

## Architecture Overview

The plugin exposes 9 stereo output buses: 1 main mix + 8 individual pad outputs. The audio `processBlock` handles this cleanly via parallel per-buffer rendering into scratch buffers, then a sequential merge into each bus (`PluginProcessor.cpp` ~L790–810). This is not the bottleneck.

The UI side has **9 independent juce::Timer instances** running simultaneously, each calling `repaint()` on their component at a fixed rate:

| Component | Rate | Per-frame channel work | Visibility |
|---|---|---|---|
| `UpcomingModifierDisplay` | 30 Hz | None | Always visible (Session tab) |
| `PresetBarComponent` | 30 Hz | None (4 slots only) | Always visible (Session tab) |
| `PadGridComponent` | 20 Hz | 8× flash/glow animator ticks | Always visible (Session tab) |
| `PluginEditorContent` | 20 Hz | 8× playhead + loop + state sync | Always visible |
| `BackgroundAnimator` | 15 Hz | None | Always visible |
| `ModifierProbabilityPanel` | 10 Hz | Label refresh per modifier type | Probability tab |
| `FxStatusPanel` | 5 Hz | 8× string concat + float format | Settings/Debug tab |
| `NodeClipDebugPanel` | 4 Hz | 8×14 = 112 cell renders | Debug tab |
| `TearingDebugPanel` | 2 Hz | 8×18 = 144 atomic reads + text | Debug tab |

The critical insight is that **JUCE timers fire on the message thread regardless of component visibility**. A component in a background tab still has its timer running, builds strings, and calls `repaint()` — the repaint may be coalesced by the framework but the timer callback work is not.

---

## Primary Bottlenecks

### 1. `PluginEditorContent::timerCallback()` — 20 Hz, Always Active

**Location:** [PluginEditor.cpp](Source/PluginEditor.cpp#L526)

This is the most impactful timer because it runs on every tab and does per-channel work every tick:

```
Lines 667-690: For all 8 buffers:
  - setTotalSamplesForPad()
  - setPlayheadForPad()
  - setLoopWindowForPad()
  Then: padGrid.repaint()  ← unconditional full repaint
```

**Problem:** `padGrid.repaint()` is called unconditionally every 50ms (line 693), forcing a full repaint of all 8 pad cells even when nothing has changed. Each pad repaint involves:
- Waveform thumbnail drawing (`drawChannels`)
- Loop overlay with diagonal hatching (clip region + line drawing in a loop)
- Playhead with gradient glow
- Selection overlay with glow
- Flash/glow animator overlay
- Multiple `fillRoundedRectangle` / `drawRoundedRectangle` calls

**Cost:** 8 pads × ~15 draw operations × 20 Hz = **~2,400 draw calls/sec** from this component alone.

### 2. `FxStatusPanel::paint()` — 5 Hz, Debug/Settings Tab

**Location:** [FxStatusPanel.h](Source/FxStatusPanel.h#L29-L50)

Rebuilds per-pad strings entirely inside `paint()` every frame:

```cpp
for (int i = 0; i < app.channelStrips.size(); ++i)  // 8 iterations
{
    juce::String line = "Pad " + juce::String(i+1) + ": "
        + (fx.reverbEnabled ? "Reverb " : "") + ...;  // 9 conditional concats
    juce::String p = juce::String::formatted("rvb=%.2f pd=%.0fms ...", ...);
                                             // 8 floats formatted
}
```

**Problem:** ~80 string concatenations + 8 `String::formatted()` calls per frame. Strings are never cached — identical output is rebuilt every 200ms even if no FX state has changed.

### 3. `TearingDebugPanel::paint()` — 2 Hz, Debug Tab

**Location:** [TearingDebugPanel.h](Source/TearingDebugPanel.h#L117-L181)

Per-pad row with 18 columns of atomic reads + `juce::String` construction + `drawText`:

```cpp
for (int i = 0; i < app.channelStrips.size(); ++i)  // 8 pads
{
    g.drawText(juce::String(stats.emptyOutputBuffers.load()), ...);   // ×18 columns
}
// Then a second 8-iteration loop for totals
```

**Per frame:** 144 atomic loads + 144 `juce::String(int)` constructions + 144 `drawText` calls, plus a second pass of ~32 atomic loads for the summary row.

### 4. `NodeClipDebugPanel::paint()` — 4 Hz, Debug Tab

**Location:** [NodeClipDebugPanel.h](Source/NodeClipDebugPanel.h#L105-L165)

8 pads × 14 signal-chain nodes = 112 cells per frame:

```cpp
for (int padIdx = 0; padIdx < 8; ++padIdx)
    for (int n = 0; n < 14; ++n)  // per node
    {
        // Color severity check (4 branches with atomic reads)
        g.fillRect(cell.reduced(1));     // alpha-blended fill
        g.drawText(...);                  // clip count or peak dB
    }
```

**Per frame:** 112 cells × (2–4 atomic reads + color logic + fillRect + drawText) = ~450 atomic reads + 224 draw calls. Plus a pulsing-red indicator using `sin(getMillisecondCounterHiRes())` computed per paint, driving continuous invalidation.

### 5. Cascading Repaints from `padGrid.repaint()`

The `PluginEditorContent` timer calls `padGrid.repaint()` unconditionally at 20 Hz (line 693). The `PadGridComponent::paintOverChildren()` then renders all 8 pads with:
- Gradient fills, rounded rectangles, waveform thumbnails
- Loop region hatching (a `for` loop drawing diagonal lines inside clip regions)
- Playhead glow gradient

This work is done even when the playhead hasn't moved and no visual state has changed.

---

## Why It Gets Worse Over Time

The phrase "starts to drag over time" suggests a **leak or accumulation** rather than a static overhead. Possible causes:

1. **Heap fragmentation from `juce::String` allocations in `paint()`**: Every frame, `FxStatusPanel`, `TearingDebugPanel`, and `NodeClipDebugPanel` create hundreds of temporary `juce::String` objects. Over hours of operation, this fragments the small-object heap, making allocations progressively slower.

2. **Tearing/clip stat counters grow unboundedly**: `TearingDebugPanel` reads `stats.getTotalEvents()` which sums all atomic counters. As these grow into large integers, `juce::String(int)` formatting and text width measurement may behave slightly differently, but more importantly the severity-coloring logic re-evaluates on every paint.

3. **AudioThumbnail cache pressure**: The `PadGridComponent` creates 8 `AudioThumbnail` objects with a shared cache of 64 entries. When all 8 are loaded and being redrawn at 20 Hz, cache thrashing could occur if the waveforms are large enough.

4. **Animator callback closures**: `PadGridComponent::flashPads()` creates lambda closures capturing `this` and `idx` for each glow animation. If modifiers fire frequently, old animator closures may accumulate before being replaced.

---

## Quantified Per-Frame Cost (8 Outputs Active)

| Component | Strings/frame | Atomic reads/frame | Draw calls/frame | Frames/sec |
|---|---|---|---|---|
| PluginEditorContent | ~10 | ~48 | ~240 | 20 |
| FxStatusPanel | ~80 | ~72 | ~24 | 5 |
| TearingDebugPanel | ~176 | ~176 | ~176 | 2 |
| NodeClipDebugPanel | ~112 | ~450 | ~224 | 4 |
| PadGridComponent | ~16 | ~16 | ~120 | 20 |
| **Totals (per second)** | **~5,400** | **~12,600** | **~10,500** | — |

---

## Testing Plan

### A. Reproduce and Measure

1. **Baseline measurement (stereo only):**
   - Load the plugin in a DAW with only the main stereo output bus active
   - Load 8 samples, start playback, enable modifiers
   - Record UI frame rate and CPU usage for 30 minutes using Instruments (Time Profiler)
   - Note: measure `juce::MessageThread` time specifically

2. **8-output measurement:**
   - Enable all 8 individual output buses in the DAW (route each to a separate track)
   - Same 8 samples + playback + modifiers
   - Record the same metrics for 30 minutes
   - Compare frame rate degradation over time between the two cases

3. **Tab-specific measurement:**
   - With 8 outputs active, measure CPU on each tab:
     - Session tab (main UI — PluginEditorContent + PadGrid + UpcomingModifier + PresetBar)
     - Probability tab
     - Settings tab
     - Debug tab (TearingDebugPanel + NodeClipDebugPanel + ModifierHistory)
     - Help tab
   - This isolates whether the debug panels are a factor even when not visible

### B. Isolate Timer Overhead

4. **Timer-disable test:**
   - Add a `#if 0` guard around `startTimerHz` in each of these components one at a time:
     - `FxStatusPanel` (5 Hz)
     - `TearingDebugPanel` (2 Hz)
     - `NodeClipDebugPanel` (4 Hz)
     - `PluginEditorContent` (20 Hz — reduce to 10 Hz)
   - Re-measure CPU after each to quantify the contribution of each timer

5. **Repaint coalescing test:**
   - Add a counter to `PadGridComponent::paintOverChildren()` to log how often it's actually called per second
   - Compare against the expected 20 Hz from the timer
   - Check if multiple timers cause the same component to repaint more than once per JUCE message loop cycle

### C. Memory / Accumulation Tests

6. **Long-duration heap test:**
   - Run with 8 outputs for 2+ hours
   - Use Instruments Allocations to track `juce::String` heap growth
   - Check for monotonically increasing memory in the message thread

7. **Stat counter growth test:**
   - Run with 8 outputs and frequent modifiers for 1 hour
   - Check if tearing/clip stat counters grow unbounded
   - Reset stats halfway through and see if UI responsiveness improves

### D. Targeted Fixes to Validate

8. **Dirty-flag test for PadGrid:**
   - Add a dirty flag to `PadGridComponent` — only call `repaint()` from `PluginEditorContent::timerCallback()` when playhead position actually changes (compare to last-painted value with a small epsilon)
   - Measure CPU reduction

9. **String caching test for FxStatusPanel:**
   - Cache the formatted strings and only rebuild when FX state actually changes
   - Measure string allocation reduction

10. **Visibility guard test:**
    - Add `if (!isShowing()) return;` at the top of each debug panel's `timerCallback()`
    - Measure CPU reduction when on non-debug tabs

---

## Recommended Fixes (Priority Order)

### Immediate (High Impact)

1. **Add visibility guards to all timer callbacks:**
   ```cpp
   void timerCallback() override {
       if (!isShowing()) return;  // skip work when in background tab
       repaint();
   }
   ```
   Affected: `FxStatusPanel`, `TearingDebugPanel`, `NodeClipDebugPanel`, `ModifierProbabilityPanel`

2. **Make `padGrid.repaint()` conditional:**
   Only repaint when playhead positions or loop windows actually change. Store last-painted values and compare.

3. **Cache formatted strings in `FxStatusPanel`:**
   Build strings in `timerCallback()`, store in a member, only rebuild when FX params differ from last snapshot.

### Short-term (Medium Impact)

4. **Reduce timer rates:**
   - `UpcomingModifierDisplay`: 30 Hz → 15 Hz (shimmer is subtle enough)
   - `PluginEditorContent`: 20 Hz → 15 Hz (playhead movement is smooth at 15)
   - `PresetBarComponent`: 30 Hz → 15 Hz (glow animations are smooth at 15)

5. **Batch atomic reads in debug panels:**
   Read all stats once in `timerCallback()` into a plain struct, then use the cached struct in `paint()`.

### Architecture (Long-term)

6. **Move to a single coordinated timer:**
   Replace 9 independent timers with a single 30 Hz timer that dispatches updates only to visible components. This eliminates overlapping repaint requests and ensures at most one paint pass per frame.

7. **Implement partial invalidation:**
   Instead of `repaint()` (full component), use `repaint(Rectangle)` to only invalidate the specific pad cell or row that changed.
