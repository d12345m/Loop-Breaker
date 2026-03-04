# SoundTouch Lookahead Pre-Priming: Feasibility Report

## Problem Statement

When SoundTouch-powered timestretching modifiers are combined with slice modifiers, audible **discontinuities** (clicks, tearing, brief silence) occur at slice boundaries. The problem is worse when slices jump rapidly (short slice durations, e.g. with `SliceRepeater` at high slice counts).

### Root Cause Chain

1. **Slice jump happens mid-stream**: Inside `fillInputScratch`'s per-sample loop, when the playhead reaches a slice boundary, it instantly jumps to the start of the next slice.

2. **Input-side crossfade helps but isn't enough**: An equal-power crossfade (minimum 50 ms in stretch mode) blends old and new source audio _before_ feeding SoundTouch. This smooths the raw discontinuity in the source waveform.

3. **SoundTouch's internal FIFO still contains pre-jump audio**: SoundTouch's TDStretch algorithm maintains an internal buffer of ~60 ms of input (SEQUENCE_MS=60, SEEKWINDOW_MS=25, OVERLAP_MS=12). At the moment of a slice jump, this buffer is full of audio from the _old_ slice position. The OLA (overlap-add) algorithm will attempt to correlate and splice old-position audio with new-position audio, producing poor matches that manifest as tearing.

4. **No pipeline flush by design**: The code explicitly avoids flushing SoundTouch on slice jumps (§11 comment) because a full `st.clear()` would be worse — it destroys the entire pipeline and requires re-priming, creating a guaranteed gap in output.

5. **Underfills compound the problem**: When SoundTouch's internal correlation fails (bad OLA matches), it can produce fewer output samples than requested, triggering the tail fade-to-zero logic and creating brief silent gaps.

---

## The Proposed Solution: Lookahead Pre-Priming

### Core Insight

In every slice mode, the system can know **which slice will play next** before the current slice finishes:

| Mode                | How next slice is known                                                                                                                                 |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **ArpSlice**        | `arpSequence[(arpSequencePos + 1) % arpSequence.size()]` — the sequence is pre-built and deterministic until the next refresh                           |
| **SliceRepeater**   | Same as ArpSlice — the sequence is all the same slice index, so the next is always known                                                                |
| **BeatSliceRandom** | The next slice is chosen by `random.nextInt()` at boundary time. However, we could **pre-select** it: roll the dice a few ms early and store the result |

### The Idea

Instead of waiting until the playhead crosses the slice boundary to begin feeding SoundTouch audio from the new position, we **start blending in** the next slice's audio into SoundTouch's input pipeline _before_ the boundary is reached. This gives SoundTouch's OLA algorithm time to "learn" the new audio's spectral characteristics before the hard transition occurs.

Concretely:

- When the playhead is within N samples of a slice boundary (where N ≈ SoundTouch's internal latency, ~2600 samples at 44.1 kHz), begin a **gradual crossfade** on the SoundTouch input from current-slice audio to next-slice audio.
- By the time the actual boundary arrives, SoundTouch's internal FIFO is already populated with a smooth transition, and its OLA algorithm has had time to find good correlation matches in the new material.

---

## Feasibility Analysis

### What Makes This Feasible

1. **Next-slice information IS available ahead of time for arp/repeater modes.** The `arpSequence` vector is fully populated and the position within it (`arpSequencePos`) is tracked. The next index is `arpSequence[(pos + 1) % size]`. No new infrastructure is needed to know the next slice index.

2. **For random mode, pre-rolling the RNG is trivial.** Instead of calling `random.nextInt()` at boundary time, call it when entering the "lookahead zone" and cache the result. The statistical properties are identical.

3. **The `fillInputScratch` lambda is already the right place.** It runs per-sample in its inner loop and already handles crossfade state (`isInCrossfade`, `crossfadePosition`, `previousSlicePlayheadPos`). Adding a "pre-crossfade" before the boundary would follow the same pattern.

4. **Slice start/end positions are cheap to compute.** `getSliceStartPosition` and `getSliceEndPosition` are simple division operations. Computing distance-to-boundary per sample is a single subtraction.

5. **No second SoundTouch instance needed.** This approach works _within_ the existing single-pipeline architecture by manipulating what gets fed into SoundTouch's input, not by running a parallel processor.

6. **SoundTouch's internal latency is queryable.** `stretcher.getLatencySamples()` returns the pipeline depth, which is exactly the ideal lookahead distance.

### What Makes This Non-Trivial

1. **The lookahead crossfade overlaps with the existing post-jump crossfade.** Currently, `isInCrossfade` is set _after_ a jump and fades from old→new. The new "pre-crossfade" would fade from current→next _before_ the jump. These two crossfade phases must be coordinated so they don't double-apply or create a volume dip.

2. **Effective speed affects boundary prediction.** The playhead advances at `inputSpeed * (fileSampleRate / hostSampleRate)` per sample. When speed changes (smoothed over 50 ms), the predicted boundary arrival time shifts. The lookahead zone calculation needs to use the _current_ effective step — not a constant.

3. **Reverse playback inverts the boundary.** In forward mode, the boundary is at `sliceEnd - 1.0`. In reverse, it's at `sliceStart`. The lookahead logic must handle both directions.

4. **Arp sequence refresh at cycle boundaries.** When `arpCycleCount >= arpTotalCyclesPerRefresh`, the sequence is regenerated. If the lookahead zone spans a refresh boundary, the "next slice" prediction could be wrong. This edge case needs special handling (either skip pre-priming across refresh boundaries, or pre-compute the first element of the new sequence).

5. **The fast paths (bulk copy, block resampling) bypass the per-sample loop.** These paths only activate when slicing is off (`!slicingModeActive.load()`), so they don't need modification. But this is worth verifying.

6. **Thread safety.** The `arpSequence` vector is mutated by UI-thread calls (`startArpSlicing`, etc.) and read by the audio thread. Currently this works because mutations only happen between `processBlock` calls (modifier triggers are dispatched on the message thread). The lookahead read-ahead of `arpSequence[(pos+1) % size]` follows the same access pattern, so no new synchronization is needed.

### Risk Assessment

| Risk                                            | Severity | Mitigation                                                                                                                             |
| ----------------------------------------------- | -------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| Double-crossfade (pre + post) causes volume dip | Medium   | Skip the post-jump crossfade when a pre-crossfade already ran to completion                                                            |
| Mispredicted next slice (random mode edge case) | Low      | If the predicted slice doesn't match the actual jump target, abort the pre-crossfade and fall back to the existing post-jump crossfade |
| CPU cost of per-sample boundary distance check  | Low      | Single subtraction + comparison; negligible vs. the interpolation already happening per sample                                         |
| Arp refresh boundary misprediction              | Low      | Detect refresh boundary in lookahead zone; use existing post-jump crossfade as fallback                                                |

---

## Recommended Architecture

### New State Variables (in `AudioBuffer`)

```
int   lookaheadNextSlice = -1;          // pre-computed next slice index (-1 = not yet determined)
bool  isInLookaheadCrossfade = false;   // currently in pre-boundary crossfade
int   lookaheadCrossfadePosition = 0;   // progress within lookahead crossfade
int   lookaheadCrossfadeSamples = 0;    // total length of lookahead crossfade
double lookaheadNextSliceReadPos = 0.0; // read position in the next slice (advances with step)
```

### Modified Flow in `fillInputScratch`

```
for each sample:
  1. [EXISTING] handleSlicePlaybackLocal()  — checks if boundary reached, does hard jump
  2. [NEW]      handleLookaheadLocal()       — checks distance to boundary, starts/advances pre-crossfade
  3. [EXISTING] interpolate current sample from source
  4. [EXISTING] if isInCrossfade: blend with old-position sample (post-jump crossfade)
  5. [NEW]      if isInLookaheadCrossfade: blend with next-slice sample (pre-jump crossfade)
  6. [EXISTING] advance position
```

### Key Behavioral Rules

1. **Lookahead zone entry**: When `distanceToBoundary <= lookaheadSamples` (where `lookaheadSamples ≈ stretcher.getLatencySamples()`), compute the next slice index and start the pre-crossfade.

2. **Pre-crossfade shape**: Linear or equal-power ramp from 0% next-slice to ~50% next-slice at the boundary. NOT a full 0→100% crossfade — the post-jump crossfade handles the final transition.

3. **Boundary arrival**: When the playhead reaches the actual boundary:
   - If a lookahead pre-crossfade was active: skip the hard jump's post-crossfade (the transition is already smooth), OR do a short post-crossfade only for the remaining 50%.
   - If no lookahead was active (e.g., external trigger): use existing post-jump crossfade as-is.

4. **Pre-crossfade cancellation**: If `lookaheadNextSlice` doesn't match the actual jump target (can happen if an external `triggerSlice()` overrides the natural sequence), abort the pre-crossfade and fall back to the existing mechanism.

---

## Implementation Checklist

### Phase 1: Next-Slice Prediction Infrastructure

- [ ] **1.1** Add `getNextSliceIndex()` method to `AudioBuffer`
  - For `arpSliceActive`: return `arpSequence[(arpSequencePos + 1) % arpSequence.size()]`
  - Handle arp cycle refresh boundary: if `(arpSequencePos + 1) >= arpSequence.size()` AND `(arpCycleCount + 1) >= arpTotalCyclesPerRefresh`, return `-1` (unpredictable — new sequence will be generated)
  - For `continuousRandomSlicing`: pre-roll `random.nextInt(params.numSlices)` into a new member `precomputedNextRandomSlice`, return it. Re-roll whenever it's consumed.
  - For `arpSliceRepeaterMode`: same as arp (the sequence is known)
  - For external triggers (`sliceTriggered` flag): return `-1` (unknown — external caller controls the target)

- [ ] **1.2** Add `getDistanceToSliceBoundary()` helper
  - Compute `sliceEnd - currentPos` (forward) or `currentPos - sliceStart` (reverse)
  - Return value in source samples (pre-resampling)
  - Account for effective speed to convert to "samples until boundary in the fillInputScratch domain"

- [ ] **1.3** Add new state variables to `AudioBuffer.h`
  - `int lookaheadNextSlice = -1`
  - `bool isInLookaheadCrossfade = false`
  - `int lookaheadCrossfadePosition = 0`
  - `int lookaheadCrossfadeSamples = 0`
  - `double lookaheadNextSliceReadPos = 0.0`
  - `int precomputedNextRandomSlice = -1`

### Phase 2: Lookahead Crossfade in fillInputScratch

- [ ] **2.1** Compute the lookahead distance at the top of `processWithTimeStretch`
  - `lookaheadSamples = stretcher.getLatencySamples()` — this represents SoundTouch's internal pipeline depth
  - Clamp to a reasonable range: `juce::jlimit(512, 4096, lookaheadSamples)`
  - Store as a local variable for use in the per-sample loop

- [ ] **2.2** Add `handleLookaheadLocal()` lambda inside `fillInputScratch`
  - Only active when `slicingOn && snap.useStretcher()`
  - Check distance to boundary each sample
  - When entering the lookahead zone (distance <= `lookaheadSamples`):
    - Call `getNextSliceIndex()` to get the predicted next slice
    - If result is `-1`, skip lookahead (unpredictable transition)
    - Otherwise, set `isInLookaheadCrossfade = true`, compute `lookaheadNextSliceReadPos` from `getSliceStartPosition(nextSlice)` (or end position if reversed)
    - Set `lookaheadCrossfadeSamples = min(distanceToBoundary, lookaheadSamples)`

- [ ] **2.3** Implement the per-sample lookahead blend
  - After the main interpolation and existing crossfade, add:
    ```
    if (isInLookaheadCrossfade):
        progress = lookaheadCrossfadePosition / lookaheadCrossfadeSamples
        blendFactor = progress * 0.5  // ramp from 0% to 50% next-slice by boundary
        read sample from lookaheadNextSliceReadPos (linear interp, same as main)
        output = output * (1 - blendFactor) + nextSample * blendFactor
        advance lookaheadNextSliceReadPos by step
        lookaheadCrossfadePosition++
        if lookaheadCrossfadePosition >= lookaheadCrossfadeSamples:
            isInLookaheadCrossfade = false
    ```

- [ ] **2.4** Coordinate with existing post-jump crossfade
  - In `handleSlicePlaybackLocal()`, when a boundary jump occurs:
    - If `lookaheadNextSlice == actualNextSlice` (prediction was correct):
      - Reduce `crossfadeLengthSamples` for this transition to half its normal value (the pre-crossfade already handled the first half)
      - OR skip the post-jump crossfade entirely and just let the pre-crossfade's momentum carry through
    - If prediction was wrong (`lookaheadNextSlice != actualNextSlice`):
      - Reset `isInLookaheadCrossfade = false`
      - Use full-length post-jump crossfade as before
    - Always reset `lookaheadNextSlice = -1` after a jump

### Phase 3: Random Mode Pre-Selection

- [ ] **3.1** Pre-compute next random slice index
  - In `handleSlicePlaybackLocal()`, after consuming a slice boundary (new random slice chosen), immediately pre-compute the _following_ random slice: `precomputedNextRandomSlice = random.nextInt(params.numSlices)`
  - This moves the RNG call from boundary-time to post-boundary-time, making it available for lookahead on the next cycle

- [ ] **3.2** Wire `getNextSliceIndex()` to use `precomputedNextRandomSlice`
  - For `continuousRandomSlicing` mode, return `precomputedNextRandomSlice` if it's valid (>= 0), else `-1`

- [ ] **3.3** Handle the initial case
  - When `startContinuousRandomSlicing()` or `triggerRandomSlice()` is called, also initialize `precomputedNextRandomSlice = random.nextInt(params.numSlices)` so the very first transition has lookahead available

### Phase 4: Edge Cases and Safety

- [ ] **4.1** Handle external `triggerSlice()` calls during lookahead
  - If `isInLookaheadCrossfade` is true and `sliceTriggered` flag is set from outside (UI or modifier), abort the lookahead crossfade immediately: `isInLookaheadCrossfade = false; lookaheadNextSlice = -1`
  - Let the normal post-jump crossfade handle the unexpected transition

- [ ] **4.2** Handle speed/direction changes during lookahead
  - If speed sign flips while `isInLookaheadCrossfade` is active, abort: the boundary is now on the opposite side
  - If speed magnitude changes significantly (smoother hasn't settled), the boundary distance prediction could be off — but the crossfade will still help. No special handling needed beyond what the existing smoother provides.

- [ ] **4.3** Handle arp sequence refresh during lookahead
  - `getNextSliceIndex()` already returns `-1` when the next position crosses a refresh boundary
  - Verify this works correctly: if `arpSequencePos + 1 >= arpSequence.size()` and a refresh will occur, the prediction is effectively "unknown" and lookahead is skipped

- [ ] **4.4** Reset lookahead state on mode transitions
  - In `exitSlicingMode()`, add: `isInLookaheadCrossfade = false; lookaheadNextSlice = -1; precomputedNextRandomSlice = -1`
  - In `processBlock` mode transition logic (repitch ↔ stretch), reset lookahead state
  - In `setPlaying(false)` / `stop()`, reset lookahead state

- [ ] **4.5** Ensure lookahead doesn't trigger on non-stretch path
  - The lookahead pre-crossfade should ONLY be active when `snap.useStretcher()` is true
  - `processWithRepitching` doesn't use SoundTouch and doesn't benefit from this — the existing input-side crossfade is sufficient there

### Phase 5: Testing and Tuning

- [ ] **5.1** Add debug logging for lookahead events
  - Log when entering lookahead zone: predicted next slice, distance to boundary, lookahead length
  - Log when prediction matches/mismatches actual jump
  - Log when lookahead is aborted (external trigger, speed flip, refresh boundary)

- [ ] **5.2** Add `TearingDebugStats` counters
  - `std::atomic<int> lookaheadPreCrossfades { 0 }` — successful pre-crossfades
  - `std::atomic<int> lookaheadMispredictions { 0 }` — prediction didn't match actual jump
  - `std::atomic<int> lookaheadAborts { 0 }` — aborted due to external trigger/speed change

- [ ] **5.3** Test with ArpSlice + Stretch combination
  - Verify discontinuity count drops significantly vs. baseline
  - Test with various stretch ratios (0.5, 0.75, 1.5, 2.0)
  - Test with various slice counts (4, 8, 16, 32)

- [ ] **5.4** Test with SliceRepeater + Stretch combination
  - Same parameter matrix as 5.3
  - Especially test rapid-fire repetitions (high slice count, short slices)

- [ ] **5.5** Test with BeatSliceRandom + Stretch combination
  - Verify pre-computed random slice index is consumed correctly
  - Verify RNG statistics are not biased by pre-rolling

- [ ] **5.6** Test with Pitch + Slice combinations
  - Particularly octave-up (doubles effective tempo ratio through SoundTouch)
  - Verify pre-crossfade length scales appropriately with pitch deviation

- [ ] **5.7** Test edge cases
  - Slice modifier triggered while lookahead is active
  - Speed reversal while lookahead is active
  - ResetAll modifier while lookahead is active
  - Arp sequence refresh during lookahead zone
  - Very short slices (< lookahead distance) — lookahead zone may span the entire slice

- [ ] **5.8** Tune the lookahead distance
  - Start with `stretcher.getLatencySamples()` as the baseline
  - If artifacts persist, try 1.5× or 2× the latency
  - If CPU cost is a concern with very long lookahead, clamp to 4096 samples (~93 ms at 44.1 kHz)

- [ ] **5.9** Tune the max blend factor
  - Start with 50% blend at boundary arrival (ramp 0→0.5 during pre-crossfade)
  - If this still produces a seam at boundary, try 60-70%
  - If this causes "pre-echo" (audible bleed of next slice before boundary), reduce to 30-40%

### Phase 6: Cleanup

- [ ] **6.1** Update the §11 comment in `handleSlicePlaybackLocal()`
  - Document the new lookahead pre-crossfade mechanism
  - Explain the interaction between pre-crossfade and post-crossfade

- [ ] **6.2** Consider making the lookahead distance configurable
  - Expose as a parameter in `AudioBufferParams` or `SessionSettings` for tuning
  - Default to `stretcher.getLatencySamples()` if not explicitly set

---

## Summary

**Feasibility: HIGH.** The infrastructure needed is modest — the arp sequence is already fully pre-built, the per-sample loop already processes crossfades, and SoundTouch's latency is queryable. The riskiest part is coordinating the pre-crossfade with the existing post-jump crossfade, but this can be mitigated with the fallback rule: if anything goes wrong, abort the pre-crossfade and let the existing mechanism handle it.

**Expected Impact:** The root cause of the worst artifacts is SoundTouch's OLA algorithm trying to correlate audio that spans a hard discontinuity in its internal FIFO. By gradually introducing the next slice's audio _before_ the boundary, the OLA algorithm will see a smooth spectral transition instead of a sudden one. This should significantly reduce both the severity and frequency of audible tearing at slice boundaries, and reduce underfills caused by poor OLA matches.

**What this WON'T fix:** Artifacts caused by extreme stretch ratios (>2×), direction flips, or mode transitions — those have separate causes and separate mitigations already in place.
