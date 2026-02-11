# SoundTouch Audio Tearing — Root Cause Analysis & TODO

**Date:** 2026-02-11  
**Context:** Audio tearing related to SoundTouch when combined with speed changes, reverse, pitch shift, and slicing.

---

## Executive Summary

The SoundTouch time-stretch engine is used for two features: **time-stretch (tempo without pitch change)** and **pitch shifting**. Audio tearing occurs when these interact with **reverse playback** and **speed changes** because:

1. SoundTouch's internal pipeline is never flushed when the playback direction reverses.
2. SoundTouch's overlap-add algorithm receives abrupt input discontinuities (direction flips, slice jumps) without any reset or crossfade on its input side.
3. The mode-transition crossfade between repitch ↔ SoundTouch only fades the new block in — it doesn't fade the old block out.
4. Multiple parameters (`speed`, `stretchRatio`, `pitchSemiTones`, `direction`) are changed independently via atomics with no coordinated snapshot, so the audio thread can see inconsistent combinations for one block.

---

## Problematic Combinations (Ranked by Severity)

### 🔴 CRITICAL — Combo A: Reverse while SoundTouch is active (Stretch or Pitch ≠ 0)

**How it happens:** Stretch or pitch shift is active → Reverse modifier fires → `setSpeed(-abs(speed))` flips direction.

**Why it tears:**
- `setSpeed()` only writes `params.speed`. It does **not** set `stretcherNeedsReset`.
- On the next `processBlock`, `direction` flips from `+1` to `−1` (line 488).
- `fillInputScratch` starts reading the source buffer **backwards**.
- But SoundTouch's internal FIFO still contains **forward-read samples** from the previous block(s).
- SoundTouch's TDHS overlap-add now blends old forward samples with new reversed samples at the splice point.
- Result: a brief burst of **phased/distorted audio** (the forward tail + reversed head collide), followed by potentially poor correlation matches as SoundTouch tries to find overlap windows in reversed waveforms.

**Affected code:** `AudioBuffer::setSpeed()` at AudioBuffer.cpp line ~400, `processWithTimeStretch` direction computation at line ~488.

---

### 🔴 CRITICAL — Combo B: Repitch → SoundTouch mode transition (Speed then Stretch, or vice versa)

**How it happens:** Buffer playing at speed 2.0x (repitch) → Stretch modifier fires → switches to SoundTouch path.

**Why it tears:**
- Mode transition triggers `stretcherNeedsReset`, which clears SoundTouch's pipeline.
- The crossfade (lines 82–101) blends the *tail* of the previous block with the *head* of the new block, but the new block may be partially silent because SoundTouch hasn't been primed yet (underrun).
- The playhead advancement rate changes abruptly: repitch path advances at `speed × (fileSR/hostSR)` per sample, but SoundTouch path advances at `direction × tempoMult × (fileSR/hostSR)` — the speed magnitude is dropped or re-routed to `setRate()`.
- The audio content at the splice point is discontinuous because the source position was "skipping" at 2x rate and now reads at 1x rate.

**Note:** This was partially documented in SPEED_STRETCH_CONFLICT_REPORT.md but the existing crossfade (added since that report) only partially addresses it — underruns in the first 1–2 SoundTouch blocks still produce audible gaps.

---

### 🟠 HIGH — Combo C: Reverse toggle while SoundTouch is active AND speed ≠ 1.0

**How it happens:** Speed 2.0x + Pitch +12 (SoundTouch active with `useRate`) → Reverse fires.

**Why it tears:**  
This is Combo A compounded by the `useRate` interaction:
- `useRate` is true when pitch is inactive and `|speed| ≠ 1.0`. When pitch IS active, `useRate` is false and speed magnitude is folded into `tempoRatioForSt = smoothedRatio * clampedRate`.
- When direction flips, `clampedRate` (which is `abs(params.speed)`) doesn't change. But the input feed flips instantly.
- SoundTouch sees the same `setRate()` / `setTempo()` / `setPitch()` parameters but its input suddenly runs backwards.
- The overlap-add correlation windows are optimized for forward audio patterns. Reversed audio has different auto-correlation characteristics (attack/decay transients are inverted), so SoundTouch's seek algorithm may land on poor overlap points, producing **metallic phasing or echo artifacts** on every processing window (~60ms chunks).

---

### 🟠 HIGH — Combo D: Slice jump while SoundTouch is active

**How it happens:** Continuous random slicing + stretch (or pitch) active → slice boundary hit → playhead jumps to new slice start.

**Why it tears:**
- When a slice triggers inside `fillInputScratch`, the local playhead jumps from one position to a completely different one (e.g., sample 80000 → sample 12000).
- SoundTouch's internal FIFO has buffered input samples around position 80000.
- New input starts at position 12000 — a huge content discontinuity.
- SoundTouch's overlap-add tries to splice these together, producing a brief ~12ms artifact at the window boundary.
- The existing crossfade in `applyCrossfadeToSliceTransition` operates on the **stretchInScratch** buffer (SoundTouch's input), which helps somewhat, but SoundTouch's internal state still holds the old position's audio.

---

### 🟡 MEDIUM — Combo E: Pitch shift active + speed change (without stretch)

**How it happens:** Pitch shift is active (+12 semitones) → Speed modifier fires (speed = 0.5x).

**Why it tears:**
- Pitch alone activates SoundTouch. Speed change is routed through SoundTouch's tempo (since `pitchActive` is true and `useRate` becomes false).
- `tempoRatioForSt = smoothedRatio * clampedRate`. If `smoothedRatio` was 1.0 and speed changes from 1.0→0.5, `clampedRate` goes from 1.0 to 0.5, so `tempoRatioForSt` halves.
- The `stretchSmoother` smooths `stretchRatio` but **not** `clampedRate`. The rate parameter change is applied instantly to SoundTouch.
- SoundTouch handles gradual tempo changes reasonably well, but an abrupt halving in one block can produce a brief pitch/tempo glitch.

---

### 🟡 MEDIUM — Combo F: Boundary crossfade position mismatch in stretch mode

**How it happens:** Loop boundary or slice crossfade while SoundTouch is active.

**Why it tears:**
- `applyCrossfadeToSliceTransition` reads source audio at the "previous" position to blend with "current" position.
- The effective speed used for computing the previous position is:  
  ```cpp
  useStretch ? (direction * tempoMultiplier) : getEffectiveSpeed()
  ```
- When `useRate` is true (speed routed through SoundTouch rate), the *perceived* audio rate differs from the source-read rate. The crossfade blends source samples at the wrong relative rate compared to what SoundTouch actually outputs.
- Result: subtle phasing/flanging during the 20ms crossfade window.

---

### 🟡 MEDIUM — Combo G: `applyReverse` doesn't toggle — it only sets negative

**How it happens:** Reverse fires twice on the same buffer.

**Why it matters:**
```cpp
void applyReverse(...)
{
    double s = b->getSpeed();
    if (s == 0.0) s = 1.0;
    b->setSpeed(-std::abs(s));  // Always negative!
}
```
- Calling reverse on an already-reversed buffer is a no-op (stays negative). This isn't tearing per se, but it means the scheduler can fire Reverse repeatedly without ever returning to forward playback.
- Combined with the fact that Reset clears speed to +1.0 via `resetToDefaults()`, this creates an asymmetric state machine: reverse can accumulate but never self-cancel.

---

## Root Causes Summary

| # | Root Cause | Combos Affected |
|---|-----------|----------------|
| RC-1 | No SoundTouch reset/flush when playback direction flips | A, C |
| RC-2 | Mode-transition crossfade doesn't handle SoundTouch priming underruns | B |
| RC-3 | SoundTouch receives input discontinuities (position jumps) on slice/loop boundaries | D |
| RC-4 | Speed magnitude change not smoothed when routed through SoundTouch tempo/rate | E |
| RC-5 | Crossfade position math doesn't account for SoundTouch rate transposition | F |
| RC-6 | No coordinated parameter snapshot — direction, speed, stretch, pitch read independently | A, C, E |
| RC-7 | `applyReverse` is one-directional, not a toggle | G |

---

## TODO List

### 🔴 P0 — Critical (most likely causing the audible tearing)

- [ ] **T1: Flush SoundTouch on direction change.**  
  In `AudioBuffer::setSpeed()`, detect when the sign of `params.speed` changes while the stretcher is active (`stretchRatio ≠ 1.0 || pitchSemiTones ≠ 0`). Set `stretcherNeedsReset = true` so the audio thread clears SoundTouch's internal buffers before feeding reversed samples. Add a short fade-in (reuse `stretchFadeInRemaining`) to mask the reset transient.  
  *File: AudioBuffer.cpp, `setSpeed()` ~line 400. ~15 lines.*

- [ ] **T2: Pre-prime SoundTouch after every reset to eliminate underruns.**  
  After `stretcher.reset()` in `processWithTimeStretch`, immediately feed `stretcher.getLatencySamples()` worth of input before entering the drain loop. The current code skips priming to save CPU, but the resulting underruns (1–2 blocks of partial silence) are the main source of tearing on mode transitions. The one-time priming cost is negligible compared to the artifacts it prevents.  
  *File: AudioBuffer.cpp, `processWithTimeStretch()` after the reset block ~line 505. ~20 lines.*

- [ ] **T3: Coordinate speed/stretch/pitch state changes atomically.**  
  Wrap `params.speed`, `stretchRatio`, and `pitchSemiTones` into a single struct written atomically (or use a `juce::SpinLock`-guarded snapshot). On the audio thread, read all three at once at the top of `processBlock` so the mode decision and the parameter values are always consistent. Currently, the modifier thread can write `speed` and `stretchRatio` in sequence, and the audio thread may see only one of the two changes.  
  *Files: AudioBuffer.h (add struct), AudioBuffer.cpp (read snapshot in processBlock). ~30 lines.*

### 🟠 P1 — High (significant improvement to tearing in common scenarios)

- [ ] **T4: Smooth speed-magnitude changes when routed through SoundTouch.**  
  Add a `SmoothedValue<double>` for the speed magnitude (similar to existing `stretchSmoother`). Use it when computing `clampedRate` and `tempoRatioForSt` so that abrupt speed changes (e.g., 1.0→2.0) ramp over ~50ms instead of jumping in one block. This prevents the "rate hiccup" when Speed fires while pitch shift is active.  
  *File: AudioBuffer.cpp, `processWithTimeStretch()` ~line 493. ~15 lines.*

- [ ] **T5: Clear SoundTouch pipeline on slice position jumps.**  
  In `fillInputScratch`, when the playhead jumps by more than a threshold (e.g., > `sequenceWindowSamples`), call `stretcher.reset()` and re-prime before continuing to feed. This prevents SoundTouch from splicing audio from two completely different source positions. Apply a micro-fade (~5ms) to the input to mask the discontinuity.  
  *File: AudioBuffer.cpp, `fillInputScratch` lambda, inside slice-trigger handling. ~25 lines.*

- [ ] **T6: Make `applyReverse` a proper toggle.**  
  Change `applyReverse` to flip the sign of speed (not always set negative). If `speed > 0`, set negative. If `speed < 0`, set positive. This makes Reverse a toggle and prevents it from being a no-op on already-reversed buffers.  
  *File: AppState.h, `applyReverse()`. ~3 lines.*

### 🟡 P2 — Medium (quality-of-life and edge-case fixes)

- [ ] **T7: Fix crossfade position computation when `useRate` is active.**  
  In `applyCrossfadeToSliceTransition`, when the stretch path is active AND `useRate` is true, the effective source-read speed should NOT include the rate factor (since SoundTouch handles rate internally). Currently the computation is correct for the stretch-only case but wrong when speed is routed through `setRate()`. Divide by `clampedRate` or branch on `useRate`.  
  *File: AudioBuffer.cpp, `applyCrossfadeToSliceTransition()` ~line 1140. ~8 lines.*

- [ ] **T8: Add a direction-change crossfade (not just mode-change).**  
  When playback direction reverses mid-stream (forward→reverse or reverse→forward), apply a short crossfade between the tail of the old direction and the head of the new direction. This is independent of the mode-transition crossfade. Can reuse the `previousBlockBuffer` mechanism: detect `direction != lastDirection` and blend.  
  *File: AudioBuffer.cpp, `processWithTimeStretch()` after the drain loop. ~20 lines.*

- [ ] **T9: Increase SoundTouch window sizes for reversed audio.**  
  SoundTouch's TDHS correlation works best with forward audio. When `direction < 0`, consider increasing `SETTING_SEEKWINDOW_MS` from 25→35 and `SETTING_OVERLAP_MS` from 12→18 to give the algorithm more room to find good overlap points in reversed waveforms. This requires re-calling `setSetting()` when direction changes, which pairs well with T1.  
  *File: AudioBuffer.cpp or TimeStretchSoundTouch.h. ~10 lines.*

- [ ] **T10: Add debug logging for SoundTouch state transitions.**  
  Log (via `DBG()`) whenever: mode switches (repitch↔stretch), direction flips, SoundTouch resets, underruns occur, or slice jumps happen while SoundTouch is active. Include playhead position, speed, stretchRatio, and pitchSemiTones. This will dramatically speed up future debugging.  
  *File: AudioBuffer.cpp, scattered. ~15 lines total.*

---

## Reproduction Recipes

| Recipe | Steps | Expected Artifact |
|--------|-------|-------------------|
| R1 | Load audio → Start playback → Apply Stretch 0.5x → Apply Reverse | Burst of phased/distorted audio on direction flip |
| R2 | Load audio → Start playback → Apply Pitch +12 → Apply Reverse | Same as R1 but with pitch shift active |
| R3 | Load audio → Start playback → Apply Speed 2.0x → Apply Stretch 0.5x | Brief silence gap + click on mode transition |
| R4 | Load audio → Stretch 0.5x → Enable continuous random slicing (16 slices) | Brief metallic artifacts at each slice boundary |
| R5 | Load audio → Apply Pitch +12 → Apply Speed 0.5x | Tempo hiccup (abrupt rate jump) |
| R6 | Load audio → Apply Reverse → Apply Reverse (again) | No toggle back (stays reversed) — not tearing, but wrong behavior |
| R7 | R1 or R3 → Apply ResetAll → observe second tear on stretch→repitch transition | Tear on both edges of the transition |

---

## Architecture Note

The fundamental tension is that SoundTouch is a **forward-streaming pipeline** (put samples in → get processed samples out) being used in a system that supports **instant, non-sequential input changes** (direction flips, position jumps, speed jumps). Every time the input stream is discontinuous, SoundTouch's internal state becomes invalid.

The long-term fix would be to treat SoundTouch's pipeline as something that must be **flushed and re-primed** on every input discontinuity, with a short crossfade to bridge the gap. T1, T2, and T5 together achieve this. T3 and T4 prevent the discontinuities from being worse than necessary.
